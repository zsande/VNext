// The phone: its chats and messages, calls (ringing, answered, missed), the people in it, its apps and lock
// screen, and the chat and call scenes that play on it.
#include "Engine.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace vn {

namespace {

constexpr std::string_view kRinging = "phone ringing"; // (the sound channel a call rings on)

} // namespace

Game::Person Game::person(const std::string& character, const std::string& sender) const {
    if (const CharacterDef* def = character.empty() ? nullptr : script_.character(character)) return {.name = def->name, .character = def->key, .colour = def->nameColour};
    return {.name = sender, .colour = gameSettings().narrationColour};
}

Game::Person Game::personNamed(std::string_view name) const {
    for (const auto& [key, def] : script_.characters)
        if (def.name == name) return {.name = def.name, .character = key, .colour = def.nameColour};
    return {.name = std::string(name), .colour = gameSettings().narrationColour};
}

// A chat scene's message on the phone, or a change to one: it comes at once, or typed first (as setting
// chat_messages says), and the scene waits for it: until it comes, or for a click once it's there.
void Game::sendMessage(const cmd::Message& c, std::size_t here) {
    using Kind = cmd::Message::Kind;
    if (c.kind == Kind::End) return change(c); // (the scene's over: its line at once)
    const bool scene = !skipping() && !chat_.empty();
    const bool typed = gameSettings().chatMessages == ChatMessages::Typed;
    if (!isMessage(c.kind) && c.kind != Kind::Typing) { // (a change to one)
        if (scene && typed) { // (after a moment, as if they thought it over)
            change_ = c;
            changeIn_ = 1.0f;
            return waitFor(Wait::Chat, here);
        }
        change(c);
        if (scene) { // (each after a click)
            autoTimer_ = 0.0f;
            wait_ = Wait::Pause;
        }
        return;
    }
    if (c.kind == Kind::Typing && skipping()) return; // (skipping: nobody waits for it)
    const Person who = person(c.character, c.sender); // (the player's own: who it goes to)
    const std::string& with = who.name;
    const std::string from = c.mine ? std::string{} : who.name;
    MessageItem m{.kind = c.kind, .from = from, .with = with, .character = c.mine ? std::string{} : who.character, .expression = c.expression,
                  .colour = c.mine ? kOwnColour : who.colour, .text = fillValues(c.text, lookup_), .file = c.file, .read = c.mine};
    // How long it takes: someone typing a while; someone else's (as long as typing it takes, after their
    // others); the player's own a moment; skipping, no time at all. A chat= scene's messages, sent at once, come
    // one after another: each after the ones still coming there.
    float after = 0.0f;
    if (replying_ && !skipping())
        for (const MessageItem& other : phone_.messages)
            if (other.with == with) after = std::max(after, other.typing);
    if (c.kind == Kind::Typing) {
        m.typing = after + (c.seconds > 0.0f ? c.seconds : 2.0f);
    } else if (!c.mine && ((scene && typed) || (replying_ && !skipping())) && gameSettings().phoneTyping > 0.0f) {
        for (const MessageItem& other : phone_.messages)
            if (other.with == with && other.from == from) m.typing = std::max(m.typing, other.typing);
        const std::size_t letters = c.kind == Kind::Text ? utf8::length(m.text) : 12; // (an image: as long as a few words)
        m.typing = std::max(m.typing, after) + 0.8f + static_cast<float>(letters) / gameSettings().phoneTyping * static_cast<float>(randomNumber(75, 125)) / 100.0f;
    } else if (c.mine && ((scene && typed) || (replying_ && !skipping()))) {
        m.typing = after + 0.4f;
    }
    const std::uint32_t number = messageCount_ + 1;
    addMessage(std::move(m));
    if (!scene) return;
    if (const MessageItem& added = phone_.messages.back(); added.number == number && added.typing > 0.0f) { // a chat scene: until it comes...
        chatWait_ = number;
        wait_ = Wait::Chat; // (a save made meanwhile goes on after it: the phone has it, coming)
    } else if (!typed) { // ...or, each after a click, until one
        autoTimer_ = 0.0f;
        wait_ = Wait::Pause;
    }
}

