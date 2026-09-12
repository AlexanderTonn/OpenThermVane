#pragma once

#include <QString>

namespace thermvane {

struct FanCapabilities
{
    bool rpmReading = false;
    bool manualControl = false;
    bool firmwareControl = false;
    bool zeroRpm = false;
    bool pwmControl = false;
};

struct FanInfo
{
    QString id;
    QString name;
    int rpm = 0;
    double speedPercent = 0.0;
    bool automatic = true;
    FanCapabilities capabilities;
};

struct SensorInfo
{
    QString id;
    QString name;
    double temperatureCelsius = 0.0;
    QString source;
    bool available = true;
};

} // namespace thermvane
