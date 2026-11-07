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

constexpr std::string_view kMagic = "vnext-flowchart 1";

// A save is several lines; in flowchart.txt it's one.
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

// The player's clock: their date (days since 1970), minutes from midnight, and seconds since 1970.
struct RealNow {
    int date = 0, minutes = 0;
    double seconds = 0.0;
};
RealNow realNow() {
    SDL_Time now = 0;
    SDL_DateTime t{};
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &t, true)) return {};
    return {daysFromDate(t.year, t.month, t.day), t.hour * 60 + t.minute, static_cast<double>(now) / 1e9};
}

// `text` with each {name} in it replaced by its value.
std::string filledIn(std::string text, std::initializer_list<std::pair<std::string_view, std::string>> values) {
    for (const auto& [name, value] : values) {
        const std::string tag = std::format("{{{}}}", name);
        for (std::size_t at = text.find(tag); at != std::string::npos; at = text.find(tag, at + value.size())) text.replace(at, tag.size(), value);
    }
    return text;
}

} // namespace

void Flowchart::visit(const std::string& id, Kind kind, const std::string& from, int option, std::string state,
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

const Flowchart::Node* Flowchart::node(std::string_view id) const {
    auto it = std::ranges::find(nodes_, id, &Node::id);
    return it != nodes_.end() ? &*it : nullptr;
}

void Flowchart::clear() {
    nodes_.clear();
    edges_.clear();
    changed_ = true;
}

// flowchart.txt:  node ID KIND ORDER [OPTION:OPEN ...]   state ID SAVE   edge FROM TO OPTION
bool Flowchart::read(std::string_view saved) {
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
            n.kind = static_cast<Kind>(std::clamp(kind, 0, 1));
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

std::string Flowchart::text() const {
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
    std::erase_if(objects_, [](const Object& o) { return o.oldBackground; });
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

void Stage::show(const std::string& tag, const CharacterDef* def, const std::string& image, const std::string& expression,
                 const StageProps& props, std::shared_ptr<const Animation> played, float slide, bool done) {
    auto play = [&](Object& o) {
        if (!played) return;
        o.s.played = played->name;
        o.animations.set(std::move(played), false, done || still_);
    };
    if (Object* o = find(tag)) {
        if (o->leaving || o->oldBackground) { // (going: it stays, as it was before it began to go)
            o->leaving = o->oldBackground = false;
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
        const Image pic = w.image.empty() ? Image{} : assets.image(w.image, {}, colourAlpha_ >= 0.5f ? colourMask_ : 0);
        for (const auto& p : ps) {
            const float x = p.x, y = p.y;
            float a = alpha;
            if (w.kind == "embers") a *= 0.6f + 0.4f * std::sin(clock_ * 7.0f + p.phase); // flicker
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

// TV static: an image of random dots, a different part of it each frame.
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
    for (auto& o : objects_) o.oldBackground = true;
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
    // What's on stage, in its layers (and, among those, in the order they came).
    auto drawObjects = [&](bool old) {
        std::vector<const Object*> sorted;
        sorted.reserve(objects_.size());
        for (const auto& o : objects_)
            if (o.oldBackground == old) sorted.push_back(&o);
        std::ranges::stable_sort(sorted, [](const Object* a, const Object* b) { return a->s.layer != b->s.layer ? a->s.layer < b->s.layer : a->order < b->order; });
        for (const Object* o : sorted) drawObject(r, assets, font, *o, characterHeight, frameOf ? frameOf(o->s) : Frame{});
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
    if (assets.image(key)) {
        paint(assets, key, {}, [&](const Image& pic, float a) {
            const float w = pic.w(), h = pic.h();
            const float scale = std::max(canvas.w / w, canvas.h / h); // cover
            const Placed at = placed({screen.x + (canvas.w - w * scale) * 0.5f, screen.y + (canvas.h - h * scale) * 0.5f, w * scale, h * scale});
            wiped(at, [&] {
                pic.draw(r, at.box, at.alpha * a, at.look.rotation, &at.pivot, false, shade(at.look.colour.value_or(SDL_Color{255, 255, 255, 255}), true));
                if (at.look.fade > 0.0f) drawCovered(r, pic, at.box, at.look.rotation, at.pivot, false, shade(at.look.fadeColour), at.alpha * a * std::min(1.0f, at.look.fade));
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

    if (assets.image(key, frame)) {
        paint(assets, key, frame, [&](const Image& pic, float a) {
            const float scale = (character ? std::min(1.0f, canvas.h * characterHeight / pic.h()) : 1.0f) * s.zoom;
            const Placed at = placed(pic.w() * scale, pic.h() * scale);
            const float seen = alpha * a * std::clamp(at.look.opacity, 0.0f, 1.0f), turn = s.rotation + at.look.rotation;
            wiped(at, [&] {
                pic.draw(r, at.box, seen, turn, &at.pivot, s.flip, shade(at.look.colour.value_or(s.tint), true));
                if (at.look.fade > 0.0f) drawCovered(r, pic, at.box, turn, at.pivot, s.flip, shade(at.look.fadeColour), seen * std::min(1.0f, at.look.fade));
            });
        });
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

// ---------------------------------------------------------------------------
// The game
// ---------------------------------------------------------------------------

namespace {

constexpr int kInstructionBudget = 100000;
constexpr std::string_view kSaveMagic = "vnext-save 3";

// Saves keep one value per line: spaces, backslashes and newlines are escaped.
std::string escape(std::string_view s) {
    return encodeValue(std::string(s)).substr(2);
}

std::string unescape(std::string_view s) {
    return toString(decodeValue(std::format("s {}", s)));
}

// A text as one word of a save line (spaces and all; nothing: "~"), and back.
std::string field(std::string_view s) { return s.empty() ? std::string("~") : "=" + escape(s); }
std::string unfield(std::string_view s) { return s.size() < 2 || s.front() != '=' ? std::string{} : unescape(s.substr(1)); }

// An image on stage as a save has it ("object TAG char=... x=..."), and read back (after "object").
std::string objectLine(const Stage::ObjectState& o) {
    return std::format("object {} char={} image={} expr={} x={} y={} zoom={} alpha={} rotation={} flip={} layer={} tint=#{:02x}{:02x}{:02x}{}{}\n",
                       escape(o.tag), escape(o.character), escape(o.image), escape(o.expression), std::lround(o.x * 10000.0f),
                       std::lround(o.y * 10000.0f), std::lround(o.zoom * 10000.0f), std::lround(o.opacity * 10000.0f), std::lround(o.rotation * 100.0f),
                       o.flip ? 1 : 0, o.layer, o.tint.r, o.tint.g, o.tint.b, o.animation.empty() ? "" : " animation=" + escape(o.animation),
                       o.played.empty() ? "" : " played=" + escape(o.played));
}
Stage::ObjectState readObject(std::istream& ls) {
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
        else if (k == "rotation") o.rotation = n / 100.0f;
        else if (k == "flip") o.flip = n != 0.0f;
        else if (k == "layer") o.layer = static_cast<int>(n);
        else if (k == "tint") o.tint = parseColour(v).value_or(o.tint);
        else if (k == "animation") o.animation = v;
        else if (k == "played") o.played = v;
    }
    return o;
}

} // namespace

Game::Game(Context ctx, const Script& script)
    : ctx_(std::move(ctx)), script_(script), lookup_([this](std::string_view name) { return variable(name); }), stage_(ctx_.canvas) {}

void Game::start(std::size_t pc) {
    stage_.reset();
    vars_ = std::exchange(carried_, {});
    callStack_.clear();
    history_.clear();
    choiceItems_.clear();
    trigger_.reset();
    pass_.reset();
    asked_.reset();
    parked_.reset();
    chatOpen_.clear();
    putAway_.clear();
    hiddenLines_ = 0;
    others_.clear();
    together_ = false;
    text_.clear();
    screens_.clear();
    page_.clear();
    phone_ = {};
    call_.reset();
    chat_.clear();
    change_.reset();
    messageCount_ = arrivals_ = chatWait_ = 0;
    loops_.clear();
    expressions_.clear();
    error_.clear();
    music_.clear();
    flowchartNode_.clear();
    flowchartOption_ = -1;
    pendingVoice_.reset();
    past_.clear();
    ctx_.sound.stopAll(0.5f);
    if (gameSettings().time == TimeKind::Real && !vars_.contains("time_started")) vars_.insert_or_assign("time_started", realNow().date); // (its day 1)
    pc_ = resumePc_ = pc;
    wait_ = Wait::None;
    run();
}

bool Game::skipping() const {
    return skip_ || (gameSettings().holdToSkip != 0 && (SDL_GetModState() & gameSettings().holdToSkip) != 0);
}

std::string Game::summary() const {
    if (wait_ == Wait::Menu && menuPrompt_) return text_;
    if (history_.empty()) return {};
    const HistoryEntry& last = history_.back();
    return last.speaker.empty() ? last.text : std::format("{}: {}", last.speaker, last.text);
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
        if (--budget == 0) return fail("The story ran a very long time without showing anything (does a jump go round in a circle?)"_tr);
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

namespace {
// The computer's clock, as a phone shows it: "20:41".
std::string clockNow() {
    SDL_Time now = 0;
    SDL_DateTime t{};
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &t, true)) return {};
    return std::format("{}:{:02}", t.hour, t.minute);
}

} // namespace

Value Game::variable(std::string_view name) const {
    if (item_)
        if (const auto it = item_->find(name); it != item_->end()) return it->second;
    if (gameSettings().time != TimeKind::None) { // the time's values; the phone's clock and date (unless setting phone_follows_time no)
        if (name.starts_with("time_"))
            if (auto v = timeValue(name)) return *v;
        if (gameSettings().phoneFollowsTime && (name == "phone_clock" || name == "phone_date")) return *timeValue(name == "phone_clock" ? "time_clock" : "time_date");
    }
    const Value* kept = nullptr;
    if (const auto it = vars_.find(name); it != vars_.end()) kept = &it->second;
    else if (const auto p = ctx_.persistent.find(name); p != ctx_.persistent.end()) kept = &p->second;
    else if (const auto s = script_.startValues.find(name); s != script_.startValues.end()) kept = &s->second;
    if (name == "phone_clock" && (!kept || toString(*kept).empty())) return clockNow(); // (none: the computer's clock)
    if (kept) return *kept;
    if (ctx_.builtin)
        if (auto v = ctx_.builtin(name)) return *v;
    return 0;
}

std::string Game::typedValue(std::string_view name) const {
    const bool has = vars_.contains(name) || ctx_.persistent.contains(name) || script_.startValues.contains(name) || (item_ && item_->contains(name));
    return has ? toString(variable(name)) : std::string{};
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

// Starts a new line: fills in {expressions} and reads its text tags.
void Game::say(std::string speaker, SDL_Color colour, std::string_view text, const CharacterDef* def) {
    speaker_ = std::move(speaker);
    speakerKey_ = def ? def->key : std::string{};
    speakerColour_ = colour;
    voiced_ = madeUpVoice_ = textFollowsVoice_ = typing_ = false;
    lineTime_ = 0.0f;
    textColour_ = def ? def->textColour : std::nullopt;
    textbox_ = boxOf(def);
    source_ = fillValues(text, lookup_);
    typingSound_ = def ? def->typingSound : std::string{};
    revealMode_ = def && def->textAppears ? *def->textAppears : gameSettings().textAppears;
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
        else if (tag == "no_wait") noWait_ = true;
        else if (tag == "speed") speeds_.push_back({at, n ? std::max(0, static_cast<int>(*n)) : -1});
        else if (tag == "/speed") speeds_.push_back({at, -1});
        else if (tag == "appears") revealMode_ = lower(arg) == "line" ? TextAppears::Line : lower(arg) == "word" ? TextAppears::Word : TextAppears::Letter;
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
    return def && !def->textbox.empty() ? def->textbox : "textbox";
}

bool Game::othersTyping() const {
    return std::ranges::any_of(others_, [](const BoxLine& l) { return l.reveal < static_cast<float>(l.length); });
}

Game::BoxLine Game::boxLine(std::string box, std::string speaker, SDL_Color colour, std::string source, const CharacterDef* def) const {
    RichText rich = richText(source, lookup_, &script_.palette);
    BoxLine l{.box = std::move(box), .speaker = std::move(speaker), .speakerKey = def ? def->key : std::string{}, .source = std::move(source),
              .text = std::move(rich.text), .speakerColour = colour, .spans = std::move(rich.spans)};
    l.length = utf8::length(l.text);
    if (def && !def->face.empty()) {
        const auto it = expressions_.find(def->key);
        l.face = def->faceOf(it != expressions_.end() ? it->second : std::string{});
    }
    return l;
}

// A line of a together block, before its last: it shows now in its own textbox (typing, without a
// voice), and the story goes on to the next line.
void Game::sayTogether(const cmd::Say& c, const CharacterDef* def) {
    BoxLine l = boxLine(boxOf(def), def ? def->name : c.speaker, def ? def->nameColour : gameSettings().narrationColour, fillValues(c.text, lookup_), def);
    if (skipping() || ctx_.settings.textSpeed == 0) l.reveal = static_cast<float>(l.length);
    std::erase_if(others_, [&](const BoxLine& other) { return other.box == l.box; });
    together_ = true;
    learnTips(c.text);
    ctx_.read.insert(c.id);
    addHistory({.speaker = l.speaker, .colour = l.speakerColour, .text = l.text, .spans = l.spans});
    others_.push_back(std::move(l));
}

std::optional<Game::LineView> Game::line(std::string_view box) const {
    if (showingSay() && box == textbox_)
        return LineView{.speaker = speaker_, .text = text_, .face = face_, .faceBase = faceBase_, .faceSide = faceSide_, .speakerColour = speakerColour_,
                        .textColour = textColour_, .spans = &spans_, .revealed = revealed(), .limit = revealLimit(), .mode = revealMode_,
                        .complete = textComplete(), .atPause = atPause(), .character = speakerCharacter(), .faceFrame = faceFrame()};
    const auto it = std::ranges::find(others_, box, &BoxLine::box);
    if (it == others_.end()) return std::nullopt;
    const CharacterDef* def = script_.character(it->speakerKey);
    const bool typing = it->reveal < static_cast<float>(it->length);
    Frame face;
    if (def) {
        face.mouth = typing ? mouth(def->faceMouth, flapping(it->speakerKey + " face", clock_, def->mouthSpeed), false) : 0.0f; // (said together: no voice)
        if (def->faceBlink) face.eyes = blinking(it->speakerKey, clock_, def->secondsBetweenBlinks);
    }
    return LineView{.speaker = it->speaker, .text = it->text, .face = it->face, .faceBase = def ? std::string_view(def->face) : std::string_view{},
                    .faceSide = def && !def->face.empty() ? std::string_view(def->faceSide) : std::string_view{}, .speakerColour = it->speakerColour,
                    .textColour = def ? def->textColour : std::nullopt, .spans = &it->spans, .revealed = static_cast<std::size_t>(it->reveal),
                    .limit = it->length, .mode = def && def->textAppears ? *def->textAppears : gameSettings().textAppears, .complete = !typing, .character = def, .faceFrame = face};
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
    return cps < 0 ? ctx_.settings.textSpeed : cps;
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
    if (!typingSound_.empty() && ctx_.settings.typingSounds && !voiced_ && static_cast<std::size_t>(reveal_) > before && typingSoundTimer_ <= 0.0f && !skipping()) {
        ctx_.sound.playSound(ctx_.assets.sound(typingSound_), gameSettings().typingSoundVolume);
        typingSoundTimer_ = gameSettings().typingSoundGap;
    }
}

void Game::execute(const Command& command, std::size_t here) {
    const RandomScope _(random_); // (its random() numbers: from its own sequence)
    const bool fast = skipping();
    const bool instant = fast || !ctx_.settings.animations; // (an animation played: at its end at once; effects and weather: there at once)

    std::visit(
        Overloaded{
            [&](const cmd::Say& c) { sayLine(c, here, fast); },
            [&](const cmd::Background& c) { // a new background (what was on stage goes with the one before), or the one there played on
                have(RecordKind::Gallery, c.image);
                const std::string shown = backgroundFor(c.image); // (park_evening in the evening, when there's one)
                if (shown != stage_.background()) stage_.clearObjects();
                stage_.setBackground(shown, animation(c.with.value_or("")), instant);
                if (c.animation) stage_.animateBackground(animation(*c.animation), true, fast);
                if (stage_.backgroundComing()) wait_ = Wait::Background; // (the story goes on once it's in)
            },
            [&](const cmd::Show& c) { // (one that comes plays the game's animation_on_show, unless the line says)
                if (!c.image.empty()) have(RecordKind::Gallery, c.image);
                const CharacterDef* def = c.character.empty() ? nullptr : script_.character(c.character);
                const std::string played = c.with ? *c.with : stage_.has(c.tag) ? std::string{} : gameSettings().animationOnShow;
                stage_.show(c.tag, def, c.image, c.expression, c.props, animation(played), fast ? 0.0f : gameSettings().moveTime, instant);
                if (c.animation) stage_.animate(c.tag, animation(*c.animation), true, fast);
                if (def && def->faceCopies.expression) // their face looks as their image does (a new one starts as it is)
                    expressions_.insert_or_assign(def->key, stage_.expression(c.tag));
                if (c.wait && !fast && stage_.moving()) wait_ = Wait::Move;
            },
            [&](const cmd::Hide& c) { // (it plays the game's animation_on_hide, unless the line says; then it's gone)
                stage_.hide(c.tag, animation(c.with.value_or(gameSettings().animationOnHide)), instant);
                if (c.wait && !fast && stage_.moving()) wait_ = Wait::Move;
            },
            [&](const cmd::Chat& c) { // a chat scene: the phone comes up on its chat, until its end
                if (c.end) {
                    chat_.clear();
                    return requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "chat"});
                }
                chat_ = c.with;
                requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "chat", .phone = c.with});
            },
            [&](const cmd::Call& c) {
                if (!c.end) return startCall(c, here);
                if (!call_) return;
                logCall(*call_, false);
                call_.reset();
                stopRinging();
                requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "call"});
            },
            [&](const cmd::Effect& c) {
                if (fast && c.seconds > 0.0f) return; // skipping goes past a moment's effect
                stage_.setEffect(c, instant);
            },
            [&](const cmd::Menu& c) { openMenu(c, here); },
            [&](const cmd::Jump& c) {
                if (c.unless && c.unless->test(lookup_)) return;
                pc_ = c.target;
                choiceAgain_ = c.again;
            },
            [&](const cmd::Return&) {
                if (callStack_.empty()) { // (nothing started these lines, a chat= scene played on its own: the story's over, as at a scene's end)
                    wait_ = Wait::Finished;
                    return;
                }
                pc_ = callStack_.back();
                callStack_.pop_back();
                if (pass_ && callStack_.size() < pass_->depth) hiddenLines_ = std::exchange(pass_, std::nullopt)->lines; // a choice's return=no
                if (asked_ && callStack_.size() < asked_->depth) { // a chat or call scene's lines over (or waiting on the phone): the story's line again
                    const Asked a = *std::exchange(asked_, std::nullopt);
                    if (a.shown) sayAgain(a.page);
                    if (a.call && !parking_ && call_) callOver(); // (a call scene's end: the call's over)
                    if (!a.chat.empty() && !parking_) putAway_ = a.chat, putAwayIn_ = 2.0f; // (a chat scene's: the phone's put away once its messages have come)
                }
            },
            [&](const cmd::End&) { // a scene's end: back to what started it (a phone scene's), else the story's over
                if (!callStack_.empty()) return execute(cmd::Return{}, here);
                wait_ = Wait::Finished;
            },
            [&](const cmd::Set& c) {
                const Value old = variable(c.variable);
                const Value v = c.value ? c.value->eval(lookup_) : Value{0};
                if (c.op == '!') setVariable(c.variable, truthy(old) ? 0 : 1);
                else setVariable(c.variable, c.op == '=' ? v : combine(old, c.op, v));
                // An ending reached (set persistent.ending.NAME = yes) by the story's own line, not a button's: a point on the flowchart.
                if (c.variable.starts_with("persistent.ending.") && truthy(variable(c.variable)) && here < script_.code.size() && &command == &script_.code[here].command)
                    passPoint(here, Flowchart::Kind::Ending);
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
            [&](const cmd::Ui& c) { // open SCREEN: the story waits until the player closes it (unless it's story=goes_on)
                const ScreenDef* screen = c.verb == "open" ? script_.screen(c.arg) : nullptr;
                if (!screen || !screen->storyWaits) return requests_.push_back(c);
                vars_.insert_or_assign("answer", std::string{}); // until it answers
                waitForScreen({.name = c.arg, .opening = c.opening, .closing = c.closing}, here);
            },
            [&](const cmd::Time& c) { passTime(c, here); },
            [&](const cmd::Movie& c) { playMovie(c, here, fast); },
            [&](const cmd::Message& c) { sendMessage(c, here); }, // on the phone (a chat scene's: the story waits for it)
            [&](const cmd::Weather& c) { stage_.setWeather(c, instant ? 0.0f : c.fade); },
            [&](const cmd::Camera& c) {
                if (c.animation) stage_.animateCamera(animation(*c.animation), true, fast);
                if (c.with) stage_.animateCamera(animation(*c.with), false, instant);
                if (c.wait && !fast && stage_.moving()) wait_ = Wait::Move;
            },
        },
        command);
}

// A line said (or narrated): its speaker's face and textbox (the other textboxes' lines go, unless said together with it), its
// voice (a voice line just before it, their voice file, or made-up speech), the history, and the story
// waiting for a click.
void Game::sayLine(const cmd::Say& c, std::size_t here, bool fast) {
    const CharacterDef* def = c.character.empty() ? nullptr : script_.character(c.character);
    takeExpression(c, def);
    if (hiddenLines_ > 0) { // gone past unseen (a choice's return=no)
        --hiddenLines_;
        pendingVoice_.reset();
        return;
    }
    if (trigger_ && trigger_->lines-- <= 0) { // a choice up for some lines: its time is over
        trigger_.reset();
        choiceItems_.clear();
    }
    // Lines in other textboxes, said together with the last line: they go.
    if (!together_) others_.clear();
    if (c.together) return sayTogether(c, def);
    together_ = false;
    const std::string box = boxOf(def);
    std::erase_if(others_, [&](const BoxLine& l) { return l.box == box; });
    lineVoice_ = std::exchange(pendingVoice_, std::nullopt).value_or(LineVoice{}); // a voice line just before it
    bool voiced = !lineVoice_.file.empty();
    const std::optional<bool> follows = lineVoice_.textFollows;
    if (!voiced) ctx_.sound.stopVoice();
    say(def ? def->name : c.speaker, def ? def->nameColour : gameSettings().narrationColour, c.text, def);
    // Its voice (kept in the history to hear again): the voice line, else their own voice file
    // for this line (voice_files=), else made-up speech (made_up_voice=).
    HistoryEntry entry{.speaker = speaker_, .colour = speakerColour_, .text = text_, .spans = spans_};
    if (voiced) entry.voice = ctx_.assets.sound(lineVoice_.file), entry.voiceVolume = lineVoice_.volume;
    else if (def && !def->voiceFiles.empty())
        if (const auto file = ctx_.assets.findSound(std::format("{}/{}", def->voiceFiles, c.voiceId))) entry.voice = *file;
    if (!voiced && entry.voice.empty() && def && def->madeUpVoice > 0.0f && ctx_.settings.madeUpVoices) entry.madeUpVoice = def->madeUpVoice, entry.madeUpVoiceSpeed = def->madeUpVoiceSpeed;
    if (!voiced && !fast) {
        if (!entry.voice.empty()) ctx_.sound.playVoice(entry.voice), voiced = true;
        else if (entry.madeUpVoice > 0.0f) voiced = madeUpVoice_ = ctx_.sound.playMadeUpVoice(text_, entry.madeUpVoice, entry.madeUpVoiceSpeed);
    }
    voiced_ = voiced;
    textFollowsVoice_ = voiced && follows.value_or(gameSettings().textFollowsVoices);
    menuPrompt_ = false;
    saidAt_ = here;
    lineRead_ = !ctx_.read.insert(c.id).second;
    addToPage();
    addHistory(std::move(entry));
    learnTips(c.text);
    waitFor(Wait::Say, here);
    remember();
}

// Their face, and their image on stage if the face copies it.
void Game::takeExpression(const cmd::Say& c, const CharacterDef* def) {
    if (!def || c.expression.empty()) return;
    expressions_.insert_or_assign(def->key, c.expression);
    if (def->faceCopies.expression || def->face.empty()) stage_.setExpression(def->key, c.expression);
}

// The line the story was on, back as it was once a chat= or call= scene's lines that showed something are over: all there
// at once, without its voice, not in the history again, its page as it was. (A choice, a wait...: done again.)
void Game::sayAgain(std::size_t page) {
    const auto* c = pc_ < script_.code.size() ? std::get_if<cmd::Say>(&script_.code[pc_].command) : nullptr;
    if (!c || c->together) return; // (a line said with others: said again)
    const std::size_t here = current_ = pc_++;
    const CharacterDef* def = c->character.empty() ? nullptr : script_.character(c->character);
    takeExpression(*c, def);
    others_.clear();
    together_ = false;
    say(def ? def->name : c->speaker, def ? def->nameColour : gameSettings().narrationColour, c->text, def);
    reveal_ = static_cast<float>(length_);
    passedStops_ = stops_.size();
    noWait_ = false;
    lineVoice_ = {};
    menuPrompt_ = false;
    saidAt_ = here;
    page_.resize(std::min(page_.size(), page));
    waitFor(Wait::Say, here);
}

// A choice: its options (none of them pickable: the story goes on), up while the story goes on for some
// lines, or waited on with the line that asks it; and a point on the flowchart.
void Game::openMenu(const cmd::Menu& c, std::size_t here) {
    const bool again = std::exchange(choiceAgain_, false); // (a repeating one up again: without the line that asked it)
    if (!c.phone.empty() && asked_ && callStack_.size() == asked_->depth) { // a chat= or call= scene's choice: it waits on the phone (passed once the player hung up or left)
        bool any = false; // (an option that can be picked)
        for (std::size_t k = 0; k < c.choices.size() && !any; ++k)
            if (const auto item = offer(c.choices[k], static_cast<int>(k))) any = !item->locked;
        if (asked_->passing || !any) return void(pc_ = c.after); // (passed: the player hung up or left, or none can be picked)
        parked_ = Parked{.menu = here, .who = c.phone, .call = c.call};
        parking_ = true;
        execute(cmd::Return{}, here); // (the scene's lines over for now: the story goes on meanwhile)
        parking_ = false;
        return;
    }
    trigger_.reset();
    listChoices(c);
    if (std::ranges::all_of(choiceItems_, &ChoiceItem::locked)) {
        logWarn("{}: None of the options can be picked right now, so the story goes on.", script_.where(script_.code[here]));
        choiceItems_.clear();
        return;
    }
    for (const ChoiceItem& item : choiceItems_) learnTips(item.text);
    std::vector<Flowchart::Option> options; // as the flowchart shows it: picked or not, locked or not
    options.reserve(choiceItems_.size());
    for (const ChoiceItem& item : choiceItems_) options.push_back({item.option, !item.locked});
    if (c.lines > 0) { // up while the story goes on, for its next lines
        trigger_ = Trigger{.at = here, .lines = c.lines};
        pc_ = c.after;
        return passPoint(here, Flowchart::Kind::Choice, std::move(options));
    }
    menuTime_ = c.seconds;
    menuPrompt_ = c.phone.empty() && !text_.empty() && !again; // (the line before it stays on screen: it asks it; not when it's up again, nor a phone's)
    if (!ctx_.settings.skipAfterChoices) skip_ = false;
    autosave_ = true;
    waitFor(Wait::Menu, here);
    remember();
    passPoint(here, Flowchart::Kind::Choice, std::move(options));
}

// Music, a voice or a sound played, or stopped (a voice keeps playing through the next line; a sound on a
// channel of its own may loop).
void Game::play(const cmd::Play& c, bool fast) {
    const std::string file = c.file.contains('{') ? fillValues(c.file, lookup_) : c.file; // (a value's: sound {message.sound})
    const std::string path = file.empty() ? std::string{} : ctx_.assets.sound(file);
    switch (c.track) {
    case cmd::Play::Track::Music:
        music_ = file;
        musicVolume_ = c.volume;
        have(RecordKind::Song, file); // the music room
        if (file.empty()) ctx_.sound.stopMusic(c.fade);
        else ctx_.sound.playMusic(path, c.loop, c.fade, c.volume, c.fadeOut);
        break;
    case cmd::Play::Track::Voice:
        if (file.empty()) return ctx_.sound.stopVoice();
        if (!fast) ctx_.sound.playVoice(path, c.volume);
        pendingVoice_ = LineVoice{file, c.volume, c.textFollows}; // keep playing through the next line
        break;
    case cmd::Play::Track::Sound:
        if (file.empty()) { // stop sound [NAME]
            ctx_.sound.stopChannel(c.channel, c.fade);
            if (c.channel.empty()) loops_.clear();
            else loops_.erase(c.channel);
        } else if (c.channel.empty()) {
            if (!fast) ctx_.sound.playSound(path, c.volume, c.fade, c.fadeOut);
        } else {
            ctx_.sound.playChannel(c.channel, path, c.loop, c.volume, c.fade, c.fadeOut.value_or(0.0f));
            if (c.loop) loops_.insert_or_assign(c.channel, Loop{file, c.volume});
            else loops_.erase(c.channel);
        }
        break;
    }
}

// A movie, full screen: past it when skipping (if it can be skipped); one that can't play is said, and in
// developer mode a card with its name shows for a moment instead.
void Game::playMovie(const cmd::Movie& c, std::size_t here, bool fast) {
    have(RecordKind::Gallery, c.file); // a movie in the gallery
    const bool skippable = c.skippable && ctx_.settings.skipMovies; // (the player can say no too)
    const bool stopsHere = watchThere_ && (*watchThere_)(here); // (playTo() stops at one there: its first image)
    if ((fast && skippable) || (watch_ != SIZE_MAX && !stopsHere)) return; // skipping goes past movies, and so does playTo()
    const auto file = ctx_.assets.findMovie(c.file);
    movie_ = file ? std::make_unique<Movie>(ctx_.renderer, ctx_.sound, *file, c.loop, c.volume) : nullptr;
    if (!movie_ || !*movie_) {
        movie_.reset();
        logWarn("{}: {}", script_.where(script_.code[here]),
                file ? std::format("Can't play '{}'.", *file) : std::format("There's no movie called '{}'.", c.file));
        if (!gameSettings().developer) return; // players: the story goes on
        movieStandIn_ = c.file;    // a labelled card for a moment instead
        timer_ = 3.0f;
    }
    movieSkippable_ = skippable;
    ctx_.sound.stopVoice();
    waitFor(Wait::Movie, here);
}

// The language changed: the line, choices or question on screen again in the new one (the line
// types out again unless it was all there; its voice goes on).
void Game::retranslate() {
    if (wait_ != Wait::Say && wait_ != Wait::Menu) return;
    const Command& command = script_.code[resumePc_].command;
    auto again = [&](const std::string& speaker, SDL_Color colour, const std::string& text, const CharacterDef* def, bool whole) {
        const bool voiced = voiced_, madeUp = madeUpVoice_, follows = textFollowsVoice_;
        const float time = lineTime_;
        say(speaker, colour, text, def);
        voiced_ = voiced;
        madeUpVoice_ = madeUp;
        textFollowsVoice_ = follows;
        lineTime_ = time;
        if (whole) {
            reveal_ = static_cast<float>(length_);
            passedStops_ = stops_.size();
        }
        if (!page_.empty()) page_.back() = {speaker_, speakerColour_, text_, spans_, textColour_};
    };
    if (const auto* c = std::get_if<cmd::Say>(&command)) {
        const CharacterDef* def = c->character.empty() ? nullptr : script_.character(c->character);
        again(def ? def->name : c->speaker, def ? def->nameColour : gameSettings().narrationColour, c->text, def, textComplete() && !pendingStop());
        if (!history_.empty()) {
            HistoryEntry& h = history_.back();
            h.speaker = speaker_;
            h.colour = speakerColour_;
            h.text = text_;
            h.spans = spans_;
        }
    } else if (const auto* m = std::get_if<cmd::Menu>(&command)) {
        listChoices(*m);
    }
}

void Game::addToPage() {
    page_.push_back({speaker_, speakerColour_, text_, spans_, textColour_});
    if (page_.size() > 60) page_.erase(page_.begin());
}

std::shared_ptr<const Animation> Game::animation(std::string_view name) const {
    if (name.empty()) return nullptr;
    const auto it = script_.animations.find(name);
    return it != script_.animations.end() ? std::make_shared<const Animation>(it->second) : nullptr;
}

void Game::perform(const Command& command) {
    if (const auto* jump = std::get_if<cmd::Jump>(&command)) {
        const auto pc = script_.findScene(jump->scene);
        if (!pc) return;
        choiceItems_.clear();
        menuPrompt_ = false;
        pc_ = *pc;
        wait_ = Wait::None;
        return run();
    }
    execute(command, current_);
}

void Game::nextLine() {
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
            if (madeUpVoice_) ctx_.sound.stopVoice(); // (made-up speech is the typing's: shown at once, it's done)
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
        ctx_.sound.stopVoice(); // clicked past: its voice goes with it (not on over what comes next)
        break;
    }
    case Wait::Background:
    case Wait::Move:
        stage_.finish();
        break;
    case Wait::Timer:
        if (!chat_.empty()) return; // (a chat scene's wait holds: it's the chat's own timing)
        break;
    case Wait::Pause:
    case Wait::Reached: // (played to a line by the developer tools: on from where it stopped)
        break;
    case Wait::Chat: // (a click hurries a chat scene's message, or its change)
        if (change_) change(*std::exchange(change_, std::nullopt));
        else hurry();
        break;
    default:
        return;
    }
    wait_ = Wait::None;
    run();
}

// The message a chat scene waits for comes now (someone just typing stops).
void Game::hurry() {
    const auto it = std::ranges::find(phone_.messages, chatWait_, &MessageItem::number);
    if (it == phone_.messages.end() || it->typing <= 0.0f) return;
    it->typing = 0.0f;
    if (it->kind == MessageItem::Kind::Typing) phone_.messages.erase(it);
    else arrive(*it);
}

bool Game::playTo(std::size_t target, const std::function<bool(std::size_t)>& there) {
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
                nextLine();
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
                if (wait_ == Wait::Movie) movie_.reset();
                request_ = {};
                pc_ = target;
                wait_ = Wait::None;
                run();
                break;
            }
            if (there(resumePc_)) { // (what the player sees at that line: a movie's first image)
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
        case Wait::Chat: // a chat scene's message coming: there at once (its line may be the one)
            if (there(current_)) {
                if (change_) change(*std::exchange(change_, std::nullopt));
                else hurry();
                return true;
            }
            nextLine();
            break;
        case Wait::Call: { // a call ringing: there (its line may be the one); else answered, or missed for a line of its missed:
            if (there(current_)) return true;
            const cmd::Call* c = call_ && call_->at ? std::get_if<cmd::Call>(&script_.code[*call_->at].command) : nullptr;
            if (c && target >= c->missed && target < c->after) {
                stopRinging();
                endCall(true);
            } else if (call_) {
                answerCall(true);
            } else {
                return false;
            }
            break;
        }
        case Wait::Timer: // a wait: over at once (a chat scene's too, which a click doesn't end)
            wait_ = Wait::None;
            run();
            break;
        case Wait::Clock: // a wait for the player's clock: over at once
            vars_.erase("time_wait");
            vars_.erase("time_wait_line");
            text_.clear();
            wait_ = Wait::None;
            run();
            break;
        case Wait::Pause: // a click's wait (a chat scene's message after a click): there, else on
            if (there(current_)) return true;
            nextLine();
            break;
        default: // an animation: over at once
            nextLine();
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
    ctx_.sound.pauseVoice(covered);
    if (movie_) movie_->setPaused(covered);
}

Variables Game::savedVariables(std::string_view state) {
    Variables vars;
    std::istringstream in{std::string(state)};
    for (std::string line; std::getline(in, line);)
        if (line.starts_with("variable ")) {
            std::istringstream ls(line.substr(9));
            std::string name, value;
            ls >> name;
            std::getline(ls >> std::ws, value);
            vars.insert_or_assign(name, decodeValue(value));
        }
    return vars;
}

// The player is at a point on the flowchart (a choice or an ending): it
// remembers the game here, and which way the player came.
void Game::passPoint(std::size_t here, Flowchart::Kind kind, std::vector<Flowchart::Option> options) {
    const auto [label, offset] = script_.locate(here);
    const std::string id = std::format("{}:{}", label.empty() ? "-" : label, offset);
    const std::size_t resume = std::exchange(resumePc_, here); // the game here repeats this command
    std::string state = save();
    resumePc_ = resume;
    ctx_.flowchart.visit(id, kind, flowchartNode_, flowchartOption_, std::move(state), std::move(options));
    flowchartNode_ = id;
    flowchartOption_ = -1;
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

// ---------------------------------------------------------------------------
// The phone
// ---------------------------------------------------------------------------

namespace {
constexpr SDL_Color kOwnColour{230, 230, 235, 255}; // (the phone owner's own messages)
constexpr std::string_view kRinging = "phone ringing"; // (the sound channel a call rings on)
} // namespace

Game::Person Game::person(const std::string& character, const std::string& sender) const {
    if (const CharacterDef* def = character.empty() ? nullptr : script_.character(character)) return {.name = def->name, .character = def->key, .colour = def->nameColour};
    return {.name = sender, .colour = gameSettings().narrationColour};
}

Game::Person Game::personNamed(std::string_view name) const {
    for (const auto& [key, def] : script_.characters)
        if (def.name == name) return {.name = def.name, .character = key, .colour = def.nameColour};
    return {.name = std::string(name), .colour = gameSettings().narrationColour};
}

// A chat scene's message on the phone, or a change to one: it comes at once, or typed first (as setting
// chat_messages says), and the scene waits for it: until it comes, or for a click once it's there.
void Game::sendMessage(const cmd::Message& c, std::size_t here) {
    using Kind = cmd::Message::Kind;
    if (c.kind == Kind::End) return change(c); // (the scene's over: its line at once)
    const bool scene = !skipping() && !chat_.empty();
    const bool typed = gameSettings().chatMessages == ChatMessages::Typed;
    if (!isMessage(c.kind) && c.kind != Kind::Typing) { // (a change to one)
        if (scene && typed) { // (after a moment, as if they thought it over)
            change_ = c;
            changeIn_ = 1.0f;
            return waitFor(Wait::Chat, here);
        }
        change(c);
        if (scene) { // (each after a click)
            autoTimer_ = 0.0f;
            wait_ = Wait::Pause;
        }
        return;
    }
    if (c.kind == Kind::Typing && skipping()) return; // (skipping: nobody waits for it)
    const Person who = person(c.character, c.sender); // (the player's own: who it goes to)
    const std::string& with = who.name;
    const std::string from = c.mine ? std::string{} : who.name;
    MessageItem m{.kind = c.kind, .from = from, .with = with, .character = c.mine ? std::string{} : who.character, .expression = c.expression,
                  .colour = c.mine ? kOwnColour : who.colour, .text = fillValues(c.text, lookup_), .file = c.file, .read = c.mine};
    // How long it takes: someone typing a while; someone else's (as long as typing it takes, after their
    // others); the player's own a moment; skipping, no time at all. A chat= scene's messages, sent at once, come
    // one after another: each after the ones still coming there.
    float after = 0.0f;
    if (replying_ && !skipping())
        for (const MessageItem& other : phone_.messages)
            if (other.with == with) after = std::max(after, other.typing);
    if (c.kind == Kind::Typing) {
        m.typing = after + (c.seconds > 0.0f ? c.seconds : 2.0f);
    } else if (!c.mine && ((scene && typed) || (replying_ && !skipping())) && gameSettings().phoneTyping > 0.0f) {
        for (const MessageItem& other : phone_.messages)
            if (other.with == with && other.from == from) m.typing = std::max(m.typing, other.typing);
        const std::size_t letters = c.kind == Kind::Text ? utf8::length(m.text) : 12; // (an image: as long as a few words)
        m.typing = std::max(m.typing, after) + 0.8f + static_cast<float>(letters) / gameSettings().phoneTyping * static_cast<float>(randomNumber(75, 125)) / 100.0f;
    } else if (c.mine && ((scene && typed) || (replying_ && !skipping()))) {
        m.typing = after + 0.4f;
    }
    const std::uint32_t number = messageCount_ + 1;
    addMessage(std::move(m));
    if (!scene) return;
    if (const MessageItem& added = phone_.messages.back(); added.number == number && added.typing > 0.0f) { // a chat scene: until it comes...
        chatWait_ = number;
        wait_ = Wait::Chat; // (a save made meanwhile goes on after it: the phone has it, coming)
    } else if (!typed) { // ...or, each after a click, until one
        autoTimer_ = 0.0f;
        wait_ = Wait::Pause;
    }
}

// Deletes it, edits it, reacts, reads it: a change to a chat's messages.
void Game::change(const cmd::Message& c) {
    using Kind = cmd::Message::Kind;
    const Person who = person(c.character, c.sender);
    const std::string& with = who.name;
    const std::string from = c.mine ? std::string{} : who.name;
    auto last = [&](auto&& which) -> MessageItem* { // (the newest there that has come, and isn't deleted)
        for (auto it = phone_.messages.rbegin(); it != phone_.messages.rend(); ++it)
            if (it->with == with && it->typing <= 0.0f && isMessage(it->kind) && !it->deleted && which(*it)) return &*it;
        return nullptr;
    };
    switch (c.kind) {
    case Kind::Delete:
        if (MessageItem* m = last([&](const MessageItem& item) { return item.from == from; })) m->deleted = true;
        break;
    case Kind::Edit:
        if (MessageItem* m = last([&](const MessageItem& item) { return item.from == from && item.kind == Kind::Text; })) {
            m->text = fillValues(c.text, lookup_);
            m->edited = true;
        }
        break;
    case Kind::React: // (on the newest someone else sent)
        if (MessageItem* m = last([&](const MessageItem& item) { return item.from != from; })) m->reaction = fillValues(c.text, lookup_);
        break;
    case Kind::Read: // someone has read the owner's messages there (the owner reading theirs: they're seen)
        for (MessageItem& m : phone_.messages)
            if (m.with == with && m.typing <= 0.0f) {
                if (!from.empty() && m.from.empty()) m.seen = true;
                else if (from.empty() && !m.from.empty()) m.read = true;
            }
        break;
    case Kind::End: // (a chat scene over: a line saying so)
        addMessage({.kind = c.kind, .with = with, .read = true});
        break;
    default:
        break;
    }
}

void Game::addMessage(MessageItem m) {
    m.number = ++messageCount_;
    MessageItem& added = phone_.messages.emplace_back(std::move(m));
    if (added.typing <= 0.0f) arrive(added);
    else if (!added.from.empty() && !skipping() && hasPhone() && !gameSettings().phoneTypingSound.empty())
        ctx_.sound.playSound(ctx_.assets.sound(gameSettings().phoneTypingSound));
}

// A message comes to the phone: its tips are learned, an image goes in the photos (and the gallery), its sound
// plays, the history has it (setting phone_history), and the player hears of it as setting new_messages says (not
// of a chat scene's: it's on the phone already, nor of the owner's own).
void Game::arrive(MessageItem& m) {
    m.time = toString(variable("phone_clock"));
    m.date = toString(variable("phone_date"));
    if (!isMessage(m.kind)) return;
    learnTips(m.text);
    if (m.kind == MessageItem::Kind::Image) have(RecordKind::Gallery, m.file);
    const bool fast = skipping();
    if (gameSettings().phoneHistory && !unlogged_) { // (as a line, the owner's own as a choice made)
        const std::string words = m.text.empty() ? m.file : richText(m.text, lookup_, &script_.palette).text;
        if (m.from.empty()) addHistory({.speaker = "", .colour = SDL_Color{150, 150, 165, 255}, .text = "> " + words, .choice = true});
        else addHistory({.speaker = m.from, .colour = m.colour, .text = words});
    }
    if (m.from.empty()) {
        if (!fast && !gameSettings().phoneSendSound.empty()) ctx_.sound.playSound(ctx_.assets.sound(gameSettings().phoneSendSound));
        return;
    }
    ++arrivals_;
    if (!hasPhone()) return; // (no phone to come to)
    if (!fast && !gameSettings().phoneMessageSound.empty()) ctx_.sound.playSound(ctx_.assets.sound(gameSettings().phoneMessageSound));
    if (m.with == chat_) return; // (a chat scene's: on the phone already)
    if (!fast) requests_.push_back(cmd::Ui{.verb = "buzz"}); // (controllers buzz, as a phone does)
    if (gameSettings().newMessages == NewMessages::Open && !fast) // (the engine opens the phone on it, unless it's up already; not while skipping past)
        requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "message", .phone = m.with}); // (button: the Phone button counts it)
}

namespace {
// Someone's character's chat= or call= scene: where its lines start.
std::optional<std::size_t> phoneSceneOf(const Script& script, std::string_view who, bool call) {
    for (const auto& [key, def] : script.characters) {
        if (def.name != who) continue;
        const auto it = script.phoneScenes.find(call ? def.callScene : def.chatScene);
        return it != script.phoneScenes.end() ? std::optional(it->second) : std::nullopt;
    }
    return std::nullopt;
}
} // namespace

void Game::updatePhone(float dt) {
    for (std::size_t i = 0; i < phone_.messages.size(); ++i) {
        MessageItem& m = phone_.messages[i];
        if (m.typing <= 0.0f || (m.typing -= dt) > 0.0f) continue;
        m.typing = 0.0f;
        if (m.kind == MessageItem::Kind::Typing) phone_.messages.erase(phone_.messages.begin() + static_cast<std::ptrdiff_t>(i--)); // (they stopped)
        else arrive(m);
    }
    if (!putAway_.empty()) { // (a chat= scene over: once its messages have come, and the last could be read)
        if (std::ranges::any_of(phone_.messages, [&](const MessageItem& m) { return m.with == putAway_ && m.typing > 0.0f; })) putAwayIn_ = 2.0f;
        else if ((putAwayIn_ -= dt) <= 0.0f) {
            putAway_.clear();
            requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "chat"});
        }
    }
    if (!call_) return;
    call_->time += dt;
    if (call_->talking) return;
    // Ringing: one coming rings until its time is over (then it's missed); the player's rings a moment, and is
    // declined. A declined one (either) says Busy a moment first.
    if (call_->busy) { // (declined: Busy a moment, then it's over)
        if (call_->time >= 1.5f) endCall(false);
        return;
    }
    if (!call_->outgoing) {
        if (call_->rings > 0.0f && call_->time >= call_->rings) {
            stopRinging();
            endCall(true);
        }
        return;
    }
    if (call_->time < 2.5f) return;
    stopRinging();
    call_->time = 0.0f;
    if (!call_->talks) { // (no call scene: they decline it)
        call_->busy = true;
        return;
    }
    call_->talking = true; // (they answer: their call scene plays)
    if (const auto at = phoneSceneOf(script_, call_->who, true)) talk(*at, {}, true);
}

// The options of the choice waiting on the phone, if it's this chat's or call's.
std::vector<ChoiceItem> Game::phoneChoices(std::string_view who, bool call) const {
    std::vector<ChoiceItem> out;
    const auto* m = parked_ && parked_->who == who && parked_->call == call ? std::get_if<cmd::Menu>(&script_.code[parked_->menu].command) : nullptr;
    for (std::size_t k = 0; m && k < m->choices.size(); ++k)
        if (auto item = offer(m->choices[k], static_cast<int>(k))) out.push_back(std::move(*item));
    return out;
}

// The player picks an option of the choice waiting on the phone: in a chat it's sent there as theirs (on a call, the
// history has it, as a choice's), and the scene goes on with its lines.
void Game::pickOnPhone(std::size_t option) {
    const auto* m = parked_ && !asking() ? std::get_if<cmd::Menu>(&script_.code[parked_->menu].command) : nullptr;
    if (!m || option >= m->choices.size() || !hasSignal()) return;
    const cmd::Choice& picked = m->choices[option];
    if (picked.condition && !picked.condition->test(lookup_)) return;
    ctx_.read.insert(picked.id); // (picked before, from now on)
    const Parked p = *std::exchange(parked_, std::nullopt);
    if (p.call) {
        RichText chosen = richText(picked.text, lookup_, &script_.palette);
        for (TextSpan& span : chosen.spans) span.begin += 2, span.end += 2; // after "> "
        addHistory({.speaker = "", .colour = SDL_Color{150, 150, 165, 255}, .text = "> " + chosen.text, .choice = true, .spans = std::move(chosen.spans)});
    } else {
        addMessage({.with = p.who, .colour = kOwnColour, .text = fillValues(picked.text, lookup_), .read = true});
    }
    talk(picked.target, p.call ? std::string{} : p.who, p.call);
}

// Lines of someone's chat= or call= scene, run aside: the story stays on its line, shown again after them if they
// showed something (a call scene's end ends the call; a choice waits on the phone).
void Game::talk(std::size_t target, const std::string& chat, bool call, bool passing) {
    asked_ = Asked{.depth = callStack_.size() + 1, .page = page_.size(), .call = call, .passing = passing, .chat = chat}; // (runAside's own frame: while it's there, they run)
    putAway_.clear();
    replying_ = !chat.empty(); // (a chat's: messages typed one after another)
    runAside(target);
    replying_ = false;
    if (asking()) asked_->shown = true; // (still under way: they show something)
    else asked_.reset();
}

// The player hangs up: the rest of their call scene first (its choices passed), then the call's over.
void Game::hangUp() {
    if (!call_ || !call_->outgoing || !call_->talking || asking()) return;
    const auto* m = parked_ && parked_->call ? std::get_if<cmd::Menu>(&script_.code[parked_->menu].command) : nullptr;
    parked_.reset();
    if (m) talk(m->after, {}, true, true);
    else callOver();
}

void Game::callOver() {
    logCall(*call_, false);
    call_.reset();
    if (parked_ && parked_->call) parked_.reset();
    requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "call"});
}

// The player's chat open on the phone now (none: ""): its chat= scene plays as it's opened; left at a choice, the rest
// of it (its choices passed). Not while a chat scene or a call is on (its choice waiting is dropped), nor without signal.
void Game::chatOpen(const std::string& with) {
    const std::string now = chat_.empty() && !call_ ? with : std::string{};
    if (now == chatOpen_) return;
    const bool can = hasPhone() && hasSignal() && chat_.empty() && !call_;
    if (parked_ && !parked_->call && parked_->who == chatOpen_) {
        const auto* m = std::get_if<cmd::Menu>(&script_.code[parked_->menu].command);
        parked_.reset();
        if (m && can && !asking()) talk(m->after, chatOpen_, false, true);
    }
    chatOpen_ = now;
    if (const auto at = now.empty() || !can || asking() ? std::nullopt : phoneSceneOf(script_, now, false)) talk(*at, now, false);
}

// A call: the phone comes up on it, ringing, until it's answered or declined (or its time is over); the
// player's is answered after a moment, if it has lines. No phone, or no signal: nobody gets through.
void Game::startCall(const cmd::Call& c, std::size_t here) {
    const Person who = person(c.character, c.sender);
    const CallNow now{.who = who.name, .character = who.character, .colour = who.colour, .rings = c.seconds, .at = here};
    if (!hasPhone() || !hasSignal()) {
        logCall(now, true);
        pc_ = c.missed;
        return;
    }
    call_ = now;
    requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "call"});
    if (skipping()) skip_ = false; // (one to answer stops skipping, as a choice does)
    const CharacterDef* def = who.character.empty() ? nullptr : script_.character(who.character);
    const std::string& tone = def && !def->ringtone.empty() ? def->ringtone : gameSettings().phoneRingtone;
    if (!tone.empty()) ctx_.sound.playChannel(std::string(kRinging), ctx_.assets.sound(tone), true, 1.0f, 0.0f);
    waitFor(Wait::Call, here); // (a save made meanwhile rings again)
}

