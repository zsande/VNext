// Shapes: rectangles, outlines, circles, four-cornered shapes, gradients and boxes with round corners; wipes.
#include "Shapes.hpp"
#include "Assets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace vn {

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

void quad(SDL_Renderer* r, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color colour) {
    const SDL_FColor fc = toF(colour);
    const std::array<SDL_Vertex, 4> v{{{a, fc, {}}, {b, fc, {}}, {c, fc, {}}, {d, fc, {}}}};
    static constexpr std::array<int, 6> idx{0, 1, 2, 2, 3, 0};
    SDL_RenderGeometry(r, nullptr, v.data(), 4, idx.data(), 6);
}

void roundRect(SDL_Renderer* r, const SDL_FRect& given, float radius, SDL_Color c, float soft) {
    // A crisp one's straight edges are rects (as sharp as any, with any renderer: the software one puts a
    // shape's points on whole pixels, which would blur a soft edge there), only its corners smoothed; it's on
    // whole pixels too, so its rects and corners meet.
    const bool crisp = soft <= 1.0f;
    SDL_FRect box = given;
    if (crisp) {
        const float left = std::round(given.x), top = std::round(given.y);
        box = {left, top, std::round(given.x + given.w) - left, std::round(given.y + given.h) - top};
        radius = std::round(radius);
    }
    if (box.w <= 0.0f || box.h <= 0.0f) return;
    radius = std::clamp(radius, crisp ? 0.0f : soft / 2, std::max(crisp ? 0.0f : soft / 2, std::min(box.w, box.h) / 2));
    if (crisp) {
        rect(r, {box.x + radius, box.y, box.w - 2 * radius, box.h}, c);
        rect(r, {box.x, box.y + radius, radius, box.h - 2 * radius}, c);
        rect(r, {box.x + box.w - radius, box.y + radius, radius, box.h - 2 * radius}, c);
        if (radius <= 0.0f) return;
    }
    const int steps = std::clamp(static_cast<int>(std::ceil(radius)), 2, 16); // (each corner's)
    const SDL_FColor in = toF(c), out{in.r, in.g, in.b, 0.0f};
    const std::array<SDL_FPoint, 4> centres{{{box.x + radius, box.y + radius},
                                             {box.x + box.w - radius, box.y + radius},
                                             {box.x + box.w - radius, box.y + box.h - radius},
                                             {box.x + radius, box.y + box.h - radius}}};
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    // A corner's round edge, two points each step: one half `soft` inside it (all there), one half outside (none).
    auto edge = [&](std::size_t corner, int i) {
        const float a = std::numbers::pi_v<float> * (1.0f + (static_cast<float>(corner) + static_cast<float>(i) / static_cast<float>(steps)) / 2.0f);
        const SDL_FPoint to{std::cos(a), std::sin(a)}, at = centres[corner];
        v.push_back({{at.x + to.x * (radius - soft / 2), at.y + to.y * (radius - soft / 2)}, in, {}});
        v.push_back({{at.x + to.x * (radius + soft / 2), at.y + to.y * (radius + soft / 2)}, out, {}});
    };
    auto fan = [&](int middle, int a, int b) { idx.insert(idx.end(), {middle, a, b, a, a + 1, b + 1, a, b + 1, b}); }; // (the inside, then the soft edge)
    if (crisp) // (each corner alone, from its centre)
        for (std::size_t corner = 0; corner < centres.size(); ++corner) {
            const int middle = static_cast<int>(v.size());
            v.push_back({centres[corner], in, {}});
            for (int i = 0; i <= steps; ++i) edge(corner, i);
            for (int i = 0; i < steps; ++i) fan(middle, middle + 1 + 2 * i, middle + 3 + 2 * i);
        }
    else { // (all of it as one shape, from its middle)
        v.push_back({{box.x + box.w / 2, box.y + box.h / 2}, in, {}});
        for (std::size_t corner = 0; corner < centres.size(); ++corner)
            for (int i = 0; i <= steps; ++i) edge(corner, i);
        const int n = 4 * (steps + 1);
        for (int i = 0; i < n; ++i) fan(0, 1 + 2 * i, 1 + 2 * ((i + 1) % n));
    }
    SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
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

} // namespace vn
