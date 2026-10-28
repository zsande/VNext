#pragma once

// Developer mode: the editor's view of the game's scripts (StoryEditor, in StoryEditor.cpp), and what
// the developer tools keep from frame to frame (DeveloperTools). Developer.cpp and a file for each tab
// (Developer*.cpp) draw them.

#include "Game.hpp"
#include "Screens.hpp"
#include "Script.hpp"
#include "System.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace vn {

class Engine;
class DeveloperUi;

// The editor's layout: the measurements its forms, lists, popups and the colour wheel share, for a
// 720-pixel-high window (each is scaled by k, the window's height over 720, where it's used). One
// place to change them, so they stay the same everywhere.
namespace layout {
inline constexpr float titleH = 40;          // a form's title row (its title, and a button at its right)
inline constexpr float boxH = 32;            // a box typed into or picked from
inline constexpr float rowGap = 8;           // between a form's rows
inline constexpr float labelPart = 0.3f;     // a form's labels: this much of its width
inline constexpr float partLabelPart = 0.4f; // a screen part's form: its labels are longer
inline constexpr float buttonH = 30;         // a button on its own (+ New, Play, As text...)
inline constexpr float toggleW = 96;         // the As text / As a form button
inline constexpr float barButtonH = 26, barButtonGap = 4; // a bar of small buttons (the text format bar, the add-a-line bar, a preview's choices)
inline constexpr float swatch = 20;          // a colour's square in its box
inline constexpr float popupPad = 4;         // around and between what a list shows
inline constexpr float itemH = 28;           // a row of a list
inline constexpr float cellW = 92, cellH = 74;            // a picture in a list: its cell
inline constexpr float listW = 260, picturesW = 400;      // a list's least width: names, or pictures
inline constexpr float listMaxH = 320, wheelMaxH = 440;   // a list's most height: names or pictures, or with the wheel over them
inline constexpr float codeH = 28;           // the colour's code box over the wheel
inline constexpr float wheelD = 150;         // the wheel's diameter
inline constexpr float barW = 16, barGap = 8; // the value and alpha bars beside it, and the gaps between
inline constexpr float boxText = 15, labelText = 13, titleText = 18; // text sizes: in a box, its label, a form's title
// The window: its bar, its panels and their lists.
inline constexpr float barH = 48;            // the bar with the tabs and the tools
inline constexpr float margin = 16;          // around the panels
inline constexpr float tabPad = 28, toolGap = 6, tabText = 16, toolText = 15; // a tab or tool: its label and this much, the gap between tools
inline constexpr float headingH = 22, headingText = 14; // a panel's heading ("Scenes", "Library"...) over its list
inline constexpr float gap = 6;              // between a list and its button, between buttons, and the like
inline constexpr float listItemH = 30;       // a row of a panel's list
inline constexpr float listIndent = 18;      // a row inside another (an option in its choice)
inline constexpr float listPad = 10, listText = 15; // a row's text: in from its left, and its size
inline constexpr float crossW = 24, crossArmedW = 64; // a row's x, and once clicked (to click again)
inline constexpr float markW = 4;            // a row's mark at its left (a mistake, or never reached)
// Buttons.
inline constexpr float smallButtonH = 22;    // (Save as a text file, Audio off, a jump's x)
inline constexpr float mediumButtonH = 28;   // (Try it, the Map's + New scene, The screen or the part)
inline constexpr float bigButtonH = 34;      // (Make it the game's own, the first + New scene)
inline constexpr float wideButtonW = 120;    // the least for: Play, Replace..., Import... (DeveloperUi::buttonWidth)
inline constexpr float widerButtonW = 160;   // the least for: + Font, + Sub-image, Go to the line
inline constexpr float optionButtonW = 140;  // the least for: + Option, + Answer, + Else, the Map's + New scene
inline constexpr float listenW = 64;         // the voice row's Play
inline constexpr float buttonText = 14, smallButtonText = 13;
inline constexpr float buttonPad = 24;       // a button in a row of them: its label and this much
inline constexpr float choicePad = 16;       // a choice's button: its label and this much
inline constexpr float previewButtonW = 80;  // the least for: Preview on, Audio off, Reset
inline constexpr float previewHeadH = 28;    // the preview's title row, with those buttons (all that's left of it when it's off)
// Texts.
inline constexpr float noteText = 14;        // notes, help and problems
inline constexpr float smallText = 13;       // small labels
inline constexpr float lineText = 16;        // a plain line of text, and a button's unless it says
// A form's groups (headings that fold), and a label over its box.
inline constexpr float groupHeadH = 24, groupStep = 26, groupGap = 6, groupIndent = 14, groupText = 15;
inline constexpr float labelAboveH = 20;
inline constexpr float pairBoxW = 110;       // two boxes on a row: each at least this wide (else each has a row)
inline constexpr float runBoxW = 80;         // an action's boxes on a row: each at least this wide (else each has a row)
inline constexpr float pairLabelPart = 0.28f; // the second's label: at most this much of the room after the first's, on two lines past that
// The Settings tab, the Translate tab's table, the Map and the log.
inline constexpr float settingRowH = 42, settingHeadH = 44, settingNoteH = 60, settingHeadNoteH = 22, settingOtherH = 24;
inline constexpr float settingLabelW = 200, settingLabelPart = 0.22f, settingFieldW = 340, settingFieldPart = 0.32f, settingHelpGap = 16;
inline constexpr float settingHeadText = 17, settingLabelText = 15; // a group's heading, a setting's label
inline constexpr float mapPadX = 20, mapPadY = 40, mapNoteY = 22;   // the Map: where its grid starts, and its note over the unreached column
inline constexpr float tableRowH = 40, languageW = 360, onlyW = 170;
inline constexpr float nodeW = 170, nodeH = 36, nodeGapX = 90, nodeGapY = 16, mapStep = 60;
inline constexpr float logRowH = 21;
// What's shown over a Library form (at most this high, or this much of the room), and a list's popup.
inline constexpr float sampleStyleH = 110, sampleAtlasH = 230, sampleAnimationH = 240, sampleSubPictureH = 380;
inline constexpr float sampleStylePart = 0.25f, sampleAtlasPart = 0.42f, sampleAnimationPart = 0.4f, sampleSubPicturePart = 0.55f;
inline constexpr float popupShadow = 3, popupMargin = 8, popupGap = 2;
} // namespace layout

