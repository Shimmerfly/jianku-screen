#include "AnimationDriver.h"

#include "../mac/MousePoll.h"

#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
Animation::SpringConfig springFrom(const QVariantMap &map, const QString &key,
    const Animation::SpringConfig &fallback) {
    const QVariantMap value = map.value(key).toMap();
    if (value.isEmpty())
        return fallback;
    Animation::SpringConfig config = fallback;
    if (value.contains("stiffness"))
        config.stiffness = value.value("stiffness").toDouble();
    if (value.contains("damping"))
        config.damping = value.value("damping").toDouble();
    if (value.contains("mass"))
        config.mass = value.value("mass").toDouble();
    return config;
}
}

AnimationDriver::AnimationDriver(QObject *parent) : QObject(parent) {
    const QSizeF size = primaryScreenPoints();
    contentWidth_ = size.width();
    contentHeight_ = size.height();
    cursor_.setSettings(cursorSettings_);
    camScaleSpring_.setConfig(screenSpring_);
    camXSpring_.setConfig(screenSpring_);
    camYSpring_.setConfig(screenSpring_);
    timer_ = new QTimer(this);
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, &AnimationDriver::tick);
}

void AnimationDriver::start() {
    if (active_)
        return;
    active_ = true;
    clock_.restart();
    nowMs_ = 0.0;
    lastTickMs_ = 0.0;
    lastX_ = -1.0;
    lastY_ = -1.0;
    pressed_ = false;
    zoomUntilMs_ = -1.0;
    track_ = Animation::EventTrack{};
    cursor_ = Animation::CursorEngine{};
    cursor_.setSettings(cursorSettings_);
    camScaleSpring_.reset(1.0);
    camXSpring_.reset(0.0);
    camYSpring_.reset(0.0);
    pressSpring_.reset(1.0);
    timer_->start();
    emit activeChanged();
}

void AnimationDriver::stop() {
    if (!active_)
        return;
    active_ = false;
    timer_->stop();
    emit activeChanged();
}

void AnimationDriver::setSettings(const QVariantMap &settings) {
    if (settings.contains("disableMouseMovementSpring"))
        smoothingEnabled_ = !settings.value("disableMouseMovementSpring").toBool();
    if (settings.contains("mouseMovementSpring"))
        cursorSettings_.movement = springFrom(settings, "mouseMovementSpring", cursorSettings_.movement);
    cursorSettings_.smoothingEnabled = smoothingEnabled_;
    if (settings.contains("cursorRotateOnXMovementRatio"))
        cursorSettings_.rotationRatio = settings.value("cursorRotateOnXMovementRatio").toDouble();
    if (settings.contains("hideNotMovingCursorAfterMs"))
        cursorSettings_.hideAfterMs = settings.value("hideNotMovingCursorAfterMs").toDouble();
    if (settings.contains("cursorBaseRotation"))
        cursorSettings_.baseRotationDeg = settings.value("cursorBaseRotation").toDouble();
    if (settings.contains("defaultZoomLevel"))
        zoomLevel_ = settings.value("defaultZoomLevel").toDouble();
    if (settings.contains("autoZoom"))
        autoZoomEnabled_ = settings.value("autoZoom").toBool();
    if (settings.contains("snapToEdgesRatio"))
        snapToEdgesRatio_ = settings.value("snapToEdgesRatio").toDouble();
    if (settings.contains("screenMovementSpring"))
        screenSpring_ = springFrom(settings, "screenMovementSpring", screenSpring_);
    camScaleSpring_.setConfig(screenSpring_);
    camXSpring_.setConfig(screenSpring_);
    camYSpring_.setConfig(screenSpring_);
    cursor_.setSettings(cursorSettings_);
}

void AnimationDriver::setManualZoom(bool zoomed) {
    if (zoomed) {
        zoomUntilMs_ = 1.0e12;
        zoomFocus_ = {contentWidth_ > 0.0 ? cursorX_ / contentWidth_ : 0.5,
            contentHeight_ > 0.0 ? cursorY_ / contentHeight_ : 0.5};
    } else {
        zoomUntilMs_ = -1.0;
    }
}

