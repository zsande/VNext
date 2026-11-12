#pragma once

// The .vnscript language: how its lines are written, and the scripts as the player reads them (Script): characters,
// styles, animations, screens and their parts, controls, the game's settings and the story's code.

#include "Commands.hpp"
#include "Assets.hpp"
#include "DeveloperLanguage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vn {

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

// Everything a game's .vnscript files define: the story (as a list of commands), the characters,
// and the presentation (settings, colours, fonts, styles, screens, controls).

struct Translation;

// When a character's mouth moves (their images need mouth frames: see Atlas).
enum class MouthMoves : std::uint8_t {
    Yes,   // with the line's voice, or while their text types if it has none
    Voice, // only while the line plays a voice
    No,
};

// What a face in the textbox copies from the character's image on stage (face_copies=). What it
// doesn't copy, it does on its own: its own expression, blinks and mouth movements.
struct FaceCopies {
    bool expression = true; // Aiko happy: ... changes both (else only the face; show Aiko happy only the stage)
    bool blink = true;      // blinks at the same moments
    bool mouth = true;      // the mouth opens and closes with the one on stage
};

struct CharacterDef {
    std::string key;   // lower-case name
    std::string name;  // as shown
    std::string image; // images/<image>.png, images/<image>_<expression>.png
    [[nodiscard]] std::string imageOf(std::string_view expression) const { return expression.empty() ? image : std::format("{}_{}", image, expression); }
    [[nodiscard]] std::string faceOf(std::string_view expression) const { return expression.empty() ? face : std::format("{}_{}", face, expression); }
    SDL_Color nameColour{255, 255, 255, 255};
    std::optional<SDL_Color> textColour;  // dialogue colour (else the dialogue style's)
    std::optional<TextAppears> textAppears;   // how their lines appear (else setting text_appears)
    std::string textbox;                 // screen used as their textbox (else "textbox")
    std::string typingSound;             // sound played while their text types out (lines without a voice)
    std::string voiceFiles;              // their lines' voice files: sounds/<voiceFiles>/<scene>_<n> (empty: none)
    float madeUpVoice = 0.0f;            // made-up voice for every line, this high (Hz); 0: none
    float madeUpVoiceSpeed = 1.0f;       // how fast it talks (1 = normal)

    // On stage: how they come when a line doesn't say (where, at= and y=: the stage's 0 to 1 from the top left, none the
    // middle, standing at the bottom; their zoom, opacity, rotation, flip, tint and layer; the animation they play while
    // shown and the one they come with), and how they blink and talk (atlases with eyes and mouth frames).
    StageProps comes;
    std::optional<std::string> animation, animationOnShow;
    bool blink = true;
    float secondsBetweenBlinks = 4.0f;  // seconds between blinks, on average
    MouthMoves mouthMoves = MouthMoves::Yes;
    float mouthSpeed = 4.0f;   // mouth movements a second while text types without a voice

    // The face in the textbox: images/<face>.png, <face>_<expression>.png, shown by a visual showing {face}.
    std::string face;
    std::string faceSide = "left"; // which end of the textbox ({face_side} in screens)
    FaceCopies faceCopies;             // what it copies from the image on stage
    bool faceFlip = false;         // mirrored
    bool faceBlink = true;         // (default: like on stage)
    MouthMoves faceMouth = MouthMoves::Yes;     // (default: like on stage)

    // On the phone: their image beside their messages and in the phone's people (images/<avatar>.png,
    // <avatar>_<expression>.png; else their face's), a line about them, and what plays when they call
    // (else setting phone_ringtone); they're in its people while `contact` is true (none: never); and the
    // scenes with the questions the player can ask them from the phone, in their chat and on a call (chat=, call=).
    std::string avatar, about, ringtone;
    std::optional<Expr> contact;
    std::string love; // a love interest's (love=): its love's variable (loveVariable: mika.love)
    std::string chatScene, callScene;
    [[nodiscard]] std::string avatarOf(std::string_view expression) const {
        const std::string& base = avatar.empty() ? face : avatar;
        return base.empty() || expression.empty() ? base : std::format("{}_{}", base, expression);
    }
};

enum class Align : std::uint8_t { Start, Centre, End };

struct Style {
    struct State {
        std::optional<SDL_Color> colour, background, border;
        std::optional<SDL_Color> backgroundBottom; // (background= fading down to it: navy-black)
        std::string image;
    };

    std::string font = "default";
    float size = 24.0f;
    SDL_Color colour{240, 240, 245, 255};
    SDL_Color background{0, 0, 0, 0};
    std::optional<SDL_Color> backgroundBottom; // (background= fading down to it: navy-black)
    SDL_Color border{0, 0, 0, 0};
    std::string image;          // drawn stretched instead of background/border
    State pointed, chosen, unclickable;
    Align align = Align::Start; // text alignment inside the element's box
    Align alignVertically = Align::Start;
    float padding = 0.0f;
    float opacity = 1.0f;
    TextShadow shadow;
    float lineSpacing = 1.0f;   // 1 = the font's own
    bool opacityFollows = false;  // also fades with the player's "textbox opacity" setting
    bool textSizeFollows = false; // its size follows the player's "text size" setting
    bool speakerColour = false;  // text uses the speaking character's colour
    std::string pointedSound, clickSound;
};