// The visual editor's view of a game: its script files as lines, the scenes in them (labels), each
// scene's rows (a line each; the lines inside a choice or an "if" are under it), the characters and
// the game's settings (its config lines). It changes lines and writes the files straight away. Lines
// it has no form for stay as they are, to be edited as text.
class StoryEditor {
public:
    // A Line is someone speaking, or narration (no one); a Background is a scene line; a Character
    // shows or hides someone (or a picture); an Option is one of a choice's, or an answer to a message;
    // Game is quick save, fullscreen, screenshot or quit. Other is any line without a form (a mistake).
    enum class Kind : std::uint8_t { Line, Character, Background, Choice, Option, If, Else, Set, Jump, End, Music, Sound, Voice, Stop, Wait,
                                     Move, Shake, Effect, Weather, Camera, Movie, Screen, Notify, Textbox, NewPage, Together, Chapter,
                                     Unlock, Message, Phone, Ask, Game, Note, Other };
    // The names a field lists (Expressions: of the character in the field's context; Records: the
    // endings, achievements, tips or gallery pictures, by the context's word). Shown: characters and
    // pictures; Backgrounds: pictures and colours; Bases: the names pictures start with (aiko for
    // aiko_happy); Channels: the names sounds play as; FontNames: the fonts font lines name; Keys: key,
    // mouse and controller names; SettingValues: a player setting and a value for it ("fullscreen yes"), as
    // buttons set them; Settings: the players' settings sliders change;
    // Conditions: the variables, and the words screens' conditions know; PictureFiles: images/NAME.png.
    enum class Pick : std::uint8_t { None, Characters, Shown, Pictures, Sounds, Places, Transitions, Scenes, VariableNames, Expressions, Screens,
                                     Movies, Records, Fonts, Styles, Colors, PicturesOrColors, Bases, Channels, FontNames, Keys, Languages,
                                     VoiceFolders, Settings, Controls, Conditions, PictureFiles, Animations };
    // Every box a form has is of a kind, defined once (kindOf): what it lists to pick from, the values it
    // suggests, the buttons it has instead of a box and what they say, and how its value is written. A
    // row says its box's kind and what its own empty box means, so boxes of a kind are the same in
    // every form.
    enum class Box : std::uint8_t {
        None,  // (only while a row is being made: every row says its kind)
        Words, // text that can be bold, coloured, paused...
        Text,  // plain text: a title, a question, a note, what a tab says
        Name,  // one word that names something: a style, a character, a part
        Picture, // a picture, or a colour (every picture's box takes either)
        PictureFile, Colour, Sound, Movie, FontFile, Font, Style, Character, Shown, Expression, Scene, Screen, Variable,
        Setting, Animation, Key, Place, Channel, Record, VoiceFolder, Control, Base, Language,
        Across, Down, Layer, Speed, Zoom, Opacity, Volume, Percent, Rotation, Seconds,
        Pixels, Count, Number, PerSecond, LineSpacing, TextSpeed, Pause, Wind, WidthHeight, Point, // (numbers: a size, how many, a rate...)
        FontStyle, Babble, VariableValue, Action, Change, Playing, KeySlot, SaveMode, TimedTransition,      // (lists of words)
        YesNo, YesNoStyle, YesNoSettings, YesNoSame, Easing,
        Condition, Transition, Fading, Fit, Reveal, Talk, ImageFilter, SkipHold, WindowFill, Mouths,
        Sync, Side, FaceCopies, FaceTalk, Align, VAlign, Operation, Joiner, ShowHide, Visibility, SetVerb, Sent, Anchor, Repeat, Scroll, Effects, WeatherKind,
        Edges, ContainerFit,
        ScreenAction, VariableAction, StoryAction, SoundAction, SaveAction, SettingAction, GalleryAction, // (an action's kind, by what it's about)
        SettingChoice, GameLanguage, // (what a setting goes to: the Settings tab's own options for it, as a list; a language)
    };
    struct BoxKind {
        Box box = Box::None;
        Pick pick = Pick::None;
        std::string_view suggest, choices, labels;
        bool rich = false;     // text with bold, colours, pauses... (buttons for them under the box)
        bool percent = false;  // written as one (50%), however it's typed (0.5, 50)
        bool wheel = false;    // a colour's box: the colour as a square, and a wheel with the code to type when it opens
        bool pictures = false; // its list shows the pictures, as cells
        bool listen = false;   // its list's entries, and the box, have a button to hear them
        bool wide = false;     // its names can be long (a file's, a picture's): never half a row
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
        std::string suggest; // values it often has, listed with the names: "|" between them, "value:what it is"
        bool hidden = false;      // words the form has no box for, kept as they are
        bool rich = false;        // text that can be bold, coloured, paused... (buttons for that show under it)
        bool beside = false;      // on the row of the field before it, when there's room (an action's kind, and what it asks for)
        std::string group;   // the heading it starts (the fields after it are under it too, until the next; "+Name": folded at first)
    };
    struct Scene {
        std::size_t file = 0, line = 0; // its label line
        std::size_t end = 0;            // after its last line
        std::string name;
    };
    struct Row {
        std::size_t line = 0; // in the scene's file
        std::size_t end = 0;  // after its last line, and those of the rows inside it
        int depth = 0;        // 0: right under the label
        Kind kind = Kind::Other;
        std::string text;     // without the indentation
    };
    struct Character {
        std::size_t file = 0, line = 0;
        std::string name;
    };

    void load(const std::string& root); // the game folder, with a slash
    void clear() { *this = {}; }
    [[nodiscard]] bool loaded() const { return !files_.empty(); }
    [[nodiscard]] const std::vector<Scene>& scenes() const { return scenes_; }
    [[nodiscard]] const std::vector<Row>& rows(const Scene& scene) const; // (kept as of this version: a reference, not to hold across a change)
    [[nodiscard]] const std::vector<Character>& characters() const;
    [[nodiscard]] std::vector<std::string> names(Pick pick, std::string_view context = {}) const;
    [[nodiscard]] const std::vector<std::string>& variables() const; // the ones the scripts change or ask for
    [[nodiscard]] const std::string& path(std::size_t file) const { return files_[file].path; } // "scripts/main.vnscript"

    // Rows as forms: the fields of a row (they can change with its values: a hidden character has no
    // place), and the line they make (from the fields by name).
    [[nodiscard]] static std::string_view kindName(Kind kind);
    [[nodiscard]] std::vector<Field> fields(const Row& row) const;
    [[nodiscard]] std::string lineOf(Kind kind, const std::vector<Field>& fields) const;
    [[nodiscard]] std::string summary(const Row& row) const; // as the list shows it
    [[nodiscard]] std::vector<Field> fields(const Character& c) const;

