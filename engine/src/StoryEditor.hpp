#pragma once

// The editor (StoryEditor): the game's scripts as the developer tools' pages show them, and its changes to them.

#include "Script.hpp"
#include "DeveloperLanguage.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vn {

// The visual editor's view of a game: its script files as lines, the scenes in them, each
// scene's rows (a line each; the lines inside a choice or an "if" are under it), the characters and
// the game's settings (its setting lines). It changes lines and writes the files straight away. Lines
// it has no form for stay as they are, to be edited as text.
class StoryEditor {
public:
    // A Line is someone speaking, or narration (no one; in a chat, a message); a Visual shows or hides someone (or an image
    // or a movie), or puts one at the background; an Option is one of a choice's. On the phone (one
    // kind, Phone, to the person: its first box says which, see phoneWord): a Message (a chat's line that sends
    // something, types...), a Chat scene, and a Call (and its Missed lines). Other is any line without a form (a
    // mistake).
    enum class Kind : std::uint8_t { Line, Visual, Choice, Option, If, Else, Set, Jump, Sound, Wait, Time,
                                     Effect, Weather, Camera, Screen, Together,
                                     Message, Chat, Call, Missed, Other };
    // The names a field lists (Expressions: of the character in the field's context; Records: the
    // endings, achievements, tips or gallery images, by the context's word). Shown: characters, images
    // and movies; ImagesOrColours: images, movies and colours; Bases: the names images start with (aiko for
    // aiko_happy) and the atlases; Channels: what stop stops (the sound types, the named sounds, the files); FontNames: the fonts font lines
    // name; Keys: key, mouse and controller names; Settings: the players' settings sliders change;
    // Conditions: the variables, and the words screens' conditions know; ImageFiles: images/NAME.png.
    enum class Pick : std::uint8_t { None, Characters, Shown, Images, Sounds, Places, Scenes, VariableNames, Expressions, Screens,
                                     Records, Fonts, Styles, Colours, ImagesOrColours, Bases, Channels, FontNames, Keys, Languages,
                                     VoiceFolders, Settings, Controls, Conditions, ImageFiles, Animations, TimeWords, Weekdays, Textboxes,
                                     VisualTypes, Movies, SoundTypes };
    // Every box a form has is of a kind, defined once (kindOf): what it lists to pick from, the values it
    // suggests, the buttons it has instead of a box and what they say, and how its value is written. A
    // row says its box's kind and what its own empty box means, so boxes of a kind are the same in
    // every form.
    enum class Box : std::uint8_t {
        None,  // (only while a row is being made: every row says its kind)
        Words, // text that can be bold, coloured, paused...
        Text,  // plain text: a title, a question, a note, what a page says
        Name,  // one word that names something: a style, a character, a part
        Image, // an image, or a colour (every image's box takes either)
        Shape,   // an image alone, for its shape: where it's darkest (a wipe's)
        ImageFile, Colour, Sound, FontFile, Font, Style, Character, Shown, Expression, Scene, Screen, Textbox, Variable,
        VisualType, Still, Movie, Accepts, // (a visual's type; an image alone (a sub-image too) to show; a movie; what a type takes)
        SoundType, Follows, PlayStop,      // (a sound's type; which of the player's volumes a sound type follows; a sound line's action)
        Setting, Animation, Key, Place, Channel, Record, VoiceFolder, Control, Base, Language,
        Across, Down, Layer, Speed, Zoom, Opacity, Wipe, Volume, Percent, Rotation, Seconds,
        Pixels, Count, Number, PerSecond, LineSpacing, TextSpeed, Pause, Wind, // (numbers: a size, how many, a rate...)
        FontStyle, MadeUpVoice, VariableValue, Action, KeySlot, SaveMode,      // (lists of words)
        YesNo, YesNoStyle, YesNoSettings, YesNoSame, Easing, Plays,
        Condition, Fit, TextAppears, MouthMoves, ImageScaling, HoldToSkip, WindowFill, Mouths, NewMessages, ChatMessages,
        TimeKind, Clock, DayParts, TimeWhen, Weekday, TimeUnit, // (the game's time: its kind, clock and parts; a time line's when (a start's time
                                                                // of day too), a weekday or a date (a start's day), and what a length counts)
        MessageKind, PhoneLine, // (the phone's lines: which one, what a message does)
        Side, FaceCopies, FaceMouth, Align, AlignVertically, Operation, Joiner, ShowHide, Visibility, Sent, Position, Repeat, Scroll, Effects, WeatherKind,
        Edges, ContainerFit, Stack, List, // (a container's)
        InputChanges, InputAs,            // (what an input changes; one changing a variable: typed in or slid)
        ScreenAction, StoryAction, SoundAction, SaveAction, SettingAction, ExtrasAction, PhoneAction, ToolsAction, // (an action's kind, by what it's about)
        SettingChoice, GameLanguage, StoryWhileOpen, // (what a setting goes to: the Library's Settings' own options for it, as a list; a language)
    };
    struct BoxKind {
        Box box = Box::None;
        Pick pick = Pick::None;
        ToolsSuggestions suggest;
        ToolsChoices choices;
        ToolsLabels labels;
        bool rich = false;     // text with bold, colours, pauses... (buttons for them under the box)
        bool percent = false;  // written as one (50%), however it's typed (0.5, 50)
        bool wheel = false;    // a colour's box: the colour as a square, and a wheel with the code to type when it opens
        bool images = false; // its list shows the images, as cells
        bool listen = false;   // its list's entries, and the box, have a button to hear them
        bool wide = false;     // its names can be long (a file's, an image's): never half a row
    };
    [[nodiscard]] static const BoxKind& kindOf(Box box);
    struct Field {
        std::string name;
        std::string value;
        std::string label;        // what the form calls it (else its name)
        Pick pick = Pick::None;
        Box box = Box::None;      // its kind (applyBox gives it the kind's pick, suggestions and buttons)
        std::string choices; // the words it can be, as buttons (else it's typed); "_" for a space
        std::string hint;    // shown while it's empty
        std::string labels;  // what the buttons say (else the words themselves)
        bool multi = false;       // several of the choices (or names) at once (the value: them, with spaces)
        std::string context;      // for the names it lists
        std::string suggest; // values it often has, listed with the names: "|" between them
        bool hidden = false;      // words the form has no box for, kept as they are
        bool rich = false;        // text that can be bold, coloured... (buttons for that show under it)
        bool typesOut = false;    // ...and that types out, a story line's words: their speed, pauses, how they appear, not waiting
        std::string group;   // the heading it starts (the fields after it are under it too, until the next)
        std::string usual;   // the choice it is when none is written (in one box with a list, said in grey: as any box's default)
    };
    struct Scene {
        std::size_t file = 0, line = 0; // its scene line
        std::size_t end = 0;            // after its last line
        std::string name;
    };
    struct Row {
        std::size_t line = 0; // in the scene's file
        std::size_t end = 0;  // after its last line, and those of the rows inside it
        int depth = 0;        // 0: right under the scene line
        Kind kind = Kind::Other;
        std::string text;     // without the indentation
    };
    struct Character {
        std::size_t file = 0, line = 0;
        std::string name;
    };
    // An atlas as its lines have it: its name, the image it's cut from (its image=, else its name), its expressions (its
    // own, else its same_names_as= atlas's) and the images its eyes and mouth are drawn from (all lower-case).
    struct AtlasLines {
        std::string name, image;
        std::vector<std::string> expressions, layers;
    };

