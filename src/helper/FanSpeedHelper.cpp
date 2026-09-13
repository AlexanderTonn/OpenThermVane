#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

namespace {

constexpr uint32_t kSmcSelector = 2;
constexpr uint8_t kSmcCommandReadKeyInfo = 9;
constexpr uint8_t kSmcCommandReadBytes = 5;
constexpr uint8_t kSmcCommandWriteBytes = 6;
constexpr int kServerIdleTimeoutSeconds = 600;

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

uint32_t fourCharCode(const char *text)
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(text[0])) << 24)
        | (static_cast<uint32_t>(static_cast<uint8_t>(text[1])) << 16)
        | (static_cast<uint32_t>(static_cast<uint8_t>(text[2])) << 8)
        | static_cast<uint32_t>(static_cast<uint8_t>(text[3]));
}

uint32_t keyCode(const std::string &key)
{
    if (key.size() != 4) {
        return 0;
    }
    return fourCharCode(key.c_str());
}

std::string fanKey(int fanIndex, const char *suffix)
{
    std::string key = "F0";
    key[1] = static_cast<char>('0' + fanIndex);
    key += suffix;
    return key;
}

std::optional<int> fanIndexFromId(const std::string &fanId)
{
    const std::string prefix = "smc-fan-";
    if (fanId.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    try {
        const int index = std::stoi(fanId.substr(prefix.size()));
        if (index < 0 || index > 7) {
            return std::nullopt;
        }
        return index;
    } catch (...) {
        return std::nullopt;
    }
}

class AppleSmcConnection
{
public:
    AppleSmcConnection()
    {
        open(IOServiceMatching("AppleSMC"));
        if (!isOpen()) {
            open(IOServiceMatching("AppleSMCKeysEndpoint"));
        }
    }

    ~AppleSmcConnection()
    {
        if (m_connection != IO_OBJECT_NULL) {
            IOServiceClose(m_connection);
        }
    }

    bool isOpen() const
    {
        return m_connection != IO_OBJECT_NULL;
    }

    bool readNumber(const std::string &key, double &number) const
    {
        uint32_t type = 0;
        std::array<uint8_t, 32> bytes = {};
        if (!readKey(key, type, bytes)) {
            return false;
        }

        if (type == fourCharCode("flt ")) {
            float value = 0.0F;
            std::memcpy(&value, bytes.data(), sizeof(value));
            number = value;
        } else if (type == fourCharCode("fpe2")) {
            const uint16_t raw = (static_cast<uint16_t>(bytes[0]) << 8) | static_cast<uint16_t>(bytes[1]);
            number = static_cast<double>(raw) / 4.0;
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

    bool writeNumber(const std::string &key, double number) const
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
        } else if (info.dataType == fourCharCode("ui32")) {
            const uint32_t raw = static_cast<uint32_t>(std::llround(std::clamp(number, 0.0, 4294967295.0)));
            bytes[0] = static_cast<uint8_t>((raw >> 24) & 0xff);
            bytes[1] = static_cast<uint8_t>((raw >> 16) & 0xff);
            bytes[2] = static_cast<uint8_t>((raw >> 8) & 0xff);
            bytes[3] = static_cast<uint8_t>(raw & 0xff);
        } else {
            return false;
        }

        return writeKey(key, info, bytes);
    }

private:
    void open(CFMutableDictionaryRef matching)
    {
        if (!matching) {
            return;
        }

        const io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
        if (service == IO_OBJECT_NULL) {
            return;
        }

        for (uint32_t type : {0U, 1U}) {
            io_connect_t connection = IO_OBJECT_NULL;
            if (IOServiceOpen(service, mach_task_self(), type, &connection) == KERN_SUCCESS) {
                m_connection = connection;
                break;
            }
        }
        IOObjectRelease(service);
    }

    bool call(const SmcKeyData &input, SmcKeyData &output) const
    {
        std::memset(&output, 0, sizeof(output));
        size_t outputSize = sizeof(output);
        return IOConnectCallStructMethod(m_connection,
                                         kSmcSelector,
                                         &input,
                                         sizeof(input),
                                         &output,
                                         &outputSize) == KERN_SUCCESS
            && outputSize == sizeof(output);
    }

    bool readKeyInfo(const std::string &key, SmcKeyInfo &info) const
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

    bool readKey(const std::string &key, uint32_t &type, std::array<uint8_t, 32> &bytes) const
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
        return true;
    }

    bool writeKey(const std::string &key, const SmcKeyInfo &info, const std::array<uint8_t, 32> &bytes) const
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

    io_connect_t m_connection = IO_OBJECT_NULL;
};

