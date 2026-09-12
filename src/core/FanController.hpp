#pragma once

#include "core/FanCurve.hpp"

#include <QObject>

namespace thermvane {

class FanController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double emergencyTemperature READ emergencyTemperature WRITE setEmergencyTemperature NOTIFY policyChanged)
    Q_PROPERTY(double minimumSpeed READ minimumSpeed WRITE setMinimumSpeed NOTIFY policyChanged)

public:
    explicit FanController(QObject *parent = nullptr);

    double emergencyTemperature() const;
    void setEmergencyTemperature(double temperature);

    double minimumSpeed() const;
    void setMinimumSpeed(double speed);

    Q_INVOKABLE double automaticSpeed(double temperature) const;

signals:
    void policyChanged();

private:
    FanCurve m_curve;
    double m_emergencyTemperature = 95.0;
    double m_minimumSpeed = 20.0;
};

} // namespace thermvane