    void load(const std::string& root); // the game folder, with a slash
    void clear() { *this = {}; }
    [[nodiscard]] bool loaded() const { return !files_.empty(); }
    [[nodiscard]] const std::vector<Scene>& scenes() const { return scenes_; }
    [[nodiscard]] const std::vector<Row>& rows(const Scene& scene) const; // (kept as of this version: a reference, not to hold across a change)
    // A scene a character's chat= or call= names ("chat", "call"; else ""): it plays when the player opens their chat or calls them.
    [[nodiscard]] std::string_view phoneSceneOf(const Scene& scene) const;
    [[nodiscard]] const std::vector<Character>& characters() const;
    // The love interests (characters with love=), in their order: their names and their love's variables (mika.love);
    // what picking an option adds to one's love (its set line, first in it: set mika.love to mika.love + 2; 0: none), and
    // that changed (put first in it, before a jump would leave it; 0: taken out).
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> loveInterests() const;
    [[nodiscard]] int loveIn(const Scene& scene, const Row& option, std::string_view variable) const;
    void setLove(const Scene& scene, const Row& option, const std::string& variable, int amount, const std::string& key);
    [[nodiscard]] const std::vector<AtlasLines>& atlases() const; // (in the order they're written)
    [[nodiscard]] std::vector<std::string> names(Pick pick, std::string_view context = {}) const;
    void timesToCheck(std::vector<Field>& fields) const; // (a check of the part of the day, the weekday or the month: those, in its value's list)
    [[nodiscard]] const std::vector<std::string>& variables() const; // the ones the scripts change, or an input or a slider sets
    [[nodiscard]] const std::string& path(std::size_t file) const { return files_[file].path; } // "scripts/main.vnscript"

