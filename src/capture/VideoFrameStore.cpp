#include "VideoFrameStore.h"

#include <QMetaObject>

VideoFrameStore::~VideoFrameStore() {
    std::lock_guard lock(mutex_);
    if (latest_)
        CVPixelBufferRelease(latest_);
}

void VideoFrameStore::publish(CVPixelBufferRef pixelBuffer) {
    if (!pixelBuffer)
        return;
    CVPixelBufferRetain(pixelBuffer);
    {
        std::lock_guard lock(mutex_);
        if (latest_)
            CVPixelBufferRelease(latest_);
        latest_ = pixelBuffer;
        ++sequence_;
    }
    scheduleNotification();
}

void VideoFrameStore::clear() {
    {
        std::lock_guard lock(mutex_);
        if (latest_)
            CVPixelBufferRelease(latest_);
        latest_ = nullptr;
        ++sequence_;
    }
    scheduleNotification();
}

VideoFrameStore::Snapshot VideoFrameStore::snapshot() const {
    std::lock_guard lock(mutex_);
    if (latest_)
        CVPixelBufferRetain(latest_);
    return {latest_, sequence_};
}

void VideoFrameStore::scheduleNotification() {
    if (notificationQueued_.exchange(true))
        return;
    QMetaObject::invokeMethod(this, [this] {
        notificationQueued_.store(false);
        emit frameChanged();
    }, Qt::QueuedConnection);
}
