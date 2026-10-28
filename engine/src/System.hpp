#pragma once

// What everything uses: small helpers and logging, the game's files (its folder or its pack), saves, and
// the player's settings.

#include <SDL3/SDL.h>
#include <array>
#include <cctype>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

// A deleted function with its reason, which the compiler says where it's used: C++26's = delete("...")
// (a plain = delete where the compiler doesn't have it yet).
#if defined(__cpp_deleted_function) && __cpp_deleted_function >= 202403L
#define VN_DELETED(reason) = delete(reason)
#else
#define VN_DELETED(reason) = delete
#endif

namespace vn {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Logical resolution ("config resolution" in the script). Everything is laid out in these
// coordinates and scaled to the window. The engine has the one in effect (Engine::canvas()).
struct Canvas {
    float w = 1280.0f;
    float h = 720.0f;
};

// unique_ptr deleters for SDL handles (and others one call frees: CFRelease, SDL_free).
template <auto Fn>
struct SdlDeleter {
    template <class T>
    void operator()(T* p) const noexcept { if (p) Fn(p); }
};

using WindowPtr = std::unique_ptr<SDL_Window, SdlDeleter<SDL_DestroyWindow>>;
using RendererPtr = std::unique_ptr<SDL_Renderer, SdlDeleter<SDL_DestroyRenderer>>;
using TexturePtr = std::unique_ptr<SDL_Texture, SdlDeleter<SDL_DestroyTexture>>;
using SurfacePtr = std::unique_ptr<SDL_Surface, SdlDeleter<SDL_DestroySurface>>;
using CursorPtr = std::unique_ptr<SDL_Cursor, SdlDeleter<SDL_DestroyCursor>>;
using IOStreamPtr = std::unique_ptr<SDL_IOStream, SdlDeleter<SDL_CloseIO>>;
using GamepadPtr = std::unique_ptr<SDL_Gamepad, SdlDeleter<SDL_CloseGamepad>>;
// A handle (Ref: a pointer type, such as SDL_Process* or CFURLRef) that Fn frees when it goes.
template <class Ref, auto Fn>
using Owned = std::unique_ptr<std::remove_pointer_t<Ref>, SdlDeleter<Fn>>;
// What SDL hands back for the caller to SDL_free: bytes (SdlOwned<>), a string (SdlOwned<char>), a list (SdlOwned<char*>).
struct SdlFree {
    template <class T>
    void operator()(T* p) const noexcept { SDL_free(static_cast<void*>(p)); }
};
template <class T = void>
using SdlOwned = std::unique_ptr<T, SdlFree>;

// An SDL property set (the options a call takes), destroyed when it goes.
class SdlProperties {
public:
    SdlProperties() : id_(SDL_CreateProperties()) {}
    ~SdlProperties() {
        if (id_) SDL_DestroyProperties(id_);
    }
    SdlProperties(const SdlProperties&) = delete;
    SdlProperties& operator=(const SdlProperties&) = delete;
    SdlProperties(SdlProperties&&) = delete;
    SdlProperties& operator=(SdlProperties&&) = delete;
    [[nodiscard]] SDL_PropertiesID id() const { return id_; }

private:
    SDL_PropertiesID id_;
};

// The renderer drawing into `target` (null: the window) while it lives, then into what it drew into
// before. SDL keeps each target's own scale, viewport and clip, so going back puts those back too; a
// `scale` given is the new target's.
class RenderTarget {
public:
    RenderTarget(SDL_Renderer* renderer, SDL_Texture* target) : r_(renderer), previous_(SDL_GetRenderTarget(renderer)) { SDL_SetRenderTarget(r_, target); }
    RenderTarget(SDL_Renderer* renderer, SDL_Texture* target, float scale) : RenderTarget(renderer, target) { SDL_SetRenderScale(r_, scale, scale); }
    ~RenderTarget() { SDL_SetRenderTarget(r_, previous_); }
    RenderTarget(const RenderTarget&) VN_DELETED("one switch, put back once");
    RenderTarget& operator=(const RenderTarget&) VN_DELETED("one switch, put back once");
    RenderTarget(RenderTarget&&) VN_DELETED("one switch, put back once");
    RenderTarget& operator=(RenderTarget&&) VN_DELETED("one switch, put back once");

private:
    SDL_Renderer* r_;
    SDL_Texture* previous_;
};

// Drawing cut off outside `area` (and outside what was cut off already) while it lives; then cut off as
// it was before.
class RenderClip {
public:
    RenderClip(SDL_Renderer* renderer, const SDL_FRect& area) : r_(renderer), had_(SDL_RenderClipEnabled(renderer)) {
        if (had_) SDL_GetRenderClipRect(r_, &previous_);
        SDL_Rect rect{static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.w), static_cast<int>(area.h)};
        if (had_ && !SDL_GetRectIntersection(&previous_, &rect, &rect)) rect = {}; // (nothing of it inside: nothing shows)
        SDL_SetRenderClipRect(r_, &rect);
    }
    ~RenderClip() { SDL_SetRenderClipRect(r_, had_ ? &previous_ : nullptr); }
    RenderClip(const RenderClip&) VN_DELETED("one cut, put back once");
    RenderClip& operator=(const RenderClip&) VN_DELETED("one cut, put back once");
    RenderClip(RenderClip&&) VN_DELETED("one cut, put back once");
    RenderClip& operator=(RenderClip&&) VN_DELETED("one cut, put back once");

private:
    SDL_Renderer* r_;
    bool had_;
    SDL_Rect previous_{};
};

