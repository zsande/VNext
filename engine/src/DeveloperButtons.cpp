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
    // A page shown; what Undo, Cut and the others like them do with what's chosen or typed into (as their keys, pressed
    // now); moving page parts and putting a page's back; the tools' language, and a file to translate them; About VNext.
    auto press = [&](ToolKey which) { pressTool(which); };
    auto show = [&](Page page) {
        if (d.page == page) return;
        d.page = page;
        d.aboutScroll = 0;
        d.focus.clear();
    };
    if (const auto page = std::ranges::find(kPages, command, &std::pair<ToolCommand, Page>::first); page != kPages.end()) {
        if (!engine.noGame() && d.editor.loaded()) show(page->second); // (without a game, its page is all there is)
        return;
    }
    switch (command) {
    case kAbout: // (and again: back to the page it was opened from)
        if (d.page == Page::About) return show(d.beforeAbout);
        d.beforeAbout = d.page;
        d.editLayout = false; // (About is one page part: nothing to lay out)
        d.pagePartDrag.reset();
        return show(Page::About);
    case kUndo: return press(ToolKey::Undo);
    case kRedo: return press(ToolKey::Redo);
    case kCut: return press(ToolKey::Cut);
    case kCopy: return press(ToolKey::Copy);
    case kPaste: return press(ToolKey::Paste);
    case kDuplicate: return press(ToolKey::Duplicate);
    case kDelete: return press(ToolKey::Delete);
    case kSelectAll: return press(ToolKey::SelectAll);
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
    case kNextLine: // (the Story page form's, at the story's line: on to the next line it gets to, and stopped there)
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
    case kPlayScene: // (the Story page form's: the scene chosen, or Play from here, from the line chosen)
        if (d.page == Page::Story && d.scene < d.editor.scenes().size()) {
            d.playScene = d.editor.scenes()[d.scene].name;
            d.playLine.reset();
            if (d.row && d.moreRows.empty() && !d.visualType && !d.character && !d.namedVisual && !d.soundType && !d.namedSound && std::ranges::contains(d.editor.rows(d.editor.scenes()[d.scene]), *d.row, &StoryEditor::Row::line))
                d.playLine = d.row;
            engine.reloadRequested_ = true;
        }
        break;
    case kReload:
        engine.reloadRequested_ = true;
        break;
    case kExport: {
        if (d.exporting.joinable()) break; // (one at a time)
        if (const std::string folder = engine.finishedFolder(); SDL_GetPathInfo(folder.c_str(), nullptr)) { // (one exported before: replaced if the developer says so)
            ask("Replace the exported game?"_tr(), "There's an exported game in '{}' already. Exporting again puts it in the Trash."_tr(folder),
                {"Replace it"_tr(), "Keep it"_tr()}, 1, SDL_MESSAGEBOX_WARNING, [this, folder](std::size_t answer) {
                    if (answer != 0) return logInfo("{}", "The exported game in '{}' stays as it was."_tr(folder));
                    tools.exportIn = 2; // (once the game's been read again, next frame)
                    engine.reloadRequested_ = true;
                });
            break;
        }
        d.exportIn = 2; // (once the game's been read again, next frame)
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
            d.status = "There's still no game folder next to VNext."_tr;
            break;
        }
        // Its menus, ready-made: saving, loading, Settings, the history and its question, then the pause menu and the
        // title screen with a button for each; as the game starts, so nothing to undo.
        if (d.editor.loaded()) {
            for (const std::string_view screen : {"save", "load", "settings", "history", "confirm", "pause", "title"}) addReadyMadeScreen(screen);
            d.editor.load(engine.gameRoot());
        }
        break;
    }
    case kLookAgain:
        if (!engine.lookForGame()) {
            d.failed = true;
            d.status = "There's still no game folder or game.vnext next to VNext."_tr;
        }
        break;
    case kQuit:
        engine.quit_ = true;
        break;
    default:
        break;
    }
}