    // Changes, each written to its file at once. Changes with the same `key` are one step for undo
    // (typing into one field).
    void setText(const Scene& scene, const Row& row, const std::string& text, const std::string& key);
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
    // only right under a choice or message, and nothing else there; never into itself.
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
    void setCharacter(const Character& c, const std::vector<Field>& fields, const std::string& key);
    void removeCharacter(const Character& c);
    std::optional<std::size_t> moveCharacter(std::size_t c, std::size_t to); // before character `to` (or after the last), in its file
    // Finding: the lines with `what` in them (any case), in every script file, and putting `with` in its
    // place in all of them (one step for undo): how many lines changed.
    struct Found {
        std::size_t file = 0, line = 0;
        std::string text;
    };
    [[nodiscard]] std::vector<Found> find(std::string_view what) const;
    [[nodiscard]] std::optional<std::string> lineText(std::string_view path, std::size_t line) const; // a line (trimmed) of the script at `path`
    [[nodiscard]] bool mentions(std::string_view word) const; // some script line has the word (whole, case aside)
    // What a line is part of, as the found lines say: its scene, its screen ("screen title"), a
    // character, a definition ("style button"), Settings, or else its file.
    [[nodiscard]] std::string placeOf(std::size_t file, std::size_t line) const;
    std::size_t replaceAll(std::string_view what, const std::string& with);
    // A variable's lines (set, if, {name} in words, screens' if= and var=...), and a new name for it in
    // all of them (one step for undo): false when it can't be one.
    [[nodiscard]] std::vector<Found> uses(std::string_view variable) const;
    // Every line that uses it changes too: how many (its own variable line not counted); none when the name can't be.
    std::optional<std::size_t> renameVariable(const std::string& from, const std::string& to);
    // The layout's lines (screens' parts, styles, animations' steps) with `scale` done to them (one
    // step for undo, with others of the same key).
    void scaleLayout(const std::function<std::string(const std::string&)>& scale, const std::string& key);
    // A new name for a file in images/, audio/, movies/ or fonts/ (`from` and `to` with their
    // extension), and the lines that use it changed to match (a character's pictures: its
    // expressions). One step for undo, the file's name too. How many lines changed; none when it
    // can't have that name.
    std::optional<std::size_t> renameAsset(std::string_view folder, const std::string& from, const std::string& to);
    // The scene ends by jumping to `to`: a jump or end it ends with already changes, else one is added.
    // True when one was there.
    bool jumpAtEnd(std::size_t scene, const std::string& to);
    // Each character's name and the name its pictures start with (aiko_happy is "show aiko happy").
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> pictureBases() const;
    // Where the story goes from each scene: jump, call and option arrows (-> scene), by name.
    struct Link {
        std::size_t from = 0;
        std::string to;
        bool call = false, option = false;
        std::size_t line = 0; // (its line in the scene's file)
    };
    [[nodiscard]] const std::vector<Link>& links() const;
    // A jump's (or call's) line taken out, or an option's arrow (it goes on after the choice then).
    void removeLink(const Link& link);
    // Where the Map shows the scenes put somewhere by hand (by name; game/.map.txt, which packs leave
    // out, as "name x y"); the others are laid out by themselves. Renaming a scene keeps its place.
    [[nodiscard]] std::optional<SDL_FPoint> mapPlace(std::string_view scene) const;
    void setMapPlace(const std::string& scene, std::optional<SDL_FPoint> at);
    bool undo();
    bool redo();
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
    // One the Story tab lists: with scenes, or nothing yet (not one of just screens or definitions).
    [[nodiscard]] bool storyFile(std::size_t file) const;
    std::size_t addFile();                                      // a new one (included), its index
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
    [[nodiscard]] bool defines(std::string_view what, std::string_view name) const; // a "style NAME" line, "color NAME"...
    [[nodiscard]] std::vector<std::string> defined(std::string_view what) const;     // the names of those

    // Things listed once by name, outside the scenes: "color NAME ...", "style", "font", "position",
    // "atlas" (with the lines indented under it), "gallery", "ending", "song", "achievement", "tip"
    // and "control" lines. A new one goes after the last of its kind (or in main.vnscript, before its
    // first scene); renaming one follows where it's used (styles, colours, fonts, places, atlases,
    // keys, and the story's endings, achievements and tips).
    struct Definition {
        std::size_t file = 0, line = 0, end = 0; // its line, and after the lines under it
        std::string name;
    };
    [[nodiscard]] const std::vector<Definition>& definitions(std::string_view kind) const;
    [[nodiscard]] std::vector<std::string> linesOf(const Definition& d) const; // (without their indentation)
    // A definition as a form, and the lines a form makes (an atlas: its line and the ones under it).
    [[nodiscard]] std::vector<Field> fields(std::string_view kind, const Definition& d) const;
    [[nodiscard]] std::vector<std::string> linesOf(std::string_view kind, const std::vector<Field>& fields) const;
    std::size_t addDefinition(std::string_view kind, const std::vector<std::string>& lines); // gives its index
    void setDefinition(const Definition& d, const std::vector<std::string>& lines, const std::string& key);
    void removeDefinition(const Definition& d);
    std::optional<std::size_t> moveDefinition(std::string_view kind, std::size_t index, std::size_t to); // in its file
    bool renameDefinition(std::string_view kind, std::size_t index, const std::string& name, const std::string& key);
    [[nodiscard]] std::string root() const { return root_; }

    // The game's settings: the config lines in main.vnscript. A setting's value as written (without
    // quotes), and its line; none: it has no line, so it has its default.
    [[nodiscard]] bool hasMain() const;
    [[nodiscard]] std::optional<std::string> setting(std::string_view name) const;
    [[nodiscard]] std::optional<std::size_t> settingLine(std::string_view name) const;
    [[nodiscard]] bool isSetting(std::size_t line) const; // a config line in main.vnscript
    // A new value (in quotes if `quoted`); an empty one takes the line out.
    void setSetting(std::string_view name, const std::string& value, bool quoted, const std::string& key);

    [[nodiscard]] std::size_t version() const { return version_; } // goes up with every change
    [[nodiscard]] const std::string& problem() const { return problem_; }        // a file that couldn't be written

private:
    struct File {
        std::string path; // from the game folder
        std::vector<std::string> lines;
        SDL_Time modified = 0; // when the editor read or wrote it
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
    void write(std::size_t file);
    void eraseLines(std::size_t file, std::size_t from, std::size_t until); // (one step for undo, written and read again)
    void writeAll();
    [[nodiscard]] std::size_t blockEnd(std::size_t file, std::size_t label) const;
    // Moves lines [from, until) of a file to before line `at` (with a blank line on each side when
    // they're a scene or a screen): gives where they start now.
    std::size_t moveLines(std::size_t file, std::size_t from, std::size_t until, std::size_t at, bool spaced);
    [[nodiscard]] bool isCharacter(std::string_view name) const;
    [[nodiscard]] bool hasScene(std::string_view name) const; // (by name, in any case)
    [[nodiscard]] bool hasScreen(std::string_view name) const;
    void renameSpeaker(const std::string& from, const std::string& to);

