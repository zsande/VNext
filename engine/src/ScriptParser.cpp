// Reading the scripts (Parser, ScriptParser.hpp): their files and lines, mistakes, the names read and written, the
// values in a line's words, and what's bound and checked once everything is read. What each kind of line says is read
// by ScriptStory.cpp (the story's lines), ScriptLibrary.cpp (what the Library lists) and ScriptScreens.cpp (screens).
#include "ScriptParser.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace vn {

namespace parse {

std::optional<int> clockMinutes(std::string_view text) {
    std::string t = lower(trim(text));
    std::optional<int> half; // (12-hour: 0 am, 720 pm)
    if (t.ends_with("am") || t.ends_with("pm")) {
        half = t.ends_with("pm") ? 720 : 0;
        t = std::string(trim(std::string_view(t).substr(0, t.size() - 2)));
    }
    const auto colon = t.find(':');
    int hours = 0, minutes = 0;
    const std::string_view h = std::string_view(t).substr(0, colon), m = colon == std::string::npos ? std::string_view("0") : std::string_view(t).substr(colon + 1);
    if (h.empty() || h.size() > 2 || (colon != std::string::npos && m.size() != 2)) return std::nullopt;
    if (std::from_chars(h.data(), h.data() + h.size(), hours).ec != std::errc{} || std::from_chars(m.data(), m.data() + m.size(), minutes).ec != std::errc{}) return std::nullopt;
    if (minutes > 59 || (half ? hours < 1 || hours > 12 : hours > 23)) return std::nullopt;
    return (half ? hours % 12 + *half / 60 : hours) * 60 + minutes;
}

std::vector<std::string_view> piecesOf(std::string_view text, char by) {
    std::vector<std::string_view> out;
    for (std::size_t at = 0; at <= text.size();) {
        const std::size_t end = std::min(text.find(by, at), text.size());
        if (const std::string_view piece = trim(text.substr(at, end - at)); !piece.empty()) out.push_back(piece);
        at = end + 1;
    }
    return out;
}

std::optional<int> weekdayNamed(std::string_view word) {
    for (std::size_t i = 0; i < kWeekdays.size(); ++i)
        if (lower(word) == lower(kWeekdays[i])) return static_cast<int>(i);
    return std::nullopt;
}

std::vector<Line> nest(std::vector<Line>& flat, std::size_t& i, int parentIndent) {
    std::vector<Line> out;
    while (i < flat.size() && flat[i].indent > parentIndent) {
        Line line = std::move(flat[i++]);
        line.children = nest(flat, i, line.indent);
        out.push_back(std::move(line));
    }
    return out;
}

std::string headOf(std::string_view text) {
    std::string head = lower(firstWord(text));
    if (head.size() > 1 && head.ends_with(':')) head.pop_back();
    return head;
}

std::string_view afterWords(std::string_view text, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const auto space = text.find_first_of(" \t");
        text = space == std::string_view::npos ? std::string_view{} : trim(text.substr(space));
    }
    return text;
}

bool isName(std::string_view s) {
    if (s.empty() || !(letterByte(s[0]) || s[0] == '_')) return false;
    return std::ranges::all_of(s, [](char c) { return letterOrDigitByte(c) || c == '_' || c == '-'; });
}

bool isVariable(std::string_view s) {
    if (s.empty() || !(letterByte(s[0]) || s[0] == '_')) return false;
    return std::ranges::all_of(s, [](char c) { return letterOrDigitByte(c) || c == '_' || c == '.'; });
}

std::string commandOf(std::string_view text, bool story) {
    const std::string first = lower(firstWord(text));
    const std::string second = lower(firstWord(afterWords(text, 1)));
    for (const auto& c : kCommands) {
        if (!(story ? c.story : c.button)) continue;
        const auto space = c.words.find(' ');
        if (space == std::string_view::npos ? first == c.words : first == c.words.substr(0, space) && second == c.words.substr(space + 1))
            return std::string(c.words);
    }
    return {};
}

bool isBlockWord(std::string_view verb) {
    return verb == "if" || verb == "else" || verb == "choice" || verb == "scene";
}

bool hasAppPart(const Line& screen, std::string_view name) {
    return std::ranges::any_of(screen.children, [&](const Line& part) {
        const bool app = lower(firstWord(part.text)) == "container" && std::ranges::any_of(rawWords(afterWords(part.text, 1)), [&](const std::string& word) {
            return lower(word).starts_with("app=") && lower(unquote(std::string_view(word).substr(4))) == name;
        });
        return app || hasAppPart(part, name);
    });
}

