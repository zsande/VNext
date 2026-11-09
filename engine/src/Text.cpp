// Fonts and text: fonts loaded with their fallbacks, text laid out in lines (both ways, and Chinese and Japanese),
// drawn letter by letter, and a text's tags as its look (rich text).
#include "Text.hpp"

#include <SDL3/SDL.h>
#include <SheenBidi/SheenBidi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>

namespace vn {

Font::~Font() {
    closeFont();
}

void Font::closeFont() {
    if (font_)
        for (const FontPtr& f : fallbacks_) TTF_RemoveFallbackFont(font_.get(), f.get()); // (off it before they're closed)
    fallbacks_.clear();
    font_.reset();
}

namespace {

// Opens a font file. A file with several named styles (a variable font) opens at the one called
// `style` (Regular by default) rather than its built-in default, which is often a thin weight.
FontPtr openStyled(const GameFiles& files, const std::string& path, float size, std::string_view style) {
    FontPtr font{TTF_OpenFontIO(files.open(path).release(), true, size)};
    if (!font) return nullptr;
    const std::string want = style.empty() ? "regular" : lower(style);
    if (const char* name = TTF_GetFontStyleName(font.get()); name && lower(name) == want) return font;
    for (Sint64 instance = 1; instance <= 64; ++instance) {
        const SdlProperties props;
        IOStreamPtr stream = files.open(path);
        SDL_SetPointerProperty(props.id(), TTF_PROP_FONT_CREATE_IOSTREAM_POINTER, stream.get());
        SDL_SetBooleanProperty(props.id(), TTF_PROP_FONT_CREATE_IOSTREAM_AUTOCLOSE_BOOLEAN, true);
        SDL_SetFloatProperty(props.id(), TTF_PROP_FONT_CREATE_SIZE_FLOAT, size);
        SDL_SetNumberProperty(props.id(), TTF_PROP_FONT_CREATE_FACE_NUMBER, instance << 16);
        FontPtr candidate{TTF_OpenFontWithProperties(props.id())};
        static_cast<void>(stream.release()); // (the font has it now: closed with it, or already, if it couldn't open)
        if (!candidate) break; // no more named styles (or none at all)
        if (const char* name = TTF_GetFontStyleName(candidate.get()); name && lower(name) == want) return candidate;
    }
    return font;
}

// Numbers no font has had before: a text laid out with a font that has since been replaced
// (a new language's) can't mistake a new font at the same address for the old one.
std::uint32_t nextGeneration() {
    static std::uint32_t last = 0;
    return ++last;
}

} // namespace

bool Font::load(std::span<const std::string> candidates, float pointSize, std::string_view style,
                std::span<const std::string> fallbacks, const GameFiles& files) {
    closeFont();
    files_ = &files;
    fallbackPaths_.assign(fallbacks.begin(), fallbacks.end());
    pointSize_ = pointSize;
    generation_ = nextGeneration();
    for (const auto& path : candidates) {
        if (path.empty()) continue;
        if (!files.exists(path)) continue;
        font_ = openStyled(files, path, pointSize_ * scale_, style);
        if (font_) {
            path_ = path;
            style_ = std::string(style);
            for (const auto& file : fallbackPaths_)
                if (FontPtr f{TTF_OpenFontIO(files.open(file).release(), true, pointSize_ * scale_)}; f && TTF_AddFallbackFont(font_.get(), f.get()))
                    fallbacks_.push_back(std::move(f));
            return true;
        }
    }
    return false;
}

void Font::setPixelScale(float scale) {
    scale = std::max(scale, 0.25f);
    if (std::abs(scale - scale_) < 0.001f) return;
    scale_ = scale;
    if (font_) TTF_SetFontSize(font_.get(), pointSize_ * scale_);
    for (const FontPtr& f : fallbacks_) TTF_SetFontSize(f.get(), pointSize_ * scale_);
    for (auto& [key, variant] : variants_) variant->setPixelScale(scale);
    generation_ = nextGeneration();
}

const Font& Font::variant(TTF_FontStyleFlags style, float size) const {
    if ((style == 0 && size == 1.0f) || !font_) return *this;
    auto& slot = variants_[{style, static_cast<int>(std::lround(size * 100.0f))}];
    if (slot) return *slot;
    slot = std::make_unique<Font>();
    slot->scale_ = scale_;
    const bool bold = (style & TTF_STYLE_BOLD) != 0, italic = (style & TTF_STYLE_ITALIC) != 0;
    // The closest style the file has: Bold Italic, else Bold (slanted by SDL_ttf), else this one.
    std::vector<std::string> tries;
    if (bold && italic) tries.emplace_back("Bold Italic");
    if (bold) tries.emplace_back("Bold");
    if (italic && !bold) tries.emplace_back("Italic");
    tries.push_back(style_);
    const std::array<std::string, 1> file{path_};
    for (const auto& want : tries) {
        if (!slot->load(file, pointSize_ * size, want, fallbackPaths_, *files_)) break;
        const char* name = TTF_GetFontStyleName(slot->font_.get());
        if (want == style_ || lower(name ? name : "") == lower(want)) break;
    }
    if (slot->font_) {
        // The file's own Bold / Italic styles look best; without them, SDL_ttf slants and thickens.
        const char* name = TTF_GetFontStyleName(slot->font_.get());
        const std::string got = lower(name ? name : "");
        TTF_FontStyleFlags made = style & (TTF_STYLE_UNDERLINE | TTF_STYLE_STRIKETHROUGH);
        if (bold && got.find("bold") == std::string::npos) made |= TTF_STYLE_BOLD;
        if (italic && got.find("italic") == std::string::npos && got.find("oblique") == std::string::npos) {
            made |= TTF_STYLE_ITALIC;
            slot->upright_ = &variant(style & ~static_cast<TTF_FontStyleFlags>(TTF_STYLE_ITALIC), size);
        }
        TTF_SetFontStyle(slot->font_.get(), made);
    }
    return *slot;
}

float Font::fallbackScale() const {
    return std::max(1.0f, std::round(pointSize_ / 12.0f));
}

float Font::lineHeight() const {
    if (font_) return static_cast<float>(TTF_GetFontLineSkip(font_.get())) / scale_;
    return SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * fallbackScale() * 1.4f;
}

float Font::measure(std::string_view text) const {
    if (text.empty()) return 0.0f;
    if (upright_) return upright_->measure(text); // slanting doesn't move the letters, only leans them
    if (!font_) return static_cast<float>(utf8::length(text)) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * fallbackScale();
    int w = 0;
    TTF_GetStringSize(font_.get(), text.data(), text.size(), &w, nullptr);
    return static_cast<float>(w) / scale_;
}

TexturePtr Font::render(SDL_Renderer* renderer, std::string_view text) const {
    if (!font_ || text.empty()) return nullptr;
    SurfacePtr surface{TTF_RenderText_Blended(font_.get(), text.data(), text.size(), SDL_Color{255, 255, 255, 255})};
    if (!surface) return nullptr;
    TexturePtr texture{SDL_CreateTextureFromSurface(renderer, surface.get())};
    if (texture) {
        SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
    }
    return texture;
}

void Font::drawFallback(SDL_Renderer* renderer, float x, float y, std::string_view text, SDL_Color colour) const {
    const float k = fallbackScale();
    float sx = 1, sy = 1;
    SDL_GetRenderScale(renderer, &sx, &sy);
    SDL_SetRenderScale(renderer, k, k);
    SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
    // Centre the 8px glyphs vertically inside the line.
    const float pad = (lineHeight() - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * k) * 0.5f;
    SDL_RenderDebugText(renderer, x / k, (y + pad) / k, std::string(text).c_str());
    SDL_SetRenderScale(renderer, sx, sy);
}

void TextBlock::set(const Font& font, std::string_view text, float wrapWidth, float lineSpacing) {
    if (font_ == &font && text_ == text && wrap_ == wrapWidth && spacing_ == lineSpacing) return;
    font_ = &font;
    text_ = std::string(text);
    wrap_ = wrapWidth;
    spacing_ = lineSpacing;
    generation_ = std::numeric_limits<std::uint32_t>::max();
}

namespace {

bool sameColour(const std::optional<SDL_Color>& a, const std::optional<SDL_Color>& b) {
    if (!a || !b) return a.has_value() == b.has_value();
    return a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a;
}

// Japanese, Chinese (and their punctuation): lines can break between any two of these.
bool isCjk(Uint32 c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) ||
           (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x2FFFF);
}

// Marks that never start a line (closing brackets, full stops, small kana...) or end one (opening brackets).
bool noLineStart(Uint32 c) {
    static constexpr std::array<Uint32, 46> marks{
        0x3001, 0x3002, 0xFF0C, 0xFF0E, 0x30FB, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF01, 0x30FC, 0xFF09, 0x300D, 0x300F, 0x3011, 0x3015, 0x3009,
        0x300B, 0x2026, 0x3005, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, 0x30A1, 0x30A3, 0x30A5,
        0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, 0x30F5, 0x30F6, 0xFF3D, 0xFF5D, 0x301F, 0x2019, 0x201D};
    return std::ranges::contains(marks, c);
}
bool noLineEnd(Uint32 c) {
    static constexpr std::array<Uint32, 10> marks{0xFF08, 0x300C, 0x300E, 0x3010, 0x3014, 0x3008, 0x300A, 0xFF3B, 0xFF5B, 0x301D};
    return std::ranges::contains(marks, c);
}

} // namespace

