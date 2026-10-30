// The preview: the game drawn as it would be at a row of the Story tab, or with a screen of the
// Screens tab open, with its hooks run; and playing from a line.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

namespace vn {

// The bottom of the Story tab's right side: the screen as it will be at the chosen line (or the
// scene's start), the story played there from the scene's start.
void Developer::drawPreview(DeveloperUi& ui, const SDL_FRect& box, const StoryEditor::Scene& scene, const StoryEditor::Row* row) {
    DeveloperTools& d = tools;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    if (!d.previewOn) { // (off: its heading, with the button that turns it on)
        if (d.preview) d.dropPreviews();
        PreviewHead head(ui, box); // (all there is of it)
        if (head.button("Preview off"_tr, false)) d.previewOn = true;
        head.title("Preview"_tr);
        d.previewShown = {};
        return;
    }
    const auto of = std::tuple{d.editor.version(), d.scene, row ? row->line : scene.line};
    if (d.previewOf != of) {
        d.dropPreviews();
        d.previewOf = of;
        makePreview(scene, row);
    }
    const float scale = std::min(box.w / engine.canvas().w, box.h / engine.canvas().h);
    const SDL_FRect shown{box.x, box.y + box.h - engine.canvas().h * scale, engine.canvas().w * scale, engine.canvas().h * scale};
    d.previewShown = shown;
    // The chosen line shows someone (or moves the one there): dragging them on the preview puts them
    // there (their place and height), and the wheel zooms them.
    const bool placing = d.preview && row && !d.previewPlaying && row->kind == Kind::Character && lower(firstWord(row->text)) == "show";
    if (!placing) d.placeDrag.reset();
    // Its buttons, from the right: off, its sound (only while no game is going on behind the tools: it
    // would cut into that one's), and back to the line after it has played on; then its title, in the room left.
    PreviewHead head(ui, PreviewHead::over(shown, k));
    if (head.button("Preview on"_tr, true)) {
        d.previewOn = false;
        d.dropPreviews();
        return;
    }
    if (engine.waiting_ && head.button(d.previewSound ? "Audio on"_tr : "Audio off"_tr, d.previewSound)) {
        d.previewSound = !d.previewSound;
        d.dropPreviews(); // (made again, heard or not)
        return;
    }
    if (d.previewPlaying && head.button("Reset"_tr, false)) { // (back to the line)
        d.dropPreviews();
        return;
    }
    head.title(d.preview && !d.previewReached ? "Preview (not reached from the start)"_tr // (short, to fit beside the buttons)
               : placing                      ? "Preview: drag them; the wheel zooms"_tr
                                              : "Preview"_tr);
    draw::rect(r, shown, kBox);
    if (d.preview) {
        // It takes the mouse and the keys as the game does (previewEvent): its buttons, choices and
        // textbox work, and a click or a key goes on, as the game's controls say.
        const Uint64 now = SDL_GetTicks();
        const float dt = std::min(0.1f, static_cast<float>(now - d.previewTicks) / 1000.0f);
        if (d.previewPlaying) d.preview->update(dt);
        if (d.preview && d.script) recordReached(*d.preview, *d.script); // (a line run in the preview has run)
        else if (d.preview->playingMovie()) d.preview->playMovieRound(dt); // (a movie's line: it plays there)
        if (d.previewPlaying) { // the line it's at (lit in the list of lines)
            if (const std::size_t pc = d.preview->here(); pc < d.script->code.size() && d.script->code[pc].line >= 1) {
                const Instruction& ins = d.script->code[pc];
                d.previewAt = std::pair{d.script->files[ins.file], static_cast<std::size_t>(ins.line) - 1};
            }
        } else {
            d.previewAt.reset();
        }
        d.previewTicks = now;
        if (paintCanvas([&] { // drawn again every frame, as the game would draw it (pictures may still be loading), with the screens as they are now
                const SwappedIn previewGame(engine.game_, d.preview);
                const SetFor previewViews(engine.viewsFrom_, d.script.get());
                engine.drawGame(true);
                if (engine.game_->openingScreen() && !engine.game_->playingMovie()) // (the screen the line opens)
                    if (ScreenView* v = engine.view(engine.game_->request().name)) v->draw(engine.renderer());
                for (const std::string& name : d.previewOverlays) // (the screens its buttons opened)
                    if (ScreenView* v = engine.view(name)) v->draw(engine.renderer());
                auto under = [&]() -> ScreenView* { // (the screen on top of the preview: a button's, else the line's)
                    return !d.previewOverlays.empty() ? engine.view(d.previewOverlays.back()) : engine.game_->openingScreen() ? engine.view(engine.game_->request().name) : nullptr;
                };
                std::erase_if(d.previewLeaving, [&](const std::string& name) { // (the ones closed: drawn while their hide_animation= plays)
                    const ScreenView* v = engine.view(name);
                    if (v && v->leaving()) return false;
                    if (ScreenView* top = under(); top && ((v && v->endsCovered()) || top->hasLeft())) top->opened(); // (what was under comes back into view)
                    return true;
                });
                if (!d.previewPending.empty()) // (opening once the screen under it has faded out)
                    if (ScreenView* u = under(); !u || !u->leaving())
                        if (ScreenView* v = engine.view(d.previewPending)) {
                            v->reset();
                            v->playWith(d.previewPendingWith.first, d.previewPendingWith.second);
                            d.previewOverlays.push_back(std::exchange(d.previewPending, {}));
                        }
                for (const std::string& name : d.previewLeaving)
                    if (ScreenView* v = engine.view(name)) v->draw(engine.renderer());
            }))
            SDL_RenderTexture(r, d.canvas.get(), nullptr, &shown);
        // Where the mouse is on it, in the game's own pixels, and the mouse's events there.
        const float mx = (d.mouseX - shown.x) * engine.canvas().w / shown.w, my = (d.mouseY - shown.y) * engine.canvas().h / shown.h;
        auto mouseEvent = [&](Uint32 type) { return mouseEventAt(type, mx, my, d.doubleClick); };
        if (placing) {
            auto fields = d.editor.fields(*row);
            auto field = [&](std::string_view name) -> std::string& { return std::ranges::find(fields, name, &StoryEditor::Field::name)->value; };
            auto write = [&](const std::string& key) { d.editor.setText(scene, *row, d.editor.lineOf(row->kind, fields), key); };
            const std::string who = lower(!field("Called").empty() ? field("Called") : field("Who"));
            const auto objects = d.preview->onStage();
            const auto object = std::ranges::find_if(objects, [&](const Stage::ObjectState& o) { return lower(o.tag) == who || lower(o.character) == who; });
            const bool found = object != objects.end();
            if (const float turns = found ? ui.wheelOver(shown) : 0.0f; turns != 0.0f) {
                const std::string& zoom = field("Zoom");
                const float current = object->zoom;
                const float was = zoom.ends_with('%') ? parseNumber(zoom.substr(0, zoom.size() - 1)).value_or(current * 100.0f) / 100.0f
                                                      : parseNumber(zoom).value_or(current);
                field("Zoom") = std::format("{}%", std::lround(std::clamp(was + turns * 0.1f, 0.1f, 4.0f) * 100.0f));
                write(std::format("zoom:{}:{}", row->line, d.focusCount));
            }
            if (ui.click(shown) && found) { // where it is now, and how far a pixel moves it (so it stays under the mouse)
                const float perPixel = engine.canvas().w / shown.w; // (the engine.canvas()'s pixels a pixel of the preview is)
                DeveloperTools::PlaceDrag g{.x = d.mouseX, .y = d.mouseY, .across = 0.0f, .up = 0.0f, .perX = 0.0f, .perY = 0.0f, .moving = false,
                                            .key = ++d.focusCount};
                // (its picture lines up with the screen as much as its place is across: it moves by the room left beside it)
                const Stage::ObjectState& o = *object;
                std::string key = o.image;
                const CharacterDef* def = o.character.empty() || !d.script ? nullptr : d.script->character(o.character);
                if (def) key = def->pictureOf(o.expression);
                const Picture pic = engine.assets().picture(key);
                const float sized = (def && pic ? std::min(1.0f, engine.canvas().h * (d.script ? d.script->config.spriteHeight : 0.95f) / pic.h()) : 1.0f) * o.zoom;
                const float w = pic ? pic.w() * sized : engine.canvas().w * 0.5f * o.zoom, h = pic ? pic.h() * sized : engine.canvas().h * 0.5f * o.zoom;
                g.across = o.x * 2.0f - 1.0f;
                g.up = 1.0f - o.y * 2.0f;
                g.perX = 2.0f * perPixel / (std::abs(engine.canvas().w - w) > 1.0f ? engine.canvas().w - w : engine.canvas().w);
                g.perY = -2.0f * perPixel / (std::abs(engine.canvas().h - h) > 1.0f ? engine.canvas().h - h : engine.canvas().h);
                d.placeDrag = g;
            }
            if (d.placeDrag) {
                auto& g = *d.placeDrag;
                const float dx = d.mouseX - g.x, dy = d.mouseY - g.y;
                if (!g.moving && std::max(std::abs(dx), std::abs(dy)) > 6 * k) g.moving = true;
                if (g.moving) { // (in steps of 0.05; only what the drag changes is written)
                    auto step = [](float v) { return std::clamp(std::round(v * 20.0f) / 20.0f, -1.0f, 1.0f); };
                    auto text = [](float v) { return std::format("{}", v == 0.0f ? 0.0f : v); };
                    bool changed = false;
                    for (const auto& [name, from, by] : {std::tuple{std::string("Place"), g.across, g.perX * dx}, {std::string("Height"), g.up, g.perY * dy}})
                        if (step(from + by) != step(from) && field(name) != text(step(from + by))) {
                            field(name) = text(step(from + by));
                            changed = true;
                        }
                    if (changed) write(std::format("place:{}:{}", row->line, g.key));
                }
                if (!d.held) { // let go: it was a click (the game's, as its controls take it), unless something was moved
                    if (!g.moving) {
                        d.previewPlaying = true;
                        previewEvent(mouseEvent(SDL_EVENT_MOUSE_BUTTON_DOWN));
                        previewEvent(mouseEvent(SDL_EVENT_MOUSE_BUTTON_UP));
                    }
                    d.placeDrag.reset();
                }
            }
        } else {
            previewEvent(mouseEvent(SDL_EVENT_MOUSE_MOTION)); // (what the mouse is on)
            if (d.clicked && ui.over(shown)) {
                d.clicked = false;
                d.focus.clear();
                d.previewHeld = true;
                d.previewPlaying = true;
                previewEvent(mouseEvent(SDL_EVENT_MOUSE_BUTTON_DOWN));
            }
            if (d.previewHeld && !d.held) {
                d.previewHeld = false;
                previewEvent(mouseEvent(SDL_EVENT_MOUSE_BUTTON_UP));
            }
            if (const float turns = ui.wheelOver(shown); turns != 0.0f) previewEvent(wheelEventAt(turns, mx, my));
            if (d.focus.empty() && ui.over(shown)) { // (nothing typed into: the keys are the game's while the mouse is on it)
                if (!d.typed.empty()) {
                    previewEvent(textEvent(d.typed.c_str()));
                    d.typed.clear();
                }
                std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
                    if (key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) return false; // (undo and the like: the editor's)
                    d.previewPlaying = true;
                    previewEvent(keyEvent(key));
                    return true;
                });
            }
        }
        previewHooks();
        previewDid(); // (a button whose click animation is over)
    } else {
        ui.textAround(d.problems.empty() ? "Nothing to show here yet."_tr : "The preview shows once the scripts' mistakes are fixed."_tr, shown.x + 14 * k,
                      shown.y + shown.h / 2, layout::noteText, theme::dim, shown.w - 28 * k);
    }
    draw::outline(r, shown, kEdge);
}

