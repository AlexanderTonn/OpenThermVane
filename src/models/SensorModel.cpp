#include "models/SensorModel.hpp"

#include <utility>

namespace thermvane {

namespace {

bool sameSensorOrder(const QList<SensorInfo> &left, const QList<SensorInfo> &right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (qsizetype index = 0; index < left.size(); ++index) {
        if (left.at(index).id != right.at(index).id) {
            return false;
        }
    }

    return true;
}

} // namespace

SensorModel::SensorModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void SensorModel::setManager(SensorManager *manager)
{
    if (m_manager == manager) {
        return;
    }

    if (m_manager) {
        disconnect(m_manager, nullptr, this, nullptr);
    }

    beginResetModel();
    m_manager = manager;
    m_sensors = m_manager ? m_manager->sensors() : QList<SensorInfo> {};
    endResetModel();

    if (m_manager) {
        connect(m_manager, &SensorManager::sensorsChanged, this, [this] {
            replaceSensors(m_manager ? m_manager->sensors() : QList<SensorInfo> {});
        });
        connect(m_manager, &SensorManager::refreshIntervalMsChanged,
                this, &SensorModel::refreshIntervalMsChanged);
    }

    emit refreshIntervalMsChanged();
}

int SensorModel::refreshIntervalMs() const
{
    return m_manager ? m_manager->refreshIntervalMs() : 1000;
}

void SensorModel::setRefreshIntervalMs(int intervalMs)
{
    if (m_manager) {
        m_manager->setRefreshIntervalMs(intervalMs);
    }
}

int SensorModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_sensors.size());
}

QVariant SensorModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    if (index.row() < 0 || index.row() >= m_sensors.size()) {
        return {};
    }

    const auto &sensor = m_sensors.at(index.row());
    switch (role) {
    case IdRole:
        return sensor.id;
    case NameRole:
        return sensor.name;
    case TemperatureRole:
        return sensor.temperatureCelsius;
    case SourceRole:
        return sensor.source;
    case AvailableRole:
        return sensor.available;
    default:
        return {};
    }
}

QHash<int, QByteArray> SensorModel::roleNames() const
{
    return {
        {IdRole, "sensorId"},
        {NameRole, "name"},
        {TemperatureRole, "temperature"},
        {SourceRole, "source"},
        {AvailableRole, "available"},
    };
}


void SensorModel::replaceSensors(QList<SensorInfo> sensors)
{
    if (sameSensorOrder(m_sensors, sensors)) {
        m_sensors = std::move(sensors);
        if (!m_sensors.isEmpty()) {
            emit dataChanged(index(0), index(m_sensors.size() - 1), {NameRole, TemperatureRole, SourceRole, AvailableRole});
        }
        return;
    }

    beginResetModel();
    m_sensors = std::move(sensors);
    endResetModel();
}

} // namespace thermvane
