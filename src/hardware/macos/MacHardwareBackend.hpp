#pragma once

#include "hardware/IHardwareBackend.hpp"

namespace thermvane {

class MacHardwareBackend final : public IHardwareBackend
{
    Q_OBJECT

public:
    explicit MacHardwareBackend(QObject *parent = nullptr);

    void scan() override;
    QList<SensorInfo> sensors() const override;
    QList<FanInfo> fans() const override;
    bool setFanSpeed(const QString &fanId, double percent) override;
    bool restoreAutomaticControl(const QString &fanId) override;

private:
    QList<SensorInfo> m_sensors;
};

} // namespace thermvane
