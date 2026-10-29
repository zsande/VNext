// The Map tab: the scenes as boxes, with arrows where the story goes from one to another.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

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

// An arrow along a curve, with its head at the end: dashed (a call), or thick (chosen, pointed at).
void drawCurve(SDL_Renderer* r, const std::vector<SDL_FPoint>& curve, SDL_Color c, bool dashed, bool thick, float k) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int pass = 0; pass < (thick ? 2 : 1); ++pass) { // (thick: once more, a pixel lower)
        std::vector<SDL_FPoint> line = curve;
        for (SDL_FPoint& p : line) p.y += static_cast<float>(pass);
        if (!dashed) SDL_RenderLines(r, line.data(), static_cast<int>(line.size()));
        else
            for (std::size_t s = 0; s + 1 < line.size(); s += 2) SDL_RenderLine(r, line[s].x, line[s].y, line[s + 1].x, line[s + 1].y);
    }
    const SDL_FPoint p3 = curve.back();
    SDL_RenderLine(r, p3.x - 8 * k, p3.y - 5 * k, p3.x, p3.y);
    SDL_RenderLine(r, p3.x - 8 * k, p3.y + 5 * k, p3.x, p3.y);
}

} // namespace

// The Map tab as it's drawn this frame: the scenes and the links between them (a copy: taking one away
// changes the editor's), each scene's column (jumps from start; `reached`: the column of the ones never
// jumped to) and box, the arrows, and what's under the mouse.
struct Developer::MapFrame {
    struct Arrow {
        std::size_t link = 0;
        std::vector<SDL_FPoint> curve;
    };
    DeveloperUi& ui;
    SDL_FRect area;
    const std::vector<StoryEditor::Scene>& scenes;
    std::vector<StoryEditor::Link> links;
    std::map<std::string, std::size_t> byName{};
    std::vector<int> column{};
    int reached = 0;
    bool unreachedLaidOut = false; // (some of them are in that column)
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

// The Map tab: every scene as a box, in columns by how many jumps they are from start (the ones the
// story never jumps to after them), or where it was put by hand, with arrows for jumps, calls and
// options' arrows. A click chooses a scene, another click renames it, and a double click opens it in
// the Story tab; dragging one moves it, and dragging the dot at its right to another makes it jump
// there. A click chooses an arrow, and its x (or Delete) takes it away. Dragging the space between
// them (or the wheel) looks around.
void Developer::drawMap(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    MapFrame f{.ui = ui, .area = area, .scenes = editor.scenes(), .links = editor.links()};
    const auto& scenes = f.scenes;
    const auto& links = f.links;
    layOutMap(f);
    draw::rect(r, area, kBox);
    const DeveloperUi::Clip clipped(ui, area);
    // Looking around: the wheel (Shift: sideways), or dragging the empty space.
    if (const float turns = ui.wheelOver(area); turns != 0.0f) {
        const bool sideways = SDL_GetModState() & SDL_KMOD_SHIFT;
        (sideways ? d.mapX : d.mapY) -= turns * layout::mapStep * k;
    }
    // + New scene (in the column of scenes nothing jumps to, to draw a jump to).
    const float newW = ui.buttonWidth("+ New scene", layout::buttonText, layout::optionButtonW);
    if (ui.button({area.x + area.w - newW - 10 * k, area.y + layout::gap * k, newW, layout::mediumButtonH * k}, "+ New scene", false, true, layout::buttonText)) {
        d.scene = editor.addScene(d.scene < scenes.size() ? scenes[d.scene].file : 0);
        d.row.reset();
        d.character.reset();
        return;
    }
    // The arrows: out to the right of one box, in from the left of another (or to the mouse).
    for (std::size_t i = 0; i < links.size(); ++i)
        if (const auto to = f.byName.find(links[i].to); to != f.byName.end()) {
            const SDL_FRect a = f.boxes[links[i].from], b = f.boxes[to->second];
            f.arrows.push_back({i, curveOf({a.x + a.w, a.y + a.h / 2}, {b.x, b.y + b.h / 2}, k)});
        }
    if (d.mapArrow && std::ranges::none_of(links, [&](const auto& l) { return std::pair{l.from, l.line} == *d.mapArrow; })) d.mapArrow.reset();
    if (const auto it = std::ranges::find_if(f.arrows, [&](const MapFrame::Arrow& a) { return d.mapArrow == std::pair{links[a.link].from, links[a.link].line}; });
        it != f.arrows.end())
        f.chosenArrow = &*it;
    if (removeMapArrow(f) || mapClicks(f)) return;
    drawMapArrows(f);
    drawMapScenes(f);
    if (mapLetGo(f)) return;
    // What the arrows mean, the column of scenes nothing jumps to, and what can be done.
    float lx = area.x + 12 * k;
    for (const auto& [label, color] : {std::pair{"Jump", kEdge}, {"Call (dashed)", kWarn}, {"Option", kOptionArrow}}) {
        draw::rect(r, {lx, area.y + 14 * k, 18 * k, 3 * k}, color);
        lx += 24 * k + ui.text(label, lx + 24 * k, area.y + 6 * k, layout::smallText, theme::dim).x + 14 * k;
    }
    ui.text("Drag a scene to move it, or its dot to another to jump there. Click an arrow to take it away; double-click a scene to open it.", lx + 10 * k,
            area.y + 6 * k, layout::noteText, theme::dim, area.x + area.w - 170 * k - lx);
    const float nodeW = layout::nodeW * k, gapX = layout::nodeGapX * k;
    if (f.unreachedLaidOut && f.reached < static_cast<int>(scenes.size()))
        ui.text("Nothing jumps to these", area.x + layout::mapPadX * k + static_cast<float>(f.reached) * (nodeW + gapX) - d.mapX, area.y + layout::mapNoteY * k - d.mapY,
                layout::smallText, theme::dim);
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
        f.unreachedLaidOut = f.unreachedLaidOut || column[i] == reached;
        boxes[i] = {area.x + layout::mapPadX * k + static_cast<float>(column[i]) * (nodeW + gapX) - d.mapX,
                    area.y + layout::mapPadY * k + static_cast<float>(row) * (nodeH + gapY) - d.mapY, nodeW, nodeH};
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
    if (d.focus.empty())
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key)
            if (key->key == SDLK_DELETE || key->key == SDLK_BACKSPACE) {
                d.keys.erase(key);
                remove = true;
                break;
            }
    if (!remove) return false;
    const StoryEditor::Link& link = f.links[chosenArrow->link];
    const std::string from = f.scenes[link.from].name;
    editor.removeLink(link);
    d.mapArrow.reset();
    d.failed = false;
    d.status = link.option ? std::format("An option in '{}' goes on after its choice now, instead of to '{}'.", from, link.to)
                           : std::format("'{}' no longer {} '{}'.", from, link.call ? "calls" : "jumps to", link.to);
    return true;
}

