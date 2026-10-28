#pragma once

// What the player draws: fonts and text, shapes, and the script's screens.

#include "Assets.hpp"
#include "Script.hpp"
#include "System.hpp"

#include <SDL3_ttf/SDL_ttf.h>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Text and shapes
// ---------------------------------------------------------------------------
// Fonts, text layout and the basic shapes everything is drawn with.
namespace vn {

namespace draw {
void rect(SDL_Renderer* r, const SDL_FRect& rect, SDL_Color c);
void outline(SDL_Renderer* r, const SDL_FRect& rect, SDL_Color c);
void gradient(SDL_Renderer* r, const SDL_FRect& rc, SDL_Color top, SDL_Color bottom);
void circle(SDL_Renderer* r, float cx, float cy, float radius, SDL_Color c);
void quad(SDL_Renderer* r, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color color);
SDL_Color hsv(float h, float s, float v, Uint8 alpha = 255);
} // namespace draw

// Colours for the engine's own drawing (error screen, placeholders). Game screens use script styles.
namespace theme {
inline constexpr SDL_Color text{240, 240, 245, 255};
inline constexpr SDL_Color dim{150, 150, 165, 255};
} // namespace theme

using FontPtr = std::unique_ptr<TTF_Font, SdlDeleter<TTF_CloseFont>>; // (a font file open)

// A TTF font that renders at the window's real pixel density, so text stays sharp when the
// 1280x720 logical canvas is scaled up. Falls back to SDL's built-in debug font if no TTF is found.
class Font {
public:
    Font() = default;
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&&) = delete;
    Font& operator=(Font&&) = delete;

    // Tries each candidate path in order. Returns false if it fell back to the debug font. `style`
    // picks a named style of a font file that has several (variable fonts); empty means Regular.
    // `fallbacks` are fonts for the letters this one doesn't have (Japanese, emoji...).
    bool load(std::span<const std::string> candidates, float pointSize, std::string_view style = {},
              std::span<const std::string> fallbacks = {}, const GameFiles& files = GameFiles::disk());
    void setPixelScale(float scale);

    [[nodiscard]] bool isFallback() const { return font_ == nullptr; }
    [[nodiscard]] float lineHeight() const;
    [[nodiscard]] float measure(std::string_view text) const;
    [[nodiscard]] std::uint32_t generation() const { return generation_; }
    [[nodiscard]] float pixelScale() const { return scale_; }
    // This font bold, italic, underlined or struck through (TTF_STYLE_* bits; a font file's own
    // Bold / Italic styles when it has them), and/or `size` times as big. Made when first needed.
    [[nodiscard]] const Font& variant(TTF_FontStyleFlags style, float size = 1.0f) const;

    // White glyphs, physical pixel size; tint with SDL_SetTextureColorMod.
    [[nodiscard]] TexturePtr render(SDL_Renderer* renderer, std::string_view text) const;
    void drawFallback(SDL_Renderer* renderer, float x, float y, std::string_view text, SDL_Color color) const;

private:
    [[nodiscard]] float fallbackScale() const;

    FontPtr font_;
    const GameFiles* files_ = &GameFiles::disk(); // (what its files are read from: its variants' too)
    float pointSize_ = 24.0f;
    float scale_ = 1.0f;
    std::uint32_t generation_ = 0;
    std::string path_, style_;
    std::vector<std::string> fallbackPaths_;
    const Font* upright_ = nullptr; // a slanted (made-italic) font: the same upright, for measuring
    std::vector<FontPtr> fallbacks_; // (taken off font_ before they're closed: see closeFont, which the destructor runs)
    void closeFont();
    mutable std::map<std::pair<TTF_FontStyleFlags, int>, std::unique_ptr<Font>> variants_;
};

// A styled range of a TextBlock's text, in codepoints: colour, bold / italic / underline /
// strike (TTF_STYLE_* bits) and furigana (a reading shown small above it).
struct TextSpan {
    std::size_t begin = 0, end = 0;
    std::optional<SDL_Color> color;
    TTF_FontStyleFlags style = 0;
    std::string ruby;
};

// A text and the spans that style it (made from text tags by richText(), in Game.hpp).
struct RichText {
    std::string text;
    std::vector<TextSpan> spans;
};

