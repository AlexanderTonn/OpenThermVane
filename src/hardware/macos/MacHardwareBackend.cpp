#include "hardware/macos/MacHardwareBackend.hpp"

#include <QCollator>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <unistd.h>
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
constexpr uint8_t kSmcCommandWriteBytes = 6;

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
    return std::isfinite(temperature) && temperature >= 5.0 && temperature < 150.0;
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

double decodeSmcFloatNumber(const std::array<uint8_t, 32> &bytes)
{
    float nativeValue = 0.0F;
    std::memcpy(&nativeValue, bytes.data(), sizeof(nativeValue));
    if (std::isfinite(nativeValue) && std::abs(nativeValue) < 1000000.0F) {
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
        uint32_t type = 0;
        std::array<uint8_t, 32> bytes = {};
        if (!readKey(key, type, bytes)) {
            return false;
        }

        if (type == fourCharCode("flt ")) {
            temperature = decodeSmcFloat(bytes);
        } else if (type == fourCharCode("fpe2")) {
            temperature = decodeSmcFixedPoint(bytes, 2);
        } else if (type == fourCharCode("sp78")) {
            const int16_t raw = static_cast<int16_t>((static_cast<uint16_t>(bytes[0]) << 8)
                                                     | static_cast<uint16_t>(bytes[1]));
            temperature = static_cast<double>(raw) / 256.0;
        } else {
            return false;
        }

        return plausibleTemperature(temperature);
    }

    bool readNumber(const QString &key, double &number) const
    {
        uint32_t type = 0;
        std::array<uint8_t, 32> bytes = {};
        if (!readKey(key, type, bytes)) {
            return false;
        }

        if (type == fourCharCode("flt ")) {
            number = decodeSmcFloatNumber(bytes);
        } else if (type == fourCharCode("fpe2")) {
            number = decodeSmcFixedPoint(bytes, 2);
        } else if (type == fourCharCode("ui8 ")) {
            number = bytes[0];
        } else if (type == fourCharCode("ui16")) {
            number = (static_cast<uint16_t>(bytes[0]) << 8) | static_cast<uint16_t>(bytes[1]);
        } else if (type == fourCharCode("ui32")) {
            number = (static_cast<uint32_t>(bytes[0]) << 24)
                | (static_cast<uint32_t>(bytes[1]) << 16)
                | (static_cast<uint32_t>(bytes[2]) << 8)
                | static_cast<uint32_t>(bytes[3]);
        } else {
            return false;
        }

        return std::isfinite(number);
    }

    bool readString(const QString &key, QString &text) const
    {
        uint32_t type = 0;
        std::array<uint8_t, 32> bytes = {};
        SmcKeyInfo info;
        if (!readKey(key, type, bytes, &info)) {
            return false;
        }

        const qsizetype length = static_cast<qsizetype>(std::min<uint32_t>(info.dataSize, bytes.size()));
        QByteArray data(reinterpret_cast<const char *>(bytes.data()), length);
        const int nulIndex = data.indexOf('\0');
        if (nulIndex >= 0) {
            data.truncate(nulIndex);
        }

        text = QString::fromUtf8(data).simplified();
        return !text.isEmpty();
    }

    bool writeNumber(const QString &key, double number) const
    {
        SmcKeyInfo info;
        if (!readKeyInfo(key, info)) {
            return false;
        }

        std::array<uint8_t, 32> bytes = {};
        if (info.dataType == fourCharCode("flt ")) {
            const float value = static_cast<float>(number);
            std::memcpy(bytes.data(), &value, sizeof(value));
        } else if (info.dataType == fourCharCode("fpe2")) {
            const uint16_t raw = static_cast<uint16_t>(std::lround(std::clamp(number, 0.0, 65535.0 / 4.0) * 4.0));
            bytes[0] = static_cast<uint8_t>((raw >> 8) & 0xff);
            bytes[1] = static_cast<uint8_t>(raw & 0xff);
        } else if (info.dataType == fourCharCode("ui8 ")) {
            bytes[0] = static_cast<uint8_t>(std::lround(std::clamp(number, 0.0, 255.0)));
        } else if (info.dataType == fourCharCode("ui16")) {
            const uint16_t raw = static_cast<uint16_t>(std::lround(std::clamp(number, 0.0, 65535.0)));
            bytes[0] = static_cast<uint8_t>((raw >> 8) & 0xff);
            bytes[1] = static_cast<uint8_t>(raw & 0xff);
        } else {
            return false;
        }

        return writeKey(key, info, bytes);
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


    bool readKeyInfo(const QString &key, SmcKeyInfo &info) const
    {
        if (!isOpen()) {
            return false;
        }

        SmcKeyData input;
        SmcKeyData output;
        input.key = keyCode(key);
        input.data8 = kSmcCommandReadKeyInfo;
        if (input.key == 0 || !call(input, output) || output.result != 0) {
            return false;
        }

        info = output.keyInfo;
        return info.dataSize > 0 && info.dataSize <= 32;
    }

    bool readKey(const QString &key, uint32_t &type, std::array<uint8_t, 32> &bytes, SmcKeyInfo *keyInfo = nullptr) const
    {
        SmcKeyInfo info;
        if (!readKeyInfo(key, info)) {
            return false;
        }

        SmcKeyData input;
        SmcKeyData output;
        input.key = keyCode(key);
        input.keyInfo = info;
        input.data8 = kSmcCommandReadBytes;
        if (!call(input, output) || output.result != 0) {
            return false;
        }

        type = info.dataType;
        bytes = output.bytes;
        if (keyInfo) {
            *keyInfo = info;
        }
        return true;
    }

    bool writeKey(const QString &key, const SmcKeyInfo &info, const std::array<uint8_t, 32> &bytes) const
    {
        if (!isOpen()) {
            return false;
        }

        SmcKeyData input;
        SmcKeyData output;
        input.key = keyCode(key);
        input.keyInfo = info;
        input.data8 = kSmcCommandWriteBytes;
        input.bytes = bytes;
        return input.key != 0 && call(input, output) && output.result == 0;
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

QString smcFanKey(int fanIndex, const QString &suffix)
{
    return QStringLiteral("F%1%2").arg(fanIndex).arg(suffix);
}

QString smcFanId(int fanIndex)
{
    return QStringLiteral("smc-fan-%1").arg(fanIndex);
}


QString shellQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

QString appleScriptString(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

QString privilegedHelperSocketPath()
{
    return QStringLiteral("/tmp/thermvane-fan-helper-%1.sock").arg(getuid());
}

bool writeAllToSocket(int socketFd, const QByteArray &message)
{
    const char *data = message.constData();
    qsizetype remaining = message.size();
    while (remaining > 0) {
        const ssize_t written = write(socketFd, data, static_cast<size_t>(remaining));
        if (written <= 0) {
            return false;
        }
        data += written;
        remaining -= written;
    }
    return true;
}

bool sendCommandToHelperServer(const QByteArray &command)
{
    const QByteArray socketPath = privilegedHelperSocketPath().toUtf8();
    if (socketPath.size() >= static_cast<int>(sizeof(sockaddr_un::sun_path))) {
        return false;
    }

    const int socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFd < 0) {
        return false;
    }

    timeval timeout = {1, 0};
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socketPath.constData(), sizeof(address.sun_path) - 1);

    if (connect(socketFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        close(socketFd);
        return false;
    }

    if (!writeAllToSocket(socketFd, command)) {
        close(socketFd);
        return false;
    }

    char response[128] = {};
    const ssize_t count = read(socketFd, response, sizeof(response) - 1);
    close(socketFd);
    if (count <= 0) {
        return false;
    }

    const QByteArray reply(response, count);
    if (!reply.startsWith("OK")) {
        qWarning() << "Fan helper command failed" << reply.trimmed();
        return false;
    }

    return true;
}

bool pingHelperServer()
{
    return sendCommandToHelperServer(QByteArrayLiteral("PING\n"));
}

bool sendFanSpeedToHelperServer(const QString &fanId, double percent)
{
    const QByteArray command = QByteArrayLiteral("SET ")
        + fanId.toUtf8()
        + QByteArrayLiteral(" ")
        + QByteArray::number(percent, 'f', 2)
        + QByteArrayLiteral("\n");
    return sendCommandToHelperServer(command);
}

bool startPrivilegedHelperServer(const QString &fanId, double percent)
{
    if (qEnvironmentVariableIsSet("THERMVANE_NO_ADMIN_FALLBACK")) {
        return false;
    }

    static qint64 lastStartRequestMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - lastStartRequestMs < 15000) {
        return true;
    }
    lastStartRequestMs = nowMs;

    const QString helperPath = QCoreApplication::applicationDirPath() + QStringLiteral("/ThermVaneFanHelper");
    if (!QFileInfo::exists(helperPath)) {
        qWarning() << "Fan helper not found" << helperPath;
        return false;
    }

    const QString logPath = QStringLiteral("/tmp/thermvane-fan-helper-%1.log").arg(getuid());
    const QString command = QStringLiteral("cd / && ")
        + shellQuote(helperPath)
        + QStringLiteral(" --server ")
        + shellQuote(privilegedHelperSocketPath())
        + QStringLiteral(" </dev/null >> ")
        + shellQuote(logPath)
        + QStringLiteral(" 2>&1 & sleep 0.4; ")
        + shellQuote(helperPath)
        + QStringLiteral(" ")
        + shellQuote(fanId)
        + QStringLiteral(" ")
        + shellQuote(QString::number(percent, 'f', 2))
        + QStringLiteral(" >> ")
        + shellQuote(logPath)
        + QStringLiteral(" 2>&1");
    const QString script = QStringLiteral("do shell script ")
        + appleScriptString(command)
        + QStringLiteral(" with administrator privileges");

    if (!QProcess::startDetached(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), script})) {
        qWarning() << "Could not start privileged fan helper request";
        return false;
    }

    return true;
}

