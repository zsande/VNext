// The developer tools' widgets (DeveloperUi.hpp): text, buttons, lists, boxes to type into, choices,
// colours.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

namespace vn {

namespace {

// Values a box often has that are all plain numbers (seconds, volumes, sizes...): typed, not listed.
bool justNumbers(std::string_view suggest) {
    for (auto&& one : std::views::split(suggest, '|')) {
        std::string_view v(one.begin(), one.end());
        if (v.contains(':')) return false; // (one that says what it is)
        if (v.ends_with('%') || v.ends_with('s')) v.remove_suffix(1);
        if (!v.empty() && !parseNumber(v)) return false;
    }
    return true;
}

// A kind's buttons as a list's entries ("word:what it says"), where a list button is of that kind.
std::string listOfChoices(const StoryEditor::Field& f) {
    const auto words = wordsOf(f.choices), labels = wordsOf(f.labels, '|');
    std::string text;
    for (std::size_t i = 0; i < words.size(); ++i) {
        std::string said(i < labels.size() ? labels[i] : words[i]);
        if (!said.empty()) said[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(said[0]))); // (as a list says things)
        text += std::format("{}{}:{}", text.empty() ? "" : "|", words[i], said);
    }
    return text;
}

std::size_t nextLetter(const std::string& s, std::size_t i) {
    if (i >= s.size()) return s.size();
    do ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}

} // namespace

TextBlock& DeveloperUi::block() {
    if (d.used == d.texts.size()) d.texts.emplace_back();
    return d.texts[d.used++];
}

SDL_FPoint DeveloperUi::text(std::string_view s, float x, float y, float size, SDL_Color color, float wrap) {
    TextBlock& b = block();
    b.set(font(size), s, wrap);
    b.draw(r, x, y, color);
    return b.size();
}

float DeveloperUi::width(std::string_view s, float size) {
    TextBlock& b = block();
    b.set(font(size), s);
    return b.size().x;
}

SDL_FPoint DeveloperUi::measure(std::string_view s, float size, float wrap) {
    TextBlock& b = block();
    b.set(font(size), s, wrap);
    return b.size();
}

void DeveloperUi::textIn(std::string_view s, const SDL_FRect& box, float size, SDL_Color color) {
    TextBlock& b = block();
    b.set(font(size), s);
    const Clip clipped(*this, box);
    b.drawIn(r, box, 0.5f, 0.5f, color);
}

DeveloperUi::Clip::Clip(DeveloperUi& ui, const SDL_FRect& box, bool clicks) : ui_(ui), within_(ui.within_) {
    ui_.clip(box);
    if (clicks) ui_.within_ = &box;
}

DeveloperUi::Clip::~Clip() {
    ui_.within_ = within_;
    ui_.unclip();
}

void DeveloperUi::clip(const SDL_FRect& box) {
    SDL_Rect area{static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h)};
    if (!clips_.empty() && !SDL_GetRectIntersection(&clips_.back(), &area, &area)) area = {};
    clips_.push_back(area);
    SDL_SetRenderClipRect(r, &area);
}

void DeveloperUi::unclip() {
    if (!clips_.empty()) clips_.pop_back();
    SDL_SetRenderClipRect(r, clips_.empty() ? nullptr : &clips_.back());
}

bool DeveloperUi::click(const SDL_FRect& box) {
    if (!d.clicked || !over(box) || (within_ && !over(*within_))) return false;
    d.clicked = false;
    return true;
}

bool DeveloperUi::button(const SDL_FRect& box, std::string_view label, bool chosen, bool enabled, float size) {
    draw::rect(r, box, chosen ? kChosen : enabled && over(box) ? kButtonHover : kButton);
    draw::outline(r, box, kEdge);
    textIn(label, box, size, enabled ? theme::text : theme::dim);
    return click(box) && enabled;
}

