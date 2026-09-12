#pragma once

#include <QList>

namespace thermvane {

struct FanCurvePoint
{
    double temperature = 0.0;
    double fanSpeed = 0.0;
};

class FanCurve
{
public:
    FanCurve();
    explicit FanCurve(QList<FanCurvePoint> points);

    const QList<FanCurvePoint> &points() const;
    void setPoints(QList<FanCurvePoint> points);
    double speedForTemperature(double temperature) const;

    static QList<FanCurvePoint> defaultPoints();

private:
    QList<FanCurvePoint> m_points;
};

} // namespace thermvane
