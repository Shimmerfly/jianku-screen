// Regression tests for the recording media clock.
//
// Fixture values are the real timestamps of the recording that produced a
// moov-less MP4
// (`~/Movies/Jianku Screen/Jianku Screen 2026-10-03 23-27-15-049.jianku`):
// record at 23:27:26, click, pause at 23:27:46, resume at 00:03:44, stop at
// 00:03:49. The old code recomputed the newest frame's media time at stop time,
// subtracted the whole 1898.5 s pause a second time and handed the writer a PTS
// 13 ms *behind* the frame already in the file; AVAssetWriter then failed with
// NSCocoaErrorDomain 11800 and no moov atom was ever written.
#include "../src/capture/MediaClock.h"

#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

// Anchors taken verbatim from the damaged project.
constexpr qint64 kZeroHostNs = 398892113232791;      // mediaZeroHostTimeNs
constexpr qint64 kLastFrameDisplayNs = 398960981812625;
constexpr qint64 kPauseStartNs = 398960985356833;
constexpr qint64 kResumeNs = 400859412479125;
// The user pressed stop 92 ms after resume — while the receiver's pause was
// still open, which is exactly why the paused branch ran.
constexpr qint64 kStopWhilePausedNs = 400859504443208;
// A hypothetical stop five seconds after resuming.
constexpr qint64 kStopAfterResumeNs = kResumeNs + 5000000000LL;
constexpr qint64 kFrameNs = 16666667;

// The PTS the old expression produced, kept as the negative control.
qint64 regressedFinalPtsNs() {
    const qint64 pausedTotal = kStopWhilePausedNs - kPauseStartNs;
    const qint64 lastMedia = Capture::mediaTimeAt(kLastFrameDisplayNs, kZeroHostNs, pausedTotal);
    const qint64 liveNs = kStopWhilePausedNs - kLastFrameDisplayNs;
    return lastMedia + liveNs - kFrameNs;
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const qint64 pausedTotal = kStopWhilePausedNs - kPauseStartNs;
        const qint64 lastFrameMedia = Capture::mediaTimeAt(kLastFrameDisplayNs, kZeroHostNs, 0);
        const qint64 lastFrameFoldedHost = Capture::foldedHost(kLastFrameDisplayNs, 0);

        // The fixture must reproduce the recorded project exactly.
        require(lastFrameMedia == 68868579834, "last written frame media time matches project.json");
        require(pausedTotal == 1898519086375, "folded pause matches project.json");
        require(regressedFinalPtsNs() == 68855457375,
            "negative control reproduces the repeatedAtStop PTS in video-frames.jsonl");
        require(regressedFinalPtsNs() < lastFrameMedia,
            "the old expression moved the final PTS backwards");

        // What actually happened: stop while the receiver's pause was open.
        const qint64 pausedTail = Capture::tailNsAtStop(kStopWhilePausedNs, pausedTotal,
            lastFrameFoldedHost, true);
        require(pausedTail == 0, "stopping while paused appends no tail");
        require(!(Capture::finalFrameMediaNs(lastFrameMedia, pausedTail, kFrameNs) > lastFrameMedia),
            "paused stop cannot move the last PTS forward");
        const qint64 pausedDuration = lastFrameMedia + pausedTail;
        require(pausedDuration == lastFrameMedia,
            "metadata duration stays at the last real frame");

        // Stopping after a real resume: the tail is measured on the folded clock
        // and always lands one frame after the last written frame.
        const qint64 resumedTail = Capture::tailNsAtStop(kStopAfterResumeNs, pausedTotal,
            lastFrameFoldedHost, false);
        const qint64 resumedFinal = Capture::finalFrameMediaNs(lastFrameMedia, resumedTail, kFrameNs);
        require(resumedTail == kStopAfterResumeNs - pausedTotal - lastFrameFoldedHost,
            "tail is measured on the folded host clock");
        require(resumedFinal > lastFrameMedia, "resumed stop keeps PTS strictly increasing");
        require(resumedFinal == lastFrameMedia + resumedTail - kFrameNs,
            "final PTS is one frame before the end");

        // Folded host time must never run past the raw stop time.
        require(Capture::foldedHost(kStopAfterResumeNs, pausedTotal) <= kStopAfterResumeNs,
            "folding only moves time left");

        // Frames arriving after a completed pause keep positive media time.
        require(Capture::mediaTimeAt(kResumeNs, kZeroHostNs, pausedTotal) > 0,
            "post-pause frames keep positive media time");

        // A tail shorter than one frame must not produce a new PTS at all.
        const qint64 tinyTail = 1000000; // 1 ms
        require(!(Capture::finalFrameMediaNs(lastFrameMedia, tinyTail, kFrameNs) > lastFrameMedia),
            "sub-frame tail is rejected instead of appended");

        std::cout << "recording media clock checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
