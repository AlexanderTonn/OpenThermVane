#pragma once

#include "hardware/HardwareTypes.hpp"

#include <QObject>
#include <QList>
#include <QPointer>
#include <QTimer>

namespace thermvane {

class IHardwareBackend;

class SensorManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int refreshIntervalMs READ refreshIntervalMs WRITE setRefreshIntervalMs NOTIFY refreshIntervalMsChanged)

public:
    explicit SensorManager(QObject *parent = nullptr);

    void setBackend(IHardwareBackend *backend);
    QList<SensorInfo> sensors() const;
    int refreshIntervalMs() const;
    void setRefreshIntervalMs(int intervalMs);

    void scan();

signals:
    void sensorsChanged();
    void refreshIntervalMsChanged();

private:
    void refresh();

    QPointer<IHardwareBackend> m_backend;
    QList<SensorInfo> m_sensors;
    QTimer m_refreshTimer;
    int m_refreshIntervalMs = 1000;
};

} // namespace thermvane
