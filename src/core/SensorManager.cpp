#include "core/SensorManager.hpp"

#include "hardware/IHardwareBackend.hpp"

#include <QDebug>
#include <QSettings>
#include <algorithm>

namespace thermvane {

SensorManager::SensorManager(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_refreshIntervalMs = settings.value(QStringLiteral("sensors/refreshIntervalMs"), 1000).toInt();
    // Keep the safety/emergency controller responsive even when a stale
    // setting from an older build requested a very long interval.
    m_refreshIntervalMs = std::clamp(m_refreshIntervalMs, 250, 10000);
    qInfo() << "Sensor manager initialized; refresh interval ms" << m_refreshIntervalMs;

    m_refreshTimer.setInterval(m_refreshIntervalMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, &SensorManager::scan);
}

SensorManager::~SensorManager()
{
    m_refreshTimer.stop();
    if (m_activeScanThread) {
        m_activeScanThread->wait();
        m_activeScanThread = nullptr;
    }
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
    qInfo() << "Sensor backend changed" << (m_backend ? "available" : "none");
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
    const int clampedInterval = std::clamp(intervalMs, 250, 10000);
    if (m_refreshIntervalMs == clampedInterval) {
        return;
    }

    m_refreshIntervalMs = clampedInterval;
    m_refreshTimer.setInterval(m_refreshIntervalMs);

    QSettings settings;
    settings.setValue(QStringLiteral("sensors/refreshIntervalMs"), m_refreshIntervalMs);
    settings.sync();
    qInfo() << "Sensor refresh interval changed to" << m_refreshIntervalMs << "ms";

    emit refreshIntervalMsChanged();
}

void SensorManager::scan()
{
    if (!m_backend) {
        return;
    }

    if (m_backend->scanIsAsynchronous()) {
        m_backend->scan();
        return;
    }

    if (m_scanInFlight) {
        return;
    }

    qDebug() << "Scheduling asynchronous hardware scan";
    m_scanInFlight = true;

    QPointer<IHardwareBackend> backend = m_backend;
    QThread *worker = QThread::create([backend] {
        if (backend) {
            backend->scan();
        }
    });
    m_activeScanThread = worker;
    worker->setObjectName(QStringLiteral("ThermVaneHardwareScan"));
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, worker] {
        m_scanInFlight = false;
        if (m_activeScanThread == worker) {
            m_activeScanThread = nullptr;
        }
    }, Qt::QueuedConnection);
    worker->start();
}

void SensorManager::refresh()
{
    m_sensors.clear();

    if (m_backend) {
        const auto sensors = m_backend->sensors();
        for (const auto &sensor : sensors) {
            if (sensor.available) {
                m_sensors.append(sensor);
                if (sensor.temperatureCelsius >= 90.0) {
                    qWarning() << "High temperature sensor reading"
                               << sensor.name << sensor.temperatureCelsius << "C"
                               << "source" << sensor.source;
                }
            }
        }
    }

    qDebug() << "Temperature sensor refresh completed; readable sensors" << m_sensors.size();
    emit sensorsChanged();
}

} // namespace thermvane
