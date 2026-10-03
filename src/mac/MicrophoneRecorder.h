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
- (NSDictionary *)metadata;

@end
