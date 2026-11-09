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
    if (row.kind == StoryEditor::Kind::Character) return 0;
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
            if (row.kind == StoryEditor::Kind::Character) continue;
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
// New line's buttons, in groups: what's said, what's seen, what's heard, and what happens next.
const Adding kAddWords[]{{"Line", Kind::Line}, {"Choice", Kind::Choice}, {"Phone", Kind::Chat}, {"Together", Kind::Together}}; // (a phone line starts as a chat)
const Adding kAddStage[]{{"Character", Kind::Character}, {"Background", Kind::Background},
                             {"Effect", Kind::Effect},       {"Weather", Kind::Weather},       {"Camera", Kind::Camera}, {"Movie", Kind::Movie}};
const Adding kAddSound[]{{"Music", Kind::Music}, {"Sound", Kind::Sound}, {"Voice", Kind::Voice}, {"Stop", Kind::Stop}};
const Adding kAddFlow[]{{"If", Kind::If},     {"Set", Kind::Set},   {"Jump", Kind::Jump},
                            {"Wait", Kind::Wait}, {"Time", Kind::Time}, {"Screen", Kind::Screen}}; // (a screen opened: the story waits for it)
const std::pair<ToolsText, std::span<const Adding>> kAdds[]{{"Words", kAddWords}, {"Stage", kAddStage}, {"Sound", kAddSound}, {"Flow", kAddFlow}};

SDL_Color kindColour(Kind kind) {
    switch (kind) {
    case Kind::Line:
    case Kind::Together:
    case Kind::Message:
    case Kind::Chat:
    case Kind::Call:
    case Kind::Missed: return kAccent; // (the phone's lines: one kind)
    case Kind::Background:
    case Kind::Effect:
    case Kind::Weather:
    case Kind::Camera:
    case Kind::Movie:
    case Kind::Character: return {120, 220, 210, 255};
    case Kind::Music:
    case Kind::Sound:
    case Kind::Voice:
    case Kind::Stop: return {200, 165, 255, 255};
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

// The voice file a story line plays ("voices/aiko/start_3", in sounds/), when its speaker has voice_files=.
std::string voiceFileAt(const Script& s, const std::string& path, std::size_t line) {
    for (const Instruction& ins : s.code)
        if (ins.line == static_cast<int>(line) + 1 && s.files[ins.file] == path)
            if (const auto* say = std::get_if<cmd::Say>(&ins.command))
                if (const CharacterDef* def = s.character(say->character); def && !def->voiceFiles.empty()) return def->voiceFiles + "/" + say->voiceId;
    return {};
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
    case Kind::Background: return {"background"};
    case Kind::Character: return {"show"};
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
    case Kind::Background: return "Image";
    case Kind::Character: return "Who";
    case Kind::Set: return "Variable";
    case Kind::Jump: return "Scene";
    case Kind::Music: return "Song";
    case Kind::Sound: return "Sound";
    case Kind::Voice: return "Voice";
    case Kind::Wait: return "Seconds";
    case Kind::Time: return "How long";
    case Kind::Movie: return "Movie";
    case Kind::Screen: return "Screen";
    default: return {};
    }
}

} // namespace

// The Story page as it is this frame: the scene open, its lines and the one chosen, the characters, and what's
// said under the lines. Its page parts are drawn by drawScenesList, drawCharactersList, drawLinesList, drawNewLine,
// drawWholeStory, drawStoryForm and drawMap, each in the room drawPageParts gives it.
struct Developer::StoryFrame {
    DeveloperUi& ui;
    DeveloperTools& d;
    const StoryEditor::Scene& scene;
    const std::vector<StoryEditor::Row>& rows;
    const std::vector<StoryEditor::Character>& characters;
    const StoryEditor::Row* selected;
    std::string counts; // what's said under the lines (storyCounts)

