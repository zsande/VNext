#pragma once

// The story as it plays (Game): its lines, choices, values, the phone, the history; and what it's told by the
// player (Context).

#include "Stage.hpp"
#include "Flowchart.hpp"
#include "Script.hpp"
#include "Text.hpp"
#include "Sound.hpp"
#include "PlayerSettings.hpp"
#include "Movie.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vn {

// Hashes of every line the player has seen (across all playthroughs), used by "skip read text only":
// from read.txt's text, and as that.
using ReadLog = std::unordered_set<std::uint64_t>;
[[nodiscard]] ReadLog readLogOf(std::string_view saved);
[[nodiscard]] std::string readLogText(const ReadLog& log);

struct Context {
    SDL_Renderer* renderer;
    Assets& assets;
    Sound& sound;
    const Settings& settings;
    ReadLog& read;
    Variables& persistent; // "persistent.*" variables, shared by every playthrough
    Flowchart& flowchart;          // the flowchart, from every playthrough
    const Canvas& canvas;  // the game's size (the engine's)
    std::function<std::optional<Value>(std::string_view)> builtin; // {title}, skip, has_save...
};

// A line on the page (textbox page): who said it and what, with its colours.
struct PageLine {
    std::string speaker;
    SDL_Color colour;
    std::string text;
    std::vector<TextSpan> spans;
    std::optional<SDL_Color> textColour;
};

// An option of the choice on screen (or on the phone).
struct ChoiceItem {
    std::string text;    // its text tags kept, for the screen
    bool locked = false; // shown, but it needs something first: (needs ...)
    std::string note;    // what it needs
    bool seen = false;   // picked before (in this playthrough or an earlier one)
    int option = 0;      // which one it is, as written
};

// A message on the phone, in someone's chat (theirs and the phone owner's).
struct MessageItem {
    using Kind = cmd::Message::Kind; // Text, Image, Sticker, Voice; Typing: someone typing for a while
                                     // (nothing comes); End: a chat scene's over
    Kind kind = Kind::Text;
    std::string from;       // who sent it (empty: the phone's owner)
    std::string with;       // the chat: the person's name
    std::string character;  // the sender's, a defined character (their face beside it)
    std::string expression; // ...and how it looks
    SDL_Color colour{255, 255, 255, 255}; // the sender's
    std::string text;       // its words, text tags and all (an image's or voice message's caption)
    std::string file;       // Image, Sticker: the image; Voice: the sound
    std::string time, date; // the phone's clock when it came
    std::string reaction;   // someone's reaction to it
    bool read = false;      // the owner has seen it
    bool seen = false;      // (the owner's own) the other side has read it
    bool deleted = false, edited = false;
    float typing = 0.0f;      // seconds until it comes (meanwhile they're typing)
    std::uint32_t number = 0; // the count of messages when it came (the newest has the highest)
};
// A message proper: not someone typing, nor a change to one, nor the line saying a chat scene's over.
[[nodiscard]] inline bool isMessage(MessageItem::Kind kind) {
    using Kind = MessageItem::Kind;
    return kind == Kind::Text || kind == Kind::Image || kind == Kind::Sticker || kind == Kind::Voice;
}
// A call made or missed.
struct CallItem {
    std::string who;
    SDL_Color colour{255, 255, 255, 255};
    bool outgoing = false, missed = false, busy = false; // missed: nobody answered; busy: declined
    bool seen = false;                     // (a missed one) the owner has seen it
    std::string time, date;
    int seconds = 0; // how long it went on
};
// The phone's contents.
struct PhoneData {
    std::vector<MessageItem> messages;
    std::vector<CallItem> calls;
};
// A call under way: ringing, or talking (its lines are being said).
struct CallNow {
    std::string who, character; // (character: a defined one's lower-case name)
    SDL_Color colour{255, 255, 255, 255};
    bool outgoing = false, talking = false;
    bool busy = false;                    // declined: it says Busy a moment (time: since), then it's over
    bool talks = false;                   // the player's, to someone with a call= scene: answered, and it plays
    float time = 0.0f;                    // how long it has rung, or gone on
    float rings = 0.0f;                   // it rings at most this long (0: until answered)
    std::optional<std::size_t> at;        // its command (none: the player's call with nobody to answer)
};

