// The developer tools' input: events, the tools' own keys, a controller (its sticks are its d-pad and the wheel), the
// keys moving between the stops on a page as on the game's screens (Tab and Shift+Tab too), and the clipboard.
#include "DeveloperUi.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>

namespace vn {

void Developer::developerEvent(const SDL_Event& e) {
    DeveloperTools& d = tools;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: // (the pointer's in charge: the keys' ring goes, and their place follows it)
        d.mouseX = e.motion.x;
        d.mouseY = e.motion.y;
        d.pointerMoved = true;
        d.keyRing = false;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        d.mouseX = e.button.x;
        d.mouseY = e.button.y;
        d.keyRing = false;
        if (e.button.button == SDL_BUTTON_LEFT) {
            d.clicked = d.held = true;
            d.doubleClick = e.button.clicks >= 2;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) d.held = false;
        break;
    case SDL_EVENT_MOUSE_WHEEL: // (where it's turned: who's in charge stays)
        d.wheelX = e.wheel.mouse_x;
        d.wheelY = e.wheel.mouse_y;
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) && e.wheel.x == 0.0f) d.wheelAcross -= e.wheel.y; // (Shift and the wheel: across)
        else {
            d.wheel += e.wheel.y;
            d.wheelAcross += e.wheel.x;
        }
        break;
    case SDL_EVENT_TEXT_INPUT: // (a real keyboard: the on-screen one goes)
        d.screenKeys.open = false;
        if (!d.focus.empty()) d.typed += e.text.text;
        else if (const DeveloperTools::KeyStop* at = d.keyRing ? d.keysOn() : nullptr;
                 at && at->kind == DeveloperTools::KeyStop::Kind::Type && std::string_view(e.text.text) != " ") { // (the keys on a box: it's typed into, all of it chosen to type over; Space presses it)
            d.focusOn(at->id, at->value, true);
            d.typed += e.text.text;
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (std::exchange(d.screenKeys.open, false) && e.key.key == SDLK_ESCAPE && !e.key.repeat) break; // (a real key puts the on-screen keyboard away: Esc only that)
        d.keys.push_back(e.key);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        padButton(e);
        break;
    case SDL_EVENT_DROP_FILE: { // (imported into the game)
        const std::scoped_lock _(d.imports.mutex);
        if (e.drop.data) d.imports.files.emplace_back(e.drop.data, std::string{});
        d.imports.dropAt = SDL_FPoint{e.drop.x, e.drop.y};
        break;
    }
    default:
        break;
    }
}

// The tools' own keys: what each of their controls has (action="tools WHAT"; a game's own control of that name replaces
// the built-in one, as in the game), as the scripts are now in the editor; without a game, the built-in ones.
void Developer::readToolKeys() {
    DeveloperTools& d = tools;
    static const Script builtIn = Script::load("scripts/main.vnscript", [](const std::string&) { return std::optional<std::string>("scene start\n"); }, defaultUi()); // (without a game: the built-in controls, read with a story of nothing)
    for (auto& keys : d.toolKeys) keys.clear();
    for (const Control& c : (d.script ? *d.script : builtIn).controls)
        if (const auto i = toolsVerbOf(c)) d.toolKeys[*i] = c.keys;
}

bool Developer::toolKey(const SDL_Event& e, ToolKey which) {
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) { // (a controller's: pad_guide for Play...)
        readToolKeys();
        return std::ranges::any_of(tools.toolKeys[static_cast<std::size_t>(which)], [&](const KeyChord& k) { return k.matches(e); });
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return false;
    readToolKeys();
    return tools.isKey(e.key, which);
}

// One of the tools' keys pressed (from its button, or a controller's), whatever its keys: with its first key's code, so a box
// that's typed into does what that key does there (Return, Escape, the arrows).
void Developer::pressTool(ToolKey which) {
    DeveloperTools& d = tools;
    SDL_KeyboardEvent e{};
    e.type = SDL_EVENT_KEY_DOWN;
    if (const auto& keys = d.toolKeys[static_cast<std::size_t>(which)]; !keys.empty() && keys.front().device == KeyChord::Device::Keyboard) {
        e.key = keys.front().key;
        e.mod = keys.front().mods;
    }
    e.which = kToolButton;
    e.raw = static_cast<Uint16>(which);
    e.down = true;
    d.keys.push_back(e);
}

