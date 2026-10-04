#include "EditSidecar.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace Project {
namespace {

// Bumped when the meaning of a stored field changes. A file with a newer schema is
// refused rather than guessed at: reading a future format with today's rules is how a
// project gets silently mangled.
constexpr int kSchemaVersion = 1;
const char *const kSchemaName = "jianku-edit/1";

} // namespace

QString path(const QString &projectDirectory) {
    if (projectDirectory.isEmpty())
        return {};
    return QDir(projectDirectory).filePath(QStringLiteral("edit.json"));
}

EditSidecar load(const QString &projectDirectory, double durationMs) {
    EditSidecar sidecar;
    const QString file = path(projectDirectory);
    if (file.isEmpty() || !QFileInfo::exists(file))
        return sidecar;

    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly)) {
        sidecar.error = QStringLiteral("无法读取编辑记录：") + file;
        return sidecar;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(handle.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sidecar.error = QStringLiteral("编辑记录不是有效的 JSON：") + parseError.errorString();
        return sidecar;
    }
    const QJsonObject root = document.object();
    const QString schema = root.value(QStringLiteral("schema")).toString();
    if (schema != QLatin1String(kSchemaName)) {
        // An unknown schema is reported but not fatal: the recording opens, the edit is
        // ignored, and the user is told why instead of losing the whole project.
        sidecar.error = QStringLiteral("编辑记录版本不认识（%1），已忽略").arg(schema);
        return sidecar;
    }

    sidecar.projectCreatedAt = root.value(QStringLiteral("projectCreatedAt")).toString();
    sidecar.savedAt = root.value(QStringLiteral("savedAt")).toString();
    sidecar.playheadMs = root.value(QStringLiteral("playheadMs")).toDouble(0.0);

    const QJsonValue timelineValue = root.value(QStringLiteral("timeline"));
    if (!timelineValue.isObject())
        return sidecar;   // A saved playhead with no timeline is legitimate.

    QString timelineError;
    EditTimeline timeline = EditTimeline::fromJson(timelineValue.toObject(), durationMs, &timelineError);
    if (!timeline.valid()) {
        sidecar.error = QStringLiteral("编辑记录里的时间线无效：") + timelineError;
        return sidecar;
    }
    sidecar.timeline = timeline;
    sidecar.valid = true;
    return sidecar;
}

bool save(const QString &projectDirectory, const EditTimeline &timeline, double playheadMs,
    const QString &projectCreatedAt, QString *error) {
    const QString file = path(projectDirectory);
    if (file.isEmpty() || !QDir(projectDirectory).exists()) {
        if (error)
            *error = QStringLiteral("工程目录不存在");
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QLatin1String(kSchemaName));
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("projectCreatedAt"), projectCreatedAt);
    root.insert(QStringLiteral("playheadMs"), playheadMs);
    root.insert(QStringLiteral("savedAt"),
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    if (timeline.valid())
        root.insert(QStringLiteral("timeline"), timeline.toJson());

    // QSaveFile: the edit is either the old file or the new one, never a truncated
    // mixture. A half-written sidecar would make the project fail to open, which is a
    // worse outcome than losing the edit.
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入编辑记录：") + file;
        return false;
    }
    if (out.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !out.commit()) {
        if (error)
            *error = QStringLiteral("保存编辑记录失败：") + file;
        return false;
    }
    return true;
}

} // namespace Project
