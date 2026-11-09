// Sound: music, sounds, voices and what the developer tools listen to, through one mixer; and the stand-in tunes
// for files that aren't there yet.
#include "Sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace vn {

namespace {

constexpr int kPlaceholderRate = 22050;
constexpr float kTau = 6.2831853f;

float noteHz(int semitonesFromA4) {
    return 440.0f * std::exp2(static_cast<float>(semitonesFromA4) / 12.0f);
}

// A soft 4-bar loop (pad chords, an arpeggio and a bass line). Key, mode and tempo are derived
// from the file name, so different missing tracks are easy to tell apart.
std::vector<float> placeholderMusic(std::uint64_t seed) {
    const int key = static_cast<int>(seed % 12) - 9; // C4..B4 relative to A4
    const bool minor = ((seed >> 8) & 1) != 0;
    const float beat = 60.0f / (84.0f + static_cast<float>((seed >> 16) % 5) * 8.0f); // 84-116 bpm
    const float bar = beat * 4.0f;
    const auto total = static_cast<std::size_t>(bar * 4.0f * kPlaceholderRate);
    std::vector<float> out(total, 0.0f);

    // Writes wrap around the end of the buffer so tails continue seamlessly into the next loop.
    auto voice = [&](float start, float length, float hz, float gain, float attack, float decay, float harmonic) {
        const auto first = static_cast<std::size_t>(start * kPlaceholderRate);
        const auto count = static_cast<std::size_t>(length * kPlaceholderRate);
        for (std::size_t i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / kPlaceholderRate;
            const float env = std::min(1.0f, t / attack) *
                              (decay > 0.0f ? std::exp(-t * decay) : std::min(1.0f, (length - t) / attack));
            const float phase = kTau * hz * t;
            out[(first + i) % total] += (std::sin(phase) + harmonic * std::sin(2.0f * phase)) * env * gain;
        }
    };

    struct Chord { int root; bool minor; };
    static constexpr std::array<Chord, 4> majorProgression{{{0, false}, {7, false}, {9, true}, {5, false}}};  // I V vi IV
    static constexpr std::array<Chord, 4> minorProgression{{{0, true}, {8, false}, {3, false}, {10, false}}}; // i VI III VII
    static constexpr std::array<std::size_t, 8> arpeggio{0, 1, 2, 3, 2, 1, 0, 2};

    for (std::size_t b = 0; b < 4; ++b) {
        const Chord c = (minor ? minorProgression : majorProgression)[b];
        const int root = key + c.root;
        const std::array<int, 4> tones{root, root + (c.minor ? 3 : 4), root + 7, root + 12};
        const float t0 = static_cast<float>(b) * bar;
        for (std::size_t i = 0; i < 3; ++i) { // pad, slightly detuned for width
            voice(t0, bar + 0.4f, noteHz(tones[i] - 12), 0.05f, 0.4f, 0.0f, 0.0f);
            voice(t0, bar + 0.4f, noteHz(tones[i] - 12) * 1.004f, 0.03f, 0.4f, 0.0f, 0.0f);
        }
        for (std::size_t n = 0; n < arpeggio.size(); ++n) // eighth-note plucks
            voice(t0 + static_cast<float>(n) * beat * 0.5f, 1.2f, noteHz(tones[arpeggio[n]]), 0.10f, 0.004f, 5.0f, 0.25f);
        for (int n = 0; n < 2; ++n) // bass on beats 1 and 3
            voice(t0 + static_cast<float>(n) * beat * 2.0f, beat * 1.5f, noteHz(root - 24), 0.22f, 0.01f, 2.5f, 0.4f);
    }

    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::abs(v));
    if (peak > 0.0f)
        for (float& v : out) v *= 0.4f / peak;
    return out;
}

