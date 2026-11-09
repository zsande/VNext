// Input: events from the mouse, keys, controllers and touch screens, who's in charge (the pointer or the keys), and
// the keys and a controller on the screens.
#include "Engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ranges>

namespace vn {

namespace {

bool isPress(const SDL_Event& e) {
    return e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat);
}

} // namespace

void Engine::rumble(float strength, Uint32 ms) {
    if (!settings_.vibration) return;
    const auto low = static_cast<Uint16>(std::clamp(strength, 0.0f, 1.0f) * 65535.0f);
    for (const GamepadPtr& pad : pads_) SDL_RumbleGamepad(pad.get(), low, static_cast<Uint16>(low / 2), ms); // (a phone's buzz: mostly low)
}

// The screens a control's move, press and scroll actions work on: an open menu on top that takes clicks (or one the
// story waits for), alone; else the title screen; else the story's, together: the choice, the textbox, the screens
// the story shows (down to one that takes every key) and the hud.
std::vector<ScreenView*> Engine::keyViews() {
    for (const auto& name : overlays_ | std::views::reverse)
        if (ScreenView* v = view(name); v && (v->takesClicks() || v->modal())) return {v};
    std::vector<ScreenView*> out;
    auto add = [&](ScreenView* v) { // (true: it takes every key, so none under it does)
        if (v && (v->takesClicks() || v->modal())) out.push_back(v);
        return v && v->modal();
    };
    if (mode_ == Mode::Title) add(view("title"));
    else if (mode_ == Mode::Game && game_) {
        if (game_->showingChoices() && !game_->choiceOnPhone()) add(view("choice"));
        if (game_->showingSay()) add(textbox());
        for (const auto& name : game_->shownScreens() | std::views::reverse)
            if (add(view(name))) return out;
        add(view("hud"));
    }
    return out;
}

// The move actions: a slider pointed at changes (left and right), else the nearest part that way on the screens the
// keys work on is pointed at (none pointed at yet: the first one); with none that way, what scrolls there scrolls.
void Engine::moveKeys(int dx, int dy) {
    const std::vector<ScreenView*> views = keyViews();
    ScreenView* from = nullptr;
    std::optional<SDL_FRect> at;
    for (ScreenView* v : views)
        if ((at = v->pointedRect())) {
            from = v;
            break;
        }
    if (from && dx != 0 && from->adjustPointed(dx)) return;
    if (!from) {
        for (ScreenView* v : views)
            if (v->pointFirst()) return;
    } else {
        ScreenView* best = nullptr;
        float nearest = 0.0f;
        for (ScreenView* v : views)
            if (const auto far = v->toward(*at, dx, dy); far && (!best || *far < nearest)) {
                best = v;
                nearest = *far;
            }
        if (best) {
            for (ScreenView* v : views)
                if (v != best) v->letGo();
            best->pointToward(*at, dx, dy);
            return;
        }
    }
    if (dy != 0)
        for (ScreenView* v : views)
            if (v->scrollBy(dy < 0 ? "up" : "down")) return;
}

bool Engine::idle(const Commands& actions) {
    if (actions.empty()) return false;
    const std::vector<ScreenView*> views = keyViews();
    for (const Command& c : actions) {
        const auto* ui = std::get_if<cmd::Ui>(&c);
        if (!ui) return false;
        const std::string& v = ui->verb;
        if (v == "tools") continue; // (the developer tools' key: in the game, its key goes on to the others)
        if (v.starts_with("move ") || v.starts_with("scroll ")) {
            if (std::ranges::any_of(views, &ScreenView::takesKeys)) return false;
        } else if (v == "press") {
            if (std::ranges::any_of(views, &ScreenView::keysPoint)) return false;
        } else {
            return false;
        }
    }
    return true;
}

// The right stick on the screens the keys work on: as the wheel turns, where the keys are (their ring), else where the
// pointer is.
void Engine::stickScroll(float up, float across) {
    const std::vector<ScreenView*> views = keyViews();
    for (ScreenView* v : views)
        if (const auto at = v->keysPoint() ? v->pointedRect() : std::nullopt) {
            v->wheelAt(centre(*at), up, across);
            return;
        }
    for (ScreenView* v : views)
        if (v->wheelAt(v->pointer(), up, across)) return;
}

void Engine::pointedIn(const ScreenView& view) { // (on the screens the keys work on with it: one under a menu keeps its own, for when that closes)
    for (ScreenView* v : keyViews())
        if (v != &view) v->letGo();
}

