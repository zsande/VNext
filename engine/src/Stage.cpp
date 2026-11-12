// The stage: who and what is shown (characters, images, backgrounds), how it moves, the camera, the weather and
// the effects over it all; faces blinking and mouths moving.
#include "Game.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <numbers>
#include <random>

namespace vn {

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

namespace {

std::array<float, 8> values(const Stage::ObjectState& s) {
    return {s.x, s.y, s.zoom, s.opacity, s.rotation, static_cast<float>(s.tint.r), static_cast<float>(s.tint.g), static_cast<float>(s.tint.b)};
}

void setValues(Stage::ObjectState& s, const std::array<float, 8>& v) {
    s.x = v[0];
    s.y = v[1];
    s.zoom = v[2];
    s.opacity = v[3];
    s.rotation = v[4];
    auto u8 = [](float c) { return static_cast<Uint8>(std::clamp(std::lround(c), 0L, 255L)); };
    s.tint = {u8(v[5]), u8(v[6]), u8(v[7]), 255};
}

} // namespace

void Stage::setBackground(std::string key, std::shared_ptr<const Animation> entrance, bool done, bool loop, float volume, SDL_FPoint part) {
    if (key == background_ && !key.empty()) { // (the one there: the animation, on it, and the part that shows)
        backgroundPart_ = part;
        return backgroundAnimations_.set(std::move(entrance), false, done);
    }
    if (!previous_.empty()) dropPrevious(); // (one still coming in: it's in)
    previousPart_ = std::exchange(backgroundPart_, part);
    previous_ = std::exchange(background_, std::move(key));
    previousAnimations_ = std::exchange(backgroundAnimations_, {});
    previousMovie_ = std::exchange(backgroundMovie_, movieFor(background_, loop, volume)); // (the one going plays on under it)
    backgroundLoop_ = loop;
    backgroundVolume_ = volume;
    backgroundAnimations_.set(std::move(entrance), false, done || still_);
    if (!backgroundAnimations_.going()) dropPrevious();
}

void Stage::dropPrevious() {
    previous_.clear();
    previousAnimations_ = {};
    previousMovie_ = {};
    std::erase_if(objects_, [](const Object& o) { return o.oldBackground; });
}

Stage::Playing Stage::movieFor(const std::string& name, bool loop, float volume) const {
    Playing p{.movie = makeMovie_ && !name.empty() ? makeMovie_(name, loop, volume) : nullptr, .loop = loop};
    if (p.movie && moviesPaused_) p.movie->setPaused(true);
    return p;
}

Image Stage::Playing::image() const {
    SDL_Texture* t = movie ? movie->texture() : nullptr;
    if (!t) return {};
    float w = 0, h = 0;
    SDL_GetTextureSize(t, &w, &h);
    return Image{.texture = t, .src = {0, 0, w, h}};
}

void Stage::Playing::settle() {
    if (!once()) return;
    done = true;
    movie->showLast();
    movie->setPaused(true);
}

void Stage::skipMovies() {
    backgroundMovie_.settle();
    for (Object& o : objects_) o.movie.settle();
}

void Stage::pauseMovies(bool paused) {
    moviesPaused_ = paused;
    for (Playing* p : {&backgroundMovie_, &previousMovie_})
        if (p->movie && !p->done) p->movie->setPaused(paused);
    for (Object& o : objects_)
        if (o.movie.movie && !o.movie.done) o.movie.movie->setPaused(paused);
}

Stage::Object* Stage::find(std::string_view tag) {
    auto it = std::ranges::find_if(objects_, [&](const Object& o) { return o.s.tag == tag; });
    return it != objects_.end() ? &*it : nullptr;
}

void Stage::apply(Object& o, const StageProps& p, float seconds, Ease ease) {
    const bool tweening = o.tweenTime < 1.0f;
    std::array<float, kTweened> target = tweening ? o.to : values(o.s);
    if (p.x) target[0] = *p.x;
    if (p.y) target[1] = *p.y;
    if (p.zoom) target[2] = std::max(0.0f, *p.zoom);
    if (p.opacity) target[3] = std::clamp(*p.opacity, 0.0f, 1.0f);
    if (p.rotation) target[4] = *p.rotation;
    if (p.tint) target[5] = p.tint->r, target[6] = p.tint->g, target[7] = p.tint->b;
    if (p.flip) o.s.flip = *p.flip;
    if (p.layer) o.s.layer = *p.layer;
    if (seconds <= 0.0f) {
        setValues(o.s, target);
        o.tweenTime = 1.0f;
    } else {
        o.from = values(o.s);
        o.to = target;
        o.tweenTime = 0.0f;
        o.tweenDuration = seconds;
        o.ease = ease;
    }
}

void Stage::show(const cmd::Show& line, const CharacterDef* def, std::shared_ptr<const Animation> played, float slide, bool done) {
    auto play = [&](Object& o) {
        if (!played) return;
        o.s.played = played->name;
        o.animations.set(std::move(played), false, done || still_);
    };
    if (Object* o = find(line.tag)) {
        if (o->leaving || o->oldBackground) { // (going: it stays, as it was before it began to go)
            o->leaving = o->oldBackground = false;
            o->s.played.clear();
            o->animations.set(nullptr, false, true);
        }
        if (!line.expression.empty()) o->s.expression = line.expression;
        // (a colour's box shown again by its name alone, no type said: the colour it is)
        const bool sameBox = o->s.image.starts_with('#') && !line.box && lower(line.image) == line.tag;
        if (!line.image.empty() && !sameBox) o->s.image = line.image;
        if (line.box) o->s.width = line.box->x, o->s.height = line.box->y;
        apply(*o, line.props, slide);
        play(*o);
        // Shown again: in front of the others of its layer, as one just come is (and last among them, so a save keeps that).
        o->order = nextOrder_++;
        const auto at = std::ranges::find_if(objects_, [&](const Object& x) { return &x == o; });
        std::rotate(at, at + 1, objects_.end());
        return;
    }
    Object o{.s = {.tag = line.tag, .image = line.image, .expression = line.expression, .loop = line.loop, .volume = line.volume}, .order = nextOrder_++};
    if (def) {
        o.s.character = def->key;
        o.def = *def;
    } else { // images start in the middle (characters stand at the bottom); a movie plays
        o.s.y = 0.5f;
        o.movie = movieFor(line.image, line.loop, line.volume);
    }
    if (line.box) o.s.width = line.box->x, o.s.height = line.box->y;
    // Its kind's layer, then where it comes and its type's layer (its character's or visual's, the type the line names),
    // then the line's own.
    o.s.layer = visualTypeLayer(def ? "character" : o.movie.movie ? "movie" : "image");
    apply(o, line.comes, 0.0f);
    apply(o, line.props, 0.0f);
    play(o);
    objects_.push_back(std::move(o));
}

bool Stage::has(std::string_view tag) const {
    return std::ranges::any_of(objects_, [&](const Object& o) { return o.s.tag == tag && !o.leaving && !o.oldBackground; });
}

void Stage::setExpression(std::string_view tag, const std::string& expression) {
    if (Object* o = find(tag)) o->s.expression = expression;
}

std::string Stage::expression(std::string_view tag) const {
    const auto it = std::ranges::find_if(objects_, [&](const Object& o) { return o.s.tag == tag && !o.leaving; });
    return it != objects_.end() ? it->s.expression : std::string{};
}

void Stage::hide(std::string_view tag, std::shared_ptr<const Animation> exit, bool done) {
    Object* o = find(tag);
    if (!o) return;
    if (!exit || done || still_) {
        std::erase_if(objects_, [&](const Object& x) { return x.s.tag == tag; });
        return;
    }
    o->leaving = true;
    o->s.played = exit->name;
    o->animations.set(std::move(exit), false, false);
}

void Stage::Animations::set(std::shared_ptr<const Animation> animation, bool isOwn, bool done) {
    std::shared_ptr<const Animation>& now = isOwn ? own : played;
    if (isOwn && now && animation && now->name == animation->name) return; // (the one it has goes on)
    (isOwn ? ownTime : playedTime) = done && animation && !animation->loops() ? animation->total() : 0.0f;
    now = std::move(animation);
}

void Stage::Animations::onward(float dt, bool still) {
    for (auto [a, time] : {std::pair{own.get(), &ownTime}, std::pair{played.get(), &playedTime}}) {
        if (!a) continue;
        if (still) { // (as it ends; one that plays again and again, standing at its start)
            if (!a->loops()) *time = a->total();
            continue;
        }
        *time += dt;
        if (!a->loops()) *time = std::min(*time, a->total());
        else if (a->length > 0.0f && *time >= 2.0f * a->length) *time = a->length + std::fmod(*time - a->length, a->length);
    }
}

void Stage::Animations::finish() {
    if (played) playedTime = played->loops() ? std::max(playedTime, played->length) : played->total(); // (at its end, or round again)
    if (own && !own->loops()) ownTime = own->total();
}

bool Stage::Animations::going() const { return played && playedTime < played->total(); }

Animation::Look Stage::Animations::look(SDL_Color tint, const Animation::Offset& offset, SDL_FPoint screen) const {
    Animation::Look out;
    if (own) out = own->at(ownTime, tint, offset, screen);
    if (played) out = out.with(playedLook(out.colour.value_or(tint), offset, screen));
    else if (playedFrom.x != 0.0f || playedFrom.y != 0.0f || playedFrom.opacity != 1.0f || playedFrom.zoom != 1.0f || playedFrom.rotation != 0.0f ||
             playedFrom.colour || playedFrom.fade > 0.0f || playedFrom.wipe < 1.0f)
        out = out.with(playedFrom); // (the camera: where the one before left it, when the one played now is gone from the scripts)
    return out;
}

Animation::Look Stage::Animations::playedLook(SDL_Color tint, const Animation::Offset& offset, SDL_FPoint screen) const {
    return played ? played->at(playedTime, tint, offset, screen, playedFrom) : playedFrom;
}

void Stage::animate(std::string_view tag, std::shared_ptr<const Animation> animation, bool own, bool done) {
    Object* o = find(tag);
    if (!o) return;
    (own ? o->s.animation : o->s.played) = animation ? animation->name : std::string{};
    o->animations.set(std::move(animation), own, done);
}

void Stage::animateBackground(std::shared_ptr<const Animation> animation, bool own, bool done) {
    backgroundAnimations_.set(std::move(animation), own, done);
}

std::string Stage::backgroundAnimation(bool own) const {
    const Animation* a = own ? backgroundAnimations_.own.get() : backgroundAnimations_.played.get();
    return a ? a->name : std::string{};
}

bool Stage::moving() const { // (an animation played on something, until it ends: one time through, if it's looping)
    return camera_.going() || backgroundAnimations_.going() ||
           std::ranges::any_of(objects_, [](const Object& o) { return o.tweenTime < 1.0f || o.animations.going() || o.movie.once(); });
}

// The camera's places: what it looks at, as far from the middle as the place is (x=-1: the left edge).
Animation::Offset Stage::cameraPlaces() const {
    return [w = canvas.w, h = canvas.h](bool across, float place) { return across ? place * w / 2 : -place * h / 2; };
}

Animation::Look Stage::cameraLook() const { return camera_.look({255, 255, 255, 255}, cameraPlaces(), {canvas.w, canvas.h}); }

void Stage::animateCamera(std::shared_ptr<const Animation> animation, bool own, bool done) {
    if (!own) camera_.playedFrom = animation ? cameraStartFor(*animation) : Animation::Look{}; // (none: back to normal)
    camera_.set(std::move(animation), own, done || still_);
}

// Where an animation played on the view starts: what it changes, from where the view is now; the rest,
// where the one played before was taking it (as if it had ended: a click doesn't leave it halfway).
Animation::Look Stage::cameraStartFor(const Animation& next) const {
    const SDL_Color white{255, 255, 255, 255};
    const SDL_FPoint screen{canvas.w, canvas.h};
    const Animation::Look now = camera_.playedLook(white, cameraPlaces(), screen);
    const Animation* was = camera_.played.get();
    Animation::Look out = was && !was->loops() ? was->at(was->total(), white, cameraPlaces(), screen, camera_.playedFrom) : now;
    using V = Animation::Value;
    for (const Animation::Change& c : next.changes)
        switch (c.what) {
        case V::X: out.x = now.x; break;
        case V::Y: out.y = now.y; break;
        case V::Opacity: out.opacity = now.opacity; break;
        case V::Zoom: out.zoom = now.zoom; break;
        case V::Rotation: out.rotation = now.rotation; break;
        case V::Colour: out.colour = now.colour; break;
        case V::Fade: out.fade = now.fade, out.fadeColour = now.fadeColour; break;
        case V::Wipe: out.wipe = now.wipe, out.wipeShape = now.wipeShape; break;
        }
    return out;
}

std::string Stage::cameraAnimation(bool own) const {
    const Animation* a = own ? camera_.own.get() : camera_.played.get();
    return a ? a->name : std::string{};
}

void Stage::restoreCamera(std::shared_ptr<const Animation> own, std::shared_ptr<const Animation> played, const Animation::Look& start) {
    camera_ = {};
    camera_.set(std::move(own), true, true);
    camera_.playedFrom = start;
    camera_.set(std::move(played), false, true);
}

void Stage::setWeather(const cmd::Weather& weather, float fade) {
    if (weather.kind == weather_.kind && !weather.kind.empty()) { // the same kind: changed in place
        const bool more = weather.amount != weather_.amount;
        weather_ = weather;
        if (more) spawn(weather_, particles_);
        return;
    }
    // The old weather fades out while the new one fades in.
    fading_ = std::exchange(weather_, weather);
    fadingParticles_ = std::exchange(particles_, {});
    fadingAlpha_ = weatherAlpha_;
    weatherAlpha_ = fade > 0.0f ? 0.0f : 1.0f;
    if (fade <= 0.0f) fadingParticles_.clear();
    weatherFade_ = std::max(fade, 0.01f);
    spawn(weather_, particles_);
}

// Fills the stage with as many flakes, drops or petals as the weather wants, anywhere on it.
void Stage::spawn(const cmd::Weather& w, std::vector<Particle>& out) {
    struct Kind {
        std::string_view name;
        int count;
        float vy0, vy1, size0, size1, sway;
        SDL_Color colour;
    };
    static constexpr std::array kinds{
        Kind{"rain", 180, 700, 1000, 14, 26, 0, {200, 216, 255, 130}},
        Kind{"snow", 130, 40, 100, 1.5f, 4.5f, 20, {255, 255, 255, 220}},
        Kind{"petals", 45, 40, 90, 5, 9, 30, {255, 183, 213, 230}},
        Kind{"leaves", 30, 50, 110, 7, 12, 40, {217, 130, 43, 240}},
        Kind{"embers", 60, -80, -30, 1.2f, 2.8f, 15, {255, 179, 71, 230}},
    };
    const auto kind = std::ranges::find(kinds, std::string_view(w.kind), &Kind::name);
    if (kind == kinds.end()) return out.clear();
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const auto count = static_cast<std::size_t>(static_cast<float>(kind->count) * w.amount);
    while (out.size() > count) out.pop_back();
    static constexpr std::array<SDL_Color, 3> autumn{{{217, 130, 43, 240}, {192, 57, 43, 240}, {230, 179, 60, 240}}};
    while (out.size() < count) {
        Particle p{.x = unit(rng_) * canvas.w, .y = unit(rng_) * canvas.h};
        p.vy = std::lerp(kind->vy0, kind->vy1, unit(rng_));
        p.vx = w.wind * std::abs(p.vy) * 0.6f + (kind->name == "petals" ? 25.0f : 0.0f);
        p.size = std::lerp(kind->size0, kind->size1, unit(rng_));
        p.angle = unit(rng_) * 360.0f;
        p.spin = (unit(rng_) - 0.5f) * 180.0f;
        p.phase = unit(rng_) * 6.2831853f;
        p.sway = kind->sway * (0.5f + unit(rng_));
        p.colour = w.tint ? *w.tint : kind->name == "leaves" ? autumn[static_cast<std::size_t>(unit(rng_) * 2.99f)] : kind->colour;
        out.push_back(p);
    }
}

void Stage::updateWeather(float dt) {
    const float step = still_ ? 1.0f : dt / weatherFade_; // (still: it comes and goes at once, and stays where it is)
    weatherAlpha_ = weather_.kind.empty() ? 0.0f : std::min(1.0f, weatherAlpha_ + step);
    fadingAlpha_ = std::max(0.0f, fadingAlpha_ - step);
    if (fadingAlpha_ <= 0.0f) fadingParticles_.clear();
    auto move = [&](std::vector<Particle>& ps, const cmd::Weather& w) {
        const float margin = 40.0f;
        for (auto& p : ps) {
            p.x += (p.vx + p.sway * std::cos(clock_ * 1.3f + p.phase)) * w.speed * dt;
            p.y += p.vy * w.speed * dt;
            p.angle += p.spin * w.speed * dt;
            if (p.y > canvas.h + margin) p.y -= canvas.h + 2 * margin; // round again from the other edge
            if (p.y < -margin) p.y += canvas.h + 2 * margin;
            if (p.x > canvas.w + margin) p.x -= canvas.w + 2 * margin;
            if (p.x < -margin) p.x += canvas.w + 2 * margin;
        }
    };
    if (still_) return;
    move(particles_, weather_);
    move(fadingParticles_, fading_);
}

void Stage::drawWeather(SDL_Renderer* r, Assets& assets) {
    auto drawAll = [&](const std::vector<Particle>& ps, const cmd::Weather& w, float alpha) {
        if (alpha <= 0.0f || ps.empty()) return;
        const Image pic = w.image.empty() ? Image{} : assets.image(w.image, {}, colourAlpha_ >= 0.5f ? colourMask_ : 0);
        for (const auto& p : ps) {
            const float x = p.x, y = p.y;
            float a = alpha;
            if (w.kind == "embers") a *= 0.6f + 0.4f * std::sin((still_ ? 0.0f : clock_ * 7.0f) + p.phase); // flicker (still: each as bright as it was)
            const SDL_Color c = withAlpha(shade(p.colour), a);
            if (pic) {
                const float s = p.size * 4.0f;
                pic.draw(r, {x - s * 0.5f, y - s * 0.5f, s, s * pic.h() / std::max(1.0f, pic.w())}, a, p.angle);
            } else if (w.kind == "rain") { // a streak along its path
                const float len = p.size / std::max(1.0f, std::hypot(p.vx, p.vy));
                const SDL_FPoint tail{x - p.vx * len, y - p.vy * len};
                draw::quad(r, {x - 0.8f, y}, {x + 0.8f, y}, {tail.x + 0.8f, tail.y}, {tail.x - 0.8f, tail.y}, c);
            } else if (w.kind == "petals" || w.kind == "leaves") { // a turning diamond
                const float rad = p.angle * 0.0174533f, ca = std::cos(rad), sa = std::sin(rad);
                const float lx = p.size, ly = p.size * (w.kind == "petals" ? 0.55f : 0.45f);
                auto at = [&](float u, float v) { return SDL_FPoint{x + u * ca - v * sa, y + u * sa + v * ca}; };
                draw::quad(r, at(-lx, 0), at(0, -ly), at(lx, 0), at(0, ly), c);
            } else {
                draw::circle(r, x, y, p.size, c);
            }
        }
    };
    drawAll(fadingParticles_, fading_, fadingAlpha_);
    drawAll(particles_, weather_, weatherAlpha_);
}

void Stage::setEffect(const cmd::Effect& e, bool instant) {
    if (e.seconds > 0.0f) { // for a moment, on top (it comes and goes at once)
        burst_ = e.looks;
        burstAmount_ = e.amount;
        burstTime_ = e.seconds;
    } else {
        looks_ = e.looks;
        if (e.looks != 0) lookAmount_ = e.amount; // (none: the ones there go as strong as they were)
        lookFade_ = instant ? 0.0f : e.fade;
    }
    if (instant) updateEffects(0.0f);
}

// Each effect grows toward on or off (all at once without a fade, or for a moment's). The colour
// ones go from one to another at once, and fade only from or to none (or another amount).
void Stage::updateEffects(float dt) {
    unsigned ended = 0;
    if (burstTime_ > 0.0f && (burstTime_ -= dt) <= 0.0f) ended = std::exchange(burst_, 0u);
    const unsigned want = looks_ | burst_, sudden = burst_ | ended;
    const float step = lookFade_ > 0.0f && dt > 0.0f && !still_ ? dt / lookFade_ : 1.0f;
    for (std::size_t i = 0; i < strength_.size(); ++i) {
        const float target = want & (1u << i) ? 1.0f : 0.0f, by = sudden & (1u << i) ? 1.0f : step;
        strength_[i] = strength_[i] < target ? std::min(target, strength_[i] + by) : std::max(target, strength_[i] - by);
    }
    const unsigned colours = want & kColourLooks;
    const float by = sudden & kColourLooks ? 1.0f : step, target = !colours ? 0.0f : burst_ & kColourLooks ? burstAmount_ : lookAmount_;
    if (colours) colourMask_ = colours;
    colourAlpha_ = colourAlpha_ < target ? std::min(target, colourAlpha_ + by) : std::max(target, colourAlpha_ - by);
    if (colourAlpha_ <= 0.0f) colourMask_ = 0;
}

void Stage::paint(Assets& assets, std::string_view name, Frame frame, const std::function<void(const Image&, float)>& draw) const {
    if (colourAlpha_ < 1.0f)
        if (const Image pic = assets.image(name, frame)) draw(pic, 1.0f);
    if (colourAlpha_ > 0.0f)
        if (const Image pic = assets.image(name, frame, colourMask_)) draw(pic, colourAlpha_);
}

SDL_Color Stage::shade(SDL_Color c, bool tint) const {
    if (colourAlpha_ <= 0.0f) return c;
    const SDL_Color to = looked(c, tint ? colourMask_ & ~kLookInvert : colourMask_);
    auto mix = [&](Uint8 a, Uint8 b) { return static_cast<Uint8>(std::lround(std::lerp(static_cast<float>(a), static_cast<float>(b), colourAlpha_))); };
    return {mix(c.r, to.r), mix(c.g, to.g), mix(c.b, to.b), c.a};
}

SDL_Texture* Stage::canvasTexture(SDL_Renderer* r, TexturePtr& texture, int w, int h, SDL_ScaleMode mode) {
    w = std::max(1, w);
    h = std::max(1, h);
    float tw = 0, th = 0;
    if (texture) SDL_GetTextureSize(texture.get(), &tw, &th);
    if (!texture || static_cast<int>(tw) != w || static_cast<int>(th) != h)
        texture.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h));
    if (texture) SDL_SetTextureScaleMode(texture.get(), mode);
    return texture.get();
}