// Word-wrapped, cached text with a typewriter reveal and styled spans. Lines break at spaces, and
// anywhere in Japanese and Chinese (not before closing punctuation, nor inside furigana).
class TextBlock {
public:
    static constexpr std::size_t kAll = std::numeric_limits<std::size_t>::max();

    // `lineSpacing`: rows this many times the font's line height apart.
    void set(const Font& font, std::string_view text, float wrapWidth = 0.0f, float lineSpacing = 1.0f);
    void setSpans(std::vector<TextSpan> spans);
    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] SDL_FPoint size();
    [[nodiscard]] std::size_t length(); // codepoints in the text
    // How much to show when `reveal` codepoints are typed out: in Word and Line mode the word or
    // line being typed appears whole.
    [[nodiscard]] std::size_t visible(std::size_t reveal, Reveal mode);

    // Where codepoints [begin, end) are, a box on each row they're on, as drawn by draw() at 0, 0
    // (words that can be clicked).
    [[nodiscard]] std::vector<SDL_FRect> boxes(std::size_t begin, std::size_t end);

    // `reveal`: how many codepoints of the text (counting spaces and line breaks) are visible.
    void draw(SDL_Renderer* renderer, float x, float y, SDL_Color color, std::size_t reveal = kAll,
              const TextShadow& shadow = {});
    // Each line positioned inside `box` by `align` / `valign` (0 = start, 0.5 = centre, 1 = end).
    void drawIn(SDL_Renderer* renderer, const SDL_FRect& box, float align, float valign, SDL_Color color,
                std::size_t reveal = kAll, const TextShadow& shadow = {});

private:
    struct Piece { // a run of one style, colour and direction on a line
        std::size_t begin = 0, end = 0; // codepoints, from the line's start
        TTF_FontStyleFlags style = 0;
        std::optional<SDL_Color> color;
        int level = 0;                  // odd: right to left (Arabic, Hebrew...)
        float x = 0.0f, width = 0.0f;   // x: where it is on the line (pieces are in reading order, not left to right)
        std::vector<float> advance; // advance[i] = width of its first i+1 codepoints (made when needed)
        TexturePtr texture;
    };
    struct Ruby {
        std::string text;
        std::size_t end = 0;  // shown once the line's first `end` codepoints are
        float center = 0.0f;  // over this x, from the line's start
        TexturePtr texture;
    };
    struct Line {
        std::size_t rawBegin = 0; // codepoint index of the line's first character in the text
        std::size_t codepoints = 0;
        bool rtl = false;         // its paragraph reads right to left (it starts at the right, "start" is right)
        float width = 0.0f;
        float y = 0.0f;           // from the block's top
        std::vector<Piece> pieces;
        std::vector<Ruby> rubies;
    };

    void layout();
    void wrapParagraph(std::size_t from, std::size_t to);
    // A line, its codepoints with their levels (Unicode's bidirectional algorithm: odd, right to left;
    // see wrapParagraph), in pieces put in their order on screen.
    void addLine(std::size_t from, std::size_t to, bool rtl, const std::vector<int>& level);
    [[nodiscard]] bool canBreakBefore(std::size_t i, std::size_t from, std::size_t to) const;
    [[nodiscard]] float measure(std::size_t from, std::size_t to) const; // codepoints of the text, in their styles
    [[nodiscard]] std::string_view slice(std::size_t from, std::size_t to) const;
    void drawLines(SDL_Renderer* renderer, float x0, float y, float align, float width, SDL_Color color,
                   std::size_t reveal, const TextShadow& shadow);
    [[nodiscard]] float lineStep() const;

    const Font* font_ = nullptr;
    std::string text_;
    float wrap_ = 0.0f;
    float spacing_ = 1.0f;
    std::vector<Line> lines_;
    std::vector<TextSpan> spans_;
    std::vector<std::size_t> offsets_; // byte offset of each codepoint (and the end)
    std::vector<Uint32> codes_;        // the codepoints
    std::vector<TTF_FontStyleFlags> styles_; // each codepoint's style bits
    std::vector<std::optional<SDL_Color>> colors_;
    std::uint32_t generation_ = std::numeric_limits<std::uint32_t>::max();
    std::size_t length_ = 0;
    SDL_FPoint size_{};
};

