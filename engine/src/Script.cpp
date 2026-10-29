#include "Script.hpp"
#include "System.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <expected>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------
// How .vnscript files are written, line by line (scripts and translations).
namespace vn {

std::optional<std::size_t> noteStart(std::string_view line) {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        if (line[i] != '#' || quoted) continue;
        const bool atStart = line.find_first_not_of(" \t") == i;
        const bool spaced = i > 0 && std::isspace(static_cast<unsigned char>(line[i - 1])) &&
                            (i + 1 == line.size() || std::isspace(static_cast<unsigned char>(line[i + 1])));
        if (atStart || spaced) return i;
    }
    return std::nullopt;
}

std::string_view stripNote(std::string_view line) {
    const auto at = noteStart(line);
    return at ? line.substr(0, *at) : line;
}

std::pair<std::string_view, std::string_view> splitNote(std::string_view line) {
    const auto at = noteStart(line);
    if (!at) return {trim(line), {}};
    return {trim(line.substr(0, *at)), line.substr(*at)};
}

bool isQuoted(std::string_view s, char q) {
    return s.size() >= 2 && s.front() == q && s.back() == q && s.substr(1, s.size() - 2).find(q) == std::string_view::npos;
}

std::string unquote(std::string_view s, bool single) {
    s = trim(s);
    if (isQuoted(s) || (single && isQuoted(s, '\''))) s = s.substr(1, s.size() - 2);
    return std::string(s);
}

std::string_view firstWord(std::string_view text) {
    const auto start = text.find_first_not_of(" \t");
    if (start == std::string_view::npos) return {};
    text.remove_prefix(start);
    return text.substr(0, text.find_first_of(" \t"));
}

std::vector<std::pair<std::size_t, std::size_t>> wordSpans(std::string_view line) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    bool inQuotes = false;
    std::optional<std::size_t> start;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        const bool end = i == line.size() || (std::isspace(static_cast<unsigned char>(line[i])) && !inQuotes);
        if (i < line.size() && line[i] == '"') inQuotes = !inQuotes;
        if (end) {
            if (start) out.emplace_back(*start, i - *start);
            start.reset();
        } else if (!start) {
            start = i;
        }
    }
    return out;
}

std::vector<std::string> rawWords(std::string_view line) {
    std::vector<std::string> out;
    for (const auto [at, n] : wordSpans(line)) out.emplace_back(line.substr(at, n));
    return out;
}

std::expected<std::vector<ScriptWord>, std::string> splitWords(std::string_view line) {
    std::vector<ScriptWord> out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
            continue;
        }
        ScriptWord w{.quoted = line[i] == '"'};
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) {
            if (line[i] != '"') {
                w.text += line[i++];
                continue;
            }
            const auto close = line.find('"', i + 1);
            if (close == std::string_view::npos) return std::unexpected("a quote mark (\") is opened but never closed");
            w.text += line.substr(i + 1, close - i - 1);
            i = close + 1;
        }
        out.push_back(std::move(w));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Scripts
// ---------------------------------------------------------------------------

namespace {

// ---------------------------------------------------------------------------
// Lines and words
// ---------------------------------------------------------------------------

struct Line {
    std::string text; // without indentation or note
    int indent = 0;
    std::uint16_t file = 0;
    int number = 0;
    std::vector<Line> children; // the lines indented under it
};

// Each line takes the following lines that are indented further as its children.
std::vector<Line> nest(std::vector<Line>& flat, std::size_t& i, int parentIndent) {
    std::vector<Line> out;
    while (i < flat.size() && flat[i].indent > parentIndent) {
        Line line = std::move(flat[i++]);
        line.children = nest(flat, i, line.indent);
        out.push_back(std::move(line));
    }
    return out;
}

// The first word in lower case, without a trailing ':' ("else:" -> else).
std::string headOf(std::string_view text) {
    std::string head = lower(firstWord(text));
    if (head.size() > 1 && head.ends_with(':')) head.pop_back();
    return head;
}

bool capitalized(std::string_view text) { return startsWithCapital(text); }

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

// Variable names may contain dots: persistent.endings
bool isVariable(std::string_view s) {
    if (s.empty() || !(letterByte(s[0]) || s[0] == '_')) return false;
    return std::ranges::all_of(s, [](char c) { return letterOrDigitByte(c) || c == '_' || c == '.'; });
}

// Reads the words of a command from left to right.
class Cursor {
public:
    explicit Cursor(std::vector<ScriptWord> words) : w_(std::move(words)) {}

    [[nodiscard]] bool done() const { return i_ >= w_.size(); }
    [[nodiscard]] std::size_t left() const { return w_.size() - i_; }
    [[nodiscard]] const ScriptWord& peek(std::size_t ahead = 0) const { return w_[i_ + ahead]; }
    [[nodiscard]] bool at(std::string_view word) const { return !done() && !w_[i_].quoted && lower(w_[i_].text) == word; }
    bool take(std::string_view word) {
        if (!at(word)) return false;
        ++i_;
        return true;
    }
    std::optional<std::string> next() {
        if (done()) return std::nullopt;
        return w_[i_++].text;
    }
    void skip(std::size_t n) { i_ = std::min(w_.size(), i_ + n); }
    // key=value (the key in lower case)
    std::optional<std::pair<std::string, std::string>> option() {
        if (done() || w_[i_].quoted) return std::nullopt;
        const auto eq = w_[i_].text.find('=');
        if (eq == std::string::npos || eq == 0) return std::nullopt;
        auto kv = std::pair{lower(w_[i_].text.substr(0, eq)), w_[i_].text.substr(eq + 1)};
        ++i_;
        return kv;
    }
    // The next word is key=...
    [[nodiscard]] bool atOption(std::string_view key) const {
        return !done() && !w_[i_].quoted && lower(w_[i_].text).starts_with(std::format("{}=", key));
    }
    // A number, optionally followed by "seconds".
    std::optional<float> number() {
        if (done() || w_[i_].quoted) return std::nullopt;
        const auto n = parseNumber(w_[i_].text);
        if (!n) return std::nullopt;
        ++i_;
        (void)(take("seconds") || take("second"));
        return n;
    }

private:
    std::vector<ScriptWord> w_;
    std::size_t i_ = 0;
};

// ---------------------------------------------------------------------------
// Vocabulary
// ---------------------------------------------------------------------------

struct CommandWord {
    std::string_view words;
    bool story;  // can be used in the story
    bool button; // can be used in buttons, keys and screen hooks
};
constexpr auto kCommands = std::to_array<CommandWord>({
    // The story's own: blocks, waiting, questions, its structure.
    {"label", true, false}, {"if", true, false}, {"else", true, false}, {"elif", true, false}, {"choice", true, false},
    {"return", true, false}, {"wait", true, false}, {"ask", true, false}, {"textbox", true, false}, {"new page", true, false}, {"together", true, false},
    {"movie", true, false}, {"chapter", true, false}, {"chart", true, false}, {"ending", true, false}, {"message", true, false},
    {"phone", true, false},
    // Both.
    {"scene", true, true}, {"show", true, true}, {"hide", true, true}, {"move", true, true}, {"shake", true, true},
    {"flash", true, true}, {"effect", true, true}, {"weather", true, true}, {"camera", true, true}, {"music", true, true}, {"sound", true, true},
    {"voice", true, true}, {"stop", true, true}, {"jump", true, true}, {"call", true, true}, {"end", true, true},
    {"set", true, true}, {"add", true, true}, {"subtract", true, true}, {"toggle", true, true}, {"unlock", true, true},
    {"achievement", true, true}, {"tip", true, true}, {"notify", true, true}, {"open", true, true}, {"quick save", true, true},
    {"fullscreen", true, true}, {"screenshot", true, true}, {"quit", true, true},
    // Buttons and keys only.
    {"start", false, true}, {"continue", false, true}, {"close", false, true}, {"answer", false, true},
    {"quick load", false, true}, {"skip", false, true}, {"auto", false, true},
    {"advance", false, true}, {"rollback", false, true}, {"reload", false, true}, {"yes", false, true}, {"no", false, true},
    {"next page", false, true}, {"previous page", false, true}, {"delete save", false, true}, {"next cg", false, true},
    {"previous cg", false, true}, {"setting", false, true}, {"reset settings", false, true}, {"reset controls", false, true},
    {"reset progress", false, true},
});
constexpr auto kDefinitions = std::to_array<std::string_view>({"character", "color", "position", "font", "atlas", "picture", "config", "style",
                                                                "control", "gallery", "ending", "song", "achievement", "tip", "animation", "screen",
                                                                "variable"});

// The command a line starts with ("quick save"), or empty. Case doesn't matter.
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
    return verb == "if" || verb == "else" || verb == "elif" || verb == "choice" || verb == "label";
}

// "ending NAME", "achievement NAME" and "tip NAME" with nothing after the name are the story reaching
// one; with a title after it, they list one.
bool isRecordMark(std::string_view text, std::string_view head) {
    return (head == "ending" || head == "achievement" || head == "tip") && afterWords(text, 2).empty();
}

struct KindSpec {
    std::string_view name;
    ElementKind kind;
    std::string_view style; // style used when the element names none
    std::array<std::string_view, 10> options;
    bool list = false;      // a list or grid of items: also columns= item_w= item_h= gap= gap_x= gap_y=
};
constexpr auto kListOptions = std::to_array<std::string_view>({"columns", "item_w", "item_h", "gap", "gap_x", "gap_y", "glide", "glide_speed", "glide_distance"});
constexpr std::array kKinds{
    KindSpec{"image", ElementKind::Image, "", {"fit", "flip", "repeat", "selected", "scroll", "scroll_time", "bottom"}}, // (a picture, a colour, or one fading to bottom=)
    KindSpec{"text", ElementKind::Text, "text", {"selected"}},
    KindSpec{"button", ElementKind::Button, "button", {"action", "confirm", "key", "enabled", "selected", "drag", "drop", "click_animation"}},
    KindSpec{"key", ElementKind::Key, "", {"action", "confirm"}},
    KindSpec{"namebox", ElementKind::Namebox, "namebox", {"bar", "bar_color"}},
    KindSpec{"dialogue", ElementKind::Dialogue, "dialogue", {}},
    KindSpec{"indicator", ElementKind::Indicator, "indicator", {"bob", "bob_speed", "pause_image"}},
    KindSpec{"choices", ElementKind::Choices, "choice", {"anchor", "locked_style", "seen_style"}, true},
    KindSpec{"history", ElementKind::History, "history",
             {"name_w", "close_on_bottom", "name_style", "text_style", "choice_style", "voice", "back", "button_style"}, true},
    KindSpec{"pages", ElementKind::Pages, "tab", {"wide_w", "mode", "auto", "quick"}, true},
    KindSpec{"slots", ElementKind::Slots, "slot", {"mode", "delete", "title_style", "text_style", "empty_style", "confirm", "load_confirm", "delete_confirm"}, true},
    KindSpec{"message", ElementKind::Message, "notify", {}},
    KindSpec{"face", ElementKind::Face, "face", {"fit", "flip"}}, // flip=yes mirrors every face (and face_flip=yes ones back)
    KindSpec{"control", ElementKind::Control, "setting", {"slot", "waiting"}},
    KindSpec{"gallery", ElementKind::Gallery, "gallery", {"action", "title_style", "locked_style"}, true},
    KindSpec{"page", ElementKind::Page, "page", {"name_w", "name_style", "text_style"}, true},
    KindSpec{"input", ElementKind::Input, "input", {"var", "max", "hint", "action", "confirm", "enabled"}},
    KindSpec{"bar", ElementKind::Bar, "bar", {"value", "max", "vertical", "track", "fill"}},
    KindSpec{"slider", ElementKind::Slider, "slider", {"var", "setting", "min", "max", "step", "track", "fill", "knob", "enabled", "action"}},
    KindSpec{"movie", ElementKind::Movie, "", {"fit", "loop", "volume"}},
    KindSpec{"endings", ElementKind::Endings, "record", {"title_style", "text_style", "locked_style", "hidden"}, true},
    KindSpec{"songs", ElementKind::Songs, "record", {"title_style", "text_style", "locked_style"}, true},
    KindSpec{"achievements", ElementKind::Achievements, "record", {"title_style", "text_style", "locked_style", "hidden"}, true},
    KindSpec{"tips", ElementKind::Tips, "record", {"action", "title_style", "locked_style", "new_style"}, true},
    KindSpec{"chart", ElementKind::Chart, "chart", {"line", "chapter_style", "ending_style", "stub_style", "label_style", "confirm"}, true},
    KindSpec{"phone", ElementKind::Phone, "", {"name_style", "text_style", "mine_style", "reply_style"}, true},
    KindSpec{"contacts", ElementKind::Contacts, "record", {"action", "name_style", "text_style", "unread_style"}, true},
    KindSpec{"container", ElementKind::Container, "", {"selected", "fit", "scroll", "scrollbar", "scrollbar_size", "glide", "glide_speed", "glide_distance", "edges", "fade_size"}},
};
constexpr auto kColorOptions = std::to_array<std::string_view>({"bottom", "bar_color", "track", "fill", "knob", "line"});
constexpr auto kFormulaOptions = std::to_array<std::string_view>({"value", "min", "max", "step"});
// The player's settings: "setting NAME VALUE" (buttons), slider setting=NAME and "config default.NAME" lines.
constexpr auto kSettingNames = std::to_array<std::string_view>({
    "fullscreen", "window_size", "scaling", "vsync", "frame_limit", "text_speed", "auto_delay", "textbox_opacity", "text_size",
    "transitions", "master_volume", "music_volume", "sound_volume", "voice_volume", "babble", "typing_sounds", "mouths", "skip_unread",
    "skip_after_choices", "skip_movies", "rollback", "read_aloud", "high_contrast", "language",
});

// Words that pick one of a few values.
template <class T>
using Choices = std::span<const std::pair<std::string_view, T>>;
constexpr std::pair<std::string_view, Ease> kEases[]{{"linear", Ease::Linear}, {"in", Ease::In}, {"out", Ease::Out}, {"inout", Ease::InOut}};
constexpr std::pair<std::string_view, Reveal> kReveals[]{{"letter", Reveal::Letter}, {"word", Reveal::Word}, {"line", Reveal::Line}};
constexpr std::pair<std::string_view, TransitionKind> kTransitions[]{{"fade", TransitionKind::Fade}, {"dissolve", TransitionKind::Dissolve},
                                                                     {"wipe", TransitionKind::Wipe}, {"slide", TransitionKind::Slide},
                                                                     {"none", TransitionKind::None}};
