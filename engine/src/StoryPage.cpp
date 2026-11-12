// The Story page: the scenes and characters to choose from, a scene's lines, New line and Whole story under them,
// and the chosen line's form (the map is StoryMap.cpp's).
#include "DeveloperUi.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace vn {

namespace {

// How many words a text has: runs of letters or numbers (a variable in braces is one; tags aren't),
// and each Chinese or Japanese character.
std::size_t wordsIn(std::string_view text) {
    std::size_t words = 0;
    bool inWord = false;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '{') {
            const auto close = text.find('}', i);
            if (close == npos) break;
            const std::string_view tag = text.substr(i + 1, close - i - 1);
            const std::string_view word = tag.substr(tag.starts_with('/') ? 1 : 0, tag.find(' ') == npos ? npos : tag.find(' ') - (tag.starts_with('/') ? 1 : 0));
            if (!std::ranges::contains(kTextTags, lower(word))) ++words;
            inWord = false;
            i = close + 1;
            continue;
        }
        std::uint32_t point = c;
        std::size_t len = 1;
        if (c >= 0xF0) point = c & 0x07, len = 4;
        else if (c >= 0xE0) point = c & 0x0F, len = 3;
        else if (c >= 0xC0) point = c & 0x1F, len = 2;
        for (std::size_t j = 1; j < len && i + j < text.size(); ++j) point = (point << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3F);
        const bool cjk = (point >= 0x3040 && point <= 0x30FF) || (point >= 0x3400 && point <= 0x9FFF) || (point >= 0xF900 && point <= 0xFAFF) ||
                         (point >= 0xFF66 && point <= 0xFF9F);
        if (cjk) {
            ++words;
            inWord = false;
        } else if (std::isalnum(c) || c >= 0x80) {
            if (!inWord) ++words;
            inWord = true;
        } else {
            inWord = false;
        }
        i += len;
    }
    return words;
}

// About how long that many words take to read (200 a minute): "under a minute", "about 5 min"...
std::string readingTime(std::size_t words) {
    const std::size_t minutes = (words + 100) / 200;
    if (minutes < 1) return "under a minute"_tr();
    if (minutes < 60) return "about {} min"_tr(minutes);
    return minutes % 60 == 0 ? "about {} h"_tr(minutes / 60) : "about {} h {} min"_tr(minutes / 60, minutes % 60);
}

// The words a line shows the player (its texts; not who speaks, nor commands).
std::size_t rowWords(const StoryEditor& editor, const StoryEditor::Row& row) {
    if (row.kind == StoryEditor::Kind::Visual) return 0;
    std::size_t words = 0;
    for (const StoryEditor::Field& f : editor.fields(row))
        if (f.rich) words += wordsIn(f.value);
    return words;
}

// The story as one text file: each scene's lines, choices and messages, without the commands between
// them (for proofreaders and voice actors).
std::string storyText(const StoryEditor& editor) {
    std::string out;
    for (const StoryEditor::Scene& scene : editor.scenes()) {
        out += std::format("{}== {} ==\n", out.empty() ? "" : "\n", scene.name);
        for (const StoryEditor::Row& row : editor.rows(scene)) {
            if (row.kind == StoryEditor::Kind::Visual) continue;
            const auto fields = editor.fields(row);
            auto value = [&](std::string_view name) {
                const auto it = std::ranges::find(fields, name, &StoryEditor::Field::name);
                return it == fields.end() ? std::string{} : withoutTags(it->value);
            };
            std::string line;
            switch (row.kind) {
            case StoryEditor::Kind::Line: {
                const std::string who = value("Speaker"), words = value("Words");
                line = who.empty() ? words : who + ": " + words;
                break;
            }
            case StoryEditor::Kind::Message: { // (a chat's: who writes it, and its words or an image's caption)
                if (const std::string words = value("Text"); !words.empty() && value("Does") != "reacts")
                    line = (value("Sent") == "by_player" ? std::string("me") : value("Who")) + ": " + words;
                break;
            }
            case StoryEditor::Kind::Choice: line = "Choice:"; break;
            case StoryEditor::Kind::Option: line = "> " + value("Text"); break;
            default: break; // (no words of their own)
            }
            if (!line.empty()) out += std::string(static_cast<std::size_t>(row.depth) * 4, ' ') + line + "\n";
        }
    }
    return out;
}

// The Story page's buttons that add a line (New line, under the lines), in groups as the Screens page's
// New part has its parts.
struct Adding {
    ToolsText label;
    Kind kind;
};
// New line's buttons, in groups: what's said, what's seen and heard, and what happens next.
const Adding kAddWords[]{{"Line", Kind::Line}, {"Choice", Kind::Choice}, {"Phone", Kind::Chat}, {"Together", Kind::Together}}; // (a phone line starts as a chat)
// (a background: a visual at it; each of them shown or started, or hidden or stopped: its form's Action)
const Adding kAddStage[]{{"Visual", Kind::Visual}, {"Sound", Kind::Sound}, {"Effect", Kind::Effect}, {"Weather", Kind::Weather}, {"Camera", Kind::Camera}};
const Adding kAddFlow[]{{"If", Kind::If},     {"Set", Kind::Set},   {"Jump", Kind::Jump},
                            {"Wait", Kind::Wait}, {"Time", Kind::Time}, {"Screen", Kind::Screen}}; // (a screen opened: the story waits for it)
const std::pair<ToolsText, std::span<const Adding>> kAdds[]{{"Words", kAddWords}, {"Stage", kAddStage}, {"Flow", kAddFlow}};

SDL_Color kindColour(Kind kind) {
    switch (kind) {
    case Kind::Line:
    case Kind::Together:
    case Kind::Message:
    case Kind::Chat:
    case Kind::Call:
    case Kind::Missed: return kAccent; // (the phone's lines: one kind)
    case Kind::Effect:
    case Kind::Weather:
    case Kind::Camera:
    case Kind::Visual: return {120, 220, 210, 255};
    case Kind::Sound: return {200, 165, 255, 255};
    case Kind::Choice:
    case Kind::Option: return {255, 190, 120, 255};
    case Kind::Jump:
    case Kind::If:
    case Kind::Else:
    case Kind::Set:
    case Kind::Wait:
    case Kind::Time: return kWarn;
    case Kind::Other: return theme::dim;
    default: return {230, 230, 240, 255};
    }
}

// A line's Voice file box says in grey what plays without one: its speaker's voice file for it ("voices/aiko/start_3",
// in sounds/: voice_files=), or their made-up voice (else None).
void showUsualVoice(std::vector<StoryEditor::Field>& fields, const Script& s, const std::string& path, std::size_t line) {
    const auto field = std::ranges::find(fields, std::string_view("Voice"), &StoryEditor::Field::name);
    if (field == fields.end()) return;
    for (const Instruction& ins : s.code)
        if (ins.line == static_cast<int>(line) + 1 && s.files[ins.file] == path)
            if (const auto* say = std::get_if<cmd::Say>(&ins.command))
                if (const CharacterDef* def = say->character.empty() ? nullptr : s.character(say->character)) {
                    if (!def->voiceFiles.empty()) field->hint = def->voiceFiles + "/" + say->voiceId;
                    else if (def->madeUpVoice > 0.0f) field->hint = "Made-up voice"_hint;
                    return;
                }
}

// The lines a new row of `kind` starts as: who or what it's about, and its words, left empty (to pick
// from its list, or to type), not made up.
std::vector<std::string> newLines(Kind kind) {
    switch (kind) {
    case Kind::Line: return {"\"\""}; // (blank, to type into)
    case Kind::Choice: return {"choice"}; // (no options: its form's + New option adds them)
    case Kind::Option: return {"\"\""}; // (blank, to type into)
    case Kind::If: return {"if"}; // (nothing in it: its form's + New line adds it)
    case Kind::Else: return {"else"};
    case Kind::Set: return {"set"};
    case Kind::Visual: return {"show"};
    case Kind::Sound: return {"play"};
    case Kind::Weather: return {"weather rain"};
    case Kind::Together: return {"together"};
    case Kind::Missed: return {"missed:"};
    case Kind::Time: return {"time passes"}; // (to the next part of the day)
    default:
        if (const auto phone = StoryEditor::phoneLines(kind); !phone.empty()) return phone; // (the phone's lines)
        if (const CommandForm* form = formOf(kind)) return {std::string(spaced(wordsOf(form->verbs).front()))}; // (its form's first word)
        return {"\"\""}; // (a line to type into)
    }
}

// Who writes the phone lines of the chat a new row goes into, after the chosen row or inside it: the chat's
// person (not picked yet: the player, me). None: it isn't in one.
std::optional<std::string> chatWriter(const StoryEditor& editor, const std::vector<StoryEditor::Row>& rows, const StoryEditor::Row* selected, bool inside) {
    if (!selected) return std::nullopt;
    int depth = selected->depth + (inside ? 1 : 0); // (the new row's)
    for (std::size_t j = static_cast<std::size_t>(selected - rows.data()) + 1; j-- > 0;) {
        const StoryEditor::Row& r = rows[j];
        if (r.depth >= depth) continue;
        depth = r.depth;
        if (r.kind != Kind::Chat) continue;
        std::string who;
        for (const StoryEditor::Field& field : editor.fields(r))
            if (field.name == "Who") who = field.value;
        return who.empty() ? std::string("me") : who;
    }
    return std::nullopt;
}

// The box a new row is typed into (or picked from) first, if it has one: its words, or who or what
// it's about.
std::string_view mainField(Kind kind) {
    switch (kind) {
    case Kind::Line: return "Words";
    case Kind::Option: return "Text";
    case Kind::Message: return "Image"; // (a chat's new one: someone sends an image)
    case Kind::Chat: return "Who";
    case Kind::Call: return "Who";
    case Kind::Visual: return "Who";
    case Kind::Set: return "Variable";
    case Kind::Jump: return "Scene";
    case Kind::Sound: return "Sound";
    case Kind::Wait: return "Seconds";
    case Kind::Time: return "How long";
    case Kind::Screen: return "Screen";
    default: return {};
    }
}

} // namespace

// The Story page as it is this frame: the scene open, its lines and the one chosen, the characters, and what's
// said under the lines. Its page parts are drawn by drawScenesList, drawVisualTypes, drawSoundTypes, drawLinesList, drawNewLine,
// drawWholeStory, drawStoryForm and drawMap, each in the room drawPageParts gives it.
struct Developer::StoryFrame {
    DeveloperUi& ui;
    DeveloperTools& d;
    const StoryEditor::Scene& scene;
    const std::vector<StoryEditor::Row>& rows;
    // A type: every game's (none of its own lines), or the game's own (its type line).
    struct Type {
        std::string name;
        std::optional<StoryEditor::Definition> own;
    };
    const std::vector<Type>& types;                        // the visual types: every game's (background, character, image, movie), then the game's own
    const std::vector<StoryEditor::Character>& characters; // (listed under the type character)
    const std::vector<StoryEditor::Definition>& visuals;   // the ones named by visual lines (listed under their types)
    const std::vector<Type>& soundTypes;                   // the sound types: every game's (music, sound, voice), then the game's own
    const std::vector<StoryEditor::Definition>& sounds;    // the ones named by sound lines (listed under their types)
    const StoryEditor::Row* selected;
    std::string counts; // what's said under the lines (storyCounts)