// Fonts by (name, size), as named in the script's `font` lines. Every font falls back to the
// "default" font, then to a system font, then to SDL's debug font. The name "" is the system font
// (the player's own text: the developer tools, the mistakes screen).
class FontCache {
public:
    explicit FontCache(const GameFiles& source = GameFiles::disk()) : source_(source) {} // (what the fonts are read from)
    struct File {
        std::string path;
        std::string style;
    };
    // `fallbacks`: fonts for letters the others don't have, in order.
    void configure(std::map<std::string, File, std::less<>> files, std::vector<std::string> systemFonts,
                   std::vector<std::string> fallbacks = {});
    Font& get(const std::string& name, float size);
    void setPixelScale(float scale);
    void clear() { fonts_.clear(); }

private:
    std::map<std::pair<std::string, int>, std::unique_ptr<Font>> fonts_;
    std::map<std::string, File, std::less<>> files_;
    std::vector<std::string> system_, fallbacks_;
    const GameFiles& source_;
    float scale_ = 1.0f;
};

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

class Engine;

// A screen from the script, live: draws its elements and handles input for them.
// A place on the screen across, -1 to 1 like the stage's, as the left edge of something `size` wide on a
// `screen` that wide: ±1 lines its edge up with the screen's, past that it goes off, and at ±2 it's just off
// the screen. (Down: pass minus the place, as 1 is the top; it gives the top edge.)
[[nodiscard]] float placeEdge(float place, float size, float screen);

class ScreenView {
public:
    // `tools`: shown in the developer tools (its animations go on while the game stands still under them).
    ScreenView(const ScreenDef& def, Engine& engine, bool tools = false);
    ScreenView(const ScreenView&) VN_DELETED("its lookups point at it");
    ScreenView& operator=(const ScreenView&) VN_DELETED("its lookups point at it");
    ScreenView(ScreenView&&) VN_DELETED("its lookups point at it");
    ScreenView& operator=(ScreenView&&) VN_DELETED("its lookups point at it");
    ~ScreenView() = default;