// Blur and pixelate: the stage drawn small, then big over it. A glitch: strips of it moved
// sideways, some in one colour, new ones every few hundredths of a second.
void Stage::drawEffects(SDL_Renderer* r, SDL_Texture* view) {
    float vw = 0, vh = 0;
    SDL_GetTextureSize(view, &vw, &vh);
    const float k = vw / canvas.w;
    const float blur = strength(kLookBlur), pixelate = strength(kLookPixelate), glitch = strength(kLookGlitch);
    const float amount = burst_ & (kLookBlur | kLookPixelate | kLookGlitch) ? burstAmount_ : lookAmount_;
    SDL_SetTextureAlphaModFloat(view, 1.0f);
    const SDL_FRect all = whole();
    SDL_RenderTexture(r, view, nullptr, &all);
    if (glitch > 0.0f) {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        if (strips_.empty() || (!still_ && clock_ >= glitchUntil_)) { // (still: the strips it has stay)
            glitchUntil_ = clock_ + 0.04f + 0.08f * unit(rng_);
            strips_.clear();
            const int count = 3 + static_cast<int>(unit(rng_) * 6.0f);
            for (int i = 0; i < count; ++i) {
                static constexpr std::array<SDL_Color, 3> tints{{{255, 255, 255, 255}, {255, 80, 110, 255}, {80, 230, 255, 255}}};
                strips_.push_back({.y = unit(rng_) * canvas.h, .h = 4.0f + unit(rng_) * canvas.h * 0.08f, .dx = (unit(rng_) - 0.5f) * canvas.w * 0.12f,
                                   .tint = tints[static_cast<std::size_t>(unit(rng_) * 2.99f)]});
            }
        }
        for (const Strip& strip : strips_) {
            const SDL_FRect src{0.0f, strip.y * k, vw, strip.h * k}, dst{strip.dx * glitch * amount, strip.y, canvas.w, strip.h};
            SDL_SetTextureColorMod(view, strip.tint.r, strip.tint.g, strip.tint.b);
            SDL_RenderTexture(r, view, &src, &dst);
        }
        SDL_SetTextureColorMod(view, 255, 255, 255);
    }
    auto shrink = [&](TexturePtr& into, float divide, SDL_Texture* from, SDL_ScaleMode mode) {
        SDL_Texture* t = canvasTexture(r, into, static_cast<int>(canvas.w / divide), static_cast<int>(canvas.h / divide), mode);
        if (!t) return t;
        const RenderTarget _(r, t);
        SDL_SetTextureAlphaModFloat(from, 1.0f);
        SDL_RenderTexture(r, from, nullptr, nullptr);
        return t;
    };
    if (blur > 0.0f) // twice smaller, so it's soft rather than blocky
        if (SDL_Texture* a = shrink(small_, 2.0f + 3.0f * amount, view, SDL_SCALEMODE_LINEAR))
            if (SDL_Texture* b = shrink(smaller_, 4.0f + 10.0f * amount, a, SDL_SCALEMODE_LINEAR)) {
                SDL_SetTextureAlphaModFloat(b, blur);
                SDL_RenderTexture(r, b, nullptr, &all);
            }
    if (pixelate > 0.0f)
        if (SDL_Texture* a = shrink(blocks_, 4.0f + 20.0f * amount, view, SDL_SCALEMODE_NEAREST)) {
            SDL_SetTextureAlphaModFloat(a, pixelate);
            SDL_RenderTexture(r, a, nullptr, &all);
        }
}

