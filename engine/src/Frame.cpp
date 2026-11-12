// Each frame: the game's clock, the story going on, what's open, and everything drawn; the window's view of the
// game; and the screen with the scripts' mistakes.
#include "Engine.hpp"

#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace vn {

void Engine::updatePixelScale() {
    // Text is rasterised at the size it's shown at: the game's as its view scales the canvas (Settings'
    // scaling), the developer tools' as sharp as the window's pixels.
    if (!renderer_) return;
    const bool tools = toolsView_;
    useView(false);
    if (SDL_FRect shown{}; SDL_GetRenderLogicalPresentationRect(renderer_.get(), &shown) && shown.w > 0 && shown.h > 0)
        fonts_.setPixelScale(std::max(shown.w / canvas_.w, shown.h / canvas_.h));
    useView(tools);
    toolsFonts_.setPixelScale(SDL_GetWindowPixelDensity(window_.get()));
}

void Engine::applyGameView() {
    static constexpr std::array modes{SDL_LOGICAL_PRESENTATION_LETTERBOX, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE,
                                      SDL_LOGICAL_PRESENTATION_STRETCH, SDL_LOGICAL_PRESENTATION_OVERSCAN};
    SDL_RendererLogicalPresentation mode = modes[static_cast<std::size_t>(settings_.scaling)];
    // (whole pixels in a window with fewer pixels than the game: SDL would show it at its own size, cut off; it fits instead)
    if (int w = 0, h = 0; mode == SDL_LOGICAL_PRESENTATION_INTEGER_SCALE && SDL_GetRenderOutputSize(renderer_.get(), &w, &h) &&
                          (static_cast<float>(w) < canvas_.w || static_cast<float>(h) < canvas_.h))
        mode = SDL_LOGICAL_PRESENTATION_LETTERBOX;
    SDL_SetRenderLogicalPresentation(renderer_.get(), static_cast<int>(canvas_.w), static_cast<int>(canvas_.h), mode);
    toolsView_ = false;
}

void Engine::useView(bool tools) {
    if (!renderer_) return;
    if (!tools) {
        if (toolsView_) applyGameView();
        return;
    }
    SDL_Point size{};
    SDL_GetWindowSize(window_.get(), &size.x, &size.y);
    if (toolsView_ && size.x == toolsViewSize_.x && size.y == toolsViewSize_.y) return;
    toolsView_ = true;
    toolsViewSize_ = size;
    SDL_SetRenderLogicalPresentation(renderer_.get(), std::max(size.x, 1), std::max(size.y, 1), SDL_LOGICAL_PRESENTATION_LETTERBOX); // (as big as it: no bars)
}

void Engine::refreshWindowSizes() {
    static constexpr std::array factors{0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f};
    SDL_Rect usable{0, 0, static_cast<int>(canvas_.w), static_cast<int>(canvas_.h)};
    SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_.get()), &usable);
    windowSizes_.clear();
    for (float f : factors) {
        const SDL_Point p{static_cast<int>(std::lround(canvas_.w * f)), static_cast<int>(std::lround(canvas_.h * f))};
        if (p.x >= 320 && ((p.x <= usable.w && p.y <= usable.h) || f == 1.0f)) windowSizes_.push_back(p);
    }
}

void Engine::showErrors(const std::vector<std::string>& lines) {
    std::string text;
    for (std::size_t i = 0; i < lines.size() && i < 16; ++i) text += lines[i] + '\n';
    if (lines.size() > 16) text += "... and {} more (see log.txt)"_tr(lines.size() - 16) + '\n';
    errorText_.set(fonts_.get("", 18.0f * lookScale(canvas_.w, canvas_.h)), std::move(text), canvas_.w - 120);
    closeAll();
    mode_ = Mode::Error;
}

