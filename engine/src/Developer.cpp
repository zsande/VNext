// What developer mode (config developer yes, in main.vnscript) adds to the game. The player starts in
// the developer tools, Play starts the game, F10 switches between the two (the story waits
// meanwhile), and where the player would close (Quit, the story's end without a title screen) it
// comes back to them. They are the engine's own, the same in every game, and never in a finished one
// (a pack). Without a game, the player is just them. Their tabs are the visual editor: the game's
// scenes, characters and lines as forms (Story), a map of the scenes, the screens, what the scripts
// list by name and the game's files (Library), the config lines (Settings) and the translations; all
// are written to the files as they change.
//
// This file: opening and closing them, their events and tool buttons, the bar and the tab shown, the
// files they add, and going to a line. Each tab has a file of its own (Developer*.cpp).
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

namespace vn {

namespace {

enum Tool : int { kPlay, kNextLine, kPlayScene, kReload, kPack, kFolder, kNewGame, kLookAgain, kQuit };
struct ToolButton {
    Tool tool;
    ToolsText label;
};

const std::array kGameTools{ToolButton{kPlay, "Play"}, ToolButton{kPlayScene, "Play this scene"}, ToolButton{kReload, "Reload"},
                                ToolButton{kPack, "Pack the game"}, ToolButton{kFolder, "Show the folder"}, ToolButton{kQuit, "Quit"}};

const std::array kNoGameTools{ToolButton{kNewGame, "New game"}, ToolButton{kLookAgain, "Look again"},
                                  ToolButton{kFolder, "Show the folder"}, ToolButton{kQuit, "Quit"}};

// What New game writes: game/scripts/main.vnscript.
constexpr std::string_view kNewGameScript = R"vnext(# The game starts here: its settings, then label start, where the story begins.
config developer yes

label start
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
        d.view = View::Log;
        return;
    }
    const bool first = !d.editor.loaded();
    const std::string last = first || d.scene >= d.editor.scenes().size() ? "start" : d.editor.scenes()[d.scene].name;
    d.editor.load(engine.gameRoot());
    d.played = d.editor.version();
    if (!d.editor.loaded()) {
        if (d.view == View::Story || d.view == View::Map || d.view == View::Screens || d.view == View::Settings) d.view = View::Log;
    } else if (first || (d.view == View::Settings && !d.editor.hasMain())) {
        d.view = View::Story;
    }
    // The Story tab shows where the story is (its line in sight, a few lines under the top), or the
    // scene it showed last.
    const auto& scenes = d.editor.scenes();
    d.rowsScroll = 0.0f;
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
                d.rowsScroll = std::max(0.0f, static_cast<float>(row - rows.begin() - 4) * layout::listItemH * d.k);
        }
    } else {
        const auto found = std::ranges::find(scenes, last, &StoryEditor::Scene::name);
        d.scene = found != scenes.end() ? static_cast<std::size_t>(found - scenes.begin()) : 0;
    }
}

