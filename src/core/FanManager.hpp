#pragma once

#include "hardware/HardwareTypes.hpp"

#include <QObject>
#include <QList>
#include <QPointer>

namespace thermvane {

class IHardwareBackend;

class FanManager final : public QObject
{
    Q_OBJECT

public:
    explicit FanManager(QObject *parent = nullptr);

    void setBackend(IHardwareBackend *backend);
    QList<FanInfo> fans() const;

    Q_INVOKABLE bool setManualSpeed(const QString &fanId, double percent);
    Q_INVOKABLE bool restoreAutomaticControl(const QString &fanId);

signals:
    void fansChanged();

private:
    void refresh();

    QPointer<IHardwareBackend> m_backend;
    QList<FanInfo> m_fans;
};

} // namespace thermvane
