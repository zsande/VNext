#include "Assets.hpp"
#include "System.hpp"

#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <pl_mpeg.h>
#include <random>

namespace vn {

// ---------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------

namespace {

// One texture rectangle; atlas cells are drawn half a pixel in, so neighbours don't bleed in.
void drawCell(SDL_Renderer* r, SDL_Texture* texture, SDL_FRect src, bool cell, const SDL_FRect& dst, float alpha, double angle,
              const SDL_FPoint* pivot, bool flip, SDL_Color tint) {
    if (cell) src = {src.x + 0.5f, src.y + 0.5f, std::max(0.0f, src.w - 1.0f), std::max(0.0f, src.h - 1.0f)};
    SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaModFloat(texture, alpha);
    SDL_RenderTextureRotated(r, texture, &src, &dst, angle, pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

// A picture's file: its name with one of these endings, or as written (with its own).
constexpr auto kImageExtensions = [] {
    std::array<std::string_view, kImageTypes.size() + 1> out{};
    std::ranges::copy(kImageTypes, out.begin());
    return out;
}();

// The name a picture is kept under: with colour effects, a copy of its own.
std::string cacheKey(std::string_view name, unsigned looks) {
    return looks ? std::format("{}#{}", name, looks) : std::string(name);
}

// A picture file as RGBA pixels (to read or change them).
SurfacePtr loadPixels(const GameFiles& files, const std::string& file) {
    const SurfacePtr loaded{IMG_Load_IO(files.open(file).release(), true)};
    return loaded ? SurfacePtr{SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32)} : nullptr;
}

} // namespace

SDL_Color looked(SDL_Color c, unsigned looks) {
    const float r = c.r, g = c.g, b = c.b;
    auto u8 = [](float v) { return static_cast<Uint8>(std::clamp(v, 0.0f, 255.0f)); };
    if (looks & kLookSepia) c = {u8(0.393f * r + 0.769f * g + 0.189f * b), u8(0.349f * r + 0.686f * g + 0.168f * b), u8(0.272f * r + 0.534f * g + 0.131f * b), c.a};
    else if (looks & kLookGrayscale) c.r = c.g = c.b = u8(0.299f * r + 0.587f * g + 0.114f * b);
    if (looks & kLookInvert) c = {static_cast<Uint8>(255 - c.r), static_cast<Uint8>(255 - c.g), static_cast<Uint8>(255 - c.b), c.a};
    return c;
}

Picture Picture::crop(const SDL_FRect& part) const {
    Picture p = *this;
    p.src = {src.x + part.x, src.y + part.y, part.w, part.h};
    p.overlayCount = 0;
    return p;
}

void Picture::drawLayers(SDL_Renderer* r, const SDL_FRect& dst, float alpha, double angle, const SDL_FPoint* pivot, bool flip, SDL_Color tint) const {
    const SDL_FPoint p = pivot ? *pivot : SDL_FPoint{dst.w * 0.5f, dst.h * 0.5f};
    drawCell(r, texture, src, frame, dst, alpha, angle, &p, flip, tint);
    const float sx = dst.w / src.w, sy = dst.h / src.h;
    for (std::size_t i = 0; i < overlayCount; ++i) { // turned and mirrored with the picture
        const Overlay& o = overlays[i];
        const float x = flip ? src.w - o.at.x - o.src.w : o.at.x;
        const SDL_FRect od{dst.x + x * sx, dst.y + o.at.y * sy, o.src.w * sx, o.src.h * sy};
        const SDL_FPoint op{dst.x + p.x - od.x, dst.y + p.y - od.y};
        drawCell(r, o.texture, o.src, true, od, alpha, angle, &op, flip, tint);
    }
}

void Picture::draw(SDL_Renderer* r, const SDL_FRect& dst, float alpha, double angle, const SDL_FPoint* pivot, bool flip, SDL_Color tint) const {
    if (!texture) return;
    SDL_Texture* together = overlayCount > 0 && alpha < 0.999f && assets
                                ? assets->scratch(static_cast<int>(std::ceil(src.w)), static_cast<int>(std::ceil(src.h)))
                                : nullptr;
    if (!together) return drawLayers(r, dst, alpha, angle, pivot, flip, tint);
    // See-through with parts on top: put it together first, or the parts would show darker.
    const SDL_FRect whole{0.0f, 0.0f, src.w, src.h};
    {
        const RenderTarget _(r, together);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        drawLayers(r, whole, 1.0f, 0.0, nullptr, false, tint);
    }
    SDL_SetTextureColorModFloat(together, alpha, alpha, alpha); // its colours are premultiplied
    SDL_SetTextureAlphaModFloat(together, alpha);
    SDL_RenderTextureRotated(r, together, &whole, &dst, angle, pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

float blinking(std::string_view who, float seconds, float every) {
    // One blink in each stretch of `every` seconds, somewhere random in it.
    every = std::max(every, 0.5f);
    const float stretch = std::floor(seconds / every);
    const std::uint64_t h = fnv1a(who, static_cast<std::uint64_t>(static_cast<std::int64_t>(stretch)) * 0x9E3779B97F4A7C15ull + 1);
    const float start = stretch * every + 0.1f * every + static_cast<float>(h % 1000) / 1000.0f * (every * 0.8f - 0.2f);
    auto shape = [](float t) { // closing, shut, opening
        if (t < 0.0f || t > 0.2f) return 0.0f;
        return t < 0.06f ? t / 0.06f : t < 0.1f ? 1.0f : 1.0f - (t - 0.1f) / 0.1f;
    };
    const bool twice = (h >> 32) % 4 == 0;
    return std::max(shape(seconds - start), twice ? shape(seconds - start - 0.32f) : 0.0f);
}

float flapping(std::string_view who, float seconds, float perSecond) {
    // One flap in each stretch of 1/perSecond seconds, somewhere in it.
    const float beats = seconds * std::max(perSecond, 0.1f);
    const float stretch = std::floor(beats);
    const std::uint64_t h = fnv1a(who, static_cast<std::uint64_t>(static_cast<std::int64_t>(stretch)) * 0x9E3779B97F4A7C15ull + 7);
    auto part = [&](int shift) { return static_cast<float>((h >> shift) % 1000) / 1000.0f; };
    const float length = 0.55f + 0.4f * part(0);         // of the stretch
    const float start = (1.0f - length) * part(10);
    const float wide = 0.55f + 0.45f * part(20);
    const float t = (beats - stretch - start) / length;
    return t < 0.0f || t > 1.0f ? 0.0f : wide * (0.5f - 0.5f * std::cos(t * 6.2831853f));
}

Assets::Assets(SDL_Renderer* renderer, std::string root, const GameFiles& files) : renderer_(renderer), root_(withSlash(std::move(root))), files_(files) {}

bool Assets::exists(const std::string& path) const {
    return files_.exists(path);
}

// The file `name` is in `folder` (or the language's own folder in it first, or the game folder),
// as written or with one of the `types` endings.
std::optional<std::string> Assets::find(std::string_view folder, std::string_view name, std::span<const std::string_view> types) const {
    std::vector<std::string> folders{std::string(folder), ""};
    if (!language_.empty()) folders.insert(folders.begin(), std::string(folder) + language_); // the language's own first
    for (const auto& in : folders) {
        if (auto file = path(std::format("{}{}", in, name)); exists(file)) return file;
        const std::string own = ownName(in, name, types);
        for (const std::string_view ext : types)
            if (auto file = path(std::format("{}{}{}", in, own, ext)); exists(file)) return file;
    }
    return std::nullopt;
}

std::string Assets::ownName(const std::string& folder, std::string_view name, std::span<const std::string_view> types) const {
    for (const std::string_view ext : types) // (as written: no need to look further)
        if (exists(path(std::format("{}{}{}", folder, name, ext)))) return std::string(name);
    auto [names, fresh] = ownNames_.try_emplace(folder);
    if (fresh)
        for (const std::string_view ext : types)
            for (const std::string& file : files_.list(path(folder), ext))
                if (const std::string stem = file.substr(0, file.size() - ext.size()); !file.contains('/') && scriptName(stem) != stem)
                    names->second.emplace(lower(scriptName(stem)), stem);
    const auto found = names->second.find(lower(name));
    return found != names->second.end() ? found->second : std::string(name);
}

std::optional<std::string> Assets::findAudio(std::string_view name) const {
    return find("audio/", name, kAudioTypes);
}

std::optional<std::string> Assets::findMovie(std::string_view name) const {
    return find("movies/", name, kVideoTypes);
}

void Assets::setLanguage(std::string id) {
    if (!id.empty()) id += '/';
    if (id == language_) return;
    language_ = std::move(id);
    clear();
}

std::string Assets::audio(std::string_view name) const {
    if (auto file = findAudio(name)) return *file;
    return name.find('.') == std::string_view::npos ? path(std::format("audio/{}.ogg", name)) : path(name);
}

SDL_Texture* Assets::scratch(int w, int h) {
    float cw = 0, ch = 0;
    if (scratch_) SDL_GetTextureSize(scratch_.get(), &cw, &ch);
    if (!scratch_ || cw < static_cast<float>(w) || ch < static_cast<float>(h)) {
        scratch_.reset(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, std::max(w, static_cast<int>(cw)),
                                         std::max(h, static_cast<int>(ch))));
        if (!scratch_) return nullptr;
        SDL_SetTextureBlendMode(scratch_.get(), SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    }
    SDL_SetTextureScaleMode(scratch_.get(), pixelArt_ ? SDL_SCALEMODE_PIXELART : SDL_SCALEMODE_LINEAR);
    return scratch_.get();
}

std::string Assets::imageFolder(std::string_view key) const {
    const bool own = !language_.empty() && std::ranges::any_of(kImageTypes, [&](std::string_view ext) { // the language's own picture
        return exists(path(std::format("images/{}{}{}", language_, key, ext)));
    });
    return own ? "images/" + language_ : "images/";
}

bool Assets::hasImageFile(std::string_view name) const {
    const std::string folder = imageFolder(name), own = ownName(folder, name, kImageTypes);
    return std::ranges::any_of(kImageTypes, [&](std::string_view ext) { return exists(path(std::format("{}{}{}", folder, own, ext))); }) ||
           (name.contains('.') && exists(path(std::format("{}{}", folder, name))));
}

SurfacePtr Assets::pixels(std::string_view name) const {
    const auto sub = subPictures_.find(lower(name));
    if (sub == subPictures_.end()) return filePixels(name);
    SurfacePtr all = filePixels(sub->second.image);
    if (!all) return nullptr;
    const SDL_Rect part{static_cast<int>(sub->second.rect.x), static_cast<int>(sub->second.rect.y), static_cast<int>(sub->second.rect.w),
                        static_cast<int>(sub->second.rect.h)};
    SurfacePtr out{SDL_CreateSurface(part.w, part.h, SDL_PIXELFORMAT_RGBA32)};
    if (!out) return nullptr;
    SDL_SetSurfaceBlendMode(all.get(), SDL_BLENDMODE_NONE);
    SDL_BlitSurface(all.get(), &part, out.get(), nullptr);
    return out;
}

SurfacePtr Assets::filePixels(std::string_view name) const {
    const std::string folder = imageFolder(name), own = ownName(folder, name, kImageTypes);
    for (const std::string_view ext : kImageExtensions)
        if (const auto file = path(std::format("{}{}{}", folder, own, ext)); exists(file))
            if (SurfacePtr loaded = loadPixels(files_, file)) return loaded;
    return nullptr;
}

SDL_Texture* Assets::image(std::string_view name, unsigned looks) {
    const std::string key = cacheKey(name, looks);
    if (auto it = images_.find(key); it != images_.end()) {
        it->second.lastFrame = frame_;
        return it->second.texture.get();
    }

    TexturePtr texture;
    bool found = false;
    const std::string folder = imageFolder(name), own = ownName(folder, name, kImageTypes);
    for (const std::string_view ext : kImageExtensions) {
        const auto file = path(std::format("{}{}{}", folder, own, ext));
        if (!exists(file)) continue;
        if (looks == 0) {
            texture = TexturePtr{IMG_LoadTexture_IO(renderer_, files_.open(file).release(), true)};
        } else if (SurfacePtr rgba = loadPixels(files_, file)) { // a copy with colour effects
            for (int y = 0; y < rgba->h; ++y) {
                auto* row = reinterpret_cast<SDL_Color*>(static_cast<Uint8*>(rgba->pixels) + static_cast<std::ptrdiff_t>(y) * rgba->pitch);
                for (int x = 0; x < rgba->w; ++x) row[x] = vn::looked(row[x], looks);
            }
            texture = TexturePtr{SDL_CreateTextureFromSurface(renderer_, rgba.get())};
        }
        if (texture) {
            SDL_SetTextureScaleMode(texture.get(), pixelArt_ ? SDL_SCALEMODE_PIXELART : SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND); // (a JPEG, a PNG without alpha: they fade and show through too)
            break;
        }
        // There, but it can't be used (a broken file, or too big for the graphics card).
        if (looks == 0) logWarn("Can't load 'images/{}{}': {}, so a stand-in is drawn.", name, ext, SDL_GetError());
        found = true;
        break;
    }
    if (!texture && !found && looks == 0) logWarn("There's no image called '{}', so a stand-in is drawn.", name);
    Image entry{.texture = std::move(texture), .lastFrame = frame_};
    if (entry.texture) {
        float w = 0, h = 0;
        SDL_GetTextureSize(entry.texture.get(), &w, &h);
        entry.bytes = static_cast<std::size_t>(w * h * 4.0f);
        used_ += entry.bytes;
    }
    SDL_Texture* out = entry.texture.get();
    images_.emplace(key, std::move(entry));
    trim();
    return out;
}

// Over the memory limit: lets go of the pictures not drawn for the longest (never those of the
// last two frames). They load again when they're needed.
void Assets::trim() {
    while (limit_ > 0 && used_ > limit_) {
        auto oldest = images_.end();
        for (auto it = images_.begin(); it != images_.end(); ++it)
            if (it->second.texture && it->second.lastFrame + 2 <= frame_ && (oldest == images_.end() || it->second.lastFrame < oldest->second.lastFrame))
                oldest = it;
        if (oldest == images_.end()) return;
        used_ -= oldest->second.bytes;
        images_.erase(oldest);
    }
}

bool Assets::preload(std::string_view name, unsigned looks) {
    if (const auto sub = subPictures_.find(lower(name)); sub != subPictures_.end()) name = sub->second.image;
    const auto atlas = frames_.find(lower(name));
    std::vector<std::string> files{atlas == frames_.end() ? std::string(name) : atlases_[atlas->second.first].image};
    if (atlas != frames_.end()) {
        const Atlas& a = atlases_[atlas->second.first];
        if (a.eyesPart) files.push_back(a.eyesPart->image);
        if (a.mouthPart) files.push_back(a.mouthPart->image);
    }
    bool loaded = false;
    for (const auto& file : files)
        if (!images_.contains(cacheKey(file, looks))) {
            image(file, looks);
            loaded = true;
        }
    return loaded;
}

void Assets::setPixelArt(bool pixelArt) {
    if (pixelArt == pixelArt_) return;
    pixelArt_ = pixelArt;
    clear();
}

void Assets::setAtlases(std::vector<Atlas> atlases) {
    atlases_ = std::move(atlases);
    frames_.clear();
    for (std::size_t a = 0; a < atlases_.size(); ++a) {
        const Atlas& atlas = atlases_[a];
        frames_.insert_or_assign(lower(atlas.name), std::pair{a, std::size_t{0}});
        for (std::size_t e = 0; e < atlas.expressions.size(); ++e)
            frames_.insert_or_assign(lower(std::format("{}_{}", atlas.name, atlas.expressions[e])), std::pair{a, e});
    }
}

void Assets::setSubPictures(const std::vector<SubPicture>& parts) {
    subPictures_.clear();
    for (const SubPicture& p : parts) subPictures_.insert_or_assign(p.name, p);
}

Picture Assets::picture(std::string_view name, Frame frame, unsigned looks) {
    const auto sub = subPictures_.find(lower(name));
    if (sub == subPictures_.end()) return whole(name, frame, looks);
    const Picture all = whole(sub->second.image, frame, looks);
    if (!all) return {};
    SDL_FRect part{};
    const SDL_FRect bounds{0.0f, 0.0f, all.w(), all.h()};
    if (!SDL_GetRectIntersectionFloat(&sub->second.rect, &bounds, &part)) return {};
    Picture out = all.crop(part);
    out.frame = true; // (its neighbours on the sheet don't bleed in)
    return out;
}

Picture Assets::whole(std::string_view name, Frame frame, unsigned looks) {
    const auto it = frames_.find(lower(name));
    if (it == frames_.end()) {
        SDL_Texture* tex = image(name, looks);
        if (!tex) return {};
        float w = 0, h = 0;
        SDL_GetTextureSize(tex, &w, &h);
        return {.texture = tex, .src = {0, 0, w, h}};
    }
    const Atlas& a = atlases_[it->second.first];
    SDL_Texture* tex = image(a.image, looks);
    if (!tex) return {};
    // The nearest frame: 0 is the first eyes / mouth, 1 the last.
    auto pick = [](float amount, std::size_t count) {
        return count < 2 ? 0 : static_cast<int>(std::lround(std::clamp(amount, 0.0f, 1.0f) * static_cast<float>(count - 1)));
    };
    const int expression = static_cast<int>(it->second.second);
    const int expressions = std::max<int>(1, static_cast<int>(a.expressions.size()));
    const int eyes = a.eyesPart ? 1 : std::max<int>(1, static_cast<int>(a.eyes.size())); // frames in the grid for them
    const int mouths = a.mouthPart ? 1 : std::max<int>(1, static_cast<int>(a.mouth.size()));
    const int eye = pick(frame.eyes, a.eyes.size()), mouth = pick(frame.mouth, a.mouth.size());
    const int perExpression = eyes * mouths;
    const int index = (expression * eyes + (a.eyesPart ? 0 : eye)) * mouths + (a.mouthPart ? 0 : mouth);
    const int columns = a.columns > 0 ? a.columns : perExpression > 1 ? perExpression : expressions;
    const int rows = (expressions * perExpression + columns - 1) / columns;
    float w = 0, h = 0;
    SDL_GetTextureSize(tex, &w, &h);
    const float fw = w / static_cast<float>(columns), fh = h / static_cast<float>(rows);
    Picture pic{.texture = tex, .src = {fw * static_cast<float>(index % columns), fh * static_cast<float>(index / columns), fw, fh},
                .frame = true, .assets = this};
    // Parts: a row per expression, a column per state.
    auto part = [&](const std::optional<Atlas::Part>& p, int state, std::size_t states) {
        if (!p || states == 0) return;
        SDL_Texture* t = image(p->image, looks);
        if (!t) return;
        float pw = 0, ph = 0;
        SDL_GetTextureSize(t, &pw, &ph);
        const float cw = pw / static_cast<float>(states), ch = ph / static_cast<float>(expressions);
        pic.overlays[pic.overlayCount++] = {t, {cw * static_cast<float>(state), ch * static_cast<float>(expression), cw, ch}, {p->x, p->y}};
    };
    part(a.eyesPart, eye, a.eyes.size());
    part(a.mouthPart, mouth, a.mouth.size());
    return pic;
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

namespace {

constexpr int kPlaceholderRate = 22050;
constexpr float kTau = 6.2831853f;

float noteHz(int semitonesFromA4) {
    return 440.0f * std::exp2(static_cast<float>(semitonesFromA4) / 12.0f);
}

// A soft 4-bar loop (pad chords, an arpeggio and a bass line). Key, mode and tempo are derived
// from the file name, so different missing tracks are easy to tell apart.
std::vector<float> placeholderMusic(std::uint64_t seed) {
    const int key = static_cast<int>(seed % 12) - 9; // C4..B4 relative to A4
    const bool minor = ((seed >> 8) & 1) != 0;
    const float beat = 60.0f / (84.0f + static_cast<float>((seed >> 16) % 5) * 8.0f); // 84-116 bpm
    const float bar = beat * 4.0f;
    const auto total = static_cast<std::size_t>(bar * 4.0f * kPlaceholderRate);
    std::vector<float> out(total, 0.0f);

    // Writes wrap around the end of the buffer so tails continue seamlessly into the next loop.
    auto voice = [&](float start, float length, float hz, float gain, float attack, float decay, float harmonic) {
        const auto first = static_cast<std::size_t>(start * kPlaceholderRate);
        const auto count = static_cast<std::size_t>(length * kPlaceholderRate);
        for (std::size_t i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / kPlaceholderRate;
            const float env = std::min(1.0f, t / attack) *
                              (decay > 0.0f ? std::exp(-t * decay) : std::min(1.0f, (length - t) / attack));
            const float phase = kTau * hz * t;
            out[(first + i) % total] += (std::sin(phase) + harmonic * std::sin(2.0f * phase)) * env * gain;
        }
    };

    struct Chord { int root; bool minor; };
    static constexpr std::array<Chord, 4> majorProgression{{{0, false}, {7, false}, {9, true}, {5, false}}};  // I V vi IV
    static constexpr std::array<Chord, 4> minorProgression{{{0, true}, {8, false}, {3, false}, {10, false}}}; // i VI III VII
    static constexpr std::array<std::size_t, 8> arpeggio{0, 1, 2, 3, 2, 1, 0, 2};

    for (std::size_t b = 0; b < 4; ++b) {
        const Chord c = (minor ? minorProgression : majorProgression)[b];
        const int root = key + c.root;
        const std::array<int, 4> tones{root, root + (c.minor ? 3 : 4), root + 7, root + 12};
        const float t0 = static_cast<float>(b) * bar;
        for (std::size_t i = 0; i < 3; ++i) { // pad, slightly detuned for width
            voice(t0, bar + 0.4f, noteHz(tones[i] - 12), 0.05f, 0.4f, 0.0f, 0.0f);
            voice(t0, bar + 0.4f, noteHz(tones[i] - 12) * 1.004f, 0.03f, 0.4f, 0.0f, 0.0f);
        }
        for (std::size_t n = 0; n < arpeggio.size(); ++n) // eighth-note plucks
            voice(t0 + static_cast<float>(n) * beat * 0.5f, 1.2f, noteHz(tones[arpeggio[n]]), 0.10f, 0.004f, 5.0f, 0.25f);
        for (int n = 0; n < 2; ++n) // bass on beats 1 and 3
            voice(t0 + static_cast<float>(n) * beat * 2.0f, beat * 1.5f, noteHz(root - 24), 0.22f, 0.01f, 2.5f, 0.4f);
    }

    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::abs(v));
    if (peak > 0.0f)
        for (float& v : out) v *= 0.4f / peak;
    return out;
}

// A short "blip" whose pitch and direction come from the file name.
std::vector<float> placeholderSound(std::uint64_t seed) {
    const float base = 440.0f + static_cast<float>(seed % 600);
    const bool rising = ((seed >> 12) & 1) != 0;
    constexpr float length = 0.16f;
    std::vector<float> out(static_cast<std::size_t>(length * kPlaceholderRate));
    float phase = 0.0f;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kPlaceholderRate;
        const float sweep = rising ? 1.0f + 0.5f * t / length : 1.5f - 0.5f * t / length;
        phase += kTau * base * sweep / kPlaceholderRate;
        const float env = std::min(1.0f, t / 0.004f) * std::exp(-t * 18.0f);
        out[i] = (std::sin(phase) + 0.3f * std::sin(2.0f * phase)) * env * 0.35f;
    }
    return out;
}

} // namespace

Audio::~Audio() {
    while (!streams_.empty()) closeStream(streams_.back().stream.get());
    cache_.clear();  // MIX_Audio is ref-counted, so tracks still using it stay valid
    mixer_.reset();  // also destroys every track created on it
    if (ready_) MIX_Quit();
}

bool Audio::init() {
    if (!MIX_Init()) {
        logWarn("Can't start SDL_mixer (it plays the audio): {}.", SDL_GetError());
        return false;
    }
    ready_ = true;
    mixer_.reset(MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr));
    if (!mixer_) {
        logWarn("Can't open the audio device: {}, so the game plays without sound.", SDL_GetError());
        return false;
    }
    for (auto& t : music_) t = MIX_CreateTrack(mixer_.get());
    for (auto& t : sounds_) t = MIX_CreateTrack(mixer_.get());
    voice_.track = MIX_CreateTrack(mixer_.get());
    replay_.track = MIX_CreateTrack(mixer_.get());
    listen_ = MIX_CreateTrack(mixer_.get());
    setVolume(masterVolume_, musicVolume_, soundVolume_, voiceVolume_);
    return true;
}

// Track gains combine the player's volume setting with each sound's own volume.
void Audio::setVolume(float master, float music, float sound, float voice) {
    masterVolume_ = master;
    musicVolume_ = music;
    soundVolume_ = sound;
    voiceVolume_ = voice;
    if (!mixer_) return;
    MIX_SetMixerGain(mixer_.get(), masterVolume_);
    applyMusicGain();
    for (std::size_t i = 0; i < sounds_.size(); ++i) MIX_SetTrackGain(sounds_[i], soundVolume_ * soundGain_[i]);
    for (auto& entry : channels_) MIX_SetTrackGain(entry.second.track, soundVolume_ * entry.second.volume);
    for (const Voice* v : {&voice_, &replay_}) MIX_SetTrackGain(v->track, voiceVolume_ * v->gain);
    for (const Stream& s : streams_) MIX_SetTrackGain(s.track.get(), musicVolume_ * s.gain);
}

MIX_Audio* Audio::load(const std::string& path, Kind kind) {
    if (auto it = cache_.find(path); it != cache_.end()) return it->second.get();

    // Music is decoded while it plays (only the compressed file stays in memory);
    // sound effects are short, so they are decoded once up front.
    IOStreamPtr file = files_.open(path);
    MixAudioPtr audio{file ? MIX_LoadAudio_IO(mixer_.get(), file.release(), kind == Kind::Sound, true) : nullptr};
    if (!audio) {
        logWarn("Can't load '{}': {}, so a stand-in plays.", path, SDL_GetError());
        const auto slash = path.find_last_of("/\\");
        return placeholder(path, kind, fnv1a(slash == std::string::npos ? path : path.substr(slash + 1)));
    }
    return cache_.emplace(path, std::move(audio)).first->second.get();
}

MIX_Audio* Audio::placeholder(const std::string& key, Kind kind, std::uint64_t seed) {
    const auto pcm = kind == Kind::Music ? placeholderMusic(seed) : placeholderSound(seed);
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kPlaceholderRate};
    MixAudioPtr audio{MIX_LoadRawAudio(mixer_.get(), pcm.data(), pcm.size() * sizeof(float), &spec)};
    return cache_.insert_or_assign(key, std::move(audio)).first->second.get();
}

Sint64 Audio::frames(MIX_Track* track, float seconds) const {
    return seconds > 0.0f ? MIX_TrackMSToFrames(track, static_cast<Sint64>(seconds * 1000.0f)) : 0;
}

bool Audio::start(MIX_Track* track, MIX_Audio* audio, bool loop, float fadeIn) {
    MIX_SetTrackAudio(track, audio);
    const SdlProperties options;
    SDL_SetNumberProperty(options.id(), MIX_PROP_PLAY_LOOPS_NUMBER, loop ? -1 : 0);
    SDL_SetNumberProperty(options.id(), MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, static_cast<Sint64>(fadeIn * 1000.0f));
    const bool ok = MIX_PlayTrack(track, options.id());
    if (!ok) logWarn("Can't play audio: {}.", SDL_GetError());
    return ok;
}

void Audio::playMusic(const std::string& path, bool loop, float fadeIn, float volume) {
    if (!mixer_) {
        musicPath_ = path;
        return;
    }
    if (path == musicPath_ && MIX_TrackPlaying(music_[current_])) return;
    stopMusic(crossfade_);
    musicPath_ = path;
    musicLoops_ = loop;

    MIX_Audio* audio = load(path, Kind::Music);
    if (!audio) return;
    current_ = 1 - current_;
    musicGain_[current_] = volume;
    applyMusicGain();
    start(music_[current_], audio, loop, fadeIn);
}

void Audio::stopMusic(float fadeOut) {
    musicPath_.clear();
    if (!mixer_) return;
    MIX_Track* track = music_[current_];
    MIX_StopTrack(track, frames(track, fadeOut));
}

void Audio::playSound(const std::string& path, float volume) {
    if (mixer_) play(load(path, Kind::Sound), volume);
}

void Audio::playChannel(const std::string& channel, const std::string& path, bool loop, float volume, float fadeIn) {
    if (!mixer_) return;
    MIX_Audio* audio = load(path, loop ? Kind::Music : Kind::Sound);
    if (!audio) return;
    Channel& c = channels_[channel];
    if (!c.track) c.track = MIX_CreateTrack(mixer_.get());
    c.path = path;
    c.volume = volume;
    MIX_SetTrackGain(c.track, soundVolume_ * volume);
    start(c.track, audio, loop, fadeIn);
}

void Audio::stopChannel(const std::string& channel, float fadeOut) {
    if (!mixer_) return;
    if (channel.empty()) {
        for (auto& entry : channels_) MIX_StopTrack(entry.second.track, frames(entry.second.track, fadeOut));
        return;
    }
    if (auto it = channels_.find(channel); it != channels_.end())
        MIX_StopTrack(it->second.track, frames(it->second.track, fadeOut));
}

void Audio::say(Voice& v, MIX_Audio* audio, float volume, const Envelope* envelope) {
    MIX_StopTrack(v.track, 0);
    v.gain = volume;
    v.envelope = envelope;
    MIX_SetTrackGain(v.track, voiceVolume_ * volume);
    if (audio) start(v.track, audio, false, 0.0f);
}

bool Audio::sayBabble(Voice& v, std::string_view text, float pitch, float speed) {
    const std::vector<float> pcm = babble(text, pitch, kPlaceholderRate, speed);
    if (pcm.empty()) return false;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kPlaceholderRate};
    MixAudioPtr audio{MIX_LoadRawAudio(mixer_.get(), pcm.data(), pcm.size() * sizeof(float), &spec)};
    if (!audio) return false;
    v.babble = std::move(audio); // (the track keeps the one it was playing while it needs it)
    v.babbleEnvelope = envelope(pcm, kPlaceholderRate);
    say(v, v.babble.get(), 1.0f, &v.babbleEnvelope);
    return true;
}