struct HistoryEntry {
    std::string speaker;
    SDL_Color colour{};
    std::string text;
    bool choice = false; // a choice the player made
    std::vector<TextSpan> spans;
    // The line's voice, to hear it again: a sound file, or made-up speech (made_up_voice=) of its text.
    std::string voice;
    float madeUpVoice = 0.0f, madeUpVoiceSpeed = 1.0f;
    [[nodiscard]] bool voiced() const { return !voice.empty() || madeUpVoice > 0.0f; }
};

// A screen the story opened and waits for ("open", a new day's calendar).
struct ScreenRequest {
    std::string name;
    std::optional<std::string> opening, closing; // open ... with NAME and close with NAME: what it plays this time (none said: its own)
};

// Plays a Script: runs its commands until one has to wait (a line, a choice, a timer, an
// animation, a called screen) and draws the stage. The textbox and menus are script screens that
// read the state below. Exists on the title screen too (not started), so buttons there can set
// variables and play music.
class Game {
public:
    Game(Context ctx, const Script& script);
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;
    ~Game() = default;

    void start(std::size_t pc);
    void carry(Variables vars) { carried_ = std::move(vars); } // the variables the next start() begins with (else none)
    void update(float dt);
    void draw(const Font& labelFont);
    [[nodiscard]] Stage& stage() { return stage_; } // (the developer tools' samples: an effect or the weather tried on it)

    // Player input.
    void nextLine();
    void choose(std::size_t index);
    void setUiHidden(bool hidden) { uiHidden_ = hidden && (wait_ == Wait::Say || wait_ == Wait::Menu); }
    void toggleSkip() { skip_ = !skip_; }
    void toggleAuto() { auto_ = !auto_; }
    // A command from a button, key or screen hook (anything that doesn't wait).
    void perform(const Command& command);
    // The developer tools' Play from here (and the Screens page's sample): plays on as fast as it goes
    // (picking the options that lead to command `target`) to the first line, choice or screen opened at a
    // command where `there` is true, and shows it (a line whole), with the stage settled. When the story would
    // go past `target` without doing it (an `if` it's in isn't true, or the option it's under isn't picked), it
    // goes on from `target` itself, with everything as it is then. False if the story never gets there (or not
    // within a second). Movies that play once are gone past.
    bool playTo(std::size_t target, const std::function<bool(std::size_t)>& there);
    // Set before start(): the story stops just before command `pc` (playTo() stops there, ready to do it,
    // whatever it is: a background, a scene...), so a save made then starts with it.
    void stopBefore(std::optional<std::size_t> pc) { stopAt_ = pc; }
    // The developer tools' Stop here and Next line: the story asks `where` before each command it runs,
    // and stops just before one it says yes to, until goOn() (a save made meanwhile starts with it).
    void stopWhere(std::function<bool(std::size_t)> where) { stopWhere_ = std::move(where); }
    [[nodiscard]] bool stoppedThere() const { return wait_ == Wait::Stopped; }
    void goOn();
    // The screen the story was waiting for closed, with an answer ("answer TEXT") or not.
    void screenClosed(std::optional<Value> answer);
    // A menu covers the game (or no longer does): the line's voice and the stage's movies wait meanwhile.
    void setCovered(bool covered);
    [[nodiscard]] bool lineVoiced() const { return voiced_ && !madeUpVoice_; } // the line on screen has a voice of its own (made-up speech isn't one)
    // Captions (the player's setting): the sounds and songs that just began, by their caption lines (a song's after a
    // note), each for a few seconds; and the movie's subtitle now (its .srt's).
    struct Caption {
        std::string text;
        std::vector<TextSpan> spans; // (its words' look, as its caption line's tags have it: bold, colours...)
        bool music = false;
        float left = 0.0f; // seconds
    };
    [[nodiscard]] const std::vector<Caption>& captions() const { return captions_; }
    [[nodiscard]] std::string subtitle() const;
    [[nodiscard]] bool captioned() const { return !captions_.empty() || !subtitle().empty(); }

