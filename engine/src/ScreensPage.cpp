// The Screens page: the game's screens, each one's parts, a preview to click and drag them in, a part's form, and
// the kinds of parts to add.
#include "DeveloperUi.hpp"
#include "ReadyMadeScreens.hpp"

#include <array>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <tuple>

namespace vn {

// The Screens page's sample: the story played silently from its start to its first line (or its first
// choice), for the screens to be seen over.
void Developer::makeSample(bool choice) {
    DeveloperTools& d = tools;
    d.sample.reset();
    if (!d.script || !d.script->errors.empty()) return;
    const Script& s = *d.script;
    const auto start = s.findScene("start");
    if (!start) return;
    d.sample = silentGame(s);
    d.sample->start(*start);
    d.sample->playTo(SIZE_MAX, [&](std::size_t pc) {
        return choice ? std::holds_alternative<cmd::Menu>(s.code[pc].command) : std::holds_alternative<cmd::Say>(s.code[pc].command);
    });
}

namespace {

// What a new part of each kind starts as (for a 1280 x 720 game): where and how big the ready-made
// screens have it (the textbox's parts where the built-in textbox has them, lists in the menus'
// list area, small parts in the menus' button column, pages where the question's is), with the
// look's colours by their names and nothing a part doesn't need.
struct PartStart {
    ToolsText kind; // (what its button says)
    std::string_view line;
};
const PartStart kPartBasics[]{
    {"Container", "container x=310 y=210 width=660 height=300"},
    {"Text", "text \"Text\" x=460 y=345 width=360"},
    {"Button", "button \"Button\" x=460 y=334 width=360 height=52"},
    {"Image", "image \"IMAGE\" x=0 y=0 width=1280 height=720 fit=cover"}, // (a picture, a colour, or a movie, playing)
};
const PartStart kPartValues[]{
    {"Slider", "slider x=460 y=344 width=360 height=32 setting=master_volume"}, // (or one showing a value: value=)
    {"Input", "input x=460 y=334 width=360 height=52 variable=name"},
    {"Control", "control next_line x=460 y=344 width=360 height=32 waiting=\"Press a key (Esc keeps it)\""},
};
const PartStart kPartTextbox[]{
    {"Dialogue", "dialogue x=72 y=528 width=1136"},
    {"Face", "face x=64 y=452 width=190 height=232"},
};
// (a list of things: the parts put in it show once for each, in the menus' list area; the flowchart there too)
const PartStart kPartLists[]{
    {"List", "container list=history x=60 y=92 width=1160 height=540 spacing_down=10 scroll=yes scrollbar=yes"},
    {"Flowchart", "flowchart x=60 y=92 width=1160 height=540 unreached_style=flowchart_unreached ending_style=flowchart_ending choice_style=flowchart_choice scrollbar=yes confirm=\"Go back to this point? Progress since your last save will be lost.\""},
};
// (where the ready-made phone has them: its parts in its apps' place)
const PartStart kPartPhone[]{
    {"App", "container app=new_app title=\"New app\" x=962 y=46 width=276 height=394"},
};
// New part's groups, as the Story page's New line has its lines.
const std::pair<ToolsText, std::span<const PartStart>> kParts[]{
    {"Basics", kPartBasics}, {"Values", kPartValues}, {"Textbox", kPartTextbox}, {"Lists", kPartLists}, {"Phone", kPartPhone}};

// Each kind of part's own settings, in the form's order (a list's items' size and spacing, its scrollbar
// and how it glides come after them: the ones it takes, partTakes).
struct PartKind {
    std::string_view kind, keys;
};

constexpr PartKind kPartKinds[]{
    {"container", "fit stack scroll scrollbar scrollbar_thickness glide glide_speed glide_distance close_at_end edges fading_edges list mode position item_animation columns item_width item_height spacing spacing_across spacing_down app title icon count"},
    {"text", "width_at_most lines"}, {"image", "bottom fit flip repeat scroll scroll_round loop volume"}, {"button", "action confirm click_animation keys clickable drag drop"},
    {"face", "fit flip"}, {"control", "slot waiting"}, {"input", "variable most_letters placeholder action confirm clickable"},
    {"slider", "variable setting value minimum maximum step vertical clickable action track fill knob"},
    {"flowchart", "confirm line ending_style unreached_style choice_style"},
};

// The heading a kind of part's own settings are under (a button's: by what they're for).
ToolsGroup partGroup(std::string_view kind, std::string_view key) {
    if (kind == "button") return key == "drag" || key == "drop" ? "Dragging and dropping"_group : "Clicking"_group;
    if (kind == "container") { // (its parts, a list's items, one of the phone's apps, then how it scrolls)
        static constexpr std::string_view kList[]{"list", "mode", "position", "item_animation", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down"};
        static constexpr std::string_view kApp[]{"app", "title", "icon", "count"};
        if (key == "fit" || key == "stack") return "Parts"_group;
        if (std::ranges::contains(kList, key)) return "List"_group;
        if (std::ranges::contains(kApp, key)) return "App"_group;
        return key == "edges" || key == "fading_edges" ? "Edges"_group : "Scrolling"_group;
    }
    if (kind == "text") return "Words"_group;
    if (kind == "slider" && (key == "track" || key == "fill" || key == "knob")) return "Colours"_group;
    if (kind == "image" && (key == "loop" || key == "volume")) return "Playing"_group; // (a movie's)
    static const std::pair<std::string_view, ToolsGroup> groups[]{
        {"image", "Image"_group}, {"face", "Face"_group}, {"control", "Key"_group}, {"input", "Typing"_group}, {"slider", "Value"_group}, {"flowchart", "Flowchart"_group}};
    const auto it = std::ranges::find(groups, kind, &std::pair<std::string_view, ToolsGroup>::first);
    return it != std::end(groups) ? it->second : "Settings"_group;
}

constexpr std::string_view kListKeys[]{"columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down"}; // (a list's: its items' size and spacing)
constexpr std::string_view kScrollbarKeys[]{"scrollbar", "scrollbar_thickness"};          // (one that scrolls: its scrollbar)
constexpr std::string_view kGlideKeys[]{"glide", "glide_speed", "glide_distance"};        // (...and how it glides on after a drag)
// A part's settings: its own, a list's, its style and look (as a style's), then when it shows.
std::vector<PartOption> partOptions(std::string_view kind) {
    std::vector<PartOption> out;
    std::string_view last; // (each heading once: on the first of its settings)
    auto option = [&](std::string_view key) {
        const auto known = std::ranges::find(kPartOptions, key, &PartOption::key);
        if (known == std::end(kPartOptions)) return; // (one with no row: bottom=, which the image's box shows as a gradient)
        PartOption o = *known;
        o.group = partGroup(kind, key);
        if (std::ranges::contains(kListKeys, key) && kind != "container" && partTakes(kind, "item_height")) o.group = "Item size"_group; // (else its gap: with its own)
        if ((std::ranges::contains(kScrollbarKeys, key) || std::ranges::contains(kGlideKeys, key)) && kind != "container") o.group = "Scrolling"_group;
        if (o.group == last) o.group = {};
        else last = o.group;
        if (key == "fit" && kind == "image") o.choices = "stretch contain cover original"_choices;
        if (key == "fit" && kind == "container") o.label = "Fit"_text, o.box = Box::ContainerFit, o.choices = "box parts"_choices;
        if (key == "scroll" && kind == "container") o.label = "Scrolls"_text, o.box = Box::YesNo, o.choices = "no yes"_choices;
        if (key == "position" && kind == "container") o.choices = "top centre bottom"_choices, o.labels = "The top|The centre|The bottom, and its end as more comes"_labels;
        if (key == "confirm" && kind == "flowchart") o.label = "Confirm going back"_text;
        if (key == "action" && kind == "input") o.label = "Action on Enter"_text;
        if (key == "action" && kind == "slider") o.label = "Action on change"_text;
        out.push_back(o);
    };
    if (const auto k = std::ranges::find(kPartKinds, kind, &PartKind::kind); k != std::end(kPartKinds)) {
        for (const std::string_view key : wordsOf(k->keys)) option(key);
        if (kind != "container") // (a container's are among its own, in its order)
            for (const auto& keys : {std::span<const std::string_view>(kListKeys), std::span<const std::string_view>(kScrollbarKeys), std::span<const std::string_view>(kGlideKeys)})
                for (const std::string_view key : keys)
                    if (partTakes(kind, key)) option(key);
    }
    // Its look: its style's alone (Library > Styles); a button, a box, an image or a text looks as its style's
    // Chosen says while what Chosen when checks is true.
    out.push_back({.key = "style", .label = "Style"_text, .box = Box::Style, .hint = "Default"_hint, .group = "Look"_group});
    static constexpr std::string_view kChoosable[]{"button", "container", "image", "text"};
    if (std::ranges::contains(kChoosable, kind)) {
        option("chosen");
        out.back().group = {};
    }
    out.push_back({.key = "when", .label = "Shown when"_text, .box = Box::Condition, .hint = "Always"_hint, .is = 'c'}); // (still its look: when it's there, and how it moves)
    out.push_back({.key = "animation", .label = "Animation"_text, .box = Box::Animation, .hint = "None"_hint});
    out.push_back({.key = "pointed_animation", .label = "Animation when pointed at"_text, .box = Box::Animation, .hint = "None"_hint});
    out.push_back({.key = "message_animation", .label = "Animation on a new message"_text, .box = Box::Animation, .hint = "None"_hint});
    return out;
}

// An image that depends on a condition: {if(CONDITION, 'YES', 'NO')} (written so in the script; in a form,
// the condition as its checks, as every condition's are, and an image for each).
struct ImageBy {
    std::string condition, yes, no;
};

std::optional<ImageBy> imageByOf(std::string_view v) {
    v = trim(v);
    if (!v.starts_with("{if(") || !v.ends_with(")}")) return std::nullopt;
    const Parts args = splitTop(v.substr(4, v.size() - 6), {","});
    if (args.parts.size() != 3) return std::nullopt;
    auto bare = [](std::string_view a) {
        a = trim(a);
        return std::string(a.size() >= 2 && (a.front() == '\'' || a.front() == '"') && a.back() == a.front() ? a.substr(1, a.size() - 2) : a);
    };
    return ImageBy{std::string(trim(args.parts[0])), bare(args.parts[1]), bare(args.parts[2])};
}

// An image box's fields: one image, or (after "depends on a variable" in its list) what it depends on,
// as a condition's checks (a yes or no variable, or "language is English"), then an image for yes and one
// for no. Named `name` (the image, or the one for yes), `name by` (and its checks': `name by is`...) and
// `name no`. Each box is an image's (an image, or a colour).
void addImageFields(std::vector<StoryEditor::Field>& out, const std::string& name, const std::string& label, std::string_view hint, std::string_view value,
                      const std::vector<std::string>& variables) {
    const auto by = imageByOf(value);
    if (!by) {
        out.push_back({.name = name, .value = std::string(value), .label = label, .box = Box::Image, .hint = std::string(hint), .suggest = std::string(kImageBySuggest)});
        return;
    }
    addCheckFields(out, name + " by", "{} depends on"_tr(tr(label)), "None", by->condition, Box::Condition, variables); // (in the tools' language: made of the label's)
    out.push_back({.name = name, .value = by->yes, .label = "{} if yes"_tr(tr(label)), .box = Box::Image, .hint = std::string(hint)});
    out.push_back({.name = name + " no", .value = by->no, .label = "{} if no"_tr(tr(label)), .box = Box::Image, .hint = std::string(hint)});
}

// ...and back: the image, or {if(CONDITION, 'YES', 'NO')} (back to one image when what it depends on is taken away).
std::string imageFrom(const StoryEditor& editor, const std::vector<StoryEditor::Field>& fields, const std::string& name) {
    std::string v(trim(fieldValue(fields, name)));
    if (hasField(fields, name + " by")) {
        const std::string by = conditionFrom(fields, name + " by", editor.variables());
        if (by.empty()) return v;
        return std::format("{{if({}, '{}', '{}')}}", by, v, trim(fieldValue(fields, name + " no")));
    }
    if (v != kImageBy) return v;
    std::string guess; // (the first yes or no variable it could be)
    for (const StoryEditor::Definition& def : editor.definitions("variable"))
        if (const auto w = rawWords(afterFirst(editor.linesOf(def).front())); w.size() > 1 && parseBool(unquote(w[1]))) {
            guess = w[0];
            break;
        }
    if (guess.empty() && !editor.variables().empty()) guess = editor.variables().front();
    return guess.empty() ? std::string{} : std::format("{{if({}, '', '')}}", guess);
}

// A screen's or a part's settings as fields, and back.
std::vector<StoryEditor::Field> optionFields(const StoryEditor& editor, const StoryEditor::Part& p, std::span<const PartOption> options) {
    std::vector<StoryEditor::Field> out;
    for (const PartOption& o : options) {
        const std::string key(o.key), v = p.option(o.key);
        const std::size_t first = out.size();
        std::string hint(o.hint);
        if (key == "chosen" && p.kind == "button") { // (pressed, it looks chosen; one that sets something to a value, while it's so)
            const auto w = rawWords(unquote(p.option("action")));
            hint = w.size() == 4 && lower(w[0]) == "set" && lower(w[2]) == "to" ? "While {} is {}"_tr(w[1], unquote(w[3]))
                 : w.size() == 3 && lower(w[0]) == "setting"                    ? "While {} is {}"_tr(w[1], w[2])
                                                                                : "While pressed"_tr();
        }
        if (o.is == 'a') addActionFields(out, key, o.label, o.hint, v, editor.variables());
        else if (o.is == 'c') addCheckFields(out, key, o.label, hint, v, o.box, editor.variables());
        else if (o.box == Box::Image) addImageFields(out, key, std::string(o.label), o.hint, v, editor.variables());
        else out.push_back({.name = key, .value = o.choices.empty() ? (StoryEditor::kindOf(o.box).percent ? percentText(v) : v) : v.empty() ? std::string(wordsOf(o.choices).front()) : lower(v),
                            .label = std::string(o.label), .box = o.box, .choices = std::string(o.choices), .hint = std::string(o.hint), .labels = std::string(o.labels),
                            .multi = o.box == Box::Key, .suggest = std::string(o.suggest), // (a button's keys: several, as a control's)
                            .usual = o.choices.empty() ? std::string{} : std::string(wordsOf(o.choices).front())});
        if (first < out.size()) out[first].group = o.group;
    }
    return out;
}

StoryEditor::Part withOptions(const StoryEditor& editor, StoryEditor::Part p, const std::vector<StoryEditor::Field>& fields, std::span<const PartOption> options) {
    for (const PartOption& o : options) {
        const std::string key(o.key);
        if (o.is == 'a') {
            p.set(o.key, actionsFrom(fields, key, editor.variables()));
        } else if (o.is == 'c') {
            p.set(o.key, conditionFrom(fields, key, editor.variables()));
        } else if (o.box == Box::Image && hasField(fields, key)) {
            p.set(o.key, imageFrom(editor, fields, key));
        } else if (const auto f = std::ranges::find(fields, key, &StoryEditor::Field::name); f != fields.end()) {
            const std::string v(trim(f->value));
            p.set(o.key, !o.choices.empty() && v == f->usual ? std::string{} : StoryEditor::kindOf(o.box).percent ? percentText(v) : v); // (what it does without one: none)
        }
    }
    return p;
}

// What the editor calls a part: its name (name=), else its kind and text.
// (with `palette`: an image part's colour by what it's called, never its code)
std::string partTitle(const StoryEditor::Part& p, const Palette* palette = nullptr) {
    if (std::string name = p.option("name"); !name.empty()) return name;
    if (p.kind == "container" && !p.option("app").empty()) return std::format("container  app {}", p.option("app"));
    if (p.kind == "container" && !p.option("list").empty()) return std::format("container  list {}", p.option("list"));
    if (p.text.empty()) return p.kind;
    if (palette && p.kind == "image" && parseColour(p.text, palette)) return std::format("{}  {}", p.kind, colourWords(p.text, palette));
    return std::format("{}  {}", p.kind, p.text);
}

// A name as typed, as name= keeps it (without double quotes: one would end it).
std::string partName(std::string_view typed) {
    std::string out(trim(typed));
    std::erase(out, '"');
    return out;
}

// What a part's text (or word) is, by its kind; empty for kinds without one.
std::string_view textLabel(std::string_view kind) {
    if (kind == "text") return "Text"_text;
    if (kind == "button") return "Text"_text;
    if (kind == "image") return "Image, colour or movie"_text;
    if (kind == "control") return "Control"_text;
    return {};
}

} // namespace

// The Screens page as it is this frame: the screen open (and the screens to pick from), its parts and the
// one chosen, and the screen as read. Its page parts are drawn by drawScreensList, drawPartsList, drawPreview, drawNewPart
// and drawScreensForm, each in the room drawPageParts gives it.
struct Developer::ScreensFrame {
    struct Entry {
        std::string name;
        std::optional<std::size_t> index; // in editor.screens() (none: built in)
    };
    DeveloperUi& ui;
    DeveloperTools& d;
    const std::vector<Entry>& entries;
    const Entry& entry;
    const Entry* chosen; // (the entry open: entry's place in entries)
    const std::optional<StoryEditor::Screen>& screen; // (none: built in)
    const std::vector<StoryEditor::Part>& parts;
    const StoryEditor::Part* selected;
    ScreenDef* def;
    const std::vector<std::optional<std::size_t>>& elementByPart;
    SDL_FRect middle, right; // the room of the part being drawn: the screen's (or New part's), and the form's
    // New part's buttons (the kinds of parts, in groups), and what Goes in (at the right of its title, with a part
    // chosen) calls the part chosen.
    std::vector<DeveloperUi::ButtonGroup> newParts{};
    std::string intoName{};
    // What the screen's column found, for the form: the parts chosen (in the script's order), and
    // whether the screen can be changed here (the game's own, read without mistakes).
    std::vector<std::size_t> chosenLines{};
    bool editable = false;
    // The screen as the middle column shows it: where (its pixels times `scale`), the mouse over it (in
    // its pixels), the chosen part's element with its handle (that resizes it), and the one pointed at.
    SDL_FRect shown{};
    float scale = 1.0f, mx = 0.0f, my = 0.0f;
    std::optional<std::size_t> selectedElement{}, hovered{};
    SDL_FRect handle{};

