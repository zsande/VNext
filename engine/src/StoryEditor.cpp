// The editor (StoryEditor): the scripts as the pages show them, read: scenes, lines, characters, names, the phone's
// lines.
#include "Forms.hpp"

#include <cmath>
#include <iterator>
#include <ranges>
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
    if (command == "background") return Kind::Background;
    if ((command == "show" || command == "hide") && second == "screen") return Kind::Screen;
    if (command == "hide" && second == "ui") return Kind::Other;
    if (command == "show" || command == "hide") return Kind::Character;
    static constexpr std::pair<std::string_view, Kind> kCommandKinds[]{ // (the plain ones: a word, its kind)
        {"choice", Kind::Choice},     {"if", Kind::If},             {"else", Kind::Else},         {"set", Kind::Set},
        {"jump", Kind::Jump},
        {"music", Kind::Music},       {"sound", Kind::Sound},       {"voice", Kind::Voice},       {"stop", Kind::Stop},
        {"wait", Kind::Wait},         {"time", Kind::Time},         {"effect", Kind::Effect},     {"weather", Kind::Weather},
        {"camera", Kind::Camera},     {"movie", Kind::Movie},       {"open", Kind::Screen},       {"together", Kind::Together},
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
    const std::size_t version = version_; // (it only goes up: what's made from the scripts is made again)
    clear();
    version_ = version + 1;
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
}

std::size_t StoryEditor::blockEnd(std::size_t file, std::size_t label) const {
    const auto& lines = files_[file].lines;
    std::size_t end = label + 1;
    while (end < lines.size() && (blank(lines[end]) || indentOf(lines[end]) > 0)) ++end;
    while (end > label + 1 && blank(lines[end - 1])) --end;
    return end;
}

std::string_view StoryEditor::phoneSceneOf(const Scene& scene) const {
    for (const File& f : files_)
        for (const std::string& line : f.lines)
            if (indentOf(line) == 0 && lower(firstWord(line)) == "character")
                for (const std::string& word : rawWords(line))
                    if (const std::string w = lower(word); (w.starts_with("chat=") || w.starts_with("call=")) && lower(unquote(w.substr(5))) == lower(scene.name))
                        return w.starts_with("chat=") ? "chat" : "call";
    return {};
}