    // Whether the scene's row i is chosen, and the lines of the chosen rows (the first one's last).
    [[nodiscard]] bool isChosen(std::size_t i) const { return d.row == rows[i].line || std::ranges::contains(d.moreRows, rows[i].line); }
    [[nodiscard]] std::vector<std::size_t> chosenLines() const {
        std::vector<std::size_t> out(d.moreRows);
        if (d.row) out.push_back(*d.row);
        return out;
    }
    // A line, a visual type, a character, a named visual, a sound type or a named sound chosen: the form shows it, and what
    // was chosen with it isn't any more.
    void choose(std::optional<std::size_t> row, std::optional<std::size_t> type, std::optional<std::size_t> character = {},
                std::optional<std::size_t> visual = {}, std::optional<std::size_t> soundType = {}, std::optional<std::size_t> sound = {}) const {
        d.row = row;
        d.moreRows.clear();
        d.collapseTo.reset();
        d.visualType = type;
        d.character = character;
        d.namedVisual = visual;
        d.soundType = soundType;
        d.namedSound = sound;
        d.asText = false;
        d.armed.clear();
        d.focus.clear();
        d.formScroll = 0.0f;
    }
    // A line just added by a button: chosen, in sight, its form from its top (what's in it is clicked into, as any box); its
    // own mistakes wait while it's chosen (DeveloperTools::freshRow).
    void show(std::size_t line) const {
        choose(line, {});
        d.rowInSight = line;
        fresh(line);
    }
    void fresh(std::size_t line) const {
        const auto now = d.editor.rows(scene);
        const auto row = std::ranges::find(now, line, &StoryEditor::Row::line);
        d.freshRow = DeveloperTools::FreshRow{.scene = d.scene, .path = d.editor.path(scene.file), .from = line, .end = row != now.end() ? row->end : line + 1};
        d.showProblems();
    }
    // A line just added by Enter in the words of the one before: chosen, and typed into straight away.
    void startTyping(std::size_t line) const {
        choose(line, {});
        d.rowInSight = line;
        fresh(line);
        const auto now = d.editor.rows(scene);
        const auto row = std::ranges::find(now, line, &StoryEditor::Row::line);
        const std::string_view name = row != now.end() ? mainField(row->kind) : std::string_view{};
        if (name.empty()) return;
        const auto fields = d.editor.fields(*row);
        auto f = std::ranges::find(fields, name, &StoryEditor::Field::name);
        if (f == fields.end() && row->kind == Kind::Message) f = std::ranges::find(fields, std::string_view("Text"), &StoryEditor::Field::name); // (one that says something: its words)
        if (f == fields.end()) return;
        d.focusOn(std::format("row:{}:{}", line, f->name), f->value);
        StoryEditor::Field boxed = *f; // (as its kind has it)
        applyBox(boxed);
        if (boxed.pick != StoryEditor::Pick::None || !boxed.suggest.empty()) d.openList = d.focus; // (its list opens, to pick from)
    }
};

namespace {

// New line's buttons, in their groups.
std::vector<DeveloperUi::ButtonGroup> newLineGroups() {
    std::vector<DeveloperUi::ButtonGroup> groups;
    for (const auto& [name, adds] : kAdds) {
        DeveloperUi::ButtonGroup& g = groups.emplace_back(DeveloperUi::ButtonGroup{.name = tr(name), .labels = {}});
        for (const Adding& add : adds) g.labels.push_back(tr(add.label));
    }
    return groups;
}

// What's under the lines, `w` wide: how long the scene and the game are (storyCounts) beside its buttons (Save as a
// text file, and Remove the stops while there are some), or over them, the whole width, when that would leave them
// less than half; and how tall that is.
struct StoryFoot {
    std::string unstop;
    float saveW = 0.0f, unstopW = 0.0f, countsW = 0.0f, countsH = 0.0f, h = 0.0f, leastW = 0.0f;
    bool countsOver = false;
};
StoryFoot storyFoot(DeveloperUi& ui, const std::string& counts, std::size_t stops, float w) {
    const float k = ui.k, listGap = layout::gap * k;
    StoryFoot foot;
    foot.saveW = ui.buttonWidth("Save as a text file"_tr, layout::smallButtonText, layout::widerButtonW);
    foot.unstop = stops == 1 ? "Remove the stop"_tr() : "Remove the {} stops"_tr(stops); // (Stop here, in every scene)
    foot.unstopW = stops == 0 ? 0.0f : ui.buttonWidth(foot.unstop, layout::smallButtonText, layout::widerButtonW) + listGap;
    const float besideW = w - foot.saveW - foot.unstopW - 2 * listGap;
    foot.countsOver = besideW < w / 2;
    foot.countsW = foot.countsOver ? w : std::max(0.0f, besideW);
    foot.countsH = ui.measure(counts, layout::smallText, foot.countsW).y;
    foot.h = foot.countsOver ? foot.countsH + listGap + layout::smallButtonH * k : std::max(foot.countsH, layout::smallButtonH * k);
    foot.leastW = foot.saveW + foot.unstopW; // (its buttons side by side, the counts over them)
    return foot;
}

} // namespace

// The Story page, its page parts each under its title: Scenes over Visual types and Sound types on the left; the chosen
// scene's Lines over New line and Whole story (what's under them) in the middle; and the chosen line's (or type's) form
// over the Map of the scenes on the right.
void Developer::drawStory(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    using Row = StoryEditor::Row;

    // Where the page puts them, side by side: Scenes over Visual types and Sound types; Lines over New line and Whole story;
    // the form over the Map.
    const auto [left, middle, right] = ui.columns(area);
    const float half = std::round(left.h * 0.46f), typesH = std::round(left.h * 0.3f), formH = std::round(right.h * 0.62f);
    const SDL_FRect scenesAt{left.x, left.y, left.w, half};
    const auto& scenes = editor.scenes();
    if (scenes.empty()) { // (none yet: only the scenes, empty, and + New scene)
        std::array pageParts{listPagePart(ui, "scenes", "Scenes"_tr, scenesAt, 1, [&](const ListRoom& at) {
            ui.list(at.list, d.scenesScroll, 0, [](std::size_t) { return std::string{}; }, [](std::size_t) { return false; }, [](std::size_t) { return false; });
            if (!ui.button(at.rows[0], "+ New scene"_tr, false, true, layout::buttonText)) return false;
            d.scene = editor.addScene(0);
            return true;
        })};
        drawPageParts(ui, "story", pageParts);
        return;
    }
    d.scene = std::min(d.scene, scenes.size() - 1);
    const StoryEditor::Scene scene = scenes[d.scene];
    const auto rows = editor.rows(scene);
    std::vector<StoryFrame::Type> types, soundTypes;
    for (const std::string_view type : kVisualTypes) types.push_back({.name = std::string(type), .own = std::nullopt});
    for (const StoryEditor::Definition& def : editor.definitions("type")) types.push_back({.name = def.name, .own = def});
    for (const std::string_view type : kBuiltInSoundTypes) soundTypes.push_back({.name = std::string(type), .own = std::nullopt});
    for (const StoryEditor::Definition& def : editor.definitions("sound type")) soundTypes.push_back({.name = def.name, .own = def});
    const auto characters = editor.characters();
    const auto visuals = editor.definitions("visual"), sounds = editor.definitions("sound");
    const Row* selected = nullptr;
    if (d.row) {
        const auto it = std::ranges::find(rows, *d.row, &Row::line);
        if (it != rows.end()) selected = &*it;
        else d.row.reset();
    }
    clampIndex(d.visualType, types.size());
    clampIndex(d.character, characters.size());
    clampIndex(d.namedVisual, visuals.size());
    clampIndex(d.soundType, soundTypes.size());
    clampIndex(d.namedSound, sounds.size());

    StoryFrame f{.ui = ui,
                 .d = d,
                 .scene = scene,
                 .rows = rows,
                 .types = types,
                 .characters = characters,
                 .visuals = visuals,
                 .soundTypes = soundTypes,
                 .sounds = sounds,
                 .selected = selected,
                 .counts = {}};
    f.counts = storyCounts(f);
    const SDL_FPoint chrome = pagePartChrome(k); // (New line and Whole story are as tall as what's in them)
    const float headingH = layout::headingH * k;
    const float newLineH = chrome.y + headingH + ui.newButtonsH(newLineGroups(), middle.w - chrome.x);
    const float footH = chrome.y + headingH + storyFoot(ui, f.counts, d.stops.size(), middle.w - chrome.x).h;
    const float linesH = std::max(chrome.y + headingH + layout::listItemH * k, middle.h - newLineH - footH); // (the rest: a line's room under its title at least)
    std::array pageParts{
        listPagePart(ui, "scenes", "Scenes"_tr, scenesAt, 1, [&](const ListRoom& at) { return drawScenesList(f, at); }),
        listPagePart(ui, "types", "Visual types"_tr, {left.x, left.y + half, left.w, typesH}, 1, [&](const ListRoom& at) { return drawVisualTypes(f, at); }),
        listPagePart(ui, "sound_types", "Sound types"_tr, {left.x, left.y + half + typesH, left.w, left.h - half - typesH}, 1,
                     [&](const ListRoom& at) { return drawSoundTypes(f, at); }),
        listPagePart(ui, "lines", "Lines"_tr, {middle.x, middle.y, middle.w, linesH}, 0, [&](const ListRoom& at) { return drawLinesList(f, at); }),
        PagePart{.id = "new_line",
                 .heading = "New line"_tr,
                 .at = {middle.x, middle.y + linesH, middle.w, newLineH},
                 .draw = [&](const SDL_FRect& box) { return drawNewLine(f, box); },
                 .least = {layout::pagePartButtonsW * k, 0.0f},
                 .tallAt = [&](float w) { return ui.newButtonsH(newLineGroups(), w); }},
        PagePart{.id = "whole_story",
                 .heading = "Whole story"_tr,
                 .at = {middle.x, middle.y + linesH + newLineH, middle.w, footH},
                 .draw = [&](const SDL_FRect& box) { return drawWholeStory(f, box); },
                 .least = {storyFoot(ui, f.counts, d.stops.size(), 0.0f).leastW, 0.0f},
                 .tallAt = [&](float w) { return storyFoot(ui, f.counts, d.stops.size(), w).h; }},
        formPagePart(ui, "form", "Form"_tr, {right.x, right.y, right.w, formH}, true, [&](const SDL_FRect& box) { return drawStoryForm(f, box), false; }), // (the scene's, at least)
        PagePart{.id = "map", .heading = "Map"_tr, .at = {right.x, right.y + formH, right.w, right.h - formH}, .draw = [&](const SDL_FRect& box) { return drawMap(ui, box); }}};
    drawPageParts(ui, "story", pageParts); // (one changed something: the rest are drawn next frame)
}