std::optional<std::size_t> DeveloperUi::list(const SDL_FRect& box, float& scroll, std::size_t count, const std::function<std::string(std::size_t)>& label,
                                             const std::function<bool(std::size_t)>& chosen, const std::function<bool(std::size_t)>& wrong,
                                             const std::function<bool(std::size_t)>& faint) {
    const float itemH = layout::listItemH * k;
    draw::rect(r, box, kBox);
    scroll = std::clamp(scroll - wheelOver(box) * itemH * 2, 0.0f, std::max(0.0f, static_cast<float>(count) * itemH - box.h));
    std::optional<std::size_t> clicked;
    const Clip clipped(*this, box);
    for (std::size_t i = 0; i < count; ++i) {
        const SDL_FRect item{box.x, box.y + static_cast<float>(i) * itemH - scroll, box.w, itemH - 2 * k};
        if (item.y + item.h < box.y || item.y > box.y + box.h) continue;
        if (chosen(i) || over(item)) draw::rect(r, item, chosen(i) ? kChosen : kButtonHover);
        if (wrong(i)) draw::rect(r, {item.x, item.y, layout::markW * k, item.h}, kBad);
        text(label(i), item.x + layout::listPad * k, item.y + 5 * k, layout::listText, faint && faint(i) ? theme::dim : theme::text);
        if (over(box) && click(item)) clicked = i;
    }
    if (count == 0) text("Empty", box.x + layout::listPad * k, box.y + 5 * k, layout::listText, theme::dim);
    return clicked;
}

DeveloperUi::ListEvent DeveloperUi::editList(const std::string& id, const SDL_FRect& box, float& scroll, const ListItems& items) {
    using Event = ListEvent::Kind;
    const float itemH = items.itemH * k;
    ListEvent event;
    draw::rect(r, box, kBox);
    scroll = std::clamp(scroll - wheelOver(box) * itemH * 2, 0.0f, std::max(0.0f, static_cast<float>(items.count) * itemH - box.h));
    const bool dragging = d.listDrag && d.listDrag->list == id;
    if (dragging && !d.listDrag->moving && std::abs(d.mouseY - d.listDrag->y) > 6 * k) d.listDrag->moving = true;
    std::optional<int> depth; // (how far in it would go)
    const std::optional<std::size_t> slot = [&]() -> std::optional<std::size_t> { // where it would go
        if (!dragging || !d.listDrag->moving || !over(box)) return std::nullopt;
        const auto at = static_cast<std::size_t>(std::clamp(std::round((d.mouseY - box.y + scroll) / itemH), 0.0f, static_cast<float>(items.count)));
        if (items.place) {
            depth = items.place(d.listDrag->item, at, d.mouseX - d.listDrag->x);
            return depth ? std::optional(at) : std::nullopt;
        }
        if (at == d.listDrag->item || at == d.listDrag->item + 1 || (items.drops && !items.drops(d.listDrag->item, at))) return std::nullopt;
        return at;
    }();
    const Clip clipped(*this, box);
    for (std::size_t i = 0; i < items.count; ++i) {
        const float in = items.indent ? items.indent(i) : 0.0f;
        const SDL_FRect item{box.x + in, box.y + static_cast<float>(i) * itemH - scroll, box.w - in, itemH - 2 * k};
        if (item.y + item.h < box.y || item.y > box.y + box.h) continue;
        const std::string renameId = std::format("{}:rename:{}", id, i), armId = std::format("{}:{}", id, i);
        const bool renaming = d.focus == renameId, armed = d.armed == armId, pointed = over(item) && over(box), chosen = items.chosen(i);
        if (chosen || pointed) draw::rect(r, item, chosen ? kChosen : kButtonHover);
        if (items.wrong && items.wrong(i)) draw::rect(r, {item.x, item.y, layout::markW * k, item.h}, kBad);
        else if (items.unreached && items.unreached(i)) draw::rect(r, {item.x, item.y, layout::markW * k, item.h}, theme::dim);
        const bool deletes = !renaming && items.deletes && items.deletes(i) && (chosen || pointed || armed);
        const float xw = (armed ? layout::crossArmedW : layout::crossW) * k;
        const SDL_FRect cross{item.x + item.w - xw - 3 * k, item.y + 3 * k, xw, item.h - 6 * k};
        if (renaming) {
            if (auto name = field(renameId, item, items.name ? items.name(i) : items.label(i), 15)) event = {.kind = Event::Renamed, .item = i, .to = 0, .name = std::move(*name)};
        } else {
            const SDL_FRect content{item.x, item.y, item.w - (deletes ? xw + 6 * k : 0.0f), item.h};
            const Clip clippedItem(*this, content);
            if (items.paint) items.paint(i, content);
            else textAround(items.label(i), item.x + layout::listPad * k, item.y + item.h / 2, layout::listText, items.faint && items.faint(i) ? theme::dim : theme::text);
        }
        if (deletes) {
            draw::rect(r, cross, armed ? SDL_Color{150, 50, 64, 255} : over(cross) ? kButtonHover : kButton);
            draw::outline(r, cross, armed ? kBad : kEdge);
            textIn(armed ? "Delete" : "x", cross, 14, theme::text);
        }
        if (renaming || !d.clicked || !pointed) continue;
        d.clicked = false;
        if (deletes && over(cross)) {
            if (armed) event = {.kind = Event::Deleted, .item = i, .to = 0, .name = {}};
            d.armed = armed ? std::string() : armId;
            continue;
        }
        d.armed.clear();
        if (d.doubleClick && items.renames && items.renames(i)) {
            d.focusOn(renameId, items.name ? items.name(i) : items.label(i));
            continue;
        }
        event = {.kind = Event::Chosen, .item = i, .to = 0, .name = {}};
        d.lastList = id; // (copy and paste act on it)
        if (items.moves && items.moves(i)) d.listDrag = DeveloperTools::ListDrag{.list = id, .item = i, .x = d.mouseX, .y = d.mouseY};
    }
    if (slot) {
        const float in = depth ? static_cast<float>(*depth) * items.indentStep : 0.0f;
        draw::rect(r, {box.x + in, box.y + static_cast<float>(*slot) * itemH - scroll - 2 * k, box.w - in, 3 * k}, kAccent);
    }
    if (items.count == 0) textAround("Empty", box.x + layout::listPad * k, box.y + itemH / 2, layout::listText, theme::dim);
    if (dragging && !d.held) {
        if (slot) event = {.kind = Event::Moved, .item = d.listDrag->item, .to = *slot, .name = {}, .depth = depth.value_or(0)};
        d.listDrag.reset();
    }
    return event;
}

