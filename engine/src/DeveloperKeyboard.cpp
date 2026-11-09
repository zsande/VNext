// The developer tools' on-screen keyboard, for typing into a box without a keyboard.
#include "DeveloperUi.hpp"

#include <cmath>
#include <cstdlib>

namespace vn {

namespace {

// The on-screen keyboard's rows as a US keyboard has them, and with Shift; then its last row: Shift, Space, the caret back and
// on, Delete and Done, each so many keys wide (thirteen, as the top rows).
constexpr std::array<std::string_view, 4> kKeyRows{"`1234567890-=", "qwertyuiop[]\\", "asdfghjkl;'", "zxcvbnm,./"};
constexpr std::array<std::string_view, 4> kShiftedRows{"~!@#$%^&*()_+", "QWERTYUIOP{}|", "ASDFGHJKL:\"", "ZXCVBNM<>?"};
enum ScreenKey : int { kShiftKey, kSpaceKey, kCaretBackKey, kCaretOnKey, kDeleteKey, kDoneKey };
constexpr std::array<int, 6> kLastRowWidths{2, 5, 1, 1, 2, 2};
constexpr int kKeysAcross = 13;

int rowLength(int row) { return row < 4 ? static_cast<int>(kKeyRows[static_cast<std::size_t>(row)].size()) : static_cast<int>(kLastRowWidths.size()); }

// A key's middle, across, in keys from the left (the shorter rows in the middle).
float keyMiddle(int row, int col) {
    if (row < 4) return static_cast<float>(kKeysAcross - rowLength(row)) / 2.0f + static_cast<float>(col) + 0.5f;
    float x = 0.0f;
    for (int i = 0; i < col; ++i) x += static_cast<float>(kLastRowWidths[static_cast<std::size_t>(i)]);
    return x + static_cast<float>(kLastRowWidths[static_cast<std::size_t>(col)]) / 2.0f;
}

std::string keyLabel(int row, int col, bool shift) {
    if (row < 4) return std::string(1, (shift ? kShiftedRows : kKeyRows)[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)]);
    switch (col) {
    case kShiftKey: return std::string("Shift"_tr);
    case kSpaceKey: return std::string("Space"_tr);
    case kCaretBackKey: return "←";
    case kCaretOnKey: return "→";
    case kDeleteKey: return std::string("Delete"_tr);
    default: return std::string("Done"_tr);
    }
}

} // namespace

void Developer::openScreenKeys(bool all) {
    DeveloperTools& d = tools;
    DeveloperTools::ScreenKeys& s = d.screenKeys;
    s = {.open = true, .shift = false, .box = d.focus, .row = 1, .col = 0, .area = {}, .keys = {}};
    if (all) { // (as Tab into a box has it)
        d.anchor = 0;
        d.caret = d.buffer.size();
        d.anchorFocus = d.focusCount;
    }
    if (engine.screenReader()) announce(keyLabel(s.row, s.col, s.shift));
}

// A controller's button while a box is typed into: A brings the keyboard up; while it's up, the d-pad moves on it, A presses
// the key, X deletes, Y types a space, the shoulders move the caret, the left trigger is Shift, Start is Done and B lets the
// box go (true: it took the button).
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
    case SDL_GAMEPAD_BUTTON_SOUTH: screenKeysPress(s.row, s.col); return true;
    case SDL_GAMEPAD_BUTTON_WEST: screenKeysPress(4, kDeleteKey); return true;
    case SDL_GAMEPAD_BUTTON_NORTH: screenKeysPress(4, kSpaceKey); return true;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: screenKeysPress(4, kCaretBackKey), held(ToolKey::Previous); return true;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: screenKeysPress(4, kCaretOnKey), held(ToolKey::Next); return true;
    case SDL_GAMEPAD_BUTTON_START: screenKeysPress(4, kDoneKey); return true;
    case KeyChord::kLeftTrigger: screenKeysPress(4, kShiftKey); return true;
    case SDL_GAMEPAD_BUTTON_EAST: s.open = false; return true; // (it goes; the box is still typed into: B again lets it go)
    default: return false;
    }
}

void Developer::screenKeysMove(int dx, int dy) {
    DeveloperTools::ScreenKeys& s = tools.screenKeys;
    if (dy != 0) {
        const int row = std::clamp(s.row + dy, 0, 4);
        if (row != s.row) { // (to the key nearest across)
            const float x = keyMiddle(s.row, s.col);
            int best = 0;
            for (int c = 1; c < rowLength(row); ++c)
                if (std::abs(keyMiddle(row, c) - x) < std::abs(keyMiddle(row, best) - x)) best = c;
            s.row = row;
            s.col = best;
        }
    }
    if (dx != 0) s.col = std::clamp(s.col + dx, 0, rowLength(s.row) - 1);
    if (engine.screenReader()) announce(keyLabel(s.row, s.col, s.shift));
}

