// What opens over a page: a box's list (or colour wheel), and tips.
#include "DeveloperUi.hpp"

#include <cmath>
#include <numbers>

namespace vn {

namespace {

// A colour from its hue (0 to 360), saturation, value and alpha (0 to 1), and back.
SDL_Color hsvColour(float h, float sat, float v, float a) { // (h in degrees, any way round)
    return draw::hsv(std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f) / 360.0f, sat, v, static_cast<Uint8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f)));
}

void hsvOf(SDL_Color c, float& h, float& sat, float& v) { // (a grey keeps the hue it had)
    const auto [hue, s, value] = vn::hsvOf(c);
    sat = s;
    v = value;
    if (s > 0.0f) h = hue;
}

std::string hexText(SDL_Color c) { return c.a == 255 ? std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b) : std::format("#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a); }

// An entry in a box's list: its value, what the list shows, and whether it's what the box empty means.
struct PopupEntry {
    std::string value, label;
    bool empty = false;
};

// What a box's list holds: the values it often has and the names it lists, each once, as far as they
// match what's `typed`; first, what the box empty means; a colour's, only the words it can be and the
// game's own colours. "Depends on a variable" isn't among them (`byVariable`: it was; a row of its own).
std::vector<PopupEntry> popupEntries(DeveloperTools::Popup& p, const StoryEditor& editor, const StoryEditor::Field& f, const StoryEditor::BoxKind& kind,
                                     const std::string& typed, bool& byVariable) {
    std::vector<PopupEntry> entries;
    std::set<std::string> seen; // (each value once)
    auto add = [&](std::string value, std::string label, bool empty) {
        const std::string low = lower(value);
        if (seen.contains(low)) return;
        if (!typed.empty() && !low.contains(typed) && !lower(label).contains(typed)) return;
        seen.insert(low);
        entries.push_back({std::move(value), std::move(label), empty});
    };
    for (const auto& [word, label] : DeveloperUi::choiceLabels(f)) add(std::string(word), label, false); // (a field's choices: as their buttons say them)
    for (const std::string_view entry : wordsOf(f.suggest, '|')) { // (one that says what it is: "0: a fifth of the box", "Japanese (ja)")
        const auto [value, what] = suggestionParts(entry);
        add(std::string(value), what.empty() ? std::string(value) : what.starts_with('(') ? std::format("{} {}", value, what) : std::format("{}: {}", value, tr(what)), false);
    }
    if (p.namesVersion != editor.version()) { // (found once, not every frame it's open)
        p.names = editor.names(f.pick, f.context);
        p.namesVersion = editor.version();
    }
    for (const std::string& name : p.names) add(name, name, false);
    // First, what the field empty means (unless the list is empty, or has it already): its hint; a box of
    // images without one: None, to empty it for what's usual; a colour's without one: transparent.
    const bool colourOnly = kind.wheel && !kind.images && f.hint.empty();
    const std::string_view emptyMeans = !f.hint.empty() ? f.hint : kind.images ? std::string_view("None"_text) : colourOnly ? std::string_view("Transparent"_text) : std::string_view{};
    const std::string emptyValue = colourOnly ? "#00000000" : "";
    if (!entries.empty() && !emptyMeans.empty() && !f.multi && f.choices.empty() && !p.typed && typed.empty() && // (choices: just them, as their buttons)
        std::ranges::none_of(entries, [&](const PopupEntry& e) { return lower(e.value) == lower(colourOnly ? std::string_view(emptyValue) : emptyMeans); }))
        entries.insert(entries.begin(), PopupEntry{.value = emptyValue, .label = std::string(tr(emptyMeans)), .empty = true}); // (transparent: as the empty one, kept and dim)

    if (f.box == StoryEditor::Box::Colour) { // (under the wheel: the game's own colours, and the words the box can be)
        std::vector<std::string> words;
        for (const std::string_view entry : wordsOf(f.suggest, '|')) words.emplace_back(suggestionParts(entry).first);
        const auto& own = editor.definitions("colour"); // (once, not a scan of the scripts for each entry)
        std::erase_if(entries, [&](const PopupEntry& e) {
            return !e.empty && !std::ranges::contains(words, e.value) && std::ranges::none_of(own, [&](const StoryEditor::Definition& c) { return lower(c.name) == lower(e.value); });
        });
    }
    const auto by = std::ranges::find(entries, std::string(kImageBy), &PopupEntry::value); // ("depends on a variable": a row of its own, over the images)
    byVariable = by != entries.end();
    if (byVariable) entries.erase(by);
    return entries;
}

} // namespace