void Game::answerCall(bool answer) {
    if (!call_ || call_->talking || call_->busy || call_->outgoing) return;
    stopRinging();
    if (!answer) { // (declined: Busy a moment, then it's over)
        call_->busy = true;
        call_->time = 0.0f;
        return;
    }
    const cmd::Call* c = call_->at ? std::get_if<cmd::Call>(&script_.code[*call_->at].command) : nullptr;
    call_->talking = true;
    call_->time = 0.0f;
    if (c && wait_ == Wait::Call) {
        wait_ = Wait::None;
        pc_ = *call_->at + 1;
        run();
    }
}

// A call over without being answered: missed (it rang out) or busy (declined); a story's goes on to its missed: lines.
void Game::endCall(bool missed) {
    const cmd::Call* c = call_->at ? std::get_if<cmd::Call>(&script_.code[*call_->at].command) : nullptr;
    const bool story = c && wait_ == Wait::Call;
    logCall(*call_, missed);
    call_.reset();
    requests_.push_back(cmd::Ui{.verb = "hide phone", .arg = "call"});
    if (story) {
        wait_ = Wait::None;
        pc_ = c->missed;
        run();
    }
}

void Game::logCall(const CallNow& c, bool missed) {
    phone_.calls.push_back({.who = c.who, .colour = c.colour, .outgoing = c.outgoing, .missed = missed, .busy = c.busy, .seen = !missed || c.outgoing,
                                .time = toString(variable("phone_clock")), .date = toString(variable("phone_date")), .seconds = missed || c.busy ? 0 : static_cast<int>(std::lround(c.time))});
}

