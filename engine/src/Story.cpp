// The story as it plays (Game): its lines run one after another, its values, what it waits for, going on and
// skipping, and what it shows each frame.
#include "Game.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <climits>
#include <format>

namespace vn {

Game::Game(Context ctx, const Script& script)
    : ctx_(std::move(ctx)), script_(script), lookup_([this](std::string_view name) { return variable(name); }), stage_(ctx_.canvas) {}

void Game::start(std::size_t pc) {
    stage_.reset();
    vars_ = std::exchange(carried_, {});
    callStack_.clear();
    history_.clear();
    choiceItems_.clear();
    trigger_.reset();
    pass_.reset();
    asked_.reset();
    parked_.reset();
    chatOpen_.clear();
    putAway_.clear();
    hiddenLines_ = 0;
    others_.clear();
    together_ = false;
    text_.clear();
    screens_.clear();
    page_.clear();
    phone_ = {};
    call_.reset();
    chat_.clear();
    change_.reset();
    messageCount_ = arrivals_ = chatWait_ = 0;
    loops_.clear();
    expressions_.clear();
    error_.clear();
    music_.clear();
    flowchartNode_.clear();
    flowchartOption_ = -1;
    pendingVoice_.reset();
    past_.clear();
    ctx_.sound.stopAll(0.5f);
    if (gameSettings().time == TimeKind::Real && !vars_.contains("time_started")) vars_.insert_or_assign("time_started", realNow().date); // (its day 1)
    pc_ = resumePc_ = pc;
    wait_ = Wait::None;
    run();
}

bool Game::skipping() const {
    return skip_ || (gameSettings().holdToSkip != 0 && (SDL_GetModState() & gameSettings().holdToSkip) != 0);
}

std::string Game::summary() const {
    if (wait_ == Wait::Menu && menuPrompt_) return text_;
    if (history_.empty()) return {};
    const HistoryEntry& last = history_.back();
    return last.speaker.empty() ? last.text : std::format("{}: {}", last.speaker, last.text);
}

void Game::fail(std::string message) {
    error_ = current_ < script_.code.size() ? std::format("{}: {}", script_.where(script_.code[current_]), message)
                                            : std::move(message);
    logError("{}", error_);
    wait_ = Wait::Finished;
}

void Game::run() {
    int budget = kInstructionBudget;
    while (wait_ == Wait::None) {
        if (pc_ >= script_.code.size()) {
            wait_ = Wait::Finished;
            return;
        }
        if (--budget == 0) return fail("The story ran a very long time without showing anything (does a jump go round in a circle?)"_tr);
        if (stopAt_ && pc_ == *stopAt_) { // (stopBefore)
            resumePc_ = pc_;
            resumeRandom_ = random_;
            wait_ = Wait::Reached;
            return;
        }
        if (stopWhere_ && stopWhere_(pc_)) { // (stopWhere: the developer tools open there; skipping stops)
            resumePc_ = pc_;
            resumeRandom_ = random_;
            skip_ = false;
            wait_ = Wait::Stopped;
            return;
        }
        const std::size_t here = current_ = pc_++;
        if (ran_.size() < 100000) ran_.push_back(here);
        if (here == watch_) watched_ = true;
        resumePc_ = pc_;
        resumeRandom_ = random_;
        execute(script_.code[here].command, here);
        if (resumePc_ != here) resumeRandom_ = random_; // (a command that waits is done again, from before it)
        if (watchThere_ && wait_ == Wait::None && std::holds_alternative<cmd::Screen>(script_.code[here].command) && (*watchThere_)(here)) {
            wait_ = Wait::Reached; // (playTo(): a screen shown or hidden there is what that line shows)
            return;
        }
    }
}

void Game::goOn() {
    if (wait_ != Wait::Stopped) return;
    wait_ = Wait::None;
    run();
}

void Game::waitFor(Wait wait, std::size_t here) {
    resumePc_ = here; // a save made now repeats this command
    wait_ = wait;
}

namespace {

// The computer's clock, as a phone shows it: "20:41".
std::string clockNow() {
    SDL_Time now = 0;
    SDL_DateTime t{};
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &t, true)) return {};
    return std::format("{}:{:02}", t.hour, t.minute);
}

} // namespace

