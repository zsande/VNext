// What the mouse, the keys and a controller do on a screen: pointing, pressing, dragging, scrolling and moving
// between parts (the same rule as in the developer tools), and dragging a slider.
#include "Engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ranges>

namespace vn {

const ScreenView::Target* ScreenView::targetAt(float x, float y) const {
    for (const auto& t : targets_ | std::views::reverse)
        if (!t.hidden && t.presses && pointInRect(x, y, t.rect)) {
            const auto cut = t.spot.element < def_->elements.size() ? cutBy(t.spot.element) : std::nullopt; // (cut off past a container's edges: not there)
            if (!cut || pointInRect(x, y, *cut)) return &t;
        }
    return nullptr;
}

const ScreenView::Target* ScreenView::focusedTarget() const {
    if (!focus_) return nullptr;
    const auto it = std::ranges::find(targets_, *focus_, &Target::spot);
    return it != targets_.end() ? &*it : nullptr;
}

void ScreenView::clicked(const Element& el) {
    if (!el.style.clickSound.empty()) engine_.playSound(el.style.clickSound);
}

void ScreenView::run(const Element& el) {
    clicked(el);
    if (source_->name != "keyboard") engine_.closeKeyboard(); // (the player's on to something else: the on-screen keyboard goes)
    engine_.run(el.actions, el.confirm, itemOf(indexOf(el)));
}

std::optional<ScreenView::Pressed> ScreenView::takePressed() {
    if (!playing_ || *playing_ >= def_->elements.size()) return std::nullopt;
    if (engine_.frameNumber() > drawnFrame_ + 1 || !drawn(*playing_)) { // (its screen closed, or it was hidden: the click goes with it)
        playing_.reset();
        return std::nullopt;
    }
    const Element& el = def_->elements[*playing_];
    const auto& since = state_[*playing_].clicked;
    if (since && el.clickAnimation && now() - *since < el.clickAnimation->total()) return std::nullopt;
    const std::size_t index = *playing_;
    playing_.reset();
    return Pressed{el.actions, el.confirm, itemOf(index)};
}

void ScreenView::setFocus(const Target& t) {
    place_ = t.spot;
    if (focus_ == t.spot) return;
    focus_ = t.spot;
    if (!tools_) engine_.pointedIn(*this); // (what another screen had pointed at goes)
    const Element& el = def_->elements[t.spot.element];
    if (!el.style.pointedSound.empty()) engine_.playSound(el.style.pointedSound);
}

void ScreenView::hold(std::size_t element) {
    const Element& el = def_->elements[element];
    held_ = element;
    heldMoved_ = false;
    pressed_ = mouse_;
    grab_ = {mouse_.x - rectOf(element).x, mouse_.y - rectOf(element).y};
    clicked(el);
}

void ScreenView::drop(const Element& target) {
    const std::string what = held();
    held_.reset();
    clicked(target);
    engine_.drop(what, target.drop);
}

void ScreenView::activate(const Target& t, std::optional<float> mouseX) {
    const Element& el = def_->elements[t.spot.element];
    const auto item = static_cast<std::size_t>(t.spot.item);
    if (el.kind == ElementKind::Button) { // dragging: take one (or put it back), drop it on a part that takes it
        if (!el.option("drag").empty()) return held_ == t.spot.element ? held_.reset() : hold(t.spot.element);
        if (held_ && !el.drop.empty()) return drop(el);
        held_.reset();
        if (el.clickAnimation && el.clickAnimation->total() > 0.0f && !still()) { // (it plays, then it does what it does)
            if (playing_) return;
            clicked(el);
            state_[t.spot.element].clicked = now();
            playing_ = t.spot.element;
            return;
        }
        return run(el);
    }
    clicked(el);
    switch (el.kind) {
    case ElementKind::Control:
        engine_.startRebind(el.text, Engine::slotOf(el));
        break;
    case ElementKind::Flowchart: // a point (or an option not tried yet: its choice)
        if (const auto& ids = state_[t.spot.element].ids; item < ids.size() && !ids[item].empty())
            engine_.goToPlace(ids[item], def_->elements[t.spot.element].confirm);
        break;
    case ElementKind::Slider:
        if (mouseX) {
            dragging_ = t.spot.element;
            slide(t.spot.element, *mouseX);
        }
        break;
    case ElementKind::Input: // the box pressed: typed into, and the on-screen keyboard comes up over it when a controller pressed it,
                             // or a click did while no real keyboard's been used (a real one's key puts it away)
        typing_ = t.spot.element;
        keyboardShut_ = false;
        if (engine_.padInUse() || (mouseX && !engine_.keyboardUsed())) engine_.wantKeyboard(*this, t.spot.element, !mouseX);
        break;
    default:
        break;
    }
}

void ScreenView::slide(std::size_t element, float x) {
    const Element& el = def_->elements[element];
    const SDL_FRect at = sliderBox(element);
    const float t = std::clamp((x - at.x) / std::max(1.0f, at.w), 0.0f, 1.0f);
    if (const SettingControl* c = findSettingControl(el.option("setting"))) { // a player setting
        c->setValue(engine_.settings(), t);
        engine_.settingsChanged(false);
        return;
    }
    const float lo = number(el, "minimum", 0.0f), hi = number(el, "maximum", 100.0f), step = std::max(1.0f, number(el, "step", 1.0f));
    const int value = static_cast<int>(lo + std::round((hi - lo) * t / step) * step);
    if (toInt(engine_.game().variable(el.option("variable"))) != value) engine_.game().setVariable(el.option("variable"), value);
}

// What the move actions can point at: what takes clicks there (and the items of a list that scrolls, to scroll it).
bool ScreenView::keyReachable(const Target& t) const {
    return t.reachable;
}

const ScreenView::Target* ScreenView::nearest(const SDL_FRect& from, int dx, int dy, float* score) const {
    const Target* current = placeTarget();
    std::vector<const Target*> those;
    std::vector<SDL_FRect> boxes;
    for (const auto& t : targets_)
        if (keyReachable(t) && &t != current) {
            those.push_back(&t);
            boxes.push_back(t.rect);
        }
    const auto best = nearestThatWay(from, boxes, dx, dy, score);
    return best ? those[*best] : nullptr;
}

std::optional<std::size_t> nearestThatWay(const SDL_FRect& from, std::span<const SDL_FRect> boxes, int dx, int dy, float* score) {
    std::optional<std::size_t> best;
    float bestScore = 0.0f;
    const SDL_FPoint a = centre(from);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const SDL_FRect& r = boxes[i];
        const SDL_FPoint b = centre(r);
        const float along = dx != 0 ? (b.x - a.x) * static_cast<float>(dx) : (b.y - a.y) * static_cast<float>(dy);
        const float across = dx != 0 ? std::abs(b.y - a.y) : std::abs(b.x - a.x);
        if (along <= 1.0f) continue;
        // (in line, the next one that way, little as their middles line up: a slider's middle is far from a "<" button's over it)
        const bool inLine = dx != 0 ? r.y < from.y + from.h && r.y + r.h > from.y : r.x < from.x + from.w && r.x + r.w > from.x;
        const bool past = dx > 0 ? r.x >= from.x + from.w - 1.0f : dx < 0 ? r.x + r.w <= from.x + 1.0f
                        : dy > 0 ? r.y >= from.y + from.h - 1.0f : r.y + r.h <= from.y + 1.0f;
        if (!inLine && !past) continue;
        const float s = inLine ? along + across * 0.5f : along + across * 2.0f + 1e6f;
        if (!best || s < bestScore) {
            best = i;
            bestScore = s;
        }
    }
    if (score) *score = bestScore;
    return best;
}

