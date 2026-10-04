#include "CaptureSource.h"

#include <QList>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace Capture {

QString sourceKindName(SourceKind kind) {
    switch (kind) {
    case SourceKind::Window: return QStringLiteral("window");
    case SourceKind::Region: return QStringLiteral("region");
    case SourceKind::Display: break;
    }
    return QStringLiteral("display");
}

SourceKind sourceKindFromName(const QString &name, bool *ok) {
    if (ok)
        *ok = true;
    if (name == QLatin1String("window")) return SourceKind::Window;
    if (name == QLatin1String("region")) return SourceKind::Region;
    if (name == QLatin1String("display") || name.isEmpty()) return SourceKind::Display;
    if (ok)
        *ok = false;
    return SourceKind::Display;
}

QJsonObject CaptureSource::toJson() const {
    QJsonObject object{{QStringLiteral("type"), sourceKindName(kind)},
        {QStringLiteral("displayId"), static_cast<int>(displayId)},
        {QStringLiteral("label"), label}};
    if (kind == SourceKind::Window)
        object.insert(QStringLiteral("windowId"), static_cast<int>(windowId));
    if (kind != SourceKind::Display) {
        object.insert(QStringLiteral("regionPoints"),
            QJsonObject{{QStringLiteral("x"), regionPoints.x()}, {QStringLiteral("y"), regionPoints.y()},
                {QStringLiteral("width"), regionPoints.width()},
                {QStringLiteral("height"), regionPoints.height()}});
    }
    return object;
}

CaptureSource CaptureSource::fromJson(const QJsonObject &object, bool *ok) {
    CaptureSource source;
    bool kindOk = false;
    source.kind = sourceKindFromName(object.value(QStringLiteral("type")).toString(), &kindOk);
    source.displayId = static_cast<std::uint32_t>(object.value(QStringLiteral("displayId")).toInt());
    source.windowId = static_cast<std::uint32_t>(object.value(QStringLiteral("windowId")).toInt());
    source.label = object.value(QStringLiteral("label")).toString();
    const QJsonObject region = object.value(QStringLiteral("regionPoints")).toObject();
    if (!region.isEmpty()) {
        source.regionPoints = QRectF(region.value(QStringLiteral("x")).toDouble(),
            region.value(QStringLiteral("y")).toDouble(),
            region.value(QStringLiteral("width")).toDouble(),
            region.value(QStringLiteral("height")).toDouble());
    }
    const bool geometryOk = source.kind != SourceKind::Region
        || (source.regionPoints.width() > 0.0 && source.regionPoints.height() > 0.0);
    if (ok)
        *ok = kindOk && geometryOk;
    return source;
}

QPointF pointerPixelPosition(const QPointF &globalPointAppKit, const QRectF &boundsPoints,
    const QSize &pixelSize, double primaryHeightPoints) {
    if (!(boundsPoints.width() > 0.0) || !(boundsPoints.height() > 0.0)
        || pixelSize.width() <= 0 || pixelSize.height() <= 0)
        return {};
    const double quartzY = primaryHeightPoints - globalPointAppKit.y();
    return QPointF((globalPointAppKit.x() - boundsPoints.x()) * pixelSize.width() / boundsPoints.width(),
        (quartzY - boundsPoints.y()) * pixelSize.height() / boundsPoints.height());
}

QPointF pixelToPointPosition(const QPointF &pixelPosition, const QSize &pixelSize,
    const QSizeF &pointSize) {
    if (pixelSize.width() <= 0 || pixelSize.height() <= 0
        || !(pointSize.width() > 0.0) || !(pointSize.height() > 0.0))
        return pixelPosition;
    return QPointF(pixelPosition.x() * pointSize.width() / pixelSize.width(),
        pixelPosition.y() * pointSize.height() / pixelSize.height());
}

int evenExtent(double points) {
    if (!(points > 0.0))
        return 2;
    const int rounded = static_cast<int>(std::lround(points));
    return std::max(2, rounded & ~1);
}

CaptureGeometry resolveGeometry(const CaptureSource &source, const QRectF &displayBoundsPoints,
    const QSize &displayPixelSize, const QRectF &windowFramePoints) {
    CaptureGeometry geometry;
    if (displayBoundsPoints.width() <= 0.0 || displayBoundsPoints.height() <= 0.0
        || displayPixelSize.width() <= 0 || displayPixelSize.height() <= 0) {
        geometry.error = QStringLiteral("显示器坐标无效");
        return geometry;
    }
    geometry.pointPixelScale = displayPixelSize.width() / displayBoundsPoints.width();

    // The rect actually captured, in global points.
    QRectF region;
    switch (source.kind) {
    case SourceKind::Display:
        region = displayBoundsPoints;
        break;
    case SourceKind::Window:
        // A desktop-independent window is captured as a whole, wherever it sits,
        // so its frame is used unclipped — the frame contains window pixels even
        // when part of the window hangs off the display. Clipping here would
        // promise a smaller frame than ScreenCaptureKit actually delivers.
        region = windowFramePoints;
        break;
    case SourceKind::Region:
        // A region is carved out of the display, so only the part that lies on
        // the display can ever have pixels. Clip and record the clipped rect
        // rather than promise a size the stream cannot deliver.
        region = source.regionPoints.intersected(displayBoundsPoints);
        break;
    }
    if (region.width() <= 0.0 || region.height() <= 0.0) {
        geometry.error = source.kind == SourceKind::Window
            ? QStringLiteral("窗口没有可录制的画面区域")
            : source.kind == SourceKind::Region
            ? QStringLiteral("录制区域不在所选显示器上") : QStringLiteral("录制区域无效");
        return geometry;
    }

    const QSize pixels(evenExtent(region.width() * geometry.pointPixelScale),
        evenExtent(region.height() * geometry.pointPixelScale));
    if (pixels.width() < 2 || pixels.height() < 2) {
        geometry.error = QStringLiteral("录制区域太小");
        return geometry;
    }

    geometry.pixelSize = pixels;
    geometry.boundsPoints = region;
    geometry.valid = true;
    return geometry;
}

int displayIndexContaining(const QPointF &point, const QList<QRectF> &displayFrames) {
    // Half-open on the right and bottom edges, matching CGRectContainsPoint: two
    // displays placed side by side share a coordinate, and macOS draws that pixel
    // on the right-hand one. Using QRectF::contains here would be inclusive on
    // both and hand every shared-edge point to the left display.
    for (int index = 0; index < displayFrames.size(); ++index) {
        const QRectF &frame = displayFrames.at(index);
        if (point.x() >= frame.left() && point.x() < frame.right()
            && point.y() >= frame.top() && point.y() < frame.bottom())
            return index;
    }
    // Off every display (a menu bar strip, a gap between monitors): fall back to
    // the nearest one so the recording still has a sane origin.
    int best = -1;
    double bestDistance = 0.0;
    for (int index = 0; index < displayFrames.size(); ++index) {
        const QRectF &frame = displayFrames.at(index);
        const double dx = std::max({frame.left() - point.x(), 0.0, point.x() - frame.right()});
        const double dy = std::max({frame.top() - point.y(), 0.0, point.y() - frame.bottom()});
        const double distance = dx * dx + dy * dy;
        if (best < 0 || distance < bestDistance) {
            best = index;
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace Capture