std::optional<std::size_t> DeveloperUi::buttons(float x, float y, float w, float h, const std::vector<std::string_view>& labels, const std::vector<bool>& enabled,
                                                float size) {
    const float gap = layout::gap * k;
    std::vector<float> widths;
    float total = gap * static_cast<float>(labels.size() - 1);
    for (const std::string_view label : labels) total += widths.emplace_back(width(label, size) + layout::buttonPad * k);
    const float fit = std::min(1.0f, w / total);
    std::optional<std::size_t> clicked;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (button({x, y, widths[i] * fit, h}, labels[i], false, enabled[i], size)) clicked = i;
        x += (widths[i] + gap) * fit;
    }
    return clicked;
}

bool DeveloperUi::pairedButtons(std::string_view a, std::string_view b) {
    static constexpr std::pair<std::string_view, std::string_view> kPairs[]{
        {"yes", "no"},         {"on", "off"},        {"equal to", "not equal to"}, {"at least", "at most"}, {"more than", "less than"},
        {"left", "right"},     {"top", "bottom"},    {"in", "out"},                {"show", "hide"},        {"open", "close"},
        {"bold", "italic"},    {"next", "previous"}, {"bigger", "smaller"},        {"quick save", "quick load"},
    };
    const std::string x = lower(a), y = lower(b);
    return std::ranges::any_of(kPairs, [&](const auto& pair) { return (pair.first == x && pair.second == y) || (pair.first == y && pair.second == x); });
}

std::vector<SDL_FRect> DeveloperUi::flow(const std::vector<std::string_view>& labels, float w, float h, float gap, float size, const std::vector<bool>& with,
                                         float pad) {
    std::vector<SDL_FRect> out;
    const std::size_t n = labels.size();
    std::vector<float> widths(n);
    for (std::size_t i = 0; i < n; ++i) widths[i] = std::min(w, width(labels[i], size) + pad * k); // (snug, like a form's choices: more to a row)
    auto joined = [&](std::size_t i) { return i > 0 && i < n && ((i < with.size() && with[i]) || pairedButtons(labels[i - 1], labels[i])); };
    float x = 0.0f, y = 0.0f;
    for (std::size_t i = 0; i < n;) {
        std::size_t end = i + 1; // (the group: i up to end)
        float groupW = widths[i];
        while (end < n && joined(end)) groupW += gap + widths[end++];
        if (x > 0.0f && x + groupW > w) {
            x = 0.0f;
            y += h + gap;
        }
        for (; i < end; ++i) {
            if (x > 0.0f && x + widths[i] > w) { // (a group wider than a row: split after all)
                x = 0.0f;
                y += h + gap;
            }
            out.push_back({x, y, widths[i], h});
            x += widths[i] + gap;
        }
    }
    return out;
}