void Audio::playVoice(const std::string& path, float volume) {
    if (!mixer_) return;
    voicePaused_ = voiceHeld_ = false;
    MIX_Audio* audio = load(path, Kind::Sound);
    say(voice_, audio, volume, audio ? &measure(path) : nullptr);
}

bool Audio::playBabble(std::string_view text, float pitch, float speed) {
    if (!mixer_) return false;
    voicePaused_ = voiceHeld_ = false;
    return sayBabble(voice_, text, pitch, speed);
}

void Audio::stopVoice() {
    if (mixer_) MIX_StopTrack(voice_.track, frames(voice_.track, 0.1f));
    voicePaused_ = voiceHeld_ = false;
}

void Audio::replayVoice(const std::string& path, float volume) {
    if (mixer_) say(replay_, load(path, Kind::Sound), volume, nullptr);
}

bool Audio::replayBabble(std::string_view text, float pitch, float speed) {
    return mixer_ && sayBabble(replay_, text, pitch, speed);
}

void Audio::stopReplay() {
    if (mixer_) MIX_StopTrack(replay_.track, frames(replay_.track, 0.1f));
}

bool Audio::replaying() const {
    return mixer_ && MIX_TrackPlaying(replay_.track);
}

void Audio::pauseVoice(bool paused) { waitVoice(voicePaused_, paused); }
void Audio::holdVoice(bool held) { waitVoice(voiceHeld_, held); }

