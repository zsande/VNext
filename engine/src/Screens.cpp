// A screen as it's open (ScreenView): its parts' state, whether each shows (when=), can be clicked or is chosen, the
// values in its texts, and its animations as it opens and closes.
#include "Engine.hpp"

#include <algorithm>
#include <cmath>

namespace vn {

// (the screens' files share these: Screens.hpp)
bool isInput(const SDL_Event& e) {
    if (e.type == gestureEventType()) return true;
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_TEXT_INPUT:
        return true;
    default:
        return false;
    }
}

std::string formatTime(SDL_Time time) {
    SDL_DateTime dt{};
    if (!SDL_TimeToDateTime(time, &dt, true)) return {};
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}", dt.year, dt.month, dt.day, dt.hour, dt.minute);
}

SDL_FPoint centre(const SDL_FRect& r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }

// A box that holds parts: a container.
bool isBox(ElementKind kind) { return kind == ElementKind::Container; }

std::string hexOf(SDL_Color c) { return std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b); } // (a colour as a value: a speaker's, a chat's)

ScreenView::ScreenView(const ScreenDef& def, Engine& engine, bool tools) : source_(&def), def_(&def), engine_(engine), tools_(tools), state_(def.elements.size()) {
    hasLists_ = std::ranges::any_of(def.elements, [](const Element& el) { return el.kind == ElementKind::Container && !el.option("list").empty(); });
    // A part in a list sees its item's values first ({chat.name}); in a textbox, the line's own names are about
    // the line it shows (several can show at once).
    lookup_ = [this](std::string_view name) -> Value {
        if (current_ < bound_.size() && bound_[current_])
            if (const auto it = bound_[current_]->find(name); it != bound_[current_]->end()) return it->second;
        if (name == "speaker" || name == "speaker_colour" || name == "has_face" || name == "face_side" || name == "text_complete" || name == "text_paused")
            if (const auto line = engine_.game().line(def_->name)) {
                if (name == "speaker") return std::string(line->speaker);
                if (name == "speaker_colour") return hexOf(line->speakerColour);
                if (name == "has_face") return line->faceBase.empty() ? 0 : 1;
                if (name == "face_side") return std::string(line->faceSide);
                if (name == "text_paused") return line->complete && line->atPause && engine_.game().textComplete() ? 1 : 0; // (at a {pause}: shown so far)
                return line->complete && engine_.game().textComplete() ? 1 : 0; // (all of it shown: a click goes on)
            }
        return engine_.lookup()(name);
    };
}

void ScreenView::reset() {
    for (auto& st : state_) {
        st.scroll = 0.0f;
        st.keptScroll = -1.0f; // (a list with position=bottom starts at its end again, and stays there as more comes)
        st.keptMost = 0.0f;
        st.glideX = st.glideY = 0.0f;
        st.since.reset(); // (animations start again)
        st.clicked.reset();
        st.pointedPlayed = 0.0f;
        st.pointedPlayedAt.reset();
        st.movie.reset(); // movies start again
        st.movieName.clear();
        st.scrollX = 0.0f;
        st.centred = false;
    }
    opening_.reset(); // (its own animations again, until the one opening it says otherwise)
    closing_.reset();
    chosen_.clear();
    focus_.reset();
    place_.reset();
    keyFocus_ = false;
    ringAt_ = Ring::None;
    pageTurns_ = 0.0f;
    typing_.reset();
    caretBox_.reset();
    keyboardShut_ = false;
    dragging_.reset();
    playing_.reset();
    held_.reset();
    pressing_.reset();
    scrollDrag_.reset();
    tapping_.reset();
    mouse_ = {-1.0f, -1.0f};
    laidOutOpen_ = false; // (its words from the last time it was open: not said until it's laid out again)
    opened();
}

float ScreenView::now() const {
    return tools_ ? engine_.toolsTime() : engine_.time();
}

ScreenView::Played ScreenView::animationIn(const Script* script, const std::optional<std::string>& name) {
    if (!name) return std::nullopt;
    if (!script) return nullptr;
    const auto it = script->animations.find(*name);
    return it != script->animations.end() ? std::make_shared<const Animation>(it->second) : nullptr;
}

bool ScreenView::still() const { return !tools_ && !engine_.settings().animations; }

float ScreenView::played(const Animation& a, float seconds) const {
    if (!still()) return seconds;
    return a.loops() ? 0.0f : a.total();
}

void ScreenView::opened() {
    shownSince_ = now();
    leftSince_.reset();
    for (ElementState& st : state_) st.chosen = false; // (shown again: what chosen buttons set holds again)
}

void ScreenView::leave() {
    leftSince_ = now();
    pressing_.reset();
}

bool ScreenView::leaving() const {
    return leftSince_ && hideAnimation() && !still() && now() - *leftSince_ < hideAnimation()->total();
}

void ScreenView::playWith(Played opening, Played closing) {
    opening_ = std::move(opening);
    closing_ = std::move(closing);
}

bool ScreenView::startsCovered() const {
    const Animation* a = showAnimation();
    return a && a->at(0.0f, {255, 255, 255, 255}, [](bool, float) { return 0.0f; }, {}).fade >= 0.999f;
}

bool ScreenView::endsCovered() const {
    const Animation* a = hideAnimation();
    return a && a->at(a->loops() ? a->length * 0.999f : a->total(), {255, 255, 255, 255}, [](bool, float) { return 0.0f; }, {}).fade >= 0.999f;
}