// A controller's button: the tools' keys whose controls have it (tools_press pad_a...), pressed. The d-pad's and the shoulders'
// go again while held (after a moment, ten times a second).
void Developer::padButton(const SDL_Event& e) {
    DeveloperTools& d = tools;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && screenKeysButton(static_cast<SDL_GamepadButton>(e.gbutton.button))) return;
    readToolKeys();
    SDL_Event down = e;
    down.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; // (to find its keys either way)
    for (std::size_t i = 0; i < d.toolKeys.size(); ++i) {
        if (std::ranges::none_of(d.toolKeys[i], [&](const KeyChord& k) { return k.matches(down); })) continue;
        const auto which = static_cast<ToolKey>(i);
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
            if (d.padHeld && d.padHeld->first == which) d.padHeld.reset();
            continue;
        }
        if (which == ToolKey::Play) continue; // (Engine::event has it: back to the game)
        if (which == ToolKey::Press) d.padPressed = true;
        pressTool(which);
        if (which == ToolKey::Up || which == ToolKey::Down || which == ToolKey::Left || which == ToolKey::Right || which == ToolKey::Next || which == ToolKey::Previous)
            d.padHeld = std::pair{which, engine.toolsTime() + kRepeatAfter};
    }
}

void Developer::padMoves() {
    DeveloperTools& d = tools;
    const float now = engine.toolsTime();
    auto again = [&](ToolKey which) { // (on the on-screen keyboard, when it's up: its keys, or the caret)
        if (!d.screenKeys.open) return pressTool(which);
        switch (which) {
        case ToolKey::Up: return screenKeysMove(0, -1);
        case ToolKey::Down: return screenKeysMove(0, 1);
        case ToolKey::Left: return screenKeysMove(-1, 0);
        case ToolKey::Right: return screenKeysMove(1, 0);
        case ToolKey::Previous: return screenKeysPress("left");
        case ToolKey::Next: return screenKeysPress("right");
        default: return;
        }
    };
    if (d.padHeld && now >= d.padHeld->second) {
        again(d.padHeld->first);
        d.padHeld->second = now + kRepeatEvery;
    }
}

void Developer::stickWheel(float up, float across) {
    DeveloperTools& d = tools;
    const DeveloperTools::KeyStop* at = d.keyRing ? d.keysOn() : nullptr;
    d.wheelX = at ? at->box.x + at->box.w * 0.5f : d.mouseX;
    d.wheelY = at ? at->box.y + at->box.h * 0.5f : d.mouseY;
    d.wheel += up;
    d.wheelAcross += across;
}

namespace dev {

SDL_Event mouseMotionAt(float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = x;
    e.motion.y = y;
    return e;
}

std::vector<std::string> clipboardLines() {
    std::vector<std::string> out;
    if (const SdlOwned<char> text{SDL_GetClipboardText()}) out = linesOf(text.get());
    while (!out.empty() && blank(out.back())) out.pop_back();
    return out;
}

char clipboardKey(const DeveloperTools& d, const SDL_KeyboardEvent& key) {
    return d.isKey(key, ToolKey::Copy) ? 'c' : d.isKey(key, ToolKey::Cut) ? 'x' : d.isKey(key, ToolKey::Paste) ? 'v' : d.isKey(key, ToolKey::Duplicate) ? 'd' : 0;
}

} // namespace dev