// A box's list as it's drawn this frame: where, its value (a colour box's gradient, navy-black: its two
// colours as words), and what's picked in it (`keepOpen`: by the wheel, which stays open). changed(): a
// colour picked, as the box's new value (a gradient's: the colour the wheel is on, changed).
struct Developer::PopupFrame {
    DeveloperUi& ui;
    DeveloperTools::Popup& p;
    SDL_FRect box;
    float top;
    std::optional<std::pair<SDL_Color, SDL_Color>> gradient;
    std::string topText, bottomText;
    std::optional<std::string> clicked{};
    bool keepOpen = false;

    void split(const Palette& palette) {
        gradient = parseGradient(p.field.value, &palette);
        if (!gradient) return;
        const auto dash = p.field.value.find('-', 1);
        topText = std::string(trim(std::string_view(p.field.value).substr(0, dash)));
        bottomText = std::string(trim(std::string_view(p.field.value).substr(dash + 1)));
    }
    [[nodiscard]] std::string changed(const std::string& one) const { return !gradient ? one : p.editBottom ? topText + "-" + one : one + "-" + bottomText; }
};

// The list a field has open, under it (or over it, when there's no room under it): what the field
// empty means, the values it often has and the names it lists, as far as they match what's typed into
// it. Images show as small images, colours with a sample (and colours to pick from), and sounds
// with a button that plays them. A click picks one (several, for a field of several: a click turns
// one on or off); the field takes it next frame.
void Developer::drawPopup(DeveloperUi& ui) {
    DeveloperTools& d = tools;
    auto& p = d.popup;
    d.keyStopsOff = true; // (what's in it: its own keys, not stops)
    const struct KeysBack {
        DeveloperTools& d;
        ~KeysBack() { d.keyStopsOff = false; }
    } keysBack{d};
    if (!p.shown) p.id.clear();
    if (p.id.empty()) {
        p.box = {};
        if (d.focus == "popup:colour") d.focus.clear();
        return;
    }
    using Pick = StoryEditor::Pick;
    SDL_Renderer* r = ui.r;
    applyBox(p.field); // (as its kind has it, however it was opened)
    const StoryEditor::Field& f = p.field;
    const float k = ui.k;
    std::vector<std::string> on; // (several: the ones chosen)
    if (f.multi)
        for (const std::string& word : rawWords(f.value)) on.push_back(lower(word));
    std::string typed = p.typed ? lower(trim(f.value)) : std::string{};
    if (f.multi && p.typed) typed = f.value.ends_with(' ') || on.empty() ? std::string{} : on.back(); // (the word being typed)
    const StoryEditor::BoxKind& kind = StoryEditor::kindOf(f.box);
    const bool colours = kind.wheel; // (a wheel over the list: a background's is an image or a colour)
    if (colours && d.focus == "popup:colour" && !trim(d.buffer).empty() && !parseColour(d.buffer, &engine.palette()))
        typed = lower(trim(d.buffer)); // (a name's start typed above the wheel, not a code: what fits it under the wheel)
    bool byVariable = false;
    const std::vector<PopupEntry> entries = popupEntries(p, d.editor, f, kind, typed, byVariable);
    const bool images = kind.images, sounds = kind.listen, people = f.pick == Pick::Characters || f.pick == Pick::Shown;
    const float pad = layout::popupPad * k, itemH = layout::itemH * k, cellW = layout::cellW * k, cellH = layout::cellH * k;
    float w = std::max(p.at.w, (images ? layout::imagesW : layout::listW) * k);
    const float valueH = layout::codeH * k, discD = layout::wheelD * k; // (the colour's code, then its wheel: drawColourWheel)
    const float gridH = colours ? valueH + pad + itemH + pad + discD + pad : 0.0f; // (the code, the gradient buttons, the wheel)
    const float most = (colours ? layout::wheelMaxH : layout::listMaxH) * k, barRoom = layout::scrollbarRoom * k;
    // How tall what it shows is, `room` wide: its images in rows that fit (they leave a scrollbar its room).
    std::size_t perRow = 1;
    const auto contentIn = [&](float room) {
        perRow = images ? std::max<std::size_t>(1, static_cast<std::size_t>((room - pad) / (cellW + pad))) : 1;
        const std::size_t rows = (entries.size() + perRow - 1) / perRow;
        const float listH = static_cast<float>(rows) * (images ? cellH + pad : itemH) + (byVariable ? itemH : 0.0f);
        return std::max(listH + gridH, itemH) + 2 * pad;
    };
    float content = contentIn(w);
    const float margin = layout::popupMargin * k, lift = layout::popupGap * k, below = d.size.y - margin - (p.at.y + p.at.h + lift), above = p.at.y - lift - margin;
    float h = std::min(content, most);
    SDL_FRect box{std::min(p.at.x, d.size.x - margin - w), p.at.y + p.at.h + lift, w, h};
    if (h > below && above > below) box.y = p.at.y - lift - (h = std::min(h, above)); // (no room under it: over it)
    else h = std::min(h, below);
    box.h = h;
    if (content > h) content = contentIn(w - barRoom); // (it scrolls: its images leave its scrollbar room)
    p.box = box;

    // Its own clicks and wheel (the ones over it were kept from everything under it).
    d.overPopup = false;
    d.clicked = std::exchange(d.popupClick, false);
    d.wheel = std::exchange(d.popupWheel, 0.0f);
    // The keys: the entry they're on moved (a row up or down; across too in a grid of images), in sight.
    if ((p.keyMove.x != 0 || p.keyMove.y != 0) && !entries.empty()) {
        const auto n = static_cast<long>(entries.size()), by = static_cast<long>(p.keyMove.y) * static_cast<long>(perRow) + p.keyMove.x;
        p.keyed = static_cast<std::size_t>(std::clamp(p.keyed ? static_cast<long>(*p.keyed) + by : by > 0 ? 0L : n - 1, 0L, n - 1));
        const float rowH = images ? cellH + pad : itemH, top = gridH + (byVariable ? itemH : 0.0f) + static_cast<float>(*p.keyed / perRow) * rowH;
        p.scroll = std::clamp(p.scroll, top + rowH + 2 * pad - h, top);
        if (engine.screenReader()) announce(entries[*p.keyed].label); // (a screen reader says what they're on now)
    }
    p.keyMove = {};
    if (p.keyed && *p.keyed >= entries.size()) p.keyed.reset();
    p.scroll = std::clamp(p.scroll - ui.wheelOver(box) * itemH * 2, 0.0f, std::max(0.0f, content - h));
    draw::rect(r, {box.x + layout::popupShadow * k, box.y + layout::popupShadow * k, box.w, box.h}, {0, 0, 0, 120}); // (a shadow)
    draw::rect(r, box, kBox);
    ui.scrollbar(&p.scroll, ui.barInside(box), h, content, p.scroll);
    const SDL_FRect inner{box.x, box.y, box.w - (content > h ? barRoom : 0.0f), box.h}; // (what it shows: beside its scrollbar)
    std::optional<DeveloperUi::Clip> clipped(std::in_place, ui, box);
    const float top = box.y + pad - p.scroll, listTop = top + gridH; // (colours to pick from first)
    PopupFrame pop{.ui = ui, .p = p, .box = inner, .top = top, .gradient = std::nullopt, .topText = p.field.value, .bottomText = {}};
    if (colours) pop.split(engine.palette());
    auto chosen = [&](const PopupEntry& e) { return f.multi ? std::ranges::contains(on, lower(e.value)) : lower(e.value) == lower(f.value); };
    if (byVariable) {
        const SDL_FRect row{box.x + pad, listTop, inner.w - 2 * pad, itemH - 2 * k};
        if (ui.over(row)) draw::rect(r, row, kButtonHover);
        ui.textAround("Depends on a variable..."_tr, row.x + 8 * k, row.y + row.h / 2, layout::popupText, kAccent);
        if (ui.click(row)) pop.clicked = std::string(kImageBy);
    }
    const float itemsTop = listTop + (byVariable ? itemH : 0.0f);
    if (entries.empty() && !colours) ui.textAround("Empty"_tr, box.x + 10 * k, itemsTop + itemH / 2, layout::popupText, theme::dim);
    const std::string imagesOf = f.pick == Pick::Expressions ? d.editor.imageBaseOf(f.context) : std::string{}; // (an expression's picture: the character's image)
    std::optional<std::size_t> pointed; // (the entry the pointer or the keys are on: what it shows, beside the list)
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const PopupEntry& e = entries[i];
        if (images) { // a small image, its name under it
            const SDL_FRect c{box.x + pad + static_cast<float>(i % perRow) * (cellW + pad), itemsTop + static_cast<float>(i / perRow) * (cellH + pad), cellW, cellH};
            if (c.y + c.h < box.y || c.y > box.y + box.h) continue;
            if (ui.button(c, "", chosen(e) || p.keyed == i, true, layout::smallButtonText)) pop.clicked = e.value;
            std::string image = f.pick == Pick::Expressions ? imagesOf + "_" + lower(e.value) : lower(e.value);
            if (f.pick == Pick::ImageFiles) { // ("images/icon.png": the image, by its name)
                if (const auto slash = image.rfind('/'); slash != npos) image.erase(0, slash + 1);
                if (const auto dot = image.rfind('.'); dot != npos) image.erase(dot);
            }
            // (a visual named by a visual line, or a character: what it shows)
            const std::string shows = f.pick != Pick::Expressions && f.pick != Pick::ImageFiles && d.script && !e.empty ? d.script->visualOf(e.value) : e.value;
            if (lower(shows) != lower(e.value)) image = lower(shows);
            if (const auto colour = e.empty ? std::nullopt : parseColour(shows, &engine.palette())) {
                draw::rect(r, {c.x + 6 * k, c.y + 6 * k, c.w - 12 * k, c.h - 32 * k}, *colour);
            } else if (const Image pic = e.empty ? Image{} : pictureOf(image)) { // (a movie: its first image)
                const float s = std::min((cellW - 8 * k) / std::max(1.0f, pic.w()), (cellH - 26 * k) / std::max(1.0f, pic.h()));
                pic.draw(r, {c.x + (cellW - pic.w() * s) / 2, c.y + 4 * k, pic.w() * s, pic.h() * s}, 1.0f);
            }
            const std::string name = ui.fitted(e.label, layout::cellText, c.w - 4 * k); // (cut to fit: its whole in a tip)
            ui.textIn(name, {c.x + 2 * k, c.y + c.h - 22 * k, c.w - 4 * k, 20 * k}, layout::cellText, e.empty ? theme::dim : theme::text);
            if (name != e.label) ui.tip(c, e.label);
            continue;
        }
        const SDL_FRect row{box.x + pad, itemsTop + static_cast<float>(i) * itemH, inner.w - 2 * pad, itemH - 2 * k};
        if (row.y + row.h < box.y || row.y > box.y + box.h) continue;
        float tx = row.x + 8 * k;
        const SDL_FRect play{row.x + row.w - row.h, row.y, row.h, row.h};
        if (chosen(e) || ui.over(row) || p.keyed == i) draw::rect(r, row, chosen(e) ? kChosen : kButtonHover);
        if (p.keyed == i) draw::outline(r, row, kAccent); // (the keys are on it)
        if ((ui.over(row) || p.keyed == i) && !e.empty) {
            pointed = i;
            d.choicePointed = {p.id, e.value, d.textFrame}; // (a choice row's list: the one pointed at, as its buttons say it)
        }
        if (const auto colour = e.empty ? std::nullopt : colours ? parseColour(e.value, &engine.palette()) : people ? d.characterColour(engine.palette(), e.value) : std::nullopt)
            tx = ui.swatch(tx, row.y + row.h / 2, *colour); // (a colour, or a character's name colour: as its box shows it)
        const bool voice = f.box == StoryEditor::Box::MadeUpVoice; // (a made-up voice's pitch: heard in a few words)
        const bool heard = sounds && !e.empty && (!voice || madeUpVoicePitch(e.value));
        const float end = row.x + row.w - (heard ? row.h : 0.0f) - 6 * k; // (cut to fit)
        if (const SDL_Texture* own = f.box == StoryEditor::Box::Font && !e.empty ? fontLabel(ui, e.value, e.label, layout::popupText * k) : nullptr) { // (a font: its name in it)
            drawFontWords(ui, own, e.label, tx, row.y + row.h / 2, end - tx);
        } else {
            ui.textFitted(e.label, tx, row.y + row.h / 2, layout::popupText, e.empty ? theme::dim : theme::text, end - tx);
        }
        if (heard) // (heard, not picked; a named sound's file)
            listenButton(ui, play, voice ? std::string(kMadeUpVoiceHeard) + lower(e.value) : d.cachedSound(engine.assets(), d.soundFileOf(e.value)), true);
        if (ui.click(row)) pop.clicked = colours ? pop.changed(e.value) : e.value; // (a gradient's: the colour the wheel is on)
    }
    if (p.keyPick && p.keyed) pop.clicked = colours ? pop.changed(entries[*p.keyed].value) : entries[*p.keyed].value; // (Enter on it)
    p.keyPick = false;
    if (gridH > 0.0f) drawColourWheel(pop);
    clipped.reset(); // (its outline: over the edge)
    draw::outline(r, box, kAccent);
    drawPickPreview(ui, box, f, pointed ? entries[*pointed].value : f.value.empty() ? f.hint : f.value); // (beside it)
    d.clicked = false; // (a click on it, but on nothing in it)
    d.wheel = 0.0f;
    if (!pop.clicked) return;
    std::string value = *pop.clicked;
    const std::string id = p.id;
    if (f.multi) { // turned on or off
        std::vector<std::string> now = on;
        if (!typed.empty() && !now.empty()) now.pop_back(); // (the word half typed)
        if (const auto it = std::ranges::find(now, lower(value)); it != now.end()) now.erase(it);
        else now.push_back(lower(value));
        value = joined(now);
        p.field.value = value;
        p.typed = false;
    } else if (!pop.keepOpen) {
        if (d.focus == id) d.focus.clear();
        p.id.clear();
    }
    d.picked = {id, value};
}

