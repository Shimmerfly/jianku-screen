// 复现 -11800：起一个 SCStream 往 AVAssetWriter 写，跑 90 秒，看 append 什么时候失败、
// 失败时 writer.error 的 underlying error 是什么。
// 关键：errorText() 现在只取 localizedDescription + domain + code，
// 而 AVFoundation 的真正原因在 userInfo[NSUnderlyingErrorKey] 里。
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>

static NSString *describe(NSError *e, int depth) {
    if (!e) return @"(nil)";
    NSMutableString *s = [NSMutableString string];
    NSString *pad = [@"" stringByPaddingToLength:depth*2 withString:@" " startingAtIndex:0];
    [s appendFormat:@"%@domain=%@ code=%ld desc=%@\n", pad, e.domain, (long)e.code, e.localizedDescription];
    NSError *under = e.userInfo[NSUnderlyingErrorKey];
    if (under) [s appendString:describe(under, depth+1)];
    if (e.userInfo[NSLocalizedFailureReasonErrorKey])
        [s appendFormat:@"%@reason=%@\n", pad, e.userInfo[NSLocalizedFailureReasonErrorKey]];
    if (e.userInfo[NSLocalizedRecoverySuggestionErrorKey])
        [s appendFormat:@"%@suggestion=%@\n", pad, e.userInfo[NSLocalizedRecoverySuggestionErrorKey]];
    return s;
}

@interface Rec : NSObject <SCStreamOutput>
@property (nonatomic, strong) SCStream *stream;
@property (nonatomic, strong) AVAssetWriter *writer;
@property (nonatomic, strong) AVAssetWriterInput *videoInput;
@property (nonatomic, strong) AVAssetWriterInputPixelBufferAdaptor *adaptor;
@property (nonatomic, assign) BOOL started;
@property (nonatomic, assign) int frames;
@property (nonatomic, assign) double seconds;
@end

@implementation Rec
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sb ofType:(SCStreamOutputType)type {
    if (type != SCStreamOutputTypeScreen) return;
    CVPixelBufferRef px = CMSampleBufferGetImageBuffer(sb);
    if (!px) return;
    if (!self.started) {
        if (![self.writer startWriting]) { printf("startWriting 失败: %s\n", describe(self.writer.error,0).UTF8String); exit(1); }
        [self.writer startSessionAtSourceTime:kCMTimeZero];
        self.started = YES;
    }
    if (!self.videoInput.isReadyForMoreMediaData) return;
    CMTime pts = CMSampleBufferGetPresentationTimeStamp(sb);
    self.seconds = CMTimeGetSeconds(pts);
    if (![self.adaptor appendPixelBuffer:px withPresentationTime:pts]) {
        printf("\n!!! append 在第 %d 帧 / %.1f 秒失败\n", self.frames, self.seconds);
        printf("writer.status = %ld\n", (long)self.writer.status);
        printf("%s\n", describe(self.writer.error, 0).UTF8String);
        printf("input 状态: ready=%d\n", self.videoInput.isReadyForMoreMediaData);
        exit(2);
    }
    self.frames++;
    if (self.frames % 600 == 0) printf("  %d 帧 / %.1f 秒 …\n", self.frames, self.seconds);
}
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        double limit = argc > 1 ? atof(argv[1]) : 90.0;
        int wantW = argc > 2 ? atoi(argv[2]) : 0;
        int wantH = argc > 3 ? atoi(argv[3]) : 0;
        int wantFps = argc > 4 ? atoi(argv[4]) : 60;
        NSString *codec = argc > 5 ? [NSString stringWithUTF8String:argv[5]] : AVVideoCodecTypeH264;
        __block SCDisplay *display = nil;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *c, NSError *e) {
            if (c.displays.count) display = c.displays.firstObject;
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10*NSEC_PER_SEC));
        if (!display) { printf("拿不到显示器（多半是权限）\n"); return 1; }

        NSString *path = [NSString stringWithFormat:@"/tmp/scmatrix/o-%d-%d.mp4", getpid(), argc > 8 ? atoi(argv[8]) : 1];
        [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
        NSError *e = nil;
        Rec *rec = [Rec new];
        rec.writer = [AVAssetWriter assetWriterWithURL:[NSURL fileURLWithPath:path] fileType:AVFileTypeMPEG4 error:&e];
        if (!rec.writer) { printf("writer 创建失败: %s\n", describe(e,0).UTF8String); return 1; }
        if (argc <= 8 || atoi(argv[8]) != 0) rec.writer.movieFragmentInterval = CMTimeMake(1,1);
        int w = wantW > 0 ? wantW : (int)display.width;
        int h = wantH > 0 ? wantH : (int)display.height;
        NSDictionary *vs = @{AVVideoCodecKey: codec,
            AVVideoWidthKey: @(w), AVVideoHeightKey: @(h),
            AVVideoCompressionPropertiesKey: @{
                AVVideoAverageBitRateKey: @(argc > 6 ? atoi(argv[6]) : 30000000),
                AVVideoMaxKeyFrameIntervalKey: @(argc > 7 ? atoi(argv[7]) : 120),
                AVVideoExpectedSourceFrameRateKey: @(wantFps)
            }};
        rec.videoInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:vs];
        rec.videoInput.expectsMediaDataInRealTime = YES;
        [rec.writer addInput:rec.videoInput];
        rec.adaptor = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:rec.videoInput sourcePixelBufferAttributes:nil];

        SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
        SCStreamConfiguration *cfg = [SCStreamConfiguration new];
        cfg.width = w; cfg.height = h;
        cfg.minimumFrameInterval = CMTimeMake(1, wantFps);
        cfg.pixelFormat = kCVPixelFormatType_32BGRA;
        SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:cfg delegate:nil];
        rec.stream = stream;
        dispatch_queue_t q = dispatch_queue_create("sctest", DISPATCH_QUEUE_SERIAL);
        [stream addStreamOutput:rec type:SCStreamOutputTypeScreen sampleHandlerQueue:q error:&e];
        printf("开始录制 %dx%d @%d %s，上限 %.0f 秒\n", w, h, wantFps, codec.UTF8String, limit);
        [stream startCaptureWithCompletionHandler:^(NSError *err) {
            if (err) { printf("startCapture 失败: %s\n", describe(err,0).UTF8String); exit(1); }
        }];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(limit*NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            printf("\n到达时限，正常结束：%d 帧 / %.1f 秒，没有失败\n", rec.frames, rec.seconds);
            [rec.videoInput markAsFinished];
            [rec.writer finishWritingWithCompletionHandler:^{ exit(0); }];
        });
        dispatch_main();
    }
    return 0;
}
