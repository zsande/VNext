// The Story tab: the scenes and characters to choose from, a scene's rows (its lines, as forms) and
// the chosen row's form.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

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

// About how long that many words take to read (200 a minute).
std::string readingTime(std::size_t words) {
    const std::size_t minutes = (words + 100) / 200;
    if (minutes < 1) return "under a minute";
    if (minutes < 60) return std::format("{} min", minutes);
    return minutes % 60 == 0 ? std::format("{} h", minutes / 60) : std::format("{} h {} min", minutes / 60, minutes % 60);
}

// The words a line shows the player (its texts; not who speaks, nor commands).
std::size_t rowWords(const StoryEditor& editor, const StoryEditor::Row& row) {
    if (row.kind == StoryEditor::Kind::Note || row.kind == StoryEditor::Kind::Character) return 0;
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
            if (row.kind == StoryEditor::Kind::Note || row.kind == StoryEditor::Kind::Character) continue;
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
            case StoryEditor::Kind::Choice: {
                const std::string question = value("Question");
                line = question.empty() ? "Choice:" : "Choice: " + question;
                break;
            }
            case StoryEditor::Kind::Option: {
                const std::string to = value("Goes to");
                line = "> " + value("Text") + (to.empty() ? "" : " (-> " + to + ")");
                break;
            }
            case StoryEditor::Kind::Chapter: line = "# " + value("Title"); break;
            default:
                for (const StoryEditor::Field& f : fields)
                    if (f.rich && !trim(f.value).empty()) line += (line.empty() ? std::string(StoryEditor::kindName(row.kind)) + ": " : " ") + withoutTags(f.value);
                break;
            }
            if (!line.empty()) out += std::string(static_cast<std::size_t>(row.depth) * 4, ' ') + line + "\n";
        }
    }
    return out;
}

// The Story tab's buttons that add a line (New line, under the lines), in groups as the Screens tab's
// New part has its parts.
struct Adding {
    std::string_view label;
    Kind kind;
};
constexpr Adding kAddWords[]{{"Line", Kind::Line}, {"Choice", Kind::Choice}, {"Ask", Kind::Ask}, {"Message", Kind::Message}, {"Chapter", Kind::Chapter}, {"Note", Kind::Note}};
constexpr Adding kAddStage[]{{"Character", Kind::Character}, {"Background", Kind::Background}, {"Move", Kind::Move}, {"Shake", Kind::Shake},
                             {"Effect", Kind::Effect},       {"Weather", Kind::Weather},       {"Camera", Kind::Camera}, {"Movie", Kind::Movie}};
constexpr Adding kAddSound[]{{"Music", Kind::Music}, {"Sound", Kind::Sound}, {"Voice", Kind::Voice}, {"Stop", Kind::Stop}};
constexpr Adding kAddFlow[]{{"If", Kind::If}, {"Set", Kind::Set}, {"Jump", Kind::Jump}, {"End", Kind::End}, {"Wait", Kind::Wait}, {"Together", Kind::Together}};
constexpr Adding kAddScreens[]{{"Screen", Kind::Screen}, {"Textbox", Kind::Textbox}, {"New page", Kind::NewPage}, {"Notify", Kind::Notify},
                               {"Phone", Kind::Phone},   {"Unlock", Kind::Unlock},   {"Game", Kind::Game}};
constexpr std::pair<std::string_view, std::span<const Adding>> kAdds[]{
    {"Words", kAddWords}, {"Stage", kAddStage}, {"Sound", kAddSound}, {"Flow", kAddFlow}, {"Screens", kAddScreens}};

SDL_Color kindColor(Kind kind) {
    switch (kind) {
    case Kind::Line:
    case Kind::Together:
    case Kind::Message: return kAccent;
    case Kind::Background:
    case Kind::Effect:
    case Kind::Weather:
    case Kind::Camera:
    case Kind::Movie:
    case Kind::Shake: return {140, 230, 160, 255};
    case Kind::Character:
    case Kind::Move: return {120, 220, 210, 255};
    case Kind::Music:
    case Kind::Sound:
    case Kind::Voice:
    case Kind::Stop: return {200, 165, 255, 255};
    case Kind::Choice:
    case Kind::Option:
    case Kind::Ask: return {255, 190, 120, 255};
    case Kind::Jump:
    case Kind::End:
    case Kind::If:
    case Kind::Else:
    case Kind::Set:
    case Kind::Wait: return kWarn;
    case Kind::Note:
    case Kind::Other: return theme::dim;
    default: return {230, 230, 240, 255};
    }
}

// The voice file a story line plays ("voices/aiko/start_3", in audio/), when its speaker has voices=.
std::string voiceFileAt(const Script& s, const std::string& path, std::size_t line) {
    for (const Instruction& ins : s.code)
        if (ins.line == static_cast<int>(line) + 1 && s.files[ins.file] == path)
            if (const auto* say = std::get_if<cmd::Say>(&ins.command))
                if (const CharacterDef* def = s.character(say->character); def && !def->voices.empty()) return def->voices + "/" + say->voiceId;
    return {};
}

