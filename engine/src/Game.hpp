#pragma once

// The story as it plays: the game, its stage, and the flowchart of places reached.

#include "Assets.hpp"
#include "Screens.hpp"
#include "Script.hpp"
#include "System.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vn {

// ---------------------------------------------------------------------------
// The flowchart
// ---------------------------------------------------------------------------

// The story's flowchart: the places the player has passed in every playthrough (chapters, choices,
// endings and "chart" points), joined by the way they went. Each place keeps the game as it was the
// last time the player was there, so they can go back to it. Kept in chart.txt.
class Chart {
public:
    enum class Kind : std::uint8_t { Chapter, Choice, Ending, Point };
    struct Option {
        int index = 0;     // in the choice as written
        bool open = false; // it could be picked (else it was shown locked: needs ...)
    };
    struct Node {
        std::string id;              // "label:offset": where it is in the scripts
        Kind kind = Kind::Point;
        int order = 0;               // the order places were first reached in
        std::string state;           // the game there (a save that repeats the place)
        std::vector<Option> options; // a choice's options, as shown last time (hidden ones left out)
    };
    struct Edge {
        std::string from, to;
        int option = -1; // the option picked at `from`, when it's a choice
    };

    // The player reached `id` from `from` (empty: from the start), having picked `option` there.
    void visit(const std::string& id, Kind kind, const std::string& from, int option, std::string state,
               std::vector<Option> options = {});
    [[nodiscard]] const std::vector<Node>& nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<Edge>& edges() const { return edges_; }
    [[nodiscard]] const Node* node(std::string_view id) const;
    void clear();

    bool read(std::string_view saved);     // chart.txt's text (false: it isn't one)
    [[nodiscard]] std::string text() const; // ...and the chart as that
    [[nodiscard]] bool takeChanged() { return std::exchange(changed_, false); }

private:
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    bool changed_ = false;
};

// ---------------------------------------------------------------------------
// The stage
// ---------------------------------------------------------------------------

// The visual layer: a background plus tagged objects (characters and images) with position,
// anchor, zoom, rotation, opacity, flip, layer and tint; eased tweens; shake and flash; scene
// transitions; weather; the camera; effects over the whole stage. Missing images are drawn as
// labelled placeholders.
class Stage {
public:
    explicit Stage(const Canvas& size) : canvas(size) {}
    // Everything needed to recreate an object (saves).
    struct ObjectState {
        std::string tag, character, image, expression;
        float x = 0.5f, y = 1.0f, zoom = 1.0f, opacity = 1.0f, rotate = 0.0f; // x, y: 0..1, the part of the picture there too
        bool flip = false;
        int layer = 0;
        SDL_Color tint{255, 255, 255, 255};
    };

    void setBackground(std::string key, const Transition& transition);
    // Shows a character (`def`) or an image under `tag`, or changes the one already there (it
    // slides to a new place in `slide` seconds).
    void show(const std::string& tag, const CharacterDef* def, const std::string& image, const std::string& expression,
              const StageProps& props, const Transition& transition, float slide);
    void setExpression(std::string_view tag, const std::string& expression);
    [[nodiscard]] std::string expression(std::string_view tag) const; // what a picture on stage shows ("": its own)
    void hide(std::string_view tag, const Transition& transition);
    // Animates the given properties; returns false when nothing has that tag.
    bool move(std::string_view tag, const StageProps& props, float seconds, Ease ease);
    [[nodiscard]] bool moving() const;
    void shake(float strength, float seconds);
    void flash(SDL_Color color, float seconds);

    // Weather over the stage; a kind of "" stops it. Fades in or out over `fade` seconds.
    void setWeather(const cmd::Weather& weather, float fade);
    [[nodiscard]] const cmd::Weather& weather() const { return weather_; }