bool setFanSpeed(const std::string &fanId, double percent)
{
    const auto fanIndex = fanIndexFromId(fanId);
    if (!fanIndex) {
        std::cerr << "Invalid fan id\n";
        return false;
    }

    AppleSmcConnection smc;
    if (!smc.isOpen()) {
        std::cerr << "Could not open Apple SMC\n";
        return false;
    }

    double minRpm = 0.0;
    double maxRpm = 0.0;
    if (!smc.readNumber(fanKey(*fanIndex, "Mn"), minRpm)
        || !smc.readNumber(fanKey(*fanIndex, "Mx"), maxRpm)
        || !std::isfinite(minRpm)
        || !std::isfinite(maxRpm)
        || maxRpm <= minRpm) {
        std::cerr << "Could not read fan limits\n";
        return false;
    }

    const double clampedPercent = std::clamp(percent, 0.0, 100.0);
    const double targetRpm = minRpm + ((maxRpm - minRpm) * clampedPercent / 100.0);

    bool modeSet = false;
    double maskValue = 0.0;
    if (smc.readNumber("FS! ", maskValue)) {
        const uint16_t bit = static_cast<uint16_t>(1U << *fanIndex);
        const uint16_t mask = static_cast<uint16_t>(std::lround(std::clamp(maskValue, 0.0, 65535.0))) | bit;
        modeSet = smc.writeNumber("FS! ", mask);
    }
    if (!modeSet) {
        modeSet = smc.writeNumber(fanKey(*fanIndex, "Md"), 1.0)
            || smc.writeNumber(fanKey(*fanIndex, "md"), 1.0);
    }

    smc.writeNumber("Ftst", 1.0);
    const bool targetSet = smc.writeNumber(fanKey(*fanIndex, "Tg"), targetRpm)
        || smc.writeNumber(fanKey(*fanIndex, "Tg"), targetRpm);

    if (!modeSet || !targetSet) {
        std::cerr << "SMC write failed modeSet=" << modeSet
                  << " targetSet=" << targetSet
                  << " targetRpm=" << targetRpm << '\n';
        return false;
    }

    return true;
}

bool writeAll(int fd, const std::string &message)
{
    const char *data = message.data();
    size_t remaining = message.size();
    while (remaining > 0) {
        const ssize_t written = write(fd, data, remaining);
        if (written <= 0) {
            return false;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}

std::string readLine(int fd)
{
    std::string line;
    char ch = 0;
    while (line.size() < 512) {
        const ssize_t count = read(fd, &ch, 1);
        if (count <= 0 || ch == '\n') {
            break;
        }
        line.push_back(ch);
    }
    return line;
}

void handleClient(int clientFd)
{
    const std::string line = readLine(clientFd);
    std::istringstream stream(line);
    std::string command;
    stream >> command;

    if (command == "PING") {
        writeAll(clientFd, "OK\n");
        return;
    }

    std::string fanId;
    double percent = 0.0;
    stream >> fanId >> percent;

    if (command != "SET" || fanId.empty() || stream.fail()) {
        writeAll(clientFd, "ERR invalid command\n");
        return;
    }

    writeAll(clientFd, setFanSpeed(fanId, percent) ? "OK\n" : "ERR smc write failed\n");
}

int runServer(const std::string &socketPath)
{
    signal(SIGPIPE, SIG_IGN);
    unlink(socketPath.c_str());

    const int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(address.sun_path)) {
        std::cerr << "socket path too long\n";
        close(serverFd);
        return 1;
    }
    std::strncpy(address.sun_path, socketPath.c_str(), sizeof(address.sun_path) - 1);

    if (bind(serverFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << '\n';
        close(serverFd);
        return 1;
    }
    chmod(socketPath.c_str(), 0666);

    if (listen(serverFd, 8) != 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << '\n';
        close(serverFd);
        unlink(socketPath.c_str());
        return 1;
    }

    auto lastActivity = std::chrono::steady_clock::now();
    while (true) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(serverFd, &fds);
        timeval timeout = {1, 0};
        const int ready = select(serverFd + 1, &fds, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(serverFd, &fds)) {
            const int clientFd = accept(serverFd, nullptr, nullptr);
            if (clientFd >= 0) {
                lastActivity = std::chrono::steady_clock::now();
                handleClient(clientFd);
                close(clientFd);
            }
        }

        const auto idleSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - lastActivity);
        if (idleSeconds.count() >= kServerIdleTimeoutSeconds) {
            break;
        }
    }

    close(serverFd);
    unlink(socketPath.c_str());
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc == 3 && std::string(argv[1]) == "--server") {
        return runServer(argv[2]);
    }

    if (argc != 3) {
        std::cerr << "Usage: ThermVaneFanHelper <fan-id> <percent>\n"
                  << "       ThermVaneFanHelper --server <socket-path>\n";
        return 2;
    }

    try {
        return setFanSpeed(argv[1], std::stod(argv[2])) ? 0 : 1;
    } catch (...) {
        std::cerr << "Invalid percent\n";
        return 2;
    }
}
