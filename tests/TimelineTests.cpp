#include "../src/animation/AutomaticZoom.h"
#include "../src/project/ProjectTimeline.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "fixture open");
    require(file.write(data) == data.size(), "fixture write");
}
QByteArray jsonLine(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}
QJsonObject read(const QString &path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "result open");
    return QJsonDocument::fromJson(file.readAll()).object();
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        using Animation::automaticZooms;
        auto ranges = automaticZooms({2000.9}, 10000);
        require(ranges.size() == 1 && ranges[0].startMs == 1700 && ranges[0].endMs == 4500,
            "click must floor before offsets");
        require(automaticZooms({9000}, 10000).empty(), "exact one-second tail excluded");
        ranges = automaticZooms({8999.99}, 10000.5);
        require(ranges.size() == 1 && ranges[0].endMs == 9200, "fractional duration end floors");
        ranges = automaticZooms({0}, 10000);
        require(ranges[0].startMs == 1, "start clamped at one millisecond");
        ranges = automaticZooms({2000, 7300}, 15000);
        require(ranges.size() == 1 && ranges[0].endMs == 9800, "exact 2500 gap joins");
        require(automaticZooms({2000, 7301}, 15000).size() == 2, "2501 gap remains separate");
        require(automaticZooms({2000}, 10000, true).empty(), "external device has no auto ranges");
        require(automaticZooms({}, 10000).empty() && automaticZooms({0}, 800).empty(), "empty and short inputs");

        QTemporaryDir directory; require(directory.isValid(), "temporary project");
        const QString root = directory.path();
        // Anchor is intentionally far from zero. Nanoseconds stay decimal strings.
        const qint64 zero = 123456789000000;
        const QJsonObject video{{"mediaZeroHostTimeNs", QString::number(zero)}, {"durationNs", "10000000000"}};
        write(root + "/video-frames.jsonl", jsonLine({{"mediaTimeNs", "0"},
            {"displayHostTimeNs", QString::number(zero)}, {"displayTimeAvailable", true}})
            + jsonLine({{"mediaTimeNs", "16666667"}, {"displayHostTimeNs", QString::number(zero + 16666667)},
                {"displayTimeAvailable", true}}));
        write(root + "/pointer-events.jsonl", jsonLine({{"hostTimeNs", QString::number(zero - 1000000)},
            {"type", "mouseDown"}}) + jsonLine({{"hostTimeNs", QString::number(zero + 2000900000)}, {"type", "mouseDown"}})
            + jsonLine({{"hostTimeNs", QString::number(zero + 2010000000)}, {"type", "mouseUp"}})
            + jsonLine({{"hostTimeNs", QString::number(zero + 9000000000)}, {"type", "mouseDown"}}));
        write(root + "/cursor-observations.jsonl", jsonLine({{"hostTimeNs", QString::number(zero - 2000000)},
            {"cursorId", "initial"}, {"available", true}}));
        const auto result = ProjectTimeline::build(root, video);
        require(result.value("state") == "generated", "project processing succeeds");
        require(result.value("clickEventCount").toInt() == 3, "pre-roll clicks excluded");
        require(result.value("maxFrameClockResidualNs") == "0", "host and media clocks align");
        const auto range = read(root + "/automatic-zooms.json").value("ranges").toArray()[0].toObject();
        require(range.value("startTimeMs").toInt() == 1700 && range.value("endTimeMs").toInt() == 4510,
            "mouse up contributes while exact tail is excluded");
        QFile cursor(root + "/cursor-timeline.jsonl"); require(cursor.open(QIODevice::ReadOnly), "cursor timeline");
        const auto observation = QJsonDocument::fromJson(cursor.readLine()).object();
        require(observation.value("mediaTimeNs") == "-2000000" && !observation.value("withinVideo").toBool(),
            "pre-roll cursor retained with signed time");
        write(root + "/pointer-events.jsonl", "{invalid}\n");
        require(ProjectTimeline::build(root, video).value("state") == "failed", "corruption explicit failure");
        require(read(root + "/automatic-zooms.json").value("ranges").toArray().size() == 1,
            "failed regeneration preserves prior atomic result");
        write(root + "/video-frames.jsonl", jsonLine({{"mediaTimeNs", "0"}, {"displayHostTimeNs", "1"}}));
        require(ProjectTimeline::build(root, video).value("state") == "failed", "mismatched anchor rejected");

        // Pause folding: a 3 s pause at t=4s..7s must not create a gap. Frames
        // already carry folded media time, so host time 7.1 s maps to 4.1 s.
        write(root + "/video-frames.jsonl", jsonLine({{"mediaTimeNs", "0"},
            {"displayHostTimeNs", QString::number(zero)}, {"displayTimeAvailable", true}})
            + jsonLine({{"mediaTimeNs", "4100000000"}, {"displayHostTimeNs", QString::number(zero + 7100000000)},
                {"displayTimeAvailable", true}}));
        const QJsonObject pausedVideo{{"mediaZeroHostTimeNs", QString::number(zero)},
            {"durationNs", "8000000000"},
            {"pauseRanges", QJsonArray{QJsonObject{
                {"startHostTimeNs", QString::number(zero + 4000000000)},
                {"endHostTimeNs", QString::number(zero + 7000000000)},
                {"durationNs", "3000000000"}}}}};
        write(root + "/pointer-events.jsonl",
            jsonLine({{"hostTimeNs", QString::number(zero + 1000000000)}, {"type", "mouseMoved"}})
            + jsonLine({{"hostTimeNs", QString::number(zero + 4500000000)}, {"type", "mouseMoved"}})
            + jsonLine({{"hostTimeNs", QString::number(zero + 7500000000)}, {"type", "mouseMoved"}}));
        const auto paused = ProjectTimeline::build(root, pausedVideo);
        require(paused.value("state") == "generated", "paused project processes");
        require(paused.value("maxFrameClockResidualNs") == "0", "pause fold keeps clocks aligned");
        QFile pausedTimeline(root + "/pointer-timeline.jsonl");
        require(pausedTimeline.open(QIODevice::ReadOnly), "paused timeline");
        const auto before = QJsonDocument::fromJson(pausedTimeline.readLine()).object();
        const auto inside = QJsonDocument::fromJson(pausedTimeline.readLine()).object();
        const auto after = QJsonDocument::fromJson(pausedTimeline.readLine()).object();
        require(before.value("mediaTimeNs") == "1000000000" && before.value("withinVideo").toBool(),
            "pre-pause event keeps its time");
        require(inside.value("mediaTimeNs") == "4000000000" && inside.value("duringPause").toBool()
            && !inside.value("withinVideo").toBool(), "in-pause event folds to pause start");
        require(after.value("mediaTimeNs") == "4500000000" && after.value("withinVideo").toBool(),
            "post-pause event loses the paused interval");
        std::cout << "timeline boundary and recovery checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