bool ScreenView::pointedAt(std::size_t index) const {
    if (leftSince_) return false;
    if (focus_ && focus_->element == index) return true; // (a part that takes clicks: pointed at, with the mouse or a control's move actions)
    return seenMotion_ == engine_.pointerMoves() && SDL_PointInRectFloat(&mouse_, &rectOf(index));
}

void ScreenView::pointedOnward(std::size_t index) {
    const Animation* a = def_->elements[index].pointedAnimation.get();
    ElementState& st = state_[index];
    if (!a) return;
    const float t = now(), dt = st.pointedPlayedAt ? std::clamp(t - *st.pointedPlayedAt, 0.0f, 0.25f) : 0.0f;
    st.pointedPlayedAt = t;
    if (still()) { // (as it ends while it's pointed at; else as it starts)
        st.pointedPlayed = pointedAt(index) ? std::max(played(*a, 0.0f), 0.0001f) : 0.0f;
        return;
    }
    if (pointedAt(index)) {
        st.pointedPlayed = std::max(st.pointedPlayed + dt, 0.0001f); // (it starts at once)
        if (!a->loops()) st.pointedPlayed = std::min(st.pointedPlayed, a->total());
        else if (a->length > 0.0f && st.pointedPlayed >= 2.0f * a->length) st.pointedPlayed = a->length + std::fmod(st.pointedPlayed - a->length, a->length);
        return;
    }
    // Back the way it came, from where it is in the time through it's in (one played to its end: from its end).
    if (a->length > 0.0f && st.pointedPlayed > a->length) st.pointedPlayed = !a->loops() && st.pointedPlayed >= a->total() ? a->length : std::fmod(st.pointedPlayed, a->length);
    st.pointedPlayed = std::max(0.0f, st.pointedPlayed - dt);
}

bool ScreenView::visible(const Element& el) {
    const Bound bound(*this, indexOf(el));
    return !el.visible || el.visible->test(lookup_);
}

bool ScreenView::live(std::size_t index) {
    const Element& el = def_->elements[index];
    return shows(index) && (el.parent < 0 || drawn(static_cast<std::size_t>(el.parent)));
}

bool ScreenView::enabled(const Element& el) {
    const Bound bound(*this, indexOf(el));
    return !el.clickable || el.clickable->test(lookup_);
}

bool ScreenView::selected(const Element& el) {
    const Bound bound(*this, indexOf(el));
    if (el.chosen) return el.chosen->test(lookup_);
    // A button that sets a variable or a player setting to a value is chosen while it has it.
    if (const auto* set = el.actions.size() == 1 ? std::get_if<cmd::Set>(&el.actions.front()) : nullptr; set && set->value)
        if (const auto value = set->value->constant()) {
            const Value now = lookup_(set->variable);
            const bool text = std::holds_alternative<std::string>(now) || std::holds_alternative<std::string>(*value);
            return text ? toString(now) == toString(*value) : toNumber(now) == toNumber(*value);
        }
    const auto* u = el.actions.size() == 1 ? std::get_if<cmd::Ui>(&el.actions.front()) : nullptr;
    if (!u || u->verb != "setting") return false;
    const auto space = u->arg.find(' ');
    if (space == std::string::npos) return false;
    const std::string_view name = std::string_view(u->arg).substr(0, space), value = std::string_view(u->arg).substr(space + 1);
    if (name == "language") { // (one by name: while it's the language on)
        const auto id = engine_.languageIdNamed(value);
        return id && *id == engine_.languageId(engine_.languageIndex());
    }
    const auto now = engine_.settings().get(name);
    return now && std::to_string(*now) == value;
}

float ScreenView::number(const Element& el, std::string_view key, float fallback) {
    const Bound bound(*this, indexOf(el));
    if (const auto it = el.exprs.find(key); it != el.exprs.end())
        return static_cast<float>(toNumber(it->second.eval(lookup_)));
    return fallback;
}

std::string ScreenView::interpolate(std::string_view text) {
    return vn::expand(text, lookup_);
}

RichText ScreenView::rich(std::string_view text) {
    if (text.find_first_of("{}") == std::string_view::npos) return {.text = std::string(text), .spans = {}}; // (nothing to fill in or style: as it is)
    if (const auto read = richRead_.find(text); read != richRead_.end()) return read->second;
    // (one that fills in a value is read every time: the value may have changed; one that only styles
    // its words, once)
    bool fills = false;
    RichText out = richText(text, [&](std::string_view name) {
        fills = true;
        return lookup_(name);
    }, &engine_.palette());
    if (!fills && richRead_.size() < 512) richRead_.emplace(std::string(text), out);
    return out;
}

RichText ScreenView::wordsOf(std::size_t index, std::string_view text) {
    RichText out = rich(text);
    if (text.size() > 2 && text.front() == '{' && text.back() == '}' && index < boundLooks_.size() && boundLooks_[index])
        if (const auto it = boundLooks_[index]->find(text.substr(1, text.size() - 2)); it != boundLooks_[index]->end() && !it->second.empty()) out.spans = it->second;
    return out;
}

// Whether part `index` shows now: its when= lets it and, on the phone, an app (a container with app=) while it's the
// one open.
bool ScreenView::shows(std::size_t index) {
    const Bound bound(*this, index);
    const Element& el = def_->elements[index];
    if (!visible(el)) return false;
    if (el.kind == ElementKind::Container && !el.option("app").empty()) return engine_.phoneApp(*source_) == el.option("app");
    return true;
}

void ScreenView::endFrame(std::uint64_t frame) {
    for (auto& st : state_)
        if (st.movie && st.movieFrame != frame) {
            st.movie.reset();
            st.movieName.clear();
        }
}

} // namespace vn
