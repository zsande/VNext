// The editor's changes: undo and redo, scenes, lines and characters added, moved, renamed and removed.
#include "Forms.hpp"

#include <climits>
#include <ranges>

namespace vn {

// --- Changes ---------------------------------------------------------------------------------

StoryEditor::Snapshot StoryEditor::snapshot() const {
    Snapshot out;
    for (const File& f : files_) out.emplace_back(f.path, f.lines);
    return out;
}

// The files as they were: changed ones written, missing ones made again, and ones that weren't there
// then taken away.
void StoryEditor::restore(const Snapshot& snapshot) {
    std::vector<std::string> gone;
    for (const File& f : files_)
        if (std::ranges::none_of(snapshot, [&](const auto& s) { return s.first == f.path; })) gone.push_back(f.path);
    std::vector<File> next;
    std::vector<std::size_t> changed;
    for (const auto& [path, lines] : snapshot) {
        const auto it = std::ranges::find(files_, path, &File::path);
        if (it == files_.end() || it->lines != lines) changed.push_back(next.size());
        next.push_back({.path = path, .lines = lines, .modified = it != files_.end() ? it->modified : 0});
    }
    files_ = std::move(next);
    for (const std::size_t f : changed) write(f);
    for (const std::string& path : gone) {
        SDL_RemovePath((root_ + path).c_str());
        ++version_;
    }
    rescan();
}

void StoryEditor::remember(const std::string& key) {
    if (!key.empty() && key == lastKey_) return; // (still typing into the same field)
    lastKey_ = key;
    undo_.push_back({.files = snapshot(), .renamed = {}});
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
}

void StoryEditor::typedOn(const std::string& key) {
    if (!key.empty() && key == lastKey_ && !undo_.empty()) restore(undo_.back().files);
}

void StoryEditor::write(std::size_t file) {
    const std::string text = joinedLines(files_[file].lines);
    const std::string path = root_ + files_[file].path;
    if (!SDL_SaveFile(path.c_str(), text.data(), text.size())) problem_ = "Can't save '{}': {}."_tr(files_[file].path, SDL_GetError());
    if (SDL_PathInfo info; SDL_GetPathInfo(path.c_str(), &info)) files_[file].modified = info.modify_time;
    ++version_;
}

void StoryEditor::writeAll() {
    for (std::size_t f = 0; f < files_.size(); ++f) write(f);
}

void StoryEditor::movedFiles(Moves moves) {
    if (moves.empty()) return;
    remember({});
    undo_.back().renamed = std::move(moves);
    ++version_;
}

StoryEditor::Moves StoryEditor::undoMoves(const Moves& moves) {
    Moves again;
    for (const auto& [from, to] : moves | std::views::reverse) {
        if (from.empty()) { // (imported: to the Trash, and back from there)
            if (const auto there = moveToTrash(to); there && !there->empty()) again.emplace_back(*there, to);
        } else if (SDL_RenamePath(to.c_str(), from.c_str())) {
            again.emplace_back(from, to);
        }
    }
    std::ranges::reverse(again);
    return again;
}

StoryEditor::Moves StoryEditor::redoMoves(const Moves& moves) {
    Moves again;
    for (const auto& [from, to] : moves)
        if (SDL_RenamePath(from.c_str(), to.c_str())) again.emplace_back(from, to);
    return again;
}

// (files moved in the step go back too, or again)
bool StoryEditor::undo() {
    if (undo_.empty()) return false;
    const Step before = std::move(undo_.back());
    undo_.pop_back();
    const Snapshot now = snapshot();
    redo_.push_back({.files = now, .renamed = undoMoves(before.renamed)});
    if (!before.renamed.empty()) ++version_;
    restore(before.files);
    lastKey_.clear();
    return true;
}

bool StoryEditor::redo() {
    if (redo_.empty()) return false;
    const Step after = std::move(redo_.back());
    redo_.pop_back();
    const Snapshot now = snapshot();
    undo_.push_back({.files = now, .renamed = redoMoves(after.renamed)});
    if (!after.renamed.empty()) ++version_;
    restore(after.files);
    lastKey_.clear();
    return true;
}

bool StoryEditor::changedOutside() const {
    if (files_.empty()) return false;
    std::size_t there = 0;
    for (const std::string& name : listFiles(root_ + "scripts/", ".vnscript")) {
        if (name.contains('/')) continue;
        if (std::ranges::none_of(files_, [&](const File& f) { return f.path == "scripts/" + name; })) return true; // (a new one)
        ++there;
    }
    if (there != files_.size()) return true; // (one went)
    for (const File& f : files_)
        if (SDL_PathInfo info; SDL_GetPathInfo((root_ + f.path).c_str(), &info) && info.modify_time != f.modified) return true;
    return false;
}

std::string StoryEditor::textOf(std::size_t file, std::size_t from, std::size_t until) const {
    const auto& lines = files_[file].lines;
    from = std::min(from, lines.size());
    return joined(unshared(std::span(lines).subspan(from, std::max(from, std::min(until, lines.size())) - from)), 0, "\n");
}

std::size_t StoryEditor::pasteScene(std::size_t after, std::vector<std::string> lines) {
    std::string base(trim(afterFirst(lines.front())));
    if (base.ends_with(':')) base.pop_back();
    if (!validName(base)) base = "scene";
    const std::string name = uniqueName(base, [&](const std::string& n) { return hasScene(n); });
    lines.front() = "scene " + name;
    const Scene s = scenes_[after];
    remember({});
    lines.insert(lines.begin(), "");
    auto& file = files_[s.file].lines;
    file.insert(file.begin() + static_cast<std::ptrdiff_t>(s.end), lines.begin(), lines.end());
    write(s.file);
    rescan();
    return static_cast<std::size_t>(std::ranges::find(scenes_, name, &Scene::name) - scenes_.begin());
}

std::size_t StoryEditor::pasteCharacter(std::size_t after, const std::string& line) {
    auto words = rawWords(afterFirst(line));
    if (words.empty()) return after;
    const std::string base = unquote(words[0]);
    const std::string name = uniqueName(base, [&](const std::string& n) { return isCharacter(n); }, " ");
    words[0] = name.contains(' ') ? "\"" + name + "\"" : name;
    const Character c = characters()[after];
    remember({});
    files_[c.file].lines.insert(files_[c.file].lines.begin() + static_cast<std::ptrdiff_t>(c.line + 1), "character " + joined(words));
    write(c.file);
    rescan();
    const auto now = characters();
    return static_cast<std::size_t>(std::ranges::find(now, name, &Character::name) - now.begin());
}

std::string StoryEditor::fileName(std::size_t file) const { return stemOf(files_[file].path.substr(8)); } // (without scripts/ and .vnscript)

void StoryEditor::include(std::string_view name) {
    if (!hasMain()) return;
    auto& main = files_[0].lines;
    if (std::ranges::any_of(main, [&](const std::string& l) { return includesScript(l, lower(name)); })) return;
    std::size_t at = 0; // after the other include and setting lines (or the notes at the top)
    for (std::size_t i = 0; i < main.size(); ++i)
        if (lower(firstWord(main[i])) == "include" || settingOf(main[i])) at = i + 1;
    if (at == 0)
        while (at < main.size() && trim(main[at]).starts_with('#')) ++at;
    main.insert(main.begin() + static_cast<std::ptrdiff_t>(at), "include " + std::string(name));
    write(0);
}

bool StoryEditor::storyFile(std::size_t file) const {
    return std::ranges::contains(scenes_, file, &Scene::file) || std::ranges::all_of(files_[file].lines, [](const std::string& l) {
               const std::string_view t = trim(l);
               return t.empty() || t.starts_with('#') || t.starts_with("include ");
           });
}

bool StoryEditor::renameFile(std::size_t file, const std::string& name, const std::string& key) {
    typedOn(key);
    const std::string from = fileName(file), path = scriptPath(name);
    if (lower(name) == lower(from)) return true;
    if (file == 0 || !validName(name) || std::ranges::any_of(files_, [&](const File& f) { return lower(f.path) == lower(path); }) ||
        fileExists(root_ + path))
        return false;
    remember(key);
    const std::string old = files_[file].path;
    files_[file].path = path;
    write(file);
    SDL_RemovePath((root_ + old).c_str());
    changeLines([&](std::size_t, std::size_t, const std::string& line) -> std::optional<std::string> { // (its include lines follow)
        if (!includesScript(line, lower(from))) return std::nullopt;
        return leading(line) + "include " + name;
    });
    rescan();
    return true;
}

void StoryEditor::removeFile(std::size_t file) {
    if (file == 0 || file >= files_.size()) return;
    remember({});
    const std::string name = lower(fileName(file));
    for (std::size_t f = 0; f < files_.size(); ++f)
        if (f != file && std::erase_if(files_[f].lines, [&](const std::string& l) { return includesScript(l, name); }) > 0) write(f);
    if (!moveToTrash(root_ + files_[file].path)) SDL_RemovePath((root_ + files_[file].path).c_str()); // (undo has its lines still)
    files_.erase(files_.begin() + static_cast<std::ptrdiff_t>(file));
    ++version_;
    rescan();
}

std::optional<std::size_t> StoryEditor::moveSceneTo(std::size_t scene, std::size_t file, std::size_t to) {
    const Scene s = scenes_[scene];
    if (file == s.file) return moveScene(scene, to);
    if (lower(s.name) == "start") return std::nullopt; // (it stays in main.vnscript)
    remember({});
    auto& source = files_[s.file].lines;
    const std::vector<std::string> block(source.begin() + static_cast<std::ptrdiff_t>(s.line), source.begin() + static_cast<std::ptrdiff_t>(s.end));
    std::size_t from = s.line, until = s.end;
    if (until < source.size() && blank(source[until])) ++until;
    else if (from > 0 && blank(source[from - 1])) --from;
    source.erase(source.begin() + static_cast<std::ptrdiff_t>(from), source.begin() + static_cast<std::ptrdiff_t>(until));
    auto& target = files_[file].lines;
    const std::size_t at = to < scenes_.size() && scenes_[to].file == file ? scenes_[to].line : target.size();
    std::vector<std::string> put = block;
    if (at > 0 && !blank(target[at - 1])) put.insert(put.begin(), "");
    if (at < target.size() && !blank(target[at])) put.emplace_back();
    target.insert(target.begin() + static_cast<std::ptrdiff_t>(at), put.begin(), put.end());
    write(s.file);
    write(file);
    rescan();
    for (std::size_t i = 0; i < scenes_.size(); ++i)
        if (scenes_[i].file == file && lower(scenes_[i].name) == lower(s.name)) return i;
    return std::nullopt;
}

void StoryEditor::setText(const Scene& scene, const Row& row, const std::string& text, const std::string& key) {
    std::string& line = files_[scene.file].lines[row.line];
    if (trim(line) == text) return;
    remember(key);
    line = leading(line) + text;
    write(scene.file);
}

void StoryEditor::setRowLines(const Scene& scene, const Row& row, const std::string& text, const std::vector<std::string>& inside, const std::string& key) {
    auto& lines = files_[scene.file].lines;
    std::vector<std::size_t> within; // (the lines inside it, blank ones aside)
    for (std::size_t i = row.line + 1; i < row.end; ++i)
        if (!blank(lines[i])) within.push_back(i);
    const bool untouched = std::ranges::all_of(within, [&](std::size_t i) { return trim(lines[i]) == "\"\"" || trim(lines[i]) == "\"\":"; });
    if (!untouched) return setText(scene, row, text, key); // (lines someone wrote stay)
    remember(key);
    const std::string indent = leading(lines[row.line]);
    lines[row.line] = indent + text;
    for (const std::size_t i : within | std::views::reverse) lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
    for (std::size_t i = 0; i < inside.size(); ++i) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(row.line + 1 + i), indent + inside[i]);
    write(scene.file);
    rescan();
}