void Audio::waitVoice(bool& reason, bool wait) {
    if (!mixer_ || reason == wait) return;
    if (wait && !MIX_TrackPlaying(voice_.track) && !MIX_TrackPaused(voice_.track)) return; // nothing to wait
    reason = wait;
    const bool waiting = voicePaused_ || voiceHeld_;
    if (waiting && !MIX_TrackPaused(voice_.track)) MIX_PauseTrack(voice_.track);
    else if (!waiting && MIX_TrackPaused(voice_.track)) resume(voice_.track);
}

SDL_AudioStream* Audio::openStream(int rate, float volume) {
    if (!mixer_) return nullptr;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate};
    Stream s{.stream = AudioStreamPtr(SDL_CreateAudioStream(&spec, &spec)), .track = nullptr, .gain = volume};
    if (s.stream) s.track.reset(MIX_CreateTrack(mixer_.get()));
    if (!s.track || !MIX_SetTrackAudioStream(s.track.get(), s.stream.get())) { // (it plays once there's sound in it: see update())
        logWarn("Can't play the video's sound: {}.", SDL_GetError());
        return nullptr;
    }
    MIX_SetTrackGain(s.track.get(), musicVolume_ * volume);
    return streams_.emplace_back(std::move(s)).stream.get();
}

void Audio::pauseStream(SDL_AudioStream* stream, bool paused) {
    for (Stream& s : streams_)
        if (s.stream.get() == stream) {
            s.paused = paused;
            if (paused) MIX_PauseTrack(s.track.get());
            else resume(s.track.get());
        }
}

