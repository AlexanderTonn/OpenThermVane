#include "hardware/linux/LinuxHardwareBackend.hpp"

#include <QCollator>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<linux/i8k.h>)
#include <linux/i8k.h>
#endif
#endif

#ifndef I8K_PROC
#define I8K_PROC "/proc/i8k"
#endif
#ifndef I8K_GET_SPEED
#define I8K_GET_SPEED _IOWR('i', 0x85, size_t)
#endif
#ifndef I8K_SET_FAN
#define I8K_SET_FAN _IOWR('i', 0x87, size_t)
#endif
#ifndef I8K_FAN_RIGHT
#define I8K_FAN_RIGHT 0
#endif
#ifndef I8K_FAN_LEFT
#define I8K_FAN_LEFT 1
#endif
#ifndef I8K_FAN_AUTO
#define I8K_FAN_AUTO 3
#endif

namespace thermvane {

namespace {

constexpr auto kHwmonRoot = "/sys/class/hwmon";
constexpr qint64 kNvidiaTemperatureCacheMs = 2000;
constexpr qint64 kNvidiaFanCacheMs = 10000;

struct LinuxTemperatureSensor
{
    SensorInfo sensor;
    QString baseName;
    QString deviceId;
};

struct LinuxFanControl
{
    enum class Type {
        Hwmon,
        NvidiaSettings,
        I8k,
        ThermalCooling,
    };

    FanInfo fan;
    Type type = Type::Hwmon;
    QString pwmPath;
    QString pwmEnablePath;
    int pwmMax = 255;
    int pwmEnableMode = 0;
    int gpuIndex = -1;
    int nvidiaFanIndex = -1;
    int i8kFanIndex = -1;
    int i8kFanState = -1;
    QString coolingStatePath;
    int coolingMaxState = 0;
    int coolingAutoState = 0;
};

struct NvidiaGpuInfo
{
    QString name;
    QString uuid;
    QString busId;
};

QFileInfoList hwmonDirectories(const QDir &hwmonRoot);

QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    return QString::fromUtf8(file.readAll()).trimmed();
}

bool writeTextFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    return file.write(text.toUtf8()) == text.toUtf8().size();
}

bool readIntFile(const QString &path, int &value)
{
    bool ok = false;
    const int parsed = readTextFile(path).toInt(&ok);
    if (!ok) {
        return false;
    }

    value = parsed;
    return true;
}

bool plausibleTemperature(double temperature)
{
    return std::isfinite(temperature) && temperature > 0.0 && temperature < 150.0;
}

bool readTemperatureCelsius(const QString &path, double &temperature)
{
    bool ok = false;
    const qlonglong milliCelsius = readTextFile(path).toLongLong(&ok);
    if (!ok) {
        return false;
    }

    temperature = static_cast<double>(milliCelsius) / 1000.0;
    return plausibleTemperature(temperature);
}

bool runProcess(const QString &program, const QStringList &arguments, QString *standardOutput = nullptr)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();

    if (!process.waitForFinished(1500) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return false;
    }

    if (standardOutput) {
        *standardOutput = QString::fromUtf8(process.readAllStandardOutput());
    }

    return true;
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

    return slug;
}

QString titleCaseLabel(QString label)
{
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    label.replace(QLatin1Char('-'), QLatin1Char(' '));
    label.replace(QStringLiteral("temperature"), QStringLiteral(""), Qt::CaseInsensitive);
    label.replace(QStringLiteral("temp"), QStringLiteral(""), Qt::CaseInsensitive);
    label = label.simplified();

    QStringList words = label.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString &word : words) {
        if (!word.isEmpty()) {
            word[0] = word.at(0).toUpper();
        }
    }

    return words.join(QLatin1Char(' '));
}

QString deviceIdFromHwmonPath(const QString &hwmonPath)
{
    QString devicePath = QFileInfo(QDir(hwmonPath).filePath(QStringLiteral("device"))).canonicalFilePath();
    if (devicePath.isEmpty()) {
        devicePath = QFileInfo(hwmonPath).canonicalFilePath();
    }

    QRegularExpression pciAddressPattern(QStringLiteral("([0-9a-fA-F]{4}:)?[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\\.[0-7]"));
    auto matches = pciAddressPattern.globalMatch(devicePath);

    QString lastMatch;
    while (matches.hasNext()) {
        lastMatch = matches.next().captured(0);
    }

    return lastMatch;
}

bool isAmdHwmonDevice(const QString &chipName)
{
    const QString name = chipName.toLower();
    return name == QStringLiteral("amdgpu")
        || name == QStringLiteral("k10temp")
        || name == QStringLiteral("zenpower")
        || name.contains(QStringLiteral("amd"));
}

bool isNvidiaHwmonDevice(const QString &chipName)
{
    const QString name = chipName.toLower();
    return name == QStringLiteral("nvidia")
        || name == QStringLiteral("nouveau")
        || name.contains(QStringLiteral("nvidia"));
}