// "animation NAME [loop=yes|N]", with what it changes on the lines under it: how a screen, a
// part of one (animation=, pointed_animation=, click_animation=) or a character or image on the stage
// (show ... animation=, show/hide ... with) moves, fades, grows, turns, changes colour and wipes. Each line changes some
// of those to its values: "over 1 second" from where the line before it ended, or at moments of its
// own ("from 0.5 to 1 second", "at 2 seconds"); without either, at once where the line before ended.
// "wait 1 second" leaves a gap. A change goes from where the ones before it left that value; one that
// starts while another of the same value goes on takes over from there. Played again and again (loop=yes),
// a few times (loop=3) or once, it stays as its end left it.
struct Animation {
    enum class Value : std::uint8_t { X, Y, Opacity, Zoom, Rotation, Colour, Fade, Wipe };
    struct Change {
        Value what = Value::Opacity;
        // x, y: a place on the screen, -1 to 1 like the stage's (NaN: home, its own place), or with `by`
        // a share of the screen to move by from its own place (left=5%: -0.05 across; up=5%: 0.05 down
        // the other way, as places go up); opacity 0..1; zoom 1 = its size; rotation in degrees.
        float to = 0.0f;
        bool by = false;
        SDL_Color colour{};       // colour=: an image's tint, words' colour, a rect's fill; fade=: what it's covered with (alpha 0: none, uncovered again)
        std::string shape;       // wipe= ... shape=IMAGE: where the wipe shows it first (the image's darkest parts); none: from the left
        float start = 0.0f;      // when it begins (seconds into the animation)
        float seconds = 0.0f;    // how long it takes (0: at once)
        Ease ease = Ease::InOut; // (a fade's: linear, as a screen's fade is, unless said)
        [[nodiscard]] float end() const { return start + seconds; }
    };
    struct Look {
        float x = 0.0f, y = 0.0f, opacity = 1.0f, zoom = 1.0f, rotation = 0.0f;
        std::optional<SDL_Color> colour; // (none: its own)
        float fade = 0.0f;              // how far it's covered (0 to 1), and with what
        SDL_Color fadeColour{0, 0, 0, 255};
        float wipe = 1.0f;              // how much of it shows (1: all of it): from the left, or as `wipeShape` has it (Wiper)
        std::string wipeShape;
        // Two at once (a part's own and its pointed one, a character's own and one played on it): moves
        // and turns added up, opacity and zoom multiplied, the other's colour, cover and wipe where it has them.
        [[nodiscard]] Look with(const Look& other) const;
    };
    std::string name;
    std::vector<Change> changes; // in the order they begin
    float length = 0.0f;         // one time through, in seconds
    int times = 1;               // how many times it plays (0: again and again)
    [[nodiscard]] bool loops() const { return times == 0; }
    // Until it ends, played as many times as it plays: one time through when it plays again and again
    // (a click's, or a closing screen's, then goes on).
    [[nodiscard]] float total() const { return loops() ? length : length * static_cast<float>(times); }
    // How it looks `seconds` after it started; `own`: the part's own colour (where a colour starts from);
    // `offset`: how far (in pixels) a place across (or down) is from its own place; `screen`: the game's
    // size (what a move by a share of it is a share of). Look's x and y are pixels then. `start`: where
    // its values start from (the camera: where the animation before left it; else as anything is).
    using Offset = std::function<float(bool across, float place)>;
    [[nodiscard]] Look at(float seconds, SDL_Color own, const Offset& offset, SDL_FPoint screen, const Look& start) const;
    [[nodiscard]] Look at(float seconds, SDL_Color own, const Offset& offset, SDL_FPoint screen) const { return at(seconds, own, offset, screen, Look{}); }
};

