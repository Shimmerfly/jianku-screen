#include "TimelineController.h"

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
    timeline_ = Project::EditTimeline::whole(project.durationMs);
    undo_.clear();
    redo_.clear();
    loaded_ = timeline_.valid();
    setError(loaded_ ? QString() : timeline_.error());
    emit changed();
    emit timelineChanged();
    return loaded_;
}

void TimelineController::clear() {
    timeline_ = Project::EditTimeline();
    undo_.clear();
    redo_.clear();
    projectDirectory_.clear();
    loaded_ = false;
    setError(QString());
    emit changed();
    emit timelineChanged();
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
    emit changed();
    emit timelineChanged();
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
    emit changed();
    emit timelineChanged();
}

void TimelineController::undo() {
    if (undo_.empty())
        return;
    redo_.push_back(timeline_);
    timeline_ = undo_.back();
    undo_.pop_back();
    setError(QString());
    emit changed();
    emit timelineChanged();
}

void TimelineController::redo() {
    if (redo_.empty())
        return;
    undo_.push_back(timeline_);
    timeline_ = redo_.back();
    redo_.pop_back();
    setError(QString());
    emit changed();
    emit timelineChanged();
}

} // namespace Render