void Developer::screenKeysPress(int row, int col) {
    DeveloperTools& d = tools;
    DeveloperTools::ScreenKeys& s = d.screenKeys;
    auto key = [&](SDL_Keycode code) { // (as the box's own key: the caret's moves, deleting, Enter)
        SDL_KeyboardEvent e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key = code;
        e.scancode = SDL_GetScancodeFromKey(code, nullptr);
        e.which = kScreenKeyboard;
        e.down = true;
        d.keys.push_back(e);
    };
    if (row < 4) {
        d.typed += (s.shift ? kShiftedRows : kKeyRows)[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
        s.shift = false; // (one letter at a time, as a phone's)
        return;
    }
    switch (col) {
    case kShiftKey: s.shift = !s.shift; break;
    case kSpaceKey: d.typed += ' '; break;
    case kCaretBackKey: key(SDLK_LEFT); break;
    case kCaretOnKey: key(SDLK_RIGHT); break;
    case kDeleteKey: key(SDLK_BACKSPACE); break;
    default: key(SDLK_RETURN); break; // (Done: Enter, which does what it does in the box and lets it go)
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
    for (const auto& [box, at] : s.keys)
        if (pointInRect(d.mouseX, d.mouseY, box)) {
            s.row = at.first;
            s.col = at.second;
            screenKeysPress(at.first, at.second);
        }
}

// The keyboard, over the rest: under the box typed into, or over it when that's low in the window. The key a controller is
// on ringed, the one the pointer's on lit, Shift lit while it's on.
void Developer::drawScreenKeys(DeveloperUi& ui) {
    DeveloperTools& d = tools;
    DeveloperTools::ScreenKeys& s = d.screenKeys;
    s.keys.clear();
    if (!s.open) {
        s.area = {};
        return;
    }
    const float k = ui.k, gap = std::round(6.0f * k), edge = std::round(10.0f * k);
    const float u = std::floor(std::min(46.0f * k, (d.size.x * 0.94f - 2.0f * edge - (kKeysAcross - 1) * gap) / kKeysAcross)), keyH = std::round(u * 0.85f);
    const float w = kKeysAcross * u + (kKeysAcross - 1) * gap + 2.0f * edge, h = 5.0f * keyH + 4.0f * gap + 2.0f * edge;
    float boxMiddle = d.size.y * 0.3f;
    if (const auto at = std::ranges::find(d.keyStops, s.box, &DeveloperTools::KeyStop::id); at != d.keyStops.end()) boxMiddle = at->box.y + at->box.h / 2.0f;
    s.area = {std::round((d.size.x - w) / 2.0f), boxMiddle > d.size.y * 0.5f ? std::round(8.0f * k) : std::round(d.size.y - h - 8.0f * k), w, h};
    draw::rect(ui.r, s.area, kPanel);
    draw::outline(ui.r, s.area, kEdge);
    const bool pad = d.keyRing; // (the key the controller's on shows while the keys are in charge)
    for (int row = 0; row < 5; ++row) {
        float x = s.area.x + edge + (row < 4 ? static_cast<float>(kKeysAcross - rowLength(row)) / 2.0f * (u + gap) : 0.0f);
        const float y = s.area.y + edge + static_cast<float>(row) * (keyH + gap);
        for (int col = 0; col < rowLength(row); ++col) {
            const float span = row < 4 ? 1.0f : static_cast<float>(kLastRowWidths[static_cast<std::size_t>(col)]);
            const SDL_FRect box{std::round(x), y, std::round(span * u + (span - 1.0f) * gap), keyH};
            const bool on = pad && s.row == row && s.col == col, pointed = pointInRect(d.mouseX, d.mouseY, box);
            draw::rect(ui.r, box, row == 4 && col == kShiftKey && s.shift ? kChosen : on || pointed ? kButtonHover : kButton);
            draw::outline(ui.r, box, on ? kAccent : kEdge);
            if (on) draw::outline(ui.r, {box.x + 1, box.y + 1, box.w - 2, box.h - 2}, kAccent);
            ui.textIn(keyLabel(row, col, s.shift), box, layout::lineText, theme::text);
            s.keys.push_back({box, {row, col}});
            x += box.w + gap;
        }
    }
}

} // namespace vn