DeveloperUi::ChoiceButtons DeveloperUi::choiceButtons(const SDL_FRect& box, const StoryEditor::Field& f) {
    ChoiceButtons out;
    auto& [words, shown, places] = out;
    std::vector<std::string_view> labels;
    for (auto&& w : std::views::split(f.choices, ' ')) words.emplace_back(w.begin(), w.end());
    for (auto&& l : std::views::split(f.labels, '|')) labels.emplace_back(l.begin(), l.end());
    if (words.size() == 2 && words[0] == "no" && words[1] == "yes") { // (Yes, then No, everywhere)
        std::ranges::reverse(words);
        if (labels.size() == 2) std::ranges::reverse(labels);
    }
    float widest = 0.0f;
    for (std::size_t i = 0; i < words.size(); ++i) {
        std::string label = i < labels.size() && !f.labels.empty() ? std::string(labels[i]) : std::string(words[i]);
        label = upperFirst(spaced(label));
        widest = std::max(widest, width(label, layout::buttonText) + layout::choicePad * k);
        shown.push_back(std::move(label));
    }
    const float gap = layout::gap * k;
    const auto n = words.size();
    // Their places: all alike in one row when they fit, else as wide as their labels, in rows
    // stretched to the box's width.
    places.resize(n);
    if (static_cast<float>(n) * (widest + gap) - gap <= box.w) {
        const float cw = (box.w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (std::size_t i = 0; i < n; ++i) places[i] = {box.x + static_cast<float>(i) * (cw + gap), box.y, cw, box.h};
    } else {
        std::size_t rowStart = 0;
        float x = 0.0f, rowY = box.y;
        auto finish = [&](std::size_t end) { // (the row's buttons share out what's left of its width)
            float used = gap * static_cast<float>(end - rowStart - 1);
            for (std::size_t j = rowStart; j < end; ++j) used += places[j].w;
            const float extra = (box.w - used) / static_cast<float>(end - rowStart);
            float at = box.x;
            for (std::size_t j = rowStart; j < end; ++j) {
                places[j] = {at, rowY, places[j].w + extra, box.h};
                at += places[j].w + gap;
            }
        };
        auto widthOf = [&](std::size_t i) { return std::min(box.w, width(shown[i], layout::buttonText) + layout::choicePad * k); }; // (as snug as the one-row ones: more to a row)
        for (std::size_t i = 0; i < n; ++i) {
            const float bw = widthOf(i);
            bool wrap = i > rowStart && x + gap + bw > box.w;
            if (!wrap && i > rowStart && i + 1 < n && pairedButtons(shown[i], shown[i + 1])) wrap = x + gap + bw + gap + widthOf(i + 1) > box.w; // (its pair comes along)
            if (wrap) {
                finish(i);
                rowStart = i;
                rowY += box.h + gap;
                x = 0.0f;
            }
            x += (i > rowStart ? gap : 0.0f) + bw;
            places[i].w = bw;
        }
        finish(n);
    }
    return out;
}

float DeveloperUi::choicesHeight(const StoryEditor::Field& f, float w, float h) {
    const auto buttons = choiceButtons({0.0f, 0.0f, w, h}, f);
    return buttons.places.empty() ? h : buttons.places.back().y + h;
}

std::optional<std::string> DeveloperUi::choices(const SDL_FRect& box, const StoryEditor::Field& f, float& y) {
    const auto [words, shown, places] = choiceButtons(box, f);
    const auto n = words.size();
    std::vector<std::string_view> on;
    for (auto&& w : std::views::split(std::string_view(f.value), ' '))
        if (!std::string_view(w.begin(), w.end()).empty()) on.emplace_back(w.begin(), w.end());
    std::optional<std::string> clicked;
    for (std::size_t i = 0; i < n; ++i) {
        const bool chosen = f.multi ? std::ranges::contains(on, words[i]) : f.value == words[i];
        if (!button(places[i], shown[i], chosen, true, 14)) continue;
        if (!f.multi) {
            if (!chosen) clicked = std::string(words[i]);
            continue;
        }
        std::string now;
        for (const std::string_view w : words) {
            const bool keep = w == words[i] ? !chosen : std::ranges::contains(on, w) && words[i] != "none" && w != "none";
            if (keep) now += (now.empty() ? "" : " ") + std::string(w);
        }
        clicked = now;
    }
    y = places.back().y - box.y + y; // (to the last row)
    return clicked;
}

bool DeveloperUi::tab(const SDL_FRect& box, std::string_view label, bool chosen, bool wrong) {
    if (!chosen && over(box)) draw::rect(r, box, kButton);
    if (chosen) draw::rect(r, {box.x + 8 * k, box.y + box.h - 3 * k, box.w - 16 * k, 3 * k}, wrong ? kBad : kAccent);
    textIn(label, box, 16, wrong ? kBad : chosen ? kAccent : over(box) ? theme::text : theme::dim);
    return click(box);
}

void DeveloperUi::textAround(std::string_view s, float x, float middle, float size, SDL_Color color, float wrap) {
    TextBlock& b = block();
    b.set(font(size), s, wrap);
    b.draw(r, x, middle - b.size().y / 2, color);
}

std::optional<std::string> DeveloperUi::field(const std::string& id, const SDL_FRect& box, const std::string& value, float size, std::string_view hint,
                                              bool spelling) {
    const float pad = 8 * k;
    auto prefix = [&](const std::string& text, std::size_t n) { return width(std::string_view(text).substr(0, n), size); };
    auto shiftOf = [&](const std::string& text, std::size_t caret) { return std::max(0.0f, prefix(text, caret) - (box.w - 2 * pad)); };
    auto letterAt = [&](float x, const std::string& text, float shift) { // the place between letters nearest x
        std::vector<std::size_t> stops{0};
        for (std::size_t i = 0; i < text.size();) stops.push_back(i = nextLetter(text, i));
        const float target = x - (box.x + pad - shift);
        std::size_t lo = 0, hi = stops.size() - 1;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (prefix(text, stops[mid]) < target) lo = mid + 1;
            else hi = mid;
        }
        if (lo > 0 && target - prefix(text, stops[lo - 1]) < prefix(text, stops[lo]) - target) --lo;
        return stops[lo];
    };
    auto wordChar = [](char c) { return isWordChar(c) || (static_cast<unsigned char>(c) & 0x80); };
    const bool wasFocused = d.focus == id;
    if (click(box)) {
        const float shift = wasFocused ? shiftOf(d.buffer, d.caret) : 0.0f;
        if (!wasFocused) d.focusOn(id, value);
        const std::size_t at = letterAt(d.mouseX, d.buffer, shift);
        if (d.doubleClick) { // a word
            std::size_t from = at, to = at;
            while (from > 0 && wordChar(d.buffer[from - 1])) --from;
            while (to < d.buffer.size() && wordChar(d.buffer[to])) ++to;
            d.anchor = from;
            d.caret = to;
        } else {
            if (!(wasFocused && (SDL_GetModState() & SDL_KMOD_SHIFT))) d.anchor = at;
            d.caret = at;
            d.selectingIn = id;
        }
        d.anchorFocus = d.focusCount;
    }
    const bool focused = d.focus == id;
    std::optional<std::string> changed;
    if (focused) {
        engine_.wantTextInput();
        d.caret = std::min(d.caret, d.buffer.size());
        if (d.anchorFocus != d.focusCount) { // (put in the box from elsewhere: nothing chosen)
            d.anchor = d.caret;
            d.anchorFocus = d.focusCount;
        }
        d.anchor = std::min(d.anchor, d.buffer.size());
        if (d.selectingIn == id && d.held && !d.doubleClick) d.caret = letterAt(d.mouseX, d.buffer, shiftOf(d.buffer, d.caret));
        auto chosen = [&] { return std::pair{std::min(d.anchor, d.caret), std::max(d.anchor, d.caret)}; };
        auto eraseChosen = [&] {
            const auto [from, to] = chosen();
            if (from == to) return false;
            d.buffer.erase(from, to - from);
            d.caret = d.anchor = from;
            return true;
        };
        auto put = [&](const std::string& text) {
            eraseChosen();
            d.buffer.insert(d.caret, text);
            d.caret += text.size();
            d.anchor = d.caret;
            changed = d.buffer;
        };
        if (!d.typed.empty()) {
            put(d.typed);
            d.typed.clear();
        }
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            const bool command = key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL), shift = key.mod & SDL_KMOD_SHIFT;
            auto moveTo = [&](std::size_t to) { // (Shift: choosing as it goes)
                d.caret = to;
                if (!shift) d.anchor = to;
            };
            switch (key.key) {
            case SDLK_BACKSPACE:
                if (eraseChosen()) changed = d.buffer;
                else if (const auto from = previousLetter(d.buffer, d.caret); from < d.caret) {
                    d.buffer.erase(from, d.caret - from);
                    d.caret = d.anchor = from;
                    changed = d.buffer;
                }
                return true;
            case SDLK_DELETE:
                if (eraseChosen()) changed = d.buffer;
                else if (const auto to = nextLetter(d.buffer, d.caret); to > d.caret) {
                    d.buffer.erase(d.caret, to - d.caret);
                    changed = d.buffer;
                }
                return true;
            case SDLK_LEFT: moveTo(!shift && d.anchor != d.caret ? chosen().first : previousLetter(d.buffer, d.caret)); return true;
            case SDLK_RIGHT: moveTo(!shift && d.anchor != d.caret ? chosen().second : nextLetter(d.buffer, d.caret)); return true;
            case SDLK_HOME: moveTo(0); return true;
            case SDLK_END: moveTo(d.buffer.size()); return true;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                d.entered = id;
                d.focus.clear();
                return true;
            case SDLK_TAB:
            case SDLK_ESCAPE: d.focus.clear(); return true;
            case SDLK_A:
                if (!command) return false;
                d.anchor = 0;
                d.caret = d.buffer.size();
                return true;
            case SDLK_V:
                if (!command) return false;
                if (const SdlOwned<char> pasted{SDL_GetClipboardText()}) {
                    std::string text = pasted.get();
                    std::erase(text, '\r');
                    std::ranges::replace(text, '\n', ' ');
                    put(text);
                }
                return true;
            case SDLK_C:
            case SDLK_X: {
                if (!command) return false;
                const auto [from, to] = chosen();
                SDL_SetClipboardText((from == to ? d.buffer : d.buffer.substr(from, to - from)).c_str()); // (nothing chosen: all of it)
                if (key.key == SDLK_X) {
                    if (from == to) d.anchor = 0, d.caret = d.buffer.size();
                    eraseChosen();
                    changed = d.buffer;
                }
                return true;
            }
            default: return false;
            }
        });
    }
    if (!d.held && d.selectingIn == id) d.selectingIn.clear();
    draw::rect(r, box, kBox);
    draw::outline(r, box, focused ? kAccent : kEdge);
    const std::string& text = focused ? d.buffer : value;
    const bool hinted = !focused && value.empty() && !hint.empty();
    const float shift = focused ? shiftOf(d.buffer, d.caret) : 0.0f; // (the caret stays in sight)
    const float x0 = box.x + pad - shift;
    TextBlock& whole = block();
    whole.set(font(size), hinted ? std::string(hint) : text.empty() ? std::string(" ") : text);
    const float ty = box.y + (box.h - whole.size().y) / 2;
    const Clip clipped(*this, box);
    if (focused && d.anchor != d.caret) { // what's chosen
        const float a = prefix(d.buffer, std::min(d.anchor, d.caret)), b = prefix(d.buffer, std::max(d.anchor, d.caret));
        draw::rect(r, {x0 + a, box.y + 5 * k, b - a, box.h - 10 * k}, kChosen);
    }
    if (hinted) {
        whole.draw(r, x0, ty, theme::dim);
    } else { // (what's in braces dimmer, and words spelt wrong underlined)
        float x = x0;
        for (std::size_t i = 0; i < text.size();) {
            const bool tag = text[i] == '{';
            std::size_t j = tag ? text.find('}', i) : text.find('{', i);
            j = j == npos ? text.size() : tag ? j + 1 : j;
            const std::string_view run = std::string_view(text).substr(i, j - i);
            TextBlock& b = block();
            b.set(font(size), run);
            b.draw(r, x, ty, tag ? theme::dim : theme::text);
            x += b.size().x;
            i = j;
        }
        if (spelling)
            for (const auto& [from, to] : misspelled(d.spelling, text, spellingLanguage(engine_.config().language), names())) {
                const float a = prefix(text, from), b = prefix(text, to);
                const float step = 4 * k;
                for (int n = 0; static_cast<float>(n) * step < b - a; ++n) { // (a wavy line)
                    const float u = x0 + a + static_cast<float>(n) * step;
                    draw::rect(r, {u, ty + whole.size().y - 1 * k + (n % 2 == 0 ? 0.0f : 1.5f * k), std::min(step, x0 + b - u), 1.5f * k}, kBad);
                }
            }
    }
    if (focused && std::fmod(engine_.toolsTime(), 1.0f) < 0.6f) draw::rect(r, {x0 + prefix(d.buffer, d.caret), box.y + 6 * k, 2 * k, box.h - 12 * k}, kAccent);
    return changed;
}