// A short beep whose pitch and direction come from the file name.
std::vector<float> placeholderSound(std::uint64_t seed) {
    const float base = 440.0f + static_cast<float>(seed % 600);
    const bool rising = ((seed >> 12) & 1) != 0;
    constexpr float length = 0.16f;
    std::vector<float> out(static_cast<std::size_t>(length * kPlaceholderRate));
    float phase = 0.0f;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kPlaceholderRate;
        const float sweep = rising ? 1.0f + 0.5f * t / length : 1.5f - 0.5f * t / length;
        phase += kTau * base * sweep / kPlaceholderRate;
        const float env = std::min(1.0f, t / 0.004f) * std::exp(-t * 18.0f);
        out[i] = (std::sin(phase) + 0.3f * std::sin(2.0f * phase)) * env * 0.35f;
    }
    return out;
}

} // namespace

Sound::~Sound() {
    while (!streams_.empty()) closeStream(streams_.back().stream.get());
    cache_.clear();  // MIX_Audio is ref-counted, so tracks still using it stay valid
    mixer_.reset();  // also destroys every track created on it
    if (ready_) MIX_Quit();
}

bool Sound::init() {
    if (!MIX_Init()) {
        logWarn("Can't start SDL_mixer (it plays the sound): {}.", SDL_GetError());
        return false;
    }
    ready_ = true;
    mixer_.reset(MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr));
    if (!mixer_) {
        logWarn("Can't open the sound device: {}, so the game plays silently.", SDL_GetError());
        return false;
    }
    for (auto& t : music_) t = MIX_CreateTrack(mixer_.get());
    for (auto& t : sounds_) t = MIX_CreateTrack(mixer_.get());
    voice_.track = MIX_CreateTrack(mixer_.get());
    replay_.track = MIX_CreateTrack(mixer_.get());
    listen_ = MIX_CreateTrack(mixer_.get());
    setVolume(masterVolume_, musicVolume_, soundVolume_, voiceVolume_);
    return true;
}

// Track gains combine the player's volume setting with each sound's own volume.
void Sound::setVolume(float master, float music, float sound, float voice) {
    masterVolume_ = master;
    musicVolume_ = music;
    soundVolume_ = sound;
    voiceVolume_ = voice;
    if (!mixer_) return;
    MIX_SetMixerGain(mixer_.get(), masterVolume_);
    applyMusicGain();
    for (std::size_t i = 0; i < sounds_.size(); ++i) MIX_SetTrackGain(sounds_[i], soundVolume_ * soundGain_[i]);
    for (const auto& entry : channels_)
        for (MIX_Track* track : entry.second.tracks)
            if (track) MIX_SetTrackGain(track, soundVolume_ * entry.second.volume);
    for (const Voice* v : {&voice_, &replay_}) MIX_SetTrackGain(v->track, voiceVolume_ * v->gain);
    for (const Stream& s : streams_) MIX_SetTrackGain(s.track.get(), musicVolume_ * s.gain);
}

MIX_Audio* Sound::load(const std::string& path, Kind kind) {
    if (auto it = cache_.find(path); it != cache_.end()) return it->second.get();

    // Music is decoded while it plays (only the compressed file stays in memory);
    // sound effects are short, so they are decoded once up front.
    IOStreamPtr file = files_.open(path);
    MixSoundPtr clip{file ? MIX_LoadAudio_IO(mixer_.get(), file.release(), kind == Kind::Sound, true) : nullptr};
    if (!clip) {
        logWarn("Can't load '{}': {}, so a stand-in plays.", path, SDL_GetError());
        const auto slash = path.find_last_of("/\\");
        return placeholder(path, kind, fnv1a(slash == std::string::npos ? path : path.substr(slash + 1)));
    }
    return cache_.emplace(path, std::move(clip)).first->second.get();
}

MIX_Audio* Sound::placeholder(const std::string& key, Kind kind, std::uint64_t seed) {
    const auto pcm = kind == Kind::Music ? placeholderMusic(seed) : placeholderSound(seed);
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kPlaceholderRate};
    MixSoundPtr clip{MIX_LoadRawAudio(mixer_.get(), pcm.data(), pcm.size() * sizeof(float), &spec)};
    return cache_.insert_or_assign(key, std::move(clip)).first->second.get();
}

