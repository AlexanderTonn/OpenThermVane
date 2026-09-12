#pragma once

#include "hardware/IHardwareBackend.hpp"

namespace thermvane {

class MockHardwareBackend final : public IHardwareBackend
{
    Q_OBJECT

public:
    explicit MockHardwareBackend(QObject *parent = nullptr);

    void scan() override;
    QList<SensorInfo> sensors() const override;
    QList<FanInfo> fans() const override;
    bool setFanSpeed(const QString &fanId, double percent) override;
    bool restoreAutomaticControl(const QString &fanId) override;

private:
    QList<SensorInfo> m_sensors;
    QList<FanInfo> m_fans;
};

} // namespace thermvane
