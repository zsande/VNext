// The developer tools' widgets (DeveloperUi.hpp): text, buttons, lists, boxes to type into, choices,
// colours.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

#include <ranges>

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

std::size_t nextLetter(const std::string& s, std::size_t i) {
    if (i >= s.size()) return s.size();
    do ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}

} // namespace

TextBlock& DeveloperUi::block(const Font& font, std::string_view s, float wrap) {
    d.textKey.clear();
    std::format_to(std::back_inserter(d.textKey), "{}|{}|{}", static_cast<const void*>(&font), wrap, s);
    auto kept = d.texts.find(d.textKey);
    if (kept == d.texts.end()) {
        kept = d.texts.try_emplace(d.textKey).first;
        kept->second.block.set(font, s, wrap);
    }
    kept->second.drawn = d.textFrame;
    return kept->second.block;
}

SDL_FPoint DeveloperUi::text(std::string_view s, float x, float y, float size, SDL_Color colour, float wrap) {
    TextBlock& b = block(font(size), s, wrap);
    b.draw(r, x, y, colour);
    return b.size();
}

float DeveloperUi::width(std::string_view s, float size) {
    return block(font(size), s).size().x;
}

SDL_FPoint DeveloperUi::measure(std::string_view s, float size, float wrap) {
    return block(font(size), s, wrap).size();
}

std::string DeveloperUi::fitted(std::string_view s, float size, float room) {
    return shortened(s, [&](std::string_view t) { return width(t, size) <= room; });
}

void DeveloperUi::tip(const SDL_FRect& area, std::string_view whole) {
    if (!over(area)) return;
    if (!clips_.empty()) { // (only where it shows)
        const SDL_Rect& c = clips_.back();
        if (!pointInRect(d.mouseX, d.mouseY, {static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.w), static_cast<float>(c.h)})) return;
    }
    d.tip = whole;
}

float DeveloperUi::textFitted(std::string_view s, float x, float middle, float size, SDL_Color colour, float room) {
    const std::string shown = fitted(s, size, room);
    textAround(shown, x, middle, size, colour);
    const SDL_FPoint at = measure(shown, size);
    if (shown != s) tip({x, middle - at.y / 2, at.x, at.y}, s);
    return at.x;
}

void DeveloperUi::heading(std::string_view title, float x, float y, float room) {
    textFitted(title, x, y + layout::headingH * k / 2, layout::headingText, theme::dim, room);
}

DeveloperUi::Grouped DeveloperUi::grouped(std::span<const ButtonGroup> groups, float w) {
    Grouped out;
    const float h = layout::barButtonH * k, gap = layout::barButtonGap * k, groupGap = layout::gap * k;
    float nameW = 0.0f;
    for (const ButtonGroup& g : groups) nameW = std::max(nameW, width(g.name, layout::labelText));
    if (nameW > 0.0f) nameW += 2 * layout::gap * k;
    const float room = std::max(1.0f, w - nameW);
    float y = 0.0f;
    for (const ButtonGroup& g : groups) {
        const std::size_t n = g.labels.size();
        out.names.push_back({0.0f, y, nameW, n > 0 ? h : 0.0f});
        if (n == 0) continue;
        // One row: an even grid when every label fits its cell, else each as wide as its label, sharing out
        // the room (narrower, when they must, to fit in it).
        std::vector<float> natural(n);
        float together = 0.0f;
        for (std::size_t i = 0; i < n; ++i) // (one as long as a part's title at most half the room: its button cuts it)
            together += natural[i] = std::min(room / 2, width(g.labels[i], layout::smallButtonText) + layout::choicePad * k);
        const float buttons = std::max(1.0f, room - gap * static_cast<float>(n - 1)), cell = buttons / static_cast<float>(n);
        const bool grid = std::ranges::all_of(natural, [&](float v) { return v <= cell; });
        float x = nameW;
        for (std::size_t i = 0; i < n; ++i) {
            const float bw = grid ? cell : natural[i] * buttons / together;
            out.buttons.push_back({std::round(x), y, std::round(x + bw) - std::round(x), h}); // (whole pixels: crisp edges)
            x += bw + gap;
        }
        y += h + groupGap;
    }
    out.h = std::max(0.0f, y - groupGap);
    return out;
}

