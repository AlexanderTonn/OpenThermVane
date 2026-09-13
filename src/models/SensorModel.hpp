#pragma once

#include "core/SensorManager.hpp"

#include <QAbstractListModel>
#include <QPointer>

namespace thermvane {

class SensorModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int refreshIntervalMs READ refreshIntervalMs WRITE setRefreshIntervalMs NOTIFY refreshIntervalMsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TemperatureRole,
        SourceRole,
        AvailableRole,
    };

    explicit SensorModel(QObject *parent = nullptr);

    void setManager(SensorManager *manager);
    int refreshIntervalMs() const;
    void setRefreshIntervalMs(int intervalMs);
    int count() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void refreshIntervalMsChanged();
    void countChanged();

private:
    void replaceSensors(QList<SensorInfo> sensors);

    QPointer<SensorManager> m_manager;
    QList<SensorInfo> m_sensors;
};

} // namespace thermvane
