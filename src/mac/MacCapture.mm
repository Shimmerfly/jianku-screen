#import "MacCapture.h"
#import "MicrophoneRecorder.h"
#import "PointerEventRecorder.h"
#import "../capture/MediaClock.h"
#import "../capture/VideoFrameStore.h"
#include "../project/ProjectTimeline.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <AVFoundation/AVFoundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <AppKit/AppKit.h>

#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QDrag>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QWindow>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QMetaObject>
#include <QSize>
#include <QStandardPaths>
#include <QThreadPool>
#include <algorithm>
#include <functional>
#include <mutex>
#include <utility>

namespace {
struct CallbackGate {
    std::mutex mutex;
    MacCapture *owner = nullptr;
};

void postToOwner(const std::shared_ptr<CallbackGate> &gate,
                 std::function<void(MacCapture *)> action) {
    std::lock_guard lock(gate->mutex);
    if (MacCapture *owner = gate->owner) {
        QMetaObject::invokeMethod(owner,
            [owner, action = std::move(action)] { action(owner); },
            Qt::QueuedConnection);
    }
}

QString errorText(NSError *error) {
    if (!error)
        return QStringLiteral("未知屏幕采集错误");
    return QString::fromNSString(error.localizedDescription);
}

QSize displayPixelSize(SCDisplay *display) {
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display.displayID);
    if (!mode)
        return QSize(static_cast<int>(display.width), static_cast<int>(display.height));
    const QSize result(static_cast<int>(CGDisplayModeGetPixelWidth(mode)),
                       static_cast<int>(CGDisplayModeGetPixelHeight(mode)));
    CGDisplayModeRelease(mode);
    return result;
}

qint64 timeNs(CMTime time) {
    return CMTimeConvertScale(time, 1000000000, kCMTimeRoundingMethod_RoundHalfAwayFromZero).value;
}

QRectF QRectFFromCGRect(CGRect rect) {
    return QRectF(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
}

// Used for diagnostics only; the manifest records the source kind and rect, not
// this string, so a renamed window never invalidates an existing recording.
QString sourceLabel(SCDisplay *display, SCWindow *window) {
    if (window) {
        const QString title = window.title ? QString::fromNSString(window.title) : QString();
        const QString app = window.owningApplication && window.owningApplication.applicationName
            ? QString::fromNSString(window.owningApplication.applicationName) : QString();
        if (!title.isEmpty() && !app.isEmpty())
            return app + QStringLiteral(" · ") + title;
        return title.isEmpty() ? app : title;
    }
    return display
        ? QStringLiteral("显示器 %1").arg(static_cast<int>(display.displayID))
        : QStringLiteral("画面来源");
}

// Windows worth offering as a source: real windows with an owning app and a
// usable frame. System chrome (menus, the dock, wallpaper) are windows too, but
// recording them is never what the user meant.
QVariantList windowSourceList(NSArray<SCWindow *> *windows) {
    QVariantList list;
    for (SCWindow *window in windows) {
        if (!window.owningApplication || !window.onScreen)
            continue;
        const CGRect frame = window.frame;
        if (frame.size.width < 80.0 || frame.size.height < 60.0)
            continue;
        list.append(QVariantMap{
            {QStringLiteral("windowId"), static_cast<double>(window.windowID)},
            {QStringLiteral("title"), window.title ? QString::fromNSString(window.title) : QString()},
            {QStringLiteral("application"),
                window.owningApplication.applicationName
                    ? QString::fromNSString(window.owningApplication.applicationName) : QString()},
            {QStringLiteral("label"), sourceLabel(nil, window)},
            {QStringLiteral("width"), frame.size.width},
            {QStringLiteral("height"), frame.size.height}});
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("label")).toString()
            < b.toMap().value(QStringLiteral("label")).toString();
    });
    return list;
}

bool writeProject(const QString &directory, const QJsonObject &manifest) {
    QSaveFile file(directory + QStringLiteral("/project.json"));
    const auto bytes = QJsonDocument(manifest).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

// Objective-C containers are not QJsonValue-convertible. Handing an NSDictionary
// straight to QJsonObject::insert() silently stores a bool: every microphone
// manifest written so far says `"microphone": true` instead of the track info,
// because the implicit conversion picks a pointer-to-bool overload. Round-trip
// through NSJSONSerialization so the structure actually survives.
QJsonObject jsonFromDictionary(NSDictionary *dictionary) {
    if (![dictionary isKindOfClass:[NSDictionary class]] || dictionary.count == 0)
        return {};
    if (![NSJSONSerialization isValidJSONObject:dictionary])
        return {};
    NSData *data = [NSJSONSerialization dataWithJSONObject:dictionary options:0 error:nil];
    if (!data)
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray(reinterpret_cast<const char *>(data.bytes), static_cast<int>(data.length)));
    return document.isObject() ? document.object() : QJsonObject{};
}
} // namespace

@interface JiankuStreamReceiver : NSObject <SCStreamOutput, SCStreamDelegate> {
@public
    std::shared_ptr<VideoFrameStore> frameStore;
    std::shared_ptr<CallbackGate> callbackGate;
    dispatch_queue_t sampleQueue;
    AVAssetWriter *writer;
    AVAssetWriterInput *videoInput;
    AVAssetWriterInputPixelBufferAdaptor *videoAdaptor;
    AVAssetWriterInput *audioInput;
    BOOL audioMuted;
    NSURL *recordingURL;
    CMTime firstFrameTime;
    BOOL writerStarted;
    BOOL recordingActive;
    BOOL paused;
    // Total paused duration folded out of the media timeline. Every frame and
    // audio buffer is shifted left by this so a pause leaves no gap in the file.
    qint64 pausedAccumNs;
    qint64 pauseStartHostNs;
    CMTime lastFrameTime;
    // Media time actually handed to the writer for the newest frame. Recomputing
    // it later via mediaTimeForSourceTime would subtract pauses accumulated
    // after that frame and could move it backwards past already-written frames.
    CMTime lastFrameMediaTime;
    // Host time of the newest frame already folded past every completed pause.
    // Subtracting it from the folded stop time yields the true tail length even
    // when the stop happens while the recording is paused.
    qint64 lastFrameFoldedHostNs;
    // Extra tail media time appended at stop so a static source still plays up
    // to the moment the user pressed stop.
    qint64 mediaTailNs;
    qint64 firstDisplayHostNs;
    qint64 lastDisplayHostNs;
    qint64 frameCount;
    qint64 droppedFrameCount;
    CVPixelBufferRef lastRecordedFrame;
    std::unique_ptr<QFile> frameTimeline;
    bool timelineHealthy;
    NSMutableArray<NSDictionary *> *pauseRanges;
    std::function<void(QString, QString, QJsonObject)> recordingFinished;
}
- (BOOL)beginRecordingAtURL:(NSURL *)url size:(QSize)size error:(NSError **)error;
- (void)endRecording;
- (void)pauseRecording;
- (void)resumeRecording;
- (CMTime)mediaTimeForSourceTime:(CMTime)sourceTime;
- (QJsonObject)recordingMetadata;
@end