// What's under the mouse (a scene, its dot, an arrow), and the clicks on them: a jump drawn from a dot, a
// scene chosen (to move or rename it) or opened (a double click: true, in the Story tab), an arrow
// chosen, or the empty space dragged to look around.
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
                d.character.reset();
                d.rowsScroll = 0.0f;
                d.view = DeveloperTools::View::Story;
                return true;
            }
            d.mapPress = DeveloperTools::MapPress{.from = *hovered, .x = d.mouseX, .y = d.mouseY, .box = {boxes[*hovered].x, boxes[*hovered].y},
                                                  .dot = false, .moving = false, .chosen = d.scene == *hovered};
            d.scene = *hovered;
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
        SDL_Color c = chosen ? kAccent : link.option ? kOptionArrow : link.call ? kWarn : kEdge;
        if (!lit && (hovered || hoveredArrow || d.mapArrow)) c.a = 90;
        drawCurve(r, a.curve, c, link.call, chosen || hoveredArrow == a.link, k);
    }
    if (d.mapPress && d.mapPress->dot && d.mapPress->moving) {
        const SDL_FRect a = boxes[d.mapPress->from];
        const SDL_FPoint to = target ? SDL_FPoint{boxes[*target].x, boxes[*target].y + boxes[*target].h / 2} : SDL_FPoint{d.mouseX, d.mouseY};
        drawCurve(r, curveOf({a.x + a.w, a.y + a.h / 2}, to, k), kAccent, false, true, k);
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
    std::vector<bool> wrong(scenes.size());
    for (const ScriptError& e : d.problems)
        if (const auto i = sceneOf(editor, e)) wrong[*i] = true;
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        const bool unreached = f.column[i] == f.reached;
        draw::rect(r, boxes[i], i == d.scene ? kChosen : hovered == i ? kButtonHover : kButton);
        draw::outline(r, boxes[i], target == i ? kAccent : wrong[i] ? kBad : kEdge);
        const std::string id = std::format("map:rename:{}", i);
        if (d.focus == id) { // (renamed as it's typed: the jumps to it follow)
            if (const auto name = ui.field(id, boxes[i], scenes[i].name, layout::listText)) {
                d.saidRenamed(editor.renameScene(i, std::string(trim(*name)), std::format("map-name:{}:{}", i, d.focusCount)), "A scene's name is one word (letters, numbers, _ and -) that no other scene has.");
            }
            continue;
        }
        ui.textIn(scenes[i].name, {boxes[i].x, boxes[i].y, boxes[i].w - 14 * k, boxes[i].h}, layout::listText,
                  unreached && scenes[i].name != "start" ? theme::dim : theme::text);
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
        d.failed = false;
        d.status = changed ? std::format("'{}' jumps to '{}' at its end now, instead of what it did.", from, to) : std::format("'{}' jumps to '{}' at its end now.", from, to);
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
