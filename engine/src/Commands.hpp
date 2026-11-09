#pragma once

// The story's commands: what each line of a scene does, as the parser leaves it for the game to run.

#include "Values.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vn {

// Optional changes to an image on stage (characters and images).
struct StageProps {
    std::optional<float> x, y;             // 0..1 across / down the canvas (the image lines up with that much of it)
    std::optional<float> zoom, opacity, rotation;
    std::optional<bool> flip;
    std::optional<int> layer;              // higher layers draw on top
    std::optional<SDL_Color> tint;         // multiplies its colours (black: a silhouette)
};

// How a change goes from where it was to where it's going: evenly, speeding up, slowing down, or
// both; bouncing as it lands, springing past and back, or going a little past and back.
enum class Ease : std::uint8_t { Linear, In, Out, InOut, Bounce, Spring, Overshoot };
// t (0..1) with that speeding up and slowing down. Spring and Overshoot go past 1 on the way.
[[nodiscard]] float eased(Ease e, float t);

// Things the player collects, each listed once in the scripts ("gallery", "ending", "song",
// "achievement" and "tip" lines) and remembered for good once reached: persistent.<kind>.<name>,
// which the story sets ("set persistent.ending.good to yes"). Images and songs are also had by showing
// or playing them, tips by a line that shows {tip NAME}.
enum class RecordKind : std::uint8_t { Gallery, Ending, Song, Achievement, Tip };
inline constexpr std::array<std::string_view, 5> kRecordKinds{"gallery", "ending", "song", "achievement", "tip"};

