#include "core/FanManager.hpp"

#include "hardware/IHardwareBackend.hpp"

#include <algorithm>
#include <utility>

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
    if (!m_backend) {
        return false;
    }

    const double normalized = std::clamp(percent, 0.0, 100.0);
    if (!m_backend->setFanSpeed(fanId, m_emergencyActive ? 100.0 : normalized)) {
        return false;
    }

    m_manualSpeedOverrides.insert(fanId, normalized);
    if (m_emergencyActive) {
        m_emergencyControlledFans.insert(fanId);
    }
    applyManualOverrides();
    emit fansChanged();
    return true;
}

bool FanManager::restoreAutomaticControl(const QString &fanId)
{
    if (!m_backend) {
        return false;
    }

    if (m_emergencyActive) {
        if (!m_backend->setFanSpeed(fanId, 100.0)) {
            return false;
        }
        m_manualSpeedOverrides.remove(fanId);
        m_emergencyControlledFans.insert(fanId);
        applyManualOverrides();
        emit fansChanged();
        return true;
    }

    if (!m_backend->restoreAutomaticControl(fanId)) {
        return false;
    }
    m_manualSpeedOverrides.remove(fanId);
    refresh();
    return true;
}

bool FanManager::setEmergencyFullSpeed(bool active)
{
    if (!m_backend) {
        return false;
    }

    if (active) {
        m_emergencyActive = true;
        const bool ok = applyEmergencySpeed();
        applyManualOverrides();
        emit fansChanged();
        return ok;
    }

    if (!m_emergencyActive && m_emergencyControlledFans.isEmpty()) {
        return true;
    }

    const bool ok = restoreEmergencyControlledFans();
    m_emergencyActive = false;
    m_emergencyControlledFans.clear();
    refresh();
    return ok;
}

void FanManager::refresh()
{
    m_fans = m_backend ? m_backend->fans() : QList<FanInfo> {};
    if (m_emergencyActive) {
        applyEmergencySpeed();
    }
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

    if (!m_emergencyActive) {
        return;
    }

    for (FanInfo &fan : m_fans) {
        if (!fan.capabilities.manualControl) {
            continue;
        }

        fan.speedPercent = 100.0;
        fan.automatic = false;
    }
}

bool FanManager::applyEmergencySpeed()
{
    if (!m_backend) {
        return false;
    }

    bool allOk = true;
    for (const FanInfo &fan : std::as_const(m_fans)) {
        if (!fan.capabilities.manualControl) {
            continue;
        }

        if (m_backend->setFanSpeed(fan.id, 100.0)) {
            m_emergencyControlledFans.insert(fan.id);
        } else {
            allOk = false;
        }
    }
    return allOk;
}

bool FanManager::restoreEmergencyControlledFans()
{
    if (!m_backend) {
        return false;
    }

    bool allOk = true;
    for (const QString &fanId : std::as_const(m_emergencyControlledFans)) {
        const auto overrideIt = m_manualSpeedOverrides.constFind(fanId);
        if (overrideIt != m_manualSpeedOverrides.cend()) {
            allOk = m_backend->setFanSpeed(fanId, overrideIt.value()) && allOk;
        } else {
            allOk = m_backend->restoreAutomaticControl(fanId) && allOk;
        }
    }
    return allOk;
}

} // namespace thermvane