std::optional<std::string> DeveloperUi::takePicked(const std::string& id) {
    if (!d.picked || d.picked->first != id) return std::nullopt;
    std::string value = std::move(d.picked->second);
    d.picked.reset();
    return value;
}

void DeveloperUi::openPopup(const std::string& id, const StoryEditor::Field& f, const SDL_FRect& box) {
    d.popup = {.id = id, .field = f, .at = box, .box = {}, .typed = false, .shown = true, .scroll = 0.0f}; // (a gradient's wheel: on its top first)
}

std::optional<std::string> DeveloperUi::listButton(const std::string& id, const SDL_FRect& box, std::string_view label, const StoryEditor::Field& given) {
    StoryEditor::Field f = given; // (as its kind has it)
    applyBox(f);
    if (f.suggest.empty() && !f.choices.empty()) f.suggest = listOfChoices(f); // (a kind with buttons elsewhere: a list here, saying the same)
    const std::optional<std::string> picked = takePicked(id);
    if (picked && d.popup.id == id) d.popup.field.value = *picked; // (still open, as a wheel stays: the code above it is the colour now)
    const bool open = d.popup.id == id;
    if (button(box, label, open, true, 13)) {
        if (open) d.popup.id.clear();
        else openPopup(id, f, box);
    }
    if (d.popup.id == id) {
        d.popup.at = box;
        d.popup.shown = true;
    }
    return picked;
}

