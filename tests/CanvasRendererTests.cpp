// Canvas renderer checks.
//
// The preview, the quick screenshot and the offline compositor all draw the same
// packaging now, and this is where that claim is checked rather than asserted. The
// three used to disagree in ways a user could see: the screenshot knew only about
// a flat colour, so a gradient or built-in background produced a PNG that looked
// nothing like the preview, and the padding was derived from the canvas in two
// paths and from the source in the third, so "10% padding" meant two different
// widths depending on which output you looked at.
#include "../src/render/CanvasRenderer.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QVariantMap>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Render;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(double a, double b, double epsilon = 1e-6) {
    return std::abs(a - b) <= epsilon;
}

QVariantMap baseSettings() {
    return QVariantMap{
        {QStringLiteral("backgroundType"), QStringLiteral("color")},
        {QStringLiteral("backgroundColor"), QStringLiteral("#204060")},
        {QStringLiteral("backgroundPaddingRatio"), 10.0},
        {QStringLiteral("windowBorderRadius"), 12.0},
        {QStringLiteral("insetSize"), 0.0},
        {QStringLiteral("shadowIntensity"), 0.0},
        {QStringLiteral("backgroundBlur"), 0.0}};
}

// Paints the content exactly the way the compositor and the screenshot do.
QImage renderCanvas(const CanvasPlan &plan, const QColor &contentColour) {
    QImage result(plan.width(), plan.height(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    drawCanvasBackdrop(painter, plan);
    beginFrame(painter, plan, QColor(QString::fromLatin1(kFrameColor)));
    const QRectF content = plan.layout.contentRect.translated(-plan.layout.frameRect.topLeft());
    QImage source(qMax(2, int(content.width())), qMax(2, int(content.height())),
        QImage::Format_ARGB32_Premultiplied);
    source.fill(contentColour);
    painter.drawImage(content, source);
    drawInsetBorder(painter, plan);
    endFrame(painter, plan);
    painter.end();
    return result;
}
} // namespace

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        // --- the padding rule is the same whichever way the canvas is derived ---
        // This is the disagreement that made the preview and the screenshot differ.
        {
            const QSizeF content(1000.0, 500.0);
            const QVariantMap settings = baseSettings();
            const CanvasStyle style = canvasStyleFromMap(settings);

            // Screenshot: the canvas grows from the content.
            const CanvasPlan grown = planCanvas(style, QSizeF(), content);
            require(grown.valid, "a content-derived canvas plans");
            require(close(grown.layout.padding, 62.5, 1e-6),
                "padding solves p = min(content) * r / (1 - 2r)");
            // 500 * 0.1 / 0.8 = 62.5, so the canvas is 1125 x 625 (odd, rounded by
            // the renderer, but the padding itself is exact).
            require(close(grown.canvasSize.width(), 1125.0), "canvas grows by 2p");
            require(close(grown.layout.frameRect.width(), 1000.0),
                "the frame is exactly the content when there is no inset");

            // Export: the canvas is fixed, and the padding is a share of it.
            const CanvasPlan fixed = planCanvas(style, QSizeF(1875.0, 937.5), content);
            require(fixed.valid, "a fixed canvas plans");
            require(close(fixed.layout.padding, 93.75, 1e-6),
                "padding is min(canvas) * r when the canvas is known");

            // The two are different absolute paddings — that is expected and not a
            // bug — but the *proportion of the canvas* must come out the same, or
            // the preview and the finished file would frame the source differently.
            const double grownShare = grown.layout.padding / grown.canvasSize.height();
            const double fixedShare = fixed.layout.padding / fixed.canvasSize.height();
            require(close(grownShare, fixedShare, 1e-6),
                "both paths leave the same share of the canvas as padding");
        }

        // --- aspect ratios ---------------------------------------------------
        {
            const QSizeF content(3360.0, 2100.0);
            const QVariantMap settings = baseSettings();
            const CanvasStyle style = canvasStyleFromMap(settings);
            const CanvasPlan wide = planCanvas(style, canvasSizeForAspect(content,
                QStringLiteral("9:16")), content);
            require(wide.valid, "a portrait canvas plans");
            require(wide.height() > wide.width(), "9:16 is taller than it is wide");
            require(wide.layout.contentRect.width() <= wide.layout.innerBox.width() + 1e-6
                    && wide.layout.contentRect.height() <= wide.layout.innerBox.height() + 1e-6,
                "the source is contained, never overflowing the padded box");
            // Never upscaled past the canvas, never cropped: the source keeps its
            // own aspect ratio inside a differently-shaped canvas.
            const double sourceAspect = content.width() / content.height();
            const double fittedAspect = wide.layout.contentRect.width()
                / wide.layout.contentRect.height();
            require(close(sourceAspect, fittedAspect, 1e-6),
                "a differently-shaped canvas does not stretch the source");
        }

        // --- the background reaches the canvas edge --------------------------
        {
            const QVariantMap settings = baseSettings();
            CanvasStyle style = canvasStyleFromMap(settings);
            require(style.backgroundColor == QColor("#204060"), "the colour is read");
            style.paddingPercent = 12.0;
            const CanvasPlan plan = planCanvas(style, QSizeF(1200.0, 700.0), QSizeF(1000.0, 500.0));
            require(plan.valid, "plan builds");
            const QImage canvas = renderCanvas(plan, QColor(0, 200, 0));
            require(canvas.pixelColor(1, 1) == QColor("#204060"),
                "the background fills the corner outside the frame");
            require(canvas.pixelColor(canvas.width() / 2, canvas.height() / 2) == QColor(0, 200, 0),
                "the source is drawn in the middle");
        }

        // --- a gradient background is not a flat colour ----------------------
        // The screenshot path used to force a flat colour whatever the setting
        // said; this is the check that would have caught it.
        {
            QVariantMap settings = baseSettings();
            settings[QStringLiteral("backgroundType")] = QStringLiteral("gradient");
            settings[QStringLiteral("gradientStartColor")] = QStringLiteral("#FF0000");
            settings[QStringLiteral("gradientEndColor")] = QStringLiteral("#0000FF");
            settings[QStringLiteral("gradientAngle")] = 135.0;
            const CanvasPlan plan = planCanvas(canvasStyleFromMap(settings),
                QSizeF(1200.0, 700.0), QSizeF(1000.0, 500.0));
            require(plan.valid, "gradient plan builds");
            const QImage canvas = renderCanvas(plan, QColor(0, 200, 0));
            // The preview draws its gradient as a rotated square with QML's
            // clockwise rotation; at 135° that puts the start colour at the
            // top-right and the end colour at the bottom-left. Matching that
            // convention is the point — the two must not mirror each other.
            const QColor topRight = canvas.pixelColor(canvas.width() - 3, 3);
            const QColor bottomLeft = canvas.pixelColor(3, canvas.height() - 4);
            require(topRight != bottomLeft, "a gradient background varies across the canvas");
            require(topRight.red() > topRight.blue(), "the top-right holds the start colour");
            require(bottomLeft.blue() > bottomLeft.red(), "the bottom-left holds the end colour");

            // At 0° the gradient runs horizontally, start on the left.
            settings[QStringLiteral("gradientAngle")] = 0.0;
            const CanvasPlan horizontal = planCanvas(canvasStyleFromMap(settings),
                QSizeF(1200.0, 700.0), QSizeF(1000.0, 500.0));
            const QImage flat = renderCanvas(horizontal, QColor(0, 200, 0));
            const QColor left = flat.pixelColor(2, flat.height() / 2);
            const QColor right = flat.pixelColor(flat.width() - 3, flat.height() / 2);
            require(left.red() > left.blue(), "at 0° the start colour is on the left");
            require(right.blue() > right.red(), "and the end colour on the right");
        }

        // --- radius and inset reach the drawn frame --------------------------
        {
            QVariantMap settings = baseSettings();
            settings[QStringLiteral("windowBorderRadius")] = 40.0;
            settings[QStringLiteral("insetSize")] = 6.0;
            settings[QStringLiteral("insetColor")] = QStringLiteral("#FF00FF");
            settings[QStringLiteral("insetAlpha")] = 1.0;
            const CanvasPlan plan = planCanvas(canvasStyleFromMap(settings),
                QSizeF(1200.0, 700.0), QSizeF(1000.0, 500.0));
            require(plan.valid, "rounded plan builds");
            const QImage canvas = renderCanvas(plan, QColor(255, 255, 255));
            // The rounded corner leaves background visible just inside the frame.
            const QPointF frameCorner = plan.layout.frameRect.topLeft();
            require(canvas.pixelColor(int(frameCorner.x()), int(frameCorner.y()))
                    != QColor(255, 255, 255),
                "the rounded corner does not paint into its own square");
            // The inset border runs along the frame edge, a few pixels in.
            const QColor border = canvas.pixelColor(int(frameCorner.x() + 3),
                int(frameCorner.y() + plan.layout.frameRect.height() / 2));
            require(border.magenta() > 200 && border.red() > 200 && border.green() < 60,
                "the inset border is drawn on the frame edge in its configured colour");
        }

        // --- shadow ----------------------------------------------------------
        {
            QVariantMap settings = baseSettings();
            settings[QStringLiteral("shadowIntensity")] = 0.75;
            settings[QStringLiteral("shadowBlur")] = 20.0;
            settings[QStringLiteral("shadowDistance")] = 25.0;
            settings[QStringLiteral("shadowAngle")] = 90.0;
            const CanvasStyle style = canvasStyleFromMap(settings);
            require(style.shadowIntensity > 0.0, "shadow settings are read");
            const CanvasPlan plan = planCanvas(style, QSizeF(1200.0, 700.0), QSizeF(1000.0, 500.0));
            require(plan.valid && !plan.shadow.isNull(), "a shadow sprite is built");
            const QImage canvas = renderCanvas(plan, QColor(255, 255, 255));
            // Angle 90° puts the shadow below the frame, so the pixels just under
            // the frame are darker than the plain background would be.
            const QColor plain = QColor("#204060");
            const QColor shaded = canvas.pixelColor(canvas.width() / 2,
                int(plan.layout.frameRect.bottom()) + 12);
            require(shaded.lightness() < plain.lightness(),
                "the shadow darkens the pixels in its direction");

            // With the intensity at zero nothing is drawn: the same pixel keeps the
            // background colour exactly.
            QVariantMap noShadow = settings;
            noShadow[QStringLiteral("shadowIntensity")] = 0.0;
            const CanvasPlan plainPlan = planCanvas(canvasStyleFromMap(noShadow),
                QSizeF(1200.0, 700.0), QSizeF(1000.0, 500.0));
            require(plainPlan.shadow.isNull(), "no shadow sprite without intensity");
        }

        // --- degenerate input -------------------------------------------------
        {
            const CanvasStyle style = canvasStyleFromMap(baseSettings());
            require(!planCanvas(style, QSizeF(1200.0, 700.0), QSizeF()).valid,
                "a zero content size is refused");
            require(!planCanvas(style, QSizeF(1200.0, 700.0), QSizeF(-5.0, 100.0)).valid,
                "a negative content size is refused");
            // A padding over 50% cannot be solved from the content: there would be
            // no room left for the source. It must not divide by zero.
            CanvasStyle extreme = style;
            extreme.paddingPercent = 60.0;
            const CanvasPlan plan = planCanvas(extreme, QSizeF(), QSizeF(1000.0, 500.0));
            require(plan.valid, "an extreme padding still plans");
            require(plan.layout.contentRect.width() > 0.0 && plan.layout.contentRect.height() > 0.0,
                "and still leaves a source rectangle");
        }

        // --- the built-in background name resolves to a real file ------------
        {
            QVariantMap settings = baseSettings();
            settings[QStringLiteral("backgroundType")] = QStringLiteral("system");
            settings[QStringLiteral("backgroundSystemName")] = QStringLiteral("macOS/tahoe-light.jpg");
            CanvasStyle style = canvasStyleFromMap(settings,
                QStringLiteral(JIANKU_SOURCE_DIR "/assets/backgrounds"));
            require(!style.backgroundImagePath.isEmpty(),
                "the configured built-in background is found");
            settings[QStringLiteral("backgroundSystemName")] = QStringLiteral("nope/missing.jpg");
            style = canvasStyleFromMap(settings, QStringLiteral(JIANKU_SOURCE_DIR "/assets/backgrounds"));
            require(style.backgroundImagePath.isEmpty(),
                "a missing built-in background resolves to nothing instead of a stale path");
        }

        std::cout << "canvas renderer checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