// The lines a new row of `kind` starts as: who or what it's about, and its words, left empty (to pick
// from its list, or to type), not made up.
std::vector<std::string> newLines(Kind kind, const StoryEditor& editor) {
    std::string record = "ending"; // (one the game lists, if any)
    for (const char* k : {"ending", "achievement", "tip", "unlock"})
        if (!editor.names(StoryEditor::Pick::Records, k).empty()) {
            record = k;
            break;
        }
    switch (kind) {
    case Kind::Line: return {"\"\""}; // (blank, to type into)
    case Kind::Choice: return {"choice", "    \"\"", "    \"\""};
    case Kind::Option: return {"\"\""}; // (blank, to type into)
    case Kind::If: return {"if", "    \"\""};
    case Kind::Else: return {"else", "    \"\""};
    case Kind::Set: return {"set"};
    case Kind::Background: return {"scene"};
    case Kind::Character: return {"show"};
    case Kind::Weather: return {"weather rain"};
    case Kind::Together: return {"together", "    \"\"", "    \"\""};
    case Kind::Unlock: return {record};
    case Kind::Message: return {"message"}; // (no form table of its own)
    default:
        if (const CommandForm* form = formOf(kind)) return {std::string(spaced(wordsOf(form->verbs).front()))}; // (its form's first word)
        return {"#"};
    }
}

// The box a new row is typed into (or picked from) first, if it has one: its words, or who or what
// it's about.
std::string_view mainField(Kind kind) {
    switch (kind) {
    case Kind::Line: return "Words";
    case Kind::Option: return "Text";
    case Kind::Choice: return "Question";
    case Kind::Note: return "Note";
    case Kind::Notify:
    case Kind::Message: return kind == Kind::Notify ? "Message" : "Text";
    case Kind::Chapter: return "Title";
    case Kind::Ask: return "Question";
    case Kind::Background: return "Image";
    case Kind::Character:
    case Kind::Move: return "Who";
    case Kind::Set: return "Variable";
    case Kind::Jump: return "Scene";
    case Kind::Music: return "Song";
    case Kind::Sound: return "Sound";
    case Kind::Voice: return "Voice";
    case Kind::Wait: return "Seconds";
    case Kind::Movie: return "Movie";
    case Kind::Screen:
    case Kind::Textbox: return "Screen";
    case Kind::Unlock: return "Name";
    default: return {};
    }
}

// The licenses, line by line; a heading is a line with a line of - or = under it.
struct AboutLine {
    std::string_view text;
    bool heading = false;
};

const std::vector<AboutLine>& aboutLines() {
    static const std::vector<AboutLine> lines = [] {
        std::vector<AboutLine> out;
        for (auto&& part : std::views::split(licenses(), '\n')) out.push_back({std::string_view(part.begin(), part.end())});
        for (std::size_t i = 0; i + 1 < out.size(); ++i)
            out[i].heading = !out[i + 1].text.empty() && out[i + 1].text.find_first_not_of("-=") == std::string_view::npos;
        return out;
    }();
    return lines;
}

std::string versionOf(int v) {
    return std::format("{}.{}.{}", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
}

// The versions of what's built in, at the top of About.
std::string libraries() {
    int ft[3]{}, hb[3]{};
    TTF_GetFreeTypeVersion(&ft[0], &ft[1], &ft[2]);
    TTF_GetHarfBuzzVersion(&hb[0], &hb[1], &hb[2]);
    return std::format("VNext {}: SDL {}, SDL_image {}, SDL_ttf {} (FreeType {}.{}.{}, HarfBuzz {}.{}.{}), SDL_mixer {} and pl_mpeg", kVersion,
                       versionOf(SDL_GetVersion()), versionOf(IMG_Version()), versionOf(TTF_Version()), ft[0], ft[1], ft[2], hb[0], hb[1],
                       hb[2], versionOf(MIX_Version()));
}

} // namespace