// The scenes (each file's under its name, with more than one) and + New scene (true: changed, the rest next
// frame).
bool Developer::drawScenesList(const StoryFrame& f, const ListRoom& at) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const auto& scenes = editor.scenes();
    const auto& scene = f.scene;
    const auto& rows = f.rows;
    const std::vector<bool>& wrongScene = d.wrongScenes(); // (as of this version of the scripts)
    const SDL_FRect& scenesBox = at.list;
    // With more than one script file, each file's scenes are under its name.
    struct Entry {
        std::size_t file = 0;
        std::optional<std::size_t> scene; // (none: the file's name)
    };
    std::vector<Entry> entries;
    std::vector<std::size_t> files; // (not the ones of just screens or definitions)
    for (std::size_t file = 0; file < editor.fileCount(); ++file)
        if (editor.storyFile(file)) files.push_back(file);
    const bool byFile = files.size() > 1;
    for (const std::size_t file : files) {
        if (byFile) entries.push_back({.file = file, .scene = std::nullopt});
        for (std::size_t i = 0; i < scenes.size(); ++i)
            if (scenes[i].file == file) entries.push_back({.file = file, .scene = i});
    }
    // Where a scene dropped before entry `slot` goes: the file, and the scene it goes before (or past
    // the file's last).
    auto target = [&](std::size_t slot) -> std::pair<std::size_t, std::size_t> {
        if (slot < entries.size() && entries[slot].scene) return {entries[slot].file, *entries[slot].scene};
        const std::size_t file = slot > 0 ? entries[slot - 1].file : 0;
        std::size_t to = 0;
        for (std::size_t i = 0; i < scenes.size(); ++i)
            if (scenes[i].file <= file) to = i + 1;
        return {file, to};
    };
    // A line dragged onto a scene goes to its end.
    std::optional<std::size_t> dropOn;
    if (d.listDrag && d.listDrag->list == "lines" && d.listDrag->moving && ui.over(scenesBox) && d.listDrag->item < rows.size()) {
        const auto i = static_cast<std::size_t>(std::max(0.0f, d.mouseY - scenesBox.y + d.scenesScroll) / (layout::listItemH * k));
        if (i < entries.size() && entries[i].scene && *entries[i].scene != d.scene) dropOn = *entries[i].scene;
        if (!d.held) {
            const std::size_t item = d.listDrag->item;
            d.listDrag.reset();
            if (dropOn) {
                editor.moveRowToScene(scene, rows[item], scenes[*dropOn]);
                f.choose({}, {});
                return true;
            }
        }
    }
    // A click opens a scene, a double click renames it (the jumps to it follow), its x deletes it, and
    // dragging it moves it (to another file, too). A file's name renames the same way, and its x moves
    // it to the Trash, with its scenes.
    using ListKind = DeveloperUi::ListEvent::Kind;
    const auto event = ui.editList(
        "scenes", scenesBox, d.scenesScroll,
        {.count = entries.size(),
         .label = [&](std::size_t i) { return entries[i].scene ? scenes[*entries[i].scene].name : editor.fileName(entries[i].file); },
         .chosen = [&](std::size_t i) { return entries[i].scene == d.scene && !d.visualType && !d.character && !d.namedVisual; },
         .wrong = [&](std::size_t i) { return entries[i].scene && wrongScene[*entries[i].scene]; },
         .faint = {},
         .renames = [&](std::size_t i) { return entries[i].scene || entries[i].file > 0; },
         .deletes =
             [&](std::size_t i) {
                 if (!entries[i].scene) return entries[i].file > 0;
                 return scenes.size() > 1 && scenes[*entries[i].scene].name != "start";
             },
         .moves = [&](std::size_t i) { return entries[i].scene && scenes[*entries[i].scene].name != "start"; },
         .drops =
             [&](std::size_t item, std::size_t slot) { // (never before start: the story begins there)
                 if (byFile && slot == 0) return false;
                 const auto [file, to] = target(slot);
                 const std::size_t from = *entries[item].scene;
                 if (file == scenes[from].file && (to == from || to == from + 1)) return false; // (where it is)
                 const auto start = std::ranges::find(scenes, std::string("start"), &StoryEditor::Scene::name);
                 return !(start != scenes.end() && file == start->file && to <= static_cast<std::size_t>(start - scenes.begin()));
             },
         .indent = [&](std::size_t i) { return byFile && entries[i].scene ? 12 * k : 0.0f; },
         .paint =
             [&](std::size_t i, const SDL_FRect& box) {
                 if (entries[i].scene) {
                     ui.textFitted(scenes[*entries[i].scene].name, box.x + 10 * k, box.y + box.h / 2, layout::listText, theme::text, box.w - 20 * k);
                     if (dropOn == entries[i].scene) draw::outline(ui.r, box, kAccent);
                 } else {
                     ui.textFitted(editor.fileName(entries[i].file) + ".vnscript", box.x + 6 * k, box.y + box.h / 2, layout::smallText, kAccent, box.w - 12 * k);
                 }
             }});
    switch (event.kind) {
    case ListKind::Chosen:
        if (!entries[event.item].scene) break;
        f.choose({}, {});
        if (d.scene != *entries[event.item].scene) { // (another scene: its lines are drawn from the next frame)
            d.linesScroll = 0.0f;
            d.scene = *entries[event.item].scene;
            return true;
        }
        break;
    case ListKind::Renamed:
        if (trim(event.name).empty()) return true; // (emptied: it keeps its name)
        if (const Entry e = entries[event.item]; !e.scene) {
            d.saidRenamed(editor.renameFile(e.file, event.name, std::format("file-name:{}:{}", e.file, d.focusCount)), "A file's name is one word (letters, numbers, _ and -) that no other script has."_tr);
        } else d.saidRenamed(editor.renameScene(*e.scene, event.name, std::format("scene-name:{}:{}", *e.scene, d.focusCount)), "A scene's name is one word (letters, numbers, _ and -) that no other scene has."_tr);
        return true;
    case ListKind::Deleted:
        if (const Entry e = entries[event.item]; !e.scene) {
            const std::string name = editor.fileName(e.file);
            editor.removeFile(e.file);
            d.scene = 0;
            logInfo("{}", "'{}.vnscript' and its scenes are in the Trash now (undo brings them back)."_tr(name));
        } else {
            editor.removeScene(*e.scene);
            d.scene = d.scene >= *e.scene && d.scene > 0 ? d.scene - 1 : d.scene;
        }
        f.choose({}, {});
        return true;
    case ListKind::Moved: {
        const std::size_t from = *entries[event.item].scene;
        const auto [file, to] = target(event.to);
        if (const auto now = file == scenes[from].file ? editor.moveScene(from, to) : editor.moveSceneTo(from, file, to)) {
            d.scene = *now;
            f.choose({}, {});
        }
        return true;
    }
    case ListKind::Opened:
    case ListKind::Closed:
    case ListKind::None:
        break;
    }
    if (ui.button(at.rows[0], "+ New scene"_tr, false, true, layout::buttonText)) { // (in the file of the one open, chosen: its form shows it)
        d.scene = editor.addScene(scene.file);
        f.choose({}, {});
        return true; // (the lines have moved: drawn again next frame)
    }
    return false;
}

// The visual types, and + New type (true: changed, the rest next frame), listed as a screen's parts are: every game's
// first, then the game's own, each with what's of it under it (the characters under character, each with their name
// colour; the visuals named by visual lines under their types). The game's own types, the characters and the named
// visuals are renamed by a double click (the lines of them follow), deleted by their x and dragged among their own (a
// character among the characters of its file, a visual among its type's); + New type makes a type, each type's form one
// of it, each chosen, its form shown.
bool Developer::drawVisualTypes(const StoryFrame& f, const ListRoom& at) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const auto& types = f.types;
    const auto& characters = f.characters;
    const auto& visuals = f.visuals;
    constexpr std::size_t prebuilt = kVisualTypes.size();
    using ListKind = DeveloperUi::ListEvent::Kind;
    struct Entry {
        std::optional<std::size_t> type, character, visual;
    };
    std::vector<Entry> entries;
    std::vector<std::string> typeOf; // (each named visual's)
    for (const StoryEditor::Definition& v : visuals) typeOf.push_back(editor.namedVisualType(v.name));
    std::vector<bool> listed(visuals.size(), false);
    for (std::size_t i = 0; i < types.size(); ++i) {
        entries.push_back({.type = i, .character = std::nullopt, .visual = std::nullopt});
        if (types[i].name == "character")
            for (std::size_t j = 0; j < characters.size(); ++j) entries.push_back({.type = std::nullopt, .character = j, .visual = std::nullopt});
        for (std::size_t v = 0; v < visuals.size(); ++v)
            if (sameName(typeOf[v], types[i].name)) entries.push_back({.type = std::nullopt, .character = std::nullopt, .visual = v}), listed[v] = true;
    }
    for (std::size_t v = 0; v < visuals.size(); ++v) // (of a type there isn't: at the end, its mistake said)
        if (!listed[v]) entries.push_back({.type = std::nullopt, .character = std::nullopt, .visual = v});
    auto own = [&](std::size_t i) { return i < entries.size() && entries[i].type && types[*entries[i].type].own.has_value(); };
    auto person = [&](std::size_t i) -> const StoryEditor::Character* { return i < entries.size() && entries[i].character ? &characters[*entries[i].character] : nullptr; };
    auto named = [&](std::size_t i) -> const StoryEditor::Definition* { return i < entries.size() && entries[i].visual ? &visuals[*entries[i].visual] : nullptr; };
    auto editable = [&](std::size_t i) { return own(i) || person(i) != nullptr || named(i) != nullptr; };
    const auto picked = ui.editList(
        "types", at.list, d.typesScroll,
        {.count = entries.size(),
         .label = [&](std::size_t i) { return person(i) ? person(i)->name : named(i) ? named(i)->name : types[*entries[i].type].name; },
         .chosen =
             [&](std::size_t i) {
                 if (person(i)) return d.character == entries[i].character;
                 if (named(i)) return d.namedVisual == entries[i].visual;
                 return !d.character && !d.namedVisual && d.visualType == entries[i].type;
             },
         .wrong =
             [&](std::size_t i) {
                 if (const auto* c = person(i)) return d.problemAt(c->file, c->line) != nullptr;
                 if (const auto* v = named(i)) return d.problemWithin(v->file, v->line, v->end);
                 const auto& t = types[*entries[i].type];
                 return t.own && d.problemWithin(t.own->file, t.own->line, t.own->end);
             },
         .faint = {},
         .renames = editable,
         .deletes = editable,
         .moves = editable,
         .drops = // (a character among the characters of its file, a named visual among its type's, a type of the game's own among those)
             [&](std::size_t item, std::size_t slot) {
                 if (const auto* c = person(item)) {
                     auto ofFile = [&](std::size_t s) { return person(s) && person(s)->file == c->file; };
                     return ofFile(slot) || (slot > 0 && ofFile(slot - 1));
                 }
                 if (named(item)) {
                     auto ofType = [&](std::size_t s) { return named(s) && sameName(typeOf[*entries[s].visual], typeOf[*entries[item].visual]); };
                     return ofType(slot) || (slot > 0 && ofType(slot - 1));
                 }
                 return own(slot) || (slot > 0 && own(slot - 1));
             },
         .place = {},
         .indentStep = 0.0f,
         .indent = [&](std::size_t i) { return person(i) || named(i) ? 18.0f * k : 0.0f; },
         .paint = // (a type: its name and its layer after it, small; a character: their name colour as a square before their name)
             [&](std::size_t i, const SDL_FRect& row) {
                 float x = row.x + layout::listPad * k;
                 const float room = row.x + row.w - layout::listPad * k - x;
                 if (const auto* c = person(i)) {
                     if (const auto colour = d.characterColour(engine.palette(), c->name)) x = ui.swatch(x, row.y + row.h / 2, *colour);
                     ui.textFitted(c->name, x, row.y + row.h / 2, layout::listText, theme::text, row.x + row.w - layout::listPad * k - x);
                     return;
                 }
                 if (const auto* v = named(i)) {
                     ui.textFitted(v->name, x, row.y + row.h / 2, layout::listText, theme::text, room);
                     return;
                 }
                 const std::string& name = types[*entries[i].type].name;
                 const float shown = ui.textFitted(name, x, row.y + row.h / 2, layout::listText, theme::text, room * 0.7f);
                 ui.textFitted(std::to_string(editor.typeLayer(name)), x + shown + 8 * k, row.y + row.h / 2, layout::smallText, theme::dim, room - shown - 8 * k);
             }});
    switch (picked.kind) {
    case ListKind::Chosen:
        f.choose({}, entries[picked.item].type, entries[picked.item].character, entries[picked.item].visual);
        break;
    case ListKind::Renamed:
        if (trim(picked.name).empty()) return true; // (emptied: it keeps its name)
        if (const auto* c = person(picked.item)) {
            auto fields = editor.fields(*c);
            fields[0].value = std::string(trim(picked.name));
            d.saidRenamed(editor.setCharacter(*c, fields, std::format("character-name:{}:{}", c->line, d.focusCount)), "Another character has that name."_tr);
        } else if (named(picked.item)) {
            d.saidRenamed(editor.renameDefinition("visual", *entries[picked.item].visual, std::string(trim(picked.name)), std::format("visual-name:{}:{}", picked.item, d.focusCount)),
                          "A visual's name is one word (letters, numbers, _ and -) that no other visual has."_tr);
        } else if (own(picked.item)) {
            d.saidRenamed(editor.renameDefinition("type", *entries[picked.item].type - prebuilt, std::string(trim(picked.name)), std::format("type-name:{}:{}", picked.item, d.focusCount)),
                          "A type's name is one word (letters, numbers, _ and -) that no other type has."_tr);
        }
        return true;
    case ListKind::Deleted:
        if (const auto* c = person(picked.item)) editor.removeCharacter(*c);
        else if (const auto* v = named(picked.item)) editor.removeDefinition(*v);
        else if (own(picked.item)) editor.removeDefinition(*types[*entries[picked.item].type].own);
        f.choose({}, {});
        return true;
    case ListKind::Moved: {
        auto nextOf = [&](auto has, std::size_t none) { // (the one at the place it goes, or after them all)
            for (std::size_t s = picked.to; s < entries.size(); ++s)
                if (const auto found = has(s)) return *found;
            return none;
        };
        if (person(picked.item)) {
            const std::size_t to = nextOf([&](std::size_t s) { return entries[s].character; }, characters.size());
            if (const auto moved = editor.moveCharacter(*entries[picked.item].character, to)) f.choose({}, {}, *moved);
        } else if (named(picked.item)) {
            const std::size_t to = nextOf([&](std::size_t s) { return entries[s].visual; }, visuals.size());
            if (const auto moved = editor.moveDefinition("visual", *entries[picked.item].visual, to)) f.choose({}, {}, {}, *moved);
        } else if (own(picked.item)) {
            const std::size_t to = nextOf([&](std::size_t s) { return own(s) ? entries[s].type : std::nullopt; }, types.size());
            if (const auto moved = editor.moveDefinition("type", *entries[picked.item].type - prebuilt, to - prebuilt)) f.choose({}, *moved + prebuilt);
        }
        return true;
    }
    case ListKind::Opened:
    case ListKind::Closed:
    case ListKind::None:
        break;
    }
    if (ui.button(at.rows[0], "+ New type"_tr, false, true, layout::buttonText)) { // (named new_type: its form shows it)
        const std::string name = uniqueName("new_type", [&](const std::string& n) { return editor.typeNameTaken(n); });
        f.choose({}, editor.addDefinition("type", {"type " + name}) + prebuilt);
        return true;
    }
    return false;
}