std::size_t StoryEditor::add(const Scene& scene, const Row* row, bool inside, const std::vector<std::string>& lines) {
    remember({});
    auto& file = files_[scene.file].lines;
    const auto rs = rows(scene);
    std::size_t at = row ? row->end : blockEnd(scene.file, scene.line);
    std::string indent = row ? leading(file[row->line]) + (inside ? std::string(kIndent, ' ') : "")
                       : rs.empty() ? std::string(kIndent, ' ') : leading(file[rs.front().line]);
    if (row && inside) // (as far in as the lines already inside it)
        for (const Row& r : rs)
            if (r.line > row->line && r.line < row->end && r.depth == row->depth + 1) {
                indent = leading(file[r.line]);
                break;
            }
    for (std::size_t i = 0; i < lines.size(); ++i) file.insert(file.begin() + static_cast<std::ptrdiff_t>(at + i), indent + lines[i]);
    write(scene.file);
    rescan();
    return at;
}

void StoryEditor::eraseLines(std::size_t file, std::size_t from, std::size_t until) {
    remember({});
    auto& lines = files_[file].lines;
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(until));
    write(file);
    rescan();
}

void StoryEditor::remove(const Scene& scene, const Row& row) { eraseLines(scene.file, row.line, row.end); }

std::size_t StoryEditor::addScene(std::size_t file) {
    const std::string name = uniqueName("new_scene", [&](const std::string& n) { return hasScene(n); }); // (one there already: new_scene_2...)
    remember({});
    auto& lines = files_[file].lines;
    if (!lines.empty() && !blank(lines.back())) lines.emplace_back();
    lines.push_back("scene " + name); // (empty: its lines are added in the Story page)
    write(file);
    rescan();
    return static_cast<std::size_t>(std::ranges::find(scenes_, name, &Scene::name) - scenes_.begin());
}

