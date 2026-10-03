#include "BrandAssets.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSize>

namespace Branding {
namespace {

// The sizes the build script produces. Asking QIcon for a size it has no pixmap for
// makes Qt scale one of these, which is fine for a one-off but wasteful per repaint.
constexpr int kSizes[] = {22, 48, 128};

QString brandingDirectory() {
    static const QString resolved = [] {
        const QString bundled = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../Resources/branding"));
        if (QDir(bundled).exists())
            return QDir(bundled).absolutePath();
        const QString source = QStringLiteral(JIANKU_SOURCE_DIR "/assets/branding");
        if (QDir(source).exists())
            return QDir(source).absolutePath();
        return QString();
    }();
    return resolved;
}

QIcon iconFromFile(const QString &name) {
    const QString path = QDir(brandingDirectory()).filePath(name);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    // Not `QIcon(path)`: the marks ship at fixed pixel sizes and Qt's own scaling
    // picks a different filter than the build script's Lanczos, so a 22 px tray icon
    // taken from a 128 px source would come out visibly different from the file.
    QIcon icon;
    for (const int size : kSizes) {
        const QString candidate = QDir(brandingDirectory())
            .filePath(name.chopped(4) + QStringLiteral("-") + QString::number(size) + QStringLiteral(".png"));
        if (QFileInfo::exists(candidate))
            icon.addFile(candidate, QSize(size, size));
    }
    if (icon.isNull())
        icon.addFile(path);
    return icon;
}

} // namespace

QString directory() { return brandingDirectory(); }

QString file(const QString &name) {
    const QString path = QDir(brandingDirectory()).filePath(name);
    return QFileInfo::exists(path) ? path : QString();
}

QIcon mark(int size) {
    if (size > 0) {
        // A specific size: use that exact file when it exists, otherwise scale the
        // nearest one. QIcon does the scaling when the requested size is unknown.
        const QString path = QDir(brandingDirectory())
            .filePath(QStringLiteral("jianku-mark-") + QString::number(size) + QStringLiteral(".png"));
        if (QFileInfo::exists(path)) {
            QIcon icon;
            icon.addFile(path, QSize(size, size));
            return icon;
        }
    }
    return iconFromFile(QStringLiteral("jianku-mark-128.png"));
}

QIcon menuBarIcon() {
    const QString path = QDir(brandingDirectory()).filePath(QStringLiteral("jianku-menubar.png"));
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    QIcon icon;
    icon.addFile(path, QSize(22, 22));
    // Tells macOS to treat it as a template so it inverts with the menu bar. Qt maps
    // QIcon::isMask() onto the NSImage template flag.
    icon.setIsMask(true);
    return icon;
}

} // namespace Branding