constexpr std::pair<std::string_view, Talk> kTalks[]{{"yes", Talk::Yes}, {"voice", Talk::Voice}, {"no", Talk::No}};
constexpr std::pair<std::string_view, int> kScalings[]{{"fit", 0}, {"integer", 1}, {"stretch", 2}, {"fill", 3}};
constexpr std::pair<std::string_view, int> kMouths[]{{"always", 0}, {"yes", 0}, {"voice", 1}, {"never", 2}, {"no", 2}};

std::optional<Align> toAlign(std::string_view s) {
    if (s == "left" || s == "top" || s == "start") return Align::Start;
    if (s == "center" || s == "middle") return Align::Center;
    if (s == "right" || s == "bottom" || s == "end") return Align::End;
    return std::nullopt;
}

// A picture name as scripts use it: "images/mina.png" -> mina
std::string pictureName(std::string_view file) {
    std::string out(file);
    if (out.starts_with("images/")) out.erase(0, 7);
    if (const auto dot = out.rfind('.'); dot != std::string::npos && out.find('/', dot) == std::string::npos) out.erase(dot);
    return out;
}

// Default sprite file name for a character: "Mr. Tanaka" -> mr_tanaka
std::string imageName(std::string_view name) {
    std::string out;
    for (char c : lower(name)) {
        if (letterOrDigitByte(c) || c == '-' || c == '_') out += c;
        else if (c == ' ' && !out.empty() && out.back() != '_') out += '_';
    }
    return out;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser {
public:
    Parser(Script& script, const FileReader& read) : s_(script), read_(read) {}

    // `entry` is main.vnscript: the game starts there, like a C++ program in main(). It has the game's
    // config lines and its "label start", and includes the other files.
    void run(const std::string& entry, std::string_view builtin) {
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
        if (!s_.labels.contains("start"))
            s_.errors.push_back({entry, 0, "The story needs a beginning: write  label start  in main.vnscript, then its first lines under it. The game begins there."});
        screens();
        bindings();
        resolve();
    }

private:
    enum class Phase : std::uint8_t { Definitions, Story };

    // -- Errors and output ---------------------------------------------------

    void error(const Line& line, std::string message) {
        s_.errors.push_back({line.file < s_.files.size() ? s_.files[line.file] : std::string{}, line.number, std::move(message)});
    }
    // The same, for parts that then give back nothing: return fail(line, "...").
    std::nullopt_t fail(const Line& line, std::string message) {
        error(line, std::move(message));
        return std::nullopt;
    }

    // Runs `parse`; when the line's first word was capitalized and it doesn't work as a command,
    // its errors are dropped and false is returned (the line is then shown as text instead).
    template <class F>
    bool attempt(bool lenient, F&& parse) {
        const auto before = s_.errors.size();
        std::forward<F>(parse)();
        if (!lenient || s_.errors.size() == before) return true;
        s_.errors.resize(before);
        return false;
    }

    std::size_t emit(Command c, const Line& line) {
        s_.code.push_back({std::move(c), line.file, line.number});
        return s_.code.size() - 1;
    }

    cmd::Jump& jumpAt(std::size_t pc) { return std::get<cmd::Jump>(s_.code[pc].command); }

    std::optional<Expr> formula(const Line& line, std::string_view src) {
        src = trim(src);
        if (src.empty()) {
            error(line, "A value or a condition is missing.");
            return std::nullopt;
        }
        auto e = Expr::parse(src);
        if (!e) error(line, std::format("There's a mistake in '{}': {}.", src, e.error()));
        return e ? std::optional<Expr>(std::move(*e)) : std::nullopt;
    }

    void checkTextOf(const Line& line, std::string_view text) {
        if (auto problem = checkText(text); !problem.empty()) return error(line, problem + ".");
        const Lookup none = [](std::string_view) { return Value{0}; };
        (void)expand(text, none, [&](std::string_view tag, std::string_view name, std::size_t) { // {tip NAME}: one that's listed
            if (tag != "tip") return;
            if (name.empty()) error(line, "{tip} needs the name of a tip.");
            else if (!s_.record(RecordKind::Tip, name))
                error(line, std::format("There's no tip called '{}'.", name));
        });
    }

    std::optional<float> number(const Line& line, std::string_view value, std::string_view what) {
        if (auto v = parseNumber(value)) return v;
        return fail(line, std::format("{} needs a number, not '{}'.", what, value));
    }

    std::optional<bool> boolean(const Line& line, std::string_view value, std::string_view what) {
        if (auto v = parseBool(value)) return v;
        return fail(line, std::format("{} can be yes or no, not '{}'.", what, value));
    }

    std::optional<SDL_Color> color(const Line& line, std::string_view value) {
        if (auto c = parseColor(value, &s_.palette)) return c;
        return fail(line, std::format("'{}' isn't a colour.", value));
    }

    // One of a few words (case doesn't matter), else an error listing them.
    template <class T>
    std::optional<T> oneOf(const Line& line, std::string_view what, std::string_view value, Choices<T> choices) {
        std::string list;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            if (lower(value) == choices[i].first) return choices[i].second;
            list += std::format("{}{}", i == 0 ? "" : i + 1 == choices.size() ? " or " : ", ", choices[i].first);
        }
        error(line, std::format("{} can be {}, not '{}'.", what, list, value));
        return std::nullopt; // (not fail(): GCC 16 takes its nullopt, copied in, for something unset)
    }

    // When a mouth moves (talk= and face_talk=, config talk): yes, voice or no.
    std::optional<Talk> talk(const Line& line, std::string_view what, std::string_view value) {
        if (const auto b = parseBool(value)) return *b ? Talk::Yes : Talk::No;
        return oneOf<Talk>(line, what, value, kTalks);
    }

    // Words after a command that read as a phrase. Took::No: the next word isn't this one;
    // Took::Bad: it is, but what follows is wrong (the error is written).
    enum class Took : std::uint8_t { No, Yes, Bad };

    // "over 2 seconds" or "for 2 seconds" (moves, the camera, shake, flash), then for moves and the
    // camera "ease=out" and "and wait".
    Took timing(const Line& line, Cursor& c, float& seconds, Ease* ease = nullptr, bool* wait = nullptr, bool story = true) {
        if (const std::string_view word = c.take("over") ? "over" : c.take("for") ? "for" : ""; !word.empty()) {
            const auto n = c.number();
            if (!n) return error(line, std::format("'{}' needs a number of seconds.", word)), Took::Bad;
            seconds = std::max(0.0f, *n);
        } else if (wait && c.take("and")) {
        } else if (wait && c.take("wait")) {
            if (!story) return error(line, "A button can't wait for this to end."), Took::Bad;
            *wait = true;
        } else if (ease && c.atOption("ease")) {
            const auto e = oneOf<Ease>(line, "ease", c.option()->second, kEases);
            if (!e) return Took::Bad;
            *ease = *e;
        } else {
            return Took::No;
        }
        return Took::Yes;
    }

    // "fade 2" or "fade=2", "volume 50%" or "volume=50%" (music, sounds, voices, videos, weather).
    Took amount(const Line& line, Cursor& c, std::string_view key, float& out) {
        std::optional<float> n;
        if (c.take(key)) n = c.number();
        else if (c.atOption(key)) n = parseNumber(c.option()->second);
        else return Took::No;
        if (!n) return error(line, std::format("{} needs a number after it.", key)), Took::Bad;
        out = key == "volume" ? std::clamp(*n, 0.0f, 1.0f) : std::max(0.0f, *n);
        return Took::Yes;
    }

    std::optional<Cursor> words(const Line& line, std::string_view text) {
        auto w = splitWords(text);
        if (!w) {
            error(line, std::format("In '{}': {}.", text, w.error()));
            return std::nullopt;
        }
        return Cursor(std::move(*w));
    }

    // -- Reading files ---------------------------------------------------------

    void readFile(const std::string& path, int indent, std::vector<Line>& out, const Line* from) {
        if (!included_.insert(path).second) return; // each file is read once
        auto source = read_(path);
        if (!source) {
            if (from) error(*from, std::format("There's no file '{}'.", path));
            else s_.errors.push_back({path, 0, "This game has no scripts/main.vnscript, where every game starts."});
            return;
        }
        s_.hash = fnv1a(*source, s_.hash);
        readSource(path, *source, indent, out);
    }

    void readSource(const std::string& name, std::string_view source, int baseIndent, std::vector<Line>& out) {
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

    // -- Walking blocks ----------------------------------------------------------

    // Commands may start with a capital letter ("If", "Show"); such a line only counts as a command
    // if it works as one, so narration like "If only I knew." stays text.
    void block(const std::vector<Line>& lines) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const Line& line = lines[i];
            const std::string head = headOf(line.text);
            const bool exact = !capitalized(line.text);
            const bool hasBlock = !line.children.empty();
            if (head == "if" && (exact || hasBlock)) {
                i = ifChain(lines, i);
            } else if ((head == "else" || head == "elif") && (exact || hasBlock)) {
                if (phase_ == Phase::Story) error(line, "This 'else' has no 'if' right above it at the same indentation.");
                block(line.children);
            } else if (head == "choice" && (exact || hasBlock)) {
                menu(line);
            } else if (head == "message" && hasBlock) {
                messageBlock(line);
            } else if (head == "together" && (exact || hasBlock) && afterWords(line.text, 1).empty()) {
                together(line);
            } else if (std::ranges::contains(kDefinitions, head) && !asText_.contains(&line) && !isRecordMark(line.text, head)) {
                if (phase_ == Phase::Definitions) defs_.push_back(&line);
            } else {
                if (phase_ == Phase::Story) statement(line);
                block(line.children); // indentation under an ordinary line is just layout
            }
        }
    }

    // if / else if / elif / else; returns the index of the chain's last line.
    std::size_t ifChain(const std::vector<Line>& lines, std::size_t i) {
        std::vector<std::size_t> exits;
        for (;; ++i) {
            const Line& line = lines[i];
            const std::string head = headOf(line.text);
            const bool isElse = head == "else" && headOf(afterWords(line.text, 1)) != "if";
            std::string condition(isElse ? "" : afterWords(line.text, head == "else" ? 2 : 1));
            if (condition.ends_with(':')) condition.pop_back();
            std::optional<std::size_t> branch;
            if (phase_ == Phase::Story) {
                if (isElse && !trim(afterWords(line.text, 1)).empty() && trim(afterWords(line.text, 1)) != ":")
                    error(line, "'else' takes nothing after it.");
                if (!isElse) branch = emit(cmd::Jump{.unless = formula(line, condition)}, line);
                if (line.children.empty())
                    error(line, "Nothing is indented under this line.");
            }
            block(line.children);

            bool more = false;
            if (!isElse && i + 1 < lines.size()) {
                const std::string next = headOf(lines[i + 1].text);
                more = (next == "else" || next == "elif") && (!capitalized(lines[i + 1].text) || !lines[i + 1].children.empty());
            }
            if (phase_ == Phase::Story) {
                if (more) exits.push_back(emit(cmd::Jump{}, line));
                if (branch) jumpAt(*branch).target = s_.code.size();
            }
            if (!more) break;
        }
        for (auto pc : exits) jumpAt(pc).target = s_.code.size();
        return i;
    }

    // choice [prompt] [settings] with one indented option per line; each option's own lines run when
    // it's picked.
    void menu(const Line& line) {
        if (phase_ == Phase::Definitions) {
            for (const auto& option : line.children) block(option.children);
            return;
        }
        cmd::Menu m;
        std::string prompt = unquote(choiceSettings(line, afterWords(line.text, 1), m));
        if (prompt.ends_with(':')) prompt.pop_back();
        checkTextOf(line, prompt);
        const bool meanwhile = m.lines > 0; // up while the story goes on: each option comes back when its lines are done
        if (meanwhile && !prompt.empty())
            error(line, "A choice that's up for some lines has no question.");
        m.prompt = prompt;
        const std::size_t at = emit(std::move(m), line);
        std::vector<cmd::Choice> choices;
        std::vector<std::size_t> exits;   // jumps to the end of the choice
        std::vector<std::size_t> skipped; // options without lines or arrow: continue after the choice
        for (const auto& option : line.children) {
            auto [choice, label] = this->option(option, prompt);
            choice.label = label;
            if (!option.children.empty()) {
                choice.target = s_.code.size();
                block(option.children);
                if (!label.empty()) emit(cmd::Jump{.label = label}, option);
                else if (meanwhile) emit(cmd::Return{}, option);
                else exits.push_back(emit(cmd::Jump{}, option));
            } else if (label.empty()) {
                if (meanwhile) choice.target = emit(cmd::Return{}, option); // nothing to do: back to the story
                else skipped.push_back(choices.size());
            }
            choices.push_back(std::move(choice));
        }
        for (auto pc : exits) jumpAt(pc).target = s_.code.size();
        for (auto k : skipped) choices[k].target = s_.code.size();
        if (choices.empty())
            error(line, "This choice has no options.");
        auto& emitted = std::get<cmd::Menu>(s_.code[at].command);
        emitted.choices = std::move(choices);
        emitted.after = s_.code.size();
    }

    // An option of a choice or an answer to a message: "Text [(if CONDITION) | (needs CONDITION: what)]
    // [-> label]", and the label it goes to. `question`: its choice's question, or its message.
    std::pair<cmd::Choice, std::string> option(const Line& option, std::string_view question) {
        std::string text(trim(option.text));
        std::string label;
        std::optional<Expr> condition;
        if (text.ends_with(':') && !isQuoted(text)) text.pop_back();
        if (const auto arrow = text.rfind("->"); arrow != std::string::npos && !isQuoted(text)) {
            label = lower(trim(std::string_view(text).substr(arrow + 2)));
            text = std::string(trim(std::string_view(text).substr(0, arrow)));
            if (!isName(label)) error(option, std::format("'{}' isn't a label name.", label));
        }
        bool shownLocked = false;
        std::string note;
        if (const auto open = text.rfind("(if "); text.ends_with(')') && open != std::string::npos && !isQuoted(text)) {
            condition = formula(option, std::string_view(text).substr(open + 4, text.size() - open - 5));
            text = std::string(trim(std::string_view(text).substr(0, open)));
        } else if (const auto needs = text.rfind("(needs "); text.ends_with(')') && needs != std::string::npos && !isQuoted(text)) {
            // (needs CONDITION) or (needs CONDITION: what it needs): shown but locked until then
            std::string_view inside = std::string_view(text).substr(needs + 7, text.size() - needs - 8);
            std::size_t colon = std::string_view::npos;
            for (std::size_t k = 0, quotes = 0; k < inside.size(); ++k) {
                if (inside[k] == '"') ++quotes;
                else if (inside[k] == ':' && quotes % 2 == 0) colon = k;
            }
            if (colon != std::string_view::npos) {
                note = unquote(trim(inside.substr(colon + 1)));
                inside = trim(inside.substr(0, colon));
                checkTextOf(option, note);
            }
            condition = formula(option, inside);
            shownLocked = true;
            text = std::string(trim(std::string_view(text).substr(0, needs)));
        }
        text = unquote(text);
        checkTextOf(option, text);
        cmd::Choice choice{.text = text, .condition = std::move(condition), .shownLocked = shownLocked, .note = std::move(note),
                           .id = fnv1a(text, fnv1a(question, fnv1a(currentLabel_, fnv1a("choice"))))};
        return {std::move(choice), std::move(label)};
    }

    // together, with lines indented under it: they all show at once, each in its own textbox (a
    // character's textbox=, or the textbox now), and the story waits for the last.
    void together(const Line& line) {
        if (phase_ == Phase::Definitions) return block(line.children);
        const std::size_t first = s_.code.size();
        block(line.children);
        std::vector<cmd::Say*> says;
        for (std::size_t pc = first; pc < s_.code.size(); ++pc)
            if (auto* say = std::get_if<cmd::Say>(&s_.code[pc].command)) says.push_back(say);
        if (says.size() < 2) return error(line, "'together' needs two lines or more indented under it.");
        for (std::size_t k = 0; k + 1 < says.size(); ++k) says[k]->together = true;
    }

    // message NAME: text, with the answers the player can send back indented under it. An answer's
    // own lines run when it's sent, and the story then goes on where it was; -> label goes there
    // instead (after its lines).
    void messageBlock(const Line& line) {
        if (phase_ == Phase::Definitions) {
            for (const auto& option : line.children) block(option.children);
            return;
        }
        auto c = command(line, "message", true);
        if (!c) return;
        const std::string text = std::get<cmd::Message>(*c).text;
        const std::size_t at = emit(std::move(*c), line);
        const std::size_t past = emit(cmd::Jump{}, line); // the story goes past the answers' lines
        std::vector<cmd::Choice> replies;
        for (const Line& option : line.children) {
            auto [reply, label] = this->option(option, text);
            reply.target = s_.code.size();
            reply.label = label;
            block(option.children);
            emit(label.empty() ? Command{cmd::Return{}} : Command{cmd::Jump{.label = label}}, option);
            replies.push_back(std::move(reply));
        }
        jumpAt(past).target = s_.code.size();
        std::get<cmd::Message>(s_.code[at].command).replies = std::move(replies);
    }

    // The settings at the end of a choice's line (optional=yes, screen=NAME, for N seconds); returns
    // the rest, the question.
    std::string_view choiceSettings(const Line& line, std::string_view text, cmd::Menu& m) {
        std::vector<std::string_view> w; // the words, as parts of `text`
        for (std::size_t i = 0; i < text.size();) {
            const auto start = text.find_first_not_of(" \t", i);
            if (start == std::string_view::npos) break;
            const auto end = std::min(text.size(), text.find_first_of(" \t", start));
            w.push_back(text.substr(start, end - start));
            i = end;
        }
        auto cut = [&](std::size_t n) { // the last n words aren't the question
            text = trim(text.substr(0, static_cast<std::size_t>(w[w.size() - n].data() - text.data())));
            w.resize(w.size() - n);
        };
        bool returns = false; // return= was written
        while (!w.empty()) {
            const std::string last = lower(w.back());
            const bool lines = last == "lines" || last == "line";
            const std::size_t unit = lines || last == "seconds" || last == "second" ? 1 : 0; // for N [seconds] | for N lines
            if (last.starts_with("optional=")) {
                if (!set(m.optional, boolean(line, last.substr(9), "optional"))) return text;
                cut(1);
            } else if (last.starts_with("return=")) {
                if (!set(m.returns, boolean(line, last.substr(7), "return"))) return text;
                returns = true;
                cut(1);
            } else if (last.starts_with("screen=")) {
                m.screen = last.substr(7);
                if (!screenLines_.contains(m.screen)) {
                    error(line, std::format("There's no screen called '{}'.", m.screen));
                    return text;
                }
                cut(1);
            } else if (w.size() >= 2 + unit && lower(w[w.size() - 2 - unit]) == "for" && parseNumber(w[w.size() - 1 - unit])) {
                const float n = std::max(0.0f, *parseNumber(w[w.size() - 1 - unit]));
                if (lines) m.lines = static_cast<int>(std::lround(n));
                else m.seconds = n;
                cut(2 + unit);
            } else {
                break;
            }
        }
        if (m.lines > 0 && m.seconds > 0.0f) error(line, "A choice is up for some seconds or for some lines, not both.");
        if (returns && m.lines == 0) error(line, "return= only goes with a choice that's up for some lines.");
        return text;
    }

    // -- Story lines ---------------------------------------------------------------

    void statement(const Line& line) {
        const bool lenient = capitalized(line.text);
        const std::string verb = commandOf(line.text, true);
        if (verb == "label") {
            if (!attempt(lenient, [&] { label(line); })) say(line);
            return;
        }
        if (verb.empty() || isBlockWord(verb)) return say(line); // real blocks were handled by block()
        std::optional<Command> c;
        if (!attempt(lenient, [&] { c = command(line, verb, true); })) return say(line);
        if (c) emit(std::move(*c), line);
    }

    void label(const Line& line) {
        std::string name = lower(afterWords(line.text, 1));
        if (name.ends_with(':')) name.pop_back();
        if (!isName(name)) return error(line, std::format("'{}' can't be a label name (one word of letters, numbers, _ and -).", name));
        if (auto [it, fresh] = s_.labels.emplace(name, s_.code.size()); !fresh)
            error(line, std::format("There are two labels called '{}'.", name));
        else if (name == "start" && line.file != builtinFiles_)
            error(line, "label start belongs in main.vnscript, where the story begins.");
        currentLabel_ = name;
        lineCounts_.clear();
    }

    // "Name: text", "Name expression: text" or narration.
    void say(const Line& line) {
        const std::string_view t = line.text;
        // A screen's part written where the story is (not indented under a screen line): a mistake, not narration.
        if (!isQuoted(t)) {
            const std::string head = lower(t.substr(0, t.find(' ')));
            static constexpr std::string_view kPartSettings[]{" x=", " y=", " w=", " h=", " action=", " style=", " bg=", " image=", " keys="};
            if (std::ranges::find(kKinds, std::string_view(head), &KindSpec::name) != kKinds.end() &&
                std::ranges::any_of(kPartSettings, [&](std::string_view key) { return t.contains(key); }))
                return error(line, std::format("'{}' is a screen part: it goes indented under a screen line.", head));
        }
        cmd::Say say;
        const auto colon = t.find(':');
        const bool speakerColon = colon != std::string_view::npos && colon > 0 && (colon + 1 == t.size() || t[colon + 1] == ' ');
        std::string prefix(speakerColon ? trim(t.substr(0, colon)) : std::string_view{});
        const auto nameWords = std::ranges::count(prefix, ' ') + 1;
        if (isQuoted(t) || !speakerColon || nameWords > 3 || prefix.find_first_of(",!?\"{};") != std::string::npos) {
            say.text = unquote(t);
        } else {
            say.text = unquote(t.substr(colon + 1));
            if (prefix.ends_with(')') && prefix.find('(') != std::string::npos) { // Aiko (happy):
                const auto open = prefix.rfind('(');
                say.expression = std::string(trim(std::string_view(prefix).substr(open + 1, prefix.size() - open - 2)));
                prefix = std::string(trim(std::string_view(prefix).substr(0, open)));
            }
            const auto space = prefix.rfind(' ');
            if (s_.characters.contains(lower(prefix))) {
                say.character = lower(prefix);
            } else if (say.expression.empty() && space != std::string::npos && s_.characters.contains(lower(prefix.substr(0, space)))) {
                say.character = lower(prefix.substr(0, space)); // Aiko happy:
                say.expression = prefix.substr(space + 1);
            } else {
                say.speaker = prefix; // someone who isn't a defined character
            }
            if (say.text.empty()) error(line, std::format("{} doesn't say anything.", prefix));
            if (!say.character.empty()) say.voiceId = std::format("{}_{}", currentLabel_, ++lineCounts_[say.character]);
        }
        checkTextOf(line, say.text);
        say.id = fnv1a(say.text, fnv1a(say.character.empty() ? say.speaker : say.character));
        emit(std::move(say), line);
    }

    // -- Commands (story lines and button actions) ----------------------------------

    std::optional<Command> command(const Line& line, const std::string& verb, bool story, std::string_view text = {}) {
        if (text.empty()) text = line.text;
        const std::string_view rest = afterWords(text, verb.find(' ') == std::string::npos ? 1 : 2);
        auto cursor = words(line, rest);
        if (!cursor) return std::nullopt;
        Cursor& c = *cursor;
        // The command, once nothing is left after it.
        auto finished = [&](Command result) -> std::optional<Command> {
            if (!c.done()) return fail(line, std::format("'{}' doesn't belong in this {} line.", c.peek().text, verb));
            return result;
        };
        auto screenName = [&](std::string_view what) -> std::optional<std::string> {
            const auto name = c.done() || c.at("with") || c.at("and") ? std::nullopt : c.next(); // (open with fade...: no screen yet)
            if (!name) return fail(line, std::format("Which screen to {} is missing.", what));
            if (!screenLines_.contains(lower(*name))) return fail(line, std::format("There's no screen called '{}'.", *name));
            return lower(*name);
        };

        if (verb == "scene") { // scene PICTURE | scene COLOUR [bottom=COLOUR] [with ...]
            const auto name = c.next();
            if (!name) return fail(line, "Which image to show is missing.");
            cmd::Scene scene{.image = *name};
            auto hex = [](SDL_Color col) { return std::format("#{:02x}{:02x}{:02x}{:02x}", col.r, col.g, col.b, col.a); };
            const auto col = parseColor(*name, &s_.palette);
            if (col) scene.image = hex(*col);
            if (const auto grad = parseGradient(*name, &s_.palette)) scene.image = hex(grad->first) + "-" + hex(grad->second); // (navy-black: fading down to another)
            if (c.atOption("bottom")) { // (bottom=: the same, as two words)
                const auto opt = c.option();
                if (!col) return fail(line, "A background that fades to a colour starts with one: scene black bottom=navy.");
                const auto bottom = color(line, opt->second);
                if (!bottom) return std::nullopt;
                scene.image = hex(*col) + "-" + hex(*bottom);
            }
            if (!transition(line, c, scene.transition, true)) return std::nullopt;
            return finished(scene);
        }
        if ((verb == "show" || verb == "hide") && c.take("screen")) { // show screen NAME / hide screen NAME
            const auto name = screenName(verb + " screen");
            if (!name) return std::nullopt;
            return finished(cmd::Screen{.name = *name, .show = verb == "show"});
        }
        if (verb == "hide" && c.take("textbox")) { // hide textbox [NAME]: its kept line goes (all of them without one)
            cmd::Textbox t{.hide = true};
            if (const auto name = c.next()) {
                t.screen = lower(*name);
                if (!screenLines_.contains(t.screen)) return fail(line, std::format("There's no screen called '{}'.", t.screen));
            }
            return finished(t);
        }
        if (verb == "hide" && c.take("ui")) {
            if (story) return fail(line, "'hide ui' only works in buttons and keys.");
            return finished(cmd::Ui{.verb = "hide ui"});
        }
        if (verb == "show") return show(line, c);
        if (verb == "hide" || verb == "move") { // hide NAME [with ...] / move NAME [to PLACE] [settings] [over N] [ease=] [and wait]
            std::string tag;
            if (const CharacterDef* def = takeCharacter(c)) tag = def->key;
            else if (auto name = c.next()) tag = lower(*name);
            else return fail(line, std::format("Who or what to {} is missing.", verb));
            if (verb == "hide") {
                cmd::Hide hide{.tag = tag, .transition = s_.config.hideTransition};
                if (!transition(line, c, hide.transition)) return std::nullopt;
                return finished(hide);
            }
            cmd::Move move{.tag = tag};
            while (!c.done()) {
                if (const Took t = timing(line, c, move.seconds, &move.ease, &move.wait, story); t != Took::No) {
                    if (t == Took::Bad) return std::nullopt;
                } else if (c.take("to") || c.take("at")) {
                    if (!place(line, c, move.props)) return std::nullopt;
                } else if (auto opt = c.option()) {
                    if (!stageProp(line, move.props, opt->first, opt->second)) return std::nullopt;
                } else {
                    return fail(line, std::format("'{}' doesn't belong in a move line.", c.peek().text));
                }
            }
            return move;
        }
        if (verb == "shake" || verb == "flash") { // shake [strength=N] [for N] / flash [COLOUR] [for N]
            const GameConfig& cfg = s_.config;
            cmd::Shake shake{.flash = verb == "flash", .strength = cfg.shakeStrength, .color = cfg.flashColor,
                             .seconds = verb == "flash" ? cfg.flashTime : cfg.shakeTime};
            while (!c.done()) {
                if (const Took t = timing(line, c, shake.seconds); t != Took::No) {
                    if (t == Took::Bad) return std::nullopt;
                } else if (auto col = shake.flash ? parseColor(c.peek().text, &s_.palette) : std::nullopt) {
                    shake.color = *col;
                    c.skip(1);
                } else if (!shake.flash && c.atOption("strength")) {
                    const auto n = number(line, c.option()->second, "strength");
                    if (!n) return std::nullopt;
                    shake.strength = *n;
                } else if (shake.flash && c.atOption("color")) {
                    const auto col2 = color(line, c.option()->second);
                    if (!col2) return std::nullopt;
                    shake.color = *col2;
                } else {
                    return fail(line, std::format("'{}' doesn't belong in this {} line.", c.peek().text, verb));
                }
            }
            shake.seconds = std::max(0.01f, shake.seconds);
            return shake;
        }
        if (verb == "effect") { // effect NAME... [amount=50%] [fade 2] [for N seconds] | effect none [fade 2]
            cmd::Effect e;
            bool none = false;
            while (!c.done()) {
                if (const Took t = amount(line, c, "fade", e.fade); t != Took::No) {
                    if (t == Took::Bad) return std::nullopt;
                } else if (c.take("for")) {
                    const auto n = c.number();
                    if (!n) return fail(line, "'for' needs a number of seconds.");
                    e.seconds = std::max(0.0f, *n);
                } else if (c.atOption("amount")) {
                    const auto n = number(line, c.option()->second, "amount");
                    if (!n) return std::nullopt;
                    e.amount = std::clamp(*n, 0.0f, 1.0f);
                } else if (c.take("none")) {
                    none = true;
                } else if (const auto look = std::ranges::find(kStageLooks, lower(c.peek().text)); look != kStageLooks.end()) {
                    e.looks |= 1u << (look - kStageLooks.begin());
                    c.skip(1);
                } else {
                    return fail(line, std::format("'{}' isn't an effect.", c.peek().text));
                }
            }
            if (none && e.looks != 0) return fail(line, "'effect none' can't go with other effects.");
            if (!none && e.looks == 0) return fail(line, "Which effect is missing.");
            if (none && e.seconds > 0.0f) return fail(line, "'effect none' doesn't last for a time.");
            return e;
        }
        if (verb == "jump" || verb == "call") {
            if (c.at("screen")) return fail(line, std::format("{} goes to a label, not a screen.", verb));
            const auto label = c.next();
            if (!label || !isName(*label)) return fail(line, std::format("{} needs a label name.", verb));
            return finished(cmd::Jump{.label = lower(*label), .call = verb == "call"});
        }
        if (verb == "return") return finished(cmd::Return{});
        if (verb == "end") return finished(cmd::End{});
        if (verb == "set" || verb == "add" || verb == "subtract" || verb == "toggle") return change(line, verb, rest);
        if (verb == "unlock" || verb == "ending" || verb == "achievement" || verb == "tip") { // unlock PICTURE / ending NAME / ...
            const RecordKind kind = verb == "unlock" ? RecordKind::Gallery : verb == "ending" ? RecordKind::Ending
                                  : verb == "achievement" ? RecordKind::Achievement : RecordKind::Tip;
            const auto name = c.next();
            if (!name) return fail(line, std::format("{} needs a name.", verb));
            if (!s_.record(kind, *name))
                return fail(line, std::format("There's no {} called '{}'.",
                                              kind == RecordKind::Gallery ? "gallery picture" : kRecordKinds[static_cast<std::size_t>(kind)], *name));
            return finished(cmd::Record{.kind = kind, .name = *name});
        }
        if (verb == "music" || verb == "sound" || verb == "voice") return play(line, verb, c);
        if (verb == "message") { // message NAME: text, or the player's own: message to NAME: text
            const auto colon = rest.find(':');
            std::string who(colon == std::string_view::npos ? std::string_view{} : trim(rest.substr(0, colon)));
            cmd::Message m{.text = unquote(colon == std::string_view::npos ? std::string_view{} : rest.substr(colon + 1))};
            if (const std::string first = lower(headOf(who)); first == "to" && who.size() > 3) {
                m.mine = true;
                who = std::string(trim(std::string_view(who).substr(3)));
            } else if (lower(who) == "me") {
                return fail(line, "The player's own messages need who they go to.");
            }
            if (who.empty() || m.text.empty())
                return fail(line, "A message needs who it's from and what it says.");
            if (s_.characters.contains(lower(who))) m.character = lower(who);
            else m.sender = who;
            checkTextOf(line, m.text);
            return m;
        }
        if (verb == "phone") { // phone off / phone on
            const auto word = c.next();
            const std::string w = lower(word.value_or(""));
            if ((w != "on" && w != "off") || !c.done())
                return fail(line, "phone can be on or off.");
            return cmd::Phone{.on = w == "on"};
        }
        if (verb == "stop") { // stop music | stop sound [NAME] | stop voice | stop weather, then [fade 2]
            cmd::Play stop;
            if (c.take("weather")) {
                cmd::Weather calm;
                if (amount(line, c, "fade", calm.fade) == Took::Bad) return std::nullopt;
                return finished(calm);
            }
            if (c.take("music")) stop.track = cmd::Play::Track::Music;
            else if (c.take("voice")) stop.track = cmd::Play::Track::Voice;
            else if (!c.take("sound")) return fail(line, "stop goes with music, sound, voice or weather.");
            if (stop.track == cmd::Play::Track::Sound && !c.done() && !c.at("fade") && !c.atOption("fade")) stop.channel = lower(*c.next());
            if (amount(line, c, "fade", stop.fade) == Took::Bad) return std::nullopt;
            return finished(stop);
        }
        if (verb == "wait") {
            if (c.done()) return cmd::Wait{};
            const auto n = c.number();
            if (!n) return fail(line, "wait takes a number of seconds, or nothing.");
            return finished(cmd::Wait{.seconds = std::max(0.0f, *n)});
        }
        if (verb == "ask") return ask(line, c);
        if (verb == "weather") return weather(line, c);
        if (verb == "camera") return camera(line, c, story);
        if (verb == "textbox") { // textbox NAME (or just "textbox": back to the usual one)
            cmd::Textbox t{.screen = lower(c.next().value_or("say"))};
            if (!screenLines_.contains(t.screen)) return fail(line, std::format("There's no screen called '{}' to use as the textbox.", t.screen));
            return finished(t);
        }
        if (verb == "new page") return finished(cmd::Textbox{.newPage = true});
        if (verb == "chapter" || verb == "chart") { // chapter TITLE / chart TITLE (the rest of the line; quotes are optional)
            std::string title = unquote(trim(rest));
            if (title.empty())
                return fail(line, verb == "chapter" ? "The chapter's title is missing."
                                                    : "The flowchart's name for this place is missing.");
            checkTextOf(line, title);
            if (verb == "chart") return cmd::Mark{.title = std::move(title)};
            return cmd::Chapter{.title = std::move(title)};
        }
        if (verb == "movie") { // movie NAME [loop] [skip=no] [volume 80%]
            cmd::Movie m{.file = c.next().value_or("")};
            if (m.file.empty()) return fail(line, "Which video to play is missing.");
            while (!c.done()) {
                if (const Took t = amount(line, c, "volume", m.volume); t != Took::No) {
                    if (t == Took::Bad) return std::nullopt;
                } else if (c.take("loop")) {
                    m.loop = true;
                } else if (c.atOption("skip")) {
                    const auto b = boolean(line, c.option()->second, "skip");
                    if (!b) return std::nullopt;
                    m.skippable = *b;
                } else {
                    return fail(line, std::format("'{}' doesn't belong in a movie line.", c.peek().text));
                }
            }
            if (m.loop && !m.skippable) return fail(line, "A movie with loop and skip=no would never end.");
            return m;
        }

        // Menu and engine commands.
        cmd::Ui ui{.verb = verb};
        if (verb == "notify" || verb == "answer") { // the rest of the line (quotes are optional)
            ui.arg = unquote(rest, true);
            if (verb == "notify" && ui.arg.empty()) return fail(line, "The message to show is missing.");
            checkTextOf(line, ui.arg);
            return ui;
        }
        if (verb == "open") { // open SCREEN or open https://...
            if (const std::string target = unquote(rest, true); target.find("://") != std::string::npos || target.starts_with("www."))
                return ui.arg = target, ui;
            const auto name = screenName("open");
            if (!name || !transition(line, c, ui.transition)) return std::nullopt;
            if (ui.transition.kind != TransitionKind::None && ui.transition.kind != TransitionKind::Fade)
                return fail(line, "A screen opens at once, or with fade (with fade to white).");
            auto thenFades = [&](Transition& t) { // then fade [SECONDS] [to COLOUR]: another fade after it
                while (t.kind == TransitionKind::Fade && c.take("then")) {
                    if (!c.take("fade")) return fail(line, "After then comes another fade (then fade 1 to black)."), false;
                    Transition more{.kind = TransitionKind::Fade, .duration = s_.config.fadeTime};
                    fadeRest(c, more);
                    t.then.push_back(std::move(more));
                }
                return true;
            };
            if (!thenFades(ui.transition)) return std::nullopt;
            if (c.at("and") && c.left() > 1 && lower(c.peek(1).text) == "close") { // and close with fade ...: how it goes when it closes
                c.skip(2);
                if (!c.at("with")) return fail(line, "After and close comes how it closes: and close with fade 1.");
                if (!transition(line, c, ui.closing)) return std::nullopt;
                if (ui.closing.kind != TransitionKind::Fade) return fail(line, "A screen closes with fade (and close with fade to white).");
                if (!thenFades(ui.closing)) return std::nullopt;
            }
            ui.arg = *name;
            return finished(ui);
        }
        if (verb == "setting") { // setting NAME VALUE: a player setting (the window's size: bigger or smaller; the language: next, previous or one by name)
            const auto name = c.next();
            if (!name) return fail(line, "Which setting to change is missing.");
            const std::string setting = lower(*name);
            if (!std::ranges::contains(kSettingNames, setting)) return fail(line, std::format("There's no player setting called '{}'.", *name));
            const auto value = c.next();
            if (!value) return fail(line, std::format("What to set {} to is missing.", setting));
            const std::string word = lower(*value);
            std::optional<int> n;
            if (setting == "window_size" && (word == "bigger" || word == "smaller")) n = word == "bigger" ? 1 : -1;
            else if (setting == "window_size") { // or a width and a height, in pixels
                std::optional<float> h;
                if (!c.done())
                    if (const auto second = c.next()) h = parseNumber(*second);
                const auto w = parseNumber(word);
                if (!w || !h || *w < 1.0f || *h < 1.0f) return fail(line, "The window's size is a width and a height in pixels, or bigger or smaller.");
                ui.arg = std::format("window_size {} {}", static_cast<int>(*w), static_cast<int>(*h));
                return finished(ui);
            }
            else if (setting == "language" && (word == "next" || word == "previous")) n = word == "next" ? 1 : -1;
            else if (setting == "language") { // (the game's own, or a translation's name or file: known once the game runs)
                ui.arg = std::format("language {}", word);
                return finished(ui);
            }
            else n = settingNumber(line, setting, setting, word);
            if (!n) return std::nullopt;
            ui.arg = std::format("{} {}", setting, *n);
            return finished(ui);
        }
        if (verb == "start" && !c.done()) ui.arg = lower(*c.next());
        return finished(ui);
    }

    // A player setting's value as a number: a number, yes / no, or (scaling, mouths) its word.
    std::optional<int> settingNumber(const Line& line, const std::string& setting, std::string_view key, std::string_view value) {
        if (setting == "mouths") return oneOf<int>(line, key, value, kMouths);
        if (const auto b = parseBool(value)) return *b ? 1 : 0;
        if (setting == "scaling" && !parseNumber(value)) return oneOf<int>(line, key, value, kScalings);
        if (const auto x = number(line, value, key)) return value.ends_with('%') ? static_cast<int>(std::lround(*x * 100.0f)) : static_cast<int>(*x); // (80%: 80)
        return std::nullopt;
    }

    // show NAME [expression] [at POSITION] [as TAG] [x= y= zoom= ...] [with TRANSITION]
    std::optional<Command> show(const Line& line, Cursor& c) {
        cmd::Show show{.transition = s_.config.showTransition};
        auto keyword = [&] { return c.at("at") || c.at("to") || c.at("as") || c.at("with") || c.peek().text.find('=') != std::string::npos; };
        if (const CharacterDef* def = takeCharacter(c)) {
            show.character = show.tag = def->key;
            if (!c.done() && !keyword()) show.expression = *c.next();
        } else if (auto name = c.next()) {
            show.image = *name;
            show.tag = lower(*name);
        } else {
            return fail(line, "Who or what to show is missing.");
        }
        while (!c.done()) {
            if (c.take("at") || c.take("to")) {
                if (!place(line, c, show.props)) return std::nullopt;
            } else if (c.take("as")) {
                const auto tag = c.next();
                if (!tag) return fail(line, "'as' needs a name after it.");
                show.tag = lower(*tag);
            } else if (c.at("with")) {
                if (!transition(line, c, show.transition)) return std::nullopt;
            } else if (auto opt = c.option()) {
                if (!stageProp(line, show.props, opt->first, opt->second)) return std::nullopt;
            } else {
                return fail(line, std::format("'{}' doesn't belong in a show line.", c.peek().text));
            }
        }
        return show;
    }

    // The character named by the next words, if any (names may have spaces: "Mr. Tanaka").
    const CharacterDef* takeCharacter(Cursor& c) {
        for (std::size_t n = std::min<std::size_t>(3, c.left()); n >= 1; --n) {
            std::string name;
            for (std::size_t k = 0; k < n; ++k) name += (k ? " " : "") + c.peek(k).text;
            if (const CharacterDef* def = s_.character(lower(name))) {
                c.skip(n);
                return def;
            }
        }
        return nullptr;
    }

    // at / to POSITION: a name from a "position" line, or a number from -1 (left) to 1 (right).
    bool place(const Line& line, Cursor& c, StageProps& props) {
        const auto word = c.next();
        if (!word) return error(line, "'at' needs a place on the stage."), false;
        if (auto n = stageCoordinate(*word, false)) props.x = *n;
        else if (auto it = positions_.find(lower(*word)); it != positions_.end()) props.x = it->second;
        else {
            return error(line, std::format("There's no position called '{}'.", *word)), false;
        }
        return true;
    }

    // with fade|dissolve|wipe|slide|none [seconds] [to COLOUR] (scenes: with wipe PICTURE, shaped by it)
    bool transition(const Line& line, Cursor& c, Transition& t, bool scene = false) {
        if (!c.take("with")) return true;
        const auto kind = oneOf<TransitionKind>(line, "A transition", c.next().value_or(""), kTransitions);
        if (!kind) return false;
        t = {.kind = *kind,
             .duration = *kind == TransitionKind::Dissolve ? s_.config.dissolveTime
                       : *kind == TransitionKind::None     ? 0.0f
                                                           : s_.config.fadeTime};
        if (*kind == TransitionKind::Wipe && !c.done() && !parseNumber(c.peek().text) && !c.at("at") && !c.at("as") && !c.at("to") &&
            !c.peek().text.contains('=')) {
            if (!scene) return error(line, "Only a scene line can wipe with an image."), false;
            t.shape = pictureName(*c.next());
        }
        fadeRest(c, t);
        return true;
    }
    // A transition's length and colour: [SECONDS] [to COLOUR] (or the other way round).
    void fadeRest(Cursor& c, Transition& t) {
        bool timed = false;
        if (auto n = c.number()) t.duration = std::max(0.0f, *n), timed = true;
        const std::size_t through = c.at("to") ? 1 : 0; // "with fade to white"
        if (c.left() > through)
            if (auto col = parseColor(c.peek(through).text, &s_.palette)) {
                t.color = *col;
                c.skip(through + 1);
            }
        if (!timed)
            if (auto n = c.number()) t.duration = std::max(0.0f, *n); // ("with fade to white 2")
    }

    // x= y= zoom= opacity= rotate= flip= layer= tint=
    bool stageProp(const Line& line, StageProps& p, const std::string& key, const std::string& value) {
        if (key == "flip") {
            const auto b = boolean(line, value, key);
            p.flip = b;
            return b.has_value();
        }
        if (key == "tint") return (p.tint = color(line, value)).has_value();
        static constexpr auto keys = std::to_array<std::string_view>({"x", "y", "zoom", "opacity", "rotate", "layer"});
        if (!std::ranges::contains(keys, key))
            return error(line, std::format("'{}' isn't a picture setting.", key)), false;
        if (key == "x" || key == "y") {
            const auto n = stageCoordinate(value, key == "y");
            if (!n) return error(line, key == "x" ? std::format("x needs a number from -1 (the left edge) to 1 (the right edge), not '{}'.", value)
                                                  : std::format("y needs a number from -1 (the bottom) to 1 (the top), not '{}'.", value)),
                          false;
            (key == "x" ? p.x : p.y) = *n;
            return true;
        }
        const auto n = parseNumber(value);
        if (!n) return error(line, std::format("{} needs a number, not '{}'.", key, value)), false;
        if (key == "zoom") p.zoom = *n;
        else if (key == "opacity") p.opacity = *n;
        else if (key == "rotate") p.rotate = *n;
        else p.layer = static_cast<int>(*n);
        return true;
    }

    // set X to V | set X = V | add V to X | subtract V from X | toggle X
    std::optional<Command> change(const Line& line, const std::string& verb, std::string_view rest) {
        cmd::Set set{.op = verb == "add" ? '+' : verb == "subtract" ? '-' : verb == "toggle" ? '!' : '='};
        std::string_view value;
        if (verb == "set") {
            std::size_t i = 0;
            while (i < rest.size() && !std::isspace(static_cast<unsigned char>(rest[i])) && rest[i] != '=' && rest[i] != '+' && rest[i] != '-') ++i;
            set.variable = lower(rest.substr(0, i));
            value = trim(rest.substr(i));
            if (value.starts_with("+=") || value.starts_with("-=")) {
                set.op = value[0];
                value.remove_prefix(2);
            } else if (value.starts_with('=') && !value.starts_with("==")) {
                value.remove_prefix(1);
            } else if (lower(firstWord(value)) == "to") {
                value = afterWords(value, 1);
            } else {
                return fail(line, "set needs a name, 'to' and a value.");
            }
        } else if (verb == "toggle") {
            set.variable = lower(rest);
        } else {
            const std::string_view word = verb == "add" ? " to " : " from ";
            const auto at = lower(rest).rfind(word);
            if (at == std::string::npos)
                return fail(line, verb == "add" ? "add needs a value, 'to' and a name."
                                                : "subtract needs a value, 'from' and a name.");
            value = rest.substr(0, at);
            set.variable = lower(trim(rest.substr(at + word.size())));
        }
        if (!isVariable(set.variable))
            return fail(line, std::format("'{}' can't be a variable name (one word of letters, numbers and _).",
                                          set.variable));
        if (set.op != '!') {
            set.value = formula(line, value);
            if (!set.value) return std::nullopt;
        }
        return set;
    }

    // music FILE [once] | sound FILE [loop] [as CHANNEL] | voice FILE [sync | no sync], then [fade 2] [volume 50%]
    std::optional<Command> play(const Line& line, std::string_view verb, Cursor& c) {
        cmd::Play p{.track = verb == "music" ? cmd::Play::Track::Music : verb == "voice" ? cmd::Play::Track::Voice : cmd::Play::Track::Sound,
                    .loop = verb == "music"};
        const auto file = c.next();
        if (!file)
            return fail(line, "Which file to play is missing.");
        if (lower(*file) == "stop") return fail(line, "'stop' isn't a file to play.");
        p.file = *file;
        while (!c.done()) {
            if (const Took t = amount(line, c, "fade", p.fade); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
            } else if (const Took v = amount(line, c, "volume", p.volume); v != Took::No) {
                if (v == Took::Bad) return std::nullopt;
            } else if (verb == "music" && c.take("once")) {
                p.loop = false;
            } else if (verb == "sound" && c.take("loop")) {
                p.loop = true;
            } else if (verb == "sound" && c.take("as")) {
                const auto channel = c.next();
                if (!channel) return fail(line, "'as' needs a name after it.");
                p.channel = lower(*channel);
            } else if (verb == "voice" && c.take("sync")) { // the next line types out with the voice
                p.sync = true;
            } else if (verb == "voice" && c.at("no") && c.left() >= 2 && lower(c.peek(1).text) == "sync") {
                c.skip(2);
                p.sync = false;
            } else {
                return fail(line, std::format("'{}' doesn't belong in this {} line.", c.peek().text, verb));
            }
        }
        if (p.track == cmd::Play::Track::Sound && p.loop && p.channel.empty()) p.channel = lower(*file); // so it can be stopped
        return p;
    }

    // weather rain|snow|petals|leaves|embers [amount=150%] [speed=2] [wind=-0.5] [color=C] [image=NAME] [behind=yes] [fade 2]
    std::optional<Command> weather(const Line& line, Cursor& c) {
        static constexpr auto kinds = std::to_array<std::string_view>({"rain", "snow", "petals", "leaves", "embers"});
        cmd::Weather w{.kind = lower(c.next().value_or(""))};
        if (!std::ranges::contains(kinds, std::string_view(w.kind)))
            return fail(line, std::format("'{}' isn't a kind of weather.", w.kind));
        while (!c.done()) {
            if (const Took t = amount(line, c, "fade", w.fade); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
                continue;
            }
            const auto opt = c.option();
            if (!opt) return fail(line, std::format("'{}' doesn't belong in a weather line.", c.peek().text));
            const auto& [key, value] = *opt;
            if (key == "image") {
                w.image = pictureName(value);
            } else if (key == "color") {
                if (!(w.color = color(line, value))) return std::nullopt;
            } else if (key == "behind") {
                const auto b = boolean(line, value, key);
                if (!b) return std::nullopt;
                w.behind = *b;
            } else if (key == "amount" || key == "speed" || key == "wind") {
                const auto n = number(line, value, key);
                if (!n) return std::nullopt;
                (key == "amount" ? w.amount : key == "speed" ? w.speed : w.wind) = *n;
            } else {
                return fail(line, std::format("weather has no setting '{}'.", key));
            }
        }
        w.amount = std::clamp(w.amount, 0.0f, 10.0f);
        w.speed = std::clamp(w.speed, 0.0f, 10.0f);
        return w;
    }

    // camera [zoom=150%] [x=30%] [y=40%] [rotate=5] [over N] [ease=out] [and wait] | camera reset ...
    std::optional<Command> camera(const Line& line, Cursor& c, bool story) {
        cmd::Camera cam{.reset = c.take("reset")};
        while (!c.done()) {
            if (const Took t = timing(line, c, cam.seconds, &cam.ease, &cam.wait, story); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
                continue;
            }
            const auto opt = c.option();
            if (!opt) return fail(line, std::format("'{}' doesn't belong in a camera line.", c.peek().text));
            const auto& [key, value] = *opt;
            if (key == "x" || key == "y") {
                const auto n = stageCoordinate(value, key == "y");
                if (!n) return fail(line, std::format("{} needs a place on the stage from -1 to 1.", key));
                (key == "x" ? cam.x : cam.y) = *n;
            } else if (key == "zoom" || key == "rotate") {
                const auto n = number(line, value, key);
                if (!n) return std::nullopt;
                (key == "zoom" ? cam.zoom : cam.rotate) = key == "zoom" ? std::max(0.1f, *n) : *n;
            } else {
                return fail(line, std::format("camera has no setting '{}'.", key));
            }
        }
        return cam;
    }

    // ask NAME "Prompt" [default=TEXT] [max=N]
    std::optional<Command> ask(const Line& line, Cursor& c) {
        cmd::Ask a;
        const auto name = c.next();
        if (!name || !isVariable(lower(*name))) return fail(line, "ask needs a variable name and a question.");
        a.variable = lower(*name);
        while (!c.done()) {
            if (auto opt = c.option()) {
                if (opt->first == "default") a.fallback = opt->second;
                else if (opt->first == "max" && parseNumber(opt->second)) a.max = std::max(1, static_cast<int>(*parseNumber(opt->second)));
                else return fail(line, std::format("ask has no setting '{}'.", opt->first));
            } else {
                a.prompt += (a.prompt.empty() ? "" : " ") + *c.next();
            }
        }
        if (a.prompt.empty()) return fail(line, "The question to ask is missing.");
        checkTextOf(line, a.prompt);
        return a;
    }

    // Button, key and hook actions: commands separated by ';'.
    Commands actions(const Line& line, std::string_view text) {
        Commands out;
        char quote = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            const char ch = i < text.size() ? text[i] : ';';
            if (quote) {
                if (ch == quote) quote = 0;
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == ';') {
                const std::string_view part = trim(text.substr(start, i - start));
                start = i + 1;
                if (part.empty()) continue;
                const std::string verb = commandOf(part, false);
                if (verb.empty()) {
                    error(line, std::format("'{}' isn't something a button can do.", firstWord(part)));
                    continue;
                }
                auto c = command(line, verb, false, part);
                if (!c) continue;
                const auto* jump = std::get_if<cmd::Jump>(&*c);
                const auto* ui = std::get_if<cmd::Ui>(&*c);
                const std::string label = jump ? jump->label : ui && ui->verb == "start" ? ui->arg : std::string{};
                if (!label.empty() && !s_.labels.contains(label)) error(line, std::format("There's no label called '{}'.", label));
                out.push_back(std::move(*c));
            }
        }
        return out;
    }

    // -- Definitions ----------------------------------------------------------------

    void definitions() {
        // Colours first so everything else can use them, then the rest in a fixed order; within a
        // group, lines take effect in the order they were written (so the last one wins).
        static constexpr std::array<std::array<std::string_view, 2>, 13> order{{
            {"color", ""}, {"config", ""}, {"position", ""}, {"font", ""}, {"atlas", "picture"}, {"character", ""}, {"style", ""},
            {"control", ""}, {"gallery", "ending"}, {"song", "achievement"}, {"tip", "animation"}, {"screen", ""}, {"variable", ""},
        }};
        for (const auto& group : order)
            for (const Line* line : defs_)
                if (const std::string kind = headOf(line->text); kind == group[0] || kind == group[1]) {
                    const bool lenient = capitalized(line->text) && line->children.empty();
                    if (!attempt(lenient, [&] { define(*line, kind); })) asText_.insert(line);
                }
    }

    void define(const Line& line, std::string_view kind) {
        auto cursor = words(line, afterWords(line.text, 1));
        if (!cursor) return;
        Cursor& c = *cursor;
        if (kind != "screen" && kind != "atlas" && kind != "animation" && !line.children.empty())
            error(line, "Lines indented under this one don't belong to it.");

        if (kind == "color") {
            const auto name = c.next();
            const auto value = c.next();
            const auto col = value ? parseColor(*value, &s_.palette) : std::nullopt;
            if (!name || !col || !c.done()) return error(line, "A color line needs a name and a colour.");
            s_.palette.insert_or_assign(lower(*name), *col);
        } else if (kind == "variable") { // variable NAME [VALUE] ["what it's for"]
            const auto name = c.next();
            const bool valid = name && !name->empty() && (letterByte((*name)[0]) || (*name)[0] == '_') &&
                               std::ranges::all_of(*name, [](char ch) { return letterOrDigitByte(ch) || ch == '_' || ch == '.'; });
            if (!valid) return error(line, "A variable line needs the variable's name: one word (letters, numbers and _).");
            Value start = 0;
            if (!c.done()) {
                const ScriptWord& w = c.peek();
                if (w.quoted) start = w.text;
                else if (const auto e = Expr::parse(w.text)) start = e->eval({});
                else return error(line, std::format("'{}' isn't a value a variable can start with: a number, yes or no, or \"text\".", w.text));
                c.skip(1);
            }
            if (!c.done() && c.peek().quoted) c.skip(1); // (what it's for)
            if (!c.done()) return error(line, "A variable line is its name, the value it starts with, and what it's for, in quotes.");
            s_.startValues.insert_or_assign(lower(*name), std::move(start));
        } else if (kind == "position") { // position NAME VALUE (or NAME=VALUE)
            const auto [name, value] = nameValue(c);
            const auto n = value ? stageCoordinate(*value, false) : std::nullopt;
            if (!name || !n || !c.done()) return error(line, "A position line needs a name and a number from -1 to 1.");
            positions_.insert_or_assign(lower(*name), *n);
        } else if (kind == "font") { // font [NAME] FILE [STYLE]
            const auto a = c.next(), b = c.next(), style = c.next();
            if (!a || !c.done())
                return error(line, "A font line needs a font file.");
            if (b) s_.fonts.insert_or_assign(lower(*a), Script::FontFile{*b, style.value_or("")});
            else s_.fonts.insert_or_assign("default", Script::FontFile{*a, ""});
        } else if (kind == "config") { // config SETTING VALUE... (or SETTING=VALUE)
            if (line.file > builtinFiles_) // (the built-in menus' come before main.vnscript)
                return error(line, "config lines belong in main.vnscript, with the game's other settings.");
            const auto [key, value] = nameValue(c);
            if (!key) return error(line, "A config line needs a setting and a value.");
            std::vector<std::string> values;
            if (value) values.push_back(*value);
            while (auto v = c.next()) values.push_back(*v);
            config(line, lower(*key), values);
        } else if (kind == "atlas") {
            atlas(line, c);
        } else if (kind == "picture") {
            subPicture(line, c);
        } else if (kind == "animation") {
            animation(line, c);
        } else if (kind == "character") {
            character(line, c);
        } else if (kind == "style") {
            const auto name = c.next();
            if (!name) return error(line, "A style line needs a name.");
            styleLines_[lower(*name)].push_back(&line);
        } else if (kind == "control") {
            control(line, c);
        } else if (const auto record = std::ranges::find(kRecordKinds, kind); record != kRecordKinds.end()) {
            this->record(line, static_cast<RecordKind>(record - kRecordKinds.begin()), c);
        } else if (kind == "screen") {
            const auto name = c.next();
            if (!name) return error(line, "A screen line needs a name.");
            screenLines_.insert_or_assign(lower(*name), &line);
        }
    }

    // atlas NAME [image=FILE] [columns=N] [like=ATLAS], with the frames' names indented below it:
    //     expressions neutral happy sad
    //     eyes open half closed
    //     mouth closed half open
    void atlas(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name || !isName(*name))
            return error(line, "An atlas line needs a name.");
        Atlas a{.name = lower(*name), .image = *name};
        std::vector<std::pair<std::string, std::string>> options;
        while (!c.done()) {
            auto opt = c.option();
            if (!opt) return error(line, std::format("'{}' doesn't belong in an atlas line.", c.peek().text));
            if (opt->first == "like") { // the same frames as an atlas above (its settings come first)
                const auto other = std::ranges::find(s_.atlases, lower(opt->second), &Atlas::name);
                if (other == s_.atlases.end())
                    return error(line, std::format("There's no atlas called '{}' above this line.", opt->second));
                a.columns = other->columns;
                a.expressions = other->expressions;
                a.eyes = other->eyes;
                a.mouth = other->mouth;
            } else {
                options.push_back(std::move(*opt));
            }
        }
        for (const auto& [key, value] : options) {
            if (key == "image") {
                a.image = pictureName(value);
            } else if (key == "columns") {
                const auto n = number(line, value, key);
                if (!n) return;
                if (*n < 1.0f) return error(line, std::format("columns needs a number from 1 up, not {}.", *n));
                a.columns = static_cast<int>(*n);
            } else {
                return error(line, std::format("Atlases have no setting '{}'.", key));
            }
        }
        for (const Line& part : line.children) {
            std::string text = part.text; // "eyes: open, half, closed" works too
            std::ranges::replace(text, ',', ' ');
            std::ranges::replace(text, ':', ' ');
            auto w = words(part, text);
            if (!w) continue;
            const std::string what = lower(*w->next());
            std::vector<std::string>* list = what == "expressions" ? &a.expressions : what == "eyes" ? &a.eyes : what == "mouth" ? &a.mouth : nullptr;
            if (!list) {
                error(part, std::format("'{}' isn't part of an atlas.", what));
                continue;
            }
            // The names, then (for eyes and mouth) image= x= y= when they're a part drawn on top.
            std::vector<std::string> names;
            std::optional<Atlas::Part> layer;
            while (!w->done()) {
                if (auto opt = w->option()) {
                    if (list == &a.expressions || (opt->first != "image" && opt->first != "x" && opt->first != "y")) {
                        error(part, std::format("'{}' has no setting '{}'.", what, opt->first));
                        break;
                    }
                    if (!layer) layer.emplace();
                    if (opt->first == "image") layer->image = pictureName(opt->second);
                    else if (const auto n = number(part, opt->second, opt->first)) (opt->first == "x" ? layer->x : layer->y) = *n;
                } else {
                    names.push_back(lower(*w->next()));
                }
            }
            if (!names.empty()) *list = std::move(names); // (like= may have given the names already)
            if (list->empty()) error(part, std::format("'{}' needs a name for each frame.", what));
            if (layer && layer->image.empty()) error(part, std::format("Which image holds the {} is missing.", what));
            if (list == &a.eyes) a.eyesPart = layer;
            if (list == &a.mouth) a.mouthPart = layer;
        }
        std::erase_if(s_.atlases, [&](const Atlas& other) { return other.name == a.name; }); // the last one wins
        s_.atlases.push_back(std::move(a));
    }

    // animation NAME [loop], with a step on each line under it:
    //     x=-10 opacity=40% over 1 second ease=out
    //     wait 0.5 seconds
    void animation(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name || !isName(*name)) return error(line, "An animation line needs a name.");
        Animation a{.name = lower(*name)};
        while (!c.done())
            if (!c.take("loop")) return error(line, std::format("'{}' doesn't belong in an animation line (its steps go on the lines under it).", c.peek().text));
            else a.loop = true;
        for (const Line& part : line.children) {
            auto w = words(part, part.text);
            if (!w) continue;
            Animation::Step step;
            bool easeSaid = false; // (a fade without one is linear, as a screen's fade is)
            bool ok = true;
            while (ok && !w->done()) {
                if (w->take("wait")) {
                    const auto n = w->number();
                    if (!n) error(part, "'wait' needs a number of seconds."), ok = false;
                    else step.seconds = std::max(0.0f, *n);
                    continue;
                }
                if (w->peek().text.starts_with("ease=")) easeSaid = true;
                if (const Took t = timing(part, *w, step.seconds, &step.ease); t != Took::No) {
                    ok = t == Took::Yes;
                    continue;
                }
                const auto opt = w->option();
                if (!opt) {
                    error(part, std::format("'{}' isn't part of an animation's step.", w->peek().text)), ok = false;
                    break;
                }
                const auto& [key, value] = *opt;
                std::optional<float>* to = key == "x" ? &step.x : key == "y" ? &step.y : key == "opacity" ? &step.opacity : key == "zoom" ? &step.zoom
                                         : key == "rotate" ? &step.rotate : nullptr;
                if (key == "color") step.color = color(part, value), ok = step.color.has_value();
                else if (key == "fade") { // fade=COLOUR: the screen covered with it by the end; fade=none: uncovered again
                    if (lower(value) == "none") step.fade = SDL_Color{0, 0, 0, 0};
                    else step.fade = color(part, value), ok = step.fade.has_value();
                }
                else if (!to) error(part, std::format("An animation's step can change x, y, opacity, zoom, rotate, color and fade, not '{}'.", key)), ok = false;
                else if ((key == "x" || key == "y") && lower(value) == "home") *to = std::numeric_limits<float>::quiet_NaN(); // (its own place)
                else if (const auto n = number(part, value, key)) *to = *n;
                else ok = false;
            }
            if (step.fade && !easeSaid) step.ease = Ease::Linear;
            if (ok) a.steps.push_back(step);
        }
        if (a.steps.empty()) return error(line, "An animation needs its steps, each on a line under it (like: opacity=0.5 over 1 second).");
        s_.animations.insert_or_assign(a.name, std::move(a)); // the last one wins
    }

    // picture NAME image=FILE x=N y=N w=N h=N: a part of a picture, shown by its own name.
    void subPicture(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name || !isName(*name)) return error(line, "A picture line needs a name.");
        SubPicture p{.name = lower(*name)};
        while (!c.done()) {
            const auto opt = c.option();
            if (!opt) return error(line, std::format("'{}' doesn't belong in a picture line.", c.peek().text));
            const auto& [key, value] = *opt;
            float* at = key == "x" ? &p.rect.x : key == "y" ? &p.rect.y : key == "w" ? &p.rect.w : key == "h" ? &p.rect.h : nullptr;
            if (key == "image") p.image = pictureName(value);
            else if (!at) return error(line, std::format("A picture line has no setting '{}'.", key));
            else if (const auto n = number(line, value, key)) *at = *n;
            else return;
        }
        if (p.image.empty() || p.rect.w <= 0.0f || p.rect.h <= 0.0f)
            return error(line, "A picture line needs the picture it's cut from and the part: image=FILE x= y= w= h=.");
        if (lower(p.image) == p.name) return error(line, "A picture can't be cut from itself: give it a name of its own.");
        std::erase_if(s_.subPictures, [&](const SubPicture& other) { return other.name == p.name; }); // the last one wins
        s_.subPictures.push_back(std::move(p));
    }

    // "NAME VALUE" or "NAME=VALUE" (config and position lines).
    static std::pair<std::optional<std::string>, std::optional<std::string>> nameValue(Cursor& c) {
        auto name = c.next();
        if (!name) return {};
        if (const auto eq = name->find('='); eq != std::string::npos && eq > 0)
            return {name->substr(0, eq), name->substr(eq + 1)};
        return {name, c.next()};
    }

    // A place on stage as written: -1 to 1 around the middle, y going up (-1 the left edge or the
    // bottom). Returns the engine's own 0 to 1 from the top left.
    std::optional<float> stageCoordinate(std::string_view text, bool vertical) const {
        const auto n = text.ends_with('%') ? std::nullopt : parseNumber(text);
        if (!n) return std::nullopt;
        return vertical ? (1.0f - *n) * 0.5f : (*n + 1.0f) * 0.5f;
    }

    // control NAME keys="space mouse_left" action="..." [everywhere=yes] [if=CONDITION]
    void control(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name || !isName(*name)) return error(line, "A control line needs a name.");
        ControlLine control{.line = &line, .name = lower(*name)};
        while (!c.done()) {
            const auto opt = c.option();
            if (!opt) return error(line, std::format("'{}' doesn't belong in a control line.", c.peek().text));
            const auto& [key, value] = *opt;
            if (key == "keys" || key == "key") {
                std::string list(value);
                std::ranges::replace(list, ',', ' ');
                auto keyWords = splitWords(list);
                for (const auto& w : keyWords.value_or(std::vector<ScriptWord>{})) {
                    const auto chord = parseKeyChord(w.text);
                    if (!chord)
                        return error(line, std::format("'{}' isn't a key name.", w.text));
                    control.keys.push_back(*chord);
                }
            } else if (key == "action") {
                control.action = value;
            } else if (key == "everywhere") {
                control.everywhere = boolean(line, value, key).value_or(false);
            } else if (key == "if") {
                control.condition = value;
            } else {
                return error(line, std::format("Controls have no setting '{}'.", key));
            }
        }
        if (control.action.empty()) return error(line, "A control needs action=.");
        std::erase_if(controlLines_, [&](const ControlLine& other) { return other.name == control.name; }); // the last one wins
        controlLines_.push_back(std::move(control));
    }

    // character NAME setting=value ...
    void character(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name) return error(line, "A character line needs a name.");
        const GameConfig& cfg = s_.config;
        CharacterDef def{.key = lower(*name), .name = *name, .image = imageName(*name), .babbleSpeed = cfg.babbleSpeed,
                         .blink = cfg.blink, .blinkEvery = cfg.blinkEvery, .talk = cfg.talk, .talkSpeed = cfg.talkSpeed};
        std::optional<bool> faceBlink;
        std::optional<Talk> faceTalk;
        while (!c.done()) {
            const auto opt = c.option();
            if (!opt) return error(line, std::format("'{}' doesn't belong in a character line.", c.peek().text));
            const auto& [key, value] = *opt;
            // Each sets its field when the value is right, and says whether it was.
            auto flag = [&](bool& out) { return set(out, boolean(line, value, key)); };
            auto within = [&](float& out, float lo, float hi) {
                const auto n = number(line, value, key);
                return set(out, n ? std::optional(std::clamp(*n, lo, hi)) : std::nullopt);
            };
            auto sync = [&](FaceSync& out) -> bool { // yes, no, or what it copies: face_sync=expression,blink
                if (const auto b = parseBool(value)) return out = {*b, *b, *b}, true;
                out = {false, false, false};
                for (auto&& part : std::views::split(std::string_view(value), ',')) {
                    const std::string_view written = trim(std::string_view(part.begin(), part.end()));
                    const std::string word = lower(written);
                    if (word == "expression") out.expression = true;
                    else if (word == "blink") out.blink = true;
                    else if (word == "talk") out.talk = true;
                    else return error(line, std::format("face_sync can be yes, no, or what the face copies (expression, blink, talk), not '{}'.", written)), false;
                }
                return true;
            };
            bool ok = true;
            if (key == "color" || key == "text_color") {
                const auto col = color(line, value);
                ok = col.has_value();
                if (col) (key == "color" ? def.color : def.textColor) = *col;
            }
            else if (key == "image") def.image = pictureName(value);
            else if (key == "reveal") ok = (def.reveal = oneOf<Reveal>(line, key, value, kReveals)).has_value();
            else if (key == "textbox") def.textbox = lower(value);
            else if (key == "blip") def.blip = value;
            else if (key == "voices") { // yes: audio/voices/<name>/, or audio/voices/<value>/
                const auto b = parseBool(value);
                def.voices = b == false ? "" : std::format("voices/{}", b ? imageName(*name) : pictureName(value));
            }
            else if (key == "babble") { // high, medium, low, deep, no, or a pitch in Hz
                static constexpr std::pair<std::string_view, float> heights[]{{"high", 250.0f}, {"medium", 190.0f}, {"low", 135.0f}, {"deep", 105.0f}};
                const auto height = std::ranges::find(heights, lower(value), &std::pair<std::string_view, float>::first);
                if (height != std::end(heights)) def.babble = height->second;
                else if (parseBool(value) == false) def.babble = 0.0f;
                else if (const auto hz = parseNumber(value); hz && *hz >= 50.0f && *hz <= 600.0f) def.babble = *hz;
                else return error(line, std::format("babble can be high, medium, low, deep, no or a pitch, not '{}'.", value));
            }
            else if (key == "babble_speed") ok = within(def.babbleSpeed, 0.25f, 4.0f);
            else if (key == "blink") ok = flag(def.blink);
            else if (key == "blink_every") ok = within(def.blinkEvery, 0.5f, 60.0f);
            else if (key == "talk") ok = set(def.talk, talk(line, key, value));
            else if (key == "talk_speed") ok = within(def.talkSpeed, 0.5f, 20.0f);
            else if (key == "face") def.face = value; // a picture name, or yes / no
            else if (key == "face_side") {
                if (!isName(value)) return error(line, std::format("face_side is one word, not '{}'.", value));
                def.faceSide = lower(value);
            }
            else if (key == "face_sync") ok = sync(def.faceSync);
            else if (key == "face_flip") ok = flag(def.faceFlip);
            else if (key == "face_blink") ok = (faceBlink = boolean(line, value, key)).has_value();
            else if (key == "face_talk") ok = (faceTalk = talk(line, key, value)).has_value();
            else return error(line, std::format("Characters have no setting '{}'.", key));
            if (!ok) return;
        }
        if (const auto yes = parseBool(def.face)) def.face = *yes ? def.image + "_face" : ""; // face=yes: images/<image>_face.png
        else def.face = pictureName(def.face);
        def.faceBlink = faceBlink.value_or(def.blink); // the face does what the picture on stage does, unless told
        def.faceTalk = faceTalk.value_or(def.talk);
        if (!def.textbox.empty()) textboxes_.emplace_back(&line, def.textbox);
        s_.characters.insert_or_assign(def.key, std::move(def));
    }

    // Sets `out` to `value` when it has one; says whether it had.
    template <class T>
    static bool set(T& out, const std::optional<T>& value) {
        if (value) out = *value;
        return value.has_value();
    }

    // gallery PICTURE "Title" | ending NAME "Title" [hint="..."] | song NAME "Title" | achievement NAME "Title" "Description" [hidden]
    // | tip NAME "Title" "Description"
    void record(const Line& line, RecordKind kind, Cursor& c) {
        const auto name = c.next();
        if (!name || (kind != RecordKind::Gallery && !isName(*name))) return error(line, std::format("{} needs a name.", kRecordKinds[static_cast<std::size_t>(kind)]));
        Script::Record r{.name = *name, .title = c.next().value_or(*name)};
        if (r.title.empty()) r.title = *name; // (a title left empty, "": the name)
        while (!c.done()) {
            if (kind == RecordKind::Achievement && c.take("hidden")) r.hidden = true;
            else if (kind == RecordKind::Ending && c.atOption("hint")) r.text = c.option()->second;
            else if ((kind == RecordKind::Achievement || kind == RecordKind::Tip) && r.text.empty() && (c.peek().quoted || !c.peek().text.contains('=')))
                r.text = *c.next();
            else return error(line, std::format("'{}' doesn't belong in this {} line.", c.peek().text, kRecordKinds[static_cast<std::size_t>(kind)]));
        }
        auto& list = s_.lists[static_cast<std::size_t>(kind)];
        std::erase_if(list, [&](const Script::Record& other) { return sameName(other.name, r.name); }); // the last one wins
        list.push_back(std::move(r));
    }

    // config SETTING VALUE (one value, or two for resolution, cursor_hotspot and the transitions)
    void config(const Line& line, const std::string& key, const std::vector<std::string>& v) {
        GameConfig& c = s_.config;
        struct Number {
            std::string_view key;
            float GameConfig::*field;
            float lo, hi;
        };
        static constexpr std::array numbers{
            Number{"skip_delay", &GameConfig::skipDelay, 0.0f, 10.0f},     Number{"auto_per_char", &GameConfig::autoPerChar, 0.0f, 10.0f},
            Number{"fade_time", &GameConfig::fadeTime, 0.0f, 60.0f},       Number{"dissolve_time", &GameConfig::dissolveTime, 0.0f, 60.0f},
            Number{"sprite_height", &GameConfig::spriteHeight, 0.05f, 10.0f}, Number{"move_time", &GameConfig::moveTime, 0.0f, 60.0f},
            Number{"shake_strength", &GameConfig::shakeStrength, 0.0f, 1000.0f}, Number{"shake_time", &GameConfig::shakeTime, 0.01f, 60.0f},
            Number{"flash_time", &GameConfig::flashTime, 0.01f, 60.0f},    Number{"notify_time", &GameConfig::notifyTime, 0.1f, 600.0f},
            Number{"music_crossfade", &GameConfig::musicCrossfade, 0.0f, 60.0f},
            Number{"glide_speed", &GameConfig::glideSpeed, 0.0f, 10.0f},      Number{"glide_distance", &GameConfig::glideDistance, 0.05f, 10.0f},
            Number{"scrollbar_size", &GameConfig::scrollbarSize, 1.0f, 64.0f}, Number{"fade_size", &GameConfig::fadeSize, 0.0f, 1000.0f},
            Number{"blip_every", &GameConfig::blipEvery, 0.01f, 10.0f},    Number{"blip_volume", &GameConfig::blipVolume, 0.0f, 1.0f},
            Number{"babble_speed", &GameConfig::babbleSpeed, 0.25f, 4.0f}, Number{"blink_every", &GameConfig::blinkEvery, 0.5f, 60.0f},
            Number{"talk_speed", &GameConfig::talkSpeed, 0.5f, 20.0f},     Number{"phone_typing", &GameConfig::phoneTyping, 0.0f, 100.0f},
            Number{"music_while_speaking", &GameConfig::musicWhileSpeaking, 0.0f, 1.0f},
        };
        struct Whole {
            std::string_view key;
            int GameConfig::*field;
            int lo, hi;
        };
        static constexpr std::array wholes{
            Whole{"save_pages", &GameConfig::savePages, 1, 99}, Whole{"save_slots", &GameConfig::saveSlots, 1, 60},
            Whole{"history_limit", &GameConfig::historyLimit, 1, 100000}, Whole{"rollback", &GameConfig::rollback, 0, 10000},
            Whole{"preload", &GameConfig::preload, 0, 1000}, Whole{"image_memory", &GameConfig::imageMemory, 0, 65536},
        };
        static const std::map<std::string_view, bool GameConfig::*> flags{
            {"autosave", &GameConfig::autosave}, {"voice_sync", &GameConfig::voiceSync}, {"developer", &GameConfig::developer}, {"glide", &GameConfig::glide},
            {"blink", &GameConfig::blink}, {"resizable", &GameConfig::resizable}};
        static const std::map<std::string_view, SDL_Color GameConfig::*> colors{
            {"flash_color", &GameConfig::flashColor}, {"narrator_color", &GameConfig::narratorColor},
            {"letterbox_color", &GameConfig::letterboxColor}};
        static const std::map<std::string_view, std::string GameConfig::*> texts{
            {"title", &GameConfig::title}, {"version", &GameConfig::version}, {"save_id", &GameConfig::saveId}, {"icon", &GameConfig::icon},
            {"cursor", &GameConfig::cursor}, {"language", &GameConfig::language}};
        static constexpr std::pair<std::string_view, bool> filters[]{{"smooth", false}, {"pixel", true}};
        static constexpr std::pair<std::string_view, SDL_Keymod> holds[]{
            {"ctrl", SDL_KMOD_CTRL}, {"shift", SDL_KMOD_SHIFT}, {"alt", SDL_KMOD_ALT}, {"none", 0}};

        const std::size_t needs = key == "resolution" && v.size() == 1 && v[0].contains('x') ? 1
                                : key == "resolution" || key == "cursor_hotspot"               ? 2
                                                                                               : 1;
        if (v.size() < needs) return error(line, std::format("config {} needs {} value{}.", key, needs, needs == 1 ? "" : "s"));
        const std::string& value = v[0];
        auto transition = [&](Transition& out) {
            const auto kind = oneOf<TransitionKind>(line, key, value, kTransitions);
            if (!kind) return;
            out.kind = *kind;
            if (v.size() > 1)
                if (auto x = number(line, v[1], key)) out.duration = std::max(0.0f, *x);
        };

        if (const auto n = std::ranges::find(numbers, std::string_view(key), &Number::key); n != numbers.end()) {
            if (auto x = number(line, value, key)) c.*(n->field) = std::clamp(*x, n->lo, n->hi);
        } else if (const auto w = std::ranges::find(wholes, std::string_view(key), &Whole::key); w != wholes.end()) {
            if (auto x = number(line, value, key)) c.*(w->field) = std::clamp(static_cast<int>(*x), w->lo, w->hi);
        } else if (const auto f = flags.find(key); f != flags.end()) {
            set(c.*(f->second), boolean(line, value, key));
        } else if (const auto col = colors.find(key); col != colors.end()) {
            set(c.*(col->second), color(line, value));
        } else if (const auto t = texts.find(key); t != texts.end()) {
            c.*(t->second) = value;
        } else if (key == "resolution") { // resolution 1280 720 (or 1280x720)
            const bool joined = v.size() == 1;
            const auto width = number(line, joined ? value.substr(0, value.find('x')) : value, key);
            const auto height = number(line, joined ? value.substr(value.find('x') + 1) : v[1], key);
            if (width && height) {
                c.width = std::clamp(static_cast<int>(*width), 160, 7680);
                c.height = std::clamp(static_cast<int>(*height), 120, 4320);
            }
        } else if (key == "cursor_hotspot") { // cursor_hotspot X Y
            const auto x = number(line, value, key), y = number(line, v[1], key);
            if (x && y) c.cursorHotspot = {static_cast<int>(*x), static_cast<int>(*y)};
        } else if (key == "image_filter") {
            set(c.pixelArt, oneOf<bool>(line, key, value, filters));
        } else if (key == "skip_hold") {
            set(c.skipHold, oneOf<SDL_Keymod>(line, key, value, holds));
        } else if (key == "talk") { // yes, voice or no: for every character without talk=
            set(c.talk, talk(line, key, value));
        } else if (key == "reveal") {
            set(c.reveal, oneOf<Reveal>(line, key, value, kReveals));
        } else if (key == "show_transition" || key == "hide_transition") {
            transition(key == "show_transition" ? c.showTransition : c.hideTransition);
        } else if (key.starts_with("default.")) { // a player setting's starting value: a number, yes / no, or (scaling, mouths) its word
            const std::string setting = key.substr(8);
            if (!std::ranges::contains(kSettingNames, setting) || setting == "window_size" || setting == "language")
                return error(line, std::format("There's no player setting called '{}'.", setting));
            if (const auto start = settingNumber(line, setting, key, value)) c.settingDefaults[setting] = *start;
        } else {
            error(line, std::format("There's no config setting called '{}'.", key));
        }
    }

    // -- Styles and screens -----------------------------------------------------------

    // Returns false when `key` is not a style property.
    bool styleProp(Style& s, std::string_view key, std::string_view value, const Line& line) {
        auto at = [&](float lo) { // a number, at least `lo`
            const auto n = number(line, value, key);
            return n ? std::optional(std::max(lo, *n)) : std::nullopt;
        };
        auto state = [&](std::string_view prefix) -> Style::State* {
            return prefix == "hover" ? &s.hover : prefix == "selected" ? &s.selected : prefix == "disabled" ? &s.disabled : nullptr;
        };
        const auto us = key.find('_');
        Style::State* st = us == std::string_view::npos ? nullptr : state(key.substr(0, us));
        const std::string_view field = st ? key.substr(us + 1) : key;

        if (st && (field == "color" || field == "bg" || field == "border")) { // hover_color= selected_bg= disabled_border=...
            const auto c = color(line, value);
            if (c) (field == "color" ? st->color : field == "bg" ? st->bg : st->border) = c;
            if (c && field == "bg") { // (navy-black: fading down to another)
                const auto grad = parseGradient(value, &s_.palette);
                st->bgBottom = grad ? std::optional(grad->second) : std::nullopt;
            }
        }
        else if (st && field == "image") st->image = std::string(value);
        else if (key == "font") s.font = lower(value);
        else if (key == "size") set(s.size, at(1.0f));
        else if (key == "color") {
            s.speakerColor = value == "speaker";
            if (!s.speakerColor) set(s.color, color(line, value));
        }
        else if (key == "bg") { // (navy-black: fading down to another)
            set(s.bg, color(line, value));
            const auto grad = parseGradient(value, &s_.palette);
            s.bgBottom = grad ? std::optional(grad->second) : std::nullopt;
        }
        else if (key == "border") set(s.border, color(line, value));
        else if (key == "image") s.image = std::string(value);
        else if (key == "align" || key == "valign") {
            if (auto a = toAlign(value)) (key == "align" ? s.align : s.valign) = *a;
            else error(line, std::format("{} can be left, center or right (top, center or bottom for valign), not '{}'.", key, value));
        }
        else if (key == "padding") set(s.padding, at(0.0f));
        else if (key == "opacity") {
            if (const auto n = number(line, value, key)) s.opacity = std::clamp(*n, 0.0f, 1.0f);
        }
        else if (key == "shadow") set(s.shadow.on, boolean(line, value, key));
        else if (key == "shadow_color") set(s.shadow.color, color(line, value));
        else if (key == "shadow_offset") set(s.shadow.offset, at(0.0f));
        else if (key == "line_spacing") set(s.lineSpacing, at(0.1f));
        else if (key == "textbox_opacity") set(s.textboxOpacity, boolean(line, value, key));
        else if (key == "text_size") set(s.textSize, boolean(line, value, key));
        else if (key == "hover_sound") s.hoverSound = std::string(value);
        else if (key == "click_sound") s.clickSound = std::string(value);
        else return false;
        return true;
    }

    std::optional<Style> style(const std::string& name, const Line& user, int depth = 0) {
        if (auto it = styles_.find(name); it != styles_.end()) return it->second;
        const auto raw = styleLines_.find(name);
        if (raw == styleLines_.end()) return std::nullopt;
        if (depth > 16) {
            error(user, std::format("Style '{}' is its own parent.", name));
            return Style{};
        }
        std::vector<std::pair<const Line*, std::pair<std::string, std::string>>> props;
        for (const Line* line : raw->second) {
            auto c = words(*line, afterWords(line->text, 2));
            if (!c) continue;
            while (!c->done()) {
                if (auto opt = c->option()) {
                    if (line->file < builtinFiles_) scaleBuiltIn(*opt);
                    props.emplace_back(line, std::move(*opt));
                } else {
                    error(*line, std::format("'{}' doesn't belong in a style line.", *c->next()));
                }
            }
        }
        std::string parent = name == "default" ? "" : "default";
        for (const auto& [line, kv] : props)
            if (kv.first == "parent") parent = lower(kv.second);
        Style s;
        if (!parent.empty()) {
            if (auto base = style(parent, *raw->second.front(), depth + 1)) s = *base;
            else if (parent != "default") error(*raw->second.front(), std::format("There's no style called '{}'.", parent));
        }
        for (const auto& [line, kv] : props)
            if (kv.first != "parent" && !styleProp(s, kv.first, kv.second, *line))
                error(*line, std::format("Styles have no setting '{}'.", kv.first));
        return styles_.emplace(name, s).first->second;
    }

    void screens() {
        for (const auto& [name, line] : screenLines_) {
            ScreenDef def{.name = name};
            auto c = words(*line, afterWords(line->text, 2));
            std::optional<bool> keyboard;
            while (c && !c->done()) {
                const auto opt = c->option();
                if (!opt) {
                    error(*line, std::format("'{}' doesn't belong in a screen line.", *c->next()));
                    continue;
                }
                const auto& [key, value] = *opt;
                if (key == "modal") def.modal = boolean(*line, value, key).value_or(true);
                else if (key == "keyboard") keyboard = boolean(*line, value, key);
                else if (key == "on_show") def.onShow = actions(*line, value);
                else if (key == "close_after") def.closeAfter = std::max(0.0f, number(*line, value, key).value_or(0.0f));
                else if (key == "on_hide") def.onHide = actions(*line, value);
                else if (key == "show_animation" || key == "hide_animation") {
                    if (const auto a = s_.animations.find(lower(value)); a != s_.animations.end())
                        (key == "show_animation" ? def.showAnimation : def.hideAnimation) = std::make_shared<const Animation>(a->second);
                    else error(*line, std::format("There's no animation called '{}'.", value));
                }
                else if (key == "keep") def.keep = boolean(*line, value, key).value_or(false);
                else if (key == "text_speed") def.textSpeed = std::max(0.05f, number(*line, value, key).value_or(1.0f));
                else if (key == "stop_music_on_open") def.stopOnOpen.music = boolean(*line, value, key).value_or(false);
                else if (key == "stop_music_on_close") def.stopOnClose.music = boolean(*line, value, key).value_or(false);
                else if (key == "stop_voice_on_open") def.stopOnOpen.voice = boolean(*line, value, key).value_or(false);
                else if (key == "stop_voice_on_close") def.stopOnClose.voice = boolean(*line, value, key).value_or(false);
                else error(*line, std::format("Screens have no setting '{}'.", key));
            }
            def.keyboard = keyboard.value_or(def.modal);
            elements(def, line->children, -1);
            s_.screens.insert_or_assign(name, std::move(def));
        }
        for (const auto& [line, screen] : textboxes_)
            if (!s_.screens.contains(screen)) error(*line, std::format("There's no screen called '{}' to use as a textbox.", screen));
    }

    // A screen's parts, each followed by the ones in it (indented under it: their places are in its box).
    void elements(ScreenDef& def, const std::vector<Line>& lines, int parent) {
        for (const Line& line : lines) {
            auto el = element(line);
            if (!el) continue;
            el->parent = parent;
            def.elements.push_back(std::move(*el));
            elements(def, line.children, static_cast<int>(def.elements.size()) - 1);
        }
    }

    std::optional<Element> element(const Line& line) {
        auto cursor = words(line, line.text);
        if (!cursor) return std::nullopt;
        Cursor& c = *cursor;
        const std::string kind = lower(*c.next());
        const auto spec = std::ranges::find(kKinds, std::string_view(kind), &KindSpec::name);
        if (spec == kKinds.end()) {
            error(line, std::format("'{}' isn't a screen part.", kind));
            return std::nullopt;
        }
        Element el{.kind = spec->kind, .file = line.file, .line = line.number};
        if (!c.done() && (c.peek().quoted || c.peek().text.find('=') == std::string::npos)) el.text = *c.next();
        std::vector<std::pair<std::string, std::string>> props;
        while (!c.done()) {
            if (auto opt = c.option()) props.push_back(std::move(*opt));
            else error(line, std::format("'{}' isn't a setting.", *c.next()));
        }
        if (line.file < builtinFiles_) // (written for 1280 x 720)
            for (auto& kv : props) scaleBuiltIn(kv);

        // Base style: style=NAME, else the style named after the element kind, else "default".
        std::string styleName(spec->style.empty() ? "default" : spec->style);
        if (!styleLines_.contains(styleName)) styleName = "default";
        for (const auto& [k, v] : props)
            if (k == "style") {
                styleName = lower(v);
                if (!styleLines_.contains(styleName)) error(line, std::format("There's no style called '{}'.", v));
            }
        el.style = style(styleName, line).value_or(Style{});

        auto allowed = [&](std::string_view key) {
            return std::ranges::contains(spec->options, key) || (spec->list && std::ranges::contains(kListOptions, key));
        };
        for (const auto& [k, v] : props) {
            if (k == "style" || k == "name") continue; // (name=: what the editor calls it)
            if (k == "animation" || (allowed(k) && k == "click_animation")) { // (any part; a button's when it's clicked)
                if (const auto a = s_.animations.find(lower(v)); a != s_.animations.end())
                    (k == "animation" ? el.animation : el.clickAnimation) = std::make_shared<const Animation>(a->second);
                else error(line, std::format("There's no animation called '{}'.", v));
            } else if (k == "x" || k == "y" || k == "w" || k == "h") { // (places from -1 to 1, shares of its box)
                if (auto n = number(line, v, k)) (k == "x" ? el.placeX : k == "y" ? el.placeY : k == "w" ? el.shareW : el.shareH) = *n;
            } else if (k == "if") {
                el.visible = formula(line, v);
            } else if (allowed(k) && k == "action") {
                el.actions = actions(line, v);
            } else if (allowed(k) && k == "drop") {
                el.drop = actions(line, v);
            } else if (allowed(k) && k == "confirm") {
                el.confirm = v;
            } else if (allowed(k) && std::ranges::contains(kFormulaOptions, k)) {
                if (auto e = formula(line, v)) el.exprs.insert_or_assign(k, std::move(*e));
            } else if (allowed(k) && k == "key") {
                el.key = parseKeyChord(v);
                if (!el.key) error(line, std::format("'{}' isn't a key name.", v));
            } else if (allowed(k) && (k == "enabled" || k == "selected")) {
                (k == "enabled" ? el.enabled : el.selected) = formula(line, v);
            } else if (allowed(k) && k.ends_with("_style")) {
                if (auto s = style(lower(v), line)) el.substyles.insert_or_assign(k, *s);
                else error(line, std::format("There's no style called '{}'.", v));
            } else if (allowed(k) && std::ranges::contains(kColorOptions, k)) {
                if (auto col = color(line, v)) el.colors.insert_or_assign(k, *col);
            } else if (allowed(k)) {
                el.props.insert_or_assign(k, k == "var" ? lower(v) : v);
            } else if (!styleProp(el.style, k, v, line)) {
                error(line, std::format("'{}' has no setting '{}'.", kind, k));
            }
        }
        if (el.kind == ElementKind::Text || el.kind == ElementKind::Button) checkTextOf(line, el.text);

        switch (el.kind) {
        case ElementKind::Key:
            el.key = parseKeyChord(el.text);
            if (!el.key) error(line, std::format("'{}' isn't a key name.", el.text));
            if (el.actions.empty()) error(line, "A key needs action=.");
            break;
        case ElementKind::Image: // (a picture by name, a colour, or a box of its style's background; one fading down with bottom=)
            if (el.text.empty() && !el.colors.contains("bottom") && el.style.bg.a == 0) error(line, "Which image to show is missing.");
            break;
        case ElementKind::Movie:
            if (el.text.empty()) error(line, "Which video to play is missing.");
            break;
        case ElementKind::Input:
        case ElementKind::Slider:
            if (el.kind == ElementKind::Slider && !el.option("setting").empty()) { // (a player setting: a volume, the text's speed...)
                const SettingControl* setting = findSettingControl(lower(el.option("setting")));
                if (!setting) error(line, std::format("'{}' isn't a player setting a slider can change.", el.option("setting")));
                if (!el.option("var").empty()) error(line, "A slider changes a variable (var=) or a player setting (setting=), not both.");
                break;
            }
            if (el.kind == ElementKind::Slider && el.option("var").empty()) error(line, "A slider needs var=, the variable it changes, or setting=, the player setting.");
            if (!el.option("var").empty() && !isVariable(el.option("var")))
                error(line, std::format("'{}' can't be a variable name.", el.option("var")));
            break;
        case ElementKind::Bar:
            if (!el.exprs.contains("value")) error(line, "A bar needs value=.");
            break;
        case ElementKind::Control:
            el.text = lower(el.text);
            if (std::ranges::none_of(controlLines_, [&](const ControlLine& known) { return known.name == el.text; }))
                error(line, std::format("There's no control called '{}'.", el.text));
            if (el.number("slot", 1.0f) < 1.0f) error(line, std::format("slot needs a number from 1 up, not {}.", el.number("slot", 1.0f)));
            break;
        default:
            break;
        }
        return el;
    }

    // A built-in screen's (or style's) text size, padding or bar, for a game of another size than the
    // 1280 x 720 they're written for (places and shares fit any size).
    void scaleBuiltIn(std::pair<std::string, std::string>& kv) const {
        const float sy = static_cast<float>(s_.config.height) / 720.0f;
        static constexpr std::string_view kSizes[]{"size", "padding", "bar"};
        if (sy == 1.0f || !std::ranges::contains(kSizes, kv.first)) return;
        if (const auto n = parseNumber(kv.second)) kv.second = std::format("{}", std::lround(*n * sy));
    }

    void bindings() {
        for (const auto& c : controlLines_)
            s_.controls.push_back({.name = c.name, .keys = c.keys, .actions = actions(*c.line, c.action), .everywhere = c.everywhere,
                                   .condition = c.condition.empty() ? std::nullopt : formula(*c.line, c.condition)});
    }

    // -- Labels -----------------------------------------------------------------------

    void resolve() {
        for (auto& ins : s_.code) {
            const Line where{.file = ins.file, .number = ins.line};
            auto fix = [&](const std::string& label, std::size_t& target) {
                if (auto pc = s_.findLabel(label)) target = *pc;
                else error(where, std::format("There's no label called '{}'.", label));
            };
            if (auto* jump = std::get_if<cmd::Jump>(&ins.command); jump && !jump->label.empty()) fix(jump->label, jump->target);
            if (auto* menu = std::get_if<cmd::Menu>(&ins.command))
                for (auto& choice : menu->choices)
                    if (!choice.label.empty() && choice.target == 0) fix(choice.label, choice.target); // (one with lines starts there)
        }
    }

    Script& s_;
    const FileReader& read_;
    Phase phase_ = Phase::Definitions;
    std::size_t builtinFiles_ = 0; // the built-in menus' files: main.vnscript is the next one
    std::set<std::string> included_;
    std::vector<const Line*> defs_;
    std::set<const Line*> asText_; // capitalized lines that didn't work as definitions
    // Places for "show NAME at PLACE": these, and the game's "position" lines.
    std::map<std::string, float, std::less<>> positions_{
        {"farleft", -1.0f}, {"left", -0.6f}, {"center", 0.0f}, {"middle", 0.0f}, {"right", 0.6f}, {"farright", 1.0f}};
    std::map<std::string, std::vector<const Line*>, std::less<>> styleLines_; // repeated definitions merge
    std::map<std::string, Style, std::less<>> styles_;
    std::map<std::string, const Line*, std::less<>> screenLines_;             // the last definition wins
    struct ControlLine {
        const Line* line = nullptr;
        std::string name;
        std::vector<KeyChord> keys;
        std::string action, condition;
        bool everywhere = false;
    };
    std::vector<ControlLine> controlLines_; // the last definition of a name wins
    std::vector<std::pair<const Line*, std::string>> textboxes_;              // character textbox screens to check
    std::string currentLabel_ = "line";                // the label the story lines being read are in
    std::map<std::string, int, std::less<>> lineCounts_; // each character's lines in it so far (voice ids)
};

} // namespace