void Audio::closeStream(SDL_AudioStream* stream) {
    const auto it = std::ranges::find(streams_, stream, [](const Stream& s) { return s.stream.get(); });
    if (it == streams_.end()) return;
    MIX_Track* track = it->track.get();
    std::erase(held_, track);
    if (track == videoTrack_) videoTrack_ = nullptr;
    MIX_StopTrack(track, 0);
    streams_.erase(it); // (its track, then its stream)
}

Audio::Envelope Audio::envelope(const std::vector<float>& mono, int rate) {
    Envelope e;
    const auto window = std::max<std::size_t>(1, static_cast<std::size_t>(kWindow * static_cast<float>(rate)));
    for (std::size_t i = 0; i < mono.size(); i += window) {
        const std::size_t end = std::min(mono.size(), i + window);
        double sum = 0.0;
        for (std::size_t k = i; k < end; ++k) sum += static_cast<double>(mono[k]) * mono[k];
        e.level.push_back(static_cast<float>(std::sqrt(sum / static_cast<double>(end - i))));
    }
    if (e.level.empty()) return e;

    // 1 is (nearly) the loudest moment; quiet hiss counts as silence.
    std::vector<float> sorted = e.level;
    std::ranges::sort(sorted);
    const float top = sorted[sorted.size() - 1 - sorted.size() / 50];
    for (float& v : e.level) v = top > 0.0f ? std::clamp((v / top - 0.08f) / 0.92f, 0.0f, 1.0f) : 0.0f;

    // Speech is where there's sound, bridging the short gaps between syllables (60 ms either side).
    constexpr float kSilence = 0.12f;
    constexpr std::size_t kBridge = 3;
    std::vector<bool> speaking(e.level.size());
    for (std::size_t i = 0; i < e.level.size(); ++i)
        for (std::size_t k = i > kBridge ? i - kBridge : 0; k <= std::min(e.level.size() - 1, i + kBridge); ++k)
            if (e.level[k] > kSilence) speaking[i] = true;
    const auto total = static_cast<float>(std::count(speaking.begin(), speaking.end(), true));
    float done = 0.0f;
    for (std::size_t i = 0; i < e.level.size(); ++i) {
        done += speaking[i] ? 1.0f : 0.0f;
        e.spoken.push_back(total > 0.0f ? done / total : static_cast<float>(i + 1) / static_cast<float>(e.level.size()));
    }
    return e;
}

