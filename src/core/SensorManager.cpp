#include "core/SensorManager.hpp"

#include "hardware/IHardwareBackend.hpp"

#include <QSettings>
#include <algorithm>

namespace thermvane {

SensorManager::SensorManager(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_refreshIntervalMs = settings.value(QStringLiteral("sensors/refreshIntervalMs"), 1000).toInt();
    m_refreshIntervalMs = std::clamp(m_refreshIntervalMs, 250, 60000);

    m_refreshTimer.setInterval(m_refreshIntervalMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, &SensorManager::scan);
}

void SensorManager::setBackend(IHardwareBackend *backend)
{
    if (m_backend == backend) {
        return;
    }

    if (m_backend) {
        disconnect(m_backend, nullptr, this, nullptr);
    }

    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &IHardwareBackend::hardwareChanged, this, &SensorManager::refresh);
        m_refreshTimer.start();
    } else {
        m_refreshTimer.stop();
    }

    refresh();
}

QList<SensorInfo> SensorManager::sensors() const
{
    return m_sensors;
}

int SensorManager::refreshIntervalMs() const
{
    return m_refreshIntervalMs;
}

void SensorManager::setRefreshIntervalMs(int intervalMs)
{
    const int clampedInterval = std::clamp(intervalMs, 250, 60000);
    if (m_refreshIntervalMs == clampedInterval) {
        return;
    }

    m_refreshIntervalMs = clampedInterval;
    m_refreshTimer.setInterval(m_refreshIntervalMs);

    QSettings settings;
    settings.setValue(QStringLiteral("sensors/refreshIntervalMs"), m_refreshIntervalMs);

    emit refreshIntervalMsChanged();
}

void SensorManager::scan()
{
    if (m_backend) {
        m_backend->scan();
    }
}

void SensorManager::refresh()
{
    m_sensors.clear();

    if (m_backend) {
        const auto sensors = m_backend->sensors();
        for (const auto &sensor : sensors) {
            if (sensor.available) {
                m_sensors.append(sensor);
            }
        }
    }

    emit sensorsChanged();
}

} // namespace thermvane
