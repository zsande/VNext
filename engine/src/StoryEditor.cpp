// The editor (StoryEditor): the scripts as the pages show them, read: scenes, lines, characters, names, the phone's
// lines.
#include "Forms.hpp"

#include <cmath>
#include <iterator>
#include <ranges>
#include <set>
#include <tuple>

namespace vn {

namespace {

StoryEditor::Kind classify(std::string_view t, StoryEditor::Kind parent) {
    using Kind = StoryEditor::Kind;
    if (parent == Kind::Choice) return Kind::Option; // (whatever its words: the lines under them are options)
    if (parent == Kind::Call && (lower(trim(t)) == "missed:" || lower(trim(t)) == "missed")) return Kind::Missed;
    const std::string command = storyCommandOf(t);
    if (!command.empty() && startsWithCapital(t)) return Kind::Other; // ("Show me": text, or a command)
    const std::string second = lower(firstWord(afterFirst(t)));
    if (command == "phone" && second == "call") return Kind::Call;
    if (command.empty() && phoneDoes(t)) return Kind::Message; // (a phone line that sends something, types...)
    if ((command == "show" || command == "hide") && second == "screen") return Kind::Screen;
    if (command == "hide" && second == "ui") return Kind::Other;
    if (command == "show" || command == "hide") return Kind::Visual; // (a character, an image or a movie: a visual)
    if (command == "stop") return second == "weather" ? Kind::Weather : second == "effect" ? Kind::Effect : Kind::Sound; // (what it stops: the weather, the effects, a sound)
    if (command == "play") return Kind::Sound;
    static constexpr std::pair<std::string_view, Kind> kCommandKinds[]{ // (the plain ones: a word, its kind)
        {"choice", Kind::Choice},     {"if", Kind::If},             {"else", Kind::Else},         {"set", Kind::Set},
        {"jump", Kind::Jump},
        {"wait", Kind::Wait},         {"time", Kind::Time},         {"effect", Kind::Effect},     {"weather", Kind::Weather},
        {"camera", Kind::Camera},     {"open", Kind::Screen},       {"together", Kind::Together},
        {"chat", Kind::Chat},
    };
    if (const auto it = std::ranges::find(kCommandKinds, std::string_view(command), &std::pair<std::string_view, Kind>::first); it != std::end(kCommandKinds))
        return it->second;
    if (!command.empty() || !unknownCommand(t).empty()) return Kind::Other; // (a lower-case word that isn't a command: a mistake, as the engine has it)
    if (parent == Kind::Chat && !isQuoted(trim(t))) return Kind::Message; // (in a chat, someone's words are a message, as the engine has it)
    return Kind::Line; // someone speaking, or narration
}

} // namespace

void StoryEditor::load(const std::string& root) {
    const std::size_t version = version_, stamps = stamps_; // (they only go up: what's made from the scripts is made again)
    clear();
    version_ = version + 1;
    stamps_ = stamps;
    root_ = root;
    auto names = listFiles(root + "scripts/", ".vnscript");
    std::ranges::stable_partition(names, [](const std::string& n) { return lower(n) == "main.vnscript"; });
    for (const auto& name : names) {
        if (name.contains('/')) continue;
        File f{.path = "scripts/" + name, .lines = {}};
        if (const auto text = readTextFile(root + f.path)) {
            f.lines = vn::linesOf(*text);
            if (f.lines.size() > 1 && f.lines.back().empty()) f.lines.pop_back(); // (the file's last line break)
        }
        if (SDL_PathInfo info; SDL_GetPathInfo((root + f.path).c_str(), &info)) f.modified = info.modify_time;
        f.stamp = ++stamps_;
        files_.push_back(std::move(f));
    }
    if (const auto map = readTextFile(root + ".map.txt"))
        for (auto&& part : std::views::split(std::string_view(*map), '\n'))
            if (const auto w = rawWords(std::string_view(part.begin(), part.end())); w.size() == 3)
                if (const auto x = parseNumber(w[1]), y = parseNumber(w[2]); x && y) mapPlaces_.insert_or_assign(lower(w[0]), SDL_FPoint{*x, *y});
    rescan();
}

std::optional<SDL_FPoint> StoryEditor::mapPlace(std::string_view scene) const {
    const auto it = mapPlaces_.find(lower(scene));
    return it != mapPlaces_.end() ? std::optional(it->second) : std::nullopt;
}

void StoryEditor::setMapPlace(const std::string& scene, std::optional<SDL_FPoint> at) {
    if (at) mapPlaces_.insert_or_assign(lower(scene), *at);
    else mapPlaces_.erase(lower(scene));
    saveMapPlaces();
}

void StoryEditor::saveMapPlaces() const {
    std::string text;
    for (const auto& [name, at] : mapPlaces_) text += std::format("{} {} {}\n", name, std::lround(at.x), std::lround(at.y));
    if (text.empty()) SDL_RemovePath((root_ + ".map.txt").c_str());
    else SDL_SaveFile((root_ + ".map.txt").c_str(), text.data(), text.size());
}

void StoryEditor::rescan() {
    scenes_.clear();
    screens_.clear();
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i) {
            const std::string& line = files_[f].lines[i];
            if (indentOf(line) != 0) continue;
            const std::string first = lower(firstWord(line));
            if (first == "scene") {
                std::string name(afterFirst(line));
                if (name.ends_with(':')) name.pop_back();
                scenes_.push_back({.file = f, .line = i, .end = blockEnd(f, i), .name = name});
            } else if (first == "screen") {
                screens_.push_back({.file = f, .line = i, .end = blockEnd(f, i), .name = std::string(firstWord(afterFirst(line)))});
            }
        }
    std::set<std::pair<std::size_t, std::size_t>> starts; // (the rows kept for lines that start no scene now go)
    for (const Scene& scene : scenes_) starts.emplace(scene.file, scene.line);
    std::erase_if(rows_, [&](const auto& kept) { return !starts.contains(kept.first); });
}