// The preview takes an event (in the game's coordinates) as the game would: the screens its buttons
// opened (the top first), the screen the line opens, its choices, its textbox, the screens the story
// shows and the hud, then the game's controls (a click or a key goes on). What a button does is kept
// (as Try it keeps it) for previewDid.
void Developer::previewEvent(const SDL_Event& e) {
    DeveloperTools& d = tools;
    if (!d.preview || !d.script) return;
    { // (the preview's story in the engine's place while it takes it)
        const SwappedIn previewGame(engine.game_, d.preview);
        const SetFor previewViews(engine.viewsFrom_, d.script.get());
        const SetFor asTried(engine.trying_, true);
        auto handled = [&] {
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) // (a button pressed is clicked when the mouse comes up: its screen takes it)
                for (auto& [name, v] : d.previewViews)
                    if (v->pressing()) return v->handleEvent(e), true;
            const std::vector<std::string> over = d.previewOverlays; // (a copy: a button may open or close one)
            for (const std::string& name : over | std::views::reverse)
                if (ScreenView* v = engine.view(name); v && (v->handleEvent(e) || v->modal())) return true;
            if (engine.game_->openingScreen()) // (the screen the line opens: the story waits for it)
                if (ScreenView* v = engine.view(engine.game_->request().name); v && (v->handleEvent(e) || v->modal())) return true;
            if (engine.game_->uiHidden()) {
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)) engine.game_->setUiHidden(false);
                return true;
            }
            if (ScreenView* v = engine.view(engine.game_->choiceScreen()); v && engine.game_->showingChoices() && v->handleEvent(e)) return true;
            if (ScreenView* v = engine.textbox(); v && engine.game_->showingSay() && v->handleEvent(e)) return true;
            const std::vector<std::string> shown = engine.game_->shownScreens();
            for (const std::string& name : shown | std::views::reverse)
                if (ScreenView* v = engine.view(name); v && (v->handleEvent(e) || v->modal())) return true;
            if (ScreenView* v = engine.view("hud"); v && v->handleEvent(e)) return true;
            return false;
        };
        if (!handled())
            for (const Control& c : d.script->controls) // (with their keys as written: the player's own aren't the preview's)
                if (std::ranges::any_of(c.keys, [&](const KeyChord& k) { return k.matches(e); }) && (!c.condition || c.condition->test(engine.lookup()))) {
                    engine.run(c.actions);
                    break;
                }
    }
    previewDid();
}

