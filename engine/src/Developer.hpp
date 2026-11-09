#pragma once

// Developer mode: what the developer tools keep from frame to frame (DeveloperTools) and the tools themselves
// (Developer: Developer.cpp, a file for each page, and the files about their parts).

#include "StoryEditor.hpp"
#include "Game.hpp"
#include "Screens.hpp"
#include "Movie.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vn {

class Engine;
class DeveloperUi;
class PreviewHead;

// The editor's layout: the measurements its forms, lists, popups and the colour wheel share, for a
// 720-pixel-high window (each is scaled by k, the window's height over 720, where it's used). One
// place to change them, so they stay the same everywhere.

namespace layout {

inline constexpr float titleH = 40;          // a form's title row (its title, and a button at its right)
inline constexpr float boxH = 32;            // a box typed into or picked from
inline constexpr float rowGap = 8;           // between a form's rows
inline constexpr float labelPart = 0.3f;     // a form's labels: this much of its width
inline constexpr float partLabelPart = 0.4f; // a screen part's form: its labels are longer
inline constexpr float labelMostPart = 0.55f; // ...wider when a label needs it (Developer::labelColumn), up to this much
inline constexpr float buttonH = 30;         // a button on its own (+ New, Play, As text...)
inline constexpr float toggleW = 96;         // the As text / As a form button
inline constexpr float barButtonH = 26, barButtonGap = 4; // a bar of small buttons (the text format bar, New line's and New part's)
inline constexpr float swatch = 20;          // a colour's square in its box
inline constexpr float popupPad = 4;         // around and between what a list shows
inline constexpr float popupText = 14;       // what a popup's list says
inline constexpr float itemH = 28;           // a row of a list
inline constexpr float cellW = 92, cellH = 74, cellText = 12; // an image in a list: its cell, and a label in it
inline constexpr float listW = 260, imagesW = 400;      // a list's least width: names, or images
inline constexpr float listMaxH = 320, wheelMaxH = 440;   // a list's most height: names or images, or with the wheel over them
inline constexpr float codeH = 28;           // the colour's code box over the wheel
inline constexpr float wheelD = 150;         // the wheel's diameter
inline constexpr float barW = 16, barGap = 8; // the value and alpha bars beside it, and the gaps between
inline constexpr float boxText = 15, labelText = 13, titleText = 18; // text sizes: in a box, its label, a form's title
// The window: its page parts and their lists.
inline constexpr float leastW = 1024, leastH = 640; // the smallest window the tools fit at their own size (in a smaller one, all of them shrinks)
// The tools' own part (their buttons, in every page), small as a menu bar's words: a button's height, its words' size
// and the room either side of them, the room between buttons (and rows), and between groups.
inline constexpr float toolButtonH = 22, toolText = 12, toolPad = 8, toolGap = 3, toolGroupGap = 12;
// A page's page part: the room between its edge and what's in it, how near its edge the pointer catches it
// to resize it (with Layout on), how near the border two share (side by side) it moves both, how small it can be
// made, and how near the side of another near it (or the window's edge) a side being moved goes to it.
inline constexpr float pagePartPad = 6, pagePartGrab = 8, pagePartShare = 2, pagePartLeastW = 140, pagePartLeastH = 80, pagePartSnap = 16;
// What's in a page part needs at least this room (with less, it scrolls): a list across, and its rows down (its buttons
// under them too), and the rows of buttons that make something new (New line, New part) across.
inline constexpr float pagePartListW = 160, pagePartListRows = 3, pagePartButtonsW = 320;
// A page's columns (Story, Screens, Library), side by side: its lists at the left, the chosen thing's form at the right,
// what's chosen between them; each this much of the room. The Screens' form gives its screen room, down to this much
// of its width.
inline constexpr float sidePart = 0.18f, formPart = 0.36f, screensFormPart = 0.6f;
inline constexpr float sideLeast = 180 + 2 * (1 + pagePartPad), sideMost = 360, formLeast = 300, formMost = 720; // (and within these, to read well in any window; a list's 180 inside its page part's edge)
inline constexpr float formBarRoom = 16;     // a form's scrollbar: the room it's in at the form's right (its page part's padding in it)
inline constexpr float headingH = 18, headingText = 12; // a page part's title ("Scenes", "Library", "Preview"...): its row, over what's in it
inline constexpr float gap = 6;              // between a list and its button, between buttons, and the like
inline constexpr float listItemH = 30;       // a row of a page part's list
inline constexpr float scrollbarW = 6, scrollbarLeast = 28, scrollbarRoom = 12; // a scrollbar: how thick, its thumb at the least, and the room it takes from a list's rows
inline constexpr float listIndent = 18;      // a row inside another (an option in its choice)
inline constexpr float listPad = 10, listText = 15; // a row's text: in from its left, and its size
inline constexpr float crossW = 24, crossArmedW = 64; // a row's x, and once clicked (to click again)
inline constexpr float markW = 4;            // a row's mark at its left (a mistake, or never reached)
// Buttons.
inline constexpr float smallButtonH = 22;    // (Save as a text file, a preview's buttons, a jump's x)
inline constexpr float bigButtonH = 34;      // (Make it the game's own)
inline constexpr float wideButtonW = 120;    // the least for: Play, Replace..., Import..., Replace all (DeveloperUi::buttonWidth)
inline constexpr float widerButtonW = 160;   // the least for: + Font, + Sub-image, Go to the line, Move to the Trash, Make it the game's own, Only untranslated
inline constexpr float optionButtonW = 140;  // the least for a form's + New buttons (+ New option, + Else...)
inline constexpr float listenW = 64;         // the voice row's Play
inline constexpr float buttonText = 14, smallButtonText = 13;
inline constexpr float buttonPad = 24;       // a button in a row of them: its label and this much
inline constexpr float choicePad = 16;       // a choice's button: its label and this much
inline constexpr float previewButtonW = 80;  // the least for a preview's buttons (an animation's Play)
inline constexpr float previewHeadH = headingH + gap; // a preview's heading, with those buttons, and the gap under it
// Texts.
inline constexpr float noteText = 14;        // notes, help and problems
inline constexpr float smallText = 13;       // small labels
inline constexpr float lineText = 16;        // a plain line of text, and a button's unless it says
// A form's groups: the room over a heading (but the first), its row, its rows' labels in from it, its words; and a
// label over its box.
inline constexpr float groupGap = 6, groupStep = 26, groupIndent = 14, groupText = 15;
inline constexpr float labelAboveH = 20;
// The Library's Settings, its translations' table, the Story page's map, and About.
inline constexpr float settingLabelW = 220, settingLabelPart = 0.27f, settingFieldW = 340, settingFieldPart = 0.32f, settingProblemGap = 16;
inline constexpr float mapPadX = 20, mapPadY = 20;   // the Map: where its grid starts
inline constexpr float tableRowH = 40, languageW = 360;
inline constexpr float nodeW = 170, nodeH = 36, nodeGapX = 90, nodeGapY = 16, mapStep = 60;
inline constexpr float aboutRowH = 21;
// What's shown over a Library form (at most this high, or this much of the room), and a list's popup.
inline constexpr float sampleStyleH = 110, sampleAtlasH = 230, sampleAnimationH = 240, sampleSubImageH = 380;
inline constexpr float sampleText = 12;      // words drawn over a sample (an atlas's expression, how to mark a sub-image)
inline constexpr float sampleStylePart = 0.25f, sampleAtlasPart = 0.42f, sampleAnimationPart = 0.25f, sampleSubImagePart = 0.55f;
// An animation's timeline: its rows' names, the seconds over them, a row; a change at once (a diamond),
// a bar's ends (to drag), how near a moment a drag snaps to it, the least room for each second marked.
inline constexpr float timelineLabelW = 110, timelineRulerH = 22, timelineRowH = 22, timelineDiamond = 12, timelineEdge = 6, timelineSnap = 6,
                       timelineTickW = 48, timelineSwatch = 12; // (and a colour's square on a bar)
inline constexpr float popupShadow = 3, popupMargin = 8, popupGap = 2;
inline constexpr float tipDelay = 0.5f, tipW = 420, tipPad = 6; // a tip (the whole of a text cut to fit): once the pointer rests on it this long (seconds), at most this wide, its text this far in

} // namespace layout

