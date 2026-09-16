#include "hardware/windows/WindowsHardwareBackend.hpp"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define THERMVANE_NVAPI_CALL __stdcall
#else
#define THERMVANE_NVAPI_CALL
#endif

namespace thermvane {

namespace {

constexpr int kPowershellTimeoutMs = 3500;
constexpr qint64 kMinimumScanIntervalMs = 2500;

#if defined(_WIN32)
#pragma pack(push, 1)
struct HwinfoSharedHeader
{
    uint32_t signature = 0;
    uint32_t version = 0;
    uint32_t revision = 0;
    int64_t pollTime = 0;
    uint32_t sensorOffset = 0;
    uint32_t sensorSize = 0;
    uint32_t sensorCount = 0;
    uint32_t readingOffset = 0;
    uint32_t readingSize = 0;
    uint32_t readingCount = 0;
    uint32_t pollingPeriod = 0;
};

struct HwinfoSensorElement
{
    uint32_t id = 0;
    uint32_t instance = 0;
    char originalName[128] = {};
    char userName[128] = {};
};

struct HwinfoReadingElement
{
    uint32_t type = 0;
    uint32_t sensorIndex = 0;
    uint32_t id = 0;
    char originalLabel[128] = {};
    char userLabel[128] = {};
    char unit[16] = {};
    double value = 0.0;
    double minValue = 0.0;
    double maxValue = 0.0;
    double averageValue = 0.0;
};
#pragma pack(pop)
#endif

QString powershellExe()
{
    return QStringLiteral("powershell.exe");
}

QString powershellString(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

bool runPowershell(const QString &script, QString *output = nullptr)
{
    QProcess process;
    process.setProgram(powershellExe());
    process.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-Command"),
        script,
    });
    process.start();
    if (!process.waitForFinished(kPowershellTimeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return false;
    }

    if (output) {
        *output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    }
    return true;
}

bool runProcess(const QString &program, const QStringList &arguments, int timeoutMs = 2500, QString *output = nullptr)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return false;
    }

    if (output) {
        *output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    }
    return true;
}

QString findNbfcExecutable()
{
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("nbfc.exe"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    const QStringList programRoots = {
        qEnvironmentVariable("ProgramFiles"),
        qEnvironmentVariable("ProgramFiles(x86)"),
        qEnvironmentVariable("LOCALAPPDATA"),
    };
    const QStringList relativeCandidates = {
        QStringLiteral("NoteBook FanControl/nbfc.exe"),
        QStringLiteral("NoteBookFanControl/nbfc.exe"),
        QStringLiteral("NBFC/nbfc.exe"),
        QStringLiteral("nbfc/nbfc.exe"),
    };

    for (const QString &root : programRoots) {
        if (root.isEmpty()) {
            continue;
        }
        const QDir rootDir(root);
        for (const QString &relative : relativeCandidates) {
            const QString candidate = rootDir.filePath(relative);
            if (QFileInfo::exists(candidate)) {
                return candidate;
            }
        }
    }

    return {};
}

bool nbfcResponds(const QString &nbfcPath)
{
    if (nbfcPath.isEmpty()) {
        return false;
    }
    return runProcess(nbfcPath, {QStringLiteral("status")}, 2500);
}

QString collectWindowsHardwareScript()
{
    return QStringLiteral(
        "$ErrorActionPreference='SilentlyContinue';"
        "function Read-Cim($ns,$class,$props){"
        "  $items=Get-CimInstance -Namespace $ns -ClassName $class -ErrorAction SilentlyContinue;"
        "  if($null -eq $items){return @()};"
        "  return @($items|Select-Object -Property $props)"
        "};"
        "$data=[ordered]@{"
        "  lhmSensors=Read-Cim 'root\\LibreHardwareMonitor' 'Sensor' @('Identifier','Name','SensorType','Value');"
        "  lhmControls=Read-Cim 'root\\LibreHardwareMonitor' 'Control' @('Identifier','Name','ControlType','SoftwareValue','MinSoftwareValue','MaxSoftwareValue');"
        "  ohmSensors=Read-Cim 'root\\OpenHardwareMonitor' 'Sensor' @('Identifier','Name','SensorType','Value');"
        "  ohmControls=Read-Cim 'root\\OpenHardwareMonitor' 'Control' @('Identifier','Name','ControlType','SoftwareValue','MinSoftwareValue','MaxSoftwareValue');"
        "  acpiZones=Read-Cim 'root\\WMI' 'MSAcpi_ThermalZoneTemperature' @('InstanceName','CurrentTemperature');"
        "  temperatureProbes=Read-Cim 'root\\CIMV2' 'Win32_TemperatureProbe' @('Name','DeviceID','CurrentTemperature');"
        "  win32Fans=Read-Cim 'root\\CIMV2' 'Win32_Fan' @('Name','DeviceID','DesiredSpeed','VariableSpeed','ActiveCooling');"
        "  hpNumericSensors=Read-Cim 'root\\HP\\InstrumentedBIOS' 'HPBIOS_BIOSNumericSensor' @('Name','CurrentReading','SensorType','Description')"
        "};"
        "$data|ConvertTo-Json -Compress -Depth 4");
}

QJsonObject parseHardwareJson(const QString &output)
{
    if (output.isEmpty()) {
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    return document.object();
}

QJsonObject collectWindowsHardwareJson()
{
    QString output;
    if (!runPowershell(collectWindowsHardwareScript(), &output)) {
        return {};
    }
    return parseHardwareJson(output);
}

QJsonArray arrayValue(const QJsonObject &object, const QString &name)
{
    const QJsonValue value = object.value(name);
    if (value.isArray()) {
        return value.toArray();
    }
    if (value.isObject()) {
        return {value.toObject()};
    }
    return {};
}

QString slugify(const QString &rawValue)
{
    QString slug;
    slug.reserve(rawValue.size());

    for (const QChar character : rawValue.toLower()) {
        if (character.isLetterOrNumber()) {
            slug.append(character);
        } else if (!slug.endsWith(QLatin1Char('-'))) {
            slug.append(QLatin1Char('-'));
        }
    }

    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }

    return slug.isEmpty() ? QStringLiteral("device") : slug;
}

bool plausibleTemperature(double temperature)
{
    return std::isfinite(temperature) && temperature > 0.0 && temperature < 150.0;
}

bool plausibleRpm(double rpm)
{
    return std::isfinite(rpm) && rpm >= 0.0 && rpm < 30000.0;
}

double jsonNumber(const QJsonObject &object, const QString &name, double fallback = 0.0)
{
    const QJsonValue value = object.value(name);
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        if (ok) {
            return parsed;
        }
    }
    return fallback;
}

