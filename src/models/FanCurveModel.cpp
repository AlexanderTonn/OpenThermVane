#include "models/FanCurveModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSet>
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

QString localFilePath(const QUrl &fileUrl)
{
    if (fileUrl.isLocalFile()) {
        return fileUrl.toLocalFile();
    }
    if (fileUrl.scheme().isEmpty()) {
        return fileUrl.toString();
    }
    return {};
}

QString curveFilePathForExport(const QUrl &fileUrl)
{
    QString path = localFilePath(fileUrl);
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".otvc");
    }
    return path;
}

QJsonArray pointsToJson(const QList<FanCurvePoint> &points)
{
    QJsonArray array;
    for (const FanCurvePoint &point : points) {
        QJsonObject pointObject;
        pointObject.insert(QStringLiteral("temperature"), point.temperature);
        pointObject.insert(QStringLiteral("speed"), point.fanSpeed);
        array.append(pointObject);
    }
    return array;
}

QList<FanCurvePoint> pointsFromJson(const QJsonArray &array)
{
    QList<FanCurvePoint> points;
    for (const QJsonValue &value : array) {
        const QJsonObject pointObject = value.toObject();
        const double temperature = pointObject.value(QStringLiteral("temperature")).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double speed = pointObject.value(QStringLiteral("speed")).toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(temperature) || !std::isfinite(speed)) {
            continue;
        }
        points.append({
            std::clamp(temperature, 30.0, 95.0),
            std::clamp(speed, 0.0, 100.0),
        });
    }
    return points;
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

bool FanCurveModel::exportCurves(const QUrl &fileUrl) const
{
    const QString path = curveFilePathForExport(fileUrl);
    if (path.isEmpty()) {
        return false;
    }

    QSet<QString> fanIds;
    for (auto it = m_curvesByFanId.constBegin(); it != m_curvesByFanId.constEnd(); ++it) {
        fanIds.insert(it.key());
    }
    for (auto it = m_sensorIdsByFanId.constBegin(); it != m_sensorIdsByFanId.constEnd(); ++it) {
        fanIds.insert(it.key());
    }
    for (const QString &fanId : m_curveAutoFanIds) {
        fanIds.insert(fanId);
    }
    fanIds.insert(normalizedFanId(m_selectedFanId));

    QStringList sortedFanIds = fanIds.values();
    sortedFanIds.sort();

    QJsonArray fanArray;
    for (const QString &fanId : sortedFanIds) {
        QJsonObject fanObject;
        fanObject.insert(QStringLiteral("fanId"), fanId);
        fanObject.insert(QStringLiteral("sensorId"), m_sensorIdsByFanId.value(fanId));
        fanObject.insert(QStringLiteral("curveAuto"), m_curveAutoFanIds.contains(fanId));
        fanObject.insert(QStringLiteral("points"), pointsToJson(curveForFan(fanId).points()));
        fanArray.append(fanObject);
    }

    QStringList autoFanIds = m_curveAutoFanIds.values();
    autoFanIds.sort();

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("OpenThermVaneCurveProfile"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("selectedFanId"), normalizedFanId(m_selectedFanId));
    root.insert(QStringLiteral("curveAutoFanIds"), QJsonArray::fromStringList(autoFanIds));
    root.insert(QStringLiteral("fans"), fanArray);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.error() == QFileDevice::NoError;
}

bool FanCurveModel::importCurves(const QUrl &fileUrl)
{
    const QString path = localFilePath(fileUrl);
    if (path.isEmpty()) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("OpenThermVaneCurveProfile")
        || root.value(QStringLiteral("version")).toInt() != 1) {
        return false;
    }

    QHash<QString, FanCurve> importedCurves;
    QHash<QString, QString> importedSensorIds;
    QSet<QString> importedCurveAutoFanIds;

    const QJsonArray fans = root.value(QStringLiteral("fans")).toArray();
    for (const QJsonValue &value : fans) {
        const QJsonObject fanObject = value.toObject();
        const QString fanId = normalizedFanId(fanObject.value(QStringLiteral("fanId")).toString());
        const QList<FanCurvePoint> points = pointsFromJson(fanObject.value(QStringLiteral("points")).toArray());
        if (points.size() >= 2) {
            importedCurves.insert(fanId, FanCurve(points));
        }

        const QString sensorId = fanObject.value(QStringLiteral("sensorId")).toString();
        if (!sensorId.isEmpty()) {
            importedSensorIds.insert(fanId, sensorId);
        }

        if (fanObject.value(QStringLiteral("curveAuto")).toBool()) {
            importedCurveAutoFanIds.insert(fanId);
        }
    }

    const QJsonArray autoFanIds = root.value(QStringLiteral("curveAutoFanIds")).toArray();
    for (const QJsonValue &value : autoFanIds) {
        const QString fanId = normalizedFanId(value.toString());
        if (!fanId.isEmpty()) {
            importedCurveAutoFanIds.insert(fanId);
        }
    }

    if (importedCurves.isEmpty()) {
        return false;
    }

    const QString selectedFanId = normalizedFanId(root.value(QStringLiteral("selectedFanId")).toString(m_selectedFanId));
    const QString nextSelectedFanId = importedCurves.contains(selectedFanId)
        ? selectedFanId
        : importedCurves.constBegin().key();

    beginResetModel();
    m_curvesByFanId = importedCurves;
    m_sensorIdsByFanId = importedSensorIds;
    m_curveAutoFanIds = importedCurveAutoFanIds;
    m_selectedFanId = nextSelectedFanId == fallbackFanId() ? QString() : nextSelectedFanId;
    endResetModel();

    saveAllSettings();

    emit selectedFanIdChanged();
    emit selectedSensorIdChanged();
    emit fanSensorBindingsChanged();
    emit curveAutoFansChanged();
    return true;
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

    const QStringList autoFanIds = settings.value(QStringLiteral("curveAutoFanIds")).toStringList();
    for (const QString &fanId : autoFanIds) {
        m_curveAutoFanIds.insert(normalizedFanId(fanId));
    }

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

void FanCurveModel::saveAllSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("fanCurves"));
    settings.remove(QString());
    settings.setValue(QStringLiteral("selectedFanId"), m_selectedFanId);

    settings.beginGroup(QStringLiteral("curves"));
    for (auto it = m_curvesByFanId.constBegin(); it != m_curvesByFanId.constEnd(); ++it) {
        settings.beginGroup(settingsKeyForFanId(it.key()));
        settings.setValue(QStringLiteral("fanId"), it.key());
        settings.beginWriteArray(QStringLiteral("points"));
        const auto &points = it.value().points();
        for (int index = 0; index < points.size(); ++index) {
            settings.setArrayIndex(index);
            settings.setValue(QStringLiteral("temperature"), points.at(index).temperature);
            settings.setValue(QStringLiteral("speed"), points.at(index).fanSpeed);
        }
        settings.endArray();
        settings.endGroup();
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("sensors"));
    for (auto it = m_sensorIdsByFanId.constBegin(); it != m_sensorIdsByFanId.constEnd(); ++it) {
        if (it.value().isEmpty()) {
            continue;
        }
        settings.beginGroup(settingsKeyForFanId(it.key()));
        settings.setValue(QStringLiteral("fanId"), it.key());
        settings.setValue(QStringLiteral("sensorId"), it.value());
        settings.endGroup();
    }
    settings.endGroup();

    QStringList autoFanIds = m_curveAutoFanIds.values();
    autoFanIds.sort();
    settings.setValue(QStringLiteral("curveAutoFanIds"), autoFanIds);
    settings.endGroup();
}

} // namespace thermvane
