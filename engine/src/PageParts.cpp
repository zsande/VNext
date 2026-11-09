// The pages' page parts: each where its page puts it, or where it was moved (kept as shares of the room over the tools'
// own part, so it keeps its place in any window), drawn bottom to top, each with an edge in the tools' colours, never
// over another. With Layout on their edges light up (what a press would move, brighter), and a page part is moved by
// dragging it and resized by dragging its edges (two that touch: the middle of their border moves both), each side
// going to the sides near it (what's in the page parts takes no pointer then). The layout is kept in the tools' own
// folder (layout.txt); Reset puts a page's back.
#include "DeveloperUi.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <format>
#include <numeric>
#include <optional>
#include <span>

namespace vn {

namespace {

constexpr int kLeftSide = 1, kRightSide = 2, kTopSide = 4, kBottomSide = 8, kWhole = kLeftSide | kRightSide | kTopSide | kBottomSide;

// A place as shares of the room, and back.
SDL_FRect sharesOf(const SDL_FRect& at, const SDL_FRect& room) {
    return {(at.x - room.x) / std::max(1.0f, room.w), (at.y - room.y) / std::max(1.0f, room.h), at.w / std::max(1.0f, room.w), at.h / std::max(1.0f, room.h)};
}
SDL_FRect pixelsOf(const SDL_FRect& shares, const SDL_FRect& room) {
    const float left = std::round(room.x + shares.x * room.w), top = std::round(room.y + shares.y * room.h);
    return {left, top, std::round(room.x + (shares.x + shares.w) * room.w) - left, std::round(room.y + (shares.y + shares.h) * room.h) - top};
}

// The room inside a page part: within its edge (a pixel round it), the room between that and what's in it.
SDL_FRect insideOf(const SDL_FRect& at, float k) {
    const float by = std::max(1.0f, std::round(k)) + std::round(layout::pagePartPad * k);
    return {at.x + by, at.y + by, std::max(0.0f, at.w - 2 * by), std::max(0.0f, at.h - 2 * by)};
}

// The sides of a page part the pointer at (x, y) is on, within `reach` of its edge: the ones a drag there moves.
int sidesAt(const SDL_FRect& at, float x, float y, float reach) {
    if (x < at.x - reach || x > at.x + at.w + reach || y < at.y - reach || y > at.y + at.h + reach) return 0;
    int sides = 0;
    if (std::abs(x - at.x) <= reach) sides |= kLeftSide;
    else if (std::abs(x - (at.x + at.w)) <= reach) sides |= kRightSide;
    if (std::abs(y - at.y) <= reach) sides |= kTopSide;
    else if (std::abs(y - (at.y + at.h)) <= reach) sides |= kBottomSide;
    return sides;
}

// The pointer for moving those sides (all four: the whole page part).
SDL_SystemCursor pointerFor(int sides) {
    if (sides == kWhole) return SDL_SYSTEM_CURSOR_MOVE;
    if (sides == (kLeftSide | kTopSide) || sides == (kRightSide | kBottomSide)) return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    if (sides == (kRightSide | kTopSide) || sides == (kLeftSide | kBottomSide)) return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    return sides & (kLeftSide | kRightSide) ? SDL_SYSTEM_CURSOR_EW_RESIZE : SDL_SYSTEM_CURSOR_NS_RESIZE;
}

// How far the nearest of `sides` is from one of `lines`, within `reach` (else 0): a side moved near another's
// goes to it.
float snapBy(std::initializer_list<float> sides, const std::vector<float>& lines, float reach) {
    float best = reach, by = 0.0f;
    for (const float side : sides)
        for (const float line : lines)
            if (std::abs(line - side) <= best) {
                best = std::abs(line - side);
                by = line - side;
            }
    return by;
}

// Whether two places are over each other (more than touching).
bool overlap(const SDL_FRect& a, const SDL_FRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// `at` clear of every one of `others` (over none of them): moved out of the way (`moves`), or with those of its `sides`
// pulled back, each time by the least that clears the one it's over, within `room` and no smaller than `least` (or
// than it is). None when it can't be.
std::optional<SDL_FRect> clearOf(SDL_FRect at, std::span<const SDL_FRect> others, int sides, bool moves, const SDL_FRect& room, SDL_FPoint least) {
    least = {std::min(least.x, at.w), std::min(least.y, at.h)};
    for (int tries = 0; tries < 32; ++tries) { // (one way out can lead over another: a few goes)
        const auto over = std::ranges::find_if(others, [&](const SDL_FRect& o) { return overlap(at, o); });
        if (over == others.end()) return at;
        const SDL_FRect& o = *over;
        std::optional<SDL_FRect> best;
        float bestBy = FLT_MAX;
        auto consider = [&](float by, const SDL_FRect& then) {
            const bool fits = then.w >= least.x - 0.5f && then.h >= least.y - 0.5f && then.x >= room.x - 0.5f && then.y >= room.y - 0.5f &&
                              then.x + then.w <= room.x + room.w + 0.5f && then.y + then.h <= room.y + room.h + 0.5f;
            if (fits && by < bestBy) {
                bestBy = by;
                best = then;
            }
        };
        const float right = at.x + at.w - o.x, left = o.x + o.w - at.x, bottom = at.y + at.h - o.y, top = o.y + o.h - at.y; // (how far in it each side is)
        if (moves) {
            consider(right, {at.x - right, at.y, at.w, at.h}); // (to its left)
            consider(left, {at.x + left, at.y, at.w, at.h});   // (to its right)
            consider(bottom, {at.x, at.y - bottom, at.w, at.h}); // (over it)
            consider(top, {at.x, at.y + top, at.w, at.h});     // (under it)
        } else {
            if (sides & kRightSide) consider(right, {at.x, at.y, at.w - right, at.h});
            if (sides & kLeftSide) consider(left, {at.x + left, at.y, at.w - left, at.h});
            if (sides & kBottomSide) consider(bottom, {at.x, at.y, at.w, at.h - bottom});
            if (sides & kTopSide) consider(top, {at.x, at.y + top, at.w, at.h - top});
        }
        if (!best) return std::nullopt;
        at = *best;
    }
    return std::nullopt;
}

} // namespace

bool Developer::drawPageParts(DeveloperUi& ui, std::string_view page, std::span<PagePart> pageParts) {
    DeveloperTools& d = tools;
    if (!d.layoutRead) readLayout();
    SDL_Renderer* r = ui.r;
    const float k = ui.k, reach = layout::pagePartGrab * k;
    const SDL_FRect room = d.room;
    const std::string name(page);
    auto keyOf = [&](std::string_view id) { return std::format("{}/{}", page, id); };
    d.shownPage = name;

    // Bottom to top: as the page's order has them (the one moved last on top), then the rest as the page lists them;
    // each where it was put, else where the page puts it.
    struct Shown {
        PagePart* pagePart;
        std::string key;
        SDL_FRect at;
    };
    std::vector<Shown> shown;
    auto show = [&](PagePart& p) {
        if (std::ranges::contains(shown, &p, &Shown::pagePart)) return;
        const auto placed = d.pageParts.find(keyOf(p.id));
        shown.push_back({&p, keyOf(p.id), placed != d.pageParts.end() ? pixelsOf(placed->second, room) : p.at});
    };
    for (const std::string& id : d.pagePartOrder[name])
        if (const auto it = std::ranges::find(pageParts, std::string_view(id), &PagePart::id); it != pageParts.end()) show(*it);
    for (PagePart& p : pageParts) show(p);
    const SDL_FPoint least{std::min(layout::pagePartLeastW * k, room.w), std::min(layout::pagePartLeastH * k, room.h)};

    // No page part over another (a layout kept from before, or a page part only some of the Library's kinds show, where others
    // were put): the ones put by hand keep their places, the one put last first, and the rest give way, cut back (or
    // else moved) until they're clear.
    {
        std::vector<std::size_t> byRank(shown.size());
        std::iota(byRank.begin(), byRank.end(), std::size_t{0});
        std::ranges::sort(byRank, std::greater{}, [&](std::size_t i) { return std::pair{d.pageParts.contains(shown[i].key), i}; });
        std::vector<SDL_FRect> placed;
        for (const std::size_t i : byRank) {
            SDL_FRect& at = shown[i].at;
            if (const auto cut = clearOf(at, placed, kWhole, false, room, least)) at = *cut;
            else if (const auto moved = clearOf(at, placed, kWhole, true, room, least)) at = *moved;
            placed.push_back(at);
        }
    }

    // What a press where the pointer is takes (with Layout on): the page part it's in (two side by side: the one it's
    // on; else the one it's near), whole, or the sides it's near; on the border it shares with another (they touch
    // there), in the middle of their two edges, that one's side too (a little into either: only its own).
    struct Target {
        std::size_t part = 0;
        int sides = kWhole;
        std::optional<std::size_t> with;
    };
    auto opposite = [](int side) { return side == kLeftSide ? kRightSide : side == kRightSide ? kLeftSide : side == kTopSide ? kBottomSide : kTopSide; };
    auto sideAt = [](const SDL_FRect& at, int side) { return side == kLeftSide ? at.x : side == kRightSide ? at.x + at.w : side == kTopSide ? at.y : at.y + at.h; };
    auto targetAt = [&](float x, float y) -> std::optional<Target> {
        if (!pointInRect(x, y, room)) return std::nullopt; // (the tools' own part, under it: its buttons)
        auto in = [&](float grow) {
            for (std::size_t i = shown.size(); i-- > 0;)
                if (const SDL_FRect& at = shown[i].at; pointInRect(x, y, {at.x - grow, at.y - grow, at.w + 2 * grow, at.h + 2 * grow})) return std::optional(i);
            return std::optional<std::size_t>{};
        };
        auto part = in(0.0f);
        if (!part) part = in(reach);
        if (!part) return std::nullopt;
        const SDL_FRect& at = shown[*part].at;
        const int sides = sidesAt(at, x, y, reach);
        if (sides == 0) return Target{.part = *part, .sides = kWhole, .with = {}};
        Target t{.part = *part, .sides = sides, .with = {}};
        if (sides != kLeftSide && sides != kRightSide && sides != kTopSide && sides != kBottomSide) return t; // (a corner: its own)
        const bool across = sides & (kLeftSide | kRightSide);
        const float line = sideAt(at, sides), pointer = across ? x : y, along = across ? y : x;
        if (std::abs(pointer - line) > layout::pagePartShare * k) return t;
        for (std::size_t i = 0; i < shown.size(); ++i) {
            const SDL_FRect& o = shown[i].at;
            if (i != *part && std::abs(sideAt(o, opposite(sides)) - line) < 0.5f && (across ? along >= o.y && along < o.y + o.h : along >= o.x && along < o.x + o.w)) t.with = i;
        }
        return t;
    };
    // The sides lit (bright) with Layout on: what the pointer would take, or is moving.
    std::vector<int> lit(shown.size(), 0);
    // A page part caught (by the pointer's press, or the keys): the page's layout kept as it is then (as shares: its
    // page parts keep their places together, as they're shown), and the one caught on top (the one sharing its border under it).
    auto catchAt = [&](const Target& t, SDL_FPoint from) {
        for (const Shown& s : shown) d.pageParts.insert_or_assign(s.key, sharesOf(s.at, room));
        const Shown& part = shown[t.part];
        const Shown* with = t.with ? &shown[*t.with] : nullptr;
        d.pagePartDrag = DeveloperTools::PagePartDrag{.id = part.key, .sides = t.sides, .from = from, .start = part.at,
                                                      .with = with ? with->key : std::string{}, .withStart = with ? with->at : SDL_FRect{}};
        std::vector<std::string> order;
        for (const Shown& s : shown)
            if (&s != &part && &s != with) order.emplace_back(s.pagePart->id);
        if (with) order.emplace_back(with->pagePart->id);
        order.emplace_back(part.pagePart->id);
        d.pagePartOrder[name] = std::move(order);
    };

    // Layout on, by the keys: Tab and Shift+Tab (a controller's shoulders) go to each page part's whole, its right side, its
    // bottom, in turn; the arrows (the d-pad) take it as the pointer would, 16 points a step (Shift: 4), across a side; Enter
    // keeps it there, and so does Esc (B), which leaves Layout when nothing's caught. The pointer stays where it is.
    DeveloperTools::LayoutKeys& lk = d.layoutKeys;
    const bool heldBefore = d.held;
    const float mouseBefore[]{d.mouseX, d.mouseY};
    bool keyDrag = false;
    if (lk.page != name) lk = {.page = name, .handle = 0, .by = {}, .dragging = false, .ring = false, .ringBox = {}};
    lk.ringBox = {};
    // (by the page's own order of them: the one caught goes on top of the others, last in `shown`)
    auto shownAt = [&](std::size_t handle) { return static_cast<std::size_t>(std::ranges::find(shown, &pageParts[handle / 3], &Shown::pagePart) - shown.begin()); };
    if (d.editLayout && !shown.empty() && shown.size() == pageParts.size()) {
        const std::size_t handles = pageParts.size() * 3;
        lk.handle = std::min(lk.handle, handles - 1);
        auto said = [&] { // (a screen reader's told the page part and what of it)
            const ToolsText what[]{"move"_text, "right side"_text, "bottom"_text};
            if (engine.screenReader()) announce(std::format("{}: {}", pageParts[lk.handle / 3].heading, tr(what[lk.handle % 3])));
        };
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            const bool back = d.isKey(key, ToolKey::Previous, true);
            if (back || d.isKey(key, ToolKey::Next, true)) {
                lk.dragging = false;
                if (std::exchange(lk.ring, true)) lk.handle = (lk.handle + (back ? handles - 1 : 1)) % handles; // (the first: where they are)
                said();
                return true;
            }
            const int dx = d.isKey(key, ToolKey::Left, true, true) ? -1 : d.isKey(key, ToolKey::Right, true, true) ? 1 : 0;
            const int dy = d.isKey(key, ToolKey::Up, true, true) ? -1 : d.isKey(key, ToolKey::Down, true, true) ? 1 : 0;
            if (dx != 0 || dy != 0) {
                const std::size_t part = shownAt(lk.handle);
                const int what = static_cast<int>(lk.handle % 3);
                lk.ring = true;
                if ((what == 1 && dx == 0) || (what == 2 && dy == 0)) return true; // (a side goes across)
                if (!lk.dragging) {
                    const SDL_FRect& at = shown[part].at;
                    const SDL_FPoint from = what == 0   ? SDL_FPoint{at.x + at.w / 2, at.y + at.h / 2}
                                          : what == 1 ? SDL_FPoint{at.x + at.w - 0.5f, at.y + at.h / 2}
                                                      : SDL_FPoint{at.x + at.w / 2, at.y + at.h - 0.5f};
                    std::optional<Target> t = what == 0 ? std::optional(Target{.part = part, .sides = kWhole, .with = {}}) : targetAt(from.x, from.y);
                    if (!t || t->part != part) t = Target{.part = part, .sides = what == 1 ? kRightSide : kBottomSide, .with = {}};
                    catchAt(*t, from);
                    lk.dragging = true;
                    lk.by = {};
                }
                const float by = (key.mod & SDL_KMOD_SHIFT ? 4.0f : 16.0f) * k;
                lk.by.x += static_cast<float>(dx) * by;
                lk.by.y += static_cast<float>(dy) * by;
                return true;
            }
            if (d.isKey(key, ToolKey::Press)) {
                lk.dragging = false;
                return true;
            }
            if (d.isKey(key, ToolKey::Back)) {
                if (!std::exchange(lk.dragging, false) && !d.pagePartDrag) d.editLayout = false;
                return true;
            }
            return false;
        });
        if (lk.dragging && d.pagePartDrag) { // (as the pointer would hold it there)
            keyDrag = true;
            d.held = true;
            d.mouseX = d.pagePartDrag->from.x + lk.by.x;
            d.mouseY = d.pagePartDrag->from.y + lk.by.y;
        } else if (!lk.dragging && d.pagePartDrag && !heldBefore) {
            d.held = false; // (let go: kept)
        }
    }

