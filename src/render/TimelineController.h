#pragma once

#include "../project/EditTimeline.h"

#include <QObject>
#include <QVariantList>
#include <QString>
#include <vector>

namespace Render {

// The edit timeline as the UI sees it: a list of segments, the operations that
// change them, and undo/redo.
//
// All the arithmetic lives in Project::EditTimeline, which is a pure model with its
// own tests. This class only adds what a UI needs — a list QML can render, error
// reporting, and a history — so there is still exactly one implementation of what a
// cut means. The distinction matters: if the UI did its own time arithmetic, the
// preview and the export would eventually disagree about where a cut is.
//
// Times are in *output* milliseconds, i.e. the clock of the finished film. After a
// cut, everything after it shifts earlier, which is what a timeline strip shows.
class TimelineController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool edited READ edited NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString projectDirectory READ projectDirectory NOTIFY changed)
    Q_PROPERTY(double sourceDurationMs READ sourceDurationMs NOTIFY changed)
    Q_PROPERTY(double outputDurationMs READ outputDurationMs NOTIFY changed)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // The exported length as a proportion of the recording, for a one-glance label.
    Q_PROPERTY(double outputRatio READ outputRatio NOTIFY changed)

public:
    explicit TimelineController(QObject *parent = nullptr);

    bool loaded() const { return loaded_; }
    // Whether the export would differ from the recording. A split alone does not
    // qualify: it is an undoable step, not a change to the film.
    bool edited() const { return !timeline_.playsWholeRecording(); }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    QString projectDirectory() const { return projectDirectory_; }
    double sourceDurationMs() const { return timeline_.sourceDurationMs(); }
    double outputDurationMs() const { return timeline_.outputDurationMs(); }
    QVariantList segments() const;
    QString error() const { return error_; }
    double outputRatio() const;

    const Project::EditTimeline &timeline() const { return timeline_; }

    // Reads the recording's length from the project. Called when a recording
    // finishes; an empty or unreadable directory clears the timeline.
    Q_INVOKABLE bool load(const QString &projectDirectory);
    Q_INVOKABLE void clear();

    // --- operations, all in output time -----------------------------------
    Q_INVOKABLE bool splitAt(double outputMs);
    Q_INVOKABLE bool removeRange(double fromMs, double toMs);
    Q_INVOKABLE bool setSpeed(double fromMs, double toMs, double rate);
    Q_INVOKABLE bool trimStart(double outputMs);
    Q_INVOKABLE bool trimEnd(double outputMs);
    Q_INVOKABLE void reset();

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

signals:
    void changed();
    void errorChanged();
    // Emitted for every successful change, so the exporter can pick the timeline up.
    void timelineChanged();

private:
    // Applies `change` and records a history entry only when it succeeds and
    // actually changed something. A failed edit must not push an undo step.
    template <typename Change>
    bool apply(const QString &failureMessage, Change &&change);

    void setError(const QString &message);

    Project::EditTimeline timeline_;
    std::vector<Project::EditTimeline> undo_;
    std::vector<Project::EditTimeline> redo_;
    QString projectDirectory_;
    QString error_;
    bool loaded_ = false;
};

} // namespace Render