// A translation file's text (a line in quotes) and the line with its translation under it, if any.
struct TextPair {
    std::size_t original = 0;
    std::optional<std::size_t> translated;
};
[[nodiscard]] std::vector<TextPair> textPairs(const std::vector<std::string>& lines);
[[nodiscard]] bool untranslatedPair(const std::vector<std::string>& lines, const TextPair& p);
void setTranslation(std::vector<std::string>& lines, const TextPair& p, const std::string& value);

// The developer tools' own keys, as kToolsVerbs has them (their controls: tools_undo...); and the keyboard a press of
// one of their buttons comes from (it's that key, whatever keys it has: SDL_KeyboardEvent::raw says which).
enum class ToolKey : std::uint8_t {
    Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, SelectAll, Reload, Quit, Story, Screens, Library, Play, StopHere,
    Next, Previous, Press, Up, Down, Left, Right, // (the keys' place: on and back, pressing, the arrows)
    Back, NextPage, PreviousPage,               // (a list or a box let go of; the pages in turn)
    Count
};
static_assert(static_cast<std::size_t>(ToolKey::Count) == kToolsVerbs.size());
inline constexpr SDL_KeyboardID kToolButton = 0xFFFF'FFFFu;
inline constexpr SDL_KeyboardID kScreenKeyboard = 0xFFFF'FFFEu; // (a key of the tools' on-screen keyboard: not a real keyboard's)