// Deletes it, edits it, reacts, reads it: a change to a chat's messages.
void Game::change(const cmd::Message& c) {
    using Kind = cmd::Message::Kind;
    const Person who = person(c.character, c.sender);
    const std::string& with = who.name;
    const std::string from = c.mine ? std::string{} : who.name;
    auto last = [&](auto&& which) -> MessageItem* { // (the newest there that has come, and isn't deleted)
        for (auto it = phone_.messages.rbegin(); it != phone_.messages.rend(); ++it)
            if (it->with == with && it->typing <= 0.0f && isMessage(it->kind) && !it->deleted && which(*it)) return &*it;
        return nullptr;
    };
    switch (c.kind) {
    case Kind::Delete:
        if (MessageItem* m = last([&](const MessageItem& item) { return item.from == from; })) m->deleted = true;
        break;
    case Kind::Edit:
        if (MessageItem* m = last([&](const MessageItem& item) { return item.from == from && item.kind == Kind::Text; })) {
            m->text = fillValues(c.text, lookup_);
            m->edited = true;
        }
        break;
    case Kind::React: // (on the newest someone else sent)
        if (MessageItem* m = last([&](const MessageItem& item) { return item.from != from; })) m->reaction = fillValues(c.text, lookup_);
        break;
    case Kind::Read: // someone has read the owner's messages there (the owner reading theirs: they're seen)
        for (MessageItem& m : phone_.messages)
            if (m.with == with && m.typing <= 0.0f) {
                if (!from.empty() && m.from.empty()) m.seen = true;
                else if (from.empty() && !m.from.empty()) m.read = true;
            }
        break;
    case Kind::End: // (a chat scene over: a line saying so)
        addMessage({.kind = c.kind, .with = with, .read = true});
        break;
    default:
        break;
    }
}

void Game::addMessage(MessageItem m) {
    m.number = ++messageCount_;
    MessageItem& added = phone_.messages.emplace_back(std::move(m));
    if (added.typing <= 0.0f) arrive(added);
    else if (!added.from.empty() && !skipping() && hasPhone() && !gameSettings().phoneTypingSound.empty())
        ctx_.sound.playSound(ctx_.assets.sound(gameSettings().phoneTypingSound));
}

// A message comes to the phone: its tips are learned, an image goes in the photos (and the gallery), its sound
// plays, the history has it (setting phone_history), and the player hears of it as setting new_messages says (not
// of a chat scene's: it's on the phone already, nor of the owner's own).
void Game::arrive(MessageItem& m) {
    m.time = toString(variable("phone_clock"));
    m.date = toString(variable("phone_date"));
    if (!isMessage(m.kind)) return;
    learnTips(m.text);
    if (m.kind == MessageItem::Kind::Image) have(RecordKind::Gallery, m.file);
    const bool fast = skipping();
    if (gameSettings().phoneHistory && !unlogged_) { // (as a line, the owner's own as a choice made)
        const std::string words = m.text.empty() ? m.file : richText(m.text, lookup_, &script_.palette).text;
        if (m.from.empty()) addHistory({.speaker = "", .colour = SDL_Color{150, 150, 165, 255}, .text = "> " + words, .choice = true});
        else addHistory({.speaker = m.from, .colour = m.colour, .text = words});
    }
    if (m.from.empty()) {
        if (!fast && !gameSettings().phoneSendSound.empty()) ctx_.sound.playSound(ctx_.assets.sound(gameSettings().phoneSendSound));
        return;
    }
    ++arrivals_;
    if (!hasPhone()) return; // (no phone to come to)
    if (!fast && ctx_.settings.readAloud) { // (who sent it, and its words: an image's or a voice message's caption, else its file's name)
        std::string words = m.text.empty() ? stemOf(m.file.substr(m.file.rfind('/') + 1)) : richText(m.text, lookup_, &script_.palette).text;
        std::ranges::replace(words, '_', ' ');
        news_.push_back(m.from + ": " + words);
    }
    if (!fast && !gameSettings().phoneMessageSound.empty()) ctx_.sound.playSound(ctx_.assets.sound(gameSettings().phoneMessageSound));
    if (m.with == chat_) return; // (a chat scene's: on the phone already)
    if (!fast) requests_.push_back(cmd::Ui{.verb = "buzz"}); // (controllers buzz, as a phone does)
    if (gameSettings().newMessages == NewMessages::Open && !fast) // (the engine opens the phone on it, unless it's up already; not while skipping past)
        requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "message", .phone = m.with}); // (button: the Phone button counts it)
}