bool StoryEditor::renameScene(std::size_t scene, const std::string& name, const std::string& key) {
    const std::string was = scenes_[scene].name; // (as it's called now: its place on the Map is under that)
    typedOn(key);
    const std::string from = scenes_[scene].name;
    const bool same = lower(name) == lower(from), ok = same || (validName(name) && !hasScene(name));
    if (const auto at = mapPlace(was); at && lower(was) != lower(ok ? name : from)) { // (its place on the Map goes with it)
        mapPlaces_.erase(lower(was));
        mapPlaces_.insert_or_assign(lower(ok ? name : from), *at);
        saveMapPlaces();
    }
    if (same || !ok) return ok;
    remember(key);
    const Scene s = scenes_[scene];
    files_[s.file].lines[s.line] = "scene " + name;
    changeLines([&](std::size_t, std::size_t, const std::string& line) -> std::optional<std::string> {
        std::string after = renamed(line, from, name);
        return after != line ? std::optional(std::move(after)) : std::nullopt;
    }, s.file);
    rescan();
    return true;
}

void StoryEditor::removeScene(std::size_t scene) {
    const Scene s = scenes_[scene];
    const std::size_t from = s.line > 0 && blank(files_[s.file].lines[s.line - 1]) ? s.line - 1 : s.line; // (the blank line before it too)
    eraseLines(s.file, from, blockEnd(s.file, s.line));
}

