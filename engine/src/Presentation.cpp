// What the scripts say about showing and playing: keys and buttons, colours, text speed and the like.
#include "Script.hpp"
#include "System.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <expected>
#include <numbers>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vn {

// ---------------------------------------------------------------------------
// Presentation helpers
// ---------------------------------------------------------------------------

namespace {

// Controller buttons by name (Xbox letters; south, east, west, north on other pads).
constexpr std::array<std::pair<std::string_view, int>, 17> kPadButtons{{
    {"pad_a", SDL_GAMEPAD_BUTTON_SOUTH},        {"pad_b", SDL_GAMEPAD_BUTTON_EAST},
    {"pad_x", SDL_GAMEPAD_BUTTON_WEST},         {"pad_y", SDL_GAMEPAD_BUTTON_NORTH},
    {"pad_back", SDL_GAMEPAD_BUTTON_BACK},      {"pad_start", SDL_GAMEPAD_BUTTON_START},
    {"pad_lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"pad_rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"pad_ls", SDL_GAMEPAD_BUTTON_LEFT_STICK},  {"pad_rs", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"pad_up", SDL_GAMEPAD_BUTTON_DPAD_UP},     {"pad_down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"pad_left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"pad_right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"pad_lt", KeyChord::kLeftTrigger},         {"pad_rt", KeyChord::kRightTrigger},
    {"pad_guide", SDL_GAMEPAD_BUTTON_GUIDE},
}};
constexpr std::array<std::pair<std::string_view, KeyChord::Gesture>, 6> kGestures{{
    {"swipe_left", KeyChord::Gesture::SwipeLeft}, {"swipe_right", KeyChord::Gesture::SwipeRight},
    {"swipe_up", KeyChord::Gesture::SwipeUp},     {"swipe_down", KeyChord::Gesture::SwipeDown},
    {"long_press", KeyChord::Gesture::LongPress}, {"two_finger_tap", KeyChord::Gesture::TwoFingerTap},
}};

} // namespace

Uint32 gestureEventType() {
    static const Uint32 type = SDL_RegisterEvents(1);
    return type;
}

std::optional<KeyChord> parseKeyChord(std::string_view text) {
    KeyChord chord;
    std::string name = lower(text);
    for (std::size_t plus; (plus = name.find('+')) != std::string::npos && plus + 1 < name.size();) {
        const std::string mod = name.substr(0, plus);
        if (mod == "ctrl" || mod == "control") chord.mods |= SDL_KMOD_CTRL;
        else if (mod == "shift") chord.mods |= SDL_KMOD_SHIFT;
        else if (mod == "alt" || mod == "option") chord.mods |= SDL_KMOD_ALT;
        else if (mod == "cmd" || mod == "gui" || mod == "super" || mod == "meta") chord.mods |= SDL_KMOD_GUI;
        else return std::nullopt;
        name.erase(0, plus + 1);
    }
    if (name == "mouse_left") chord = {.device = KeyChord::Device::Mouse, .mods = chord.mods, .button = SDL_BUTTON_LEFT};
    else if (name == "mouse_right") chord = {.device = KeyChord::Device::Mouse, .mods = chord.mods, .button = SDL_BUTTON_RIGHT};
    else if (name == "mouse_middle") chord = {.device = KeyChord::Device::Mouse, .mods = chord.mods, .button = SDL_BUTTON_MIDDLE};
    else if (name == "wheel_up") chord = {.device = KeyChord::Device::Wheel, .mods = chord.mods, .wheel = 1};
    else if (name == "wheel_down") chord = {.device = KeyChord::Device::Wheel, .mods = chord.mods, .wheel = -1};
    else if (const auto pad = std::ranges::find(kPadButtons, std::string_view(name), &std::pair<std::string_view, int>::first); pad != kPadButtons.end())
        chord = {.device = KeyChord::Device::Pad, .pad = pad->second};
    else if (const auto g = std::ranges::find(kGestures, std::string_view(name), &std::pair<std::string_view, KeyChord::Gesture>::first); g != kGestures.end())
        chord = {.device = KeyChord::Device::Touch, .gesture = g->second};
    else {
        static const std::map<std::string, std::string, std::less<>> aliases{
            {"esc", "escape"}, {"enter", "return"}, {"pgup", "pageup"}, {"pgdn", "pagedown"},
            {"del", "delete"}, {"kp_enter", "keypad enter"}, {"ins", "insert"},
        };
        if (auto it = aliases.find(name); it != aliases.end()) name = it->second;
        chord.key = SDL_GetKeyFromName(name.c_str());
        if (chord.key == SDLK_UNKNOWN) return std::nullopt;
    }
    return chord;
}

std::string KeyChord::name() const {
    std::string out;
    if (mods & SDL_KMOD_CTRL) out += "ctrl+";
    if (mods & SDL_KMOD_SHIFT) out += "shift+";
    if (mods & SDL_KMOD_ALT) out += "alt+";
    if (mods & SDL_KMOD_GUI) out += "cmd+";
    switch (device) {
    case Device::Mouse: return out + (button == SDL_BUTTON_LEFT ? "mouse_left" : button == SDL_BUTTON_RIGHT ? "mouse_right" : "mouse_middle");
    case Device::Wheel: return out + (wheel > 0 ? "wheel_up" : "wheel_down");
    case Device::Pad:
        for (const auto& [n, b] : kPadButtons) if (b == pad) return std::string(n);
        return "pad_a";
    case Device::Touch:
        for (const auto& [n, g] : kGestures) if (g == gesture) return std::string(n);
        return "long_press";
    case Device::Keyboard: break;
    }
    return out + lower(SDL_GetKeyName(key));
}

bool KeyChord::matches(const SDL_Event& e) const {
    auto modsOf = [](SDL_Keymod m) {
        SDL_Keymod out = 0;
        if (m & SDL_KMOD_CTRL) out |= SDL_KMOD_CTRL;
        if (m & SDL_KMOD_SHIFT) out |= SDL_KMOD_SHIFT;
        if (m & SDL_KMOD_ALT) out |= SDL_KMOD_ALT;
        if (m & SDL_KMOD_GUI) out |= SDL_KMOD_GUI;
        return out;
    };
    switch (device) {
    case Device::Keyboard:
        return e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == key && modsOf(e.key.mod) == mods;
    case Device::Mouse:
        return e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == button && modsOf(SDL_GetModState()) == mods;
    case Device::Wheel:
        return e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.y != 0 && (e.wheel.y > 0 ? 1 : -1) == wheel;
    case Device::Pad:
        return e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == pad;
    case Device::Touch:
        return e.type == gestureEventType() && e.user.code == static_cast<Sint32>(gesture);
    }
    return false;
}

float Element::number(std::string_view name, float fallback) const {
    if (auto it = props.find(name); it != props.end())
        if (auto v = parseNumber(it->second)) return *v;
    return fallback;
}

std::string Element::option(std::string_view name, std::string_view fallback) const {
    if (auto it = props.find(name); it != props.end()) return it->second;
    return std::string(fallback);
}

bool Element::scrolls() const { return lower(option("scroll", "no")) == "yes"; }
bool Element::fitsParts() const { return lower(option("fit")) == "parts"; }
std::string Element::edges() const {
    const std::string own = lower(option("edges"));
    return !own.empty() ? own : scrolls() ? "cut" : "show";
}
bool Element::glides(bool usually) const {
    const std::string own = lower(option("glide"));
    return own.empty() ? usually : own != "no";
}
float Element::dragDirection() const {
    switch (kind) {
    case ElementKind::Container:
        return scrolls() ? -1.0f : 0.0f;
    case ElementKind::Gallery:
    case ElementKind::Endings:
    case ElementKind::Songs:
    case ElementKind::Achievements:
    case ElementKind::Tips:
    case ElementKind::Contacts:
    case ElementKind::Chart:
        return -1.0f;
    case ElementKind::History:
    case ElementKind::Phone:
        return 1.0f;
    default:
        return 0.0f;
    }
}

SDL_Color Element::color(std::string_view name, SDL_Color fallback) const {
    if (auto it = colors.find(name); it != colors.end()) return it->second;
    return fallback;
}

float eased(Ease e, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (e) {
    case Ease::Linear: return t;
    case Ease::In: return t * t;
    case Ease::Out: return 1.0f - (1.0f - t) * (1.0f - t);
    case Ease::InOut: return easeInOut(t);
    case Ease::Bounce: { // (it lands, and bounces lower each time)
        constexpr float n = 7.5625f, d = 2.75f;
        if (t < 1.0f / d) return n * t * t;
        if (t < 2.0f / d) return n * (t - 1.5f / d) * (t - 1.5f / d) + 0.75f;
        if (t < 2.5f / d) return n * (t - 2.25f / d) * (t - 2.25f / d) + 0.9375f;
        return n * (t - 2.625f / d) * (t - 2.625f / d) + 0.984375f;
    }
    case Ease::Spring: // (past it and back, less each time, till it settles)
        if (t <= 0.0f || t >= 1.0f) return t;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * (2.0f * std::numbers::pi_v<float> / 3.0f)) + 1.0f;
    case Ease::Overshoot: { // (a little past it, then back)
        constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
        const float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u;
    }
    }
    return t;
}

