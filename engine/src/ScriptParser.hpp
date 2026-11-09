#pragma once

// For the parser's files only (Script.cpp, ScriptParser.cpp, ScriptStory.cpp, ScriptLibrary.cpp, ScriptScreens.cpp):
// the parser (Parser) and what it reads a script's lines with, in a namespace of their own (parse).

#include "Script.hpp"
#include "DeveloperLanguage.hpp"
#include "PlayerSettings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vn::parse {

// A time of day as written (18:30, 6:30pm, 6:30 pm, 6pm, or an hour on its own: 8 is 8:00): minutes from midnight.
std::optional<int> clockMinutes(std::string_view text);

// The pieces of `text` between `by`s, each trimmed (empty ones left out).
std::vector<std::string_view> piecesOf(std::string_view text, char by = ' ');

// A weekday's name (0 Monday), any case.
std::optional<int> weekdayNamed(std::string_view word);

struct Line {
    std::string text; // without indentation or note
    int indent = 0;
    std::uint16_t file = 0;
    int number = 0;
    std::vector<Line> children; // the lines indented under it
};

// Each line takes the following lines that are indented further as its children.
std::vector<Line> nest(std::vector<Line>& flat, std::size_t& i, int parentIndent);

// The first word in lower case, without a trailing ':' ("else:" -> else).
std::string headOf(std::string_view text);

std::string_view afterWords(std::string_view text, std::size_t count);

bool isName(std::string_view s);

// Variable names may contain dots: persistent.endings
bool isVariable(std::string_view s);

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
    {"set", true, true},
    {"open app", false, true}, {"open chat", false, true},
    {"open image", false, true}, {"open tip", false, true}, {"open", true, true},
    {"quick save", false, true},
    {"fullscreen", false, true}, {"screenshot", false, true}, {"quit", false, true},
    // Buttons and keys only.
    {"start", false, true}, {"continue", false, true}, {"close", false, true}, {"answer call", false, true}, {"decline call", false, true}, {"hang up", false, true},
    {"answer", false, true}, {"phone call", false, true}, {"keypad", false, true}, {"unlock", false, true}, {"type", false, true},
    {"quick load", false, true}, {"skip", false, true}, {"auto", false, true}, {"tools", false, true},
    {"next line", false, true}, {"rollback", false, true}, {"reload", false, true}, {"yes", false, true}, {"no", false, true},
    {"next page", false, true}, {"previous page", false, true}, {"page", false, true}, {"save", false, true}, {"load", false, true},
    {"delete save", false, true}, {"play song", false, true}, {"replay", false, true}, {"next image", false, true},
    {"previous image", false, true}, {"setting", false, true}, {"reset settings", false, true}, {"reset controls", false, true},
    {"reset progress", false, true}, {"back", false, true}, {"move up", false, true}, {"move down", false, true}, {"move left", false, true},
    {"move right", false, true}, {"press", false, true}, {"choose", false, true}, {"scroll up", false, true}, {"scroll down", false, true},
    {"scroll to the top", false, true}, {"scroll to the end", false, true}, {"put back", false, true},
});

constexpr auto kDefinitions = std::to_array<std::string_view>({"character", "colour", "place", "font", "atlas", "sub-image", "setting", "style",
                                                                "control", "gallery", "ending", "song", "caption", "achievement", "tip", "animation",
                                                                "screen", "variable"});

// The command a line starts with ("quick save"), or empty. Case doesn't matter.
std::string commandOf(std::string_view text, bool story);

bool isBlockWord(std::string_view verb);