void ScreenView::pointByKeys(const Target& t) {
    const Target hit = t; // (a copy: it's one of the targets)
    setFocus(hit);
    keyFocus_ = true;
    reveal(hit);
}

bool ScreenView::takesKeys() {
    return std::ranges::any_of(targets_, [&](const Target& t) { return keyReachable(t); }) || scroller().has_value();
}

std::optional<SDL_FRect> ScreenView::pointedRect() const {
    if (const Target* t = placeTarget()) return t->rect;
    return std::nullopt;
}

const ScreenView::Target* ScreenView::placeTarget() const {
    if (const Target* t = focusedTarget()) return t;
    if (!place_) return nullptr;
    const auto it = std::ranges::find(targets_, *place_, &Target::spot);
    return it != targets_.end() ? &*it : nullptr;
}

bool ScreenView::keysPoint() const { return keyFocus_ && focusedTarget(); }

std::optional<float> ScreenView::toward(const SDL_FRect& from, int dx, int dy) const {
    float score = 0.0f;
    if (!nearest(from, dx, dy, &score)) return std::nullopt;
    return score;
}

void ScreenView::pointToward(const SDL_FRect& from, int dx, int dy) {
    if (const Target* t = nearest(from, dx, dy)) pointByKeys(*t);
}

bool ScreenView::pointFirst() {
    const Target* first = nullptr; // (the first one in sight: one scrolled away only when there's none)
    for (const auto& t : targets_)
        if (keyReachable(t) && (!first || (first->hidden && !t.hidden))) {
            first = &t;
            if (!t.hidden) break;
        }
    if (first) pointByKeys(*first);
    return first != nullptr;
}