void Developer::closeDeveloper() {
    DeveloperTools& d = tools;
    d.open = false;
    engine.audio().stopListening();
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
    d.status = d.failed ? "The scripts need fixing: the Story tab and the log say where."_tr() : std::string{};
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

void Developer::useTool(int tool) {
    DeveloperTools& d = tools;
    d.failed = false;
    d.status.clear(); // (each tool says how it went, if it needs to)
    d.focus.clear();
    switch (tool) {
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
        if (d.scene < d.editor.scenes().size()) {
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
        d.status = "Reloading..."_tr;
        break;
    case kPack:
        d.packIn = 2; // after the next frame, which says what's happening
        d.status = "Packing the game..."_tr;
        engine.reloadRequested_ = true; // (first the game is read again, so its scripts are checked as they are now)
        break;
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

// Called every frame: carries out what takes a moment, once the screen says it's happening.
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
        if (d.packIn == 0) d.status = d.failed ? "The scripts need fixing: the Story tab and the log say where."_tr : "Reloaded the scripts, images and audio."_tr;
    }
}

ScreenView* Developer::previewScreen(std::string_view name, const Script& from) {
    auto& made = tools.previewViews; // (until the editor's scripts change)
    if (const auto it = made.find(name); it != made.end()) return it->second.get();
    const ScreenDef* def = from.screen(name);
    return def ? made.emplace(std::string(name), std::make_unique<ScreenView>(*def, engine, true)).first->second.get() : nullptr;
}

void Developer::updateDeveloper() {
    DeveloperTools& d = tools;
    if (!engine.developerMode() && (d.open || engine.waiting_)) { // (developer mode turned off, then reloaded)
        d.open = false;
        if (std::exchange(engine.waiting_, false) && engine.mode() != Engine::Mode::Error) engine.play();
    }
    const bool packNow = d.packIn == 1; // (the frame after "Packing..." was drawn)
    if (d.packIn > 0) --d.packIn;
    if (packNow) {
        const auto packed = engine.finishGame();
        const std::string& said = packed ? *packed : packed.error(); // (a sentence or two a line)
        d.failed = !packed;
        d.status = said;
        std::ranges::replace(d.status, '\n', ' '); // (the log has it on one line)
        // Said in a window of its own too (the log isn't always in view): its title says how it went, the
        // rest a line apart, and after a pack there's the way to it.
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
            if (!SDL_OpenURL(fileUrl(folder).c_str())) d.status += " Can't show '{}': {}."_tr(folder, SDL_GetError());
        }
    }
}

void Developer::listenButton(DeveloperUi& ui, const SDL_FRect& at, const std::string& path, bool small) {
    const bool playing = engine.audio().listening() == path;
    tools.listenShown = tools.listenShown || playing;
    if (ui.button(at, small ? std::string_view{} : std::string_view(playing ? "Stop"_tr : "Play"_tr), playing, true, small ? layout::smallButtonText : layout::buttonText)) {
        if (playing) {
            engine.audio().stopListening();
        } else {
            engine.audio().listen(path);
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
    const float k = ui.k, boxH = layout::boxH * k, labelW = std::round(w * layout::labelPart);
    ui.textAround("As the script has it"_tr, x, y + boxH / 2, layout::labelText, theme::dim, labelW - 8 * k);
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
    const float margin = layout::margin * k;
    d.k = k;
    d.size = {w, h};
    DeveloperUi ui(engine, r, engine.toolsFonts_, d, k);
    if (!std::exchange(d.listenShown, false)) engine.audio().stopListening(); // (its button is gone)
    if (d.moviePlaying && engine.audio().listening() != d.movieOf) d.stopMovie();   // (stopped, or something else plays)
    draw::rect(r, {0, 0, w, h}, kPanel); // (opaque: the game under the tools is drawn at its own size, and would show through)
    // A list open under a field takes the clicks and the wheel over it (it's drawn last, over the
    // rest); a click anywhere else but its field closes it.
    d.popup.shown = false;
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
            d.failed = false;
            d.status = "The scripts changed in another program, so the editor read them again."_tr;
        }
    }
    // The game's size changed in Settings (and its box was left): the screens' places and shares fit any
    // size; their text sizes, padding and bars grow or shrink to fit it, as one step for undo with the change.
    if (editing && d.sizeFrom && d.focus != "setting:resolution") {
        auto sizeOf = [](std::string_view v) -> std::optional<SDL_FPoint> {
            const auto words = rawWords(v);
            const auto width = words.size() == 2 ? parseNumber(words[0]) : std::nullopt, height = words.size() == 2 ? parseNumber(words[1]) : std::nullopt;
            return width && height && *width >= 1.0f && *height >= 1.0f ? std::optional(SDL_FPoint{*width, *height}) : std::nullopt;
        };
        const auto from = sizeOf(*d.sizeFrom), to = sizeOf(d.editor.setting("resolution").value_or("1280 720"));
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
        if (before) // (the texts changed keep their translations; the Translate tab shows them as they are now)
            for (const std::string& id : keepTranslations(engine.gameRoot(), *before, *d.script))
                if (id == d.translation.id) d.translation.lines = linesOf(*readTextFile(engine.gameRoot() + "translations/" + id + ".vnscript"));
        d.problems = d.script->errors;
        d.runLines.clear(); // (the lines Stop here is for: with a command of their own)
        for (const Instruction& ins : d.script->code)
            if (!ins.tail && ins.line > 0 && ins.file < d.script->files.size()) d.runLines.emplace(d.script->files[ins.file], static_cast<std::size_t>(ins.line - 1));
        followStops();
        engine.assets().setSubPictures(d.script->subPictures); // (the previews and lists show them as they are now)
        // The game's size as Settings has it now (the screens and the preview are drawn at it; the game
        // takes it when it's played again).
        if (const GameConfig& c = d.script->config;
            c.width > 0 && c.height > 0 && (engine.canvas().w != static_cast<float>(c.width) || engine.canvas().h != static_cast<float>(c.height))) {
            engine.canvas_ = {static_cast<float>(c.width), static_cast<float>(c.height)};
            engine.updatePixelScale(); // (the game's view takes it; the tools' stays the window's)
        }
        // Whether the window can be resized, as Settings has it now: the tools' window is the game's.
        if (const bool resizable = d.script->config.resizable; resizable != ((SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE) != 0))
            SDL_SetWindowResizable(window, resizable);
        std::ranges::move(missingFiles(*d.script), std::back_inserter(d.problems));
    }

    // The bar: the tabs on the left, the tools on the right; in a window too narrow for both, the tools
    // go on a row of their own under the tabs (and what's still too wide is drawn narrower).
    std::vector<std::pair<View, std::string_view>> tabs;
    if (editing) {
        tabs = {{View::Story, "Story"_tr}, {View::Map, "Map"_tr}, {View::Screens, "Screens"_tr}, {View::Library, "Library"_tr}};
        if (d.editor.hasMain()) tabs.emplace_back(View::Settings, "Settings"_tr);
        tabs.emplace_back(View::Translate, "Translate"_tr);
    }
    tabs.emplace_back(View::Log, "Log"_tr);
    tabs.emplace_back(View::About, "About"_tr);
    const auto toolSet = engine.noGame() ? std::span<const ToolButton>(kNoGameTools) : std::span<const ToolButton>(kGameTools);
    std::vector<ToolButton> toolButtons(toolSet.begin(), toolSet.end());
    for (ToolButton& t : toolButtons) {
        if (t.tool == kPlay && !engine.waiting_) t.label = "Back to the game"_text;
        if (t.tool == kPlayScene && d.view == View::Story && d.row && d.moreRows.empty() && !d.character) t.label = "Play from this line"_text;
    }
    if (!engine.noGame() && storyAt()) toolButtons.insert(toolButtons.begin() + 1, ToolButton{kNextLine, "Next line"_text}); // (a story under the tools)
    const float pad = layout::tabPad * k, gap = layout::toolGap * k, between = 24 * k, room = w - 2 * margin;
    float tabsW = 0.0f, toolsW = 0.0f;
    for (const auto& tab : tabs) tabsW += ui.width(tab.second, layout::tabText) + pad;
    for (const ToolButton& t : toolButtons) toolsW += ui.width(tr(t.label), layout::toolText) + pad + gap;
    const bool twoRows = (room - between) / (tabsW + toolsW) < layout::barLeast;
    const float tabsFit = std::min(1.0f, twoRows ? room / tabsW : (room - between) / (tabsW + toolsW)), toolsFit = twoRows ? std::min(1.0f, room / toolsW) : tabsFit;
    auto shrunk = [](float size, float fit) { return fit < 1.0f ? std::max(10.0f, std::floor(size * fit)) : size; }; // (whole sizes: a font each)
    const float barH = (twoRows ? layout::tabsRowH : layout::barH) * k, toolsY = twoRows ? barH : 0.0f, barsH = twoRows ? barH + layout::toolsRowH * k : barH;
    draw::rect(r, {0, 0, w, barsH}, kBar);
    float x = margin;
    for (const auto& [view, label] : tabs) {
        const float bw = (ui.width(label, layout::tabText) + pad) * tabsFit;
        auto wrongIn = [&](const auto& items) { return std::ranges::any_of(items, [&](const auto& it) { return d.problemWithin(it.file, it.line, it.end); }); };
        const bool wrong = editing && (view == View::Screens ? wrongIn(d.editor.screens())
                                       : view == View::Library ? wrongIn(d.editor.definitions("variable")) ||
                                                                     std::ranges::any_of(kDefinitionKinds, [&](const DefinitionKind& kind) { return wrongIn(d.editor.definitions(kind.kind)); })
                                       : view == View::Story || view == View::Map ? std::ranges::contains(d.wrongScenes(), true)
                                                               : view == View::Settings && std::ranges::any_of(d.problems, [&](const ScriptError& e) { return inSettings(d.editor, e); }));
        if (ui.tab({x, 0, bw, barH}, label, d.view == view, wrong, shrunk(layout::tabText, tabsFit)) && d.view != view) {
            d.view = view;
            d.scroll = 0;
            d.focus.clear();
            if (d.trying) stopTrying(); // (a screen tried: no longer)
        }
        x += bw;
    }
    x = w - margin;
    const float toolH = (twoRows ? layout::toolsRowH - 6 : layout::barH - 16) * k; // (in the middle of its row)
    for (const ToolButton& t : toolButtons | std::views::reverse) {
        const float bw = (ui.width(tr(t.label), layout::toolText) + pad) * toolsFit;
        x -= bw;
        const bool enabled = t.tool != kPlayScene || (d.view == View::Story && !d.editor.scenes().empty());
        const float rowH = twoRows ? layout::toolsRowH * k : barH;  // (the tools' own row, or the tabs')
        if (ui.button({x, toolsY + (rowH - toolH) / 2, bw, toolH}, tr(t.label), t.tool == kPlay, enabled, shrunk(layout::toolText, toolsFit))) useTool(t.tool);
        x -= gap * toolsFit;
    }

    // What the last tool did goes to the log (the Log tab), once, as it changes.
    if (!d.editor.problem().empty()) {
        d.failed = true;
        d.status = d.editor.problem();
    }
    if (d.status != d.loggedStatus) {
        d.loggedStatus = d.status;
        if (d.failed && !d.status.empty()) logWarn("{}", d.status);
        else if (!d.status.empty()) logInfo("{}", d.status);
    }
    if (engine.noGame() && !d.saidNoGame) {
        d.saidNoGame = true;
        logInfo("{}", "There's no game yet: make a New game, or put a game folder or game.vnext next to the player and Look again."_tr());
    }

    const SDL_FRect area{margin, barsH + margin, w - 2 * margin, std::max(0.0f, h - barsH - 2 * margin)}; // (the same room on each side)
    if (d.view == View::Story && editing) drawStory(ui, area);
    else if (d.view == View::Map && editing) drawMap(ui, area);
    else if (d.view == View::Screens && editing) drawScreens(ui, area);
    else if (d.view == View::Library && editing) drawLibrary(ui, area);
    else if (d.view == View::Translate && editing) drawTranslations(ui, area);
    else if (d.view == View::Settings && editing && d.editor.hasMain()) drawSettings(ui, area);
    else if (d.view == View::About) { // (the tools' language over it)
        const float used = drawToolsLanguage(ui, area);
        drawLines(ui, {area.x, area.y + used, area.w, std::max(0.0f, area.h - used)});
    } else drawLines(ui, area);
    drawPopup(ui);

    // Keys nothing else took.
    for (const SDL_KeyboardEvent& key : d.keys) {
        const bool command = key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL);
        if (key.key == SDLK_ESCAPE && !engine.noGame() && !engine.waiting_) {
            closeDeveloper();
        } else if (key.key == SDLK_F6 && !key.repeat) {
            useTool(engine.noGame() ? kLookAgain : kReload);
        } else if (command && (key.key == SDLK_Z || key.key == SDLK_Y) && editing && d.view != View::Log && d.view != View::About && d.view != View::Translate) {
            d.focus.clear();
            const bool redo = key.key == SDLK_Y || (key.mod & SDL_KMOD_SHIFT); // (Cmd+Shift+Z, or Ctrl+Y)
            if (!(redo ? d.editor.redo() : d.editor.undo())) d.status = redo ? "Nothing to redo."_tr : "Nothing to undo."_tr;
        }
    }
    if (d.clicked) d.focus.clear(); // a click on nothing
    if (d.focus.empty()) d.focusOwner.clear();
    d.keys.clear();
    d.typed.clear();
    d.entered.clear();
    d.clicked = d.doubleClick = false;
    d.wheel = 0.0f;
}

