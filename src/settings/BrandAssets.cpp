#include "BrandAssets.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPixmap>
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
    // Drawn, not scaled. A menu bar template is defined by its alpha and painted in a
    // single colour by macOS, so what matters is the silhouette at 18-22 pt — and the
    // app icon's silhouette is unusable there. This draws the logo's idea directly:
    // a screen frame with a record dot, in strokes thick enough to survive the size.
    //
    // Geometry is in a 22x22 box (the menu bar height) and inset by 2 so the glyph does
    // not touch neighbouring items. `devicePixelRatio` variants are added because a
    // vector path rasterised at 1x on a Retina display is the one thing that would
    // still look soft; QIcon picks the right one.
    constexpr int kBox = 22;
    QIcon icon;
    for (const int scale : {1, 2}) {
        QPixmap pixmap(kBox * scale, kBox * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // Black: for a mask the colour is irrelevant, only the alpha is read.
        QPen pen(Qt::black);
        pen.setWidthF(2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(2.5, 4.5, 17.0, 13.0), 3.0, 3.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawEllipse(QPointF(7.5, 9.0), 1.9, 1.9);
        // The tile's diagonal ribbons, reduced to the one stroke that reads as motion
        // without closing the frame into a filled shape.
        QPen slash(Qt::black);
        slash.setWidthF(1.6);
        slash.setCapStyle(Qt::RoundCap);
        painter.setPen(slash);
        painter.drawLine(QPointF(13.5, 13.6), QPointF(16.4, 10.7));
        painter.end();
        icon.addPixmap(pixmap);
    }
    // Qt maps QIcon::isMask() onto NSImage's template flag, which is what tells macOS
    // to invert the glyph for a dark menu bar.
    icon.setIsMask(true);
    return icon;
}

} // namespace Branding