@implementation JiankuStreamReceiver
- (BOOL)beginRecordingAtURL:(NSURL *)url size:(QSize)size error:(NSError **)error {
    AVAssetWriter *newWriter = [[AVAssetWriter alloc] initWithURL:url
        fileType:AVFileTypeMPEG4 error:error];
    if (!newWriter)
        return NO;
    const int pixels = size.width() * size.height();
    NSDictionary *compression = @{
        AVVideoAverageBitRateKey: @(std::clamp(pixels * 6, 12000000, 36000000)),
        AVVideoExpectedSourceFrameRateKey: @60,
        AVVideoMaxKeyFrameIntervalKey: @120
    };
    NSDictionary *settings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(size.width()),
        AVVideoHeightKey: @(size.height()),
        AVVideoCompressionPropertiesKey: compression
    };
    AVAssetWriterInput *input = [[AVAssetWriterInput alloc]
        initWithMediaType:AVMediaTypeVideo outputSettings:settings];
    input.expectsMediaDataInRealTime = YES;
    if (![newWriter canAddInput:input]) {
        if (error)
            *error = [NSError errorWithDomain:@"JiankuScreen" code:1
                userInfo:@{NSLocalizedDescriptionKey: @"当前尺寸无法创建视频轨道"}];
        return NO;
    }
    [newWriter addInput:input];
    if (!audioMuted) {
        // System audio is muxed into the recording as a second (AAC) track.
        NSDictionary *audioSettings = @{
            AVFormatIDKey: @(kAudioFormatMPEG4AAC),
            AVSampleRateKey: @48000.0,
            AVNumberOfChannelsKey: @2,
            AVEncoderBitRateKey: @(128000)
        };
        AVAssetWriterInput *newAudioInput = [[AVAssetWriterInput alloc]
            initWithMediaType:AVMediaTypeAudio outputSettings:audioSettings];
        newAudioInput.expectsMediaDataInRealTime = YES;
        if ([newWriter canAddInput:newAudioInput]) {
            [newWriter addInput:newAudioInput];
            audioInput = newAudioInput;
        } else {
            audioInput = nil;
        }
    } else {
        audioInput = nil;
    }
    NSDictionary *attributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey: @(size.width()),
        (id)kCVPixelBufferHeightKey: @(size.height())
    };
    AVAssetWriterInputPixelBufferAdaptor *adaptor =
        [[AVAssetWriterInputPixelBufferAdaptor alloc]
            initWithAssetWriterInput:input sourcePixelBufferAttributes:attributes];
    dispatch_sync(sampleQueue, ^{
        frameTimeline = std::make_unique<QFile>(QFileInfo(QString::fromNSString(url.path)).path()
            + QStringLiteral("/video-frames.jsonl"));
        if (!frameTimeline->open(QIODevice::WriteOnly)) return;
        writer = newWriter;
        videoInput = input;
        videoAdaptor = adaptor;
        recordingURL = url;
        writerStarted = NO;
        recordingActive = YES;
        paused = NO;
        pausedAccumNs = 0;
        pauseStartHostNs = 0;
        firstFrameTime = kCMTimeInvalid;
        lastFrameTime = kCMTimeInvalid;
        lastFrameMediaTime = kCMTimeInvalid;
        lastFrameFoldedHostNs = 0;
        mediaTailNs = 0;
        firstDisplayHostNs = lastDisplayHostNs = 0;
        frameCount = droppedFrameCount = 0;
        timelineHealthy = true;
        lastRecordedFrame = nullptr;
        pauseRanges = [NSMutableArray array];
    });
    if (!recordingActive && error)
        *error = [NSError errorWithDomain:@"JiankuScreen" code:2
            userInfo:@{NSLocalizedDescriptionKey: @"无法创建视频帧时间记录"}];
    return recordingActive;
}

- (CMTime)mediaTimeForSourceTime:(CMTime)sourceTime {
    // Fold every completed pause out of the media timeline so the exported file
    // has no gap where the recording was suspended.
    return CMTimeSubtract(CMTimeSubtract(sourceTime, firstFrameTime),
        CMTimeMake(pausedAccumNs, 1000000000));
}

- (void)pauseRecording {
    dispatch_async(sampleQueue, ^{
        if (!recordingActive || paused || !writerStarted)
            return;
        paused = YES;
        pauseStartHostNs = timeNs(CMClockGetTime(CMClockGetHostTimeClock()));
        [pauseRanges addObject:[NSMutableDictionary dictionaryWithObjectsAndKeys:
            QString::number(pauseStartHostNs).toNSString(), @"startHostTimeNs", nil]];
    });
}

- (void)resumeRecording {
    dispatch_async(sampleQueue, ^{
        if (!recordingActive || !paused)
            return;
        const qint64 now = timeNs(CMClockGetTime(CMClockGetHostTimeClock()));
        const qint64 duration = std::max<qint64>(0, now - pauseStartHostNs);
        pausedAccumNs += duration;
        NSDictionary *last = [pauseRanges lastObject];
        NSMutableDictionary *updated = [last mutableCopy];
        updated[@"endHostTimeNs"] = QString::number(now).toNSString();
        updated[@"durationNs"] = QString::number(duration).toNSString();
        [pauseRanges removeLastObject];
        [pauseRanges addObject:updated];
        paused = NO;
    });
}

