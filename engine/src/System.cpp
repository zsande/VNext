#include "System.hpp"
#include "Script.hpp"

#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <ranges>
#include <span>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <variant>

namespace vn {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace utf8 {

std::size_t length(std::string_view s) {
    return static_cast<std::size_t>(std::ranges::count_if(s, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

std::size_t offset(std::string_view s, std::size_t codepoints) {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (seen == codepoints) return i;
            ++seen;
        }
    }
    return s.size();
}

} // namespace utf8

// A capital letter's small one, by code point, for the alphabets where that's one rule each: Latin
// with its accents (Á, Ő, Ÿ...), Greek and Cyrillic. Others stay as they are.
char32_t lowerOf(char32_t c) {
    if (c < 0x80) return static_cast<char32_t>(std::tolower(static_cast<int>(c)));
    if (c == 0x130) return U'i';                                                                 // İ
    if (c == 0x178) return 0xFF;                                                                 // Ÿ
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;                                    // À..Þ (not ×)
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return c | 1;                  // Ā..ķ, Ŋ..ŷ: the capital is the even one
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c % 2 ? c + 1 : c;      // Ĺ..ň, Ź..ž: the odd one
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 0x20;                                 // Α..Ω
    if (c >= 0x400 && c <= 0x40F) return c + 0x50;                                               // Ѐ..Џ
    if (c >= 0x410 && c <= 0x42F) return c + 0x20;                                               // А..Я
    if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || (c >= 0x4D0 && c <= 0x52F)) return c | 1;
    return c;
}

// The other way round (ß and the letters without a capital stay as they are).
char32_t upperOf(char32_t c) {
    if (c < 0x80) return static_cast<char32_t>(std::toupper(static_cast<int>(c)));
    if (c == 0xFF) return 0x178;                                                                 // ÿ
    if (c == 0x131) return U'I';                                                                 // ı
    if (c == 0x3C2) return 0x3A3;                                                                // ς (a final sigma)
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;                                    // à..þ (not ÷)
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return c % 2 ? c - 1 : c;      // (the small one is the odd one)
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c % 2 ? c : c - 1;      // (the even one)
    if (c >= 0x3B1 && c <= 0x3C9) return c - 0x20;                                               // α..ω
    if (c >= 0x450 && c <= 0x45F) return c - 0x50;                                               // ѐ..џ
    if (c >= 0x430 && c <= 0x44F) return c - 0x20;                                               // а..я
    if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || (c >= 0x4D0 && c <= 0x52F)) return c % 2 ? c - 1 : c;
    return c;
}

