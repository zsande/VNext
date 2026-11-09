// The Library's atlases and sub-images: an image with its cells or parts marked, picked by clicking.
#include "DeveloperUi.hpp"

#include <cmath>
#include <cstdlib>
#include <ranges>

namespace vn {

// An atlas over its form: on the left its image, the frames marked on it and each expression's first
// named (a click shows that expression on the right); on the right that frame, as big as fits, with its
// eyes and mouth parts where they go: dragging one moves it there (its x= and y=).
void Developer::drawAtlas(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    draw::rect(r, box, kBox);
    const Atlas* a = nullptr;
    if (d.script)
        for (const Atlas& each : d.script->atlases)
            if (each.name == lower(def.name)) a = &each;
    const Image sheet = a ? engine.assets().file(a->image) : Image{};
    if (!a || !sheet) {
        if (a) ui.textAround("There's no image called '{}'."_tr(a->image), box.x + 10 * k, box.y + box.h / 2, layout::noteText, theme::dim, box.w - 20 * k);
        draw::outline(r, box, kEdge);
        return;
    }
    // The frames, as the player cuts them (Assets::image).
    const int expressions = std::max<int>(1, static_cast<int>(a->expressions.size()));
    const int eyes = a->eyesPart ? 1 : std::max<int>(1, static_cast<int>(a->eyes.size()));
    const int mouths = a->mouthPart ? 1 : std::max<int>(1, static_cast<int>(a->mouth.size()));
    const int perExpression = eyes * mouths;
    const int columns = a->columns > 0 ? a->columns : perExpression > 1 ? perExpression : expressions;
    const int rows = (expressions * perExpression + columns - 1) / columns;
    const float fw = sheet.w() / static_cast<float>(columns), fh = sheet.h() / static_cast<float>(rows);
    d.atlasExpression = std::min<std::size_t>(d.atlasExpression, static_cast<std::size_t>(expressions - 1));
    const float pad = 8 * k, half = (box.w - 3 * pad) / 2;
    const SDL_FRect left = fitIn({box.x + pad, box.y + pad, half, box.h - 2 * pad}, sheet.w(), sheet.h());
    sheet.draw(r, left, 1.0f);
    const float s = left.w / sheet.w();
    for (int i = 0; i < expressions * perExpression; ++i) {
        const SDL_FRect cell{left.x + static_cast<float>(i % columns) * fw * s, left.y + static_cast<float>(i / columns) * fh * s, fw * s, fh * s};
        const int expression = i / perExpression;
        const bool first = i % perExpression == 0, chosen = expression == static_cast<int>(d.atlasExpression);
        draw::outline(r, cell, chosen ? kAccent : SDL_Color{kEdge.r, kEdge.g, kEdge.b, 140});
        if (first && expression < static_cast<int>(a->expressions.size())) { // (its name at its top, cut to fit: its whole in a tip)
            const DeveloperUi::Clip clipped(ui, cell);
            ui.textFitted(a->expressions[static_cast<std::size_t>(expression)], cell.x + 3 * k, cell.y + 2 * k + ui.measure("Ag", layout::sampleText).y / 2,
                          layout::sampleText, chosen ? kAccent : theme::text, cell.w - 6 * k);
        }
        if (ui.click(cell)) d.atlasExpression = static_cast<std::size_t>(expression);
    }
    // The chosen expression's frame, with its parts.
    const int index = static_cast<int>(d.atlasExpression) * perExpression;
    const Image frame = sheet.crop({fw * static_cast<float>(index % columns), fh * static_cast<float>(index / columns), fw, fh});
    const SDL_FRect right = fitIn({box.x + 2 * pad + half, box.y + pad, half, box.h - 2 * pad}, fw, fh);
    draw::rect(r, right, {40, 40, 48, 255});
    frame.draw(r, right, 1.0f);
    const float fs = right.w / fw;
    struct Shown {
        bool mouth;
        SDL_FRect at;
    };
    std::vector<Shown> parts;
    for (const bool mouth : {false, true}) {
        const auto& part = mouth ? a->mouthPart : a->eyesPart;
        const std::size_t states = mouth ? a->mouth.size() : a->eyes.size();
        if (!part || states == 0) continue;
        const Image pic = engine.assets().file(part->image);
        if (!pic) continue;
        const float cw = pic.w() / static_cast<float>(states), ch = pic.h() / static_cast<float>(expressions);
        float px = part->x, py = part->y;
        if (d.partDrag && d.partDrag->mouth == mouth) { // (where it's being dragged)
            px = std::round(d.partDrag->px + (d.mouseX - d.partDrag->x) / fs);
            py = std::round(d.partDrag->py + (d.mouseY - d.partDrag->y) / fs);
        }
        const SDL_FRect at{right.x + px * fs, right.y + py * fs, cw * fs, ch * fs};
        pic.crop({0.0f, ch * static_cast<float>(d.atlasExpression), cw, ch}).draw(r, at, 1.0f);
        draw::outline(r, at, ui.over(at) || (d.partDrag && d.partDrag->mouth == mouth) ? kAccent : kOptionArrow);
        parts.push_back({mouth, at});
    }
    for (const Shown& p : parts | std::views::reverse) // (the one on top first)
        if (ui.click(p.at)) {
            const auto& part = p.mouth ? a->mouthPart : a->eyesPart;
            d.partDrag = DeveloperTools::PartDrag{.mouth = p.mouth, .x = d.mouseX, .y = d.mouseY, .px = part->x, .py = part->y, .key = ++d.focusCount};
        }
    if (d.partDrag && !d.held) { // let go: where it is now, written
        const DeveloperTools::PartDrag g = *d.partDrag;
        d.partDrag.reset();
        const float px = std::round(g.px + (d.mouseX - g.x) / fs), py = std::round(g.py + (d.mouseY - g.y) / fs);
        if (px != g.px || py != g.py) {
            auto fields = d.editor.fields("atlas", def);
            const std::string n = g.mouth ? "Mouth" : "Eyes";
            for (auto& f : fields)
                if (f.name == n + "' x") f.value = std::format("{}", std::lround(px));
                else if (f.name == n + "' y") f.value = std::format("{}", std::lround(py));
            d.editor.setDefinition(def, d.editor.linesOf("atlas", fields), std::format("atlas-part:{}", g.key));
        }
    }
    if (parts.empty()) ui.textAround(a->expressions.empty() ? "" : a->expressions[d.atlasExpression], right.x + 6 * k, right.y + 12 * k, layout::sampleText, theme::text);
    draw::outline(r, box, kEdge);
}

namespace {

// The piece of an image at a point (in its pixels): the box around the pixels joined to it (gaps of a
// pixel bridged), on a see-through background or one the colour of its top left corner. None on the
// background.
std::optional<SDL_FRect> pieceAt(SDL_Surface* pixels, SDL_FPoint at) {
    if (!pixels || pixels->format != SDL_PIXELFORMAT_RGBA32) return std::nullopt;
    const int w = pixels->w, h = pixels->h, x0 = static_cast<int>(at.x), y0 = static_cast<int>(at.y);
    if (x0 < 0 || y0 < 0 || x0 >= w || y0 >= h) return std::nullopt;
    const auto* bytes = static_cast<const Uint8*>(pixels->pixels);
    auto pixel = [&](int x, int y) { return bytes + static_cast<std::ptrdiff_t>(y) * pixels->pitch + x * 4; };
    bool seeThrough = false;
    for (int y = 0; y < h && !seeThrough; ++y)
        for (int x = 0; x < w && !seeThrough; ++x) seeThrough = pixel(x, y)[3] < 255;
    const Uint8* corner = pixel(0, 0);
    auto solid = [&](int x, int y) {
        const Uint8* c = pixel(x, y);
        return seeThrough ? c[3] >= 24 : std::abs(c[0] - corner[0]) + std::abs(c[1] - corner[1]) + std::abs(c[2] - corner[2]) > 48;
    };
    if (!solid(x0, y0)) return std::nullopt;
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    std::vector<int> next{y0 * w + x0};
    seen[static_cast<std::size_t>(next.back())] = 1;
    int left = x0, top = y0, right = x0, bottom = y0;
    while (!next.empty()) {
        const int i = next.back();
        next.pop_back();
        const int x = i % w, y = i / w;
        left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                std::uint8_t& s = seen[static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx)];
                if (s || !solid(nx, ny)) continue;
                s = 1;
                next.push_back(ny * w + nx);
            }
    }
    return SDL_FRect{static_cast<float>(left), static_cast<float>(top), static_cast<float>(right - left + 1), static_cast<float>(bottom - top + 1)};
}

} // namespace

