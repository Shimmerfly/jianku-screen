#pragma once

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

// Records the microphone into a standalone AAC file (a separate channel, like
// the reference's per-device recordings).
@interface MicrophoneRecorder : NSObject

- (BOOL)startAtURL:(NSURL *)url;
- (void)pause;
- (void)resume;
- (void)stop;
- (NSString *)lastError;
// Track info for project.json: file name, duration and the host-clock start.
// The start time is what lets the exporter align the microphone with the video
// timeline instead of guessing from durations alone.
- (NSDictionary *)metadata;

@end