bool runPrivilegedSetFanSpeed(const QString &fanId, double percent)
{
    if (sendFanSpeedToHelperServer(fanId, percent)) {
        return true;
    }

    if (!startPrivilegedHelperServer(fanId, percent)) {
        return false;
    }

    // The administrator dialog is asynchronous. Keep the requested UI state;
    // the next manual commit will use the helper server once it is ready.
    return true;
}

int fanIndexFromId(const QString &fanId)
{
    const QRegularExpression pattern(QStringLiteral("^smc-fan-(\\d+)$"));
    const auto match = pattern.match(fanId);
    if (!match.hasMatch()) {
        return -1;
    }

    bool ok = false;
    const int index = match.captured(1).toInt(&ok);
    return ok ? index : -1;
}

QString fanModeKey(const AppleSmcReader &smc, int fanIndex)
{
    const QString upperKey = smcFanKey(fanIndex, QStringLiteral("Md"));
    double mode = 0.0;
    if (smc.readNumber(upperKey, mode)) {
        return upperKey;
    }

    const QString lowerKey = smcFanKey(fanIndex, QStringLiteral("md"));
    if (smc.readNumber(lowerKey, mode)) {
        return lowerKey;
    }

    return {};
}

bool readFanCount(const AppleSmcReader &smc, int &fanCount)
{
    double value = 0.0;
    if (smc.readNumber(QStringLiteral("FNum"), value)) {
        fanCount = std::clamp(static_cast<int>(std::lround(value)), 0, 8);
        return true;
    }

    fanCount = 0;
    for (int index = 0; index < 8; ++index) {
        double rpm = 0.0;
        if (smc.readNumber(smcFanKey(index, QStringLiteral("Ac")), rpm)) {
            fanCount = index + 1;
        }
    }

    return fanCount > 0;
}