std::optional<Align> toAlign(std::string_view s, bool vertical) {
    if (s == (vertical ? "top" : "left")) return Align::Start;
    if (s == "centre") return Align::Centre;
    if (s == (vertical ? "bottom" : "right")) return Align::End;
    return std::nullopt;
}

// `entry` is main.vnscript: the game starts there, like a C++ program in main(). It has the game's
// setting lines and its "scene start", and includes the other files.
void Parser::run(const std::string& entry, std::string_view builtin) {
    std::vector<Line> flat;
    if (!builtin.empty()) readSource("<built-in ui>", builtin, 0, flat);
    builtinFiles_ = s_.files.size();
    readFile(entry, 0, flat, nullptr);
    if (s_.files.size() == builtinFiles_) return; // no main.vnscript (the error says so)
    std::size_t i = 0;
    const std::vector<Line> lines = nest(flat, i, -1);

    phase_ = Phase::Definitions; // characters, colours, styles... wherever they are
    block(lines);
    definitions();
    phase_ = Phase::Story;
    block(lines);
    if (!s_.scenes.contains("start"))
        s_.errors.push_back({entry, 0, "The story needs a beginning: a 'scene start' line in main.vnscript, with its first lines under it."_tr()});
    screens();
    for (const auto& [name, defined] : styleLines_) (void)style(name, *defined.front()); // (each read once: one no screen uses has its mistakes said too)
    bindings();
    resolve();
    // The phone's own variables: none (the clock: the computer's), a signal and a full battery, unless Settings starts them.
    for (const char* phone : {"phone_owner", "phone_wallpaper", "phone_lock_wallpaper", "phone_passcode", "phone_clock", "phone_date"})
        s_.startValues.try_emplace(phone, std::string{});
    s_.startValues.try_emplace("phone_signal", 1);
    s_.startValues.try_emplace("phone_battery", 100);
    checkNames();
}

// -- Errors and output ---------------------------------------------------
void Parser::error(const Line& line, std::string message) {
    s_.errors.push_back({line.file < s_.files.size() ? s_.files[line.file] : std::string{}, line.number, std::move(message)});
}

// The same, for parts that then give back nothing: return fail(line, "...").
std::nullopt_t Parser::fail(const Line& line, std::string message) {
    error(line, std::move(message));
    return std::nullopt;
}

std::size_t Parser::emit(Command c, const Line& line, bool tail) {
    s_.code.push_back({std::move(c), line.file, line.number, tail});
    return s_.code.size() - 1;
}

cmd::Jump& Parser::jumpAt(std::size_t pc) { return std::get<cmd::Jump>(s_.code[pc].command); }

// `missing`: what's said when there's nothing there, in the words of the line's form (else the plain one).
std::optional<Expr> Parser::formula(const Line& line, std::string_view src, std::string missing) {
    src = trim(src);
    if (src.empty()) {
        error(line, missing.empty() ? "A value or a condition is missing."_tr() : std::move(missing));
        return std::nullopt;
    }
    auto e = Expr::parse(src);
    if (!e) return fail(line, "There's a mistake in '{}': {}."_tr(src, e.error()));
    for (std::string& name : e->names()) read(line, std::move(name));
    return std::move(*e);
}

// A text's {values} and tags: {tip NAME} one that's listed, {colour NAME} a colour or a value holding one,
// {speed N} whole letters a second, {pause N} seconds, {appears HOW} letter, word or line.
void Parser::checkTextOf(const Line& line, std::string_view text) {
    if (auto problem = checkText(text); !problem.empty()) return error(line, problem);
    readValues(line, text);
    const Lookup none = [](std::string_view) { return Value{0}; };
    (void)expand(text, none, [&](std::string_view tag, std::string_view name, std::size_t) {
        if (tag == "colour" && !name.empty() && !parseColour(name, &s_.palette)) read(line, lower(name));
        if (tag == "speed") {
            const auto n = parseNumber(name);
            if (!n || *n < 0.0f || *n != std::floor(*n))
                error(line, name.empty() ? "{speed} needs how many letters a second (0: at once)."_tr()
                                         : "{{speed}} needs a whole number of letters a second (0: at once), not '{}'."_tr(name));
        }
        if (tag == "pause" && !name.empty())
            if (const auto n = parseNumber(name); !n || *n < 0.0f) error(line, "{{pause}} needs a number of seconds (none: until a click), not '{}'."_tr(name));
        if (tag == "appears" && lower(name) != "letter" && lower(name) != "word" && lower(name) != "line")
            error(line, name.empty() ? "{appears} needs how the words appear: letter, word or line."_tr()
                                     : "{{appears}} needs letter, word or line, not '{}'."_tr(name));
        if (tag != "tip") return;
        if (name.empty()) error(line, "{tip} needs the name of a tip."_tr());
        else if (!s_.record(RecordKind::Tip, name))
            error(line, "There's no tip called '{}'."_tr(name));
    });
}

