#pragma once

// Sound: one mixer for the music, sounds, voices and what the developer tools listen to; made-up voices.

#include "Files.hpp"

#include <SDL3_mixer/SDL_mixer.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vn {

using MixerPtr = std::unique_ptr<MIX_Mixer, SdlDeleter<MIX_DestroyMixer>>;
using MixSoundPtr = std::unique_ptr<MIX_Audio, SdlDeleter<MIX_DestroyAudio>>;
using SoundStreamPtr = std::unique_ptr<SDL_AudioStream, SdlDeleter<SDL_DestroyAudioStream>>;
using TrackPtr = std::unique_ptr<MIX_Track, SdlDeleter<MIX_DestroyTrack>>; // (one of its own: the rest are the mixer's, gone with it)

// Music + sound effects on SDL3_mixer. Supports OGG, MP3, FLAC and WAV.
// Music uses two tracks so a new song crossfades with the old one; sound effects share a small
// pool of tracks. Missing files are replaced by generated placeholders (a short loop for music,
// a beep for sounds), so a script can be written before its sounds exist.
// If no sound device is available every call is a silent no-op.
class Sound {
public:
    // `files`: what its sounds are read from (the engine's: its pack too); none given, the disk.
    explicit Sound(const GameFiles& files = GameFiles::disk()) : files_(files) {}
    [[nodiscard]] const GameFiles& files() const { return files_; }
    ~Sound();
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;
    Sound(Sound&&) = delete;
    Sound& operator=(Sound&&) = delete;

    bool init();
    // The song before it fades out over `fadeOut` seconds (none: the crossfade).
    void playMusic(const std::string& path, bool loop = true, float fadeIn = 0.0f, float volume = 1.0f, std::optional<float> fadeOut = std::nullopt);
    void stopMusic(float fadeOut = 0.0f);
    // A one-shot effect; with `fadeOut`, the ones still playing fade out over that long (else they play on).
    void playSound(const std::string& path, float volume = 1.0f, float fadeIn = 0.0f, std::optional<float> fadeOut = std::nullopt);
    // Named channels (ambience, loops) can be stopped later; one sound per channel: the one there before
    // fades out over `fadeOut` seconds (0: stops at once).
    void playChannel(const std::string& channel, const std::string& path, bool loop, float volume, float fadeIn, float fadeOut = 0.0f);
    void stopChannel(const std::string& channel, float fadeOut); // empty name: every channel
    void playVoice(const std::string& path, float volume = 1.0f);
    // Plays made-up speech for `text` as the voice line (see "Made-up voices" below); false if it has nothing to say.
    bool playMadeUpVoice(std::string_view text, float pitch, float speed = 1.0f);
    void stopVoice();
    // While a menu covers the game its voice line waits, and goes on when the menu closes. (A voice
    // started in the meantime, say by a button, plays.) holdVoice(): the same while its text waits
    // for a click at a {pause}; it goes on when neither holds it.
    void pauseVoice(bool paused);
    void holdVoice(bool held);
    // The voice line playing now: whether there is one (a waiting one isn't playing), how loud it
    // is (0..1, its loudest moment is 1) and how much of its speech is done (0..1, it doesn't move
    // during pauses, or while it waits).
    [[nodiscard]] bool voicePlaying() const;
    [[nodiscard]] float voiceLevel() const;
    [[nodiscard]] std::optional<float> voiceProgress() const;
    // Where the voice line could wait for a {pause} that comes after `share` of its text: the
    // silence in it closest to that much of its speech (as the share said by then), if one is near.
    [[nodiscard]] std::optional<float> voiceSilenceNear(float share) const;
    [[nodiscard]] bool voiceSilent() const; // it's between words (or waiting) now
    // The history's "hear it again": a line's voice file, or its made-up speech, on a track of its
    // own, so the voice line waiting under the menu stays as it was. stopReplay() when the menu goes.
    void replayVoice(const std::string& path, float volume = 1.0f);
    bool replayMadeUpVoice(std::string_view text, float pitch, float speed = 1.0f);
    void stopReplay();
    [[nodiscard]] bool replaying() const;
    // The developer tools' Play: a file on a track of its own, while every other sound waits where
    // it was (what starts meanwhile too). They go on when it stops, or ends. A movie's: `name`'s
    // sound plays from `movie` (openStream's; null when it has none), until stopListening().
    void listen(const std::string& path);
    void listen(const std::string& name, SDL_AudioStream* movie);
    void stopListening();
    [[nodiscard]] const std::string& listening() const { return listened_; }
    // While the developer tools are open over a game: every sound of the game waits where it is (and
    // what starts meanwhile too); the tools' own Play still plays.
    void holdGame(bool held);
    void stopAll(float fadeOut);
    // Movies' sound: each movie gets a track of its own, playing what the movie pushes into the
    // stream this returns (stereo floats at `rate`) as it plays, at the music volume times
    // `volume`. Null without sound.
    SDL_AudioStream* openStream(int rate, float volume);
    void pauseStream(SDL_AudioStream* stream, bool paused);
    void closeStream(SDL_AudioStream* stream);
    void playTestSound(); // short beep used by the volume sliders
    void update(float dt);
    // Forgets loaded files so edited or newly added sounds are picked up (a reload). The next playMusic()
    // reloads the song even if the same path is already playing, crossfading to the new version.
    void clearCache();

