// What macOS offers the player through its own frameworks: spelling, reading text aloud and movies
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

// A movie through AVFoundation: a reader for its images and one for its sound (each read at its
// own pace). (Its tracks are looked up the way that waits for them: fine for a file on disk.)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
class AppleMovie final : public SystemMovie {
public:
    bool start(NSURL* url) {
        asset_ = [AVURLAsset URLAssetWithURL:url options:nil];
        AVAssetTrack* imageTrack = [asset_ tracksWithMediaType:AVMediaTypeVideo].firstObject;
        if (!imageTrack) return false;
        const CGSize size = imageTrack.naturalSize;
        describe({.width = static_cast<int>(size.width),
                  .height = static_cast<int>(size.height),
                  .duration = CMTimeGetSeconds(asset_.duration),
                  .sound = [asset_ tracksWithMediaType:AVMediaTypeAudio].firstObject != nil});
        return readers();
    }
    bool nextImage(std::vector<std::uint8_t>& pixels, int& pitch, double& time) override {
        @autoreleasepool {
            for (;;) {
                const Owned<CMSampleBufferRef, CFRelease> sample{[imageOut_ copyNextSampleBuffer]};
                if (!sample) return false;
                CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample.get());
                if (!image) continue; // (not an image: on to the next)
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
        [images_ cancelReading];
        [sounds_ cancelReading];
        readers();
    }

private:
    bool readers() {
        NSError* error = nil;
        images_ = [AVAssetReader assetReaderWithAsset:asset_ error:&error];
        AVAssetTrack* imageTrack = [asset_ tracksWithMediaType:AVMediaTypeVideo].firstObject;
        if (!images_ || !imageTrack) return false;
        imageOut_ = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:imageTrack
                                                                outputSettings:@{(__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)}];
        imageOut_.alwaysCopiesSampleData = NO;
        [images_ addOutput:imageOut_];
        if (![images_ startReading]) return false;
        soundOut_ = nil;
        if (AVAssetTrack* soundTrack = [asset_ tracksWithMediaType:AVMediaTypeAudio].firstObject) {
            sounds_ = [AVAssetReader assetReaderWithAsset:asset_ error:&error];
            soundOut_ = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:soundTrack outputSettings:@{
                AVFormatIDKey : @(kAudioFormatLinearPCM), AVLinearPCMBitDepthKey : @32, AVLinearPCMIsFloatKey : @YES,
                AVLinearPCMIsNonInterleaved : @NO, AVLinearPCMIsBigEndianKey : @NO, AVNumberOfChannelsKey : @2, AVSampleRateKey : @(kSoundRate)}];
            if (sounds_ && soundOut_) [sounds_ addOutput:soundOut_];
            if (!sounds_ || ![sounds_ startReading]) soundOut_ = nil;
        }
        return true;
    }

    AVURLAsset* asset_ = nil;
    AVAssetReader* images_ = nil;
    AVAssetReader* sounds_ = nil;
    AVAssetReaderTrackOutput* imageOut_ = nil;
    AVAssetReaderTrackOutput* soundOut_ = nil;
};
#pragma clang diagnostic pop

} // namespace

std::unique_ptr<SystemMovie> SystemMovie::open(const std::string& path, const std::string* data) {
    @autoreleasepool {
        NSString* file = text(path);
        if (data) { // (from a pack: a copy on disk to read, made once)
            const auto dot = path.rfind('.'); // (what it is goes by its ending)
            const std::string name = std::format("vnext-movie-{:016x}{}", std::hash<std::string>{}(*data), dot == std::string::npos ? std::string{} : path.substr(dot));
            file = [NSTemporaryDirectory() stringByAppendingPathComponent:text(name)];
            if (![[NSFileManager defaultManager] fileExistsAtPath:file] &&
                ![[NSData dataWithBytesNoCopy:const_cast<char*>(data->data()) length:data->size() freeWhenDone:NO] writeToFile:file atomically:YES])
                return nullptr;
        }
        auto movie = std::make_unique<AppleMovie>();
        if (!movie->start([NSURL fileURLWithPath:file])) return nullptr;
        return movie;
    }
}

} // namespace vn