QString jsonString(const QJsonObject &object, const QString &name)
{
    return object.value(name).toString().trimmed();
}

QString sensorTypeName(const QJsonObject &object)
{
    const QJsonValue value = object.value(QStringLiteral("SensorType"));
    if (value.isString()) {
        return value.toString().trimmed();
    }

    if (!value.isDouble()) {
        return {};
    }

    switch (static_cast<int>(value.toDouble())) {
    case 2:
        return QStringLiteral("Temperature");
    case 4:
        return QStringLiteral("Fan");
    case 6:
        return QStringLiteral("Control");
    default:
        return {};
    }
}

QString titleCaseLabel(QString label)
{
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    label.replace(QLatin1Char('-'), QLatin1Char(' '));
    label = label.simplified();

    QStringList words = label.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString &word : words) {
        if (!word.isEmpty()) {
            word[0] = word.at(0).toUpper();
        }
    }

    return words.join(QLatin1Char(' '));
}

QString readableHardwareMonitorSensorName(const QString &name, const QString &identifier)
{
    const QString lowerIdentifier = identifier.toLower();
    const QString simplifiedName = name.simplified();

    if (lowerIdentifier.contains(QStringLiteral("/intelcpu/"))
        || lowerIdentifier.contains(QStringLiteral("/amdcpu/"))) {
        if (simplifiedName.contains(QStringLiteral("Package"), Qt::CaseInsensitive)) {
            return QStringLiteral("CPU Package");
        }
        if (simplifiedName.contains(QStringLiteral("Core"), Qt::CaseInsensitive)) {
            return QStringLiteral("CPU %1").arg(simplifiedName);
        }
        return simplifiedName.isEmpty() ? QStringLiteral("CPU Temperature") : QStringLiteral("CPU %1").arg(simplifiedName);
    }

    if (lowerIdentifier.contains(QStringLiteral("/nvidiagpu/"))) {
        return simplifiedName.isEmpty() ? QStringLiteral("NVIDIA GPU Core") : QStringLiteral("NVIDIA GPU %1").arg(simplifiedName);
    }

    if (lowerIdentifier.contains(QStringLiteral("/amdgpu/"))) {
        return simplifiedName.isEmpty() ? QStringLiteral("AMD GPU Core") : QStringLiteral("AMD GPU %1").arg(simplifiedName);
    }

    if (lowerIdentifier.contains(QStringLiteral("/hdd/"))) {
        return simplifiedName.isEmpty() ? QStringLiteral("Drive Temperature") : QStringLiteral("Drive %1").arg(simplifiedName);
    }

    return simplifiedName.isEmpty() ? QStringLiteral("Temperature Sensor") : simplifiedName;
}

QString readableHardwareMonitorFanName(const QString &name, const QString &identifier, int fallbackIndex)
{
    const QString lowerIdentifier = identifier.toLower();
    const QString simplifiedName = name.simplified();

    if (lowerIdentifier.contains(QStringLiteral("/nvidiagpu/"))) {
        return simplifiedName.isEmpty() ? QStringLiteral("NVIDIA GPU Fan") : QStringLiteral("NVIDIA GPU %1").arg(simplifiedName);
    }
    if (lowerIdentifier.contains(QStringLiteral("/amdgpu/"))) {
        return simplifiedName.isEmpty() ? QStringLiteral("AMD GPU Fan") : QStringLiteral("AMD GPU %1").arg(simplifiedName);
    }
    if (lowerIdentifier.contains(QStringLiteral("/intelcpu/"))
        || lowerIdentifier.contains(QStringLiteral("/amdcpu/"))) {
        return simplifiedName.isEmpty() ? QStringLiteral("CPU Fan") : QStringLiteral("CPU %1").arg(simplifiedName);
    }

    return simplifiedName.isEmpty() ? QStringLiteral("Fan %1").arg(fallbackIndex + 1) : simplifiedName;
}

QString parentIdentifier(const QString &identifier)
{
    const int lastSlash = identifier.lastIndexOf(QLatin1Char('/'));
    return lastSlash > 0 ? identifier.left(lastSlash) : identifier;
}