// The log, the newest at the bottom; or About from the top: the versions, then the licenses.
void Developer::drawLines(DeveloperUi& ui, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    const float k = ui.k, rowHeight = layout::logRowH * k;
    draw::rect(ui.r, box, kBox);
    const std::size_t pageLines = static_cast<std::size_t>(std::max(1.0f, (box.h - 12 * k) / rowHeight)); // (that fit)
    auto scroll = [&](float lines) { // up is positive: the log goes back to older lines, About up to earlier ones
        const float by = d.view == View::About ? -lines : lines;
        d.scroll = by > 0 ? d.scroll + static_cast<std::size_t>(by) : d.scroll - std::min(d.scroll, static_cast<std::size_t>(-by));
    };
    if (const float turns = ui.wheelOver(box); turns != 0.0f) scroll(turns * 3.0f);
    const auto page = static_cast<float>(pageLines);
    std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
        switch (key.key) {
        case SDLK_PAGEUP: scroll(page); return true;
        case SDLK_PAGEDOWN: scroll(-page); return true;
        case SDLK_UP: scroll(1); return true;
        case SDLK_DOWN: scroll(-1); return true;
        default: return false;
        }
    });
    std::vector<std::pair<std::string, SDL_Color>> lines;
    if (d.view == View::About) {
        const auto& all = aboutLines();
        const std::size_t total = all.size() + 2;
        d.scroll = std::min(d.scroll, total > pageLines ? total - pageLines : 0);
        for (std::size_t i = d.scroll; i < std::min(total, d.scroll + pageLines); ++i) {
            if (i < 2) {
                lines.emplace_back(i == 0 ? libraries() : std::string{}, theme::text);
                continue;
            }
            const AboutLine& line = all[i - 2];
            lines.emplace_back(line.text, line.heading ? kAccent : line.text.starts_with("--") || line.text.starts_with("==") ? theme::dim : theme::text);
        }
    } else {
        const std::size_t total = engine.log_.size();
        d.scroll = std::min(d.scroll, total > pageLines ? total - pageLines : 0);
        for (const auto& entry : engine.log_.newest(pageLines, d.scroll)) {
            const SDL_LogPriority p = entry.priority;
            lines.emplace_back(entry.text, p >= SDL_LOG_PRIORITY_ERROR ? kBad : p == SDL_LOG_PRIORITY_WARN ? kWarn : theme::text);
            std::ranges::replace(lines.back().first, '\n', ' ');
        }
    }
    const DeveloperUi::Clip clipped(ui, box);
    float y = d.view == View::About ? box.y + 6 * k : box.y + box.h - 6 * k - rowHeight * static_cast<float>(lines.size());
    for (const auto& [text, color] : lines) {
        // "story.vnscript line 12: ..." leads to that line (in the developer tools' tabs) when clicked.
        std::optional<std::pair<std::string, std::size_t>> place;
        static constexpr std::string_view kExtension = ".vnscript", kLine = " line ";
        if (const auto at = text.find(std::string(kExtension) + std::string(kLine)); d.view == View::Log && at != std::string::npos && d.editor.loaded()) {
            const auto start = text.find_last_of(" \t:", at);
            const std::size_t from = start == std::string::npos ? 0 : start + 1;
            const std::string name = text.substr(from, at + kExtension.size() - from);
            std::size_t n = 0, i = at + kExtension.size() + kLine.size();
            for (; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i) n = n * 10 + static_cast<std::size_t>(text[i] - '0');
            if (n >= 1) place = std::pair{name.starts_with("scripts/") ? name : "scripts/" + name, n - 1};
        }
        const SDL_FRect row{box.x, y, box.w, rowHeight};
        if (place && ui.over(row)) draw::rect(ui.r, row, kButtonHover);
        ui.text(text, box.x + 10 * k, y, layout::listText, color);
        if (place && ui.click(row) && !goTo(place->first, place->second)) {
            d.failed = true;
            d.status = std::format("{} line {} isn't in a scene, a screen, a setting, a character or the Library: change it in a text editor.",
                                   place->first, place->second + 1);
        }
        y += rowHeight;
    }
}

// The Story tab: the scenes and characters on the left, the chosen scene's lines in the middle, and
// the chosen line (or character) as a form on the right.
// The Story tab as it is this frame: the scene open, its lines and the one chosen, the characters, and
// where the lists, the lines and the form go. Its parts are drawn by drawStoryLists, drawStoryRows and
// drawStoryForm.
struct Developer::StoryFrame {
    DeveloperUi& ui;
    DeveloperTools& d;
    const StoryEditor::Scene& scene;
    const std::vector<StoryEditor::Row>& rows;
    const std::vector<StoryEditor::Character>& characters;
    const StoryEditor::Row* selected;
    SDL_FRect left, middle, right;
    float gap;

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
        const auto now = d.editor.rows(scene);
        const auto row = std::ranges::find(now, line, &StoryEditor::Row::line);
        const std::string_view name = row != now.end() ? mainField(row->kind) : std::string_view{};
        if (name.empty()) return;
        const auto fields = d.editor.fields(*row);
        const auto f = std::ranges::find(fields, name, &StoryEditor::Field::name);
        if (f == fields.end()) return;
        d.focusOn(std::format("row:{}:{}", line, name), f->value);
        StoryEditor::Field boxed = *f; // (as its kind has it)
        applyBox(boxed);
        if (boxed.pick != StoryEditor::Pick::None || !boxed.suggest.empty()) d.openList = d.focus; // (its list opens, to pick from)
    }
};