float DeveloperUi::swatch(float x, float middle, SDL_Color color, bool dim, std::optional<SDL_Color> bottom) const {
    const float side = layout::swatch * k;
    const SDL_FRect box{x, std::round(middle - side / 2), side, side};
    if (dim) color.a = static_cast<Uint8>(color.a / 2);
    if (bottom) { // (a gradient: fading down in the square too)
        if (dim) bottom->a = static_cast<Uint8>(bottom->a / 2);
        draw::gradient(r, box, color, *bottom);
    } else {
        draw::rect(r, box, color);
    }
    draw::outline(r, box, kEdge);
    return x + side + 8 * k;
}

std::optional<std::string> DeveloperUi::colorField(const std::string& id, const SDL_FRect& box, const StoryEditor::Field& f, float size) {
    std::optional<std::string> changed = takePicked(id);
    auto open = [&] { openPopup(id, f, box); };
    if (d.openList == id) {
        d.openList.clear();
        if (d.popup.id != id) open();
    }
    if (click(box)) {
        if (d.popup.id == id) d.popup.id.clear();
        else open();
    }
    const bool shown = d.popup.id == id;
    const std::string value = changed.value_or(f.value);
    draw::rect(r, box, kBox);
    draw::outline(r, box, shown ? kAccent : over(box) ? kButtonHover : kEdge);
    const SDL_FRect arrow{box.x + box.w - box.h, box.y, box.h, box.h};
    // The colour as a square (its default's, dimmed, while the box is empty) and its name: its
    // own, or for a code what it's called or most like (never the code). A picture's name or
    // other words: as they are.
    const std::string_view text = value.empty() ? f.hint : std::string_view(value);
    std::string said(text);
    float tx = box.x + 8 * k;
    if (const auto color = text.empty() ? std::nullopt : parseColor(text, &engine_.palette())) {
        const auto grad = parseGradient(text, &engine_.palette());
        said = colorWords(text, &engine_.palette()); // (its name, a code's likeness, "navy down to black")
        tx = swatch(tx, box.y + box.h / 2, *color, value.empty(), grad ? std::optional(grad->second) : std::nullopt); // (dimmed: its default's)
    }
    textAround(said, tx, box.y + box.h / 2, size, value.empty() ? theme::dim : theme::text, std::max(0.0f, arrow.x - tx - 4 * k));
    draw::rect(r, arrow, shown ? kChosen : over(arrow) ? kButtonHover : kButton);
    draw::outline(r, arrow, kEdge);
    triangle(arrow.x + arrow.w / 2, arrow.y + arrow.h / 2, 5 * k, shown ? Pointing::Up : Pointing::Down, theme::text);
    if (shown) {
        d.popup.field = f;
        d.popup.field.value = value;
        d.popup.at = box;
        d.popup.shown = true;
    }
    return changed;
}