QString genericSensorName(const QString &chipName, const QString &label, int index)
{
    const QString chip = chipName.toLower();
    const QString normalizedLabel = label.toLower().simplified();

    if (chip == QStringLiteral("coretemp")) {
        if (normalizedLabel.contains(QStringLiteral("package"))) {
            return QStringLiteral("CPU Package");
        }
        if (normalizedLabel.startsWith(QStringLiteral("core"))) {
            return QStringLiteral("CPU %1").arg(titleCaseLabel(label));
        }
        return index == 1 ? QStringLiteral("CPU Package") : QStringLiteral("CPU Sensor %1").arg(index);
    }

    if (chip == QStringLiteral("nvme")) {
        if (normalizedLabel.contains(QStringLiteral("composite"))) {
            return QStringLiteral("NVMe Composite");
        }
        if (normalizedLabel.contains(QStringLiteral("sensor"))) {
            return QStringLiteral("NVMe %1").arg(titleCaseLabel(label));
        }
        return index == 1 ? QStringLiteral("NVMe Composite") : QStringLiteral("NVMe Sensor %1").arg(index);
    }

    if (!label.isEmpty()) {
        return QStringLiteral("%1 %2").arg(titleCaseLabel(chipName), titleCaseLabel(label));
    }

    return index == 1 ? titleCaseLabel(chipName)
                      : QStringLiteral("%1 Sensor %2").arg(titleCaseLabel(chipName)).arg(index);
}

QString amdSensorName(const QString &chipName, const QString &label, int index)
{
    const QString chip = chipName.toLower();
    const QString normalizedLabel = label.toLower().simplified();

    if (chip == QStringLiteral("amdgpu")) {
        if (normalizedLabel.contains(QStringLiteral("edge"))) {
            return QStringLiteral("AMD GPU Edge");
        }
        if (normalizedLabel.contains(QStringLiteral("junction"))
            || normalizedLabel.contains(QStringLiteral("hotspot"))) {
            return QStringLiteral("AMD GPU Junction");
        }
        if (normalizedLabel.contains(QStringLiteral("mem"))
            || normalizedLabel.contains(QStringLiteral("vram"))) {
            return QStringLiteral("AMD GPU Memory");
        }
        if (!label.isEmpty()) {
            return QStringLiteral("AMD GPU %1").arg(titleCaseLabel(label));
        }
        return index == 1 ? QStringLiteral("AMD GPU Edge") : QStringLiteral("AMD GPU Sensor %1").arg(index);
    }

    if (chip == QStringLiteral("k10temp") || chip == QStringLiteral("zenpower")) {
        if (normalizedLabel == QStringLiteral("tctl")) {
            return QStringLiteral("AMD CPU Control");
        }
        if (normalizedLabel == QStringLiteral("tdie")) {
            return QStringLiteral("AMD CPU Die");
        }

        const QRegularExpression ccdPattern(QStringLiteral("^tccd\\s*(\\d+)$"),
                                            QRegularExpression::CaseInsensitiveOption);
        const auto ccdMatch = ccdPattern.match(normalizedLabel);
        if (ccdMatch.hasMatch()) {
            return QStringLiteral("AMD CPU CCD %1").arg(ccdMatch.captured(1));
        }

        if (!label.isEmpty()) {
            return QStringLiteral("AMD CPU %1").arg(titleCaseLabel(label));
        }
        return index == 1 ? QStringLiteral("AMD CPU Control") : QStringLiteral("AMD CPU Sensor %1").arg(index);
    }

    return index == 1 ? QStringLiteral("AMD Temperature") : QStringLiteral("AMD Temperature Sensor %1").arg(index);
}

QString nvidiaSensorName(const QString &label, int index)
{
    const QString normalizedLabel = label.toLower().simplified();
    if (normalizedLabel.contains(QStringLiteral("mem"))
        || normalizedLabel.contains(QStringLiteral("vram"))) {
        return QStringLiteral("NVIDIA GPU Memory");
    }
    if (normalizedLabel.contains(QStringLiteral("junction"))
        || normalizedLabel.contains(QStringLiteral("hotspot"))) {
        return QStringLiteral("NVIDIA GPU Junction");
    }
    if (normalizedLabel.contains(QStringLiteral("gpu"))
        || normalizedLabel.contains(QStringLiteral("temp"))) {
        return QStringLiteral("NVIDIA GPU Core");
    }
    if (!label.isEmpty()) {
        return QStringLiteral("NVIDIA GPU %1").arg(titleCaseLabel(label));
    }
    return index == 1 ? QStringLiteral("NVIDIA GPU Core") : QStringLiteral("NVIDIA GPU Sensor %1").arg(index);
}

QString nvidiaSmiSensorId(const QString &uuid, const QString &busId, int index)
{
    if (!uuid.isEmpty() && uuid != QStringLiteral("[N/A]")) {
        return QStringLiteral("linux-nvidia-smi-%1-core").arg(slugify(uuid));
    }

    if (!busId.isEmpty() && busId != QStringLiteral("[N/A]")) {
        return QStringLiteral("linux-nvidia-smi-%1-core").arg(slugify(busId));
    }

    return QStringLiteral("linux-nvidia-smi-gpu%1-core").arg(index);
}

QList<LinuxTemperatureSensor> detectNvidiaSmiTemperatureSensors()
{
    static QElapsedTimer cacheTimer;
    static QList<LinuxTemperatureSensor> cachedReadings;
    if (cacheTimer.isValid() && cacheTimer.elapsed() < kNvidiaTemperatureCacheMs) {
        return cachedReadings;
    }

    QString output;
    if (!runProcess(QStringLiteral("nvidia-smi"), {
        QStringLiteral("--query-gpu=name,uuid,pci.bus_id,temperature.gpu"),
        QStringLiteral("--format=csv,noheader,nounits"),
    }, &output)) {
        cachedReadings.clear();
        cacheTimer.restart();
        return {};
    }

    QList<LinuxTemperatureSensor> readings;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (qsizetype lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QStringList fields = lines.at(lineIndex).split(QLatin1Char(','));
        if (fields.size() < 4) {
            continue;
        }

        const QString gpuName = fields.mid(0, fields.size() - 3).join(QLatin1Char(',')).trimmed();
        const QString uuid = fields.at(fields.size() - 3).trimmed();
        const QString busId = fields.at(fields.size() - 2).trimmed();

        bool ok = false;
        const double temperature = fields.at(fields.size() - 1).trimmed().toDouble(&ok);
        if (!ok || !plausibleTemperature(temperature)) {
            continue;
        }

        LinuxTemperatureSensor reading;
        reading.baseName = gpuName.isEmpty() || gpuName == QStringLiteral("[N/A]")
            ? QStringLiteral("NVIDIA GPU Core")
            : QStringLiteral("%1 Core").arg(gpuName);
        reading.deviceId = busId;
        reading.sensor.id = nvidiaSmiSensorId(uuid, busId, static_cast<int>(lineIndex));
        reading.sensor.name = reading.baseName;
        reading.sensor.temperatureCelsius = temperature;
        reading.sensor.source = busId.isEmpty() || busId == QStringLiteral("[N/A]")
            ? QStringLiteral("nvidia-smi")
            : QStringLiteral("nvidia-smi %1").arg(busId);
        reading.sensor.available = true;
        readings.append(std::move(reading));
    }

    cachedReadings = readings;
    cacheTimer.restart();
    return cachedReadings;
}

