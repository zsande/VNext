// The editor's Library: what the scripts list by name, animations' timelines, and the game's settings.
#include "Forms.hpp"

namespace vn {

namespace {

// Seconds as an animation's lines write them: "1 second", "0.5 seconds".
std::string secondsWords(float seconds) {
    const std::string n = secondsText(seconds);
    return n + (n == "1" ? " second" : " seconds");
}

} // namespace

std::size_t StoryEditor::timelineRow(std::string_view key) {
    const auto it = std::ranges::find_if(kTimelineRows, [&](const TimelineRow& row) { return std::ranges::contains(wordsOf(row.keys), key); });
    return static_cast<std::size_t>(it - std::begin(kTimelineRows));
}

StoryEditor::Timeline StoryEditor::timelineOf(std::span<const std::string> steps) {
    Timeline t;
    float ended = 0.0f; // (where the line before ended)
    for (const std::string& line : steps) {
        const std::string_view text = trim(splitNote(line).first);
        if (text.empty()) continue;
        const auto w = rawWords(text);
        std::vector<Bar> bars;
        std::optional<float> start, seconds;
        std::string ease, shape;
        auto number = [&](std::size_t& i) -> std::optional<float> { // (the number at i, and "seconds" after it)
            const auto n = i < w.size() ? parseNumber(w[i]) : std::nullopt;
            if (!n) return std::nullopt;
            if (i + 1 < w.size() && (lower(w[i + 1]) == "seconds" || lower(w[i + 1]) == "second")) ++i;
            return std::max(0.0f, *n);
        };
        if (lower(w.front()) == "wait") {
            std::size_t i = 1;
            const auto n = number(i);
            if (!n || i + 1 != w.size()) t.ok = false;
            else ended += *n, t.length = std::max(t.length, ended);
            continue;
        }
        for (std::size_t i = 0; i < w.size(); ++i) {
            const std::string word = lower(w[i]);
            if (word == "over" || word == "for") {
                if (const auto n = number(++i)) seconds = *n;
                else t.ok = false;
            } else if (word == "from") {
                const auto a = number(++i);
                const auto b = a && i + 1 < w.size() && lower(w[i + 1]) == "to" ? number(i += 2) : std::nullopt;
                if (!a || !b || *b < *a) t.ok = false;
                else start = *a, seconds = *b - *a;
            } else if (word == "at") {
                if (const auto n = number(++i)) start = *n, seconds = 0.0f;
                else t.ok = false;
            } else if (word.starts_with("ease=")) {
                ease = word.substr(5);
            } else if (word.starts_with("shape=")) { // (the line's wipe's)
                shape = unquote(std::string_view(w[i]).substr(6));
            } else if (const auto eq = word.find('='); eq != npos && eq > 0 && timelineRow(word.substr(0, eq)) < kTimelineRowCount) {
                bars.push_back({.key = word.substr(0, eq), .value = unquote(std::string_view(w[i]).substr(eq + 1))});
            } else {
                t.ok = false;
            }
        }
        const auto wipe = std::ranges::find(bars, std::string("wipe"), &Bar::key);
        if (bars.empty() || (!shape.empty() && wipe == bars.end())) {
            t.ok = false;
            continue;
        }
        if (!shape.empty()) wipe->shape = shape;
        for (Bar& b : bars) {
            b.start = start.value_or(ended);
            b.seconds = seconds.value_or(0.0f);
            b.ease = ease.empty() ? std::string(usualEase(b.key)) : ease;
            t.bars.push_back(std::move(b));
        }
        ended = start.value_or(ended) + seconds.value_or(0.0f);
        t.length = std::max(t.length, ended);
    }
    std::ranges::stable_sort(t.bars, {}, &Bar::start);
    return t;
}

std::vector<std::string> StoryEditor::stepsOf(const Timeline& timeline) {
    std::vector<Bar> bars = timeline.bars; // (in the order they begin, then end; together on a line when they begin and end together, eased alike)
    std::ranges::stable_sort(bars, [](const Bar& a, const Bar& b) {
        if (!sameMoment(a.start, b.start)) return a.start < b.start;
        if (!sameMoment(a.seconds, b.seconds)) return a.seconds < b.seconds;
        if (a.ease != b.ease) return a.ease < b.ease;
        return timelineRow(a.key) < timelineRow(b.key);
    });
    std::vector<std::string> out;
    float ended = 0.0f, last = 0.0f; // (where the line before ended; where the last change ends)
    for (std::size_t i = 0; i < bars.size();) {
        const Bar& b = bars[i];
        std::string changes;
        bool usual = true;
        std::size_t j = i;
        for (; j < bars.size() && sameMoment(bars[j].start, b.start) && sameMoment(bars[j].seconds, b.seconds) && bars[j].ease == b.ease; ++j) {
            changes += std::format("{}{}={}", changes.empty() ? "" : " ", bars[j].key, written(bars[j].value));
            if (!bars[j].shape.empty()) changes += " shape=" + written(bars[j].shape);
            usual = usual && bars[j].ease == usualEase(bars[j].key);
        }
        std::string timing;
        if (sameMoment(b.start, ended)) timing = b.seconds > 0.0f ? " over " + secondsWords(b.seconds) : "";
        else if (b.seconds <= 0.0f) timing = " at " + secondsWords(b.start);
        else timing = std::format(" from {} to {}", secondsText(b.start), secondsWords(b.start + b.seconds));
        out.push_back(changes + timing + (usual ? "" : " ease=" + b.ease));
        ended = b.start + b.seconds;
        last = std::max(last, ended);
        i = j;
    }
    if (timeline.length > last + 0.005f) out.push_back("wait " + secondsWords(timeline.length - ended)); // (time after the last change, before it ends)
    return out;
}

// --- Things listed by name ---------------------------------------------------------------------

const std::vector<StoryEditor::Definition>& StoryEditor::definitions(std::string_view kind) const {
    auto& kept = definitions_[std::string(kind)];
    if (kept.first == version_) return kept.second;
    std::vector<Definition> out; // (each file's, as namesIn found them: a file looked through again only when it changed)
    for (std::size_t f = 0; f < files_.size(); ++f)
        if (const auto& in = namesIn(f).definitions; in.contains(kind)) std::ranges::copy(in.find(kind)->second, std::back_inserter(out));
    kept = {version_, std::move(out)};
    return kept.second;
}

std::string StoryEditor::endingOf(std::string_view scene) const {
    for (const Definition& d : definitions("ending"))
        if (sameName(fieldValue(fields("ending", d), "Scene"), scene)) return d.name;
    return {};
}

void StoryEditor::setEnding(std::string_view scene, const std::string& ending, const std::string& key) {
    const auto all = definitions("ending"); // (as they are now: changing one doesn't move the others)
    for (const Definition& d : all) {
        auto f = fields("ending", d);
        const auto at = std::ranges::find(f, std::string_view("Scene"), &Field::name);
        if (at == f.end()) continue;
        const bool reached = sameName(d.name, ending), was = sameName(at->value, scene);
        if (reached == was) continue;
        at->value = reached ? std::string(scene) : std::string{};
        setDefinition(d, linesOf("ending", f), key);
    }
}

std::vector<std::string> StoryEditor::linesOf(const Definition& d) const {
    std::vector<std::string> out;
    for (std::size_t i = d.line; i < d.end; ++i)
        if (!blank(files_[d.file].lines[i])) out.emplace_back(trim(files_[d.file].lines[i]));
    return out;
}

std::size_t StoryEditor::addDefinition(std::string_view kind, const std::vector<std::string>& lines) {
    std::vector<std::string> put;
    put.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) put.push_back((i > 0 ? std::string(kIndent, ' ') : std::string{}) + lines[i]);
    return putDefinition(kind, nullptr, std::move(put));
}