    // Variables, then persistent ones, then what `variable` lines start them at, then the engine's built-in names.
    [[nodiscard]] Value variable(std::string_view name) const;
    // As text, for a box the player types into: empty while nothing has given it a value yet (not 0).
    [[nodiscard]] std::string typedValue(std::string_view name) const;
    void setVariable(const std::string& name, Value value);
    [[nodiscard]] const Variables& variables() const { return vars_; } // the story's own (set so far)
    [[nodiscard]] std::vector<Stage::ObjectState> onStage() const { return stage_.objects(); }
    [[nodiscard]] const Lookup& lookup() const { return lookup_; }
    // While set: a list's item's values ({chat.name}...), seen before everything else, so what a button in one
    // does is for that item (see Engine::run).
    void setItem(const Variables* values) { item_ = values; }
    [[nodiscard]] const Variables* item() const { return item_; }

    // State for the screens.
    [[nodiscard]] bool finished() const { return wait_ == Wait::Finished; }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] bool showingSay() const { return wait_ == Wait::Say || wait_ == Wait::Clock || (wait_ == Wait::Menu && menuPrompt_); } // (Clock: when the story goes on)
    [[nodiscard]] bool showingChoices() const { return wait_ == Wait::Menu || trigger_; } // (one up while the story goes on too)
    [[nodiscard]] bool menuPrompt() const { return wait_ == Wait::Menu && menuPrompt_; }
    [[nodiscard]] bool textComplete() const; // waiting for a click (end of the line or a {pause})
    [[nodiscard]] bool atPause() const { return textComplete() && pendingStop(); } // ...at a {pause}
    [[nodiscard]] const std::string& speaker() const { return speaker_; }
    [[nodiscard]] SDL_Color speakerColour() const { return speakerColour_; }
    [[nodiscard]] std::optional<SDL_Color> textColour() const { return textColour_; }
    [[nodiscard]] const std::string& textbox() const { return textbox_; }
    [[nodiscard]] const std::string& dialogue() const { return text_; }
    [[nodiscard]] const std::vector<TextSpan>& spans() const { return spans_; }
    [[nodiscard]] std::size_t revealed() const { return static_cast<std::size_t>(reveal_); }
    // The next {pause} (or the end): word and line reveal never show text past it.
    [[nodiscard]] std::size_t revealLimit() const;
    // The speaker's face image with their expression ("aiko_face_happy"), and without it; empty
    // when they have none.
    [[nodiscard]] const std::string& face() const { return face_; }
    [[nodiscard]] const std::string& faceBase() const { return faceBase_; }
    [[nodiscard]] const std::string& faceSide() const { return faceSide_; } // empty when there's no face
    // What a textbox screen shows: the line on screen (in its textbox), or a line in another one said
    // together with it. boxes(): the textboxes with a line to show, the line on screen's last.
    struct LineView {
        std::string_view speaker, text, face, faceBase, faceSide;
        SDL_Color speakerColour{255, 255, 255, 255};
        std::optional<SDL_Color> textColour;
        const std::vector<TextSpan>* spans = nullptr;
        std::size_t revealed = 0, limit = 0; // letters shown, and the most word or line reveal may show
        TextAppears mode = TextAppears::Letter;
        bool complete = false, atPause = false; // all there, or at a {pause}: waiting for a click
        const CharacterDef* character = nullptr;
        Frame faceFrame;
    };
    [[nodiscard]] std::optional<LineView> line(std::string_view box) const;
    [[nodiscard]] std::vector<std::string> boxes() const;
    // The eyes and mouth a character (or an image on stage, by its name) shows now: blinking,
    // and talking while it's their line. faceFrame(): the speaker's face (it blinks and talks on
    // its own when face_copies= leaves those out).
    [[nodiscard]] Frame frame(std::string_view who) const;
    [[nodiscard]] Frame faceFrame() const;
    [[nodiscard]] const CharacterDef* speakerCharacter() const { return script_.character(speakerKey_); }
    [[nodiscard]] std::string expressionOf(std::string_view character) const; // a character's latest ("": none)
    // The images the next `commands` story commands show, each with the colour effects it will be
    // drawn with (to load them before they're needed; a colour effect coming needs copies of what's
    // on stage then).
    struct Upcoming {
        std::string name;
        unsigned looks = 0;
    };
    [[nodiscard]] std::vector<Upcoming> upcomingImages(int commands) const;
    [[nodiscard]] const std::vector<ChoiceItem>& choices() const { return choiceItems_; }
    // The phone: its messages (the newest last) and calls.
    [[nodiscard]] const PhoneData& phone() const { return phone_; }
    [[nodiscard]] const std::vector<MessageItem>& messages() const { return phone().messages; }
    void readMessages(std::string_view with = {}); // the ones in a chat (or all) have been seen
    // The chats, the latest first: each person's, with its newest message, how many
    // haven't been seen and whether someone's typing in it; ones with nothing in them yet last.
    struct Chat {
        std::string name;
        SDL_Color colour{255, 255, 255, 255};
        std::string character; // (a defined character's)
        std::optional<std::size_t> last; // its newest message
        int unread = 0;
        bool typing = false;
    };
    [[nodiscard]] std::vector<Chat> chats() const;
    void callPerson(std::string_view who); // the player calls someone: it rings, then it's answered (they have a call= scene) or busy
    // Someone's chat= or call= scene plays when the player opens their chat or their call's answered, aside (the story
    // stays on its line, shown again after it); a choice in it waits on the phone, the story going on meanwhile: its
    // options there, and the player picking one (in a chat, sent there as theirs).
    [[nodiscard]] bool waitsOnPhone(std::string_view who, bool call) const { return parked_ && parked_->who == who && parked_->call == call; }
    [[nodiscard]] std::vector<ChoiceItem> phoneChoices(std::string_view who, bool call) const;
    void pickOnPhone(std::size_t option);
    [[nodiscard]] bool asking() const { return asked_ && callStack_.size() >= asked_->depth; } // (a chat or call scene's lines, running)
    void hangUp(); // the player's call ends: the rest of its call scene first, its choices passed
    void chatOpen(const std::string& with); // the player's chat open on the phone now (none: ""): its chat= scene as it's opened, its rest as it's left
    // A chat scene on: its chat (empty: none). A choice in it is on the phone: its options are choices().
    [[nodiscard]] const std::string& chatScene() const { return chat_; }
    [[nodiscard]] bool choiceOnPhone() const; // (the choice waited on is a chat scene's)
    // A call ringing or under way, and the player answering or declining one that comes.
    [[nodiscard]] const std::optional<CallNow>& call() const { return call_; }
    void answerCall(bool answer);
    [[nodiscard]] bool phoneBusy() const { return !chat_.empty() || call_.has_value(); } // (it can't be put away meanwhile)
    // The story's on the phone: a chat scene, a call that came, or a chat= or call= scene's lines. Clicks on the phone go on
    // with it then; on the player's own call or chat they're the phone's.
    [[nodiscard]] bool storyOnPhone() const { return !chat_.empty() || (call_ && !call_->outgoing) || asking(); }
    // The phone's calls have been shown (the count on their app goes).
    void seeCalls();
    [[nodiscard]] int unseenMessages() const; // (not the ones still being typed)
    [[nodiscard]] int unseenCalls() const;    // (missed ones)
    [[nodiscard]] std::uint32_t arrivals() const { return arrivals_; } // messages come so far (one coming plays message_animation=)
    // (while setting has_phone's condition is true, and its battery isn't flat: the variable phone_battery)
    [[nodiscard]] bool hasPhone() const {
        return (!gameSettings().hasPhone || gameSettings().hasPhone->test(lookup_)) && toNumber(variable("phone_battery")) > 0;
    }
    void updatePhone(float dt); // messages being typed arrive, calls ring (with the story, or with the phone open over a menu)
    [[nodiscard]] float vibration() const { return stage_.vibration(); } // (effect vibration: how strongly the controllers rumble)
    // How much of a timed choice's time is left (1 down to 0), if one is on screen.
    [[nodiscard]] std::optional<float> timeLeft() const;
    // What came to the phone since this was last taken, for Read aloud to say: someone's message, who sent it first.
    [[nodiscard]] std::vector<std::string> takeNews() { return std::exchange(news_, {}); }
    [[nodiscard]] const std::vector<HistoryEntry>& history() const { return history_; }
    [[nodiscard]] const std::string& flowchartPoint() const { return flowchartNode_; } // the last point on the flowchart passed
    [[nodiscard]] std::size_t here() const { return resumePc_; } // the command the story is at
    [[nodiscard]] std::vector<std::size_t> takeRan() { return std::exchange(ran_, {}); } // the commands run since last asked (the editor marks lines never run)
    [[nodiscard]] const std::string& music() const { return music_; } // the story's song (a name), and how loud
    [[nodiscard]] float musicVolume() const { return musicVolume_; }
    // The lines on the page so far (the newest last, it's the one being typed).
    [[nodiscard]] const std::vector<PageLine>& page() const { return page_; }
    [[nodiscard]] const std::vector<std::string>& shownScreens() const { return screens_; }
    [[nodiscard]] const ScreenRequest& request() const { return request_; }
    [[nodiscard]] bool uiHidden() const { return uiHidden_; }
    [[nodiscard]] bool canSave() const { return wait_ != Wait::None && wait_ != Wait::Finished; }
    [[nodiscard]] bool skipping() const;
    [[nodiscard]] bool skipMode() const { return skip_; }
    [[nodiscard]] bool autoMode() const { return auto_; }
    // The line on screen, e.g. "Aiko: Hello!" (shown on save slots).
    [[nodiscard]] std::string summary() const;

    // Things the engine picks up once per frame.
    [[nodiscard]] bool takeAutosaveRequest() { return std::exchange(autosave_, false); }
    [[nodiscard]] bool takeScreenRequest() { return std::exchange(screenRequested_, false); }
    [[nodiscard]] std::vector<cmd::Ui> takeRequests() { return std::exchange(requests_, {}); } // open, show phone...
    [[nodiscard]] bool takePersistentChanged() { return std::exchange(persistentChanged_, false); }

    // Save data is a small text format (`history`: with the history's lines, as saves have them; rollback's moments
    // count them instead). load() returns a note for the log (when the script changed since saving) or an error.
    [[nodiscard]] std::string save(bool history = true) const;
    std::expected<std::string, std::string> load(std::string_view data);

    // The script's texts changed language: shows the ones on screen in the new one.
    void retranslate();
    // Rollback: back to the line (or choice) before this one, as it was then. rollbackTo(): back to
    // the line (or choice) of history() entry `entry`, if it isn't too far back.
    [[nodiscard]] bool canRollback() const;
    bool rollback();
    [[nodiscard]] bool canRollbackTo(std::size_t entry) const { return momentOf(entry).has_value(); }
    bool rollbackTo(std::size_t entry);
    // The story's variables in a save (to show a flowchart point's texts as they were there).
    [[nodiscard]] static Variables savedVariables(std::string_view state);

