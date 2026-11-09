#pragma once

// Basics the whole player uses: SDL's things owned and let go of, what's undone when a scope ends, logging, and small
// helpers for words, numbers, names and colours.

#include <SDL3/SDL.h>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// A deleted function with its reason, which the compiler says where it's used: C++26's = delete("...")
// (a plain = delete where the compiler doesn't have it yet, or builds an earlier C++: clang has it there too,
// as an extension it warns about).
#if defined(__cpp_deleted_function) && __cpp_deleted_function >= 202403L && __cplusplus > 202302L
#define VN_DELETED(reason) = delete(reason)
#else
#define VN_DELETED(reason) = delete
#endif

namespace vn {

// Logical resolution ("setting game_size" in the script). Everything is laid out in these
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
    } catch (...) { // (what it was doing is dropped: a log line, files picked, a movie's sound)
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

// A place on the screen across, -1 to 1 like the stage's, as the left edge of something `size` wide on a
// `screen` that wide: ±1 lines its edge up with the screen's, past that it goes off, and at ±2 it's just off
// the screen. (Down: pass minus the place, as 1 is the top; it gives the top edge.)
[[nodiscard]] float placeEdge(float place, float size, float screen);

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

// FNV-1a; used for script hashes and placeholder colours.
constexpr std::uint64_t fnv1a(std::string_view s, std::uint64_t h = 1469598103934665603ull) {
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// The shadow drawn under text (styles: shadow=, shadow_colour=, shadow_offset=).
struct TextShadow {
    bool on = true;
    SDL_Color colour{0, 0, 0, 153};
    float offset = 0.0f; // canvas pixels; 0: one sixteenth of a line
};

// How a line of dialogue appears: a letter, a word or a whole line at a time.
enum class TextAppears : std::uint8_t { Letter, Word, Line };

namespace utf8 {

std::size_t length(std::string_view s);
std::size_t offset(std::string_view s, std::size_t codepoints); // byte offset of the n-th codepoint

} // namespace utf8

// `text`, or as much of its start as `fits` with "..." after it: cut between letters, with no space or
// comma left hanging before the dots ("..." alone when no more fits). How every text too long for its
// room is cut, in the game and in the developer tools; `fits` says what too long is (a width, lines,
// letters).
std::string shortened(std::string_view text, const std::function<bool(std::string_view)>& fits);

// Small and capital letters, in ASCII and the alphabets with one rule each for it (Latin with its
// accents, Greek, Cyrillic): names compare the same however they're capitalised.
char32_t lowerOf(char32_t c);
char32_t upperOf(char32_t c);
std::string lower(std::string_view s);
inline bool sameName(std::string_view a, std::string_view b) { return lower(a) == lower(b); } // (a name: case doesn't matter)
// `base`, or base_2, base_3... (`sep` between): the first that `taken` doesn't claim.
std::string uniqueName(const std::string& base, const std::function<bool(const std::string&)>& taken, std::string_view sep = "_");
// Every byte but letters, digits and `safe` as %XX (a file:// address, trash info).
std::string percentEscaped(std::string_view text, std::string_view safe = "/-_.~");
// A text (a game's title) as a file's or a folder's name on every system: without the characters Windows doesn't allow
// (/ \ : * ? " < > |) or control characters, no dots or spaces at its ends, and not a name Windows keeps for itself (con,
// nul, com1...: those get an _). Empty: nothing was left of it.
std::string fileNameOf(std::string_view text);
// The folder a game's players' data goes in (in VNext's own): its save_folder setting, else its title, as a folder can be called
// (fileNameOf), else VNext.
std::string saveFolderNameOf(std::string_view saveFolder, std::string_view title);
// An image name as scripts use it: "images/mina.png" -> mina.
std::string imageName(std::string_view file);
// The name a character's images start with, unless its image= says another: "Mr. Tanaka" -> mr_tanaka (mr_tanaka_happy).
std::string characterImageName(std::string_view name);
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

// Colour names: the built-in ones (white, black, red, pink, skyblue...) plus "colour" lines.
using Palette = std::map<std::string, SDL_Color, std::less<>>;
// "#rgb", "#rrggbb", "#rrggbbaa" or a colour name; a gradient ("navy-black": fading down to another) gives its top.
std::optional<SDL_Color> parseColour(std::string_view text, const Palette* palette = nullptr);
// "TOP-BOTTOM": a colour fading down to another (navy-black, #203040-#000000).
std::optional<std::pair<SDL_Color, SDL_Color>> parseGradient(std::string_view text, const Palette* palette = nullptr);
// What a colour's box says for a value: its name, a code's likeness (colourName), "navy down to black" for a gradient.
std::string colourWords(std::string_view value, const Palette* palette = nullptr);
// What to call a colour: its name (the game's own, or a built-in one) when it has one, else the
// built-in one it's most like ("like navy"); a see-through one says how much ("black, 50%").
std::string colourName(SDL_Color c, const Palette* palette = nullptr);
// A colour's hue (0 to 360), saturation and value (0 to 1); a grey's hue is 0.
std::array<float, 3> hsvOf(SDL_Color c);

// For as long as it lives, `place` holds what `standIn` held (the Screens page's sample story in the engine's
// place, its phone in the player's), and standIn what place held: swapped back when it goes, whatever happens.
template <class T>
class SwappedIn {
public:
    SwappedIn(T& place, T& standIn) : place_(place), standIn_(standIn) { std::swap(place_, standIn_); }
    ~SwappedIn() { std::swap(place_, standIn_); }
    SwappedIn(const SwappedIn&) = delete;
    SwappedIn& operator=(const SwappedIn&) = delete;
    SwappedIn(SwappedIn&&) = delete;
    SwappedIn& operator=(SwappedIn&&) = delete;

private:
    T& place_;
    T& standIn_;
};

// For as long as it lives, `place` is `value`; then it's what it was.
template <class T>
class SetFor {
public:
    SetFor(T& place, std::type_identity_t<T> value) : place_(place), was_(std::exchange(place, std::move(value))) {}
    ~SetFor() { place_ = std::move(was_); }
    SetFor(const SetFor&) = delete;
    SetFor& operator=(const SetFor&) = delete;
    SetFor(SetFor&&) = delete;
    SetFor& operator=(SetFor&&) = delete;

private:
    T& place_;
    T was_;
};

} // namespace vn