- (QJsonObject)recordingMetadata {
    QJsonArray pauses;
    for (NSDictionary *range in pauseRanges) {
        NSString *start = range[@"startHostTimeNs"];
        NSString *end = range[@"endHostTimeNs"];
        NSString *duration = range[@"durationNs"];
        pauses.append(QJsonObject{
            {"startHostTimeNs", start ? QString::fromNSString(start) : QString()},
            {"endHostTimeNs", end ? QString::fromNSString(end) : QString()},
            {"durationNs", duration ? QString::fromNSString(duration) : QString()}});
    }
    // The newest frame's media time was captured when it was written; do not
    // recompute it here, because every pause completed since then would be
    // subtracted a second time. lastFrameMediaTime is already relative to
    // firstFrameTime, so it is the duration by itself.
    const CMTime mediaEnd = CMTIME_IS_VALID(lastFrameMediaTime)
        ? CMTimeAdd(lastFrameMediaTime, CMTimeMake(mediaTailNs, 1000000000)) : kCMTimeInvalid;
    return {{"file", "raw.mp4"}, {"frames", "video-frames.jsonl"}, {"cursorBakedIn", false},
        {"firstSourcePtsNs", CMTIME_IS_VALID(firstFrameTime) ? QString::number(timeNs(firstFrameTime)) : QString()},
        {"mediaZeroHostTimeNs", QString::number(firstDisplayHostNs)},
        {"lastDisplayHostTimeNs", QString::number(lastDisplayHostNs)},
        {"durationNs", CMTIME_IS_VALID(mediaEnd) ? QString::number(timeNs(mediaEnd)) : QString()},
        {"frameCount", frameCount}, {"droppedFrameCount", droppedFrameCount},
        {"pauseRanges", pauses}, {"pausedTotalNs", QString::number(pausedAccumNs)},
        {"timelineWriteError", !timelineHealthy ? QStringLiteral("视频帧时间记录写入失败") : QString()}};
}

- (void)endRecording {
    dispatch_async(sampleQueue, ^{
        if (!recordingActive)
            return;
        recordingActive = NO;
        if (!writerStarted) {
            [writer cancelWriting];
            [[NSFileManager defaultManager] removeItemAtURL:recordingURL error:nil];
            if (frameTimeline) frameTimeline->close();
            recordingFinished({}, QStringLiteral("未收到可写入的视频帧"), [self recordingMetadata]);
            writer = nil;
            videoInput = nil;
            videoAdaptor = nil;
            audioInput = nil;
            recordingURL = nil;
            return;
        }
        // Extend a static source to the actual stop time without inventing motion.
        // lastFrameMediaTime is the last PTS already written; taking the tail
        // from it (instead of recomputing through the source clock after a pause
        // was folded in) keeps every appended PTS strictly increasing.
        const qint64 stopHostNs = timeNs(CMClockGetTime(CMClockGetHostTimeClock()));
        const bool stoppedWhilePaused = paused;
        if (stoppedWhilePaused) {
            // Stopping while paused: the frozen interval is not part of the video,
            // and the frame that was on screen is the last thing the source showed.
            const qint64 duration = std::max<qint64>(0, stopHostNs - pauseStartHostNs);
            pausedAccumNs += duration;
            NSDictionary *last = [pauseRanges lastObject];
            NSMutableDictionary *updated = [last mutableCopy];
            updated[@"endHostTimeNs"] = QString::number(stopHostNs).toNSString();
            updated[@"durationNs"] = QString::number(duration).toNSString();
            [pauseRanges removeLastObject];
            [pauseRanges addObject:updated];
            paused = NO;
        }
        const CMTime lastMediaTime = CMTIME_IS_VALID(lastFrameMediaTime)
            ? lastFrameMediaTime : kCMTimeZero;
        // While paused nothing new was shown, so there is no tail to extend.
        const qint64 tailNs = Capture::tailNsAtStop(stopHostNs, pausedAccumNs,
            lastFrameFoldedHostNs, stoppedWhilePaused);
        mediaTailNs = tailNs;
        const CMTime endTime = CMTimeAdd(lastMediaTime, CMTimeMake(tailNs, 1000000000));
        const CMTime finalFrameTime = CMTimeMake(
            Capture::finalFrameMediaNs(timeNs(lastMediaTime), tailNs, 16666667), 1000000000);
        if (lastRecordedFrame && videoInput.readyForMoreMediaData
            && CMTimeCompare(finalFrameTime, lastMediaTime) > 0
            && [videoAdaptor appendPixelBuffer:lastRecordedFrame
                withPresentationTime:finalFrameTime]) {
            ++frameCount;
            const auto line = QJsonDocument(QJsonObject{{"mediaTimeNs", QString::number(timeNs(finalFrameTime))},
                {"displayHostTimeNs", QString::number(stopHostNs - 16666667)}, {"repeatedAtStop", true}}).toJson(QJsonDocument::Compact) + '\n';
            timelineHealthy &= frameTimeline->write(line) == line.size();
        }
        if (lastRecordedFrame) { CVPixelBufferRelease(lastRecordedFrame); lastRecordedFrame = nullptr; }
        const bool flushed = frameTimeline->flush() && timelineHealthy;
        const QJsonObject metadata = [self recordingMetadata];
        frameTimeline->close();
        [videoInput markAsFinished];
        if (audioInput) [audioInput markAsFinished];
        AVAssetWriter *finishingWriter = writer;
        NSURL *finishedURL = recordingURL;
        auto completion = recordingFinished;
        writer = nil;
        videoInput = nil;
        videoAdaptor = nil;
        audioInput = nil;
        recordingURL = nil;
        [finishingWriter finishWritingWithCompletionHandler:^{
            if (finishingWriter.status == AVAssetWriterStatusCompleted && flushed) {
                completion(QString::fromNSString(finishedURL.path), {}, metadata);
            } else {
                const QString failure = flushed ? errorText(finishingWriter.error)
                    : QStringLiteral("视频帧时间记录保存失败");
                completion({}, failure, metadata);
            }
        }];
    });
}