// Fingers: a tap clicks, a quick swipe, a long press or a two-finger tap is a gesture (for
// controls), and dragging scrolls menus.
SDL_AppResult Engine::touch(const SDL_Event& e) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_.get(), &ww, &wh);
    const float x = e.tfinger.x * static_cast<float>(ww), y = e.tfinger.y * static_cast<float>(wh);
    auto mouse = [&](Uint32 type) {
        SDL_Event m{};
        m.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            m.motion.windowID = SDL_GetWindowID(window_.get());
            m.motion.x = x;
            m.motion.y = y;
        } else if (type == SDL_EVENT_MOUSE_WHEEL) {
            m.wheel.windowID = SDL_GetWindowID(window_.get());
            m.wheel.mouse_x = x;
            m.wheel.mouse_y = y;
        } else {
            m.button.windowID = SDL_GetWindowID(window_.get());
            m.button.button = SDL_BUTTON_LEFT;
            m.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            m.button.clicks = 1;
            m.button.x = x;
            m.button.y = y;
        }
        return m;
    };
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN: {
        if (touch_.down) { // another finger
            touch_.second = true;
            return SDL_APP_CONTINUE;
        }
        touch_ = {.finger = e.tfinger.fingerID, .down = true, .x = x, .y = y, .startX = x, .startY = y, .scrolledY = y, .start = SDL_GetTicks()};
        SDL_Event motion = mouse(SDL_EVENT_MOUSE_MOTION);
        return event(motion);
    }
    case SDL_EVENT_FINGER_MOTION:
        if (!touch_.down || e.tfinger.fingerID != touch_.finger) return SDL_APP_CONTINUE;
        touch_.x = x;
        touch_.y = y;
        if (std::hypot(x - touch_.startX, y - touch_.startY) > static_cast<float>(std::min(ww, wh)) * 0.03f) touch_.moved = true;
        if (touch_.moved && !overlays_.empty()) // dragging scrolls lists in menus, a wheel step each 40 points
            for (const float step = 40.0f; std::abs(y - touch_.scrolledY) >= step; touch_.scrolledY += y > touch_.scrolledY ? step : -step) {
                SDL_Event wheel = mouse(SDL_EVENT_MOUSE_WHEEL);
                wheel.wheel.y = y > touch_.scrolledY ? 1.0f : -1.0f;
                event(wheel);
            }
        return SDL_APP_CONTINUE;
    default: { // up or cancelled
        if (!touch_.down || e.tfinger.fingerID != touch_.finger) return SDL_APP_CONTINUE;
        const Touch t = std::exchange(touch_, {});
        if (e.type == SDL_EVENT_FINGER_CANCELED || t.longPressed) return SDL_APP_CONTINUE;
        const Uint64 held = SDL_GetTicks() - t.start;
        if (t.second && !t.moved && held < 600) return gesture(KeyChord::Gesture::TwoFingerTap);
        if (!t.moved && held < 600) { // a tap: a click where it was
            SDL_Event down = mouse(SDL_EVENT_MOUSE_BUTTON_DOWN), up = mouse(SDL_EVENT_MOUSE_BUTTON_UP);
            event(down);
            return event(up);
        }
        const float dx = x - t.startX, dy = y - t.startY;
        if (!t.moved || held > 700 || !overlays_.empty()) return SDL_APP_CONTINUE;
        if (std::abs(dx) > std::abs(dy) && std::abs(dx) > static_cast<float>(ww) * 0.12f)
            return gesture(dx < 0 ? KeyChord::Gesture::SwipeLeft : KeyChord::Gesture::SwipeRight);
        if (std::abs(dy) > static_cast<float>(wh) * 0.12f) return gesture(dy < 0 ? KeyChord::Gesture::SwipeUp : KeyChord::Gesture::SwipeDown);
        return SDL_APP_CONTINUE;
    }
    }
}

SDL_AppResult Engine::gesture(KeyChord::Gesture g) {
    SDL_Event u{};
    u.type = gestureEventType();
    u.user.code = static_cast<Sint32>(g);
    u.user.windowID = SDL_GetWindowID(window_.get());
    return event(u);
}