// Runs `done` when it goes, however the scope is left: what must be put back or tidied away.
template <class F>
class ScopeExit {
public:
    explicit ScopeExit(F done) : done_(std::move(done)) {}
    ~ScopeExit() {
        if (armed_) done_();
    }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;
    void dismiss() { armed_ = false; } // (not needed after all)

private:
    F done_;
    bool armed_ = true;
};

// What C code calls back (SDL, pl_mpeg) must not let an exception go back through it: one is caught here,
// and `failed` given back instead (the other form: nothing).
template <class R, class F>
R withoutThrowing(R failed, F&& body) noexcept {
    try {
        return std::forward<F>(body)();
    } catch (...) {
        return failed;
    }
}
template <class F>
void withoutThrowing(F&& body) noexcept {
    try {
        std::forward<F>(body)();
    } catch (...) { // (what it was doing is dropped: a log line, files picked, a video's sound)
    }
}

template <class... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };

// Heterogeneous lookup for unordered containers keyed by std::string.
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

template <class... Args>
void logInfo(std::format_string<Args...> fmt, Args&&... args) {
    SDL_Log("%s", std::format(fmt, std::forward<Args>(args)...).c_str());
}

template <class... Args>
void logWarn(std::format_string<Args...> fmt, Args&&... args) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", std::format(fmt, std::forward<Args>(args)...).c_str());
}

template <class... Args>
void logError(std::format_string<Args...> fmt, Args&&... args) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", std::format(fmt, std::forward<Args>(args)...).c_str());
}

constexpr SDL_Color withAlpha(SDL_Color c, float a) {
    return {c.r, c.g, c.b, static_cast<Uint8>(static_cast<float>(c.a) * (a < 0 ? 0 : a > 1 ? 1 : a))};
}

constexpr float easeInOut(float t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3.0f - 2.0f * t);
}

inline bool pointInRect(float x, float y, const SDL_FRect& r) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

// Spaces before a line's text (a tab counts as 4).
inline int indentOf(std::string_view line) {
    int n = 0;
    for (const char c : line) {
        if (c == ' ') ++n;
        else if (c == '\t') n += 4;
        else break;
    }
    return n;
}
// A letter of any alphabet: ASCII, or a byte of a UTF-8 character (ő, é, 日); or a digit too.
inline bool letterByte(char c) { return std::isalpha(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80; }
inline bool letterOrDigitByte(char c) { return std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80; }
// A file's name without its ending ("ls_txt01.png": "ls_txt01").
inline std::string stemOf(std::string_view file) { return std::string(file.substr(0, file.rfind('.'))); }
// Lines as one text, with a line break after each.
inline std::string joinedLines(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) out += line + "\n";
    return out;
}
[[nodiscard]] std::string withSlash(std::string path); // a folder's path, with a slash at its end
[[nodiscard]] bool isDirectory(const std::string& path);