void TextBlock::setSpans(std::vector<TextSpan> spans) {
    auto same = [](const TextSpan& a, const TextSpan& b) {
        return a.begin == b.begin && a.end == b.end && sameColour(a.colour, b.colour) && a.style == b.style && a.ruby == b.ruby;
    };
    if (std::ranges::equal(spans, spans_, same)) return; // laid out already
    spans_ = std::move(spans);
    generation_ = std::numeric_limits<std::uint32_t>::max();
}

SDL_FPoint TextBlock::size() {
    layout();
    return size_;
}

std::size_t TextBlock::lines() {
    layout();
    return lines_.size();
}

std::size_t TextBlock::length() {
    layout();
    return length_;
}

std::size_t TextBlock::visible(std::size_t reveal, TextAppears mode) {
    layout();
    if (reveal == 0 || reveal >= length_ || mode == TextAppears::Letter) return reveal;
    if (mode == TextAppears::Line) {
        for (const auto& line : lines_)
            if (reveal - 1 < line.rawBegin + line.codepoints) return std::max(reveal, line.rawBegin + line.codepoints);
        return reveal;
    }
    // Word: the rest of the word being typed (nothing more right after a space).
    if (codes_[reveal - 1] == ' ' || codes_[reveal - 1] == '\n') return reveal;
    std::size_t n = reveal;
    while (n < length_ && codes_[n] != ' ' && codes_[n] != '\n' && !isCjk(codes_[n])) ++n;
    return n;
}

