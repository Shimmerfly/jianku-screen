#include "AutomaticZoom.h"
#include <algorithm>
#include <cmath>

namespace Animation {
std::vector<ZoomRange> automaticZooms(const std::vector<double> &clickTimesMs,
    double durationMs, bool externalDevice, double zoom) {
    if (externalDevice || !std::isfinite(durationMs) || durationMs <= 0) return {};
    std::vector<ZoomRange> result;
    for (double time : clickTimesMs) {
        if (!std::isfinite(time) || !(time < durationMs - 1000)) continue;
        const double integerTime = std::floor(time);
        result.push_back({std::max(integerTime - 300, 1.0),
            std::min(integerTime + 2500, durationMs - 800), zoom});
    }
    // Preserve the two passes and repeated reduction, including replacement of
    // the previous end by the next end. This also defines unsorted input behavior.
    for (;;) {
        const auto oldCount = result.size();
        for (double gap : {1.0, 2500.0}) {
            std::vector<ZoomRange> merged;
            for (auto range : result) {
                const bool joins = !merged.empty() && (gap == 1
                    ? merged.back().endMs + 1 == range.startMs
                    : merged.back().endMs + gap >= range.startMs);
                if (joins) merged.back().endMs = range.endMs;
                else merged.push_back(range);
            }
            result = std::move(merged);
        }
        if (result.size() == oldCount) break;
    }
    for (auto &range : result) {
        range.startMs = std::floor(range.startMs);
        range.endMs = std::floor(range.endMs);
    }
    return result;
}
}