// Lines that use a picture, sound or movie the game doesn't have (the player would show a stand-in
// or stay silent): as mistakes too, so they're marked like the scripts' own.
std::vector<ScriptError> Developer::missingFiles(const Script& script) const {
    const StoryEditor& editor = tools.editor;
    std::set<std::string, std::less<>> frames; // (atlases' pictures by name)
    for (const Atlas& a : script.atlases) {
        frames.insert(lower(a.name));
        for (const std::string& e : a.expressions) frames.insert(lower(a.name + "_" + e));
    }
    for (const SubPicture& p : script.subPictures) frames.insert(p.name);
    auto picture = [&](const std::string& name) {
        return name.empty() || name.contains('{') || parseColor(name, &script.palette) || frames.contains(lower(name)) || engine.assets().hasImageFile(name) ||
               engine.assets().hasImageFile(lower(name));
    };
    auto sound = [&](const std::string& name) { return name.empty() || name.contains('{') || engine.assets().findAudio(name) || engine.assets().findAudio(lower(name)); };
    auto movie = [&](const std::string& name) { return name.empty() || name.contains('{') || engine.assets().findMovie(name) || engine.assets().findMovie(lower(name)); };
    std::vector<ScriptError> out;
    auto at = [&](std::size_t file, int line, std::string message) {
        if (file < script.files.size() && script.files[file].starts_with("scripts/")) out.push_back({script.files[file], line, std::move(message)});
    };
    auto noPicture = [](const std::string& name) { return "There's no image called '{}'."_tr(name); };
    for (const Instruction& ins : script.code) {
        if (const auto* scene = std::get_if<cmd::Scene>(&ins.command)) {
            if (!picture(scene->image)) at(ins.file, ins.line, noPicture(scene->image));
        } else if (const auto* show = std::get_if<cmd::Show>(&ins.command)) {
            if (const CharacterDef* def = show->character.empty() ? nullptr : script.character(show->character)) {
                const std::string key = def->pictureOf(show->expression);
                if (!picture(key) && !(!show->expression.empty() && picture(def->image))) at(ins.file, ins.line, noPicture(key)); // (else its usual picture shows)
            } else if (!picture(show->image)) {
                at(ins.file, ins.line, noPicture(show->image));
            }
        } else if (const auto* play = std::get_if<cmd::Play>(&ins.command)) {
            if (!sound(play->file)) at(ins.file, ins.line, "There's no sound called '{}'."_tr(play->file));
        } else if (const auto* film = std::get_if<cmd::Movie>(&ins.command)) {
            if (!movie(film->file)) at(ins.file, ins.line, "There's no movie called '{}'."_tr(film->file));
        } else if (const auto* weather = std::get_if<cmd::Weather>(&ins.command)) {
            if (!picture(weather->image)) at(ins.file, ins.line, noPicture(weather->image));
        }
    }
    for (const auto& [name, def] : script.screens)
        for (const Element& el : def.elements) {
            if (el.kind == ElementKind::Image && !picture(el.text) && !parseColor(el.text, &script.palette)) at(el.file, el.line, noPicture(el.text)); // (a colour isn't a picture)
            if (el.kind == ElementKind::Movie && !movie(el.text)) at(el.file, el.line, "There's no movie called '{}'."_tr(el.text));
        }
    // What the scripts list by name, at their lines in the editor.
    auto listed = [&](std::size_t file, std::size_t line, std::string message) { out.push_back({editor.path(file), static_cast<int>(line) + 1, std::move(message)}); };
    for (const StoryEditor::Definition& def : editor.definitions("animation")) // (the picture a wipe has the shape of)
        if (const auto it = script.animations.find(lower(def.name)); it != script.animations.end())
            if (const auto shaped = std::ranges::find_if(it->second.changes, [&](const Animation::Change& c) { return !picture(c.shape); });
                shaped != it->second.changes.end())
                listed(def.file, def.line, noPicture(shaped->shape));
    for (const StoryEditor::Character& c : editor.characters())
        if (const CharacterDef* def = script.character(lower(c.name))) {
            if (!sound(def->blip)) listed(c.file, c.line, "There's no sound called '{}'."_tr(def->blip));
            if (!picture(def->face)) listed(c.file, c.line, noPicture(def->face));
        }
    for (const auto& def : editor.definitions("gallery"))
        if (!picture(def.name) && !movie(def.name)) listed(def.file, def.line, noPicture(def.name));
    for (const auto& def : editor.definitions("song"))
        if (!sound(def.name)) listed(def.file, def.line, "There's no song called '{}'."_tr(def.name));
    for (const auto& def : editor.definitions("picture"))
        if (const auto p = std::ranges::find(script.subPictures, lower(def.name), &SubPicture::name);
            p != script.subPictures.end() && !engine.assets().hasImageFile(p->image) && !engine.assets().hasImageFile(lower(p->image)))
            listed(def.file, def.line, noPicture(p->image));
    for (const auto& def : editor.definitions("atlas"))
        if (const auto a = std::ranges::find(script.atlases, lower(def.name), &Atlas::name); a != script.atlases.end())
            for (const std::string& image : {a->image, a->eyesPart ? a->eyesPart->image : std::string{}, a->mouthPart ? a->mouthPart->image : std::string{}})
                if (!image.empty() && !engine.assets().hasImageFile(image) && !engine.assets().hasImageFile(lower(image))) listed(def.file, def.line, noPicture(image));
    return out;
}