    // Whether the scene's row i is chosen, and the lines of the chosen rows (the first one's last).
    [[nodiscard]] bool isChosen(std::size_t i) const { return d.row == rows[i].line || std::ranges::contains(d.moreRows, rows[i].line); }
    [[nodiscard]] std::vector<std::size_t> chosenLines() const {
        std::vector<std::size_t> out(d.moreRows);
        if (d.row) out.push_back(*d.row);
        return out;
    }
    // A line (or a character) chosen: the form shows it, and what was chosen with it isn't any more.
    void choose(std::optional<std::size_t> row, std::optional<std::size_t> character) const {
        d.row = row;
        d.moreRows.clear();
        d.collapseTo.reset();
        d.character = character;
        d.asText = false;
        d.armed.clear();
        d.focus.clear();
        d.formScroll = 0.0f;
    }
    // A line just added: chosen, and typed into straight away when it has words to write.
    void startTyping(std::size_t line) const {
        choose(line, {});
        d.rowInSight = line;
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

// The Story page, its page parts each under its title: Scenes over Characters on the left; the chosen scene's Lines
// over New line and Whole story (what's under them) in the middle; and the chosen line's (or character's) form over the
// Map of the scenes on the right.
void Developer::drawStory(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    using Row = StoryEditor::Row;

    // Where the page puts them, side by side: Scenes over Characters; Lines over New line and Whole story; the form over
    // the Map.
    const auto [left, middle, right] = ui.columns(area);
    const float half = std::round(left.h * 0.56f), formH = std::round(right.h * 0.62f);
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
    const auto characters = editor.characters();
    const Row* selected = nullptr;
    if (d.row) {
        const auto it = std::ranges::find(rows, *d.row, &Row::line);
        if (it != rows.end()) selected = &*it;
        else d.row.reset();
    }
    clampIndex(d.character, characters.size());

    StoryFrame f{.ui = ui, .d = d, .scene = scene, .rows = rows, .characters = characters, .selected = selected, .counts = {}};
    f.counts = storyCounts(f);
    const SDL_FPoint chrome = pagePartChrome(k); // (New line and Whole story are as tall as what's in them)
    const float headingH = layout::headingH * k;
    const float newLineH = chrome.y + headingH + ui.newButtonsH(newLineGroups(), middle.w - chrome.x);
    const float footH = chrome.y + headingH + storyFoot(ui, f.counts, d.stops.size(), middle.w - chrome.x).h;
    const float linesH = std::max(chrome.y + headingH + layout::listItemH * k, middle.h - newLineH - footH); // (the rest: a line's room under its title at least)
    std::array pageParts{
        listPagePart(ui, "scenes", "Scenes"_tr, scenesAt, 1, [&](const ListRoom& at) { return drawScenesList(f, at); }),
        listPagePart(ui, "characters", "Characters"_tr, {left.x, left.y + half, left.w, left.h - half}, 1, [&](const ListRoom& at) { return drawCharactersList(f, at); }),
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
        formPagePart(ui, "form", "Form"_tr, {right.x, right.y, right.w, formH}, d.character || selected, [&](const SDL_FRect& box) { return drawStoryForm(f, box), false; }),
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
         .chosen = [&](std::size_t i) { return entries[i].scene == d.scene && !d.character; },
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
    case ListKind::None:
        break;
    }
    if (ui.button(at.rows[0], "+ New scene"_tr, false, true, layout::buttonText)) { // (its name, ready to type: its entry in the list)
        std::size_t entry = 0; // (after the file's last entry)
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].file == scene.file) entry = i + 1;
        d.scene = editor.addScene(scene.file);
        f.choose({}, {});
        d.focusOn(std::format("scenes:rename:{}", entry), scenes[d.scene].name, true); // (all of it chosen: typed over)
        return true; // (the lines have moved: drawn again next frame)
    }
    return false;
}