// The tools' own part's buttons as the tools are now, group by group, in one row: the pages; Play (the game from the
// start, or back to it: the scene or from the line chosen are the Story page form's); then menus: Edit (changing what's
// chosen), Game (its files: reloaded, exported, its folder shown; New game and Look again are the page without a game's)
// and View (the layout: the page parts moved and resized, and the page's put back; the tools' language: English, the
// translations, a file to translate them); About VNext and Quit. They're always all there, each in its place (one whose
// words change is as wide as the longest of them): one that can't be pressed now is grey (a menu, when none in it can),
// one that's on is lit (the page shown, moving and resizing page parts, the language in use, About VNext), and a page
// with a mistake in what it shows has the lists' red mark.
std::vector<ToolGroup> Developer::toolButtons() {
    DeveloperTools& d = tools;
    const bool game = !engine.noGame(), editing = game && d.editor.loaded();
    auto button = [](int id, std::string_view label, bool enabled = true, bool on = false, bool lineBefore = false) {
        return ToolButton{.id = id, .label = std::string(label), .longest = {}, .enabled = enabled, .on = on, .wrong = false, .lineBefore = lineBefore};
    };
    std::vector<ToolGroup> out;

    auto wrongIn = [&](const auto& items) { return std::ranges::any_of(items, [&](const auto& it) { return d.problemWithin(it.file, it.line, it.end); }); };
    auto wrong = [&](Page page) {
        if (!editing) return false;
        switch (page) {
        case Page::Story: // (its scenes, and the visual types listed there with the visuals named)
            return std::ranges::contains(d.wrongScenes(), true) || wrongIn(d.editor.definitions("type")) || wrongIn(d.editor.definitions("visual")) ||
                   wrongIn(d.editor.definitions("sound type")) || wrongIn(d.editor.definitions("sound"));
        case Page::Screens: return wrongIn(d.editor.screens());
        case Page::Library:
            return wrongIn(d.editor.definitions("variable")) || std::ranges::any_of(d.problems, [&](const ScriptError& e) { return inSettings(d.editor, e); }) ||
                   std::ranges::any_of(kDefinitionKinds, [&](const DefinitionKind& kind) { return kind.kind != "type" && kind.kind != "visual" && kind.kind != "sound type" && kind.kind != "sound" && wrongIn(d.editor.definitions(kind.kind)); });
        default: return false;
        }
    };
    ToolGroup& pages = out.emplace_back();
    for (const auto& [id, page] : kPages) {
        ToolButton b = button(id, pageName(page), editing, editing && d.page == page);
        b.wrong = wrong(page);
        pages.buttons.push_back(std::move(b));
    }

    // (Play scene, Play from here and Next line are the Story page form's, as what's chosen there has them)
    ToolButton play = button(kPlay, !game || engine.waiting_ ? "Play"_tr : "Resume"_tr, game);
    play.longest = "Resume"_tr;
    out.push_back({.menu = {}, .buttons = {std::move(play)}});

    // (undo and redo: the editor's steps, or the Library's translations' own; none of them on About, nor while laying out)
    const bool changing = editing && d.page != Page::About && !d.editLayout;
    const bool translating = d.page == Page::Library && d.library == "translations";
    const bool undoes = changing && (translating ? !d.translateUndo.empty() : d.editor.canUndo());
    const bool redoes = changing && (translating ? !d.translateRedo.empty() : d.editor.canRedo());
    const DeveloperTools::Acts& can = d.shownActs; // (what the page has for them to act on: a box typed into, what's chosen...)
    out.push_back({.menu = "Edit"_tr,
                   .buttons = {button(kUndo, "Undo"_tr, undoes), button(kRedo, "Redo"_tr, redoes), button(kCut, "Cut"_tr, changing && can.cut, false, true),
                               button(kCopy, "Copy"_tr, changing && can.copy), button(kPaste, "Paste"_tr, changing && can.paste),
                               button(kDuplicate, "Duplicate"_tr, changing && can.duplicate), button(kDelete, "Delete"_tr, changing && can.remove),
                               button(kSelectAll, "Select all"_tr, changing && can.selectAll, false, true)}});

    out.push_back({.menu = "Game"_tr,
                   .buttons = {button(kReload, "Reload the game"_tr, game),
                               button(kExport, d.exporting.joinable() ? "Exporting the game..."_tr : "Export the game"_tr, game && !d.exporting.joinable()),
                               button(kFolder, game ? "Show the game's folder"_tr : "Show the folder"_tr)}});

    // The layout; then the developer tools' language: English, the translations in 'developer tools translations/' by name,
    // and a file to translate them (with one in use: the texts it doesn't have yet, added).
    const bool moved = std::ranges::any_of(d.pageParts, [&](const auto& placed) { return placed.first.starts_with(d.shownPage + "/"); });
    ToolGroup& view = out.emplace_back(ToolGroup{.menu = "View"_tr, .buttons = {}});
    view.buttons = {button(kEditLayout, "Move and resize page parts"_tr, editing && d.page != Page::About, d.editLayout),
                    button(kResetLayout, "Reset this page's layout"_tr, moved)};
    if (d.cachedFiles(toolsLanguagesFolder(), ".vnscript") != d.toolsLanguageFiles) findToolsLanguages(); // (one added or taken away)
    view.buttons.push_back(button(kToolsLanguage, "English", true, d.toolsLanguage.empty(), true));
    for (std::size_t i = 0; i < d.toolsLanguages.size(); ++i)
        view.buttons.push_back(button(kToolsLanguage + 1 + static_cast<int>(i), d.toolsLanguages[i].name, true, d.toolsLanguages[i].id == d.toolsLanguage));
    view.buttons.push_back(button(kToolsTranslation, "Translate the developer tools"_tr, true, false, true));

    out.push_back({.menu = {}, .buttons = {button(kAbout, "About VNext"_tr, true, d.page == Page::About), button(kQuit, "Quit"_tr)}});
    return out;
}

