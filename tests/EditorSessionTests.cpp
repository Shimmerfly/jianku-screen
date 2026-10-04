// Two recordings open at once must not share anything.
//
// This is the invariant that forced `EditorSession` into existence. The controllers used
// to be process-wide singletons, so `load()` on one project reset the other's timeline —
// meaning that with a second tab open, merely switching tabs would discard the edit you
// were not looking at. That failure is silent, which is why it is pinned here rather than
// left to the UI to demonstrate.
#include "../src/render/EditorSession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Render;

namespace {

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool writeProject(const QString &directory, double durationMs, const QString &createdAt) {
    if (!QDir().mkpath(directory))
        return false;
    // The loader requires the video file and both timelines to exist: an unreadable
    // timeline is treated as a damaged project rather than as "no events", because a
    // lost timeline is how a recording ends up with no pointer at all.
    for (const char *name : {"/raw.mp4", "/pointer-timeline.jsonl", "/cursor-timeline.jsonl",
             "/pointer-events.jsonl", "/cursor-observations.jsonl"}) {
        QFile file(directory + QString::fromLatin1(name));
        if (!file.open(QIODevice::WriteOnly))
            return false;
    }
    QFile frames(directory + "/video-frames.jsonl");
    if (!frames.open(QIODevice::WriteOnly))
        return false;
    for (int i = 0; i < 30; ++i) {
        const QJsonObject frame{{"mediaTimeNs", QString::number(qint64(i) * 33333333)}};
        const QByteArray line = QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n';
        if (frames.write(line) != line.size())
            return false;
    }
    frames.close();

    QFile manifest(directory + "/project.json");
    if (!manifest.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject root{
        {"schemaVersion", 1},
        {"createdAt", createdAt},
        {"source", QJsonObject{{"type", "display"}, {"widthPx", 1000}, {"heightPx", 500}}},
        {"settings", QJsonObject{{"outputAspectRatio", "auto"}}},
        {"video", QJsonObject{{"file", "raw.mp4"},
            {"durationNs", QString::number(qint64(durationMs * 1e6))},
            {"mediaZeroHostTimeNs", "1000"}, {"frameCount", 30}}}};
    const QByteArray bytes = QJsonDocument(root).toJson();
    return manifest.write(bytes) == bytes.size();
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        require(temp.isValid(), "temporary directory");
        const QString first = temp.path() + "/first.jianku";
        const QString second = temp.path() + "/second.jianku";
        require(writeProject(first, 10000.0, "2026-10-04T07:00:00.000"), "first fixture");
        require(writeProject(second, 20000.0, "2026-10-04T08:00:00.000"), "second fixture");

        EditorSession a(first);
        EditorSession b(second);
        require(a.open(), "the first session opens");
        require(b.open(), "the second session opens");
        require(std::abs(a.timeline()->sourceDurationMs() - 10000.0) < 0.5,
            "the first session sees its own duration");
        require(std::abs(b.timeline()->sourceDurationMs() - 20000.0) < 0.5,
            "and the second session sees a different one, not the first's");

        // --- the two do not share an exporter target ------------------------------
        // With one process-wide controller, an export started from a tab could write the
        // other tab's project. Each session has to point at its own directory.
        require(a.defaultExportPath().contains(first), "a's export lands in a's project");
        require(b.defaultExportPath().contains(second), "b's export lands in b's project");
        require(a.defaultExportPath() != b.defaultExportPath(),
            "the two sessions do not export to the same file");

        // --- editing one leaves the other alone -----------------------------------
        require(a.timeline()->splitAt(4000.0), "split in a");
        require(a.timeline()->setSpeed(0.0, 4000.0, 2.0), "speed change in a");
        const double aDuration = a.timeline()->outputDurationMs();
        require(aDuration < 10000.0, "which shortens a's output");
        require(a.timeline()->canUndo(), "and a has an undo step");
        require(!b.timeline()->canUndo(), "while b has none — the undo stacks are separate");
        require(std::abs(b.timeline()->outputDurationMs() - 20000.0) < 0.5,
            "b's timeline is untouched");
        require(b.timeline()->segments().size() == 1, "b still has a single segment");

        // --- reloading one does not disturb the other -----------------------------
        // This is the case that used to lose data: `load()` resets the timeline, and with
        // a shared controller that meant opening b threw away a's edit.
        require(b.open(), "reopen b");
        require(a.timeline()->canUndo(), "a's undo history survives b being opened");
        require(std::abs(a.timeline()->outputDurationMs() - aDuration) < 0.5,
            "and a's edit is still applied");

        // --- each session saves only its own edit ---------------------------------
        require(a.save(), "a saves");
        require(QFileInfo::exists(first + "/edit.json"), "a wrote its sidecar");
        require(!QFileInfo::exists(second + "/edit.json"), "and b's project has none");

        EditorSession reopened(first);
        require(reopened.open(), "reopen a");
        require(std::abs(reopened.timeline()->outputDurationMs() - aDuration) < 0.5,
            "a's saved edit comes back");
        require(!reopened.dirty(), "and a freshly opened session is clean");
        require(reopened.title().contains(QStringLiteral(":")),
            "the tab title carries the recording's time and length");

        std::cout << "editor session checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