// The screens that came onto the preview since last frame (the one the line opens, the ones the story
// shows, the ones its buttons opened) and the ones that went: their on_show= and on_hide=, as the
// game runs them (a title screen's music plays through the preview, when it's heard).
void Developer::previewHooks() {
    DeveloperTools& d = tools;
    if (!d.preview || !d.script) return;
    std::vector<std::string> now = d.preview->shownScreens();
    if (d.preview->openingScreen()) now.push_back(d.preview->request().name);
    now.insert(now.end(), d.previewOverlays.begin(), d.previewOverlays.end());
    const std::vector<std::string> before = std::exchange(d.previewScreens, now);
    for (const std::string& name : before)
        if (!std::ranges::contains(now, name)) runScreenHook(name, false);
    for (const std::string& name : now)
        if (!std::ranges::contains(before, name)) runScreenHook(name, true);
}

void Developer::runTrying(const Commands& commands, const std::string& confirm) {
    const SetFor _(engine.trying_, true);
    engine.run(commands, confirm);
}

void Developer::tryScreenHook(const ScreenDef& def, bool opening) {
    const SetFor _(engine.trying_, true);
    engine.screenHook(def, opening);
}

void Developer::runScreenHook(std::string_view screen, bool shown) {
    const ScreenDef* def = screen.empty() || !tools.script ? nullptr : tools.script->screen(screen);
    if (def) tryScreenHook(*def, shown);
}