Value Game::variable(std::string_view name) const {
    if (item_)
        if (const auto it = item_->find(name); it != item_->end()) return it->second;
    if (gameSettings().time != TimeKind::None) { // the time's values; the phone's clock and date (unless setting phone_follows_time no)
        if (name.starts_with("time_"))
            if (auto v = timeValue(name)) return *v;
        if (gameSettings().phoneFollowsTime && (name == "phone_clock" || name == "phone_date")) return *timeValue(name == "phone_clock" ? "time_clock" : "time_date");
    }
    const Value* kept = nullptr;
    if (const auto it = vars_.find(name); it != vars_.end()) kept = &it->second;
    else if (const auto p = ctx_.persistent.find(name); p != ctx_.persistent.end()) kept = &p->second;
    else if (const auto s = script_.startValues.find(name); s != script_.startValues.end()) kept = &s->second;
    if (name == "phone_clock" && (!kept || toString(*kept).empty())) return clockNow(); // (none: the computer's clock)
    if (kept) return *kept;
    if (ctx_.builtin)
        if (auto v = ctx_.builtin(name)) return *v;
    return 0;
}

std::string Game::typedValue(std::string_view name) const {
    const bool has = vars_.contains(name) || ctx_.persistent.contains(name) || script_.startValues.contains(name) || (item_ && item_->contains(name));
    return has ? toString(variable(name)) : std::string{};
}

void Game::setVariable(const std::string& name, Value value) {
    if (name.starts_with("persistent.")) {
        ctx_.persistent.insert_or_assign(name, std::move(value));
        persistentChanged_ = true;
    } else {
        vars_.insert_or_assign(name, std::move(value));
    }
}