namespace {

// Someone's character's chat= or call= scene: where its lines start.
std::optional<std::size_t> phoneSceneOf(const Script& script, std::string_view who, bool call) {
    for (const auto& [key, def] : script.characters) {
        if (def.name != who) continue;
        const auto it = script.phoneScenes.find(call ? def.callScene : def.chatScene);
        return it != script.phoneScenes.end() ? std::optional(it->second) : std::nullopt;
    }
    return std::nullopt;
}

} // namespace

void Game::updatePhone(float dt) {
    for (std::size_t i = 0; i < phone_.messages.size(); ++i) {
        MessageItem& m = phone_.messages[i];
        if (m.typing <= 0.0f || (m.typing -= dt) > 0.0f) continue;
        m.typing = 0.0f;
        if (m.kind == MessageItem::Kind::Typing) phone_.messages.erase(phone_.messages.begin() + static_cast<std::ptrdiff_t>(i--)); // (they stopped)
        else arrive(m);
    }
    if (!putAway_.empty()) { // (a chat= scene over: once its messages have come, and the last could be read)
        if (std::ranges::any_of(phone_.messages, [&](const MessageItem& m) { return m.with == putAway_ && m.typing > 0.0f; })) putAwayIn_ = 2.0f;
        else if ((putAwayIn_ -= dt) <= 0.0f) {
            putAway_.clear();
            requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "chat"});
        }
    }
    if (!call_) return;
    call_->time += dt;
    if (call_->talking) return;
    // Ringing: one coming rings until its time is over (then it's missed); the player's rings a moment, and is
    // declined. A declined one (either) says Busy a moment first.
    if (call_->busy) { // (declined: Busy a moment, then it's over)
        if (call_->time >= 1.5f) endCall(false);
        return;
    }
    if (!call_->outgoing) {
        if (call_->rings > 0.0f && ctx_.settings.timeLimits && call_->time >= call_->rings) { // (Time limits off: it rings on)
            stopRinging();
            endCall(true);
        }
        return;
    }
    if (call_->time < 2.5f) return;
    stopRinging();
    call_->time = 0.0f;
    if (!call_->talks) { // (no call scene: they decline it)
        call_->busy = true;
        return;
    }
    call_->talking = true; // (they answer: their call scene plays)
    if (const auto at = phoneSceneOf(script_, call_->who, true)) talk(*at, {}, true);
}

// The options of the choice waiting on the phone, if it's this chat's or call's.
std::vector<ChoiceItem> Game::phoneChoices(std::string_view who, bool call) const {
    std::vector<ChoiceItem> out;
    const auto* m = parked_ && parked_->who == who && parked_->call == call ? std::get_if<cmd::Menu>(&script_.code[parked_->menu].command) : nullptr;
    for (std::size_t k = 0; m && k < m->choices.size(); ++k)
        if (auto item = offer(m->choices[k], static_cast<int>(k))) out.push_back(std::move(*item));
    return out;
}

