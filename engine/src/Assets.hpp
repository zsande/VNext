#pragma once

// The game's pictures, sounds and videos, and the made-up voices.

#include "System.hpp"

#include <SDL3_mixer/SDL_mixer.h>
#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vn {

// ---------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------

// One image holding every look of a character: a grid of equal frames ("atlas" lines). Each
// expression has a frame for every eyes + mouth pair; the mouth changes first, then the eyes,
// then the expression, reading left to right and row by row. The eyes and the mouth can instead
// be parts: small pictures of just them, drawn over the expression's frame.
struct Atlas {
    // Eyes or a mouth drawn over every frame: a grid with a row per expression and a column per
    // state (the same order as their names).
    struct Part {
        std::string image;
        float x = 0.0f, y = 0.0f; // where its top left goes on the frame, in the frame's pixels
    };
    std::string name;                     // scripts show "<name>" and "<name>_<expression>"
    std::string image;                    // the grid: images/<image>.png
    int columns = 0;                      // frames across; 0: one row per expression
    std::vector<std::string> expressions; // the first is the default
    std::vector<std::string> eyes;        // open first, closed last (blinks go through them)
    std::vector<std::string> mouth;       // closed first, widest last (talking goes through them)
    std::optional<Part> eyesPart, mouthPart;
};

// A part of a picture with a name of its own ("picture" lines), shown like any picture: a logo on a
// sheet of a menu's pieces.
struct SubPicture {
    std::string name;  // lower-case
    std::string image; // the whole picture: images/<image>
    SDL_FRect rect{};  // the part, in its pixels
};

// How closed the eyes and how open the mouth are (0..1); an atlas shows its nearest frame.
struct Frame {
    float eyes = 0.0f;
    float mouth = 0.0f;
};

// How the whole stage looks ("effect" lines): bits of a mask, 0 is as it is. The colour ones are
// pictures changed pixel by pixel; the others are drawn over the stage.
enum StageLook : unsigned { kLookGrayscale = 1, kLookSepia = 2, kLookInvert = 4, kLookBlur = 8, kLookPixelate = 16, kLookNoise = 32, kLookGlitch = 64 };
inline constexpr std::array<std::string_view, 7> kStageLooks{"grayscale", "sepia", "invert", "blur", "pixelate", "noise", "glitch"};
inline constexpr unsigned kColorLooks = kLookGrayscale | kLookSepia | kLookInvert;
// A colour with the colour effects of `looks` (grayscale or sepia first, then invert).
[[nodiscard]] SDL_Color looked(SDL_Color c, unsigned looks);

class Assets;

// A whole image, or one frame of an atlas (with its eyes and mouth parts on top).
struct Picture {
    struct Overlay {
        SDL_Texture* texture = nullptr;
        SDL_FRect src{};
        SDL_FPoint at{}; // its top left on the picture, in the picture's pixels
    };
    SDL_Texture* texture = nullptr;
    SDL_FRect src{};
    bool frame = false; // part of an atlas (drawn half a pixel in, so neighbours don't bleed in)
    std::array<Overlay, 2> overlays{};
    std::size_t overlayCount = 0;
    Assets* assets = nullptr; // for see-through drawing of pictures with parts

    explicit operator bool() const { return texture != nullptr; }
    [[nodiscard]] float w() const { return src.w; }
    [[nodiscard]] float h() const { return src.h; }
    // `part` of it, in its own pixels (without eyes and mouth parts).
    [[nodiscard]] Picture crop(const SDL_FRect& part) const;
    // `tint` multiplies its colours (black: a silhouette).
    void draw(SDL_Renderer* r, const SDL_FRect& dst, float alpha, double angle = 0.0, const SDL_FPoint* pivot = nullptr,
              bool flip = false, SDL_Color tint = {255, 255, 255, 255}) const;

private:
    void drawLayers(SDL_Renderer* r, const SDL_FRect& dst, float alpha, double angle, const SDL_FPoint* pivot, bool flip, SDL_Color tint) const;
};

// How closed `who`'s eyes are `seconds` in: a blink about every `every` seconds, now and then two.
[[nodiscard]] float blinking(std::string_view who, float seconds, float every = 4.0f);
// How open `who`'s mouth is `seconds` into a line: about `perSecond` flaps a second, each at its own
// moment, of its own length and width (a face that talks on its own rhythm).
[[nodiscard]] float flapping(std::string_view who, float seconds, float perSecond);

// Finds files in the game folder and caches images. Scripts name files without folder or
// extension ("theme" is audio/theme.ogg or .mp3...), but full paths work too.
class Assets {
public:
    Assets(SDL_Renderer* renderer, std::string root, const GameFiles& files);
    [[nodiscard]] const GameFiles& files() const { return files_; }

