#include "Game.hpp"
#include "System.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <sstream>

namespace vn {

// ---------------------------------------------------------------------------
// The flowchart
// ---------------------------------------------------------------------------

namespace {

constexpr std::string_view kMagic = "vnext-chart 1";

// A save is several lines; in chart.txt it's one.
std::string flatten(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string unflatten(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) out += text[++i] == 'n' ? '\n' : text[i];
        else out += text[i];
    }
    return out;
}

} // namespace

void Chart::visit(const std::string& id, Kind kind, const std::string& from, int option, std::string state,
                  std::vector<Option> options) {
    auto it = std::ranges::find(nodes_, id, &Node::id);
    if (it == nodes_.end()) it = nodes_.insert(nodes_.end(), Node{.id = id, .order = static_cast<int>(nodes_.size())});
    it->kind = kind;
    it->state = std::move(state);
    if (kind == Kind::Choice) it->options = std::move(options);
    if (!from.empty() && from != id &&
        std::ranges::none_of(edges_, [&](const Edge& e) { return e.from == from && e.to == id && e.option == option; }))
        edges_.push_back({from, id, option});
    changed_ = true;
}

const Chart::Node* Chart::node(std::string_view id) const {
    auto it = std::ranges::find(nodes_, id, &Node::id);
    return it != nodes_.end() ? &*it : nullptr;
}

void Chart::clear() {
    nodes_.clear();
    edges_.clear();
    changed_ = true;
}

// chart.txt:  node ID KIND ORDER [OPTION:OPEN ...]   state ID SAVE   edge FROM TO OPTION
bool Chart::read(std::string_view saved) {
    nodes_.clear();
    edges_.clear();
    std::istringstream in{std::string(saved)};
    std::string line;
    if (!std::getline(in, line) || line != kMagic) return false;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key, id;
        ls >> key >> id;
        if (key == "node") {
            Node n{.id = id};
            int kind = 0;
            ls >> kind >> n.order;
            n.kind = static_cast<Kind>(std::clamp(kind, 0, 3));
            for (std::string option; ls >> option;)
                if (const auto colon = option.find(':'); colon != std::string::npos)
                    if (int to = 0; std::from_chars(option.data(), option.data() + colon, to).ec == std::errc{}) n.options.push_back({to, option[colon + 1] == '1'});
            nodes_.push_back(std::move(n));
        } else if (key == "state") {
            std::string state;
            std::getline(ls >> std::ws, state);
            if (auto it = std::ranges::find(nodes_, id, &Node::id); it != nodes_.end()) it->state = unflatten(state);
        } else if (key == "edge") {
            Edge e{.from = id};
            ls >> e.to >> e.option;
            if (!e.to.empty()) edges_.push_back(std::move(e));
        }
    }
    std::ranges::sort(nodes_, {}, &Node::order);
    return true;
}

std::string Chart::text() const {
    std::string out = std::format("{}\n", kMagic);
    for (const Node& n : nodes_) {
        out += std::format("node {} {} {}", n.id, static_cast<int>(n.kind), n.order);
        for (const Option& o : n.options) out += std::format(" {}:{}", o.index, o.open ? 1 : 0);
        out += std::format("\nstate {} {}\n", n.id, flatten(n.state));
    }
    for (const Edge& e : edges_) out += std::format("edge {} {} {}\n", e.from, e.to, e.option);
    return out;
}

// ---------------------------------------------------------------------------
// The stage
// ---------------------------------------------------------------------------

namespace {

std::array<float, 8> values(const Stage::ObjectState& s) {
    return {s.x, s.y, s.zoom, s.opacity, s.rotate, static_cast<float>(s.tint.r), static_cast<float>(s.tint.g), static_cast<float>(s.tint.b)};
}

void setValues(Stage::ObjectState& s, const std::array<float, 8>& v) {
    s.x = v[0];
    s.y = v[1];
    s.zoom = v[2];
    s.opacity = v[3];
    s.rotate = v[4];
    auto u8 = [](float c) { return static_cast<Uint8>(std::clamp(std::lround(c), 0L, 255L)); };
    s.tint = {u8(v[5]), u8(v[6]), u8(v[7]), 255};
}


} // namespace

void Stage::setBackground(std::string key, std::shared_ptr<const Animation> entrance, bool done) {
    if (key == background_ && !key.empty()) return backgroundAnimations_.set(std::move(entrance), false, done); // (the one there: only the animation, on it)
    if (!previous_.empty()) dropPrevious(); // (one still coming in: it's in)
    previous_ = std::exchange(background_, std::move(key));
    previousAnimations_ = std::exchange(backgroundAnimations_, {});
    backgroundAnimations_.set(std::move(entrance), false, done || still_);
    if (!backgroundAnimations_.going()) dropPrevious();
}

void Stage::dropPrevious() {
    previous_.clear();
    previousAnimations_ = {};
    std::erase_if(objects_, [](const Object& o) { return o.oldScene; });
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
    if (p.rotate) target[4] = *p.rotate;
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

void Stage::show(const std::string& tag, const CharacterDef* def, const std::string& image, const std::string& expression,
                 const StageProps& props, std::shared_ptr<const Animation> played, float slide, bool done) {
    auto play = [&](Object& o) {
        if (!played) return;
        o.s.played = played->name;
        o.animations.set(std::move(played), false, done || still_);
    };
    if (Object* o = find(tag)) {
        if (o->leaving || o->oldScene) { // (going: it stays, as it was before it began to go)
            o->leaving = o->oldScene = false;
            o->s.played.clear();
            o->animations.set(nullptr, false, true);
        }
        if (!expression.empty()) o->s.expression = expression;
        if (!image.empty()) o->s.image = image;
        apply(*o, props, slide);
        play(*o);
        return;
    }
    Object o{.s = {.tag = tag, .image = image, .expression = expression}, .order = nextOrder_++};
    if (def) {
        o.s.character = def->key;
        o.def = *def;
    } else { // images start in the middle (characters stand at the bottom)
        o.s.y = 0.5f;
    }
    apply(o, props, 0.0f);
    play(o);
    objects_.push_back(std::move(o));
}

bool Stage::has(std::string_view tag) const {
    return std::ranges::any_of(objects_, [&](const Object& o) { return o.s.tag == tag && !o.leaving && !o.oldScene; });
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
    if (played) out = out.with(playedLook(out.color.value_or(tint), offset, screen));
    else if (playedFrom.x != 0.0f || playedFrom.y != 0.0f || playedFrom.opacity != 1.0f || playedFrom.zoom != 1.0f || playedFrom.rotate != 0.0f ||
             playedFrom.color || playedFrom.fade > 0.0f || playedFrom.wipe < 1.0f)
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
           std::ranges::any_of(objects_, [](const Object& o) { return o.tweenTime < 1.0f || o.animations.going(); });
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
        case V::Rotate: out.rotate = now.rotate; break;
        case V::Color: out.color = now.color; break;
        case V::Fade: out.fade = now.fade, out.fadeColor = now.fadeColor; break;
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

// ---------------------------------------------------------------------------
// Weather
// ---------------------------------------------------------------------------

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
        SDL_Color color;
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
        p.color = w.color ? *w.color : kind->name == "leaves" ? autumn[static_cast<std::size_t>(unit(rng_) * 2.99f)] : kind->color;
        out.push_back(p);
    }
}

void Stage::updateWeather(float dt) {
    const float step = dt / weatherFade_;
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
    move(particles_, weather_);
    move(fadingParticles_, fading_);
}

