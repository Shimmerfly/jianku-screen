#include "EditorSession.h"

#include "../project/RecentProjects.h"

namespace Render {

EditorSession::EditorSession(const QString &projectDirectory, QObject *parent)
    : QObject(parent), projectDirectory_(projectDirectory) {
    title_ = RecentProjects::describe(projectDirectory_);

    // The wiring lives here rather than in main.cpp so it exists exactly once per
    // project instead of exactly once per process. `timelineChanged` is emitted for
    // every successful edit, so the exporter always holds the current timeline and an
    // export started from the UI cannot pick up a stale one.
    connect(&timeline_, &TimelineController::timelineChanged, &exporter_, [this] {
        exporter_.setTimeline(timeline_.timeline());
    });
    connect(&timeline_, &TimelineController::changed, this, [this] {
        emit changed();
        emit dirtyChanged();
    });
    connect(&timeline_, &TimelineController::dirtyChanged, this, &EditorSession::dirtyChanged);
    connect(&timeline_, &TimelineController::errorChanged, this, [this] {
        error_ = timeline_.error();
        emit errorChanged();
        emit changed();
    });
}

bool EditorSession::open() {
    const bool ok = timeline_.load(projectDirectory_);
    // The exporter follows the session, not "the last recording". Without this an
    // export started from a tab would write the other tab's project — the exact
    // failure that made a process-wide controller untenable.
    exporter_.setProjectDirectory(projectDirectory_);
    exporter_.setTimeline(timeline_.timeline());
    title_ = RecentProjects::describe(projectDirectory_);
    emit changed();
    return ok;
}

bool EditorSession::save() {
    const bool ok = timeline_.save();
    if (!ok)
        error_ = timeline_.error();
    emit changed();
    return ok;
}

} // namespace Render