    // A part chosen (or none): the form shows it, and what was chosen with it isn't any more.
    void choose(std::optional<std::size_t> part) const {
        if (d.part != part) d.formScroll = 0.0f;
        d.part = part;
        d.moreParts.clear();
        d.focus.clear();
        d.armed.clear();
        d.asText = false;
        d.drag.reset();
    }
    [[nodiscard]] SDL_FRect onScreen(const SDL_FRect& b) const { return {shown.x + b.x * scale, shown.y + b.y * scale, b.w * scale, b.h * scale}; }
    [[nodiscard]] bool isChosen(std::size_t line) const { return d.part == line || std::ranges::contains(d.moreParts, line); }
    // The chosen parts that aren't in another chosen one (the parts in them go with them).
    [[nodiscard]] std::vector<std::size_t> outerLines() const {
        std::vector<std::size_t> out;
        for (const StoryEditor::Part& p : parts)
            if (isChosen(p.line) && std::ranges::none_of(parts, [&](const StoryEditor::Part& q) { return isChosen(q.line) && q.line < p.line && p.line < q.end; }))
                out.push_back(p.line);
        return out;
    }
    // The screen's element a part's line made, if it's there.
    [[nodiscard]] std::optional<std::size_t> elementOf(std::size_t line) const {
        if (!def || !screen) return std::nullopt;
        for (std::size_t i = 0; i < def->elements.size(); ++i)
            if (def->elements[i].line == static_cast<int>(line) + 1 && d.script->files[def->elements[i].file] == d.editor.path(screen->file)) return i;
        return std::nullopt;
    }
};

// The Screens page: the game's screens on the left (and the built-in ones), the one open in the
// middle as the game draws it (a click chooses a part, dragging moves it, its corner resizes it),
// and the chosen part's settings on the right (or the screen's own).
void Developer::drawScreens(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    using Part = StoryEditor::Part;
    using Screen = StoryEditor::Screen;

    // The screens: the game's, then the built-in textbox, captions and keyboard, each if it hasn't made it its own (every
    // game has them; the choice menu and the Saved notice are built in too, but only listed once they're the game's: + New
    // screen has them).
    std::vector<ScreensFrame::Entry> entries;
    entries.reserve(editor.screens().size() + 3);
    for (std::size_t i = 0; i < editor.screens().size(); ++i) entries.push_back({editor.screens()[i].name, i});
    for (const std::string_view always : {"textbox", "captions", "keyboard"})
        if (std::ranges::none_of(entries, [&](const ScreensFrame::Entry& e) { return lower(e.name) == always; })) entries.push_back({std::string(always), std::nullopt});
    auto chosen = std::ranges::find_if(entries, [&](const ScreensFrame::Entry& e) { return sameName(e.name, d.screenName); });
    if (chosen == entries.end()) {
        chosen = entries.begin();
        d.screenName = chosen->name;
        d.part.reset();
    }
    const ScreensFrame::Entry entry = *chosen;
    const std::optional<Screen> screen = entry.index ? std::optional(editor.screens()[*entry.index]) : std::nullopt;
    const std::vector<Part> parts = screen ? editor.parts(*screen) : std::vector<Part>{};
    const Part* selected = nullptr;
    if (d.part) {
        const auto it = std::ranges::find(parts, *d.part, &Part::line);
        if (it != parts.end()) selected = &*it;
        else d.part.reset();
    }

    // The screen as read (with its parts' places), and a view of it; the textboxes in it show the
    // story's first line (or choice).
    ScreenDef* def = nullptr;
    if (d.script)
        if (const auto it = d.script->screens.find(lower(entry.name)); it != d.script->screens.end()) def = &it->second;
    if (!def) d.screenView.reset();
    else if (!d.screenView || &d.screenView->def() != def) {
        d.screenView = std::make_unique<ScreenView>(*def, engine, true);
        d.sampleMade = false;
    }
    if (def && !d.sampleMade) {
        d.sampleMade = true;
        makeSample(std::ranges::any_of(def->elements, [](const Element& el) { return el.kind == ElementKind::Container && el.option("list") == "options"; }));
    }
    std::vector<std::optional<std::size_t>> elementByPart(parts.size()); // (found once a frame, not for each row of the list)

    auto [left, middle, right] = ui.columns(area);

    ScreensFrame f{.ui = ui, .d = d, .entries = entries, .entry = entry, .chosen = &*chosen, .screen = screen, .parts = parts, .selected = selected, .def = def,
                         .elementByPart = elementByPart, .middle = middle, .right = right};
    for (std::size_t i = 0; i < parts.size(); ++i) elementByPart[i] = f.elementOf(parts[i].line);
    // What the parts share, found before any of them is drawn (any may be drawn first): New part's buttons (with a
    // part chosen, where a new one goes: in it if it's a container, else on the screen), whether the screen can be
    // changed here, the parts chosen and the chosen one's element.
    if (d.addIntoOf != d.part) { // (another part chosen)
        d.addIntoOf = d.part;
        const auto now = d.part ? std::ranges::find(parts, *d.part, &Part::line) : parts.end(); // (chosen just now: not `selected` yet)
        d.addInto = now != parts.end() && now->kind == "container";
    }
    if (selected) f.intoName = partTitle(*selected, &engine.palette()); // (its button cuts it to fit)
    for (const auto& [name, starts] : kParts) {
        DeveloperUi::ButtonGroup& g = f.newParts.emplace_back(DeveloperUi::ButtonGroup{.name = tr(name), .labels = {}});
        for (const PartStart& start : starts) g.labels.push_back(tr(start.kind));
    }
    f.editable = screen && def && d.screenView;
    f.selectedElement = selected ? f.elementOf(selected->line) : std::nullopt;
    for (const Part& p : parts) // (in the script's order)
        if (f.isChosen(p.line)) f.chosenLines.push_back(p.line);

    // Where the page puts them, side by side: Screens over Parts; the screen over New part (as tall as its buttons: the
    // screen has the rest of the column); and the form. The screen is as big as the column's height lets it be: as wide as
    // that takes, from the form (which keeps layout::screensFormPart of its width, and its least).
    const SDL_FPoint chrome = pagePartChrome(k);
    const float half = std::round(left.h * 0.42f), headH = layout::previewHeadH * k;
    auto newPartH = [&](float w) { return chrome.y + headH + ui.newButtonsH(f.newParts, w - chrome.x); }; // (Goes in at the right of its title)
    const float screenH = std::max(0.0f, middle.h - chrome.y - headH - newPartH(middle.w)), fillW = chrome.x + engine.canvas().w * screenH / engine.canvas().h;
    const float formLeast = std::min(right.w, std::max(layout::formLeast * k, std::round(right.w * layout::screensFormPart)));
    middle.w = std::round(std::clamp(fillW, middle.w, middle.w + right.w - formLeast));
    right = {middle.x + middle.w, right.y, area.x + area.w - middle.x - middle.w, right.h};
    const float newH = std::min(middle.h, newPartH(middle.w)); // (its rows as wide as the column is now)
    std::array pageParts{
        listPagePart(ui, "screens", "Screens"_tr, {left.x, left.y, left.w, half}, 1, [&](const ListRoom& at) { return drawScreensList(f, at); }),
        listPagePart(ui, "parts", "Parts"_tr, {left.x, left.y + half, left.w, left.h - half}, 0, [&](const ListRoom& at) { return drawPartsList(f, at); }),
        PagePart{.id = "preview", .heading = "Preview"_tr, .at = {middle.x, middle.y, middle.w, middle.h - newH}, .draw = [&](const SDL_FRect& box) {
                  f.middle = box;
                  return drawPreview(f);
              }},
        PagePart{.id = "new_part",
                 .heading = "New part"_tr,
                 .at = {middle.x, middle.y + middle.h - newH, middle.w, newH},
                 .draw = [&](const SDL_FRect& box) {
                     f.middle = box;
                     return drawNewPart(f);
                 },
                 .least = {layout::pagePartButtonsW * k, 0.0f},
                 .tallAt = [&](float w) { return ui.newButtonsH(f.newParts, w); },
                 .head = [&](PreviewHead& row) { return drawGoesIn(f, row), false; }},
        formPagePart(ui, "form", "Form"_tr, right, true, [&](const SDL_FRect& box) {
            f.right = box;
            return drawScreensForm(f), false;
        })};
    drawPageParts(ui, "screens", pageParts); // (a screen or part changed: the rest are drawn next frame)
}

// The screens (the game's, then the built-in ones), and + New screen (true: changed, the rest next frame).
bool Developer::drawScreensList(const ScreensFrame& f, const ListRoom& at) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    using Screen = StoryEditor::Screen;
    const auto& entries = f.entries;
    const auto* chosen = f.chosen;
    const SDL_FRect& screensBox = at.list;
    // A click opens a screen; the game's own can be renamed with a double click (where it's opened
    // follows) and deleted with their x.
    using ListKind = DeveloperUi::ListEvent::Kind;
    auto own = [&](std::size_t i) { return entries[i].index.has_value(); };
    const auto event = ui.editList("screens", screensBox, d.screensScroll,
                                   {.count = entries.size(),
                                    .label = [&](std::size_t i) { return own(i) ? entries[i].name : "{} (built in)"_tr(entries[i].name); },
                                    .chosen = [&](std::size_t i) { return &entries[i] == &*chosen && !d.picking; },
                                    .wrong =
                                        [&](std::size_t i) {
                                            if (!own(i)) return false;
                                            const Screen& s = editor.screens()[*entries[i].index];
                                            return d.problemWithin(s.file, s.line, s.end);
                                        },
                                    .faint = [&](std::size_t i) { return !own(i); },
                                    .renames = own,
                                    .deletes = own,
                                    .moves = own,
                                    .drops =
                                        [&](std::size_t item, std::size_t slot) { // among the game's screens in its file
                                            const std::size_t file = editor.screens()[*entries[item].index].file;
                                            auto in = [&](std::size_t j) { return j < entries.size() && own(j) && editor.screens()[*entries[j].index].file == file; };
                                            return in(slot) || (slot > 0 && in(slot - 1));
                                        }});
    switch (event.kind) {
    case ListKind::Chosen:
        if (lower(d.screenName) != lower(entries[event.item].name)) d.partsScroll = 0.0f;
        d.screenName = entries[event.item].name;
        d.picking = false;
        f.choose({});
        return true;
    case ListKind::Renamed: {
        if (trim(event.name).empty()) return true; // (emptied: it keeps its name)
        const bool ok = editor.renameScreen(*entries[event.item].index, event.name, std::format("screen-name:{}:{}", event.item, d.focusCount));
        if (ok && lower(d.screenName) == lower(entries[event.item].name)) d.screenName = event.name;
        d.saidRenamed(ok, "A screen's name is one word (letters, numbers, _ and -) that no other screen has."_tr);
        return true;
    }
    case ListKind::Deleted:
        editor.removeScreen(*entries[event.item].index);
        if (lower(d.screenName) == lower(entries[event.item].name)) d.screenName.clear();
        f.choose({});
        return true;
    case ListKind::Moved:
        if (editor.moveScreen(*entries[event.item].index, event.to)) f.choose({});
        return true;
    case ListKind::None:
        break;
    }
    if (ui.button(at.rows[0], "+ New screen"_tr, d.picking, true, layout::buttonText)) {
        d.picking = !d.picking;
        d.formScroll = 0.0f;
        f.choose({});
    }
    return false;
}

// The open screen's parts, by their names (else by kind and text): a click chooses one, a double click renames it,
// its x deletes it, and dragging one changes which are drawn over which, and (dragged right or left) puts it in the
// part above it, or out of the one it's in (true: changed, the rest next frame). New part adds one (in the part
// chosen, or the screen).
bool Developer::drawPartsList(const ScreensFrame& f, const ListRoom& at) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    using Part = StoryEditor::Part;
    using ListKind = DeveloperUi::ListEvent::Kind;
    const auto& screen = f.screen;
    const auto& parts = f.parts;
    const auto& elementByPart = f.elementByPart;
    const SDL_FRect& partsBox = at.list;
    const float inStep = layout::listIndent * k; // (each part it's in, as the Story's lines are)
    const auto partEvent = ui.editList("parts", partsBox, d.partsScroll,
                                       {.count = parts.size(),
                                        .label = [&](std::size_t i) { return partTitle(parts[i]); },
                                        .name = [&](std::size_t i) { return parts[i].option("name"); },
                                        .chosen = [&](std::size_t i) { return d.part == parts[i].line || std::ranges::contains(d.moreParts, parts[i].line); },
                                        .wrong = [&](std::size_t i) { return d.problemAt(screen->file, parts[i].line) != nullptr; },
                                        .faint =
                                            [&](std::size_t i) {
                                                const auto el = elementByPart[i];
                                                return !el || !d.screenView || !d.screenView->drawnPart(*el);
                                            },
                                        .renames = [](std::size_t) { return true; },
                                        .deletes = [](std::size_t) { return true; },
                                        .moves = [](std::size_t) { return true; },
                                        .drops = {},
                                        .place = // (not into itself; as far in as the part above it and one more, as far out as the one under it)
                                            [&](std::size_t item, std::size_t slot, float dx) -> std::optional<int> {
                                                std::size_t after = item + 1; // (past the parts in it)
                                                while (after < parts.size() && parts[after].line < parts[item].end) ++after;
                                                if (slot > item && slot < after) return std::nullopt;
                                                const bool here = slot == item || slot == after; // (the parts around it, but not its own)
                                                const std::size_t up = here ? item : slot, down = here ? after : slot;
                                                const Part* above = up > 0 ? &parts[up - 1] : nullptr;
                                                const Part* below = down < parts.size() ? &parts[down] : nullptr;
                                                const int most = above ? above->depth + 1 : 0, least = std::min(below ? below->depth : 0, most);
                                                const int want = std::clamp(parts[item].depth + static_cast<int>(std::lround(dx / (inStep * 2))), least, most);
                                                if (here && want == parts[item].depth) return std::nullopt;
                                                return want;
                                            },
                                        .indentStep = inStep,
                                        .indent = [&](std::size_t i) { return static_cast<float>(parts[i].depth) * inStep; },
                                        .paint = // (a named one: its kind after its name, in grey; an image part's colour: a square and its name, as its box shows it)
                                            [&](std::size_t i, const SDL_FRect& row) {
                                                const auto el = elementByPart[i];
                                                const bool faint = !el || !d.screenView || !d.screenView->drawnPart(*el);
                                                const bool named = !parts[i].option("name").empty();
                                                const auto colour = !named && parts[i].kind == "image" ? parseColour(parts[i].text, &engine.palette()) : std::nullopt;
                                                const std::string title = colour ? parts[i].kind : partTitle(parts[i]);
                                                float x = row.x + 10 * k;
                                                const float mid = row.y + row.h / 2;
                                                const SDL_Color shade = faint ? theme::dim : theme::text;
                                                const float end = row.x + row.w - layout::listPad * k; // (each cut to fit: the title leaves what's after it its room, up to half)
                                                const std::string after = colour ? colourWords(parts[i].text, &engine.palette()) : named ? parts[i].kind : std::string{};
                                                const float afterW = after.empty() ? 0.0f : (colour ? (layout::swatch + 8) * k : 0.0f) + ui.width(after, colour ? layout::listText : layout::smallText) + 8 * k;
                                                x += ui.textFitted(title, x, mid, layout::listText, shade, std::max(end - x - afterW, (end - x) / 2)) + 8 * k;
                                                if (colour) {
                                                    const auto grad = parseGradient(parts[i].text, &engine.palette());
                                                    const float sx = ui.swatch(x, mid, *colour, false, grad ? std::optional(grad->second) : std::nullopt);
                                                    ui.textFitted(after, sx, mid, layout::listText, shade, end - sx);
                                                }
                                                else if (named) ui.textFitted(after, x, mid, layout::smallText, theme::dim, end - x);
                                            }});
    switch (partEvent.kind) {
    case ListKind::Chosen:
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) && d.part && *d.part != parts[partEvent.item].line) { // (one more chosen, or one less)
            if (const auto it = std::ranges::find(d.moreParts, parts[partEvent.item].line); it != d.moreParts.end()) d.moreParts.erase(it);
            else d.moreParts.push_back(parts[partEvent.item].line);
            d.listDrag.reset();
            break;
        }
        f.choose(parts[partEvent.item].line);
        break;
    case ListKind::Deleted:
        editor.removePart(*screen, parts[partEvent.item]);
        f.choose({});
        return true;
    case ListKind::Moved:
        if (const auto to = editor.movePart(*screen, parts[partEvent.item], partEvent.to, partEvent.depth)) f.choose(*to);
        return true;
    case ListKind::Renamed: { // (as typed: one step for undo while it's typed into)
        Part named = parts[partEvent.item];
        named.set("name", partName(partEvent.name));
        editor.setPart(*screen, named, std::format("part-name:{}:{}", named.line, d.focusCount));
        break;
    }
    case ListKind::None:
        break;
    }
    return false;
}

