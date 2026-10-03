#import "PointerEventRecorder.h"
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <atomic>
#include <cmath>

namespace {
qint64 hostTimeNs() {
    return CMTimeConvertScale(CMClockGetTime(CMClockGetHostTimeClock()),
        1000000000, kCMTimeRoundingMethod_RoundHalfAwayFromZero).value;
}
QString typeName(CGEventType type) {
    switch (type) {
    case kCGEventMouseMoved: return QStringLiteral("mouseMoved");
    case kCGEventLeftMouseDragged:
    case kCGEventRightMouseDragged:
    case kCGEventOtherMouseDragged: return QStringLiteral("mouseDragged");
    case kCGEventLeftMouseDown:
    case kCGEventRightMouseDown:
    case kCGEventOtherMouseDown: return QStringLiteral("mouseDown");
    case kCGEventLeftMouseUp:
    case kCGEventRightMouseUp:
    case kCGEventOtherMouseUp: return QStringLiteral("mouseUp");
    case kCGEventScrollWheel: return QStringLiteral("scroll");
    default: return QStringLiteral("other");
    }
}
bool saveJson(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}

struct PointerEventRecorder::Impl : std::enable_shared_from_this<Impl> {
    CFMachPortRef tap = nullptr;
    CFRunLoopSourceRef source = nullptr;
    CVDisplayLinkRef displayLink = nullptr;
    std::atomic<std::uint64_t> generation{0};
    std::atomic<bool> cursorSamplePending{false};
    CGRect displayBounds = CGRectZero;
    QSize pixelSize;
    std::uint32_t displayId = 0;
    QString directory, failure;
    QFile eventsFile, cursorFile;
    QJsonObject cursorDefinitions;
    qint64 startHostNs = 0, eventCount = 0, cursorCount = 0;
    int tapInterruptions = 0;
    bool recording = false;
    // While paused the tap and cursor clock stop writing; a boundary event marks
    // the resume so the timeline rebuilder can fold the gap out.
    qint64 pausedAccumNs = 0, pauseStartHostNs = 0;
    bool paused = false;