std::size_t StoryEditor::blockEnd(std::size_t file, std::size_t label) const {
    const auto& lines = files_[file].lines;
    std::size_t end = label + 1;
    while (end < lines.size() && (blank(lines[end]) || indentOf(lines[end]) > 0)) ++end;
    while (end > label + 1 && blank(lines[end - 1])) --end;
    return end;
}

std::string_view StoryEditor::phoneSceneOf(const Scene& scene) const {
    if (phoneScenes_.first != version_) { // (the characters' lines read once a version: the first to name a scene has it)
        std::map<std::string, std::string_view, std::less<>> found;
        for (const Character& c : characters())
            for (const std::string& word : rawWords(files_[c.file].lines[c.line]))
                if (const std::string w = lower(word); w.starts_with("chat=") || w.starts_with("call="))
                    found.try_emplace(lower(unquote(w.substr(5))), w.starts_with("chat=") ? "chat" : "call");
        phoneScenes_ = {version_, std::move(found)};
    }
    const auto it = phoneScenes_.second.find(lower(scene.name));
    return it != phoneScenes_.second.end() ? it->second : std::string_view{};
}

const std::vector<StoryEditor::Row>& StoryEditor::rows(const Scene& scene) const {
    auto& kept = rows_[{scene.file, scene.line}];
    const Kind top = phoneSceneOf(scene) == "chat" ? Kind::Chat : Kind::Other; // (a character's chat= scene: its lines are a chat's)
    if (kept.stamp == files_[scene.file].stamp && kept.top == top) return kept.rows; // (its file as it was: another one changed)
    std::vector<Row> out;
    const auto& lines = files_[scene.file].lines;
    const std::size_t end = blockEnd(scene.file, scene.line);
    std::vector<std::pair<int, std::size_t>> open; // the rows the next ones may be inside: indentation, row
    for (std::size_t i = scene.line + 1; i < end; ++i) {
        if (blank(lines[i]) || trim(lines[i]).starts_with('#')) continue; // (a comment: for whoever reads the script, not a row)
        const int indent = indentOf(lines[i]);
        while (!open.empty() && indent <= open.back().first) open.pop_back();
        Row row{.line = i, .end = end, .depth = static_cast<int>(open.size()), .kind = Kind::Other, .text = std::string(trim(lines[i]))};
        row.kind = classify(row.text, open.empty() ? top : out[open.back().second].kind);
        open.emplace_back(indent, out.size());
        out.push_back(std::move(row));
    }
    for (std::size_t k = 0; k < out.size(); ++k)
        for (std::size_t j = k + 1; j < out.size(); ++j)
            if (out[j].depth <= out[k].depth) {
                out[k].end = out[j].line;
                break;
            }
    kept = {.stamp = files_[scene.file].stamp, .top = top, .rows = std::move(out)};
    return kept.rows;
}

std::vector<std::pair<std::string, std::string>> StoryEditor::loveInterests() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Character& c : characters())
        if (std::ranges::any_of(rawWords(afterFirst(files_[c.file].lines[c.line])), [](const std::string& w) { return lower(w).starts_with("love="); }))
            out.emplace_back(c.name, lower(loveVariable(c.name)));
    return out;
}

namespace {

// What a set line adds to `variable` (set mika.love to mika.love + 2: 2, - 1: -1), if that's all it does.
std::optional<int> addsTo(std::string_view text, std::string_view variable) {
    const auto w = rawWords(text);
    if (w.size() != 6 || lower(w[0]) != "set" || lower(w[1]) != variable || lower(w[2]) != "to" || lower(w[3]) != variable || (w[4] != "+" && w[4] != "-")) return std::nullopt;
    const auto n = parseNumber(w[5]);
    if (!n) return std::nullopt;
    return static_cast<int>(std::lround(*n)) * (w[4] == "-" ? -1 : 1);
}

} // namespace

int StoryEditor::loveIn(const Scene& scene, const Row& option, std::string_view variable) const {
    for (const Row& r : rows(scene))
        if (r.line > option.line && r.line < option.end && r.depth == option.depth + 1)
            if (const auto n = addsTo(r.text, variable)) return *n;
    return 0;
}

void StoryEditor::setLove(const Scene& scene, const Row& option, const std::string& variable, int amount, const std::string& key) {
    auto& file = files_[scene.file].lines;
    std::optional<std::size_t> at; // (its set line, and the first line in it)
    std::optional<std::size_t> first;
    for (const Row& r : rows(scene))
        if (r.line > option.line && r.line < option.end && r.depth == option.depth + 1) {
            if (!first) first = r.line;
            if (!at && addsTo(r.text, variable)) at = r.line;
        }
    const std::string text = amount == 0 ? std::string{} : std::format("set {0} to {0} {1} {2}", variable, amount < 0 ? '-' : '+', std::abs(amount));
    if (at ? trim(file[*at]) == text : text.empty()) return;
    remember(key);
    if (!at) file.insert(file.begin() + static_cast<std::ptrdiff_t>(option.line + 1), (first ? leading(file[*first]) : leading(file[option.line]) + std::string(kIndent, ' ')) + text);
    else if (text.empty()) file.erase(file.begin() + static_cast<std::ptrdiff_t>(*at));
    else file[*at] = leading(file[*at]) + text;
    write(scene.file);
    rescan();
}