// ---------------------------------------------------------------------------
// Script
// ---------------------------------------------------------------------------

Script Script::load(const std::string& entry, const FileReader& read, std::string_view builtin) {
    Script script;
    Parser(script, read).run(entry, builtin);
    std::ranges::stable_sort(script.errors, {}, [](const ScriptError& e) { return std::tie(e.file, e.line); });
    return script;
}

std::optional<std::size_t> Script::findLabel(std::string_view name) const {
    if (auto it = labels.find(name); it != labels.end()) return it->second;
    return std::nullopt;
}

const CharacterDef* Script::character(std::string_view key) const {
    auto it = characters.find(key);
    return it != characters.end() ? &it->second : nullptr;
}

const Control* Script::control(std::string_view name) const {
    auto it = std::ranges::find(controls, name, &Control::name);
    return it != controls.end() ? &*it : nullptr;
}

const Script::Record* Script::record(RecordKind kind, std::string_view name) const {
    const auto& list = records(kind);
    auto it = std::ranges::find_if(list, [&](const Record& r) { return sameName(r.name, name); });
    return it != list.end() ? &*it : nullptr;
}

bool Script::isRecordVariable(std::string_view name) {
    return std::ranges::any_of(kRecordKinds, [&](std::string_view kind) { return name.starts_with(std::format("persistent.{}.", kind)); });
}