// Where the tools' own part's buttons go, from (x, y) in `w`: each as wide as its longest words (small, as a menu bar's; a
// menu's, with room for its arrow), a group's side by side and the groups further apart with a line between them (menus
// side by side are as a group's buttons: Edit, Game and View), all in one row in the middle of `w` (narrower when it's
// less than they take: their words cut, each whole in a tip); and how tall.
Developer::ToolsPlaced Developer::placeToolButtons(DeveloperUi& ui, const std::vector<ToolGroup>& groups, float x, float y, float w) {
    const float k = ui.k, buttonH = layout::toolButtonH * k, gap = layout::toolGap * k, groupGap = layout::toolGroupGap * k;
    auto widthOf = [&](std::string_view label, std::string_view longest, bool menu) {
        const float words = std::max(ui.width(label, layout::toolText), longest.empty() ? 0.0f : ui.width(longest, layout::toolText));
        return std::round(words + 2 * layout::toolPad * k + (menu ? layout::toolArrowW * k : 0.0f));
    };
    auto apart = [&](std::size_t g) { return g > 0 && (groups[g].menu.empty() || groups[g - 1].menu.empty()); }; // (from the group before it)
    std::vector<std::vector<float>> widths;
    float buttonsW = 0.0f, gapsW = 0.0f;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        std::vector<float>& each = widths.emplace_back();
        if (!groups[g].menu.empty()) each.push_back(widthOf(groups[g].menu, {}, true));
        else
            for (const ToolButton& b : groups[g].buttons) each.push_back(widthOf(b.label, b.longest, false));
        for (const float bw : each) buttonsW += bw;
        gapsW += gap * static_cast<float>(each.size() - 1) + (g == 0 ? 0.0f : apart(g) ? groupGap : gap);
    }
    const float scale = buttonsW + gapsW > w && buttonsW > 0.0f ? std::max(0.0f, w - gapsW) / buttonsW : 1.0f;
    ToolsPlaced out;
    float cx = x + std::floor(std::max(0.0f, w - (buttonsW * scale + gapsW)) / 2);
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if (apart(g)) { // (the line in the middle of the gap)
            cx += groupGap - gap;
            out.lines.push_back({std::round(cx - groupGap / 2), y, std::max(1.0f, std::round(k)), buttonH});
        }
        std::vector<SDL_FRect>& at = out.buttons.emplace_back();
        for (const float bw : widths[g]) {
            const float shown = std::floor(bw * scale);
            at.push_back({cx, y, shown, buttonH});
            cx += shown + gap;
        }
    }
    out.h = groups.empty() ? 0.0f : buttonH;
    return out;
}

namespace {

// A menu's first item that can be pressed, from `from` on (back or forth, round to the other end).
std::optional<std::size_t> pressable(const std::vector<DeveloperTools::ToolsMenu::Item>& items, std::optional<std::size_t> from, int by) {
    const std::size_t n = items.size();
    if (n == 0) return std::nullopt;
    std::size_t i = from ? *from : by > 0 ? n - 1 : 0;
    for (std::size_t tries = 0; tries < n; ++tries) {
        i = by > 0 ? (i + 1) % n : (i + n - 1) % n;
        if (items[i].enabled) return i;
    }
    return std::nullopt;
}

std::string itemWords(const ToolButton& b) { // (what a screen reader says of it, as of a button)
    std::string words = b.label;
    if (b.on) words += ", " + std::string("chosen"_tr);
    if (!b.enabled) words += ", " + std::string("not now"_tr);
    return words;
}

} // namespace