const std::vector<StoryEditor::Character>& StoryEditor::characters() const {
    if (characters_.first == version_) return characters_.second;
    std::vector<Character> out;
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i)
            if (const std::string& line = files_[f].lines[i]; indentOf(line) == 0 && lower(firstWord(line)) == "character")
                if (const auto words = rawWords(afterFirst(line)); !words.empty()) out.push_back({.file = f, .line = i, .name = unquote(words[0])});
    characters_ = {version_, std::move(out)};
    return characters_.second;
}

const std::vector<StoryEditor::AtlasLines>& StoryEditor::atlases() const {
    if (atlases_.first == version_) return atlases_.second;
    std::vector<AtlasLines> out;
    for (const File& f : files_)
        for (std::size_t i = 0; i < f.lines.size(); ++i) {
            if (indentOf(f.lines[i]) != 0 || lower(firstWord(f.lines[i])) != "atlas") continue;
            const std::string rest(afterFirst(f.lines[i]));
            AtlasLines a{.name = lower(firstWord(rest)), .image = lower(firstWord(rest))};
            for (const std::string& w : rawWords(afterFirst(rest)))
                if (lower(w).starts_with("image=")) a.image = lower(imageName(unquote(w.substr(6))));
                else if (lower(w).starts_with("same_names_as="))
                    if (const auto it = std::ranges::find(out, lower(unquote(w.substr(14))), &AtlasLines::name); it != out.end()) a.expressions = it->expressions;
            for (std::size_t j = i + 1; j < f.lines.size() && (blank(f.lines[j]) || indentOf(f.lines[j]) > 0); ++j) {
                std::string part = f.lines[j]; // ("eyes: open, half, closed" too)
                std::ranges::replace(part, ',', ' ');
                std::ranges::replace(part, ':', ' ');
                std::vector<std::string> listed;
                for (const std::string& w : rawWords(afterFirst(part)))
                    if (lower(w).starts_with("image=")) a.layers.push_back(lower(imageName(unquote(w.substr(6)))));
                    else if (!w.contains('=')) listed.push_back(lower(w));
                if (lower(firstWord(part)) == "expressions" && !listed.empty()) a.expressions = std::move(listed);
            }
            out.push_back(std::move(a));
        }
    atlases_ = {version_, std::move(out)};
    return atlases_.second;
}

bool StoryEditor::isCharacter(std::string_view name) const { return hasNamed(characters(), name); }

std::pair<std::string, std::string> StoryEditor::speakerOf(std::string_view who) const {
    who = trim(who);
    if (who.ends_with(')') && who.find('(') != npos) { // Aiko (happy)
        const auto open = who.rfind('(');
        return {std::string(trim(who.substr(0, open))), std::string(trim(who.substr(open + 1, who.size() - open - 2)))};
    }
    if (const auto space = who.rfind(' '); space != npos && !isCharacter(who) && isCharacter(who.substr(0, space))) // Aiko happy
        return {std::string(who.substr(0, space)), std::string(who.substr(space + 1))};
    return {std::string(who), {}};
}

StoryEditor::PhoneLine StoryEditor::phoneLineOf(std::string_view text) const {
    static constexpr std::pair<std::string_view, std::string_view> kVerbs[]{{"sends", "sends"},   {"is typing", "typing"}, {"deletes it", "deletes"},
                                                                             {"edits it", "edits"}, {"reacts", "reacts"},    {"reads it", "reads"}};
    PhoneLine p;
    text = trim(text);
    std::size_t colon = npos; // (the first outside quotes)
    for (std::size_t k = 0, quotes = 0; k < text.size() && colon == npos; ++k) {
        if (text[k] == '"') ++quotes;
        else if (text[k] == ':' && quotes % 2 == 0) colon = k;
    }
    const auto spans = wordSpans(text);
    auto wordIs = [&](std::size_t i, std::string_view word, std::size_t& end) { // (a word, without a colon after it)
        std::string_view written = text.substr(spans[i].first, spans[i].second);
        end = spans[i].first + spans[i].second;
        if (written.ends_with(':')) written.remove_suffix(1), --end;
        return lower(written) == word;
    };
    std::size_t verbAt = npos, verbEnd = 0;
    for (std::size_t w = 1; w < spans.size() && verbAt == npos; ++w) {
        if (colon != npos && spans[w].first > colon) break;
        for (const auto& [words, does] : kVerbs) {
            const auto space = words.find(' ');
            std::size_t end = 0;
            if (!wordIs(w, words.substr(0, space), end)) continue;
            if (space != npos && (w + 1 >= spans.size() || !wordIs(w + 1, words.substr(space + 1), end))) continue;
            verbAt = spans[w].first;
            verbEnd = end;
            p.does = does;
            break;
        }
    }
    std::string who(trim(text.substr(0, verbAt != npos ? verbAt : colon)));
    const std::string_view after = verbAt != npos ? trim(text.substr(verbEnd)) : colon == npos ? std::string_view{} : trim(text.substr(colon + 1));
    if (lower(who) == "me") p.mine = true;
    else std::tie(p.who, p.expression) = speakerOf(who);
    if (p.does == "says" || p.does == "reacts") {
        p.text = unquote(after);
    } else if (p.does == "sends") { // sends [sticker | voice] NAME [: caption]
        std::string_view what = after, caption;
        if (const auto c = what.find(':'); c != npos) {
            caption = trim(what.substr(c + 1));
            what = trim(what.substr(0, c));
        }
        if (const std::string first = lower(firstWord(what)); first == "sticker" || first == "voice") {
            p.does = first;
            what = trim(afterFirst(what));
        }
        p.file = unquote(what);
        p.text = unquote(caption);
    } else if (p.does == "typing") {
        if (lower(firstWord(after)) == "for") p.seconds = std::string(firstWord(afterFirst(after)));
    } else if (p.does == "edits") {
        p.text = unquote(after.starts_with(':') ? after.substr(1) : after);
    }
    return p;
}