std::size_t StoryEditor::pasteDefinition(std::string_view kind, const Definition* after, const std::vector<std::string>& lines) {
    std::vector<std::string> put = unshared(lines);
    while (!put.empty() && blank(put.back())) put.pop_back();
    const std::string_view head = kind == "sound type" ? std::string_view("type") : kind; // (a sound type's line starts as a visual type's)
    if (put.empty() || lower(firstWord(put.front())) != head || rawWords(put.front()).size() < 2 || (head == "type" && soundTypeLine(put.front()) != (kind == "sound type")))
        return definitions(kind).size();
    const auto all = definitions(kind);
    std::string base = unquote(rawWords(put.front())[1]);
    std::string_view rest = afterFirst(afterFirst(put.front()));
    if (kind == "font" && (base.contains('/') || base.contains('.'))) { // (font FILE, the default font: its copy gets a name)
        rest = afterFirst(put.front());
        base = "default";
    }
    const std::string name = uniqueName(base, [&](const std::string& n) { return hasNamed(all, n) || (head == "type" && typeNameTaken(n)); });
    put.front() = std::format("{} {}{}", head, name, rest.empty() ? "" : " " + std::string(rest));
    if (kind == "atlas" && lower(name) != lower(base) && std::ranges::none_of(rawWords(rest), [](const std::string& w) { return lower(w).starts_with("image="); }))
        put.front() += " image=" + base; // (an atlas named as its image: the copy cuts that image too)
    return putDefinition(kind, after, std::move(put));
}

