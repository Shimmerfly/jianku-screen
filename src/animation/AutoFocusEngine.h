#pragma once

#include "AutomaticZoom.h"
#include "InputEvent.h"

#include <vector>

namespace Animation {

// Focus point normalised within the captured content (0..1).
struct FocusPoint {
    double x = 0.5;
    double y = 0.5;
};

// Maps content pixels into the output canvas.
struct ScreenTransform {
    double scale = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
};

// Auto-focus range generation plus camera framing. Grouping and framing follow
// the reference static research (research/桌面动画处理链-官方3.7.5静态.md §5) and
// are marked pending frame validation.
class AutoFocusEngine {
public:
    static std::vector<ZoomRange> ranges(const std::vector<double> &clickTimesMs,
        double durationMs, double zoom = 2.0, bool externalDevice = false);

    // Group the events inside the range with greedy bounding boxes and return the
    // centre of the group selected for `timeMs` (by first-event time).
    static FocusPoint focusAt(const ZoomRange &range, const std::vector<InputEvent> &events,
        double contentW, double contentH, double timeMs, double snapToEdgesRatio = 0.25);

    // Camera transform so the focus point lands under the canvas centre, clamped
    // so the zoomed content always covers the canvas.
    static ScreenTransform transformFor(const FocusPoint &focus, double zoom,
        double contentW, double contentH, double canvasW, double canvasH,
        double snapToEdgesRatio = 0.25);
};

} // namespace Animation
