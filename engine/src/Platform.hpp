#pragma once

// What the system offers, where it can (Apple.mm, Windows.cpp, Linux.cpp; Platform.cpp without): spelling, reading text
// aloud, a screen reader, and movies other than MPEG-1.

#include "Basics.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

// What the system offers for writing and for players, where it has it (Apple.mm, Windows.cpp, and
// Linux.cpp with Hunspell and speech-dispatcher; without them a word is always spelt right and nothing
// is read aloud): whether a word is spelt right in a language ("en", "es"...; empty: the system's own),
// what it may be instead, and a word to take as right from now on; and reading text aloud (it stops what
// it was reading first; empty: it just stops). And whether a screen reader is running (VoiceOver; Narrator, NVDA or JAWS;
// Orca), and words said through it (in its voice and on a braille display, stopping what it was saying; where none can
// be told: read aloud).
[[nodiscard]] bool spelledRight(std::string_view word, std::string_view language);
[[nodiscard]] std::vector<std::string> spellings(std::string_view word, std::string_view language);
void learnSpelling(std::string_view word);
void speak(std::string_view words);
[[nodiscard]] bool screenReaderOn();
void announce(std::string_view words);
// A movie the system plays (MP4, MOV and M4V: AVFoundation on a Mac, Media Foundation on Windows,
// GStreamer on Linux where it's installed; elsewhere none): its images in order, each with its time, and
// its sound. From a file (or, `data` given, from its bytes: one in a pack).
class SystemMovie {
public:
    [[nodiscard]] static std::unique_ptr<SystemMovie> open(const std::string& path, const std::string* data);
    virtual ~SystemMovie() = default;
    SystemMovie(const SystemMovie&) VN_DELETED("used through a pointer to it: never copied, so never sliced");
    SystemMovie& operator=(const SystemMovie&) VN_DELETED("used through a pointer to it: never copied, so never sliced");
    SystemMovie(SystemMovie&&) VN_DELETED("used through a pointer to it: never copied, so never sliced");
    SystemMovie& operator=(SystemMovie&&) VN_DELETED("used through a pointer to it: never copied, so never sliced");
    // What it is: its size in pixels, how long it lasts, and whether it has sound (stereo, kSoundRate a second).
    struct Info {
        int width = 0, height = 0;
        double duration = 0.0;
        bool sound = false;
    };
    [[nodiscard]] const Info& info() const { return info_; }
    static constexpr int kSoundRate = 48000;
    // The next image: blue, green, red and alpha, `pitch` bytes a row; false after the last.
    virtual bool nextImage(std::vector<std::uint8_t>& pixels, int& pitch, double& time) = 0;
    // More of its sound, the two sides' samples in turn; false after the end.
    virtual bool nextSound(std::vector<float>& samples) = 0;
    virtual void restart() = 0; // from the beginning again

protected:
    SystemMovie() = default;
    void describe(const Info& info) { info_ = info; } // (once it's open: what the system says it is)

private:
    Info info_;
};
inline constexpr std::array<std::string_view, 3> kSystemMovieTypes{".mp4", ".mov", ".m4v"};

} // namespace vn