// The sound types, and + New type (true: changed, the rest next frame), listed as the visual types are: every game's
// first, then the game's own, each with the sounds named of it by sound lines under it. The game's own types and the
// named sounds are renamed by a double click (the lines that play or stop them follow), deleted by their x and dragged
// among their own (a sound among its type's); + New type makes a type (following the sound volume), each type's form a
// sound of it, each chosen, its form shown.
bool Developer::drawSoundTypes(const StoryFrame& f, const ListRoom& at) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const auto& types = f.soundTypes;
    const auto& sounds = f.sounds;
    constexpr std::size_t prebuilt = kBuiltInSoundTypes.size();
    using ListKind = DeveloperUi::ListEvent::Kind;
    struct Entry {
        std::optional<std::size_t> type, sound;
    };
    std::vector<Entry> entries;
    std::vector<std::string> typeOf; // (each named sound's)
    for (const StoryEditor::Definition& s : sounds) typeOf.push_back(editor.namedSoundType(s.name));
    std::vector<bool> listed(sounds.size(), false);
    for (std::size_t i = 0; i < types.size(); ++i) {
        entries.push_back({.type = i, .sound = std::nullopt});
        for (std::size_t s = 0; s < sounds.size(); ++s)
            if (sameName(typeOf[s], types[i].name)) entries.push_back({.type = std::nullopt, .sound = s}), listed[s] = true;
    }
    for (std::size_t s = 0; s < sounds.size(); ++s) // (of a type there isn't: at the end, its mistake said)
        if (!listed[s]) entries.push_back({.type = std::nullopt, .sound = s});
    auto own = [&](std::size_t i) { return i < entries.size() && entries[i].type && types[*entries[i].type].own.has_value(); };
    auto named = [&](std::size_t i) -> const StoryEditor::Definition* { return i < entries.size() && entries[i].sound ? &sounds[*entries[i].sound] : nullptr; };
    auto editable = [&](std::size_t i) { return own(i) || named(i) != nullptr; };
    const auto picked = ui.editList(
        "sound_types", at.list, d.soundTypesScroll,
        {.count = entries.size(),
         .label = [&](std::size_t i) { return named(i) ? named(i)->name : types[*entries[i].type].name; },
         .chosen = [&](std::size_t i) { return named(i) ? d.namedSound == entries[i].sound : !d.namedSound && d.soundType == entries[i].type; },
         .wrong =
             [&](std::size_t i) {
                 if (const auto* s = named(i)) return d.problemWithin(s->file, s->line, s->end);
                 const auto& t = types[*entries[i].type];
                 return t.own && d.problemWithin(t.own->file, t.own->line, t.own->end);
             },
         .faint = {},
         .renames = editable,
         .deletes = editable,
         .moves = editable,
         .drops = // (a named sound among its type's, a type of the game's own among those)
             [&](std::size_t item, std::size_t slot) {
                 if (named(item)) {
                     auto ofType = [&](std::size_t s) { return named(s) && sameName(typeOf[*entries[s].sound], typeOf[*entries[item].sound]); };
                     return ofType(slot) || (slot > 0 && ofType(slot - 1));
                 }
                 return own(slot) || (slot > 0 && own(slot - 1));
             },
         .place = {},
         .indentStep = 0.0f,
         .indent = [&](std::size_t i) { return named(i) ? 18.0f * k : 0.0f; },
         .paint = {}});
    switch (picked.kind) {
    case ListKind::Chosen:
        f.choose({}, {}, {}, {}, entries[picked.item].type, entries[picked.item].sound);
        break;
    case ListKind::Renamed:
        if (trim(picked.name).empty()) return true; // (emptied: it keeps its name)
        if (named(picked.item))
            d.saidRenamed(editor.renameDefinition("sound", *entries[picked.item].sound, std::string(trim(picked.name)), std::format("sound-name:{}:{}", picked.item, d.focusCount)),
                          "A sound's name is one word (letters, numbers, _ and -) that no other sound or sound type has."_tr);
        else if (own(picked.item))
            d.saidRenamed(editor.renameDefinition("sound type", *entries[picked.item].type - prebuilt, std::string(trim(picked.name)),
                                                  std::format("sound-type-name:{}:{}", picked.item, d.focusCount)),
                          "A type's name is one word (letters, numbers, _ and -) that no other type has."_tr);
        return true;
    case ListKind::Deleted:
        if (const auto* s = named(picked.item)) editor.removeDefinition(*s);
        else if (own(picked.item)) editor.removeDefinition(*types[*entries[picked.item].type].own);
        f.choose({}, {});
        return true;
    case ListKind::Moved: {
        auto nextOf = [&](auto has, std::size_t none) { // (the one at the place it goes, or after them all)
            for (std::size_t s = picked.to; s < entries.size(); ++s)
                if (const auto found = has(s)) return *found;
            return none;
        };
        if (named(picked.item)) {
            const std::size_t to = nextOf([&](std::size_t s) { return entries[s].sound; }, sounds.size());
            if (const auto moved = editor.moveDefinition("sound", *entries[picked.item].sound, to)) f.choose({}, {}, {}, {}, {}, *moved);
        } else if (own(picked.item)) {
            const std::size_t to = nextOf([&](std::size_t s) { return own(s) ? entries[s].type : std::nullopt; }, types.size());
            if (const auto moved = editor.moveDefinition("sound type", *entries[picked.item].type - prebuilt, to - prebuilt)) f.choose({}, {}, {}, {}, *moved + prebuilt);
        }
        return true;
    }
    case ListKind::Opened:
    case ListKind::Closed:
    case ListKind::None:
        break;
    }
    if (ui.button(at.rows[0], "+ New type"_tr, false, true, layout::buttonText)) { // (named new_type, following the sound volume: its form shows it)
        const std::string name = uniqueName("new_type", [&](const std::string& n) { return editor.typeNameTaken(n) || editor.defines("sound", n); });
        f.choose({}, {}, {}, {}, editor.addDefinition("sound type", {"type " + name + " follows=sound"}) + prebuilt);
        return true;
    }
    return false;
}

// The scene's lines (a click chooses one, its x deletes it, dragging moves it among the lines next to it):
// chosen, moved, copied (true: changed, the rest next frame).
bool Developer::drawLinesList(StoryFrame& f, const ListRoom& at) {
    DeveloperTools& d = tools;
    const auto& scene = f.scene;
    const auto& selected = f.selected;
    StoryEditor& editor = d.editor;
    const SDL_FRect& rowsBox = at.list;
    // F9: Stop here, on or off, for the chosen line (one that does something), as its form's button.
    if (const std::string path = editor.path(scene.file); selected && d.moreRows.empty() && d.runLines.contains({path, selected->line}) && d.focus.empty())
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            if (!d.isKey(key, ToolKey::StopHere)) return false;
            toggleStop(path, selected->line);
            return true;
        });
    return drawRowList(f, rowsBox) || storyClipboard(f);
}

// New line: after the chosen line (and what's inside it), or inside a choice's option, an "if" or
// "else", "together", a chat, a call or its missed: lines; with none chosen, at the scene's end.
bool Developer::drawNewLine(const StoryFrame& f, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    StoryEditor& editor = d.editor;
    const auto& scene = f.scene;
    const auto& selected = f.selected;
    const auto& rows = f.rows;
    const bool inside = selected && (selected->kind == Kind::Option || selected->kind == Kind::If || selected->kind == Kind::Else || selected->kind == Kind::Together ||
                                     selected->kind == Kind::Chat || selected->kind == Kind::Call || selected->kind == Kind::Missed);
    if (const auto later = std::exchange(d.addLater, std::nullopt); later && later->scene == scene.name) { // (its screen made last frame)
        const auto* row = later->row && *later->row < rows.size() ? &rows[*later->row] : nullptr;
        f.show(editor.add(scene, row, later->inside, later->lines));
        return true;
    }
    if (const auto clicked = ui.newButtons(newLineGroups(), box)) {
        std::size_t i = *clicked;
        for (const auto& [name, adds] : kAdds) {
            if (i >= adds.size()) {
                i -= adds.size();
                continue;
            }
            std::vector<std::string> lines = newLines(adds[i].kind);
            if (adds[i].kind == Kind::Chat) // (in a chat: one of its own phone lines, someone sending an image)
                if (const auto who = chatWriter(editor, rows, selected, inside)) lines = {*who + " sends"};
            // A line that needs a screen brings it, ready-made, when the game has none (a chat the phone, the time the calendar
            // for each new day): the screen first, then the line, next frame.
            const std::string_view needs = adds[i].kind == Kind::Chat ? "phone" : adds[i].kind == Kind::Time ? "calendar" : "";
            if (!needs.empty() && std::ranges::none_of(editor.screens(), [&](const StoryEditor::Screen& s) { return lower(s.name) == needs; })) {
                addReadyMadeScreen(needs);
                d.addLater = DeveloperTools::AddLater{.lines = std::move(lines), .scene = scene.name, .row = selected ? std::optional(static_cast<std::size_t>(selected - rows.data())) : std::nullopt,
                                                      .inside = inside};
                return true;
            }
            f.show(editor.add(scene, selected, inside, lines));
            return true; // (the lines have moved: drawn again next frame)
        }
    }
    return false;
}

// Whole story, under the lines: how long the scene and the game are, the lines here never run (see storyCounts), the
// story as one text file (next to the game folder) for proofreaders and voice actors, and Stop here taken off every
// line.
bool Developer::drawWholeStory(const StoryFrame& f, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    StoryEditor& editor = d.editor;
    const float k = ui.k, listGap = layout::gap * k, buttonH = layout::smallButtonH * k;
    const StoryFoot foot = storyFoot(ui, f.counts, d.stops.size(), box.w);
    const float buttonY = foot.countsOver ? box.y + box.h - buttonH : box.y + (box.h - buttonH) / 2;
    ui.text(f.counts, box.x, foot.countsOver ? box.y : box.y + (box.h - foot.countsH) / 2, layout::smallText, theme::dim, foot.countsW);
    if (ui.button({box.x + box.w - foot.saveW, buttonY, foot.saveW, buttonH}, "Save as a text file"_tr, false, true, layout::smallButtonText)) {
        const std::string file = parentFolder(engine.gameRoot()) + "story.txt", text = storyText(editor);
        d.failed = !SDL_SaveFile(file.c_str(), text.data(), text.size());
        d.status = d.failed ? "Can't write '{}': {}."_tr(file, SDL_GetError()) : "Saved the story as '{}'."_tr(file);
    }
    if (foot.unstopW > 0.0f &&
        ui.button({box.x + box.w - foot.saveW - foot.unstopW, buttonY, foot.unstopW - listGap, buttonH}, foot.unstop, false, true, layout::smallButtonText)) {
        d.stops.clear();
        d.stopsSeen.clear();
        saveStops();
        logInfo("{}", "The story doesn't stop at any line now."_tr());
    }
    return false;
}