const Audio::Envelope& Audio::measure(const std::string& path) {
    if (auto it = envelopes_.find(path); it != envelopes_.end()) return it->second;
    constexpr int kRate = 16000;
    std::vector<float> mono;
    if (const Owned<MIX_AudioDecoder*, MIX_DestroyAudioDecoder> decoder{MIX_CreateAudioDecoder_IO(files_.open(path).release(), true, 0)}) { // (in a pack too)
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kRate};
        std::vector<float> chunk(4096);
        for (int bytes; (bytes = MIX_DecodeAudio(decoder.get(), chunk.data(), static_cast<int>(chunk.size() * sizeof(float)), &spec)) > 0;)
            mono.insert(mono.end(), chunk.begin(), chunk.begin() + bytes / static_cast<int>(sizeof(float)));
    }
    return envelopes_.emplace(path, envelope(mono, kRate)).first->second;
}

bool Audio::voicePlaying() const {
    return mixer_ && MIX_TrackPlaying(voice_.track);
}

std::optional<std::size_t> Audio::voiceWindow() const {
    if (!mixer_ || (!MIX_TrackPlaying(voice_.track) && !MIX_TrackPaused(voice_.track))) return std::nullopt; // (a waiting one counts)
    const Sint64 position = MIX_GetTrackPlaybackPosition(voice_.track);
    if (position < 0) return std::nullopt;
    return static_cast<std::size_t>(static_cast<float>(MIX_TrackFramesToMS(voice_.track, position)) / (kWindow * 1000.0f));
}

float Audio::voiceLevel() const {
    const auto window = voicePlaying() ? voiceWindow() : std::nullopt;
    if (!window || !voice_.envelope || voice_.envelope->level.empty()) return 0.0f;
    return voice_.envelope->level[std::min(*window, voice_.envelope->level.size() - 1)];
}

std::optional<float> Audio::voiceProgress() const {
    const auto window = voiceWindow();
    if (!window) return std::nullopt;
    if (voice_.envelope && !voice_.envelope->spoken.empty())
        return voice_.envelope->spoken[std::min(*window, voice_.envelope->spoken.size() - 1)];
    MIX_Audio* audio = MIX_GetTrackAudio(voice_.track); // loudness unknown: by time
    const Sint64 length = audio ? MIX_GetAudioDuration(audio) : -1;
    if (length <= 0) return std::nullopt;
    const auto ms = static_cast<float>(MIX_AudioFramesToMS(audio, length));
    return std::clamp(static_cast<float>(*window) * kWindow * 1000.0f / ms, 0.0f, 1.0f);
}

std::optional<float> Audio::voiceSilenceNear(float share) const {
    if (!voice_.envelope) return std::nullopt;
    const std::vector<float>& spoken = voice_.envelope->spoken;
    std::optional<float> best;
    for (std::size_t i = 1; i < spoken.size(); ++i) // silences: windows where no more speech is done
        if (spoken[i] == spoken[i - 1] && (i < 2 || spoken[i - 1] != spoken[i - 2]) && spoken[i] > 0.0f) {
            constexpr float kNear = 0.15f;
            if (std::abs(spoken[i] - share) <= kNear && (!best || std::abs(spoken[i] - share) < std::abs(*best - share))) best = spoken[i];
        }
    return best;
}

bool Audio::voiceSilent() const {
    const auto window = voiceWindow();
    if (!window || !voice_.envelope || voice_.envelope->spoken.size() < 2) return true;
    const std::vector<float>& spoken = voice_.envelope->spoken;
    const std::size_t i = std::clamp<std::size_t>(*window, 1, spoken.size() - 1);
    return spoken[i] == spoken[i - 1];
}

void Audio::playTestSound() {
    if (!mixer_) return;
    static const std::string key = "<volume test>";
    auto it = cache_.find(key);
    play(it != cache_.end() ? it->second.get() : placeholder(key, Kind::Sound, 7), 1.0f);
}

void Audio::play(MIX_Audio* audio, float volume) {
    if (!audio) return;
    // Prefer an idle track; if all are busy, reuse them round-robin (the oldest sound is cut).
    std::size_t pick = nextSound_;
    for (std::size_t i = 0; i < kSoundTracks; ++i) {
        const std::size_t candidate = (nextSound_ + i) % kSoundTracks;
        if (!MIX_TrackPlaying(sounds_[candidate])) {
            pick = candidate;
            break;
        }
    }
    nextSound_ = (pick + 1) % kSoundTracks;
    soundGain_[pick] = volume;
    MIX_SetTrackGain(sounds_[pick], soundVolume_ * volume);
    MIX_SetTrackAudio(sounds_[pick], audio);
    MIX_PlayTrack(sounds_[pick], 0);
}

void Audio::listen(const std::string& path) {
    if (!mixer_) return;
    listenedVideo_ = false;
    videoTrack_ = nullptr;
    holdOthers();
    listened_ = path;
    MIX_SetTrackGain(listen_, 1.0f);
    start(listen_, load(path, Kind::Music), false, 0.0f);
}

void Audio::listen(const std::string& name, SDL_AudioStream* video) {
    listened_ = name;
    listenedVideo_ = true;
    if (!mixer_) return;
    MIX_StopTrack(listen_, 0);
    const auto it = std::ranges::find(streams_, video, [](const Stream& s) { return s.stream.get(); });
    videoTrack_ = it != streams_.end() ? it->track.get() : nullptr;
    if (videoTrack_ && std::erase(held_, videoTrack_) > 0) MIX_ResumeTrack(videoTrack_);
    holdOthers();
}

void Audio::stopListening() {
    if (listened_.empty()) return;
    listened_.clear();
    listenedVideo_ = false;
    videoTrack_ = nullptr;
    if (!mixer_) return;
    MIX_StopTrack(listen_, 0);
    if (!gameHeld_) releaseHeld();
}

void Audio::holdGame(bool held) {
    gameHeld_ = held;
    if (!mixer_) return;
    if (held) holdOthers();
    else if (listened_.empty()) releaseHeld();
}

void Audio::releaseHeld() {
    for (MIX_Track* track : std::exchange(held_, {})) {
        const bool waits = (track == voice_.track && (voicePaused_ || voiceHeld_)) ||
                           std::ranges::any_of(streams_, [&](const Stream& s) { return s.track.get() == track && s.paused; });
        if (!waits) MIX_ResumeTrack(track);
    }
}

