// The Story page's map: the scenes as boxes, with arrows where the story goes from one to another.
#include "DeveloperUi.hpp"

#include <cmath>
#include <cstdlib>

namespace vn {

namespace {

// An arrow's curve, out to the right of p0 and in from the left of p3 (on whole pixels, so it doesn't
// break up); k: the window's scale.
std::vector<SDL_FPoint> curveOf(SDL_FPoint p0, SDL_FPoint p3, float k) {
    const float reach = std::max(40 * k, std::abs(p3.x - p0.x) / 2);
    const SDL_FPoint p1{p0.x + reach, p0.y}, p2{p3.x - reach, p3.y};
    std::vector<SDL_FPoint> curve;
    for (int s = 0; s <= 24; ++s) {
        const float t = static_cast<float>(s) / 24, u = 1 - t;
        curve.push_back({std::round(u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x),
                         std::round(u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y)});
    }
    return curve;
}

// A rounded corner at v, coming from `from` and going on to `to` (4 points, on whole pixels).
void cornerOf(std::vector<SDL_FPoint>& way, SDL_FPoint from, SDL_FPoint v, SDL_FPoint to, float radius) {
    const auto toward = [&](SDL_FPoint p) { // (its start and end: radius from v, half the side at most)
        const float dx = p.x - v.x, dy = p.y - v.y, len = std::hypot(dx, dy), step = std::min(radius, len / 2);
        return len > 0.0f ? SDL_FPoint{v.x + dx / len * step, v.y + dy / len * step} : v;
    };
    const SDL_FPoint s = toward(from), e = toward(to);
    for (int i = 0; i < 4; ++i) {
        const float t = static_cast<float>(i) / 3, u = 1 - t;
        way.push_back({std::round(u * u * s.x + 2 * u * t * v.x + t * t * e.x), std::round(u * u * s.y + 2 * u * t * v.y + t * t * e.y)});
    }
}

// An arrow from box a to box b: a curve, or, when b isn't far enough right of a for one (left of it, or a
// itself), the way around: out to the right, along the gap under a's row (over it, to a scene higher up)
// and in from the left, so it doesn't run behind the boxes. Its middle point is the middle of the way (the
// chosen one's x goes there). gapY: the gap between rows; k: the window's scale.
std::vector<SDL_FPoint> arrowOf(const SDL_FRect& a, const SDL_FRect& b, float gapY, float k) {
    const SDL_FPoint p0{std::round(a.x + a.w), std::round(a.y + a.h / 2)}, p3{std::round(b.x), std::round(b.y + b.h / 2)};
    const float stub = 12 * k; // (how far it goes out of a, and into b, before it turns)
    if (p3.x - p0.x >= 2 * stub) return curveOf(p0, p3, k);
    const float lane = p3.y < a.y ? a.y - gapY / 2 : a.y + a.h + gapY / 2;
    const SDL_FPoint v1{p0.x + stub, p0.y}, v2{v1.x, lane}, v4{p3.x - stub, p3.y}, v3{v4.x, lane};
    std::vector<SDL_FPoint> way{p0};
    cornerOf(way, p0, v1, v2, 6 * k);
    cornerOf(way, v1, v2, v3, 6 * k);
    way.push_back({std::round((v2.x + v3.x) / 2), std::round(lane)}); // (as many points before it as after)
    cornerOf(way, v2, v3, v4, 6 * k);
    cornerOf(way, v3, v4, p3, 6 * k);
    way.push_back(p3);
    return way;
}

// Whether (x, y) is on a curve (6 pixels from it at most, times k).
bool onCurve(const std::vector<SDL_FPoint>& curve, float x, float y, float k) {
    for (std::size_t s = 0; s + 1 < curve.size(); ++s) {
        const SDL_FPoint p = curve[s], q = curve[s + 1];
        const float dx = q.x - p.x, dy = q.y - p.y, len = dx * dx + dy * dy;
        const float t = len > 0.0f ? std::clamp(((x - p.x) * dx + (y - p.y) * dy) / len, 0.0f, 1.0f) : 0.0f;
        if (std::hypot(x - (p.x + t * dx), y - (p.y + t * dy)) < 6 * k) return true;
    }
    return false;
}

// An arrow along a curve, with its head at the end: thick when it's chosen or pointed at.
void drawCurve(SDL_Renderer* r, const std::vector<SDL_FPoint>& curve, SDL_Color c, bool thick, float k) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int pass = 0; pass < (thick ? 3 : 1); ++pass) { // (thick: once more a pixel lower, and a pixel to the right)
        std::vector<SDL_FPoint> line = curve;
        for (SDL_FPoint& p : line) {
            if (pass == 1) p.y += 1;
            if (pass == 2) p.x += 1;
        }
        SDL_RenderLines(r, line.data(), static_cast<int>(line.size()));
    }
    const SDL_FPoint p3 = curve.back();
    SDL_RenderLine(r, p3.x - 8 * k, p3.y - 5 * k, p3.x, p3.y);
    SDL_RenderLine(r, p3.x - 8 * k, p3.y + 5 * k, p3.x, p3.y);
}

} // namespace