std::optional<std::string> DeveloperUi::pickField(const std::string& id, const SDL_FRect& box, const StoryEditor::Field& given, float size) {
    StoryEditor::Field f = given; // (as its kind has it)
    applyBox(f);
    if (StoryEditor::kindOf(f.box).wheel) return colorField(id, box, f, size); // (a colour's: a square, and the wheel when it opens)
    if (f.pick == StoryEditor::Pick::None && justNumbers(f.suggest)) return field(id, box, f.value, size, f.hint, f.rich); // (no list: what empty means, in grey)
    std::optional<std::string> changed = takePicked(id); // (picked from its list last frame)
    if (changed && d.focus == id) {
        d.buffer = *changed;
        d.caret = d.anchor = d.buffer.size();
    }
    auto open = [&] { openPopup(id, f, box); };
    if (d.openList == id) { // (a new line's box: focused by the editor, its list open as if clicked into)
        d.openList.clear();
        if (d.popup.id != id) open();
    }
    const SDL_FRect arrow{box.x + box.w - box.h, box.y, box.h, box.h};
    if (click(arrow)) {
        if (d.popup.id == id) d.popup.id.clear();
        else open();
    }
    const bool focused = d.focus == id;
    if (const auto typed = field(id, {box.x, box.y, box.w - box.h + 1, box.h}, changed.value_or(f.value), size, f.hint)) {
        changed = typed;
        if (d.popup.id != id) open();
        d.popup.typed = true;
    }
    if (!focused && d.focus == id && d.popup.id != id) open();             // (clicked into)
    if (focused && d.focus != id && d.popup.id == id) d.popup.id.clear(); // (Enter, Tab or Esc)
    const bool shown = d.popup.id == id;
    draw::rect(r, arrow, shown ? kChosen : over(arrow) ? kButtonHover : kButton);
    draw::outline(r, arrow, kEdge);
    triangle(arrow.x + arrow.w / 2, arrow.y + arrow.h / 2, 5 * k, shown ? Pointing::Up : Pointing::Down, theme::text);
    if (shown) {
        d.popup.field = f;
        d.popup.field.value = d.focus == id ? d.buffer : changed.value_or(f.value);
        d.popup.at = box;
        d.popup.shown = true;
    }
    return changed;
}

