// Capture source geometry.
//
// The source description is the contract between a recording and everything that
// reads it later: frames, pointer coordinates and the compositor all derive their
// mapping from `widthPx/heightPx` plus `globalBoundsPoints`. Getting this wrong
// for window and region sources puts every pointer event in the wrong place, so
// the geometry rules are pinned here rather than discovered in a finished film.
#include "../src/capture/CaptureSource.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <iostream>
#include <stdexcept>

using namespace Capture;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(double a, double b, double epsilon = 1e-6) {
    return std::abs(a - b) <= epsilon;
}
const QRectF kDisplay(0.0, 0.0, 1512.0, 982.0);   // 1512x982 points
const QSize kPixels(3024, 1964);                  // exactly 2x, a Retina display
} // namespace

int main() {
    try {
        // --- display --------------------------------------------------------
        {
            CaptureSource source;
            source.kind = SourceKind::Display;
            source.displayId = 1;
            const CaptureGeometry geometry = resolveGeometry(source, kDisplay, kPixels);
            require(geometry.valid, "a display source resolves");
            require(geometry.pixelSize == kPixels, "a display captures its full pixel size");
            require(geometry.boundsPoints == kDisplay, "a display is its own bounds");
            require(close(geometry.pointPixelScale, 2.0), "the scale is pixels per point");
        }

        // --- region ---------------------------------------------------------
        {
            CaptureSource source;
            source.kind = SourceKind::Region;
            // 400x300 points at the top-left quadrant.
            source.regionPoints = QRectF(100.0, 50.0, 400.0, 300.0);
            const CaptureGeometry geometry = resolveGeometry(source, kDisplay, kPixels);
            require(geometry.valid, "a region source resolves");
            require(geometry.pixelSize == QSize(800, 600), "a region scales to pixels");
            require(geometry.boundsPoints == source.regionPoints,
                "the region rect is reported back unchanged");
        }

        // --- region partly off the display ----------------------------------
        {
            CaptureSource source;
            source.kind = SourceKind::Region;
            // Half the region hangs off the right edge of the display.
            source.regionPoints = QRectF(1400.0, 100.0, 400.0, 200.0);
            const CaptureGeometry geometry = resolveGeometry(source, kDisplay, kPixels);
            require(geometry.valid, "a region that overlaps the display still resolves");
            require(close(geometry.boundsPoints.width(), 112.0),
                "the recorded rect is the part that is actually on the display");
            require(geometry.pixelSize == QSize(224, 400),
                "the pixel size follows the clipped rect, not the requested one");
            require(geometry.boundsPoints.right() <= kDisplay.right() + 1e-6,
                "the recorded rect never claims pixels outside the display");
        }

        // --- region entirely off the display --------------------------------
        {
            CaptureSource source;
            source.kind = SourceKind::Region;
            source.regionPoints = QRectF(5000.0, 5000.0, 100.0, 100.0);
            const CaptureGeometry geometry = resolveGeometry(source, kDisplay, kPixels);
            require(!geometry.valid, "a region off the display is refused");
            require(!geometry.error.isEmpty(), "the refusal explains itself");
        }

        // --- window ---------------------------------------------------------
        {
            CaptureSource source;
            source.kind = SourceKind::Window;
            source.windowId = 42;
            const QRectF windowFrame(300.0, 200.0, 800.0, 600.0);
            const CaptureGeometry geometry = resolveGeometry(source, kDisplay, kPixels, windowFrame);
            require(geometry.valid, "a window source resolves");
            require(geometry.pixelSize == QSize(1600, 1200), "a window scales to pixels");
            require(geometry.boundsPoints == windowFrame,
                "a window keeps its own frame in the manifest");
        }

        // A window may hang over the edge of the display it sits on. ScreenCaptureKit
        // still hands back the whole window, so the geometry must not clip it: a
        // clipped size would make every frame the wrong shape for the writer.
        {
            CaptureSource source;
            source.kind = SourceKind::Window;
            const QRectF overhanging(1200.0, 700.0, 800.0, 600.0);   // crosses both edges
            const CaptureGeometry geometry = resolveGeometry(source, kDisplay, kPixels, overhanging);
            require(geometry.valid, "a window overhanging the display resolves");
            require(geometry.boundsPoints == overhanging, "a window frame is used unclipped");
            require(geometry.pixelSize == QSize(1600, 1200),
                "an overhanging window keeps its full pixel size");
        }

        {
            CaptureSource source;
            source.kind = SourceKind::Window;
            const CaptureGeometry geometry = resolveGeometry(source, kDisplay, kPixels, QRectF());
            require(!geometry.valid, "a window without a frame is refused");
        }

        // --- sizes are forced even ------------------------------------------
        // H.264 with 4:2:0 chroma cannot encode an odd width or height; a stream
        // configured with one fails later at the writer instead of here.
        {
            CaptureSource source;
            source.kind = SourceKind::Region;
            source.regionPoints = QRectF(10.0, 10.0, 101.0, 77.0);
            const CaptureGeometry geometry = resolveGeometry(source, kDisplay, kPixels);
            require(geometry.valid, "an odd-sized region resolves");
            require(geometry.pixelSize.width() % 2 == 0 && geometry.pixelSize.height() % 2 == 0,
                "the configured pixel size is even");
            require(geometry.pixelSize == QSize(202, 154), "and rounds to the nearest even size");

            // 101 * 2 = 202 exactly; take a value that must round down.
            require(evenExtent(155.0) == 154, "an odd pixel count rounds down to even");
            require(evenExtent(1.0) == 2, "a degenerate size is clamped, never zero");
            require(evenExtent(0.0) == 2, "a zero size is clamped");
            require(evenExtent(-5.0) == 2, "a negative size is clamped");
        }

        // --- degenerate inputs ----------------------------------------------
        {
            CaptureSource source;
            require(!resolveGeometry(source, QRectF(), kPixels).valid,
                "a display without bounds is refused");
            require(!resolveGeometry(source, kDisplay, QSize()).valid,
                "a display without pixels is refused");
        }

        // --- serialisation round trip ---------------------------------------
        {
            CaptureSource display;
            display.kind = SourceKind::Display;
            display.displayId = 7;
            display.label = QStringLiteral("显示器 7");
            const QJsonObject displayJson = display.toJson();
            require(displayJson.value(QStringLiteral("type")).toString() == QLatin1String("display"),
                "a display serialises its kind");
            require(!displayJson.contains(QStringLiteral("regionPoints")),
                "a display does not carry a region rect");

            bool ok = false;
            CaptureSource restored = CaptureSource::fromJson(displayJson, &ok);
            require(ok, "a display manifest parses");
            require(restored == display, "a display survives a round trip");

            CaptureSource window;
            window.kind = SourceKind::Window;
            window.displayId = 3;
            window.windowId = 991;
            window.regionPoints = QRectF(10.0, 20.0, 300.0, 200.0);
            window.label = QStringLiteral("Safari · 文档");
            restored = CaptureSource::fromJson(window.toJson(), &ok);
            require(ok, "a window manifest parses");
            require(restored == window, "a window survives a round trip");
            require(restored.windowId == 991, "the window id survives");

            CaptureSource region;
            region.kind = SourceKind::Region;
            region.displayId = 3;
            region.regionPoints = QRectF(1.5, 2.5, 640.0, 480.0);
            restored = CaptureSource::fromJson(region.toJson(), &ok);
            require(ok, "a region manifest parses");
            require(restored == region, "a region survives a round trip");

            // An unknown kind must not silently become a display-source recording
            // of the wrong thing.
            CaptureSource::fromJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("hologram")}}, &ok);
            require(!ok, "an unknown source kind is rejected");

            // A region with no rectangle is not a region.
            CaptureSource::fromJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("region")}}, &ok);
            require(!ok, "a region without a rect is rejected");

            // A manifest from before window/region existed.
            restored = CaptureSource::fromJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("display")},
                {QStringLiteral("displayId"), 2}}, &ok);
            require(ok && restored.kind == SourceKind::Display && restored.displayId == 2,
                "an older display manifest still loads");
        }

        // --- kind names ------------------------------------------------------
        {
            require(sourceKindName(SourceKind::Display) == QLatin1String("display"), "display name");
            require(sourceKindName(SourceKind::Window) == QLatin1String("window"), "window name");
            require(sourceKindName(SourceKind::Region) == QLatin1String("region"), "region name");
            bool ok = true;
            require(sourceKindFromName(QStringLiteral("window"), &ok) == SourceKind::Window && ok,
                "window parses");
            require(sourceKindFromName(QString(), &ok) == SourceKind::Display && ok,
                "an absent kind defaults to display, like older manifests");
            sourceKindFromName(QStringLiteral("nope"), &ok);
            require(!ok, "an unknown name reports failure");
        }

        // --- display selection ----------------------------------------------
        {
            const QList<QRectF> displays{QRectF(0.0, 0.0, 1512.0, 982.0),
                QRectF(1512.0, 0.0, 1920.0, 1080.0)};
            require(displayIndexContaining(QPointF(100.0, 100.0), displays) == 0,
                "a point on the first display");
            require(displayIndexContaining(QPointF(2000.0, 500.0), displays) == 1,
                "a point on the second display");
            // Exactly on the shared edge belongs to the second display, which is
            // where the cursor is really drawn.
            require(displayIndexContaining(QPointF(1512.0, 400.0), displays) == 1,
                "the shared edge resolves to the right-hand display");
            // Below both displays: nearest by distance, not "no display".
            require(displayIndexContaining(QPointF(100.0, 2000.0), displays) == 0,
                "a point below the displays falls back to the nearest one");
            require(displayIndexContaining(QPointF(100.0, 100.0), QList<QRectF>()) == -1,
                "no displays at all reports -1");
        }

        // --- pointer mapping -------------------------------------------------
        // The recorder turns a global mouse position into frame pixels with
        //     (point - bounds.origin) * pixelSize / bounds.size
        // If `bounds` were the display while the stream captured a region, every
        // event of that recording would be off by the region's offset and scale —
        // invisible in the manifest, obvious in the finished film. This pins the
        // mapping the recorder relies on.
        {
            auto toFrame = [](const QPointF &point, const QRectF &bounds, const QSize &pixels) {
                return QPointF((point.x() - bounds.x()) * pixels.width() / bounds.width(),
                    (point.y() - bounds.y()) * pixels.height() / bounds.height());
            };

            CaptureSource display;
            display.kind = SourceKind::Display;
            const CaptureGeometry displayGeometry = resolveGeometry(display, kDisplay, kPixels);

            CaptureSource region;
            region.kind = SourceKind::Region;
            region.regionPoints = QRectF(400.0, 200.0, 500.0, 400.0);
            const CaptureGeometry regionGeometry = resolveGeometry(region, kDisplay, kPixels);
            require(regionGeometry.valid, "the region resolves");

            // The region's top-left corner is pixel (0,0) of the region recording.
            const QPointF corner = toFrame(regionGeometry.boundsPoints.topLeft(),
                regionGeometry.boundsPoints, regionGeometry.pixelSize);
            require(close(corner.x(), 0.0) && close(corner.y(), 0.0),
                "the region's own origin maps to the frame origin");

            // The same global point sits at a different frame position in the two
            // recordings, and each is correct for its own bounds.
            const QPointF probe(650.0, 400.0);   // centre of the region
            const QPointF inRegion = toFrame(probe, regionGeometry.boundsPoints, regionGeometry.pixelSize);
            require(close(inRegion.x(), regionGeometry.pixelSize.width() / 2.0)
                    && close(inRegion.y(), regionGeometry.pixelSize.height() / 2.0),
                "the region's centre lands at the centre of the region frame");

            const QPointF inDisplay = toFrame(probe, displayGeometry.boundsPoints,
                displayGeometry.pixelSize);
            require(!close(inDisplay.x(), inRegion.x()),
                "using the display bounds instead would place the event somewhere else");
            require(close(inDisplay.x(), 1300.0) && close(inDisplay.y(), 800.0),
                "and the display mapping is the one that is correct for a display recording");

            // A point outside the region is outside the frame: negative or past the
            // edge. The animation engine filters those out via `insideDisplay`.
            const QPointF outside = toFrame(QPointF(100.0, 100.0),
                regionGeometry.boundsPoints, regionGeometry.pixelSize);
            require(outside.x() < 0.0 && outside.y() < 0.0,
                "a point left of the region maps off the frame, so it can be filtered");
        }

        std::cout << "capture source checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
