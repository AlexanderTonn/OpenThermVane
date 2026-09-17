#pragma once

#include "hardware/HardwareTypes.hpp"

#include <QObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>

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
    bool setEmergencyFullSpeed(bool active);

signals:
    void fansChanged();

private:
    void refresh();
    void applyManualOverrides();
    bool applyEmergencySpeed();
    bool restoreEmergencyControlledFans();

    QPointer<IHardwareBackend> m_backend;
    QList<FanInfo> m_fans;
    QHash<QString, double> m_manualSpeedOverrides;
    QSet<QString> m_emergencyControlledFans;
    bool m_emergencyActive = false;
};

} // namespace thermvane
