#pragma once

#include "hardware/IHardwareBackend.hpp"

#include <QHash>
#include <QRecursiveMutex>
#include <QSet>
#include <QTimer>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#endif

namespace thermvane {

class MacHardwareBackend final : public IHardwareBackend
{
    Q_OBJECT

public:
    explicit MacHardwareBackend(QObject *parent = nullptr);
    ~MacHardwareBackend() override;

    void scan() override;
    QList<SensorInfo> sensors() const override;
    QList<FanInfo> fans() const override;
    bool setFanSpeed(const QString &fanId, double percent) override;
    bool restoreAutomaticControl(const QString &fanId) override;

private:
#if defined(Q_OS_MACOS)
    static void powerCallback(void *refCon, io_service_t service, natural_t messageType, void *messageArgument);
    void handlePowerMessage(io_service_t service, natural_t messageType, void *messageArgument);
    void restorePendingManualFans();
    void scheduleWakeRecovery();
    void dispatchHelperFanWrite(const QString &fanId);
    void completeHelperFanWrite(const QString &fanId, double requestedPercent, bool success);
    void dispatchHelperFanRestore(const QString &fanId);
    void completeHelperFanRestore(const QString &fanId, bool success);
    void dispatchNextHelperOperation();
    void dispatchHelperLeasePing();
#endif

    mutable QRecursiveMutex m_mutex;
    QList<SensorInfo> m_sensors;
    QList<FanInfo> m_fans;
    QHash<QString, double> m_autoFanModes;
    QHash<QString, int> m_missingSensorScans;
    QHash<QString, int> m_missingFanScans;
    QHash<QString, double> m_pendingManualFanSpeeds;
#if defined(Q_OS_MACOS)
    QTimer m_helperLeaseTimer;
    QTimer m_helperWriteRetryTimer;
    QTimer m_wakeRecoveryTimer;
    QHash<QString, double> m_pendingHelperFanWrites;
    QSet<QString> m_helperFanWritesInFlight;
    QSet<QString> m_pendingHelperFanRestores;
    bool m_helperLeasePingInFlight = false;
    int m_wakeRecoveryPass = 0;
    io_connect_t m_powerConnection = IO_OBJECT_NULL;
    io_object_t m_powerNotifier = IO_OBJECT_NULL;
    IONotificationPortRef m_powerNotificationPort = nullptr;
    CFRunLoopSourceRef m_powerRunLoopSource = nullptr;
#endif
};

} // namespace thermvane
