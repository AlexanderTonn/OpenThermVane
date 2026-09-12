#include "hardware/macos/MacHardwareBackend.hpp"

#include <QCollator>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <sys/sysctl.h>
#endif

namespace thermvane {

namespace {

#ifdef Q_OS_MACOS

constexpr int64_t kIOHIDEventTypeTemperature = 15;
constexpr int kIOHIDEventFieldTemperatureLevel = static_cast<int>(kIOHIDEventTypeTemperature << 16);
constexpr int kAppleVendorTemperatureSensorUsagePage = 0xff00;
constexpr int kAppleVendorTemperatureSensorUsage = 5;
constexpr uint32_t kSmcSelector = 2;
constexpr uint8_t kSmcCommandReadKeyInfo = 9;
constexpr uint8_t kSmcCommandReadBytes = 5;

extern "C" {
CFTypeRef IOHIDEventSystemClientCreate(CFAllocatorRef allocator);
int IOHIDEventSystemClientSetMatching(CFTypeRef client, CFDictionaryRef match);
CFArrayRef IOHIDEventSystemClientCopyServices(CFTypeRef client);
CFTypeRef IOHIDServiceClientCopyEvent(CFTypeRef service, int64_t type, int32_t options, int64_t timestamp);
CFTypeRef IOHIDServiceClientCopyProperty(CFTypeRef service, CFStringRef property);
double IOHIDEventGetFloatValue(CFTypeRef event, int32_t field);
}

QString readableSensorName(const QString &rawName);

struct SmcVersion
{
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t build = 0;
    uint8_t reserved = 0;
    uint16_t release = 0;
};

struct SmcPLimitData
{
    uint16_t version = 0;
    uint16_t length = 0;
    uint32_t cpuPLimit = 0;
    uint32_t gpuPLimit = 0;
    uint32_t memPLimit = 0;
};

struct SmcKeyInfo
{
    uint32_t dataSize = 0;
    uint32_t dataType = 0;
    uint8_t dataAttributes = 0;
};

struct SmcKeyData
{
    uint32_t key = 0;
    SmcVersion version;
    SmcPLimitData pLimitData;
    SmcKeyInfo keyInfo;
    uint8_t result = 0;
    uint8_t status = 0;
    uint8_t data8 = 0;
    uint32_t data32 = 0;
    std::array<uint8_t, 32> bytes = {};
};

static_assert(sizeof(SmcKeyData) == 80);

struct SmcSensorGroup
{
    QString name;
    QStringList keys;
};

struct SmcSensorProfile
{
    QString family;
    QList<SmcSensorGroup> groups;
};

class ScopedIoObject
{
public:
    explicit ScopedIoObject(io_object_t value)
        : m_value(value)
    {
    }

    ~ScopedIoObject()
    {
        if (m_value != IO_OBJECT_NULL) {
            IOObjectRelease(m_value);
        }
    }

    io_object_t get() const
    {
        return m_value;
    }

private:
    io_object_t m_value = IO_OBJECT_NULL;
};

class ScopedCFRelease
{
public:
    explicit ScopedCFRelease(CFTypeRef value)
        : m_value(value)
    {
    }

    ~ScopedCFRelease()
    {
        if (m_value) {
            CFRelease(m_value);
        }
    }

    CFTypeRef get() const
    {
        return m_value;
    }

private:
    CFTypeRef m_value = nullptr;
};

QString cfStringToQString(CFStringRef value)
{
    if (!value) {
        return {};
    }

    const CFIndex length = CFStringGetLength(value);
    const CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    QByteArray buffer(maxSize, Qt::Uninitialized);
    if (!CFStringGetCString(value, buffer.data(), maxSize, kCFStringEncodingUTF8)) {
        return {};
    }

    return QString::fromUtf8(buffer.constData());
}

uint32_t fourCharCode(const char *value)
{
    return (static_cast<uint32_t>(static_cast<unsigned char>(value[0])) << 24)
        | (static_cast<uint32_t>(static_cast<unsigned char>(value[1])) << 16)
        | (static_cast<uint32_t>(static_cast<unsigned char>(value[2])) << 8)
        | static_cast<uint32_t>(static_cast<unsigned char>(value[3]));
}

uint32_t keyCode(const QString &key)
{
    const QByteArray keyBytes = key.toLatin1();
    if (keyBytes.size() != 4) {
        return 0;
    }

    return fourCharCode(keyBytes.constData());
}

bool plausibleTemperature(double temperature)
{
    return std::isfinite(temperature) && temperature > 0.0 && temperature < 150.0;
}

double decodeSmcFloat(const std::array<uint8_t, 32> &bytes)
{
    float nativeValue = 0.0F;
    std::memcpy(&nativeValue, bytes.data(), sizeof(nativeValue));
    if (plausibleTemperature(nativeValue)) {
        return nativeValue;
    }

    const uint32_t bigEndianBits = (static_cast<uint32_t>(bytes[0]) << 24)
        | (static_cast<uint32_t>(bytes[1]) << 16)
        | (static_cast<uint32_t>(bytes[2]) << 8)
        | static_cast<uint32_t>(bytes[3]);
    float bigEndianValue = 0.0F;
    std::memcpy(&bigEndianValue, &bigEndianBits, sizeof(bigEndianValue));
    return bigEndianValue;
}

double decodeSmcFixedPoint(const std::array<uint8_t, 32> &bytes, int fractionalBits)
{
    const uint16_t raw = (static_cast<uint16_t>(bytes[0]) << 8) | static_cast<uint16_t>(bytes[1]);
    return static_cast<double>(raw) / static_cast<double>(1 << fractionalBits);
}

class ScopedIoConnect
{
public:
    explicit ScopedIoConnect(io_connect_t value = IO_OBJECT_NULL)
        : m_value(value)
    {
    }