std::size_t StoryEditor::moveLines(std::size_t file, std::size_t from, std::size_t until, std::size_t at, bool spaced) {
    auto& lines = files_[file].lines;
    const std::vector<std::string> block(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(until));
    if (spaced) { // (with the blank line next to it)
        if (until < lines.size() && blank(lines[until])) ++until;
        else if (from > 0 && blank(lines[from - 1])) --from;
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(until));
    if (at >= until) at -= until - from;
    std::vector<std::string> put = block;
    if (spaced && at > 0 && !blank(lines[at - 1])) put.insert(put.begin(), "");
    if (spaced && at < lines.size() && !blank(lines[at])) put.emplace_back();
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), put.begin(), put.end());
    return spaced && at > 0 && put.front().empty() ? at + 1 : at;
}

std::optional<std::size_t> StoryEditor::moveScene(std::size_t scene, std::size_t to) {
    return moveRecord(scenes_, scene, to, true, [](const Scene& s) { return s.end; }, [&] { return scenes_; });
}

namespace {

bool holdsLines(StoryEditor::Kind kind) {
    using Kind = StoryEditor::Kind;
    return kind == Kind::Option || kind == Kind::If || kind == Kind::Else || kind == Kind::Together || kind == Kind::Choice || kind == Kind::Chat ||
           kind == Kind::Call || kind == Kind::Missed;
}

// Whether a row can be in a row of `parent`'s kind (none: right under the scene): options only right under a
// choice, and nothing else there.
bool belongsIn(StoryEditor::Kind row, std::optional<StoryEditor::Kind> parent) {
    return (row == StoryEditor::Kind::Option) == (parent && *parent == StoryEditor::Kind::Choice);
}

// The rows a row's block covers: [item, end).
std::size_t blockEndOf(const std::vector<StoryEditor::Row>& rows, std::size_t item) {
    std::size_t j = item + 1;
    while (j < rows.size() && rows[j].depth > rows[item].depth) ++j;
    return j;
}

// The rows above and below a slot, not counting the item's own block (none: -1 / rows.size()).
std::pair<std::ptrdiff_t, std::size_t> around(const std::vector<StoryEditor::Row>& rows, std::size_t item, std::size_t slot) {
    const std::size_t end = blockEndOf(rows, item);
    const std::ptrdiff_t above = slot > item && slot <= end ? static_cast<std::ptrdiff_t>(item) - 1 : static_cast<std::ptrdiff_t>(slot) - 1;
    const std::size_t below = slot >= item && slot < end ? end : slot;
    return {above, below};
}

} // namespace

bool StoryEditor::fits(const std::vector<Row>& rows, std::size_t item, std::size_t slot, int depth) const {
    if (item >= rows.size() || slot > rows.size()) return false;
    const std::size_t end = blockEndOf(rows, item);
    if (slot > item && slot < end) return false; // (into itself)
    const auto [above, below] = around(rows, item, slot);
    const int lowest = below < rows.size() ? rows[below].depth : 0;
    const int highest = above >= 0 ? rows[static_cast<std::size_t>(above)].depth + (holdsLines(rows[static_cast<std::size_t>(above)].kind) ? 1 : 0) : 0;
    if (depth < lowest || depth > highest) return false;
    if ((slot == item || slot == end) && depth == rows[item].depth) return false; // (where it is)
    // What it would be inside: the nearest row above at one level less.
    std::optional<Kind> parent;
    for (std::ptrdiff_t j = above; j >= 0 && depth > 0; --j)
        if (rows[static_cast<std::size_t>(j)].depth == depth - 1) {
            parent = rows[static_cast<std::size_t>(j)].kind;
            break;
        }
    if (depth > 0 && !parent) return false;
    return belongsIn(rows[item].kind, parent);
}