std::string_view TextBlock::slice(std::size_t from, std::size_t to) const {
    return std::string_view(text_).substr(offsets_[from], offsets_[to] - offsets_[from]);
}

float TextBlock::measure(std::size_t from, std::size_t to) const {
    float width = 0.0f;
    for (std::size_t a = from; a < to;) {
        std::size_t b = a + 1;
        while (b < to && styles_[b] == styles_[a]) ++b;
        width += font_->variant(styles_[a]).measure(slice(a, b));
        a = b;
    }
    return width;
}

void TextBlock::layout() {
    if (!font_ || generation_ == font_->generation()) return;
    generation_ = font_->generation();
    lines_.clear();
    offsets_.clear();
    codes_.clear();
    const char* at = text_.data();
    std::size_t left = text_.size();
    while (left > 0) {
        offsets_.push_back(text_.size() - left);
        codes_.push_back(SDL_StepUTF8(&at, &left));
    }
    offsets_.push_back(text_.size());
    length_ = codes_.size();
    styles_.assign(length_, 0);
    colours_.assign(length_, std::nullopt);
    for (const auto& span : spans_)
        for (std::size_t i = span.begin; i < std::min(span.end, length_); ++i) {
            styles_[i] |= span.style;
            if (span.colour) colours_[i] = span.colour;
        }

    std::size_t from = 0;
    for (std::size_t i = 0; i <= length_; ++i)
        if (i == length_ || codes_[i] == '\n') {
            wrapParagraph(from, i);
            from = i + 1;
        }

    // Rows go down a line apart; a row with furigana gets room above it.
    const float rubyHeight = font_->variant(0, 0.5f).lineHeight() * 0.85f;
    float y = 0.0f, width = 0.0f;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        y += (i == 0 ? 0.0f : lineStep()) + (lines_[i].rubies.empty() ? 0.0f : rubyHeight);
        lines_[i].y = y;
        width = std::max(width, lines_[i].width);
    }
    size_ = {width, lines_.empty() ? 0.0f : y + font_->lineHeight()};
}

