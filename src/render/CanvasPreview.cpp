#include "CanvasPreview.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace Render {

CanvasPreview::CanvasPreview(QObject *parent) : QObject(parent) {
    // Built-in backgrounds: the bundle when running from one, else the source tree.
    const QString bundled = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../Resources/backgrounds"));
    if (QDir(bundled).exists())
        backgroundRoot_ = QDir(bundled).absolutePath();
    else
        backgroundRoot_ = QStringLiteral(JIANKU_SOURCE_DIR "/assets/backgrounds");
    update({}, sourceWidth_, sourceHeight_);
}

QRectF CanvasPreview::fraction(const QRectF &rect) const {
    if (!plan_.valid || plan_.width() <= 0 || plan_.height() <= 0)
        return {};
    // Fractions of the canvas, which the preview maps onto its own stage rect.
    return QRectF(rect.x() / plan_.width(), rect.y() / plan_.height(),
        rect.width() / plan_.width(), rect.height() / plan_.height());
}

double CanvasPreview::radiusRatio() const {
    if (!plan_.valid || plan_.height() <= 0)
        return 0.0;
    // Corners are drawn on the stage, so the radius has to scale with it too.
    return style_.radius / plan_.height();
}

double CanvasPreview::insetRatio() const {
    if (!plan_.valid || plan_.height() <= 0)
        return 0.0;
    return style_.insetSize / plan_.height();
}

double CanvasPreview::aspect() const {
    if (plan_.valid && plan_.height() > 0)
        return plan_.width() / plan_.height();
    return sourceHeight_ > 0 ? sourceWidth_ / sourceHeight_ : 16.0 / 9.0;
}

double CanvasPreview::backgroundBlur() const {
    return std::clamp(style_.backgroundBlur / 40.0, 0.0, 1.0);
}

QString CanvasPreview::backgroundUrl(const QString &name) const {
    if (name.isEmpty())
        return {};
    const QStringList candidates{backgroundRoot_ + QLatin1Char('/') + name,
        backgroundRoot_ + QStringLiteral("/backgrounds/") + name};
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QStringLiteral("file://") + candidate;
    }
    return {};
}

void CanvasPreview::update(const QVariantMap &settings, double sourceWidth, double sourceHeight) {
    if (sourceWidth > 0.0)
        sourceWidth_ = sourceWidth;
    if (sourceHeight > 0.0)
        sourceHeight_ = sourceHeight;
    style_ = canvasStyleFromMap(settings, backgroundRoot_);

    // Aspect ratio: the export picks its canvas from the source and the requested
    // ratio, and the preview has to show that same canvas.
    const QSizeF canvas = canvasSizeForAspect(QSizeF(sourceWidth_, sourceHeight_),
        settings.value(QStringLiteral("outputAspectRatio"),
            QStringLiteral("auto")).toString());
    plan_ = planCanvas(style_, canvas, QSizeF(sourceWidth_, sourceHeight_));
    emit changed();
}

} // namespace Render
