#pragma once

// The .vnscript language: expressions, the scripts as the player reads them, and translations.

#include "Assets.hpp"
#include "System.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vn {

// ---------------------------------------------------------------------------
// Lines and words: how a .vnscript line is read (the parser and the editor read it alike)
// ---------------------------------------------------------------------------

// Where a line's note starts: '#' as its first character after any spaces, or after a space with a
// space (or the end) after it, outside quotes (so colours like #ff9ecf still work). None: no note.
[[nodiscard]] std::optional<std::size_t> noteStart(std::string_view line);
[[nodiscard]] std::string_view stripNote(std::string_view line); // the line without its note, as it is
// The line without its note (trimmed), and the note ('#' and what follows it).
[[nodiscard]] std::pair<std::string_view, std::string_view> splitNote(std::string_view line);
// "text" (with `q`, 'text' and so on), with no other such quote mark inside.
[[nodiscard]] bool isQuoted(std::string_view s, char q = '"');
// "text" -> text (with `single`, 'text' too); anything else stays as it is. Trimmed.
[[nodiscard]] std::string unquote(std::string_view s, bool single = false);
// The first word: up to the first space or tab (after any before it).
[[nodiscard]] std::string_view firstWord(std::string_view text);
// Where each of a line's words is (start, length): "quoted text" is one word, and so is key="a value".
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> wordSpans(std::string_view line);
[[nodiscard]] std::vector<std::string> rawWords(std::string_view line); // the words as written, quote marks and all
// The words as the parser takes them: without their quote marks. An error when a quote mark is never closed.
struct ScriptWord {
    std::string text;
    bool quoted = false; // (it begins with a quote mark)
};
[[nodiscard]] std::expected<std::vector<ScriptWord>, std::string> splitWords(std::string_view line);

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

// A script value: a whole number (yes/no are 1/0), a number with a fraction (2.5), or a text. A
// sum that comes out whole is a whole number again.
using Value = std::variant<int, double, std::string>;
using Variables = std::map<std::string, Value, std::less<>>;
// Finds a variable's value; names are lower case. Unset variables are 0.
using Lookup = std::function<Value(std::string_view name)>;

[[nodiscard]] bool truthy(const Value& v);        // non-zero / non-empty
[[nodiscard]] int toInt(const Value& v);          // texts are read as numbers, 0 if they aren't; fractions are cut off
[[nodiscard]] double toNumber(const Value& v);    // (with its fraction)
[[nodiscard]] Value number(double n);             // whole when it is
[[nodiscard]] std::string toString(const Value& v); // fractions to at most 4 places: 2.5, 0.3333
// "add" (op '+') and "subtract" (op '-'): numbers add up, texts are joined.
[[nodiscard]] Value combine(const Value& old, char op, const Value& v);
// One value per line in save files: "i 42", "d 2.5" or "s text" (spaces, newlines and backslashes escaped).
[[nodiscard]] std::string encodeValue(const Value& v);
[[nodiscard]] Value decodeValue(std::string_view s);

// random()'s numbers come in a sequence (splitmix64: its whole state is one number). A game's commands
// draw from the game's own state, put in effect while they run (a RandomScope); saves, and so rollback,
// keep it, so going back and forward again rolls the same numbers. Anything else (a screen's text
// drawn) draws from a state of its own, and doesn't move the game's on.
class RandomScope {
public:
    explicit RandomScope(std::uint64_t& state);
    ~RandomScope();
    RandomScope(const RandomScope&) = delete;
    RandomScope& operator=(const RandomScope&) = delete;
    RandomScope(RandomScope&&) = delete;
    RandomScope& operator=(RandomScope&&) = delete;

private:
    std::uint64_t*& source_; // (where random() draws from)
    std::uint64_t* was_;
};
[[nodiscard]] std::uint64_t newRandomState(); // (a game's, to begin with: from the system)
// What random(lo, hi) gives: a whole number from lo to hi (the next in that sequence).
[[nodiscard]] int randomNumber(int lo, int hi);

// A formula, as used by set/add, if, choice conditions, {...} in texts and screen conditions.
// Numbers (2, 2.5), "text" or 'text', true/false, variables, + - * / %, parentheses, comparisons
// (= == != < <= > >= or the words is, is not, at least, at most, more than, less than),
// and / or / not, and the functions random(a, b), random(n), min, max, abs, round, len, str, int,
// upper, lower. Names are case-insensitive. Dividing gives a fraction when it doesn't come out whole.
class Expr {
public:
    static std::expected<Expr, std::string> parse(std::string_view source);