// The kinds of part, each doing what no other does (a list of things is a container with list=, a movie plays in an
// image, a bar is a slider that shows a value, a name box and the "go on" mark are texts in the textbox).
enum class ElementKind : std::uint8_t {
    Image, Text, Button,   // general (an image: a picture, a colour or a movie)
    Dialogue, Face,        // the line said (as it types) and the speaker's face
    Control, Input, Slider, // a key to change, a box to type in, a value to drag (or one shown)
    Flowchart,             // the flowchart
    Container, // parts in it (any part can hold parts; list=: once for each item of a list: the saves, the history...)
};
// The visual types every game has (a show line's "as TYPE", else what it shows), their layers (higher: in front) and
// what each takes: the background at 0 (one at a time, the whole stage: an image, a movie or a colour), characters at 1,
// images at 2 and movies at 3; the game's own types are at theirs, any whole number (under 0: behind the background),
// taking what they accept (Script::visualTypes).
inline constexpr std::array<std::string_view, 4> kVisualTypes{"background", "character", "image", "movie"};
inline constexpr std::array<int, 4> kVisualTypeLayers{0, 1, 2, 3};
inline constexpr std::array<std::string_view, 4> kVisualTypeAccepts{"image movie colour", "character", "image", "movie"};
[[nodiscard]] constexpr int visualTypeLayer(std::string_view type) {
    for (std::size_t i = 0; i < kVisualTypes.size(); ++i)
        if (kVisualTypes[i] == type) return kVisualTypeLayers[i];
    return 0;
}
// The sound types every game has (a play line's "as TYPE", else what it plays), as the player's three volumes are: music
// one at a time (a new song fading over the one before) and looping; sounds many at once, playing once; a voice one at a
// time, once; the game's own follow one of those volumes, as they say (Script::soundTypes).
inline constexpr std::array<std::string_view, 3> kBuiltInSoundTypes{"music", "sound", "voice"};
// What a container can repeat its parts for (list=NAME), and what one of them is called in its parts' values
// ({chat.name}): the options of the choice being made (on the phone, in the phone's lists; else the story's), the
// lines read (the history) and the lines on the page now, the save pages and the saves on the one open, the gallery's
// images, the endings, songs, achievements and tips; the phone's apps, its chats, the messages in the one open, its
// calls, the people in it and what's new on its lock screen; and the player's captions (what sounds and songs that
// just began are) and a movie's subtitle now.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 19> kLists{{{"options", "option"},
                                                                                      {"history", "line"},
                                                                                      {"lines", "line"},
                                                                                      {"pages", "page"},
                                                                                      {"saves", "save"},
                                                                                      {"gallery", "image"},
                                                                                      {"endings", "ending"},
                                                                                      {"songs", "song"},
                                                                                      {"achievements", "achievement"},
                                                                                      {"tips", "tip"},
                                                                                      {"apps", "app"},
                                                                                      {"chats", "chat"},
                                                                                      {"messages", "message"},
                                                                                      {"calls", "call"},
                                                                                      {"people", "person"},
                                                                                      {"notices", "notice"},
                                                                                      {"captions", "caption"},
                                                                                      {"subtitles", "subtitle"},
                                                                                      {"loves", "love"}}};
// Each list's items' values, after its word ({chat.name}): ScreenView::listItems() gives these (a line's are the
// history's or the page's: each has its own of them). Someone's (a chat's, a message's, a call's or a person's)
// have their character, avatar, initial and colour too. A text part showing just {line.text} keeps its look.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 18> kItemValues{{
    {"option", "text locked note on picked kind number"},
    {"line", "number name colour text choice voiced back typing"},
    {"page", "name number open"},
    {"save", "number used time text image"},
    {"image", "number name title locked"},
    {"ending", "number name title text locked"},
    {"song", "number name title locked playing"},
    {"achievement", "number name title text locked secret"},
    {"tip", "number name title text locked open new"},
    {"app", "name title icon initial count open"},
    {"chat", "name text time unread typing open character avatar initial colour"},
    {"message", "number from mine text typing image sticker sound time day edited deleted seen reaction ended character avatar initial colour"},
    {"call", "who missed busy outgoing incoming time length character avatar initial colour"},
    {"person", "name about character avatar initial colour"},
    {"notice", "who text chat call time"},
    {"caption", "text music"},
    {"subtitle", "text"},
    {"love", "name value hearts most character avatar initial colour"}}};
// Whether a kind of part ("container", "input"...) takes a setting of its own ("columns", "glide"...):
// the ones the editor's form for it shows, besides any part's place, size and look.
[[nodiscard]] bool partTakes(std::string_view kind, std::string_view key);
// What a part is, as it's written (kKinds): a text showing {dialogue} the line said, one with something to do when clicked a
// button; a visual showing {face} the speaker's face; an input a key to press, a slider or a box to type in (inputIs).
// `says`: whether it has that setting.
[[nodiscard]] ElementKind partKindOf(std::string_view kind, std::string_view text, const std::function<bool(std::string_view)>& says);
// A made-up voice's pitch (Hz) as a character's made_up_voice= says it: high, medium, low, deep, or a pitch from 50 to 600
// (none: no, or not one of those).
[[nodiscard]] std::optional<float> madeUpVoicePitch(std::string_view value);

