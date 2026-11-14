#include "app/Application.hpp"

#include <QDebug>
#include <QSysInfo>
#include <algorithm>
#include <cmath>
#include <limits>

namespace thermvane {

namespace {

constexpr double kEmergencyClearHysteresisC = 3.0;

}

Application::Application(QObject *parent)
    : QObject(parent)
{
    // SMC/HID values can contain one transient stale sample while the
    // thermal controller and helper settle after process startup. Do not
    // enter the emergency override during that short initialization window.
    m_startupGraceTimer.start();

#if defined(Q_OS_MACOS)
    IHardwareBackend *backend = QSysInfo::currentCpuArchitecture() == QStringLiteral("arm64")
        ? static_cast<IHardwareBackend *>(&m_macBackend)
        : static_cast<IHardwareBackend *>(&m_backend);
#elif defined(Q_OS_LINUX)
    IHardwareBackend *backend = &m_linuxBackend;
#elif defined(Q_OS_WIN) || defined(_WIN32)
    IHardwareBackend *backend = &m_windowsBackend;
#else
    IHardwareBackend *backend = &m_backend;
#endif

    m_fanManager.setBackend(backend);
    m_sensorManager.setBackend(backend);
    m_fanModel.setManager(&m_fanManager);
    m_sensorModel.setManager(&m_sensorManager);

    qInfo() << "ThermVane application initialized on" << QSysInfo::prettyProductName()
            << "architecture" << QSysInfo::currentCpuArchitecture();

    connect(&m_sensorManager, &SensorManager::sensorsChanged,
            this, &Application::evaluateEmergencyPolicy);
    connect(&m_fanManager, &FanManager::fansChanged,
            this, &Application::evaluateEmergencyPolicy);
    connect(&m_fanController, &FanController::policyChanged,
            this, &Application::evaluateEmergencyPolicy);

    // The backend performs its first hardware scan synchronously, but helper
    // startup and queued hardware notifications can still arrive after the
    // QML engine has been created. Keep the native splash visible until two
    // consecutive event-loop samples expose a stable fan/sensor inventory.
    m_backendReadyTimer.setInterval(100);
    connect(&m_backendReadyTimer, &QTimer::timeout,
            this, &Application::checkBackendReady);
    m_backendReadyTimer.start();

    evaluateEmergencyPolicy();
}

FanModel *Application::fanModel()
{
    return &m_fanModel;
}

SensorModel *Application::sensorModel()
{
    return &m_sensorModel;
}

FanCurveModel *Application::fanCurveModel()
{
    return &m_fanCurveModel;
}

FanController *Application::fanController()
{
    return &m_fanController;
}

QList<SensorInfo> Application::sensors() const
{
    return m_sensorManager.sensors();
}

bool Application::shuttingDown() const
{
    return m_shuttingDown;
}

bool Application::backendReady() const
{
    return m_backendReady;
}

void Application::checkBackendReady()
{
    if (m_backendReady || m_shuttingDown) {
        m_backendReadyTimer.stop();
        return;
    }

    const int fanCount = m_fanManager.fans().size();
    const int sensorCount = m_sensorManager.sensors().size();
    if (fanCount == m_lastBackendFanCount && sensorCount == m_lastBackendSensorCount) {
        ++m_backendStableSamples;
    } else {
        m_lastBackendFanCount = fanCount;
        m_lastBackendSensorCount = sensorCount;
        m_backendStableSamples = 1;
    }

    if (m_backendStableSamples < 2) {
        return;
    }

    m_backendReady = true;
    m_backendReadyTimer.stop();
    qInfo() << "Hardware backend initialization completed; fans" << fanCount
            << "readable sensors" << sensorCount;
    emit backendReadyChanged();
}

bool Application::restoreFansToAutomatic()
{
    // Closing can be initiated from QML, the tray menu, or Qt's
    // aboutToQuit signal. Keep the safety policy disabled while the
    // synchronous backend calls hand control back to the firmware.
    if (!m_shuttingDown) {
        m_shuttingDown = true;
        emit shuttingDownChanged();
    }
    if (m_shutdownRestoreCompleted) {
        return true;
    }

    qInfo() << "Restoring all fans to system automatic control before application exit";
    const bool restored = m_fanManager.restoreAllAutomaticControl();
    m_shutdownRestoreCompleted = restored;
    return restored;
}

void Application::evaluateEmergencyPolicy()
{
    if (m_shuttingDown || m_evaluatingEmergencyPolicy) {
        return;
    }

    if (m_startupGraceTimer.isValid() && m_startupGraceTimer.elapsed() < 5000) {
        return;
    }

    m_evaluatingEmergencyPolicy = true;

    double highestTemperature = -std::numeric_limits<double>::infinity();
    for (const SensorInfo &sensor : m_sensorManager.sensors()) {
        if (!sensor.available || !std::isfinite(sensor.temperatureCelsius)) {
            continue;
        }

#if defined(Q_OS_MACOS)
        // Emergency control must use the real CPU/GPU SMC readings. HID and
        // IORegistry duplicate sensors can briefly report stale sentinel
        // values during startup and must not force both fans to 100%.
        const bool cpuOrGpu = sensor.name.startsWith(QStringLiteral("CPU "), Qt::CaseInsensitive)
            || sensor.name.startsWith(QStringLiteral("GPU "), Qt::CaseInsensitive);
        if (!cpuOrGpu || !sensor.source.startsWith(QStringLiteral("SMC "), Qt::CaseInsensitive)) {
            continue;
        }
#endif

        highestTemperature = std::max(highestTemperature, sensor.temperatureCelsius);
    }

    bool nextEmergencyActive = m_emergencyActive;
    if (std::isfinite(highestTemperature)) {
        if (highestTemperature >= m_fanController.emergencyTemperature()) {
            ++m_highTemperatureSamples;
            // Require two independent refreshes before taking over both
            // fans. This filters one stale SMC sample without weakening the
            // emergency response for a sustained over-temperature.
            nextEmergencyActive = m_emergencyActive || m_highTemperatureSamples >= 2;
        } else if (highestTemperature <= m_fanController.emergencyTemperature() - kEmergencyClearHysteresisC) {
            m_highTemperatureSamples = 0;
            nextEmergencyActive = false;
        }
    } else {
        m_highTemperatureSamples = 0;
        nextEmergencyActive = false;
    }

    // Keep this call idempotent even while the policy is already cleared.
    // FanManager retains failed emergency restores and uses subsequent
    // sensor cycles to retry them, preventing a transient helper/SMC error
    // from leaving a fan physically locked at full speed.
    m_fanManager.setEmergencyFullSpeed(nextEmergencyActive);
    if (nextEmergencyActive != m_emergencyActive) {
        qWarning() << "Emergency fan policy" << (nextEmergencyActive ? "activated" : "cleared")
                   << "highest temperature" << highestTemperature;
    }
    m_emergencyActive = nextEmergencyActive;

    m_evaluatingEmergencyPolicy = false;
}

} // namespace thermvane