Sint64 Sound::frames(MIX_Track* track, float seconds) const {
    return seconds > 0.0f ? MIX_TrackMSToFrames(track, static_cast<Sint64>(seconds * 1000.0f)) : 0;
}

bool Sound::start(MIX_Track* track, MIX_Audio* clip, bool loop, float fadeIn) {
    MIX_SetTrackAudio(track, clip);
    const SdlProperties options;
    SDL_SetNumberProperty(options.id(), MIX_PROP_PLAY_LOOPS_NUMBER, loop ? -1 : 0);
    SDL_SetNumberProperty(options.id(), MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, static_cast<Sint64>(fadeIn * 1000.0f));
    const bool ok = MIX_PlayTrack(track, options.id());
    if (!ok) logWarn("Can't play the sound: {}.", SDL_GetError());
    return ok;
}

void Sound::playMusic(const std::string& path, bool loop, float fadeIn, float volume, std::optional<float> fadeOut) {
    if (!mixer_) {
        musicPath_ = path;
        return;
    }
    if (path == musicPath_ && MIX_TrackPlaying(music_[current_])) return;
    stopMusic(fadeOut.value_or(crossfade_));
    musicPath_ = path;
    musicLoops_ = loop;

    MIX_Audio* clip = load(path, Kind::Music);
    if (!clip) return;
    current_ = 1 - current_;
    musicGain_[current_] = volume;
    applyMusicGain();
    start(music_[current_], clip, loop, fadeIn);
}

void Sound::stopMusic(float fadeOut) {
    musicPath_.clear();
    if (!mixer_) return;
    MIX_Track* track = music_[current_];
    MIX_StopTrack(track, frames(track, fadeOut));
}

void Sound::playSound(const std::string& path, float volume, float fadeIn, std::optional<float> fadeOut) {
    if (!mixer_) return;
    if (fadeOut)
        for (MIX_Track* track : sounds_)
            if (MIX_TrackPlaying(track)) MIX_StopTrack(track, frames(track, *fadeOut));
    play(load(path, Kind::Sound), volume, fadeIn);
}

void Sound::playChannel(const std::string& channel, const std::string& path, bool loop, float volume, float fadeIn, float fadeOut) {
    if (!mixer_) return;
    MIX_Audio* clip = load(path, loop ? Kind::Music : Kind::Sound);
    if (!clip) return;
    Channel& c = channels_[channel];
    for (MIX_Track*& track : c.tracks)
        if (!track) track = MIX_CreateTrack(mixer_.get());
    if (MIX_Track* before = c.tracks[c.current]; MIX_TrackPlaying(before)) MIX_StopTrack(before, frames(before, fadeOut));
    c.current = 1 - c.current;
    c.path = path;
    c.volume = volume;
    MIX_Track* track = c.tracks[c.current];
    MIX_StopTrack(track, 0); // (one still fading from before: gone)
    MIX_SetTrackGain(track, soundVolume_ * volume);
    start(track, clip, loop, fadeIn);
}

void Sound::stopChannel(const std::string& channel, float fadeOut) {
    if (!mixer_) return;
    auto stop = [&](const Channel& c) {
        for (MIX_Track* track : c.tracks)
            if (track && MIX_TrackPlaying(track)) MIX_StopTrack(track, frames(track, fadeOut));
    };
    if (channel.empty()) {
        for (const auto& entry : channels_) stop(entry.second);
        return;
    }
    if (auto it = channels_.find(channel); it != channels_.end()) stop(it->second);
}

void Sound::say(Voice& v, MIX_Audio* clip, float volume, const Envelope* envelope) {
    MIX_StopTrack(v.track, 0);
    v.gain = volume;
    v.envelope = envelope;
    MIX_SetTrackGain(v.track, voiceVolume_ * volume);
    if (clip) start(v.track, clip, false, 0.0f);
}