void Developer::drawStory(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    using Row = StoryEditor::Row;

    const auto& scenes = editor.scenes();
    if (scenes.empty()) {
        ui.text("Empty", area.x, area.y, layout::lineText, theme::dim);
        if (ui.button({area.x, area.y + layout::bigButtonH * k, 200 * k, layout::bigButtonH * k}, "+ New scene")) d.scene = editor.addScene(0);
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
    // Typing into a field of a form: the form as it was when the typing started is what changes
    // (what's typed can't turn the line into another kind halfway).

    const auto [left, middle, right] = ui.columns(area);
    const float gap = layout::columnGap * k;

    StoryFrame f{.ui = ui, .d = d, .scene = scene, .rows = rows, .characters = characters, .selected = selected, .left = left, .middle = middle,
                 .right = right, .gap = gap};
    if (drawStoryLists(f) || drawStoryRows(f)) return; // (something changed: the rest is drawn next frame)
    drawStoryForm(f);
}

bool Developer::drawStoryLists(const StoryFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const auto& scenes = editor.scenes();
    const auto& scene = f.scene;
    const auto& rows = f.rows;
    const auto& characters = f.characters;
    const SDL_FRect& left = f.left;
    // --- The scenes and the characters.
    const std::vector<bool>& wrongScene = d.wrongScenes(); // (as of this version of the scripts)
    const float half = std::round(left.h * 0.56f);
    const float headingH = layout::headingH * k, listButtonH = layout::buttonH * k, listGap = layout::gap * k;
    ui.heading("Scenes", left.x, left.y);
    const SDL_FRect scenesBox{left.x, left.y + headingH, left.w, half - headingH - listButtonH - 2 * listGap};
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
    if (d.listDrag && d.listDrag->list == "rows" && d.listDrag->moving && ui.over(scenesBox) && d.listDrag->item < rows.size()) {
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
                     ui.textAround(scenes[*entries[i].scene].name, box.x + 10 * k, box.y + box.h / 2, layout::listText, theme::text);
                     if (dropOn == entries[i].scene) draw::outline(ui.r, box, kAccent);
                 } else {
                     ui.textAround(editor.fileName(entries[i].file) + ".vnscript", box.x + 6 * k, box.y + box.h / 2, layout::smallText, kAccent);
                 }
             }});
    switch (event.kind) {
    case ListKind::Chosen:
        if (!entries[event.item].scene) break;
        if (d.scene != *entries[event.item].scene) d.rowsScroll = 0.0f;
        d.scene = *entries[event.item].scene;
        f.choose({}, {});
        break;
    case ListKind::Renamed:
        if (trim(event.name).empty()) return true; // (emptied: it keeps its name)
        if (const Entry e = entries[event.item]; !e.scene) {
            d.saidRenamed(editor.renameFile(e.file, event.name, std::format("file-name:{}:{}", e.file, d.focusCount)), "A file's name is one word (letters, numbers, _ and -) that no other script has.");
        } else d.saidRenamed(editor.renameScene(*e.scene, event.name, std::format("scene-name:{}:{}", *e.scene, d.focusCount)), "A scene's name is one word (letters, numbers, _ and -) that no other scene has.");
        return true;
    case ListKind::Deleted:
        if (const Entry e = entries[event.item]; !e.scene) {
            const std::string name = editor.fileName(e.file);
            editor.removeFile(e.file);
            d.scene = 0;
            d.failed = false;
            d.status = std::format("'{}.vnscript' and its scenes are in the Trash now (undo brings them back).", name);
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
    const float bw = (left.w - 6 * k) / 2;
    auto typeName = [&](std::size_t entry, std::string name) { // (the new one's name, ready to type: its entry in the list)
        f.choose({}, {});
        d.focusOn(std::format("scenes:rename:{}", entry), std::move(name));
    };
    if (ui.button({left.x, scenesBox.y + scenesBox.h + listGap, bw, listButtonH}, "+ New scene", false, true, layout::buttonText)) {
        std::size_t entry = 0; // (after the file's last entry)
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].file == scene.file) entry = i + 1;
        d.scene = editor.addScene(scene.file);
        typeName(entry, scenes[d.scene].name);
        return true; // (the lines have moved: drawn again next frame)
    }
    if (ui.button({left.x + bw + listGap, scenesBox.y + scenesBox.h + listGap, bw, listButtonH}, "+ New file", false, true, layout::buttonText)) {
        const std::size_t entry = entries.size() + (byFile ? 0 : 1); // (the last entry, with one per file once there are two)
        typeName(entry, editor.fileName(editor.addFile()));
        return true;
    }
    ui.heading("Characters", left.x, left.y + half);
    const SDL_FRect charactersBox{left.x, left.y + half + headingH, left.w, left.h - half - headingH - listButtonH - listGap};
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
                                      [&](std::size_t i, const SDL_FRect& at) {
                                          float x = at.x + layout::listPad * k;
                                          if (const auto c = d.characterColor(engine.palette(), characters[i].name)) x = ui.swatch(x, at.y + at.h / 2, *c);
                                          ui.textAround(characters[i].name, x, at.y + at.h / 2, layout::listText, theme::text);
                                      }});
    switch (who.kind) {
    case ListKind::Chosen:
        f.choose({}, who.item);
        break;
    case ListKind::Renamed:
        if (!trim(who.name).empty()) {
            auto fields = editor.fields(characters[who.item]);
            fields[0].value = std::string(trim(who.name));
            editor.setCharacter(characters[who.item], fields, std::format("character-name:{}:{}", who.item, d.focusCount));
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
    if (ui.button({left.x, charactersBox.y + charactersBox.h + listGap, left.w, listButtonH}, "+ New character", false, true, layout::buttonText)) {
        const std::size_t c = editor.addCharacter();
        f.choose({}, c);
        const StoryEditor::Character made = editor.characters()[c]; // (its name, ready to type: the form's Name box)
        d.focusOn(std::format("character:{}:Name", made.line), made.name);
        return true;
    }
    return false;
}

bool Developer::drawStoryRows(StoryFrame& f) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const float listGap = layout::gap * k;
    const auto& scene = f.scene;
    const auto& selected = f.selected;
    const SDL_FRect& middle = f.middle;
    StoryEditor& editor = d.editor;
    const auto& rows = f.rows;
    // --- The scene's lines (a click chooses one, its x deletes it, dragging moves it among the lines next
    // to it), New line under them (its buttons, in groups), and under that what the scene comes to.
    std::vector<DeveloperUi::ButtonGroup> addGroups;
    for (const auto& [name, adds] : kAdds) {
        DeveloperUi::ButtonGroup& g = addGroups.emplace_back(DeveloperUi::ButtonGroup{.name = name, .labels = {}});
        for (const Adding& add : adds) g.labels.push_back(add.label);
    }
    const auto addPlaces = ui.grouped(addGroups, middle.w);
    const std::string counts = storyCounts(f);
    const float saveW = ui.buttonWidth("Save as a text file", layout::smallButtonText, layout::widerButtonW);
    const float countsW = std::max(0.0f, middle.w - saveW - layout::columnGap * k), countsH = ui.measure(counts, layout::smallText, countsW).y;
    const float findH = layout::listItemH * k, headingH = layout::headingH * k, bottomH = std::max(countsH, layout::smallButtonH * k);
    if (drawFindBar(f, findH)) return true;
    const SDL_FRect rowsBox{middle.x, middle.y + findH + listGap, middle.w,
                            std::max(0.0f, middle.h - findH - listGap - listGap - headingH - addPlaces.h - listGap - bottomH)};
    const float rowH = layout::listItemH * k;
    // Up and down choose the line before or after (while nothing's typed into).
    if (d.focus.empty())
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            if ((key.key != SDLK_UP && key.key != SDLK_DOWN) || rows.empty()) return false;
            const auto at = static_cast<std::size_t>(selected ? selected - rows.data() : 0);
            const std::size_t to = !selected ? (key.key == SDLK_UP ? rows.size() - 1 : 0)
                                 : key.key == SDLK_UP ? (at == 0 ? 0 : at - 1)
                                                      : std::min(at + 1, rows.size() - 1);
            f.choose(rows[to].line, {});
            const float top = static_cast<float>(to) * rowH; // (in sight)
            d.rowsScroll = std::clamp(d.rowsScroll, top + rowH - rowsBox.h, top);
            return true;
        });
    if (!d.find.empty()) {
        if (d.lookedFind.what != d.find || d.lookedFind.version != editor.version()) // (looked for again when the words or the scripts change)
            d.lookedFind = {.what = d.find, .version = editor.version(), .found = editor.find(d.find)};
        const auto& found = d.lookedFind.found;
        const auto clicked = ui.list(rowsBox, d.findScroll, found.size(),
                                     [&](std::size_t i) { return std::format("{}:  {}", editor.placeOf(found[i].file, found[i].line), found[i].text); },
                                     [](std::size_t) { return false; }, [&](std::size_t i) { return d.problemAt(found[i].file, found[i].line) != nullptr; });
        if (clicked) { // (in its tab)
            goTo(editor.path(found[*clicked].file), found[*clicked].line);
            d.find.clear();
            return true;
        }
    }
    if (drawRowList(f, rowsBox) || storyClipboard(f)) return true;

    // New line: after the chosen line (and what's inside it), or inside a choice's option, an "if" or
    // "else", or "together"; with none chosen, at the scene's end. Its heading says which.
    const bool inside = selected && (selected->kind == Kind::Option || selected->kind == Kind::If || selected->kind == Kind::Else || selected->kind == Kind::Together);
    const float addsY = rowsBox.y + rowsBox.h + listGap;
    ui.heading(!selected ? "New line, at the scene's end" : inside ? "New line, inside the chosen one" : "New line, after the chosen one", middle.x, addsY, middle.w);
    if (const auto clicked = ui.groupedButtons(addGroups, addPlaces, middle.x, addsY + headingH)) {
        std::size_t i = *clicked;
        for (const auto& [name, adds] : kAdds) {
            if (i >= adds.size()) {
                i -= adds.size();
                continue;
            }
            f.startTyping(editor.add(scene, selected, inside, newLines(adds[i].kind, editor)));
            return true; // (the lines have moved: drawn again next frame)
        }
    }
    // Under them: how long the scene and the game are, the lines here never run (see above), and the
    // story as one text file (next to the game folder) for proofreaders and voice actors.
    {
        const float buttonH = layout::smallButtonH * k;
        ui.text(counts, middle.x, middle.y + middle.h - bottomH + (bottomH - countsH) / 2, layout::smallText, theme::dim, countsW);
        if (ui.button({middle.x + middle.w - saveW, middle.y + middle.h - bottomH + (bottomH - buttonH) / 2, saveW, buttonH}, "Save as a text file", false, true,
                      layout::smallButtonText)) {
            const std::string file = parentFolder(engine.gameRoot()) + "story.txt", text = storyText(editor);
            d.failed = !SDL_SaveFile(file.c_str(), text.data(), text.size());
            d.status = d.failed ? std::format("Can't write '{}': {}.", file, SDL_GetError()) : std::format("Saved the story as '{}'.", file);
        }
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
    auto marks = [](const Row& row) { return row.kind != Kind::Note && row.kind != Kind::Character; };
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
    std::string counts = std::format("{} word{} here, {} in the game: {}{} to read", d.sceneWords, d.sceneWords == 1 ? "" : "s", d.gameWords,
                                     time.starts_with("under") ? "" : "about ", time);
    if (showReach)
        counts += never == 0 ? "; every line here has run in a play" : std::format("; {} line{} here {} never run in a play (marked grey)", never, never == 1 ? "" : "s", never == 1 ? "has" : "have");
    return counts;
}