bool ScreenView::adjustPointed(int dx) {
    const Target* t = placeTarget();
    if (!t || t->spot.element >= def_->elements.size() || def_->elements[t->spot.element].kind != ElementKind::Slider) return false;
    const Target at = *t; // (a copy: it's one of the targets)
    adjustSlider(at.spot.element, dx);
    pointByKeys(at); // (the keys are on it: its ring)
    return true;
}

void ScreenView::letGo() {
    focus_.reset();
    place_.reset();
    keyFocus_ = false;
}

void ScreenView::adjustSlider(std::size_t element, int delta) {
    const Element& el = def_->elements[element];
    if (!enabled(el)) return;
    if (const SettingControl* c = findSettingControl(el.option("setting"))) { // a player setting: a twentieth at a time
        c->setValue(engine_.settings(), std::clamp(c->value(engine_.settings()) + static_cast<float>(delta) * 0.05f, 0.0f, 1.0f));
        engine_.settingsChanged(c->testSound);
        return;
    }
    const float lo = number(el, "minimum", 0.0f), hi = number(el, "maximum", 100.0f), step = std::max(1.0f, number(el, "step", 1.0f));
    const float value = static_cast<float>(toNumber(engine_.game().variable(el.option("variable"))));
    const int next = static_cast<int>(std::clamp(value + step * static_cast<float>(delta), std::min(lo, hi), std::max(lo, hi)));
    engine_.game().setVariable(el.option("variable"), next);
    if (!el.actions.empty()) run(el);
}

void ScreenView::pressPointed() {
    if (!keyFocus_) return; // (what the pointer is on, a click presses)
    if (const Target* t = focusedTarget(); t && t->presses) {
        const Target hit = *t; // (activate() may change the targets)
        activate(hit, std::nullopt);
    }
}

std::optional<std::size_t> ScreenView::scroller() {
    if (const Target* t = placeTarget(); t && t->spot.element < def_->elements.size())
        for (int p = def_->elements[t->spot.element].parent; p >= 0; p = def_->elements[static_cast<std::size_t>(p)].parent)
            if (isBox(def_->elements[static_cast<std::size_t>(p)].kind) && def_->elements[static_cast<std::size_t>(p)].scrolls()) return static_cast<std::size_t>(p);
    for (std::size_t i = 0; i < def_->elements.size(); ++i)
        if (isBox(def_->elements[i].kind) && def_->elements[i].scrolls() && live(i)) return i;
    return std::nullopt;
}

bool ScreenView::scrollBy(std::string_view how) {
    const auto box = scroller();
    if (!box) return false;
    const bool up = how == "up", down = how == "down", top = how == "to the top";
    ElementState& st = state_[*box];
    st.glideX = st.glideY = 0.0f;
    st.scroll = up ? st.scroll - 60.0f : down ? st.scroll + 60.0f : top ? 0.0f : 1e9f; // (kept within it as it's laid out)
    return true;
}

void ScreenView::putBack() { held_.reset(); }

bool ScreenView::wheelAt(SDL_FPoint at, float up, float across) {
    if (up == 0.0f && across == 0.0f) return false;
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0; // (Shift and the wheel: sideways)
    const float sideways = across != 0.0f ? across : shift ? -up : 0.0f, down = shift ? 0.0f : up;
    for (std::size_t i = 0; i < def_->elements.size(); ++i) {
        const Element& el = def_->elements[i];
        if (!live(i)) continue;
        switch (el.kind) {
        case ElementKind::Flowchart: // up and down; sideways
            state_[i].scrollX += sideways * 60.0f;
            state_[i].scroll -= down * 60.0f;
            return true;
        case ElementKind::Container: // (the saves or their pages: the wheel turns the page, a whole turn a page)
            if (down == 0.0f || (el.option("list") != "saves" && el.option("list") != "pages")) break;
            for (pageTurns_ += down; std::abs(pageTurns_) >= 1.0f; pageTurns_ -= pageTurns_ > 0.0f ? 1.0f : -1.0f) engine_.slots().turn(pageTurns_ < 0.0f ? +1 : -1);
            return true;
        default:
            break;
        }
    }
    // A container with scroll=yes there (the innermost): what's in it scrolls (sideways: a trackpad, Shift, the stick).
    if (const auto found = scrollableAt(at.x, at.y)) {
        ElementState& st = state_[*found];
        st.glideX = st.glideY = 0.0f;
        // (close_at_end=yes: turned on down past its end, the screen closes: the history, opened by the wheel, goes the same way)
        if (down < 0.0f && sideways == 0.0f && st.scroll >= std::max(0.0f, st.reach - rectOf(*found).h) - 0.5f && def_->elements[*found].option("close_at_end", "no") == "yes") {
            engine_.run({cmd::Ui{.verb = "close"}});
            return true;
        }
        st.scrollX += sideways * 60.0f;
        st.scroll -= down * 60.0f;
        return true;
    }
    return false;
}