bool Sound::sayMadeUpVoice(Voice& v, std::string_view text, float pitch, float speed) {
    const std::vector<float> pcm = madeUpVoice(text, pitch, kPlaceholderRate, speed);
    if (pcm.empty()) return false;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kPlaceholderRate};
    MixSoundPtr clip{MIX_LoadRawAudio(mixer_.get(), pcm.data(), pcm.size() * sizeof(float), &spec)};
    if (!clip) return false;
    v.madeUpVoice = std::move(clip); // (the track keeps the one it was playing while it needs it)
    v.madeUpVoiceEnvelope = envelope(pcm, kPlaceholderRate);
    say(v, v.madeUpVoice.get(), 1.0f, &v.madeUpVoiceEnvelope);
    return true;
}

void Sound::playVoice(const std::string& path, float volume) {
    if (!mixer_) return;
    voicePaused_ = voiceHeld_ = false;
    MIX_Audio* clip = load(path, Kind::Sound);
    say(voice_, clip, volume, clip ? &measure(path) : nullptr);
}

bool Sound::playMadeUpVoice(std::string_view text, float pitch, float speed) {
    if (!mixer_) return false;
    voicePaused_ = voiceHeld_ = false;
    return sayMadeUpVoice(voice_, text, pitch, speed);
}

void Sound::stopVoice() {
    if (mixer_) MIX_StopTrack(voice_.track, frames(voice_.track, 0.1f));
    voicePaused_ = voiceHeld_ = false;
}

void Sound::replayVoice(const std::string& path, float volume) {
    if (mixer_) say(replay_, load(path, Kind::Sound), volume, nullptr);
}

bool Sound::replayMadeUpVoice(std::string_view text, float pitch, float speed) {
    return mixer_ && sayMadeUpVoice(replay_, text, pitch, speed);
}

void Sound::stopReplay() {
    if (mixer_) MIX_StopTrack(replay_.track, frames(replay_.track, 0.1f));
}

bool Sound::replaying() const {
    return mixer_ && MIX_TrackPlaying(replay_.track);
}

void Sound::pauseVoice(bool paused) { waitVoice(voicePaused_, paused); }
void Sound::holdVoice(bool held) { waitVoice(voiceHeld_, held); }

void Sound::waitVoice(bool& reason, bool wait) {
    if (!mixer_ || reason == wait) return;
    if (wait && !MIX_TrackPlaying(voice_.track) && !MIX_TrackPaused(voice_.track)) return; // nothing to wait
    reason = wait;
    const bool waiting = voicePaused_ || voiceHeld_;
    if (waiting && !MIX_TrackPaused(voice_.track)) MIX_PauseTrack(voice_.track);
    else if (!waiting && MIX_TrackPaused(voice_.track)) resume(voice_.track);
}

SDL_AudioStream* Sound::openStream(int rate, float volume) {
    if (!mixer_) return nullptr;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate};
    Stream s{.stream = SoundStreamPtr(SDL_CreateAudioStream(&spec, &spec)), .track = nullptr, .gain = volume};
    if (s.stream) s.track.reset(MIX_CreateTrack(mixer_.get()));
    if (!s.track || !MIX_SetTrackAudioStream(s.track.get(), s.stream.get())) { // (it plays once there's sound in it: see update())
        logWarn("Can't play the movie's sound: {}.", SDL_GetError());
        return nullptr;
    }
    MIX_SetTrackGain(s.track.get(), musicVolume_ * volume);
    return streams_.emplace_back(std::move(s)).stream.get();
}

void Sound::pauseStream(SDL_AudioStream* stream, bool paused) {
    for (Stream& s : streams_)
        if (s.stream.get() == stream) {
            s.paused = paused;
            if (paused) MIX_PauseTrack(s.track.get());
            else resume(s.track.get());
        }
}

