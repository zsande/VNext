#include "Screens.hpp"
#include "Engine.hpp"

#include <SheenBidi/SheenBidi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <ranges>

namespace vn {

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

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

void Font::drawFallback(SDL_Renderer* renderer, float x, float y, std::string_view text, SDL_Color color) const {
    const float k = fallbackScale();
    float sx = 1, sy = 1;
    SDL_GetRenderScale(renderer, &sx, &sy);
    SDL_SetRenderScale(renderer, k, k);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    // Centre the 8px glyphs vertically inside the line.
    const float pad = (lineHeight() - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * k) * 0.5f;
    SDL_RenderDebugText(renderer, x / k, (y + pad) / k, std::string(text).c_str());
    SDL_SetRenderScale(renderer, sx, sy);
}

// ---------------------------------------------------------------------------
// TextBlock
// ---------------------------------------------------------------------------

void TextBlock::set(const Font& font, std::string_view text, float wrapWidth, float lineSpacing) {
    if (font_ == &font && text_ == text && wrap_ == wrapWidth && spacing_ == lineSpacing) return;
    font_ = &font;
    text_ = std::string(text);
    wrap_ = wrapWidth;
    spacing_ = lineSpacing;
    generation_ = std::numeric_limits<std::uint32_t>::max();
}

namespace {

bool sameColor(const std::optional<SDL_Color>& a, const std::optional<SDL_Color>& b) {
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
        return a.begin == b.begin && a.end == b.end && sameColor(a.color, b.color) && a.style == b.style && a.ruby == b.ruby;
    };
    if (std::ranges::equal(spans, spans_, same)) return; // laid out already
    spans_ = std::move(spans);
    generation_ = std::numeric_limits<std::uint32_t>::max();
}

SDL_FPoint TextBlock::size() {
    layout();
    return size_;
}

std::size_t TextBlock::length() {
    layout();
    return length_;
}

std::size_t TextBlock::visible(std::size_t reveal, Reveal mode) {
    layout();
    if (reveal == 0 || reveal >= length_ || mode == Reveal::Letter) return reveal;
    if (mode == Reveal::Line) {
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
    colors_.assign(length_, std::nullopt);
    for (const auto& span : spans_)
        for (std::size_t i = span.begin; i < std::min(span.end, length_); ++i) {
            styles_[i] |= span.style;
            if (span.color) colors_[i] = span.color;
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
        while (b < to && styles_[b] == styles_[a] && sameColor(colors_[b], colors_[a]) && level[b - from] == level[a - from]) ++b;
        Piece piece{.begin = a - from, .end = b - from, .style = styles_[a], .color = colors_[a], .level = level[a - from]};
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
                                   .center = measure(from, span.begin) + measure(span.begin, span.end) * 0.5f});
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

void TextBlock::drawIn(SDL_Renderer* renderer, const SDL_FRect& box, float align, float valign, SDL_Color color,
                       std::size_t reveal, const TextShadow& shadow) {
    if (!font_ || text_.empty() || color.a == 0) return;
    layout();
    const float y = box.y + (box.h > 0.0f ? (box.h - size_.y) * valign : 0.0f);
    drawLines(renderer, box.x, y, box.w > 0.0f ? align : 0.0f, box.w, color, reveal, shadow);
}

void TextBlock::draw(SDL_Renderer* renderer, float x, float y, SDL_Color color, std::size_t reveal, const TextShadow& shadow) {
    if (!font_ || text_.empty() || color.a == 0) return;
    layout();
    drawLines(renderer, x, y, 0.0f, 0.0f, color, reveal, shadow);
}

void TextBlock::drawLines(SDL_Renderer* renderer, float x0, float y0, float align, float width, SDL_Color color,
                          std::size_t reveal, const TextShadow& shadow) {
    const float scale = font_->pixelScale();
    const float shadowOffset = shadow.offset > 0.0f ? shadow.offset : std::max(1.0f, std::round(font_->lineHeight() / 16.0f));
    auto paint = [&](SDL_Texture* tex, const SDL_FRect& src, const SDL_FRect& dst, SDL_Color c) {
        const float alpha = static_cast<float>(c.a) / 255.0f;
        if (shadow.on) {
            const SDL_FRect sdst{dst.x + shadowOffset, dst.y + shadowOffset, dst.w, dst.h};
            SDL_SetTextureColorMod(tex, shadow.color.r, shadow.color.g, shadow.color.b);
            SDL_SetTextureAlphaModFloat(tex, alpha * static_cast<float>(shadow.color.a) / 255.0f);
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
            const SDL_Color c = piece.color ? SDL_Color{piece.color->r, piece.color->g, piece.color->b,
                                                        static_cast<Uint8>(piece.color->a * color.a / 255)}
                                            : color;
            const Font& font = font_->variant(piece.style);
            const std::string_view text = slice(line.rawBegin + piece.begin, line.rawBegin + piece.end);
            if (font.isFallback()) {
                const auto run = text.substr(0, utf8::offset(text, shown));
                if (shadow.on) font.drawFallback(renderer, x + piece.x + shadowOffset, y + shadowOffset, run, withAlpha(shadow.color, static_cast<float>(c.a) / 255.0f));
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
            paint(ruby.texture.get(), {0, 0, w, h}, {x + ruby.center - w / scale * 0.5f, y - small.lineHeight() * 0.85f, w / scale, h / scale}, color);
        }
    }
}

// ---------------------------------------------------------------------------
// FontCache
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

namespace draw {

namespace {
SDL_FColor toF(SDL_Color c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}
} // namespace

void rect(SDL_Renderer* r, const SDL_FRect& rect, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}

void outline(SDL_Renderer* r, const SDL_FRect& rect, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderRect(r, &rect);
}

void gradient(SDL_Renderer* r, const SDL_FRect& rc, SDL_Color top, SDL_Color bottom) {
    const SDL_FColor t = toF(top), b = toF(bottom);
    const std::array<SDL_Vertex, 4> v{{
        {{rc.x, rc.y}, t, {}},
        {{rc.x + rc.w, rc.y}, t, {}},
        {{rc.x + rc.w, rc.y + rc.h}, b, {}},
        {{rc.x, rc.y + rc.h}, b, {}},
    }};
    static constexpr std::array<int, 6> idx{0, 1, 2, 2, 3, 0};
    SDL_RenderGeometry(r, nullptr, v.data(), 4, idx.data(), 6);
}

void circle(SDL_Renderer* r, float cx, float cy, float radius, SDL_Color c) {
    constexpr std::size_t segments = 48;
    const SDL_FColor fc = toF(c);
    std::array<SDL_Vertex, segments + 1> v{};
    std::array<int, segments * 3> idx{};
    v[0] = {{cx, cy}, fc, {}};
    for (std::size_t i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * std::numbers::pi_v<float>;
        v[i + 1] = {{cx + std::cos(a) * radius, cy + std::sin(a) * radius}, fc, {}};
        idx[i * 3 + 0] = 0;
        idx[i * 3 + 1] = static_cast<int>(i + 1);
        idx[i * 3 + 2] = static_cast<int>((i + 1) % segments + 1);
    }
    SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

void quad(SDL_Renderer* r, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color color) {
    const SDL_FColor fc = toF(color);
    const std::array<SDL_Vertex, 4> v{{{a, fc, {}}, {b, fc, {}}, {c, fc, {}}, {d, fc, {}}}};
    static constexpr std::array<int, 6> idx{0, 1, 2, 2, 3, 0};
    SDL_RenderGeometry(r, nullptr, v.data(), 4, idx.data(), 6);
}

SDL_Color hsv(float h, float s, float v, Uint8 alpha) {
    h = std::fmod(h, 1.0f) * 6.0f;
    const int i = static_cast<int>(h);
    const float f = h - static_cast<float>(i);
    const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float rr = v, gg = t, bb = p;
    switch (i % 6) {
    case 1: rr = q; gg = v; bb = p; break;
    case 2: rr = p; gg = v; bb = t; break;
    case 3: rr = p; gg = q; bb = v; break;
    case 4: rr = t; gg = p; bb = v; break;
    case 5: rr = v; gg = p; bb = q; break;
    default: break;
    }
    auto u8 = [](float x) { return static_cast<Uint8>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); };
    return {u8(rr), u8(gg), u8(bb), alpha};
}

} // namespace draw

void Wiper::draw(SDL_Renderer* r, Assets& assets, SDL_FPoint canvas, const SDL_FRect& box, float shown, const std::string& shape,
                 const std::function<void()>& paint) {
    if (shown >= 0.999f) return paint();
    if (shown <= 0.0f || box.w <= 0.0f || box.h <= 0.0f) return;
    int outW = 0, outH = 0;
    SDL_GetCurrentRenderOutputSize(r, &outW, &outH);
    const float k = std::clamp(static_cast<float>(outW) / canvas.x, 1.0f, 4.0f); // (the layer: as sharp as where it goes)
    const int w = static_cast<int>(std::ceil(canvas.x * k)), h = static_cast<int>(std::ceil(canvas.y * k));
    if (Mask* mask = shape.empty() ? nullptr : maskOf(r, assets, shape)) {
        if (!layer_ || layerW_ != w || layerH_ != h) {
            layer_.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h));
            layerW_ = w, layerH_ = h;
            if (layer_) SDL_SetTextureBlendMode(layer_.get(), SDL_BLENDMODE_BLEND);
        }
        if (layer_) {
            if (mask->shown != shown) { // how far in each pixel of the shape is: it shows once the wipe passes its brightness, softly over `soft`
                constexpr float soft = 0.15f;
                std::vector<SDL_Color> pixels(mask->brightness.size());
                for (std::size_t i = 0; i < pixels.size(); ++i) {
                    const float a = std::clamp((shown * (1.0f + soft) - static_cast<float>(mask->brightness[i]) / 255.0f) / soft, 0.0f, 1.0f);
                    pixels[i] = {255, 255, 255, static_cast<Uint8>(a * 255.0f)};
                }
                SDL_UpdateTexture(mask->texture.get(), nullptr, pixels.data(), mask->w * 4);
                mask->shown = shown;
            }
            { // (only the box: emptied, drawn in, and cut as the mask has it; outside it nothing of it shows)
                const RenderTarget _(r, layer_.get(), k);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
                SDL_RenderFillRect(r, &box);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                {
                    const RenderClip inside(r, box);
                    paint();
                }
                SDL_RenderTexture(r, mask->texture.get(), nullptr, &box);
            }
            const SDL_FRect from{box.x * k, box.y * k, box.w * k, box.h * k};
            SDL_RenderTexture(r, layer_.get(), &from, &box);
            return;
        }
    }
    const RenderClip _(r, {box.x, box.y, box.w * shown, box.h}); // (straight, from the left)
    paint();
}

Wiper::Mask* Wiper::maskOf(SDL_Renderer* r, Assets& assets, const std::string& shape) {
    if (const auto it = masks_.find(shape); it != masks_.end()) return it->second.texture ? &it->second : nullptr;
    if (masks_.size() >= 8) masks_.clear(); // (the few in use are read again)
    Mask& m = masks_[shape];
    const SurfacePtr pixels = assets.pixels(shape);
    if (!pixels) {
        logWarn("There's no image called '{}' to wipe with, so the wipe is a straight one.", shape);
        return nullptr;
    }
    const int step = std::max(1, (pixels->w + 479) / 480);
    m.w = pixels->w / step;
    m.h = pixels->h / step;
    m.brightness.reserve(static_cast<std::size_t>(m.w) * static_cast<std::size_t>(m.h));
    for (int y = 0; y < m.h; ++y) {
        const auto* row = reinterpret_cast<const SDL_Color*>(static_cast<const Uint8*>(pixels->pixels) + static_cast<std::ptrdiff_t>(y * step) * pixels->pitch);
        for (int x = 0; x < m.w; ++x) {
            const SDL_Color c = row[x * step];
            m.brightness.push_back(static_cast<Uint8>((c.r * 299 + c.g * 587 + c.b * 114) / 1000));
        }
    }
    m.texture.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, m.w, m.h));
    // (laid over what's drawn: keeps its colours, and as much of it as the mask is opaque)
    static const SDL_BlendMode cut = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
                                                                SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    if (m.texture && !SDL_SetTextureBlendMode(m.texture.get(), cut)) m.texture.reset();
    return m.texture ? &m : nullptr;
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

namespace {

float factor(Align a) {
    return a == Align::Start ? 0.0f : a == Align::Center ? 0.5f : 1.0f;
}

bool isInput(const SDL_Event& e) {
    if (e.type == gestureEventType()) return true;
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_TEXT_INPUT:
        return true;
    default:
        return false;
    }
}

std::string ellipsize(const Font& font, const std::string& text, float width) {
    if (font.measure(text) <= width) return text;
    for (std::size_t n = utf8::length(text); n > 0; --n) {
        std::string cut = text.substr(0, utf8::offset(text, n - 1)) + "...";
        if (font.measure(cut) <= width) return cut;
    }
    return "...";
}

std::string formatTime(SDL_Time time) {
    SDL_DateTime dt{};
    if (!SDL_TimeToDateTime(time, &dt, true)) return {};
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}", dt.year, dt.month, dt.day, dt.hour, dt.minute);
}

SDL_FPoint center(const SDL_FRect& r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }

bool inside(const SDL_FRect& r, const SDL_FRect& area) {
    return r.x >= area.x && r.y >= area.y && r.x + r.w <= area.x + area.w + 1 && r.y + r.h <= area.y + area.h + 1;
}

// Puts what was drawn aside (`aside`, at `scale` pixels per one of the game's) on the screen at `box`,
// see-through towards its left and right edges over `fadeX` of its width (0: not), and towards its top
// and bottom over `fadeY` of its height.
void fadedEdges(SDL_Renderer* r, SDL_Texture* aside, const SDL_FRect& box, float scale, float fadeX, float fadeY) {
    float tw = 0.0f, th = 0.0f;
    SDL_GetTextureSize(aside, &tw, &th);
    if (tw <= 0.0f || th <= 0.0f) return;
    const float xs[4]{box.x, box.x + fadeX, box.x + box.w - fadeX, box.x + box.w}, ys[4]{box.y, box.y + fadeY, box.y + box.h - fadeY, box.y + box.h};
    const float edge[4]{0.0f, 1.0f, 1.0f, 0.0f};
    SDL_Vertex v[16];
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            SDL_Vertex& at = v[row * 4 + col];
            at.position = {xs[col], ys[row]};
            at.color = {1.0f, 1.0f, 1.0f, (fadeX > 0.0f ? edge[col] : 1.0f) * (fadeY > 0.0f ? edge[row] : 1.0f)};
            at.tex_coord = {at.position.x * scale / tw, at.position.y * scale / th};
        }
    int indices[54];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            const int a = row * 4 + col;
            const int quad[6]{a, a + 1, a + 4, a + 1, a + 5, a + 4};
            std::copy_n(quad, 6, indices + (row * 3 + col) * 6);
        }
    SDL_RenderGeometry(r, aside, v, 16, indices, 54);
}

bool overlaps(const SDL_FRect& r, const SDL_FRect& area) {
    return r.x + r.w >= area.x && r.x <= area.x + area.w && r.y + r.h >= area.y && r.y <= area.y + area.h;
}

} // namespace

ScreenView::Grid ScreenView::grid(const Element& el, Grid d) const {
    Grid g{.columns = std::max(1, static_cast<int>(el.number("columns", static_cast<float>(d.columns))))};
    const float gap = el.number("gap", std::numeric_limits<float>::quiet_NaN());
    const SDL_FRect box = boxOf(static_cast<std::size_t>(&el - def_->elements.data()));
    g.gapX = share(el, "gap_x", true, std::isnan(gap) ? d.gapX : gap * box.w);
    g.gapY = share(el, "gap_y", false, std::isnan(gap) ? d.gapY : gap * box.h);
    const auto columns = static_cast<float>(g.columns);
    g.itemW = share(el, "item_w", true, d.itemW > 0 ? d.itemW : (rectOf(indexOf(el)).w - g.gapX * (columns - 1)) / columns);
    g.itemH = share(el, "item_h", false, d.itemH);
    return g;
}

float ScreenView::share(const Element& el, std::string_view key, bool across, float fallback) const {
    const float v = el.number(key, std::numeric_limits<float>::quiet_NaN());
    if (std::isnan(v)) return fallback;
    const SDL_FRect box = boxOf(static_cast<std::size_t>(&el - def_->elements.data()));
    return v * (across ? box.w : box.h);
}

SDL_FRect ScreenView::boxOf(std::size_t index) const {
    const int p = index < def_->elements.size() ? def_->elements[index].parent : -1;
    return p >= 0 && static_cast<std::size_t>(p) < index ? rectOf(static_cast<std::size_t>(p)) : SDL_FRect{0.0f, 0.0f, engine_.canvas().w, engine_.canvas().h};
}

