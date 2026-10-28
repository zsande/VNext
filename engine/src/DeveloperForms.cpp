// The forms the tabs show (a row's, a screen part's, a definition's): their boxes, the bar that formats
// text, and the list or colour wheel that opens under a box; and the Settings tab, one form.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

namespace vn {

namespace {

// A colour from its hue (0 to 360), saturation, value and alpha (0 to 1), and back.
SDL_Color hsvColor(float h, float sat, float v, float a) { // (h in degrees, any way round)
    return draw::hsv(std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f) / 360.0f, sat, v, static_cast<Uint8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f)));
}

void hsvOf(SDL_Color c, float& h, float& sat, float& v) { // (a grey keeps the hue it had)
    const auto [hue, s, value] = vn::hsvOf(c);
    sat = s;
    v = value;
    if (s > 0.0f) h = hue;
}

std::string hexText(SDL_Color c) { return c.a == 255 ? std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b) : std::format("#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a); }

// An entry in a box's list: its value, what the list shows, what it is (said after it in grey: "value:what
// it is" in a list of suggestions), and whether it's what the box empty means.
struct PopupEntry {
    std::string value, label;
    std::string about;
    bool empty = false;
};

// What a box's list holds: the values it often has and the names it lists, each once, as far as they
// match what's `typed`; first, what the box empty means; a colour's, only the words it can be and the
// game's own colours. "Depends on a variable" isn't among them (`byVariable`: it was; a row of its own).
std::vector<PopupEntry> popupEntries(DeveloperTools::Popup& p, const StoryEditor& editor, const StoryEditor::Field& f, const StoryEditor::BoxKind& kind,
                                     const std::string& typed, bool& byVariable) {
    std::vector<PopupEntry> entries;
    std::set<std::string> seen; // (each value once)
    auto add = [&](std::string value, std::string label, std::string about, bool empty) {
        const std::string low = lower(value);
        if (seen.contains(low)) return;
        if (!typed.empty() && !low.contains(typed) && !lower(label).contains(typed) && !lower(about).contains(typed)) return;
        seen.insert(low);
        entries.push_back({std::move(value), std::move(label), std::move(about), empty});
    };
    for (const std::string_view entry : wordsOf(f.suggest, '|')) {
        const auto colon = entry.find(':');
        const std::string value(entry.substr(0, colon));
        add(value, value, colon == npos ? std::string{} : std::string(entry.substr(colon + 1)), false);
    }
    if (p.namesVersion != editor.version()) { // (found once, not every frame it's open)
        p.names = editor.names(f.pick, f.context);
        p.namesVersion = editor.version();
    }
    for (const std::string& name : p.names) add(name, name, {}, false);
    // First, what the field empty means (unless the list is empty, or has it already): its hint; a box of
    // pictures without one: None, to empty it for what's usual; a colour's without one: transparent.
    const bool colourOnly = kind.wheel && !kind.pictures && f.hint.empty();
    const std::string_view emptyMeans = !f.hint.empty() ? f.hint : kind.pictures ? std::string_view("None") : colourOnly ? std::string_view("Transparent") : std::string_view{};
    const std::string emptyValue = colourOnly ? "#00000000" : "";
    if (!entries.empty() && !emptyMeans.empty() && !f.multi && !p.typed && typed.empty() &&
        std::ranges::none_of(entries, [&](const PopupEntry& e) { return lower(e.value) == lower(colourOnly ? std::string_view(emptyValue) : emptyMeans); }))
        entries.insert(entries.begin(), PopupEntry{.value = emptyValue, .label = std::string(emptyMeans), .empty = true}); // (transparent: as the empty one, kept and dim)

    if (f.box == StoryEditor::Box::Colour) { // (under the wheel: the game's own colours, and the words the box can be)
        std::vector<std::string> words;
        for (const std::string_view entry : wordsOf(f.suggest, '|')) words.emplace_back(entry.substr(0, entry.find(':')));
        const auto& own = editor.definitions("color"); // (once, not a scan of the scripts for each entry)
        std::erase_if(entries, [&](const PopupEntry& e) {
            return !e.empty && !std::ranges::contains(words, e.value) && std::ranges::none_of(own, [&](const StoryEditor::Definition& c) { return lower(c.name) == lower(e.value); });
        });
    }
    const auto by = std::ranges::find(entries, std::string(kPictureBy), &PopupEntry::value); // ("depends on a variable": a row of its own, over the pictures)
    byVariable = by != entries.end();
    if (byVariable) entries.erase(by);
    return entries;
}

} // namespace

// Boxes that share a row in a form when they come one after the other: the first on the left, with
// its label in the label column, the second beside it with its label between.
constexpr std::pair<std::string_view, std::string_view> kPairedLabels[]{
    {"Width", "Height"},       {"Left", "Top"},         {"Item width", "Item height"},    {"Spacing across", "Spacing down"},
    {"Minimum", "Maximum"},    {"Zoom", "Opacity"},     {"Place", "Up or down"},          {"Looks at (across)", "Looks at (up or down)"},
    {"Amount", "Speed"},       {"Eyes X", "Eyes Y"},    {"Mouth X", "Mouth Y"},           {"Duration (seconds)", "Easing"},
    {"Font size", "Text colour"}, {"Padding", "Opacity"}, {"Shadow colour", "Shadow offset"}, {"Background colour", "Border colour"},
    {"Speaker", "Expression"}, {"Fade in (seconds)", "Volume"}, {"Strength", "Duration (seconds)"}, {"Strength", "Fade in (seconds)"}, {"Time limit (seconds)", "Stays up for (lines)"},
    {"Name colour", "Text colour"}, {"Made-up voice", "Made-up voice speed"}, {"Bob distance", "Bob speed"}, {"Bar height", "Bar colour"},
    {"Track colour", "Fill colour"}, {"Columns", "Spacing"},
}; // (numbers, colours and short names: a file's, a picture's or a font's name, or words, need a box's whole width)