bool Developer::drawPreview(ScreensFrame& f) {
    DeveloperTools& d = tools;
    const float k = f.ui.k;
    // --- The screen as big as fits, a gap under the title, in the middle of the room.
    const SDL_FRect& middle = f.middle;
    const float headH = (layout::previewHeadH - layout::headingH) * k;
    f.scale = std::max(0.01f, std::min(middle.w / engine.canvas().w, (middle.h - headH) / engine.canvas().h));
    f.shown = {middle.x + (middle.w - engine.canvas().w * f.scale) / 2, middle.y + headH + std::max(0.0f, (middle.h - headH - engine.canvas().h * f.scale) / 2),
               engine.canvas().w * f.scale, engine.canvas().h * f.scale};
    f.mx = (d.mouseX - f.shown.x) / f.scale;
    f.my = (d.mouseY - f.shown.y) / f.scale;
    d.shownScreen = f.shown;
    // (a stop: with the keys on it, the arrows move its chosen parts)
    (void)f.ui.keyStop(DeveloperTools::KeyStop::Kind::Press, f.shown, std::string("Preview"_tr), "preview", {},
                       f.editable && f.selected && !d.drag ? DeveloperTools::kOwnAll : 0);
    chooseOnScreen(f);
    if (dragOnScreen(f) || screenKeys(f)) return true;
    paintScreen(f);
    return false;
}