void Engine::screenshot(const std::string& path) {
    // Read the whole window: with "Fill" scaling the logical viewport extends past its edges.
    SDL_Renderer* r = renderer_.get();
    int w = 0, h = 0;
    SDL_RendererLogicalPresentation mode{};
    SDL_GetRenderLogicalPresentation(r, &w, &h, &mode);
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SurfacePtr shot{SDL_RenderReadPixels(r, nullptr)};
    SDL_SetRenderLogicalPresentation(r, w, h, mode);
    if (shot && IMG_SavePNG(shot.get(), path.c_str())) logInfo("Saved the screenshot as '{}'.", path);
    else logWarn("Can't save the screenshot: {}.", SDL_GetError());
}

void Engine::drawGame(bool thumbnail) {
    thumbnail_ = thumbnail;
    SDL_Renderer* r = renderer_.get();
    game_->draw(fonts_.get("default", 18));
    if (!game_->uiHidden()) {
        for (const auto& name : game_->shownScreens())
            if (ScreenView* v = view(name)) v->draw(r);
        for (const std::string& box : game_->boxes()) // every textbox with a line, the line on screen's on top
            if (ScreenView* v = view(box)) v->draw(r);
        if (ScreenView* v = view("choice"); v && game_->showingChoices() && !game_->choiceOnPhone()) v->draw(r); // (a chat's: on the phone)
        if (ScreenView* v = view("hud"); v && !thumbnail) v->draw(r);
    }
    if (ScreenView* v = view("captions"); v && !thumbnail && settings_.captions && game_->captioned() && !std::ranges::contains(overlays_, std::string("captions")))
        v->draw(r); // (over a movie too; menus over them. Opened by the game, it's one of them)
    if (thumbnail) // (the phone, say, as it was before the menu opened over it; not a notice that goes by itself)
        for (const auto& name : overlays_) {
            if (stopsStory(name)) break;
            if (ScreenView* v = view(name); v && v->def().closeAfter <= 0.0f) v->draw(r);
        }
    thumbnail_ = false;
}

// In developer mode the window says so, and how to open the developer tools, or with them open, the page
// they show and how to go back to the game (without a game, the tools are all there is).
std::string Engine::windowTitle() const {
    if (noGame_) return "VNext: developer mode"_tr;
    if (!gameSettings().developer) return gameSettings().title;
    if (developer_.isOpen()) return "{}: {}  (developer mode: F10 for the game)"_tr(gameSettings().title, developer_.shownPageName());
    return "{}  (developer mode: F10 for the developer tools)"_tr(gameSettings().title);
}

void Engine::showTitle() {
    if (std::string title = windowTitle(); window_ && title != shownTitle_) {
        SDL_SetWindowTitle(window_.get(), title.c_str());
        shownTitle_ = std::move(title);
    }
}

void Engine::drawErrors() {
    SDL_Renderer* r = renderer_.get();
    draw::rect(r, {0, 0, canvas_.w, canvas_.h}, {40, 12, 18, 255});
    const float k = lookScale(canvas_.w, canvas_.h); // keep the layout proportional on bigger canvases (and taller ones)
    // (the game itself can't be opened: no script to fix)
    errorTitle_.set(fonts_.get("", 24 * k), cantOpen_ ? std::string_view("The game can't be opened"_tr) : std::string_view("Something in the script needs fixing"_tr));
    errorTitle_.draw(r, 60 * k, 50 * k, {255, 140, 150, 255});
    errorText_.draw(r, 60 * k, 100 * k, theme::text, TextBlock::kAll, {.on = false});
    errorHint_.set(fonts_.get("", 18 * k), cantOpen_                     ? std::string_view("Check that the game folder or game.vnext is there, or get the game again"_tr)
                                           : gameSettings().developer ? std::string_view("Fix the lines above in the developer tools (F10)"_tr)
                                                                      : std::string_view("Fix the lines above, save, then start the game again"_tr));
    errorHint_.draw(r, 60 * k, canvas_.h - 50 * k, theme::dim);
}

