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
#include <set>
#include <string>
#include <vector>

namespace vn {

inline constexpr const char* kVersion = VN_VERSION; // the player's (CMakeLists.txt)

// The licences of the software built into the player (Licences.cpp).
[[nodiscard]] std::string_view licences();

class DeveloperUi;

// The player: owns the window, renderer, sound, fonts and saves, and runs the game's script.
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
    Image image(std::string_view name, Frame frame = {});
    // What a button does (`item`: a list item's values when it's in one: {chat.name}, and what send acts on).
    void run(const Commands& commands, const std::string& confirm = {}, const Variables& item = {});
    // What a screen does as it opens (or closes): the music and the voice line stop when its stop_music_on_open=
    // and stop_voice_on_open= (or _on_close=) say so, then its action_on_open= (action_on_close=) runs.
    void screenHook(const ScreenDef& def, bool opening);
    [[nodiscard]] Game& game() { return *game_; }
    [[nodiscard]] const Lookup& lookup() const { return game_->lookup(); }
    void wantTextInput() { textInputWanted_ = true; }
    void playSound(const std::string& file) { sound_.playSound(assets_->sound(file)); }
    [[nodiscard]] const GameSettings& gameSettings() const { return script_.gameSettings; }
    [[nodiscard]] const Palette& palette() const { return script_.palette; }
    [[nodiscard]] Assets& assets() { return *assets_; }
    [[nodiscard]] Sound& sound() { return sound_; }
    [[nodiscard]] std::uint64_t frameNumber() const { return frameNumber_; }
    [[nodiscard]] std::uint64_t pointerMoves() const { return pointerMoves_; } // (how many times the pointer has moved: screens tell whether the last move came to them)
    [[nodiscard]] bool padInUse() const { return padInUse_; } // the last thing pressed was a controller's (an input's keyboard comes up by itself)
    [[nodiscard]] float time() const { return time_; } // the game's clock: it stands still while the developer tools are open over it
    [[nodiscard]] float toolsTime() const { return toolsTime_; }
    [[nodiscard]] Settings& settings() { return settings_; }
    void settingsChanged(bool testSound);
    [[nodiscard]] SlotBrowser& slots() { return slots_; }
    // A save slot clicked (the questions, if any, before saving over a save and before loading while a
    // game is on) and its delete button (the question before deleting).
    void slotPressed(std::size_t index, std::string overwriteQuestion, std::string loadQuestion);
    void slotDelete(std::size_t index, std::string question);
    // The history's buttons: hear entry `entry`'s voice again; go back to its line (closing the menus).
    void historyVoice(std::size_t entry);
    void historyBack(std::size_t entry);
    // Something dragged (its drag=) dropped on a part: its drop= runs, with {dropped} what it was.
    void drop(const std::string& what, const Commands& commands);
    // The music room: plays song `index` (one the player has heard); what was playing comes back
    // when the screen with the songs closes.
    void playSong(std::size_t index);
    // The flowchart: its points, and going back to one (asking first during a game).
    [[nodiscard]] const Flowchart& flowchart() const { return flowchart_; }
    [[nodiscard]] bool playing() const; // a game is on (not the title screen)
    void goToPlace(const std::string& id, std::string question);
    // Where a place is in the scripts (its command), if it's still there.
    [[nodiscard]] std::optional<std::size_t> pointCommand(std::string_view id) const;
    [[nodiscard]] const Script& script() const { return script_; }
    // The gallery's images, endings, songs, achievements and tips: whether the player has the
    // index-th of a kind, and how many they have.
    [[nodiscard]] bool has(RecordKind kind, std::size_t index) const;
    [[nodiscard]] int count(RecordKind kind) const;
    void viewGalleryImage(std::size_t index) { galleryImage_ = index; } // the gallery image being looked at ({gallery_image})
    [[nodiscard]] bool galleryMovie(std::size_t index) const; // the index-th in the gallery is a movie (movies/NAME.mpg)
    // The tip being read ({tip}); a tip the player has but hasn't read yet is new.
    void viewTip(std::size_t index);
    [[nodiscard]] bool newTip(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> tip() const { return tip_; }
    // The phone screen: the app open (its first while none is; its call or lock app while those are up),
    // the chat open in it ({open_chat}; none: the list of chats), the image open (one from a chat), and
    // whether its lock screen is up. Opening one of each, and Back: the image, the chat, the app (to the
    // one before, or home), then the phone itself. A chat scene or a call keeps it where it is.
    [[nodiscard]] std::string phoneApp(const ScreenDef& def) const;
    [[nodiscard]] bool appOn(const Element& app) const; // the phone has it (while its when= is true)
    [[nodiscard]] std::string passcode() const;          // the phone's (the variable phone_passcode; none: "")
    [[nodiscard]] const std::string& openChat() const { return phone_.chat; }
    [[nodiscard]] const std::string& openImage() const { return phone_.image; }
    void openApp(const ScreenDef& def, std::string name);
    void openImage(std::string name) { phone_.image = std::move(name); }
    void back();
    bool phoneBack(); // a step back on the phone (the image, the chat, the app); false: none to take (Back then closes it)
    bool unlockPhone(std::string_view code); // its lock screen goes (with the passcode, if it has one: right or not)
    // Controllers rumble (unless the player's Vibration setting is off): `strength` from 0 to 1, for `ms`.
    void rumble(float strength, Uint32 ms);
    [[nodiscard]] std::string appHolding(const ScreenDef& def, std::string_view list) const; // the app with a list= of that in it (none: "")
    void openChat(const ScreenDef& def, std::string with); // someone's chat, in the app it's in
    // What the phone's parts do (a sample of the developer tools' does nothing): call someone, pick an option
    // waiting on it, answer (or decline) a call, press the lock screen's keypad.
    void callPerson(const std::string& who);
    void pickOnPhone(std::size_t option); // an option of a chat or call scene's choice waiting on the phone
    void answerCall(bool answer);
    void keypad(std::string_view key); // the lock screen's keypad: a digit (once there are enough, they're tried), or back
    // Languages: the game's own first, then its translations (the "language" setting).
    [[nodiscard]] std::vector<std::string> languageNames() const;
    [[nodiscard]] int languageIndex() const;
    [[nodiscard]] std::string languageId(int index) const; // as Settings::language has it
    // The language a word names, as Settings::language has it: "original" for the game's own (by its name,
    // or "original"), a translation's id for its file, name or code; none for a word that's no language.
    [[nodiscard]] std::optional<std::string> languageIdNamed(std::string_view word) const;
    void changeSetting(std::string_view what); // a button's "setting NAME VALUE"
    // A control part: the key in a control's slot, and changing it (the next key pressed).
    [[nodiscard]] std::string keyLabel(std::string_view control, std::size_t slot) const;
    static constexpr std::size_t kPadSlot = 99; // a control's controller button (slot=pad)
    [[nodiscard]] static std::size_t slotOf(const Element& el); // slot=1, 2... or pad
    [[nodiscard]] static std::vector<std::size_t> keySlots(const std::vector<std::string>& names);
    void startRebind(const std::string& control, std::size_t slot) {
        if (!sampling_) rebinding_ = std::pair{control, slot};
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
    // SDL's log: also written to <player data>/log.txt (GUI builds have no console); the messages from before
    // the file is open are kept for it till then.
    struct Log {
        struct Entry {
            SDL_LogPriority priority = SDL_LOG_PRIORITY_INFO;
            std::string text;
        };
        std::mutex mutex; // (sound can log from its own thread)
        std::deque<Entry> early; // the newest last
        IOStreamPtr io;
        SDL_LogOutputFunction previous = nullptr;
        void* previousData = nullptr;
        bool started = false;
        void start();                       // keeps messages from now on
        void open(const std::string& path); // and writes them to this file (the ones so far first)
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
    void play(); // the game from the beginning: scene start
    bool lookForGame(); // developer mode without a game: one found next to the player begins (false: still none)
    [[nodiscard]] Script readScript() const;
    // Packs the game folder into `file` (game.vnext next to the folder): what was done, or why not.
    [[nodiscard]] std::expected<std::string, std::string> pack(const std::string& file) const;
    [[nodiscard]] std::expected<std::string, std::string> packInto(const std::string& file) const; // (what pack() and finishGame() share)
    [[nodiscard]] std::string packPath() const;
    // Pack (and --pack): game.vnext and licences.txt in a folder named after the game, next to
    // the game folder (finishedFolder()): what was made, or why not.
    [[nodiscard]] std::expected<std::string, std::string> finishGame() const;
    [[nodiscard]] std::string finishedFolder() const;
    [[nodiscard]] bool writeVoiceList() const;
    [[nodiscard]] bool writeTranslation(const std::string& id);
    void loadTranslations(); // translations/*.vnscript (their errors join the script's)
    [[nodiscard]] const Translation* language() const; // the one shown, if any
    void useLanguage();      // its texts, fonts, images and sounds
    void configureFonts();
    void configure(); // applies the script: canvas, fonts, screens...
    [[nodiscard]] std::unique_ptr<Game> makeGame();
    [[nodiscard]] std::optional<Value> builtin(std::string_view name) const;
    void updatePixelScale();
    // The window as the game draws on it (its size, shown as Settings scales it) or as the developer tools
    // do (the window's own size: they lay themselves out in it). Clicks are read in the view they're for.
    void useView(bool tools);
    void applyGameView();
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
    void newGame(std::string_view label);
    void startAt(std::size_t pc, const Variables* carried = nullptr); // (`carried`: the variables it begins with)
    void leaveTitle();
    void beginStory(); // scene start, as the game begins
    void newStory();   // New Game: on from where the story opened the title screen (or scene start)
    void fromTitle(std::function<void()> then); // the story goes on from the title screen: through its closing fade, if it has one
    [[nodiscard]] std::optional<std::size_t> titleLine() const; // the line in scene start that opens the title screen
    [[nodiscard]] ScreenView* view(std::string_view name);
    [[nodiscard]] bool showsPhone(std::string_view screen); // it has the phone's parts: apps, chats, calls...
    void showPhone(const cmd::Ui& c);                      // the story's: a chat scene, a call or a message shown on it
    void hidePhone();                                      // ...over: it goes, if the story opened it
    [[nodiscard]] ScreenView* topView(); // the screen on top: the top overlay, else the title on the title screen
    // The screens a control's move, press and scroll actions work on, and what those do there. idle(): actions that
    // would do nothing now (each moves, presses or scrolls, with nothing there for it), so their key goes on.
    [[nodiscard]] std::vector<ScreenView*> keyViews();
    void moveKeys(int dx, int dy);
    [[nodiscard]] bool idle(const Commands& actions);
    [[nodiscard]] ScreenView* textbox();
    // A screen opened over the rest, playing `opening` and later `closing` this time (with NAME and close
    // with NAME; none: its own animation_on_open= and animation_on_close=).
    bool open(const std::string& name, ScreenView::Played opening = std::nullopt, ScreenView::Played closing = std::nullopt);
    bool openNow(const std::string& name, ScreenView::Played opening = std::nullopt,
                 ScreenView::Played closing = std::nullopt); // (open(), once the screen under it has faded out, when it must first)
    // The top screen: its hide animation first (unless `fade` is false), then it's gone (and the story
    // goes on, if it waited for it).
    void close(bool fade = true);
    void closeNow();
    void left(const std::string& name); // a "show screen" one hidden: its animation_on_close= plays (the story goes on)
    void closeAll();
    void syncShownScreens();
    void command(const Command& c);
    void ui(const cmd::Ui& c);
    void ask(std::string question, std::function<void()> yes);
    void loadPersistent();
    void savePersistent();
    // The player's progress (persistent.txt, read.txt, flowchart.txt): in game.vnext with the saves when the
    // game is a pack that keeps them (inPack_), else in the player's own folder. Without one, a write
    // keeps nothing, and that's no failure.
    [[nodiscard]] std::optional<std::string> readProgress(std::string_view file) const;
    bool writeProgress(std::string_view file, std::string_view text);
    void quickSave();
    void quickLoad();
    void autosave();
    void countSaves(); // whether there are saves (has_save), and quick saves (has_quick_save)
    bool saveToSlot(const std::string& slot);
    bool loadFromSlot(const std::string& slot);
    std::expected<std::string, std::string> resume(std::string_view state);
    [[nodiscard]] SurfacePtr captureThumbnail();
    void reload();
    void readAloud(); // (the player's setting: what's new on screen, once)

    // Display
    void applySettings();
    void saveSettings();
    void toggleFullscreen();
    void screenshot(const std::string& path);
    void drawGame(bool thumbnail); // (thumbnail: a save's picture, with what's open over the story that lets it go on, like the phone, but no notices)
    [[nodiscard]] bool stopsStory(const std::string& screen) const; // a menu: the story waits while it's open (not story=goes_on)
    void drawErrors();
    [[nodiscard]] std::string windowTitle() const;
    void showTitle(); // (when it's changed)

    // The developer tools
    [[nodiscard]] bool developerMode() const { return noGame_ || gameSettings().developer; }

    Session session_;
    Log log_;
    Developer developer_{*this};
    WindowPtr window_;
    RendererPtr renderer_;
    Canvas canvas_; // (setting game_size)
    GameFiles files_; // (the game's files: before what reads through them, so they go after it)
    Sound sound_{files_};
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
    FontCache toolsFonts_;      // the developer tools' (the computer's own font, as sharp as the window's pixels)
    bool toolsView_ = false;    // the window is laid out for the developer tools now (else for the game)
    SDL_Point toolsViewSize_{}; // (the window's size it was laid out for)
    SaveStore saves_;
    SlotBrowser slots_;
    std::optional<std::string> musicBeforeSongs_; // what played before the music room (a path; empty: nothing)
    std::string topScreen_;                       // the screen on top, and since when (close_after=)
    float topSince_ = 0.0f;
    [[nodiscard]] bool songsOpen() const;         // a screen with a songs part is open
    Script script_;
    std::vector<Translation> translations_;
    std::uint64_t frameNumber_ = 0;
    std::uint64_t pointerMoves_ = 0;
    bool padInUse_ = false;
    bool covered_ = false; // a menu covers the game (its voice and movie wait)
    std::unique_ptr<Game> game_;                          // idle (not started) on the title screen
    std::map<std::string, ScreenView, std::less<>> views_; // every screen of the script
    std::vector<std::string> overlays_;                   // open menus, the top one last
    std::vector<std::string> shown_;                      // "show screen" screens last frame (action_on_open / action_on_close)
    std::size_t calledDepth_ = 0;                         // overlays_ size while a screen the story waits for is open
    struct Leaving {
        std::string name;
        std::function<void()> then; // (once its animation is over; none: a "show screen" one, drawn on top until then)
    };
    std::vector<Leaving> leaving_; // screens playing their animation_on_close=
    bool coveredClose_ = false;    // (the top screen's animation_on_close= ended covered: what was under it comes back into view)
    // "open NAME" for a screen open under others, or for the title screen under menus: back to it. The
    // ones over it close, the top one first, each as it does (with its animation_on_close= and what it does
    // on closing), until it's on top; then it opens again (what it does on opening: its music...).
    std::optional<std::string> backTo_;
    void backTo(const std::string& name);
    void closeTowards(); // (the next one over it, once the top has closed)
    std::optional<Value> answer_;                         // what the screen the story waits for answers
    Variables persistent_;                                // "persistent.*", kept in persistent.txt
    Flowchart flowchart_;                                         // the flowchart, kept in flowchart.txt
    std::vector<std::vector<KeyChord>> controlKeys_;      // keys in effect, one list per script_.controls
    std::optional<std::pair<std::string, std::size_t>> rebinding_; // control + slot waiting for a key
    std::size_t galleryImage_ = 0;                                  // gallery image being looked at
    mutable std::map<std::string, bool, std::less<>> movies_; // gallery names that are movies (looked up once)
    std::optional<std::size_t> tip_;                      // tip being read
    struct PhoneView {
        std::string app, chat, image; // (see phoneApp())
        std::vector<std::string> back;     // the apps before it (Back goes to them)
        bool locked = false;               // its lock screen is up (whenever the player opens it: it locks as it's put away)
        bool straight = false;             // (opening for the story: no lock screen)
        bool pinned = false;               // (the Screens tab's preview: on `app`, even while a call is on)
        std::string code;                  // the lock screen's keypad: the passcode so far
        std::optional<float> wrongAt;      // ...and when a wrong one was tried
    } phone_;
    float ringPulse_ = 0.0f; // a call coming: seconds to the controllers' next buzz
    bool dataFolder_ = false;                             // --data: everything, saves too, goes there
    bool packed_ = false;                                 // the game is a pack (game.vnext): finished
    bool inPack_ = false;                                 // ...that keeps its players' saves and progress itself
    bool noGame_ = false;                                 // there's no game: just developer mode
    bool storyStarted_ = false;                           // without a title screen, the story's end closes the player
    bool waiting_ = false;                                // developer mode: no game is going on, the developer tools wait for Play
    bool textInputWanted_ = false, textInputActive_ = false;
    bool opening_ = false;                                // scene start before it opens the title screen (nothing's saved then)
    bool titleShown_ = false;                             // (its action_on_close runs when a game starts)
    std::optional<std::size_t> storyStart_;               // where New Game starts: the line after the one that opened the title screen
    Variables beforeTitle_; // what the story set before it opened the title screen (a language picked...): the title screen and New Game keep it
    // While the developer tools point at a sample of theirs (the Library's style): what it would do isn't done.
    bool sampling_ = false;
    std::string spoken_; // what was read aloud last

    Mode mode_ = Mode::Title;
    std::string question_;
    std::function<void()> confirmYes_;
    std::vector<SDL_Point> windowSizes_;
    bool hasSave_ = false, hasQuickSave_ = false; // (any save; a quick save)
    bool thumbnail_ = false;
    bool reloadRequested_ = false;
    bool clickThrough_ = false; // (the developer tools are open: a click that brings the window forward clicks too)
    std::string shownTitle_;     // the window's title, as it was last set
    TextBlock errorTitle_, errorText_, errorHint_;

    std::string gameRoot_; // the game folder, or the pack, with a slash after it
    std::string startScene_;
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