    // Effects over the whole stage ("effect" lines); `instant`: no fading. The ones that stay (not
    // those "for N seconds"), for saves.
    void setEffect(const cmd::Effect& effect, bool instant);
    [[nodiscard]] cmd::Effect effect() const { return {.looks = looks_, .amount = lookAmount_}; }

    // The camera: which point of the stage is in the middle (0..1), how close, how turned.
    struct Camera {
        float x = 0.5f, y = 0.5f, zoom = 1.0f, rotate = 0.0f;
        bool operator==(const Camera&) const = default;
    };
    void moveCamera(const Camera& to, float seconds, Ease ease);
    [[nodiscard]] Camera camera() const { return cameraTime_ < 1.0f ? cameraTo_ : camera_; } // where it's going
    void clearObjects(const Transition& transition);
    void reset();
    void restore(const ObjectState& state, const CharacterDef* def);

    void update(float dt);
    void finishTransitions();
    [[nodiscard]] bool backgroundTransitioning() const { return bgTime_ < bgDuration_; }

    // `labels`: write the expected file name on placeholders (config developer yes). `frameOf` says which
    // eyes and mouth each object shows (blinking and talking atlases).
    using FrameOf = std::function<Frame(const ObjectState&)>;
    void draw(SDL_Renderer* renderer, Assets& assets, const Font& labelFont, bool labels, float spriteHeight, const FrameOf& frameOf);

    [[nodiscard]] const std::string& background() const { return background_; }
    [[nodiscard]] std::vector<ObjectState> objects() const;

private:
    const Canvas& canvas; // the game's size
    [[nodiscard]] SDL_FRect whole() const { return {0.0f, 0.0f, canvas.w, canvas.h}; } // (all of it)
    static constexpr std::size_t kTweened = 8; // x, y, zoom, opacity, rotate, tint (red, green, blue)

    struct Object {
        ObjectState s;
        CharacterDef def; // for characters: image prefix, placeholder colour and name
        int order = 0;    // draw order within a layer
        float fade = 0.0f, fadeRate = 0.0f;
        bool leaving = false;
        std::array<float, kTweened> from{}, to{};
        float tweenTime = 1.0f, tweenDuration = 0.0f;
        Ease ease = Ease::InOut;
    };

    Object* find(std::string_view tag);
    void apply(Object& o, const StageProps& props, float seconds, Ease ease = Ease::InOut); // 0 seconds: at once
    void drawBackground(SDL_Renderer* r, Assets& assets, const Font* font, const std::string& key, float alpha, float dx);
    void drawObject(SDL_Renderer* r, Assets& assets, const Font* font, const Object& o, float spriteHeight, Frame frame);
    void drawScene(SDL_Renderer* r, Assets& assets, const Font* font, float spriteHeight, const FrameOf& frameOf);
    void drawView(SDL_Renderer* r, Assets& assets, const Font* font, float spriteHeight, const FrameOf& frameOf); // through the camera
    void drawWeather(SDL_Renderer* r, Assets& assets);
    void drawShapedWipe(SDL_Renderer* r, Assets& assets, const Font* font, float t);
    void drawEffects(SDL_Renderer* r, SDL_Texture* view); // blur, pixelate, glitch over the stage drawn into `view`
    void drawNoise(SDL_Renderer* r, float alpha);
    void updateWeather(float dt);
    void updateEffects(float dt);
    // A picture as the colour effects show it (fading from or to as it is), and a colour the same way.
    // A tint multiplies a picture's colours, so it turns grey or brown with them but isn't inverted
    // (white, no tint, would make everything black).
    void paint(Assets& assets, std::string_view name, Frame frame, const std::function<void(const Picture&, float)>& draw) const;
    [[nodiscard]] SDL_Color shade(SDL_Color c, bool tint = false) const;
    // A texture to draw into, w x h pixels (made again when the size changes).
    SDL_Texture* canvasTexture(SDL_Renderer* r, TexturePtr& texture, int w, int h, SDL_ScaleMode mode = SDL_SCALEMODE_LINEAR);
    TextBlock& label(const Font& font, const std::string& text);