namespace {

// Boxes that hold a name that can be long (a file's, a picture's, a font's), or text: never half as wide.
bool needsWidth(const StoryEditor::Field& f) {
    return f.rich || StoryEditor::kindOf(f.box).wide;
}

} // namespace

std::optional<Developer::FormEdit> Developer::drawForm(DeveloperUi& ui, const std::string& id, const std::vector<StoryEditor::Field>& given, float x,
                                                 float w, float labelW, float& y) {
    DeveloperTools& d = tools;
    std::vector<StoryEditor::Field> fields = given; // (each box as its kind has it)
    for (StoryEditor::Field& f : fields) applyBox(f);
    const float k = ui.k, boxH = layout::boxH * k, gap = layout::rowGap * k;
    std::optional<FormEdit> out;
    std::string_view group; // (the heading the fields are under: every one's rows show)
    bool first = true;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const StoryEditor::Field& f = fields[i];
        if (f.hidden) continue;
        if (!f.group.empty() && f.group != group) {
            group = f.group;
            y += first ? 0.0f : layout::groupGap * k;
            ui.text(group, x, y, layout::groupText, kAccent);
            y += layout::groupStep * k;
        }
        first = false;
        const float in = group.empty() ? 0.0f : layout::groupIndent * k;
        auto labelOf = [](const StoryEditor::Field& g) { return g.label.empty() ? std::string_view(g.name) : std::string_view(g.label); };
        const std::string_view label = labelOf(f);
        // A box: typed into or picked from, with its colour beside it when it names one, and the
        // buttons for what its words can be under it while it's typed into.
        auto drawBox = [&](const StoryEditor::Field& g, const SDL_FRect& box, bool withBar) {
            const std::string fid = std::format("{}:{}", id, g.name);
            SDL_FRect inner = box;
            std::string sound; // (a sound's box, when its file is there: a button beside it to hear it)
            if (StoryEditor::kindOf(g.box).listen)
                if (const std::string name = lower(trim(g.value)); !name.empty())
                    if (const auto found = d.cachedFoundAudio(engine.assets(), name)) {
                        sound = *found;
                        inner.w -= boxH + 4 * k;
                    }
            if (const auto value = ui.pickField(fid, inner, g, layout::boxText)) out = FormEdit{.name = g.name, .value = *value, .typed = true};
            if (!sound.empty()) listenButton(ui, {box.x + box.w - boxH, box.y, boxH, boxH}, sound, true);
            if (withBar && g.rich && d.focus == fid) { // what the words can be: buttons under the box
                y += boxH + 4 * k;
                if (const auto text = drawFormatBar(ui, fid, box.x, box.w, y)) out = FormEdit{.name = g.name, .value = *text, .typed = true};
                y -= boxH;
            }
        };
        // Boxes that go together on one row (an action: what it's about, its kind, and what that asks for):
        // their labels between them when there's room for all, else just the boxes, else each on its own row.
        std::vector<std::size_t> run{i};
        for (std::size_t j = i + 1; j < fields.size(); ++j) {
            if (fields[j].hidden) continue;
            if (!fields[j].beside || !fields[j].choices.empty() || needsWidth(fields[j])) break;
            run.push_back(j);
        }
        if (run.size() > 1 && labelW > 0.0f && f.choices.empty() && !needsWidth(f)) {
            const float room = w - labelW, between = 6 * k;
            auto need = [&](const StoryEditor::Field& g) { // (as wide as its words need, with its arrow; a little at least)
                const std::string_view shown = trim(g.value).empty() ? g.hint : std::string_view(g.value);
                const bool arrow = g.pick != StoryEditor::Pick::None || !g.suggest.empty();
                return std::max(layout::runBoxW * k, ui.width(shown, layout::boxText) + 20 * k + (arrow ? boxH : 0.0f));
            };
            std::vector<float> widths;
            float total = -between, labels = 0.0f;
            for (std::size_t j = 0; j < run.size(); ++j) {
                widths.push_back(need(fields[run[j]]));
                total += widths.back() + between;
                if (j > 0) labels += ui.width(labelOf(fields[run[j]]), layout::labelText) + 12 * k;
            }
            // (their labels between them when all fit on one row with those, else just the boxes, on as many
            // rows as they need; the boxes on a row share out the room left on it, so each row is filled)
            const bool withLabels = total + labels <= room;
            std::vector<float> lws(run.size(), 0.0f);
            if (withLabels)
                for (std::size_t j = 1; j < run.size(); ++j) lws[j] = ui.width(labelOf(fields[run[j]]), layout::labelText) + 12 * k;
            std::vector<std::vector<std::size_t>> rowBoxes(1);
            float used = 0.0f;
            for (std::size_t j = 0; j < run.size(); ++j) {
                widths[j] = std::min(widths[j], room - lws[j]);
                float add = (rowBoxes.back().empty() ? 0.0f : between) + lws[j] + widths[j];
                if (!rowBoxes.back().empty() && used + add > room + 0.5f) {
                    rowBoxes.emplace_back();
                    used = 0.0f;
                    add = lws[j] + widths[j];
                }
                rowBoxes.back().push_back(j);
                used += add;
            }
            ui.textAround(label, x + in, y + boxH / 2, layout::labelText, theme::dim, labelW - in - 8 * k);
            for (std::size_t r = 0; r < rowBoxes.size(); ++r) {
                float taken = between * static_cast<float>(rowBoxes[r].size() - 1);
                for (const std::size_t j : rowBoxes[r]) taken += lws[j] + widths[j];
                const float spare = std::max(0.0f, room - taken) / static_cast<float>(rowBoxes[r].size());
                float bx = x + labelW;
                for (std::size_t c = 0; c < rowBoxes[r].size(); ++c) {
                    const std::size_t j = rowBoxes[r][c];
                    const StoryEditor::Field& g = fields[run[j]];
                    if (c > 0) bx += between;
                    if (lws[j] > 0.0f) {
                        ui.textAround(labelOf(g), bx, y + boxH / 2, layout::labelText, theme::dim, lws[j] - 4 * k);
                        bx += lws[j];
                    }
                    const float bw = std::floor(widths[j] + spare);
                    drawBox(g, {bx, y, bw, boxH}, false);
                    bx += bw;
                }
                if (r + 1 < rowBoxes.size()) y += boxH + gap;
            }
            y += boxH + gap;
            i = run.back();
            continue;
        }
        // Two that go together (a width and a height, a place across and up) share a row.
        std::size_t next = i + 1;
        while (next < fields.size() && fields[next].hidden) ++next;
        const bool pair = labelW > 0.0f && f.choices.empty() && !needsWidth(f) && next < fields.size() && fields[next].group.empty() && fields[next].choices.empty() &&
                          !needsWidth(fields[next]) && (std::ranges::contains(kPairedLabels, std::pair{label, labelOf(fields[next])}) || labelOf(fields[next]) == "To"); // (a change and what to)
        if (pair) { // (when there's room for both to be read: the second's label on two lines when it's long, and each box wide enough)
            const StoryEditor::Field& g = fields[next];
            const std::string_view label2 = labelOf(g);
            const float room = w - labelW;
            float lw2 = ui.width(label2, layout::labelText) + 12 * k;
            if (lw2 > room * layout::pairLabelPart) { // (on two lines, no narrower than its longest word)
                float longest = 0.0f;
                for (const auto word : std::views::split(label2, ' ')) longest = std::max(longest, ui.width(std::string_view(word.begin(), word.end()), layout::labelText));
                lw2 = std::max(std::round(room * layout::pairLabelPart), longest + 12 * k);
            }
            const float bw = std::floor((room - lw2 - 6 * k) / 2);
            if (bw >= layout::pairBoxW * k) {
                ui.textAround(label, x + in, y + boxH / 2, layout::labelText, theme::dim, labelW - in - 8 * k);
                drawBox(f, {x + labelW, y, bw, boxH}, false);
                ui.textAround(label2, x + labelW + bw + 6 * k, y + boxH / 2, layout::labelText, theme::dim, lw2 - 4 * k);
                drawBox(g, {x + labelW + bw + 6 * k + lw2, y, bw, boxH}, false);
                y += boxH + gap;
                i = next;
                continue;
            }
        }
        if (labelW > 0.0f) {
            ui.textAround(label, x + in, y + boxH / 2, layout::labelText, theme::dim, labelW - in - 8 * k);
        } else {
            ui.text(label, x, y, layout::labelText, theme::dim);
            y += layout::labelAboveH * k;
        }
        const SDL_FRect box{x + labelW, y, w - labelW, boxH};
        if (!f.choices.empty()) {
            if (const auto word = ui.choices(box, f, y)) out = FormEdit{.name = f.name, .value = *word, .typed = false};
        } else {
            drawBox(f, box, true);
        }
        y += boxH + gap;
    }
    return out;
}