Animation::Look Animation::Look::with(const Look& other) const {
    Look out = *this;
    out.x += other.x;
    out.y += other.y;
    out.opacity *= other.opacity;
    out.zoom *= other.zoom;
    out.rotate += other.rotate;
    if (other.color) out.color = other.color;
    if (other.fade > out.fade) {
        out.fade = other.fade;
        out.fadeColor = other.fadeColor;
    }
    if (other.wipe < out.wipe) {
        out.wipe = other.wipe;
        out.wipeShape = other.wipeShape;
    }
    return out;
}

Animation::Look Animation::at(float seconds, SDL_Color own, const Offset& offset, SDL_FPoint screen, const Look& start) const {
    // Where it is in one time through: again and again, a few times (then as its end left it), or once.
    float t = std::max(0.0f, seconds);
    if (length <= 0.0f) t = 0.0f;
    else if (!loops() && t >= total()) t = length;
    else t = std::fmod(t, length);
    // A change's value as it goes: from where it began towards its own, as its easing has it by then.
    auto progress = [](const Change& c, float when) { return eased(c.ease, c.seconds > 0.0f ? (when - c.start) / c.seconds : 1.0f); };
    auto target = [&](const Change& c) {
        switch (c.what) {
        case Value::X: return c.by ? c.to * screen.x : std::isnan(c.to) ? 0.0f : offset(true, c.to); // (home: its own place)
        case Value::Y: return c.by ? -c.to * screen.y : std::isnan(c.to) ? 0.0f : offset(false, c.to);
        case Value::Fade: return c.color.a == 0 ? 0.0f : 1.0f; // (covered, or uncovered again)
        default: return c.to;
        }
    };
    auto mix = [](SDL_Color a, SDL_Color b, float e) {
        auto channel = [e](Uint8 x, Uint8 y) { return static_cast<Uint8>(std::clamp(std::lround(x + (y - x) * e), 0L, 255L)); };
        return SDL_Color{channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
    };
    // Each value: the latest change of it begun by now, from where the ones before it had taken it.
    auto number = [&](Value what, float initial) {
        const Change* last = nullptr;
        float from = initial;
        for (const Change& c : changes) {
            if (c.what != what) continue;
            if (t < c.start) break;
            from = last ? std::lerp(from, target(*last), progress(*last, c.start)) : initial;
            last = &c;
        }
        return last ? std::lerp(from, target(*last), progress(*last, t)) : initial;
    };
    Look now{.x = number(Value::X, start.x), .y = number(Value::Y, start.y), .opacity = number(Value::Opacity, start.opacity),
             .zoom = number(Value::Zoom, start.zoom), .rotate = number(Value::Rotate, start.rotate), .color = start.color,
             .fade = number(Value::Fade, start.fade), .fadeColor = start.fadeColor, .wipe = std::clamp(number(Value::Wipe, start.wipe), 0.0f, 1.0f),
             .wipeShape = start.wipeShape};
    const Change* last = nullptr;
    const SDL_Color first = start.color.value_or(own);
    SDL_Color from = first;
    for (const Change& c : changes) {
        if (t < c.start) break;
        if (c.what == Value::Fade && c.color.a != 0) now.fadeColor = {c.color.r, c.color.g, c.color.b, 255}; // (going away again: from the one it had)
        if (c.what == Value::Wipe) now.wipeShape = c.shape;
        if (c.what != Value::Color) continue;
        from = last ? mix(from, last->color, progress(*last, c.start)) : first;
        last = &c;
    }
    if (last) now.color = mix(from, last->color, progress(*last, t));
    return now;
}

const Style& Element::substyle(std::string_view name) const {
    if (auto it = substyles.find(name); it != substyles.end()) return it->second;
    return style;
}

void Script::forEachText(const std::function<void(std::string& text, const TextPlace& place)>& f) {
    auto sorted = [](auto& map) { // the same order every time (and in --translate files)
        std::vector<std::pair<const std::string*, decltype(&map.begin()->second)>> out;
        out.reserve(map.size());
        for (auto& [key, value] : map) out.emplace_back(&key, &value);
        std::ranges::sort(out, {}, [](const auto& p) { return *p.first; });
        return out;
    };
    auto commands = [&](Commands& list, const TextPlace& place) { // messages buttons and keys show
        for (Command& c : list)
            if (auto* ui = std::get_if<cmd::Ui>(&c); ui && ui->verb == "notify") f(ui->arg, place);
    };

    f(config.title, {.what = "title"});
    for (Instruction& ins : code) {
        const TextPlace place{.line = &ins};
        std::visit(Overloaded{
                       [&](cmd::Say& c) {
                           if (!c.speaker.empty()) f(c.speaker, place);
                           f(c.text, place);
                       },
                       [&](cmd::Menu& c) {
                           if (!c.prompt.empty()) f(c.prompt, place);
                           for (auto& choice : c.choices) {
                               f(choice.text, place);
                               if (!choice.note.empty()) f(choice.note, place);
                           }
                       },
                       [&](cmd::Ask& c) {
                           f(c.prompt, place);
                           if (!c.fallback.empty()) f(c.fallback, place);
                       },
                       [&](cmd::Ui& c) {
                           if (c.verb == "notify") f(c.arg, place);
                       },
                       [&](cmd::Chapter& c) { f(c.title, place); },
                       [&](cmd::Mark& c) { f(c.title, place); },
                       [&](cmd::Message& c) {
                           if (!c.sender.empty()) f(c.sender, place);
                           f(c.text, place);
                           for (auto& reply : c.replies) {
                               f(reply.text, place);
                               if (!reply.note.empty()) f(reply.note, place);
                           }
                       },
                       [](auto&) {},
                   },
                   ins.command);
    }
    for (auto [key, def] : sorted(characters)) f(def->name, {.what = "character", .name = *key});
    for (auto [key, screen] : sorted(screens)) {
        const TextPlace place{.what = "screen", .name = *key};
        for (Element& el : screen->elements) {
            if (el.kind == ElementKind::Text || el.kind == ElementKind::Button) f(el.text, place);
            if (!el.confirm.empty()) f(el.confirm, place);
            if (auto it = el.props.find("hint"); it != el.props.end()) f(it->second, place); // (an input's)
            commands(el.actions, place);
        }
        commands(screen->onShow, place);
        commands(screen->onHide, place);
    }
    for (Control& c : controls) commands(c.actions, {.what = "control", .name = c.name});
    for (std::size_t k = 0; k < lists.size(); ++k)
        for (Record& r : lists[k]) {
            f(r.title, {.what = kRecordKinds[k], .name = r.name});
            if (!r.text.empty()) f(r.text, {.what = kRecordKinds[k], .name = r.name});
        }
}

void Script::translate(const Translation* translation) {
    if (originals_.empty()) forEachText([&](std::string& text, const TextPlace&) { originals_.push_back(text); });
    translation_ = translation;
    std::size_t i = 0;
    forEachText([&](std::string& text, const TextPlace&) {
        const std::string& original = originals_[i++];
        text = translation ? translation->text(original) : original;
    });
}

std::string_view keyWord(std::string_view id) {
    const auto it = std::ranges::find(kKeyWords, id, &std::pair<std::string_view, std::string_view>::first);
    return it != kKeyWords.end() ? it->second : id;
}

std::string Script::translated(std::string_view text) const {
    return translation_ ? translation_->text(std::string(text)) : std::string(text);
}

} // namespace vn