// FNV-1a; used for script hashes and placeholder colours.
constexpr std::uint64_t fnv1a(std::string_view s, std::uint64_t h = 1469598103934665603ull) {
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// The shadow drawn under text (styles: shadow=, shadow_color=, shadow_offset=).
struct TextShadow {
    bool on = true;
    SDL_Color color{0, 0, 0, 153};
    float offset = 0.0f; // canvas pixels; 0: one sixteenth of a line
};

// How a line of dialogue appears: a letter, a word or a whole line at a time.
enum class Reveal : std::uint8_t { Letter, Word, Line };

namespace utf8 {
std::size_t length(std::string_view s);
std::size_t offset(std::string_view s, std::size_t codepoints); // byte offset of the n-th codepoint
} // namespace utf8

// Small and capital letters, in ASCII and the alphabets with one rule each for it (Latin with its
// accents, Greek, Cyrillic): names compare the same however they're capitalized.
char32_t lowerOf(char32_t c);
char32_t upperOf(char32_t c);
std::string lower(std::string_view s);
inline bool sameName(std::string_view a, std::string_view b) { return lower(a) == lower(b); } // (a name: case doesn't matter)
// `base`, or base_2, base_3... (`sep` between): the first that `taken` doesn't claim.
std::string uniqueName(const std::string& base, const std::function<bool(const std::string&)>& taken, std::string_view sep = "_");
// Every byte but letters, digits and `safe` as %XX (a file:// address, trash info).
std::string percentEscaped(std::string_view text, std::string_view safe = "/-_.~");
std::string upper(std::string_view s);
std::string upperFirst(std::string_view s);        // its first letter a capital
bool startsWithCapital(std::string_view s);        // its first letter is one
std::string_view trim(std::string_view s);
// "12", "0.5", "50%" (= 0.5) or "2s" (seconds).
std::optional<float> parseNumber(std::string_view s);
// A number written in full ("2.5", "-3"), as any locale writes it in scripts; none for anything else.
[[nodiscard]] std::optional<double> parseDecimal(std::string_view s);
// on/off, yes/no, true/false, 1/0
std::optional<bool> parseBool(std::string_view s);

// Colour names: the built-in ones (white, black, red, pink, skyblue...) plus "color" lines.
using Palette = std::map<std::string, SDL_Color, std::less<>>;
// "#rgb", "#rrggbb", "#rrggbbaa" or a colour name; a gradient ("navy-black": fading down to another) gives its top.
std::optional<SDL_Color> parseColor(std::string_view text, const Palette* palette = nullptr);
// "TOP-BOTTOM": a colour fading down to another (navy-black, #203040-#000000).
std::optional<std::pair<SDL_Color, SDL_Color>> parseGradient(std::string_view text, const Palette* palette = nullptr);
// What a colour's box says for a value: its name, a code's likeness (colorName), "navy down to black" for a gradient.
std::string colorWords(std::string_view value, const Palette* palette = nullptr);
// What to call a colour: its name (the game's own, or a built-in one) when it has one, else the
// built-in one it's most like ("like navy"); a see-through one says how much ("black, 50%").
std::string colorName(SDL_Color c, const Palette* palette = nullptr);
// A colour's hue (0 to 360), saturation and value (0 to 1); a grey's hue is 0.
std::array<float, 3> hsvOf(SDL_Color c);

// ---------------------------------------------------------------------------
// The game's files
// ---------------------------------------------------------------------------

// The game's files, read from its folder or from its pack: game.vnext, the whole folder (but its
// saves) in one file, made in developer mode. A pack can't be opened and changed like a folder:
// its files are compressed and scrambled, and each one is checked when it's read.
//
// The engine reads the game through its GameFiles: once a pack is open there, the files in it have
// paths as if it were a folder ("<pack>/images/aiko.png"); everything else comes from the disk.

inline constexpr std::string_view kPackName = "game.vnext";

// The files the player reads, by their endings (in lower case): pictures, sounds, videos (MPEG-1;
// the others where the system plays them, see kSystemVideoTypes) and fonts.
inline constexpr std::array<std::string_view, 6> kImageTypes{".png", ".jpg", ".jpeg", ".bmp", ".qoi", ".gif"};
inline constexpr std::array<std::string_view, 4> kAudioTypes{".ogg", ".mp3", ".flac", ".wav"};
inline constexpr std::array<std::string_view, 5> kVideoTypes{".mpg", ".mpeg", ".mp4", ".mov", ".m4v"};
inline constexpr std::array<std::string_view, 3> kFontTypes{".ttf", ".otf", ".ttc"};

// The game's files: its pack's, once one is open (for the paths under its root), else the disk's. The
// engine has one (Engine::files()); what reads the game (pictures, sounds, fonts, videos, scripts) is
// given it.
class GameFiles {
public:
    GameFiles();
    ~GameFiles();
    GameFiles(const GameFiles&) = delete;
    GameFiles& operator=(const GameFiles&) = delete;
    GameFiles(GameFiles&&) = delete;
    GameFiles& operator=(GameFiles&&) = delete;
    // None open: just the disk (as what never reads a pack has it: the editor, a silent preview).
    [[nodiscard]] static const GameFiles& disk();
    // Opens `file` as the game's pack (the error says why it can't be used).
    [[nodiscard]] std::expected<void, std::string> openPack(const std::string& file);
    [[nodiscard]] bool exists(const std::string& path) const;
    // The whole file, or nullopt if it can't be read; the same as text (without a UTF-8 BOM).
    [[nodiscard]] std::optional<std::string> read(const std::string& path) const;
    [[nodiscard]] std::optional<std::string> readText(const std::string& path) const;
    // A stream that reads the file (closed when it goes: .release() it to an SDL call that closes it), or null.
    [[nodiscard]] IOStreamPtr open(const std::string& path) const;
    // The names of the files in `folder` that end with `extension` (in any case), in order.
    [[nodiscard]] std::vector<std::string> list(const std::string& folder, std::string_view extension) const;

private:
    struct Pack;
    std::unique_ptr<Pack> pack_; // (none: no pack open)
};

// The same, for files on the disk (the player's own data, the game folder in developer mode).
[[nodiscard]] bool fileExists(const std::string& path);
[[nodiscard]] std::optional<std::string> readFile(const std::string& path);
[[nodiscard]] std::optional<std::string> readTextFile(const std::string& path);
[[nodiscard]] IOStreamPtr openFile(const std::string& path);
[[nodiscard]] std::vector<std::string> listFiles(const std::string& folder, std::string_view extension);
// What scripts call a file (its name without the ending): one word, so "19. YOMI.flac" is 19_YOMI
// (spaces, dots and the like are _; letters of any language stay).
[[nodiscard]] std::string scriptName(std::string_view stem);
// Moves a file (not in a pack) to the system's Trash or Recycle Bin, where it can be got back from:
// where it is now (empty where the system doesn't say: the Recycle Bin), or none when it can't be
// moved (it stays where it is).
std::optional<std::string> moveToTrash(const std::string& path);
// What the system offers for writing and for players, where it has it (Apple.mm, Windows.cpp, and
// Linux.cpp with Hunspell and speech-dispatcher; without them a word is always spelt right and nothing
// is read aloud): whether a word is spelt right in a language
// ("en", "es"...; empty: the system's own), what it may be instead, and a word to take as right from
// now on; and reading text aloud (it stops what it was reading first; empty: it just stops).
[[nodiscard]] bool spelledRight(std::string_view word, std::string_view language);
[[nodiscard]] std::vector<std::string> spellings(std::string_view word, std::string_view language);
void learnSpelling(std::string_view word);
void speak(std::string_view words);
// A video the system plays (MP4, MOV and M4V: AVFoundation on a Mac, Media Foundation on Windows, GStreamer on
// Linux where it's installed; elsewhere none): its
// pictures in order, each with its time, and its sound. From a file (or, `data` given, from its
// bytes: one in a pack).
class SystemVideo {
public:
    [[nodiscard]] static std::unique_ptr<SystemVideo> open(const std::string& path, const std::string* data);
    virtual ~SystemVideo() = default;
    SystemVideo(const SystemVideo&) VN_DELETED("used through a pointer to it: never copied, so never sliced");
    SystemVideo& operator=(const SystemVideo&) VN_DELETED("used through a pointer to it: never copied, so never sliced");
    SystemVideo(SystemVideo&&) VN_DELETED("used through a pointer to it: never copied, so never sliced");
    SystemVideo& operator=(SystemVideo&&) VN_DELETED("used through a pointer to it: never copied, so never sliced");
    // What it is: its size in pixels, how long it lasts, and whether it has sound (stereo, kSoundRate a second).
    struct Info {
        int width = 0, height = 0;
        double duration = 0.0;
        bool sound = false;
    };
    [[nodiscard]] const Info& info() const { return info_; }
    static constexpr int kSoundRate = 48000;
    // The next picture: blue, green, red and alpha, `pitch` bytes a row; false after the last.
    virtual bool nextPicture(std::vector<std::uint8_t>& pixels, int& pitch, double& time) = 0;
    // More of its sound, the two sides' samples in turn; false after the end.
    virtual bool nextSound(std::vector<float>& samples) = 0;
    virtual void restart() = 0; // from the beginning again

protected:
    SystemVideo() = default;
    void describe(const Info& info) { info_ = info; } // (once it's open: what the system says it is)

private:
    Info info_;
};
inline constexpr std::array<std::string_view, 3> kSystemVideoTypes{".mp4", ".mov", ".m4v"};
// The running player: its .app on a Mac (when it's in one), else its program file.
[[nodiscard]] std::string playerPath();
// Runs a program (its path, then what it's given) and waits for it: whether it went well.
bool runProgram(const std::vector<std::string>& args);
// Copies a file or a folder with everything in it (programs stay programs): false when it can't.
bool copyFiles(const std::string& from, const std::string& to);
// The folder (and everything in it, under the folder's own name) as a zip file, as it is (not
// compressed again): why not, if it can't be.
[[nodiscard]] std::optional<std::string> writeZip(const std::string& folder, const std::string& file);

// Packs the game folder `folder` (with a slash at the end) into the file `file`: everything in it
// but saves/ and hidden files.
struct PackReport {
    std::size_t files = 0;
    std::uint64_t bytes = 0;  // the files' own size
    std::uint64_t packed = 0; // the pack's
};
[[nodiscard]] std::expected<PackReport, std::string> writePack(const std::string& folder, const std::string& file);

// ---------------------------------------------------------------------------
// Saves
// ---------------------------------------------------------------------------

struct SaveInfo {
    bool exists = false;
    SDL_Time time = 0;
    std::string summary; // speaker and line shown when the save was made
};

// Save slots on disk: one file each, <folder>/<page>-<n>.vnsave. It holds the game state as lines of
// text (metadata last), then a "thumbnail png <bytes>" line and the thumbnail picture.
// Pages are "auto" and "quick" (written round-robin) plus the numbered pages "1".."N".
class SaveStore {
public:
    void setFolder(std::string folder);
    void configure(int pages, int slotsPerPage);

    [[nodiscard]] const std::vector<std::string>& pages() const { return pages_; }
    [[nodiscard]] int slotsPerPage() const { return slots_; }
    [[nodiscard]] static std::string slotName(std::string_view page, int index) { return std::format("{}-{}", page, index + 1); }
    [[nodiscard]] std::string path(std::string_view slot) const { return std::format("{}{}.vnsave", folder_, slot); }

    [[nodiscard]] SaveInfo info(std::string_view slot) const;
    [[nodiscard]] std::optional<std::string> read(std::string_view slot) const; // the game state
    [[nodiscard]] std::string thumbnail(std::string_view slot) const;           // PNG bytes (empty: none)
    bool write(std::string_view slot, std::string_view state, std::string_view summary, SDL_Surface* thumbnail);
    void remove(std::string_view slot);

    // First empty slot of a page, otherwise its oldest one (for autosaves and quick saves).
    [[nodiscard]] std::string rotatingSlot(std::string_view page) const;
    // Most recent save on one page, or on any page when `page` is empty.
    [[nodiscard]] std::optional<std::string> newest(std::string_view page = {}) const;

private:
    struct File {
        std::string text; // the state and metadata
        std::string png;  // the thumbnail
    };
    [[nodiscard]] std::optional<File> load(std::string_view slot) const;

    std::string folder_;
    std::vector<std::string> pages_{"auto", "quick"};
    int slots_ = 6;
};

// What the Save / Load screens are showing: mode, page, the slots on it (with thumbnails).
class SlotBrowser {
public:
    enum class Mode : std::uint8_t { Save, Load };
    struct Card {
        std::string slot;
        SaveInfo info;
        TexturePtr thumbnail;
    };

    void attach(SaveStore& store, SDL_Renderer* renderer) {
        store_ = &store;
        renderer_ = renderer;
    }
    void open(Mode mode);
    [[nodiscard]] Mode mode() const { return mode_; }
    [[nodiscard]] const std::vector<std::string>& pages() const { return pages_; }
    [[nodiscard]] std::size_t page() const { return page_; }
    void setPage(std::size_t page);
    void turn(int delta);
    [[nodiscard]] std::vector<Card>& cards() { return cards_; }
    void refresh(std::size_t index);

    std::optional<std::size_t> selected; // card under the mouse / keyboard focus (for "delete")

private:
    SaveStore* store_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    Mode mode_ = Mode::Load;
    std::vector<std::string> pages_;
    std::size_t page_ = 0;
    std::vector<Card> cards_;
    std::array<std::string, 2> last_; // last page per mode
};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

struct GameConfig; // (Script.hpp)

// Player preferences, stored as "name value" lines in settings.txt in the save folder.
// Defaults come from "config default.<name> value" lines in the script.
struct Settings {
    // Display
    bool fullscreen = false;
    int windowWidth = 1280;
    int windowHeight = 720;
    int scaling = 0; // fit, integer, stretch, fill
    bool vsync = true;
    int frameLimit = 0; // frames per second, 0 = unlimited
    // Audio (percent)
    int masterVolume = 100;
    int musicVolume = 80;
    int soundVolume = 100;
    int voiceVolume = 100;
    bool babble = true;       // made-up voices (characters with babble=)
    bool typingSounds = true; // the little sounds as text types (characters with blip=)
    int mouths = 0;           // when mouths move: 0 as the characters say (talk=), 1 only with a voice, 2 never
    // Text
    int textSpeed = 45;      // characters per second, 0 = instant
    int autoDelay = 500;     // milliseconds before auto-advance (plus a little per letter on lines without a voice)
    int textboxOpacity = 85; // percent
    int textSize = 100;      // percent, for text in styles with text_size=yes
    bool transitions = true;
    // Skipping and going back
    bool skipUnread = false;       // false: skipping stops at text that hasn't been read yet
    bool skipAfterChoices = false; // false: making a choice ends skip mode
    bool rollback = true;          // the mouse wheel / Back button goes back to earlier lines
    bool skipMovies = true;        // clicks and skipping go past videos (unless the story says skip=no)
    // Seeing and hearing
    bool readAloud = false;    // the system reads the lines, choices and the buttons pointed at aloud (macOS)
    bool highContrast = false; // screens drawn in white on black, the button pointed at in yellow
    // Language: a translation's id, "original" (as written), or empty: the computer's language when
    // there's a translation for it.
    std::string language;
    // Keys the player chose on the Controls screen, by control name ("none" for an emptied slot).
    std::map<std::string, std::vector<std::string>, std::less<>> keys;

    // Sets one value by its name (settings.txt, config default.NAME); false for unknown names.
    bool set(std::string_view key, int value);
    [[nodiscard]] std::optional<int> get(std::string_view key) const; // (yes: 1, no: 0)
    static Settings load(const std::string& path, const Settings& defaults);
    bool save(const std::string& path) const;
};

// Hashes of every line the player has seen (across all playthroughs), used by "skip read text only".
using ReadLog = std::unordered_set<std::uint64_t>;
ReadLog loadReadLog(const std::string& path);
bool saveReadLog(const ReadLog& log, const std::string& path);

// A player setting a slider can change (slider setting=NAME): its place on the slider (0 to 1), and
// how it reads ({NAME} on screens: "45 cps", "80%").
struct SettingControl {
    std::function<float(const Settings&)> value;
    std::function<void(Settings&, float)> setValue;
    std::function<std::string(const Settings&, const GameConfig&)> text;
    bool testSound = false; // play a blip after changing it
};
const SettingControl* findSettingControl(std::string_view name);

} // namespace vn
