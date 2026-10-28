// What macOS offers the player through its own frameworks: spelling, reading text aloud and videos
// other than MPEG-1 (System.hpp says what each does; other systems have stand-ins in System.cpp).
#include "System.hpp"

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <cstring>

namespace vn {

namespace {

NSString* text(std::string_view s) {
    NSString* made = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
    return made ? made : @""; // (bytes that aren't UTF-8: none)
}

NSString* languageOf(std::string_view code) { // (one the spell checker has, else its own choice)
    if (code.empty()) return nil;
    NSString* wanted = text(code);
    for (NSString* have in [[NSSpellChecker sharedSpellChecker] availableLanguages])
        if ([have isEqualToString:wanted] || [have hasPrefix:[wanted stringByAppendingString:@"_"]]) return have;
    return nil;
}

} // namespace

bool spelledRight(std::string_view word, std::string_view language) {
    @autoreleasepool {
        NSSpellChecker* checker = [NSSpellChecker sharedSpellChecker];
        const NSRange wrong = [checker checkSpellingOfString:text(word) startingAt:0 language:languageOf(language) wrap:NO
                                     inSpellDocumentWithTag:0 wordCount:nullptr];
        return wrong.length == 0;
    }
}

std::vector<std::string> spellings(std::string_view word, std::string_view language) {
    std::vector<std::string> out;
    @autoreleasepool {
        NSString* w = text(word);
        NSArray<NSString*>* guesses = [[NSSpellChecker sharedSpellChecker] guessesForWordRange:NSMakeRange(0, w.length) inString:w
                                                                                     language:languageOf(language) inSpellDocumentWithTag:0];
        for (NSString* g in guesses) out.emplace_back(g.UTF8String);
    }
    return out;
}

void learnSpelling(std::string_view word) {
    @autoreleasepool {
        [[NSSpellChecker sharedSpellChecker] learnWord:text(word)];
    }
}

void speak(std::string_view words) {
    static AVSpeechSynthesizer* synthesizer = [[AVSpeechSynthesizer alloc] init];
    @autoreleasepool {
        [synthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        if (!words.empty()) [synthesizer speakUtterance:[AVSpeechUtterance speechUtteranceWithString:text(words)]];
    }
}

namespace {

// A video through AVFoundation: a reader for its pictures and one for its sound (each read at its
// own pace). (Its tracks are looked up the way that waits for them: fine for a file on disk.)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
class AppleVideo final : public SystemVideo {
public:
    bool start(NSURL* url) {
        asset_ = [AVURLAsset URLAssetWithURL:url options:nil];
        AVAssetTrack* video = [asset_ tracksWithMediaType:AVMediaTypeVideo].firstObject;
        if (!video) return false;
        const CGSize size = video.naturalSize;
        describe({.width = static_cast<int>(size.width),
                  .height = static_cast<int>(size.height),
                  .duration = CMTimeGetSeconds(asset_.duration),
                  .sound = [asset_ tracksWithMediaType:AVMediaTypeAudio].firstObject != nil});
        return readers();
    }
    bool nextPicture(std::vector<std::uint8_t>& pixels, int& pitch, double& time) override {
        @autoreleasepool {
            for (;;) {
                const Owned<CMSampleBufferRef, CFRelease> sample{[pictureOut_ copyNextSampleBuffer]};
                if (!sample) return false;
                CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample.get());
                if (!image) continue; // (not a picture: on to the next)
                const auto rows = CVPixelBufferGetHeight(image), stride = CVPixelBufferGetBytesPerRow(image);
                pitch = static_cast<int>(stride);
                pixels.resize(rows * stride); // (before the lock: nothing between it and the unlock can fail)
                CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
                std::memcpy(pixels.data(), CVPixelBufferGetBaseAddress(image), pixels.size());
                CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
                time = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample.get()));
                return true;
            }
        }
    }
    bool nextSound(std::vector<float>& samples) override {
        if (!soundOut_) return false;
        @autoreleasepool {
            const Owned<CMSampleBufferRef, CFRelease> sample{[soundOut_ copyNextSampleBuffer]};
            if (!sample) return false;
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample.get());
            const std::size_t bytes = block ? CMBlockBufferGetDataLength(block) : 0;
            samples.resize(bytes / sizeof(float));
            if (bytes > 0) CMBlockBufferCopyDataBytes(block, 0, samples.size() * sizeof(float), samples.data());
            return true;
        }
    }
    void restart() override {
        [pictures_ cancelReading];
        [sounds_ cancelReading];
        readers();
    }

private:
    bool readers() {
        NSError* error = nil;
        pictures_ = [AVAssetReader assetReaderWithAsset:asset_ error:&error];
        AVAssetTrack* video = [asset_ tracksWithMediaType:AVMediaTypeVideo].firstObject;
        if (!pictures_ || !video) return false;
        pictureOut_ = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:video
                                                                outputSettings:@{(__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)}];
        pictureOut_.alwaysCopiesSampleData = NO;
        [pictures_ addOutput:pictureOut_];
        if (![pictures_ startReading]) return false;
        soundOut_ = nil;
        if (AVAssetTrack* audio = [asset_ tracksWithMediaType:AVMediaTypeAudio].firstObject) {
            sounds_ = [AVAssetReader assetReaderWithAsset:asset_ error:&error];
            soundOut_ = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:audio outputSettings:@{
                AVFormatIDKey : @(kAudioFormatLinearPCM), AVLinearPCMBitDepthKey : @32, AVLinearPCMIsFloatKey : @YES,
                AVLinearPCMIsNonInterleaved : @NO, AVLinearPCMIsBigEndianKey : @NO, AVNumberOfChannelsKey : @2, AVSampleRateKey : @(kSoundRate)}];
            if (sounds_ && soundOut_) [sounds_ addOutput:soundOut_];
            if (!sounds_ || ![sounds_ startReading]) soundOut_ = nil;
        }
        return true;
    }

    AVURLAsset* asset_ = nil;
    AVAssetReader* pictures_ = nil;
    AVAssetReader* sounds_ = nil;
    AVAssetReaderTrackOutput* pictureOut_ = nil;
    AVAssetReaderTrackOutput* soundOut_ = nil;
};
#pragma clang diagnostic pop

} // namespace

std::unique_ptr<SystemVideo> SystemVideo::open(const std::string& path, const std::string* data) {
    @autoreleasepool {
        NSString* file = text(path);
        if (data) { // (from a pack: a copy on disk to read, made once)
            const std::string name = std::format("vnext-video-{:016x}{}", std::hash<std::string>{}(*data), path.substr(path.rfind('.')));
            file = [NSTemporaryDirectory() stringByAppendingPathComponent:text(name)];
            if (![[NSFileManager defaultManager] fileExistsAtPath:file] &&
                ![[NSData dataWithBytesNoCopy:const_cast<char*>(data->data()) length:data->size() freeWhenDone:NO] writeToFile:file atomically:YES])
                return nullptr;
        }
        auto video = std::make_unique<AppleVideo>();
        if (!video->start([NSURL fileURLWithPath:file])) return nullptr;
        return video;
    }
}

} // namespace vn
