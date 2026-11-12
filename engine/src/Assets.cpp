// The game's images (and where its sounds and movies are): found by name, loaded, kept, cut from atlases and
// sub-images, and drawn.
#include "Assets.hpp"

#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace vn {

namespace {

// One texture rectangle; atlas cells are drawn half a pixel in, so neighbours don't bleed in.
void drawCell(SDL_Renderer* r, SDL_Texture* texture, SDL_FRect src, bool cell, const SDL_FRect& dst, float alpha, double angle,
              const SDL_FPoint* pivot, bool flip, SDL_Color tint) {
    if (cell) src = {src.x + 0.5f, src.y + 0.5f, std::max(0.0f, src.w - 1.0f), std::max(0.0f, src.h - 1.0f)};
    SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaModFloat(texture, alpha);
    SDL_RenderTextureRotated(r, texture, &src, &dst, angle, pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

// An image's file: its name with one of these endings, or as written (with its own).
constexpr auto kImageExtensions = [] {
    std::array<std::string_view, kImageTypes.size() + 1> out{};
    std::ranges::copy(kImageTypes, out.begin());
    return out;
}();

// The name an image is kept under: with colour effects, a copy of its own.
std::string cacheKey(std::string_view name, unsigned looks) {
    return looks ? std::format("{}#{}", name, looks) : std::string(name);
}

// An image file as RGBA pixels (to read or change them).
SurfacePtr loadPixels(const GameFiles& files, const std::string& file) {
    const SurfacePtr loaded{IMG_Load_IO(files.open(file).release(), true)};
    return loaded ? SurfacePtr{SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32)} : nullptr;
}

} // namespace

SDL_Color looked(SDL_Color c, unsigned looks) {
    const float r = c.r, g = c.g, b = c.b;
    auto u8 = [](float v) { return static_cast<Uint8>(std::clamp(v, 0.0f, 255.0f)); };
    if (looks & kLookSepia) c = {u8(0.393f * r + 0.769f * g + 0.189f * b), u8(0.349f * r + 0.686f * g + 0.168f * b), u8(0.272f * r + 0.534f * g + 0.131f * b), c.a};
    else if (looks & kLookGreyscale) c.r = c.g = c.b = u8(0.299f * r + 0.587f * g + 0.114f * b);
    if (looks & kLookInvert) c = {static_cast<Uint8>(255 - c.r), static_cast<Uint8>(255 - c.g), static_cast<Uint8>(255 - c.b), c.a};
    return c;
}

Image Image::crop(const SDL_FRect& part) const {
    Image p = *this;
    p.src = {src.x + part.x, src.y + part.y, part.w, part.h};
    p.overlayCount = 0;
    return p;
}

void Image::drawLayers(SDL_Renderer* r, const SDL_FRect& dst, float alpha, double angle, const SDL_FPoint* pivot, bool flip, SDL_Color tint) const {
    const SDL_FPoint p = pivot ? *pivot : SDL_FPoint{dst.w * 0.5f, dst.h * 0.5f};
    drawCell(r, texture, src, frame, dst, alpha, angle, &p, flip, tint);
    const float sx = dst.w / src.w, sy = dst.h / src.h;
    for (std::size_t i = 0; i < overlayCount; ++i) { // turned and mirrored with the image
        const Overlay& o = overlays[i];
        const float x = flip ? src.w - o.at.x - o.src.w : o.at.x;
        const SDL_FRect od{dst.x + x * sx, dst.y + o.at.y * sy, o.src.w * sx, o.src.h * sy};
        const SDL_FPoint op{dst.x + p.x - od.x, dst.y + p.y - od.y};
        drawCell(r, o.texture, o.src, true, od, alpha, angle, &op, flip, tint);
    }
}

void Image::draw(SDL_Renderer* r, const SDL_FRect& dst, float alpha, double angle, const SDL_FPoint* pivot, bool flip, SDL_Color tint) const {
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
    if (const auto slash = name.rfind('/'); slash != std::string_view::npos) // (one in a folder in it, gallery/beach: as that folder has it)
        return std::string(name.substr(0, slash + 1)) + ownName(folder + std::string(name.substr(0, slash + 1)), name.substr(slash + 1), types);
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

std::optional<std::string> Assets::findSound(std::string_view name) const {
    return find("sounds/", name, kSoundTypes);
}

std::optional<std::string> Assets::findMovie(std::string_view name) const {
    return find("movies/", name, kMovieTypes);
}

std::optional<std::string> Assets::findSubtitles(std::string_view movie) const {
    static constexpr std::array<std::string_view, 1> kTypes{".srt"};
    return find("movies/", stemOf(movie), kTypes);
}

void Assets::setLanguage(std::string id) {
    if (!id.empty()) id += '/';
    if (id == language_) return;
    language_ = std::move(id);
    clear();
}

std::string Assets::sound(std::string_view name) const {
    if (auto file = findSound(name)) return *file;
    return name.find('.') == std::string_view::npos ? path(std::format("sounds/{}.ogg", name)) : path(name);
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
    const bool own = !language_.empty() && std::ranges::any_of(kImageTypes, [&](std::string_view ext) { // the language's own image
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
    const auto sub = subImages_.find(lower(name));
    if (sub == subImages_.end()) return filePixels(name);
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

SDL_Texture* Assets::loadTexture(std::string_view name, unsigned looks) {
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
    LoadedImage entry{.texture = std::move(texture), .lastFrame = frame_};
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

// Over the memory limit: lets go of the images not drawn for the longest (never those of the
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
    if (!hasImage(name) && findMovie(name)) return false; // (a movie: it plays where an image goes)
    if (const auto sub = subImages_.find(lower(name)); sub != subImages_.end()) name = sub->second.image;
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
            loadTexture(file, looks);
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

void Assets::setSubImages(const std::vector<SubImage>& parts) {
    subImages_.clear();
    for (const SubImage& p : parts) subImages_.insert_or_assign(p.name, p);
}

Image Assets::image(std::string_view name, Frame frame, unsigned looks) {
    const auto sub = subImages_.find(lower(name));
    if (sub == subImages_.end()) return frameOrFile(name, frame, looks);
    const Image all = file(sub->second.image, looks); // (cut_from= is a file, as pixels() cuts it)
    if (!all) return {};
    SDL_FRect part{};
    const SDL_FRect bounds{0.0f, 0.0f, all.w(), all.h()};
    if (!SDL_GetRectIntersectionFloat(&sub->second.rect, &bounds, &part)) return {};
    Image out = all.crop(part);
    out.frame = true; // (its neighbours on the sheet don't bleed in)
    return out;
}

Image Assets::file(std::string_view name, unsigned looks) {
    SDL_Texture* tex = loadTexture(name, looks);
    if (!tex) return {};
    float w = 0, h = 0;
    SDL_GetTextureSize(tex, &w, &h);
    return {.texture = tex, .src = {0, 0, w, h}};
}

Image Assets::frameOrFile(std::string_view name, Frame frame, unsigned looks) {
    const auto it = frames_.find(lower(name));
    if (it == frames_.end()) return file(name, looks);
    const Atlas& a = atlases_[it->second.first];
    SDL_Texture* tex = loadTexture(a.image, looks);
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
    Image pic{.texture = tex, .src = {fw * static_cast<float>(index % columns), fh * static_cast<float>(index / columns), fw, fh},
                .frame = true, .assets = this};
    // Parts: a row per expression, a column per state.
    auto part = [&](const std::optional<Atlas::Part>& p, int state, std::size_t states) {
        if (!p || states == 0) return;
        SDL_Texture* t = loadTexture(p->image, looks);
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

} // namespace vn