// Choosing parts on the screen, and starting to move or resize them. With a part chosen, the screen
// only moves or resizes it (even under others), so another part is chosen in the list, or after Esc
// lets it go; Shift+click (on the screen or in the list) chooses more, to move together.
void Developer::chooseOnScreen(ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    const Part* selected = f.selected;
    ScreenDef* def = f.def;
    const SDL_FRect& shown = f.shown;
    const float mx = f.mx, my = f.my;
    const bool editable = f.editable;
    const auto& selectedElement = f.selectedElement;
    std::vector<std::size_t>& chosenLines = f.chosenLines;
    auto number = [](const std::string& text, float fallback) { return parseNumber(text).value_or(fallback); };
    if (selected && d.focus.empty())
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            if (d.isKey(key, ToolKey::Back)) {
                f.choose({});
                return true;
            }
            if (d.isKey(key, ToolKey::Delete)) { // (the chosen parts go)
                editor.removeParts(*screen, chosenLines);
                f.choose({});
                return true;
            }
            return false;
        });
    const bool adding = SDL_GetModState() & SDL_KMOD_SHIFT;
    std::optional<std::size_t>& hovered = f.hovered;
    if (editable && ui.over(shown) && !d.drag && (!selected || adding))
        for (std::size_t i = def->elements.size(); i-- > 0;) {
            const SDL_FRect b = d.screenView->partBounds(i);
            if (d.screenView->drawnPart(i) && pointInRect(mx, my, b)) {
                hovered = i;
                break;
            }
        }
    f.handle = selectedElement ? [&] {
        const SDL_FRect b = f.onScreen(d.screenView->partBounds(*selectedElement));
        return SDL_FRect{b.x + b.w - 5 * k, b.y + b.h - 5 * k, 10 * k, 10 * k};
    }() : SDL_FRect{};
    const SDL_FRect& handle = f.handle;
    auto inside = [&](std::size_t element) {
        const SDL_FRect b = d.screenView->partBounds(element);
        return pointInRect(mx, my, b);
    };
    auto within = [&](std::size_t element, std::size_t outer) { // (a part in that one, or in one in it...)
        for (int at = def->elements[element].parent; at >= 0; at = def->elements[static_cast<std::size_t>(at)].parent)
            if (static_cast<std::size_t>(at) == outer) return true;
        return false;
    };
    if (editable && d.clicked && ui.over(shown)) {
        d.clicked = false;
        d.focus.clear();
        if (adding && hovered) { // one more chosen (or one less)
            const std::size_t line = static_cast<std::size_t>(def->elements[*hovered].line - 1);
            if (!d.part) {
                f.choose(line);
            } else if (line == *d.part) {
                d.part = d.moreParts.empty() ? std::nullopt : std::optional(d.moreParts.front());
                if (!d.moreParts.empty()) d.moreParts.erase(d.moreParts.begin());
            } else if (const auto it = std::ranges::find(d.moreParts, line); it != d.moreParts.end()) {
                d.moreParts.erase(it);
            } else {
                d.moreParts.push_back(line);
            }
            d.lastList = "parts";
        } else {
            const bool resize = selectedElement && ui.over(handle);
            const std::optional<std::size_t> element = [&]() -> std::optional<std::size_t> { // (what the press takes hold of)
                if (!selectedElement) return hovered;
                if (resize || inside(*selectedElement)) return selectedElement;
                for (const std::size_t line : chosenLines) // (or another chosen one: they all move)
                    if (const auto el = f.elementOf(line); el && inside(*el)) return selectedElement;
                return std::nullopt;
            }();
            if (element) {
                const Element& el = def->elements[*element];
                const Part p = editor.part(*screen, static_cast<std::size_t>(el.line - 1));
                const SDL_FRect b = d.screenView->partBounds(*element);
                if (!selected) {
                    f.choose(p.line);
                    chosenLines = {p.line};
                }
                d.lastList = "parts";
                DeveloperTools::Drag drag{.part = p.line, .element = *element, .resize = resize, .from = d.screenView->partRect(*element), .x = d.mouseX, .y = d.mouseY,
                                          .px = number(p.option("x"), 0.0f), .py = number(p.option("y"), 0.0f), .pw = b.w, .ph = b.h, .others = {}};
                // The other chosen parts move with it (but not one in a part that's moving: it goes with that
                // one), and so do the parts in them.
                std::vector<std::size_t> moving{*element};
                if (!resize)
                    for (const std::size_t line : chosenLines)
                        if (const auto other = f.elementOf(line); other && line != p.line) moving.push_back(*other);
                auto carried = [&](std::size_t i) { return std::ranges::any_of(moving, [&](std::size_t m) { return within(i, m); }); };
                drag.own = !carried(*element);
                for (const std::size_t other : moving)
                    if (other != *element && !carried(other)) {
                        const Part q = editor.part(*screen, static_cast<std::size_t>(def->elements[other].line - 1));
                        drag.others.push_back({.part = q.line, .element = other, .from = d.screenView->partRect(other),
                                               .px = number(q.option("x"), 0.0f), .py = number(q.option("y"), 0.0f)});
                    }
                if (!resize)
                    for (std::size_t i = 0; i < def->elements.size(); ++i)
                        if (std::ranges::contains(moving, i) || carried(i)) drag.moved.emplace_back(i, d.screenView->partRect(i));
                d.drag = drag;
            }
        }
    }
}

