// A screen drawn: each part with its look (pointed at, chosen, high contrast), its animations, the keys' ring, the
// flowchart and movies.
#include "Engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace vn {

namespace {

float factor(Align a) {
    return a == Align::Start ? 0.0f : a == Align::Centre ? 0.5f : 1.0f;
}

// `text` in at most `most` lines `width` wide, or on one line: as it is, or shortened() to fit.
std::string cutToLines(const Font& font, const std::string& text, float width, float spacing, std::size_t most) {
    TextBlock probe;
    return shortened(text, [&](std::string_view t) {
        probe.set(font, t, width, spacing);
        return probe.lines() <= most;
    });
}

std::string ellipsise(const Font& font, const std::string& text, float width) {
    return shortened(text, [&](std::string_view t) { return font.measure(t) <= width; });
}

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
            at.color = {1.0f, 1.0f, 1.0f, (fadeX > 0.0f ? edge[col] : 1.0f) * (fadeY > 0.0f ? edge[row] : 1.0f)}; // (SDL_Vertex spells it so)
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

// A part's scrollbars (scrollbar=yes), each way what's in it reaches past it: a track in its border
// colour (none: white), faint, with the thumb in its text colour.
void ScreenView::drawScrollbar(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    if (lower(el.option("scrollbar", "no")) != "yes") return;
    const ElementState& st = state_[index];
    const Style& s = styleOf(index);
    for (const bool across : {false, true}) {
        if (across ? st.reachX <= rectOf(index).w : st.reach <= rectOf(index).h) continue;
        draw::rect(r, barOf(index, across), withAlpha(s.border.a > 0 ? s.border : SDL_Color{255, 255, 255, 255}, 0.25f * alpha));
        draw::rect(r, thumbOf(index, across), withAlpha(s.colour, 0.75f * alpha));
    }
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

ScreenView::ButtonLook ScreenView::buttonLook(std::size_t index, const SDL_FRect& hit) {
    const Element& el = def_->elements[index];
    const bool on = enabled(el);
    const bool pressed = (pressing_ == index && pointInRect(mouse_.x, mouse_.y, hit)) || playing_ == index; // (it looks chosen)
    const bool chosen = selected(el) || held_ == index;
    const Look look = !on ? Look::Disabled : pressed ? Look::Selected : focused(index, 0) ? Look::Hover : chosen ? Look::Selected : Look::Idle;
    return {look, on, chosen};
}

RichText ScreenView::textOf(std::size_t index, float width) {
    const Element& el = def_->elements[index];
    RichText out = wordsOf(index, el.text);
    const auto most = static_cast<std::size_t>(std::max(0.0f, el.number("lines", 0.0f)));
    if (most == 0 || width <= 0.0f) return out;
    const Style& s = styleOf(index);
    out.text = cutToLines(styleFont(s), out.text, width, s.lineSpacing, most);
    const std::size_t n = utf8::length(out.text);
    for (TextSpan& span : out.spans) span.begin = std::min(span.begin, n), span.end = std::min(span.end, n);
    return out;
}

Font& ScreenView::styleFont(const Style& s) {
    return engine_.font(s.font, s.textSizeFollows ? s.size * static_cast<float>(engine_.settings().textSize) / 100.0f : s.size);
}

float ScreenView::boxAlpha(const Style& s, float alpha) {
    if (engine_.settings().highContrast) return alpha; // (nothing see-through)
    const float textbox = s.opacityFollows ? static_cast<float>(engine_.settings().textboxOpacity) / 100.0f : 1.0f;
    return alpha * s.opacity * textbox;
}

const Style::State* ScreenView::stateOf(const Style& s, Look look) {
    return look == Look::Hover ? &s.pointed : look == Look::Selected ? &s.chosen : look == Look::Disabled ? &s.unclickable : nullptr;
}

// The image a style shows in a look: the look's own, else (pointed at while chosen) its chosen one,
// else the style's; one named by a variable (image={door}) filled in.
std::string ScreenView::lookImage(const Style& s, Look look, bool chosen) {
    const Style::State* st = stateOf(s, look);
    std::string image = st && !st->image.empty()                              ? st->image
                        : look == Look::Hover && chosen && !s.chosen.image.empty() ? s.chosen.image
                                                                                     : s.image;
    if (image.contains('{')) image = interpolate(image);
    return image;
}

// High contrast (the player's setting), whatever the styles say: white text on black boxes with white edges, yellow
// edges and text where it's pointed at (the keys' ring too), grey where it can't be clicked, and a chosen one yellow
// with black text (one other parts are drawn on: its edges yellow), so the ring and a chosen one are told apart.
constexpr SDL_Color kContrastText{255, 255, 255, 255}, kContrastLit{255, 220, 0, 255}, kContrastOff{170, 170, 170, 255}, kContrastBlack{0, 0, 0, 255};

bool ScreenView::filledLook(Look look, bool chosen) { return look == Look::Selected || (look == Look::Hover && chosen); }

bool ScreenView::hasBox(const Style& s, Look look) {
    const Style::State* st = stateOf(s, look);
    return s.background.a > 0 || s.border.a > 0 || !s.image.empty() || (st && (st->background || st->border || !st->image.empty()));
}

SDL_Color ScreenView::textColour(const Style& s, Look look, bool chosen) const {
    if (engine_.settings().highContrast)
        return filledLook(look, chosen) && hasBox(s, look)      ? kContrastBlack
               : look == Look::Hover || look == Look::Selected ? kContrastLit
               : look == Look::Disabled                         ? kContrastOff
                                                                : kContrastText;
    const Style::State* st = stateOf(s, look);
    return st && st->colour ? *st->colour : s.colour;
}

void ScreenView::box(SDL_Renderer* r, const SDL_FRect& rect, const Style& s, Look look, float alpha, bool chosen, bool holds) {
    const Style::State* st = stateOf(s, look);
    if (engine_.settings().highContrast) { // (a box with something in it: black, its edge white; yellow when pointed at or chosen)
        if (!hasBox(s, look)) return;
        const bool filled = filledLook(look, chosen) && !holds, lit = look == Look::Hover || look == Look::Selected;
        draw::rect(r, rect, withAlpha(filled ? kContrastLit : kContrastBlack, alpha));
        draw::outline(r, rect, withAlpha(lit ? kContrastLit : look == Look::Disabled ? kContrastOff : kContrastText, alpha));
        if (lit && !filled) draw::outline(r, {rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2}, withAlpha(kContrastLit, alpha));
        return;
    }
    if (const std::string image = lookImage(s, look, false); !image.empty())
        if (const Image pic = engine_.image(image)) {
            pic.draw(r, rect, alpha);
            return;
        }
    const SDL_Color bg = st && st->background ? *st->background : s.background;
    const std::optional<SDL_Color> bottom = st && st->background ? st->backgroundBottom : s.backgroundBottom; // (background=navy-black: fading down)
    const SDL_Color border = st && st->border ? *st->border : s.border;
    if (bottom) draw::gradient(r, rect, withAlpha(bg, alpha), withAlpha(*bottom, alpha));
    else if (bg.a > 0) draw::rect(r, rect, withAlpha(bg, alpha));
    if (border.a > 0) draw::outline(r, rect, withAlpha(border, alpha));
}

void ScreenView::text(SDL_Renderer* r, TextBlock& block, std::string_view str, const SDL_FRect& rect, const Style& s,
                      SDL_Color colour, float alpha, std::size_t reveal, std::vector<TextSpan> spans) {
    Font& font = styleFont(s);
    const SDL_FRect inner{rect.x + s.padding, rect.y + s.padding, std::max(0.0f, rect.w - 2 * s.padding),
                          rect.h > 0 ? std::max(0.0f, rect.h - 2 * s.padding) : 0.0f};
    block.set(font, str, inner.w, s.lineSpacing);
    block.setSpans(std::move(spans));
    block.drawIn(r, inner, factor(s.align), factor(s.alignVertically), withAlpha(colour, alpha), reveal, s.shadow);
}

template <class Paint> void ScreenView::wiped(SDL_Renderer* r, const Shift& by, const Paint& paint) {
    if (by.wipe >= 0.999f) return paint(); // (a std::function made only for a wipe)
    partWiper_.draw(r, engine_.assets(), {engine_.canvas().w, engine_.canvas().h}, by.wipeBox, by.wipe, by.wipeShape, paint);
}

void ScreenView::draw(SDL_Renderer* r, float alpha) {
    drawnFrame_ = engine_.frameNumber();
    expand(); // (its lists' items, as they are now)
    if (const std::uint32_t came = engine_.game().arrivals(); came != arrivals_) { // a message came: parts with message_animation= play it
        if (came > arrivals_ && arrivals_ > 0 && !still())
            for (std::size_t i = 0; i < def_->elements.size(); ++i)
                if (def_->elements[i].messageAnimation) state_[i].messageAt = now();
        arrivals_ = came;
    }
    layout();
    targets_.clear();
    const std::size_t n = def_->elements.size();
    drawn_.assign(n, false);
    // A part in another shows only while that one does, and moves and fades with its animation; the
    // whole screen moves and fades with its animation_on_open= (or, closed, its animation_on_close=).
    Shift whole{};
    if (const Animation* a = leftSince_ ? hideAnimation() : showAnimation(); a && (leftSince_ || shownSince_)) {
        float t = now() - (leftSince_ ? *leftSince_ : *shownSince_);
        if (leftSince_ && a->loops()) t = std::min(t, a->length * 0.999f); // (closing: once through, even with loop)
        const Canvas& size = engine_.canvas();
        const Animation::Look look = a->at(played(*a, t), {255, 255, 255, 255}, [&size](bool across, float place) { // (the whole screen: ±2 is just off it)
            return across ? placeEdge(place, size.w, size.w) : placeEdge(-place, size.h, size.h);
        }, {size.w, size.h});
        whole = {look.x, look.y, std::clamp(look.opacity, 0.0f, 1.0f), std::clamp(look.fade, 0.0f, 1.0f), look.fadeColour, look.wipe, {0.0f, 0.0f, size.w, size.h},
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
        const float own = def_->elements[*fading].number("fading_edges", engine_.gameSettings().fadingEdges); // (fading_edges=: pixels; none: a fifth of the box, at most 64)
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
            const Bound bound(*this, i);
            const Element& el = def_->elements[i];
            const bool in = el.parent >= 0 && static_cast<std::size_t>(el.parent) < i;
            if ((in && !drawn_[static_cast<std::size_t>(el.parent)]) || !shows(i)) {
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
            pointedOnward(i);
            std::optional<RenderClip> clip; // (in a container that cuts off its edges: only what's inside its box shows)
            if (const auto cut = cutBy(i, &shifts)) clip.emplace(r, *cut);
            if (st.messageAt && el.messageAnimation && now() - *st.messageAt >= el.messageAnimation->total()) st.messageAt.reset();
            if (st.itemAt && (!el.itemAnimation || now() - *st.itemAt >= el.itemAnimation->total())) st.itemAt.reset();
            if (el.animation || (el.pointedAnimation && st.pointedPlayed > 0.0f) || (st.clicked && el.clickAnimation) || st.messageAt || st.itemAt) { // (clicked: its click animation too, until it's over)
                Shift wipe = by;
                const Animation::Look look = drawAnimated(r, i, alpha * by.opacity, wipe, boxOf(i));
                shifts[i] = {by.x + look.x, by.y + look.y, by.opacity * std::clamp(look.opacity, 0.0f, 1.0f), 0.0f, {}, wipe.wipe, wipe.wipeBox, wipe.wipeShape};
            } else {
                wiped(r, by, [&] { drawElement(r, i, alpha * by.opacity); });
            }
            clip.reset();
            rectOf(i) = rect;
            drawn_[i] = true;
            if (el.kind == ElementKind::Button && el.chosen) { // (chosen because of its chosen=: what its action sets holds, once it's so)
                const bool chosen = el.chosen->test(lookup_);
                if (chosen && !st.chosen)
                    for (const Command& c : el.actions) {
                        const auto* set = std::get_if<cmd::Set>(&c);
                        const auto* ui = std::get_if<cmd::Ui>(&c);
                        if ((set && set->value && set->value->constant()) || (ui && ui->verb == "setting")) chosen_.push_back(c); // (to a value, not by a sum of its own)
                    }
                st.chosen = chosen;
            }
            if (isBox(el.kind) && i + 1 < n && within(i + 1, i) && (el.scrolls() || el.edges() == "fade")) {
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
        if (const Target* t = keyFocus_ ? focusedTarget() : nullptr; t && t->spot.element < n && !t->hidden) { // what the keys point at: a ring round it,
            const SDL_Color c = withAlpha(textColour(styleOf(t->spot.element), Look::Hover), alpha * whole.opacity); // plain to see whatever its pointed look
            const float k = std::max(2.0f, std::round(std::min(engine_.canvas().w, engine_.canvas().h) / 360.0f)), gap = k * 2.0f;
            std::optional<RenderClip> clip; // (in a box that cuts off its edges: only what of it shows there; the wheel may have scrolled it away)
            if (const auto cut = cutBy(t->spot.element, &shifts)) clip.emplace(r, *cut);
            const SDL_FRect o{t->rect.x - gap, t->rect.y - gap, t->rect.w + 2.0f * gap, t->rect.h + 2.0f * gap};
            draw::rect(r, {o.x, o.y, o.w, k}, c);
            draw::rect(r, {o.x, o.y + o.h - k, o.w, k}, c);
            draw::rect(r, {o.x, o.y + k, k, o.h - 2.0f * k}, c);
            draw::rect(r, {o.x + o.w - k, o.y + k, k, o.h - 2.0f * k}, c);
        }
        if (whole.fade > 0.0f) draw::rect(r, {0.0f, 0.0f, engine_.canvas().w, engine_.canvas().h}, withAlpha(whole.fadeColour, whole.fade * alpha)); // (fade=: over everything)
    };
    if (whole.wipe >= 0.999f) parts();
    else screenWiper_.draw(r, engine_.assets(), {engine_.canvas().w, engine_.canvas().h}, whole.wipeBox, whole.wipe, whole.wipeShape, parts);
    if (leftSince_) targets_.clear(); // (closed: only seen going)
    if (ringAt_ != Ring::None && !targets_.empty()) { // (opened by the keys: the ring at once on the answer that keeps things as they are, or the first letter)
        const Ring at = std::exchange(ringAt_, Ring::None);
        auto wanted = [&](const Element& el) {
            if (at == Ring::Keep) {
                const auto* u = el.kind == ElementKind::Button && el.actions.size() == 1 ? std::get_if<cmd::Ui>(&el.actions.front()) : nullptr;
                return u && u->verb == "no";
            }
            const std::string letter = keyLetter(el);
            return !letter.empty() && std::isalpha(static_cast<unsigned char>(letter.front()));
        };
        if (const auto t = std::ranges::find_if(targets_, [&](const Target& target) { return target.reachable && !target.hidden && wanted(def_->elements[target.spot.element]); }); t != targets_.end())
            pointByKeys(*t);
    }
    if (held_ && heldMoved_) { // what's being dragged, under the pointer
        const Element& el = def_->elements[*held_];
        const SDL_FRect at{mouse_.x - grab_.x, mouse_.y - grab_.y, rectOf(*held_).w, rectOf(*held_).h};
        box(r, at, styleOf(*held_), Look::Selected, boxAlpha(styleOf(*held_), alpha) * 0.9f);
        RichText label = rich(el.text);
        text(r, state_[*held_].value, label.text, at, styleOf(*held_), textColour(styleOf(*held_), Look::Selected), alpha * styleOf(*held_).opacity * 0.9f,
             TextBlock::kAll, std::move(label.spans));
    }
}

Animation::Look ScreenView::drawAnimated(SDL_Renderer* r, std::size_t index, float alpha, Shift& by, const SDL_FRect& box) {
    const SDL_FPoint shift{by.x, by.y};
    const Element& el = def_->elements[index];
    const ElementState& st = state_[index];
    const bool image = el.kind == ElementKind::Image || el.kind == ElementKind::Face;
    const bool shape = isBox(el.kind);
    const SDL_Color own = image ? SDL_Color{255, 255, 255, 255} : shape ? el.style.background : el.style.colour;
    const SDL_FRect rect = rectOf(index);
    const auto offset = [&](bool across, float place) { // (places in the box it's in, from where it's written)
        return across ? box.x + placeEdge(place, rect.w, box.w) - (rect.x - shift.x) : box.y + placeEdge(-place, rect.h, box.h) - (rect.y - shift.y);
    };
    const SDL_FPoint screen{engine_.canvas().w, engine_.canvas().h};
    Animation::Look look;
    auto add = [&](const Animation& a, float seconds) { look = look.with(a.at(seconds, look.colour.value_or(own), offset, screen)); };
    if (el.animation && st.since) add(*el.animation, played(*el.animation, now() - *st.since));
    if (el.pointedAnimation && st.pointedPlayed > 0.0f) add(*el.pointedAnimation, st.pointedPlayed);
    if (st.clicked && el.clickAnimation) add(*el.clickAnimation, now() - *st.clicked);
    if (st.messageAt && el.messageAnimation) add(*el.messageAnimation, now() - *st.messageAt);
    if (st.itemAt && el.itemAnimation) add(*el.itemAnimation, now() - *st.itemAt);
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
    if (look.colour) (image ? tint_ : shape ? style.background : style.colour) = *look.colour;
    state_[index].styled = std::move(style);
    turn_ = look.rotation;
    if (look.wipe < by.wipe) { // (its own wipe, over where it is now)
        by.wipe = look.wipe;
        by.wipeBox = rectOf(index);
        by.wipeShape = look.wipeShape;
    }
    wiped(r, by, [&] {
        drawElement(r, index, alpha * std::clamp(look.opacity, 0.0f, 1.0f));
        if (look.fade > 0.0f) draw::rect(r, rectOf(index), withAlpha(look.fadeColour, std::clamp(look.fade, 0.0f, 1.0f) * alpha)); // (fade=: the part covered with the colour)
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
    case ElementKind::Container: { // (its box, when its style has one: the parts in it are drawn after it; a list's: what it shows is seen)
        box(r, rectOf(index), s, selected(el) ? Look::Selected : Look::Idle, a, false, true);
        if (!el.option("list").empty() && !tools_) seen(el);
        // A list's item with nothing in it to press (an ending, an achievement): the keys go to it (it's read, and its list
        // scrolls to it; scrolled away, they still can), the pointer doesn't.
        auto presses = [&] {
            for (std::size_t j = index + 1; j < def_->elements.size() && within(j, index); ++j)
                if (const ElementKind k = def_->elements[j].kind; k == ElementKind::Button || k == ElementKind::Slider || k == ElementKind::Input || k == ElementKind::Control ||
                                                                 k == ElementKind::Flowchart)
                    return true;
            return false;
        };
        if (isItemBox(index) && el.parent >= 0 && !presses())
            targets_.push_back({{index}, rectOf(index), true, !inside(rectOf(index), rectOf(static_cast<std::size_t>(el.parent))), false});
        break;
    }

    case ElementKind::Image: { // the name may come from a value: image "{open_image}"; atlases blink; chosen: its chosen image; a movie plays
        const std::string& shows = !s.chosen.image.empty() && selected(el) ? s.chosen.image : el.text;
        const std::string name = shows.find('{') == std::string::npos ? shows : interpolate(shows);
        const auto fill = name.empty() ? std::nullopt : parseColour(name, &engine_.palette()); // (a colour, not an image)
        if (const auto grad = name.empty() ? std::nullopt : parseGradient(name, &engine_.palette())) { // (navy-black: fading down to another)
            draw::gradient(r, rectOf(index), withAlpha(grad->first, a), withAlpha(grad->second, a));
            break;
        }
        if (el.colours.contains("bottom")) { // its colour fading down to bottom= (the same, as two words)
            const SDL_Color top = fill.value_or(s.background);
            draw::gradient(r, rectOf(index), withAlpha(top, a), withAlpha(el.colour("bottom", top), a));
            break;
        }
        if (fill || name.empty()) { // a box of that colour (or of its style's background), with the style's border
            Style filled = s;
            if (fill) filled.background = *fill;
            box(r, rectOf(index), filled, selected(el) ? Look::Selected : Look::Idle, a, false, true);
            break;
        }
        if (name.starts_with("save:")) { // a save's picture ({save.image}: one on the page of saves open), as its fit= has it
            const auto& cards = engine_.slots().cards();
            const auto n = static_cast<std::size_t>(std::max(0, toInt(Value(name.substr(5)))));
            if (n >= 1 && n <= cards.size() && cards[n - 1].thumbnail) {
                float w = 0, h = 0;
                SDL_GetTextureSize(cards[n - 1].thumbnail.get(), &w, &h);
                image(r, Image{.texture = cards[n - 1].thumbnail.get(), .src = {0, 0, w, h}}, el, el.option("fit", "stretch"), 0.5f, 0.5f, a);
            }
            break;
        }
        if (drawMovie(r, index, name, a)) break; // (a movie by that name: it plays)
        const Image pic = engine_.image(name, {.eyes = engine_.gameSettings().blink ? blinking(name, now()) : 0.0f});
        if (!pic) break;
        // repeat=across|down|both: copies of it edge to edge, all over the screen that way (one moved its own
        // width over and over goes on without a seam).
        const std::string repeat = lower(el.option("repeat", "no")), fit = el.option("fit", "stretch");
        const bool across = repeat == "across" || repeat == "both", down = repeat == "down" || repeat == "both";
        SDL_FRect box{rectOf(index).x, rectOf(index).y, rectOf(index).w > 0.0f ? rectOf(index).w : pic.w(), rectOf(index).h > 0.0f ? rectOf(index).h : pic.h()};
        // scroll=left|right|up|down: it slides that way on and on, by its own size every scroll_round= seconds
        // (with repeat=, without a seam). Still: it stands where it starts.
        if (const std::string scroll = lower(el.option("scroll", "no")); scroll != "no" && !still()) {
            const float t = std::fmod(now() / std::max(0.1f, el.number("scroll_round", 10.0f)), 1.0f);
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
                image(r, pic, el, fit, 0.5f, 0.5f, a, false, &at);
            }
        break;
    }

    case ElementKind::Face: // the speaker's face image
        if (const auto line = game.line(def_->name); line && !line->faceBase.empty()) {
            Image pic = engine_.image(line->face, line->faceFrame);
            if (!pic) pic = engine_.image(line->faceBase, line->faceFrame);
            if (pic) {
                std::optional<RenderClip> clip; // (cover and original: what's past the part's edges is cut off)
                if (const std::string fit = el.option("fit", "contain"); fit == "cover" || fit == "original") clip.emplace(r, rectOf(index));
                image(r, pic, el, el.option("fit", "contain"), factor(s.align), factor(s.alignVertically), a, line->character && line->character->faceFlip);
            } else if (engine_.gameSettings().developer) { // stand-in: a box in their colour with the file it expects
                const SDL_Color c = line->speakerColour;
                draw::rect(r, rectOf(index), withAlpha({static_cast<Uint8>(c.r * 0.45f), static_cast<Uint8>(c.g * 0.45f), static_cast<Uint8>(c.b * 0.45f), 230}, a));
                draw::outline(r, rectOf(index), withAlpha(c, a));
                Style label = s;
                label.size = 13.0f;
                label.align = label.alignVertically = Align::Centre;
                text(r, st.text, std::format("{}\nimages/{}.png", line->speaker, line->face), rectOf(index), label, theme::text, ta);
            }
        }
        break;

    case ElementKind::Text: {
        const auto line = game.line(def_->name);
        const SDL_Color colour = engine_.settings().highContrast ? kContrastText
                              : s.speakerColour                ? (line ? line->speakerColour : game.speakerColour())
                                                              : textColour(s, selected(el) ? Look::Selected : Look::Idle);
        SDL_FRect in = rectOf(index);
        if (st.ownWords) in.w = 0.0f; // (as big as its words: drawn as they are, not wrapped to their own width)
        RichText label = textOf(index, in.w > 0.0f ? std::max(1.0f, in.w - 2 * s.padding) : 0.0f);
        text(r, st.text, label.text, in, s, colour, ta, TextBlock::kAll, std::move(label.spans));
        break;
    }

    case ElementKind::Button: {
        const SDL_FRect hit = st.hit.value_or(rectOf(index)); // (one sized by its image: its usual size counts too)
        const auto [look, on, chosen] = buttonLook(index, hit);
        const bool held = held_ == index;
        // A chosen one pointed at: what its pointed-at look sets, the rest as chosen (its chosen image stays).
        Style both;
        if (look == Look::Hover && chosen) {
            both = s;
            if (both.pointed.image.empty()) both.pointed.image = s.chosen.image;
            if (!both.pointed.background) both.pointed.background = s.chosen.background;
            if (!both.pointed.border) both.pointed.border = s.chosen.border;
            if (!both.pointed.colour) both.pointed.colour = s.chosen.colour;
        }
        const Style& looks = look == Look::Hover && chosen ? both : s;
        const float left = held && heldMoved_ ? 0.35f : 1.0f; // (its copy follows the pointer)
        box(r, rectOf(index), looks, look, a * left, chosen);
        if (const std::string letter = keyLetter(el); el.text.empty() && !letter.empty()) // (an on-screen keyboard's key: the letter it types)
            text(r, st.text, letter, rectOf(index), looks, textColour(looks, look, chosen), ta * left);
        else if (!el.text.empty()) {
            RichText label = wordsOf(index, el.text);
            SDL_FRect in = rectOf(index);
            if (st.ownWords) in.w = 0.0f; // (as big as its words: they're drawn as they are, not wrapped to their own width)
            text(r, st.text, label.text, in, looks, textColour(looks, look, chosen), ta * left, TextBlock::kAll, std::move(label.spans));
        }
        if (on) targets_.push_back({{index}, hit, true});
        break;
    }

    case ElementKind::Dialogue:
        if (const auto line = game.line(def_->name)) {
            Font& font = styleFont(s);
            st.text.set(font, line->text, rectOf(index).w, s.lineSpacing);
            st.text.setSpans(*line->spans);
            const SDL_Color colour = engine_.settings().highContrast ? kContrastText : s.speakerColour ? line->speakerColour : line->textColour.value_or(s.colour);
            // Word and line reveal show the whole word / line being typed, but never past a {pause}.
            const std::size_t shown = std::max(line->revealed, std::min(st.text.visible(line->revealed, line->mode), line->limit));
            st.text.drawIn(r, rectOf(index), factor(s.align), factor(s.alignVertically), withAlpha(colour, ta), shown, s.shadow);
        }
        break;

    case ElementKind::Flowchart:
        drawFlowchart(r, index, alpha);
        break;

    case ElementKind::Control: { // one key slot of a control; click it, then press the new key
        const std::size_t slot = Engine::slotOf(el);
        const bool waiting = engine_.rebinding(el.text, slot);
        const Look look = waiting ? Look::Selected : focused(index, 0) ? Look::Hover : Look::Idle;
        box(r, rectOf(index), s, look, a);
        text(r, st.text, waiting ? el.option("waiting") : engine_.keyLabel(el.text, slot), rectOf(index), s, textColour(s, look), ta);
        targets_.push_back({{index}, rectOf(index), true});
        break;
    }
    case ElementKind::Input:
        drawInput(r, index, alpha);
        break;
    case ElementKind::Slider:
        drawSlider(r, index, alpha);
        break;
    }
}

// Draws an image into the element's box: fit=stretch, contain (all of it), cover (fills the box)
// or original (its own pixels, the game's: what's past the box, the caller cuts off), placed by
// `ax` / `ay` (0 = left / top, 1 = right / bottom). flip=yes mirrors it.
void ScreenView::image(SDL_Renderer* r, const Image& pic, const Element& el, std::string_view fit, float ax, float ay, float alpha,
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

// The flowchart: every point reached (choices and endings) as a tree, each under the place the player
// first came from, with the option picked there above it. A choice's options not tried yet hang under it
// as stubs: locked ones say what they need, and ones that opened since (their condition is true now) get
// a mark. Clicking a place goes back to it; a stub goes to its choice. It moves with the mouse wheel and
// by dragging, and starts at the current place.
void ScreenView::drawFlowchart(SDL_Renderer* r, std::size_t index, float alpha) {
    const Element& el = def_->elements[index];
    const Style& s = styleOf(index);
    ElementState& st = state_[index];
    const Flowchart& flowchart = engine_.flowchart();
    const auto& nodes = flowchart.nodes();
    const Script& script = engine_.script();
    if (nodes.empty()) return; // (points appear as the story reaches them)
    const Grid g = grid(el, {.itemW = 220.0f, .itemH = 60.0f, .gapX = 26.0f, .gapY = 56.0f});
    const float nodeW = g.itemW, nodeH = g.itemH, gapX = g.gapX, gapY = g.gapY;

    struct Item {
        int node = -1;     // the point (-1: a stub)
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
    auto lookupAt = [&](const Flowchart::Node& n) {
        auto vars = std::make_shared<Variables>(Game::savedVariables(n.state));
        const Lookup& now = engine_.lookup();
        return Lookup([vars, now](std::string_view name) -> Value {
            if (auto it = vars->find(name); it != vars->end()) return it->second;
            return now(name);
        });
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Flowchart::Node& n = nodes[i];
        Item& item = items[i];
        item.node = static_cast<int>(i);
        const auto pc = engine_.pointCommand(n.id);
        const Command* c = pc ? &script.code[*pc].command : nullptr;
        const Lookup lookup = lookupAt(n);
        constexpr std::string_view ending = "persistent.ending."; // (an ending reached: set persistent.ending.NAME to yes)
        if (const auto* set = c ? std::get_if<cmd::Set>(c) : nullptr; set && set->variable.starts_with(ending)) {
            const auto& list = script.records(RecordKind::Ending);
            if (const Script::Record* e = script.record(RecordKind::Ending, std::string_view(set->variable).substr(ending.size()))) item.title = std::format("{}. {}", e - list.data() + 1, e->title);
        } else if (const auto* menu = c ? std::get_if<cmd::Menu>(c) : nullptr) {
            for (const cmd::Choice& choice : menu->choices) item.title += (item.title.empty() ? "" : " / ") + fillValues(choice.text, lookup); // (its options)
            // Its options not tried yet: stubs under it.
            for (const Flowchart::Option& o : n.options) {
                if (o.index < 0 || static_cast<std::size_t>(o.index) >= menu->choices.size()) continue;
                if (std::ranges::any_of(flowchart.edges(), [&](const Flowchart::Edge& e) { return e.from == n.id && e.option == o.index; })) continue;
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
    for (const Flowchart::Edge& e : flowchart.edges()) {
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
    const std::string current = engine_.playing() ? engine_.game().flowchartPoint() : std::string();
    if (!st.centred) { // start at the current place (else the first one)
        st.centred = true;
        auto it = at.find(current);
        if (it == at.end()) it = at.find(nodes.front().id);
        if (it != at.end()) {
            const Item& item = items[static_cast<std::size_t>(it->second)];
            st.scrollX = item.x * (nodeW + gapX) + nodeW * 0.5f + margin - rectOf(index).w * 0.5f;
            st.scroll = static_cast<float>(item.depth) * (nodeH + gapY) + nodeH * 0.5f - rectOf(index).h * 0.5f + margin;
        }
    }
    const float spareX = rectOf(index).w - width - 2 * margin; // (room to spare: in the middle)
    settle(index, height + 2 * margin - rectOf(index).h);
    st.reachX = width + 2 * margin;
    const float wantX = st.scrollX;
    st.scrollX = spareX >= 0.0f ? 0.0f : std::clamp(st.scrollX, 0.0f, -spareX);
    if (st.scrollX != wantX) st.glideX = 0.0f;
    const float shiftX = spareX >= 0.0f ? spareX * 0.5f : -st.scrollX;
    auto boxAt = [&](const Item& item) {
        return SDL_FRect{rectOf(index).x + margin + shiftX + item.x * (nodeW + gapX), rectOf(index).y + margin - st.scroll + static_cast<float>(item.depth) * (nodeH + gapY),
                         nodeW, nodeH};
    };

    const RenderClip clip(r, rectOf(index));
    const SDL_Color line = withAlpha(el.colour("line", SDL_Color{92, 96, 130, 255}), alpha);
    const Style& choiceStyle = el.substyle("choice_style"); // (an option's words, beside its line)
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
            const auto pc = engine_.pointCommand(nodes[static_cast<std::size_t>(items[static_cast<std::size_t>(item.parent)].node)].id);
            const auto* menu = pc ? std::get_if<cmd::Menu>(&script.code[*pc].command) : nullptr;
            if (menu && static_cast<std::size_t>(item.option) < menu->choices.size()) {
                Font& font = styleFont(choiceStyle);
                st.extra[j].set(font, ellipsise(font, fillValues(menu->choices[static_cast<std::size_t>(item.option)].text, lookupAt(nodes[static_cast<std::size_t>(item.node)])), nodeW * 0.5f + gapX - 10.0f));
                const SDL_FPoint size = st.extra[j].size();
                st.extra[j].draw(r, bx + 7.0f, b.y - size.y - 3.0f, withAlpha(choiceStyle.colour, alpha * choiceStyle.opacity), TextBlock::kAll, choiceStyle.shadow); // beside the line
            }
        }
    }
    for (std::size_t j = 0; j < items.size(); ++j) {
        const Item& item = items[j];
        const SDL_FRect b = boxAt(item);
        const bool stub = item.node < 0;
        const Flowchart::Node* n = stub ? nullptr : &nodes[static_cast<std::size_t>(item.node)];
        st.ids[j] = stub ? nodes[static_cast<std::size_t>(items[static_cast<std::size_t>(item.parent)].node)].id : n->id;
        targets_.push_back({{index, static_cast<int>(j)}, b, true, !inside(b, rectOf(index))}); // (scrolled away: the keys can still go to it)
        if (!overlaps(b, rectOf(index))) continue;
        const Style& bs = stub ? el.substyle("unreached_style") : n->kind == Flowchart::Kind::Ending ? el.substyle("ending_style") : s;
        const Look look = n && n->id == current ? Look::Selected : focused(index, static_cast<int>(j)) ? Look::Hover : Look::Idle;
        box(r, b, bs, look, boxAlpha(bs, alpha));
        std::string label = item.title;
        if (item.locked && !item.note.empty()) label = item.title + "  (" + item.note + ")";
        RichText rt = rich(label);
        // (in the lines its box has room for: a long one, a choice's many options, would spill over the points around it)
        Font& font = styleFont(bs);
        const float lineH = std::max(1.0f, font.lineHeight() * bs.lineSpacing);
        const auto most = static_cast<std::size_t>(std::max(1.0f, std::floor((b.h - 2 * bs.padding) / lineH)));
        rt.text = cutToLines(font, rt.text, std::max(0.0f, b.w - 2 * bs.padding), bs.lineSpacing, most);
        const std::size_t length = utf8::length(rt.text);
        for (TextSpan& span : rt.spans) span.begin = std::min(span.begin, length), span.end = std::min(span.end, length);
        text(r, st.items[j], rt.text, b, bs, textColour(bs, look), alpha * bs.opacity, TextBlock::kAll, std::move(rt.spans));
        if (item.fresh) { // newly open: a small diamond in the corner
            const float cx = b.x + b.w - 9.0f, cy = b.y + 9.0f, k = 5.0f;
            draw::quad(r, {cx, cy - k}, {cx + k, cy}, {cx, cy + k}, {cx - k, cy}, withAlpha(SDL_Color{255, 200, 90, 255}, alpha));
        }
    }
    drawScrollbar(r, index, alpha);
}

// An image part's movie: one named so (not an image) plays in it, as its fit= has it, again and again unless loop=no.
bool ScreenView::drawMovie(SDL_Renderer* r, std::size_t index, const std::string& name, float alpha) {
    const Element& el = def_->elements[index];
    ElementState& st = state_[index];
    if (name != st.movieName) { // (looked for once a name: an image's isn't a movie)
        st.movie.reset();
        st.movieName = name;
        if (!engine_.assets().hasImage(name))
            if (const auto file = engine_.assets().findMovie(name)) {
                st.movie = std::make_unique<Movie>(r, engine_.sound(), *file, parseBool(el.option("loop", "yes")).value_or(true), std::clamp(el.number("volume", 1.0f), 0.0f, 1.0f));
                if (!*st.movie) st.movie.reset();
            }
        st.movieTime = now();
    }
    if (!st.movie) return false;
    st.movieFrame = engine_.frameNumber();
    st.movie->update(now() - std::exchange(st.movieTime, now()));
    if (SDL_Texture* frame = st.movie->texture()) {
        float w = 0, h = 0;
        SDL_GetTextureSize(frame, &w, &h);
        const std::string fit = el.option("fit", "stretch");
        std::optional<RenderClip> clip; // (cover and original: what's past the part's edges is cut off)
        if (fit == "cover" || fit == "original") clip.emplace(r, rectOf(index));
        image(r, Image{.texture = frame, .src = {0, 0, w, h}}, el, fit, 0.5f, 0.5f, alpha);
    }
    return true;
}

} // namespace vn