// The characters, and + New character (true: changed, the rest next frame).
bool Developer::drawCharactersList(const StoryFrame& f, const ListRoom& at) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const auto& characters = f.characters;
    using ListKind = DeveloperUi::ListEvent::Kind;
    const SDL_FRect& charactersBox = at.list;
    // Characters: a double click renames one (and the lines it says), its x deletes it, and dragging
    // moves it among the characters of its file.
    const auto who = ui.editList("characters", charactersBox, d.charactersScroll,
                                 {.count = characters.size(),
                                  .label = [&](std::size_t i) { return characters[i].name; },
                                  .chosen = [&](std::size_t i) { return d.character == i; },
                                  .wrong = [&](std::size_t i) { return d.problemAt(characters[i].file, characters[i].line) != nullptr; },
                                  .faint = {},
                                  .renames = [](std::size_t) { return true; },
                                  .deletes = [](std::size_t) { return true; },
                                  .moves = [](std::size_t) { return true; },
                                  .drops =
                                      [&](std::size_t item, std::size_t slot) {
                                          const std::size_t file = characters[item].file;
                                          return (slot < characters.size() && characters[slot].file == file) || (slot > 0 && characters[slot - 1].file == file);
                                      },
                                  .paint = // (its name colour as a square before its name, as its box shows it)
                                      [&](std::size_t i, const SDL_FRect& row) {
                                          float x = row.x + layout::listPad * k;
                                          if (const auto c = d.characterColour(engine.palette(), characters[i].name)) x = ui.swatch(x, row.y + row.h / 2, *c);
                                          ui.textFitted(characters[i].name, x, row.y + row.h / 2, layout::listText, theme::text, row.x + row.w - layout::listPad * k - x);
                                      }});
    switch (who.kind) {
    case ListKind::Chosen:
        f.choose({}, who.item);
        break;
    case ListKind::Renamed:
        if (!trim(who.name).empty()) {
            auto fields = editor.fields(characters[who.item]);
            fields[0].value = std::string(trim(who.name));
            d.saidRenamed(editor.setCharacter(characters[who.item], fields, std::format("character-name:{}:{}", who.item, d.focusCount)),
                          "Another character has that name."_tr);
        }
        return true;
    case ListKind::Deleted:
        editor.removeCharacter(characters[who.item]);
        f.choose({}, {});
        return true;
    case ListKind::Moved:
        if (const auto to = editor.moveCharacter(who.item, who.to)) f.choose({}, *to);
        return true;
    case ListKind::None:
        break;
    }
    if (ui.button(at.rows[0], "+ New character"_tr, false, true, layout::buttonText)) {
        const std::size_t c = editor.addCharacter();
        f.choose({}, c);
        const StoryEditor::Character made = editor.characters()[c]; // (its name, ready to type: the form's Name box)
        d.focusOn(std::format("character:{}:Name", made.line), made.name, true); // (all of it chosen: typed over)
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
    // F9 (or the tools' own Stop here): Stop here, on or off, for the chosen line (one that does something).
    if (const std::string path = editor.path(scene.file); selected && d.moreRows.empty() && d.runLines.contains({path, selected->line})) {
        d.stoppable = std::pair{path, selected->line};
        if (d.focus.empty())
            std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
                if (!d.isKey(key, ToolKey::StopHere)) return false;
                toggleStop(path, selected->line);
                return true;
            });
    }
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
            f.startTyping(editor.add(scene, selected, inside, lines));
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
    auto marks = [](const Row& row) { return row.kind != Kind::Character; };
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
        for (const Row& r : rows) {
            d.rowSummaries.push_back(editor.summary(r));
            d.rowRan.push_back(!marks(r) || rowReached(r));
            if (!d.rowRan.back()) ++d.neverRan;
        }
    }
    if (d.wordsVersion != editor.version()) { // (counted again after a change)
        d.wordsVersion = editor.version();
        d.gameWords = 0;
        for (const auto& s : editor.scenes())
            for (const Row& r : editor.rows(s)) d.gameWords += rowWords(editor, r);
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
    std::optional<std::size_t> storyRow;
    if (const auto at = storyAt(); at && at->first == scenePath) {
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].line == at->second) storyRow = i;
        for (std::size_t i = 0; i < rows.size() && !storyRow; ++i) // (a line inside one, with no row of its own)
            if (at->second > rows[i].line && at->second < rows[i].end) storyRow = i;
    }
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
                 if (storyRow == i) { // (the story is at it: two lines round it)
                     draw::outline(ui.r, box, kAccent);
                     draw::outline(ui.r, {box.x + k, box.y + k, box.w - 2 * k, box.h - 2 * k}, kAccent);
                 }
                 ui.textFitted(tr(StoryEditor::kindName(row.kind)), box.x + 10 * k, box.y + box.h / 2, layout::smallText, kindColour(row.kind), 82 * k);
                 std::string summary = i < d.rowSummaries.size() ? d.rowSummaries[i] : editor.summary(row);
                 float sx = box.x + 96 * k;
                 if (row.kind == Kind::Background) { // (a colour: a square and what it's called, never its code, as its box shows it)
                     std::string named;
                     for (std::string_view word : wordsOf(summary)) {
                         const bool comma = word.ends_with(',');
                         if (comma) word.remove_suffix(1);
                         const auto colour = parseColour(word, &engine.palette());
                         const auto grad = parseGradient(word, &engine.palette());
                         if (colour && named.empty()) sx = ui.swatch(sx, box.y + box.h / 2, *colour, false, grad ? std::optional(grad->second) : std::nullopt); // (the first: the background)
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
    case ListKind::None:
        break;
    }
    if (d.collapseTo && !d.held && !(d.listDrag && d.listDrag->moving)) f.choose(*d.collapseTo, {});
    // The tools' delete key (Delete, Backspace) deletes the chosen lines, after a click in their list.
    if (d.focus.empty() && d.lastList == "lines" && d.row)
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key)
            if (d.isKey(*key, ToolKey::Delete)) {
                d.keys.erase(key);
                editor.removeRows(scene, f.chosenLines());
                f.choose({}, {});
                return true;
            }
    return false;
}