    std::string root_;
    std::vector<File> files_;
    std::vector<Scene> scenes_;
    std::vector<Screen> screens_;
    std::vector<Step> undo_, redo_; // the files before each change, and before each undo
    std::string lastKey_;
    std::size_t version_ = 0;
    std::string problem_;
    std::pair<std::string, std::size_t> removed_; // the setting whose line went last, and where it was
    std::map<std::string, SDL_FPoint, std::less<>> mapPlaces_; // (lower-case names)
    void saveMapPlaces() const;
    mutable std::pair<std::size_t, std::vector<std::string>> variables_{SIZE_MAX, {}}; // (at a version)
    // What's read from the lines often, kept from a version of them until they change.
    mutable std::pair<std::size_t, std::vector<Character>> characters_{SIZE_MAX, {}};
    mutable std::pair<std::size_t, std::vector<Link>> links_{SIZE_MAX, {}};
    mutable std::pair<std::size_t, std::map<std::string, std::size_t, std::less<>>> settingLines_{SIZE_MAX, {}}; // each setting's line
    mutable std::map<std::string, std::pair<std::size_t, std::vector<Definition>>, std::less<>> definitions_;    // by kind
    mutable std::map<std::pair<std::size_t, std::size_t>, std::pair<std::size_t, std::vector<Row>>> rows_;       // by a scene's file and line
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
// An amount as a percent kind writes it: 50% from 0.5, 50 or 50%.
[[nodiscard]] std::string percentText(std::string_view value);

// What the developer tools keep from frame to frame (Developer*.cpp). In developer mode the player starts in
// them, and F10 opens them over the game (the story waits meanwhile); without a game they're all the
// player shows.
// A translation file's text (a line in quotes) and the line with its translation under it, if any.
struct TextPair {
    std::size_t original = 0;
    std::optional<std::size_t> translated;
};
[[nodiscard]] std::vector<TextPair> textPairs(const std::vector<std::string>& lines);
[[nodiscard]] bool untranslatedPair(const std::vector<std::string>& lines, const TextPair& p);
void setTranslation(std::vector<std::string>& lines, const TextPair& p, const std::string& value);

struct DeveloperTools {
    enum class View : std::uint8_t { Story, Map, Screens, Library, Settings, Translate, Log, About };
    bool open = false;
    View view = View::Log;
    std::string status; // what the last button did (written to the log when it changes: the Log tab shows it)
    std::string loggedStatus;
    bool failed = false;
    int packIn = 0;         // frames until Pack the game packs (after one saying it's packing)
    bool play = false;      // Play: the game starts from the beginning once the scripts are read again
    std::string playScene;  // Play this scene: the scene to start then
    std::optional<std::size_t> playLine; // Play from this line: its line in the scene's file
    std::size_t scroll = 0; // log lines scrolled back from the newest, or About's lines scrolled down
    // What happened since the last frame; the drawing acts on it (buttons act when drawn).
    float mouseX = -1.0f, mouseY = -1.0f, wheel = 0.0f;
    bool clicked = false, doubleClick = false;
    std::string typed;
    std::vector<SDL_KeyboardEvent> keys;
    // The Story tab: the visual editor.
    StoryEditor editor;
    std::size_t played = 0, checked = SIZE_MAX; // the editor's version the game was read at; the problems were found at
    std::unique_ptr<Script> script;             // the scripts as the editor has them, read again after every change
    std::vector<ScriptError> problems;          // in them
    std::size_t scene = 0;
    std::optional<std::size_t> row, character; // what the form shows: a line of the scene (its line), or a character
    bool asText = false;                        // the line as text, not a form
    std::string armed;                          // the list item whose delete button was clicked once
    bool listenShown = false;                   // the button of the file playing was drawn (Engine::listenButton)
    std::string lastList;                       // the list clicked last (copy and paste act on its chosen item)
    std::string clipText;                       // what the tools copied last, and the kind of line it was
    StoryEditor::Kind clipKind = StoryEditor::Kind::Other;
    float nextCheck = 0.0f;                     // when to look for scripts changed in another program
    struct ListDrag {                           // a list item being dragged to another place
        std::string list;
        std::size_t item = 0;
        float x = 0.0f, y = 0.0f; // where the mouse was pressed
        bool moving = false;
    };
    std::optional<ListDrag> listDrag;
    float rowsScroll = 0.0f, scenesScroll = 0.0f, charactersScroll = 0.0f, settingsScroll = 0.0f, formScroll = 0.0f, findScroll = 0.0f;
    float formHeight = 0.0f; // how tall the form was last frame (it scrolls no further than its ends)
    // The game's size changed in Settings: the size before (the screens are stretched to the new one
    // once the box is left), and the change's undo key.
    std::optional<std::string> sizeFrom;
    std::string sizeKey;
    std::string find, replaceWith; // the Story tab's find box, and what Replace all puts in its place
    std::vector<std::size_t> moreRows;          // more lines of the scene chosen (Shift+click), by their lines
    std::optional<std::size_t> collapseTo;      // a chosen one pressed: only it stays chosen, unless it's dragged
    // The Library tab: what's listed (a kind of definition, "variables", "voices", or the files:
    // "pictures", "sounds", "movies" or "fontfiles") and the one chosen (a file: also what's made from it, a sub-image or a font, by
    // its index in the editor's definitions, and the files whose list of those is open); files picked
    // to import (from the file dialog's thread, or dropped on the window), copied into the game when
    // the tools are next drawn.
    std::string library = "pictures";
    std::optional<std::size_t> libraryItem, librarySub;
    std::vector<std::string> libraryOpen;
    std::string libraryFind; // a file to choose once it's listed (by the name scripts call it)
    float libraryScroll = 0.0f, kindsScroll = 0.0f;
    struct Imports {
        std::mutex mutex;
        std::vector<std::pair<std::string, std::string>> files; // each one's path, and where it goes (empty: by its kind, under its own name)
        std::optional<SDL_FPoint> dropAt;                     // where the last drop landed (on the Screens tab's screen: the image goes there)
        std::string as;                                       // where the one picked in the dialog open goes, without its extension
        std::string pattern, label;                           // (that dialog's filter)
        SDL_DialogFileFilter filter{};                        // ...pointing at them, while it's open
        bool open = false;                                    // a dialog is open (one at a time: its filter stays as it is)
    } imports;
    struct Translation { // the Translate tab's file, as lines
        std::string id;
        std::vector<std::string> lines;
    } translation;
    std::vector<Translation> translateUndo, translateRedo; // its files before each change (and each undo)
    std::string translateKey;                              // (typing into one box: one step)
    bool untranslatedOnly = false;
    std::map<std::string, std::pair<SDL_Time, std::string>> languageLabels; // the Translate tab: each file's language as its list says it, and when it was read
    std::map<std::string, bool, std::less<>> spelling;                        // "language|word": spelt right (the system's dictionary, asked once)
    std::map<std::string, std::vector<std::string>, std::less<>> spellingGuesses; // "language|word": what it may be instead
    float translateScroll = 0.0f;
    std::size_t translateChecked = SIZE_MAX;               // the scripts' version its texts were last checked against
    TexturePtr fontSample; // the chosen font file's sample, and that file
    std::string fontSampleOf;
    // The chosen movie: a still of its first picture, or playing (heard through the player's own
    // sound), and that file.
    std::unique_ptr<Movie> movie;
    std::string movieOf;
    bool moviePlaying = false;
    Uint64 movieTicks = 0;
    void stopMovie() {
        movie.reset();
        movieOf.clear();
        moviePlaying = false;
    }
    // The list a field has to pick from, open under it (drawn over everything at the end of the frame):
    // the field's id, what it lists, where the field and the list were, and a pick (the field takes it
    // next frame). Clicks and the wheel over the list are its own.
    struct Popup {
        std::string id;
        StoryEditor::Field field;
        SDL_FRect at{}, box{};
        bool typed = false; // typed into since it opened: only what matches shows
        bool shown = false; // its field was drawn this frame
        float scroll = 0.0f;
        std::vector<std::string> names; // what its field lists, as of a version of the scripts (not found again every frame)
        std::size_t namesVersion = SIZE_MAX;
        // A colour's wheel (what a colour's box opens): the colour as hue, saturation, value and
        // alpha, and what's being dragged (0: nothing; 1: the wheel; 2: the value bar; 3: the alpha bar).
        float hue = 0.0f, sat = 0.0f, val = 1.0f, alpha = 1.0f;
        int dragging = 0;
        bool editBottom = false; // a gradient (navy-black): the wheel changes its bottom colour, not its top
    } popup;
    TexturePtr wheelPicture; // the colour wheel's picture, `wheelPictureSize` pixels across
    int wheelPictureSize = 0;
    std::optional<std::pair<std::string, std::string>> picked;
    std::string openList; // a box whose list opens once it's drawn (a new line's first box, focused by the editor)
    bool popupClick = false, overPopup = false;
    float popupWheel = 0.0f;
    float mapX = 0.0f, mapY = 0.0f;   // the Map tab: how far it's looked around
    std::optional<SDL_FPoint> mapDrag; // (being dragged: where the mouse started, plus the look)
    struct MapPress {                 // a scene pressed on: dragged, it moves; its dot dragged to another, a jump is drawn
        std::size_t from = 0;
        float x = 0.0f, y = 0.0f;     // where the mouse was pressed
        SDL_FPoint box{};             // where the scene's box was then
        bool dot = false, moving = false, chosen = false; // (it was the chosen one already: let go where it was, it's renamed)
    };
    std::optional<MapPress> mapPress;
    std::optional<std::pair<std::size_t, std::size_t>> mapArrow; // the arrow chosen: its scene, and its line
    // The box being typed into: its id, text and caret (and where what's chosen in it starts: `anchor`,
    // the same as the caret when nothing is), and what it changes.
    std::string focus, buffer;
    std::size_t caret = 0, anchor = 0, anchorFocus = 0; // (anchor belongs to the box typed into at focusCount anchorFocus)
    std::string selectingIn; // the box the mouse is choosing letters in (it's held down)
    std::string entered;     // the box Enter was pressed in, this frame
    std::size_t focusCount = 0; // (each time a box is clicked into: one step for undo)
    // The caret into a box: `text` in it (all of it chosen, with `all`), as a new step for undo.
    void focusOn(std::string id, std::string text, bool all = false) {
        focus = std::move(id);
        buffer = std::move(text);
        caret = buffer.size();
        focusOwner.clear();
        ++focusCount;
        if (all) anchor = 0, anchorFocus = focusCount;
    }
    std::string focusOwner; // the field (or "raw") the buffer belongs to, and its form when it started
    StoryEditor::Kind focusKind = StoryEditor::Kind::Other;
    std::vector<StoryEditor::Field> focusFields;
    // The preview: the story played to the chosen line (or the scene's start) in a game of its own,
    // silent, with its own copies of what the player has seen and kept, and drawn into `screen`.
    std::unique_ptr<Game> preview;
    std::optional<std::tuple<std::size_t, std::size_t, std::size_t>> previewOf; // editor version, scene, line
    bool previewReached = false; // (else it's shown without what comes before it)
    bool previewPlaying = false; // it was clicked on: it plays on from there
    bool previewSound = false;   // it's heard (through `heard`, the player's own sound)
    bool previewOn = true;       // (off: none is made or shown, and the form has its room)
    bool previewHeld = false;    // the mouse went down on it: it gets the coming up too
    SDL_FRect previewShown{};    // where it's drawn this frame
    std::vector<std::string> previewOverlays; // the screens its buttons opened over it (the top last)
    std::vector<std::string> previewLeaving;  // the ones its buttons closed, drawn while their hide_animation= plays
    std::string previewPending;               // one a button opened, opening once the screen under it has faded out
    std::vector<std::string> previewScreens;  // the screens on it last frame (their on_show= and on_hide= run as they come and go)
    std::optional<std::pair<std::string, std::size_t>> previewAt; // playing on: the file and line it's at
    std::optional<std::size_t> previewRow;                        // (the row lit for it in the list, kept in view)
    struct PlaceDrag {           // the chosen line's character (or the camera), dragged on it
        float x = 0.0f, y = 0.0f;           // where the mouse was pressed
        float across = 0.0f, up = 0.0f;     // where they were then (-1 to 1: the left edge to the right, the bottom to the top)
        float perX = 0.0f, perY = 0.0f;     // how far those go for each pixel the mouse moves
        bool moving = false;
        std::size_t key = 0;                // (one step for undo)
    };
    std::optional<PlaceDrag> placeDrag;
    Audio* heard = nullptr;
    Uint64 previewTicks = 0;
    Audio silence;               // (never started)
    ReadLog previewRead;
    Variables previewPersistent;
    Chart previewChart;
    TexturePtr canvas; // (the Screens tab draws into it too)
    // The Screens tab: the screen open (its name: the game's own, or a built-in one), the part chosen
    // (its line; none: the screen's own settings), and a part being moved or resized.
    std::string screenName;
    std::optional<std::size_t> part;
    bool picking = false;  // + New screen: the screens to start from show
    std::string screenTip; // the Screens tab's word of help for the moment (said as its form is drawn, shown under the preview's buttons the next frame)
    bool addingPart = false, addInto = false; // + New part: its kinds show (and it goes in the part chosen, or the screen)
    bool trying = false;   // Try it: the screen works as in the game (what its buttons would do is said)
    bool tryHeld = false;  // ...the mouse was pressed on it
    Settings trySettings;  // ...and its setting buttons and sliders change these (the player's own stay as they are)
    bool held = false;     // the mouse button is down
    struct Drag {
        std::size_t part = 0, element = 0; // its line, and its part of the screen as read
        bool resize = false;
        SDL_FRect from{};                  // its box then
        float x = 0.0f, y = 0.0f;          // where the mouse was then (on the canvas)
        float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f; // its x= y= w= h= then
        bool own = true;                                  // it moves on its own (not with a chosen part it's in)
        std::vector<std::pair<std::size_t, SDL_FRect>> moved; // everything it moves (the parts in them too), and where each was
        struct Other {                                    // the other chosen parts, moving with it
            std::size_t part = 0, element = 0;
            SDL_FRect from{};
            float px = 0.0f, py = 0.0f;
        };
        std::vector<Other> others;
    };
    std::optional<Drag> drag;
    SDL_FRect shownScreen{}; // where the Screens tab showed the screen last (the mouse over it: on the screen)
    std::size_t wordsVersion = SIZE_MAX, gameWords = 0; // the game's words, as of that version of the scripts
    // The open scene's lines as the list says them, and which never ran in a play (as of that version, scene and set of lines run).
    std::size_t listVersion = SIZE_MAX, listScene = SIZE_MAX, listReached = SIZE_MAX, neverRan = 0;
    std::vector<std::string> rowSummaries;
    std::vector<bool> rowRan;
    std::size_t wrongScenesVersion = SIZE_MAX; // which scenes have a mistake, as of that version (wrongScenes())
    std::vector<bool> scenesWrong;
    std::size_t colourVersion = SIZE_MAX; // the characters' name colours, by lower-case name, as of that version
    std::map<std::string, std::optional<SDL_Color>, std::less<>> characterColours;
    std::uint64_t audioGeneration = 0; // sound files found by name, as of that list of files (see filesGeneration)
    std::map<std::string, std::string> audioPaths;
    std::map<std::string, std::optional<std::string>> foundAudio;
    std::size_t sceneWordsVersion = SIZE_MAX, sceneWordsScene = SIZE_MAX, sceneWords = 0; // the open scene's, as of that version
    // The files in each folder with an ending, as listed at a generation: listed again at the next (twice a
    // second, and when files are imported, renamed or thrown away), not every frame.
    std::map<std::string, std::pair<std::uint64_t, std::vector<std::string>>> fileLists;
    bool saidNoGame = false; // (the hint that there's no game yet went to the log: once)