// The player picks an option of the choice waiting on the phone: in a chat it's sent there as theirs (on a call, the
// history has it, as a choice's), and the scene goes on with its lines.
void Game::pickOnPhone(std::size_t option) {
    const auto* m = parked_ && !asking() ? std::get_if<cmd::Menu>(&script_.code[parked_->menu].command) : nullptr;
    if (!m || option >= m->choices.size() || !hasSignal()) return;
    const cmd::Choice& picked = m->choices[option];
    if (picked.condition && !picked.condition->test(lookup_)) return;
    ctx_.read.insert(picked.id); // (picked before, from now on)
    const Parked p = *std::exchange(parked_, std::nullopt);
    if (p.call) {
        RichText chosen = richText(picked.text, lookup_, &script_.palette);
        for (TextSpan& span : chosen.spans) span.begin += 2, span.end += 2; // after "> "
        addHistory({.speaker = "", .colour = SDL_Color{150, 150, 165, 255}, .text = "> " + chosen.text, .choice = true, .spans = std::move(chosen.spans)});
    } else {
        addMessage({.with = p.who, .colour = kOwnColour, .text = fillValues(picked.text, lookup_), .read = true});
    }
    talk(picked.target, p.call ? std::string{} : p.who, p.call);
}

// Lines of someone's chat= or call= scene, run aside: the story stays on its line, shown again after them if they
// showed something (a call scene's end ends the call; a choice waits on the phone).
void Game::talk(std::size_t target, const std::string& chat, bool call, bool passing) {
    asked_ = Asked{.depth = callStack_.size() + 1, .page = page_.size(), .call = call, .passing = passing, .chat = chat}; // (runAside's own frame: while it's there, they run)
    putAway_.clear();
    replying_ = !chat.empty(); // (a chat's: messages typed one after another)
    runAside(target);
    replying_ = false;
    if (asking()) asked_->shown = true; // (still under way: they show something)
    else asked_.reset();
}

// The player hangs up: the rest of their call scene first (its choices passed), then the call's over.
void Game::hangUp() {
    if (!call_ || !call_->outgoing || !call_->talking || asking()) return;
    const auto* m = parked_ && parked_->call ? std::get_if<cmd::Menu>(&script_.code[parked_->menu].command) : nullptr;
    parked_.reset();
    if (m) talk(m->after, {}, true, true);
    else callOver();
}

void Game::callOver() {
    logCall(*call_, false);
    call_.reset();
    if (parked_ && parked_->call) parked_.reset();
    requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "call"});
}

// The player's chat open on the phone now (none: ""): its chat= scene plays as it's opened; left at a choice, the rest
// of it (its choices passed). Not while a chat scene or a call is on (its choice waiting is dropped), nor without signal.
void Game::chatOpen(const std::string& with) {
    const std::string now = chat_.empty() && !call_ ? with : std::string{};
    if (now == chatOpen_) return;
    const bool can = hasPhone() && hasSignal() && chat_.empty() && !call_;
    if (parked_ && !parked_->call && parked_->who == chatOpen_) {
        const auto* m = std::get_if<cmd::Menu>(&script_.code[parked_->menu].command);
        parked_.reset();
        if (m && can && !asking()) talk(m->after, chatOpen_, false, true);
    }
    chatOpen_ = now;
    if (const auto at = now.empty() || !can || asking() ? std::nullopt : phoneSceneOf(script_, now, false)) talk(*at, now, false);
}

// A call: the phone comes up on it, ringing, until it's answered or declined (or its time is over); the
// player's is answered after a moment, if it has lines. No phone, or no signal: nobody gets through.
void Game::startCall(const cmd::Call& c, std::size_t here) {
    const Person who = person(c.character, c.sender);
    const CallNow now{.who = who.name, .character = who.character, .colour = who.colour, .rings = c.seconds, .at = here};
    if (!hasPhone() || !hasSignal()) {
        logCall(now, true);
        pc_ = c.missed;
        return;
    }
    call_ = now;
    requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "call"});
    if (skipping()) skip_ = false; // (one to answer stops skipping, as a choice does)
    const CharacterDef* def = who.character.empty() ? nullptr : script_.character(who.character);
    const std::string& tone = def && !def->ringtone.empty() ? def->ringtone : gameSettings().phoneRingtone;
    if (!tone.empty()) ctx_.sound.playChannel(std::string(kRinging), ctx_.assets.sound(tone), true, 1.0f, 0.0f);
    waitFor(Wait::Call, here); // (a save made meanwhile rings again)
}

