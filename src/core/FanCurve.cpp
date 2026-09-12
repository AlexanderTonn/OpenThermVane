#include "core/FanCurve.hpp"

#include <algorithm>

namespace thermvane {

FanCurve::FanCurve()
    : m_points(defaultPoints())
{
}

FanCurve::FanCurve(QList<FanCurvePoint> points)
{
    setPoints(std::move(points));
}

const QList<FanCurvePoint> &FanCurve::points() const
{
    return m_points;
}

void FanCurve::setPoints(QList<FanCurvePoint> points)
{
    std::sort(points.begin(), points.end(), [](const FanCurvePoint &left, const FanCurvePoint &right) {
        return left.temperature < right.temperature;
    });
    m_points = std::move(points);
}

double FanCurve::speedForTemperature(double temperature) const
{
    if (m_points.isEmpty()) {
        return 100.0;
    }

    if (temperature <= m_points.first().temperature) {
        return m_points.first().fanSpeed;
    }

    for (qsizetype i = 1; i < m_points.size(); ++i) {
        const auto &previous = m_points.at(i - 1);
        const auto &next = m_points.at(i);
        if (temperature <= next.temperature) {
            const double span = next.temperature - previous.temperature;
            if (span <= 0.0) {
                return next.fanSpeed;
            }
            const double ratio = (temperature - previous.temperature) / span;
            return previous.fanSpeed + ((next.fanSpeed - previous.fanSpeed) * ratio);
        }
    }

    return m_points.last().fanSpeed;
}

QList<FanCurvePoint> FanCurve::defaultPoints()
{
    return {
        {40.0, 20.0},
        {50.0, 30.0},
        {60.0, 45.0},
        {70.0, 65.0},
        {80.0, 85.0},
        {90.0, 100.0},
    };
}

} // namespace thermvane