void ScreenView::layout() {
    const auto& elements = def_->elements; // (the parts they're in come first)
    auto place = [&](std::size_t i) {
        const Element& el = elements[i];
        const ElementState& st = state_[i];
        const SDL_FRect box = boxOf(i);
        // (a container without a size: all of its box; with fit=parts, as big as its parts, once they're placed)
        const bool container = el.kind == ElementKind::Container, parts = container && el.fitsParts();
        const float w = el.shareW ? *el.shareW * box.w : parts && st.ownW > 0.0f ? st.ownW : container ? box.w : 0.0f;
        const float h = el.shareH ? *el.shareH * box.h : parts && st.ownH > 0.0f ? st.ownH : container ? box.h : 0.0f;
        rectOf(i) = {std::round(box.x + placeEdge(el.placeX.value_or(-1.0f), w, box.w)), std::round(box.y + placeEdge(-el.placeY.value_or(1.0f), h, box.h)),
                   std::round(w), std::round(h)}; // (on whole pixels: sharp)
        sizeOwn(i);
    };
    for (int pass = 0; pass < 4; ++pass) {
        for (std::size_t i = 0; i < elements.size(); ++i) place(i);
        // A container with fit=parts and no width (or height): as wide (high) as its biggest part that way
        // (one with a size of its own: not a share of the box, nor a container filling it); the parts are
        // placed within that by their places. News: placed again.
        bool changed = false;
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const Element& el = elements[i];
            if (el.kind != ElementKind::Container || !el.fitsParts() || (el.shareW && el.shareH)) continue;
            float w = 0.0f, h = 0.0f;
            for (std::size_t j = i + 1; j < elements.size(); ++j) {
                if (!within(j, i)) continue;
                const Element& part = elements[j];
                const bool fills = part.kind == ElementKind::Container && !part.fitsParts();
                if (!part.shareW && !fills) w = std::max(w, rectOf(j).w);
                if (!part.shareH && !fills) h = std::max(h, rectOf(j).h);
            }
            ElementState& st = state_[i];
            if (std::abs(w - st.ownW) > 0.5f || std::abs(h - st.ownH) > 0.5f) st.ownW = w, st.ownH = h, changed = true;
        }
        if (!changed) break;
    }
    // A container with scroll=yes: what's in it moves up (and left) by how far it's scrolled, as far as its
    // parts reach past its box (the parts in parts in it too: the containers in it come first, scrolled by it).
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const Element& box = elements[i];
        if (box.kind != ElementKind::Container || !box.scrolls()) continue;
        ElementState& st = state_[i];
        st.reach = st.reachX = 0.0f;
        for (std::size_t j = i + 1; j < elements.size(); ++j)
            if (within(j, i)) {
                const SDL_FRect& b = rectOf(j); // (as laid out: a part without a size of its own takes none)
                st.reach = std::max(st.reach, b.y + b.h - rectOf(i).y);
                st.reachX = std::max(st.reachX, b.x + b.w - rectOf(i).x);
            }
        glideStep(i); // (gliding on after a drag: until it's slow, or at an end)
        const float wantY = st.scroll, wantX = st.scrollX;
        st.scroll = std::clamp(st.scroll, 0.0f, std::max(0.0f, st.reach - rectOf(i).h));
        st.scrollX = std::clamp(st.scrollX, 0.0f, std::max(0.0f, st.reachX - rectOf(i).w));
        if (st.scroll != wantY) st.glideY = 0.0f;
        if (st.scrollX != wantX) st.glideX = 0.0f;
        if (st.scroll > 0.0f || st.scrollX > 0.0f)
            for (std::size_t j = i + 1; j < elements.size(); ++j)
                if (within(j, i)) rectOf(j).y -= st.scroll, rectOf(j).x -= st.scrollX;
    }
}

std::optional<std::size_t> ScreenView::scrollableAt(float x, float y) {
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < def_->elements.size(); ++i) {
        const Element& el = def_->elements[i];
        if (el.dragDirection() != 0.0f && live(i) && pointInRect(x, y, rectOf(i)) && (!found || within(i, *found))) found = i;
    }
    return found;
}

void ScreenView::glideStep(std::size_t index) {
    ElementState& st = state_[index];
    if (st.glideX == 0.0f && st.glideY == 0.0f) return;
    const Element& el = def_->elements[index];
    const float t = now(), dt = std::clamp(t - st.glideAt, 0.0f, 0.1f);
    st.glideAt = t;
    st.scroll += st.glideY * dt;
    (el.kind == ElementKind::Chart ? st.panX : st.scrollX) += st.glideX * dt;
    const float keep = std::exp(-dt * 2.5f / std::max(0.05f, el.number("glide_distance", engine_.config().glideDistance))); // (glide_distance=200%: slowing half as fast, twice as far; none: the game's)
    st.glideX = std::abs(st.glideX) < 10.0f ? 0.0f : st.glideX * keep;
    st.glideY = std::abs(st.glideY) < 10.0f ? 0.0f : st.glideY * keep;
}

void ScreenView::settle(std::size_t index, float most) {
    ElementState& st = state_[index];
    glideStep(index);
    st.reach = rectOf(index).h + std::max(0.0f, most);
    const float want = st.scroll;
    st.scroll = std::clamp(st.scroll, 0.0f, std::max(0.0f, most));
    if (st.scroll != want) st.glideY = 0.0f;
}

SDL_FRect ScreenView::barOf(std::size_t index, bool across) const {
    const Element& el = def_->elements[index];
    const SDL_FRect& box = rectOf(index);
    const ElementState& st = state_[index];
    const float t = std::max(1.0f, el.number("scrollbar_size", engine_.config().scrollbarSize)), in = t * 0.5f; // (its thickness, set in from the edges by half of it)
    const bool down = st.reach > box.h, side = st.reachX > box.w; // (both: each stops short of the other)
    if (across) return {box.x + in, box.y + box.h - t - in, std::max(0.0f, box.w - t - (down ? t + in : 0.0f)), t};
    return {box.x + box.w - t - in, box.y + in, t, std::max(0.0f, box.h - t - (side ? t + in : 0.0f))};
}

SDL_FRect ScreenView::thumbOf(std::size_t index, bool across) const {
    const SDL_FRect bar = barOf(index, across), box = rectOf(index);
    const ElementState& st = state_[index];
    if (across) {
        if (st.reachX <= box.w || box.w <= 0.0f) return bar;
        const float w = std::max(20.0f, bar.w * box.w / st.reachX);
        return {bar.x + (bar.w - w) * st.scrollX / (st.reachX - box.w), bar.y, w, bar.h};
    }
    if (st.reach <= box.h || box.h <= 0.0f) return bar;
    const float h = std::max(20.0f, bar.h * box.h / st.reach);
    return {bar.x, bar.y + (bar.h - h) * st.scroll / (st.reach - box.h), bar.w, h};
}

// A container's scrollbars (scrollbar=yes), each way its parts reach past it: a track in its border
// colour (none: white), faint, with the thumb in its text colour.
void ScreenView::drawScrollbar(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    if (lower(el.option("scrollbar", "no")) != "yes") return;
    const ElementState& st = state_[index];
    const Style& s = styleOf(index);
    for (const bool across : {false, true}) {
        if (across ? st.reachX <= rectOf(index).w : st.reach <= rectOf(index).h) continue;
        draw::rect(r, barOf(index, across), withAlpha(s.border.a > 0 ? s.border : SDL_Color{255, 255, 255, 255}, 0.25f * alpha));
        draw::rect(r, thumbOf(index, across), withAlpha(s.color, 0.75f * alpha));
    }
}

void ScreenView::reveal(const Target& t) {
    if (t.spot.element >= def_->elements.size()) return;
    for (int p = def_->elements[t.spot.element].parent; p >= 0; p = def_->elements[static_cast<std::size_t>(p)].parent) {
        const auto at = static_cast<std::size_t>(p);
        const Element& box = def_->elements[at];
        if (box.kind != ElementKind::Container || !box.scrolls()) continue;
        if (t.rect.y < rectOf(at).y) state_[at].scroll -= rectOf(at).y - t.rect.y;
        else if (t.rect.y + t.rect.h > rectOf(at).y + rectOf(at).h) state_[at].scroll += t.rect.y + t.rect.h - (rectOf(at).y + rectOf(at).h);
        if (t.rect.x < rectOf(at).x) state_[at].scrollX -= rectOf(at).x - t.rect.x;
        else if (t.rect.x + t.rect.w > rectOf(at).x + rectOf(at).w) state_[at].scrollX += t.rect.x + t.rect.w - (rectOf(at).x + rectOf(at).w);
    }
}

bool ScreenView::within(std::size_t element, std::size_t container) const {
    for (int p = def_->elements[element].parent; p >= 0; p = def_->elements[static_cast<std::size_t>(p)].parent)
        if (static_cast<std::size_t>(p) == container) return true;
    return false;
}

std::optional<SDL_FRect> ScreenView::cutBy(std::size_t index, const std::vector<Shift>* shifts) const {
    std::optional<SDL_FRect> out;
    for (int p = def_->elements[index].parent; p >= 0; p = def_->elements[static_cast<std::size_t>(p)].parent) {
        const auto at = static_cast<std::size_t>(p);
        const Element& box = def_->elements[at];
        if (box.kind != ElementKind::Container || box.edges() == "show") continue;
        SDL_FRect rect = rectOf(at);
        if (shifts && at < shifts->size()) rect.x += (*shifts)[at].x, rect.y += (*shifts)[at].y;
        if (!out) out = rect;
        else if (!SDL_GetRectIntersectionFloat(&*out, &rect, &*out)) out = SDL_FRect{};
    }
    return out;
}

bool ScreenView::startAside(SDL_Renderer* r, float& scale) {
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(r, &ow, &oh);
    scale = std::max(1.0f, std::min(static_cast<float>(ow) / engine_.canvas().w, static_cast<float>(oh) / engine_.canvas().h));
    const int w = static_cast<int>(std::ceil(engine_.canvas().w * scale)), h = static_cast<int>(std::ceil(engine_.canvas().h * scale));
    float tw = 0.0f, th = 0.0f;
    if (aside_) SDL_GetTextureSize(aside_.get(), &tw, &th);
    if (!aside_ || static_cast<int>(tw) != w || static_cast<int>(th) != h) {
        aside_.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h));
        if (!aside_) return false;
        SDL_SetTextureBlendMode(aside_.get(), SDL_BLENDMODE_BLEND); // (premultiplied: drawn as geometry, its alpha is ignored)
        SDL_SetTextureScaleMode(aside_.get(), SDL_SCALEMODE_LINEAR);
    }
    return true;
}

// A button without a width (or a height) is as big as the picture it shows now, or (with none) its
// words and its padding, and placed by that size. It's clicked within its usual size as well, so a
// smaller pointed-at or pressed picture can't let the pointer slip out of it.
// A part without a width (or a height) is as big as what it shows, and placed by that size: a button's
// picture or words (sizeButton), a picture's pixels, a text's words with its padding.
void ScreenView::sizeOwn(std::size_t index) {
    const Element& el = def_->elements[index];
    if (el.kind == ElementKind::Button) return sizeButton(index);
    ElementState& st = state_[index];
    st.ownWords = false;
    if ((el.shareW && el.shareH) || (el.kind != ElementKind::Image && el.kind != ElementKind::Text)) return;
    SDL_FPoint size{0.0f, 0.0f};
    if (el.kind == ElementKind::Image) {
        const std::string name = el.text.contains('{') ? interpolate(el.text) : el.text;
        if (!name.empty() && !parseColor(name, &engine_.palette()) && !parseGradient(name, &engine_.palette()))
            if (const Picture pic = engine_.picture(name)) size = {pic.w(), pic.h()};
    } else {
        st.text.set(styleFont(styleOf(index)), rich(el.text).text, 0.0f, styleOf(index).lineSpacing);
        st.ownWords = !el.shareW;
        const SDL_FPoint words = st.text.size();
        if (words.x > 0.0f || words.y > 0.0f) size = {words.x + 2 * styleOf(index).padding, words.y + 2 * styleOf(index).padding};
    }
    if (size.x <= 0.0f && size.y <= 0.0f) return;
    const SDL_FRect box = boxOf(index);
    if (!el.shareW) rectOf(index).w = std::round(size.x), rectOf(index).x = std::round(box.x + placeEdge(el.placeX.value_or(-1.0f), rectOf(index).w, box.w));
    if (!el.shareH) rectOf(index).h = std::round(size.y), rectOf(index).y = std::round(box.y + placeEdge(-el.placeY.value_or(1.0f), rectOf(index).h, box.h));
}

void ScreenView::sizeButton(std::size_t index) {
    const Element& el = def_->elements[index];
    ElementState& st = state_[index];
    st.ownWords = false;
    if (el.shareW && el.shareH) {
        st.hit.reset();
        return;
    }
    const ButtonLook now = buttonLook(index, st.hit.value_or(rectOf(index)));
    auto sizeOf = [&](Look look, bool chosen) -> SDL_FPoint {
        if (const std::string image = lookImage(styleOf(index), look, chosen); !image.empty())
            if (const Picture pic = engine_.picture(image)) {
                st.ownWords = false;
                return {pic.w(), pic.h()};
            }
        st.text.set(styleFont(styleOf(index)), rich(el.text).text, 0.0f, styleOf(index).lineSpacing);
        st.ownWords = !el.shareW;
        const SDL_FPoint words = st.text.size();
        return {words.x + 2 * styleOf(index).padding, words.y + 2 * styleOf(index).padding};
    };
    const SDL_FRect box = boxOf(index);
    auto placed = [&](SDL_FPoint size) {
        SDL_FRect at = rectOf(index);
        if (!el.shareW) at.w = std::round(size.x), at.x = std::round(box.x + placeEdge(el.placeX.value_or(-1.0f), at.w, box.w));
        if (!el.shareH) at.h = std::round(size.y), at.y = std::round(box.y + placeEdge(-el.placeY.value_or(1.0f), at.h, box.h));
        return at;
    };
    const SDL_FRect usual = placed(sizeOf(Look::Idle, false)); // (the current look's last: ownWords is its)
    rectOf(index) = now.look == Look::Idle ? usual : placed(sizeOf(now.look, now.chosen));
    SDL_FRect hit{};
    SDL_GetRectUnionFloat(&rectOf(index), &usual, &hit);
    st.hit = hit;
}

ScreenView::ButtonLook ScreenView::buttonLook(std::size_t index, const SDL_FRect& hit) {
    const Element& el = def_->elements[index];
    const bool on = enabled(el);
    const bool pressed = (pressing_ == index && pointInRect(mouse_.x, mouse_.y, hit)) || playing_ == index; // (it looks chosen)
    const bool chosen = selected(el) || held_ == index;
    const Look look = !on ? Look::Disabled : pressed ? Look::Selected : focused(index, 0) ? Look::Hover : chosen ? Look::Selected : Look::Idle;
    return {look, on, chosen};
}

SDL_FRect ScreenView::Grid::cell(const SDL_FRect& area, std::size_t i, float scroll) const {
    const auto col = static_cast<float>(i % static_cast<std::size_t>(columns)), row = static_cast<float>(i / static_cast<std::size_t>(columns));
    return {area.x + col * (itemW + gapX), area.y + row * (itemH + gapY) - scroll, itemW, itemH};
}

float ScreenView::Grid::height(std::size_t items) const {
    const auto rows = static_cast<float>((items + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns));
    return std::max(0.0f, rows * (itemH + gapY) - gapY);
}

ScreenView::ScreenView(const ScreenDef& def, Engine& engine, bool tools) : def_(&def), engine_(engine), tools_(tools), state_(def.elements.size()) {
    // In a textbox, the line's own names are about the line it shows (several can show at once).
    lookup_ = [this](std::string_view name) -> Value {
        if (name == "speaker" || name == "has_face" || name == "face_side" || name == "text_complete")
            if (const auto line = engine_.game().line(def_->name)) {
                if (name == "speaker") return std::string(line->speaker);
                if (name == "has_face") return line->faceBase.empty() ? 0 : 1;
                if (name == "face_side") return std::string(line->faceSide);
                return line->complete ? 1 : 0;
            }
        return engine_.lookup()(name);
    };
}

void ScreenView::reset() {
    for (auto& st : state_) {
        st.scroll = 0.0f;
        st.since.reset(); // (animations start again)
        st.clicked.reset();
        st.hover = 0.0f;
        st.hoverAt.reset();
        st.movie.reset(); // videos start again
        st.movieName.clear();
        st.panX = 0.0f;
        st.centered = false;
    }
    opening_.reset(); // (its own animations again, until the one opening it says otherwise)
    closing_.reset();
    chosen_.clear();
    focus_.reset();
    dragging_.reset();
    playing_.reset();
    held_.reset();
    pressing_.reset();
    scrollDrag_.reset();
    tapping_.reset();
    mouse_ = {-1.0f, -1.0f};
    opened();
}

float placeEdge(float place, float size, float screen) {
    const float room = screen - size;
    if (place < -1.0f) return (place + 1.0f) * size;      // (off the left: at -2, just off)
    if (place > 1.0f) return room + (place - 1.0f) * size; // (off the right)
    return room * (place + 1.0f) / 2.0f;
}

float ScreenView::now() const {
    return tools_ ? engine_.toolsTime() : engine_.time();
}

ScreenView::Played ScreenView::animationIn(const Script* script, const std::optional<std::string>& name) {
    if (!name) return std::nullopt;
    if (!script) return nullptr;
    const auto it = script->animations.find(*name);
    return it != script->animations.end() ? std::make_shared<const Animation>(it->second) : nullptr;
}

bool ScreenView::still() const { return !tools_ && !engine_.settings().animations; }

float ScreenView::played(const Animation& a, float seconds) const {
    if (!still()) return seconds;
    return a.loops() ? 0.0f : a.total();
}

void ScreenView::opened() {
    shownSince_ = now();
    leftSince_.reset();
    for (ElementState& st : state_) st.chosen = false; // (shown again: what chosen buttons set holds again)
}

void ScreenView::leave() {
    leftSince_ = now();
    pressing_.reset();
}

bool ScreenView::leaving() const {
    return leftSince_ && hideAnimation() && !still() && now() - *leftSince_ < hideAnimation()->total();
}

