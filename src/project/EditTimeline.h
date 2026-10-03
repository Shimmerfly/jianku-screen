#pragma once

#include <QJsonObject>
#include <QString>
#include <vector>

namespace Project {

// A non-destructive edit timeline.
//
// The recording is never modified: this only describes how output time maps onto
// the media time that was captured. The product docs already require this shape
// (docs/双模式产品范围.md, 工程应保留什么): segments carry a source range and a
// speed, and every consumer — picture, pointer, camera, audio — has to use the
// same mapping, or a sped-up clip ends up with its clicks in the wrong places.
//
// One rule everything else follows from: the output timeline is a concatenation
// of segments, each playing `sourceEnd - sourceStart` of media in
// `(sourceEnd - sourceStart) / speed` of output time. Mapping is continuous and
// strictly increasing, which is what lets the animation engines be stepped in
// source time while the frame clock runs in output time.
struct Segment {
    double sourceStartMs = 0.0;
    double sourceEndMs = 0.0;
    double speed = 1.0;

    double sourceLengthMs() const { return sourceEndMs - sourceStartMs; }
    double outputLengthMs() const {
        return speed > 0.0 ? sourceLengthMs() / speed : 0.0;
    }
};

class EditTimeline {
public:
    // The identity timeline: the whole recording, real time.
    static EditTimeline whole(double durationMs);

    // Rejects out-of-range source spans, non-positive speeds, gaps and overlaps.
    // A timeline that fails this must not be used for an export.
    bool valid() const { return valid_; }
    QString error() const { return error_; }
    double sourceDurationMs() const { return sourceDurationMs_; }
    const std::vector<Segment> &segments() const { return segments_; }
    bool isIdentity() const;

    // Total output length: the sum of every segment's output length.
    double outputDurationMs() const;

    // Output time -> media time. Clamped at both ends; times between segments
    // cannot occur because the segments are contiguous in output time.
    double sourceTimeAt(double outputMs) const;
    // Media time -> output time. A media time inside a cut-away region maps to the
    // start of the next segment, so dragging in the timeline cannot land in a hole.
    double outputTimeAt(double sourceMs) const;

    // How fast media time advances per output millisecond at this output time:
    // the segment's speed. The animation engines and the motion blur both need it,
    // because a 2× clip has to move the camera twice as fast per output frame.
    double rateAt(double outputMs) const;
    // The same for a media time (the inverse mapping's slope).
    double rateAtSource(double sourceMs) const;

    // --- editing ----------------------------------------------------------
    // All of these keep the timeline valid or leave it untouched and return false;
    // a failed edit never leaves a half-applied state.
    //
    // Trimming to at least `minimumMs` of output is enforced: a zero-length
    // timeline has no meaningful frame count and would divide by zero downstream.
    bool trimStart(double outputMs, double minimumMs = 100.0);
    bool trimEnd(double outputMs, double minimumMs = 100.0);
    // Splits the segment containing `outputMs` in two, at the same media time.
    bool split(double outputMs);
    // Removes an output-time span, splicing the remainder together.
    bool remove(double fromMs, double toMs, double minimumMs = 100.0);
    // Sets the speed of every segment overlapping the output-time span, splitting
    // segments at the boundaries first so the range is exact.
    bool setSpeed(double fromMs, double toMs, double speed);

    QJsonObject toJson() const;
    // `durationMs` is the recording's own length, used to validate the source
    // ranges. An unreadable or invalid document reports why instead of silently
    // exporting the whole recording.
    static EditTimeline fromJson(const QJsonObject &object, double durationMs, QString *error);

private:
    void revalidate();
    // Index of the segment whose output span contains `outputMs` (the last one
    // when the time is exactly the end).
    int segmentIndexAtOutput(double outputMs) const;
    int segmentIndexAtSource(double sourceMs) const;
    // Splits at an output time without the "must leave minimum on both sides" rule
    // that `split` applies; used internally by the range operations.
    bool splitInternal(double outputMs);
    // Drops segments lying entirely inside [from, to]; both must be boundaries.
    bool dropRange(double from, double to);

    std::vector<Segment> segments_;
    double sourceDurationMs_ = 0.0;
    bool valid_ = false;
    QString error_;
};

} // namespace Project
