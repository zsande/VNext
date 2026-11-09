// The forms the pages show (a line's, a screen part's, a definition's): their boxes, the bar that formats text, As
// text, and listen buttons.
#include "DeveloperUi.hpp"

#include <cmath>
#include <cstdlib>
#include <tuple>

namespace vn {

void Developer::listenButton(DeveloperUi& ui, const SDL_FRect& at, const std::string& path, bool small) {
    const bool playing = engine.sound().listening() == path;
    tools.listenShown = tools.listenShown || playing;
    if (ui.button(at, small ? std::string_view{} : std::string_view(playing ? "Stop"_tr : "Play"_tr), playing, true, small ? layout::smallButtonText : layout::buttonText)) {
        if (playing) {
            engine.sound().stopListening();
        } else {
            engine.sound().listen(path);
            tools.listenShown = true;
        }
    }
    if (small && playing) { // (a square while it plays, a triangle to play it)
        const float side = std::round(at.h * 0.3f);
        draw::rect(ui.r, {std::round(at.x + (at.w - side) / 2), std::round(at.y + (at.h - side) / 2), side, side}, theme::text);
    } else if (small) {
        ui.triangle(std::round(at.x + at.w / 2) + ui.k, std::round(at.y + at.h / 2), 5 * ui.k, DeveloperUi::Pointing::Right, theme::text);
    }
}

float Developer::asTextButton(DeveloperUi& ui, float rightEdge, float y) {
    const float k = ui.k;
    const float toggleW = std::max(ui.buttonWidth("As a form"_tr, layout::buttonText, layout::toggleW), ui.buttonWidth("As text"_tr, layout::buttonText, layout::toggleW)); // (one size, either way)
    if (ui.button({rightEdge - toggleW, y, toggleW, layout::buttonH * k}, tools.asText ? "As a form"_tr : "As text"_tr, false, true, layout::buttonText)) {
        tools.asText = !tools.asText;
        tools.focus.clear();
    }
    return toggleW;
}

std::optional<std::string> Developer::drawRawLine(DeveloperUi& ui, const std::string& id, const std::string& line, float x, float w, float& y) {
    const float k = ui.k, boxH = layout::boxH * k, labelW = labelColumn(ui, std::round(w * layout::labelPart), w, {}, {"As the script has it"_tr});
    ui.textFitted("As the script has it"_tr, x, y + boxH / 2, layout::labelText, theme::dim, labelW - 8 * k);
    const auto text = ui.field(id, {x + labelW, y, w - labelW, boxH}, line, layout::boxText);
    y += boxH + layout::rowGap * k;
    if (!text || trim(*text).empty()) return std::nullopt;
    return std::string(trim(*text));
}

float Developer::labelColumn(DeveloperUi& ui, float least, float w, std::initializer_list<std::span<const StoryEditor::Field>> forms,
                             std::initializer_list<std::string_view> more) {
    const float k = ui.k;
    float need = 0.0f;
    for (const auto fields : forms) {
        bool in = false; // (under a group's heading, as drawForm() has them: from the first on)
        for (const StoryEditor::Field& f : fields) {
            if (f.hidden) continue;
            in = in || !f.group.empty();
            const std::string_view label = tr(f.label.empty() ? std::string_view(f.name) : std::string_view(f.label));
            need = std::max(need, ui.width(label, layout::labelText) + (in ? layout::groupIndent * k : 0.0f) + 8 * k);
        }
    }
    for (const std::string_view label : more) need = std::max(need, ui.width(label, layout::labelText) + 8 * k);
    return std::max(least, std::min(std::ceil(need), std::round(w * layout::labelMostPart)));
}

std::optional<Developer::FormEdit> Developer::drawForm(DeveloperUi& ui, const std::string& id, const std::vector<StoryEditor::Field>& given, float x,
                                                 float w, float labelW, float& y) {
    DeveloperTools& d = tools;
    std::vector<StoryEditor::Field> fields = given; // (each box as its kind has it; empty, showing what that is in grey: its default, else None)
    for (StoryEditor::Field& f : fields) {
        applyBox(f);
        if (f.hint.empty()) f.hint = "None"; // (a list of choices with none chosen too: several to pick, nothing picked)
    }
    const float k = ui.k, boxH = layout::boxH * k, gap = layout::rowGap * k;
    std::optional<FormEdit> out;
    std::string_view group; // (the heading the fields are under: every one's rows show)
    bool first = true;
    for (const StoryEditor::Field& f : fields) {
        if (f.hidden) continue;
        if (!f.group.empty() && f.group != group) {
            group = f.group;
            y += first ? 0.0f : layout::groupGap * k;
            ui.text(tr(group), x, y, layout::groupText, kAccent);
            y += layout::groupStep * k;
        }
        first = false;
        const float in = group.empty() ? 0.0f : layout::groupIndent * k;
        const std::string_view label = tr(f.label.empty() ? std::string_view(f.name) : std::string_view(f.label));
        // Its row: its label in the label column (or over the box, without one), then its box, or its
        // choices.
        if (labelW > 0.0f) {
            ui.textFitted(label, x + in, y + boxH / 2, layout::labelText, theme::dim, labelW - in - 8 * k);
        } else {
            ui.text(label, x, y, layout::labelText, theme::dim);
            y += layout::labelAboveH * k;
        }
        const SDL_FRect box{x + labelW, y, w - labelW, boxH};
        const std::string fid = std::format("{}:{}", id, f.name);
        d.keyLabel = label; // (what its box or buttons say for a screen reader: under this label)
        if (!f.choices.empty()) {
            if (const auto word = ui.choices(fid, box, f)) out = FormEdit{.name = f.name, .value = *word, .typed = false};
            d.keyLabel.clear();
            y += boxH + gap;
            continue;
        }
        // A box: typed into or picked from (a sound's, when its file is there, with a button to hear it at
        // its end), and while it's typed into, the buttons for what its words can be under it.
        SDL_FRect inner = box;
        std::string sound;
        if (StoryEditor::kindOf(f.box).listen)
            if (const std::string name = lower(trim(f.value)); !name.empty())
                if (const auto found = d.cachedFoundSound(engine.assets(), name)) {
                    sound = *found;
                    inner.w -= boxH + 4 * k;
                }
        if (const auto value = ui.pickField(fid, inner, f, layout::boxText)) out = FormEdit{.name = f.name, .value = *value, .typed = true};
        d.keyLabel.clear();
        if (!sound.empty()) listenButton(ui, {box.x + box.w - boxH, box.y, boxH, boxH}, sound, true);
        if (f.rich && d.focus == fid) {
            y += boxH + 4 * k;
            if (const auto text = drawFormatBar(ui, fid, box.x, box.w, y)) out = FormEdit{.name = f.name, .value = *text, .typed = true};
            y -= boxH;
        }
        y += boxH + gap;
    }
    return out;
}

namespace {

// A format bar button's words as what it does: bold, italic, underlined, crossed out, a colour to each letter,
// with a reading over them, as a tip shows, or as a variable in its braces (the others as they are: see
// formatPlayed).
RichText formatLook(std::string_view label, const Palette& palette) {
    RichText out{.text = std::string(tr(label)), .spans = {}};
    const std::size_t n = utf8::length(out.text);
    if (label == "Bold") out.spans.push_back({.begin = 0, .end = n, .style = TTF_STYLE_BOLD});
    else if (label == "Italic") out.spans.push_back({.begin = 0, .end = n, .style = TTF_STYLE_ITALIC});
    else if (label == "Underline") out.spans.push_back({.begin = 0, .end = n, .style = TTF_STYLE_UNDERLINE});
    else if (label == "Crossed out") out.spans.push_back({.begin = 0, .end = n, .style = TTF_STYLE_STRIKETHROUGH});
    else if (label == "Furigana") out.spans.push_back({.begin = 0, .end = n, .ruby = "ふりがな"});
    else if (label == "Tip") out.spans.push_back({.begin = 0, .end = n, .colour = parseColour("tip", &palette).value_or(SDL_Color{173, 162, 255, 255}), .style = TTF_STYLE_UNDERLINE});
    else if (label == "Variable") { // ({name}: its braces dimmer, as a box shows them)
        out.text = "{" + out.text + "}";
        out.spans.push_back({.begin = 0, .end = 1, .colour = theme::dim});
        out.spans.push_back({.begin = n + 1, .end = n + 2, .colour = theme::dim});
    } else if (label == "Colour") { // (around the colour wheel, a letter at a time)
        for (std::size_t i = 0; i < n; ++i) {
            const float hue = 360.0f * static_cast<float>(i) / static_cast<float>(std::max<std::size_t>(n, 1)), c = 0.55f, xx = c * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
            const auto [r, g, b] = hue < 60 ? std::tuple{c, xx, 0.0f} : hue < 120 ? std::tuple{xx, c, 0.0f} : hue < 180 ? std::tuple{0.0f, c, xx}
                                 : hue < 240 ? std::tuple{0.0f, xx, c} : hue < 300 ? std::tuple{xx, 0.0f, c} : std::tuple{c, 0.0f, xx};
            auto byte = [](float v) { return static_cast<Uint8>(std::lround((v + 0.45f) * 255.0f)); };
            out.spans.push_back({.begin = i, .end = i + 1, .colour = SDL_Color{byte(r), byte(g), byte(b), 255}});
        }
    }
    return out;
}

// How much of a format bar button's words show `t` seconds after it's pointed at: Speed types them fast, Pause
// stops halfway a moment, Appears shows them at once after a moment, No wait types them and starts again at
// once (the others: all of them).
std::size_t formatPlayed(std::string_view label, std::size_t n, float t) {
    const float cps = 12.0f, typed = static_cast<float>(n) / cps; // (letters a second, and how long all of them take)
    if (label == "Speed") return static_cast<std::size_t>(std::fmod(t, typed / 2.5f + 0.8f) * cps * 2.5f); // (fast, then a moment)
    if (label == "Pause") {
        const float half = typed / 2.0f, at = std::fmod(t, typed + 1.6f); // (half, a pause, the rest, a moment)
        return static_cast<std::size_t>((at < half ? at : at < half + 0.8f ? half : at - 0.8f) * cps);
    }
    if (label == "Appears") return std::fmod(t, 1.6f) < 0.6f ? 0 : n; // (all of it at once)
    if (label == "No wait") return static_cast<std::size_t>(std::fmod(t, typed) * cps);
    return n;
}

} // namespace

// Buttons that make the words chosen in the box typed into (or what's typed next) bold, italic,
// underlined, crossed out, coloured or typed at another speed, with furigana or a tip over them; or
// that put in a pause, a variable, how the line appears, or no wait at its end, in three rows as wide
// as the box, each saying it as what it does (formatLook, formatPlayed). Gives the new text.
std::optional<std::string> Developer::drawFormatBar(DeveloperUi& ui, const std::string& id, float x, float w, float& y) {
    DeveloperTools& d = tools;
    const float k = ui.k, h = layout::barButtonH * k, gap = layout::barButtonGap * k;
    static const std::array<std::string_view, 12> kLabels{"Bold"_text,     "Italic"_text, "Underline"_text, "Crossed out"_text, "Colour"_text,   "Speed"_text,
                                                          "Furigana"_text, "Tip"_text,    "Pause"_text,     "Variable"_text,    "Appears"_text, "No wait"_text};
    std::vector<SDL_FRect> places; // (the looks; colour, speed, a reading and a tip; what goes in a line)
    const float cell = (w - 3 * gap) / 4;
    for (std::size_t i = 0; i < kLabels.size(); ++i)
        places.push_back({std::round(static_cast<float>(i % 4) * (cell + gap)), static_cast<float>(i / 4) * (h + gap), std::round(cell), h});
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
        const RichText look = formatLook(label, engine.palette());
        const bool fits = ui.width(look.text, layout::smallButtonText) <= b.w - 2 * k; // (the room any button's words have; else its words alone, cut to fit, as any button's)
        const std::string_view words = fits ? std::string_view{} : tr(label);
        std::optional<std::string> picked;
        auto list = [&](const StoryEditor::Field& f) { picked = ui.listButton(bid, b, words, f); };
        bool clicked = false;
        if (label == "Colour") list({.name = "colour", .box = Box::Colour});
        else if (label == "Speed") list({.name = "speed", .box = Box::TextSpeed});
        else if (label == "Tip") list({.name = "tip", .box = Box::Record, .context = "tip"});
        else if (label == "Pause") list({.name = "pause", .box = Box::Pause});
        else if (label == "Variable") list({.name = "variable", .box = Box::Variable});
        else if (label == "Appears") list({.name = "appears", .box = Box::TextAppears, .choices = "letter word line"});
        else clicked = ui.button(b, words, false, true, layout::smallButtonText);
        if (fits) { // (pointed at, the ones that take time play it, from when it was first pointed at)
            if (!ui.over(b)) d.formatSince.erase(std::string(label));
            const float since = ui.over(b) ? engine.toolsTime() - d.formatSince.try_emplace(std::string(label), engine.toolsTime()).first->second : 1e6f;
            const std::size_t n = utf8::length(look.text);
            ui.richIn(look, b, layout::smallButtonText, theme::text, ui.over(b) ? formatPlayed(label, n, since) : TextBlock::kAll);
        }
        if (!clicked && !picked) continue;
        if (label == "Bold") wrap("{bold}", "{/bold}");
        else if (label == "Italic") wrap("{italic}", "{/italic}");
        else if (label == "Underline") wrap("{underline}", "{/underline}");
        else if (label == "Crossed out") wrap("{crossed_out}", "{/crossed_out}");
        else if (label == "Furigana") { // (the reading over the words chosen, to type over)
            const std::size_t from = chosen().first;
            wrap("{furigana reading}", "{/furigana}");
            d.anchor = from + 6;
            d.caret = from + 13;
        } else if (label == "No wait") {
            if (!d.buffer.ends_with("{no_wait}")) put(d.buffer.size(), "{no_wait}");
        } else if (label == "Colour") { // (from the wheel while it stays open: the tag around the words changes, not one more around them)
            const auto [from, to] = chosen();
            const std::string open = std::format("{{colour {}}}", *picked);
            const std::string_view before(d.buffer.data(), from), after(d.buffer.data() + to, d.buffer.size() - to);
            if (const auto tag = before.rfind("{colour "); tag != npos && from > 0 && before.find('}', tag) == from - 1 && after.starts_with("{/colour}")) {
                d.buffer.replace(tag, from - tag, open);
                d.anchor = tag + open.size();
                d.caret = d.anchor + (to - from);
            } else {
                wrap(open, "{/colour}");
            }
        }
        else if (label == "Speed") wrap(std::format("{{speed {}}}", *picked), "{/speed}");
        else if (label == "Tip") wrap(std::format("{{tip {}}}", *picked), "{/tip}");
        else if (label == "Pause") put(chosen().second, *picked == "click" ? std::string("{pause}") : std::format("{{pause {}}}", *picked));
        else if (label == "Variable") put(chosen().second, std::format("{{{}}}", *picked));
        else if (label == "Appears") put(0, std::format("{{appears {}}}", *picked));
        d.focus = id; // (still typed into)
        out = d.buffer;
    }
    y += places.back().y + places.back().h;
    // The word at the caret, when it isn't spelt right: what it may be instead (a click puts it in its
    // place), or Learn it (it's right).
    const std::string language = spellingLanguage(engine.gameSettings().language);
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
        std::vector<std::string_view> labels{"Spelling:"_text};
        for (const std::string& g : it->second) labels.emplace_back(g);
        labels.emplace_back("Learn it"_text);
        const auto row = ui.flow(labels, w, h, layout::barButtonGap * k, layout::smallButtonText);
        y += 4 * k;
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const SDL_FRect b{x + row[i].x, y + row[i].y, row[i].w, row[i].h};
            if (i == 0) {
                ui.textAround(tr(labels[0]), b.x, b.y + b.h / 2, layout::labelText, kBad);
                continue;
            }
            if (!ui.button(b, i + 1 == labels.size() ? tr(labels[i]) : labels[i], false, true, layout::smallButtonText)) continue;
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

} // namespace vn