namespace {

// A paragraph's directions by Unicode's bidirectional algorithm (SheenBidi): whether it reads right to
// left (its first letter's way), and each line's levels (even: left to right; odd: right to left), with
// what explicit marks and isolates say, and the spaces at a line's end its own way.
class Bidi {
public:
    Bidi(const std::vector<Uint32>& codes, std::size_t from, std::size_t to) : from_(from) {
        const SBCodepointSequence text{SBStringEncodingUTF32, codes.data() + from, to - from};
        algorithm_.reset(SBAlgorithmCreate(&text));
        if (algorithm_) paragraph_.reset(SBAlgorithmCreateParagraph(algorithm_.get(), 0, to - from, SBLevelDefaultLTR));
    }
    [[nodiscard]] bool rightToLeft() const { return paragraph_ && SBParagraphGetBaseLevel(paragraph_.get()) % 2 == 1; }
    [[nodiscard]] std::vector<int> line(std::size_t from, std::size_t to) const {
        std::vector<int> out(to - from, rightToLeft() ? 1 : 0);
        if (!paragraph_ || from == to) return out;
        const Owned<SBLineRef, SBLineRelease> line{SBParagraphCreateLine(paragraph_.get(), from - from_, to - from)};
        if (!line) return out;
        const SBRun* runs = SBLineGetRunsPtr(line.get());
        for (SBUInteger r = 0; r < SBLineGetRunCount(line.get()); ++r)
            for (SBUInteger i = runs[r].offset; i < runs[r].offset + runs[r].length; ++i)
                if (i >= from - from_ && i < to - from_) out[i - (from - from_)] = runs[r].level;
        return out;
    }

private:
    std::size_t from_; // (the paragraph's start in the text: the algorithm's 0)
    Owned<SBAlgorithmRef, SBAlgorithmRelease> algorithm_;
    Owned<SBParagraphRef, SBParagraphRelease> paragraph_; // (after the algorithm: it goes first)
};

} // namespace

bool TextBlock::canBreakBefore(std::size_t i, std::size_t from, std::size_t to) const {
    if (i <= from || i >= to) return false;
    for (const auto& span : spans_) // furigana stay over a whole word
        if (!span.ruby.empty() && span.begin < i && i < span.end) return false;
    const Uint32 before = codes_[i - 1], here = codes_[i];
    if (before == ' ') return here != ' ';
    if (here == ' ') return false;
    return (isCjk(here) || isCjk(before)) && !noLineStart(here) && !noLineEnd(before);
}

// Splits a paragraph (codepoints from..to) into lines no wider than the box: as much as fits,
// up to a place where a line can break; a word wider than the box is broken anywhere.
void TextBlock::wrapParagraph(std::size_t from, std::size_t to) {
    const Bidi bidi(codes_, from, to);
    const bool rtl = bidi.rightToLeft();
    if (wrap_ <= 0.0f || from == to) return addLine(from, to, rtl, bidi.line(from, to));
    std::size_t start = from;
    while (start < to) {
        std::size_t next = 0, end = 0; // where the next line starts, and where this one ends (without spaces)
        for (std::size_t i = start + 1; i <= to; ++i) {
            if (i < to && !canBreakBefore(i, from, to)) continue;
            std::size_t e = i;
            while (e > start && codes_[e - 1] == ' ') --e;
            if (measure(start, e) > wrap_) break;
            next = i;
            end = e;
        }
        if (next == 0) { // nothing fits: as much of the word as does
            std::size_t e = start + 1;
            while (e < to && measure(start, e + 1) <= wrap_) ++e;
            addLine(start, e, rtl, bidi.line(start, e));
            start = e;
            continue;
        }
        addLine(start, end, rtl, bidi.line(start, end));
        start = next;
    }
}

