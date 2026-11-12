// A screen's parts laid out: places, sizes and shares, stacks, grids, scrolling and its scrollbars; and where each
// part is, as the developer tools ask.
#include "Engine.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace vn {

ScreenView::Grid ScreenView::grid(const Element& el, Grid d) const {
    Grid g{.columns = std::max(1, static_cast<int>(el.number("columns", static_cast<float>(d.columns))))};
    const SDL_FRect box = boxOf(indexOf(el));
    g.gapX = amount(el, "spacing_across", box.w).value_or(amount(el, "spacing", box.w).value_or(d.gapX));
    g.gapY = amount(el, "spacing_down", box.h).value_or(amount(el, "spacing", box.h).value_or(d.gapY));
    const auto columns = static_cast<float>(g.columns);
    const float room = el.kind == ElementKind::Container ? 0.0f : barRoom(indexOf(el)); // (a list's items clear of its scrollbar; a container's parts are where they're put)
    const float wide = rectOf(indexOf(el)).w - room;
    g.itemW = share(el, "item_width", true, d.itemW > 0 ? d.itemW : (wide - g.gapX * (columns - 1)) / columns);
    g.itemH = share(el, "item_height", false, d.itemH);
    // (a list with item_width= and no columns=: as many as come nearest to it in its width, at least one, each as wide as
    // that leaves, its shape kept: three across a screen, one in a phone)
    if (el.kind == ElementKind::Container && !el.props.contains("columns") && el.props.contains("item_width") && g.itemW > 0.0f) {
        g.columns = std::max(1, static_cast<int>(std::lround((wide + g.gapX) / (g.itemW + g.gapX))));
        const float w = std::max(1.0f, (wide - g.gapX * static_cast<float>(g.columns - 1)) / static_cast<float>(g.columns));
        if (g.itemH > 0.0f) g.itemH *= w / g.itemW;
        g.itemW = w;
    }
    return g;
}

float ScreenView::share(const Element& el, std::string_view key, bool across, float fallback) const {
    const SDL_FRect box = boxOf(indexOf(el));
    return amount(el, key, across ? box.w : box.h).value_or(fallback);
}

std::optional<float> ScreenView::amount(const Element& el, std::string_view key, float whole) {
    const std::string v = el.option(key);
    if (v.empty()) return std::nullopt;
    if (lower(v).ends_with("px")) return parseNumber(std::string_view(v).substr(0, v.size() - 2));
    if (const auto n = parseNumber(v)) return *n * whole;
    return std::nullopt;
}

float ScreenView::sized(std::size_t index, bool across) const {
    const Element& el = def_->elements[index];
    const std::optional<float>& v = across ? el.shareWidth : el.shareHeight;
    if (!v) return 0.0f;
    const float of = shareOf(index, across);
    if (!(across ? el.pixelWidth : el.pixelHeight)) return *v * of;
    return across ? std::min(*v, of) : *v; // (pixels: a width no wider than the box)
}

bool ScreenView::fillsRest(std::size_t index, bool down) const {
    const Element& el = def_->elements[index];
    return isBox(el.kind) && !el.fitsParts() && !isItemBox(index) && !(down ? el.shareHeight : el.shareWidth).has_value();
}

SDL_FRect ScreenView::boxOf(std::size_t index) const {
    const int p = index < def_->elements.size() ? def_->elements[index].parent : -1;
    return p >= 0 && static_cast<std::size_t>(p) < index ? rectOf(static_cast<std::size_t>(p)) : SDL_FRect{0.0f, 0.0f, engine_.canvas().w, engine_.canvas().h};
}