bool readFanSwitchMask(const AppleSmcReader &smc, uint16_t &mask)
{
    double value = 0.0;
    if (!smc.readNumber(QStringLiteral("FS! "), value)
        || !std::isfinite(value)
        || value < 0.0
        || value > 65535.0) {
        return false;
    }

    mask = static_cast<uint16_t>(std::lround(value));
    return true;
}

QString readableFanName(const AppleSmcReader &smc, int fanIndex, int fanCount)
{
    QString smcName;
    if (smc.readString(smcFanKey(fanIndex, QStringLiteral("ID")), smcName)) {
        const QRegularExpression printable(QStringLiteral("^[\\x20-\\x7e]+$"));
        if (printable.match(smcName).hasMatch()) {
            return smcName;
        }
    }

    if (fanCount == 1) {
        return QStringLiteral("MacBook Fan");
    }

    if (fanIndex == 0) {
        return QStringLiteral("Left Fan");
    }

    if (fanIndex == 1) {
        return QStringLiteral("Right Fan");
    }

    return QStringLiteral("Fan %1").arg(fanIndex + 1);
}

void appendSmcFans(QList<FanInfo> &fans, QHash<QString, double> *autoFanModes, QHash<QString, double> *pendingManualFanSpeeds)
{
    AppleSmcReader smc;
    if (!smc.isOpen()) {
        return;
    }

    int fanCount = 0;
    if (!readFanCount(smc, fanCount)) {
        return;
    }

    uint16_t manualFanMask = 0;
    const bool manualFanMaskReadable = readFanSwitchMask(smc, manualFanMask);

    for (int index = 0; index < fanCount; ++index) {
        double rpm = 0.0;
        if (!smc.readNumber(smcFanKey(index, QStringLiteral("Ac")), rpm)
            || !std::isfinite(rpm)
            || rpm < 0.0
            || rpm > 20000.0) {
            continue;
        }

        double minRpm = 0.0;
        double maxRpm = 0.0;
        const bool minReadable = smc.readNumber(smcFanKey(index, QStringLiteral("Mn")), minRpm);
        const bool maxReadable = smc.readNumber(smcFanKey(index, QStringLiteral("Mx")), maxRpm);
        const bool limitsReadable = minReadable
            && maxReadable
            && std::isfinite(minRpm)
            && std::isfinite(maxRpm)
            && minRpm >= 0.0
            && maxRpm > minRpm
            && maxRpm <= 20000.0;

        if (limitsReadable && rpm > maxRpm * 1.25) {
            continue;
        }

        double targetRpm = rpm;
        bool targetReadable = smc.readNumber(smcFanKey(index, QStringLiteral("Tg")), targetRpm)
            && std::isfinite(targetRpm)
            && targetRpm >= 0.0
            && targetRpm <= 20000.0;
        if (targetReadable && limitsReadable && targetRpm > maxRpm * 1.25) {
            targetReadable = false;
            targetRpm = rpm;
        }

        const QString modeKey = fanModeKey(smc, index);
        double mode = 0.0;
        const bool modeReadable = !modeKey.isEmpty() && smc.readNumber(modeKey, mode);
        const bool manualBySwitch = manualFanMaskReadable && ((manualFanMask & (1U << index)) != 0);

        FanInfo fan;
        fan.id = smcFanId(index);
        fan.name = readableFanName(smc, index, fanCount);
        fan.rpm = static_cast<int>(std::lround(rpm));
        fan.automatic = manualFanMaskReadable
            ? !manualBySwitch
            : (!modeReadable || static_cast<int>(std::lround(mode)) != 1);
        fan.capabilities.rpmReading = true;
        fan.capabilities.manualControl = (manualFanMaskReadable || modeReadable) && targetReadable && limitsReadable;
        fan.capabilities.firmwareControl = manualFanMaskReadable || modeReadable;
        fan.capabilities.zeroRpm = limitsReadable && minRpm <= 0.0;
        fan.capabilities.pwmControl = false;

        if (autoFanModes && !manualFanMaskReadable && modeReadable && fan.automatic) {
            autoFanModes->insert(fan.id, mode);
        }

        if (limitsReadable) {
            const double referenceRpm = fan.automatic ? rpm : targetRpm;
            fan.speedPercent = std::clamp((referenceRpm - minRpm) * 100.0 / (maxRpm - minRpm), 0.0, 100.0);
        } else {
            fan.speedPercent = 0.0;
        }

        if (pendingManualFanSpeeds) {
            if (!fan.automatic && pendingManualFanSpeeds->contains(fan.id)) {
                const double pendingPercent = pendingManualFanSpeeds->value(fan.id);
                if (!targetReadable || std::abs(fan.speedPercent - pendingPercent) <= 3.0) {
                    fan.speedPercent = pendingPercent;
                }
            } else if (fan.automatic) {
                pendingManualFanSpeeds->remove(fan.id);
            }
        }

        fans.append(std::move(fan));
    }
}