    [[nodiscard]] const std::string& name() const { return def_->name; }
    [[nodiscard]] bool modal() const { return def_->modal; }
    [[nodiscard]] const ScreenDef& def() const { return *def_; }
    void reset(); // forget focus, scrolling and dragging (when the screen opens again)
    void opened(); // its show_animation= starts (reset() does this too)
    void leave();  // it's closing: its hide_animation= plays (it's drawn, but takes no input)
    [[nodiscard]] bool leaving() const; // ...until that's over
    [[nodiscard]] bool hasLeft() const { return leftSince_.has_value(); } // (since leave(), until it opens again)
    [[nodiscard]] bool startsCovered() const; // its show_animation= begins with everything covered by a colour (fade=)
    [[nodiscard]] bool endsCovered() const;   // its hide_animation= ends with everything covered (fade=)
    void draw(SDL_Renderer* r, float alpha = 1.0f);
    // After a frame: its videos that weren't drawn in it stop (the screen was closed or hidden).
    void endFrame(std::uint64_t frame);
    // Returns true when the event was used (modal screens use every input event).
    bool handleEvent(const SDL_Event& e);
    // The editor's view of the last draw: whether part `index` was drawn (its if= let it), and where
    // (its box, or for one sized by its text, the text's).
    [[nodiscard]] bool drawn(std::size_t index) const { return index < drawn_.size() && drawn_[index]; }
    [[nodiscard]] SDL_FRect bounds(std::size_t index);
    // Where part `index` is now (its box, as laid out), and a part's index from the part itself.
    [[nodiscard]] SDL_FRect& rectOf(std::size_t index) { return state_[index].rect; }
    [[nodiscard]] const SDL_FRect& rectOf(std::size_t index) const { return state_[index].rect; }
    [[nodiscard]] std::size_t indexOf(const Element& el) const { return static_cast<std::size_t>(&el - def_->elements.data()); }
    // The style part `index` is drawn with: its own, or the one its animation makes of it for the moment.
    [[nodiscard]] const Style& styleOf(std::size_t index) const { return state_[index].styled ? *state_[index].styled : def_->elements[index].style; }
    // The words of what the mouse (or the keys) is on: a button's label or an option (read aloud);
    // empty when it's on nothing.
    [[nodiscard]] std::string pointedText();
    // A button's click_animation= is over: what it does (and its question first), to run. None when
    // the button (or its screen) went away meanwhile.
    [[nodiscard]] std::optional<std::pair<Commands, std::string>> takePressed();
    // What the buttons that came to be chosen because of their selected= set (set VARIABLE to VALUE,
    // setting NAME VALUE), to put in effect as if they were clicked: a choice that shows is the one that
    // holds. Their other actions (opening a screen, music, quitting...) run only when they're clicked.
    // Gathered as the screen is drawn: once a button becomes chosen, and again each time the screen opens.
    [[nodiscard]] Commands takeChosen() { return std::exchange(chosen_, {}); }
    // The drag= of the button the player holds, if any ({dragging}).
    [[nodiscard]] std::string held() const { return held_ ? def_->elements[*held_].option("drag") : std::string{}; }
    // The box part `index` is in, where it is now: the part it's in, or the screen (its x and y are places
    // in it, its w and h shares of it).
    [[nodiscard]] SDL_FRect boxOf(std::size_t index) const;
    // The mouse went down on one of its buttons: it's clicked if it comes up there (the engine gives
    // this screen the mouse's coming up).
    [[nodiscard]] bool pressing() const { return pressing_.has_value(); }

private:
    enum class Look : std::uint8_t { Idle, Hover, Selected, Disabled };
    // What can be clicked: a part, or one of its items (a choice, a tab, a save...), or a small
    // button on an item: a save's delete button, a history line's "hear it again" and "go back", or
    // an option of an item: an answer to a message.
    enum class Button : std::uint8_t { None, Delete, Voice, Back };
    struct Spot {
        std::size_t element = 0;
        int item = 0;
        Button button = Button::None;
        int option = -1;
        bool operator==(const Spot&) const = default;
    };
    struct Target {
        Spot spot;
        SDL_FRect rect;
        bool keyboard; // reachable with the arrow keys
    };
    // Lists and grids (choices, saves, the gallery, endings, the chart...): columns= item_w= item_h=
    // gap= (or gap_x= and gap_y=), with these defaults (an itemW of 0: the part's width shared out).
    struct Grid {
        int columns = 1;
        float itemW = 0, itemH = 0, gapX = 0, gapY = 0;
        [[nodiscard]] SDL_FRect cell(const SDL_FRect& area, std::size_t i, float scroll = 0.0f) const;
        [[nodiscard]] float height(std::size_t items) const; // of all the rows
    };
    // (item_w=, gap=... are shares of the box the part is in; without them, these pixels)
    [[nodiscard]] Grid grid(const Element& el, Grid defaults) const;
    [[nodiscard]] float share(const Element& el, std::string_view key, bool across, float fallback) const; // (name_w=, wide_w=...)
    void layout(); // where each part is now (its place and size in its box)
    struct ElementState {
        SDL_FRect rect{}; // where the part is on the screen, in the game's pixels (laid out every frame; w/h 0: automatic)
        std::optional<Style> styled;  // while its animation changes its look (zoomed, a colour): the style it's drawn with
        TextBlock text, value;
        std::vector<TextBlock> items, extra, buttons;
        std::vector<std::string> cache;
        float scroll = 0.0f;
        std::unique_ptr<Movie> movie; // a movie part: playing
        std::string movieName;
        float movieTime = 0.0f;       // the engine's time when it last played
        std::uint64_t movieFrame = 0; // the frame it was last drawn in
        float panX = 0.0f;            // the flowchart: how far it's been moved sideways
        bool centered = false;        // ...and whether it has been moved to the current place yet
        std::vector<std::string> ids; // ...the place each item goes to
        std::optional<float> since;     // a part with an animation: when it showed (its animation starts then)
        std::optional<float> clicked;   // a button with click_animation=: when it was clicked (it plays from then)
        float reach = 0.0f, reachX = 0.0f; // a container that scrolls: how far down from its top, and across from its left, its parts reach
        float scrollX = 0.0f;              // ...and how far it's scrolled across
        float glideX = 0.0f, glideY = 0.0f, glideAt = 0.0f; // ...gliding on after a drag: how fast (the game's pixels a second), and when it last moved
        float ownW = 0.0f, ownH = 0.0f;    // a container with fit=parts: as wide and as high as its parts, measured where they are (0: not yet)
        std::optional<SDL_FRect> hit;   // a button without a size: where it's clicked (its usual and its current picture's size together)
        bool ownWords = false;          // ...one without a picture: as big as its words (drawn as they are, not wrapped)
        bool chosen = false;            // a button with selected=: chosen when it was last drawn (see takeChosen)
    };
    struct Shift { // what the part a part is in moves it by (its animation's), how faded, and how covered with a colour
        float x = 0.0f, y = 0.0f, opacity = 1.0f, fade = 0.0f;
        SDL_Color fadeColor{0, 0, 0, 255};
    };
    [[nodiscard]] bool within(std::size_t element, std::size_t container) const; // in it, or in a part in it
    // Where the containers a part is in cut it off (their edges=cut or fade, or scroll=yes), moved by their
    // `shifts` when given; none: nowhere.
    [[nodiscard]] std::optional<SDL_FRect> cutBy(std::size_t index, const std::vector<Shift>* shifts = nullptr) const;
    bool startAside(SDL_Renderer* r, float& scale); // aside_ the window's size (scale: its pixels per one of the game's)
    TexturePtr aside_; // what a container with edges=fade holds, drawn here first
    // Dragging scrolls a container with scroll=yes, a grid, a history, a phone and a flowchart (it glides on
    // when let go moving, as glide=, glide_speed= and glide_distance= say; Element::dragDirection() says
    // which way). scrollableAt(): the innermost one under a point. glideStep(): a glide's next moment;
    // settle(): a list's scroll glided on and kept from 0 to `most`, stopping at an end.
    [[nodiscard]] std::optional<std::size_t> scrollableAt(float x, float y);
    void glideStep(std::size_t index);
    void settle(std::size_t index, float most);
    // A container's scrollbars (scrollbar=yes) at its right edge and (`across`) along its bottom, and the
    // thumb on each; the bars, drawn after its parts.
    [[nodiscard]] SDL_FRect barOf(std::size_t index, bool across) const;
    [[nodiscard]] SDL_FRect thumbOf(std::size_t index, bool across) const;
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