    // A character's name colour, when it has one (the lists that name characters show it as a square).
    [[nodiscard]] std::optional<SDL_Color> characterColor(const Palette& palette, std::string_view name);
    // A sound file by its name (and whether it's there), as of the list of files: not looked for on disk
    // every frame.
    [[nodiscard]] const std::string& cachedAudio(const Assets& assets, const std::string& name);
    [[nodiscard]] std::optional<std::string> cachedFoundAudio(const Assets& assets, const std::string& name);
    // The files in a folder with an ending, as listed once a generation (see filesGeneration).
    [[nodiscard]] const std::vector<std::string>& cachedFiles(const std::string& folder, std::string_view ext);
    // The mistake the scripts have on a line, if any; whether they have one on a line of `file` from
    // `from` up to `end` (not `end` itself); which scenes have one, as of this version of the scripts.
    [[nodiscard]] const ScriptError* problemAt(std::size_t file, std::size_t line) const;
    [[nodiscard]] bool problemWithin(std::size_t file, std::size_t from, std::size_t end) const;
    [[nodiscard]] const std::vector<bool>& wrongScenes();
    // A form's edit put into its fields: typed, into the copy kept while that box is typed into (one step
    // for undo: `key` names the box); picked, into a copy of the fields, and the box let go.
    struct Edited {
        std::vector<StoryEditor::Field> fields;
        std::string key; // (empty: a pick)
        StoryEditor::Kind kind;
    };
    [[nodiscard]] Edited edited(const std::string& id, StoryEditor::Kind kind, const std::vector<StoryEditor::Field>& fields, bool wasTyped,
                                const std::string& name, const std::string& value);
    // What a rename did: a mistake said (and kept while the name is wrong), or the last one cleared.
    void saidRenamed(bool ok, std::string_view mistake);
    std::uint64_t filesGeneration = 1;
    // The Voices list: the lines characters with voices say, as of the scripts read (`checked`), and
    // whether each one's file is there (in a translation's folder too), as of the list of files.
    struct VoicedLine {
        std::string file, who, text, path;
        int line = 0;
    };
    std::vector<VoicedLine> voiced;
    std::size_t voicedVersion = SIZE_MAX;
    std::map<std::string, bool, std::less<>> voiceThere;
    std::uint64_t voiceThereGeneration = 0;
    // What Find found, and the lines a variable is used in: looked for again when what's looked for (or
    // the scripts) change, not every frame.
    struct Looked {
        std::string what;
        std::size_t version = SIZE_MAX;
        std::vector<StoryEditor::Found> found;
    };
    Looked lookedFind, lookedUses;
    std::size_t unusedVersion = SIZE_MAX, unusedCount = 0; // the Library's files no script names, as of that version and list
    std::string unusedOf;
    std::set<std::string> unused;
    std::vector<std::size_t> moreParts;     // more parts chosen (Shift+click), by their lines
    std::optional<float> guideX, guideY;    // what the part being moved lines up with (on the canvas)
    std::unique_ptr<Game> sample; // the story at its first line (or choice), for the textboxes in screens
    bool sampleMade = false;
    std::string tryScreen; // (tried: the screen whose on_show= ran; its on_hide= runs when another is tried)
    std::vector<std::string> tryOverlays; // the screens the tried one's buttons opened over it (the top last), as in the game
    std::vector<std::string> tryLeaving;  // (closed: drawn while their hide_animation= plays)
    bool tryClosing = false;              // the tried screen closed itself: its hide_animation= plays, then Try it stops
    std::string tryPending;               // one a button opened, opening once the screen under it has faded out
    std::unique_ptr<ScreenView> screenView; // the screen open, as the game draws it
    float screensScroll = 0.0f, partsScroll = 0.0f;
    // The Library's atlas: the expression its frame shows, and its eyes or mouth being dragged there.
    std::size_t atlasExpression = 0;
    struct PartDrag {
        bool mouth = false;
        float x = 0.0f, y = 0.0f;   // where the mouse was pressed
        float px = 0.0f, py = 0.0f; // where the part was then (in the frame's pixels)
        std::size_t key = 0;        // (one step for undo)
    };
    std::optional<PartDrag> partDrag;
    // The Library's sub-picture: a box being dragged on the picture it's cut from (from where, in
    // the picture's pixels, and where the mouse was pressed).
    struct CutDrag {
        SDL_FPoint from{};
        float x = 0.0f, y = 0.0f;
    };
    std::optional<CutDrag> cutDrag;
    // The Library's animation: its sample plays from this time (again when another is chosen).
    std::pair<std::string, float> animationSince;
    // The Library's sample of a style: the scripts with a screen of a button and a text in it, at an
    // editor version.
    std::unique_ptr<Script> styleScript;
    std::unique_ptr<ScreenView> styleView;
    std::pair<std::size_t, std::string> styleOf{SIZE_MAX, {}};
    std::map<std::string, std::unique_ptr<ScreenView>, std::less<>> previewViews; // the preview's screens (Engine::view)
    // The games and views made from `script` (it's read again after every change).
    void dropPreviews() {
        preview.reset();
        previewOf.reset();
        previewPlaying = false;
        previewAt.reset();
        previewRow.reset();
        previewHeld = false;
        previewOverlays.clear();
        previewLeaving.clear();
        previewPending.clear();
        previewScreens.clear();
        tryOverlays.clear();
        tryLeaving.clear();
        tryClosing = false;
        tryPending.clear();
        if (heard) heard->stopAll(0.2f);
        heard = nullptr;
        sample.reset();
        sampleMade = false;
        screenView.reset();
        drag.reset();
        styleView.reset();
        styleScript.reset();
        styleOf = {SIZE_MAX, {}};
        previewViews.clear();
    }
    // Text drawn this frame, kept so it isn't laid out again every frame.
    std::vector<TextBlock> texts;
    std::size_t used = 0;
};

// The developer tools: what the player shows over its game in developer mode (F10), and what they do
// (drawn, the keys and clicks they take, Play...). Their state is theirs (DeveloperTools); the player
// asks them only what's below. Made with the player: never copied.
class Developer {
public:
    explicit Developer(Engine& player) : engine(player) {}
    Developer(const Developer&) = delete;
    Developer& operator=(const Developer&) = delete;
    Developer(Developer&&) = delete;
    Developer& operator=(Developer&&) = delete;
    ~Developer() = default;