    [[nodiscard]] Value eval(const Lookup& lookup) const;
    [[nodiscard]] bool test(const Lookup& lookup) const { return truthy(eval(lookup)); }
    [[nodiscard]] std::optional<Value> constant() const; // its value, when it's just one (3, yes, "Mia")

private:
    friend class ExprParser;

    enum class Kind : std::uint8_t { Literal, Variable, Not, Negate, Binary, Call };
    enum class BinOp : std::uint8_t { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, And, Or };

    struct Node {
        Kind kind = Kind::Literal;
        BinOp op = BinOp::Add;
        Value value = 0;
        std::string name;
        int lhs = -1;
        int rhs = -1;
        std::vector<int> args;
    };

    [[nodiscard]] Value evalNode(int index, const Lookup& lookup) const;

    std::vector<Node> nodes_;
    int root_ = -1;
};

// Text tags handled by the textbox: {pause}, {pause 0.5}, {nowait}, {speed 10}...{/speed},
// {color gold}...{/color}, {reveal line}. `at` is the tag's position in the expanded text, in codepoints.
using TagHandler = std::function<void(std::string_view name, std::string_view arg, std::size_t at)>;
[[nodiscard]] bool isTextTag(std::string_view inner);

// Fills in the {expression} parts of a text; {{ and }} are literal braces. Text tags are passed
// to `tag` (or dropped without one). An expression that doesn't make sense leaves its {...} as is.
[[nodiscard]] std::string expand(std::string_view text, const Lookup& lookup, const TagHandler& tag = {});
// Fills in {expressions} now, but leaves text tags (and {{ }}) for a later expand(); braces in the
// values are doubled, so they stay text. (Messages and choices, shown later as rich text.)
[[nodiscard]] std::string fillValues(std::string_view text, const Lookup& lookup);
// Checks the {...} parts of a text; returns the first problem.
[[nodiscard]] std::string checkText(std::string_view text);

// ---------------------------------------------------------------------------
// Scripts
// ---------------------------------------------------------------------------
// Everything a game's .vnscript files define: the story (as a list of commands), the characters,
// and the presentation (config, colours, fonts, styles, screens, controls).

struct Translation;

// ---------------------------------------------------------------------------
// Story commands
// ---------------------------------------------------------------------------

enum class TransitionKind : std::uint8_t { None, Fade, Dissolve, Wipe, Slide };

struct Transition {
    TransitionKind kind = TransitionKind::None;
    float duration = 0.0f;
    SDL_Color color{0, 0, 0, 255}; // colour a fade passes through
    std::string shape;             // a wipe shaped by this picture: its dark parts change first
    std::vector<Transition> then;  // open SCREEN with fade ... then fade ...: the fades after it
};

// Optional changes to a picture on stage (characters and images).
struct StageProps {
    std::optional<float> x, y;             // 0..1 across / down the canvas (the picture lines up with that much of it)
    std::optional<float> zoom, opacity, rotate;
    std::optional<bool> flip;
    std::optional<int> layer;              // higher layers draw on top
    std::optional<SDL_Color> tint;         // multiplies its colours (black: a silhouette)
};

enum class Ease : std::uint8_t { Linear, In, Out, InOut };
[[nodiscard]] float eased(Ease e, float t); // t (0..1) with that speeding up and slowing down

// Things the player collects, each listed once in the scripts ("gallery", "ending", "song",
// "achievement" and "tip" lines) and remembered for good once reached: persistent.<kind>.<name>.
enum class RecordKind : std::uint8_t { Gallery, Ending, Song, Achievement, Tip };
inline constexpr std::array<std::string_view, 5> kRecordKinds{"gallery", "ending", "song", "achievement", "tip"};

