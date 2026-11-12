// Controls: the keys, mouse buttons, wheel, controller buttons and touches a control's keys= name, what they're called,
// and the player changing them (the Keys rows of Settings).
#include "Engine.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

std::optional<std::size_t> toolsVerbOf(const Control& control) {
    if (control.actions.size() != 1) return std::nullopt;
    const auto* ui = std::get_if<cmd::Ui>(&control.actions.front());
    if (!ui || ui->verb != "tools") return std::nullopt;
    const auto it = std::ranges::find(kToolsVerbs, std::string_view(ui->arg));
    return it != kToolsVerbs.end() ? std::optional(static_cast<std::size_t>(it - kToolsVerbs.begin())) : std::nullopt;
}

bool Control::onScreens() const {
    return !actions.empty() && std::ranges::all_of(actions, [](const Command& a) {
        const auto* ui = std::get_if<cmd::Ui>(&a);
        return ui && (ui->verb.starts_with("move ") || ui->verb.starts_with("scroll ") || ui->verb == "press");
    });
}

bool Control::repeats() const {
    return !actions.empty() && std::ranges::all_of(actions, [](const Command& a) {
        const auto* ui = std::get_if<cmd::Ui>(&a);
        return ui && (ui->verb.starts_with("move ") || ui->verb.starts_with("scroll "));
    });
}

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

bool KeyChord::matches(const SDL_Event& e, bool repeats) const {
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
        return e.type == SDL_EVENT_KEY_DOWN && (repeats || !e.key.repeat) && e.key.key == key && modsOf(e.key.mod) == mods;
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

std::string_view keyWord(std::string_view id) {
    const auto it = std::ranges::find(kKeyWords, id, &std::pair<std::string_view, std::string_view>::first);
    return it != kKeyWords.end() ? it->second : id;
}

std::vector<std::string> Engine::keyNames(const Control& control) const {
    if (auto it = settings_.keys.find(control.name); it != settings_.keys.end()) return it->second;
    std::vector<std::string> names;
    names.reserve(control.keys.size());
    for (const auto& k : control.keys) names.push_back(k.name());
    return names;
}

void Engine::refreshKeys() {
    controlKeys_.clear();
    for (const auto& control : script_.controls) {
        auto& keys = controlKeys_.emplace_back();
        for (const auto& name : keyNames(control))
            if (auto key = parseKeyChord(name)) keys.push_back(*key);
    }
}

std::string Engine::keyLabel(const KeyChord& key) const {
    std::string out;
    if (key.mods & SDL_KMOD_CTRL) out += "Ctrl+";
    if (key.mods & SDL_KMOD_SHIFT) out += "Shift+";
    if (key.mods & SDL_KMOD_ALT) out += "Alt+";
    if (key.mods & SDL_KMOD_GUI) out += "Cmd+";
    switch (key.device) {
    case KeyChord::Device::Mouse:
        return out + script_.translated(keyWord(key.button == SDL_BUTTON_LEFT ? "mouse_left" : key.button == SDL_BUTTON_RIGHT ? "mouse_right" : "mouse_middle"));
    case KeyChord::Device::Wheel:
        return out + script_.translated(keyWord(key.wheel > 0 ? "wheel_up" : "wheel_down"));
    case KeyChord::Device::Pad:
    case KeyChord::Device::Touch:
        return script_.translated(keyWord(key.name())); // pad_a, swipe_left...
    case KeyChord::Device::Keyboard:
        break;
    }
    const std::string name = SDL_GetKeyName(key.key);
    return out + (name == "Return" ? std::string("Enter") : name);
}

namespace {

bool padOrTouch(const std::string& name) {
    const auto key = parseKeyChord(name);
    return key && (key->device == KeyChord::Device::Pad || key->device == KeyChord::Device::Touch);
}

} // namespace

// Where a control's keyboard and mouse keys are in its list: slot 1, 2... count only those
// (controller buttons and touch gestures have their own column, or none).
std::vector<std::size_t> Engine::keySlots(const std::vector<std::string>& names) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < names.size(); ++i)
        if (!padOrTouch(names[i])) out.push_back(i);
    return out;
}