// The Story page's map as it's drawn this frame: the scenes and the links between them (a copy: taking one away
// changes the editor's), each scene's column (jumps from start; `reached`: the column of the ones never
// jumped to) and box, the arrows, and what's under the mouse.
struct Developer::MapFrame {
    struct Arrow {
        std::size_t link = 0;
        std::vector<SDL_FPoint> curve;
    };
    DeveloperUi& ui;
    SDL_FRect area;
    float top; // where the boxes start
    const std::vector<StoryEditor::Scene>& scenes;
    std::vector<StoryEditor::Link> links;
    std::map<std::string, std::size_t> byName{};
    std::vector<int> column{};
    int reached = 0;
    std::vector<SDL_FRect> boxes{};
    std::vector<Arrow> arrows{};
    const Arrow* chosenArrow = nullptr;
    std::optional<std::size_t> hovered{}, hoveredDot{}, hoveredArrow{}, target{}; // (a scene, its dot, an arrow's link; the scene a jump is drawn to)

    // The dot at a box's right: dragged to another scene, it draws a jump there.
    [[nodiscard]] SDL_FRect dotOf(const SDL_FRect& b) const {
        const float k = ui.k;
        return {b.x + b.w - 7 * k, b.y + b.h / 2 - 7 * k, 14 * k, 14 * k};
    }
};

// The Story page's map: every scene as a box, in columns by how many jumps they are from start (the ones the
// story never jumps to after them), or where it was put by hand, with an arrow for each jump. A click
// chooses a scene (Lines shows it), another click renames it, and a double click just opens it; dragging one
// moves it, and dragging the dot at its right to another makes it jump there. A click chooses an arrow,
// and its x (or Delete) takes it away. Dragging the space between them (or the wheel) looks around.
bool Developer::drawMap(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    const float top = layout::mapPadY * k;
    MapFrame f{.ui = ui, .area = area, .top = top, .scenes = editor.scenes(), .links = editor.links()};
    const auto& scenes = f.scenes;
    const auto& links = f.links;
    layOutMap(f);
    // The chosen scene in sight, each time another is chosen: the map goes as far as it takes to
    // have its box in the middle, and stays where it's looked around to after that.
    if (d.scene < scenes.size() && d.mapFollows != d.scene) {
        d.mapFollows = d.scene;
        const SDL_FRect& box = f.boxes[d.scene];
        const SDL_FRect seen{area.x, area.y + top, area.w, std::max(0.0f, area.h - top)};
        if (box.x < seen.x || box.x + box.w > seen.x + seen.w || box.y < seen.y || box.y + box.h > seen.y + seen.h) {
            d.mapX += box.x + box.w / 2 - (seen.x + seen.w / 2);
            d.mapY += box.y + box.h / 2 - (seen.y + seen.h / 2);
            layOutMap(f);
        }
    }
    draw::rect(r, area, kBox);
    const DeveloperUi::Clip clipped(ui, area);
    // Looking around: the wheel (sideways, or Shift and the wheel: across), or dragging the empty space.
    d.mapY -= ui.wheelOver(area) * layout::mapStep * k;
    d.mapX += ui.wheelAcrossOver(area) * layout::mapStep * k;
    // The arrows: out to the right of one box, in from the left of another (or to the mouse).
    for (std::size_t i = 0; i < links.size(); ++i)
        if (const auto to = f.byName.find(links[i].to); to != f.byName.end())
            f.arrows.push_back({i, arrowOf(f.boxes[links[i].from], f.boxes[to->second], layout::nodeGapY * k, k)});
    if (d.mapArrow && std::ranges::none_of(links, [&](const auto& l) { return std::pair{l.from, l.line} == *d.mapArrow; })) d.mapArrow.reset();
    if (const auto it = std::ranges::find_if(f.arrows, [&](const MapFrame::Arrow& a) { return d.mapArrow == std::pair{links[a.link].from, links[a.link].line}; });
        it != f.arrows.end())
        f.chosenArrow = &*it;
    if (removeMapArrow(f) || mapClicks(f)) return true;
    drawMapArrows(f);
    drawMapScenes(f);
    if (mapLetGo(f)) return true;
    return false;
}