void Game::stopRinging() { ctx_.sound.stopChannel(std::string(kRinging), 0.1f); }

void Game::callPerson(std::string_view who) {
    if (call_ || !chat_.empty() || !hasPhone() || !hasSignal()) return;
    const Person p = personNamed(who); // (it rings a while, then it's answered if they have a call scene, else busy)
    call_ = CallNow{.who = p.name, .character = p.character, .colour = p.colour, .outgoing = true, .talks = phoneSceneOf(script_, p.name, true).has_value()};
    if (!gameSettings().phoneCallingSound.empty()) ctx_.sound.playChannel(std::string(kRinging), ctx_.assets.sound(gameSettings().phoneCallingSound), true, 1.0f, 0.0f);
    requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "call"}); // (on the call, from a key or a button outside it too)
}

bool Game::choiceOnPhone() const {
    const cmd::Menu* m = wait_ == Wait::Menu ? menu() : nullptr;
    return m && !m->phone.empty();
}

// Lines from `target` (a chat= or call= scene's), run as if called from where the story is: lines that show nothing
// (set, a message, a sound...) leave it there; ones that do play, and the story comes back to its line after them.
void Game::runAside(std::size_t target) {
    const Wait was = wait_;
    const std::size_t pc = pc_, resume = resumePc_, current = current_;
    callStack_.push_back(resumePc_);
    const std::size_t depth = callStack_.size();
    pc_ = target;
    wait_ = Wait::None;
    for (int budget = kInstructionBudget; wait_ == Wait::None && callStack_.size() >= depth;) {
        if (pc_ >= script_.code.size()) wait_ = Wait::Finished;
        else if (--budget == 0) fail("A phone scene's lines ran a very long time without showing anything (does a jump go round in a circle?)"_tr);
        else {
            const std::size_t here = current_ = pc_++;
            resumePc_ = pc_;
            execute(script_.code[here].command, here);
        }
    }
    if (wait_ != Wait::None) return;
    wait_ = was;
    pc_ = pc;
    resumePc_ = resume;
    current_ = current;
}