// The parts being moved or resized, as the mouse goes (in steps of 5 pixels, lined up with the other
// parts' edges and middles, and the screen's); written to the script when it's let go (true).
bool Developer::dragOnScreen(ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    ScreenDef* def = f.def;
    const bool editable = f.editable;
    auto snap = [](float v) { return std::round(v / 5.0f) * 5.0f; };
    d.guideX.reset();
    d.guideY.reset();
    if (d.drag && editable && d.drag->element < def->elements.size()) {
        const DeveloperTools::Drag g = *d.drag;
        // (as far as the mouse went, in the screen's pixels as it's shown now: choosing a part can change the room the
        // preview has; in steps of 5 pixels, or lined up with something)
        float dx = snap((d.mouseX - g.x) / f.scale), dy = snap((d.mouseY - g.y) / f.scale);
        // The edges and middles to line up with: the screen's, and those of the parts that aren't moving.
        std::vector<float> xs{0.0f, engine.canvas().w / 2, engine.canvas().w}, ys{0.0f, engine.canvas().h / 2, engine.canvas().h};
        for (std::size_t i = 0; i < def->elements.size(); ++i) {
            if (i == g.element || !d.screenView->drawnPart(i) ||
                std::ranges::any_of(g.moved, [&](const auto& m) { return m.first == i; }))
                continue;
            const SDL_FRect b = d.screenView->partBounds(i);
            xs.insert(xs.end(), {b.x, b.x + b.w / 2, b.x + b.w});
            ys.insert(ys.end(), {b.y, b.y + b.h / 2, b.y + b.h});
        }
        auto lineUp = [](std::initializer_list<float> edges, const std::vector<float>& targets, float& by, std::optional<float>& guide) {
            float best = 7.0f; // (closer than this lines up)
            for (const float e : edges)
                for (const float t : targets)
                    if (std::abs(t - e) < std::abs(best)) {
                        best = t - e;
                        guide = t;
                    }
            if (std::abs(best) < 7.0f) by += best;
        };
        const SDL_FRect from = g.from; // (only across the way it's being moved: a drag sideways stays at its height)
        if (g.resize) {
            if (dx != 0.0f) lineUp({from.x + g.pw + dx}, xs, dx, d.guideX);
            if (dy != 0.0f) lineUp({from.y + g.ph + dy}, ys, dy, d.guideY);
        } else {
            if (dx != 0.0f) lineUp({from.x + dx, from.x + dx + from.w / 2, from.x + dx + from.w}, xs, dx, d.guideX);
            if (dy != 0.0f) lineUp({from.y + dy, from.y + dy + from.h / 2, from.y + dy + from.h}, ys, dy, d.guideY);
        }
        float w = std::max(5.0f, g.pw + dx), h = std::max(5.0f, g.ph + dy);
        if (g.resize && (SDL_GetModState() & SDL_KMOD_SHIFT) && g.pw > 0.0f && g.ph > 0.0f) { // (Shift: its shape kept, by the way it's pulled most)
            const float across = w / g.pw, down = h / g.ph, grow = std::abs(across - 1.0f) >= std::abs(down - 1.0f) ? across : down;
            w = std::max(5.0f, g.pw * grow);
            h = std::max(5.0f, g.ph * grow);
        }
        Element& el = def->elements[g.element];
        // (as places and shares in the box each is in: the parts in them go with them)
        auto placed = [&](std::size_t i, SDL_FPoint edge, float pw, float ph) {
            Element& m = def->elements[i];
            const SDL_FRect box = d.screenView->partBox(i);
            m.placeX = placeFor(edge.x - box.x, pw, box.w);
            m.placeY = -placeFor(edge.y - box.y, ph, box.h);
        };
        if (g.resize) {
            const SDL_FRect box = d.screenView->partBox(g.element);
            el.shareWidth = w / box.w;
            el.shareHeight = h / box.h;
            placed(g.element, {g.from.x, g.from.y}, w, h);
        } else {
            if (g.own) placed(g.element, {g.from.x + dx, g.from.y + dy}, g.from.w, g.from.h);
            for (const auto& o : g.others) placed(o.element, {o.from.x + dx, o.from.y + dy}, o.from.w, o.from.h);
        }
        if (!d.held) { // let go: written to the script (one step for undo)
            d.drag.reset();
            d.guideX.reset();
            d.guideY.reset();
            if (dx != 0.0f || dy != 0.0f) {
                const std::string key = std::format("drag:{}", ++d.focusCount);
                auto write = [&](std::size_t part, std::size_t element, bool size) {
                    Part q = editor.part(*screen, part);
                    const Element& m = def->elements[element];
                    q.set("x", unitText(m.placeX.value_or(-1.0f)));
                    q.set("y", unitText(m.placeY.value_or(1.0f)));
                    if (size) {
                        q.set("width", unitText(m.shareWidth.value_or(0.0f)));
                        q.set("height", unitText(m.shareHeight.value_or(0.0f)));
                    }
                    editor.setPart(*screen, q, key);
                };
                if (g.resize || g.own) write(g.part, g.element, g.resize);
                for (const auto& o : g.others) write(o.part, o.element, false);
                return true;
            }
        }
    }
    return false;
}

