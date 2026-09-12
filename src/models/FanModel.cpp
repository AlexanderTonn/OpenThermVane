#include "models/FanModel.hpp"

namespace thermvane {

FanModel::FanModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void FanModel::setManager(FanManager *manager)
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
        connect(m_manager, &FanManager::fansChanged, this, [this] {
            beginResetModel();
            endResetModel();
        });
    }
}

int FanModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_manager) {
        return 0;
    }
    return static_cast<int>(m_manager->fans().size());
}

QVariant FanModel::data(const QModelIndex &index, int role) const
{
    if (!m_manager || !index.isValid()) {
        return {};
    }

    const auto fans = m_manager->fans();
    if (index.row() < 0 || index.row() >= fans.size()) {
        return {};
    }

    const auto &fan = fans.at(index.row());
    switch (role) {
    case IdRole:
        return fan.id;
    case NameRole:
        return fan.name;
    case RpmRole:
        return fan.rpm;
    case SpeedPercentRole:
        return fan.speedPercent;
    case AutomaticRole:
        return fan.automatic;
    case SupportsControlRole:
        return fan.capabilities.manualControl;
    case SupportsRpmRole:
        return fan.capabilities.rpmReading;
    case SupportsFirmwareControlRole:
        return fan.capabilities.firmwareControl;
    default:
        return {};
    }
}

QHash<int, QByteArray> FanModel::roleNames() const
{
    return {
        {IdRole, "fanId"},
        {NameRole, "name"},
        {RpmRole, "rpm"},
        {SpeedPercentRole, "speedPercent"},
        {AutomaticRole, "automatic"},
        {SupportsControlRole, "supportsControl"},
        {SupportsRpmRole, "supportsRpm"},
        {SupportsFirmwareControlRole, "supportsFirmwareControl"},
    };
}


bool FanModel::setManualSpeed(const QString &fanId, double percent)
{
    return m_manager && m_manager->setManualSpeed(fanId, percent);
}

bool FanModel::restoreAutomaticControl(const QString &fanId)
{
    return m_manager && m_manager->restoreAutomaticControl(fanId);
}

} // namespace thermvane