- (void)handleAudioSampleBuffer:(CMSampleBufferRef)sampleBuffer {
    if (!recordingActive || !audioInput || !writerStarted || paused)
        return;
    if (!CMSampleBufferIsValid(sampleBuffer))
        return;
    if (!CMTIME_IS_VALID(firstFrameTime))
        return;
    // Align audio with the video timeline (both use the SCK host clock) and fold
    // any completed pause out so audio and video stay in sync afterwards.
    const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    const CMTime relative = [self mediaTimeForSourceTime:pts];
    if (CMTimeCompare(relative, kCMTimeZero) < 0)
        return;
    if (![audioInput isReadyForMoreMediaData])
        return;
    CMSampleBufferRef adjusted = nullptr;
    CMSampleTimingInfo timing;
    timing.duration = CMSampleBufferGetDuration(sampleBuffer);
    if (!CMTIME_IS_VALID(timing.duration))
        timing.duration = CMTimeMake(1, 48000);
    timing.presentationTimeStamp = relative;
    timing.decodeTimeStamp = kCMTimeInvalid;
    // Exactly one timing entry, applied to every sample in the buffer. Passing
    // the sample count here would read past the single-element struct.
    if (CMSampleBufferCreateCopyWithNewTiming(kCFAllocatorDefault, sampleBuffer,
            1, &timing, &adjusted) != noErr)
        return;
    [audioInput appendSampleBuffer:adjusted];
    CFRelease(adjusted);
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type == SCStreamOutputTypeAudio) {
        // Audio shares the serial writer queue, keeping writer/input state and
        // the first-frame anchor single-threaded.
        return [self handleAudioSampleBuffer:sampleBuffer];
    }
    if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sampleBuffer))
        return;

    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (!attachments || CFArrayGetCount(attachments) == 0)
        return;
    NSDictionary *frameInfo = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
    if ([frameInfo[SCStreamFrameInfoStatus] integerValue] != SCFrameStatusComplete)
        return;

    CVPixelBufferRef frame = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (frame)
        frameStore->publish(frame);
    if (frame && recordingActive) {
        const CMTime timestamp = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        if (!CMTIME_IS_VALID(timestamp))
            return;
        if (!writerStarted) {
            if (![writer startWriting]) {
                recordingActive = NO;
                const QString failure = errorText(writer.error);
                [writer cancelWriting];
                [[NSFileManager defaultManager] removeItemAtURL:recordingURL error:nil];
                if (frameTimeline) frameTimeline->close();
                recordingFinished({}, failure, [self recordingMetadata]);
                return;
            }
            [writer startSessionAtSourceTime:kCMTimeZero];
            firstFrameTime = timestamp;
            writerStarted = YES;
        }
        const NSNumber *displayTime = frameInfo[SCStreamFrameInfoDisplayTime];
        const qint64 receiveNs = timeNs(CMClockGetTime(CMClockGetHostTimeClock()));
        const qint64 displayNs = displayTime ? timeNs(CMClockMakeHostTimeFromSystemUnits(displayTime.unsignedLongLongValue)) : receiveNs;
        // While paused the source keeps delivering frames; they are dropped so
        // the pause leaves no silent interval in the file.
        if (paused) {
            ++droppedFrameCount;
            return;
        }
        if (videoInput.readyForMoreMediaData) {
            if (frameCount == 0) firstFrameTime = timestamp;
            const CMTime relativeTime = [self mediaTimeForSourceTime:timestamp];
            if (CMTIME_IS_VALID(lastFrameTime) && CMTimeCompare(timestamp, lastFrameTime) <= 0) {
                ++droppedFrameCount;
                return;
            }
            if (CMTimeCompare(relativeTime, kCMTimeZero) >= 0 &&
                ![videoAdaptor appendPixelBuffer:frame withPresentationTime:relativeTime]) {
                recordingActive = NO;
                const QString failure = errorText(writer.error);
                [videoInput markAsFinished];
                AVAssetWriter *failedWriter = writer;
                NSURL *failedURL = recordingURL;
                auto completion = recordingFinished;
                const QJsonObject metadata = [self recordingMetadata];
                if (frameTimeline) frameTimeline->close();
                if (lastRecordedFrame) { CVPixelBufferRelease(lastRecordedFrame); lastRecordedFrame = nullptr; }
                [failedWriter finishWritingWithCompletionHandler:^{
                    Q_UNUSED(failedURL);
                    completion({}, failure, metadata);
                }];
                return;
            }
            if (CMTimeCompare(relativeTime, kCMTimeZero) >= 0) {
                if (!firstDisplayHostNs) firstDisplayHostNs = displayNs;
                lastFrameTime = timestamp;
                lastFrameMediaTime = relativeTime;
                lastFrameFoldedHostNs = displayNs - pausedAccumNs;
                lastDisplayHostNs = displayNs;
                ++frameCount;
                if (lastRecordedFrame) CVPixelBufferRelease(lastRecordedFrame);
                lastRecordedFrame = CVPixelBufferRetain(frame);
                const auto line = QJsonDocument(QJsonObject{{"mediaTimeNs", QString::number(timeNs(relativeTime))},
                    {"sourcePtsNs", QString::number(timeNs(timestamp))}, {"displayHostTimeNs", QString::number(displayNs)},
                    {"receivedHostTimeNs", QString::number(receiveNs)}, {"displayTimeAvailable", displayTime != nil}}).toJson(QJsonDocument::Compact) + '\n';
                timelineHealthy &= frameTimeline->write(line) == line.size();
            }
        } else {
            ++droppedFrameCount;
        }
    }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    (void)stream;
    const QString message = errorText(error);
    postToOwner(callbackGate, [message](MacCapture *owner) {
        owner->stop();
        Q_UNUSED(message);
    });
}
@end

struct MacCapture::Impl {
    explicit Impl(MacCapture *owner) : gate(std::make_shared<CallbackGate>()) {
        gate->owner = owner;
    }

    std::shared_ptr<CallbackGate> gate;
    MicrophoneRecorder *micRecorder = nil;
    NSArray<SCDisplay *> *displays = nil;
    NSArray<SCRunningApplication *> *applications = nil;
    NSArray<SCWindow *> *windows = nil;
    SCStream *stream = nil;
    JiankuStreamReceiver *receiver = nil;
    SCStreamConfiguration *configuration = nil;
    PointerEventRecorder pointerRecorder;
    QString projectDirectory;
    QJsonObject projectManifest;
    // Size the stream was configured with. Every frame arrives at exactly this
    // size, so the writer and the manifest both take it from here instead of
    // re-deriving it from whichever display happens to be current.
    QSize sourcePixelSize;
    std::uint64_t generation = 0;
};

MacCapture::MacCapture(QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this)),
      frameStore_(std::make_shared<VideoFrameStore>()) {}

MacCapture::~MacCapture() {
    if (impl_->pointerRecorder.active()) {
        impl_->projectManifest.insert("pointer", impl_->pointerRecorder.stop());
        impl_->projectManifest.insert("state", "interrupted");
        writeProject(impl_->projectDirectory, impl_->projectManifest);
    }
    if (recording_ && impl_->receiver)
        [impl_->receiver endRecording];
    if (impl_->micRecorder) {
        [impl_->micRecorder stop];
        impl_->micRecorder = nil;
    }
    {
        std::lock_guard lock(impl_->gate->mutex);
        impl_->gate->owner = nullptr;
    }
    if (impl_->stream)
        [impl_->stream stopCaptureWithCompletionHandler:nil];
}

QObject *MacCapture::frameStore() const { return frameStore_.get(); }

bool MacCapture::screenAuthorized() const {
    // Preflight is unreliable for ad-hoc signed builds, so a successful capture
    // probe (SCShareableContent) also counts as authorised.
    return captureProbeOk_ || CGPreflightScreenCaptureAccess();
}

void MacCapture::refreshScreenAuthorization() {
    emit screenAuthorizedChanged();
}

