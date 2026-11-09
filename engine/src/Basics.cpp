// Small helpers for words, numbers and names (Basics.hpp).
#include "Basics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>

namespace vn {

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

std::string shortened(std::string_view text, const std::function<bool(std::string_view)>& fits) {
    if (text.empty() || fits(text)) return std::string(text);
    std::vector<std::size_t> cuts; // (where each letter starts: it can be cut there)
    for (std::size_t i = 0; i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) cuts.push_back(i);
    auto cut = [&](std::size_t at) { // (no space or comma left hanging before the dots)
        while (at > 0 && std::string_view(" \t\n,:;(").contains(text[at - 1])) --at;
        return std::string(text.substr(0, at)) + "...";
    };
    std::size_t fit = 0, over = cuts.size(); // (the most letters that fit, found by halves: all of them don't)
    while (over - fit > 1) {
        const std::size_t mid = (fit + over) / 2;
        if (fits(cut(cuts[mid]))) fit = mid;
        else over = mid;
    }
    return cut(cuts[fit]);
}

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
// however they're capitalised: Őrs is ŐRS and őrs.
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

std::string fileNameOf(std::string_view text) {
    std::string name;
    for (const char c : text)
        if (!std::string_view("/\\:*?\"<>|").contains(c) && static_cast<unsigned char>(c) >= 32) name += c;
    auto edge = [](char c) { return c == '.' || c == ' '; };
    while (!name.empty() && edge(name.front())) name.erase(0, 1);
    while (!name.empty() && edge(name.back())) name.pop_back();
    static constexpr std::string_view kKept[]{"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                                              "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    if (std::ranges::contains(kKept, std::string_view(lower(name.substr(0, name.find('.')))))) name += "_";
    return name;
}

std::string saveFolderNameOf(std::string_view saveFolder, std::string_view title) {
    const std::string name = fileNameOf(trim(saveFolder).empty() ? title : saveFolder);
    return name.empty() ? std::string("VNext") : name;
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

float placeEdge(float place, float size, float screen) {
    const float room = screen - size;
    if (place < -1.0f) return (place + 1.0f) * size;      // (off the left: at -2, just off)
    if (place > 1.0f) return room + (place - 1.0f) * size; // (off the right)
    return room * (place + 1.0f) / 2.0f;
}

} // namespace vn