    [[nodiscard]] bool isOpen() const { return tools.open; }
    void dropPreviews() { tools.dropPreviews(); } // (they use the pictures and the renderer: let go of before those are)
    void dropCanvas() { tools.canvas.reset(); }
    void stopMovie() { tools.stopMovie(); }
    // They close, as they are (the game a found game folder has begins without them).
    void leave() {
        tools.open = false;
        tools.failed = false;
        tools.status.clear();
    }
    // The game was read again (Reload, or Play after a change): Play, Play this scene or Play from this
    // line starts it; else they read the scripts again too.
    void reloaded();
    // A screen as the previews and Try it show it: made from `from` (the editor's scripts) when first needed.
    [[nodiscard]] ScreenView* previewScreen(std::string_view name, const Script& from);
    void loadReached();   // the script lines the game ran in the developer's plays (reached.txt), and
    void recordReached(); // the ones it ran since last frame
    void recordReached(Game& game, const Script& script); // (the preview's too: a line run there has run)
    void openDeveloper();
    void closeDeveloper(); // (the game reads the scripts again if the editor changed them)
    void backToGame();     // or Play, when no game is going on
    void backToEditor();   // developer mode: the developer tools, until Play (at the start, and where the player would close)
    void developerEvent(const SDL_Event& e);
    void useTool(int tool);
    void updateDeveloper();
    void drawDeveloper();
    void playFromLine(const std::string& scene, std::optional<std::size_t> line); // the game, as it is at that line (none: the scene's first)

private:
    void runTrying(const Commands& commands, const std::string& confirm = {}); // (as a tried screen's: what they'd do to the player's game is said, not done)
    void tryScreenHook(const ScreenDef& def, bool opening); // (as a tried screen's: said, not done)
    void runScreenHook(std::string_view screen, bool shown);               // its on_show= or on_hide=, as the game runs them when it opens and closes
    [[nodiscard]] ScreenView* previewView(std::string_view name); // the preview's and Try it's: the editor's screens, as they are now
    [[nodiscard]] std::string translationPath(const std::string& id) const; // a translation's file
    void saveTranslation();                             // the Translate tab's file, written
    [[nodiscard]] std::string translationSheet() const; // its spreadsheet's path (next to the game folder)
    std::string exportTranslationSheet();               // written, and read back: both say how it went
    std::string importTranslationSheet();
    void drawStory(DeveloperUi& ui, const SDL_FRect& area);
    struct StoryFrame; // (that tab this frame: what its parts share)
    bool drawStoryLists(const StoryFrame& f); // the scenes and the characters (true: changed, the rest next frame)
    bool drawStoryRows(StoryFrame& f); // the scene's lines, found, chosen, moved, copied (true: changed)
    std::string storyCounts(const StoryFrame& f);                  // ...what's said under them: words, reading time, lines never run
    bool drawFindBar(const StoryFrame& f, float h);                // ...Find, and Replace with (true: replaced)
    bool drawRowList(const StoryFrame& f, const SDL_FRect& box);   // ...the list: chosen, moved, deleted (true: changed)
    bool storyClipboard(const StoryFrame& f);                      // ...copy, cut, paste, duplicate (true: changed)
    void drawStoryForm(const StoryFrame& f); // the preview, and the chosen line's (or character's) form over it
    void drawPreview(DeveloperUi& ui, const SDL_FRect& box, const StoryEditor::Scene& scene, const StoryEditor::Row* row);
    void makePreview(const StoryEditor::Scene& scene, const StoryEditor::Row* row);
    // The preview takes an event (in the game's coordinates) as the game would; and what its buttons
    // did is done to it where it can be (the story goes on, a screen opens over it...), or said.
    void previewEvent(const SDL_Event& e);
    void previewDid();
    void previewHooks(); // the on_show= and on_hide= of the screens that came and went on it
    // The story of `s` played as fast as it goes to a line of a scene (an option's: its choice's;
    // none: the scene's first), in a game of its own (heard through `audio`) with copies of what the
    // player has seen and kept. `reached` false: it never got there from the scene's start, so it
    // starts at the line itself. None when the line has nothing to show.
    // `before`: it stops just before the line's command, whatever it is (to start the game with it).
    std::unique_ptr<Game> playedTo(const Script& s, const StoryEditor::Scene& scene, std::optional<std::size_t> line, Audio& audio, bool& reached,
                                   bool before = false);
    void drawSettings(DeveloperUi& ui, const SDL_FRect& box);
    void drawScreens(DeveloperUi& ui, const SDL_FRect& area);
    struct ScreensFrame; // (that tab this frame: what its columns share)
    bool drawScreenLists(const ScreensFrame& f);   // the screens and the parts of the one open (true: changed, the rest next frame)
    bool drawScreenPreview(ScreensFrame& f); // the screen: Try it, and parts chosen, moved and resized on it (true: changed)
    void chooseOnScreen(ScreensFrame& f);    // ...parts chosen on it, and moving or resizing them started
    bool dragOnScreen(ScreensFrame& f);      // ...parts moved or resized, lined up; written once let go (true)
    bool screenKeys(ScreensFrame& f);        // ...copy, cut, paste, duplicate, and the arrows that nudge (true: changed)
    void paintScreen(const ScreensFrame& f, bool canTry); // ...the screen drawn, and Try it and the sound's button under it
    void runTryIt(const ScreensFrame& f);    // ...Try it, this frame
    void stopTrying(); // ...and when it stops
    void drawScreenForm(const ScreensFrame& f);    // the form: the screens to start from, the chosen part's settings, or the screen's
    void drawNewScreenForm(const ScreensFrame& f); // ...+ New screen's: the screens to start from
    void drawNewPartForm(const ScreensFrame& f);   // ...+ New part's: its kind, and where it goes
    void drawBuiltInForm(const ScreensFrame& f);   // ...a built-in screen's
    void drawLineUpForm(const ScreensFrame& f);    // ...several parts': lined up, or spread out
    void drawPartForm(const ScreensFrame& f);      // ...a part's (or the screen's own)
    void drawMap(DeveloperUi& ui, const SDL_FRect& area);
    struct MapFrame;                           // (that tab this frame: the scenes, their boxes and arrows, what's pointed at)
    void layOutMap(MapFrame& f);               // ...each scene's column and box
    bool removeMapArrow(const MapFrame& f);    // ...the chosen arrow taken away (true)
    bool mapClicks(MapFrame& f);               // ...what's pointed at, and clicked (true: a scene opened)
    void drawMapArrows(const MapFrame& f);     // ...the arrows
    void drawMapScenes(const MapFrame& f);     // ...the scenes' boxes (one renamed)
    bool mapLetGo(const MapFrame& f);          // ...a scene let go: moved, or a jump drawn (true: written)
    void drawLibrary(DeveloperUi& ui, const SDL_FRect& area);
    struct LibraryFrame; // (that tab this frame: what its parts share)
    void drawLibraryFiles(const LibraryFrame& f);                          // the files of the kind chosen: listed, renamed, imported
    void drawLibraryFile(const LibraryFrame& f, const std::string& name); // the one chosen: its name in scripts, the Trash, a look at it
    void drawLibraryDefinitions(const LibraryFrame& f);                    // the definitions of the kind chosen, and the one chosen's form
    void drawVariables(DeveloperUi& ui, const SDL_FRect& area, const SDL_FRect& form); // the Library's variables
    void drawVoices(DeveloperUi& ui, const SDL_FRect& list, const SDL_FRect& form);    // ...and voiced lines
    void drawStyleSample(DeveloperUi& ui, const std::string& style, const SDL_FRect& box);
    void drawAtlas(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& box); // its frames, and its eyes and mouth to drag
    void drawSubPicture(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& box); // the picture it's cut from, to cut it out on
    float drawAnimationSample(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& room); // a square playing it: how tall it is
    void drawDefinition(DeveloperUi& ui, const std::string& kind, const StoryEditor::Definition& def, const SDL_FRect& right); // as a form
    void drawRecordTitle(DeveloperUi& ui, std::string_view kind, const std::string& name, float x, float w, float& y); // in the gallery or music room
    void drawTranslations(DeveloperUi& ui, const SDL_FRect& area);
    bool drawLanguages(DeveloperUi& ui, const SDL_FRect& left, const std::vector<std::string>& ids);   // ...the languages and their buttons (true: one did its work)
    void drawTranslation(DeveloperUi& ui, const SDL_FRect& right, const std::vector<std::string>& ids); // ...the one open: its language and its texts
    std::string languageLabel(const std::string& id);           // ...a file's language, as listed
    void openTranslation(const std::string& id);                // ...a file opened
    void fillTranslation(const std::string& id);                // ...every text the game shows, into a file
    void renameTranslation(std::string from, const std::string& to); // ...a file named after its language
    void rememberTranslation(const std::string& key);           // ...a step for undo
    void translationKeys();                                     // ...undo and redo
    void pruneTranslation();                                    // ...the texts the game doesn't show any more, taken out
    bool goTo(const std::string& path, std::size_t line); // the developer tools at that line of the scripts
    void importFiles(); // the files picked or dropped, into images/, audio/, movies/ or fonts/
    // The system's dialog to pick files of a kind (`extensions` with their dots) to import; `as`: the
    // one picked goes there, under that name (images/spanish/sign, without its extension).
    void chooseFiles(std::string_view label, std::span<const std::string_view> extensions, const std::string& as);
    // A sound file's Play button: it plays the file while every other sound waits, and is Stop while
    // it plays (`small`: > and a square). The sound stops when its button isn't drawn any more.
    void listenButton(DeveloperUi& ui, const SDL_FRect& at, const std::string& path, bool small = false);
    // A form's title row: the As text / As a form button at its right (`rightEdge`); and, as text,
    // the line as the script has it, in a box to change it: what's typed there, once it isn't empty.
    void asTextButton(DeveloperUi& ui, float rightEdge, float y);
    std::optional<std::string> drawRawLine(DeveloperUi& ui, const std::string& id, const std::string& line, float x, float w, float& y);
    void makeSample(bool choice); // the story at its first line, or its first choice
    bool paintCanvas(const std::function<void()>& paint); // into developer_.canvas, as big as the canvas
    void drawLines(DeveloperUi& ui, const SDL_FRect& box); // the log, or About
    // A form's fields, one under another (the label at the left, `labelW` wide, or over its box when
    // that's 0), each with a list to pick from when it has names or values to list: the field changed
    // and its new value, if any.
    struct FormEdit {
        std::string name, value;
        bool typed = false; // into its box, or picked from its list (else a button): one step for undo while it's typed into
    };
    std::optional<FormEdit> drawForm(DeveloperUi& ui, const std::string& id, const std::vector<StoryEditor::Field>& given, float x, float w,
                                     float labelW, float& y);
    void drawPopup(DeveloperUi& ui); // the list a field has open, over everything
    struct PopupFrame;                    // (that list this frame: where, the value, what's picked)
    void drawColourWheel(PopupFrame& pop); // ...a colour box's: its code, wheel and bars
    std::optional<std::string> drawFormatBar(DeveloperUi& ui, const std::string& id, float x, float w, float& y);
    [[nodiscard]] std::vector<ScriptError> missingFiles(const Script& script) const; // lines using pictures, sounds or videos the game lacks

    Engine& engine;       // the player they're the tools of
    DeveloperTools tools; // their state
};

} // namespace vn
