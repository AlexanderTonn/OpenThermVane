#include "core/FanManager.hpp"

#include "hardware/IHardwareBackend.hpp"

#include <QDebug>
#include <QScopedValueRollback>
#include <algorithm>
#include <utility>

namespace thermvane {

FanManager::FanManager(QObject *parent)
    : QObject(parent)
{
}

FanManager::~FanManager()
{
    restoreAllAutomaticControl();
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
        qWarning() << "Cannot set manual fan speed without a hardware backend" << fanId;
        return false;
    }

    const double normalized = std::clamp(percent, 0.0, 100.0);
    if (!m_backend->setFanSpeed(fanId, m_emergencyActive ? 100.0 : normalized)) {
        // Backends must roll back a partially applied manual SMC/PWM mode.
        // Keep this defensive restore here as well so a backend error can
        // never leave the physical fan in an untracked manual state.
        m_backend->restoreAutomaticControl(fanId);
        qWarning() << "Manual fan speed request failed" << fanId << normalized;
        return false;
    }

    qInfo() << "Manual fan speed set" << fanId << normalized << "%";

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
        qWarning() << "Cannot restore fan without a hardware backend" << fanId;
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
        qWarning() << "Failed to restore fan to system automatic control" << fanId;
        return false;
    }
    qInfo() << "Fan restored to system automatic control" << fanId;
    m_manualSpeedOverrides.remove(fanId);
    refresh();
    return true;
}

bool FanManager::restoreAllAutomaticControl()
{
    if (!m_backend) {
        return true;
    }

    QSet<QString> fanIds = m_emergencyControlledFans;
    for (auto it = m_manualSpeedOverrides.cbegin(); it != m_manualSpeedOverrides.cend(); ++it) {
        fanIds.insert(it.key());
    }

    // Include the current hardware state as a fallback. This covers a
    // manual override that was restored from the privileged helper but was
    // not present in the in-memory override map anymore.
    for (const FanInfo &fan : std::as_const(m_fans)) {
        if (fan.capabilities.firmwareControl) {
            fanIds.insert(fan.id);
        }
    }

    // Shutdown restoration must never trigger the emergency path from a
    // synchronous backend scan. Keep the captured fan IDs, then disable the
    // policy before the first restore operation emits hardwareChanged.
    m_emergencyActive = false;

    bool allOk = true;
    QSet<QString> restoredFanIds;
    for (const QString &fanId : std::as_const(fanIds)) {
        // Do not route through restoreAutomaticControl(): that method keeps
        // an emergency override at full speed while the safety policy is
        // active. Shutdown must always return control to the SMC firmware.
        if (m_backend->restoreAutomaticControl(fanId)) {
            restoredFanIds.insert(fanId);
            qInfo() << "Fan restored during shutdown" << fanId;
        } else {
            qWarning() << "Failed to restore fan during shutdown" << fanId;
            allOk = false;
        }
    }

    for (FanInfo &fan : m_fans) {
        if (restoredFanIds.contains(fan.id)) {
            fan.automatic = true;
        }
    }
    m_manualSpeedOverrides.clear();
    m_emergencyControlledFans.clear();
    qInfo() << "Shutdown fan restore completed; success" << allOk << "fans" << fanIds.size();
    return allOk;
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

    m_emergencyActive = false;
    // The backend emits hardwareChanged synchronously after each restore.
    // Clear the policy first so refresh() cannot immediately re-apply full
    // emergency speed while this sequence is running.
    const bool ok = restoreEmergencyControlledFans();
    // Keep the set when restoration failed. Sensor updates continue while
    // the application is running, so the next cycle can retry the hardware
    // restore instead of leaving a fan physically locked at full speed while
    // the UI already reports the normal curve value.
    if (ok) {
        m_emergencyControlledFans.clear();
    }
    refresh();
    return ok;
}

void FanManager::refresh()
{
    // A backend scan emits hardwareChanged synchronously. Applying an
    // emergency speed starts another scan, so re-entering refresh here would
    // recurse until the main-thread stack overflows.
    if (m_refreshing) {
        return;
    }
    QScopedValueRollback<bool> refreshGuard(m_refreshing, true);

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

    // setFanSpeed() performs a synchronous backend scan. That scan emits
    // hardwareChanged, which can call refresh() immediately. Without this
    // guard, refresh() would call applyEmergencySpeed() again indefinitely.
    if (m_applyingEmergencySpeed) {
        return true;
    }
    QScopedValueRollback<bool> emergencyGuard(m_applyingEmergencySpeed, true);

    bool allOk = true;
    // setFanSpeed() may synchronously emit hardwareChanged(), causing
    // refresh() to replace m_fans while this loop is still running. Iterate
    // over a snapshot and keep the ID by value; references into m_fans would
    // otherwise become dangling and can corrupt QString/QSet internals.
    const QList<FanInfo> fansSnapshot = m_fans;
    for (const FanInfo &fan : fansSnapshot) {
        if (!fan.capabilities.manualControl) {
            continue;
        }

        const QString fanId = fan.id;
        if (m_backend->setFanSpeed(fanId, 100.0)) {
            m_emergencyControlledFans.insert(fanId);
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