void Game::execute(const Command& command, std::size_t here) {
    const RandomScope _(random_); // (its random() numbers: from its own sequence)
    const bool fast = skipping();
    const bool instant = fast || !ctx_.settings.animations; // (an animation played: at its end at once; effects and weather: there at once)

    std::visit(
        Overloaded{
            [&](const cmd::Say& c) { sayLine(c, here, fast); },
            [&](const cmd::Background& c) { // a new background (what was on stage goes with the one before), or the one there played on
                have(RecordKind::Gallery, c.image);
                const std::string shown = backgroundFor(c.image); // (park_evening in the evening, when there's one)
                if (shown != stage_.background()) stage_.clearObjects();
                stage_.setBackground(shown, animation(c.with.value_or("")), instant);
                if (c.animation) stage_.animateBackground(animation(*c.animation), true, fast);
                if (stage_.backgroundComing()) wait_ = Wait::Background; // (the story goes on once it's in)
            },
            [&](const cmd::Show& c) { // (one that comes plays the game's animation_on_show, unless the line says)
                if (!c.image.empty()) have(RecordKind::Gallery, c.image);
                const CharacterDef* def = c.character.empty() ? nullptr : script_.character(c.character);
                const std::string played = c.with ? *c.with : stage_.has(c.tag) ? std::string{} : gameSettings().animationOnShow;
                stage_.show(c.tag, def, c.image, c.expression, c.props, animation(played), fast ? 0.0f : gameSettings().moveTime, instant);
                if (c.animation) stage_.animate(c.tag, animation(*c.animation), true, fast);
                if (def && def->faceCopies.expression) // their face looks as their image does (a new one starts as it is)
                    expressions_.insert_or_assign(def->key, stage_.expression(c.tag));
                if (c.wait && !fast && stage_.moving()) wait_ = Wait::Move;
            },
            [&](const cmd::Hide& c) { // (it plays the game's animation_on_hide, unless the line says; then it's gone)
                stage_.hide(c.tag, animation(c.with.value_or(gameSettings().animationOnHide)), instant);
                if (c.wait && !fast && stage_.moving()) wait_ = Wait::Move;
            },
            [&](const cmd::Chat& c) { // a chat scene: the phone comes up on its chat, until its end
                if (c.end) {
                    chat_.clear();
                    return requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "chat"});
                }
                chat_ = c.with;
                requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "chat", .phone = c.with});
            },
            [&](const cmd::Call& c) {
                if (!c.end) return startCall(c, here);
                if (!call_) return;
                logCall(*call_, false);
                call_.reset();
                stopRinging();
                requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "call"});
            },
            [&](const cmd::Effect& c) {
                if (fast && c.seconds > 0.0f) return; // skipping goes past a moment's effect
                stage_.setEffect(c, instant);
            },
            [&](const cmd::Menu& c) { openMenu(c, here); },
            [&](const cmd::Jump& c) {
                if (c.unless && c.unless->test(lookup_)) return;
                pc_ = c.target;
                choiceAgain_ = c.again;
            },
            [&](const cmd::Return&) {
                if (callStack_.empty()) { // (nothing started these lines, a chat= scene played on its own: the story's over, as at a scene's end)
                    wait_ = Wait::Finished;
                    return;
                }
                pc_ = callStack_.back();
                callStack_.pop_back();
                if (pass_ && callStack_.size() < pass_->depth) hiddenLines_ = std::exchange(pass_, std::nullopt)->lines; // a choice's return=no
                if (asked_ && callStack_.size() < asked_->depth) { // a chat or call scene's lines over (or waiting on the phone): the story's line again
                    const Asked a = *std::exchange(asked_, std::nullopt);
                    if (a.shown) sayAgain(a.page);
                    if (a.call && !parking_ && call_) callOver(); // (a call scene's end: the call's over)
                    if (!a.chat.empty() && !parking_) putAway_ = a.chat, putAwayIn_ = 2.0f; // (a chat scene's: the phone's put away once its messages have come)
                }
            },
            [&](const cmd::End&) { // a scene's end: back to what started it (a phone scene's), else the story's over
                if (!callStack_.empty()) return execute(cmd::Return{}, here);
                wait_ = Wait::Finished;
            },
            [&](const cmd::Set& c) {
                setVariable(c.variable, c.value ? c.value->eval(lookup_) : Value{0});
                // An ending reached (set persistent.ending.NAME to yes) by the story's own line, not a button's: a point on the flowchart.
                if (c.variable.starts_with("persistent.ending.") && truthy(variable(c.variable)) && here < script_.code.size() && &command == &script_.code[here].command)
                    passPoint(here, Flowchart::Kind::Ending);
            },
            [&](const cmd::Play& c) { play(c, fast); },
            [&](const cmd::Wait& c) {
                if (c.seconds < 0.0f) {
                    autoTimer_ = 0.0f;
                    return waitFor(Wait::Pause, here);
                }
                if (fast || c.seconds <= 0.0f) return;
                timer_ = c.seconds;
                waitFor(Wait::Timer, here);
            },
            [&](const cmd::Screen& c) {
                if (!c.show) std::erase(screens_, c.name);
                else if (!std::ranges::contains(screens_, c.name)) screens_.push_back(c.name);
            },
            [&](const cmd::Ui& c) { // open SCREEN: the story waits until the player closes it (unless it's story=goes_on)
                const ScreenDef* screen = c.verb == "open" ? script_.screen(c.arg) : nullptr;
                if (!screen || !screen->storyWaits) return requests_.push_back(c);
                vars_.insert_or_assign("answer", std::string{}); // until it answers
                waitForScreen({.name = c.arg, .opening = c.opening, .closing = c.closing}, here);
            },
            [&](const cmd::Time& c) { passTime(c, here); },
            [&](const cmd::Movie& c) { playMovie(c, here, fast); },
            [&](const cmd::Message& c) { sendMessage(c, here); }, // on the phone (a chat scene's: the story waits for it)
            [&](const cmd::Weather& c) { stage_.setWeather(c, instant ? 0.0f : c.fade); },
            [&](const cmd::Camera& c) {
                if (c.animation) stage_.animateCamera(animation(*c.animation), true, fast);
                if (c.with) stage_.animateCamera(animation(*c.with), false, instant);
                if (c.wait && !fast && stage_.moving()) wait_ = Wait::Move;
            },
        },
        command);
}

std::shared_ptr<const Animation> Game::animation(std::string_view name) const {
    if (name.empty()) return nullptr;
    const auto it = script_.animations.find(name);
    return it != script_.animations.end() ? std::make_shared<const Animation>(it->second) : nullptr;
}

void Game::perform(const Command& command) {
    if (const auto* jump = std::get_if<cmd::Jump>(&command)) {
        const auto pc = script_.findScene(jump->scene);
        if (!pc) return;
        choiceItems_.clear();
        menuPrompt_ = false;
        pc_ = *pc;
        wait_ = Wait::None;
        return run();
    }
    execute(command, current_);
}