// What the developer tools keep from frame to frame (Developer.cpp and the files of their pages and parts). In developer mode the player starts in
// them, and F10 opens them over the game (the story waits meanwhile); without a game they're all the
// player shows.
struct DeveloperTools {
    enum class Page : std::uint8_t { Story, Screens, Library, About };
    bool open = false;
    Page page = Page::Story;
    // What the last button said (how it went wrong, or what it made where): logged and shown in a window of its
    // own as the frame goes on (tell), then gone.
    std::string status;
    // The tools' own part's button pressed (ToolButton::id): done as the next frame starts.
    std::optional<int> pressedTool;
    // The pages' page parts (Developer::drawPageParts): with Layout on they're moved and resized. Where each one was put, by its
    // page and name ("story/form"): as shares of the room over the tools' own part (none: where its page puts it); each
    // page's page parts as they're drawn, the last over the rest (as they were last moved). The room they're in this frame,
    // the page shown (Reset puts its back), a page part being moved or resized, and the pointer the tools show over a page part
    // (none: the game's), made as each is first shown. How far each is scrolled (across, down: by its key), when what's in
    // it needs more room than it has.
    bool editLayout = false, layoutRead = false;
    std::map<std::string, SDL_FRect, std::less<>> pageParts;
    std::map<std::string, SDL_FPoint, std::less<>> pagePartScroll;
    std::map<std::string, std::vector<std::string>, std::less<>> pagePartOrder;
    SDL_FRect room{};
    std::string shownPage;
    struct PagePartDrag {
        std::string id;    // ("page/page part")
        int sides = 0;     // the sides that follow the pointer (all four: it moves)
        SDL_FPoint from{}; // where the pointer caught it
        SDL_FRect start{}; // and where it was then
        std::string with;  // a border two share: the other page part, its side there going with it (empty: none)
        SDL_FRect withStart{};
    };
    std::optional<PagePartDrag> pagePartDrag;
    // Layout by the keys (Tab, the arrows; a controller's shoulders and d-pad): the page's page part and which of it they're on
    // (its whole: moving it; its right side or its bottom: resizing it, three to a page part), and how far the arrows have taken
    // it since they caught it (a drag, as the pointer's); its ring (where the keys are), once they've been used.
    struct LayoutKeys {
        std::string page;
        std::size_t handle = 0;
        SDL_FPoint by{};
        bool dragging = false, ring = false;
        SDL_FRect ringBox{};
    };
    LayoutKeys layoutKeys;
    std::optional<SDL_SystemCursor> pointer, wantPointer;
    std::array<CursorPtr, SDL_SYSTEM_CURSOR_COUNT> cursors{};
    std::optional<std::pair<std::string, std::size_t>> stoppable; // the Story page's chosen line Stop here is for (its file, line), as last drawn
    std::string loggedStatus;
    bool failed = false;
    int packIn = 0;         // frames until Pack packs (the game read again first)
    bool play = false;      // Play: the game starts from the beginning once the scripts are read again
    std::string playScene;  // Play this scene: the scene to start then
    std::optional<std::size_t> playLine; // Play from this line: its line in the scene's file
    std::size_t aboutScroll = 0; // About's lines scrolled down
    // What happened since the last frame; the drawing acts on it (buttons act when drawn). Where the pointer is (away,
    // -1: the keys are in charge, so nothing's lit under it, until it moves again), and whether it moved; the wheel's
    // turns and where they're turned (under the pointer, or where the keys are: the right stick's).
    float mouseX = -1.0f, mouseY = -1.0f, wheel = 0.0f, wheelAcross = 0.0f; // (the wheel: up is positive; across: right is, as Shift and the wheel down)
    float wheelX = -1.0f, wheelY = -1.0f;
    bool pointerMoved = false;
    // A controller: its buttons are the tools' keys (their controls' pad_ keys; its left stick is the d-pad: Engine::padAxis);
    // the d-pad and the shoulders go again and again while held (the one held, and when it goes next); its A pressed this
    // frame (a box it hands the typing to brings up the on-screen keyboard).
    std::optional<std::pair<ToolKey, float>> padHeld;
    bool padPressed = false;
    bool clicked = false, doubleClick = false;
    float k = 1.0f;     // this frame's scale (DeveloperUi::k) and the window's size the tools are laid out in
    SDL_FPoint size{};
    std::string typed;
    std::vector<SDL_KeyboardEvent> keys;
    // The Story page: the visual editor.
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
    struct BarDrag {              // a scrollbar's thumb being dragged
        const void* of = nullptr; // (what it scrolls: the place its scroll is kept)
        float grab = 0.0f;        // where on the thumb it was caught
    };
    std::optional<BarDrag> barDrag;
    float linesScroll = 0.0f, scenesScroll = 0.0f, charactersScroll = 0.0f, formScroll = 0.0f, usesScroll = 0.0f;
    float formHeight = 0.0f; // how tall the form was last frame (it scrolls no further than its ends)
    // The game's size changed in Settings: the size before (the screens are stretched to the new one
    // once the box is left), and the change's undo key.
    std::optional<std::string> sizeFrom;
    std::string sizeKey;
    std::vector<std::size_t> moreRows;          // more lines of the scene chosen (Shift+click), by their lines
    std::optional<std::size_t> collapseTo;      // a chosen one pressed: only it stays chosen, unless it's dragged
    std::optional<std::size_t> rowInSight;      // a line just added (by its line): the list scrolls to it
    // The Library page: what's listed (a kind of definition, "variables", "voices", or the files:
    // "images", "sounds", "movies" or "fontfiles") and the one chosen (a file: also what's made from it, a sub-image or a font, by
    // its index in the editor's definitions, and the files whose list of those is open); files picked
    // to import (from the file chooser's thread, or dropped on the window), copied into the game when
    // the tools are next drawn.
    std::string library = "images";
    std::optional<std::size_t> libraryItem, librarySub;
    std::vector<std::string> libraryOpen;
    std::string libraryFind; // a file to choose once it's listed (by the name scripts call it)
    float kindScroll = 0.0f, libraryScroll = 0.0f;
    struct Imports {
        std::mutex mutex;
        std::vector<std::pair<std::string, std::string>> files; // each one's path, and where it goes (empty: by its kind, under its own name)
        std::optional<SDL_FPoint> dropAt;                     // where the last drop landed (on the Screens page's screen: the image goes there)
        std::string as;                                       // where the one picked in the file chooser that's open goes, without its extension
        std::string pattern, label;                           // (that file chooser's filter)
        SDL_DialogFileFilter filter{};                        // ...pointing at them, while it's open
        bool open = false;                                    // a file chooser is open (one at a time: its filter stays as it is)
    } imports;
    struct Translation { // the Library's translations' file, as lines
        std::string id;
        std::vector<std::string> lines;
        bool gone = false; // (a step for undo: its file isn't there)
    } translation;
    std::vector<Translation> translateUndo, translateRedo; // its files before each change (and each undo)
    std::string translateKey;                              // (typing into one box: one step)
    bool untranslatedOnly = false;
    struct LanguageLabel { // the Library's translations: each file's language as its list says it, when the file was read, and the list of files then
        SDL_Time read = 0;
        std::string label;
        std::uint64_t generation = 0;
    };
    std::map<std::string, LanguageLabel> languageLabels;
    std::map<std::string, bool, std::less<>> spelling;                        // "language|word": spelt right (the system's dictionary, asked once)
    std::map<std::string, std::vector<std::string>, std::less<>> spellingGuesses; // "language|word": what it may be instead
    float translationScroll = 0.0f;
    std::size_t translateChecked = SIZE_MAX;               // the scripts' version its texts were last checked against
    TexturePtr fontSample; // the chosen font file's sample, and that file
    std::string fontSampleOf;
    // The chosen movie: a still of its first image, or playing (heard through the player's own
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
        // The keys in it: the entry they're on, how they move it this frame (across, down) and whether Enter picks it.
        std::optional<std::size_t> keyed;
        SDL_Point keyMove{};
        bool keyPick = false;
    } popup;
    TexturePtr wheelImage; // the colour wheel's image, `wheelImageSize` pixels across
    int wheelImageSize = 0;
    std::optional<std::pair<std::string, std::string>> picked;
    std::string openList; // a box whose list opens once it's drawn (a new line's first box, focused by the editor)
    bool popupClick = false, overPopup = false;
    float popupWheel = 0.0f;
    float mapX = 0.0f, mapY = 0.0f;   // the Story page's map: how far it's looked around
    std::optional<std::size_t> mapFollows; // ...the scene it last brought into sight (it brings each one chosen)
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
    // The tools' on-screen keyboard, for typing into a box without a keyboard: a controller's A that hands a box the typing
    // (on the box, or on + New...) brings it up, and so does a click that does, until a real keyboard's been used; a real
    // key puts it away (Esc only that), and so do B and Done. Its keys as a US keyboard has them (Shift: the capitals and
    // the signs over them), then Shift, Space, the caret back and on, Delete and Done.
    struct ScreenKeys {
        bool open = false, shift = false;
        std::string box;      // the box it types into (focus)
        int row = 1, col = 0; // the key the controller is on
        SDL_FRect area{};     // where it was drawn: a click there is its
        std::vector<std::pair<SDL_FRect, std::pair<int, int>>> keys; // each key as drawn: its place, row and column
    } screenKeys;
    // A question or a message, and files to choose, in a window of the tools' own while a controller is in use (it can't
    // answer the system's windows, which the mouse and the keys have: Developer::ask, chooseFiles). The question: its
    // buttons in a row, the keys (or the d-pad) between them, Enter (A) the one they're on, Esc (B) the one that keeps
    // things as they are; then what it does with the answer.
    struct Asking {
        std::string title, body;
        std::vector<std::string> buttons;
        std::size_t at = 0, keep = 0;
        std::function<void(std::size_t)> then;
        std::vector<SDL_FRect> boxes; // (as drawn)
    };
    std::optional<Asking> asking;
    // The files: a folder's folders (and the one it's in, ".."), then its files of the kinds wanted, up and down, Enter (A)
    // opening a folder or importing a file (one for a place of its own, `as`: then it closes), Esc (B) or Done closing.
    struct FilePicker {
        std::string label, as, folder;
        std::vector<std::string> extensions; // (".png"...)
        std::vector<std::pair<std::string, bool>> entries; // (a name, and whether it's a folder)
        std::size_t at = 0;
        float scroll = 0.0f;
        std::set<std::string, std::less<>> taken; // (imported from this folder so far)
        std::vector<SDL_FRect> boxes;             // (as drawn: each entry's, then Done)
        SDL_FRect list{};
    };
    std::optional<FilePicker> filePicker;
    std::string pickerFolder; // (where it was last)
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
    // The keys' place in the tools (Tab and Shift+Tab move it, and the arrows, to the nearest that way): what can be
    // pressed, typed into or chosen from (a key stop), in the order they're met (each page part's in turn, by where it is:
    // down the columns, then across; the tools' own part last). The last whole frame's (`keyStops`) and this one's so far;
    // the one the keys are on (by its key: its page part's name and its place there) and whether it's ringed (the keys are
    // in charge, not the pointer); what Enter or Space pressed this frame, and which way up or down moves in a list (-1, 1).
    // Each stop says which arrows it uses itself now (own): a list up and down (not at its ends), a box being typed in left
    // and right (and down, for its list), the screen's preview all four (moving its chosen parts).
    static constexpr std::uint8_t kOwnLeft = 1, kOwnRight = 2, kOwnUp = 4, kOwnDown = 8, kOwnAll = 15;
    struct KeyStop {
        enum class Kind : std::uint8_t { Press, Type, List };
        Kind kind = Kind::Press;
        std::string key;          // ("story/lines#2")
        SDL_FPoint order{};       // its page part's place
        SDL_FRect box{};
        SDL_Rect clip{};          // where it shows
        std::string words;        // what it is and says (a box's label and value): for a screen reader
        std::string id, value;    // a box's (Type): its id and words
        float* scroll = nullptr;  // what scrolls it into sight, in the room that shows it
        SDL_FRect view{};
        std::uint8_t own = 0;
    };
    std::vector<KeyStop> keyStops, keyStopsNow;
    std::string keyGroup, keyHeading, keyLabel; // (this frame's: the page part being drawn, its title, and a form row's label)
    SDL_FPoint keyOrder{};
    std::size_t keyCount = 0;
    bool keyStopsWhole = true, keyStopsOff = false; // (every page part was drawn; what's drawn now isn't a stop: an open list)
    std::string keyAt, keyPress;
    bool keyRing = false;
    // The keys are in charge now: their ring shows, and nothing's lit under the pointer until it moves again.
    void keysTakeOver() {
        keyRing = true;
        if (!held) mouseX = mouseY = -1.0f;
    }
    // The stop the keys are on: the box being typed in (it keeps them), else theirs (keyAt).
    [[nodiscard]] const KeyStop* keysOn() const {
        if (!focus.empty())
            if (const auto box = std::ranges::find_if(keyStops, [&](const KeyStop& s) { return s.kind == KeyStop::Kind::Type && s.id == focus; }); box != keyStops.end()) return &*box;
        const auto at = std::ranges::find(keyStops, keyAt, &KeyStop::key);
        return at != keyStops.end() ? &*at : nullptr;
    }
    std::string toldKey, toldWords; // (what a screen reader was told last: the stop and its words; messages are the system's own windows)
    std::optional<Page> toldPage;
    int keyStep = 0;
    struct Scroller {
        float* scroll;
        SDL_FRect view;
    };
    std::vector<Scroller> scrollers; // (what's being drawn scrolls by these: the innermost last)
    std::string focusOwner; // the field (or "raw") the buffer belongs to, and its form when it started
    StoryEditor::Kind focusKind = StoryEditor::Kind::Other;
    std::vector<StoryEditor::Field> focusFields;
    Sound silence;               // (never started: what the tools' own games are heard through)
    // Copies of what the player has seen and kept, for the games the tools play on their own (Play from
    // this line's, the Screens page's sample): nothing the player has changes.
    ReadLog playRead;
    Variables playPersistent;
    Flowchart playFlowchart;
    TexturePtr canvas; // what the Screens page and the Library's samples draw into
    // The Screens page: the screen open (its name: the game's own, or a built-in one), the part chosen
    // (its line; none: the screen's own settings), and a part being moved or resized.
    std::string screenName;
    std::optional<std::size_t> part;
    bool picking = false;  // + New screen: the screens to start from show
    bool addInto = false; // New part goes in the part chosen (else on the screen), as it was set for that part
    std::optional<std::size_t> addIntoOf;
    bool held = false;     // the mouse button is down
    // A text shown cut to fit (DeveloperUi::tip): the whole of the one the pointer is on, this frame's and
    // the one before's, and since when (the tools' clock); drawTip() shows it once the pointer rests there.
    std::string tip, tipBefore;
    float tipSince = 0.0f;
    std::optional<SDL_FPoint> tipAt; // (a tip for what the keys are on: where it goes, under that; none: by the pointer)
    std::map<std::string, float, std::less<>> formatSince; // (a text format bar button pointed at, and since when: what it does plays)
    struct Drag {
        std::size_t part = 0, element = 0; // its line, and its part of the screen as read
        bool resize = false;
        SDL_FRect from{};                  // its box then
        float x = 0.0f, y = 0.0f;          // where the mouse was then (in the window)
        float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f; // its x= y= width= height= then
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
    SDL_FRect shownScreen{}; // where the Screens page showed the screen last (the mouse over it: on the screen)
    std::size_t wordsVersion = SIZE_MAX, gameWords = 0; // the game's words, as of that version of the scripts
    // The open scene's lines as the list says them, and which never ran in a play (as of that version, scene and set of lines run).
    std::size_t listVersion = SIZE_MAX, listScene = SIZE_MAX, listReached = SIZE_MAX, neverRan = 0;
    std::vector<std::string> rowSummaries;
    std::vector<bool> rowRan;
    std::size_t wrongScenesVersion = SIZE_MAX; // which scenes have a mistake, as of that version (wrongScenes())
    std::vector<bool> scenesWrong;
    std::size_t colourVersion = SIZE_MAX; // the characters' name colours, by lower-case name, as of that version
    std::map<std::string, std::optional<SDL_Color>, std::less<>> characterColours;
    std::uint64_t soundGeneration = 0; // sound files found by name, as of that list of files (see filesGeneration)
    std::map<std::string, std::string> soundPaths;
    std::map<std::string, std::optional<std::string>> foundSound;
    std::size_t sceneWordsVersion = SIZE_MAX, sceneWordsScene = SIZE_MAX, sceneWords = 0; // the open scene's, as of that version
    // The files in each folder with an ending, as listed at a generation: listed again at the next (twice a
    // second, and when files are imported, renamed or thrown away), not every frame.
    std::map<std::string, std::pair<std::uint64_t, std::vector<std::string>>> fileLists;
    bool saidNoGame = false; // (the hint that there's no game yet went to the log: once)
    // The developer tools' language (the tools' own part's language buttons): the translations in "developer tools translations" next to the player
    // (each file's name without .vnscript, its language's name and code), as of that folder's list of
    // files; and the one in use (empty: English).
    struct ToolsLanguage {
        std::string id, name, code;
    };
    std::vector<ToolsLanguage> toolsLanguages;
    std::vector<std::string> toolsLanguageFiles;
    std::string toolsLanguage;
    // Stop here: the lines the story stops just before when it's played (the developer tools open at
    // them), each by its file, line and words; kept in stops.txt with the player's data, and moved with
    // their lines as the scripts change (`stopsSeen`: the lines of the files they're in, as they were).
    struct StopMark {
        std::string path;
        std::size_t line = 0;
        std::string text;
    };
    std::vector<StopMark> stops;
    std::map<std::string, std::vector<std::string>, std::less<>> stopsSeen;
    std::set<std::pair<std::string, std::size_t>> runLines; // the lines with a command of their own (Stop here is for them), as of `checked`
    // Next line: the story stops at the next line it gets to after `stepFrom` (a scene's line: one of
    // `rowStarts`, as of `rowStartsVersion`). `passing`: the line it stopped at, done first when it goes on.
    bool stepping = false;
    std::pair<std::string, std::size_t> stepFrom;
    std::set<std::pair<std::string, std::size_t>> rowStarts;
    std::size_t rowStartsVersion = SIZE_MAX;
    std::optional<std::pair<std::string, std::size_t>> passing;

    // A character's name colour, when it has one (the lists that name characters show it as a square).
    [[nodiscard]] std::optional<SDL_Color> characterColour(const Palette& palette, std::string_view name);
    // A sound file by its name (and whether it's there), as of the list of files: not looked for on disk
    // every frame.
    [[nodiscard]] const std::string& cachedSound(const Assets& assets, const std::string& name);
    [[nodiscard]] std::optional<std::string> cachedFoundSound(const Assets& assets, const std::string& name);
    // The files in a folder with an ending, as listed once a generation (see filesGeneration); and whether
    // a file is there, as of that generation too: not looked for on disk every frame.
    [[nodiscard]] const std::vector<std::string>& cachedFiles(const std::string& folder, std::string_view ext);
    [[nodiscard]] bool cachedExists(const std::string& path);
    std::map<std::string, bool, std::less<>> filesThere;
    std::uint64_t filesThereGeneration = 0;
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
    // The tools' own keys, as their controls have them this frame (Developer::readToolKeys), and whether a key pressed
    // is one of them (or a press of its button).
    std::array<std::vector<KeyChord>, static_cast<std::size_t>(ToolKey::Count)> toolKeys;
    // Whether `key` is one of the tools' own (as their controls have them); `repeating`: held down too (it comes again, as
    // an arrow moving on does); `anyShift`: with Shift too (Shift and an arrow: a bigger step).
    [[nodiscard]] bool isKey(const SDL_KeyboardEvent& key, ToolKey which, bool repeating = false, bool anyShift = false) const;
    std::uint64_t filesGeneration = 1;
    // The Voices list: the lines characters with voices say, as of the scripts read (`checked`), and
    // whether each one's file is there (in a translation's folder too: cachedExists).
    struct VoicedLine {
        std::string file, who, text, path;
        int line = 0;
    };
    std::vector<VoicedLine> voiced;
    std::size_t voicedVersion = SIZE_MAX;
    // The lines a variable is used in: looked for again when the variable (or the scripts) change, not every
    // frame.
    struct Looked {
        std::string what;
        std::size_t version = SIZE_MAX;
        std::vector<StoryEditor::Found> found;
    };
    Looked lookedUses;
    std::size_t unusedVersion = SIZE_MAX, unusedCount = 0; // the Library's files no script names, as of that version and list
    std::string unusedOf;
    std::set<std::string> unused;
    std::vector<std::size_t> moreParts;     // more parts chosen (Shift+click), by their lines
    std::optional<float> guideX, guideY;    // what the part being moved lines up with (on the canvas)
    std::unique_ptr<Game> sample; // the story at its first line (or choice), for the textboxes in screens
    bool sampleMade = false;
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
    // The Library's sub-image: a box being dragged on the image it's cut from (from where, in
    // the image's pixels, and where the mouse was pressed).
    struct CutDrag {
        SDL_FPoint from{};
        float x = 0.0f, y = 0.0f;
    };
    std::optional<CutDrag> cutDrag;
    // The Library's animation: where its timeline's playhead is (seconds into one time through), whether it
    // plays on (from when, by the tools' clock), and the change chosen on it (its place in the timeline's
    // bars); a bar, one of its ends, the playhead or the end being dragged (where the pointer took it, in
    // seconds, and the bar and the end as they were then; the seconds the timeline showed then).
    struct AnimationView {
        std::string name; // (another chosen: it starts again)
        float playhead = 0.0f, playedAt = 0.0f;
        bool playing = true;
        std::optional<std::size_t> bar;
        enum class Drag : std::uint8_t { None, Bar, Start, End, Playhead, Length };
        Drag drag = Drag::None;
        float grab = 0.0f, span = 0.0f, wasLength = 0.0f;
        StoryEditor::Bar was;
    };
    AnimationView animation;
    Wiper sampleWiper; // (its sample's wipe)
    bool pickingAnimation = false; // + New animation: the animations to start from show (the one pointed at playing, since `startShownAt`)
    std::size_t startShown = 0;
    float startShownAt = 0.0f;
    // The Library's sample of a style: the scripts with a screen of a button and a text in it, at an
    // editor version.
    std::unique_ptr<Script> styleScript;
    std::unique_ptr<ScreenView> styleView;
    std::pair<std::size_t, std::string> styleOf{SIZE_MAX, {}};
    // The games and views made from `script` (it's read again after every change).
    void dropPreviews() {
        sample.reset();
        sampleMade = false;
        screenView.reset();
        drag.reset();
        styleView.reset();
        styleScript.reset();
        styleOf = {SIZE_MAX, {}};
    }
    // Text the tools draw, laid out once and kept by what it is (DeveloperUi::block: its font, the width
    // it wraps at and its words), wherever in a frame it's drawn; one not drawn for a while goes.
    struct KeptText {
        TextBlock block;
        std::uint64_t drawn = 0; // (the frame it was drawn in last)
    };
    std::unordered_map<std::string, KeptText, StringHash, std::equal_to<>> texts;
    std::string textKey;         // (one buffer for the key looked for)
    std::uint64_t textFrame = 0; // the frames drawn
};