// Whether a screen's lines have an app called `name` (in lower case) among their parts, at any depth: a
// container with app=NAME.
bool hasAppPart(const Line& screen, std::string_view name);

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
    KindSpec{"image", ElementKind::Image, "", {"fit", "flip", "repeat", "chosen", "scroll", "scroll_round", "bottom", "loop", "volume"}}, // (an image, a colour, one fading to bottom=, or a movie)
    KindSpec{"text", ElementKind::Text, "text", {"chosen", "width_at_most", "lines"}},
    KindSpec{"button", ElementKind::Button, "button", {"action", "confirm", "keys", "clickable", "chosen", "drag", "drop", "click_animation"}},
    KindSpec{"dialogue", ElementKind::Dialogue, "dialogue", {}},
    KindSpec{"face", ElementKind::Face, "face", {"fit", "flip"}}, // flip=yes mirrors every face (and face_flip=yes ones back)
    KindSpec{"control", ElementKind::Control, "setting", {"slot", "waiting"}},
    KindSpec{"input", ElementKind::Input, "input", {"variable", "most_letters", "placeholder", "action", "confirm", "clickable"}},
    KindSpec{"slider", ElementKind::Slider, "slider", {"variable", "setting", "value", "minimum", "maximum", "step", "vertical", "track", "fill", "knob", "clickable", "action"}}, // (value=: shown, not dragged)
    KindSpec{"flowchart", ElementKind::Flowchart, "flowchart", // (its points as a tree: no columns)
             {"line", "ending_style", "unreached_style", "choice_style", "confirm", "item_width", "item_height", "spacing", "spacing_across", "spacing_down", "scrollbar",
              "scrollbar_thickness", "glide", "glide_speed", "glide_distance"}},
    KindSpec{"container", ElementKind::Container, "",
             {"chosen", "fit", "scroll", "scrollbar", "scrollbar_thickness", "glide", "glide_speed", "glide_distance", "close_at_end", "edges", "fading_edges", "stack", "list",
              "mode", "position", "item_animation", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down", "app", "title", "icon", "count"}},
};

constexpr auto kColourOptions = std::to_array<std::string_view>({"bottom", "track", "fill", "knob", "line"});

constexpr auto kFormulaOptions = std::to_array<std::string_view>({"value", "minimum", "maximum", "most_letters", "step"});

// The player's settings: "setting NAME VALUE" (buttons), slider setting=NAME and "setting default.NAME" lines.
constexpr auto kSettingNames = std::to_array<std::string_view>({
    "fullscreen", "window_size", "scaling", "vsync", "frame_limit", "text_speed", "auto_delay", "time_limits", "textbox_opacity", "text_size",
    "animations", "master_volume", "music_volume", "sound_volume", "voice_volume", "made_up_voices", "typing_sounds", "mouths", "skip_unread",
    "skip_after_choices", "skip_movies", "rollback", "read_aloud", "high_contrast", "captions", "vibration", "language",
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

// A style's align= (left, centre, right) or, `vertical`, its align_vertically= (top, centre, bottom).
std::optional<Align> toAlign(std::string_view s, bool vertical);

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser {
public:
    Parser(Script& script, const FileReader& read) : s_(script), read_(read) {}

    void run(const std::string& entry, std::string_view builtin);

private:
    enum class Phase : std::uint8_t { Definitions, Story };

    void error(const Line& line, std::string message);
    std::nullopt_t fail(const Line& line, std::string message);

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
    std::size_t emit(Command c, const Line& line, bool tail = false); // (tail: see Instruction)
    cmd::Jump& jumpAt(std::size_t pc);

    std::optional<Expr> formula(const Line& line, std::string_view src, std::string missing = {});

    void checkTextOf(const Line& line, std::string_view text);

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
    void read(const Line& line, std::string name);
    void readValues(const Line& line, std::string_view text); // (a text's {values})
    void wrote(std::string name);

    void checkNames();
    std::optional<float> number(const Line& line, std::string_view value, std::string_view what);
    std::optional<bool> boolean(const Line& line, std::string_view value, std::string_view what);
    std::optional<SDL_Color> colour(const Line& line, std::string_view value);

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

    std::optional<MouthMoves> mouthMoves(const Line& line, std::string_view what, std::string_view value);

    // Words after a command that read as a phrase. Took::No: the next word isn't this one;
    // Took::Bad: it is, but what follows is wrong (the error is written).
    enum class Took : std::uint8_t { No, Yes, Bad };

    Took amount(const Line& line, Cursor& c, std::string_view key, float& out);
    std::optional<Cursor> words(const Line& line, std::string_view text);

    void readFile(const std::string& path, int indent, std::vector<Line>& out, const Line* from);
    void readSource(const std::string& name, std::string_view source, int baseIndent, std::vector<Line>& out);

    void block(std::span<const Line> lines);

    std::size_t ifChain(std::span<const Line> lines, std::size_t i);

    void menu(const Line& line);

    void options(const Line& line, std::span<const Line> optionLines, cmd::Menu m);

    cmd::Choice option(const Line& option);

    void together(const Line& line);

    // -- The phone's story lines ----------------------------------------------------------

    // Who a phone line is from (or to): "Mika", "Mika happy", "Mika (happy)" (`expression`) or anyone's
    // name. A defined character's lower-case name goes in `character`, anyone else's in `sender`.
    struct Who {
        std::string character, sender, expression;
        [[nodiscard]] bool empty() const { return character.empty() && sender.empty(); }
    };
    [[nodiscard]] Who whoOf(std::string_view written, bool expression) const;
    [[nodiscard]] std::string shownName(const Who& who) const;
    [[nodiscard]] static std::size_t wordAt(std::string_view text, std::string_view word);
    // While a chat scene's lines are read: its chat.
    struct ChatContext {
        std::string with;      // the person's name as shown
        std::string character; // ...a defined character's
    };
    std::optional<ChatContext> chat_;
    std::string callWith_; // in someone's call= scene: whose (its choices show on the call)

    void chatBlock(const Line& line);

    bool chatStatement(const Line& line);

    enum class Parsed : std::uint8_t { No, Yes, Bad };
    Parsed phoneLine(const Line& line, std::string_view text, cmd::Message& m);

    [[nodiscard]] std::optional<std::pair<const CharacterDef*, bool>> phoneSceneFor(const Line& line) const;

    void phoneScene(const Line& line, const CharacterDef& def, bool chat);

    void callBlock(const Line& line);
    void calling(const Line& line, cmd::Call c, std::span<const Line> lines);

    std::string_view choiceSettings(const Line& line, std::string_view text, cmd::Menu& m);

    void statement(const Line& line);
    void scene(const Line& line);

    void say(const Line& line);

    std::optional<Command> command(const Line& line, const std::string& verb, bool story, std::string_view text = {});

    std::optional<Command> phone(const Line& line, std::string_view);

    std::optional<int> settingNumber(const Line& line, const std::string& setting, std::string_view key, std::string_view value);

    std::optional<Command> show(const Line& line, Cursor& c, bool story);

    const CharacterDef* takeCharacter(Cursor& c);

    bool place(const Line& line, Cursor& c, StageProps& props);

    std::optional<std::string> animationNamed(const Line& line, std::string_view name);
    Took withAnimation(const Line& line, Cursor& c, std::optional<std::string>& out);
    Took andWait(const Line& line, Cursor& c, bool& wait, bool story);

    bool stageProp(const Line& line, StageProps& p, const std::string& key, const std::string& value);

    std::optional<Command> change(const Line& line, std::string_view rest);

    std::optional<Command> play(const Line& line, std::string_view verb, Cursor& c);

    std::optional<Command> weather(const Line& line, Cursor& c);

    std::optional<Command> camera(const Line& line, Cursor& c, bool story);

    std::optional<Command> timeLine(const Line& line, Cursor& c);

    Commands actions(const Line& line, std::string_view text);

    void definitions();

    void startTime();
    void define(const Line& line, std::string_view kind);

    void atlas(const Line& line, Cursor& c);

    void animation(const Line& line, Cursor& c);

    void subImage(const Line& line, Cursor& c);

    static std::pair<std::optional<std::string>, std::optional<std::string>> nameValue(Cursor& c);

    std::optional<float> stageCoordinate(std::string_view text, bool vertical) const;

    void control(const Line& line, Cursor& c);

    void character(const Line& line, Cursor& c);

    // Sets `out` to `value` when it has one; says whether it had.
    template <class T>
    static bool set(T& out, const std::optional<T>& value) {
        if (value) out = *value;
        return value.has_value();
    }

    void record(const Line& line, RecordKind kind, Cursor& c);

    void caption(const Line& line, Cursor& c);

    void gameSettings(const Line& line, const std::string& key, const std::vector<std::string>& v);

    bool styleProp(Style& s, std::string_view key, std::string_view value, const Line& line);
    std::optional<Style> style(const std::string& name, const Line& user, int depth = 0);
    void screens();

    void elements(ScreenDef& def, const std::vector<Line>& lines, int parent);
    std::optional<Element> element(const Line& line);

    void scaleBuiltIn(std::pair<std::string, std::string>& kv) const;
    void bindings();

    void resolve();

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

} // namespace vn::parse