// The words here and in the game, how long they take to read, and (once there have been plays) how many
// lines here never ran in one: what's said under the lines. Lines the game never ran in the developer's
// plays are marked in grey in the list; a line with lines in it (an option, an if) counts as run when any
// of them was.
std::string Developer::storyCounts(const StoryFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Row = StoryEditor::Row;
    const auto& scene = f.scene;
    const auto& rows = f.rows;
    const std::string scenePath = editor.path(scene.file);
    auto reachedLine = [&](std::size_t line) {
        const auto text = editor.lineText(scenePath, line);
        return text && engine.reached_.contains(scenePath + "\t" + *text);
    };
    auto marks = [](const Row& row) { return row.kind != Kind::Visual; };
    auto rowReached = [&](const Row& row) {
        for (std::size_t line = row.line; line < std::max(row.end, row.line + 1); ++line)
            if (reachedLine(line)) return true;
        return false;
    };
    const bool showReach = !engine.reached_.empty();
    if (d.listVersion != editor.version() || d.listScene != d.scene || d.listReached != engine.reachedGeneration_) { // (found again after a change, not every frame)
        d.listVersion = editor.version();
        d.listScene = d.scene;
        d.listReached = engine.reachedGeneration_;
        d.rowSummaries.clear();
        d.rowRan.clear();
        d.neverRan = 0;
        std::string summariesFor = d.toolsLanguage;
        for (const StoryEditor::Character& c : editor.characters()) summariesFor += '\n' + c.name;
        if (summariesFor != d.summariesFor) {
            d.summariesFor = std::move(summariesFor);
            d.summariesKept.clear();
        }
        std::map<std::pair<Kind, std::string>, std::string> summaries; // (this scene's, kept for the next time)
        for (const Row& r : rows) {
            std::pair<Kind, std::string> key{r.kind, r.text};
            const auto kept = d.summariesKept.find(key);
            std::string said = kept != d.summariesKept.end() ? kept->second : editor.summary(r);
            d.rowSummaries.push_back(said);
            summaries.insert_or_assign(std::move(key), std::move(said));
            d.rowRan.push_back(!marks(r) || rowReached(r));
            if (!d.rowRan.back()) ++d.neverRan;
        }
        d.summariesKept = std::move(summaries);
    }
    if (d.wordsVersion != editor.version()) { // (counted again after a change: in the scenes it changed)
        d.wordsVersion = editor.version();
        std::string names;
        for (const StoryEditor::Character& c : editor.characters()) names += c.name + '\n';
        if (names != d.wordsNames) {
            d.wordsNames = std::move(names);
            d.wordsKept.clear();
        }
        std::map<std::pair<std::size_t, std::size_t>, DeveloperTools::KeptWords> kept; // (the scenes there are now)
        d.gameWords = 0;
        for (const auto& s : editor.scenes()) {
            auto& one = kept[{s.file, s.line}];
            if (const auto was = d.wordsKept.find({s.file, s.line}); was != d.wordsKept.end()) one = was->second;
            if (one.stamp != editor.fileStamp(s.file)) { // (its file changed: its rows, if they did)
                const auto& sceneRows = editor.rows(s);
                std::size_t said = sceneRows.size();
                for (const Row& r : sceneRows) said = said * 31 + std::hash<std::string>{}(r.text) + static_cast<std::size_t>(r.kind);
                if (said != one.rows) {
                    one.rows = said;
                    one.words = 0;
                    for (const Row& r : sceneRows) one.words += rowWords(editor, r);
                }
                one.stamp = editor.fileStamp(s.file);
            }
            d.gameWords += one.words;
        }
        d.wordsKept = std::move(kept);
    }
    if (d.sceneWordsVersion != editor.version() || d.sceneWordsScene != d.scene) { // (counted again after a change)
        d.sceneWordsVersion = editor.version();
        d.sceneWordsScene = d.scene;
        d.sceneWords = 0;
        for (const Row& r : rows) d.sceneWords += rowWords(editor, r);
    }
    const std::size_t never = showReach ? d.neverRan : 0;
    const std::string time = readingTime(d.gameWords);
    std::string counts = d.sceneWords == 1 ? "1 word here, {} in the game: {} to read"_tr(d.gameWords, time) : "{} words here, {} in the game: {} to read"_tr(d.sceneWords, d.gameWords, time);
    if (showReach)
        counts += never == 0 ? "; every line here has run in a play"_tr() : never == 1 ? "; 1 line here has never run in a play"_tr() : "; {} lines here have never run in a play"_tr(never);
    return counts;
}

// The scene's lines: chosen (Shift+click: more, or one less, to move, copy, cut and delete together),
// moved, deleted (true: changed).
bool Developer::drawRowList(const StoryFrame& f, const SDL_FRect& rowsBox) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    using Row = StoryEditor::Row;
    using ListKind = DeveloperUi::ListEvent::Kind;
    const auto& scene = f.scene;
    const auto& rows = f.rows;
    const float rowH = layout::listItemH * k, indent = layout::listIndent * k;
    const std::string scenePath = editor.path(scene.file);
    const bool showReach = !engine.reached_.empty();
    // A line just added: in sight.
    if (d.rowInSight) {
        if (const auto row = std::ranges::find(rows, *d.rowInSight, &Row::line); row != rows.end()) {
            const float top = static_cast<float>(row - rows.begin()) * rowH;
            d.linesScroll = std::clamp(d.linesScroll, std::min(top, top + rowH - rowsBox.h), top);
        }
        d.rowInSight.reset();
    }
    // The story under the developer tools (opened over the game, or stopped at a line) is at a row:
    // it's outlined.
    const std::optional<std::size_t> storyAtRow = storyRow(rows, scenePath);
    const auto rowEvent = ui.editList(
        "lines", rowsBox, d.linesScroll,
        {.count = rows.size(),
         .label = [&](std::size_t i) { return editor.summary(rows[i]); },
         .chosen = [&](std::size_t i) { return f.isChosen(i); },
         .wrong = [&](std::size_t i) { return d.problemAt(scene.file, rows[i].line) != nullptr; },
         .faint = {},
         .renames = {},
         .deletes = [](std::size_t) { return true; },
         .moves = [](std::size_t) { return true; },
         .unreached = [&](std::size_t i) { return showReach && i < d.rowRan.size() && !d.rowRan[i]; },
         .drops = {},
         .place = // anywhere it fits; dragging it right or left puts it further in or out (into an option, out of an if)
             [&](std::size_t item, std::size_t slot, float dx) -> std::optional<int> {
                 const int want = rows[item].depth + static_cast<int>(std::lround(dx / (indent * 2)));
                 std::optional<int> best;
                 for (int depth = 0; depth <= (rows.empty() ? 0 : std::ranges::max(rows, {}, &Row::depth).depth + 1); ++depth)
                     if (editor.fits(rows, item, slot, depth) && (!best || std::abs(depth - want) < std::abs(*best - want))) best = depth;
                 return best;
             },
         .indentStep = indent,
         .indent = [&](std::size_t i) { return static_cast<float>(rows[i].depth) * indent; },
         .paint =
             [&](std::size_t i, const SDL_FRect& box) {
                 const Row& row = rows[i];
                 if (storyAtRow == i) { // (the story is at it: two lines round it)
                     draw::outline(ui.r, box, kAccent);
                     draw::outline(ui.r, {box.x + k, box.y + k, box.w - 2 * k, box.h - 2 * k}, kAccent);
                 }
                 ui.textFitted(tr(StoryEditor::kindName(row.kind)), box.x + 10 * k, box.y + box.h / 2, layout::smallText, kindColour(row.kind), 82 * k);
                 std::string summary = i < d.rowSummaries.size() ? d.rowSummaries[i] : editor.summary(row);
                 float sx = box.x + 96 * k;
                 auto colourShown = [&] { // (a background's, or a colour's box's: show leaf as panel)
                     if (StoryEditor::asBackground(row.text)) return true;
                     const auto w = rawWords(row.text);
                     for (std::size_t j = 2; j + 1 < w.size(); ++j)
                         if (lower(w[j]) == "as") return editor.colourBox(unquote(w[1]), unquote(w[j + 1]));
                     return false;
                 };
                 if (row.kind == Kind::Visual && colourShown()) { // (a colour: a square and what it's called, never its code, as its box shows it)
                     std::string named;
                     bool swatched = false;
                     for (std::string_view word : wordsOf(summary)) {
                         const bool comma = word.ends_with(',');
                         if (comma) word.remove_suffix(1);
                         const auto colour = swatched ? std::nullopt : parseColour(word, &engine.palette()); // (the first: the background's)
                         const auto grad = colour ? parseGradient(word, &engine.palette()) : std::nullopt;
                         if (colour) sx = ui.swatch(sx, box.y + box.h / 2, *colour, false, grad ? std::optional(grad->second) : std::nullopt), swatched = true;
                         named += (named.empty() ? "" : " ") + (colour ? colourWords(word, &engine.palette()) : std::string(word)) + (comma ? "," : "");
                     }
                     summary = named;
                 }
                 // A line with Stop here: a red dot at its end (its words stop before it).
                 const bool stop = hasStop(scenePath, row.line);
                 ui.textFitted(summary, sx, box.y + box.h / 2, layout::listText, theme::text, box.x + box.w - (stop ? 22 : 8) * k - sx);
                 if (stop) draw::circle(ui.r, box.x + box.w - 11 * k, box.y + box.h / 2, 5 * k, kBad);
             }});
    switch (rowEvent.kind) {
    case ListKind::Chosen: {
        const std::size_t line = rows[rowEvent.item].line;
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) && d.row && *d.row != line) { // (one more chosen, or one less)
            if (const auto it = std::ranges::find(d.moreRows, line); it != d.moreRows.end()) d.moreRows.erase(it);
            else d.moreRows.push_back(line);
            d.listDrag.reset();
            break;
        }
        if (!d.moreRows.empty() && f.isChosen(rowEvent.item)) { // (they stay chosen while it's dragged; let go, it's the only one)
            if (d.row && d.row != line) {
                std::erase(d.moreRows, line);
                d.moreRows.push_back(*d.row);
                d.row = line;
            }
            d.collapseTo = line;
            break;
        }
        f.choose(line, {});
        break;
    }
    case ListKind::Deleted:
        if (!d.moreRows.empty() && f.isChosen(rowEvent.item)) editor.removeRows(scene, f.chosenLines());
        else editor.remove(scene, rows[rowEvent.item]);
        f.choose({}, {});
        return true;
    case ListKind::Moved:
        if (!d.moreRows.empty() && f.isChosen(rowEvent.item)) {
            if (const auto moved = editor.moveRowsTo(scene, f.chosenLines(), rowEvent.to, rowEvent.depth); !moved.empty()) {
                f.choose(moved.front(), {});
                d.moreRows.assign(moved.begin() + 1, moved.end());
            }
        } else if (const auto to = editor.moveRowTo(scene, rows[rowEvent.item], rowEvent.to, rowEvent.depth)) {
            f.choose(*to, {});
        }
        return true;
    case ListKind::Renamed:
    case ListKind::Opened:
    case ListKind::Closed:
    case ListKind::None:
        break;
    }
    if (d.collapseTo && !d.held && !(d.listDrag && d.listDrag->moving)) f.choose(*d.collapseTo, {});
    // The tools' delete key (Delete, Backspace) deletes the chosen lines, after a click in their list.
    if (d.focus.empty() && d.lastList == "lines" && d.row) {
        d.acts.remove = true;
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key)
            if (d.isKey(*key, ToolKey::Delete)) {
                d.keys.erase(key);
                editor.removeRows(scene, f.chosenLines());
                f.choose({}, {});
                return true;
            }
    }
    return false;
}