// TV static: an image of random dots, a different part of it each frame (still: the same part).
void Stage::drawNoise(SDL_Renderer* r, float alpha) {
    constexpr int w = 960, h = 540; // grains of grey; a part of them, somewhere else each frame, covers the stage
    if (!noise_) {
        noise_.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h));
        if (!noise_) return;
        std::vector<SDL_Color> dots(static_cast<std::size_t>(w * h));
        std::uniform_int_distribution<int> grey(0, 255);
        for (SDL_Color& c : dots) c.r = c.g = c.b = static_cast<Uint8>(grey(rng_)), c.a = 255;
        SDL_UpdateTexture(noise_.get(), nullptr, dots.data(), w * 4);
        SDL_SetTextureScaleMode(noise_.get(), SDL_SCALEMODE_NEAREST);
    }
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    if (!still_ || noiseAt_.x < 0.0f) noiseAt_ = {std::floor(unit(rng_) * w / 3.0f), std::floor(unit(rng_) * h / 3.0f)};
    const SDL_FRect src{noiseAt_.x, noiseAt_.y, w * 2.0f / 3.0f, h * 2.0f / 3.0f}, all = whole();
    SDL_SetTextureAlphaModFloat(noise_.get(), alpha);
    SDL_RenderTexture(r, noise_.get(), &src, &all);
}