// A definition's lines (indented as they go in the file) after `after` (none: after the last of its kind, else in
// main.vnscript before its first scene and its notes, or at its end): its index.
std::size_t StoryEditor::putDefinition(std::string_view kind, const Definition* after, std::vector<std::string> put) {
    const auto existing = definitions(kind);
    if (!after && !existing.empty()) after = &existing.back();
    remember({});
    std::size_t file = 0, at = 0;
    if (after) {
        file = after->file;
        at = after->end;
    } else {
        auto& main = files_[0].lines;
        at = main.size();
        for (std::size_t i = 0; i < main.size(); ++i)
            if (indentOf(main[i]) == 0 && lower(firstWord(main[i])) == "scene") {
                at = i;
                while (at > 0 && trim(main[at - 1]).starts_with('#')) --at;
                break;
            }
    }
    auto& to = files_[file].lines;
    if (!after && at < to.size() && !blank(to[at])) put.emplace_back();
    to.insert(to.begin() + static_cast<std::ptrdiff_t>(at), put.begin(), put.end());
    write(file);
    rescan();
    const auto now = definitions(kind);
    for (std::size_t i = 0; i < now.size(); ++i)
        if (now[i].file == file && now[i].line == at) return i;
    return 0;
}

void StoryEditor::setDefinition(const Definition& d, const std::vector<std::string>& lines, const std::string& key) {
    if (lines.empty() || trim(afterFirst(trim(lines.front()))).empty()) return; // (its name emptied in the form: it keeps the one it has)
    auto& file = files_[d.file].lines;
    std::vector<std::string> put;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (i == 0 || !blank(lines[i])) put.push_back((i > 0 ? std::string(kIndent, ' ') : std::string{}) + std::string(trim(lines[i])));
    if (std::equal(file.begin() + static_cast<std::ptrdiff_t>(d.line), file.begin() + static_cast<std::ptrdiff_t>(d.end), put.begin(), put.end())) return;
    remember(key);
    file.erase(file.begin() + static_cast<std::ptrdiff_t>(d.line), file.begin() + static_cast<std::ptrdiff_t>(d.end));
    file.insert(file.begin() + static_cast<std::ptrdiff_t>(d.line), put.begin(), put.end());
    write(d.file);
    rescan();
}