    // Rows as forms: the fields of a row (they can change with its values: a hidden character has no
    // place), and the line they make (from the fields by name).
    [[nodiscard]] static std::string_view kindName(Kind kind);
    [[nodiscard]] static bool asBackground(std::string_view line); // a show line making what it shows the background
    [[nodiscard]] std::string visualKindOf(std::string_view name) const; // "character", a visual line's type, "image" or "movie" (in that order); none or unknown: ""
    [[nodiscard]] int typeLayer(std::string_view type) const;            // a visual type's layer (every game's: visualTypeLayer)
    [[nodiscard]] std::string namedVisualType(std::string_view name) const; // a visual named by a visual line: its type (else image); none: ""
    [[nodiscard]] std::string typeAccepts(std::string_view type) const;     // what a visual type takes: "character", "image", "movie", "colour", with spaces
    // A type line a sound type's (it says which volume it follows, or how it plays: follows= one_at_a_time= loop= volume=),
    // not a visual type's.
    [[nodiscard]] static bool soundTypeLine(std::string_view line);
    [[nodiscard]] bool typeNameTaken(std::string_view name) const; // (a type's: visual or sound, every game's or the game's own)
    [[nodiscard]] bool isSoundType(std::string_view type) const;            // every game's (music, sound, voice), or the game's own
    [[nodiscard]] std::string namedSoundType(std::string_view name) const;  // a sound named by a sound line: its type (else sound); none: ""
    // A sound type's setting (follows, one_at_a_time, loop, volume): its line's, else as every game has it (none: "").
    [[nodiscard]] std::string soundTypeOption(std::string_view type, std::string_view key) const;
    // A sound's own setting (plays, loop, volume, fade, fade_out) as its sound line has it (none: "").
    [[nodiscard]] std::string soundOption(std::string_view name, std::string_view key) const;
    // What a sound by `name` is: a named sound's type, a song (music/...): music, a file in sounds/: sound; else "".
    [[nodiscard]] std::string soundKindOf(std::string_view name) const;
    // A colour's box: a colour (or one fading down to another) shown as a type taking colours that isn't the background
    // (the whole stage). `shows`: what's shown (a visual named by a visual line: its shows=, else its name).
    [[nodiscard]] bool colourBox(std::string_view shows, std::string_view type) const;
    // Where a visual comes when its line doesn't say (`key`: "at" across or "y" down, as the scripts write them): a
    // character's or a visual named by a visual line's own (its at= and y=), else as every game has them (the middle,
    // characters standing at the bottom).
    [[nodiscard]] std::string comes(std::string_view who, std::string_view key, bool character) const;
    // A character's or a visual named by a visual line's own setting of how it comes (zoom=, flip=, animation=...: its
    // line's, as written; none: "").
    [[nodiscard]] std::string visualOption(std::string_view who, std::string_view key, bool character) const;
    std::size_t filesGeneration = 0; // the developer tools' look at the files (images and movies imported, renamed, gone)
    // The phone's lines are one kind to the person (Phone), each a kind of its own here: the word a phone line's
    // first box ("What") has for it (none: not one of them), the kind a word is (Other: none), and the lines
    // one starts as (with a first line inside it where it holds lines: a chat's, a call's...).
    [[nodiscard]] static std::string_view phoneWord(Kind kind);
    [[nodiscard]] static Kind phoneKind(std::string_view word);
    [[nodiscard]] static std::vector<std::string> phoneLines(Kind kind);
    [[nodiscard]] std::vector<Field> fields(const Row& row) const;
    [[nodiscard]] std::string lineOf(Kind kind, const std::vector<Field>& fields) const;
    [[nodiscard]] std::string summary(const Row& row) const; // as the list shows it
    [[nodiscard]] std::vector<Field> fields(const Character& c) const;