void Parser::read(const Line& line, std::string name) { reads_.push_back({.file = line.file, .number = line.number, .name = std::move(name), .items = items_}); }

void Parser::readValues(const Line& line, std::string_view text) {
    for (std::string& name : namesIn(text)) read(line, std::move(name));
}

void Parser::wrote(std::string name) { written_.push_back(std::move(name)); }

// Each name read is something: a variable (one the scripts set or start with, or a slider's or
// input's), a value the engine gives (its built-in ones,
// the player's settings, answer and dropped), a record's (persistent.ending.NAME...), or, in a part inside a
// list, its items' ({chat.name}).
void Parser::checkNames() {
    std::set<std::string, std::less<>> known(written_.begin(), written_.end());
    for (const auto& [name, value] : s_.startValues) known.insert(name);
    for (const std::string_view name : kBuiltinValues) known.emplace(name);
    known.insert({"answer", "dropped"});
    for (std::size_t k = 0; k < kRecordKinds.size(); ++k)
        for (const Script::Record& r : s_.records(static_cast<RecordKind>(k))) known.insert(Script::recordVariable(static_cast<RecordKind>(k), r.name));
    auto itemOf = [](std::string_view name) -> std::string_view { // the item word, when it's one of a list item's values
        const auto dot = name.find('.');
        const auto it = dot == std::string_view::npos ? kItemValues.end() : std::ranges::find(kItemValues, name.substr(0, dot), &std::pair<std::string_view, std::string_view>::first);
        if (it == kItemValues.end()) return {};
        for (auto&& word : std::views::split(it->second, ' '))
            if (std::string_view(word.begin(), word.end()) == name.substr(dot + 1)) return it->first;
        return {};
    };
    std::set<std::tuple<std::uint16_t, int, std::string>> said;
    for (const Read& r : reads_) {
        if (known.contains(r.name) || Settings::has(r.name) || findSettingControl(r.name)) continue;
        const std::string_view item = itemOf(r.name);
        if (!item.empty() && std::ranges::contains(r.items, item)) continue;
        if (!said.insert({r.file, r.number, r.name}).second) continue;
        const Line where{.file = r.file, .number = r.number};
        if (item.empty()) error(where, "There's no variable or value called '{}'."_tr(r.name));
        else error(where, "'{}' only works inside a container with list={}."_tr(r.name, std::ranges::find(kLists, item, &std::pair<std::string_view, std::string_view>::second)->first));
    }
}

std::optional<float> Parser::number(const Line& line, std::string_view value, std::string_view what) {
    if (auto v = parseNumber(value)) return v;
    return fail(line, "'{}' needs a number, not '{}'."_tr(what, value));
}

std::optional<bool> Parser::boolean(const Line& line, std::string_view value, std::string_view what) {
    if (auto v = parseBool(value)) return v;
    return fail(line, "'{}' can be yes or no, not '{}'."_tr(what, value));
}

std::optional<SDL_Color> Parser::colour(const Line& line, std::string_view value) {
    if (auto c = parseColour(value, &s_.palette)) return c;
    return fail(line, "'{}' isn't a colour."_tr(value));
}

// When a mouth moves (mouth_moves= and face_mouth=, setting mouths_move): yes, voice or no.
std::optional<MouthMoves> Parser::mouthMoves(const Line& line, std::string_view what, std::string_view value) {
    if (const auto b = parseBool(value)) return *b ? MouthMoves::Yes : MouthMoves::No;
    return oneOf<MouthMoves>(line, what, value, kMouthMoves);
}

