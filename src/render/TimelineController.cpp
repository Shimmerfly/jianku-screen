#include "TimelineController.h"

#include "../project/EditSidecar.h"
#include "ProjectCompositor.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace Render {
namespace {

// Bounded so a long editing session cannot grow memory without limit. Deep enough
// that no realistic run of edits hits it.
constexpr size_t kMaxHistory = 100;

} // namespace

TimelineController::TimelineController(QObject *parent) : QObject(parent) {}

QVariantList TimelineController::segments() const {
    QVariantList list;
    if (!timeline_.valid())
        return list;
    double outputStart = 0.0;
    for (const Project::Segment &segment : timeline_.segments()) {
        const double outputLength = segment.outputLengthMs();
        list.append(QVariantMap{
            {QStringLiteral("outputStartMs"), outputStart},
            {QStringLiteral("outputEndMs"), outputStart + outputLength},
            {QStringLiteral("sourceStartMs"), segment.sourceStartMs},
            {QStringLiteral("sourceEndMs"), segment.sourceEndMs},
            {QStringLiteral("speed"), segment.speed},
            // Fractions of the total output, so the strip can be laid out without
            // knowing the pixel width.
            {QStringLiteral("startRatio"), outputStart / std::max(1.0, outputDurationMs())},
            {QStringLiteral("lengthRatio"), outputLength / std::max(1.0, outputDurationMs())},
            {QStringLiteral("retimed"), std::abs(segment.speed - 1.0) > 1e-6}});
        outputStart += outputLength;
    }
    return list;
}

double TimelineController::outputRatio() const {
    const double source = timeline_.sourceDurationMs();
    return source > 0.0 ? timeline_.outputDurationMs() / source : 1.0;
}

double TimelineController::playheadRatio() const {
    return TimelineGeometry::ratioForTime(playheadMs_, timeline_.outputDurationMs());
}

QString TimelineController::playheadLabel() const {
    return TimelineGeometry::tickLabel(playheadMs_, 10.0);
}

QVariantList TimelineController::rulerTicks() const {
    QVariantList ticks;
    const double duration = timeline_.outputDurationMs();
    if (!loaded_ || duration <= 0.0)
        return ticks;
    // The strip is a fixed-width widget, so a nominal width is used for the spacing
    // rule; the QML side lays the ticks out by ratio, so the exact width only
    // affects how many there are.
    const double step = TimelineGeometry::niceTickStepMs(duration, 900.0, 70.0);
    for (const double time : TimelineGeometry::tickTimes(duration, step)) {
        ticks.append(QVariantMap{
            {QStringLiteral("timeMs"), time},
            {QStringLiteral("ratio"), TimelineGeometry::ratioForTime(time, duration)},
            {QStringLiteral("label"), TimelineGeometry::tickLabel(time, step)}});
    }
    return ticks;
}

void TimelineController::setFrameRate(double fps) {
    const double next = fps > 0.0 ? 1000.0 / fps : 0.0;
    if (std::abs(next - frameDurationMs_) <= 1e-9)
        return;
    frameDurationMs_ = next;
    emit playheadChanged();
}

void TimelineController::setPlayheadMs(double ms) {
    const double clamped = std::clamp(ms, 0.0, std::max(0.0, timeline_.outputDurationMs()));
    if (std::abs(clamped - playheadMs_) <= 1e-9)
        return;
    playheadMs_ = clamped;
    emit playheadChanged();
}

void TimelineController::setPlayheadRatio(double ratio) {
    setPlayheadMs(TimelineGeometry::timeAtRatio(ratio, timeline_.outputDurationMs()));
}

double TimelineController::snappedPlayheadMs() const {
    if (!timeline_.valid())
        return 0.0;
    // Clamped after snapping too: rounding up at the very end would put the
    // playhead one frame past the last frame, where an operation is refused.
    const double snapped = TimelineGeometry::snapToFrame(playheadMs_, frameDurationMs_);
    return std::clamp(snapped, 0.0, timeline_.outputDurationMs());
}

bool TimelineController::splitAtPlayhead() {
    if (!timeline_.valid())
        return false;
    const double at = snappedPlayheadMs();
    // A split exactly at either end would create an empty segment, which the model
    // refuses; saying so here is friendlier than a generic failure.
    if (at <= 0.0 || at >= timeline_.outputDurationMs()) {
        setError(QStringLiteral("请把播放头移到片段中间再切分"));
        return false;
    }
    return splitAt(at);
}

bool TimelineController::removeAroundPlayhead(double spanMs) {
    if (!timeline_.valid())
        return false;
    if (!(spanMs > 0.0)) {
        setError(QStringLiteral("删除长度必须大于 0"));
        return false;
    }
    const double centre = snappedPlayheadMs();
    const double from = std::max(0.0, centre - spanMs / 2.0);
    const double to = std::min(timeline_.outputDurationMs(), centre + spanMs / 2.0);
    if (to - from <= 1e-6) {
        setError(QStringLiteral("请把播放头移到要删除的位置"));
        return false;
    }
    return removeRange(from, to);
}