// Where a page part's list goes, and the rows of buttons under it (DeveloperUi::listRoom).
struct ListRoom {
    SDL_FRect list{};
    std::vector<SDL_FRect> rows;
};

// A button of the tools' own part: what it does (Developer::runCommand), its words (and the longest they can be, which
// sets how wide it is: the buttons stay where they are), whether it can be pressed now (else it's grey), whether it's
// on (lit), and a mistake in what it shows (the lists' red mark).
struct ToolButton {
    int id = 0;
    std::string label, longest;
    bool enabled = true, on = false, wrong = false;
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
    [[nodiscard]] bool toolKey(const SDL_Event& e, ToolKey which); // a key of the tools' own (F10, their Play: as the game's controls have it)
    void dropPreviews() { tools.dropPreviews(); } // (they use the images and the renderer: let go of before those are)
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
    void loadReached();   // the script lines the game ran in the developer's plays (reached.txt), and
    void recordReached(); // the ones it ran since last frame
    void loadStops(); // Stop here's lines (stops.txt, with the player's data)
    // Whether the story stops just before command `pc` of the game's scripts: a line with Stop here, or
    // the next line after Next line. And when it has: the developer tools, at that line.
    [[nodiscard]] bool stopsBefore(std::size_t pc);
    void storyStopped();
    // The developer tools' language as they start: the one chosen last (language.txt in their own folder; empty:
    // English), else the computer's, when there's a translation for it.
    void startToolsLanguage();
    void openDeveloper();
    void closeDeveloper(); // (the game reads the scripts again if the editor changed them)
    void backToGame();     // or Play, when no game is going on
    void backToEditor();   // developer mode: the developer tools, until Play (at the start, and where the player would close)
    void developerEvent(const SDL_Event& e);
    void stickWheel(float up, float across); // the right stick: the wheel turned, where the keys are (their ring), else under the pointer
    void updateDeveloper();
    void drawDeveloper();
    [[nodiscard]] std::string_view shownPageName() const { return pageName(tools.page); } // the page open (the window's title says it)
    void playFromLine(const std::string& scene, std::optional<std::size_t> line); // the game, as it is at that line (none: the scene's first)

private:
    // What the tools' own part's buttons do (ToolButton::id; Developer::toolButtons has them in their order).
    enum ToolCommand : int {
        kNewGame = 1, kLookAgain, kReload, kPack, kFolder, kQuit,
        kUndo, kRedo, kCut, kCopy, kPaste, kDuplicate, kDelete, kSelectAll,
        kStoryPage, kScreensPage, kLibraryPage, kEditLayout, kResetLayout,
        kPlay, kNextLine, kPlayScene, kStopHere,
        kToolsTranslation, kAbout,
        kToolsLanguage // (+ its place among the language buttons: 0 English, then the translations; last, so they come after every other)
    };
    static constexpr std::array kPages{std::pair{kStoryPage, DeveloperTools::Page::Story}, std::pair{kScreensPage, DeveloperTools::Page::Screens},
                               std::pair{kLibraryPage, DeveloperTools::Page::Library}}; // (in this order: their keys are tools_story, tools_screens and tools_library)

