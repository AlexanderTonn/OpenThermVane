#pragma once

#include "core/SensorManager.hpp"

#include <QAbstractListModel>
#include <QPointer>

namespace thermvane {

class SensorModel final : public QAbstractListModel
{
    Q_OBJECT

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

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void scan();

private:
    QPointer<SensorManager> m_manager;
};

} // namespace thermvane
