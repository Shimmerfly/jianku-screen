#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <cstdint>

namespace Capture {

// What is being captured. The three kinds share one code path everywhere else:
// a source always resolves to "a rectangle of the global point space recorded at
// a known pixel size", so the pointer recorder, the animation engine and the
// compositor need no per-kind branching.
enum class SourceKind { Display, Window, Region };

QString sourceKindName(SourceKind kind);
SourceKind sourceKindFromName(const QString &name, bool *ok = nullptr);

struct CaptureSource {
    SourceKind kind = SourceKind::Display;
    // Display the source lives on. Also the clock the cursor sampler follows, so
    // it is required for every kind, not just Display.
    std::uint32_t displayId = 0;
    // Window id for Window sources (SCWindow.windowID).
    std::uint32_t windowId = 0;
    // Global points. For Region this is the recorded rect itself; for Window it
    // is filled in from the window when the source is resolved, so the manifest
    // always states the rect that was actually recorded even if the window moves.
    QRectF regionPoints;
    // Human-readable name recorded for diagnostics ("显示器 1", a window title…).
    QString label;

    bool operator==(const CaptureSource &other) const {
        return kind == other.kind && displayId == other.displayId && windowId == other.windowId
            && regionPoints == other.regionPoints && label == other.label;
    }

    QJsonObject toJson() const;
    static CaptureSource fromJson(const QJsonObject &object, bool *ok = nullptr);
};

// Everything about a source that has been resolved against the live system.
struct CaptureGeometry {
    bool valid = false;
    QString error;
    QSize pixelSize;          // frames arrive at exactly this size
    QRectF boundsPoints;      // where the source sits in global points
    double pointPixelScale = 1.0;
};

// Resolves a source into frame size and global bounds.
//
//   displayBoundsPoints / displayPixelSize  the display the source is on
//   windowFramePoints                       SCWindow.frame, Window sources only
//
// Pixel dimensions are rounded and forced even: H.264 with 4:2:0 chroma cannot
// encode an odd width or height, and a stream configured with odd dimensions
// fails at the writer instead of at the source.
CaptureGeometry resolveGeometry(const CaptureSource &source, const QRectF &displayBoundsPoints,
    const QSize &displayPixelSize, const QRectF &windowFramePoints = {});

// Nearest even integer that is at least 2.
int evenExtent(double points);

// The display whose frame contains the point, or -1 when there are no displays.
int displayIndexContaining(const QPointF &point, const QList<QRectF> &displayFrames);

} // namespace Capture