void ScreenView::playWith(Played opening, Played closing) {
    opening_ = std::move(opening);
    closing_ = std::move(closing);
}

bool ScreenView::startsCovered() const {
    const Animation* a = showAnimation();
    return a && a->at(0.0f, {255, 255, 255, 255}, [](bool, float) { return 0.0f; }, {}).fade >= 0.999f;
}

bool ScreenView::endsCovered() const {
    const Animation* a = hideAnimation();
    return a && a->at(a->loops() ? a->length * 0.999f : a->total(), {255, 255, 255, 255}, [](bool, float) { return 0.0f; }, {}).fade >= 0.999f;
}

bool ScreenView::pointedAt(std::size_t index) const {
    if (leftSince_) return false;
    if (focus_ && focus_->element == index) return true; // (a part that takes clicks: pointed at, or the keys' or a controller's)
    return seenMotion_ == engine_.pointerMoves() && SDL_PointInRectFloat(&mouse_, &rectOf(index));
}

void ScreenView::hoverOnward(std::size_t index) {
    const Animation* a = def_->elements[index].hoverAnimation.get();
    ElementState& st = state_[index];
    if (!a) return;
    const float t = now(), dt = st.hoverAt ? std::clamp(t - *st.hoverAt, 0.0f, 0.25f) : 0.0f;
    st.hoverAt = t;
    if (still()) { // (as it ends while it's pointed at; else as it starts)
        st.hover = pointedAt(index) ? std::max(played(*a, 0.0f), 0.0001f) : 0.0f;
        return;
    }
    if (pointedAt(index)) {
        st.hover = std::max(st.hover + dt, 0.0001f); // (it starts at once)
        if (!a->loops()) st.hover = std::min(st.hover, a->total());
        else if (a->length > 0.0f && st.hover >= 2.0f * a->length) st.hover = a->length + std::fmod(st.hover - a->length, a->length);
        return;
    }
    // Back the way it came, from where it is in the time through it's in (one played to its end: from its end).
    if (a->length > 0.0f && st.hover > a->length) st.hover = !a->loops() && st.hover >= a->total() ? a->length : std::fmod(st.hover, a->length);
    st.hover = std::max(0.0f, st.hover - dt);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool ScreenView::visible(const Element& el) {
    return !el.visible || el.visible->test(lookup_);
}

bool ScreenView::live(std::size_t index) {
    const Element& el = def_->elements[index];
    return visible(el) && (el.parent < 0 || drawn(static_cast<std::size_t>(el.parent)));
}

bool ScreenView::enabled(const Element& el) {
    return !el.enabled || el.enabled->test(lookup_);
}

bool ScreenView::selected(const Element& el) {
    if (el.selected) return el.selected->test(lookup_);
    // A button that sets a variable or a player setting to a value is chosen while it has it.
    if (const auto* set = el.actions.size() == 1 ? std::get_if<cmd::Set>(&el.actions.front()) : nullptr; set && set->op == '=' && set->value)
        if (const auto value = set->value->constant()) {
            const Value now = lookup_(set->variable);
            const bool text = std::holds_alternative<std::string>(now) || std::holds_alternative<std::string>(*value);
            return text ? toString(now) == toString(*value) : toNumber(now) == toNumber(*value);
        }
    const auto* u = el.actions.size() == 1 ? std::get_if<cmd::Ui>(&el.actions.front()) : nullptr;
    if (!u || u->verb != "setting") return false;
    const auto space = u->arg.find(' ');
    if (space == std::string::npos) return false;
    const std::string_view name = std::string_view(u->arg).substr(0, space), value = std::string_view(u->arg).substr(space + 1);
    if (name == "language") { // (one by name: while it's the language on)
        const auto id = engine_.languageIdNamed(value);
        return id && *id == engine_.languageId(engine_.languageIndex());
    }
    const auto now = engine_.settings().get(name);
    return now && std::to_string(*now) == value;
}

float ScreenView::number(const Element& el, std::string_view key, float fallback) {
    if (const auto it = el.exprs.find(key); it != el.exprs.end())
        return static_cast<float>(toNumber(it->second.eval(lookup_)));
    return fallback;
}

std::string ScreenView::inputTarget(const Element& el) {
    const std::string var = el.option("var");
    return var.empty() ? engine_.game().request().variable : var;
}

std::string ScreenView::interpolate(std::string_view text) {
    return expand(text, lookup_);
}

RichText ScreenView::rich(std::string_view text) {
    if (text.find_first_of("{}") == std::string_view::npos) return {.text = std::string(text), .spans = {}}; // (nothing to fill in or style: as it is)
    if (const auto read = richRead_.find(text); read != richRead_.end()) return read->second;
    // (one that fills in a value is read every time: the value may have changed; one that only styles
    // its words, once)
    bool fills = false;
    RichText out = richText(text, [&](std::string_view name) {
        fills = true;
        return lookup_(name);
    }, &engine_.palette());
    if (!fills && richRead_.size() < 512) richRead_.emplace(std::string(text), out);
    return out;
}

Font& ScreenView::styleFont(const Style& s) {
    return engine_.font(s.font, s.textSize ? s.size * static_cast<float>(engine_.settings().textSize) / 100.0f : s.size);
}

float ScreenView::boxAlpha(const Style& s, float alpha) {
    if (engine_.settings().highContrast) return alpha; // (nothing see-through)
    const float textbox = s.textboxOpacity ? static_cast<float>(engine_.settings().textboxOpacity) / 100.0f : 1.0f;
    return alpha * s.opacity * textbox;
}

const Style::State* ScreenView::stateOf(const Style& s, Look look) {
    return look == Look::Hover ? &s.hover : look == Look::Selected ? &s.selected : look == Look::Disabled ? &s.disabled : nullptr;
}

// The picture a style shows in a look: the look's own, else (pointed at while chosen) its chosen one,
// else the style's; one named by a variable (image={door}) filled in.
std::string ScreenView::lookImage(const Style& s, Look look, bool chosen) {
    const Style::State* st = stateOf(s, look);
    std::string image = st && !st->image.empty()                              ? st->image
                        : look == Look::Hover && chosen && !s.selected.image.empty() ? s.selected.image
                                                                                     : s.image;
    if (image.contains('{')) image = interpolate(image);
    return image;
}

// High contrast (the player's setting): text in white (yellow where it's pointed at or chosen, grey
// where it can't be clicked) on black boxes with white edges, whatever the styles say.
constexpr SDL_Color kContrastText{255, 255, 255, 255}, kContrastLit{255, 220, 0, 255}, kContrastOff{170, 170, 170, 255};

SDL_Color ScreenView::textColor(const Style& s, Look look) const {
    if (engine_.settings().highContrast) return look == Look::Hover || look == Look::Selected ? kContrastLit : look == Look::Disabled ? kContrastOff : kContrastText;
    const Style::State* st = stateOf(s, look);
    return st && st->color ? *st->color : s.color;
}

void ScreenView::box(SDL_Renderer* r, const SDL_FRect& rect, const Style& s, Look look, float alpha) {
    const Style::State* st = stateOf(s, look);
    if (engine_.settings().highContrast) { // (a box with something in it: black, its edge white, or yellow when pointed at)
        if (s.bg.a == 0 && s.border.a == 0 && s.image.empty() && !(st && (st->bg || st->border || !st->image.empty()))) return;
        const bool lit = look == Look::Hover || look == Look::Selected;
        draw::rect(r, rect, withAlpha({0, 0, 0, 255}, alpha));
        draw::outline(r, rect, withAlpha(lit ? kContrastLit : look == Look::Disabled ? kContrastOff : kContrastText, alpha));
        if (lit) draw::outline(r, {rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2}, withAlpha(kContrastLit, alpha));
        return;
    }
    if (const std::string image = lookImage(s, look, false); !image.empty())
        if (const Picture pic = engine_.picture(image)) {
            pic.draw(r, rect, alpha);
            return;
        }
    const SDL_Color bg = st && st->bg ? *st->bg : s.bg;
    const std::optional<SDL_Color> bottom = st && st->bg ? st->bgBottom : s.bgBottom; // (bg=navy-black: fading down)
    const SDL_Color border = st && st->border ? *st->border : s.border;
    if (bottom) draw::gradient(r, rect, withAlpha(bg, alpha), withAlpha(*bottom, alpha));
    else if (bg.a > 0) draw::rect(r, rect, withAlpha(bg, alpha));
    if (border.a > 0) draw::outline(r, rect, withAlpha(border, alpha));
}

void ScreenView::text(SDL_Renderer* r, TextBlock& block, std::string_view str, const SDL_FRect& rect, const Style& s,
                      SDL_Color color, float alpha, std::size_t reveal, std::vector<TextSpan> spans) {
    Font& font = styleFont(s);
    const SDL_FRect inner{rect.x + s.padding, rect.y + s.padding, std::max(0.0f, rect.w - 2 * s.padding),
                          rect.h > 0 ? std::max(0.0f, rect.h - 2 * s.padding) : 0.0f};
    block.set(font, str, inner.w, s.lineSpacing);
    block.setSpans(std::move(spans));
    block.drawIn(r, inner, factor(s.align), factor(s.valign), withAlpha(color, alpha), reveal, s.shadow);
}

const ScreenView::Target* ScreenView::targetAt(float x, float y) const {
    for (const auto& t : targets_ | std::views::reverse)
        if (pointInRect(x, y, t.rect)) {
            const auto cut = t.spot.element < def_->elements.size() ? cutBy(t.spot.element) : std::nullopt; // (cut off past a container's edges: not there)
            if (!cut || pointInRect(x, y, *cut)) return &t;
        }
    return nullptr;
}

std::string ScreenView::pointedText() {
    const Target* t = focusedTarget();
    if (!t && mouse_.x >= 0.0f) t = targetAt(mouse_.x, mouse_.y);
    if (!t || t->spot.element >= def_->elements.size()) return {};
    const Element& el = def_->elements[t->spot.element];
    if (el.kind == ElementKind::Button) return rich(el.text).text;
    if (el.kind == ElementKind::Choices) {
        const auto& items = engine_.game().choices();
        const auto i = static_cast<std::size_t>(t->spot.item);
        return i < items.size() ? rich(items[i].text).text : std::string{};
    }
    return {};
}

const ScreenView::Target* ScreenView::focusedTarget() const {
    if (!focus_) return nullptr;
    const auto it = std::ranges::find(targets_, *focus_, &Target::spot);
    return it != targets_.end() ? &*it : nullptr;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

template <class Paint> void ScreenView::wiped(SDL_Renderer* r, const Shift& by, const Paint& paint) {
    if (by.wipe >= 0.999f) return paint(); // (a std::function made only for a wipe)
    partWiper_.draw(r, engine_.assets(), {engine_.canvas().w, engine_.canvas().h}, by.wipeBox, by.wipe, by.wipeShape, paint);
}

void ScreenView::draw(SDL_Renderer* r, float alpha) {
    drawnFrame_ = engine_.frameNumber();
    layout();
    targets_.clear();
    const std::size_t n = def_->elements.size();
    drawn_.assign(n, false);
    // A part in another shows only while that one does, and moves and fades with its animation; the
    // whole screen moves and fades with its show_animation= (or, closed, its hide_animation=).
    Shift whole{};
    if (const Animation* a = leftSince_ ? hideAnimation() : showAnimation(); a && (leftSince_ || shownSince_)) {
        float t = now() - (leftSince_ ? *leftSince_ : *shownSince_);
        if (leftSince_ && a->loops()) t = std::min(t, a->length * 0.999f); // (closing: once through, even with loop)
        const Canvas& size = engine_.canvas();
        const Animation::Look look = a->at(played(*a, t), {255, 255, 255, 255}, [&size](bool across, float place) { // (the whole screen: ±2 is just off it)
            return across ? placeEdge(place, size.w, size.w) : placeEdge(-place, size.h, size.h);
        }, {size.w, size.h});
        whole = {look.x, look.y, std::clamp(look.opacity, 0.0f, 1.0f), std::clamp(look.fade, 0.0f, 1.0f), look.fadeColor, look.wipe, {0.0f, 0.0f, size.w, size.h},
                 look.wipeShape};
    }
    std::vector<Shift> shifts(n);
    // What's in a container with edges=cut (or scroll=yes) is cut off at its edges; with edges=fade it
    // fades out towards the top and the bottom: its parts are drawn aside first, then put on the screen so.
    std::optional<std::size_t> fading; // the container whose parts are being drawn aside
    std::optional<RenderTarget> aside; // (meanwhile: drawing goes there, at `scale`)
    float scale = 1.0f;
    auto finishFade = [&] {
        if (!fading) return;
        const SDL_FRect& box = rectOf(*fading);
        const ElementState& st = state_[*fading];
        aside.reset(); // (drawing goes where it went before them, at its own scale)
        const float own = def_->elements[*fading].number("fade_size", engine_.config().fadeSize); // (fade_size=: pixels; none: a fifth of the box, at most 64)
        const float size = own > 0.0f ? own : std::min(std::min(box.w, box.h) * 0.2f, 64.0f); // (towards the edges its parts reach past: down, unless only across)
        fadedEdges(r, aside_.get(), {box.x + shifts[*fading].x, box.y + shifts[*fading].y, box.w, box.h}, scale, st.reachX > box.w ? size : 0.0f,
                   st.reach > box.h || st.reachX <= box.w ? size : 0.0f);
        fading.reset();
    };
    std::vector<std::size_t> open; // containers whose parts are being drawn: when they're done, the fade finishes and the scrollbar goes on
    auto closeTo = [&](std::size_t i) { // (down to the one `i` is in)
        while (!open.empty() && !(i < n && within(i, open.back()))) {
            const std::size_t box = open.back();
            open.pop_back();
            if (fading == box) finishFade();
            const SDL_FRect was = rectOf(box);
            rectOf(box).x += shifts[box].x;
            rectOf(box).y += shifts[box].y;
            drawScrollbar(r, box, alpha * shifts[box].opacity);
            rectOf(box) = was;
        }
    };
    // The parts, on the screen as its show (or hide) animation wipes it: all of them at once.
    const Shift onScreen{.x = whole.x, .y = whole.y, .opacity = whole.opacity}; // (what the parts on the screen itself are moved and faded by)
    auto parts = [&] {
        for (std::size_t i = 0; i < n; ++i) {
            closeTo(i);
            const Element& el = def_->elements[i];
            const bool in = el.parent >= 0 && static_cast<std::size_t>(el.parent) < i;
            if ((in && !drawn_[static_cast<std::size_t>(el.parent)]) || !visible(el)) {
                state_[i].since.reset(); // (it starts again when it shows again)
                continue;
            }
            const Shift by = in ? shifts[static_cast<std::size_t>(el.parent)] : onScreen;
            const SDL_FRect rect = rectOf(i);
            rectOf(i).x += by.x;
            rectOf(i).y += by.y;
            shifts[i] = by;
            ElementState& st = state_[i];
            if (st.clicked && el.clickAnimation && now() - *st.clicked >= el.clickAnimation->total()) st.clicked.reset();
            if (el.animation && !st.since) st.since = now();
            hoverOnward(i);
            std::optional<RenderClip> clip; // (in a container that cuts off its edges: only what's inside its box shows)
            if (const auto cut = cutBy(i, &shifts)) clip.emplace(r, *cut);
            if (el.animation || (el.hoverAnimation && st.hover > 0.0f) || (st.clicked && el.clickAnimation)) { // (clicked: its click animation too, until it's over)
                Shift wipe = by;
                const Animation::Look look = drawAnimated(r, i, alpha * by.opacity, wipe, boxOf(i));
                shifts[i] = {by.x + look.x, by.y + look.y, by.opacity * std::clamp(look.opacity, 0.0f, 1.0f), 0.0f, {}, wipe.wipe, wipe.wipeBox, wipe.wipeShape};
            } else {
                wiped(r, by, [&] { drawElement(r, i, alpha * by.opacity); });
            }
            clip.reset();
            rectOf(i) = rect;
            drawn_[i] = true;
            if (el.kind == ElementKind::Button && el.selected) { // (chosen because of its selected=: what its action sets holds, once it's so)
                const bool chosen = el.selected->test(lookup_);
                if (chosen && !st.chosen)
                    for (const Command& c : el.actions) {
                        const auto* set = std::get_if<cmd::Set>(&c);
                        const auto* ui = std::get_if<cmd::Ui>(&c);
                        if ((set && set->op == '=') || (ui && ui->verb == "setting")) chosen_.push_back(c);
                    }
                st.chosen = chosen;
            }
            if (el.kind == ElementKind::Container && i + 1 < n && within(i + 1, i) && (el.scrolls() || el.edges() == "fade")) {
                open.push_back(i);
                if (!fading && el.edges() == "fade" && startAside(r, scale)) { // (its parts: aside)
                    aside.emplace(r, aside_.get(), scale);
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
                    SDL_RenderClear(r);
                    fading = i;
                }
            }
        }
        closeTo(n);
        if (whole.fade > 0.0f) draw::rect(r, {0.0f, 0.0f, engine_.canvas().w, engine_.canvas().h}, withAlpha(whole.fadeColor, whole.fade * alpha)); // (fade=: over everything)
    };
    if (whole.wipe >= 0.999f) parts();
    else screenWiper_.draw(r, engine_.assets(), {engine_.canvas().w, engine_.canvas().h}, whole.wipeBox, whole.wipe, whole.wipeShape, parts);
    if (leftSince_) targets_.clear(); // (closed: only seen going)
    if (held_ && heldMoved_) { // what's being dragged, under the pointer
        const Element& el = def_->elements[*held_];
        const SDL_FRect at{mouse_.x - grab_.x, mouse_.y - grab_.y, rectOf(*held_).w, rectOf(*held_).h};
        box(r, at, styleOf(*held_), Look::Selected, boxAlpha(styleOf(*held_), alpha) * 0.9f);
        RichText label = rich(el.text);
        text(r, state_[*held_].value, label.text, at, styleOf(*held_), textColor(styleOf(*held_), Look::Selected), alpha * styleOf(*held_).opacity * 0.9f,
             TextBlock::kAll, std::move(label.spans));
    }
}

SDL_FRect ScreenView::bounds(std::size_t index) {
    const Element& el = def_->elements[index];
    SDL_FRect box = rectOf(index); // (one sized by its text: the text's; a picture without a size: its pixels)
    if (box.w > 0.0f && box.h > 0.0f) return box;
    SDL_FPoint own = state_[index].text.size();
    if (el.kind == ElementKind::Image)
        if (const Picture pic = engine_.picture(el.text.contains('{') ? interpolate(el.text) : el.text)) own = {pic.w(), pic.h()};
    if (box.w <= 0.0f) box.w = own.x > 0.0f ? own.x : 120.0f;
    if (box.h <= 0.0f) box.h = own.y > 0.0f ? own.y : 40.0f;
    return box;
}

Animation::Look ScreenView::drawAnimated(SDL_Renderer* r, std::size_t index, float alpha, Shift& by, const SDL_FRect& box) {
    const SDL_FPoint shift{by.x, by.y};
    const Element& el = def_->elements[index];
    const ElementState& st = state_[index];
    const bool picture = el.kind == ElementKind::Image || el.kind == ElementKind::Face;
    const bool shape = el.kind == ElementKind::Container;
    const SDL_Color own = picture ? SDL_Color{255, 255, 255, 255} : shape ? el.style.bg : el.style.color;
    const SDL_FRect rect = rectOf(index);
    const auto offset = [&](bool across, float place) { // (places in the box it's in, from where it's written)
        return across ? box.x + placeEdge(place, rect.w, box.w) - (rect.x - shift.x) : box.y + placeEdge(-place, rect.h, box.h) - (rect.y - shift.y);
    };
    const SDL_FPoint screen{engine_.canvas().w, engine_.canvas().h};
    Animation::Look look;
    auto add = [&](const Animation& a, float seconds) { look = look.with(a.at(seconds, look.color.value_or(own), offset, screen)); };
    if (el.animation && st.since) add(*el.animation, played(*el.animation, now() - *st.since));
    if (el.hoverAnimation && st.hover > 0.0f) add(*el.hoverAnimation, st.hover);
    if (st.clicked && el.clickAnimation) add(*el.clickAnimation, now() - *st.clicked);
    Style style = el.style; // (as the look makes it, for this moment: the part itself stays as written)
    rectOf(index).x += look.x;
    rectOf(index).y += look.y;
    if (look.zoom != 1.0f) { // (from its middle)
        rectOf(index).x -= rectOf(index).w * (look.zoom - 1.0f) / 2;
        rectOf(index).y -= rectOf(index).h * (look.zoom - 1.0f) / 2;
        rectOf(index).w *= look.zoom;
        rectOf(index).h *= look.zoom;
        style.size = std::max(1.0f, std::round(el.style.size * look.zoom)); // (whole sizes: a font for each)
        style.padding = el.style.padding * look.zoom;
    }
    if (look.color) (picture ? tint_ : shape ? style.bg : style.color) = *look.color;
    state_[index].styled = std::move(style);
    turn_ = look.rotate;
    if (look.wipe < by.wipe) { // (its own wipe, over where it is now)
        by.wipe = look.wipe;
        by.wipeBox = rectOf(index);
        by.wipeShape = look.wipeShape;
    }
    wiped(r, by, [&] {
        drawElement(r, index, alpha * std::clamp(look.opacity, 0.0f, 1.0f));
        if (look.fade > 0.0f) draw::rect(r, rectOf(index), withAlpha(look.fadeColor, std::clamp(look.fade, 0.0f, 1.0f) * alpha)); // (fade=: the part covered with the colour)
    });
    turn_ = 0.0f;
    tint_ = {255, 255, 255, 255};
    rectOf(index) = rect;
    state_[index].styled.reset();
    return look;
}

void ScreenView::drawElement(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    const float a = boxAlpha(s, alpha);   // boxes (may follow the textbox opacity setting)
    const float ta = alpha * s.opacity;   // text
    Game& game = engine_.game();

    switch (el.kind) {
    case ElementKind::Container: // (its box, when its style has one: the parts in it are drawn after it)
        box(r, rectOf(index), s, selected(el) ? Look::Selected : Look::Idle, a);
        break;

    case ElementKind::Image: { // the name may come from a variable: image "{cg}"; atlases blink; chosen: its chosen image
        const std::string& shows = !s.selected.image.empty() && selected(el) ? s.selected.image : el.text;
        const std::string name = shows.find('{') == std::string::npos ? shows : interpolate(shows);
        const auto fill = name.empty() ? std::nullopt : parseColor(name, &engine_.palette()); // (a colour, not a picture)
        if (const auto grad = name.empty() ? std::nullopt : parseGradient(name, &engine_.palette())) { // (navy-black: fading down to another)
            draw::gradient(r, rectOf(index), withAlpha(grad->first, a), withAlpha(grad->second, a));
            break;
        }
        if (el.colors.contains("bottom")) { // its colour fading down to bottom= (the same, as two words)
            const SDL_Color top = fill.value_or(s.bg);
            draw::gradient(r, rectOf(index), withAlpha(top, a), withAlpha(el.color("bottom", top), a));
            break;
        }
        if (fill || name.empty()) { // a box of that colour (or of its style's background), with the style's border
            Style filled = s;
            if (fill) filled.bg = *fill;
            box(r, rectOf(index), filled, selected(el) ? Look::Selected : Look::Idle, a);
            break;
        }
        const Picture pic = engine_.picture(name, {.eyes = engine_.config().blink ? blinking(name, now()) : 0.0f});
        if (!pic) break;
        // repeat=across|down|both: copies of it edge to edge, all over the screen that way (one moved its own
        // width over and over goes on without a seam).
        const std::string repeat = lower(el.option("repeat", "no")), fit = el.option("fit", "stretch");
        const bool across = repeat == "across" || repeat == "both", down = repeat == "down" || repeat == "both";
        SDL_FRect box{rectOf(index).x, rectOf(index).y, rectOf(index).w > 0.0f ? rectOf(index).w : pic.w(), rectOf(index).h > 0.0f ? rectOf(index).h : pic.h()};
        // scroll=left|right|up|down: it slides that way on and on, by its own size every scroll_time= seconds
        // (with repeat=, without a seam).
        if (const std::string scroll = lower(el.option("scroll", "no")); scroll != "no") {
            const float t = std::fmod(now() / std::max(0.1f, el.number("scroll_time", 10.0f)), 1.0f);
            if (scroll == "left" || scroll == "right") box.x += (scroll == "left" ? -t : t) * box.w;
            else if (scroll == "up" || scroll == "down") box.y += (scroll == "up" ? -t : t) * box.h;
        }
        auto copies = [](bool on, float at, float size, float screen) {
            return on && size > 0.0f ? std::pair{static_cast<int>(std::floor(-at / size)), static_cast<int>(std::ceil((screen - at) / size))}
                                     : std::pair{0, 1};
        };
        const auto [x0, x1] = copies(across, box.x, box.w, engine_.canvas().w);
        const auto [y0, y1] = copies(down, box.y, box.h, engine_.canvas().h);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const SDL_FRect at{box.x + static_cast<float>(x) * box.w, box.y + static_cast<float>(y) * box.h, box.w, box.h};
                std::optional<RenderClip> clip; // (cover and original: what's past the part's edges is cut off)
                if (fit == "cover" || fit == "original") clip.emplace(r, at);
                picture(r, pic, el, fit, 0.5f, 0.5f, a, false, &at);
            }
        break;
    }

    case ElementKind::Face: // the speaker's face picture
        if (const auto line = game.line(def_->name); line && !line->faceBase.empty()) {
            Picture pic = engine_.picture(line->face, line->faceFrame);
            if (!pic) pic = engine_.picture(line->faceBase, line->faceFrame);
            if (pic) {
                std::optional<RenderClip> clip; // (cover and original: what's past the part's edges is cut off)
                if (const std::string fit = el.option("fit", "contain"); fit == "cover" || fit == "original") clip.emplace(r, rectOf(index));
                picture(r, pic, el, el.option("fit", "contain"), factor(s.align), factor(s.valign), a, line->character && line->character->faceFlip);
            } else if (engine_.config().developer) { // stand-in: a box in their colour with the file it expects
                const SDL_Color c = line->speakerColor;
                draw::rect(r, rectOf(index), withAlpha({static_cast<Uint8>(c.r * 0.45f), static_cast<Uint8>(c.g * 0.45f), static_cast<Uint8>(c.b * 0.45f), 230}, a));
                draw::outline(r, rectOf(index), withAlpha(c, a));
                Style label = s;
                label.size = 13.0f;
                label.align = label.valign = Align::Center;
                text(r, st.text, std::format("{}\nimages/{}.png", line->speaker, line->face), rectOf(index), label, theme::text, ta);
            }
        }
        break;

    case ElementKind::Text: {
        const auto line = game.line(def_->name);
        const SDL_Color color = engine_.settings().highContrast ? kContrastText
                              : s.speakerColor                ? (line ? line->speakerColor : game.speakerColor())
                                                              : textColor(s, selected(el) ? Look::Selected : Look::Idle);
        RichText label = rich(el.text);
        SDL_FRect in = rectOf(index);
        if (st.ownWords) in.w = 0.0f; // (as big as its words: drawn as they are, not wrapped to their own width)
        text(r, st.text, label.text, in, s, color, ta, TextBlock::kAll, std::move(label.spans));
        break;
    }

    case ElementKind::Button: {
        const SDL_FRect hit = st.hit.value_or(rectOf(index)); // (one sized by its picture: its usual size counts too)
        const auto [look, on, chosen] = buttonLook(index, hit);
        const bool held = held_ == index;
        // A chosen one pointed at: what its pointed-at look sets, the rest as chosen (its chosen picture stays).
        Style both;
        if (look == Look::Hover && chosen) {
            both = s;
            if (both.hover.image.empty()) both.hover.image = s.selected.image;
            if (!both.hover.bg) both.hover.bg = s.selected.bg;
            if (!both.hover.border) both.hover.border = s.selected.border;
            if (!both.hover.color) both.hover.color = s.selected.color;
        }
        const Style& looks = look == Look::Hover && chosen ? both : s;
        const float left = held && heldMoved_ ? 0.35f : 1.0f; // (its copy follows the pointer)
        box(r, rectOf(index), looks, look, a * left);
        if (!el.text.empty()) {
            RichText label = rich(el.text);
            SDL_FRect in = rectOf(index);
            if (st.ownWords) in.w = 0.0f; // (as big as its words: they're drawn as they are, not wrapped to their own width)
            text(r, st.text, label.text, in, looks, textColor(looks, look), ta * left, TextBlock::kAll, std::move(label.spans));
        }
        if (on) targets_.push_back({{index}, hit, def_->keyboard});
        break;
    }

    case ElementKind::Key:
        break;

    case ElementKind::Namebox:
        drawNamebox(r, index, alpha);
        break;

    case ElementKind::Dialogue:
        if (const auto line = game.line(def_->name)) {
            Font& font = styleFont(s);
            st.text.set(font, line->text, rectOf(index).w, s.lineSpacing);
            st.text.setSpans(*line->spans);
            const SDL_Color color = engine_.settings().highContrast ? kContrastText : s.speakerColor ? line->speakerColor : line->textColor.value_or(s.color);
            // Word and line reveal show the whole word / line being typed, but never past a {pause}.
            const std::size_t shown = std::max(line->revealed, std::min(st.text.visible(line->revealed, line->mode), line->limit));
            st.text.drawIn(r, rectOf(index), factor(s.align), factor(s.valign), withAlpha(color, ta), shown, s.shadow);
        }
        break;

    case ElementKind::Indicator: // the line (or the text up to a {pause}) is shown: click to go on
        if (const auto line = game.line(def_->name); line && line->complete && game.textComplete()) {
            SDL_FRect rr{rectOf(index).x, rectOf(index).y, rectOf(index).w > 0 ? rectOf(index).w : 18.0f, rectOf(index).h > 0 ? rectOf(index).h : 12.0f};
            rr.y += el.number("bob", 3.0f) * std::sin(now() * el.number("bob_speed", 5.0f));
            const std::string image = line->atPause && !el.option("pause_image").empty() ? el.option("pause_image") : s.image;
            if (const Picture pic = image.empty() ? Picture{} : engine_.picture(image)) {
                pic.draw(r, rr, ta);
            } else {
                const SDL_Color c = withAlpha(s.color, ta);
                draw::quad(r, {rr.x, rr.y}, {rr.x + rr.w, rr.y}, {rr.x + rr.w * 0.5f, rr.y + rr.h}, {rr.x + rr.w * 0.5f, rr.y + rr.h}, c);
            }
        }
        break;

    case ElementKind::Choices:
        drawChoices(r, index, alpha);
        break;

    case ElementKind::Gallery:
    case ElementKind::Endings:
    case ElementKind::Songs:
    case ElementKind::Achievements:
    case ElementKind::Tips:
        drawRecords(r, index, alpha);
        break;

    case ElementKind::Chart:
        drawChart(r, index, alpha);
        break;

    case ElementKind::Phone:
        drawPhone(r, index, alpha);
        break;

    case ElementKind::Contacts:
        drawContacts(r, index, alpha);
        break;

    case ElementKind::Control: { // one key slot of a control; click it, then press the new key
        const std::size_t slot = Engine::slotOf(el);
        const bool waiting = engine_.rebinding(el.text, slot);
        const Look look = waiting ? Look::Selected : focused(index, 0) ? Look::Hover : Look::Idle;
        box(r, rectOf(index), s, look, a);
        text(r, st.text, waiting ? el.option("waiting") : engine_.keyLabel(el.text, slot), rectOf(index), s, textColor(s, look), ta);
        targets_.push_back({{index}, rectOf(index), def_->keyboard});
        break;
    }
    case ElementKind::History:
        drawHistory(r, index, alpha);
        break;
    case ElementKind::Page:
        drawPage(r, index, alpha);
        break;
    case ElementKind::Pages:
        drawPages(r, index, alpha);
        break;
    case ElementKind::Slots:
        drawSlots(r, index, alpha);
        break;
    case ElementKind::Input:
        drawInput(r, index, alpha);
        break;
    case ElementKind::Bar:
        drawBar(r, index, alpha);
        break;
    case ElementKind::Slider:
        drawSlider(r, index, alpha);
        break;

    case ElementKind::Message: { // in a box sized to fit (rows wrap at w=, if it has one)
        const std::string message = toString(engine_.lookup()("message"));
        if (message.empty()) break;
        RichText shown = rich(message);
        st.text.set(styleFont(s), std::move(shown.text), rectOf(index).w > 0 ? std::max(1.0f, rectOf(index).w - 2 * s.padding) : 0.0f, s.lineSpacing);
        st.text.setSpans(std::move(shown.spans));
        const SDL_FPoint size = st.text.size();
        const float w = size.x + 2 * s.padding, h = size.y + 2 * s.padding;
        const SDL_FRect rr{rectOf(index).x - w * factor(s.align), rectOf(index).y, w, h};
        box(r, rr, s, Look::Idle, a);
        st.text.drawIn(r, {rr.x + s.padding, rr.y + s.padding, size.x, 0.0f}, factor(s.align), 0.0f, withAlpha(s.color, ta),
                       TextBlock::kAll, s.shadow);
        break;
    }
    case ElementKind::Movie:
        drawMovie(r, index, alpha);
        break;
    }
}

// Draws a picture into the element's box: fit=stretch, contain (all of it), cover (fills the box)
// or original (its own pixels, the game's: what's past the box, the caller cuts off), placed by
// `ax` / `ay` (0 = left / top, 1 = right / bottom). flip=yes mirrors it.
void ScreenView::picture(SDL_Renderer* r, const Picture& pic, const Element& el, std::string_view fit, float ax, float ay, float alpha,
                         bool flip, const SDL_FRect* box) {
    const float w = pic.w(), h = pic.h();
    const SDL_FRect& in = box ? *box : rectOf(indexOf(el));
    SDL_FRect dst{in.x, in.y, in.w > 0 ? in.w : w, in.h > 0 ? in.h : h};
    if (fit != "stretch" && w > 0 && h > 0) {
        const float scale = fit == "original" ? 1.0f : fit == "cover" ? std::max(dst.w / w, dst.h / h) : std::min(dst.w / w, dst.h / h);
        dst = {std::round(dst.x + (dst.w - w * scale) * ax), std::round(dst.y + (dst.h - h * scale) * ay), w * scale, h * scale};
    }
    pic.draw(r, dst, alpha, turn_, nullptr, flip != parseBool(el.option("flip", "no")).value_or(false), tint_);
}

void ScreenView::drawNamebox(SDL_Renderer* r, std::size_t index, float alpha) {
    const auto line = engine_.game().line(def_->name);
    if (!line || line->speaker.empty()) return;
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    st.text.set(styleFont(s), line->speaker);
    const SDL_FPoint size = st.text.size();
    SDL_FRect rr{rectOf(index).x, rectOf(index).y, std::max(rectOf(index).w, size.x + 2 * s.padding), rectOf(index).h > 0 ? rectOf(index).h : size.y + 2 * s.padding};
    if (s.align != Align::Start && rectOf(index).w > 0) rr.x += (rectOf(index).w - rr.w) * factor(s.align);
    box(r, rr, s, Look::Idle, boxAlpha(s, alpha));
    if (const float bar = el.number("bar", 0.0f); bar > 0.0f)
        draw::rect(r, {rr.x, rr.y + rr.h - bar, rr.w, bar}, withAlpha(el.color("bar_color", line->speakerColor), alpha * s.opacity));
    const SDL_Color color = engine_.settings().highContrast ? kContrastText : s.speakerColor ? line->speakerColor : s.color;
    st.text.draw(r, rr.x + (rr.w - size.x) * 0.5f, rr.y + (rr.h - size.y) * 0.5f, withAlpha(color, alpha * s.opacity),
                 TextBlock::kAll, s.shadow);
}

void ScreenView::drawChoices(SDL_Renderer* r, std::size_t index, float alpha) {
    Game& game = engine_.game();
    if (!game.showingChoices()) return;
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    const auto& items = game.choices();
    // Many choices share the box: they get shorter (and closer together) rather than spill out of it.
    Grid g = grid(el, {.itemH = 54.0f, .gapY = 14.0f});
    const auto n = static_cast<float>(items.size());
    if (rectOf(index).h > 0 && g.height(items.size()) > rectOf(index).h) {
        g.gapY = std::min(g.gapY, 8.0f);
        g.itemH = (rectOf(index).h - g.gapY * (n - 1)) / n;
    }
    const float total = g.height(items.size());
    const std::string anchor = el.option("anchor", "center");
    const float top = anchor == "top" ? 0.0f : anchor == "bottom" ? rectOf(index).h - total : (rectOf(index).h - total) * 0.5f;
    const SDL_FRect area{rectOf(index).x, rectOf(index).y + top, rectOf(index).w, rectOf(index).h};
    // (a text for each option as written, so one shown or hidden by its if doesn't move the others' texts)
    auto blockOf = [](const ChoiceItem& item) { return static_cast<std::size_t>(std::max(item.option, 0)); };
    std::size_t blocks = 0;
    for (const ChoiceItem& item : items) blocks = std::max(blocks, blockOf(item) + 1);
    st.items.resize(blocks);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const ChoiceItem& item = items[i];
        const SDL_FRect rr = g.cell(area, i);
        // Locked ones (needs ...) look disabled and say what they need; ones picked before can look
        // different (seen_style=).
        const Style& is = item.locked ? el.substyle("locked_style") : item.seen ? el.substyle("seen_style") : s;
        const Look look = item.locked ? Look::Disabled : focused(index, static_cast<int>(i)) ? Look::Hover : Look::Idle;
        box(r, rr, is, look, boxAlpha(is, alpha));
        const std::string shown = item.locked && !item.note.empty() ? item.text + "  (" + item.note + ")" : item.text;
        RichText label = rich(shown);
        text(r, st.items[blockOf(item)], label.text, rr, is, textColor(is, look), alpha * is.opacity, TextBlock::kAll, std::move(label.spans));
        targets_.push_back({{index, static_cast<int>(i)}, rr, true});
    }
}

