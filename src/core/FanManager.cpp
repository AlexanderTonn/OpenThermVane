#include "core/FanManager.hpp"

#include "hardware/IHardwareBackend.hpp"

namespace thermvane {

FanManager::FanManager(QObject *parent)
    : QObject(parent)
{
}

void FanManager::setBackend(IHardwareBackend *backend)
{
    if (m_backend == backend) {
        return;
    }

    if (m_backend) {
        disconnect(m_backend, nullptr, this, nullptr);
    }

    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &IHardwareBackend::hardwareChanged, this, &FanManager::refresh);
    }

    refresh();
}

QList<FanInfo> FanManager::fans() const
{
    return m_fans;
}

void FanManager::scan()
{
    if (m_backend) {
        m_backend->scan();
    }
}

bool FanManager::setManualSpeed(const QString &fanId, double percent)
{
    return m_backend && m_backend->setFanSpeed(fanId, percent);
}

bool FanManager::restoreAutomaticControl(const QString &fanId)
{
    return m_backend && m_backend->restoreAutomaticControl(fanId);
}

void FanManager::refresh()
{
    m_fans = m_backend ? m_backend->fans() : QList<FanInfo> {};
    emit fansChanged();
}

} // namespace thermvane
