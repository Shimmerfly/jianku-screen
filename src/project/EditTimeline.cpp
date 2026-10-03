#include "EditTimeline.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace Project {
namespace {

// Output times that land exactly on a boundary are common (a frame clock and a
// segment edge coincide), and floating point makes "exactly" unreliable. This is
// the tolerance used for every boundary comparison in this file.
constexpr double kEpsilon = 1e-6;

} // namespace

EditTimeline EditTimeline::whole(double durationMs) {
    EditTimeline timeline;
    timeline.sourceDurationMs_ = std::max(0.0, durationMs);
    if (timeline.sourceDurationMs_ > 0.0)
        timeline.segments_.push_back(Segment{0.0, timeline.sourceDurationMs_, 1.0});
    timeline.revalidate();
    return timeline;
}

void EditTimeline::revalidate() {
    valid_ = false;
    error_.clear();
    if (segments_.empty()) {
        error_ = QStringLiteral("时间线为空");
        return;
    }
    if (!(sourceDurationMs_ > 0.0)) {
        error_ = QStringLiteral("录制时长无效");
        return;
    }
    double expectedSource = 0.0;
    for (const Segment &segment : segments_) {
        if (!(segment.speed > 0.0)) {
            error_ = QStringLiteral("播放速度必须大于 0");
            return;
        }
        if (!(segment.sourceEndMs > segment.sourceStartMs)) {
            error_ = QStringLiteral("片段长度必须大于 0");
            return;
        }
        if (segment.sourceStartMs < -kEpsilon || segment.sourceEndMs > sourceDurationMs_ + kEpsilon) {
            error_ = QStringLiteral("片段超出录制范围");
            return;
        }
        // Segments are laid out in recording order. A cut-away removes media, it
        // never reorders it, so a backwards jump would mean the trim operations
        // produced something that cannot be played.
        if (segment.sourceStartMs < expectedSource - kEpsilon) {
            error_ = QStringLiteral("片段顺序与录制顺序不一致");
            return;
        }
        expectedSource = segment.sourceEndMs;
    }
    valid_ = true;
}

bool EditTimeline::isIdentity() const {
    return valid_ && segments_.size() == 1
        && std::abs(segments_.front().sourceStartMs) <= kEpsilon
        && std::abs(segments_.front().sourceEndMs - sourceDurationMs_) <= kEpsilon
        && std::abs(segments_.front().speed - 1.0) <= kEpsilon;
}

double EditTimeline::outputDurationMs() const {
    double total = 0.0;
    for (const Segment &segment : segments_)
        total += segment.outputLengthMs();
    return total;
}

int EditTimeline::segmentIndexAtOutput(double outputMs) const {
    if (segments_.empty())
        return -1;
    if (outputMs <= 0.0)
        return 0;
    double elapsed = 0.0;
    for (size_t index = 0; index < segments_.size(); ++index) {
        const double length = segments_[index].outputLengthMs();
        if (outputMs < elapsed + length - kEpsilon)
            return static_cast<int>(index);
        // The very end belongs to the last segment, not to "nothing".
        if (index + 1 == segments_.size())
            return static_cast<int>(index);
        elapsed += length;
    }
    return static_cast<int>(segments_.size()) - 1;
}

int EditTimeline::segmentIndexAtSource(double sourceMs) const {
    if (segments_.empty())
        return -1;
    for (size_t index = 0; index < segments_.size(); ++index) {
        const Segment &segment = segments_[index];
        if (sourceMs < segment.sourceEndMs - kEpsilon)
            return static_cast<int>(index);
        if (index + 1 == segments_.size())
            return static_cast<int>(index);
    }
    return static_cast<int>(segments_.size()) - 1;
}

double EditTimeline::sourceTimeAt(double outputMs) const {
    if (!valid_)
        return 0.0;
    const int index = segmentIndexAtOutput(outputMs);
    if (index < 0)
        return 0.0;
    double elapsed = 0.0;
    for (int i = 0; i < index; ++i)
        elapsed += segments_[i].outputLengthMs();
    const Segment &segment = segments_[index];
    const double into = std::clamp(outputMs - elapsed, 0.0, segment.outputLengthMs());
    return std::clamp(segment.sourceStartMs + into * segment.speed,
        segment.sourceStartMs, segment.sourceEndMs);
}

double EditTimeline::outputTimeAt(double sourceMs) const {
    if (!valid_)
        return 0.0;
    const int index = segmentIndexAtSource(sourceMs);
    if (index < 0)
        return 0.0;
    double elapsed = 0.0;
    for (int i = 0; i < index; ++i)
        elapsed += segments_[i].outputLengthMs();
    const Segment &segment = segments_[index];
    const double source = std::clamp(sourceMs, segment.sourceStartMs, segment.sourceEndMs);
    return elapsed + (source - segment.sourceStartMs) / segment.speed;
}

double EditTimeline::rateAt(double outputMs) const {
    if (!valid_)
        return 1.0;
    const int index = segmentIndexAtOutput(outputMs);
    return index < 0 ? 1.0 : segments_[index].speed;
}

double EditTimeline::rateAtSource(double sourceMs) const {
    if (!valid_)
        return 1.0;
    const int index = segmentIndexAtSource(sourceMs);
    return index < 0 ? 1.0 : segments_[index].speed;
}