private:
    // (Chat: a chat scene waits for a message to come, or for a change to it; Call: a call rings; Clock: the player's
    // clock, setting time real)
    enum class Wait : std::uint8_t { None, Say, Menu, Timer, Background, Pause, Move, Screen, Reached, Stopped, Finished, Chat, Call, Clock };

    struct Stop {
        std::size_t at; // codepoint position in the text
        float seconds;  // < 0: wait for a click
    };
    struct Speed {
        std::size_t at;
        int cps; // < 0: back to the player's setting
    };
    struct Loop {
        std::string name;          // what was played: a named sound's name, or a file's (what stops it)
        std::string file;          // what it plays, as sounds/ names it
        float volume = 1.0f;       // (its type's in it)
        std::string type = "sound"; // its sound type (which of the player's volumes it follows)
    };

    void run();
    // The state to restore. `history`: rollback keeps that many history entries (else load: none).
    std::expected<std::string, std::string> restore(std::string_view data, std::optional<std::size_t> history);
    void remember(); // a rollback point at the line or choice now on screen
    [[nodiscard]] std::optional<std::size_t> momentOf(std::size_t entry) const; // in past_
    bool rewind(std::size_t moment);                                           // to past_[moment]
    void addHistory(HistoryEntry entry);                                       // keeping setting history_length
    void addToPage(); // the line just said
    void execute(const Command& command, std::size_t here);
    void sayLine(const cmd::Say& c, std::size_t here, bool fast);       // ...a line said
    void openMenu(const cmd::Menu& c, std::size_t here);                // ...a choice
    void play(const cmd::Play& c, bool fast);                           // ...a sound, as its type plays it (or stopped)
    void stopSound(const std::string& what, float fade);                // ...by its name, or all of a type
