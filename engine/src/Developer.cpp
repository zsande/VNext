// What developer mode (setting developer yes, in main.vnscript) adds to the game. The player starts in
// the developer tools, Play starts the game, F10 switches between the two (the story waits
// meanwhile), and where the player would close (Quit, the story's end without a title screen) it
// comes back to them. They are the engine's own, the same in every game, and never in a finished one
// (a pack). Without a game, the player is just them. Their tabs are the visual editor: the game's
// scenes, characters and lines as forms, with a map of the scenes (Story); its screens (Screens); and
// what the scripts list by name, the game's files, its translations and its settings (Library); all are
// written to the files as they change.
//
// This file: opening and closing them, their events, their own part (the buttons at the window's foot)
// and the tab shown, the files they add, and going to a line. Each tab has a file of its own
// (Developer*.cpp).
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

#include <ranges>

namespace vn {

namespace {

// What the tools' own part's buttons do (ToolButton::id; Developer::toolButtons has them in their order).
enum ToolCommand : int {
    kNewGame = 1, kLookAgain, kReload, kPack, kFolder, kQuit,
    kUndo, kRedo, kCut, kCopy, kPaste, kDuplicate, kDelete, kSelectAll,
    kStoryTab, kScreensTab, kLibraryTab, kEditLayout, kResetLayout,
    kPlay, kNextLine, kPlayScene, kStopHere,
    kToolsTranslation, kAbout,
    kToolsLanguage // (+ its place among the language buttons: 0 English, then the translations; last, so they come after every other)
};
constexpr std::array kTabs{std::pair{kStoryTab, View::Story}, std::pair{kScreensTab, View::Screens},
                           std::pair{kLibraryTab, View::Library}}; // (in this order: their keys are tools_story, tools_screens and tools_library)

// What New game writes: game/scripts/main.vnscript.
constexpr std::string_view kNewGameScript = R"vnext(# The game starts here: its settings, then scene start, where the story begins.
setting developer yes

scene start
    This is the first line of the story.
)vnext";

// After the scripts change: a text players read that changed in its place (a line's words, a
// button's label...) keeps its translations. When the texts are the same in number before and after,
// each one changed is changed in translations/*.vnscript as well (or copied, when the old text is still
// shown somewhere), unless a translation file has the new text already. Gives the files changed.
std::vector<std::string> keepTranslations(const std::string& root, Script& before, Script& now) {
    std::vector<std::string> was, is;
    before.forEachText([&](std::string& text, const Script::TextPlace&) { was.push_back(text); });
    now.forEachText([&](std::string& text, const Script::TextPlace&) { is.push_back(text); });
    std::vector<std::pair<std::string, std::string>> changed;
    if (was.size() == is.size())
        for (std::size_t i = 0; i < was.size(); ++i)
            if (was[i] != is[i] && !was[i].empty() && !is[i].empty() && std::ranges::none_of(changed, [&](const auto& c) { return c.first == was[i]; }))
                changed.emplace_back(was[i], is[i]);
    std::vector<std::string> touched;
    if (changed.empty()) return touched;
    const std::set<std::string, std::less<>> shown(is.begin(), is.end());
    auto line = [](const std::string& text) { return text.contains('"') ? text : "\"" + text + "\""; };
    for (const std::string& name : listFiles(root + "translations/", ".vnscript")) {
        if (name.contains('/')) continue;
        const std::string path = root + "translations/" + name;
        const auto text = readTextFile(path);
        if (!text) continue;
        std::vector<std::string> lines = linesOf(*text);
        auto original = [&](const std::string& t) { // the line a text is translated under
            for (std::size_t i = 0; i < lines.size(); ++i)
                if (indentOf(lines[i]) == 0 && !trim(lines[i]).starts_with('#') && unquote(trim(lines[i])) == t) return std::optional(i);
            return std::optional<std::size_t>{};
        };
        bool edited = false;
        for (const auto& [from, to] : changed) {
            const auto at = original(from);
            if (!at || original(to)) continue;
            if (!shown.contains(from)) {
                lines[*at] = line(to);
            } else { // (still shown elsewhere: a copy for the new one, after it)
                std::size_t end = *at + 1;
                while (end < lines.size() && (blank(lines[end]) || indentOf(lines[end]) > 0)) ++end;
                while (end > *at + 1 && blank(lines[end - 1])) --end;
                std::vector<std::string> copy{"", line(to)};
                copy.insert(copy.end(), lines.begin() + static_cast<std::ptrdiff_t>(*at + 1), lines.begin() + static_cast<std::ptrdiff_t>(end));
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(end), copy.begin(), copy.end());
            }
            edited = true;
        }
        if (!edited) continue;
        std::string out;
        for (const std::string& l : lines) out += l + "\n";
        while (out.ends_with("\n\n")) out.pop_back();
        if (SDL_SaveFile(path.c_str(), out.data(), out.size())) touched.push_back(stemOf(name));
    }
    return touched;
}

// A file:// address for the folder `path` (spaces and other letters written as %xx).
std::string fileUrl(std::string_view path) {
    std::string slashed(path);
    std::ranges::replace(slashed, '\\', '/');
    return (path.starts_with('/') ? "file://" : "file:///") + percentEscaped(slashed, "/-_.~:"); // (Windows: file:///C:/...)
}

} // namespace

void Developer::openDeveloper() {
    DeveloperTools& d = tools;
    d.open = true;
    d.stepping = false; // (Next line: it's done, or the tools were opened before it got there)
    d.dropPreviews();
    d.focus.clear();
    d.armed.clear();
    d.row.reset();
    d.character.reset();
    if (engine.noGame()) {
        d.editor.clear();
        return;
    }
    const bool first = !d.editor.loaded();
    const std::string last = first || d.scene >= d.editor.scenes().size() ? "start" : d.editor.scenes()[d.scene].name;
    d.editor.load(engine.gameRoot());
    d.played = d.editor.version();
    if (first && d.editor.loaded()) d.view = View::Story;
    // The Story tab shows where the story is (its line in sight, a few lines under the top), or the
    // scene it showed last.
    const auto& scenes = d.editor.scenes();
    d.linesScroll = 0.0f;
    if (engine.mode() == Engine::Mode::Game && engine.game_->here() < engine.script().code.size()) {
        const Instruction& ins = engine.script().code[engine.game_->here()];
        const std::string& file = engine.script().files[ins.file];
        const auto line = static_cast<std::size_t>(std::max(ins.line - 1, 0));
        for (std::size_t i = 0; i < scenes.size(); ++i)
            if (d.editor.path(scenes[i].file) == file && scenes[i].line < line) {
                d.scene = i;
                d.row = line;
            }
        if (d.row && d.scene < scenes.size()) {
            const auto rows = d.editor.rows(scenes[d.scene]);
            if (const auto row = std::ranges::find(rows, *d.row, &StoryEditor::Row::line); row != rows.end())
                d.linesScroll = std::max(0.0f, static_cast<float>(row - rows.begin() - 4) * layout::listItemH * d.k);
        }
    } else {
        const auto found = std::ranges::find(scenes, last, &StoryEditor::Scene::name);
        d.scene = found != scenes.end() ? static_cast<std::size_t>(found - scenes.begin()) : 0;
    }
}

void Developer::closeDeveloper() {
    DeveloperTools& d = tools;
    d.open = false;
    d.panelDrag.reset();
    showPointer(std::nullopt);
    engine.sound().stopListening();
    d.stopMovie();
    d.focus.clear();
    d.dropPreviews();
    d.canvas.reset();
    if (!engine.noGame() && d.editor.loaded() && d.editor.version() != d.played) engine.reloadRequested_ = true; // (the game shows the changes)
    // A story stopped at a line (Stop here, Next line) goes on, with that line; read again, it goes on
    // from it then.
    if (engine.mode() == Engine::Mode::Game && engine.game_ && engine.game_->stoppedThere() && !engine.reloadRequested_) engine.game_->goOn();
}

