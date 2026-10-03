#pragma once

#include <QString>
#include <vector>

namespace Render {

// Arithmetic for drawing a timeline strip.
//
// This exists as a separate, testable unit because a timeline's geometry is where
// small errors become user-visible nonsense: a playhead that is one pixel off at
// the end, a ruler whose labels drift, a cut that snaps to a point where no frame
// exists. None of it is hard, all of it is easy to get subtly wrong, and none of it
// can be checked by looking at the screen.
//
// Conventions: time is in output milliseconds, positions are fractions of the
// strip (0 at its left edge, 1 at its right edge), and a frame is 1 / fps.
struct TimelineGeometry {
    // --- position <-> time ------------------------------------------------
    // Both clamp: a dragged playhead that leaves the strip must stop at the end
    // rather than produce a negative or past-the-end time.
    static double ratioForTime(double timeMs, double durationMs);
    static double timeAtRatio(double ratio, double durationMs);

    // --- snapping ---------------------------------------------------------
    // Rounds to the nearest frame boundary. Cutting between frames would produce a
    // segment whose source range no frame corresponds to, which makes the cut
    // impossible to reproduce by dragging the handle again.
    static double snapToFrame(double timeMs, double frameDurationMs);
    // Rounds to whichever of `candidates` is closest, but only when it is within
    // `toleranceMs`; otherwise returns the time unchanged. Used so a dragged handle
    // lands exactly on an existing cut instead of a frame next to it.
    static double snapToNearest(double timeMs, const std::vector<double> &candidates,
        double toleranceMs);

    // --- ruler ------------------------------------------------------------
    // A "nice" tick interval (1, 2, 5 × a power of ten seconds) such that ticks are
    // at least `minimumPixels` apart. Returns 0 when no interval qualifies, which
    // the caller should treat as "do not draw a ruler" rather than dividing by it.
    static double niceTickStepMs(double durationMs, double pixelWidth, double minimumPixels);

    // Tick times from 0 to `durationMs` inclusive, spaced by `stepMs`.
    // `stepMs <= 0` yields an empty list.
    static std::vector<double> tickTimes(double durationMs, double stepMs);

    // A tick label: seconds with a decimal only when the step needs one, so a
    // 10-minute timeline is not covered in ".000".
    static QString tickLabel(double timeMs, double stepMs);
};

} // namespace Render