// Buttons that make the words chosen in the box typed into (or what's typed next) bold, italic,
// underlined, crossed out, coloured or typed at another speed, with furigana or a tip over them; or
// that put in a pause, a variable, how the line appears, or no wait at its end. Gives the new text.
std::optional<std::string> Developer::drawFormatBar(DeveloperUi& ui, const std::string& id, float x, float w, float& y) {
    DeveloperTools& d = tools;
    const float k = ui.k, h = layout::barButtonH * k;
    static constexpr std::array<std::string_view, 12> kLabels{"Bold", "Italic", "Underline", "Crossed out", "Colour", "Speed",
                                                               "Furigana", "Tip", "Pause", "Variable", "Appears", "No wait"};
    static constexpr std::array<bool, 12> kWith{false, true, true, true, false, true, false, true, false, true, false, true}; // (the looks together, then each pair)
    const auto places = ui.flow({kLabels.begin(), kLabels.end()}, w, h, layout::barButtonGap * k, layout::smallText, std::vector<bool>(kWith.begin(), kWith.end()));
    auto chosen = [&] { return std::pair{std::min(d.anchor, d.caret), std::max(d.anchor, d.caret)}; };
    auto wrap = [&](const std::string& open, const std::string& close) { // (around what's chosen, which stays chosen)
        const auto [from, to] = chosen();
        d.buffer.insert(to, close);
        d.buffer.insert(from, open);
        d.anchor = from + open.size();
        d.caret = to + open.size();
    };
    auto put = [&](std::size_t at, const std::string& text) {
        d.buffer.insert(at, text);
        d.caret = d.anchor = at + text.size();
    };
    std::optional<std::string> out;
    for (std::size_t i = 0; i < kLabels.size(); ++i) {
        const SDL_FRect b{x + places[i].x, y + places[i].y, places[i].w, places[i].h};
        const std::string_view label = kLabels[i];
        const std::string bid = std::format("{}:format:{}", id, label);
        std::optional<std::string> picked;
        auto list = [&](const StoryEditor::Field& f) { picked = ui.listButton(bid, b, label, f); };
        bool clicked = false;
        if (label == "Colour") list({.name = "colour", .box = Box::Colour});
        else if (label == "Speed") list({.name = "speed", .box = Box::TextSpeed});
        else if (label == "Tip") list({.name = "tip", .box = Box::Record, .context = "tip"});
        else if (label == "Pause") list({.name = "pause", .box = Box::Pause});
        else if (label == "Variable") list({.name = "variable", .box = Box::Variable});
        else if (label == "Appears") list({.name = "appears", .box = Box::Reveal, .choices = "letter word line"});
        else clicked = ui.button(b, label, false, true, 13);
        if (!clicked && !picked) continue;
        if (label == "Bold") wrap("{bold}", "{/bold}");
        else if (label == "Italic") wrap("{italic}", "{/italic}");
        else if (label == "Underline") wrap("{underline}", "{/underline}");
        else if (label == "Crossed out") wrap("{strike}", "{/strike}");
        else if (label == "Furigana") { // (the reading over the words chosen, to type over)
            const std::size_t from = chosen().first;
            wrap("{ruby reading}", "{/ruby}");
            d.anchor = from + 6;
            d.caret = from + 13;
        } else if (label == "No wait") {
            if (!d.buffer.ends_with("{nowait}")) put(d.buffer.size(), "{nowait}");
        } else if (label == "Colour") { // (from the wheel while it stays open: the tag around the words changes, not one more around them)
            const auto [from, to] = chosen();
            const std::string open = std::format("{{color {}}}", *picked);
            const std::string_view before(d.buffer.data(), from), after(d.buffer.data() + to, d.buffer.size() - to);
            if (const auto tag = before.rfind("{color "); tag != npos && from > 0 && before.find('}', tag) == from - 1 && after.starts_with("{/color}")) {
                d.buffer.replace(tag, from - tag, open);
                d.anchor = tag + open.size();
                d.caret = d.anchor + (to - from);
            } else {
                wrap(open, "{/color}");
            }
        }
        else if (label == "Speed") wrap(std::format("{{speed {}}}", *picked), "{/speed}");
        else if (label == "Tip") wrap(std::format("{{tip {}}}", *picked), "{/tip}");
        else if (label == "Pause") put(chosen().second, *picked == "click" ? std::string("{pause}") : std::format("{{pause {}}}", *picked));
        else if (label == "Variable") put(chosen().second, std::format("{{{}}}", *picked));
        else if (label == "Appears") put(0, std::format("{{reveal {}}}", *picked));
        d.focus = id; // (still typed into)
        out = d.buffer;
    }
    y += places.back().y + places.back().h;
    // The word at the caret, when it isn't spelt right: what it may be instead (a click puts it in its
    // place), or Learn it (it's right).
    const std::string language = spellingLanguage(engine.config().language);
    for (const auto& [from, to] : misspelled(d.spelling, d.buffer, language, ui.names())) {
        if (d.caret < from || d.caret > to) continue;
        const std::string word = d.buffer.substr(from, to - from);
        auto& guesses = d.spellingGuesses; // (looked up once)
        auto it = guesses.find(language + "|" + word);
        if (it == guesses.end()) {
            auto all = spellings(word, language);
            if (all.size() > 5) all.resize(5);
            it = guesses.emplace(language + "|" + word, std::move(all)).first;
        }
        std::vector<std::string_view> labels{"Spelling:"};
        for (const std::string& g : it->second) labels.emplace_back(g);
        labels.emplace_back("Learn it");
        const auto row = ui.flow(labels, w, h, 4 * k, 13);
        y += 4 * k;
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const SDL_FRect b{x + row[i].x, y + row[i].y, row[i].w, row[i].h};
            if (i == 0) {
                ui.textAround(labels[0], b.x, b.y + b.h / 2, 13, kBad);
                continue;
            }
            if (!ui.button(b, labels[i], false, true, 13)) continue;
            if (i + 1 == labels.size()) {
                learnSpelling(word);
                d.spelling.erase(language + "|" + word);
            } else {
                const std::string with(labels[i]);
                d.buffer.replace(from, to - from, with);
                d.caret = d.anchor = from + with.size();
                out = d.buffer;
            }
            d.focus = id;
        }
        y += row.back().y + row.back().h;
        break;
    }
    return out;
}

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
// it. Pictures show as small pictures, colours with a sample (and colours to pick from), and sounds
// with a button that plays them. A click picks one (several, for a field of several: a click turns
// one on or off); the field takes it next frame.
void Developer::drawPopup(DeveloperUi& ui) {
    DeveloperTools& d = tools;
    auto& p = d.popup;
    if (!p.shown) p.id.clear();
    if (p.id.empty()) {
        p.box = {};
        if (d.focus == "popup:color") d.focus.clear();
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
    const bool colors = kind.wheel; // (a wheel over the list: a background's is a picture or a colour)
    if (colors && d.focus == "popup:color" && !trim(d.buffer).empty() && !parseColor(d.buffer, &engine.palette()))
        typed = lower(trim(d.buffer)); // (a name's start typed above the wheel, not a code: what fits it under the wheel)
    bool byVariable = false;
    const std::vector<PopupEntry> entries = popupEntries(p, d.editor, f, kind, typed, byVariable);
    const bool pictures = kind.pictures, sounds = kind.listen, people = f.pick == Pick::Characters || f.pick == Pick::Shown;
    const float pad = layout::popupPad * k, itemH = layout::itemH * k, cellW = layout::cellW * k, cellH = layout::cellH * k;
    float w = std::max(p.at.w, (pictures ? layout::picturesW : layout::listW) * k);
    for (const PopupEntry& e : entries) // (an entry with what it is said after it: room for the longest, up to half the window)
        if (!pictures && !e.about.empty()) w = std::max(w, std::min(ui.width(e.label, 14) + 12 * k + ui.width(e.about, 14) + 44 * k, engine.canvas().w / 2));
    const auto perRow = pictures ? std::max<std::size_t>(1, static_cast<std::size_t>((w - pad) / (cellW + pad))) : 1;
    const std::size_t rows = (entries.size() + perRow - 1) / perRow;
    const float listH = static_cast<float>(rows) * (pictures ? cellH + pad : itemH) + (byVariable ? itemH : 0.0f);
    const float valueH = layout::codeH * k, discD = layout::wheelD * k; // (the colour's code, then its wheel: drawColourWheel)
    const float gridH = colors ? valueH + pad + itemH + pad + discD + pad : 0.0f; // (the code, the gradient buttons, the wheel)
    const float content = std::max(listH + gridH, itemH) + 2 * pad;
    const float margin = layout::popupMargin * k, lift = layout::popupGap * k, below = engine.canvas().h - margin - (p.at.y + p.at.h + lift), above = p.at.y - lift - margin;
    float h = std::min(content, (colors ? layout::wheelMaxH : layout::listMaxH) * k);
    SDL_FRect box{std::min(p.at.x, engine.canvas().w - margin - w), p.at.y + p.at.h + lift, w, h};
    if (h > below && above > below) box.y = p.at.y - lift - (h = std::min(h, above)); // (no room under it: over it)
    else h = std::min(h, below);
    box.h = h;
    p.box = box;

    // Its own clicks and wheel (the ones over it were kept from everything under it).
    d.overPopup = false;
    d.clicked = std::exchange(d.popupClick, false);
    d.wheel = std::exchange(d.popupWheel, 0.0f);
    p.scroll = std::clamp(p.scroll - ui.wheelOver(box) * itemH * 2, 0.0f, std::max(0.0f, content - h));
    draw::rect(r, {box.x + layout::popupShadow * k, box.y + layout::popupShadow * k, box.w, box.h}, {0, 0, 0, 120}); // (a shadow)
    draw::rect(r, box, kBox);
    std::optional<DeveloperUi::Clip> clipped(std::in_place, ui, box, true);
    const float top = box.y + pad - p.scroll, listTop = top + gridH; // (colours to pick from first)
    PopupFrame pop{.ui = ui, .p = p, .box = box, .top = top, .gradient = std::nullopt, .topText = p.field.value, .bottomText = {}};
    if (colors) pop.split(engine.palette());
    auto chosen = [&](const PopupEntry& e) { return f.multi ? std::ranges::contains(on, lower(e.value)) : lower(e.value) == lower(f.value); };
    if (byVariable) {
        const SDL_FRect row{box.x + pad, listTop, box.w - 2 * pad, itemH - 2 * k};
        if (ui.over(row)) draw::rect(r, row, kButtonHover);
        ui.textAround("Depends on a variable...", row.x + 8 * k, row.y + row.h / 2, 14, kAccent);
        if (ui.click(row)) pop.clicked = std::string(kPictureBy);
    }
    const float itemsTop = listTop + (byVariable ? itemH : 0.0f);
    if (entries.empty() && !colors) ui.textAround("Empty", box.x + 10 * k, itemsTop + itemH / 2, 14, theme::dim);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const PopupEntry& e = entries[i];
        if (pictures) { // a small picture, its name under it
            const SDL_FRect c{box.x + pad + static_cast<float>(i % perRow) * (cellW + pad), itemsTop + static_cast<float>(i / perRow) * (cellH + pad), cellW, cellH};
            if (c.y + c.h < box.y || c.y > box.y + box.h) continue;
            if (ui.button(c, "", chosen(e), true, 12)) pop.clicked = e.value;
            std::string picture = f.pick == Pick::Expressions ? lower(f.context) + "_" + lower(e.value) : lower(e.value);
            if (f.pick == Pick::PictureFiles) { // ("images/icon.png": the picture, by its name)
                if (const auto slash = picture.rfind('/'); slash != npos) picture.erase(0, slash + 1);
                if (const auto dot = picture.rfind('.'); dot != npos) picture.erase(dot);
            }
            if (const auto color = e.empty ? std::nullopt : parseColor(e.value, &engine.palette())) {
                draw::rect(r, {c.x + 6 * k, c.y + 6 * k, c.w - 12 * k, c.h - 32 * k}, *color);
            } else if (const Picture pic = e.empty ? Picture{} : engine.assets().picture(picture)) {
                const float s = std::min((cellW - 8 * k) / std::max(1.0f, pic.w()), (cellH - 26 * k) / std::max(1.0f, pic.h()));
                pic.draw(r, {c.x + (cellW - pic.w() * s) / 2, c.y + 4 * k, pic.w() * s, pic.h() * s}, 1.0f);
            }
            ui.textIn(e.label, {c.x + 2 * k, c.y + c.h - 22 * k, c.w - 4 * k, 20 * k}, 12, e.empty ? theme::dim : theme::text);
            continue;
        }
        const SDL_FRect row{box.x + pad, itemsTop + static_cast<float>(i) * itemH, box.w - 2 * pad, itemH - 2 * k};
        if (row.y + row.h < box.y || row.y > box.y + box.h) continue;
        float tx = row.x + 8 * k;
        const SDL_FRect play{row.x + row.w - row.h, row.y, row.h, row.h};
        if (chosen(e) || ui.over(row)) draw::rect(r, row, chosen(e) ? kChosen : kButtonHover);
        if (const auto color = e.empty ? std::nullopt : colors ? parseColor(e.value, &engine.palette()) : people ? d.characterColor(engine.palette(), e.value) : std::nullopt)
            tx = ui.swatch(tx, row.y + row.h / 2, *color); // (a colour, or a character's name colour: as its box shows it)
        ui.textAround(e.label, tx, row.y + row.h / 2, 14, e.empty ? theme::dim : theme::text);
        if (!e.about.empty()) ui.textAround(e.about, tx + ui.width(e.label, 14) + 12 * k, row.y + row.h / 2, 14, theme::dim);
        if (sounds && !e.empty) listenButton(ui, play, d.cachedAudio(engine.assets(), lower(e.value)), true); // (heard, not picked)
        if (ui.click(row)) pop.clicked = colors ? pop.changed(e.value) : e.value; // (a gradient's: the colour the wheel is on)
    }
    if (gridH > 0.0f) drawColourWheel(pop);
    clipped.reset(); // (its outline: over the edge)
    draw::outline(r, box, kAccent);
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
    if (const auto now = parseColor(gradient && p.editBottom ? bottomText : topText, &pal); now && !p.dragging && d.focus != "popup:color") {
        hsvOf(*now, p.hue, p.sat, p.val);
        p.alpha = now->a / 255.0f;
    }
    std::optional<SDL_Color> set;
    const SDL_FRect codeBox{box.x + pad, top, box.w - 2 * pad, valueH};
    if (const auto code = ui.field("popup:color", codeBox, p.field.value, 14, f.hint))
        if (const auto c = parseColor(*code, &pal)) {
            set = *c;
            pop.clicked = *code;
        }
    // Gradient: the colour fading down to another (navy-black), the wheel on its top or its bottom; One colour: its top alone.
    const SDL_FRect buttons{box.x + pad, top + valueH + pad, box.w - 2 * pad, itemH - 2 * k};
    const float bw = std::floor((buttons.w - 2 * pad) / 3);
    auto third = [&](int i) { return SDL_FRect{buttons.x + static_cast<float>(i) * (bw + pad), buttons.y, bw, buttons.h}; };
    bool arranged = false; // (made a gradient, or one colour again: it stays open)
    if (gradient) {
        if (ui.button(third(0), "Top", !p.editBottom, true, 13)) p.editBottom = false, arranged = true;
        if (ui.button(third(1), "Bottom", p.editBottom, true, 13)) p.editBottom = true, arranged = true;
        if (ui.button(third(2), "One colour", false, true, 13)) pop.clicked = topText, p.editBottom = false, arranged = true;
    } else if (ui.button(third(0), "Gradient", false, true, 13)) {
        const std::string one = parseColor(p.field.value, &pal) ? p.field.value : parseColor(f.hint, &pal) ? std::string(f.hint) : std::string("black");
        pop.clicked = one + "-" + one;
        p.editBottom = true;
        arranged = true;
    }
    if (arranged) pop.keepOpen = true;
    const float R = discD / 2, cx = box.x + pad + R, cy = top + valueH + pad + itemH + pad + R;
    const int n = static_cast<int>(std::lround(discD));
    if (!d.wheelPicture || d.wheelPictureSize != n) { // (its picture, made once for its size)
        std::vector<Uint8> pixels(static_cast<std::size_t>(n) * static_cast<std::size_t>(n) * 4);
        for (int py = 0; py < n; ++py)
            for (int px = 0; px < n; ++px) {
                const float dx = static_cast<float>(px) + 0.5f - R, dy = static_cast<float>(py) + 0.5f - R, dist = std::hypot(dx, dy);
                const SDL_Color c = hsvColor(std::atan2(dy, dx) * 180.0f / std::numbers::pi_v<float>, std::min(1.0f, dist / R), 1.0f, 1.0f);
                Uint8* at = &pixels[(static_cast<std::size_t>(py) * static_cast<std::size_t>(n) + static_cast<std::size_t>(px)) * 4];
                at[0] = c.r, at[1] = c.g, at[2] = c.b;
                at[3] = static_cast<Uint8>(std::lround(std::clamp(R - dist + 0.5f, 0.0f, 1.0f) * 255.0f)); // (a soft edge)
            }
        d.wheelPicture.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, n, n));
        if (d.wheelPicture) {
            SDL_UpdateTexture(d.wheelPicture.get(), nullptr, pixels.data(), n * 4);
            SDL_SetTextureBlendMode(d.wheelPicture.get(), SDL_BLENDMODE_BLEND);
        }
        d.wheelPictureSize = n;
    }
    const SDL_FRect disc{cx - R, cy - R, discD, discD}, valueBar{cx + R + barGap, cy - R, barW, discD}, alphaBar{cx + R + barGap + barW + barGap, cy - R, barW, discD};
    if (d.wheelPicture) SDL_RenderTexture(r, d.wheelPicture.get(), nullptr, &disc);
    draw::gradient(r, valueBar, hsvColor(p.hue, p.sat, 1.0f, 1.0f), {0, 0, 0, 255});
    draw::outline(r, valueBar, kEdge);
    draw::rect(r, alphaBar, kBox);
    draw::gradient(r, alphaBar, hsvColor(p.hue, p.sat, p.val, 1.0f), hsvColor(p.hue, p.sat, p.val, 0.0f));
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
        set = hsvColor(p.hue, p.sat, p.val, p.alpha);
    } else if (p.dragging == 2) {
        p.val = 1.0f - std::clamp((d.mouseY - valueBar.y) / valueBar.h, 0.0f, 1.0f);
        set = hsvColor(p.hue, p.sat, p.val, p.alpha);
    } else if (p.dragging == 3) {
        p.alpha = 1.0f - std::clamp((d.mouseY - alphaBar.y) / alphaBar.h, 0.0f, 1.0f);
        set = hsvColor(p.hue, p.sat, p.val, p.alpha);
    }
    if (p.dragging) {
        pop.clicked = pop.changed(hexText(*set));
        if (d.focus == "popup:color") d.buffer = *pop.clicked, d.caret = d.anchor = d.buffer.size();
    }
    if (set && d.focus != "popup:color") hsvOf(*set, p.hue, p.sat, p.val);
    if (set) pop.keepOpen = true; // (a colour from the wheel, the bars or the code: it stays open; one from the list under it closes it)
    // Where the colour is on them.
    const float mx = cx + std::cos(p.hue * std::numbers::pi_v<float> / 180.0f) * p.sat * R, my = cy + std::sin(p.hue * std::numbers::pi_v<float> / 180.0f) * p.sat * R;
    draw::circle(r, mx, my, 6 * k, {0, 0, 0, 255});
    draw::circle(r, mx, my, 4 * k, hsvColor(p.hue, p.sat, 1.0f, 1.0f));
    draw::circle(r, mx, my, 4 * k, {255, 255, 255, 60});
    for (const auto& [bar, where] : {std::pair{valueBar, p.val}, {alphaBar, p.alpha}}) {
        const float markY = bar.y + (1.0f - where) * bar.h;
        draw::rect(r, {bar.x - 2 * k, markY - 1.5f * k, bar.w + 4 * k, 3 * k}, theme::text);
        draw::outline(r, {bar.x - 2 * k, markY - 1.5f * k, bar.w + 4 * k, 3 * k}, {0, 0, 0, 255});
    }
}