void Game::readMessages(std::string_view with) {
    for (MessageItem& m : phone_.messages)
        if (with.empty() || m.with == with) m.read = m.read || m.typing <= 0.0f;
}

std::vector<Game::Chat> Game::chats() const {
    std::vector<Chat> out;
    auto entry = [&](const std::string& name) -> Chat& {
        if (const auto it = std::ranges::find(out, name, &Chat::name); it != out.end()) return *it;
        const Person p = personNamed(name);
        return out.emplace_back(Chat{.name = name, .colour = p.colour, .character = p.character});
    };
    for (std::size_t i = 0; i < phone_.messages.size(); ++i) {
        const MessageItem& m = phone_.messages[i];
        Chat& c = entry(m.with);
        if (m.typing > 0.0f) c.typing = c.typing || !m.from.empty();
        else if (m.kind != MessageItem::Kind::End) c.last = i; // (a chat scene's end isn't its newest message)
        if (!m.read && m.typing <= 0.0f && !m.from.empty() && isMessage(m.kind)) ++c.unread;
    }
    std::ranges::stable_sort(out, std::greater{}, [](const Chat& c) { return c.last ? static_cast<long long>(*c.last) : -1LL; });
    return out;
}

void Game::seeCalls() {
    for (CallItem& c : phone_.calls) c.seen = true;
}