void StoryEditor::removeDefinition(const Definition& d) { eraseLines(d.file, d.line, d.end); }

std::optional<std::size_t> StoryEditor::moveDefinition(std::string_view kind, std::size_t index, std::size_t to) {
    return moveRecord(definitions(kind), index, to, false, [](const Definition& d) { return d.end; }, [&] { return definitions(kind); });
}

bool StoryEditor::renameDefinition(std::string_view kind, std::size_t index, const std::string& name, const std::string& key) {
    typedOn(key);
    const auto all = definitions(kind);
    const Definition& d = all[index];
    if (sameName(name, d.name)) return true;
    const bool kept = kind == "ending" || kind == "achievement" || kind == "tip"; // (its name in what keeps it: persistent.ending.NAME)
    const bool type = kind == "type" || kind == "sound type";
    if (!validName(name) || (kept && name.contains('-')) || (kind == "font" && d.name == "default") || hasNamed(all, name) ||
        (type && typeNameTaken(name)) || // (a type every game has, or one of the other kind's)
        (kind == "sound" && (isSoundType(name) || lower(name) == "weather" || lower(name) == "effect")) || // (what stop stops by: one, not both)
        (kind == "sound type" && (hasNamed(definitions("sound"), name) || lower(name) == "weather" || lower(name) == "effect")))
        return false;
    remember(key);
    { // its own line (a named visual or sound showing or playing the file named as it: that file still, said)
        std::string& line = files_[d.file].lines[d.line];
        auto w = rawWords(line);
        if (w.size() > 1) w[1] = name;
        const std::string_view file = kind == "visual" ? "shows=" : kind == "sound" ? "plays=" : "";
        if (!file.empty() && std::ranges::none_of(w, [&](const std::string& word) { return lower(word).starts_with(file); })) w.push_back(std::string(file) + d.name);
        line = joined(w);
    }
    // Where it's used: settings that name it (style=, based_on=, ..._style=; colours; font=; same_names_as=,
    // image=), places (at NAME), visual types (show ... as NAME), keys (inputs for controls' keys), what keeps it (persistent.ending.NAME);
    // a colour wherever a colour goes (an image part, a background, a gradient's side, a colour setting, {colour NAME}).
    const std::string from = lower(d.name);
    auto names = [&](std::string_view setting) {
        if (kind == "style") return setting == "style" || setting == "based_on" || setting.ends_with("_style");
        if (kind == "colour")
            return setting.contains("colour") || setting.contains("background") || setting.contains("border") || setting.contains("tint") || setting == "bottom" ||
                   setting == "fill" || setting == "track" || setting == "knob" || setting == "line";
        if (kind == "font") return setting == "font";
        if (kind == "atlas") return setting == "same_names_as" || setting == "image";
        if (kind == "animation") return setting == "animation" || setting.ends_with("_animation") || setting == "animation_on_show"; // (a part's, a click's, a screen's; a visual's or a character's as it comes)
        return false;
    };
    const auto bases = kind == "sub-image" ? imageBases() : std::vector<std::pair<std::string, std::string>>{}; // (a sub-image is used like an image file)
    // A value with it renamed, if it names it: a colour's on either side of a gradient too (navy-black).
    auto renamedValue = [&](std::string_view written) -> std::optional<std::string> {
        const std::string v = unquote(written);
        if (lower(v) == from) return name;
        if (const auto dash = v.find('-', 1); kind == "colour" && dash != npos) {
            const std::string top = v.substr(0, dash), bottom = v.substr(dash + 1);
            if (lower(top) == from || lower(bottom) == from) return (lower(top) == from ? name : top) + "-" + (lower(bottom) == from ? name : bottom);
        }
        return std::nullopt;
    };
    changeLines([&](std::size_t f, std::size_t i, const std::string& line) -> std::optional<std::string> {
        if (f == d.file && i == d.line) return std::nullopt;
        const std::string indent = leading(line);
        auto w = rawWords(line);
        bool changed = false;
        // (a colour also as an image part's, a background's, a colour box's, a colour line's or a colour setting's value: image
        // panel, show navy-black as background, show leaf as panel, colour accent2 accent, setting narration_colour sky)
        const std::string head = w.empty() ? std::string{} : lower(w[0]);
        auto colourBox = [&] { // (show NAME as TYPE, TYPE taking colours)
            for (std::size_t j = 1; j + 1 < w.size(); ++j)
                if (lower(w[j]) == "as") return std::ranges::contains(wordsOf(typeAccepts(unquote(w[j + 1]))), std::string_view("colour"));
            return false;
        };
        const std::size_t valueAt = kind != "colour"                                                                                          ? npos
                                  : (head == "image" && indentOf(line) > 0) || (head == "show" && (StoryEditor::asBackground(line) || colourBox())) ? 1
                                  : head == "colour" && indentOf(line) == 0                                                                   ? 2
                                  : head == "setting" && w.size() > 1 && (lower(w[1]) == "narration_colour" || lower(w[1]) == "letterbox_colour") ? 2
                                                                                                                                              : npos;
        // (and a visual line's or a character line's place, at=; a visual line's type, type=, and what it shows, shows=, a colour)
        auto namesHere = [&](std::string_view setting) {
            return names(setting) || (kind == "place" && (head == "visual" || head == "character") && setting == "at") ||
                   (kind == "type" && (head == "visual" || (head == "image" && indentOf(line) > 0)) && setting == "type") || // (a screen's visual's too)
                   (kind == "sound type" && head == "sound" && setting == "type") || (kind == "control" && head == "input" && setting == "control") || // (an input's key)
                   (kind == "colour" && head == "visual" && setting == "shows");
        };
        for (std::size_t j = 0; j < w.size(); ++j) {
            const auto eq = w[j].find('=');
            if (const std::string option = eq != npos && eq > 0 ? lower(w[j].substr(0, eq)) : std::string{};
                kind == "sound type" && (option == "stop_on_open" || option == "stop_on_close")) { // (a screen's: sound types, with commas between them)
                std::string list;
                bool hit = false;
                const std::string was = unquote(std::string_view(w[j]).substr(eq + 1));
                for (const std::string_view part : wordsOf(was, ',')) {
                    std::string one(trim(part));
                    if (lower(one) == from) one = name, hit = true;
                    list += (list.empty() ? "" : ",") + one;
                }
                if (hit) {
                    w[j] = w[j].substr(0, eq + 1) + list;
                    changed = true;
                }
            } else if (eq != npos && eq > 0 && namesHere(lower(w[j].substr(0, eq)))) {
                if (const auto now = renamedValue(std::string_view(w[j]).substr(eq + 1))) {
                    w[j] = w[j].substr(0, eq + 1) + *now;
                    changed = true;
                }
            } else if (j == valueAt) {
                if (const auto now = renamedValue(w[j])) {
                    w[j] = isQuoted(w[j]) ? "\"" + *now + "\"" : *now;
                    changed = true;
                }
            } else if (j > 0 && lower(w[j]) == from &&
                       ((kind == "place" && lower(w[j - 1]) == "at") || (kind == "type" && lower(w[j - 1]) == "as" && lower(w[0]) == "show") ||
                        (kind == "visual" && j == 1 && (lower(w[0]) == "show" || lower(w[0]) == "hide")))) {
                w[j] = name;
                changed = true;
            }
        }
        std::string now = changed ? indent + joined(w) : line;
        if (kind == "colour") { // {colour NAME}words{/colour}
            const std::string tag = "{colour " + from + "}";
            for (std::size_t at = lower(now).find(tag); at != npos; at = lower(now).find(tag, at + 1)) now.replace(at, tag.size(), "{colour " + name + "}");
        }
        if (kept) now = lineWithVariable(now, std::format("persistent.{}.{}", kind, from), std::format("persistent.{}.{}", kind, lower(name)));
        if (kind == "sub-image" || kind == "visual") now = assetRenamed(now, "images/", d.name, name, bases); // (wherever a visual goes: a screen's, a style's, the gallery...)
        if (kind == "sound") now = assetRenamed(now, "sounds/", d.name, name, {}); // (wherever a sound goes: a line's voice, a button's click, the music room...)
        if (kind == "sound type") now = soundRenamed(now, from, name, true); // (played as it, stopped)
        if (kind == "type") now = visualRenamed(now, from, name, true);       // (shown as it: an action's too)
        if (kind == "animation") now = animationRenamed(now, from, name);
        if (kind == "tip") { // {tip NAME}words{/tip}
            const std::string tag = "{tip " + from + "}";
            for (std::size_t at = lower(now).find(tag); at != npos; at = lower(now).find(tag, at + 1)) now.replace(at, tag.size(), "{tip " + name + "}");
        }
        return now != line ? std::optional(std::move(now)) : std::nullopt;
    }, d.file);
    rescan();
    return true;
}

