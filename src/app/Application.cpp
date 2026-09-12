#include "app/Application.hpp"

namespace thermvane {

Application::Application(QObject *parent)
    : QObject(parent)
{
    m_fanManager.setBackend(&m_backend);
    m_sensorManager.setBackend(&m_backend);
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

} // namespace thermvane
