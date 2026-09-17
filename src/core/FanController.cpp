#include "core/FanController.hpp"

#include <QSettings>
#include <algorithm>

namespace thermvane {

FanController::FanController(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_emergencyTemperature = std::clamp(settings.value(QStringLiteral("safety/emergencyTemperatureC"), m_emergencyTemperature).toDouble(),
                                        70.0,
                                        110.0);
    m_minimumSpeed = std::clamp(settings.value(QStringLiteral("safety/minimumSpeedPercent"), m_minimumSpeed).toDouble(),
                                0.0,
                                100.0);
}

double FanController::emergencyTemperature() const
{
    return m_emergencyTemperature;
}

void FanController::setEmergencyTemperature(double temperature)
{
    const double normalized = std::clamp(temperature, 70.0, 110.0);
    if (qFuzzyCompare(m_emergencyTemperature, normalized)) {
        return;
    }
    m_emergencyTemperature = normalized;
    QSettings settings;
    settings.setValue(QStringLiteral("safety/emergencyTemperatureC"), m_emergencyTemperature);
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
    QSettings settings;
    settings.setValue(QStringLiteral("safety/minimumSpeedPercent"), m_minimumSpeed);
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