// The Settings tab: the game's config lines as a form. An empty box, or the choice a setting starts
// with, takes its line out of main.vnscript (it has its default again).
void Developer::drawSettings(DeveloperUi& ui, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    SDL_Renderer* r = ui.r;
    const float k = ui.k, rowH = layout::settingRowH * k, headH = layout::settingHeadH * k, noteH = layout::settingNoteH * k, otherH = layout::settingOtherH * k;
    // Mistakes on config lines the form has no row for (config default..., config text...), at the top.
    std::vector<const ScriptError*> others;
    for (const ScriptError& e : d.problems)
        if (inSettings(editor, e) && std::ranges::none_of(kSettings, [&](const Setting& s) {
                return !s.name.empty() && editor.settingLine(s.name) == static_cast<std::size_t>(e.line - 1);
            }))
            others.push_back(&e);
    const float headNoteH = layout::settingHeadNoteH * k; // (a heading's note, under it)
    const float labelW = std::round(std::min(layout::settingLabelW * k, box.w * layout::settingLabelPart)), fieldW = std::round(std::min(layout::settingFieldW * k, box.w * layout::settingFieldPart));
    const float boxH = layout::boxH * k, pairW = std::floor((fieldW - 12 * k) / 2); // (a box; one of a pair's)
    auto pair = [&](std::size_t i) -> const Setting* { return i + 1 < kSettings.size() && kSettings[i + 1].with ? &kSettings[i + 1] : nullptr; }; // (two on one row)
    auto extra = [&](const Setting& s, float w) { // (buttons that don't fit a row go on under it)
        if (s.choices.empty() && StoryEditor::kindOf(s.box).choices.empty()) return 0.0f;
        StoryEditor::Field f{.box = s.box, .choices = std::string(s.choices)};
        applyBox(f);
        return ui.choicesHeight(f, w, boxH) - boxH;
    };
    auto height = [&](std::size_t i) {
        const Setting& s = kSettings[i];
        if (s.name.empty()) return headH + (s.help.empty() ? 0.0f : headNoteH);
        const Setting* second = pair(i);
        return rowH + (second ? std::max(extra(s, pairW), extra(*second, pairW)) : extra(s, fieldW));
    };
    float total = noteH + static_cast<float>(others.size()) * otherH;
    for (std::size_t i = 0; i < kSettings.size(); ++i) {
        total += height(i);
        if (pair(i)) ++i;
    }
    float& scroll = d.settingsScroll;
    scroll -= ui.wheelOver(box) * rowH * 2;
    if (d.focus.empty())
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            switch (key.key) {
            case SDLK_PAGEUP: scroll -= box.h * 0.9f; return true;
            case SDLK_PAGEDOWN: scroll += box.h * 0.9f; return true;
            case SDLK_UP: scroll -= rowH; return true;
            case SDLK_DOWN: scroll += rowH; return true;
            default: return false;
            }
        });
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, total - box.h));

    const float fieldX = box.x + labelW, helpX = fieldX + fieldW + layout::settingHelpGap * k, helpW = std::max(0.0f, box.x + box.w - helpX);
    const DeveloperUi::Clip clipped(ui, box, true);
    float y = box.y - scroll;
    for (const ScriptError* e : others) {
        ui.text(std::format("main.vnscript line {}: {}", e->line, e->message), box.x, y + 4 * k, 14, kBad, box.w);
        y += otherH;
    }
    // A setting's control in `field` (buttons for its choices, else a box with its default in grey)
    // and its problem, if its line has one.
    auto control = [&](const Setting& s, const SDL_FRect& field) -> const ScriptError* {
        const auto written = editor.setting(s.name);
        const auto line = editor.settingLine(s.name);
        const ScriptError* problem = line ? d.problemAt(0, *line) : nullptr;
        if (!s.choices.empty() || !StoryEditor::kindOf(s.box).choices.empty()) { // (its kind's buttons, as every form draws them)
            StoryEditor::Field f{.name = std::string(s.name), .value = lower(written.value_or(std::string(s.standard))), .box = s.box, .choices = std::string(s.choices)};
            applyBox(f);
            float under = field.y;
            if (const auto word = ui.choices(field, f, under)) editor.setSetting(s.name, *word == s.standard ? "" : *word, false, {});
        } else {
            const std::string id = std::format("setting:{}", s.name);
            const bool percent = StoryEditor::kindOf(s.box).percent; // (shown and written as one, as everywhere)
            StoryEditor::Field f{.name = std::string(s.name), .value = written ? percent ? percentText(*written) : *written : std::string{}, .box = s.box,
                                 .hint = s.standard.empty() ? std::string("None") : std::string(s.standard), .suggest = std::string(s.suggest)};
            applyBox(f);
            if (const auto value = ui.pickField(id, field, f, 15)) {
                std::string v = *value;
                if (s.quoted) std::erase(v, '"');
                if (percent && !v.empty()) v = percentText(v);
                if (s.name == "resolution" && !d.sizeFrom) d.sizeFrom = written.value_or(std::string(s.standard)); // (see drawDeveloper)
                editor.setSetting(s.name, v, s.quoted, id + std::to_string(d.focusCount));
                if (s.name == "resolution") d.sizeKey = id + std::to_string(d.focusCount);
            }
        }
        return problem;
    };
    auto helpText = [&](const Setting& s, const ScriptError* problem, float x, float middle, float w) {
        ui.textAround(problem ? std::string_view(problem->message) : s.help, x, middle, 13, problem ? kBad : theme::dim, w);
    };
    for (std::size_t i = 0; i < kSettings.size(); ++i) {
        const Setting& s = kSettings[i];
        const Setting* second = pair(i);
        const float top = y;
        y += height(i);
        if (second) ++i;
        if (y < box.y || top > box.y + box.h) continue;
        if (s.name.empty()) {
            ui.text(s.label, box.x, top + 16 * k, layout::settingHeadText, kAccent);
            if (!s.help.empty()) ui.text(s.help, box.x, top + headH - 2 * k, 13, theme::dim, box.w);
            continue;
        }
        const float middle = top + rowH / 2;
        ui.textAround(s.label, box.x + 12 * k, middle, layout::settingLabelText, theme::text, labelW - 16 * k);
        if (!second) {
            const ScriptError* problem = control(s, {fieldX, top + 5 * k, fieldW, boxH});
            if (problem) draw::rect(r, {box.x, top + 6 * k, 4 * k, rowH - 12 * k}, kBad);
            helpText(s, problem, helpX, middle, helpW);
            continue;
        }
        // The pair: each with a box half as wide, the second's label between, and their help on two lines.
        const float lw2 = std::min(labelW, ui.width(second->label, 15) + 16 * k);
        const ScriptError* problem = control(s, {fieldX, top + 5 * k, pairW, boxH});
        const float x2 = fieldX + pairW + 12 * k;
        ui.textAround(second->label, x2, middle, layout::settingLabelText, theme::text, lw2 - 4 * k);
        const ScriptError* problem2 = control(*second, {x2 + lw2, top + 5 * k, pairW, boxH});
        if (problem || problem2) draw::rect(r, {box.x, top + 6 * k, 4 * k, rowH - 12 * k}, kBad);
        const float hx = x2 + lw2 + pairW + 16 * k, hw = std::max(0.0f, box.x + box.w - hx);
        helpText(s, problem, hx, middle - 8 * k, hw);
        helpText(*second, problem2, hx, middle + 8 * k, hw);
    }
}

} // namespace vn