// An open menu as the frame starts: a click on it is its own (nothing under it gets it, nor the pointer), a click anywhere
// else but its button shuts it, and it takes the keys that go through it.
void Developer::toolsMenuFrame() {
    DeveloperTools& d = tools;
    auto& m = d.toolsMenu;
    m.clicked = false;
    if (!m.group) return;
    if (pointInRect(d.mouseX, d.mouseY, m.box)) {
        d.overPopup = true;
        m.clicked = std::exchange(d.clicked, false);
    } else if (d.clicked && !pointInRect(d.mouseX, d.mouseY, m.button)) {
        m.group.reset();
        return;
    }
    auto keyTo = [&](std::optional<std::size_t> to) {
        if (!to || to == m.keyed) return;
        m.keyed = to;
        if (engine.screenReader()) announce(m.items[*to].words);
    };
    std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
        if (!m.group) return false;
        if (d.isKey(key, ToolKey::Up, true)) return keyTo(pressable(m.items, m.keyed, -1)), true;
        if (d.isKey(key, ToolKey::Down, true)) return keyTo(pressable(m.items, m.keyed, 1)), true;
        if (const bool left = d.isKey(key, ToolKey::Left, true); left || d.isKey(key, ToolKey::Right, true)) { // (the menu beside it)
            if (const auto at = std::ranges::find(m.menus, *m.group); at != m.menus.end() && m.menus.size() > 1) {
                const auto i = static_cast<std::size_t>(at - m.menus.begin()), n = m.menus.size();
                m.group = m.menus[left ? (i + n - 1) % n : (i + 1) % n];
                m.keyed.reset();
                m.keyFirst = true;
            }
            return true;
        }
        if (d.isKey(key, ToolKey::Press)) {
            if (m.keyed && *m.keyed < m.items.size() && m.items[*m.keyed].enabled) d.pressedTool = m.items[*m.keyed].id, m.group.reset();
            return true;
        }
        if (d.isKey(key, ToolKey::Back)) return m.group.reset(), true;
        if (d.isKey(key, ToolKey::Next) || d.isKey(key, ToolKey::Previous)) m.group.reset(); // (the keys go on elsewhere: it shuts)
        return false;
    });
}

// The tools' own part, at `at`: drawn as the pages' page parts are (but never lit: it isn't moved), with its buttons in the
// middle; the one pressed is done as the next frame starts (with what it acts on as it is now). A menu's button opens its
// menu (and again: shuts it); by the keys, they go on its first item that can be pressed.
Developer::ToolsPlaced Developer::drawToolsPart(DeveloperUi& ui, const std::vector<ToolGroup>& groups, const SDL_FRect& at) {
    DeveloperTools& d = tools;
    auto& m = d.toolsMenu;
    const float k = ui.k;
    draw::rect(ui.r, at, kPanel);
    draw::outline(ui.r, at, kEdge);
    const SDL_FPoint chrome = pagePartChrome(k);
    const SDL_FRect box{at.x + chrome.x / 2, at.y + chrome.y / 2, std::max(0.0f, at.w - chrome.x), std::max(0.0f, at.h - chrome.y)};
    const DeveloperUi::Clip clipped(ui, box);
    const ToolsPlaced placed = placeToolButtons(ui, groups, box.x, box.y, box.w);
    for (const SDL_FRect& line : placed.lines) draw::rect(ui.r, line, kEdge);
    m.menus.clear();
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const ToolGroup& group = groups[g];
        if (group.menu.empty()) {
            for (std::size_t i = 0; i < group.buttons.size(); ++i) {
                const ToolButton& b = group.buttons[i];
                const SDL_FRect& place = placed.buttons[g][i];
                if (ui.button(place, b.label, b.on, b.enabled, layout::toolText)) d.pressedTool = b.id;
                if (b.wrong) draw::rect(ui.r, {place.x, place.y, layout::markW * k, place.h}, kBad);
            }
            continue;
        }
        m.menus.push_back(g);
        const SDL_FRect& place = placed.buttons[g][0];
        const bool open = m.group == g, enabled = std::ranges::any_of(group.buttons, &ToolButton::enabled);
        std::string words = std::format("{}, {}", group.menu, std::string("menu"_tr));
        if (!enabled) words += ", " + std::string("not now"_tr);
        const DeveloperUi::KeyStopNow keys = ui.keyStop(DeveloperTools::KeyStop::Kind::Press, place, std::move(words));
        draw::rect(ui.r, place, open ? kChosen : enabled && (ui.over(place) || keys.lit) ? kButtonHover : kButton);
        draw::outline(ui.r, place, kEdge);
        const float arrowW = layout::toolArrowW * k;
        const SDL_FRect label{place.x, place.y, std::max(0.0f, place.w - arrowW), place.h};
        const std::string shown = ui.fitted(group.menu, layout::toolText, std::max(0.0f, label.w - 2 * k));
        ui.textIn(shown, {label.x + arrowW / 3, label.y, label.w, label.h}, layout::toolText, enabled ? theme::text : theme::dim);
        ui.triangle(place.x + place.w - arrowW / 2 - layout::toolPad * k / 3, place.y + place.h / 2, 4 * k, open ? DeveloperUi::Pointing::Down : DeveloperUi::Pointing::Up,
                    enabled ? theme::text : theme::dim);
        if (shown != group.menu) ui.tip(place, group.menu);
        if ((ui.click(place) || keys.pressed) && enabled) {
            if (open) {
                m.group.reset();
            } else {
                m.group = g;
                m.keyed.reset();
                m.keyFirst = keys.pressed;
                d.popup.id.clear(); // (a box's list shuts: one open thing at a time)
            }
        }
    }
    return placed;
}