    QJsonObject position(CGPoint location) const {
        return {{"globalX", location.x}, {"globalY", location.y},
            {"xPx", (location.x - displayBounds.origin.x) * pixelSize.width() / displayBounds.size.width},
            {"yPx", (location.y - displayBounds.origin.y) * pixelSize.height() / displayBounds.size.height},
            {"insideDisplay", CGRectContainsPoint(displayBounds, location)}};
    }
    bool append(QFile &file, const QJsonObject &object) {
        if (!failure.isEmpty()) return false;
        const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        if (file.write(line) != line.size()) {
            failure = QStringLiteral("事件记录写入失败：") + file.errorString();
            return false;
        }
        return true;
    }
    void snapshotPosition(const QString &reason) {
        if (paused) return;
        CGEventRef event = CGEventCreate(nullptr);
        if (!event) return;
        QJsonObject sample = position(CGEventGetLocation(event));
        sample.insert("hostTimeNs", QString::number(hostTimeNs()));
        sample.insert("type", "mouseMoved");
        sample.insert("synthetic", reason);
        if (append(eventsFile, sample)) ++eventCount;
        CFRelease(event);
    }
    void sampleCursor(qint64 scheduledDisplayHostNs = 0) {
        if (!recording || paused || !failure.isEmpty()) return;
        @autoreleasepool {
            const qint64 sampledHostNs = hostTimeNs();
            NSCursor *cursor = NSCursor.currentSystemCursor;
            CGEventRef event = CGEventCreate(nullptr);
            QJsonObject sample = position(event ? CGEventGetLocation(event) : CGPointZero);
            if (event) CFRelease(event);
            sample.insert("hostTimeNs", QString::number(sampledHostNs));
            sample.insert("scheduledDisplayHostTimeNs", QString::number(scheduledDisplayHostNs));
            sample.insert("available", cursor != nil);
            if (cursor) {
                NSImage *image = cursor.image;
                NSData *tiff = image.TIFFRepresentation;
                CGImageRef cgImage = [image CGImageForProposedRect:nullptr context:nil hints:nil];
                if (tiff && cgImage && image.size.width > 0 && image.size.height > 0) {
                    QByteArray identity(static_cast<const char *>(tiff.bytes), tiff.length);
                    identity += QByteArray::number(cursor.hotSpot.x, 'g', 17) + ','
                        + QByteArray::number(cursor.hotSpot.y, 'g', 17) + ','
                        + QByteArray::number(image.size.width, 'g', 17) + ','
                        + QByteArray::number(image.size.height, 'g', 17);
                    const QString id = QString::fromLatin1(QCryptographicHash::hash(identity,
                        QCryptographicHash::Sha256).toHex());
                    if (!cursorDefinitions.contains(id)) {
                        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
                        NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                        const QString relative = QStringLiteral("cursors/") + id + QStringLiteral(".png");
                        QSaveFile file(directory + '/' + relative);
                        if (!png || !file.open(QIODevice::WriteOnly)
                            || file.write(static_cast<const char *>(png.bytes), png.length)
                                != static_cast<qint64>(png.length) || !file.commit()) {
                            failure = QStringLiteral("无法保存光标图片"); return;
                        }
                        const double width = CGImageGetWidth(cgImage), height = CGImageGetHeight(cgImage);
                        cursorDefinitions.insert(id, QJsonObject{{"image", relative},
                            {"widthPx", width}, {"heightPx", height},
                            {"widthPoints", image.size.width}, {"heightPoints", image.size.height},
                            {"hotSpotXPoints", cursor.hotSpot.x}, {"hotSpotYPoints", cursor.hotSpot.y},
                            {"hotSpotXPx", cursor.hotSpot.x * width / image.size.width},
                            {"hotSpotYPx", cursor.hotSpot.y * height / image.size.height}});
                        if (!saveJson(directory + QStringLiteral("/cursors.json"), cursorDefinitions)) {
                            failure = QStringLiteral("无法保存光标定义"); return;
                        }
                    }
                    sample.insert("cursorId", id);
                } else sample.insert("available", false);
            }
            if (append(cursorFile, sample)) ++cursorCount;
        }
    }
    static CGEventRef callback(CGEventTapProxy, CGEventType type, CGEventRef event, void *context) {
        auto *self = static_cast<Impl *>(context);
        if (!self->recording || self->paused) return event;
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            ++self->tapInterruptions;
            self->append(self->eventsFile, {{"type", "eventTapInterrupted"},
                {"hostTimeNs", QString::number(hostTimeNs())},
                {"reason", type == kCGEventTapDisabledByTimeout ? "timeout" : "userInput"}});
            if (self->tap) CGEventTapEnable(self->tap, true);
            return event;
        }
        @autoreleasepool {
            NSEvent *native = [NSEvent eventWithCGEvent:event];
            QJsonObject sample = self->position(CGEventGetLocation(event));
            sample.insert("type", typeName(type));
            sample.insert("nativeType", static_cast<int>(type));
            sample.insert("hostTimeNs", QString::number(CGEventGetTimestamp(event)));
            sample.insert("receivedHostTimeNs", QString::number(hostTimeNs()));
            sample.insert("nseventTimestampNs", QString::number(static_cast<qint64>(std::llround(native.timestamp * 1e9))));
            sample.insert("modifierFlags", QString::number(CGEventGetFlags(event)));
            if (type != kCGEventMouseMoved && type != kCGEventScrollWheel) {
                sample.insert("button", static_cast<int>(CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber)));
                sample.insert("clickCount", static_cast<int>(CGEventGetIntegerValueField(event, kCGMouseEventClickState)));
            }
            if (type == kCGEventScrollWheel) {
                sample.insert("scrollX", CGEventGetDoubleValueField(event, kCGScrollWheelEventPointDeltaAxis2));
                sample.insert("scrollY", CGEventGetDoubleValueField(event, kCGScrollWheelEventPointDeltaAxis1));
            }
            if (self->append(self->eventsFile, sample)) ++self->eventCount;
        }
        return event;
    }
    static CVReturn displayCallback(CVDisplayLinkRef, const CVTimeStamp *, const CVTimeStamp *output,
        CVOptionFlags, CVOptionFlags *, void *context) {
        auto *self = static_cast<Impl *>(context);
        if (self->cursorSamplePending.exchange(true)) return kCVReturnSuccess;
        const auto weak = self->weak_from_this();
        const auto epoch = self->generation.load();
        const qint64 displayNs = CMTimeConvertScale(CMClockMakeHostTimeFromSystemUnits(output->hostTime),
            1000000000, kCMTimeRoundingMethod_RoundHalfAwayFromZero).value;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (auto state = weak.lock()) {
                if (state->generation.load() == epoch) {
                    state->cursorSamplePending = false;
                    state->sampleCursor(displayNs);
                }
            }
        });
        return kCVReturnSuccess;
    }
    void close() {
        recording = false;
        paused = false;
        ++generation;
        if (displayLink) {
            CVDisplayLinkStop(displayLink); CVDisplayLinkRelease(displayLink); displayLink = nullptr;
        }
        cursorSamplePending = false;
        if (tap) CGEventTapEnable(tap, false);
        if (source) {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
            CFRelease(source); source = nullptr;
        }
        if (tap) { CFRelease(tap); tap = nullptr; }
        eventsFile.close(); cursorFile.close();
    }
};