int Game::unseenMessages() const {
    return static_cast<int>(std::ranges::count_if(phone_.messages, [](const MessageItem& m) { return !m.read && m.typing <= 0.0f && !m.from.empty() && isMessage(m.kind); }));
}

int Game::unseenCalls() const { return static_cast<int>(std::ranges::count(phone_.calls, false, &CallItem::seen)); }

std::string Game::expressionOf(std::string_view character) const {
    const auto it = expressions_.find(character);
    return it != expressions_.end() ? it->second : std::string{};
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
    flowchartOption_ = choiceItems_[index].option;
    RichText chosen = richText(choiceItems_[index].text, lookup_, &script_.palette);
    for (TextSpan& span : chosen.spans) span.begin += 2, span.end += 2; // after "> "
    addHistory({.speaker = "", .colour = SDL_Color{150, 150, 165, 255}, .text = "> " + chosen.text, .choice = true, .spans = std::move(chosen.spans)});
    if (!m->phone.empty()) { // a chat's choice: the option picked is sent there, as the player's (the history has it already, as a choice)
        unlogged_ = true;
        addMessage({.with = m->phone, .colour = kOwnColour, .text = fillValues(picked.text, lookup_), .read = true});
        unlogged_ = false;
    }
    if (wait_ != Wait::Menu) { // one up while the story goes on: its lines, then back where the story was (or past its lines)
        callStack_.push_back(wait_ == Wait::Say ? pc_ : resumePc_);
        if (!m->returns) pass_ = Pass{.depth = callStack_.size(), .lines = trigger_->lines};
        trigger_.reset();
        ctx_.sound.stopVoice();
    }
    pc_ = picked.target;
    choiceItems_.clear();
    menuPrompt_ = false;
    wait_ = Wait::None;
    run();
}

void Game::have(RecordKind kind, std::string_view name) {
    if (!script_.record(kind, name)) return;
    if (const std::string var = Script::recordVariable(kind, name); !truthy(variable(var))) setVariable(var, 1); // (a tip read stays read: 2)
}

// The tips a text shows ({tip NAME}...{/tip}): learned when it's said.
void Game::learnTips(std::string_view text) {
    (void)expand(text, lookup_, [&](std::string_view tag, std::string_view name, std::size_t) {
        if (tag == "tip") have(RecordKind::Tip, name);
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
    typingSoundTimer_ -= dt;
    clock_ += dt;
    lineTime_ += dt;
    typing_ = false;
    const float cps = static_cast<float>(ctx_.settings.textSpeed);
    for (BoxLine& l : others_) // lines in other textboxes type on, each at its textbox's speed
        l.reveal = fast || cps <= 0.0f ? static_cast<float>(l.length) : std::min(static_cast<float>(l.length), l.reveal + cps * dt);

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
    case Wait::Clock: // the player's clock gets there (setting time real): the story goes on
        if (const auto until = vars_.find("time_wait"); until == vars_.end() || realNow().seconds >= toNumber(until->second)) {
            vars_.erase("time_wait");
            vars_.erase("time_wait_line");
            text_.clear();
            wait_ = Wait::None;
            timeMoved(waitFrom_, timeNow(), false);
            if (wait_ == Wait::None) run();
        }
        break;
    case Wait::Pause: // until a click, or auto-forward's delay
        if (fast || (auto_ && (autoTimer_ += dt) >= static_cast<float>(ctx_.settings.autoDelay) / 1000.0f)) nextLine();
        break;
    case Wait::Movie:
        if (movie_) movie_->update(dt);
        if ((movie_ ? movie_->ended() : (timer_ -= dt) <= 0.0f) || (fast && movieSkippable_)) endMovie();
        break;
    case Wait::Menu:
        if (menu()->seconds > 0.0f && (menuTime_ -= dt) <= 0.0f) letPass(); // time's up
        break;
    case Wait::Chat: // a chat scene: until the message comes (skipping: at once), or the change is made
        if (change_) {
            if ((changeIn_ -= dt) > 0.0f && !fast) break;
            change(*std::exchange(change_, std::nullopt));
        } else if (const auto& messages = phone_.messages; !fast && std::ranges::any_of(messages, [&](const MessageItem& m) {
                       return m.number == chatWait_ && m.typing > 0.0f;
                   })) {
            break;
        } else if (fast) {
            hurry();
        }
        wait_ = Wait::None;
        run();
        break;
    case Wait::Call: // (updatePhone() rings it)
    case Wait::Screen:
    case Wait::Reached: // (stopBefore: it's saved from there, not played on)
    case Wait::Stopped: // (stopWhere: until goOn())
    case Wait::Finished:
        break;
    }
    ctx_.sound.holdVoice(voiceAtPause());
    updateSpeech();
}

// A line that follows its voice waits at a {pause} for a click, and so does the voice, once it has
// said the words before it: in the silence there (the one nearest to where the {pause} is in the
// text), else right at that place. A click that shows the text early lets the voice catch up first.
bool Game::voiceAtPause() const {
    if (wait_ != Wait::Say || !textFollowsVoice_) return false;
    const Stop* stop = pendingStop();
    if (!stop || stop->seconds >= 0.0f) return false;
    const auto said = ctx_.sound.voiceProgress();
    if (!said) return false;
    const float at = static_cast<float>(stop->at) / static_cast<float>(std::max<std::size_t>(length_, 1));
    if (const auto silence = ctx_.sound.voiceSilenceNear(at)) return *said >= *silence && ctx_.sound.voiceSilent();
    return *said >= at;
}

// The line on screen: types out (or follows its voice), holds at {pause}s, and goes on by itself
// when skipping, on auto-forward or with {no_wait}.
void Game::updateLine(float dt, bool fast) {
    const auto length = static_cast<float>(length_);
    const Settings& s = ctx_.settings;
    const bool skipLine = fast && (lineRead_ || s.skipUnread);
    if (fast && !skipLine) skip_ = false; // reached text the player hasn't read yet
    if (skipLine) {
        reveal_ = length;
        passedStops_ = stops_.size();
        if ((skipTimer_ += dt) >= gameSettings().skipTime) {
            skipTimer_ = 0.0f;
            nextLine();
        }
        return;
    }
    // A line that follows its voice shows as much as has been said; timed pauses don't apply.
    std::optional<float> said;
    if (textFollowsVoice_) {
        said = ctx_.sound.voiceProgress();
        if (!said) textFollowsVoice_ = false; // the voice ended (or can't play): the rest types as usual
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
            nextLine();
        }
        return;
    }
    const float limit = stop ? static_cast<float>(stop->at) : length;
    float next = limit;
    if (said) next = voiceAtPause() ? limit : std::min(limit, std::max(reveal_, *said * length)); // (the voice waits at a {pause}: all of it)
    else if (const int cps = currentSpeed(); cps > 0) next = std::min(limit, reveal_ + static_cast<float>(cps) * dt);
    if (static_cast<std::size_t>(next) > static_cast<std::size_t>(reveal_)) revealTo(static_cast<std::size_t>(next)); // (its typing sound)
    reveal_ = next;
    typing_ = reveal_ < limit;
    if (reveal_ < length || pendingStop() || othersTyping()) return; // (lines said together with it still typing too)
    // {no_wait} and auto-forward wait for the voice line to end; reading time per letter is only
    // added to lines without a voice (the voice gave the player that time).
    const bool talking = voiced_ && ctx_.sound.voicePlaying();
    if (noWait_) {
        if (!talking) nextLine();
        return;
    }
    const float delay = static_cast<float>(s.autoDelay) / 1000.0f + (voiced_ ? 0.0f : length * gameSettings().autoLetterDelay);
    if (auto_ && !talking && (autoTimer_ += dt) >= delay) nextLine();
}

