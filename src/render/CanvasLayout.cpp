#include "CanvasLayout.h"

#include <QString>
#include <algorithm>
#include <cmath>

namespace Render {
namespace {
double evenFloor(double value) {
    const double rounded = std::floor(value);
    return rounded - std::fmod(rounded, 2.0);
}
}

double paddingForCanvas(const QSizeF &canvas, double paddingPercent) {
    const double ratio = std::clamp(paddingPercent, 0.0, 40.0) / 100.0;
    if (ratio <= 0.0)
        return 0.0;
    const double shortSide = std::min(canvas.width(), canvas.height());
    // Never let the padding eat the whole canvas.
    return std::min(shortSide * ratio, shortSide / 2.0 - 1.0);
}

double paddingForContent(const QSizeF &content, double paddingPercent) {
    const double ratio = std::clamp(paddingPercent, 0.0, 40.0) / 100.0;
    if (ratio <= 0.0)
        return 0.0;
    const double shortSide = std::min(content.width(), content.height());
    // The canvas becomes content + 2p and the padding is measured against the
    // canvas, so p is the fixed point of p = (shortSide + 2p) * ratio.
    const double denominator = 1.0 - 2.0 * ratio;
    if (denominator <= 0.0)
        return 0.0;
    return shortSide * ratio / denominator;
}

double aspectValue(const QString &aspectRatio, const QSizeF &content) {
    if (aspectRatio == QStringLiteral("16:9"))
        return 16.0 / 9.0;
    if (aspectRatio == QStringLiteral("16:10"))
        return 16.0 / 10.0;
    if (aspectRatio == QStringLiteral("4:3"))
        return 4.0 / 3.0;
    if (aspectRatio == QStringLiteral("1:1"))
        return 1.0;
    if (aspectRatio == QStringLiteral("9:16"))
        return 9.0 / 16.0;
    if (aspectRatio == QStringLiteral("21:9"))
        return 21.0 / 9.0;
    if (content.width() > 0.0 && content.height() > 0.0)
        return content.width() / content.height();
    return 16.0 / 9.0;
}

QSizeF canvasSizeForAspect(const QSizeF &content, const QString &aspectRatio) {
    const double sourceWidth = std::max(1.0, content.width());
    const double sourceHeight = std::max(1.0, content.height());
    if (aspectRatio.isEmpty() || aspectRatio == QStringLiteral("auto"))
        return {evenFloor(sourceWidth), evenFloor(sourceHeight)};

    const double target = aspectValue(aspectRatio, content);
    // Keep the source resolution: grow the shorter axis to reach the ratio so
    // the source is never upscaled and never cropped.
    const double sourceAspect = sourceWidth / sourceHeight;
    double width = sourceWidth;
    double height = sourceHeight;
    if (target > sourceAspect)
        height = sourceWidth / target;
    else
        width = sourceHeight * target;
    return {evenFloor(width), evenFloor(height)};
}

CanvasLayout computeCanvasLayout(const CanvasLayoutInput &input) {
    CanvasLayout layout;
    layout.canvas = QRectF(QPointF(0.0, 0.0), input.canvas);
    if (input.canvas.width() <= 0.0 || input.canvas.height() <= 0.0
        || input.content.width() <= 0.0 || input.content.height() <= 0.0)
        return layout;

    layout.padding = paddingForCanvas(input.canvas, input.paddingPercent);
    layout.radius = std::max(0.0, input.radius);
    layout.inset = std::max(0.0, input.inset);

    layout.paddedBox = layout.canvas.adjusted(layout.padding, layout.padding,
        -layout.padding, -layout.padding);
    // Keep the padded box usable even with an extreme padding percentage.
    layout.paddedBox = QRectF(layout.paddedBox.topLeft(),
        QSizeF(std::max(1.0, layout.paddedBox.width()), std::max(1.0, layout.paddedBox.height())));

    const double insetLimit = std::min(layout.paddedBox.width(), layout.paddedBox.height()) / 2.0 - 1.0;
    layout.inset = std::min(layout.inset, std::max(0.0, insetLimit));
    layout.innerBox = layout.paddedBox.adjusted(layout.inset, layout.inset, -layout.inset, -layout.inset);

    layout.fitScale = std::min(layout.innerBox.width() / input.content.width(),
        layout.innerBox.height() / input.content.height());
    if (!std::isfinite(layout.fitScale) || layout.fitScale <= 0.0)
        return layout;

    const QSizeF contentSize(input.content.width() * layout.fitScale,
        input.content.height() * layout.fitScale);
    layout.contentRect = QRectF(QPointF(
        layout.innerBox.x() + (layout.innerBox.width() - contentSize.width()) / 2.0,
        layout.innerBox.y() + (layout.innerBox.height() - contentSize.height()) / 2.0), contentSize);
    layout.frameRect = layout.contentRect.adjusted(-layout.inset, -layout.inset,
        layout.inset, layout.inset);
    layout.valid = true;
    return layout;
}

} // namespace Render