    ~ScopedIoConnect()
    {
        if (m_value != IO_OBJECT_NULL) {
            IOServiceClose(m_value);
        }
    }

    io_connect_t get() const
    {
        return m_value;
    }

    void reset(io_connect_t value)
    {
        if (m_value != IO_OBJECT_NULL) {
            IOServiceClose(m_value);
        }
        m_value = value;
    }

private:
    io_connect_t m_value = IO_OBJECT_NULL;
};

class AppleSmcReader
{
public:
    AppleSmcReader()
    {
        openService(IOServiceMatching("AppleSMC"));
        if (!isOpen()) {
            openService(IOServiceMatching("AppleSMCKeysEndpoint"));
        }
    }

    bool isOpen() const
    {
        return m_connection.get() != IO_OBJECT_NULL;
    }

    bool readTemperature(const QString &key, double &temperature) const
    {
        if (!isOpen()) {
            return false;
        }

        SmcKeyData keyInfoInput;
        SmcKeyData keyInfoOutput;
        keyInfoInput.key = keyCode(key);
        keyInfoInput.data8 = kSmcCommandReadKeyInfo;
        if (keyInfoInput.key == 0 || !call(keyInfoInput, keyInfoOutput) || keyInfoOutput.result != 0) {
            return false;
        }

        SmcKeyData valueInput;
        SmcKeyData valueOutput;
        valueInput.key = keyInfoInput.key;
        valueInput.keyInfo = keyInfoOutput.keyInfo;
        valueInput.data8 = kSmcCommandReadBytes;
        if (!call(valueInput, valueOutput) || valueOutput.result != 0) {
            return false;
        }

        const uint32_t type = keyInfoOutput.keyInfo.dataType;
        if (type == fourCharCode("flt ")) {
            temperature = decodeSmcFloat(valueOutput.bytes);
        } else if (type == fourCharCode("fpe2")) {
            temperature = decodeSmcFixedPoint(valueOutput.bytes, 2);
        } else if (type == fourCharCode("sp78")) {
            const int16_t raw = static_cast<int16_t>((static_cast<uint16_t>(valueOutput.bytes[0]) << 8)
                                                     | static_cast<uint16_t>(valueOutput.bytes[1]));
            temperature = static_cast<double>(raw) / 256.0;
        } else {
            return false;
        }

        return plausibleTemperature(temperature);
    }

private:
    void openService(CFMutableDictionaryRef matching)
    {
        if (!matching) {
            return;
        }

        const io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
        if (service == IO_OBJECT_NULL) {
            return;
        }

        ScopedIoObject serviceGuard(service);
        for (uint32_t type : {0U, 1U}) {
            io_connect_t connection = IO_OBJECT_NULL;
            if (IOServiceOpen(service, mach_task_self(), type, &connection) == KERN_SUCCESS) {
                m_connection.reset(connection);
                return;
            }
        }
    }

    bool call(const SmcKeyData &input, SmcKeyData &output) const
    {
        std::memset(&output, 0, sizeof(output));
        size_t outputSize = sizeof(output);
        return IOConnectCallStructMethod(m_connection.get(),
                                         kSmcSelector,
                                         &input,
                                         sizeof(input),
                                         &output,
                                         &outputSize) == KERN_SUCCESS
            && outputSize == sizeof(output);
    }