// The gallery's pictures, the endings, songs, achievements or tips, in the order they're listed,
// in a grid that scrolls. Pictures show their picture and title; the others their title, and under
// it an achievement's description (or a locked ending's hint). Ones the player doesn't have show
// text.locked (pictures) or text.unknown, except achievements that aren't hidden. Pictures, songs
// and tips the player has can be clicked: a picture opens big (action=), a song plays (the one
// playing looks selected), a tip is the one read ({tip_text}; it looks selected, and ones not read
// yet use new_style).
void ScreenView::drawRecords(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    const RecordKind kind = el.kind == ElementKind::Gallery ? RecordKind::Gallery
                          : el.kind == ElementKind::Endings ? RecordKind::Ending
                          : el.kind == ElementKind::Songs   ? RecordKind::Song
                          : el.kind == ElementKind::Tips    ? RecordKind::Tip
                                                            : RecordKind::Achievement;
    const auto& list = engine_.script().records(kind);
    const Style& titleStyle = el.substyle("title_style");
    const Style& textStyle = el.substyle("text_style");
    const Style& lockedStyle = el.substyle("locked_style");
    const bool pictures = kind == RecordKind::Gallery, song = kind == RecordKind::Song, tip = kind == RecordKind::Tip;
    const Grid g = grid(el, pictures ? Grid{.columns = 3, .itemH = 202.0f, .gapX = 24.0f, .gapY = 24.0f} : Grid{.itemH = 64.0f, .gapX = 8.0f, .gapY = 8.0f});
    settle(index, g.height(list.size()) - rectOf(index).h);
    st.items.resize(list.size());
    st.extra.resize(list.size());

    const RenderClip clip(r, rectOf(index));
    const std::string playing = engine_.audio().music();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Script::Record& rec = list[i];
        const SDL_FRect rr = g.cell(rectOf(index), i, st.scroll);
        if (!overlaps(rr, rectOf(index))) continue;
        const bool have = engine_.has(kind, i);
        const bool clickable = have && (pictures || song || tip);
        const bool current = have && ((song && !playing.empty() && playing == engine_.assets().audio(rec.name)) || (tip && engine_.tip() == i));
        const Look look = current ? Look::Selected : clickable && focused(index, static_cast<int>(i)) ? Look::Hover : Look::Idle;
        const bool fresh = tip && engine_.newTip(i);
        const Style& bs = !have ? lockedStyle : fresh ? el.substyle("new_style") : s;
        box(r, rr, bs, look, boxAlpha(bs, alpha));
        if (clickable && inside(rr, rectOf(index))) targets_.push_back({{index, static_cast<int>(i)}, rr, true});
        const SDL_FRect inner{rr.x + s.padding, rr.y + s.padding, rr.w - 2 * s.padding, rr.h - 2 * s.padding};
        if (pictures) { // the picture, cut to fit the card, with its title on a strip at the bottom
            if (!have) continue; // (its locked look, nothing on it)
            const bool movie = engine_.galleryMovie(i);
            if (const Picture pic = engine_.picture(rec.name)) { // (a video shows the picture of its name, if there is one)
                const float w = pic.w(), h = pic.h();
                const float scale = std::max(inner.w / w, inner.h / h);
                pic.crop({(w - inner.w / scale) * 0.5f, (h - inner.h / scale) * 0.5f, inner.w / scale, inner.h / scale}).draw(r, inner, alpha);
            } else {
                draw::rect(r, inner, withAlpha(movie ? SDL_Color{14, 15, 26, 255} : draw::hsv(static_cast<float>(fnv1a(rec.name) % 1000) / 1000.0f, 0.35f, 0.4f), alpha));
            }
            if (movie) { // a play sign
                const float cx = inner.x + inner.w * 0.5f, cy = inner.y + inner.h * 0.42f, k = std::min(inner.w, inner.h) * 0.12f;
                draw::circle(r, cx, cy, k * 1.5f, withAlpha(SDL_Color{0, 0, 0, 150}, alpha));
                draw::quad(r, {cx - k * 0.5f, cy - k * 0.7f}, {cx + k * 0.75f, cy}, {cx + k * 0.75f, cy}, {cx - k * 0.5f, cy + k * 0.7f},
                           withAlpha(SDL_Color{240, 240, 245, 230}, alpha));
            }
            const float stripH = titleStyle.size * 1.6f;
            const SDL_FRect strip{inner.x, inner.y + inner.h - stripH, inner.w, stripH};
            box(r, strip, titleStyle, Look::Idle, boxAlpha(titleStyle, alpha));
            text(r, st.items[i], rec.title, strip, titleStyle, titleStyle.color, alpha * titleStyle.opacity);
            continue;
        }
        const bool achievement = kind == RecordKind::Achievement, ending = kind == RecordKind::Ending;
        const bool secret = !have && (!achievement || rec.hidden); // hidden= instead of its title
        std::string title = secret ? el.option("hidden") : rec.title;
        if (ending) title = std::format("{}. {}", i + 1, title);
        const std::string text = achievement ? (rec.hidden && !have ? el.option("hidden") : rec.text) : ending && !have ? rec.text : "";
        const Style& ts = !have ? lockedStyle : fresh ? el.substyle("new_style") : titleStyle;
        const float pad = s.padding > 0.0f ? s.padding : 12.0f;
        st.items[i].set(styleFont(ts), title, rr.w - 2 * pad, ts.lineSpacing);
        st.extra[i].set(styleFont(textStyle), text, rr.w - 2 * pad, textStyle.lineSpacing);
        const float h = st.items[i].size().y + (text.empty() ? 0.0f : st.extra[i].size().y + 2.0f);
        const float y = rr.y + std::max(0.0f, (rr.h - h) * 0.5f);
        st.items[i].draw(r, rr.x + pad, y, withAlpha(textColor(ts, look), alpha * ts.opacity), TextBlock::kAll, ts.shadow);
        if (!text.empty())
            st.extra[i].draw(r, rr.x + pad, y + st.items[i].size().y + 2.0f, withAlpha(have ? textStyle.color : lockedStyle.color, alpha * textStyle.opacity),
                             TextBlock::kAll, textStyle.shadow);
    }
}

