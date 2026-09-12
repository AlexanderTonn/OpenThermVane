#pragma once

#include "core/FanCurve.hpp"

#include <QAbstractListModel>
#include <QVariantList>

namespace thermvane {

class FanCurveModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        TemperatureRole = Qt::UserRole + 1,
        SpeedRole,
    };

    explicit FanCurveModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void movePoint(int row, double temperature, double speed);
    Q_INVOKABLE double speedForTemperature(double temperature) const;
    Q_INVOKABLE QVariantList points() const;

private:
    FanCurve m_curve;
};

} // namespace thermvane