bool StoryEditor::hasScene(std::string_view name) const { return hasNamed(scenes_, name); }
bool StoryEditor::hasScreen(std::string_view name) const { return hasNamed(screens_, name); }

const StoryEditor::KeptNames& StoryEditor::namesIn(std::size_t file) const {
    if (fileNames_.size() != files_.size()) fileNames_.resize(files_.size());
    KeptNames& kept = fileNames_[file];
    if (kept.stamp == files_[file].stamp) return kept;
    kept = {.stamp = files_[file].stamp, .variables = {}, .definitions = {}};
    const auto& lines = files_[file].lines;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const std::string verb = lower(firstWord(line));
        const std::string_view rest = afterFirst(line);
        std::string name;
        if (verb == "set" || (verb == "variable" && indentOf(line) == 0)) name = lower(firstWord(rest));
        if (!name.empty() && std::ranges::all_of(name, [](char c) { return isWordChar(c) || c == '.'; }) && !std::ranges::contains(kept.variables, name))
            kept.variables.push_back(std::move(name));
        if (line.contains("ariable=")) // (inputs' and sliders' variable=: only their lines split into words)
            for (const std::string& word : rawWords(line))
                if (lower(word).starts_with("variable=") && validName(unquote(word.substr(9))) && !std::ranges::contains(kept.variables, lower(unquote(word.substr(9)))))
                    kept.variables.push_back(lower(unquote(word.substr(9))));
        if (indentOf(line) != 0 || verb.empty()) continue;
        const auto w = rawWords(splitNote(rest).first);
        if (w.empty()) continue;
        if (verb == "character" && std::ranges::any_of(w, [](const std::string& word) { return lower(word).starts_with("love="); })) // (a love interest's love)
            if (std::string love = lower(loveVariable(unquote(w[0]))); !std::ranges::contains(kept.variables, love)) kept.variables.push_back(std::move(love));
        std::string defined = unquote(w[0]);
        if (verb == "font" && (w.size() == 1 || defined.contains('/') || defined.contains('.'))) defined = "default"; // (font FILE)
        const std::string kind = verb == "type" && soundTypeLine(line) ? std::string("sound type") : verb; // (a type a sound is of: a list of its own)
        kept.definitions[kind].push_back({.file = file, .line = i, .end = blockEnd(file, i), .name = std::move(defined)});
    }
    return kept;
}

const std::vector<std::string>& StoryEditor::variables() const {
    if (variables_.first == version_) return variables_.second;
    std::vector<std::string> out;
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (const std::string& name : namesIn(f).variables)
            if (!std::ranges::contains(out, name)) out.push_back(name);
    std::ranges::sort(out);
    variables_ = {version_, std::move(out)};
    return variables_.second;
}

