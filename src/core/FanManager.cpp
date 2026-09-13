#include "core/FanManager.hpp"

#include "hardware/IHardwareBackend.hpp"

#include <algorithm>

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
    m_manualSpeedOverrides.clear();
    if (m_backend) {
        connect(m_backend, &IHardwareBackend::hardwareChanged, this, &FanManager::refresh);
    }

    refresh();
}

QList<FanInfo> FanManager::fans() const
{
    return m_fans;
}


bool FanManager::setManualSpeed(const QString &fanId, double percent)
{
    if (!m_backend || !m_backend->setFanSpeed(fanId, percent)) {
        return false;
    }

    m_manualSpeedOverrides.insert(fanId, std::clamp(percent, 0.0, 100.0));
    applyManualOverrides();
    emit fansChanged();
    return true;
}

bool FanManager::restoreAutomaticControl(const QString &fanId)
{
    if (!m_backend || !m_backend->restoreAutomaticControl(fanId)) {
        return false;
    }

    m_manualSpeedOverrides.remove(fanId);
    refresh();
    return true;
}

void FanManager::refresh()
{
    m_fans = m_backend ? m_backend->fans() : QList<FanInfo> {};
    applyManualOverrides();
    emit fansChanged();
}

void FanManager::applyManualOverrides()
{
    for (FanInfo &fan : m_fans) {
        const auto overrideIt = m_manualSpeedOverrides.constFind(fan.id);
        if (overrideIt == m_manualSpeedOverrides.cend()) {
            continue;
        }

        fan.speedPercent = overrideIt.value();
        fan.automatic = false;
    }
}

} // namespace thermvane