bool ScreenView::hasKey(const SDL_Event& e) {
    for (std::size_t i = 0; i < def_->elements.size(); ++i)
        if (const Element& el = def_->elements[i]; live(i) && enabled(el) && std::ranges::any_of(el.keys, [&](const KeyChord& k) { return k.matches(e); }))
            return true;
    return false;
}

bool ScreenView::catches(const SDL_Event& e) {
    SDL_FPoint at{};
    if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) && e.button.button == SDL_BUTTON_LEFT) at = {e.button.x, e.button.y};
    else if (e.type == SDL_EVENT_MOUSE_WHEEL) at = mouse_; // (where the pointer is, as the wheel's scrolling has it)
    else return false;
    for (std::size_t i = 0; i < def_->elements.size(); ++i) {
        const Element& el = def_->elements[i];
        if (!drawn(i) || el.kind == ElementKind::Control) continue;
        if (el.kind == ElementKind::Container && el.style.background.a == 0 && el.style.border.a == 0) continue; // (a box with nothing of its own to show)
        if (pointInRect(at.x, at.y, bounds(i))) return true;
    }
    return false;
}

bool ScreenView::handleEvent(const SDL_Event& e) {
    tapped_ = false; // (what this event is: see takeTapped)
    if (e.type == SDL_EVENT_MOUSE_MOTION) { // (where the pointer is, as this screen saw it last)
        mouse_ = {e.motion.x, e.motion.y};
        seenMotion_ = engine_.pointerMoves();
    }
    if (leftSince_) return def_->storyWaits && isInput(e); // (closing: it only plays its animation_on_close=)
    // Buttons' keys= (as if clicked: a click animation plays first).
    for (std::size_t i = 0; i < def_->elements.size(); ++i) {
        const Element& el = def_->elements[i];
        if (std::ranges::none_of(el.keys, [&](const KeyChord& k) { return k.matches(e); }) || !live(i) || !enabled(el)) continue;
        activate({{i}, rectOf(i), false}, std::nullopt);
        return true;
    }

    if (dragging_) {
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            mouse_ = {e.motion.x, e.motion.y};
            slide(*dragging_, e.motion.x);
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            const Element& el = def_->elements[*dragging_];
            dragging_.reset();
            if (const SettingControl* c = findSettingControl(el.option("setting")); c && c->testSound) engine_.settingsChanged(true);
            if (!el.actions.empty()) run(el);
            return true;
        }
    }

    if (scrollDrag_) { // a container's parts dragged (or its scrollbar's thumb): it scrolls
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            mouse_ = {e.motion.x, e.motion.y};
            ElementState& st = state_[*scrollDrag_];
            const float dx = e.motion.x - scrollFrom_.x, dy = e.motion.y - scrollFrom_.y;
            if (scrollBarDrag_) {
                const SDL_FRect bar = barOf(*scrollDrag_, scrollBarAcross_), thumb = thumbOf(*scrollDrag_, scrollBarAcross_), box = rectOf(*scrollDrag_);
                if (scrollBarAcross_ && bar.w > thumb.w) st.scrollX = scrollWasX_ + dx * (st.reachX - box.w) / (bar.w - thumb.w);
                else if (!scrollBarAcross_ && bar.h > thumb.h)
                    st.scroll = scrollWas_ - def_->elements[*scrollDrag_].dragDirection() * dy * (st.reach - box.h) / (bar.h - thumb.h);
                return true;
            }
            if (!scrollDragMoved_ && std::hypot(dx, dy) > 6.0f) {
                if (held_) { // (a button's own dragging: that, not scrolling)
                    scrollDrag_.reset();
                    tapping_.reset();
                    return false;
                }
                scrollDragMoved_ = true;
                pressing_.reset(); // (not a click, nor a tap)
                tapping_.reset();
            }
            if (scrollDragMoved_) {
                const Element& el = def_->elements[*scrollDrag_];
                st.scroll = scrollWas_ + dy * el.dragDirection();
                st.scrollX = scrollWasX_ - dx;
                dragTrail_.emplace_back(now(), SDL_FPoint{e.motion.x, e.motion.y});
                if (dragTrail_.size() > 8) dragTrail_.erase(dragTrail_.begin());
                return true;
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            const std::size_t box = *std::exchange(scrollDrag_, std::nullopt);
            const bool moved = std::exchange(scrollDragMoved_, false), bar = std::exchange(scrollBarDrag_, false);
            const Element& el = def_->elements[box];
            if (moved && el.glides(engine_.gameSettings().glide) && dragTrail_.size() >= 2) { // let go while moving: it glides on, as fast as it went over the last moments
                const auto& [then, at] = dragTrail_.back();
                auto from = std::ranges::find_if(dragTrail_, [&](const auto& s) { return then - s.first <= 0.15f; });
                if (from == dragTrail_.end() - 1) --from;
                const float dt = std::max(0.005f, then - from->first);
                const float speed = std::max(0.0f, el.number("glide_speed", engine_.gameSettings().glideSpeed)); // (glide_speed=150%: faster than it was let go; none: the game's)
                ElementState& st = state_[box];
                st.glideX = -std::clamp((at.x - from->second.x) / dt, -5000.0f, 5000.0f) * speed; // (sideways: what's in it follows the mouse)
                st.glideY = std::clamp((at.y - from->second.y) / dt, -5000.0f, 5000.0f) * speed * el.dragDirection();
                st.glideAt = now();
            }
            dragTrail_.clear();
            if (moved || bar) {
                tapping_.reset();
                return true;
            }
            if (tapping_) { // (a list's item, pressed and let go without dragging: tapped)
                const Target hit = *std::exchange(tapping_, std::nullopt);
                if (const Target* t = targetAt(e.button.x, e.button.y); t && t->spot == hit.spot) activate(hit, e.button.x);
                return true;
            }
            if (!pressing_ && !held_) { // (on nothing to click: a tap, as a click elsewhere would be; a button pressed is clicked below)
                tapped_ = true;
                return true;
            }
        }
    }

    if (handleInput(e)) return true;
    if (e.type == SDL_EVENT_MOUSE_WHEEL && wheelAt(mouse_, e.wheel.y, e.wheel.x)) return true;

    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        mouse_ = {e.motion.x, e.motion.y};
        if (held_ && std::hypot(mouse_.x - pressed_.x, mouse_.y - pressed_.y) > 6.0f) heldMoved_ = true;
        if (const Target* t = targetAt(e.motion.x, e.motion.y)) setFocus(*t);
        else focus_.reset();
        keyFocus_ = false; // (the pointer's now)
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP: // a button is clicked when the mouse comes up on it; let go of what's dragged: on a part that takes it, or back where it was
        if (e.button.button == SDL_BUTTON_LEFT && pressing_) {
            const std::size_t was = *std::exchange(pressing_, std::nullopt);
            const Target* t = engine_.frameNumber() <= drawnFrame_ + 1 ? targetAt(e.button.x, e.button.y) : nullptr; // (not if it closed meanwhile)
            if (t && t->spot.element == was) {
                const Target hit = *t;
                activate(hit, e.button.x);
            }
            return true;
        }
        if (e.button.button == SDL_BUTTON_LEFT && held_ && heldMoved_) {
            const Target* t = targetAt(e.button.x, e.button.y);
            if (t && !def_->elements[t->spot.element].drop.empty()) drop(def_->elements[t->spot.element]);
            else held_.reset();
            return true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) {
            for (std::size_t i = 0; i < def_->elements.size(); ++i) // (an input box clicked: the typing goes there)
                if (def_->elements[i].kind == ElementKind::Input && live(i) && pointInRect(e.button.x, e.button.y, rectOf(i))) typing_ = i;
            if (const auto box = scrollableAt(e.button.x, e.button.y)) { // (dragging scrolls it: its parts, or its scrollbar's thumb)
                scrollDrag_ = box;
                scrollFrom_ = {e.button.x, e.button.y};
                scrollWas_ = state_[*box].scroll;
                scrollWasX_ = state_[*box].scrollX;
                state_[*box].glideX = state_[*box].glideY = 0.0f; // (a touch stops it gliding)
                dragTrail_.assign(1, {now(), scrollFrom_});
                scrollDragMoved_ = false;
                const Element& el = def_->elements[*box];
                const bool bar = lower(el.option("scrollbar", "no")) == "yes";
                scrollBarAcross_ = bar && state_[*box].reachX > rectOf(*box).w && pointInRect(e.button.x, e.button.y, barOf(*box, true));
                scrollBarDrag_ = scrollBarAcross_ || (bar && state_[*box].reach > rectOf(*box).h && pointInRect(e.button.x, e.button.y, barOf(*box, false)));
                if (scrollBarDrag_) return true;
            }
            if (const Target* t = targetAt(e.button.x, e.button.y)) {
                const Target hit = *t; // (activate() may change the targets)
                setFocus(hit);
                keyFocus_ = false;
                mouse_ = {e.button.x, e.button.y};
                const Element& el = def_->elements[hit.spot.element];
                if (el.kind == ElementKind::Button && el.option("drag").empty() && !(held_ && !el.drop.empty())) {
                    pressing_ = hit.spot.element; // (clicked when the mouse comes up on it)
                    return true;
                }
                if (scrollDrag_ && el.dragDirection() != 0.0f) { // (a list's item: when the mouse comes up on it, unless that's a drag)
                    tapping_ = hit;
                    return true;
                }
                activate(hit, e.button.x);
                return true;
            }
            if (held_) { // a click elsewhere puts it back
                held_.reset();
                return true;
            }
            if (scrollDrag_) return true; // (a list or a container pressed on: dragging scrolls it)
        }
        break;

    default:
        break;
    }
    return def_->storyWaits && isInput(e);
}

