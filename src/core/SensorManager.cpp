#include "core/SensorManager.hpp"

#include "hardware/IHardwareBackend.hpp"

namespace thermvane {

SensorManager::SensorManager(QObject *parent)
    : QObject(parent)
{
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
    }

    refresh();
}

QList<SensorInfo> SensorManager::sensors() const
{
    return m_sensors;
}

void SensorManager::scan()
{
    if (m_backend) {
        m_backend->scan();
    }
}

void SensorManager::refresh()
{
    m_sensors = m_backend ? m_backend->sensors() : QList<SensorInfo> {};
    emit sensorsChanged();
}

} // namespace thermvane
