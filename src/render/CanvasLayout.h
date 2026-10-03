#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace Render {

// One layout rule shared by the preview, the quick screenshot and the offline
// compositor. Before this existed there were three copies of the padding maths
// and they disagreed (docs/项目目标.md §10.1): the preview took a percentage of
// each canvas axis, the screenshot took a percentage of the source, and the
// rounded corners were not clipped the same way.
//
// The rule, stated once:
//
//   * padding is a percentage of the *final canvas short side*
//   * the source is contained (never stretched, never cropped) inside the
//     padded box, minus any inset
//   * the rounded corners and the inset belong to the outer frame, which is the
//     contained content grown back by the inset
//
// When the canvas is fixed in advance (preview, export) the padding is simply
// `min(canvas) * ratio`. When the canvas is derived from the content (quick
// screenshot grows the image to make room) the same rule is solved the other
// way round: `p = min(content) * ratio / (1 - 2 * ratio)`, which is exactly what
// the screenshot path already did.

struct CanvasLayoutInput {
    QSizeF canvas;         // output canvas in pixels
    QSizeF content;        // captured source in pixels
    double paddingPercent = 0.0;
    double radius = 0.0;   // outer frame corner radius, canvas px
    double inset = 0.0;    // inner border thickness, canvas px
};

struct CanvasLayout {
    QRectF canvas;
    QRectF paddedBox;    // canvas deflated by the padding
    QRectF innerBox;     // paddedBox deflated by the inset
    QRectF contentRect;  // source contained in innerBox
    QRectF frameRect;    // contentRect inflated by the inset (carries radius/inset)
    double padding = 0.0;
    double radius = 0.0;
    double inset = 0.0;
    double fitScale = 1.0;   // content px -> canvas px
    bool valid = false;
};

// Padding in canvas pixels for a canvas whose size is already known.
double paddingForCanvas(const QSizeF &canvas, double paddingPercent);

// Padding in content pixels for a canvas that is grown from the content.
double paddingForContent(const QSizeF &content, double paddingPercent);

CanvasLayout computeCanvasLayout(const CanvasLayoutInput &input);

// Output canvas size for a requested aspect ratio ("auto" keeps the source).
// Sizes are rounded to even numbers so 4:2:0 encoders accept them.
QSizeF canvasSizeForAspect(const QSizeF &content, const QString &aspectRatio);

double aspectValue(const QString &aspectRatio, const QSizeF &content);

} // namespace Render