// The keys for what's chosen on the screen: copy, cut, paste and duplicate, and the arrows that nudge
// (true: the script changed).
bool Developer::screenKeys(ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Part = StoryEditor::Part;
    const auto& entry = f.entry;
    const auto& screen = f.screen;
    const auto& parts = f.parts;
    const Part* selected = f.selected;
    const bool editable = f.editable;
    const std::vector<std::size_t>& chosenLines = f.chosenLines;
    // Copy, cut, paste and duplicate the chosen parts, or the screen (in the list clicked last).
    if (d.focus.empty() && !d.drag)
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key) {
            const char what = clipboardKey(d, *key);
            if (!what) continue;
            d.keys.erase(key);
            const bool onPart = selected && d.lastList != "screens";
            std::string text;
            if (onPart)
                for (const std::size_t line : f.outerLines())
                    for (const std::string& l : editor.partLines(*screen, editor.part(*screen, line))) text += (text.empty() ? "" : "\n") + l;
            else if (screen)
                text = editor.textOf(screen->file, screen->line, screen->end);
            if (what == 'c' || what == 'x') {
                if (text.empty()) return true;
                SDL_SetClipboardText(text.c_str());
                d.clipText = text;
                if (what == 'x' && onPart) {
                    editor.removeParts(*screen, chosenLines);
                    f.choose({});
                } else if (what == 'x' && screen) {
                    editor.removeScreen(*entry.index);
                    d.screenName.clear();
                    f.choose({});
                }
                return true;
            }
            std::vector<std::string> lines;
            if (what == 'd')
                lines = linesOf(text);
            else
                lines = clipboardLines();
            if (lines.empty()) return true;
            if (lower(firstWord(lines.front())) == "screen") {
                d.screenName = editor.screens()[editor.pasteScreen(lines)].name;
                f.choose({});
            } else if (screen) {
                std::erase_if(lines, [](const std::string& l) { return blank(l); });
                const auto last = chosenLines.empty() ? parts.end() : std::ranges::find(parts, chosenLines.back(), &Part::line);
                const Part* after = last != parts.end() ? &*last : nullptr;
                const std::size_t first = editor.addParts(*screen, after, lines);
                f.choose(first);
                for (std::size_t i = 1; i < lines.size(); ++i) // (the pasted ones stay chosen together: the outer ones)
                    if (indentOf(lines[i]) <= indentOf(lines.front())) d.moreParts.push_back(first + i);
            }
            return true;
        }
    // Arrow keys nudge the chosen parts (Shift: 10 pixels), with the keys on the preview.
    if (const DeveloperTools::KeyStop* on = d.keysOn(); editable && selected && d.focus.empty() && !d.drag && on && on->id == "preview")
        for (const SDL_KeyboardEvent& key : d.keys) {
            const float step = key.mod & SDL_KMOD_SHIFT ? 10.0f : 1.0f;
            const float dx = d.isKey(key, ToolKey::Left, true, true) ? -step : d.isKey(key, ToolKey::Right, true, true) ? step : 0.0f;
            const float dy = d.isKey(key, ToolKey::Up, true, true) ? -step : d.isKey(key, ToolKey::Down, true, true) ? step : 0.0f;
            if (dx == 0.0f && dy == 0.0f) continue;
            std::erase_if(d.keys, [&](const SDL_KeyboardEvent& other) { return &other == &key; });
            const std::string stepKey = std::format("nudge:{}", ++d.focusCount);
            for (const std::size_t line : f.outerLines()) { // (the parts in them go with them)
                const auto el = f.elementOf(line);
                if (!el || !d.screenView) continue;
                const SDL_FRect at = d.screenView->partRect(*el), box = d.screenView->partBox(*el);
                Part p = editor.part(*screen, line);
                p.set("x", unitText(placeFor(at.x + dx - box.x, at.w, box.w)));
                p.set("y", unitText(-placeFor(at.y + dy - box.y, at.h, box.h)));
                editor.setPart(*screen, p, stepKey);
            }
            return true;
        }

    return false;
}

// The screen drawn (what's chosen on it outlined, the lines things line up with), under its heading.
void Developer::paintScreen(const ScreensFrame& f) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    const auto& entry = f.entry;
    const ScreenDef* def = f.def;
    const SDL_FRect& shown = f.shown;
    const float scale = f.scale;
    const auto& hovered = f.hovered;
    const auto& selectedElement = f.selectedElement;
    const SDL_FRect& handle = f.handle;
    draw::rect(r, shown, kBox);
    if (def && d.screenView &&
        paintCanvas([&] {
            std::optional<SwappedIn<std::unique_ptr<Game>>> sampleGame;
            if (d.sample) sampleGame.emplace(engine.game_, d.sample);
            // The phone, on the app the chosen part is in (or the chosen app), on a chat for a part of one.
            // Another part of an app that doesn't show so (as a "Chats" heading with a chat open) shows with a
            // chat open, or none, instead.
            auto phone = engine.phone_;
            std::optional<std::string> otherChat;
            if (selectedElement) {
                const auto& els = def->elements;
                for (int at = static_cast<int>(*selectedElement); at >= 0; at = els[static_cast<std::size_t>(at)].parent)
                    if (const std::string app = els[static_cast<std::size_t>(at)].option("app"); !app.empty()) {
                        phone.app = app;
                        phone.pinned = true;
                        break;
                    }
                phone.locked = phone.app == "lock";
                phone.image.clear();
                // (a part with the messages in it, in them, or with them in it: on the newest chat; with the chats: none)
                auto near = [&](std::string_view list) {
                    for (int at = static_cast<int>(*selectedElement); at >= 0; at = els[static_cast<std::size_t>(at)].parent)
                        if (els[static_cast<std::size_t>(at)].option("list") == list) return true;
                    for (std::size_t j = *selectedElement + 1; j < els.size(); ++j) {
                        int p = els[j].parent;
                        while (p >= 0 && static_cast<std::size_t>(p) != *selectedElement) p = els[static_cast<std::size_t>(p)].parent;
                        if (p >= 0 && els[j].option("list") == list) return true;
                    }
                    return false;
                };
                const auto chats = engine.game().chats();
                const std::string newest = chats.empty() ? std::string{} : chats.front().name;
                if (near("messages")) phone.chat = newest;
                else if (near("chats")) phone.chat.clear();
                else if (phone.pinned) otherChat = phone.chat.empty() ? newest : std::string{};
            }
            const SwappedIn samplePhone(engine.phone_, phone);
            auto paint = [&] {
                if (lower(entry.name) != "title") engine.game_->draw(engine.fonts_.get("default", 18)); // (the story under it)
                d.screenView->draw(engine.renderer());
            };
            paint();
            if (otherChat && *otherChat != engine.phone_.chat && !d.screenView->drawnPart(*selectedElement)) {
                engine.phone_.chat = *otherChat;
                SDL_SetRenderDrawColor(engine.renderer(), 0, 0, 0, 255);
                SDL_RenderClear(engine.renderer());
                paint();
            }
        })) {
        SDL_RenderTexture(r, d.canvas.get(), nullptr, &shown);
        const DeveloperUi::Clip clipped(ui, shown);
        if (hovered) draw::outline(r, f.onScreen(d.screenView->partBounds(*hovered)), kEdge);
        for (const std::size_t line : d.moreParts)
            if (const auto el = f.elementOf(line)) draw::outline(r, f.onScreen(d.screenView->partBounds(*el)), kAccent);
        if (selectedElement) {
            draw::outline(r, f.onScreen(d.screenView->partBounds(*selectedElement)), d.screenView->drawnPart(*selectedElement) ? kAccent : kWarn);
            draw::rect(r, handle, kAccent);
        }
        if (d.guideX) draw::rect(r, {shown.x + *d.guideX * scale, shown.y, 1 * k, shown.h}, kOptionArrow); // (what it lines up with)
        if (d.guideY) draw::rect(r, {shown.x, shown.y + *d.guideY * scale, shown.w, 1 * k}, kOptionArrow);
    }
    draw::outline(r, shown, kEdge);
}

void Developer::drawScreensForm(const ScreensFrame& f) {
    if (tools.picking) return drawNewScreenForm(f);
    if (!f.screen) return drawBuiltInForm(f);
    if (f.selected && !tools.moreParts.empty()) return drawLineUpForm(f);
    drawPartForm(f);
}