std::optional<std::size_t> DeveloperUi::groupedButtons(std::span<const ButtonGroup> groups, const Grouped& at, float x, float y,
                                                       std::span<const std::size_t> chosen, bool enabled) {
    std::optional<std::size_t> clicked;
    std::size_t i = 0;
    for (std::size_t g = 0; g < groups.size() && g < at.names.size(); ++g) {
        const SDL_FRect& name = at.names[g];
        if (name.h > 0.0f) textAround(groups[g].name, x + name.x, y + name.y + name.h / 2, layout::labelText, theme::dim);
        for (const std::string_view label : groups[g].labels) {
            if (i >= at.buttons.size()) break;
            const SDL_FRect& b = at.buttons[i];
            if (button({x + b.x, y + b.y, b.w, b.h}, label, std::ranges::contains(chosen, i), enabled, layout::smallButtonText)) clicked = i;
            ++i;
        }
    }
    return clicked;
}

float DeveloperUi::newButtonsH(std::span<const ButtonGroup> groups, float w) { return layout::headingH * k + grouped(groups, w).h; }

std::optional<std::size_t> DeveloperUi::newButtons(std::string_view title, std::span<const ButtonGroup> groups, const SDL_FRect& box,
                                                   std::span<const std::size_t> chosen, bool enabled) {
    heading(title, box.x, box.y, box.w);
    return groupedButtons(groups, grouped(groups, box.w), box.x, box.y + layout::headingH * k, chosen, enabled);
}

ListPart DeveloperUi::listPart(std::string_view title, const SDL_FRect& box, std::size_t rows) {
    const float headingH = layout::headingH * k, buttonH = layout::buttonH * k, gap = layout::gap * k;
    heading(title, box.x, box.y, box.w);
    ListPart at;
    at.list = {box.x, box.y + headingH, box.w, std::max(0.0f, box.h - headingH - static_cast<float>(rows) * (gap + buttonH))};
    for (float y = at.list.y + at.list.h; at.rows.size() < rows; y += gap + buttonH) at.rows.push_back({box.x, y + gap, box.w, buttonH});
    return at;
}

DeveloperUi::Columns DeveloperUi::columns(const SDL_FRect& area) const {
    const float leftW = std::round(std::clamp(area.w * layout::sidePart, layout::sideLeast * k, layout::sideMost * k));
    const float formW = std::round(std::clamp(area.w * layout::formPart, layout::formLeast * k, layout::formMost * k));
    const SDL_FRect left{area.x, area.y, leftW, area.h};
    const SDL_FRect middle{left.x + leftW, area.y, std::max(0.0f, area.w - leftW - formW), area.h};
    return {left, middle, {middle.x + middle.w, area.y, formW, area.h}};
}

void DeveloperUi::formTitle(std::string_view title, float x, float y, SDL_Color colour, float room) {
    textFitted(title, x, y + layout::buttonH * k / 2, layout::titleText, colour, room);
}

void DeveloperUi::textIn(std::string_view s, const SDL_FRect& box, float size, SDL_Color colour) {
    TextBlock& b = block(font(size), s);
    const Clip clipped(*this, box);
    b.drawIn(r, box, 0.5f, 0.5f, colour);
}

void DeveloperUi::richIn(const RichText& text, const SDL_FRect& box, float size, SDL_Color colour, std::size_t reveal) {
    const Font& f = font(size);
    d.textKey.clear(); // (kept as plain texts are, by what it is: its words and their look)
    std::format_to(std::back_inserter(d.textKey), "{}|rich|{}", static_cast<const void*>(&f), text.text);
    for (const TextSpan& span : text.spans)
        std::format_to(std::back_inserter(d.textKey), "|{},{},{},{},{}", span.begin, span.end, static_cast<unsigned>(span.style),
                       span.colour ? (span.colour->r << 24 | span.colour->g << 16 | span.colour->b << 8 | span.colour->a) : -1, span.ruby);
    auto kept = d.texts.find(d.textKey);
    if (kept == d.texts.end()) {
        kept = d.texts.try_emplace(d.textKey).first;
        kept->second.block.set(f, text.text);
        kept->second.block.setSpans(text.spans);
    }
    kept->second.drawn = d.textFrame;
    const Clip clipped(*this, box);
    kept->second.block.drawIn(r, box, 0.5f, 0.5f, colour, reveal);
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
    const std::string shown = fitted(label, size, box.w - 2 * k); // (cut when it doesn't fit inside it: its whole in a tip)
    textIn(shown, box, size, enabled ? theme::text : theme::dim);
    if (shown != label) tip(box, label);
    return click(box) && enabled;
}

