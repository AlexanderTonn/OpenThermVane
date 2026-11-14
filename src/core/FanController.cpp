#include "core/FanController.hpp"

#include <QDebug>
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
    m_fanSpeedDecreaseDelayMs = std::clamp(settings.value(QStringLiteral("fanControl/fanSpeedDecreaseDelayMs"),
                                                          m_fanSpeedDecreaseDelayMs).toInt(),
                                           0,
                                           60000);
    qInfo() << "Fan controller initialized; decrease delay ms" << m_fanSpeedDecreaseDelayMs;
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
    qInfo() << "Emergency temperature threshold changed to" << m_emergencyTemperature << "C";
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
    qInfo() << "Minimum automatic fan speed changed to" << m_minimumSpeed << "%";
    emit policyChanged();
}

int FanController::fanSpeedDecreaseDelayMs() const
{
    return m_fanSpeedDecreaseDelayMs;
}

void FanController::setFanSpeedDecreaseDelayMs(int delayMs)
{
    const int normalized = std::clamp(delayMs, 0, 60000);
    if (m_fanSpeedDecreaseDelayMs == normalized) {
        return;
    }

    m_fanSpeedDecreaseDelayMs = normalized;
    QSettings settings;
    settings.setValue(QStringLiteral("fanControl/fanSpeedDecreaseDelayMs"), m_fanSpeedDecreaseDelayMs);
    qInfo() << "Fan speed decrease delay changed to" << m_fanSpeedDecreaseDelayMs << "ms";
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
