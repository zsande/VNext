#pragma once

// A movie playing: MPEG-1 by pl_mpeg, the others by the system.

#include "Sound.hpp"
#include "Platform.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct plm_t;

namespace vn {

// A movie playing: an .mpg file (MPEG-1 movie, MP2 sound) by pl_mpeg, or an MP4, MOV or M4V by the system. Its
// images go into a texture as it plays, and its sound into a mixer track of its own (Sound::openStream).
class Movie {
public:
    // Reads `path`; check with operator bool. `volume` scales the sound (on top of the music volume).
    Movie(SDL_Renderer* renderer, Sound& sound, const std::string& path, bool loop, float volume);
    Movie(const Movie&) VN_DELETED("its decoder calls back to it");
    Movie& operator=(const Movie&) VN_DELETED("its decoder calls back to it");
    Movie(Movie&&) VN_DELETED("its decoder calls back to it");
    Movie& operator=(Movie&&) VN_DELETED("its decoder calls back to it");
    ~Movie() = default;

    explicit operator bool() const { return plm_ != nullptr || system_ != nullptr; }
    void update(float dt);       // plays `dt` seconds more
    void showFirst();            // plays on to its first image (a still of it, for the developer tools)
    void showLast();             // its last image (an MPEG-1 movie's; a system one stays where it is)
    void setLoop(bool loop);     // round again at its end, or not
    [[nodiscard]] bool looping() const { return loop_; }
    void setPaused(bool paused); // its sound waits too
    [[nodiscard]] bool ended() const;
    [[nodiscard]] SDL_Texture* texture() const { return frames_ > 0 ? texture_.get() : nullptr; } // null until the first image
    [[nodiscard]] float width() const { return width_; }   // as it should be shown
    [[nodiscard]] float height() const { return height_; }
    [[nodiscard]] double time() const;     // seconds played
    [[nodiscard]] double duration() const;
    [[nodiscard]] int frames() const { return frames_; } // images shown so far
    [[nodiscard]] SDL_AudioStream* stream() const { return stream_.get(); } // (Sound::openStream's; null without sound)

private:
    struct Callbacks; // pl_mpeg's: a new image, new sound
    friend struct Callbacks;

    void pushSound(const float* interleaved, int count); // plays `count` stereo samples of the movie's sound

    SDL_Renderer* renderer_;
    Sound& sound_;
    std::string data_; // the whole file (pl_mpeg reads it from memory)
    struct PlmDeleter {
        void operator()(plm_t* p) const noexcept;
    };
    std::unique_ptr<plm_t, PlmDeleter> plm_; // (after data_: it reads from it, so it goes first)
    // A movie the system plays instead (MP4, MOV, M4V: Apple.mm, Windows.cpp, Linux.cpp): the next image, waiting for its time.
    void updateSystem(float dt);
    std::unique_ptr<SystemMovie> system_;
    std::vector<std::uint8_t> image_, shownImage_; // (the next one, and the one shown)
    std::vector<float> samples_;
    int pitch_ = 0;
    double imageTime_ = -1.0, time_ = 0.0; // (-1: no more images)
    bool loop_ = false, soundEnded_ = false;
    TexturePtr texture_;
    struct StreamCloser { // (its stream is Sound's: Sound closes it)
        Sound* sound = nullptr;
        void operator()(SDL_AudioStream* s) const noexcept { sound->closeStream(s); }
    };
    std::unique_ptr<SDL_AudioStream, StreamCloser> stream_;
    float width_ = 0.0f, height_ = 0.0f;
    int frames_ = 0;
    bool paused_ = false;
};

} // namespace vn
