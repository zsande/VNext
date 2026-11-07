#include "Script.hpp"
#include "System.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <expected>
#include <limits>
#include <optional>
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
            if (close == std::string_view::npos) return std::unexpected("a quote mark (\") is opened but never closed"_tr());
            w.text += line.substr(i + 1, close - i - 1);
            i = close + 1;
        }
        out.push_back(std::move(w));
    }
    return out;
}

int daysFromDate(int year, int month, int day) { // (the proleptic Gregorian calendar's days, from 1970)
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int ofEra = year - era * 400;
    const int ofYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    return era * 146097 + ofEra * 365 + ofEra / 4 - ofEra / 100 + ofYear - 719468;
}

std::array<int, 3> dateFromDays(int days) {
    days += 719468;
    const int era = (days >= 0 ? days : days - 146096) / 146097;
    const int ofEra = days - era * 146097;
    const int year = (ofEra - ofEra / 1460 + ofEra / 36524 - ofEra / 146096) / 365;
    const int ofYear = ofEra - (365 * year + year / 4 - year / 100);
    const int m = (5 * ofYear + 2) / 153;
    const int month = m < 10 ? m + 3 : m - 9;
    return {year + era * 400 + (month <= 2 ? 1 : 0), month, ofYear - (153 * m + 2) / 5 + 1};
}

// ---------------------------------------------------------------------------
// Scripts
// ---------------------------------------------------------------------------

