#include "models/FanCurveModel.hpp"

#include <algorithm>
#include <QByteArray>
#include <QSettings>
#include <QVariantMap>

namespace thermvane {

namespace {

const QString &fallbackFanId()
{
    static const QString fallback = QStringLiteral("default");
    return fallback;
}

QString normalizedFanId(const QString &fanId)
{
    return fanId.isEmpty() ? fallbackFanId() : fanId;
}

QString settingsKeyForFanId(const QString &fanId)
{
    return QString::fromLatin1(normalizedFanId(fanId).toUtf8().toPercentEncoding());
}

QString fanIdFromSettingsKey(const QString &key)
{
    return QString::fromUtf8(QByteArray::fromPercentEncoding(key.toUtf8()));
}

} // namespace

FanCurveModel::FanCurveModel(QObject *parent)
    : QAbstractListModel(parent)
{
    loadSettings();
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

    saveSelectedFanId();

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
    return m_sensorIdsByFanId.value(normalizedFanId(fanId));
}

void FanCurveModel::setSensorIdForFan(const QString &fanId, const QString &sensorId)
{
    const QString key = normalizedFanId(fanId);
    if (m_sensorIdsByFanId.value(key) == sensorId) {
        return;
    }

    if (sensorId.isEmpty()) {
        m_sensorIdsByFanId.remove(key);
    } else {
        m_sensorIdsByFanId.insert(key, sensorId);
    }

    saveSensorIdForFan(key);

    if (key == normalizedFanId(m_selectedFanId)) {
        emit selectedSensorIdChanged();
    }
    emit fanSensorBindingsChanged();
}

double FanCurveModel::speedForFanTemperature(const QString &fanId, double temperature) const
{
    return curveForFan(fanId).speedForTemperature(temperature);
}

QStringList FanCurveModel::curveAutoFanIds() const
{
    QStringList ids = m_curveAutoFanIds.values();
    ids.sort();
    return ids;
}

bool FanCurveModel::curveAutoForFan(const QString &fanId) const
{
    return m_curveAutoFanIds.contains(normalizedFanId(fanId));
}

void FanCurveModel::setCurveAutoForFan(const QString &fanId, bool enabled)
{
    const QString key = normalizedFanId(fanId);
    const bool changed = enabled ? !m_curveAutoFanIds.contains(key) : m_curveAutoFanIds.contains(key);
    if (!changed) {
        return;
    }

    if (enabled) {
        m_curveAutoFanIds.insert(key);
    } else {
        m_curveAutoFanIds.remove(key);
    }

    saveCurveAutoFanIds();
    emit curveAutoFansChanged();
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

    saveCurveForFan(m_selectedFanId);
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
    const QString key = normalizedFanId(fanId);
    if (!m_curvesByFanId.contains(key)) {
        m_curvesByFanId.insert(key, FanCurve {});
    }
    return m_curvesByFanId[key];
}

const FanCurve &FanCurveModel::curveForFan(const QString &fanId) const
{
    const QString key = normalizedFanId(fanId);
    const auto it = m_curvesByFanId.constFind(key);
    return it == m_curvesByFanId.constEnd() ? m_defaultCurve : it.value();
}

void FanCurveModel::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("fanCurves"));

    m_selectedFanId = settings.value(QStringLiteral("selectedFanId")).toString();

    settings.beginGroup(QStringLiteral("curves"));
    const QStringList curveGroups = settings.childGroups();
    for (const QString &curveGroup : curveGroups) {
        settings.beginGroup(curveGroup);
        const QString fanId = settings.value(QStringLiteral("fanId"), fanIdFromSettingsKey(curveGroup)).toString();
        QList<FanCurvePoint> points;
        const int pointCount = settings.beginReadArray(QStringLiteral("points"));
        for (int index = 0; index < pointCount; ++index) {
            settings.setArrayIndex(index);
            bool temperatureOk = false;
            bool speedOk = false;
            const double temperature = settings.value(QStringLiteral("temperature")).toDouble(&temperatureOk);
            const double speed = settings.value(QStringLiteral("speed")).toDouble(&speedOk);
            if (temperatureOk && speedOk) {
                points.append({temperature, speed});
            }
        }
        settings.endArray();
        settings.endGroup();

        if (points.size() >= 2) {
            m_curvesByFanId.insert(normalizedFanId(fanId), FanCurve(points));
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("sensors"));
    const QStringList sensorGroups = settings.childGroups();
    for (const QString &sensorGroup : sensorGroups) {
        settings.beginGroup(sensorGroup);
        const QString fanId = settings.value(QStringLiteral("fanId"), fanIdFromSettingsKey(sensorGroup)).toString();
        const QString sensorId = settings.value(QStringLiteral("sensorId")).toString();
        settings.endGroup();

        if (!sensorId.isEmpty()) {
            m_sensorIdsByFanId.insert(normalizedFanId(fanId), sensorId);
        }
    }
    settings.endGroup();

    settings.remove(QStringLiteral("curveAutoFanIds"));

    settings.endGroup();
}

void FanCurveModel::saveSelectedFanId() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("fanCurves/selectedFanId"), m_selectedFanId);
}

void FanCurveModel::saveCurveForFan(const QString &fanId) const
{
    const QString key = normalizedFanId(fanId);
    const auto it = m_curvesByFanId.constFind(key);
    if (it == m_curvesByFanId.constEnd()) {
        return;
    }

    QSettings settings;
    settings.beginGroup(QStringLiteral("fanCurves"));
    settings.beginGroup(QStringLiteral("curves"));
    settings.beginGroup(settingsKeyForFanId(key));
    settings.setValue(QStringLiteral("fanId"), key);
    settings.beginWriteArray(QStringLiteral("points"));
    const auto &points = it.value().points();
    for (int index = 0; index < points.size(); ++index) {
        settings.setArrayIndex(index);
        settings.setValue(QStringLiteral("temperature"), points.at(index).temperature);
        settings.setValue(QStringLiteral("speed"), points.at(index).fanSpeed);
    }
    settings.endArray();
    settings.endGroup();
    settings.endGroup();
    settings.endGroup();
}

void FanCurveModel::saveSensorIdForFan(const QString &fanId) const
{
    const QString key = normalizedFanId(fanId);
    QSettings settings;
    settings.beginGroup(QStringLiteral("fanCurves"));
    settings.beginGroup(QStringLiteral("sensors"));

    const QString settingsKey = settingsKeyForFanId(key);
    const QString sensorId = m_sensorIdsByFanId.value(key);
    if (sensorId.isEmpty()) {
        settings.remove(settingsKey);
    } else {
        settings.beginGroup(settingsKey);
        settings.setValue(QStringLiteral("fanId"), key);
        settings.setValue(QStringLiteral("sensorId"), sensorId);
        settings.endGroup();
    }

    settings.endGroup();
    settings.endGroup();
}

void FanCurveModel::saveCurveAutoFanIds() const
{
    QStringList ids = m_curveAutoFanIds.values();
    ids.sort();

    QSettings settings;
    settings.setValue(QStringLiteral("fanCurves/curveAutoFanIds"), ids);
}

} // namespace thermvane