// The keys as the frame starts, as the game's screens have them (Engine::moveKeys): the arrows (a controller's d-pad
// and left stick) go to the nearest stop that way, unless the one the keys are on uses that arrow itself (a list up and
// down, out at its ends; a box being typed in its caret's, and down for its list; the screen's preview all four, moving
// its chosen parts); with none that way, the arrow is left for the page (About scrolls). Tab and Shift+Tab go to the
// next stop in order, or back (into a box: typing in it, all of its words chosen). Moving shows the ring, and the
// pointer's light goes until it moves; one out of sight is scrolled into it. With the ring on, Enter and Space (A) press
// the one it's on (a box: typing in it). A box being typed in has its own keys (but not Tab, nor an arrow it doesn't
// use: that lets it go); so has a list open under a box.
void Developer::keyMoves() {
    DeveloperTools& d = tools;
    using Stop = DeveloperTools::KeyStop;
    if (d.clicked) d.keyRing = false;
    if (d.editLayout) return; // (moving page parts has the keys: drawPageParts)
    if (auto& p = d.popup; !p.id.empty()) { // (a list open: the arrows move in it, and Enter picks what they're on)
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) { // (typing in its box: the box has left and right, and Space)
            const int dx = d.isKey(key, ToolKey::Left, true) ? -1 : d.isKey(key, ToolKey::Right, true) ? 1 : 0;
            const int dy = d.isKey(key, ToolKey::Up, true) ? -1 : d.isKey(key, ToolKey::Down, true) ? 1 : 0;
            if (dy != 0 || (dx != 0 && d.focus != p.id)) {
                p.keyMove.x += dx;
                p.keyMove.y += dy;
                d.keysTakeOver();
                return true;
            }
            if (d.isKey(key, ToolKey::Press) && p.keyed && !(d.focus == p.id && key.key == SDLK_SPACE)) return p.keyPick = true;
            return false;
        });
        if (std::ranges::none_of(d.keys, [&](const SDL_KeyboardEvent& key) { return d.isKey(key, ToolKey::Next, true) || d.isKey(key, ToolKey::Previous, true); }))
            return;
        p.id.clear(); // (the keys on: it closes)
    }
    auto shows = [](const Stop& s) { // (where it is: what it's in cuts it off there)
        const SDL_FRect clip{static_cast<float>(s.clip.x), static_cast<float>(s.clip.y), static_cast<float>(s.clip.w), static_cast<float>(s.clip.h)};
        return s.box.w > 0.0f && s.box.h > 0.0f && SDL_HasRectIntersectionFloat(&s.box, &clip);
    };
    auto reachable = [&](const Stop& s) { return shows(s) || (s.scroll && s.box.w > 0.0f && s.box.h > 0.0f); }; // (or what it's in scrolls it into sight)
    auto holds = [](const SDL_FRect& a, const SDL_FRect& b) { return a.x <= b.x && a.y <= b.y && a.x + a.w >= b.x + b.w && a.y + a.h >= b.y + b.h; };
    auto goTo = [&](const Stop& to, bool typing) { // (Tab: into a box, typing in it; an arrow: the ring on it)
        d.keyAt = to.key;
        d.keysTakeOver();
        if (typing && to.kind == Stop::Kind::Type) d.focusOn(to.id, to.value, true);
        else d.focus.clear();
        if (to.scroll) { // (into sight, with a little room round it)
            const float room = 8.0f * d.k;
            if (to.box.y < to.view.y) *to.scroll -= to.view.y - to.box.y + room;
            else if (to.box.y + to.box.h > to.view.y + to.view.h) *to.scroll += to.box.y + to.box.h - (to.view.y + to.view.h) + room;
        }
    };
    std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
        const Stop* at = d.keysOn();
        if (const bool back = d.isKey(key, ToolKey::Previous, true); back || d.isKey(key, ToolKey::Next, true)) {
            if (d.keyStops.empty()) return true;
            const std::size_t n = d.keyStops.size(), was = at ? static_cast<std::size_t>(at - d.keyStops.data()) : n;
            goTo(d.keyStops[was == n ? (back ? n - 1 : 0) : (was + (back ? n - 1 : 1)) % n], true);
            return true;
        }
        const int dx = d.isKey(key, ToolKey::Left, true) ? -1 : d.isKey(key, ToolKey::Right, true) ? 1 : 0;
        const int dy = dx != 0 ? 0 : d.isKey(key, ToolKey::Up, true) ? -1 : d.isKey(key, ToolKey::Down, true) ? 1 : 0;
        if (dx != 0 || dy != 0) {
            const std::uint8_t arrow = dx < 0 ? DeveloperTools::kOwnLeft : dx > 0 ? DeveloperTools::kOwnRight : dy < 0 ? DeveloperTools::kOwnUp : DeveloperTools::kOwnDown;
            if (at && (at->own & arrow)) { // (its own)
                if (at->kind == Stop::Kind::List) {
                    d.keyStep = dy;
                    d.keyFold = dx;
                    d.keysTakeOver();
                    return true;
                }
                if (at->kind != Stop::Kind::Type) d.keysTakeOver(); // (a box's caret moves as it's typed in)
                return false;
            }
            if (!at) { // (nowhere to go on from: the first one in sight)
                const auto first = std::ranges::find_if(d.keyStops, shows);
                if (first == d.keyStops.end()) return false;
                goTo(*first, false);
                return true;
            }
            std::vector<const Stop*> those; // (any but one it's in, or one in it: a list's box being renamed)
            std::vector<SDL_FRect> boxes;
            for (const Stop& s : d.keyStops) {
                if (&s == at || !reachable(s) || holds(s.box, at->box) || holds(at->box, s.box)) continue;
                those.push_back(&s);
                boxes.push_back(s.box);
            }
            const auto to = nearestThatWay(at->box, boxes, dx, dy);
            if (!to) return false; // (none that way: the page's own, if it has a use for it)
            goTo(*those[*to], false);
            return true;
        }
        if (!at || !d.focus.empty() || !d.keyRing) return false; // (put there by the pointer: press as it was)
        if (d.isKey(key, ToolKey::Press)) {
            d.keyPress = at->key;
            return true;
        }
        return false;
    });
}

