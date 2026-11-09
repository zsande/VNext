// The developer tools' own part: the buttons at the window's foot, and what each does.
#include "DeveloperUi.hpp"

#include <cmath>

namespace vn {

namespace {

// What New game writes: game/scripts/main.vnscript.
constexpr std::string_view kNewGameScript = R"vnext(# The game starts here: its settings, then scene start, where the story begins.
setting developer yes

scene start
    This is the first line of the story.
)vnext";

} // namespace

void Developer::runCommand(int command) {
    DeveloperTools& d = tools;
    // A page shown; what Undo, Cut and the others like them, and Stop here, do with what's chosen or typed into (as their
    // keys, pressed now); Layout and Reset; the tools' language, and a file to translate them; About.
    auto press = [&](ToolKey which) { pressTool(which); };
    auto show = [&](Page page) {
        if (d.page == page) return;
        d.page = page;
        d.aboutScroll = 0;
        d.focus.clear();
    };
    if (const auto page = std::ranges::find(kPages, command, &std::pair<ToolCommand, Page>::first); page != kPages.end()) return show(page->second); // (without a game: what to do)
    switch (command) {
    case kAbout: return show(Page::About);
    case kUndo: return press(ToolKey::Undo);
    case kRedo: return press(ToolKey::Redo);
    case kCut: return press(ToolKey::Cut);
    case kCopy: return press(ToolKey::Copy);
    case kPaste: return press(ToolKey::Paste);
    case kDuplicate: return press(ToolKey::Duplicate);
    case kDelete: return press(ToolKey::Delete);
    case kSelectAll: return press(ToolKey::SelectAll);
    case kStopHere:
        if (d.page == Page::Story && d.stoppable) toggleStop(d.stoppable->first, d.stoppable->second);
        return;
    case kEditLayout: // (what's in the page parts takes no pointer while they're moved: nothing's typed into or open then)
        d.editLayout = !d.editLayout;
        d.pagePartDrag.reset();
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
        if (d.page == Page::Story && d.scene < d.editor.scenes().size()) {
            d.playScene = d.editor.scenes()[d.scene].name;
            d.playLine.reset();
            if (d.page == Page::Story && d.row && d.moreRows.empty() && !d.character &&
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
            ask("Replace the packed game?"_tr(), "There's a packed game in '{}' already. Packing again puts it in the Trash."_tr(folder),
                {"Replace it"_tr(), "Keep it"_tr()}, 1, SDL_MESSAGEBOX_WARNING, [this, folder](std::size_t answer) {
                    if (answer != 0) return logInfo("{}", "The packed game in '{}' stays as it was."_tr(folder));
                    tools.packIn = 2; // (once the game's been read again, next frame)
                    engine.reloadRequested_ = true;
                });
            break;
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
        if (!engine.lookForGame()) { // (in developer mode, so it opens in the Story page)
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

// The tools' own part's buttons as the tools are now, group by group, in one row (two in a small window): the pages;
// playing the game (from the start or back to it, the scene or from the line chosen, the next line, Stop here);
// changing what's chosen; the game's files (New game and Look again without one, Reload, Pack, its folder); the
// layout (Layout, and Reset: the page's put back); the tools' language (English, the translations, a file to
// translate them); About and Quit. They're always all there, each in its place (one whose words change is as wide
// as the longest of them): one that can't be pressed now is grey, one that's on is lit (the page shown, Stop here,
// Layout, the language in use, About), and a page with a mistake in what it shows has the lists' red mark.
std::vector<std::vector<ToolButton>> Developer::toolButtons() {
    DeveloperTools& d = tools;
    const bool game = !engine.noGame(), editing = game && d.editor.loaded();
    auto button = [](int id, std::string_view label, bool enabled = true, bool on = false) {
        return ToolButton{.id = id, .label = std::string(label), .longest = {}, .enabled = enabled, .on = on};
    };
    std::vector<std::vector<ToolButton>> out;

    auto wrongIn = [&](const auto& items) { return std::ranges::any_of(items, [&](const auto& it) { return d.problemWithin(it.file, it.line, it.end); }); };
    auto wrong = [&](Page page) {
        if (!editing) return false;
        switch (page) {
        case Page::Story: return std::ranges::contains(d.wrongScenes(), true);
        case Page::Screens: return wrongIn(d.editor.screens());
        case Page::Library:
            return wrongIn(d.editor.definitions("variable")) || std::ranges::any_of(d.problems, [&](const ScriptError& e) { return inSettings(d.editor, e); }) ||
                   std::ranges::any_of(kDefinitionKinds, [&](const DefinitionKind& kind) { return wrongIn(d.editor.definitions(kind.kind)); });
        default: return false;
        }
    };
    std::vector<ToolButton> pages;
    for (const auto& [id, page] : kPages) {
        ToolButton b = button(id, pageName(page), true, d.page == page);
        b.wrong = wrong(page);
        pages.push_back(std::move(b));
    }
    out.push_back(std::move(pages));

    const bool story = editing && d.page == Page::Story, fromLine = story && d.row && d.moreRows.empty() && !d.character;
    const bool stoppable = story && d.stoppable.has_value();
    ToolButton play = button(kPlay, !game || engine.waiting_ ? "Play"_tr : "Resume"_tr, game);
    ToolButton scene = button(kPlayScene, fromLine ? "Play from line"_tr : "Play scene"_tr, story && !d.editor.scenes().empty());
    play.longest = "Resume"_tr;
    scene.longest = "Play from line"_tr;
    out.push_back({std::move(play), std::move(scene), button(kNextLine, "Next line"_tr, game && storyAt().has_value()),
                   button(kStopHere, "Stop here"_tr, stoppable, stoppable && hasStop(d.stoppable->first, d.stoppable->second))});

    // (undo and redo: the editor's steps, or the Library's translations' own)
    const bool translating = d.page == Page::Library && d.library == "translations", undoing = editing && d.page != Page::About;
    const bool undoes = translating ? !d.translateUndo.empty() : undoing && d.editor.canUndo();
    const bool redoes = translating ? !d.translateRedo.empty() : undoing && d.editor.canRedo();
    out.push_back({button(kUndo, "Undo"_tr, undoes), button(kRedo, "Redo"_tr, redoes), button(kCut, "Cut"_tr, editing), button(kCopy, "Copy"_tr, editing),
                   button(kPaste, "Paste"_tr, editing), button(kDuplicate, "Duplicate"_tr, editing), button(kDelete, "Delete"_tr, editing),
                   button(kSelectAll, "Select all"_tr, editing)});

    out.push_back({button(kNewGame, "New game"_tr, !game), button(kLookAgain, "Look again"_tr, !game), button(kReload, "Reload"_tr, game),
                   button(kPack, "Pack"_tr, game), button(kFolder, "Folder"_tr)});

    const bool moved = std::ranges::any_of(d.pageParts, [&](const auto& placed) { return placed.first.starts_with(d.shownPage + "/"); });
    out.push_back({button(kEditLayout, "Layout"_tr, true, d.editLayout), button(kResetLayout, "Reset"_tr, moved)});

    // The developer tools' language: English, then the translations in 'developer tools translations/' by name, and a
    // file to translate them (with one in use: the texts it doesn't have yet, added).
    if (d.cachedFiles(toolsLanguagesFolder(), ".vnscript") != d.toolsLanguageFiles) findToolsLanguages(); // (one added or taken away)
    std::vector<ToolButton> language{button(kToolsLanguage, "English", true, d.toolsLanguage.empty())};
    for (std::size_t i = 0; i < d.toolsLanguages.size(); ++i)
        language.push_back(button(kToolsLanguage + 1 + static_cast<int>(i), d.toolsLanguages[i].name, true, d.toolsLanguages[i].id == d.toolsLanguage));
    language.push_back(button(kToolsTranslation, "Translate"_tr));
    out.push_back(std::move(language));

    out.push_back({button(kAbout, "About"_tr, true, d.page == Page::About), button(kQuit, "Quit"_tr)});
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

// The tools' own part, at `at`: drawn as the pages' page parts are (but never lit: it isn't moved), with its buttons in the
// middle; the one pressed is done as the next frame starts (with what it acts on as it is now).
void Developer::drawToolsPart(DeveloperUi& ui, const std::vector<std::vector<ToolButton>>& groups, const SDL_FRect& at) {
    DeveloperTools& d = tools;
    const float k = ui.k;
    draw::rect(ui.r, at, kPanel);
    draw::outline(ui.r, at, kEdge);
    const SDL_FPoint chrome = pagePartChrome(k);
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

} // namespace vn