    // The pages' page parts (PageParts.cpp). A page part: its name in its page, its title, where the page puts
    // it, and what draws what's under its title in the room it's given (true: it changed something, so the page parts
    // after it wait for the next frame); a form's scrollbar goes beside it, in room kept at its right; and the room what's
    // under its title needs (0: any; `tallAt`: how tall at a width, for what wraps): in less, it's drawn that big and
    // scrolls under the title, down and across as needed. `head` draws buttons at the right of its title, as a preview's
    // (true: it changed something); its title's row is then layout::previewHeadH high, a gap under them.
    // drawPageParts: the page's page parts where they are, bottom to top, each with its edge (true: one changed something);
    // with Layout on they're moved and resized. The layout as kept (layout.txt, in the tools' own folder), a page's put
    // back, and the pointer the tools show (none: the game's).
    struct PagePart {
        std::string_view id;
        std::string heading;
        SDL_FRect at;
        std::function<bool(const SDL_FRect&)> draw;
        bool scrollsBeside = false;
        SDL_FPoint least{};
        std::function<float(float)> tallAt;
        std::function<bool(PreviewHead&)> head;
    };
    bool drawPageParts(DeveloperUi& ui, std::string_view page, std::span<PagePart> pageParts);
    // What a page part's edge and the room inside it take from its room: across, and down. A page part as tall as what's
    // in it is that much taller.
    [[nodiscard]] SDL_FPoint pagePartChrome(float k) const;
    // The page parts most pages have, made alike: a list with `rows` rows of buttons under it (DeveloperUi::listRoom;
    // `draw` is given where they go), and a form (its scrollbar beside it in room kept at its right; `filled`: there's
    // something chosen to show in it). Each needs room for a few of its rows (a list's and its buttons, a form's labels
    // and boxes; an empty form none): with less, it scrolls.
    PagePart listPagePart(DeveloperUi& ui, std::string_view id, std::string heading, const SDL_FRect& at, std::size_t rows, std::function<bool(const ListRoom&)> draw);
    PagePart formPagePart(DeveloperUi& ui, std::string_view id, std::string heading, const SDL_FRect& at, bool filled, std::function<bool(const SDL_FRect&)> draw);
    // The keys' place (DeveloperTools::keyStops): Tab and Shift+Tab move it as the frame starts, Enter, Space and up and
    // down act on it; and once the frame's drawn its stops are kept, and a ring goes round the one the keys are on.
    void keyMoves();
    void keyRing(DeveloperUi& ui);
    void readLayout();
    void saveLayout();
    void resetLayout();
    void showPointer(std::optional<SDL_SystemCursor> wanted);
    // The tools' own part: its buttons as the tools are now (group by group), what one does (ToolButton::id), where
    // they go in a width (and how tall they are), and the part drawn (at the window's foot).
    [[nodiscard]] std::vector<std::vector<ToolButton>> toolButtons();
    void runCommand(int command);
    void pressTool(ToolKey which); // one of the tools' keys pressed, whatever keys it has (its button's, a controller's)
    void padButton(const SDL_Event& e); // a controller's button down or up: the tools' keys it is
    void padMoves(); // a controller held: its d-pad (and so its left stick) and shoulders again
    // The on-screen keyboard (DeveloperTools::screenKeys): brought up for the box typed into, a controller's button on it (true:
    // it took it), its key moved to and pressed, a click on it, and drawn over the rest.
    void openScreenKeys(bool all = false); // (all: the box's words chosen, so what's typed takes their place)
    // A question (or a message: one button) in the system's window, or the tools' own while a controller is in use; `keep`:
    // the button that keeps things as they are (Esc's); then `then` with the button pressed. Without a window to show it in
    // (SDL's offscreen video), as `keep` was pressed.
    void ask(std::string title, std::string body, std::vector<std::string> buttons, std::size_t keep, SDL_MessageBoxFlags kind,
             std::function<void(std::size_t)> then = {});
    bool modalFrame(); // the frame's input to the tools' own question or file picker, when one's open (true: it took it)
    void drawModal(DeveloperUi& ui);
    void readPickerFolder();
    bool screenKeysButton(SDL_GamepadButton button);
    void screenKeysMove(int dx, int dy);
    void screenKeysPress(int row, int col);
    void screenKeysFrame();
    void drawScreenKeys(DeveloperUi& ui);
    void readToolKeys(); // (DeveloperTools::toolKeys, as the scripts have them now)
    struct ToolsPlaced {
        std::vector<std::vector<SDL_FRect>> buttons; // (group by group)
        std::vector<SDL_FRect> lines;                // between groups side by side
        float h = 0.0f;
    };
    [[nodiscard]] static ToolsPlaced placeToolButtons(DeveloperUi& ui, const std::vector<std::vector<ToolButton>>& groups, float x, float y, float w);
    void drawToolsPart(DeveloperUi& ui, const std::vector<std::vector<ToolButton>>& groups, const SDL_FRect& at);
    [[nodiscard]] static std::string_view pageName(DeveloperTools::Page page); // a page's name, in the tools' language
    [[nodiscard]] std::string translationPath(const std::string& id) const; // a translation's file
    void saveTranslation();                             // the Library's translations' file, written
    [[nodiscard]] std::string translationSheet() const; // its spreadsheet's path (next to the game folder)
    std::string exportTranslationSheet();               // written, and read back: both say how it went
    std::string importTranslationSheet();
    void drawStory(DeveloperUi& ui, const SDL_FRect& area);
    struct StoryFrame; // (that page this frame: what its page parts share)
    bool drawScenesList(const StoryFrame& f, const ListRoom& at);     // the scenes and + New scene (true: changed, the rest next frame)
    bool drawCharactersList(const StoryFrame& f, const ListRoom& at); // the characters, and + New character (true: changed)
    bool drawLinesList(StoryFrame& f, const ListRoom& at);              // the scene's lines: chosen, moved, copied (true: changed)
    bool drawRowList(const StoryFrame& f, const SDL_FRect& box);          // ...the list: chosen, moved, deleted (true: changed)
    bool storyClipboard(const StoryFrame& f);                             // ...copy, cut, paste, duplicate (true: changed)
    bool drawNewLine(const StoryFrame& f, const SDL_FRect& box);          // New line: a button for each kind (true: one added)
    bool drawWholeStory(const StoryFrame& f, const SDL_FRect& box);       // Whole story: its length, as a text file, its stops
    std::string storyCounts(const StoryFrame& f);                         // ...what's said there: words, reading time, lines never run
    void drawStoryForm(const StoryFrame& f, const SDL_FRect& right);      // the chosen line's (or character's) form
    std::unique_ptr<Game> silentGame(const Script& s); // a game of the tools' own, with copies of what the player has seen and kept
    std::unique_ptr<Game> playedTo(const Script& s, const StoryEditor::Scene& scene, std::optional<std::size_t> line); // (Play from this line's)
    void drawSettingsForm(DeveloperUi& ui, const SDL_FRect& right); // the Library's Settings, as a form
    void drawScreens(DeveloperUi& ui, const SDL_FRect& area);
    struct ScreensFrame; // (that page this frame: what its columns share)
    bool drawScreensList(const ScreensFrame& f, const ListRoom& at); // the screens, and + New screen (true: changed, the rest next frame)
    bool drawPartsList(const ScreensFrame& f, const ListRoom& at);   // the parts of the one open (true: changed)
    bool drawPreview(ScreensFrame& f); // the screen: parts chosen, moved and resized on it (true: changed)
    bool drawNewPart(ScreensFrame& f);       // New part: a button for each kind (true: one added)
    void drawGoesIn(const ScreensFrame& f, PreviewHead& row); // ...and where it goes, at the right of its title
    void chooseOnScreen(ScreensFrame& f);    // ...parts chosen on it, and moving or resizing them started
    bool dragOnScreen(ScreensFrame& f);      // ...parts moved or resized, lined up; written once let go (true)
    bool screenKeys(ScreensFrame& f);        // ...copy, cut, paste, duplicate, and the arrows that nudge (true: changed)
    void paintScreen(const ScreensFrame& f); // ...the screen drawn, with its heading
    void drawScreensForm(const ScreensFrame& f);    // the form: the screens to start from, the chosen part's settings, or the screen's
    void drawNewScreenForm(const ScreensFrame& f); // ...+ New screen's: the screens to start from
    // The sizes of the texts without one in screen lines written in pixels, for placesFromPixels (ReadyMadeScreens.cpp).
    [[nodiscard]] std::map<std::size_t, SDL_FPoint> ownSizes(const std::vector<std::string>& lines, const std::vector<std::string>& needs, float width, float height, float scale);
    void drawBuiltInForm(const ScreensFrame& f);   // ...a built-in screen's
    void drawLineUpForm(const ScreensFrame& f);    // ...several parts': lined up, or spread out
    void drawPartForm(const ScreensFrame& f);      // ...a part's (or the screen's own)
    bool drawMap(DeveloperUi& ui, const SDL_FRect& area); // (true: changed, the rest next frame)
    struct MapFrame;                           // (the map this frame: the scenes, their boxes and arrows, what's pointed at)
    void layOutMap(MapFrame& f);               // ...each scene's column and box
    bool removeMapArrow(const MapFrame& f);    // ...the chosen arrow taken away (true)
    bool mapClicks(MapFrame& f);               // ...what's pointed at, and clicked (true: another scene's lines shown)
    void drawMapArrows(const MapFrame& f);     // ...the arrows
    void drawMapScenes(const MapFrame& f);     // ...the scenes' boxes (one renamed)
    bool mapLetGo(const MapFrame& f);          // ...a scene let go: moved, or a jump drawn (true: written)
    void drawLibrary(DeveloperUi& ui, const SDL_FRect& area);
    struct LibraryFrame; // (that page this frame: what its page parts share)
    // Each kind's list or its form, the kind's two page parts (true: the list changed something, the rest next frame).
    enum class KindPagePart : std::uint8_t { List, Form };
    bool drawLibraryFiles(const LibraryFrame& f, KindPagePart pagePart);       // the files of the kind chosen: listed, renamed, imported
    void drawLibraryFile(const LibraryFrame& f, const std::string& name); // the one chosen: its name in scripts, the Trash, a look at it
    bool drawLibraryDefinitions(const LibraryFrame& f, KindPagePart pagePart); // the definitions of the kind chosen, and the one chosen's form
    bool drawVariables(const LibraryFrame& f, KindPagePart pagePart); // the Library's variables
    // ...a list's copy, cut, paste and duplicate: `text` the chosen one's lines, `chosen` its definition (none: a built-in
    // one, copied, not cut), `choose` given the pasted one's index among the kind's, or none once it's cut (true: done).
    bool libraryClipboard(const std::string& list, std::string_view kind, const std::string& text, const StoryEditor::Definition* chosen,
                          const std::function<void(std::optional<std::size_t>)>& choose);
    bool drawVoices(const LibraryFrame& f, KindPagePart pagePart);    // ...and voiced lines
    void drawStyleSample(DeveloperUi& ui, const std::string& style, const SDL_FRect& box);
    void drawAtlas(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& box); // its frames, and its eyes and mouth to drag
    void drawSubImage(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& box); // the image it's cut from, to cut it out on
    // An animation in the Library: how it looks at its timeline's playhead, then how often it plays, its
    // timeline (drawTimeline: a row for each thing it changes, a bar for each change) and the chosen
    // change's form. The sample: a square in the middle of the game's screen, as a part there would
    // be (where it's drawn).
    void drawAnimation(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& room);
    SDL_FRect drawAnimationSample(DeveloperUi& ui, const Animation& a, float seconds, const SDL_FRect& room);
    void drawNewAnimationForm(DeveloperUi& ui, const SDL_FRect& right); // + New animation: the ones to start from
    float drawTimeline(DeveloperUi& ui, const StoryEditor::Definition& def, const StoryEditor::Timeline& timeline, float x, float y, float w);
    void drawDefinition(DeveloperUi& ui, const std::string& kind, const StoryEditor::Definition& def, const SDL_FRect& right); // as a form
    void drawBuiltInControl(DeveloperUi& ui, const std::string& name, const std::string& line, const SDL_FRect& right); // ...a built-in control's
    void drawRecordTitle(DeveloperUi& ui, std::string_view kind, const std::string& name, float x, float w, float& y, float labelW = 0.0f); // in the gallery or music room, or its caption (0: its own labels' column)
    std::vector<std::string> translationIds(); // the Library's translations: their files (undo and redo taken, texts the game doesn't show taken out)
    bool drawTranslationsList(DeveloperUi& ui, const ListRoom& at, const std::vector<std::string>& ids);  // ...the translations (a language each) and their buttons (true: one did its work)
    void drawTranslationForm(DeveloperUi& ui, const SDL_FRect& right, const std::vector<std::string>& ids); // ...the one open: its language and its texts
    std::string languageLabel(const std::string& id);           // ...a file's language, as listed
    void openTranslation(const std::string& id);                // ...a file opened
    void fillTranslation(const std::string& id);                // ...every text the game shows, into a file
    void renameTranslation(std::string from, const std::string& to); // ...a file named after its language
    void nameAfterLanguage(const std::vector<std::string>& ids); // ...the one open's file named after its language
    void setLanguage(const std::string& typed, const std::string& key); // ...the one open's language set, as typed
    void removeTranslation(const std::string& id);              // ...a file to the Trash
    void rememberTranslation(const std::string& key);           // ...a step for undo
    void translationKeys();                                     // ...undo and redo
    void pruneTranslation();                                    // ...the texts the game doesn't show any more, taken out
    bool goTo(const std::string& path, std::size_t line); // the developer tools at that line of the scripts
    void importFiles(); // the files picked or dropped, into images/, sounds/, movies/ or fonts/
    // The system's file chooser to pick files of a kind (`extensions` with their dots) to import; `as`: the
    // one picked goes there, under that name (images/spanish/sign, without its extension).
    void chooseFiles(std::string_view label, std::span<const std::string_view> extensions, const std::string& as);
    // A sound file's Play button: it plays the file while every other sound waits, and is Stop while
    // it plays (`small`: > and a square). The sound stops when its button isn't drawn any more.
    void listenButton(DeveloperUi& ui, const SDL_FRect& at, const std::string& path, bool small = false);
    // A form's title row: the As text / As a form button at its right (`rightEdge`), and how wide it is;
    // and, as text, the line as the script has it, in a box to change it: what's typed there, once it
    // isn't empty.
    float asTextButton(DeveloperUi& ui, float rightEdge, float y);
    // Stop here, for a line of the scripts: whether it has it, it on or off (and saved), and the stops
    // moved with their lines after the scripts changed. stopHereButton: the button (lit while it's on)
    // at a form's title row, ending at `rightEdge`: how wide it is.
    [[nodiscard]] bool hasStop(std::string_view path, std::size_t line) const;
    void toggleStop(const std::string& path, std::size_t line);
    void saveStops();
    void followStops();
    float stopHereButton(DeveloperUi& ui, const std::string& path, std::size_t line, float rightEdge, float y);
    // Where the story is (the file and line of its command), when the developer tools were opened over it.
    [[nodiscard]] std::optional<std::pair<std::string, std::size_t>> storyAt() const;
    // The developer tools' language: their own folder (where the one chosen is kept), the folder of the
    // translations ("developer tools translations", next to the player), the translations there (the tools' own
    // part lists them), and one put in use (empty: English). writeToolsTranslation: a file with every text the
    // tools show to translate, or the texts the one in use doesn't have yet added to its file.
    [[nodiscard]] std::string toolsFolder() const;
    [[nodiscard]] std::string toolsLanguagesFolder() const;
    void findToolsLanguages();
    void useToolsLanguage(const std::string& id);
    void writeToolsTranslation();
    std::optional<std::string> drawRawLine(DeveloperUi& ui, const std::string& id, const std::string& line, float x, float w, float& y);
    void makeSample(bool choice); // the story at its first line, or its first choice
    bool paintCanvas(const std::function<void()>& paint); // into the tools' canvas, as big as the game's
    void drawAbout(DeveloperUi& ui, const SDL_FRect& box); // About: the versions, then the licences
    [[nodiscard]] float aboutLeastW(DeveloperUi& ui) const; // (as wide as its widest line: narrower, it scrolls across)
    void tell(const std::string& message, bool failed); // what the last tool did, in a window of its own (with OK)
    // A form's fields, one under another (the label at the left, `labelW` wide, or over its box when
    // that's 0), each with a list to pick from when it has names or values to list: the field changed
    // and its new value, if any.
    struct FormEdit {
        std::string name, value;
        bool typed = false; // into its box, or picked from its list (else a button): one step for undo while it's typed into
    };
    std::optional<FormEdit> drawForm(DeveloperUi& ui, const std::string& id, const std::vector<StoryEditor::Field>& given, float x, float w,
                                     float labelW, float& y);
    // A form's labels' column: `least` (its share of the form's width `w`), or wider when a label (of its
    // fields', or `more`) needs it to show whole on its line, up to layout::labelMostPart of `w`. Every
    // drawForm() of one form, and the rows it draws itself, take the one column.
    static float labelColumn(DeveloperUi& ui, float least, float w, std::initializer_list<std::span<const StoryEditor::Field>> forms,
                             std::initializer_list<std::string_view> more = {});
    void drawPopup(DeveloperUi& ui); // the list a field has open, over everything
    void drawTip(DeveloperUi& ui);   // the whole of a text shown cut, in a little window by the pointer, over that
    struct PopupFrame;                    // (that list this frame: where, the value, what's picked)
    void drawColourWheel(PopupFrame& pop); // ...a colour box's: its code, wheel and bars
    std::optional<std::string> drawFormatBar(DeveloperUi& ui, const std::string& id, float x, float w, float& y);
    [[nodiscard]] std::vector<ScriptError> missingFiles(const Script& script) const; // lines using images, sounds or movies the game lacks

    Engine& engine;       // the player they're the tools of
    DeveloperTools tools; // their state
};

} // namespace vn