// A screen of the preview's (and Try it's): the editor's, as it is now (Engine::view with viewsFrom_).
ScreenView* Developer::previewView(std::string_view name) {
    DeveloperTools& d = tools;
    if (!d.script) return nullptr;
    const SetFor _(engine.viewsFrom_, d.script.get());
    return engine.view(name);
}

// What the preview's buttons did (and what one whose click animation is over does): done to the
// preview where it can be (the story goes on, a choice, a variable set, a screen opened over it or
// closed), and only said otherwise (saving, the title screen, quitting... are the player's game's).
void Developer::previewDid() {
    DeveloperTools& d = tools;
    if (!d.preview || !d.script) return;
    const SetFor _(engine.viewsFrom_, d.script.get());
    for (auto& [name, v] : d.previewViews)
        if (auto did = v->takePressed()) {
            runTrying(did->first, did->second);
        }
    std::vector<std::string> said;
    for (std::vector<Command> batch = std::exchange(engine.tried_, {}); !batch.empty(); batch = std::exchange(engine.tried_, {})) { // (what one does may ask for more)
        for (const Command& c : batch) {
            const auto* u = std::get_if<cmd::Ui>(&c);
            if (!u) { // the story's own: a jump (what's open over it closes), a variable set, music...
                if (std::holds_alternative<cmd::Jump>(c)) d.previewOverlays.clear(), d.previewPending.clear();
                d.preview->perform(c);
                d.previewPlaying = true;
                continue;
            }
            const std::string& v = u->verb;
            if (v == "advance") {
                if (d.preview->skipMode()) d.preview->toggleSkip();
                else d.preview->advance();
                d.previewPlaying = true;
            } else if (v == "rollback") {
                d.preview->rollback();
            } else if (v == "skip") {
                d.preview->toggleSkip();
            } else if (v == "auto") {
                d.preview->toggleAuto();
            } else if (v == "hide ui") {
                d.preview->setUiHidden(true);
            } else if (v == "open" && !u->arg.contains("://") && !u->arg.starts_with("www.")) {
                if (std::ranges::contains(d.previewOverlays, u->arg)) { // (open already: put away)
                    std::erase(d.previewOverlays, u->arg);
                } else if (ScreenView* screen = engine.view(u->arg)) {
                    std::erase(d.previewLeaving, u->arg); // (still closing: open again)
                    ScreenView* under = !d.previewOverlays.empty() ? engine.view(d.previewOverlays.back()) : d.preview->openingScreen() ? engine.view(d.preview->request().name) : nullptr;
                    const std::pair with{ScreenView::animationIn(d.script.get(), u->opening), ScreenView::animationIn(d.script.get(), u->closing)}; // (with NAME, and close with NAME: this time)
                    screen->playWith(with.first, with.second);
                    if (screen->startsCovered() && under && under != screen && under->endsCovered() && !under->hasLeft()) { // (animations wait for each other: the one under fades out first)
                        under->leave();
                        d.previewPending = u->arg;
                        d.previewPendingWith = with;
                    } else {
                        screen->reset();
                        screen->playWith(with.first, with.second);
                        d.previewOverlays.push_back(u->arg);
                    }
                } else {
                    said.push_back("open " + u->arg);
                }
            } else if (v == "close" || v == "yes" || v == "no" || v == "answer") {
                if (!d.previewOverlays.empty()) {
                    const std::string name = d.previewOverlays.back();
                    d.previewOverlays.pop_back();
                    if (ScreenView* screen = engine.view(name); screen && screen->hideAnimation()) { // (its hide animation plays, as in the game: drawn until it's over)
                        screen->leave();
                        d.previewLeaving.push_back(name);
                    }
                } else if (d.preview->openingScreen()) { // (the screen the line opened: the story goes on)
                    d.preview->screenClosed(v == "answer" ? std::optional<Value>(expand(u->arg, d.preview->lookup())) : std::nullopt);
                    d.previewPlaying = true;
                }
            } else {
                said.push_back(u->arg.empty() ? v : v + " " + u->arg);
            }
        }
    }
    if (!said.empty()) {
        d.failed = false;
        d.status = "In the game, that would {}."_tr(joined(said)); // (what it would do, as the scripts say it)
    }
}