std::size_t Engine::slotOf(const Element& el) {
    if (el.option("slot") == "pad") return kPadSlot;
    return static_cast<std::size_t>(std::max(1.0f, el.number("slot", 1.0f))) - 1;
}

std::string Engine::keyLabel(std::string_view control, std::size_t slot) const {
    const Control* c = script_.control(control);
    const auto names = c ? keyNames(*c) : std::vector<std::string>{};
    if (slot == kPadSlot) { // the first controller button it has
        const auto pad = std::ranges::find_if(names, [](const std::string& n) { return n.starts_with("pad_"); });
        if (pad != names.end())
            if (auto key = parseKeyChord(*pad)) return keyLabel(*key);
        return {};
    }
    const auto keys = keySlots(names);
    if (slot < keys.size())
        if (auto key = parseKeyChord(names[keys[slot]])) return keyLabel(*key);
    return {};
}

// While an input for a control's key waits for a key: Esc cancels, Backspace / Delete empties the slot,
// anything else (a key, possibly with Ctrl/Shift/Alt/Cmd, a mouse button or the wheel) is taken; a controller's button
// goes in its controller slot. A mouse button coming up goes on to the screens (the press that started the wait ends
// there: else the list it's in would think it's still being dragged, and the next click would end that).
bool Engine::captureKey(const SDL_Event& e) {
    const auto mods = static_cast<SDL_Keymod>(SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI));
    KeyChord key;
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        if (e.key.repeat) return true;
        switch (e.key.key) {
        case SDLK_ESCAPE: rebinding_.reset(); return true;
        case SDLK_BACKSPACE:
        case SDLK_DELETE: setKey("none"); return true;
        case SDLK_LCTRL: case SDLK_RCTRL: case SDLK_LSHIFT: case SDLK_RSHIFT:
        case SDLK_LALT: case SDLK_RALT: case SDLK_LGUI: case SDLK_RGUI:
            return true; // wait for the key that goes with it
        default: break;
        }
        key = {.key = e.key.key, .mods = mods};
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button > SDL_BUTTON_RIGHT) return true;
        key = {.device = KeyChord::Device::Mouse, .mods = mods, .button = e.button.button};
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.wheel.y == 0) return true;
        key = {.device = KeyChord::Device::Wheel, .mods = mods, .wheel = e.wheel.y > 0 ? 1 : -1};
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        key = {.device = KeyChord::Device::Pad, .pad = e.gbutton.button};
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return false;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_TEXT_INPUT:
        return true;
    default:
        return false;
    }
    if (rebinding_->second == kPadSlot && key.device != KeyChord::Device::Pad) return true; // the Controller column takes buttons only
    if (key.device == KeyChord::Device::Pad) rebinding_->second = kPadSlot; // (a button for a key's slot: the control's controller button)
    setKey(key.name());
    return true;
}

void Engine::setKey(const std::string& name) {
    if (!rebinding_) return;
    const auto [control, slot] = *std::exchange(rebinding_, std::nullopt);
    const Control* target = script_.control(control);
    if (!target) return;
    if (name != "none") // a key does one thing: take it away from the others (on screens, or in the story, as it is)
        for (const auto& other : script_.controls) {
            if (other.name == control || other.onScreens() != target->onScreens()) continue;
            auto names = keyNames(other);
            if (std::ranges::contains(names, name)) {
                if (padOrTouch(name)) std::erase(names, name);              // its column just goes empty
                else std::ranges::replace(names, name, std::string("none")); // the others keep their slots
                settings_.keys.insert_or_assign(other.name, std::move(names));
            }
        }
    auto names = keyNames(*target);
    if (slot == kPadSlot) { // replaces its controller button (or adds one)
        const auto pad = std::ranges::find_if(names, [](const std::string& n) { return n.starts_with("pad_"); });
        if (name == "none") {
            if (pad != names.end()) names.erase(pad);
        } else if (pad != names.end()) {
            *pad = name;
        } else {
            names.push_back(name);
        }
    } else {
        auto keys = keySlots(names);
        while (keys.size() <= slot) {
            names.emplace_back("none");
            keys.push_back(names.size() - 1);
        }
        names[keys[slot]] = name;
    }
    settings_.keys.insert_or_assign(control, std::move(names));
    refreshKeys();
    saveSettings();
}

} // namespace vn