void Audio::holdOthers() {
    auto hold = [&](MIX_Track* track) {
        if (track == videoTrack_ || !MIX_TrackPlaying(track)) return;
        MIX_PauseTrack(track);
        if (std::ranges::find(held_, track) == held_.end()) held_.push_back(track);
    };
    for (MIX_Track* track : music_) hold(track);
    for (MIX_Track* track : sounds_) hold(track);
    for (const auto& entry : channels_) hold(entry.second.track);
    for (const Voice* v : {&voice_, &replay_}) hold(v->track);
    for (const Stream& s : streams_) hold(s.track.get());
}

// A track that waited for something goes on, or (while the developer tools play a file) waits for that.
void Audio::resume(MIX_Track* track) {
    if (listened_.empty() && !gameHeld_) MIX_ResumeTrack(track);
    else if (std::ranges::find(held_, track) == held_.end()) held_.push_back(track);
}

void Audio::stopAll(float fadeOut) {
    stopListening();
    stopMusic(fadeOut);
    stopChannel("", fadeOut);
    stopVoice();
    stopReplay();
}

void Audio::applyMusicGain() {
    if (!mixer_) return;
    for (std::size_t i = 0; i < music_.size(); ++i) MIX_SetTrackGain(music_[i], musicVolume_ * musicGain_[i] * duck_);
}

void Audio::update(float dt) {
    if (!mixer_) return;
    // A video's track stops when it finds its stream empty (before the first sound, or if it runs
    // dry): it plays again when there's sound in it.
    for (const Stream& s : streams_)
        if (!s.paused && !MIX_TrackPlaying(s.track.get()) && !MIX_TrackPaused(s.track.get()) && SDL_GetAudioStreamAvailable(s.stream.get()) > 0)
            MIX_PlayTrack(s.track.get(), 0);
    if (!listened_.empty() && !listenedVideo_ && !MIX_TrackPlaying(listen_)) stopListening();
    if (!listened_.empty() || gameHeld_) holdOthers(); // (what started meanwhile waits too)
    // A non-looping song that finished is no longer "the current music" (matters for saves).
    if (!musicLoops_ && !musicPath_.empty() && !MIX_TrackPlaying(music_[current_])) musicPath_.clear();
    // Music gets quieter while someone speaks, and comes back after (over about a third of a second).
    const float target = voicePlaying() || replaying() ? duckLevel_ : 1.0f;
    if (duck_ != target) {
        const float step = dt / 0.3f;
        duck_ = duck_ < target ? std::min(target, duck_ + step) : std::max(target, duck_ - step);
        applyMusicGain();
    }
}

void Audio::clearCache() {
    cache_.clear();
    voice_.envelope = replay_.envelope = nullptr;
    envelopes_.clear();
    musicPath_.clear();
}

// ---------------------------------------------------------------------------
// Made-up voices
// ---------------------------------------------------------------------------

namespace {

struct Vowel {
    float f1, f2; // formants (Hz): what makes an "a" sound like an "a"
};
constexpr std::array<Vowel, 5> kVowels{{{850, 1220}, {600, 2300}, {350, 2700}, {500, 900}, {380, 950}}}; // a e i o u
constexpr Vowel kHum{280, 1600}; // ん, through the nose

const Vowel* vowel(char c) {
    switch (c) {
    case 'a': return &kVowels[0];
    case 'e': return &kVowels[1];
    case 'i':
    case 'y': return &kVowels[2];
    case 'o': return &kVowels[3];
    case 'u': return &kVowels[4];
    default: return nullptr;
    }
}

// Hiragana from ぁ (U+3041) to ゖ (U+3096); katakana are the same 0x60 further on. A vowel letter
// is a syllable with that vowel (with a consonant after the first ten); a capital is a small kana,
// which changes the vowel of the one before (きゃ, ファ); Q is っ (a short stop), N is ん (a hum).
constexpr std::string_view kKana = "AaIiUuEeOo" "aaiiuueeoo" "aaiiuueeoo" "aaiiQuueeoo" "aiueo" "aaaiiiuuueeeooo" "aiueo"
                                   "AaUuOo" "aiueo" "AaieoNuAE";
// ...and how each starts: k a stop (k t p), g a voiced one (g d b), s a hiss (s sh ch ts h f), z a
// voiced hiss (z j), n a soft one (n m r y w), - just the vowel.
constexpr std::string_view kKanaStarts = "----------" "kgkgkgkgkg" "szszszszsz" "kgsz-szkgkg" "nnnnn" "sgksgksgksgksgk" "nnnnn"
                                         "-n-n-n" "nnnnn" "-nnn--g--";
// Korean: the vowel of each syllable block, by its middle letter (ㅏ ㅐ ㅑ ... ㅣ).
constexpr std::string_view kHangulVowels = "aeaeoeoeoaeeouoeiuuii";

struct Syllable {
    float start, length;
    Vowel sound;
    bool consonant;         // starts with a puff of noise (a mora: with its kind of consonant, see kKanaStarts)
    char starts = '-';      // a mora's consonant
    bool mora = false;      // Japanese: morae flow into each other without a break
    float level = 1.0f;
    float from = 0, to = 0; // pitch glide
};

} // namespace

