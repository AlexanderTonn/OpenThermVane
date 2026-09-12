#pragma once

#include "core/FanController.hpp"
#include "core/FanManager.hpp"
#include "core/SensorManager.hpp"
#include "hardware/mock/MockHardwareBackend.hpp"
#include "models/FanCurveModel.hpp"
#include "models/FanModel.hpp"
#include "models/SensorModel.hpp"

#include <QObject>

namespace thermvane {

class Application final : public QObject
{
    Q_OBJECT

public:
    explicit Application(QObject *parent = nullptr);

    FanModel *fanModel();
    SensorModel *sensorModel();
    FanCurveModel *fanCurveModel();
    FanController *fanController();

private:
    MockHardwareBackend m_backend;
    FanManager m_fanManager;
    SensorManager m_sensorManager;
    FanController m_fanController;
    FanModel m_fanModel;
    SensorModel m_sensorModel;
    FanCurveModel m_fanCurveModel;
};

} // namespace thermvane
