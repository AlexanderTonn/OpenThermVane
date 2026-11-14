#pragma once

#include "core/FanController.hpp"
#include "core/FanManager.hpp"
#include "core/SensorManager.hpp"

#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "hardware/linux/LinuxHardwareBackend.hpp"
#endif
#if defined(Q_OS_WIN) || defined(_WIN32)
#include "hardware/windows/WindowsHardwareBackend.hpp"
#endif
#include "hardware/mock/MockHardwareBackend.hpp"
#include "hardware/macos/MacHardwareBackend.hpp"
#include "hardware/HardwareTypes.hpp"
#include "models/FanCurveModel.hpp"
#include "models/FanModel.hpp"
#include "models/SensorModel.hpp"

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

namespace thermvane {

class Application final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool shuttingDown READ shuttingDown NOTIFY shuttingDownChanged)
    Q_PROPERTY(bool backendReady READ backendReady NOTIFY backendReadyChanged)

public:
    explicit Application(QObject *parent = nullptr);

    FanModel *fanModel();
    SensorModel *sensorModel();
    FanCurveModel *fanCurveModel();
    FanController *fanController();
    QList<SensorInfo> sensors() const;
    bool shuttingDown() const;
    bool backendReady() const;

    Q_INVOKABLE bool restoreFansToAutomatic();

signals:
    void shuttingDownChanged();
    void backendReadyChanged();

private:
    void evaluateEmergencyPolicy();
    void checkBackendReady();

    MockHardwareBackend m_backend;
#if defined(Q_OS_LINUX)
    LinuxHardwareBackend m_linuxBackend;
#endif
#if defined(Q_OS_WIN) || defined(_WIN32)
    WindowsHardwareBackend m_windowsBackend;
#endif
    MacHardwareBackend m_macBackend;
    FanManager m_fanManager;
    SensorManager m_sensorManager;
    FanController m_fanController;
    FanModel m_fanModel;
    SensorModel m_sensorModel;
    FanCurveModel m_fanCurveModel;
    bool m_emergencyActive = false;
    bool m_evaluatingEmergencyPolicy = false;
    bool m_shuttingDown = false;
    bool m_shutdownRestoreCompleted = false;
    int m_highTemperatureSamples = 0;
    QElapsedTimer m_startupGraceTimer;
    QTimer m_backendReadyTimer;
    int m_lastBackendFanCount = -1;
    int m_lastBackendSensorCount = -1;
    int m_backendStableSamples = 0;
    bool m_backendReady = false;
};

} // namespace thermvane