    std::string background_, previous_;
    Transition bgTransition_;
    float bgTime_ = 0.0f, bgDuration_ = 0.0f;
    std::vector<Object> objects_;
    int nextOrder_ = 0;
    float shakeTime_ = 0.0f, shakeDuration_ = 0.0f, shakeStrength_ = 0.0f;
    float flashTime_ = 0.0f, flashDuration_ = 0.0f;
    SDL_Color flashColor_{255, 255, 255, 255};
    float clock_ = 0.0f;
    SDL_FPoint offset_{}; // current shake offset

    struct Particle {
        float x = 0, y = 0, vx = 0, vy = 0, size = 1, angle = 0, spin = 0, phase = 0, sway = 0;
        SDL_Color color{255, 255, 255, 255};
    };
    cmd::Weather weather_, fading_;       // the weather, and the one fading out
    std::vector<Particle> particles_, fadingParticles_;
    float weatherAlpha_ = 0.0f, fadingAlpha_ = 0.0f, weatherFade_ = 1.0f;
    std::mt19937 rng_{12345}; // (always the same seed: the weather looks the same each time)
    void spawn(const cmd::Weather& w, std::vector<Particle>& out);

    Camera camera_, cameraFrom_, cameraTo_;
    float cameraTime_ = 1.0f, cameraDuration_ = 0.0f;
    Ease cameraEase_ = Ease::InOut;
    TexturePtr scene_;

    // Effects: the ones that stay and the ones for a while, how strong each is now (0..1, fading
    // toward on or off), and the colour ones being drawn (they change from one to another at once).
    unsigned looks_ = 0, burst_ = 0;
    float burstTime_ = 0.0f, lookAmount_ = 1.0f, burstAmount_ = 1.0f, lookFade_ = 0.0f;
    std::array<float, kStageLooks.size()> strength_{};
    [[nodiscard]] float strength(StageLook look) const { return strength_[static_cast<std::size_t>(std::countr_zero(static_cast<unsigned>(look)))]; }
    unsigned colorMask_ = 0;
    float colorAlpha_ = 0.0f;
    TexturePtr view_, small_, smaller_, blocks_, noise_; // the stage drawn whole; blurred; pixelated; TV static
    struct Strip {
        float y, h, dx;
        SDL_Color tint;
    };
    std::vector<Strip> strips_; // a glitch's, for a moment
    float glitchUntil_ = 0.0f;
    // A wipe shaped by a picture: its pixels (0 = dark, changes first), the see-through mask made
    // from them, and the new background cut by it.
    std::string shape_;
    std::vector<Uint8> shapeValues_;
    int shapeW_ = 0, shapeH_ = 0;
    TexturePtr shapeMask_, shapeLayer_;
    std::unordered_map<std::string, TextBlock, StringHash, std::equal_to<>> labels_;
};

// ---------------------------------------------------------------------------
// The game
// ---------------------------------------------------------------------------

struct Context {
    SDL_Renderer* renderer;
    Assets& assets;
    Audio& audio;
    const Settings& settings;
    ReadLog& read;
    Variables& persistent; // "persistent.*" variables, shared by every playthrough
    Chart& chart;          // the flowchart, from every playthrough
    const Canvas& canvas;  // the game's size (the engine's)
    std::function<std::optional<Value>(std::string_view)> builtin; // {title}, skip, has_save...
};

// A line on the page (textbox page): who said it and what, with its colours.
struct PageLine {
    std::string speaker;
    SDL_Color color;
    std::string text;
    std::vector<TextSpan> spans;
    std::optional<SDL_Color> textColor;
};

// An option of the choice on screen (or an answer to a message).
struct ChoiceItem {
    std::string text;    // its text tags kept, for the screen
    bool locked = false; // shown, but it needs something first: (needs ...)
    std::string note;    // what it needs
    bool seen = false;   // picked before (in this playthrough or an earlier one)
    int option = 0;      // which one it is, as written
};