// Find (Cmd/Ctrl+F) in every scene's lines: while something's typed there, the list shows where it is (a
// click goes there), and it can be replaced everywhere at once (true: it was).
bool Developer::drawFindBar(const StoryFrame& f, float findH) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& middle = f.middle;
    if (d.focus.empty())
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            if (key.key == SDLK_ESCAPE && !d.find.empty()) return d.find.clear(), true;
            if (key.key != SDLK_F || !(key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) return false;
            d.focusOn("find", d.find);
            return true;
        });
    const float findLabelW = ui.width("Find", layout::labelText) + 8 * k, replaceAllW = 110 * k;
    const float findW = (d.find.empty() ? middle.w : std::round(middle.w * 0.4f)) - findLabelW;
    ui.textAround("Find", middle.x, middle.y + findH / 2, layout::labelText, theme::dim);
    if (const auto v = ui.field("find", {middle.x + findLabelW, middle.y, findW, findH}, d.find, layout::boxText)) d.find = *v;
    if (!d.find.empty()) {
        const float withX = middle.x + findLabelW + findW + 6 * k, withLabelW = ui.width("Replace with", layout::labelText) + 8 * k;
        ui.textAround("Replace with", withX, middle.y + findH / 2, layout::labelText, theme::dim);
        const SDL_FRect with{withX + withLabelW, middle.y, middle.x + middle.w - replaceAllW - 6 * k - (withX + withLabelW), findH};
        if (const auto v = ui.field("replace", with, d.replaceWith, layout::boxText)) d.replaceWith = *v;
        if (ui.button({middle.x + middle.w - replaceAllW, middle.y, replaceAllW, findH}, "Replace all", false, true, layout::buttonText)) {
            const std::size_t n = editor.replaceAll(d.find, d.replaceWith);
            d.failed = false;
            d.status = n == 1 ? "Replaced it in 1 line." : std::format("Replaced it in {} lines.", n);
            d.find = d.replaceWith;
            d.focus.clear();
            return true;
        }
    }
    return false;
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
    // The preview playing on: the row it's at is lit, and kept in view as it moves.
    std::optional<std::size_t> playingRow;
    if (d.previewPlaying && d.previewAt && d.previewAt->first == scenePath) {
        const std::size_t at = d.previewAt->second;
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].line == at) playingRow = i;
        if (!playingRow) // (a line inside one, with no row of its own)
            for (std::size_t i = 0; i < rows.size(); ++i)
                if (at > rows[i].line && at < rows[i].end) playingRow = i;
        if (playingRow && d.previewRow != playingRow) {
            d.previewRow = playingRow;
            const float top = static_cast<float>(*playingRow) * rowH;
            d.rowsScroll = std::clamp(d.rowsScroll, std::min(top, top + rowH - rowsBox.h), top);
        }
    }
    const auto rowEvent = d.find.empty() ? ui.editList(
        "rows", rowsBox, d.rowsScroll,
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
                 if (playingRow == i) { // (the preview is at it)
                     draw::rect(ui.r, box, SDL_Color{kAccent.r, kAccent.g, kAccent.b, 60});
                     draw::outline(ui.r, box, kAccent);
                 }
                 ui.textAround(StoryEditor::kindName(row.kind), box.x + 10 * k, box.y + box.h / 2, layout::smallText, kindColor(row.kind));
                 std::string summary = i < d.rowSummaries.size() ? d.rowSummaries[i] : editor.summary(row);
                 float sx = box.x + 96 * k;
                 if (row.kind == Kind::Background) { // (a colour: a square and what it's called, never its code, as its box shows it)
                     std::string named;
                     for (std::string_view word : wordsOf(summary)) {
                         const bool comma = word.ends_with(',');
                         if (comma) word.remove_suffix(1);
                         const auto color = parseColor(word, &engine.palette());
                         const auto grad = parseGradient(word, &engine.palette());
                         if (color && named.empty()) sx = ui.swatch(sx, box.y + box.h / 2, *color, false, grad ? std::optional(grad->second) : std::nullopt); // (the first: the background)
                         named += (named.empty() ? "" : " ") + (color ? colorWords(word, &engine.palette()) : std::string(word)) + (comma ? "," : "");
                     }
                     summary = named;
                 }
                 ui.textAround(summary, sx, box.y + box.h / 2, layout::listText, row.kind == Kind::Note ? theme::dim : theme::text);
             },
         .itemH = 30.0f}) : DeveloperUi::ListEvent{};
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
    // Delete (or Backspace) deletes the chosen lines, after a click in their list.
    if (d.focus.empty() && d.lastList == "rows" && d.row)
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key)
            if (key->key == SDLK_DELETE || key->key == SDLK_BACKSPACE) {
                d.keys.erase(key);
                editor.removeRows(scene, f.chosenLines());
                f.choose({}, {});
                return true;
            }
    return false;
}