void AnimationDriver::tick() {
    if (!active_)
        return;

    nowMs_ = clock_.elapsed();
    const double dt = std::max(0.0, nowMs_ - lastTickMs_);
    lastTickMs_ = nowMs_;
    const MouseSample sample = pollMouse();
    if (sample.cursorIdentity != 0 && sample.cursorIdentity != cursorIdentity_) {
        cursorIdentity_ = sample.cursorIdentity;
        const CursorSample cursorSample = currentCursor();
        if (cursorSample.valid) {
            cursorImage_ = cursorSample.image;
            cursorAvailable_ = true;
            cursorPointWidth_ = cursorSample.pointWidth;
            cursorPointHeight_ = cursorSample.pointHeight;
            cursorHotspotX_ = cursorSample.hotspotX;
            cursorHotspotY_ = cursorSample.hotspotY;
            ++cursorImageRevision_;
            emit cursorImageChanged();
        }
    }
    const double localX = sample.x;
    const double localY = contentHeight_ - sample.y; // AppKit bottom-left -> top-left
    const double x = std::clamp(localX, 0.0, contentWidth_);
    const double y = std::clamp(localY, 0.0, contentHeight_);

    const bool first = lastX_ < 0.0;
    const bool moved = first || std::abs(x - lastX_) > 0.01 || std::abs(y - lastY_) > 0.01;
    if (moved) {
        track_.append({nowMs_, x, y, Animation::InputKind::Move});
        lastX_ = x;
        lastY_ = y;
    }
    if (sample.pressed != pressed_) {
        track_.append({nowMs_, x, y, sample.pressed ? Animation::InputKind::Down : Animation::InputKind::Up});
        if (sample.pressed) {
            zoomUntilMs_ = nowMs_ + zoomHoldMs_;
            zoomFocus_ = {contentWidth_ > 0.0 ? x / contentWidth_ : 0.5,
                contentHeight_ > 0.0 ? y / contentHeight_ : 0.5};
        }
        pressed_ = sample.pressed;
    }
    track_.pruneBefore(nowMs_ - 60000.0);

    cursor_.setTrack(track_);
    const Animation::CursorPose pose = cursor_.advanceTo(nowMs_);
    cursorX_ = pose.x;
    cursorY_ = pose.y;
    cursorRotation_ = pose.rotationDeg;
    cursorAlpha_ = pose.alpha;
    // Press shrink: hold at clickScale while any button is down, spring back on release.
    pressSpring_.setTargetValue(pressed_ ? cursorSettings_.clickScale : 1.0);
    if (dt > 0.0)
        pressSpring_.advanceBy(dt);
    cursorScale_ = pressSpring_.value();

    // While zoomed, the view follows the live cursor; otherwise recenter so the
    // camera returns to identity smoothly as the zoom ends.
    const bool zoomed = autoZoomEnabled_ && nowMs_ <= zoomUntilMs_;
    if (zoomed) {
        zoomFocus_ = {contentWidth_ > 0.0 ? x / contentWidth_ : 0.5,
            contentHeight_ > 0.0 ? y / contentHeight_ : 0.5};
    } else {
        zoomFocus_ = {0.5, 0.5};
    }
    zoomTarget_ = zoomed ? zoomLevel_ : 1.0;
    const Animation::ScreenTransform transform = Animation::AutoFocusEngine::transformFor(zoomFocus_,
        zoomTarget_, contentWidth_, contentHeight_, contentWidth_, contentHeight_, snapToEdgesRatio_);
    camScaleSpring_.setTargetValue(transform.scale);
    camXSpring_.setTargetValue(transform.offsetX);
    camYSpring_.setTargetValue(transform.offsetY);

    if (dt > 0.0) {
        camScaleSpring_.advanceBy(dt);
        camXSpring_.advanceBy(dt);
        camYSpring_.advanceBy(dt);
    }
    camScale_ = camScaleSpring_.value();
    camOffsetX_ = camXSpring_.value();
    camOffsetY_ = camYSpring_.value();

    emit poseChanged();
}

CursorImageProvider::CursorImageProvider(AnimationDriver *driver)
    : QQuickImageProvider(QQuickImageProvider::Image), driver_(driver) {}

QImage CursorImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(id);
    Q_UNUSED(requestedSize);
    const QImage image = driver_->cursorImage();
    if (size)
        *size = image.size();
    return image;
}
