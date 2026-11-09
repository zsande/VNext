// Choices: a menu's options as the story offers them, timed ones, and the one chosen.
#include "Game.hpp"

#include <SDL3/SDL.h>
#include <algorithm>

namespace vn {

// A choice: its options (none of them pickable: the story goes on), up while the story goes on for some
// lines, or waited on with the line that asks it; and a point on the flowchart.
void Game::openMenu(const cmd::Menu& c, std::size_t here) {
    const bool again = std::exchange(choiceAgain_, false); // (a repeating one up again: without the line that asked it)
    if (!c.phone.empty() && asked_ && callStack_.size() == asked_->depth) { // a chat= or call= scene's choice: it waits on the phone (passed once the player hung up or left)
        bool any = false; // (an option that can be picked)
        for (std::size_t k = 0; k < c.choices.size() && !any; ++k)
            if (const auto item = offer(c.choices[k], static_cast<int>(k))) any = !item->locked;
        if (asked_->passing || !any) return void(pc_ = c.after); // (passed: the player hung up or left, or none can be picked)
        parked_ = Parked{.menu = here, .who = c.phone, .call = c.call};
        parking_ = true;
        execute(cmd::Return{}, here); // (the scene's lines over for now: the story goes on meanwhile)
        parking_ = false;
        return;
    }
    trigger_.reset();
    listChoices(c);
    if (std::ranges::all_of(choiceItems_, &ChoiceItem::locked)) {
        logWarn("{}: None of the options can be picked right now, so the story goes on.", script_.where(script_.code[here]));
        choiceItems_.clear();
        return;
    }
    for (const ChoiceItem& item : choiceItems_) learnTips(item.text);
    std::vector<Flowchart::Option> options; // as the flowchart shows it: picked or not, locked or not
    options.reserve(choiceItems_.size());
    for (const ChoiceItem& item : choiceItems_) options.push_back({item.option, !item.locked});
    if (c.lines > 0) { // up while the story goes on, for its next lines
        trigger_ = Trigger{.at = here, .lines = c.lines};
        pc_ = c.after;
        return passPoint(here, Flowchart::Kind::Choice, std::move(options));
    }
    menuTime_ = c.seconds;
    menuPrompt_ = c.phone.empty() && !text_.empty() && !again; // (the line before it stays on screen: it asks it; not when it's up again, nor a phone's)
    if (!ctx_.settings.skipAfterChoices) skip_ = false;
    autosave_ = true;
    waitFor(Wait::Menu, here);
    remember();
    passPoint(here, Flowchart::Kind::Choice, std::move(options));
}

// Options whose condition is false are left out, or shown locked with (needs ...).
std::optional<ChoiceItem> Game::offer(const cmd::Choice& choice, int option) const {
    const bool open = !choice.condition || choice.condition->test(lookup_);
    if (!open && !choice.shownLocked) return std::nullopt;
    return ChoiceItem{.text = fillValues(choice.text, lookup_), // text tags stay for the screen
                      .locked = !open,
                      .note = open ? std::string() : fillValues(choice.note, lookup_),
                      .seen = ctx_.read.contains(choice.id),
                      .option = option};
}

void Game::listChoices(const cmd::Menu& menu) {
    choiceItems_.clear();
    for (std::size_t k = 0; k < menu.choices.size(); ++k)
        if (auto item = offer(menu.choices[k], static_cast<int>(k))) choiceItems_.push_back(std::move(*item));
}

const cmd::Menu* Game::menu() const {
    const std::optional<std::size_t> at = wait_ == Wait::Menu ? std::optional(resumePc_) : trigger_ ? std::optional(trigger_->at) : std::nullopt;
    return at ? std::get_if<cmd::Menu>(&script_.code[*at].command) : nullptr;
}

void Game::letPass() {
    pc_ = menu()->after;
    choiceItems_.clear();
    menuPrompt_ = false;
    wait_ = Wait::None;
    run();
}

std::optional<float> Game::timeLeft() const {
    const cmd::Menu* m = menu();
    if (!m || m->seconds <= 0.0f || !ctx_.settings.timeLimits) return std::nullopt; // (the player's Time limits off: none)
    return std::clamp(menuTime_ / m->seconds, 0.0f, 1.0f);
}

void Game::choose(std::size_t index) {
    const cmd::Menu* m = menu();
    if (!m || index >= choiceItems_.size() || choiceItems_[index].locked) return;
    const cmd::Choice& picked = m->choices[static_cast<std::size_t>(choiceItems_[index].option)];
    ctx_.read.insert(picked.id); // picked before, from now on
    flowchartOption_ = choiceItems_[index].option;
    RichText chosen = richText(choiceItems_[index].text, lookup_, &script_.palette);
    for (TextSpan& span : chosen.spans) span.begin += 2, span.end += 2; // after "> "
    addHistory({.speaker = "", .colour = SDL_Color{150, 150, 165, 255}, .text = "> " + chosen.text, .choice = true, .spans = std::move(chosen.spans)});
    if (!m->phone.empty()) { // a chat's choice: the option picked is sent there, as the player's (the history has it already, as a choice)
        unlogged_ = true;
        addMessage({.with = m->phone, .colour = kOwnColour, .text = fillValues(picked.text, lookup_), .read = true});
        unlogged_ = false;
    }
    if (wait_ != Wait::Menu) { // one up while the story goes on: its lines, then back where the story was (or past its lines)
        callStack_.push_back(wait_ == Wait::Say ? pc_ : resumePc_);
        if (!m->returns) pass_ = Pass{.depth = callStack_.size(), .lines = trigger_->lines};
        trigger_.reset();
        ctx_.sound.stopVoice();
    }
    pc_ = picked.target;
    choiceItems_.clear();
    menuPrompt_ = false;
    wait_ = Wait::None;
    run();
}

} // namespace vn