// Copy, cut, paste and duplicate (Cmd/Ctrl+C, X, V, D) the chosen line, scene or character (in the list
// clicked last), while nothing's typed into; they go through the clipboard as script lines (true: done).
bool Developer::storyClipboard(const StoryFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const auto& scene = f.scene;
    const auto& characters = f.characters;
    const StoryEditor::Row* selected = f.selected;
    if (d.focus.empty())
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key) {
            const char what = clipboardKey(*key);
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
            if (first == "label") {
                d.scene = editor.pasteScene(d.scene, lines);
                f.choose({}, {});
            } else if (first == "character" && lines.size() == 1 && !characters.empty()) {
                f.choose({}, editor.pasteCharacter(d.character.value_or(characters.size() - 1), lines.front()));
            } else { // lines: after the chosen one (into it, if it holds lines and they aren't its own kind)
                const std::string pasted = joined(lines, 0, "\n"); // (the lines copied here: their kind is known)
                const bool options = (what == 'd' || pasted == d.clipText) && d.clipKind == Kind::Option;
                bool inside = false;
                if (selected && what != 'd')
                    inside = selected->kind == Kind::If || selected->kind == Kind::Else || selected->kind == Kind::Together ||
                             (selected->kind == Kind::Option && !options) || ((selected->kind == Kind::Choice || selected->kind == Kind::Message) && options);
                f.choose(editor.add(scene, selected, inside, lines), {});
            }
            return true;
        }
    return false;
}