// The whole of a text shown cut to fit (DeveloperUi::tip), once the pointer has rested on it a moment:
// in a little window by the pointer (over it when there's no room under it), over everything; gone
// while the mouse is down.
void Developer::drawTip(DeveloperUi& ui) {
    DeveloperTools& d = tools;
    const float now = engine.toolsTime();
    if (d.tip.empty() || d.tip != d.tipBefore || d.held) d.tipSince = now; // (another one, or none: its moment starts again)
    d.tipBefore = std::exchange(d.tip, {});
    if (d.tipBefore.empty() || now - d.tipSince < layout::tipDelay) return;
    const float k = ui.k, pad = layout::tipPad * k, margin = layout::popupMargin * k;
    const float wrap = std::max(1.0f, std::min(layout::tipW * k, d.size.x - 2 * (margin + pad)));
    const SDL_FPoint size = ui.measure(d.tipBefore, layout::noteText, wrap);
    const float w = size.x + 2 * pad, h = size.y + 2 * pad;
    const SDL_FPoint near = d.tipAt ? SDL_FPoint{d.tipAt->x - 12 * k, d.tipAt->y - 22 * k + 4 * k} : SDL_FPoint{d.mouseX, d.mouseY}; // (the keys': under what they're on)
    float y = near.y + 22 * k;
    if (y + h > d.size.y - margin) y = near.y - 8 * k - h;
    const SDL_FRect box{std::max(margin, std::min(near.x + 12 * k, d.size.x - margin - w)), std::max(margin, y), w, h};
    draw::rect(ui.r, {box.x + layout::popupShadow * k, box.y + layout::popupShadow * k, box.w, box.h}, {0, 0, 0, 120}); // (a shadow)
    draw::rect(ui.r, box, kBox);
    draw::outline(ui.r, box, kEdge);
    ui.text(d.tipBefore, box.x + pad, box.y + pad, layout::noteText, theme::text, wrap);
}