bool MacCapture::requestScreenAuthorization() {
    const bool granted = CGRequestScreenCaptureAccess();
    emit screenAuthorizedChanged();
    return granted;
}

void MacCapture::openScreenRecordingSettings() {
    NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

void MacCapture::openInputMonitoringSettings() {
    NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent"];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

void MacCapture::openMicrophoneSettings() {
    NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

bool MacCapture::requestInputMonitoringAccess() {
    // Shows the system prompt on first call; afterwards it only reports state.
    return CGRequestListenEventAccess() || CGPreflightListenEventAccess();
}

void MacCapture::revealAppInFinder() {
    NSURL *url = [NSURL fileURLWithPath:NSBundle.mainBundle.bundlePath];
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ url ]];
}

QString MacCapture::appBundlePath() const {
    return QString::fromNSString(NSBundle.mainBundle.bundlePath);
}

QString MacCapture::appFileUrl() const {
    return QUrl::fromLocalFile(appBundlePath()).toString();
}

void MacCapture::beginAppDrag() {
    auto *mime = new QMimeData;
    mime->setUrls({ QUrl::fromLocalFile(appBundlePath()) });
    QWindow *window = QGuiApplication::focusWindow();
    auto *drag = new QDrag(window ? static_cast<QObject *>(window) : this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}

void MacCapture::resetScreenPermission() {
    const QString bundleId = NSBundle.mainBundle.bundleIdentifier
        ? QString::fromNSString(NSBundle.mainBundle.bundleIdentifier)
        : QStringLiteral("com.jianku.screen");
    QProcess::execute(QStringLiteral("/usr/bin/tccutil"),
        { QStringLiteral("reset"), QStringLiteral("ScreenCapture"), bundleId });
    requestScreenAuthorization();
    emit screenAuthorizedChanged();
}

void MacCapture::relaunch() {
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    QCoreApplication::quit();
}

void MacCapture::setStatus(const QString &value) {
    if (status_ == value)
        return;
    status_ = value;
    emit statusChanged();
}

void MacCapture::setPermissionIssue(const QString &kind, const QString &message) {
    if (permissionIssue_ == message && permissionIssueKind_ == kind)
        return;
    permissionIssueKind_ = kind;
    permissionIssue_ = message;
    emit permissionIssueChanged();
}

void MacCapture::setRunning(bool value) {
    if (running_ == value)
        return;
    running_ = value;
    emit runningChanged();
}

void MacCapture::setBusy(bool value) {
    if (busy_ == value)
        return;
    busy_ = value;
    emit busyChanged();
}

void MacCapture::setRecording(bool value) {
    if (recording_ == value)
        return;
    recording_ = value;
    emit recordingChanged();
}

void MacCapture::setRecordingPaused(bool value) {
    if (recordingPaused_ == value)
        return;
    recordingPaused_ = value;
    emit recordingPausedChanged();
}

void MacCapture::setRecordingStatus(const QString &value) {
    if (recordingStatus_ == value)
        return;
    recordingStatus_ = value;
    emit recordingStatusChanged();
}

void MacCapture::setLastRecordingPath(const QString &value) {
    if (lastRecordingPath_ == value)
        return;
    lastRecordingPath_ = value;
    emit lastRecordingPathChanged();
}

void MacCapture::reportError(const QString &value) {
    recordWhenReady_ = false;
    setBusy(false);
    setRunning(false);
    setStatus(QStringLiteral("屏幕采集失败：") + value);
}

void MacCapture::refreshDisplays() {
    if (busy_ || running_)
        return;
    setBusy(true);
    setStatus(QStringLiteral("正在读取可录制的显示器…"));
    auto gate = impl_->gate;
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
        onScreenWindowsOnly:NO
        completionHandler:^(SCShareableContent *content, NSError *error) {
            const QString failure = errorText(error);
            postToOwner(gate, [content, error, failure](MacCapture *owner) {
                if (error || !content) {
                    owner->captureProbeOk_ = false;
                    emit owner->screenAuthorizedChanged();
                    emit owner->captureAccessDenied();
                    owner->setPermissionIssue(QStringLiteral("screen"), failure);
                    owner->reportError(failure);
                    return;
                }
                owner->captureProbeOk_ = true;
                emit owner->screenAuthorizedChanged();
                owner->setPermissionIssue({}, {});
                owner->impl_->displays = content.displays;
                owner->impl_->applications = content.applications;
                owner->impl_->windows = content.windows;
                QStringList names;
                for (SCDisplay *display in content.displays) {
                    const QSize pixels = displayPixelSize(display);
                    names << QStringLiteral("显示器 %1 · %2 × %3")
                                 .arg(display.displayID).arg(pixels.width()).arg(pixels.height());
                }
                owner->displayNames_ = names;
                emit owner->displayNamesChanged();
                owner->windowSources_ = windowSourceList(content.windows);
                emit owner->windowSourcesChanged();
                owner->setBusy(false);
                owner->setStatus(names.isEmpty()
                    ? QStringLiteral("没有可用显示器。请检查屏幕录制权限。")
                    : QStringLiteral("选择显示器后开始预览。"));
            });
        }];
}

void MacCapture::startDisplay(int index) {
    Capture::CaptureSource source;
    source.kind = Capture::SourceKind::Display;
    if (index >= 0 && index < static_cast<int>(impl_->displays.count))
        source.displayId = impl_->displays[index].displayID;
    startSource(source);
}

void MacCapture::startSource(const Capture::CaptureSource &source) {
    if (busy_ || running_)
        return;
    // A window source is not tied to a display by the caller, and the user may
    // have dragged it since the picker was filled. Resolve the display from where
    // the window actually is now; for displays and regions the caller's choice
    // stands (startRegion already picked one from the region's centre).
    std::uint32_t wantedDisplay = source.displayId;
    if (source.kind == Capture::SourceKind::Window) {
        SCWindow *found = nil;
        for (SCWindow *candidate in impl_->windows) {
            if (candidate.windowID == source.windowId)
                found = candidate;
        }
        if (!found) {
            reportError(QStringLiteral("这个窗口已经关闭，请重新选择来源"));
            return;
        }
        QList<QRectF> frames;
        for (SCDisplay *candidate in impl_->displays)
            frames.append(QRectFFromCGRect(CGDisplayBounds(candidate.displayID)));
        const QRectF frame = QRectFFromCGRect(found.frame);
        const int index = Capture::displayIndexContaining(frame.center(), frames);
        if (index >= 0)
            wantedDisplay = impl_->displays[index].displayID;
    }

    SCDisplay *display = nil;
    for (SCDisplay *candidate in impl_->displays) {
        if (candidate.displayID == wantedDisplay)
            display = candidate;
    }
    if (!display)
        display = impl_->displays.firstObject;
    if (!display) {
        reportError(QStringLiteral("录制来源无效"));
        return;
    }
    SCWindow *window = nil;
    if (source.kind == Capture::SourceKind::Window) {
        for (SCWindow *candidate in impl_->windows) {
            if (candidate.windowID == source.windowId)
                window = candidate;
        }
        if (!window) {
            reportError(QStringLiteral("这个窗口已经关闭，请重新选择来源"));
            return;
        }
    }

    Capture::CaptureSource resolved = source;
    resolved.displayId = display.displayID;
    const QRectF displayBounds = QRectFFromCGRect(CGDisplayBounds(display.displayID));
    const QRectF windowFrame = window ? QRectFFromCGRect(window.frame) : QRectF();
    const Capture::CaptureGeometry geometry = Capture::resolveGeometry(resolved, displayBounds,
        displayPixelSize(display), windowFrame);
    if (!geometry.valid) {
        reportError(geometry.error);
        return;
    }
    // The manifest always records the rect that was really captured, so a window
    // recording still states its geometry correctly after the window has moved.
    resolved.regionPoints = geometry.boundsPoints;
    if (resolved.label.isEmpty())
        resolved.label = sourceLabel(display, window);

    setBusy(true);
    stopRequested_ = false;
    activeSource_ = resolved;
    for (NSUInteger i = 0; i < impl_->displays.count; ++i) {
        if (impl_->displays[i] == display)
            activeDisplayIndex_ = static_cast<int>(i);
    }
    setStatus(source.kind == Capture::SourceKind::Display
        ? QStringLiteral("正在启动屏幕采集…") : QStringLiteral("正在启动来源采集…"));

    // Our own windows must never appear in a recording: they would show the
    // preview of the recording inside the recording.
    NSString *bundleId = NSBundle.mainBundle.bundleIdentifier;
    NSMutableArray<SCRunningApplication *> *excluded = [NSMutableArray array];
    for (SCRunningApplication *app in impl_->applications) {
        if ([app.bundleIdentifier isEqualToString:bundleId])
            [excluded addObject:app];
    }

    SCContentFilter *filter = nil;
    if (window) {
        // A desktop-independent window keeps producing its own pixels even while
        // occluded, which is what a window recording is expected to show.
        filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
    } else {
        filter = [[SCContentFilter alloc] initWithDisplay:display
            excludingApplications:excluded exceptingWindows:@[]];
    }

    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
    const QSize pixels = geometry.pixelSize;
    config.width = pixels.width();
    config.height = pixels.height();
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.minimumFrameInterval = CMTimeMake(1, 60);
    config.queueDepth = 3;
    if (window) {
        // Measured on this machine: leaving shadows on insets the window content
        // by (64,49) px inside a frame that is still sized window.frame × scale,
        // so the content and the recorded rect would disagree — every pointer
        // event of a window recording would be off by that margin. With shadows
        // ignored the content starts at (0,0) and fills the frame.
        config.ignoreShadowsSingleWindow = YES;
    }
    if (source.kind == Capture::SourceKind::Region) {
        // sourceRect is in points relative to the display's top-left corner; the
        // stream scales it up to the requested pixel size.
        config.sourceRect = CGRectMake(geometry.boundsPoints.x() - displayBounds.x(),
            geometry.boundsPoints.y() - displayBounds.y(),
            geometry.boundsPoints.width(), geometry.boundsPoints.height());
        config.scalesToFit = YES;
        config.preservesAspectRatio = YES;
    }
    // Never capture the system cursor: the animation engine composites its own
    // smoothed pointer, otherwise two cursors would be visible.
    config.showsCursor = NO;
    // System audio is always captured; it only enters the file when recording.
    config.capturesAudio = YES;
    config.excludesCurrentProcessAudio = YES;

    JiankuStreamReceiver *receiver = [[JiankuStreamReceiver alloc] init];
    receiver->frameStore = frameStore_;
    receiver->callbackGate = impl_->gate;
    receiver->audioMuted = recordingSettings_.value("muteSystemAudio").toBool();
    SCStream *stream = [[SCStream alloc] initWithFilter:filter
        configuration:config delegate:receiver];
    NSError *error = nil;
    dispatch_queue_t sampleQueue = dispatch_queue_create(
        "com.jianku.screen.capture-frames", DISPATCH_QUEUE_SERIAL);
    receiver->sampleQueue = sampleQueue;
    if (![stream addStreamOutput:receiver type:SCStreamOutputTypeScreen
              sampleHandlerQueue:sampleQueue error:&error]) {
        reportError(errorText(error));
        return;
    }
    // System audio shares the receiver; handleAudioSampleBuffer drops samples
    // when the writer has no audio track (muted) or recording has not started.
    if (!recordingSettings_.value("muteSystemAudio").toBool()) {
        NSError *audioError = nil;
        if (![stream addStreamOutput:receiver type:SCStreamOutputTypeAudio
                  sampleHandlerQueue:sampleQueue error:&audioError]) {
            const QString failure = QStringLiteral("系统声音输出未接通，请检查屏幕录制权限：") + errorText(audioError);
            setPermissionIssue(QStringLiteral("screen"), failure);
            reportError(failure);
            return;
        }
    }
    impl_->receiver = receiver;
    impl_->stream = stream;
    impl_->configuration = config;
    impl_->sourcePixelSize = pixels;
    const std::uint64_t generation = ++impl_->generation;
    auto gate = impl_->gate;
    [stream startCaptureWithCompletionHandler:^(NSError *startError) {
        const QString failure = errorText(startError);
        postToOwner(gate, [generation, startError, failure](MacCapture *owner) {
            if (generation != owner->impl_->generation)
                return;
            if (startError) {
                owner->impl_->stream = nil;
                owner->impl_->receiver = nil;
                owner->reportError(failure);
                return;
            }
            owner->setBusy(false);
            owner->setRunning(true);
            owner->setStatus(QStringLiteral("屏幕画面正在输出。"));
            if (owner->recordWhenReady_) {
                owner->recordWhenReady_ = false;
                owner->startRecording();
            }
            if (owner->stopRequested_)
                owner->stop();
        });
    }];
}

void MacCapture::startRecordingWindow(double windowId, const QVariantMap &settings) {
    if (recording_ || recordingFinalizing_ || busy_)
        return;
    recordingSettings_ = settings;
    // A running stream is capturing whatever source it was started with; if that
    // is a different one, restart it rather than silently recording the wrong
    // thing under the new name.
    if (running_ && activeSource_.kind == Capture::SourceKind::Window
        && activeSource_.windowId == static_cast<std::uint32_t>(windowId)) {
        startRecording();
        return;
    }
    if (running_)
        stop();
    recordWhenReady_ = true;
    startWindow(windowId);
}

void MacCapture::startRecordingRegion(double x, double y, double width, double height,
    const QVariantMap &settings) {
    if (recording_ || recordingFinalizing_ || busy_)
        return;
    recordingSettings_ = settings;
    const QRectF region(x, y, width, height);
    if (running_ && activeSource_.kind == Capture::SourceKind::Region
        && activeSource_.regionPoints == region) {
        startRecording();
        return;
    }
    if (running_)
        stop();
    recordWhenReady_ = true;
    startRegion(x, y, width, height);
}

void MacCapture::startRecordingDisplay(int index, const QVariantMap &settings) {
    if (recording_ || recordingFinalizing_ || busy_)
        return;
    recordingSettings_ = settings;
    if (running_ && activeSource_.kind == Capture::SourceKind::Display) {
        startRecording();
        return;
    }
    if (running_)
        stop();
    recordWhenReady_ = true;
    startDisplay(index);
}

void MacCapture::startWindow(double windowId) {
    Capture::CaptureSource source;
    source.kind = Capture::SourceKind::Window;
    source.windowId = static_cast<std::uint32_t>(windowId);
    startSource(source);
}

void MacCapture::startRegion(double x, double y, double width, double height) {
    Capture::CaptureSource source;
    source.kind = Capture::SourceKind::Region;
    source.regionPoints = QRectF(x, y, width, height);
    // Pick the display under the region's centre. A region dragged across a
    // monitor boundary cannot be captured by either display in full, so the
    // centre decides which one it belongs to and resolveGeometry crops it.
    QList<QRectF> frames;
    for (SCDisplay *display in impl_->displays)
        frames.append(QRectFFromCGRect(CGDisplayBounds(display.displayID)));
    const int index = Capture::displayIndexContaining(source.regionPoints.center(), frames);
    if (index < 0) {
        reportError(QStringLiteral("录制区域不在任何显示器上"));
        return;
    }
    activeDisplayIndex_ = index;
    source.displayId = impl_->displays[index].displayID;
    startSource(source);
}

void MacCapture::startRecording() {
    if (!running_ || !impl_->receiver || recording_ || recordingFinalizing_)
        return;
    if (impl_->configuration.showsCursor) {
        setBusy(true);
        impl_->configuration.showsCursor = NO;
        const auto generation = impl_->generation;
        auto gate = impl_->gate;
        [impl_->stream updateConfiguration:impl_->configuration completionHandler:^(NSError *error) {
            const QString failure = errorText(error);
            postToOwner(gate, [generation, error, failure](MacCapture *owner) {
                if (generation != owner->impl_->generation) return;
                owner->setBusy(false);
                if (error) {
                    owner->setRecordingStatus(QStringLiteral("无法准备无光标录制：") + failure);
                    owner->stop();
                } else owner->startRecording();
            });
        }];
        return;
    }
    QString directory = recordingSettings_.value("recordingDirectory").toString();
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
            + QStringLiteral("/Jianku Screen");
    if (!QDir().mkpath(directory)) {
        setRecordingStatus(QStringLiteral("无法创建录像保存目录：") + directory);
        return;
    }
    const QString projectDirectory = directory + QStringLiteral("/Jianku Screen ")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss-zzz"))
        + QStringLiteral(".jianku");
    if (!QDir().mkpath(projectDirectory)) {
        setRecordingStatus(QStringLiteral("无法创建录制工程目录"));
        stop();
        return;
    }
    const QString path = projectDirectory + QStringLiteral("/raw.mp4");
    if (!activeSource_.displayId) {
        setRecordingStatus(QStringLiteral("没有可用的录制来源"));
        return;
    }
    impl_->projectDirectory = projectDirectory;
    // The source block is the contract between a recording and everything that
    // reads it later: the frame size, the global rect the pixels correspond to,
    // and the kind of thing that was captured. A region or window recording whose
    // rect were wrong here would put every pointer event in the wrong place.
    Capture::CaptureSource recordedSource = activeSource_;
    const QRectF bounds = recordedSource.regionPoints;
    const QSize pixels = impl_->sourcePixelSize;
    QJsonObject sourceJson = recordedSource.toJson();
    sourceJson.insert(QStringLiteral("widthPx"), pixels.width());
    sourceJson.insert(QStringLiteral("heightPx"), pixels.height());
    sourceJson.insert(QStringLiteral("globalBoundsPoints"),
        QJsonObject{{QStringLiteral("x"), bounds.x()}, {QStringLiteral("y"), bounds.y()},
            {QStringLiteral("width"), bounds.width()}, {QStringLiteral("height"), bounds.height()}});
    impl_->projectManifest = {{"schemaVersion", 1}, {"application", "Jianku Screen"},
        {"animationModelVersion", "desktop-3.7.5-research-v1"}, {"state", "preparing"},
        {"createdAt", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {"settings", QJsonObject::fromVariantMap(recordingSettings_)},
        {"source", sourceJson}};
    lastProjectPath_ = projectDirectory;
    emit lastRecordingPathChanged();
    if (!impl_->pointerRecorder.start(recordedSource.displayId, pixels, bounds, projectDirectory)) {
        const QString failure = impl_->pointerRecorder.error();
        impl_->projectManifest.insert("state", "failed");
        impl_->projectManifest.insert("error", failure);
        writeProject(projectDirectory, impl_->projectManifest);
        setPermissionIssue(QStringLiteral("input"), failure);
        setRecordingStatus(failure);
        stop();
        return;
    }
    impl_->projectManifest.insert("state", "recording");
    if (!writeProject(projectDirectory, impl_->projectManifest)) {
        impl_->pointerRecorder.stop();
        setRecordingStatus(QStringLiteral("无法保存录制工程清单"));
        stop();
        return;
    }
    const bool micMuted = recordingSettings_.value("muteMicrophone").toBool();
    if (!micMuted) {
        // Microphone is kept as its own track so it can be re-edited later.
        MicrophoneRecorder *recorder = [[MicrophoneRecorder alloc] init];
        NSString *micPath = (projectDirectory + QStringLiteral("/microphone.m4a")).toNSString();
        if (![recorder startAtURL:[NSURL fileURLWithPath:micPath]]) {
            NSString *reason = [recorder lastError];
            const QString failure = reason ? QString::fromNSString(reason)
                : QStringLiteral("麦克风录音启动失败，请检查麦克风权限与输入设备。");
            impl_->pointerRecorder.stop();
            impl_->projectManifest.insert("state", "failed");
            impl_->projectManifest.insert("error", failure);
            writeProject(projectDirectory, impl_->projectManifest);
            setPermissionIssue(QStringLiteral("microphone"), failure);
            setRecordingStatus(failure);
            stop();
            return;
        }
        impl_->micRecorder = recorder;
    }
    setPermissionIssue({}, {});
    NSError *error = nil;
    auto gate = impl_->gate;
    impl_->receiver->recordingFinished = [gate](QString output, QString failure, QJsonObject metadata) {
        postToOwner(gate, [output, failure, metadata](MacCapture *owner) {
            if (owner->impl_->pointerRecorder.active())
                owner->impl_->projectManifest.insert("pointer", owner->impl_->pointerRecorder.stop());
            QString problem = failure;
            if (problem.isEmpty()) problem = owner->impl_->projectManifest.value("pointer").toObject().value("writeError").toString();
            owner->impl_->projectManifest.insert("video", metadata);
            owner->impl_->projectManifest.insert("state", problem.isEmpty() ? "processing" : "failed");
            // Keep `error` meaning "why this project is not ready". A successful
            // stop writes an empty string rather than leaving a stale message
            // from an earlier attempt in the same project directory.
            owner->impl_->projectManifest.insert("error", problem);
            if (!writeProject(owner->impl_->projectDirectory, owner->impl_->projectManifest))
                problem = QStringLiteral("录制工程清单保存失败，原始素材仍在工程目录");
            owner->setRecording(false);
            if (problem.isEmpty()) {
                owner->setLastRecordingPath(output);
                owner->setRecordingStatus(QStringLiteral("正在生成鼠标时间轴与自动缩放区间…"));
                const QString directory = owner->impl_->projectDirectory;
                auto processingGate = owner->impl_->gate;
                QThreadPool::globalInstance()->start([directory, metadata, processingGate] {
                    const auto processing = ProjectTimeline::build(directory, metadata);
                    postToOwner(processingGate, [directory, processing](MacCapture *current) {
                        current->impl_->projectManifest.insert("processing", processing);
                        const bool ok = processing.value("state") == "generated";
                        current->impl_->projectManifest.insert("state", ok ? "readyForProcessing" : "recordedUnprocessed");
                        // A failed timeline build is the reason the project is not
                        // ready, so it belongs in `error`; a successful one clears
                        // whatever was there before.
                        current->impl_->projectManifest.insert("error", ok ? QString()
                            : processing.value("error").toString());
                        const bool saved = writeProject(directory, current->impl_->projectManifest);
                        current->recordingFinalizing_ = false;
                        current->setRecordingStatus(!saved ? QStringLiteral("工程清单保存失败，原始素材保留：") + directory
                            : ok ? QStringLiteral("录制工程已保存：") + directory
                            : QStringLiteral("原始录像已保存，时间轴处理失败：") + processing.value("error").toString());
                    });
                });
            } else {
                owner->recordingFinalizing_ = false;
                owner->setRecordingStatus(QStringLiteral("录制工程保存失败：") + problem);
                if (owner->running_) owner->stop();
            }
        });
    };
    if (![impl_->receiver beginRecordingAtURL:[NSURL fileURLWithPath:path.toNSString()]
        size:pixels error:&error]) {
        if (impl_->micRecorder) {
            [impl_->micRecorder stop];
            impl_->micRecorder = nil;
        }
        impl_->projectManifest.insert("pointer", impl_->pointerRecorder.stop());
        impl_->projectManifest.insert("state", "failed");
        impl_->projectManifest.insert("error", errorText(error));
        writeProject(projectDirectory, impl_->projectManifest);
        setRecordingStatus(QStringLiteral("无法开始录像：") + errorText(error));
        stop();
        return;
    }
    setLastRecordingPath({});
    setRecording(true);
    setRecordingStatus(QStringLiteral("正在录制屏幕…"));
}

void MacCapture::stopRecording() {
    if (!recording_ || !impl_->receiver)
        return;
    recordingFinalizing_ = true;
    setRecordingPaused(false);
    impl_->projectManifest.insert("pointer", impl_->pointerRecorder.stop());
    if (impl_->micRecorder) {
        impl_->projectManifest.insert("microphone", jsonFromDictionary([impl_->micRecorder metadata]));
        [impl_->micRecorder stop];
        impl_->micRecorder = nil;
    }
    impl_->projectManifest.insert("state", "finalizing");
    writeProject(impl_->projectDirectory, impl_->projectManifest);
    setRecording(false);
    setRecordingStatus(QStringLiteral("正在保存录像…"));
    [impl_->receiver endRecording];
}

void MacCapture::pauseRecording() {
    if (!recording_ || recordingFinalizing_ || recordingPaused_ || !impl_->receiver)
        return;
    impl_->pointerRecorder.pause();
    [impl_->micRecorder pause];
    [impl_->receiver pauseRecording];
    setRecordingPaused(true);
    setRecordingStatus(QStringLiteral("已暂停录制"));
}

void MacCapture::resumeRecording() {
    if (!recording_ || !recordingPaused_ || !impl_->receiver)
        return;
    [impl_->receiver resumeRecording];
    impl_->pointerRecorder.resume();
    [impl_->micRecorder resume];
    setRecordingPaused(false);
    setRecordingStatus(QStringLiteral("正在录制屏幕…"));
}

void MacCapture::openLastProject() {
    if (!lastProjectPath_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(lastProjectPath_));
}

void MacCapture::openRecordingDirectory() {
    QString directory = recordingSettings_.value("recordingDirectory").toString();
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
            + QStringLiteral("/Jianku Screen");
    QDir().mkpath(directory);
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

void MacCapture::stop() {
    recordWhenReady_ = false;
    if (recording_)
        stopRecording();
    if (!impl_->stream)
        return;
    if (busy_ && !running_) {
        stopRequested_ = true;
        return;
    }
    stopRequested_ = false;
    SCStream *stream = impl_->stream;
    impl_->stream = nil;
    impl_->receiver = nil;
    ++impl_->generation;
    setRunning(false);
    setBusy(true);
    setStatus(QStringLiteral("正在停止采集…"));
    auto gate = impl_->gate;
    [stream stopCaptureWithCompletionHandler:^(NSError *error) {
        const QString failure = errorText(error);
        postToOwner(gate, [error, failure](MacCapture *owner) {
            owner->frameStore_->clear();
            owner->setBusy(false);
            owner->setStatus(error
                ? QStringLiteral("停止采集时出错：") + failure
                : QStringLiteral("采集已停止。"));
        });
    }];
}