namespace cmd {
struct Say {
    std::string character;  // a defined character (lower-case name), or empty
    std::string speaker;    // a one-off speaker name, or empty for narration
    std::string expression; // optional new expression for the character
    std::string text;       // may contain {expressions} and text tags
    std::string voiceId;    // "<label>_<n>": the character's n-th line in the label (for voices=)
    std::uint64_t id = 0;   // the line as written, who says it included (the same in every language)
    bool together = false;  // in a "together" block, before its last line: it shows with the next one, without waiting
};
struct Scene { std::string image; Transition transition; }; // image name or "#rrggbbaa"
struct Show {
    std::string character, image; // one of them
    std::string tag;               // name used by move / hide
    std::string expression;
    StageProps props;
    Transition transition;
};
struct Hide { std::string tag; Transition transition; };
struct Move {
    std::string tag;
    StageProps props;
    float seconds = 0.5f;
    Ease ease = Ease::InOut;
    bool wait = false; // the story waits until the movement ends
};
struct Shake { // shake or flash
    bool flash = false;
    float strength = 12.0f;
    SDL_Color color{255, 255, 255, 255};
    float seconds = 0.5f;
};
struct Choice {
    std::string text;
    std::string label; // "-> label": where it goes, after its own lines if it has any
    std::size_t target = 0;
    std::optional<Expr> condition;
    bool shownLocked = false; // (needs ...): while the condition is false it shows, but can't be picked
    std::string note;         // what it needs, shown with it then
    std::uint64_t id = 0;     // the option as written (the same in every language): "picked before"
};
struct Menu {
    std::string prompt;
    std::vector<Choice> choices;
    float seconds = 0.0f;  // for N seconds: then none is picked (0: no time limit)
    int lines = 0;         // for N lines: the story goes on meanwhile, the options up for its next N lines
    bool returns = true;   // (for N lines) after a picked option's lines, back to the line after the one showing; no: past the N lines
    bool optional = false; // optional=yes: clicking on picks none (the line before it stays on screen)
    std::string screen;    // screen=NAME: the screen that shows it (else "choice")
    std::size_t after = 0; // where the story goes on when none is picked
};
struct Jump {
    std::string label;
    std::size_t target = 0;
    bool call = false;            // come back here on "return"
    std::optional<Expr> unless;   // only jump when this is false (if / else)
};
struct Return {};
struct End {};
struct Set {
    std::string variable;
    char op = '=';                // '=', '+' (add), '-' (subtract), '!' (toggle)
    std::optional<Expr> value;
};
struct Play {
    enum class Track : std::uint8_t { Music, Sound, Voice };
    Track track = Track::Sound;
    std::string file;    // empty: stop
    std::string channel; // sounds: a named channel that can loop and be stopped
    bool loop = false;
    float volume = 1.0f;
    float fade = 0.0f;
    std::optional<bool> sync; // voice: the next line types out with it (else config voice_sync)
};
struct Wait { float seconds = -1.0f; }; // < 0: until a click
struct Screen { std::string name; bool show = true; }; // "show screen" / "hide screen": up while playing
struct Ask { std::string variable, prompt, fallback; int max = 24; };
// Rain, snow, petals, leaves or embers over the stage. kind empty: stop.
struct Weather {
    std::string kind;
    float amount = 1.0f, speed = 1.0f, wind = 0.0f;
    std::optional<SDL_Color> color;
    std::string image; // a picture for each flake / drop instead of the drawn one
    bool behind = false; // behind the characters (else in front of them)
    float fade = 1.0f;
};
// The view of the stage: the point in the middle, how close, how turned. reset: back to normal.
struct Camera {
    std::optional<float> x, y, zoom, rotate;
    bool reset = false;
    float seconds = 0.0f;
    Ease ease = Ease::InOut;
    bool wait = false;
};
// A video (movies/NAME.mpg) over everything; the story goes on when it ends, or when the player
// clicks past it.
struct Movie {
    std::string file;
    bool loop = false;      // plays again and again, until clicked past
    bool skippable = true;  // clicks, keys and skipping go past it
    float volume = 1.0f;    // its sound, on top of the music volume
};
// "textbox NAME": the screen every line uses from now on (else "say"); "new page" empties the page;
// "hide textbox [NAME]": a textbox's kept line goes (all of them without NAME).
struct Textbox { std::string screen; bool newPage = false, hide = false; };
// Menu commands: open (from the story it waits for the screen to close), close, answer, quick
// save, notify...
struct Ui {
    std::string verb, arg;
    Transition transition{}; // open SCREEN with fade [SECONDS] [to COLOUR] [then fade ...]
    Transition closing{};    // ...and close with fade ...: how it goes when it closes
};
// "unlock PICTURE", "ending NAME", "achievement NAME", "tip NAME": the player has it now (a picture,
// an ending reached, an achievement earned, a tip learned). Pictures and songs are also had by
// showing / playing them, tips by a line that shows {tip NAME}.
struct Record { RecordKind kind; std::string name; };
// "chapter TITLE": the story's part from here on (saves show it; a "chapter" screen shows it big).
struct Chapter { std::string title; };
// "effect sepia noise": how the whole stage looks, until "effect none" (StageLook bits). With
// "for N seconds" they're added for that long, on top of what's there.
struct Effect {
    unsigned looks = 0;
    float amount = 1.0f; // how strong
    float fade = 0.0f;   // coming in (or going) over this many seconds
    float seconds = 0.0f;
};
// "chart TITLE": a place on the flowchart (chapters, choices and endings are on it by themselves).
struct Mark { std::string title; };
// "message NAME: text": a message on the phone (the phone part), with the answers the player
// can send back indented under it (options like a choice's). "message to NAME: text" is the
// player's own, to them. Each person's messages are a conversation of their own (the contacts part).
struct Message {
    std::string character; // who it's from (or to, if it's the player's): a defined character (lower-case name)...
    std::string sender;    // ...or anyone else's name
    bool mine = false;     // sent by the player
    std::string text;
    std::vector<Choice> replies;
};
// "phone off" takes the phone away for a while (screens showing it close, no notices); "phone on" gives it back.
struct Phone { bool on = true; };
} // namespace cmd