// The flowchart: every place reached (chapters, choices, endings, "chart" points) as a tree, each
// under the place the player first came from, with the option picked there above it. A choice's
// options not tried yet hang under it as stubs: locked ones say what they need, and ones that
// opened since (their condition is true now) get a mark. Clicking a place goes back to it; a stub
// goes to its choice. It moves with the mouse wheel and by dragging, and starts at the current place.
void ScreenView::drawChart(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    const Chart& chart = engine_.chart();
    const auto& nodes = chart.nodes();
    const Script& script = engine_.script();
    if (nodes.empty()) return; // (places appear as the story reaches them)
    const Grid g = grid(el, {.itemW = 220.0f, .itemH = 60.0f, .gapX = 26.0f, .gapY = 56.0f});
    const float nodeW = g.itemW, nodeH = g.itemH, gapX = g.gapX, gapY = g.gapY;

    struct Item {
        int node = -1;     // the place (-1: a stub)
        int parent = -1;   // the item it hangs under
        int option = -1;   // the option picked at the parent to get here
        std::string title, note;
        bool locked = false, fresh = false;
        std::vector<int> children;
        float x = 0.0f;
        int depth = 0;
    };
    std::vector<Item> items(nodes.size());
    std::unordered_map<std::string_view, int> at;
    for (std::size_t i = 0; i < nodes.size(); ++i) at.emplace(nodes[i].id, static_cast<int>(i));
    // A place's texts show as they were there: its saved variables, then the rest as now.
    auto lookupAt = [&](const Chart::Node& n) {
        auto vars = std::make_shared<Variables>(Game::savedVariables(n.state));
        const Lookup& now = engine_.lookup();
        return Lookup([vars, now](std::string_view name) -> Value {
            if (auto it = vars->find(name); it != vars->end()) return it->second;
            return now(name);
        });
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Chart::Node& n = nodes[i];
        Item& item = items[i];
        item.node = static_cast<int>(i);
        const auto pc = engine_.placeCommand(n.id);
        const Command* c = pc ? &script.code[*pc].command : nullptr;
        const Lookup lookup = lookupAt(n);
        if (const auto* ch = c ? std::get_if<cmd::Chapter>(c) : nullptr) item.title = fillValues(ch->title, lookup);
        else if (const auto* m = c ? std::get_if<cmd::Mark>(c) : nullptr) item.title = fillValues(m->title, lookup);
        else if (const auto* rec = c ? std::get_if<cmd::Record>(c) : nullptr) {
            const auto& list = script.records(rec->kind);
            if (const Script::Record* e = script.record(rec->kind, rec->name)) item.title = std::format("{}. {}", e - list.data() + 1, e->title);
        } else if (const auto* menu = c ? std::get_if<cmd::Menu>(c) : nullptr) {
            if (!menu->prompt.empty()) item.title = fillValues(menu->prompt, lookup);
            else // (no question: its options)
                for (const cmd::Choice& choice : menu->choices) item.title += (item.title.empty() ? "" : " / ") + fillValues(choice.text, lookup);
            // Its options not tried yet: stubs under it.
            for (const Chart::Option& o : n.options) {
                if (o.index < 0 || static_cast<std::size_t>(o.index) >= menu->choices.size()) continue;
                if (std::ranges::any_of(chart.edges(), [&](const Chart::Edge& e) { return e.from == n.id && e.option == o.index; })) continue;
                const cmd::Choice& choice = menu->choices[static_cast<std::size_t>(o.index)];
                Item stub{.parent = static_cast<int>(i), .option = o.index, .title = fillValues(choice.text, lookup)};
                const bool openNow = !choice.condition || choice.condition->test(lookup);
                stub.locked = !openNow;
                stub.fresh = !o.open && openNow; // locked last time, open now (a key was found...)
                if (stub.locked) stub.note = fillValues(choice.note, lookup);
                items[i].fresh = items[i].fresh || stub.fresh;
                items.push_back(std::move(stub));
            }
        }
        if (item.title.empty()) item.title = n.id;
    }
    // Each place hangs under the place it was first reached from.
    for (const Chart::Edge& e : chart.edges()) {
        const auto from = at.find(e.from), to = at.find(e.to);
        if (from == at.end() || to == at.end()) continue;
        Item& child = items[static_cast<std::size_t>(to->second)];
        const int order = nodes[static_cast<std::size_t>(from->second)].order;
        if (order < nodes[static_cast<std::size_t>(to->second)].order &&
            (child.parent < 0 || order < nodes[static_cast<std::size_t>(child.parent)].order)) {
            child.parent = from->second;
            child.option = e.option;
        }
    }
    for (std::size_t j = 0; j < items.size(); ++j)
        if (items[j].parent >= 0) items[static_cast<std::size_t>(items[j].parent)].children.push_back(static_cast<int>(j));
    for (Item& item : items)
        std::ranges::stable_sort(item.children, [&](int a, int b) { return items[static_cast<std::size_t>(a)].option < items[static_cast<std::size_t>(b)].option; });
    // Leaves side by side, each parent over the middle of its children.
    float leaves = 0.0f;
    int deepest = 0;
    std::function<void(int, int)> place = [&](int j, int depth) {
        Item& item = items[static_cast<std::size_t>(j)];
        item.depth = depth;
        deepest = std::max(deepest, depth);
        if (item.children.empty()) {
            item.x = leaves++;
            return;
        }
        for (int c : item.children) place(c, depth + 1);
        item.x = (items[static_cast<std::size_t>(item.children.front())].x + items[static_cast<std::size_t>(item.children.back())].x) * 0.5f;
    };
    for (std::size_t j = 0; j < nodes.size(); ++j)
        if (items[j].parent < 0) place(static_cast<int>(j), 0);

    const float width = leaves * (nodeW + gapX) - gapX, height = static_cast<float>(deepest + 1) * (nodeH + gapY) - gapY;
    const float margin = 24.0f;
    const std::string current = engine_.playing() ? engine_.game().chartPlace() : std::string();
    if (!st.centered) { // start at the current place (else the first one)
        st.centered = true;
        auto it = at.find(current);
        if (it == at.end()) it = at.find(nodes.front().id);
        if (it != at.end()) {
            const Item& item = items[static_cast<std::size_t>(it->second)];
            st.panX = rectOf(index).w * 0.5f - (item.x * (nodeW + gapX) + nodeW * 0.5f) - margin;
            st.scroll = static_cast<float>(item.depth) * (nodeH + gapY) + nodeH * 0.5f - rectOf(index).h * 0.5f + margin;
        }
    }
    const float spareX = rectOf(index).w - width - 2 * margin;
    settle(index, height + 2 * margin - rectOf(index).h);
    const float wantPan = st.panX;
    st.panX = spareX >= 0.0f ? spareX * 0.5f : std::clamp(st.panX, spareX, 0.0f);
    if (st.panX != wantPan) st.glideX = 0.0f;
    auto boxAt = [&](const Item& item) {
        return SDL_FRect{rectOf(index).x + margin + st.panX + item.x * (nodeW + gapX), rectOf(index).y + margin - st.scroll + static_cast<float>(item.depth) * (nodeH + gapY),
                         nodeW, nodeH};
    };

    const RenderClip clip(r, rectOf(index));
    const SDL_Color line = withAlpha(el.color("line", SDL_Color{92, 96, 130, 255}), alpha);
    const Style& labelStyle = el.substyle("label_style");
    st.items.resize(items.size());
    st.extra.resize(items.size());
    st.ids.assign(items.size(), {});
    for (std::size_t j = 0; j < items.size(); ++j) { // lines first, then the boxes on top
        const Item& item = items[j];
        if (item.parent < 0) continue;
        const SDL_FRect a = boxAt(items[static_cast<std::size_t>(item.parent)]), b = boxAt(item);
        const float ax = a.x + a.w * 0.5f, bx = b.x + b.w * 0.5f, mid = a.y + a.h + gapY * 0.3f;
        draw::rect(r, {ax - 1.0f, a.y + a.h, 2.0f, mid - a.y - a.h}, line);
        draw::rect(r, {std::min(ax, bx) - 1.0f, mid - 1.0f, std::abs(bx - ax) + 2.0f, 2.0f}, line);
        draw::rect(r, {bx - 1.0f, mid, 2.0f, b.y - mid}, line);
        if (item.node >= 0 && item.option >= 0) { // the option that led here, above it
            const auto pc = engine_.placeCommand(nodes[static_cast<std::size_t>(items[static_cast<std::size_t>(item.parent)].node)].id);
            const auto* menu = pc ? std::get_if<cmd::Menu>(&script.code[*pc].command) : nullptr;
            if (menu && static_cast<std::size_t>(item.option) < menu->choices.size()) {
                Font& font = styleFont(labelStyle);
                st.extra[j].set(font, ellipsize(font, fillValues(menu->choices[static_cast<std::size_t>(item.option)].text, lookupAt(nodes[static_cast<std::size_t>(item.node)])), nodeW * 0.5f + gapX - 10.0f));
                const SDL_FPoint size = st.extra[j].size();
                st.extra[j].draw(r, bx + 7.0f, b.y - size.y - 3.0f, withAlpha(labelStyle.color, alpha * labelStyle.opacity), TextBlock::kAll, labelStyle.shadow); // beside the line
            }
        }
    }
    for (std::size_t j = 0; j < items.size(); ++j) {
        const Item& item = items[j];
        const SDL_FRect b = boxAt(item);
        if (!overlaps(b, rectOf(index))) continue;
        const bool stub = item.node < 0;
        const Chart::Node* n = stub ? nullptr : &nodes[static_cast<std::size_t>(item.node)];
        const Style& bs = stub ? el.substyle("stub_style") : n->kind == Chart::Kind::Chapter ? el.substyle("chapter_style")
                                                         : n->kind == Chart::Kind::Ending  ? el.substyle("ending_style")
                                                                                           : s;
        const Look look = n && n->id == current ? Look::Selected : focused(index, static_cast<int>(j)) ? Look::Hover : Look::Idle;
        box(r, b, bs, look, boxAlpha(bs, alpha));
        std::string label = item.title;
        if (item.locked && !item.note.empty()) label = item.title + "  (" + item.note + ")";
        RichText rt = rich(label);
        text(r, st.items[j], rt.text, b, bs, textColor(bs, look), alpha * bs.opacity, TextBlock::kAll, std::move(rt.spans));
        if (item.fresh) { // newly open: a small diamond in the corner
            const float cx = b.x + b.w - 9.0f, cy = b.y + 9.0f, k = 5.0f;
            draw::quad(r, {cx, cy - k}, {cx + k, cy}, {cx, cy + k}, {cx - k, cy}, withAlpha(SDL_Color{255, 200, 90, 255}, alpha));
        }
        st.ids[j] = stub ? nodes[static_cast<std::size_t>(items[static_cast<std::size_t>(item.parent)].node)].id : n->id;
        if (inside(b, rectOf(index))) targets_.push_back({{index, static_cast<int>(j)}, b, true});
    }
}