// How much the speaker is speaking right now: their voice's loudness, or a steady flapping while
// their text types out (lines without a voice). A face that doesn't copy their mouth flaps at its
// own moments instead, opening as wide as the voice is loud.
void Game::updateSpeech() {
    speech_ = faceSpeech_ = 0.0f;
    if (wait_ != Wait::Say || speakerKey_.empty()) return;
    const CharacterDef* def = script_.character(speakerKey_);
    const float flaps = def ? def->mouthSpeed : gameSettings().mouthSpeed;
    float loudness = 1.0f;
    if (voiced_) {
        if (!ctx_.sound.voicePlaying()) return;
        speech_ = loudness = ctx_.sound.voiceLevel();
    } else if (typing_) {
        speech_ = 0.5f - 0.5f * std::cos(lineTime_ * flaps * 6.2831853f);
    } else {
        return;
    }
    faceSpeech_ = flapping(speakerKey_ + " face", lineTime_, flaps) * std::min(1.0f, loudness * 1.5f);
}

float Game::mouth(MouthMoves moves, float speech, bool voiced) const {
    moves = std::max(moves, static_cast<MouthMoves>(std::clamp(ctx_.settings.mouths, 0, 2))); // (MouthMoves goes Yes, Voice, No, as the setting does)
    return moves == MouthMoves::Yes || (moves == MouthMoves::Voice && voiced) ? speech : 0.0f;
}