void Stage::clearObjects() {
    for (auto& o : objects_) o.oldBackground = true;
}

void Stage::reset() {
    background_.clear();
    previous_.clear();
    backgroundPart_ = previousPart_ = {0.5f, 0.5f};
    backgroundAnimations_ = previousAnimations_ = {};
    backgroundMovie_ = {};
    previousMovie_ = {};
    objects_.clear();
    setWeather({}, 0.0f);
    camera_ = {};
    burst_ = 0;
    burstTime_ = 0.0f;
    setEffect({}, true);
}

void Stage::restore(const ObjectState& state, const CharacterDef* def) {
    Object o{.s = state, .order = nextOrder_++};
    if (def) o.def = *def;
    else o.movie = movieFor(state.image, state.loop, state.volume);
    objects_.push_back(std::move(o));
}

void Stage::update(float dt) {
    clock_ += dt;
    if (!moviesPaused_) {
        for (Playing* p : {&backgroundMovie_, &previousMovie_})
            if (p->movie && !p->done) p->movie->update(dt);
        for (Object& o : objects_)
            if (o.movie.movie && !o.movie.done) o.movie.movie->update(dt);
    }
    updateWeather(dt);
    updateEffects(dt);
    camera_.onward(dt, still_);
    backgroundAnimations_.onward(dt, still_);
    previousAnimations_.onward(dt, still_);
    if (!previous_.empty() && !backgroundAnimations_.going()) dropPrevious(); // (the new one has come in)
    for (auto& o : objects_) {
        o.animations.onward(dt, still_);
        if (o.tweenTime < 1.0f) {
            o.tweenTime = o.tweenDuration > 0.0f && !still_ ? std::min(1.0f, o.tweenTime + dt / o.tweenDuration) : 1.0f;
            const float t = eased(o.ease, o.tweenTime);
            std::array<float, kTweened> v{};
            for (std::size_t i = 0; i < kTweened; ++i) v[i] = std::lerp(o.from[i], o.to[i], t);
            setValues(o.s, v);
        }
    }
    std::erase_if(objects_, [](const Object& o) { return o.leaving && !o.animations.going(); }); // (hidden: gone once its animation ends)
}