void TextBlock::addLine(std::size_t from, std::size_t to, bool rtl, const std::vector<int>& level) {
    Line line{.rawBegin = from, .codepoints = to - from, .rtl = rtl};
    for (std::size_t a = from; a < to;) { // pieces of one style, colour and direction
        std::size_t b = a + 1;
        while (b < to && styles_[b] == styles_[a] && sameColour(colours_[b], colours_[a]) && level[b - from] == level[a - from]) ++b;
        Piece piece{.begin = a - from, .end = b - from, .style = styles_[a], .colour = colours_[a], .level = level[a - from]};
        piece.width = font_->variant(piece.style).measure(slice(a, b));
        line.pieces.push_back(std::move(piece));
        a = b;
    }
    // Their order on screen: from the highest level down, every stretch at that level or higher
    // turns round (Unicode's rule L2).
    std::vector<std::size_t> order(line.pieces.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    int highest = 0, lowestOdd = std::numeric_limits<int>::max();
    for (const Piece& p : line.pieces) {
        highest = std::max(highest, p.level);
        if (p.level % 2 == 1) lowestOdd = std::min(lowestOdd, p.level);
    }
    for (int lv = highest; lv >= lowestOdd; --lv)
        for (std::size_t i = 0; i < order.size();) {
            if (line.pieces[order[i]].level < lv) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < order.size() && line.pieces[order[j]].level >= lv) ++j;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        }
    float x = 0.0f;
    for (std::size_t i : order) {
        line.pieces[i].x = x;
        x += line.pieces[i].width;
    }
    line.width = x;
    for (const auto& span : spans_) // furigana over the words on this line
        if (!span.ruby.empty() && span.begin >= from && span.end <= to && span.begin < span.end)
            line.rubies.push_back({.text = span.ruby, .end = span.end - from,
                                   .centre = measure(from, span.begin) + measure(span.begin, span.end) * 0.5f});
    lines_.push_back(std::move(line));
}

std::vector<SDL_FRect> TextBlock::boxes(std::size_t begin, std::size_t end) {
    std::vector<SDL_FRect> out;
    if (!font_) return out;
    layout();
    for (const Line& line : lines_)
        for (const Piece& piece : line.pieces) { // (its part in each piece, where that piece is on screen: right to left ones from their right end)
            const std::size_t start = line.rawBegin + piece.begin, a = std::max(begin, start), b = std::min(end, line.rawBegin + piece.end);
            if (a >= b) continue;
            const Font& font = font_->variant(piece.style);
            const float into = font.measure(slice(start, a)), wide = font.measure(slice(a, b));
            out.push_back({piece.level % 2 == 1 ? piece.x + piece.width - into - wide : piece.x + into, line.y, wide, font_->lineHeight()});
        }
    return out;
}

float TextBlock::lineStep() const {
    return font_->lineHeight() * spacing_;
}

void TextBlock::drawIn(SDL_Renderer* renderer, const SDL_FRect& box, float align, float alignVertically, SDL_Color colour,
                       std::size_t reveal, const TextShadow& shadow) {
    if (!font_ || text_.empty() || colour.a == 0) return;
    layout();
    const float y = box.y + (box.h > 0.0f ? (box.h - size_.y) * alignVertically : 0.0f);
    drawLines(renderer, box.x, y, box.w > 0.0f ? align : 0.0f, box.w, colour, reveal, shadow);
}

void TextBlock::draw(SDL_Renderer* renderer, float x, float y, SDL_Color colour, std::size_t reveal, const TextShadow& shadow) {
    if (!font_ || text_.empty() || colour.a == 0) return;
    layout();
    drawLines(renderer, x, y, 0.0f, 0.0f, colour, reveal, shadow);
}

