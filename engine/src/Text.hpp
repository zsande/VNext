#pragma once

// Fonts and text: a font at the window's pixel density (with its fallbacks), text laid out in lines and drawn, and
// text with a look (rich text).

#include "Values.hpp"
#include "Files.hpp"

#include <SDL3_ttf/SDL_ttf.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vn {

// Colours for the engine's own drawing (error screen, placeholders). Game screens use script styles.

namespace theme {

inline constexpr SDL_Color text{240, 240, 245, 255};
inline constexpr SDL_Color dim{150, 150, 165, 255};

} // namespace theme

using FontPtr = std::unique_ptr<TTF_Font, SdlDeleter<TTF_CloseFont>>; // (a font file open)

// A TTF font that renders at the window's real pixel density, so text stays sharp when the
// game's canvas is scaled up. Falls back to SDL's built-in debug font if no TTF is found.
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
    void drawFallback(SDL_Renderer* renderer, float x, float y, std::string_view text, SDL_Color colour) const;

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
// crossed out (TTF_STYLE_* bits) and furigana (a reading shown small above it).
struct TextSpan {
    std::size_t begin = 0, end = 0;
    std::optional<SDL_Color> colour;
    TTF_FontStyleFlags style = 0;
    std::string ruby;
};

// A text and the spans that style it (made from text tags by richText(), below).
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
    [[nodiscard]] std::size_t lines(); // how many it takes
    [[nodiscard]] std::size_t length(); // codepoints in the text
    // How much to show when `reveal` codepoints are typed out: in Word and Line mode the word or
    // line being typed appears whole.
    [[nodiscard]] std::size_t visible(std::size_t reveal, TextAppears mode);

    // Where codepoints [begin, end) are, a box on each row they're on, as drawn by draw() at 0, 0
    // (words that can be clicked).
    [[nodiscard]] std::vector<SDL_FRect> boxes(std::size_t begin, std::size_t end);

    // `reveal`: how many codepoints of the text (counting spaces and line breaks) are visible.
    void draw(SDL_Renderer* renderer, float x, float y, SDL_Color colour, std::size_t reveal = kAll,
              const TextShadow& shadow = {});
    // Each line positioned inside `box` by `align` / `alignVertically` (0 = start, 0.5 = centre, 1 = end).
    void drawIn(SDL_Renderer* renderer, const SDL_FRect& box, float align, float alignVertically, SDL_Color colour,
                std::size_t reveal = kAll, const TextShadow& shadow = {});

private:
    struct Piece { // a run of one style, colour and direction on a line
        std::size_t begin = 0, end = 0; // codepoints, from the line's start
        TTF_FontStyleFlags style = 0;
        std::optional<SDL_Color> colour;
        int level = 0;                  // odd: right to left (Arabic, Hebrew...)
        float x = 0.0f, width = 0.0f;   // x: where it is on the line (pieces are in reading order, not left to right)
        std::vector<float> advance; // advance[i] = width of its first i+1 codepoints (made when needed)
        TexturePtr texture;
    };
    struct Ruby {
        std::string text;
        std::size_t end = 0;  // shown once the line's first `end` codepoints are
        float centre = 0.0f;  // over this x, from the line's start
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
    void drawLines(SDL_Renderer* renderer, float x0, float y, float align, float width, SDL_Color colour,
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
    std::vector<std::optional<SDL_Color>> colours_;
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
    [[nodiscard]] const File* file(std::string_view name) const { // (a font's file by its name: none, the system's)
        const auto it = files_.find(name);
        return it != files_.end() ? &it->second : nullptr;
    }
    void setPixelScale(float scale);
    void clear() { fonts_.clear(); }

private:
    std::map<std::pair<std::string, int>, std::unique_ptr<Font>> fonts_;
    std::map<std::string, File, std::less<>> files_;
    std::vector<std::string> system_, fallbacks_;
    const GameFiles& source_;
    float scale_ = 1.0f;
};

// A text with its look (see RichText): {colour} {bold} {italic} {underline} {crossed_out} and {furigana}
// tags become spans, and {expressions} are filled in. Other tags ({pause}, {speed}...) go to `other`.
[[nodiscard]] RichText richText(std::string_view source, const Lookup& lookup, const Palette* palette, const TagHandler& other = {});

} // namespace vn