bool EditTimeline::splitInternal(double outputMs) {
    const int index = segmentIndexAtOutput(outputMs);
    if (index < 0)
        return false;
    const Segment &segment = segments_[index];
    double elapsed = 0.0;
    for (int i = 0; i < index; ++i)
        elapsed += segments_[i].outputLengthMs();
    const double into = outputMs - elapsed;
    // Nothing to split at either end: the time is already a boundary.
    if (into <= kEpsilon || into >= segment.outputLengthMs() - kEpsilon)
        return false;
    const double sourceSplit = segment.sourceStartMs + into * segment.speed;
    Segment left{segment.sourceStartMs, sourceSplit, segment.speed};
    Segment right{sourceSplit, segment.sourceEndMs, segment.speed};
    segments_[index] = left;
    segments_.insert(segments_.begin() + index + 1, right);
    return true;
}

bool EditTimeline::split(double outputMs) {
    if (!valid_)
        return false;
    // A split has to leave a usable piece on both sides; splitting exactly on an
    // existing boundary is a no-op, not an error, so it reports success.
    const int index = segmentIndexAtOutput(outputMs);
    if (index < 0)
        return false;
    splitInternal(outputMs);
    return true;
}

// Drops every segment whose output span lies entirely inside [from, to] and keeps
// the rest. `from` and `to` are expected to be boundaries already (the callers
// split first), so this never has to slice a segment in half.
bool EditTimeline::dropRange(double from, double to) {
    std::vector<Segment> kept;
    double consumed = 0.0;
    for (const Segment &segment : segments_) {
        const double start = consumed;
        const double end = consumed + segment.outputLengthMs();
        consumed = end;
        if (start >= from - kEpsilon && end <= to + kEpsilon)
            continue;
        kept.push_back(segment);
    }
    if (kept.empty())
        return false;
    segments_ = std::move(kept);
    return true;
}

bool EditTimeline::trimStart(double outputMs, double minimumMs) {
    if (!valid_)
        return false;
    const double total = outputDurationMs();
    if (outputMs <= kEpsilon)
        return true;
    // Never leave less than a usable amount of timeline: a zero-length timeline
    // has no meaningful frame count and would divide by zero downstream.
    if (outputMs > total - minimumMs)
        return false;
    splitInternal(outputMs);
    if (!dropRange(0.0, outputMs))
        return false;
    revalidate();
    return valid_;
}

bool EditTimeline::trimEnd(double outputMs, double minimumMs) {
    if (!valid_)
        return false;
    const double total = outputDurationMs();
    if (outputMs >= total - kEpsilon)
        return true;
    if (outputMs < minimumMs)
        return false;
    splitInternal(outputMs);
    if (!dropRange(outputMs, total))
        return false;
    revalidate();
    return valid_;
}

bool EditTimeline::remove(double fromMs, double toMs, double minimumMs) {
    if (!valid_)
        return false;
    const double total = outputDurationMs();
    const double from = std::clamp(std::min(fromMs, toMs), 0.0, total);
    const double to = std::clamp(std::max(fromMs, toMs), 0.0, total);
    if (to - from <= kEpsilon)
        return false;
    if (total - (to - from) < minimumMs)
        return false;
    if (from <= kEpsilon) {
        // Removing from the head is a trim; keeping this in one place avoids two
        // subtly different implementations of the same operation.
        return trimStart(to, minimumMs);
    }
    if (to >= total - kEpsilon)
        return trimEnd(from, minimumMs);
    // Splitting does not move the output clock, so both boundaries can be cut
    // before anything is removed.
    splitInternal(from);
    splitInternal(to);
    if (!dropRange(from, to))
        return false;
    revalidate();
    return valid_;
}

bool EditTimeline::setSpeed(double fromMs, double toMs, double speed) {
    if (!valid_ || !(speed > 0.0))
        return false;
    const double total = outputDurationMs();
    const double from = std::clamp(std::min(fromMs, toMs), 0.0, total);
    const double to = std::clamp(std::max(fromMs, toMs), 0.0, total);
    if (to - from <= kEpsilon)
        return false;
    splitInternal(from);
    splitInternal(to);

    bool changed = false;
    double consumed = 0.0;
    for (Segment &segment : segments_) {
        const double start = consumed;
        const double end = consumed + segment.outputLengthMs();
        consumed = end;
        // Only a segment fully inside the range is retimed: one that merely
        // touches the boundary belongs to the surrounding real-time material.
        if (start >= from - kEpsilon && end <= to + kEpsilon) {
            segment.speed = speed;
            changed = true;
        }
    }
    if (!changed)
        return false;
    revalidate();
    return valid_;
}

QJsonObject EditTimeline::toJson() const {
    QJsonArray array;
    for (const Segment &segment : segments_) {
        array.append(QJsonObject{{QStringLiteral("sourceStartMs"), segment.sourceStartMs},
            {QStringLiteral("sourceEndMs"), segment.sourceEndMs},
            {QStringLiteral("speed"), segment.speed}});
    }
    return QJsonObject{{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("sourceDurationMs"), sourceDurationMs_},
        {QStringLiteral("segments"), array}};
}

EditTimeline EditTimeline::fromJson(const QJsonObject &object, double durationMs, QString *error) {
    EditTimeline timeline;
    timeline.sourceDurationMs_ = durationMs > 0.0
        ? durationMs : object.value(QStringLiteral("sourceDurationMs")).toDouble();
    const QJsonArray array = object.value(QStringLiteral("segments")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject entry = value.toObject();
        Segment segment;
        segment.sourceStartMs = entry.value(QStringLiteral("sourceStartMs")).toDouble();
        segment.sourceEndMs = entry.value(QStringLiteral("sourceEndMs")).toDouble();
        segment.speed = entry.value(QStringLiteral("speed")).toDouble(1.0);
        timeline.segments_.push_back(segment);
    }
    timeline.revalidate();
    if (!timeline.valid_ && error)
        *error = timeline.error_;
    return timeline;
}

} // namespace Project