void Developer::backToGame() {
    if (!engine.waiting_) return closeDeveloper();
    tools.play = true; // (from the beginning, with the scripts read again)
    engine.reloadRequested_ = true;
}

void Developer::backToEditor() {
    DeveloperTools& d = tools;
    if (engine.mode() != Engine::Mode::Error) engine.leaveStory();
    engine.waiting_ = true;
    engine.storyStarted_ = false;
    openDeveloper();
    d.failed = engine.mode() == Engine::Mode::Error;
    d.status = d.failed ? "The scripts need fixing: their mistakes are marked in red in the tabs."_tr() : std::string{};
}

void Developer::developerEvent(const SDL_Event& e) {
    DeveloperTools& d = tools;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        d.mouseX = e.motion.x;
        d.mouseY = e.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) {
            d.mouseX = e.button.x;
            d.mouseY = e.button.y;
            d.clicked = d.held = true;
            d.doubleClick = e.button.clicks >= 2;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) d.held = false;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        d.mouseX = e.wheel.mouse_x;
        d.mouseY = e.wheel.mouse_y;
        d.wheel += e.wheel.y;
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (!d.focus.empty()) d.typed += e.text.text;
        break;
    case SDL_EVENT_KEY_DOWN:
        d.keys.push_back(e.key);
        break;
    case SDL_EVENT_DROP_FILE: { // (imported into the game)
        const std::scoped_lock _(d.imports.mutex);
        if (e.drop.data) d.imports.files.emplace_back(e.drop.data, std::string{});
        d.imports.dropAt = SDL_FPoint{e.drop.x, e.drop.y};
        break;
    }
    default:
        break;
    }
}

void Developer::runCommand(int command) {
    DeveloperTools& d = tools;
    // A tab; what Undo, Cut and the others like them, and Stop here, do with what's chosen or typed into (as their
    // keys, pressed now); Layout and Reset; the tools' language, and a file to translate them; About.
    auto press = [&](ToolKey which) { // (its first key, so a box that's typed into does what it does; from its button: it's that whatever keys it has)
        SDL_KeyboardEvent e{};
        e.type = SDL_EVENT_KEY_DOWN;
        if (const auto& keys = d.toolKeys[static_cast<std::size_t>(which)]; !keys.empty() && keys.front().device == KeyChord::Device::Keyboard) {
            e.key = keys.front().key;
            e.mod = keys.front().mods;
        }
        e.which = kToolButton;
        e.raw = static_cast<Uint16>(which);
        e.down = true;
        d.keys.push_back(e);
    };
    auto show = [&](View view) {
        if (d.view == view) return;
        d.view = view;
        d.aboutScroll = 0;
        d.focus.clear();
    };
    if (const auto tab = std::ranges::find(kTabs, command, &std::pair<ToolCommand, View>::first); tab != kTabs.end()) return show(tab->second); // (without a game: what to do)
    switch (command) {
    case kAbout: return show(View::About);
    case kUndo: return press(ToolKey::Undo);
    case kRedo: return press(ToolKey::Redo);
    case kCut: return press(ToolKey::Cut);
    case kCopy: return press(ToolKey::Copy);
    case kPaste: return press(ToolKey::Paste);
    case kDuplicate: return press(ToolKey::Duplicate);
    case kDelete: return press(ToolKey::Delete);
    case kSelectAll: return press(ToolKey::SelectAll);
    case kStopHere:
        if (d.view == View::Story && d.stoppable) toggleStop(d.stoppable->first, d.stoppable->second);
        return;
    case kEditLayout: // (what's in the parts takes no pointer while they're moved: nothing's typed into or open then)
        d.editLayout = !d.editLayout;
        d.panelDrag.reset();
        d.focus.clear();
        d.popup.id.clear();
        return;
    case kResetLayout: return resetLayout();
    case kToolsTranslation: return writeToolsTranslation();
    default:
        if (command >= kToolsLanguage) { // (English, or a translation: kept for the next time the tools open)
            const auto i = static_cast<std::size_t>(command - kToolsLanguage);
            const std::string id = i >= 1 && i <= d.toolsLanguages.size() ? d.toolsLanguages[i - 1].id : std::string{};
            useToolsLanguage(id);
            const std::string file = toolsFolder() + "language.txt";
            if (!SDL_SaveFile(file.c_str(), id.data(), id.size())) logWarn("{}", "Can't save the developer tools' language: {}."_tr(SDL_GetError()));
            return;
        }
        break;
    }
    // The rest: what the tools do with the game (or, without one, find it), each saying how it went if it needs to.
    if (engine.noGame() ? command != kNewGame && command != kLookAgain && command != kFolder && command != kQuit : command == kNewGame || command == kLookAgain)
        return;
    d.failed = false;
    d.status.clear();
    d.focus.clear();
    switch (command) {
    case kPlay:
        backToGame();
        break;
    case kNextLine: // (the story under the tools: on to the next line it gets to, and stopped there)
        if (const auto at = storyAt()) {
            if (d.rowStartsVersion != d.editor.version()) { // (the scenes' lines, where it can stop)
                d.rowStartsVersion = d.editor.version();
                d.rowStarts.clear();
                for (const StoryEditor::Scene& scene : d.editor.scenes())
                    for (const StoryEditor::Row& row : d.editor.rows(scene)) d.rowStarts.emplace(d.editor.path(scene.file), row.line);
            }
            d.stepFrom = *at;
            d.stepping = true; // (before: a stopped story goes on as the tools close)
            backToGame();
        }
        break;
    case kPlayScene: // (or from the chosen line)
        if (d.view == View::Story && d.scene < d.editor.scenes().size()) {
            d.playScene = d.editor.scenes()[d.scene].name;
            d.playLine.reset();
            if (d.view == View::Story && d.row && d.moreRows.empty() && !d.character &&
                std::ranges::contains(d.editor.rows(d.editor.scenes()[d.scene]), *d.row, &StoryEditor::Row::line))
                d.playLine = d.row;
            engine.reloadRequested_ = true;
        }
        break;
    case kReload:
        engine.reloadRequested_ = true;
        break;
    case kPack: {
        if (const std::string folder = engine.finishedFolder(); SDL_GetPathInfo(folder.c_str(), nullptr)) { // (one packed before: replaced if the developer says so)
            const std::string title = "Replace the packed game?"_tr(), replace = "Replace it"_tr(), keep = "Keep it"_tr();
            const std::string body = "There's a packed game in '{}' already. Packing again puts it in the Trash."_tr(folder);
            const SDL_MessageBoxButtonData buttons[]{
                {.flags = 0, .buttonID = 1, .text = replace.c_str()},
                {.flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, .buttonID = 0, .text = keep.c_str()}};
            const SDL_MessageBoxData box{.flags = SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
                                         .window = engine.window(),
                                         .title = title.c_str(),
                                         .message = body.c_str(),
                                         .numbuttons = 2,
                                         .buttons = buttons,
                                         .colorScheme = nullptr};
            int clicked = 0;
            if (!SDL_ShowMessageBox(&box, &clicked) || clicked != 1) {
                logInfo("{}", "The packed game in '{}' stays as it was."_tr(folder));
                break;
            }
        }
        d.packIn = 2; // (once the game's been read again, next frame)
        engine.reloadRequested_ = true; // (first the game is read again, so its scripts are checked as they are now)
        break;
    }
    case kFolder: {
        const std::string folder = engine.noGame() ? engine.gamePlaces().front() : engine.gameRoot();
        if (!SDL_OpenURL(fileUrl(folder).c_str())) {
            d.failed = true;
            d.status = "Can't show '{}': {}."_tr(folder, SDL_GetError());
        }
        break;
    }
    case kNewGame: {
        const std::string scripts = engine.gamePlaces().front() + "game/scripts/";
        if (!SDL_CreateDirectory(scripts.c_str()) ||
            !SDL_SaveFile((scripts + "main.vnscript").c_str(), kNewGameScript.data(), kNewGameScript.size())) {
            d.failed = true;
            d.status = "Can't make a game folder there: {}."_tr(SDL_GetError());
            break;
        }
        logInfo("{}", "Made a new game: '{}main.vnscript'."_tr(scripts));
        if (!engine.lookForGame()) { // (in developer mode, so it opens in the Story tab)
            d.failed = true;
            d.status = "There's still no game folder next to the player."_tr;
        }
        break;
    }
    case kLookAgain:
        if (!engine.lookForGame()) {
            d.failed = true;
            d.status = "There's still no game folder or game.vnext next to the player."_tr;
        }
        break;
    case kQuit:
        engine.quit_ = true;
        break;
    default:
        break;
    }
}

