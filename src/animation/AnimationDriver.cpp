#include "AnimationDriver.h"
#include "AnimationSettings.h"

#include "../capture/CaptureSource.h"
#include "../mac/MousePoll.h"

#include <QTimer>
#include <algorithm>
#include <cmath>

AnimationDriver::AnimationDriver(QObject *parent) : QObject(parent) {
    const QSizeF size = primaryScreenPoints();
    contentWidth_ = size.width();
    contentHeight_ = size.height();
    primaryHeightPoints_ = size.height();
    // Until a source is chosen, the content box is the whole primary display, which
    // is what the old code always assumed.
    sourceBoundsX_ = 0.0;
    sourceBoundsY_ = 0.0;
    sourceBoundsWidth_ = size.width();
    sourceBoundsHeight_ = size.height();
    sourcePixelWidth_ = size.width();
    sourcePixelHeight_ = size.height();
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
    // Shared with the offline compositor so both read a setting the same way.
    Animation::DriverSettings base;
    base.cursor = cursorSettings_;
    base.screenSpring = screenSpring_;
    base.autoZoom = autoZoomEnabled_;
    base.zoomLevel = zoomLevel_;
    base.snapToEdgesRatio = snapToEdgesRatio_;
    const Animation::DriverSettings resolved = Animation::driverSettingsFromMap(settings, base);
    cursorSettings_ = resolved.cursor;
    screenSpring_ = resolved.screenSpring;
    autoZoomEnabled_ = resolved.autoZoom;
    zoomLevel_ = resolved.zoomLevel;
    snapToEdgesRatio_ = resolved.snapToEdgesRatio;
    smoothingEnabled_ = cursorSettings_.smoothingEnabled;
    camScaleSpring_.setConfig(screenSpring_);
    camXSpring_.setConfig(screenSpring_);
    camYSpring_.setConfig(screenSpring_);
    cursor_.setSettings(cursorSettings_);
}

void AnimationDriver::setSourceGeometry(double x, double y, double widthPoints,
    double heightPoints, double widthPixels, double heightPixels) {
    if (!(widthPoints > 0.0) || !(heightPoints > 0.0)
        || !(widthPixels > 0.0) || !(heightPixels > 0.0))
        return;
    sourceBoundsX_ = x;
    sourceBoundsY_ = y;
    sourceBoundsWidth_ = widthPoints;
    sourceBoundsHeight_ = heightPoints;
    sourcePixelWidth_ = widthPixels;
    sourcePixelHeight_ = heightPixels;
    // The preview draws in source *points* (the frame is scaled to fit anyway), so
    // the content box is the recorded rect's size. The pixel size is kept because a
    // region capture can have a different points-to-pixels scale than its display.
    if (std::abs(contentWidth_ - widthPoints) > 0.5
        || std::abs(contentHeight_ - heightPoints) > 0.5) {
        contentWidth_ = widthPoints;
        contentHeight_ = heightPoints;
        emit contentSizeChanged();
    }
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
    // Pointer position inside the captured rect, in the same pixel space the
    // compositor draws the pointer in. Before this the driver always mapped through
    // the primary display's size, so a window or region capture — or anything on a
    // second display — drew the smooth pointer in the wrong place; only a
    // full-primary-display recording happened to come out right.
    double x = 0.0;
    double y = 0.0;
    if (sourceBoundsWidth_ > 0.0 && sourceBoundsHeight_ > 0.0
        && sourcePixelWidth_ > 0.0 && sourcePixelHeight_ > 0.0) {
        // One shared implementation, because the pointer recorder does the same
        // conversion: if the two ever disagree the recorded events and the live
        // preview would put the pointer in different places.
        const QPointF mapped = Capture::pointerPixelPosition(
            QPointF(sample.x, sample.y),
            QRectF(sourceBoundsX_, sourceBoundsY_, sourceBoundsWidth_, sourceBoundsHeight_),
            QSize(int(sourcePixelWidth_), int(sourcePixelHeight_)),
            primaryHeightPoints_ > 0.0 ? primaryHeightPoints_ : contentHeight_);
        x = mapped.x();
        y = mapped.y();
    }
    x = std::clamp(x, 0.0, contentWidth_);
    y = std::clamp(y, 0.0, contentHeight_);

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