void Stage::finish() {
    if (!previous_.empty()) dropPrevious();
    backgroundAnimations_.finish();
    for (auto& o : objects_) {
        if (o.tweenTime < 1.0f) {
            setValues(o.s, o.to);
            o.tweenTime = 1.0f;
        }
        o.animations.finish();
    }
    std::erase_if(objects_, [](const Object& o) { return o.leaving; });
    camera_.finish();
    weatherAlpha_ = weather_.kind.empty() ? 0.0f : 1.0f;
    fadingAlpha_ = 0.0f;
    fadingParticles_.clear();
    lookFade_ = 0.0f;
    updateEffects(0.0f);
}

std::vector<Stage::ObjectState> Stage::objects() const {
    std::vector<ObjectState> out;
    for (const auto& o : objects_) {
        if (o.leaving || o.oldBackground) continue;
        ObjectState s = o.s;
        if (o.tweenTime < 1.0f) setValues(s, o.to); // save where it is going
        out.push_back(std::move(s));
    }
    return out;
}

TextBlock& Stage::label(const Font& font, const std::string& text) {
    auto [it, _] = labels_.try_emplace(text);
    it->second.set(font, text);
    return it->second;
}

void Stage::draw(SDL_Renderer* r, Assets& assets, const Font& labelFont, bool labels, float characterHeight, const FrameOf& frameOf) {
    const Font* font = labels ? &labelFont : nullptr;
    // Blur, pixelate and glitch work on the stage drawn into a texture first.
    SDL_Texture* view = nullptr;
    std::optional<RenderTarget> into; // (meanwhile: drawing goes into the view)
    if (strength(kLookBlur) > 0.0f || strength(kLookPixelate) > 0.0f || strength(kLookGlitch) > 0.0f) {
        int outW = 0, outH = 0;
        SDL_GetCurrentRenderOutputSize(r, &outW, &outH);
        const float k = std::clamp(std::min(static_cast<float>(outW) / canvas.w, static_cast<float>(outH) / canvas.h), 1.0f, 4.0f);
        if ((view = canvasTexture(r, view_, static_cast<int>(canvas.w * k), static_cast<int>(canvas.h * k)))) {
            into.emplace(r, view, k);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
        }
    }
    drawView(r, assets, font, characterHeight, frameOf);
    if (view) {
        into.reset();
        drawEffects(r, view);
    }
    if (strength(kLookNoise) > 0.0f) drawNoise(r, 0.35f * strength(kLookNoise) * (burst_ & kLookNoise ? burstAmount_ : lookAmount_));
}