std::vector<float> babble(std::string_view text, float pitch, int rate, float speed) {
    const float slow = 1.0f / std::clamp(speed, 0.25f, 4.0f); // stretches every syllable and pause
    std::mt19937 rng(static_cast<std::uint32_t>(fnv1a(text)));
    auto jitter = [&](int range) { return static_cast<float>(static_cast<int>(rng() % static_cast<unsigned>(range))); };
    std::vector<Syllable> syllables;
    std::size_t sentence = 0; // first syllable of the sentence being read
    float t = 0.05f;
    std::string word;

    auto add = [&](Vowel sound, bool consonant, bool last) {
        const float length = (0.085f + jitter(40) / 1000.0f + (last ? 0.04f : 0.0f)) * slow;
        syllables.push_back({t, length, sound, consonant});
        t += length + 0.012f * slow;
    };
    auto addMora = [&](Vowel sound, char starts, float level = 1.0f) { // Japanese: even beats, one running into the next
        const float length = (0.1f + jitter(10) / 1000.0f) * slow;
        syllables.push_back({.start = t, .length = length, .sound = sound, .consonant = starts != '-', .starts = starts, .mora = true, .level = level});
        t += length;
    };
    auto vowelOf = [](char v) { return *vowel(static_cast<char>(std::tolower(static_cast<unsigned char>(v)))); };
    // Japanese: kana say their own vowels, a mora each, and kanji are read as one or two morae. A
    // line without kana is Chinese: a syllable a character.
    bool japanese = false;
    const char* p = text.data();
    for (std::size_t n = text.size(); n > 0 && !japanese;) {
        const Uint32 c = SDL_StepUTF8(&p, &n);
        japanese = c >= 0x3041 && c <= 0x30FA;
    }
    auto endWord = [&] { // a syllable for each group of vowels
        std::vector<std::pair<const Vowel*, bool>> groups;
        bool consonant = false;
        for (std::size_t i = 0; i < word.size(); ++i) {
            const Vowel* v = vowel(word[i]);
            if (!v) {
                consonant = true;
                continue;
            }
            if (i == 0 || !vowel(word[i - 1])) groups.emplace_back(v, consonant);
            consonant = false;
        }
        if (groups.empty() && !word.empty()) groups.emplace_back(&kVowels[4], true); // "hm", "shh"
        for (std::size_t k = 0; k < groups.size(); ++k) add(*groups[k].first, groups[k].second, k + 1 == groups.size());
        if (!groups.empty()) t += 0.03f * slow;
        word.clear();
    };
    auto endSentence = [&](char end) { // the voice falls through a sentence; a question rises at the end
        const std::size_t n = syllables.size() - sentence;
        for (std::size_t k = 0; k < n; ++k) {
            Syllable& s = syllables[sentence + k];
            const float pos = n > 1 ? static_cast<float>(k) / static_cast<float>(n - 1) : 0.0f;
            const bool last = k + 1 == n;
            if (s.mora) { // a smooth line: up after the first mora, then slowly down, each glide starting where the last one ended
                float tone = pitch * (k == 0 ? 0.95f : 1.08f - 0.16f * pos) * (end == '!' ? 1.06f : 1.0f);
                if (end == '?' && last) tone = pitch * 1.25f;
                const Syllable* before = k > 0 ? &syllables[sentence + k - 1] : nullptr;
                s.from = before && before->mora ? before->to : tone;
                s.to = last && end != '?' ? tone * 0.9f : tone;
                continue;
            }
            float tone = pitch * (1.1f - 0.2f * pos) * (1.0f + (jitter(60) - 30.0f) / 1000.0f);
            if (end == '!') tone *= 1.08f;
            if (end == '?' && last) tone = pitch * 1.3f;
            s.from = tone;
            s.to = tone * (last ? (end == '?' ? 1.12f : 0.88f) : 0.98f);
        }
        sentence = syllables.size();
    };

    const char* at = text.data();
    std::size_t left = text.size();
    while (left > 0) {
        const Uint32 c = SDL_StepUTF8(&at, &left);
        if (c < 0x80) {
            const auto ch = static_cast<char>(std::tolower(static_cast<int>(c)));
            if ((ch >= 'a' && ch <= 'z') || ch == '\'') {
                word += ch;
            } else if (ch >= '0' && ch <= '9') { // a syllable a digit
                endWord();
                add(kVowels[static_cast<std::size_t>(ch - '0') % kVowels.size()], true, true);
            } else if (ch == ',' || ch == ';' || ch == ':') {
                endWord();
                t += 0.16f * slow;
            } else if (ch == '.' && left >= 2 && at[0] == '.' && at[1] == '.') { // ...
                endWord();
                t += 0.45f * slow;
                at += 2;
                left -= 2;
            } else if (ch == '.' || ch == '!' || ch == '?') {
                endWord();
                endSentence(ch);
                t += 0.32f * slow;
            } else {
                endWord();
            }
        } else if (c == 0x3002 || c == 0xFF01 || c == 0xFF1F || c == 0xFF0E) { // 。！？．
            endWord();
            endSentence(c == 0xFF1F ? '?' : c == 0xFF01 ? '!' : '.');
            t += 0.32f * slow;
        } else if (c == 0x3001 || c == 0xFF0C) { // 、，
            endWord();
            t += 0.16f * slow;
        } else if (c == 0x2026) { // …
            endWord();
            t += 0.45f * slow;
        } else if ((c >= 0x2000 && c < 0x2070) || (c >= 0x3000 && c < 0x3040) || (c >= 0xFF00 && c < 0xFF10)) {
            endWord(); // other punctuation and spaces
        } else if (const Uint32 k = c >= 0x30A1 && c <= 0x30F6 ? c - 0x60 : c; k >= 0x3041 && k <= 0x3096) { // kana
            endWord();
            const char v = kKana[k - 0x3041];
            if (v == 'Q') t += 0.07f * slow; // っ: a short stop
            else if (v == 'N') addMora(kHum, '-', 0.6f);
            else if (std::isupper(static_cast<unsigned char>(v)) && !syllables.empty() && syllables.back().start + syllables.back().length + 0.02f >= t)
                syllables.back().sound = vowelOf(v); // small kana: きゃ, ファ
            else addMora(vowelOf(v), kKanaStarts[k - 0x3041]);
        } else if (c == 0x30FC && !syllables.empty()) { // ー: the vowel goes on
            syllables.back().length += 0.09f * slow;
            t += 0.09f * slow;
        } else if (c >= 0xAC00 && c <= 0xD7A3) { // Korean: a syllable a block, with its vowel; ㅇ first is no consonant
            endWord();
            const Uint32 block = c - 0xAC00;
            add(vowelOf(kHangulVowels[(block / 28) % 21]), block / 588 != 11, false);
        } else { // a letter of another script (kanji, hanzi...): a syllable each, or one or two morae in Japanese
            endWord();
            const std::uint64_t h = fnv1a(std::string_view(reinterpret_cast<const char*>(&c), sizeof c));
            for (int n = japanese && h % 5 < 3 ? 2 : 1, i = 0; i < n; ++i) {
                const Vowel sound = kVowels[(h >> (8 * i)) % kVowels.size()];
                if (japanese) addMora(sound, "kgsznn-"[(h >> (8 * i + 4)) % 7]);
                else add(sound, true, false);
            }
        }
    }
    endWord();
    if (sentence < syllables.size()) endSentence('.');
    if (syllables.empty()) return {};

    // Each syllable: a buzz at the voice's pitch, its harmonics loudest near the vowel's formants
    // (lower voices get lower formants), faded in and out.
    const float scale = std::clamp(0.82f + (pitch - 110.0f) / 500.0f, 0.82f, 1.0f);
    constexpr int kMostHarmonics = 64;
    std::array<float, kMostHarmonics> loudness{};
    std::vector<float> out(static_cast<std::size_t>((t + 0.15f) * static_cast<float>(rate)), 0.0f);
    const auto fr = static_cast<float>(rate);
    float phase = 0.0f;
    for (std::size_t index = 0; index < syllables.size(); ++index) {
        const Syllable& s = syllables[index];
        const auto first = static_cast<std::size_t>(s.start * fr);
        // A mora right after another flows on from it: the same buzz goes on, gliding to the new vowel.
        const Syllable* before = index > 0 && s.mora && syllables[index - 1].mora && std::abs(syllables[index - 1].start + syllables[index - 1].length - s.start) < 0.001f
                                     ? &syllables[index - 1] : nullptr;
        const bool after = index + 1 < syllables.size() && syllables[index + 1].mora && std::abs(s.start + s.length - syllables[index + 1].start) < 0.001f;
        if (s.consonant && !s.mora) {
            const auto n = static_cast<std::size_t>(0.03f * fr);
            float last = 0.0f;
            for (std::size_t i = 0; i < n && first >= n; ++i) {
                const float noise = (jitter(2001) - 1000.0f) / 1000.0f;
                out[first - n + i] += 0.05f * (noise - last) * std::sin(3.14159265f * static_cast<float>(i) / static_cast<float>(n));
                last = noise;
            }
        }
        const auto count = static_cast<std::size_t>(s.length * fr);
        if (!before) phase = 0.0f;
        int harmonics = 0;
        float last = 0.0f; // (the hiss's last noise sample)
        for (std::size_t i = 0; i < count && first + i < out.size(); ++i) {
            const float tt = static_cast<float>(i) / fr;
            const float f0 = (s.from + (s.to - s.from) * tt / s.length) * (1.0f + 0.012f * std::sin(kTau * 5.2f * (s.start + tt)));
            if (i % 128 == 0) { // the pitch moves slowly, so this is worked out now and then
                const float glide = before ? std::min(1.0f, tt / 0.035f) : 1.0f; // from the vowel before
                const Vowel v{before ? before->sound.f1 + (s.sound.f1 - before->sound.f1) * glide : s.sound.f1,
                              before ? before->sound.f2 + (s.sound.f2 - before->sound.f2) * glide : s.sound.f2};
                harmonics = std::min(kMostHarmonics, static_cast<int>(4500.0f / f0));
                for (int h = 1; h <= harmonics; ++h) {
                    const float f = f0 * static_cast<float>(h);
                    auto peak = [&](float centre, float width) { return std::exp(-std::pow((f - centre * scale) / width, 2.0f)); };
                    loudness[static_cast<std::size_t>(h - 1)] = peak(v.f1, 110) + 0.7f * peak(v.f2, 170) + 0.35f * peak(2900, 250) + 0.2f / static_cast<float>(h);
                }
            }
            phase = std::fmod(phase + kTau * f0 / fr, kTau);
            const float c1 = std::cos(phase), s1 = std::sin(phase);
            float ch = c1, sh = s1; // cos and sin of h * phase, by turning h times
            float v = 0.0f;
            for (int h = 0; h < harmonics; ++h) {
                v += loudness[static_cast<std::size_t>(h)] * sh;
                const float next = ch * c1 - sh * s1;
                sh = sh * c1 + ch * s1;
                ch = next;
            }
            if (!s.mora) {
                const float env = std::min(1.0f, tt / 0.018f) * std::min(1.0f, (s.length - tt) / 0.04f) * (0.75f + 0.25f * std::sin(3.14159265f * tt / s.length));
                out[first + i] += v * env * 0.12f;
                continue;
            }
            // A mora: faded in and out only where the voice starts and stops, and its consonant as a dip
            // in the voice: silence for a stop (k), less for a voiced one (g), a hiss (s, with some voice
            // for z), or just a little quieter (n).
            const float close = s.starts == 'k' ? 0.032f : s.starts == 's' ? 0.04f : s.starts == '-' ? 0.0f : 0.026f;
            const float floor = s.starts == 'g' ? 0.3f : s.starts == 'z' ? 0.35f : s.starts == 'n' ? 0.55f : 0.0f;
            const float open = close > 0.0f ? std::clamp((tt - close) / 0.012f, 0.0f, 1.0f) : 1.0f;
            const float env = (before ? 1.0f : std::min(1.0f, tt / 0.015f)) * (after ? 1.0f : std::min(1.0f, (s.length - tt) / 0.04f)) * (floor + (1.0f - floor) * open);
            float hiss = 0.0f;
            if ((s.starts == 's' || s.starts == 'z' || s.starts == 'k') && tt < close + 0.006f) { // breath (a stop: a short burst as it opens)
                const float noise = (jitter(2001) - 1000.0f) / 1000.0f;
                const float shape = s.starts == 'k' ? std::max(0.0f, 1.0f - std::abs(tt - close) / 0.006f) : std::sin(3.14159265f * std::min(1.0f, tt / (close + 0.006f)));
                hiss = (s.starts == 'z' ? 0.02f : 0.035f) * (noise - last) * shape;
                last = noise;
            }
            out[first + i] += (v * env * 0.12f + hiss) * s.level;
        }
    }
    float top = 0.0f;
    for (float v : out) top = std::max(top, std::abs(v));
    if (top > 0.0f)
        for (float& v : out) v *= 0.7f / top;
    return out;
}