    // A page part being moved or resized: where the pointer has it now (its sides going to others' near them, within the
    // room and no smaller than it can be), never over another (it stops against them, or slides along them; where
    // it can't be clear of them, it stays where it was); a border two share moves both their sides. Kept once it's
    // let go (or its page left).
    if (d.pagePartDrag) {
        const auto dragged = std::ranges::find(shown, d.pagePartDrag->id, &Shown::key);
        const auto partner = d.pagePartDrag->with.empty() ? shown.end() : std::ranges::find(shown, d.pagePartDrag->with, &Shown::key);
        if (!d.held || dragged == shown.end() || !d.editLayout || (!d.pagePartDrag->with.empty() && partner == shown.end())) {
            d.pagePartDrag.reset();
            saveLayout();
        } else {
            const DeveloperTools::PagePartDrag& drag = *d.pagePartDrag;
            std::vector<SDL_FRect> others; // (every other page part: not the one moved, nor the one sharing its border)
            for (const Shown& s : shown)
                if (&s != &*dragged && (partner == shown.end() || &s != &*partner)) others.push_back(s.at);
            const float dx = d.mouseX - drag.from.x, dy = d.mouseY - drag.from.y, snap = keyDrag ? 0.0f : layout::pagePartSnap * k; // (the keys' steps: as they go)
            const float leastW = least.x, leastH = least.y;
            // The lines a side moved near goes to (within layout::pagePartSnap): the room's edges, and the sides of the page parts
            // near where it's going (against them, or in line with them).
            auto linesNear = [&](const SDL_FRect& going) {
                std::pair<std::vector<float>, std::vector<float>> lines{{room.x, room.x + room.w}, {room.y, room.y + room.h}};
                const SDL_FRect within{going.x - snap, going.y - snap, going.w + 2 * snap, going.h + 2 * snap};
                for (const SDL_FRect& o : others)
                    if (overlap(within, o)) {
                        lines.first.insert(lines.first.end(), {o.x, o.x + o.w});
                        lines.second.insert(lines.second.end(), {o.y, o.y + o.h});
                    }
                return lines;
            };
            if (partner != shown.end()) {
                // A border two share: the one before it (left, or over) and the one after it, as they were, and the line
                // between them where the pointer has it, from the least either can be to the page parts beyond them.
                const bool across = drag.sides & (kLeftSide | kRightSide), draggedFirst = drag.sides == kRightSide || drag.sides == kBottomSide;
                const SDL_FRect first = draggedFirst ? drag.start : drag.withStart, second = draggedFirst ? drag.withStart : drag.start;
                const float from = across ? second.x : second.y;
                const float x0 = std::min(first.x, second.x), y0 = std::min(first.y, second.y); // (the page parts near the two)
                const auto [xs, ys] = linesNear({x0, y0, std::max(first.x + first.w, second.x + second.w) - x0, std::max(first.y + first.h, second.y + second.h) - y0});
                float lo = (across ? first.x + leastW : first.y + leastH), hi = (across ? second.x + second.w - leastW : second.y + second.h - leastH);
                for (const SDL_FRect& o : others) {
                    const bool besideFirst = across ? o.y < first.y + first.h && first.y < o.y + o.h : o.x < first.x + first.w && first.x < o.x + o.w;
                    const bool besideSecond = across ? o.y < second.y + second.h && second.y < o.y + o.h : o.x < second.x + second.w && second.x < o.x + o.w;
                    if (besideFirst && (across ? o.x : o.y) >= from) hi = std::min(hi, across ? o.x : o.y); // (the first can't grow over it)
                    if (besideSecond && (across ? o.x + o.w : o.y + o.h) <= from) lo = std::max(lo, across ? o.x + o.w : o.y + o.h); // (nor the second)
                }
                float line = from + (across ? dx : dy);
                line = std::round(std::clamp(line + snapBy({line}, across ? xs : ys, snap), lo, std::max(lo, hi)));
                if (lo > hi) line = from; // (no room either way)
                SDL_FRect a = first, b = second;
                if (across) {
                    a.w = line - a.x;
                    b.w = b.x + b.w - line;
                    b.x = line;
                } else {
                    a.h = line - a.y;
                    b.h = b.y + b.h - line;
                    b.y = line;
                }
                dragged->at = draggedFirst ? a : b;
                partner->at = draggedFirst ? b : a;
                d.pageParts[drag.id] = sharesOf(dragged->at, room);
                d.pageParts[drag.with] = sharesOf(partner->at, room);
                lit[static_cast<std::size_t>(partner - shown.begin())] = opposite(drag.sides);
            } else {
                SDL_FRect at = drag.start;
                SDL_FRect going = at; // (where the pointer takes it, before it goes to the lines near it)
                if (drag.sides == kWhole) {
                    going.x += dx;
                    going.y += dy;
                } else {
                    const float x0 = going.x + (drag.sides & kLeftSide ? dx : 0.0f), x1 = going.x + going.w + (drag.sides & kRightSide ? dx : 0.0f);
                    const float y0 = going.y + (drag.sides & kTopSide ? dy : 0.0f), y1 = going.y + going.h + (drag.sides & kBottomSide ? dy : 0.0f);
                    going = {std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)};
                }
                const auto [xs, ys] = linesNear(going);
                if (drag.sides == kWhole) {
                    at.x += dx;
                    at.y += dy;
                    at.x += snapBy({at.x, at.x + at.w}, xs, snap);
                    at.y += snapBy({at.y, at.y + at.h}, ys, snap);
                    at.x = std::clamp(at.x, room.x, std::max(room.x, room.x + room.w - at.w));
                    at.y = std::clamp(at.y, room.y, std::max(room.y, room.y + room.h - at.h));
                } else {
                    float left = at.x, right = at.x + at.w, top = at.y, bottom = at.y + at.h;
                    if (drag.sides & kLeftSide) left = std::clamp(left + dx + snapBy({left + dx}, xs, snap), room.x, right - leastW);
                    if (drag.sides & kRightSide) right = std::clamp(right + dx + snapBy({right + dx}, xs, snap), left + leastW, room.x + room.w);
                    if (drag.sides & kTopSide) top = std::clamp(top + dy + snapBy({top + dy}, ys, snap), room.y, bottom - leastH);
                    if (drag.sides & kBottomSide) bottom = std::clamp(bottom + dy + snapBy({bottom + dy}, ys, snap), top + leastH, room.y + room.h);
                    at = {left, top, right - left, bottom - top};
                }
                if (const auto clear = clearOf({std::round(at.x), std::round(at.y), std::round(at.w), std::round(at.h)}, others, drag.sides, drag.sides == kWhole, room, least)) {
                    dragged->at = *clear;
                    d.pageParts[drag.id] = sharesOf(dragged->at, room);
                }
            }
            lit[static_cast<std::size_t>(dragged - shown.begin())] = drag.sides;
            d.wantPointer = pointerFor(drag.sides);
        }
    }

    if (keyDrag) { // (the pointer as it was)
        d.held = heldBefore;
        d.mouseX = mouseBefore[0];
        d.mouseY = mouseBefore[1];
    }
    if (d.editLayout && lk.ring && lk.handle / 3 < pageParts.size() && shownAt(lk.handle) < shown.size()) { // (the keys' ring: round the page part, or along its side; drawn over it all)
        const SDL_FRect& at = shown[shownAt(lk.handle)].at;
        const float t = 4.0f * k;
        lk.ringBox = lk.handle % 3 == 0 ? at : lk.handle % 3 == 1 ? SDL_FRect{at.x + at.w - t, at.y, t, at.h} : SDL_FRect{at.x, at.y + at.h - t, at.w, t};
    }

    // Layout on: a press moves what it takes (targetAt), and the page part comes to the top.
    if (d.editLayout && !d.pagePartDrag)
        if (const auto t = targetAt(d.mouseX, d.mouseY)) {
            lit[t->part] = t->sides;
            if (t->with) lit[*t->with] = opposite(t->sides);
            d.wantPointer = pointerFor(t->sides);
            if (std::exchange(d.clicked, false)) catchAt(*t, {d.mouseX, d.mouseY});
        }

    // The page parts, bottom to top, each over what's under it, with its edge (lit while Layout is on: what's in
    // them takes no pointer then). Else a press (or the pointer, while nothing's held) goes only to the one it's on,
    // the one on top; while something's dragged, every page part sees the pointer.
    const bool onlyTop = !d.held || d.clicked;
    const float pointerX = d.mouseX, pointerY = d.mouseY;
    const float headingH = layout::headingH * k, barRoom = layout::scrollbarRoom * k, step = 2 * layout::listItemH * k;
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const Shown& s = shown[i];
        const bool under = onlyTop && std::any_of(shown.begin() + static_cast<std::ptrdiff_t>(i) + 1, shown.end(),
                                                  [&](const Shown& o) { return pointInRect(pointerX, pointerY, o.at); });
        const bool off = onlyTop && !pointInRect(pointerX, pointerY, s.at);
        if (d.editLayout || under || off) d.mouseX = d.mouseY = -1e6f; // (out of its reach)
        draw::rect(r, s.at, kPanel);
        draw::outline(r, s.at, d.editLayout ? kAccent : kEdge);
        if (const int sides = lit[i]; d.editLayout && sides != 0) { // (what moves, bright: two edges side by side, a border both share)
            const float t = 2 * std::max(1.0f, std::round(k));
            if (sides & kLeftSide) draw::rect(r, {s.at.x, s.at.y, t, s.at.h}, theme::text);
            if (sides & kRightSide) draw::rect(r, {s.at.x + s.at.w - t, s.at.y, t, s.at.h}, theme::text);
            if (sides & kTopSide) draw::rect(r, {s.at.x, s.at.y, s.at.w, t}, theme::text);
            if (sides & kBottomSide) draw::rect(r, {s.at.x, s.at.y + s.at.h - t, s.at.w, t}, theme::text);
        }
        // Its title, at the top (its buttons at the right); under it, what's in it: as big as it needs (the room under the
        // title at least); bigger, it's scrolled there, with a scrollbar at the right to scroll it down and one at the foot to
        // scroll it across, as needed (each takes its room from the other's way). Only what shows takes the pointer. Its
        // stops for the keys go under its name (the title's buttons first), with what scrolls them into sight.
        const PagePart& p = *s.pagePart;
        const float headH = p.head ? layout::previewHeadH * k : headingH;
        const SDL_FRect whole = insideOf(s.at, k), inside{whole.x, whole.y + headH, whole.w, std::max(0.0f, whole.h - headH)};
        d.keyGroup = s.key;
        d.keyHeading = p.heading;
        d.keyOrder = {s.at.x, s.at.y};
        d.keyCount = 0;
        bool changed = false;
        if (whole.w > 0.0f && whole.h > 0.0f) {
            const DeveloperUi::Clip clipped(ui, {whole.x, s.at.y, whole.w, std::min(whole.h, headH) + whole.y - s.at.y}); // (the buttons stand out of the row a little)
            PreviewHead row(ui, {whole.x, whole.y, whole.w, headingH});
            if (p.head) changed = p.head(row);
            row.title(p.heading);
        }
        bool down = false, across = false;
        SDL_FRect view = inside;
        float w = view.w, h = view.h;
        for (int pass = 0; pass < 3; ++pass) {
            view = {inside.x, inside.y, std::max(0.0f, inside.w - (down ? barRoom : 0.0f)), std::max(0.0f, inside.h - (across ? barRoom : 0.0f))};
            w = std::max(view.w, p.least.x);
            h = std::max(view.h, p.tallAt ? p.tallAt(w) : p.least.y);
            down = h > view.h + 0.5f;
            across = w > view.w + 0.5f;
        }
        view = {inside.x, inside.y, std::max(0.0f, inside.w - (down ? barRoom : 0.0f)), std::max(0.0f, inside.h - (across ? barRoom : 0.0f))};
        SDL_FPoint& scroll = d.pagePartScroll[s.key];
        scroll = {std::clamp(scroll.x, 0.0f, std::max(0.0f, w - view.w)), std::clamp(scroll.y, 0.0f, std::max(0.0f, h - view.h))};
        SDL_FRect box{view.x - scroll.x, view.y - scroll.y, w, h}; // (a form: its scrollbar in the room kept at its right, a gap from what's in it)
        if (p.scrollsBeside) box.w = std::max(0.0f, box.w - (layout::formBarRoom - layout::pagePartPad) * k);
        if (!changed && view.w > 0.0f && view.h > 0.0f) {
            const DeveloperUi::Clip clipped(ui, view);
            const std::size_t depth = d.scrollers.size();
            d.scrollers.push_back({&scroll.y, view});
            changed = p.draw(box);
            d.scrollers.resize(depth);
        }
        d.keyHeading.clear();
        if (!changed && (down || across) && inside.w > 0.0f && inside.h > 0.0f) { // (and the wheel anywhere in it, what's in it not taking it)
            const DeveloperUi::Clip clipped(ui, whole);
            const float thick = layout::scrollbarW * k, in = (barRoom - thick) / 2;
            if (down) {
                scroll.y = std::clamp(scroll.y - ui.wheelOver(whole) * step, 0.0f, h - view.h);
                ui.scrollbar(&scroll.y, {view.x + view.w + in, view.y, thick, view.h}, view.h, h, scroll.y);
            }
            if (across) {
                scroll.x = std::clamp(scroll.x + ui.wheelAcrossOver(whole) * step, 0.0f, w - view.w);
                ui.scrollbar(&scroll.x, {view.x, view.y + view.h + in, view.w, thick}, view.w, w, scroll.x, true);
            }
        }
        d.mouseX = pointerX;
        d.mouseY = pointerY;
        if (changed) return !(d.keyStopsWhole = false); // (the stops: last frame's, still)
    }
    return false;
}