namespace {

// A time of day as written (18:30, 6:30pm, 6:30 pm, 6pm, or an hour on its own: 8 is 8:00): minutes from midnight.
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

// The pieces of `text` between `by`s, each trimmed (empty ones left out).
std::vector<std::string_view> piecesOf(std::string_view text, char by = ' ') {
    std::vector<std::string_view> out;
    for (std::size_t at = 0; at <= text.size();) {
        const std::size_t end = std::min(text.find(by, at), text.size());
        if (const std::string_view piece = trim(text.substr(at, end - at)); !piece.empty()) out.push_back(piece);
        at = end + 1;
    }
    return out;
}

// A weekday's name (0 Monday), any case.
std::optional<int> weekdayNamed(std::string_view word) {
    for (std::size_t i = 0; i < kWeekdays.size(); ++i)
        if (lower(word) == lower(kWeekdays[i])) return static_cast<int>(i);
    return std::nullopt;
}

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
    {"scene", true, false}, {"if", true, false}, {"else", true, false}, {"choice", true, false},
    {"wait", true, false}, {"together", true, false},
    {"movie", true, false},
    {"phone", true, false}, {"chat", true, false}, {"time", true, false},
    // Both.
    {"background", true, true}, {"show", true, true}, {"hide", true, true},
    {"effect", true, true}, {"weather", true, true}, {"camera", true, true}, {"music", true, true}, {"sound", true, true},
    {"voice", true, true}, {"stop", true, true}, {"jump", true, true},
    {"set", true, true}, {"add", true, true}, {"subtract", true, true}, {"toggle", true, true},
    {"open app", false, true}, {"open chat", false, true},
    {"open image", false, true}, {"open", true, true},
    {"quick save", true, true},
    {"fullscreen", true, true}, {"screenshot", true, true}, {"quit", true, true},
    // Buttons and keys only.
    {"start", false, true}, {"continue", false, true}, {"close", false, true}, {"answer call", false, true}, {"decline call", false, true}, {"hang up", false, true},
    {"answer", false, true}, {"phone call", false, true}, {"send", false, true}, {"keypad", false, true}, {"unlock", false, true},
    {"quick load", false, true}, {"skip", false, true}, {"auto", false, true}, {"tools", false, true},
    {"next line", false, true}, {"rollback", false, true}, {"reload", false, true}, {"yes", false, true}, {"no", false, true},
    {"next page", false, true}, {"previous page", false, true}, {"delete save", false, true}, {"next image", false, true},
    {"previous image", false, true}, {"setting", false, true}, {"reset settings", false, true}, {"reset controls", false, true},
    {"reset progress", false, true}, {"back", false, true}, {"move up", false, true}, {"move down", false, true}, {"move left", false, true},
    {"move right", false, true}, {"press", false, true}, {"choose", false, true}, {"scroll up", false, true}, {"scroll down", false, true},
    {"scroll to the top", false, true}, {"scroll to the end", false, true}, {"put back", false, true},
});
constexpr auto kDefinitions = std::to_array<std::string_view>({"character", "colour", "place", "font", "atlas", "sub-image", "setting", "style",
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
    return verb == "if" || verb == "else" || verb == "choice" || verb == "scene";
}

// Whether a screen's lines have an app called `name` (in lower case) among their parts, at any depth: a
// container with app=NAME.
bool hasAppPart(const Line& screen, std::string_view name) {
    return std::ranges::any_of(screen.children, [&](const Line& part) {
        const bool app = lower(firstWord(part.text)) == "container" && std::ranges::any_of(rawWords(afterWords(part.text, 1)), [&](const std::string& word) {
            return lower(word).starts_with("app=") && lower(unquote(std::string_view(word).substr(4))) == name;
        });
        return app || hasAppPart(part, name);
    });
}

// A kind of part: its settings are these (a list's: the ones of its items' size and spacing it uses, one
// whose items go one way with spacing= alone, and glide=, glide_speed=, glide_distance= when it's dragged
// to scroll), besides any part's (place, size, look).
struct KindSpec {
    std::string_view name;
    ElementKind kind;
    std::string_view style; // style used when the element names none
    std::array<std::string_view, 28> options;
};
constexpr std::array kKinds{
    KindSpec{"image", ElementKind::Image, "", {"fit", "flip", "repeat", "chosen", "scroll", "scroll_round", "bottom"}}, // (an image, a colour, or one fading to bottom=)
    KindSpec{"text", ElementKind::Text, "text", {"chosen", "width_at_most", "lines"}},
    KindSpec{"button", ElementKind::Button, "button", {"action", "confirm", "keys", "clickable", "chosen", "drag", "drop", "click_animation"}},
    KindSpec{"namebox", ElementKind::Namebox, "namebox", {"bar", "bar_colour"}},
    KindSpec{"dialogue", ElementKind::Dialogue, "dialogue", {}},
    KindSpec{"indicator", ElementKind::Indicator, "indicator", {"bob", "bob_speed", "pause_image"}},
    KindSpec{"choices", ElementKind::Choices, "choice",
             {"position", "locked_style", "picked_before_style", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down"}},
    KindSpec{"history", ElementKind::History, "history", // (its lines one under another: only the gap between them)
             {"name_width", "close_at_end", "name_style", "text_style", "choice_style", "voice", "back", "button_style", "spacing", "scrollbar", "scrollbar_thickness",
              "glide", "glide_speed", "glide_distance"}},
    KindSpec{"pages", ElementKind::Pages, "tab", {"wide_tab_width", "mode", "auto", "quick", "item_width", "item_height", "spacing"}}, // (its tabs in a row)
    KindSpec{"slots", ElementKind::Slots, "slot",
             {"mode", "delete", "title_style", "text_style", "empty_style", "confirm", "load_confirm", "delete_confirm", "columns", "item_width", "item_height", "spacing",
              "spacing_across", "spacing_down"}},
    KindSpec{"face", ElementKind::Face, "face", {"fit", "flip"}}, // flip=yes mirrors every face (and face_flip=yes ones back)
    KindSpec{"control", ElementKind::Control, "setting", {"slot", "waiting"}},
    KindSpec{"gallery", ElementKind::Gallery, "gallery",
             {"action", "title_style", "locked_style", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down", "scrollbar", "scrollbar_thickness",
              "glide", "glide_speed", "glide_distance"}},
    KindSpec{"page", ElementKind::Page, "page", {"name_width", "name_style", "text_style", "spacing"}}, // (its lines one under another)
    KindSpec{"input", ElementKind::Input, "input", {"variable", "most_letters", "placeholder", "action", "confirm", "clickable"}},
    KindSpec{"bar", ElementKind::Bar, "bar", {"value", "full_at", "vertical", "track", "fill"}},
    KindSpec{"slider", ElementKind::Slider, "slider", {"variable", "setting", "minimum", "maximum", "step", "track", "fill", "knob", "clickable", "action"}},
    KindSpec{"movie", ElementKind::Movie, "", {"fit", "loop", "volume"}},
    KindSpec{"endings", ElementKind::Endings, "record",
             {"title_style", "text_style", "locked_style", "secret", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down", "scrollbar",
              "scrollbar_thickness", "glide", "glide_speed", "glide_distance"}},
    KindSpec{"songs", ElementKind::Songs, "record",
             {"title_style", "text_style", "locked_style", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down", "scrollbar", "scrollbar_thickness",
              "glide", "glide_speed", "glide_distance"}},
    KindSpec{"achievements", ElementKind::Achievements, "record",
             {"title_style", "text_style", "locked_style", "secret", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down", "scrollbar",
              "scrollbar_thickness", "glide", "glide_speed", "glide_distance"}},
    KindSpec{"tips", ElementKind::Tips, "record",
             {"action", "title_style", "locked_style", "unread_style", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down", "scrollbar",
              "scrollbar_thickness", "glide", "glide_speed", "glide_distance"}},
    KindSpec{"flowchart", ElementKind::Flowchart, "flowchart", // (its points as a tree: no columns)
             {"line", "ending_style", "unreached_style", "choice_style", "confirm", "item_width", "item_height", "spacing", "spacing_across", "spacing_down", "scrollbar",
              "scrollbar_thickness", "glide", "glide_speed", "glide_distance"}},
    KindSpec{"container", ElementKind::Container, "",
             {"chosen", "fit", "scroll", "scrollbar", "scrollbar_thickness", "glide", "glide_speed", "glide_distance", "edges", "fading_edges", "stack", "list", "position",
              "item_animation", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down", "app", "title", "icon", "count"}},
};
constexpr auto kColourOptions = std::to_array<std::string_view>({"bottom", "bar_colour", "track", "fill", "knob", "line"});
constexpr auto kFormulaOptions = std::to_array<std::string_view>({"value", "minimum", "maximum", "full_at", "most_letters", "step"});
// The player's settings: "setting NAME VALUE" (buttons), slider setting=NAME and "setting default.NAME" lines.
constexpr auto kSettingNames = std::to_array<std::string_view>({
    "fullscreen", "window_size", "scaling", "vsync", "frame_limit", "text_speed", "auto_delay", "textbox_opacity", "text_size",
    "animations", "master_volume", "music_volume", "sound_volume", "voice_volume", "made_up_voices", "typing_sounds", "mouths", "skip_unread",
    "skip_after_choices", "skip_movies", "rollback", "read_aloud", "high_contrast", "vibration", "language",
});

// Words that pick one of a few values.
template <class T>
using Choices = std::span<const std::pair<std::string_view, T>>;
constexpr std::pair<std::string_view, Ease> kEases[]{{"linear", Ease::Linear}, {"in", Ease::In},         {"out", Ease::Out},
                                                     {"inout", Ease::InOut},   {"bounce", Ease::Bounce}, {"spring", Ease::Spring},
                                                     {"overshoot", Ease::Overshoot}};
constexpr std::pair<std::string_view, TextAppears> kTextAppears[]{{"letter", TextAppears::Letter}, {"word", TextAppears::Word}, {"line", TextAppears::Line}};
constexpr std::pair<std::string_view, NewMessages> kNewMessages[]{{"button", NewMessages::Button}, {"open", NewMessages::Open}};
constexpr std::pair<std::string_view, ChatMessages> kChatMessages[]{{"typed", ChatMessages::Typed}, {"click", ChatMessages::Click}};
constexpr std::pair<std::string_view, TimeKind> kTimeKinds[]{{"none", TimeKind::None}, {"story", TimeKind::Story}, {"real", TimeKind::Real}, {"days", TimeKind::Days}};
constexpr std::pair<std::string_view, MouthMoves> kMouthMoves[]{{"yes", MouthMoves::Yes}, {"voice", MouthMoves::Voice}, {"no", MouthMoves::No}};
constexpr std::pair<std::string_view, int> kScalings[]{{"fit", 0}, {"integer", 1}, {"stretch", 2}, {"fill", 3}};
constexpr std::pair<std::string_view, int> kMouths[]{{"yes", 0}, {"voice", 1}, {"no", 2}};

std::optional<Align> toAlign(std::string_view s) {
    if (s == "left" || s == "top" || s == "start") return Align::Start;
    if (s == "centre" || s == "middle") return Align::Centre;
    if (s == "right" || s == "bottom" || s == "end") return Align::End;
    return std::nullopt;
}

// An image name as scripts use it: "images/mina.png" -> mina
std::string imageName(std::string_view file) {
    std::string out(file);
    if (out.starts_with("images/")) out.erase(0, 7);
    if (const auto dot = out.rfind('.'); dot != std::string::npos && out.find('/', dot) == std::string::npos) out.erase(dot);
    return out;
}

// Default sprite file name for a character: "Mr. Tanaka" -> mr_tanaka
std::string characterImageName(std::string_view name) {
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
    // setting lines and its "scene start", and includes the other files.
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
        if (!s_.scenes.contains("start"))
            s_.errors.push_back({entry, 0, "The story needs a beginning: a 'scene start' line in main.vnscript, with its first lines under it."_tr()});
        screens();
        bindings();
        resolve();
        // The phone's own variables: none (the clock: the computer's), a signal and a full battery, unless Settings starts them.
        for (const char* phone : {"phone_owner", "phone_wallpaper", "phone_lock_wallpaper", "phone_passcode", "phone_clock", "phone_date"})
            s_.startValues.try_emplace(phone, std::string{});
        s_.startValues.try_emplace("phone_signal", 1);
        s_.startValues.try_emplace("phone_battery", 100);
        checkNames();
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

    // Runs `parse`; when the line's first word was capitalised and it doesn't work as a command,
    // its errors are dropped and false is returned (the line is then shown as text instead).
    template <class F>
    bool attempt(bool lenient, F&& parse) {
        const auto before = s_.errors.size(), reads = reads_.size(), writes = written_.size();
        std::forward<F>(parse)();
        if (!lenient || s_.errors.size() == before) return true;
        s_.errors.resize(before);
        reads_.resize(reads); // (and what it read and wrote)
        written_.resize(writes);
        return false;
    }

    std::size_t emit(Command c, const Line& line, bool tail = false) { // (tail: see Instruction)
        s_.code.push_back({std::move(c), line.file, line.number, tail});
        return s_.code.size() - 1;
    }

    cmd::Jump& jumpAt(std::size_t pc) { return std::get<cmd::Jump>(s_.code[pc].command); }

    // `missing`: what's said when there's nothing there, in the words of the line's form (else the plain one).
    std::optional<Expr> formula(const Line& line, std::string_view src, std::string missing = {}) {
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
    void checkTextOf(const Line& line, std::string_view text) {
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

    // -- Names ----------------------------------------------------------------------------

    // The names conditions, formulas and texts' {values} read (with the lists a screen's part is in: its
    // items' values are its too), and the variables the scripts write; checkNames() goes through them once
    // everything is read.
    struct Read {
        std::uint16_t file = 0;
        int number = 0;
        std::string name;
        std::vector<std::string> items; // the item words of the lists around it ("chat", "message"...)
    };
    std::vector<Read> reads_;
    std::vector<std::string> written_;
    std::vector<std::string> items_; // (while a screen's parts are read: the lists they're in)
    struct PhoneSceneName { // a character's chat= or call= (checked once the scenes are read)
        std::uint16_t file = 0;
        int number = 0;
        std::string scene;
    };
    std::vector<PhoneSceneName> phoneSceneNames_;

    void read(const Line& line, std::string name) { reads_.push_back({.file = line.file, .number = line.number, .name = std::move(name), .items = items_}); }
    void readValues(const Line& line, std::string_view text) { // (a text's {values})
        for (std::string& name : namesIn(text)) read(line, std::move(name));
    }
    void wrote(std::string name) { written_.push_back(std::move(name)); }

    // Each name read is something: a variable (one the scripts set, add to, subtract from, toggle or
    // start with, or a slider's or input's), a value the engine gives (its built-in ones,
    // the player's settings, answer and dropped), a record's (persistent.ending.NAME...), or, in a part inside a
    // list, its items' ({chat.name}).
    void checkNames() {
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

    std::optional<float> number(const Line& line, std::string_view value, std::string_view what) {
        if (auto v = parseNumber(value)) return v;
        return fail(line, "'{}' needs a number, not '{}'."_tr(what, value));
    }

    std::optional<bool> boolean(const Line& line, std::string_view value, std::string_view what) {
        if (auto v = parseBool(value)) return v;
        return fail(line, "'{}' can be yes or no, not '{}'."_tr(what, value));
    }

    std::optional<SDL_Color> colour(const Line& line, std::string_view value) {
        if (auto c = parseColour(value, &s_.palette)) return c;
        return fail(line, "'{}' isn't a colour."_tr(value));
    }

    // One of a few words (case doesn't matter), else an error listing them.
    template <class T>
    std::optional<T> oneOf(const Line& line, std::string_view what, std::string_view value, Choices<T> choices) {
        std::string list;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            if (lower(value) == choices[i].first) return choices[i].second;
            list += std::format("{}{}", i == 0 ? "" : i + 1 == choices.size() ? " or " : ", ", choices[i].first);
        }
        error(line, "'{}' can be {}, not '{}'."_tr(what, list, value));
        return std::nullopt; // (not fail(): GCC 16 takes its nullopt, copied in, for something unset)
    }

    // When a mouth moves (mouth_moves= and face_mouth=, setting mouths_move): yes, voice or no.
    std::optional<MouthMoves> mouthMoves(const Line& line, std::string_view what, std::string_view value) {
        if (const auto b = parseBool(value)) return *b ? MouthMoves::Yes : MouthMoves::No;
        return oneOf<MouthMoves>(line, what, value, kMouthMoves);
    }

    // Words after a command that read as a phrase. Took::No: the next word isn't this one;
    // Took::Bad: it is, but what follows is wrong (the error is written).
    enum class Took : std::uint8_t { No, Yes, Bad };

    // "fade 2" or "fade=2", "volume 50%" or "volume=50%" (music, sounds, voices, movies, weather).
    Took amount(const Line& line, Cursor& c, std::string_view key, float& out) {
        std::optional<float> n;
        if (c.take(key)) n = c.number();
        else if (c.atOption(key)) n = parseNumber(c.option()->second);
        else return Took::No;
        if (!n) return error(line, "'{}' needs a number after it."_tr(key)), Took::Bad;
        out = key == "volume" ? std::clamp(*n, 0.0f, 1.0f) : std::max(0.0f, *n);
        return Took::Yes;
    }

    std::optional<Cursor> words(const Line& line, std::string_view text) {
        auto w = splitWords(text);
        if (!w) {
            error(line, "In '{}': {}."_tr(text, w.error()));
            return std::nullopt;
        }
        return Cursor(std::move(*w));
    }

    // -- Reading files ---------------------------------------------------------

    void readFile(const std::string& path, int indent, std::vector<Line>& out, const Line* from) {
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
    void block(std::span<const Line> lines) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const Line& line = lines[i];
            const std::string head = headOf(line.text);
            const std::string second = lower(firstWord(afterWords(line.text, 1)));
            const bool exact = !startsWithCapital(line.text);
            const bool hasBlock = !line.children.empty();
            if (const auto phone = head == "scene" && hasBlock && phase_ == Phase::Story ? phoneSceneFor(line) : std::nullopt) {
                phoneScene(line, *phone->first, phone->second);
            } else if (head == "if" && (exact || hasBlock)) {
                i = ifChain(lines, i);
            } else if (head == "else" && (exact || hasBlock)) {
                if (phase_ == Phase::Story) error(line, "This 'else' has no 'if' right above it at the same indentation."_tr());
                block(line.children);
            } else if (head == "choice" && (exact || hasBlock)) {
                menu(line);
            } else if (head == "chat" && hasBlock) {
                chatBlock(line);
            } else if (head == "phone" && second == "call" && (exact || hasBlock)) {
                callBlock(line);
            } else if (head == "together" && (exact || hasBlock) && afterWords(line.text, 1).empty()) {
                together(line);
            } else if (std::ranges::contains(kDefinitions, head) && !asText_.contains(&line)) {
                if (phase_ == Phase::Definitions) defs_.push_back(&line);
            } else {
                if (phase_ == Phase::Story && !(chat_ && chatStatement(line))) statement(line);
                block(line.children); // indentation under an ordinary line is just layout
            }
        }
    }

    // if / else if / else; returns the index of the chain's last line.
    std::size_t ifChain(std::span<const Line> lines, std::size_t i) {
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
                    error(line, "'else' takes nothing after it."_tr());
                if (!isElse) branch = emit(cmd::Jump{.unless = formula(line, condition, "What to check is missing."_tr())}, line);
                if (line.children.empty())
                    error(line, "Nothing is indented under this line."_tr());
            }
            block(line.children);

            bool more = false;
            if (!isElse && i + 1 < lines.size()) {
                const std::string next = headOf(lines[i + 1].text);
                more = next == "else" && (!startsWithCapital(lines[i + 1].text) || !lines[i + 1].children.empty());
            }
            if (phase_ == Phase::Story) {
                if (more) exits.push_back(emit(cmd::Jump{}, line, true));
                if (branch) jumpAt(*branch).target = s_.code.size();
            }
            if (!more) break;
        }
        for (auto pc : exits) jumpAt(pc).target = s_.code.size();
        return i;
    }

    // choice [settings] with one indented option per line; each option's own lines run when
    // it's picked.
    void menu(const Line& line) {
        if (phase_ == Phase::Definitions) {
            for (const auto& option : line.children) block(option.children);
            return;
        }
        cmd::Menu m;
        std::string rest(choiceSettings(line, afterWords(line.text, 1), m));
        if (rest.ends_with(':')) rest.pop_back();
        if (!trim(rest).empty()) error(line, "'{}' isn't one of a choice's settings (optional=, repeat=, return=, for N seconds, for N lines)."_tr(trim(rest)));
        const bool meanwhile = m.lines > 0; // up while the story goes on: each option comes back when its lines are done
        if (meanwhile && m.repeats) error(line, "A choice that's up for some lines can't repeat."_tr());
        if (chat_ || !callWith_.empty()) { // (in a chat or a call: its options are on the phone)
            if (meanwhile || m.optional) error(line, (chat_ ? "A chat's choice can only have a time limit and repeat."_tr : "A call's choice can only have a time limit and repeat."_tr)());
            m.phone = chat_ ? chat_->with : callWith_;
            m.call = !chat_;
        }
        options(line, line.children, std::move(m));
    }

    // A choice's options (`optionLines`, under `line`), each with its own lines run when it's picked.
    void options(const Line& line, std::span<const Line> optionLines, cmd::Menu m) {
        const bool meanwhile = m.lines > 0, repeats = m.repeats;
        const std::size_t at = emit(std::move(m), line);
        std::vector<cmd::Choice> choices;
        std::vector<std::size_t> exits;   // jumps to the end of the choice
        std::vector<std::size_t> skipped; // options without lines: continue after the choice
        for (const auto& option : optionLines) {
            cmd::Choice choice = this->option(option);
            if (!option.children.empty()) {
                choice.target = s_.code.size();
                block(option.children);
                if (meanwhile) emit(cmd::Return{}, option, true);
                else if (repeats) emit(cmd::Jump{.target = at, .again = true}, option, true); // (the choice again)
                else exits.push_back(emit(cmd::Jump{}, option, true));
            } else if (meanwhile) {
                choice.target = emit(cmd::Return{}, option, true); // nothing to do: back to the story
            } else if (repeats) {
                choice.target = at;
            } else {
                skipped.push_back(choices.size());
            }
            choices.push_back(std::move(choice));
        }
        for (auto pc : exits) jumpAt(pc).target = s_.code.size();
        for (auto k : skipped) choices[k].target = s_.code.size();
        if (choices.empty())
            error(line, "This choice has no options."_tr());
        auto& emitted = std::get<cmd::Menu>(s_.code[at].command);
        emitted.choices = std::move(choices);
        emitted.after = s_.code.size();
    }

    // An option of a choice: "Text [(if CONDITION) | (needs CONDITION: what)]".
    cmd::Choice option(const Line& option) {
        std::string text(trim(option.text));
        std::optional<Expr> condition;
        if (text.ends_with(':') && !isQuoted(text)) text.pop_back();
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
        return cmd::Choice{.text = text, .condition = std::move(condition), .shownLocked = shownLocked, .note = std::move(note),
                           .id = fnv1a(text, fnv1a(currentScene_, fnv1a("choice")))};
    }

    // together, with lines indented under it: they all show at once, each in its own textbox (a
    // character's textbox=, or the usual one), and the story waits for the last.
    void together(const Line& line) {
        if (phase_ == Phase::Definitions) return block(line.children);
        const std::size_t first = s_.code.size();
        block(line.children);
        std::vector<cmd::Say*> says;
        for (std::size_t pc = first; pc < s_.code.size(); ++pc)
            if (auto* say = std::get_if<cmd::Say>(&s_.code[pc].command)) says.push_back(say);
        if (says.size() < 2) return error(line, "'together' needs two lines or more indented under it."_tr());
        for (std::size_t k = 0; k + 1 < says.size(); ++k) says[k]->together = true;
    }

    // -- The phone's story lines ----------------------------------------------------------

    // Who a phone line is from (or to): "Mika", "Mika happy", "Mika (happy)" (`expression`) or anyone's
    // name. A defined character's lower-case name goes in `character`, anyone else's in `sender`.
    struct Who {
        std::string character, sender, expression;
        [[nodiscard]] bool empty() const { return character.empty() && sender.empty(); }
    };
    [[nodiscard]] Who whoOf(std::string_view written, bool expression) const {
        std::string w(trim(written));
        Who who;
        if (expression && w.ends_with(')') && w.find('(') != std::string::npos) { // Mika (happy)
            const auto open = w.rfind('(');
            who.expression = std::string(trim(std::string_view(w).substr(open + 1, w.size() - open - 2)));
            w = std::string(trim(std::string_view(w).substr(0, open)));
        }
        const auto space = w.rfind(' ');
        if (s_.character(lower(w))) {
            who.character = lower(w);
        } else if (expression && who.expression.empty() && space != std::string::npos && s_.character(lower(w.substr(0, space)))) {
            who.character = lower(w.substr(0, space)); // Mika happy
            who.expression = w.substr(space + 1);
        } else {
            who.sender = unquote(w);
        }
        return who;
    }
    // A person's name as the phone shows it: a character's own.
    [[nodiscard]] std::string shownName(const Who& who) const {
        if (const CharacterDef* def = who.character.empty() ? nullptr : s_.character(who.character)) return def->name;
        return who.sender;
    }
    // Where the word `word` (lower case) is in `text`: outside quotes, in any case. None: npos.
    [[nodiscard]] static std::size_t wordAt(std::string_view text, std::string_view word) {
        for (const auto& [start, length] : wordSpans(text))
            if (length == word.size() && lower(text.substr(start, length)) == word) return start;
        return std::string_view::npos;
    }
    // While a chat scene's lines are read: its chat.
    struct ChatContext {
        std::string with;      // the person's name as shown
        std::string character; // ...a defined character's
    };
    std::optional<ChatContext> chat_;
    std::string callWith_; // in someone's call= scene: whose (its choices show on the call)

    // chat WHO, with its lines under it: a chat scene. Its lines are the phone's (Mika: words, me: words,
    // Mika sends IMAGE...), a choice in it shows its options on the phone, and the rest are the story's, as anywhere.
    void chatBlock(const Line& line) {
        if (phase_ == Phase::Definitions) return block(line.children);
        if (chat_) return error(line, "A chat can't be inside another chat."_tr());
        std::string name(trim(afterWords(line.text, 1)));
        if (name.ends_with(':')) name.pop_back();
        name = unquote(name);
        if (name.empty()) return error(line, "Who the chat is with is missing."_tr());
        const Who who = whoOf(name, false);
        const cmd::Chat c{.with = shownName(who)};
        if (!screenLines_.contains("phone")) error(line, "A chat needs a screen called 'phone'."_tr());
        emit(c, line);
        chat_ = ChatContext{.with = c.with, .character = who.character};
        block(line.children);
        chat_.reset();
        emit(cmd::Message{.kind = cmd::Message::Kind::End, .character = who.character, .sender = who.character.empty() ? c.with : std::string{}}, line,
             true);                    // (a line saying it's over)
        emit(cmd::Wait{}, line, true); // (the last message read: a click, and the phone goes back)
        emit(cmd::Chat{.with = c.with, .end = true}, line, true);
    }

    // A line in a chat that isn't a command: one of the phone's (Mika: words, me sends IMAGE...), emitted.
    // False when it isn't one (then it's the story's).
    bool chatStatement(const Line& line) {
        if (!commandOf(line.text, true).empty() || isQuoted(trim(line.text))) return false;
        cmd::Message m;
        const Parsed p = phoneLine(line, line.text, m);
        if (p == Parsed::Yes) emit(std::move(m), line);
        return p != Parsed::No;
    }

    enum class Parsed : std::uint8_t { No, Yes, Bad };
    // A phone line: who (and how they look), then what: ": words", "sends [sticker | voice] NAME [: caption]",
    // "is typing [for N seconds]", "deletes it", "edits it: words", "reacts TEXT" or "reads it", in a chat
    // scene: "me" is the player, and the chat is the scene's. No: it isn't one (nothing written); Bad: it is,
    // with a mistake (written).
    Parsed phoneLine(const Line& line, std::string_view text, cmd::Message& m) {
        using Kind = cmd::Message::Kind;
        static constexpr std::pair<std::string_view, Kind> kVerbs[]{{"sends", Kind::Image}, {"is typing", Kind::Typing}, {"deletes it", Kind::Delete},
                                                                    {"edits it", Kind::Edit},  {"reacts", Kind::React},     {"reads it", Kind::Read}};
        text = trim(text);
        std::size_t colon = std::string_view::npos; // (the first outside quotes)
        for (std::size_t k = 0, quotes = 0; k < text.size() && colon == std::string_view::npos; ++k) {
            if (text[k] == '"') ++quotes;
            else if (text[k] == ':' && quotes % 2 == 0) colon = k;
        }
        const auto spans = wordSpans(text);
        auto wordIs = [&](std::size_t w, std::string_view word, std::size_t& end) { // (a word, without a colon after it)
            std::string_view written = text.substr(spans[w].first, spans[w].second);
            end = spans[w].first + spans[w].second;
            if (written.ends_with(':')) written.remove_suffix(1), --end;
            return lower(written) == word;
        };
        std::size_t verbAt = std::string_view::npos, verbEnd = 0;
        m.kind = Kind::Text;
        for (std::size_t w = 1; w < spans.size() && verbAt == std::string_view::npos; ++w) {
            if (colon != std::string_view::npos && spans[w].first > colon) break;
            for (const auto& [words, kind] : kVerbs) {
                const auto space = words.find(' ');
                std::size_t end = 0;
                if (!wordIs(w, words.substr(0, space), end)) continue;
                if (space != std::string_view::npos && (w + 1 >= spans.size() || !wordIs(w + 1, words.substr(space + 1), end))) continue;
                verbAt = spans[w].first;
                verbEnd = end;
                m.kind = kind;
                break;
            }
        }
        std::string who;
        std::string_view after;
        if (verbAt != std::string_view::npos) {
            who = std::string(trim(text.substr(0, verbAt)));
            after = trim(text.substr(verbEnd));
        } else if (colon != std::string_view::npos && colon > 0) {
            who = std::string(trim(text.substr(0, colon)));
            after = text.substr(colon + 1);
            // (narration with a colon in it, "At last: rain.", isn't one: as a line's speaker, at most three words)
            if ((std::ranges::count(who, ' ') > 2 || who.find_first_of(",!?\"{};") != std::string::npos)) return Parsed::No;
        } else {
            return Parsed::No;
        }

        // Who: the player ("me"), or someone.
        if (lower(who) == "me") {
            m.mine = true;
            who.clear();
        }
        if (!who.empty()) {
            const Who w = whoOf(who, !m.mine);
            m.character = w.character;
            m.sender = w.sender;
            m.expression = w.expression;
        }

        // What: its words, its image, how long they type...
        switch (m.kind) {
        case Kind::Text:
            m.text = unquote(after);
            if (m.text.empty()) return error(line, who.empty() ? "The player doesn't say anything."_tr() : "{} doesn't say anything."_tr(who)), Parsed::Bad;
            break;
        case Kind::Image: { // sends [sticker | voice] NAME [: caption]
            std::string_view what = after, caption;
            if (const auto c = what.find(':'); c != std::string_view::npos) {
                caption = trim(what.substr(c + 1));
                what = trim(what.substr(0, c));
            }
            if (const std::string first = lower(firstWord(what)); first == "sticker" || first == "voice") {
                m.kind = first == "sticker" ? Kind::Sticker : Kind::Voice;
                what = trim(afterWords(what, 1));
            }
            m.file = unquote(what);
            m.text = unquote(caption);
            if (m.file.empty()) return error(line, m.kind == Kind::Voice ? "Which voice file to send is missing."_tr() : "Which image to send is missing."_tr()), Parsed::Bad;
            if (m.file.find(' ') != std::string::npos)
                return error(line, "'sends' needs an image's name after it, or sticker or voice and a name."_tr()), Parsed::Bad;
            if (m.kind != Kind::Voice) m.file = imageName(m.file);
            break;
        }
        case Kind::Typing: // is typing [for N seconds]
            if (!after.empty()) {
                const auto seconds = parseNumber(firstWord(afterWords(after, 1)));
                const std::string unit = lower(afterWords(after, 2));
                if (lower(firstWord(after)) != "for" || !seconds || (!unit.empty() && unit != "seconds" && unit != "second"))
                    return error(line, "After 'is typing' can only come for how many seconds."_tr()), Parsed::Bad;
                m.seconds = std::max(0.0f, *seconds);
            }
            break;
        case Kind::Edit: // edits it: words
            if (after.empty() || (after.starts_with(':') && trim(after.substr(1)).empty())) return error(line, "What to change it to is missing."_tr()), Parsed::Bad;
            if (!after.starts_with(':')) return error(line, "'edits it' needs the new words after a colon."_tr()), Parsed::Bad;
            m.text = unquote(after.substr(1));
            break;
        case Kind::React:
            m.text = unquote(after);
            if (m.text.empty()) return error(line, "Which reaction is missing."_tr()), Parsed::Bad;
            break;
        default: // deletes it, reads it
            if (!after.empty()) return error(line, "'{}' doesn't belong in this line."_tr(after)), Parsed::Bad;
            break;
        }
        if (!m.text.empty()) checkTextOf(line, m.text);

        // Its chat, the chat scene's: only its person and the player write in it.
        if (m.mine) {
            m.character = chat_->character;
            m.sender = chat_->character.empty() ? chat_->with : std::string{};
        } else if (shownName({.character = m.character, .sender = m.sender}) != chat_->with) {
            return error(line, "Only {} and the player (me) write in {}'s chat."_tr(chat_->with, chat_->with)), Parsed::Bad;
        }
        return Parsed::Yes;
    }

    // A character's chat= or call= scene, by its scene line: whose, and whether it's their chat's (else their call's).
    [[nodiscard]] std::optional<std::pair<const CharacterDef*, bool>> phoneSceneFor(const Line& line) const {
        std::string name = lower(afterWords(line.text, 1));
        if (name.ends_with(':')) name.pop_back();
        for (const auto& [key, def] : s_.characters)
            if (def.chatScene == name || def.callScene == name) return std::pair{&def, def.chatScene == name};
        return std::nullopt;
    }

    // scene NAME that a character's chat= or call= names: what happens when the player opens their chat or calls them
    // (and their call's answered), played from its top then, its choices on the phone: in a chat, its lines are the
    // chat's (Mika: words); a call's over at its end. Run into or jumped to, the scene is nothing: the story goes on
    // after it.
    void phoneScene(const Line& line, const CharacterDef& def, bool chat) {
        scene(line);
        const std::size_t past = emit(cmd::Jump{}, line, true);
        s_.phoneScenes.insert_or_assign(currentScene_, s_.code.size());
        if (chat) chat_ = ChatContext{.with = def.name, .character = def.key};
        else callWith_ = def.name;
        block(line.children);
        chat_.reset();
        callWith_.clear();
        emit(cmd::Return{}, line, true);
        jumpAt(past).target = s_.code.size();
    }

    // phone call WHO [for N seconds]: they call the player, with the call's lines under it and, last, its
    // missed: lines (when the player doesn't answer). (The player calls from the phone.)
    void callBlock(const Line& line) {
        if (phase_ == Phase::Definitions) return block(line.children);
        std::string rest(trim(afterWords(line.text, 2)));
        if (rest.ends_with(':')) rest.pop_back();
        cmd::Call c;
        if (const auto at = wordAt(rest, "for"); at != std::string::npos && at > 0) { // for N seconds, at its end
            const std::string_view tail = std::string_view(rest).substr(at);
            const auto n = parseNumber(firstWord(afterWords(tail, 1)));
            const std::string unit = lower(afterWords(tail, 2));
            if (!n || (!unit.empty() && unit != "seconds" && unit != "second"))
                return error(line, "'for' needs how many seconds the call rings."_tr());
            c.seconds = std::max(0.0f, *n);
            rest = std::string(trim(std::string_view(rest).substr(0, at)));
        }
        const Who who = whoOf(rest, false);
        if (who.empty()) return error(line, "Who the call is from is missing."_tr());
        c.character = who.character;
        c.sender = who.sender;
        if (chat_) error(line, "A call can't be inside a chat."_tr());
        if (!screenLines_.contains("phone")) error(line, "A call needs a screen called 'phone'."_tr());
        calling(line, std::move(c), line.children);
    }
    // A call (`c`), its lines (`lines`: the last may be missed:), its end, and its missed: lines.
    void calling(const Line& line, cmd::Call c, std::span<const Line> lines) {
        const auto missed = std::ranges::find_if(lines, [](const Line& l) {
            const std::string t = lower(trim(l.text));
            return t == "missed:" || t == "missed";
        });
        if (missed != lines.end() && missed + 1 != lines.end()) error(*missed, "'missed:' comes last, after the call's lines."_tr());
        const std::span<const Line> talk(lines.begin(), missed);
        const std::size_t at = emit(std::move(c), line);
        block(talk);
        emit(cmd::Call{.end = true}, line, true);
        const std::size_t exit = emit(cmd::Jump{}, line, true);
        const std::size_t missedAt = s_.code.size();
        if (missed != lines.end()) block(missed->children);
        jumpAt(exit).target = s_.code.size();
        auto& emitted = std::get<cmd::Call>(s_.code[at].command);
        emitted.missed = missedAt;
        emitted.after = s_.code.size();
    }

    // The settings at the end of a choice's line (optional=yes, repeat=yes, for N seconds); returns the rest.
    std::string_view choiceSettings(const Line& line, std::string_view text, cmd::Menu& m) {
        std::vector<std::string_view> w; // the words, as parts of `text`
        for (std::size_t i = 0; i < text.size();) {
            const auto start = text.find_first_not_of(" \t", i);
            if (start == std::string_view::npos) break;
            const auto end = std::min(text.size(), text.find_first_of(" \t", start));
            w.push_back(text.substr(start, end - start));
            i = end;
        }
        auto cut = [&](std::size_t n) { // the last n words are settings
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
            } else if (last.starts_with("repeat=")) {
                if (!set(m.repeats, boolean(line, last.substr(7), "repeat"))) return text;
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
        if (m.lines > 0 && m.seconds > 0.0f) error(line, "A choice is up for some seconds or for some lines, not both."_tr());
        if (returns && m.lines == 0) error(line, "'return=' only goes with a choice that's up for some lines."_tr());
        return text;
    }

    // -- Story lines ---------------------------------------------------------------

    void statement(const Line& line) {
        const bool lenient = startsWithCapital(line.text);
        const std::string verb = commandOf(line.text, true);
        if (verb == "scene") {
            if (!attempt(lenient, [&] { scene(line); })) say(line);
            return;
        }
        if (verb.empty() || isBlockWord(verb)) return say(line); // real blocks were handled by block()
        std::optional<Command> c;
        if (!attempt(lenient, [&] { c = command(line, verb, true); })) return say(line);
        if (c) emit(std::move(*c), line);
    }

    void scene(const Line& line) {
        std::string name = lower(afterWords(line.text, 1));
        if (name.ends_with(':')) name.pop_back();
        if (!isName(name)) return error(line, "'{}' can't be a scene name (one word of letters, numbers, _ and -)."_tr(name));
        emit(cmd::End{}, line, true); // (the scene before ends here: it never runs on into this one)
        if (auto [it, fresh] = s_.scenes.emplace(name, s_.code.size()); !fresh)
            error(line, "There are two scenes called '{}'."_tr(name));
        else if (name == "start" && line.file != builtinFiles_)
            error(line, "'scene start' belongs in main.vnscript, where the story begins."_tr());
        currentScene_ = name;
        lineCounts_.clear();
    }

    // "Name: text", "Name expression: text" or narration.
    void say(const Line& line) {
        const std::string_view t = line.text;
        // A screen's part written where the story is (not indented under a screen line): a mistake, not narration.
        if (!isQuoted(t)) {
            const std::string head = lower(t.substr(0, t.find(' ')));
            static constexpr std::string_view kPartSettings[]{" x=", " y=", " width=", " height=", " action=", " style=", " background=", " image=", " keys="};
            if (std::ranges::find(kKinds, std::string_view(head), &KindSpec::name) != kKinds.end() &&
                std::ranges::any_of(kPartSettings, [&](std::string_view key) { return t.contains(key); }))
                return error(line, "'{}' is a screen part: it goes indented under a screen line."_tr(head));
        }
        // A line that starts as commands do, in lower case, without being one: a mistake too (a capital: text).
        if (const std::string word = unknownCommand(t); !word.empty()) {
            if (const std::string verb = commandOf(t, false); !verb.empty())
                return error(line, "'{}' is something a button can do, not a line of the story."_tr(verb));
            return error(line, "'{}' isn't a command: text that starts with a lower-case letter goes in quotes."_tr(word));
        }
        cmd::Say say;
        const auto colon = t.find(':');
        std::string prefix;
        if (!spoken(t)) {
            say.text = unquote(t);
        } else {
            prefix = trim(t.substr(0, colon));
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
            if (say.text.empty()) error(line, "{} doesn't say anything."_tr(prefix));
            if (!say.character.empty()) say.voiceId = std::format("{}_{}", currentScene_, ++lineCounts_[say.character]);
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
            if (!c.done()) return fail(line, "'{}' doesn't belong in this {} line."_tr(c.peek().text, verb));
            return result;
        };
        auto screenName = [&](std::string_view what) -> std::optional<std::string> {
            const auto name = c.done() || c.at("with") || c.at("and") ? std::nullopt : c.next(); // (open with fade...: no screen yet)
            if (!name) return fail(line, "Which screen to {} is missing."_tr(what));
            // (the built-in textbox's Phone button names the game's phone: it only shows when there's one)
            if (!screenLines_.contains(lower(*name)) && line.file >= builtinFiles_) return fail(line, "There's no screen called '{}'."_tr(*name));
            return lower(*name);
        };

        if (verb == "background") { // background IMAGE | background COLOUR [bottom=COLOUR] [animation=NAME] [with ANIMATION]
            const auto name = c.next();
            if (!name) return fail(line, "Which image to show is missing."_tr());
            cmd::Background background{.image = *name};
            auto hex = [](SDL_Color col) { return std::format("#{:02x}{:02x}{:02x}{:02x}", col.r, col.g, col.b, col.a); };
            const auto col = parseColour(*name, &s_.palette);
            if (col) background.image = hex(*col);
            if (const auto grad = parseGradient(*name, &s_.palette)) background.image = hex(grad->first) + "-" + hex(grad->second); // (navy-black: fading down to another)
            while (!c.done()) {
                if (c.atOption("bottom")) { // (bottom=: the same, as two words)
                    const auto opt = c.option();
                    if (!col) return fail(line, "A background that fades to a colour needs a colour to start from."_tr());
                    const auto bottom = colour(line, opt->second);
                    if (!bottom) return std::nullopt;
                    background.image = hex(*col) + "-" + hex(*bottom);
                } else if (c.atOption("animation")) { // (what it plays while it's shown; none: nothing)
                    const auto named = animationNamed(line, c.option()->second);
                    if (!named) return std::nullopt;
                    background.animation = *named;
                } else if (const Took t = withAnimation(line, c, background.with); t != Took::No) {
                    if (t == Took::Bad) return std::nullopt;
                } else {
                    return fail(line, "'{}' doesn't belong in this {} line."_tr(c.peek().text, verb));
                }
            }
            return background;
        }
        if ((verb == "show" || verb == "hide") && c.take("screen")) { // show screen NAME / hide screen NAME
            const auto name = screenName(verb + " screen");
            if (!name) return std::nullopt;
            return finished(cmd::Screen{.name = *name, .show = verb == "show"});
        }
        if (verb == "hide" && c.take("ui")) {
            if (story) return fail(line, "'hide ui' only works in buttons and keys."_tr());
            return finished(cmd::Ui{.verb = "hide ui"});
        }
        if (verb == "show") return show(line, c, story);
        if (verb == "hide") { // hide NAME [with ANIMATION] [and wait]
            std::string tag;
            if (const CharacterDef* def = takeCharacter(c)) tag = def->key;
            else if (auto name = c.next()) tag = lower(*name);
            else return fail(line, "Who or what to {} is missing."_tr(verb));
            cmd::Hide hide{.tag = tag};
            while (!c.done())
                if (const Took t = withAnimation(line, c, hide.with); t != Took::No) {
                    if (t == Took::Bad) return std::nullopt;
                } else if (const Took w = andWait(line, c, hide.wait, story); w != Took::No) {
                    if (w == Took::Bad) return std::nullopt;
                } else {
                    return fail(line, "'{}' doesn't belong in this {} line."_tr(c.peek().text, verb));
                }
            return hide;
        }
        if (verb == "effect") { // effect NAME... [amount=50%] [fade 2] [for N seconds] | effect none [fade 2]
            cmd::Effect e;
            bool none = false;
            while (!c.done()) {
                if (const Took t = amount(line, c, "fade", e.fade); t != Took::No) {
                    if (t == Took::Bad) return std::nullopt;
                } else if (c.take("for")) {
                    const auto n = c.number();
                    if (!n) return fail(line, "'for' needs a number of seconds."_tr());
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
                    return fail(line, "'{}' isn't an effect."_tr(c.peek().text));
                }
            }
            if (none && e.looks != 0) return fail(line, "'effect none' can't go with other effects."_tr());
            if (!none && e.looks == 0) return fail(line, "Which effect is missing."_tr());
            if (none && e.seconds > 0.0f) return fail(line, "'effect none' doesn't last for a time."_tr());
            return e;
        }
        if (verb == "jump") {
            if (c.at("screen")) return fail(line, "'{}' goes to a scene, not a screen."_tr(verb));
            const auto scene = c.next();
            if (!scene) return fail(line, "Which scene to go to is missing."_tr());
            if (!isName(*scene)) return fail(line, "'{}' needs a scene name."_tr(verb));
            return finished(cmd::Jump{.scene = lower(*scene)});
        }
        if (verb == "set" || verb == "add" || verb == "subtract" || verb == "toggle") return change(line, verb, rest);
        if (verb == "music" || verb == "sound" || verb == "voice") return play(line, verb, c);
        if (verb == "chat") return fail(line, "'{}' needs its lines indented under it."_tr(verb)); // (with its lines: block() reads them)
        if (verb == "phone") return phone(line, rest);
        if (verb == "stop") { // stop music | stop sound [NAME] | stop voice | stop weather, then [fade 2]
            cmd::Play stop;
            if (c.take("weather")) {
                cmd::Weather calm;
                if (amount(line, c, "fade", calm.fade) == Took::Bad) return std::nullopt;
                return finished(calm);
            }
            if (c.take("music")) stop.track = cmd::Play::Track::Music;
            else if (c.take("voice")) stop.track = cmd::Play::Track::Voice;
            else if (!c.take("sound")) return fail(line, "'stop' goes with music, sound, voice or weather."_tr());
            if (stop.track == cmd::Play::Track::Sound && !c.done() && !c.at("fade") && !c.atOption("fade")) stop.channel = lower(*c.next());
            if (amount(line, c, "fade", stop.fade) == Took::Bad) return std::nullopt;
            return finished(stop);
        }
        if (verb == "wait") {
            if (c.done()) return cmd::Wait{};
            const auto n = c.number();
            if (!n) return fail(line, "'wait' takes a number of seconds, or nothing."_tr());
            return finished(cmd::Wait{.seconds = std::max(0.0f, *n)});
        }
        if (verb == "time") return timeLine(line, c);
        if (verb == "weather") return weather(line, c);
        if (verb == "camera") return camera(line, c, story);
        if (verb == "movie") { // movie NAME [loop] [skip=no] [volume 80%]
            cmd::Movie m{.file = c.next().value_or("")};
            if (m.file.empty()) return fail(line, "Which movie to play is missing."_tr());
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
                    return fail(line, "'{}' doesn't belong in a movie line."_tr(c.peek().text));
                }
            }
            if (m.loop && !m.skippable) return fail(line, "A movie with 'loop' and 'skip=no' would never end."_tr());
            return m;
        }

        // Menu and engine commands.
        cmd::Ui ui{.verb = verb};
        if (verb == "answer") { // the rest of the line (quotes are optional)
            ui.arg = unquote(rest, true);
            checkTextOf(line, ui.arg);
            return ui;
        }
        if (verb == "open") { // open SCREEN [with ANIMATION] [and close with ANIMATION], or open https://...
            if (const std::string target = unquote(rest, true); target.find("://") != std::string::npos || target.starts_with("www."))
                return ui.arg = target, ui;
            const auto name = screenName("open");
            if (!name) return std::nullopt;
            if (withAnimation(line, c, ui.opening) == Took::Bad) return std::nullopt;
            if (c.at("and") && c.left() > 1 && lower(c.peek(1).text) == "close") { // and close with ANIMATION: how it goes when it closes
                c.skip(2);
                const Took t = withAnimation(line, c, ui.closing);
                if (t == Took::No) return fail(line, "'and close' needs 'with' and an animation after it."_tr());
                if (t == Took::Bad) return std::nullopt;
            }
            ui.arg = *name;
            return finished(ui);
        }
        if (verb == "setting") { // setting NAME VALUE: a player setting (the window's size: bigger or smaller; the language: next, previous or one by name)
            const auto name = c.next();
            if (!name) return fail(line, "Which setting to change is missing."_tr());
            const std::string setting = lower(*name);
            if (!std::ranges::contains(kSettingNames, setting)) return fail(line, "There's no player setting called '{}'."_tr(*name));
            const auto value = c.next();
            if (!value) return fail(line, "What to set {} to is missing."_tr(setting));
            const std::string word = lower(*value);
            std::optional<int> n;
            if (setting == "window_size" && (word == "bigger" || word == "smaller")) n = word == "bigger" ? 1 : -1;
            else if (setting == "window_size") { // or a width and a height, in pixels
                std::optional<float> h;
                if (!c.done())
                    if (const auto second = c.next()) h = parseNumber(*second);
                const auto w = parseNumber(word);
                if (!w || !h || *w < 1.0f || *h < 1.0f) return fail(line, "The window's size is a width and a height in pixels, or bigger or smaller."_tr());
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
        if (verb == "tools") { // tools WHAT: one of the developer tools' own keys (a control's): nothing in the game
            ui.arg = lower(std::string(trim(rest)));
            if (!std::ranges::contains(kToolsVerbs, ui.arg)) return fail(line, "'{}' isn't one of the developer tools' keys."_tr(ui.arg));
            return ui;
        }
        if (verb == "start" && !c.done()) ui.arg = lower(*c.next());
        if (verb == "open chat" || verb == "phone call" || verb == "open image") { // open chat WHO, phone call WHO, open image NAME (in a list: {chat.name}...)
            ui.arg = unquote(rest, true);
            if (ui.arg.empty())
                return fail(line, verb == "open image" ? "Which image to open is missing."_tr() : verb == "open chat" ? "Whose chat to open is missing."_tr()
                                                                                                                  : "Who to call is missing."_tr());
            return ui;
        }
        if (verb == "choose") { // choose N: the choice's option N (the first is 1)
            const auto n = c.done() ? std::nullopt : parseNumber(*c.next());
            if (!n || *n < 1.0f || *n != std::floor(*n)) return fail(line, "Which option to choose is missing: its number, 1 for the first."_tr());
            ui.arg = std::format("{}", static_cast<int>(*n));
            return finished(ui);
        }
        if (verb == "keypad") { // keypad 0 to 9, or keypad back: the phone's lock screen's keypad
            const auto key = c.next();
            if (!key || !(lower(*key) == "back" || (key->size() == 1 && (*key)[0] >= '0' && (*key)[0] <= '9')))
                return fail(line, "'keypad' needs a digit (0 to 9) or back after it."_tr());
            ui.arg = lower(*key);
            return finished(ui);
        }
        if (verb == "open app") { // open app NAME: the phone's app called that
            const auto name = c.next();
            if (!name) return fail(line, "Which app to open is missing."_tr());
            ui.arg = lower(*name);
            if (const auto phone = screenLines_.find("phone"); ui.arg != "home" && ui.arg != "call" && ui.arg != "lock" && !ui.arg.contains('{') && // (the phone's own; a value's: as it goes)
                (phone == screenLines_.end() || !hasAppPart(*phone->second, ui.arg)))
                error(line, "There's no app called '{}'."_tr(*name)); // (said once: the button keeps its action)
        }
        return finished(ui);
    }

    // A phone line is a call (phone call WHO, with its lines under it: callBlock), never a line of its own.
    std::optional<Command> phone(const Line& line, std::string_view) {
        return fail(line, "'phone' only starts a call: phone call WHO, with what's said indented under it."_tr());
    }

    // A player setting's value as a number: a number, yes / no, or (scaling, mouths) its word; auto_delay's seconds
    // as milliseconds.
    std::optional<int> settingNumber(const Line& line, const std::string& setting, std::string_view key, std::string_view value) {
        if (setting == "mouths") return oneOf<int>(line, key, value, kMouths);
        if (setting == "auto_delay") {
            const auto seconds = number(line, value, key);
            return seconds ? std::optional(static_cast<int>(std::lround(*seconds * 1000.0f))) : std::nullopt;
        }
        if (const auto b = parseBool(value)) return *b ? 1 : 0;
        if (setting == "scaling" && !parseNumber(value)) return oneOf<int>(line, key, value, kScalings);
        if (const auto x = number(line, value, key)) return value.ends_with('%') ? static_cast<int>(std::lround(*x * 100.0f)) : static_cast<int>(*x); // (80%: 80)
        return std::nullopt;
    }

    // show NAME [expression] [at POSITION] [x= y= zoom= ...] [animation=NAME] [with ANIMATION] [and wait]
    std::optional<Command> show(const Line& line, Cursor& c, bool story) {
        cmd::Show show;
        auto keyword = [&] { return c.at("at") || c.at("to") || c.at("with") || c.at("and") || c.peek().text.find('=') != std::string::npos; };
        if (const CharacterDef* def = takeCharacter(c)) {
            show.character = show.tag = def->key;
            if (!c.done() && !keyword()) show.expression = *c.next();
        } else if (auto name = c.next()) {
            show.image = *name;
            show.tag = lower(*name);
        } else {
            return fail(line, "Who or what to show is missing."_tr());
        }
        while (!c.done()) {
            if (c.take("at") || c.take("to")) {
                if (!place(line, c, show.props)) return std::nullopt;
            } else if (const Took t = withAnimation(line, c, show.with); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
            } else if (const Took w = andWait(line, c, show.wait, story); w != Took::No) {
                if (w == Took::Bad) return std::nullopt;
            } else if (c.atOption("animation")) { // (what it plays while it's shown; none: nothing)
                const auto named = animationNamed(line, c.option()->second);
                if (!named) return std::nullopt;
                show.animation = *named;
            } else if (auto opt = c.option()) {
                if (!stageProp(line, show.props, opt->first, opt->second)) return std::nullopt;
            } else {
                return fail(line, "'{}' doesn't belong in a show line."_tr(c.peek().text));
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

    // at / to PLACE: a name from a "place" line, or a number from -1 (left) to 1 (right).
    bool place(const Line& line, Cursor& c, StageProps& props) {
        const auto word = c.next();
        if (!word) return error(line, "'at' needs a place on the stage."_tr()), false;
        if (auto n = stageCoordinate(*word, false)) props.x = *n;
        else if (auto it = places_.find(lower(*word)); it != places_.end()) props.x = it->second;
        else return error(line, "There's no place called '{}'."_tr(*word)), false;
        return true;
    }

    // An animation the scripts have, by the name a line gives it ("none": none, an empty name); a mistake
    // when there's none by that name.
    std::optional<std::string> animationNamed(const Line& line, std::string_view name) {
        const std::string animation = lower(name);
        if (animation == "none") return std::string{};
        if (!s_.animations.contains(animation)) return error(line, "There's no animation called '{}'."_tr(name)), std::nullopt;
        return animation;
    }
    // with ANIMATION: one of the scripts' animations (none: none) that the line plays.
    Took withAnimation(const Line& line, Cursor& c, std::optional<std::string>& out) {
        if (!c.take("with")) return Took::No;
        const auto name = c.next();
        if (!name) return error(line, "'with' needs an animation after it."_tr()), Took::Bad;
        const auto named = animationNamed(line, *name);
        if (!named) return Took::Bad;
        out = *named;
        return Took::Yes;
    }
    // and wait: the story waits for what the line does to end (a button's action can't).
    Took andWait(const Line& line, Cursor& c, bool& wait, bool story) {
        if (!c.at("and") || c.left() < 2 || lower(c.peek(1).text) != "wait") return Took::No;
        c.skip(2);
        if (!story) return error(line, "A button can't wait for this to end."_tr()), Took::Bad;
        wait = true;
        return Took::Yes;
    }

    // x= y= zoom= opacity= rotation= flip= layer= tint=
    bool stageProp(const Line& line, StageProps& p, const std::string& key, const std::string& value) {
        if (key == "flip") {
            const auto b = boolean(line, value, key);
            p.flip = b;
            return b.has_value();
        }
        if (key == "tint") return (p.tint = colour(line, value)).has_value();
        static constexpr auto keys = std::to_array<std::string_view>({"x", "y", "zoom", "opacity", "rotation", "layer"});
        if (!std::ranges::contains(keys, key))
            return error(line, "A show line has no setting '{}'."_tr(key)), false;
        if (key == "x" || key == "y") {
            const auto n = stageCoordinate(value, key == "y");
            if (!n) return error(line, key == "x" ? "'x' needs a number from -1 (the left edge) to 1 (the right edge), not '{}'."_tr(value)
                                                  : "'y' needs a number from -1 (the bottom) to 1 (the top), not '{}'."_tr(value)),
                          false;
            (key == "x" ? p.x : p.y) = *n;
            return true;
        }
        const auto n = parseNumber(value);
        if (!n) return error(line, "'{}' needs a number, not '{}'."_tr(key, value)), false;
        if (key == "zoom") p.zoom = *n;
        else if (key == "opacity") p.opacity = *n;
        else if (key == "rotation") p.rotation = *n;
        else p.layer = static_cast<int>(*n);
        return true;
    }

    // set X to V | set X = V | add V to X | subtract V from X | toggle X
    std::optional<Command> change(const Line& line, const std::string& verb, std::string_view rest) {
        cmd::Set set{.op = verb == "add" ? '+' : verb == "subtract" ? '-' : verb == "toggle" ? '!' : '='};
        std::string_view value;
        const std::string spaced = " " + std::string(rest); // (add and subtract: a value left out, "add to x", still has the word)
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
            } else if (!set.variable.empty() && value.empty()) {
                return fail(line, "What to set {} to is missing."_tr(set.variable));
            } else if (!set.variable.empty()) {
                const std::string low = lower(rest);
                if (const auto to = low.find(" to "); to != std::string::npos) // ("set my score to 1": a name that's more than a word)
                    return fail(line, "'{}' can't be a variable name (one word of letters, numbers and _)."_tr(trim(std::string_view(low).substr(0, to))));
                return fail(line, "The word 'to' after '{}' is missing."_tr(set.variable));
            }
        } else if (verb == "toggle") {
            set.variable = lower(rest);
        } else {
            const std::string_view word = verb == "add" ? " to " : " from ";
            if (const auto at = lower(spaced).rfind(word); at != std::string::npos) {
                value = trim(std::string_view(spaced).substr(0, at));
                set.variable = lower(trim(std::string_view(spaced).substr(at + word.size())));
            }
        }
        if (set.variable.empty())
            return fail(line, verb == "add"        ? "Which variable to add to is missing."_tr()
                              : verb == "subtract" ? "Which variable to subtract from is missing."_tr()
                              : verb == "toggle"   ? "Which variable to toggle is missing."_tr()
                                                   : "Which variable to set is missing."_tr());
        if (!isVariable(set.variable))
            return fail(line, "'{}' can't be a variable name (one word of letters, numbers and _)."_tr(set.variable));
        for (std::size_t k = 0; k < kRecordKinds.size(); ++k) // (a record's, persistent.ending.NAME: one the game lists)
            if (const std::string head = std::format("persistent.{}.", kRecordKinds[k]); set.variable.starts_with(head)) {
                const auto kind = static_cast<RecordKind>(k);
                if (!s_.record(kind, set.variable.substr(head.size())))
                    return fail(line, "There's no {} called '{}'."_tr(kind == RecordKind::Gallery ? "gallery image" : kRecordKinds[k], set.variable.substr(head.size())));
            }
        wrote(set.variable);
        if (set.op != '!') {
            set.value = formula(line, value, set.op == '+'   ? "What to add to {} is missing."_tr(set.variable)
                                             : set.op == '-' ? "What to subtract from {} is missing."_tr(set.variable)
                                                             : "What to set {} to is missing."_tr(set.variable));
            if (!set.value) return std::nullopt;
        }
        return set;
    }

    // music FILE [once] | sound FILE [loop] [as CHANNEL] | voice FILE [text_follows=yes|no], then [fade 2] [volume 50%]; music
    // and sound: [fade_out 1] (what played before fades out)
    std::optional<Command> play(const Line& line, std::string_view verb, Cursor& c) {
        cmd::Play p{.track = verb == "music" ? cmd::Play::Track::Music : verb == "voice" ? cmd::Play::Track::Voice : cmd::Play::Track::Sound,
                    .loop = verb == "music"};
        const auto file = c.next();
        if (!file)
            return fail(line, verb == "music" ? "Which song to play is missing."_tr() : verb == "voice" ? "Which voice file to play is missing."_tr()
                                                                                       : "Which sound to play is missing."_tr());
        static constexpr auto kWords = std::to_array<std::string_view>({"fade", "fade_out", "volume", "once", "loop", "as"});
        if (std::ranges::contains(kWords, lower(*file)) || file->contains('=')) // (the file left out, and what comes after it in its place)
            return fail(line, verb == "music" ? "Which song to play is missing."_tr() : verb == "voice" ? "Which voice file to play is missing."_tr()
                                                                                       : "Which sound to play is missing."_tr());
        if (lower(*file) == "stop") return fail(line, "'stop' isn't a file to play."_tr());
        p.file = *file;
        readValues(line, p.file);
        float fadeOut = 0.0f;
        while (!c.done()) {
            if (const Took t = amount(line, c, "fade", p.fade); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
            } else if (const Took o = verb == "voice" ? Took::No : amount(line, c, "fade_out", fadeOut); o != Took::No) {
                if (o == Took::Bad) return std::nullopt;
                p.fadeOut = fadeOut;
            } else if (const Took v = amount(line, c, "volume", p.volume); v != Took::No) {
                if (v == Took::Bad) return std::nullopt;
            } else if (verb == "music" && c.take("once")) {
                p.loop = false;
            } else if (verb == "sound" && c.take("loop")) {
                p.loop = true;
            } else if (verb == "sound" && c.take("as")) {
                const auto channel = c.next();
                if (!channel) return fail(line, "'as' needs a name after it."_tr());
                p.channel = lower(*channel);
            } else if (verb == "voice" && c.atOption("text_follows")) { // the next line types out with the voice (or doesn't)
                const auto b = boolean(line, c.option()->second, "text_follows");
                if (!b) return std::nullopt;
                p.textFollows = *b;
            } else {
                return fail(line, "'{}' doesn't belong in this {} line."_tr(c.peek().text, verb));
            }
        }
        if (p.track == cmd::Play::Track::Sound && p.loop && p.channel.empty()) p.channel = lower(*file); // so it can be stopped
        return p;
    }

    // weather rain|snow|petals|leaves|embers [amount=150%] [speed=2] [wind=-0.5] [tint=C] [image=NAME] [behind=yes] [fade 2]
    std::optional<Command> weather(const Line& line, Cursor& c) {
        static constexpr auto kinds = std::to_array<std::string_view>({"rain", "snow", "petals", "leaves", "embers"});
        cmd::Weather w{.kind = lower(c.next().value_or(""))};
        if (!std::ranges::contains(kinds, std::string_view(w.kind)))
            return fail(line, "'{}' isn't a kind of weather."_tr(w.kind));
        while (!c.done()) {
            if (const Took t = amount(line, c, "fade", w.fade); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
                continue;
            }
            const auto opt = c.option();
            if (!opt) return fail(line, "'{}' doesn't belong in a weather line."_tr(c.peek().text));
            const auto& [key, value] = *opt;
            if (key == "image") {
                w.image = imageName(value);
            } else if (key == "tint") {
                if (!(w.tint = colour(line, value))) return std::nullopt;
            } else if (key == "behind") {
                const auto b = boolean(line, value, key);
                if (!b) return std::nullopt;
                w.behind = *b;
            } else if (key == "amount" || key == "speed" || key == "wind") {
                const auto n = number(line, value, key);
                if (!n) return std::nullopt;
                (key == "amount" ? w.amount : key == "speed" ? w.speed : w.wind) = *n;
            } else {
                return fail(line, "'weather' has no setting '{}'."_tr(key));
            }
        }
        w.amount = std::clamp(w.amount, 0.0f, 10.0f);
        w.speed = std::clamp(w.speed, 0.0f, 10.0f);
        return w;
    }

    // camera [animation=NAME] [with NAME] [and wait]
    std::optional<Command> camera(const Line& line, Cursor& c, bool story) {
        cmd::Camera cam;
        while (!c.done()) {
            if (c.atOption("animation")) { // (what it keeps playing; none: nothing)
                const auto named = animationNamed(line, c.option()->second);
                if (!named) return std::nullopt;
                cam.animation = *named;
            } else if (const Took t = withAnimation(line, c, cam.with); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
            } else if (const Took w = andWait(line, c, cam.wait, story); w != Took::No) {
                if (w == Took::Bad) return std::nullopt;
            } else {
                return fail(line, "'{}' doesn't belong in a camera line."_tr(c.peek().text));
            }
        }
        if (!cam.animation && !cam.with) return fail(line, "Which animation to play is missing."_tr());
        return cam;
    }

    // time WHEN [day=N] (WHEN: a weekday, and a time of day or a part of the day; none: the same time) | time passes [N minutes|hours|days]
    std::optional<Command> timeLine(const Line& line, Cursor& c) {
        const GameSettings& g = s_.gameSettings;
        if (g.time == TimeKind::None) return fail(line, "A time line needs the game's time: choose one in Settings > Time."_tr());
        std::string parts; // (the game's, as the errors list them)
        for (const auto& [name, start] : g.dayParts) parts += (parts.empty() ? "" : ", ") + name;
        // What it can go on to, as the game's time has it: a clock time (not with days and parts of the day), a part of
        // the day, a weekday (real time's calendar, or a date or a weekday it starts at), a day number.
        const bool clocked = g.time != TimeKind::Days, weekdays = g.time == TimeKind::Real || g.timeStartDate >= 0 || g.timeStartWeekday >= 0;
        const std::string when = std::format("{}{}{} {}", clocked ? "a time of day ({}), "_tr(g.clock12 ? "6:30 pm" : "18:30") : std::string{},
                                             "a part of the day ({})"_tr(parts), weekdays ? std::string(", a weekday"_tr()) : std::string{}, "or a day number"_tr());
        cmd::Time t;
        if (c.take("passes")) { // (how long: minutes, hours or days; nothing: to the next part of the day)
            t.passes = true;
            if (c.done()) return t;
            const auto n = c.number();
            int unit = 0;
            for (const auto& [word, minutes] : {std::pair{"minute", 1}, {"minutes", 1}, {"hour", 60}, {"hours", 60}, {"day", 1440}, {"days", 1440}})
                if (!unit && c.take(word)) unit = minutes;
            if (!n || *n <= 0.0f || !unit || !c.done())
                return fail(line, clocked ? "How long is a number of minutes, hours or days: 30 minutes, 2 hours, 1 day (or nothing: to the next part of the day)."_tr()
                                        : "How long is a number of days: 1 day, 2 days (or nothing: to the next part of the day)."_tr());
            t.passing = std::max(1, static_cast<int>(std::lround(*n * static_cast<float>(unit))));
            return t;
        }
        while (!c.done()) {
            if (c.atOption("day")) { // day=N: that day of the story (the first is 1)
                const auto n = parseNumber(c.option()->second);
                if (!n || *n < 1.0f || *n != std::floor(*n)) return fail(line, "The day number is 1 or more: 1 is the first day."_tr());
                t.day = static_cast<int>(*n);
                continue;
            }
            const std::string word = *c.next();
            std::string clock = word; // (6:30 pm: the am or pm as a word of its own)
            if (c.at("am") || c.at("pm")) clock += *c.next();
            if (const auto weekday = weekdayNamed(word); weekday && t.weekday < 0) {
                t.weekday = *weekday;
            } else if (const auto minutes = clockMinutes(clock); minutes && t.minutes < 0 && t.part.empty()) {
                t.minutes = *minutes;
            } else if (const auto part = std::ranges::find(g.dayParts, lower(word), &std::pair<std::string, int>::first);
                       part != g.dayParts.end() && t.minutes < 0 && t.part.empty()) {
                t.part = part->first;
            } else {
                return fail(line, "'{}' isn't {}, or it's said twice."_tr(word, when));
            }
        }
        if (t.weekday < 0 && t.day == 0 && t.minutes < 0 && t.part.empty())
            return fail(line, "When it goes on to is missing: {}."_tr(when));
        if (t.weekday >= 0 && !weekdays)
            return fail(line, "A weekday needs one to start from: Settings > Time > Starts at (a date or a weekday)."_tr());
        return t;
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
                readValues(line, part); // (open chat {chat.name}...)
                const std::string verb = commandOf(part, false);
                if (verb.empty()) {
                    error(line, "'{}' isn't something a button can do."_tr(firstWord(part)));
                    continue;
                }
                auto c = command(line, verb, false, part);
                if (!c) continue;
                const auto* jump = std::get_if<cmd::Jump>(&*c);
                const auto* ui = std::get_if<cmd::Ui>(&*c);
                const std::string scene = jump ? jump->scene : ui && ui->verb == "start" ? ui->arg : std::string{};
                if (!scene.empty() && !s_.scenes.contains(scene)) error(line, "There's no scene called '{}'."_tr(scene));
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
            {"colour", ""}, {"setting", ""}, {"place", ""}, {"font", ""}, {"atlas", "sub-image"}, {"character", ""}, {"style", ""},
            {"control", ""}, {"gallery", "ending"}, {"song", "achievement"}, {"tip", "animation"}, {"screen", ""}, {"variable", ""},
        }};
        for (const auto& group : order)
            for (const Line* line : defs_)
                if (const std::string kind = headOf(line->text); kind == group[0] || kind == group[1]) {
                    const bool lenient = startsWithCapital(line->text) && line->children.empty();
                    if (!attempt(lenient, [&] { define(*line, kind); })) asText_.insert(line);
                }
        startTime();
        for (const auto& [key, line] : animationSettings_) // (setting animation_on_show and animation_on_hide: animations the scripts have)
            if (const std::string& name = key == "animation_on_show" ? s_.gameSettings.animationOnShow : s_.gameSettings.animationOnHide; !name.empty() && !s_.animations.contains(name))
                error(*line, "There's no animation called '{}'."_tr(name));
    }

    // setting time_start: a date (2025-07-14) or a weekday, then a time of day or a part of the day; none said: the first
    // part of the day for days and parts, else 8:00. Real time doesn't read it.
    void startTime() {
        GameSettings& g = s_.gameSettings;
        g.timeStartMinutes = g.time == TimeKind::Days ? g.dayParts.front().second : 480;
        if (!timeStart_.second || g.time == TimeKind::Real) return; // (real time: the computer's clock and calendar, never a start)
        const Line& line = *timeStart_.second;
        const auto w = piecesOf(timeStart_.first);
        bool clock = false;
        for (std::size_t i = 0; i < w.size(); ++i) {
            const std::string word = lower(w[i]);
            int year = 0, month = 0, day = 0;
            const auto dashes = piecesOf(word, '-');
            const auto part = std::ranges::find(g.dayParts, word, &std::pair<std::string, int>::first);
            std::string at = word; // (6:30 pm)
            if (i + 1 < w.size() && (lower(w[i + 1]) == "am" || lower(w[i + 1]) == "pm")) at += lower(w[++i]);
            if (dashes.size() == 3 && std::from_chars(dashes[0].data(), dashes[0].data() + dashes[0].size(), year).ec == std::errc{} &&
                std::from_chars(dashes[1].data(), dashes[1].data() + dashes[1].size(), month).ec == std::errc{} &&
                std::from_chars(dashes[2].data(), dashes[2].data() + dashes[2].size(), day).ec == std::errc{} && month >= 1 && month <= 12 && day >= 1 &&
                dateFromDays(daysFromDate(year, month, day))[2] == day) { // (31 June isn't a day)
                g.timeStartDate = daysFromDate(year, month, day);
                g.timeStartWeekday = weekdayOf(g.timeStartDate);
            } else if (const auto weekday = weekdayNamed(word); weekday && g.timeStartDate < 0) {
                g.timeStartWeekday = *weekday;
            } else if (const auto minutes = clockMinutes(at); minutes && !clock) {
                g.timeStartMinutes = *minutes;
                clock = true;
            } else if (part != g.dayParts.end() && !clock) {
                g.timeStartMinutes = part->second;
                clock = true;
            } else {
                return error(line, "Starts at is a date (2025-07-14) or a weekday, then a time of day (8:00) or a part of the day: not '{}'."_tr(w[i]));
            }
        }
    }

    void define(const Line& line, std::string_view kind) {
        auto cursor = words(line, afterWords(line.text, 1));
        if (!cursor) return;
        Cursor& c = *cursor;
        if (kind != "screen" && kind != "atlas" && kind != "animation" && !line.children.empty())
            error(line, "Lines indented under this one don't belong to it."_tr());

        if (kind == "colour") {
            const auto name = c.next();
            const auto value = c.next();
            const auto col = value ? parseColour(*value, &s_.palette) : std::nullopt;
            if (!name || !col || !c.done()) return error(line, "A colour line needs a name and a colour."_tr());
            s_.palette.insert_or_assign(lower(*name), *col);
        } else if (kind == "variable") { // variable NAME [VALUE] ["what it's for"]
            const auto name = c.next();
            if (!name || !isVariable(*name)) return error(line, "A variable line needs the variable's name: one word (letters, numbers and _)."_tr());
            Value start = 0;
            if (!c.done()) {
                const ScriptWord& w = c.peek();
                if (w.quoted) start = w.text;
                else if (const auto e = Expr::parse(w.text)) start = e->eval({});
                else return error(line, "'{}' isn't a value a variable can start with: a number, yes or no, or \"text\"."_tr(w.text));
                c.skip(1);
            }
            if (!c.done() && c.peek().quoted) c.skip(1); // (what it's for)
            if (!c.done()) return error(line, "A variable line is its name, the value it starts with, and what it's for, in quotes."_tr());
            s_.startValues.insert_or_assign(lower(*name), std::move(start));
        } else if (kind == "place") { // place NAME VALUE (or NAME=VALUE)
            const auto [name, value] = nameValue(c);
            const auto n = value ? stageCoordinate(*value, false) : std::nullopt;
            if (!name || !n || !c.done()) return error(line, "A place line needs a name and a number from -1 to 1."_tr());
            places_.insert_or_assign(lower(*name), *n);
        } else if (kind == "font") { // font [NAME] FILE [STYLE]
            const auto a = c.next(), b = c.next(), style = c.next();
            if (!a || !c.done())
                return error(line, "A font line needs a font file."_tr());
            if (b) s_.fonts.insert_or_assign(lower(*a), Script::FontFile{*b, style.value_or("")});
            else s_.fonts.insert_or_assign("default", Script::FontFile{*a, ""});
        } else if (kind == "setting") { // setting NAME VALUE... (or NAME=VALUE)
            if (line.file > builtinFiles_) // (the built-in menus' come before main.vnscript)
                return error(line, "'setting' lines belong in main.vnscript, with the game's other settings."_tr());
            const auto [key, value] = nameValue(c);
            if (!key) return error(line, "A setting line needs a name and a value."_tr());
            std::vector<std::string> values;
            if (value) values.push_back(*value);
            while (auto v = c.next()) values.push_back(*v);
            gameSettings(line, lower(*key), values);
        } else if (kind == "atlas") {
            atlas(line, c);
        } else if (kind == "sub-image") {
            subImage(line, c);
        } else if (kind == "animation") {
            animation(line, c);
        } else if (kind == "character") {
            character(line, c);
        } else if (kind == "style") {
            const auto name = c.next();
            if (!name) return error(line, "A style line needs a name."_tr());
            styleLines_[lower(*name)].push_back(&line);
        } else if (kind == "control") {
            control(line, c);
        } else if (const auto record = std::ranges::find(kRecordKinds, kind); record != kRecordKinds.end()) {
            this->record(line, static_cast<RecordKind>(record - kRecordKinds.begin()), c);
        } else if (kind == "screen") {
            const auto name = c.next();
            if (!name) return error(line, "A screen line needs a name."_tr());
            screenLines_.insert_or_assign(lower(*name), &line);
        }
    }

    // atlas NAME [image=FILE] [frames_in_a_row=N] [same_names_as=ATLAS], with the frames' names indented below it:
    //     expressions neutral happy sad
    //     eyes open half closed
    //     mouth closed half open
    void atlas(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name || !isName(*name))
            return error(line, "An atlas line needs a name."_tr());
        Atlas a{.name = lower(*name), .image = *name};
        std::vector<std::pair<std::string, std::string>> options;
        while (!c.done()) {
            auto opt = c.option();
            if (!opt) return error(line, "'{}' doesn't belong in an atlas line."_tr(c.peek().text));
            if (opt->first == "same_names_as") { // the same frames as an atlas above (its settings come first)
                const auto other = std::ranges::find(s_.atlases, lower(opt->second), &Atlas::name);
                if (other == s_.atlases.end())
                    return error(line, "There's no atlas called '{}' above this line."_tr(opt->second));
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
                a.image = imageName(value);
            } else if (key == "frames_in_a_row") {
                const auto n = number(line, value, key);
                if (!n) return;
                if (*n < 1.0f) return error(line, "'frames_in_a_row' needs a number from 1 up, not '{}'."_tr(*n));
                a.columns = static_cast<int>(*n);
            } else {
                return error(line, "Atlases have no setting '{}'."_tr(key));
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
                error(part, "'{}' isn't part of an atlas."_tr(what));
                continue;
            }
            // The names, then (for eyes and mouth) image= x= y= when they're a part drawn on top.
            std::vector<std::string> names;
            std::optional<Atlas::Part> layer;
            while (!w->done()) {
                if (auto opt = w->option()) {
                    if (list == &a.expressions || (opt->first != "image" && opt->first != "x" && opt->first != "y")) {
                        error(part, "'{}' has no setting '{}'."_tr(what, opt->first));
                        break;
                    }
                    if (!layer) layer.emplace();
                    if (opt->first == "image") layer->image = imageName(opt->second);
                    else if (const auto n = number(part, opt->second, opt->first)) (opt->first == "x" ? layer->x : layer->y) = *n;
                } else {
                    names.push_back(lower(*w->next()));
                }
            }
            if (!names.empty()) *list = std::move(names); // (same_names_as= may have given the names already)
            if (list->empty()) error(part, "'{}' needs a name for each frame."_tr(what));
            if (layer && layer->image.empty()) error(part, "Which image holds the {} is missing."_tr(what));
            if (list == &a.eyes) a.eyesPart = layer;
            if (list == &a.mouth) a.mouthPart = layer;
        }
        std::erase_if(s_.atlases, [&](const Atlas& other) { return other.name == a.name; }); // the last one wins
        s_.atlases.push_back(std::move(a));
    }

    // animation NAME [loop [N times]], with what it changes on the lines under it:
    //     x=-2 opacity=0
    //     x=home over 1 second ease=out
    //     opacity=100% from 0 to 0.5 seconds
    //     up=5% at 2 seconds
    //     wait 0.5 seconds
    void animation(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name || !isName(*name)) return error(line, "An animation line needs a name."_tr());
        Animation a{.name = lower(*name)};
        if (c.take("loop")) {
            a.times = 0;
            if (!c.done()) { // (loop 3 times)
                const auto n = parseNumber(c.peek().text);
                if (!n || *n < 1.0f || *n != std::floor(*n)) return error(line, "After 'loop' can only come how many times it plays."_tr());
                c.skip(1);
                if (!c.take("times") && !c.take("time")) return error(line, "After 'loop' can only come how many times it plays."_tr());
                a.times = static_cast<int>(*n);
            }
        }
        if (!c.done()) return error(line, "'{}' doesn't belong in an animation line (what it changes goes on the lines under it)."_tr(c.peek().text));
        float ended = 0.0f; // where the line before ended
        for (const Line& part : line.children) {
            auto w = words(part, part.text);
            if (!w) continue;
            if (w->take("wait")) {
                const auto n = w->number();
                if (!n || !w->done()) {
                    error(part, "'wait' needs a number of seconds."_tr());
                    continue;
                }
                ended += std::max(0.0f, *n);
                a.length = std::max(a.length, ended);
                continue;
            }
            std::vector<Animation::Change> changes;
            std::optional<float> start, seconds;
            std::optional<std::string> shape; // (shape=: the line's wipe's)
            Ease ease = Ease::InOut;
            bool easeSaid = false; // (a fade without one is linear, as a screen's fade is)
            bool ok = true;
            while (ok && !w->done()) {
                if (w->take("over") || w->take("for")) {
                    const auto n = w->number();
                    if (!n) error(part, "'over' needs a number of seconds."_tr()), ok = false;
                    else seconds = std::max(0.0f, *n);
                } else if (w->take("from")) { // from 0.5 to 1 second
                    const auto begins = w->number();
                    const auto ends = begins && w->take("to") ? w->number() : std::nullopt;
                    if (!begins || !ends) error(part, "'from' needs when it starts and when it ends."_tr()), ok = false;
                    else if (*ends < *begins) error(part, "It ends before it starts: from {} to {} seconds."_tr(*begins, *ends)), ok = false;
                    else start = std::max(0.0f, *begins), seconds = *ends - std::max(0.0f, *begins);
                } else if (w->take("at")) { // at 2 seconds: at once, then
                    const auto n = w->number();
                    if (!n) error(part, "'at' needs a number of seconds."_tr()), ok = false;
                    else start = std::max(0.0f, *n), seconds = 0.0f;
                } else if (w->atOption("ease")) {
                    const auto e = oneOf<Ease>(part, "ease", w->option()->second, kEases);
                    if (!e) ok = false;
                    else ease = *e, easeSaid = true;
                } else if (w->atOption("shape")) { // shape=IMAGE: where the wipe on this line shows it first
                    shape = imageName(w->option()->second);
                } else if (const auto opt = w->option()) {
                    const auto& [key, value] = *opt;
                    using V = Animation::Value;
                    Animation::Change change;
                    if (key == "colour") {
                        change.what = V::Colour;
                        const auto col = colour(part, value);
                        if (!col) ok = false;
                        else change.colour = *col;
                    } else if (key == "fade") { // fade=COLOUR: covered with it by the end; fade=none: uncovered again
                        change.what = V::Fade;
                        const auto col = lower(value) == "none" ? std::optional<SDL_Color>(SDL_Color{0, 0, 0, 0}) : colour(part, value);
                        if (!col) ok = false;
                        else change.colour = *col;
                    } else if (key == "x" || key == "y") { // places, as the stage's (home: its own)
                        change.what = key == "x" ? V::X : V::Y;
                        if (lower(value) == "home") change.to = std::numeric_limits<float>::quiet_NaN();
                        else if (const auto n = number(part, value, key)) change.to = *n;
                        else ok = false;
                    } else if (key == "left" || key == "right" || key == "up" || key == "down") { // by a share of the screen, from its own place
                        change.what = key == "left" || key == "right" ? V::X : V::Y;
                        change.by = true;
                        if (const auto n = number(part, value, key)) change.to = key == "left" || key == "down" ? -*n : *n;
                        else ok = false;
                    } else if (key == "opacity" || key == "zoom" || key == "rotation") {
                        change.what = key == "opacity" ? V::Opacity : key == "zoom" ? V::Zoom : V::Rotation;
                        if (const auto n = number(part, value, key)) change.to = *n;
                        else ok = false;
                    } else if (key == "wipe") { // how much of it shows (100%: all of it), from the left or as shape= has it
                        change.what = V::Wipe;
                        if (const auto n = number(part, value, key)) change.to = std::clamp(*n, 0.0f, 1.0f);
                        else ok = false;
                    } else {
                        error(part, "An animation can change x, y, left, right, up, down, opacity, zoom, rotation, colour, fade and wipe, not '{}'."_tr(key)), ok = false;
                    }
                    if (ok) changes.push_back(change);
                } else {
                    error(part, "'{}' doesn't belong in an animation's line."_tr(w->peek().text)), ok = false;
                }
            }
            if (ok && shape) { // (the wipe's shape)
                const auto wipe = std::ranges::find(changes, Animation::Value::Wipe, &Animation::Change::what);
                if (wipe == changes.end()) error(part, "'shape=' goes with a wipe on its line."_tr()), ok = false;
                else wipe->shape = *shape;
            }
            if (!ok) continue;
            if (changes.empty()) {
                error(part, "This line needs what it changes."_tr());
                continue;
            }
            for (Animation::Change& change : changes) {
                change.start = start.value_or(ended);
                change.seconds = seconds.value_or(0.0f);
                change.ease = change.what == Animation::Value::Fade && !easeSaid ? Ease::Linear : ease;
                a.changes.push_back(change);
            }
            ended = start.value_or(ended) + seconds.value_or(0.0f);
            a.length = std::max(a.length, ended);
        }
        std::ranges::stable_sort(a.changes, {}, &Animation::Change::start);
        if (a.changes.empty()) return error(line, "An animation needs what it changes, on the lines under it."_tr());
        s_.animations.insert_or_assign(a.name, std::move(a)); // the last one wins
    }

    // sub-image NAME cut_from=FILE x=N y=N width=N height=N: a part of an image, shown by its own name.
    void subImage(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name || !isName(*name)) return error(line, "A sub-image line needs a name."_tr());
        SubImage p{.name = lower(*name)};
        while (!c.done()) {
            const auto opt = c.option();
            if (!opt) return error(line, "'{}' doesn't belong in a sub-image line."_tr(c.peek().text));
            const auto& [key, value] = *opt;
            float* at = key == "x" ? &p.rect.x : key == "y" ? &p.rect.y : key == "width" ? &p.rect.w : key == "height" ? &p.rect.h : nullptr;
            if (key == "cut_from") p.image = imageName(value);
            else if (!at) return error(line, "A sub-image line has no setting '{}'."_tr(key));
            else if (const auto n = number(line, value, key)) *at = *n;
            else return;
        }
        if (p.image.empty() || p.rect.w <= 0.0f || p.rect.h <= 0.0f)
            return error(line, "A sub-image line needs the image it's cut from and the part of it."_tr());
        if (lower(p.image) == p.name) return error(line, "A sub-image can't be cut from itself: give it a name of its own."_tr());
        std::erase_if(s_.subImages, [&](const SubImage& other) { return other.name == p.name; }); // the last one wins
        s_.subImages.push_back(std::move(p));
    }

    // "NAME VALUE" or "NAME=VALUE" (setting and place lines).
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

    // control NAME keys="space mouse_left" action="..." [in_menus=yes] [when=CONDITION]
    void control(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name || !isName(*name)) return error(line, "A control line needs a name."_tr());
        ControlLine control{.line = &line, .name = lower(*name)};
        while (!c.done()) {
            const auto opt = c.option();
            if (!opt) return error(line, "'{}' doesn't belong in a control line."_tr(c.peek().text));
            const auto& [key, value] = *opt;
            if (key == "keys") {
                std::string list(value);
                std::ranges::replace(list, ',', ' ');
                auto keyWords = splitWords(list);
                for (const auto& w : keyWords.value_or(std::vector<ScriptWord>{})) {
                    const auto chord = parseKeyChord(w.text);
                    if (!chord)
                        return error(line, "'{}' isn't a key name."_tr(w.text));
                    control.keys.push_back(*chord);
                }
            } else if (key == "action") {
                control.action = value;
            } else if (key == "in_menus") {
                control.inMenus = boolean(line, value, key).value_or(false);
            } else if (key == "when") {
                control.condition = value;
            } else {
                return error(line, "Controls have no setting '{}'."_tr(key));
            }
        }
        if (control.action.empty()) return error(line, "The control's action is missing."_tr());
        std::erase_if(controlLines_, [&](const ControlLine& other) { return other.name == control.name; }); // the last one wins
        controlLines_.push_back(std::move(control));
    }

    // character NAME setting=value ...
    void character(const Line& line, Cursor& c) {
        const auto name = c.next();
        if (!name) return error(line, "A character line needs a name."_tr());
        const GameSettings& game = s_.gameSettings;
        CharacterDef def{.key = lower(*name), .name = *name, .image = characterImageName(*name), .madeUpVoiceSpeed = game.madeUpVoiceSpeed,
                         .blink = game.blink, .secondsBetweenBlinks = game.secondsBetweenBlinks, .mouthMoves = game.mouthsMove, .mouthSpeed = game.mouthSpeed};
        std::optional<bool> faceBlink;
        std::optional<MouthMoves> faceMouth;
        while (!c.done()) {
            const auto opt = c.option();
            if (!opt) return error(line, "'{}' doesn't belong in a character line."_tr(c.peek().text));
            const auto& [key, value] = *opt;
            // Each sets its field when the value is right, and says whether it was.
            auto flag = [&](bool& out) { return set(out, boolean(line, value, key)); };
            auto within = [&](float& out, float lo, float hi) {
                const auto n = number(line, value, key);
                return set(out, n ? std::optional(std::clamp(*n, lo, hi)) : std::nullopt);
            };
            auto sync = [&](FaceCopies& out) -> bool { // yes, no, or what it copies: face_copies=expression,blink
                if (const auto b = parseBool(value)) return out = {*b, *b, *b}, true;
                out = {false, false, false};
                for (auto&& part : std::views::split(std::string_view(value), ',')) {
                    const std::string_view written = trim(std::string_view(part.begin(), part.end()));
                    const std::string word = lower(written);
                    if (word == "expression") out.expression = true;
                    else if (word == "blink") out.blink = true;
                    else if (word == "mouth") out.mouth = true;
                    else return error(line, "'face_copies' can be yes, no, or what the face copies (expression, blink, mouth), not '{}'."_tr(written)), false;
                }
                return true;
            };
            bool ok = true;
            if (key == "name_colour" || key == "text_colour") {
                const auto col = colour(line, value);
                ok = col.has_value();
                if (col && key == "name_colour") def.nameColour = *col;
                else if (col) def.textColour = *col;
            }
            else if (key == "image") def.image = imageName(value);
            else if (key == "text_appears") ok = (def.textAppears = oneOf<TextAppears>(line, key, value, kTextAppears)).has_value();
            else if (key == "textbox") def.textbox = lower(value);
            else if (key == "typing_sound") def.typingSound = value;
            else if (key == "voice_files") { // yes: sounds/voices/<name>/, or sounds/voices/<value>/
                const auto b = parseBool(value);
                def.voiceFiles = b == false ? "" : std::format("voices/{}", b ? characterImageName(*name) : imageName(value));
            }
            else if (key == "made_up_voice") { // high, medium, low, deep, no, or a pitch in Hz
                static constexpr std::pair<std::string_view, float> heights[]{{"high", 250.0f}, {"medium", 190.0f}, {"low", 135.0f}, {"deep", 105.0f}};
                const auto height = std::ranges::find(heights, lower(value), &std::pair<std::string_view, float>::first);
                if (height != std::end(heights)) def.madeUpVoice = height->second;
                else if (parseBool(value) == false) def.madeUpVoice = 0.0f;
                else if (const auto hz = parseNumber(value); hz && *hz >= 50.0f && *hz <= 600.0f) def.madeUpVoice = *hz;
                else return error(line, "'made_up_voice' can be high, medium, low, deep, no or a pitch, not '{}'."_tr(value));
            }
            else if (key == "made_up_voice_speed") ok = within(def.madeUpVoiceSpeed, 0.25f, 4.0f);
            else if (key == "blink") ok = flag(def.blink);
            else if (key == "seconds_between_blinks") ok = within(def.secondsBetweenBlinks, 0.5f, 60.0f);
            else if (key == "mouth_moves") ok = set(def.mouthMoves, mouthMoves(line, key, value));
            else if (key == "mouth_speed") ok = within(def.mouthSpeed, 0.5f, 20.0f);
            else if (key == "face") def.face = value; // an image name, or yes / no
            else if (key == "face_side") {
                if (!isName(value)) return error(line, "'face_side' is one word, not '{}'."_tr(value));
                def.faceSide = lower(value);
            }
            else if (key == "face_copies") ok = sync(def.faceCopies);
            else if (key == "avatar") def.avatar = imageName(value);
            else if (key == "about") {
                def.about = value;
                checkTextOf(line, value);
            }
            else if (key == "contact") def.contact = formula(line, value); // (in the phone's people while it's true)
            else if (key == "ringtone") def.ringtone = value;
            else if (key == "chat" || key == "call") { // the scene played when the player opens their chat, or calls them
                if (!isName(lower(value))) return error(line, "'{}' needs a scene's name, not '{}'."_tr(key, value));
                (key == "chat" ? def.chatScene : def.callScene) = lower(value);
                phoneSceneNames_.push_back({.file = line.file, .number = line.number, .scene = lower(value)});
            }
            else if (key == "face_flip") ok = flag(def.faceFlip);
            else if (key == "face_blink") ok = (faceBlink = boolean(line, value, key)).has_value();
            else if (key == "face_mouth") ok = (faceMouth = mouthMoves(line, key, value)).has_value();
            else return error(line, "Characters have no setting '{}'."_tr(key));
            if (!ok) return;
        }
        if (const auto yes = parseBool(def.face)) def.face = *yes ? def.image + "_face" : ""; // face=yes: images/<image>_face.png
        else def.face = imageName(def.face);
        def.faceBlink = faceBlink.value_or(def.blink); // the face does what the image on stage does, unless told
        def.faceMouth = faceMouth.value_or(def.mouthMoves);
        if (!def.textbox.empty()) textboxes_.emplace_back(&line, def.textbox);
        s_.characters.insert_or_assign(def.key, std::move(def));
    }

    // Sets `out` to `value` when it has one; says whether it had.
    template <class T>
    static bool set(T& out, const std::optional<T>& value) {
        if (value) out = *value;
        return value.has_value();
    }

    // gallery IMAGE "Title" | ending NAME "Title" [hint="..."] | song NAME "Title" | achievement NAME "Title" "Description" [secret]
    // | tip NAME "Title" "Description"
    void record(const Line& line, RecordKind kind, Cursor& c) {
        const auto name = c.next();
        if (!name || (kind != RecordKind::Gallery && !isName(*name))) return error(line, "'{}' needs a name."_tr(kRecordKinds[static_cast<std::size_t>(kind)]));
        if ((kind == RecordKind::Ending || kind == RecordKind::Achievement || kind == RecordKind::Tip) && name->contains('-')) // (what keeps it: persistent.ending.NAME)
            return error(line, "'{}' can't be its name (one word of letters, numbers and _)."_tr(*name));
        Script::Record r{.name = *name, .title = c.next().value_or(*name)};
        if (r.title.empty()) r.title = *name; // (a title left empty, "": the name)
        while (!c.done()) {
            if (kind == RecordKind::Achievement && c.take("secret")) r.secret = true;
            else if (kind == RecordKind::Ending && c.atOption("hint")) r.text = c.option()->second;
            else if ((kind == RecordKind::Achievement || kind == RecordKind::Tip) && r.text.empty() && (c.peek().quoted || !c.peek().text.contains('=')))
                r.text = *c.next();
            else return error(line, "'{}' doesn't belong in this {} line."_tr(c.peek().text, kRecordKinds[static_cast<std::size_t>(kind)]));
        }
        auto& list = s_.lists[static_cast<std::size_t>(kind)];
        std::erase_if(list, [&](const Script::Record& other) { return sameName(other.name, r.name); }); // the last one wins
        list.push_back(std::move(r));
    }

    // setting NAME VALUE (one value, or two for game_size and pointer_tip)
    void gameSettings(const Line& line, const std::string& key, const std::vector<std::string>& v) {
        GameSettings& c = s_.gameSettings;
        struct Number {
            std::string_view key;
            float GameSettings::*field;
            float lo, hi;
        };
        static constexpr std::array numbers{
            Number{"skip_time", &GameSettings::skipTime, 0.0f, 10.0f},
            Number{"auto_letter_delay", &GameSettings::autoLetterDelay, 0.0f, 10.0f},
            Number{"character_height", &GameSettings::characterHeight, 0.05f, 10.0f},
            Number{"move_time", &GameSettings::moveTime, 0.0f, 60.0f},
            Number{"music_crossfade", &GameSettings::musicCrossfade, 0.0f, 60.0f},
            Number{"glide_speed", &GameSettings::glideSpeed, 0.0f, 10.0f},
            Number{"glide_distance", &GameSettings::glideDistance, 0.05f, 10.0f},
            Number{"scrollbar_thickness", &GameSettings::scrollbarThickness, 1.0f, 64.0f},
            Number{"fading_edges", &GameSettings::fadingEdges, 0.0f, 1000.0f},
            Number{"typing_sound_gap", &GameSettings::typingSoundGap, 0.01f, 10.0f},
            Number{"typing_sound_volume", &GameSettings::typingSoundVolume, 0.0f, 1.0f},
            Number{"made_up_voice_speed", &GameSettings::madeUpVoiceSpeed, 0.25f, 4.0f},
            Number{"seconds_between_blinks", &GameSettings::secondsBetweenBlinks, 0.5f, 60.0f},
            Number{"mouth_speed", &GameSettings::mouthSpeed, 0.5f, 20.0f},
            Number{"phone_typing", &GameSettings::phoneTyping, 0.0f, 100.0f},
            Number{"music_under_voices", &GameSettings::musicUnderVoices, 0.0f, 1.0f},
        };
        struct Whole {
            std::string_view key;
            int GameSettings::*field;
            int lo, hi;
        };
        static constexpr std::array wholes{
            Whole{"save_pages", &GameSettings::savePages, 1, 99}, Whole{"page_slots", &GameSettings::pageSlots, 1, 60},
            Whole{"history_length", &GameSettings::historyLength, 1, 100000}, Whole{"rollback_lines", &GameSettings::rollbackLines, 0, 10000},
            Whole{"load_ahead", &GameSettings::loadAhead, 0, 1000}, Whole{"image_memory", &GameSettings::imageMemory, 0, 65536},
        };
        static const std::map<std::string_view, bool GameSettings::*> flags{
            {"autosave", &GameSettings::autosave}, {"text_follows_voices", &GameSettings::textFollowsVoices}, {"developer", &GameSettings::developer}, {"glide", &GameSettings::glide},
            {"blink", &GameSettings::blink}, {"resizable", &GameSettings::resizable}, {"phone_history", &GameSettings::phoneHistory},
            {"phone_follows_time", &GameSettings::phoneFollowsTime}, {"backgrounds_follow_time", &GameSettings::backgroundsFollowTime}};
        static const std::map<std::string_view, SDL_Color GameSettings::*> colours{
            {"narration_colour", &GameSettings::narrationColour},
            {"letterbox_colour", &GameSettings::letterboxColour}};
        static const std::map<std::string_view, std::string GameSettings::*> texts{
            {"title", &GameSettings::title}, {"version", &GameSettings::version}, {"save_folder", &GameSettings::saveFolder}, {"icon", &GameSettings::icon},
            {"mouse_pointer", &GameSettings::mousePointer}, {"language", &GameSettings::language}, {"phone_message_sound", &GameSettings::phoneMessageSound},
            {"phone_send_sound", &GameSettings::phoneSendSound}, {"phone_typing_sound", &GameSettings::phoneTypingSound},
            {"phone_ringtone", &GameSettings::phoneRingtone}, {"phone_calling_sound", &GameSettings::phoneCallingSound}};
        static constexpr std::pair<std::string_view, bool> filters[]{{"smooth", false}, {"pixel", true}};
        static constexpr std::pair<std::string_view, SDL_Keymod> holds[]{
            {"ctrl", SDL_KMOD_CTRL}, {"shift", SDL_KMOD_SHIFT}, {"alt", SDL_KMOD_ALT}, {"none", 0}};

        const std::size_t needs = key == "game_size" && v.size() == 1 && v[0].contains('x') ? 1
                                : key == "game_size" || key == "pointer_tip"               ? 2
                                                                                               : 1;
        if (v.size() < needs) return error(line, needs == 1 ? "'setting {}' needs 1 value."_tr(key) : "'setting {}' needs {} values."_tr(key, needs));
        const std::string& value = v[0];
        if (const auto n = std::ranges::find(numbers, std::string_view(key), &Number::key); n != numbers.end()) {
            if (auto x = number(line, value, key)) c.*(n->field) = std::clamp(*x, n->lo, n->hi);
        } else if (const auto w = std::ranges::find(wholes, std::string_view(key), &Whole::key); w != wholes.end()) {
            if (auto x = number(line, value, key)) c.*(w->field) = std::clamp(static_cast<int>(*x), w->lo, w->hi);
        } else if (const auto f = flags.find(key); f != flags.end()) {
            set(c.*(f->second), boolean(line, value, key));
        } else if (const auto col = colours.find(key); col != colours.end()) {
            set(c.*(col->second), colour(line, value));
        } else if (const auto t = texts.find(key); t != texts.end()) {
            c.*(t->second) = value;
        } else if (key == "game_size") { // game_size 1280 720 (or 1280x720)
            const bool joined = v.size() == 1;
            const auto width = number(line, joined ? value.substr(0, value.find('x')) : value, key);
            const auto height = number(line, joined ? value.substr(value.find('x') + 1) : v[1], key);
            if (width && height) {
                c.width = std::clamp(static_cast<int>(*width), 160, 7680);
                c.height = std::clamp(static_cast<int>(*height), 120, 4320);
            }
        } else if (key == "pointer_tip") { // pointer_tip X Y
            const auto x = number(line, value, key), y = number(line, v[1], key);
            if (x && y) c.pointerTip = {static_cast<int>(*x), static_cast<int>(*y)};
        } else if (key == "image_scaling") {
            set(c.pixelArt, oneOf<bool>(line, key, value, filters));
        } else if (key == "hold_to_skip") {
            set(c.holdToSkip, oneOf<SDL_Keymod>(line, key, value, holds));
        } else if (key == "mouths_move") { // yes, voice or no: for every character without mouth_moves=
            set(c.mouthsMove, mouthMoves(line, key, value));
        } else if (key == "text_appears") {
            set(c.textAppears, oneOf<TextAppears>(line, key, value, kTextAppears));
        } else if (key == "has_phone") { // has_phone CONDITION: the player has their phone while it's true
            c.hasPhone = formula(line, value);
        } else if (key == "phone_owner" || key == "phone_wallpaper" || key == "phone_lock_wallpaper" || key == "phone_passcode" || key == "phone_clock" ||
                   key == "phone_date") { // (the phone's variables, as they start)
            s_.startValues.insert_or_assign(key, unquote(value));
        } else if (key == "phone_signal") {
            if (const auto on = boolean(line, value, key)) s_.startValues.insert_or_assign(key, *on ? 1 : 0);
        } else if (key == "phone_battery") { // (how full: 0 to 100)
            if (const auto full = number(line, value, key)) s_.startValues.insert_or_assign(key, std::clamp(static_cast<int>(std::lround(*full)), 0, 100));
        } else if (key == "new_messages") { // button (the Phone button counts it) or open (the phone opens): what tells players a message has come
            set(c.newMessages, oneOf<NewMessages>(line, key, value, kNewMessages));
        } else if (key == "chat_messages") { // typed or click: how a chat scene's messages come
            set(c.chatMessages, oneOf<ChatMessages>(line, key, value, kChatMessages));
        } else if (key == "time") { // none, story, real or days: the story's time
            set(c.time, oneOf<TimeKind>(line, key, value, kTimeKinds));
        } else if (key == "time_start") { // a date or a weekday, then a time or a part of the day (read once the parts are)
            std::string all;
            for (const std::string& word : v) all += (all.empty() ? "" : " ") + word;
            timeStart_ = {all, &line};
        } else if (key == "time_parts") { // "morning 6:00, afternoon 12:00, ...": each part of the day and when it starts
            std::vector<std::pair<std::string, int>> parts;
            std::string all;
            for (const std::string& word : v) all += (all.empty() ? "" : " ") + word;
            for (const std::string_view part : piecesOf(all, ',')) {
                const auto said = piecesOf(part);
                const auto starts = said.size() >= 2 ? clockMinutes(part.substr(said.front().size())) : std::nullopt;
                if (!starts || !isName(said.front()) || std::ranges::contains(parts, lower(said.front()), &std::pair<std::string, int>::first))
                    return error(line, "time_parts lists each part of the day with when it starts, commas between them: morning 6:00, evening 18:00."_tr());
                parts.emplace_back(lower(said.front()), *starts);
            }
            if (parts.empty()) return error(line, "time_parts lists each part of the day with when it starts, commas between them: morning 6:00, evening 18:00."_tr());
            std::ranges::sort(parts, {}, &std::pair<std::string, int>::second);
            c.dayParts = std::move(parts);
        } else if (key == "clock") { // 24 or 12: how times show (18:30, or 6:30 PM)
            if (value == "24" || value == "12") c.clock12 = value == "12";
            else error(line, "'clock' can be 24 or 12, not '{}'."_tr(value));
        } else if (key == "animation_on_show" || key == "animation_on_hide") { // what show and hide lines play when they name none (checked once the animations are read)
            (key == "animation_on_show" ? c.animationOnShow : c.animationOnHide) = lower(value) == "none" ? std::string{} : lower(value);
            std::erase_if(animationSettings_, [&](const auto& kept) { return kept.first == key; });
            animationSettings_.emplace_back(key, &line);
        } else if (key.starts_with("default.")) { // a player setting's starting value: a number, yes / no, or (scaling, mouths) its word
            const std::string setting = key.substr(8);
            if (!std::ranges::contains(kSettingNames, setting) || setting == "window_size" || setting == "language")
                return error(line, "There's no player setting called '{}'."_tr(setting));
            if (const auto start = settingNumber(line, setting, key, value)) c.settingDefaults[setting] = *start;
        } else {
            error(line, "There's no setting called '{}'."_tr(key));
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
            return prefix == "pointed" ? &s.pointed : prefix == "chosen" ? &s.chosen : prefix == "unclickable" ? &s.unclickable : nullptr;
        };
        const auto us = key.find('_');
        Style::State* st = us == std::string_view::npos ? nullptr : state(key.substr(0, us));
        const std::string_view field = st ? key.substr(us + 1) : key;

        if (st && (field == "colour" || field == "background" || field == "border")) { // pointed_colour= chosen_background= unclickable_border=...
            const auto c = colour(line, value);
            if (c) (field == "colour" ? st->colour : field == "background" ? st->background : st->border) = c;
            if (c && field == "background") { // (navy-black: fading down to another)
                const auto grad = parseGradient(value, &s_.palette);
                st->backgroundBottom = grad ? std::optional(grad->second) : std::nullopt;
            }
        }
        else if (st && field == "image") st->image = std::string(value);
        else if (key == "font") s.font = lower(value);
        else if (key == "size") set(s.size, at(1.0f));
        else if (key == "colour") {
            s.speakerColour = value == "speaker";
            if (!s.speakerColour) set(s.colour, colour(line, value));
        }
        else if (key == "background") { // (navy-black: fading down to another)
            set(s.background, colour(line, value));
            const auto grad = parseGradient(value, &s_.palette);
            s.backgroundBottom = grad ? std::optional(grad->second) : std::nullopt;
        }
        else if (key == "border") set(s.border, colour(line, value));
        else if (key == "image") s.image = std::string(value);
        else if (key == "align" || key == "align_vertically") {
            if (auto a = toAlign(value)) (key == "align" ? s.align : s.alignVertically) = *a;
            else error(line, "'{}' can be left, centre or right (top, centre or bottom for align_vertically), not '{}'."_tr(key, value));
        }
        else if (key == "padding") set(s.padding, at(0.0f));
        else if (key == "opacity") {
            if (const auto n = number(line, value, key)) s.opacity = std::clamp(*n, 0.0f, 1.0f);
        }
        else if (key == "shadow") set(s.shadow.on, boolean(line, value, key));
        else if (key == "shadow_colour") set(s.shadow.colour, colour(line, value));
        else if (key == "shadow_offset") set(s.shadow.offset, at(0.0f));
        else if (key == "line_spacing") set(s.lineSpacing, at(0.1f));
        else if (key == "opacity_follows") set(s.opacityFollows, boolean(line, value, key));
        else if (key == "text_size_follows") set(s.textSizeFollows, boolean(line, value, key));
        else if (key == "pointed_sound") s.pointedSound = std::string(value);
        else if (key == "click_sound") s.clickSound = std::string(value);
        else return false;
        return true;
    }

    std::optional<Style> style(const std::string& name, const Line& user, int depth = 0) {
        if (auto it = styles_.find(name); it != styles_.end()) return it->second;
        const auto raw = styleLines_.find(name);
        if (raw == styleLines_.end()) return std::nullopt;
        if (depth > 16) {
            error(user, "Style '{}' builds on itself."_tr(name));
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
                    error(*line, "'{}' doesn't belong in a style line."_tr(*c->next()));
                }
            }
        }
        std::string parent = name == "default" ? "" : "default";
        for (const auto& [line, kv] : props)
            if (kv.first == "based_on") parent = lower(kv.second);
        Style s;
        if (!parent.empty()) {
            if (auto base = style(parent, *raw->second.front(), depth + 1)) s = *base;
            else if (parent != "default") error(*raw->second.front(), "There's no style called '{}'."_tr(parent));
        }
        for (const auto& [line, kv] : props)
            if (kv.first != "based_on" && !styleProp(s, kv.first, kv.second, *line))
                error(*line, "Styles have no setting '{}'."_tr(kv.first));
        return styles_.emplace(name, s).first->second;
    }

    void screens() {
        for (const auto& [name, line] : screenLines_) {
            ScreenDef def{.name = name};
            auto c = words(*line, afterWords(line->text, 2));
            while (c && !c->done()) {
                const auto opt = c->option();
                if (!opt) {
                    error(*line, "'{}' doesn't belong in a screen line."_tr(*c->next()));
                    continue;
                }
                const auto& [key, value] = *opt;
                if (key == "story") {
                    if (const std::string v = lower(value); v == "waits" || v == "goes_on") def.storyWaits = v == "waits";
                    else error(*line, "'{}' can be {}, not '{}'."_tr(key, "waits or goes_on", value));
                }
                else if (key == "action_on_open") def.actionOnOpen = actions(*line, value);
                else if (key == "close_after") def.closeAfter = std::max(0.0f, number(*line, value, key).value_or(0.0f));
                else if (key == "action_on_close") def.actionOnClose = actions(*line, value);
                else if (key == "animation_on_open" || key == "animation_on_close") {
                    if (const auto a = s_.animations.find(lower(value)); a != s_.animations.end())
                        (key == "animation_on_open" ? def.animationOnOpen : def.animationOnClose) = std::make_shared<const Animation>(a->second);
                    else error(*line, "There's no animation called '{}'."_tr(value));
                }
                else if (key == "stop_music_on_open") def.stopOnOpen.music = boolean(*line, value, key).value_or(false);
                else if (key == "stop_music_on_close") def.stopOnClose.music = boolean(*line, value, key).value_or(false);
                else if (key == "stop_voice_on_open") def.stopOnOpen.voice = boolean(*line, value, key).value_or(false);
                else if (key == "stop_voice_on_close") def.stopOnClose.voice = boolean(*line, value, key).value_or(false);
                else error(*line, "Screens have no setting '{}'."_tr(key));
            }
            elements(def, line->children, -1);
            s_.screens.insert_or_assign(name, std::move(def));
        }
        for (const auto& [line, screen] : textboxes_)
            if (!s_.screens.contains(screen)) error(*line, "There's no screen called '{}' to use as a textbox."_tr(screen));
    }

    // A screen's parts, each followed by the ones in it (indented under it: their places are in its box).
    void elements(ScreenDef& def, const std::vector<Line>& lines, int parent) {
        for (const Line& line : lines) {
            auto el = element(line);
            if (!el) continue;
            el->parent = parent;
            const auto list = std::ranges::find(kLists, std::string_view(el->option("list")), &std::pair<std::string_view, std::string_view>::first);
            def.elements.push_back(std::move(*el));
            if (list != kLists.end()) items_.emplace_back(list->second); // (the parts in it read its items' values: {chat.name}...)
            elements(def, line.children, static_cast<int>(def.elements.size()) - 1);
            if (list != kLists.end()) items_.pop_back();
        }
    }

    std::optional<Element> element(const Line& line) {
        auto cursor = words(line, line.text);
        if (!cursor) return std::nullopt;
        Cursor& c = *cursor;
        const std::string kind = lower(*c.next());
        const auto spec = std::ranges::find(kKinds, std::string_view(kind), &KindSpec::name);
        if (spec == kKinds.end()) {
            error(line, "'{}' isn't a screen part."_tr(kind));
            return std::nullopt;
        }
        Element el{.kind = spec->kind, .file = line.file, .line = line.number};
        if (!c.done() && (c.peek().quoted || c.peek().text.find('=') == std::string::npos)) el.text = *c.next();
        std::vector<std::pair<std::string, std::string>> props;
        while (!c.done()) {
            if (auto opt = c.option()) props.push_back(std::move(*opt));
            else error(line, "'{}' isn't a setting."_tr(*c.next()));
        }
        if (line.file < builtinFiles_) // (written for 1280 x 720)
            for (auto& kv : props) scaleBuiltIn(kv);

        // Base style: style=NAME, else the style named after the element kind, else "default".
        std::string styleName(partStyle(spec->name, [&](std::string_view name) { return styleLines_.contains(name); }));
        for (const auto& [k, v] : props)
            if (k == "style") {
                styleName = lower(v);
                if (!styleLines_.contains(styleName)) error(line, "There's no style called '{}'."_tr(v));
            }
        el.style = style(styleName, line).value_or(Style{});

        auto allowed = [&](std::string_view key) { return std::ranges::contains(spec->options, key); };
        for (const auto& [k, v] : props) {
            if (k == "style" || k == "name") continue; // (name=: what the editor calls it)
            if (k == "animation" || k == "pointed_animation" || k == "message_animation" || (allowed(k) && (k == "click_animation" || k == "item_animation"))) { // (any part's; a button's when it's clicked; a list's items')
                if (const auto a = s_.animations.find(lower(v)); a != s_.animations.end())
                    (k == "animation" ? el.animation : k == "pointed_animation" ? el.pointedAnimation : k == "message_animation" ? el.messageAnimation
                     : k == "item_animation" ? el.itemAnimation : el.clickAnimation) = std::make_shared<const Animation>(a->second);
                else error(line, "There's no animation called '{}'."_tr(v));
            } else if (k == "x" || k == "y" || k == "width" || k == "height") { // (places from -1 to 1, shares of its box)
                if (auto n = number(line, v, k)) (k == "x" ? el.placeX : k == "y" ? el.placeY : k == "width" ? el.shareWidth : el.shareHeight) = *n;
            } else if (k == "when") {
                el.visible = formula(line, v);
            } else if (allowed(k) && k == "action") {
                el.actions = actions(line, v);
            } else if (allowed(k) && k == "drop") {
                el.drop = actions(line, v);
            } else if (allowed(k) && k == "confirm") {
                el.confirm = v;
            } else if (allowed(k) && (std::ranges::contains(kFormulaOptions, k) || (k == "count" && el.kind == ElementKind::Container))) {
                if (auto e = formula(line, v)) el.exprs.insert_or_assign(k, std::move(*e));
            } else if (allowed(k) && k == "keys") { // (as a control's: several, by spaces or commas)
                std::string list(v);
                std::ranges::replace(list, ',', ' ');
                for (const std::string& word : rawWords(list))
                    if (const auto chord = parseKeyChord(word)) el.keys.push_back(*chord);
                    else error(line, "'{}' isn't a key name."_tr(word));
            } else if (allowed(k) && (k == "clickable" || k == "chosen")) {
                (k == "clickable" ? el.clickable : el.chosen) = formula(line, v);
            } else if (allowed(k) && k.ends_with("_style")) {
                if (auto s = style(lower(v), line)) el.substyles.insert_or_assign(k, *s);
                else error(line, "There's no style called '{}'."_tr(v));
            } else if (allowed(k) && std::ranges::contains(kColourOptions, k)) {
                if (auto col = colour(line, v)) el.colours.insert_or_assign(k, *col);
            } else if (allowed(k)) {
                el.props.insert_or_assign(k, k == "variable" ? lower(v) : v);
            } else { // (its look: its style's alone, style=)
                error(line, "'{}' has no setting '{}'."_tr(kind, k));
            }
        }
        if (el.kind == ElementKind::Text || el.kind == ElementKind::Button) checkTextOf(line, el.text);
        else readValues(line, el.text); // (an image's or a movie's name: {chat.avatar}...)
        if (!el.confirm.empty()) checkTextOf(line, el.confirm);
        for (const auto& [key, value] : el.props) readValues(line, value); // (an app's title, an input's placeholder...)

        switch (el.kind) {
        case ElementKind::Image: // (an image by name, a colour, or a box of its style's background; one fading down with bottom=)
            if (el.text.empty() && !el.colours.contains("bottom") && el.style.background.a == 0) error(line, "Which image to show is missing."_tr());
            break;
        case ElementKind::Movie:
            if (el.text.empty()) error(line, "Which movie to play is missing."_tr());
            break;
        case ElementKind::Input:
        case ElementKind::Slider:
            if (el.kind == ElementKind::Slider && !el.option("setting").empty()) { // (a player setting: a volume, the text's speed...)
                const SettingControl* setting = findSettingControl(lower(el.option("setting")));
                if (!setting) error(line, "'{}' isn't a player setting a slider can change."_tr(el.option("setting")));
                if (!el.option("variable").empty()) error(line, "A slider changes a variable ('variable=') or a player setting ('setting='), not both."_tr());
                break;
            }
            if (el.kind == ElementKind::Slider && el.option("variable").empty()) error(line, "Which variable or player setting the slider changes is missing."_tr());
            if (!el.option("variable").empty() && !isVariable(el.option("variable")))
                error(line, "'{}' can't be a variable name."_tr(el.option("variable")));
            else if (!el.option("variable").empty()) wrote(el.option("variable"));
            break;
        case ElementKind::Bar:
            if (!el.exprs.contains("value")) error(line, "Which value the bar shows is missing."_tr());
            break;
        case ElementKind::Container: { // (list=, stack=, position=, app=: each one of its words)
            if (const std::string list = lower(el.option("list")); !list.empty()) {
                if (std::ranges::find(kLists, std::string_view(list), &std::pair<std::string_view, std::string_view>::first) == kLists.end())
                    error(line, "There's no list called '{}'."_tr(el.option("list")));
                el.props.insert_or_assign("list", list);
            }
            static constexpr std::pair<std::string_view, std::string_view> kWordsOf[]{{"stack", "no down across"}, {"position", "top centre bottom"}};
            for (const auto& [key, words] : kWordsOf)
                if (const std::string v = lower(el.option(key)); !v.empty()) {
                    if (!std::ranges::contains(std::views::split(words, ' ') | std::views::transform([](auto&& w) { return std::string_view(w.begin(), w.end()); }), std::string_view(v)))
                        error(line, "'{}' can be {}, not '{}'."_tr(key, key == "stack" ? "no, down or across" : "top, centre or bottom", el.option(key)));
                    el.props.insert_or_assign(std::string(key), v);
                }
            if (const std::string app = lower(el.option("app")); !app.empty()) {
                if (!isName(app)) error(line, "'{}' can't be an app name (one word of letters, numbers, _ and -)."_tr(el.option("app")));
                el.props.insert_or_assign("app", app);
            }
            break;
        }
        case ElementKind::Control:
            el.text = lower(el.text);
            if (std::ranges::none_of(controlLines_, [&](const ControlLine& known) { return known.name == el.text; }))
                error(line, "There's no control called '{}'."_tr(el.text));
            if (el.number("slot", 1.0f) < 1.0f) error(line, "'slot' needs a number from 1 up, not '{}'."_tr(el.number("slot", 1.0f)));
            break;
        default:
            break;
        }
        return el;
    }

    // A built-in screen's (or style's) text size, padding or bar, for a game of another size than the
    // 1280 x 720 they're written for (places and shares fit any size).
    void scaleBuiltIn(std::pair<std::string, std::string>& kv) const {
        const float sy = static_cast<float>(s_.gameSettings.height) / 720.0f;
        static constexpr std::string_view kSizes[]{"size", "padding", "bar"};
        if (sy == 1.0f || !std::ranges::contains(kSizes, kv.first)) return;
        if (const auto n = parseNumber(kv.second)) kv.second = std::format("{}", std::lround(*n * sy));
    }

    void bindings() {
        const std::size_t first = s_.controls.size();
        for (const auto& c : controlLines_)
            s_.controls.push_back({.name = c.name, .keys = c.keys, .actions = actions(*c.line, c.action), .inMenus = c.inMenus,
                                   .condition = c.condition.empty() ? std::nullopt : formula(*c.line, c.condition)});
        // A key the game gives one of its own controls isn't a built-in one's (on screens, or in the story, as they are): a
        // game's "a" for its map stays the map's, and auto-forward keeps its controller button.
        auto builtIn = [&](std::size_t i) { return controlLines_[i].line->file < builtinFiles_; };
        for (std::size_t i = 0; i < controlLines_.size(); ++i)
            if (builtIn(i))
                for (std::size_t j = 0; j < controlLines_.size(); ++j)
                    if (!builtIn(j) && s_.controls[first + j].onScreens() == s_.controls[first + i].onScreens())
                        std::erase_if(s_.controls[first + i].keys, [&](const KeyChord& k) { return std::ranges::contains(s_.controls[first + j].keys, k); });
    }

    // -- Scenes -----------------------------------------------------------------------

    void resolve() {
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

    Script& s_;
    const FileReader& read_;
    Phase phase_ = Phase::Definitions;
    std::size_t builtinFiles_ = 0; // the built-in menus' files: main.vnscript is the next one
    std::set<std::string> included_;
    std::vector<const Line*> defs_;
    std::set<const Line*> asText_; // capitalised lines that didn't work as definitions
    std::vector<std::pair<std::string, const Line*>> animationSettings_; // setting animation_on_show / animation_on_hide, and where
    std::pair<std::string, const Line*> timeStart_{}; // setting time_start, and where
    // Places for "show NAME at PLACE": these, and the game's "place" lines, as the stage has them (as
    // stageCoordinate gives them: 0 at the left edge, 1 at the right; left is -0.6 in a place line).
    std::map<std::string, float, std::less<>> places_{
        {"farleft", 0.0f}, {"left", 0.2f}, {"centre", 0.5f}, {"right", 0.8f}, {"farright", 1.0f}};
    std::map<std::string, std::vector<const Line*>, std::less<>> styleLines_; // repeated definitions merge
    std::map<std::string, Style, std::less<>> styles_;
    std::map<std::string, const Line*, std::less<>> screenLines_;             // the last definition wins
    struct ControlLine {
        const Line* line = nullptr;
        std::string name;
        std::vector<KeyChord> keys;
        std::string action, condition;
        bool inMenus = false;
    };
    std::vector<ControlLine> controlLines_; // the last definition of a name wins
    std::vector<std::pair<const Line*, std::string>> textboxes_;              // character textbox screens to check
    std::string currentScene_ = "line";                // the scene the story lines being read are in
    std::map<std::string, int, std::less<>> lineCounts_; // each character's lines in it so far (voice ids)
};

} // namespace