    void drawElement(SDL_Renderer* r, std::size_t index, float alpha);
    // A part with animation= (or a button playing its click_animation=): drawn as its animation has it
    // now, then put back as it's written. Gives how it looks now (the parts in it move with it).
    // `shift`: what the part it's in (or its screen) moves it by now (its places are where it's written, and it moves with that).
    Animation::Look drawAnimated(SDL_Renderer* r, std::size_t index, float alpha, const Animation& a, float seconds, SDL_FPoint shift, const SDL_FRect& box);
    std::optional<std::size_t> playing_; // a button whose click animation plays: what it does is done when it's over
    float turn_ = 0.0f;                     // ...a picture's turn and tint meanwhile
    SDL_Color tint_{255, 255, 255, 255};
    // `flip` mirrors it (on top of the element's flip=); `box`: where (else the element's own box).
    void picture(SDL_Renderer* r, const Picture& pic, const Element& el, std::string_view fit, float ax, float ay, float alpha,
                 bool flip = false, const SDL_FRect* box = nullptr);
    void drawNamebox(SDL_Renderer* r, std::size_t index, float alpha);
    void drawChoices(SDL_Renderer* r, std::size_t index, float alpha);
    void drawHistory(SDL_Renderer* r, std::size_t index, float alpha);
    void drawRecords(SDL_Renderer* r, std::size_t index, float alpha); // the gallery, endings, songs, achievements
    void drawChart(SDL_Renderer* r, std::size_t index, float alpha);
    void drawPhone(SDL_Renderer* r, std::size_t index, float alpha);
    void drawContacts(SDL_Renderer* r, std::size_t index, float alpha);
    void drawPage(SDL_Renderer* r, std::size_t index, float alpha);
    void drawPages(SDL_Renderer* r, std::size_t index, float alpha);
    void drawSlots(SDL_Renderer* r, std::size_t index, float alpha);
    void drawInput(SDL_Renderer* r, std::size_t index, float alpha);
    void drawBar(SDL_Renderer* r, std::size_t index, float alpha);
    void drawSlider(SDL_Renderer* r, std::size_t index, float alpha);
    void drawMovie(SDL_Renderer* r, std::size_t index, float alpha);
    [[nodiscard]] float number(const Element& el, std::string_view key, float fallback);
    [[nodiscard]] std::string inputTarget(const Element& el);
    bool handleInput(const SDL_Event& e);
    void adjustSlider(std::size_t element, int delta);
    [[nodiscard]] SDL_FRect sliderBox(std::size_t element) const;
    void box(SDL_Renderer* r, const SDL_FRect& rect, const Style& s, Look look, float alpha);
    void text(SDL_Renderer* r, TextBlock& block, std::string_view str, const SDL_FRect& rect, const Style& s,
              SDL_Color color, float alpha, std::size_t reveal = TextBlock::kAll, std::vector<TextSpan> spans = {});
    [[nodiscard]] static const Style::State* stateOf(const Style& s, Look look); // its look then (nullptr: its own)
    [[nodiscard]] std::string lookImage(const Style& s, Look look, bool chosen); // the picture a style shows in a look (empty: none)
    // A button's look now: it can't be clicked, it's pressed (until the mouse comes up) or playing its
    // click animation, it's pointed at, it's chosen (its selected= is true, or it's being dragged), or
    // its usual self. `hit` is where a press on it counts.
    struct ButtonLook {
        Look look;
        bool on, chosen;
    };
    [[nodiscard]] ButtonLook buttonLook(std::size_t index, const SDL_FRect& hit);
    void sizeButton(std::size_t index); // one without a width (or a height): as big as its picture now, or its words
    void sizeOwn(std::size_t index);    // any part without a width (or a height): as big as what it shows, and placed by that
    [[nodiscard]] SDL_Color textColor(const Style& s, Look look) const;
    [[nodiscard]] float boxAlpha(const Style& s, float alpha);
    Font& styleFont(const Style& s); // its font, at the player's text size when it has text_size=yes
    [[nodiscard]] std::string interpolate(std::string_view text);
    [[nodiscard]] RichText rich(std::string_view text); // {expressions} filled in, {bold} and the like as spans

