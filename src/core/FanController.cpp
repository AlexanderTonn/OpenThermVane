#include "core/FanController.hpp"

#include <algorithm>

namespace thermvane {

FanController::FanController(QObject *parent)
    : QObject(parent)
{
}

double FanController::emergencyTemperature() const
{
    return m_emergencyTemperature;
}

void FanController::setEmergencyTemperature(double temperature)
{
    if (qFuzzyCompare(m_emergencyTemperature, temperature)) {
        return;
    }
    m_emergencyTemperature = temperature;
    emit policyChanged();
}

double FanController::minimumSpeed() const
{
    return m_minimumSpeed;
}

void FanController::setMinimumSpeed(double speed)
{
    const double normalized = std::clamp(speed, 0.0, 100.0);
    if (qFuzzyCompare(m_minimumSpeed, normalized)) {
        return;
    }
    m_minimumSpeed = normalized;
    emit policyChanged();
}

double FanController::automaticSpeed(double temperature) const
{
    if (temperature >= m_emergencyTemperature) {
        return 100.0;
    }

    return std::clamp(m_curve.speedForTemperature(temperature), m_minimumSpeed, 100.0);
}

} // namespace thermvane