const std::vector<StoryEditor::Row>& StoryEditor::rows(const Scene& scene) const {
    auto& kept = rows_[{scene.file, scene.line}];
    if (kept.first == version_) return kept.second;
    std::vector<Row> out;
    const auto& lines = files_[scene.file].lines;
    const std::size_t end = blockEnd(scene.file, scene.line);
    std::vector<std::pair<int, std::size_t>> open; // the rows the next ones may be inside: indentation, row
    const Kind top = phoneSceneOf(scene) == "chat" ? Kind::Chat : Kind::Other; // (a character's chat= scene: its lines are a chat's)
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
    kept = {version_, std::move(out)};
    return kept.second;
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

const std::vector<std::string>& StoryEditor::variables() const {
    if (variables_.first == version_) return variables_.second;
    std::vector<std::string> out;
    for (const File& f : files_)
        for (const std::string& line : f.lines) {
            const std::string verb = lower(firstWord(line));
            const std::string_view rest = afterFirst(line);
            std::string name;
            if (verb == "set" || (verb == "variable" && indentOf(line) == 0)) name = lower(firstWord(rest));
            if (!name.empty() && std::ranges::all_of(name, [](char c) { return isWordChar(c) || c == '.'; }) && !std::ranges::contains(out, name))
                out.push_back(std::move(name));
            for (const std::string& word : rawWords(line)) // (inputs' and sliders' variable=)
                if (lower(word).starts_with("variable=") && validName(unquote(word.substr(9))) && !std::ranges::contains(out, lower(unquote(word.substr(9)))))
                    out.push_back(lower(unquote(word.substr(9))));
        }
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
    std::vector<std::string> out;
    switch (pick) {
    case Pick::None:
        break;
    case Pick::Characters:
    case Pick::Shown:
        for (const Character& c : characters()) out.push_back(c.name);
        if (pick == Pick::Shown) std::ranges::copy(filesIn("images/", kImageTypes), std::back_inserter(out));
        if (pick == Pick::Shown) std::ranges::copy(defined("sub-image"), std::back_inserter(out));
        break;
    case Pick::Images: // (the images alone: the first for a new part)
        out = filesIn("images/", kImageTypes);
        for (std::string& p : defined("sub-image")) add(out, std::move(p));
        break;
    case Pick::ImagesOrColours: // (an image's box: context "files" leaves out the sub-images, for one cut from a file)
        out = filesIn("images/", kImageTypes);
        if (context != "files")
            for (std::string& p : defined("sub-image")) add(out, std::move(p));
        for (std::string& c : defined("colour")) add(out, std::move(c));
        for (const std::string_view c : {"black", "white"}) add(out, std::string(c));
        break;
    case Pick::Bases: // aiko for aiko_happy.png, and the atlases
        for (const std::string& name : filesIn("images/", kImageTypes)) add(out, name.substr(0, name.find('_')));
        for (std::string& a : defined("atlas")) add(out, std::move(a));
        std::ranges::sort(out);
        break;
    case Pick::Sounds:
        out = filesIn("sounds/", kSoundTypes);
        break;
    case Pick::VoiceFolders: // sounds/voices/NAME/
        for (const auto ext : kSoundTypes)
            for (const std::string& name : listFiles(root_ + "sounds/voices/", ext))
                if (const auto slash = name.find('/'); slash != npos) add(out, name.substr(0, slash));
        std::ranges::sort(out);
        break;
    case Pick::Channels: // sound ... called=NAME, and looping sounds (by their file)
        for (const File& f : files_)
            for (const std::string& line : f.lines) {
                const auto w = rawWords(line);
                if (w.size() < 3 || lower(w[0]) != "sound") continue;
                for (std::size_t i = 2; i < w.size(); ++i) {
                    const std::string word = lower(w[i]);
                    if (word.starts_with("called=") && word.size() > 7) add(out, lower(unquote(std::string_view(w[i]).substr(7))));
                    if (word == "loop=yes") add(out, lower(unquote(w[1])));
                }
            }
        std::ranges::sort(out);
        break;
    case Pick::Movies:
        out = filesIn("movies/", kMovieTypes);
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
        // (real time has the computer's calendar; else Starts at needs a date, 2025-07-14, or a weekday)
        const std::string start = lower(setting("time_start").value_or(""));
        const bool weekdays = lower(setting("time").value_or("none")) == "real" ||
                              std::ranges::any_of(wordsOf(start), [](std::string_view w) {
                                  return std::ranges::contains(kWeekdays, w, [](std::string_view d) { return lower(d); }) ||
                                         (w.size() == 10 && w[4] == '-' && w[7] == '-');
                              });
        if (weekdays && !timeBox)
            for (const std::string_view day : kWeekdays) out.push_back(lower(day));
        break;
    }
    case Pick::TimeSpans: // (a time line's how long: by the clock, or with days and parts of the day, in days)
        if (lower(setting("time").value_or("none")) == "days") out = {"1 day", "2 days"};
        else out = {"30 minutes", "1 hour", "2 hours", "1 day"};
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
        std::map<std::string, std::vector<std::string>, std::less<>> expressionsOf; // (each atlas's, for same_names_as=)
        for (const File& f : files_)
            for (std::size_t i = 0; i < f.lines.size(); ++i) {
                if (indentOf(f.lines[i]) != 0 || lower(firstWord(f.lines[i])) != "atlas") continue;
                const std::string rest(afterFirst(f.lines[i])), name = lower(firstWord(rest));
                std::vector<std::string> expressions;
                notShown.push_back(name);
                for (const std::string& w : rawWords(afterFirst(rest)))
                    if (lower(w).starts_with("image=")) notShown.push_back(lower(imageName(unquote(w.substr(6)))));
                    else if (lower(w).starts_with("same_names_as="))
                        if (const auto it = expressionsOf.find(lower(unquote(w.substr(14)))); it != expressionsOf.end()) expressions = it->second;
                for (std::size_t j = i + 1; j < f.lines.size() && (blank(f.lines[j]) || indentOf(f.lines[j]) > 0); ++j) {
                    std::string part = f.lines[j]; // ("eyes: open, half, closed" too)
                    std::ranges::replace(part, ',', ' ');
                    std::ranges::replace(part, ':', ' ');
                    std::vector<std::string> listed;
                    for (const std::string& w : rawWords(afterFirst(part)))
                        if (lower(w).starts_with("image=")) notShown.push_back(lower(imageName(unquote(w.substr(6)))));
                        else if (!w.contains('=')) listed.push_back(lower(w));
                    if (lower(firstWord(part)) == "expressions" && !listed.empty()) expressions = std::move(listed);
                }
                if (name == base) std::ranges::copy(expressions, std::back_inserter(atlased));
                else if (name.starts_with(prefix))
                    for (const std::string& e : expressions) atlased.push_back(name.substr(prefix.size()) + "_" + e);
                expressionsOf.insert_or_assign(name, std::move(expressions));
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
    return out;
}

std::string_view StoryEditor::kindName(Kind kind) {
    // (in Kind's order, one each; each said with tr() where it's shown)
    static const std::array<std::string_view, static_cast<std::size_t>(Kind::Other) + 1> names{
        "Line"_text,    "Character"_text, "Background"_text, "Choice"_text, "Option"_text,   "If"_text,    "Else"_text, "Set"_text,
        "Jump"_text,    "Music"_text,     "Sound"_text,      "Voice"_text,  "Stop"_text,     "Wait"_text,  "Time"_text, "Effect"_text,
        "Weather"_text, "Camera"_text,    "Movie"_text,      "Screen"_text, "Together"_text, "Phone"_text, "Phone"_text, "Phone"_text,
        "Missed"_text,  "Command"_text};
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