bool TimelineController::trimStartTo(double outputMs) {
    if (!timeline_.valid())
        return false;
    const double snapped = TimelineGeometry::snapToFrame(
        std::max(0.0, std::min(outputMs, timeline_.outputDurationMs())), frameDurationMs_);
    if (trimStart(snapped))
        return true;
    // A refused trim is usually the minimum-length rule, which is worth saying
    // plainly: "nothing happened" with no reason reads as a broken handle.
    setError(QStringLiteral("裁剪后至少要保留 %1 毫秒").arg(100));
    return false;
}

bool TimelineController::trimEndTo(double outputMs) {
    if (!timeline_.valid())
        return false;
    const double snapped = TimelineGeometry::snapToFrame(
        std::max(0.0, std::min(outputMs, timeline_.outputDurationMs())), frameDurationMs_);
    if (trimEnd(snapped))
        return true;
    setError(QStringLiteral("裁剪后至少要保留 %1 毫秒").arg(100));
    return false;
}

bool TimelineController::setSpeedAtPlayhead(double rate) {
    if (!timeline_.valid())
        return false;
    if (!(rate > 0.0)) {
        setError(QStringLiteral("播放速度必须大于 0"));
        return false;
    }
    const double at = snappedPlayheadMs();
    const double duration = timeline_.outputDurationMs();
    // The whole segment the playhead is inside, found from the segment list so the
    // range matches exactly what the strip draws. Using a nominal span instead would
    // leave a sliver at real time next to the retimed part.
    double start = 0.0;
    double end = duration;
    for (const QVariant &value : segments()) {
        const QVariantMap segment = value.toMap();
        const double segmentStart = segment.value(QStringLiteral("outputStartMs")).toDouble();
        const double segmentEnd = segment.value(QStringLiteral("outputEndMs")).toDouble();
        // Half-open, with the very end belonging to the last segment: at the end of
        // the timeline the playhead must still retime something.
        if (at >= segmentStart - 1e-6 && (at < segmentEnd - 1e-6 || segmentEnd >= duration - 1e-6)) {
            start = segmentStart;
            end = segmentEnd;
            break;
        }
    }
    if (end - start <= 1e-6) {
        setError(QStringLiteral("播放头所在的片段长度为零，无法变速"));
        return false;
    }
    return setSpeed(start, end, rate);
}

void TimelineController::clampPlayhead() {
    const double limit = std::max(0.0, timeline_.outputDurationMs());
    const double clamped = std::clamp(playheadMs_, 0.0, limit);
    if (std::abs(clamped - playheadMs_) <= 1e-9)
        return;
    playheadMs_ = clamped;
    emit playheadChanged();
}

void TimelineController::setError(const QString &message) {
    if (error_ == message)
        return;
    error_ = message;
    emit errorChanged();
}

bool TimelineController::load(const QString &projectDirectory) {
    if (projectDirectory.isEmpty()) {
        clear();
        return false;
    }
    if (!QFileInfo::exists(projectDirectory + QStringLiteral("/project.json"))) {
        clear();
        // An empty path is "nothing to edit"; a path that looks like a project but
        // has no manifest is a problem worth reporting.
        setError(QStringLiteral("找不到录制工程清单：") + projectDirectory);
        return false;
    }
    QString loadError;
    const ProjectData project = loadProject(projectDirectory, &loadError);
    if (!project.valid) {
        clear();
        setError(QStringLiteral("无法读取录制工程：") + loadError);
        return false;
    }
    projectDirectory_ = projectDirectory;
    projectCreatedAt_ = project.createdAt;
    timeline_ = Project::EditTimeline::whole(project.durationMs);
    undo_.clear();
    redo_.clear();
    loaded_ = timeline_.valid();
    setError(loaded_ ? QString() : timeline_.error());
    playheadMs_ = 0.0;

    // A saved edit is applied on top of the whole-recording timeline. Until this
    // existed, `load()` reset every split, trim and speed change, so closing the app
    // silently threw the edit away — and `EditTimeline::toJson()`/`fromJson()` had no
    // callers at all.
    const Project::EditSidecar sidecar = Project::load(projectDirectory, project.durationMs);
    if (sidecar.valid && sidecar.timeline.valid()) {
        // A sidecar from a different recording that happens to reuse the directory
        // name is refused rather than applied to the wrong film.
        const bool sameProject = sidecar.projectCreatedAt.isEmpty()
            || projectCreatedAt_.isEmpty()
            || sidecar.projectCreatedAt == projectCreatedAt_;
        if (sameProject) {
            timeline_ = sidecar.timeline;
            playheadMs_ = std::clamp(sidecar.playheadMs, 0.0, timeline_.outputDurationMs());
        } else {
            setError(QStringLiteral("编辑记录属于另一个工程，已忽略"));
        }
    } else if (!sidecar.error.isEmpty()) {
        // Reported, not fatal: the recording still opens, with the edit ignored.
        setError(sidecar.error);
    }
    // The baseline is what is on disk now, so a freshly opened project is clean even
    // though its timeline is no longer the identity.
    savedSnapshot_ = snapshot();
    emit changed();
    emit rulerChanged();
    emit playheadChanged();
    emit timelineChanged();
    emit dirtyChanged();
    return loaded_;
}