void Game::nextLine() {
    switch (wait_) {
    case Wait::Movie:
        if (movieSkippable_) endMovie(); // clicked past
        return;
    case Wait::Menu:
        if (menu()->optional) letPass(); // clicked on
        return;
    case Wait::Say: {
        if (const Stop* stop = pendingStop(); stop && reveal_ >= static_cast<float>(stop->at)) {
            ++passedStops_; // go on after a {pause}
            holdTimer_ = 0.0f;
            return;
        }
        const bool others = othersTyping(); // lines said together with it: all there at once
        for (BoxLine& l : others_) l.reveal = static_cast<float>(l.length);
        if (reveal_ < static_cast<float>(length_)) {
            if (madeUpVoice_) ctx_.sound.stopVoice(); // (made-up speech is the typing's: shown at once, it's done)
            // Show everything up to the next click pause (timed pauses are skipped).
            while (const Stop* stop = pendingStop()) {
                if (stop->seconds < 0) {
                    revealTo(stop->at);
                    return;
                }
                ++passedStops_;
            }
            revealTo(length_);
            return;
        }
        if (others) return;
        ctx_.sound.stopVoice(); // clicked past: its voice goes with it (not on over what comes next)
        break;
    }
    case Wait::Background:
    case Wait::Move:
        stage_.finish();
        break;
    case Wait::Timer:
        if (!chat_.empty()) return; // (a chat scene's wait holds: it's the chat's own timing)
        break;
    case Wait::Pause:
    case Wait::Reached: // (played to a line by the developer tools: on from where it stopped)
        break;
    case Wait::Chat: // (a click hurries a chat scene's message, or its change)
        if (change_) change(*std::exchange(change_, std::nullopt));
        else hurry();
        break;
    default:
        return;
    }
    wait_ = Wait::None;
    run();
}

// The message a chat scene waits for comes now (someone just typing stops).
void Game::hurry() {
    const auto it = std::ranges::find(phone_.messages, chatWait_, &MessageItem::number);
    if (it == phone_.messages.end() || it->typing <= 0.0f) return;
    it->typing = 0.0f;
    if (it->kind == MessageItem::Kind::Typing) phone_.messages.erase(it);
    else arrive(*it);
}

bool Game::playTo(std::size_t target, const std::function<bool(std::size_t)>& there) {
    watch_ = target;
    watched_ = std::ranges::contains(ran_, target); // (start() runs on to the first wait: it may have run it already)
    watchThere_ = &there;
    const ScopeExit _([this] { // (however this ends)
        watch_ = SIZE_MAX;
        watchThere_ = nullptr;
    });
    const Uint64 until = SDL_GetTicks() + 1000; // (a story that goes round in a circle never gets there)
    for (int step = 0; step < 20000 && SDL_GetTicks() < until; ++step) {
        switch (wait_) {
        case Wait::Finished:
            return false;
        case Wait::Reached:
            return true;
        case Wait::Say:
        case Wait::Menu:
            if (!watched_ && target < script_.code.size() && resumePc_ > target) { // gone past it: on from it
                choiceItems_.clear();
                trigger_.reset();
                menuPrompt_ = false;
                pc_ = target;
                wait_ = Wait::None;
                run();
                break;
            }
            if (there(resumePc_)) {
                for (BoxLine& l : others_) l.reveal = static_cast<float>(l.length);
                passedStops_ = stops_.size();
                revealTo(length_);
                stage_.finish();
                return true;
            }
            if (wait_ == Wait::Say) {
                nextLine();
            } else { // the option that starts last before `target`, else the first
                const cmd::Menu* m = menu();
                std::optional<std::size_t> pick, first;
                auto start = [&](std::size_t i) { return m->choices[static_cast<std::size_t>(choiceItems_[i].option)].target; };
                for (std::size_t i = 0; i < choiceItems_.size(); ++i) {
                    if (choiceItems_[i].locked) continue;
                    if (!first) first = i;
                    if (start(i) <= target && (!pick || start(i) > start(*pick))) pick = i;
                }
                if (!pick) pick = first;
                if (!pick) return false;
                choose(*pick);
            }
            break;
        case Wait::Screen:
        case Wait::Movie:
            if (!watched_ && target < script_.code.size() && resumePc_ > target) { // gone past it: on from it
                if (wait_ == Wait::Movie) movie_.reset();
                request_ = {};
                pc_ = target;
                wait_ = Wait::None;
                run();
                break;
            }
            if (there(resumePc_)) { // (what the player sees at that line: a movie's first image)
                stage_.finish();
                if (movie_) movie_->showFirst();
                return true;
            }
            if (wait_ == Wait::Screen) screenClosed(std::nullopt);
            else endMovie();
            break;
        case Wait::None:
            run();
            break;
        case Wait::Chat: // a chat scene's message coming: there at once (its line may be the one)
            if (there(current_)) {
                if (change_) change(*std::exchange(change_, std::nullopt));
                else hurry();
                return true;
            }
            nextLine();
            break;
        case Wait::Call: { // a call ringing: there (its line may be the one); else answered, or missed for a line of its missed:
            if (there(current_)) return true;
            const cmd::Call* c = call_ && call_->at ? std::get_if<cmd::Call>(&script_.code[*call_->at].command) : nullptr;
            if (c && target >= c->missed && target < c->after) {
                stopRinging();
                endCall(true);
            } else if (call_) {
                answerCall(true);
            } else {
                return false;
            }
            break;
        }
        case Wait::Timer: // a wait: over at once (a chat scene's too, which a click doesn't end)
            wait_ = Wait::None;
            run();
            break;
        case Wait::Clock: // a wait for the player's clock: over at once
            vars_.erase("time_wait");
            vars_.erase("time_wait_line");
            text_.clear();
            wait_ = Wait::None;
            run();
            break;
        case Wait::Pause: // a click's wait (a chat scene's message after a click): there, else on
            if (there(current_)) return true;
            nextLine();
            break;
        default: // an animation: over at once
            nextLine();
            break;
        }
    }
    return false;
}

