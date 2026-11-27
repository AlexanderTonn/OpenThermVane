#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kSmcSelector = 2;
constexpr uint8_t kSmcCommandReadKeyInfo = 9;
constexpr uint8_t kSmcCommandReadBytes = 5;
constexpr uint8_t kSmcCommandWriteBytes = 6;
constexpr const char *kAuthorizedUidFile = "/Library/Application Support/OpenThermVane/fan-helper.uid";
constexpr const char *kSocketPath = "/tmp/thermvane-fan-helper.sock";
constexpr auto kClientLeaseTimeout = std::chrono::seconds(15);

std::mutex g_controlledFansMutex;
std::set<std::string> g_controlledFans;
std::chrono::steady_clock::time_point g_lastClientActivity = std::chrono::steady_clock::now();
std::atomic_bool g_running = true;
std::atomic_bool g_commandInFlight = false;
std::recursive_mutex g_smcOperationMutex;
std::mutex g_powerRunLoopMutex;
CFRunLoopRef g_powerRunLoop = nullptr;

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

bool writeFanModeWithUnlock(const AppleSmcConnection &smc, int fanIndex, double mode)
{
    const auto writeMode = [&]() {
        return smc.writeNumber(fanKey(fanIndex, "Md"), mode)
            || smc.writeNumber(fanKey(fanIndex, "md"), mode);
    };

    if (writeMode()) {
        return true;
    }

    // On M3/M4 Macs thermalmonitord can hold the fan in system mode (3) and
    // reject F0Md/F0md writes. Ftst=1 makes it yield; the mode write only
    // succeeds after the firmware transitions to an idle/auto state.
    if (mode != 1.0 || !smc.writeNumber("Ftst", 1.0)) {
        return false;
    }

    constexpr int kUnlockAttempts = 60; // up to six seconds, 100 ms each
    for (int attempt = 0; attempt < kUnlockAttempts && g_running.load(); ++attempt) {
        if (writeMode()) {
            return true;
        }
        usleep(100000);
    }

    return false;
}

bool readFanMode(const AppleSmcConnection &smc, int fanIndex, double &mode)
{
    return smc.readNumber(fanKey(fanIndex, "Md"), mode)
        || smc.readNumber(fanKey(fanIndex, "md"), mode);
}

bool restoreFanControl(const std::string &fanId);
void markFanControlled(const std::string &fanId);
void markFanRestored(const std::string &fanId);

void touchClientLease()
{
    std::scoped_lock lock(g_controlledFansMutex);
    g_lastClientActivity = std::chrono::steady_clock::now();
}

bool clientLeaseExpired()
{
    std::scoped_lock lock(g_controlledFansMutex);
    return !g_controlledFans.empty()
        && std::chrono::steady_clock::now() - g_lastClientActivity > kClientLeaseTimeout;
}

