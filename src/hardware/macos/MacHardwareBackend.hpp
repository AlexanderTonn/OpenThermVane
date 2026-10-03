#pragma once

#include "hardware/IHardwareBackend.hpp"

#include <QHash>

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
#endif

    QList<SensorInfo> m_sensors;
    QList<FanInfo> m_fans;
    QHash<QString, double> m_autoFanModes;
    QHash<QString, int> m_missingSensorScans;
    QHash<QString, int> m_missingFanScans;
    QHash<QString, double> m_pendingManualFanSpeeds;
#if defined(Q_OS_MACOS)
    io_connect_t m_powerConnection = IO_OBJECT_NULL;
    io_object_t m_powerNotifier = IO_OBJECT_NULL;
    IONotificationPortRef m_powerNotificationPort = nullptr;
    CFRunLoopSourceRef m_powerRunLoopSource = nullptr;
#endif
};

} // namespace thermvane