// Copy, cut, paste and duplicate (their controls' keys: Cmd/Ctrl+C, X, V and D at first) the chosen line, scene, visual
// or sound type of the game's own, character, named visual or named sound (in the list clicked last), while nothing's
// typed into; they go through the clipboard as script lines (true: done).
bool Developer::storyClipboard(const StoryFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const auto& scene = f.scene;
    const StoryEditor::Row* selected = f.selected;
    constexpr std::size_t prebuilt = kVisualTypes.size();
    const StoryEditor::Definition* type = d.lastList == "types" && d.visualType && f.types[*d.visualType].own ? &*f.types[*d.visualType].own : nullptr;
    const StoryEditor::Character* character = d.lastList == "types" && d.character ? &f.characters[*d.character] : nullptr;
    const StoryEditor::Definition* visual = d.lastList == "types" && d.namedVisual ? &f.visuals[*d.namedVisual] : nullptr;
    const StoryEditor::Definition* soundType =
        d.lastList == "sound_types" && d.soundType && f.soundTypes[*d.soundType].own ? &*f.soundTypes[*d.soundType].own : nullptr;
    const StoryEditor::Definition* sound = d.lastList == "sound_types" && d.namedSound ? &f.sounds[*d.namedSound] : nullptr;
    const bool onScene = d.lastList == "scenes", onCharacter = character != nullptr;
    const StoryEditor::Definition* onDefinition = type ? type : visual ? visual : soundType ? soundType : sound; // (one line of the game's own, and what's under it)
    const bool onRow = !onScene && d.lastList != "types" && d.lastList != "sound_types" && selected;
    if (d.focus.empty()) {
        d.acts.cut = d.acts.copy = d.acts.duplicate = onRow || onScene || onCharacter || onDefinition;
        d.acts.paste = SDL_HasClipboardText();
    }
    if (d.focus.empty())
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key) {
            const char what = clipboardKey(d, *key);
            if (!what) continue;
            d.keys.erase(key);
            std::string text;
            if (onRow) text = d.moreRows.empty() ? editor.textOf(scene.file, selected->line, selected->end) : editor.textOfRows(scene, f.chosenLines());
            else if (onScene) text = editor.textOf(scene.file, scene.line, scene.end);
            else if (onCharacter) text = editor.textOf(character->file, character->line, character->line + 1);
            else if (onDefinition) text = editor.textOf(onDefinition->file, onDefinition->line, onDefinition->end);
            if (what == 'c' || what == 'x') {
                if (text.empty()) return true;
                SDL_SetClipboardText(text.c_str());
                d.clipText = text;
                d.clipKind = onRow ? selected->kind : Kind::Other;
                if (what == 'x' && onRow) editor.removeRows(scene, f.chosenLines()), f.choose({}, {});
                else if (what == 'x' && onScene && scene.name != "start") editor.removeScene(d.scene), d.scene = d.scene > 0 ? d.scene - 1 : 0, f.choose({}, {});
                else if (what == 'x' && onCharacter) editor.removeCharacter(*character), f.choose({}, {});
                else if (what == 'x' && onDefinition) editor.removeDefinition(*onDefinition), f.choose({}, {});
                return true;
            }
            std::vector<std::string> lines;
            if (what == 'd') {
                lines = linesOf(text);
                d.clipKind = onRow ? selected->kind : Kind::Other;
            } else {
                lines = clipboardLines();
            }
            if (lines.empty()) return true;
            const std::string first = lower(firstWord(lines.front()));
            if (first == "scene") {
                d.scene = editor.pasteScene(d.scene, lines);
                f.choose({}, {});
            } else if (first == "visual") { // (after the chosen one, under a name no other visual has)
                if (const std::size_t i = editor.pasteDefinition("visual", visual, lines); i < editor.definitions("visual").size()) f.choose({}, {}, {}, i);
            } else if (first == "sound") { // (after the chosen one, under a name no other sound has)
                if (const std::size_t i = editor.pasteDefinition("sound", sound, lines); i < editor.definitions("sound").size()) f.choose({}, {}, {}, {}, {}, i);
            } else if (first == "type" && StoryEditor::soundTypeLine(lines.front())) { // (a sound type's: after the chosen one, under a name no other type has)
                if (const std::size_t i = editor.pasteDefinition("sound type", soundType, lines); i < editor.definitions("sound type").size())
                    f.choose({}, {}, {}, {}, i + kBuiltInSoundTypes.size());
            } else if (first == "type") { // (after the chosen one, under a name no other type has)
                if (const std::size_t i = editor.pasteDefinition("type", type, lines); i < editor.definitions("type").size()) f.choose({}, i + prebuilt);
            } else if (first == "character" && lines.size() == 1 && !f.characters.empty()) { // (after the chosen one, else the last, under a name no other has)
                f.choose({}, {}, editor.pasteCharacter(d.character.value_or(f.characters.size() - 1), lines.front()));
            } else { // lines: after the chosen one (into it, if it holds lines and they aren't its own kind)
                const std::string pasted = joined(lines, 0, "\n"); // (the lines copied here: their kind is known)
                const Kind copied = what == 'd' || pasted == d.clipText ? d.clipKind : Kind::Other;
                const bool options = copied == Kind::Option, phone = copied == Kind::Chat || copied == Kind::Call; // (a chat in a chat, a call in a call: after it)
                bool inside = false;
                if (selected && what != 'd') // (where New line puts a line)
                    inside = selected->kind == Kind::If || selected->kind == Kind::Else || selected->kind == Kind::Together ||
                             (selected->kind == Kind::Option && !options) || (selected->kind == Kind::Choice && options) ||
                             ((selected->kind == Kind::Chat || selected->kind == Kind::Call || selected->kind == Kind::Missed) && !phone && !options);
                f.choose(editor.add(scene, selected, inside, lines), {});
            }
            return true;
        }
    return false;
}