    [[nodiscard]] const std::string& root() const { return root_; }
    [[nodiscard]] std::string path(std::string_view relative) const { return root_ + std::string(relative); }
    // A translation's id: its own pictures (images/<id>/) and sounds (audio/<id>/) are used first.
    // Forgets loaded pictures when it changes.
    void setLanguage(std::string id);
    // The audio file a script means: audio/<name>.ogg/.mp3/.flac/.wav, or `name` as written.
    // A missing file gives the path it was expected at (the player then makes up a placeholder).
    [[nodiscard]] std::string audio(std::string_view name) const;
    // The audio file a script means, if it exists.
    [[nodiscard]] std::optional<std::string> findAudio(std::string_view name) const;
    // Whether images/ (or the language's) has a picture by this name (not an atlas's frame).
    [[nodiscard]] bool hasImageFile(std::string_view name) const;
    // The video a script means: movies/<name>.mpg (or .mpeg, .mp4, .mov, .m4v), or `name` as written.
    [[nodiscard]] std::optional<std::string> findMovie(std::string_view name) const;

    // The pictures atlases add: "<name>" and "<name>_<expression>".
    void setAtlases(std::vector<Atlas> atlases);
    void setSubPictures(const std::vector<SubPicture>& parts);
    // Sharp pixels when pictures are scaled (pixel art), else smooth. Forgets loaded pictures.
    void setPixelArt(bool pixelArt);
    // images/<name>.png (or another of kImageTypes), an atlas frame or a sub-picture; empty when missing.
    // `looks`: with colour effects (a copy, made when first needed).
    Picture picture(std::string_view name, Frame frame = {}, unsigned looks = 0);
    Picture whole(std::string_view name, Frame frame = {}, unsigned looks = 0); // the same, but never a sub-picture
    // The picture's pixels (RGBA32), for reading them (a picture-shaped wipe).
    [[nodiscard]] SurfacePtr pixels(std::string_view name) const;
    // Loads a picture (with colour effects) before it's needed; false if it already was.
    bool preload(std::string_view name, unsigned looks = 0);
    // Pictures not drawn for a while are let go when they take more than this (0: no limit).
    void setMemoryLimit(std::size_t bytes) { limit_ = bytes; }
    void beginFrame() { ++frame_; }
    void clear() {
        images_.clear();
        ownNames_.clear();
        used_ = 0;
    }
    // A texture to put a picture together in, at least w x h (see Picture::draw).
    SDL_Texture* scratch(int w, int h);

private:
    struct Image {
        TexturePtr texture; // null: missing (remembered, so placeholders don't hit the disk every frame)
        std::size_t bytes = 0;
        std::uint64_t lastFrame = 0;
    };
    [[nodiscard]] bool exists(const std::string& path) const;
    [[nodiscard]] std::optional<std::string> find(std::string_view folder, std::string_view name, std::span<const std::string_view> types) const;
    // The file's own name for what scripts call it: as written, or one with spaces, dots... in its
    // name ("19. YOMI" for 19_YOMI).
    [[nodiscard]] std::string ownName(const std::string& folder, std::string_view name, std::span<const std::string_view> types) const;
    mutable std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> ownNames_; // by folder: script name -> its own (looked up once)
    [[nodiscard]] SurfacePtr filePixels(std::string_view name) const;
    [[nodiscard]] std::string imageFolder(std::string_view key) const; // images/, or the language's own when it has that picture
    SDL_Texture* image(std::string_view name, unsigned looks = 0);
    void trim();

    SDL_Renderer* renderer_;
    std::string root_;
    const GameFiles& files_; // (the engine's)
    std::unordered_map<std::string, Image, StringHash, std::equal_to<>> images_;
    std::vector<Atlas> atlases_;
    std::unordered_map<std::string, SubPicture, StringHash, std::equal_to<>> subPictures_; // by lower-case name
    bool pixelArt_ = false;
    std::string language_; // a translation's folder, with a slash ("spanish/"), or empty
    // lower-case picture name -> atlas and expression
    std::unordered_map<std::string, std::pair<std::size_t, std::size_t>, StringHash, std::equal_to<>> frames_;
    std::size_t used_ = 0, limit_ = 0; // bytes of pictures loaded, and the most wanted
    std::uint64_t frame_ = 0;
    TexturePtr scratch_;
};

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