    // Changes, each written to its file at once. Changes with the same `key` are one step for undo
    // (typing into one field).
    void setText(const Scene& scene, const Row& row, const std::string& text, const std::string& key);
    // Its line, and the lines inside it (as written under it: indented once) in place of the ones there when
    // those are blank, as a new row's are (else they stay). One step for undo.
    void setRowLines(const Scene& scene, const Row& row, const std::string& text, const std::vector<std::string>& inside, const std::string& key);
    // Adds lines after `row` (and the rows inside it), or at the end of what's inside it; without
    // a row, at the end of the scene. Gives the first new line.
    std::size_t add(const Scene& scene, const Row* row, bool inside, const std::vector<std::string>& lines);
    void remove(const Scene& scene, const Row& row);
    // Several rows (with what's inside them) at once, one step for undo: taken out; moved to before row
    // `slot`, `depth` levels in, in their order; or copied (their lines as text, each without the
    // indentation it has).
    void removeRows(const Scene& scene, const std::vector<std::size_t>& lines);
    std::vector<std::size_t> moveRowsTo(const Scene& scene, const std::vector<std::size_t>& lines, std::size_t slot, int depth); // (their new lines)
    [[nodiscard]] std::string textOfRows(const Scene& scene, const std::vector<std::size_t>& lines) const;
    // Whether a row can go before row `slot` (rows.size(): after the last) `depth` levels in: options
    // only right under a choice, and nothing else there; never into itself.
    [[nodiscard]] bool fits(const std::vector<Row>& rows, std::size_t item, std::size_t slot, int depth) const;
    // Moves a row (with what's inside it) there, as far in as `depth`: its new line.
    std::optional<std::size_t> moveRowTo(const Scene& scene, const Row& row, std::size_t slot, int depth);
    // Moves a row (with what's inside it) to the end of another scene.
    void moveRowToScene(const Scene& scene, const Row& row, const Scene& to);
    std::size_t addScene(std::size_t file); // gives its index in scenes()
    // A new name for a scene (the jumps to it follow); false if it can't be one.
    bool renameScene(std::size_t scene, const std::string& name, const std::string& key);
    void removeScene(std::size_t scene);
    // Moves a scene before scene `to` (or after the last one), within its file: its new index, or
    // none when it can't go there.
    std::optional<std::size_t> moveScene(std::size_t scene, std::size_t to);
    std::size_t addCharacter(); // gives its index in characters()
    bool setCharacter(const Character& c, const std::vector<Field>& fields, const std::string& key); // (false: its new name is another character's)
    void removeCharacter(const Character& c);
    [[nodiscard]] std::string newCharacterName() const;                     // what addCharacter calls the next one (Someone, Someone 2...)
    [[nodiscard]] std::size_t lineCount(std::size_t file) const { return files_[file].lines.size(); }
    std::optional<std::size_t> moveCharacter(std::size_t c, std::size_t to); // before character `to` (or after the last), in its file
    // Finding: the lines with `what` in them (any case), in every script file.
    struct Found {
        std::size_t file = 0, line = 0;
        std::string text;
    };
    [[nodiscard]] std::vector<Found> find(std::string_view what) const;
    [[nodiscard]] std::optional<std::string> lineText(std::string_view path, std::size_t line) const; // a line (trimmed) of the script at `path`
    [[nodiscard]] const std::vector<std::string>* fileLines(std::string_view path) const; // all of them, as they are (none: no such script)
    [[nodiscard]] bool mentions(std::string_view word) const; // some script line has the word (whole, case aside)
    // What a line is part of, as the found lines say: its scene, its screen ("screen title"), a
    // character, a definition ("style button"), Settings, or else its file.
    [[nodiscard]] std::string placeOf(std::size_t file, std::size_t line) const;
    // A variable's lines (set, if, {name} in words, screens' when= and variable=...), and a new name for it in
    // all of them (one step for undo): false when it can't be one.
    [[nodiscard]] std::vector<Found> uses(std::string_view variable) const;
    // Every line that uses it changes too: how many (its own variable line not counted); none when the name can't be.
    std::optional<std::size_t> renameVariable(const std::string& from, const std::string& to);
    // The layout's lines (screens' parts, styles, animations' steps) with `scale` done to them (one
    // step for undo, with others of the same key).
    void scaleLayout(const std::function<std::string(const std::string&)>& scale, const std::string& key);
    // A new name for a file in images/, sounds/, movies/ or fonts/ (`from` and `to` with their
    // extension), and the lines that use it changed to match (a character's images: its
    // expressions). One step for undo, the file's name too. How many lines changed; none when it
    // can't have that name.
    std::optional<std::size_t> renameAsset(std::string_view folder, const std::string& from, const std::string& to);
    // The scene ends by jumping to `to`: a jump or end it ends with already changes, else one is added.
    // True when one was there.
    bool jumpAtEnd(std::size_t scene, const std::string& to);
    // Each character's name and the name its images start with (aiko_happy is "show aiko happy").
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> imageBases() const;
    // The name a character's images start with: its image=, else its name as the game makes one of it (Mr. Tanaka:
    // mr_tanaka); for a name that's no character's, the same as for one without image=.
    [[nodiscard]] std::string imageBaseOf(std::string_view character) const;
    // Where the story goes from each scene: its jumps, by name.
    struct Link {
        std::size_t from = 0;
        std::string to;
        std::size_t line = 0; // (its line in the scene's file)
    };
    [[nodiscard]] const std::vector<Link>& links() const;
    // A jump's (or call's) line taken out.
    void removeLink(const Link& link);
    // Where the Map shows the scenes put somewhere by hand (by name; game/.map.txt, which packs leave
    // out, as "name x y"); the others are laid out by themselves. Renaming a scene keeps its place.
    [[nodiscard]] std::optional<SDL_FPoint> mapPlace(std::string_view scene) const;
    void setMapPlace(const std::string& scene, std::optional<SDL_FPoint> at);
    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
    // A script changed (or one came or went) since the editor read or wrote it: another program edited it.
    [[nodiscard]] bool changedOutside() const;