// A slider: a value to drag (a variable's or a player setting's: a track, filled up to its knob), or one shown
// (value=: its box filled as far as the value goes from minimum= to maximum=, across, or up with vertical=yes).
void ScreenView::drawSlider(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    const float lo = number(el, "minimum", 0.0f), hi = number(el, "maximum", 100.0f);
    if (el.exprs.contains("value")) { // (shown: nothing to drag)
        const float a = boxAlpha(s, alpha), f = hi > lo ? std::clamp((number(el, "value", 0.0f) - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
        const SDL_FRect rr = rectOf(index);
        draw::rect(r, rr, withAlpha(el.colour("track", s.background.a ? s.background : SDL_Color{60, 64, 92, 255}), a));
        const bool vertical = el.option("vertical", "no") == "yes";
        draw::rect(r, vertical ? SDL_FRect{rr.x, rr.y + rr.h * (1.0f - f), rr.w, rr.h * f} : SDL_FRect{rr.x, rr.y, rr.w * f, rr.h}, withAlpha(el.colour("fill", {150, 170, 255, 255}), a));
        if (s.border.a > 0) draw::outline(r, rr, withAlpha(s.border, a));
        return;
    }
    const bool on = enabled(el);
    const float a = boxAlpha(s, alpha) * (on ? 1.0f : 0.35f);
    const float value = static_cast<float>(toNumber(engine_.game().variable(el.option("variable"))));
    const SettingControl* c = findSettingControl(el.option("setting"));
    const float f = c ? std::clamp(c->value(engine_.settings()), 0.0f, 1.0f) : hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    const SDL_FRect rr = sliderBox(index);
    const float my = rr.y + rr.h * 0.5f;
    const bool hot = focused(index, 0) || dragging_ == index;
    draw::rect(r, {rr.x, my - 2, rr.w, 4}, withAlpha(el.colour("track", {60, 64, 92, 255}), a));
    draw::rect(r, {rr.x, my - 2, rr.w * f, 4}, withAlpha(el.colour("fill", {150, 170, 255, 255}), a));
    draw::circle(r, rr.x + rr.w * f, my, hot ? 10.0f : 8.0f, withAlpha(el.colour("knob", s.colour), a));
    if (on) targets_.push_back({{index}, rr, true});
}

// A slider's box (inside the part it's in, its place there when it was drawn).
SDL_FRect ScreenView::sliderBox(std::size_t element) const {
    const auto it = std::ranges::find(targets_, Spot{element}, &Target::spot);
    return it != targets_.end() ? it->rect : rectOf(element);
}

} // namespace vn
