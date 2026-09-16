#pragma once

#include "hardware/IHardwareBackend.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QString>

namespace thermvane {

class WindowsHardwareBackend final : public IHardwareBackend
{
    Q_OBJECT

public:
    explicit WindowsHardwareBackend(QObject *parent = nullptr);

    struct WmiControlTarget
    {
        QString namespaceName;
        QString className;
        QString identifier;
    };

    void scan() override;
    QList<SensorInfo> sensors() const override;
    QList<FanInfo> fans() const override;
    bool setFanSpeed(const QString &fanId, double percent) override;
    bool restoreAutomaticControl(const QString &fanId) override;

private:
    void startNbfcService();
    void applyHardwareSnapshot(const QJsonObject &hardware);

    QList<SensorInfo> m_sensors;
    QList<FanInfo> m_fans;
    QHash<QString, WmiControlTarget> m_controlsByFanId;
    QHash<QString, int> m_nbfcFanIndexesByFanId;
    QHash<QString, double> m_pendingManualFanSpeeds;
    QString m_nbfcPath;
    QElapsedTimer m_lastScanTimer;
    bool m_scanRunning = false;
    bool m_nbfcStartAttempted = false;
};

} // namespace thermvane