QList<NvidiaGpuInfo> detectNvidiaSmiGpus()
{
    QString output;
    if (!runProcess(QStringLiteral("nvidia-smi"), {
        QStringLiteral("--query-gpu=name,uuid,pci.bus_id"),
        QStringLiteral("--format=csv,noheader,nounits"),
    }, &output)) {
        return {};
    }

    QList<NvidiaGpuInfo> gpus;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char(','));
        if (fields.size() < 3) {
            continue;
        }

        NvidiaGpuInfo gpu;
        gpu.name = fields.mid(0, fields.size() - 2).join(QLatin1Char(',')).trimmed();
        gpu.uuid = fields.at(fields.size() - 2).trimmed();
        gpu.busId = fields.at(fields.size() - 1).trimmed();
        gpus.append(std::move(gpu));
    }

    return gpus;
}

bool readNvidiaSettingsInt(const QString &target, const QString &attribute, int &value)
{
    QString output;
    if (!runProcess(QStringLiteral("nvidia-settings"), {
        QStringLiteral("-q"),
        QStringLiteral("%1/%2").arg(target, attribute),
        QStringLiteral("-t"),
    }, &output)) {
        return false;
    }

    bool ok = false;
    const int parsed = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts).value(0).trimmed().toInt(&ok);
    if (!ok) {
        return false;
    }

    value = parsed;
    return true;
}

QHash<int, int> detectNvidiaFanCounts()
{
    QHash<int, int> fanCounts;
    QString output;
    if (runProcess(QStringLiteral("nvidia-settings"), {QStringLiteral("-q"), QStringLiteral("fans")}, &output)) {
        const QRegularExpression fanCountPattern(QStringLiteral("\\[gpu:(\\d+)\\]\\):\\s*(\\d+)"));
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const auto match = fanCountPattern.match(line);
            if (match.hasMatch()) {
                fanCounts.insert(match.captured(1).toInt(), match.captured(2).toInt());
            }
        }
    }

    if (!fanCounts.isEmpty()) {
        return fanCounts;
    }

    if (runProcess(QStringLiteral("nvidia-settings"),
                   {QStringLiteral("-q"), QStringLiteral("fans"), QStringLiteral("-t")},
                   &output)) {
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (qsizetype index = 0; index < lines.size(); ++index) {
            bool ok = false;
            const int count = lines.at(index).trimmed().toInt(&ok);
            if (ok) {
                fanCounts.insert(static_cast<int>(index), count);
            }
        }
    }

    return fanCounts;
}

QString nvidiaSettingsFanId(int gpuIndex, int fanIndex)
{
    return QStringLiteral("linux-nvidia-settings-gpu%1-fan%2").arg(gpuIndex).arg(fanIndex);
}

QString hwmonFanId(const QString &chipName, const QString &deviceSlug, int index)
{
    return QStringLiteral("linux-hwmon-%1-%2-fan%3").arg(slugify(chipName), deviceSlug, QString::number(index));
}

QString fanNameForChip(const QString &chipName, const QString &label, int index)
{
    const QString chip = chipName.toLower();
    if (chip == QStringLiteral("dell_smm") || chip == QStringLiteral("dell-smm")) {
        return index == 1 ? QStringLiteral("Dell Laptop Fan 1") : QStringLiteral("Dell Laptop Fan %1").arg(index);
    }

    if (isAmdHwmonDevice(chipName)) {
        return index == 1 ? QStringLiteral("AMD GPU Fan") : QStringLiteral("AMD GPU Fan %1").arg(index);
    }

    if (isNvidiaHwmonDevice(chipName)) {
        return index == 1 ? QStringLiteral("NVIDIA GPU Fan") : QStringLiteral("NVIDIA GPU Fan %1").arg(index);
    }

    if (!label.isEmpty()) {
        return QStringLiteral("%1 %2").arg(titleCaseLabel(chipName), titleCaseLabel(label));
    }

    return index == 1 ? QStringLiteral("%1 Fan").arg(titleCaseLabel(chipName))
                      : QStringLiteral("%1 Fan %2").arg(titleCaseLabel(chipName)).arg(index);
}

int i8kStatePercent(int state)
{
    if (state <= 0) {
        return 0;
    }
    if (state == 1) {
        return 50;
    }
    return 100;
}

int i8kStateForPercent(double percent)
{
    if (percent <= 0.0) {
        return 0;
    }
    if (percent < 66.0) {
        return 1;
    }
    return 2;
}

