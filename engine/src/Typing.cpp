// Typing into a box (an input part), and the on-screen keyboard over it, for playing without a keyboard.
#include "Engine.hpp"

#include <algorithm>
#include <cmath>

namespace vn {

void ScreenView::drawInput(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const std::string var = el.option("variable");
    if (var.empty()) return;
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    const bool on = enabled(el);
    const bool typed = on && typedInto() == index;
    const Look look = !on ? Look::Disabled : typed || focused(index) ? Look::Hover : Look::Idle;
    box(r, rectOf(index), s, look, boxAlpha(s, alpha));

    const std::string value = engine_.game().typedValue(var);
    const bool empty = value.empty();
    const std::string shown = empty ? el.option("placeholder") : value;
    const SDL_Color colour = empty ? withAlpha(textColour(s, look), 0.5f) : textColour(s, look);
    text(r, st.text, shown, rectOf(index), s, colour, alpha * s.opacity);

    if (typed) {
        engine_.wantTextInput();
        if (engine_.padInUse() && !engine_.keyboardOpen() && !keyboardShut_ && !tools_) engine_.wantKeyboard(*this, index, true); // (a controller: the keyboard, by itself)
        if (still() || std::fmod(now(), 1.0f) < 0.6f) { // blinking caret where typing goes (still: steady)
            Font& font = styleFont(s);
            const float w = empty ? 0.0f : font.measure(value), h = font.lineHeight();
            const float at = empty ? 0.0f : font.measure(value.substr(0, utf8::offset(value, caretAt(index, value))));
            const float left = rectOf(index).x + s.padding + (s.align == Align::Centre ? (rectOf(index).w - 2 * s.padding - w) * 0.5f
                                                       : s.align == Align::End ? rectOf(index).w - 2 * s.padding - w : 0.0f);
            const float top = rectOf(index).y + (rectOf(index).h > 0 ? (rectOf(index).h - h) * 0.5f : s.padding);
            draw::rect(r, {left + at + 2, top, 2, h}, withAlpha(textColour(s, Look::Hover), alpha * s.opacity));
        }
    }
    if (on) targets_.push_back({{index}, rectOf(index), true});
}

std::optional<std::size_t> ScreenView::typedInto() {
    auto takes = [&](std::size_t i) {
        const Element& el = def_->elements[i];
        return el.kind == ElementKind::Input && live(i) && enabled(el) && !el.option("variable").empty();
    };
    if (const Target* t = keyFocus_ ? focusedTarget() : nullptr; t && t->spot.element < def_->elements.size() && takes(t->spot.element)) return t->spot.element; // (the keys on one)
    if (typing_ && *typing_ < def_->elements.size() && takes(*typing_)) return typing_;
    for (std::size_t i = 0; i < def_->elements.size(); ++i)
        if (takes(i)) return i;
    return std::nullopt;
}

bool ScreenView::handleInput(const SDL_Event& e) {
    if (e.type != SDL_EVENT_TEXT_INPUT && e.type != SDL_EVENT_KEY_DOWN) return false;
    if (const auto i = typedInto()) {
        const Element& el = def_->elements[*i];
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            typeInto(*i, e.text.text ? e.text.text : "");
            if (engine_.keyboardOpen()) { // (typed on a real keyboard: the one on the screen goes)
                engine_.closeKeyboard();
                keyboardShut_ = true;
            }
            return true;
        }
        if (e.key.key == SDLK_BACKSPACE) {
            eraseLast(*i);
            return true;
        }
        if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && !e.key.repeat && !el.actions.empty()) {
            run(el);
            return true;
        }
        // A key that types (without Ctrl, Cmd or Alt): its letter comes as text, and nothing else takes it (the
        // game's keys, the phone's...).
        return e.key.key >= SDLK_SPACE && e.key.key <= SDLK_TILDE && !(e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT));
    }
    return false;
}

std::size_t ScreenView::caretAt(std::size_t index, std::string_view value) const {
    const std::size_t letters = utf8::length(value);
    return caretBox_ == index ? letters - std::min(caretBack_, letters) : letters;
}