using Command = std::variant<cmd::Say, cmd::Scene, cmd::Show, cmd::Hide, cmd::Move, cmd::Shake, cmd::Menu, cmd::Jump,
                             cmd::Return, cmd::End, cmd::Set, cmd::Play, cmd::Wait, cmd::Screen, cmd::Ask, cmd::Ui,
                             cmd::Weather, cmd::Camera, cmd::Textbox, cmd::Movie, cmd::Record, cmd::Chapter, cmd::Mark,
                             cmd::Message, cmd::Effect, cmd::Phone>;
using Commands = std::vector<Command>;

// When a character's mouth moves (their pictures need mouth frames: see Atlas).
enum class Talk : std::uint8_t {
    Yes,   // with their voice line, or while their text types if the line has none
    Voice, // only with a voice line
    No,
};

// What a face in the textbox copies from the character's picture on stage (face_sync=). What it
// doesn't copy, it does on its own: its own expression, blinks and mouth movements.
struct FaceSync {
    bool expression = true; // Aiko happy: ... changes both (else only the face; show Aiko happy only the stage)
    bool blink = true;      // blinks at the same moments
    bool talk = true;       // the mouth opens and closes with the one on stage
};

struct CharacterDef {
    std::string key;   // lower-case name
    std::string name;  // as shown
    std::string image; // images/<image>.png, images/<image>_<expression>.png
    [[nodiscard]] std::string pictureOf(std::string_view expression) const { return expression.empty() ? image : std::format("{}_{}", image, expression); }
    [[nodiscard]] std::string faceOf(std::string_view expression) const { return expression.empty() ? face : std::format("{}_{}", face, expression); }
    SDL_Color color{255, 255, 255, 255}; // name colour
    std::optional<SDL_Color> textColor;  // dialogue colour (else the dialogue style's)
    std::optional<Reveal> reveal;        // how their lines appear (else config reveal)
    std::string textbox;                 // screen used as their textbox (else "say")
    std::string blip;                    // sound played while their text types out (lines without a voice)
    std::string voices;                  // their lines' voice files: audio/<voices>/<label>_<n> (empty: none)
    float babble = 0.0f;                 // made-up voice for every line, this high (Hz); 0: none
    float babbleSpeed = 1.0f;            // how fast it talks (1 = normal)

    // On stage (atlases with eyes and mouth frames).
    bool blink = true;
    float blinkEvery = 4.0f;  // seconds between blinks, on average
    Talk talk = Talk::Yes;
    float talkSpeed = 4.0f;   // mouth movements a second while text types without a voice

    // The face in the textbox: images/<face>.png, <face>_<expression>.png, shown by a "face" part.
    std::string face;
    std::string faceSide = "left"; // which end of the textbox ({face_side} in screens)
    FaceSync faceSync;             // what it copies from the picture on stage
    bool faceFlip = false;         // mirrored
    bool faceBlink = true;         // (default: like on stage)
    Talk faceTalk = Talk::Yes;     // (default: like on stage)
};

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

enum class Align : std::uint8_t { Start, Center, End };

struct Style {
    struct State {
        std::optional<SDL_Color> color, bg, border;
        std::optional<SDL_Color> bgBottom; // (bg= fading down to it: navy-black)
        std::string image;
    };

    std::string font = "default";
    float size = 24.0f;
    SDL_Color color{240, 240, 245, 255};
    SDL_Color bg{0, 0, 0, 0};
    std::optional<SDL_Color> bgBottom; // (bg= fading down to it: navy-black)
    SDL_Color border{0, 0, 0, 0};
    std::string image;          // drawn stretched instead of bg/border
    State hover, selected, disabled;
    Align align = Align::Start; // text alignment inside the element's box
    Align valign = Align::Start;
    float padding = 0.0f;
    float opacity = 1.0f;
    TextShadow shadow;
    float lineSpacing = 1.0f;   // 1 = the font's own
    bool textboxOpacity = false; // also fades with the player's "textbox opacity" setting
    bool textSize = false;       // its size follows the player's "text size" setting
    bool speakerColor = false;  // text uses the speaking character's colour
    std::string hoverSound, clickSound;
};