bool i8kFanStateCandidate(const QList<int> &values, int stateOffset, int rpmOffset)
{
    if (values.size() <= rpmOffset + 1) {
        return false;
    }

    const int firstState = values.at(stateOffset);
    const int secondState = values.at(stateOffset + 1);
    const int firstRpm = values.at(rpmOffset);
    const int secondRpm = values.at(rpmOffset + 1);
    const bool statesValid = firstState >= -1 && firstState <= 3 && secondState >= -1 && secondState <= 3;
    const bool rpmValid = firstRpm >= -1 && firstRpm < 30000 && secondRpm >= -1 && secondRpm < 30000;
    return statesValid && rpmValid && (firstState >= 0 || secondState >= 0 || firstRpm > 0 || secondRpm > 0);
}

bool parseI8kFans(QList<int> &states, QList<int> &rpms)
{
    states.clear();
    rpms.clear();

    const QString text = readTextFile(QString::fromLatin1(I8K_PROC));
    if (text.isEmpty()) {
        return false;
    }

    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.size() < 8) {
        return false;
    }

    QList<int> values;
    for (qsizetype index = 3; index < tokens.size(); ++index) {
        bool ok = false;
        const int value = tokens.at(index).toInt(&ok);
        if (ok) {
            values.append(value);
        }
    }

    int stateOffset = -1;
    int rpmOffset = -1;
    if (i8kFanStateCandidate(values, 2, 4)) {
        stateOffset = 2;
        rpmOffset = 4;
    } else if (i8kFanStateCandidate(values, 1, 3)) {
        stateOffset = 1;
        rpmOffset = 3;
    }

    if (stateOffset < 0 || rpmOffset < 0) {
        return false;
    }

    states = {values.at(stateOffset), values.at(stateOffset + 1)};
    rpms = {values.at(rpmOffset), values.at(rpmOffset + 1)};
    return true;
}

