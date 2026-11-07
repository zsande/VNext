// The tabs' parts as sub-windows: each where its tab puts it, or where it was moved (kept as shares of the room over
// the tools' own part, so it keeps its place in any window), drawn bottom to top, each with an edge in the tools'
// colours, never over another. With Layout on their edges light up (what a press would move, brighter), and a
// part is moved by dragging it and resized by dragging its edges (two that touch: the middle of their border moves
// both), each side going to the sides near it (what's in the parts takes no pointer then). The layout is kept in the
// tools' own folder (layout.txt); Reset puts a tab's back.

#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <numeric>
#include <optional>
#include <ranges>
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

// The room inside a part: within its edge (a pixel round it), the room between that and what's in it.
SDL_FRect insideOf(const SDL_FRect& at, float k) {
    const float by = std::max(1.0f, std::round(k)) + std::round(layout::panelPad * k);
    return {at.x + by, at.y + by, std::max(0.0f, at.w - 2 * by), std::max(0.0f, at.h - 2 * by)};
}

// The sides of a part the pointer at (x, y) is on, within `reach` of its edge: the ones a drag there moves.
int sidesAt(const SDL_FRect& at, float x, float y, float reach) {
    if (x < at.x - reach || x > at.x + at.w + reach || y < at.y - reach || y > at.y + at.h + reach) return 0;
    int sides = 0;
    if (std::abs(x - at.x) <= reach) sides |= kLeftSide;
    else if (std::abs(x - (at.x + at.w)) <= reach) sides |= kRightSide;
    if (std::abs(y - at.y) <= reach) sides |= kTopSide;
    else if (std::abs(y - (at.y + at.h)) <= reach) sides |= kBottomSide;
    return sides;
}

// The pointer for moving those sides (all four: the whole part).
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

