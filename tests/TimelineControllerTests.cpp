// Timeline controller checks.
//
// The controller is the only thing the UI talks to for editing, and it owns two
// behaviours the model does not: an edit that fails must leave the timeline
// untouched, and every successful edit must be undoable. Both are easy to get
// subtly wrong (pushing an undo step for a rejected edit, or reporting success
// while the timeline is unchanged) and neither shows up until a user loses work.
#include "../src/render/TimelineController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Render;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(double a, double b, double epsilon = 1e-6) {
    return std::abs(a - b) <= epsilon;
}

// A minimal project. The controller reads the recording's length through the same
// loader the exporter uses, so the fixture has to satisfy that loader: a manifest
// plus the video it names. The video is a placeholder — the loader checks that the
// file exists, not that it decodes, and nothing here decodes it.
bool writeProject(const QString &directory, double durationMs) {
    if (!QDir().mkpath(directory))
        return false;
    QFile video(directory + "/raw.mp4");
    if (!video.open(QIODevice::WriteOnly))
        return false;
    video.write("placeholder");
    video.close();
    // The frame timeline is what drives the picture, so the loader refuses a
    // project without one. A handful of frames spread over the recording is
    // enough: nothing here renders.
    QFile frames(directory + "/video-frames.jsonl");
    if (!frames.open(QIODevice::WriteOnly))
        return false;
    const int frameCount = 10;
    for (int i = 0; i < frameCount; ++i) {
        const qint64 mediaNs = qint64(durationMs * 1e6) * i / frameCount;
        const QJsonObject row{{"mediaTimeNs", QString::number(mediaNs)},
            {"displayHostTimeNs", QString::number(1000 + mediaNs)},
            {"displayTimeAvailable", true}};
        const QByteArray line = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
        if (frames.write(line) != line.size())
            return false;
    }
    frames.close();
    // The event and cursor timelines must exist even when empty: the loader treats
    // an unreadable file as a damaged project rather than as "no events", because a
    // lost timeline is how a recording ends up with no pointer at all.
    for (const char *name : {"/pointer-timeline.jsonl", "/cursor-timeline.jsonl"}) {
        QFile empty(directory + QString::fromLatin1(name));
        if (!empty.open(QIODevice::WriteOnly))
            return false;
    }
    QFile file(directory + "/project.json");
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject manifest{
        {"schemaVersion", 1},
        {"source", QJsonObject{{"type", "display"}, {"widthPx", 1000}, {"heightPx", 500}}},
        {"settings", QJsonObject{{"outputAspectRatio", "auto"}}},
        {"video", QJsonObject{{"file", "raw.mp4"},
            {"durationNs", QString::number(qint64(durationMs * 1e6))},
            {"mediaZeroHostTimeNs", "1000"}, {"frameCount", 10}}}};
    const QByteArray bytes = QJsonDocument(manifest).toJson();
    return file.write(bytes) == bytes.size();
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        require(temp.isValid(), "temporary directory");
        const QString project = temp.path() + "/test.jianku";
        require(writeProject(project, 10000.0), "fixture project");

        // --- loading ---------------------------------------------------------
        {
            TimelineController controller;
            require(!controller.loaded(), "nothing is loaded at construction");
            require(!controller.load(QString()), "an empty path is refused");
            require(!controller.loaded(), "and leaves nothing loaded");
            require(!controller.load(temp.path() + "/missing.jianku"),
                "a directory without project.json is refused");
            require(!controller.error().isEmpty(), "and says why");

            require(controller.load(project), "a real project loads");
            require(controller.loaded(), "and is marked loaded");
            require(close(controller.sourceDurationMs(), 10000.0), "the source length is read");
            require(close(controller.outputDurationMs(), 10000.0), "output starts as the whole recording");
            require(!controller.edited(), "a fresh load is not edited");
            require(!controller.canUndo() && !controller.canRedo(), "with no history");
            require(close(controller.outputRatio(), 1.0), "and a ratio of 1");
            require(controller.segments().size() == 1, "one segment");
            const QVariantMap segment = controller.segments().front().toMap();
            require(close(segment.value(QStringLiteral("startRatio")).toDouble(), 0.0),
                "the single segment starts at the beginning of the strip");
            require(close(segment.value(QStringLiteral("lengthRatio")).toDouble(), 1.0),
                "and covers the whole strip");
            require(!segment.value(QStringLiteral("retimed")).toBool(), "and is not retimed");
        }

        // --- operations and history ------------------------------------------
        {
            TimelineController controller;
            int changes = 0;
            QObject::connect(&controller, &TimelineController::timelineChanged,
                [&changes] { ++changes; });
            require(controller.load(project), "loaded");

            require(changes == 1, "loading announced the initial timeline");
            require(controller.splitAt(4000.0), "split succeeds");
            require(controller.segments().size() == 2, "into two segments");
            require(controller.canUndo(), "a successful edit is undoable");
            require(!controller.edited(), "but splitting alone does not change the output");
            require(changes == 2, "and the split announced the new timeline");

            require(controller.removeRange(1000.0, 3000.0), "a range can be removed");
            require(close(controller.outputDurationMs(), 8000.0), "output shrinks");
            require(controller.edited(), "and the timeline counts as edited");
            require(close(controller.outputRatio(), 0.8), "the ratio reflects it");
            const QVariantList segments = controller.segments();
            require(segments.size() == 3, "three segments after the cut");
            double ratioSum = 0.0;
            for (const QVariant &value : segments)
                ratioSum += value.toMap().value(QStringLiteral("lengthRatio")).toDouble();
            require(close(ratioSum, 1.0, 1e-9), "the strip fractions add up to the whole");

            // --- undo / redo -------------------------------------------------
            controller.undo();
            require(close(controller.outputDurationMs(), 10000.0), "undo restores the length");
            require(controller.canRedo(), "and offers a redo");
            controller.redo();
            require(close(controller.outputDurationMs(), 8000.0), "redo reapplies the cut");
            require(!controller.canRedo(), "after which there is nothing to redo");

            // A new edit after an undo drops the redo branch: the alternative would
            // be a history tree, which is not what an editor's undo means.
            controller.undo();
            require(controller.canRedo(), "undo leaves a redo available");
            require(controller.setSpeed(0.0, 2000.0, 2.0), "a new edit succeeds");
            require(!controller.canRedo(), "and clears the redo branch");
            require(close(controller.outputDurationMs(), 9000.0), "the new edit took effect");

            controller.reset();
            require(controller.edited() == false, "reset returns to the whole recording");
            require(close(controller.outputDurationMs(), 10000.0), "with its full length");
            require(controller.canUndo(), "and is itself undoable");
        }

        // --- a rejected edit changes nothing ---------------------------------
        // This is the property that protects the user's work: a refused operation
        // must not move the timeline and must not become an undo step.
        {
            TimelineController controller;
            require(controller.load(project), "loaded");
            require(controller.removeRange(0.0, 2000.0), "a first edit succeeds");
            const double before = controller.outputDurationMs();
            const int segmentsBefore = controller.segments().size();
            controller.undo();
            controller.redo();
            const bool undoBefore = controller.canUndo();
            require(undoBefore, "history is non-empty before the rejected edit");

            const int changesBefore = 0;
            Q_UNUSED(changesBefore);
            require(!controller.setSpeed(0.0, 1000.0, 0.0), "a zero speed is refused");
            require(!controller.error().isEmpty(), "and reports a reason");
            require(close(controller.outputDurationMs(), before), "the length is unchanged");
            require(controller.segments().size() == segmentsBefore, "the segments are unchanged");
            require(controller.canUndo() == undoBefore, "no undo step was pushed");

            require(!controller.removeRange(500.0, 500.0), "an empty range is refused");
            require(!controller.removeRange(0.0, 100000.0),
                "removing the whole timeline is refused");
            require(!controller.trimEnd(10.0), "a trim leaving under the minimum is refused");
            require(close(controller.outputDurationMs(), before),
                "and none of them moved the timeline");

            // A later valid edit still reports the error state clearly.
            require(controller.setSpeed(0.0, 1000.0, 1.5), "a valid edit is accepted");
            require(controller.error().isEmpty(), "and clears the error");
        }

        // --- edits are expressed in output time ------------------------------
        // After a cut everything downstream shifts earlier, which is what a timeline
        // strip shows. A controller that used source time here would cut the wrong
        // part of the recording.
        {
            TimelineController controller;
            require(controller.load(project), "loaded");
            require(controller.removeRange(1000.0, 6000.0), "cut out the middle");
            require(close(controller.outputDurationMs(), 5000.0), "leaving five seconds");
            // A cut leaves two segments (0..1000 and 6000..10000 of media). Splitting
            // at output 1 s lands exactly on that boundary, so no third segment is
            // created — and, crucially, the boundary is at media 6000, not media
            // 1000: the controller works in output time.
            require(controller.splitAt(1000.0), "split at the cut point");
            const QVariantList segments = controller.segments();
            require(segments.size() == 2, "splitting on an existing boundary adds nothing");
            require(close(segments[1].toMap().value(QStringLiteral("sourceStartMs")).toDouble(), 6000.0),
                "output 1 s is where media 6 s now lives");

            // Splitting inside a segment does create a boundary, and the boundary is
            // in media time: output 500 ms is media 500 ms.
            require(controller.splitAt(500.0), "split inside the first segment");
            const QVariantList split = controller.segments();
            require(split.size() == 3, "three segments now");
            require(close(split[1].toMap().value(QStringLiteral("sourceStartMs")).toDouble(), 500.0),
                "the new boundary is at media 500 ms");
            require(close(split[1].toMap().value(QStringLiteral("sourceEndMs")).toDouble(), 1000.0),
                "and the first segment still ends where the cut begins");
        }

        // --- no project -------------------------------------------------------
        {
            TimelineController controller;
            controller.clear();
            require(!controller.loaded(), "cleared");
            require(!controller.splitAt(1000.0), "operations on an empty controller fail");
            require(!controller.canUndo(), "and do not create history");
            controller.undo();
            controller.redo();
            require(!controller.loaded(), "undo/redo on an empty history is harmless");
        }

        std::cout << "timeline controller checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