void Developer::drawStoryForm(const StoryFrame& f, const SDL_FRect& right) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    using Row = StoryEditor::Row;
    using Field = StoryEditor::Field;
    const auto& scene = f.scene;
    const auto& rows = f.rows;
    const Row* selected = f.selected;
    // --- The form, the whole of the room it's given (it scrolls when it's longer than the room it has), under its title
    // row and, for the scene or one of its lines, a row of what plays it.
    const float titleH = layout::titleH * k, boxH = layout::boxH * k, rowGap = layout::rowGap * k, buttonH = layout::buttonH * k;
    const bool plays = !d.visualType && !d.character && !d.namedVisual && !d.soundType && !d.namedSound && d.moreRows.empty();
    const float headH = plays ? 2 * titleH : titleH, playY = right.y + titleH;
    const SDL_FRect formBox{right.x, right.y + headH, right.w, std::max(0.0f, right.h - headH)};
    // The labels' column (as wide as the form's labels need: column()), and the boxes after it.
    float labelW = std::round(right.w * layout::labelPart), fieldX = right.x + labelW;
    auto column = [&](const std::vector<Field>& fields, std::initializer_list<std::string_view> more = {}) {
        labelW = labelColumn(ui, std::round(right.w * layout::labelPart), right.w, {fields}, more);
        fieldX = right.x + labelW;
    };
    ui.scrollForm(formBox, boxH + rowGap);
    float y = formBox.y - d.formScroll;

    std::optional<DeveloperUi::Clip> clipped; // (the title row and what plays it stay put; what's under them scrolls)
    auto under = [&] { clipped.emplace(ui, formBox); };
    auto form = [&] {
        if (d.namedVisual) { // a visual named by a visual line, under its type in Visual types: its name, its type and what it shows
            const StoryEditor::Definition def = f.visuals[*d.namedVisual];
            const float toggleW = asTextButton(ui, right.x + right.w, right.y) + layout::gap * k;
            ui.formTitle("Visual: {}"_tr(def.name), right.x, right.y, kAccent, right.w - toggleW);
            under();
            for (std::size_t l = def.line; l < def.end; ++l)
                if (const ScriptError* problem = d.problemAt(def.file, l)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
            const std::string id = std::format("def:visual:{}", def.line);
            if (d.asText) { // its line as the script has it
                const auto lines = editor.linesOf(def);
                if (const auto text = drawRawLine(ui, id + ":raw", lines.empty() ? std::string{} : lines.front(), right.x, right.w, y))
                    editor.setDefinition(def, {*text}, id + ":raw" + std::to_string(d.focusCount));
                return;
            }
            // (its size, a colour's box's: one of a type taking colours, not the background, showing a colour)
            auto box = [&](const std::vector<Field>& now) {
                return editor.colourBox(fieldValue(now, "Shows").empty() ? def.name : fieldValue(now, "Shows"), fieldValue(now, "Type").empty() ? "image" : fieldValue(now, "Type"));
            };
            // (how it comes: its layer its type's unless it says, no layer or look for a background, which is the whole stage;
            // a movie's playing only for a movie)
            auto fields = editor.fields("visual", def);
            const bool sized = box(fields);
            const std::string kind = fieldValue(fields, "Type").empty() ? std::string("image") : lower(fieldValue(fields, "Type"));
            const std::string shows = fieldValue(fields, "Shows").empty() ? def.name : fieldValue(fields, "Shows");
            const bool back = kind == "background", movie = editor.visualKindOf(shows) == "movie" || editor.typeAccepts(kind) == "movie";
            static constexpr std::array<std::string_view, 6> kLook{"Layer", "Zoom", "Opacity", "Rotation", "Flip", "Tint"};
            for (Field& field : fields)
                if (field.name == "Shows") {
                    field.hint = def.name;                                                      // (empty: the one named as it)
                    if (editor.typeAccepts(editor.namedVisualType(def.name)) == "movie") field.box = Box::Movie; // (a movie's: the movies alone)
                } else if (field.name == "Width" || field.name == "Height") {
                    field.hidden = !sized;
                } else if (std::ranges::contains(kLook, std::string_view(field.name))) {
                    field.hidden = back;
                    if (field.name == "Layer") field.hint = std::to_string(editor.typeLayer(kind));
                } else if (field.name == "Loop" || field.name == "Volume") {
                    field.hidden = !movie;
                } else if (field.name == "Animation on show" && back) {
                    field.hint = "At once"_tr;
                }
            showGameSettingValues(fields, engine.gameSettings()); // (as its show lines say them: what Settings has)
            column(fields);
            if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
                if (edit->name == "Name" && edit->typed) { // (renamed: the show and hide lines of it follow)
                    if (!trim(edit->value).empty())
                        d.saidRenamed(editor.renameDefinition("visual", *d.namedVisual, std::string(trim(edit->value)), std::format("{}:Name{}", id, d.focusCount)),
                                      "A visual's name is one word (letters, numbers, _ and -) that no other visual has."_tr);
                } else {
                    std::ranges::find(fields, edit->name, &Field::name)->value = edit->value;
                    if (!box(fields)) // (no colour's box now: no size)
                        for (Field& field : fields)
                            if (field.name == "Width" || field.name == "Height") field.value.clear();
                    editor.setDefinition(def, editor.linesOf("visual", fields), edit->typed ? std::format("{}:{}{}", id, edit->name, d.focusCount) : std::string{});
                }
            }
            return;
        }
        if (d.character) { // a character, under character in Visual types: their settings (what they're called, their name's colour, their voice...)
            const StoryEditor::Character c = f.characters[*d.character];
            const float toggleW = asTextButton(ui, right.x + right.w, right.y) + layout::gap * k;
            ui.formTitle("Character: {}"_tr(c.name), right.x, right.y, kAccent, right.w - toggleW);
            under();
            if (d.asText) { // its line as the script has it
                const std::string id = std::format("character:{}:raw", c.line);
                const StoryEditor::Definition line{.file = c.file, .line = c.line, .end = c.line + 1, .name = c.name};
                const auto lines = editor.linesOf(line);
                if (const auto text = drawRawLine(ui, id, lines.empty() ? std::string{} : lines.front(), right.x, right.w, y))
                    editor.setDefinition(line, {*text}, id + std::to_string(d.focusCount));
                return;
            }
            if (const ScriptError* problem = d.problemAt(c.file, c.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
            auto fields = editor.fields(c);
            showGameSettingValues(fields, engine.gameSettings());
            const std::string id = std::format("character:{}", c.line);
            column(fields);
            if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
                const auto e = d.edited(id, Kind::Other, fields, edit->typed, edit->name, edit->value);
                d.saidRenamed(editor.setCharacter(c, e.fields, e.key), "Another character has that name."_tr);
                if (edit->name == "Love" && !trim(edit->value).empty()) d.bringLater = "relationships"; // (a love interest: their love shown there)
            }
            return;
        }
        if (d.visualType) { // a visual type: every game's (its layer, as every game has it), or the game's own (its name and layer)
            const StoryFrame::Type& type = f.types[*d.visualType];
            const float toggleW = type.own ? asTextButton(ui, right.x + right.w, right.y) + layout::gap * k : 0.0f;
            ui.formTitle("Visual type: {}"_tr(type.name), right.x, right.y, kAccent, right.w - toggleW);
            under();
            // + New TYPE (at the right of the row under the title, as an image's + Atlas): one of it, listed under it in
            // Visual types and chosen, its form shown: a character, else a visual named of it (a visual line; the story's
            // lines are the Visual line's to add). A type of the game's own that takes only characters has none to make.
            if (type.name == "character" || editor.typeAccepts(type.name) != "character") {
                const std::string plus = "+ New {}"_tr(type.name);
                const float plusW = ui.buttonWidth(plus, layout::buttonText, layout::widerButtonW);
                if (ui.button({right.x + right.w - plusW, y, plusW, buttonH}, plus, false, true, layout::buttonText)) {
                    if (type.name == "character") {
                        f.choose({}, {}, editor.addCharacter());
                    } else {
                        const std::string name = uniqueName("new_" + lower(type.name), [&](const std::string& n) {
                            return editor.defines("visual", n) || !editor.visualKindOf(n).empty(); // (no visual, file or character has it)
                        });
                        f.choose({}, {}, {}, editor.addDefinition("visual", {std::format("visual {} type={}", name, lower(type.name))}));
                    }
                    return;
                }
                y += buttonH + 10 * k;
            }
            if (!type.own) { // (its layer and what it takes, as every game has them: shown, not changed)
                const std::string layer = std::to_string(editor.typeLayer(type.name)), takes = editor.typeAccepts(type.name);
                labelW = labelColumn(ui, std::round(right.w * layout::labelPart), right.w, {}, {"Layer"_tr, "Takes"_tr});
                ui.textFitted("Layer"_tr, right.x, y + boxH / 2, layout::labelText, theme::dim, labelW - 8 * k);
                ui.textFitted(layer, right.x + labelW, y + boxH / 2, layout::boxText, theme::text, right.w - labelW);
                y += boxH + rowGap;
                ui.textFitted("Takes"_tr, right.x, y + boxH / 2, layout::labelText, theme::dim, labelW - 8 * k);
                // (its buttons, greyed, the ones it takes lit: placed as a choice row's are, each as wide as its label, else alike)
                Field taken{.name = "Takes", .value = takes, .box = Box::Accepts, .multi = true};
                applyBox(taken);
                const SDL_FRect row{right.x + labelW, y, right.w - labelW, boxH};
                auto [words, shown, places] = ui.choiceButtons(row, taken);
                const float gap = layout::gap * k, each = std::floor((row.w - gap * static_cast<float>(words.size() - 1)) / static_cast<float>(words.size()));
                for (std::size_t i = 0; i < words.size(); ++i)
                    ui.button(i < places.size() ? places[i] : SDL_FRect{row.x + static_cast<float>(i) * (each + gap), y, each, boxH}, shown[i],
                              std::ranges::contains(wordsOf(takes), words[i]), false, layout::buttonText);
                return;
            }
            const StoryEditor::Definition& def = *type.own;
            for (std::size_t l = def.line; l < def.end; ++l)
                if (const ScriptError* problem = d.problemAt(def.file, l)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
            const std::string id = std::format("def:type:{}", def.line);
            if (d.asText) { // its line as the script has it
                const auto lines = editor.linesOf(def);
                if (const auto text = drawRawLine(ui, id + ":raw", lines.empty() ? std::string{} : lines.front(), right.x, right.w, y))
                    editor.setDefinition(def, {*text}, id + ":raw" + std::to_string(d.focusCount));
                return;
            }
            auto fields = editor.fields("type", def);
            column(fields);
            if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
                if (edit->name == "Name" && edit->typed) { // (renamed: the show lines of its type follow, as a rename in the list does)
                    if (!trim(edit->value).empty())
                        d.saidRenamed(editor.renameDefinition("type", *d.visualType - kVisualTypes.size(), std::string(trim(edit->value)), std::format("{}:Name{}", id, d.focusCount)),
                                      "A type's name is one word (letters, numbers, _ and -) that no other type has."_tr);
                } else {
                    std::ranges::find(fields, edit->name, &Field::name)->value = edit->value;
                    editor.setDefinition(def, editor.linesOf("type", fields), edit->typed ? std::format("{}:{}{}", id, edit->name, d.focusCount) : std::string{});
                }
            }
            return;
        }
        if (d.namedSound) { // a sound named by a sound line, under its type in Sound types: its name, its type, what it plays and how
            const StoryEditor::Definition def = f.sounds[*d.namedSound];
            const float toggleW = asTextButton(ui, right.x + right.w, right.y) + layout::gap * k;
            ui.formTitle("Sound: {}"_tr(def.name), right.x, right.y, kAccent, right.w - toggleW);
            under();
            for (std::size_t l = def.line; l < def.end; ++l)
                if (const ScriptError* problem = d.problemAt(def.file, l)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
            const std::string id = std::format("def:sound:{}", def.line);
            if (d.asText) { // its line as the script has it
                const auto lines = editor.linesOf(def);
                if (const auto text = drawRawLine(ui, id + ":raw", lines.empty() ? std::string{} : lines.front(), right.x, right.w, y))
                    editor.setDefinition(def, {*text}, id + ":raw" + std::to_string(d.focusCount));
                return;
            }
            auto fields = editor.fields("sound", def);
            column(fields);
            if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
                if (edit->name == "Name" && edit->typed) { // (renamed: the lines that play or stop it follow)
                    if (!trim(edit->value).empty())
                        d.saidRenamed(editor.renameDefinition("sound", *d.namedSound, std::string(trim(edit->value)), std::format("{}:Name{}", id, d.focusCount)),
                                      "A sound's name is one word (letters, numbers, _ and -) that no other sound or sound type has."_tr);
                } else {
                    std::ranges::find(fields, edit->name, &Field::name)->value = edit->value;
                    editor.setDefinition(def, editor.linesOf("sound", fields), edit->typed ? std::format("{}:{}{}", id, edit->name, d.focusCount) : std::string{});
                }
            }
            drawRecordTitle(ui, "caption", lower(def.name), right.x, right.w, y, labelW); // (what the player's Captions show as it begins, as a Sound line's form has it)
            return;
        }
        if (d.soundType) { // a sound type: every game's (as every game has it), or the game's own (its name, the volume it follows and how its sounds play)
            const StoryFrame::Type& type = f.soundTypes[*d.soundType];
            const float toggleW = type.own ? asTextButton(ui, right.x + right.w, right.y) + layout::gap * k : 0.0f;
            ui.formTitle("Sound type: {}"_tr(type.name), right.x, right.y, kAccent, right.w - toggleW);
            under();
            // + New TYPE (at the right of the row under the title, as a visual type's): a sound named of it (a sound line; the
            // story's lines are the Sound line's to add), listed under it in Sound types and chosen, its form shown.
            const std::string plus = "+ New {}"_tr(type.name);
            const float plusW = ui.buttonWidth(plus, layout::buttonText, layout::widerButtonW);
            if (ui.button({right.x + right.w - plusW, y, plusW, buttonH}, plus, false, true, layout::buttonText)) {
                const std::string name = uniqueName("new_" + lower(type.name), [&](const std::string& n) {
                    return editor.defines("sound", n) || editor.typeNameTaken(n) || !editor.soundKindOf(n).empty(); // (no sound, type or file has it)
                });
                const std::string of = lower(type.name) == "sound" ? std::string{} : " type=" + lower(type.name);
                f.choose({}, {}, {}, {}, {}, editor.addDefinition("sound", {"sound " + name + of}));
                return;
            }
            y += buttonH + 10 * k;
            if (!type.own) { // (how its sounds play, as every game has them: shown, not changed; not what's the same for all three, the
                             // volume each follows being its own name's and its own volume 100%)
                labelW = labelColumn(ui, std::round(right.w * layout::labelPart), right.w, {}, {"One at a time"_tr, "Loops"_tr});
                auto choices = [&](std::string_view label, Box box, std::string_view chosen) { // (its buttons, greyed: the one it is lit)
                    ui.textFitted(label, right.x, y + boxH / 2, layout::labelText, theme::dim, labelW - 8 * k);
                    const StoryEditor::BoxKind& of = StoryEditor::kindOf(box);
                    const auto words = wordsOf(of.choices);
                    const auto labels = wordsOf(of.labels, '|');
                    const float gap = layout::gap * k, count = static_cast<float>(words.size());
                    const float each = std::floor((right.w - labelW - gap * (count - 1)) / count);
                    for (std::size_t i = 0; i < words.size(); ++i)
                        ui.button({right.x + labelW + static_cast<float>(i) * (each + gap), y, each, boxH}, tr(i < labels.size() ? labels[i] : words[i]), words[i] == chosen, false,
                                  layout::buttonText);
                    y += boxH + rowGap;
                };
                choices("One at a time"_tr, Box::YesNo, editor.soundTypeOption(type.name, "one_at_a_time"));
                choices("Loops"_tr, Box::YesNo, editor.soundTypeOption(type.name, "loop"));
                return;
            }
            const StoryEditor::Definition& def = *type.own;
            for (std::size_t l = def.line; l < def.end; ++l)
                if (const ScriptError* problem = d.problemAt(def.file, l)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
            const std::string id = std::format("def:sound-type:{}", def.line);
            if (d.asText) { // its line as the script has it
                const auto lines = editor.linesOf(def);
                if (const auto text = drawRawLine(ui, id + ":raw", lines.empty() ? std::string{} : lines.front(), right.x, right.w, y))
                    editor.setDefinition(def, {*text}, id + ":raw" + std::to_string(d.focusCount));
                return;
            }
            auto fields = editor.fields("sound type", def);
            column(fields);
            if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
                if (edit->name == "Name" && edit->typed) { // (renamed: the lines that play as it or stop it follow, as a rename in the list does)
                    if (!trim(edit->value).empty())
                        d.saidRenamed(editor.renameDefinition("sound type", *d.soundType - kBuiltInSoundTypes.size(), std::string(trim(edit->value)),
                                                              std::format("{}:Name{}", id, d.focusCount)),
                                      "A type's name is one word (letters, numbers, _ and -) that no other type has."_tr);
                } else {
                    std::ranges::find(fields, edit->name, &Field::name)->value = edit->value;
                    editor.setDefinition(def, editor.linesOf("sound type", fields), edit->typed ? std::format("{}:{}{}", id, edit->name, d.focusCount) : std::string{});
                }
            }
            return;
        }
        if (!selected) { // (no line chosen: the scene, played from its start: its name, and the ending reached as it ends)
            ui.formTitle("Scene: {}"_tr(scene.name), right.x, right.y, kAccent, right.w);
            const std::string play = "Play scene"_tr;
            const float playW = ui.buttonWidth(play, layout::buttonText, layout::toggleW);
            if (ui.button({right.x + right.w - playW, playY, playW, buttonH}, play, false, true, layout::buttonText)) d.pressedTool = kPlayScene;
            under();
            const std::string id = std::format("scene:{}", scene.line);
            const std::vector<Field> fields{{.name = "Name", .value = scene.name, .box = Box::Name},
                                            {.name = "Ending", .value = editor.endingOf(scene.name), .label = "Reaches the ending", .box = Box::Record, .hint = "None",
                                             .context = "ending"}};
            column(fields);
            if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
                const std::string value(trim(edit->value));
                if (edit->name == "Name" && !value.empty()) // (renamed as in the Scenes list: the jumps to it follow)
                    d.saidRenamed(editor.renameScene(d.scene, value, std::format("scene-name:{}:{}", d.scene, d.focusCount)),
                                  "A scene's name is one word (letters, numbers, _ and -) that no other scene has."_tr);
                else if (edit->name == "Ending" && (!edit->typed || value.empty() || hasNamed(editor.definitions("ending"), value))) // (one there: not the letters on the way)
                    editor.setEnding(scene.name, value, std::format("{}:ending{}", id, d.focusCount));
            }
            return;
        }
        if (!d.moreRows.empty()) {
            ui.formTitle("{} lines"_tr(d.moreRows.size() + 1), right.x, right.y);
            return;
        }
        const Row row = *selected;
        const bool asForm = row.kind != Kind::Other;
        const std::string scenePath = editor.path(scene.file);
        const float toggleW = asForm ? asTextButton(ui, right.x + right.w, right.y) + layout::gap * k : 0.0f;
        ui.formTitle(tr(StoryEditor::kindName(row.kind)), right.x, right.y, kindColour(row.kind), right.w - toggleW);
        // What plays it, at the right of the row under the title (the boxes under it, for the arrows): Next line on the line
        // the story under the tools is at (opened over the game there, or stopped there), else Play from here, one width
        // either way (what's after it stays put); and Stop here, for a line that does something.
        const auto storyAtRow = storyRow(rows, scenePath);
        const bool storyHere = storyAtRow && rows[*storyAtRow].line == row.line, stops = d.runLines.contains({scenePath, row.line});
        const std::string next = "Next line"_tr, from = "Play from here"_tr, stop = "Stop here"_tr;
        const float playW = std::max(ui.buttonWidth(next, layout::buttonText, layout::toggleW), ui.buttonWidth(from, layout::buttonText, layout::toggleW));
        const float stopW = stops ? ui.buttonWidth(stop, layout::buttonText, layout::toggleW) : 0.0f, stopX = right.x + right.w - stopW;
        const float playX = stopX - (stops ? layout::gap * k : 0.0f) - playW;
        if (ui.button({playX, playY, playW, buttonH}, storyHere ? next : from, false, true, layout::buttonText)) d.pressedTool = storyHere ? kNextLine : kPlayScene;
        if (stops && ui.button({stopX, playY, stopW, buttonH}, stop, hasStop(scenePath, row.line), true, layout::buttonText)) toggleStop(scenePath, row.line);
        under();
        if (const ScriptError* problem = d.problemAt(scene.file, row.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
        if (d.asText || !asForm) {
            const std::string id = std::format("raw:{}", row.line);
            if (const auto text = drawRawLine(ui, id, row.text, right.x, right.w, y)) editor.setText(scene, row, *text, id + std::to_string(d.focusCount));
            return;
        }
        auto fields = editor.fields(row);
        showGameSettingValues(fields, engine.gameSettings());
        if (row.kind == Kind::Line && d.script) showUsualVoice(fields, *d.script, editor.path(scene.file), row.line);
        const std::string id = std::format("row:{}", row.line);
        column(fields, {"Caption"_tr});
        if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
            auto e = d.edited(id, row.kind, fields, edit->typed, edit->name, edit->value);
            auto set = [&](std::string_view name, const std::string& value) {
                if (const auto at = std::ranges::find(e.fields, name, &Field::name); at != e.fields.end()) at->value = value;
            };
            if (row.kind == Kind::Visual && edit->name == "Type") { // (another type: what it shows goes, unless that type can show it too)
                const std::string who = fieldValue(e.fields, "Who"), type = lower(edit->value), takes = editor.typeAccepts(type);
                const std::string known = editor.visualKindOf(who), is = known.empty() && (parseColour(who, &engine.palette()) || parseGradient(who, &engine.palette())) ? "colour" : known;
                const bool fits = fieldValue(e.fields, "Who").empty() || is.empty() || is == type || std::ranges::contains(wordsOf(takes), std::string_view(is));
                if (!fits) set("Who", {}), set("Expression", {});
            }
            const std::string line = editor.lineOf(e.kind, e.fields);
            if (const Kind to = StoryEditor::phoneKind(edit->name == "Phone line" ? edit->value : std::string{}); to != Kind::Other && to != row.kind) {
                auto lines = StoryEditor::phoneLines(to); // (another of the phone's lines: the lines it starts with inside it, in place of blank ones)
                editor.setRowLines(scene, row, line, std::vector(lines.begin() + 1, lines.end()), e.key);
            } else {
                editor.setText(scene, row, line, e.key);
            }
        }
        if (row.kind == Kind::Effect || row.kind == Kind::Weather) drawStageSample(ui, scene, row, fields, id, right.x, right.w, y); // (what it looks like there)
        // An option, with love interests: what picking it adds to each one's love (a set line, first in it: a row each,
        // under Love; none: nothing).
        if (const auto loves = row.kind == Kind::Option ? editor.loveInterests() : std::vector<std::pair<std::string, std::string>>{}; !loves.empty()) {
            std::vector<Field> love;
            for (const auto& [name, variable] : loves) {
                const int adds = editor.loveIn(scene, row, variable);
                love.push_back({.name = variable, .value = adds == 0 ? std::string{} : std::format("{:+}", adds), .label = name, .box = Box::Number, .hint = "None",
                                .group = love.empty() ? std::string("Love"_group) : std::string{}});
            }
            y += rowGap;
            if (const auto edit = drawForm(ui, id + ":love", love, right.x, right.w, labelW, y)) {
                const auto n = parseNumber(trim(edit->value));
                editor.setLove(scene, row, edit->name, n ? static_cast<int>(std::lround(*n)) : 0, std::format("{}:love:{}{}", id, edit->name, d.focusCount));
            }
        }
        // A sound's caption (what the player's Captions show as it begins): its caption line, as its form in the Library has it.
        if (row.kind == Kind::Sound && fieldValue(fields, "Play or stop") == "play")
            if (const std::string sound = lower(trim(fieldValue(fields, "Sound"))); !sound.empty() && !sound.contains('{'))
                drawRecordTitle(ui, "caption", sound, right.x, right.w, y, labelW);
        // Enter in a line's words starts the next line, said by the same one (a chat's message: written by the
        // same one); in an option's text, the next option.
        if (d.entered == id + ":Words" && row.kind == Kind::Line) {
            const std::string speaker = fieldValue(fields, "Speaker");
            f.startTyping(editor.add(scene, &row, false, {speaker.empty() ? std::string("\"\"") : speaker + ": "}));
            return;
        }
        if (d.entered == id + ":Text" && row.kind == Kind::Message && fieldValue(fields, "Does") == "says") {
            f.startTyping(editor.add(scene, &row, false, {(fieldValue(fields, "Sent") == "by_player" ? std::string("me") : fieldValue(fields, "Who")) + ": "}));
            return;
        }
        if (d.entered == id + ":Text" && row.kind == Kind::Option) {
            f.startTyping(editor.add(scene, &row, false, {"\"\""}));
            return;
        }
        // A line that holds lines starts empty (no line made up in it); what it holds is added one way, by its form's
        // + New buttons, each at the end of what it holds, ready to type: + New option (a choice), + New message (a
        // chat: from its person), + New line (an option, an if, an else, together, a call, its missed: lines); beside
        // them, an if's + Else (or + Else if, before the last else) and a call's + If nobody answers.
        struct Adding {
            std::string label;
            std::vector<std::string> lines;
            const Row* after;
            bool inside;
        };
        std::vector<Adding> adds;
        const auto at = static_cast<std::size_t>(selected - rows.data());
        if (row.kind == Kind::Choice) adds.push_back({"+ New option"_tr, {"\"\""}, &row, true});
        if (row.kind == Kind::Chat) {
            const std::string who = fieldValue(fields, "Who");
            adds.push_back({"+ New message"_tr, {(who.empty() ? std::string("me") : who) + ": "}, &row, true});
        }
        if (row.kind == Kind::Option || row.kind == Kind::If || row.kind == Kind::Else || row.kind == Kind::Together || row.kind == Kind::Call ||
            row.kind == Kind::Missed)
            adds.push_back({"+ New line"_tr, {"\"\""}, &row, true});
        if (row.kind == Kind::If) {
            const Row* last = &row; // the chain's last "else if" or "else"
            for (std::size_t j = at + 1; j < rows.size() && rows[j].depth >= row.depth; ++j)
                if (rows[j].depth == row.depth) {
                    if (rows[j].kind != Kind::Else) break;
                    last = &rows[j];
                }
            if (last != &row && editor.fields(*last)[0].value.empty()) { // (a plain else last: + Else if goes before it)
                const Row* before = &row;
                for (std::size_t j = at + 1; j < rows.size() && &rows[j] != last; ++j)
                    if (rows[j].depth == row.depth) before = &rows[j];
                adds.push_back({"+ Else if"_tr, {"else if " + std::string(afterFirst(row.text))}, before, false});
            } else {
                adds.push_back({"+ Else"_tr, newLines(Kind::Else), last, false});
            }
        }
        if (row.kind == Kind::Call) {
            bool missed = false; // (a call has its missed: lines already)
            for (std::size_t j = at + 1; j < rows.size() && rows[j].depth > row.depth; ++j) missed = missed || (rows[j].depth == row.depth + 1 && rows[j].kind == Kind::Missed);
            if (!missed) adds.push_back({"+ If nobody answers"_tr, newLines(Kind::Missed), &row, true});
        }
        if (!adds.empty()) {
            float x = fieldX;
            for (const Adding& add : adds) {
                const float w = std::min(right.x + right.w - x, ui.buttonWidth(add.label, layout::buttonText, layout::optionButtonW));
                if (w <= 0.0f) break;
                if (ui.button({x, y, w, boxH}, add.label, false, true, layout::buttonText)) {
                    f.show(editor.add(scene, add.after, add.inside, add.lines));
                    return;
                }
                x += w + layout::gap * k;
            }
            y += boxH + rowGap;
        }
    };
    form();
    ui.formDrawn(formBox, y);
}