PointerEventRecorder::PointerEventRecorder() : impl_(std::make_shared<Impl>()) {}
PointerEventRecorder::~PointerEventRecorder() { impl_->close(); }

bool PointerEventRecorder::start(std::uint32_t displayId, const QSize &pixelSize,
    const QRectF &boundsPoints, const QString &directory) {
    impl_->close(); impl_->failure.clear();
    impl_->eventCount = impl_->cursorCount = 0; impl_->tapInterruptions = 0;
    impl_->cursorDefinitions = {}; impl_->directory = directory;
    impl_->displayId = displayId; impl_->pixelSize = pixelSize;
    // The capture rect comes from the caller: for a display source it is the
    // display frame, for a window or region it is the clipped capture rectangle.
    impl_->displayBounds = CGRectMake(boundsPoints.x(), boundsPoints.y(),
        boundsPoints.width(), boundsPoints.height());
    if (!pixelSize.isValid() || impl_->displayBounds.size.width <= 0 || impl_->displayBounds.size.height <= 0) {
        impl_->failure = QStringLiteral("录制区域坐标无效"); return false;
    }
    if (!CGPreflightListenEventAccess() && !CGRequestListenEventAccess()) {
        impl_->failure = QStringLiteral("请在系统设置的“隐私与安全性 → 输入监控”中允许简库镜传，然后重新打开应用。");
        return false;
    }
    QDir().mkpath(directory + QStringLiteral("/cursors"));
    impl_->eventsFile.setFileName(directory + QStringLiteral("/pointer-events.jsonl"));
    impl_->cursorFile.setFileName(directory + QStringLiteral("/cursor-observations.jsonl"));
    if (!impl_->eventsFile.open(QIODevice::WriteOnly) || !impl_->cursorFile.open(QIODevice::WriteOnly)) {
        impl_->failure = QStringLiteral("无法创建事件记录文件"); impl_->close(); return false;
    }
    const CGEventMask mask = CGEventMaskBit(kCGEventMouseMoved)
        | CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventRightMouseDragged)
        | CGEventMaskBit(kCGEventOtherMouseDragged) | CGEventMaskBit(kCGEventLeftMouseDown)
        | CGEventMaskBit(kCGEventLeftMouseUp) | CGEventMaskBit(kCGEventRightMouseDown)
        | CGEventMaskBit(kCGEventRightMouseUp) | CGEventMaskBit(kCGEventOtherMouseDown)
        | CGEventMaskBit(kCGEventOtherMouseUp) | CGEventMaskBit(kCGEventScrollWheel);
    impl_->tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly, mask, &Impl::callback, impl_.get());
    if (!impl_->tap) {
        impl_->failure = QStringLiteral("无法监听鼠标事件，请检查输入监控权限"); impl_->close(); return false;
    }
    impl_->source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, impl_->tap, 0);
    if (!impl_->source || CVDisplayLinkCreateWithCGDisplay(displayId, &impl_->displayLink) != kCVReturnSuccess) {
        impl_->failure = QStringLiteral("无法创建光标采样时钟"); impl_->close(); return false;
    }
    CFRunLoopAddSource(CFRunLoopGetMain(), impl_->source, kCFRunLoopCommonModes);
    impl_->startHostNs = hostTimeNs(); impl_->recording = true;
    impl_->snapshotPosition(QStringLiteral("recordingStart")); impl_->sampleCursor();
    if (impl_->cursorDefinitions.isEmpty() || !impl_->failure.isEmpty()) {
        if (impl_->failure.isEmpty()) impl_->failure = QStringLiteral("无法读取系统光标图片");
        impl_->close(); return false;
    }
    CVDisplayLinkSetOutputCallback(impl_->displayLink, &Impl::displayCallback, impl_.get());
    if (CVDisplayLinkStart(impl_->displayLink) != kCVReturnSuccess) {
        impl_->failure = QStringLiteral("无法启动光标采样时钟"); impl_->close(); return false;
    }
    CGEventTapEnable(impl_->tap, true);
    return true;
}

