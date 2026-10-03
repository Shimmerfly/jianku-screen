#pragma once

#include <QtGlobal>
#include <algorithm>

namespace Capture {

// Pause folding for the recording media clock.
//
// The recorder writes frames on a media clock that has every completed pause
// folded out, while the source keeps reporting host-clock timestamps that never
// stop advancing. Two quantities therefore have to be tracked side by side:
//
//   * `pausedAccumNs`  — total host time already folded out at the current moment
//   * `lastFrameFoldedHostNs` — host time of the newest *written* frame, already
//     folded by the pauses that were complete when it was written
//
// Mixing the two is what produced a non-monotonic PTS (and a moov-less MP4) when
// a recording was stopped while paused: the tail was recomputed from the source
// clock with the newest pause subtracted a second time, landing *behind* the
// frame already in the file. Everything here is pure so the invariant is under
// test; see tests/MediaClockTests.cpp.

// Host time with every completed pause folded out.
inline qint64 foldedHost(qint64 hostNs, qint64 pausedAccumNs) {
    return hostNs - pausedAccumNs;
}

// Media time of a frame written at `hostNs`, given the pauses completed so far.
inline qint64 mediaTimeAt(qint64 hostNs, qint64 zeroHostNs, qint64 pausedAccumNs) {
    return hostNs - zeroHostNs - pausedAccumNs;
}

// Tail media time to append at stop so a static source still plays up to the
// moment the user pressed stop. Stopping while paused has no tail: the frozen
// frame is the last thing the source showed, and extending past it would append
// a PTS beyond the pause the user just cut out.
//
// The result is never negative, so `lastFrameMediaNs + tail` cannot move
// backwards relative to the last written frame.
inline qint64 tailNsAtStop(qint64 stopHostNs, qint64 pausedAccumNs,
    qint64 lastFrameFoldedHostNs, bool stoppedWhilePaused) {
    if (stoppedWhilePaused)
        return 0;
    return std::max<qint64>(0, foldedHost(stopHostNs, pausedAccumNs) - lastFrameFoldedHostNs);
}

// Final PTS handed to the writer for the repeated last frame, one frame before
// the end so the clip ends where the user stopped it.
inline qint64 finalFrameMediaNs(qint64 lastFrameMediaNs, qint64 tailNs, qint64 frameDurationNs) {
    return lastFrameMediaNs + tailNs - frameDurationNs;
}

} // namespace Capture