// + New screen: the screens to start from (the list scrolls when it's longer than its room).
void Developer::drawNewScreenForm(const ScreensFrame& f) {
    using Screen = StoryEditor::Screen;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    const float titleH = layout::titleH * k;
    ui.formTitle("New screen"_tr, right.x, y);
    const SDL_FRect list{right.x, y + titleH, right.w, right.y + right.h - y - titleH};
    ui.scrollForm(list, (layout::buttonH + layout::gap) * k); // (a button a step, as + New animation's)
    y = list.y - d.formScroll;
    const DeveloperUi::Clip clipped(ui, list);
    auto has = [&](std::string_view name) { return std::ranges::any_of(editor.screens(), [&](const Screen& s) { return lower(s.name) == name; }); };
    for (const ScreenStart& start : kScreenStarts) {
        if (ui.button({right.x, y, right.w, layout::buttonH * k}, tr(start.label), false, true, layout::buttonText)) {
            const std::string_view name = start.name;
            auto will = [&](std::string_view n) { return has(n) || n == name; };
            std::vector<std::string> controls; // (the built-in ones, the game's as its files have them now, then the one it comes with: not the tools' own)
            for (const std::string& line : linesOf(defaultUi()))
                if (lower(firstWord(line)) == "control" && !line.contains("action=\"tools ")) controls.push_back(lower(firstWord(afterFirst(line))));
            for (const std::string& control : editor.defined("control")) {
                const Control* made = d.script ? d.script->control(lower(control)) : nullptr;
                if (!std::ranges::contains(controls, lower(control)) && !(made && toolsVerbOf(*made))) controls.push_back(lower(control));
            }
            for (const auto& [screen, line] : kScreenControls)
                if (const std::string control(firstWord(afterFirst(line))); screen == name && !std::ranges::contains(controls, control)) controls.push_back(control);
            const float sy = engine.canvas().h / 720.0f;
            // (one there already: the new one is another, pause_2...)
            const std::string as = uniqueName(name.empty() ? std::string("new_screen") : std::string(name), has);
            const bool timed = d.script && std::ranges::any_of(d.script->code, [](const Instruction& in) { // (a choice or a call with a time limit)
                if (const auto* m = std::get_if<cmd::Menu>(&in.command)) return m->seconds > 0.0f;
                const auto* c = std::get_if<cmd::Call>(&in.command);
                return c && c->seconds > 0.0f;
            });
            auto uses = [&](std::string_view w) { return !editor.find(w).empty(); };
            std::vector<std::string> lines = startScreen(name, as, will, uses, timed, controls);
            // (the gallery's viewer comes with it, when the game has none: a picture clicked shows there)
            std::vector<std::string> viewer = name == "gallery" && !has("gallery_viewer") ? startScreen("gallery_viewer", "gallery_viewer", will, uses, timed, controls)
                                                                                          : std::vector<std::string>{};
            std::vector<std::string> needs = needsOf(editor, lines, name);
            for (std::string& l : needsOf(editor, viewer, "gallery_viewer"))
                if (!std::ranges::contains(needs, l)) needs.push_back(std::move(l));
            for (std::string& l : needs) l = scaled(l, sy);
            if (!builtIn(name)) // (written for 1280 x 720: places and shares fit any size; its texts placed by their sizes)
                lines = placesFromPixels(lines, 1280.0f, 720.0f, ownSizes(lines, needs, 1280.0f, 720.0f, sy));
            for (std::string& l : lines) l = scaled(l, sy);
            if (!viewer.empty()) { // (after what they need, before the gallery)
                viewer = placesFromPixels(viewer, 1280.0f, 720.0f, ownSizes(viewer, needs, 1280.0f, 720.0f, sy));
                needs.emplace_back();
                for (const std::string& l : viewer) needs.push_back(scaled(l, sy));
            }
            const std::size_t made = editor.addScreen(needs, lines);
            d.picking = false;
            d.screenName = editor.screens()[made].name;
            f.choose({});
            break;
        }
        y += layout::buttonH * k + layout::gap * k;
    }
    ui.formDrawn(list, y);
}

// With a part chosen, where New part puts a new one, at the right of its title: Goes in the screen, or in that part.
void Developer::drawGoesIn(const ScreensFrame& f, PreviewHead& row) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    if (!f.selected) return;
    const float k = ui.k, gap = layout::gap * k;
    const std::string_view label = "Goes in"_tr, screen = "The screen"_tr;
    const float labelW = ui.width(label, layout::labelText), screenW = ui.buttonWidth(screen, layout::smallButtonText, layout::previewButtonW);
    const float partW = std::min(ui.buttonWidth(f.intoName, layout::smallButtonText, layout::previewButtonW), // (a long one cut: the title keeps its room)
                                 std::max(layout::previewButtonW * k, row.room() - ui.width("New part"_tr, layout::headingText) - labelW - screenW - 3 * gap));
    const SDL_FRect partAt = row.at(partW), screenAt = row.at(screenW), labelAt = row.at(labelW); // (from the right)
    ui.textAround(label, labelAt.x, labelAt.y + labelAt.h / 2, layout::labelText, theme::dim);
    d.keyLabel = label; // (a screen reader hears it before each)
    if (ui.button(screenAt, screen, !d.addInto, f.screen.has_value(), layout::smallButtonText)) d.addInto = false;
    if (ui.button(partAt, f.intoName, d.addInto, f.screen.has_value(), layout::smallButtonText)) d.addInto = true;
    d.keyLabel.clear();
}

// New part, under the screen, as the Story page's New line is under its lines: a button for each kind of part, in
// groups (Goes in at the right of its title: see drawGoesIn). A part goes in at the top left of the part it goes in.
bool Developer::drawNewPart(ScreensFrame& f) {
    using Part = StoryEditor::Part;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const auto& screen = f.screen;
    const Part* selected = f.selected;
    const auto clicked = ui.newButtons(f.newParts, f.middle, {}, screen.has_value());
    if (!clicked || !screen) return false;
    const PartStart* start = nullptr;
    for (std::size_t i = *clicked; const auto& [name, starts] : kParts) {
        if (i < starts.size()) {
            start = &starts[i];
            break;
        }
        i -= starts.size();
    }
    if (!start) return false;
    const Part* into = d.addInto ? selected : nullptr;
    std::string line(start->line);
    const auto images = editor.names(StoryEditor::Pick::Images); // (the first, or none to pick)
    line = replaced(line, "IMAGE", images.empty() ? "" : images.front());
    line = std::string(trim(scaled("    " + line, engine.canvas().h / 720.0f)));
    // (written for 1280 x 720: as places and shares, on the screen or at the top left of the part it goes in)
    std::vector<std::string> lines{"screen new", "    " + line};
    if (into)
        if (const auto el = f.elementOf(into->line); el && d.screenView) {
            const SDL_FRect b = d.screenView->partBounds(*el);
            const float sx = 1280.0f / engine.canvas().w, sy = 720.0f / engine.canvas().h;
            std::string at;
            for (const std::string& word : rawWords(line))
                at += (at.empty() ? "" : " ") + (word.starts_with("x=") ? std::string("x=0") : word.starts_with("y=") ? std::string("y=0") : word);
            lines = {"screen new", std::format("    container x={} y={} width={} height={}", b.x * sx, b.y * sy, b.w * sx, b.h * sy), "        " + at};
        }
    std::vector<std::string> needs = needsOf(editor, {"screen new", "    " + line}, "new"); // (the colours and styles it uses that the game lacks)
    for (std::string& l : needs) l = scaled(l, engine.canvas().h / 720.0f);
    line = std::string(trim(placesFromPixels(lines, 1280.0f, 720.0f, ownSizes(lines, needs, 1280.0f, 720.0f, 1.0f)).back()));
    d.picking = false;
    f.choose(editor.addPart(*screen, into, line, needs));
    return true;
}

// A built-in screen (the textbox, the choice menu): what it is, and a button that makes it the game's own.
void Developer::drawBuiltInForm(const ScreensFrame& f) {
    const auto& entry = f.entry;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    const float titleH = layout::titleH * k;
    ui.formTitle(entry.name, right.x, y, kAccent, right.w);
    y += titleH;
    if (ui.button({right.x, y, std::min(right.w, ui.buttonWidth("Make it the game's own"_tr, layout::lineText, layout::widerButtonW)), layout::bigButtonH * k}, "Make it the game's own"_tr)) { // (a copy in the game's screens, used instead)
        const float sy = engine.canvas().h / 720.0f;
        const std::vector<std::string> lines = ownScreenLines(entry.name, sy, editor.defines("screen", "phone"), editor.defines("screen", "pause"));
        std::vector<std::string> needs = needsOf(editor, lines, entry.name); // (the colours and styles it uses that the game lacks)
        for (std::string& l : needs) l = scaled(l, sy);
        if (!lines.empty()) editor.addScreen(needs, lines);
        f.choose({});
        return;
    }
}

// Several parts chosen: lined up by their edges or middles, or spread out evenly between the outer two
// (one step for undo).
void Developer::drawLineUpForm(const ScreensFrame& f) {
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    const auto& chosenLines = f.chosenLines;
    const bool editable = f.editable;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    ui.formTitle("{} parts"_tr(chosenLines.size()), right.x, y);
    y += layout::titleH * k;
    struct Item {
        std::size_t line = 0, element = 0;
        SDL_FRect b{};
    };
    std::vector<Item> items;
    if (editable)
        for (const std::size_t line : chosenLines)
            if (const auto el = f.elementOf(line); el && d.screenView->drawnPart(*el)) items.push_back({line, *el, d.screenView->partBounds(*el)});
    static const std::array<std::string_view, 6> kLineUp{"Left"_text, "Centre"_text, "Right"_text, "Top"_text, "Middle"_text, "Bottom"_text}; // (told apart by their English)
    static const std::array<std::string_view, 2> kSpread{"Across"_text, "Down"_text};
    const float labelW = labelColumn(ui, std::round(right.w * layout::labelPart), right.w, {}, {"Line up"_tr, "Spread evenly"_tr}), bh = layout::boxH * k;
    std::optional<std::string_view> clicked;
    for (const auto& [label, words, need] : {std::tuple{std::string_view("Line up"_tr), std::span<const std::string_view>(kLineUp), std::size_t{2}},
                                             {std::string_view("Spread evenly"_tr), std::span<const std::string_view>(kSpread), std::size_t{3}}}) {
        ui.textFitted(label, right.x, y + bh / 2, layout::labelText, theme::dim, labelW - 8 * k); // (each a row: its buttons on one line)
        std::vector<std::string_view> shown;
        for (const std::string_view word : words) shown.push_back(tr(word));
        if (const auto i = ui.buttons(right.x + labelW, y, right.w - labelW, bh, shown, std::vector<bool>(words.size(), items.size() >= need), layout::smallButtonText)) clicked = words[*i];
        y += bh + layout::rowGap * k; // (as every form's rows)
    }
    if (!clicked) return;
    const bool across = *clicked == "Left" || *clicked == "Centre" || *clicked == "Right" || *clicked == "Across";
    auto from = [&](const Item& i) { return across ? i.b.x : i.b.y; };
    auto size = [&](const Item& i) { return across ? i.b.w : i.b.h; };
    std::vector<float> by(items.size()); // how far each one moves
    float low = FLT_MAX, high = -FLT_MAX;
    for (const Item& i : items) low = std::min(low, from(i)), high = std::max(high, from(i) + size(i));
    if (*clicked == "Across" || *clicked == "Down") { // (in their order, the same space between them)
        std::vector<std::size_t> order(items.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::ranges::sort(order, {}, [&](std::size_t i) { return from(items[i]); });
        float sizes = 0.0f;
        for (const Item& i : items) sizes += size(i);
        const float space = (high - low - sizes) / static_cast<float>(items.size() - 1);
        float at = low;
        for (const std::size_t i : order) {
            by[i] = at - from(items[i]);
            at += size(items[i]) + space;
        }
    } else {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const bool start = *clicked == "Left" || *clicked == "Top", end = *clicked == "Right" || *clicked == "Bottom";
            by[i] = start ? low - from(items[i]) : end ? high - from(items[i]) - size(items[i]) : (low + high) / 2 - from(items[i]) - size(items[i]) / 2;
        }
    }
    const std::string key = std::format("line-up:{}", ++d.focusCount);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (std::lround(by[i]) == 0) continue;
        const SDL_FRect at = d.screenView->partRect(items[i].element), box = d.screenView->partBox(items[i].element);
        Part q = editor.part(*screen, items[i].line);
        q.set(across ? "x" : "y", unitText(across ? placeFor(at.x + by[i] - box.x, at.w, box.w) : -placeFor(at.y + by[i] - box.y, at.h, box.h)));
        editor.setPart(*screen, q, key);
    }
}