    // Copying and pasting: lines as text (without the indentation they share), and pasted back as a
    // new scene (after scene `after`, under a name of its own), character (after character `after`)
    // or screen. Each gives the new one's index.
    [[nodiscard]] std::string textOf(std::size_t file, std::size_t from, std::size_t until) const;
    std::size_t pasteScene(std::size_t after, std::vector<std::string> lines);
    std::size_t pasteCharacter(std::size_t after, const std::string& line);

    // The script files: scripts/NAME.vnscript, each but main.vnscript read by an include line in it.
    [[nodiscard]] std::size_t fileCount() const { return files_.size(); }
    [[nodiscard]] std::string fileName(std::size_t file) const; // "story" for scripts/story.vnscript
    // One the Story page lists: with scenes, or nothing yet (not one of just screens or definitions).
    [[nodiscard]] bool storyFile(std::size_t file) const;
    bool renameFile(std::size_t file, const std::string& name, const std::string& key); // (its include lines follow)
    void removeFile(std::size_t file); // to the Trash, with what's in it (undo brings it back); its include lines go too
    // Moves a scene to another file, before scene `to` there (or after its last): its new index.
    std::optional<std::size_t> moveSceneTo(std::size_t scene, std::size_t file, std::size_t to);

    // Screens: "screen NAME" lines and the parts indented under them, one a line.
    struct Screen {
        std::size_t file = 0, line = 0, end = 0; // its screen line, and after its last part
        std::string name;
    };
    // A screen's line or a part's: its kind ("screen" for the screen's own), the text or word after
    // it, its settings (name=value) as written, and the note at its end, if any.
    struct Part {
        std::size_t line = 0, end = 0; // its line, and after the parts in it (indented under it)
        int depth = 0;                 // how many parts it's in
        std::string kind, text;
        bool quoted = false; // the text is in quotes (else a word: the screen's name, a key's...)
        std::vector<std::pair<std::string, std::string>> options;
        std::vector<std::string> words; // anything else, kept as written
        std::string note;
        [[nodiscard]] std::string option(std::string_view name) const;
        void set(std::string_view name, const std::string& value); // (an empty one goes)
    };
    [[nodiscard]] const std::vector<Screen>& screens() const { return screens_; }
    [[nodiscard]] Part part(const Screen& screen, std::size_t line) const;
    // A button for a screen put in a menu's box of buttons (the ready-made title screen's, the pause menu's...), as its
    // others are (a copy of the one before it, else after it, doing order[at] with `more`), among them in `order` (what
    // the menu's buttons do, in their order). Part of the change that made the screen it opens: no step of its own for
    // undo. False: no box of those buttons (the menu made otherwise), or one does it already.
    bool addMenuButton(const Screen& menu, const std::vector<std::string>& order, std::size_t at, const std::string& label, const std::string& more);
    // Lines put in among a screen's parts at `line` (one of its file's, in it), indented as `like` is (their own
    // indentation kept under the first's), what they use that the game lacks (`needs`) above the screen. Part of the
    // change that made the screen they open, as addMenuButton: no step of its own for undo.
    void insertParts(const Screen& screen, std::size_t line, const Part& like, const std::vector<std::string>& text, const std::vector<std::string>& needs);
    [[nodiscard]] const std::vector<Part>& parts(const Screen& screen) const; // (not the screen's own line)
    [[nodiscard]] static std::string lineOf(const Part& part);
    void setPart(const Screen& screen, const Part& part, const std::string& key);
    std::size_t addPart(const Screen& screen, const Part* into, const std::string& text, const std::vector<std::string>& needs = {}); // last in that part (or the screen): its line (`needs`: look lines it uses, put above the screen)
    // After `after` (or at the end), the lines' own indentation kept under the first's: the first's line.
    std::size_t addParts(const Screen& screen, const Part* after, const std::vector<std::string>& text);
    [[nodiscard]] std::vector<std::string> partLines(const Screen& screen, const Part& part) const; // it and the parts in it, as lines
    std::size_t pasteScreen(std::vector<std::string> lines); // a copy, under a name of its own: its index
    void removePart(const Screen& screen, const Part& part); // (and the parts in it)
    void removeParts(const Screen& screen, const std::vector<std::size_t>& lines); // (one step for undo)
    // Before part `to` (or after the last), `depth` parts in, with the parts in it: its new line.
    std::optional<std::size_t> movePart(const Screen& screen, const Part& part, std::size_t to, int depth);
    // A new screen and the lines it needs (styles, colours, controls), at the end of
    // scripts/screens.vnscript (made, and included from main.vnscript, if it isn't there). Gives its index.
    std::size_t addScreen(const std::vector<std::string>& needs, const std::vector<std::string>& lines);
    bool renameScreen(std::size_t screen, const std::string& name, const std::string& key); // (its uses follow)
    void removeScreen(std::size_t screen); // (and the keys and buttons that only open it)
    std::optional<std::size_t> moveScreen(std::size_t screen, std::size_t to); // before screen `to` (or after the last), in its file
    [[nodiscard]] bool defines(std::string_view what, std::string_view name) const; // a "style NAME" line, "colour NAME"...
    [[nodiscard]] std::vector<std::string> defined(std::string_view what) const;     // the names of those