void Game::setCovered(bool covered) {
    ctx_.sound.pauseVoice(covered);
    if (movie_) movie_->setPaused(covered);
}

void Game::waitForScreen(ScreenRequest request, std::size_t here) {
    request_ = std::move(request);
    screenRequested_ = true;
    skip_ = false;
    waitFor(Wait::Screen, here);
}

void Game::screenClosed(std::optional<Value> answer) {
    if (wait_ != Wait::Screen) return;
    if (answer) vars_.insert_or_assign("answer", std::move(*answer));
    request_ = {};
    wait_ = Wait::None;
    run();
}

void Game::update(float dt) {
    const bool fast = skipping();
    if (fast) stage_.finish();
    stage_.setStill(!ctx_.settings.animations); // (animations off: still)
    stage_.update(dt);
    for (Caption& c : captions_) c.left -= dt;
    std::erase_if(captions_, [](const Caption& c) { return c.left <= 0.0f || c.text.empty(); });
    typingSoundTimer_ -= dt;
    clock_ += dt;
    lineTime_ += dt;
    typing_ = false;
    const float cps = static_cast<float>(ctx_.settings.textSpeed);
    for (BoxLine& l : others_) // lines in other textboxes type on, each at its textbox's speed
        l.reveal = fast || cps <= 0.0f ? static_cast<float>(l.length) : std::min(static_cast<float>(l.length), l.reveal + cps * dt);

    switch (wait_) {
    case Wait::None:
        run();
        break;
    case Wait::Say:
        updateLine(dt, fast);
        break;
    case Wait::Background:
        if (!stage_.backgroundComing()) {
            wait_ = Wait::None;
            run();
        }
        break;
    case Wait::Move:
        if (!stage_.moving()) {
            wait_ = Wait::None;
            run();
        }
        break;
    case Wait::Timer:
        if ((timer_ -= dt) <= 0.0f || fast) {
            wait_ = Wait::None;
            run();
        }
        break;
    case Wait::Clock: // the player's clock gets there (setting time real): the story goes on
        if (const auto until = vars_.find("time_wait"); until == vars_.end() || realNow().seconds >= toNumber(until->second)) {
            vars_.erase("time_wait");
            vars_.erase("time_wait_line");
            text_.clear();
            wait_ = Wait::None;
            timeMoved(waitFrom_, timeNow(), false);
            if (wait_ == Wait::None) run();
        }
        break;
    case Wait::Pause: // until a click, or auto-forward's delay
        if (fast || (auto_ && (autoTimer_ += dt) >= static_cast<float>(ctx_.settings.autoDelay) / 1000.0f)) nextLine();
        break;
    case Wait::Movie:
        if (movie_) movie_->update(dt);
        if ((movie_ ? movie_->ended() : (timer_ -= dt) <= 0.0f) || (fast && movieSkippable_)) endMovie();
        break;
    case Wait::Menu:
        if (menu()->seconds > 0.0f && ctx_.settings.timeLimits && (menuTime_ -= dt) <= 0.0f) letPass(); // time's up (Time limits off: it waits)
        break;
    case Wait::Chat: // a chat scene: until the message comes (skipping: at once), or the change is made
        if (change_) {
            if ((changeIn_ -= dt) > 0.0f && !fast) break;
            change(*std::exchange(change_, std::nullopt));
        } else if (const auto& messages = phone_.messages; !fast && std::ranges::any_of(messages, [&](const MessageItem& m) {
                       return m.number == chatWait_ && m.typing > 0.0f;
                   })) {
            break;
        } else if (fast) {
            hurry();
        }
        wait_ = Wait::None;
        run();
        break;
    case Wait::Call: // (updatePhone() rings it)
    case Wait::Screen:
    case Wait::Reached: // (stopBefore: it's saved from there, not played on)
    case Wait::Stopped: // (stopWhere: until goOn())
    case Wait::Finished:
        break;
    }
    ctx_.sound.holdVoice(voiceAtPause());
    updateSpeech();
}