using MixerPtr = std::unique_ptr<MIX_Mixer, SdlDeleter<MIX_DestroyMixer>>;
using MixAudioPtr = std::unique_ptr<MIX_Audio, SdlDeleter<MIX_DestroyAudio>>;
using AudioStreamPtr = std::unique_ptr<SDL_AudioStream, SdlDeleter<SDL_DestroyAudioStream>>;
using TrackPtr = std::unique_ptr<MIX_Track, SdlDeleter<MIX_DestroyTrack>>; // (one of its own: the rest are the mixer's, gone with it)

// Music + sound effects on SDL3_mixer. Supports OGG, MP3, FLAC and WAV.
// Music uses two tracks so a new song crossfades with the old one; sound effects share a small
// pool of tracks. Missing files are replaced by generated placeholders (a short loop for music,
// a blip for sounds), so a script can be written before the audio exists.
// If no audio device is available every call is a silent no-op.
class Audio {
public:
    // `files`: what its sounds are read from (the engine's: its pack too); none given, the disk.
    explicit Audio(const GameFiles& files = GameFiles::disk()) : files_(files) {}
    [[nodiscard]] const GameFiles& files() const { return files_; }
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&&) = delete;
    Audio& operator=(Audio&&) = delete;

    bool init();
    void playMusic(const std::string& path, bool loop = true, float fadeIn = 0.0f, float volume = 1.0f);
    void stopMusic(float fadeOut = 0.0f);
    void playSound(const std::string& path, float volume = 1.0f); // one-shot effect
    // Named channels (ambience, loops) can be stopped later; one sound per channel.
    void playChannel(const std::string& channel, const std::string& path, bool loop, float volume, float fadeIn);
    void stopChannel(const std::string& channel, float fadeOut); // empty name: every channel
    void playVoice(const std::string& path, float volume = 1.0f);
    // Plays made-up speech for `text` as the voice line (see "Made-up voices" below); false if it has nothing to say.
    bool playBabble(std::string_view text, float pitch, float speed = 1.0f);
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
    bool replayBabble(std::string_view text, float pitch, float speed = 1.0f);
    void stopReplay();
    [[nodiscard]] bool replaying() const;
    // The developer tools' Play: a file on a track of its own, while every other sound waits where
    // it was (what starts meanwhile too). They go on when it stops, or ends. A video's: `name`'s
    // sound plays from `video` (openStream's; null when it has none), until stopListening().
    void listen(const std::string& path);
    void listen(const std::string& name, SDL_AudioStream* video);
    void stopListening();
    [[nodiscard]] const std::string& listening() const { return listened_; }
    // While the developer tools are open over a game: every sound of the game waits where it is (and
    // what starts meanwhile too); the tools' own Play still plays.
    void holdGame(bool held);
    void stopAll(float fadeOut);
    // Videos' sound: each video gets a track of its own, playing what the video pushes into the
    // stream this returns (stereo floats at `rate`) as it plays, at the music volume times
    // `volume`. Null without audio.
    SDL_AudioStream* openStream(int rate, float volume);
    void pauseStream(SDL_AudioStream* stream, bool paused);
    void closeStream(SDL_AudioStream* stream);
    void playTestSound(); // short blip used by the volume sliders
    void update(float dt);
    // Forgets loaded files so edited or newly added audio is picked up (F6). The next playMusic()
    // reloads the song even if the same path is already playing, crossfading to the new version.
    void clearCache();
    // Paths that got a generated placeholder since the last call (for an on-screen notice).

    [[nodiscard]] const std::string& music() const { return musicPath_; }
    void setVolume(float master, float music, float sound, float voice); // 0..1 each
    void setCrossfade(float seconds) { crossfade_ = seconds; } // how long the old song fades out when the music changes
    void setDucking(float musicLevel) { duckLevel_ = musicLevel; } // music volume while a voice plays (1: unchanged)

private:
    static constexpr std::size_t kSoundTracks = 8;
    struct Channel {
        MIX_Track* track = nullptr; // owned by the mixer
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
    void play(MIX_Audio* audio, float volume);
    bool start(MIX_Track* track, MIX_Audio* audio, bool loop, float fadeIn);
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
        MixAudioPtr babble;                 // made-up speech playing
        Envelope babbleEnvelope;
    };
    Voice voice_, replay_;
    void say(Voice& v, MIX_Audio* audio, float volume, const Envelope* envelope);
    bool sayBabble(Voice& v, std::string_view text, float pitch, float speed);
    std::unordered_map<std::string, Envelope, StringHash, std::equal_to<>> envelopes_;
    std::size_t current_ = 0;                       // index of the active music track
    std::size_t nextSound_ = 0;
    std::unordered_map<std::string, MixAudioPtr, StringHash, std::equal_to<>> cache_;
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
        AudioStreamPtr stream;
        TrackPtr track; // (it plays stream: destroyed before it, and before the mixer: see ~Audio)
        float gain;
        bool paused = false;
    };
    std::vector<Stream> streams_; // videos playing
    void applyMusicGain();
    MIX_Track* listen_ = nullptr; // owned by the mixer
    std::string listened_;
    bool listenedVideo_ = false;
    MIX_Track* videoTrack_ = nullptr; // (its sound's)
    std::vector<MIX_Track*> held_; // what waits for it (or for the developer tools)
    bool gameHeld_ = false;
    void holdOthers();
    void releaseHeld();
    void resume(MIX_Track* track);
};