// Copy, cut, paste and duplicate (their controls' keys: Cmd/Ctrl+C, X, V and D at first) the chosen line, scene or
// character (in the list clicked last), while nothing's typed into; they go through the clipboard as script lines
// (true: done).
bool Developer::storyClipboard(const StoryFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const auto& scene = f.scene;
    const auto& characters = f.characters;
    const StoryEditor::Row* selected = f.selected;
    if (d.focus.empty())
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key) {
            const char what = clipboardKey(d, *key);
            if (!what) continue;
            d.keys.erase(key);
            const bool onScene = d.lastList == "scenes", onCharacter = d.lastList == "characters" && d.character;
            const bool onRow = !onScene && !onCharacter && selected;
            std::string text;
            if (onRow) text = d.moreRows.empty() ? editor.textOf(scene.file, selected->line, selected->end) : editor.textOfRows(scene, f.chosenLines());
            else if (onScene) text = editor.textOf(scene.file, scene.line, scene.end);
            else if (onCharacter) text = editor.textOf(characters[*d.character].file, characters[*d.character].line, characters[*d.character].line + 1);
            if (what == 'c' || what == 'x') {
                if (text.empty()) return true;
                SDL_SetClipboardText(text.c_str());
                d.clipText = text;
                d.clipKind = onRow ? selected->kind : Kind::Other;
                if (what == 'x' && onRow) editor.removeRows(scene, f.chosenLines()), f.choose({}, {});
                else if (what == 'x' && onScene && scene.name != "start") editor.removeScene(d.scene), d.scene = d.scene > 0 ? d.scene - 1 : 0, f.choose({}, {});
                else if (what == 'x' && onCharacter) editor.removeCharacter(characters[*d.character]), f.choose({}, {});
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
            } else if (first == "character" && lines.size() == 1 && !characters.empty()) {
                f.choose({}, editor.pasteCharacter(d.character.value_or(characters.size() - 1), lines.front()));
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
    const auto& characters = f.characters;
    const Row* selected = f.selected;
    // --- The form, the whole of the room it's given (it scrolls when it's longer than the room it has).
    const float titleH = layout::titleH * k, boxH = layout::boxH * k, rowGap = layout::rowGap * k;
    const SDL_FRect formBox{right.x, right.y + titleH, right.w, std::max(0.0f, right.h - titleH)};
    // The labels' column (as wide as the form's labels need: column()), and the boxes after it.
    float labelW = std::round(right.w * layout::labelPart), fieldX = right.x + labelW, fieldW = right.w - labelW;
    auto column = [&](const std::vector<Field>& fields, std::initializer_list<std::string_view> more = {}) {
        labelW = labelColumn(ui, std::round(right.w * layout::labelPart), right.w, {fields}, more);
        fieldX = right.x + labelW;
        fieldW = right.w - labelW;
    };
    ui.scrollForm(formBox, boxH + rowGap);
    float y = formBox.y - d.formScroll;
    auto label = [&](std::string_view name) { // (one line: cut to fit)
        ui.textFitted(name, right.x, y + boxH / 2, layout::labelText, theme::dim, labelW - 8 * k);
    };

    std::optional<DeveloperUi::Clip> clipped; // (the title row stays put; what's under it scrolls)
    auto under = [&] { clipped.emplace(ui, formBox); };
    auto form = [&] {
        if (d.character) {
            const StoryEditor::Character c = characters[*d.character];
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
            auto fields = editor.fields(c);
            showGameSettingValues(fields, engine.gameSettings());
            const std::string id = std::format("character:{}", c.line);
            column(fields);
            if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
                const auto e = d.edited(id, Kind::Other, fields, edit->typed, edit->name, edit->value);
                d.saidRenamed(editor.setCharacter(c, e.fields, e.key), "Another character has that name."_tr);
            }
            return;
        }
        if (!selected) return;
        if (!d.moreRows.empty()) {
            ui.formTitle("{} lines"_tr(d.moreRows.size() + 1), right.x, right.y);
            return;
        }
        const Row row = *selected;
        ui.formTitle(tr(StoryEditor::kindName(row.kind)), right.x, right.y, kindColour(row.kind));
        const bool asForm = row.kind != Kind::Other;
        const std::string scenePath = editor.path(scene.file);
        float titleRight = right.x + right.w; // (the title row's buttons, from its right)
        if (asForm) titleRight -= asTextButton(ui, titleRight, right.y) + layout::gap * k;
        if (d.runLines.contains({scenePath, row.line})) stopHereButton(ui, scenePath, row.line, titleRight, right.y); // (a line that does something)
        under();
        if (const ScriptError* problem = d.problemAt(scene.file, row.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
        if (d.asText || !asForm) {
            const std::string id = std::format("raw:{}", row.line);
            if (const auto text = drawRawLine(ui, id, row.text, right.x, right.w, y)) editor.setText(scene, row, *text, id + std::to_string(d.focusCount));
            return;
        }
        auto fields = editor.fields(row);
        showGameSettingValues(fields, engine.gameSettings());
        const std::string id = std::format("row:{}", row.line);
        column(fields, {"Voice file"_tr, "Caption"_tr});
        if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
            const auto e = d.edited(id, row.kind, fields, edit->typed, edit->name, edit->value);
            const std::string line = editor.lineOf(e.kind, e.fields);
            if (const Kind to = StoryEditor::phoneKind(edit->name == "Phone line" ? edit->value : std::string{}); to != Kind::Other && to != row.kind) {
                auto lines = StoryEditor::phoneLines(to); // (another of the phone's lines: the lines it starts with inside it, in place of blank ones)
                editor.setRowLines(scene, row, line, std::vector(lines.begin() + 1, lines.end()), e.key);
            } else {
                editor.setText(scene, row, line, e.key);
            }
        }
        // A song's or a sound's caption (what the player's Captions show as it begins): its caption line, as its form in
        // the Library has it.
        if (row.kind == Kind::Music || row.kind == Kind::Sound)
            if (const std::string sound = lower(trim(fieldValue(fields, row.kind == Kind::Music ? "Song" : "Sound"))); !sound.empty() && !sound.contains('{'))
                drawRecordTitle(ui, "caption", sound, right.x, right.w, y, labelW);
        // Someone with voice_files=: the file the line plays.
        if (const std::string voice = row.kind == Kind::Line && d.script ? voiceFileAt(*d.script, editor.path(scene.file), row.line) : std::string{};
            !voice.empty()) {
            label("Voice file"_tr);
            const auto found = engine.assets().findSound(voice);
            const bool there = found.has_value();
            ui.textFitted(there ? "sounds/" + voice : "sounds/{} (not there yet)"_tr(voice), fieldX, y + boxH / 2, layout::noteText, there ? theme::text : theme::dim, fieldW - 70 * k);
            if (found) listenButton(ui, {right.x + right.w - layout::listenW * k, y, layout::listenW * k, boxH}, *found);
            y += boxH + rowGap;
        }
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
                    f.startTyping(editor.add(scene, add.after, add.inside, add.lines));
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

} // namespace vn