void Sound::closeStream(SDL_AudioStream* stream) {
    const auto it = std::ranges::find(streams_, stream, [](const Stream& s) { return s.stream.get(); });
    if (it == streams_.end()) return;
    MIX_Track* track = it->track.get();
    std::erase(held_, track);
    if (track == movieTrack_) movieTrack_ = nullptr;
    MIX_StopTrack(track, 0);
    streams_.erase(it); // (its track, then its stream)
}

Sound::Envelope Sound::envelope(const std::vector<float>& mono, int rate) {
    Envelope e;
    const auto window = std::max<std::size_t>(1, static_cast<std::size_t>(kWindow * static_cast<float>(rate)));
    for (std::size_t i = 0; i < mono.size(); i += window) {
        const std::size_t end = std::min(mono.size(), i + window);
        double sum = 0.0;
        for (std::size_t k = i; k < end; ++k) sum += static_cast<double>(mono[k]) * mono[k];
        e.level.push_back(static_cast<float>(std::sqrt(sum / static_cast<double>(end - i))));
    }
    if (e.level.empty()) return e;

    // 1 is (nearly) the loudest moment; quiet hiss counts as silence.
    std::vector<float> sorted = e.level;
    std::ranges::sort(sorted);
    const float top = sorted[sorted.size() - 1 - sorted.size() / 50];
    for (float& v : e.level) v = top > 0.0f ? std::clamp((v / top - 0.08f) / 0.92f, 0.0f, 1.0f) : 0.0f;

    // Speech is where there's sound, bridging the short gaps between syllables (60 ms either side).
    constexpr float kSilence = 0.12f;
    constexpr std::size_t kBridge = 3;
    std::vector<bool> speaking(e.level.size());
    for (std::size_t i = 0; i < e.level.size(); ++i)
        for (std::size_t k = i > kBridge ? i - kBridge : 0; k <= std::min(e.level.size() - 1, i + kBridge); ++k)
            if (e.level[k] > kSilence) speaking[i] = true;
    const auto total = static_cast<float>(std::count(speaking.begin(), speaking.end(), true));
    float done = 0.0f;
    for (std::size_t i = 0; i < e.level.size(); ++i) {
        done += speaking[i] ? 1.0f : 0.0f;
        e.spoken.push_back(total > 0.0f ? done / total : static_cast<float>(i + 1) / static_cast<float>(e.level.size()));
    }
    return e;
}

const Sound::Envelope& Sound::measure(const std::string& path) {
    if (auto it = envelopes_.find(path); it != envelopes_.end()) return it->second;
    constexpr int kRate = 16000;
    std::vector<float> mono;
    if (const Owned<MIX_AudioDecoder*, MIX_DestroyAudioDecoder> decoder{MIX_CreateAudioDecoder_IO(files_.open(path).release(), true, 0)}) { // (in a pack too)
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kRate};
        std::vector<float> chunk(4096);
        for (int bytes; (bytes = MIX_DecodeAudio(decoder.get(), chunk.data(), static_cast<int>(chunk.size() * sizeof(float)), &spec)) > 0;)
            mono.insert(mono.end(), chunk.begin(), chunk.begin() + bytes / static_cast<int>(sizeof(float)));
    }
    return envelopes_.emplace(path, envelope(mono, kRate)).first->second;
}

bool Sound::voicePlaying() const {
    return mixer_ && MIX_TrackPlaying(voice_.track);
}

std::optional<std::size_t> Sound::voiceWindow() const {
    if (!mixer_ || (!MIX_TrackPlaying(voice_.track) && !MIX_TrackPaused(voice_.track))) return std::nullopt; // (a waiting one counts)
    const Sint64 position = MIX_GetTrackPlaybackPosition(voice_.track);
    if (position < 0) return std::nullopt;
    return static_cast<std::size_t>(static_cast<float>(MIX_TrackFramesToMS(voice_.track, position)) / (kWindow * 1000.0f));
}