std::vector<Game::Upcoming> Game::upcomingImages(int commands) const {
    std::vector<Upcoming> out;
    unsigned looks = stage_.effect().looks & kColourLooks; // the colour effects on stage from there on
    std::vector<std::string> stage;                       // what's on it
    std::vector<std::string> people;                      // the characters on it (a line can change their look)
    auto imageOf = [&](const std::string& key, const std::string& expression) {
        const CharacterDef* def = script_.character(key);
        return def ? def->imageOf(expression) : std::string{};
    };
    auto show = [&](std::string name) {
        if (name.empty()) return;
        if (looks != 0) out.push_back({name, looks});
        out.push_back({name, 0}); // (as it is too: for when the effect fades)
        stage.push_back(std::move(name));
    };
    if (!stage_.background().starts_with('#')) stage.push_back(stage_.background());
    for (const Stage::ObjectState& o : stage_.objects()) {
        stage.push_back(o.character.empty() ? o.image : imageOf(o.character, o.expression));
        if (!o.character.empty()) people.push_back(o.character);
    }
    const std::size_t end = std::min(script_.code.size(), resumePc_ + static_cast<std::size_t>(std::max(0, commands)));
    for (std::size_t pc = resumePc_; pc < end; ++pc)
        std::visit(Overloaded{
                       [&](const cmd::Background& c) {
                           stage.clear();
                           people.clear();
                           if (!c.image.starts_with('#')) show(c.image);
                       },
                       [&](const cmd::Show& c) {
                           show(c.character.empty() ? c.image : imageOf(c.character, c.expression));
                           if (!c.character.empty()) people.push_back(c.character);
                       },
                       [&](const cmd::Say& c) {
                           if (c.character.empty()) return;
                           if (!c.expression.empty() && std::ranges::contains(people, c.character)) show(imageOf(c.character, c.expression));
                           if (const CharacterDef* def = script_.character(c.character); def && !def->face.empty()) // (in the textbox: no effects)
                               out.push_back({def->faceOf(c.expression)});
                       },
                       [&](const cmd::Effect& c) { // what's on stage then is drawn in its colours (for a moment, or from then on)
                           const unsigned with = (c.seconds > 0.0f ? looks | c.looks : c.looks) & kColourLooks;
                           if (with != 0)
                               for (const std::string& name : stage) out.push_back({name, with});
                           if (c.seconds <= 0.0f) looks = with;
                       },
                       [](const auto&) {},
                   },
                   script_.code[pc].command);
    return out;
}

void Game::draw(const Font& labelFont) {
    if (wait_ == Wait::Movie) { // a movie covers everything, as big as fits
        SDL_Renderer* r = ctx_.renderer;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderFillRect(r, nullptr);
        if (SDL_Texture* frame = movie_ ? movie_->texture() : nullptr) {
            const float scale = std::min(ctx_.canvas.w / movie_->width(), ctx_.canvas.h / movie_->height());
            const float w = movie_->width() * scale, h = movie_->height() * scale;
            const SDL_FRect dst{(ctx_.canvas.w - w) * 0.5f, (ctx_.canvas.h - h) * 0.5f, w, h};
            SDL_RenderTexture(r, frame, nullptr, &dst);
        } else if (!movieStandIn_.empty()) { // (developer mode) the movie that's missing
            movieLabel_.set(labelFont, std::format("movies/{}.mpg", movieStandIn_)); // (the kind that plays everywhere, as a missing image says .png)
            const SDL_FPoint size = movieLabel_.size();
            movieLabel_.draw(r, (ctx_.canvas.w - size.x) * 0.5f, (ctx_.canvas.h - size.y) * 0.5f, theme::dim);
        }
        return;
    }
    stage_.draw(ctx_.renderer, ctx_.assets, labelFont, gameSettings().developer, gameSettings().characterHeight,
                [this](const Stage::ObjectState& s) { return frame(s.character.empty() ? s.tag : s.character); });
}

} // namespace vn