// What an input is, by what it changes: a variable typed in, or slid (one with a range or a slider's look: minimum=, maximum=,
// step=, vertical=, track=, fill=, knob=), a player setting slid (setting=), a control's key (control=), or nothing: a value
// shown, filling it (value=).
enum class InputIs : std::uint8_t { Typed, Slid, Setting, Key, Shown };
[[nodiscard]] InputIs inputIs(const std::function<bool(std::string_view)>& says);
// The settings each takes (what it changes first), in its form's order.
[[nodiscard]] std::span<const std::string_view> inputTakes(InputIs is);
// The style a part has when it names none (by what it is: a button's "button"...), if the game has one: else "default".
[[nodiscard]] std::string_view partStyle(ElementKind kind, const std::function<bool(std::string_view)>& hasStyle);

// What screens' conditions and texts (and the story's) know besides the variables: every value
// Engine::builtin gives, in its order (the player's settings, by their names, too: Settings::has).
inline constexpr std::string_view kBuiltinValues[]{
    "in_game", "has_save", "has_quick_save", "can_save", "can_rollback", "skip", "auto", "speaking", "choosing", "prompt", "text_complete", "text_paused", "time_left",
    "tip", "tip_title", "tip_text", "flowchart_points", "gallery_total", "gallery_unlocked",
    "endings_total", "endings_reached", "songs_total", "songs_heard", "achievements_total", "achievements_earned", "tips_total", "tips_found",
    "messages_total", "chats_total", "open_chat_messages", "calls_total", "people_total", "open_chat", "phone_app", "has_phone", "phone_button", "menu_button", "messages_unread", "calls_missed", "phone_unread",
    "phone_locked", "in_call", "in_chat", "caller",
    "call_incoming", "call_outgoing", "call_busy", "call_choice", "call_time", "caller_avatar", "caller_colour", "caller_initial", "open_image", "open_image_text", "phone_needs_code",
    "phone_code", "phone_wrong", "keyboard_shift", "keyboard_text", "dragging", "has_face", "face_side", "fullscreen", "developer", "thumbnail", "save_mode", "title", "language", "languages",
    "version", "engine_version", "sdl_version", "speaker", "speaker_colour", "question", "page", "window_size",
    "time_day", "time_hour", "time_minute", "time_part", "time_weekday", "time_month", "time_year", "time_clock", "time_date", "time_waiting", "time_until",
    "most_loved"};

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
    [[nodiscard]] bool matches(const SDL_Event& e, bool repeats = false) const; // (`repeats`: a key held, going again, too)
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
    std::string text;  // a text's or button's words, an image's or a movie's name, a key's or a control's name...
    // Where it is and how big, as written: x and y places from -1 to 1 in the box it's in (the screen, or
    // the part it's in: -1 lines its left edge or bottom up with that one's; none: the top left corner),
    // w and h shares of that box, or pixels (140px; a width no wider than the box: in a smaller one, as wide
    // as that), none: automatic (a container fills it; in a box whose parts go one after another, the room
    // they leave). A part without a width (or a height) is placed by its corner.
    std::optional<float> placeX, placeY, shareWidth, shareHeight;
    bool pixelWidth = false, pixelHeight = false; // (shareWidth, shareHeight: pixels)
    Style style;
    Commands actions;
    Commands drop;       // drop=: what dropping something dragged on it does ({dropped} is what it was)
    std::string confirm; // ask this question before running the actions
    std::vector<KeyChord> keys; // keys=, a button's: they press it
    std::optional<Expr> visible, clickable, chosen; // when=, clickable= and chosen=
    std::map<std::string, Style, std::less<>> substyles;    // options ending in "_style"
    std::map<std::string, SDL_Color, std::less<>> colours;   // colour options (top, bottom, track...)
    std::map<std::string, std::string, std::less<>> props;  // other widget options
    std::map<std::string, Expr, std::less<>> exprs;         // formula options (a slider's value=, minimum=...)
    std::shared_ptr<const Animation> animation;             // animation=
    std::shared_ptr<const Animation> showAnimation;         // animation_on_show=: a visual's, played as it comes (from when it shows)
    std::shared_ptr<const Animation> pointedAnimation;        // pointed_animation=: while it's pointed at (and back to its start after)
    std::shared_ptr<const Animation> clickAnimation;        // click_animation=: a button's, played when it's clicked (then it does what it does)
    std::shared_ptr<const Animation> messageAnimation;      // message_animation=: played when a message comes
    std::shared_ptr<const Animation> itemAnimation;         // item_animation=: a list's (list=), played by each new item as it comes

    [[nodiscard]] float number(std::string_view name, float fallback) const;
    [[nodiscard]] std::string option(std::string_view name, std::string_view fallback = {}) const;
    [[nodiscard]] SDL_Color colour(std::string_view name, SDL_Color fallback) const;
    [[nodiscard]] const Style& substyle(std::string_view name) const;
    // A container's: scroll=yes (what's in it scrolls), fit=parts (as big as its parts, not its box: its
    // biggest part, or stacked, all of them) and edges= (what's past its edges: show, cut or fade; scrolling:
    // cut, unless it says). Any part that scrolls: whether a drag glides on (glide=; none: `usually`, the
    // game's setting), and which way dragging scrolls it (-1: a container that scrolls, or the flowchart: what's
    // in it follows the pointer; 0: it isn't dragged).
    [[nodiscard]] bool scrolls() const;
    [[nodiscard]] bool fitsParts() const;
    [[nodiscard]] std::string edges() const;
    [[nodiscard]] bool glides(bool usually) const;
    [[nodiscard]] float dragDirection() const;
    // Whether it's pressed as a button is (clicked when the pointer comes up on it): a text that's clicked, or a visual
    // or a box with an action.
    [[nodiscard]] bool pressedAsButton() const {
        return kind == ElementKind::Button || ((kind == ElementKind::Image || kind == ElementKind::Container) && !actions.empty());
    }
};