void Developer::chooseFiles(std::string_view label, std::span<const std::string_view> extensions, const std::string& as) {
    DeveloperTools& d = tools;
    {
        const std::scoped_lock _(d.imports.mutex);
        if (d.imports.open) { // (its filter's words stay as they are until it's answered)
            d.status = "A file dialog is open already."_tr;
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

// Files picked in the Library's import dialog, or dropped on the window: copied into the game folder
// (pictures into images/, sounds into audio/, movies into movies/, fonts into fonts/), under a name of
// their own if one's there already; or, picked for a place of its own (a translation's own picture, a
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
            if (stem.empty()) stem = folder == "images/" ? "image" : folder == "audio/" ? "audio" : folder == "movies/" ? "movie" : "font";
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
                d.library = folder == "images/" ? "pictures" : folder == "audio/" ? "sounds" : folder == "movies/" ? "movies" : "fontfiles";
                d.libraryItem.reset();
                d.librarySub.reset();
            }
        }
    }
    d.editor.movedFiles(std::move(moves));
    d.failed = copied == 0;
    d.status = copied == 1 ? "Imported '{}'."_tr(last) : copied > 1 ? "Imported {} files."_tr(copied)
             : !skipped.empty() ? "'{}' isn't an image, audio, movie or font file the game can use there."_tr(skipped)
                                : "Can't copy that into the game folder."_tr;
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
                    if (el.line == static_cast<int>(*d.part) + 1 && d.script->files[el.file] == d.editor.path(sc->file) && pointInRect(px, py, d.screenView->rectOf(d.screenView->indexOf(el)))) {
                        box = d.screenView->rectOf(d.screenView->indexOf(el));
                        into = d.editor.part(*sc, *d.part);
                    }
            const std::string line = std::format("image \"{}\" x={} y={}", firstImage, unitText(placeFor(px - box.x, 0.0f, box.w)), unitText(-placeFor(py - box.y, 0.0f, box.h)));
            d.part = d.editor.addPart(*sc, into ? &*into : nullptr, line);
            d.moreParts.clear();
            d.status = "Imported '{}', and put it on the screen."_tr(last);
            placed = true;
        }
    if (copied > 0 && !placed) d.view = DeveloperTools::View::Library;
}