// --- The game's settings ---------------------------------------------------------------------

bool StoryEditor::hasMain() const {
    return !files_.empty() && lower(files_[0].path) == "scripts/main.vnscript";
}

std::optional<std::size_t> StoryEditor::settingLine(std::string_view name) const {
    if (!hasMain()) return std::nullopt;
    if (settingLines_.first != version_) { // (each setting's line, the last one counting: found once a version)
        settingLines_ = {version_, {}};
        for (std::size_t i = 0; i < files_[0].lines.size(); ++i)
            if (const auto c = settingOf(files_[0].lines[i])) settingLines_.second.insert_or_assign(c->first, i);
    }
    const auto it = settingLines_.second.find(name);
    return it != settingLines_.second.end() ? std::optional(it->second) : std::nullopt;
}

bool StoryEditor::isSetting(std::size_t line) const {
    return hasMain() && line < files_[0].lines.size() && settingOf(files_[0].lines[line]);
}

std::optional<std::string> StoryEditor::setting(std::string_view name) const {
    const auto line = settingLine(name);
    if (!line) return std::nullopt;
    const auto parsed = settingOf(splitNote(files_[0].lines[*line]).first); // (not its note)
    if (!parsed) return std::nullopt;
    std::vector<std::string> words;
    for (const std::string& word : rawWords(parsed->second)) words.push_back(unquote(word));
    return joined(words);
}

