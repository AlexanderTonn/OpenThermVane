#include "models/FanCurveModel.hpp"

#include <algorithm>
#include <QVariantMap>

namespace thermvane {

FanCurveModel::FanCurveModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int FanCurveModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_curve.points().size());
}

QVariant FanCurveModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_curve.points().size()) {
        return {};
    }

    const auto &point = m_curve.points().at(index.row());
    switch (role) {
    case TemperatureRole:
        return point.temperature;
    case SpeedRole:
        return point.fanSpeed;
    default:
        return {};
    }
}

QHash<int, QByteArray> FanCurveModel::roleNames() const
{
    return {
        {TemperatureRole, "temperature"},
        {SpeedRole, "speed"},
    };
}

void FanCurveModel::movePoint(int row, double temperature, double speed)
{
    auto points = m_curve.points();
    if (row < 0 || row >= points.size()) {
        return;
    }

    points[row].temperature = std::clamp(temperature, 30.0, 95.0);
    points[row].fanSpeed = std::clamp(speed, 0.0, 100.0);

    beginResetModel();
    m_curve.setPoints(std::move(points));
    endResetModel();
}

double FanCurveModel::speedForTemperature(double temperature) const
{
    return m_curve.speedForTemperature(temperature);
}

QVariantList FanCurveModel::points() const
{
    QVariantList result;
    for (const auto &point : m_curve.points()) {
        QVariantMap item;
        item.insert(QStringLiteral("temperature"), point.temperature);
        item.insert(QStringLiteral("speed"), point.fanSpeed);
        result.append(item);
    }
    return result;
}

} // namespace thermvane