// Opens where a line of the scripts is (`line` counting from 0): its scene's line in the Story tab,
// its screen's part in Screens, its setting, its character, or what it lists in the Library. False
// when it's none of those.
bool Developer::goTo(const std::string& path, std::size_t line) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    std::optional<std::size_t> file;
    for (std::size_t f = 0; f < editor.fileCount(); ++f)
        if (lower(editor.path(f)) == lower(path)) file = f;
    if (!file) return false;
    d.focus.clear();
    d.find.clear();
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
                d.rowsScroll = std::max(0.0f, static_cast<float>(row - rows.begin() - 4) * layout::listItemH * d.k); // (a few lines over it in sight)
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
        d.view = View::Settings;
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
                if (kind.kind == "position") return false; // (not listed)
                if (kind.kind == "gallery" || kind.kind == "song") { // (its image, sub-image, movie or song, with its title)
                    const std::string name = lower(defs[i].name);
                    d.view = View::Library;
                    d.library = kind.kind == "song" ? "sounds" : engine.assets().findMovie(name) ? "movies" : "pictures";
                    d.libraryItem.reset();
                    d.librarySub.reset();
                    d.libraryFind = name;
                    if (d.library == "pictures") {
                        const auto subs = editor.definitions("picture");
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
                if (kind.kind == "picture" || kind.kind == "font") { // (under the file it's made from)
                    d.library = kind.kind == "picture" ? "pictures" : "fontfiles";
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

// The developer tools' own folder (with --data, that one): it keeps the language chosen for them.
std::string Developer::toolsFolder() const {
    if (engine.dataFolder_) return engine.prefPath();
    const SdlOwned<char> pref{SDL_GetPrefPath("VNext", "Developer")};
    return pref ? std::string(pref.get()) : std::string{};
}

std::string Developer::toolsLanguagesFolder() const { return engine.gamePlaces().front() + "editor languages/"; }

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
    for (const ScriptError& e : errors) logWarn("editor languages/{}.vnscript line {}: {}", id, e.line, e.message);
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

// About's row: the developer tools' language (English, or a translation), and a file to translate
// them; under it, where translations are.
float Developer::drawToolsLanguage(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    const float k = ui.k, buttonH = layout::buttonH * k, gap = layout::gap * k;
    if (d.cachedFiles(toolsLanguagesFolder(), ".vnscript") != d.toolsLanguageFiles) findToolsLanguages(); // (one added or taken away)
    const std::string_view label = "The developer tools' language"_tr;
    float x = area.x;
    ui.textAround(label, x, area.y + buttonH / 2, layout::labelText, theme::dim);
    x += ui.width(label, layout::labelText) + 2 * gap;
    auto choose = [&](const std::string& id) {
        useToolsLanguage(id);
        const std::string file = toolsFolder() + "language.txt";
        if (!SDL_SaveFile(file.c_str(), id.data(), id.size())) logWarn("{}", "Can't save the developer tools' language: {}."_tr(SDL_GetError()));
    };
    const std::string_view make = "Make a file to translate them"_tr;
    const float makeW = ui.buttonWidth(make, layout::buttonText, layout::widerButtonW);
    const float room = area.x + area.w - makeW - gap;
    auto languageButton = [&](std::string_view name, const std::string& id) {
        const float w = ui.buttonWidth(name, layout::buttonText, layout::toggleW);
        if (x + w > room) return; // (as many as fit)
        if (ui.button({x, area.y, w, buttonH}, name, d.toolsLanguage == id, true, layout::buttonText) && d.toolsLanguage != id) choose(id);
        x += w + gap;
    };
    languageButton("English", {});
    for (const DeveloperTools::ToolsLanguage& l : d.toolsLanguages) languageButton(l.name, l.id);
    if (ui.button({area.x + area.w - makeW, area.y, makeW, buttonH}, make, false, true, layout::buttonText)) writeToolsTranslation();
    const float noteY = area.y + buttonH + gap;
    const float noteH = ui.text("A translation of the developer tools is a file in \"editor languages\", next to VNext, with each text they show and its translation under it. \"Make a file to translate them\" writes one with every text, ready to translate; with a translation in use, it adds the texts that one doesn't have yet."_tr,
                                area.x, noteY, layout::noteText, theme::dim, area.w).y;
    return noteY + noteH + gap - area.y;
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
                                 "# (spanish.vnscript), keep it here, next to VNext, and choose it in About.\n"
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
    d.status = existing ? "Added {} texts the developer tools show to 'editor languages/{}.vnscript'."_tr(added, id)
                        : "Made 'editor languages/{}.vnscript', with the {} texts the developer tools show, ready to translate."_tr(id, added);
    findToolsLanguages();
    if (!SDL_OpenURL(fileUrl(folder).c_str())) d.status += " " + std::string("Can't show '{}': {}."_tr(folder, SDL_GetError()));
}

} // namespace vn
