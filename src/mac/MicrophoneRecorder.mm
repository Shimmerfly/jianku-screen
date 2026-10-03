#import "MicrophoneRecorder.h"

#import <CoreMedia/CoreMedia.h>

@implementation MicrophoneRecorder {
    AVAudioRecorder *recorder;
    NSURL *fileURL;
    NSString *lastError;
}

- (BOOL)startAtURL:(NSURL *)url {
    [self stop];
    lastError = nil;
    fileURL = url;

    NSDictionary *settings = @{
        AVFormatIDKey: @(kAudioFormatMPEG4AAC),
        AVSampleRateKey: @48000.0,
        AVNumberOfChannelsKey: @1,
        AVEncoderBitRateKey: @(96000)
    };
    NSError *error = nil;
    recorder = [[AVAudioRecorder alloc] initWithURL:url settings:settings error:&error];
    if (!recorder) {
        lastError = error.localizedDescription ?: @"无法创建麦克风录音器";
        return NO;
    }
    if (![recorder record]) {
        lastError = @"麦克风录音启动失败（请检查麦克风权限）";
        recorder = nil;
        return NO;
    }
    return YES;
}

- (void)pause {
    // AVAudioRecorder drops the paused interval from the file on resume, so the
    // microphone track stays aligned with the video's pause-folded timeline.
    if (recorder && recorder.recording)
        [recorder pause];
}

- (void)resume {
    if (recorder && !recorder.recording)
        [recorder record];
}

- (void)stop {
    if (recorder) {
        [recorder stop];
        recorder = nil;
    }
}

- (NSString *)lastError {
    return lastError;
}

- (NSDictionary *)metadata {
    if (!fileURL)
        return @{};
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:fileURL options:nil];
    const double durationMs = CMTimeGetSeconds(asset.duration) * 1000.0;
    return @{
        @"file": fileURL.lastPathComponent ?: @"",
        @"durationMs": @(durationMs)
    };
}

@end
