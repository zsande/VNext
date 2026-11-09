// The player's settings: their values (kept in settings.txt), what a new player starts with, the setting parts
// that change them, and putting a change in effect.
#include "Engine.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <ranges>
#include <sstream>
#include <variant>

namespace vn {

namespace {

int percent(float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 100.0f)); }

// The settings kept in settings.txt and set by "setting default.NAME" (by the same names), and
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
    {"made_up_voices", &Settings::madeUpVoices},
    {"typing_sounds", &Settings::typingSounds},
    {"mouths", &Settings::mouths, 0, 2},
    {"text_speed", &Settings::textSpeed, 0, 150}, // (as far as their sliders go: text speed 10 to 150, or 0, at once)
    {"auto_delay", &Settings::autoDelay, 300, 5000},
    {"time_limits", &Settings::timeLimits},
    {"textbox_opacity", &Settings::textboxOpacity, 0, 100},
    {"text_size", &Settings::textSize, 75, 150},
    {"animations", &Settings::animations},
    {"skip_unread", &Settings::skipUnread},
    {"skip_after_choices", &Settings::skipAfterChoices},
    {"rollback", &Settings::rollback},
    {"skip_movies", &Settings::skipMovies},
    {"read_aloud", &Settings::readAloud},
    {"high_contrast", &Settings::highContrast},
    {"captions", &Settings::captions},
    {"vibration", &Settings::vibration},
};

} // namespace

std::optional<int> Settings::get(std::string_view key) const {
    const auto f = std::ranges::find(kFields, key, &Field::name);
    if (f == std::end(kFields)) return std::nullopt;
    return std::visit([&](auto field) { return static_cast<int>(this->*field); }, f->field);
}

bool Settings::has(std::string_view key) { return std::ranges::find(kFields, key, &Field::name) != std::end(kFields); }

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

const SettingControl* findSettingControl(std::string_view name) {
    auto volume = [](int Settings::*field, bool test) {
        return SettingControl{.value = [=](const Settings& s) { return static_cast<float>(s.*field) / 100.0f; },
                              .setValue = [=](Settings& s, float v) { s.*field = percent(v); },
                              .number = [=](const Settings& s) { return static_cast<double>(s.*field); }, // (a percentage)
                              .testSound = test};
    };
    static const std::map<std::string, SettingControl, std::less<>> controls{
        {"text_speed",
         // 10..150 letters a second in steps of 5; the far right end is at once (0)
         {.value = [](const Settings& s) { return s.textSpeed == 0 ? 1.0f : static_cast<float>(s.textSpeed - 10) / 140.0f * 0.95f; },
          .setValue = [](Settings& s, float v) { s.textSpeed = v >= 0.975f ? 0 : 10 + 5 * static_cast<int>(std::lround(std::min(v / 0.95f, 1.0f) * 28.0f)); },
          .number = [](const Settings& s) { return static_cast<double>(s.textSpeed); }}},
        {"auto_delay",
         {.value = [](const Settings& s) { return static_cast<float>(s.autoDelay - 300) / 4700.0f; },
          .setValue = [](Settings& s, float v) { s.autoDelay = 300 + 100 * static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 47.0f)); },
          .number = [](const Settings& s) { return s.autoDelay / 1000.0; }}}, // (in seconds)
        {"textbox_opacity", volume(&Settings::textboxOpacity, false)},
        {"text_size",
         {.value = [](const Settings& s) { return static_cast<float>(s.textSize - 75) / 75.0f; }, // 75% to 150% in steps of 5
          .setValue = [](Settings& s, float v) { s.textSize = 75 + 5 * static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 15.0f)); },
          .number = [](const Settings& s) { return static_cast<double>(s.textSize); }}}, // (a percentage)
        {"master_volume", volume(&Settings::masterVolume, true)},
        {"music_volume", volume(&Settings::musicVolume, false)},
        {"sound_volume", volume(&Settings::soundVolume, true)},
        {"voice_volume", volume(&Settings::voiceVolume, false)},
    };
    auto it = controls.find(name);
    return it != controls.end() ? &it->second : nullptr;
}

Settings defaultSettings(const GameSettings& game) {
    Settings s;
    s.windowWidth = game.width;
    s.windowHeight = game.height;
    for (const auto& [key, value] : game.settingDefaults) s.set(key, value);
    if (screenReaderOn()) s.readAloud = true;
    return s;
}