bool writeFanMode(const AppleSmcReader &smc, int fanIndex, double mode)
{
    const QString upperKey = smcFanKey(fanIndex, QStringLiteral("Md"));
    if (smc.writeNumber(upperKey, mode)) {
        return true;
    }

    const QString lowerKey = smcFanKey(fanIndex, QStringLiteral("md"));
    return smc.writeNumber(lowerKey, mode);
}


bool writeFanSwitchMask(const AppleSmcReader &smc, uint16_t mask)
{
    return smc.writeNumber(QStringLiteral("FS! "), static_cast<double>(mask));
}

bool writeManualFanSwitch(const AppleSmcReader &smc, int fanIndex, bool manual)
{
    uint16_t mask = 0;
    if (readFanSwitchMask(smc, mask)) {
        const uint16_t fanBit = static_cast<uint16_t>(1U << fanIndex);
        if (manual) {
            mask = static_cast<uint16_t>(mask | fanBit);
        } else {
            mask = static_cast<uint16_t>(mask & ~fanBit);
        }
        return writeFanSwitchMask(smc, mask);
    }

    return writeFanMode(smc, fanIndex, manual ? 1.0 : 0.0);
}

bool readFanLimits(const AppleSmcReader &smc, int fanIndex, double &minRpm, double &maxRpm)
{
    return smc.readNumber(smcFanKey(fanIndex, QStringLiteral("Mn")), minRpm)
        && smc.readNumber(smcFanKey(fanIndex, QStringLiteral("Mx")), maxRpm)
        && std::isfinite(minRpm)
        && std::isfinite(maxRpm)
        && minRpm >= 0.0
        && maxRpm > minRpm
        && maxRpm <= 20000.0;
}