void ScreenView::layout() {
    const auto& elements = def_->elements; // (the parts they're in come first)
    const std::size_t n = elements.size();
    // Where the next part goes in a box whose parts go one after another (stack=), and the next item's box
    // in a list: across and down from its top left (the row's height so far, and the column it's in).
    struct Flow {
        float x = 0.0f, y = 0.0f, rowH = 0.0f;
        int column = 0;
    };
    std::vector<Flow> flows;
    std::vector<char> left; // (in a box whose parts go one after another, but not shown: it takes no room, nor do the parts in it)
    auto place = [&](std::size_t i) {
        const Bound bound(*this, i);
        const Element& el = elements[i];
        const ElementState& st = state_[i];
        const SDL_FRect box = boxOf(i);
        const auto p = static_cast<std::size_t>(el.parent);
        left[i] = el.parent >= 0 && left[p];
        if (isItemBox(i)) { // an item's: where its list has room for the next (as high as item_height=, else its parts)
            const Grid g = grid(elements[p], {});
            const float w = g.itemW, h = g.itemH > 0.0f ? g.itemH : std::max(0.0f, st.ownH);
            Flow& f = flows[p];
            if (f.column >= g.columns) f = {0.0f, f.y + f.rowH + g.gapY, 0.0f, 0};
            rectOf(i) = {std::round(box.x + static_cast<float>(f.column) * (w + g.gapX)), std::round(box.y + f.y), std::round(w), std::round(h)};
            f.rowH = std::max(f.rowH, h);
            ++f.column;
            return;
        }
        // (a container without a size: all of its box, or in a box whose parts go one after another, the room the
        // others leave that way, shared by the ones like it; with fit=parts, as big as its parts, once they're placed)
        const bool container = isBox(el.kind), parts = container && el.fitsParts();
        bool down = true;
        const bool stacked = el.parent >= 0 && stacks(p, &down);
        const float rest = stacked && fillsRest(i, down) ? state_[p].rest : -1.0f;
        Flow& f = flows[el.parent >= 0 ? p : i];
        const float w = el.shareWidth ? sized(i, true) : parts && st.ownW >= 0.0f ? st.ownW : container ? (!down && rest >= 0.0f ? rest : box.w) : 0.0f;
        const float h = el.shareHeight ? sized(i, false) : parts && st.ownH >= 0.0f ? st.ownH : container ? (down && rest >= 0.0f ? rest : box.h) : 0.0f;
        rectOf(i) = {std::round(box.x + placeEdge(el.placeX.value_or(-1.0f), w, box.w)), std::round(box.y + placeEdge(-el.placeY.value_or(1.0f), h, box.h)),
                   std::round(w), std::round(h)}; // (on whole pixels: sharp)
        sizeOwn(i);
        if (stacked) { // one after the one before it (its place the other way)
            if (left[i] || !shows(i)) {
                left[i] = 1;
                return;
            }
            const Element& in = elements[p];
            const SDL_FRect outer = boxOf(p);
            const float gap = down ? amount(in, "spacing_down", outer.h).value_or(amount(in, "spacing", outer.h).value_or(0.0f))
                                   : amount(in, "spacing_across", outer.w).value_or(amount(in, "spacing", outer.w).value_or(0.0f));
            // (one that takes no room, an empty list, takes no gap either)
            if (down) rectOf(i).y = std::round(box.y + f.y), f.y += rectOf(i).h > 0.0f ? rectOf(i).h + gap : 0.0f;
            else {
                // (onto the next row when it doesn't fit what's left of this one: in a box with a width of its own that
                // doesn't scroll; its rows one under another, as far apart as its parts going down would be)
                if (f.x > 0.0f && f.x + rectOf(i).w > box.w + 0.5f && !(in.fitsParts() && !in.shareWidth) && !in.scrolls()) {
                    f.y += f.rowH + amount(in, "spacing_down", outer.h).value_or(amount(in, "spacing", outer.h).value_or(0.0f));
                    f.x = f.rowH = 0.0f;
                }
                rectOf(i).x = std::round(box.x + f.x);
                rectOf(i).y += f.y;
                f.rowH = std::max(f.rowH, rectOf(i).h);
                f.x += rectOf(i).w > 0.0f ? rectOf(i).w + gap : 0.0f;
            }
        }
    };
    for (int pass = 0; pass < 6; ++pass) {
        flows.assign(n, {});
        left.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) place(i);
        // A container with fit=parts and no width (or height): as wide (high) as its biggest part that way
        // (one with a size of its own: not a share of the box, nor a container filling it); the parts are
        // placed within that by their places. One whose parts go one after another (and an item's box
        // without item_height=): as far as they go. When a size changes, everything is placed again.
        bool changed = false;
        for (std::size_t i = 0; i < n; ++i) {
            const Element& el = elements[i];
            const bool item = isItemBox(i);
            if (!isBox(el.kind) || !(el.fitsParts() || item) || (el.shareWidth && el.shareHeight)) continue;
            if (item && grid(elements[static_cast<std::size_t>(el.parent)], {}).itemH > 0.0f) continue;
            bool down = true;
            const bool flow = stacks(i, &down) || !el.option("list").empty(); // (a list's items: one after another)
            if (!el.option("list").empty()) down = true;
            float w = 0.0f, h = 0.0f;
            for (std::size_t j = i + 1; j < n; ++j) {
                if (!within(j, i) || left[j]) continue;
                const Element& part = elements[j];
                // (one filling its box, without a size of its own, doesn't count; one with a width= or height= does: a share of the box this one's in)
                const bool fills = isBox(part.kind) && !part.fitsParts() && !isItemBox(j) && !part.shareWidth && !part.shareHeight;
                if (fills) continue;
                const SDL_FRect& b = rectOf(j);
                if (flow && down) {
                    h = std::max(h, b.y + b.h - rectOf(i).y);
                    w = std::max(w, b.w);
                } else if (flow) { // (its rows, when they go onto more than one: down to the last)
                    w = std::max(w, b.x + b.w - rectOf(i).x);
                    h = std::max(h, b.y + b.h - rectOf(i).y);
                } else {
                    w = std::max(w, b.w);
                    h = std::max(h, b.h);
                }
            }
            ElementState& st = state_[i];
            if (std::abs(w - st.ownW) > 0.5f || std::abs(h - st.ownH) > 0.5f) st.ownW = w, st.ownH = h, changed = true;
        }
        // A box whose parts go one after another: the room the parts filling it have that way (what its others and the
        // gaps leave, shared; one as big as its parts that way has none to fill).
        for (std::size_t i = 0; i < n; ++i) {
            bool down = true;
            if (!stacks(i, &down)) continue;
            const Element& el = elements[i];
            float rest = -1.0f;
            if (!el.fitsParts() || (down ? el.shareHeight : el.shareWidth)) {
                const SDL_FRect outer = boxOf(i);
                const float gap = down ? amount(el, "spacing_down", outer.h).value_or(amount(el, "spacing", outer.h).value_or(0.0f))
                                       : amount(el, "spacing_across", outer.w).value_or(amount(el, "spacing", outer.w).value_or(0.0f));
                float used = 0.0f;
                int fillers = 0, placed = 0;
                for (std::size_t j = i + 1; j < n && within(j, i); ++j) {
                    if (elements[j].parent != static_cast<int>(i) || left[j]) continue;
                    const float size = down ? rectOf(j).h : rectOf(j).w;
                    if (fillsRest(j, down)) ++fillers, ++placed;
                    else if (size > 0.0f) used += size, ++placed;
                }
                const float room = down ? rectOf(i).h : rectOf(i).w;
                if (fillers > 0) rest = std::max(0.0f, (room - used - gap * static_cast<float>(std::max(0, placed - 1))) / static_cast<float>(fillers));
            }
            if (std::abs(rest - state_[i].rest) > 0.5f) state_[i].rest = rest, changed = true;
        }
        if (!changed) break;
    }
    // A container with position=bottom (or centre): what's in it, when it doesn't fill it, at its bottom (in its middle).
    for (std::size_t i = 0; i < n; ++i) {
        const Element& el = elements[i];
        const std::string position = el.kind == ElementKind::Container ? el.option("position", "top") : "top";
        if (position == "top" || el.fitsParts()) continue;
        float bottom = 0.0f;
        for (std::size_t j = i + 1; j < n && within(j, i); ++j)
            if (!left[j]) bottom = std::max(bottom, rectOf(j).y + rectOf(j).h - rectOf(i).y);
        if (const float room = rectOf(i).h - bottom; room > 0.0f) {
            const float by = std::round(position == "bottom" ? room : room * 0.5f);
            for (std::size_t j = i + 1; j < n && within(j, i); ++j) rectOf(j).y += by;
        }
    }
    // A container with scroll=yes: what's in it moves up (and left) by how far it's scrolled, as far as its
    // parts reach past its box (the parts in parts in it too: the containers in it come first, scrolled by it).
    // One with position=bottom that was at its end stays there as more comes (and starts there).
    for (std::size_t i = 0; i < n; ++i) {
        const Element& box = elements[i];
        if (!isBox(box.kind) || !box.scrolls()) continue;
        ElementState& st = state_[i];
        st.reach = st.reachX = 0.0f;
        for (std::size_t j = i + 1; j < n; ++j)
            if (within(j, i) && !left[j]) {
                const SDL_FRect& b = rectOf(j); // (as laid out: a part without a size of its own takes none)
                st.reach = std::max(st.reach, b.y + b.h - rectOf(i).y);
                st.reachX = std::max(st.reachX, b.x + b.w - rectOf(i).x);
            }
        const float most = std::max(0.0f, st.reach - rectOf(i).h);
        const bool atEnd = box.option("position") == "bottom";
        if (atEnd && (st.keptScroll < 0.0f || (st.scroll == st.keptScroll && st.keptScroll >= st.keptMost - 1.0f))) st.scroll = most;
        glideStep(i); // (gliding on after a drag: until it's slow, or at an end)
        const float wantY = st.scroll, wantX = st.scrollX;
        st.scroll = std::clamp(st.scroll, 0.0f, most);
        st.scrollX = std::clamp(st.scrollX, 0.0f, std::max(0.0f, st.reachX - rectOf(i).w));
        if (atEnd) st.keptScroll = st.scroll, st.keptMost = most;
        if (st.scroll != wantY) st.glideY = 0.0f;
        if (st.scrollX != wantX) st.glideX = 0.0f;
        if (st.scroll > 0.0f || st.scrollX > 0.0f)
            for (std::size_t j = i + 1; j < n; ++j)
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
    if (still()) st.glideX = st.glideY = 0.0f; // (still: it stops where it was let go)
    if (st.glideX == 0.0f && st.glideY == 0.0f) return;
    const Element& el = def_->elements[index];
    const float t = now(), dt = std::clamp(t - st.glideAt, 0.0f, 0.1f);
    st.glideAt = t;
    st.scroll += st.glideY * dt;
    st.scrollX += st.glideX * dt;
    const float keep = std::exp(-dt * 2.5f / std::max(0.05f, el.number("glide_distance", engine_.gameSettings().glideDistance))); // (glide_distance=200%: slowing half as fast, twice as far; none: the game's)
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
    const float t = std::max(1.0f, el.number("scrollbar_thickness", engine_.gameSettings().scrollbarThickness)), in = t * 0.5f; // (its thickness, set in from the edges by half of it)
    const bool down = st.reach > box.h, side = st.reachX > box.w; // (both: each stops short of the other)
    if (across) return {box.x + in, box.y + box.h - t - in, std::max(0.0f, box.w - t - (down ? t + in : 0.0f)), t};
    return {box.x + box.w - t - in, box.y + in, t, std::max(0.0f, box.h - t - (side ? t + in : 0.0f))};
}