std::vector<Game::Upcoming> Game::upcomingImages(int commands) const {
    std::vector<Upcoming> out;
    unsigned looks = stage_.effect().looks & kColourLooks; // the colour effects on stage from there on
    std::vector<std::string> stage;                       // what's on it
    std::vector<std::string> people;                      // the characters on it (a line can change their look)
    auto imageOf = [&](const std::string& key, const std::string& expression) {
        const CharacterDef* def = script_.character(key);
        return def ? def->imageOf(expression) : std::string{};
    };
    auto show = [&](std::string name) {
        if (name.empty()) return;
        if (looks != 0) out.push_back({name, looks});
        out.push_back({name, 0}); // (as it is too: for when the effect fades)
        stage.push_back(std::move(name));
    };
    if (!stage_.background().starts_with('#')) stage.push_back(stage_.background());
    for (const Stage::ObjectState& o : stage_.objects()) {
        stage.push_back(o.character.empty() ? o.image : imageOf(o.character, o.expression));
        if (!o.character.empty()) people.push_back(o.character);
    }
    const std::size_t end = std::min(script_.code.size(), resumePc_ + static_cast<std::size_t>(std::max(0, commands)));
    for (std::size_t pc = resumePc_; pc < end; ++pc)
        std::visit(Overloaded{
                       [&](const cmd::Background& c) {
                           stage.clear();
                           people.clear();
                           if (!c.image.starts_with('#')) show(c.image);
                       },
                       [&](const cmd::Show& c) {
                           show(c.character.empty() ? c.image : imageOf(c.character, c.expression));
                           if (!c.character.empty()) people.push_back(c.character);
                       },
                       [&](const cmd::Say& c) {
                           if (c.character.empty()) return;
                           if (!c.expression.empty() && std::ranges::contains(people, c.character)) show(imageOf(c.character, c.expression));
                           if (const CharacterDef* def = script_.character(c.character); def && !def->face.empty()) // (in the textbox: no effects)
                               out.push_back({def->faceOf(c.expression)});
                       },
                       [&](const cmd::Effect& c) { // what's on stage then is drawn in its colours (for a moment, or from then on)
                           const unsigned with = (c.seconds > 0.0f ? looks | c.looks : c.looks) & kColourLooks;
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
    if (def ? def->blink : gameSettings().blink) f.eyes = blinking(who, clock_, def ? def->secondsBetweenBlinks : gameSettings().secondsBetweenBlinks);
    if (def && who == speakerKey_) f.mouth = mouth(def->mouthMoves, speech_, voiced_);
    else if (def && std::ranges::any_of(others_, [&](const BoxLine& l) { return l.speakerKey == who && l.reveal < static_cast<float>(l.length); }))
        f.mouth = mouth(def->mouthMoves, flapping(who, clock_, def->mouthSpeed), false); // a line said together: typing without a voice
    return f;
}

Frame Game::faceFrame() const {
    const CharacterDef* def = script_.character(speakerKey_);
    if (!def) return {};
    // A face that copies the character blinks and talks with them; otherwise at its own moments.
    Frame f{.mouth = mouth(def->faceMouth, def->faceCopies.mouth ? speech_ : faceSpeech_, voiced_)};
    if (def->faceBlink) f.eyes = blinking(def->faceCopies.blink ? speakerKey_ : speakerKey_ + " face", clock_, def->secondsBetweenBlinks);
    return f;
}

void Game::draw(const Font& labelFont) {
    if (wait_ == Wait::Movie) { // a movie covers everything, as big as fits
        SDL_Renderer* r = ctx_.renderer;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderFillRect(r, nullptr);
        if (SDL_Texture* frame = movie_ ? movie_->texture() : nullptr) {
            const float scale = std::min(ctx_.canvas.w / movie_->width(), ctx_.canvas.h / movie_->height());
            const float w = movie_->width() * scale, h = movie_->height() * scale;
            const SDL_FRect dst{(ctx_.canvas.w - w) * 0.5f, (ctx_.canvas.h - h) * 0.5f, w, h};
            SDL_RenderTexture(r, frame, nullptr, &dst);
        } else if (!movieStandIn_.empty()) { // (developer mode) the movie that's missing
            movieLabel_.set(labelFont, std::format("movies/{}.mpg", movieStandIn_));
            const SDL_FPoint size = movieLabel_.size();
            movieLabel_.draw(r, (ctx_.canvas.w - size.x) * 0.5f, (ctx_.canvas.h - size.y) * 0.5f, theme::dim);
        }
        return;
    }
    stage_.draw(ctx_.renderer, ctx_.assets, labelFont, gameSettings().developer, gameSettings().characterHeight,
                [this](const Stage::ObjectState& s) { return frame(s.character.empty() ? s.tag : s.character); });
}

// ---------------------------------------------------------------------------
// Save / load
// ---------------------------------------------------------------------------

std::string Game::save(bool history) const {
    auto located = [&](std::size_t pc) {
        auto [label, offset] = script_.locate(pc);
        return std::format("{} {} {}", label.empty() ? "-" : label, offset, pc);
    };
    std::string out = std::format("{}\nhash {}\nresume {}\nrandom {}\n", kSaveMagic, script_.hash, located(resumePc_), resumeRandom_);
    for (auto pc : callStack_) out += std::format("call {}\n", located(pc));
    for (const auto& [name, value] : vars_) out += std::format("variable {} {}\n", name, encodeValue(value));
    for (const auto& o : stage_.objects()) out += objectLine(o);
    if (!stage_.background().empty())
        out += std::format("background {}{}{}\n", stage_.background(), stage_.backgroundAnimation(true).empty() ? "" : " animation=" + escape(stage_.backgroundAnimation(true)),
                           stage_.backgroundAnimation(false).empty() ? "" : " played=" + escape(stage_.backgroundAnimation(false)));
    if (!flowchartNode_.empty()) out += std::format("flowchartpoint {} {}\n", flowchartNode_, flowchartOption_);
    if (!music_.empty()) out += std::format("musicvol {}\nmusic {}\n", std::lround(musicVolume_ * 1000.0f), music_);
    for (const auto& [channel, loop] : loops_)
        out += std::format("loop {} {} {}\n", escape(channel), std::lround(loop.volume * 1000.0f), loop.file);
    for (const auto& screen : screens_) out += std::format("screen {}\n", screen);
    // The phone: how many messages have come (pphone), its messages and calls, and a chat scene or a call under
    // way. Texts are one word each (field()); a command, "-" or where it is.
    auto message = [&](std::string_view key, const MessageItem& m) {
        return std::format("{} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {}\n", key, static_cast<int>(m.kind), m.read ? 1 : 0, m.seen ? 1 : 0,
                           m.deleted ? 1 : 0, m.edited ? 1 : 0, std::lround(m.typing * 1000.0f), m.number, m.colour.r, m.colour.g, m.colour.b, field(m.from),
                           field(m.with), field(m.character), field(m.expression), field(m.text), field(m.file), field(m.time), field(m.date),
                           field(m.reaction));
    };
    out += std::format("pphone {} {}\n", messageCount_, arrivals_);
    for (const MessageItem& m : phone_.messages) out += message("pmsg", m);
    for (const CallItem& c : phone_.calls)
        out += std::format("pcall {} {} {} {} {} {} {} {} {} {} {}\n", c.outgoing ? 1 : 0, c.missed ? 1 : 0, c.busy ? 1 : 0, c.seen ? 1 : 0, c.seconds, c.colour.r,
                           c.colour.g, c.colour.b, field(c.who), field(c.time), field(c.date));
    if (!chat_.empty()) out += std::format("pscene {} {}\n", field(chat_), wait_ == Wait::Chat && !change_ ? chatWait_ : 0); // (and the message it waits for)
    if (call_ && call_->talking && call_->at) // (one ringing rings again: its command is done again)
        out += std::format("pcallnow {} {} {} {} {} {} {} {}\n", call_->outgoing ? 1 : 0, std::lround(call_->time * 1000.0f), call_->colour.r, call_->colour.g,
                           call_->colour.b, field(call_->who), field(call_->character), located(*call_->at));
    for (std::size_t i = 0; i + (wait_ == Wait::Say ? 1 : 0) < page_.size(); ++i) { // (the line on screen is said again)
        const PageLine& p = page_[i];
        out += std::format("page {} {} {} {} {}\n", p.colour.r, p.colour.g, p.colour.b, escape(p.speaker.empty() ? "-" : p.speaker), escape(p.text));
    }
    // The history (but the line on screen: it's said again), each line's words, looks and voice; and a choice's
    // question, the line on screen with it.
    if (history)
        for (std::size_t i = 0; i + (wait_ == Wait::Say ? 1 : 0) < history_.size(); ++i) {
            const HistoryEntry& h = history_[i];
            out += std::format("hist {} {} {} {} {} {} {} {} {} {} {}", h.colour.r, h.colour.g, h.colour.b, h.choice ? 1 : 0, std::lround(h.voiceVolume * 1000.0f),
                               std::lround(h.madeUpVoice * 1000.0f), std::lround(h.madeUpVoiceSpeed * 1000.0f), field(h.speaker), field(h.voice), field(h.text),
                               h.spans.size());
            for (const TextSpan& s : h.spans)
                out += std::format(" {} {} {} {} {}", s.begin, s.end, s.colour ? std::format("#{:02x}{:02x}{:02x}{:02x}", s.colour->r, s.colour->g, s.colour->b, s.colour->a) : "-",
                                   static_cast<unsigned>(s.style), field(s.ruby));
            out += "\n";
        }
    if (wait_ == Wait::Menu && menuPrompt_ && saidAt_ < script_.code.size()) out += std::format("prompt {}\n", located(saidAt_));
    if (const cmd::Weather& w = stage_.weather(); !w.kind.empty())
        out += std::format("weather {} {} {} {} {} {} {}\n", w.kind, std::lround(w.amount * 1000.0f), std::lround(w.speed * 1000.0f),
                           std::lround(w.wind * 1000.0f), w.behind ? 1 : 0,
                           w.tint ? std::format("#{:02x}{:02x}{:02x}{:02x}", w.tint->r, w.tint->g, w.tint->b, w.tint->a) : "-",
                           w.image.empty() ? "-" : w.image);
    if (const cmd::Effect e = stage_.effect(); e.looks != 0) out += std::format("effect {} {}\n", e.looks, std::lround(e.amount * 1000.0f));
    if (trigger_) out += std::format("trigger {} {}\n", located(trigger_->at), trigger_->lines + (wait_ == Wait::Say ? 1 : 0)); // (the line is said again)
    for (const BoxLine& l : others_)
        out += std::format("box {} {} {} {} {} {} {} {}\n", l.speakerColour.r, l.speakerColour.g, l.speakerColour.b, escape(l.box),
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
                           std::lround(s.rotation * 1000.0f), std::lround(s.fade * 1000.0f), std::lround(s.wipe * 1000.0f), hex(s.fadeColour),
                           s.colour ? hex(*s.colour) : "-", s.wipeShape.empty() ? "-" : escape(s.wipeShape));
    }
    for (const auto& [character, expression] : expressions_) out += std::format("expr {} {}\n", escape(character), escape(expression));
    if (wait_ == Wait::Say && !lineVoice_.file.empty()) // the line's voice plays again when it's loaded
        out += std::format("voice {} {} {}\n", lineVoice_.textFollows ? (*lineVoice_.textFollows ? 1 : 0) : -1, std::lround(lineVoice_.volume * 1000.0f),
                           lineVoice_.file);
    return out;
}

std::expected<std::string, std::string> Game::load(std::string_view data) {
    auto note = restore(data, std::nullopt); // (a game of its own: rollback goes back as far as the line it goes on at)
    autosave_ = false; // reaching a choice by loading is not a new decision point
    return note;
}

void Game::remember() {
    if (gameSettings().rollbackLines <= 0 || asking()) return; // (a chat= or call= scene's lines: the story's line is the place to go back to)
    const bool line = wait_ == Wait::Say;
    past_.push_back({save(false), historyDropped_ + history_.size() - (line && !history_.empty() ? 1 : 0), !(line && noWait_), ctx_.persistent});
    if (past_.size() > static_cast<std::size_t>(gameSettings().rollbackLines) + 1) past_.erase(past_.begin());
}

bool Game::canRollback() const {
    if (!ctx_.settings.rollback || asking() || (wait_ != Wait::Say && wait_ != Wait::Menu) || past_.size() < 2) return false; // (not in a chat= or call= scene's lines: once it's over)
    return std::any_of(past_.begin(), past_.end() - 1, [](const Moment& m) { return m.stop; });
}

bool Game::rollback() {
    if (!canRollback()) return false;
    std::size_t moment = past_.size() - 2; // before the line on screen now ({no_wait} lines go by)
    while (!past_[moment].stop) --moment;
    return rewind(moment);
}

std::optional<std::size_t> Game::momentOf(std::size_t entry) const {
    if (!ctx_.settings.rollback || asking() || (wait_ != Wait::Say && wait_ != Wait::Menu)) return std::nullopt;
    const std::size_t counted = historyDropped_ + entry; // a line's entry is its moment's history; a choice's, its menu's
    for (std::size_t i = 0; i + 1 < past_.size(); ++i)
        if (past_[i].stop && past_[i].history == counted) return i;
    return std::nullopt;
}

bool Game::rollbackTo(std::size_t entry) {
    const auto moment = momentOf(entry);
    return moment && rewind(*moment);
}

void Game::addHistory(HistoryEntry entry) {
    history_.push_back(std::move(entry));
    const auto limit = static_cast<std::size_t>(std::max(1, gameSettings().historyLength));
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
    // doesn't do it twice (a visit counter, say). Gallery images, endings, songs and achievements
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
    std::optional<std::uint64_t> random; // (none written: the game's own goes on)
    std::optional<Trigger> trigger;
    std::optional<Pass> pass;
    int hidden = 0;
    std::vector<BoxLine> others;
    std::vector<std::size_t> stack;
    Variables vars;
    std::vector<Stage::ObjectState> objects;
    std::string background, backgroundAnimation, backgroundPlayed, music, flowchartNode;
    int flowchartOption = -1;
    float musicVolume = 1.0f;
    std::map<std::string, Loop, std::less<>> loops;
    std::vector<std::string> screens;
    PhoneData phone;
    PhoneData* ph = nullptr; // (once its line is read: its things come after it)
    std::string chatScene;
    std::uint32_t chatWaits = 0; // (the message the chat scene waits for, still coming)
    std::uint32_t messageCount = 0, arrivals = 0;
    std::optional<CallNow> callNow;
    std::map<std::string, std::string, std::less<>> expressions;
    std::optional<LineVoice> voice;
    std::vector<PageLine> page;
    std::vector<HistoryEntry> history;
    std::optional<std::size_t> prompt;
    cmd::Weather weather;
    cmd::Effect effect;
    std::string cameraOwn, cameraPlayed;
    Animation::Look cameraStart;

    // The exact command when the script is unchanged; otherwise scene + offset.
    auto resolve = [&](std::istream& s) -> std::optional<std::size_t> {
        std::string label;
        std::size_t offset = 0, pc = 0;
        if (!(s >> label >> offset >> pc)) return std::nullopt;
        if (hash == script_.hash && pc <= script_.code.size()) return pc; // (the end: a scene played on its own returns there)
        if (auto at = script_.findScene(label)) {
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
            int r = 0, g = 0, b = 0;
            std::string box, speakerKey, speaker, face, source;
            ls >> r >> g >> b >> box >> speakerKey >> speaker >> face >> source;
            auto none = [](const std::string& s) { return s == "-" ? std::string{} : s; };
            BoxLine l = boxLine(unescape(box), none(unescape(speaker)), {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255},
                                unescape(source), script_.character(none(unescape(speakerKey))));
            l.face = none(unescape(face));
            l.reveal = static_cast<float>(l.length);
            others.push_back(std::move(l));
        } else if (key == "call") {
            if (auto pc = resolve(ls)) stack.push_back(*pc);
        } else if (key == "variable") {
            std::string name, value;
            ls >> name;
            std::getline(ls >> std::ws, value);
            vars.insert_or_assign(name, decodeValue(value));
        } else if (key == "object") {
            objects.push_back(readObject(ls));
        } else if (key == "background") { // (its name, then its animations: key=value)
            ls >> background;
            std::string field;
            while (ls >> field)
                if (field.starts_with("animation=")) backgroundAnimation = unescape(field.substr(10));
                else if (field.starts_with("played=")) backgroundPlayed = unescape(field.substr(7));
        } else if (key == "flowchartpoint") {
            ls >> flowchartNode >> flowchartOption;
        } else if (key == "music") {
            music = rest;
        } else if (key == "musicvol") {
            musicVolume = static_cast<float>(toInt(rest)) / 1000.0f;
        } else if (key == "loop") {
            std::string channel, file;
            long volume = 1000;
            ls >> channel >> volume;
            std::getline(ls >> std::ws, file);
            loops.insert_or_assign(unescape(channel), Loop{file, static_cast<float>(volume) / 1000.0f});
        } else if (key == "screen") {
            screens.push_back(rest);
        } else if (key == "pphone") { // (its things come next)
            ls >> messageCount >> arrivals;
            ph = &phone;
        } else if (key == "pmsg") {
            int kind = 0, read = 0, seen = 0, deleted = 0, edited = 0, r = 0, g = 0, b = 0;
            long typing = 0;
            MessageItem m;
            std::string from, with, character, expression, text, file, time, date, reaction;
            ls >> kind >> read >> seen >> deleted >> edited >> typing >> m.number >> r >> g >> b >> from >> with >> character >> expression >> text >> file >> time >>
                date >> reaction;
            m.kind = static_cast<MessageItem::Kind>(kind);
            m.read = read != 0;
            m.seen = seen != 0;
            m.deleted = deleted != 0;
            m.edited = edited != 0;
            m.typing = static_cast<float>(typing) / 1000.0f;
            m.colour = {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255};
            m.from = unfield(from);
            m.with = unfield(with);
            m.character = unfield(character);
            m.expression = unfield(expression);
            m.text = unfield(text);
            m.file = unfield(file);
            m.time = unfield(time);
            m.date = unfield(date);
            m.reaction = unfield(reaction);
            if (ph) ph->messages.push_back(std::move(m));
        } else if (key == "pcall" && ph) {
            int outgoing = 0, missed = 0, busy = 0, seen = 0, r = 0, g = 0, b = 0;
            CallItem c;
            std::string who, time, date;
            ls >> outgoing >> missed >> busy >> seen >> c.seconds >> r >> g >> b >> who >> time >> date;
            c.outgoing = outgoing != 0;
            c.missed = missed != 0;
            c.busy = busy != 0;
            c.seen = seen != 0;
            c.colour = {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255};
            c.who = unfield(who);
            c.time = unfield(time);
            c.date = unfield(date);
            ph->calls.push_back(std::move(c));
        } else if (key == "pscene") {
            std::string with;
            ls >> with >> chatWaits;
            chatScene = unfield(with);
        } else if (key == "pcallnow") {
            int outgoing = 0, r = 0, g = 0, b = 0;
            long time = 0;
            std::string who, character;
            ls >> outgoing >> time >> r >> g >> b >> who >> character;
            if (const auto pc = resolve(ls))
                callNow = CallNow{.who = unfield(who), .character = unfield(character), .colour = {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255},
                                  .outgoing = outgoing != 0, .talking = true, .time = static_cast<float>(time) / 1000.0f, .at = *pc};
        } else if (key == "expr") {
            std::string character, expression;
            ls >> character >> expression;
            expressions.insert_or_assign(unescape(character), unescape(expression));
        } else if (key == "hist") {
            int r = 0, g = 0, b = 0, choice = 0;
            long volume = 1000, madeUp = 0, madeUpSpeed = 1000;
            std::string speaker, voiceFile, text;
            std::size_t spans = 0;
            if (!(ls >> r >> g >> b >> choice >> volume >> madeUp >> madeUpSpeed >> speaker >> voiceFile >> text >> spans)) continue;
            HistoryEntry h{.speaker = unfield(speaker), .colour = {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255}, .text = unfield(text),
                           .choice = choice != 0, .voice = unfield(voiceFile), .voiceVolume = static_cast<float>(volume) / 1000.0f,
                           .madeUpVoice = static_cast<float>(madeUp) / 1000.0f, .madeUpVoiceSpeed = static_cast<float>(madeUpSpeed) / 1000.0f};
            for (std::size_t i = 0; i < spans; ++i) {
                TextSpan s;
                std::string colour, ruby;
                unsigned style = 0;
                if (!(ls >> s.begin >> s.end >> colour >> style >> ruby)) break;
                if (colour != "-") s.colour = parseColour(colour);
                s.style = static_cast<TTF_FontStyleFlags>(style);
                s.ruby = unfield(ruby);
                h.spans.push_back(std::move(s));
            }
            history.push_back(std::move(h));
        } else if (key == "prompt") {
            prompt = resolve(ls);
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
            std::string colourText, image;
            ls >> weather.kind >> amount >> speed >> wind >> behind >> colourText >> image;
            weather.amount = static_cast<float>(amount) / 1000.0f;
            weather.speed = static_cast<float>(speed) / 1000.0f;
            weather.wind = static_cast<float>(wind) / 1000.0f;
            weather.behind = behind != 0;
            if (colourText != "-") weather.tint = parseColour(colourText);
            if (image != "-") weather.image = image;
        } else if (key == "effect") {
            long amount = 1000;
            ls >> effect.looks >> amount;
            effect.amount = static_cast<float>(amount) / 1000.0f;
        } else if (key == "camera") {
            std::string own, played, fadeColour, colour, shape;
            long x = 0, y = 0, opacity = 1000, zoom = 1000, rotation = 0, fade = 0, wipe = 1000;
            ls >> own >> played >> x >> y >> opacity >> zoom >> rotation >> fade >> wipe >> fadeColour >> colour >> shape;
            auto named = [](const std::string& s) { return s == "-" ? std::string{} : unescape(s); };
            auto thousandths = [](long n) { return static_cast<float>(n) / 1000.0f; };
            cameraOwn = named(own);
            cameraPlayed = named(played);
            cameraStart = {.x = thousandths(x), .y = thousandths(y), .opacity = thousandths(opacity), .zoom = thousandths(zoom), .rotation = thousandths(rotation),
                           .colour = colour == "-" ? std::nullopt : parseColour(colour), .fade = thousandths(fade),
                           .fadeColour = parseColour(fadeColour).value_or(SDL_Color{0, 0, 0, 255}), .wipe = thousandths(wipe), .wipeShape = named(shape)};
        } else if (key == "voice") {
            int follows = -1;
            long volume = 1000;
            LineVoice v;
            ls >> follows >> volume;
            std::getline(ls >> std::ws, v.file);
            v.volume = static_cast<float>(volume) / 1000.0f;
            if (follows >= 0) v.textFollows = follows == 1;
            voice = std::move(v);
        }
    }
    if (!resume) return std::unexpected("this save points to a scene that no longer exists");

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
    if (!background.empty()) {
        stage_.setBackground(background, nullptr, true);
        stage_.animateBackground(animation(backgroundAnimation), true, true);
        stage_.animateBackground(animation(backgroundPlayed), false, true);
    }
    flowchartNode_ = flowchartNode;
    flowchartOption_ = flowchartOption;
    stage_.setWeather(weather, 0.0f);
    stage_.setEffect(effect, true);
    stage_.restoreCamera(animation(cameraOwn), animation(cameraPlayed), cameraStart);
    page_ = std::move(page);
    vars_ = std::move(vars);
    callStack_ = std::move(stack);
    screens_ = std::move(screens);
    phone_ = std::move(phone);
    messageCount_ = messageCount;
    arrivals_ = arrivals;
    const bool phoneUp = !chat_.empty() || call_; // (the story's chat scene or call: rolling back to before it puts the phone away)
    chat_ = chatScene;
    chatWait_ = chatWaits;
    call_ = callNow;
    change_.reset();
    stopRinging();
    if (!chat_.empty()) requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "chat", .phone = chat_}); // (on with it)
    if (call_) requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "call"});
    if (phoneUp && chat_.empty() && !call_) requests_.push_back(cmd::Ui{.verb = "hide phone"});
    expressions_ = std::move(expressions);
    music_ = music;
    musicVolume_ = musicVolume;
    ctx_.sound.stopVoice();
    if (music.empty()) ctx_.sound.stopMusic(0.5f);
    else ctx_.sound.playMusic(ctx_.assets.sound(music), true, 0.5f, musicVolume); // the same song keeps playing
    // Looping sounds: a load starts them over; rollback keeps those that are already right.
    const bool rollingBack = keepHistory.has_value();
    if (!rollingBack) ctx_.sound.stopChannel("", 0.3f);
    for (const auto& [channel, loop] : loops_)
        if (rollingBack && (!loops.contains(channel) || loops.at(channel).file != loop.file)) ctx_.sound.stopChannel(channel, 0.3f);
    for (const auto& [channel, loop] : loops)
        if (!rollingBack || !loops_.contains(channel) || loops_.at(channel).file != loop.file)
            ctx_.sound.playChannel(channel, ctx_.assets.sound(loop.file), true, loop.volume, 0.5f);
    loops_ = std::move(loops);

    // Rollback keeps the entries before its line (counting those let go); a load has the save's.
    if (keepHistory) history_.resize(std::min(history_.size(), *keepHistory > historyDropped_ ? *keepHistory - historyDropped_ : 0));
    else {
        historyDropped_ = 0;
        history_ = std::move(history);
    }
    choiceItems_.clear();
    trigger_ = trigger;
    if (const cmd::Menu* m = trigger && trigger->at < script_.code.size() ? std::get_if<cmd::Menu>(&script_.code[trigger->at].command) : nullptr)
        listChoices(*m);
    else trigger_.reset();
    pass_ = pass;
    asked_.reset();
    parked_.reset();
    chatOpen_.clear();
    putAway_.clear();
    hiddenLines_ = hidden;
    others_ = std::move(others);
    together_ = !others_.empty(); // (the last line of a together block is said again)
    error_.clear();
    skip_ = uiHidden_ = menuPrompt_ = false;
    pendingVoice_ = voice; // the line's own voice line plays again
    if (voice && !skipping()) ctx_.sound.playVoice(ctx_.assets.sound(voice->file), voice->volume);
    pc_ = resumePc_ = *resume;
    if (random) random_ = *random;
    // A choice with its question on screen: that line back as it was, all there at once, then the choice.
    if (const auto* asks = prompt && *prompt < script_.code.size() ? std::get_if<cmd::Say>(&script_.code[*prompt].command) : nullptr) {
        const CharacterDef* def = asks->character.empty() ? nullptr : script_.character(asks->character);
        say(def ? def->name : asks->speaker, def ? def->nameColour : gameSettings().narrationColour, asks->text, def);
        reveal_ = static_cast<float>(length_);
        passedStops_ = stops_.size();
        noWait_ = false;
        saidAt_ = *prompt;
    } else {
        text_.clear(); // (none: no question with it, nor one left from before rolling back)
    }
    if (std::ranges::any_of(phone_.messages, [&](const MessageItem& m) { return chatWait_ && m.number == chatWait_ && m.typing > 0.0f; }))
        wait_ = Wait::Chat; // (a chat scene's message still coming: on once it's come)
    else {
        wait_ = Wait::None;
        run();
    }
    return note;
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