// "animation NAME [loop]", with its steps on the lines under it: how a screen's part with
// animation=NAME moves, fades, grows, turns and changes colour, from how it's written. Each step goes
// from where the last one left it to its own values over its seconds ("wait 1 second" stays put); a
// loop starts again from the start, else it stays as its last step left it.
struct Animation {
    struct Step {
        // x, y: places on the screen, -1 to 1 like the stage's (NaN: home, its own place); opacity 0..1; zoom
        // 1 = its size; rotate in degrees.
        std::optional<float> x, y, opacity, zoom, rotate;
        std::optional<SDL_Color> color;                   // a picture's tint, words' colour, a rect's fill
        std::optional<SDL_Color> fade;                    // the screen covered with this colour by the end (fade=none: uncovered again)
        float seconds = 0.0f;
        Ease ease = Ease::InOut;                          // (a fade's: linear, as a screen's fade is, unless said)
    };
    struct Look {
        float x = 0.0f, y = 0.0f, opacity = 1.0f, zoom = 1.0f, rotate = 0.0f;
        std::optional<SDL_Color> color; // (none: its own)
        float fade = 0.0f;              // how far the screen is covered (0 to 1), and with what: as a screen's fade transition covers it
        SDL_Color fadeColor{0, 0, 0, 255};
    };
    std::string name;
    std::vector<Step> steps;
    bool loop = false;
    // How it looks `seconds` after it started; `own`: the part's own colour (where a colour starts from);
    // `offset`: how far (in pixels) a place across (or down) is from its own place. Look's x and y are
    // pixels then.
    using Offset = std::function<float(bool across, float place)>;
    [[nodiscard]] Look at(float seconds, SDL_Color own, const Offset& offset) const;
    [[nodiscard]] float length() const; // one time through, in seconds
};

enum class ElementKind : std::uint8_t {
    Image, Text, Button, Key,                                                         // general
    Namebox, Dialogue, Indicator, Choices, History, Pages, Slots, Message, Face, Control, Gallery, Page, // engine widgets
    Input, Bar, Slider,                                                             // variable widgets
    Movie,                                                                          // a video playing (loops)
    Endings, Songs, Achievements, Tips,                                             // lists of what was reached
    Chart,                                                                          // the flowchart
    Phone, Contacts,                                                                // the phone's messages, and who sent them
    Container,                                                                      // parts in it (any part can hold parts)
};

// A key (with modifiers), mouse button, wheel direction, controller button or touch gesture:
// "escape", "alt+return", "mouse_right", "wheel_up", "pad_a", "swipe_left", "long_press".
struct KeyChord {
    enum class Device : std::uint8_t { Keyboard, Mouse, Wheel, Pad, Touch };
    enum class Gesture : std::uint8_t { SwipeLeft, SwipeRight, SwipeUp, SwipeDown, LongPress, TwoFingerTap };
    // Controller buttons past SDL's own: the triggers (pressed past halfway).
    static constexpr int kLeftTrigger = SDL_GAMEPAD_BUTTON_COUNT, kRightTrigger = SDL_GAMEPAD_BUTTON_COUNT + 1;
    Device device = Device::Keyboard;
    SDL_Keycode key = 0;
    SDL_Keymod mods = 0;
    Uint8 button = 0;
    int wheel = 0; // +1 up, -1 down
    int pad = -1;  // SDL_GamepadButton, or a trigger
    Gesture gesture = Gesture::SwipeLeft;
    [[nodiscard]] bool matches(const SDL_Event& e) const;
    [[nodiscard]] std::string name() const; // as written in scripts: "ctrl+s", "mouse_left"
    bool operator==(const KeyChord&) const = default;
};
std::optional<KeyChord> parseKeyChord(std::string_view text);
// The event type touch gestures arrive as (user.code: the KeyChord::Gesture).
Uint32 gestureEventType();

struct Element {
    ElementKind kind = ElementKind::Image;
    std::uint16_t file = 0; // where it's written (Script::files), for the editor
    int line = 0;
    int parent = -1;        // the part it's in (its lines are indented under that one's): its index, or -1
    std::string text;  // a text's or button's words, an image's or a video's name, a key's or a control's name...
    // Where it is and how big, as written: x and y places from -1 to 1 in the box it's in (the screen, or
    // the part it's in: -1 lines its left edge or bottom up with that one's; none: the top left corner),
    // w and h shares of that box (none: automatic; a container fills it). A part without a width (or a
    // height) is placed by its corner.
    std::optional<float> placeX, placeY, shareW, shareH;
    Style style;
    Commands actions;
    Commands drop;       // drop=: what dropping something dragged on it does ({dropped} is what it was)
    std::string confirm; // ask this question before running the actions
    std::optional<KeyChord> key;
    std::optional<Expr> visible, enabled, selected;
    std::map<std::string, Style, std::less<>> substyles;    // options ending in "_style"
    std::map<std::string, SDL_Color, std::less<>> colors;   // colour options (top, bottom, track...)
    std::map<std::string, std::string, std::less<>> props;  // other widget options
    std::map<std::string, Expr, std::less<>> exprs;         // formula options (bar value=, max=...)
    std::shared_ptr<const Animation> animation;             // animation=
    std::shared_ptr<const Animation> clickAnimation;        // click_animation=: a button's, played when it's clicked (then it does what it does)