std::string_view Developer::viewName(View view) {
    switch (view) {
    case View::Story: return "Story"_tr;
    case View::Screens: return "Screens"_tr;
    case View::Library: return "Library"_tr;
    case View::About: return "About"_tr;
    }
    return {};
}

// The tools' own part's buttons as the tools are now, group by group, in one row (two in a small window): the tabs;
// playing the game (from the start or back to it, the scene or from the line chosen, the next line, Stop here);
// changing what's chosen; the game's files (New game and Look again without one, Reload, Pack, its folder); the
// layout (Layout, and Reset: the tab's put back); the tools' language (English, the translations, a file to
// translate them); About and Quit. They're always all there, each in its place (one whose words change is as wide
// as the longest of them): one that can't be pressed now is grey, one that's on is lit (the tab shown, Stop here,
// Layout, the language in use, About), and a tab with a mistake in what it shows has the lists' red mark.
std::vector<std::vector<ToolButton>> Developer::toolButtons() {
    DeveloperTools& d = tools;
    const bool game = !engine.noGame(), editing = game && d.editor.loaded();
    auto button = [](int id, std::string_view label, bool enabled = true, bool on = false) {
        return ToolButton{.id = id, .label = std::string(label), .longest = {}, .enabled = enabled, .on = on};
    };
    std::vector<std::vector<ToolButton>> out;

    auto wrongIn = [&](const auto& items) { return std::ranges::any_of(items, [&](const auto& it) { return d.problemWithin(it.file, it.line, it.end); }); };
    auto wrong = [&](View tab) {
        if (!editing) return false;
        switch (tab) {
        case View::Story: return std::ranges::contains(d.wrongScenes(), true);
        case View::Screens: return wrongIn(d.editor.screens());
        case View::Library:
            return wrongIn(d.editor.definitions("variable")) || std::ranges::any_of(d.problems, [&](const ScriptError& e) { return inSettings(d.editor, e); }) ||
                   std::ranges::any_of(kDefinitionKinds, [&](const DefinitionKind& kind) { return wrongIn(d.editor.definitions(kind.kind)); });
        default: return false;
        }
    };
    std::vector<ToolButton> tabs;
    for (const auto& [id, tab] : kTabs) {
        ToolButton b = button(id, viewName(tab), true, d.view == tab);
        b.wrong = wrong(tab);
        tabs.push_back(std::move(b));
    }
    out.push_back(std::move(tabs));

    const bool story = editing && d.view == View::Story, fromLine = story && d.row && d.moreRows.empty() && !d.character;
    const bool stoppable = story && d.stoppable.has_value();
    ToolButton play = button(kPlay, !game || engine.waiting_ ? "Play"_tr : "Resume"_tr, game);
    ToolButton scene = button(kPlayScene, fromLine ? "Play from line"_tr : "Play scene"_tr, story && !d.editor.scenes().empty());
    play.longest = "Resume"_tr;
    scene.longest = "Play from line"_tr;
    out.push_back({std::move(play), std::move(scene), button(kNextLine, "Next line"_tr, game && storyAt().has_value()),
                   button(kStopHere, "Stop here"_tr, stoppable, stoppable && hasStop(d.stoppable->first, d.stoppable->second))});

    // (undo and redo: the editor's steps, or the Library's translations' own)
    const bool translating = d.view == View::Library && d.library == "translations", undoing = editing && d.view != View::About;
    const bool undoes = translating ? !d.translateUndo.empty() : undoing && d.editor.canUndo();
    const bool redoes = translating ? !d.translateRedo.empty() : undoing && d.editor.canRedo();
    out.push_back({button(kUndo, "Undo"_tr, undoes), button(kRedo, "Redo"_tr, redoes), button(kCut, "Cut"_tr, editing), button(kCopy, "Copy"_tr, editing),
                   button(kPaste, "Paste"_tr, editing), button(kDuplicate, "Duplicate"_tr, editing), button(kDelete, "Delete"_tr, editing),
                   button(kSelectAll, "Select all"_tr, editing)});

    out.push_back({button(kNewGame, "New game"_tr, !game), button(kLookAgain, "Look again"_tr, !game), button(kReload, "Reload"_tr, game),
                   button(kPack, "Pack"_tr, game), button(kFolder, "Folder"_tr)});

    const bool moved = std::ranges::any_of(d.panels, [&](const auto& placed) { return placed.first.starts_with(d.panelTab + "/"); });
    out.push_back({button(kEditLayout, "Layout"_tr, true, d.editLayout), button(kResetLayout, "Reset"_tr, moved)});

    // The developer tools' language: English, then the translations in 'developer tools translations/' by name, and a
    // file to translate them (with one in use: the texts it doesn't have yet, added).
    if (d.cachedFiles(toolsLanguagesFolder(), ".vnscript") != d.toolsLanguageFiles) findToolsLanguages(); // (one added or taken away)
    std::vector<ToolButton> language{button(kToolsLanguage, "English", true, d.toolsLanguage.empty())};
    for (std::size_t i = 0; i < d.toolsLanguages.size(); ++i)
        language.push_back(button(kToolsLanguage + 1 + static_cast<int>(i), d.toolsLanguages[i].name, true, d.toolsLanguages[i].id == d.toolsLanguage));
    language.push_back(button(kToolsTranslation, "Translate"_tr));
    out.push_back(std::move(language));

    out.push_back({button(kAbout, "About"_tr, true, d.view == View::About), button(kQuit, "Quit"_tr)});
    return out;
}

// Where the tools' own part's buttons go, from (x, y) in `w`, group by group: each as wide as its longest words (small,
// as a menu bar's), a group's side by side and the groups further apart with a line between them, on as many rows as
// they take (a group that doesn't fit where its row is up to starts the next; one wider than a row goes on over the
// next), each row in the middle of `w`; and how tall they are.
Developer::ToolsPlaced Developer::placeToolButtons(DeveloperUi& ui, const std::vector<std::vector<ToolButton>>& groups, float x, float y, float w) {
    const float k = ui.k, buttonH = layout::toolButtonH * k, gap = layout::toolGap * k, groupGap = layout::toolGroupGap * k;
    auto widthOf = [&](const ToolButton& b) {
        const float words = std::max(ui.width(b.label, layout::toolText), b.longest.empty() ? 0.0f : ui.width(b.longest, layout::toolText));
        return std::min(w, std::round(words + 2 * layout::toolPad * k));
    };
    ToolsPlaced out;
    float cx = x, cy = y;
    for (const std::vector<ToolButton>& group : groups) {
        float groupW = -gap;
        for (const ToolButton& b : group) groupW += widthOf(b) + gap;
        if (cx > x && cx + groupW > x + w) cx = x, cy += buttonH + gap;
        else if (cx > x) out.lines.push_back({std::round(cx - groupGap / 2), cy, std::max(1.0f, std::round(k)), buttonH}); // (in the middle of the gap)
        std::vector<SDL_FRect>& at = out.buttons.emplace_back();
        for (const ToolButton& b : group) {
            const float bw = widthOf(b);
            if (cx > x && cx + bw > x + w) cx = x, cy += buttonH + gap;
            at.push_back({cx, cy, bw, buttonH});
            cx += bw + gap;
        }
        cx += groupGap - gap;
    }
    std::map<float, float> ends; // (each row's right end, by its top)
    for (const auto& at : out.buttons)
        for (const SDL_FRect& b : at) ends[b.y] = std::max(ends[b.y], b.x + b.w);
    auto centre = [&](SDL_FRect& r) { r.x += std::floor((x + w - ends[r.y]) / 2); };
    for (auto& at : out.buttons) std::ranges::for_each(at, centre);
    std::ranges::for_each(out.lines, centre);
    out.h = groups.empty() ? 0.0f : cy + buttonH - y;
    return out;
}

