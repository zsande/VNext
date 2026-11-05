#pragma once

// The story as it plays: the game, its stage, and the flowchart of points reached.

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

// The story's flowchart: the points the player has passed in every playthrough (choices and endings),
// joined by the way they went. Each point keeps the game as it was the
// last time the player was there, so they can go back to it. Kept in flowchart.txt.
class Flowchart {
public:
    enum class Kind : std::uint8_t { Choice, Ending };
    struct Option {
        int index = 0;     // in the choice as written
        bool open = false; // it could be picked (else it was shown locked: needs ...)
    };
    struct Node {
        std::string id;              // "scene:offset": where it is in the scripts
        Kind kind = Kind::Choice;
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

    bool read(std::string_view saved);     // flowchart.txt's text (false: it isn't one)
    [[nodiscard]] std::string text() const; // ...and the flowchart as that
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
// anchor, zoom, rotation, opacity, flip, layer and tint; eased tweens; animations (as they come and
// go, and while they're shown); weather; the camera; effects over the whole stage.
// Missing images are drawn as labelled placeholders.
class Stage {
public:
    explicit Stage(const Canvas& size) : canvas(size) {}
    // Everything needed to recreate an object (saves).
    struct ObjectState {
        std::string tag, character, image, expression;
        float x = 0.5f, y = 1.0f, zoom = 1.0f, opacity = 1.0f, rotation = 0.0f; // x, y: 0..1, the part of the image there too
        bool flip = false;
        int layer = 0;
        SDL_Color tint{255, 255, 255, 255};
        std::string animation; // what it plays while it's shown (show ... animation=)
        std::string played;    // one played on it (show ... with), on top of that
    };

    // A new background, playing `entrance` as it comes in over the one before (which goes, with what was
    // on stage, when that ends; none: at once). The one already there (`key` is its name): only
    // `entrance`, played on it. `done`: at its end at once (skipping, or animations off).
    void setBackground(std::string key, std::shared_ptr<const Animation> entrance, bool done);
    [[nodiscard]] bool backgroundComing() const { return !previous_.empty(); } // (the one before still under it)
    // Shows a character (`def`) or an image under `tag`, playing `played` as it comes, or changes the one
    // already there (it slides to a new place in `slide` seconds, and plays `played`).
    void show(const std::string& tag, const CharacterDef* def, const std::string& image, const std::string& expression,
              const StageProps& props, std::shared_ptr<const Animation> played, float slide, bool done);
    [[nodiscard]] bool has(std::string_view tag) const; // (on stage, not going)
    // What an image on stage plays: its own (`own`: while it's shown; else one played on it, on top
    // of that). Null: none. `done`: as it ends (skipping, or a save loaded), unless it's looping. (Nothing
    // with that tag: nothing.) animateBackground(): the same for the background (a new one starts without
    // them).
    void animate(std::string_view tag, std::shared_ptr<const Animation> animation, bool own, bool done);
    void animateBackground(std::shared_ptr<const Animation> animation, bool own, bool done);
    [[nodiscard]] std::string backgroundAnimation(bool own) const; // (its name: "" for none)
    void setExpression(std::string_view tag, const std::string& expression);
    [[nodiscard]] std::string expression(std::string_view tag) const; // what an image on stage shows ("": its own)
    // It plays `exit`, then it's gone (none, or `done`: at once).
    void hide(std::string_view tag, std::shared_ptr<const Animation> exit, bool done);
    [[nodiscard]] bool moving() const; // (a slide, the camera, or an animation played on something that hasn't ended)

    // Weather over the stage; a kind of "" stops it. Fades in or out over `fade` seconds.
    void setWeather(const cmd::Weather& weather, float fade);
    [[nodiscard]] const cmd::Weather& weather() const { return weather_; }

    // Effects over the whole stage ("effect" lines); `instant`: no fading. The ones that stay (not
    // those "for N seconds"), for saves.
    void setEffect(const cmd::Effect& effect, bool instant);
    [[nodiscard]] cmd::Effect effect() const { return {.looks = looks_, .amount = lookAmount_}; }

    // The view, played as an image's animations are (`own`: one it keeps playing; else one played on it
    // now, from where the one before left the view; none: back to normal). `done`: at its end at once.
    // Its places are what it looks at (x=-1: the left edge in the middle), its moves by a share of the
    // screen move what it looks at; its opacity, colour, cover and wipe are the whole stage's.
    void animateCamera(std::shared_ptr<const Animation> animation, bool own, bool done);
    [[nodiscard]] std::string cameraAnimation(bool own) const; // (its name: "" for none)
    [[nodiscard]] const Animation::Look& cameraStart() const { return camera_.playedFrom; } // (where the one played on it started)
    void restoreCamera(std::shared_ptr<const Animation> own, std::shared_ptr<const Animation> played, const Animation::Look& start); // (a save's: as they ended)
    void clearObjects(); // (the background changes: they go with the one before)
    void reset();
    void restore(const ObjectState& state, const CharacterDef* def);