struct ScreenDef {
    std::string name;
    std::vector<Element> elements;
    bool storyWaits = true; // story=waits: the story waits while it's open (and nothing underneath takes clicks)
    float closeAfter = 0.0f; // close_after=: it closes by itself after this many seconds (0: it doesn't)
    // stop_on_open= (or stop_on_close=): the sound types, with commas between them, whose sounds stop then (as stop TYPE).
    std::vector<std::string> stopOnOpen, stopOnClose;
    Commands actionOnOpen, actionOnClose;
    // animation_on_open= / animation_on_close=: the whole screen moves and fades as these say when it opens
    // and when it closes (it stays on screen, without taking clicks, until that's over).
    std::shared_ptr<const Animation> animationOnOpen, animationOnClose;
};

// Something a key does ("control" lines). Players can change its keys with a screen's inputs for them (Settings' Keys).
struct Control {
    std::string name;
    std::vector<KeyChord> keys; // the defaults
    Commands actions;
    bool inMenus = false;       // also on the title screen and in menus (else only while playing)
    std::optional<Expr> condition; // when=: works only while it's true
    // Its actions only move, press or scroll on screens: it can share its keys with the story's controls (Enter can
    // press what the keys point at on a menu and go to the next line, as the mouse's left button clicks and goes on).
    [[nodiscard]] bool onScreens() const;
    // Its actions only move or scroll: held, its key goes again (a controller's after a moment, ten times a second).
    [[nodiscard]] bool repeats() const;
};
// What the developer tools do with their own keys ("tools WHAT", their built-in controls' actions: tools_copy and the
// rest): nothing in the game. A control that does only one of these is the tools' (its place here, else none).
inline constexpr std::array<std::string_view, 25> kToolsVerbs{"undo",  "redo",    "cut",     "copy",    "paste", "duplicate", "delete", "select all",
                                                            "reload", "quit",    "story",   "screens", "library", "play",   "stop here", "next",
                                                            "previous", "press", "up",      "down",    "left",  "right",  "back",      "next page",
                                                            "previous page"};
[[nodiscard]] std::optional<std::size_t> toolsVerbOf(const Control& control);

// What a screen's inputs for controls' keys call the mouse buttons, the wheel, controller buttons and touch gestures
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
// What Read aloud calls what shows no words: a save's delete button, and the on-screen keyboard's keys that type a sign
// (its others say their letters, and its Shift, Space, Delete and Done are its screen's own words). Words the player
// says that aren't the game's own either, so a translation file lists them, with a screen that has a box or saves.
inline constexpr std::array<std::string_view, 5> kReadAloudWords{"Delete", "Apostrophe", "Comma", "Full stop", "Hyphen"};

// What tells players a message has come to their phone (setting new_messages): the textbox's Phone button counting
// them, or the phone opening (the Phone button is there either way, to open it).
enum class NewMessages : std::uint8_t { Button, Open };
// How a chat scene's messages come (setting chat_messages): as they're typed (dots first; a click hurries one),
// or each after a click.
enum class ChatMessages : std::uint8_t { Typed, Click };