namespace {
// `s` with each letter changed by `f` (a code point to a code point), UTF-8 in and out.
std::string eachLetter(std::string_view s, char32_t (*f)(char32_t), bool firstOnly = false) {
    std::string out;
    out.reserve(s.size());
    bool done = false;
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        const std::size_t n = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 0; // (0: a stray byte, kept as it is)
        if (n == 0 || i + n > s.size() || done) {
            out += s[i];
            ++i;
            continue;
        }
        char32_t c = n == 1 ? b : b & (0x7Fu >> n);
        for (std::size_t k = 1; k < n; ++k) c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        const char32_t l = f(c);
        if (l == c) {
            out.append(s.substr(i, n));
        } else if (l < 0x80) {
            out += static_cast<char>(l);
        } else if (l < 0x800) {
            out += static_cast<char>(0xC0 | (l >> 6));
            out += static_cast<char>(0x80 | (l & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (l >> 12));
            out += static_cast<char>(0x80 | ((l >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (l & 0x3F));
        }
        i += n;
        done = firstOnly;
    }
    return out;
}
} // namespace

// `s` in small letters (ASCII, and the alphabets lowerOf() knows), so names compare the same
// however they're capitalized: Őrs is ŐRS and őrs.
std::string lower(std::string_view s) { return eachLetter(s, lowerOf); }
std::string upper(std::string_view s) { return eachLetter(s, upperOf); }
std::string upperFirst(std::string_view s) { return eachLetter(s, upperOf, true); }

bool startsWithCapital(std::string_view s) {
    const std::string_view head = s.substr(0, std::min<std::size_t>(s.size(), 4)); // (its first letter, however many bytes)
    return !head.empty() && eachLetter(head, lowerOf, true) != head;                // (a capital: made small, it changes)
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::optional<float> parseNumber(std::string_view s) {
    float divide = 1.0f;
    if (s.ends_with('%')) {
        s.remove_suffix(1);
        divide = 100.0f;
    } else if (s.ends_with('s')) {
        s.remove_suffix(1);
    }
    const auto v = parseDecimal(s);
    if (!v) return std::nullopt;
    return static_cast<float>(*v) / divide;
}

std::optional<double> parseDecimal(std::string_view s) {
    // (std::from_chars reads decimals only from macOS 26 on, and the player runs on 13.3: strtod reads them
    // the same, with a point, while the C locale is the "C" one; the player never changes it)
    if (s.empty()) return std::nullopt;
    const std::string copy(s);
    char* end = nullptr;
    const double v = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::string uniqueName(const std::string& base, const std::function<bool(const std::string&)>& taken, std::string_view sep) {
    std::string name = base;
    for (int n = 2; taken(name); ++n) name = std::format("{}{}{}", base, sep, n);
    return name;
}

std::string percentEscaped(std::string_view text, std::string_view safe) {
    std::string out;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || safe.contains(c)) out += c;
        else out += std::format("%{:02X}", static_cast<unsigned char>(c));
    }
    return out;
}

std::optional<bool> parseBool(std::string_view s) {
    const std::string v = lower(s);
    if (v == "on" || v == "yes" || v == "true" || v == "1") return true;
    if (v == "off" || v == "no" || v == "false" || v == "0") return false;
    return std::nullopt;
}

namespace {
// The built-in colours, by name.
const Palette& colorNames() {
    static const Palette names{
        {"white", {255, 255, 255, 255}},   {"black", {0, 0, 0, 255}},         {"gray", {128, 128, 128, 255}},
        {"grey", {128, 128, 128, 255}},    {"silver", {192, 192, 200, 255}},  {"red", {230, 60, 60, 255}},
        {"darkred", {140, 20, 30, 255}},   {"orange", {255, 150, 50, 255}},   {"gold", {255, 211, 110, 255}},
        {"yellow", {255, 230, 80, 255}},   {"lime", {180, 240, 120, 255}},    {"green", {70, 180, 90, 255}},
        {"darkgreen", {20, 90, 40, 255}},  {"teal", {40, 160, 160, 255}},     {"cyan", {90, 220, 230, 255}},
        {"skyblue", {143, 211, 255, 255}}, {"lightblue", {180, 210, 255, 255}}, {"blue", {70, 110, 240, 255}},
        {"navy", {20, 30, 90, 255}},       {"purple", {150, 80, 200, 255}},   {"violet", {184, 160, 255, 255}},
        {"magenta", {230, 80, 200, 255}},  {"pink", {255, 158, 207, 255}},    {"hotpink", {255, 90, 170, 255}},
        {"brown", {140, 90, 50, 255}},     {"beige", {235, 220, 190, 255}},   {"transparent", {0, 0, 0, 0}},
        {"none", {0, 0, 0, 0}},
    };
    return names;
}
} // namespace

std::optional<SDL_Color> parseColor(std::string_view text, const Palette* palette) {
    const Palette& names = colorNames();
    const std::string key = lower(trim(text));
    if (palette)
        if (auto it = palette->find(key); it != palette->end()) return it->second;
    if (auto it = names.find(key); it != names.end()) return it->second;
    if (const auto g = parseGradient(key, palette)) return g->first; // (a gradient: its top)
    if (!key.starts_with('#')) return std::nullopt;

    const std::string_view s = std::string_view(key).substr(1);
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    if (std::ranges::any_of(s, [&](char c) { return hex(c) < 0; })) return std::nullopt;
    auto byte = [&](std::size_t i) { return static_cast<Uint8>(hex(s[i]) * 16 + hex(s[i + 1])); };
    auto nib = [&](std::size_t i) { return static_cast<Uint8>(hex(s[i]) * 17); };
    switch (s.size()) {
    case 3: return SDL_Color{nib(0), nib(1), nib(2), 255};
    case 6: return SDL_Color{byte(0), byte(2), byte(4), 255};
    case 8: return SDL_Color{byte(0), byte(2), byte(4), byte(6)};
    default: return std::nullopt;
    }
}

std::optional<std::pair<SDL_Color, SDL_Color>> parseGradient(std::string_view text, const Palette* palette) {
    const std::string_view key = trim(text);
    const auto dash = key.find('-', 1);
    if (dash == std::string_view::npos) return std::nullopt;
    const auto top = parseColor(key.substr(0, dash), palette), bottom = parseColor(key.substr(dash + 1), palette);
    if (!top || !bottom) return std::nullopt;
    return std::pair{*top, *bottom};
}

std::string colorWords(std::string_view value, const Palette* palette) {
    auto one = [&](std::string_view v) {
        v = trim(v);
        const auto c = parseColor(v, palette);
        return c && v.starts_with('#') ? colorName(*c, palette) : std::string(v);
    };
    if (const auto dash = value.find('-', 1); dash != std::string_view::npos && parseGradient(value, palette))
        return one(value.substr(0, dash)) + " down to " + one(value.substr(dash + 1));
    return one(value);
}

std::array<float, 3> hsvOf(SDL_Color c) {
    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f, hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
    float h = 0.0f;
    if (d > 0.0f) h = hi == r ? std::fmod((g - b) / d, 6.0f) : hi == g ? (b - r) / d + 2.0f : (r - g) / d + 4.0f;
    if (h < 0.0f) h += 6.0f;
    return {h * 60.0f, hi > 0.0f ? d / hi : 0.0f, hi};
}

std::string colorName(SDL_Color c, const Palette* palette) {
    if (c.a == 0) return "transparent";
    auto same = [&](SDL_Color o) { return o.r == c.r && o.g == c.g && o.b == c.b; };
    const std::string amount = c.a == 255 ? std::string{} : std::format(", {}%", (c.a * 100 + 127) / 255);
    if (palette)
        for (const auto& [name, color] : *palette)
            if (same(color)) return name + (color.a == c.a ? "" : amount);
    // Hue, saturation and value: which built-in colour it's most like (a grey's hue means nothing).
    const auto [h, s, v] = hsvOf(c);
    std::string nearest;
    float best = std::numeric_limits<float>::max();
    for (const auto& [name, color] : colorNames()) {
        if (color.a == 0 || name == "grey") continue;
        if (same(color)) return name + amount;
        const auto [h2, s2, v2] = hsvOf(color);
        float dh = std::fabs(h - h2);
        if (dh > 180.0f) dh = 360.0f - dh;
        const float hue = dh / 180.0f * 3.0f * std::min(s, s2), ds = s - s2, dv = (v - v2) * 1.5f; // (the hue counts most)
        if (const float apart = hue * hue + ds * ds + dv * dv; apart < best) {
            best = apart;
            nearest = name;
        }
    }
    return "like " + nearest + amount;
}

// ---------------------------------------------------------------------------
// The game's files
// ---------------------------------------------------------------------------

namespace {

// --- The pack ------------------------------------------------------------------------------
// A header: "VNXPACK1", the number of files (4 bytes), 4 empty bytes, then where the list of files
// starts and how long it is (8 bytes each). Then the files, one after the other, and the list at
// the end: for each file its path (2 bytes saying how long, then the letters), where it starts,
// how long it is in the pack and how long it really is (8 bytes each), how it's packed (1 byte:
// 0 as it is, 1 compressed) and its check (4 bytes). Numbers are little-endian; the list and the
// files are scrambled. Files added later (the players' saves and progress, in saves/) go after the list,
// then a new list, and only then does the header name that one: what's after the list it names is left over.
constexpr std::string_view kMagic = "VNXPACK1";
constexpr std::uint64_t kHeaderSize = 32;
constexpr std::uint64_t kListSeed = 0x564e657874ull; // scrambles the list (a file: where it starts)
constexpr std::string_view kSavesIn = "saves/";      // where in a pack its players' saves and progress are
constexpr std::uint64_t kLeftOver = 4ull << 20;      // left over from replaced saves before they're tidied

struct Entry {
    std::uint64_t offset = 0, stored = 0, size = 0;
    std::uint8_t method = 0;
    std::uint32_t check = 0;
};


void put(std::string& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out += static_cast<char>(value >> (8 * i) & 0xFF);
}

std::uint64_t get(std::string_view in, std::size_t& at, int bytes) {
    std::uint64_t value = 0;
    for (int i = 0; i < bytes; ++i) value |= std::uint64_t{static_cast<unsigned char>(in[at + static_cast<std::size_t>(i)])} << (8 * i);
    at += static_cast<std::size_t>(bytes);
    return value;
}

// Scrambles bytes with a stream of numbers made from `seed` (and unscrambles them: it's the same).
void scramble(std::span<char> bytes, std::uint64_t seed) {
    std::uint64_t s = (seed + 1) * 0x9E3779B97F4A7C15ull;
    if (s == 0) s = 1;
    for (std::size_t i = 0; i < bytes.size(); i += 8) {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        std::uint64_t key = s * 0x2545F4914F6CDD1Dull;
        for (std::size_t j = i; j < std::min(i + 8, bytes.size()); ++j, key >>= 8)
            bytes[j] = static_cast<char>(static_cast<unsigned char>(bytes[j]) ^ (key & 0xFF));
    }
}

std::uint32_t checkOf(std::string_view data) {
    const std::uint64_t h = fnv1a(data);
    return static_cast<std::uint32_t>(h ^ (h >> 32));
}

// A file's line in the list.
void putEntry(std::string& list, std::string_view path, const Entry& e) {
    put(list, path.size(), 2);
    list += path;
    put(list, e.offset, 8);
    put(list, e.stored, 8);
    put(list, e.size, 8);
    put(list, e.method, 1);
    put(list, e.check, 4);
}

// The header, for `count` files and the (scrambled) list at `listAt`, `listSize` long.
std::string headerOf(std::uint64_t count, std::uint64_t listAt, std::uint64_t listSize) {
    std::string header(kMagic);
    put(header, count, 4);
    put(header, 0, 4);
    put(header, listAt, 8);
    put(header, listSize, 8);
    return header;
}

// A file cut to `size` bytes, where the system can (what's after goes).
void cutFile(const std::string& path, std::uint64_t size) {
    std::u8string name(path.size(), u8'\0');
    std::memcpy(name.data(), path.data(), path.size());
    std::error_code error;
    std::filesystem::resize_file(std::filesystem::path(name), size, error);
}

// Compression: LZ77, like LZ4's blocks. A step is a token byte (its high 4 bits: how many bytes
// follow as they are; its low 4 bits: how long the repeat after them is, less 4; 15 means more
// follows, in bytes of 255 and a last smaller one), those bytes, then how far back the repeat
// starts (2 bytes). The last step has no repeat.
void putLength(std::string& out, std::size_t n) {
    for (; n >= 255; n -= 255) out += '\xff';
    out += static_cast<char>(n);
}

std::string compress(std::string_view in) {
    std::string out;
    out.reserve(in.size() / 2 + 16);
    std::vector<std::int64_t> seen(std::size_t{1} << 16, -1); // where 4 bytes with each hash were last
    auto word = [&](std::size_t i) {
        std::uint32_t v = 0;
        std::memcpy(&v, in.data() + i, 4);
        return v;
    };
    std::size_t anchor = 0; // the first byte not written yet
    auto step = [&](std::size_t end, std::size_t distance, std::size_t length) {
        const std::size_t literals = end - anchor, extra = length > 0 ? length - 4 : 0;
        out += static_cast<char>(std::min<std::size_t>(literals, 15) << 4 | std::min<std::size_t>(extra, 15));
        if (literals >= 15) putLength(out, literals - 15);
        out.append(in.substr(anchor, literals));
        if (length == 0) return;
        out += static_cast<char>(distance & 0xFF);
        out += static_cast<char>(distance >> 8);
        if (extra >= 15) putLength(out, extra - 15);
    };
    std::size_t misses = 0;
    for (std::size_t i = 0; i + 4 <= in.size();) {
        const std::uint32_t v = word(i);
        const std::int64_t before = std::exchange(seen[(v * 2654435761u) >> 16], static_cast<std::int64_t>(i));
        if (before >= 0 && i - static_cast<std::size_t>(before) <= 65535 && word(static_cast<std::size_t>(before)) == v) {
            const auto from = static_cast<std::size_t>(before);
            std::size_t length = 4;
            while (i + length < in.size() && in[from + length] == in[i + length]) ++length;
            step(i, i - from, length);
            i += length;
            anchor = i;
            misses = 0;
        } else {
            i += 1 + (misses++ >> 6); // faster through bytes that don't repeat
        }
    }
    step(in.size(), 0, 0);
    return out;
}

std::optional<std::string> decompress(std::string_view in, std::uint64_t size) {
    if (size > in.size() * 256 + 64) return std::nullopt; // more than it could hold
    std::string out(static_cast<std::size_t>(size), '\0');
    std::size_t p = 0, o = 0;
    auto more = [&](std::size_t& n) { // the bytes after a 15
        unsigned char b = 0;
        do {
            if (p >= in.size()) return false;
            b = static_cast<unsigned char>(in[p++]);
            n += b;
        } while (b == 255);
        return true;
    };
    while (p < in.size()) {
        const auto token = static_cast<unsigned char>(in[p++]);
        std::size_t literals = token >> 4;
        if (literals == 15 && !more(literals)) return std::nullopt;
        if (literals > in.size() - p || literals > out.size() - o) return std::nullopt;
        std::memcpy(out.data() + o, in.data() + p, literals);
        p += literals;
        o += literals;
        if (p == in.size()) break; // the last step
        if (in.size() - p < 2) return std::nullopt;
        const std::size_t low = static_cast<unsigned char>(in[p++]);
        const std::size_t distance = low | std::size_t{static_cast<unsigned char>(in[p++])} << 8;
        std::size_t length = (token & 15u) + 4;
        if ((token & 15) == 15 && !more(length)) return std::nullopt;
        if (distance == 0 || distance > o || length > out.size() - o) return std::nullopt;
        for (const std::size_t end = o + length; o < end; ++o) out[o] = out[o - distance];
    }
    if (o != out.size()) return std::nullopt;
    return out;
}

// Files that are compressed already (pictures, sound, video) are packed as they are.
bool compressible(std::string_view path) {
    const auto dot = path.find_last_of('.');
    const std::string ext = dot == std::string_view::npos ? std::string() : lower(path.substr(dot));
    static constexpr std::string_view kPacked[]{".png", ".jpg", ".jpeg", ".gif", ".qoi", ".ogg", ".mp3", ".flac"};
    return !std::ranges::contains(kPacked, ext) && !std::ranges::contains(kVideoTypes, ext);
}

// Every file in folder + sub, by its path from folder, but saves/, packs and hidden files.
void collect(const std::string& folder, const std::string& sub, std::vector<std::string>& out) {
    std::vector<std::string> names;
    SDL_EnumerateDirectory(
        (folder + sub).c_str(),
        [](void* user, const char*, const char* name) -> SDL_EnumerationResult {
            return withoutThrowing(SDL_ENUM_FAILURE, [&] {
                static_cast<std::vector<std::string>*>(user)->emplace_back(name);
                return SDL_ENUM_CONTINUE;
            });
        },
        &names);
    for (const std::string& name : names) {
        if (name.starts_with('.') || (sub.empty() && lower(name) == "saves") || lower(name).ends_with(".vnext")) continue;
        const std::string path = sub + name;
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo((folder + path).c_str(), &info)) continue;
        if (info.type == SDL_PATHTYPE_DIRECTORY) collect(folder, path + "/", out);
        else if (info.type == SDL_PATHTYPE_FILE) out.push_back(path);
    }
}

} // namespace

struct GameFiles::Pack {
    std::string file; // its path
    std::string root; // file + "/": the paths of the files in it start with this
    std::unordered_map<std::string, Entry, StringHash, std::equal_to<>> entries; // by lower-case path
    std::vector<std::string> paths;                                              // as written, in order
    std::uint64_t listAt = 0, listSize = 0;                                      // where the list the header names is

