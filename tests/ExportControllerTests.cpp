// Export path checks.
//
// Covers the layer the UI actually talks to: ExportController must run the
// compositor off the GUI thread, report progress, and never claim success for an
// output that is missing or short. The fixture is a real (tiny) MP4 produced by
// ffmpeg, so the decode → compose → encode → verify chain runs end to end.
#include "../src/render/ExportController.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace Render;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
QByteArray line(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}
void write(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "fixture open");
    require(file.write(data) == data.size(), "fixture write");
}

QString ffmpegPath() {
    return findFfmpeg();
}

// 30 frames of 320x180 green, standing in for a recording.
bool makeSourceVideo(const QString &path) {
    QProcess ffmpeg;
    ffmpeg.start(ffmpegPath(), {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("color=c=green:s=320x180:r=30:d=1"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-y"), path});
    return ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0 && QFileInfo::exists(path);
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary project");
        const QString root = directory.path();
        require(makeSourceVideo(root + "/raw.mp4"), "source video is produced by ffmpeg");
        write(root + "/cursor-a.png", [] {
            QImage image(4, 4, QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor(255, 0, 0));
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            return bytes;
        }());

        // The source is 30 frames over 1000 ms but the export runs at 60 fps, so
        // the compositor must hold each source frame for two output frames. That
        // is the point of this check: the file has to contain the full output
        // timeline, not the source frame count.
        constexpr qint64 kSourceFrames = 30;
        constexpr qint64 kOutputFrames = 60;
        QByteArray frames;
        for (int i = 0; i < kSourceFrames; ++i)
            frames += line(QJsonObject{{"mediaTimeNs", QString::number(qint64(i) * 33333333LL)},
                {"displayHostTimeNs", QString::number(1000 + qint64(i) * 33333333LL)},
                {"displayTimeAvailable", true}});
        write(root + "/video-frames.jsonl", frames);

        write(root + "/pointer-timeline.jsonl",
            line(QJsonObject{{"type", "mouseMoved"}, {"mediaTimeNs", "0"},
                {"withinVideo", true}, {"xPx", 100}, {"yPx", 50}})
            + line(QJsonObject{{"type", "mouseDown"}, {"mediaTimeNs", "500000000"},
                {"withinVideo", true}, {"xPx", 100}, {"yPx", 50}}));
        write(root + "/cursor-timeline.jsonl",
            line(QJsonObject{{"mediaTimeNs", "0"}, {"withinVideo", true},
                {"available", true}, {"cursorId", "cursor-a"}}));
        {
            // Keyed by cursor id, so it cannot be a braced pair list.
            QJsonObject definitions;
            definitions.insert(QStringLiteral("cursor-a"), QJsonObject{
                {"image", "cursor-a.png"}, {"widthPx", 4}, {"heightPx", 4},
                {"hotSpotXPx", 0}, {"hotSpotYPx", 0}});
            write(root + "/cursors.json", QJsonDocument(definitions).toJson());
        }
        write(root + "/automatic-zooms.json", QJsonDocument(QJsonObject{
            {"ranges", QJsonArray{QJsonObject{{"startTimeMs", 200.0}, {"endTimeMs", 800.0},
                {"zoom", 2.0}, {"snapToEdgesRatio", 0.25}, {"isDisabled", false}}}}}).toJson());
        write(root + "/project.json", QJsonDocument(QJsonObject{
            {"schemaVersion", 1},
            {"source", QJsonObject{{"type", "display"}, {"widthPx", 320}, {"heightPx", 180},
                {"globalBoundsPoints", QJsonObject{{"width", 320}, {"height", 180}}}}},
            {"settings", QJsonObject{{"outputAspectRatio", "auto"},
                {"backgroundPaddingRatio", 8.0}, {"windowBorderRadius", 8.0},
                {"shadowIntensity", 0.0}, {"backgroundType", "color"},
                {"backgroundColor", "#101820"}, {"cursorSize", 2.0}}},
            {"video", QJsonObject{{"file", "raw.mp4"},
                {"durationNs", QString::number(1000LL * 1000000LL)},
                {"mediaZeroHostTimeNs", "1000"}, {"frameCount", 30}}}}).toJson());

        // --- refuses to run without a project -----------------------------
        {
            ExportController controller;
            require(!controller.busy(), "idle at construction");
            require(controller.defaultOutputPath().isEmpty(), "no default output before a recording");
            require(!controller.start(QString(), true, true, true, true),
                "starting without a project fails instead of pretending to export");
            require(!controller.error().isEmpty(), "the refusal explains itself");
        }

        // --- a real export through the controller --------------------------
        {
            ExportController controller;
            QObject::connect(&controller, &ExportController::finished,
                &app, [&](bool) { app.quit(); });

            controller.setProjectDirectory(root);
            require(controller.defaultOutputPath() == root + "/composed.mp4",
                "default output is composed.mp4 next to the project");
            require(controller.start(QString(), true, true, false, false), "export starts");
            require(controller.busy(), "busy while exporting");

            double seenProgress = 0.0;
            QObject::connect(&controller, &ExportController::progressChanged,
                &app, [&] { seenProgress = std::max(seenProgress, controller.progress()); });

            QTimer::singleShot(120000, &app, [] {
                throw std::runtime_error("export did not finish within 120 s");
            });
            app.exec();

            require(!controller.busy(), "not busy after finishing");
            require(controller.error().isEmpty(),
                "export reports no error (see status for detail)");
            require(QFileInfo::exists(root + "/composed.mp4"), "output file exists");
            require(QFileInfo(root + "/composed.mp4").size() > 0, "output file is not empty");
            require(seenProgress > 0.0, "progress was reported");
            require(controller.progress() == 1.0, "progress ends at 1.0");
            require(controller.status().contains(QStringLiteral("导出完成")),
                "status line names the finished export");

            // The written frame count must match what the file actually holds.
            QProcess probe;
            probe.start(findFfprobe(), {QStringLiteral("-v"), QStringLiteral("error"),
                QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                QStringLiteral("-count_packets"),
                QStringLiteral("-show_entries"), QStringLiteral("stream=nb_read_packets"),
                QStringLiteral("-of"), QStringLiteral("csv=p=0"), root + "/composed.mp4"});
            require(probe.waitForFinished(60000) && probe.exitCode() == 0, "ffprobe runs");
            bool ok = false;
            const qint64 packets = QString::fromUtf8(probe.readAllStandardOutput())
                .trimmed().toLongLong(&ok);
            require(ok && packets == kOutputFrames,
                "the file holds every composited output frame (60 at 60 fps)");
        }

        // --- a missing project is still refused ----------------------------
        {
            ExportController controller;
            controller.setProjectDirectory(root + "/does-not-exist");
            require(!controller.start(QString(), true, true, false, false),
                "a project directory without project.json is refused");
        }

        std::cout << "export controller checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