void PointerEventRecorder::pause() {
    if (!impl_->recording || impl_->paused) return;
    impl_->paused = true;
    impl_->pauseStartHostNs = hostTimeNs();
    impl_->append(impl_->eventsFile, {{"type", "recordingPaused"},
        {"hostTimeNs", QString::number(impl_->pauseStartHostNs)}});
}

void PointerEventRecorder::resume() {
    if (!impl_->recording || !impl_->paused) return;
    const qint64 now = hostTimeNs();
    impl_->pausedAccumNs += std::max<qint64>(0, now - impl_->pauseStartHostNs);
    impl_->paused = false;
    impl_->append(impl_->eventsFile, {{"type", "recordingResumed"},
        {"hostTimeNs", QString::number(now)},
        {"pausedDurationNs", QString::number(now - impl_->pauseStartHostNs)}});
    impl_->snapshotPosition(QStringLiteral("recordingResume"));
    impl_->sampleCursor();
}

QJsonObject PointerEventRecorder::stop() {
    if (impl_->paused) resume();
    const bool wasActive = active();
    if (wasActive) {
        impl_->snapshotPosition(QStringLiteral("recordingEnd")); impl_->sampleCursor();
        if (!impl_->eventsFile.flush() || !impl_->cursorFile.flush())
            impl_->failure = QStringLiteral("事件记录刷新到磁盘失败");
    }
    const qint64 endHostNs = hostTimeNs(); impl_->close();
    return {{"schemaVersion", 1}, {"eventTapActive", wasActive},
        {"events", "pointer-events.jsonl"}, {"cursorObservations", "cursor-observations.jsonl"},
        {"cursorDefinitions", "cursors.json"}, {"eventCount", impl_->eventCount},
        {"cursorObservationCount", impl_->cursorCount}, {"cursorImageCount", impl_->cursorDefinitions.size()},
        {"startHostTimeNs", QString::number(impl_->startHostNs)}, {"endHostTimeNs", QString::number(endHostNs)},
        {"tapInterruptions", impl_->tapInterruptions}, {"writeError", impl_->failure}};
}
bool PointerEventRecorder::active() const { return impl_->recording && impl_->tap; }
QString PointerEventRecorder::error() const { return impl_->failure; }
