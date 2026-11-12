// What developer mode (setting developer yes, in main.vnscript) adds to the game. The player starts in the developer
// tools, Play starts the game, F10 switches between the two (the story waits meanwhile), and where the player would
// close (Quit, the story's end without a title screen) it comes back to them. They are the engine's own, the same in
// every game, and never in a finished one (a pack). Without a game, the player is just them. Their pages are the
// visual editor: the game's scenes, characters and lines as forms, with a map of the scenes (Story); its screens
// (Screens); and what the scripts list by name, the game's files, its translations and its settings (Library); all
// are written to the files as they change.
//
// This file: opening and closing them, each frame of them, their caches, the scripts' mistakes and missing files, and
// going to a line.
#include "DeveloperUi.hpp"

#include <cfloat>
#include <chrono>
#include <cmath>
#include <iterator>

namespace vn {

namespace {

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

} // namespace

void Developer::openDeveloper() {
    DeveloperTools& d = tools;
    d.open = true;
    d.stepping = false; // (Next line: it's done, or the tools were opened before it got there)
    d.dropPreviews();
    d.focus.clear();
    d.armed.clear();
    d.row.reset();
    d.visualType.reset();
    d.character.reset();
    d.namedVisual.reset();
    d.soundType.reset();
    d.namedSound.reset();
    if (engine.noGame()) {
        d.editor.clear();
        return;
    }
    const bool first = !d.editor.loaded();
    const std::string last = first || d.scene >= d.editor.scenes().size() ? "start" : d.editor.scenes()[d.scene].name;
    d.editor.load(engine.gameRoot());
    d.played = d.editor.version();
    if (first && d.editor.loaded()) d.page = Page::Story;
    // The Story page shows where the story is (its line in sight, a few lines under the top), or the
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
        if (d.row && d.scene < scenes.size()) { // (its row chosen: the form has Next line)
            const auto rows = d.editor.rows(scenes[d.scene]);
            auto row = std::ranges::find(rows, *d.row, &StoryEditor::Row::line);
            if (row == rows.end()) // (a line with no row of its own: the one it's inside, as the lines outline it)
                row = std::ranges::find_if(rows, [&](const StoryEditor::Row& r) { return *d.row > r.line && *d.row < r.end; });
            if (row != rows.end()) {
                d.row = row->line;
                d.linesScroll = std::max(0.0f, static_cast<float>(row - rows.begin() - 4) * layout::listItemH * d.k);
            }
        }
    } else {
        const auto found = std::ranges::find(scenes, last, &StoryEditor::Scene::name);
        d.scene = found != scenes.end() ? static_cast<std::size_t>(found - scenes.begin()) : 0;
    }
}

void Developer::closeDeveloper() {
    DeveloperTools& d = tools;
    d.open = false;
    d.pagePartDrag.reset();
    d.toolsMenu.group.reset(); // (a menu open: it shuts with them)
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
    d.status = d.failed ? "The scripts need fixing: their mistakes are marked in red in the pages."_tr() : std::string{};
}

std::string_view Developer::pageName(Page page) {
    switch (page) {
    case Page::Story: return "Story"_tr;
    case Page::Screens: return "Screens"_tr;
    case Page::Library: return "Library"_tr;
    case Page::About: return "About"_tr;
    }
    return {};
}

void Developer::reloaded() {
    DeveloperTools& d = tools;
    d.played = d.editor.version();
    const bool start = std::exchange(d.play, false);
    const std::string scene = std::exchange(d.playScene, {});
    const auto line = std::exchange(d.playLine, std::nullopt);
    if (engine.mode() != Engine::Mode::Error && (start || !scene.empty())) { // Play, Play scene, or Play from here
        d.open = false;
        engine.waiting_ = false;
        d.stepping = false;
        d.passing.reset(); // (a play from a line with Stop here stops at once, before it)
        if (scene.empty()) engine.play();
        else playFromLine(scene, line);
    } else if (d.open) {
        d.editor.load(engine.gameRoot()); // (the files may have changed outside the player)
        d.failed = engine.mode() == Engine::Mode::Error;
        if (d.exportIn > 0) return; // (exporting says how it went)
        if (d.failed) d.status = "The scripts need fixing: their mistakes are marked in red in the pages."_tr;
        else logInfo("{}", "Reloaded the scripts, visuals and sounds."_tr());
    }
}