// The phone's messages, the newest at the bottom (like a chat), each in a bubble at most item_w
// wide: theirs on the left under the sender's name (text_style, name_style), the player's own on the
// right (mine_style). The answers open to a message: ones whose words are in it are underlined there
// and can be clicked (in reply_style's colour); the others are buttons under it (reply_style). An
// answer back that's still being typed shows as dots. Showing the messages reads them.
namespace {
// Made-up saves and pages for the editor's preview of a screen (the game's own are empty there), so
// those lists can be laid out; made-up history and messages come from the preview's game itself.
const std::vector<SlotBrowser::Card>& sampleCards() {
    static const std::vector<SlotBrowser::Card> cards = [] {
        std::vector<SlotBrowser::Card> out;
        SDL_Time now = 0;
        SDL_GetCurrentTime(&now);
        static constexpr std::string_view kLines[]{"Aiko: Good morning! I brought cake.", "The rain didn't stop all day.", "Stranger: Who's there?"};
        for (int i = 0; i < 6; ++i) {
            SlotBrowser::Card card{.slot = std::to_string(i + 1), .info = {}, .thumbnail = {}};
            if (i < 3) card.info = {.exists = true, .time = now - static_cast<SDL_Time>(i) * 86400LL * 1000000000LL, .summary = std::string(kLines[i])};
            out.push_back(std::move(card));
        }
        return out;
    }();
    return cards;
}
const std::vector<std::string>& samplePages() {
    static const std::vector<std::string> pages{"auto", "quick", "1", "2", "3"};
    return pages;
}
} // namespace

void ScreenView::drawPhone(SDL_Renderer* r, std::size_t index, float alpha) {
    Game& game = engine_.game();
    const std::string& contact = engine_.contact(); // their conversation (none picked: everyone's, with names)
    game.readMessages(contact);
    const Element& el = def_->elements[index];
    ElementState& st = state_[index];
    const auto& messages = game.messages();
    auto inView = [&](const MessageItem& m) { return contact.empty() || m.with == contact; };
    const Style& nameStyle = el.substyle("name_style");
    const Style& theirs = el.substyle("text_style");
    const Style& mine = el.substyle("mine_style");
    const Style& replyStyle = el.substyle("reply_style");
    const Grid g = grid(el, {.itemW = rectOf(index).w * 0.7f, .gapY = 14.0f});
    constexpr float kButtonGap = 6.0f;
    const float buttonH = styleFont(replyStyle).lineHeight() + 2.0f * std::max(replyStyle.padding, 6.0f);

    struct Answer {
        ChoiceItem item;
        std::size_t begin = 0, end = 0; // its words in the message (else a button)
        bool word = false;
    };
    std::vector<std::vector<Answer>> answers(messages.size());
    std::vector<float> heights(messages.size());
    st.items.resize(messages.size());
    st.extra.resize(messages.size());
    std::size_t buttons = 0;
    float total = 0.0f;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        const MessageItem& m = messages[i];
        if (!inView(m)) {
            heights[i] = -g.gapY; // (takes no room)
            continue;
        }
        const Style& bs = m.from.empty() ? mine : theirs;
        // One still being typed: dots, one more every third of a second.
        RichText text = m.typing > 0.0f ? RichText{.text = std::string(" . . .").substr(0, 2 * (1 + static_cast<std::size_t>(now() * 3.0f) % 3))}
                                        : rich(m.text);
        const std::string plain = lower(text.text);
        float buttonsH = 0.0f;
        for (ChoiceItem& item : game.replies(i)) {
            Answer a{.item = std::move(item)};
            const std::string word = lower(rich(a.item.text).text);
            const auto at = word.empty() || a.item.locked ? std::string::npos : plain.find(word);
            if (at != std::string::npos) {
                a.word = true;
                a.begin = utf8::length(std::string_view(plain).substr(0, at));
                a.end = a.begin + utf8::length(word);
                text.spans.push_back({.begin = a.begin, .end = a.end, .color = replyStyle.color, .style = TTF_STYLE_UNDERLINE});
            } else {
                ++buttons;
                buttonsH += kButtonGap + buttonH;
            }
            answers[i].push_back(std::move(a));
        }
        const bool named = contact.empty() && !m.from.empty(); // (in one person's conversation, their name is at the top)
        st.items[i].set(styleFont(nameStyle), named ? m.from : std::string{});
        st.extra[i].set(styleFont(bs), std::move(text.text), std::max(1.0f, g.itemW - 2 * bs.padding), bs.lineSpacing);
        st.extra[i].setSpans(std::move(text.spans));
        heights[i] = (named ? st.items[i].size().y + 2.0f : 0.0f) + st.extra[i].size().y + 2 * bs.padding + buttonsH;
        total += heights[i] + (total > 0.0f ? g.gapY : 0.0f);
    }
    st.buttons.resize(buttons);
    settle(index, total - rectOf(index).h);

    const RenderClip clip(r, rectOf(index));
    float y = total <= rectOf(index).h ? rectOf(index).y : rectOf(index).y + rectOf(index).h - total + st.scroll;
    std::size_t button = 0;
    for (std::size_t i = 0; i < messages.size(); y += heights[i] + g.gapY, ++i) {
        const MessageItem& m = messages[i];
        if (!inView(m)) continue;
        const bool own = m.from.empty();
        const Style& bs = own ? mine : theirs;
        float at = y;
        if (!st.items[i].text().empty()) {
            st.items[i].draw(r, rectOf(index).x, at, withAlpha(m.color, alpha * nameStyle.opacity), TextBlock::kAll, nameStyle.shadow);
            at += st.items[i].size().y + 2.0f;
        }
        const SDL_FPoint size = st.extra[i].size();
        const float w = std::min(g.itemW, size.x + 2 * bs.padding), h = size.y + 2 * bs.padding;
        const SDL_FRect bubble{own ? rectOf(index).x + rectOf(index).w - w : rectOf(index).x, at, w, h};
        box(r, bubble, bs, Look::Idle, boxAlpha(bs, alpha));
        const float tx = bubble.x + bs.padding, ty = bubble.y + bs.padding;
        for (const Answer& a : answers[i]) // words that answer: highlighted under the pointer
            if (a.word)
                for (const SDL_FRect& b : st.extra[i].boxes(a.begin, a.end)) {
                    const SDL_FRect word{tx + b.x - 2.0f, ty + b.y, b.w + 4.0f, b.h};
                    if (focused(index, static_cast<int>(i), Button::None, a.item.option))
                        draw::rect(r, word, withAlpha(replyStyle.hover.bg.value_or(SDL_Color{255, 255, 255, 40}), alpha));
                    if (inside(word, rectOf(index))) targets_.push_back({{index, static_cast<int>(i), Button::None, a.item.option}, word, true});
                }
        st.extra[i].draw(r, tx, ty, withAlpha(bs.color, alpha * bs.opacity), TextBlock::kAll, bs.shadow);
        at += h;
        for (const Answer& a : answers[i]) { // the other answers: buttons under it, on the player's side
            if (a.word) continue;
            at += kButtonGap;
            const SDL_FRect b{rectOf(index).x + rectOf(index).w - g.itemW, at, g.itemW, buttonH};
            at += buttonH;
            const bool hot = focused(index, static_cast<int>(i), Button::None, a.item.option);
            const Look look = a.item.locked ? Look::Disabled : hot ? Look::Hover : Look::Idle;
            box(r, b, replyStyle, look, boxAlpha(replyStyle, alpha));
            const std::string label = a.item.locked && !a.item.note.empty() ? a.item.text + "  (" + a.item.note + ")" : a.item.text;
            RichText shown = rich(label);
            text(r, st.buttons[button++], shown.text, b, replyStyle, textColor(replyStyle, look), alpha * replyStyle.opacity, TextBlock::kAll,
                 std::move(shown.spans));
            if (!a.item.locked && inside(b, rectOf(index))) targets_.push_back({{index, static_cast<int>(i), Button::None, a.item.option}, b, true});
        }
    }
}

// The people messaging the player, the latest conversation first: each with their newest message
// (or dots while they write) and how many are unread. Clicking one sets {contact} and runs action=.
void ScreenView::drawContacts(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    const auto people = engine_.game().contacts();
    const auto& messages = engine_.game().messages();
    const Style& nameStyle = el.substyle("name_style");
    const Style& textStyle = el.substyle("text_style");
    const Style& unreadStyle = el.substyle("unread_style");
    const Grid g = grid(el, {.itemH = 64.0f, .gapX = 8.0f, .gapY = 8.0f});
    settle(index, g.height(people.size()) - rectOf(index).h);
    st.items.resize(people.size());
    st.extra.resize(people.size());
    st.buttons.resize(people.size());

    const RenderClip clip(r, rectOf(index));
    for (std::size_t i = 0; i < people.size(); ++i) {
        const Game::Contact& c = people[i];
        const SDL_FRect rr = g.cell(rectOf(index), i, st.scroll);
        if (!overlaps(rr, rectOf(index))) continue;
        const Look look = focused(index, static_cast<int>(i)) ? Look::Hover : Look::Idle;
        box(r, rr, s, look, boxAlpha(s, alpha));
        if (inside(rr, rectOf(index))) targets_.push_back({{index, static_cast<int>(i)}, rr, true});
        const float pad = s.padding > 0.0f ? s.padding : 12.0f;
        float right = rr.x + rr.w - pad;
        if (c.unread > 0) { // the count, in a badge at the right
            const std::string count = std::to_string(c.unread);
            const float h = styleFont(unreadStyle).lineHeight() + 4.0f, w = std::max(h, styleFont(unreadStyle).measure(count) + 2.0f * h / 3.0f);
            const SDL_FRect badge{right - w, rr.y + (rr.h - h) * 0.5f, w, h};
            box(r, badge, unreadStyle, Look::Idle, boxAlpha(unreadStyle, alpha));
            Style centred = unreadStyle;
            centred.align = centred.valign = Align::Center;
            text(r, st.buttons[i], count, badge, centred, unreadStyle.color, alpha * unreadStyle.opacity);
            right = badge.x - pad;
        }
        const float width = std::max(1.0f, right - rr.x - pad);
        const std::string newest = c.typing ? std::string(" . . .").substr(0, 2 * (1 + static_cast<std::size_t>(now() * 3.0f) % 3))
                                            : rich(messages[c.last].text).text;
        st.items[i].set(styleFont(nameStyle), ellipsize(styleFont(nameStyle), c.name, width));
        st.extra[i].set(styleFont(textStyle), ellipsize(styleFont(textStyle), newest, width));
        const float h = st.items[i].size().y + 2.0f + st.extra[i].size().y;
        const float y = rr.y + std::max(0.0f, (rr.h - h) * 0.5f);
        st.items[i].draw(r, rr.x + pad, y, withAlpha(c.color, alpha * nameStyle.opacity), TextBlock::kAll, nameStyle.shadow);
        st.extra[i].draw(r, rr.x + pad, y + st.items[i].size().y + 2.0f, withAlpha(textStyle.color, alpha * textStyle.opacity), TextBlock::kAll,
                         textStyle.shadow);
    }
}

// A video playing in a box (fit= like image parts), again and again unless loop=no.
void ScreenView::drawMovie(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    ElementState& st = state_[index];
    st.movieFrame = engine_.frameNumber();
    const std::string name = el.text.find('{') == std::string::npos ? el.text : interpolate(el.text);
    if (name != st.movieName) {
        st.movie.reset();
        st.movieName = name;
        if (const auto file = engine_.assets().findMovie(name)) {
            st.movie = std::make_unique<Movie>(r, engine_.audio(), *file, parseBool(el.option("loop", "yes")).value_or(true),
                                               std::clamp(el.number("volume", 1.0f), 0.0f, 1.0f));
            if (!*st.movie) st.movie.reset();
        } else {
            logWarn("There's no video called '{}'.", name);
        }
        st.movieTime = now();
    }
    if (!st.movie) return;
    st.movie->update(now() - std::exchange(st.movieTime, now()));
    if (SDL_Texture* frame = st.movie->texture()) {
        float w = 0, h = 0;
        SDL_GetTextureSize(frame, &w, &h);
        std::optional<RenderClip> clip; // (cover and original: what's past the part's edges is cut off)
        if (const std::string fit = el.option("fit", "contain"); fit == "cover" || fit == "original") clip.emplace(r, rectOf(index));
        picture(r, Picture{.texture = frame, .src = {0, 0, w, h}}, el, el.option("fit", "contain"), 0.5f, 0.5f, alpha * styleOf(index).opacity);
    }
}

void ScreenView::endFrame(std::uint64_t frame) {
    for (auto& st : state_)
        if (st.movie && st.movieFrame != frame) {
            st.movie.reset();
            st.movieName.clear();
        }
}

void ScreenView::drawHistory(SDL_Renderer* r, std::size_t index, float alpha) {
    Game& game = engine_.game();
    const Element& el = def_->elements[index];
    ElementState& st = state_[index];
    const auto& entries = game.history();
    const Style& nameStyle = el.substyle("name_style");
    const Style& textStyle = el.substyle("text_style");
    const Style& choiceStyle = el.substyle("choice_style");
    const float nameW = share(el, "name_w", true, 200.0f), gap = grid(el, {.gapY = 18.0f}).gapY;
    // Buttons at the end of each entry: hear its voice again (voice=no: none), go back to it (back=no: none).
    const bool voices = el.option("voice", "yes") != "no", backs = el.option("back", "yes") != "no";
    const Style& buttonStyle = el.substyle("button_style");
    constexpr float kButton = 30.0f, kButtonGap = 8.0f;
    const float buttonsW = (voices ? kButton + kButtonGap : 0.0f) + (backs ? kButton + kButtonGap : 0.0f);
    const float textX = rectOf(index).x + nameW + 30.0f, textW = rectOf(index).w - nameW - 30.0f - buttonsW;

    st.items.resize(entries.size());
    st.extra.resize(entries.size());
    float total = 0.0f;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Style& ts = entries[i].choice ? choiceStyle : textStyle;
        st.items[i].set(styleFont(nameStyle), entries[i].speaker);
        st.extra[i].set(styleFont(ts), entries[i].text, textW, ts.lineSpacing);
        st.extra[i].setSpans(entries[i].spans);
        total += std::max(st.items[i].size().y, st.extra[i].size().y) + gap;
    }
    settle(index, total - rectOf(index).h);

    const RenderClip clip(r, rectOf(index));
    float y = rectOf(index).y + rectOf(index).h - total + st.scroll;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const float h = std::max(st.items[i].size().y, st.extra[i].size().y);
        if (y + h >= rectOf(index).y && y <= rectOf(index).y + rectOf(index).h) {
            const Style& ts = entries[i].choice ? choiceStyle : textStyle;
            st.items[i].drawIn(r, {rectOf(index).x, y, nameW, 0}, 1.0f, 0.0f, withAlpha(entries[i].color, alpha * nameStyle.opacity),
                               TextBlock::kAll, nameStyle.shadow);
            st.extra[i].draw(r, textX, y, withAlpha(ts.color, alpha * ts.opacity), TextBlock::kAll, ts.shadow);
            float bx = rectOf(index).x + rectOf(index).w - kButton;
            const float by = y + std::max(0.0f, (std::min(h, 40.0f) - kButton) * 0.5f);
            auto button = [&](Button which) {
                const SDL_FRect b{bx, by, kButton, kButton};
                bx -= kButton + kButtonGap;
                const bool hot = focused(index, static_cast<int>(i), which);
                box(r, b, buttonStyle, hot ? Look::Hover : Look::Idle, boxAlpha(buttonStyle, alpha));
                const Style::State& over = buttonStyle.hover;
                const SDL_Color c = withAlpha(hot && over.color ? *over.color : buttonStyle.color, alpha * buttonStyle.opacity);
                const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f, k = 7.0f;
                if (which == Button::Back) { // |<  go back
                    draw::rect(r, {cx - k, cy - k, 2.5f, 2 * k}, c);
                    draw::quad(r, {cx - k + 3.5f, cy}, {cx + k, cy - k}, {cx + k, cy + k}, {cx - k + 3.5f, cy}, c);
                } else { // >  play
                    draw::quad(r, {cx - k * 0.7f, cy - k}, {cx + k, cy}, {cx + k, cy}, {cx - k * 0.7f, cy + k}, c);
                }
                if (inside(b, rectOf(index))) targets_.push_back({{index, static_cast<int>(i), which}, b, false});
            };
            if (backs && game.canRollbackTo(i)) button(Button::Back);
            else if (backs) bx -= kButton + kButtonGap; // (keeps the voice buttons in one column)
            if (voices && entries[i].voiced()) button(Button::Voice);
        }
        y += h + gap;
    }
}