    void update(float dt);
    void finish();                                // what's under way, at its end (a click, or skipping)
    void setStill(bool still) { still_ = still; } // animations off: they show how they end, and looping ones stand still

    // `labels`: write the expected file name on placeholders (setting developer yes). `frameOf` says which
    // eyes and mouth each object shows (blinking and talking atlases).
    using FrameOf = std::function<Frame(const ObjectState&)>;
    void draw(SDL_Renderer* renderer, Assets& assets, const Font& labelFont, bool labels, float characterHeight, const FrameOf& frameOf);

    [[nodiscard]] const std::string& background() const { return background_; }
    [[nodiscard]] std::vector<ObjectState> objects() const;

private:
    const Canvas& canvas; // the game's size
    [[nodiscard]] SDL_FRect whole() const { return {0.0f, 0.0f, canvas.w, canvas.h}; } // (all of it)
    static constexpr std::size_t kTweened = 8; // x, y, zoom, opacity, rotation, tint (red, green, blue)

    // An image's animations: its own (while it's shown) and one played on it (on top), and how far
    // into each. Their time goes on to their end, or round and round (kept past one time through,
    // which is what waiting for a played one waits for).
    struct Animations {
        std::shared_ptr<const Animation> own, played;
        float ownTime = 0.0f, playedTime = 0.0f;
        Animation::Look playedFrom; // where the played one starts (the camera's: where the one before left it)
        void set(std::shared_ptr<const Animation> animation, bool isOwn, bool done); // (its own, the same one: goes on)
        void onward(float dt, bool still);
        void finish();                    // (skipping: the ones that end, at their end; a played loop past one time through)
        [[nodiscard]] bool going() const; // a played one hasn't ended (one time through, if it plays again and again)
        // How they have it look (`tint`: where a colour starts from; places and amounts as Animation::at's),
        // and how the played one alone has it.
        [[nodiscard]] Animation::Look look(SDL_Color tint, const Animation::Offset& offset, SDL_FPoint screen) const;
        [[nodiscard]] Animation::Look playedLook(SDL_Color tint, const Animation::Offset& offset, SDL_FPoint screen) const;
    };

    struct Object {
        ObjectState s;
        CharacterDef def; // for characters: image prefix, placeholder colour and name
        int order = 0;    // draw order within a layer
        bool leaving = false;  // hidden: going once its animation ends
        bool oldBackground = false; // there when the background changed: drawn with the one before, and gone with it
        std::array<float, kTweened> from{}, to{};
        float tweenTime = 1.0f, tweenDuration = 0.0f;
        Ease ease = Ease::InOut;
        Animations animations; // (its own, animation=; and the one its line plays, with)
    };

    Object* find(std::string_view tag);
    void apply(Object& o, const StageProps& props, float seconds, Ease ease = Ease::InOut); // 0 seconds: at once
    void dropPrevious(); // the new background has come in: the one before goes, with what was on stage then
    // The background (`previous`: the one going, while a new one comes in), as its animations have it.
    void drawBackground(SDL_Renderer* r, Assets& assets, const Font* font, bool previous);
    void drawObject(SDL_Renderer* r, Assets& assets, const Font* font, const Object& o, float characterHeight, Frame frame);
    // How an image's animations have it look now, drawn at `box` (where it is without them): moved (in
    // pixels), faded, zoomed, turned, tinted (from `tint`), covered with a colour, wiped.
    [[nodiscard]] Animation::Look lookOf(const Animations& animations, SDL_Color tint, const SDL_FRect& box) const;
    // An image drawn at `box` covered `amount` of the way with a colour: its shape filled with it.
    // (Where the graphics can't fill a shape: tinted towards it.)
    void drawCovered(SDL_Renderer* r, const Image& pic, const SDL_FRect& box, float angle, const SDL_FPoint& pivot, bool flip, SDL_Color colour,
                     float amount);
    TexturePtr cover_; // (what drawCovered fills, as big as the biggest so far)
    int coverW_ = 0, coverH_ = 0;
    Wiper wiper_;
    bool still_ = false;
    void drawStage(SDL_Renderer* r, Assets& assets, const Font* font, float characterHeight, const FrameOf& frameOf);
    void drawView(SDL_Renderer* r, Assets& assets, const Font* font, float characterHeight, const FrameOf& frameOf); // through the camera
    void drawWeather(SDL_Renderer* r, Assets& assets);
    void drawEffects(SDL_Renderer* r, SDL_Texture* view); // blur, pixelate, glitch over the stage drawn into `view`
    void drawNoise(SDL_Renderer* r, float alpha);
    void updateWeather(float dt);
    void updateEffects(float dt);
    // An image as the colour effects show it (fading from or to as it is), and a colour the same way.
    // A tint multiplies an image's colours, so it turns grey or brown with them but isn't inverted
    // (white, no tint, would make everything black).
    void paint(Assets& assets, std::string_view name, Frame frame, const std::function<void(const Image&, float)>& draw) const;
    [[nodiscard]] SDL_Color shade(SDL_Color c, bool tint = false) const;
    // A texture to draw into, w x h pixels (made again when the size changes).
    SDL_Texture* canvasTexture(SDL_Renderer* r, TexturePtr& texture, int w, int h, SDL_ScaleMode mode = SDL_SCALEMODE_LINEAR);
    TextBlock& label(const Font& font, const std::string& text);

