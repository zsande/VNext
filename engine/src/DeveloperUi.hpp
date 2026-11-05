#pragma once

// The developer tools' widgets (DeveloperUi.cpp draws them), for the developer files only.

#include "DeveloperShared.hpp"

#include <limits>

namespace vn {

// Immediate-mode drawing for the developer tools: each widget is drawn and acts on this frame's
// input at once (a button gives true when it's clicked).
class DeveloperUi {
public:
    DeveloperUi(Engine& engine, SDL_Renderer* renderer, FontCache& fonts, DeveloperTools& tools, float scale)
        : k(scale), r(renderer), d(tools), engine_(engine), fonts_(fonts) {}

    const float k;
    SDL_Renderer* const r;
    DeveloperTools& d;

    TextBlock& block(const Font& font, std::string_view s, float wrap = 0.0f); // (laid out, kept by what it is: DeveloperTools::texts)
    Font& font(float size) { return fonts_.get("", size * k); } // (the system's: not the game's own)
    SDL_FPoint text(std::string_view s, float x, float y, float size, SDL_Color colour, float wrap = 0.0f);
    float width(std::string_view s, float size);
    // A button's width: its label and room either side, and at least `least` (the editor's pixels), so
    // the short ones in a row keep one size.
    float buttonWidth(std::string_view label, float size, float least) { return std::max(least * k, width(label, size) + layout::buttonPad * k); }
    SDL_FPoint measure(std::string_view s, float size, float wrap = 0.0f); // (how big text() would draw it)
    // `s` on one line `room` wide: as it is, or shortened() to fit (ending with "...").
    std::string fitted(std::string_view s, float size, float room);
    // The whole of a text shown cut to fit at `area`: shown in a tip while the pointer rests on it
    // (Developer::drawTip).
    void tip(const SDL_FRect& area, std::string_view whole);
    // `s` on one line `room` wide from x, centred on `middle` (up and down): as it is, or fitted(), its
    // whole in a tip. How a text that may not fit is drawn. Gives how wide it's drawn.
    float textFitted(std::string_view s, float x, float middle, float size, SDL_Color colour, float room);
    // A panel's heading ("Scenes", "Images", "Preview"...), the same on every tab: in the middle of its
    // row (layout::headingH high from `y`), on one line `room` wide.
    void heading(std::string_view title, float x, float y, float room = std::numeric_limits<float>::infinity());
    // Buttons that make something new, in named groups, the same on every tab (the Story's New line, the
    // Screens' New part): each group's name at the left, its buttons after it on one row as wide as the
    // room, an even grid when every label fits its cell (else each as wide as its label, narrower when
    // they must be). grouped() lays them out (from the room's top left: each button, by its place over all
    // the groups, and each group's name), groupedButtons() draws them at (x, y) and gives the one clicked
    // (`chosen`: the ones that look pressed).
    struct ButtonGroup {
        std::string_view name;
        std::vector<std::string_view> labels;
    };
    struct Grouped {
        std::vector<SDL_FRect> buttons, names;
        float h = 0.0f;
    };
    [[nodiscard]] Grouped grouped(std::span<const ButtonGroup> groups, float w);
    std::optional<std::size_t> groupedButtons(std::span<const ButtonGroup> groups, const Grouped& at, float x, float y, std::span<const std::size_t> chosen = {},
                                              bool enabled = true);
    // A tab's columns, the same on every tab (Story, Screens, Library, Translate): its lists at the left,
    // the chosen thing's form at the right (with `form`), what's chosen between them; each its share of
    // the room, the lists' and the form's within what reads well (layout::sideLeast...).
    struct Columns {
        SDL_FRect left, middle, right;
    };
    [[nodiscard]] Columns columns(const SDL_FRect& area, bool form = true) const;
    // A form's title ("Line", "Style: shout"...), the same on every tab: at the top of its row
    // (layout::titleH high from `y`), level with a button there (As text), cut to fit in `room` (what
    // that leaves it).
    void formTitle(std::string_view title, float x, float y, SDL_Color colour = kAccent, float room = std::numeric_limits<float>::infinity());
    void textIn(std::string_view s, const SDL_FRect& box, float size, SDL_Color colour);
    // A text with a look (its spans: bold, colours, a reading over it...) in the middle of `box`, as textIn(),
    // `reveal` letters of it showing.
    void richIn(const RichText& text, const SDL_FRect& box, float size, SDL_Color colour, std::size_t reveal = TextBlock::kAll);
    // Drawing stays inside `box` (and the box before it) while it lives; with `clicks`, clicks count only
    // in there too (a list that scrolls). Then both are as they were.
    class Clip {
    public:
        Clip(DeveloperUi& ui, const SDL_FRect& box, bool clicks = false);
        Clip(DeveloperUi& ui, SDL_FRect&& box, bool clicks = false) VN_DELETED("a box that's gone before the clip would still be pointed at");
        ~Clip();
        Clip(const Clip&) VN_DELETED("one box, given back once");
        Clip& operator=(const Clip&) VN_DELETED("one box, given back once");
        Clip(Clip&&) VN_DELETED("one box, given back once");
        Clip& operator=(Clip&&) VN_DELETED("one box, given back once");