void StoryEditor::setSetting(std::string_view name, const std::string& value, bool quote, const std::string& key) {
    if (!hasMain()) return;
    auto& lines = files_[0].lines;
    const auto at = settingLine(name);
    const std::string v(trim(value));
    const std::string_view note = at ? splitNote(lines[*at]).second : std::string_view{}; // (kept at its end)
    const std::string line = std::format("setting {} {}", name, quote ? "\"" + v + "\"" : v) + (note.empty() ? "" : "  " + std::string(note));
    if (at ? lines[*at] == line : v.empty()) return;
    remember(key);
    if (v.empty()) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*at));
        removed_ = {std::string(name), *at};
    } else if (at) {
        lines[*at] = line;
    } else if (removed_.first == name && removed_.second <= lines.size()) { // (a box emptied to type in it again)
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(removed_.second), line);
    } else { // after the other setting lines, or the notes at the top
        std::optional<std::size_t> last;
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (settingOf(lines[i])) last = i;
        std::size_t to = 0;
        if (last) to = *last + 1;
        else
            while (to < lines.size() && trim(lines[to]).starts_with('#')) ++to;
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(to), line);
        if (!last && to + 1 < lines.size() && !blank(lines[to + 1])) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(to + 1), "");
    }
    write(0);
    rescan();
}

} // namespace vn