void DeveloperUi::scrollForm(const SDL_FRect& box, float step) {
    d.formScroll = std::clamp(d.formScroll - wheelOver(box) * step, 0.0f, std::max(0.0f, d.formHeight - box.h));
}

void DeveloperUi::formDrawn(const SDL_FRect& box, float y) {
    d.formHeight = y + d.formScroll - box.y;
    d.formScroll = std::clamp(d.formScroll, 0.0f, std::max(0.0f, d.formHeight - box.h));
}

std::vector<std::string> DeveloperUi::names() const {
    std::vector<std::string> out;
    for (const auto& c : d.editor.characters())
        for (auto&& w : std::views::split(std::string_view(c.name), ' ')) out.push_back(lower(std::string_view(w.begin(), w.end())));
    return out;
}

void DeveloperUi::triangle(float x, float y, float size, Pointing to, SDL_Color color) const {
    const SDL_FColor c{static_cast<float>(color.r) / 255.0f, static_cast<float>(color.g) / 255.0f, static_cast<float>(color.b) / 255.0f, 1.0f};
    const float tip = to == Pointing::Up ? -size / 2 : size / 2;
    const bool right = to == Pointing::Right;
    const SDL_Vertex v[3]{{.position = right ? SDL_FPoint{x - tip, y - size} : SDL_FPoint{x - size, y - tip}, .color = c, .tex_coord = {}},
                          {.position = right ? SDL_FPoint{x - tip, y + size} : SDL_FPoint{x + size, y - tip}, .color = c, .tex_coord = {}},
                          {.position = right ? SDL_FPoint{x + tip, y} : SDL_FPoint{x, y + tip}, .color = c, .tex_coord = {}}};
    SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
}

} // namespace vn