    private:
        DeveloperUi& ui_;
        const SDL_FRect* within_; // (where clicks counted before: put back)
    };
    [[nodiscard]] bool over(const SDL_FRect& box) const { return !d.overPopup && pointInRect(d.mouseX, d.mouseY, box); } // (an open list is over it)
    bool click(const SDL_FRect& box); // (taken: nothing under it gets it too)
    float wheelOver(const SDL_FRect& box) { return d.wheel != 0.0f && over(box) ? std::exchange(d.wheel, 0.0f) : 0.0f; }
    bool button(const SDL_FRect& box, std::string_view label, bool chosen = false, bool enabled = true, float size = layout::lineText);
    // A list that scrolls (a red mark: the item has a mistake; `faint` items are dim); gives the item
    // clicked.
    std::optional<std::size_t> list(const SDL_FRect& box, float& scroll, std::size_t count, const std::function<std::string(std::size_t)>& label,
                                    const std::function<bool(std::size_t)>& chosen, const std::function<bool(std::size_t)>& wrong,
                                    const std::function<bool(std::size_t)>& faint = {});
    // A list whose items can also be renamed (a double click), deleted (the button at the end of the
    // chosen or pointed-at one, clicked twice) and moved (dragged), each as `items` allows.
    struct ListItems {
        std::size_t count = 0;
        std::function<std::string(std::size_t)> label;
        std::function<std::string(std::size_t)> name; // what renaming it starts from (else its label)
        std::function<bool(std::size_t)> chosen, wrong, faint, renames, deletes, moves;
        std::function<bool(std::size_t)> unreached; // a grey mark at its left (the game never ran it)
        std::function<bool(std::size_t, std::size_t)> drops;          // item can go to (before) this place
        // Instead of `drops`: how far in item would go before this place, by how far right the mouse
        // has moved since the drag began (none: it can't go there); each step in is `indentStep`.
        std::function<std::optional<int>(std::size_t, std::size_t, float)> place;
        float indentStep = 0.0f;
        std::function<float(std::size_t)> indent;                     // how far in it's drawn
        std::function<void(std::size_t, const SDL_FRect&)> paint;     // draws it (else its label)
        float itemH = layout::listItemH;
    };
    struct ListEvent {
        enum class Kind : std::uint8_t { None, Chosen, Renamed, Deleted, Moved };
        Kind kind = Kind::None;
        std::size_t item = 0, to = 0; // Moved: before item `to` (count: after the last)
        std::string name;             // Renamed: as typed so far
        int depth = 0;                // Moved, with `place`: how far in
    };
    ListEvent editList(const std::string& id, const SDL_FRect& box, float& scroll, const ListItems& items);
    // Buttons side by side from (x, y), as wide as their labels (narrower, if they don't fit in `w`);
    // gives the one clicked.
    std::optional<std::size_t> buttons(float x, float y, float w, float h, const std::vector<std::string_view>& labels, const std::vector<bool>& enabled,
                                       float size = 14);
    // Whether two buttons belong side by side (yes and no, at least and at most, left and right...): a
    // row never ends between them.
    static bool pairedButtons(std::string_view a, std::string_view b);
    // Buttons in rows `w` wide: each as wide as its label, wrapped when they don't fit. The ones that
    // belong together (pairs, and `with`: each that stays with the one before it) never split across rows.
    std::vector<SDL_FRect> flow(const std::vector<std::string_view>& labels, float w, float h, float gap, float size, const std::vector<bool>& with = {},
                                float pad = 16.0f);
    // A field's choices: each word and what its button (or its list) says, Yes before No.
    static std::vector<std::pair<std::string_view, std::string>> choiceLabels(const StoryEditor::Field& f);
    // The buttons a field's choices make in `box`: what they say and where, in one row (all alike when
    // they fit so, else as wide as their labels, sharing out the room left). No places: they don't fit it.
    struct ChoiceButtons {
        std::vector<std::string_view> words;
        std::vector<std::string> shown;
        std::vector<SDL_FRect> places;
    };
    ChoiceButtons choiceButtons(const SDL_FRect& box, const StoryEditor::Field& f);
    // A field's choices in `box`, always one row: buttons when they fit, else one box saying what's
    // chosen, whose list opens under it on a click. Gives the field's new value when one is clicked or
    // picked. With `multi`, a click turns one on or off ("none" goes on its own).
    std::optional<std::string> choices(const std::string& id, const SDL_FRect& box, const StoryEditor::Field& f);
    // A tab in the bar at the top (red: what it shows has a mistake).
    bool tab(const SDL_FRect& box, std::string_view label, bool chosen, bool wrong, float size = layout::tabText);
    // Text centred on `middle` (up and down).
    void textAround(std::string_view s, float x, float middle, float size, SDL_Color colour, float wrap = 0.0f);