void Stage::drawView(SDL_Renderer* r, Assets& assets, const Font* font, float characterHeight, const FrameOf& frameOf) {
    const Animation::Look look = cameraLook();
    if (look.x == 0.0f && look.y == 0.0f && look.zoom == 1.0f && look.rotation == 0.0f && look.opacity >= 1.0f && !look.colour && look.fade <= 0.0f && look.wipe >= 0.999f)
        return drawStage(r, assets, font, characterHeight, frameOf);
    // The camera's animations have it moved (or fading, tinting, covering or wiping the stage): the stage
    // is drawn into a texture (sharp enough for its zoom), then that is zoomed and turned around the point
    // the camera looks at.
    int outW = 0, outH = 0;
    SDL_GetCurrentRenderOutputSize(r, &outW, &outH);
    const float density = std::max(1.0f, std::min(static_cast<float>(outW) / canvas.w, static_cast<float>(outH) / canvas.h));
    // The view stays inside the stage: looking off its middle without zooming in (a shake, a small move), it zooms
    // in as far as that needs to show nothing past the stage's edges, at most to 125% (further off, it stops at them).
    const float x = 0.5f + look.x / canvas.w, y = 0.5f + look.y / canvas.h;
    const float off = std::max(std::abs(x - 0.5f), std::abs(y - 0.5f));
    const float zoom = look.zoom >= 1.0f ? std::max(look.zoom, 1.0f / std::max(0.8f, 1.0f - 2.0f * off)) : std::max(look.zoom, 0.1f);
    const float scale = std::min(density * std::max(1.0f, zoom), 8192.0f / std::max(canvas.w, canvas.h));
    SDL_Texture* drawn = canvasTexture(r, drawn_, static_cast<int>(std::ceil(canvas.w * scale)), static_cast<int>(std::ceil(canvas.h * scale)));
    if (!drawn) return drawStage(r, assets, font, characterHeight, frameOf);
    {
        const RenderTarget _(r, drawn, scale);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        drawStage(r, assets, font, characterHeight, frameOf);
    }
    // Keep the view inside the stage when zoomed in (turning still shows its corners).
    const float half = 0.5f / zoom;
    const float fx = zoom >= 1.0f ? std::clamp(x, half, 1.0f - half) : x;
    const float fy = zoom >= 1.0f ? std::clamp(y, half, 1.0f - half) : y;
    const SDL_FRect src{0, 0, canvas.w * scale, canvas.h * scale};
    const SDL_FRect dst{canvas.w * 0.5f - fx * canvas.w * zoom, canvas.h * 0.5f - fy * canvas.h * zoom, canvas.w * zoom, canvas.h * zoom};
    const SDL_FPoint pivot{fx * canvas.w * zoom, fy * canvas.h * zoom};
    const float seen = std::clamp(look.opacity, 0.0f, 1.0f);
    const SDL_Color tint = shade(look.colour.value_or(SDL_Color{255, 255, 255, 255}), true);
    SDL_SetTextureColorMod(drawn, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(drawn, static_cast<Uint8>(std::lround(seen * 255.0f)));
    auto paint = [&] {
        SDL_RenderTextureRotated(r, drawn, &src, &dst, look.rotation, &pivot, SDL_FLIP_NONE);
        if (look.fade > 0.0f) draw::rect(r, whole(), withAlpha(shade(look.fadeColour), seen * std::min(1.0f, look.fade))); // (covered with a colour)
    };
    if (look.wipe >= 0.999f) paint();
    else cameraWiper_.draw(r, assets, {canvas.w, canvas.h}, whole(), look.wipe, look.wipeShape, paint);
    SDL_SetTextureColorMod(drawn, 255, 255, 255);
    SDL_SetTextureAlphaMod(drawn, 255);
}

void Stage::drawStage(SDL_Renderer* r, Assets& assets, const Font* font, float characterHeight, const FrameOf& frameOf) {
    // What's on stage, in its layers (and, among those, in the order they came): the background at 0, the ones under 0
    // behind it, the rest in front.
    auto drawObjects = [&](bool old, bool behind) {
        std::vector<const Object*> sorted;
        sorted.reserve(objects_.size());
        for (const auto& o : objects_)
            if (o.oldBackground == old && (o.s.layer < kVisualTypeLayers[0]) == behind) sorted.push_back(&o);
        std::ranges::stable_sort(sorted, [](const Object* a, const Object* b) { return a->s.layer != b->s.layer ? a->s.layer < b->s.layer : a->order < b->order; });
        for (const Object* o : sorted) drawObject(r, assets, font, *o, characterHeight, frameOf ? frameOf(o->s) : Frame{});
    };
    // The background before, with what was on stage then, under the new one while it comes in.
    if (!previous_.empty()) {
        drawObjects(true, true);
        drawBackground(r, assets, font, true);
        drawObjects(true, false);
    }
    drawObjects(false, true);
    drawBackground(r, assets, font, false);
    if (weather_.behind) drawWeather(r, assets);
    drawObjects(false, false);
    if (!weather_.behind) drawWeather(r, assets);
}

void Stage::drawBackground(SDL_Renderer* r, Assets& assets, const Font* font, bool previous) {
    const std::string& key = previous ? previous_ : background_;
    const Animations& animations = previous ? previousAnimations_ : backgroundAnimations_;
    const SDL_FPoint part = previous ? previousPart_ : backgroundPart_; // (of a picture bigger than the stage: the part that shows)
    if (key.empty()) return;
    const SDL_FRect screen = whole();
    // Its animations move it, zoom and turn it around its middle, fade it, tint it, cover it with a
    // colour and wipe it: where it goes when it would be drawn at `box`, and how it looks. (A colour
    // fading down to another doesn't turn.)
    struct Placed {
        SDL_FRect box;
        SDL_FPoint pivot;
        Animation::Look look;
        float alpha;
    };
    auto placed = [&](const SDL_FRect& box) {
        const Animation::Look look = lookOf(animations, {255, 255, 255, 255}, box);
        const float zoom = std::max(0.0f, look.zoom), w = box.w * zoom, h = box.h * zoom;
        return Placed{{box.x + look.x + (box.w - w) / 2, box.y + look.y + (box.h - h) / 2, w, h}, {w / 2, h / 2}, look, std::clamp(look.opacity, 0.0f, 1.0f)};
    };
    auto tinted = [](SDL_Color c, const Animation::Look& look) { // (multiplied by its animations' colour)
        if (!look.colour) return c;
        auto times = [](Uint8 a, Uint8 b) { return static_cast<Uint8>(a * b / 255); };
        return SDL_Color{times(c.r, look.colour->r), times(c.g, look.colour->g), times(c.b, look.colour->b), c.a};
    };
    auto covered = [&](const Placed& at) {
        if (at.look.fade > 0.0f) draw::rect(r, at.box, withAlpha(shade(at.look.fadeColour), at.alpha * std::min(1.0f, at.look.fade)));
    };

    auto wiped = [&](const Placed& at, const auto& paint) { // (a std::function made only for a wipe)
        if (at.look.wipe >= 0.999f) return paint();
        wiper_.draw(r, assets, {canvas.w, canvas.h}, at.box, at.look.wipe, at.look.wipeShape, paint);
    };

    if (key.starts_with('#')) { // a colour, or "#top-#bottom": one fading down to another
        const Placed at = placed(screen);
        auto colourOf = [&](std::string_view text) { return withAlpha(shade(tinted(parseColour(text).value_or(SDL_Color{0, 0, 0, 255}), at.look)), at.alpha); };
        wiped(at, [&] {
            if (const auto dash = key.find('-'); dash != std::string::npos) {
                draw::gradient(r, at.box, colourOf(key.substr(0, dash)), colourOf(key.substr(dash + 1)));
            } else {
                const float turn = at.look.rotation * std::numbers::pi_v<float> / 180.0f, c = std::cos(turn), s = std::sin(turn);
                const SDL_FPoint mid{at.box.x + at.pivot.x, at.box.y + at.pivot.y};
                auto corner = [&](float x, float y) { return SDL_FPoint{mid.x + x * c - y * s, mid.y + x * s + y * c}; };
                const float hw = at.box.w / 2, hh = at.box.h / 2;
                draw::quad(r, corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh), colourOf(key));
            }
            covered(at);
        });
        return;
    }
    auto picture = [&](const Image& pic, float a) {
        const float w = pic.w(), h = pic.h();
        const float scale = std::max(canvas.w / w, canvas.h / h); // cover
        const Placed at = placed({screen.x + (canvas.w - w * scale) * part.x, screen.y + (canvas.h - h * scale) * part.y, w * scale, h * scale});
        wiped(at, [&] {
            pic.draw(r, at.box, at.alpha * a, at.look.rotation, &at.pivot, false, shade(at.look.colour.value_or(SDL_Color{255, 255, 255, 255}), true));
            if (at.look.fade > 0.0f) drawCovered(r, pic, at.box, at.look.rotation, at.pivot, false, shade(at.look.fadeColour), at.alpha * a * std::min(1.0f, at.look.fade));
        });
    };
    if (const Playing& movie = previous ? previousMovie_ : backgroundMovie_; movie.movie) { // (a movie: its image now; none yet: nothing)
        if (const Image pic = movie.image()) picture(pic, 1.0f);
        return;
    }
    if (assets.image(key)) {
        paint(assets, key, {}, picture);
        return;
    }

    // Placeholder: a gradient tinted by the image name.
    const Placed at = placed(screen);
    wiped(at, [&] {
        const float hue = static_cast<float>(fnv1a(key) % 1000) / 1000.0f;
        const auto a = static_cast<Uint8>(255.0f * at.alpha);
        draw::gradient(r, at.box, shade(tinted(draw::hsv(hue, 0.35f, 0.50f, a), at.look)), shade(tinted(draw::hsv(hue + 0.08f, 0.55f, 0.16f, a), at.look)));
        draw::rect(r, {at.box.x, at.box.y + at.box.h * 0.62f, at.box.w, 2}, withAlpha({255, 255, 255, 40}, at.alpha));
        covered(at);
        if (font) label(*font, std::format("images/{}.png", key)).draw(r, at.box.x + 24, at.box.y + 20, withAlpha(theme::dim, at.alpha));
    });
}