// The frame's stops, kept in the order they're met (when every page part was drawn: else last frame's stay), and a ring
// round the one the keys are on, where it shows.
void Developer::keyRing(DeveloperUi& ui) {
    DeveloperTools& d = tools;
    if (d.keyStopsWhole) {
        std::ranges::stable_sort(d.keyStopsNow, [](const DeveloperTools::KeyStop& a, const DeveloperTools::KeyStop& b) {
            return a.order.x != b.order.x ? a.order.x < b.order.x : a.order.y < b.order.y;
        });
        d.keyStops = std::move(d.keyStopsNow);
    }
    d.keyStopsNow.clear();
    d.keyStopsWhole = true;
    d.keyPress.clear();
    d.keyStep = d.keyFold = 0;
    d.pointerMoved = false;
    d.scrollers.clear();
    const auto at = std::ranges::find(d.keyStops, d.keyAt, &DeveloperTools::KeyStop::key);
    if (engine.screenReader()) { // (a screen reader's told the page when it's another, and what the keys are on)
        std::string words;
        auto add = [&](std::string_view w) {
            if (!w.empty()) words += (words.empty() ? "" : ". ") + std::string(w);
        };
        if (d.toldPage != d.page) add(pageName(*(d.toldPage = d.page)));
        if (at != d.keyStops.end() && (at->key != d.toldKey || at->words != d.toldWords)) {
            d.toldKey = at->key;
            add(d.toldWords = at->words);
        }
        if (!words.empty()) announce(words);
    }
    if (d.editLayout && d.layoutKeys.ringBox.w > 0.0f) { // (while page parts are moved: what of one the keys are on)
        const SDL_FRect& b = d.layoutKeys.ringBox;
        const float t = 2.0f * std::max(1.0f, std::round(ui.k));
        SDL_SetRenderClipRect(ui.r, nullptr);
        if (b.w <= 6.0f * ui.k || b.h <= 6.0f * ui.k) draw::rect(ui.r, b, kAccent);
        else {
            draw::rect(ui.r, {b.x, b.y, b.w, t}, kAccent);
            draw::rect(ui.r, {b.x, b.y + b.h - t, b.w, t}, kAccent);
            draw::rect(ui.r, {b.x, b.y + t, t, b.h - 2 * t}, kAccent);
            draw::rect(ui.r, {b.x + b.w - t, b.y + t, t, b.h - 2 * t}, kAccent);
        }
        return;
    }
    if (!d.keyRing || at == d.keyStops.end()) return;
    SDL_Rect clip = at->clip;
    SDL_SetRenderClipRect(ui.r, &clip);
    const float t = 2.0f * std::max(1.0f, std::round(ui.k)); // (inside its edge: it shows even where the room ends at it)
    const SDL_FRect& b = at->box;
    draw::rect(ui.r, {b.x, b.y, b.w, t}, kAccent);
    draw::rect(ui.r, {b.x, b.y + b.h - t, b.w, t}, kAccent);
    draw::rect(ui.r, {b.x, b.y + t, t, b.h - 2 * t}, kAccent);
    draw::rect(ui.r, {b.x + b.w - t, b.y + t, t, b.h - 2 * t}, kAccent);
    SDL_SetRenderClipRect(ui.r, nullptr);
}

} // namespace vn
