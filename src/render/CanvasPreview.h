#pragma once

#include "CanvasRenderer.h"

#include <QObject>
#include <QRectF>
#include <QVariantMap>

namespace Render {

// Exposes the shared canvas layout to QML.
//
// The preview used to compute its own padding in QML, from a different rule than
// the screenshot and the compositor used, so the same setting produced three
// different pictures. QML cannot call into the C++ layout directly — the values are
// needed as bindable properties — so this holds the computed plan and re-derives it
// when the settings change.
//
// It reports rectangle *fractions* of the stage, not pixels: the preview draws at
// whatever size the window happens to be, while the layout is defined in output
// pixels. Keeping the ratio is exactly what makes the preview show the same
// composition as the export.
class CanvasPreview final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    // Stage fractions, all relative to the canvas (the output frame), 0..1.
    Q_PROPERTY(QRectF frameRect READ frameRect NOTIFY changed)
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY changed)
    Q_PROPERTY(double radiusRatio READ radiusRatio NOTIFY changed)
    Q_PROPERTY(double insetRatio READ insetRatio NOTIFY changed)
    // Canvas aspect ratio (width / height) for the requested output ratio.
    Q_PROPERTY(double aspect READ aspect NOTIFY changed)
    // Style, passed through for the QML that draws the background itself.
    Q_PROPERTY(QString backgroundType READ backgroundType NOTIFY changed)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY changed)
    Q_PROPERTY(QColor gradientStart READ gradientStart NOTIFY changed)
    Q_PROPERTY(QColor gradientEnd READ gradientEnd NOTIFY changed)
    Q_PROPERTY(double gradientAngle READ gradientAngle NOTIFY changed)
    Q_PROPERTY(QColor insetColor READ insetColor NOTIFY changed)
    Q_PROPERTY(double insetAlpha READ insetAlpha NOTIFY changed)
    Q_PROPERTY(bool hasShadow READ hasShadow NOTIFY changed)
    // Background blur as MultiEffect's 0..1 expects it. The reference's 0..40
    // slider maps onto a radius; the exact curve is not verified, so the same
    // proportional mapping the compositor uses is applied here, which at least
    // keeps preview and export equal.
    Q_PROPERTY(double backgroundBlur READ backgroundBlur NOTIFY changed)

public:
    explicit CanvasPreview(QObject *parent = nullptr);

    bool valid() const { return plan_.valid; }
    QRectF frameRect() const { return fraction(plan_.layout.frameRect); }
    QRectF contentRect() const { return fraction(plan_.layout.contentRect); }
    double radiusRatio() const;
    double insetRatio() const;
    double aspect() const;
    QString backgroundType() const { return style_.backgroundType; }
    QColor backgroundColor() const { return style_.backgroundColor; }
    QColor gradientStart() const { return style_.gradientStart; }
    QColor gradientEnd() const { return style_.gradientEnd; }
    double gradientAngle() const { return style_.gradientAngle; }
    QColor insetColor() const { return style_.insetColor; }
    double insetAlpha() const { return style_.insetAlpha; }
    bool hasShadow() const { return style_.shadowIntensity > 0.001; }
    double backgroundBlur() const;

    // Settings map plus the captured source size. Both come from QML: the settings
    // are the live current values, the source size from the driver (or the chosen
    // display before capture starts).
    Q_INVOKABLE void update(const QVariantMap &settings, double sourceWidth, double sourceHeight);
    Q_INVOKABLE QString backgroundUrl(const QString &name) const;

signals:
    void changed();

private:
    QRectF fraction(const QRectF &rect) const;
    // Keeps the last source size so a settings-only update can re-plan.
    double sourceWidth_ = 1920.0;
    double sourceHeight_ = 1080.0;
    QString backgroundRoot_;
    CanvasStyle style_;
    CanvasPlan plan_;
};

} // namespace Render
