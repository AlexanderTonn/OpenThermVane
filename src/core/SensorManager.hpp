#pragma once

#include "hardware/HardwareTypes.hpp"

#include <QObject>
#include <QList>
#include <QPointer>

namespace thermvane {

class IHardwareBackend;

class SensorManager final : public QObject
{
    Q_OBJECT

public:
    explicit SensorManager(QObject *parent = nullptr);

    void setBackend(IHardwareBackend *backend);
    QList<SensorInfo> sensors() const;

    Q_INVOKABLE void scan();

signals:
    void sensorsChanged();

private:
    void refresh();

    QPointer<IHardwareBackend> m_backend;
    QList<SensorInfo> m_sensors;
};

} // namespace thermvane