    [[nodiscard]] bool visible(const Element& el);
    [[nodiscard]] bool live(std::size_t index); // shown now: its if= lets it, and the part it's in was drawn last time
    [[nodiscard]] bool enabled(const Element& el);
    [[nodiscard]] bool selected(const Element& el);
    [[nodiscard]] bool focused(std::size_t element, int item = 0, Button button = Button::None, int option = -1) const {
        return focus_ == Spot{element, item, button, option};
    }
    [[nodiscard]] const Target* targetAt(float x, float y) const;
    [[nodiscard]] const Target* focusedTarget() const;

    void setFocus(const Target& t);
    void activate(const Target& t, std::optional<float> mouseX);
    void clicked(const Element& el); // its click_sound
    void navigate(int dx, int dy);
    void slide(std::size_t element, float x);
    void run(const Element& el);
    bool scrollWidgets(const SDL_Event& e);

    const ScreenDef* def_;
    Engine& engine_;
    bool tools_ = false;
    [[nodiscard]] float now() const; // the game's clock, or the developer tools'
    Lookup lookup_; // the engine's names, with a textbox's line's own (speaker, has_face...) for its line
    std::vector<ElementState> state_;
    std::vector<bool> drawn_;
    std::uint64_t drawnFrame_ = 0; // the frame it was last drawn in
    std::vector<Target> targets_; // from the last draw, topmost last
    std::optional<Spot> focus_;
    std::optional<std::size_t> dragging_; // a slider being dragged
    // A button with drag= the player holds: it follows the pointer once moved (from where on it they
    // took it), or waits for a click on a part with drop= (keyboard, controller, a tap).
    std::optional<std::size_t> held_;
    bool heldMoved_ = false;
    std::optional<std::size_t> pressing_; // a button the mouse went down on: it looks chosen, and is clicked when the mouse comes up on it
    std::optional<float> shownSince_, leftSince_; // when it opened (its show_animation=), when it closed (its hide_animation=)
    SDL_FPoint grab_{}, pressed_{};
    Commands chosen_; // (see takeChosen)
    void hold(std::size_t element);
    void drop(const Element& target);
    SDL_FPoint mouse_{-1.0f, -1.0f};
};

} // namespace vn