// The tools' own part, at `at`: drawn as the tabs' parts are (but never lit: it isn't moved), with its buttons in the
// middle; the one pressed is done as the next frame starts (with what it acts on as it is now).
void Developer::drawToolsPart(DeveloperUi& ui, const std::vector<std::vector<ToolButton>>& groups, const SDL_FRect& at) {
    DeveloperTools& d = tools;
    const float k = ui.k;
    draw::rect(ui.r, at, kPanel);
    draw::outline(ui.r, at, kEdge);
    const SDL_FPoint chrome = panelChrome(k);
    const SDL_FRect box{at.x + chrome.x / 2, at.y + chrome.y / 2, std::max(0.0f, at.w - chrome.x), std::max(0.0f, at.h - chrome.y)};
    const DeveloperUi::Clip clipped(ui, box);
    const ToolsPlaced placed = placeToolButtons(ui, groups, box.x, box.y, box.w);
    for (const SDL_FRect& line : placed.lines) draw::rect(ui.r, line, kEdge);
    for (std::size_t g = 0; g < groups.size(); ++g)
        for (std::size_t i = 0; i < groups[g].size(); ++i) {
            const ToolButton& b = groups[g][i];
            const SDL_FRect& place = placed.buttons[g][i];
            if (ui.button(place, b.label, b.on, b.enabled, layout::toolText)) d.pressedTool = b.id;
            if (b.wrong) draw::rect(ui.r, {place.x, place.y, layout::markW * k, place.h}, kBad);
        }
}

// The tools' own keys: what each of their controls has (action="tools WHAT"; a game's own control of that name replaces
// the built-in one, as in the game), as the scripts are now in the editor; without a game, the built-in ones.
void Developer::readToolKeys() {
    DeveloperTools& d = tools;
    static const Script builtIn = Script::load("scripts/main.vnscript", [](const std::string&) { return std::optional<std::string>("scene start\n"); }, defaultUi()); // (without a game: the built-in controls, read with a story of nothing)
    for (auto& keys : d.toolKeys) keys.clear();
    for (const Control& c : (d.script ? *d.script : builtIn).controls)
        if (const auto i = toolsVerbOf(c)) d.toolKeys[*i] = c.keys;
}

bool Developer::toolKey(const SDL_Event& e, ToolKey which) {
    if (e.type != SDL_EVENT_KEY_DOWN) return false;
    readToolKeys();
    return tools.isKey(e.key, which);
}

// What a tool says (how it went wrong, or what it made where), in a window of its own with OK, as packing says how
// it went. With SDL's offscreen video (the recorder's) there's no window: the log has it.
void Developer::tell(const std::string& message, bool failed) {
    const char* video = SDL_GetCurrentVideoDriver();
    if (!video || std::string_view(video) == "offscreen") return;
    if (!SDL_ShowSimpleMessageBox(failed ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, "VNext", message.c_str(), engine.window()))
        logWarn("Can't show a message: {}.", SDL_GetError());
}

void Developer::reloaded() {
    DeveloperTools& d = tools;
    d.played = d.editor.version();
    const bool start = std::exchange(d.play, false);
    const std::string scene = std::exchange(d.playScene, {});
    const auto line = std::exchange(d.playLine, std::nullopt);
    if (engine.mode() != Engine::Mode::Error && (start || !scene.empty())) { // Play, Play this scene, or Play from this line
        d.open = false;
        engine.waiting_ = false;
        d.stepping = false;
        d.passing.reset(); // (a play from a line with Stop here stops at once, before it)
        if (scene.empty()) engine.play();
        else playFromLine(scene, line);
    } else if (d.open) {
        d.editor.load(engine.gameRoot()); // (the files may have changed outside the player)
        d.failed = engine.mode() == Engine::Mode::Error;
        if (d.packIn > 0) return; // (packing says how it went)
        if (d.failed) d.status = "The scripts need fixing: their mistakes are marked in red in the tabs."_tr;
        else logInfo("{}", "Reloaded the scripts, images and sounds."_tr());
    }
}

// Called every frame: carries out what takes a moment, once the screen says it's happening.
void Developer::updateDeveloper() {
    DeveloperTools& d = tools;
    if (!engine.developerMode() && (d.open || engine.waiting_)) { // (developer mode turned off, then reloaded)
        d.open = false;
        if (std::exchange(engine.waiting_, false) && engine.mode() != Engine::Mode::Error) engine.play();
    }
    const bool packNow = d.packIn == 1; // (the game read again)
    if (d.packIn > 0) --d.packIn;
    if (packNow) {
        const auto packed = engine.finishGame();
        const std::string& said = packed ? *packed : packed.error(); // (a sentence or two a line)
        std::string line = said;
        std::ranges::replace(line, '\n', ' '); // (the log has it on one line)
        if (packed) logInfo("{}", line);
        else logWarn("{}", line);
        // Said in a window of its own: its title says how it went, the rest a line apart, and after a pack there's
        // the way to it.
        const std::string title = packed ? "Packed the game"_tr() : "Can't pack the game"_tr(), showFolder = "Show the folder"_tr(), ok = "OK"_tr();
        std::string body = said.starts_with(title + ": ") ? upperFirst(said.substr(title.size() + 2)) : said;
        for (std::size_t at = body.find('\n'); at != std::string::npos; at = body.find('\n', at + 2)) body.insert(at, 1, '\n');
        const SDL_MessageBoxButtonData buttons[]{
            {.flags = 0, .buttonID = 1, .text = showFolder.c_str()},
            {.flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, .buttonID = 0, .text = ok.c_str()}};
        const SDL_MessageBoxData box{.flags = (packed ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR) | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
                                     .window = engine.window(),
                                     .title = title.c_str(),
                                     .message = body.c_str(),
                                     .numbuttons = packed ? 2 : 1,
                                     .buttons = packed ? buttons : buttons + 1,
                                     .colorScheme = nullptr};
        int clicked = 0;
        if (SDL_ShowMessageBox(&box, &clicked) && clicked == 1) {
            const std::string folder = engine.finishedFolder();
            if (!SDL_OpenURL(fileUrl(folder).c_str())) {
                d.failed = true;
                d.status = "Can't show '{}': {}."_tr(folder, SDL_GetError());
            }
        }
    }
}

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