bool setFanSpeed(const std::string &fanId, double percent)
{
    std::lock_guard<std::recursive_mutex> operationLock(g_smcOperationMutex);
    if (!std::isfinite(percent) || percent < 0.0 || percent > 100.0) {
        std::cerr << "Invalid fan percent\n";
        return false;
    }

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
        || minRpm < 0.0
        || maxRpm > 20000.0
        || maxRpm <= minRpm) {
        std::cerr << "Could not read fan limits\n";
        return false;
    }

    const double clampedPercent = std::clamp(percent, 0.0, 100.0);
    // On Apple Silicon, F*Mn is often the lowest *running* speed (around
    // 2000 RPM), not the stop value. A curve point at 0 % must therefore be
    // sent as an explicit 0 RPM target instead of being mapped to F*Mn.
    const double targetRpm = clampedPercent <= 0.0
        ? 0.0
        : minRpm + ((maxRpm - minRpm) * clampedPercent / 100.0);

    // Do not rewrite an already active manual mode for every curve sample.
    // thermalmonitord may reject a redundant F*Md write even though F*Tg is
    // writable; avoiding it makes successive Auto updates reliable.
    double currentMode = 0.0;
    const bool modeReadable = readFanMode(smc, *fanIndex, currentMode);
    bool modeSet = modeReadable && static_cast<int>(std::lround(currentMode)) == 1;
    if (!modeSet) {
        double maskValue = 0.0;
        if (smc.readNumber("FS! ", maskValue)
            && std::isfinite(maskValue)
            && maskValue >= 0.0
            && maskValue <= 65535.0) {
            const uint16_t mask = static_cast<uint16_t>(std::lround(maskValue));
            modeSet = (mask & static_cast<uint16_t>(1U << *fanIndex)) != 0;
        }
    }
    if (!modeSet) {
        modeSet = writeFanModeWithUnlock(smc, *fanIndex, 1.0);
    }

    double maskValue = 0.0;
    if (!modeSet && smc.readNumber("FS! ", maskValue)) {
        const uint16_t bit = static_cast<uint16_t>(1U << *fanIndex);
        const uint16_t mask = static_cast<uint16_t>(std::lround(std::clamp(maskValue, 0.0, 65535.0))) | bit;
        modeSet = smc.writeNumber("FS! ", mask);
    }

    if (!modeSet) {
        smc.writeNumber("Ftst", 0.0);
        std::cerr << "Could not unlock fan control mode\n";
        return false;
    }

    // Track the fan before the target write. If the target write fails or the
    // helper is terminated, shutdown cleanup must still restore this fan.
    markFanControlled(fanId);

    const bool targetSet = smc.writeNumber(fanKey(*fanIndex, "Tg"), targetRpm)
        || smc.writeNumber(fanKey(*fanIndex, "Tg"), targetRpm);

    if (!modeSet || !targetSet) {
        if (modeSet && !restoreFanControl(fanId)) {
            std::cerr << "SMC write failed and automatic restore also failed\n";
        }
        std::cerr << "SMC write failed modeSet=" << modeSet
                  << " targetSet=" << targetSet
                  << " targetRpm=" << targetRpm << '\n';
        return false;
    }

    return true;
}

bool restoreFanControl(const std::string &fanId)
{
    std::lock_guard<std::recursive_mutex> operationLock(g_smcOperationMutex);
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

    double currentMode = 0.0;
    const bool modeReadable = readFanMode(smc, *fanIndex, currentMode);
    if (modeReadable) {
        const int mode = static_cast<int>(std::lround(currentMode));
        if (mode == 0 || mode == 3) {
            // Already under firmware/system control. Clearing Ftst is enough
            // and avoids reporting a false restore failure on startup.
            smc.writeNumber("Ftst", 0.0);
            return true;
        }
    }

    bool modeSet = writeFanModeWithUnlock(smc, *fanIndex, 0.0);

    double maskValue = 0.0;
    if (!modeSet && smc.readNumber("FS! ", maskValue)) {
        const uint16_t bit = static_cast<uint16_t>(1U << *fanIndex);
        const uint16_t mask = static_cast<uint16_t>(std::lround(std::clamp(maskValue, 0.0, 65535.0)))
            & static_cast<uint16_t>(~bit);
        modeSet = smc.writeNumber("FS! ", mask);
    }

    smc.writeNumber("Ftst", 0.0);
    return modeSet;
}

void markFanControlled(const std::string &fanId)
{
    std::scoped_lock lock(g_controlledFansMutex);
    g_controlledFans.insert(fanId);
}

void markFanRestored(const std::string &fanId)
{
    std::scoped_lock lock(g_controlledFansMutex);
    g_controlledFans.erase(fanId);
}

std::vector<std::string> controlledFanIds()
{
    std::scoped_lock lock(g_controlledFansMutex);
    return {g_controlledFans.begin(), g_controlledFans.end()};
}

void restoreAllControlledFans(const char *reason)
{
    const std::vector<std::string> fanIds = controlledFanIds();
    if (fanIds.empty()) {
        return;
    }

    std::cerr << "Restoring " << fanIds.size() << " fan(s) to automatic control before " << reason << '\n';
    for (const std::string &fanId : fanIds) {
        if (restoreFanControl(fanId)) {
            markFanRestored(fanId);
        }
    }
}

void powerCallback(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
    auto *connection = static_cast<io_connect_t *>(refCon);
    switch (messageType) {
    case kIOMessageSystemWillSleep:
        restoreAllControlledFans("system sleep");
        if (connection && *connection != IO_OBJECT_NULL) {
            IOAllowPowerChange(*connection, reinterpret_cast<long>(messageArgument));
        }
        break;
    default:
        (void)service;
        break;
    }
}

