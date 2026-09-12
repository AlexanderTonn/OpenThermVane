#include "hardware/macos/MacHardwareBackend.hpp"

namespace thermvane {

MacHardwareBackend::MacHardwareBackend(QObject *parent)
    : IHardwareBackend(parent)
{
}

void MacHardwareBackend::scan()
{
    emit hardwareChanged();
}

QList<SensorInfo> MacHardwareBackend::sensors() const
{
    return {};
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