void Developer::drawDeveloper() {
    SDL_Renderer* r = engine.renderer();
    DeveloperTools& d = tools;
    if (++d.textFrame % 60 == 0) // (text not drawn for two seconds or so goes)
        std::erase_if(d.texts, [&](const auto& kept) { return kept.second.drawn + 120 < d.textFrame; });
    // The window as the tools have it (Engine::useView): its own size, in its own points (bigger where the
    // screen is set to show things bigger, as Windows at 150% does), and all of it smaller in a window too
    // small for the tools (by sixteenths: a font for each size it's been).
    SDL_Window* window = engine.window();
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    const float w = static_cast<float>(std::max(ww, 1)), h = static_cast<float>(std::max(wh, 1));
    const float density = SDL_GetWindowPixelDensity(window), shown = SDL_GetWindowDisplayScale(window);
    const float own = density > 0.0f && shown > 0.0f ? std::clamp(shown / density, 0.5f, 4.0f) : 1.0f;
    const float k = own * std::floor(std::min({1.0f, w / (layout::leastW * own), h / (layout::leastH * own)}) * 16.0f) / 16.0f;
    d.k = k;
    d.size = {w, h};
    DeveloperUi ui(engine, r, engine.toolsFonts_, d, k);
    if (!std::exchange(d.listenShown, false)) engine.sound().stopListening(); // (its button is gone)
    if (d.moviePlaying && engine.sound().listening() != d.movieOf) d.stopMovie();   // (stopped, or something else plays)
    draw::rect(r, {0, 0, w, h}, kPanel); // (opaque: the game under the tools is drawn at its own size, and would show through)
    // A list open under a field takes the clicks and the wheel over it (it's drawn last, over the
    // rest); a click anywhere else but its field closes it, and so does Esc (that, and nothing else: not
    // the tools, nor what's chosen under it; a field being typed in closes it with Esc itself).
    d.popup.shown = false;
    if (!d.popup.id.empty() && d.focus != d.popup.id && std::erase_if(d.keys, [](const SDL_KeyboardEvent& key) { return key.key == SDLK_ESCAPE; }) > 0)
        d.popup.id.clear();
    d.overPopup = !d.popup.id.empty() && pointInRect(d.mouseX, d.mouseY, d.popup.box);
    d.popupClick = d.overPopup && std::exchange(d.clicked, false);
    d.popupWheel = d.overPopup ? std::exchange(d.wheel, 0.0f) : 0.0f;
    if (d.clicked && !pointInRect(d.mouseX, d.mouseY, d.popup.at)) d.popup.id.clear();

    // Mistakes in the scripts as the editor has them, found again after every change.
    const bool editing = !engine.noGame() && d.editor.loaded();
    if (editing) importFiles();
    if (editing && engine.toolsTime() >= d.nextCheck) { // (scripts changed in another program are read again)
        d.nextCheck = engine.toolsTime() + 0.5f;
        ++d.filesGeneration; // (files that came or went outside: the lists show them)
        if (d.editor.changedOutside()) {
            d.editor.load(engine.gameRoot());
            d.focus.clear();
            logInfo("{}", "The scripts changed in another program, so the developer tools read them again."_tr());
        }
    }
    // The game's size changed in Settings (and its box was left): the screens' places and shares fit any
    // size; their text sizes, padding and bars grow or shrink to fit it, as one step for undo with the change.
    if (editing && d.sizeFrom && d.focus != "setting:game_size") {
        auto sizeOf = [](std::string_view v) -> std::optional<SDL_FPoint> {
            const auto words = rawWords(v);
            const auto width = words.size() == 2 ? parseNumber(words[0]) : std::nullopt, height = words.size() == 2 ? parseNumber(words[1]) : std::nullopt;
            return width && height && *width >= 1.0f && *height >= 1.0f ? std::optional(SDL_FPoint{*width, *height}) : std::nullopt;
        };
        const auto from = sizeOf(*d.sizeFrom), to = sizeOf(d.editor.setting("game_size").value_or("1280 720"));
        d.sizeFrom.reset();
        if (from && to && (from->x != to->x || from->y != to->y)) {
            d.editor.scaleLayout([&](const std::string& line) { return scaled(line, to->y / from->y); }, d.sizeKey);
            d.failed = false;
            d.status = "The screens' text sizes were changed from {} x {} to fit {} x {}."_tr(from->x, from->y, to->x, to->y);
        }
    }
    if (editing && d.checked != d.editor.version()) {
        d.checked = d.editor.version();
        d.dropPreviews(); // (it plays the scripts read before)
        auto before = std::move(d.script);
        d.script = std::make_unique<Script>(Script::load("scripts/main.vnscript", [this](const std::string& p) { return readTextFile(engine.gameRoot() + p); }, defaultUi()));
        if (before) // (the texts changed keep their translations; the Library shows them as they are now)
            for (const std::string& id : keepTranslations(engine.gameRoot(), *before, *d.script))
                if (id == d.translation.id) d.translation.lines = linesOf(*readTextFile(engine.gameRoot() + "translations/" + id + ".vnscript"));
        d.problems = d.script->errors;
        d.runLines.clear(); // (the lines Stop here is for: with a command of their own)
        for (const Instruction& ins : d.script->code)
            if (!ins.tail && ins.line > 0 && ins.file < d.script->files.size()) d.runLines.emplace(d.script->files[ins.file], static_cast<std::size_t>(ins.line - 1));
        followStops();
        engine.assets().setSubImages(d.script->subImages); // (the previews and lists show them as they are now)
        engine.script_.palette = d.script->palette;        // (and the game's colours: a ready-made screen's backdrop, a colour just made)
        // The game's size as Settings has it now (the screens are drawn at it; the game
        // takes it when it's played again).
        if (const GameSettings& c = d.script->gameSettings;
            c.width > 0 && c.height > 0 && (engine.canvas().w != static_cast<float>(c.width) || engine.canvas().h != static_cast<float>(c.height))) {
            engine.canvas_ = {static_cast<float>(c.width), static_cast<float>(c.height)};
            engine.updatePixelScale(); // (the game's view takes it; the tools' stays the window's)
        }
        // Whether the window can be resized, as Settings has it now: the tools' window is the game's.
        if (const bool resizable = d.script->gameSettings.resizable; resizable != ((SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE) != 0))
            SDL_SetWindowResizable(window, resizable);
        std::ranges::move(missingFiles(*d.script), std::back_inserter(d.problems));
    }

    // The tools' own keys as their controls have them now, and the tools' own part's button pressed last frame: done
    // first, as the tools are drawn.
    readToolKeys();
    if (const auto pressed = std::exchange(d.pressedTool, std::nullopt)) runCommand(*pressed);
    const std::vector<std::vector<ToolButton>> buttons = toolButtons();
    d.stoppable.reset(); // (the Story tab says it again as it's drawn)

    // What the last tool said goes to the log and shows in a window of its own (tell), once: the same words said
    // again show again, but a save that can't be written says so once, until one can be.
    if (!d.editor.problem().empty()) {
        d.failed = true;
        d.status = d.editor.problem();
    }
    if (!d.status.empty() && d.status != d.loggedStatus) {
        d.loggedStatus = d.status;
        std::string line = d.status;
        std::ranges::replace(line, '\n', ' ');
        if (d.failed) logWarn("{}", line);
        else logInfo("{}", line);
        tell(d.status, d.failed);
    }
    if (d.editor.problem().empty()) d.loggedStatus.clear();
    d.status.clear();
    d.failed = false;
    if (engine.noGame() && !d.saidNoGame) {
        d.saidNoGame = true;
        logInfo("{}", "There's no game yet: make a New game, or put a game folder or game.vnext next to the player and Look again."_tr());
    }

    // The tools' own part, at the window's foot in every tab (as wide as the window, as tall as its buttons: it's
    // neither moved nor resized), and the room over it for the tab, to the window's edges: its parts, each a
    // sub-window (drawPanels).
    const SDL_FPoint chrome = panelChrome(k);
    const float toolsH = std::min(h, chrome.y + placeToolButtons(ui, buttons, 0.0f, 0.0f, std::max(0.0f, w - chrome.x)).h);
    const SDL_FRect toolsAt{0.0f, h - toolsH, w, toolsH};
    d.room = {0.0f, 0.0f, w, toolsAt.y}; // (the parts end where the tools' own starts)
    d.wantPointer.reset();
    const SDL_FRect area = d.room;
    auto one = [&](std::string_view tab, Panel part) {
        std::array panels{std::move(part)};
        drawPanels(ui, tab, panels);
    };
    if (d.view == View::About) {
        one("about", titledPanel(ui, "about", "About"_tr, area, [&](const SDL_FRect& box) { return drawAbout(ui, box), false; }));
    } else if (!editing) { // (no game yet: what to do)
        const std::string_view say = "There's no game yet: make a New game, or put a game folder or game.vnext next to the player and Look again."_tr;
        one("no_game", titledPanel(ui, "no_game", "No game"_tr, area, [&](const SDL_FRect& box) { return ui.text(say, box.x, box.y, layout::noteText, theme::dim, box.w), false; }));
    } else if (d.view == View::Story) drawStory(ui, area);
    else if (d.view == View::Screens) drawScreens(ui, area);
    else drawLibrary(ui, area);
    drawToolsPart(ui, buttons, toolsAt);
    showPointer(d.wantPointer);
    drawPopup(ui);
    drawTip(ui);

    // Keys nothing else took: the tools' own that act on them as a whole (undo and redo, the tabs, Reload, Quit; the
    // others are the keys of what they act on, as their controls have them), and Esc, back to the game.
    std::vector<int> commands;
    for (const SDL_KeyboardEvent& key : d.keys) {
        const bool undo = d.isKey(key, ToolKey::Undo), redo = d.isKey(key, ToolKey::Redo);
        if (key.key == SDLK_ESCAPE && key.which != kToolButton && !engine.noGame() && !engine.waiting_) {
            closeDeveloper();
        } else if ((undo || redo) && editing && d.view != View::About && !(d.view == View::Library && d.library == "translations")) {
            d.focus.clear();
            static_cast<void>(redo ? d.editor.redo() : d.editor.undo());
        } else if (d.isKey(key, ToolKey::Story)) {
            commands.push_back(kStoryTab);
        } else if (d.isKey(key, ToolKey::Screens)) {
            commands.push_back(kScreensTab);
        } else if (d.isKey(key, ToolKey::Library)) {
            commands.push_back(kLibraryTab);
        } else if (d.isKey(key, ToolKey::Reload)) {
            commands.push_back(kReload);
        } else if (d.isKey(key, ToolKey::Quit)) {
            commands.push_back(kQuit);
        }
    }
    for (const int command : commands) runCommand(command);
    if (d.clicked) d.focus.clear(); // a click on nothing
    if (d.focus.empty()) d.focusOwner.clear();
    d.keys.clear();
    d.typed.clear();
    d.entered.clear();
    d.clicked = d.doubleClick = false;
    d.wheel = 0.0f;
}