std::vector<std::string> StoryEditor::names(Pick pick, std::string_view context) const {
    auto filesIn = [&](const char* folder, std::span<const std::string_view> extensions) {
        std::vector<std::string> out;
        for (const auto ext : extensions)
            for (const std::string& name : listFiles(root_ + folder, ext))
                if (!name.contains('/')) out.push_back(scriptName(name.substr(0, name.size() - ext.size()))); // (what scripts call it)
        std::ranges::sort(out);
        return out;
    };
    auto add = [](std::vector<std::string>& out, std::string name) {
        if (!std::ranges::any_of(out, [&](const std::string& o) { return sameName(o, name); })) out.push_back(std::move(name));
    };
    static constexpr std::string_view kColourNames[]{"white", "black", "grey", "silver", "red", "darkred", "orange", "gold", "yellow",
                                                    "lime", "green", "darkgreen", "teal", "cyan", "skyblue", "lightblue", "blue", "navy",
                                                    "purple", "violet", "magenta", "pink", "hotpink", "brown", "beige", "transparent"};
    auto shown = [&] { // (the images to show: not one only atlases use, as their eyes' and mouths' or a sheet named otherwise)
        std::vector<std::string> images = filesIn("images/", kImageTypes);
        for (const std::string& name : filesIn("images/gallery/", kImageTypes)) images.push_back("gallery/" + name); // (the gallery's, by their names there)
        std::erase_if(images, [&](const std::string& image) {
            return std::ranges::any_of(atlases(), [&](const AtlasLines& a) { return std::ranges::contains(a.layers, lower(image)) || (a.image == lower(image) && a.name != a.image); });
        });
        return images;
    };
    std::vector<std::string> out;
    switch (pick) {
    case Pick::None:
        break;
    case Pick::Characters:
    case Pick::Shown:
        for (const Character& c : characters()) out.push_back(c.name);
        if (pick == Pick::Shown) std::ranges::copy(shown(), std::back_inserter(out));
        if (pick == Pick::Shown) std::ranges::copy(defined("sub-image"), std::back_inserter(out));
        if (pick == Pick::Shown) std::ranges::copy(filesIn("movies/", kMovieTypes), std::back_inserter(out)); // (a movie plays as an image)
        break;
    case Pick::VisualTypes: // (every game's, then the game's own)
        for (const std::string_view type : kVisualTypes) out.emplace_back(type);
        for (std::string& type : defined("type")) add(out, std::move(type));
        break;
    case Pick::Movies:
        out = filesIn("movies/", kMovieTypes);
        break;
    case Pick::Images: // (the images alone: the first for a new part)
        out = shown();
        for (std::string& p : defined("sub-image")) add(out, std::move(p));
        break;
    case Pick::ImagesOrColours: // (an image's box, where a movie plays too; context "files" leaves out the sub-images and movies, for one cut from
                                // a file; "still" the movies, for one drawn as a still: a style's, an atlas's, a face, a weather speck, the pointer)
        out = shown();
        if (context != "files")
            for (std::string& p : defined("sub-image")) add(out, std::move(p));
        if (context != "files" && context != "still")
            for (std::string& m : filesIn("movies/", kMovieTypes)) add(out, std::move(m));
        for (std::string& c : defined("colour")) add(out, std::move(c));
        for (const std::string_view c : {"black", "white"}) add(out, std::string(c));
        break;
    case Pick::Bases: // aiko for aiko_happy.png, and the atlases
        for (const std::string& name : filesIn("images/", kImageTypes)) add(out, name.substr(0, name.find('_')));
        for (std::string& a : defined("atlas")) add(out, std::move(a));
        std::ranges::sort(out);
        break;
    case Pick::Sounds: // (a sound's box: the files, then the sounds named by sound lines; context "files" the files alone, for what a named
                       // sound plays; "type:KIND", the named sounds of that type alone)
        out = filesIn("sounds/", kSoundTypes);
        for (const std::string& name : filesIn("sounds/music/", kSoundTypes)) out.push_back("music/" + name); // (the music room's, by their names there)
        if (context != "files")
            for (const Definition& d : definitions("sound"))
                if (!context.starts_with("type:") || namedSoundType(d.name) == context.substr(5)) add(out, d.name);
        break;
    case Pick::VoiceFolders: // sounds/voices/NAME/
        for (const auto ext : kSoundTypes)
            for (const std::string& name : listFiles(root_ + "sounds/voices/", ext))
                if (const auto slash = name.find('/'); slash != npos) add(out, name.substr(0, slash));
        std::ranges::sort(out);
        break;
    case Pick::Channels: // (what stop stops: a sound type, all of it; a named sound, or a file, by its name)
        for (std::string& t : names(Pick::SoundTypes)) add(out, std::move(t));
        for (const std::string& s : names(Pick::Sounds)) add(out, s);
        break;
    case Pick::SoundTypes: // (every game's, then the game's own)
        for (const std::string_view type : kBuiltInSoundTypes) out.emplace_back(type);
        for (std::string& type : defined("sound type")) add(out, std::move(type));
        break;
    case Pick::ImageFiles:
        for (const auto ext : kImageTypes)
            for (const std::string& name : listFiles(root_ + "images/", ext))
                if (!name.contains('/')) out.push_back("images/" + name);
        std::ranges::sort(out);
        break;
    case Pick::Fonts: // (as font lines name them: "fonts/Lora.ttf")
        for (const auto ext : kFontTypes)
            for (const std::string& name : listFiles(root_ + "fonts/", ext))
                if (!name.contains('/')) out.push_back("fonts/" + name);
        std::ranges::sort(out);
        break;
    case Pick::FontNames:
        out = {"default"};
        for (std::string& f : defined("font")) add(out, std::move(f));
        break;
    case Pick::Places:
        out = {"left", "centre", "right", "farleft", "farright"};
        for (std::string& p : defined("place"))
            if (!std::ranges::contains(out, lower(p))) out.push_back(std::move(p));
        break;
    case Pick::Scenes:
        for (const Scene& s : scenes_) out.push_back(s.name);
        break;
    case Pick::Screens: { // (what the story or a button opens, shows or hides: not what lines and choices come in by themselves)
        std::vector<std::string> boxes{"textbox", "choice"};
        for (const Definition& c : definitions("character")) // (a character's own textbox)
            for (const std::string& line : linesOf(c))
                for (const std::string& word : rawWords(line))
                    if (lower(word).starts_with("textbox=")) boxes.push_back(lower(unquote(std::string_view(word).substr(8))));
        for (const Screen& s : screens_)
            if (!std::ranges::contains(boxes, lower(s.name))) out.push_back(s.name);
        break;
    }
    case Pick::Textboxes: // (what a character's lines come in: one of the game's screens, or the built-in textbox)
        for (const Screen& s : screens_)
            if (lower(s.name) != "choice") out.push_back(s.name);
        if (!std::ranges::contains(out, std::string("textbox"))) out.emplace_back("textbox");
        break;
    case Pick::Styles:
        out = defined("style");
        break;
    case Pick::Colours:
        out = defined("colour");
        for (const std::string_view c : kColourNames) add(out, std::string(c));
        break;
    case Pick::Records:
        out = defined(context);
        break;
    case Pick::Animations:
        out = defined("animation");
        break;
    case Pick::TimeWords: { // (a time line's day, `context` "day": the weekdays once the game knows them; its time of day, "time":
                            // the game's parts of the day)
        const bool dayBox = context == "day", timeBox = context == "time";
        const std::string parts = setting("time_parts").value_or("morning 6:00, afternoon 12:00, evening 18:00, night 22:00");
        if (!dayBox)
            for (const std::string_view part : wordsOf(unquote(parts), ','))
                if (const auto w = wordsOf(trim(part)); !w.empty()) out.push_back(lower(w.front()));
        // (real time has the computer's calendar; else Start day needs a date, 2025-07-14, or a weekday)
        const std::string start = lower(setting("time_start").value_or(""));
        const bool weekdays = lower(setting("time").value_or("none")) == "real" ||
                              std::ranges::any_of(wordsOf(start), [](std::string_view w) {
                                  return std::ranges::contains(kWeekdays, w, [](std::string_view d) { return lower(d); }) ||
                                         (w.size() == 10 && w[4] == '-' && w[7] == '-');
                              });
        if (weekdays && !timeBox)
            for (const std::string_view day : kWeekdays) out.emplace_back(day); // (as time_weekday has them: Friday)
        break;
    }
    case Pick::Weekdays: // (a day: these, or a date typed)
        for (const std::string_view day : kWeekdays) out.emplace_back(day);
        break;
    case Pick::VariableNames: // (then what the records keep, to set: persistent.ending.NAME, for each the game lists)
    case Pick::Conditions: // (and to check, with the engine's own values; what screens can check too)
        out = variables();
        for (const std::string_view kind : {"ending", "achievement", "tip", "gallery", "song"})
            for (const std::string& name : defined(kind))
                if (std::ranges::all_of(name, [](char c) { return letterOrDigitByte(c) || c == '_'; })) // (as a variable's name can be)
                    add(out, std::format("persistent.{}.{}", kind, lower(name)));
        if (pick == Pick::Conditions)
            for (const std::string_view w : kBuiltinValues) add(out, std::string(w));
        break;
    case Pick::Settings: // (the ones a slider can change)
        out = {"master_volume", "music_volume", "sound_volume", "voice_volume", "text_speed", "auto_delay", "textbox_opacity", "text_size"};
        break;
    case Pick::Controls:
        out = defined("control");
        break;
    case Pick::Keys: {
        out = {"escape", "space", "return", "kp_enter", "tab", "backspace", "delete", "up", "down", "left", "right", "home", "end", "pageup", "pagedown"};
        for (int n = 1; n <= 12; ++n) out.push_back(std::format("f{}", n));
        for (char c = 'a'; c <= 'z'; ++c) out.emplace_back(1, c);
        for (char c = '0'; c <= '9'; ++c) out.emplace_back(1, c);
        for (const std::string_view k : {"ctrl+s", "ctrl+l", "ctrl+z", "mouse_left", "mouse_right", "mouse_middle", "wheel_up", "wheel_down",
                                         "pad_a", "pad_b", "pad_x", "pad_y", "pad_start", "pad_back", "pad_lb", "pad_rb", "pad_lt", "pad_rt", "pad_ls", "pad_rs",
                                         "pad_up", "pad_down", "pad_left", "pad_right", "pad_guide", "swipe_left", "swipe_right", "swipe_up", "swipe_down",
                                         "long_press", "two_finger_tap"})
            out.emplace_back(k);
        break;
    }
    case Pick::Languages: // (setting language NAME: the game's own by its name, then its translations by their files)
        out.push_back(setting("language").value_or("English")); // (as Settings has it)
        for (const std::string& file : filesIn("translations/", std::array{std::string_view{".vnscript"}})) out.push_back(lower(file));
        break;
    case Pick::Expressions: { // what `show NAME EXPRESSION` can show (the image NAME_EXPRESSION, as the game finds it): the
                              // character's own images (images/aiko_happy.png) and sub-images, and its atlases' expressions: the
                              // atlas named as its images are, as they are (aiko: happy), and others, after their own name
                              // (aiko_close: close_happy). Never an atlas's own image, the images its eyes blink and its mouth
                              // talks with, nor the face beside the textbox (aiko_face).
        const std::string base = imageBaseOf(context), prefix = base + "_";
        std::vector<std::string> notShown, atlased; // (atlases' names and images, and their eyes' and mouths'; their expressions)
        for (const AtlasLines& a : atlases()) {
            notShown.push_back(a.name);
            notShown.push_back(a.image);
            std::ranges::copy(a.layers, std::back_inserter(notShown));
            if (a.name == base) std::ranges::copy(a.expressions, std::back_inserter(atlased));
            else if (a.name.starts_with(prefix))
                for (const std::string& e : a.expressions) atlased.push_back(a.name.substr(prefix.size()) + "_" + e);
        }
        for (const std::string& name : filesIn("images/", kImageTypes))
            if (const std::string l = lower(name); l.starts_with(prefix) && !l.ends_with("_face") && !std::ranges::contains(notShown, l))
                add(out, name.substr(prefix.size()));
        for (const std::string& p : defined("sub-image"))
            if (lower(p).starts_with(prefix)) add(out, p.substr(prefix.size()));
        for (std::string& e : atlased) add(out, std::move(e));
        break;
    }
    }
    if ((pick == Pick::ImagesOrColours || pick == Pick::Shown) && !context.starts_with("type:") && context != "files" && context != "shows") { // (any other visual's box, a hide line's: the visuals named by visual lines and the characters too, as the story shows them; a still's, none showing a movie; not what a visual shows)
        for (const Definition& d : definitions("visual")) {
            const std::string shows = visualOption(d.name, "shows", false);
            if (context == "still" && (namedVisualType(d.name) == "movie" || visualKindOf(shows.empty() ? d.name : shows) == "movie")) continue;
            add(out, d.name);
        }
        for (const Character& c : characters()) add(out, c.name);
    }
    if (context.starts_with("type:")) { // (a box of a visual type's: the visuals named as of it too, and only what it takes; its colours' with its characters)
        const std::string type(context.substr(5)), takes = typeAccepts(type);
        if (pick == Pick::ImagesOrColours && std::ranges::contains(wordsOf(takes), std::string_view("character")))
            for (const Character& c : characters()) add(out, c.name);
        for (const Definition& d : definitions("visual"))
            if (namedVisualType(d.name) == type) add(out, d.name);
        if ((pick == Pick::Shown || pick == Pick::ImagesOrColours) && !takes.empty())
            std::erase_if(out, [&](const std::string& n) {
                const std::string is = visualKindOf(n);
                return !is.empty() && is != type && !std::ranges::contains(wordsOf(takes), std::string_view(is));
            });
    }
    return out;
}

