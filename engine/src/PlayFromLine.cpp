// Playing from the developer tools: Play from this line (the story played silently to a line first), Stop here, and
// the lines the story has reached.
#include "DeveloperUi.hpp"

#include <charconv>

namespace vn {

// The script lines the game ran in the developer's plays, by their file and text (so they stay known
// when lines move), kept in reached.txt with the player's data: the Story page marks the others.
void Developer::loadReached() {
    engine.reached_.clear();
    if (const auto text = readTextFile(engine.prefPath() + "reached.txt"))
        for (const std::string& line : linesOf(*text))
            if (!line.empty()) engine.reached_.insert(line);
    ++engine.reachedGeneration_;
}

void Developer::recordReached() {
    if (!engine.game_) return;
    const std::vector<std::size_t> ran = engine.game_->takeRan();
    const Script& script = engine.script();
    DeveloperTools& d = tools;
    if (!engine.gameSettings().developer || !d.editor.loaded() || engine.prefPath().empty()) return;
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

// Stop here's lines, in stops.txt with the player's data: "FILE<tab>LINE<tab>WORDS" each (LINE counting
// from 1). They find their lines again the first time the scripts are read (followStops).
void Developer::loadStops() {
    DeveloperTools& d = tools;
    d.stops.clear();
    d.stopsSeen.clear();
    d.stepping = false;
    d.passing.reset();
    if (engine.prefPath().empty()) return;
    if (const auto text = readTextFile(engine.prefPath() + "stops.txt"))
        for (const std::string& line : linesOf(*text)) {
            const std::size_t tab = line.find('\t'), tab2 = tab == std::string::npos ? tab : line.find('\t', tab + 1);
            if (tab2 == std::string::npos) continue;
            std::size_t number = 0;
            if (const std::string_view n = std::string_view(line).substr(tab + 1, tab2 - tab - 1);
                std::from_chars(n.data(), n.data() + n.size(), number).ec != std::errc{} || number == 0)
                continue;
            d.stops.push_back({.path = line.substr(0, tab), .line = number - 1, .text = line.substr(tab2 + 1)});
        }
}

void Developer::saveStops() {
    if (engine.prefPath().empty()) return;
    std::string out;
    for (const DeveloperTools::StopMark& s : tools.stops) out += std::format("{}\t{}\t{}\n", s.path, s.line + 1, s.text);
    SDL_SaveFile((engine.prefPath() + "stops.txt").c_str(), out.data(), out.size());
}

bool Developer::hasStop(std::string_view path, std::size_t line) const {
    return std::ranges::any_of(tools.stops, [&](const DeveloperTools::StopMark& s) { return s.line == line && s.path == path; });
}

void Developer::toggleStop(const std::string& path, std::size_t line) {
    DeveloperTools& d = tools;
    if (std::erase_if(d.stops, [&](const DeveloperTools::StopMark& s) { return s.line == line && s.path == path; }) > 0) {
        logInfo("{}", "The story doesn't stop at this line any more."_tr());
    } else {
        const std::vector<std::string>* lines = d.editor.fileLines(path);
        if (!lines || line >= lines->size()) return;
        d.stops.push_back({.path = path, .line = line, .text = std::string(trim((*lines)[line]))});
        d.stopsSeen.insert_or_assign(path, *lines); // (its file as it is now: the stop moves with its line from here)
    }
    saveStops();
}

// After the scripts changed, each stop moves with its line. In a file that changed, the lines before
// the change and after it are the same lines (those after it further up or down); a stop in the lines
// that changed goes to its words there, or stays where it is when as many lines changed as were there
// (its line was typed into). One whose line went, goes too. Stops read from stops.txt look for their
// words: at their line, else the nearest line with them.
void Developer::followStops() {
    DeveloperTools& d = tools;
    if (d.stops.empty() || !d.editor.loaded()) return;
    bool changed = false;
    std::vector<DeveloperTools::StopMark> kept;
    for (DeveloperTools::StopMark s : d.stops) {
        const std::vector<std::string>* now = d.editor.fileLines(s.path);
        if (!now) {
            changed = true;
            continue;
        }
        auto words = [&](std::size_t i) { return trim((*now)[i]); };
        std::optional<std::size_t> at;
        if (const auto seen = d.stopsSeen.find(s.path); seen != d.stopsSeen.end()) {
            const std::vector<std::string>& was = seen->second;
            std::size_t before = 0, after = 0;
            while (before < was.size() && before < now->size() && was[before] == (*now)[before]) ++before;
            while (after < was.size() - before && after < now->size() - before && was[was.size() - 1 - after] == (*now)[now->size() - 1 - after]) ++after;
            if (s.line < before) {
                at = s.line;
            } else if (s.line >= was.size() - after) {
                at = s.line - (was.size() - after) + (now->size() - after);
            } else {
                for (std::size_t i = before; i < now->size() - after && !at; ++i)
                    if (words(i) == s.text) at = i;
                if (!at && was.size() == now->size()) at = s.line;
            }
        } else {
            for (std::size_t distance = 0; !at && distance < now->size(); ++distance)
                for (const std::size_t i : {s.line + distance, s.line - std::min(s.line, distance)})
                    if (!at && i < now->size() && words(i) == s.text) at = i;
        }
        if (!at || *at >= now->size() || words(*at).empty() ||
            std::ranges::any_of(kept, [&](const DeveloperTools::StopMark& k) { return k.line == *at && k.path == s.path; })) {
            changed = true;
            continue;
        }
        if (*at != s.line || words(*at) != s.text) changed = true;
        s.line = *at;
        s.text = std::string(words(*at));
        kept.push_back(std::move(s));
    }
    d.stops = std::move(kept);
    d.stopsSeen.clear();
    for (const DeveloperTools::StopMark& s : d.stops)
        if (!d.stopsSeen.contains(s.path)) d.stopsSeen.emplace(s.path, *d.editor.fileLines(s.path));
    if (changed) saveStops();
}

bool Developer::stopsBefore(std::size_t pc) {
    DeveloperTools& d = tools;
    const Script& script = engine.script();
    if (pc >= script.code.size()) return false;
    const Instruction& ins = script.code[pc];
    if (ins.tail || ins.line < 1 || ins.file >= script.files.size()) return false;
    std::pair<std::string, std::size_t> here{script.files[ins.file], static_cast<std::size_t>(ins.line - 1)};
    if (const auto passing = std::exchange(d.passing, std::nullopt); passing == here) return false; // (the line it stopped at: done now)
    if (!(d.stepping && here != d.stepFrom && d.rowStarts.contains(here)) && !hasStop(here.first, here.second)) return false;
    d.passing = std::move(here); // (done first, when it goes on)
    return true;
}

void Developer::storyStopped() {
    DeveloperTools& d = tools;
    const bool step = d.stepping;
    openDeveloper(); // (at the line the story is at)
    d.page = Page::Story;
    const auto at = storyAt();
    const std::string place = at ? "line {} of {}"_tr(at->second + 1, at->first.substr(at->first.find('/') + 1)) : "a line"_tr();
    logInfo("{}", step ? "Next line: the story is at {} now."_tr(place) : "The story stopped just before {} (Stop here)."_tr(place));
}

// Where the story under the developer tools is (they were opened over the game): the file and line of
// the command it's at.
std::optional<std::pair<std::string, std::size_t>> Developer::storyAt() const {
    if (engine.mode() != Engine::Mode::Game || engine.waiting_ || !engine.game_) return std::nullopt;
    const Script& script = engine.script();
    const std::size_t pc = engine.game_->here();
    if (pc >= script.code.size() || script.code[pc].line < 1 || script.code[pc].file >= script.files.size()) return std::nullopt;
    return std::pair{script.files[script.code[pc].file], static_cast<std::size_t>(script.code[pc].line - 1)};
}

float Developer::stopHereButton(DeveloperUi& ui, const std::string& path, std::size_t line, float rightEdge, float y) {
    const float w = ui.buttonWidth("Stop here"_tr, layout::buttonText, layout::toggleW);
    if (ui.button({rightEdge - w, y, w, layout::buttonH * ui.k}, "Stop here"_tr, hasStop(path, line), true, layout::buttonText)) toggleStop(path, line);
    return w;
}

// A game of the tools' own, heard through silence, with fresh copies of what the player has seen and kept
// (so nothing the player has changes).
std::unique_ptr<Game> Developer::silentGame(const Script& s) {
    DeveloperTools& d = tools;
    d.playRead = engine.readLog();
    d.playPersistent = engine.persistent();
    d.playFlowchart = engine.flowchart();
    return std::make_unique<Game>(Context{engine.renderer(), engine.assets(), d.silence, engine.settings_, d.playRead, d.playPersistent, d.playFlowchart, engine.canvas(),
                                          [this](std::string_view name) { return engine.builtin(name); }},
                                  s);
}

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
    auto play = [&](std::size_t pc, bool& got) {
        auto game = silentGame(s);
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

} // namespace vn