// The chosen part's form (or, with none chosen, the screen's): its name, its text, where it is and its
// settings; or its line as text.
void Developer::drawPartForm(const ScreensFrame& f) {
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    const Part* selected = f.selected;
    ScreenDef* def = f.def;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    const float titleH = layout::titleH * k;
    const bool header = !selected;
    const Part p = header ? editor.part(*screen, screen->line) : *selected;
    const float toggleW = asTextButton(ui, right.x + right.w, y) + layout::gap * k;
    ui.formTitle(header ? "Screen: {}"_tr(p.text) : p.option("name").empty() ? std::string(tr(upperFirst(p.kind))) : std::format("{}: {}", tr(upperFirst(p.kind)), p.option("name")),
                 right.x, y, kAccent, right.w - toggleW);
    y += titleH;
    const std::string id = std::format("part:{}:{}", screen->line, p.line);
    auto change = [&](const Part& changed, std::string_view what) { editor.setPart(*screen, changed, std::format("{}:{}:{}", id, what, d.focusCount)); };
    if (d.asText) { // (its line as the script has it: the screen's own too, as every form's)
        if (const ScriptError* problem = d.problemAt(screen->file, p.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
        if (const auto text = drawRawLine(ui, id + ":raw", StoryEditor::lineOf(p), right.x, right.w, y)) {
            Part raw = p;
            raw.kind = *text;
            raw.text.clear();
            raw.quoted = false;
            raw.options.clear();
            raw.words.clear();
            raw.note.clear();
            change(raw, "raw");
            if (const auto words = rawWords(*text); header && words.size() > 1) d.screenName = unquote(words[1]); // (its name typed over: still the one chosen)
        }
        return;
    }
    // The form (it scrolls when it's longer than its room): a part's text, where it is, then its
    // settings; or the screen's own.
    const SDL_FRect formBox{right.x, y, right.w, std::max(0.0f, right.y + right.h - y)};
    ui.scrollForm(formBox, (layout::boxH + layout::rowGap) * k);
    y -= d.formScroll;
    const DeveloperUi::Clip clipped(ui, formBox);
    const float least = std::round(right.w * layout::partLabelPart); // (the labels' column: wider when they need it, the same for all the form's rows)
    if (const ScriptError* problem = d.problemAt(screen->file, p.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
    // A change from the form: typed (one step for undo while one box is typed into) or picked.
    auto apply = [&](const std::optional<FormEdit>& edit, const std::vector<StoryEditor::Field>& fields, auto&& changed) {
        if (!edit) return;
        auto now = fields;
        std::ranges::find(now, edit->name, &StoryEditor::Field::name)->value = edit->value;
        if (edit->typed) change(changed(now), edit->name);
        else editor.setPart(*screen, changed(now), {}), d.focus.clear(); // (each click a step for undo)
    };
    if (header) {
        const auto fields = optionFields(editor, p, kScreenOptions);
        apply(drawForm(ui, id, fields, right.x, right.w, labelColumn(ui, least, right.w, {fields}), y), fields, [&](const auto& now) { return withOptions(editor, p, now, kScreenOptions); });
    } else {
        // Its name (what the Parts list calls it: the game doesn't show it), its text (or the word after its
        // kind) and its settings, their labels in one column.
        const std::vector<StoryEditor::Field> name{{.name = "name", .value = p.option("name"), .label = "Name", .box = Box::Name, .hint = "None"}};
        const std::string_view textName = textLabel(p.kind);
        std::vector<StoryEditor::Field> text;
        if (!textName.empty() && p.kind == "image") { // (an image or a colour; or one for yes and one for no)
            std::string imageText = p.text; // (bottom=: a colour fading down to another, as one value: navy-black)
            if (const std::string fadesTo = p.option("bottom"); !fadesTo.empty() && parseColour(imageText, &engine.palette()) && !imageText.contains('-')) imageText += "-" + fadesTo;
            addImageFields(text, "text", std::string(textName), "", imageText, editor.variables());
        } else if (!textName.empty()) {
            const Box box = p.kind == "control" ? Box::Control : Box::None;
            text.push_back({.name = "text", .value = p.text, .label = std::string(textName), .box = box, .rich = p.kind == "text" || p.kind == "button"});
        }
        const auto el = f.elementOf(p.line);
        const auto options = partOptions(p.kind);
        auto fields = optionFields(editor, p, options);
        // Its style without one: the one named after its kind (Library > Styles), else default; its other styles (a
        // title's, a locked one's...) without one: its style.
        const std::string_view kindStyle = partStyle(p.kind, [&](std::string_view style) { return editor.defines("style", style); });
        for (StoryEditor::Field& field : fields)
            if (field.name == "style") field.hint = kindStyle;
            else if (field.name.ends_with("_style") && field.box == Box::Style) field.hint = p.option("style").empty() ? std::string(kindStyle) : lower(p.option("style"));
        // ...and where one without its own goes by the game's Settings (Library > Settings), what they say.
        const GameSettings& game = engine.gameSettings();
        for (StoryEditor::Field& field : fields)
            if (field.name == "glide") {
                field.usual = game.glide ? "yes" : "no";
                if (p.option("glide").empty()) field.value = field.usual;
            } else if (field.name == "glide_speed" || field.name == "glide_distance") {
                field.hint = std::format("{:g}%", 100.0f * (field.name == "glide_speed" ? game.glideSpeed : game.glideDistance));
            } else if (field.name == "scrollbar_thickness" || field.name == "fading_edges") {
                field.hint = std::format("{:g}", field.name == "scrollbar_thickness" ? game.scrollbarThickness : game.fadingEdges);
            }
        // Where it is: x, y, width and height (empty: the top left corner, and its own size, shown as it is now).
        std::vector<StoryEditor::Field> place;
        for (std::size_t i = 0; i < 4; ++i) {
            static constexpr std::array names{"x", "y", "width", "height"};
            static const std::array<std::string_view, 4> labels{"X"_text, "Y"_text, "Width"_text, "Height"_text};
            std::string hint = i == 0 ? "-1" : i == 1 ? "1" : "None";
            if (i >= 2 && el && d.screenView) { // (its own size now, as a share of its box)
                const Element& elDef = def->elements[*el];
                const SDL_FRect elBox = elDef.parent >= 0 ? d.screenView->partRect(static_cast<std::size_t>(elDef.parent)) : SDL_FRect{0.0f, 0.0f, engine.canvas().w, engine.canvas().h};
                const SDL_FRect autoSize = d.screenView->partBounds(*el);
                const float of = i == 2 ? elBox.w : elBox.h;
                if (of > 0.0f) hint = unitText((i == 2 ? autoSize.w : autoSize.h) / of);
            }
            place.push_back({.name = names[i], .value = p.option(names[i]), .label = std::string(labels[i]), .box = i == 0 ? Box::Across : i == 1 ? Box::Down : Box::Number,
                             .hint = hint, .group = i == 0 ? std::string("Place and size"_text) : std::string{}});
        }
        const float labelW = labelColumn(ui, least, right.w, {name, text, place, fields});
        if (const auto edit = drawForm(ui, id, name, right.x, right.w, labelW, y)) {
            Part changed = p;
            changed.set("name", partName(edit->value));
            change(changed, "name");
        }
        // Its text, as a box with what it can be to pick from.
        if (!textName.empty()) {
            if (const auto edit = drawForm(ui, id, text, right.x, right.w, labelW, y)) {
                Part changed = p;
                changed.text = edit->value;
                if (p.kind == "image") {
                    auto now = text;
                    if (const auto edited = std::ranges::find(now, edit->name, &StoryEditor::Field::name); edited != now.end()) edited->value = edit->value;
                    changed.text = imageFrom(editor, now, "text");
                    if (parseGradient(changed.text, &engine.palette())) changed.set("bottom", ""); // (one value now: navy-black)
                }
                changed.quoted = p.kind == "text" || p.kind == "button" || p.kind == "image";
                change(changed, "text");
            }
        }
        if (const auto edit = drawForm(ui, id, place, right.x, right.w, labelW, y)) {
            Part changed = p;
            changed.set(edit->name, std::string(trim(edit->value)));
            change(changed, edit->name);
        }
        y += layout::groupGap * k;
        apply(drawForm(ui, id, fields, right.x, right.w, labelW, y), fields, [&](const auto& now) { return withOptions(editor, p, now, options); });
    }
    ui.formDrawn(formBox, y);
}

} // namespace vn