int trailingIndex(const QString &identifier)
{
    const QRegularExpression pattern(QStringLiteral("(\\d+)$"));
    const auto match = pattern.match(identifier);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

template<typename T>
void sortByName(QList<T> &items)
{
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(items.begin(), items.end(), [&collator](const T &left, const T &right) {
        return collator.compare(left.name, right.name) < 0;
    });
}

bool hasFanId(const QList<FanInfo> &fans, const QString &id)
{
    return std::any_of(fans.cbegin(), fans.cend(), [&](const FanInfo &fan) {
        return fan.id == id;
    });
}

void appendHardwareMonitorNamespace(const QString &namespaceName,
                                    const QJsonArray &sensorObjects,
                                    const QJsonArray &controlObjects,
                                    QList<SensorInfo> &sensors,
                                    QList<FanInfo> &fans,
                                    QHash<QString, WindowsHardwareBackend::WmiControlTarget> &controlsByFanId,
                                    const QHash<QString, double> &pendingManualFanSpeeds)
{
    QHash<QString, QJsonObject> controlsByParentAndIndex;
    QHash<QString, QJsonObject> controlsByParent;
    QSet<QString> usedControlIds;
    for (const QJsonValue &value : controlObjects) {
        const QJsonObject control = value.toObject();
        const QString identifier = jsonString(control, QStringLiteral("Identifier"));
        if (identifier.isEmpty()) {
            continue;
        }

        const QString parent = parentIdentifier(identifier);
        const int index = trailingIndex(identifier);
        if (index >= 0) {
            controlsByParentAndIndex.insert(QStringLiteral("%1#%2").arg(parent).arg(index), control);
        }
        controlsByParent.insert(parent, control);
    }

    int fanIndex = 0;
    for (const QJsonValue &value : sensorObjects) {
        const QJsonObject object = value.toObject();
        const QString sensorType = sensorTypeName(object);
        const QString identifier = jsonString(object, QStringLiteral("Identifier"));
        const QString name = jsonString(object, QStringLiteral("Name"));
        const double sensorValue = jsonNumber(object, QStringLiteral("Value"), std::numeric_limits<double>::quiet_NaN());
        if (identifier.isEmpty()) {
            continue;
        }

        if (sensorType.compare(QStringLiteral("Temperature"), Qt::CaseInsensitive) == 0
            && plausibleTemperature(sensorValue)) {
            SensorInfo sensor;
            sensor.id = QStringLiteral("windows-monitor-%1").arg(slugify(identifier));
            sensor.name = readableHardwareMonitorSensorName(name, identifier);
            sensor.temperatureCelsius = sensorValue;
            sensor.source = QStringLiteral("%1 %2").arg(namespaceName, identifier);
            sensor.available = true;
            sensors.append(std::move(sensor));
            continue;
        }

        if (sensorType.compare(QStringLiteral("Fan"), Qt::CaseInsensitive) != 0) {
            continue;
        }

        QJsonObject control;
        bool hasControl = false;
        const QString parent = parentIdentifier(identifier);
        const int index = trailingIndex(identifier);
        const QString exactKey = QStringLiteral("%1#%2").arg(parent).arg(index);
        if (controlsByParentAndIndex.contains(exactKey)) {
            control = controlsByParentAndIndex.value(exactKey);
            hasControl = true;
        } else if (controlsByParent.contains(parent)) {
            control = controlsByParent.value(parent);
            hasControl = true;
        }

        FanInfo fan;
        fan.id = QStringLiteral("windows-monitor-%1").arg(slugify(identifier));
        fan.name = readableHardwareMonitorFanName(name, identifier, fanIndex);
        fan.rpm = plausibleRpm(sensorValue) ? static_cast<int>(std::lround(sensorValue)) : 0;
        fan.speedPercent = pendingManualFanSpeeds.value(fan.id, 0.0);
        fan.automatic = !pendingManualFanSpeeds.contains(fan.id);
        fan.capabilities.rpmReading = plausibleRpm(sensorValue);
        fan.capabilities.manualControl = hasControl;
        fan.capabilities.firmwareControl = hasControl;
        fan.capabilities.zeroRpm = true;
        fan.capabilities.pwmControl = hasControl;

        if (hasControl) {
            const QString controlIdentifier = jsonString(control, QStringLiteral("Identifier"));
            controlsByFanId.insert(fan.id, {namespaceName, QStringLiteral("Control"), controlIdentifier});
            usedControlIds.insert(controlIdentifier);
        }

        fans.append(std::move(fan));
        ++fanIndex;
    }

    for (const QJsonValue &value : controlObjects) {
        const QJsonObject control = value.toObject();
        const QString identifier = jsonString(control, QStringLiteral("Identifier"));
        if (identifier.isEmpty() || usedControlIds.contains(identifier)) {
            continue;
        }

        const double softwareValue = jsonNumber(control, QStringLiteral("SoftwareValue"), 0.0);
        FanInfo fan;
        fan.id = QStringLiteral("windows-control-%1").arg(slugify(identifier));
        fan.name = readableHardwareMonitorFanName(jsonString(control, QStringLiteral("Name")), identifier, fanIndex);
        fan.rpm = 0;
        fan.speedPercent = pendingManualFanSpeeds.value(fan.id, std::clamp(softwareValue, 0.0, 100.0));
        fan.automatic = !pendingManualFanSpeeds.contains(fan.id);
        fan.capabilities.rpmReading = false;
        fan.capabilities.manualControl = true;
        fan.capabilities.firmwareControl = true;
        fan.capabilities.zeroRpm = true;
        fan.capabilities.pwmControl = true;
        controlsByFanId.insert(fan.id, {namespaceName, QStringLiteral("Control"), identifier});
        fans.append(std::move(fan));
        ++fanIndex;
    }
}

void appendAcpiThermalZones(const QJsonArray &zones, QList<SensorInfo> &sensors)
{
    for (int index = 0; index < zones.size(); ++index) {
        const QJsonObject zone = zones.at(index).toObject();
        const double deciKelvin = jsonNumber(zone, QStringLiteral("CurrentTemperature"), 0.0);
        const double celsius = (deciKelvin / 10.0) - 273.15;
        if (!plausibleTemperature(celsius)) {
            continue;
        }

        const QString rawName = jsonString(zone, QStringLiteral("InstanceName"));
        const QString name = rawName.isEmpty() ? QStringLiteral("ACPI Thermal Zone %1").arg(index + 1) : rawName;
        SensorInfo sensor;
        sensor.id = QStringLiteral("windows-acpi-%1").arg(slugify(name));
        sensor.name = name.startsWith(QStringLiteral("ACPI"), Qt::CaseInsensitive)
            ? name
            : QStringLiteral("ACPI %1").arg(titleCaseLabel(name));
        sensor.temperatureCelsius = celsius;
        sensor.source = QStringLiteral("root\\WMI MSAcpi_ThermalZoneTemperature");
        sensor.available = true;
        sensors.append(std::move(sensor));
    }
}

void appendWindowsTemperatureProbes(const QJsonArray &probes, QList<SensorInfo> &sensors)
{
    for (int index = 0; index < probes.size(); ++index) {
        const QJsonObject probe = probes.at(index).toObject();
        const double raw = jsonNumber(probe, QStringLiteral("CurrentTemperature"), 0.0);
        const double celsius = raw > 200.0 ? (raw / 10.0) - 273.15 : raw;
        if (!plausibleTemperature(celsius)) {
            continue;
        }

        const QString name = jsonString(probe, QStringLiteral("Name")).isEmpty()
            ? QStringLiteral("Windows Temperature Probe %1").arg(index + 1)
            : jsonString(probe, QStringLiteral("Name"));
        SensorInfo sensor;
        sensor.id = QStringLiteral("windows-probe-%1").arg(slugify(name));
        sensor.name = name;
        sensor.temperatureCelsius = celsius;
        sensor.source = QStringLiteral("root\\CIMV2 Win32_TemperatureProbe");
        sensor.available = true;
        sensors.append(std::move(sensor));
    }
}

void appendWin32Fans(const QJsonArray &fanObjects, QList<FanInfo> &fans)
{
    for (int index = 0; index < fanObjects.size(); ++index) {
        const QJsonObject object = fanObjects.at(index).toObject();
        const QString deviceId = jsonString(object, QStringLiteral("DeviceID"));
        const QString name = jsonString(object, QStringLiteral("Name")).isEmpty()
            ? QStringLiteral("Windows Laptop Fan %1").arg(index + 1)
            : jsonString(object, QStringLiteral("Name"));
        const double desiredSpeed = jsonNumber(object, QStringLiteral("DesiredSpeed"), 0.0);
        const bool hasRpm = plausibleRpm(desiredSpeed) && desiredSpeed > 0.0;
        const bool hasFirmwareSignal = object.value(QStringLiteral("ActiveCooling")).toBool(false)
            || object.value(QStringLiteral("VariableSpeed")).toBool(false);
        if (!hasRpm && !hasFirmwareSignal) {
            continue;
        }

        FanInfo fan;
        fan.id = QStringLiteral("windows-win32fan-%1").arg(slugify(deviceId.isEmpty() ? name : deviceId));
        fan.name = name;
        fan.rpm = plausibleRpm(desiredSpeed) ? static_cast<int>(std::lround(desiredSpeed)) : 0;
        fan.speedPercent = 0.0;
        fan.automatic = true;
        fan.capabilities.rpmReading = hasRpm;
        fan.capabilities.manualControl = false;
        fan.capabilities.firmwareControl = hasFirmwareSignal;
        fan.capabilities.zeroRpm = false;
        fan.capabilities.pwmControl = false;
        if (!hasFanId(fans, fan.id)) {
            fans.append(std::move(fan));
        }
    }
}

void appendHpInstrumentedBiosSensors(const QJsonArray &sensorObjects, QList<SensorInfo> &sensors, QList<FanInfo> &fans)
{
    for (const QJsonValue &value : sensorObjects) {
        const QJsonObject object = value.toObject();
        const QString name = jsonString(object, QStringLiteral("Name"));
        const QString description = jsonString(object, QStringLiteral("Description"));
        const QString type = jsonString(object, QStringLiteral("SensorType"));
        const QString label = name.isEmpty() ? description : name;
        const QString normalized = QStringLiteral("%1 %2 %3").arg(name, description, type).toLower();
        const double reading = jsonNumber(object, QStringLiteral("CurrentReading"), std::numeric_limits<double>::quiet_NaN());
        if (label.isEmpty() || !std::isfinite(reading)) {
            continue;
        }

        if (normalized.contains(QStringLiteral("fan")) && plausibleRpm(reading) && reading > 0.0) {
            FanInfo fan;
            fan.id = QStringLiteral("windows-hp-bios-%1-fan").arg(slugify(label));
            fan.name = label;
            fan.rpm = static_cast<int>(std::lround(reading));
            fan.speedPercent = 0.0;
            fan.automatic = true;
            fan.capabilities.rpmReading = true;
            fan.capabilities.manualControl = false;
            fan.capabilities.firmwareControl = true;
            fan.capabilities.zeroRpm = true;
            fan.capabilities.pwmControl = false;
            if (!hasFanId(fans, fan.id)) {
                fans.append(std::move(fan));
            }
            continue;
        }

        if ((normalized.contains(QStringLiteral("temp")) || normalized.contains(QStringLiteral("thermal")))
            && plausibleTemperature(reading)) {
            SensorInfo sensor;
            sensor.id = QStringLiteral("windows-hp-bios-%1-temp").arg(slugify(label));
            sensor.name = label;
            sensor.temperatureCelsius = reading;
            sensor.source = QStringLiteral("HP InstrumentedBIOS");
            sensor.available = true;
            sensors.append(std::move(sensor));
        }
    }
}

void appendNvidiaSmi(QList<SensorInfo> &sensors, QList<FanInfo> &fans)
{
    QProcess process;
    process.setProgram(QStringLiteral("nvidia-smi"));
    process.setArguments({
        QStringLiteral("--query-gpu=name,uuid,pci.bus_id,temperature.gpu,fan.speed"),
        QStringLiteral("--format=csv,noheader,nounits"),
    });
    process.start();
    if (!process.waitForFinished(1200)
        || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        process.kill();
        return;
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const QStringList fields = lines.at(index).split(QLatin1Char(','));
        if (fields.size() < 5) {
            continue;
        }

        const QString name = fields.mid(0, fields.size() - 4).join(QLatin1Char(',')).trimmed();
        const QString uuid = fields.at(fields.size() - 4).trimmed();
        const QString busId = fields.at(fields.size() - 3).trimmed();
        const QString idBase = !uuid.isEmpty() && uuid != QStringLiteral("[N/A]")
            ? uuid
            : (!busId.isEmpty() && busId != QStringLiteral("[N/A]") ? busId : QStringLiteral("gpu%1").arg(index));

        bool temperatureOk = false;
        const double temperature = fields.at(fields.size() - 2).trimmed().toDouble(&temperatureOk);
        if (temperatureOk && plausibleTemperature(temperature)) {
            SensorInfo sensor;
            sensor.id = QStringLiteral("windows-nvidia-smi-%1-core").arg(slugify(idBase));
            sensor.name = name.isEmpty() || name == QStringLiteral("[N/A]")
                ? QStringLiteral("NVIDIA GPU Core")
                : QStringLiteral("%1 Core").arg(name);
            sensor.temperatureCelsius = temperature;
            sensor.source = busId.isEmpty() || busId == QStringLiteral("[N/A]")
                ? QStringLiteral("nvidia-smi")
                : QStringLiteral("nvidia-smi %1").arg(busId);
            sensor.available = true;
            sensors.append(std::move(sensor));
        }

        bool fanPercentOk = false;
        const double fanPercent = fields.at(fields.size() - 1).trimmed().remove(QLatin1Char('%')).toDouble(&fanPercentOk);
        if (fanPercentOk && fanPercent >= 0.0 && fanPercent <= 100.0) {
            FanInfo fan;
            fan.id = QStringLiteral("windows-nvidia-smi-%1-fan").arg(slugify(idBase));
            fan.name = name.isEmpty() || name == QStringLiteral("[N/A]")
                ? QStringLiteral("NVIDIA GPU Fan")
                : QStringLiteral("%1 Fan").arg(name);
            fan.rpm = 0;
            fan.speedPercent = fanPercent;
            fan.automatic = true;
            fan.capabilities.rpmReading = false;
            fan.capabilities.manualControl = false;
            fan.capabilities.firmwareControl = true;
            fan.capabilities.zeroRpm = true;
            fan.capabilities.pwmControl = false;
            if (!hasFanId(fans, fan.id)) {
                fans.append(std::move(fan));
            }
        }
    }
}

void appendNvapiFans(QList<FanInfo> &fans)
{
#if defined(_WIN32)
    QLibrary nvapi(QStringLiteral("nvapi64"));
    if (!nvapi.load()) {
        nvapi.setFileName(QStringLiteral("nvapi"));
        if (!nvapi.load()) {
            return;
        }
    }

    using QueryInterface = void *(THERMVANE_NVAPI_CALL *)(unsigned int);
    using Initialize = int (THERMVANE_NVAPI_CALL *)();
    using EnumPhysicalGpus = int (THERMVANE_NVAPI_CALL *)(void **, unsigned int *);
    using GpuGetFullName = int (THERMVANE_NVAPI_CALL *)(void *, char *);
    using GpuGetTachReading = int (THERMVANE_NVAPI_CALL *)(void *, unsigned int *);

    const auto queryInterface = reinterpret_cast<QueryInterface>(nvapi.resolve("nvapi_QueryInterface"));
    if (!queryInterface) {
        return;
    }

    const auto initialize = reinterpret_cast<Initialize>(queryInterface(0x0150E828));
    const auto enumPhysicalGpus = reinterpret_cast<EnumPhysicalGpus>(queryInterface(0xE5AC921F));
    const auto getFullName = reinterpret_cast<GpuGetFullName>(queryInterface(0xCEEE8E9F));
    const auto getTachReading = reinterpret_cast<GpuGetTachReading>(queryInterface(0x5F608315));
    if (!initialize || !enumPhysicalGpus || !getTachReading || initialize() != 0) {
        return;
    }

    void *handles[64] = {};
    unsigned int gpuCount = 0;
    if (enumPhysicalGpus(handles, &gpuCount) != 0) {
        return;
    }

    gpuCount = std::min(gpuCount, 64U);
    for (unsigned int index = 0; index < gpuCount; ++index) {
        if (!handles[index]) {
            continue;
        }

        unsigned int rpm = 0;
        if (getTachReading(handles[index], &rpm) != 0 || rpm == 0 || rpm >= 30000) {
            continue;
        }

        QString name = QStringLiteral("NVIDIA GPU");
        if (getFullName) {
            char rawName[64] = {};
            if (getFullName(handles[index], rawName) == 0 && rawName[0] != '\0') {
                name = QString::fromLocal8Bit(rawName).trimmed();
            }
        }

        FanInfo fan;
        fan.id = QStringLiteral("windows-nvapi-gpu%1-fan").arg(index);
        fan.name = QStringLiteral("%1 Fan").arg(name);
        fan.rpm = static_cast<int>(rpm);
        fan.speedPercent = 0.0;
        fan.automatic = true;
        fan.capabilities.rpmReading = true;
        fan.capabilities.manualControl = false;
        fan.capabilities.firmwareControl = true;
        fan.capabilities.zeroRpm = true;
        fan.capabilities.pwmControl = false;
        if (!hasFanId(fans, fan.id)) {
            fans.append(std::move(fan));
        }
    }
#else
    Q_UNUSED(fans)
#endif
}

QString fixedAsciiString(const char *data, qsizetype maxLength)
{
    const qsizetype length = qstrnlen(data, maxLength);
    return QString::fromLocal8Bit(data, length).trimmed();
}

void appendHwinfoSharedMemory(QList<SensorInfo> &sensors, QList<FanInfo> &fans)
{
#if defined(_WIN32)
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\HWiNFO_SENS_SM2");
    if (!mapping) {
        return;
    }

    void *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(mapping);
        return;
    }

    MEMORY_BASIC_INFORMATION memoryInfo = {};
    const SIZE_T querySize = VirtualQuery(view, &memoryInfo, sizeof(memoryInfo));
    const size_t mappedSize = querySize == sizeof(memoryInfo) ? memoryInfo.RegionSize : 0;
    const auto *base = static_cast<const unsigned char *>(view);
    const auto *header = reinterpret_cast<const HwinfoSharedHeader *>(base);
    constexpr uint32_t hwinfoSignature = 0x53695748; // "HWiS"

    const auto rangeValid = [&](uint32_t offset, uint32_t elementSize, uint32_t count) {
        if (mappedSize == 0 || elementSize == 0 || count > 4096) {
            return false;
        }
        const size_t start = offset;
        const size_t bytes = static_cast<size_t>(elementSize) * static_cast<size_t>(count);
        return start <= mappedSize && bytes <= mappedSize - start;
    };

    if (header->signature == hwinfoSignature
        && rangeValid(header->sensorOffset, header->sensorSize, header->sensorCount)
        && rangeValid(header->readingOffset, header->readingSize, header->readingCount)) {
        for (uint32_t index = 0; index < header->readingCount; ++index) {
            const auto *reading = reinterpret_cast<const HwinfoReadingElement *>(
                base + header->readingOffset + static_cast<size_t>(header->readingSize) * index);
            if (reading->sensorIndex >= header->sensorCount || !std::isfinite(reading->value)) {
                continue;
            }

            const auto *sensor = reinterpret_cast<const HwinfoSensorElement *>(
                base + header->sensorOffset + static_cast<size_t>(header->sensorSize) * reading->sensorIndex);
            const QString sensorName = fixedAsciiString(sensor->userName, sizeof(sensor->userName)).isEmpty()
                ? fixedAsciiString(sensor->originalName, sizeof(sensor->originalName))
                : fixedAsciiString(sensor->userName, sizeof(sensor->userName));
            const QString label = fixedAsciiString(reading->userLabel, sizeof(reading->userLabel)).isEmpty()
                ? fixedAsciiString(reading->originalLabel, sizeof(reading->originalLabel))
                : fixedAsciiString(reading->userLabel, sizeof(reading->userLabel));
            const QString unit = fixedAsciiString(reading->unit, sizeof(reading->unit));
            const QString displayName = sensorName.isEmpty()
                ? label
                : (label.isEmpty() ? sensorName : QStringLiteral("%1 %2").arg(sensorName, label));
            const QString idBase = QStringLiteral("%1-%2-%3")
                                       .arg(sensor->id)
                                       .arg(sensor->instance)
                                       .arg(reading->id);

            if (reading->type == 1 && plausibleTemperature(reading->value)) {
                SensorInfo info;
                info.id = QStringLiteral("windows-hwinfo-%1-temp").arg(slugify(idBase));
                info.name = displayName.isEmpty() ? QStringLiteral("HWiNFO Temperature") : displayName;
                info.temperatureCelsius = reading->value;
                info.source = QStringLiteral("HWiNFO Shared Memory%1").arg(unit.isEmpty() ? QString() : QStringLiteral(" %1").arg(unit));
                info.available = true;
                sensors.append(std::move(info));
            } else if (reading->type == 3 && plausibleRpm(reading->value)) {
                FanInfo fan;
                fan.id = QStringLiteral("windows-hwinfo-%1-fan").arg(slugify(idBase));
                fan.name = displayName.isEmpty() ? QStringLiteral("HWiNFO Fan") : displayName;
                fan.rpm = static_cast<int>(std::lround(reading->value));
                fan.speedPercent = 0.0;
                fan.automatic = true;
                fan.capabilities.rpmReading = true;
                fan.capabilities.manualControl = false;
                fan.capabilities.firmwareControl = true;
                fan.capabilities.zeroRpm = true;
                fan.capabilities.pwmControl = false;
                if (!hasFanId(fans, fan.id)) {
                    fans.append(std::move(fan));
                }
            }
        }
    }

    UnmapViewOfFile(view);
    CloseHandle(mapping);
#else
    Q_UNUSED(sensors)
    Q_UNUSED(fans)
#endif
}

