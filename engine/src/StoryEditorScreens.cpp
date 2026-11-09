// The editor's screens: the Screens page's screens and parts in the scripts.
#include "Forms.hpp"

#include <climits>
#include <ranges>

namespace vn {

// --- Screens ---------------------------------------------------------------------------------

std::string StoryEditor::Part::option(std::string_view name) const {
    const auto it = std::ranges::find(options, name, &std::pair<std::string, std::string>::first);
    return it != options.end() ? it->second : std::string{};
}

void StoryEditor::Part::set(std::string_view name, const std::string& value) {
    const auto it = std::ranges::find(options, name, &std::pair<std::string, std::string>::first);
    if (value.empty()) {
        if (it != options.end()) options.erase(it);
    } else if (it != options.end()) {
        it->second = value;
    } else {
        options.emplace_back(name, value);
    }
}

StoryEditor::Part StoryEditor::part(const Screen& screen, std::size_t line) const {
    const auto& lines = files_[screen.file].lines;
    Part p{.line = line, .end = line + 1};
    // The parts in it (indented under it), and the ones it's in.
    for (std::size_t i = line + 1; i < screen.end && (!partLine(lines[i]) || indentOf(lines[i]) > indentOf(lines[line])); ++i)
        if (partLine(lines[i])) p.end = i + 1;
    for (std::size_t i = line, in = static_cast<std::size_t>(indentOf(lines[line])); i-- > screen.line + 1;)
        if (partLine(lines[i]) && static_cast<std::size_t>(indentOf(lines[i])) < in) {
            ++p.depth;
            in = static_cast<std::size_t>(indentOf(lines[i]));
        }
    const auto [body, note] = splitNote(lines[line]);
    p.note = note;
    const auto words = rawWords(body);
    if (words.empty()) return p;
    p.kind = lower(words[0]);
    std::size_t i = 1;
    if (i < words.size() && (words[i].starts_with('"') || !words[i].contains('='))) {
        p.quoted = isQuoted(words[i]);
        p.text = unquote(words[i++]);
    }
    for (; i < words.size(); ++i)
        if (const auto eq = words[i].find('='); eq != npos && eq > 0 && !words[i].starts_with('"'))
            p.options.emplace_back(lower(words[i].substr(0, eq)), unquote(words[i].substr(eq + 1)));
        else
            p.words.push_back(words[i]);
    return p;
}

const std::vector<StoryEditor::Part>& StoryEditor::parts(const Screen& screen) const {
    auto& kept = parts_[{screen.file, screen.line}];
    if (kept.first == version_) return kept.second;
    std::vector<Part> out;
    for (std::size_t i = screen.line + 1; i < screen.end; ++i)
        if (partLine(files_[screen.file].lines[i])) out.push_back(part(screen, i));
    kept = {version_, std::move(out)};
    return kept.second;
}

std::string StoryEditor::lineOf(const Part& p) {
    std::string out = p.kind;
    if (p.quoted || !p.text.empty()) out += " " + (p.quoted ? "\"" + p.text + "\"" : p.text);
    for (const auto& [name, value] : p.options)
        if (!value.empty()) out += " " + name + "=" + written(value);
    for (const std::string& word : p.words) out += " " + word;
    if (!p.note.empty()) out += "   " + p.note;
    return out;
}

void StoryEditor::setPart(const Screen& screen, const Part& p, const std::string& key) {
    std::string& line = files_[screen.file].lines[p.line];
    const std::string text = leading(line) + lineOf(p);
    if (line == text) return;
    remember(key);
    line = text;
    write(screen.file);
    rescan();
}

namespace {

// The indentation of a screen's parts that are in no other part.
std::string partIndent(const std::vector<std::string>& lines, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i)
        if (partLine(lines[i])) return leading(lines[i]);
    return std::string(kIndent, ' ');
}

} // namespace

std::size_t StoryEditor::addPart(const Screen& screen, const Part* into, const std::string& text, const std::vector<std::string>& needs) {
    remember({});
    auto& lines = files_[screen.file].lines;
    std::size_t at = into ? into->end : screen.end;
    const std::string indent = into ? leading(lines[into->line]) + std::string(kIndent, ' ') : partIndent(lines, screen.line + 1, screen.end);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), indent + text);
    if (!needs.empty()) { // (the colours and styles it uses that the game lacks: above its screen)
        std::vector<std::string> block = needs;
        block.emplace_back();
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(screen.line), block.begin(), block.end());
        at += block.size();
    }
    write(screen.file);
    rescan();
    return at;
}