    // The path of a file in it, as its list has it (lower case), if it's under its root.
    [[nodiscard]] std::optional<std::string> keyOf(std::string_view path) const {
        if (!path.starts_with(root)) return std::nullopt;
        std::string key = lower(path.substr(root.size()));
        std::ranges::replace(key, '\\', '/');
        return key;
    }
    [[nodiscard]] std::optional<std::string> read(const std::string& key) const {
        const auto it = entries.find(key);
        if (it == entries.end()) {
            SDL_SetError("%s isn't in %s", key.c_str(), file.c_str());
            return std::nullopt;
        }
        const Entry& e = it->second;
        std::string stored(static_cast<std::size_t>(e.stored), '\0');
        IOStreamPtr io{SDL_IOFromFile(file.c_str(), "rb")};
        if (!io) return std::nullopt;
        const bool done = SDL_SeekIO(io.get(), static_cast<Sint64>(e.offset), SDL_IO_SEEK_SET) >= 0 &&
                          SDL_ReadIO(io.get(), stored.data(), stored.size()) == stored.size();
        io.reset();
        if (done) {
            scramble(stored, e.offset);
            auto data = e.method == 1 ? decompress(stored, e.size) : std::optional(std::move(stored));
            if (data && data->size() == e.size && checkOf(*data) == e.check) return data;
        }
        logError("'{}' is damaged: '{}' can't be read from it.", file, key);
        SDL_SetError("%s is damaged", file.c_str());
        return std::nullopt;
    }