// Lines that use an image, sound or movie the game doesn't have (the player would show a stand-in
// or stay silent): as mistakes too, so they're marked like the scripts' own.
std::vector<ScriptError> Developer::missingFiles(const Script& script) const {
    const StoryEditor& editor = tools.editor;
    std::set<std::string, std::less<>> frames; // (atlases' images by name)
    for (const Atlas& a : script.atlases) {
        frames.insert(lower(a.name));
        for (const std::string& e : a.expressions) frames.insert(lower(a.name + "_" + e));
    }
    for (const SubImage& p : script.subImages) frames.insert(p.name);
    auto image = [&](const std::string& name) {
        return name.empty() || name.contains('{') || parseColour(name, &script.palette) || frames.contains(lower(name)) || engine.assets().hasImageFile(name) ||
               engine.assets().hasImageFile(lower(name));
    };
    auto sound = [&](const std::string& name) { return name.empty() || name.contains('{') || engine.assets().findSound(name) || engine.assets().findSound(lower(name)); };
    auto movie = [&](const std::string& name) { return name.empty() || name.contains('{') || engine.assets().findMovie(name) || engine.assets().findMovie(lower(name)); };
    std::vector<ScriptError> out;
    auto at = [&](std::size_t file, int line, std::string message) {
        if (file < script.files.size() && script.files[file].starts_with("scripts/")) out.push_back({script.files[file], line, std::move(message)});
    };
    auto noImage = [](const std::string& name) { return "There's no image called '{}'."_tr(name); };
    for (const Instruction& ins : script.code) {
        if (const auto* background = std::get_if<cmd::Background>(&ins.command)) {
            if (!image(background->image)) at(ins.file, ins.line, noImage(background->image));
        } else if (const auto* show = std::get_if<cmd::Show>(&ins.command)) {
            if (const CharacterDef* def = show->character.empty() ? nullptr : script.character(show->character)) {
                const std::string key = def->imageOf(show->expression);
                if (!image(key) && !(!show->expression.empty() && image(def->image))) at(ins.file, ins.line, noImage(key)); // (else its usual image shows)
            } else if (!image(show->image)) {
                at(ins.file, ins.line, noImage(show->image));
            }
        } else if (const auto* play = std::get_if<cmd::Play>(&ins.command)) {
            if (!sound(play->file)) at(ins.file, ins.line, "There's no sound called '{}'."_tr(play->file));
        } else if (const auto* film = std::get_if<cmd::Movie>(&ins.command)) {
            if (!movie(film->file)) at(ins.file, ins.line, "There's no movie called '{}'."_tr(film->file));
        } else if (const auto* weather = std::get_if<cmd::Weather>(&ins.command)) {
            if (!image(weather->image)) at(ins.file, ins.line, noImage(weather->image));
        }
    }
    for (const auto& [name, def] : script.screens)
        for (const Element& el : def.elements) {
            if (el.kind == ElementKind::Image && !image(el.text) && !parseColour(el.text, &script.palette)) at(el.file, el.line, noImage(el.text)); // (a colour isn't an image)
            if (el.kind == ElementKind::Movie && !movie(el.text)) at(el.file, el.line, "There's no movie called '{}'."_tr(el.text));
        }
    // What the scripts list by name, at their lines in the editor.
    auto listed = [&](std::size_t file, std::size_t line, std::string message) { out.push_back({editor.path(file), static_cast<int>(line) + 1, std::move(message)}); };
    for (const StoryEditor::Definition& def : editor.definitions("animation")) // (the image a wipe has the shape of)
        if (const auto it = script.animations.find(lower(def.name)); it != script.animations.end())
            if (const auto shaped = std::ranges::find_if(it->second.changes, [&](const Animation::Change& c) { return !image(c.shape); });
                shaped != it->second.changes.end())
                listed(def.file, def.line, noImage(shaped->shape));
    for (const StoryEditor::Character& c : editor.characters())
        if (const CharacterDef* def = script.character(lower(c.name))) {
            if (!sound(def->typingSound)) listed(c.file, c.line, "There's no sound called '{}'."_tr(def->typingSound));
            if (!image(def->face)) listed(c.file, c.line, noImage(def->face));
        }
    for (const auto& def : editor.definitions("gallery"))
        if (!image(def.name) && !movie(def.name)) listed(def.file, def.line, noImage(def.name));
    for (const auto& def : editor.definitions("song"))
        if (!sound(def.name)) listed(def.file, def.line, "There's no song called '{}'."_tr(def.name));
    for (const auto& def : editor.definitions("sub-image"))
        if (const auto p = std::ranges::find(script.subImages, lower(def.name), &SubImage::name);
            p != script.subImages.end() && !engine.assets().hasImageFile(p->image) && !engine.assets().hasImageFile(lower(p->image)))
            listed(def.file, def.line, noImage(p->image));
    for (const auto& def : editor.definitions("atlas"))
        if (const auto a = std::ranges::find(script.atlases, lower(def.name), &Atlas::name); a != script.atlases.end())
            for (const std::string& file : {a->image, a->eyesPart ? a->eyesPart->image : std::string{}, a->mouthPart ? a->mouthPart->image : std::string{}})
                if (!file.empty() && !engine.assets().hasImageFile(file) && !engine.assets().hasImageFile(lower(file))) listed(def.file, def.line, noImage(file));
    return out;
}

void Developer::chooseFiles(std::string_view label, std::span<const std::string_view> extensions, const std::string& as) {
    DeveloperTools& d = tools;
    {
        const std::scoped_lock _(d.imports.mutex);
        if (d.imports.open) { // (its filter's words stay as they are until it's answered)
            d.status = "A window for choosing files is open already."_tr;
            return;
        }
        d.imports.open = true;
        d.imports.as = as;
        d.imports.label = label;
        d.imports.pattern.clear();
        for (const std::string_view ext : extensions) d.imports.pattern += (d.imports.pattern.empty() ? "" : ";") + std::string(ext.substr(1));
        d.imports.filter = {d.imports.label.c_str(), d.imports.pattern.c_str()};
    }
    SDL_ShowOpenFileDialog(
        [](void* state, const char* const* chosen, int) {
            auto& imports = static_cast<DeveloperTools*>(state)->imports;
            withoutThrowing([&] { // (SDL calls this, maybe from another thread)
                const std::scoped_lock _(imports.mutex);
                const ScopeExit answered([&] { // (whatever could be kept of it)
                    imports.as.clear(); // (dropped on the window later: by their kind)
                    imports.open = false;
                });
                for (; chosen && *chosen; ++chosen) imports.files.emplace_back(*chosen, imports.as);
            });
        },
        &d, engine.window(), &d.imports.filter, 1, nullptr, as.empty());
}