// An effect line's or a weather line's sample, under its form: the stage as the story has it at that line (played there
// silently, as Play from here does), with what the form says on it: the effect or the weather pointed at (a button of its
// Effects or its Kind), else the line's own; a stop line, what's there without it. Its weather and its effects' grain go
// on while it's drawn (a new weather comes as if it had been falling a while).
void Developer::drawStageSample(DeveloperUi& ui, const StoryEditor::Scene& scene, const StoryEditor::Row& row, const std::vector<StoryEditor::Field>& fields,
                                const std::string& id, float x, float w, float& y) {
    DeveloperTools& d = tools;
    if (!d.script) return;
    const float k = ui.k, now = engine.toolsTime();
    if (const auto of = std::tuple{d.editor.version(), scene.name, row.line}; d.stageSampleOf != of) {
        d.stageSampleOf = of;
        d.stageSample = playedTo(*d.script, scene, row.line);
        d.stageSampleTried.clear();
        d.stageSampleAt = now;
    }
    if (!d.stageSample) return;
    const auto& pointed = d.choicePointed;
    auto pointedAt = [&](std::string_view field) { // (a button of that row the pointer or the keys are on: its word)
        return pointed.id == std::format("{}:{}", id, field) && d.textFrame - pointed.frame <= 1 ? pointed.word : std::string{};
    };
    auto share = [](std::string_view v, float usual) { // (50% or 0.5)
        v = trim(v);
        const bool percent = v.ends_with('%');
        const auto n = parseNumber(percent ? v.substr(0, v.size() - 1) : v);
        return n ? (percent ? *n / 100.0f : *n) : usual;
    };
    Stage& stage = d.stageSample->stage();
    bool weatherNew = false;
    if (row.kind == StoryEditor::Kind::Effect) {
        cmd::Effect e;
        const std::string words = !pointedAt("Effects").empty() ? pointedAt("Effects") : fieldValue(fields, "What") == "stop_effect" ? std::string{} : fieldValue(fields, "Effects");
        for (const std::string_view word : wordsOf(lower(words)))
            if (const auto look = std::ranges::find(kStageLooks, word); look != kStageLooks.end()) e.looks |= 1u << static_cast<unsigned>(look - kStageLooks.begin());
        e.amount = std::clamp(share(fieldValue(fields, "Amount"), 1.0f), 0.0f, 1.0f);
        if (const std::string tried = std::format("effect {} {}", e.looks, e.amount); tried != d.stageSampleTried) {
            d.stageSampleTried = tried;
            stage.setEffect(e, true);
        }
    } else {
        cmd::Weather weather{.kind = !pointedAt("Weather").empty() ? pointedAt("Weather") : fieldValue(fields, "What") == "stop_weather" ? std::string{} : lower(fieldValue(fields, "Weather"))};
        weather.amount = share(fieldValue(fields, "Amount"), 1.0f);
        weather.speed = share(fieldValue(fields, "Speed"), 1.0f);
        weather.wind = share(fieldValue(fields, "Wind"), 0.0f);
        weather.tint = parseColour(trim(fieldValue(fields, "Tint")), &engine.palette());
        weather.image = std::string(trim(fieldValue(fields, "Image")));
        weather.behind = fieldValue(fields, "Behind people") == "yes";
        const std::string tried = std::format("weather {} {} {} {} {} {} {}", weather.kind, weather.amount, weather.speed, weather.wind,
                                              weather.tint ? std::format("{},{},{},{}", weather.tint->r, weather.tint->g, weather.tint->b, weather.tint->a) : std::string{},
                                              weather.image, weather.behind);
        if (tried != d.stageSampleTried) {
            d.stageSampleTried = tried;
            stage.setWeather(weather, 0.0f);
            weatherNew = true;
        }
    }
    if (weatherNew)
        for (int i = 0; i < 60; ++i) stage.update(0.05f); // (as if it had been falling three seconds)
    stage.update(std::clamp(now - d.stageSampleAt, 0.0f, 0.1f));
    d.stageSampleAt = now;
    const float h = std::round(std::min(w * engine.canvas().h / engine.canvas().w, 260.0f * k)), shownW = std::round(h * engine.canvas().w / engine.canvas().h);
    const SDL_FRect at{x, y, shownW, h};
    draw::rect(ui.r, at, kBox);
    if (paintCanvas([&] {
            const SwappedIn sampleGame(engine.game_, d.stageSample);
            engine.game_->draw(engine.fonts_.get("default", 18));
        }))
        SDL_RenderTexture(ui.r, d.canvas.get(), nullptr, &at);
    draw::outline(ui.r, at, kEdge);
    y += h + layout::rowGap * k;
}

} // namespace vn