namespace cmd {

struct Say {
    std::string character;  // a defined character (lower-case name), or empty
    std::string speaker;    // a one-off speaker name, or empty for narration
    std::string expression; // optional new expression for the character
    std::string text;       // may contain {expressions} and text tags
    std::string voiceId;    // "<scene>_<n>": the character's n-th line in the scene (for voice_files=)
    std::uint64_t id = 0;   // the line as written, who says it included (the same in every language)
    bool together = false;  // in a "together" block, before its last line: it shows with the next one, without waiting
};
// A line's animations are the scripts' (by name): "" for none. "with NAME" plays one now: as a new
// background, character or image comes (over what was there), on one already there, or as one goes.
struct Background {
    std::string image; // image name or "#rrggbbaa"
    std::optional<std::string> animation; // animation=NAME: what the background plays while it's shown
    std::optional<std::string> with;      // with NAME: as it comes in, over the one before (which goes when it ends); on the one already there
};
struct Show {
    std::string character, image; // one of them
    std::string tag;               // name used by hide
    std::string expression;
    StageProps props;
    std::optional<std::string> animation; // animation=NAME: what it plays while it's shown (animation=none: nothing)
    std::optional<std::string> with;      // with NAME: as it comes, or on the one already there (none said: setting animation_on_show, as one comes)
    bool wait = false;                    // and wait: the story waits for it to end (and for it to reach a new place)
};
struct Hide {
    std::string tag;
    std::optional<std::string> with; // with NAME: plays, then it's gone (none said: setting animation_on_hide)
    bool wait = false;               // and wait: the story waits until it's gone
};
struct Choice {
    std::string text;
    std::size_t target = 0;
    std::optional<Expr> condition;
    bool shownLocked = false; // (needs ...): while the condition is false it shows, but can't be picked
    std::string note;         // what it needs, shown with it then
    std::uint64_t id = 0;     // the option as written (the same in every language): "picked before"
};
struct Menu {
    std::vector<Choice> choices;
    float seconds = 0.0f;  // for N seconds: then none is picked (0: no time limit)
    int lines = 0;         // for N lines: the story goes on meanwhile, the options up for its next N lines
    bool returns = true;   // (for N lines) after a picked option's lines, back to the line after the one showing; no: past the N lines
    bool optional = false; // optional=yes: clicking on picks none (the line before it stays on screen)
    bool repeats = false;  // repeat=yes: after a picked option's lines, it's up again
    std::size_t after = 0; // where the story goes on when none is picked
    // A chat's (in a chat scene, or someone's chat= scene) or a call's (someone's call= scene): its options show on the
    // phone, in this person's chat (the one picked is sent there) or on the call with them.
    std::string phone;
    bool call = false;
};
struct Jump {
    std::string scene;
    std::size_t target = 0;
    bool again = false;           // back to its repeating choice, after an option's lines (shown without the line before it)
    std::optional<Expr> unless;   // only jump when this is false (if / else)
};
struct Return {}; // back to the line after what started this part (an option's lines, a phone scene)
struct End {};    // a scene's end: back to what started it, if something did; else the story's over
struct Set { // set X to V
    std::string variable;
    std::optional<Expr> value;
};
struct Play {
    enum class Track : std::uint8_t { Music, Sound, Voice };
    Track track = Track::Sound;
    std::string file;    // empty: stop
    std::string channel; // sounds: a named channel that can loop and be stopped
    bool loop = false;
    float volume = 1.0f;
    float fade = 0.0f;               // in (stopping: out)
    std::optional<float> fadeOut;    // music: the song before's (else setting music_crossfade); a sound: the one before by its name
                                     // (else at once), or for one without a name the others playing (else they play on)
    std::optional<bool> textFollows; // voice: the next line types out with it (else setting text_follows_voices)
};
struct Wait { float seconds = -1.0f; }; // < 0: until a click
struct Screen { std::string name; bool show = true; }; // "show screen" / "hide screen": up while playing
// Rain, snow, petals, leaves or embers over the stage. kind empty: stop.
struct Weather {
    std::string kind;
    float amount = 1.0f, speed = 1.0f, wind = 0.0f;
    std::optional<SDL_Color> tint;
    std::string image; // an image for each flake / drop instead of the drawn one
    bool behind = false; // behind the characters (else in front of them)
    float fade = 1.0f;
};
// The view of the stage, played as an image's animations are: one it keeps playing (animation=NAME),
// and one it plays now (with NAME), from where the one before left it ("": none; with none: back to
// normal); `wait`: the story waits for that one.
struct Camera {
    std::optional<std::string> animation, with;
    bool wait = false;
};
// A movie (movies/NAME.mpg) over everything; the story goes on when it ends, or when the player
// clicks past it.
struct Movie {
    std::string file;
    bool loop = false;      // plays again and again, until clicked past
    bool skippable = true;  // clicks, keys and skipping go past it
    float volume = 1.0f;    // its sound, on top of the music volume
};
// Menu commands: open (from the story it waits for the screen to close), close, answer, quick
// save...
struct Ui {
    std::string verb, arg;
    std::optional<std::string> opening, closing; // open SCREEN with NAME and close with NAME: what it plays this time (none said: its own animation_on_open= and animation_on_close=)
    std::string phone; // what the game asks of the phone: the chat to show
};
// "effect sepia noise": how the whole stage looks, until "stop effect" (StageLook bits; vibration is felt: the
// controllers rumble). With "for N seconds" they're added for that long, on top of what's there.
struct Effect {
    unsigned looks = 0;
    float amount = 1.0f; // how strong
    float fade = 0.0f;   // coming in (or going) over this many seconds
    float seconds = 0.0f;
};
// A line of a chat scene: a message on the phone in its chat, "Mika: words" someone's and
// "me: words" the player's. Its other forms send an image, a sticker or a voice message, or change the
// chat: someone deletes or edits their last message, reacts to the last one someone else sent, reads the
// player's, or types for a while. End: the scene is over (a line saying so, after its last).
struct Message {
    enum class Kind : std::uint8_t { Text, Image, Sticker, Voice, Typing, Delete, Edit, React, Read, End };
    Kind kind = Kind::Text;
    std::string character;  // who sent it (the player's own: who it goes to): a defined character (lower-case name)...
    std::string sender;     // ...or anyone else's name
    std::string expression; // their face's, beside it
    bool mine = false;      // the player's (whoever owns the phone)
    std::string text;       // its words: a message's, an image's or voice message's caption, Edit's new ones, React's reaction
    std::string file;       // Image and Sticker: an image; Voice: a sound
    float seconds = 0.0f;   // Typing: for how long (0: two seconds)
};
// The phone has no lines of its own besides chats and calls: whether there is one, and its apps, go by
// variables (setting has_phone, each app's when=), and so do its owner, wallpapers, passcode, clock, date,
// signal and battery (phone_owner, phone_wallpaper, phone_lock_wallpaper, phone_passcode, phone_clock,
// phone_date, phone_signal, phone_battery: as Settings starts them). It locks whenever it's put away.
// "chat WHO": a chat scene. The phone comes up on their chat, its lines play there (the story waits for
// each), and `end` (after them) waits for a click and puts the phone back.
struct Chat {
    std::string with; // whose: their name, as shown
    bool end = false;
};
// "time 18:30", "time evening", "time saturday evening", "time morning day=3": the story's time goes on to the next
// such moment (staying when it's that already; day=N: to that day; a weekday or a day without a time of day: at the
// same time); "time passes 2 hours" (minutes, hours or days; alone: to the next part of the day). With setting time
// real, the story waits for the player's own clock instead.
struct Time {
    int weekday = -1;    // 0 Monday to 6 Sunday (-1: none said)
    int day = 0;         // day=N (0: none said)
    int minutes = -1;    // a time of day, in minutes from midnight (-1: none said)...
    std::string part;    // ...or a part of the day (setting time_parts), by its name
    bool passes = false; // time passes...
    int passing = 0;     // ...this many minutes (0: to the next part of the day)
};
// "phone call WHO": they call, and the phone rings until it's answered or declined (or `for N seconds`
// pass). Its lines are the call; its `missed:` lines run instead when nobody answers. `end`: the call is
// over (after its lines).
struct Call {
    std::string character, sender; // who: a defined character (lower-case name), or anyone else's name
    float seconds = 0.0f;   // rings at most this long (0: until it's answered or declined)
    std::size_t missed = 0; // its missed: lines (or past it)
    std::size_t after = 0;  // past it
    bool end = false;
};

} // namespace cmd

using Command = std::variant<cmd::Say, cmd::Background, cmd::Show, cmd::Hide, cmd::Menu, cmd::Jump,
                             cmd::Return, cmd::End, cmd::Set, cmd::Play, cmd::Wait, cmd::Screen, cmd::Ui,
                             cmd::Weather, cmd::Camera, cmd::Movie,
                             cmd::Message, cmd::Effect, cmd::Chat, cmd::Call, cmd::Time>;
using Commands = std::vector<Command>;

} // namespace vn
