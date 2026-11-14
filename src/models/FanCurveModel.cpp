#include "models/FanCurveModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
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

constexpr qint64 kMaxCurveFileBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaxImportedFans = 256;
constexpr qsizetype kMaxPointsPerFan = 512;

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

QSettings fanCurveSettings()
{
#if defined(Q_OS_MACOS)
    // Keep curve and sensor bindings independent of the bundle identifier.
    // Older builds used a different identifier, so QSettings' implicit
    // native path could otherwise silently switch to an empty plist.
    return QSettings(QDir::homePath()
                         + QStringLiteral("/Library/Preferences/com.openthermvane.ThermVane.plist"),
                     QSettings::NativeFormat);
#else
    return QSettings();
#endif
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
    const qsizetype pointCount = std::min<qsizetype>(array.size(), kMaxPointsPerFan);
    for (qsizetype index = 0; index < pointCount; ++index) {
        const QJsonValue value = array.at(index);
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
    qInfo() << "Selected fan curve changed to" << m_selectedFanId;

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

QString FanCurveModel::startupMode() const
{
    return m_startupMode;
}

void FanCurveModel::setStartupMode(const QString &mode)
{
    const QString normalized = mode.compare(QStringLiteral("curve"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("curve")
        : QStringLiteral("system");
    if (m_startupMode == normalized) {
        return;
    }

    m_startupMode = normalized;
    QSettings settings = fanCurveSettings();
    settings.setValue(QStringLiteral("fanCurves/startupMode"), m_startupMode);
    settings.sync();
    qInfo() << "Fan startup mode changed to" << m_startupMode;
    emit startupModeChanged();
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
    qInfo() << "Sensor binding changed" << key << "->" << sensorId;

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
        qWarning() << "Curve export rejected: invalid file path" << fileUrl;
        return false;
    }

    // A profile is deliberately independent of a physical fan. This lets a
    // curve exported from one fan be imported into another one without
    // changing sensor bindings, auto modes, or the selected fan.
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("OpenThermVaneCurveProfile"));
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("points"), pointsToJson(curveForFan(m_selectedFanId).points()));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Curve export failed to open file" << path << file.errorString();
        return false;
    }

    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || file.error() != QFileDevice::NoError) {
        qWarning() << "Curve export failed while writing" << path << file.errorString();
        return false;
    }
    const bool committed = file.commit();
    if (committed) {
        qInfo() << "Curve exported" << path << "points"
                << curveForFan(m_selectedFanId).points().size();
    } else {
        qWarning() << "Curve export failed to commit" << path << file.errorString();
    }
    return committed;
}

bool FanCurveModel::importCurves(const QUrl &fileUrl)
{
    const QString path = localFilePath(fileUrl);
    if (path.isEmpty()) {
        qWarning() << "Curve import rejected: invalid file path" << fileUrl;
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Curve import failed to open file" << path << file.errorString();
        return false;
    }

    if (file.size() < 0 || file.size() > kMaxCurveFileBytes) {
        qWarning() << "Curve import rejected due to file size" << path << file.size();
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        qWarning() << "Curve import rejected: invalid JSON" << path;
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("OpenThermVaneCurveProfile")) {
        qWarning() << "Curve import rejected: unknown format" << path;
        return false;
    }

    const int version = root.value(QStringLiteral("version")).toInt();
    QJsonArray pointArray;
    if (version == 2) {
        pointArray = root.value(QStringLiteral("points")).toArray();
    } else if (version == 1) {
        // Read legacy multi-fan profiles without restoring any of their
        // settings. Only the points are imported into the currently selected
        // fan, preserving compatibility with files created by older builds.
        const QJsonArray fans = root.value(QStringLiteral("fans")).toArray();
        if (fans.size() > kMaxImportedFans) {
            return false;
        }

        const QString legacySelectedFanId = normalizedFanId(
            root.value(QStringLiteral("selectedFanId")).toString());
        for (const QJsonValue &value : fans) {
            const QJsonObject fanObject = value.toObject();
            if (normalizedFanId(fanObject.value(QStringLiteral("fanId")).toString())
                == legacySelectedFanId) {
                pointArray = fanObject.value(QStringLiteral("points")).toArray();
                break;
            }
        }
    } else {
        qWarning() << "Curve import rejected: unsupported version" << version << path;
        return false;
    }

    const QList<FanCurvePoint> importedPoints = pointsFromJson(pointArray);
    if (importedPoints.size() < 2) {
        qWarning() << "Curve import rejected: fewer than two valid points" << path;
        return false;
    }

    beginResetModel();
    m_curvesByFanId.insert(normalizedFanId(m_selectedFanId), FanCurve(importedPoints));
    endResetModel();

    saveCurveForFan(m_selectedFanId);
    qInfo() << "Curve imported" << path << "into fan" << m_selectedFanId
            << "points" << importedPoints.size();
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
    QSettings settings = fanCurveSettings();
    settings.beginGroup(QStringLiteral("fanCurves"));

    m_selectedFanId = settings.value(QStringLiteral("selectedFanId")).toString();
    m_startupMode = settings.value(QStringLiteral("startupMode"), QStringLiteral("system")).toString()
                        .compare(QStringLiteral("curve"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("curve")
        : QStringLiteral("system");

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

    qInfo() << "Loaded fan curve settings; selected fan" << m_selectedFanId
            << "startup mode" << m_startupMode
            << "sensor bindings" << m_sensorIdsByFanId.size();
    for (auto it = m_sensorIdsByFanId.constBegin(); it != m_sensorIdsByFanId.constEnd(); ++it) {
        qInfo() << "Loaded sensor binding" << it.key() << "->" << it.value();
    }
}

void FanCurveModel::saveSelectedFanId() const
{
    QSettings settings = fanCurveSettings();
    settings.setValue(QStringLiteral("fanCurves/selectedFanId"), m_selectedFanId);
}

void FanCurveModel::saveCurveForFan(const QString &fanId) const
{
    const QString key = normalizedFanId(fanId);
    const auto it = m_curvesByFanId.constFind(key);
    if (it == m_curvesByFanId.constEnd()) {
        return;
    }

    QSettings settings = fanCurveSettings();
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
    QSettings settings = fanCurveSettings();
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

    QSettings settings = fanCurveSettings();
    settings.setValue(QStringLiteral("fanCurves/curveAutoFanIds"), ids);
}

void FanCurveModel::saveAllSettings() const
{
    QSettings settings = fanCurveSettings();
    settings.beginGroup(QStringLiteral("fanCurves"));
    settings.remove(QString());
    settings.setValue(QStringLiteral("selectedFanId"), m_selectedFanId);
    settings.setValue(QStringLiteral("startupMode"), m_startupMode);

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