bool StoryEditor::asBackground(std::string_view line) {
    const auto w = rawWords(line);
    if (w.empty() || lower(w[0]) != "show") return false;
    for (std::size_t i = 1; i + 1 < w.size(); ++i)
        if (lower(w[i]) == "as" && lower(w[i + 1]) == "background") return true;
    return false;
}

std::string StoryEditor::namedVisualType(std::string_view name) const {
    for (const Definition& d : definitions("visual"))
        if (sameName(d.name, name)) {
            for (const std::string& word : rawWords(linesOf(d).empty() ? std::string{} : linesOf(d).front()))
                if (lower(word).starts_with("type=")) return lower(unquote(std::string_view(word).substr(5)));
            return "image";
        }
    return {};
}

std::string StoryEditor::typeAccepts(std::string_view type) const {
    const std::string key = lower(type);
    if (const auto every = std::ranges::find(kVisualTypes, key); every != kVisualTypes.end()) // (every game's: as every game has it)
        return std::string(kVisualTypeAccepts[static_cast<std::size_t>(every - kVisualTypes.begin())]);
    if (!typeLine(key)) return {};
    std::string out = lower(typeOption(key, "accepts"));
    std::ranges::replace(out, ',', ' ');
    return out.empty() ? "character image movie colour" : out; // (none written: all of them)
}