// The lines on the page (textbox page), from the top; the newest is the one being typed. When
// they don't all fit, the oldest go. Names go in a column name_w wide, or above their line.
void ScreenView::drawPage(SDL_Renderer* r, std::size_t index, float alpha) {
    Game& game = engine_.game();
    const auto typed = game.line(def_->name); // the page's newest line, typing if it's the line on screen
    if (!typed) return;
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    const auto& lines = game.page();
    const Style& nameStyle = el.substyle("name_style");
    const float nameW = share(el, "name_w", true, 0.0f), gap = grid(el, {.gapY = 16.0f}).gapY;
    const float textX = rectOf(index).x + (nameW > 0.0f ? nameW + 24.0f : 0.0f), textW = rectOf(index).x + rectOf(index).w - textX;
    Font& font = styleFont(s);
    st.items.resize(lines.size());
    st.extra.resize(lines.size());
    std::vector<float> heights(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        st.items[i].set(styleFont(nameStyle), lines[i].speaker);
        st.extra[i].set(font, lines[i].text, textW, s.lineSpacing);
        st.extra[i].setSpans(lines[i].spans);
        const float name = lines[i].speaker.empty() ? 0.0f : st.items[i].size().y;
        heights[i] = nameW > 0.0f ? std::max(name, st.extra[i].size().y) : name + st.extra[i].size().y;
    }
    std::size_t first = lines.size(); // the oldest line that still fits
    for (float used = 0.0f; first > 0 && used + heights[first - 1] <= rectOf(index).h; used += heights[--first] + gap) {}
    if (first == lines.size() && !lines.empty()) first = lines.size() - 1;
    float y = rectOf(index).y;
    for (std::size_t i = first; i < lines.size(); ++i) {
        const PageLine& line = lines[i];
        const bool typing = i + 1 == lines.size();
        if (!line.speaker.empty()) {
            if (nameW > 0.0f) st.items[i].drawIn(r, {rectOf(index).x, y, nameW, 0}, 1.0f, 0.0f, withAlpha(line.color, alpha * nameStyle.opacity), TextBlock::kAll, nameStyle.shadow);
            else st.items[i].draw(r, rectOf(index).x, y, withAlpha(line.color, alpha * nameStyle.opacity), TextBlock::kAll, nameStyle.shadow);
        }
        const float textY = nameW > 0.0f || line.speaker.empty() ? y : y + st.items[i].size().y;
        const std::size_t shown = typing ? std::max(typed->revealed, std::min(st.extra[i].visible(typed->revealed, typed->mode), typed->limit))
                                         : TextBlock::kAll;
        st.extra[i].draw(r, textX, textY, withAlpha(line.textColor.value_or(s.color), alpha * s.opacity), shown, s.shadow);
        y += heights[i] + gap;
    }
}

void ScreenView::drawPages(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    SlotBrowser& browser = engine_.slots();
    const auto& pages = tools_ && browser.pages().empty() ? samplePages() : browser.pages(); // (the editor's preview: made-up pages)
    const Grid g = grid(el, {.itemW = 44.0f, .itemH = rectOf(index).h > 0 ? rectOf(index).h : 34.0f, .gapX = 6.0f});
    const float wideW = share(el, "wide_w", true, 76.0f); // the Auto and Quick tabs
    st.items.resize(pages.size());
    float x = rectOf(index).x;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const bool numbered = !pages[i].empty() && std::isdigit(static_cast<unsigned char>(pages[i][0]));
        const std::string label = pages[i] == "auto" ? el.option("auto", "auto") : pages[i] == "quick" ? el.option("quick", "quick") : pages[i]; // (auto= quick=: what those tabs say)
        const SDL_FRect rr{x, rectOf(index).y, numbered ? g.itemW : wideW, g.itemH};
        const Look look = focused(index, static_cast<int>(i)) ? Look::Hover : i == browser.page() ? Look::Selected : Look::Idle;
        box(r, rr, s, look, boxAlpha(s, alpha));
        text(r, st.items[i], label, rr, s, textColor(s, look), alpha * s.opacity);
        targets_.push_back({{index, static_cast<int>(i)}, rr, true});
        x += rr.w + g.gapX;
    }
}


void ScreenView::drawSlots(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    SlotBrowser& browser = engine_.slots();
    const auto& cards = tools_ && browser.cards().empty() ? sampleCards() : browser.cards(); // (the editor's preview: made-up saves)
    const Style& titleStyle = el.substyle("title_style");
    const Style& textStyle = el.substyle("text_style");
    const Style& emptyStyle = el.substyle("empty_style");
    const Grid g = grid(el, {.columns = 3, .itemW = 360.0f, .itemH = 262.0f, .gapX = 40.0f, .gapY = 16.0f});
    const bool deletable = el.option("delete", "yes") != "no";

    st.items.resize(cards.size());
    st.extra.resize(cards.size());
    st.cache.resize(cards.size() * 2);
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const auto& card = cards[i];
        const SDL_FRect rr = g.cell(rectOf(index), i);
        const Look look = focused(index, static_cast<int>(i)) ? Look::Hover : Look::Idle;
        box(r, rr, s, look, boxAlpha(s, alpha));

        const float pad = s.padding;
        const SDL_FRect thumb{rr.x + pad, rr.y + pad, rr.w - 2 * pad, (rr.w - 2 * pad) * engine_.canvas().h / engine_.canvas().w};
        if (card.thumbnail) {
            SDL_SetTextureAlphaModFloat(card.thumbnail.get(), alpha);
            SDL_RenderTexture(r, card.thumbnail.get(), nullptr, &thumb);
        } else {
            box(r, thumb, emptyStyle, Look::Idle, boxAlpha(emptyStyle, alpha)); // (empty, or without a picture)
        }

        const std::string title = card.info.exists ? std::format("{}  ·  {}", i + 1, formatTime(card.info.time)) : std::to_string(i + 1);
        const Style& ts = card.info.exists ? titleStyle : emptyStyle;
        st.items[i].set(styleFont(ts), title);
        const float infoY = thumb.y + thumb.h + 6.0f;
        st.items[i].draw(r, rr.x + 10, infoY, withAlpha(ts.color, alpha * ts.opacity), TextBlock::kAll, ts.shadow);

        Font& textFont = styleFont(textStyle);
        if (st.cache[i * 2] != card.info.summary || st.extra[i].text().empty() != card.info.summary.empty()) {
            st.cache[i * 2] = card.info.summary;
            st.extra[i].set(textFont, ellipsize(textFont, card.info.summary, rr.w - 20.0f));
        }
        st.extra[i].draw(r, rr.x + 10, infoY + st.items[i].size().y + 2.0f,
                         withAlpha(textStyle.color, alpha * textStyle.opacity), TextBlock::kAll, textStyle.shadow);
        targets_.push_back({{index, static_cast<int>(i)}, rr, true});

        if (deletable && card.info.exists && pointInRect(mouse_.x, mouse_.y, rr)) {
            const SDL_FRect d{thumb.x + thumb.w - 34, thumb.y + 6, 28, 28};
            const bool hot = focused(index, static_cast<int>(i), Button::Delete);
            draw::rect(r, d, withAlpha(hot ? SDL_Color{190, 60, 70, 235} : SDL_Color{0, 0, 0, 170}, alpha));
            const float cx = d.x + d.w * 0.5f, cy = d.y + d.h * 0.5f, k = 7.0f, t = 1.5f;
            const SDL_Color c = withAlpha({240, 240, 245, 255}, alpha);
            draw::quad(r, {cx - k - t, cy - k + t}, {cx - k + t, cy - k - t}, {cx + k + t, cy + k - t}, {cx + k - t, cy + k + t}, c);
            draw::quad(r, {cx + k - t, cy - k - t}, {cx + k + t, cy - k + t}, {cx - k + t, cy + k + t}, {cx - k - t, cy + k - t}, c);
            targets_.push_back({{index, static_cast<int>(i), Button::Delete}, d, false});
        }
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void ScreenView::clicked(const Element& el) {
    if (!el.style.clickSound.empty()) engine_.playSound(el.style.clickSound);
}

void ScreenView::run(const Element& el) {
    clicked(el);
    engine_.run(el.actions, el.confirm);
}

std::optional<std::pair<Commands, std::string>> ScreenView::takePressed() {
    if (!playing_ || *playing_ >= def_->elements.size()) return std::nullopt;
    if (engine_.frameNumber() > drawnFrame_ + 1 || !drawn(*playing_)) { // (its screen closed, or it was hidden: the click goes with it)
        playing_.reset();
        return std::nullopt;
    }
    const Element& el = def_->elements[*playing_];
    const auto& since = state_[*playing_].clicked;
    if (since && el.clickAnimation && now() - *since < el.clickAnimation->total()) return std::nullopt;
    playing_.reset();
    return std::pair{el.actions, el.confirm};
}

void ScreenView::setFocus(const Target& t) {
    if (focus_ == t.spot) return;
    focus_ = t.spot;
    if (t.spot.button != Button::None) return;
    const Element& el = def_->elements[t.spot.element];
    if (!el.style.hoverSound.empty()) engine_.playSound(el.style.hoverSound);
    if (el.kind == ElementKind::Slots) engine_.slots().selected = static_cast<std::size_t>(t.spot.item);
}

void ScreenView::hold(std::size_t element) {
    const Element& el = def_->elements[element];
    held_ = element;
    heldMoved_ = false;
    pressed_ = mouse_;
    grab_ = {mouse_.x - rectOf(element).x, mouse_.y - rectOf(element).y};
    clicked(el);
}

void ScreenView::drop(const Element& target) {
    const std::string what = held();
    held_.reset();
    clicked(target);
    engine_.drop(what, target.drop);
}

void ScreenView::activate(const Target& t, std::optional<float> mouseX) {
    const Element& el = def_->elements[t.spot.element];
    const auto item = static_cast<std::size_t>(t.spot.item);
    if (el.kind == ElementKind::Button) { // dragging: take one (or put it back), drop it on a part that takes it
        if (!el.option("drag").empty()) return held_ == t.spot.element ? held_.reset() : hold(t.spot.element);
        if (held_ && !el.drop.empty()) return drop(el);
        held_.reset();
        if (el.clickAnimation && el.clickAnimation->total() > 0.0f && !still()) { // (it plays, then it does what it does)
            if (playing_) return;
            clicked(el);
            state_[t.spot.element].clicked = now();
            playing_ = t.spot.element;
            return;
        }
        return run(el);
    }
    clicked(el);
    switch (el.kind) {
    case ElementKind::Choices:
        engine_.game().choose(item);
        break;
    case ElementKind::Pages:
        engine_.slots().setPage(item);
        break;
    case ElementKind::Gallery:
        engine_.viewCg(item);
        engine_.run(el.actions);
        break;
    case ElementKind::Control:
        engine_.startRebind(el.text, Engine::slotOf(el));
        break;
    case ElementKind::Songs:
        engine_.playSong(item);
        break;
    case ElementKind::Tips:
        engine_.viewTip(item);
        engine_.run(el.actions);
        break;
    case ElementKind::Chart: // a place (or an option not tried yet: its choice)
        if (const auto& ids = state_[t.spot.element].ids; item < ids.size() && !ids[item].empty())
            engine_.goToPlace(ids[item], def_->elements[t.spot.element].option("confirm"));
        break;
    case ElementKind::History:
        if (t.spot.button == Button::Back) engine_.historyBack(item);
        else if (t.spot.button == Button::Voice) engine_.historyVoice(item);
        break;
    case ElementKind::Phone:
        engine_.reply(item, static_cast<std::size_t>(t.spot.option));
        break;
    case ElementKind::Contacts:
        if (const auto people = engine_.game().contacts(); item < people.size()) {
            engine_.viewContact(people[item].name);
            engine_.run(el.actions);
        }
        break;
    case ElementKind::Slots:
        if (t.spot.button == Button::Delete) engine_.slotDelete(item, el.option("delete_confirm"));
        else engine_.slotPressed(item, el.option("confirm"), el.option("load_confirm"));
        break;
    case ElementKind::Slider:
        if (mouseX) {
            dragging_ = t.spot.element;
            slide(t.spot.element, *mouseX);
        }
        break;
    default:
        break;
    }
}

void ScreenView::slide(std::size_t element, float x) {
    const Element& el = def_->elements[element];
    const SDL_FRect at = sliderBox(element);
    const float t = std::clamp((x - at.x) / std::max(1.0f, at.w), 0.0f, 1.0f);
    if (const SettingControl* c = findSettingControl(el.option("setting"))) { // a player setting
        c->setValue(engine_.settings(), t);
        engine_.settingsChanged(false);
        return;
    }
    const float lo = number(el, "min", 0.0f), hi = number(el, "max", 100.0f), step = std::max(1.0f, number(el, "step", 1.0f));
    const int value = static_cast<int>(lo + std::round((hi - lo) * t / step) * step);
    if (toInt(engine_.game().variable(el.option("var"))) != value) engine_.game().setVariable(el.option("var"), value);
}

void ScreenView::navigate(int dx, int dy) {
    const Target* current = focusedTarget();
    const Target* best = nullptr;
    float bestScore = 0.0f;
    for (const auto& t : targets_) {
        if (!t.keyboard || &t == current) continue;
        if (!current) {
            best = &t; // nothing focused yet: start at the first control
            break;
        }
        const SDL_FPoint a = center(current->rect), b = center(t.rect);
        const float along = dx != 0 ? (b.x - a.x) * static_cast<float>(dx) : (b.y - a.y) * static_cast<float>(dy);
        const float across = dx != 0 ? std::abs(b.y - a.y) : std::abs(b.x - a.x);
        if (along <= 1.0f) continue;
        const float score = along + across * 2.0f;
        if (!best || score < bestScore) {
            best = &t;
            bestScore = score;
        }
    }
    if (best) {
        setFocus(*best);
        reveal(*best);
    }
}

bool ScreenView::scrollWidgets(const SDL_Event& e) {
    const bool wheel = e.type == SDL_EVENT_MOUSE_WHEEL;
    for (std::size_t i = 0; i < def_->elements.size(); ++i) {
        const Element& el = def_->elements[i];
        if (!live(i)) continue;
        float& scroll = state_[i].scroll;
        switch (el.kind) {
        case ElementKind::Phone: // (up and down go between its answers)
            if (wheel) scroll += e.wheel.y * 60.0f;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_PAGEUP) scroll += rectOf(i).h * 0.8f;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_PAGEDOWN) scroll -= rectOf(i).h * 0.8f;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_HOME) scroll = 1e9f;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_END) scroll = 0.0f;
            else break;
            return true;
        case ElementKind::History:
            if (wheel) {
                if (e.wheel.y < 0 && scroll <= 0.0f && el.option("close_on_bottom", "no") == "yes") engine_.run({cmd::Ui{.verb = "close"}});
                scroll += e.wheel.y * 60.0f;
                return true;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                switch (e.key.key) {
                case SDLK_UP: scroll += 60.0f; return true;
                case SDLK_DOWN: scroll -= 60.0f; return true;
                case SDLK_PAGEUP: scroll += rectOf(i).h * 0.8f; return true;
                case SDLK_PAGEDOWN: scroll -= rectOf(i).h * 0.8f; return true;
                case SDLK_HOME: scroll = 1e9f; return true;
                case SDLK_END: scroll = 0.0f; return true;
                default: break;
                }
            }
            break;
        case ElementKind::Chart: // up and down; sideways with Shift
            if (!wheel) break;
            if (e.wheel.x != 0.0f || (SDL_GetModState() & SDL_KMOD_SHIFT)) state_[i].panX += (e.wheel.x != 0.0f ? -e.wheel.x : e.wheel.y) * 60.0f;
            else scroll -= e.wheel.y * 60.0f;
            return true;
        case ElementKind::Gallery:
        case ElementKind::Endings:
        case ElementKind::Songs:
        case ElementKind::Achievements:
        case ElementKind::Tips:
        case ElementKind::Contacts: { // a row at a time
            if (!wheel) break;
            const Grid g = grid(el, {.itemH = el.kind == ElementKind::Gallery ? 202.0f : 64.0f, .gapY = 8.0f});
            scroll -= e.wheel.y * (g.itemH + g.gapY);
            return true;
        }
        case ElementKind::Slots:
        case ElementKind::Pages:
            if (!wheel) break;
            engine_.slots().turn(e.wheel.y < 0 ? +1 : -1);
            return true;
        case ElementKind::Choices: // 1-9 pick a choice
            if (e.type == SDL_EVENT_KEY_DOWN && engine_.game().showingChoices() && e.key.key >= SDLK_1 && e.key.key <= SDLK_9 &&
                e.key.key - SDLK_1 < engine_.game().choices().size()) {
                engine_.game().choose(e.key.key - SDLK_1);
                return true;
            }
            break;
        default:
            break;
        }
    }
    if (wheel) // a container with scroll=yes under the mouse (the innermost): what's in it scrolls (sideways: a trackpad, or Shift)
        if (const auto found = scrollableAt(mouse_.x, mouse_.y)) {
            ElementState& st = state_[*found];
            st.glideX = st.glideY = 0.0f;
            if (e.wheel.x != 0.0f) st.scrollX += e.wheel.x * 60.0f;
            else if (SDL_GetModState() & SDL_KMOD_SHIFT) st.scrollX -= e.wheel.y * 60.0f;
            else st.scroll -= e.wheel.y * 60.0f;
            return true;
        }
    return false;
}