void TextBlock::drawLines(SDL_Renderer* renderer, float x0, float y0, float align, float width, SDL_Color colour,
                          std::size_t reveal, const TextShadow& shadow) {
    const float scale = font_->pixelScale();
    const float shadowOffset = shadow.offset > 0.0f ? shadow.offset : std::max(1.0f, std::round(font_->lineHeight() / 16.0f));
    auto paint = [&](SDL_Texture* tex, const SDL_FRect& src, const SDL_FRect& dst, SDL_Color c) {
        const float alpha = static_cast<float>(c.a) / 255.0f;
        if (shadow.on) {
            const SDL_FRect sdst{dst.x + shadowOffset, dst.y + shadowOffset, dst.w, dst.h};
            SDL_SetTextureColorMod(tex, shadow.colour.r, shadow.colour.g, shadow.colour.b);
            SDL_SetTextureAlphaModFloat(tex, alpha * static_cast<float>(shadow.colour.a) / 255.0f);
            SDL_RenderTexture(renderer, tex, &src, &sdst);
        }
        SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
        SDL_SetTextureAlphaModFloat(tex, alpha);
        SDL_RenderTexture(renderer, tex, &src, &dst);
    };

    for (auto& line : lines_) {
        if (reveal <= line.rawBegin && line.codepoints > 0) break;
        const std::size_t visible = std::min(line.codepoints, reveal - std::min(reveal, line.rawBegin));
        const float x = x0 + (width - line.width) * (line.rtl ? 1.0f - align : align), y = y0 + line.y; // "start" is the right for right to left
        for (auto& piece : line.pieces) {
            if (piece.begin >= visible) break; // (pieces are in reading order)
            const std::size_t count = piece.end - piece.begin, shown = std::min(piece.end, visible) - piece.begin;
            const SDL_Color c = piece.colour ? SDL_Color{piece.colour->r, piece.colour->g, piece.colour->b,
                                                        static_cast<Uint8>(piece.colour->a * colour.a / 255)}
                                            : colour;
            const Font& font = font_->variant(piece.style);
            const std::string_view text = slice(line.rawBegin + piece.begin, line.rawBegin + piece.end);
            if (font.isFallback()) {
                const auto run = text.substr(0, utf8::offset(text, shown));
                if (shadow.on) font.drawFallback(renderer, x + piece.x + shadowOffset, y + shadowOffset, run, withAlpha(shadow.colour, static_cast<float>(c.a) / 255.0f));
                font.drawFallback(renderer, x + piece.x, y, run, c);
                continue;
            }
            if (!piece.texture) piece.texture = font.render(renderer, text);
            if (!piece.texture) continue;
            float w = 0, h = 0;
            SDL_GetTextureSize(piece.texture.get(), &w, &h);
            if (shown < count && piece.advance.empty()) // being typed: how far each letter reaches
                for (std::size_t i = 1; i <= count; ++i) piece.advance.push_back(font.measure(text.substr(0, utf8::offset(text, i))));
            const float part = shown >= count ? w : std::min(w, piece.advance[shown - 1] * scale); // the letters typed so far
            if (piece.level % 2 == 1) // right to left: they're at its right end
                paint(piece.texture.get(), {w - part, 0, part, h}, {x + piece.x + (w - part) / scale, y, part / scale, h / scale}, c);
            else
                paint(piece.texture.get(), {0, 0, part, h}, {x + piece.x, y, part / scale, h / scale}, c);
        }
        // Furigana, once the word under them is all there.
        if (line.rubies.empty()) continue;
        const Font& small = font_->variant(0, 0.5f);
        for (auto& ruby : line.rubies) {
            if (ruby.end > visible || small.isFallback()) continue;
            if (!ruby.texture) ruby.texture = small.render(renderer, ruby.text);
            if (!ruby.texture) continue;
            float w = 0, h = 0;
            SDL_GetTextureSize(ruby.texture.get(), &w, &h);
            paint(ruby.texture.get(), {0, 0, w, h}, {x + ruby.centre - w / scale * 0.5f, y - small.lineHeight() * 0.85f, w / scale, h / scale}, colour);
        }
    }
}

void FontCache::configure(std::map<std::string, File, std::less<>> files, std::vector<std::string> systemFonts,
                          std::vector<std::string> fallbacks) {
    fonts_.clear();
    files_ = std::move(files);
    system_ = std::move(systemFonts);
    fallbacks_ = std::move(fallbacks);
}

