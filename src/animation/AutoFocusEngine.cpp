#include "AutoFocusEngine.h"

#include <algorithm>
#include <cmath>

namespace Animation {
namespace {
double clampd(double value, double low, double high) {
    return std::max(low, std::min(high, value));
}

struct Box {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool empty = true;
    void add(double x, double y) {
        if (empty) {
            minX = maxX = x;
            minY = maxY = y;
            empty = false;
            return;
        }
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
};

// Greedy bounding-box grouping. A point joins the current group while the
// resulting box stays within (groupW, groupH); otherwise it starts a new group.
std::vector<std::pair<Box, double>> groupEvents(
    const std::vector<const InputEvent *> &points, double groupW, double groupH) {
    std::vector<std::pair<Box, double>> groups; // box + first-event time
    for (const InputEvent *event : points) {
        if (groups.empty()) {
            Box box;
            box.add(event->x, event->y);
            groups.emplace_back(box, event->timeMs);
            continue;
        }
        Box candidate = groups.back().first;
        candidate.add(event->x, event->y);
        if (candidate.width() <= groupW && candidate.height() <= groupH) {
            groups.back().first = candidate;
        } else {
            Box box;
            box.add(event->x, event->y);
            groups.emplace_back(box, event->timeMs);
        }
    }
    return groups;
}
} // namespace

std::vector<ZoomRange> AutoFocusEngine::ranges(const std::vector<double> &clickTimesMs,
    double durationMs, double zoom, bool externalDevice) {
    return automaticZooms(clickTimesMs, durationMs, externalDevice, zoom);
}

FocusPoint AutoFocusEngine::focusAt(const ZoomRange &range, const std::vector<InputEvent> &events,
    double contentW, double contentH, double timeMs, double snapToEdgesRatio) {
    std::vector<const InputEvent *> inside;
    const InputEvent *before = nullptr;
    const InputEvent *after = nullptr;
    for (const InputEvent &event : events) {
        if (event.timeMs < range.startMs) {
            before = &event;
            continue;
        }
        if (event.timeMs > range.endMs) {
            if (!after)
                after = &event;
            continue;
        }
        inside.push_back(&event);
    }

    if (inside.empty()) {
        const InputEvent *fallback = before ? before : after;
        if (!fallback)
            return {0.5, 0.5};
        return {contentW > 0 ? clampd(fallback->x / contentW, 0.0, 1.0) : 0.5,
            contentH > 0 ? clampd(fallback->y / contentH, 0.0, 1.0) : 0.5};
    }

    const double groupW = (contentW / std::max(0.01, range.zoom)) * 0.5;
    const double groupH = (contentH / std::max(0.01, range.zoom)) * 0.7;
    const auto groups = groupEvents(inside, groupW, groupH);

    const Box *chosen = &groups.front().first;
    for (const auto &entry : groups) {
        if (entry.second <= timeMs)
            chosen = &entry.first;
        else
            break;
    }

    (void)snapToEdgesRatio;
    const double cx = (chosen->minX + chosen->maxX) / 2.0;
    const double cy = (chosen->minY + chosen->maxY) / 2.0;
    return {contentW > 0 ? clampd(cx / contentW, 0.0, 1.0) : 0.5,
        contentH > 0 ? clampd(cy / contentH, 0.0, 1.0) : 0.5};
}

ScreenTransform AutoFocusEngine::transformFor(const FocusPoint &focus, double zoom,
    double contentW, double contentH, double canvasW, double canvasH, double snapToEdgesRatio) {
    ScreenTransform transform;
    transform.scale = std::max(0.01, zoom);

    const double span = std::max(0.0001, 1.0 - 2.0 * snapToEdgesRatio + 0.0001);
    const double fx = clampd((focus.x - snapToEdgesRatio) / span, 0.0, 1.0);
    const double fy = clampd((focus.y - snapToEdgesRatio) / span, 0.0, 1.0);

    const double scaledW = contentW * transform.scale;
    const double scaledH = contentH * transform.scale;

    // Focus under canvas centre, then clamp so content covers the canvas.
    transform.offsetX = clampd(canvasW / 2.0 - fx * scaledW, canvasW - scaledW, 0.0);
    transform.offsetY = clampd(canvasH / 2.0 - fy * scaledH, canvasH - scaledH, 0.0);
    return transform;
}

} // namespace Animation