// A message on the phone ("message NAME: text"), or one the player sent.
struct MessageItem {
    std::string from; // the sender's name (empty: the player)
    std::string with; // whose conversation it's in: the sender's, or the one the player wrote to
    SDL_Color color;  // that person's
    std::string text; // with its text tags
    bool read = false;
    std::optional<std::size_t> replies; // its message command, while the answers to it are open
    float typing = 0.0f;                // an answer back: seconds until it arrives (they're typing meanwhile)
};

struct HistoryEntry {
    std::string speaker;
    SDL_Color color{};
    std::string text;
    bool choice = false; // a choice the player made
    std::vector<TextSpan> spans;
    // The line's voice, to hear it again: a sound file, or made-up speech (babble=) of its text.
    std::string voice;
    float voiceVolume = 1.0f;
    float babble = 0.0f, babbleSpeed = 1.0f;
    [[nodiscard]] bool voiced() const { return !voice.empty() || babble > 0.0f; }
};

// A text with its look (see RichText): {color} {bold} {italic} {underline} {strike} and {ruby}
// tags become spans, and {expressions} are filled in. Other tags ({pause}, {speed}...) go to `other`.
[[nodiscard]] RichText richText(std::string_view source, const Lookup& lookup, const Palette* palette, const TagHandler& other = {});

// A screen the story opened and waits for ("open", "ask", a chapter's title card).
struct ScreenRequest {
    std::string name;
    std::string variable; // ask: the variable being typed into
    std::string prompt;   // ask: the question, shown as {question}
    int max = 0;          // ask: longest answer
    Transition transition{}; // open ... with fade
    Transition closing{};    // ...and close with fade
};