float ScreenView::barRoom(std::size_t index) const {
    const Element& el = def_->elements[index];
    if (lower(el.option("scrollbar", "no")) != "yes") return 0.0f;
    return 2.0f * std::max(1.0f, el.number("scrollbar_thickness", engine_.gameSettings().scrollbarThickness)); // (the bar, and half of it each side)
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

// Scrolls `t` into view: in a part that scrolls its items itself (the flowchart), and in the containers it's in.
void ScreenView::reveal(const Target& t) {
    if (t.spot.element >= def_->elements.size()) return;
    if (const Element& el = def_->elements[t.spot.element]; el.kind != ElementKind::Container && el.dragDirection() != 0.0f) {
        ElementState& st = state_[t.spot.element];
        const SDL_FRect& box = rectOf(t.spot.element);
        const float way = el.dragDirection(); // (scrolling on moves its items up)
        if (t.rect.y < box.y) st.scroll += way * (box.y - t.rect.y);
        else if (t.rect.y + t.rect.h > box.y + box.h) st.scroll -= way * (t.rect.y + t.rect.h - box.y - box.h);
        if (t.rect.x < box.x) st.scrollX -= box.x - t.rect.x;
        else if (t.rect.x + t.rect.w > box.x + box.w) st.scrollX += t.rect.x + t.rect.w - box.x - box.w;
        st.glideX = st.glideY = 0.0f;
    }
    for (int p = def_->elements[t.spot.element].parent; p >= 0; p = def_->elements[static_cast<std::size_t>(p)].parent) {
        const auto at = static_cast<std::size_t>(p);
        const Element& box = def_->elements[at];
        if (!isBox(box.kind) || !box.scrolls()) continue;
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
        if (!isBox(box.kind) || box.edges() == "show") continue;
        SDL_FRect rect = rectOf(at);
        if (shifts && at < shifts->size()) rect.x += (*shifts)[at].x, rect.y += (*shifts)[at].y;
        if (!out) out = rect;
        else if (!SDL_GetRectIntersectionFloat(&*out, &rect, &*out)) out = SDL_FRect{};
    }
    return out;
}

// A part without a width (or a height) is as big as what it shows, and placed by that size: a button's
// image or words (sizeButton), an image's pixels, a text's words with its padding.
void ScreenView::sizeOwn(std::size_t index) {
    const Element& el = def_->elements[index];
    if (el.kind == ElementKind::Button) return sizeButton(index);
    ElementState& st = state_[index];
    st.ownWords = false;
    if ((el.shareWidth && el.shareHeight) || (el.kind != ElementKind::Image && el.kind != ElementKind::Text)) return;
    SDL_FPoint size{0.0f, 0.0f};
    if (el.kind == ElementKind::Image) { // (as big as its image; with a width but no height, or the other way, its shape kept)
        const std::string name = engine_.script().visualOf(el.text.contains('{') ? interpolate(el.text) : el.text, el.option("expression"));
        if (!name.empty() && !parseColour(name, &engine_.palette()) && !parseGradient(name, &engine_.palette()))
            if (const Image pic = engine_.image(name)) {
                size = {pic.w(), pic.h()};
                if (el.shareWidth && pic.w() > 0.0f) size.y = sized(index, true) * pic.h() / pic.w();
                else if (el.shareHeight && pic.h() > 0.0f) size.x = sized(index, false) * pic.w() / pic.h();
            }
    } else { // (its words wrapped to its width=, or to width_at_most= and as wide as their lines then; else as they come)
        const Style& s = styleOf(index);
        const float most = el.shareWidth ? 0.0f : widest(index);
        const float wrap = el.shareWidth ? std::max(1.0f, sized(index, true) - 2 * s.padding) : most > 0.0f ? std::max(1.0f, most - 2 * s.padding) : 0.0f;
        st.text.set(styleFont(s), textOf(index, wrap).text, wrap, s.lineSpacing);
        st.ownWords = !el.shareWidth && most <= 0.0f;
        const SDL_FPoint words = st.text.size();
        if (words.x > 0.0f || words.y > 0.0f) size = {(most > 0.0f ? std::ceil(words.x) + 1.0f : words.x) + 2 * s.padding, words.y + 2 * s.padding};
    }
    if (size.x <= 0.0f && size.y <= 0.0f) return;
    const SDL_FRect box = boxOf(index);
    if (!el.shareWidth) rectOf(index).w = std::round(size.x), rectOf(index).x = std::round(box.x + placeEdge(el.placeX.value_or(-1.0f), rectOf(index).w, box.w));
    if (!el.shareHeight) rectOf(index).h = std::round(size.y), rectOf(index).y = std::round(box.y + placeEdge(-el.placeY.value_or(1.0f), rectOf(index).h, box.h));
}

// A text that's clicked (a button) without a width (or a height) is as big as the image it shows now, or (with none) its
// words and its padding as a text's are (wrapped to width_at_most=, lines= at most), and placed by that size. It's
// clicked within its usual size as well, so a smaller pointed-at or pressed image can't let the pointer slip out of it.
void ScreenView::sizeButton(std::size_t index) {
    const Element& el = def_->elements[index];
    ElementState& st = state_[index];
    st.ownWords = false;
    if (el.shareWidth && el.shareHeight) {
        st.hit.reset();
        return;
    }
    const ButtonLook now = buttonLook(index, st.hit.value_or(rectOf(index)));
    auto sizeOf = [&](Look look, bool chosen) -> SDL_FPoint {
        if (const std::string image = lookImage(styleOf(index), look, chosen); !image.empty())
            if (const Image pic = engine_.image(image)) {
                st.ownWords = false;
                return {pic.w(), pic.h()};
            }
        const Style& s = styleOf(index);
        const float most = el.shareWidth ? 0.0f : widest(index);
        const float wrap = el.shareWidth ? std::max(1.0f, sized(index, true) - 2 * s.padding) : most > 0.0f ? std::max(1.0f, most - 2 * s.padding) : 0.0f;
        st.text.set(styleFont(s), textOf(index, wrap).text, wrap, s.lineSpacing);
        st.ownWords = !el.shareWidth && most <= 0.0f;
        const SDL_FPoint words = st.text.size();
        return {(most > 0.0f ? std::ceil(words.x) + 1.0f : words.x) + 2 * s.padding, words.y + 2 * s.padding};
    };
    const SDL_FRect box = boxOf(index);
    auto placed = [&](SDL_FPoint size) {
        SDL_FRect at = rectOf(index);
        if (!el.shareWidth) at.w = std::round(size.x), at.x = std::round(box.x + placeEdge(el.placeX.value_or(-1.0f), at.w, box.w));
        if (!el.shareHeight) at.h = std::round(size.y), at.y = std::round(box.y + placeEdge(-el.placeY.value_or(1.0f), at.h, box.h));
        return at;
    };
    const SDL_FRect usual = placed(sizeOf(Look::Idle, false)); // (the current look's last: ownWords is its)
    rectOf(index) = now.look == Look::Idle ? usual : placed(sizeOf(now.look, now.chosen));
    SDL_FRect hit{};
    SDL_GetRectUnionFloat(&rectOf(index), &usual, &hit);
    st.hit = hit;
}

SDL_FRect ScreenView::Grid::cell(const SDL_FRect& area, std::size_t i, float scroll) const {
    const auto col = static_cast<float>(i % static_cast<std::size_t>(columns)), row = static_cast<float>(i / static_cast<std::size_t>(columns));
    return {area.x + col * (itemW + gapX), area.y + row * (itemH + gapY) - scroll, itemW, itemH};
}

float ScreenView::Grid::height(std::size_t items) const {
    const auto rows = static_cast<float>((items + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns));
    return std::max(0.0f, rows * (itemH + gapY) - gapY);
}

SDL_FRect ScreenView::bounds(std::size_t index) {
    const Element& el = def_->elements[index];
    SDL_FRect box = rectOf(index); // (one sized by its text: the text's; an image without a size: its pixels)
    if (box.w > 0.0f && box.h > 0.0f) return box;
    SDL_FPoint own = state_[index].text.size();
    if (el.kind == ElementKind::Image)
        if (const Image pic = engine_.image(engine_.script().visualOf(el.text.contains('{') ? interpolate(el.text) : el.text, el.option("expression")))) own = {pic.w(), pic.h()};
    if (box.w <= 0.0f) box.w = own.x > 0.0f ? own.x : 120.0f;
    if (box.h <= 0.0f) box.h = own.y > 0.0f ? own.y : 40.0f;
    return box;
}

bool ScreenView::stacks(std::size_t index, bool* down) const {
    const Element& el = def_->elements[index];
    if (!isBox(el.kind)) return false;
    const std::string how = el.option("stack");
    if (how != "down" && how != "across") return false;
    if (down) *down = how == "down";
    return true;
}

float ScreenView::shareOf(std::size_t index, bool across) const {
    int p = def_->elements[index].parent;
    while (p >= 0) {
        const auto at = static_cast<std::size_t>(p);
        const Element& box = def_->elements[at];
        const bool sized = across ? box.shareWidth.has_value() : box.shareHeight.has_value();
        const bool fromParts = (box.fitsParts() && !sized) || (!across && isItemBox(at) && grid(def_->elements[static_cast<std::size_t>(box.parent)], {}).itemH <= 0.0f);
        if (!fromParts) break;
        p = box.parent;
    }
    const SDL_FRect box = p >= 0 ? rectOf(static_cast<std::size_t>(p)) : SDL_FRect{0.0f, 0.0f, engine_.canvas().w, engine_.canvas().h};
    return across ? box.w : box.h;
}

float ScreenView::widest(std::size_t index) const {
    const Element& el = def_->elements[index];
    const float most = el.number("width_at_most", 0.0f);
    if (most <= 0.0f) return 0.0f;
    int p = el.parent;
    while (p >= 0 && def_->elements[static_cast<std::size_t>(p)].fitsParts() && !def_->elements[static_cast<std::size_t>(p)].shareWidth) p = def_->elements[static_cast<std::size_t>(p)].parent;
    return most * (p >= 0 ? rectOf(static_cast<std::size_t>(p)).w : engine_.canvas().w);
}

std::size_t ScreenView::asDrawn(std::size_t part) const {
    return firstOf_.empty() ? part : part < firstOf_.size() ? firstOf_[part] : SIZE_MAX;
}

// The script's part `part` as it's drawn now (in a list: its first item's), for the developer tools.
bool ScreenView::drawnPart(std::size_t part) const {
    const std::size_t i = asDrawn(part);
    return i != SIZE_MAX && drawn(i);
}

SDL_FRect ScreenView::partBounds(std::size_t part) {
    const std::size_t i = asDrawn(part);
    return i < state_.size() ? bounds(i) : SDL_FRect{};
}

SDL_FRect ScreenView::partRect(std::size_t part) const {
    const std::size_t i = asDrawn(part);
    return i < state_.size() ? rectOf(i) : SDL_FRect{};
}

SDL_FRect ScreenView::partBox(std::size_t part) const {
    const std::size_t i = asDrawn(part);
    return i < state_.size() ? boxOf(i) : SDL_FRect{0.0f, 0.0f, engine_.canvas().w, engine_.canvas().h};
}

} // namespace vn
