#include "models/FanCurveModel.hpp"

#include <algorithm>
#include <QVariantMap>

namespace thermvane {

namespace {

const QString &fallbackFanId()
{
    static const QString fallback = QStringLiteral("default");
    return fallback;
}

} // namespace

FanCurveModel::FanCurveModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QString FanCurveModel::selectedFanId() const
{
    return m_selectedFanId;
}

void FanCurveModel::setSelectedFanId(const QString &fanId)
{
    if (m_selectedFanId == fanId) {
        return;
    }

    beginResetModel();
    m_selectedFanId = fanId;
    curveForFan(m_selectedFanId);
    endResetModel();

    emit selectedFanIdChanged();
    emit selectedSensorIdChanged();
}

QString FanCurveModel::selectedSensorId() const
{
    return sensorIdForFan(m_selectedFanId);
}

void FanCurveModel::setSelectedSensorId(const QString &sensorId)
{
    setSensorIdForFan(m_selectedFanId, sensorId);
}

QString FanCurveModel::sensorIdForFan(const QString &fanId) const
{
    const QString key = fanId.isEmpty() ? fallbackFanId() : fanId;
    return m_sensorIdsByFanId.value(key);
}

void FanCurveModel::setSensorIdForFan(const QString &fanId, const QString &sensorId)
{
    const QString key = fanId.isEmpty() ? fallbackFanId() : fanId;
    if (m_sensorIdsByFanId.value(key) == sensorId) {
        return;
    }

    if (sensorId.isEmpty()) {
        m_sensorIdsByFanId.remove(key);
    } else {
        m_sensorIdsByFanId.insert(key, sensorId);
    }

    if (key == (m_selectedFanId.isEmpty() ? fallbackFanId() : m_selectedFanId)) {
        emit selectedSensorIdChanged();
    }
    emit fanSensorBindingsChanged();
}

double FanCurveModel::speedForFanTemperature(const QString &fanId, double temperature) const
{
    return curveForFan(fanId).speedForTemperature(temperature);
}

int FanCurveModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(curveForFan(m_selectedFanId).points().size());
}

QVariant FanCurveModel::data(const QModelIndex &index, int role) const
{
    const auto &points = curveForFan(m_selectedFanId).points();
    if (!index.isValid() || index.row() < 0 || index.row() >= points.size()) {
        return {};
    }

    const auto &point = points.at(index.row());
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
    FanCurve &curve = curveForFan(m_selectedFanId);
    auto points = curve.points();
    if (row < 0 || row >= points.size()) {
        return;
    }

    points[row].temperature = std::clamp(temperature, 30.0, 95.0);
    points[row].fanSpeed = std::clamp(speed, 0.0, 100.0);

    beginResetModel();
    curve.setPoints(std::move(points));
    endResetModel();
}

double FanCurveModel::speedForTemperature(double temperature) const
{
    return curveForFan(m_selectedFanId).speedForTemperature(temperature);
}

QVariantList FanCurveModel::points() const
{
    QVariantList result;
    for (const auto &point : curveForFan(m_selectedFanId).points()) {
        QVariantMap item;
        item.insert(QStringLiteral("temperature"), point.temperature);
        item.insert(QStringLiteral("speed"), point.fanSpeed);
        result.append(item);
    }
    return result;
}

FanCurve &FanCurveModel::curveForFan(const QString &fanId)
{
    const QString key = fanId.isEmpty() ? fallbackFanId() : fanId;
    if (!m_curvesByFanId.contains(key)) {
        m_curvesByFanId.insert(key, FanCurve {});
    }
    return m_curvesByFanId[key];
}

const FanCurve &FanCurveModel::curveForFan(const QString &fanId) const
{
    const QString key = fanId.isEmpty() ? fallbackFanId() : fanId;
    const auto it = m_curvesByFanId.constFind(key);
    return it == m_curvesByFanId.constEnd() ? m_defaultCurve : it.value();
}

} // namespace thermvane