Font& FontCache::get(const std::string& name, float size) {
    const auto key = std::pair{name, static_cast<int>(std::lround(size * 4.0f))};
    if (auto it = fonts_.find(key); it != fonts_.end()) return *it->second;

    std::vector<std::string> candidates;
    std::string style;
    if (auto it = files_.find(name); it != files_.end()) {
        candidates.push_back(it->second.path);
        style = it->second.style;
    }
    if (auto it = files_.find("default"); !name.empty() && name != "default" && it != files_.end()) { // ("": the system's own)
        candidates.push_back(it->second.path);
        if (style.empty()) style = it->second.style;
    }
    candidates.insert(candidates.end(), system_.begin(), system_.end());

    auto font = std::make_unique<Font>();
    font->setPixelScale(scale_);
    if (!font->load(candidates, size, style, fallbacks_, source_) && fonts_.empty())
        logWarn("There's no font file for '{}', so SDL's built-in font is used: import one in the Library's Fonts.", name);
    return *fonts_.emplace(key, std::move(font)).first->second;
}

void FontCache::setPixelScale(float scale) {
    scale_ = scale;
    for (auto& entry : fonts_) entry.second->setPixelScale(scale);
}

RichText richText(std::string_view source, const Lookup& lookup, const Palette* palette, const TagHandler& other) {
    RichText out;
    std::vector<std::pair<std::size_t, SDL_Color>> colours;              // open {colour} tags
    std::map<std::string, std::vector<std::size_t>, std::less<>> styles; // open {bold} {italic} {underline} {crossed_out}
    std::vector<std::pair<std::size_t, std::string>> rubies;             // open {furigana READING} tags
    std::vector<std::size_t> tips;                                       // open {tip NAME} tags
    // A tip's words: underlined, in the game's colour called "tip" (without one, the ready-made look's accent).
    auto tip = [&](std::size_t begin, std::size_t end) {
        out.spans.push_back({.begin = begin, .end = end, .colour = parseColour("tip", palette).value_or(SDL_Color{173, 162, 255, 255}),
                             .style = TTF_STYLE_UNDERLINE});
    };
    static const std::map<std::string, TTF_FontStyleFlags, std::less<>> kStyles{
        {"bold", TTF_STYLE_BOLD}, {"italic", TTF_STYLE_ITALIC}, {"underline", TTF_STYLE_UNDERLINE}, {"crossed_out", TTF_STYLE_STRIKETHROUGH}};
    out.text = expand(source, lookup, [&](std::string_view tag, std::string_view arg, std::size_t at) {
        if (tag == "colour") { // (a colour, or a value holding one: {colour chat.colour})
            auto colour = parseColour(arg, palette);
            if (!colour && !arg.empty()) colour = parseColour(toString(lookup(trim(arg))), palette);
            colours.emplace_back(at, colour.value_or(SDL_Color{255, 255, 255, 255}));
        } else if (tag == "/colour") {
            if (colours.empty()) return;
            out.spans.push_back({.begin = colours.back().first, .end = at, .colour = colours.back().second});
            colours.pop_back();
        } else if (kStyles.contains(tag)) {
            styles[std::string(tag)].push_back(at);
        } else if (tag.starts_with('/') && kStyles.contains(tag.substr(1))) {
            auto& open = styles[std::string(tag.substr(1))];
            if (open.empty()) return;
            out.spans.push_back({.begin = open.back(), .end = at, .style = kStyles.at(std::string(tag.substr(1)))});
            open.pop_back();
        } else if (tag == "furigana") {
            rubies.emplace_back(at, std::string(arg));
        } else if (tag == "/furigana") {
            if (rubies.empty()) return;
            out.spans.push_back({.begin = rubies.back().first, .end = at, .ruby = rubies.back().second});
            rubies.pop_back();
        } else if (tag == "tip") {
            tips.push_back(at);
        } else if (tag == "/tip") {
            if (tips.empty()) return;
            tip(tips.back(), at);
            tips.pop_back();
        } else if (other) {
            other(tag, arg, at);
        }
    });
    const std::size_t length = utf8::length(out.text); // unclosed tags run to the end
    for (const auto& [start, col] : colours) out.spans.push_back({.begin = start, .end = length, .colour = col});
    for (const auto& [tag, starts] : styles)
        for (auto start : starts) out.spans.push_back({.begin = start, .end = length, .style = kStyles.at(tag)});
    for (auto start : tips) tip(start, length);
    return out;
}

} // namespace vn