// ---------------------------------------------------------------------------
// Made-up voices
// ---------------------------------------------------------------------------

// Made-up speech for a line of text (characters with babble=): a syllable for each group of
// vowels (or each character, in scripts like Japanese), sounding like that vowel, with a puff of
// noise for consonants and pauses at commas and full stops. `pitch` is the voice's height in Hz.
// `speed`: 1 is normal, 2 twice as fast. The same text always sounds the same. Mono samples at
// `rate`; empty when there's nothing to say.
std::vector<float> babble(std::string_view text, float pitch, int rate, float speed = 1.0f);

} // namespace vn

// ---------------------------------------------------------------------------
// Videos
// ---------------------------------------------------------------------------
struct plm_t;

namespace vn {

// A video playing: an .mpg file (MPEG-1 video, MP2 sound). Its pictures go into a texture as it
// plays, and its sound into a mixer track of its own (Audio::openStream).
class Movie {
public:
    // Reads `path`; check with operator bool. `volume` scales the sound (on top of the music volume).
    Movie(SDL_Renderer* renderer, Audio& audio, const std::string& path, bool loop, float volume);
    Movie(const Movie&) VN_DELETED("its decoder calls back to it");
    Movie& operator=(const Movie&) VN_DELETED("its decoder calls back to it");
    Movie(Movie&&) VN_DELETED("its decoder calls back to it");
    Movie& operator=(Movie&&) VN_DELETED("its decoder calls back to it");
    ~Movie() = default;

    explicit operator bool() const { return plm_ != nullptr || system_ != nullptr; }
    void update(float dt);       // plays `dt` seconds more
    void showFirst();            // plays on to its first picture (a still of it, for the developer tools)
    void setLoop(bool loop);     // round again at its end, or not
    [[nodiscard]] bool looping() const { return loop_; }
    void setPaused(bool paused); // its sound waits too
    [[nodiscard]] bool ended() const;
    [[nodiscard]] SDL_Texture* texture() const { return frames_ > 0 ? texture_.get() : nullptr; } // null until the first picture
    [[nodiscard]] float width() const { return width_; }   // as it should be shown
    [[nodiscard]] float height() const { return height_; }
    [[nodiscard]] double time() const;     // seconds played
    [[nodiscard]] double duration() const;
    [[nodiscard]] int frames() const { return frames_; } // pictures shown so far
    [[nodiscard]] SDL_AudioStream* sound() const { return sound_.get(); } // (Audio::openStream's; null without sound)

private:
    struct Callbacks; // pl_mpeg's: a new picture, new sound
    friend struct Callbacks;

    void pushSound(const float* interleaved, int count); // plays `count` stereo samples of the video's sound

    SDL_Renderer* renderer_;
    Audio& audio_;
    std::string data_; // the whole file (pl_mpeg reads it from memory)
    struct PlmDeleter {
        void operator()(plm_t* p) const noexcept;
    };
    std::unique_ptr<plm_t, PlmDeleter> plm_; // (after data_: it reads from it, so it goes first)
    // A video the system plays instead (MP4, MOV, M4V on a Mac): the next picture, waiting for its time.
    void updateSystem(float dt);
    std::unique_ptr<SystemVideo> system_;
    std::vector<std::uint8_t> picture_, shownPicture_; // (the next one, and the one shown)
    std::vector<float> samples_;
    int pitch_ = 0;
    double pictureTime_ = -1.0, time_ = 0.0; // (-1: no more pictures)
    bool loop_ = false, soundEnded_ = false;
    TexturePtr texture_;
    struct StreamCloser { // (its sound is Audio's stream: Audio closes it)
        Audio* audio = nullptr;
        void operator()(SDL_AudioStream* s) const noexcept { audio->closeStream(s); }
    };
    std::unique_ptr<SDL_AudioStream, StreamCloser> sound_;
    float width_ = 0.0f, height_ = 0.0f;
    int frames_ = 0;
    bool paused_ = false;
};

} // namespace vn