// The menu open, over the rest, above its button (its left edge with the button's, kept in the window): its items in a
// column, a line between their kinds, each lit as it's pointed at (or the keys are on it), grey when it can't be pressed;
// one pressed is done as the next frame starts, and the menu shuts.
void Developer::drawToolsMenu(DeveloperUi& ui, const std::vector<ToolGroup>& groups, const ToolsPlaced& placed) {
    DeveloperTools& d = tools;
    auto& m = d.toolsMenu;
    if (m.group && (*m.group >= groups.size() || groups[*m.group].menu.empty() || !std::ranges::any_of(groups[*m.group].buttons, &ToolButton::enabled)))
        m.group.reset();
    if (!m.group) {
        m.box = m.button = {};
        m.items.clear();
        return;
    }
    const ToolGroup& group = groups[*m.group];
    const float k = ui.k, rowH = layout::toolMenuRowH * k, pad = layout::toolPad * k, lineH = layout::gap * k, edge = std::max(1.0f, std::round(k));
    m.button = placed.buttons[*m.group][0];
    m.items.clear();
    float w = m.button.w, h = 2 * edge;
    for (const ToolButton& b : group.buttons) {
        w = std::max(w, std::round(ui.width(b.label, layout::toolText) + 4 * pad));
        h += rowH + (b.lineBefore ? lineH : 0.0f);
        m.items.push_back({.id = b.id, .words = itemWords(b), .enabled = b.enabled});
    }
    const float roomW = d.size.x;
    m.box = {std::max(0.0f, std::min(m.button.x, roomW - w)), std::max(0.0f, m.button.y - h - layout::toolGap * k), w, h};
    if (m.keyFirst) {
        m.keyFirst = false;
        m.keyed = pressable(m.items, std::nullopt, 1);
        if (m.keyed && engine.screenReader()) announce(m.items[*m.keyed].words);
    }
    // (what's in it takes the click taken for it, and the pointer over it; its items are its own keys', not stops)
    const bool wasOver = std::exchange(d.overPopup, false), wasOff = std::exchange(d.keyStopsOff, true);
    d.clicked = m.clicked;
    draw::rect(ui.r, m.box, kPanel);
    draw::outline(ui.r, m.box, kEdge);
    float y = m.box.y + edge;
    for (std::size_t i = 0; i < group.buttons.size(); ++i) {
        const ToolButton& b = group.buttons[i];
        if (b.lineBefore) {
            draw::rect(ui.r, {m.box.x + pad, std::round(y + lineH / 2), m.box.w - 2 * pad, edge}, kEdge);
            y += lineH;
        }
        const SDL_FRect row{m.box.x + edge, y, m.box.w - 2 * edge, rowH};
        const bool keyed = m.keyed == i, over = ui.over(row);
        draw::rect(ui.r, row, b.on ? kChosen : b.enabled && (over || keyed) ? kButtonHover : kPanel);
        if (keyed) draw::outline(ui.r, row, kAccent);
        ui.textFitted(b.label, row.x + pad, row.y + row.h / 2, layout::toolText, b.enabled ? theme::text : theme::dim, row.w - 2 * pad);
        if (b.enabled && ui.click(row)) {
            d.pressedTool = b.id;
            m.group.reset();
        }
        y += rowH;
    }
    d.clicked = false;
    d.overPopup = wasOver;
    d.keyStopsOff = wasOff;
}

} // namespace vn
