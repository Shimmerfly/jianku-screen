#include "BackgroundLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

BackgroundLibrary::BackgroundLibrary(QObject *parent) : QObject(parent) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString bundled = QDir(appDir).filePath(QStringLiteral("../Resources/backgrounds"));
    if (QDir(bundled).exists())
        rootPath_ = QDir(bundled).absolutePath();
    else
        rootPath_ = QDir(QStringLiteral(JIANKU_SOURCE_DIR "/assets/backgrounds")).absolutePath();
    scan();
}

void BackgroundLibrary::scan() {
    entries_.clear();
    const QDir root(rootPath_);
    if (!root.exists()) {
        emit entriesChanged();
        return;
    }
    const QStringList categories = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &category : categories) {
        const QDir categoryDir(root.filePath(category));
        const QStringList files = categoryDir.entryList(
            QStringList{"*.jpg", "*.jpeg", "*.png", "*.webp"}, QDir::Files, QDir::Name);
        for (const QString &file : files) {
            entries_.append(QVariantMap{
                {"category", category},
                {"name", QFileInfo(file).completeBaseName()},
                {"relative", category + "/" + file},
                {"url", QUrl::fromLocalFile(categoryDir.filePath(file)).toString()}});
        }
    }
    emit entriesChanged();
}

QString BackgroundLibrary::urlFor(const QString &relative) const {
    if (relative.isEmpty())
        return {};
    return QUrl::fromLocalFile(QDir(rootPath_).filePath(relative)).toString();
}

void BackgroundLibrary::refresh() {
    scan();
}