bool StoryEditor::colourBox(std::string_view shows, std::string_view type) const {
    const std::string kind = lower(type);
    if (kind == "background" || !std::ranges::contains(wordsOf(typeAccepts(kind)), std::string_view("colour"))) return false;
    Palette palette; // (the game's colours by name; what each is doesn't matter here)
    for (const std::string& c : defined("colour")) palette.emplace(lower(c), SDL_Color{});
    return parseColour(shows, &palette).has_value(); // (a gradient too: its top)
}

std::optional<StoryEditor::Definition> StoryEditor::typeLine(std::string_view type) const {
    std::optional<Definition> out;
    for (const Definition& d : definitions("type")) // (the last one, as the game has it)
        if (sameName(d.name, type)) out = d;
    return out;
}

bool StoryEditor::soundTypeLine(std::string_view line) {
    return std::ranges::any_of(rawWords(line), [](const std::string& w) {
        const std::string k = lower(w.substr(0, w.find('=')));
        return w.contains('=') && (k == "follows" || k == "one_at_a_time" || k == "loop" || k == "volume");
    });
}

bool StoryEditor::typeNameTaken(std::string_view name) const {
    const std::string key = lower(name);
    return std::ranges::contains(kVisualTypes, key) || std::ranges::contains(kBuiltInSoundTypes, key) || hasNamed(definitions("type"), key) ||
           hasNamed(definitions("sound type"), key);
}

bool StoryEditor::isSoundType(std::string_view type) const {
    if (std::ranges::contains(kBuiltInSoundTypes, lower(type))) return true;
    return hasNamed(definitions("sound type"), type);
}

std::string StoryEditor::namedSoundType(std::string_view name) const {
    for (const Definition& d : definitions("sound"))
        if (sameName(d.name, name)) {
            for (const std::string& word : rawWords(linesOf(d).empty() ? std::string{} : linesOf(d).front()))
                if (lower(word).starts_with("type=")) return lower(unquote(std::string_view(word).substr(5)));
            return "sound";
        }
    return {};
}

std::string StoryEditor::soundTypeOption(std::string_view type, std::string_view key) const {
    const std::string t = lower(type);
    std::optional<Definition> own;
    for (const Definition& d : definitions("sound type")) // (the last one, as the game has it)
        if (sameName(d.name, t)) own = d;
    std::string follows = t == "music" || t == "voice" ? t : "sound"; // (every game's, as they are)
    if (own) {
        const std::string prefix = std::string(key) + "=";
        std::string said;
        for (const std::string& word : rawWords(linesOf(*own).front())) {
            if (lower(word).starts_with(prefix)) said = unquote(std::string_view(word).substr(prefix.size()));
            if (lower(word).starts_with("follows=")) follows = lower(unquote(std::string_view(word).substr(8)));
        }
        if (!said.empty()) return key == "follows" ? lower(said) : said;
    } else if (!std::ranges::contains(kBuiltInSoundTypes, t)) {
        return {};
    }
    // (unsaid: as the one it follows has it)
    if (key == "follows") return follows;
    if (key == "one_at_a_time") return follows == "sound" ? "no" : "yes";
    if (key == "loop") return follows == "music" ? "yes" : "no";
    if (key == "volume") return "100%";
    return {};
}

