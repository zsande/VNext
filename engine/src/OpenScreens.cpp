// The screens open over the game (menus, the phone...): opened, closed, and the story's own screens shown.
#include "Engine.hpp"

#include <algorithm>
#include <ranges>

namespace vn {

ScreenView* Engine::view(std::string_view name) {
    auto it = views_.find(name);
    return it != views_.end() ? &it->second : nullptr;
}

ScreenView* Engine::textbox() {
    if (ScreenView* v = view(game_->textbox())) return v;
    return view("textbox");
}

// Runs action_on_open / action_on_close for screens that "show screen" / "hide screen" changed.
void Engine::syncShownScreens() {
    const std::vector<std::string> now = mode_ == Mode::Game ? game_->shownScreens() : std::vector<std::string>{};
    const std::vector<std::string> before = std::exchange(shown_, now);
    for (const auto& name : before)
        if (!std::ranges::contains(now, name))
            if (ScreenView* v = view(name)) {
                screenHook(v->def(), false);
                left(name);
            }
    for (const auto& name : now)
        if (!std::ranges::contains(before, name))
            if (ScreenView* v = view(name)) {
                v->opened();
                screenHook(v->def(), true);
            }
}

ScreenView* Engine::topView() {
    return !overlays_.empty() ? view(overlays_.back()) : mode_ == Mode::Title ? view("title") : nullptr;
}

bool Engine::open(const std::string& name, ScreenView::Played opening, ScreenView::Played closing) {
    ScreenView* v = view(name);
    if (!v) {
        logWarn("There's no screen called '{}' to open.", name);
        return false;
    }
    if (!overlays_.empty() && overlays_.back() == name) return true;
    if (mode_ == Mode::Game) // (a menu over the story: what the keys point at on the story's screens stays, but a press goes on with the story again)
        for (auto& [named, under] : views_)
            if (!std::ranges::contains(overlays_, named)) under.keysRest();
    // Animations wait for each other: a screen whose show animation starts covered (fade=) opens once the
    // one under it has faded out with its hide animation (ending covered), not over it at once.
    v->playWith(opening, closing); // (what it plays this time, to know)
    if (v->startsCovered())
        if (ScreenView* under = topView(); under && under != v && under->endsCovered() && !under->hasLeft()) {
            under->leave();
            leaving_.push_back({!overlays_.empty() ? overlays_.back() : std::string("title"),
                                [this, name, opening, closing] { openNow(name, opening, closing); }});
            return true;
        }
    return openNow(name, std::move(opening), std::move(closing));
}

bool Engine::openNow(const std::string& name, ScreenView::Played opening, ScreenView::Played closing) {
    ScreenView* v = view(name);
    if (!v) return false;
    if (!overlays_.empty() && overlays_.back() == name) return true;
    for (const auto& el : v->def().elements) // (its saves: saved into, or loaded; mode=, else by the screen's name)
        if (el.kind == ElementKind::Container && el.option("list") == "saves")
            slots_.open(el.option("mode", name == "save" ? "save" : "load") == "save" ? SlotBrowser::Mode::Save : SlotBrowser::Mode::Load);
    v->reset();
    v->playWith(std::move(opening), std::move(closing));
    backTo_.reset(); // (something opening over the rest: no longer going back)
    if (!phone_.straight && std::ranges::any_of(v->def().elements, [](const Element& el) { return el.kind == ElementKind::Container && !el.option("app").empty(); })) {
        // The player opening the phone: on its apps (home), not on a chat or call the story showed on it before
        // (the story opens it on what it shows: straight).
        phone_.app.clear();
        phone_.chat.clear();
        phone_.image.clear();
        phone_.back.clear();
    }
    if (std::ranges::any_of(v->def().elements, [](const Element& el) { return el.kind == ElementKind::Container && el.option("app") == "lock"; }))
        phone_.locked = !phone_.straight, phone_.code.clear(); // (its lock screen first, unless the story opens it on something)
    if (name == "keyboard" && !keyboardFor_) // (opened by the game: for the box on the top screen that has one)
        for (const auto& under : overlays_ | std::views::reverse)
            if (ScreenView* u = view(under))
                if (const auto box = u->boxTypedInto()) {
                    keyboardFor_ = KeyboardFor{.screen = under, .element = *box, .shift = u->boxText(*box).empty()};
                    break;
                }
    overlays_.push_back(name);
    screenHook(v->def(), true);
    return true;
}

void Engine::close(bool fade) {
    if (overlays_.empty()) return;
    if (!fade) return closeNow();
    const std::string name = overlays_.back();
    ScreenView* v = view(name);
    if (v && v->hasLeft()) return; // (going already)
    if (v && v->hideAnimation()) { // its hide animation plays, and then it closes
        v->leave();
        leaving_.push_back({name, [this, name] {
                                if (overlays_.empty() || overlays_.back() != name) return;
                                const ScreenView* gone = view(name);
                                coveredClose_ = gone && gone->endsCovered(); // (everything covered: what was under it comes back into view)
                                closeNow();
                            }});
        return;
    }
    closeNow();
}

void Engine::closeNow() {
    if (overlays_.empty()) return;
    const std::string name = overlays_.back();
    overlays_.pop_back();
    if (name == "confirm") confirmYes_ = {};
    if (name == "keyboard") keyboardFor_.reset();
    else if (keyboardFor_ && name == keyboardFor_->screen) closeKeyboard(); // (its box gone)
    saveSettings();
    if (ScreenView* v = view(name)) screenHook(v->def(), false);
    // What was under it comes back into view when the closing ended covered, or when it had faded out for
    // the one closing to open: its animation_on_open= plays again, after.
    const bool covered = std::exchange(coveredClose_, false);
    if (ScreenView* top = topView(); top && (covered || top->hasLeft())) top->opened();
    if (calledDepth_ > overlays_.size()) { // the story was waiting for this screen
        calledDepth_ = 0;
        game_->screenClosed(std::exchange(answer_, std::nullopt));
    }
    closeTowards();
}

void Engine::backTo(const std::string& name) {
    backTo_ = name;
    closeTowards();
}

void Engine::closeTowards() {
    if (!backTo_) return;
    if (!overlays_.empty() && overlays_.back() != *backTo_) return close();
    const std::string name = *std::exchange(backTo_, std::nullopt);
    if (ScreenView* v = view(name); v && (!overlays_.empty() || (name == "title" && titleShown_))) screenHook(v->def(), true); // (on top again: opened)
}

void Engine::left(const std::string& name) {
    ScreenView* v = view(name);
    if (!v || !v->hideAnimation()) return;
    v->leave();
    std::erase_if(leaving_, [&](const Leaving& l) { return l.name == name; });
    leaving_.push_back({name, {}});
}

void Engine::closeAll() {
    overlays_.clear();
    backTo_.reset();
    phone_ = {};
    confirmYes_ = {};
    calledDepth_ = 0;
    keyboardFor_.reset(); // (the on-screen keyboard went with the rest: what it typed into too)
    keyboardWanted_.reset();
}

bool Engine::stopsStory(const std::string& screen) const {
    const ScreenDef* def = script_.screen(screen);
    return !def || def->storyWaits;
}

} // namespace vn
