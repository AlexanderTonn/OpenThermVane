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

QString powershellExe()
{
    return QStringLiteral("powershell.exe");
}

QString powershellString(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

void hideProcessConsoleWindow(QProcess &process)
{
#if defined(_WIN32)
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
        arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        arguments->startupInfo->wShowWindow = SW_HIDE;
    });
#else
    Q_UNUSED(process)
#endif
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
    hideProcessConsoleWindow(process);
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
        const QString standardOutput = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        const QString standardError = QString::fromUtf8(process.readAllStandardError()).trimmed();
        *output = standardError.isEmpty()
            ? standardOutput
            : QStringLiteral("%1\n%2").arg(standardOutput, standardError).trimmed();
    }
    return true;
}

bool runProcess(const QString &program, const QStringList &arguments, int timeoutMs = 2500, QString *output = nullptr)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    hideProcessConsoleWindow(process);
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

bool nbfcOutputContainsFailure(const QString &output)
{
    const QString normalized = output.toLower();
    return normalized.contains(QStringLiteral("could not"))
        || normalized.contains(QStringLiteral("failed"))
        || normalized.contains(QStringLiteral("invalid"))
        || normalized.contains(QStringLiteral("unavailable"))
        || normalized.contains(QStringLiteral("timed out"))
        || normalized.contains(QStringLiteral("no config"));
}

bool runNbfcCommand(const QString &nbfcPath, const QStringList &arguments, int timeoutMs = 5000, QString *output = nullptr)
{
    QString commandOutput;
    if (!runProcess(nbfcPath, arguments, timeoutMs, &commandOutput)) {
        return false;
    }

    if (output) {
        *output = commandOutput;
    }
    return !nbfcOutputContainsFailure(commandOutput);
}

QString valueAfterColon(const QString &line)
{
    return line.section(QLatin1Char(':'), 1).trimmed();
}

QString selectedNbfcConfigName(const QString &statusOutput)
{
    const QStringList lines = statusOutput.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("Selected config name"), Qt::CaseInsensitive)) {
            return valueAfterColon(line);
        }
    }
    return {};
}

QString firstNonEmptyLine(const QString &output)
{
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!line.isEmpty()) {
            return line;
        }
    }
    return {};
}

struct NbfcFanStatus
{
    int index = -1;
    QString name;
    double currentPercent = 0.0;
    double targetPercent = 0.0;
    bool automatic = true;
};

bool plausibleTemperature(double temperature);

bool lineBoolValue(const QString &line, bool fallback)
{
    const QString value = line.section(QLatin1Char(':'), 1).trimmed();
    if (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    return fallback;
}

double lineNumberValue(const QString &line, double fallback = 0.0)
{
    bool ok = false;
    const double value = line.section(QLatin1Char(':'), 1).trimmed().toDouble(&ok);
    return ok ? value : fallback;
}

QList<NbfcFanStatus> readNbfcFanStatuses(const QString &nbfcPath)
{
    QString output;
    if (nbfcPath.isEmpty() || !runProcess(nbfcPath, {QStringLiteral("status"), QStringLiteral("-a")}, 3000, &output)) {
        return {};
    }

    QList<NbfcFanStatus> fans;
    NbfcFanStatus current;
    bool hasCurrent = false;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        if (line.startsWith(QStringLiteral("Fan display name"), Qt::CaseInsensitive)) {
            if (hasCurrent) {
                fans.append(current);
            }
            current = {};
            current.index = fans.size();
            current.name = line.section(QLatin1Char(':'), 1).trimmed();
            if (current.name.isEmpty()) {
                current.name = QStringLiteral("NBFC Fan %1").arg(current.index + 1);
            }
            hasCurrent = true;
            continue;
        }

        if (!hasCurrent) {
            continue;
        }

        if (line.startsWith(QStringLiteral("Auto control enabled"), Qt::CaseInsensitive)) {
            current.automatic = lineBoolValue(line, current.automatic);
        } else if (line.startsWith(QStringLiteral("Current fan speed"), Qt::CaseInsensitive)) {
            current.currentPercent = std::clamp(lineNumberValue(line, current.currentPercent), 0.0, 100.0);
        } else if (line.startsWith(QStringLiteral("Target fan speed"), Qt::CaseInsensitive)) {
            current.targetPercent = std::clamp(lineNumberValue(line, current.targetPercent), 0.0, 100.0);
        }
    }

    if (hasCurrent) {
        fans.append(current);
    }
    return fans;
}

