#include "TimelineGeometry.h"

#include <algorithm>
#include <cmath>

namespace Render {
namespace {

// The candidate steps a ruler may use, in milliseconds: 1, 2, 5 × a power of ten
// seconds. A ruler that only used powers of ten would jump from "every 10 s" to
// "every 100 s" and leave long timelines almost unlabelled; allowing 2 and 5 keeps
// the spacing close to the requested minimum.
constexpr double kStepBases[] = {1.0, 2.0, 5.0};

} // namespace

double TimelineGeometry::ratioForTime(double timeMs, double durationMs) {
    if (!(durationMs > 0.0))
        return 0.0;
    return std::clamp(timeMs / durationMs, 0.0, 1.0);
}

double TimelineGeometry::timeAtRatio(double ratio, double durationMs) {
    if (!(durationMs > 0.0))
        return 0.0;
    return std::clamp(ratio, 0.0, 1.0) * durationMs;
}

double TimelineGeometry::snapToFrame(double timeMs, double frameDurationMs) {
    if (!(frameDurationMs > 0.0))
        return timeMs;
    return std::round(timeMs / frameDurationMs) * frameDurationMs;
}

double TimelineGeometry::snapToNearest(double timeMs, const std::vector<double> &candidates,
    double toleranceMs) {
    double best = timeMs;
    double bestDistance = toleranceMs;
    bool found = false;
    for (const double candidate : candidates) {
        const double distance = std::abs(candidate - timeMs);
        // The tolerance is inclusive — "within 2 px" should include exactly 2 px.
        if (distance > toleranceMs)
            continue;
        // A tie resolves to the earlier time rather than to whichever came first in
        // the list: the same drag must land on the same cut however the caller
        // happened to order them.
        if (!found || distance < bestDistance
            || (distance == bestDistance && candidate < best)) {
            best = candidate;
            bestDistance = distance;
            found = true;
        }
    }
    return best;
}

double TimelineGeometry::niceTickStepMs(double durationMs, double pixelWidth, double minimumPixels) {
    if (!(durationMs > 0.0) || !(pixelWidth > 0.0) || !(minimumPixels > 0.0))
        return 0.0;
    // One pixel stands for this much time.
    const double msPerPixel = durationMs / pixelWidth;
    const double minimumStep = msPerPixel * minimumPixels;
    for (int exponent = -3; exponent <= 6; ++exponent) {
        const double scale = std::pow(10.0, exponent) * 1000.0;
        for (const double base : kStepBases) {
            const double step = base * scale;
            if (step >= minimumStep)
                return step;
        }
    }
    return 0.0;
}

std::vector<double> TimelineGeometry::tickTimes(double durationMs, double stepMs) {
    std::vector<double> times;
    if (!(durationMs > 0.0) || !(stepMs > 0.0))
        return times;
    // A guard against a caller that asks for a step far smaller than the timeline:
    // the vector would grow without bound and freeze the UI thread.
    const double count = std::floor(durationMs / stepMs) + 1.0;
    if (count > 100000.0)
        return times;
    for (int index = 0; index < static_cast<int>(count); ++index)
        times.push_back(index * stepMs);
    // The end is a tick even when the step does not divide the duration, otherwise
    // the last label can be missing entirely on an odd-length recording.
    if (times.empty() || std::abs(times.back() - durationMs) > 1e-6)
        times.push_back(durationMs);
    return times;
}

QString TimelineGeometry::tickLabel(double timeMs, double stepMs) {
    const double seconds = timeMs / 1000.0;
    // Show a decimal only when the step is finer than a second, so a long timeline
    // is not covered in ".0".
    const bool needsDecimal = stepMs < 1000.0;
    const bool needsTwoDecimals = stepMs < 100.0;
    const int decimals = needsTwoDecimals ? 2 : (needsDecimal ? 1 : 0);
    if (seconds < 60.0)
        return QString::number(seconds, 'f', decimals) + QStringLiteral("s");
    const int minutes = static_cast<int>(seconds) / 60;
    const double remainder = seconds - minutes * 60;
    const QString rest = QString::number(remainder, 'f', decimals);
    // "3:05" reads better than "3:5".
    const QString padded = remainder < 10.0 ? QStringLiteral("0") + rest : rest;
    return QString::number(minutes) + QLatin1Char(':') + padded;
}

} // namespace Render