// Files picked in the Library's file chooser, or dropped on the window: copied into the game folder
// (images into images/, sounds into sounds/, movies into movies/, fonts into fonts/), under a name of
// their own if one's there already; or, picked for a place of its own (a translation's own image, a
// line's voice), there, in place of one there already (to the Trash). Undo takes them out again.
void Developer::importFiles() {
    DeveloperTools& d = tools;
    std::vector<std::pair<std::string, std::string>> files;
    {
        const std::scoped_lock _(d.imports.mutex);
        files.swap(d.imports.files);
    }
    if (files.empty() || engine.noGame()) return;
    std::size_t copied = 0;
    std::string last, skipped, firstImage;
    StoryEditor::Moves moves;
    const std::optional<SDL_FPoint> dropAt = std::exchange(d.imports.dropAt, std::nullopt);
    ++d.filesGeneration; // (the lists show what's imported)
    auto folderOf = [](std::string_view ext) -> std::string_view {
        const auto kind = std::ranges::find_if(kFileKinds, [&](const FileKind& f) { return std::ranges::contains(f.extensions, ext); });
        return kind != std::end(kFileKinds) ? kind->folder : std::string_view{};
    };
    for (const auto& [from, as] : files) {
        const auto slash = from.find_last_of("/\\");
        std::string name = from.substr(slash == std::string::npos ? 0 : slash + 1);
        auto dot = name.rfind('.');
        const std::string ext = dot == std::string::npos ? std::string() : lower(name.substr(dot));
        const std::string_view folder = folderOf(ext);
        if (folder.empty() || (!as.empty() && !as.starts_with(folder))) {
            skipped = name;
            continue;
        }
        { // (named as scripts call it: "19. YOMI.flac" is 19_YOMI.flac)
            std::string stem = scriptName(name.substr(0, dot));
            if (stem.empty()) stem = folder == "images/" ? "image" : folder == "sounds/" ? "sound" : folder == "movies/" ? "movie" : "font";
            name = stem + ext;
            dot = stem.size();
        }
        std::string to;
        if (as.empty()) {
            SDL_CreateDirectory((engine.gameRoot() + std::string(folder)).c_str());
            const std::string stem = uniqueName(name.substr(0, dot), [&](const std::string& n) { return fileExists(engine.gameRoot() + std::string(folder) + n + name.substr(dot)); });
            to = engine.gameRoot() + std::string(folder) + stem + name.substr(dot);
        } else { // (one there by that name, of any kind of its folder's files, goes to the Trash)
            to = engine.gameRoot() + as + ext;
            SDL_CreateDirectory(to.substr(0, to.find_last_of('/')).c_str()); // (and the folders it's in)
            if (const auto kind = std::ranges::find(kFileKinds, folder, &FileKind::folder); kind != std::end(kFileKinds) && folder != "fonts/")
                for (const std::string_view other : kind->extensions)
                    if (const std::string old = engine.gameRoot() + as + std::string(other); fileExists(old))
                        if (const auto trashed = moveToTrash(old); trashed && !trashed->empty()) moves.emplace_back(old, *trashed);
        }
        if (SDL_CopyFile(from.c_str(), to.c_str())) {
            ++copied;
            last = to.substr(engine.gameRoot().size());
            if (folder == "images/" && firstImage.empty()) firstImage = last.substr(7, last.rfind('.') - 7);
            moves.emplace_back(std::string{}, to);
            if (as.empty()) {
                d.library = folder == "images/" ? "images" : folder == "sounds/" ? "sounds" : folder == "movies/" ? "movies" : "fontfiles";
                d.libraryItem.reset();
                d.librarySub.reset();
            }
        }
    }
    d.editor.movedFiles(std::move(moves));
    if (copied > 0) {
        logInfo("{}", copied == 1 ? "Imported '{}'."_tr(last) : "Imported {} files."_tr(copied));
    } else {
        d.failed = true;
        d.status = !skipped.empty() ? "'{}' isn't an image, sound, movie or font file the game can use there."_tr(skipped) : "Can't copy that into the game folder."_tr();
    }
    // An image dropped on the screen open in the Screens tab: a part showing it, where it was dropped
    // (in the part chosen there when it landed on that, else the screen), placed by its corner.
    bool placed = false;
    if (dropAt && !firstImage.empty() && d.view == DeveloperTools::View::Screens && d.screenView && d.script && pointInRect(dropAt->x, dropAt->y, d.shownScreen))
        if (const auto sc = std::ranges::find_if(d.editor.screens(), [&](const StoryEditor::Screen& s) { return lower(s.name) == lower(d.screenName); });
            sc != d.editor.screens().end()) {
            const ScreenDef& def = d.screenView->def();
            const float scale = d.shownScreen.w / engine.canvas().w;
            const float px = (dropAt->x - d.shownScreen.x) / scale, py = (dropAt->y - d.shownScreen.y) / scale;
            SDL_FRect box{0.0f, 0.0f, engine.canvas().w, engine.canvas().h};
            std::optional<StoryEditor::Part> into;
            if (d.part)
                for (const Element& el : def.elements)
                    if (el.line == static_cast<int>(*d.part) + 1 && d.script->files[el.file] == d.editor.path(sc->file) && pointInRect(px, py, d.screenView->partRect(d.screenView->partIndex(el)))) {
                        box = d.screenView->partRect(d.screenView->partIndex(el));
                        into = d.editor.part(*sc, *d.part);
                    }
            const std::string line = std::format("image \"{}\" x={} y={}", firstImage, unitText(placeFor(px - box.x, 0.0f, box.w)), unitText(-placeFor(py - box.y, 0.0f, box.h)));
            d.part = d.editor.addPart(*sc, into ? &*into : nullptr, line);
            d.moreParts.clear();
            logInfo("{}", "Imported '{}', and put it on the screen."_tr(last));
            placed = true;
        }
    if (copied > 0 && !placed) d.view = DeveloperTools::View::Library;
}