    // A box to type into, showing `value` (while it's typed into, what's typed; when it's empty, the
    // `hint`), with what's in braces ({bold}) dimmer. A click puts the caret there; dragging, Shift (with
    // a click or the arrows, Home and End) and a double click (a word) choose letters, Cmd/Ctrl+A all
    // of them, and typing, pasting, cutting and deleting replace what's chosen. Gives the new text when
    // it changes.
    std::optional<std::string> field(const std::string& id, const SDL_FRect& box, const std::string& value, float size = 16, std::string_view hint = {},
                                     bool spelling = false);
    // What a box's list picked for it last frame, if anything; and a box's list, opened under it.
    std::optional<std::string> takePicked(const std::string& id);
    void openPopup(const std::string& id, const StoryEditor::Field& f, const SDL_FRect& box);
    // A button that opens a list (as a field's, from `f`), and gives what's picked from it.
    std::optional<std::string> listButton(const std::string& id, const SDL_FRect& box, std::string_view label, const StoryEditor::Field& given);
    // A colour as a small square at `x`, centred on `middle` (dim: half seen, for a default); gives
    // the x after it. Every colour shows this way: in a box, a list, a popup's entry.
    float swatch(float x, float middle, SDL_Color colour, bool dim = false, std::optional<SDL_Color> bottom = std::nullopt, float size = layout::swatch) const;
    // A colour's box: the colour and its name or code, not typed into. A click opens its wheel under it,
    // with the colour's code to type and the game's colours to pick. Gives what's picked there.
    std::optional<std::string> colourField(const std::string& id, const SDL_FRect& box, const StoryEditor::Field& f, float size);
    // A box to type into (as field()) with a list to pick from under it, when `f` has names or values
    // to list: a click on the box or its arrow opens it. Gives the new text, typed or picked.
    std::optional<std::string> pickField(const std::string& id, const SDL_FRect& box, const StoryEditor::Field& given, float size = 16);
    // The form in `box` (the Story, Screens and Library tabs' right side) scrolled by the wheel over
    // it, `step` a turn, or by its scrollbar, no further than its ends as drawn last frame; then, once
    // it's drawn down to `y`, its height kept for the next.
    void scrollForm(const SDL_FRect& box, float step);
    void formDrawn(const SDL_FRect& box, float y);
    // A scrollbar down `track` for what's `total` high and shows `view` of it, scrolled `scroll` down (in
    // any one measure: pixels, lines); `of`: the place that scroll is kept (it tells one from another).
    // None while it all shows. Its thumb dragged, or the track clicked (the thumb comes there, to drag
    // on), scrolls it.
    void scrollbar(const void* of, const SDL_FRect& track, float view, float total, float& scroll);
    // Where a scrollbar runs: inside a box at its right (its rows leave it layout::scrollbarRoom), or
    // beside one, in the margin at its right (a form's, which has no box of its own).
    [[nodiscard]] SDL_FRect barInside(const SDL_FRect& box) const;
    [[nodiscard]] SDL_FRect barBeside(const SDL_FRect& box) const;
    // What the spell checker doesn't check: the characters' names (and their words).
    std::vector<std::string> names() const;
    // A small triangle around (x, y): a dropdown's, pointing down (open: up), or the same turned to point
    // right (a play button).
    enum class Pointing : std::uint8_t { Down, Up, Right };
    void triangle(float x, float y, float size, Pointing to, SDL_Color colour) const;

private:
    void clip(const SDL_FRect& box); // (Clip's, in and out)
    void unclip();

    Engine& engine_;
    FontCache& fonts_;
    std::vector<SDL_Rect> clips_;
    const SDL_FRect* within_ = nullptr; // clicks count only in here (a list that scrolls: see Clip)
};

// A preview's heading, the same on every tab: in `row` (a heading's, layout::headingH high), its buttons
// from the right, each left of the last (true: clicked), then its title in the room they leave.
class PreviewHead {
public:
    PreviewHead(DeveloperUi& ui, const SDL_FRect& row) : ui_(ui), row_(row), right_(row.x + row.w) {}
    // The row over a preview shown at `shown`, a gap above it.
    static SDL_FRect over(const SDL_FRect& shown, float k) { return {shown.x, shown.y - layout::previewHeadH * k, shown.w, layout::headingH * k}; }
    bool button(std::string_view label, bool chosen, bool enabled = true);
    void title(std::string_view text);

private:
    DeveloperUi& ui_;
    SDL_FRect row_;
    float right_; // (where the next button ends)
};

} // namespace vn