// A button's "setting NAME VALUE" (as the parser leaves it: "NAME NUMBER"; the window's size and the
// language go up or down a step, the window's size can be given ("window_size 1280 720") and the
// language named ("language japanese")).
void Engine::changeSetting(std::string_view what) {
    Settings& s = settings_;
    const auto space = what.find(' ');
    const std::string_view name = what.substr(0, space);
    int n = 0; // (the number after the name: the first of them, if there are two)
    if (space != std::string_view::npos) std::from_chars(what.data() + space + 1, what.data() + what.size(), n);
    if (name == "window_size" && n != 1 && n != -1) { // a width and a height ("window_size 1280 720")
        const std::string_view rest = space == std::string_view::npos ? std::string_view{} : what.substr(space + 1);
        const auto gap = rest.find(' ');
        int w = 0, h = 0;
        if (gap != std::string_view::npos) {
            std::from_chars(rest.data(), rest.data() + gap, w);
            std::from_chars(rest.data() + gap + 1, rest.data() + rest.size(), h);
        }
        if (w > 0 && h > 0) {
            s.windowWidth = w;
            s.windowHeight = h;
        }
    } else if (name == "window_size") { // the next size that fits the display, up or down
        refreshWindowSizes();
        if (s.fullscreen || windowSizes_.empty()) return;
        const auto count = static_cast<int>(windowSizes_.size());
        int at = -1;
        for (int i = 0; i < count; ++i)
            if (windowSizes_[static_cast<std::size_t>(i)].x == s.windowWidth && windowSizes_[static_cast<std::size_t>(i)].y == s.windowHeight) at = i;
        if (at < 0) { // (a size of the player's own: the one past it that way)
            int below = -1, above = count;
            for (int i = 0; i < count; ++i)
                if (windowSizes_[static_cast<std::size_t>(i)].x < s.windowWidth) below = i;
                else if (above == count) above = i;
            at = n > 0 ? above - n : below - n;
        }
        const auto& size = windowSizes_[static_cast<std::size_t>(std::clamp(at + n, 0, count - 1))];
        s.windowWidth = size.x;
        s.windowHeight = size.y;
    } else if (name == "language") {
        const auto count = static_cast<int>(languageNames().size());
        const std::string_view word = space == std::string_view::npos ? std::string_view{} : what.substr(space + 1);
        if (n != 0) {
            if (count > 0) s.language = languageId(((languageIndex() + n) % count + count) % count);
        } else if (const auto id = languageIdNamed(word)) {
            s.language = *id;
        } else {
            logWarn("There's no language called '{}'.", word);
        }
    } else {
        s.set(name, n);
    }
}

void Engine::settingsChanged(bool testSound) {
    if (sampling_) return;
    applySettings();
    if (testSound) sound_.playTestSound();
}

void Engine::saveSettings() {
    if (!prefPath_.empty() && !settings_.save(prefPath_ + "settings.txt")) logWarn("Can't save the settings: {}.", SDL_GetError());
}

void Engine::toggleFullscreen() {
    settings_.fullscreen = !settings_.fullscreen;
    applySettings();
    saveSettings();
}

void Engine::applySettings() {
    const Settings& s = settings_;
    SDL_Window* window = window_.get();
    SDL_Renderer* renderer = renderer_.get();
    if (!window || !renderer) return;

    if (s.fullscreen != applied_.fullscreen) SDL_SetWindowFullscreen(window, s.fullscreen);
    if (!s.fullscreen && (s.windowWidth != applied_.windowWidth || s.windowHeight != applied_.windowHeight)) {
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(window);
        SDL_SetWindowSize(window, s.windowWidth, s.windowHeight);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    applyGameView();
    if (!SDL_SetRenderVSync(renderer, s.vsync ? 1 : 0)) logWarn("Can't change VSync: {}.", SDL_GetError());
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, std::to_string(s.frameLimit).c_str());
    sound_.setVolume(static_cast<float>(s.masterVolume) / 100.0f, static_cast<float>(s.musicVolume) / 100.0f,
                     static_cast<float>(s.soundVolume) / 100.0f, static_cast<float>(s.voiceVolume) / 100.0f);
    if (s.language != applied_.language && language() != script_.translation()) {
        useLanguage();
        if (game_) game_->retranslate();
    }
    applied_ = s;
    updatePixelScale();
}

} // namespace vn