std::optional<std::size_t> StoryEditor::moveRowTo(const Scene& scene, const Row& row, std::size_t slot, int depth) {
    const auto rs = rows(scene);
    const auto item = static_cast<std::size_t>(std::ranges::find(rs, row.line, &Row::line) - rs.begin());
    if (!fits(rs, item, slot, depth)) return std::nullopt;
    const auto moved = moveRowsTo(scene, {row.line}, slot, depth);
    return moved.empty() ? std::nullopt : std::optional(moved.front());
}

namespace {

// The rows (their places in `rows`) of these lines, in order, leaving out the ones inside another.
std::vector<std::size_t> blocksOf(const std::vector<StoryEditor::Row>& rows, std::vector<std::size_t> lines) {
    std::ranges::sort(lines);
    std::vector<std::size_t> out;
    std::size_t end = 0;
    for (const std::size_t line : lines) {
        const auto it = std::ranges::find(rows, line, &StoryEditor::Row::line);
        if (it == rows.end() || (!out.empty() && line < end)) continue;
        out.push_back(static_cast<std::size_t>(it - rows.begin()));
        end = it->end;
    }
    return out;
}

// A line moved from under `old` indentation to under `indent`: one further in stays as much further in.
std::string reindented(const std::string& line, const std::string& old, const std::string& indent) {
    return blank(line) ? line : line.starts_with(old) ? indent + line.substr(old.size()) : indent + std::string(trim(line));
}

} // namespace

void StoryEditor::removeRows(const Scene& scene, const std::vector<std::size_t>& lines) {
    const auto rs = rows(scene);
    const auto blocks = blocksOf(rs, lines);
    if (blocks.empty()) return;
    remember({});
    auto& file = files_[scene.file].lines;
    for (auto b = blocks.rbegin(); b != blocks.rend(); ++b)
        file.erase(file.begin() + static_cast<std::ptrdiff_t>(rs[*b].line), file.begin() + static_cast<std::ptrdiff_t>(rs[*b].end));
    write(scene.file);
    rescan();
}

std::string StoryEditor::textOfRows(const Scene& scene, const std::vector<std::size_t>& lines) const {
    const auto rs = rows(scene);
    std::string out;
    for (const std::size_t b : blocksOf(rs, lines)) out += (out.empty() ? "" : "\n") + textOf(scene.file, rs[b].line, rs[b].end); // (whole blocks)
    return out;
}

std::vector<std::size_t> StoryEditor::moveRowsTo(const Scene& scene, const std::vector<std::size_t>& lines, std::size_t slot, int depth) {
    const auto rs = rows(scene);
    const auto blocks = blocksOf(rs, lines);
    if (blocks.empty() || slot > rs.size()) return {};
    auto moving = [&](std::size_t i) { return std::ranges::any_of(blocks, [&](std::size_t b) { return i >= b && i < blockEndOf(rs, b); }); };
    if (slot < rs.size() && moving(slot) && !std::ranges::contains(blocks, slot)) return {}; // (into one of them)
    // The rows around the place (not the ones moving), what it'd be inside, and its indentation there.
    std::ptrdiff_t above = static_cast<std::ptrdiff_t>(slot) - 1;
    while (above >= 0 && moving(static_cast<std::size_t>(above))) --above;
    std::size_t below = slot;
    while (below < rs.size() && moving(below)) ++below;
    const int lowest = below < rs.size() ? rs[below].depth : 0;
    const int highest = above >= 0 ? rs[static_cast<std::size_t>(above)].depth + (holdsLines(rs[static_cast<std::size_t>(above)].kind) ? 1 : 0) : 0;
    if (depth < lowest || depth > highest) return {};
    std::optional<Kind> parent;
    for (std::ptrdiff_t j = above; j >= 0 && depth > 0; --j)
        if (rs[static_cast<std::size_t>(j)].depth == depth - 1) {
            parent = rs[static_cast<std::size_t>(j)].kind;
            break;
        }
    if (depth > 0 && !parent) return {};
    if (std::ranges::any_of(blocks, [&](std::size_t b) { return !belongsIn(rs[b].kind, parent); })) return {};
    auto& file = files_[scene.file].lines;
    std::string indent;
    for (std::ptrdiff_t j = above; j >= 0; --j) {
        const Row& r = rs[static_cast<std::size_t>(j)];
        if (r.depth <= depth) {
            indent = leading(file[r.line]) + (r.depth < depth ? std::string(kIndent, ' ') : std::string{});
            break;
        }
    }
    if (indent.empty()) indent = below < rs.size() && rs[below].depth == depth ? leading(file[rs[below].line]) : std::string(kIndent, ' ');
    remember({});
    std::vector<std::string> block;
    std::vector<std::size_t> starts;
    for (const std::size_t b : blocks) {
        starts.push_back(block.size());
        const std::string old = leading(file[rs[b].line]);
        for (std::size_t i = rs[b].line; i < rs[b].end; ++i) block.push_back(reindented(file[i], old, indent));
    }
    std::size_t to = below < rs.size() ? rs[below].line : blockEnd(scene.file, scene.line);
    for (auto b = blocks.rbegin(); b != blocks.rend(); ++b) {
        file.erase(file.begin() + static_cast<std::ptrdiff_t>(rs[*b].line), file.begin() + static_cast<std::ptrdiff_t>(rs[*b].end));
        if (rs[*b].line < to) to -= rs[*b].end - rs[*b].line;
    }
    file.insert(file.begin() + static_cast<std::ptrdiff_t>(to), block.begin(), block.end());
    write(scene.file);
    rescan();
    for (std::size_t& start : starts) start += to;
    return starts;
}