    // Files added, replaced (data given) or taken out (none): written from `at` (after the list the
    // header names, when none: what's there is left over), then the new list, and only then the header
    // naming it, so stopping halfway leaves the pack as it was.
    bool change(const std::vector<std::pair<std::string, std::optional<std::string>>>& changes, std::optional<std::uint64_t> at = std::nullopt) {
        auto now = entries;
        auto order = paths;
        const std::uint64_t from = at.value_or(listAt + listSize);
        std::uint64_t end = from;
        std::string written;
        for (const auto& [path, data] : changes) {
            std::string key = lower(path);
            if (!data) {
                if (now.erase(key) > 0) std::erase_if(order, [&](const std::string& p) { return lower(p) == key; });
                continue;
            }
            std::string stored = *data;
            scramble(stored, end);
            if (!now.contains(key)) order.push_back(path);
            now.insert_or_assign(std::move(key), Entry{.offset = end, .stored = stored.size(), .size = data->size(), .method = 0, .check = checkOf(*data)});
            end += stored.size();
            written += stored;
        }
        std::string list;
        for (const std::string& p : order) putEntry(list, p, now.find(lower(p))->second);
        scramble(list, kListSeed);
        const std::string header = headerOf(order.size(), end, list.size());
        IOStreamPtr io{SDL_IOFromFile(file.c_str(), "r+b")};
        const bool ok = io && SDL_SeekIO(io.get(), static_cast<Sint64>(from), SDL_IO_SEEK_SET) >= 0 &&
                        SDL_WriteIO(io.get(), written.data(), written.size()) == written.size() &&
                        SDL_WriteIO(io.get(), list.data(), list.size()) == list.size() && SDL_FlushIO(io.get()) &&
                        SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) == 0 && SDL_WriteIO(io.get(), header.data(), header.size()) == header.size();
        if (!io || !SDL_CloseIO(io.release()) || !ok) return false; // (closed here: what it says about the last writes counts)
        entries = std::move(now);
        paths = std::move(order);
        listAt = end;
        listSize = list.size();
        return true;
    }

    // Once what replaced saves left over is more than the saves themselves (and kLeftOver), they're
    // written again straight after its own files: first after the list, as any change, then from where
    // its own files end, which is all left over by then; and the file is cut after that.
    void tidy() {
        std::uint64_t own = kHeaderSize, saved = 0; // (where its own files end; what the saves take)
        std::vector<std::pair<std::string, std::optional<std::string>>> saves;
        for (const std::string& p : paths) {
            const Entry& e = entries.find(lower(p))->second;
            if (lower(p).starts_with(kSavesIn)) {
                saved += e.stored;
                saves.emplace_back(p, std::nullopt);
            } else {
                own = std::max(own, e.offset + e.stored);
            }
        }
        if (listAt < own + saved || listAt - own - saved <= std::max(kLeftOver, saved)) return;
        for (auto& [path, data] : saves)
            if (!(data = read(lower(path)))) return; // (a damaged save stays where it is)
        if (!change(saves)) return;
        if (own + saved + listSize > listAt - saved) return; // (the second copy would reach the first: kept as it is)
        if (change(saves, own)) cutFile(file, listAt + listSize);
    }
};

GameFiles::GameFiles() = default;
GameFiles::~GameFiles() = default;

const GameFiles& GameFiles::disk() {
    static const GameFiles none;
    return none;
}

bool GameFiles::exists(const std::string& path) const {
    if (const auto key = pack_ ? pack_->keyOf(path) : std::nullopt) return pack_->entries.contains(*key);
    return fileExists(path);
}

std::optional<std::string> GameFiles::read(const std::string& path) const {
    if (const auto key = pack_ ? pack_->keyOf(path) : std::nullopt) return pack_->read(*key);
    return readFile(path);
}

std::optional<std::string> GameFiles::readText(const std::string& path) const {
    auto text = read(path);
    if (text && text->starts_with("\xEF\xBB\xBF")) text->erase(0, 3); // UTF-8 BOM
    return text;
}

IOStreamPtr GameFiles::open(const std::string& path) const {
    const auto key = pack_ ? pack_->keyOf(path) : std::nullopt;
    if (!key) return openFile(path);
    const auto data = pack_->read(*key);
    if (!data) return nullptr;
    SdlOwned<> memory{SDL_malloc(std::max<std::size_t>(data->size(), 1))};
    if (!memory) return nullptr;
    std::memcpy(memory.get(), data->data(), data->size());
    IOStreamPtr io{SDL_IOFromConstMem(memory.get(), data->size())};
    if (!io || !SDL_SetPointerProperty(SDL_GetIOProperties(io.get()), SDL_PROP_IOSTREAM_MEMORY_FREE_FUNC_POINTER, reinterpret_cast<void*>(&SDL_free)))
        return nullptr; // (the stream closed, then `memory` frees the bytes)
    static_cast<void>(memory.release()); // (the stream frees it once it's closed)
    return io;
}