// fade=2, fade_out=1, volume=50% (music, sounds, voices, movies, weather, effects, stop lines).
Parser::Took Parser::amount(const Line& line, Cursor& c, std::string_view key, float& out) {
    if (!c.atOption(key)) return Took::No;
    const std::optional<float> n = number(line, c.option()->second, key);
    if (!n) return Took::Bad;
    out = key == "volume" ? std::clamp(*n, 0.0f, 1.0f) : std::max(0.0f, *n);
    return Took::Yes;
}

std::optional<Cursor> Parser::words(const Line& line, std::string_view text) {
    auto w = splitWords(text);
    if (!w) {
        error(line, "In '{}': {}."_tr(text, w.error()));
        return std::nullopt;
    }
    return Cursor(std::move(*w));
}

// -- Reading files ---------------------------------------------------------
void Parser::readFile(const std::string& path, int indent, std::vector<Line>& out, const Line* from) {
    if (!included_.insert(path).second) return; // each file is read once
    auto source = read_(path);
    if (!source) {
        if (from) error(*from, "There's no file '{}'."_tr(path));
        else s_.errors.push_back({path, 0, "This game has no scripts/main.vnscript, where every game starts."_tr()});
        return;
    }
    s_.hash = fnv1a(*source, s_.hash);
    readSource(path, *source, indent, out);
}

void Parser::readSource(const std::string& name, std::string_view source, int baseIndent, std::vector<Line>& out) {
    const auto file = static_cast<std::uint16_t>(s_.files.size());
    s_.files.push_back(name);
    int number = 0;
    for (auto&& part : std::views::split(source, '\n')) {
        ++number;
        std::string_view raw(part.begin(), part.end());
        if (raw.ends_with('\r')) raw.remove_suffix(1);
        const std::string_view text = trim(stripNote(raw));
        if (text.empty()) continue;
        Line line{.text = std::string(text), .indent = baseIndent + indentOf(raw), .file = file, .number = number};
        if (lower(firstWord(text)) == "include") {
            std::string target = unquote(afterWords(text, 1));
            if (target.find('.', target.find_last_of('/') == std::string::npos ? 0 : target.find_last_of('/')) == std::string::npos)
                target += ".vnscript";
            const auto slash = name.find_last_of('/');
            readFile((slash == std::string::npos ? std::string{} : name.substr(0, slash + 1)) + target, line.indent, out, &line);
            continue;
        }
        out.push_back(std::move(line));
    }
}

void Parser::bindings() {
    const std::size_t first = s_.controls.size();
    for (const auto& c : controlLines_)
        s_.controls.push_back({.name = c.name, .keys = c.keys, .actions = actions(*c.line, c.action), .inMenus = c.inMenus,
                               .condition = c.condition.empty() ? std::nullopt : formula(*c.line, c.condition)});
    // A key the game gives one of its own controls isn't a built-in one's (on screens, in the story, or in the developer
    // tools, as they are): a game's "a" for its map stays the map's, auto-forward keeps its controller button, and the
    // tools keep Tab and Enter from a game's own skip or next line.
    auto builtIn = [&](std::size_t i) { return controlLines_[i].line->file < builtinFiles_; };
    auto family = [&](std::size_t i) { return toolsVerbOf(s_.controls[first + i]) ? 2 : s_.controls[first + i].onScreens() ? 1 : 0; };
    for (std::size_t i = 0; i < controlLines_.size(); ++i)
        if (builtIn(i))
            for (std::size_t j = 0; j < controlLines_.size(); ++j)
                if (!builtIn(j) && family(j) == family(i))
                    std::erase_if(s_.controls[first + i].keys, [&](const KeyChord& k) { return std::ranges::contains(s_.controls[first + j].keys, k); });
}

// -- Scenes -----------------------------------------------------------------------
void Parser::resolve() {
    for (const auto& [file, number, scene] : phoneSceneNames_)
        if (!s_.scenes.contains(scene)) error(Line{.file = file, .number = number}, "There's no scene called '{}'."_tr(scene));
    for (auto& ins : s_.code) {
        const Line where{.file = ins.file, .number = ins.line};
        auto fix = [&](const std::string& scene, std::size_t& target) {
            if (auto pc = s_.findScene(scene)) target = *pc;
            else error(where, "There's no scene called '{}'."_tr(scene));
        };
        if (auto* jump = std::get_if<cmd::Jump>(&ins.command); jump && !jump->scene.empty()) fix(jump->scene, jump->target);
    }
}

} // namespace parse

} // namespace vn