// Where each scene's box is: in its column (jumps from start, found going out from it; the ones the story
// never jumps to after the rest), one under another, or where it was put by hand; the one being moved,
// under the mouse.
void Developer::layOutMap(MapFrame& f) {
    DeveloperTools& d = tools;
    const StoryEditor& editor = d.editor;
    const float k = f.ui.k;
    const SDL_FRect& area = f.area;
    const auto& scenes = f.scenes;
    auto& byName = f.byName;
    auto& column = f.column;
    auto& boxes = f.boxes;
    for (std::size_t i = 0; i < scenes.size(); ++i) byName.emplace(lower(scenes[i].name), i);
    // Columns: steps from start.
    column.assign(scenes.size(), -1);
    std::vector<std::size_t> queue;
    if (const auto start = byName.find("start"); start != byName.end()) {
        column[start->second] = 0;
        queue.push_back(start->second);
    }
    for (std::size_t q = 0; q < queue.size(); ++q)
        for (const auto& link : f.links)
            if (link.from == queue[q])
                if (const auto to = byName.find(link.to); to != byName.end() && column[to->second] < 0) {
                    column[to->second] = column[queue[q]] + 1;
                    queue.push_back(to->second);
                }
    const int reached = f.reached = column.empty() ? 0 : std::ranges::max(column) + 1;
    for (int& c : column)
        if (c < 0) c = reached; // (never jumped to)
    const float nodeW = layout::nodeW * k, nodeH = layout::nodeH * k, gapX = layout::nodeGapX * k, gapY = layout::nodeGapY * k;
    boxes.assign(scenes.size(), SDL_FRect{});
    std::map<int, int> rowsIn;
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        if (const auto at = editor.mapPlace(scenes[i].name)) { // (put there by hand)
            boxes[i] = {area.x + at->x * k - d.mapX, area.y + at->y * k - d.mapY, nodeW, nodeH};
            continue;
        }
        const int row = rowsIn[column[i]]++;
        boxes[i] = {area.x + layout::mapPadX * k + static_cast<float>(column[i]) * (nodeW + gapX) - d.mapX,
                    area.y + f.top + static_cast<float>(row) * (nodeH + gapY) - d.mapY, nodeW, nodeH};
    }
    if (d.mapPress && d.mapPress->from >= scenes.size()) d.mapPress.reset();
    if (d.mapPress && !d.mapPress->dot && d.mapPress->moving) // (being moved)
        boxes[d.mapPress->from] = {d.mapPress->box.x + d.mouseX - d.mapPress->x, d.mapPress->box.y + d.mouseY - d.mapPress->y, nodeW, nodeH};
}

// The chosen arrow's x (at its middle), or Delete, takes it away (true: it did).
bool Developer::removeMapArrow(const MapFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const auto* chosenArrow = f.chosenArrow;
    if (!chosenArrow) return false;
    const SDL_FPoint mid = chosenArrow->curve[chosenArrow->curve.size() / 2];
    bool remove = ui.button({mid.x - layout::smallButtonH / 2 * k, mid.y - layout::smallButtonH / 2 * k, layout::smallButtonH * k, layout::smallButtonH * k}, "x", false, true, layout::smallButtonText);
    if (d.focus.empty()) {
        d.acts.remove = true;
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key)
            if (d.isKey(*key, ToolKey::Delete)) {
                d.keys.erase(key);
                remove = true;
                break;
            }
    }
    if (!remove) return false;
    const StoryEditor::Link& link = f.links[chosenArrow->link];
    const std::string from = f.scenes[link.from].name;
    editor.removeLink(link);
    d.mapArrow.reset();
    logInfo("{}", "'{}' no longer jumps to '{}'."_tr(from, link.to));
    return true;
}

