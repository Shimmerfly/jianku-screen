#pragma once

#include <CoreVideo/CoreVideo.h>
#include <QObject>
#include <atomic>
#include <cstdint>
#include <mutex>

// Holds one IOSurface-backed frame. The capture callback never waits for rendering.
class VideoFrameStore final : public QObject {
    Q_OBJECT

public:
    struct Snapshot {
        CVPixelBufferRef pixelBuffer = nullptr; // Caller owns one retain.
        std::uint64_t sequence = 0;
    };

    VideoFrameStore() = default;
    ~VideoFrameStore() override;

    void publish(CVPixelBufferRef pixelBuffer);
    void clear();
    Snapshot snapshot() const;

signals:
    void frameChanged();

private:
    void scheduleNotification();

    mutable std::mutex mutex_;
    CVPixelBufferRef latest_ = nullptr;
    std::uint64_t sequence_ = 0;
    std::atomic_bool notificationQueued_ = false;
};