const ScreenDef* Script::screen(std::string_view name) const {
    auto it = screens.find(name);
    return it != screens.end() ? &it->second : nullptr;
}

std::pair<std::string, std::size_t> Script::locate(std::size_t pc) const {
    std::pair<std::string, std::size_t> best{"", pc};
    std::size_t bestPc = 0;
    for (const auto& [name, at] : labels) {
        if (at <= pc && (best.first.empty() || at > bestPc || (at == bestPc && name < best.first))) {
            best = {name, pc - at};
            bestPc = at;
        }
    }
    return best;
}

std::string storyCommandOf(std::string_view line) {
    return commandOf(line, true);
}

std::string Script::where(const Instruction& ins) const {
    return std::format("{} line {}", ins.file < files.size() ? files[ins.file] : "?", ins.line);
}

// ---------------------------------------------------------------------------
// The built-in screens
// ---------------------------------------------------------------------------
// The player's built-in screens: just what a story needs to play (its lines and its choices). A
// game's own screens and controls replace these, and add the rest (a title screen, menus...).

std::string_view defaultUi() {
    return R"vnext(
control advance keys="space mouse_left return kp_enter pad_a" action=advance

style choice size=24 align=center valign=center bg=#16192ae1 border=#ffffff2d hover_bg=#545caaf0 hover_border=#96aaff selected_bg=#545caaf0 selected_border=#96aaff disabled_color=#9696a5

screen say modal=no
    image #141626fa x=0 y=-0.8642 w=0.9375 h=0.2639 border=#ffffff2d textbox_opacity=yes
    namebox x=-0.9 y=-0.3947 h=0.0639 bar=3 size=28 bg=#222644f5 padding=20 color=speaker textbox_opacity=yes
    dialogue x=0 y=-0.4667 w=0.8875 size=28 text_size=yes
    indicator x=0.8938 y=-0.8305 w=0.0141 h=0.0167 color=#96aaff

screen choice modal=no keyboard=yes
    choices x=0 y=0.7333 w=0.5625 h=0.5833 item_h=0.075 gap_x=0.0109 gap_y=0.0194
)vnext";
}

} // namespace vn