void StoryEditor::moveRowToScene(const Scene& scene, const Row& row, const Scene& to) {
    if (scene.file == to.file && scene.line == to.line) return;
    remember({});
    auto& source = files_[scene.file].lines;
    const std::string old = leading(source[row.line]);
    const std::vector<std::string> block(source.begin() + static_cast<std::ptrdiff_t>(row.line), source.begin() + static_cast<std::ptrdiff_t>(row.end));
    source.erase(source.begin() + static_cast<std::ptrdiff_t>(row.line), source.begin() + static_cast<std::ptrdiff_t>(row.end));
    rescan();
    const auto target = std::ranges::find_if(scenes_, [&](const Scene& s) { return s.file == to.file && lower(s.name) == lower(to.name); });
    if (target == scenes_.end()) return;
    const auto there = rows(*target);
    auto& lines = files_[target->file].lines;
    const std::string indent = there.empty() ? std::string(kIndent, ' ') : leading(lines[there.front().line]);
    std::vector<std::string> put;
    put.reserve(block.size());
    for (const std::string& l : block) put.push_back(reindented(l, old, indent));
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(target->end), put.begin(), put.end());
    write(scene.file);
    if (target->file != scene.file) write(target->file);
    rescan();
}

std::vector<StoryEditor::Found> StoryEditor::find(std::string_view what) const {
    std::vector<Found> out;
    const std::string low = lower(trim(what));
    if (low.empty()) return out;
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i)
            if (const std::string& line = files_[f].lines[i]; lower(line).contains(low)) out.push_back({.file = f, .line = i, .text = std::string(trim(line))});
    return out;
}

std::optional<std::string> StoryEditor::lineText(std::string_view path, std::size_t line) const {
    for (const File& f : files_)
        if (f.path == path) return line < f.lines.size() ? std::optional(std::string(trim(f.lines[line]))) : std::nullopt;
    return std::nullopt;
}

const std::vector<std::string>* StoryEditor::fileLines(std::string_view path) const {
    for (const File& f : files_)
        if (f.path == path) return &f.lines;
    return nullptr;
}

bool StoryEditor::mentions(std::string_view word) const {
    const std::string low = lower(word);
    if (low.empty()) return false;
    for (const File& f : files_)
        for (const std::string& line : f.lines) {
            const std::string l = lower(line);
            for (auto at = l.find(low); at != npos; at = l.find(low, at + 1))
                if ((at == 0 || !isWordChar(l[at - 1])) && (at + low.size() == l.size() || !isWordChar(l[at + low.size()]))) return true;
        }
    return false;
}

std::string StoryEditor::placeOf(std::size_t file, std::size_t line) const {
    for (const Scene& s : scenes_)
        if (s.file == file && s.line <= line && line < std::max(s.end, s.line + 1)) return s.name;
    for (const Screen& s : screens_)
        if (s.file == file && s.line <= line && line < std::max(s.end, s.line + 1)) return "screen " + s.name;
    for (const Character& c : characters())
        if (c.file == file && c.line == line) return "character " + c.name;
    for (const DefinitionKind& kind : kDefinitionKinds)
        for (const Definition& def : definitions(kind.kind))
            if (def.file == file && def.line <= line && line < std::max(def.end, def.line + 1)) return std::format("{} {}", kind.kind, def.name);
    if (file == 0 && isSetting(line)) return "Settings"_tr();
    return fileName(file) + ".vnscript";
}

std::size_t StoryEditor::changeLines(const std::function<std::optional<std::string>(std::size_t, std::size_t, const std::string&)>& change,
                                     std::optional<std::size_t> also) {
    std::size_t count = 0;
    for (std::size_t f = 0; f < files_.size(); ++f) {
        bool touched = also == f;
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i)
            if (auto now = change(f, i, files_[f].lines[i])) {
                files_[f].lines[i] = std::move(*now);
                touched = true;
                ++count;
            }
        if (touched) write(f);
    }
    return count;
}

std::vector<StoryEditor::Found> StoryEditor::uses(std::string_view variable) const {
    std::vector<Found> out;
    const std::string marked = std::string(variable) + "\x7f";
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i)
            if (const std::string& line = files_[f].lines[i]; lineWithVariable(line, variable, marked) != line && lower(firstWord(line)) != "variable")
                out.push_back({.file = f, .line = i, .text = std::string(trim(line))});
    return out;
}