std::vector<std::string> GameFiles::list(const std::string& folder, std::string_view extension) const {
    const auto key = pack_ ? pack_->keyOf(withSlash(folder)) : std::nullopt;
    if (!key) return listFiles(folder, extension);
    std::vector<std::string> names;
    const std::string ext = lower(extension);
    for (const std::string& path : pack_->paths)
        if (const std::string low = lower(path); low.starts_with(*key) && low.ends_with(ext) && low.find('/', key->size()) == std::string::npos)
            names.push_back(path.substr(key->size()));
    std::ranges::sort(names);
    return names;
}

bool GameFiles::writable() const {
    return pack_ && IOStreamPtr{SDL_IOFromFile(pack_->file.c_str(), "r+b")} != nullptr;
}

bool GameFiles::write(const std::string& path, std::string_view data) {
    const auto key = pack_ ? pack_->keyOf(path) : std::nullopt;
    if (!key || !pack_->change({{*key, std::string(data)}})) return false;
    pack_->tidy();
    return true;
}

bool GameFiles::remove(const std::string& path) {
    const auto key = pack_ ? pack_->keyOf(path) : std::nullopt;
    if (!key || !pack_->entries.contains(*key) || !pack_->change({{*key, std::nullopt}})) return false;
    pack_->tidy();
    return true;
}

std::expected<void, std::string> GameFiles::openPack(const std::string& file) {
    const std::string name = file.substr(file.find_last_of("/\\") + 1);
    const auto damaged = std::unexpected(std::format("'{}' is damaged, or isn't a VNext pack: make it again in developer mode.", name));
    IOStreamPtr io{SDL_IOFromFile(file.c_str(), "rb")};
    if (!io) return std::unexpected(std::format("Can't open '{}': {}.", file, SDL_GetError()));
    std::string header(kHeaderSize, '\0'), list;
    bool ok = SDL_ReadIO(io.get(), header.data(), header.size()) == header.size() && header.starts_with(kMagic);
    std::size_t at = kMagic.size();
    std::uint64_t count = 0, listAt = 0, listSize = 0;
    if (ok) {
        count = get(header, at, 4);
        at += 4;
        listAt = get(header, at, 8);
        listSize = get(header, at, 8);
        const Sint64 size = SDL_GetIOSize(io.get());
        ok = size >= 0 && listAt >= kHeaderSize && listSize < (std::uint64_t{1} << 30) &&
             listAt + listSize <= static_cast<std::uint64_t>(size); // (what's after it: left over from a change that stopped halfway)
        if (ok) {
            list.resize(static_cast<std::size_t>(listSize));
            ok = SDL_SeekIO(io.get(), static_cast<Sint64>(listAt), SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(io.get(), list.data(), list.size()) == list.size();
        }
    }
    io.reset();
    if (!ok) return damaged;
    scramble(list, kListSeed);
    auto opened = std::make_unique<Pack>(Pack{.file = file, .root = file + "/", .entries = {}, .paths = {}, .listAt = listAt, .listSize = listSize});
    at = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (list.size() - at < 2) return damaged;
        const auto length = static_cast<std::size_t>(get(list, at, 2));
        if (list.size() - at < length + 29) return damaged;
        std::string path = list.substr(at, length);
        at += length;
        Entry e;
        e.offset = get(list, at, 8);
        e.stored = get(list, at, 8);
        e.size = get(list, at, 8);
        e.method = static_cast<std::uint8_t>(get(list, at, 1));
        e.check = static_cast<std::uint32_t>(get(list, at, 4));
        if (e.offset < kHeaderSize || e.stored > listAt || e.offset > listAt - e.stored || e.method > 1 ||
            (e.method == 0 && e.stored != e.size))
            return damaged;
        opened->entries.insert_or_assign(lower(path), e);
        opened->paths.push_back(std::move(path));
    }
    if (at != list.size()) return damaged;
    pack_ = std::move(opened);
    return {};
}

