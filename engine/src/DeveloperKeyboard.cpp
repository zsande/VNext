// The developer tools' on-screen keyboard, for typing into a box without a keyboard: the game's keyboard screen.
#include "DeveloperUi.hpp"

#include <cmath>
#include <cstdlib>

namespace vn {

namespace {

// The scripts the keyboard screen comes from: the game's, as the editor read them last; without one there (or without a
// game), every game's.
const ScreenDef& keyboardScreen(const Script* game) {
    static const Script builtIn = Script::load("scripts/main.vnscript", [](const std::string&) { return std::optional<std::string>("scene start\n"); }, defaultUi());
    if (game)
        if (const auto own = game->screens.find("keyboard"); own != game->screens.end() && !own->second.elements.empty()) return own->second;
    return builtIn.screens.at("keyboard");
}

} // namespace

void Developer::openScreenKeys(bool all) {
    DeveloperTools& d = tools;
    DeveloperTools::ScreenKeys& s = d.screenKeys;
    s.open = true;
    s.shift = false;
    s.box = d.focus;
    s.at = SIZE_MAX; // (once it's drawn: on its first letter)
    s.area = {};
    s.keys.clear();
    if (all) { // (as Tab into a box has it)
        d.anchor = 0;
        d.caret = d.buffer.size();
        d.anchorFocus = d.focusCount;
    }
}

// A controller's button while a box is typed into: A brings the keyboard up; while it's up, the d-pad moves on it, A presses
// the key, B lets the box go, and another button presses the key with it (its keys=: every game's X Delete, Y Space, the
// shoulders the caret, the left trigger Shift and Start Done) (true: it took the button).
bool Developer::screenKeysButton(SDL_GamepadButton button) {
    DeveloperTools& d = tools;
    DeveloperTools::ScreenKeys& s = d.screenKeys;
    if (!s.open) {
        if (button != SDL_GAMEPAD_BUTTON_SOUTH || d.focus.empty() || d.popup.id == d.focus) return false;
        openScreenKeys(true);
        return true;
    }
    const float now = engine.toolsTime();
    auto held = [&](ToolKey which) { d.padHeld = std::pair{which, now + kRepeatAfter}; };
    d.keysTakeOver(); // (the keys are on it: the key they're on shows)
    switch (static_cast<int>(button)) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: screenKeysMove(0, -1), held(ToolKey::Up); return true;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: screenKeysMove(0, 1), held(ToolKey::Down); return true;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: screenKeysMove(-1, 0), held(ToolKey::Left); return true;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: screenKeysMove(1, 0), held(ToolKey::Right); return true;
    case SDL_GAMEPAD_BUTTON_SOUTH:
        if (s.at < s.keys.size()) screenKeysPress(s.keys[s.at].key);
        return true;
    case SDL_GAMEPAD_BUTTON_EAST: s.open = false; return true; // (it goes; the box is still typed into: B again lets it go)
    default: break;
    }
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.button = static_cast<Uint8>(button);
    for (const DeveloperTools::ScreenKeys::Key& key : s.keys)
        if (std::ranges::any_of(key.chords, [&](const KeyChord& k) { return k.matches(e); })) {
            screenKeysPress(key.key);
            if (key.key == "left") held(ToolKey::Previous); // (the caret's: again while it's held)
            else if (key.key == "right") held(ToolKey::Next);
            return true;
        }
    return false;
}

// To the key along its row, or in the next row up or down the one nearest across.
void Developer::screenKeysMove(int dx, int dy) {
    DeveloperTools::ScreenKeys& s = tools.screenKeys;
    if (s.at >= s.keys.size()) return;
    const SDL_FRect now = s.keys[s.at].box;
    const float cx = now.x + now.w / 2, cy = now.y + now.h / 2;
    std::optional<std::size_t> best;
    float nearest = 0.0f;
    for (std::size_t i = 0; i < s.keys.size(); ++i) {
        const SDL_FRect& b = s.keys[i].box;
        const float bx = b.x + b.w / 2, by = b.y + b.h / 2;
        const bool row = std::abs(by - cy) < std::min(b.h, now.h) / 2;
        if (i == s.at || (dx != 0 ? !row || (bx - cx) * static_cast<float>(dx) <= 0.0f : row || (by - cy) * static_cast<float>(dy) <= 0.0f)) continue;
        const float far = dx != 0 ? std::abs(bx - cx) : std::abs(by - cy) * 1000.0f + std::abs(bx - cx); // (the nearest row first)
        if (!best || far < nearest) best = i, nearest = far;
    }
    if (best) s.at = *best;
    if (engine.screenReader()) announce(s.keys[s.at].said);
}

void Developer::screenKeysPress(const std::string& key) {
    DeveloperTools& d = tools;
    DeveloperTools::ScreenKeys& s = d.screenKeys;
    auto press = [&](SDL_Keycode code) { // (as the box's own key: the caret's moves, deleting, Enter)
        SDL_KeyboardEvent e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key = code;
        e.scancode = SDL_GetScancodeFromKey(code, nullptr);
        e.which = kScreenKeyboard;
        e.down = true;
        d.keys.push_back(e);
    };
    if (utf8::length(key) == 1) {
        d.typed += s.shift ? shiftedKey(key) : key;
        s.shift = false; // (one letter at a time, as a phone's)
    } else if (key == "shift") {
        s.shift = !s.shift;
    } else if (key == "space") {
        d.typed += ' ';
    } else if (key == "left" || key == "right" || key == "delete" || key == "done") { // (Done: Enter, which does what it does in the box and lets it go)
        press(key == "left" ? SDLK_LEFT : key == "right" ? SDLK_RIGHT : key == "delete" ? SDLK_BACKSPACE : SDLK_RETURN);
    }
}