void stabilizeSensorReadings(QList<SensorInfo> &current, const QList<SensorInfo> &previous, QHash<QString, int> &missingScans)
{
    QHash<QString, SensorInfo> previousById;
    for (const SensorInfo &sensor : previous) {
        previousById.insert(sensor.id, sensor);
    }

    QSet<QString> currentIds;
    for (SensorInfo &sensor : current) {
        currentIds.insert(sensor.id);
        const auto previousIt = previousById.constFind(sensor.id);
        if (previousIt == previousById.cend()) {
            missingScans.remove(sensor.id);
            continue;
        }

        const double previousTemperature = previousIt->temperatureCelsius;
        const bool suspiciousDrop = previousTemperature > 20.0
            && (sensor.temperatureCelsius < 10.0 || previousTemperature - sensor.temperatureCelsius > 25.0);
        if (suspiciousDrop) {
            sensor.temperatureCelsius = previousTemperature;
            sensor.source = previousIt->source;
        }

        missingScans.remove(sensor.id);
    }

    for (const SensorInfo &sensor : previous) {
        if (currentIds.contains(sensor.id)) {
            continue;
        }

        const int missingCount = missingScans.value(sensor.id, 0) + 1;
        missingScans.insert(sensor.id, missingCount);
        if (missingCount <= 3) {
            current.append(sensor);
        }
    }
}

