#include "RecentProjects.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>

namespace RecentProjects {
namespace {

QJsonObject manifest(const QString &projectDirectory) {
    QFile file(projectDirectory + QStringLiteral("/project.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object();
}

// Newest first. The manifest's own `createdAt` is preferred over the directory's
// modification time: a project that was re-processed today still *is* yesterday's
// recording, and sorting by mtime would shuffle it to the top for no reason.
QDateTime createdTime(const QString &projectDirectory, const QJsonObject &manifestObject) {
    const QString text = manifestObject.value(QStringLiteral("createdAt")).toString();
    if (!text.isEmpty()) {
        const QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
        if (parsed.isValid())
            return parsed;
        const QDateTime fallback = QDateTime::fromString(text, Qt::ISODate);
        if (fallback.isValid())
            return fallback;
    }
    return QFileInfo(projectDirectory).lastModified();
}

QString durationText(const QJsonObject &manifestObject) {
    const QJsonObject video = manifestObject.value(QStringLiteral("video")).toObject();
    const double durationMs = video.value(QStringLiteral("durationNs")).toVariant().toDouble() / 1e6;
    if (!(durationMs > 0.0))
        return {};
    const int totalSeconds = int(durationMs / 1000.0 + 0.5);
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

// The name the recorder gives a project: "Jianku Screen 2026-10-04 07-30-12-345".
// Only used as a fallback when the manifest has no createdAt.
QString timeFromName(const QString &name) {
    const int space = name.indexOf(QLatin1Char(' '));
    if (space < 0)
        return name;
    const int end = name.indexOf(QStringLiteral(".jianku"));
    const QString stamp = end > space ? name.mid(space + 1, end - space - 1) : name.mid(space + 1);
    return stamp;
}

} // namespace

QString recordingDirectory(const QString &configured) {
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
        + QStringLiteral("/Jianku Screen");
}

QStringList list(const QString &directory) {
    QStringList projects;
    const QDir root(directory);
    if (!root.exists())
        return projects;
    const QStringList names = root.entryList(QStringList{QStringLiteral("*.jianku")},
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QString path = root.filePath(name);
        // A directory without a readable manifest is not a project the loader can
        // open, so it is left out rather than offered and then rejected.
        if (QFileInfo::exists(path + QStringLiteral("/project.json")))
            projects.append(path);
    }
    std::sort(projects.begin(), projects.end(), [](const QString &a, const QString &b) {
        return createdTime(a, manifest(a)) > createdTime(b, manifest(b));
    });
    return projects;
}

QString mostRecent(const QString &directory) {
    const QStringList projects = list(directory);
    return projects.isEmpty() ? QString() : projects.first();
}

QString describe(const QString &projectDirectory) {
    if (projectDirectory.isEmpty())
        return {};
    const QJsonObject object = manifest(projectDirectory);
    const QDateTime created = createdTime(projectDirectory, object);
    // The directory name is the fallback because it always exists; the manifest may
    // be unreadable while the recording itself is perfectly fine.
    QString label = created.isValid()
        ? created.toString(QStringLiteral("MM-dd HH:mm"))
        : timeFromName(QFileInfo(projectDirectory).fileName());
    const QString duration = durationText(object);
    if (!duration.isEmpty())
        label += QStringLiteral(" · ") + duration;
    // Mark the ones that are not ready: a failed recording is still openable (the
    // fragments are readable), but the user should not be surprised by what it is.
    const QString state = object.value(QStringLiteral("state")).toString();
    if (state == QStringLiteral("failed"))
        label += QStringLiteral(" · 未完成");
    else if (state == QStringLiteral("recording"))
        label += QStringLiteral(" · 录制中");
    return label;
}

} // namespace RecentProjects