void Developer::makePreview(const StoryEditor::Scene& scene, const StoryEditor::Row* row) {
    DeveloperTools& d = tools;
    if (!d.script || !d.script->errors.empty()) return;
    const bool heard = d.previewSound && engine.waiting_;
    if (heard) d.heard = &engine.audio();
    d.preview = playedTo(*d.script, scene, row ? std::optional(row->line) : std::nullopt, heard ? engine.audio() : d.silence, d.previewReached);
    if (d.preview && d.preview->openingScreen()) { // (the screen the line opens: with NAME and close with NAME, as in the game)
        const SetFor previewViews(engine.viewsFrom_, d.script.get());
        if (ScreenView* v = engine.view(d.preview->request().name))
            v->playWith(ScreenView::animationIn(d.script.get(), d.preview->request().opening), ScreenView::animationIn(d.script.get(), d.preview->request().closing));
    }
}

std::unique_ptr<Game> Developer::playedTo(const Script& s, const StoryEditor::Scene& scene, std::optional<std::size_t> line, Audio& audio, bool& reached,
                                       bool before) {
    DeveloperTools& d = tools;
    const auto start = s.findLabel(scene.name);
    if (!start) return nullptr;
    // The commands it may stop at: the line's (an option's: its choice's), or the scene's after it.
    const std::string file = d.editor.path(scene.file);
    std::size_t from = line.value_or(scene.line);
    if (line) {
        const auto rows = d.editor.rows(scene);
        if (const auto row = std::ranges::find(rows, *line, &StoryEditor::Row::line); row != rows.end() && row->kind == Kind::Option)
            for (const StoryEditor::Row& other : rows)
                if (other.line < row->line && other.depth == row->depth - 1 && other.kind == Kind::Choice) from = other.line;
    }
    auto there = [&](std::size_t pc) {
        const Instruction& ins = s.code[pc];
        return s.files[ins.file] == file && ins.line > static_cast<int>(from) && ins.line <= static_cast<int>(scene.end);
    };
    std::optional<std::size_t> target;
    for (std::size_t pc = 0; pc < s.code.size(); ++pc)
        if (there(pc) && (!target || s.code[pc].line < s.code[*target].line)) target = pc;
    if (!target) return nullptr;
    // (so nothing the player has changes)
    d.previewRead = engine.readLog();
    d.previewPersistent = engine.persistent();
    d.previewChart = engine.chart();
    // (a scene the story calls ends at its return, as a splash screen does)
    const bool called = std::ranges::any_of(d.editor.links(), [&](const StoryEditor::Link& l) { return l.call && l.to == lower(scene.name); });
    auto play = [&](std::size_t pc, bool& got, bool& skipped) {
        auto game = std::make_unique<Game>(Context{engine.renderer(), engine.assets(), audio, engine.settings_, d.previewRead, d.previewPersistent, d.previewChart, engine.canvas(),
                                                   [this](std::string_view name) { return engine.builtin(name); }},
                                           s);
        if (before) game->stopBefore(*target);
        game->start(pc, called);
        got = game->playTo(*target, there, &skipped);
        return game;
    };
    bool got = false, skipped = false;
    auto game = play(*start, got, skipped);
    reached = got && !skipped; // (skipped: the story went past it, so it goes on from the line with what it had then)
    if (!got) { // on its own, without what comes before it
        bool shown = false;
        game = play(*target, shown, skipped);
    }
    return game;
}

