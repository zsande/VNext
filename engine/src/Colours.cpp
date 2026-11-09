// Colours: read from their names, codes or gradients ("navy-black"), and said in words ("like navy").
#include "Basics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace vn {

namespace {

// The built-in colours, by name.
const Palette& colourNames() {
    static const Palette names{
        {"white", {255, 255, 255, 255}},     {"black", {0, 0, 0, 255}},        {"grey", {128, 128, 128, 255}},
        {"silver", {192, 192, 200, 255}},    {"red", {230, 60, 60, 255}},      {"darkred", {140, 20, 30, 255}},
        {"orange", {255, 150, 50, 255}},     {"gold", {255, 211, 110, 255}},   {"yellow", {255, 230, 80, 255}},
        {"lime", {180, 240, 120, 255}},      {"green", {70, 180, 90, 255}},    {"darkgreen", {20, 90, 40, 255}},
        {"teal", {40, 160, 160, 255}},       {"cyan", {90, 220, 230, 255}},    {"skyblue", {143, 211, 255, 255}},
        {"lightblue", {180, 210, 255, 255}}, {"blue", {70, 110, 240, 255}},    {"navy", {20, 30, 90, 255}},
        {"purple", {150, 80, 200, 255}},     {"violet", {184, 160, 255, 255}}, {"magenta", {230, 80, 200, 255}},
        {"pink", {255, 158, 207, 255}},      {"hotpink", {255, 90, 170, 255}}, {"brown", {140, 90, 50, 255}},
        {"beige", {235, 220, 190, 255}},     {"transparent", {0, 0, 0, 0}},    {"none", {0, 0, 0, 0}},
    };
    return names;
}

} // namespace

std::optional<SDL_Color> parseColour(std::string_view text, const Palette* palette) {
    const Palette& names = colourNames();
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
    const auto top = parseColour(key.substr(0, dash), palette), bottom = parseColour(key.substr(dash + 1), palette);
    if (!top || !bottom) return std::nullopt;
    return std::pair{*top, *bottom};
}

std::string colourWords(std::string_view value, const Palette* palette) {
    auto one = [&](std::string_view v) {
        v = trim(v);
        const auto c = parseColour(v, palette);
        return c && v.starts_with('#') ? colourName(*c, palette) : std::string(v);
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

std::string colourName(SDL_Color c, const Palette* palette) {
    if (c.a == 0) return "transparent";
    auto same = [&](SDL_Color o) { return o.r == c.r && o.g == c.g && o.b == c.b; };
    const std::string percent = std::format(", {}%", (c.a * 100 + 127) / 255), amount = c.a == 255 ? std::string{} : percent;
    // The game's colour that it is; else a built-in one with its red, green and blue; else the game's with them
    // and another opacity (its own said).
    if (palette)
        for (const auto& [name, colour] : *palette)
            if (same(colour) && colour.a == c.a) return name;
    for (const auto& [name, colour] : colourNames())
        if (colour.a != 0 && same(colour)) return name + amount;
    if (palette)
        for (const auto& [name, colour] : *palette)
            if (same(colour)) return name + percent;
    // Hue, saturation and value: which built-in colour it's most like (a grey's hue means nothing).
    const auto [h, s, v] = hsvOf(c);
    std::string nearest;
    float best = std::numeric_limits<float>::max();
    for (const auto& [name, colour] : colourNames()) {
        if (colour.a == 0 || name == "grey") continue;
        const auto [h2, s2, v2] = hsvOf(colour);
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

} // namespace vn