SDL_AppResult Engine::iterate() {
    // While the developer tools are open, the first click on the window, with another program in front
    // (picking files in Finder...), clicks what it's on too: it doesn't only bring the window forward.
    if (developer_.isOpen() != clickThrough_) {
        clickThrough_ = developer_.isOpen();
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, clickThrough_ ? "1" : "0");
    }
    if (std::exchange(reloadRequested_, false)) {
        developer_.dropPreviews(); // (its game uses the images let go of)
        reload();
        developer_.reloaded();
    }
    developer_.updateDeveloper();
    showTitle();
    const Uint64 now = SDL_GetTicksNS();
    const float frame = std::min(0.1f, static_cast<float>(now - lastTicks_) / 1e9f);
    lastTicks_ = now;
    toolsTime_ += frame;
    // A controller's move held goes again (on the game's screens: the developer tools have their own), and the right stick
    // turns the wheel, as far as it's pushed.
    if (padRepeat_ && (developer_.isOpen() || padRepeat_->control >= script_.controls.size())) padRepeat_.reset();
    if (padRepeat_ && toolsTime_ >= padRepeat_->next) {
        padRepeat_->next = toolsTime_ + kRepeatEvery;
        if (const Control& c = script_.controls[padRepeat_->control]; (!c.condition || c.condition->test(lookup())) && !idle(c.actions)) run(c.actions);
    }
    if (const SDL_FPoint s = rightStick_; std::abs(s.x) > kStickScrolls || std::abs(s.y) > kStickScrolls) {
        const float up = std::abs(s.y) > kStickScrolls ? -s.y * kStickTurns * frame : 0.0f, across = std::abs(s.x) > kStickScrolls ? s.x * kStickTurns * frame : 0.0f;
        if (developer_.isOpen()) developer_.stickWheel(up, across);
        else stickScroll(up, across);
    }
    // While the developer tools are open over a game (F10), it stands still: its clock, its timers,
    // its sounds and movies, and the story.
    if (const bool paused = developer_.isOpen() && !noGame_ && !waiting_; paused != paused_) {
        paused_ = paused;
        sound_.holdGame(paused);
    }
    const float dt = paused_ ? 0.0f : frame;
    time_ += dt;
    sound_.update(dt);
    std::vector<ScreenView::Pressed> pressed; // (buttons whose click animations are over: what they do; and what chosen ones set)
    for (auto& [name, v] : views_) {
        if (auto did = v.takePressed()) pressed.push_back(std::move(*did));
        if (Commands kept = v.takeChosen(); !kept.empty()) pressed.push_back({.actions = std::move(kept)});
    }
    for (const ScreenView::Pressed& p : pressed) run(p.actions, p.confirm, p.item);

    assets_->beginFrame();
    ++frameNumber_;
    if (auto wanted = std::exchange(keyboardWanted_, std::nullopt); wanted && view("keyboard")) { // (the on-screen keyboard a box asked for: over it)
        if (ScreenView* box = view(wanted->first.screen)) {
            const bool again = keyboardFor_ && keyboardFor_->screen == wanted->first.screen && keyboardFor_->element == wanted->first.element;
            if (!again) {
                wanted->first.shift = box->boxText(wanted->first.element).empty(); // (a name starts with a capital)
                keyboardFor_ = wanted->first;
            }
            if (overlays_.empty() || overlays_.back() != "keyboard") {
                openNow("keyboard", {}, {});
                if (ScreenView* k = view("keyboard"); k && wanted->second) k->ringWhenDrawn(ScreenView::Ring::FirstLetter); // (by the keys: the ring on it at once)
            }
        }
    }
    // A menu covers the story and stops it (one with story=goes_on, like the phone, lets it go on).
    auto stops = [&](const std::string& name) { return stopsStory(name); };
    const bool covered = mode_ == Mode::Game && (developer_.isOpen() || std::ranges::any_of(overlays_, stops));
    if (covered != covered_) {
        covered_ = covered;
        game_->setCovered(covered); // the line's voice and the movie wait under menus
        if (!covered) sound_.stopReplay(); // a voice heard again from the history stops with it
    }
    if (touch_.down && !touch_.moved && !touch_.second && !touch_.longPressed && SDL_GetTicks() - touch_.start >= 600) {
        touch_.longPressed = true; // a finger held still: a long press
        gesture(KeyChord::Gesture::LongPress);
    }
    if (mode_ == Mode::Game) {
        // Messages come and calls ring as the story goes on, or with the phone open over a menu; a menu over them stops them.
        const auto phone = std::ranges::find(overlays_, std::string("phone"));
        const bool phoneGoesOn = !developer_.isOpen() && std::none_of(phone == overlays_.end() ? overlays_.begin() : phone + 1, overlays_.end(), stops);
        if (phoneGoesOn) game_->updatePhone(dt);
        if (const auto& call = game_->call(); phoneGoesOn && call && !call->outgoing && !call->talking) { // a call coming: controllers buzz while it rings
            if ((ringPulse_ -= dt) <= 0.0f) {
                rumble(0.6f, 500);
                ringPulse_ = 1.5f;
            }
        } else {
            ringPulse_ = 0.0f;
        }
        // A vibration effect (effect vibration): the controllers rumble as strongly as it is now while the story is on, not
        // under a menu or the developer tools; asked again every fifth of a second, a little longer each time.
        const float shake = covered ? 0.0f : game_->vibration();
        vibrateFor_ -= dt;
        if (std::abs(shake - vibrating_) > 0.02f || (shake > 0.0f && vibrateFor_ <= 0.0f)) {
            rumble(shake, shake > 0.0f ? 400 : 0);
            vibrating_ = shake;
            vibrateFor_ = 0.2f;
        }
        if (!game_->hasPhone()) // (setting has_phone's condition false, or flat): screens showing it close
            for (std::size_t i = 0; i < overlays_.size(); ++i)
                if (showsPhone(overlays_[i])) {
                    while (overlays_.size() > i) close(false);
                    break;
                }
        // Loads one of the images the story is about to show, so it doesn't stop to load it then.
        for (const auto& [name, looks] : game_->upcomingImages(gameSettings().loadAhead))
            if (assets_->preload(name, looks)) break;
        if (!covered) {
            game_->update(dt);
            developer_.recordReached();
            if (game_->takeAutosaveRequest() && !opening_) autosave();
        }
        if (game_->stoppedThere() && !developer_.isOpen()) developer_.storyStopped(); // (Stop here, Next line: the developer tools at that line)
        for (const auto& request : game_->takeRequests()) ui(request); // open, show phone... from the story
    }
    if (mode_ == Mode::Game) {
        if (game_->takeScreenRequest()) { // "open", a new day's calendar: show it and wait
            const ScreenRequest& request = game_->request();
            if (request.name == "title") { // the story opens the title screen: New Game goes on from the next line
                opening_ = true; // (not saved: Continue doesn't come back to it)
                const ScreenView::Played opening = ScreenView::animationIn(&script_, request.opening), closing = ScreenView::animationIn(&script_, request.closing);
                storyStart_ = game_->here() + 1;
                beforeTitle_ = game_->variables();
                openTitle(true);
                if (ScreenView* title = view("title")) title->playWith(opening, closing);
            } else if (!open(request.name, ScreenView::animationIn(&script_, request.opening), ScreenView::animationIn(&script_, request.closing))) {
                game_->screenClosed(std::nullopt);
            } else {
                calledDepth_ = overlays_.size();
            }
        }
        if (mode_ == Mode::Game) syncShownScreens();
        if (mode_ == Mode::Game) game_->chatOpen(std::ranges::contains(overlays_, std::string("phone")) ? phone_.chat : std::string{}); // (its chat scene's lines as it's opened and left)
        if (mode_ == Mode::Game && game_->finished()) { // the story's over: the title screen (developer mode: back to the tools)
            if (!game_->error().empty()) showErrors({game_->error()});
            else if (developerMode()) developer_.backToEditor();
            else openTitle();
        }
    }
    if (game_->takePersistentChanged()) savePersistent();
    if (flowchart_.takeChanged() && !writeProgress("flowchart.txt", flowchart_.text())) logWarn("Can't save the flowchart: {}.", SDL_GetError());
    // Screens whose animation_on_close= is over: closed now (and the story goes on, if it waited).
    for (std::size_t i = 0; i < leaving_.size();) {
        const ScreenView* v = view(leaving_[i].name);
        if (v && v->hasLeft() && v->leaving()) {
            ++i;
            continue;
        }
        auto then = std::move(leaving_[i].then);
        leaving_.erase(leaving_.begin() + static_cast<std::ptrdiff_t>(i));
        if (then && v && v->hasLeft()) then(); // (not if it opened again meanwhile)
    }
    // A screen with close_after= closes by itself (a new day's calendar...).
    if (const std::string top = overlays_.empty() ? std::string() : overlays_.back(); top != topScreen_) {
        topScreen_ = top;
        topSince_ = time_;
    } else if (const ScreenDef* def = top.empty() ? nullptr : script_.screen(top); def && def->closeAfter > 0.0f && time_ - topSince_ >= def->closeAfter) {
        close();
    }
    if (musicBeforeSongs_ && !songsOpen()) { // the music room closed: back to what played before
        if (mode_ == Mode::Game && !game_->music().empty()) sound_.playMusic(assets_->sound(script_.soundOf(game_->music(), "music").file), true, 0.5f, game_->musicVolume());
        else if (musicBeforeSongs_->empty()) sound_.stopMusic(0.5f);
        else sound_.playMusic(*musicBeforeSongs_, true, 0.5f);
        musicBeforeSongs_.reset();
    }

    readAloud();

    SDL_Renderer* r = renderer_.get();
    useView(false); // (the game as it's shown, under the developer tools too)
    const SDL_Color bars = gameSettings().letterboxColour;
    SDL_SetRenderDrawColor(r, bars.r, bars.g, bars.b, 255);
    SDL_RenderClear(r);
    textInputWanted_ = false; // input fields drawn below ask for it again
    switch (mode_) {
    case Mode::Title:
        if (ScreenView* title = view("title"); title && !waiting_) title->draw(r);
        break;
    case Mode::Game:
        drawGame(false);
        break;
    case Mode::Error:
        drawErrors();
        break;
    }
    for (const auto& name : overlays_)
        if (ScreenView* v = view(name)) v->draw(r);
    for (const Leaving& l : leaving_) // ("show screen" ones hidden, going: the others are drawn where they are)
        if (ScreenView* v = !l.then && !std::ranges::contains(overlays_, l.name) ? view(l.name) : nullptr) v->draw(r);
    if (developer_.isOpen()) {
        useView(true); // (the tools lay themselves out in the window's own size)
        developer_.drawDeveloper();
    }
    useView(developer_.isOpen()); // (until the next frame: the clicks are for the tools, or the game)
    if (textInputWanted_ != textInputActive_) { // on-screen keyboards / IME only while typing
        textInputActive_ = textInputWanted_;
        if (textInputActive_) SDL_StartTextInput(window_.get());
        else SDL_StopTextInput(window_.get());
    }

    for (auto& [name, v] : views_) v.endFrame(frameNumber_); // movies of screens no longer shown stop

    // The frame can only be read before it is presented.
    if (!screenshotRequest_.empty()) screenshot(std::exchange(screenshotRequest_, {}));
    if (!screenshotPath_.empty() && ++frames_ == 30) {
        screenshot(screenshotPath_);
        return SDL_APP_SUCCESS;
    }
    SDL_RenderPresent(r);
    return quit_ ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

} // namespace vn