bool partTakes(std::string_view kind, std::string_view key) {
    const auto spec = std::ranges::find(kKinds, kind, &KindSpec::name);
    return spec != kKinds.end() && std::ranges::contains(spec->options, key);
}

std::string_view partStyle(std::string_view kind, const std::function<bool(std::string_view)>& hasStyle) {
    const auto spec = std::ranges::find(kKinds, kind, &KindSpec::name);
    return spec != kKinds.end() && !spec->style.empty() && hasStyle(spec->style) ? spec->style : "default";
}

// ---------------------------------------------------------------------------
// Script
// ---------------------------------------------------------------------------

Script Script::load(const std::string& entry, const FileReader& read, std::string_view builtin) {
    Script script;
    Parser(script, read).run(entry, builtin);
    std::ranges::stable_sort(script.errors, {}, [](const ScriptError& e) { return std::tie(e.file, e.line); });
    return script;
}

std::optional<std::size_t> Script::findScene(std::string_view name) const {
    if (auto it = scenes.find(name); it != scenes.end()) return it->second;
    return std::nullopt;
}

const CharacterDef* Script::character(std::string_view key) const {
    auto it = characters.find(key);
    return it != characters.end() ? &it->second : nullptr;
}

std::optional<std::size_t> toolsVerbOf(const Control& control) {
    if (control.actions.size() != 1) return std::nullopt;
    const auto* ui = std::get_if<cmd::Ui>(&control.actions.front());
    if (!ui || ui->verb != "tools") return std::nullopt;
    const auto it = std::ranges::find(kToolsVerbs, std::string_view(ui->arg));
    return it != kToolsVerbs.end() ? std::optional(static_cast<std::size_t>(it - kToolsVerbs.begin())) : std::nullopt;
}