QHash<QString, int> nbfcFanIndexesForFans(QList<FanInfo> &fans, bool nbfcAvailable)
{
    QHash<QString, int> indexes;
    if (!nbfcAvailable) {
        return indexes;
    }

    int nextGenericIndex = 0;
    for (FanInfo &fan : fans) {
        const QString name = fan.name.toLower();
        int nbfcIndex = -1;
        if (name.contains(QStringLiteral("cpu"))) {
            nbfcIndex = 0;
        } else if (name.contains(QStringLiteral("gpu"))) {
            nbfcIndex = 1;
        } else {
            nbfcIndex = nextGenericIndex;
        }

        nextGenericIndex = std::max(nextGenericIndex, nbfcIndex + 1);
        indexes.insert(fan.id, nbfcIndex);
        fan.capabilities.manualControl = true;
        fan.capabilities.firmwareControl = true;
        fan.capabilities.pwmControl = true;
    }
    return indexes;
}

bool writeHardwareMonitorControl(const WindowsHardwareBackend::WmiControlTarget &target, double *percent)
{
    const QString valueAssignment = percent
        ? QStringLiteral("$control.SoftwareValue=%1;").arg(QString::number(std::clamp(*percent, 0.0, 100.0), 'f', 0))
        : QStringLiteral("$control.SoftwareValue=$null;");
    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "$control=Get-CimInstance -Namespace %1 -ClassName %2 -ErrorAction Stop|"
        "Where-Object{$_.Identifier -eq %3}|Select-Object -First 1;"
        "if($null -eq $control){exit 2};"
        "%4"
        "Set-CimInstance -InputObject $control -ErrorAction Stop")
                               .arg(powershellString(target.namespaceName),
                                    powershellString(target.className),
                                    powershellString(target.identifier),
                                    valueAssignment);
    return runPowershell(script);
}

} // namespace