// A colour box's own part of its list: its code (typed into), its wheel (hue around, saturation outward),
// the value bar and the alpha bar; and, for a gradient, which of its colours they're on.
void Developer::drawColourWheel(PopupFrame& pop) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = pop.ui;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    auto& p = pop.p;
    const StoryEditor::Field& f = p.field;
    const SDL_FRect& box = pop.box;
    const float top = pop.top;
    const auto& gradient = pop.gradient;
    const std::string& topText = pop.topText;
    const std::string& bottomText = pop.bottomText;
    const float pad = layout::popupPad * k, itemH = layout::itemH * k;
    const float valueH = layout::codeH * k, discD = layout::wheelD * k, barW = layout::barW * k, barGap = layout::barGap * k;
    const auto& pal = engine.palette();
    if (const auto now = parseColour(gradient && p.editBottom ? bottomText : topText, &pal); now && !p.dragging && d.focus != "popup:colour") {
        hsvOf(*now, p.hue, p.sat, p.val);
        p.alpha = now->a / 255.0f;
    }
    std::optional<SDL_Color> set;
    const SDL_FRect codeBox{box.x + pad, top, box.w - 2 * pad, valueH};
    if (const auto code = ui.field("popup:colour", codeBox, p.field.value, layout::popupText, f.hint))
        if (const auto c = parseColour(*code, &pal)) {
            set = *c;
            pop.clicked = *code;
        }
    // Gradient: the colour fading down to another (navy-black), the wheel on its top or its bottom; One colour: its top alone.
    const SDL_FRect buttons{box.x + pad, top + valueH + pad, box.w - 2 * pad, itemH - 2 * k};
    const float bw = std::floor((buttons.w - 2 * pad) / 3);
    auto third = [&](int i) { return SDL_FRect{buttons.x + static_cast<float>(i) * (bw + pad), buttons.y, bw, buttons.h}; };
    bool arranged = false; // (made a gradient, or one colour again: it stays open)
    if (gradient) {
        if (ui.button(third(0), "Top"_tr, !p.editBottom, true, layout::smallButtonText)) p.editBottom = false, arranged = true;
        if (ui.button(third(1), "Bottom"_tr, p.editBottom, true, layout::smallButtonText)) p.editBottom = true, arranged = true;
        if (ui.button(third(2), "One colour"_tr, false, true, layout::smallButtonText)) pop.clicked = topText, p.editBottom = false, arranged = true;
    } else if (ui.button(third(0), "Gradient"_tr, false, true, layout::smallButtonText)) {
        const std::string one = parseColour(p.field.value, &pal) ? p.field.value : parseColour(f.hint, &pal) ? std::string(f.hint) : std::string("black");
        pop.clicked = one + "-" + one;
        p.editBottom = true;
        arranged = true;
    }
    if (arranged) pop.keepOpen = true;
    const float R = discD / 2, cx = box.x + pad + R, cy = top + valueH + pad + itemH + pad + R;
    const int n = static_cast<int>(std::lround(discD));
    if (!d.wheelImage || d.wheelImageSize != n) { // (its image, made once for its size)
        std::vector<Uint8> pixels(static_cast<std::size_t>(n) * static_cast<std::size_t>(n) * 4);
        for (int py = 0; py < n; ++py)
            for (int px = 0; px < n; ++px) {
                const float dx = static_cast<float>(px) + 0.5f - R, dy = static_cast<float>(py) + 0.5f - R, dist = std::hypot(dx, dy);
                const SDL_Color c = hsvColour(std::atan2(dy, dx) * 180.0f / std::numbers::pi_v<float>, std::min(1.0f, dist / R), 1.0f, 1.0f);
                Uint8* at = &pixels[(static_cast<std::size_t>(py) * static_cast<std::size_t>(n) + static_cast<std::size_t>(px)) * 4];
                at[0] = c.r, at[1] = c.g, at[2] = c.b;
                at[3] = static_cast<Uint8>(std::lround(std::clamp(R - dist + 0.5f, 0.0f, 1.0f) * 255.0f)); // (a soft edge)
            }
        d.wheelImage.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, n, n));
        if (d.wheelImage) {
            SDL_UpdateTexture(d.wheelImage.get(), nullptr, pixels.data(), n * 4);
            SDL_SetTextureBlendMode(d.wheelImage.get(), SDL_BLENDMODE_BLEND);
        }
        d.wheelImageSize = n;
    }
    const SDL_FRect disc{cx - R, cy - R, discD, discD}, valueBar{cx + R + barGap, cy - R, barW, discD}, alphaBar{cx + R + barGap + barW + barGap, cy - R, barW, discD};
    if (d.wheelImage) SDL_RenderTexture(r, d.wheelImage.get(), nullptr, &disc);
    draw::gradient(r, valueBar, hsvColour(p.hue, p.sat, 1.0f, 1.0f), {0, 0, 0, 255});
    draw::outline(r, valueBar, kEdge);
    draw::rect(r, alphaBar, kBox);
    draw::gradient(r, alphaBar, hsvColour(p.hue, p.sat, p.val, 1.0f), hsvColour(p.hue, p.sat, p.val, 0.0f));
    draw::outline(r, alphaBar, kEdge);
    // Dragging on them (started by a click; it goes on while the button is down, beyond them too).
    if (!d.held) p.dragging = 0;
    if (ui.click(disc)) p.dragging = 1;
    else if (ui.click(valueBar)) p.dragging = 2;
    else if (ui.click(alphaBar)) p.dragging = 3;
    if (p.dragging == 1) {
        const float dx = d.mouseX - cx, dy = d.mouseY - cy;
        p.hue = std::fmod(std::atan2(dy, dx) * 180.0f / std::numbers::pi_v<float> + 360.0f, 360.0f);
        p.sat = std::min(1.0f, std::hypot(dx, dy) / R);
        set = hsvColour(p.hue, p.sat, p.val, p.alpha);
    } else if (p.dragging == 2) {
        p.val = 1.0f - std::clamp((d.mouseY - valueBar.y) / valueBar.h, 0.0f, 1.0f);
        set = hsvColour(p.hue, p.sat, p.val, p.alpha);
    } else if (p.dragging == 3) {
        p.alpha = 1.0f - std::clamp((d.mouseY - alphaBar.y) / alphaBar.h, 0.0f, 1.0f);
        set = hsvColour(p.hue, p.sat, p.val, p.alpha);
    }
    if (p.dragging) {
        pop.clicked = pop.changed(hexText(*set));
        if (d.focus == "popup:colour") d.buffer = *pop.clicked, d.caret = d.anchor = d.buffer.size();
    }
    if (set && d.focus != "popup:colour") hsvOf(*set, p.hue, p.sat, p.val);
    if (set) pop.keepOpen = true; // (a colour from the wheel, the bars or the code: it stays open; one from the list under it closes it)
    // Where the colour is on them.
    const float mx = cx + std::cos(p.hue * std::numbers::pi_v<float> / 180.0f) * p.sat * R, my = cy + std::sin(p.hue * std::numbers::pi_v<float> / 180.0f) * p.sat * R;
    draw::circle(r, mx, my, 6 * k, {0, 0, 0, 255});
    draw::circle(r, mx, my, 4 * k, hsvColour(p.hue, p.sat, 1.0f, 1.0f));
    draw::circle(r, mx, my, 4 * k, {255, 255, 255, 60});
    for (const auto& [bar, where] : {std::pair{valueBar, p.val}, {alphaBar, p.alpha}}) {
        const float markY = bar.y + (1.0f - where) * bar.h;
        draw::rect(r, {bar.x - 2 * k, markY - 1.5f * k, bar.w + 4 * k, 3 * k}, theme::text);
        draw::outline(r, {bar.x - 2 * k, markY - 1.5f * k, bar.w + 4 * k, 3 * k}, {0, 0, 0, 255});
    }
}