    ScopedIoConnect m_connection;
};

QList<SmcSensorGroup> sharedSmcTemperatureGroups()
{
    return {
        {QStringLiteral("Battery"), {QStringLiteral("TB0T"), QStringLiteral("TB1T"), QStringLiteral("TB2T")}},
    };
}

QList<SmcSensorProfile> smcTemperatureProfiles()
{
    return {
        {
            QStringLiteral("M1"),
            {
                {QStringLiteral("CPU Efficiency Core 1"), {QStringLiteral("Tp09")}},
                {QStringLiteral("CPU Efficiency Core 2"), {QStringLiteral("Tp0T")}},
                {QStringLiteral("CPU Performance Core 1"), {QStringLiteral("Tp01")}},
                {QStringLiteral("CPU Performance Core 2"), {QStringLiteral("Tp05")}},
                {QStringLiteral("CPU Performance Core 3"), {QStringLiteral("Tp0D")}},
                {QStringLiteral("CPU Performance Core 4"), {QStringLiteral("Tp0H")}},
                {QStringLiteral("CPU Performance Core 5"), {QStringLiteral("Tp0L")}},
                {QStringLiteral("CPU Performance Core 6"), {QStringLiteral("Tp0P")}},
                {QStringLiteral("CPU Performance Core 7"), {QStringLiteral("Tp0X")}},
                {QStringLiteral("CPU Performance Core 8"), {QStringLiteral("Tp0b")}},
                {QStringLiteral("GPU Cluster 1"), {QStringLiteral("Tg05")}},
                {QStringLiteral("GPU Cluster 2"), {QStringLiteral("Tg0D")}},
                {QStringLiteral("GPU Cluster 3"), {QStringLiteral("Tg0L")}},
                {QStringLiteral("GPU Cluster 4"), {QStringLiteral("Tg0T")}},
                {QStringLiteral("GPU Cluster 5"), {QStringLiteral("Tg1b")}},
                {QStringLiteral("GPU Cluster 6"), {QStringLiteral("Tg4b")}},
            },
        },
        {
            QStringLiteral("M2"),
            {
                {QStringLiteral("CPU Efficiency Core 1"), {QStringLiteral("Tp1h")}},
                {QStringLiteral("CPU Efficiency Core 2"), {QStringLiteral("Tp1t")}},
                {QStringLiteral("CPU Efficiency Core 3"), {QStringLiteral("Tp1p")}},
                {QStringLiteral("CPU Efficiency Core 4"), {QStringLiteral("Tp1l")}},
                {QStringLiteral("CPU Performance Core 1"), {QStringLiteral("Tp01")}},
                {QStringLiteral("CPU Performance Core 2"), {QStringLiteral("Tp05")}},
                {QStringLiteral("CPU Performance Core 3"), {QStringLiteral("Tp09")}},
                {QStringLiteral("CPU Performance Core 4"), {QStringLiteral("Tp0D")}},
                {QStringLiteral("CPU Performance Core 5"), {QStringLiteral("Tp0X")}},
                {QStringLiteral("CPU Performance Core 6"), {QStringLiteral("Tp0b")}},
                {QStringLiteral("CPU Performance Core 7"), {QStringLiteral("Tp0f")}},
                {QStringLiteral("CPU Performance Core 8"), {QStringLiteral("Tp0j")}},
                {QStringLiteral("GPU Cluster 1"), {QStringLiteral("Tg0f")}},
                {QStringLiteral("GPU Cluster 2"), {QStringLiteral("Tg0j")}},
            },
        },
        {
            QStringLiteral("M3"),
            {
                {QStringLiteral("CPU Efficiency Core 1"), {QStringLiteral("Te05")}},
                {QStringLiteral("CPU Efficiency Core 2"), {QStringLiteral("Te0L")}},
                {QStringLiteral("CPU Efficiency Core 3"), {QStringLiteral("Te0P")}},
                {QStringLiteral("CPU Efficiency Core 4"), {QStringLiteral("Te0S")}},
                {QStringLiteral("CPU Performance Core 1"), {QStringLiteral("Tf04")}},
                {QStringLiteral("CPU Performance Core 2"), {QStringLiteral("Tf09")}},
                {QStringLiteral("CPU Performance Core 3"), {QStringLiteral("Tf0A")}},
                {QStringLiteral("CPU Performance Core 4"), {QStringLiteral("Tf0B")}},
                {QStringLiteral("CPU Performance Core 5"), {QStringLiteral("Tf0D")}},
                {QStringLiteral("CPU Performance Core 6"), {QStringLiteral("Tf0E")}},
                {QStringLiteral("CPU Performance Core 7"), {QStringLiteral("Tf44")}},
                {QStringLiteral("CPU Performance Core 8"), {QStringLiteral("Tf49")}},
                {QStringLiteral("CPU Performance Core 9"), {QStringLiteral("Tf4A")}},
                {QStringLiteral("CPU Performance Core 10"), {QStringLiteral("Tf4B")}},
                {QStringLiteral("CPU Performance Core 11"), {QStringLiteral("Tf4D")}},
                {QStringLiteral("CPU Performance Core 12"), {QStringLiteral("Tf4E")}},
                {QStringLiteral("GPU Cluster 1"), {QStringLiteral("Tf14")}},
                {QStringLiteral("GPU Cluster 2"), {QStringLiteral("Tf18")}},
                {QStringLiteral("GPU Cluster 3"), {QStringLiteral("Tf19")}},
                {QStringLiteral("GPU Cluster 4"), {QStringLiteral("Tf1A")}},
                {QStringLiteral("GPU Cluster 5"), {QStringLiteral("Tf24")}},
                {QStringLiteral("GPU Cluster 6"), {QStringLiteral("Tf28")}},
                {QStringLiteral("GPU Cluster 7"), {QStringLiteral("Tf29")}},
                {QStringLiteral("GPU Cluster 8"), {QStringLiteral("Tf2A")}},
            },
        },
        {
            QStringLiteral("M4"),
            {
                {QStringLiteral("CPU Efficiency Core 1"), {QStringLiteral("Te05")}},
                {QStringLiteral("CPU Efficiency Core 2"), {QStringLiteral("Te0S")}},
                {QStringLiteral("CPU Efficiency Core 3"), {QStringLiteral("Te09"), QStringLiteral("Te06")}},
                {QStringLiteral("CPU Efficiency Core 4"), {QStringLiteral("Te0H"), QStringLiteral("Te0T")}},
                {QStringLiteral("CPU Performance Core 1"), {QStringLiteral("Tp01")}},
                {QStringLiteral("CPU Performance Core 2"), {QStringLiteral("Tp05")}},
                {QStringLiteral("CPU Performance Core 3"), {QStringLiteral("Tp09")}},
                {QStringLiteral("CPU Performance Core 4"), {QStringLiteral("Tp0D")}},
                {QStringLiteral("CPU Performance Core 5"), {QStringLiteral("Tp0V"), QStringLiteral("Tp0H")}},
                {QStringLiteral("CPU Performance Core 6"), {QStringLiteral("Tp0Y")}},
                {QStringLiteral("CPU Performance Core 7"), {QStringLiteral("Tp0b")}},
                {QStringLiteral("CPU Performance Core 8"), {QStringLiteral("Tp0e")}},
                {QStringLiteral("GPU Cluster 1"), {QStringLiteral("Tg0G"), QStringLiteral("Tg1U")}},
                {QStringLiteral("GPU Cluster 2"), {QStringLiteral("Tg0H"), QStringLiteral("Tg1k")}},
                {QStringLiteral("GPU Cluster 3"), {QStringLiteral("Tg0K")}},
                {QStringLiteral("GPU Cluster 4"), {QStringLiteral("Tg0L")}},
                {QStringLiteral("GPU Cluster 5"), {QStringLiteral("Tg0d")}},
                {QStringLiteral("GPU Cluster 6"), {QStringLiteral("Tg0e")}},
                {QStringLiteral("GPU Cluster 7"), {QStringLiteral("Tg0j")}},
                {QStringLiteral("GPU Cluster 8"), {QStringLiteral("Tg0k")}},
            },
        },
        {
            QStringLiteral("M5"),
            {
                {QStringLiteral("CPU Super Core 1"), {QStringLiteral("Tp00")}},
                {QStringLiteral("CPU Super Core 2"), {QStringLiteral("Tp04")}},
                {QStringLiteral("CPU Super Core 3"), {QStringLiteral("Tp08")}},
                {QStringLiteral("CPU Super Core 4"), {QStringLiteral("Tp0C")}},
                {QStringLiteral("CPU Super Core 5"), {QStringLiteral("Tp0G")}},
                {QStringLiteral("CPU Super Core 6"), {QStringLiteral("Tp0K")}},
                {QStringLiteral("CPU Performance Core 1"), {QStringLiteral("Tp0O")}},
                {QStringLiteral("CPU Performance Core 2"), {QStringLiteral("Tp0R")}},
                {QStringLiteral("CPU Performance Core 3"), {QStringLiteral("Tp0U")}},
                {QStringLiteral("CPU Performance Core 4"), {QStringLiteral("Tp0X")}},
                {QStringLiteral("CPU Performance Core 5"), {QStringLiteral("Tp0a")}},
                {QStringLiteral("CPU Performance Core 6"), {QStringLiteral("Tp0d")}},
                {QStringLiteral("CPU Performance Core 7"), {QStringLiteral("Tp0g")}},
                {QStringLiteral("CPU Performance Core 8"), {QStringLiteral("Tp0j")}},
                {QStringLiteral("CPU Performance Core 9"), {QStringLiteral("Tp0m")}},
                {QStringLiteral("CPU Performance Core 10"), {QStringLiteral("Tp0p")}},
                {QStringLiteral("CPU Performance Core 11"), {QStringLiteral("Tp0u")}},
                {QStringLiteral("CPU Performance Core 12"), {QStringLiteral("Tp0y")}},
                {QStringLiteral("GPU Cluster 1"), {QStringLiteral("Tg0U")}},
                {QStringLiteral("GPU Cluster 2"), {QStringLiteral("Tg0X")}},
                {QStringLiteral("GPU Cluster 3"), {QStringLiteral("Tg0d")}},
                {QStringLiteral("GPU Cluster 4"), {QStringLiteral("Tg0g")}},
                {QStringLiteral("GPU Cluster 5"), {QStringLiteral("Tg0j")}},
                {QStringLiteral("GPU Cluster 6"), {QStringLiteral("Tg1Y")}},
                {QStringLiteral("GPU Cluster 7"), {QStringLiteral("Tg1c")}},
                {QStringLiteral("GPU Cluster 8"), {QStringLiteral("Tg1g")}},
            },
        },
    };
}

QString sysctlString(const char *name)
{
    size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return {};
    }