// Plays a Script: runs its commands until one has to wait (a line, a choice, a timer, a
// transition, a called screen) and draws the stage. The textbox and menus are script screens that
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

    // `subroutine`: a "return" with nowhere to return to ends the game (splash screens).
    void start(std::size_t pc, bool subroutine = false);
    void carry(Variables vars) { carried_ = std::move(vars); } // the variables the next start() begins with (else none)
    void update(float dt);
    void draw(const Font& labelFont);

    // Player input.
    void advance();
    void choose(std::size_t index);
    void setUiHidden(bool hidden) { uiHidden_ = hidden && (wait_ == Wait::Say || wait_ == Wait::Menu); }
    void toggleSkip() { skip_ = !skip_; }
    void toggleAuto() { auto_ = !auto_; }
    // A command from a button, key or screen hook (anything that doesn't wait).
    void perform(const Command& command);
    // The developer tools' preview: plays on as fast as it goes (picking the options that lead to
    // command `target`) to the first line, choice, video or screen opened at a command where `there`
    // is true, and shows it (a line whole, a video's first picture), with the stage settled. When the
    // story would go past `target` without doing it (an `if` it's in isn't true, or the option it's
    // under isn't picked), it goes on from `target` itself, with everything as it is then (`skipped`).
    // False if the story never gets there (or not within a second). Other videos are gone past.
    bool playTo(std::size_t target, const std::function<bool(std::size_t)>& there, bool* skipped = nullptr);
    // Set before start(): the story stops just before command `pc` (playTo() stops there, ready to do it,
    // whatever it is: a movie, a scene...), so a save made then starts with it.
    void stopBefore(std::optional<std::size_t> pc) { stopAt_ = pc; }
    // The screen the story was waiting for closed, with an answer ("answer TEXT") or not.
    void screenClosed(std::optional<Value> answer);
    // A menu covers the game (or no longer does): the voice line and video wait meanwhile.
    void setCovered(bool covered);
    [[nodiscard]] bool playingMovie() const { return wait_ == Wait::Movie; }
    [[nodiscard]] bool openingScreen() const { return wait_ == Wait::Screen; } // (the story waits for request())
    void playMovieRound(float dt); // the developer tools' preview at a video: it plays, round and round, and the story waits

    // Variables, then persistent ones, then what `variable` lines start them at, then the engine's built-in names.
    [[nodiscard]] Value variable(std::string_view name) const;
    void setVariable(const std::string& name, Value value);
    [[nodiscard]] const Variables& variables() const { return vars_; } // the story's own (set so far)
    [[nodiscard]] std::vector<Stage::ObjectState> onStage() const { return stage_.objects(); }
    [[nodiscard]] Stage::Camera camera() const { return stage_.camera(); }
    [[nodiscard]] const Lookup& lookup() const { return lookup_; }

    // State for the screens.
    [[nodiscard]] bool finished() const { return wait_ == Wait::Finished; }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] bool showingSay() const { return wait_ == Wait::Say || (wait_ == Wait::Menu && menuPrompt_); }
    [[nodiscard]] bool showingChoices() const { return wait_ == Wait::Menu || trigger_; } // (one up while the story goes on too)
    [[nodiscard]] bool menuPrompt() const { return wait_ == Wait::Menu && menuPrompt_; }
    [[nodiscard]] bool textComplete() const; // waiting for a click (end of the line or a {pause})
    [[nodiscard]] bool atPause() const { return textComplete() && pendingStop(); } // ...at a {pause}
    [[nodiscard]] const std::string& speaker() const { return speaker_; }
    [[nodiscard]] SDL_Color speakerColor() const { return speakerColor_; }
    [[nodiscard]] std::optional<SDL_Color> textColor() const { return textColor_; }
    [[nodiscard]] const std::string& textbox() const { return textbox_; }
    [[nodiscard]] const std::string& dialogue() const { return text_; }
    [[nodiscard]] const std::vector<TextSpan>& spans() const { return spans_; }
    [[nodiscard]] std::size_t revealed() const { return static_cast<std::size_t>(reveal_); }
    // The next {pause} (or the end): word and line reveal never show text past it.
    [[nodiscard]] std::size_t revealLimit() const;
    // The speaker's face picture with their expression ("aiko_face_happy"), and without it; empty
    // when they have none.
    [[nodiscard]] const std::string& face() const { return face_; }
    [[nodiscard]] const std::string& faceBase() const { return faceBase_; }
    [[nodiscard]] const std::string& faceSide() const { return faceSide_; } // empty when there's no face
    // What a textbox screen shows: the line on screen (in its textbox), or a line still up in
    // another one, kept there (its screen has keep=yes) or said together with it. boxes(): the
    // textboxes with a line to show, the line on screen's last.
    struct LineView {
        std::string_view speaker, text, face, faceBase, faceSide;
        SDL_Color speakerColor{255, 255, 255, 255};
        std::optional<SDL_Color> textColor;
        const std::vector<TextSpan>* spans = nullptr;
        std::size_t revealed = 0, limit = 0; // letters shown, and the most word or line reveal may show
        Reveal mode = Reveal::Letter;
        bool complete = false, atPause = false; // all there, or at a {pause}: waiting for a click
        const CharacterDef* character = nullptr;
        Frame faceFrame;
    };
    [[nodiscard]] std::optional<LineView> line(std::string_view box) const;
    [[nodiscard]] std::vector<std::string> boxes() const;
    // The eyes and mouth a character (or a picture on stage, by its name) shows now: blinking,
    // and talking while it's their line. faceFrame(): the speaker's face (it blinks and talks on
    // its own when face_sync= leaves those out).
    [[nodiscard]] Frame frame(std::string_view who) const;
    [[nodiscard]] Frame faceFrame() const;
    [[nodiscard]] const CharacterDef* speakerCharacter() const { return script_.character(speakerKey_); }
    // The pictures the next `commands` story commands show, each with the colour effects it will be
    // drawn with (to load them before they're needed; a colour effect coming needs copies of what's
    // on stage then).
    struct Upcoming {
        std::string name;
        unsigned looks = 0;
    };
    [[nodiscard]] std::vector<Upcoming> upcomingPictures(int commands) const;
    [[nodiscard]] const std::vector<ChoiceItem>& choices() const { return choiceItems_; }
    // The phone: the messages so far (the newest last), and the answers open to one. Answers stay open
    // until one is sent or the same person writes again. Sending one runs its lines: true when the
    // story then moved on (to a line or a label), false when it stays where it was.
    [[nodiscard]] const std::vector<MessageItem>& messages() const { return messages_; }
    [[nodiscard]] std::vector<ChoiceItem> replies(std::size_t message) const;
    bool reply(std::size_t message, std::size_t option);
    void readMessages(std::string_view with = {}); // the ones in a conversation (or all) have been seen
    // The people messaging the player, the latest conversation first.
    struct Contact {
        std::string name;
        SDL_Color color;
        std::size_t last = 0; // their newest message
        int unread = 0;
        bool typing = false;  // writing something (then that's their newest)
    };
    [[nodiscard]] std::vector<Contact> contacts() const;
    void addSamples(); // made-up history and messages, when there are none: the editor's screen previews
    [[nodiscard]] bool hasPhone() const { return phoneOn_; } // (not taken away with "phone off")
    void updatePhone(float dt); // answers being typed arrive (menus or not)
    [[nodiscard]] std::string choiceScreen() const; // the screen showing them
    // How much of a timed choice's time is left (1 down to 0), if one is on screen.
    [[nodiscard]] std::optional<float> timeLeft() const;
    [[nodiscard]] const std::vector<HistoryEntry>& history() const { return history_; }
    [[nodiscard]] const std::string& chapter() const { return chapter_; } // "chapter TITLE" (empty before the first)
    [[nodiscard]] const std::string& chartPlace() const { return chartNode_; } // the last place on the flowchart passed
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
    [[nodiscard]] std::vector<cmd::Ui> takeRequests() { return std::exchange(requests_, {}); } // notify, open...
    [[nodiscard]] bool takePersistentChanged() { return std::exchange(persistentChanged_, false); }

    // Save data is a small text format. load() returns a note for the player (when the script
    // changed since saving) or an error.
    [[nodiscard]] std::string save() const;
    std::expected<std::string, std::string> load(std::string_view data);

    // The script's texts changed language: shows the ones on screen in the new one.
    void retranslate();
    // Rollback: back to the line (or choice) before this one, as it was then. rollbackTo(): back to
    // the line (or choice) of history() entry `entry`, if it isn't too far back.
    [[nodiscard]] bool canRollback() const;
    bool rollback();
    [[nodiscard]] bool canRollbackTo(std::size_t entry) const { return momentOf(entry).has_value(); }
    bool rollbackTo(std::size_t entry);
    // The story's variables in a save (to show a flowchart place's texts as they were there).
    [[nodiscard]] static Variables savedVariables(std::string_view state);