bool fileExists(const std::string& path) {
    SDL_PathInfo info{};
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::string withSlash(std::string path) {
    if (!path.empty() && !path.ends_with('/') && !path.ends_with('\\')) path += '/';
    return path;
}

bool isDirectory(const std::string& path) {
    SDL_PathInfo info{};
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

std::optional<std::string> readFile(const std::string& path) {
    std::size_t size = 0;
    const SdlOwned<> data{SDL_LoadFile(path.c_str(), &size)};
    if (!data) return std::nullopt;
    return std::string(static_cast<const char*>(data.get()), size);
}

std::optional<std::string> readTextFile(const std::string& path) {
    auto text = readFile(path);
    if (text && text->starts_with("\xEF\xBB\xBF")) text->erase(0, 3); // UTF-8 BOM
    return text;
}

IOStreamPtr openFile(const std::string& path) {
    return IOStreamPtr{SDL_IOFromFile(path.c_str(), "rb")};
}

std::string scriptName(std::string_view stem) {
    std::string out;
    for (const char c : stem) {
        const bool word = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || static_cast<unsigned char>(c) >= 0x80;
        if (word) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (out.ends_with('_') && out.size() > 1 && !stem.ends_with('_')) out.pop_back();
    return out;
}

std::vector<std::string> listFiles(const std::string& folder, std::string_view extension) {
    std::vector<std::string> names;
    const std::string ext = lower(extension);
    int count = 0;
    if (const SdlOwned<char*> found{SDL_GlobDirectory(folder.c_str(), ("*" + ext).c_str(), SDL_GLOB_CASEINSENSITIVE, &count)})
        for (int i = 0; i < count; ++i) names.emplace_back(found.get()[i]);
    std::ranges::sort(names);
    return names;
}

std::expected<PackReport, std::string> writePack(const std::string& folder, const std::string& file) {
    std::vector<std::string> paths;
    collect(folder, "", paths);
    std::ranges::sort(paths);
    if (std::ranges::none_of(paths, [](const std::string& p) { return lower(p) == "scripts/main.vnscript"; }))
        return std::unexpected(std::format("'{}' has no scripts/main.vnscript, so it isn't a game folder.", folder));
    const std::string part = file + ".part";
    ScopeExit unfinished([&part] { SDL_RemovePath(part.c_str()); }); // (the part written so far goes, unless the pack is finished: closed before that, as `out` goes first)
    IOStreamPtr out{SDL_IOFromFile(part.c_str(), "wb")};
    if (!out) return std::unexpected(std::format("Can't write '{}': {}.", file, SDL_GetError()));
    auto fail = [](std::string message) { return std::unexpected(std::move(message)); };
    const std::string blank(kHeaderSize, '\0'); // the header goes here at the end
    if (SDL_WriteIO(out.get(), blank.data(), blank.size()) != blank.size()) return fail(std::format("Can't write '{}': {}.", file, SDL_GetError()));
    PackReport report;
    std::string list;
    std::uint64_t at = kHeaderSize;
    for (const std::string& path : paths) {
        auto data = readFile(folder + path);
        if (!data) return fail(std::format("Can't read '{}': {}.", path, SDL_GetError()));
        if (path.size() > 0xFFFF) return fail(std::format("Can't pack '{}': its path is too long.", path));
        Entry e{.offset = at, .stored = 0, .size = data->size(), .method = 0, .check = checkOf(*data)};
        if (compressible(path))
            if (std::string small = compress(*data); small.size() < data->size() - data->size() / 16) {
                *data = std::move(small);
                e.method = 1;
            }
        e.stored = data->size();
        scramble(*data, at);
        if (SDL_WriteIO(out.get(), data->data(), data->size()) != data->size()) return fail(std::format("Can't write '{}': {}.", file, SDL_GetError()));
        putEntry(list, path, e);
        at += e.stored;
        ++report.files;
        report.bytes += e.size;
    }
    scramble(list, kListSeed);
    const std::string header = headerOf(paths.size(), at, list.size());
    if (SDL_WriteIO(out.get(), list.data(), list.size()) != list.size() || SDL_SeekIO(out.get(), 0, SDL_IO_SEEK_SET) != 0 ||
        SDL_WriteIO(out.get(), header.data(), header.size()) != header.size())
        return fail(std::format("Can't write '{}': {}.", file, SDL_GetError()));
    if (!SDL_CloseIO(out.release()) || !SDL_RenamePath(part.c_str(), file.c_str())) // (closed here: what it says about the last writes counts)
        return fail(std::format("Can't write '{}': {}.", file, SDL_GetError()));
    unfinished.dismiss();
    report.packed = at + list.size();
    return report;
}

// ---------------------------------------------------------------------------
// Saves
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// SaveStore
// ---------------------------------------------------------------------------

void SaveStore::setFolder(std::string folder, GameFiles* pack) {
    folder_ = withSlash(std::move(folder));
    pack_ = pack;
    if (!pack_) SDL_CreateDirectory(folder_.c_str());
}

void SaveStore::configure(int pages, int slotsPerPage) {
    pages_ = {"auto", "quick"};
    for (int p = 1; p <= pages; ++p) pages_.push_back(std::to_string(p));
    slots_ = slotsPerPage;
}

namespace {

constexpr std::string_view kThumbnail = "thumbnail png "; // then its size in bytes, a newline and the picture

std::string pngBytes(SDL_Surface* surface) {
    std::string out;
    const IOStreamPtr io{surface ? SDL_IOFromDynamicMem() : nullptr};
    if (!io) return out;
    if (IMG_SavePNG_IO(surface, io.get(), false)) {
        const auto* bytes = static_cast<const char*>(SDL_GetPointerProperty(SDL_GetIOProperties(io.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
        const Sint64 size = SDL_GetIOSize(io.get());
        if (bytes && size > 0) out.assign(bytes, static_cast<std::size_t>(size));
    }
    return out;
}

} // namespace

std::optional<SaveStore::File> SaveStore::load(std::string_view slot) const {
    const auto data = pack_ ? pack_->readText(path(slot)) : readTextFile(path(slot));
    if (!data) return std::nullopt;
    // The text ends at the thumbnail line (no line of the state starts with "thumbnail").
    File file;
    const std::size_t at = data->find(std::format("\n{}", kThumbnail));
    file.text = data->substr(0, at == std::string::npos ? std::string::npos : at + 1);
    if (at != std::string::npos) {
        const std::size_t number = at + 1 + kThumbnail.size(), end = data->find('\n', number);
        std::size_t bytes = 0;
        if (end != std::string::npos && std::from_chars(data->data() + number, data->data() + end, bytes).ec == std::errc{} &&
            data->size() - end - 1 == bytes)
            file.png = data->substr(end + 1);
    }
    return file;
}

std::optional<std::string> SaveStore::read(std::string_view slot) const {
    auto file = load(slot);
    return file ? std::optional(std::move(file->text)) : std::nullopt;
}

std::string SaveStore::thumbnail(std::string_view slot) const {
    auto file = load(slot);
    return file ? std::move(file->png) : std::string();
}

SaveInfo SaveStore::info(std::string_view slot) const {
    SaveInfo info;
    SDL_PathInfo file{}; // (none for a pack's: its saves say when they were made)
    if (pack_ ? !pack_->exists(path(slot)) : !SDL_GetPathInfo(path(slot).c_str(), &file) || file.type != SDL_PATHTYPE_FILE) return info;
    const auto save = load(slot);
    if (!save) return info;
    info.exists = true;
    info.time = file.modify_time; // used when the save has no metadata
    {
        std::istringstream in(save->text);
        std::string line;
        while (std::getline(in, line)) {
            if (line.starts_with("meta time ")) std::from_chars(line.data() + 10, line.data() + line.size(), info.time);
            else if (line.starts_with("meta summary ")) info.summary = line.substr(13);
        }
    }
    return info;
}

bool SaveStore::write(std::string_view slot, std::string_view state, std::string_view summary, SDL_Surface* thumbnail) {
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    std::string flat(summary);
    std::ranges::replace(flat, '\n', ' ');
    std::string data = std::format("{}meta time {}\nmeta summary {}\n", state, now, flat);
    if (const std::string png = pngBytes(thumbnail); !png.empty()) data += std::format("{}{}\n{}", kThumbnail, png.size(), png);
    if (pack_) return pack_->write(path(slot), data); // (the pack keeps the old one whole until the new one is in)
    // Written next to it first, so stopping halfway (a crash, a full disk) leaves the old save whole.
    const std::string file = path(slot), part = file + ".part";
    if (!SDL_SaveFile(part.c_str(), data.data(), data.size()) || !SDL_RenamePath(part.c_str(), file.c_str())) {
        SDL_RemovePath(part.c_str());
        return false;
    }
    return true;
}

void SaveStore::remove(std::string_view slot) {
    if (pack_) pack_->remove(path(slot));
    else SDL_RemovePath(path(slot).c_str());
}

std::string SaveStore::rotatingSlot(std::string_view page) const {
    std::string oldest;
    SDL_Time oldestTime = 0;
    for (int i = 0; i < slots_; ++i) {
        std::string name = slotName(page, i);
        const SaveInfo info = this->info(name);
        if (!info.exists) return name;
        if (oldest.empty() || info.time < oldestTime) {
            oldest = std::move(name);
            oldestTime = info.time;
        }
    }
    return oldest;
}

std::optional<std::string> SaveStore::newest(std::string_view page) const {
    std::optional<std::string> best;
    SDL_Time bestTime = 0;
    for (const auto& p : pages_) {
        if (!page.empty() && p != page) continue;
        for (int i = 0; i < slots_; ++i) {
            std::string name = slotName(p, i);
            const SaveInfo info = this->info(name);
            if (info.exists && (!best || info.time > bestTime)) {
                best = std::move(name);
                bestTime = info.time;
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// SlotBrowser
// ---------------------------------------------------------------------------

void SlotBrowser::open(Mode mode) {
    mode_ = mode;
    pages_.clear();
    for (const auto& p : store_->pages())
        if (mode == Mode::Load || (p != "auto" && p != "quick")) pages_.push_back(p);
    // Saving reopens the last page used; loading opens on the page with the newest save.
    std::string want = last_[mode == Mode::Save ? 0 : 1];
    if (mode == Mode::Load)
        if (const auto newest = store_->newest()) want = newest->substr(0, newest->rfind('-'));
    const auto it = std::ranges::find(pages_, want);
    selected.reset();
    setPage(it != pages_.end() ? static_cast<std::size_t>(it - pages_.begin()) : 0);
}

void SlotBrowser::setPage(std::size_t page) {
    if (pages_.empty()) return;
    page_ = std::min(page, pages_.size() - 1);
    last_[mode_ == Mode::Save ? 0 : 1] = pages_[page_];
    cards_.clear();
    cards_.resize(static_cast<std::size_t>(store_->slotsPerPage()));
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        cards_[i].slot = SaveStore::slotName(pages_[page_], static_cast<int>(i));
        refresh(i);
    }
}

void SlotBrowser::turn(int delta) {
    if (pages_.empty()) return;
    const auto n = static_cast<int>(pages_.size());
    setPage(static_cast<std::size_t>((static_cast<int>(page_) + delta + n) % n));
}

void SlotBrowser::refresh(std::size_t index) {
    if (index >= cards_.size()) return;
    Card& card = cards_[index];
    card.info = store_->info(card.slot);
    card.thumbnail.reset();
    if (!card.info.exists) return;
    const std::string png = store_->thumbnail(card.slot);
    if (!png.empty()) card.thumbnail.reset(IMG_LoadTexture_IO(renderer_, SDL_IOFromConstMem(png.data(), png.size()), true));
    if (card.thumbnail) SDL_SetTextureScaleMode(card.thumbnail.get(), SDL_SCALEMODE_LINEAR);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

namespace {

int percent(float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 100.0f)); }

// The settings kept in settings.txt and set by "config default.NAME" (by the same names), and
// the numbers they can be.
struct Field {
    std::string_view name;
    std::variant<int Settings::*, bool Settings::*> field;
    int lo = 0, hi = 1;
};
constexpr Field kFields[]{
    {"fullscreen", &Settings::fullscreen},
    {"window_width", &Settings::windowWidth, 320, 100000},
    {"window_height", &Settings::windowHeight, 240, 100000},
    {"scaling", &Settings::scaling, 0, 3},
    {"vsync", &Settings::vsync},
    {"frame_limit", &Settings::frameLimit, 0, 1000},
    {"master_volume", &Settings::masterVolume, 0, 100},
    {"music_volume", &Settings::musicVolume, 0, 100},
    {"sound_volume", &Settings::soundVolume, 0, 100},
    {"voice_volume", &Settings::voiceVolume, 0, 100},
    {"babble", &Settings::babble},
    {"typing_sounds", &Settings::typingSounds},
    {"mouths", &Settings::mouths, 0, 2},
    {"text_speed", &Settings::textSpeed, 0, 1000},
    {"auto_delay", &Settings::autoDelay, 100, 20000},
    {"textbox_opacity", &Settings::textboxOpacity, 0, 100},
    {"text_size", &Settings::textSize, 50, 200},
    {"transitions", &Settings::transitions},
    {"skip_unread", &Settings::skipUnread},
    {"skip_after_choices", &Settings::skipAfterChoices},
    {"rollback", &Settings::rollback},
    {"skip_movies", &Settings::skipMovies},
    {"read_aloud", &Settings::readAloud},
    {"high_contrast", &Settings::highContrast},
};

} // namespace

std::optional<int> Settings::get(std::string_view key) const {
    const auto f = std::ranges::find(kFields, key, &Field::name);
    if (f == std::end(kFields)) return std::nullopt;
    return std::visit([&](auto field) { return static_cast<int>(this->*field); }, f->field);
}

bool Settings::set(std::string_view key, int v) {
    const auto f = std::ranges::find(kFields, key, &Field::name);
    if (f == std::end(kFields)) return false;
    std::visit([&](auto field) { this->*field = static_cast<std::remove_reference_t<decltype(this->*field)>>(std::clamp(v, f->lo, f->hi)); },
               f->field);
    return true;
}

Settings Settings::load(const std::string& path, const Settings& defaults) {
    Settings s = defaults;
    const auto text = readTextFile(path);
    if (!text) return s;
    std::istringstream in(*text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (line.starts_with("language ")) {
            s.language = trim(std::string_view(line).substr(9));
            continue;
        }
        if (line.starts_with("keys ")) { // keys NAME key1, key2
            std::string name, list;
            ls >> key >> name;
            std::getline(ls >> std::ws, list);
            auto& keys = s.keys[name];
            for (auto&& part : std::views::split(std::string_view(list), ','))
                if (const auto k = trim(std::string_view(part.begin(), part.end())); !k.empty()) keys.emplace_back(k);
            continue;
        }
        if (int v = 0; ls >> key >> v) s.set(key, v);
    }
    return s;
}

bool Settings::save(const std::string& path) const {
    std::string text;
    for (const Field& f : kFields)
        std::visit([&](auto field) { text += std::format("{} {}\n", f.name, static_cast<int>(this->*field)); }, f.field);
    if (!language.empty()) text += std::format("language {}\n", language);
    for (const auto& [name, list] : keys) {
        text += std::format("keys {} ", name);
        for (std::size_t i = 0; i < list.size(); ++i) text += (i ? ", " : "") + list[i];
        text += '\n';
    }
    return SDL_SaveFile(path.c_str(), text.data(), text.size());
}

ReadLog readLogOf(std::string_view saved) {
    ReadLog log;
    for (auto&& part : std::views::split(saved, '\n')) {
        const std::string_view line(part.begin(), part.end());
        std::uint64_t h = 0;
        if (std::from_chars(line.data(), line.data() + line.size(), h, 16).ec == std::errc{}) log.insert(h);
    }
    return log;
}

std::string readLogText(const ReadLog& log) {
    std::string out;
    out.reserve(log.size() * 17);
    for (auto h : log) out += std::format("{:016x}\n", h);
    return out;
}

const SettingControl* findSettingControl(std::string_view name) {
    auto volume = [](int Settings::*field, bool test) {
        return SettingControl{.value = [=](const Settings& s) { return static_cast<float>(s.*field) / 100.0f; },
                              .setValue = [=](Settings& s, float v) { s.*field = percent(v); },
                              .text = [=](const Settings& s, const GameConfig&) { return std::format("{}%", s.*field); },
                              .testSound = test};
    };
    static const std::map<std::string, SettingControl, std::less<>> controls{
        {"text_speed",
         // 10..150 characters per second in steps of 5; the far right end is "instant"
         {.value = [](const Settings& s) { return s.textSpeed == 0 ? 1.0f : static_cast<float>(s.textSpeed - 10) / 140.0f * 0.95f; },
          .setValue = [](Settings& s, float v) { s.textSpeed = v >= 0.975f ? 0 : 10 + 5 * static_cast<int>(std::lround(std::min(v / 0.95f, 1.0f) * 28.0f)); },
          .text = [](const Settings& s, const GameConfig&) { return std::to_string(s.textSpeed); }}}, // (0: instant)
        {"auto_delay",
         {.value = [](const Settings& s) { return static_cast<float>(s.autoDelay - 300) / 4700.0f; },
          .setValue = [](Settings& s, float v) { s.autoDelay = 300 + 100 * static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 47.0f)); },
          .text = [](const Settings& s, const GameConfig&) { return std::format("{}.{}", s.autoDelay / 1000, s.autoDelay % 1000 / 100); }}},
        {"textbox_opacity", volume(&Settings::textboxOpacity, false)},
        {"text_size",
         {.value = [](const Settings& s) { return static_cast<float>(s.textSize - 75) / 75.0f; }, // 75% to 150% in steps of 5
          .setValue = [](Settings& s, float v) { s.textSize = 75 + 5 * static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 15.0f)); },
          .text = [](const Settings& s, const GameConfig&) { return std::format("{}%", s.textSize); }}},
        {"master_volume", volume(&Settings::masterVolume, true)},
        {"music_volume", volume(&Settings::musicVolume, false)},
        {"sound_volume", volume(&Settings::soundVolume, true)},
        {"voice_volume", volume(&Settings::voiceVolume, false)},
    };
    auto it = controls.find(name);
    return it != controls.end() ? &it->second : nullptr;
}

} // namespace vn

// ---------------------------------------------------------------------------
// The Trash (after the rest: the system's headers come in here)
// ---------------------------------------------------------------------------

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <objc/message.h>
#include <objc/runtime.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX // (MinGW's C++ library has it already)
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace vn {

std::optional<std::string> moveToTrash(const std::string& path) {
#if defined(__APPLE__) // NSFileManager's trashItemAtURL (through the Objective-C runtime, from C++)
    const Owned<CFURLRef, CFRelease> url{CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false)};
    if (!url) return std::nullopt;
    const auto manager = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(objc_getClass("NSFileManager"), sel_registerName("defaultManager"));
    id there = nullptr; // (an NSURL, which is a CFURL)
    const bool ok = manager && reinterpret_cast<BOOL (*)(id, SEL, id, id*, id*)>(objc_msgSend)(
                                   manager, sel_registerName("trashItemAtURL:resultingItemURL:error:"), reinterpret_cast<id>(const_cast<__CFURL*>(url.get())), &there, nullptr);
    if (!ok) return std::nullopt;
    char where[4096]{};
    if (there && CFURLGetFileSystemRepresentation(reinterpret_cast<CFURLRef>(there), true, reinterpret_cast<UInt8*>(where), sizeof(where))) return std::string(where);
    return std::string{};
#elif defined(_WIN32) // the Recycle Bin: a delete that can be undone (where it is there, the player can't tell)
    std::string native = path;
    std::ranges::replace(native, '/', '\\');
    const int n = MultiByteToWideChar(CP_UTF8, 0, native.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::nullopt;
    std::wstring wide(static_cast<std::size_t>(n) + 1, L'\0'); // (ends with two zeros)
    MultiByteToWideChar(CP_UTF8, 0, native.c_str(), -1, wide.data(), n);
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = wide.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) return std::string{};
    return std::nullopt;
#else // the freedesktop.org Trash: the file in Trash/files, and where it was (and when) in Trash/info
    const char* data = SDL_getenv("XDG_DATA_HOME");
    const char* home = SDL_getenv("HOME");
    const std::string trash = data && *data ? std::string(data) + "/Trash" : home && *home ? std::string(home) + "/.local/share/Trash" : std::string{};
    if (trash.empty() || !SDL_CreateDirectory((trash + "/files").c_str()) || !SDL_CreateDirectory((trash + "/info").c_str())) return std::nullopt;
    std::string full = path;
    if (!full.starts_with('/'))
        if (const SdlOwned<char> here{SDL_GetCurrentDirectory()}) full = here.get() + full;
    const std::string base = full.substr(full.find_last_of('/') + 1);
    const std::string name = uniqueName(base, [&](const std::string& n) {
        return SDL_GetPathInfo((trash + "/files/" + n).c_str(), nullptr) || SDL_GetPathInfo((trash + "/info/" + n + ".trashinfo").c_str(), nullptr);
    }, ".");
    const std::string escaped = percentEscaped(full);
    SDL_DateTime t{};
    SDL_Time now = 0;
    if (SDL_GetCurrentTime(&now)) SDL_TimeToDateTime(now, &t, true);
    const std::string info = std::format("[Trash Info]\nPath={}\nDeletionDate={:04}-{:02}-{:02}T{:02}:{:02}:{:02}\n", escaped, t.year, t.month, t.day, t.hour, t.minute, t.second);
    const std::string infoPath = trash + "/info/" + name + ".trashinfo", to = trash + "/files/" + name;
    if (!SDL_SaveFile(infoPath.c_str(), info.data(), info.size())) return std::nullopt;
    if (SDL_RenamePath(full.c_str(), to.c_str()) || (SDL_CopyFile(full.c_str(), to.c_str()) && SDL_RemovePath(full.c_str()))) return to;
    SDL_RemovePath(infoPath.c_str());
    return std::nullopt;
#endif
}

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__linux__) // (Apple.mm, Windows.cpp and Linux.cpp have their own: here, none)
bool spelledRight(std::string_view, std::string_view) { return true; }
std::vector<std::string> spellings(std::string_view, std::string_view) { return {}; }
void learnSpelling(std::string_view) {}
void speak(std::string_view) {}
std::unique_ptr<SystemVideo> SystemVideo::open(const std::string&, const std::string*) { return nullptr; }
#endif

} // namespace vn
