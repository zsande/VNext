#pragma once

// The script's screens as they're shown (ScreenView): laid out, drawn, and what the mouse, keys and a controller do
// on them.

#include "Script.hpp"
#include "Text.hpp"
#include "Shapes.hpp"
#include "Assets.hpp"
#include "Saves.hpp"
#include "Movie.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vn {

class Engine;

// A screen from the script, live: draws its elements and handles input for them.
class ScreenView {
public:
    // `tools`: shown in the developer tools (its animations go on while the game stands still under them).
    ScreenView(const ScreenDef& def, Engine& engine, bool tools = false);
    ScreenView(const ScreenView&) VN_DELETED("its lookups point at it");
    ScreenView& operator=(const ScreenView&) VN_DELETED("its lookups point at it");
    ScreenView(ScreenView&&) VN_DELETED("its lookups point at it");
    ScreenView& operator=(ScreenView&&) VN_DELETED("its lookups point at it");
    ~ScreenView() = default;

    [[nodiscard]] const std::string& name() const { return source_->name; }
    [[nodiscard]] bool modal() const { return source_->storyWaits; }
    [[nodiscard]] const ScreenDef& def() const { return *source_; } // (the script's: a list's parts once)
    void reset(); // forget focus, scrolling and dragging (when the screen opens again)
    void opened(); // its animation_on_open= starts (reset() does this too)
    void leave();  // it's closing: its animation_on_close= plays (it's drawn, but takes no input)
    [[nodiscard]] bool leaving() const; // ...until that's over
    [[nodiscard]] bool hasLeft() const { return leftSince_.has_value(); } // (since leave(), until it opens again)
    // The animations it plays this time it's open (open ... with NAME and close with NAME), instead of its
    // own animation_on_open= and animation_on_close=: until it's opened again. Empty: its own; null: none.
    using Played = std::optional<std::shared_ptr<const Animation>>;
    // The animation `name` in `script`, as one opening plays it (none said: its own; one it doesn't
    // have, or "none": none).
    [[nodiscard]] static Played animationIn(const Script* script, const std::optional<std::string>& name);
    void playWith(Played opening, Played closing);
    [[nodiscard]] const Animation* showAnimation() const { return opening_ ? opening_->get() : def_->animationOnOpen.get(); }
    [[nodiscard]] const Animation* hideAnimation() const { return closing_ ? closing_->get() : def_->animationOnClose.get(); }
    [[nodiscard]] bool startsCovered() const; // its show animation begins with everything covered by a colour (fade=)
    [[nodiscard]] bool endsCovered() const;   // its hide animation ends with everything covered (fade=)
    void draw(SDL_Renderer* r, float alpha = 1.0f);
    // After a frame: its movies that weren't drawn in it stop (the screen was closed or hidden).
    void endFrame(std::uint64_t frame);
    // Returns true when the event was used (screens with story=waits use every input event).
    bool handleEvent(const SDL_Event& e);
    // A click or the wheel on one of its parts that shows something (the phone's panel, a text, an image; not
    // an empty container's box): it isn't the story's (an opened screen that lets the story go on keeps it).
    [[nodiscard]] bool catches(const SDL_Event& e);
    // The event it just took was a tap on a scrolling part (pressed and let go without dragging) on nothing
    // to click: where a click elsewhere would be the story's, so is this one (the phone in a chat scene).
    bool takeTapped() { return std::exchange(tapped_, false); }
    // The editor's view of the last draw, by the script's part (its index in def(); one in a list: as its first
    // item has it): whether it was drawn (its when= let it), where (its box, or for one sized by its text, the
    // text's), its box as laid out, the box it's in, and a part's index from the part itself.
    [[nodiscard]] bool drawnPart(std::size_t part) const;
    [[nodiscard]] SDL_FRect partBounds(std::size_t part);
    [[nodiscard]] SDL_FRect partRect(std::size_t part) const;
    [[nodiscard]] SDL_FRect partBox(std::size_t part) const;
    [[nodiscard]] std::size_t partIndex(const Element& el) const { return static_cast<std::size_t>(&el - source_->elements.data()); }
    // Read aloud: the words of what the keys are on (with `mouse`, else what the pointer is on): a button's, a slider's
    // or a box's with the texts beside it in the part it's in (its label at its left, or over it there; then the others
    // on its row, a value), an option, or a list's item (its texts); empty when they're on nothing. And the words as the
    // screen opens: its texts that aren't in a list or in a part that scrolls (a phone's: the ones in the app it shows),
    // its title or its question.
    [[nodiscard]] std::string pointedText(bool mouse);
    [[nodiscard]] std::string openingText();
    // A button's click_animation= is over: what it does (and its question first, and its list item's values
    // when it's in one), to run. None when the button (or its screen) went away meanwhile.
    struct Pressed {
        Commands actions;
        std::string confirm;
        Variables item;
    };
    [[nodiscard]] std::optional<Pressed> takePressed();
    // What a control's move, press, scroll and put back actions do on the screens the keys work on (Engine::keyViews):
    // the move actions point at parts, the nearest that way on any of those screens.
    [[nodiscard]] bool takesClicks() const { return !targets_.empty(); }
    [[nodiscard]] bool takesKeys();                          // it has a part the move actions can point at, or something to scroll
    // Where the keys go on from: the part pointed at (by the keys or the pointer), else the one pointed at or clicked last
    // (the pointer moved onto nothing since).
    [[nodiscard]] std::optional<SDL_FRect> pointedRect() const;
    [[nodiscard]] bool keysPoint() const;                     // the keys point at one of its parts (press presses it)
    [[nodiscard]] SDL_FPoint pointer() const { return mouse_; } // where the pointer is, as this screen saw it last
    // The wheel turned (`up` turns up, `across` to the right) at a place on it: what scrolls there scrolls (false: nothing).
    bool wheelAt(SDL_FPoint at, float up, float across);
    // Opened by the keys (a question, the on-screen keyboard): once it's drawn, the ring on the answer that keeps things as
    // they are (a button that does no), or on its first letter.
    enum class Ring : std::uint8_t { None, Keep, FirstLetter };
    void ringWhenDrawn(Ring at) { ringAt_ = at; }
    [[nodiscard]] bool hasKey(const SDL_Event& e); // one of its parts shown has this key (keys=)
    void keyboardShut() { keyboardShut_ = true; }  // the on-screen keyboard was put away: not by itself again until a box is pressed
    // A box (an input part) typed into by the on-screen keyboard (Engine::keyboardKey): letters added, the last one taken
    // away, Enter (what the box does: its action=), and what it holds.
    void typeIntoBox(std::size_t index, std::string_view typed) { typeInto(index, typed); }
    void eraseInBox(std::size_t index) { eraseLast(index); }
    void moveCaret(std::size_t index, int by); // (the on-screen keyboard's left and right: a letter back or on)
    void enterBox(std::size_t index);
    [[nodiscard]] std::string boxText(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> boxTypedInto() { return typedInto(); } // (the box typing goes to, if it has one)
    // How far the nearest part that way from the part at `from` is (the distance on, with what's off to the side
    // counting double; one in line with it first), for comparing screens; none when there's none.
    [[nodiscard]] std::optional<float> toward(const SDL_FRect& from, int dx, int dy) const;
    void pointToward(const SDL_FRect& from, int dx, int dy); // ...pointed at by the keys (scrolled into view)
    bool pointFirst();                   // nothing pointed at yet: the first part (false: it has none)
    bool adjustPointed(int dx, int dy);  // a slider pointed at: left and right change it, up and down a vertical one (false: no slider is)
    void letGo();                        // the keys point at nothing here any more (another screen's part took them)
    void keysRest() { keyFocus_ = false; } // ...the part they pressed stays pointed at, for them to move on from, but press goes on (the story's screens)
    void pressPointed();                 // the part the keys point at, clicked
    bool scrollBy(std::string_view how); // up, down, to the top or to the end: its history, else the container that scrolls (false: nothing does)
    void putBack();                      // what's being dragged, back where it was
    // What the buttons that came to be chosen because of their chosen= set (set VARIABLE to VALUE,
    // setting NAME VALUE), to put in effect as if they were clicked: a choice that shows is the one that
    // holds. Their other actions (opening a screen, music, quitting...) run only when they're clicked.
    // Gathered as the screen is drawn: once a button becomes chosen, and again each time the screen opens.
    [[nodiscard]] Commands takeChosen() { return std::exchange(chosen_, {}); }
    // The drag= of the button the player holds, if any ({dragging}).
    [[nodiscard]] std::string held() const { return held_ ? def_->elements[*held_].option("drag") : std::string{}; }

    // The mouse went down on one of its buttons: it's clicked if it comes up there (the engine gives
    // this screen the mouse's coming up).
    [[nodiscard]] bool pressing() const { return pressing_.has_value(); }

private:
    // Part `index` as it is now (in a screen with lists: their parts once for each item): whether it was drawn,
    // where (as partBounds() above), its box as laid out, the box it's in (the part it's in, or the screen: its x
    // and y are places in it, its w and h shares of it), and a part's index from the part itself.
    [[nodiscard]] bool drawn(std::size_t index) const { return index < drawn_.size() && drawn_[index]; }
    [[nodiscard]] SDL_FRect bounds(std::size_t index);
    [[nodiscard]] SDL_FRect& rectOf(std::size_t index) { return state_[index].rect; }
    [[nodiscard]] const SDL_FRect& rectOf(std::size_t index) const { return state_[index].rect; }
    [[nodiscard]] SDL_FRect boxOf(std::size_t index) const;
    [[nodiscard]] std::size_t indexOf(const Element& el) const { return static_cast<std::size_t>(&el - def_->elements.data()); }
    [[nodiscard]] std::size_t asDrawn(std::size_t part) const; // the script's part `part` as it is now (in a list: its first item's; SIZE_MAX: none)
    // The style part `index` is drawn with: its own, or the one its animation makes of it for the moment.
    [[nodiscard]] const Style& styleOf(std::size_t index) const { return state_[index].styled ? *state_[index].styled : def_->elements[index].style; }

    // Lists (list=): a container's parts once for each item of one of the phone's lists, in a box of their own
    // each (an item's box: a container of the list's, as wide as it shares out, as high as item_height= or
    // its parts), one after another. own_ is the screen so (def_ points at it; without lists, at source_).
    // For each part of it: the script's part it is (an item's box: none), its item's values ({chat.name}...:
    // its list's item's, and those of the lists it's in), and what it's called across changes (its state,
    // focus and the like go with that). expand(): again for what the lists hold now (before each draw).
    const ScreenDef* source_;
    ScreenDef own_;
    bool hasLists_ = false;
    std::vector<std::size_t> sourceOf_, firstOf_;
    std::vector<std::shared_ptr<const Variables>> bound_;
    using Looks = std::map<std::string, std::vector<TextSpan>, std::less<>>; // (an item's words' looks: {line.text}'s colours, bold...)
    std::vector<std::shared_ptr<const Looks>> boundLooks_;
    std::vector<std::string> keyOf_;
    std::map<std::string, std::string, std::less<>> listContext_; // each list's last chat...: new items there play item_animation=
    void expand();
    struct ListItem {
        std::string key;
        Variables values;
        Looks looks = {}; // (its words that have a look of their own: a history line's, a choice's)
    };
    // A part's words as they look: its own text tags, or (a part showing just {line.text}, {option.text}) its item's
    // words with their own look.
    [[nodiscard]] RichText wordsOf(std::size_t index, std::string_view text);
    // A list's items now; `context`: what they're of (a chat's name...).
    [[nodiscard]] std::vector<ListItem> listItems(const Element& list, std::string& context);
    void seen(const Element& list);                       // what it shows has been seen (messages read, missed calls seen...)
    [[nodiscard]] bool isItemBox(std::size_t index) const { return index < sourceOf_.size() && sourceOf_[index] == SIZE_MAX; }
    [[nodiscard]] Variables itemOf(std::size_t index) const; // its item's values (none: not in a list)
    // While one lives, lookup_ sees part `index`'s item's values first.
    std::size_t current_ = SIZE_MAX;
    struct Bound {
        ScreenView& view;
        std::size_t was;
        Bound(ScreenView& v, std::size_t index) : view(v), was(std::exchange(v.current_, index)) {}
        ~Bound() { view.current_ = was; }
        Bound(const Bound&) = delete;
        Bound& operator=(const Bound&) = delete;
    };
    [[nodiscard]] bool stacks(std::size_t index, bool* down = nullptr) const; // its parts go one after another (stack=; an item's box: its list's)
    // A text's width_at_most=, in pixels (0: none): a share of the box it's in (a box whose width comes from its
    // parts, fit=parts without a width: of the one that one's in).
    [[nodiscard]] float widest(std::size_t index) const;
    // What part `index`'s width= (`across`) or height= is a share of: the box it's in, or for one in a box that's as
    // big as its parts that way (fit=parts without a size, an item's box without item_height=) the box that one's in.
    [[nodiscard]] float shareOf(std::size_t index, bool across) const;
    // ...and that width (height) in pixels: the share of that, or its pixels (a width at most that box's).
    [[nodiscard]] float sized(std::size_t index, bool across) const;
    // A size or gap in a part's setting (item_width=, spacing_across=...): a share of `whole`, or pixels (370px).
    [[nodiscard]] static std::optional<float> amount(const Element& el, std::string_view key, float whole);
    // A part filling the room the others leave in a box whose parts go one after another: a container without a
    // size that way (nor fit=parts), not a list's item.
    [[nodiscard]] bool fillsRest(std::size_t index, bool down) const;

    enum class Look : std::uint8_t { Idle, Hover, Selected, Disabled };
    // What can be clicked: a part, or one of its items (the flowchart's points: which one).
    struct Spot {
        std::size_t element = 0;
        int item = 0;
        int option = -1;
        bool operator==(const Spot&) const = default;
    };
    struct Target {
        Spot spot;
        SDL_FRect rect;
        bool reachable;       // the move actions go to it
        bool hidden = false;  // scrolled out of its list's box: the move actions go to it (it scrolls into view), the pointer can't
        bool presses = true;  // pressing it does something (an ending in a list doesn't: the keys go to it to scroll the list, the pointer doesn't)
    };
    // Lists and grids (choices, saves, the gallery, endings, the flowchart...): columns= item_width= item_height=
    // spacing= (or spacing_across= and spacing_down=), with these defaults (an itemW of 0: the part's width shared out).
    struct Grid {
        int columns = 1;
        float itemW = 0, itemH = 0, gapX = 0, gapY = 0;
        [[nodiscard]] SDL_FRect cell(const SDL_FRect& area, std::size_t i, float scroll = 0.0f) const;
        [[nodiscard]] float height(std::size_t items) const; // of all the rows
    };
    // (item_width=, spacing=... are shares of the box the part is in, or pixels; without them, these pixels. A list with
    // item_width= and no columns=: as many columns as come nearest, its items as wide as that leaves, their shape kept)
    [[nodiscard]] Grid grid(const Element& el, Grid defaults) const;
    [[nodiscard]] float share(const Element& el, std::string_view key, bool across, float fallback) const; // (item_width=, spacing_across=...)
    void layout(); // where each part is now (its place and size in its box)
    struct ElementState {
        SDL_FRect rect{}; // where the part is on the screen, in the game's pixels (laid out every frame; w/h 0: automatic)
        std::optional<Style> styled;  // while its animation changes its look (zoomed, a colour): the style it's drawn with
        TextBlock text, value;
        std::vector<TextBlock> items, extra, buttons;
        std::vector<std::string> cache;
        float scroll = 0.0f;
        std::unique_ptr<Movie> movie; // an image part's movie: playing
        std::string movieName;
        float movieTime = 0.0f;       // the engine's time when it last played
        std::uint64_t movieFrame = 0; // the frame it was last drawn in
        bool centred = false;        // the flowchart: whether it has been moved to the current place yet
        std::vector<std::string> ids; // ...the place each item goes to
        std::optional<float> since;     // a part with an animation: when it showed (its animation starts then)
        std::optional<float> clicked;   // a button with click_animation=: when it was clicked (it plays from then)
        float pointedPlayed = 0.0f;             // a part with pointed_animation=: how far into it (on while it's pointed at, back after)
        std::optional<float> pointedPlayedAt;   // ...and when that was
        float reach = 0.0f, reachX = 0.0f; // a container that scrolls: how far down from its top, and across from its left, its parts reach
        float scrollX = 0.0f;              // ...and how far it's scrolled across
        float glideX = 0.0f, glideY = 0.0f, glideAt = 0.0f; // ...gliding on after a drag: how fast (the game's pixels a second), and when it last moved
        float ownW = -1.0f, ownH = -1.0f;  // a container with fit=parts: as wide and as high as its parts, measured where they are (-1: not yet; 0: none show)
        float rest = -1.0f;                // one whose parts go one after another: the room each part filling it has (-1: not measured yet)
        std::optional<SDL_FRect> hit;   // a button without a size: where it's clicked (its usual and its current image's size together)
        bool ownWords = false;          // ...one without an image: as big as its words (drawn as they are, not wrapped)
        bool chosen = false;            // a button with chosen=: chosen when it was last drawn (see takeChosen)
        std::optional<float> messageAt; // a part with message_animation=: when a message came (it plays from then)
        std::optional<float> itemAt;    // an item's box, new in its list: when it came (its list's item_animation= plays from then)
        float keptScroll = -1.0f, keptMost = 0.0f; // a list with position=bottom: its scroll and how far it went last time (at the end: it stays there)
    };
    struct Shift { // what the part a part is in moves it by (its animation's), how faded, how covered with a colour and how wiped
        float x = 0.0f, y = 0.0f, opacity = 1.0f, fade = 0.0f;
        SDL_Color fadeColour{0, 0, 0, 255};
        float wipe = 1.0f; // (how much of it shows: over `wipeBox`, as `wipeShape` has it)
        SDL_FRect wipeBox{};
        std::string wipeShape;
    };
    // paint(), wiped as `by` says (a part in a wiped one: wiped with it, over its box).
    template <class Paint> void wiped(SDL_Renderer* r, const Shift& by, const Paint& paint);
    Wiper screenWiper_, partWiper_; // (the whole screen's wipe, and a part's)
    [[nodiscard]] bool within(std::size_t element, std::size_t container) const; // in it, or in a part in it
    // Where the containers a part is in cut it off (their edges=cut or fade, or scroll=yes), moved by their
    // `shifts` when given; none: nowhere.
    [[nodiscard]] std::optional<SDL_FRect> cutBy(std::size_t index, const std::vector<Shift>* shifts = nullptr) const;
    bool startAside(SDL_Renderer* r, float& scale); // aside_ the window's size (scale: its pixels per one of the game's)
    TexturePtr aside_; // what a container with edges=fade holds, drawn here first
    // Dragging scrolls a container with scroll=yes and the flowchart (it glides on when let go moving, as
    // glide=, glide_speed= and glide_distance= say; Element::dragDirection() says which way). scrollableAt(): the innermost one under a point. glideStep(): a glide's next moment;
    // settle(): a list's scroll glided on and kept from 0 to `most`, stopping at an end.
    [[nodiscard]] std::optional<std::size_t> scrollableAt(float x, float y);
    void glideStep(std::size_t index);
    void settle(std::size_t index, float most);
    // A part's scrollbars (scrollbar=yes: a container, the flowchart) at its right edge and (`across`) along its
    // bottom, and the thumb on each; the bars, drawn after what's in it.
    [[nodiscard]] SDL_FRect barOf(std::size_t index, bool across) const;
    [[nodiscard]] SDL_FRect thumbOf(std::size_t index, bool across) const;
    [[nodiscard]] float barRoom(std::size_t index) const; // (how much of its right edge its scrollbar takes: a list's items stay clear of it)
    void drawScrollbar(SDL_Renderer* r, std::size_t index, float alpha);
    void reveal(const Target& t); // (in a container that scrolls: scrolled into view)
    // Dragging scrolls a container: its parts (once the mouse has moved a few pixels: then the button
    // under it isn't clicked), or its scrollbar's thumb. Which, where the mouse went down, and how far
    // it was scrolled then.
    std::optional<std::size_t> scrollDrag_;
    SDL_FPoint scrollFrom_{};
    float scrollWas_ = 0.0f, scrollWasX_ = 0.0f;
    bool scrollDragMoved_ = false, scrollBarDrag_ = false, scrollBarAcross_ = false;
    std::vector<std::pair<float, SDL_FPoint>> dragTrail_; // when and where the mouse was lately while dragging to scroll (how fast it was let go)
    std::optional<Target> tapping_; // a list's item pressed: it acts when the mouse comes up on it, unless that was a drag
    bool tapped_ = false;           // a scrolling part pressed and let go without dragging, on nothing to click (see takeTapped)

    void drawElement(SDL_Renderer* r, std::size_t index, float alpha);
    // A part playing animations (its animation=, its pointed_animation= while it's pointed at and back
    // after, a button's click_animation= once clicked): drawn as they have it now, together, then put
    // back as it's written. Gives how it looks now (the parts in it move with it). `by`: what the part
    // it's in (or its screen) moves it by now (its places are where it's written, and it moves with that)
    // and wipes it by; and then how the parts in it are wiped (its own wipe, when it shows less).
    Animation::Look drawAnimated(SDL_Renderer* r, std::size_t index, float alpha, Shift& by, const SDL_FRect& box);
    [[nodiscard]] static Animation::Look ownLook(const Element& el); // (a visual's own zoom, opacity, rotation and tint)
    [[nodiscard]] static bool hasOwnLook(const Element& el);
    // Whether a part is pointed at: the one with the focus (the pointer's, or the keys' and a controller's),
    // or any under the pointer (a box with buttons in it too). pointedOnward(): its pointed animation goes on
    // meanwhile (round and round, if it plays again and again), and back to its start after.
    [[nodiscard]] bool pointedAt(std::size_t index) const;
    void pointedOnward(std::size_t index);
    std::uint64_t seenMotion_ = 0; // the engine's count of pointer moves when the last one here came (older: it's gone elsewhere)
    std::optional<std::size_t> playing_; // a button whose click animation plays: what it does is done when it's over
    float turn_ = 0.0f;                     // ...an image's turn and tint meanwhile
    SDL_Color tint_{255, 255, 255, 255};
    // `flip` mirrors it (on top of the element's flip=); `box`: where (else the element's own box).
    void image(SDL_Renderer* r, const Image& pic, const Element& el, std::string_view fit, float ax, float ay, float alpha,
                 bool flip = false, const SDL_FRect* box = nullptr);
    void drawFlowchart(SDL_Renderer* r, std::size_t index, float alpha);
    [[nodiscard]] bool shows(std::size_t index); // its when= lets it, and on the phone its app (app=) is the one open
    [[nodiscard]] int newOn(const Element& app); // what's new in an app (a container with app=, the script's): in the lists in it, and its count=
    [[nodiscard]] const CharacterDef* characterNamed(std::string_view name) const; // the character the phone calls that
    void drawInput(SDL_Renderer* r, std::size_t index, float alpha);
    void drawSlider(SDL_Renderer* r, std::size_t index, float alpha); // (value=: the value, filling it)
    // An image part's movie (one named so: it plays, as the image), drawn as its fit= has it; false: it isn't one.
    bool drawMovie(SDL_Renderer* r, std::size_t index, const std::string& name, float alpha);
    [[nodiscard]] float number(const Element& el, std::string_view key, float fallback);
    bool handleInput(const SDL_Event& e);
    // The input box that takes typing: the one the keys are on, else the one pressed last (typing_), else the first shown.
    [[nodiscard]] std::optional<std::size_t> typedInto();
    void typeInto(std::size_t index, std::string_view typed); // letters added to what it holds at its caret (up to its most_letters=)
    void eraseLast(std::size_t index);                        // ...the letter before its caret taken away
    // The caret in the box typed into: so many letters from the end of what it holds (0: at its end, where typing goes on),
    // as the on-screen keyboard's left and right move it; in another box, at its end. caretAt: in letters from its start.
    std::optional<std::size_t> caretBox_;
    std::size_t caretBack_ = 0;
    [[nodiscard]] std::size_t caretAt(std::size_t index, std::string_view value) const;
    bool keyboardShut_ = false; // the on-screen keyboard put away: it doesn't come up by itself again until a box is pressed, or the screen opens again
    // What a button types (an on-screen keyboard's key: its one action, type LETTER), while Shift is on a capital or the
    // sign over it; else nothing. Without words of its own, it shows that.
    [[nodiscard]] std::string keyLetter(const Element& el) const;
    void adjustSlider(std::size_t element, int delta);
    [[nodiscard]] SDL_FRect sliderBox(std::size_t element) const;
    // A part's box in a look (`chosen`: a chosen one pointed at; `holds`: one other parts are drawn on, a container's or a
    // colour's, whose chosen look in high contrast is its edge, not a fill).
    void box(SDL_Renderer* r, const SDL_FRect& rect, const Style& s, Look look, float alpha, bool chosen = false, bool holds = false);
    void text(SDL_Renderer* r, TextBlock& block, std::string_view str, const SDL_FRect& rect, const Style& s,
              SDL_Color colour, float alpha, std::size_t reveal = TextBlock::kAll, std::vector<TextSpan> spans = {});
    [[nodiscard]] static const Style::State* stateOf(const Style& s, Look look); // its look then (nullptr: its own)
    [[nodiscard]] std::string lookImage(const Style& s, Look look, bool chosen); // the image a style shows in a look (empty: none)
    // A button's look now: it can't be clicked, it's pressed (until the mouse comes up) or playing its
    // click animation, it's pointed at, it's chosen (its chosen= is true, or it's being dragged), or
    // its usual self. `hit` is where a press on it counts.
    struct ButtonLook {
        Look look;
        bool on, chosen;
    };
    [[nodiscard]] ButtonLook buttonLook(std::size_t index, const SDL_FRect& hit);
    void sizeButton(std::size_t index); // one without a width (or a height): as big as its image now, or its words
    void sizeOwn(std::size_t index);    // any part without a width (or a height): as big as what it shows, and placed by that
    [[nodiscard]] SDL_Color textColour(const Style& s, Look look, bool chosen = false) const; // (on its box: `chosen` as box()'s)
    [[nodiscard]] static bool hasBox(const Style& s, Look look); // a background, an edge or an image in that look (high contrast draws it)
    [[nodiscard]] static bool filledLook(Look look, bool chosen); // high contrast: chosen (pointed at or not), filled yellow
    [[nodiscard]] float boxAlpha(const Style& s, float alpha);
    Font& styleFont(const Style& s); // its font, at the player's text size when it has text_size_follows=yes
    [[nodiscard]] std::string interpolate(std::string_view text);
    [[nodiscard]] RichText rich(std::string_view text); // {expressions} filled in, {bold} and the like as spans
    // A text part's words as it shows them: rich(), and with lines=N at most N lines `width` wide (what doesn't fit
    // cut, with ...).
    [[nodiscard]] RichText textOf(std::size_t index, float width);

    [[nodiscard]] bool visible(const Element& el);
    [[nodiscard]] bool live(std::size_t index); // shown now: its when= lets it, and the part it's in was drawn last time
    [[nodiscard]] bool enabled(const Element& el);
    [[nodiscard]] bool selected(const Element& el);
    [[nodiscard]] bool focused(std::size_t element, int item = 0, int option = -1) const { return focus_ == Spot{element, item, option}; }
    [[nodiscard]] const Target* targetAt(float x, float y) const;
    [[nodiscard]] const Target* focusedTarget() const;
    [[nodiscard]] const Target* placeTarget() const; // the part pointed at, else the one the keys go on from (place_)

    void setFocus(const Target& t);
    void activate(const Target& t, std::optional<SDL_FPoint> mouse);
    void clicked(const Element& el); // its click_sound
    // The move actions: what they can point at, and the nearest of those that way from the part at `from`
    // (`score`: how far; see toward()).
    [[nodiscard]] bool keyReachable(const Target& t) const;
    [[nodiscard]] const Target* nearest(const SDL_FRect& from, int dx, int dy, float* score = nullptr) const;
    void pointByKeys(const Target& t); // pointed at by the keys, and scrolled into view
    // What scrolls when the keys scroll: its history, else the container that scrolls with the part pointed at in
    // it, else the first one shown.
    [[nodiscard]] std::optional<std::size_t> scroller();
    void slide(std::size_t element, SDL_FPoint at);
    void run(const Element& el);

    const ScreenDef* def_;
    Engine& engine_;
    bool tools_ = false;
    [[nodiscard]] float now() const; // the game's clock, or the developer tools'
    // The player's Animations setting off (not in the tools): animations show how they end, and looping
    // ones stand at their start. played(): where `a` is after `seconds`, so.
    [[nodiscard]] bool still() const;
    [[nodiscard]] float played(const Animation& a, float seconds) const;
    Lookup lookup_; // the engine's names (a list's item's first), with a textbox's line's own (speaker, has_face...) for its line
    std::uint32_t arrivals_ = 0; // messages come so far, as this screen saw it (one more: message_animation= plays)
    std::unordered_map<std::string, RichText, StringHash, std::equal_to<>> richRead_; // rich()'s texts with nothing to fill in, read once
    std::vector<ElementState> state_;
    std::vector<bool> drawn_;
    std::uint64_t drawnFrame_ = 0; // the frame it was last drawn in
    bool laidOutOpen_ = false;      // laid out since it last opened (its words are this time's: Read aloud says them then)
    std::vector<Target> targets_; // from the last draw, topmost last
    std::optional<Spot> focus_;
    std::optional<Spot> place_;           // the part pointed at or clicked last: the keys go on from it while nothing's pointed at
    bool keyFocus_ = false;               // ...pointed at by the keys (a controller's d-pad...), not the pointer
    Ring ringAt_ = Ring::None;            // (ringWhenDrawn)
    float pageTurns_ = 0.0f;              // the wheel's turns over the saves' pages, not yet a whole one
    std::optional<std::size_t> typing_;   // the input box clicked last
    std::optional<std::size_t> dragging_; // a slider being dragged
    // A button with drag= the player holds: it follows the pointer once moved (from where on it they
    // took it), or waits for a click on a part with drop= (keyboard, controller, a tap).
    std::optional<std::size_t> held_;
    bool heldMoved_ = false;
    std::optional<std::size_t> pressing_; // a button the mouse went down on: it looks chosen, and is clicked when the mouse comes up on it
    std::optional<float> shownSince_, leftSince_; // when it opened (its show animation), when it closed (its hide animation)
    Played opening_, closing_;
    SDL_FPoint grab_{}, pressed_{};
    Commands chosen_; // (see takeChosen)
    void hold(std::size_t element);
    void drop(const Element& target);
    SDL_FPoint mouse_{-1.0f, -1.0f};
};

// What the screens' files (Screens, ScreenLists, ScreenInput) share: a mouse, key, controller or touch event; a save's
// time as its list shows it; a box's middle; whether a part holds parts; a colour as a value ("#e07090").
[[nodiscard]] bool isInput(const SDL_Event& e);
[[nodiscard]] std::string formatTime(SDL_Time time);
[[nodiscard]] SDL_FPoint centre(const SDL_FRect& r);
[[nodiscard]] bool isBox(ElementKind kind);
[[nodiscard]] std::string hexOf(SDL_Color c);
// Where the keys' arrows go, on the game's screens and in the developer tools alike: of `boxes`, the nearest the way dx,
// dy goes from `from` (one in line with it first: in its row moving across, in its column moving up or down; else one past
// it that way, not one beside it in the next row), and how far (`score`: the distance on, what's off to the side counting
// half in line, double past it); none when there's none that way.
[[nodiscard]] std::optional<std::size_t> nearestThatWay(const SDL_FRect& from, std::span<const SDL_FRect> boxes, int dx, int dy, float* score = nullptr);

} // namespace vn
