#pragma once

// The player: the window, the game's screens and menus, saving, the player's settings, and the
// developer tools around them.

#include "Assets.hpp"
#include "Developer.hpp"
#include "Game.hpp"
#include "Screens.hpp"
#include "Script.hpp"
#include "System.hpp"

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <vector>

namespace vn {

inline constexpr const char* kVersion = VN_VERSION; // the player's (CMakeLists.txt)

// The licenses of the software built into the player (Licenses.cpp).
[[nodiscard]] std::string_view licenses();

class DeveloperUi;

// The player: owns the window, renderer, audio, fonts and saves, and runs the game's script.
// Every screen (title, textbox, menus, settings, save/load...) is defined by the script; the
// engine shows them and carries out what their buttons do.
class Engine final {
    friend class Developer; // (the developer tools: they change the player's insides; what they only read, they read below)

public:
    Engine() = default;
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    SDL_AppResult init(int argc, char** argv);
    SDL_AppResult event(SDL_Event& e);
    SDL_AppResult iterate();

    // For screens.
    Font& font(const std::string& name, float size) { return fonts_.get(name, size); }
    [[nodiscard]] const Canvas& canvas() const { return canvas_; } // the game's size: everything is laid out in it
    [[nodiscard]] const GameFiles& files() const { return files_; } // the game's files: its folder's, or its pack's
    // Where the log is written, and the window: what the player is shown if the game has to stop.
    [[nodiscard]] const std::string& logFile() const { return logFile_; }
    [[nodiscard]] SDL_Window* window() const { return window_.get(); }
    Picture picture(std::string_view name, Frame frame = {}) { return assets_->picture(name, frame); }
    void run(const Commands& commands, const std::string& confirm = {}); // what a button does
    // What a screen does as it opens (or closes): the music and the voice line stop when its stop_music_on_open=
    // and stop_voice_on_open= (or _on_close=) say so, then its on_show= (on_hide=) runs.
    void screenHook(const ScreenDef& def, bool opening);
    [[nodiscard]] Game& game() { return *game_; }
    [[nodiscard]] const Lookup& lookup() const { return game_->lookup(); }
    void wantTextInput() { textInputWanted_ = true; }
    void playSound(const std::string& file) { audio_.playSound(assets_->audio(file)); }
    [[nodiscard]] const GameConfig& config() const { return script_.config; }
    [[nodiscard]] const Palette& palette() const { return script_.palette; }
    [[nodiscard]] Assets& assets() { return *assets_; }
    [[nodiscard]] Audio& audio() { return audio_; }
    [[nodiscard]] std::uint64_t frameNumber() const { return frameNumber_; }
    [[nodiscard]] float time() const { return time_; } // the game's clock: it stands still while the developer tools are open over it
    [[nodiscard]] float toolsTime() const { return toolsTime_; }
    [[nodiscard]] Settings& settings() { return settings_; }
    void settingsChanged(bool testSound);
    [[nodiscard]] SlotBrowser& slots() { return slots_; }
    // A save slot clicked (the questions, if any, before saving over a save and before loading while a
    // game is on) and its delete button (the question before deleting); a place on the flowchart clicked.
    void slotPressed(std::size_t index, std::string overwriteQuestion, std::string loadQuestion);
    void slotDelete(std::size_t index, std::string question);
    // The history's buttons: hear entry `entry`'s voice again; go back to its line (closing the menus).
    void historyVoice(std::size_t entry);
    void historyBack(std::size_t entry);
    // The phone part: sends an answer to a message (the menus close when the story moves on).
    void reply(std::size_t message, std::size_t option);
    // Something dragged (its drag=) dropped on a part: its drop= runs, with {dropped} what it was.
    void drop(const std::string& what, const Commands& commands);
    // The music room: plays song `index` (one the player has heard); what was playing comes back
    // when the screen with the songs closes.
    void playSong(std::size_t index);
    // The flowchart: its places, and going back to one (asking first during a game).
    [[nodiscard]] const Chart& chart() const { return chart_; }
    [[nodiscard]] bool playing() const; // a game is on (not the title screen)
    void goToPlace(const std::string& id, std::string question);
    // Where a place is in the scripts (its command), if it's still there.
    [[nodiscard]] std::optional<std::size_t> placeCommand(std::string_view id) const;
    [[nodiscard]] const Script& script() const { return script_; }
    // The gallery's pictures, endings, songs, achievements and tips: whether the player has the
    // index-th of a kind, and how many they have.
    [[nodiscard]] bool has(RecordKind kind, std::size_t index) const;
    [[nodiscard]] int count(RecordKind kind) const;
    void viewCg(std::size_t index) { cg_ = index; } // the gallery picture being looked at ({cg})
    [[nodiscard]] bool galleryMovie(std::size_t index) const; // the index-th in the gallery is a video (movies/NAME.mpg)
    // The tip being read ({tip}); a tip the player has but hasn't read yet is new.
    void viewTip(std::size_t index);
    [[nodiscard]] bool newTip(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> tip() const { return tip_; }
    // The person whose messages the phone shows ({contact}; none: everyone's).
    void viewContact(std::string name) { contact_ = std::move(name); }
    [[nodiscard]] const std::string& contact() const { return contact_; }
    // Languages: the game's own first, then its translations (the "language" setting).
    [[nodiscard]] std::vector<std::string> languageNames() const;
    [[nodiscard]] int languageIndex() const;
    [[nodiscard]] std::string languageId(int index) const; // as Settings::language has it
    // The language a word names, as Settings::language has it: "original" for the game's own (by its name,
    // or "original"), a translation's id for its file, name or code; none for a word that's no language.
    [[nodiscard]] std::optional<std::string> languageIdNamed(std::string_view word) const;
    void changeSetting(Settings& s, std::string_view what); // a button's "setting NAME VALUE"
    // The Controls screen: the key in a control's slot, and changing it (the next key pressed).
    [[nodiscard]] std::string keyLabel(std::string_view control, std::size_t slot) const;
    static constexpr std::size_t kPadSlot = 99; // a control's controller button (slot=pad)
    [[nodiscard]] static std::size_t slotOf(const Element& el); // slot=1, 2... or pad
    [[nodiscard]] static std::vector<std::size_t> keySlots(const std::vector<std::string>& names);
    void startRebind(const std::string& control, std::size_t slot) {
        if (!trying_) rebinding_ = std::pair{control, slot};
    }
    [[nodiscard]] bool rebinding(std::string_view control, std::size_t slot) const {
        return rebinding_ && rebinding_->first == control && rebinding_->second == slot;
    }

    // What the developer tools read (what they change, they reach as a friend): what the player shows (the
    // title screen, a game, or an error), the game folder (or pack, with a slash after it) and whether
    // there is a game, the player's own data folder, the renderer, the lines read and the persistent
    // variables.
    enum class Mode : std::uint8_t { Title, Game, Error };
    [[nodiscard]] Mode mode() const { return mode_; }
    [[nodiscard]] const std::string& gameRoot() const { return gameRoot_; }
    [[nodiscard]] bool noGame() const { return noGame_; }
    [[nodiscard]] const std::string& prefPath() const { return prefPath_; }
    [[nodiscard]] SDL_Renderer* renderer() const { return renderer_.get(); }
    [[nodiscard]] const ReadLog& readLog() const { return readLog_; }
    [[nodiscard]] const Variables& persistent() const { return persistent_; }

private:

    // Shuts SDL down after every other member (declared first => destroyed last).
    struct Session {
        bool ttf = false;
        Session() = default;
        Session(const Session&) VN_DELETED("one: it shuts SDL down");
        Session& operator=(const Session&) VN_DELETED("one: it shuts SDL down");
        Session(Session&&) VN_DELETED("one: it shuts SDL down");
        Session& operator=(Session&&) VN_DELETED("one: it shuts SDL down");
        ~Session();
    };
    // SDL's log: also written to <player data>/log.txt (GUI builds have no console), and the newest
    // messages kept for the developer tools.
    struct Log {
        struct Entry {
            SDL_LogPriority priority = SDL_LOG_PRIORITY_INFO;
            std::string text;
        };
        std::mutex mutex; // (sound can log from its own thread)
        std::deque<Entry> recent; // the newest last
        IOStreamPtr io;
        SDL_LogOutputFunction previous = nullptr;
        void* previousData = nullptr;
        bool started = false;
        void start();                       // keeps messages from now on
        void open(const std::string& path); // and writes them to this file too (the ones so far first)
        [[nodiscard]] std::vector<Entry> newest(std::size_t count, std::size_t skip);
        [[nodiscard]] std::size_t size();
        Log() = default;
        Log(const Log&) VN_DELETED("one: SDL's log goes to it");
        Log& operator=(const Log&) VN_DELETED("one: SDL's log goes to it");
        Log(Log&&) VN_DELETED("one: SDL's log goes to it");
        Log& operator=(Log&&) VN_DELETED("one: SDL's log goes to it");
        ~Log(); // (SDL's log goes where it went before)
    };

    // Setup
    [[nodiscard]] std::vector<std::string> gamePlaces() const;
    [[nodiscard]] std::string findGame(const std::string& requested) const;
    std::string openGame();
    void loadPlayerData();
    void begin();
    void play(); // the game from the beginning: the splash screen, then label start
    bool lookForGame(); // developer mode without a game: one found next to the player begins (false: still none)
    [[nodiscard]] Script readScript() const;
    // Packs the game folder into `file` (game.vnext next to the folder): what was done, or why not.
    [[nodiscard]] std::expected<std::string, std::string> pack(const std::string& file) const;
    [[nodiscard]] std::string packPath() const;
    // The finished game, ready to share, next to the game folder (Pack the game, --pack): what was
    // made, or why not.
    [[nodiscard]] std::expected<std::string, std::string> finishGame() const;
    [[nodiscard]] bool writeVoiceList() const;
    [[nodiscard]] bool writeTranslation(const std::string& id);
    void loadTranslations(); // translations/*.vnscript (their errors join the script's)
    [[nodiscard]] const Translation* language() const; // the one shown, if any
    void useLanguage();      // its texts, fonts, pictures and sounds
    void configureFonts();
    void configure(); // applies the script: canvas, fonts, screens...
    [[nodiscard]] std::unique_ptr<Game> makeGame();
    [[nodiscard]] std::optional<Value> builtin(std::string_view name) const;
    void updatePixelScale();
    void refreshWindowSizes();
    // Controls: the keys in effect (the player's choice, else the script's), as names and parsed.
    [[nodiscard]] std::vector<std::string> keyNames(const Control& control) const;
    void refreshKeys();
    bool captureKey(const SDL_Event& e);
    // Controllers and touch screens.
    SDL_AppResult touch(const SDL_Event& e);
    SDL_AppResult padAxis(const SDL_Event& e);
    SDL_AppResult gesture(KeyChord::Gesture g);
    void setKey(const std::string& name);
    [[nodiscard]] std::string keyLabel(const KeyChord& key) const;

    // Flow
    void showErrors(const std::vector<std::string>& lines);
    void leaveStory(bool keepSound = false); // the story stops (if one is going on), with its sounds and screens
    void openTitle(bool keepSound = false);
    void quit(); // (in developer mode, back to the developer tools)
    void newGame(std::string_view label, bool subroutine = false);
    void startAt(std::size_t pc, bool subroutine = false, const Variables* carried = nullptr); // (`carried`: the variables it begins with)
    void leaveTitle();
    void beginStory(); // label start, as the game begins
    void newStory();   // New Game: on from where the story opened the title screen (or label start)
    void fromTitle(std::function<void()> then); // the story goes on from the title screen: through its closing fade, if it has one
    [[nodiscard]] std::optional<std::size_t> titleLine() const; // the line in label start that opens the title screen
    [[nodiscard]] ScreenView* view(std::string_view name);
    [[nodiscard]] ScreenView* topView(); // the screen on top: the top overlay, else the title on the title screen
    [[nodiscard]] ScreenView* textbox();
    bool open(const std::string& name);
    bool openNow(const std::string& name); // (open(), once the screen under it has faded out, when it must first)
    // The top screen: its hide_animation= first and its "and close with fade" (unless `fade` is false),
    // then it's gone (and the story goes on, if it waited for it).
    void close(bool fade = true);
    void closeFaded(); // (after its hide_animation=)
    void closeNow();
    void left(const std::string& name); // a "show screen" one hidden: its hide_animation= plays (the story goes on)
    void closeAll();
    void syncShownScreens();
    void command(const Command& c);
    void ui(const cmd::Ui& c);
    void ask(std::string question, std::function<void()> yes);
    void loadPersistent();
    void savePersistent();
    void quickSave();
    void quickLoad();
    void autosave();
    bool saveToSlot(const std::string& slot);
    bool loadFromSlot(const std::string& slot);
    std::expected<std::string, std::string> resume(std::string_view state);
    [[nodiscard]] SurfacePtr captureThumbnail();
    void reload();
    void notify(std::string message);
    void readAloud(); // (the player's setting: what's new on screen, once)

    // Display
    void applySettings();
    void saveSettings();
    void toggleFullscreen();
    void screenshot(const std::string& path);
    void drawGame(bool thumbnail);
    void drawErrors();
    [[nodiscard]] std::string windowTitle() const;

    // The developer tools
    [[nodiscard]] bool developerMode() const { return noGame_ || config().developer; }

    Session session_;
    Log log_;
    Developer developer_{*this};
    WindowPtr window_;
    RendererPtr renderer_;
    Canvas canvas_; // (config resolution)
    GameFiles files_; // (the game's files: before what reads through them, so they go after it)
    Audio audio_{files_};
    Settings defaults_, settings_, applied_; // applied_: what is in effect, to apply only changes
    ReadLog readLog_;
    std::set<std::string> reached_; // "FILE\tLINE TEXT" for each line run (the editor marks the others)
    std::size_t reachedGeneration_ = 0; // (one more each time reached_ changes: the lists mark their lines again)
    std::unique_ptr<Assets> assets_;
    CursorPtr cursor_;
    std::vector<GamepadPtr> pads_;
    std::array<int, SDL_GAMEPAD_AXIS_COUNT> axes_{}; // each stick / trigger: -1, 0 or 1 (pushed that way)
    struct Touch {
        SDL_FingerID finger = 0;
        bool down = false, moved = false, second = false, longPressed = false;
        float x = 0, y = 0, startX = 0, startY = 0, scrolledY = 0; // window points
        Uint64 start = 0;
    } touch_;
    FontCache fonts_{files_};
    SaveStore saves_;
    SlotBrowser slots_;
    std::optional<std::string> musicBeforeSongs_; // what played before the music room (a path; empty: nothing)
    std::deque<std::string> noticeQueue_;         // messages waiting for the one showing to go
    std::string topScreen_;                       // the screen on top, and since when (close_after=)
    float topSince_ = 0.0f;
    [[nodiscard]] bool songsOpen() const;         // a screen with a songs part is open
    Script script_;
    std::vector<Translation> translations_;
    std::uint64_t frameNumber_ = 0;
    bool covered_ = false; // a menu covers the game (its voice and video wait)
    std::unique_ptr<Game> game_;                          // idle (not started) on the title screen
    std::map<std::string, ScreenView, std::less<>> views_; // every screen of the script
    const Script* viewsFrom_ = nullptr; // (the preview: view() gives the editor's screens, as they are now)
    std::vector<std::string> overlays_;                   // open menus, the top one last
    std::vector<std::string> shown_;                      // "show screen" screens last frame (on_show / on_hide)
    std::size_t calledDepth_ = 0;                         // overlays_ size while a screen the story waits for is open
    std::map<std::string, Transition, std::less<>> closingFades_; // open screens that go with a fade ("and close with fade")
    std::optional<Transition> titleClosing_;              // ...the title screen's, when the story opened it so
    struct Leaving {
        std::string name;
        std::function<void()> then; // (once its animation is over; none: a "show screen" one, drawn on top until then)
    };
    std::vector<Leaving> leaving_; // screens playing their hide_animation=
    bool coveredClose_ = false;    // (the top screen's hide_animation= ended covered: what was under it comes back into view)
    // "open NAME" for a screen open under others, or for the title screen under menus: back to it. The
    // ones over it close, the top one first, each as it does (with its hide_animation= and what it does
    // on closing), until it's on top; then it opens again (what it does on opening: its music...).
    std::optional<std::string> backTo_;
    void backTo(const std::string& name);
    void closeTowards(); // (the next one over it, once the top has closed)
    std::optional<Value> answer_;                         // what the screen the story waits for answers
    Variables persistent_;                                // "persistent.*", kept in persistent.txt
    Chart chart_;                                         // the flowchart, kept in chart.txt
    std::vector<std::vector<KeyChord>> controlKeys_;      // keys in effect, one list per script_.controls
    std::optional<std::pair<std::string, std::size_t>> rebinding_; // control + slot waiting for a key
    std::size_t cg_ = 0;                                  // gallery picture being looked at
    mutable std::map<std::string, bool, std::less<>> movies_; // gallery names that are videos (looked up once)
    std::optional<std::size_t> tip_;                      // tip being read
    std::string contact_;                                 // conversation on the phone
    bool dataFolder_ = false;                             // --data: everything, saves too, goes there
    bool packed_ = false;                                 // the game is a pack (game.vnext): finished
    bool noGame_ = false;                                 // there's no game: just developer mode
    bool storyStarted_ = false;                           // without a title screen, the story's end closes the player
    bool waiting_ = false;                                // developer mode: no game is going on, the developer tools wait for Play
    bool textInputWanted_ = false, textInputActive_ = false;
    bool splash_ = false;                                 // running "label splashscreen"
    bool opening_ = false;                                // label start before it opens the title screen (nothing's saved then)
    bool titleShown_ = false;                             // (its on_hide runs when a game starts)
    // A screen opened with fade: to each fade's colour in half its time (from the one before), the
    // screen opens (`then`), then away from the last one's colour in the other half of its time.
    // Clicks and keys wait meanwhile.
    struct Veil {
        std::vector<std::pair<SDL_Color, float>> fades; // colour, seconds
        float time = 0.0f;
        std::function<void()> then;
        [[nodiscard]] float opensAt() const;
        [[nodiscard]] float length() const { return fades.empty() ? 0.0f : opensAt() + fades.back().second / 2; }
        [[nodiscard]] SDL_Color color() const; // over everything, now
    };
    std::optional<Veil> veil_;
    void veiled(const Transition& t, std::function<void()> then); // `then` at once, or behind a fade
    std::optional<std::size_t> storyStart_;               // where New Game starts: the line after the one that opened the title screen
    Variables beforeTitle_; // what the story set before it opened the title screen (a language picked...): the title screen and New Game keep it
    // While the Screens tab's screen is tried: its buttons do nothing (no saving, loading or leaving),
    // and what they would have done is kept here instead.
    bool trying_ = false;
    std::vector<Command> tried_;
    std::string spoken_; // what was read aloud last

    Mode mode_ = Mode::Title;
    std::string question_;
    std::function<void()> confirmYes_;
    std::string notice_;
    float noticeTime_ = 0.0f;
    std::vector<SDL_Point> windowSizes_;
    bool hasSave_ = false;
    bool thumbnail_ = false;
    bool reloadRequested_ = false;
    bool clickThrough_ = false; // (the developer tools are open: a click that brings the window forward clicks too)
    TextBlock errorTitle_, errorText_, errorHint_;

    std::string gameRoot_; // the game folder, or the pack, with a slash after it
    std::string startLabel_;
    std::string screenshotPath_;   // --screenshot: save one after 30 frames and quit
    std::string screenshotRequest_; // the screenshot command: saved at the end of the next frame
    std::string prefPath_;
    std::string logFile_; // (<player data>/log.txt)
    int frames_ = 0;
    Uint64 lastTicks_ = 0;
    float time_ = 0.0f, toolsTime_ = 0.0f;
    bool paused_ = false; // the developer tools are open over a game: it stands still
    bool quit_ = false;
};

} // namespace vn