float Sound::voiceLevel() const {
    const auto window = voicePlaying() ? voiceWindow() : std::nullopt;
    if (!window || !voice_.envelope || voice_.envelope->level.empty()) return 0.0f;
    return voice_.envelope->level[std::min(*window, voice_.envelope->level.size() - 1)];
}

std::optional<float> Sound::voiceProgress() const {
    const auto window = voiceWindow();
    if (!window) return std::nullopt;
    if (voice_.envelope && !voice_.envelope->spoken.empty())
        return voice_.envelope->spoken[std::min(*window, voice_.envelope->spoken.size() - 1)];
    MIX_Audio* clip = MIX_GetTrackAudio(voice_.track); // loudness unknown: by time
    const Sint64 length = clip ? MIX_GetAudioDuration(clip) : -1;
    if (length <= 0) return std::nullopt;
    const auto ms = static_cast<float>(MIX_AudioFramesToMS(clip, length));
    return std::clamp(static_cast<float>(*window) * kWindow * 1000.0f / ms, 0.0f, 1.0f);
}

std::optional<float> Sound::voiceSilenceNear(float share) const {
    if (!voice_.envelope) return std::nullopt;
    const std::vector<float>& spoken = voice_.envelope->spoken;
    std::optional<float> best;
    for (std::size_t i = 1; i < spoken.size(); ++i) // silences: windows where no more speech is done
        if (spoken[i] == spoken[i - 1] && (i < 2 || spoken[i - 1] != spoken[i - 2]) && spoken[i] > 0.0f) {
            constexpr float kNear = 0.15f;
            if (std::abs(spoken[i] - share) <= kNear && (!best || std::abs(spoken[i] - share) < std::abs(*best - share))) best = spoken[i];
        }
    return best;
}

bool Sound::voiceSilent() const {
    const auto window = voiceWindow();
    if (!window || !voice_.envelope || voice_.envelope->spoken.size() < 2) return true;
    const std::vector<float>& spoken = voice_.envelope->spoken;
    const std::size_t i = std::clamp<std::size_t>(*window, 1, spoken.size() - 1);
    return spoken[i] == spoken[i - 1];
}

void Sound::playTestSound() {
    if (!mixer_) return;
    static const std::string key = "<volume test>";
    auto it = cache_.find(key);
    play(it != cache_.end() ? it->second.get() : placeholder(key, Kind::Sound, 7), 1.0f);
}

void Sound::play(MIX_Audio* clip, float volume, float fadeIn) {
    if (!clip) return;
    // Prefer an idle track; if all are busy, reuse them round-robin (the oldest sound is cut).
    std::size_t pick = nextSound_;
    for (std::size_t i = 0; i < kSoundTracks; ++i) {
        const std::size_t candidate = (nextSound_ + i) % kSoundTracks;
        if (!MIX_TrackPlaying(sounds_[candidate])) {
            pick = candidate;
            break;
        }
    }
    nextSound_ = (pick + 1) % kSoundTracks;
    soundGain_[pick] = volume;
    MIX_SetTrackGain(sounds_[pick], soundVolume_ * volume);
    start(sounds_[pick], clip, false, fadeIn);
}

void Sound::listen(const std::string& path) {
    if (!mixer_) return;
    listenedMovie_ = false;
    movieTrack_ = nullptr;
    holdOthers();
    listened_ = path;
    MIX_SetTrackGain(listen_, 1.0f);
    start(listen_, load(path, Kind::Music), false, 0.0f);
}

void Sound::listen(const std::string& name, SDL_AudioStream* movie) {
    listened_ = name;
    listenedMovie_ = true;
    if (!mixer_) return;
    MIX_StopTrack(listen_, 0);
    const auto it = std::ranges::find(streams_, movie, [](const Stream& s) { return s.stream.get(); });
    movieTrack_ = it != streams_.end() ? it->track.get() : nullptr;
    if (movieTrack_ && std::erase(held_, movieTrack_) > 0) MIX_ResumeTrack(movieTrack_);
    holdOthers();
}

