// Movies: MPEG-1 played by pl_mpeg (its sound through the mixer), the others by the system (Platform.hpp).
#include "Movie.hpp"

#include <algorithm>
#include <pl_mpeg.h>

namespace vn {

struct Movie::Callbacks {
    static void movie(plm_t*, plm_frame_t* frame, void* user) {
        auto* self = static_cast<Movie*>(user);
        if (!self->texture_) return;
        const SDL_Rect area{0, 0, static_cast<int>(frame->width), static_cast<int>(frame->height)};
        SDL_UpdateYUVTexture(self->texture_.get(), &area, frame->y.data, static_cast<int>(frame->y.width), frame->cb.data,
                             static_cast<int>(frame->cb.width), frame->cr.data, static_cast<int>(frame->cr.width));
        ++self->frames_;
    }
    static void sound(plm_t*, plm_samples_t* samples, void* user) {
        withoutThrowing([&] { static_cast<Movie*>(user)->pushSound(samples->interleaved, static_cast<int>(samples->count)); }); // (pl_mpeg is C)
    }
};

void Movie::PlmDeleter::operator()(plm_t* p) const noexcept { plm_destroy(p); }

Movie::Movie(SDL_Renderer* renderer, Sound& sound, const std::string& path, bool loop, float volume)
    : renderer_(renderer), sound_(sound), stream_(nullptr, StreamCloser{&sound}) {
    if (const auto dot = path.rfind('.'); dot != std::string::npos && std::ranges::contains(kSystemMovieTypes, lower(path.substr(dot)))) {
        SDL_PathInfo info{};
        const bool onDisk = SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
        const auto bytes = onDisk ? std::nullopt : sound.files().read(path); // (in a pack)
        if (onDisk || bytes) system_ = SystemMovie::open(path, bytes ? &*bytes : nullptr);
        if (!system_) {
#if defined(__linux__)
            logWarn("Can't play '{}': on Linux, MP4, MOV and M4V movies play with GStreamer and its plugins installed (an MPEG-1 movie, .mpg, plays everywhere).", path);
#elif defined(__APPLE__) || defined(_WIN32)
            logWarn("Can't play '{}'.", path);
#else
            logWarn("Can't play '{}': MP4, MOV and M4V movies play on Macs, Windows and Linux (an MPEG-1 movie, .mpg, plays everywhere).", path);
#endif
            return;
        }
        const SystemMovie::Info& movie = system_->info();
        width_ = static_cast<float>(movie.width);
        height_ = static_cast<float>(movie.height);
        texture_.reset(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, movie.width, movie.height));
        if (!texture_) logWarn("Can't show '{}': {}.", path, SDL_GetError());
        else SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_LINEAR);
        loop_ = loop;
        if (movie.sound) stream_.reset(sound_.openStream(SystemMovie::kSoundRate, volume));
        if (!system_->nextImage(image_, pitch_, imageTime_)) imageTime_ = -1.0;
        return;
    }
    auto data = sound.files().read(path);
    if (!data) return;
    data_ = std::move(*data);
    plm_.reset(plm_create_with_memory(reinterpret_cast<uint8_t*>(data_.data()), data_.size(), 0));
    if (plm_ && plm_get_num_video_streams(plm_.get()) == 0) plm_probe(plm_.get(), 1024 * 1024); // headers that don't say
    if (!plm_ || plm_get_num_video_streams(plm_.get()) == 0) {
        logWarn("Can't play '{}': it isn't an MPEG-1 movie (.mpg).", path);
        plm_.reset();
        return;
    }
    const int w = plm_get_width(plm_.get()), h = plm_get_height(plm_.get());
    const double aspect = plm_get_pixel_aspect_ratio(plm_.get());
    width_ = static_cast<float>(w * (aspect > 0.0 ? aspect : 1.0));
    height_ = static_cast<float>(h);
    const SdlProperties props;
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_IYUV);
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT601_LIMITED); // MPEG-1's colours
    texture_.reset(SDL_CreateTextureWithProperties(renderer_, props.id()));
    if (!texture_) logWarn("Can't show '{}': {}.", path, SDL_GetError());
    else SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_LINEAR);

    plm_set_loop(plm_.get(), loop);
    plm_set_video_decode_callback(plm_.get(), &Callbacks::movie, this);
    if (plm_get_num_audio_streams(plm_.get()) > 0) stream_.reset(sound_.openStream(plm_get_samplerate(plm_.get()), volume));
    if (stream_) {
        plm_set_audio_decode_callback(plm_.get(), &Callbacks::sound, this);
        plm_set_audio_lead_time(plm_.get(), 0.15); // decoded a little ahead, so the sound never runs dry
    } else {
        plm_set_audio_enabled(plm_.get(), 0);
    }
}

void Movie::showFirst() {
    for (int i = 0; i < 20 && !texture() && !ended(); ++i) update(0.02f); // (its first image may come a little after the start)
}

void Movie::setLoop(bool loop) {
    loop_ = loop;
    if (plm_) plm_set_loop(plm_.get(), loop);
}

void Movie::update(float dt) {
    if (system_) return updateSystem(dt);
    if (!plm_ || paused_ || plm_has_ended(plm_.get())) return;
    plm_decode(plm_.get(), static_cast<double>(dt));
}

// The images whose time has come (the last shown), and its sound kept half a second ahead.
void Movie::updateSystem(float dt) {
    if (paused_ || ended()) return;
    time_ += dt;
    bool shown = false;
    while (imageTime_ >= 0.0 && imageTime_ <= time_) {
        shown = true;
        std::swap(image_, shownImage_);
        const int pitch = pitch_;
        if (!system_->nextImage(image_, pitch_, imageTime_)) imageTime_ = -1.0;
        if (texture_ && !shownImage_.empty()) SDL_UpdateTexture(texture_.get(), nullptr, shownImage_.data(), pitch);
    }
    if (shown) ++frames_;
    constexpr int kAhead = SystemMovie::kSoundRate * 2 * static_cast<int>(sizeof(float)) / 2;
    while (stream_ && !soundEnded_ && SDL_GetAudioStreamQueued(stream_.get()) < kAhead) {
        if (!system_->nextSound(samples_)) soundEnded_ = true;
        else pushSound(samples_.data(), static_cast<int>(samples_.size() / 2));
    }
    if (imageTime_ < 0.0 && loop_) { // (round again)
        system_->restart();
        time_ = 0.0;
        soundEnded_ = false;
        if (!system_->nextImage(image_, pitch_, imageTime_)) imageTime_ = -1.0;
    }
}

void Movie::setPaused(bool paused) {
    if (paused == paused_) return;
    paused_ = paused;
    if (stream_) sound_.pauseStream(stream_.get(), paused);
}

bool Movie::ended() const {
    if (system_) return imageTime_ < 0.0 && !loop_;
    return !plm_ || plm_has_ended(plm_.get());
}

double Movie::time() const {
    if (system_) return time_;
    return plm_ ? plm_get_time(plm_.get()) : 0.0;
}

double Movie::duration() const {
    if (system_) return system_->info().duration;
    return plm_ ? plm_get_duration(plm_.get()) : 0.0;
}

void Movie::pushSound(const float* interleaved, int count) {
    if (stream_ && count > 0) SDL_PutAudioStreamData(stream_.get(), interleaved, count * 2 * static_cast<int>(sizeof(float)));
}

} // namespace vn