    // Things listed once by name, outside the scenes: "colour NAME ...", "style", "font", "place", "atlas" and
    // "animation" (with the lines indented under them), "sub-image", "variable", "gallery", "ending", "song",
    // "caption", "achievement", "tip" and "control" lines. A new one goes after the last of its kind (or in
    // main.vnscript, before its first scene); renaming one follows where it's used (styles, colours, fonts, places,
    // atlases, keys, and the story's endings, achievements and tips).
    struct Definition {
        std::size_t file = 0, line = 0, end = 0; // its line, and after the lines under it
        std::string name;
    };
    [[nodiscard]] const std::vector<Definition>& definitions(std::string_view kind) const;
    [[nodiscard]] std::vector<std::string> linesOf(const Definition& d) const; // (without their indentation)
    // A visual type's line: the game's own type's, or one of every game's saying where its visuals come (none: none).
    [[nodiscard]] std::optional<Definition> typeLine(std::string_view type) const;
    [[nodiscard]] std::string typeOption(std::string_view type, std::string_view key) const; // (its key=, as written; none: "")
    // A definition as a form, and the lines a form makes (an atlas: its line and the ones under it; an
    // animation: its line, with how often it plays: the lines under it are the timeline's).
    [[nodiscard]] std::vector<Field> fields(std::string_view kind, const Definition& d) const;
    [[nodiscard]] std::vector<std::string> linesOf(std::string_view kind, const std::vector<Field>& fields) const;
    // The ending reached as a scene ends (the ending whose scene= is it; none: ""), and that one made it: the one that
    // was loses it (an ending's reached by one scene; none: no ending is).
    [[nodiscard]] std::string endingOf(std::string_view scene) const;
    void setEnding(std::string_view scene, const std::string& ending, const std::string& key);
    // An animation's lines as the Library's timeline shows them: each change a bar on the row of what
    // it changes (timelineRow), from when it begins for as long as it takes, with what it goes to as
    // written (left=5% is "5% left" on the Across row) and its easing; and one time through. Not `ok`:
    // a line it can't read (then it's only shown). stepsOf(): the lines again, in the order the bars
    // begin, the ones beginning and ending together on one line ("over" when one begins as the line
    // before ends, else "from ... to" or "at"; a "wait" at the end for any time after the last).
    struct Bar {
        std::string key, value; // what it changes and to what, as written: x=home, left=5%, opacity=40%
        float start = 0.0f, seconds = 0.0f;
        std::string ease; // (inout, linear...: a fade's is linear unless it says)
        std::string shape; // a wipe's image (shape=): where it shows first; none: from the left
    };
    struct Timeline {
        std::vector<Bar> bars;
        float length = 0.0f;
        bool ok = true;
    };
    static constexpr std::size_t kTimelineRowCount = 8; // across, up or down, opacity, zoom, rotation, colour, fade, wipe
    [[nodiscard]] static std::size_t timelineRow(std::string_view key); // (kTimelineRowCount: none)
    [[nodiscard]] static Timeline timelineOf(std::span<const std::string> steps);
    [[nodiscard]] static std::vector<std::string> stepsOf(const Timeline& timeline);
    std::size_t addDefinition(std::string_view kind, const std::vector<std::string>& lines); // gives its index
    // One copied, pasted (or duplicated) after `after` (none: where a new one goes), under its name if no other one
    // has it, else numbered (title_2): its index (lines that aren't one of `kind`: none, the count).
    std::size_t pasteDefinition(std::string_view kind, const Definition* after, const std::vector<std::string>& lines);
    void setDefinition(const Definition& d, const std::vector<std::string>& lines, const std::string& key);
    void removeDefinition(const Definition& d);
    std::optional<std::size_t> moveDefinition(std::string_view kind, std::size_t index, std::size_t to); // in its file
    bool renameDefinition(std::string_view kind, std::size_t index, const std::string& name, const std::string& key);
    [[nodiscard]] std::string root() const { return root_; }