void Sound::stopListening() {
    if (listened_.empty()) return;
    listened_.clear();
    listenedMovie_ = false;
    movieTrack_ = nullptr;
    if (!mixer_) return;
    MIX_StopTrack(listen_, 0);
    if (!gameHeld_) releaseHeld();
}

void Sound::holdGame(bool held) {
    gameHeld_ = held;
    if (!mixer_) return;
    if (held) holdOthers();
    else if (listened_.empty()) releaseHeld();
}

void Sound::releaseHeld() {
    for (MIX_Track* track : std::exchange(held_, {})) {
        const bool waits = (track == voice_.track && (voicePaused_ || voiceHeld_)) ||
                           std::ranges::any_of(streams_, [&](const Stream& s) { return s.track.get() == track && s.paused; });
        if (!waits) MIX_ResumeTrack(track);
    }
}

void Sound::holdOthers() {
    auto hold = [&](MIX_Track* track) {
        if (track == movieTrack_ || !MIX_TrackPlaying(track)) return;
        MIX_PauseTrack(track);
        if (std::ranges::find(held_, track) == held_.end()) held_.push_back(track);
    };
    for (MIX_Track* track : music_) hold(track);
    for (MIX_Track* track : sounds_) hold(track);
    for (const auto& entry : channels_)
        for (MIX_Track* track : entry.second.tracks)
            if (track) hold(track);
    for (const Voice* v : {&voice_, &replay_}) hold(v->track);
    for (const Stream& s : streams_) hold(s.track.get());
}

// A track that waited for something goes on, or (while the developer tools play a file) waits for that.
void Sound::resume(MIX_Track* track) {
    if (listened_.empty() && !gameHeld_) MIX_ResumeTrack(track);
    else if (std::ranges::find(held_, track) == held_.end()) held_.push_back(track);
}

void Sound::stopAll(float fadeOut) {
    stopListening();
    stopMusic(fadeOut);
    stopChannel("", fadeOut);
    stopVoice();
    stopReplay();
}

void Sound::applyMusicGain() {
    if (!mixer_) return;
    for (std::size_t i = 0; i < music_.size(); ++i) MIX_SetTrackGain(music_[i], musicVolume_ * musicGain_[i] * duck_);
}

void Sound::update(float dt) {
    if (!mixer_) return;
    // A movie's track stops when it finds its stream empty (before the first sound, or if it runs
    // dry): it plays again when there's sound in it.
    for (const Stream& s : streams_)
        if (!s.paused && !MIX_TrackPlaying(s.track.get()) && !MIX_TrackPaused(s.track.get()) && SDL_GetAudioStreamAvailable(s.stream.get()) > 0)
            MIX_PlayTrack(s.track.get(), 0);
    if (!listened_.empty() && !listenedMovie_ && !MIX_TrackPlaying(listen_)) stopListening();
    if (!listened_.empty() || gameHeld_) holdOthers(); // (what started meanwhile waits too)
    // A non-looping song that finished is no longer "the current music" (matters for saves).
    if (!musicLoops_ && !musicPath_.empty() && !MIX_TrackPlaying(music_[current_])) musicPath_.clear();
    // Music gets quieter while someone speaks, and comes back after (over about a third of a second).
    const float target = voicePlaying() || replaying() ? duckLevel_ : 1.0f;
    if (duck_ != target) {
        const float step = dt / 0.3f;
        duck_ = duck_ < target ? std::min(target, duck_ + step) : std::max(target, duck_ - step);
        applyMusicGain();
    }
}

void Sound::clearCache() {
    cache_.clear();
    voice_.envelope = replay_.envelope = nullptr;
    envelopes_.clear();
    musicPath_.clear();
}

} // namespace vn