// Words in a font file (its style: bold, italic, as a font line says it), this big: kept by file, style, words and size; none
// when the file doesn't open.
const SDL_Texture* Developer::wordsInFont(DeveloperUi& ui, const std::string& path, std::string_view style, std::string_view words, float size) {
    DeveloperTools& d = tools;
    const std::string key = std::format("{}|{}|{}|{}", path, lower(style), words, size);
    if (const auto kept = d.fontLabels.find(key); kept != d.fontLabels.end()) return kept->second.get();
    TexturePtr made;
    if (const FontPtr opened{TTF_OpenFont(path.c_str(), size)}) {
        TTF_FontStyleFlags flags = TTF_STYLE_NORMAL;
        if (lower(style).contains("bold")) flags |= TTF_STYLE_BOLD;
        if (lower(style).contains("italic")) flags |= TTF_STYLE_ITALIC;
        TTF_SetFontStyle(opened.get(), flags);
        const std::string text(words);
        if (const SurfacePtr drawn{TTF_RenderText_Blended(opened.get(), text.c_str(), 0, SDL_Color{240, 240, 245, 255})})
            made.reset(SDL_CreateTextureFromSurface(ui.r, drawn.get()));
    }
    return d.fontLabels.emplace(key, std::move(made)).first->second.get();
}