    // The game's settings: the setting lines in main.vnscript. A setting's value as written (without
    // quotes), and its line; none: it has no line, so it has its default.
    [[nodiscard]] bool hasMain() const;
    [[nodiscard]] std::optional<std::string> setting(std::string_view name) const;
    [[nodiscard]] std::optional<std::size_t> settingLine(std::string_view name) const;
    [[nodiscard]] bool isSetting(std::size_t line) const; // a setting line in main.vnscript
    // A new value (in quotes if `quoted`); an empty one takes the line out.
    void setSetting(std::string_view name, const std::string& value, bool quoted, const std::string& key);

    [[nodiscard]] std::size_t version() const { return version_; } // goes up with every change
    [[nodiscard]] std::size_t fileStamp(std::size_t file) const { return files_[file].stamp; } // new with every change to that file's lines
    [[nodiscard]] const std::string& problem() const { return problem_; }        // a file that couldn't be written

private:
    struct File {
        std::string path; // from the game folder
        std::vector<std::string> lines;
        SDL_Time modified = 0; // when the editor read or wrote it
        std::size_t stamp = 0; // new as it's read and with each change to its lines (what's kept from them is found again)
    };
    using Snapshot = std::vector<std::pair<std::string, std::vector<std::string>>>; // every file's path and lines
public:
    // Files moved (from, to: full paths), renamed in the game folder, to the Trash or from it; `from`
    // empty: brought in from elsewhere (imported). One step for undo, which moves them back (an
    // imported one to the Trash).
    using Moves = std::vector<std::pair<std::string, std::string>>;
    void movedFiles(Moves moves);

private:
    struct Step {      // what undo brings back: the scripts, and the files moved since
        Snapshot files;
        Moves renamed;
    };
    static Moves undoMoves(const Moves& moves); // (each done backwards: what to do again for redo)
    static Moves redoMoves(const Moves& moves);
    [[nodiscard]] Snapshot snapshot() const;
    void restore(const Snapshot& snapshot);
    void include(std::string_view name); // an include line in main.vnscript, if it has none for it
    void rescan();
    void remember(const std::string& key);
    // A rename typed on in the same box (its `key`): the files back as they were before the typing began, so the name
    // goes from the old one to the one typed now, never through the names typed on the way (the lines one of those
    // already meant would go with it: "saved" on the way to "saved_games").
    void typedOn(const std::string& key);
    void write(std::size_t file);
    void eraseLines(std::size_t file, std::size_t from, std::size_t until); // (one step for undo, written and read again)
    std::size_t putDefinition(std::string_view kind, const Definition* after, std::vector<std::string> put); // (addDefinition's and pasteDefinition's)
    void writeAll();
    [[nodiscard]] std::size_t blockEnd(std::size_t file, std::size_t label) const;
    // Moves lines [from, until) of a file to before line `at` (with a blank line on each side when
    // they're a scene or a screen): gives where they start now.
    std::size_t moveLines(std::size_t file, std::size_t from, std::size_t until, std::size_t at, bool spaced);
    [[nodiscard]] bool isCharacter(std::string_view name) const;
    // Who a line names and how they look: "Aiko happy" or "Aiko (happy)" (a character's), or anyone's name.
    [[nodiscard]] std::pair<std::string, std::string> speakerOf(std::string_view who) const;
    // A chat's phone line as its form has it ("me" the player): whether it's the player's, who sent it and
    // how they look, what it does and with what.
    struct PhoneLine {
        bool mine = false;
        std::string who, expression, does = "says", text, file, seconds;
    };
    [[nodiscard]] PhoneLine phoneLineOf(std::string_view text) const;
    [[nodiscard]] std::vector<Field> rowFields(const Row& row) const; // (fields(), but for the phone's first box)
    void lovesToCheck(std::vector<Field>& fields) const; // (with love interests: who's loved most, to check, in a condition's boxes)
    [[nodiscard]] bool hasScene(std::string_view name) const; // (by name, in any case)
    [[nodiscard]] bool hasScreen(std::string_view name) const;
    void renameSpeaker(const std::string& from, const std::string& to);