void Stage::drawWeather(SDL_Renderer* r, Assets& assets) {
    auto drawAll = [&](const std::vector<Particle>& ps, const cmd::Weather& w, float alpha) {
        if (alpha <= 0.0f || ps.empty()) return;
        const Picture pic = w.image.empty() ? Picture{} : assets.picture(w.image, {}, colorAlpha_ >= 0.5f ? colorMask_ : 0);
        for (const auto& p : ps) {
            const float x = p.x, y = p.y;
            float a = alpha;
            if (w.kind == "embers") a *= 0.6f + 0.4f * std::sin(clock_ * 7.0f + p.phase); // flicker
            const SDL_Color c = withAlpha(shade(p.color), a);
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

// ---------------------------------------------------------------------------
// Effects over the whole stage
// ---------------------------------------------------------------------------

void Stage::setEffect(const cmd::Effect& e, bool instant) {
    if (e.seconds > 0.0f) { // for a moment, on top (it comes and goes at once)
        burst_ = e.looks;
        burstAmount_ = e.amount;
        burstTime_ = e.seconds;
    } else {
        looks_ = e.looks;
        lookAmount_ = e.amount;
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
    const float step = lookFade_ > 0.0f && dt > 0.0f ? dt / lookFade_ : 1.0f;
    for (std::size_t i = 0; i < strength_.size(); ++i) {
        const float target = want & (1u << i) ? 1.0f : 0.0f, by = sudden & (1u << i) ? 1.0f : step;
        strength_[i] = strength_[i] < target ? std::min(target, strength_[i] + by) : std::max(target, strength_[i] - by);
    }
    const unsigned colors = want & kColorLooks;
    const float by = sudden & kColorLooks ? 1.0f : step, target = !colors ? 0.0f : burst_ & kColorLooks ? burstAmount_ : lookAmount_;
    if (colors) colorMask_ = colors;
    colorAlpha_ = colorAlpha_ < target ? std::min(target, colorAlpha_ + by) : std::max(target, colorAlpha_ - by);
    if (colorAlpha_ <= 0.0f) colorMask_ = 0;
}

void Stage::paint(Assets& assets, std::string_view name, Frame frame, const std::function<void(const Picture&, float)>& draw) const {
    if (colorAlpha_ < 1.0f)
        if (const Picture pic = assets.picture(name, frame)) draw(pic, 1.0f);
    if (colorAlpha_ > 0.0f)
        if (const Picture pic = assets.picture(name, frame, colorMask_)) draw(pic, colorAlpha_);
}

SDL_Color Stage::shade(SDL_Color c, bool tint) const {
    if (colorAlpha_ <= 0.0f) return c;
    const SDL_Color to = looked(c, tint ? colorMask_ & ~kLookInvert : colorMask_);
    auto mix = [&](Uint8 a, Uint8 b) { return static_cast<Uint8>(std::lround(std::lerp(static_cast<float>(a), static_cast<float>(b), colorAlpha_))); };
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
        if (clock_ >= glitchUntil_) {
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

// TV static: a picture of random dots, a different part of it each frame.
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
    const SDL_FRect src{std::floor(unit(rng_) * w / 3.0f), std::floor(unit(rng_) * h / 3.0f), w * 2.0f / 3.0f, h * 2.0f / 3.0f}, all = whole();
    SDL_SetTextureAlphaModFloat(noise_.get(), alpha);
    SDL_RenderTexture(r, noise_.get(), &src, &all);
}

void Stage::clearObjects() {
    for (auto& o : objects_) o.oldScene = true;
}

void Stage::reset() {
    background_.clear();
    previous_.clear();
    backgroundAnimations_ = previousAnimations_ = {};
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
    objects_.push_back(std::move(o));
}

void Stage::update(float dt) {
    clock_ += dt;
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
        if (o.leaving || o.oldScene) continue;
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

void Stage::draw(SDL_Renderer* r, Assets& assets, const Font& labelFont, bool labels, float spriteHeight, const FrameOf& frameOf) {
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
    drawView(r, assets, font, spriteHeight, frameOf);
    if (view) {
        into.reset();
        drawEffects(r, view);
    }
    if (strength(kLookNoise) > 0.0f) drawNoise(r, 0.35f * strength(kLookNoise) * (burst_ & kLookNoise ? burstAmount_ : lookAmount_));
}

void Stage::drawView(SDL_Renderer* r, Assets& assets, const Font* font, float spriteHeight, const FrameOf& frameOf) {
    const Animation::Look look = cameraLook();
    if (look.x == 0.0f && look.y == 0.0f && look.zoom == 1.0f && look.rotate == 0.0f && look.opacity >= 1.0f && !look.color && look.fade <= 0.0f && look.wipe >= 0.999f)
        return drawScene(r, assets, font, spriteHeight, frameOf);
    // The camera's animations have it moved (or fading, tinting, covering or wiping the stage): the stage
    // is drawn into a texture (sharp enough for its zoom), then that is zoomed and turned around the point
    // the camera looks at.
    int outW = 0, outH = 0;
    SDL_GetCurrentRenderOutputSize(r, &outW, &outH);
    const float density = std::max(1.0f, std::min(static_cast<float>(outW) / canvas.w, static_cast<float>(outH) / canvas.h));
    const float zoom = std::max(look.zoom, 0.1f);
    const float scale = std::min(density * std::max(1.0f, zoom), 8192.0f / std::max(canvas.w, canvas.h));
    SDL_Texture* scene = canvasTexture(r, scene_, static_cast<int>(std::ceil(canvas.w * scale)), static_cast<int>(std::ceil(canvas.h * scale)));
    if (!scene) return drawScene(r, assets, font, spriteHeight, frameOf);
    {
        const RenderTarget _(r, scene, scale);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        drawScene(r, assets, font, spriteHeight, frameOf);
    }
    // Keep the view inside the stage when zoomed in (turning still shows its corners).
    const float half = 0.5f / zoom, x = 0.5f + look.x / canvas.w, y = 0.5f + look.y / canvas.h;
    const float fx = zoom >= 1.0f ? std::clamp(x, half, 1.0f - half) : x;
    const float fy = zoom >= 1.0f ? std::clamp(y, half, 1.0f - half) : y;
    const SDL_FRect src{0, 0, canvas.w * scale, canvas.h * scale};
    const SDL_FRect dst{canvas.w * 0.5f - fx * canvas.w * zoom, canvas.h * 0.5f - fy * canvas.h * zoom, canvas.w * zoom, canvas.h * zoom};
    const SDL_FPoint pivot{fx * canvas.w * zoom, fy * canvas.h * zoom};
    const float seen = std::clamp(look.opacity, 0.0f, 1.0f);
    const SDL_Color tint = shade(look.color.value_or(SDL_Color{255, 255, 255, 255}), true);
    SDL_SetTextureColorMod(scene, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(scene, static_cast<Uint8>(std::lround(seen * 255.0f)));
    auto paint = [&] {
        SDL_RenderTextureRotated(r, scene, &src, &dst, look.rotate, &pivot, SDL_FLIP_NONE);
        if (look.fade > 0.0f) draw::rect(r, whole(), withAlpha(shade(look.fadeColor), seen * std::min(1.0f, look.fade))); // (covered with a colour)
    };
    if (look.wipe >= 0.999f) paint();
    else cameraWiper_.draw(r, assets, {canvas.w, canvas.h}, whole(), look.wipe, look.wipeShape, paint);
    SDL_SetTextureColorMod(scene, 255, 255, 255);
    SDL_SetTextureAlphaMod(scene, 255);
}

void Stage::drawScene(SDL_Renderer* r, Assets& assets, const Font* font, float spriteHeight, const FrameOf& frameOf) {
    // What's on stage, in its layers (and, among those, in the order they came).
    auto drawObjects = [&](bool old) {
        std::vector<const Object*> sorted;
        sorted.reserve(objects_.size());
        for (const auto& o : objects_)
            if (o.oldScene == old) sorted.push_back(&o);
        std::ranges::stable_sort(sorted, [](const Object* a, const Object* b) { return a->s.layer != b->s.layer ? a->s.layer < b->s.layer : a->order < b->order; });
        for (const Object* o : sorted) drawObject(r, assets, font, *o, spriteHeight, frameOf ? frameOf(o->s) : Frame{});
    };
    // The background before, with what was on stage then, under the new one while it comes in.
    if (!previous_.empty()) {
        drawBackground(r, assets, font, true);
        drawObjects(true);
    }
    drawBackground(r, assets, font, false);
    if (weather_.behind) drawWeather(r, assets);
    drawObjects(false);
    if (!weather_.behind) drawWeather(r, assets);
}

void Stage::drawBackground(SDL_Renderer* r, Assets& assets, const Font* font, bool previous) {
    const std::string& key = previous ? previous_ : background_;
    const Animations& animations = previous ? previousAnimations_ : backgroundAnimations_;
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
        if (!look.color) return c;
        auto times = [](Uint8 a, Uint8 b) { return static_cast<Uint8>(a * b / 255); };
        return SDL_Color{times(c.r, look.color->r), times(c.g, look.color->g), times(c.b, look.color->b), c.a};
    };
    auto covered = [&](const Placed& at) {
        if (at.look.fade > 0.0f) draw::rect(r, at.box, withAlpha(shade(at.look.fadeColor), at.alpha * std::min(1.0f, at.look.fade)));
    };

    auto wiped = [&](const Placed& at, const auto& paint) { // (a std::function made only for a wipe)
        if (at.look.wipe >= 0.999f) return paint();
        wiper_.draw(r, assets, {canvas.w, canvas.h}, at.box, at.look.wipe, at.look.wipeShape, paint);
    };

    if (key.starts_with('#')) { // a colour, or "#top-#bottom": one fading down to another
        const Placed at = placed(screen);
        auto colorOf = [&](std::string_view text) { return withAlpha(shade(tinted(parseColor(text).value_or(SDL_Color{0, 0, 0, 255}), at.look)), at.alpha); };
        wiped(at, [&] {
            if (const auto dash = key.find('-'); dash != std::string::npos) {
                draw::gradient(r, at.box, colorOf(key.substr(0, dash)), colorOf(key.substr(dash + 1)));
            } else {
                const float turn = at.look.rotate * std::numbers::pi_v<float> / 180.0f, c = std::cos(turn), s = std::sin(turn);
                const SDL_FPoint mid{at.box.x + at.pivot.x, at.box.y + at.pivot.y};
                auto corner = [&](float x, float y) { return SDL_FPoint{mid.x + x * c - y * s, mid.y + x * s + y * c}; };
                const float hw = at.box.w / 2, hh = at.box.h / 2;
                draw::quad(r, corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh), colorOf(key));
            }
            covered(at);
        });
        return;
    }
    if (assets.picture(key)) {
        paint(assets, key, {}, [&](const Picture& pic, float a) {
            const float w = pic.w(), h = pic.h();
            const float scale = std::max(canvas.w / w, canvas.h / h); // cover
            const Placed at = placed({screen.x + (canvas.w - w * scale) * 0.5f, screen.y + (canvas.h - h * scale) * 0.5f, w * scale, h * scale});
            wiped(at, [&] {
                pic.draw(r, at.box, at.alpha * a, at.look.rotate, &at.pivot, false, shade(at.look.color.value_or(SDL_Color{255, 255, 255, 255}), true));
                if (at.look.fade > 0.0f) drawCovered(r, pic, at.box, at.look.rotate, at.pivot, false, shade(at.look.fadeColor), at.alpha * a * std::min(1.0f, at.look.fade));
            });
        });
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

void Stage::drawObject(SDL_Renderer* r, Assets& assets, const Font* font, const Object& o, float spriteHeight, Frame frame) {
    const ObjectState& s = o.s;
    const float alpha = s.opacity;
    if (alpha <= 0.0f) return;
    const bool character = !s.character.empty();
    std::string key = s.image;
    if (character) {
        key = o.def.pictureOf(s.expression);
        if (!assets.picture(key, frame) && !s.expression.empty() && assets.picture(o.def.image, frame)) key = o.def.image;
    }
    // The point of the picture as far across (and down) it as its place is across the screen goes
    // there: 0 lines up the left edges, 0.5 the middles. Past the edges it slides on, still lined up.
    const float px = s.x * canvas.w, py = s.y * canvas.h;
    const float ax = std::clamp(s.x, 0.0f, 1.0f), ay = std::clamp(s.y, 0.0f, 1.0f);
    // Its animations move it, fade it, zoom and turn it around that point (as its own zoom and turn
    // do), tint it, cover it with a colour and wipe it: where a picture `w` x `h` (as it is) goes, and how
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

    if (assets.picture(key, frame)) {
        paint(assets, key, frame, [&](const Picture& pic, float a) {
            const float scale = (character ? std::min(1.0f, canvas.h * spriteHeight / pic.h()) : 1.0f) * s.zoom;
            const Placed at = placed(pic.w() * scale, pic.h() * scale);
            const float seen = alpha * a * std::clamp(at.look.opacity, 0.0f, 1.0f), turn = s.rotate + at.look.rotate;
            wiped(at, [&] {
                pic.draw(r, at.box, seen, turn, &at.pivot, s.flip, shade(at.look.color.value_or(s.tint), true));
                if (at.look.fade > 0.0f) drawCovered(r, pic, at.box, turn, at.pivot, s.flip, shade(at.look.fadeColor), seen * std::min(1.0f, at.look.fade));
            });
        });
        return;
    }

    // Placeholders, in their tint and the colour effects (and their animations' moves, fades and zooms).
    if (!character) {
        // Placeholder image: a tinted panel with its file name.
        const Placed at = placed(canvas.w * 0.5f * s.zoom, canvas.h * 0.5f * s.zoom);
        const SDL_Color tint = at.look.color.value_or(s.tint);
        auto look = [&](SDL_Color c) {
            auto times = [](Uint8 a, Uint8 b) { return static_cast<Uint8>(a * b / 255); };
            return shade({times(c.r, tint.r), times(c.g, tint.g), times(c.b, tint.b), c.a});
        };
        const float seen = alpha * std::clamp(at.look.opacity, 0.0f, 1.0f);
        const SDL_FRect rr = at.box;
        wiped(at, [&] {
            const float hue = static_cast<float>(fnv1a(key) % 1000) / 1000.0f;
            draw::gradient(r, rr, look(draw::hsv(hue, 0.3f, 0.55f, static_cast<Uint8>(230 * seen))), look(draw::hsv(hue + 0.1f, 0.5f, 0.25f, static_cast<Uint8>(230 * seen))));
            if (at.look.fade > 0.0f) draw::rect(r, rr, withAlpha(shade(at.look.fadeColor), seen * std::min(1.0f, at.look.fade)));
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
    const SDL_Color tint = at.look.color.value_or(s.tint);
    auto look = [&](SDL_Color c) {
        auto times = [](Uint8 a, Uint8 b) { return static_cast<Uint8>(a * b / 255); };
        const SDL_Color tinted = shade({times(c.r, tint.r), times(c.g, tint.g), times(c.b, tint.b), c.a});
        auto toward = [&](Uint8 from, Uint8 to) { return static_cast<Uint8>(std::lround(from + (to - from) * std::clamp(at.look.fade, 0.0f, 1.0f))); };
        const SDL_Color cover = shade(at.look.fadeColor);
        return SDL_Color{toward(tinted.r, cover.r), toward(tinted.g, cover.g), toward(tinted.b, cover.b), tinted.a};
    };
    const float alphaSeen = alpha * std::clamp(at.look.opacity, 0.0f, 1.0f);
    const float k = s.zoom * std::max(0.0f, at.look.zoom), w = at.box.w, h = at.box.h;
    const float cx = at.box.x + w * 0.5f, bottom = at.box.y + h;
    const SDL_Color body = withAlpha(look({static_cast<Uint8>(o.def.color.r * 0.55f), static_cast<Uint8>(o.def.color.g * 0.55f),
                                           static_cast<Uint8>(o.def.color.b * 0.55f), 235}),
                                     alphaSeen);
    const SDL_Color rim = withAlpha(look(o.def.color), alphaSeen * 0.9f);
    wiped(at, [&] {
        draw::quad(r, {cx - 105 * k, bottom - 345 * k}, {cx + 105 * k, bottom - 345 * k}, {cx + 150 * k, bottom}, {cx - 150 * k, bottom}, body);
        draw::circle(r, cx, bottom - 440 * k, 66 * k, rim);
        draw::circle(r, cx, bottom - 440 * k, 60 * k, body);
        if (!font) return;
        auto& name = label(*font, o.def.name);
        name.draw(r, cx - name.size().x * 0.5f, bottom - 300 * k, withAlpha(o.def.color, alphaSeen));
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

void Stage::drawCovered(SDL_Renderer* r, const Picture& pic, const SDL_FRect& box, float angle, const SDL_FPoint& pivot, bool flip, SDL_Color color,
                        float amount) {
    if (amount <= 0.0f || box.w <= 0.0f || box.h <= 0.0f) return;
    // Drawn into a picture of its own (as sharp as the screen), its colours replaced by the one and its
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
            SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
            const SDL_FRect all{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
            filled = SDL_RenderFillRect(r, &all);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
    if (!filled) return pic.draw(r, box, amount, angle, &pivot, flip, color);
    const SDL_FRect part{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
    SDL_SetTextureAlphaModFloat(cover_.get(), amount);
    SDL_RenderTextureRotated(r, cover_.get(), &part, &box, angle, &pivot, SDL_FLIP_NONE);
    SDL_SetTextureAlphaModFloat(cover_.get(), 1.0f);
}

// ---------------------------------------------------------------------------
// The game
// ---------------------------------------------------------------------------

namespace {

constexpr int kInstructionBudget = 100000;
constexpr std::string_view kSaveMagic = "vnext-save 2";

// Saves keep one value per line: spaces, backslashes and newlines are escaped.
std::string escape(std::string_view s) {
    return encodeValue(std::string(s)).substr(2);
}

std::string unescape(std::string_view s) {
    return toString(decodeValue(std::format("s {}", s)));
}

} // namespace

Game::Game(Context ctx, const Script& script)
    : ctx_(std::move(ctx)), script_(script), lookup_([this](std::string_view name) { return variable(name); }), stage_(ctx_.canvas) {}

void Game::start(std::size_t pc, bool subroutine) {
    stage_.reset();
    vars_ = std::exchange(carried_, {});
    callStack_.clear();
    history_.clear();
    choiceItems_.clear();
    trigger_.reset();
    pass_.reset();
    hiddenLines_ = 0;
    others_.clear();
    together_ = false;
    text_.clear();
    screens_.clear();
    textboxScreen_.clear();
    page_.clear();
    messages_.clear();
    phoneOn_ = true;
    loops_.clear();
    expressions_.clear();
    error_.clear();
    music_.clear();
    chapter_.clear();
    chartNode_.clear();
    chartOption_ = -1;
    pendingVoice_.reset();
    past_.clear();
    ctx_.audio.stopAll(0.5f);
    if (subroutine) callStack_.push_back(script_.code.size()); // its "return" ends the game
    pc_ = resumePc_ = pc;
    wait_ = Wait::None;
    run();
}

bool Game::skipping() const {
    return skip_ || (config().skipHold != 0 && (SDL_GetModState() & config().skipHold) != 0);
}

std::string Game::summary() const {
    std::string line;
    if (wait_ == Wait::Menu && menuPrompt_) line = text_;
    else if (!history_.empty()) line = history_.back().speaker.empty() ? history_.back().text : std::format("{}: {}", history_.back().speaker, history_.back().text);
    return chapter_.empty() ? line : line.empty() ? chapter_ : std::format("{}  ·  {}", chapter_, line); // the chapter first
}

void Game::fail(std::string message) {
    error_ = current_ < script_.code.size() ? std::format("{}: {}", script_.where(script_.code[current_]), message)
                                            : std::move(message);
    logError("{}", error_);
    wait_ = Wait::Finished;
}

void Game::run() {
    int budget = kInstructionBudget;
    while (wait_ == Wait::None) {
        if (pc_ >= script_.code.size()) {
            wait_ = Wait::Finished;
            return;
        }
        if (--budget == 0) return fail("The story ran a very long time without showing anything (does a jump go round in a circle?)");
        if (stopAt_ && pc_ == *stopAt_) { // (stopBefore)
            resumePc_ = pc_;
            resumeRandom_ = random_;
            wait_ = Wait::Reached;
            return;
        }
        if (stopWhere_ && stopWhere_(pc_)) { // (stopWhere: the developer tools open there; skipping stops)
            resumePc_ = pc_;
            resumeRandom_ = random_;
            skip_ = false;
            wait_ = Wait::Stopped;
            return;
        }
        const std::size_t here = current_ = pc_++;
        if (ran_.size() < 100000) ran_.push_back(here);
        if (here == watch_) watched_ = true;
        resumePc_ = pc_;
        resumeRandom_ = random_;
        execute(script_.code[here].command, here);
        if (resumePc_ != here) resumeRandom_ = random_; // (a command that waits is done again, from before it)
        if (watchThere_ && wait_ == Wait::None && std::holds_alternative<cmd::Screen>(script_.code[here].command) && (*watchThere_)(here)) {
            wait_ = Wait::Reached; // (playTo(): a screen shown or hidden there is what that line shows)
            return;
        }
    }
}

void Game::goOn() {
    if (wait_ != Wait::Stopped) return;
    wait_ = Wait::None;
    run();
}

void Game::waitFor(Wait wait, std::size_t here) {
    resumePc_ = here; // a save made now repeats this command
    wait_ = wait;
}

Value Game::variable(std::string_view name) const {
    if (const auto it = vars_.find(name); it != vars_.end()) return it->second;
    if (const auto it = ctx_.persistent.find(name); it != ctx_.persistent.end()) return it->second;
    if (const auto it = script_.startValues.find(name); it != script_.startValues.end()) return it->second;
    if (ctx_.builtin)
        if (auto v = ctx_.builtin(name)) return *v;
    return 0;
}

void Game::setVariable(const std::string& name, Value value) {
    if (name.starts_with("persistent.")) {
        ctx_.persistent.insert_or_assign(name, std::move(value));
        persistentChanged_ = true;
    } else {
        vars_.insert_or_assign(name, std::move(value));
    }
}

RichText richText(std::string_view source, const Lookup& lookup, const Palette* palette, const TagHandler& other) {
    RichText out;
    std::vector<std::pair<std::size_t, SDL_Color>> colors;              // open {color} tags
    std::map<std::string, std::vector<std::size_t>, std::less<>> styles; // open {bold} {italic} {underline} {strike}
    std::vector<std::pair<std::size_t, std::string>> rubies;             // open {ruby READING} tags
    std::vector<std::size_t> tips;                                       // open {tip NAME} tags
    // A tip's words: underlined, in the colour called "tip" ("color tip ...", in the built-in menus).
    auto tip = [&](std::size_t begin, std::size_t end) {
        out.spans.push_back({.begin = begin, .end = end, .color = parseColor("tip", palette).value_or(SDL_Color{126, 200, 255, 255}),
                             .style = TTF_STYLE_UNDERLINE});
    };
    static const std::map<std::string, TTF_FontStyleFlags, std::less<>> kStyles{
        {"bold", TTF_STYLE_BOLD}, {"italic", TTF_STYLE_ITALIC}, {"underline", TTF_STYLE_UNDERLINE}, {"strike", TTF_STYLE_STRIKETHROUGH}};
    out.text = expand(source, lookup, [&](std::string_view tag, std::string_view arg, std::size_t at) {
        if (tag == "color") {
            colors.emplace_back(at, parseColor(arg, palette).value_or(SDL_Color{255, 255, 255, 255}));
        } else if (tag == "/color") {
            if (colors.empty()) return;
            out.spans.push_back({.begin = colors.back().first, .end = at, .color = colors.back().second});
            colors.pop_back();
        } else if (kStyles.contains(tag)) {
            styles[std::string(tag)].push_back(at);
        } else if (tag.starts_with('/') && kStyles.contains(tag.substr(1))) {
            auto& open = styles[std::string(tag.substr(1))];
            if (open.empty()) return;
            out.spans.push_back({.begin = open.back(), .end = at, .style = kStyles.at(std::string(tag.substr(1)))});
            open.pop_back();
        } else if (tag == "ruby") {
            rubies.emplace_back(at, std::string(arg));
        } else if (tag == "/ruby") {
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
    for (const auto& [start, col] : colors) out.spans.push_back({.begin = start, .end = length, .color = col});
    for (const auto& [tag, starts] : styles)
        for (auto start : starts) out.spans.push_back({.begin = start, .end = length, .style = kStyles.at(tag)});
    for (auto start : tips) tip(start, length);
    return out;
}

// Starts a new line: fills in {expressions} and reads its text tags.
void Game::say(std::string speaker, SDL_Color color, std::string_view text, const CharacterDef* def) {
    speaker_ = std::move(speaker);
    speakerKey_ = def ? def->key : std::string{};
    speakerColor_ = color;
    voiced_ = syncVoice_ = typing_ = false;
    lineTime_ = 0.0f;
    textColor_ = def ? def->textColor : std::nullopt;
    textbox_ = boxOf(def);
    source_ = fillValues(text, lookup_);
    blip_ = def ? def->blip : std::string{};
    revealMode_ = def && def->reveal ? *def->reveal : config().reveal;
    face_ = faceBase_ = faceSide_ = {};
    if (def && !def->face.empty()) {
        faceSide_ = def->faceSide;
        const auto it = expressions_.find(def->key);
        faceBase_ = def->face;
        face_ = def->faceOf(it != expressions_.end() ? it->second : std::string{});
    }
    stops_.clear();
    speeds_.clear();
    noWait_ = false;
    RichText rich = richText(text, lookup_, &script_.palette, [&](std::string_view tag, std::string_view arg, std::size_t at) {
        const auto n = parseNumber(arg);
        if (tag == "pause") stops_.push_back({at, n ? std::max(0.0f, *n) : -1.0f});
        else if (tag == "nowait") noWait_ = true;
        else if (tag == "speed") speeds_.push_back({at, n ? std::max(0, static_cast<int>(*n)) : -1});
        else if (tag == "/speed") speeds_.push_back({at, -1});
        else if (tag == "reveal") revealMode_ = arg == "line" ? Reveal::Line : arg == "word" ? Reveal::Word : Reveal::Letter;
    });
    text_ = std::move(rich.text);
    spans_ = std::move(rich.spans);
    length_ = utf8::length(text_);
    reveal_ = autoTimer_ = holdTimer_ = 0.0f;
    passedStops_ = 0;
}

const Game::Stop* Game::pendingStop() const {
    return passedStops_ < stops_.size() ? &stops_[passedStops_] : nullptr;
}

std::string Game::boxOf(const CharacterDef* def) const {
    return def && !def->textbox.empty() ? def->textbox : !textboxScreen_.empty() ? textboxScreen_ : "say";
}

bool Game::keeps(std::string_view box) const {
    const ScreenDef* screen = script_.screen(box);
    return screen && screen->keep;
}

float Game::speedOf(std::string_view box) const {
    const ScreenDef* screen = script_.screen(box);
    return screen ? screen->textSpeed : 1.0f;
}

bool Game::othersTyping() const {
    return std::ranges::any_of(others_, [](const BoxLine& l) { return l.reveal < static_cast<float>(l.length); });
}

Game::BoxLine Game::boxLine(std::string box, std::string speaker, SDL_Color color, std::string source, const CharacterDef* def) const {
    RichText rich = richText(source, lookup_, &script_.palette);
    BoxLine l{.box = std::move(box), .speaker = std::move(speaker), .speakerKey = def ? def->key : std::string{}, .source = std::move(source),
              .text = std::move(rich.text), .speakerColor = color, .spans = std::move(rich.spans)};
    l.keep = keeps(l.box);
    l.length = utf8::length(l.text);
    if (def && !def->face.empty()) {
        const auto it = expressions_.find(def->key);
        l.face = def->faceOf(it != expressions_.end() ? it->second : std::string{});
    }
    return l;
}

// A line of a together block, before its last: it shows now in its own textbox (typing at that
// textbox's speed, without a voice), and the story goes on to the next line.
void Game::sayTogether(const cmd::Say& c, const CharacterDef* def) {
    BoxLine l = boxLine(boxOf(def), def ? def->name : c.speaker, def ? def->color : config().narratorColor, fillValues(c.text, lookup_), def);
    if (skipping() || ctx_.settings.textSpeed == 0) l.reveal = static_cast<float>(l.length);
    std::erase_if(others_, [&](const BoxLine& other) { return other.box == l.box; });
    together_ = true;
    learnTips(c.text);
    ctx_.read.insert(c.id);
    addHistory({.speaker = l.speaker, .color = l.speakerColor, .text = l.text, .spans = l.spans});
    others_.push_back(std::move(l));
}

std::optional<Game::LineView> Game::line(std::string_view box) const {
    if (showingSay() && box == textbox_)
        return LineView{.speaker = speaker_, .text = text_, .face = face_, .faceBase = faceBase_, .faceSide = faceSide_, .speakerColor = speakerColor_,
                        .textColor = textColor_, .spans = &spans_, .revealed = revealed(), .limit = revealLimit(), .mode = revealMode_,
                        .complete = textComplete(), .atPause = atPause(), .character = speakerCharacter(), .faceFrame = faceFrame()};
    const auto it = std::ranges::find(others_, box, &BoxLine::box);
    if (it == others_.end()) return std::nullopt;
    const CharacterDef* def = script_.character(it->speakerKey);
    const bool typing = it->reveal < static_cast<float>(it->length);
    Frame face;
    if (def) {
        face.mouth = typing ? mouth(def->faceTalk, flapping(it->speakerKey + " face", clock_, def->talkSpeed), false) : 0.0f; // (said together: no voice)
        if (def->faceBlink) face.eyes = blinking(it->speakerKey, clock_, def->blinkEvery);
    }
    return LineView{.speaker = it->speaker, .text = it->text, .face = it->face, .faceBase = def ? std::string_view(def->face) : std::string_view{},
                    .faceSide = def && !def->face.empty() ? std::string_view(def->faceSide) : std::string_view{}, .speakerColor = it->speakerColor,
                    .textColor = def ? def->textColor : std::nullopt, .spans = &it->spans, .revealed = static_cast<std::size_t>(it->reveal),
                    .limit = it->length, .mode = def && def->reveal ? *def->reveal : config().reveal, .complete = !typing, .character = def, .faceFrame = face};
}

std::vector<std::string> Game::boxes() const {
    std::vector<std::string> out;
    for (const BoxLine& l : others_)
        if (!(showingSay() && l.box == textbox_)) out.push_back(l.box);
    if (showingSay()) out.push_back(textbox_);
    return out;
}

int Game::currentSpeed() const {
    int cps = -1;
    for (const auto& s : speeds_)
        if (s.at <= static_cast<std::size_t>(reveal_)) cps = s.cps;
    return cps < 0 ? static_cast<int>(std::lround(static_cast<float>(ctx_.settings.textSpeed) * speedOf(textbox_))) : cps;
}

std::size_t Game::revealLimit() const {
    const Stop* stop = pendingStop();
    return stop ? stop->at : length_;
}

bool Game::textComplete() const {
    if (wait_ == Wait::Menu) return menu()->optional; // a click goes on
    if (wait_ != Wait::Say) return false;
    if (const Stop* stop = pendingStop(); stop && stop->seconds < 0 && reveal_ >= static_cast<float>(stop->at)) return true;
    return reveal_ >= static_cast<float>(length_) && !pendingStop() && !othersTyping();
}

void Game::revealTo(std::size_t position) {
    const auto before = static_cast<std::size_t>(reveal_);
    reveal_ = static_cast<float>(std::min(position, length_));
    if (!blip_.empty() && ctx_.settings.typingSounds && !voiced_ && static_cast<std::size_t>(reveal_) > before && blipTimer_ <= 0.0f && !skipping()) {
        ctx_.audio.playSound(ctx_.assets.audio(blip_), config().blipVolume);
        blipTimer_ = config().blipEvery;
    }
}

void Game::execute(const Command& command, std::size_t here) {
    const RandomScope _(random_); // (its random() numbers: from its own sequence)
    const bool fast = skipping();
    const bool instant = fast || !ctx_.settings.animations; // (an animation played: at its end at once; effects and weather: there at once)

    std::visit(
        Overloaded{
            [&](const cmd::Say& c) { sayLine(c, here, fast); },
            [&](const cmd::Scene& c) { // a new background (what was on stage goes with the one before), or the one there played on
                have(RecordKind::Gallery, c.image);
                if (c.image != stage_.background()) stage_.clearObjects();
                stage_.setBackground(c.image, animation(c.with.value_or("")), instant);
                if (c.animation) stage_.animateBackground(animation(*c.animation), true, fast);
                if (stage_.backgroundComing()) wait_ = Wait::Background; // (the story goes on once it's in)
            },
            [&](const cmd::Show& c) { // (one that comes plays the game's show_animation, unless the line says)
                if (!c.image.empty()) have(RecordKind::Gallery, c.image);
                const CharacterDef* def = c.character.empty() ? nullptr : script_.character(c.character);
                const std::string played = c.with ? *c.with : stage_.has(c.tag) ? std::string{} : config().showAnimation;
                stage_.show(c.tag, def, c.image, c.expression, c.props, animation(played), fast ? 0.0f : config().moveTime, instant);
                if (c.animation) stage_.animate(c.tag, animation(*c.animation), true, fast);
                if (def && def->faceSync.expression) // their face looks as their picture does (a new one starts as it is)
                    expressions_.insert_or_assign(def->key, stage_.expression(c.tag));
                if (c.wait && !fast && stage_.moving()) wait_ = Wait::Move;
            },
            [&](const cmd::Hide& c) { // (it plays the game's hide_animation, unless the line says; then it's gone)
                stage_.hide(c.tag, animation(c.with.value_or(config().hideAnimation)), instant);
                if (c.wait && !fast && stage_.moving()) wait_ = Wait::Move;
            },
            [&](const cmd::Phone& c) { phoneOn_ = c.on; },
            [&](const cmd::Effect& c) {
                if (fast && c.seconds > 0.0f) return; // skipping goes past a moment's effect
                stage_.setEffect(c, instant);
            },
            [&](const cmd::Menu& c) { openMenu(c, here); },
            [&](const cmd::Jump& c) {
                if (c.unless && c.unless->test(lookup_)) return;
                if (c.call) {
                    if (callStack_.size() >= 256) return fail("Too many calls inside calls (does a label call itself?)");
                    callStack_.push_back(pc_);
                }
                pc_ = c.target;
            },
            [&](const cmd::Return&) {
                if (callStack_.empty()) return fail("'return' was reached, but nothing called this label (use jump or end instead?)");
                pc_ = callStack_.back();
                callStack_.pop_back();
                if (pass_ && callStack_.size() < pass_->depth) hiddenLines_ = std::exchange(pass_, std::nullopt)->lines; // a choice's return=no
            },
            [&](const cmd::End&) { wait_ = Wait::Finished; },
            [&](const cmd::Set& c) {
                const Value old = variable(c.variable);
                if (c.op == '!') return setVariable(c.variable, truthy(old) ? 0 : 1);
                const Value v = c.value ? c.value->eval(lookup_) : Value{0};
                setVariable(c.variable, c.op == '=' ? v : combine(old, c.op, v));
            },
            [&](const cmd::Play& c) { play(c, fast); },
            [&](const cmd::Wait& c) {
                if (c.seconds < 0.0f) {
                    autoTimer_ = 0.0f;
                    return waitFor(Wait::Pause, here);
                }
                if (fast || c.seconds <= 0.0f) return;
                timer_ = c.seconds;
                waitFor(Wait::Timer, here);
            },
            [&](const cmd::Screen& c) {
                if (!c.show) std::erase(screens_, c.name);
                else if (!std::ranges::contains(screens_, c.name)) screens_.push_back(c.name);
            },
            [&](const cmd::Ask& c) {
                if (!c.fallback.empty() || !std::holds_alternative<std::string>(variable(c.variable)))
                    setVariable(c.variable, c.fallback);
                waitForScreen({.name = "ask", .variable = c.variable, .prompt = expand(c.prompt, lookup_), .max = c.max}, here);
            },
            [&](const cmd::Ui& c) { // open SCREEN: the story waits until the player closes it (unless it's modal=no)
                const ScreenDef* screen = c.verb == "open" ? script_.screen(c.arg) : nullptr;
                if (!screen || !screen->modal) return requests_.push_back(c);
                vars_.insert_or_assign("answer", std::string{}); // until it answers
                waitForScreen({.name = c.arg, .opening = c.opening, .closing = c.closing}, here);
            },
            [&](const cmd::Chapter& c) { // a new part of the story: its title card, if the screens have one
                chapter_ = fillValues(c.title, lookup_);
                passPlace(here, Chart::Kind::Chapter);
                if (!fast && script_.screen("chapter")) waitForScreen({.name = "chapter"}, here);
            },
            [&](const cmd::Mark&) { passPlace(here, Chart::Kind::Point); },
            [&](const cmd::Record& c) { // a picture unlocked, an ending reached, an achievement earned, a tip learned
                if (c.kind == RecordKind::Ending && script_.record(c.kind, c.name)) passPlace(here, Chart::Kind::Ending);
                collect(c.kind, c.name);
            },
            [&](const cmd::Movie& c) { playMovie(c, here, fast); },
            [&](const cmd::Message& c) { sendMessage(c, here); }, // on the phone; a new one from someone closes the answers to their last
            [&](const cmd::Weather& c) { stage_.setWeather(c, instant ? 0.0f : c.fade); },
            [&](const cmd::Camera& c) {
                if (c.animation) stage_.animateCamera(animation(*c.animation), true, fast);
                if (c.with) stage_.animateCamera(animation(*c.with), false, instant);
                if (c.wait && !fast && stage_.moving()) wait_ = Wait::Move;
            },
            [&](const cmd::Textbox& c) {
                if (c.hide) return void(std::erase_if(others_, [&](const BoxLine& l) { return c.screen.empty() || l.box == c.screen; }));
                if (!c.newPage) textboxScreen_ = c.screen == "say" ? std::string{} : c.screen;
                page_.clear(); // a new textbox starts on an empty page
            },
        },
        command);
}

// A line said (or narrated): its speaker's face and textbox (the other textboxes' lines go, or stay), its
// voice (a voice line just before it, their voice file, or made-up speech), the history, and the story
// waiting for a click.
void Game::sayLine(const cmd::Say& c, std::size_t here, bool fast) {
    const CharacterDef* def = c.character.empty() ? nullptr : script_.character(c.character);
    if (def && !c.expression.empty()) { // their face, and their picture on stage if the face copies it
        expressions_.insert_or_assign(def->key, c.expression);
        if (def->faceSync.expression || def->face.empty()) stage_.setExpression(def->key, c.expression);
    }
    if (hiddenLines_ > 0) { // gone past unseen (a choice's return=no)
        --hiddenLines_;
        pendingVoice_.reset();
        return;
    }
    if (trigger_ && trigger_->lines-- <= 0) { // a choice up for some lines: its time is over
        trigger_.reset();
        choiceItems_.clear();
    }
    // Lines in other textboxes: ones said with the last line go (unless their textbox keeps
    // them), and the line on screen stays as one if its textbox does.
    if (!together_) std::erase_if(others_, [](const BoxLine& l) { return !l.keep; });
    if (c.together) return sayTogether(c, def);
    together_ = false;
    const std::string box = boxOf(def);
    std::erase_if(others_, [&](const BoxLine& l) { return l.box == box; });
    if (!text_.empty() && textbox_ != box && keeps(textbox_)) {
        BoxLine kept{.box = textbox_, .speaker = speaker_, .speakerKey = speakerKey_, .source = source_, .text = text_, .face = face_,
                     .speakerColor = speakerColor_, .spans = spans_, .reveal = static_cast<float>(length_), .length = length_, .keep = true};
        std::erase_if(others_, [&](const BoxLine& l) { return l.box == kept.box; });
        others_.push_back(std::move(kept));
    }
    lineVoice_ = std::exchange(pendingVoice_, std::nullopt).value_or(LineVoice{}); // a voice line just before it
    bool voiced = !lineVoice_.file.empty();
    const std::optional<bool> sync = lineVoice_.sync;
    if (!voiced) ctx_.audio.stopVoice();
    say(def ? def->name : c.speaker, def ? def->color : config().narratorColor, c.text, def);
    // Its voice (kept in the history to hear again): the voice line, else their own voice file
    // for this line (voices=), else made-up speech (babble=).
    HistoryEntry entry{.speaker = speaker_, .color = speakerColor_, .text = text_, .spans = spans_};
    if (voiced) entry.voice = ctx_.assets.audio(lineVoice_.file), entry.voiceVolume = lineVoice_.volume;
    else if (def && !def->voices.empty())
        if (const auto file = ctx_.assets.findAudio(std::format("{}/{}", def->voices, c.voiceId))) entry.voice = *file;
    if (!voiced && entry.voice.empty() && def && def->babble > 0.0f && ctx_.settings.babble) entry.babble = def->babble, entry.babbleSpeed = def->babbleSpeed;
    if (!voiced && !fast) {
        if (!entry.voice.empty()) ctx_.audio.playVoice(entry.voice), voiced = true;
        else if (entry.babble > 0.0f) voiced = ctx_.audio.playBabble(text_, entry.babble, entry.babbleSpeed);
    }
    voiced_ = voiced;
    syncVoice_ = voiced && sync.value_or(config().voiceSync);
    menuPrompt_ = false;
    lineRead_ = !ctx_.read.insert(c.id).second;
    addToPage();
    addHistory(std::move(entry));
    learnTips(c.text);
    waitFor(Wait::Say, here);
    remember();
}

// A choice: its options (none of them pickable: the story goes on), up while the story goes on for some
// lines, or waited on, its prompt said; and a place on the flowchart.
void Game::openMenu(const cmd::Menu& c, std::size_t here) {
    trigger_.reset();
    listChoices(c);
    if (std::ranges::all_of(choiceItems_, &ChoiceItem::locked)) {
        logWarn("{}: None of the options can be picked right now, so the story goes on.", script_.where(script_.code[here]));
        choiceItems_.clear();
        return;
    }
    learnTips(c.prompt);
    for (const ChoiceItem& item : choiceItems_) learnTips(item.text);
    std::vector<Chart::Option> options; // as the flowchart shows it: picked or not, locked or not
    options.reserve(choiceItems_.size());
    for (const ChoiceItem& item : choiceItems_) options.push_back({item.option, !item.locked});
    if (c.lines > 0) { // up while the story goes on, for its next lines
        trigger_ = Trigger{.at = here, .lines = c.lines};
        pc_ = c.after;
        return passPlace(here, Chart::Kind::Choice, std::move(options));
    }
    menuTime_ = c.seconds;
    menuPrompt_ = !c.prompt.empty() || c.optional; // an optional one keeps the line before it
    if (!c.prompt.empty()) {
        say("", speakerColor_, c.prompt, nullptr);
        reveal_ = static_cast<float>(length_);
        passedStops_ = stops_.size();
        addToPage();
    }
    if (!ctx_.settings.skipAfterChoices) skip_ = false;
    autosave_ = true;
    waitFor(Wait::Menu, here);
    remember();
    passPlace(here, Chart::Kind::Choice, std::move(options));
}

// Music, a voice or a sound played, or stopped (a voice keeps playing through the next line; a sound on a
// channel of its own may loop).
void Game::play(const cmd::Play& c, bool fast) {
    const std::string path = c.file.empty() ? std::string{} : ctx_.assets.audio(c.file);
    switch (c.track) {
    case cmd::Play::Track::Music:
        music_ = c.file;
        musicVolume_ = c.volume;
        have(RecordKind::Song, c.file); // the music room
        if (c.file.empty()) ctx_.audio.stopMusic(c.fade);
        else ctx_.audio.playMusic(path, c.loop, c.fade, c.volume);
        break;
    case cmd::Play::Track::Voice:
        if (c.file.empty()) return ctx_.audio.stopVoice();
        if (!fast) ctx_.audio.playVoice(path, c.volume);
        pendingVoice_ = LineVoice{c.file, c.volume, c.sync}; // keep playing through the next line
        break;
    case cmd::Play::Track::Sound:
        if (c.file.empty()) { // stop sound [NAME]
            ctx_.audio.stopChannel(c.channel, c.fade);
            if (c.channel.empty()) loops_.clear();
            else loops_.erase(c.channel);
        } else if (c.channel.empty()) {
            if (!fast) ctx_.audio.playSound(path, c.volume);
        } else {
            ctx_.audio.playChannel(c.channel, path, c.loop, c.volume, c.fade);
            if (c.loop) loops_.insert_or_assign(c.channel, Loop{c.file, c.volume});
            else loops_.erase(c.channel);
        }
        break;
    }
}

// A video, full screen: past it when skipping (if it can be skipped); one that can't play is said, and in
// developer mode a card with its name shows for a moment instead.
void Game::playMovie(const cmd::Movie& c, std::size_t here, bool fast) {
    have(RecordKind::Gallery, c.file); // a video in the gallery
    const bool skippable = c.skippable && ctx_.settings.skipMovies; // (the player can say no too)
    const bool stopsHere = watchThere_ && (*watchThere_)(here); // (playTo() stops at one there: its first picture)
    if ((fast && skippable) || (watch_ != SIZE_MAX && !stopsHere)) return; // skipping goes past videos, and so does playTo()
    const auto file = ctx_.assets.findMovie(c.file);
    movie_ = file ? std::make_unique<Movie>(ctx_.renderer, ctx_.audio, *file, c.loop, c.volume) : nullptr;
    if (!movie_ || !*movie_) {
        movie_.reset();
        logWarn("{}: {}", script_.where(script_.code[here]),
                file ? std::format("Can't play '{}'.", *file) : std::format("There's no video called '{}'.", c.file));
        if (!config().developer) return; // players: the story goes on
        movieStandIn_ = c.file;    // a labelled card for a moment instead
        timer_ = 3.0f;
    }
    movieSkippable_ = skippable;
    ctx_.audio.stopVoice();
    waitFor(Wait::Movie, here);
}

// A message on the phone: a new one from someone closes the answers to their last, and one answering back
// is typed first, about as long as typing it takes.
void Game::sendMessage(const cmd::Message& c, std::size_t here) {
    const CharacterDef* def = c.character.empty() ? nullptr : script_.character(c.character);
    const std::string who = def ? def->name : c.sender;
    if (!c.mine)
        for (MessageItem& m : messages_)
            if (m.from == who) m.replies.reset();
    MessageItem m{.from = c.mine ? std::string{} : who, .with = who, .color = def ? def->color : config().narratorColor,
                  .text = fillValues(c.text, lookup_), .read = c.mine, .replies = c.replies.empty() ? std::nullopt : std::optional(here)};
    if (answering_ && !c.mine && config().phoneTyping > 0.0f) { // an answer back: typed first, about as long as typing it takes, after their others
        for (const MessageItem& other : messages_)
            if (other.with == who) m.typing = std::max(m.typing, other.typing);
        m.typing += 0.8f + static_cast<float>(utf8::length(m.text)) / config().phoneTyping * static_cast<float>(randomNumber(75, 125)) / 100.0f;
    }
    messages_.push_back(std::move(m));
    if (messages_.back().typing <= 0.0f) arrive(messages_.back());
}

// The language changed: the line, choices or question on screen again in the new one (the line
// types out again unless it was all there; its voice goes on).
void Game::retranslate() {
    if (wait_ == Wait::Screen && !request_.variable.empty()) { // ask
        if (const auto* c = std::get_if<cmd::Ask>(&script_.code[resumePc_].command)) request_.prompt = expand(c->prompt, lookup_);
        return;
    }
    if (wait_ != Wait::Say && wait_ != Wait::Menu) return;
    const Command& command = script_.code[resumePc_].command;
    auto again = [&](const std::string& speaker, SDL_Color color, const std::string& text, const CharacterDef* def, bool whole) {
        const bool voiced = voiced_, sync = syncVoice_;
        const float time = lineTime_;
        say(speaker, color, text, def);
        voiced_ = voiced;
        syncVoice_ = sync;
        lineTime_ = time;
        if (whole) {
            reveal_ = static_cast<float>(length_);
            passedStops_ = stops_.size();
        }
        if (!page_.empty()) page_.back() = {speaker_, speakerColor_, text_, spans_, textColor_};
    };
    if (const auto* c = std::get_if<cmd::Say>(&command)) {
        const CharacterDef* def = c->character.empty() ? nullptr : script_.character(c->character);
        again(def ? def->name : c->speaker, def ? def->color : config().narratorColor, c->text, def, textComplete() && !pendingStop());
        if (!history_.empty()) {
            HistoryEntry& h = history_.back();
            h.speaker = speaker_;
            h.color = speakerColor_;
            h.text = text_;
            h.spans = spans_;
        }
    } else if (const auto* m = std::get_if<cmd::Menu>(&command)) {
        listChoices(*m);
        if (!m->prompt.empty()) again("", speakerColor_, m->prompt, nullptr, true);
    }
}

void Game::addToPage() {
    page_.push_back({speaker_, speakerColor_, text_, spans_, textColor_});
    if (page_.size() > 60) page_.erase(page_.begin());
}

std::shared_ptr<const Animation> Game::animation(std::string_view name) const {
    if (name.empty()) return nullptr;
    const auto it = script_.animations.find(name);
    return it != script_.animations.end() ? std::make_shared<const Animation>(it->second) : nullptr;
}

void Game::perform(const Command& command) {
    if (const auto* jump = std::get_if<cmd::Jump>(&command)) {
        const auto pc = script_.findLabel(jump->label);
        if (!pc) return;
        if (jump->call) callStack_.push_back(wait_ == Wait::None || wait_ == Wait::Finished ? pc_ : resumePc_);
        choiceItems_.clear();
        menuPrompt_ = false;
        pc_ = *pc;
        wait_ = Wait::None;
        return run();
    }
    execute(command, current_);
}

void Game::advance() {
    switch (wait_) {
    case Wait::Movie:
        if (movieSkippable_) endMovie(); // clicked past
        return;
    case Wait::Menu:
        if (menu()->optional) letPass(); // clicked on
        return;
    case Wait::Say: {
        if (const Stop* stop = pendingStop(); stop && reveal_ >= static_cast<float>(stop->at)) {
            ++passedStops_; // go on after a {pause}
            holdTimer_ = 0.0f;
            return;
        }
        const bool others = othersTyping(); // lines said together with it: all there at once
        for (BoxLine& l : others_) l.reveal = static_cast<float>(l.length);
        if (reveal_ < static_cast<float>(length_)) {
            // Show everything up to the next click pause (timed pauses are skipped).
            while (const Stop* stop = pendingStop()) {
                if (stop->seconds < 0) {
                    revealTo(stop->at);
                    return;
                }
                ++passedStops_;
            }
            revealTo(length_);
            return;
        }
        if (others) return;
        break;
    }
    case Wait::Background:
    case Wait::Move:
        stage_.finish();
        break;
    case Wait::Pause:
    case Wait::Timer:
    case Wait::Reached: // (the preview: on from where it stopped)
        break;
    default:
        return;
    }
    wait_ = Wait::None;
    run();
}

bool Game::playTo(std::size_t target, const std::function<bool(std::size_t)>& there, bool* skipped) {
    watch_ = target;
    watched_ = std::ranges::contains(ran_, target); // (start() runs on to the first wait: it may have run it already)
    watchThere_ = &there;
    const ScopeExit _([this] { // (however this ends)
        watch_ = SIZE_MAX;
        watchThere_ = nullptr;
    });
    const Uint64 until = SDL_GetTicks() + 1000; // (a story that goes round in a circle never gets there)
    for (int step = 0; step < 20000 && SDL_GetTicks() < until; ++step) {
        switch (wait_) {
        case Wait::Finished:
            return false;
        case Wait::Reached:
            return true;
        case Wait::Say:
        case Wait::Menu:
            if (!watched_ && target < script_.code.size() && resumePc_ > target) { // gone past it: on from it
                if (skipped) *skipped = true;
                choiceItems_.clear();
                trigger_.reset();
                menuPrompt_ = false;
                pc_ = target;
                wait_ = Wait::None;
                run();
                break;
            }
            if (there(resumePc_)) {
                for (BoxLine& l : others_) l.reveal = static_cast<float>(l.length);
                passedStops_ = stops_.size();
                revealTo(length_);
                stage_.finish();
                return true;
            }
            if (wait_ == Wait::Say) {
                advance();
            } else { // the option that starts last before `target`, else the first
                const cmd::Menu* m = menu();
                std::optional<std::size_t> pick, first;
                auto start = [&](std::size_t i) { return m->choices[static_cast<std::size_t>(choiceItems_[i].option)].target; };
                for (std::size_t i = 0; i < choiceItems_.size(); ++i) {
                    if (choiceItems_[i].locked) continue;
                    if (!first) first = i;
                    if (start(i) <= target && (!pick || start(i) > start(*pick))) pick = i;
                }
                if (!pick) pick = first;
                if (!pick) return false;
                choose(*pick);
            }
            break;
        case Wait::Screen:
        case Wait::Movie:
            if (!watched_ && target < script_.code.size() && resumePc_ > target) { // gone past it: on from it
                if (skipped) *skipped = true;
                if (wait_ == Wait::Movie) movie_.reset();
                request_ = {};
                pc_ = target;
                wait_ = Wait::None;
                run();
                break;
            }
            if (there(resumePc_)) { // (what the player sees at that line: a video's first picture)
                stage_.finish();
                if (movie_) movie_->showFirst();
                return true;
            }
            if (wait_ == Wait::Screen) screenClosed(std::nullopt);
            else endMovie();
            break;
        case Wait::None:
            run();
            break;
        default: // an animation, a pause or a timer: over at once
            advance();
            break;
        }
    }
    return false;
}

void Game::playMovieRound(float dt) {
    if (!movie_) return;
    const bool loop = movie_->looping();
    movie_->setLoop(true);
    movie_->update(dt);
    movie_->setLoop(loop);
}

void Game::endMovie() {
    movie_.reset();
    movieStandIn_.clear();
    wait_ = Wait::None;
    run();
}

void Game::setCovered(bool covered) {
    ctx_.audio.pauseVoice(covered);
    if (movie_) movie_->setPaused(covered);
}

Variables Game::savedVariables(std::string_view state) {
    Variables vars;
    std::istringstream in{std::string(state)};
    for (std::string line; std::getline(in, line);)
        if (line.starts_with("var ")) {
            std::istringstream ls(line.substr(4));
            std::string name, value;
            ls >> name;
            std::getline(ls >> std::ws, value);
            vars.insert_or_assign(name, decodeValue(value));
        }
    return vars;
}

// The player is at a place on the flowchart (a chapter, a choice, an ending or a "chart" point): it
// remembers the game here, and which way the player came.
void Game::passPlace(std::size_t here, Chart::Kind kind, std::vector<Chart::Option> options) {
    const auto [label, offset] = script_.locate(here);
    const std::string id = std::format("{}:{}", label.empty() ? "-" : label, offset);
    const std::size_t resume = std::exchange(resumePc_, here); // the game here repeats this command
    std::string state = save();
    resumePc_ = resume;
    ctx_.chart.visit(id, kind, chartNode_, chartOption_, std::move(state), std::move(options));
    chartNode_ = id;
    chartOption_ = -1;
}

// Options whose condition is false are left out, or shown locked with (needs ...).
std::optional<ChoiceItem> Game::offer(const cmd::Choice& choice, int option) const {
    const bool open = !choice.condition || choice.condition->test(lookup_);
    if (!open && !choice.shownLocked) return std::nullopt;
    return ChoiceItem{.text = fillValues(choice.text, lookup_), // text tags stay for the screen
                      .locked = !open,
                      .note = open ? std::string() : fillValues(choice.note, lookup_),
                      .seen = ctx_.read.contains(choice.id),
                      .option = option};
}

void Game::listChoices(const cmd::Menu& menu) {
    choiceItems_.clear();
    for (std::size_t k = 0; k < menu.choices.size(); ++k)
        if (auto item = offer(menu.choices[k], static_cast<int>(k))) choiceItems_.push_back(std::move(*item));
}

const cmd::Menu* Game::menu() const {
    const std::optional<std::size_t> at = wait_ == Wait::Menu ? std::optional(resumePc_) : trigger_ ? std::optional(trigger_->at) : std::nullopt;
    return at ? std::get_if<cmd::Menu>(&script_.code[*at].command) : nullptr;
}

std::string Game::choiceScreen() const {
    const cmd::Menu* m = menu();
    return m && !m->screen.empty() ? m->screen : "choice";
}

std::vector<ChoiceItem> Game::replies(std::size_t message) const {
    std::vector<ChoiceItem> out;
    const auto* m = message < messages_.size() && messages_[message].replies && messages_[message].typing <= 0.0f
                        ? std::get_if<cmd::Message>(&script_.code[*messages_[message].replies].command) : nullptr;
    for (std::size_t k = 0; m && k < m->replies.size(); ++k)
        if (auto item = offer(m->replies[k], static_cast<int>(k))) out.push_back(std::move(*item));
    return out;
}

void Game::readMessages(std::string_view with) {
    for (MessageItem& m : messages_)
        if (with.empty() || m.with == with) m.read = m.read || m.typing <= 0.0f;
}

std::vector<Game::Contact> Game::contacts() const {
    std::vector<Contact> out;
    for (std::size_t i = 0; i < messages_.size(); ++i) {
        const MessageItem& m = messages_[i];
        auto it = std::ranges::find(out, m.with, &Contact::name);
        if (it == out.end()) it = out.insert(out.end(), {.name = m.with, .color = m.color});
        it->last = i;
        it->typing = it->typing || m.typing > 0.0f;
        if (!m.read && m.typing <= 0.0f) ++it->unread;
    }
    std::ranges::sort(out, std::greater{}, &Contact::last);
    return out;
}

void Game::updatePhone(float dt) {
    for (MessageItem& m : messages_)
        if (m.typing > 0.0f && (m.typing -= dt) <= 0.0f) {
            m.typing = 0.0f;
            arrive(m);
        }
}

void Game::arrive(const MessageItem& m) {
    learnTips(m.text);
    if (m.replies)
        if (const auto* c = std::get_if<cmd::Message>(&script_.code[*m.replies].command))
            for (const cmd::Choice& answer : c->replies) learnTips(answer.text);
}

bool Game::reply(std::size_t message, std::size_t option) {
    const auto* m = message < messages_.size() && messages_[message].replies && messages_[message].typing <= 0.0f
                        ? std::get_if<cmd::Message>(&script_.code[*messages_[message].replies].command) : nullptr;
    if (!m || option >= m->replies.size()) return false;
    const cmd::Choice& c = m->replies[option];
    if (c.condition && !c.condition->test(lookup_)) return false;
    ctx_.read.insert(c.id);
    messages_[message].replies.reset();
    messages_.push_back({.with = messages_[message].with, .color = messages_[message].color, .text = fillValues(c.text, lookup_), .read = true});
    if (!c.label.empty()) { // its lines, then a label: the story goes there
        choiceItems_.clear();
        menuPrompt_ = false;
        pc_ = c.target;
        wait_ = Wait::None;
        answering_ = true;
        run();
        answering_ = false;
        return true;
    }
    // Its lines run now, as if called from where the story is. Lines that show nothing (set, message,
    // sound...) leave it there; ones that do play, and the story goes on after the line it was on.
    const Wait was = wait_;
    const std::size_t pc = pc_, resume = resumePc_, current = current_;
    callStack_.push_back(was == Wait::Say ? pc_ : resumePc_);
    const std::size_t depth = callStack_.size();
    pc_ = c.target;
    wait_ = Wait::None;
    answering_ = true;
    for (int budget = kInstructionBudget; wait_ == Wait::None && callStack_.size() >= depth;) {
        if (pc_ >= script_.code.size()) wait_ = Wait::Finished;
        else if (--budget == 0) fail("An answer's lines ran a very long time without showing anything (does a jump go round in a circle?)");
        else {
            const std::size_t here = current_ = pc_++;
            resumePc_ = pc_;
            execute(script_.code[here].command, here);
        }
    }
    answering_ = false;
    if (wait_ != Wait::None) return true;
    wait_ = was;
    pc_ = pc;
    resumePc_ = resume;
    current_ = current;
    return false;
}

void Game::letPass() {
    pc_ = menu()->after;
    choiceItems_.clear();
    menuPrompt_ = false;
    wait_ = Wait::None;
    run();
}

std::optional<float> Game::timeLeft() const {
    const cmd::Menu* m = menu();
    if (!m || m->seconds <= 0.0f) return std::nullopt;
    return std::clamp(menuTime_ / m->seconds, 0.0f, 1.0f);
}

void Game::choose(std::size_t index) {
    const cmd::Menu* m = menu();
    if (!m || index >= choiceItems_.size() || choiceItems_[index].locked) return;
    const cmd::Choice& picked = m->choices[static_cast<std::size_t>(choiceItems_[index].option)];
    ctx_.read.insert(picked.id); // picked before, from now on
    chartOption_ = choiceItems_[index].option;
    RichText chosen = richText(choiceItems_[index].text, lookup_, &script_.palette);
    for (TextSpan& span : chosen.spans) span.begin += 2, span.end += 2; // after "> "
    addHistory({.speaker = "", .color = SDL_Color{150, 150, 165, 255}, .text = "> " + chosen.text, .choice = true, .spans = std::move(chosen.spans)});
    if (wait_ != Wait::Menu) { // one up while the story goes on: its lines, then back where the story was (or past its lines)
        if (picked.label.empty()) {
            callStack_.push_back(wait_ == Wait::Say ? pc_ : resumePc_);
            if (!m->returns) pass_ = Pass{.depth = callStack_.size(), .lines = trigger_->lines};
        }
        trigger_.reset();
        ctx_.audio.stopVoice();
    }
    pc_ = picked.target;
    choiceItems_.clear();
    menuPrompt_ = false;
    wait_ = Wait::None;
    run();
}

bool Game::have(RecordKind kind, std::string_view name) {
    if (!script_.record(kind, name)) return false;
    const std::string var = Script::recordVariable(kind, name);
    const bool fresh = !truthy(variable(var));
    if (fresh) setVariable(var, 1);
    return fresh;
}

// The player has it now, with its message: every ending, a new achievement or tip.
void Game::collect(RecordKind kind, std::string_view name) {
    const Script::Record* r = script_.record(kind, name);
    if (!r) return;
    std::ignore = have(kind, name); // (the story says so itself, if it wants to: notify after it)
}

// The tips a text shows ({tip NAME}...{/tip}): learned when it's said.
void Game::learnTips(std::string_view text) {
    (void)expand(text, lookup_, [&](std::string_view tag, std::string_view name, std::size_t) {
        if (tag == "tip") collect(RecordKind::Tip, name);
    });
}

void Game::waitForScreen(ScreenRequest request, std::size_t here) {
    request_ = std::move(request);
    screenRequested_ = true;
    skip_ = false;
    waitFor(Wait::Screen, here);
}

void Game::screenClosed(std::optional<Value> answer) {
    if (wait_ != Wait::Screen) return;
    if (answer) vars_.insert_or_assign("answer", std::move(*answer));
    request_ = {};
    wait_ = Wait::None;
    run();
}

void Game::update(float dt) {
    const bool fast = skipping();
    if (fast) stage_.finish();
    stage_.setStill(!ctx_.settings.animations); // (animations off: still)
    stage_.update(dt);
    blipTimer_ -= dt;
    clock_ += dt;
    lineTime_ += dt;
    typing_ = false;
    const float cps = static_cast<float>(ctx_.settings.textSpeed);
    for (BoxLine& l : others_) // lines in other textboxes type on, each at its textbox's speed
        l.reveal = fast || cps <= 0.0f ? static_cast<float>(l.length) : std::min(static_cast<float>(l.length), l.reveal + cps * speedOf(l.box) * dt);

    switch (wait_) {
    case Wait::None:
        run();
        break;
    case Wait::Say:
        updateLine(dt, fast);
        break;
    case Wait::Background:
        if (!stage_.backgroundComing()) {
            wait_ = Wait::None;
            run();
        }
        break;
    case Wait::Move:
        if (!stage_.moving()) {
            wait_ = Wait::None;
            run();
        }
        break;
    case Wait::Timer:
        if ((timer_ -= dt) <= 0.0f || fast) {
            wait_ = Wait::None;
            run();
        }
        break;
    case Wait::Pause: // until a click, or auto-forward's delay
        if (fast || (auto_ && (autoTimer_ += dt) >= static_cast<float>(ctx_.settings.autoDelay) / 1000.0f)) advance();
        break;
    case Wait::Movie:
        if (movie_) movie_->update(dt);
        if ((movie_ ? movie_->ended() : (timer_ -= dt) <= 0.0f) || (fast && movieSkippable_)) endMovie();
        break;
    case Wait::Menu:
        if (menu()->seconds > 0.0f && (menuTime_ -= dt) <= 0.0f) letPass(); // time's up
        break;
    case Wait::Screen:
    case Wait::Reached: // (stopBefore: it's saved from there, not played on)
    case Wait::Stopped: // (stopWhere: until goOn())
    case Wait::Finished:
        break;
    }
    ctx_.audio.holdVoice(voiceAtPause());
    updateSpeech();
}

// A line that follows its voice waits at a {pause} for a click, and so does the voice, once it has
// said the words before it: in the silence there (the one nearest to where the {pause} is in the
// text), else right at that place. A click that shows the text early lets the voice catch up first.
bool Game::voiceAtPause() const {
    if (wait_ != Wait::Say || !syncVoice_) return false;
    const Stop* stop = pendingStop();
    if (!stop || stop->seconds >= 0.0f) return false;
    const auto said = ctx_.audio.voiceProgress();
    if (!said) return false;
    const float at = static_cast<float>(stop->at) / static_cast<float>(std::max<std::size_t>(length_, 1));
    if (const auto silence = ctx_.audio.voiceSilenceNear(at)) return *said >= *silence && ctx_.audio.voiceSilent();
    return *said >= at;
}

// The line on screen: types out (or follows its voice), holds at {pause}s, and goes on by itself
// when skipping, on auto-forward or with {nowait}.
void Game::updateLine(float dt, bool fast) {
    const auto length = static_cast<float>(length_);
    const Settings& s = ctx_.settings;
    const bool skipLine = fast && (lineRead_ || s.skipUnread);
    if (fast && !skipLine) skip_ = false; // reached text the player hasn't read yet
    if (skipLine) {
        reveal_ = length;
        passedStops_ = stops_.size();
        if ((skipTimer_ += dt) >= config().skipDelay) {
            skipTimer_ = 0.0f;
            advance();
        }
        return;
    }
    // A line that follows its voice shows as much as has been said; timed pauses don't apply.
    std::optional<float> said;
    if (syncVoice_) {
        said = ctx_.audio.voiceProgress();
        if (!said) syncVoice_ = false; // the voice ended (or can't play): the rest types as usual
        else
            for (const Stop* p = pendingStop(); p && p->seconds >= 0.0f && static_cast<float>(p->at) <= *said * length; p = pendingStop())
                ++passedStops_;
    }
    const Stop* stop = pendingStop();
    if (stop && reveal_ >= static_cast<float>(stop->at)) {
        if (stop->seconds >= 0.0f) { // timed pause
            if ((holdTimer_ += dt) >= stop->seconds) {
                holdTimer_ = 0.0f;
                ++passedStops_;
            }
        } else if (auto_ && (autoTimer_ += dt) >= static_cast<float>(s.autoDelay) / 1000.0f) {
            autoTimer_ = 0.0f;
            advance();
        }
        return;
    }
    const float limit = stop ? static_cast<float>(stop->at) : length;
    float next = limit;
    if (said) next = voiceAtPause() ? limit : std::min(limit, std::max(reveal_, *said * length)); // (the voice waits at a {pause}: all of it)
    else if (const int cps = currentSpeed(); cps > 0) next = std::min(limit, reveal_ + static_cast<float>(cps) * dt);
    if (static_cast<std::size_t>(next) > static_cast<std::size_t>(reveal_)) revealTo(static_cast<std::size_t>(next)); // blip
    reveal_ = next;
    typing_ = reveal_ < limit;
    if (reveal_ < length || pendingStop() || othersTyping()) return; // (lines said together with it still typing too)
    // {nowait} and auto-forward wait for the voice line to end; reading time per letter is only
    // added to lines without a voice (the voice gave the player that time).
    const bool talking = voiced_ && ctx_.audio.voicePlaying();
    if (noWait_) {
        if (!talking) advance();
        return;
    }
    const float delay = static_cast<float>(s.autoDelay) / 1000.0f + (voiced_ ? 0.0f : length * config().autoPerChar);
    if (auto_ && !talking && (autoTimer_ += dt) >= delay) advance();
}

// How much the speaker is speaking right now: their voice's loudness, or a steady flapping while
// their text types out (lines without a voice). A face that doesn't copy their mouth flaps at its
// own moments instead, opening as wide as the voice is loud.
void Game::updateSpeech() {
    speech_ = faceSpeech_ = 0.0f;
    if (wait_ != Wait::Say || speakerKey_.empty()) return;
    const CharacterDef* def = script_.character(speakerKey_);
    const float flaps = def ? def->talkSpeed : config().talkSpeed;
    float loudness = 1.0f;
    if (voiced_) {
        if (!ctx_.audio.voicePlaying()) return;
        speech_ = loudness = ctx_.audio.voiceLevel();
    } else if (typing_) {
        speech_ = 0.5f - 0.5f * std::cos(lineTime_ * flaps * 6.2831853f);
    } else {
        return;
    }
    faceSpeech_ = flapping(speakerKey_ + " face", lineTime_, flaps) * std::min(1.0f, loudness * 1.5f);
}

float Game::mouth(Talk talk, float speech, bool voiced) const {
    talk = std::max(talk, static_cast<Talk>(std::clamp(ctx_.settings.mouths, 0, 2))); // (Talk goes Yes, Voice, No, as the setting does)
    return talk == Talk::Yes || (talk == Talk::Voice && voiced) ? speech : 0.0f;
}

std::vector<Game::Upcoming> Game::upcomingPictures(int commands) const {
    std::vector<Upcoming> out;
    unsigned looks = stage_.effect().looks & kColorLooks; // the colour effects on stage from there on
    std::vector<std::string> stage;                       // what's on it
    std::vector<std::string> people;                      // the characters on it (a line can change their look)
    auto pictureOf = [&](const std::string& key, const std::string& expression) {
        const CharacterDef* def = script_.character(key);
        return def ? def->pictureOf(expression) : std::string{};
    };
    auto show = [&](std::string name) {
        if (name.empty()) return;
        if (looks != 0) out.push_back({name, looks});
        out.push_back({name, 0}); // (as it is too: for when the effect fades)
        stage.push_back(std::move(name));
    };
    if (!stage_.background().starts_with('#')) stage.push_back(stage_.background());
    for (const Stage::ObjectState& o : stage_.objects()) {
        stage.push_back(o.character.empty() ? o.image : pictureOf(o.character, o.expression));
        if (!o.character.empty()) people.push_back(o.character);
    }
    const std::size_t end = std::min(script_.code.size(), resumePc_ + static_cast<std::size_t>(std::max(0, commands)));
    for (std::size_t pc = resumePc_; pc < end; ++pc)
        std::visit(Overloaded{
                       [&](const cmd::Scene& c) {
                           stage.clear();
                           people.clear();
                           if (!c.image.starts_with('#')) show(c.image);
                       },
                       [&](const cmd::Show& c) {
                           show(c.character.empty() ? c.image : pictureOf(c.character, c.expression));
                           if (!c.character.empty()) people.push_back(c.character);
                       },
                       [&](const cmd::Say& c) {
                           if (c.character.empty()) return;
                           if (!c.expression.empty() && std::ranges::contains(people, c.character)) show(pictureOf(c.character, c.expression));
                           if (const CharacterDef* def = script_.character(c.character); def && !def->face.empty()) // (in the textbox: no effects)
                               out.push_back({def->faceOf(c.expression)});
                       },
                       [&](const cmd::Effect& c) { // what's on stage then is drawn in its colours (for a moment, or from then on)
                           const unsigned with = (c.seconds > 0.0f ? looks | c.looks : c.looks) & kColorLooks;
                           if (with != 0)
                               for (const std::string& name : stage) out.push_back({name, with});
                           if (c.seconds <= 0.0f) looks = with;
                       },
                       [](const auto&) {},
                   },
                   script_.code[pc].command);
    return out;
}

Frame Game::frame(std::string_view who) const {
    const CharacterDef* def = script_.character(who);
    Frame f;
    if (def ? def->blink : config().blink) f.eyes = blinking(who, clock_, def ? def->blinkEvery : config().blinkEvery);
    if (def && who == speakerKey_) f.mouth = mouth(def->talk, speech_, voiced_);
    else if (def && std::ranges::any_of(others_, [&](const BoxLine& l) { return l.speakerKey == who && l.reveal < static_cast<float>(l.length); }))
        f.mouth = mouth(def->talk, flapping(who, clock_, def->talkSpeed), false); // a line said together: typing without a voice
    return f;
}

Frame Game::faceFrame() const {
    const CharacterDef* def = script_.character(speakerKey_);
    if (!def) return {};
    // A face that copies the character blinks and talks with them; otherwise at its own moments.
    Frame f{.mouth = mouth(def->faceTalk, def->faceSync.talk ? speech_ : faceSpeech_, voiced_)};
    if (def->faceBlink) f.eyes = blinking(def->faceSync.blink ? speakerKey_ : speakerKey_ + " face", clock_, def->blinkEvery);
    return f;
}

void Game::draw(const Font& labelFont) {
    if (wait_ == Wait::Movie) { // a video covers everything, as big as fits
        SDL_Renderer* r = ctx_.renderer;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderFillRect(r, nullptr);
        if (SDL_Texture* frame = movie_ ? movie_->texture() : nullptr) {
            const float scale = std::min(ctx_.canvas.w / movie_->width(), ctx_.canvas.h / movie_->height());
            const float w = movie_->width() * scale, h = movie_->height() * scale;
            const SDL_FRect dst{(ctx_.canvas.w - w) * 0.5f, (ctx_.canvas.h - h) * 0.5f, w, h};
            SDL_RenderTexture(r, frame, nullptr, &dst);
        } else if (!movieStandIn_.empty()) { // (developer mode) the video that's missing
            movieLabel_.set(labelFont, std::format("movies/{}.mpg", movieStandIn_));
            const SDL_FPoint size = movieLabel_.size();
            movieLabel_.draw(r, (ctx_.canvas.w - size.x) * 0.5f, (ctx_.canvas.h - size.y) * 0.5f, theme::dim);
        }
        return;
    }
    stage_.draw(ctx_.renderer, ctx_.assets, labelFont, config().developer, config().spriteHeight,
                [this](const Stage::ObjectState& s) { return frame(s.character.empty() ? s.tag : s.character); });
}

// ---------------------------------------------------------------------------
// Save / load
// ---------------------------------------------------------------------------

std::string Game::save() const {
    auto located = [&](std::size_t pc) {
        auto [label, offset] = script_.locate(pc);
        return std::format("{} {} {}", label.empty() ? "-" : label, offset, pc);
    };
    std::string out = std::format("{}\nhash {}\nresume {}\nrandom {}\n", kSaveMagic, script_.hash, located(resumePc_), resumeRandom_);
    for (auto pc : callStack_) out += std::format("call {}\n", located(pc));
    for (const auto& [name, value] : vars_) out += std::format("var {} {}\n", name, encodeValue(value));
    for (const auto& o : stage_.objects())
        out += std::format("object {} char={} image={} expr={} x={} y={} zoom={} alpha={} rotate={} flip={} layer={} "
                           "tint=#{:02x}{:02x}{:02x}{}{}\n",
                           escape(o.tag), escape(o.character), escape(o.image), escape(o.expression),
                           std::lround(o.x * 10000.0f), std::lround(o.y * 10000.0f), std::lround(o.zoom * 10000.0f), std::lround(o.opacity * 10000.0f),
                           std::lround(o.rotate * 100.0f), o.flip ? 1 : 0, o.layer, o.tint.r, o.tint.g, o.tint.b,
                           o.animation.empty() ? "" : " animation=" + escape(o.animation), o.played.empty() ? "" : " played=" + escape(o.played));
    if (!stage_.background().empty())
        out += std::format("scene {}{}{}\n", stage_.background(), stage_.backgroundAnimation(true).empty() ? "" : " animation=" + escape(stage_.backgroundAnimation(true)),
                           stage_.backgroundAnimation(false).empty() ? "" : " played=" + escape(stage_.backgroundAnimation(false)));
    if (!chapter_.empty()) out += std::format("chapter {}\n", escape(chapter_));
    if (!chartNode_.empty()) out += std::format("chartplace {} {}\n", chartNode_, chartOption_);
    if (!music_.empty()) out += std::format("musicvol {}\nmusic {}\n", std::lround(musicVolume_ * 1000.0f), music_);
    for (const auto& [channel, loop] : loops_)
        out += std::format("loop {} {} {}\n", escape(channel), std::lround(loop.volume * 1000.0f), loop.file);
    for (const auto& screen : screens_) out += std::format("screen {}\n", screen);
    for (const MessageItem& m : messages_) // (one still being typed: "typing MILLISECONDS message..."; the player's own: "sent ... TO TEXT")
        out += std::format("{}{} {} {} {} {} {} {}{}\n", m.typing > 0.0f ? std::format("typing {} ", std::lround(m.typing * 1000.0f)) : "",
                           m.from.empty() ? "sent" : "message", m.read ? 1 : 0, m.color.r, m.color.g, m.color.b, escape(m.with), escape(m.text),
                           m.replies ? " " + located(*m.replies) : "");
    if (!phoneOn_) out += "phone off\n";
    if (!textboxScreen_.empty()) out += std::format("textbox {}\n", textboxScreen_);
    for (std::size_t i = 0; i + (wait_ == Wait::Say ? 1 : 0) < page_.size(); ++i) { // (the line on screen is said again)
        const PageLine& p = page_[i];
        out += std::format("page {} {} {} {} {}\n", p.color.r, p.color.g, p.color.b, escape(p.speaker.empty() ? "-" : p.speaker), escape(p.text));
    }
    if (const cmd::Weather& w = stage_.weather(); !w.kind.empty())
        out += std::format("weather {} {} {} {} {} {} {}\n", w.kind, std::lround(w.amount * 1000.0f), std::lround(w.speed * 1000.0f),
                           std::lround(w.wind * 1000.0f), w.behind ? 1 : 0,
                           w.color ? std::format("#{:02x}{:02x}{:02x}{:02x}", w.color->r, w.color->g, w.color->b, w.color->a) : "-",
                           w.image.empty() ? "-" : w.image);
    if (const cmd::Effect e = stage_.effect(); e.looks != 0) out += std::format("effect {} {}\n", e.looks, std::lround(e.amount * 1000.0f));
    if (trigger_) out += std::format("trigger {} {}\n", located(trigger_->at), trigger_->lines + (wait_ == Wait::Say ? 1 : 0)); // (the line is said again)
    for (const BoxLine& l : others_)
        out += std::format("box {} {} {} {} {} {} {} {} {}\n", l.keep ? 1 : 0, l.speakerColor.r, l.speakerColor.g, l.speakerColor.b, escape(l.box),
                           escape(l.speakerKey.empty() ? "-" : l.speakerKey), escape(l.speaker.empty() ? "-" : l.speaker), escape(l.face.empty() ? "-" : l.face),
                           escape(l.source));
    if (pass_) out += std::format("pass {} {}\n", pass_->depth, pass_->lines);
    if (hiddenLines_ > 0) out += std::format("hidden {}\n", hiddenLines_);
    if (const std::string own = stage_.cameraAnimation(true), played = stage_.cameraAnimation(false); !own.empty() || !played.empty()) {
        // (its animations, then where the one played on it started: its numbers, colours and wipe's shape)
        const Animation::Look& s = stage_.cameraStart();
        auto hex = [](SDL_Color c) { return std::format("#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a); };
        out += std::format("camera {} {} {} {} {} {} {} {} {} {} {} {}\n", own.empty() ? "-" : escape(own), played.empty() ? "-" : escape(played),
                           std::lround(s.x * 1000.0f), std::lround(s.y * 1000.0f), std::lround(s.opacity * 1000.0f), std::lround(s.zoom * 1000.0f),
                           std::lround(s.rotate * 1000.0f), std::lround(s.fade * 1000.0f), std::lround(s.wipe * 1000.0f), hex(s.fadeColor),
                           s.color ? hex(*s.color) : "-", s.wipeShape.empty() ? "-" : escape(s.wipeShape));
    }
    for (const auto& [character, expression] : expressions_) out += std::format("expr {} {}\n", escape(character), escape(expression));
    if (wait_ == Wait::Say && !lineVoice_.file.empty()) // the line's voice plays again when it's loaded
        out += std::format("voice {} {} {}\n", lineVoice_.sync ? (*lineVoice_.sync ? 1 : 0) : -1, std::lround(lineVoice_.volume * 1000.0f),
                           lineVoice_.file);
    return out;
}

std::expected<std::string, std::string> Game::load(std::string_view data) {
    auto note = restore(data, std::nullopt);
    if (note) past_.clear();
    autosave_ = false; // reaching a choice by loading is not a new decision point
    return note;
}

void Game::remember() {
    if (config().rollback <= 0) return;
    const bool line = wait_ == Wait::Say;
    past_.push_back({save(), historyDropped_ + history_.size() - (line && !history_.empty() ? 1 : 0), !(line && noWait_), ctx_.persistent});
    if (past_.size() > static_cast<std::size_t>(config().rollback) + 1) past_.erase(past_.begin());
}

bool Game::canRollback() const {
    if (!ctx_.settings.rollback || (wait_ != Wait::Say && wait_ != Wait::Menu) || past_.size() < 2) return false;
    return std::any_of(past_.begin(), past_.end() - 1, [](const Moment& m) { return m.stop; });
}

bool Game::rollback() {
    if (!canRollback()) return false;
    std::size_t moment = past_.size() - 2; // before the line on screen now ({nowait} lines go by)
    while (!past_[moment].stop) --moment;
    return rewind(moment);
}

std::optional<std::size_t> Game::momentOf(std::size_t entry) const {
    if (!ctx_.settings.rollback || (wait_ != Wait::Say && wait_ != Wait::Menu)) return std::nullopt;
    const std::size_t counted = historyDropped_ + entry; // a line's entry is its moment's history; a choice's, its menu's
    for (std::size_t i = 0; i + 1 < past_.size(); ++i)
        if (past_[i].stop && past_[i].history == counted) return i;
    return std::nullopt;
}

bool Game::rollbackTo(std::size_t entry) {
    const auto moment = momentOf(entry);
    return moment && rewind(*moment);
}

void Game::addSamples() {
    if (history_.empty()) {
        addHistory({.speaker = "Aiko", .color = {255, 158, 207, 255}, .text = "Good morning! I brought cake."});
        addHistory({.speaker = "", .color = {230, 230, 235, 255}, .text = "The rain didn't stop all day."});
        addHistory({.speaker = "", .color = {150, 150, 165, 255}, .text = "> Tell the truth", .choice = true});
        addHistory({.speaker = "Aiko", .color = {255, 158, 207, 255}, .text = "Thank you for being honest."});
    }
    if (messages_.empty()) {
        messages_.push_back({.from = "Aiko", .with = "Aiko", .color = {255, 158, 207, 255}, .text = "Are you coming today?", .read = true});
        messages_.push_back({.from = "", .with = "Aiko", .color = {230, 230, 235, 255}, .text = "On my way!", .read = true});
        messages_.push_back({.from = "Mr. Tanaka", .with = "Mr. Tanaka", .color = {198, 242, 139, 255}, .text = "The meeting moved to four.", .read = true});
        messages_.push_back({.from = "Aiko", .with = "Aiko", .color = {255, 158, 207, 255}, .text = "Hurry, it's starting to rain.", .read = false});
    }
}

void Game::addHistory(HistoryEntry entry) {
    history_.push_back(std::move(entry));
    const auto limit = static_cast<std::size_t>(std::max(1, config().historyLimit));
    if (history_.size() > limit) {
        const std::size_t drop = history_.size() - limit;
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(drop));
        historyDropped_ += drop;
    }
}

bool Game::rewind(std::size_t moment) {
    past_.erase(past_.begin() + static_cast<std::ptrdiff_t>(moment) + 1, past_.end());
    Moment back = std::move(past_.back());
    past_.pop_back(); // showing that line again remembers it again
    skip_ = false;
    // What the story did to persistent variables since then is undone too, so going forward again
    // doesn't do it twice (a visit counter, say). Gallery pictures, endings, songs and achievements
    // stay: what was seen stays seen.
    std::erase_if(ctx_.persistent, [&](const auto& v) { return !Script::isRecordVariable(v.first) && !back.persistent.contains(v.first); });
    for (auto& [name, value] : back.persistent)
        if (!Script::isRecordVariable(name)) ctx_.persistent.insert_or_assign(name, std::move(value));
    persistentChanged_ = true;
    const bool ok = restore(back.state, back.history).has_value();
    autosave_ = false; // going back to a choice isn't a new decision
    return ok;
}

std::expected<std::string, std::string> Game::restore(std::string_view data, std::optional<std::size_t> keepHistory) {
    std::istringstream in{std::string(data)};
    std::string line;
    if (!std::getline(in, line) || line != kSaveMagic) return std::unexpected("not a VNext save file");

    std::uint64_t hash = 0;
    std::string note;
    std::optional<std::size_t> resume;
    std::optional<std::uint64_t> random; // (saves from before random() was saved have none)
    std::optional<Trigger> trigger;
    std::optional<Pass> pass;
    int hidden = 0;
    std::vector<BoxLine> others;
    std::vector<std::size_t> stack;
    Variables vars;
    std::vector<Stage::ObjectState> objects;
    std::string scene, sceneAnimation, scenePlayed, music, chapter, chartNode;
    int chartOption = -1;
    float musicVolume = 1.0f;
    std::map<std::string, Loop, std::less<>> loops;
    std::vector<std::string> screens;
    std::vector<MessageItem> messages;
    bool phoneOn = true;
    std::map<std::string, std::string, std::less<>> expressions;
    std::optional<LineVoice> voice;
    std::string textbox;
    std::vector<PageLine> page;
    cmd::Weather weather;
    cmd::Effect effect;
    std::string cameraOwn, cameraPlayed;
    Animation::Look cameraStart;

    // The exact command when the script is unchanged; otherwise label + offset.
    auto resolve = [&](std::istream& s) -> std::optional<std::size_t> {
        std::string label;
        std::size_t offset = 0, pc = 0;
        if (!(s >> label >> offset >> pc)) return std::nullopt;
        if (hash == script_.hash && pc <= script_.code.size()) return pc; // (the end: a scene played on its own returns there)
        if (auto at = script_.findLabel(label)) {
            note = std::format("The script changed since this save, so it goes on near '{}'.", label);
            return std::min(*at + offset, script_.code.size());
        }
        return std::nullopt;
    };

    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        std::string rest;
        if (const auto sp = line.find(' '); sp != std::string::npos) rest = line.substr(sp + 1);

        if (key == "hash") ls >> hash;
        else if (key == "resume") resume = resolve(ls);
        else if (std::uint64_t seed = 0; key == "random" && ls >> seed) random = seed;
        else if (key == "trigger") {
            const auto at = resolve(ls);
            if (int lines = 0; at && ls >> lines) trigger = Trigger{.at = *at, .lines = lines};
        } else if (Pass passed; key == "pass" && ls >> passed.depth >> passed.lines) pass = passed;
        else if (key == "hidden") ls >> hidden;
        else if (key == "box") { // a line in another textbox: all there
            int keep = 0, r = 0, g = 0, b = 0;
            std::string box, speakerKey, speaker, face, source;
            ls >> keep >> r >> g >> b >> box >> speakerKey >> speaker >> face >> source;
            auto none = [](const std::string& s) { return s == "-" ? std::string{} : s; };
            BoxLine l = boxLine(unescape(box), none(unescape(speaker)), {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255},
                                unescape(source), script_.character(none(unescape(speakerKey))));
            l.keep = keep != 0;
            l.face = none(unescape(face));
            l.reveal = static_cast<float>(l.length);
            others.push_back(std::move(l));
        }
        else if (key == "call") {
            if (auto pc = resolve(ls)) stack.push_back(*pc);
        } else if (key == "var") {
            std::string name, value;
            ls >> name;
            std::getline(ls >> std::ws, value);
            vars.insert_or_assign(name, decodeValue(value));
        } else if (key == "object") {
            Stage::ObjectState o;
            std::string tag, field;
            ls >> tag;
            o.tag = unescape(tag);
            while (ls >> field) {
                const auto eq = field.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = field.substr(0, eq), v = unescape(field.substr(eq + 1));
                const float n = static_cast<float>(toInt(v));
                if (k == "char") o.character = v;
                else if (k == "image") o.image = v;
                else if (k == "expr") o.expression = v;
                else if (k == "x") o.x = n / 10000.0f;
                else if (k == "y") o.y = n / 10000.0f;
                else if (k == "zoom") o.zoom = n / 10000.0f;
                else if (k == "alpha") o.opacity = n / 10000.0f;
                else if (k == "rotate") o.rotate = n / 100.0f;
                else if (k == "flip") o.flip = n != 0.0f;
                else if (k == "layer") o.layer = static_cast<int>(n);
                else if (k == "tint") o.tint = parseColor(v).value_or(o.tint);
                else if (k == "animation") o.animation = v;
                else if (k == "played") o.played = v;
            }
            objects.push_back(std::move(o));
        } else if (key == "scene") { // (its name, then its animations: key=value)
            ls >> scene;
            std::string field;
            while (ls >> field)
                if (field.starts_with("animation=")) sceneAnimation = unescape(field.substr(10));
                else if (field.starts_with("played=")) scenePlayed = unescape(field.substr(7));
        }
        else if (key == "chapter") chapter = unescape(rest);
        else if (key == "chartplace") ls >> chartNode >> chartOption;
        else if (key == "music") music = rest;
        else if (key == "musicvol") musicVolume = static_cast<float>(toInt(rest)) / 1000.0f;
        else if (key == "loop") {
            std::string channel, file;
            long volume = 1000;
            ls >> channel >> volume;
            std::getline(ls >> std::ws, file);
            loops.insert_or_assign(unescape(channel), Loop{file, static_cast<float>(volume) / 1000.0f});
        } else if (key == "screen") {
            screens.push_back(rest);
        } else if (key == "message" || key == "sent" || key == "typing") {
            long typing = 0;
            if (key == "typing") ls >> typing >> key; // "message"
            int read = 0, r = 0, g = 0, b = 0;
            std::string with, text;
            ls >> read >> r >> g >> b >> with >> text;
            with = unescape(with);
            const bool mine = key == "sent";
            messages.push_back({.from = mine ? std::string{} : with, .with = with, .color = {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255},
                                .text = unescape(text), .read = read != 0, .replies = mine ? std::nullopt : resolve(ls),
                                .typing = static_cast<float>(typing) / 1000.0f});
        } else if (key == "phone") {
            phoneOn = rest != "off";
        } else if (key == "expr") {
            std::string character, expression;
            ls >> character >> expression;
            expressions.insert_or_assign(unescape(character), unescape(expression));
        } else if (key == "textbox") {
            textbox = rest;
        } else if (key == "page") {
            int r = 0, g = 0, b = 0;
            std::string speaker, text;
            ls >> r >> g >> b >> speaker >> text;
            speaker = unescape(speaker);
            page.push_back({speaker == "-" ? std::string{} : speaker, {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255},
                            unescape(text), {}, std::nullopt});
        } else if (key == "weather") {
            long amount = 1000, speed = 1000, wind = 0;
            int behind = 0;
            std::string colorText, image;
            ls >> weather.kind >> amount >> speed >> wind >> behind >> colorText >> image;
            weather.amount = static_cast<float>(amount) / 1000.0f;
            weather.speed = static_cast<float>(speed) / 1000.0f;
            weather.wind = static_cast<float>(wind) / 1000.0f;
            weather.behind = behind != 0;
            if (colorText != "-") weather.color = parseColor(colorText);
            if (image != "-") weather.image = image;
        } else if (key == "effect") {
            long amount = 1000;
            ls >> effect.looks >> amount;
            effect.amount = static_cast<float>(amount) / 1000.0f;
        } else if (key == "camera") {
            std::string own, played, fadeColor, color, shape;
            long x = 0, y = 0, opacity = 1000, zoom = 1000, rotate = 0, fade = 0, wipe = 1000;
            ls >> own >> played >> x >> y >> opacity >> zoom >> rotate >> fade >> wipe >> fadeColor >> color >> shape;
            auto named = [](const std::string& s) { return s == "-" ? std::string{} : unescape(s); };
            auto thousandths = [](long n) { return static_cast<float>(n) / 1000.0f; };
            cameraOwn = named(own);
            cameraPlayed = named(played);
            cameraStart = {.x = thousandths(x), .y = thousandths(y), .opacity = thousandths(opacity), .zoom = thousandths(zoom), .rotate = thousandths(rotate),
                           .color = color == "-" ? std::nullopt : parseColor(color), .fade = thousandths(fade),
                           .fadeColor = parseColor(fadeColor).value_or(SDL_Color{0, 0, 0, 255}), .wipe = thousandths(wipe), .wipeShape = named(shape)};
        } else if (key == "voice") {
            int sync = -1;
            long volume = 1000;
            LineVoice v;
            ls >> sync >> volume;
            std::getline(ls >> std::ws, v.file);
            v.volume = static_cast<float>(volume) / 1000.0f;
            if (sync >= 0) v.sync = sync == 1;
            voice = std::move(v);
        }
    }
    if (!resume) return std::unexpected("this save points to a label that no longer exists");

    stage_.reset();
    for (const auto& o : objects) {
        const CharacterDef* def = o.character.empty() ? nullptr : script_.character(o.character);
        if (!o.character.empty() && !def) continue; // that character no longer exists
        stage_.restore(o, def);
        // Its animations: from their start if they play again and again, else as they ended. (One no
        // longer in the scripts: none.)
        stage_.animate(o.tag, animation(o.animation), true, true);
        stage_.animate(o.tag, animation(o.played), false, true);
    }
    if (!scene.empty()) {
        stage_.setBackground(scene, nullptr, true);
        stage_.animateBackground(animation(sceneAnimation), true, true);
        stage_.animateBackground(animation(scenePlayed), false, true);
    }
    chapter_ = chapter;
    chartNode_ = chartNode;
    chartOption_ = chartOption;
    stage_.setWeather(weather, 0.0f);
    stage_.setEffect(effect, true);
    stage_.restoreCamera(animation(cameraOwn), animation(cameraPlayed), cameraStart);
    textboxScreen_ = textbox;
    page_ = std::move(page);
    vars_ = std::move(vars);
    callStack_ = std::move(stack);
    screens_ = std::move(screens);
    messages_ = std::move(messages);
    phoneOn_ = phoneOn;
    expressions_ = std::move(expressions);
    music_ = music;
    musicVolume_ = musicVolume;
    ctx_.audio.stopVoice();
    if (music.empty()) ctx_.audio.stopMusic(0.5f);
    else ctx_.audio.playMusic(ctx_.assets.audio(music), true, 0.5f, musicVolume); // the same song keeps playing
    // Looping sounds: a load starts them over; rollback keeps those that are already right.
    const bool rollingBack = keepHistory.has_value();
    if (!rollingBack) ctx_.audio.stopChannel("", 0.3f);
    for (const auto& [channel, loop] : loops_)
        if (rollingBack && (!loops.contains(channel) || loops.at(channel).file != loop.file)) ctx_.audio.stopChannel(channel, 0.3f);
    for (const auto& [channel, loop] : loops)
        if (!rollingBack || !loops_.contains(channel) || loops_.at(channel).file != loop.file)
            ctx_.audio.playChannel(channel, ctx_.assets.audio(loop.file), true, loop.volume, 0.5f);
    loops_ = std::move(loops);

    // Rollback keeps the entries before its line (counting those let go); a load starts afresh.
    if (!keepHistory) historyDropped_ = 0;
    history_.resize(std::min(history_.size(), keepHistory && *keepHistory > historyDropped_ ? *keepHistory - historyDropped_ : 0));
    choiceItems_.clear();
    trigger_ = trigger;
    if (const cmd::Menu* m = trigger && trigger->at < script_.code.size() ? std::get_if<cmd::Menu>(&script_.code[trigger->at].command) : nullptr)
        listChoices(*m);
    else trigger_.reset();
    pass_ = pass;
    hiddenLines_ = hidden;
    others_ = std::move(others);
    together_ = std::ranges::any_of(others_, [](const BoxLine& l) { return !l.keep; }); // (the last line of a together block is said again)
    error_.clear();
    skip_ = uiHidden_ = menuPrompt_ = false;
    pendingVoice_ = voice; // the line's own voice line plays again
    if (voice && !skipping()) ctx_.audio.playVoice(ctx_.assets.audio(voice->file), voice->volume);
    pc_ = resumePc_ = *resume;
    if (random) random_ = *random;
    wait_ = Wait::None;
    run();
    return note;
}

} // namespace vn