WindowsHardwareBackend::WindowsHardwareBackend(QObject *parent)
    : IHardwareBackend(parent)
{
    startNbfcService();

    const QCoreApplication *application = QCoreApplication::instance();
    if (!application || !application->inherits("QGuiApplication")) {
        applyHardwareSnapshot(collectWindowsHardwareJson());
    }
}

void WindowsHardwareBackend::startNbfcService()
{
    if (m_nbfcStartAttempted) {
        return;
    }

    m_nbfcStartAttempted = true;
    m_nbfcPath = findNbfcExecutable();
    if (m_nbfcPath.isEmpty()) {
        return;
    }

    const QCoreApplication *application = QCoreApplication::instance();
    const bool async = application && application->inherits("QGuiApplication");
    const QString serviceName = QStringLiteral("NoteBookFanControlService");

    if (!async) {
        runProcess(QStringLiteral("sc.exe"), {QStringLiteral("start"), serviceName}, 5000);
        runProcess(m_nbfcPath, {QStringLiteral("start"), QStringLiteral("-e")}, 5000);
        return;
    }

    QProcess *serviceProcess = new QProcess(this);
    serviceProcess->setProgram(QStringLiteral("sc.exe"));
    serviceProcess->setArguments({QStringLiteral("start"), serviceName});
    connect(serviceProcess, &QProcess::finished, serviceProcess, &QObject::deleteLater);
    QTimer::singleShot(5000, serviceProcess, [serviceProcess] {
        if (serviceProcess->state() != QProcess::NotRunning) {
            serviceProcess->kill();
        }
    });
    serviceProcess->start();

    QTimer::singleShot(750, this, [this] {
        if (m_nbfcPath.isEmpty()) {
            return;
        }

        QProcess *nbfcProcess = new QProcess(this);
        nbfcProcess->setProgram(m_nbfcPath);
        nbfcProcess->setArguments({QStringLiteral("start"), QStringLiteral("-e")});
        connect(nbfcProcess, &QProcess::finished, this, [this, nbfcProcess] {
            nbfcProcess->deleteLater();
            scan();
        });
        QTimer::singleShot(5000, nbfcProcess, [nbfcProcess] {
            if (nbfcProcess->state() != QProcess::NotRunning) {
                nbfcProcess->kill();
            }
        });
        nbfcProcess->start();
    });
}

