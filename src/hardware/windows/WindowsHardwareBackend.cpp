#include "hardware/windows/WindowsHardwareBackend.hpp"

namespace thermvane {

WindowsHardwareBackend::WindowsHardwareBackend(QObject *parent)
    : IHardwareBackend(parent)
{
}

void WindowsHardwareBackend::scan()
{
    emit hardwareChanged();
}

QList<SensorInfo> WindowsHardwareBackend::sensors() const
{
    return {};
}

QList<FanInfo> WindowsHardwareBackend::fans() const
{
    return {};
}

bool WindowsHardwareBackend::setFanSpeed(const QString &fanId, double percent)
{
    Q_UNUSED(fanId)
    Q_UNUSED(percent)
    return false;
}

bool WindowsHardwareBackend::restoreAutomaticControl(const QString &fanId)
{
    Q_UNUSED(fanId)
    return false;
}

} // namespace thermvane
