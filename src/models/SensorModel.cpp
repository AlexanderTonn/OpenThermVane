#include "models/SensorModel.hpp"

namespace thermvane {

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
    endResetModel();

    if (m_manager) {
        connect(m_manager, &SensorManager::sensorsChanged, this, [this] {
            beginResetModel();
            endResetModel();
        });
    }
}

int SensorModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_manager) {
        return 0;
    }
    return static_cast<int>(m_manager->sensors().size());
}

QVariant SensorModel::data(const QModelIndex &index, int role) const
{
    if (!m_manager || !index.isValid()) {
        return {};
    }

    const auto sensors = m_manager->sensors();
    if (index.row() < 0 || index.row() >= sensors.size()) {
        return {};
    }

    const auto &sensor = sensors.at(index.row());
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

void SensorModel::scan()
{
    if (m_manager) {
        m_manager->scan();
    }
}

} // namespace thermvane
