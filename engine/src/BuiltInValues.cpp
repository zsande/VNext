// The values the player has for texts and conditions ({speaker}, {can_save}, {time}...).
#include "Engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

namespace vn {

// Names the engine knows: usable in conditions and {texts} like variables.
std::optional<Value> Engine::builtin(std::string_view name) const {
    auto flag = [](bool b) { return std::optional<Value>(b ? 1 : 0); };
    const Game* g = game_.get();
    const bool playing = mode_ == Mode::Game && g;
    if (name == "in_game") return flag(playing);
    if (name == "has_save") return flag(hasSave_);
    if (name == "has_quick_save") return flag(hasQuickSave_);
    if (name == "can_save") return flag(playing && g->canSave());
    if (name == "can_rollback") return flag(playing && g->canRollback());
    if (name == "skip") return flag(playing && g->skipping());
    if (name == "auto") return flag(playing && g->autoMode());
    if (name == "speaking") return flag(playing && g->showingSay());
    if (name == "choosing") return flag(playing && g->showingChoices());
    if (name == "prompt") return flag(playing && g->menuPrompt());
    if (name == "text_complete") return flag(playing && g->textComplete());
    if (name == "time_left") { // a timed choice's time, or the time before the screen on top closes by itself: 100 down to 0
        const ScreenDef* top = overlays_.empty() ? nullptr : script_.screen(overlays_.back());
        const std::optional<float> left = top && top->closeAfter > 0.0f ? std::optional(1.0f - (time_ - topSince_) / top->closeAfter)
                                        : playing ? g->timeLeft() : std::nullopt;
        return static_cast<int>(std::lround(std::clamp(left.value_or(0.0f), 0.0f, 1.0f) * 100.0f));
    }
    if (name == "tip" || name == "tip_title" || name == "tip_text") { // the tip being read (none after forgetting progress)
        const Script::Record* tip = tip_ && has(RecordKind::Tip, *tip_) ? &script_.records(RecordKind::Tip)[*tip_] : nullptr;
        return !tip ? std::string{} : name == "tip" ? tip->name : name == "tip_title" ? tip->title : tip->text;
    }
    if (name == "flowchart_points") return static_cast<int>(flowchart_.nodes().size());
    // How many images, endings, songs, achievements and tips there are, and how many the player has.
    static constexpr std::array<std::pair<std::string_view, std::string_view>, kRecordKinds.size()> counts{
        {{"gallery_total", "gallery_unlocked"}, {"endings_total", "endings_reached"}, {"songs_total", "songs_heard"},
         {"achievements_total", "achievements_earned"}, {"tips_total", "tips_found"}}};
    for (std::size_t k = 0; k < counts.size(); ++k) {
        if (name == counts[k].first) return static_cast<int>(script_.lists[k].size());
        if (name == counts[k].second) return count(static_cast<RecordKind>(k));
    }
    // The phone: its messages (proper ones: not someone typing), where it is, what's new on it, its call...
    if (name == "messages_total")
        return g ? static_cast<int>(std::ranges::count_if(g->messages(), [](const MessageItem& m) {
                       return m.typing <= 0.0f && m.kind != MessageItem::Kind::End;
                   }))
                 : 0;
    // How many chats, calls and people (characters whose contact= is true) the phone has, and messages the open
    // chat has: a ready-made app says so when it has none.
    if (name == "chats_total") return g ? static_cast<int>(g->chats().size()) : 0;
    if (name == "open_chat_messages")
        return g ? static_cast<int>(std::ranges::count_if(g->messages(), [&](const MessageItem& m) { return m.with == phone_.chat && m.kind != MessageItem::Kind::End; })) : 0;
    if (name == "calls_total") return g ? static_cast<int>(g->phone().calls.size()) : 0;
    if (name == "people_total") {
        const Lookup look = [&](std::string_view n) { return g ? g->variable(n) : Value{0}; };
        return static_cast<int>(std::ranges::count_if(script_.characters, [&](const auto& c) { return c.second.contact && c.second.contact->test(look); }));
    }
    if (name == "open_chat") return phone_.chat;
    if (name == "phone_app") {
        const ScreenDef* def = script_.screen("phone");
        return g && def ? phoneApp(*def) : std::string{};
    }
    if (name == "has_phone") return flag(playing && g->hasPhone());
    if (name == "phone_button") // (the textbox's Phone button: a phone to open, not open already; with new_messages button its count says a message came)
        return flag(playing && g->hasPhone() && script_.screen("phone") && !std::ranges::contains(overlays_, std::string("phone")));
    if (name == "menu_button") return flag(playing && script_.screen("pause") && !std::ranges::contains(overlays_, std::string("pause"))); // (the textbox's Menu button: a pause menu to open)
    if (name == "messages_unread") return g ? g->unseenMessages() : 0; // (not the ones still being typed)
    if (name == "calls_missed") return g ? g->unseenCalls() : 0;
    if (name == "phone_unread") return g ? g->unseenMessages() + g->unseenCalls() : 0; // (what the Phone button counts)
    if (name == "phone_locked") return flag(phone_.locked);
    if (name == "in_call") return flag(g && g->call());
    if (name == "in_chat") return flag(playing && !g->chatScene().empty()); // (a chat scene is on)
    if (name == "caller") return g && g->call() ? g->call()->who : std::string{};
    if (name == "call_incoming" || name == "call_outgoing") // (ringing, not answered yet: one coming, or the player's)
        return flag(g && g->call() && !g->call()->talking && !g->call()->busy && g->call()->outgoing == (name == "call_outgoing"));
    if (name == "call_busy") return flag(g && g->call() && g->call()->busy); // (declined: it says so a moment)
    if (name == "call_choice") // (the player's call: a choice of their call scene's waiting on it)
        return flag(g && g->call() && g->call()->talking && g->waitsOnPhone(g->call()->who, true));
    if (name == "call_time") { // how long it has gone on: 1:05 (ringing: none)
        if (!g || !g->call() || !g->call()->talking) return std::string{};
        const int s = std::max(0, static_cast<int>(g->call()->time));
        return std::format("{}:{:02}", s / 60, s % 60);
    }
    if (name == "caller_avatar" || name == "caller_colour" || name == "caller_initial") { // (as a list's people have theirs: {chat.avatar}...)
        if (!g || !g->call()) return std::string{};
        const CallNow& call = *g->call();
        const CharacterDef* def = call.character.empty() ? nullptr : script_.character(call.character);
        if (name == "caller_initial") return call.who.empty() ? std::string{} : upper(std::string_view(call.who).substr(0, utf8::offset(call.who, 1)));
        if (name == "caller_colour") return std::format("#{:02x}{:02x}{:02x}", call.colour.r, call.colour.g, call.colour.b);
        if (!def || def->avatarOf({}).empty()) return std::string{};
        const std::string avatar = def->avatarOf(g->expressionOf(call.character)); // (their face's expression; without its own image, their usual one)
        return assets_->hasImage(avatar) ? avatar : def->avatarOf({});
    }
    if (name == "open_image") return phone_.image; // (open image NAME: the one looked at, from a chat or the gallery)
    if (name == "open_image_text") { // ...who sent it and their words, or its title in the gallery
        const MessageItem* sent = nullptr;
        if (g)
            for (const MessageItem& m : g->messages())
                if (m.kind == MessageItem::Kind::Image && m.file == phone_.image && m.typing <= 0.0f) sent = &m;
        if (sent) return sent->from.empty() ? sent->text : sent->text.empty() ? sent->from : sent->from + ": " + sent->text;
        const auto& gallery = script_.records(RecordKind::Gallery);
        const auto it = std::ranges::find(gallery, lower(phone_.image), &Script::Record::name);
        return it != gallery.end() ? it->title : std::string{};
    }
    if (name == "phone_needs_code") return flag(phone_.locked && !passcode().empty()); // (its lock screen's keypad is needed)
    if (name == "phone_code") { // the passcode so far: a filled dot for each digit, an empty one for each to go
        const std::size_t want = g ? utf8::length(passcode()) : 4, typed = utf8::length(phone_.code);
        std::string dots;
        for (std::size_t k = 0; k < std::max(want, typed); ++k) dots += k < typed ? "\u25CF" : "\u25CB";
        return dots;
    }
    if (name == "phone_wrong") return flag(phone_.wrongAt && time_ - *phone_.wrongAt < 1.5f); // (a wrong passcode was just tried)
    if (name == "keyboard_shift") return flag(keyboardShift()); // (the on-screen keyboard's Shift is on)
    if (name == "keyboard_text") return keyboardText();       // (what's typed into the box it's open for)
    if (name == "dragging") { // what the player holds, in the screen on top
        for (const auto& screen : overlays_ | std::views::reverse)
            if (auto it = views_.find(screen); it != views_.end()) return it->second.held();
        return std::string{};
    }
    if (name == "has_face") return flag(playing && g->showingSay() && !g->faceBase().empty());
    if (name == "face_side") return playing && g->showingSay() ? g->faceSide() : std::string{};
    if (name == "fullscreen") return flag(settings_.fullscreen);
    if (name == "developer") return flag(gameSettings().developer);
    if (name == "thumbnail") return flag(thumbnail_);
    if (name == "save_mode") return flag(slots_.mode() == SlotBrowser::Mode::Save);
    if (name == "title") return gameSettings().title;
    if (name == "language") return languageNames()[static_cast<std::size_t>(languageIndex())];
    if (name == "languages") return static_cast<int>(translations_.size()) + 1;
    if (name == "version") return gameSettings().version;
    if (name == "engine_version") return std::string(kVersion);
    if (name == "sdl_version") return sdlVersion();
    if (name == "speaker") return g ? g->speaker() : std::string{};
    if (name == "most_loved") { // (the love interest loved most now: of those even, the first; none: no love interests)
        std::string most;
        std::optional<double> best;
        for (const std::string& key : script_.loves)
            if (const CharacterDef* def = script_.character(key))
                if (const double love = g ? toNumber(g->variable(def->love)) : 0.0; !best || love > *best) {
                    best = love;
                    most = def->name;
                }
        return most;
    }
    if (name == "question") return question_;
    if (name == "page" && !slots_.pages().empty()) return slots_.pages()[slots_.page()];
    // The player's settings: a slider's as a number (text_speed 45, 0 at once; auto_delay 1.5 seconds; master_volume 80,
    // a percentage: screens say the unit), the window's size, or a number (yes: 1).
    if (name == "window_size") return std::format("{} x {}", settings_.windowWidth, settings_.windowHeight);
    if (const SettingControl* c = findSettingControl(name)) return number(c->number(settings_));
    if (const auto v = settings_.get(name)) return *v;
    return std::nullopt;
}

} // namespace vn