    [[nodiscard]] float number(std::string_view name, float fallback) const;
    [[nodiscard]] std::string option(std::string_view name, std::string_view fallback = {}) const;
    [[nodiscard]] SDL_Color color(std::string_view name, SDL_Color fallback) const;
    [[nodiscard]] const Style& substyle(std::string_view name) const;
    // A container's: scroll=yes (what's in it scrolls), fit=parts (as big as its biggest part, not its box)
    // and edges= (what's past its edges: show, cut or fade; scrolling: cut, unless it says). Any part that
    // scrolls: whether a drag glides on (glide=; none: `usually`, the game's setting), and which way dragging
    // scrolls it (-1: a container or a grid, further down as it grows; +1: a history or a phone, further
    // up from the latest; 0: it isn't dragged).
    [[nodiscard]] bool scrolls() const;
    [[nodiscard]] bool fitsParts() const;
    [[nodiscard]] std::string edges() const;
    [[nodiscard]] bool glides(bool usually) const;
    [[nodiscard]] float dragDirection() const;
};

struct ScreenDef {
    std::string name;
    std::vector<Element> elements;
    bool modal = true;    // blocks input to everything underneath
    bool keyboard = true; // arrow keys / Enter move between and press its controls
    float closeAfter = 0.0f; // close_after=: it closes by itself after this many seconds (0: it doesn't)
    bool keep = false;       // keep=yes, a textbox: its last line stays while other textboxes show theirs
    float textSpeed = 1.0f;  // text_speed=, a textbox: how fast its lines type, as a part of the player's speed
    // stop_music_on_open= and stop_voice_on_open= (or _on_close=): the music, or the voice line, stops then.
    struct Stops {
        bool music = false, voice = false;
    };
    Stops stopOnOpen, stopOnClose;
    Commands onShow, onHide;
    // show_animation= / hide_animation=: the whole screen moves and fades as these say when it opens
    // and when it closes (it stays on screen, without taking clicks, until that's over).
    std::shared_ptr<const Animation> showAnimation, hideAnimation;
};

// Something a key does ("control" lines). Players can change the keys on the Controls screen.
struct Control {
    std::string name;
    std::vector<KeyChord> keys; // the defaults
    Commands actions;
    bool everywhere = false;    // also on the title screen and in menus (else only while playing)
    std::optional<Expr> condition; // if=: works only while it's true
};

// The messages the player shows, in English: the ones "config text.ID" lines don't change.
// What a Controls screen calls the mouse buttons, the wheel, controller buttons and touch gestures
// (keys are named by the system): the only words the player shows that aren't the game's own, so a
// translation file lists them.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 29> kKeyWords{{
    {"mouse_left", "Left click"}, {"mouse_middle", "Middle click"}, {"mouse_right", "Right click"}, {"wheel_up", "Wheel up"}, {"wheel_down", "Wheel down"},
    {"pad_a", "A"}, {"pad_b", "B"}, {"pad_x", "X"}, {"pad_y", "Y"}, {"pad_lb", "LB"}, {"pad_rb", "RB"}, {"pad_lt", "LT"}, {"pad_rt", "RT"},
    {"pad_ls", "Left stick"}, {"pad_rs", "Right stick"}, {"pad_start", "Start"}, {"pad_back", "Back"}, {"pad_guide", "Guide"},
    {"pad_up", "D-pad up"}, {"pad_down", "D-pad down"}, {"pad_left", "D-pad left"}, {"pad_right", "D-pad right"},
    {"swipe_left", "Swipe left"}, {"swipe_right", "Swipe right"}, {"swipe_up", "Swipe up"}, {"swipe_down", "Swipe down"},
    {"long_press", "Long press"}, {"two_finger_tap", "Two-finger tap"}, {"unknown_key", "?"}}};
[[nodiscard]] std::string_view keyWord(std::string_view id); // (the id itself when it isn't listed)