// The story's time (setting time): none, the story's own clock and calendar, the player's real ones (the story
// waits for them), or days and parts of the day (no clock).
enum class TimeKind : std::uint8_t { None, Story, Real, Days };
// Dates as days since 1 January 1970, and back; a date's weekday (0 Monday); the English names the
// scripts use (shown in the player's language).
[[nodiscard]] int daysFromDate(int year, int month, int day);
[[nodiscard]] std::array<int, 3> dateFromDays(int days); // year, month (1 to 12), day
[[nodiscard]] inline int weekdayOf(int days) { return ((days % 7) + 7 + 3) % 7; }
inline constexpr std::array<std::string_view, 7> kWeekdays{"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
inline constexpr std::array<std::string_view, 12> kMonths{"January", "February", "March", "April", "May", "June",
                                                         "July", "August", "September", "October", "November", "December"};

struct GameSettings {
    std::string title = "VNext";
    std::string version = "1.0";
    std::string language = "English"; // the scripts' own language, as players see it (Settings > Language)
    std::string saveFolder;           // setting save_folder: the folder its players' data goes in (none: its title's, saveFolderNameOf)
    std::string icon;
    std::string mousePointer;         // an image for the mouse pointer (else the system's)
    SDL_Point pointerTip{0, 0};    // the point of it that clicks
    SDL_Color letterboxColour{0, 0, 0, 255}; // around the image when the window isn't the same shape
    bool resizable = true;            // the window's edges can be dragged to resize it (the game's, and the developer tools': one setting)
    int width = 1280;
    int height = 720;
    int savePages = 9;
    int pageSlots = 6;
    bool autosave = true;
    int historyLength = 25;
    int rollbackLines = 100;  // lines and choices the player can go back (0: none)
    float skipTime = 0.05f;
    float autoLetterDelay = 0.01f;
    SDL_Keymod holdToSkip = SDL_KMOD_CTRL;
    std::string animationOnShow, animationOnHide; // setting animation_on_show / animation_on_hide: what show and hide lines play when they name none ("": nothing)
    float characterHeight = 0.95f;
    float moveTime = 0.4f;   // "show X at Y" on someone already there slides this long
    bool pixelArt = false;   // images scale with sharp pixels (else smoothly)
    int loadAhead = 30;      // story commands to look ahead for images to load early (0: none)
    int imageMemory = 512;   // MB of images kept loaded before old ones are let go (0: no limit)
    TextAppears textAppears = TextAppears::Letter;
    SDL_Color narrationColour{150, 170, 255, 255};
    float musicCrossfade = 0.5f;
    bool glide = true;        // lists and containers that scroll glide on after a drag (unless a part says), ...
    float glideSpeed = 1.0f, glideDistance = 1.0f; // ...this fast (of the speed they were let go at) and this far
    float scrollbarThickness = 6.0f; // pixels thick a scrollbar is (a part can say otherwise)
    float fadingEdges = 0.0f;      // pixels a fading edge (edges=fade) spans (0: a fifth of the box, at most 64)
    bool textFollowsVoices = false;   // the text of a voiced line types out in time with the voice
    float musicUnderVoices = 0.7f; // how loud music plays while a voice does (1: unchanged)
    float typingSoundGap = 0.06f, typingSoundVolume = 0.6f;
    float phoneTyping = 6.0f; // letters a second people type an answer on the phone (0: it arrives at once)
    std::optional<Expr> hasPhone; // has_phone CONDITION: the player has their phone while it's true (none: always)
    NewMessages newMessages = NewMessages::Button; // what tells players a message has come
    ChatMessages chatMessages = ChatMessages::Typed; // how a chat scene's messages come
    bool phoneHistory = false; // the phone's messages go in the history too
    // What the phone plays: a message coming, one the player sends, someone typing, a call coming (unless the
    // caller has a ringtone=) and the player calling. Empty: nothing.
    std::string phoneMessageSound, phoneSendSound, phoneTypingSound, phoneRingtone, phoneCallingSound;
    float madeUpVoiceSpeed = 1.0f, secondsBetweenBlinks = 4.0f, mouthSpeed = 4.0f; // defaults for characters
    bool blink = true;        // (blink no: nothing blinks, unless a character says blink=yes)
    MouthMoves mouthsMove = MouthMoves::Yes; // (mouths_move no: no mouth moves, unless a character says mouth_moves=)
    bool developer = false;   // developer mode: developer tools (F10), the reload action, labelled stand-ins for missing files
    std::map<std::string, int, std::less<>> settingDefaults; // "setting default.<setting>"
    // Time (setting time, and how it starts: time_start "2025-07-14 8:00", a date or a weekday, then a time of day
    // or a part of the day; no date and no weekday: days only by number).
    TimeKind time = TimeKind::None;
    int timeStartDate = -1;     // (days since 1970: see daysFromDate; -1: none)
    int timeStartWeekday = -1;  // (0 Monday; -1: none)
    int timeStartMinutes = 480; // (8:00)
    // The parts of the day (time_parts "morning 6:00, afternoon 12:00, evening 18:00, night 22:00"): each from the
    // minute it starts at, in order.
    std::vector<std::pair<std::string, int>> dayParts{{"morning", 360}, {"afternoon", 720}, {"evening", 1080}, {"night", 1320}};
    bool clock12 = false;              // setting clock 12: 6:30 PM (else 18:30)
    bool phoneFollowsTime = true;      // the phone's clock and date are the time's (phone_follows_time)
    bool backgroundsFollowTime = true; // a background's version for the part of the day shows, when there is one (park_evening)
};

struct Instruction {
    Command command;
    std::uint16_t file = 0;
    int line = 0;
    // A jump or return at the end of what's under a line (an if's branch, an option's lines), with that
    // line's number: the story isn't at that line when it does it (the developer
    // tools don't stop there).
    bool tail = false;
};

struct ScriptError {
    std::string file;
    int line = 0;
    std::string message;
};

// Reads a file relative to the game folder; nullopt if it does not exist.
using FileReader = std::function<std::optional<std::string>(const std::string& relativePath)>;
// The files in a folder of the game ("images/gallery/"), by their names: the gallery's and the music room's.
using FolderLister = std::function<std::vector<std::string>(const std::string& folder)>;

class Script {
public:
    // Reads `builtin` (the engine's default UI), then `entry` and everything it includes; later
    // definitions replace earlier ones; with `list`, the pictures in images/gallery and the songs in
    // sounds/music are in the gallery and the music room too. Always returns a Script; check ok() / errors.
    static Script load(const std::string& entry, const FileReader& read, std::string_view builtin = {}, const FolderLister& list = {});

    [[nodiscard]] bool ok() const { return errors.empty(); }
    [[nodiscard]] std::optional<std::size_t> findScene(std::string_view name) const;
    [[nodiscard]] const CharacterDef* character(std::string_view key) const;
    // What a visual by `name` shows, wherever one goes (a screen's visual, a style's, the phone's wallpaper, a message's
    // picture, a weather speck, a face...): a visual named by a visual line, what it shows (its shows=; none: what's named
    // as it); a character, their picture (with `expression`, that one's); anything else, itself (a picture, a sub-image,
    // an atlas's frame, a movie or a colour).
    [[nodiscard]] std::string visualOf(std::string_view name, std::string_view expression = {}) const;
    // The voice file a line plays, as sounds/ names it: its own (voice=NAME), else its speaker's for it (voice_files=:
    // voices/aiko/start_3); empty: none (made-up speech, or nothing).
    [[nodiscard]] std::string voiceOf(const cmd::Say& say) const;
    [[nodiscard]] const ScreenDef* screen(std::string_view name) const;
    // The scene containing `pc` and the offset from it (lets saves survive script edits).
    [[nodiscard]] std::pair<std::string, std::size_t> locate(std::size_t pc) const;
    [[nodiscard]] std::string where(const Instruction& ins) const;

    std::vector<Instruction> code;
    std::unordered_map<std::string, std::size_t, StringHash, std::equal_to<>> scenes;
    // A character's chat= or call= scene (by name): where its lines start (played when the player opens their chat or
    // calls them; at its end the story goes back to where it was).
    std::unordered_map<std::string, std::size_t, StringHash, std::equal_to<>> phoneScenes;
    std::unordered_map<std::string, CharacterDef, StringHash, std::equal_to<>> characters;
    std::vector<Atlas> atlases;
    std::vector<SubImage> subImages;
    // Every visual type: the ones every game has (kVisualTypes), then the game's own ("type NAME layer=N"). Each one's
    // layer, and what it accepts (characters, images, movies, colours: words with spaces between them; one of the game's
    // own with none written, all of them).
    struct VisualType {
        int layer = 0;
        std::string accepts = "character image movie colour";
    };
    std::unordered_map<std::string, VisualType, StringHash, std::equal_to<>> visualTypes;
    // The visuals with names of their own ("visual NAME type=TYPE shows=WHAT at=PLACE y=N zoom=... width=N height=N"): what
    // each is (its type: background, image, movie or the game's own), what it shows (a picture, a sub-image, a movie or a
    // colour; else the one named as it), how it comes when its line doesn't say (where: the stage's 0 to 1 from the top
    // left, none the middle, a background's the part of a picture bigger than the stage that shows; its zoom, opacity,
    // rotation, flip, tint and layer; the animation it plays while shown and the one it comes with; a movie's loop and
    // volume) and, a colour's box, its size (a part of the stage's; none: all of it). "show NAME" shows it as that type.
    struct NamedVisual {
        std::string type = "image", shows;
        StageProps comes;
        std::optional<std::string> animation, animationOnShow;
        std::optional<bool> loop;
        std::optional<float> volume, width, height;
    };
    std::unordered_map<std::string, NamedVisual, StringHash, std::equal_to<>> visuals;
    // Every sound type: the ones every game has (kBuiltInSoundTypes), then the game's own ("type NAME follows=music|sound|voice
    // one_at_a_time=yes loop=yes volume=60%"): which of the player's volumes each follows, whether one of it plays at a time
    // (a new one replacing the one playing, fading over) or many at once, whether what it plays loops unless a line says,
    // and a volume of its own (under its slider's).
    struct SoundType {
        SoundSlider follows = SoundSlider::Sound;
        bool oneAtATime = false, loop = false;
        float volume = 1.0f;
    };
    std::unordered_map<std::string, SoundType, StringHash, std::equal_to<>> soundTypes;
    // The sounds with names of their own ("sound NAME type=TYPE plays=FILE loop=yes volume=80% fade=2 fade_out=1"): what
    // each is (its type: music, sound, voice or the game's own), what it plays (a file in sounds/; else the one named as
    // it) and how, when its line doesn't say. "play NAME" plays it as that type; "stop NAME" stops it.
    struct NamedSound {
        std::string type = "sound", plays;
        std::optional<bool> loop;
        std::optional<float> volume, fade, fadeOut;
    };
    std::unordered_map<std::string, NamedSound, StringHash, std::equal_to<>> sounds;
    // What a sound by `name` plays, wherever one goes (a play line, a button's, the phone's, a line's voice, the music
    // room...): its file, as sounds/ names it, its type (`as`, else a named sound's own, else sound; a song, music/...:
    // music), how loud (its own volume, times its type's) and on which of the player's volumes, whether it loops, and how
    // it fades in and out (its own, else none; music's out: the game's crossfade, unset here).
    struct SoundPlay {
        std::string file, type = "sound";
        float volume = 1.0f;
        SoundSlider slider = SoundSlider::Sound;
        bool oneAtATime = false, loop = false, named = false;
        float fade = 0.0f;
        std::optional<float> fadeOut;
    };
    [[nodiscard]] SoundPlay soundOf(std::string_view name, std::string_view as = {}) const;
    std::unordered_map<std::string, Animation, StringHash, std::equal_to<>> animations;
    std::vector<std::string> files;
    std::vector<ScriptError> errors;
    std::uint64_t hash = fnv1a("");

    GameSettings gameSettings;
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

    // The gallery's images, the endings, the music room's songs, the achievements and the tips,
    // in the order they're listed: "gallery IMAGE "Title"", "ending NAME "Title" hint=...", "song
    // NAME "Title"", "achievement NAME "Title" "Description" [secret=yes]", "tip NAME "Title" "Description"".
    struct Record {
        std::string name;    // as written (an image's or a song's file name)
        std::string title;
        std::string text;    // an ending's hint (shown while it's locked), an achievement's description, a tip's text
        std::string scene;   // an ending's scene= (lower-case): it's reached as that scene ends
        bool secret = false; // an achievement whose title and words aren't shown until it's earned (???: {achievement.secret})
    };
    std::array<std::vector<Record>, kRecordKinds.size()> lists;
    std::vector<std::string> loves; // the love interests (characters with love=), in their order: their keys
    [[nodiscard]] const std::vector<Record>& records(RecordKind kind) const { return lists[static_cast<std::size_t>(kind)]; }
    [[nodiscard]] const Record* record(RecordKind kind, std::string_view name) const;
    // The persistent variable that remembers one, and whether a variable is one of those (they stay
    // when the player goes back with rollback).
    [[nodiscard]] static std::string recordVariable(RecordKind kind, std::string_view name) {
        return std::format("persistent.{}.{}", kRecordKinds[static_cast<std::size_t>(kind)], lower(name));
    }
    [[nodiscard]] static bool isRecordVariable(std::string_view name);

    // "caption SOUND "Words"": what the player's Captions show as that sound or song begins (by its name, in lower case).
    std::map<std::string, std::string, std::less<>> captions;

    // Translations. Where a text players read is (see forEachText):
    struct TextPlace {
        const Instruction* line = nullptr; // a story line, choice, question or message
        std::string_view what;             // else "title", "character", "screen", "caption" or a record kind ("gallery"...)
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

// The gallery's or the music room's entries their folders give, as `list` finds them (the pictures in images/gallery,
// the songs in sounds/music): named as the game has them (gallery/beach_sunset), titled as their files are called
// ("Beach sunset"). The game has them after the ones its scripts list (Parser::folderRecords); none for the others.
[[nodiscard]] std::vector<Script::Record> folderRecords(RecordKind kind, const FolderLister& list);

// The story command a line starts with ("scene", "quick save"...), or empty (a line of text).
[[nodiscard]] std::string storyCommandOf(std::string_view line);
// "Name: words" or "Name expression: words" (at most three words before the colon, then a space or
// nothing): someone saying it, with `speaker` and `words` set to its two parts.
[[nodiscard]] bool spoken(std::string_view line, std::string_view* speaker = nullptr, std::string_view* words = nullptr);
// A line said (someone's or narration) with its own voice: " voice=NAME" at its end (NAME in quotes when it has
// spaces), the sound it plays. The line without it, and in `voice` the name (empty: none written after it); the
// line as it is when it has none.
[[nodiscard]] std::string_view withoutVoice(std::string_view line, std::optional<std::string>* voice = nullptr);
// A line that starts as commands do, with a word in lower case, without being a story command, a
// definition, someone speaking or text in quotes ("notify ...", "next line"): that word, else empty.
// It's a mistake, not text: text that starts with a lower-case letter goes in quotes.
[[nodiscard]] std::string unknownCommand(std::string_view line);

// The player's built-in screens, as script (BuiltInScreens.cpp): every game starts with them.
[[nodiscard]] std::string_view defaultUi();

} // namespace vn
