#include "app/Application.hpp"

#include <QSysInfo>

namespace thermvane {

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

} // namespace thermvane
