#include "hardware/linux/LinuxHardwareBackend.hpp"

namespace thermvane {

LinuxHardwareBackend::LinuxHardwareBackend(QObject *parent)
    : IHardwareBackend(parent)
{
}

void LinuxHardwareBackend::scan()
{
    emit hardwareChanged();
}

QList<SensorInfo> LinuxHardwareBackend::sensors() const
{
    return {};
}

QList<FanInfo> LinuxHardwareBackend::fans() const
{
    return {};
}

bool LinuxHardwareBackend::setFanSpeed(const QString &fanId, double percent)
{
    Q_UNUSED(fanId)
    Q_UNUSED(percent)
    return false;
}

bool LinuxHardwareBackend::restoreAutomaticControl(const QString &fanId)
{
    Q_UNUSED(fanId)
    return false;
}

} // namespace thermvane