// Opens where a line of the scripts is (`line` counting from 0): its scene's line in the Story tab,
// its screen's part in Screens, its character, or what it lists in the Library (a setting: its Settings).
// False when it's none of those.
bool Developer::goTo(const std::string& path, std::size_t line) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    std::optional<std::size_t> file;
    for (std::size_t f = 0; f < editor.fileCount(); ++f)
        if (lower(editor.path(f)) == lower(path)) file = f;
    if (!file) return false;
    d.focus.clear();
    const auto& scenes = editor.scenes();
    for (std::size_t i = 0; i < scenes.size(); ++i)
        if (scenes[i].file == *file && scenes[i].line <= line && line < std::max(scenes[i].end, scenes[i].line + 1)) {
            d.view = View::Story;
            d.scene = i;
            d.character.reset();
            d.asText = false;
            d.row.reset();
            const auto rows = editor.rows(scenes[i]);
            if (const auto row = std::ranges::find(rows, line, &StoryEditor::Row::line); row != rows.end()) {
                d.row = line;
                d.linesScroll = std::max(0.0f, static_cast<float>(row - rows.begin() - 4) * layout::listItemH * d.k); // (a few lines over it in sight)
            }
            return true;
        }
    for (const auto& s : editor.screens())
        if (s.file == *file && s.line <= line && line < std::max(s.end, s.line + 1)) {
            d.view = View::Screens;
            d.screenName = s.name;
            d.picking = false;
            d.part = line > s.line ? std::optional(line) : std::nullopt;
            d.moreParts.clear();
            d.asText = false;
            return true;
        }
    if (*file == 0 && editor.isSetting(line)) {
        d.view = View::Library;
        d.library = "settings";
        return true;
    }
    const auto characters = editor.characters();
    for (std::size_t i = 0; i < characters.size(); ++i)
        if (characters[i].file == *file && characters[i].line == line) {
            d.view = View::Story;
            d.row.reset();
            d.character = i;
            return true;
        }
    for (const DefinitionKind& kind : kDefinitionKinds) {
        const auto defs = editor.definitions(kind.kind);
        for (std::size_t i = 0; i < defs.size(); ++i)
            if (defs[i].file == *file && defs[i].line <= line && line < defs[i].end) {
                if (kind.kind == "place") return false; // (not listed)
                if (kind.kind == "gallery" || kind.kind == "song") { // (its image, sub-image, movie or song, with its title)
                    const std::string name = lower(defs[i].name);
                    d.view = View::Library;
                    d.library = kind.kind == "song" ? "sounds" : engine.assets().findMovie(name) ? "movies" : "images";
                    d.libraryItem.reset();
                    d.librarySub.reset();
                    d.libraryFind = name;
                    if (d.library == "images") {
                        const auto subs = editor.definitions("sub-image");
                        for (std::size_t j = 0; j < subs.size(); ++j)
                            if (lower(subs[j].name) == name) d.librarySub = j;
                    }
                    d.formScroll = 0.0f;
                    return true;
                }
                d.view = View::Library;
                d.library = kind.kind;
                d.libraryItem = i;
                d.librarySub.reset();
                d.pickingAnimation = false; // (the definition, not + New animation's list)
                if (kind.kind == "sub-image" || kind.kind == "font") { // (under the file it's made from)
                    d.library = kind.kind == "sub-image" ? "images" : "fontfiles";
                    d.libraryItem.reset();
                    d.librarySub = i;
                }
                d.formScroll = 0.0f;
                return true;
            }
    }
    for (const auto& def : editor.definitions("variable")) // (listed with the others, by name)
        if (def.file == *file && def.line == line) {
            std::vector<std::string> names = editor.variables();
            if (engine.mode() == Engine::Mode::Game && engine.game_)
                for (const auto& [name, value] : engine.game_->variables())
                    if (!std::ranges::contains(names, lower(name))) names.push_back(lower(name));
            std::ranges::sort(names);
            d.view = View::Library;
            d.library = "variables";
            const auto at = std::ranges::find(names, lower(def.name));
            d.libraryItem = at != names.end() ? std::optional(static_cast<std::size_t>(at - names.begin())) : std::nullopt;
            return true;
        }
    return false;
}

// The developer tools' own folder (with --data, that one): it keeps the language chosen for them, and their layout.
std::string Developer::toolsFolder() const {
    if (engine.dataFolder_) return engine.prefPath();
    const SdlOwned<char> pref{SDL_GetPrefPath("VNext", "Developer")};
    return pref ? std::string(pref.get()) : std::string{};
}

std::string Developer::toolsLanguagesFolder() const { return engine.gamePlaces().front() + "developer tools translations/"; }

void Developer::findToolsLanguages() {
    DeveloperTools& d = tools;
    d.toolsLanguages.clear();
    const std::string folder = toolsLanguagesFolder();
    d.toolsLanguageFiles = listFiles(folder, ".vnscript");
    for (const std::string& name : d.toolsLanguageFiles) {
        if (name.contains('/')) continue;
        const auto text = readTextFile(folder + name);
        if (!text) continue;
        std::vector<ScriptError> errors;
        const Translation t = Translation::parse(stemOf(name), *text, errors);
        d.toolsLanguages.push_back({.id = t.id, .name = t.name, .code = t.code});
    }
    std::ranges::sort(d.toolsLanguages, {}, &DeveloperTools::ToolsLanguage::name);
}

void Developer::useToolsLanguage(const std::string& id) {
    DeveloperTools& d = tools;
    d.toolsLanguage.clear();
    useToolsTranslation({});
    if (id.empty()) return;
    const std::string path = toolsLanguagesFolder() + id + ".vnscript";
    const auto text = readTextFile(path);
    if (!text) {
        logWarn("{}", "Can't read the developer tools' language '{}'."_tr(path));
        return;
    }
    std::vector<ScriptError> errors;
    Translation t = Translation::parse(id, *text, errors);
    for (const ScriptError& e : errors) logWarn("developer tools translations/{}.vnscript line {}: {}", id, e.line, e.message);
    useToolsTranslation(std::move(t.texts));
    d.toolsLanguage = id;
}

void Developer::startToolsLanguage() {
    DeveloperTools& d = tools;
    findToolsLanguages();
    if (d.toolsLanguages.empty()) return; // (none there: English, and their own folder isn't looked at, so a player's computer gets none)
    auto known = [&](std::string_view id) { return std::ranges::contains(d.toolsLanguages, id, &DeveloperTools::ToolsLanguage::id); };
    if (const auto chosen = readTextFile(toolsFolder() + "language.txt")) {
        const std::string id(trim(*chosen));
        return useToolsLanguage(known(id) ? id : std::string{}); // (its file gone: English)
    }
    int count = 0; // (none chosen: the computer's language, when there's a translation for it)
    const SdlOwned<SDL_Locale*> locales{SDL_GetPreferredLocales(&count)};
    for (int i = 0; locales && i < count; ++i) {
        const std::string language = lower(locales.get()[i]->language), country = locales.get()[i]->country ? lower(locales.get()[i]->country) : "";
        for (const DeveloperTools::ToolsLanguage& l : d.toolsLanguages)
            if (!l.code.empty() && (l.code == language + "-" + country || l.code == language)) return useToolsLanguage(l.id);
    }
}

void Developer::writeToolsTranslation() {
    DeveloperTools& d = tools;
    const std::string folder = toolsLanguagesFolder();
    SDL_CreateDirectory(folder.c_str());
    const std::string id = d.toolsLanguage.empty() ? "new" : d.toolsLanguage;
    const std::string path = folder + id + ".vnscript";
    const auto existing = readTextFile(path);
    std::vector<ScriptError> errors;
    const Translation had = existing ? Translation::parse(id, *existing, errors) : Translation{};
    std::string out = existing ? *existing
                               : "# The developer tools' texts in another language: under each one, indented, its translation (one left as \"\"\n"
                                 "# stays in English). Keep each {} in it: a name, a number or a file goes there. Change the language line to\n"
                                 "# the language's own name and its code (es, pt-br, ja...), name the file after the language\n"
                                 "# (spanish.vnscript), keep it here, next to VNext, and choose it with its button in the developer tools.\n"
                                 "language \"New language\"\n";
    std::size_t added = 0;
    for (const std::string& text : toolsTexts()) {
        if (text.contains('\n') || (text.contains('"') && text.contains('#')) || had.texts.contains(text)) continue; // (a text a line: as the file has them)
        if (existing && added == 0) out += std::string(out.ends_with('\n') ? "" : "\n") + "\n# Texts added since the file was made.\n";
        out += std::format("\n{}\n    \"\"\n", text.contains('"') ? text : "\"" + text + "\"");
        ++added;
    }
    d.failed = !SDL_SaveFile(path.c_str(), out.data(), out.size());
    if (d.failed) {
        d.status = "Can't write '{}': {}."_tr(path, SDL_GetError());
        return;
    }
    d.status = existing ? "Added {} texts the developer tools show to 'developer tools translations/{}.vnscript'."_tr(added, id)
                        : "Made 'developer tools translations/{}.vnscript', with the {} texts the developer tools show, ready to translate."_tr(id, added);
    findToolsLanguages();
    if (!SDL_OpenURL(fileUrl(folder).c_str())) d.status += " " + std::string("Can't show '{}': {}."_tr(folder, SDL_GetError()));
}

} // namespace vn