// Called every frame: carries out what takes a moment, once the screen says it's happening.
void Developer::updateDeveloper() {
    DeveloperTools& d = tools;
    if (!engine.developerMode() && (d.open || engine.waiting_)) { // (developer mode turned off, then reloaded)
        d.open = false;
        if (std::exchange(engine.waiting_, false) && engine.mode() != Engine::Mode::Error) engine.play();
    }
    const bool exportNow = d.exportIn == 1; // (the game read again)
    if (d.exportIn > 0) --d.exportIn;
    if (exportNow) {
        if (!engine.script().ok()) {
            ask("Can't export the game"_tr(), "The scripts need fixing: their mistakes are marked in red in the pages."_tr(), {"OK"_tr()}, 0, SDL_MESSAGEBOX_ERROR, {});
        } else {
            startExport();
        }
    }
    if (d.exported.exchange(false)) { // (the export's thread done: how it went, in a window of its own, with the way to it)
        d.exporting.join();
        logInfo("{}", d.exportSaid);
        const bool some = !d.exportSaid.starts_with("Exported nothing."_tr());
        std::vector<std::string> buttons{"OK"_tr()};
        if (some) buttons.insert(buttons.begin(), "Show the folder"_tr());
        ask(some ? "Exported the game"_tr() : "Can't export the game"_tr(), d.exportSaid, std::move(buttons), some ? 1 : 0, some ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR,
            [this, some](std::size_t answer) {
                if (!some || answer != 0) return;
                const std::string folder = engine.finishedFolder();
                if (!SDL_OpenURL(fileUrl(folder).c_str())) {
                    tools.failed = true;
                    tools.status = "Can't show '{}': {}."_tr(folder, SDL_GetError());
                }
            });
    }
}