void runPowerNotificationLoop()
{
    IONotificationPortRef notificationPort = nullptr;
    io_object_t notifier = IO_OBJECT_NULL;
    io_connect_t connection = IORegisterForSystemPower(nullptr, &notificationPort, powerCallback, &notifier);
    if (connection == IO_OBJECT_NULL || !notificationPort) {
        std::cerr << "Could not register power notifications\n";
        return;
    }

    CFRunLoopSourceRef source = IONotificationPortGetRunLoopSource(notificationPort);
    if (source) {
        CFRunLoopRef runLoop = CFRunLoopGetCurrent();
        {
            std::scoped_lock lock(g_powerRunLoopMutex);
            g_powerRunLoop = runLoop;
        }
        CFRetain(runLoop);
        CFRunLoopAddSource(runLoop, source, kCFRunLoopDefaultMode);

        while (g_running.load()) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
        }

        CFRunLoopRemoveSource(runLoop, source, kCFRunLoopDefaultMode);
        {
            std::scoped_lock lock(g_powerRunLoopMutex);
            g_powerRunLoop = nullptr;
        }
        CFRelease(runLoop);
    }

    if (notifier != IO_OBJECT_NULL) {
        IODeregisterForSystemPower(&notifier);
    }
    IONotificationPortDestroy(notificationPort);
    IOServiceClose(connection);
}

void stopPowerNotificationLoop()
{
    g_running.store(false);
    std::scoped_lock lock(g_powerRunLoopMutex);
    if (g_powerRunLoop) {
        CFRunLoopStop(g_powerRunLoop);
    }
}

void handleTerminationSignal(int)
{
    g_running.store(false);
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

bool authorizedPeer(int clientFd, uid_t allowedUid)
{
    uid_t peerUid = 0;
    gid_t peerGid = 0;
    return getpeereid(clientFd, &peerUid, &peerGid) == 0 && peerUid == allowedUid;
}

void handleClient(int clientFd, uid_t allowedUid)
{
    timeval timeout = {2, 0};
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (!authorizedPeer(clientFd, allowedUid)) {
        writeAll(clientFd, "ERR unauthorized\n");
        return;
    }

    touchClientLease();

    const std::string line = readLine(clientFd);
    std::istringstream stream(line);
    std::string command;
    stream >> command;

    if (command == "PING") {
        writeAll(clientFd, "OK\n");
        return;
    }

    if (command == "RESTORE") {
        std::string fanId;
        stream >> fanId;
        if (fanId.empty() || stream.fail()) {
            writeAll(clientFd, "ERR invalid command\n");
            return;
        }

        std::string trailing;
        const bool hasTrailingData = static_cast<bool>(stream >> trailing);
        if (hasTrailingData) {
            writeAll(clientFd, "ERR invalid command\n");
            return;
        }
        if (restoreFanControl(fanId)) {
            markFanRestored(fanId);
            writeAll(clientFd, "OK\n");
        } else {
            writeAll(clientFd, "ERR smc restore failed\n");
        }
        return;
    }

    std::string fanId;
    double percent = 0.0;
    stream >> fanId >> percent;
    if (command != "SET" || fanId.empty() || stream.fail()
        || !std::isfinite(percent) || percent < 0.0 || percent > 100.0) {
        writeAll(clientFd, "ERR invalid command\n");
        return;
    }

    std::string trailing;
    const bool hasTrailingData = static_cast<bool>(stream >> trailing);

    if (hasTrailingData) {
        writeAll(clientFd, "ERR invalid command\n");
        return;
    }

    if (setFanSpeed(fanId, percent)) {
        markFanControlled(fanId);
        writeAll(clientFd, "OK\n");
    } else {
        writeAll(clientFd, "ERR smc write failed\n");
    }
}

int runServer(const std::string &socketPath, uid_t allowedUid)
{
    if (socketPath != kSocketPath) {
        std::cerr << "Refusing non-canonical helper socket path\n";
        return 2;
    }

    g_running.store(true);
    touchClientLease();
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, handleTerminationSignal);
    signal(SIGINT, handleTerminationSignal);
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
    if (chown(socketPath.c_str(), allowedUid, static_cast<gid_t>(-1)) != 0
        || chmod(socketPath.c_str(), 0600) != 0) {
        std::cerr << "socket permission setup failed: " << std::strerror(errno) << '\n';
        close(serverFd);
        unlink(socketPath.c_str());
        return 1;
    }

    if (listen(serverFd, 8) != 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << '\n';
        close(serverFd);
        unlink(socketPath.c_str());
        return 1;
    }

    std::thread powerThread(runPowerNotificationLoop);
    while (g_running.load()) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(serverFd, &fds);
        timeval timeout = {1, 0};
        const int ready = select(serverFd + 1, &fds, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(serverFd, &fds)) {
            const int clientFd = accept(serverFd, nullptr, nullptr);
            if (clientFd >= 0) {
                if (g_commandInFlight.exchange(true)) {
                    writeAll(clientFd, "ERR busy\n");
                    close(clientFd);
                } else {
                    try {
                        std::thread([clientFd, allowedUid] {
                            try {
                                handleClient(clientFd, allowedUid);
                            } catch (...) {
                                writeAll(clientFd, "ERR internal error\n");
                            }
                            close(clientFd);
                            g_commandInFlight.store(false);
                        }).detach();
                    } catch (...) {
                        g_commandInFlight.store(false);
                        writeAll(clientFd, "ERR busy\n");
                        close(clientFd);
                    }
                }
            }
        }

        if (clientLeaseExpired()) {
            restoreAllControlledFans("client lease timeout");
            touchClientLease();
        }
    }

    // Let the serialized client operation finish before final restoration.
    for (int waitMs = 0; g_commandInFlight.load() && waitMs < 10000; waitMs += 10) {
        usleep(10000);
    }
    restoreAllControlledFans("helper shutdown");
    stopPowerNotificationLoop();
    if (powerThread.joinable()) {
        powerThread.join();
    }

    close(serverFd);
    unlink(socketPath.c_str());
    return 0;
}