    QByteArray value(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0 || size == 0) {
        return {};
    }

    return QString::fromUtf8(value.constData()).trimmed();
}

QString detectedAppleSiliconFamily()
{
    const QStringList candidates = {
        sysctlString("machdep.cpu.brand_string"),
        sysctlString("hw.model"),
    };

    for (const QString &candidate : candidates) {
        const QRegularExpression match(QStringLiteral("\\bM([1-5])\\b"), QRegularExpression::CaseInsensitiveOption);
        const auto result = match.match(candidate);
        if (result.hasMatch()) {
            return QStringLiteral("M%1").arg(result.captured(1));
        }
    }

    return {};
}

bool readBestGroupTemperature(const AppleSmcReader &smc, const SmcSensorGroup &group, double &selectedTemperature, QString &selectedKey)
{
    selectedTemperature = 0.0;
    selectedKey.clear();

    for (const QString &key : group.keys) {
        double temperature = 0.0;
        if (!smc.readTemperature(key, temperature)) {
            continue;
        }

        if (!plausibleTemperature(selectedTemperature) || temperature > selectedTemperature) {
            selectedTemperature = temperature;
            selectedKey = key;
        }
    }

    return plausibleTemperature(selectedTemperature);
}

QList<SmcSensorGroup> selectedSmcTemperatureGroups(const AppleSmcReader &smc)
{
    const QList<SmcSensorProfile> profiles = smcTemperatureProfiles();
    static QString cachedFamily;

    const QString detectedFamily = detectedAppleSiliconFamily();
    if (!detectedFamily.isEmpty()) {
        cachedFamily = detectedFamily;
    }

    if (!cachedFamily.isEmpty()) {
        const auto profile = std::find_if(profiles.cbegin(), profiles.cend(), [](const SmcSensorProfile &candidate) {
            return candidate.family == cachedFamily;
        });
        if (profile != profiles.cend()) {
            return sharedSmcTemperatureGroups() + profile->groups;
        }
    }

    int bestScore = 0;
    QList<SmcSensorGroup> bestGroups;
    for (const SmcSensorProfile &profile : profiles) {
        int score = 0;
        for (const SmcSensorGroup &group : profile.groups) {
            double temperature = 0.0;
            QString key;
            if (readBestGroupTemperature(smc, group, temperature, key)) {
                ++score;
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestGroups = profile.groups;
            cachedFamily = profile.family;
        }
    }

    return sharedSmcTemperatureGroups() + bestGroups;
}


QString registryStringProperty(io_registry_entry_t entry, CFStringRef key)
{
    ScopedCFRelease property(IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0));
    if (!property.get()) {
        return {};
    }

    if (CFGetTypeID(property.get()) == CFStringGetTypeID()) {
        return cfStringToQString(static_cast<CFStringRef>(property.get()));
    }

    if (CFGetTypeID(property.get()) == CFDataGetTypeID()) {
        auto *data = static_cast<CFDataRef>(property.get());
        const auto *bytes = reinterpret_cast<const char *>(CFDataGetBytePtr(data));
        const CFIndex size = CFDataGetLength(data);
        if (bytes && size > 0) {
            return QString::fromUtf8(bytes, static_cast<qsizetype>(size)).trimmed();
        }
    }

    return {};
}

QString registryEntryName(io_registry_entry_t entry)
{
    io_name_t name = {};
    if (IORegistryEntryGetName(entry, name) == KERN_SUCCESS) {
        return QString::fromUtf8(name);
    }

    return {};
}

QString sensorIdFromName(const QString &rawName, quint64 registryId = 0)
{
    QString slug;
    slug.reserve(rawName.size());

    for (const QChar character : rawName.toLower()) {
        if (character.isLetterOrNumber()) {
            slug.append(character);
        } else if (!slug.endsWith(QLatin1Char('-'))) {
            slug.append(QLatin1Char('-'));
        }
    }

    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }

    if (slug.isEmpty()) {
        slug = QStringLiteral("temperature-sensor");
    }

    if (registryId != 0) {
        slug += QLatin1Char('-') + QString::number(registryId, 16);
    }

    return QStringLiteral("apple-silicon-%1").arg(slug);
}

bool shouldHideSensor(const QString &rawName)
{
    QString normalizedName = rawName;
    normalizedName.replace(QStringLiteral(" MTR Temp Sensor"), QStringLiteral(""));
    normalizedName.replace(QStringLiteral(" Temp Sensor"), QStringLiteral(""));
    normalizedName.replace(QStringLiteral("Temperature Sensor"), QStringLiteral(""));
    normalizedName = normalizedName.simplified();

    if (normalizedName.compare(QStringLiteral("PMU tcal"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QRegularExpression pmuDiePattern(QStringLiteral("^PMU\\d*\\s+tdie\\d+$"),
                                           QRegularExpression::CaseInsensitiveOption);
    if (pmuDiePattern.match(normalizedName).hasMatch()) {
        return true;
    }

    const QRegularExpression hidCpuGpuPattern(QStringLiteral("^(eACC|pACC|ECPU|PCPU|GPU)\\s*\\d*$"),
                                              QRegularExpression::CaseInsensitiveOption);
    return hidCpuGpuPattern.match(normalizedName).hasMatch();
}

void upsertSensor(QList<SensorInfo> &sensors, SensorInfo sensor)
{
    const auto existing = std::find_if(sensors.begin(), sensors.end(), [&](const SensorInfo &candidate) {
        return candidate.name.compare(sensor.name, Qt::CaseInsensitive) == 0;
    });

    if (existing == sensors.end()) {
        sensors.append(std::move(sensor));
        return;
    }

    if (!existing->available && sensor.available) {
        *existing = std::move(sensor);
        return;
    }

    if (existing->available == sensor.available && sensor.temperatureCelsius > existing->temperatureCelsius) {
        existing->temperatureCelsius = sensor.temperatureCelsius;
        existing->id = sensor.id;
        existing->source = sensor.source;
    }
}

void appendAverageSensor(QList<SensorInfo> &sensors, const QString &name, const QList<double> &temperatures)
{
    if (temperatures.isEmpty()) {
        return;
    }

    double sum = 0.0;
    for (const double temperature : temperatures) {
        sum += temperature;
    }

    SensorInfo sensor;
    sensor.id = sensorIdFromName(name);
    sensor.name = name;
    sensor.temperatureCelsius = sum / static_cast<double>(temperatures.size());
    sensor.source = QStringLiteral("SMC average (%1 sensors)").arg(temperatures.size());
    sensor.available = true;
    upsertSensor(sensors, std::move(sensor));
}

bool appendSmcTemperatureSensors(QList<SensorInfo> &sensors)
{
    AppleSmcReader smc;
    if (!smc.isOpen()) {
        return false;
    }

    QList<double> cpuTemperatures;
    QList<double> gpuTemperatures;

    bool foundSensor = false;
    for (const SmcSensorGroup &group : selectedSmcTemperatureGroups(smc)) {
        double selectedTemperature = 0.0;
        QString selectedKey;
        if (!readBestGroupTemperature(smc, group, selectedTemperature, selectedKey)) {
            continue;
        }

        SensorInfo sensor;
        sensor.id = sensorIdFromName(group.name);
        sensor.name = group.name;
        sensor.temperatureCelsius = selectedTemperature;
        sensor.source = QStringLiteral("SMC %1").arg(selectedKey);
        sensor.available = true;
        upsertSensor(sensors, std::move(sensor));

        if (group.name.startsWith(QStringLiteral("CPU "), Qt::CaseInsensitive)) {
            cpuTemperatures.append(selectedTemperature);
        } else if (group.name.startsWith(QStringLiteral("GPU "), Qt::CaseInsensitive)) {
            gpuTemperatures.append(selectedTemperature);
        }

        foundSensor = true;
    }

    appendAverageSensor(sensors, QStringLiteral("CPU Average"), cpuTemperatures);
    appendAverageSensor(sensors, QStringLiteral("GPU Average"), gpuTemperatures);

    return foundSensor;
}

bool appendIoRegistryTemperatureSensors(QList<SensorInfo> &sensors, QSet<QString> &seenNames)
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t result = IOServiceGetMatchingServices(kIOMainPortDefault,
                                                        IOServiceMatching("AppleARMPMUTempSensor"),
                                                        &iterator);
    if (result != KERN_SUCCESS || iterator == IO_OBJECT_NULL) {
        return false;
    }

    ScopedIoObject iteratorGuard(iterator);
    bool foundSensor = false;

    while (io_object_t service = IOIteratorNext(iteratorGuard.get())) {
        ScopedIoObject serviceGuard(service);

        QString rawName = registryStringProperty(service, CFSTR("Product"));
        if (rawName.isEmpty()) {
            rawName = registryEntryName(service);
        }
        if (rawName.isEmpty()) {
            rawName = QStringLiteral("Apple Silicon Temperature Sensor");
        }

        if (shouldHideSensor(rawName)) {
            continue;
        }

        if (seenNames.contains(rawName)) {
            continue;
        }

        seenNames.insert(rawName);
        foundSensor = true;

        SensorInfo sensor;
        sensor.id = sensorIdFromName(rawName);
        sensor.name = readableSensorName(rawName);
        sensor.temperatureCelsius = 0.0;
        sensor.source = QStringLiteral("%1 / IORegistry").arg(rawName);
        sensor.available = false;
        upsertSensor(sensors, std::move(sensor));
    }

    return foundSensor;
}

CFDictionaryRef createTemperatureSensorMatching()
{
    const void *keys[] = {
        CFSTR("PrimaryUsagePage"),
        CFSTR("PrimaryUsage"),
    };

    const int usagePage = kAppleVendorTemperatureSensorUsagePage;
    const int usage = kAppleVendorTemperatureSensorUsage;
    CFNumberRef usagePageValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usagePage);
    CFNumberRef usageValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);

    const void *values[] = {
        usagePageValue,
        usageValue,
    };

    CFDictionaryRef matching = CFDictionaryCreate(kCFAllocatorDefault,
                                                  keys,
                                                  values,
                                                  2,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);

    CFRelease(usagePageValue);
    CFRelease(usageValue);
    return matching;
}