// What's under the mouse (a scene, its dot, an arrow), and the clicks on them: a jump drawn from a dot, a
// scene chosen (to move or rename it) or opened (a double click: true), an arrow chosen, or the empty space
// dragged to look around.
bool Developer::mapClicks(MapFrame& f) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const auto& scenes = f.scenes;
    const auto& links = f.links;
    const auto& boxes = f.boxes;
    auto& hovered = f.hovered;
    auto& hoveredDot = f.hoveredDot;
    auto& hoveredArrow = f.hoveredArrow;
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        if (ui.over(boxes[i])) hovered = i;
        if (ui.over(f.dotOf(boxes[i]))) hoveredDot = i;
    }
    if (!hovered && !hoveredDot)
        for (const MapFrame::Arrow& a : f.arrows)
            if (ui.over(f.area) && onCurve(a.curve, d.mouseX, d.mouseY, k)) hoveredArrow = a.link;
    std::string renaming = d.focus.starts_with("map:rename:") ? d.focus : std::string{};
    if (d.clicked && d.doubleClick && hovered && renaming == std::format("map:rename:{}", *hovered) && d.buffer == scenes[*hovered].name)
        renaming.clear(); // (a double click on the chosen one: its first click began renaming it; it opens instead)
    if (d.clicked && ui.over(f.area)) {
        if (hoveredDot && renaming.empty()) { // a jump, drawn from its dot
            d.clicked = false;
            d.mapPress = DeveloperTools::MapPress{.from = *hoveredDot, .x = d.mouseX, .y = d.mouseY, .box = {}, .dot = true, .moving = false, .chosen = false};
        } else if (hovered && renaming != std::format("map:rename:{}", *hovered)) {
            d.clicked = false;
            d.focus.clear();
            d.mapArrow.reset();
            if (d.doubleClick) { // open it
                d.mapPress.reset();
                d.scene = *hovered;
                d.row.reset();
                d.visualType.reset();
                d.character.reset();
                d.namedVisual.reset();
                d.linesScroll = 0.0f;
                return true;
            }
            d.mapPress = DeveloperTools::MapPress{.from = *hovered, .x = d.mouseX, .y = d.mouseY, .box = {boxes[*hovered].x, boxes[*hovered].y},
                                                  .dot = false, .moving = false, .chosen = d.scene == *hovered};
            if (d.scene != *hovered) { // (another scene: its lines, in Lines, from the next frame)
                d.scene = *hovered;
                d.row.reset();
                d.visualType.reset();
                d.character.reset();
                d.namedVisual.reset();
                d.linesScroll = 0.0f;
                return true;
            }
        } else if (hoveredArrow) {
            d.clicked = false;
            d.mapArrow = std::pair{links[*hoveredArrow].from, links[*hoveredArrow].line};
        } else if (!hovered) {
            d.clicked = false;
            d.focus.clear();
            d.mapArrow.reset();
            d.mapDrag = SDL_FPoint{d.mouseX + d.mapX, d.mouseY + d.mapY};
        }
    }
    if (d.mapDrag) {
        d.mapX = d.mapDrag->x - d.mouseX;
        d.mapY = d.mapDrag->y - d.mouseY;
        if (!d.held) d.mapDrag.reset();
    }
    if (d.mapPress && !d.mapPress->moving && std::hypot(d.mouseX - d.mapPress->x, d.mouseY - d.mapPress->y) > 6 * k) d.mapPress->moving = true;
    f.target = d.mapPress && d.mapPress->dot && d.mapPress->moving && hovered != d.mapPress->from ? hovered : std::nullopt;
    return false;
}

// The arrows (the ones that have nothing to do with what's pointed at, faint), the jump being drawn, and
// the chosen arrow's x over them.
void Developer::drawMapArrows(const MapFrame& f) {
    const DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    const auto& links = f.links;
    const auto& boxes = f.boxes;
    const auto& hovered = f.hovered;
    const auto& hoveredArrow = f.hoveredArrow;
    const auto& target = f.target;
    for (const MapFrame::Arrow& a : f.arrows) {
        const StoryEditor::Link& link = links[a.link];
        const bool chosen = d.mapArrow == std::pair{link.from, link.line};
        const bool lit = chosen || hoveredArrow == a.link || hovered == link.from || (hovered && f.byName.at(link.to) == *hovered);
        SDL_Color c = chosen ? kAccent : kEdge;
        if (!lit && (hovered || hoveredArrow || d.mapArrow)) c.a = 90;
        drawCurve(r, a.curve, c, chosen || hoveredArrow == a.link, k);
    }
    if (d.mapPress && d.mapPress->dot && d.mapPress->moving) {
        const SDL_FRect a = boxes[d.mapPress->from];
        drawCurve(r, target ? arrowOf(a, boxes[*target], layout::nodeGapY * k, k) : curveOf({a.x + a.w, a.y + a.h / 2}, {d.mouseX, d.mouseY}, k), kAccent, true, k);
    }
    if (const auto* chosenArrow = f.chosenArrow) { // (its x, over the arrows)
        const SDL_FPoint mid = chosenArrow->curve[chosenArrow->curve.size() / 2];
        const SDL_FRect x{mid.x - layout::smallButtonH / 2 * k, mid.y - layout::smallButtonH / 2 * k, layout::smallButtonH * k, layout::smallButtonH * k};
        draw::rect(r, x, ui.over(x) ? kButtonHover : kButton);
        draw::outline(r, x, kAccent);
        ui.textIn("x", x, layout::smallButtonText, theme::text);
    }
}