    std::string root_;
    std::vector<File> files_;
    std::vector<Scene> scenes_;
    std::vector<Screen> screens_;
    std::vector<Step> undo_, redo_; // the files before each change, and before each undo
    std::string lastKey_;
    std::size_t version_ = 0, stamps_ = 0;
    std::string problem_;
    std::pair<std::string, std::size_t> removed_; // the setting whose line went last, and where it was
    std::map<std::string, SDL_FPoint, std::less<>> mapPlaces_; // (lower-case names)
    void saveMapPlaces() const;
    mutable std::pair<std::size_t, std::vector<std::string>> variables_{SIZE_MAX, {}}; // (at a version)
    // What each file names, by file: kept while its lines are the same (its stamp), so a change is looked for in its file
    // alone: its variables (lower-case, in the order met), and its definitions by kind (each line not indented, by its
    // first word, lower-case).
    struct KeptNames {
        std::size_t stamp = SIZE_MAX;
        std::vector<std::string> variables;
        std::map<std::string, std::vector<Definition>, std::less<>> definitions;
    };
    mutable std::vector<KeptNames> fileNames_;
    const KeptNames& namesIn(std::size_t file) const;
    // What's read from the lines often, kept from a version of them until they change.
    mutable std::pair<std::size_t, std::vector<Character>> characters_{SIZE_MAX, {}};
    mutable std::pair<std::size_t, std::vector<AtlasLines>> atlases_{SIZE_MAX, {}};
    struct VisualNames { // (what visualKindOf looks in: at a version and a look at the files)
        std::size_t version = SIZE_MAX, files = SIZE_MAX;
        std::vector<std::string> images, movies;
    };
    mutable VisualNames visualNames_;
    mutable std::pair<std::size_t, std::vector<Link>> links_{SIZE_MAX, {}};
    mutable std::pair<std::size_t, std::map<std::string, std::size_t, std::less<>>> settingLines_{SIZE_MAX, {}}; // each setting's line
    mutable std::map<std::string, std::pair<std::size_t, std::vector<Definition>>, std::less<>> definitions_;    // by kind
    // A scene's rows, by its file and line: kept while its file's lines are the same (its stamp), and so is what its lines
    // are (a chat's, for a character's chat= scene); and the characters' chat= and call= scenes (lower-case), at a version.
    struct KeptRows {
        std::size_t stamp = SIZE_MAX;
        Kind top = Kind::Other;
        std::vector<Row> rows;
    };
    mutable std::map<std::pair<std::size_t, std::size_t>, KeptRows> rows_;
    mutable std::pair<std::size_t, std::map<std::string, std::string_view, std::less<>>> phoneScenes_{SIZE_MAX, {}};
    mutable std::map<std::pair<std::size_t, std::size_t>, std::pair<std::size_t, std::vector<Part>>> parts_;     // by a screen's
    // Every line through `change` (its file, its number and its text; the text it gives back replaces
    // it), each file with a change written once (`also`: one written whatever happens, a line of it
    // changed already). Gives how many lines changed.
    std::size_t changeLines(const std::function<std::optional<std::string>(std::size_t, std::size_t, const std::string&)>& change,
                            std::optional<std::size_t> also = std::nullopt);
    // A scene, screen, character or definition moved before another of its kind (`to` == their count:
    // after the last) in its file; `endOf`: after one's last line; `again`: them all, once it's moved.
    // Gives where it is then.
    template <class T, class EndOf, class Again>
    std::optional<std::size_t> moveRecord(const std::vector<T>& all, std::size_t index, std::size_t to, bool spaced, EndOf endOf, Again again);
};

// A field's kind applied: its pick, suggestions (the kind's, then its own), buttons and their labels (the
// kind's words in the row's own order), and whether it's rich text.
void applyBox(StoryEditor::Field& f);
// An amount as a percent kind writes it: 50% from 0.5, 50 or 50% (typed into its box).
[[nodiscard]] std::string percentText(std::string_view value);
// ...and one read from a line, as the game reads it: a number without % is a share of one (2: 200%, 0.5: 50%).
[[nodiscard]] std::string percentRead(std::string_view value);

// (a template the editor's files use with their own lambdas: here, where they all see it)
template <class T, class EndOf, class Again>
std::optional<std::size_t> StoryEditor::moveRecord(const std::vector<T>& all, std::size_t index, std::size_t to, bool spaced, EndOf endOf, Again again) {
    const T& it = all[index];
    std::optional<std::size_t> at; // the line it goes before
    if (to < all.size() && all[to].file == it.file) at = all[to].line;
    else if (to > 0 && all[to - 1].file == it.file) at = endOf(all[to - 1]);
    if (!at || to == index || to == index + 1) return std::nullopt;
    remember({});
    moveLines(it.file, it.line, endOf(it), *at, spaced);
    write(it.file);
    rescan();
    const auto now = again();
    for (std::size_t i = 0; i < now.size(); ++i)
        if (now[i].file == it.file && lower(now[i].name) == lower(it.name)) return i;
    return std::nullopt;
}

} // namespace vn