// ...a game's font by its name (none: it has no file, the system's).
const SDL_Texture* Developer::fontLabel(DeveloperUi& ui, std::string_view font, std::string_view words, float size) {
    const FontCache::File* file = engine.fonts_.file(lower(font));
    return file ? wordsInFont(ui, file->path, file->style, words, size) : nullptr;
}

// Those words drawn from `x`, around `middle`, at most `room` wide (cut there, the whole of them in a tip): how wide.
float Developer::drawFontWords(DeveloperUi& ui, const SDL_Texture* words, std::string_view whole, float x, float middle, float room) {
    float tw = 0.0f, th = 0.0f;
    SDL_GetTextureSize(const_cast<SDL_Texture*>(words), &tw, &th);
    const SDL_FRect src{0.0f, 0.0f, std::min(tw, std::max(0.0f, room)), th};
    const SDL_FRect at{x, std::round(middle - th / 2), src.w, th};
    SDL_RenderTexture(ui.r, const_cast<SDL_Texture*>(words), &src, &at);
    if (tw > room) ui.tip(at, whole);
    return src.w;
}

// Beside a box's list (at its right, else at its left), what the entry pointed at shows, else the box's own: an animation
// playing (again a moment after it ends), a style's sample (its button and words), a screen as the game draws it (over the
// story at its first line, or its first choice for a choice's options). None for other boxes (an easing's list: the sample
// under its row follows it), or one the game hasn't.
void Developer::drawPickPreview(DeveloperUi& ui, const SDL_FRect& list, const StoryEditor::Field& f, std::string_view value) {
    using B = StoryEditor::Box;
    DeveloperTools& d = tools;
    const bool animation = f.box == B::Animation, style = f.box == B::Style, screen = f.box == B::Screen || f.box == B::Textbox;
    const std::string name = lower(trim(value));
    if (!(animation || style || screen) || name.empty() || !d.script) return;
    const auto played = animation ? d.script->animations.find(name) : d.script->animations.end();
    const auto shown = screen ? d.script->screens.find(name) : d.script->screens.end();
    if ((animation && played == d.script->animations.end()) || (screen && shown == d.script->screens.end())) return;
    if (style && name != "default" && !d.editor.defines("style", name) && !std::ranges::contains(d.editor.names(StoryEditor::Pick::Styles), name)) return;
    const float k = ui.k, gap = layout::gap * k, margin = layout::popupMargin * k, now = engine.toolsTime();
    const float w = std::round((style ? 400.0f : 288.0f) * k);
    const float h = style ? std::round(110.0f * k) : std::round(w * engine.canvas().h / engine.canvas().w);
    SDL_FRect at{list.x + list.w + gap, list.y, w, h};
    if (at.x + w > d.size.x - margin) at.x = list.x - gap - w; // (no room at its right: at its left)
    at.x = std::max(margin, at.x);
    at.y = std::clamp(at.y, margin, std::max(margin, d.size.y - margin - h));
    const std::string what = std::format("{}:{}", static_cast<int>(f.box), name);
    if (d.pickShown != what) d.pickShown = what, d.pickShownAt = now; // (another: it plays from its start)
    draw::rect(ui.r, {at.x + layout::popupShadow * k, at.y + layout::popupShadow * k, at.w, at.h}, {0, 0, 0, 120}); // (a shadow, as the list's)
    if (animation) {
        const Animation& a = played->second;
        const float round = (a.loops() ? a.length : a.total()) + 0.6f; // (a moment after its end, then again)
        const float t = round > 0.0f ? std::fmod(now - d.pickShownAt, round) : 0.0f;
        drawAnimationSample(ui, a, std::min(t, a.loops() ? a.length : a.total()), at);
    } else if (style) {
        drawStyleSample(ui, name, at, d.pickStyle, engine.canvas().w / 2); // (its words read at half the game's size, or more)
    } else {
        if (!d.pickScreenView || &d.pickScreenView->def() != &shown->second) {
            d.pickScreenView = std::make_unique<ScreenView>(shown->second, engine, true);
            d.pickSample = sampleGame(hasChoiceOptions(shown->second));
        }
        paintScreenSample(ui.r, *d.pickScreenView, d.pickSample, name == "title", at);
    }
}

} // namespace vn
