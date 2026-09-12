#include "hardware/mock/MockHardwareBackend.hpp"

#include <algorithm>

namespace thermvane {

MockHardwareBackend::MockHardwareBackend(QObject *parent)
    : IHardwareBackend(parent)
{
    scan();
}

void MockHardwareBackend::scan()
{
    m_sensors = {
        {QStringLiteral("cpu-package"), QStringLiteral("CPU Package"), 54.0, QStringLiteral("mock"), true},
        {QStringLiteral("gpu-core"), QStringLiteral("GPU Core"), 61.0, QStringLiteral("mock"), true},
        {QStringLiteral("ssd"), QStringLiteral("SSD"), 42.0, QStringLiteral("mock"), true},
    };

    FanCapabilities controllable;
    controllable.rpmReading = true;
    controllable.manualControl = true;
    controllable.firmwareControl = true;
    controllable.pwmControl = true;

    FanCapabilities readOnly;
    readOnly.rpmReading = true;
    readOnly.firmwareControl = true;

    m_fans = {
        {QStringLiteral("cpu-fan"), QStringLiteral("CPU Fan"), 1120, 42.0, true, controllable},
        {QStringLiteral("case-fan"), QStringLiteral("Case Fan"), 840, 31.0, true, controllable},
        {QStringLiteral("laptop-fan"), QStringLiteral("Laptop Fan"), 1680, 64.0, true, readOnly},
    };

    emit hardwareChanged();
}

QList<SensorInfo> MockHardwareBackend::sensors() const
{
    return m_sensors;
}

QList<FanInfo> MockHardwareBackend::fans() const
{
    return m_fans;
}

bool MockHardwareBackend::setFanSpeed(const QString &fanId, double percent)
{
    const auto it = std::find_if(m_fans.begin(), m_fans.end(), [&](const FanInfo &fan) {
        return fan.id == fanId;
    });
    if (it == m_fans.end() || !it->capabilities.manualControl) {
        return false;
    }

    it->speedPercent = std::clamp(percent, 0.0, 100.0);
    it->automatic = false;
    it->rpm = static_cast<int>(it->speedPercent * 28.0);
    emit hardwareChanged();
    return true;
}

bool MockHardwareBackend::restoreAutomaticControl(const QString &fanId)
{
    const auto it = std::find_if(m_fans.begin(), m_fans.end(), [&](const FanInfo &fan) {
        return fan.id == fanId;
    });
    if (it == m_fans.end() || !it->capabilities.firmwareControl) {
        return false;
    }

    it->automatic = true;
    emit hardwareChanged();
    return true;
}

} // namespace thermvane