bool Developer::drawPanels(DeveloperUi& ui, std::string_view tab, std::span<Panel> panels) {
    DeveloperTools& d = tools;
    if (!d.layoutRead) readLayout();
    SDL_Renderer* r = ui.r;
    const float k = ui.k, reach = layout::panelGrab * k;
    const SDL_FRect room = d.room;
    const std::string tabName(tab);
    auto keyOf = [&](std::string_view id) { return std::format("{}/{}", tab, id); };
    d.panelTab = tabName;

    // Bottom to top: as the tab's order has them (the one moved last on top), then the rest as the tab lists them;
    // each where it was put, else where the tab puts it.
    struct Shown {
        Panel* panel;
        std::string key;
        SDL_FRect at;
    };
    std::vector<Shown> shown;
    auto show = [&](Panel& p) {
        if (std::ranges::contains(shown, &p, &Shown::panel)) return;
        const auto placed = d.panels.find(keyOf(p.id));
        shown.push_back({&p, keyOf(p.id), placed != d.panels.end() ? pixelsOf(placed->second, room) : p.at});
    };
    for (const std::string& id : d.panelOrder[tabName])
        if (const auto it = std::ranges::find(panels, std::string_view(id), &Panel::id); it != panels.end()) show(*it);
    for (Panel& p : panels) show(p);
    const SDL_FPoint least{std::min(layout::panelLeastW * k, room.w), std::min(layout::panelLeastH * k, room.h)};

    // No part over another (a layout kept from before, or a part only some of the Library's kinds show, where others
    // were put): the ones put by hand keep their places, the one put last first, and the rest give way, cut back (or
    // else moved) until they're clear.
    {
        std::vector<std::size_t> byRank(shown.size());
        std::iota(byRank.begin(), byRank.end(), std::size_t{0});
        std::ranges::sort(byRank, std::greater{}, [&](std::size_t i) { return std::pair{d.panels.contains(shown[i].key), i}; });
        std::vector<SDL_FRect> placed;
        for (const std::size_t i : byRank) {
            SDL_FRect& at = shown[i].at;
            if (const auto cut = clearOf(at, placed, kWhole, false, room, least)) at = *cut;
            else if (const auto moved = clearOf(at, placed, kWhole, true, room, least)) at = *moved;
            placed.push_back(at);
        }
    }

    // What a press where the pointer is takes (with Layout on): the part it's in (two side by side: the one it's
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
        if (std::abs(pointer - line) > layout::panelShare * k) return t;
        for (std::size_t i = 0; i < shown.size(); ++i) {
            const SDL_FRect& o = shown[i].at;
            if (i != *part && std::abs(sideAt(o, opposite(sides)) - line) < 0.5f && (across ? along >= o.y && along < o.y + o.h : along >= o.x && along < o.x + o.w)) t.with = i;
        }
        return t;
    };
    // The sides lit (bright) with Layout on: what the pointer would take, or is moving.
    std::vector<int> lit(shown.size(), 0);

    // A part being moved or resized: where the pointer has it now (its sides going to others' near them, within the
    // room and no smaller than it can be), never over another (it stops against them, or slides along them; where
    // it can't be clear of them, it stays where it was); a border two share moves both their sides. Kept once it's
    // let go (or its tab left).
    if (d.panelDrag) {
        const auto dragged = std::ranges::find(shown, d.panelDrag->id, &Shown::key);
        const auto partner = d.panelDrag->with.empty() ? shown.end() : std::ranges::find(shown, d.panelDrag->with, &Shown::key);
        if (!d.held || dragged == shown.end() || !d.editLayout || (!d.panelDrag->with.empty() && partner == shown.end())) {
            d.panelDrag.reset();
            saveLayout();
        } else {
            const DeveloperTools::PanelDrag& drag = *d.panelDrag;
            std::vector<SDL_FRect> others; // (every other part: not the one moved, nor the one sharing its border)
            for (const Shown& s : shown)
                if (&s != &*dragged && (partner == shown.end() || &s != &*partner)) others.push_back(s.at);
            const float dx = d.mouseX - drag.from.x, dy = d.mouseY - drag.from.y, snap = layout::panelSnap * k;
            const float leastW = least.x, leastH = least.y;
            // The lines a side moved near goes to (within layout::panelSnap): the room's edges, and the sides of the parts
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
                // between them where the pointer has it, from the least either can be to the parts beyond them.
                const bool across = drag.sides & (kLeftSide | kRightSide), draggedFirst = drag.sides == kRightSide || drag.sides == kBottomSide;
                const SDL_FRect first = draggedFirst ? drag.start : drag.withStart, second = draggedFirst ? drag.withStart : drag.start;
                const float from = across ? second.x : second.y;
                const float x0 = std::min(first.x, second.x), y0 = std::min(first.y, second.y); // (the parts near the two)
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
                d.panels[drag.id] = sharesOf(dragged->at, room);
                d.panels[drag.with] = sharesOf(partner->at, room);
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
                    d.panels[drag.id] = sharesOf(dragged->at, room);
                }
            }
            lit[static_cast<std::size_t>(dragged - shown.begin())] = drag.sides;
            d.wantPointer = pointerFor(drag.sides);
        }
    }

    // Layout on: a press moves what it takes (targetAt), and the part comes to the top. A press keeps the tab's layout
    // as it is then (as shares: its parts keep their places together, as they're shown).
    if (d.editLayout && !d.panelDrag)
        if (const auto t = targetAt(d.mouseX, d.mouseY)) {
            lit[t->part] = t->sides;
            if (t->with) lit[*t->with] = opposite(t->sides);
            d.wantPointer = pointerFor(t->sides);
            if (std::exchange(d.clicked, false)) {
                for (const Shown& s : shown) d.panels.insert_or_assign(s.key, sharesOf(s.at, room));
                const Shown& part = shown[t->part];
                const Shown* with = t->with ? &shown[*t->with] : nullptr;
                d.panelDrag = DeveloperTools::PanelDrag{.id = part.key, .sides = t->sides, .from = {d.mouseX, d.mouseY}, .start = part.at,
                                                        .with = with ? with->key : std::string{}, .withStart = with ? with->at : SDL_FRect{}};
                std::vector<std::string> order; // (the one moved on top, and the one sharing its border under it)
                for (const Shown& s : shown)
                    if (&s != &part && &s != with) order.emplace_back(s.panel->id);
                if (with) order.emplace_back(with->panel->id);
                order.emplace_back(part.panel->id);
                d.panelOrder[tabName] = std::move(order);
            }
        }

    // The parts, bottom to top, each over what's under it, with its edge (lit while Layout is on: what's in
    // them takes no pointer then). Else a press (or the pointer, while nothing's held) goes only to the one on top
    // where it is; while something's dragged, every part sees the pointer.
    const bool onlyTop = !d.held || d.clicked;
    const float pointerX = d.mouseX, pointerY = d.mouseY;
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const Shown& s = shown[i];
        const bool under = onlyTop && std::any_of(shown.begin() + static_cast<std::ptrdiff_t>(i) + 1, shown.end(),
                                                  [&](const Shown& o) { return pointInRect(pointerX, pointerY, o.at); });
        if (d.editLayout || under) d.mouseX = d.mouseY = -1e6f; // (out of its reach)
        draw::rect(r, s.at, kPanel);
        draw::outline(r, s.at, d.editLayout ? kAccent : kEdge);
        if (const int sides = lit[i]; d.editLayout && sides != 0) { // (what moves, bright: two edges side by side, a border both share)
            const float t = 2 * std::max(1.0f, std::round(k));
            if (sides & kLeftSide) draw::rect(r, {s.at.x, s.at.y, t, s.at.h}, theme::text);
            if (sides & kRightSide) draw::rect(r, {s.at.x + s.at.w - t, s.at.y, t, s.at.h}, theme::text);
            if (sides & kTopSide) draw::rect(r, {s.at.x, s.at.y, s.at.w, t}, theme::text);
            if (sides & kBottomSide) draw::rect(r, {s.at.x, s.at.y + s.at.h - t, s.at.w, t}, theme::text);
        }
        const SDL_FRect inside = insideOf(s.at, k);
        SDL_FRect box = inside; // (a form: its scrollbar in the room kept at its right, a gap from what's in it)
        if (s.panel->scrollsBeside) box.w = std::max(0.0f, box.w - (layout::formBarRoom - layout::panelPad) * k);
        bool changed = false;
        if (inside.w > 0.0f && inside.h > 0.0f) {
            const DeveloperUi::Clip clipped(ui, inside);
            changed = s.panel->draw(box);
        }
        d.mouseX = pointerX;
        d.mouseY = pointerY;
        if (changed) return true;
    }
    return false;
}