void Stage::drawObject(SDL_Renderer* r, Assets& assets, const Font* font, const Object& o, float characterHeight, Frame frame) {
    const ObjectState& s = o.s;
    const float alpha = s.opacity;
    if (alpha <= 0.0f) return;
    const bool character = !s.character.empty();
    std::string key = s.image;
    if (character) {
        key = o.def.imageOf(s.expression);
        if (!assets.image(key, frame) && !s.expression.empty() && assets.image(o.def.image, frame)) key = o.def.image;
    }
    // The point of the image as far across (and down) it as its place is across the screen goes
    // there: 0 lines up the left edges, 0.5 the middles. Past the edges it slides on, still lined up.
    const float px = s.x * canvas.w, py = s.y * canvas.h;
    const float ax = std::clamp(s.x, 0.0f, 1.0f), ay = std::clamp(s.y, 0.0f, 1.0f);
    // Its animations move it, fade it, zoom and turn it around that point (as its own zoom and turn
    // do), tint it, cover it with a colour and wipe it: where an image `w` x `h` (as it is) goes, and how
    // it looks.
    struct Placed {
        SDL_FRect box;
        SDL_FPoint pivot;
        Animation::Look look;
    };
    auto placed = [&](float w, float h) {
        const Animation::Look look = lookOf(o.animations, o.s.tint, {px - ax * w, py - ay * h, w, h});
        const float zoom = std::max(0.0f, look.zoom), zw = w * zoom, zh = h * zoom;
        return Placed{{px + look.x - ax * zw, py + look.y - ay * zh, zw, zh}, {ax * zw, ay * zh}, look};
    };
    auto wiped = [&](const Placed& at, const auto& paint) { // (a std::function made only for a wipe)
        if (at.look.wipe >= 0.999f) return paint();
        wiper_.draw(r, assets, {canvas.w, canvas.h}, at.box, at.look.wipe, at.look.wipeShape, paint);
    };

    if (!character && key.starts_with('#')) { // a colour's box ("#top-#bottom": one fading down to another), as big as its type has it
        const Placed at = placed(s.width * canvas.w * s.zoom, s.height * canvas.h * s.zoom);
        const float seen = alpha * std::clamp(at.look.opacity, 0.0f, 1.0f), cover = std::clamp(at.look.fade, 0.0f, 1.0f);
        const SDL_Color tint = at.look.colour.value_or(s.tint), fadeColour = shade(at.look.fadeColour);
        auto colourOf = [&](std::string_view text) { // (tinted, then covered with its animations' colour as far as they have it)
            const SDL_Color c = parseColour(text).value_or(SDL_Color{0, 0, 0, 255});
            auto times = [](Uint8 a, Uint8 b) { return static_cast<Uint8>(a * b / 255); };
            const SDL_Color tinted = shade({times(c.r, tint.r), times(c.g, tint.g), times(c.b, tint.b), c.a});
            auto toward = [&](Uint8 from, Uint8 to) { return static_cast<Uint8>(std::lround(from + (to - from) * cover)); };
            return withAlpha({toward(tinted.r, fadeColour.r), toward(tinted.g, fadeColour.g), toward(tinted.b, fadeColour.b), tinted.a}, seen);
        };
        wiped(at, [&] {
            if (const auto dash = key.find('-', 1); dash != std::string::npos) { // (fading down: it doesn't turn)
                draw::gradient(r, at.box, colourOf(std::string_view(key).substr(0, dash)), colourOf(std::string_view(key).substr(dash + 1)));
                return;
            }
            const float turn = (s.rotation + at.look.rotation) * std::numbers::pi_v<float> / 180.0f, cs = std::cos(turn), sn = std::sin(turn);
            const SDL_FPoint mid{at.box.x + at.pivot.x, at.box.y + at.pivot.y}; // (it turns around its place, as an image does)
            auto corner = [&](float x, float y) {
                const float dx = x - mid.x, dy = y - mid.y;
                return SDL_FPoint{mid.x + dx * cs - dy * sn, mid.y + dx * sn + dy * cs};
            };
            const float x0 = at.box.x, y0 = at.box.y, x1 = x0 + at.box.w, y1 = y0 + at.box.h;
            draw::quad(r, corner(x0, y0), corner(x1, y0), corner(x1, y1), corner(x0, y1), colourOf(key));
        });
        return;
    }

    auto picture = [&](const Image& pic, float a) {
        const float scale = (character ? std::min(1.0f, canvas.h * characterHeight / pic.h()) : 1.0f) * s.zoom;
        const Placed at = placed(pic.w() * scale, pic.h() * scale);
        const float seen = alpha * a * std::clamp(at.look.opacity, 0.0f, 1.0f), turn = s.rotation + at.look.rotation;
        wiped(at, [&] {
            pic.draw(r, at.box, seen, turn, &at.pivot, s.flip, shade(at.look.colour.value_or(s.tint), true));
            if (at.look.fade > 0.0f) drawCovered(r, pic, at.box, turn, at.pivot, s.flip, shade(at.look.fadeColour), seen * std::min(1.0f, at.look.fade));
        });
    };
    if (o.movie.movie) { // (a movie: its image now; none yet: nothing)
        if (const Image pic = o.movie.image()) picture(pic, 1.0f);
        return;
    }
    if (assets.image(key, frame)) {
        paint(assets, key, frame, picture);
        return;
    }

    // Placeholders, in their tint and the colour effects (and their animations' moves, fades and zooms).
    if (!character) {
        // Placeholder image: a tinted panel with its file name.
        const Placed at = placed(canvas.w * 0.5f * s.zoom, canvas.h * 0.5f * s.zoom);
        const SDL_Color tint = at.look.colour.value_or(s.tint);
        auto look = [&](SDL_Color c) {
            auto times = [](Uint8 a, Uint8 b) { return static_cast<Uint8>(a * b / 255); };
            return shade({times(c.r, tint.r), times(c.g, tint.g), times(c.b, tint.b), c.a});
        };
        const float seen = alpha * std::clamp(at.look.opacity, 0.0f, 1.0f);
        const SDL_FRect rr = at.box;
        wiped(at, [&] {
            const float hue = static_cast<float>(fnv1a(key) % 1000) / 1000.0f;
            draw::gradient(r, rr, look(draw::hsv(hue, 0.3f, 0.55f, static_cast<Uint8>(230 * seen))), look(draw::hsv(hue + 0.1f, 0.5f, 0.25f, static_cast<Uint8>(230 * seen))));
            if (at.look.fade > 0.0f) draw::rect(r, rr, withAlpha(shade(at.look.fadeColour), seen * std::min(1.0f, at.look.fade)));
            draw::outline(r, rr, withAlpha({255, 255, 255, 90}, seen));
            if (font) {
                auto& file = label(*font, std::format("images/{}.png", key));
                file.draw(r, rr.x + (rr.w - file.size().x) * 0.5f, rr.y + (rr.h - file.size().y) * 0.5f, withAlpha(theme::text, seen));
            }
        });
        return;
    }

    // Placeholder silhouette in the character's colour.
    const Placed at = placed(300.0f * s.zoom, 506.0f * s.zoom);
    const SDL_Color tint = at.look.colour.value_or(s.tint);
    auto look = [&](SDL_Color c) {
        auto times = [](Uint8 a, Uint8 b) { return static_cast<Uint8>(a * b / 255); };
        const SDL_Color tinted = shade({times(c.r, tint.r), times(c.g, tint.g), times(c.b, tint.b), c.a});
        auto toward = [&](Uint8 from, Uint8 to) { return static_cast<Uint8>(std::lround(from + (to - from) * std::clamp(at.look.fade, 0.0f, 1.0f))); };
        const SDL_Color cover = shade(at.look.fadeColour);
        return SDL_Color{toward(tinted.r, cover.r), toward(tinted.g, cover.g), toward(tinted.b, cover.b), tinted.a};
    };
    const float alphaSeen = alpha * std::clamp(at.look.opacity, 0.0f, 1.0f);
    const float k = s.zoom * std::max(0.0f, at.look.zoom), w = at.box.w, h = at.box.h;
    const float cx = at.box.x + w * 0.5f, bottom = at.box.y + h;
    const SDL_Color body = withAlpha(look({static_cast<Uint8>(o.def.nameColour.r * 0.55f), static_cast<Uint8>(o.def.nameColour.g * 0.55f),
                                           static_cast<Uint8>(o.def.nameColour.b * 0.55f), 235}),
                                     alphaSeen);
    const SDL_Color rim = withAlpha(look(o.def.nameColour), alphaSeen * 0.9f);
    wiped(at, [&] {
        draw::quad(r, {cx - 105 * k, bottom - 345 * k}, {cx + 105 * k, bottom - 345 * k}, {cx + 150 * k, bottom}, {cx - 150 * k, bottom}, body);
        draw::circle(r, cx, bottom - 440 * k, 66 * k, rim);
        draw::circle(r, cx, bottom - 440 * k, 60 * k, body);
        if (!font) return;
        auto& name = label(*font, o.def.name);
        name.draw(r, cx - name.size().x * 0.5f, bottom - 300 * k, withAlpha(o.def.nameColour, alphaSeen));
        auto& file = label(*font, std::format("images/{}.png", key));
        file.draw(r, cx - file.size().x * 0.5f, bottom - 300 * k + font->lineHeight(), withAlpha(theme::dim, alphaSeen));
    });
}