SDL_FPoint Developer::pagePartChrome(float k) const {
    const SDL_FRect inside = insideOf({0.0f, 0.0f, 1000.0f, 1000.0f}, k);
    return {1000.0f - inside.w, 1000.0f - inside.h};
}

Developer::PagePart Developer::listPagePart(DeveloperUi& ui, std::string_view id, std::string heading, const SDL_FRect& at, std::size_t rows,
                                      std::function<bool(const ListRoom&)> draw) {
    const float k = ui.k, rowsH = static_cast<float>(rows) * (layout::gap + layout::buttonH) * k; // (a few of its rows, and its buttons)
    return {.id = id,
            .heading = std::move(heading),
            .at = at,
            .draw = [&ui, rows, draw = std::move(draw)](const SDL_FRect& box) { return draw(ui.listRoom(box, rows)); },
            .least = {layout::pagePartListW * k, layout::pagePartListRows * layout::listItemH * k + rowsH}};
}

Developer::PagePart Developer::formPagePart(DeveloperUi& ui, std::string_view id, std::string heading, const SDL_FRect& at, bool filled,
                                      std::function<bool(const SDL_FRect&)> draw) {
    const float k = ui.k; // (as narrow as the narrowest form column; its title row and two of its rows: it scrolls them itself)
    return {.id = id,
            .heading = std::move(heading),
            .at = at,
            .draw = std::move(draw),
            .scrollsBeside = true,
            .least = filled ? SDL_FPoint{layout::formLeast * k - pagePartChrome(k).x, (layout::titleH + 2 * (layout::boxH + layout::rowGap)) * k} : SDL_FPoint{}};
}

