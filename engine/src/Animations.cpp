// Animations: how a change eases, and where an animation is at a moment.
#include "Script.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vn {

float eased(Ease e, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (e) {
    case Ease::Linear: return t;
    case Ease::In: return t * t;
    case Ease::Out: return 1.0f - (1.0f - t) * (1.0f - t);
    case Ease::InOut: return easeInOut(t);
    case Ease::Bounce: { // (it lands, and bounces lower each time)
        constexpr float n = 7.5625f, d = 2.75f;
        if (t < 1.0f / d) return n * t * t;
        if (t < 2.0f / d) return n * (t - 1.5f / d) * (t - 1.5f / d) + 0.75f;
        if (t < 2.5f / d) return n * (t - 2.25f / d) * (t - 2.25f / d) + 0.9375f;
        return n * (t - 2.625f / d) * (t - 2.625f / d) + 0.984375f;
    }
    case Ease::Spring: // (past it and back, less each time, till it settles)
        if (t <= 0.0f || t >= 1.0f) return t;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * (2.0f * std::numbers::pi_v<float> / 3.0f)) + 1.0f;
    case Ease::Overshoot: { // (a little past it, then back)
        constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
        const float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u;
    }
    }
    return t;
}

Animation::Look Animation::Look::with(const Look& other) const {
    Look out = *this;
    out.x += other.x;
    out.y += other.y;
    out.opacity *= other.opacity;
    out.zoom *= other.zoom;
    out.rotation += other.rotation;
    if (other.colour) out.colour = other.colour;
    if (other.fade > out.fade) {
        out.fade = other.fade;
        out.fadeColour = other.fadeColour;
    }
    if (other.wipe < out.wipe) {
        out.wipe = other.wipe;
        out.wipeShape = other.wipeShape;
    }
    return out;
}

Animation::Look Animation::at(float seconds, SDL_Color own, const Offset& offset, SDL_FPoint screen, const Look& start) const {
    // Where it is in one time through: again and again, a few times (then as its end left it), or once.
    float t = std::max(0.0f, seconds);
    if (length <= 0.0f) t = 0.0f;
    else if (!loops() && t >= total()) t = length;
    else t = std::fmod(t, length);
    // A change's value as it goes: from where it began towards its own, as its easing has it by then.
    auto progress = [](const Change& c, float when) { return eased(c.ease, c.seconds > 0.0f ? (when - c.start) / c.seconds : 1.0f); };
    auto target = [&](const Change& c) {
        switch (c.what) {
        case Value::X: return c.by ? c.to * screen.x : std::isnan(c.to) ? 0.0f : offset(true, c.to); // (home: its own place)
        case Value::Y: return c.by ? -c.to * screen.y : std::isnan(c.to) ? 0.0f : offset(false, c.to);
        case Value::Fade: return c.colour.a == 0 ? 0.0f : 1.0f; // (covered, or uncovered again)
        default: return c.to;
        }
    };
    auto mix = [](SDL_Color a, SDL_Color b, float e) {
        auto channel = [e](Uint8 x, Uint8 y) { return static_cast<Uint8>(std::clamp(std::lround(x + (y - x) * e), 0L, 255L)); };
        return SDL_Color{channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
    };
    // Each value: the latest change of it begun by now, from where the ones before it had taken it.
    auto number = [&](Value what, float initial) {
        const Change* last = nullptr;
        float from = initial;
        for (const Change& c : changes) {
            if (c.what != what) continue;
            if (t < c.start) break;
            from = last ? std::lerp(from, target(*last), progress(*last, c.start)) : initial;
            last = &c;
        }
        return last ? std::lerp(from, target(*last), progress(*last, t)) : initial;
    };
    Look now{.x = number(Value::X, start.x), .y = number(Value::Y, start.y), .opacity = number(Value::Opacity, start.opacity),
             .zoom = number(Value::Zoom, start.zoom), .rotation = number(Value::Rotation, start.rotation), .colour = start.colour,
             .fade = number(Value::Fade, start.fade), .fadeColour = start.fadeColour, .wipe = std::clamp(number(Value::Wipe, start.wipe), 0.0f, 1.0f),
             .wipeShape = start.wipeShape};
    const Change* last = nullptr;
    const SDL_Color first = start.colour.value_or(own);
    SDL_Color from = first;
    for (const Change& c : changes) {
        if (t < c.start) break;
        if (c.what == Value::Fade && c.colour.a != 0) now.fadeColour = {c.colour.r, c.colour.g, c.colour.b, 255}; // (going away again: from the one it had)
        if (c.what == Value::Wipe) now.wipeShape = c.shape;
        if (c.what != Value::Colour) continue;
        from = last ? mix(from, last->colour, progress(*last, c.start)) : first;
        last = &c;
    }
    if (last) now.colour = mix(from, last->colour, progress(*last, t));
    return now;
}

} // namespace vn