void appendTemperatureServices(CFArrayRef serviceArray, QList<SensorInfo> &sensors)
{
    const CFIndex count = CFArrayGetCount(serviceArray);
    for (CFIndex index = 0; index < count; ++index) {
        CFTypeRef service = static_cast<CFTypeRef>(const_cast<void *>(CFArrayGetValueAtIndex(serviceArray, index)));
        if (!service) {
            continue;
        }

        ScopedCFRelease event(IOHIDServiceClientCopyEvent(service, kIOHIDEventTypeTemperature, 0, 0));
        if (!event.get()) {
            continue;
        }

        const double temperature = IOHIDEventGetFloatValue(event.get(), kIOHIDEventFieldTemperatureLevel);
        if (temperature <= 0.0 || temperature > 150.0) {
            continue;
        }

        ScopedCFRelease product(IOHIDServiceClientCopyProperty(service, CFSTR("Product")));
        QString rawName;
        if (product.get() && CFGetTypeID(product.get()) == CFStringGetTypeID()) {
            rawName = cfStringToQString(static_cast<CFStringRef>(product.get()));
        }

        if (rawName.isEmpty()) {
            rawName = QStringLiteral("Apple Silicon Temperature %1").arg(QString::number(index + 1));
        }

        if (shouldHideSensor(rawName)) {
            continue;
        }

        QString displayName = readableSensorName(rawName);

        SensorInfo sensor;
        sensor.id = sensorIdFromName(displayName);
        sensor.name = displayName;
        sensor.temperatureCelsius = temperature;
        sensor.source = rawName;
        sensor.available = true;
        upsertSensor(sensors, std::move(sensor));
    }
}