bool ScreenView::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_MOTION) { // (where the pointer is, as this screen saw it last)
        mouse_ = {e.motion.x, e.motion.y};
        seenMotion_ = engine_.pointerMoves();
    }
    if (leftSince_) return def_->modal && isInput(e); // (closing: it only plays its hide_animation=)
    // Hotkeys: `key` elements, and buttons with key= (as if clicked: a click animation plays first).
    for (std::size_t i = 0; i < def_->elements.size(); ++i) {
        const Element& el = def_->elements[i];
        if (!el.key || !el.key->matches(e) || !live(i) || (el.kind == ElementKind::Button && !enabled(el))) continue;
        if (el.kind == ElementKind::Button) activate({{i}, rectOf(i), false}, std::nullopt);
        else run(el);
        return true;
    }

    // Controllers move around menus like the arrow keys: the d-pad, A to press, B to go back (Esc),
    // the shoulder buttons a page up or down.
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        SDL_Keycode key = 0;
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: key = SDLK_UP; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: key = SDLK_DOWN; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: key = SDLK_LEFT; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: key = SDLK_RIGHT; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: key = SDLK_RETURN; break;
        case SDL_GAMEPAD_BUTTON_EAST: key = SDLK_ESCAPE; break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: key = SDLK_PAGEUP; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: key = SDLK_PAGEDOWN; break;
        default: break;
        }
        if (key != 0) {
            SDL_Event press{};
            press.type = SDL_EVENT_KEY_DOWN;
            press.key.key = key;
            press.key.down = true;
            if (handleEvent(press)) return true;
        }
    }

    if (dragging_) {
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            mouse_ = {e.motion.x, e.motion.y};
            slide(*dragging_, e.motion.x);
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            const Element& el = def_->elements[*dragging_];
            dragging_.reset();
            if (const SettingControl* c = findSettingControl(el.option("setting")); c && c->testSound) engine_.settingsChanged(true);
            if (!el.actions.empty()) run(el);
            return true;
        }
    }

    if (scrollDrag_) { // a container's parts dragged (or its scrollbar's thumb): it scrolls
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            mouse_ = {e.motion.x, e.motion.y};
            ElementState& st = state_[*scrollDrag_];
            const float dx = e.motion.x - scrollFrom_.x, dy = e.motion.y - scrollFrom_.y;
            if (scrollBarDrag_) {
                const SDL_FRect bar = barOf(*scrollDrag_, scrollBarAcross_), thumb = thumbOf(*scrollDrag_, scrollBarAcross_), box = rectOf(*scrollDrag_);
                if (scrollBarAcross_ && bar.w > thumb.w) st.scrollX = scrollWasX_ + dx * (st.reachX - box.w) / (bar.w - thumb.w);
                else if (!scrollBarAcross_ && bar.h > thumb.h) st.scroll = scrollWas_ + dy * (st.reach - box.h) / (bar.h - thumb.h);
                return true;
            }
            if (!scrollDragMoved_ && std::hypot(dx, dy) > 6.0f) {
                if (held_) { // (a button's own dragging: that, not scrolling)
                    scrollDrag_.reset();
                    tapping_.reset();
                    return false;
                }
                scrollDragMoved_ = true;
                pressing_.reset(); // (not a click, nor a tap)
                tapping_.reset();
            }
            if (scrollDragMoved_) {
                const Element& el = def_->elements[*scrollDrag_];
                st.scroll = scrollWas_ + dy * el.dragDirection();
                (el.kind == ElementKind::Chart ? st.panX : st.scrollX) = el.kind == ElementKind::Chart ? scrollWasX_ + dx : scrollWasX_ - dx;
                dragTrail_.emplace_back(now(), SDL_FPoint{e.motion.x, e.motion.y});
                if (dragTrail_.size() > 8) dragTrail_.erase(dragTrail_.begin());
                return true;
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            const std::size_t box = *std::exchange(scrollDrag_, std::nullopt);
            const bool moved = std::exchange(scrollDragMoved_, false), bar = std::exchange(scrollBarDrag_, false);
            const Element& el = def_->elements[box];
            if (moved && el.glides(engine_.config().glide) && dragTrail_.size() >= 2) { // let go while moving: it glides on, as fast as it went over the last moments
                const auto& [then, at] = dragTrail_.back();
                auto from = std::ranges::find_if(dragTrail_, [&](const auto& s) { return then - s.first <= 0.15f; });
                if (from == dragTrail_.end() - 1) --from;
                const float dt = std::max(0.005f, then - from->first);
                const float speed = std::max(0.0f, el.number("glide_speed", engine_.config().glideSpeed)); // (glide_speed=150%: faster than it was let go; none: the game's)
                ElementState& st = state_[box];
                st.glideX = std::clamp((at.x - from->second.x) / dt, -5000.0f, 5000.0f) * speed * (el.kind == ElementKind::Chart ? 1.0f : -1.0f);
                st.glideY = std::clamp((at.y - from->second.y) / dt, -5000.0f, 5000.0f) * speed * el.dragDirection();
                st.glideAt = now();
            }
            dragTrail_.clear();
            if (moved || bar) {
                tapping_.reset();
                return true;
            }
            if (tapping_) { // (a list's item, pressed and let go without dragging: tapped)
                const Target hit = *std::exchange(tapping_, std::nullopt);
                if (const Target* t = targetAt(e.button.x, e.button.y); t && t->spot == hit.spot) activate(hit, e.button.x);
                return true;
            }
        }
    }

    if (handleInput(e)) return true;
    if (scrollWidgets(e)) return true;

    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        mouse_ = {e.motion.x, e.motion.y};
        if (held_ && std::hypot(mouse_.x - pressed_.x, mouse_.y - pressed_.y) > 6.0f) heldMoved_ = true;
        if (const Target* t = targetAt(e.motion.x, e.motion.y)) setFocus(*t);
        else focus_.reset();
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP: // a button is clicked when the mouse comes up on it; let go of what's dragged: on a part that takes it, or back where it was
        if (e.button.button == SDL_BUTTON_LEFT && pressing_) {
            const std::size_t was = *std::exchange(pressing_, std::nullopt);
            const Target* t = engine_.frameNumber() <= drawnFrame_ + 1 ? targetAt(e.button.x, e.button.y) : nullptr; // (not if it closed meanwhile)
            if (t && t->spot.element == was && t->spot.button == Button::None) {
                const Target hit = *t;
                activate(hit, e.button.x);
            }
            return true;
        }
        if (e.button.button == SDL_BUTTON_LEFT && held_ && heldMoved_) {
            const Target* t = targetAt(e.button.x, e.button.y);
            if (t && !def_->elements[t->spot.element].drop.empty()) drop(def_->elements[t->spot.element]);
            else held_.reset();
            return true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) {
            if (const auto box = scrollableAt(e.button.x, e.button.y)) { // (dragging scrolls it: its parts, or its scrollbar's thumb)
                scrollDrag_ = box;
                scrollFrom_ = {e.button.x, e.button.y};
                scrollWas_ = state_[*box].scroll;
                scrollWasX_ = def_->elements[*box].kind == ElementKind::Chart ? state_[*box].panX : state_[*box].scrollX;
                state_[*box].glideX = state_[*box].glideY = 0.0f; // (a touch stops it gliding)
                dragTrail_.assign(1, {now(), scrollFrom_});
                scrollDragMoved_ = false;
                const Element& el = def_->elements[*box];
                const bool bar = lower(el.option("scrollbar", "no")) == "yes";
                scrollBarAcross_ = bar && state_[*box].reachX > rectOf(*box).w && pointInRect(e.button.x, e.button.y, barOf(*box, true));
                scrollBarDrag_ = scrollBarAcross_ || (bar && state_[*box].reach > rectOf(*box).h && pointInRect(e.button.x, e.button.y, barOf(*box, false)));
                if (scrollBarDrag_) return true;
            }
            if (const Target* t = targetAt(e.button.x, e.button.y)) {
                const Target hit = *t; // activate() may redraw-free mutate targets_ indirectly
                setFocus(hit);
                mouse_ = {e.button.x, e.button.y};
                const Element& el = def_->elements[hit.spot.element];
                if (el.kind == ElementKind::Button && hit.spot.button == Button::None && el.option("drag").empty() && !(held_ && !el.drop.empty())) {
                    pressing_ = hit.spot.element; // (clicked when the mouse comes up on it)
                    return true;
                }
                if (scrollDrag_ && el.dragDirection() != 0.0f) { // (a list's item: when the mouse comes up on it, unless that's a drag)
                    tapping_ = hit;
                    return true;
                }
                activate(hit, e.button.x);
                return true;
            }
            if (held_) { // a click elsewhere puts it back
                held_.reset();
                return true;
            }
            if (scrollDrag_) return true; // (a list or a container pressed on: dragging scrolls it)
        }
        break;

    case SDL_EVENT_KEY_DOWN:
        if (held_ && e.key.key == SDLK_ESCAPE) { // puts it back
            held_.reset();
            return true;
        }
        if (!def_->keyboard) break;
        switch (e.key.key) {
        case SDLK_LEFT:
        case SDLK_RIGHT: {
            const int d = e.key.key == SDLK_LEFT ? -1 : +1;
            const Target* t = focusedTarget();
            const std::size_t at = t ? t->spot.element : 0;
            if (t && def_->elements[at].kind == ElementKind::Slider) adjustSlider(at, d);
            else navigate(d, 0);
            return true;
        }
        case SDLK_UP:
        case SDLK_DOWN:
            navigate(0, e.key.key == SDLK_UP ? -1 : +1);
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            if (const Target* t = focusedTarget()) {
                const Target hit = *t;
                activate(hit, std::nullopt);
                return true;
            }
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }
    return def_->modal && isInput(e);
}

// ---------------------------------------------------------------------------
// Variable widgets: input, bar, slider
// ---------------------------------------------------------------------------

void ScreenView::drawInput(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const std::string var = inputTarget(el);
    if (var.empty()) return;
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    const bool on = enabled(el);
    const Look look = on ? Look::Hover : Look::Disabled;
    box(r, rectOf(index), s, look, boxAlpha(s, alpha));

    const std::string value = toString(engine_.game().variable(var));
    const bool empty = value.empty();
    const std::string shown = empty ? el.option("hint") : value;
    const SDL_Color color = empty ? withAlpha(textColor(s, look), 0.5f) : textColor(s, look);
    text(r, st.text, shown, rectOf(index), s, color, alpha * s.opacity);

    if (on) {
        engine_.wantTextInput();
        if (std::fmod(now(), 1.0f) < 0.6f) { // blinking caret after the text
            Font& font = styleFont(s);
            const float w = empty ? 0.0f : font.measure(value), h = font.lineHeight();
            const float left = rectOf(index).x + s.padding + (s.align == Align::Center ? (rectOf(index).w - 2 * s.padding - w) * 0.5f
                                                       : s.align == Align::End ? rectOf(index).w - 2 * s.padding - w : 0.0f);
            const float top = rectOf(index).y + (rectOf(index).h > 0 ? (rectOf(index).h - h) * 0.5f : s.padding);
            draw::rect(r, {left + w + 2, top, 2, h}, withAlpha(textColor(s, Look::Hover), alpha * s.opacity));
        }
    }
}

bool ScreenView::handleInput(const SDL_Event& e) {
    if (e.type != SDL_EVENT_TEXT_INPUT && e.type != SDL_EVENT_KEY_DOWN) return false;
    for (std::size_t i = 0; i < def_->elements.size(); ++i) {
        const Element& el = def_->elements[i];
        if (el.kind != ElementKind::Input || !live(i) || !enabled(el)) continue;
        const std::string var = inputTarget(el);
        if (var.empty()) continue;
        std::string value = toString(engine_.game().variable(var));
        const int asked = engine_.game().request().max;
        const int max = static_cast<int>(number(el, "max", el.option("var").empty() && asked > 0 ? static_cast<float>(asked) : 64.0f));
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            std::string typed = e.text.text ? e.text.text : "";
            std::erase_if(typed, [](char c) { return c == '\n' || c == '\r' || c == '{' || c == '}'; });
            for (std::size_t n = 0; n < typed.size();) { // add codepoints until the limit
                const auto len = utf8::offset(std::string_view(typed).substr(n), 1);
                if (max > 0 && static_cast<int>(utf8::length(value)) >= max) break;
                value += typed.substr(n, len);
                n += std::max<std::size_t>(len, 1);
            }
            engine_.game().setVariable(var, value);
            return true;
        }
        if (e.key.key == SDLK_BACKSPACE) {
            if (!value.empty()) {
                value.erase(utf8::offset(value, utf8::length(value) - 1));
                engine_.game().setVariable(var, value);
            }
            return true;
        }
        if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && !e.key.repeat && !el.actions.empty()) {
            run(el);
            return true;
        }
        return false;
    }
    return false;
}

void ScreenView::drawBar(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    const float max = std::max(1.0f, number(el, "max", 100.0f));
    const float f = std::clamp(number(el, "value", 0.0f) / max, 0.0f, 1.0f);
    const float a = boxAlpha(s, alpha);
    const SDL_FRect rr = rectOf(index);
    draw::rect(r, rr, withAlpha(el.color("track", s.bg.a ? s.bg : SDL_Color{60, 64, 92, 255}), a));
    const bool vertical = el.option("vertical", "no") == "yes";
    const SDL_FRect fill = vertical ? SDL_FRect{rr.x, rr.y + rr.h * (1.0f - f), rr.w, rr.h * f} : SDL_FRect{rr.x, rr.y, rr.w * f, rr.h};
    draw::rect(r, fill, withAlpha(el.color("fill", s.color), a));
    if (s.border.a > 0) draw::outline(r, rr, withAlpha(s.border, a));
}

void ScreenView::drawSlider(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    const bool on = enabled(el);
    const float a = boxAlpha(s, alpha) * (on ? 1.0f : 0.35f);
    const float lo = number(el, "min", 0.0f), hi = number(el, "max", 100.0f);
    const float value = static_cast<float>(toNumber(engine_.game().variable(el.option("var"))));
    const SettingControl* c = findSettingControl(el.option("setting"));
    const float f = c ? std::clamp(c->value(engine_.settings()), 0.0f, 1.0f) : hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    const SDL_FRect rr = sliderBox(index);
    const float my = rr.y + rr.h * 0.5f;
    const bool hot = focused(index, 0) || dragging_ == index;
    draw::rect(r, {rr.x, my - 2, rr.w, 4}, withAlpha(el.color("track", {60, 64, 92, 255}), a));
    draw::rect(r, {rr.x, my - 2, rr.w * f, 4}, withAlpha(el.color("fill", {150, 170, 255, 255}), a));
    draw::circle(r, rr.x + rr.w * f, my, hot ? 10.0f : 8.0f, withAlpha(el.color("knob", s.color), a));
    if (on) targets_.push_back({{index}, rr, true});
}

// A slider's box (inside the part it's in, its place there when it was drawn).
SDL_FRect ScreenView::sliderBox(std::size_t element) const {
    const auto it = std::ranges::find(targets_, Spot{element}, &Target::spot);
    return it != targets_.end() ? it->rect : rectOf(element);
}

void ScreenView::adjustSlider(std::size_t element, int delta) {
    const Element& el = def_->elements[element];
    if (!enabled(el)) return;
    if (const SettingControl* c = findSettingControl(el.option("setting"))) { // a player setting: a twentieth at a time
        c->setValue(engine_.settings(), std::clamp(c->value(engine_.settings()) + static_cast<float>(delta) * 0.05f, 0.0f, 1.0f));
        engine_.settingsChanged(c->testSound);
        return;
    }
    const float lo = number(el, "min", 0.0f), hi = number(el, "max", 100.0f), step = std::max(1.0f, number(el, "step", 1.0f));
    const float value = static_cast<float>(toNumber(engine_.game().variable(el.option("var"))));
    const int next = static_cast<int>(std::clamp(value + step * static_cast<float>(delta), std::min(lo, hi), std::max(lo, hi)));
    engine_.game().setVariable(el.option("var"), next);
    if (!el.actions.empty()) run(el);
}

} // namespace vn