    [[nodiscard]] const std::string& music() const { return musicPath_; }
    void setVolume(float master, float music, float sound, float voice); // 0..1 each
    void setCrossfade(float seconds) { crossfade_ = seconds; } // how long the old song fades out when the music changes
    void setDucking(float musicLevel) { duckLevel_ = musicLevel; } // music volume while a voice plays (1: unchanged)

private:
    static constexpr std::size_t kSoundTracks = 8;
    struct Channel {
        MIX_Track* tracks[2]{}; // owned by the mixer: the one playing (`current`), and the one before it, fading out
        int current = 0;
        std::string path;
        float volume = 1.0f;
    };

    // A voice line's loudness, measured once: one value per kWindow seconds.
    struct Envelope {
        std::vector<float> level;  // 0..1
        std::vector<float> spoken; // share of the speech (windows with sound) up to here, 0..1
    };
    static constexpr float kWindow = 0.02f;
    static Envelope envelope(const std::vector<float>& mono, int rate);
    const Envelope& measure(const std::string& path);
    [[nodiscard]] std::optional<std::size_t> voiceWindow() const;

    enum class Kind : std::uint8_t { Music, Sound };
    MIX_Audio* load(const std::string& path, Kind kind);
    MIX_Audio* placeholder(const std::string& key, Kind kind, std::uint64_t seed);
    void play(MIX_Audio* clip, float volume, float fadeIn = 0.0f);
    bool start(MIX_Track* track, MIX_Audio* clip, bool loop, float fadeIn);
    [[nodiscard]] Sint64 frames(MIX_Track* track, float seconds) const;

    bool ready_ = false;
    const GameFiles& files_;
    MixerPtr mixer_;
    std::array<MIX_Track*, 2> music_{};             // owned by the mixer
    std::array<MIX_Track*, kSoundTracks> sounds_{}; // owned by the mixer
    std::array<float, 2> musicGain_{1.0f, 1.0f};    // each track's own volume
    std::array<float, kSoundTracks> soundGain_{};
    std::map<std::string, Channel, std::less<>> channels_;
    // A track that speaks: the voice line, and a voice heard again from the history.
    struct Voice {
        MIX_Track* track = nullptr; // owned by the mixer
        float gain = 1.0f;
        const Envelope* envelope = nullptr; // how loud it is when (null: unknown)
        MixSoundPtr madeUpVoice;                 // made-up speech playing
        Envelope madeUpVoiceEnvelope;
    };
    Voice voice_, replay_;
    void say(Voice& v, MIX_Audio* clip, float volume, const Envelope* envelope);
    bool sayMadeUpVoice(Voice& v, std::string_view text, float pitch, float speed);
    std::unordered_map<std::string, Envelope, StringHash, std::equal_to<>> envelopes_;
    std::size_t current_ = 0;                       // index of the active music track
    std::size_t nextSound_ = 0;
    std::unordered_map<std::string, MixSoundPtr, StringHash, std::equal_to<>> cache_;
    std::string musicPath_;
    bool musicLoops_ = true;
    float masterVolume_ = 1.0f;
    float musicVolume_ = 0.8f;
    float soundVolume_ = 1.0f;
    float voiceVolume_ = 1.0f;
    float crossfade_ = 0.5f;
    float duckLevel_ = 1.0f, duck_ = 1.0f; // music's volume under voices: wanted, and now
    bool voicePaused_ = false; // under a menu
    bool voiceHeld_ = false;   // at a {pause}
    void waitVoice(bool& reason, bool wait);
    struct Stream {
        SoundStreamPtr stream;
        TrackPtr track; // (it plays stream: destroyed before it, and before the mixer: see ~Sound)
        float gain;
        bool paused = false;
    };
    std::vector<Stream> streams_; // movies playing
    void applyMusicGain();
    MIX_Track* listen_ = nullptr; // owned by the mixer
    std::string listened_;
    bool listenedMovie_ = false;
    MIX_Track* movieTrack_ = nullptr; // (its sound's)
    std::vector<MIX_Track*> held_; // what waits for it (or for the developer tools)
    bool gameHeld_ = false;
    void holdOthers();
    void releaseHeld();
    void resume(MIX_Track* track);
};

// Made-up speech for a line of text (characters with made_up_voice=): a syllable for each group of
// vowels (or each character, in scripts like Japanese), sounding like that vowel, with a puff of
// noise for consonants and pauses at commas and full stops. `pitch` is the voice's height in Hz.
// `speed`: 1 is normal, 2 twice as fast. The same text always sounds the same. Mono samples at
// `rate`; empty when there's nothing to say.
std::vector<float> madeUpVoice(std::string_view text, float pitch, int rate, float speed = 1.0f);

} // namespace vn