void TimelineController::clear() {
    timeline_ = Project::EditTimeline();
    undo_.clear();
    redo_.clear();
    projectDirectory_.clear();
    projectCreatedAt_.clear();
    savedSnapshot_.clear();
    loaded_ = false;
    playheadMs_ = 0.0;
    setError(QString());
    emit changed();
    emit rulerChanged();
    emit playheadChanged();
    emit timelineChanged();
}

QByteArray TimelineController::snapshot() const {
    if (!timeline_.valid())
        return {};
    // Compact, and only the timeline: the playhead is deliberately excluded. Moving
    // the playhead is not an edit, and folding it in would make every scrub mark the
    // project dirty.
    return QJsonDocument(timeline_.toJson()).toJson(QJsonDocument::Compact);
}

bool TimelineController::dirty() const {
    if (!loaded_)
        return false;
    return snapshot() != savedSnapshot_;
}

bool TimelineController::save() {
    if (!loaded_ || projectDirectory_.isEmpty()) {
        setError(QStringLiteral("没有可保存的工程"));
        return false;
    }
    QString failure;
    if (!Project::save(projectDirectory_, timeline_, playheadMs_, projectCreatedAt_, &failure)) {
        setError(failure);
        return false;
    }
    savedSnapshot_ = snapshot();
    setError(QString());
    emit dirtyChanged();
    return true;
}

template <typename Change>
bool TimelineController::apply(const QString &failureMessage, Change &&change) {
    if (!timeline_.valid())
        return false;
    // The edit is applied to a copy first: a rejected edit must leave the timeline
    // exactly as it was, and must not become an undo step.
    Project::EditTimeline next = timeline_;
    if (!change(next) || !next.valid()) {
        setError(failureMessage + (next.error().isEmpty() ? QString() : QStringLiteral("：") + next.error()));
        return false;
    }
    undo_.push_back(timeline_);
    if (undo_.size() > kMaxHistory)
        undo_.erase(undo_.begin());
    redo_.clear();
    timeline_ = next;
    setError(QString());
    // The output length may have changed, so the playhead can now be past the end
    // and the ruler's ticks are stale.
    clampPlayhead();
    emit changed();
    emit rulerChanged();
    emit timelineChanged();
    emit dirtyChanged();
    return true;
}

bool TimelineController::splitAt(double outputMs) {
    return apply(QStringLiteral("切分失败"), [outputMs](Project::EditTimeline &timeline) {
        return timeline.split(outputMs);
    });
}

bool TimelineController::removeRange(double fromMs, double toMs) {
    return apply(QStringLiteral("删除区间失败"), [fromMs, toMs](Project::EditTimeline &timeline) {
        return timeline.remove(fromMs, toMs);
    });
}

bool TimelineController::setSpeed(double fromMs, double toMs, double rate) {
    return apply(QStringLiteral("变速失败"), [fromMs, toMs, rate](Project::EditTimeline &timeline) {
        return timeline.setSpeed(fromMs, toMs, rate);
    });
}

bool TimelineController::trimStart(double outputMs) {
    return apply(QStringLiteral("裁掉开头失败"), [outputMs](Project::EditTimeline &timeline) {
        return timeline.trimStart(outputMs);
    });
}

bool TimelineController::trimEnd(double outputMs) {
    return apply(QStringLiteral("裁掉结尾失败"), [outputMs](Project::EditTimeline &timeline) {
        return timeline.trimEnd(outputMs);
    });
}

void TimelineController::reset() {
    if (!loaded_ || timeline_.isIdentity())
        return;
    // Reset collapses the splits too, which is what "复原" means to a user: back to
    // the recording as it was captured.
    undo_.push_back(timeline_);
    redo_.clear();
    timeline_ = Project::EditTimeline::whole(timeline_.sourceDurationMs());
    setError(QString());
    clampPlayhead();
    emit dirtyChanged();
    emit changed();
    emit rulerChanged();
    emit timelineChanged();
}

void TimelineController::undo() {
    if (undo_.empty())
        return;
    redo_.push_back(timeline_);
    timeline_ = undo_.back();
    undo_.pop_back();
    setError(QString());
    clampPlayhead();
    emit changed();
    emit rulerChanged();
    emit timelineChanged();
    emit dirtyChanged();
}

void TimelineController::redo() {
    if (redo_.empty())
        return;
    undo_.push_back(timeline_);
    timeline_ = redo_.back();
    redo_.pop_back();
    setError(QString());
    clampPlayhead();
    emit changed();
    emit rulerChanged();
    emit timelineChanged();
    emit dirtyChanged();
}

} // namespace Render