// Sticks and triggers act as buttons, for controls' keys and the developer tools' alike: the left stick as the d-pad
// (pad_up...: the way it's pushed most, once pushed most of the way; let go as it comes back), the triggers as pad_lt
// and pad_rt once pulled most of the way. The right stick scrolls (iterate). Either stick pushed is the controller in use.
SDL_AppResult Engine::padAxis(const SDL_Event& e) {
    const int axis = e.gaxis.axis;
    const float v = static_cast<float>(e.gaxis.value) / 32767.0f;
    auto button = [&](int which, bool down) { // (as the controller's own button would come)
        SDL_Event b{};
        b.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        b.gbutton.which = e.gaxis.which;
        b.gbutton.button = static_cast<Uint8>(which);
        b.gbutton.down = down;
        return event(b);
    };
    switch (axis) {
    case SDL_GAMEPAD_AXIS_RIGHTX:
    case SDL_GAMEPAD_AXIS_RIGHTY:
        (axis == SDL_GAMEPAD_AXIS_RIGHTX ? rightStick_.x : rightStick_.y) = v;
        if (std::abs(v) > kStickScrolls) padInUse_ = true; // (as the wheel: who's in charge stays)
        return SDL_APP_CONTINUE;
    case SDL_GAMEPAD_AXIS_LEFTX:
    case SDL_GAMEPAD_AXIS_LEFTY: {
        (axis == SDL_GAMEPAD_AXIS_LEFTX ? leftStick_.x : leftStick_.y) = v;
        const float x = leftStick_.x, y = leftStick_.y, most = std::max(std::abs(x), std::abs(y));
        std::optional<int> way = stickWay_;
        if (most > kStickPushed) way = std::abs(x) > std::abs(y) ? (x > 0 ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT : SDL_GAMEPAD_BUTTON_DPAD_LEFT)
                                                                : (y > 0 ? SDL_GAMEPAD_BUTTON_DPAD_DOWN : SDL_GAMEPAD_BUTTON_DPAD_UP);
        else if (most < kStickBack) way.reset();
        if (way == stickWay_) return SDL_APP_CONTINUE;
        const std::optional<int> was = std::exchange(stickWay_, way);
        if (was && button(*was, false) != SDL_APP_CONTINUE) return SDL_APP_SUCCESS;
        return way ? button(*way, true) : SDL_APP_CONTINUE;
    }
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: {
        bool& pulled = pulled_[static_cast<std::size_t>(axis)];
        const bool now = v > kStickPushed ? true : v < kStickBack ? false : pulled;
        if (now == pulled) return SDL_APP_CONTINUE;
        pulled = now;
        return button(axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? KeyChord::kLeftTrigger : KeyChord::kRightTrigger, now);
    }
    default:
        return SDL_APP_CONTINUE;
    }
}