std::optional<std::size_t> DeveloperUi::list(const SDL_FRect& box, float& scroll, std::size_t count, const std::function<std::string(std::size_t)>& label,
                                             const std::function<bool(std::size_t)>& chosen, const std::function<bool(std::size_t)>& wrong,
                                             const std::function<bool(std::size_t)>& faint) {
    const float itemH = layout::listItemH * k, total = static_cast<float>(count) * itemH;
    draw::rect(r, box, kBox);
    scroll = std::clamp(scroll - wheelOver(box) * itemH * 2, 0.0f, std::max(0.0f, total - box.h));
    scrollbar(&scroll, barInside(box), box.h, total, scroll);
    const float w = box.w - (total > box.h ? layout::scrollbarRoom * k : 0.0f); // (its rows: beside its scrollbar)
    std::optional<std::size_t> clicked;
    const Clip clipped(*this, box);
    for (std::size_t i = 0; i < count; ++i) {
        const SDL_FRect item{box.x, box.y + static_cast<float>(i) * itemH - scroll, w, itemH - 2 * k};
        if (item.y + item.h < box.y || item.y > box.y + box.h) continue;
        if (chosen(i) || over(item)) draw::rect(r, item, chosen(i) ? kChosen : kButtonHover);
        if (wrong(i)) draw::rect(r, {item.x, item.y, layout::markW * k, item.h}, kBad);
        textFitted(label(i), item.x + layout::listPad * k, item.y + item.h / 2, layout::listText, faint && faint(i) ? theme::dim : theme::text, item.w - 2 * layout::listPad * k);
        if (over(box) && click(item)) clicked = i;
    }
    if (count == 0) textAround("Empty"_tr, box.x + layout::listPad * k, box.y + itemH / 2, layout::listText, theme::dim); // (as editList's)
    return clicked;
}

DeveloperUi::ListEvent DeveloperUi::editList(const std::string& id, const SDL_FRect& box, float& scroll, const ListItems& items) {
    using Event = ListEvent::Kind;
    const float itemH = items.itemH * k, total = static_cast<float>(items.count) * itemH;
    ListEvent event;
    draw::rect(r, box, kBox);
    scroll = std::clamp(scroll - wheelOver(box) * itemH * 2, 0.0f, std::max(0.0f, total - box.h));
    scrollbar(&scroll, barInside(box), box.h, total, scroll);
    const float w = box.w - (total > box.h ? layout::scrollbarRoom * k : 0.0f); // (its rows: beside its scrollbar)
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
        const SDL_FRect item{box.x + in, box.y + static_cast<float>(i) * itemH - scroll, w - in, itemH - 2 * k};
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
            if (auto name = field(renameId, item, items.name ? items.name(i) : items.label(i), layout::listText)) event = {.kind = Event::Renamed, .item = i, .to = 0, .name = std::move(*name)};
        } else {
            const SDL_FRect content{item.x, item.y, item.w - (deletes ? xw + 6 * k : 0.0f), item.h};
            const Clip clippedItem(*this, content);
            if (items.paint) items.paint(i, content);
            else textFitted(items.label(i), item.x + layout::listPad * k, item.y + item.h / 2, layout::listText, items.faint && items.faint(i) ? theme::dim : theme::text,
                            content.w - 2 * layout::listPad * k);
        }
        if (deletes) {
            draw::rect(r, cross, armed ? SDL_Color{150, 50, 64, 255} : over(cross) ? kButtonHover : kButton);
            draw::outline(r, cross, armed ? kBad : kEdge);
            textIn(armed ? std::string_view("Delete"_tr) : std::string_view("x"), cross, layout::smallButtonText, theme::text);
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
        draw::rect(r, {box.x + in, box.y + static_cast<float>(*slot) * itemH - scroll - 2 * k, w - in, 3 * k}, kAccent);
    }
    if (items.count == 0) textAround("Empty"_tr, box.x + layout::listPad * k, box.y + itemH / 2, layout::listText, theme::dim);
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
    for (std::size_t i = 0; i < n; ++i) widths[i] = std::min(w, width(tr(labels[i]), size) + pad * k); // (as shown: snug, like a form's choices, more to a row)
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