bool appendNbfcTemperatureSensor(const QString &nbfcPath, QList<SensorInfo> &sensors)
{
    QString output;
    if (nbfcPath.isEmpty() || !runProcess(nbfcPath, {QStringLiteral("status"), QStringLiteral("-s")}, 3000, &output)) {
        return false;
    }

    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!line.startsWith(QStringLiteral("Temperature"), Qt::CaseInsensitive)) {
            continue;
        }

        bool ok = false;
        const double temperature = valueAfterColon(line).toDouble(&ok);
        if (!ok || !plausibleTemperature(temperature)) {
            return false;
        }

        SensorInfo sensor;
        sensor.id = QStringLiteral("windows-nbfc-cpu-temperature");
        sensor.name = QStringLiteral("CPU Temperature");
        sensor.temperatureCelsius = temperature;
        sensor.source = QStringLiteral("NBFC");
        sensor.available = true;
        sensors.append(std::move(sensor));
        return true;
    }

    return false;
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

bool hasTrustedPlatformTemperatureSensor(const QList<SensorInfo> &sensors)
{
    return std::any_of(sensors.cbegin(), sensors.cend(), [](const SensorInfo &sensor) {
        const QString source = sensor.source.toLower();
        return source.contains(QStringLiteral("nbfc"))
            || source.contains(QStringLiteral("librehardwaremonitor"))
            || source.contains(QStringLiteral("openhardwaremonitor"))
            || source.contains(QStringLiteral("instrumentedbios"));
    });
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
        const QString rawName = jsonString(probe, QStringLiteral("Name"));
        const QString deviceId = jsonString(probe, QStringLiteral("DeviceID"));
        const QString normalizedIdentity = QStringLiteral("%1 %2").arg(rawName, deviceId).toLower();
        if (normalizedIdentity.contains(QStringLiteral("battery"))) {
            continue;
        }

        const double raw = jsonNumber(probe, QStringLiteral("CurrentTemperature"), 0.0);
        const double celsius = raw > 200.0 ? (raw / 10.0) - 273.15 : raw;
        if (!plausibleTemperature(celsius) || celsius < 35.0) {
            continue;
        }

        const QString name = rawName.isEmpty()
            ? QStringLiteral("Windows Temperature Probe %1").arg(index + 1)
            : rawName;
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
    hideProcessConsoleWindow(process);
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

int bestNbfcIndexForFanName(const QString &fanName, const QList<NbfcFanStatus> &nbfcFans)
{
    if (nbfcFans.isEmpty()) {
        return -1;
    }
    if (nbfcFans.size() == 1) {
        return nbfcFans.first().index;
    }

    const QString normalizedFanName = fanName.toLower();
    for (const NbfcFanStatus &nbfcFan : nbfcFans) {
        const QString normalizedNbfcName = nbfcFan.name.toLower();
        if ((normalizedFanName.contains(QStringLiteral("cpu")) && normalizedNbfcName.contains(QStringLiteral("cpu")))
            || (normalizedFanName.contains(QStringLiteral("gpu")) && normalizedNbfcName.contains(QStringLiteral("gpu")))
            || normalizedFanName.contains(normalizedNbfcName)
            || normalizedNbfcName.contains(normalizedFanName)) {
            return nbfcFan.index;
        }
    }

    return -1;
}

QHash<QString, int> nbfcFanIndexesForFans(QList<FanInfo> &fans, const QList<NbfcFanStatus> &nbfcFans)
{
    QHash<QString, int> indexes;
    if (nbfcFans.isEmpty()) {
        return indexes;
    }

    int nextGenericIndex = 0;
    QSet<int> mappedNbfcIndexes;
    for (FanInfo &fan : fans) {
        int nbfcIndex = bestNbfcIndexForFanName(fan.name, nbfcFans);
        if (nbfcIndex < 0) {
            nbfcIndex = nextGenericIndex;
        }
        if (nbfcIndex >= nbfcFans.size()) {
            nbfcIndex = nbfcFans.size() - 1;
        }

        nextGenericIndex = std::max(nextGenericIndex, nbfcIndex + 1);
        indexes.insert(fan.id, nbfcIndex);
        mappedNbfcIndexes.insert(nbfcIndex);
        fan.capabilities.manualControl = true;
        fan.capabilities.firmwareControl = true;
        fan.capabilities.pwmControl = true;
    }

    for (const NbfcFanStatus &nbfcFan : nbfcFans) {
        if (mappedNbfcIndexes.contains(nbfcFan.index)) {
            continue;
        }

        FanInfo fan;
        fan.id = QStringLiteral("windows-nbfc-fan-%1").arg(nbfcFan.index);
        fan.name = nbfcFan.name.startsWith(QStringLiteral("NBFC"), Qt::CaseInsensitive)
            ? nbfcFan.name
            : QStringLiteral("NBFC %1").arg(nbfcFan.name);
        fan.rpm = 0;
        fan.speedPercent = nbfcFan.automatic ? nbfcFan.targetPercent : nbfcFan.currentPercent;
        fan.automatic = nbfcFan.automatic;
        fan.capabilities.rpmReading = false;
        fan.capabilities.manualControl = true;
        fan.capabilities.firmwareControl = true;
        fan.capabilities.zeroRpm = true;
        fan.capabilities.pwmControl = true;
        indexes.insert(fan.id, nbfcFan.index);
        fans.append(std::move(fan));
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
        runNbfcCommand(m_nbfcPath, {QStringLiteral("start"), QStringLiteral("-e")}, 5000);
        ensureNbfcConfigured();
        return;
    }

    QProcess *serviceProcess = new QProcess(this);
    serviceProcess->setProgram(QStringLiteral("sc.exe"));
    serviceProcess->setArguments({QStringLiteral("start"), serviceName});
    hideProcessConsoleWindow(*serviceProcess);
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
        hideProcessConsoleWindow(*nbfcProcess);
        connect(nbfcProcess, &QProcess::finished, this, [this, nbfcProcess] {
            nbfcProcess->deleteLater();
            ensureNbfcConfigured();
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

void WindowsHardwareBackend::ensureNbfcConfigured()
{
    if (m_nbfcConfigAttempted || m_nbfcPath.isEmpty()) {
        return;
    }
    m_nbfcConfigAttempted = true;

    QString statusOutput;
    if (runProcess(m_nbfcPath, {QStringLiteral("status")}, 5000, &statusOutput)
        && !selectedNbfcConfigName(statusOutput).isEmpty()) {
        return;
    }

    QString recommendedOutput;
    if (!runNbfcCommand(m_nbfcPath, {QStringLiteral("config"), QStringLiteral("-r")}, 8000, &recommendedOutput)) {
        return;
    }

    const QString recommendedConfig = firstNonEmptyLine(recommendedOutput);
    if (recommendedConfig.isEmpty()) {
        return;
    }

    runNbfcCommand(m_nbfcPath, {
        QStringLiteral("config"),
        QStringLiteral("-a"),
        recommendedConfig,
    }, 10000);
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
    hideProcessConsoleWindow(*process);

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

    m_nbfcPath = findNbfcExecutable();
    ensureNbfcConfigured();
    appendNbfcTemperatureSensor(m_nbfcPath, nextSensors);

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
    appendWin32Fans(arrayValue(hardware, QStringLiteral("win32Fans")), nextFans);
    appendHpInstrumentedBiosSensors(arrayValue(hardware, QStringLiteral("hpNumericSensors")), nextSensors, nextFans);
    appendNvidiaSmi(nextSensors, nextFans);
    appendNvapiFans(nextFans);

    if (!hasTrustedPlatformTemperatureSensor(nextSensors)) {
        appendAcpiThermalZones(arrayValue(hardware, QStringLiteral("acpiZones")), nextSensors);
    }
    if (nextSensors.isEmpty()) {
        appendWindowsTemperatureProbes(arrayValue(hardware, QStringLiteral("temperatureProbes")), nextSensors);
    }

    QList<NbfcFanStatus> nbfcFans = readNbfcFanStatuses(m_nbfcPath);
    if (nbfcFans.isEmpty() && !m_nbfcPath.isEmpty()) {
        runNbfcCommand(m_nbfcPath, {QStringLiteral("start"), QStringLiteral("-e")}, 5000);
        ensureNbfcConfigured();
        nbfcFans = readNbfcFanStatuses(m_nbfcPath);
    }
    m_nbfcFanCount = nbfcFans.size();
    m_nbfcFanIndexesByFanId = nbfcFanIndexesForFans(nextFans, nbfcFans);

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
        auto nbfcIt = m_nbfcFanIndexesByFanId.constFind(fanId);
        if (nbfcIt == m_nbfcFanIndexesByFanId.cend() && m_nbfcFanCount == 1) {
            m_nbfcFanIndexesByFanId.insert(fanId, 0);
            nbfcIt = m_nbfcFanIndexesByFanId.constFind(fanId);
        }
        if (nbfcIt == m_nbfcFanIndexesByFanId.cend() || m_nbfcPath.isEmpty()) {
            return false;
        }

        auto setNbfcSpeed = [&] {
            QString output;
            return runNbfcCommand(m_nbfcPath, {
                QStringLiteral("set"),
                QStringLiteral("-f"),
                QString::number(nbfcIt.value()),
                QStringLiteral("-s"),
                QString::number(static_cast<int>(std::lround(clampedPercent))),
            }, 5000, &output);
        };

        if (!setNbfcSpeed()) {
            runNbfcCommand(m_nbfcPath, {QStringLiteral("start"), QStringLiteral("-e")}, 5000);
            ensureNbfcConfigured();
            if (!setNbfcSpeed()) {
                return false;
            }
        }
        if (m_nbfcFanCount == 0) {
            m_nbfcFanCount = std::max(1, nbfcIt.value() + 1);
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
        auto nbfcIt = m_nbfcFanIndexesByFanId.constFind(fanId);
        if (nbfcIt == m_nbfcFanIndexesByFanId.cend() && m_nbfcFanCount == 1) {
            m_nbfcFanIndexesByFanId.insert(fanId, 0);
            nbfcIt = m_nbfcFanIndexesByFanId.constFind(fanId);
        }
        if (nbfcIt == m_nbfcFanIndexesByFanId.cend() || m_nbfcPath.isEmpty()) {
            return false;
        }

        auto setNbfcAuto = [&] {
            QString output;
            return runNbfcCommand(m_nbfcPath, {
                QStringLiteral("set"),
                QStringLiteral("-f"),
                QString::number(nbfcIt.value()),
                QStringLiteral("-a"),
            }, 5000, &output);
        };

        if (!setNbfcAuto()) {
            runNbfcCommand(m_nbfcPath, {QStringLiteral("start"), QStringLiteral("-e")}, 5000);
            ensureNbfcConfigured();
            if (!setNbfcAuto()) {
                return false;
            }
        }
        if (m_nbfcFanCount == 0) {
            m_nbfcFanCount = std::max(1, nbfcIt.value() + 1);
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