// The scenes' boxes (red-edged: a mistake in it; dim: never jumped to), each with its dot; the one being
// renamed, a box to type into (the jumps to it follow as it's typed).
void Developer::drawMapScenes(const MapFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    const auto& scenes = f.scenes;
    const auto& boxes = f.boxes;
    const auto& hovered = f.hovered;
    const auto& hoveredDot = f.hoveredDot;
    const auto& target = f.target;
    const std::vector<bool>& wrong = d.wrongScenes(); // (as of this version of the scripts)
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        const bool unreached = f.column[i] == f.reached;
        draw::rect(r, boxes[i], i == d.scene ? kChosen : hovered == i ? kButtonHover : kButton);
        draw::outline(r, boxes[i], target == i ? kAccent : wrong[i] ? kBad : kEdge);
        const std::string id = std::format("map:rename:{}", i);
        if (d.focus == id) { // (renamed as it's typed: the jumps to it follow)
            if (const auto name = ui.field(id, boxes[i], scenes[i].name, layout::listText)) {
                d.saidRenamed(editor.renameScene(i, std::string(trim(*name)), std::format("map-name:{}:{}", i, d.focusCount)), "A scene's name is one word (letters, numbers, _ and -) that no other scene has."_tr);
            }
            continue;
        }
        const SDL_FRect nameBox{boxes[i].x, boxes[i].y, boxes[i].w - 14 * k, boxes[i].h}; // (its name, cut to fit: its whole in a tip)
        const std::string name = ui.fitted(scenes[i].name, layout::listText, nameBox.w - 12 * k);
        ui.textIn(name, nameBox, layout::listText, unreached && scenes[i].name != "start" ? theme::dim : theme::text);
        if (name != scenes[i].name) ui.tip(nameBox, scenes[i].name);
        const SDL_FRect dot = f.dotOf(boxes[i]);
        const SDL_Color dc = hoveredDot == i || (d.mapPress && d.mapPress->dot && d.mapPress->from == i) ? kAccent : kEdge;
        draw::rect(r, {dot.x + 3 * k, dot.y + 3 * k, dot.w - 6 * k, dot.h - 6 * k}, dc);
    }
}

// A scene let go: a jump drawn from its dot to another is written (true), one moved stays where it was
// put, and the chosen one clicked again has its name to type over.
bool Developer::mapLetGo(const MapFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = f.ui.k;
    const SDL_FRect& area = f.area;
    const auto& scenes = f.scenes;
    const auto& boxes = f.boxes;
    const auto& target = f.target;
    if (!d.mapPress || d.held) return false;
    const DeveloperTools::MapPress p = *d.mapPress;
    d.mapPress.reset();
    if (p.dot && p.moving && target) {
        const std::string from = scenes[p.from].name, to = scenes[*target].name;
        const bool changed = editor.jumpAtEnd(p.from, to);
        logInfo("{}", changed ? "'{}' jumps to '{}' at its end now, instead of what it did."_tr(from, to) : "'{}' jumps to '{}' at its end now."_tr(from, to));
        return true;
    }
    if (!p.dot && p.moving) { // (it stays where it was put)
        editor.setMapPlace(scenes[p.from].name, SDL_FPoint{(boxes[p.from].x - area.x + d.mapX) / k, (boxes[p.from].y - area.y + d.mapY) / k});
    } else if (!p.dot && p.chosen) { // (the chosen one clicked again: its name, to type over)
        d.focusOn(std::format("map:rename:{}", p.from), scenes[p.from].name, true);
    }
    return false;
}

} // namespace vn