void Developer::drawStoryForm(const StoryFrame& f) {
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
    const SDL_FRect& right = f.right;
    // --- The preview at the bottom of the right side, and the form over it (it scrolls when it's
    // longer than the room it has).
    const float previewH = d.previewOn ? std::round(std::min(right.h * 0.5f, right.w * engine.canvas().h / engine.canvas().w)) : layout::headingH * k; // (off: its heading)
    drawPreview(ui, {right.x, right.y + right.h - previewH, right.w, previewH}, scene, selected);
    const float titleH = layout::titleH * k, boxH = layout::boxH * k, rowGap = layout::rowGap * k, labelW = std::round(right.w * layout::labelPart);
    // (the preview's heading is over it, or all there is of it when it's off; the form ends a gap above that, as lists do above their buttons)
    const float previewTop = right.y + right.h - previewH - (d.previewOn ? layout::previewHeadH * k : 0.0f);
    const SDL_FRect formBox{right.x, right.y + titleH, right.w, std::max(0.0f, previewTop - layout::gap * k - right.y - titleH)};
    const float fieldX = right.x + labelW, fieldW = right.w - labelW;
    ui.scrollForm(formBox, boxH + rowGap);
    float y = formBox.y - d.formScroll;
    auto label = [&](std::string_view name) { ui.textAround(name, right.x, y + boxH / 2, layout::labelText, theme::dim, labelW - 8 * k); };

    std::optional<DeveloperUi::Clip> clipped; // (the title row stays put; what's under it scrolls)
    auto under = [&] { clipped.emplace(ui, formBox, true); };
    auto form = [&] {
        if (d.character) {
            const StoryEditor::Character c = characters[*d.character];
            ui.formTitle("Character: " + c.name, right.x, right.y);
            asTextButton(ui, right.x + right.w, right.y);
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
            showConfigValues(fields, engine.config(), engine.palette());
            const std::string id = std::format("character:{}", c.line);
            if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
                const auto e = d.edited(id, Kind::Other, fields, edit->typed, edit->name, edit->value);
                editor.setCharacter(c, e.fields, e.key);
            }
            return;
        }
        if (!selected) {
            ui.text("Click a line to change it, or add one with the buttons under the lines. Choose a character on the left to change it.", right.x,
                    right.y, layout::noteText, theme::dim, right.w);
            return;
        }
        if (!d.moreRows.empty()) {
            ui.formTitle(std::format("{} lines", d.moreRows.size() + 1), right.x, right.y);
            ui.text("Drag one to move them all; Delete deletes them, and Cmd/Ctrl+C copies them.", right.x, right.y + 40 * k, layout::noteText, theme::dim, right.w);
            return;
        }
        const Row row = *selected;
        ui.formTitle(StoryEditor::kindName(row.kind), right.x, right.y, kindColor(row.kind));
        const bool asForm = row.kind != Kind::Other;
        if (asForm) asTextButton(ui, right.x + right.w, right.y);
        under();
        if (const ScriptError* problem = d.problemAt(scene.file, row.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
        if (d.asText || !asForm) {
            const std::string id = std::format("raw:{}", row.line);
            if (const auto text = drawRawLine(ui, id, row.text, right.x, right.w, y)) editor.setText(scene, row, *text, id + std::to_string(d.focusCount));
            return;
        }
        auto fields = editor.fields(row);
        showConfigValues(fields, engine.config(), engine.palette());
        if (std::ranges::all_of(fields, &Field::hidden)) { // (nothing to set)
            const std::string_view about = row.kind == Kind::Together ? "The lines inside it happen at the same time. Choose it and add them with the buttons under the lines."
                                         : row.kind == Kind::NewPage  ? "The textbox starts a new page (with textbox page)."
                                                                      : "There's nothing to set.";
            y += ui.text(about, right.x, y, layout::noteText, theme::dim, right.w).y + rowGap;
        }
        const std::string id = std::format("row:{}", row.line);
        if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
            const auto e = d.edited(id, row.kind, fields, edit->typed, edit->name, edit->value);
            editor.setText(scene, row, editor.lineOf(e.kind, e.fields), e.key);
        }
        // Someone with voices=: the file the line plays.
        if (const std::string voice = row.kind == Kind::Line && d.script ? voiceFileAt(*d.script, editor.path(scene.file), row.line) : std::string{};
            !voice.empty()) {
            label("Voice file");
            const auto found = engine.assets().findAudio(voice);
            const bool there = found.has_value();
            ui.textAround(there ? "audio/" + voice : "audio/" + voice + " (not there yet)", fieldX, y + boxH / 2, layout::noteText, there ? theme::text : theme::dim,
                          fieldW - 70 * k);
            if (found) listenButton(ui, {right.x + right.w - layout::listenW * k, y, layout::listenW * k, boxH}, *found);
            y += boxH + rowGap;
        }
        // Enter in a line's words starts the next line, said by the same one; in an option's text, the
        // next option.
        if (d.entered == id + ":Words" && row.kind == Kind::Line) {
            const std::string speaker = fieldValue(fields, "Speaker");
            f.startTyping(editor.add(scene, &row, false, {speaker.empty() ? std::string("\"\"") : speaker + ": "}));
            return;
        }
        if (d.entered == id + ":Text" && row.kind == Kind::Option) {
            f.startTyping(editor.add(scene, &row, false, {"\"\""}));
            return;
        }
        // A choice adds its options, a message its answers, and an "if" an "else" (or another "else if"
        // before the last "else").
        const auto at = static_cast<std::size_t>(selected - rows.data());
        std::optional<std::vector<std::string>> adding;
        const Row* after = &row;
        bool inside = true;
        if (row.kind == Kind::Choice && ui.button({fieldX, y, std::min(fieldW, ui.buttonWidth("+ Option", layout::buttonText, layout::optionButtonW)), boxH}, "+ Option", false, true, layout::buttonText))
            adding = newLines(Kind::Option, editor);
        if (row.kind == Kind::Message && ui.button({fieldX, y, std::min(fieldW, ui.buttonWidth("+ Answer", layout::buttonText, layout::optionButtonW)), boxH}, "+ Answer", false, true, layout::buttonText))
            adding = std::vector<std::string>{"\"\""};
        if (row.kind == Kind::If) {
            const Row* last = &row; // the chain's last "else if" or "else"
            for (std::size_t j = at + 1; j < rows.size() && rows[j].depth >= row.depth; ++j)
                if (rows[j].depth == row.depth) {
                    if (rows[j].kind != Kind::Else) break;
                    last = &rows[j];
                }
            const bool plainElse = last != &row && editor.fields(*last)[0].value.empty();
            if (ui.button({fieldX, y, std::min(fieldW, ui.buttonWidth(plainElse ? "+ Else if" : "+ Else", layout::buttonText, layout::optionButtonW)), boxH}, plainElse ? "+ Else if" : "+ Else", false, true, layout::buttonText)) {
                inside = false;
                if (plainElse) {
                    after = &row; // (before the plain else: after the chain's row before it)
                    for (std::size_t j = at + 1; j < rows.size() && &rows[j] != last; ++j)
                        if (rows[j].depth == row.depth) after = &rows[j];
                    adding = std::vector<std::string>{"else if " + std::string(afterFirst(row.text)), "    \"\""};
                } else {
                    after = last;
                    adding = newLines(Kind::Else, editor);
                }
            }
        }
        if (adding) {
            f.startTyping(editor.add(scene, after, inside, *adding));
            return;
        }
        if (row.kind == Kind::Choice || row.kind == Kind::Message || row.kind == Kind::If) y += boxH + rowGap;
    };
    form();
    ui.formDrawn(formBox, y);
}