Game::When Game::timeNow() const {
    const GameSettings& g = gameSettings();
    auto kept = [&](std::string_view name) -> std::optional<int> { // (the variable, as the story has it)
        const auto it = vars_.find(name);
        return it != vars_.end() ? std::optional(static_cast<int>(std::lround(toNumber(it->second)))) : std::nullopt;
    };
    if (g.time == TimeKind::Real) {
        const RealNow now = realNow();
        return {std::max(1, now.date - kept("time_started").value_or(now.date) + 1), now.minutes};
    }
    return {std::max(1, kept("time_day").value_or(1)), ((kept("time_minutes").value_or(g.timeStartMinutes) % 1440) + 1440) % 1440};
}

std::string Game::partAt(int minutes) const {
    const auto& parts = gameSettings().dayParts;
    std::string out = parts.back().first; // (before the first one starts: the last one's, from the night before)
    for (const auto& [name, start] : parts)
        if (start <= minutes) out = name;
    return out;
}

std::string Game::clockText(int minutes) const {
    const GameSettings& g = gameSettings();
    if (g.time == TimeKind::Days) return script_.translated(upperFirst(partAt(minutes))); // (no clock: the part of the day)
    const int hours = minutes / 60, m = minutes % 60;
    if (!g.clock12) return std::format("{}:{:02}", hours, m);
    return std::format("{}:{:02} {}", hours % 12 == 0 ? 12 : hours % 12, m, script_.translated(hours < 12 ? "AM" : "PM"));
}

int Game::dateOf(int day) const {
    const GameSettings& g = gameSettings();
    if (g.time == TimeKind::Real) {
        const auto started = vars_.find("time_started");
        return started != vars_.end() ? static_cast<int>(std::lround(toNumber(started->second))) + day - 1 : realNow().date;
    }
    return g.timeStartDate >= 0 ? g.timeStartDate + day - 1 : -1;
}

int Game::weekdayOn(int day) const {
    if (const int date = dateOf(day); date >= 0) return weekdayOf(date);
    const int first = gameSettings().timeStartWeekday;
    return first >= 0 ? (first + day - 1) % 7 : -1;
}

std::string Game::dateText(int day) const {
    const int date = dateOf(day), weekday = weekdayOn(day);
    const std::string named = weekday >= 0 ? script_.translated(kWeekdays[static_cast<std::size_t>(weekday)]) : std::string{};
    if (date < 0) return weekday >= 0 ? named : filledIn(script_.translated("Day {day}"), {{"day", std::to_string(day)}});
    const auto [year, month, dom] = dateFromDays(date);
    return filledIn(script_.translated("{weekday} {day} {month}"),
                    {{"weekday", named}, {"day", std::to_string(dom)}, {"month", script_.translated(kMonths[static_cast<std::size_t>(month - 1)])}, {"year", std::to_string(year)}});
}

std::optional<Value> Game::timeValue(std::string_view name) const {
    const When now = timeNow();
    const int date = dateOf(now.day), weekday = weekdayOn(now.day);
    if (name == "time_day") return now.day;
    if (name == "time_hour") return now.minutes / 60;
    if (name == "time_minute") return now.minutes % 60;
    if (name == "time_part") return partAt(now.minutes); // (as the scripts name it: for if lines)
    if (name == "time_weekday") return weekday >= 0 ? std::string(kWeekdays[static_cast<std::size_t>(weekday)]) : std::string{}; // (in English: for if lines)
    if (name == "time_month") return date >= 0 ? std::string(kMonths[static_cast<std::size_t>(dateFromDays(date)[1] - 1)]) : std::string{};
    if (name == "time_year") return date >= 0 ? dateFromDays(date)[0] : 0;
    if (name == "time_clock") return clockText(now.minutes); // (as it shows: in the player's language)
    if (name == "time_date") return dateText(now.day);
    if (name == "time_waiting") return wait_ == Wait::Clock ? 1 : 0; // (setting time real: the story waits for the player's clock...)
    if (name == "time_until") { // (...until then)
        const auto until = vars_.find("time_wait");
        if (wait_ != Wait::Clock || until == vars_.end()) return std::string{};
        const double left = toNumber(until->second) - realNow().seconds;
        return clockText(static_cast<int>(std::lround(now.minutes + std::max(0.0, left) / 60.0)) % 1440);
    }
    return std::nullopt;
}

std::string Game::backgroundFor(std::string_view image) const {
    const GameSettings& g = gameSettings();
    std::string base(image); // (one named for a part of the day already: its own name first)
    for (const auto& [part, start] : g.dayParts)
        if (base.size() > part.size() + 1 && base.ends_with("_" + part) && ctx_.assets.hasImage(base.substr(0, base.size() - part.size() - 1)))
            base.resize(base.size() - part.size() - 1);
    if (g.time == TimeKind::None || !g.backgroundsFollowTime || base.empty() || base.front() == '#') return std::string(image);
    const std::string version = base + "_" + partAt(timeNow().minutes);
    return ctx_.assets.hasImage(version) ? version : base == image ? std::string(image) : base;
}

void Game::timeMoved(When from, When to, bool fast) {
    if (!stage_.background().empty()) // (the background as it is in the new part of the day)
        if (const std::string shown = backgroundFor(stage_.background()); shown != stage_.background()) stage_.setBackground(shown, nullptr, true);
    if (to.day != from.day && !fast && script_.screen("calendar")) waitForScreen({.name = "calendar"}, pc_); // (a new day: the calendar, when the screens have one)
}

void Game::passTime(const cmd::Time& c, std::size_t here) {
    const GameSettings& g = gameSettings();
    const When from = timeNow();
    const int first = g.dayParts.front().second; // (a day's first part: the next one after the last)
    When to = from;
    if (c.passes && c.passing > 0) {
        const int total = from.minutes + c.passing;
        to = {from.day + total / 1440, total % 1440};
    } else if (c.passes) { // to the next part of the day
        const auto next = std::ranges::find_if(g.dayParts, [&](const auto& p) { return p.second > from.minutes; });
        to = next != g.dayParts.end() ? When{from.day, next->second} : When{from.day + 1, first};
    } else {
        int at = c.minutes >= 0 ? c.minutes : from.minutes; // (no time of day said: the same time)
        if (!c.part.empty())
            if (const auto part = std::ranges::find(g.dayParts, c.part, &std::pair<std::string, int>::first); part != g.dayParts.end()) at = part->second;
        const bool already = c.part.empty() ? at == from.minutes : partAt(from.minutes) == c.part; // (it's then already)
        if (c.day > 0) { // that day, then
            to = c.day == from.day && already ? from : When{c.day, at};
        } else {
            const int days = c.weekday >= 0 && weekdayOn(from.day) >= 0 ? (c.weekday - weekdayOn(from.day) + 7) % 7 : 0;
            if (days == 0 && already) to = from;
            else if (days == 0 && at < from.minutes) to = {from.day + (c.weekday >= 0 ? 7 : 1), at}; // (past it: the next one)
            else to = {from.day + days, at};
        }
    }
    if (g.time != TimeKind::Real) {
        vars_.insert_or_assign("time_day", to.day);
        vars_.insert_or_assign("time_minutes", to.minutes);
        return timeMoved(from, to, skipping());
    }
    // The player's clock: the story waits for it, the textbox saying until when (developer mode, and the story
    // played to a line by the developer tools: on at once). A save made meanwhile waits for the same moment.
    const RealNow now = realNow();
    const auto kept = vars_.find("time_wait"), keptLine = vars_.find("time_wait_line");
    const double until = kept != vars_.end() && keptLine != vars_.end() && std::lround(toNumber(keptLine->second)) == static_cast<long>(here)
                           ? toNumber(kept->second)
                           : now.seconds - std::fmod(now.seconds, 60.0) + ((to.day - from.day) * 1440.0 + (to.minutes - from.minutes)) * 60.0;
    if (g.developer || watch_ != SIZE_MAX || until <= now.seconds) {
        vars_.erase("time_wait");
        vars_.erase("time_wait_line");
        return;
    }
    vars_.insert_or_assign("time_wait", until);
    vars_.insert_or_assign("time_wait_line", static_cast<int>(here));
    waitFrom_ = from;
    say({}, g.narrationColour, filledIn(script_.translated("The story goes on at {time}."), {{"time", clockText(to.minutes)}}), nullptr);
    reveal_ = static_cast<float>(length_);
    waitFor(Wait::Clock, here);
}

} // namespace vn
