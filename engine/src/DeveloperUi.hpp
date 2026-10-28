#pragma once

// The developer tools' widgets (DeveloperUi.cpp draws them), for the developer files only.

#include "DeveloperShared.hpp"

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

    TextBlock& block();
    Font& font(float size) { return fonts_.get("", size * k); } // (the system's: not the game's own)
    SDL_FPoint text(std::string_view s, float x, float y, float size, SDL_Color color, float wrap = 0.0f);
    float width(std::string_view s, float size);
    // A button's width: its label and room either side, and at least `least` (the editor's pixels), so
    // the short ones in a row keep one size.
    float buttonWidth(std::string_view label, float size, float least) { return std::max(least * k, width(label, size) + layout::buttonPad * k); }
    SDL_FPoint measure(std::string_view s, float size, float wrap = 0.0f); // (how big text() would draw it)
    void textIn(std::string_view s, const SDL_FRect& box, float size, SDL_Color color);
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
    // Buttons as wide as their labels, in rows `w` wide: their places, from the top left of the first.
    // Buttons that belong side by side (yes and no, at least and at most, left and right...): a row
    // never ends between them.
    static bool pairedButtons(std::string_view a, std::string_view b);
    // Buttons in rows `w` wide: each as wide as its label, wrapped when they don't fit. The ones that
    // belong together (pairs, and `with`: each that stays with the one before it) never split across rows.
    std::vector<SDL_FRect> flow(const std::vector<std::string_view>& labels, float w, float h, float gap, float size, const std::vector<bool>& with = {},
                                float pad = 16.0f);
    // The buttons a field's choices make in `box`: what they say (their labels, or the words) and
    // where (all alike in one row when they fit, else as wide as their labels, in rows stretched to
    // the box's width, under it).
    struct ChoiceButtons {
        std::vector<std::string_view> words;
        std::vector<std::string> shown;
        std::vector<SDL_FRect> places;
    };
    ChoiceButtons choiceButtons(const SDL_FRect& box, const StoryEditor::Field& f);
    // How tall a field's choices are as buttons in a box `w` wide and `h` tall (more rows under it when they don't fit).
    float choicesHeight(const StoryEditor::Field& f, float w, float h);
    // A field's choices as buttons in `box` (in more rows, under it, when they don't fit: `y` moves
    // down to the last): gives the field's new value when one is clicked. With `multi`, a click turns
    // one on or off ("none" goes on its own).
    std::optional<std::string> choices(const SDL_FRect& box, const StoryEditor::Field& f, float& y);
    // A tab in the bar at the top (red: what it shows has a mistake).
    bool tab(const SDL_FRect& box, std::string_view label, bool chosen, bool wrong);
    // Text centred on `middle` (up and down).
    void textAround(std::string_view s, float x, float middle, float size, SDL_Color color, float wrap = 0.0f);

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
    float swatch(float x, float middle, SDL_Color color, bool dim = false, std::optional<SDL_Color> bottom = std::nullopt) const;
    // A colour's box: the colour and its name or code, not typed into. A click opens its wheel under it,
    // with the colour's code to type and the game's colours to pick. Gives what's picked there.
    std::optional<std::string> colorField(const std::string& id, const SDL_FRect& box, const StoryEditor::Field& f, float size);
    // A box to type into (as field()) with a list to pick from under it, when `f` has names or values
    // to list: a click on the box or its arrow opens it. Gives the new text, typed or picked.
    std::optional<std::string> pickField(const std::string& id, const SDL_FRect& box, const StoryEditor::Field& given, float size = 16);
    // The form in `box` (the Story, Screens and Library tabs' right side) scrolled by the wheel over
    // it, `step` a turn, no further than its ends as drawn last frame; then, once it's drawn down to
    // `y`, its height kept for the next.
    void scrollForm(const SDL_FRect& box, float step);
    void formDrawn(const SDL_FRect& box, float y);
    // What the spell checker doesn't check: the characters' names (and their words).
    std::vector<std::string> names() const;
    // A small triangle around (x, y): a dropdown's, pointing down (open: up), or the same turned to point
    // right (a play button).
    enum class Pointing : std::uint8_t { Down, Up, Right };
    void triangle(float x, float y, float size, Pointing to, SDL_Color color) const;

private:
    void clip(const SDL_FRect& box); // (Clip's, in and out)
    void unclip();

    Engine& engine_;
    FontCache& fonts_;
    std::vector<SDL_Rect> clips_;
    const SDL_FRect* within_ = nullptr; // clicks count only in here (a list that scrolls: see Clip)
};

} // namespace vn
