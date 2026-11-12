// A screen's lists (a container with list=): what each list holds (the options, the history, the saves, the
// gallery, the phone's chats...) and the parts repeated for them.
#include "Engine.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <tuple>

namespace vn {

namespace {

// Empty slots and the pages for the editor's preview of a screen (it doesn't read the game's saves), so
// those lists can be laid out.
const std::vector<SlotBrowser::Card>& sampleCards() {
    static const std::vector<SlotBrowser::Card> cards = [] {
        std::vector<SlotBrowser::Card> out;
        for (int i = 0; i < 6; ++i) out.push_back({.slot = std::to_string(i + 1), .info = {}, .thumbnail = {}});
        return out;
    }();
    return cards;
}
const std::vector<std::string>& samplePages() {
    static const std::vector<std::string> pages{"auto", "quick", "1", "2", "3"};
    return pages;
}

} // namespace

namespace {

// Seconds as a call's clock shows them: "1:05".
std::string clockOf(float seconds) {
    const int s = std::max(0, static_cast<int>(seconds));
    return std::format("{}:{:02}", s / 60, s % 60);
}
// Dots for someone typing, one more every third of a second (still: all three).
std::string typingDots(float now, bool still) { return std::string(" . . .").substr(0, still ? 6 : 2 * (1 + static_cast<std::size_t>(now * 3.0f) % 3)); }
std::string initialOf(std::string_view name) { return name.empty() ? std::string{} : upper(name.substr(0, utf8::offset(name, 1))); }
int flag(bool b) { return b ? 1 : 0; }

} // namespace

const CharacterDef* ScreenView::characterNamed(std::string_view name) const {
    for (const auto& [key, def] : engine_.script().characters)
        if (def.name == name) return &def;
    return nullptr;
}

// A container with list=NAME: its parts once for each of the list's items, in a box of their own each, one
// after another (see layout()), with the item's values in what they show, their when= and what they do
// ({message.text}, when=message.mine, action="open chat {chat.name}").
void ScreenView::expand() {
    if (!hasLists_) return;
    // What the screen is now: each part (a list's once for each item, in its item's box), with its item's values.
    struct Planned {
        std::size_t source = SIZE_MAX; // the script's part (an item's box: none)
        std::size_t list = SIZE_MAX;   // ...an item's box: its list's
        int parent = -1;
        std::string key;
        std::shared_ptr<const Variables> values;
        std::shared_ptr<const Looks> looks;
    };
    const auto& parts = source_->elements;
    std::vector<std::vector<std::size_t>> in(parts.size());
    std::vector<std::size_t> top;
    for (std::size_t i = 0; i < parts.size(); ++i) (parts[i].parent >= 0 ? in[static_cast<std::size_t>(parts[i].parent)] : top).push_back(i);
    std::vector<Planned> plan;
    std::map<std::string, std::string, std::less<>> contexts;
    const auto walk = [&](const auto& self, std::size_t i, int parent, const std::shared_ptr<const Variables>& values, const std::shared_ptr<const Looks>& looks,
                          const std::string& path) -> void {
        const int at = static_cast<int>(plan.size());
        plan.push_back({.source = i, .parent = parent, .key = path + std::to_string(i), .values = values, .looks = looks});
        const Element& el = parts[i];
        if (el.kind != ElementKind::Container || el.option("list").empty()) {
            for (const std::size_t part : in[i]) self(self, part, at, values, looks, path);
            return;
        }
        std::string context;
        std::vector<ListItem> items = listItems(el, context);
        const std::string list = plan[static_cast<std::size_t>(at)].key;
        contexts.insert_or_assign(list, context);
        for (ListItem& item : items) {
            auto both = std::make_shared<Variables>(values ? *values : Variables{});
            for (auto& [name, value] : item.values) both->insert_or_assign(name, std::move(value));
            auto bothLooks = std::make_shared<Looks>(looks ? *looks : Looks{});
            for (auto& [name, spans] : item.looks) bothLooks->insert_or_assign(name, std::move(spans));
            const std::string key = list + "#" + item.key;
            const int box = static_cast<int>(plan.size());
            plan.push_back({.list = i, .parent = at, .key = key, .values = both, .looks = bothLooks});
            for (const std::size_t part : in[i]) self(self, part, box, both, bothLooks, key + "/");
        }
    };
    for (const std::size_t i : top) walk(walk, i, -1, nullptr, nullptr, "");

    bool same = plan.size() == keyOf_.size();
    for (std::size_t k = 0; same && k < plan.size(); ++k) same = plan[k].key == keyOf_[k];
    bound_.resize(plan.size());
    boundLooks_.resize(plan.size());
    for (std::size_t k = 0; k < plan.size(); ++k) bound_[k] = plan[k].values, boundLooks_[k] = plan[k].looks;
    if (same) {
        listContext_ = std::move(contexts);
        return;
    }

    // Made again: each part's state (scrolled, its texts, its animations...) stays with what it's called.
    ScreenDef next = *source_;
    next.elements.clear();
    next.elements.reserve(plan.size());
    for (const Planned& p : plan) {
        if (p.source != SIZE_MAX) {
            Element el = parts[p.source];
            el.parent = p.parent;
            next.elements.push_back(std::move(el));
            continue;
        }
        const Element& list = parts[p.list]; // (an item's box: none of its own to draw, its parts one after another as its list's stack= says)
        Element box{.kind = ElementKind::Container, .file = list.file, .line = list.line, .parent = p.parent};
        box.style.background = box.style.border = {0, 0, 0, 0};
        if (const std::string stack = list.option("stack"); !stack.empty()) box.props.insert_or_assign("stack", stack);
        box.itemAnimation = list.itemAnimation;
        next.elements.push_back(std::move(box));
    }
    std::unordered_map<std::string, std::size_t> was;
    for (std::size_t k = 0; k < keyOf_.size(); ++k) was.emplace(keyOf_[k], k);
    std::vector<ElementState> states(plan.size());
    std::vector<std::optional<std::size_t>> to(state_.size()); // (an old part: where it is now)
    for (std::size_t k = 0; k < plan.size(); ++k) {
        if (const auto it = was.find(plan[k].key); it != was.end() && it->second < state_.size()) {
            states[k] = std::move(state_[it->second]);
            to[it->second] = k;
            continue;
        }
        if (plan[k].source != SIZE_MAX) continue;
        // A new item: in a list that was showing the same chat, it plays item_animation= as it comes.
        const std::string& list = plan[static_cast<std::size_t>(plan[k].parent)].key;
        const auto before = listContext_.find(list), now = contexts.find(list);
        const auto shown = was.find(list);
        if (parts[plan[k].list].itemAnimation && before != listContext_.end() && now != contexts.end() && before->second == now->second && shown != was.end() &&
            drawn(shown->second) && !still())
            states[k].itemAt = this->now();
    }
    for (std::size_t k = 0; k < plan.size(); ++k) // (a list now of another chat: it and what it's in from the top, or the end with position=bottom)
        if (plan[k].source != SIZE_MAX && was.contains(plan[k].key))
            if (const auto before = listContext_.find(plan[k].key), now = contexts.find(plan[k].key);
                before != listContext_.end() && now != contexts.end() && before->second != now->second)
                for (int at = static_cast<int>(k); at >= 0; at = plan[static_cast<std::size_t>(at)].parent)
                    states[static_cast<std::size_t>(at)].scroll = 0.0f, states[static_cast<std::size_t>(at)].keptScroll = -1.0f, states[static_cast<std::size_t>(at)].glideY = 0.0f;
    auto moved = [&](std::optional<std::size_t>& index) {
        if (index) index = *index < to.size() ? to[*index] : std::nullopt;
    };
    if (focus_) {
        std::optional<std::size_t> f = focus_->element;
        moved(f);
        if (f) focus_->element = *f;
        else focus_.reset();
    }
    moved(dragging_);
    moved(held_);
    moved(pressing_);
    moved(playing_);
    moved(scrollDrag_);
    if (tapping_) {
        std::optional<std::size_t> t = tapping_->spot.element;
        moved(t);
        if (t) tapping_->spot.element = *t;
        else tapping_.reset();
    }
    std::vector<bool> drawnNow(plan.size(), false);
    for (std::size_t k = 0; k < to.size(); ++k)
        if (to[k] && k < drawn_.size()) drawnNow[*to[k]] = drawn_[k];
    drawn_ = std::move(drawnNow);
    targets_.clear();
    own_ = std::move(next);
    def_ = &own_;
    state_ = std::move(states);
    keyOf_.clear();
    sourceOf_.clear();
    firstOf_.assign(parts.size(), SIZE_MAX);
    for (std::size_t k = 0; k < plan.size(); ++k) {
        keyOf_.push_back(plan[k].key);
        sourceOf_.push_back(plan[k].source);
        if (plan[k].source != SIZE_MAX && firstOf_[plan[k].source] == SIZE_MAX) firstOf_[plan[k].source] = k;
    }
    listContext_ = std::move(contexts);
}

namespace {

// A list of what the game lists by name (list=gallery, endings, songs, achievements, tips): its kind, and its items' word.
std::optional<std::pair<RecordKind, std::string_view>> recordList(std::string_view list) {
    static constexpr std::array<std::tuple<std::string_view, RecordKind, std::string_view>, 5> kRecordLists{{
        {"gallery", RecordKind::Gallery, "image"}, {"endings", RecordKind::Ending, "ending"}, {"songs", RecordKind::Song, "song"},
        {"achievements", RecordKind::Achievement, "achievement"}, {"tips", RecordKind::Tip, "tip"}}};
    for (const auto& [name, kind, word] : kRecordLists)
        if (name == list) return std::pair{kind, word};
    return std::nullopt;
}

} // namespace

std::vector<ScreenView::ListItem> ScreenView::listItems(const Element& list, std::string& context) {
    std::vector<ListItem> out;
    Game& game = engine_.game();
    const PhoneData& phone = game.phone();
    const std::string name = list.option("list");
    // Someone, as `word` has them: their name's first letter, colour (a character's name colour), the character
    // and their avatar= (else their face) image, with `expression`.
    auto someone = [&](Variables& v, std::string_view word, std::string_view who, std::string_view character, std::string_view expression, SDL_Color colour) {
        const CharacterDef* def = !character.empty() ? engine_.script().character(character) : characterNamed(who);
        std::string avatar;
        if (def && !def->avatarOf({}).empty()) {
            avatar = def->avatarOf(expression);
            if (!expression.empty() && !engine_.assets().hasImage(avatar)) avatar = def->avatarOf({});
        }
        v.insert_or_assign(std::format("{}.character", word), def ? def->key : std::string{});
        v.insert_or_assign(std::format("{}.avatar", word), std::move(avatar));
        v.insert_or_assign(std::format("{}.initial", word), initialOf(who));
        v.insert_or_assign(std::format("{}.colour", word), hexOf(def && character.empty() ? def->nameColour : colour));
    };
    // The chat a list of messages (and its options) is of: the one open, else the newest.
    auto open = [&] {
        if (!engine_.openChat().empty()) return engine_.openChat();
        const auto chats = game.chats();
        return chats.empty() ? std::string{} : chats.front().name;
    };
    // The options of a chat= or call= scene's choice waiting on the phone (choose picks one).
    // An option of a choice: its words (their look kept: a part showing {option.text}), whether it's locked (needs
    // something) and what it needs, whether it can be picked, and whether it was picked before; `kind` and `number`
    // say which it is, for choose.
    auto option = [&](std::string key, const ChoiceItem& a, bool on, std::string_view kind, int number) {
        RichText words = rich(a.text);
        out.push_back({std::move(key),
                       {{"option.text", words.text}, {"option.locked", flag(a.locked)}, {"option.note", a.note}, {"option.on", flag(on)}, {"option.picked", flag(a.seen)},
                        {"option.kind", std::string(kind)}, {"option.number", number}},
                       {{"option.text", std::move(words.spans)}}});
    };
    auto phoneOptions = [&](std::vector<ChoiceItem> items) {
        for (ChoiceItem& a : items) option(std::format("phone choice:{}", a.option), a, !a.locked && truthy(game.variable("phone_signal")), "phone choice", a.option);
    };
    const bool phoneScreen = std::ranges::any_of(source_->elements, [](const Element& e) { return e.kind == ElementKind::Container && !e.option("app").empty(); });
    if (name == "apps") {
        for (const Element& app : source_->elements) {
            const std::string id = app.option("app");
            if (app.kind != ElementKind::Container || id.empty() || id == "home" || id == "call" || id == "lock" || app.option("title").empty() || !engine_.appOn(app)) continue;
            const std::string title = rich(app.option("title")).text;
            out.push_back({id, {{"app.name", id}, {"app.title", app.option("title")}, {"app.initial", initialOf(title)}, {"app.icon", app.option("icon")},
                                {"app.count", newOn(app)}, {"app.open", flag(engine_.phoneApp(*source_) == id)}}});
        }
    } else if (name == "chats") {
        const auto& messages = game.messages();
        for (const Game::Chat& c : game.chats()) {
            const MessageItem* last = c.last ? &messages[*c.last] : nullptr;
            std::string newest;
            if (c.typing) newest = typingDots(now(), still());
            else if (last && !last->deleted && isMessage(last->kind)) newest = last->text;
            Variables v{{"chat.name", c.name}, {"chat.text", std::move(newest)}, {"chat.time", last ? last->time : std::string{}},
                        {"chat.unread", c.unread}, {"chat.typing", flag(c.typing)}, {"chat.open", flag(c.name == engine_.openChat())}};
            someone(v, "chat", c.name, c.character, {}, c.colour);
            out.push_back({c.name, std::move(v)});
        }
    } else if (name == "messages") {
        using Kind = MessageItem::Kind;
        const std::string with = open();
        context = with;
        const auto& messages = game.messages();
        std::size_t last = SIZE_MAX, lastOwn = SIZE_MAX;
        for (std::size_t i = 0; i < messages.size(); ++i)
            if (messages[i].with == with && isMessage(messages[i].kind) && messages[i].typing <= 0.0f) {
                last = i;
                if (messages[i].from.empty()) lastOwn = i;
            }
        std::string day;
        bool dots = false; // (the ones on their way show as one typing: the next)
        for (std::size_t i = 0; i < messages.size(); ++i) {
            const MessageItem& m = messages[i];
            if (m.with != with) continue;
            const bool typing = m.typing > 0.0f || m.kind == Kind::Typing, own = m.from.empty() && m.kind != Kind::End;
            if (own && typing) continue; // (the owner's own, on its way: there in a moment)
            if (typing && std::exchange(dots, true)) continue;
            std::string newDay;
            if (!typing && !m.date.empty() && m.date != day) newDay = day = m.date;
            const bool shown = m.kind == Kind::Image || m.kind == Kind::Sticker;
            Variables v{{"message.number", static_cast<int>(i)}, {"message.from", m.from}, {"message.mine", flag(own)},
                        {"message.text", typing ? typingDots(now(), still()) : m.deleted ? std::string{} : m.text}, {"message.typing", flag(typing)},
                        {"message.image", shown && !m.deleted && !typing ? m.file : std::string{}}, {"message.sticker", flag(m.kind == Kind::Sticker && !m.deleted && !typing)},
                        {"message.sound", m.kind == Kind::Voice && !m.deleted && !typing ? m.file : std::string{}}, {"message.time", typing ? std::string{} : m.time},
                        {"message.day", newDay}, {"message.edited", flag(m.edited && !m.deleted && !typing)}, {"message.deleted", flag(m.deleted && !typing)},
                        {"message.seen", flag(i == lastOwn && i == last && m.seen)}, {"message.reaction", typing ? std::string{} : m.reaction},
                        {"message.ended", flag(m.kind == Kind::End)}};
            someone(v, "message", m.from, m.character, m.expression, m.colour);
            out.push_back({std::format("{}{}", m.number > 0 ? std::to_string(m.number) : "i" + std::to_string(i), typing ? ":typing" : ""), std::move(v)});
        }
    } else if (name == "options" && phoneScreen) { // a choice on the call, in the open chat (a chat= scene's), or a chat scene's
        if (const auto& call = game.call(); call && call->talking) { // (a choice of their call scene's)
            context = "call:" + call->who;
            phoneOptions(game.phoneChoices(call->who, true));
        } else {
            const std::string with = open();
            context = with;
            const bool coming = std::ranges::any_of(game.messages(), [&](const MessageItem& m) { return m.with == with && m.typing > 0.0f; });
            if (!with.empty() && game.chatScene().empty() && !game.call() && !coming) { // (a choice of their chat scene's, once its messages have come)
                phoneOptions(game.phoneChoices(with, false));
            } else if (!with.empty() && game.chatScene() == with && game.choiceOnPhone()) {
                const auto& items = game.choices();
                for (std::size_t k = 0; k < items.size(); ++k) option(std::format("choice:{}", k), items[k], !items[k].locked, "choice", static_cast<int>(k));
            }
        }
    } else if (name == "calls") {
        for (std::size_t k = phone.calls.size(); k-- > 0;) { // (the newest first)
            const CallItem& c = phone.calls[k];
            Variables v{{"call.who", c.who}, {"call.missed", flag(c.missed && !c.outgoing)}, {"call.busy", flag(c.busy)}, {"call.outgoing", flag(c.outgoing && !c.busy)},
                        {"call.incoming", flag(!c.outgoing && !c.missed && !c.busy)}, {"call.time", c.time},
                        {"call.length", c.missed || c.busy ? std::string{} : clockOf(static_cast<float>(c.seconds))}};
            someone(v, "call", c.who, {}, {}, c.colour);
            out.push_back({std::to_string(k), std::move(v)});
        }
    } else if (name == "people") { // (the characters whose contact= is true now, by name)
        std::vector<const CharacterDef*> people;
        for (const auto& [key, def] : engine_.script().characters)
            if (def.contact && def.contact->test(lookup_)) people.push_back(&def);
        std::ranges::sort(people, {}, &CharacterDef::name);
        for (const CharacterDef* def : people) {
            Variables v{{"person.name", def->name}, {"person.about", def->about}};
            someone(v, "person", def->name, def->key, {}, def->nameColour);
            out.push_back({def->key, std::move(v)});
        }
    } else if (name == "loves") { // the love interests (love=), in their order: their love now, and as hearts (a heart a point, five at most)
        const std::string most = toString(lookup_("most_loved"));
        for (const std::string& key : engine_.script().loves) {
            const CharacterDef* def = engine_.script().character(key);
            if (!def) continue;
            const Value love = game.variable(def->love);
            const int full = std::clamp(static_cast<int>(std::lround(toNumber(love))), 0, 5);
            std::string hearts;
            for (int i = 0; i < full; ++i) hearts += "\u2665"; // (one sign, in every font: five dim ones under them show the rest)
            Variables v{{"love.name", def->name}, {"love.value", love}, {"love.hearts", hearts}, {"love.most", def->name == most ? 1 : 0}};
            someone(v, "love", def->name, def->key, {}, def->nameColour);
            out.push_back({"love:" + def->key, std::move(v)});
        }
    } else if (name == "notices") { // what's new: chats with messages not seen yet (by their newest), and missed calls
        const auto& messages = game.messages();
        for (const Game::Chat& c : game.chats())
            if (c.unread > 0 && c.last) {
                const MessageItem& m = messages[*c.last];
                out.push_back({"chat:" + c.name, {{"notice.who", c.name}, {"notice.chat", c.name},
                                                  {"notice.text", m.deleted ? std::string{} : m.text}, {"notice.call", 0}, {"notice.time", m.time}}});
            }
        for (std::size_t k = 0; k < phone.calls.size(); ++k)
            if (phone.calls[k].missed && !phone.calls[k].outgoing && !phone.calls[k].seen)
                out.push_back({std::format("call:{}", k), {{"notice.who", phone.calls[k].who}, {"notice.chat", std::string{}},
                                                           {"notice.text", std::string{}}, {"notice.call", 1}, {"notice.time", phone.calls[k].time}}});
    } else if (name == "captions") { // (the player's Captions: what the sounds and songs that just began are; the editor's preview: one)
        for (const Game::Caption& c : game.captions()) // (their words' look kept: a part showing {caption.text})
            out.push_back({"caption:" + c.text, {{"caption.text", c.text}, {"caption.music", flag(c.music)}}, {{"caption.text", c.spans}}});
        if (tools_ && out.empty()) out.push_back({"caption", {{"caption.text", std::string("A sound's caption"_tr)}, {"caption.music", 0}}});
    } else if (name == "subtitles") { // (...and the movie's line now)
        if (std::string line = game.subtitle(); !line.empty()) out.push_back({"subtitle", {{"subtitle.text", std::move(line)}}});
        else if (tools_) out.push_back({"subtitle", {{"subtitle.text", std::string("A visual's subtitle"_tr)}}});
    } else if (name == "options") { // the story's choice (the phone's own are in the phone's lists, above)
        if (game.showingChoices() && !game.choiceOnPhone()) {
            const auto& items = game.choices();
            for (std::size_t k = 0; k < items.size(); ++k) option(std::format("choice:{}", k), items[k], !items[k].locked, "choice", static_cast<int>(k));
        } else if (tools_) { // (the developer tools' look at it: two to pick from)
            option("choice:0", ChoiceItem{.text = "An option"_tr()}, true, "choice", 0);
            option("choice:1", ChoiceItem{.text = "Another option"_tr()}, true, "choice", 1);
        }
    } else if (name == "history" || name == "lines") { // the lines read, the oldest first; or the lines on the page now, the newest the one typing
        struct Said {
            std::string name;
            SDL_Color colour;
            std::string text;
            std::vector<TextSpan> spans;
            bool choice = false, voiced = false, back = false;
        };
        std::vector<Said> lines;
        if (name == "history")
            for (std::size_t k = 0; k < game.history().size(); ++k) {
                const HistoryEntry& h = game.history()[k];
                lines.push_back({h.speaker, h.colour, h.text, h.spans, h.choice, h.voiced(), game.canRollbackTo(k)});
            }
        else
            for (const PageLine& l : game.page()) {
                std::vector<TextSpan> spans = l.spans;
                if (l.textColour) spans.insert(spans.begin(), TextSpan{.begin = 0, .end = utf8::length(l.text), .colour = l.textColour}); // (its character's text_colour=)
                lines.push_back({l.speaker, l.colour, l.text, std::move(spans)});
            }
        if (tools_ && lines.empty()) // (the developer tools' look at it: a line said, and one narrated)
            lines = {{"A character"_tr(), SDL_Color{173, 162, 255, 255}, "Words someone said."_tr(), {}}, {"", SDL_Color{240, 240, 245, 255}, "A line of narration."_tr(), {}}};
        for (std::size_t k = 0; k < lines.size(); ++k) {
            Said& l = lines[k];
            out.push_back({std::format("line:{}", k),
                           {{"line.number", static_cast<int>(k + 1)}, {"line.name", l.name}, {"line.colour", hexOf(l.colour)}, {"line.text", l.text}, {"line.choice", flag(l.choice)},
                            {"line.voiced", flag(l.voiced)}, {"line.back", flag(l.back)}, {"line.typing", flag(name == "lines" && k + 1 == lines.size())}},
                           {{"line.text", std::move(l.spans)}}});
        }
    } else if (name == "pages") { // the numbered pages of saves, the one open open (page auto and page quick go to those)
        SlotBrowser& slots = engine_.slots();
        const auto& pages = tools_ && slots.pages().empty() ? samplePages() : slots.pages(); // (the developer tools' look at them)
        for (std::size_t k = 0; k < pages.size(); ++k)
            if (pages[k] != "auto" && pages[k] != "quick")
                out.push_back({"page:" + pages[k], {{"page.name", pages[k]}, {"page.number", static_cast<int>(out.size() + 1)}, {"page.open", flag(k == slots.page())}}});
    } else if (name == "saves") { // the saves on the page open: the time each was made, its words, its picture ({save.image})
        SlotBrowser& slots = engine_.slots();
        const auto& cards = tools_ && slots.cards().empty() ? sampleCards() : slots.cards(); // (the developer tools' look at them: empty)
        for (std::size_t k = 0; k < cards.size(); ++k) {
            const SlotBrowser::Card& card = cards[k];
            out.push_back({"save:" + card.slot,
                           {{"save.number", static_cast<int>(k + 1)}, {"save.used", flag(card.info.exists)}, {"save.time", card.info.exists ? formatTime(card.info.time) : std::string{}},
                            {"save.text", card.info.summary}, {"save.image", card.thumbnail ? std::format("save:{}", k + 1) : std::string{}}}});
        }
    } else if (const auto record = recordList(name)) { // the gallery's images, the endings, songs, achievements or tips, as they're listed
        const auto& [kind, word] = *record;
        const auto& records = engine_.script().records(kind);
        const std::string playing = kind == RecordKind::Song ? engine_.sound().music() : std::string{};
        for (std::size_t k = 0; k < records.size(); ++k) {
            const Script::Record& rec = records[k];
            const bool have = engine_.has(kind, k);
            auto value = [&](std::string_view what) { return std::format("{}.{}", word, what); };
            Variables v{{value("number"), static_cast<int>(k + 1)}, {value("name"), rec.name}, {value("title"), rec.title}, {value("locked"), flag(!have)}};
            if (kind == RecordKind::Ending || kind == RecordKind::Achievement || kind == RecordKind::Tip) v.insert_or_assign(value("text"), rec.text); // (a locked ending's: its hint)
            if (kind == RecordKind::Achievement) v.insert_or_assign(value("secret"), flag(rec.secret));
            if (kind == RecordKind::Song) // (a named sound's: what it plays)
                v.insert_or_assign(value("playing"), flag(have && !playing.empty() && playing == engine_.assets().sound(engine_.script().soundOf(rec.name, "music").file)));
            if (kind == RecordKind::Tip) {
                v.insert_or_assign(value("open"), flag(have && engine_.tip() == k));
                v.insert_or_assign(value("new"), flag(engine_.newTip(k)));
            }
            out.push_back({std::format("{}:{}", word, rec.name), std::move(v)});
        }
    }
    return out;
}

int ScreenView::newOn(const Element& app) {
    const Game& game = engine_.game();
    const auto& parts = source_->elements;
    const auto at = static_cast<int>(&app - parts.data());
    int count = 0;
    bool messages = false;
    for (std::size_t j = static_cast<std::size_t>(at) + 1; j < parts.size(); ++j) {
        int p = parts[j].parent;
        while (p >= 0 && p != at) p = parts[static_cast<std::size_t>(p)].parent;
        if (p != at) continue;
        const std::string list = parts[j].option("list");
        if (list == "chats" || list == "messages") {
            if (!std::exchange(messages, true)) count += game.unseenMessages();
        } else if (list == "calls") {
            count += game.unseenCalls();
        }
    }
    if (const auto it = app.exprs.find("count"); it != app.exprs.end()) count += toInt(it->second.eval(lookup_));
    return count;
}

// What a list shows has been seen: a chat's messages read, the missed calls (the counts on their apps go).
void ScreenView::seen(const Element& list) {
    Game& game = engine_.game();
    const std::string name = list.option("list");
    if (name == "messages") {
        std::string with = engine_.openChat();
        if (with.empty())
            if (const auto chats = game.chats(); !chats.empty()) with = chats.front().name;
        if (!with.empty()) game.readMessages(with);
    } else if (name == "calls") {
        game.seeCalls();
    }
}

Variables ScreenView::itemOf(std::size_t index) const {
    return index < bound_.size() && bound_[index] ? *bound_[index] : Variables{};
}

} // namespace vn