bool Control::onScreens() const {
    return !actions.empty() && std::ranges::all_of(actions, [](const Command& a) {
        const auto* ui = std::get_if<cmd::Ui>(&a);
        return ui && (ui->verb.starts_with("move ") || ui->verb.starts_with("scroll ") || ui->verb == "press");
    });
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
    for (const auto& [name, at] : scenes) {
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

bool spoken(std::string_view line, std::string_view* speaker, std::string_view* words) {
    const std::string_view t = trim(line);
    if (isQuoted(t)) return false;
    const auto colon = t.find(':');
    if (colon == std::string_view::npos || colon == 0 || !(colon + 1 == t.size() || t[colon + 1] == ' ')) return false;
    const std::string_view who = trim(t.substr(0, colon));
    if (std::ranges::count(who, ' ') + 1 > 3 || who.find_first_of(",!?\"{};") != std::string_view::npos) return false;
    if (speaker) *speaker = who;
    if (words) *words = trim(t.substr(colon + 1));
    return true;
}

std::string unknownCommand(std::string_view line) {
    const std::string_view t = trim(line), word = t.substr(0, t.find_first_of(" \t"));
    const bool scriptWord = !word.empty() && word.front() >= 'a' && word.front() <= 'z' &&
                            std::ranges::all_of(word, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
    if (!scriptWord || !storyCommandOf(t).empty() || std::ranges::contains(kDefinitions, word) || spoken(t)) return {};
    return std::string(word);
}

std::string Script::where(const Instruction& ins) const {
    return std::format("{} line {}", ins.file < files.size() ? files[ins.file] : "?", ins.line);
}

// ---------------------------------------------------------------------------
// The built-in screens
// ---------------------------------------------------------------------------
// The player's built-in screens: just what a story needs to play (its lines, with Skip, Auto and quick saves, a Phone
// button when the game has a phone and a Menu button when it has a pause menu, side by side at the right; its choices,
// and a moment's "Saved"), and its keys: the next line, skipping and auto-forward, quick saving and loading, and on
// screens, pointing at what's there (the arrow keys, a controller's d-pad or stick) and pressing it; then the developer
// tools' own (tools WHAT: the keys of their buttons, which do nothing in the game). A game's own screens and controls
// replace these, and add the rest (a title screen, menus...).

std::string_view defaultUi() {
    return R"vnext(
control next_line keys="space mouse_left return kp_enter pad_a" action="next line"
control skip keys="tab pad_rb" action=skip
control auto keys="a pad_lb" action=auto
control quick_save keys="f5" action="quick save; open saved" when=can_save
control quick_load keys="f9" action="quick load" when=has_quick_save
control move_up keys="up pad_up" action="move up" in_menus=yes
control move_down keys="down pad_down" action="move down" in_menus=yes
control move_left keys="left pad_left" action="move left" in_menus=yes
control move_right keys="right pad_right" action="move right" in_menus=yes
control press keys="return space kp_enter pad_a" action=press in_menus=yes
control tools_undo keys="cmd+z ctrl+z" action="tools undo"
control tools_redo keys="cmd+shift+z ctrl+shift+z cmd+y ctrl+y" action="tools redo"
control tools_cut keys="cmd+x ctrl+x" action="tools cut"
control tools_copy keys="cmd+c ctrl+c" action="tools copy"
control tools_paste keys="cmd+v ctrl+v" action="tools paste"
control tools_duplicate keys="cmd+d ctrl+d" action="tools duplicate"
control tools_delete keys="delete backspace" action="tools delete"
control tools_select_all keys="cmd+a ctrl+a" action="tools select all"
control tools_reload keys="cmd+r ctrl+r" action="tools reload"
control tools_quit keys="cmd+q ctrl+q" action="tools quit"
control tools_story keys="cmd+1 ctrl+1" action="tools story"
control tools_screens keys="cmd+2 ctrl+2" action="tools screens"
control tools_library keys="cmd+3 ctrl+3" action="tools library"
control tools_play keys="f10" action="tools play"
control tools_stop_here keys="f9" action="tools stop here"

style choice size=24 align=centre align_vertically=centre background=#2b2563 border=#5b51b8 pointed_background=#3b3392 chosen_background=#4d41c4 unclickable_colour=#9696a5 shadow=no

style textbox_panel border=#5b51b8 opacity_follows=yes shadow=no
style namebox size=28 background=#2b2563 padding=20 colour=speaker opacity_follows=yes shadow=no
style dialogue size=28 text_size_follows=yes shadow=no
style indicator colour=#ada2ff shadow=no
style textbox_button size=20 align=centre align_vertically=centre background=#2b2563 border=#5b51b8 pointed_background=#3b3392 chosen_background=#4d41c4 unclickable_colour=#9696a5 shadow=no
style textbox_unread size=15 colour=#0c0a22 align=centre align_vertically=centre shadow=no

screen textbox story=goes_on
    image #141230fa x=0 y=-0.8642 width=0.9375 height=0.2639 style=textbox_panel
    namebox x=-0.9 y=-0.3947 height=0.0639 bar=3
    dialogue x=0 y=-0.4667 width=0.8875
    indicator x=0.8938 y=-0.8305 width=0.0141 height=0.0167
    container x=0 y=-0.3947 width=0.9 height=0.0639
        container x=1 y=0 height=1 fit=parts stack=across spacing_across=0.0069
            container width=0.1111 height=1 when=phone_button
                button "Phone" x=0 y=0 width=1 height=1 style=textbox_button action="open phone"
                image #ada2ff x=1.358 y=1.4302 width=0.219 height=0.6088 when="phone_unread is more than 0"
                text "{phone_unread}" x=1.358 y=1.4302 width=0.219 height=0.6088 style=textbox_unread when="phone_unread is more than 0"
            button "Menu" width=0.1111 height=1 style=textbox_button action="open pause" when=menu_button
            button "Skip" width=0.1111 height=1 style=textbox_button action=skip chosen=skip
            button "Auto" width=0.1111 height=1 style=textbox_button action=auto chosen=auto
            button "Q.Save" width=0.1111 height=1 style=textbox_button action="quick save; open saved" clickable=can_save
            button "Q.Load" width=0.1111 height=1 style=textbox_button action="quick load" clickable=has_quick_save

screen choice story=goes_on
    choices x=0 y=0.7333 width=0.5625 height=0.5833 item_height=0.075 spacing_across=0.0109 spacing_down=0.0194 position=bottom

screen saved story=goes_on close_after=1.5
    container x=0.95 y=0.92 width=0.14 height=0.0639 style=textbox_button
        text "Saved" x=0 y=0 width=1 height=1 style=textbox_button
)vnext";
}

} // namespace vn
