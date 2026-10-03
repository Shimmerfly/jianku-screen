#pragma once
#include <vector>

namespace Animation {
struct ZoomRange { double startMs; double endMs; double zoom = 2; };
// Input order is significant. See research/官方3.7.5自动焦点初稿.md.
std::vector<ZoomRange> automaticZooms(const std::vector<double> &clickTimesMs,
    double durationMs, bool externalDevice = false, double zoom = 2);
}