bool i8kIoctl(int command, int fanIndex, int &value)
{
    const int fd = open(I8K_PROC, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    int args[2] = {fanIndex, value};
    const int result = ioctl(fd, command, args);
    close(fd);

    if (result < 0) {
        return false;
    }

    value = args[1];
    return true;
}

bool setI8kFanState(int fanIndex, int state)
{
    int value = state;
    return i8kIoctl(I8K_SET_FAN, fanIndex, value);
}

bool looksLikeFanCoolingDevice(const QString &type)
{
    const QString normalizedType = type.toLower();
    return normalizedType.contains(QStringLiteral("fan"))
        || normalizedType.contains(QStringLiteral("blower"));
}

QList<LinuxFanControl> detectThermalCoolingFans()
{
    const QDir thermalRoot(QStringLiteral("/sys/class/thermal"));
    if (!thermalRoot.exists()) {
        return {};
    }

    QList<LinuxFanControl> controls;
    const QFileInfoList entries = thermalRoot.entryInfoList({QStringLiteral("cooling_device*")},
                                                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);
    for (const QFileInfo &entry : entries) {
        if (!entry.isDir()) {
            continue;
        }

        const QDir deviceDir(entry.filePath());
        const QString type = readTextFile(deviceDir.filePath(QStringLiteral("type")));
        if (!looksLikeFanCoolingDevice(type)) {
            continue;
        }

        int currentState = 0;
        int maxState = 0;
        const QString statePath = deviceDir.filePath(QStringLiteral("cur_state"));
        if (!readIntFile(statePath, currentState)
            || !readIntFile(deviceDir.filePath(QStringLiteral("max_state")), maxState)) {
            continue;
        }

        maxState = std::clamp(maxState, 1, 1000);

        LinuxFanControl control;
        control.type = LinuxFanControl::Type::ThermalCooling;
        control.coolingStatePath = statePath;
        control.coolingMaxState = maxState;
        control.coolingAutoState = currentState;
        control.fan.id = QStringLiteral("linux-thermal-%1").arg(slugify(entry.fileName()));
        control.fan.name = QStringLiteral("%1 Cooling Fan").arg(titleCaseLabel(type));
        control.fan.rpm = 0;
        control.fan.speedPercent = std::clamp(static_cast<double>(currentState) * 100.0 / maxState, 0.0, 100.0);
        control.fan.automatic = true;
        control.fan.capabilities.rpmReading = false;
        control.fan.capabilities.manualControl = QFileInfo(statePath).isWritable();
        control.fan.capabilities.firmwareControl = QFileInfo(statePath).isWritable();
        control.fan.capabilities.zeroRpm = true;
        control.fan.capabilities.pwmControl = false;
        controls.append(std::move(control));
    }

    return controls;
}

QList<LinuxFanControl> detectI8kFans()
{
    QList<int> states;
    QList<int> rpms;
    if (!parseI8kFans(states, rpms)) {
        return {};
    }

    QList<LinuxFanControl> controls;
    const QList<int> i8kFanIndexes = {I8K_FAN_LEFT, I8K_FAN_RIGHT};
    const QStringList names = {QStringLiteral("Dell Laptop Fan 1"), QStringLiteral("Dell Laptop Fan 2")};
    for (int index = 0; index < 2; ++index) {
        const int state = states.value(index, -1);
        const int rpm = rpms.value(index, -1);
        if (state < 0 && rpm <= 0) {
            continue;
        }

        LinuxFanControl control;
        control.type = LinuxFanControl::Type::I8k;
        control.i8kFanIndex = i8kFanIndexes.at(index);
        control.i8kFanState = state;
        control.fan.id = QStringLiteral("linux-i8k-fan%1").arg(index + 1);
        control.fan.name = names.at(index);
        control.fan.rpm = rpm > 0 ? rpm : 0;
        control.fan.speedPercent = i8kStatePercent(state);
        control.fan.automatic = state == I8K_FAN_AUTO;
        control.fan.capabilities.rpmReading = rpm >= 0;
        control.fan.capabilities.manualControl = true;
        control.fan.capabilities.firmwareControl = true;
        control.fan.capabilities.zeroRpm = true;
        control.fan.capabilities.pwmControl = false;
        controls.append(std::move(control));
    }

    return controls;
}

QList<int> indexedEntries(const QDir &directory, const QString &pattern)
{
    QList<int> indexes;
    const QRegularExpression expression(QStringLiteral("^%1(\\d+)$").arg(pattern));
    for (const QString &entry : directory.entryList({QStringLiteral("%1*").arg(pattern)},
                                                    QDir::Files | QDir::System)) {
        const auto match = expression.match(entry);
        if (match.hasMatch()) {
            indexes.append(match.captured(1).toInt());
        }
    }

    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

QList<int> fanInputIndexes(const QDir &directory)
{
    QList<int> indexes;
    const QRegularExpression expression(QStringLiteral("^fan(\\d+)_input$"));
    for (const QString &entry : directory.entryList({QStringLiteral("fan*_input")},
                                                    QDir::Files | QDir::System)) {
        const auto match = expression.match(entry);
        if (match.hasMatch()) {
            indexes.append(match.captured(1).toInt());
        }
    }

    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

void appendMissingIndexes(QList<int> &indexes, const QList<int> &moreIndexes)
{
    for (const int index : moreIndexes) {
        if (!indexes.contains(index)) {
            indexes.append(index);
        }
    }

    std::sort(indexes.begin(), indexes.end());
}

QList<LinuxFanControl> detectHwmonFans()
{
    QList<LinuxFanControl> controls;
    const QDir hwmonRoot(QString::fromLatin1(kHwmonRoot));
    if (!hwmonRoot.exists()) {
        return {};
    }

    for (const QFileInfo &hwmonEntry : hwmonDirectories(hwmonRoot)) {
        const QDir hwmonDir(hwmonEntry.filePath());
        const QString chipName = readTextFile(hwmonDir.filePath(QStringLiteral("name")));
        if (chipName.isEmpty()) {
            continue;
        }

        const QString deviceId = deviceIdFromHwmonPath(hwmonDir.path());
        const QString deviceSlug = slugify(deviceId).isEmpty() ? slugify(hwmonEntry.fileName()) : slugify(deviceId);

        QList<int> indexes = fanInputIndexes(hwmonDir);
        appendMissingIndexes(indexes, indexedEntries(hwmonDir, QStringLiteral("pwm")));

        for (const int index : indexes) {
            const QString pwmPath = hwmonDir.filePath(QStringLiteral("pwm%1").arg(index));
            const bool pwmExists = QFileInfo::exists(pwmPath);

            int pwm = 0;
            const bool pwmReadable = readIntFile(pwmPath, pwm);

            int rpm = 0;
            const bool rpmReadable = readIntFile(hwmonDir.filePath(QStringLiteral("fan%1_input").arg(index)), rpm);
            if (!pwmExists && !rpmReadable) {
                continue;
            }

            int pwmMax = 255;
            readIntFile(hwmonDir.filePath(QStringLiteral("pwm%1_max").arg(index)), pwmMax);
            pwmMax = std::clamp(pwmMax, 1, 65535);

            const QString enablePath = hwmonDir.filePath(QStringLiteral("pwm%1_enable").arg(index));
            int enableMode = 0;
            const bool enableReadable = readIntFile(enablePath, enableMode);
            const QString label = readTextFile(hwmonDir.filePath(QStringLiteral("fan%1_label").arg(index)));

            LinuxFanControl control;
            control.type = LinuxFanControl::Type::Hwmon;
            control.pwmPath = pwmPath;
            control.pwmEnablePath = QFileInfo::exists(enablePath) ? enablePath : QString();
            control.pwmMax = pwmMax;
            control.pwmEnableMode = enableReadable ? enableMode : 0;
            control.fan.id = hwmonFanId(chipName, deviceSlug, index);
            control.fan.name = fanNameForChip(chipName, label, index);
            control.fan.rpm = rpmReadable ? rpm : 0;
            control.fan.speedPercent = pwmReadable
                ? std::clamp(static_cast<double>(pwm) * 100.0 / pwmMax, 0.0, 100.0)
                : 0.0;
            control.fan.automatic = enableReadable ? enableMode != 1 : true;
            control.fan.capabilities.rpmReading = rpmReadable;
            control.fan.capabilities.manualControl = pwmReadable;
            control.fan.capabilities.firmwareControl = QFileInfo::exists(enablePath);
            control.fan.capabilities.zeroRpm = true;
            control.fan.capabilities.pwmControl = pwmReadable;
            controls.append(std::move(control));
        }
    }

    return controls;
}

QList<LinuxFanControl> detectNvidiaSettingsFans()
{
    static QElapsedTimer cacheTimer;
    static QList<LinuxFanControl> cachedControls;
    if (cacheTimer.isValid() && cacheTimer.elapsed() < kNvidiaFanCacheMs) {
        return cachedControls;
    }

    const QList<NvidiaGpuInfo> gpus = detectNvidiaSmiGpus();
    if (gpus.isEmpty()) {
        cachedControls.clear();
        cacheTimer.restart();
        return {};
    }

    QHash<int, int> fanCounts = detectNvidiaFanCounts();
    if (fanCounts.isEmpty()) {
        int probeValue = 0;
        if (!readNvidiaSettingsInt(QStringLiteral("[gpu:0]"), QStringLiteral("GPUFanControlState"), probeValue)
            && !readNvidiaSettingsInt(QStringLiteral("[fan:0]"), QStringLiteral("GPUCurrentFanSpeed"), probeValue)) {
            cachedControls.clear();
            cacheTimer.restart();
            return {};
        }

        for (qsizetype gpuIndex = 0; gpuIndex < gpus.size(); ++gpuIndex) {
            fanCounts.insert(static_cast<int>(gpuIndex), 1);
        }
    }

    QList<LinuxFanControl> controls;
    int fanObjectIndex = 0;
    for (qsizetype gpuIndex = 0; gpuIndex < gpus.size(); ++gpuIndex) {
        const int fanCount = std::clamp(fanCounts.value(static_cast<int>(gpuIndex), 0), 0, 16);
        for (int fanNumber = 0; fanNumber < fanCount; ++fanNumber) {
            int rpm = 0;
            const bool rpmReadable = readNvidiaSettingsInt(QStringLiteral("[fan:%1]").arg(fanObjectIndex),
                                                           QStringLiteral("GPUCurrentFanSpeedRPM"),
                                                           rpm);

            int speedPercent = 0;
            const bool speedReadable = readNvidiaSettingsInt(QStringLiteral("[fan:%1]").arg(fanObjectIndex),
                                                             QStringLiteral("GPUCurrentFanSpeed"),
                                                             speedPercent);

            int controlState = 0;
            const bool controlStateReadable = readNvidiaSettingsInt(QStringLiteral("[gpu:%1]").arg(gpuIndex),
                                                                    QStringLiteral("GPUFanControlState"),
                                                                    controlState);

            LinuxFanControl control;
            control.type = LinuxFanControl::Type::NvidiaSettings;
            control.gpuIndex = static_cast<int>(gpuIndex);
            control.nvidiaFanIndex = fanObjectIndex;
            control.pwmEnableMode = controlStateReadable ? controlState : 0;
            control.fan.id = nvidiaSettingsFanId(static_cast<int>(gpuIndex), fanObjectIndex);
            control.fan.name = gpus.at(gpuIndex).name.isEmpty()
                ? QStringLiteral("NVIDIA GPU Fan")
                : QStringLiteral("%1 Fan").arg(gpus.at(gpuIndex).name);
            if (fanCount > 1) {
                control.fan.name += QStringLiteral(" %1").arg(fanNumber + 1);
            }
            control.fan.rpm = rpmReadable ? rpm : 0;
            control.fan.speedPercent = speedReadable
                ? std::clamp(static_cast<double>(speedPercent), 0.0, 100.0)
                : 0.0;
            control.fan.automatic = controlStateReadable ? controlState == 0 : true;
            control.fan.capabilities.rpmReading = rpmReadable;
            control.fan.capabilities.manualControl = true;
            control.fan.capabilities.firmwareControl = true;
            control.fan.capabilities.zeroRpm = true;
            control.fan.capabilities.pwmControl = true;
            controls.append(std::move(control));
            ++fanObjectIndex;
        }
    }

    cachedControls = controls;
    cacheTimer.restart();
    return cachedControls;
}

QList<LinuxFanControl> detectLinuxFanControls()
{
    QList<LinuxFanControl> controls = detectHwmonFans();
    const QList<LinuxFanControl> i8kControls = detectI8kFans();
    const QList<LinuxFanControl> thermalCoolingControls = detectThermalCoolingFans();
    const QList<LinuxFanControl> nvidiaSettingsControls = detectNvidiaSettingsFans();

    const bool hasDellHwmonFan = std::any_of(controls.cbegin(), controls.cend(), [](const LinuxFanControl &control) {
        return control.fan.id.contains(QStringLiteral("dell-smm"), Qt::CaseInsensitive)
            || control.fan.id.contains(QStringLiteral("dell_smm"), Qt::CaseInsensitive)
            || control.fan.name.startsWith(QStringLiteral("Dell Laptop Fan"), Qt::CaseInsensitive);
    });
    if (!hasDellHwmonFan) {
        controls.append(i8kControls);
    }

    controls.append(thermalCoolingControls);

    if (!nvidiaSettingsControls.isEmpty()) {
        controls.erase(std::remove_if(controls.begin(), controls.end(), [](const LinuxFanControl &control) {
            return control.fan.name.startsWith(QStringLiteral("NVIDIA "), Qt::CaseInsensitive);
        }), controls.end());
    }

    controls.append(nvidiaSettingsControls);

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(controls.begin(), controls.end(), [&collator](const LinuxFanControl &left, const LinuxFanControl &right) {
        return collator.compare(left.fan.name, right.fan.name) < 0;
    });

    return controls;
}

QList<FanInfo> fanInfosFromControls(QList<LinuxFanControl> controls,
                                    const QHash<QString, double> &pendingManualFanSpeeds)
{
    QList<FanInfo> fans;
    fans.reserve(controls.size());

    for (LinuxFanControl &control : controls) {
        const auto pendingIt = pendingManualFanSpeeds.constFind(control.fan.id);
        if (pendingIt != pendingManualFanSpeeds.cend()) {
            control.fan.speedPercent = pendingIt.value();
            control.fan.automatic = false;
        }
        fans.append(control.fan);
    }

    return fans;
}

bool setHwmonFanSpeed(const LinuxFanControl &control,
                      double percent,
                      QHash<QString, int> &autoFanModes)
{
    if (!control.pwmEnablePath.isEmpty() && !autoFanModes.contains(control.fan.id)) {
        int currentMode = 0;
        if (readIntFile(control.pwmEnablePath, currentMode)) {
            autoFanModes.insert(control.fan.id, currentMode);
        }
    }

    if (!control.pwmEnablePath.isEmpty() && !writeTextFile(control.pwmEnablePath, QStringLiteral("1\n"))) {
        return false;
    }

    const int pwm = std::clamp(static_cast<int>(std::lround(percent * control.pwmMax / 100.0)),
                               0,
                               control.pwmMax);
    return writeTextFile(control.pwmPath, QString::number(pwm) + QLatin1Char('\n'));
}

bool restoreHwmonFanControl(const LinuxFanControl &control, QHash<QString, int> &autoFanModes)
{
    if (control.pwmEnablePath.isEmpty()) {
        return false;
    }

    const int mode = autoFanModes.value(control.fan.id, control.pwmEnableMode > 1 ? control.pwmEnableMode : 2);
    if (!writeTextFile(control.pwmEnablePath, QString::number(mode) + QLatin1Char('\n'))) {
        return false;
    }

    autoFanModes.remove(control.fan.id);
    return true;
}

bool setNvidiaFanSpeed(const LinuxFanControl &control,
                       double percent,
                       QHash<QString, int> &autoFanModes)
{
    if (!autoFanModes.contains(control.fan.id)) {
        int currentMode = 0;
        if (readNvidiaSettingsInt(QStringLiteral("[gpu:%1]").arg(control.gpuIndex),
                                  QStringLiteral("GPUFanControlState"),
                                  currentMode)) {
            autoFanModes.insert(control.fan.id, currentMode);
        }
    }

    const int roundedPercent = std::clamp(static_cast<int>(std::lround(percent)), 0, 100);
    return runProcess(QStringLiteral("nvidia-settings"), {
        QStringLiteral("-a"),
        QStringLiteral("[gpu:%1]/GPUFanControlState=1").arg(control.gpuIndex),
        QStringLiteral("-a"),
        QStringLiteral("[fan:%1]/GPUTargetFanSpeed=%2").arg(control.nvidiaFanIndex).arg(roundedPercent),
    });
}

bool restoreNvidiaFanControl(const LinuxFanControl &control, QHash<QString, int> &autoFanModes)
{
    const int mode = autoFanModes.value(control.fan.id, 0);
    if (!runProcess(QStringLiteral("nvidia-settings"), {
        QStringLiteral("-a"),
        QStringLiteral("[gpu:%1]/GPUFanControlState=%2").arg(control.gpuIndex).arg(mode),
    })) {
        return false;
    }

    autoFanModes.remove(control.fan.id);
    return true;
}

bool setI8kFanSpeed(const LinuxFanControl &control, double percent, QHash<QString, int> &autoFanModes)
{
    if (!autoFanModes.contains(control.fan.id)) {
        autoFanModes.insert(control.fan.id, control.i8kFanState >= 0 ? control.i8kFanState : I8K_FAN_AUTO);
    }

    return setI8kFanState(control.i8kFanIndex, i8kStateForPercent(percent));
}

bool restoreI8kFanControl(const LinuxFanControl &control, QHash<QString, int> &autoFanModes)
{
    const int mode = autoFanModes.value(control.fan.id, I8K_FAN_AUTO);
    if (!setI8kFanState(control.i8kFanIndex, mode)) {
        return false;
    }

    autoFanModes.remove(control.fan.id);
    return true;
}

bool setThermalCoolingFanSpeed(const LinuxFanControl &control, double percent, QHash<QString, int> &autoFanModes)
{
    if (control.coolingStatePath.isEmpty() || control.coolingMaxState <= 0) {
        return false;
    }

    if (!autoFanModes.contains(control.fan.id)) {
        autoFanModes.insert(control.fan.id, control.coolingAutoState);
    }

    const int state = std::clamp(static_cast<int>(std::lround(percent * control.coolingMaxState / 100.0)),
                                 0,
                                 control.coolingMaxState);
    return writeTextFile(control.coolingStatePath, QString::number(state) + QLatin1Char('\n'));
}

bool restoreThermalCoolingFanControl(const LinuxFanControl &control, QHash<QString, int> &autoFanModes)
{
    if (control.coolingStatePath.isEmpty()) {
        return false;
    }

    const int state = autoFanModes.value(control.fan.id, control.coolingAutoState);
    if (!writeTextFile(control.coolingStatePath, QString::number(state) + QLatin1Char('\n'))) {
        return false;
    }

    autoFanModes.remove(control.fan.id);
    return true;
}

QList<int> temperatureIndexes(const QDir &hwmonDir)
{
    QList<int> indexes;
    const QRegularExpression inputPattern(QStringLiteral("^temp(\\d+)_input$"));

    for (const QString &entry : hwmonDir.entryList({QStringLiteral("temp*_input")}, QDir::Files | QDir::System)) {
        const auto match = inputPattern.match(entry);
        if (match.hasMatch()) {
            indexes.append(match.captured(1).toInt());
        }
    }

    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

QFileInfoList hwmonDirectories(const QDir &hwmonRoot)
{
    QFileInfoList directories;
    const QFileInfoList entries = hwmonRoot.entryInfoList({QStringLiteral("hwmon*")},
                                                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            directories.append(entry);
        }
    }

    std::sort(directories.begin(), directories.end(), [](const QFileInfo &left, const QFileInfo &right) {
        return left.fileName() < right.fileName();
    });

    return directories;
}

void uniquifySensorNames(QList<LinuxTemperatureSensor> &readings)
{
    QHash<QString, int> nameCounts;
    for (const LinuxTemperatureSensor &reading : readings) {
        nameCounts[reading.baseName.toLower()] += 1;
    }

    for (LinuxTemperatureSensor &reading : readings) {
        if (nameCounts.value(reading.baseName.toLower()) <= 1 || reading.deviceId.isEmpty()) {
            reading.sensor.name = reading.baseName;
            continue;
        }

        reading.sensor.name = QStringLiteral("%1 (%2)").arg(reading.baseName, reading.deviceId);
    }
}

QList<SensorInfo> detectLinuxTemperatureSensors()
{
    QList<LinuxTemperatureSensor> readings;
    const QDir hwmonRoot(QString::fromLatin1(kHwmonRoot));
    if (!hwmonRoot.exists()) {
        return {};
    }

    for (const QFileInfo &hwmonEntry : hwmonDirectories(hwmonRoot)) {
        const QDir hwmonDir(hwmonEntry.filePath());
        const QString chipName = readTextFile(hwmonDir.filePath(QStringLiteral("name")));
        if (chipName.isEmpty()) {
            continue;
        }

        const QString deviceId = deviceIdFromHwmonPath(hwmonDir.path());
        const QString deviceSlug = slugify(deviceId).isEmpty() ? QStringLiteral("device") : slugify(deviceId);
        for (const int index : temperatureIndexes(hwmonDir)) {
            double temperature = 0.0;
            const QString inputFileName = QStringLiteral("temp%1_input").arg(index);
            if (!readTemperatureCelsius(hwmonDir.filePath(inputFileName), temperature)) {
                continue;
            }

            const QString label = readTextFile(hwmonDir.filePath(QStringLiteral("temp%1_label").arg(index)));
            LinuxTemperatureSensor reading;
            reading.baseName = isAmdHwmonDevice(chipName)
                ? amdSensorName(chipName, label, index)
                : isNvidiaHwmonDevice(chipName)
                    ? nvidiaSensorName(label, index)
                    : genericSensorName(chipName, label, index);
            reading.deviceId = deviceId;
            reading.sensor.id = QStringLiteral("linux-%1-%2-temp%3-%4")
                                    .arg(slugify(chipName),
                                         deviceSlug,
                                         QString::number(index),
                                         slugify(label).isEmpty() ? QStringLiteral("sensor") : slugify(label));
            reading.sensor.name = reading.baseName;
            reading.sensor.temperatureCelsius = temperature;
            reading.sensor.source = QStringLiteral("%1 %2%3")
                                        .arg(chipName,
                                             inputFileName,
                                             label.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(label));
            reading.sensor.available = true;
            readings.append(std::move(reading));
        }
    }

    readings.append(detectNvidiaSmiTemperatureSensors());

    uniquifySensorNames(readings);

    QList<SensorInfo> sensors;
    sensors.reserve(readings.size());
    for (const LinuxTemperatureSensor &reading : readings) {
        sensors.append(reading.sensor);
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(sensors.begin(), sensors.end(), [&collator](const SensorInfo &left, const SensorInfo &right) {
        return collator.compare(left.name, right.name) < 0;
    });

    return sensors;
}

} // namespace

LinuxHardwareBackend::LinuxHardwareBackend(QObject *parent)
    : IHardwareBackend(parent)
{
    scan();
}

void LinuxHardwareBackend::scan()
{
    m_sensors = detectLinuxTemperatureSensors();
    m_fans = fanInfosFromControls(detectLinuxFanControls(), m_pendingManualFanSpeeds);
    emit hardwareChanged();
}

QList<SensorInfo> LinuxHardwareBackend::sensors() const
{
    return m_sensors;
}

QList<FanInfo> LinuxHardwareBackend::fans() const
{
    return m_fans;
}

bool LinuxHardwareBackend::setFanSpeed(const QString &fanId, double percent)
{
    const double clampedPercent = std::clamp(percent, 0.0, 100.0);
    const QList<LinuxFanControl> controls = detectLinuxFanControls();
    const auto controlIt = std::find_if(controls.cbegin(), controls.cend(), [&](const LinuxFanControl &control) {
        return control.fan.id == fanId;
    });
    if (controlIt == controls.cend()) {
        return false;
    }

    bool success = false;
    switch (controlIt->type) {
    case LinuxFanControl::Type::Hwmon:
        success = setHwmonFanSpeed(*controlIt, clampedPercent, m_autoFanModes);
        break;
    case LinuxFanControl::Type::NvidiaSettings:
        success = setNvidiaFanSpeed(*controlIt, clampedPercent, m_autoFanModes);
        break;
    case LinuxFanControl::Type::I8k:
        success = setI8kFanSpeed(*controlIt, clampedPercent, m_autoFanModes);
        break;
    case LinuxFanControl::Type::ThermalCooling:
        success = setThermalCoolingFanSpeed(*controlIt, clampedPercent, m_autoFanModes);
        break;
    }
    if (!success) {
        return false;
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

bool LinuxHardwareBackend::restoreAutomaticControl(const QString &fanId)
{
    const QList<LinuxFanControl> controls = detectLinuxFanControls();
    const auto controlIt = std::find_if(controls.cbegin(), controls.cend(), [&](const LinuxFanControl &control) {
        return control.fan.id == fanId;
    });
    if (controlIt == controls.cend()) {
        return false;
    }

    bool success = false;
    switch (controlIt->type) {
    case LinuxFanControl::Type::Hwmon:
        success = restoreHwmonFanControl(*controlIt, m_autoFanModes);
        break;
    case LinuxFanControl::Type::NvidiaSettings:
        success = restoreNvidiaFanControl(*controlIt, m_autoFanModes);
        break;
    case LinuxFanControl::Type::I8k:
        success = restoreI8kFanControl(*controlIt, m_autoFanModes);
        break;
    case LinuxFanControl::Type::ThermalCooling:
        success = restoreThermalCoolingFanControl(*controlIt, m_autoFanModes);
        break;
    }
    if (!success) {
        return false;
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