public:
    // A sound by its name where the game plays one of its own (the phone's, typing): as Script::soundOf has it, once,
    // `volume` times as loud.
    void playNamed(std::string_view name, float volume = 1.0f);
private:
    void sendMessage(const cmd::Message& c, std::size_t here);          // ...a message on the phone, or a change to one
    void startCall(const cmd::Call& c, std::size_t here);               // ...a call
    void passTime(const cmd::Time& c, std::size_t here);                // ...the story's time going on (or the player's waited for)
    // The story's time (setting time): its day (the first is 1) and minutes from midnight; with setting time real, the
    // player's (days since the story started). Kept as the variables time_day and time_minutes (time_started: real).
    struct When {
        int day = 1, minutes = 0;
    };
    [[nodiscard]] When timeNow() const;
    [[nodiscard]] std::optional<Value> timeValue(std::string_view name) const; // time_clock, time_part, time_date...
    [[nodiscard]] std::string partAt(int minutes) const;     // the part of the day then (setting time_parts)
    [[nodiscard]] std::string clockText(int minutes) const;  // as the game shows it: 18:30, 6:30 PM, or (days and parts) Evening
    [[nodiscard]] int dateOf(int day) const;                  // the calendar's date that day (days since 1970; -1: none)
    [[nodiscard]] int weekdayOn(int day) const;               // (0 Monday; -1: none)
    [[nodiscard]] std::string dateText(int day) const;       // as the game shows it: Saturday 14 July, Saturday, or Day 3
    void timeMoved(When from, When to, bool fast);           // the background as it is then, and a new day's calendar
    [[nodiscard]] std::string backgroundFor(std::string_view image) const; // its version for the part of the day (park_evening), if any
    When waitFrom_{}; // (a wait for the player's clock: when it started)
    // The scripts' animation called `name` (none: "", or one the scripts don't have).
    [[nodiscard]] std::shared_ptr<const Animation> animation(std::string_view name) const;
    void waitFor(Wait wait, std::size_t here);
    void fail(std::string message);
    void say(std::string speaker, SDL_Color colour, std::string_view text, const CharacterDef* def);
    void revealTo(std::size_t position);
    void updateLine(float dt, bool fast);
    void updateSpeech();
    [[nodiscard]] bool voiceAtPause() const; // the voice waits with its text at a {pause}
    // How open a mouth that moves as `moves` says is (or as the player's Mouths setting allows, if that's less).
    [[nodiscard]] float mouth(MouthMoves moves, float speech, bool voiced) const;
    [[nodiscard]] const Stop* pendingStop() const;
    [[nodiscard]] int currentSpeed() const;
    [[nodiscard]] const GameSettings& gameSettings() const { return script_.gameSettings; }
    void have(RecordKind kind, std::string_view name); // the player has it now (if the game lists it)
    void learnTips(std::string_view text);
    void waitForScreen(ScreenRequest request, std::size_t here);

    Context ctx_;
    const Script& script_;
    Lookup lookup_;
    const Variables* item_ = nullptr;
    Stage stage_;

    Wait wait_ = Wait::None;
    std::size_t pc_ = 0;
    std::size_t current_ = 0;  // instruction being executed (for error messages)
    std::size_t resumePc_ = 0; // where a save made right now should resume
    std::optional<std::size_t> stopAt_; // stopBefore()
    std::function<bool(std::size_t)> stopWhere_; // stopWhere()
    std::size_t watch_ = SIZE_MAX; // playTo(): its target, and whether it has been done yet
    bool watched_ = false;
    const std::function<bool(std::size_t)>* watchThere_ = nullptr; // (and where it stops: movies there play)
    std::uint64_t random_ = newRandomState(); // random()'s state: this game's own (its commands draw from it)
    std::uint64_t resumeRandom_ = 0; // random()'s state there (so the same numbers come again)
    std::vector<std::size_t> callStack_;
    Variables vars_;
    Variables carried_;
    std::string music_;
    // The flowchart: the last point passed, and the option picked since (at a choice).
    std::string flowchartNode_;
    int flowchartOption_ = -1;
    void passPoint(std::size_t here, Flowchart::Kind kind, std::vector<Flowchart::Option> options = {});
    float musicVolume_ = 1.0f;
    std::map<std::string, Loop, std::less<>> loops_; // looping sound channels (restored on load)
    std::map<std::string, std::string, std::less<>> typePlaying_; // a sound type played one at a time: what plays on it (stopped by its name)
    std::map<std::string, std::string, std::less<>> expressions_; // each character's latest expression
    std::vector<std::string> screens_;                // screens shown with "show screen"
    std::vector<PageLine> page_;                      // the lines said (a page part shows the newest that fit)
    // The phone, a call, a chat scene.
    PhoneData phone_;
    [[nodiscard]] bool hasSignal() const { return truthy(variable("phone_signal")); }
    std::optional<CallNow> call_;
    std::string chat_;                     // the chat a chat scene is in
    std::uint32_t messageCount_ = 0, arrivals_ = 0;
    std::uint32_t chatWait_ = 0;           // a chat scene waits for this message (its number) to come...
    bool replying_ = false;                 // a chat= scene's lines are running: messages come typed, one after another
    // A chat= or call= scene's lines are running while the call stack is this deep (runAside's frame). When they're
    // over (or wait at a choice on the phone), the story's back on its line as it was: shown again if they showed
    // something, its page as it was.
    struct Asked {
        std::size_t depth = 0, page = 0;
        bool shown = false;
        bool call = false;    // (a call scene's: the call's over at its end)
        bool passing = false; // (the player hung up or left the chat: its choices are passed)
        std::string chat;     // (a chat scene's, with this person: the phone's put away at its end, its messages come)
    };
    std::optional<Asked> asked_;
    struct Parked { // a chat= or call= scene's choice, waiting on the phone
        std::size_t menu = 0;
        std::string who;
        bool call = false;
    };
    std::optional<Parked> parked_;
    bool parking_ = false;
    std::string chatOpen_; // the player's chat open on the phone (its chat= scene played as it opened)
    std::string putAway_;  // a chat= scene over: the phone's put away a moment after this chat's messages have come
    float putAwayIn_ = 0.0f;
    void talk(std::size_t target, const std::string& chat, bool call, bool passing = false); // a chat= (with) or call= scene's lines, run aside
    void callOver();                                                            // the player's call ends
    std::optional<cmd::Message> change_;   // ...or to make this change (deletes it, edits it...) when `changeIn_` is over
    float changeIn_ = 0.0f;
    bool unlogged_ = false;  // an option picked in a chat is being sent: the history has it already (as a choice)
    std::vector<std::string> news_; // (see takeNews)
    struct Person {
        std::string name, character; // as shown, and a defined character's lower-case name
        SDL_Color colour{255, 255, 255, 255};
    };
    [[nodiscard]] Person person(const std::string& character, const std::string& sender) const; // the one a phone line names
    [[nodiscard]] Person personNamed(std::string_view name) const;                                // ...by the name the phone shows
    void addMessage(MessageItem m);
    void arrive(MessageItem& m);                                 // it comes to the phone: its tips, image and sound
    void change(const cmd::Message& c);                          // deletes it, edits it, reacts, reads it
    void hurry();                                                // the message a chat scene waits for comes now
    void logCall(const CallNow& c, bool missed);
    void endCall(bool missed); // (the call now, not answered: missed, or busy)
    void stopRinging();
    void runAside(std::size_t target); // a chat= or call= scene's lines
    void sayAgain(std::size_t page); // the line the story was on, back after a chat= or call= scene's lines
    void takeExpression(const cmd::Say& c, const CharacterDef* def); // a line's face for its speaker

    // Lines in other textboxes than the one on screen's, said together with it (a "together" block), each
    // typing at its own speed. Saves keep them as their text with its tags.
    struct BoxLine {
        std::string box, speaker, speakerKey, source, text, face;
        SDL_Color speakerColour{255, 255, 255, 255};
        std::vector<TextSpan> spans;
        float reveal = 0.0f;
        std::size_t length = 0;
    };
    std::vector<BoxLine> others_;
    bool together_ = false; // the lines of a together block are being said
    [[nodiscard]] std::string boxOf(const CharacterDef* def) const; // the textbox a line goes to
    [[nodiscard]] bool othersTyping() const;
    [[nodiscard]] BoxLine boxLine(std::string box, std::string speaker, SDL_Color colour, std::string source, const CharacterDef* def) const;
    void sayTogether(const cmd::Say& c, const CharacterDef* def); // a together block's line, before its last

    // The current line.
    std::string source_; // its text with values filled in and tags kept
    std::string speaker_;
    std::string speakerKey_; // the speaking character, if they're defined
    SDL_Color speakerColour_{255, 255, 255, 255};
    std::optional<SDL_Color> textColour_;
    std::string textbox_;
    std::string typingSound_;
    std::string face_, faceBase_, faceSide_;
    TextAppears revealMode_ = TextAppears::Letter;
    std::string text_;
    std::vector<TextSpan> spans_;
    std::vector<Stop> stops_;
    std::vector<Speed> speeds_;
    std::size_t passedStops_ = 0;
    bool noWait_ = false;
    std::size_t length_ = 0; // codepoints in text_
    bool lineRead_ = false;  // the line was seen before (in any playthrough)
    bool voiced_ = false;                // the line on screen plays a voice (its own, its speaker's voice file or made-up speech)
    bool madeUpVoice_ = false;           // ...made-up speech (it stands for the typing: shown at once by a click, it stops)
    bool textFollowsVoice_ = false;      // ...and its text shows as much as has been said
    bool typing_ = false;                // letters are appearing
    float speech_ = 0.0f;                // how much the speaker is speaking (0..1): mouths follow it
    float faceSpeech_ = 0.0f;            // the same for a face with a mouth of its own (face_copies without mouth)
    float clock_ = 0.0f;                 // game time (blinking)
    float lineTime_ = 0.0f;              // time since the line started
    float reveal_ = 0.0f;
    float holdTimer_ = 0.0f; // timed {pause ...} in progress
    float autoTimer_ = 0.0f;
    float skipTimer_ = 0.0f;
    float typingSoundTimer_ = 0.0f;
    float timer_ = 0.0f;

    std::vector<Caption> captions_; // (see captions())
    struct Subtitle {
        double from = 0.0, to = 0.0; // seconds into the movie
        std::string text;
    };
    std::vector<Subtitle> subtitles_; // the background movie's
    [[nodiscard]] static std::vector<Subtitle> subtitlesOf(const Assets& assets, std::string_view movie);
    void caption(std::string_view name, std::string_view file, bool music);
    [[nodiscard]] bool passesMovies(bool fast) const; // (skipping, the player letting it, or the developer tools playing to a line)
    std::vector<ChoiceItem> choiceItems_;
    [[nodiscard]] const cmd::Menu* menu() const; // the choice on screen (its command: a save made now repeats it)
    [[nodiscard]] std::optional<ChoiceItem> offer(const cmd::Choice& choice, int option) const; // shown now, if it is
    void listChoices(const cmd::Menu& menu); // the options to show now
    void letPass();                          // none of them picked: the story goes on after the choice
    bool menuPrompt_ = false;                // the line before it stays on screen meanwhile (it asks it)
    std::size_t saidAt_ = SIZE_MAX;          // the line said last (a choice's question: saved with it, said again on loading)
    bool choiceAgain_ = false;               // a repeating choice up again after an option's lines (without the line that asked it)
    float menuTime_ = 0.0f;                  // seconds left, for a timed one
    // A choice up while the story goes on (for N lines): its command and the lines it has left. A
    // picked option with return=no passes the rest of them unseen once its own lines are done
    // (when the call stack is back below `depth`).
    struct Trigger {
        std::size_t at = 0;
        int lines = 0;
    };
    std::optional<Trigger> trigger_;
    struct Pass {
        std::size_t depth = 0;
        int lines = 0;
    };
    std::optional<Pass> pass_;
    int hiddenLines_ = 0; // lines to go past unseen

    ScreenRequest request_;
    bool screenRequested_ = false;
    std::vector<cmd::Ui> requests_;
    bool persistentChanged_ = false;

    std::vector<HistoryEntry> history_;
    std::vector<std::size_t> ran_; // (see takeRan)
    std::size_t historyDropped_ = 0; // old entries let go (Moment::history counts them)
    struct Moment {
        std::string state;     // save() then
        std::size_t history;   // history entries before it
        bool stop;             // rollback stops here ({no_wait} lines go by)
        Variables persistent;  // the persistent variables then
    };
    std::vector<Moment> past_; // the last setting rollback_lines lines and choices, the newest last
    bool skip_ = false;
    bool auto_ = false;
    bool uiHidden_ = false;
    bool autosave_ = false;
    std::string error_;
};

// What the story's files (Story.cpp, GameTime.cpp, Phone.cpp, Saves.cpp...) share: the player's clock (their date, days since 1970;
// minutes from midnight; seconds since 1970), and the most commands a step of the story runs without showing
// anything (a jump going round in a circle stops there).
struct RealNow {
    int date = 0, minutes = 0;
    double seconds = 0.0;
};
[[nodiscard]] RealNow realNow();
inline constexpr int kInstructionBudget = 100000;

inline constexpr SDL_Color kOwnColour{230, 230, 235, 255}; // (the phone owner's own messages)

} // namespace vn
