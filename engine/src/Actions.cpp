// What buttons and keys do (their actions), and the questions some ask first.
#include "Engine.hpp"

#include <algorithm>

namespace vn {

void Engine::ask(std::string question, std::function<void()> yes) {
    ScreenView* confirm = view("confirm");
    if (question.empty() || !confirm) return yes(); // no question, or no confirm screen: just do it
    question_ = std::move(question);
    confirmYes_ = std::move(yes);
    confirm->reset();
    if (keysInCharge_) confirm->ringWhenDrawn(ScreenView::Ring::Keep); // (asked by the keys: the ring on the answer that keeps things as they are)
    overlays_.push_back("confirm");
}

void Engine::screenHook(const ScreenDef& def, bool opening) {
    const ScreenDef::Stops& stops = opening ? def.stopOnOpen : def.stopOnClose;
    Commands stop;
    if (stops.music) stop.emplace_back(cmd::Play{.track = cmd::Play::Track::Music, .fade = 0.5f});
    if (stops.voice) stop.emplace_back(cmd::Play{.track = cmd::Play::Track::Voice});
    run(stop);
    run(opening ? def.actionOnOpen : def.actionOnClose);
}

void Engine::run(const Commands& commands, const std::string& confirm, const Variables& item) {
    if (commands.empty()) return;
    if (sampling_) return; // (the developer tools' sample: nothing it would do is done)
    const Variables* was = game_ ? game_->item() : nullptr;
    if (game_ && !item.empty()) game_->setItem(&item);
    if (!confirm.empty()) {
        std::string question = expand(confirm, lookup());
        // (saving into a slot, loading one, deleting one: its question asked only when something would be lost)
        if (const auto* u = commands.size() == 1 ? std::get_if<cmd::Ui>(&commands.front()) : nullptr; u && (u->verb == "save" || u->verb == "load" || u->verb == "delete save")) {
            const int n = toInt(Value(fillValues(u->arg, lookup())));
            if (game_) game_->setItem(was);
            if (n < 1) return;
            const auto i = static_cast<std::size_t>(n - 1);
            if (u->verb == "save") saveSlot(i, std::move(question));
            else if (u->verb == "load") loadSlot(i, std::move(question));
            else slotDelete(i, std::move(question));
            return;
        }
        if (game_) game_->setItem(was);
        return ask(std::move(question), [this, commands, item] { run(commands, {}, item); });
    }
    for (const Command& c : commands) {
        command(c);
        if (quit_) break;
    }
    if (game_) game_->setItem(was);
}

// One command from a button, key or screen hook.
void Engine::command(const Command& c) {
    if (const auto* u = std::get_if<cmd::Ui>(&c)) return ui(*u);
    if (const auto* jump = std::get_if<cmd::Jump>(&c)) {
        if (mode_ != Mode::Game) return newGame(jump->scene); // from the title screen
        closeAll();
    }
    game_->perform(c);
}

// Menu commands, from buttons or from the story.
void Engine::ui(const cmd::Ui& c) {
    const std::string& v = c.verb;
    if (v == "start") fromTitle([this, label = c.arg] { label.empty() ? newStory() : newGame(label); });
    else if (v == "continue") {
        if (const auto newest = saves_.newest()) fromTitle([this, slot = *newest] { loadFromSlot(slot); });
    }
    else if (v == "open") {
        if (c.arg.find("://") != std::string::npos || c.arg.starts_with("www.")) SDL_OpenURL(c.arg.c_str());
        else if (!overlays_.empty() && overlays_.back() == c.arg) { // open already, on top: put it away, so one key opens and closes it (a chat scene or a call keeps the phone)
            if (const ScreenDef* def = script_.screen(c.arg); def && def->closeAfter > 0.0f) topSince_ = time_; // (one that closes by itself, a notice: its time again)
            else if (!showsPhone(c.arg) || !playing() || !game_->phoneBusy()) close();
        }
        else if (std::ranges::contains(overlays_, c.arg) || (c.arg == "title" && titleShown_)) backTo(c.arg); // open under others: back to it
        else open(c.arg, ScreenView::animationIn(&script_, c.opening), ScreenView::animationIn(&script_, c.closing)); // (with NAME, and close with NAME: this time)
    }
    else if (v == "show phone") showPhone(c); // (a chat scene, a call, or a message came with setting new_messages open)
    else if (v == "buzz") rumble(0.45f, 180); // (a message came)
    else if (v == "hide phone") hidePhone();  // (the chat scene or the call is over)
    else if (v == "back") back();
    else if (v == "open app" || v == "open chat") { // (the phone opens first, if it isn't open)
        if (const ScreenDef* def = script_.screen("phone")) {
            if (!std::ranges::contains(overlays_, std::string("phone"))) open("phone");
            if (v == "open app") openApp(*def, lower(fillValues(c.arg, lookup())));
            else openChat(*def, fillValues(c.arg, lookup()));
        }
    }
    else if (v == "open image") openImage(fillValues(c.arg, lookup()));
    else if (v == "phone call") callPerson(fillValues(c.arg, lookup()));
    else if (v == "answer call" || v == "decline call") answerCall(v == "answer call");
    else if (v == "hang up") {
        if (playing()) game_->hangUp();
    }
    else if (v == "choose" && c.arg.empty()) { // the option it's on (a list of options'): of the story's choice, a chat scene's, or one waiting on the phone
        const Lookup& item = lookup();
        const std::string kind = toString(item("option.kind"));
        const auto number = static_cast<std::size_t>(std::max(0, toInt(item("option.number"))));
        if (toInt(item("option.on")) != 0) {
            if (kind == "phone choice") pickOnPhone(number);
            else if (kind == "choice" && playing()) game_->choose(number);
        }
    }
    else if (v == "keypad") keypad(c.arg);
    else if (v == "type") keyboardKey(c.arg);
    else if (v == "unlock") unlockPhone({}); // (with a passcode: the keypad's)
    else if (v == "close") { // (the title screen, with nothing over it: the story goes on, as with New Game; a chat scene or a call keeps the phone)
        if (overlays_.empty() && mode_ == Mode::Title) fromTitle([this] { newStory(); });
        else if (overlays_.empty() || !showsPhone(overlays_.back()) || !playing() || !game_->phoneBusy()) close();
    }
    else if (v == "answer") {
        if (calledDepth_ == overlays_.size() && calledDepth_ > 0) answer_ = expand(c.arg, lookup());
        close();
    }
    else if (v == "quit") quit();
    else if (v == "quick save") quickSave();
    else if (v == "quick load") quickLoad();
    else if (v == "skip") game_->toggleSkip();
    else if (v == "auto") game_->toggleAuto();
    else if (v == "hide ui") game_->setUiHidden(true);
    else if (v == "next line") {
        if (mode_ != Mode::Game) return;
        if (game_->skipMode()) game_->toggleSkip(); // any input stops skipping
        else game_->nextLine();
    }
    else if (v == "move up" || v == "move down" || v == "move left" || v == "move right") moveKeys(v == "move left" ? -1 : v == "move right" ? 1 : 0, v == "move up" ? -1 : v == "move down" ? 1 : 0);
    else if (v == "press") { // (on the story's screens, the next press goes on with the story again)
        for (ScreenView* s : keyViews())
            if (s->keysPoint()) {
                const bool story = mode_ == Mode::Game && !std::ranges::contains(overlays_, s->name());
                s->pressPointed();
                if (story) s->keysRest();
                break;
            }
    }
    else if (v.starts_with("scroll ")) {
        for (ScreenView* s : keyViews())
            if (s->scrollBy(std::string_view(v).substr(7))) break;
    }
    else if (v == "put back") { for (auto& [name, screen] : views_) screen.putBack(); }
    else if (v == "choose") { // the choice's option with that number, if it has one
        const auto n = static_cast<std::size_t>(std::max(0, toInt(Value(fillValues(c.arg, lookup())))));
        if (mode_ == Mode::Game && game_->showingChoices() && !game_->choiceOnPhone() && n >= 1 && n <= game_->choices().size()) game_->choose(n - 1);
    }
    else if (v == "fullscreen") toggleFullscreen();
    else if (v == "screenshot") screenshotRequest_ = std::format("{}screenshot-{}.png", prefPath_, SDL_GetTicks());
    else if (v == "reload") { if (developerMode()) reloadRequested_ = true; }
    else if (v == "tools") {} // (the developer tools' own keys: they do it while they're open)
    else if (v == "yes" || v == "no") {
        auto yes = std::exchange(confirmYes_, {});
        close();
        if (v == "yes" && yes) yes();
    }
    else if (v == "next page") slots_.turn(+1);
    else if (v == "previous page") slots_.turn(-1);
    else if (v == "save" || v == "load" || v == "delete save" || v == "replay" || v == "rollback") { // (N: the first is 1; in a list, its item's: {save.number})
        const int n = toInt(Value(fillValues(c.arg, lookup())));
        if (v == "rollback" && c.arg.empty()) {
            if (mode_ == Mode::Game) game_->rollback();
        } else if (n >= 1) {
            const auto i = static_cast<std::size_t>(n - 1);
            if (v == "save") saveSlot(i, {});
            else if (v == "load") loadSlot(i, {});
            else if (v == "delete save") slotDelete(i, {});
            else if (v == "replay") historyVoice(i);
            else historyBack(i);
        }
    }
    else if (v == "page") { // page 3, page auto, page quick: that page of saves
        const std::string page = lower(fillValues(c.arg, lookup()));
        if (const auto it = std::ranges::find(slots_.pages(), page); it != slots_.pages().end()) slots_.setPage(static_cast<std::size_t>(it - slots_.pages().begin()));
    }
    else if (v == "play song" || v == "open tip") { // (by name: {song.name})
        const RecordKind kind = v == "play song" ? RecordKind::Song : RecordKind::Tip;
        const auto& list = script_.records(kind);
        if (const auto it = std::ranges::find(list, lower(fillValues(c.arg, lookup())), &Script::Record::name); it != list.end()) {
            const auto i = static_cast<std::size_t>(it - list.begin());
            if (kind == RecordKind::Song) playSong(i);
            else viewTip(i);
        }
    }
    else if (v == "next image" || v == "previous image") { // the gallery's next unlocked image from the one open
        const auto& gallery = script_.records(RecordKind::Gallery);
        const std::size_t n = gallery.size();
        const auto open = std::ranges::find(gallery, lower(phone_.image), &Script::Record::name);
        const std::size_t from = open != gallery.end() ? static_cast<std::size_t>(open - gallery.begin()) : 0;
        for (std::size_t step = 1; step < n; ++step) {
            const std::size_t i = (from + (v == "next image" ? step : n - step)) % n;
            if (has(RecordKind::Gallery, i)) {
                openImage(gallery[i].name);
                break;
            }
        }
    }
    else if (v == "setting") {
        changeSetting(c.arg);
        settingsChanged(c.arg.starts_with("master_volume") || c.arg.starts_with("sound_volume"));
    }
    else if (v == "reset settings" || v == "reset controls") {
        if (v == "reset settings") settings_ = defaults_;
        else settings_.keys.clear();
        refreshKeys();
        applySettings();
        saveSettings();
    }
    else if (v == "reset progress") { // forget what's kept between playthroughs (not settings or saves)
        persistent_.clear();
        readLog_.clear();
        flowchart_.clear();
        savePersistent();
        writeProgress("read.txt", readLogText(readLog_));
    }
}

void Engine::historyVoice(std::size_t entry) {
    if (sampling_) return;
    if (mode_ != Mode::Game || entry >= game_->history().size()) return;
    const HistoryEntry& h = game_->history()[entry];
    if (!h.voice.empty()) sound_.replayVoice(h.voice, h.voiceVolume);
    else if (h.madeUpVoice > 0.0f && settings_.madeUpVoices) sound_.replayMadeUpVoice(h.text, h.madeUpVoice, h.madeUpVoiceSpeed);
}

void Engine::historyBack(std::size_t entry) {
    if (sampling_) return;
    if (mode_ == Mode::Game && game_->rollbackTo(entry)) closeAll();
}

void Engine::drop(const std::string& what, const Commands& commands) {
    game_->setVariable("dropped", what);
    run(commands);
}

} // namespace vn
