// The games the developer tools play on their own, silently, with copies of what the player has seen
// and kept: the story played to a line for Play from this line, and the Screens tab's sample under its
// screens.
#include "DeveloperShared.hpp"

namespace vn {

// The story of `s` played silently, as fast as it goes, to just before a line of a scene (an option's:
// its choice's; none: the scene's first), in a game of its own with copies of what the player has seen
// and kept. When it never gets there from the scene's start, it starts at the line itself. None when
// the line has nothing to stop at.
std::unique_ptr<Game> Developer::playedTo(const Script& s, const StoryEditor::Scene& scene, std::optional<std::size_t> line) {
    DeveloperTools& d = tools;
    const auto start = s.findScene(scene.name);
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
    for (std::size_t pc = 0; pc < s.code.size(); ++pc) // (not a jump or return ending what's under a line: the story isn't at that line then)
        if (there(pc) && !s.code[pc].tail && (!target || s.code[pc].line < s.code[*target].line)) target = pc;
    if (!target) return nullptr;
    // (so nothing the player has changes)
    d.playRead = engine.readLog();
    d.playPersistent = engine.persistent();
    d.playFlowchart = engine.flowchart();
    auto play = [&](std::size_t pc, bool& got) {
        auto game = std::make_unique<Game>(Context{engine.renderer(), engine.assets(), d.silence, engine.settings_, d.playRead, d.playPersistent, d.playFlowchart, engine.canvas(),
                                                   [this](std::string_view name) { return engine.builtin(name); }},
                                           s);
        game->stopBefore(*target);
        game->start(pc);
        got = game->playTo(*target, there);
        return game;
    };
    bool got = false;
    auto game = play(*start, got);
    if (!got) game = play(*target, got); // (on its own, without what comes before it)
    return game;
}

// Play from this line (or this scene: its first line): the story played silently to it, then the game
// goes on from there as from a save made at it (its music, the stage and the variables as they are
// there).
void Developer::playFromLine(const std::string& scene, std::optional<std::size_t> line) {
    DeveloperTools& d = tools;
    const auto& scenes = d.editor.scenes();
    const auto it = std::ranges::find(scenes, scene, &StoryEditor::Scene::name);
    const auto played = it != scenes.end() ? playedTo(engine.script(), *it, line) : nullptr;
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
    const auto start = s.findScene("start");
    if (!start) return;
    d.playRead = engine.readLog();
    d.playPersistent = engine.persistent();
    d.playFlowchart = engine.flowchart();
    d.sample = std::make_unique<Game>(Context{engine.renderer(), engine.assets(), d.silence, engine.settings_, d.playRead, d.playPersistent, d.playFlowchart, engine.canvas(),
                                              [this](std::string_view name) { return engine.builtin(name); }},
                                      s);
    d.sample->start(*start);
    d.sample->playTo(SIZE_MAX, [&](std::size_t pc) {
        return choice ? std::holds_alternative<cmd::Menu>(s.code[pc].command) : std::holds_alternative<cmd::Say>(s.code[pc].command);
    });
}

} // namespace vn
