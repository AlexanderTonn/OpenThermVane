#include "app/Application.hpp"

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

    connect(&m_sensorManager, &SensorManager::sensorsChanged,
            this, &Application::evaluateEmergencyPolicy);
    connect(&m_fanManager, &FanManager::fansChanged,
            this, &Application::evaluateEmergencyPolicy);
    connect(&m_fanController, &FanController::policyChanged,
            this, &Application::evaluateEmergencyPolicy);

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

void Application::evaluateEmergencyPolicy()
{
    if (m_evaluatingEmergencyPolicy) {
        return;
    }

    m_evaluatingEmergencyPolicy = true;

    double highestTemperature = -std::numeric_limits<double>::infinity();
    for (const SensorInfo &sensor : m_sensorManager.sensors()) {
        if (!sensor.available || !std::isfinite(sensor.temperatureCelsius)) {
            continue;
        }
        highestTemperature = std::max(highestTemperature, sensor.temperatureCelsius);
    }

    bool nextEmergencyActive = m_emergencyActive;
    if (std::isfinite(highestTemperature)) {
        if (highestTemperature >= m_fanController.emergencyTemperature()) {
            nextEmergencyActive = true;
        } else if (highestTemperature <= m_fanController.emergencyTemperature() - kEmergencyClearHysteresisC) {
            nextEmergencyActive = false;
        }
    } else {
        nextEmergencyActive = false;
    }

    if (nextEmergencyActive != m_emergencyActive || nextEmergencyActive) {
        m_fanManager.setEmergencyFullSpeed(nextEmergencyActive);
        m_emergencyActive = nextEmergencyActive;
    }

    m_evaluatingEmergencyPolicy = false;
}

} // namespace thermvane