// ---------------------------------------------------------------------------
// Videos
// ---------------------------------------------------------------------------

struct Movie::Callbacks {
    static void video(plm_t*, plm_frame_t* frame, void* user) {
        auto* self = static_cast<Movie*>(user);
        if (!self->texture_) return;
        const SDL_Rect area{0, 0, static_cast<int>(frame->width), static_cast<int>(frame->height)};
        SDL_UpdateYUVTexture(self->texture_.get(), &area, frame->y.data, static_cast<int>(frame->y.width), frame->cb.data,
                             static_cast<int>(frame->cb.width), frame->cr.data, static_cast<int>(frame->cr.width));
        ++self->frames_;
    }
    static void audio(plm_t*, plm_samples_t* samples, void* user) {
        withoutThrowing([&] { static_cast<Movie*>(user)->pushSound(samples->interleaved, static_cast<int>(samples->count)); }); // (pl_mpeg is C)
    }
};

void Movie::PlmDeleter::operator()(plm_t* p) const noexcept { plm_destroy(p); }

Movie::Movie(SDL_Renderer* renderer, Audio& audio, const std::string& path, bool loop, float volume)
    : renderer_(renderer), audio_(audio), sound_(nullptr, StreamCloser{&audio}) {
    if (const auto dot = path.rfind('.'); dot != std::string::npos && std::ranges::contains(kSystemVideoTypes, lower(path.substr(dot)))) {
        SDL_PathInfo info{};
        const bool onDisk = SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
        const auto bytes = onDisk ? std::nullopt : audio.files().read(path); // (in a pack)
        if (onDisk || bytes) system_ = SystemVideo::open(path, bytes ? &*bytes : nullptr);
        if (!system_) {
#if defined(__linux__)
            logWarn("Can't play '{}': on Linux, MP4, MOV and M4V videos play with GStreamer and its plugins installed (an MPEG-1 video, .mpg, plays everywhere).", path);
#elif defined(__APPLE__) || defined(_WIN32)
            logWarn("Can't play '{}'.", path);
#else
            logWarn("Can't play '{}': MP4, MOV and M4V videos play on Macs, Windows and Linux (an MPEG-1 video, .mpg, plays everywhere).", path);
#endif
            return;
        }
        const SystemVideo::Info& video = system_->info();
        width_ = static_cast<float>(video.width);
        height_ = static_cast<float>(video.height);
        texture_.reset(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, video.width, video.height));
        if (!texture_) logWarn("Can't show '{}': {}.", path, SDL_GetError());
        else SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_LINEAR);
        loop_ = loop;
        if (video.sound) sound_.reset(audio_.openStream(SystemVideo::kSoundRate, volume));
        if (!system_->nextPicture(picture_, pitch_, pictureTime_)) pictureTime_ = -1.0;
        return;
    }
    auto data = audio.files().read(path);
    if (!data) return;
    data_ = std::move(*data);
    plm_.reset(plm_create_with_memory(reinterpret_cast<uint8_t*>(data_.data()), data_.size(), 0));
    if (plm_ && plm_get_num_video_streams(plm_.get()) == 0) plm_probe(plm_.get(), 1024 * 1024); // headers that don't say
    if (!plm_ || plm_get_num_video_streams(plm_.get()) == 0) {
        logWarn("Can't play '{}': it isn't an MPEG-1 video (.mpg).", path);
        plm_.reset();
        return;
    }
    const int w = plm_get_width(plm_.get()), h = plm_get_height(plm_.get());
    const double aspect = plm_get_pixel_aspect_ratio(plm_.get());
    width_ = static_cast<float>(w * (aspect > 0.0 ? aspect : 1.0));
    height_ = static_cast<float>(h);
    const SdlProperties props;
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_IYUV);
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(props.id(), SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT601_LIMITED); // MPEG-1's colours
    texture_.reset(SDL_CreateTextureWithProperties(renderer_, props.id()));
    if (!texture_) logWarn("Can't show '{}': {}.", path, SDL_GetError());
    else SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_LINEAR);

    plm_set_loop(plm_.get(), loop);
    plm_set_video_decode_callback(plm_.get(), &Callbacks::video, this);
    if (plm_get_num_audio_streams(plm_.get()) > 0) sound_.reset(audio_.openStream(plm_get_samplerate(plm_.get()), volume));
    if (sound_) {
        plm_set_audio_decode_callback(plm_.get(), &Callbacks::audio, this);
        plm_set_audio_lead_time(plm_.get(), 0.15); // decoded a little ahead, so the sound never runs dry
    } else {
        plm_set_audio_enabled(plm_.get(), 0);
    }
}

void Movie::showFirst() {
    for (int i = 0; i < 20 && !texture() && !ended(); ++i) update(0.02f); // (its first picture may come a little after the start)
}

void Movie::setLoop(bool loop) {
    loop_ = loop;
    if (plm_) plm_set_loop(plm_.get(), loop);
}

void Movie::update(float dt) {
    if (system_) return updateSystem(dt);
    if (!plm_ || paused_ || plm_has_ended(plm_.get())) return;
    plm_decode(plm_.get(), static_cast<double>(dt));
}

// The pictures whose time has come (the last shown), and its sound kept half a second ahead.
void Movie::updateSystem(float dt) {
    if (paused_ || ended()) return;
    time_ += dt;
    bool shown = false;
    while (pictureTime_ >= 0.0 && pictureTime_ <= time_) {
        shown = true;
        std::swap(picture_, shownPicture_);
        const int pitch = pitch_;
        if (!system_->nextPicture(picture_, pitch_, pictureTime_)) pictureTime_ = -1.0;
        if (texture_ && !shownPicture_.empty()) SDL_UpdateTexture(texture_.get(), nullptr, shownPicture_.data(), pitch);
    }
    if (shown) ++frames_;
    constexpr int kAhead = SystemVideo::kSoundRate * 2 * static_cast<int>(sizeof(float)) / 2;
    while (sound_ && !soundEnded_ && SDL_GetAudioStreamQueued(sound_.get()) < kAhead) {
        if (!system_->nextSound(samples_)) soundEnded_ = true;
        else pushSound(samples_.data(), static_cast<int>(samples_.size() / 2));
    }
    if (pictureTime_ < 0.0 && loop_) { // (round again)
        system_->restart();
        time_ = 0.0;
        soundEnded_ = false;
        if (!system_->nextPicture(picture_, pitch_, pictureTime_)) pictureTime_ = -1.0;
    }
}

void Movie::setPaused(bool paused) {
    if (paused == paused_) return;
    paused_ = paused;
    if (sound_) audio_.pauseStream(sound_.get(), paused);
}

bool Movie::ended() const {
    if (system_) return pictureTime_ < 0.0 && !loop_;
    return !plm_ || plm_has_ended(plm_.get());
}

double Movie::time() const {
    if (system_) return time_;
    return plm_ ? plm_get_time(plm_.get()) : 0.0;
}

double Movie::duration() const {
    if (system_) return system_->info().duration;
    return plm_ ? plm_get_duration(plm_.get()) : 0.0;
}

void Movie::pushSound(const float* interleaved, int count) {
    if (sound_ && count > 0) SDL_PutAudioStreamData(sound_.get(), interleaved, count * 2 * static_cast<int>(sizeof(float)));
}

} // namespace vn