void stabilizeFanReadings(QList<FanInfo> &current, const QList<FanInfo> &previous, QHash<QString, int> &missingScans)
{
    QHash<QString, FanInfo> previousById;
    for (const FanInfo &fan : previous) {
        previousById.insert(fan.id, fan);
    }

    QSet<QString> currentIds;
    for (FanInfo &fan : current) {
        currentIds.insert(fan.id);
        const auto previousIt = previousById.constFind(fan.id);
        if (previousIt == previousById.cend()) {
            missingScans.remove(fan.id);
            continue;
        }

        if (fan.rpm < 0 || fan.rpm > 20000 || std::abs(fan.rpm - previousIt->rpm) > 8000) {
            fan.rpm = previousIt->rpm;
            fan.speedPercent = previousIt->speedPercent;
            fan.automatic = previousIt->automatic;
        }

        missingScans.remove(fan.id);
    }

    for (const FanInfo &fan : previous) {
        if (currentIds.contains(fan.id)) {
            continue;
        }

        const int missingCount = missingScans.value(fan.id, 0) + 1;
        missingScans.insert(fan.id, missingCount);
        if (missingCount <= 3) {
            current.append(fan);
        }
    }
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
        if (!plausibleTemperature(temperature)) {
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
    const QList<SensorInfo> previousSensors = m_sensors;
    const QList<FanInfo> previousFans = m_fans;
    m_sensors.clear();
    m_fans.clear();

    if (QSysInfo::currentCpuArchitecture() != QStringLiteral("arm64")) {
        emit hardwareChanged();
        return;
    }

    appendSmcTemperatureSensors(m_sensors);
    appendSmcFans(m_fans, &m_autoFanModes, &m_pendingManualFanSpeeds);

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

    stabilizeSensorReadings(m_sensors, previousSensors, m_missingSensorScans);
    stabilizeFanReadings(m_fans, previousFans, m_missingFanScans);

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
    return m_fans;
}

bool MacHardwareBackend::setFanSpeed(const QString &fanId, double percent)
{
#ifdef Q_OS_MACOS
    const int fanIndex = fanIndexFromId(fanId);
    if (fanIndex < 0) {
        return false;
    }

    const double clampedPercent = std::clamp(percent, 0.0, 100.0);
    if (sendFanSpeedToHelperServer(fanId, clampedPercent)) {
        m_pendingManualFanSpeeds.insert(fanId, clampedPercent);
        scan();
        return true;
    }

    AppleSmcReader smc;
    if (!smc.isOpen()) {
        return false;
    }

    double minRpm = 0.0;
    double maxRpm = 0.0;
    if (!readFanLimits(smc, fanIndex, minRpm, maxRpm)) {
        return false;
    }

    const double targetRpm = minRpm + ((maxRpm - minRpm) * clampedPercent / 100.0);

    const QString modeKey = fanModeKey(smc, fanIndex);
    double currentMode = 0.0;
    if (!modeKey.isEmpty() && smc.readNumber(modeKey, currentMode) && static_cast<int>(std::lround(currentMode)) != 1) {
        m_autoFanModes.insert(fanId, currentMode);
    }

    smc.writeNumber(QStringLiteral("Ftst"), 1.0);
    const bool modeSet = writeManualFanSwitch(smc, fanIndex, true);
    const bool targetSet = smc.writeNumber(smcFanKey(fanIndex, QStringLiteral("Tg")), targetRpm);
    const bool targetSetAfterMode = targetSet || smc.writeNumber(smcFanKey(fanIndex, QStringLiteral("Tg")), targetRpm);

    if (modeSet && targetSetAfterMode) {
        m_pendingManualFanSpeeds.insert(fanId, clampedPercent);
        scan();
        return true;
    }

    qWarning() << "Direct SMC fan write failed; trying privileged helper"
               << "fan" << fanId
               << "percent" << clampedPercent
               << "targetRpm" << targetRpm
               << "modeSet" << modeSet
               << "targetSet" << targetSetAfterMode;

    if (runPrivilegedSetFanSpeed(fanId, clampedPercent)) {
        m_pendingManualFanSpeeds.insert(fanId, clampedPercent);
        scan();
        return true;
    }

    qWarning() << "Failed to set fan speed"
               << "fan" << fanId
               << "percent" << clampedPercent;
#endif

    return false;
}

bool MacHardwareBackend::restoreAutomaticControl(const QString &fanId)
{
#ifdef Q_OS_MACOS
    const int fanIndex = fanIndexFromId(fanId);
    if (fanIndex < 0) {
        return false;
    }

    AppleSmcReader smc;
    if (!smc.isOpen()) {
        return false;
    }

    double autoMode = m_autoFanModes.value(fanId, 0.0);
    const int roundedAutoMode = static_cast<int>(std::lround(autoMode));
    if (roundedAutoMode != 0 && roundedAutoMode != 3) {
        autoMode = 0.0;
    }

    const bool modeSet = writeManualFanSwitch(smc, fanIndex, false);
    if (!modeSet && autoMode != 0.0) {
        writeFanMode(smc, fanIndex, autoMode);
    }
    smc.writeNumber(QStringLiteral("Ftst"), 0.0);

    if (modeSet) {
        m_pendingManualFanSpeeds.remove(fanId);
        scan();
        return true;
    }
#endif

    return false;
}

} // namespace thermvane