    std::string background_, previous_;
    Animations backgroundAnimations_, previousAnimations_; // (the one going goes on as it was, while the new one comes in)
    std::vector<Object> objects_;
    int nextOrder_ = 0;
    float clock_ = 0.0f;

    struct Particle {
        float x = 0, y = 0, vx = 0, vy = 0, size = 1, angle = 0, spin = 0, phase = 0, sway = 0;
        SDL_Color colour{255, 255, 255, 255};
    };
    cmd::Weather weather_, fading_;       // the weather, and the one fading out
    std::vector<Particle> particles_, fadingParticles_;
    float weatherAlpha_ = 0.0f, fadingAlpha_ = 0.0f, weatherFade_ = 1.0f;
    std::mt19937 rng_{12345}; // (always the same seed: the weather looks the same each time)
    void spawn(const cmd::Weather& w, std::vector<Particle>& out);

    Animations camera_;
    [[nodiscard]] Animation::Offset cameraPlaces() const;
    [[nodiscard]] Animation::Look cameraLook() const; // (how its animations have it now)
    [[nodiscard]] Animation::Look cameraStartFor(const Animation& next) const;
    Wiper cameraWiper_;
    TexturePtr drawn_; // (the stage drawn, for the camera to move)

    // Effects: the ones that stay and the ones for a while, how strong each is now (0..1, fading
    // toward on or off), and the colour ones being drawn (they change from one to another at once).
    unsigned looks_ = 0, burst_ = 0;
    float burstTime_ = 0.0f, lookAmount_ = 1.0f, burstAmount_ = 1.0f, lookFade_ = 0.0f;
    std::array<float, kStageLooks.size()> strength_{};
    [[nodiscard]] float strength(StageLook look) const { return strength_[static_cast<std::size_t>(std::countr_zero(static_cast<unsigned>(look)))]; }
    unsigned colourMask_ = 0;
    float colourAlpha_ = 0.0f;
    TexturePtr view_, small_, smaller_, blocks_, noise_; // the stage drawn whole; blurred; pixelated; TV static
    struct Strip {
        float y, h, dx;
        SDL_Color tint;
    };
    std::vector<Strip> strips_; // a glitch's, for a moment
    float glitchUntil_ = 0.0f;
    std::unordered_map<std::string, TextBlock, StringHash, std::equal_to<>> labels_;
};

// ---------------------------------------------------------------------------
// The game
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// The phone
// ---------------------------------------------------------------------------

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
    float voiceVolume = 1.0f;
    float madeUpVoice = 0.0f, madeUpVoiceSpeed = 1.0f;
    [[nodiscard]] bool voiced() const { return !voice.empty() || madeUpVoice > 0.0f; }
};

// A text with its look (see RichText): {colour} {bold} {italic} {underline} {crossed_out} and {furigana}
// tags become spans, and {expressions} are filled in. Other tags ({pause}, {speed}...) go to `other`.
[[nodiscard]] RichText richText(std::string_view source, const Lookup& lookup, const Palette* palette, const TagHandler& other = {});

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