private:
    enum class Wait : std::uint8_t { None, Say, Menu, Timer, Transition, Pause, Move, Screen, Movie, Reached, Finished };

    struct Stop {
        std::size_t at; // codepoint position in the text
        float seconds;  // < 0: wait for a click
    };
    struct Speed {
        std::size_t at;
        int cps; // < 0: back to the player's setting
    };
    struct Loop {
        std::string file;
        float volume = 1.0f;
    };

    void run();
    // The state to restore. `history`: rollback keeps that many history entries (else load: none).
    std::expected<std::string, std::string> restore(std::string_view data, std::optional<std::size_t> history);
    void remember(); // a rollback point at the line or choice now on screen
    [[nodiscard]] std::optional<std::size_t> momentOf(std::size_t entry) const; // in past_
    bool rewind(std::size_t moment);                                           // to past_[moment]
    void addHistory(HistoryEntry entry);                                       // keeping config history_limit
    void addToPage(); // the line just said
    void execute(const Command& command, std::size_t here);
    void sayLine(const cmd::Say& c, std::size_t here, bool fast);       // ...a line said
    void openMenu(const cmd::Menu& c, std::size_t here);                // ...a choice
    void play(const cmd::Play& c, bool fast);                           // ...music, a voice or a sound
    void playMovie(const cmd::Movie& c, std::size_t here, bool fast);   // ...a video
    void sendMessage(const cmd::Message& c, std::size_t here);          // ...a message on the phone
    void waitFor(Wait wait, std::size_t here);
    void fail(std::string message);
    void endMovie(); // the story goes on after it
    void say(std::string speaker, SDL_Color color, std::string_view text, const CharacterDef* def);
    void revealTo(std::size_t position);
    void updateLine(float dt, bool fast);
    void updateSpeech();
    [[nodiscard]] bool voiceAtPause() const; // the voice waits with its text at a {pause}
    // How open a mouth that talks like `talk` is (or as the player's Mouths setting allows, if that's less).
    [[nodiscard]] float mouth(Talk talk, float speech, bool voiced) const;
    [[nodiscard]] const Stop* pendingStop() const;
    [[nodiscard]] int currentSpeed() const;
    [[nodiscard]] const GameConfig& config() const { return script_.config; }
    bool have(RecordKind kind, std::string_view name); // the player has it now; false if they had it already (or it isn't listed)
    void collect(RecordKind kind, std::string_view name); // ...and its message
    void learnTips(std::string_view text);
    void waitForScreen(ScreenRequest request, std::size_t here);
    void notify(std::string message) { requests_.push_back(cmd::Ui{.verb = "notify", .arg = std::move(message)}); }

    Context ctx_;
    const Script& script_;
    Lookup lookup_;
    Stage stage_;

    Wait wait_ = Wait::None;
    std::size_t pc_ = 0;
    std::size_t current_ = 0;  // instruction being executed (for error messages)
    std::size_t resumePc_ = 0; // where a save made right now should resume
    std::optional<std::size_t> stopAt_; // stopBefore()
    std::size_t watch_ = SIZE_MAX; // playTo(): its target, and whether it has been done yet
    bool watched_ = false;
    const std::function<bool(std::size_t)>* watchThere_ = nullptr; // (and where it stops: videos there play)
    std::uint64_t random_ = newRandomState(); // random()'s state: this game's own (its commands draw from it)
    std::uint64_t resumeRandom_ = 0; // random()'s state there (so the same numbers come again)
    std::vector<std::size_t> callStack_;
    Variables vars_;
    Variables carried_;
    std::string music_;
    std::string chapter_;
    // The flowchart: the last place passed, and the option picked since (at a choice).
    std::string chartNode_;
    int chartOption_ = -1;
    void passPlace(std::size_t here, Chart::Kind kind, std::vector<Chart::Option> options = {});
    float musicVolume_ = 1.0f;
    std::map<std::string, Loop, std::less<>> loops_; // looping sound channels (restored on load)
    std::map<std::string, std::string, std::less<>> expressions_; // each character's latest expression
    std::vector<std::string> screens_;                // screens shown with "show screen"
    std::string textboxScreen_;                       // "textbox NAME" (empty: say)
    std::vector<PageLine> page_;                      // lines since "new page"
    std::vector<MessageItem> messages_;               // the phone's
    bool answering_ = false; // an answer's lines are running: messages back are typed first
    bool phoneOn_ = true;    // "phone off": taken away for a while
    void arrive(const MessageItem& m); // a message reaches the phone: its tips, its notice

    // Lines in other textboxes than the one on screen's: kept there, or said together with it (a
    // "together" block), each typing at its own speed. Saves keep them as their text with its tags.
    struct BoxLine {
        std::string box, speaker, speakerKey, source, text, face;
        SDL_Color speakerColor{255, 255, 255, 255};
        std::vector<TextSpan> spans;
        float reveal = 0.0f;
        std::size_t length = 0;
        bool keep = false; // (else it goes when the next line comes)
    };
    std::vector<BoxLine> others_;
    bool together_ = false; // the lines of a together block are being said
    [[nodiscard]] std::string boxOf(const CharacterDef* def) const; // the textbox a line goes to
    [[nodiscard]] bool keeps(std::string_view box) const;           // its screen has keep=yes
    [[nodiscard]] float speedOf(std::string_view box) const;        // its screen's text_speed= (1: the player's)
    [[nodiscard]] bool othersTyping() const;
    [[nodiscard]] BoxLine boxLine(std::string box, std::string speaker, SDL_Color color, std::string source, const CharacterDef* def) const;
    void sayTogether(const cmd::Say& c, const CharacterDef* def); // a together block's line, before its last

    // The current line.
    std::string source_; // its text with values filled in and tags kept
    std::string speaker_;
    std::string speakerKey_; // the speaking character, if they're defined
    SDL_Color speakerColor_{255, 255, 255, 255};
    std::optional<SDL_Color> textColor_;
    std::string textbox_;
    std::string blip_;
    std::string face_, faceBase_, faceSide_;
    Reveal revealMode_ = Reveal::Letter;
    std::string text_;
    std::vector<TextSpan> spans_;
    std::vector<Stop> stops_;
    std::vector<Speed> speeds_;
    std::size_t passedStops_ = 0;
    bool noWait_ = false;
    std::size_t length_ = 0; // codepoints in text_
    bool lineRead_ = false;  // the line was seen before (in any playthrough)
    struct LineVoice {
        std::string file;        // as the script names it
        float volume = 1.0f;
        std::optional<bool> sync; // "voice x sync" / "no sync"
    };
    std::optional<LineVoice> pendingVoice_; // a voice line started for the next line
    LineVoice lineVoice_;                   // the line on screen's own voice line (file empty: none)
    bool voiced_ = false;                // the line on screen has its voice line
    bool syncVoice_ = false;             // ...and its text shows as much as has been said
    bool typing_ = false;                // letters are appearing
    float speech_ = 0.0f;                // how much the speaker is speaking (0..1): mouths follow it
    float faceSpeech_ = 0.0f;            // the same for a face with a mouth of its own (face_sync without talk)
    float clock_ = 0.0f;                 // game time (blinking)
    float lineTime_ = 0.0f;              // time since the line started
    float reveal_ = 0.0f;
    float holdTimer_ = 0.0f; // timed {pause ...} in progress
    float autoTimer_ = 0.0f;
    float skipTimer_ = 0.0f;
    float blipTimer_ = 0.0f;
    float timer_ = 0.0f;

    std::unique_ptr<Movie> movie_;  // "movie NAME" playing
    std::string movieStandIn_;      // ...or the name of one that's missing (a card shows it for a moment)
    TextBlock movieLabel_;
    bool movieSkippable_ = true;
    std::vector<ChoiceItem> choiceItems_;
    [[nodiscard]] const cmd::Menu* menu() const; // the choice on screen (its command: a save made now repeats it)
    [[nodiscard]] std::optional<ChoiceItem> offer(const cmd::Choice& choice, int option) const; // shown now, if it is
    void listChoices(const cmd::Menu& menu); // the options to show now
    void letPass();                          // none of them picked: the story goes on after the choice
    bool menuPrompt_ = false;                // the textbox shows something meanwhile (its question, or the line before an optional one)
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
        bool stop;             // rollback stops here ({nowait} lines go by)
        Variables persistent;  // the persistent variables then
    };
    std::vector<Moment> past_; // the last config rollback lines and choices, the newest last
    bool skip_ = false;
    bool auto_ = false;
    bool uiHidden_ = false;
    bool autosave_ = false;
    std::string error_;
};

} // namespace vn
