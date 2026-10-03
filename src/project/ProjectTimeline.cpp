#include "ProjectTimeline.h"
#include "../animation/AutomaticZoom.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {
qint64 integer(const QJsonObject &object, const char *key) {
    bool ok = false;
    const qint64 value = object.value(key).toString().toLongLong(&ok);
    if (!ok) throw std::runtime_error(QStringLiteral("缺少有效时间戳：") .append(key).toStdString());
    return value;
}
std::vector<QJsonObject> readLines(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
    std::vector<QJsonObject> result;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !json.isObject())
            throw std::runtime_error("事件文件损坏，原始素材保留");
        result.push_back(json.object());
    }
    if (file.error() != QFileDevice::NoError) throw std::runtime_error(file.errorString().toStdString());
    return result;
}
bool save(const QString &path, const QByteArray &data) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
// A pause freezes the media clock. Events recorded while paused belong to no
// video frame, so they are folded onto the pause start and flagged instead of
// silently stretching the timeline.
struct PauseFold {
    std::vector<std::pair<qint64, qint64>> ranges;

    qint64 foldedBefore(qint64 hostNs) const {
        qint64 total = 0;
        for (const auto &range : ranges)
            total += std::max<qint64>(0, std::min(hostNs, range.second) - range.first);
        return total;
    }
    bool inside(qint64 hostNs) const {
        for (const auto &range : ranges)
            if (hostNs >= range.first && hostNs < range.second)
                return true;
        return false;
    }
};
PauseFold readPauses(const QJsonObject &video) {
    PauseFold fold;
    for (const auto &value : video.value("pauseRanges").toArray()) {
        const QJsonObject range = value.toObject();
        bool startOk = false, endOk = false;
        const qint64 start = range.value("startHostTimeNs").toString().toLongLong(&startOk);
        const qint64 end = range.value("endHostTimeNs").toString().toLongLong(&endOk);
        if (startOk && endOk && end > start)
            fold.ranges.emplace_back(start, end);
    }
    std::sort(fold.ranges.begin(), fold.ranges.end());
    return fold;
}
qint64 normalize(const QString &directory, const QString &source, const QString &target,
    qint64 zero, qint64 duration, const PauseFold &pauses,
    std::vector<double> *clicks = nullptr) {
    auto records = readLines(directory + '/' + source);
    for (auto &record : records) {
        const qint64 host = integer(record, "hostTimeNs");
        const bool duringPause = pauses.inside(host);
        const qint64 media = host - zero - pauses.foldedBefore(host);
        record.insert("mediaTimeNs", QString::number(media));
        record.insert("recordingTimeMs", static_cast<double>(media) / 1e6);
        if (duringPause)
            record.insert("duringPause", true);
        // Keep pre-roll and tail observations for initialization, but exclude
        // their clicks from the actual video interval.
        record.insert("withinVideo", !duringPause && media >= 0 && media < duration);
    }
    std::stable_sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
        return integer(a, "hostTimeNs") < integer(b, "hostTimeNs");
    });
    QSaveFile file(directory + '/' + target);
    if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString());
    for (const auto &record : records) {
        const auto line = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
        if (file.write(line) != line.size()) throw std::runtime_error(file.errorString().toStdString());
        const auto type = record.value("type").toString();
        if (clicks && record.value("withinVideo").toBool()
            && (type == "mouseDown" || type == "mouseUp"))
            clicks->push_back(record.value("recordingTimeMs").toDouble());
    }
    if (!file.commit()) throw std::runtime_error(file.errorString().toStdString());
    return static_cast<qint64>(records.size());
}
}

QJsonObject ProjectTimeline::build(const QString &directory, const QJsonObject &video) {
    try {
        const qint64 zero = integer(video, "mediaZeroHostTimeNs");
        const qint64 duration = integer(video, "durationNs");
        if (zero <= 0 || duration <= 0) throw std::runtime_error("视频时间轴无效");
        const auto frames = readLines(directory + QStringLiteral("/video-frames.jsonl"));
        if (frames.empty() || integer(frames.front(), "mediaTimeNs") != 0
            || integer(frames.front(), "displayHostTimeNs") != zero)
            throw std::runtime_error("视频首帧时间与工程锚点不一致");
        const PauseFold pauses = readPauses(video);
        // Preserve the worst discrepancy for QA; never silently rescale or shift
        // events to hide an unknown clock relationship. Frame rows store media
        // time already folded past pauses, so the host clock is folded too.
        qint64 maxClockResidual = 0, previousMedia = -1;
        bool displayTimeAvailable = true;
        for (const auto &frame : frames) {
            const auto media = integer(frame, "mediaTimeNs");
            if (media <= previousMedia) throw std::runtime_error("视频帧时间没有严格递增");
            previousMedia = media;
            const qint64 displayHost = integer(frame, "displayHostTimeNs");
            maxClockResidual = std::max(maxClockResidual,
                std::abs(displayHost - zero - pauses.foldedBefore(displayHost) - media));
            if (!frame.value("repeatedAtStop").toBool())
                displayTimeAvailable &= frame.value("displayTimeAvailable").toBool();
        }
        std::vector<double> clicks;
        const auto events = normalize(directory, "pointer-events.jsonl", "pointer-timeline.jsonl", zero, duration, pauses, &clicks);
        const auto observations = normalize(directory, "cursor-observations.jsonl", "cursor-timeline.jsonl", zero, duration, pauses);
        QJsonArray ranges;
        for (const auto &range : Animation::automaticZooms(clicks, static_cast<double>(duration) / 1e6)) {
            ranges.append(QJsonObject{{"startTimeMs", range.startMs}, {"endTimeMs", range.endMs},
                {"zoom", range.zoom}, {"type", "follow-click-groups"},
                {"snapToEdgesRatio", 0.25}, {"isDisabled", false}, {"isSystem", true},
                {"hasInstantAnimation", false}});
        }
        const QJsonObject zooms{{"schemaVersion", 1}, {"model", "desktop-3.7.5-research-v1"},
            {"durationMs", static_cast<double>(duration) / 1e6}, {"ranges", ranges}};
        if (!save(directory + QStringLiteral("/automatic-zooms.json"), QJsonDocument(zooms).toJson()))
            throw std::runtime_error("无法保存自动缩放区间");
        return {{"state", "generated"}, {"error", ""}, {"pointerTimeline", "pointer-timeline.jsonl"},
            {"cursorTimeline", "cursor-timeline.jsonl"}, {"automaticZooms", "automatic-zooms.json"},
            {"eventCount", events}, {"cursorObservationCount", observations}, {"clickEventCount", static_cast<qint64>(clicks.size())},
            {"zoomRangeCount", ranges.size()}, {"maxFrameClockResidualNs", QString::number(maxClockResidual)},
            {"displayTimeAvailableForAllFrames", displayTimeAvailable}, {"visualMatchValidated", false}};
    } catch (const std::exception &error) {
        return {{"state", "failed"}, {"error", QString::fromUtf8(error.what())}};
    }
}