// Play from this line (or this scene: its first line): the story played silently to it, then the game
// goes on from there as from a save made at it (its music, the stage and the variables as they are
// there).
void Developer::playFromLine(const std::string& scene, std::optional<std::size_t> line) {
    DeveloperTools& d = tools;
    const auto& scenes = d.editor.scenes();
    const auto it = std::ranges::find(scenes, scene, &StoryEditor::Scene::name);
    bool reached = false;
    const auto played = it != scenes.end() ? playedTo(engine.script(), *it, line, d.silence, reached, true) : nullptr;
    if (!played || !played->canSave() || !engine.resume(played->save())) engine.newGame(scene);
}

bool Developer::paintCanvas(const std::function<void()>& paint) {
    DeveloperTools& d = tools;
    SDL_Renderer* r = engine.renderer();
    float w = 0.0f, h = 0.0f;
    if (d.canvas) SDL_GetTextureSize(d.canvas.get(), &w, &h);
    if (!d.canvas || w != engine.canvas().w || h != engine.canvas().h) {
        d.canvas.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, static_cast<int>(engine.canvas().w), static_cast<int>(engine.canvas().h)));
        if (d.canvas) SDL_SetTextureScaleMode(d.canvas.get(), SDL_SCALEMODE_LINEAR);
    }
    if (!d.canvas) return false;
    const RenderTarget _(r, d.canvas.get());
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    paint();
    return true;
}

void Developer::makeSample(bool choice) {
    DeveloperTools& d = tools;
    d.sample.reset();
    if (!d.script || !d.script->errors.empty()) return;
    const Script& s = *d.script;
    const auto start = s.findLabel("start");
    if (!start) return;
    d.previewRead = engine.readLog();
    d.previewPersistent = engine.persistent();
    d.previewChart = engine.chart();
    const bool heard = d.trying && d.previewSound && engine.waiting_; // (a screen tried with its sound on: heard, as the Story tab's preview is)
    if (heard) d.heard = &engine.audio();
    d.sample = std::make_unique<Game>(Context{engine.renderer(), engine.assets(), heard ? engine.audio() : d.silence, engine.settings_, d.previewRead, d.previewPersistent, d.previewChart, engine.canvas(),
                                              [this](std::string_view name) { return engine.builtin(name); }},
                                      s);
    d.sample->start(*start);
    d.sample->playTo(SIZE_MAX, [&](std::size_t pc) {
        return choice ? std::holds_alternative<cmd::Menu>(s.code[pc].command) : std::holds_alternative<cmd::Say>(s.code[pc].command);
    });
    d.sample->addSamples(); // (history and phone screens have something to show)
}

} // namespace vn