void ScreenView::typeInto(std::size_t index, std::string_view text) {
    const Element& el = def_->elements[index];
    const std::string var = el.option("variable");
    std::string value = engine_.game().typedValue(var);
    const int max = static_cast<int>(number(el, "most_letters", 64.0f));
    std::string typed(text);
    std::erase_if(typed, [](char c) { return c == '\n' || c == '\r' || c == '{' || c == '}'; });
    std::size_t at = utf8::offset(value, caretAt(index, value)); // (where it goes: at the caret; it stays after what's added)
    for (std::size_t n = 0; n < typed.size();) { // add codepoints until the limit
        const auto len = utf8::offset(std::string_view(typed).substr(n), 1);
        if (max > 0 && static_cast<int>(utf8::length(value)) >= max) break;
        value.insert(at, typed.substr(n, len));
        at += len;
        n += std::max<std::size_t>(len, 1);
    }
    engine_.game().setVariable(var, value);
}

void ScreenView::eraseLast(std::size_t index) {
    const std::string var = def_->elements[index].option("variable");
    std::string value = engine_.game().typedValue(var);
    const std::size_t at = caretAt(index, value);
    if (at == 0) return;
    const std::size_t from = utf8::offset(value, at - 1);
    value.erase(from, utf8::offset(value, at) - from);
    engine_.game().setVariable(var, value);
}

void ScreenView::moveCaret(std::size_t index, int by) {
    const std::size_t letters = utf8::length(engine_.game().typedValue(def_->elements[index].option("variable")));
    if (caretBox_ != index) caretBox_ = index, caretBack_ = 0;
    caretBack_ = static_cast<std::size_t>(std::clamp(static_cast<long>(caretBack_) - by, 0L, static_cast<long>(letters)));
}

std::string ScreenView::keyLetter(const Element& el) const {
    const auto* u = el.kind == ElementKind::Button && el.actions.size() == 1 ? std::get_if<cmd::Ui>(&el.actions.front()) : nullptr;
    if (!u || u->verb != "type" || utf8::length(u->arg) != 1) return {};
    return engine_.keyboardShift() ? shiftedKey(u->arg) : u->arg;
}

void ScreenView::enterBox(std::size_t index) {
    if (index < def_->elements.size() && !def_->elements[index].actions.empty()) run(def_->elements[index]);
}

std::string ScreenView::boxText(std::size_t index) const {
    return index < def_->elements.size() ? engine_.game().typedValue(def_->elements[index].option("variable")) : std::string{};
}

void Engine::wantKeyboard(const ScreenView& view, std::size_t element, bool keys) {
    for (const auto& [name, v] : views_)
        if (&v == &view) keyboardWanted_ = std::pair{KeyboardFor{.screen = name, .element = element, .shift = false}, keys};
}

void Engine::closeKeyboard() {
    if (!keyboardFor_) return;
    if (ScreenView* box = view(keyboardFor_->screen)) box->keyboardShut(); // (it stays away until the box is pressed again)
    keyboardFor_.reset();
    if (const auto at = std::ranges::find(overlays_, std::string("keyboard")); at != overlays_.end()) {
        if (at + 1 == overlays_.end()) closeNow();
        else overlays_.erase(at); // (something opened over it: it goes from under that)
    }
}

std::string Engine::keyboardText() const {
    if (!keyboardFor_) return {};
    const auto it = views_.find(keyboardFor_->screen);
    return it != views_.end() ? it->second.boxText(keyboardFor_->element) : std::string{};
}

void Engine::keyboardKey(std::string_view key) {
    if (!keyboardFor_) return;
    ScreenView* v = view(keyboardFor_->screen);
    if (!v) return closeKeyboard();
    const std::size_t box = keyboardFor_->element;
    if (key == "shift") keyboardFor_->shift = !keyboardFor_->shift;
    else if (key == "space") v->typeIntoBox(box, " ");
    else if (key == "left" || key == "right") v->moveCaret(box, key == "left" ? -1 : 1);
    else if (key == "delete") {
        v->eraseInBox(box);
        if (v->boxText(box).empty()) keyboardFor_->shift = true; // (a name starts with a capital)
    } else if (key == "done") {
        closeKeyboard();
        v->enterBox(box);
    } else {
        v->typeIntoBox(box, keyboardFor_->shift ? shiftedKey(key) : std::string(key));
        keyboardFor_->shift = false;
    }
}

} // namespace vn