SDL_AppResult Engine::event(SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        return touch(e);
    case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_Gamepad* pad = SDL_OpenGamepad(e.gdevice.which)) pads_.emplace_back(pad);
        return SDL_APP_CONTINUE;
    case SDL_EVENT_GAMEPAD_REMOVED:
        std::erase_if(pads_, [&](const GamepadPtr& pad) { return SDL_GetGamepadID(pad.get()) == e.gdevice.which; }); // (closed as it goes)
        return SDL_APP_CONTINUE;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        return padAxis(e);
    default:
        break;
    }
    useView(developer_.isOpen()); // (clicks are read in the view they're for)
    SDL_ConvertEventToRenderCoordinates(renderer_.get(), &e);
    if (e.type == SDL_EVENT_MOUSE_MOTION) ++pointerMoves_;
    if (isPress(e) || e.type == SDL_EVENT_MOUSE_WHEEL) padInUse_ = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN; // (what the player plays with now)
    // Who's in charge: the pointer once it moves or clicks, the keys once one's pressed (a controller's too); the wheel
    // changes neither. A real keyboard's key: there's one (the on-screen one isn't wanted then).
    if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) keysInCharge_ = false;
    else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) keysInCharge_ = true;
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_TEXT_INPUT) keyboardUsed_ = true;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP && padRepeat_ && padRepeat_->button == e.gbutton.button) padRepeat_.reset(); // (let go: it stops going again)
    switch (e.type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        if (!toolsView_) applyGameView(); // (whole pixels may fit now, or not)
        updatePixelScale();
        refreshWindowSizes();
        return SDL_APP_CONTINUE;
    case SDL_EVENT_WINDOW_RESIZED: // remember sizes the player drags the window to
        if (!(SDL_GetWindowFlags(window_.get()) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))) {
            settings_.windowWidth = applied_.windowWidth = e.window.data1;
            settings_.windowHeight = applied_.windowHeight = e.window.data2;
        }
        return SDL_APP_CONTINUE;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: // also covers the OS's own fullscreen button
        settings_.fullscreen = applied_.fullscreen = e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        return SDL_APP_CONTINUE;
    default:
        break;
    }
    auto done = [this] { return quit_ ? SDL_APP_SUCCESS : SDL_APP_CONTINUE; };
    auto pressed = [&](std::size_t i) { // (a move or a scroll goes again while its key's held)
        const bool repeats = script_.controls[i].repeats();
        return std::ranges::any_of(controlKeys_[i], [&](const KeyChord& k) { return k.matches(e, repeats); });
    };
    auto held = [&](std::size_t i) { // (a controller's: again after a moment, ten times a second, until it's let go)
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && script_.controls[i].repeats()) padRepeat_ = PadRepeat{e.gbutton.button, i, toolsTime_ + kRepeatAfter};
    };

    // Developer mode: F10 or a controller's home button (the tools' Play: control tools_play) opens and closes the developer
    // tools, which take every key, click and controller button while open.
    if (developerMode() && developer_.toolKey(e, ToolKey::Play)) {
        if (noGame_) return done(); // (without a game there's nothing else to show)
        if (developer_.isOpen()) developer_.backToGame();
        else developer_.openDeveloper();
        return done();
    }
    if (developer_.isOpen()) {
        developer_.developerEvent(e);
        return done();
    }

    if (rebinding_ && captureKey(e)) return done(); // a control part is waiting for a key

    // The on-screen keyboard is for playing without a keyboard: a real one's key puts it away (Esc only that: another does
    // what it does too), and so does a controller's B on top of it; one of its own buttons with that key presses that.
    if (const bool real = e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat;
        keyboardFor_ && (real || (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST))) {
        const bool top = !overlays_.empty() && overlays_.back() == "keyboard";
        if (ScreenView* k = top ? view("keyboard") : nullptr; k && k->hasKey(e)) {
            k->handleEvent(e);
            return done();
        }
        if (real || top) {
            closeKeyboard();
            if (top && (!real || e.key.key == SDLK_ESCAPE)) return done();
        }
    }

    // Controls with in_menus=yes work on every screen (moving and pressing on it, fullscreen, screenshot...). One that
    // would do nothing now (moving or pressing with nothing there to) lets its key go on: to the next line's, a button's...
    auto works = [&](std::size_t i) { return !script_.controls[i].condition || script_.controls[i].condition->test(lookup()); }; // (when=)
    for (std::size_t i = 0; i < script_.controls.size(); ++i)
        if (script_.controls[i].inMenus && pressed(i) && works(i) && !idle(script_.controls[i].actions)) {
            run(script_.controls[i].actions);
            held(i);
            return done();
        }

    if (mode_ == Mode::Error) return done();

    // A button pressed on a screen is clicked (or not) when the mouse comes up: that screen takes it.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
        for (auto& [name, v] : views_)
            if (v.pressing()) {
                v.handleEvent(e);
                return done();
            }

    // Open menus, top first (one that lets the story go on keeps the clicks on what it shows: inside the phone
    // they aren't the story's, but while the story's on it, in a chat scene, a call that came or a chat= or call=
    // scene's lines, they go on with it, a tap on its chat too). Copy the list: buttons may open or close screens.
    const std::vector<std::string> open = overlays_;
    const bool story = playing() && game_->storyOnPhone();
    for (const auto& name : open | std::views::reverse)
        if (ScreenView* v = view(name); v && (v->handleEvent(e) || v->modal() || (!story && v->catches(e)))) {
            if (v->takeTapped() && story) { // (as a click outside it would)
                if (game_->skipMode()) game_->toggleSkip();
                else game_->nextLine();
            }
            return done();
        }

    if (mode_ == Mode::Title) {
        if (ScreenView* title = view("title")) title->handleEvent(e);
        return done();
    }
    if (game_->uiHidden()) {
        if (isPress(e)) game_->setUiHidden(false); // any key or click brings the UI back
        return done();
    }
    if (ScreenView* v = view("choice"); v && game_->showingChoices() && !game_->choiceOnPhone() && v->handleEvent(e)) return done();
    if (ScreenView* v = textbox(); v && game_->showingSay() && v->handleEvent(e)) return done();
    const std::vector<std::string> shown = game_->shownScreens();
    for (const auto& name : shown | std::views::reverse)
        if (ScreenView* v = view(name); v && (v->handleEvent(e) || v->modal())) return done();
    if (ScreenView* v = view("hud"); v && v->handleEvent(e)) return done();
    for (std::size_t i = 0; i < script_.controls.size(); ++i)
        if (!script_.controls[i].inMenus && pressed(i) && works(i) && !idle(script_.controls[i].actions)) {
            run(script_.controls[i].actions);
            held(i);
            break;
        }
    return done();
}

} // namespace vn
