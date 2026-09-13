#pragma once

#include "core/FanManager.hpp"

#include <QAbstractListModel>
#include <QPointer>

namespace thermvane {

class FanModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        RpmRole,
        SpeedPercentRole,
        AutomaticRole,
        SupportsControlRole,
        SupportsRpmRole,
        SupportsFirmwareControlRole,
    };

    explicit FanModel(QObject *parent = nullptr);

    void setManager(FanManager *manager);
    int count() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;

    Q_INVOKABLE bool setManualSpeed(const QString &fanId, double percent);
    Q_INVOKABLE bool restoreAutomaticControl(const QString &fanId);

signals:
    void countChanged();

private:
    QPointer<FanManager> m_manager;
    int m_lastCount = 0;
};

} // namespace thermvane