std::vector<std::pair<std::string_view, std::string>> DeveloperUi::choiceLabels(const StoryEditor::Field& f) {
    std::vector<std::string_view> words, labels;
    for (auto&& w : std::views::split(std::string_view(f.choices), ' ')) words.emplace_back(w.begin(), w.end());
    for (auto&& l : std::views::split(std::string_view(f.labels), '|')) labels.emplace_back(l.begin(), l.end());
    if (words.size() == 2 && words[0] == "no" && words[1] == "yes") { // (Yes, then No, everywhere)
        std::ranges::reverse(words);
        if (labels.size() == 2) std::ranges::reverse(labels);
    }
    std::vector<std::pair<std::string_view, std::string>> out;
    for (std::size_t i = 0; i < words.size(); ++i)
        out.emplace_back(words[i], std::string(tr(upperFirst(spaced(i < labels.size() && !f.labels.empty() ? labels[i] : words[i])))));
    return out;
}

DeveloperUi::ChoiceButtons DeveloperUi::choiceButtons(const SDL_FRect& box, const StoryEditor::Field& f) {
    ChoiceButtons out;
    auto& [words, shown, places] = out;
    for (auto& [word, label] : choiceLabels(f)) {
        words.push_back(word);
        shown.push_back(std::move(label));
    }
    const float gap = layout::gap * k;
    const auto n = words.size();
    if (n == 0) return out;
    std::vector<float> widths; // (as wide as their labels)
    float widest = 0.0f, together = gap * static_cast<float>(n - 1);
    for (const std::string& s : shown) {
        widths.push_back(width(s, layout::buttonText) + layout::choicePad * k);
        widest = std::max(widest, widths.back());
        together += widths.back();
    }
    // Their places: all alike when they fit so; else as wide as their labels, sharing out the room left;
    // else none (a list).
    if (static_cast<float>(n) * (widest + gap) - gap <= box.w) {
        const float cw = (box.w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (std::size_t i = 0; i < n; ++i) places.push_back({box.x + static_cast<float>(i) * (cw + gap), box.y, cw, box.h});
    } else if (together <= box.w) {
        const float extra = (box.w - together) / static_cast<float>(n);
        float at = box.x;
        for (std::size_t i = 0; i < n; ++i) {
            places.push_back({at, box.y, widths[i] + extra, box.h});
            at += widths[i] + extra + gap;
        }
    }
    return out;
}

std::optional<std::string> DeveloperUi::choices(const std::string& id, const SDL_FRect& box, const StoryEditor::Field& f) {
    const auto [words, shown, places] = choiceButtons(box, f);
    const auto n = words.size();
    std::vector<std::string_view> on;
    for (auto&& w : std::views::split(std::string_view(f.value), ' '))
        if (!std::string_view(w.begin(), w.end()).empty()) on.emplace_back(w.begin(), w.end());
    auto chosen = [&](std::string_view word) { return f.multi ? std::ranges::contains(on, word) : f.value == word; };
    auto toggled = [&](std::string_view word) { // (several: that one on or off; "none" goes on its own)
        std::string now;
        for (const std::string_view w : words) {
            const bool keep = w == word ? !chosen(word) : std::ranges::contains(on, w) && word != "none" && w != "none";
            if (keep) now += (now.empty() ? "" : " ") + std::string(w);
        }
        return now;
    };
    if (places.empty()) { // (they don't fit one row: one box saying what's chosen, its list under it on a click)
        std::optional<std::string> picked = takePicked(id);
        if (picked && f.multi) { // (the list turns one on or off: as a button would)
            const auto now = wordsOf(*picked);
            const auto it = std::ranges::find_if(words, [&](std::string_view w) { return std::ranges::contains(now, w) != chosen(w); });
            picked = it != words.end() ? toggled(*it) : f.value;
        }
        if (picked && d.popup.id == id) d.popup.field.value = *picked; // (several: the list stays open)
        if (click(box)) {
            if (d.popup.id == id) d.popup.id.clear();
            else openPopup(id, f, box);
        }
        const bool open = d.popup.id == id;
        std::string said;
        for (std::size_t i = 0; i < n; ++i)
            if (chosen(words[i])) said += (said.empty() ? "" : ", ") + shown[i];
        const bool none = said.empty(), usual = !f.multi && !f.usual.empty() && f.value == f.usual;
        if (none) said = std::string(tr(f.hint)); // (what nothing chosen means, in grey; and the usual one, as it's what none written means)
        const SDL_FRect arrow{box.x + box.w - box.h, box.y, box.h, box.h};
        draw::rect(r, box, kBox);
        draw::outline(r, box, open ? kAccent : over(box) ? kButtonHover : kEdge);
        textFitted(said, box.x + 8 * k, box.y + box.h / 2, layout::boxText, none || usual ? theme::dim : theme::text, std::max(0.0f, arrow.x - box.x - 12 * k));
        draw::rect(r, arrow, open ? kChosen : over(arrow) ? kButtonHover : kButton);
        draw::outline(r, arrow, kEdge);
        triangle(arrow.x + arrow.w / 2, arrow.y + arrow.h / 2, 5 * k, open ? Pointing::Up : Pointing::Down, theme::text);
        if (open) {
            d.popup.at = box;
            d.popup.shown = true;
        }
        return picked;
    }
    std::optional<std::string> clicked;
    for (std::size_t i = 0; i < n; ++i) {
        if (!button(places[i], shown[i], chosen(words[i]), true, layout::buttonText)) continue;
        if (f.multi) clicked = toggled(words[i]);
        else if (!chosen(words[i])) clicked = std::string(words[i]);
    }
    return clicked;
}

void DeveloperUi::textAround(std::string_view s, float x, float middle, float size, SDL_Color colour, float wrap) {
    TextBlock& b = block(font(size), s, wrap);
    b.draw(r, x, middle - b.size().y / 2, colour);
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
            const bool shift = key.mod & SDL_KMOD_SHIFT;
            auto moveTo = [&](std::size_t to) { // (Shift: choosing as it goes)
                d.caret = to;
                if (!shift) d.anchor = to;
            };
            // (the tools' select all, paste, copy and cut keys, as their controls have them; the rest are a box's own)
            if (d.isKey(key, ToolKey::SelectAll)) {
                d.anchor = 0;
                d.caret = d.buffer.size();
                return true;
            }
            if (d.isKey(key, ToolKey::Paste)) {
                if (const SdlOwned<char> pasted{SDL_GetClipboardText()}) {
                    std::string text = pasted.get();
                    std::erase(text, '\r');
                    std::ranges::replace(text, '\n', ' ');
                    put(text);
                }
                return true;
            }
            if (const bool cut = d.isKey(key, ToolKey::Cut); cut || d.isKey(key, ToolKey::Copy)) {
                const auto [from, to] = chosen();
                SDL_SetClipboardText((from == to ? d.buffer : d.buffer.substr(from, to - from)).c_str()); // (nothing chosen: all of it)
                if (cut) {
                    if (from == to) d.anchor = 0, d.caret = d.buffer.size();
                    eraseChosen();
                    changed = d.buffer;
                }
                return true;
            }
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
    TextBlock& whole = block(font(size), hinted ? std::string(tr(hint)) : text.empty() ? std::string(" ") : text);
    const float ty = box.y + (box.h - whole.size().y) / 2;
    const Clip clipped(*this, box);
    if (focused && d.anchor != d.caret) { // what's chosen
        const float a = prefix(d.buffer, std::min(d.anchor, d.caret)), b = prefix(d.buffer, std::max(d.anchor, d.caret));
        draw::rect(r, {x0 + a, box.y + 5 * k, b - a, box.h - 10 * k}, kChosen);
    }
    if (hinted) { // (what empty is, cut to fit as its value would be: its whole in a tip)
        textFitted(tr(hint), x0, ty + whole.size().y / 2, size, theme::dim, box.w - 2 * pad);
    } else { // (what's in braces dimmer, and words spelt wrong underlined; not typed into, cut to fit, its whole in a tip)
        const std::string shown = focused ? text : fitted(text, size, box.w - 2 * pad);
        const std::size_t kept = shown == text ? text.size() : shown.size() - 3; // (of the text: before the dots)
        if (kept < text.size()) tip(box, text);
        float x = x0;
        for (std::size_t i = 0; i < shown.size();) {
            const bool tag = shown[i] == '{';
            std::size_t j = tag ? shown.find('}', i) : shown.find('{', i);
            j = j == npos ? shown.size() : tag ? j + 1 : j;
            const std::string_view run = std::string_view(shown).substr(i, j - i);
            TextBlock& b = block(font(size), run);
            b.draw(r, x, ty, tag ? theme::dim : theme::text);
            x += b.size().x;
            i = j;
        }
        if (spelling)
            for (auto [from, to] : misspelled(d.spelling, text, spellingLanguage(engine_.gameSettings().language), names())) {
                if (from >= kept) continue;
                to = std::min(to, kept);
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
    StoryEditor::Field f = given; // (as its kind has it: a kind with buttons elsewhere lists them, saying the same)
    applyBox(f);
    const std::optional<std::string> picked = takePicked(id);
    if (picked && d.popup.id == id) d.popup.field.value = *picked; // (still open, as a wheel stays: the code above it is the colour now)
    const bool open = d.popup.id == id;
    if (button(box, label, open, true, layout::smallButtonText)) {
        if (open) d.popup.id.clear();
        else openPopup(id, f, box);
    }
    if (d.popup.id == id) {
        d.popup.at = box;
        d.popup.shown = true;
    }
    return picked;
}

float DeveloperUi::swatch(float x, float middle, SDL_Color colour, bool dim, std::optional<SDL_Color> bottom, float size) const {
    const float side = size * k;
    const SDL_FRect box{x, std::round(middle - side / 2), side, side};
    if (dim) colour.a = static_cast<Uint8>(colour.a / 2);
    if (bottom) { // (a gradient: fading down in the square too)
        if (dim) bottom->a = static_cast<Uint8>(bottom->a / 2);
        draw::gradient(r, box, colour, *bottom);
    } else {
        draw::rect(r, box, colour);
    }
    draw::outline(r, box, kEdge);
    return x + side + 8 * k;
}

std::optional<std::string> DeveloperUi::colourField(const std::string& id, const SDL_FRect& box, const StoryEditor::Field& f, float size) {
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
    // own, or for a code what it's called or most like (never the code). An image's name or
    // other words: as they are.
    const std::string_view text = value.empty() ? f.hint : std::string_view(value);
    std::string said(value.empty() ? tr(text) : text);
    float tx = box.x + 8 * k;
    const Palette* palette = &engine_.palette();
    Palette others; // (a colour's own form, its name in `context`: the others)
    if (const auto own = palette->find(f.context); !f.context.empty() && own != palette->end()) {
        others = *palette;
        others.erase(own->first);
        palette = &others;
    }
    if (const auto colour = text.empty() ? std::nullopt : parseColour(text, palette)) {
        const auto grad = parseGradient(text, palette);
        said = std::string(tr(colourWords(text, palette))); // (its name, a code's likeness, "navy down to black"; None in the tools' language)
        tx = swatch(tx, box.y + box.h / 2, *colour, value.empty(), grad ? std::optional(grad->second) : std::nullopt); // (dimmed: its default's)
    }
    textFitted(said, tx, box.y + box.h / 2, size, value.empty() ? theme::dim : theme::text, std::max(0.0f, arrow.x - tx - 4 * k));
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
    if (StoryEditor::kindOf(f.box).wheel) return colourField(id, box, f, size); // (a colour's: a square, and the wheel when it opens)
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
    const SDL_FRect bar = barBeside(box);
    d.formScroll = std::clamp(d.formScroll - wheelOver({box.x, box.y, bar.x + bar.w - box.x, box.h}) * step, 0.0f, std::max(0.0f, d.formHeight - box.h));
    scrollbar(&d.formScroll, bar, box.h, d.formHeight, d.formScroll);
}

void DeveloperUi::scrollbar(const void* of, const SDL_FRect& track, float view, float total, float& scroll) {
    const bool caught = d.barDrag && d.barDrag->of == of;
    if (total <= view || track.h <= 0.0f) {
        if (caught) d.barDrag.reset();
        return;
    }
    const float most = total - view, thumbH = std::min(track.h, std::max(layout::scrollbarLeast * k, track.h * view / total)), room = track.h - thumbH;
    const auto thumbAt = [&] { return SDL_FRect{track.x, track.y + room * std::clamp(scroll / most, 0.0f, 1.0f), track.w, thumbH}; };
    SDL_FRect thumb = thumbAt();
    const SDL_FRect grip{track.x - 3 * k, track.y, track.w + 6 * k, track.h}; // (wider than it looks: easier to catch)
    if (click(grip)) // (on the thumb: dragged from where it was caught; else it comes to the pointer, by its middle)
        d.barDrag = DeveloperTools::BarDrag{.of = of, .grab = d.mouseY >= thumb.y && d.mouseY < thumb.y + thumb.h ? d.mouseY - thumb.y : thumbH / 2};
    if (d.barDrag && d.barDrag->of == of) {
        if (!d.held) d.barDrag.reset();
        else if (room > 0.0f) scroll = std::clamp((d.mouseY - d.barDrag->grab - track.y) / room, 0.0f, 1.0f) * most;
        thumb = thumbAt();
    }
    draw::rect(r, track, kBar);
    draw::rect(r, thumb, (d.barDrag && d.barDrag->of == of) || over(grip) ? kAccent : kEdge);
}

SDL_FRect DeveloperUi::barInside(const SDL_FRect& box) const {
    return {box.x + box.w - (layout::scrollbarRoom + layout::scrollbarW) / 2 * k, box.y + 3 * k, layout::scrollbarW * k, std::max(0.0f, box.h - 6 * k)};
}

SDL_FRect DeveloperUi::barBeside(const SDL_FRect& box) const {
    return {box.x + box.w + (layout::formBarRoom - layout::scrollbarW) / 2 * k, box.y, layout::scrollbarW * k, box.h};
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

void DeveloperUi::triangle(float x, float y, float size, Pointing to, SDL_Color colour) const {
    const SDL_FColor c{static_cast<float>(colour.r) / 255.0f, static_cast<float>(colour.g) / 255.0f, static_cast<float>(colour.b) / 255.0f, 1.0f};
    const float tip = to == Pointing::Up ? -size / 2 : size / 2;
    const bool right = to == Pointing::Right;
    const SDL_Vertex v[3]{{.position = right ? SDL_FPoint{x - tip, y - size} : SDL_FPoint{x - size, y - tip}, .color = c, .tex_coord = {}},
                          {.position = right ? SDL_FPoint{x - tip, y + size} : SDL_FPoint{x + size, y - tip}, .color = c, .tex_coord = {}},
                          {.position = right ? SDL_FPoint{x + tip, y} : SDL_FPoint{x, y + tip}, .color = c, .tex_coord = {}}};
    SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
}

bool PreviewHead::button(std::string_view label, bool chosen, bool enabled) {
    const float k = ui_.k;
    const float w = ui_.buttonWidth(label, layout::smallButtonText, layout::previewButtonW);
    right_ -= w;
    const float h = layout::smallButtonH * k;
    const bool clicked = ui_.button({right_, row_.y + (row_.h - h) / 2, w, h}, label, chosen, enabled, layout::smallButtonText); // (in the middle of the row)
    right_ -= layout::gap * k;
    return clicked;
}

void PreviewHead::title(std::string_view text) { // (as every part's heading, in the room the buttons leave)
    const float k = ui_.k;
    ui_.heading(text, row_.x, row_.y, right_ - row_.x - layout::gap * k);
}

} // namespace vn