void Game::answerCall(bool answer) {
    if (!call_ || call_->talking || call_->busy || call_->outgoing) return;
    stopRinging();
    if (!answer) { // (declined: Busy a moment, then it's over)
        call_->busy = true;
        call_->time = 0.0f;
        return;
    }
    const cmd::Call* c = call_->at ? std::get_if<cmd::Call>(&script_.code[*call_->at].command) : nullptr;
    call_->talking = true;
    call_->time = 0.0f;
    if (c && wait_ == Wait::Call) {
        wait_ = Wait::None;
        pc_ = *call_->at + 1;
        run();
    }
}

// A call over without being answered: missed (it rang out) or busy (declined); a story's goes on to its missed: lines.
void Game::endCall(bool missed) {
    const cmd::Call* c = call_->at ? std::get_if<cmd::Call>(&script_.code[*call_->at].command) : nullptr;
    const bool story = c && wait_ == Wait::Call;
    logCall(*call_, missed);
    call_.reset();
    requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "call"});
    if (story) {
        wait_ = Wait::None;
        pc_ = c->missed;
        run();
    }
}

void Game::logCall(const CallNow& c, bool missed) {
    phone_.calls.push_back({.who = c.who, .colour = c.colour, .outgoing = c.outgoing, .missed = missed, .busy = c.busy, .seen = !missed || c.outgoing,
                                .time = toString(variable("phone_clock")), .date = toString(variable("phone_date")), .seconds = missed || c.busy ? 0 : static_cast<int>(std::lround(c.time))});
}

void Game::stopRinging() { ctx_.sound.stopChannel(std::string(kRinging), 0.1f); }

void Game::callPerson(std::string_view who) {
    if (call_ || !chat_.empty() || !hasPhone() || !hasSignal()) return;
    const Person p = personNamed(who); // (it rings a while, then it's answered if they have a call scene, else busy)
    call_ = CallNow{.who = p.name, .character = p.character, .colour = p.colour, .outgoing = true, .talks = phoneSceneOf(script_, p.name, true).has_value()};
    if (!gameSettings().phoneCallingSound.empty()) ctx_.sound.playChannel(std::string(kRinging), ctx_.assets.sound(gameSettings().phoneCallingSound), true, 1.0f, 0.0f);
    requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "call"}); // (on the call, from a key or a button outside it too)
}

bool Game::choiceOnPhone() const {
    const cmd::Menu* m = wait_ == Wait::Menu ? menu() : nullptr;
    return m && !m->phone.empty();
}

// Lines from `target` (a chat= or call= scene's), run as if called from where the story is: lines that show nothing
// (set, a message, a sound...) leave it there; ones that do play, and the story comes back to its line after them.
void Game::runAside(std::size_t target) {
    const Wait was = wait_;
    const std::size_t pc = pc_, resume = resumePc_, current = current_;
    callStack_.push_back(resumePc_);
    const std::size_t depth = callStack_.size();
    pc_ = target;
    wait_ = Wait::None;
    for (int budget = kInstructionBudget; wait_ == Wait::None && callStack_.size() >= depth;) {
        if (pc_ >= script_.code.size()) wait_ = Wait::Finished;
        else if (--budget == 0) fail("A phone scene's lines ran a very long time without showing anything (does a jump go round in a circle?)"_tr);
        else {
            const std::size_t here = current_ = pc_++;
            resumePc_ = pc_;
            execute(script_.code[here].command, here);
        }
    }
    if (wait_ != Wait::None) return;
    wait_ = was;
    pc_ = pc;
    resumePc_ = resume;
    current_ = current;
}