    // Player input.
    void nextLine();
    void choose(std::size_t index);
    void setUiHidden(bool hidden) { uiHidden_ = hidden && (wait_ == Wait::Say || wait_ == Wait::Menu); }
    void toggleSkip() { skip_ = !skip_; }
    void toggleAuto() { auto_ = !auto_; }
    // A command from a button, key or screen hook (anything that doesn't wait).
    void perform(const Command& command);
    // The developer tools' Play from this line (and the Screens tab's sample): plays on as fast as it goes
    // (picking the options that lead to command `target`) to the first line, choice, movie or screen
    // opened at a command where `there` is true, and shows it (a line whole, a movie's first image), with
    // the stage settled. When the story would go past `target` without doing it (an `if` it's in isn't
    // true, or the option it's under isn't picked), it goes on from `target` itself, with everything as
    // it is then. False if the story never gets there (or not within a second). Other movies are gone past.
    bool playTo(std::size_t target, const std::function<bool(std::size_t)>& there);
    // Set before start(): the story stops just before command `pc` (playTo() stops there, ready to do it,
    // whatever it is: a movie, a scene...), so a save made then starts with it.
    void stopBefore(std::optional<std::size_t> pc) { stopAt_ = pc; }
    // The developer tools' Stop here and Next line: the story asks `where` before each command it runs,
    // and stops just before one it says yes to, until goOn() (a save made meanwhile starts with it).
    void stopWhere(std::function<bool(std::size_t)> where) { stopWhere_ = std::move(where); }
    [[nodiscard]] bool stoppedThere() const { return wait_ == Wait::Stopped; }
    void goOn();
    // The screen the story was waiting for closed, with an answer ("answer TEXT") or not.
    void screenClosed(std::optional<Value> answer);
    // A menu covers the game (or no longer does): the voice line and movie wait meanwhile.
    void setCovered(bool covered);
    [[nodiscard]] bool playingMovie() const { return wait_ == Wait::Movie; }
    void playMovieRound(float dt); // the developer tools' preview at a movie: it plays, round and round, and the story waits

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
    // together with it. boxes(): the
    // textboxes with a line to show, the line on screen's last.
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
    [[nodiscard]] const PhoneData& phone() const;
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
    void updatePhone(float dt); // messages being typed arrive, calls ring (under menus too)
    // How much of a timed choice's time is left (1 down to 0), if one is on screen.
    [[nodiscard]] std::optional<float> timeLeft() const;
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
    // The story's variables in a save (to show a flowchart point's texts as they were there).
    [[nodiscard]] static Variables savedVariables(std::string_view state);

private:
    // (Chat: a chat scene waits for a message to come, or for a change to it; Call: a call rings; Clock: the player's
    // clock, setting time real)
    enum class Wait : std::uint8_t { None, Say, Menu, Timer, Background, Pause, Move, Screen, Movie, Reached, Stopped, Finished, Chat, Call, Clock };

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
    void addHistory(HistoryEntry entry);                                       // keeping setting history_length
    void addToPage(); // the line just said
    void execute(const Command& command, std::size_t here);
    void sayLine(const cmd::Say& c, std::size_t here, bool fast);       // ...a line said
    void openMenu(const cmd::Menu& c, std::size_t here);                // ...a choice
    void play(const cmd::Play& c, bool fast);                           // ...music, a voice or a sound
    void playMovie(const cmd::Movie& c, std::size_t here, bool fast);   // ...a movie
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
    void endMovie(); // the story goes on after it
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
    std::map<std::string, std::string, std::less<>> expressions_; // each character's latest expression
    std::vector<std::string> screens_;                // screens shown with "show screen"
    std::vector<PageLine> page_;                      // the lines said (a page part shows the newest that fit)
    // The phone, a call, a chat scene.
    PhoneData phone_;
    PhoneData& ownPhone() { return phone_; }
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
    struct Person {
        std::string name, character; // as shown, and a defined character's lower-case name
        SDL_Color colour{255, 255, 255, 255};
    };
    [[nodiscard]] Person person(const std::string& character, const std::string& sender) const; // the one a phone line names
    [[nodiscard]] Person personNamed(std::string_view name) const;                                // ...by the name the phone shows
    void addMessage(PhoneData& phone, MessageItem m);
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
    struct LineVoice {
        std::string file;        // as the script names it
        float volume = 1.0f;
        std::optional<bool> textFollows; // "voice x text_follows=yes" / "=no"
    };
    std::optional<LineVoice> pendingVoice_; // a voice line started for the next line
    LineVoice lineVoice_;                   // the line on screen's own voice line (file empty: none)
    bool voiced_ = false;                // the line on screen has its voice line
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

    std::unique_ptr<Movie> movie_;  // "movie NAME" playing
    std::string movieStandIn_;      // ...or the name of one that's missing (a card shows it for a moment)
    TextBlock movieLabel_;
    bool movieSkippable_ = true;
    std::vector<ChoiceItem> choiceItems_;
    [[nodiscard]] const cmd::Menu* menu() const; // the choice on screen (its command: a save made now repeats it)
    [[nodiscard]] std::optional<ChoiceItem> offer(const cmd::Choice& choice, int option) const; // shown now, if it is
    void listChoices(const cmd::Menu& menu); // the options to show now
    void letPass();                          // none of them picked: the story goes on after the choice
    bool menuPrompt_ = false;                // the line before it stays on screen meanwhile (it asks it)
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

} // namespace vn