void WindowsHardwareBackend::scan()
{
    if (m_lastScanTimer.isValid() && m_lastScanTimer.elapsed() < kMinimumScanIntervalMs) {
        return;
    }
    if (m_scanRunning) {
        return;
    }

    m_scanRunning = true;
    QProcess *process = new QProcess(this);
    process->setProgram(powershellExe());
    process->setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-Command"),
        collectWindowsHardwareScript(),
    });

    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        m_scanRunning = false;
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            applyHardwareSnapshot(parseHardwareJson(QString::fromUtf8(process->readAllStandardOutput()).trimmed()));
        } else {
            m_lastScanTimer.restart();
        }
        process->deleteLater();
    });
    QTimer::singleShot(kPowershellTimeoutMs, process, [process] {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });

    process->start();
    if (!process->waitForStarted(250)) {
        m_scanRunning = false;
        process->deleteLater();
        m_lastScanTimer.restart();
    }
}

void WindowsHardwareBackend::applyHardwareSnapshot(const QJsonObject &hardware)
{
    QList<SensorInfo> nextSensors;
    QList<FanInfo> nextFans;
    QHash<QString, WmiControlTarget> nextControls;

    appendHardwareMonitorNamespace(QStringLiteral("root\\LibreHardwareMonitor"),
                                   arrayValue(hardware, QStringLiteral("lhmSensors")),
                                   arrayValue(hardware, QStringLiteral("lhmControls")),
                                   nextSensors,
                                   nextFans,
                                   nextControls,
                                   m_pendingManualFanSpeeds);
    appendHardwareMonitorNamespace(QStringLiteral("root\\OpenHardwareMonitor"),
                                   arrayValue(hardware, QStringLiteral("ohmSensors")),
                                   arrayValue(hardware, QStringLiteral("ohmControls")),
                                   nextSensors,
                                   nextFans,
                                   nextControls,
                                   m_pendingManualFanSpeeds);
    appendAcpiThermalZones(arrayValue(hardware, QStringLiteral("acpiZones")), nextSensors);
    appendWindowsTemperatureProbes(arrayValue(hardware, QStringLiteral("temperatureProbes")), nextSensors);
    appendWin32Fans(arrayValue(hardware, QStringLiteral("win32Fans")), nextFans);
    appendHpInstrumentedBiosSensors(arrayValue(hardware, QStringLiteral("hpNumericSensors")), nextSensors, nextFans);
    appendNvidiaSmi(nextSensors, nextFans);
    appendNvapiFans(nextFans);
    appendHwinfoSharedMemory(nextSensors, nextFans);

    m_nbfcPath = findNbfcExecutable();
    const bool nbfcAvailable = nbfcResponds(m_nbfcPath);
    m_nbfcFanIndexesByFanId = nbfcFanIndexesForFans(nextFans, nbfcAvailable);

    sortByName(nextSensors);
    sortByName(nextFans);

    m_sensors = std::move(nextSensors);
    m_fans = std::move(nextFans);
    m_controlsByFanId = std::move(nextControls);
    m_lastScanTimer.restart();

    emit hardwareChanged();
}