std::size_t StoryEditor::addParts(const Screen& screen, const Part* after, const std::vector<std::string>& text) {
    remember({});
    auto& lines = files_[screen.file].lines;
    const std::size_t at = after ? after->end : screen.end;
    const std::string indent = after ? leading(lines[after->line]) : partIndent(lines, screen.line + 1, screen.end);
    int least = INT_MAX; // (the lines' own indentation, under the first's)
    for (const std::string& line : text)
        if (!blank(line)) least = std::min(least, indentOf(line));
    std::size_t n = 0;
    for (const std::string& line : text)
        if (!blank(line))
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at + n++), indent + line.substr(static_cast<std::size_t>(std::min(least, indentOf(line)))));
    write(screen.file);
    rescan();
    return at;
}

std::vector<std::string> StoryEditor::partLines(const Screen& screen, const Part& p) const {
    const auto& lines = files_[screen.file].lines;
    const auto own = static_cast<std::size_t>(indentOf(lines[p.line]));
    std::vector<std::string> out;
    for (std::size_t i = p.line; i < p.end; ++i)
        if (partLine(lines[i])) out.push_back(lines[i].substr(std::min(own, static_cast<std::size_t>(indentOf(lines[i])))));
    return out;
}

std::size_t StoryEditor::pasteScreen(std::vector<std::string> lines) {
    const std::string base(firstWord(afterFirst(lines.front())));
    const std::string name = uniqueName(base, [&](const std::string& n) { return hasScreen(n); });
    const std::string_view settings = afterFirst(afterFirst(lines.front()));
    lines.front() = "screen " + name + (settings.empty() ? "" : " " + std::string(settings));
    return addScreen({}, lines);
}

void StoryEditor::removePart(const Screen& screen, const Part& p) {
    removeParts(screen, {p.line});
}

void StoryEditor::removeParts(const Screen& screen, const std::vector<std::size_t>& lines) {
    if (lines.empty()) return;
    remember({});
    std::vector<std::pair<std::size_t, std::size_t>> blocks; // (each with the parts in it; one in another chosen part goes with that one)
    for (const std::size_t line : lines) {
        const Part p = part(screen, line);
        blocks.emplace_back(p.line, p.end);
    }
    std::ranges::sort(blocks);
    auto& file = files_[screen.file].lines;
    std::size_t covered = 0;
    std::vector<std::pair<std::size_t, std::size_t>> gone;
    for (const auto& [from, to] : blocks)
        if (from >= covered) {
            gone.emplace_back(from, to);
            covered = to;
        }
    for (const auto& [from, to] : gone | std::views::reverse) // (from the bottom, so the others stay where they are)
        file.erase(file.begin() + static_cast<std::ptrdiff_t>(from), file.begin() + static_cast<std::ptrdiff_t>(to));
    write(screen.file);
    rescan();
}

std::optional<std::size_t> StoryEditor::movePart(const Screen& screen, const Part& p, std::size_t to, int depth) {
    const auto all = parts(screen);
    if (to > all.size()) return std::nullopt;
    const std::size_t at = to < all.size() ? all[to].line : screen.end;
    if (at > p.line && at < p.end) return std::nullopt; // (into itself)
    auto& lines = files_[screen.file].lines;
    const std::string base = partIndent(lines, screen.line + 1, screen.end);
    const int was = indentOf(lines[p.line]), now = static_cast<int>(base.size()) + depth * kIndent;
    if ((at == p.line || at == p.end) && was == now) return std::nullopt; // (where it is)
    remember({});
    const std::size_t moved = moveLines(screen.file, p.line, p.end, at, false);
    for (std::size_t i = moved; i < moved + (p.end - p.line); ++i) // (in further, or out: the parts in it too)
        if (!blank(lines[i])) {
            const int indent = std::max(0, indentOf(lines[i]) + now - was);
            lines[i] = std::string(static_cast<std::size_t>(indent), ' ') + std::string(trim(lines[i]));
        }
    write(screen.file);
    rescan();
    return moved;
}