Animation::Look Stage::lookOf(const Animations& animations, SDL_Color tint, const SDL_FRect& box) const {
    if (!animations.own && !animations.played) return {};
    // A place, as screens' parts have them (±1: its edge on the screen's, ±2: just off it), as how far it is
    // from where it is.
    const auto offset = [&](bool across, float place) { return across ? placeEdge(place, box.w, canvas.w) - box.x : placeEdge(-place, box.h, canvas.h) - box.y; };
    return animations.look(tint, offset, {canvas.w, canvas.h});
}

void Stage::drawCovered(SDL_Renderer* r, const Image& pic, const SDL_FRect& box, float angle, const SDL_FPoint& pivot, bool flip, SDL_Color colour,
                        float amount) {
    if (amount <= 0.0f || box.w <= 0.0f || box.h <= 0.0f) return;
    // Drawn into an image of its own (as sharp as the screen), its colours replaced by the one and its
    // see-through parts kept, then that is laid over it.
    static const SDL_BlendMode fill = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
                                                                 SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    int outW = 0, outH = 0;
    SDL_GetCurrentRenderOutputSize(r, &outW, &outH);
    const float k = std::clamp(static_cast<float>(outW) / canvas.w, 1.0f, 4.0f);
    const int w = std::min(8192, static_cast<int>(std::ceil(box.w * k))), h = std::min(8192, static_cast<int>(std::ceil(box.h * k)));
    if (!cover_ || coverW_ < w || coverH_ < h) { // (bigger, in steps: not made again every frame something grows)
        coverW_ = std::max(coverW_, (w + 255) / 256 * 256);
        coverH_ = std::max(coverH_, (h + 255) / 256 * 256);
        cover_.reset(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, coverW_, coverH_));
        if (cover_) SDL_SetTextureBlendMode(cover_.get(), SDL_BLENDMODE_BLEND);
    }
    bool filled = false;
    if (cover_) {
        const RenderTarget _(r, cover_.get(), 1.0f);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        pic.draw(r, {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)}, 1.0f, 0.0, nullptr, flip);
        if (SDL_SetRenderDrawBlendMode(r, fill)) {
            SDL_SetRenderDrawColor(r, colour.r, colour.g, colour.b, 255);
            const SDL_FRect all{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
            filled = SDL_RenderFillRect(r, &all);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
    if (!filled) return pic.draw(r, box, amount, angle, &pivot, flip, colour);
    const SDL_FRect part{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
    SDL_SetTextureAlphaModFloat(cover_.get(), amount);
    SDL_RenderTextureRotated(r, cover_.get(), &part, &box, angle, &pivot, SDL_FLIP_NONE);
    SDL_SetTextureAlphaModFloat(cover_.get(), 1.0f);
}

} // namespace vn