void Developer::drawDeveloper() {
    SDL_Renderer* r = engine.renderer();
    DeveloperTools& d = tools;
    d.shownActs = std::exchange(d.acts, {}); // (what the editing keys could act on as the last frame was drawn: their buttons are made before this one's)
    d.formatBar = std::exchange(d.formatBarNow, {}); // (the text format bar as the last frame drew it: kept while the keys are on it)
    if (!d.formatBar.keysBack.empty()) d.keyAt = d.formatBar.keysBack; // (one of its buttons pressed by them: on its box again)
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
    padMoves();
    const bool modal = modalFrame(); // (the tools' own question or file picker: it takes the input)
    if (!modal) screenKeysFrame();
    const std::string focusBefore = d.focus; // (a click into a box: the on-screen keyboard comes up, until a real key's pressed)
    const bool clickedNow = d.clicked;
    if (!d.popup.id.empty() && d.focus != d.popup.id && std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) { return d.isKey(key, ToolKey::Back); }) > 0)
        d.popup.id.clear();
    d.overPopup = !d.popup.id.empty() && pointInRect(d.mouseX, d.mouseY, d.popup.box);
    d.popupClick = d.overPopup && std::exchange(d.clicked, false);
    const bool wheelOnPopup = !d.popup.id.empty() && pointInRect(d.wheelX, d.wheelY, d.popup.box);
    d.popupWheel = wheelOnPopup ? std::exchange(d.wheel, 0.0f) : 0.0f;
    if (wheelOnPopup) d.wheelAcross = 0.0f;
    if (d.clicked && !pointInRect(d.mouseX, d.mouseY, d.popup.at)) d.popup.id.clear();
    toolsMenuFrame(); // (an open menu of the tools' own part: its clicks and its keys come first)
    keyMoves();
    if (d.editLayout) // (while the page parts are laid out, what's in them stays as it is: the keys that change it go nowhere, as their buttons are grey)
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            constexpr ToolKey kChanging[]{ToolKey::Undo, ToolKey::Redo, ToolKey::Cut, ToolKey::Copy, ToolKey::Paste, ToolKey::Duplicate, ToolKey::Delete, ToolKey::SelectAll, ToolKey::StopHere};
            return std::ranges::any_of(kChanging, [&](ToolKey which) { return d.isKey(key, which); });
        });

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
    // A ready-made screen a change brought, once nothing's typed in (Relationships, with a love interest), when the game
    // has none.
    if (editing && !d.bringLater.empty() && d.focus.empty()) {
        if (std::ranges::none_of(d.editor.screens(), [&](const StoryEditor::Screen& s) { return lower(s.name) == d.bringLater; })) addReadyMadeScreen(d.bringLater);
        d.bringLater.clear();
    }
    // The game's size changed in Settings (and its box was left): the screens' places and shares fit any
    // size; their text sizes and padding grow or shrink to fit it, as one step for undo with the change.
    if (editing && d.sizeFrom && !d.focus.starts_with("setting:game_size")) { // (its width's or its height's box)
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
    // (while a box is typed in and finding them takes a while, a long game's, once the typing stops a moment or the box is
    // let go of: the typing doesn't wait for them)
    if (editing && d.changeSeen != d.editor.version()) {
        d.changeSeen = d.editor.version();
        d.changedAt = engine.toolsTime();
    }
    d.editor.filesGeneration = d.filesGeneration; // (what a visual is: a character, an image or a movie, as the files are now)
    const bool checkWaits = !d.focus.empty() && d.checkTook > 0.03f && engine.toolsTime() - d.changedAt < 0.4f;
    if (editing && d.checked != d.editor.version() && !checkWaits) {
        const auto checkBegan = std::chrono::steady_clock::now(); // (the real clock: how long it takes)
        d.checked = d.editor.version();
        d.dropPreviews(); // (it plays the scripts read before)
        auto before = std::move(d.script);
        d.script = std::make_unique<Script>(Script::load("scripts/main.vnscript", [this](const std::string& p) { return readTextFile(engine.gameRoot() + p); }, defaultUi(),
                                                         [this](const std::string& folder) { return listFiles(engine.gameRoot() + folder, ""); }));
        if (before) // (the texts changed keep their translations; the Library shows them as they are now)
            for (const std::string& id : keepTranslations(engine.gameRoot(), *before, *d.script))
                if (id == d.translation.id) d.translation.lines = linesOf(*readTextFile(engine.gameRoot() + "translations/" + id + ".vnscript"));
        d.allProblems = d.script->errors;
        d.runLines.clear(); // (the lines Stop here is for: with a command of their own)
        for (const Instruction& ins : d.script->code)
            if (!ins.tail && ins.line > 0 && ins.file < d.script->files.size()) d.runLines.emplace(d.script->files[ins.file], static_cast<std::size_t>(ins.line - 1));
        followStops();
        engine.assets().setAtlases(d.script->atlases); // (the previews and lists show them as they are now)
        engine.assets().setSubImages(d.script->subImages);
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
        std::ranges::move(missingFiles(*d.script), std::back_inserter(d.allProblems));
        d.showProblems();
        d.checkTook = std::chrono::duration<float>(std::chrono::steady_clock::now() - checkBegan).count();
    }
    // A fresh row left (another row, scene or page chosen; in the Library, another of its list): its own mistakes show now.
    if (const auto& fresh = d.freshRow;
        fresh && (d.page != fresh->page || (fresh->page == DeveloperTools::Page::Library ? d.library != fresh->library || d.libraryItem != fresh->item
                                                                                         : d.scene != fresh->scene || d.row != fresh->from))) {
        d.freshRow.reset();
        d.showProblems();
    }

    // The tools' own keys as their controls have them now, and the tools' own part's button pressed last frame: done
    // first, as the tools are drawn.
    readToolKeys();
    if (const auto pressed = std::exchange(d.pressedTool, std::nullopt)) runCommand(*pressed);
    const std::vector<ToolGroup> buttons = toolButtons();

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
        logInfo("{}", "There's no game next to VNext yet: make a New game, or put a game folder or game.vnext there and Look again."_tr());
    }

    // The tools' own part, at the window's foot in every page (as wide as the window, as tall as its buttons: it's
    // neither moved nor resized), and the room over it for the page, to the window's edges: its page parts (drawPageParts).
    const SDL_FPoint chrome = pagePartChrome(k);
    const float toolsH = std::min(h, chrome.y + placeToolButtons(ui, buttons, 0.0f, 0.0f, std::max(0.0f, w - chrome.x)).h);
    const SDL_FRect toolsAt{0.0f, h - toolsH, w, toolsH};
    d.room = {0.0f, 0.0f, w, toolsAt.y}; // (the page parts end where the tools' own part starts)
    d.wantPointer.reset();
    const SDL_FRect area = d.room;
    auto one = [&](std::string_view page, PagePart pagePart) {
        std::array pageParts{std::move(pagePart)};
        drawPageParts(ui, page, pageParts);
    };
    if (d.page == Page::About) {
        one("about", {.id = "about", .heading = "About"_tr, .at = area, .draw = [&](const SDL_FRect& box) { return drawAbout(ui, box), false; }, .least = {aboutLeastW(ui), 0.0f}});
    } else if (!editing) { // (no game yet: making one, or finding it)
        one("no_game", {.id = "no_game",
                        .heading = "No game"_tr,
                        .at = area,
                        .draw = [&](const SDL_FRect& box) { return noGamePage(ui, box, true), false; },
                        .tallAt = [&](float width) { return noGamePage(ui, {0.0f, 0.0f, width, 0.0f}, false); }});
    } else if (d.page == Page::Story) drawStory(ui, area);
    else if (d.page == Page::Screens) drawScreens(ui, area);
    else drawLibrary(ui, area);
    d.keyGroup = "tools"; // (its buttons: the last stops)
    d.keyOrder = {FLT_MAX, FLT_MAX};
    d.keyCount = 0;
    const ToolsPlaced placed = drawToolsPart(ui, buttons, toolsAt);
    showPointer(d.wantPointer);
    drawPopup(ui);
    drawToolsMenu(ui, buttons, placed); // (over the pages: its menu opens upward from the tools' own part)
    // A box handed the typing by a press (on it, on + New...), or pressed again: the on-screen keyboard comes up for a click
    // and for a controller's A (unless its list opened: A picks from that), each time (a real keyboard's key puts it away).
    if (!d.screenKeys.open && !d.focus.empty() && (d.focus != focusBefore || d.boxPressed) && (clickedNow || (d.padPressed && d.popup.id != d.focus)))
        openScreenKeys(d.padPressed);
    d.padPressed = d.boxPressed = false;
    drawScreenKeys(ui);
    drawTip(ui);
    keyRing(ui);
    drawModal(ui);

    // Keys nothing else took: the tools' own that act on them as a whole (undo and redo, the pages, Reload the game, Quit; the
    // others are the keys of what they act on, as their controls have them), and Esc, back to the game.
    std::vector<int> commands;
    for (const SDL_KeyboardEvent& key : d.keys) {
        const bool undo = d.isKey(key, ToolKey::Undo), redo = d.isKey(key, ToolKey::Redo);
        if (d.isKey(key, ToolKey::Back) && key.which != kToolButton && !engine.noGame() && !engine.waiting_) { // (not a controller's B: it only lets go)
            closeDeveloper();
        } else if ((undo || redo) && editing && d.page != Page::About && !(d.page == Page::Library && d.library == "translations")) {
            d.focus.clear();
            static_cast<void>(redo ? d.editor.redo() : d.editor.undo());
        } else if (d.isKey(key, ToolKey::Story)) {
            commands.push_back(kStoryPage);
        } else if (d.isKey(key, ToolKey::Screens)) {
            commands.push_back(kScreensPage);
        } else if (d.isKey(key, ToolKey::Library)) {
            commands.push_back(kLibraryPage);
        } else if (const bool back = d.isKey(key, ToolKey::PreviousPage); (back || d.isKey(key, ToolKey::NextPage)) && !engine.noGame()) {
            const auto at = std::ranges::find(kPages, d.page, &std::pair<ToolCommand, Page>::second); // (About: from the Story page's place)
            const std::size_t i = at == kPages.end() ? 0 : static_cast<std::size_t>(at - kPages.begin()), n = kPages.size();
            commands.push_back(kPages[(i + (back ? n - 1 : 1)) % n].first);
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
    d.wheel = d.wheelAcross = 0.0f;
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
    auto noImage = [](const std::string& name) { return "There's no visual called '{}'."_tr(name); };
    for (const Instruction& ins : script.code) {
        if (const auto* background = std::get_if<cmd::Background>(&ins.command)) { // (an image, or a movie: it plays)
            if (!image(background->image) && !movie(background->image)) at(ins.file, ins.line, noImage(background->image));
        } else if (const auto* show = std::get_if<cmd::Show>(&ins.command)) {
            if (const CharacterDef* def = show->character.empty() ? nullptr : script.character(show->character)) {
                const std::string key = def->imageOf(show->expression);
                if (!image(key) && !(!show->expression.empty() && image(def->image))) at(ins.file, ins.line, noImage(key)); // (else its usual image shows)
            } else if (!show->image.starts_with('#') && !image(show->image) && !movie(show->image)) { // (a colour's box: no picture)
                at(ins.file, ins.line, noImage(show->image));
            }
        } else if (const auto* play = std::get_if<cmd::Play>(&ins.command)) { // (a named sound: what it plays)
            if (const std::string file = script.soundOf(play->name, play->type).file; !play->stop && !sound(file)) at(ins.file, ins.line, "There's no sound called '{}'."_tr(file));
        } else if (const auto* say = std::get_if<cmd::Say>(&ins.command)) {
            if (const std::string file = script.soundOf(say->voice, "voice").file; !sound(file)) at(ins.file, ins.line, "There's no sound called '{}'."_tr(file));
        } else if (const auto* weather = std::get_if<cmd::Weather>(&ins.command)) {
            if (!image(weather->image)) at(ins.file, ins.line, noImage(weather->image));
        }
    }
    for (const auto& [name, def] : script.screens)
        for (const Element& el : def.elements) { // (a colour or a movie isn't an image; a visual named by a visual line or a character: what it shows)
            const std::string shows = el.kind == ElementKind::Image ? script.visualOf(el.text, el.option("expression")) : std::string{};
            if (el.kind == ElementKind::Image && !image(shows) && !movie(shows) && !parseColour(shows, &script.palette)) at(el.file, el.line, noImage(shows));
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
            if (const std::string file = def->typingSound.empty() ? std::string{} : script.soundOf(def->typingSound).file; !sound(file)) // (a named sound: what it plays)
                listed(c.file, c.line, "There's no sound called '{}'."_tr(file));
            if (!image(def->face)) listed(c.file, c.line, noImage(def->face));
        }
    for (const auto& def : editor.definitions("gallery")) // (a visual named by a visual line or a character: what it shows)
        if (const std::string shows = script.visualOf(def.name); !image(shows) && !movie(shows)) listed(def.file, def.line, noImage(shows));
    for (const auto& def : editor.definitions("song")) // (a named sound: what it plays)
        if (!sound(script.soundOf(def.name, "music").file)) listed(def.file, def.line, "There's no song called '{}'."_tr(def.name));
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

// Opens where a line of the scripts is (`line` counting from 0): its scene's line in the Story page,
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
            d.page = Page::Story;
            d.scene = i;
            d.visualType.reset();
            d.character.reset();
            d.namedVisual.reset();
            d.soundType.reset();
            d.namedSound.reset();
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
            d.page = Page::Screens;
            d.screenName = s.name;
            d.picking = false;
            d.part = line > s.line ? std::optional(line) : std::nullopt;
            d.moreParts.clear();
            d.asText = false;
            return true;
        }
    if (*file == 0 && editor.isSetting(line)) {
        d.page = Page::Library;
        d.library = "settings";
        return true;
    }
    // A character's line: the character, under character in the Story page's Visual types.
    const auto characters = editor.characters();
    for (std::size_t i = 0; i < characters.size(); ++i)
        if (characters[i].file == *file && characters[i].line == line) {
            d.page = Page::Story;
            d.row.reset();
            d.moreRows.clear();
            d.visualType.reset();
            d.namedVisual.reset();
            d.soundType.reset();
            d.namedSound.reset();
            d.character = i;
            d.asText = false;
            d.formScroll = 0.0f;
            return true;
        }
    for (const DefinitionKind& kind : kDefinitionKinds) {
        const auto defs = editor.definitions(kind.kind);
        for (std::size_t i = 0; i < defs.size(); ++i)
            if (defs[i].file == *file && defs[i].line <= line && line < defs[i].end) {
                if (kind.kind == "type" || kind.kind == "visual") { // (the Story page's Visual types: a type after every game's own, a visual under its type)
                    d.page = Page::Story;
                    d.row.reset();
                    d.moreRows.clear();
                    d.character.reset();
                    d.visualType.reset();
                    d.namedVisual.reset();
                    d.soundType.reset();
                    d.namedSound.reset();
                    (kind.kind == "type" ? d.visualType : d.namedVisual) = kind.kind == "type" ? kVisualTypes.size() + i : i;
                    d.asText = false;
                    d.formScroll = 0.0f;
                    return true;
                }
                if (kind.kind == "sound type" || kind.kind == "sound") { // (the Story page's Sound types: a type after every game's own, a sound under its type)
                    d.page = Page::Story;
                    d.row.reset();
                    d.moreRows.clear();
                    d.character.reset();
                    d.visualType.reset();
                    d.namedVisual.reset();
                    d.soundType.reset();
                    d.namedSound.reset();
                    (kind.kind == "sound type" ? d.soundType : d.namedSound) = kind.kind == "sound type" ? kBuiltInSoundTypes.size() + i : i;
                    d.asText = false;
                    d.formScroll = 0.0f;
                    return true;
                }
                if (kind.kind == "place") return false; // (not listed)
                if (kind.kind == "gallery" || kind.kind == "song") { // (its image, sub-image, movie or song, with its title)
                    const std::string name = lower(defs[i].name);
                    d.page = Page::Library;
                    d.library = kind.kind == "song" ? "sounds" : "images"; // (a movie's among the images)
                    d.libraryItem.reset();
                    d.librarySub.reset();
                    d.libraryAtlas.reset();
                    d.libraryVoice.reset();
                    d.libraryFind = name;
                    if (d.library == "images") {
                        const auto subs = editor.definitions("sub-image");
                        for (std::size_t j = 0; j < subs.size(); ++j)
                            if (lower(subs[j].name) == name) d.librarySub = j;
                    }
                    d.formScroll = 0.0f;
                    return true;
                }
                d.page = Page::Library;
                d.library = kind.kind;
                d.libraryItem = i;
                d.librarySub.reset();
                d.libraryAtlas.reset();
                d.libraryVoice.reset();
                d.pickingAnimation = false; // (the definition, not + New animation's list)
                if (kind.kind == "atlas" || kind.kind == "sub-image" || kind.kind == "font") { // (under the file it's made from)
                    d.library = kind.kind == "font" ? "fontfiles" : "images";
                    d.libraryItem.reset();
                    (kind.kind == "atlas" ? d.libraryAtlas : d.librarySub) = i;
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
            d.page = Page::Library;
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

namespace dev {

std::optional<std::size_t> sceneOf(const StoryEditor& editor, const ScriptError& e) {
    const auto line = static_cast<std::size_t>(std::max(e.line - 1, 0));
    const auto& scenes = editor.scenes();
    for (std::size_t i = 0; i < scenes.size(); ++i)
        if (editor.path(scenes[i].file) == e.file && scenes[i].line < line && line < scenes[i].end) return i;
    return std::nullopt;
}

bool inSettings(const StoryEditor& editor, const ScriptError& e) {
    return e.line > 0 && e.file == "scripts/main.vnscript" && editor.isSetting(static_cast<std::size_t>(e.line - 1));
}

} // namespace dev

// What the developer tools keep, looked up and kept up to date (Developer.hpp has them).

std::optional<SDL_Color> DeveloperTools::characterColour(const Palette& palette, std::string_view name) {
    if (colourVersion != editor.version()) { // (found again after a change, not every frame)
        colourVersion = editor.version();
        characterColours.clear();
        for (const StoryEditor::Character& c : editor.characters()) characterColours[lower(c.name)] = parseColour(fieldValue(editor.fields(c), "Colour"), &palette);
    }
    const auto it = characterColours.find(lower(name));
    return it != characterColours.end() ? it->second : std::nullopt;
}

const std::string& DeveloperTools::cachedSound(const Assets& assets, const std::string& name) {
    if (soundGeneration != filesGeneration) {
        soundGeneration = filesGeneration;
        soundPaths.clear();
        foundSound.clear();
    }
    const auto it = soundPaths.find(name);
    return it != soundPaths.end() ? it->second : soundPaths.emplace(name, assets.sound(name)).first->second;
}

std::optional<std::string> DeveloperTools::cachedFoundSound(const Assets& assets, const std::string& name) {
    static_cast<void>(cachedSound(assets, name)); // (the generation)
    const auto it = foundSound.find(name);
    return it != foundSound.end() ? it->second : foundSound.emplace(name, assets.findSound(name)).first->second;
}

Image Developer::pictureOf(const std::string& name) {
    if (engine.assets().hasImage(name)) return engine.assets().image(name);
    const auto file = engine.assets().findMovie(name);
    return file ? movieThumbnail(*file) : Image{};
}

Image Developer::movieThumbnail(const std::string& file) {
    DeveloperTools& d = tools;
    auto& t = d.movieThumbnails[file];
    if (t.generation != d.filesGeneration) {
        if (d.thumbnailMadeAt == engine.frameNumber()) return {}; // (one a frame: the rest come in the next ones)
        d.thumbnailMadeAt = engine.frameNumber();
        t = {.texture = nullptr, .w = 0.0f, .h = 0.0f, .generation = d.filesGeneration};
        SDL_Renderer* r = engine.renderer();
        Movie movie(r, d.silence, file, false, 1.0f); // (silent: the tools' own sound, never started)
        movie.showFirst();
        if (SDL_Texture* image = movie ? movie.texture() : nullptr) { // (drawn small into one of its own: the movie goes)
            float w = 0.0f, h = 0.0f;
            SDL_GetTextureSize(image, &w, &h);
            const float s = std::min(1.0f, 256.0f / std::max({w, h, 1.0f}));
            t.w = std::max(1.0f, std::round(w * s));
            t.h = std::max(1.0f, std::round(h * s));
            t.texture.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, static_cast<int>(t.w), static_cast<int>(t.h)));
            if (t.texture) {
                SDL_Texture* was = SDL_GetRenderTarget(r);
                SDL_SetRenderTarget(r, t.texture.get());
                SDL_RenderTexture(r, image, nullptr, nullptr);
                SDL_SetRenderTarget(r, was);
            }
        }
    }
    return t.texture ? Image{.texture = t.texture.get(), .src = {0.0f, 0.0f, t.w, t.h}} : Image{};
}

void DeveloperTools::showProblems() {
    problems.clear();
    for (const ScriptError& e : allProblems)
        if (!freshRow || e.file != freshRow->path || e.line <= static_cast<int>(freshRow->from) || e.line > static_cast<int>(freshRow->end)) problems.push_back(e);
    wrongScenesVersion = SIZE_MAX; // (the scenes' marks again)
}

const ScriptError* DeveloperTools::problemAt(std::size_t file, std::size_t line) const {
    const std::string& path = editor.path(file);
    const auto it = std::ranges::find_if(problems, [&](const ScriptError& e) { return e.file == path && e.line == static_cast<int>(line) + 1; });
    return it != problems.end() ? &*it : nullptr;
}

const std::vector<std::string>& DeveloperTools::cachedFiles(const std::string& folder, std::string_view ext) {
    auto& [generation, names] = fileLists[folder + "|" + std::string(ext)];
    if (generation != filesGeneration) {
        names = listFiles(folder, ext);
        generation = filesGeneration;
    }
    return names;
}

bool DeveloperTools::cachedExists(const std::string& path) {
    if (filesThereGeneration != filesGeneration) { // (the files are looked at again)
        filesThereGeneration = filesGeneration;
        filesThere.clear();
    }
    if (const auto known = filesThere.find(path); known != filesThere.end()) return known->second;
    return filesThere.emplace(path, fileExists(path)).first->second;
}

bool DeveloperTools::problemWithin(std::size_t file, std::size_t from, std::size_t end) const {
    const std::string& path = editor.path(file);
    return std::ranges::any_of(problems, [&](const ScriptError& e) { return e.file == path && e.line > static_cast<int>(from) && e.line <= static_cast<int>(end); });
}

const std::vector<bool>& DeveloperTools::wrongScenes() {
    if (wrongScenesVersion != editor.version()) {
        wrongScenesVersion = editor.version();
        scenesWrong.assign(editor.scenes().size(), false);
        for (const ScriptError& e : problems)
            if (const auto i = sceneOf(editor, e)) scenesWrong[*i] = true;
    }
    return scenesWrong;
}

DeveloperTools::Edited DeveloperTools::edited(const std::string& id, StoryEditor::Kind kind, const std::vector<StoryEditor::Field>& fields, bool wasTyped,
                                                const std::string& name, const std::string& value) {
    if (wasTyped) {
        if (const std::string owner = id + ":" + name; focusOwner != owner) {
            focusOwner = owner;
            focusKind = kind;
            focusFields = fields;
        }
        if (const auto f = std::ranges::find(focusFields, name, &StoryEditor::Field::name); f != focusFields.end()) f->value = value;
        return {focusFields, std::format("{}:{}{}", id, name, focusCount), focusKind};
    }
    Edited out{fields, {}, kind};
    if (const auto f = std::ranges::find(out.fields, name, &StoryEditor::Field::name); f != out.fields.end()) f->value = value;
    focus.clear();
    return out;
}

bool DeveloperTools::isKey(const SDL_KeyboardEvent& key, ToolKey which, bool repeating, bool anyShift) const {
    if (key.which == kToolButton) return key.raw == static_cast<Uint16>(which); // (its button's: it's that, whatever its keys)
    SDL_Event e{};
    e.key = key;
    if (repeating) e.key.repeat = false;
    if (anyShift) e.key.mod = static_cast<SDL_Keymod>(e.key.mod & ~SDL_KMOD_SHIFT);
    return std::ranges::any_of(toolKeys[static_cast<std::size_t>(which)], [&](const KeyChord& chord) { return chord.matches(e); });
}

void DeveloperTools::saidRenamed(bool ok, std::string_view mistake) {
    if (!ok) {
        failed = true;
        status = std::string(mistake);
    } else if (status == mistake) {
        status.clear();
    }
}

} // namespace vn
