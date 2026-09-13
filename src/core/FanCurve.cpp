#include "core/FanCurve.hpp"

#include <algorithm>

namespace thermvane {

namespace {

double monotoneTangent(double previousSlope, double nextSlope)
{
    if (previousSlope * nextSlope <= 0.0) {
        return 0.0;
    }

    return 2.0 / ((1.0 / previousSlope) + (1.0 / nextSlope));
}

double hermite(double startValue, double endValue, double startTangent, double endTangent, double span, double ratio)
{
    const double ratio2 = ratio * ratio;
    const double ratio3 = ratio2 * ratio;
    const double h00 = (2.0 * ratio3) - (3.0 * ratio2) + 1.0;
    const double h10 = ratio3 - (2.0 * ratio2) + ratio;
    const double h01 = (-2.0 * ratio3) + (3.0 * ratio2);
    const double h11 = ratio3 - ratio2;
    return (h00 * startValue) + (h10 * span * startTangent)
        + (h01 * endValue) + (h11 * span * endTangent);
}

} // namespace

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
            const double segmentSlope = (next.fanSpeed - previous.fanSpeed) / span;
            double previousTangent = segmentSlope;
            double nextTangent = segmentSlope;

            if (i > 1) {
                const auto &beforePrevious = m_points.at(i - 2);
                const double previousSpan = previous.temperature - beforePrevious.temperature;
                if (previousSpan > 0.0) {
                    const double previousSlope = (previous.fanSpeed - beforePrevious.fanSpeed) / previousSpan;
                    previousTangent = monotoneTangent(previousSlope, segmentSlope);
                }
            }

            if (i + 1 < m_points.size()) {
                const auto &afterNext = m_points.at(i + 1);
                const double nextSpan = afterNext.temperature - next.temperature;
                if (nextSpan > 0.0) {
                    const double nextSlope = (afterNext.fanSpeed - next.fanSpeed) / nextSpan;
                    nextTangent = monotoneTangent(segmentSlope, nextSlope);
                }
            }

            return std::clamp(hermite(previous.fanSpeed, next.fanSpeed, previousTangent, nextTangent, span, ratio),
                              std::min(previous.fanSpeed, next.fanSpeed),
                              std::max(previous.fanSpeed, next.fanSpeed));
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