void Developer::readLayout() {
    DeveloperTools& d = tools;
    d.layoutRead = true;
    const auto text = readTextFile(toolsFolder() + "layout.txt");
    if (!text) return;
    for (const std::string& line : linesOf(*text)) {
        const std::vector<std::string> words = rawWords(line);
        if (words.size() == 6 && words[0] == "part") {
            const auto x = parseNumber(words[2]), y = parseNumber(words[3]), w = parseNumber(words[4]), h = parseNumber(words[5]);
            if (x && y && w && h && *w > 0.0f && *h > 0.0f) d.pageParts[words[1]] = {*x, *y, *w, *h};
        } else if (words.size() >= 2 && words[0] == "order") {
            d.pagePartOrder[words[1]].assign(words.begin() + 2, words.end());
        }
    }
}

void Developer::saveLayout() {
    const DeveloperTools& d = tools;
    std::string out = "# Where the developer tools' page parts are: VNext writes this as they're moved (the Layout button).\n";
    for (const auto& [key, at] : d.pageParts) out += std::format("part {} {:.4f} {:.4f} {:.4f} {:.4f}\n", key, at.x, at.y, at.w, at.h);
    for (const auto& [page, order] : d.pagePartOrder)
        if (!order.empty()) {
            out += "order " + page;
            for (const std::string& id : order) out += " " + id;
            out += '\n';
        }
    const std::string file = toolsFolder() + "layout.txt";
    if (!SDL_SaveFile(file.c_str(), out.data(), out.size())) logWarn("{}", "Can't save the developer tools' layout: {}."_tr(SDL_GetError()));
}

// The page shown's page parts back where it puts them.
void Developer::resetLayout() {
    DeveloperTools& d = tools;
    std::erase_if(d.pageParts, [&](const auto& kept) { return kept.first.starts_with(d.shownPage + "/"); });
    d.pagePartOrder.erase(d.shownPage);
    d.pagePartDrag.reset();
    saveLayout();
}

void Developer::showPointer(std::optional<SDL_SystemCursor> wanted) {
    DeveloperTools& d = tools;
    if (wanted == d.pointer) return;
    d.pointer = wanted;
    if (wanted) {
        CursorPtr& made = d.cursors[static_cast<std::size_t>(*wanted)];
        if (!made) made.reset(SDL_CreateSystemCursor(*wanted));
        if (made) return void(SDL_SetCursor(made.get()));
    }
    SDL_SetCursor(engine.cursor_ ? engine.cursor_.get() : SDL_GetDefaultCursor());
}

} // namespace vn