std::optional<uid_t> configuredAuthorizedUid()
{
    std::ifstream authorizedUidFile(kAuthorizedUidFile);
    unsigned long value = 0;
    if (!(authorizedUidFile >> value)
        || value > static_cast<unsigned long>(std::numeric_limits<uid_t>::max())
        || value == 0) {
        return std::nullopt;
    }
    return static_cast<uid_t>(value);
}

bool authorizedDirectCaller()
{
    if (getuid() == 0) {
        return true;
    }
    const auto allowedUid = configuredAuthorizedUid();
    return allowedUid.has_value() && allowedUid.value() == getuid();
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc == 3 && std::string(argv[1]) == "--server") {
        // The server is launched by launchd as root. Do not allow the
        // setuid command-line interface to spawn an additional root server
        // or replace the canonical socket from an unprivileged process.
        if (getuid() != 0) {
            std::cerr << "Unauthorized caller\n";
            return 1;
        }
        const auto allowedUid = configuredAuthorizedUid();
        if (!allowedUid.has_value()) {
            std::cerr << "Authorized UID is not configured\n";
            return 2;
        }
        return runServer(argv[2], allowedUid.value());
    }

    if (argc == 5 && std::string(argv[1]) == "--server" && std::string(argv[3]) == "--uid") {
        if (getuid() != 0) {
            std::cerr << "Unauthorized caller\n";
            return 1;
        }
        try {
            const unsigned long parsedUid = std::stoul(argv[4]);
            if (parsedUid > static_cast<unsigned long>(std::numeric_limits<uid_t>::max())) {
                return 2;
            }
            const auto configuredUid = configuredAuthorizedUid();
            if (!configuredUid.has_value() || parsedUid != configuredUid.value()) {
                std::cerr << "Refusing unauthorized helper UID\n";
                return 1;
            }
            return runServer(argv[2], static_cast<uid_t>(parsedUid));
        } catch (...) {
            return 2;
        }
    }

    if (argc == 3 && std::string(argv[1]) == "--restore") {
        if (!authorizedDirectCaller()) {
            std::cerr << "Unauthorized caller\n";
            return 1;
        }
        return restoreFanControl(argv[2]) ? 0 : 1;
    }

    if (argc != 3) {
        std::cerr << "Usage: ThermVaneFanHelper <fan-id> <percent>\n"
                  << "       ThermVaneFanHelper --restore <fan-id>\n"
                  << "       ThermVaneFanHelper --server <socket-path>\n";
        return 2;
    }

    try {
        if (!authorizedDirectCaller()) {
            std::cerr << "Unauthorized caller\n";
            return 1;
        }
        return setFanSpeed(argv[1], std::stod(argv[2])) ? 0 : 1;
    } catch (...) {
        std::cerr << "Invalid percent\n";
        return 2;
    }
}