// A sub-image over its form: on the left the image it's cut from, with the part marked (drag a
// box on it, or click a piece of it to cut out just that piece), and on the right the part.
void Developer::drawSubImage(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    draw::rect(r, box, kBox);
    const SubImage* p = nullptr;
    if (d.script)
        for (const SubImage& each : d.script->subImages)
            if (each.name == lower(def.name)) p = &each;
    const Image sheet = p ? engine.assets().file(p->image) : Image{};
    if (!p || !sheet) {
        if (p) ui.textAround("There's no image called '{}'."_tr(p->image), box.x + 10 * k, box.y + box.h / 2, layout::noteText, theme::dim, box.w - 20 * k);
        draw::outline(r, box, kEdge);
        return;
    }
    const float pad = 8 * k, leftW = std::round((box.w - 3 * pad) * 0.66f);
    const SDL_FRect at = fitIn({box.x + pad, box.y + pad, leftW, box.h - 2 * pad}, sheet.w(), sheet.h());
    draw::rect(r, at, {40, 40, 48, 255});
    sheet.draw(r, at, 1.0f);
    const float s = at.w / sheet.w();
    auto inImage = [&](float x, float y) {
        return SDL_FPoint{std::round(std::clamp((x - at.x) / s, 0.0f, sheet.w())), std::round(std::clamp((y - at.y) / s, 0.0f, sheet.h()))};
    };
    // The part: as it's written, or the box being dragged.
    SDL_FRect part{};
    const SDL_FRect all{0.0f, 0.0f, sheet.w(), sheet.h()};
    if (d.cutDrag) {
        const SDL_FPoint a = d.cutDrag->from, b = inImage(d.mouseX, d.mouseY);
        part = {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
    } else if (!SDL_GetRectIntersectionFloat(&p->rect, &all, &part)) {
        part = {};
    }
    const SDL_FRect shown{at.x + part.x * s, at.y + part.y * s, part.w * s, part.h * s};
    const SDL_Color shade{0, 0, 0, 150}; // (the rest of the image, darker)
    draw::rect(r, {at.x, at.y, at.w, shown.y - at.y}, shade);
    draw::rect(r, {at.x, shown.y + shown.h, at.w, at.y + at.h - shown.y - shown.h}, shade);
    draw::rect(r, {at.x, shown.y, shown.x - at.x, shown.h}, shade);
    draw::rect(r, {shown.x + shown.w, shown.y, at.x + at.w - shown.x - shown.w, shown.h}, shade);
    draw::outline(r, shown, kAccent);
    if (ui.click(at)) d.cutDrag = DeveloperTools::CutDrag{.from = inImage(d.mouseX, d.mouseY), .x = d.mouseX, .y = d.mouseY};
    if (d.cutDrag && !d.held) { // let go: a click takes the piece there, a drag the box
        const DeveloperTools::CutDrag g = *d.cutDrag;
        d.cutDrag.reset();
        std::optional<SDL_FRect> cut = part;
        if (std::abs(d.mouseX - g.x) + std::abs(d.mouseY - g.y) < 4 * k) {
            const SurfacePtr pixels = engine.assets().pixels(p->image);
            cut = pieceAt(pixels.get(), g.from);
        }
        if (cut && cut->w >= 1.0f && cut->h >= 1.0f) {
            auto fields = d.editor.fields("sub-image", def);
            for (auto& f : fields)
                if (f.name == "X") f.value = std::format("{}", std::lround(cut->x));
                else if (f.name == "Y") f.value = std::format("{}", std::lround(cut->y));
                else if (f.name == "Width") f.value = std::format("{}", std::lround(cut->w));
                else if (f.name == "Height") f.value = std::format("{}", std::lround(cut->h));
            d.editor.setDefinition(def, d.editor.linesOf("sub-image", fields), {});
        }
    }
    // The part, as big as it fits (up to four times its size).
    if (part.w >= 1.0f && part.h >= 1.0f) {
        const SDL_FRect room{box.x + 2 * pad + leftW, box.y + pad, box.w - 3 * pad - leftW, box.h - 2 * pad};
        const float scale = std::min({4.0f, room.w / part.w, room.h / part.h});
        const SDL_FRect drawn{room.x + (room.w - part.w * scale) / 2, room.y + (room.h - part.h * scale) / 2, part.w * scale, part.h * scale};
        draw::rect(r, drawn, {40, 40, 48, 255});
        sheet.crop(part).draw(r, drawn, 1.0f);
        draw::outline(r, drawn, kEdge);
    }
    draw::outline(r, box, kEdge);
}

} // namespace vn