QString readableSensorName(const QString &rawName)
{
    QString name = rawName;
    name.replace(QStringLiteral(" MTR Temp Sensor"), QStringLiteral(""));
    name.replace(QStringLiteral(" Temp Sensor"), QStringLiteral(""));
    name.replace(QStringLiteral("Temperature Sensor"), QStringLiteral(""));
    name = name.simplified();

    const QRegularExpression smcKeyPattern(QStringLiteral("^T([epg])([A-Za-z0-9]+).*$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const auto smcMatch = smcKeyPattern.match(name);
    if (smcMatch.hasMatch()) {
        const QString kind = smcMatch.captured(1).toLower();
        const QString suffix = smcMatch.captured(2);
        if (kind == QStringLiteral("e")) {
            return QStringLiteral("CPU Efficiency Sensor %1").arg(suffix);
        }
        if (kind == QStringLiteral("p")) {
            return QStringLiteral("CPU Performance Sensor %1").arg(suffix);
        }
        if (kind == QStringLiteral("g")) {
            return QStringLiteral("GPU Cluster Sensor %1").arg(suffix);
        }
    }

    const QRegularExpression pmuDiePattern(QStringLiteral("^(PMU\\d*)\\s+tdie(\\d+)$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const auto pmuDieMatch = pmuDiePattern.match(name);
    if (pmuDieMatch.hasMatch()) {
        return QStringLiteral("Power Manager Die %1").arg(pmuDieMatch.captured(2));
    }

    const QRegularExpression pmuDevicePattern(QStringLiteral("^PMU\\s+tdev(\\d+)$"),
                                              QRegularExpression::CaseInsensitiveOption);
    const auto pmuDeviceMatch = pmuDevicePattern.match(name);
    if (pmuDeviceMatch.hasMatch()) {
        return QStringLiteral("Device Sensor %1").arg(pmuDeviceMatch.captured(1));
    }

    if (name.compare(QStringLiteral("gas gauge battery"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("Battery Gas Gauge"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("Battery"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Battery");
    }

    name.replace(QStringLiteral("PMGR"), QStringLiteral("PMGR "));
    name.replace(QStringLiteral("PMU "), QStringLiteral("PMU "));
    return name.simplified();
}

#endif

} // namespace

MacHardwareBackend::MacHardwareBackend(QObject *parent)
    : IHardwareBackend(parent)
{
    scan();
}

void MacHardwareBackend::scan()
{
#ifdef Q_OS_MACOS
    m_sensors.clear();

    if (QSysInfo::currentCpuArchitecture() != QStringLiteral("arm64")) {
        emit hardwareChanged();
        return;
    }

    appendSmcTemperatureSensors(m_sensors);

    QSet<QString> registrySeenNames;

    ScopedCFRelease client(IOHIDEventSystemClientCreate(kCFAllocatorDefault));
    if (client.get()) {
        ScopedCFRelease matching(createTemperatureSensorMatching());
        IOHIDEventSystemClientSetMatching(client.get(), static_cast<CFDictionaryRef>(matching.get()));

        ScopedCFRelease services(IOHIDEventSystemClientCopyServices(client.get()));
        if (services.get() && CFGetTypeID(services.get()) == CFArrayGetTypeID()) {
            appendTemperatureServices(static_cast<CFArrayRef>(services.get()), m_sensors);
        }
    }

    if (m_sensors.isEmpty()) {
        ScopedCFRelease fallbackClient(IOHIDEventSystemClientCreate(kCFAllocatorDefault));
        if (fallbackClient.get()) {
            ScopedCFRelease allServices(IOHIDEventSystemClientCopyServices(fallbackClient.get()));
            if (allServices.get() && CFGetTypeID(allServices.get()) == CFArrayGetTypeID()) {
                appendTemperatureServices(static_cast<CFArrayRef>(allServices.get()), m_sensors);
            }
        }
    }

    appendIoRegistryTemperatureSensors(m_sensors, registrySeenNames);

    QCollator collator;
    collator.setNumericMode(true);

    std::sort(m_sensors.begin(), m_sensors.end(), [&collator](const SensorInfo &left, const SensorInfo &right) {
        if (left.available != right.available) {
            return left.available && !right.available;
        }

        return collator.compare(left.name, right.name) < 0;
    });
#endif

    emit hardwareChanged();
}

QList<SensorInfo> MacHardwareBackend::sensors() const
{
    return m_sensors;
}

QList<FanInfo> MacHardwareBackend::fans() const
{
    return {};
}

bool MacHardwareBackend::setFanSpeed(const QString &fanId, double percent)
{
    Q_UNUSED(fanId)
    Q_UNUSED(percent)
    return false;
}

bool MacHardwareBackend::restoreAutomaticControl(const QString &fanId)
{
    Q_UNUSED(fanId)
    return false;
}

} // namespace thermvane
