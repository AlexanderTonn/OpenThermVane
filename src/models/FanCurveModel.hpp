#pragma once

#include "core/FanCurve.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace thermvane {

class FanCurveModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString selectedFanId READ selectedFanId WRITE setSelectedFanId NOTIFY selectedFanIdChanged)
    Q_PROPERTY(QString selectedSensorId READ selectedSensorId WRITE setSelectedSensorId NOTIFY selectedSensorIdChanged)

public:
    enum Role {
        TemperatureRole = Qt::UserRole + 1,
        SpeedRole,
    };

    explicit FanCurveModel(QObject *parent = nullptr);

    QString selectedFanId() const;
    void setSelectedFanId(const QString &fanId);

    QString selectedSensorId() const;
    void setSelectedSensorId(const QString &sensorId);

    Q_INVOKABLE QString sensorIdForFan(const QString &fanId) const;
    Q_INVOKABLE void setSensorIdForFan(const QString &fanId, const QString &sensorId);
    Q_INVOKABLE double speedForFanTemperature(const QString &fanId, double temperature) const;
    Q_INVOKABLE QStringList curveAutoFanIds() const;
    Q_INVOKABLE bool curveAutoForFan(const QString &fanId) const;
    Q_INVOKABLE void setCurveAutoForFan(const QString &fanId, bool enabled);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void movePoint(int row, double temperature, double speed);
    Q_INVOKABLE double speedForTemperature(double temperature) const;
    Q_INVOKABLE QVariantList points() const;

signals:
    void selectedFanIdChanged();
    void selectedSensorIdChanged();
    void fanSensorBindingsChanged();
    void curveAutoFansChanged();

private:
    FanCurve &curveForFan(const QString &fanId);
    const FanCurve &curveForFan(const QString &fanId) const;
    void loadSettings();
    void saveSelectedFanId() const;
    void saveCurveForFan(const QString &fanId) const;
    void saveSensorIdForFan(const QString &fanId) const;
    void saveCurveAutoFanIds() const;

    QString m_selectedFanId;
    QHash<QString, QString> m_sensorIdsByFanId;
    QHash<QString, FanCurve> m_curvesByFanId;
    QSet<QString> m_curveAutoFanIds;
    FanCurve m_defaultCurve;
};

} // namespace thermvane