std::optional<std::size_t> StoryEditor::renameVariable(const std::string& from, const std::string& to) {
    static constexpr std::string_view kWords[]{"and", "or", "not", "is", "to", "yes", "no", "true", "false", "random", "from", "by", "at", "with", "if", "else"};
    const std::string name = lower(to);
    if (name == lower(from)) return 0;
    if (name.empty() || !letterByte(name[0]) || !std::ranges::all_of(name, nameChar) || name.ends_with('.') ||
        std::ranges::contains(kWords, name) || std::ranges::contains(variables(), name))
        return std::nullopt;
    remember({});
    std::size_t uses = 0;
    changeLines([&](std::size_t, std::size_t, const std::string& line) -> std::optional<std::string> {
        std::string now = lineWithVariable(line, from, to);
        if (now == line) return std::nullopt;
        if (lower(firstWord(trim(line))) != "variable") ++uses; // (its own line isn't a use)
        return now;
    });
    rescan();
    return uses;
}

void StoryEditor::scaleLayout(const std::function<std::string(const std::string&)>& scale, const std::string& key) {
    remember(key);
    std::string block; // the definition the lines indented under are in
    std::size_t inFile = SIZE_MAX;
    changeLines([&](std::size_t f, std::size_t, const std::string& line) -> std::optional<std::string> {
        if (f != std::exchange(inFile, f)) block.clear(); // (each file's own)
        if (!partLine(line)) return std::nullopt;
        if (indentOf(line) == 0) block = lower(firstWord(line));
        if (!(indentOf(line) == 0 ? block == "style" : block == "screen" || block == "animation")) return std::nullopt;
        std::string now = scale(line);
        return now != line ? std::optional(std::move(now)) : std::nullopt;
    });
    rescan();
}

std::optional<std::size_t> StoryEditor::renameAsset(std::string_view folder, const std::string& from, const std::string& to) {
    const std::string dot = from.substr(from.rfind('.') == npos ? from.size() : from.rfind('.'));
    const std::string name = to.contains('.') ? to : to + dot; // (the extension it had, if none is typed)
    if (name == from) return 0;
    const std::string there = root_ + std::string(folder);
    if (!usableFileName(name) || name.contains('/') || !fileExists(there + from) || (lower(name) != lower(from) && fileExists(there + name))) return std::nullopt;
    remember({});
    if (!SDL_RenamePath((there + from).c_str(), (there + name).c_str())) {
        undo_.pop_back();
        return std::nullopt;
    }
    undo_.back().renamed.emplace_back(there + from, there + name);
    const auto bases = imageBases();
    const std::size_t count = changeLines([&](std::size_t, std::size_t, const std::string& line) -> std::optional<std::string> {
        std::string now = assetRenamed(line, folder, from, name, bases);
        return now != line ? std::optional(std::move(now)) : std::nullopt;
    });
    ++version_;
    rescan();
    return count;
}

std::vector<std::pair<std::string, std::string>> StoryEditor::imageBases() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Character& c : characters()) {
        std::string base = lower(c.name);
        std::ranges::replace(base, ' ', '_');
        for (const std::string& word : rawWords(afterFirst(files_[c.file].lines[c.line])))
            if (lower(word).starts_with("image=")) base = lower(unquote(word.substr(6)));
        out.emplace_back(lower(c.name), base);
    }
    return out;
}

bool StoryEditor::jumpAtEnd(std::size_t scene, const std::string& to) {
    const Scene s = scenes_[scene];
    const auto all = rows(s);
    const Row* last = nullptr; // (its last line right under it)
    for (const Row& r : all)
        if (r.depth == 0) last = &r;
    const std::string verb = last ? lower(firstWord(last->text)) : std::string{};
    if (verb == "jump") {
        setText(s, *last, "jump " + to, {});
        return true;
    }
    add(s, nullptr, false, {"jump " + to});
    return false;
}

const std::vector<StoryEditor::Link>& StoryEditor::links() const {
    if (links_.first == version_) return links_.second;
    std::vector<Link> out;
    for (std::size_t i = 0; i < scenes_.size(); ++i)
        for (const Row& row : rows(scenes_[i])) {
            if (row.kind == Kind::Jump) {
                const auto w = rawWords(row.text);
                if (w.size() >= 2) out.push_back({.from = i, .to = lower(w[1]), .line = row.line});
            }
        }
    links_ = {version_, std::move(out)};
    return links_.second;
}

void StoryEditor::removeLink(const Link& link) {
    if (link.from >= scenes_.size()) return;
    const Scene s = scenes_[link.from];
    const auto all = rows(s);
    const auto row = std::ranges::find(all, link.line, &Row::line);
    if (row != all.end()) remove(s, *row);
}

