#pragma once

#include "hardware/HardwareTypes.hpp"

#include <QObject>
#include <QList>

namespace thermvane {

class IHardwareBackend : public QObject
{
    Q_OBJECT

public:
    explicit IHardwareBackend(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IHardwareBackend() override = default;

    virtual void scan() = 0;
    virtual QList<SensorInfo> sensors() const = 0;
    virtual QList<FanInfo> fans() const = 0;
    virtual bool setFanSpeed(const QString &fanId, double percent) = 0;
    virtual bool restoreAutomaticControl(const QString &fanId) = 0;

signals:
    void hardwareChanged();
};

} // namespace thermvane
