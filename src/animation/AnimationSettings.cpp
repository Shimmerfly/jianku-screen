#include "AnimationSettings.h"

namespace Animation {
namespace {
SpringConfig springFrom(const QVariantMap &map, const QString &key, const SpringConfig &fallback) {
    const QVariantMap value = map.value(key).toMap();
    if (value.isEmpty())
        return fallback;
    SpringConfig config = fallback;
    if (value.contains("stiffness"))
        config.stiffness = value.value("stiffness").toDouble();
    if (value.contains("damping"))
        config.damping = value.value("damping").toDouble();
    if (value.contains("mass"))
        config.mass = value.value("mass").toDouble();
    return config;
}
} // namespace

DriverSettings driverSettingsFromMap(const QVariantMap &settings, const DriverSettings &base) {
    DriverSettings result = base;
    result.cursor.movement = springFrom(settings, "mouseMovementSpring", result.cursor.movement);

    // Smoothing stays on unless the explicit "关闭平滑" switch is set or the preset
    // picker is on "None" (the reference treats that preset as smoothing off).
    const bool explicitOff = settings.value("disableMouseMovementSpring").toBool();
    const bool presetOff = settings.value("cursorSmoothing").toString() == QStringLiteral("None");
    result.cursor.smoothingEnabled = !explicitOff && !presetOff;

    // Only an explicit choice disables the click feedback; a missing key must not.
    if (settings.contains("clickEffect"))
        result.cursor.clickScaleEnabled = settings.value("clickEffect").toString() != QStringLiteral("none");

    if (settings.contains("cursorRotateOnXMovementRatio"))
        result.cursor.rotationRatio = settings.value("cursorRotateOnXMovementRatio").toDouble();
    if (settings.contains("hideNotMovingCursorAfterMs"))
        result.cursor.hideAfterMs = settings.value("hideNotMovingCursorAfterMs").toDouble();
    if (settings.contains("cursorBaseRotation"))
        result.cursor.baseRotationDeg = settings.value("cursorBaseRotation").toDouble();

    // `mouseClickSpring` (700/30/1) is deliberately NOT read here. The located
    // reference chain says the zoomer-and-fader call uses its own spring
    // (300/30/0.3) rather than that field, which is what `cursor.clickFader` already
    // holds. Reading it would look like fixing an unwired setting while actually
    // contradicting the evidence — see research/桌面动画处理链-官方3.7.5静态.md.
    // What `mouseClickSpring` does drive is not located yet.

    if (settings.contains("defaultZoomLevel"))
        result.zoomLevel = settings.value("defaultZoomLevel").toDouble();
    if (settings.contains("autoZoom"))
        result.autoZoom = settings.value("autoZoom").toBool();
    if (settings.contains("snapToEdgesRatio"))
        result.snapToEdgesRatio = settings.value("snapToEdgesRatio").toDouble();
    result.screenSpring = springFrom(settings, "screenMovementSpring", result.screenSpring);
    return result;
}

} // namespace Animation