struct GameConfig {
    std::string title = "VNext";
    std::string version = "1.0";
    std::string language = "English"; // the scripts' own language, as players see it (Settings > Language)
    std::string saveId = "VNext";
    std::string icon;
    std::string cursor;               // a picture for the mouse pointer (else the system's)
    SDL_Point cursorHotspot{0, 0};    // the point of it that clicks
    SDL_Color letterboxColor{0, 0, 0, 255}; // around the picture when the window isn't the same shape
    bool resizable = true;            // the window's edges can be dragged to resize it (the game's, and the developer tools': one setting)
    int width = 1280;
    int height = 720;
    int savePages = 9;
    int saveSlots = 6;
    bool autosave = true;
    int historyLimit = 25;
    int rollback = 100;       // lines and choices the player can go back (0: none)
    float skipDelay = 0.05f;
    float autoPerChar = 0.01f;
    SDL_Keymod skipHold = SDL_KMOD_CTRL;
    float fadeTime = 1.0f;
    float dissolveTime = 0.5f;
    Transition showTransition{.kind = TransitionKind::Dissolve, .duration = 0.25f};
    Transition hideTransition{.kind = TransitionKind::Dissolve, .duration = 0.25f};
    float spriteHeight = 0.95f;
    float moveTime = 0.4f;   // "show X at Y" on someone already there slides this long
    float shakeStrength = 12.0f, shakeTime = 0.5f, flashTime = 0.4f;
    SDL_Color flashColor{255, 255, 255, 255};
    bool pixelArt = false;   // pictures scale with sharp pixels (else smoothly)
    int preload = 30;        // story commands to look ahead for pictures to load early (0: none)
    int imageMemory = 512;   // MB of pictures kept loaded before old ones are let go (0: no limit)
    Reveal reveal = Reveal::Letter;
    SDL_Color narratorColor{150, 170, 255, 255};
    float notifyTime = 2.5f;  // seconds a notify message stays
    float musicCrossfade = 0.5f;
    bool glide = true;        // lists and containers that scroll glide on after a drag (unless a part says), ...
    float glideSpeed = 1.0f, glideDistance = 1.0f; // ...this fast (of the speed they were let go at) and this far
    float scrollbarSize = 6.0f; // pixels thick a container's scrollbar is (a part can say otherwise)
    float fadeSize = 0.0f;      // pixels a fading edge (edges=fade) spans (0: a fifth of the box, at most 64)
    bool voiceSync = false;   // the text of a voiced line types out in time with the voice
    float musicWhileSpeaking = 0.7f; // how loud music plays while a voice does (1: unchanged)
    float blipEvery = 0.06f, blipVolume = 0.6f;
    float phoneTyping = 6.0f; // letters a second people type an answer on the phone (0: it arrives at once)
    float babbleSpeed = 1.0f, blinkEvery = 4.0f, talkSpeed = 4.0f; // defaults for characters
    bool blink = true;        // (blink no: nothing blinks, unless a character says blink=yes)
    Talk talk = Talk::Yes;    // (talk no: no mouth moves, unless a character says talk=)
    bool developer = false;   // developer mode: developer tools (F10), F6 reload, labelled stand-ins for missing files
    std::map<std::string, int, std::less<>> settingDefaults; // "config default.<setting>"
};

// ---------------------------------------------------------------------------
// Script
// ---------------------------------------------------------------------------

struct Instruction {
    Command command;
    std::uint16_t file = 0;
    int line = 0;
};

struct ScriptError {
    std::string file;
    int line = 0;
    std::string message;
};

// Reads a file relative to the game folder; nullopt if it does not exist.
using FileReader = std::function<std::optional<std::string>(const std::string& relativePath)>;

class Script {
public:
    // Reads `builtin` (the engine's default UI), then `entry` and everything it includes; later
    // definitions replace earlier ones. Always returns a Script; check ok() / errors.
    static Script load(const std::string& entry, const FileReader& read, std::string_view builtin = {});

    [[nodiscard]] bool ok() const { return errors.empty(); }
    [[nodiscard]] std::optional<std::size_t> findLabel(std::string_view name) const;
    [[nodiscard]] const CharacterDef* character(std::string_view key) const;
    [[nodiscard]] const ScreenDef* screen(std::string_view name) const;
    // The label containing `pc` and the offset from it (lets saves survive script edits).
    [[nodiscard]] std::pair<std::string, std::size_t> locate(std::size_t pc) const;
    [[nodiscard]] std::string where(const Instruction& ins) const;

    std::vector<Instruction> code;
    std::unordered_map<std::string, std::size_t, StringHash, std::equal_to<>> labels;
    std::unordered_map<std::string, CharacterDef, StringHash, std::equal_to<>> characters;
    std::vector<Atlas> atlases;
    std::vector<SubPicture> subPictures;
    std::unordered_map<std::string, Animation, StringHash, std::equal_to<>> animations;
    std::vector<std::string> files;
    std::vector<ScriptError> errors;
    std::uint64_t hash = fnv1a("");