std::size_t StoryEditor::addScreen(const std::vector<std::string>& needs, const std::vector<std::string>& lines) {
    // The screens' file, made (empty, so undo empties it) if it isn't there.
    auto file = std::ranges::find_if(files_, [](const File& f) { return lower(f.path) == "scripts/screens.vnscript"; });
    if (file == files_.end()) {
        files_.push_back({.path = "scripts/screens.vnscript", .lines = {}});
        file = files_.end() - 1;
    }
    const auto f = static_cast<std::size_t>(file - files_.begin());
    remember({});
    auto& to = files_[f].lines;
    if (to.empty()) to.emplace_back("# The game's screens: its menus, and its own textboxes and images.");
    for (const auto* block : {&needs, &lines}) {
        if (block->empty()) continue;
        if (!blank(to.back())) to.emplace_back();
        to.insert(to.end(), block->begin(), block->end());
    }
    write(f);
    include("screens"); // (main.vnscript reads it)
    rescan();
    const std::string name(firstWord(afterFirst(lines.front())));
    // A title screen shows when the story opens it: at the start of scene start, unless it does already.
    if (const auto start = std::ranges::find(scenes_, std::string("start"), &Scene::name); lower(name) == "title" && start != scenes_.end()) {
        auto& in = files_[start->file].lines;
        const auto opens = [](const std::string& l) { return lower(trim(l)) == "open title"; };
        if (!std::any_of(in.begin() + static_cast<std::ptrdiff_t>(start->line), in.begin() + static_cast<std::ptrdiff_t>(start->end), opens)) {
            std::string indent(kIndent, ' ');
            for (std::size_t i = start->line + 1; i < start->end; ++i)
                if (!blank(in[i])) {
                    indent = leading(in[i]);
                    break;
                }
            in.insert(in.begin() + static_cast<std::ptrdiff_t>(start->line) + 1, indent + "open title");
            write(start->file);
            rescan();
        }
    }
    for (std::size_t i = screens_.size(); i-- > 0;)
        if (lower(screens_[i].name) == lower(name)) return i;
    return 0;
}

std::optional<std::size_t> StoryEditor::moveScreen(std::size_t screen, std::size_t to) {
    return moveRecord(screens_, screen, to, true, [](const Screen& s) { return s.end; }, [&] { return screens_; });
}

bool StoryEditor::renameScreen(std::size_t screen, const std::string& name, const std::string& key) {
    typedOn(key);
    const std::string from = screens_[screen].name;
    if (lower(name) == lower(from)) return true;
    if (!validName(name) || hasScreen(name)) return false;
    remember(key);
    const Screen s = screens_[screen];
    Part header = part(s, s.line);
    header.text = name;
    files_[s.file].lines[s.line] = lineOf(header);
    changeLines([&](std::size_t f, std::size_t i, const std::string& line) -> std::optional<std::string> {
        if (f == s.file && i == s.line) return std::nullopt;
        std::string after = screenRenamed(line, from, name);
        return after != line ? std::optional(std::move(after)) : std::nullopt;
    }, s.file);
    rescan();
    return true;
}

void StoryEditor::removeScreen(std::size_t screen) {
    remember({});
    const Screen s = screens_[screen];
    auto& lines = files_[s.file].lines;
    const std::size_t from = s.line > 0 && blank(lines[s.line - 1]) ? s.line - 1 : s.line;
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(s.end));
    // The keys and buttons that only opened it go too (a key opening a screen that isn't there is a mistake).
    const std::string opens = "open " + lower(s.name);
    for (std::size_t f = 0; f < files_.size(); ++f) {
        const auto before = files_[f].lines.size();
        std::erase_if(files_[f].lines, [&](const std::string& line) {
            const auto words = rawWords(splitNote(line).first);
            if (words.empty()) return false;
            const std::string kind = lower(words[0]);
            if (!(indentOf(line) == 0 ? kind == "control" : kind == "button")) return false;
            return std::ranges::any_of(words, [&](const std::string& w) { return lower(w).starts_with("action=") && lower(unquote(w.substr(7))) == opens; });
        });
        if (f == s.file || files_[f].lines.size() != before) write(f);
    }
    rescan();
}

std::vector<std::string> StoryEditor::defined(std::string_view what) const {
    std::vector<std::string> out;
    for (const File& f : files_)
        for (const std::string& line : f.lines)
            if (indentOf(line) == 0 && lower(firstWord(line)) == what)
                if (const std::string name(firstWord(afterFirst(line))); !name.empty() && !std::ranges::contains(out, name)) out.push_back(name);
    return out;
}

bool StoryEditor::defines(std::string_view what, std::string_view name) const {
    for (const File& f : files_)
        for (const std::string& line : f.lines)
            if (indentOf(line) == 0 && lower(firstWord(line)) == what && lower(firstWord(afterFirst(line))) == lower(name)) return true;
    return false;
}

} // namespace vn