void Game::readMessages(std::string_view with) {
    for (MessageItem& m : phone_.messages)
        if (with.empty() || m.with == with) m.read = m.read || m.typing <= 0.0f;
}

std::vector<Game::Chat> Game::chats() const {
    std::vector<Chat> out;
    auto entry = [&](const std::string& name) -> Chat& {
        if (const auto it = std::ranges::find(out, name, &Chat::name); it != out.end()) return *it;
        const Person p = personNamed(name);
        return out.emplace_back(Chat{.name = name, .colour = p.colour, .character = p.character});
    };
    for (std::size_t i = 0; i < phone_.messages.size(); ++i) {
        const MessageItem& m = phone_.messages[i];
        Chat& c = entry(m.with);
        if (m.typing > 0.0f) c.typing = c.typing || !m.from.empty();
        else if (m.kind != MessageItem::Kind::End) c.last = i; // (a chat scene's end isn't its newest message)
        if (!m.read && m.typing <= 0.0f && !m.from.empty() && isMessage(m.kind)) ++c.unread;
    }
    std::ranges::stable_sort(out, std::greater{}, [](const Chat& c) { return c.last ? static_cast<long long>(*c.last) : -1LL; });
    return out;
}

void Game::seeCalls() {
    for (CallItem& c : phone_.calls) c.seen = true;
}

int Game::unseenMessages() const {
    return static_cast<int>(std::ranges::count_if(phone_.messages, [](const MessageItem& m) { return !m.read && m.typing <= 0.0f && !m.from.empty() && isMessage(m.kind); }));
}

int Game::unseenCalls() const { return static_cast<int>(std::ranges::count(phone_.calls, false, &CallItem::seen)); }

bool Engine::showsPhone(std::string_view screen) {
    const ScreenView* v = view(screen);
    return v && std::ranges::any_of(v->def().elements, [](const Element& el) { return el.kind == ElementKind::Container && !el.option("app").empty(); });
}

// The phone screen (from the story): on the chat a chat scene is in, a call, or a message that came; straight
// there (no lock screen).
void Engine::showPhone(const cmd::Ui& c) {
    const ScreenDef* def = script_.screen("phone");
    if (!def || !playing()) return;
    const bool up = std::ranges::contains(overlays_, std::string("phone"));
    if (c.arg == "message" && up) return; // (up already: it shows there)
    if (!c.phone.empty()) { // its chat, in the app it's in
        if (std::string app = appHolding(*def, "messages"); !app.empty() || !(app = appHolding(*def, "chats")).empty()) phone_.app = std::move(app);
        phone_.chat = c.phone;
        phone_.image.clear();
    }
    if (c.arg == "call") phone_.image.clear();
    phone_.locked = false;
    if (up) return;
    phone_.straight = true;
    open("phone");
    phone_.straight = false;
}

// A chat or a call over: the phone's put away, whoever opened it.
void Engine::hidePhone() {
    if (!playing() || game_->phoneBusy()) return;
    if (!overlays_.empty() && overlays_.back() == "phone") return close();
    if (const auto it = std::ranges::find(overlays_, std::string("phone")); it != overlays_.end()) { // (something over it: it goes from under that)
        overlays_.erase(it);
        if (ScreenView* v = view("phone")) screenHook(v->def(), false);
    }
}

// An app's on the phone while its when= is true (none: always).
bool Engine::appOn(const Element& app) const {
    return !app.visible || app.visible->test(lookup());
}

std::string Engine::passcode() const { return game_ ? toString(game_->variable("phone_passcode")) : std::string{}; }

namespace {

// The phone's app called `name`: a container with app=NAME.
const Element* appIn(const ScreenDef& def, std::string_view name) {
    const auto it = std::ranges::find_if(def.elements, [&](const Element& el) { return el.kind == ElementKind::Container && el.option("app") == name; });
    return it != def.elements.end() ? &*it : nullptr;
}

} // namespace