std::size_t StoryEditor::addCharacter() {
    const std::string name = uniqueName("Someone", [&](const std::string& n) { return isCharacter(n); }, " ");
    remember({});
    auto& lines = files_[0].lines;
    const std::string line = std::format("character {}", name.contains(' ') ? "\"" + name + "\"" : name);
    std::optional<std::size_t> last; // the main file's last character line
    for (const Character& c : characters())
        if (c.file == 0) last = c.line;
    if (last) {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*last + 1), line);
    } else if (const auto label = std::ranges::find_if(lines, [](const std::string& l) { return lower(firstWord(l)) == "scene"; });
               label != lines.end()) {
        lines.insert(lines.insert(label, ""), line);
    } else {
        lines.push_back(line);
    }
    write(0);
    rescan();
    const auto now = characters();
    return static_cast<std::size_t>(std::ranges::find(now, name, &Character::name) - now.begin());
}

bool StoryEditor::setCharacter(const Character& given, const std::vector<Field>& f, const std::string& key) {
    typedOn(key);
    const auto now = std::ranges::find_if(characters(), [&](const Character& one) { return one.file == given.file && one.line == given.line; });
    if (now == characters().end()) return true;
    const Character c = *now; // (as it was before the typing: renamed from that name)
    const auto it = std::ranges::find(f, std::string_view("Name"), &Field::name);
    const std::string name = it != f.end() ? std::string(trim(it->value)) : std::string{};
    if (name.empty()) return true;
    if (lower(name) != lower(c.name) && isCharacter(name)) return false; // (another's: its lines would be theirs)
    std::vector<Field> fields = f;
    if (const auto image = std::ranges::find(fields, std::string_view("Image"), &Field::name); lower(name) != lower(c.name) && image != fields.end() && trim(image->value).empty()) {
        const std::string was = lower(c.name); // (renamed: the pictures found by its name until now stay its own)
        const auto images = names(Pick::Images);
        if (std::ranges::any_of(images, [&](const std::string& n) { return lower(n) == was || lower(n).starts_with(was + "_"); })) image->value = was;
    }
    const std::string line = formLine(definitionForm("character"), fields);
    if (files_[c.file].lines[c.line] == line) return true;
    remember(key);
    files_[c.file].lines[c.line] = line;
    if (lower(name) != lower(c.name)) renameSpeaker(c.name, name);
    writeAll();
    rescan();
    return true;
}

void StoryEditor::removeCharacter(const Character& c) { eraseLines(c.file, c.line, c.line + 1); }

std::optional<std::size_t> StoryEditor::moveCharacter(std::size_t c, std::size_t to) {
    return moveRecord(characters(), c, to, false, [](const Character& who) { return who.line + 1; }, [&] { return characters(); });
}

// The lines `from` says (and in a chat, what they send, type...), shows and hides of them, and their chats
// and calls now say `to`.
void StoryEditor::renameSpeaker(const std::string& from, const std::string& to) {
    const std::string low = lower(from), word = to.contains(' ') ? "\"" + to + "\"" : to; // (as one word: show "Mr Tanaka")
    auto theirs = [&](const std::string& start) { return start == low || start.starts_with(low + " ") || start.starts_with(low + "("); }; // (Mika, Mika happy, Mika (happy))
    for (File& f : files_)
        for (std::string& line : f.lines) {
            const std::string indent = leading(line);
            const std::string text(trim(line));
            std::string_view prefix;
            if (spoken(text, &prefix)) {
                if (theirs(lower(prefix))) line = indent + to + text.substr(from.size());
            } else if (phoneDoes(text)) { // Mika sends park, Mika (happy) is typing...
                if (theirs(lower(text))) line = indent + to + text.substr(from.size());
            } else if (const std::string verb = lower(firstWord(text)); verb == "show" || verb == "hide") {
                auto words = rawWords(text);
                if (words.size() > 1 && lower(unquote(words[1])) == low) {
                    words[1] = word;
                    line = indent + joined(words);
                }
            } else if (verb == "chat" || (verb == "phone" && lower(firstWord(afterFirst(text))) == "call")) { // chat Mika, phone call Mika [for N seconds]
                std::string_view rest = verb == "chat" ? afterFirst(text) : afterFirst(afterFirst(text));
                const bool colon = rest.ends_with(':');
                if (colon) rest.remove_suffix(1);
                std::string_view who = trim(rest), after;
                if (const auto at = lower(who).rfind(" for "); verb == "phone" && at != npos) after = who.substr(at), who = trim(who.substr(0, at));
                if (lower(unquote(who)) == low) line = indent + (verb == "chat" ? "chat " : "phone call ") + word + std::string(after) + (colon ? ":" : "");
            }
        }
}

} // namespace vn