    GameConfig config;
    Palette palette;
    struct FontFile {
        std::string file;
        std::string style; // a named style inside the file, e.g. "Bold" (empty: Regular)
    };
    std::map<std::string, FontFile, std::less<>> fonts;
    std::unordered_map<std::string, ScreenDef, StringHash, std::equal_to<>> screens;
    std::vector<Control> controls;
    [[nodiscard]] const Control* control(std::string_view name) const;
    // "variable NAME VALUE ["what it's for"]": what a variable is before the story sets it (else 0).
    Variables startValues;

    // The gallery's pictures, the endings, the music room's songs, the achievements and the tips,
    // in the order they're listed: "gallery PICTURE "Title"", "ending NAME "Title" hint=...", "song
    // NAME "Title"", "achievement NAME "Title" "Description" [hidden]", "tip NAME "Title" "Description"".
    struct Record {
        std::string name;    // as written (a picture's or a song's file name)
        std::string title;
        std::string text;    // an ending's hint (shown while it's locked), an achievement's description, a tip's text
        bool hidden = false; // an achievement that shows as ??? until it's earned
    };
    std::array<std::vector<Record>, kRecordKinds.size()> lists;
    [[nodiscard]] const std::vector<Record>& records(RecordKind kind) const { return lists[static_cast<std::size_t>(kind)]; }
    [[nodiscard]] const Record* record(RecordKind kind, std::string_view name) const;
    // The persistent variable that remembers one, and whether a variable is one of those (they stay
    // when the player goes back with rollback).
    [[nodiscard]] static std::string recordVariable(RecordKind kind, std::string_view name) {
        return std::format("persistent.{}.{}", kRecordKinds[static_cast<std::size_t>(kind)], lower(name));
    }
    [[nodiscard]] static bool isRecordVariable(std::string_view name);

    // Translations. Where a text players read is (see forEachText):
    struct TextPlace {
        const Instruction* line = nullptr; // a story line, choice, question or message
        std::string_view what;             // else "title", "character", "screen", "control", "text" or a record kind ("gallery"...)
        std::string_view name;             // ...and which one
    };
    // Every text players read: lines, choices, names, screen texts, messages, gallery titles...
    void forEachText(const std::function<void(std::string& text, const TextPlace& place)>& f);
    // Shows the texts in `translation`'s language (nullptr: as written).
    void translate(const Translation* translation);
    [[nodiscard]] const Translation* translation() const { return translation_; }
    [[nodiscard]] std::string translated(std::string_view text) const; // as the translation on has it (else as it is)

private:
    std::vector<std::string> originals_; // every text as written (forEachText order), once translated
    const Translation* translation_ = nullptr;
};

// The story command a line starts with ("scene", "quick save"...), or empty (a line of text).
[[nodiscard]] std::string storyCommandOf(std::string_view line);

// The player's built-in screens, as script (at the end of Script.cpp): every game starts with them.
[[nodiscard]] std::string_view defaultUi();

// ---------------------------------------------------------------------------
// Translations
// ---------------------------------------------------------------------------

// The game's texts in another language: game/translations/<id>.vnscript. Each text is written as
// the scripts have it, with its translation indented under it:
//
//     language "Español" code=es
//     font default "fonts/MyFont.ttf"
//
//     "Hello!"
//         "¡Hola!"
//
// Players choose it in Settings (the "language" setting). Texts it doesn't have show as written.
struct Translation {
    std::string id;   // the file's name ("spanish"); also images/<id>/ and audio/<id>/
    std::string name; // as players see it ("Español")
    std::string code; // the language's code ("es", "pt-BR"): players whose computer uses it start with it
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> texts; // as written -> translated
    std::map<std::string, Script::FontFile, std::less<>> fonts; // used instead of the game's own

    // The translation of `original`, else `original`.
    [[nodiscard]] std::string text(const std::string& original) const;
    // Reads a translation file; problems go in `errors` (as translations/<id>.vnscript).
    static Translation parse(std::string id, std::string_view source, std::vector<ScriptError>& errors);
};

// --translate: a translations/<id>.vnscript file with every text `script` shows, ready to be
// translated. With `existing` (that file as it is now), the texts it doesn't have yet are added at
// its end, and the ones the game doesn't show any more (changed or taken out since) are taken out.
struct TranslationFile {
    std::string text;
    std::size_t added = 0;             // texts added
    std::vector<std::string> removed;  // texts `existing` had that the game doesn't show any more
};
TranslationFile translationFile(Script& script, std::string_view id, const std::optional<std::string>& existing);
// A translation file as it is, less the texts `script` doesn't show any more: each one's line, the
// translation under it and its note (a heading left with nothing under it too). `removed` gets them.
std::string prunedTranslation(Script& script, std::string_view id, std::string_view file, std::vector<std::string>& removed);

} // namespace vn