std::string Engine::phoneApp(const ScreenDef& def) const {
    if (game_->call() && !phone_.pinned && appIn(def, "call")) return "call";
    if (phone_.locked && appIn(def, "lock")) return "lock";
    if (const Element* open = phone_.app.empty() ? nullptr : appIn(def, phone_.app); open && appOn(*open)) return phone_.app;
    if (appIn(def, "home")) return "home";
    for (const Element& el : def.elements)
        if (const std::string name = el.option("app"); el.kind == ElementKind::Container && !name.empty() && name != "call" && name != "lock" && appOn(el)) return name;
    return {};
}

void Engine::openApp(const ScreenDef& def, std::string name) {
    const std::string now = phoneApp(def);
    if (name == now) return;
    if (!now.empty() && now != "call" && now != "lock") phone_.back.push_back(now);
    if (phone_.back.size() > 20) phone_.back.erase(phone_.back.begin());
    phone_.app = std::move(name);
    phone_.chat.clear();
    phone_.image.clear();
}

void Engine::back() {
    if (!phoneBack()) close();
}

bool Engine::phoneBack() {
    if (!phone_.image.empty()) return phone_.image.clear(), true;
    if (playing() && game_->phoneBusy()) { // (a chat scene or a call keeps the phone where it is; in a chat scene, Back goes on as a click does)
        if (!game_->chatScene().empty() && !game_->choiceOnPhone()) game_->nextLine();
        return true;
    }
    if (!phone_.chat.empty()) return phone_.chat.clear(), true;
    if (!phone_.back.empty()) {
        phone_.app = phone_.back.back();
        phone_.back.pop_back();
        return true;
    }
    if (const ScreenDef* def = script_.screen("phone"))
        if (appIn(*def, "home") && phoneApp(*def) != "home" && !phone_.locked) return phone_.app = "home", true;
    return false;
}

bool Engine::unlockPhone(std::string_view code) {
    if (playing() && !passcode().empty() && code != passcode()) return false; // (it's asked for each time: the phone locks whenever it's put away)
    phone_.locked = false;
    return true;
}

std::string Engine::appHolding(const ScreenDef& def, std::string_view list) const {
    for (const Element& el : def.elements) {
        if (el.kind != ElementKind::Container || el.option("list") != list) continue;
        for (int p = el.parent; p >= 0; p = def.elements[static_cast<std::size_t>(p)].parent)
            if (const std::string app = def.elements[static_cast<std::size_t>(p)].option("app"); !app.empty()) return app;
        return {};
    }
    return {};
}

void Engine::openChat(const ScreenDef& def, std::string with) {
    std::string app = appHolding(def, "messages");
    if (app.empty()) app = appHolding(def, "chats");
    if (!app.empty()) openApp(def, std::move(app));
    phone_.chat = std::move(with);
    phone_.image.clear();
}

void Engine::callPerson(const std::string& who) {
    if (sampling_) return;
    if (mode_ == Mode::Game) game_->callPerson(who);
}

void Engine::pickOnPhone(std::size_t option) {
    if (sampling_) return;
    if (mode_ == Mode::Game) game_->pickOnPhone(option);
}

void Engine::answerCall(bool answer) {
    if (sampling_) return;
    if (mode_ == Mode::Game) game_->answerCall(answer);
}

void Engine::keypad(std::string_view key) {
    if (key == "back") {
        if (!phone_.code.empty()) phone_.code.pop_back();
        return;
    }
    phone_.code += key;
    if (utf8::length(phone_.code) >= (playing() ? std::max<std::size_t>(1, utf8::length(passcode())) : 4)) {
        if (!unlockPhone(phone_.code)) phone_.wrongAt = time_;
        phone_.code.clear();
    }
}

} // namespace vn