std::string StoryEditor::soundOption(std::string_view name, std::string_view key) const {
    for (const Definition& d : definitions("sound"))
        if (sameName(d.name, name)) {
            const std::string prefix = std::string(key) + "=";
            for (const std::string& word : rawWords(linesOf(d).empty() ? std::string{} : linesOf(d).front()))
                if (lower(word).starts_with(prefix)) return unquote(std::string_view(word).substr(prefix.size()));
            return {};
        }
    return {};
}

std::string StoryEditor::soundKindOf(std::string_view name) const {
    const std::string key = lower(trim(name));
    if (key.empty()) return {};
    if (std::string type = namedSoundType(key); !type.empty()) return type;
    if (key.starts_with("music/")) return "music"; // (a song)
    const auto files = names(Pick::Sounds);
    return std::ranges::any_of(files, [&](const std::string& f) { return lower(f) == key; }) ? std::string("sound") : std::string{};
}

std::string StoryEditor::typeOption(std::string_view type, std::string_view key) const {
    const auto def = typeLine(type);
    if (!def) return {};
    const auto lines = linesOf(*def);
    const std::string prefix = std::string(key) + "=";
    for (const std::string& word : rawWords(lines.empty() ? std::string{} : lines.front()))
        if (lower(word).starts_with(prefix)) return unquote(std::string_view(word).substr(prefix.size()));
    return {};
}

std::string StoryEditor::visualOption(std::string_view who, std::string_view key, bool character) const {
    std::string line; // (its character line, or its visual line)
    if (character) {
        for (const Character& c : characters())
            if (sameName(c.name, who)) line = files_[c.file].lines[c.line];
    } else {
        for (const Definition& d : definitions("visual"))
            if (sameName(d.name, who)) line = linesOf(d).empty() ? std::string{} : linesOf(d).front();
    }
    const std::string prefix = std::string(key) + "=";
    for (const std::string& word : rawWords(line))
        if (lower(word).starts_with(prefix)) return unquote(std::string_view(word).substr(prefix.size()));
    return {};
}

std::string StoryEditor::comes(std::string_view who, std::string_view key, bool character) const {
    if (std::string own = visualOption(who, key, character); !own.empty()) return own;
    return key != "y" ? "centre" : character ? "-1" : "0";
}

std::string StoryEditor::visualKindOf(std::string_view name) const {
    const std::string key = lower(trim(name));
    if (key.empty()) return {};
    if (isCharacter(key)) return "character";
    if (std::string type = namedVisualType(key); !type.empty()) return type; // (a visual named by a visual line: its type)
    if (visualNames_.version != version_ || visualNames_.files != filesGeneration) {
        visualNames_ = {.version = version_, .files = filesGeneration, .images = names(Pick::Images), .movies = names(Pick::Movies)};
        for (const Definition& a : definitions("atlas")) visualNames_.images.push_back(a.name); // (one named otherwise than its sheet)
    }
    auto listed = [&](const std::vector<std::string>& names) { return std::ranges::any_of(names, [&](const std::string& n) { return lower(n) == key; }); };
    if (listed(visualNames_.images)) return "image";
    if (listed(visualNames_.movies)) return "movie";
    return {};
}

int StoryEditor::typeLayer(std::string_view type) const {
    if (std::ranges::contains(kVisualTypes, lower(type))) return visualTypeLayer(lower(type)); // (every game's: 0 to 3)
    if (const auto n = parseNumber(typeOption(type, "layer"))) return static_cast<int>(std::lround(*n));
    return 0;
}

std::string_view StoryEditor::kindName(Kind kind) {
    // (in Kind's order, one each; each said with tr() where it's shown)
    static const std::array<std::string_view, static_cast<std::size_t>(Kind::Other) + 1> names{
        "Line"_text,    "Visual"_text,     "Choice"_text, "Option"_text,   "If"_text,    "Else"_text, "Set"_text,
        "Jump"_text,    "Sound"_text,      "Wait"_text,     "Time"_text,  "Effect"_text, "Weather"_text,
        "Camera"_text,  "Screen"_text,    "Together"_text,   "Phone"_text,  "Phone"_text,    "Phone"_text, "Missed"_text, "Command"_text};
    return names[static_cast<std::size_t>(kind)];
}

namespace {

// The phone's lines, by the word their first box has for each (Box::PhoneLine's).
constexpr std::pair<StoryEditor::Kind, std::string_view> kPhoneLines[]{
    {StoryEditor::Kind::Chat, "chat"}, {StoryEditor::Kind::Call, "call"}};

} // namespace

std::string_view StoryEditor::phoneWord(Kind kind) {
    const auto it = std::ranges::find(kPhoneLines, kind, &std::pair<Kind, std::string_view>::first);
    return it != std::end(kPhoneLines) ? it->second : std::string_view{};
}

StoryEditor::Kind StoryEditor::phoneKind(std::string_view word) {
    const auto it = std::ranges::find(kPhoneLines, word, &std::pair<Kind, std::string_view>::second);
    return it != std::end(kPhoneLines) ? it->first : Kind::Other;
}

std::vector<std::string> StoryEditor::phoneLines(Kind kind) {
    switch (kind) { // (who or what it's about left empty, and nothing in it: its form's + New buttons add that)
    case Kind::Chat: return {"chat"};
    case Kind::Call: return {"phone call"};
    default: return {};
    }
}

} // namespace vn