// As the frame starts: the keyboard goes with the box it types into (to another box, with it), and a click on it presses the
// key it's on (nothing under it gets the click).
void Developer::screenKeysFrame() {
    DeveloperTools& d = tools;
    DeveloperTools::ScreenKeys& s = d.screenKeys;
    if (!s.open) return;
    if (d.focus.empty()) {
        s.open = false;
        return;
    }
    s.box = d.focus;
    if (!d.clicked || !pointInRect(d.mouseX, d.mouseY, s.area)) return;
    d.clicked = false;
    for (std::size_t i = 0; i < s.keys.size(); ++i)
        if (pointInRect(d.mouseX, d.mouseY, s.keys[i].box)) {
            s.at = i;
            screenKeysPress(s.keys[i].key);
            break;
        }
}

// The keyboard, over the rest: the game's keyboard screen, drawn as the game draws it and where it puts itself on the game's
// screen (the window holding that as the game does), its Shift and letters as the tools' Shift has them; the key a
// controller is on ringed, the one the pointer's on lit.
void Developer::drawScreenKeys(DeveloperUi& ui) {
    DeveloperTools& d = tools;
    DeveloperTools::ScreenKeys& s = d.screenKeys;
    if (!s.open) {
        s.area = {};
        s.keys.clear();
        return;
    }
    const ScreenDef& def = keyboardScreen(d.script.get());
    if (!s.view || &s.view->def() != &def || s.madeAt != d.checked) {
        s.view = std::make_unique<ScreenView>(def, engine, true);
        s.madeAt = d.checked;
    }
    SDL_Renderer* r = ui.r;
    const Canvas& canvas = engine.canvas();
    float w = 0.0f, h = 0.0f;
    if (s.canvas) SDL_GetTextureSize(s.canvas.get(), &w, &h);
    if (!s.canvas || w != canvas.w || h != canvas.h) {
        s.canvas.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, static_cast<int>(canvas.w), static_cast<int>(canvas.h)));
        if (s.canvas) {
            SDL_SetTextureScaleMode(s.canvas.get(), SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(s.canvas.get(), SDL_BLENDMODE_BLEND);
        }
    }
    if (!s.canvas) return;
    {
        const RenderTarget into(r, s.canvas.get());
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        std::optional<bool> shift = s.shift;
        const SwappedIn ours(engine.toolsShift_, shift); // (its Shift, and the letters with it, the tools')
        s.view->draw(r);
    }
    // Where it is on the game's screen, and so in the window; each of its keys (a button that types).
    const SDL_FRect game = fitIn({0.0f, 0.0f, d.size.x, d.size.y}, canvas.w, canvas.h);
    const float scale = game.w / std::max(1.0f, canvas.w);
    auto inWindow = [&](const SDL_FRect& b) { return SDL_FRect{game.x + b.x * scale, game.y + b.y * scale, b.w * scale, b.h * scale}; };
    std::optional<SDL_FRect> all;
    s.keys.clear();
    for (std::size_t i = 0; i < def.elements.size(); ++i) {
        if (!s.view->drawnPart(i)) continue;
        const SDL_FRect b = s.view->partBounds(i);
        SDL_FRect joined = b;
        if (all) SDL_GetRectUnionFloat(&*all, &b, &joined);
        all = joined;
        const Element& el = def.elements[i];
        const auto* u = el.kind == ElementKind::Button && el.actions.size() == 1 ? std::get_if<cmd::Ui>(&el.actions.front()) : nullptr;
        if (!u || u->verb != "type") continue;
        const bool letter = utf8::length(u->arg) == 1;
        std::string said = u->arg == "left" ? std::string("Left"_tr) : u->arg == "right" ? std::string("Right"_tr) : !el.text.empty() ? el.text
                                                                                         : letter && s.shift ? shiftedKey(u->arg)
                                                                                                             : u->arg;
        s.keys.push_back({.box = inWindow(b), .key = u->arg, .said = std::move(said), .chords = el.keys});
    }
    if (!all) return;
    s.area = inWindow(*all);
    SDL_RenderTexture(r, s.canvas.get(), &*all, &s.area);
    if (s.at >= s.keys.size() && !s.keys.empty()) { // (just up: on its first letter)
        const auto first = std::ranges::find_if(s.keys, [](const auto& k) { return k.key.size() == 1 && std::isalpha(static_cast<unsigned char>(k.key[0])); });
        s.at = first != s.keys.end() ? static_cast<std::size_t>(first - s.keys.begin()) : 0;
        if (engine.screenReader()) announce(s.keys[s.at].said);
    }
    const bool pad = d.keyRing; // (the key the controller's on shows while the keys are in charge)
    for (std::size_t i = 0; i < s.keys.size(); ++i) {
        const SDL_FRect& b = s.keys[i].box;
        if (pointInRect(d.mouseX, d.mouseY, b)) draw::rect(r, b, SDL_Color{255, 255, 255, 40});
        if (pad && i == s.at) {
            draw::outline(r, b, kAccent);
            draw::outline(r, {b.x + 1, b.y + 1, b.w - 2, b.h - 2}, kAccent);
        }
    }
}

} // namespace vn