QList<SensorInfo> WindowsHardwareBackend::sensors() const
{
    return m_sensors;
}

QList<FanInfo> WindowsHardwareBackend::fans() const
{
    return m_fans;
}

bool WindowsHardwareBackend::setFanSpeed(const QString &fanId, double percent)
{
    const double clampedPercent = std::clamp(percent, 0.0, 100.0);
    const auto controlIt = m_controlsByFanId.constFind(fanId);
    if (controlIt != m_controlsByFanId.cend()) {
        double targetPercent = clampedPercent;
        if (!writeHardwareMonitorControl(controlIt.value(), &targetPercent)) {
            return false;
        }
    } else {
        const auto nbfcIt = m_nbfcFanIndexesByFanId.constFind(fanId);
        if (nbfcIt == m_nbfcFanIndexesByFanId.cend() || m_nbfcPath.isEmpty()) {
            return false;
        }

        if (!runProcess(m_nbfcPath, {
                QStringLiteral("set"),
                QStringLiteral("-f"),
                QString::number(nbfcIt.value()),
                QStringLiteral("-s"),
                QString::number(static_cast<int>(std::lround(clampedPercent))),
            }, 3000)) {
            return false;
        }
    }

    m_pendingManualFanSpeeds.insert(fanId, clampedPercent);
    for (FanInfo &fan : m_fans) {
        if (fan.id == fanId) {
            fan.speedPercent = clampedPercent;
            fan.automatic = false;
            break;
        }
    }
    emit hardwareChanged();
    return true;
}

bool WindowsHardwareBackend::restoreAutomaticControl(const QString &fanId)
{
    const auto controlIt = m_controlsByFanId.constFind(fanId);
    if (controlIt != m_controlsByFanId.cend()) {
        if (!writeHardwareMonitorControl(controlIt.value(), nullptr)) {
            return false;
        }
    } else {
        const auto nbfcIt = m_nbfcFanIndexesByFanId.constFind(fanId);
        if (nbfcIt == m_nbfcFanIndexesByFanId.cend() || m_nbfcPath.isEmpty()) {
            return false;
        }

        if (!runProcess(m_nbfcPath, {
                QStringLiteral("set"),
                QStringLiteral("-f"),
                QString::number(nbfcIt.value()),
                QStringLiteral("-a"),
            }, 3000)) {
            return false;
        }
    }

    m_pendingManualFanSpeeds.remove(fanId);
    for (FanInfo &fan : m_fans) {
        if (fan.id == fanId) {
            fan.automatic = true;
            break;
        }
    }
    emit hardwareChanged();
    return true;
}

} // namespace thermvane