SDL_FPoint Developer::panelChrome(float k) const {
    const SDL_FRect inside = insideOf({0.0f, 0.0f, 1000.0f, 1000.0f}, k);
    return {1000.0f - inside.w, 1000.0f - inside.h};
}

Developer::Panel Developer::listPanel(DeveloperUi& ui, std::string_view id, std::string heading, const SDL_FRect& at, std::size_t rows,
                                      std::function<bool(const ListPart&)> draw) {
    return {.id = id, .at = at, .draw = [&ui, heading = std::move(heading), rows, draw = std::move(draw)](const SDL_FRect& box) {
                return draw(ui.listPart(heading, box, rows));
            }};
}

Developer::Panel Developer::titledPanel(DeveloperUi& ui, std::string_view id, std::string heading, const SDL_FRect& at, std::function<bool(const SDL_FRect&)> draw) {
    return {.id = id, .at = at, .draw = [&ui, heading = std::move(heading), draw = std::move(draw)](const SDL_FRect& box) {
                const float headingH = layout::headingH * ui.k; // (as a list's: its row, then what's under it)
                ui.heading(heading, box.x, box.y, box.w);
                return draw({box.x, box.y + headingH, box.w, std::max(0.0f, box.h - headingH)});
            }};
}

Developer::Panel Developer::formPanel(DeveloperUi& ui, std::string_view id, std::string heading, const SDL_FRect& at, std::function<bool(const SDL_FRect&)> draw) {
    Panel form = titledPanel(ui, id, std::move(heading), at, std::move(draw));
    form.scrollsBeside = true;
    return form;
}

void Developer::readLayout() {
    DeveloperTools& d = tools;
    d.layoutRead = true;
    const auto text = readTextFile(toolsFolder() + "layout.txt");
    if (!text) return;
    for (const std::string& line : linesOf(*text)) {
        const std::vector<std::string> words = rawWords(line);
        if (words.size() == 6 && words[0] == "panel") {
            const auto x = parseNumber(words[2]), y = parseNumber(words[3]), w = parseNumber(words[4]), h = parseNumber(words[5]);
            if (x && y && w && h && *w > 0.0f && *h > 0.0f) d.panels[words[1]] = {*x, *y, *w, *h};
        } else if (words.size() >= 2 && words[0] == "order") {
            d.panelOrder[words[1]].assign(words.begin() + 2, words.end());
        }
    }
}

void Developer::saveLayout() {
    const DeveloperTools& d = tools;
    std::string out = "# Where the developer tools' parts are: VNext writes this as they're moved (the Layout button).\n";
    for (const auto& [key, at] : d.panels) out += std::format("panel {} {:.4f} {:.4f} {:.4f} {:.4f}\n", key, at.x, at.y, at.w, at.h);
    for (const auto& [tab, order] : d.panelOrder)
        if (!order.empty()) {
            out += "order " + tab;
            for (const std::string& id : order) out += " " + id;
            out += '\n';
        }
    const std::string file = toolsFolder() + "layout.txt";
    if (!SDL_SaveFile(file.c_str(), out.data(), out.size())) logWarn("{}", "Can't save the developer tools' layout: {}."_tr(SDL_GetError()));
}

// The tab shown's parts back where it puts them.
void Developer::resetLayout() {
    DeveloperTools& d = tools;
    std::erase_if(d.panels, [&](const auto& kept) { return kept.first.starts_with(d.panelTab + "/"); });
    d.panelOrder.erase(d.panelTab);
    d.panelDrag.reset();
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