// The script lines the game ran in the developer's plays, by their file and text (so they stay known
// when lines move), kept in reached.txt with the player's data: the Story tab marks the others.
void Developer::loadReached() {
    engine.reached_.clear();
    if (const auto text = readTextFile(engine.prefPath() + "reached.txt"))
        for (const std::string& line : linesOf(*text))
            if (!line.empty()) engine.reached_.insert(line);
    ++engine.reachedGeneration_;
}

void Developer::recordReached() {
    if (engine.game_) recordReached(*engine.game_, engine.script());
}

void Developer::recordReached(Game& game, const Script& script) {
    const std::vector<std::size_t> ran = game.takeRan();
    DeveloperTools& d = tools;
    if (!engine.config().developer || !d.editor.loaded() || engine.prefPath().empty()) return;
    bool added = false;
    for (const std::size_t pc : ran) {
        if (pc >= script.code.size()) continue;
        const Instruction& ins = script.code[pc];
        if (ins.file >= script.files.size() || ins.line < 1) continue;
        const std::string& path = script.files[ins.file];
        const auto text = d.editor.lineText(path, static_cast<std::size_t>(ins.line) - 1);
        if (text && !text->empty() && engine.reached_.insert(path + "\t" + *text).second) added = true;
    }
    if (!added) return;
    ++engine.reachedGeneration_;
    std::string out;
    for (const std::string& key : engine.reached_) out += key + "\n";
    SDL_SaveFile((engine.prefPath() + "reached.txt").c_str(), out.data(), out.size());
}

} // namespace vn
