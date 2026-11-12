// The Library's animations: each as a timeline of changes, played as a sample, and the ones to start from.
#include "DeveloperUi.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <numbers>
#include <ranges>

namespace vn {

namespace {

// + New animation's animations to start from, as + New screen has screens: its name ("": an empty one
// to fill in), what the list calls it, and its lines under it ("|" between them, as the timeline writes
// them; `loops`: looping).
struct AnimationStart {
    std::string_view name;
    ToolsText label;
    std::string_view steps;
    bool loops = false;
};

const std::array kAnimationStarts{
    AnimationStart{"", "Empty animation", ""},
    AnimationStart{"fade_in", "Fade in", "opacity=0%|opacity=100% over 0.5 seconds"},
    AnimationStart{"fade_out", "Fade out", "opacity=0% over 0.5 seconds"},
    AnimationStart{"fade_through_black", "Fade through black",
                   "opacity=0%|fade=black|opacity=100% over 0.5 seconds|fade=none over 0.5 seconds"},
    AnimationStart{"flash", "Flash", "fade=white|fade=none over 0.6 seconds"},
    AnimationStart{"slide_in_left", "Slide in from the left", "x=-2|x=home over 0.6 seconds"},
    AnimationStart{"slide_in_right", "Slide in from the right", "x=2|x=home over 0.6 seconds"},
    AnimationStart{"slide_out_left", "Slide out to the left", "x=-2 over 0.6 seconds"},
    AnimationStart{"slide_out_right", "Slide out to the right", "x=2 over 0.6 seconds"},
    AnimationStart{"wipe_in", "Wipe in", "wipe=0%|wipe=100% over 0.75 seconds"},
    AnimationStart{"wipe_out", "Wipe out", "wipe=0% over 0.75 seconds"},
    AnimationStart{"pop_in", "Pop in", "zoom=60% opacity=0%|zoom=100% opacity=100% over 0.35 seconds ease=overshoot"},
    AnimationStart{"hop", "Hop", "up=4% over 0.15 seconds ease=out|y=home over 0.15 seconds ease=in"},
    AnimationStart{"shake", "Shake", "left=1% over 0.05 seconds|right=1% over 0.1 seconds|left=1% over 0.1 seconds|x=home over 0.05 seconds"},
    AnimationStart{"float", "Float", "up=1% over 1.5 seconds|y=home over 1.5 seconds", true},
};

std::vector<std::string> startLines(const AnimationStart& start) {
    std::vector<std::string> out;
    for (const auto part : std::views::split(start.steps, '|'))
        if (const std::string_view step(part.begin(), part.end()); !step.empty()) out.emplace_back(step);
    return out;
}

bool byAmount(std::string_view key) { return key == "left" || key == "right" || key == "up" || key == "down"; }

// A new change's easing on the timeline's row `row`: as the scripts have it for the row's first key.
std::string_view rowEase(std::size_t row) { return usualEase(wordsOf(kTimelineRows[row].keys).front()); }

// A timeline's changes as the game plays them, `times` times (0: again and again). (A value that
// doesn't read: as if it didn't change.)
Animation animationOf(const StoryEditor::Timeline& t, int times, const Palette& palette) {
    using V = Animation::Value;
    Animation a{.length = t.length, .times = times};
    for (const StoryEditor::Bar& b : t.bars) {
        const auto ease = std::ranges::find(kEaseNames, b.ease, &std::pair<std::string_view, Ease>::first);
        Animation::Change c{.start = b.start, .seconds = b.seconds, .ease = ease != std::end(kEaseNames) ? ease->second : Ease::InOut};
        const auto n = parseNumber(b.value);
        if (b.key == "x" || b.key == "y") {
            c.what = b.key == "x" ? V::X : V::Y;
            c.to = n && lower(b.value) != "home" ? *n : std::numeric_limits<float>::quiet_NaN();
        } else if (byAmount(b.key)) {
            c.what = b.key == "left" || b.key == "right" ? V::X : V::Y;
            c.by = true;
            c.to = (b.key == "left" || b.key == "down" ? -1.0f : 1.0f) * n.value_or(0.0f);
        } else if (b.key == "wipe") {
            c.what = V::Wipe;
            c.to = std::clamp(n.value_or(1.0f), 0.0f, 1.0f);
            c.shape = b.shape;
        } else if (b.key == "colour" || b.key == "fade") {
            c.what = b.key == "colour" ? V::Colour : V::Fade;
            const SDL_Color usual = b.key == "fade" ? SDL_Color{0, 0, 0, 255} : SDL_Color{255, 255, 255, 255};
            c.colour = b.key == "fade" && lower(b.value) == "none" ? SDL_Color{0, 0, 0, 0} : parseColour(b.value, &palette).value_or(usual);
        } else {
            c.what = b.key == "opacity" ? V::Opacity : b.key == "zoom" ? V::Zoom : V::Rotation;
            c.to = n.value_or(b.key == "rotation" ? 0.0f : 1.0f);
        }
        a.changes.push_back(c);
    }
    std::ranges::stable_sort(a.changes, {}, &Animation::Change::start);
    return a;
}

// What a change goes to as its box shows it: an amount with its way (5% left), a percent as one.
std::string barShown(const StoryEditor::Bar& b) {
    if (byAmount(b.key)) return percentRead(b.value) + " " + b.key;
    if (b.key == "opacity" || b.key == "zoom" || b.key == "wipe") return percentRead(b.value);
    return b.value;
}

// ...and back: what's written for a value typed into it, on the timeline's row `row` ("5% left" across: left=5%).
StoryEditor::Bar barWritten(StoryEditor::Bar b, std::size_t row, std::string_view typed) {
    const auto keys = wordsOf(kTimelineRows[row].keys);
    b.key = std::string(keys.front());
    b.value = std::string(trim(typed));
    if (const auto w = rawWords(b.value); w.size() > 1)
        for (const std::string_view way : std::span(keys).subspan(1))
            if (lower(w.back()) == way) {
                b.key = std::string(way);
                b.value = percentText(joined(std::vector<std::string>(w.begin(), w.end() - 1)));
            }
    if (b.key == "opacity" || b.key == "zoom" || b.key == "wipe") b.value = percentText(b.value);
    if (b.key != "wipe") b.shape.clear();
    return b;
}

float lastEnd(const std::vector<StoryEditor::Bar>& bars) {
    float last = 0.0f;
    for (const StoryEditor::Bar& b : bars) last = std::max(last, b.start + b.seconds);
    return last;
}

// How many seconds the timeline shows across: a round number, a little past `seconds`.
float roundSpan(float seconds) {
    for (const float step : {0.5f, 1.0f, 2.0f, 5.0f, 10.0f})
        if (seconds <= step * 10.0f) return std::ceil(seconds / step) * step;
    return std::ceil(seconds / 30.0f) * 30.0f;
}

// The animation written again with `t` (its own line as it is; one time through as long as it was after
// its last change, or as `t` says when that's longer), and the change `chosen` still chosen.
void writeTimeline(DeveloperTools& d, const StoryEditor::Definition& def, StoryEditor::Timeline t, const std::optional<StoryEditor::Bar>& chosen,
                   const std::string& key) {
    t.length = std::max(t.length, lastEnd(t.bars));
    const std::vector<std::string> now = d.editor.linesOf(def);
    std::vector<std::string> lines{now.front()}; // (its own line; the notes under it, on lines of their own or after one, under that)
    for (std::size_t i = 1; i < now.size(); ++i)
        if (const std::string_view note = trim(splitNote(now[i]).second); !note.empty()) lines.emplace_back(note);
    const std::vector<std::string> steps = StoryEditor::stepsOf(t);
    lines.insert(lines.end(), steps.begin(), steps.end());
    d.animation.bar.reset();
    if (chosen) { // (where it is in the order they're in now)
        const auto again = StoryEditor::timelineOf(steps).bars;
        for (std::size_t i = 0; i < again.size(); ++i)
            if (again[i].key == chosen->key && again[i].value == chosen->value && sameMoment(again[i].start, chosen->start) && sameMoment(again[i].seconds, chosen->seconds))
                d.animation.bar = i;
    }
    d.editor.setDefinition(def, lines, key);
}

} // namespace

void Developer::drawAnimation(DeveloperUi& ui, const StoryEditor::Definition& given, const SDL_FRect& room) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Played = DeveloperTools::AnimationView;
    using Field = StoryEditor::Field;
    const StoryEditor::Definition def = given; // (a copy: writing it makes the Library's list again)
    Played& v = d.animation;
    const float k = ui.k, buttonH = layout::buttonH * k, boxH = layout::boxH * k, rowGap = layout::rowGap * k, gap = layout::gap * k;
    const std::vector<std::string> lines = editor.linesOf(def);
    const StoryEditor::Timeline timeline = StoryEditor::timelineOf(std::span(lines).subspan(1));
    auto fields = editor.fields("animation", def);
    const std::string plays = fieldValue(fields, "Plays");
    if (v.name != def.name) v = Played{.name = def.name, .playedAt = engine.toolsTime()};
    if (v.bar && *v.bar >= timeline.bars.size()) v.bar.reset();
    // The playhead goes on while it plays: round again after the end (a moment after, unless it plays again and again).
    const float now = engine.toolsTime();
    if (v.playing && v.drag != Played::Drag::Playhead) {
        v.playhead += now - v.playedAt;
        if (v.playhead > timeline.length + (plays == "loop" ? 0.0f : 0.6f)) v.playhead = 0.0f;
    }
    v.playedAt = now;

    // Its preview, under a heading with Play or Pause (as every preview has its buttons), then the rest
    // under it (that scrolls).
    const float sampleH = std::round(std::min(layout::sampleAnimationH * k, room.h * layout::sampleAnimationPart));
    const SDL_FRect headRow{room.x, room.y, room.w, layout::headingH * k};
    PreviewHead head(ui, headRow);
    if (head.button(v.playing ? "Pause"_tr : "Play"_tr, false, timeline.length > 0.0f)) {
        v.playing = !v.playing;
        if (v.playing && v.playhead >= timeline.length) v.playhead = 0.0f;
    }
    const std::string at = secondsText(std::min(v.playhead, timeline.length)), of = secondsText(timeline.length);
    head.title(of == "1" ? "Preview ({} of 1 second)"_tr(at) : "Preview ({} of {} seconds)"_tr(at, of));
    const SDL_FRect shown = drawAnimationSample(ui, animationOf(timeline, 1, engine.palette()), std::min(v.playhead, timeline.length),
                                                {room.x, room.y + layout::previewHeadH * k, room.w, sampleH});
    const float top = shown.y + shown.h + rowGap;
    const SDL_FRect formBox{room.x, top, room.w, room.y + room.h - top};
    ui.scrollForm(formBox, boxH + rowGap);
    float y = formBox.y - d.formScroll;
    const DeveloperUi::Clip clipped(ui, formBox);
    const float least = std::round(room.w * layout::labelPart); // (the labels' column, as wide as they need: each of its two forms its own)
    for (std::size_t l = def.line; l < def.end; ++l)
        if (const ScriptError* problem = d.problemAt(def.file, l)) y += ui.text(problem->message, room.x, y, layout::noteText, kBad, room.w).y + rowGap;

    // Its name and how often it plays.
    const std::string id = std::format("def:animation:{}", def.line);
    if (const auto edit = drawForm(ui, id, fields, room.x, room.w, labelColumn(ui, least, room.w, {fields}), y)) {
        auto changed = fields;
        std::ranges::find(changed, edit->name, &Field::name)->value = edit->value;
        std::vector<std::string> written = editor.linesOf("animation", changed);
        written.insert(written.end(), lines.begin() + 1, lines.end()); // (the lines under it as they are)
        editor.setDefinition(def, written, edit->typed ? std::format("{}:{}{}", id, edit->name, d.focusCount) : std::string{});
        if (!edit->typed) d.focus.clear();
        return; // (written: drawn again from it next frame, as tall as it was)
    }
    y += rowGap;

    // Its timeline (under its title, as a list is), with + New change under it (at the playhead: on the
    // chosen change's row, else Opacity's).
    ui.heading("Timeline"_tr, room.x, y);
    y += layout::headingH * k;
    if (!timeline.ok) // (a line it can't show: the timeline only shows it, until that's fixed as text)
        y += ui.text("The timeline can't change it while a line under it has a mistake."_tr, room.x, y, layout::noteText, kBad, room.w).y + gap;
    const float drawn = drawTimeline(ui, def, timeline, room.x, y, room.w);
    if (drawn < 0.0f) return; // (written: drawn again from it next frame, as tall as it was)
    y += drawn + gap;
    const float after = std::max(0.0f, timeline.length - lastEnd(timeline.bars)); // (time after its last change: kept as changes move)
    if (ui.button({room.x, y, room.w, buttonH}, "+ New change"_tr, false, timeline.ok, layout::buttonText)) {
        const std::size_t row = v.bar ? StoryEditor::timelineRow(timeline.bars[*v.bar].key) : 2;
        StoryEditor::Bar bar{.key = std::string(wordsOf(kTimelineRows[row].keys).front()), .value = std::string(kTimelineRows[row].start),
                             .start = std::round(std::min(v.playhead, timeline.length) * 20.0f) / 20.0f, .seconds = 0.5f, .ease = std::string(rowEase(row))};
        StoryEditor::Timeline added = timeline;
        added.bars.push_back(bar);
        added.length = lastEnd(added.bars) + after;
        writeTimeline(d, def, added, bar, {});
        return;
    }
    y += buttonH + rowGap;

    // The change chosen on it, as a group of the form (what it changes is its heading).
    if (!v.bar || !timeline.ok) {
        ui.formDrawn(formBox, y);
        return;
    }
    const StoryEditor::Bar& b = timeline.bars[*v.bar];
    const std::size_t row = StoryEditor::timelineRow(b.key);
    std::vector<Field> change{
        {.name = "To"_text, .value = barShown(b), .box = kTimelineRows[row].box, .suggest = std::string(kTimelineRows[row].more), .group = std::string(kTimelineRows[row].label)},
        {.name = "Starts at"_text, .value = secondsText(b.start), .label = "Starts at (seconds)"_text, .box = StoryEditor::Box::Seconds, .hint = "0"},
        {.name = "Duration"_text, .value = b.seconds > 0.0f ? secondsText(b.seconds) : std::string{}, .label = "Duration (seconds)"_text, .box = StoryEditor::Box::Seconds,
         .hint = "At once"_hint},
        {.name = "Easing"_text, .value = b.ease, .box = StoryEditor::Box::Easing, .usual = std::string(usualEase(b.key))}};
    if (b.key == "wipe") // (where it shows first: the image's darkest parts)
        change.insert(change.begin() + 1, {.name = "Shape"_text, .value = b.shape, .box = StoryEditor::Box::Shape, .hint = "None"_hint});
    const std::string changeId = std::format("{}:change", id);
    const float changeLabelW = labelColumn(ui, least, room.w, {change});
    const auto edit = drawForm(ui, changeId, change, room.x, room.w, changeLabelW, y);
    // Its easing as a dot going across (the one pointed at, else its own), under its row.
    const auto& pointed = d.choicePointed;
    const std::string easeShown = pointed.id == changeId + ":Easing" && d.textFrame - pointed.frame <= 1 ? pointed.word : b.ease.empty() ? std::string(usualEase(b.key)) : lower(b.ease);
    if (const auto ease = std::ranges::find(kEaseNames, std::string_view(easeShown), &std::pair<std::string_view, Ease>::first); ease != std::end(kEaseNames)) {
        if (d.pickShown != "ease:" + easeShown) d.pickShown = "ease:" + easeShown, d.pickShownAt = engine.toolsTime();
        drawEasingSample(ui, ease->second, d.pickShownAt, {room.x + changeLabelW, y, room.w - changeLabelW, std::round(48.0f * k)});
        y += std::round(48.0f * k) + rowGap;
    }
    if (edit) {
        StoryEditor::Bar changed = b;
        if (edit->name == "To") changed = barWritten(changed, row, edit->value);
        else if (edit->name == "Shape") changed.shape = std::string(trim(edit->value));
        else if (edit->name == "Starts at") changed.start = std::max(0.0f, parseNumber(edit->value).value_or(0.0f));
        else if (edit->name == "Duration") changed.seconds = std::max(0.0f, parseNumber(edit->value).value_or(0.0f));
        else if (edit->name == "Easing") changed.ease = edit->value;
        StoryEditor::Timeline t = timeline;
        t.bars[*v.bar] = changed;
        t.length = lastEnd(t.bars) + after;
        writeTimeline(d, def, t, changed, edit->typed ? std::format("{}:{}{}", changeId, edit->name, d.focusCount) : std::string{});
        if (!edit->typed) d.focus.clear();
        return;
    }
    ui.formDrawn(formBox, y);
}

float Developer::drawTimeline(DeveloperUi& ui, const StoryEditor::Definition& def, const StoryEditor::Timeline& timeline, float x, float y, float w) {
    DeveloperTools& d = tools;
    SDL_Renderer* r = ui.r;
    using Played = DeveloperTools::AnimationView;
    Played& v = d.animation;
    const float k = ui.k;
    const float labelW = layout::timelineLabelW * k, rulerH = layout::timelineRulerH * k, rowH = layout::timelineRowH * k;
    const SDL_FRect grid{x, y, w, rulerH + rowH * static_cast<float>(StoryEditor::kTimelineRowCount)};
    const float trackX = x + labelW, trackW = std::max(1.0f, w - labelW);
    // How many seconds it shows across: all of it and a little more, in a round number (kept while something's dragged).
    if (v.drag == Played::Drag::None || v.span <= 0.0f) v.span = roundSpan(std::max(1.0f, std::max(timeline.length, lastEnd(timeline.bars)) * 1.15f));
    const float span = v.span, perSecond = trackW / span;
    auto xOf = [&](float t) { return trackX + t * perSecond; };
    const float pointer = std::max(0.0f, (d.mouseX - trackX) / perSecond);
    // Where a dragged moment goes: onto one near it (the start, the end, the playhead, another change's
    // ends), else to a twentieth of a second.
    auto snap = [&](float t, std::optional<std::size_t> skip) {
        float best = std::round(t * 20.0f) / 20.0f, within = layout::timelineSnap * k / perSecond;
        auto near = [&](float m) {
            if (std::abs(m - t) < within) best = m, within = std::abs(m - t);
        };
        near(0.0f);
        near(v.playhead);
        near(timeline.length);
        for (std::size_t i = 0; i < timeline.bars.size(); ++i)
            if (i != skip) near(timeline.bars[i].start), near(timeline.bars[i].start + timeline.bars[i].seconds);
        return std::max(0.0f, best);
    };
    // The changes, and the end, as they are dragged.
    std::vector<StoryEditor::Bar> bars = timeline.bars;
    float length = timeline.length;
    const bool barDrag = v.drag == Played::Drag::Bar || v.drag == Played::Drag::Start || v.drag == Played::Drag::End;
    if (barDrag && v.bar && *v.bar < bars.size()) {
        StoryEditor::Bar& b = bars[*v.bar];
        const float moved = pointer - v.grab, end = v.was.start + v.was.seconds;
        if (v.drag == Played::Drag::Bar) b.start = snap(v.was.start + moved, v.bar);
        else if (v.drag == Played::Drag::End) b.seconds = std::max(0.0f, snap(end + moved, v.bar) - b.start);
        else b.start = std::min(end, snap(v.was.start + moved, v.bar)), b.seconds = end - b.start;
        length = lastEnd(bars) + std::max(0.0f, timeline.length - lastEnd(timeline.bars));
    }
    if (v.drag == Played::Drag::Length) length = std::max(lastEnd(bars), snap(v.wasLength + pointer - v.grab, std::nullopt));
    if (v.drag == Played::Drag::Playhead) v.playhead = std::clamp(pointer, 0.0f, span);

    // The rows, the seconds over them, and what's past the end of one time through (darker).
    draw::rect(r, grid, kBox);
    for (std::size_t row = 0; row < StoryEditor::kTimelineRowCount; ++row) {
        const float top = y + rulerH + rowH * static_cast<float>(row);
        if (row % 2 == 1) draw::rect(r, {trackX, top, trackW, rowH}, {255, 255, 255, 8});
        const std::string label(tr(kTimelineRows[row].label));
        ui.textFitted(label, x + 8 * k, top + rowH / 2, layout::smallText, theme::dim, labelW - 14 * k);
    }
    if (xOf(length) < trackX + trackW) draw::rect(r, {xOf(length), y + rulerH, trackX + trackW - xOf(length), grid.h - rulerH}, {0, 0, 0, 110});
    float step = 30.0f;
    for (const float s : {0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f})
        if (s * perSecond >= layout::timelineTickW * k) {
            step = s;
            break;
        }
    for (int i = 0; static_cast<float>(i) * step <= span + 0.0001f; ++i) {
        const float tx = xOf(static_cast<float>(i) * step);
        draw::rect(r, {tx, y + rulerH - 6 * k, 1.0f, 6 * k}, kEdge);
        draw::rect(r, {tx, y + rulerH, 1.0f, grid.h - rulerH}, {255, 255, 255, 10});
        ui.textAround(secondsText(static_cast<float>(i) * step), tx + 6 * k, y + rulerH / 2 + 1 * k, layout::sampleText, theme::dim);
    }
    draw::rect(r, {trackX, y + rulerH, trackW, 1.0f}, kEdge);
    draw::rect(r, {trackX - 1.0f, y, 1.0f, grid.h}, kEdge);

    // The changes: a bar from when one begins to when it ends, saying what it goes to; a diamond (over
    // the bars) for one at once, saying it too where there's room before the next on its row.
    const float edge = layout::timelineEdge * k, diamond = layout::timelineDiamond * k;
    std::vector<SDL_FRect> boxes(bars.size());
    auto rowTop = [&](const StoryEditor::Bar& b) {
        return y + rulerH + rowH * static_cast<float>(std::min(StoryEditor::timelineRow(b.key), StoryEditor::kTimelineRowCount - 1));
    };
    auto atOnce = [&](const StoryEditor::Bar& b) { return b.seconds <= 0.0f; };
    auto say = [&](const StoryEditor::Bar& b, float at, float mid, float room, SDL_Color textColour) { // (a colour: its square and its name, as a colour box shows it)
        const bool colour = (b.key == "colour" || b.key == "fade") && lower(b.value) != "none";
        if (const auto c = colour ? parseColour(b.value, &engine.palette()) : std::nullopt; c && room > layout::timelineSwatch * k + 16 * k) {
            const float after = ui.swatch(at, mid, *c, false, std::nullopt, layout::timelineSwatch);
            room -= after - at;
            at = after;
        }
        if (room > 16 * k) ui.textFitted(colour ? colourWords(b.value, &engine.palette()) : barShown(b), at, mid, layout::sampleText, textColour, room);
    };
    for (const bool instant : {false, true})
        for (std::size_t i = 0; i < bars.size(); ++i) {
            const StoryEditor::Bar& b = bars[i];
            if (atOnce(b) != instant) continue;
            const float top = rowTop(b), mid = top + rowH / 2;
            const bool chosen = v.bar == i;
            const SDL_Color fill = withAlpha(kAccent, chosen ? 0.9f : 0.45f);
            std::optional<float> next; // (where the next change on its row begins, after this one)
            for (const StoryEditor::Bar& o : bars)
                if (&o != &b && rowTop(o) == top && o.start > b.start + 0.001f) next = std::min(next.value_or(o.start), o.start);
            const float room = (next ? xOf(*next) : trackX + trackW) - 6 * k;
            if (!instant) {
                boxes[i] = {xOf(b.start), top + 4 * k, std::max(2 * k, b.seconds * perSecond), rowH - 8 * k};
                draw::rect(r, boxes[i], fill);
                draw::outline(r, boxes[i], chosen ? theme::text : kEdge);
                const bool marked = std::ranges::any_of(bars, [&](const StoryEditor::Bar& o) { return atOnce(o) && rowTop(o) == top && sameMoment(o.start, b.start); });
                const float textX = boxes[i].x + (marked ? diamond / 2 + 4 * k : 4 * k);
                say(b, textX, mid, boxes[i].x + boxes[i].w - 4 * k - textX, chosen ? kBox : theme::text);
            } else {
                const float cx = xOf(b.start), h = diamond / 2;
                boxes[i] = {cx - h, mid - h, diamond, diamond};
                draw::quad(r, {cx, mid - h - 1.0f}, {cx + h + 1.0f, mid}, {cx, mid + h + 1.0f}, {cx - h - 1.0f, mid}, kBox); // (an edge around it, over a bar)
                draw::quad(r, {cx, mid - h}, {cx + h, mid}, {cx, mid + h}, {cx - h, mid}, chosen ? theme::text : withAlpha(kAccent, 0.8f));
                const bool covered = std::ranges::any_of(bars, [&](const StoryEditor::Bar& o) {
                    return !atOnce(o) && rowTop(o) == top && o.start <= b.start + 0.001f && o.start + o.seconds > b.start + 0.001f;
                });
                if (!covered) say(b, cx + h + 4 * k, mid, room - (cx + h + 4 * k), theme::dim);
            }
        }
    // The chosen change: an x after it (before it, at the end of the row), as a list's chosen item has;
    // clicked, it says Delete, and clicked again the change goes.
    const std::string armId = v.bar ? std::format("timeline:{}:{}", def.line, *v.bar) : std::string{};
    std::optional<SDL_FRect> cross;
    if (v.bar && *v.bar < bars.size() && timeline.ok && v.drag == Played::Drag::None) {
        const bool armed = d.armed == armId;
        const SDL_FRect& at = boxes[*v.bar];
        const float xw = (armed ? layout::crossArmedW : layout::crossW) * k, h = rowH - 6 * k, mid = at.y + at.h / 2;
        cross = SDL_FRect{at.x + at.w + 3 * k, mid - h / 2, xw, h};
        if (cross->x + xw > trackX + trackW) cross->x = at.x - 3 * k - xw;
        draw::rect(r, *cross, armed ? SDL_Color{150, 50, 64, 255} : ui.over(*cross) ? kButtonHover : kButton);
        draw::outline(r, *cross, armed ? kBad : kEdge);
        ui.textIn(armed ? std::string_view("Delete"_tr) : std::string_view("x"), *cross, layout::smallButtonText, theme::text);
    }

    // The end of one time through (its handle on the seconds, to drag), and the playhead.
    const float endX = xOf(length), playX = xOf(std::clamp(v.playhead, 0.0f, span));
    draw::rect(r, {endX - 1.0f, y, 2.0f, grid.h}, withAlpha(kWarn, 0.7f));
    const SDL_FRect endHandle{endX - edge, y, 2 * edge, rulerH};
    draw::quad(r, {endX - 5 * k, y}, {endX + 5 * k, y}, {endX + 1.0f, y + 8 * k}, {endX - 1.0f, y + 8 * k}, kWarn);
    draw::rect(r, {playX - 1.0f, y, 2.0f, grid.h}, theme::text);
    draw::quad(r, {playX - 5 * k, y}, {playX + 5 * k, y}, {playX + 1.0f, y + 7 * k}, {playX - 1.0f, y + 7 * k}, theme::text);
    draw::outline(r, grid, kEdge);

    // What the pointer takes: the end's handle, the seconds (the playhead), a change (or one of its ends),
    // or a row: once to put the playhead there, twice to add a change there.
    if (v.drag == Played::Drag::None && timeline.ok) {
        if (cross && ui.over(*cross) && ui.click(*cross)) {
            if (d.armed != armId) {
                d.armed = armId;
                return grid.h;
            }
            d.armed.clear();
            StoryEditor::Timeline t = timeline;
            t.bars.erase(t.bars.begin() + static_cast<std::ptrdiff_t>(*v.bar));
            t.length = lastEnd(t.bars) + std::max(0.0f, timeline.length - lastEnd(timeline.bars));
            writeTimeline(d, def, t, std::nullopt, {});
            return -1.0f;
        }
        if (d.clicked && ui.over(grid) && d.armed.starts_with("timeline:")) d.armed.clear(); // (a click anywhere else on it: the x again)
        if (ui.over(endHandle) && ui.click(endHandle)) {
            v.drag = Played::Drag::Length;
            v.grab = pointer;
            v.wasLength = length;
        } else if (const SDL_FRect ruler{trackX, y, trackW, rulerH}; ui.click(ruler)) {
            v.drag = Played::Drag::Playhead;
            v.playing = false;
            v.playhead = std::clamp(pointer, 0.0f, span);
        } else {
            for (std::size_t i = bars.size(); i-- > 0;) {
                const SDL_FRect hit{boxes[i].x - 2 * k, boxes[i].y - 3 * k, boxes[i].w + 4 * k, boxes[i].h + 6 * k};
                if (!ui.over(hit) || !ui.click(hit)) continue;
                const bool timed = bars[i].seconds > 0.0f && boxes[i].w > 3 * edge;
                v.drag = timed && d.mouseX > boxes[i].x + boxes[i].w - edge ? Played::Drag::End : timed && d.mouseX < boxes[i].x + edge ? Played::Drag::Start : Played::Drag::Bar;
                v.bar = i;
                v.was = bars[i];
                v.grab = pointer;
                d.focus.clear();
                break;
            }
            for (std::size_t row = 0; v.drag == Played::Drag::None && row < StoryEditor::kTimelineRowCount; ++row) {
                const SDL_FRect lane{trackX, y + rulerH + rowH * static_cast<float>(row), trackW, rowH};
                if (!ui.click(lane)) continue;
                if (d.doubleClick) { // (a change there, going to what a new one on the row does, for half a second)
                    StoryEditor::Bar bar{.key = std::string(wordsOf(kTimelineRows[row].keys).front()), .value = std::string(kTimelineRows[row].start),
                                         .start = snap(pointer, std::nullopt), .seconds = 0.5f, .ease = std::string(rowEase(row))};
                    StoryEditor::Timeline t = timeline;
                    t.bars.push_back(bar);
                    t.length = lastEnd(t.bars) + std::max(0.0f, timeline.length - lastEnd(timeline.bars));
                    writeTimeline(d, def, t, bar, {});
                    return -1.0f;
                }
                v.bar.reset();
                v.drag = Played::Drag::Playhead;
                v.playing = false;
                v.playhead = std::clamp(pointer, 0.0f, span);
            }
        }
    }
    // Let go: a change written where it was taken to, or the end.
    if (v.drag != Played::Drag::None && !d.held) {
        const Played::Drag was = std::exchange(v.drag, Played::Drag::None);
        StoryEditor::Timeline t = timeline;
        t.bars = bars;
        t.length = length;
        const bool moved = barDrag && v.bar && *v.bar < bars.size() && (!sameMoment(bars[*v.bar].start, v.was.start) || !sameMoment(bars[*v.bar].seconds, v.was.seconds));
        if (moved || (was == Played::Drag::Length && !sameMoment(length, timeline.length))) {
            writeTimeline(d, def, t, v.bar ? std::optional(bars[*v.bar]) : std::nullopt, {});
            return -1.0f;
        }
    }
    // The keys: Delete takes the chosen change away, Esc lets it go.
    if (v.bar && *v.bar < bars.size() && d.focus.empty() && timeline.ok) {
        d.acts.remove = true;
        bool removed = false;
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            if (d.isKey(key, ToolKey::Back)) {
                v.bar.reset();
                return true;
            }
            if (d.isKey(key, ToolKey::Delete) && !removed && v.bar) {
                removed = true;
                return true;
            }
            return false;
        });
        if (removed) {
            StoryEditor::Timeline t = timeline;
            t.bars.erase(t.bars.begin() + static_cast<std::ptrdiff_t>(*v.bar));
            t.length = lastEnd(t.bars) + std::max(0.0f, timeline.length - lastEnd(timeline.bars));
            writeTimeline(d, def, t, std::nullopt, {});
            return -1.0f;
        }
    }
    return grid.h;
}

// How an animation looks `seconds` into it: a square (a fifth of the game's screen high) in the middle
// of a small screen of the game's size, as a part there would. Gives where it's drawn.
SDL_FRect Developer::drawAnimationSample(DeveloperUi& ui, const Animation& a, float seconds, const SDL_FRect& room) {
    SDL_Renderer* r = ui.r;
    const float side = std::round(engine.canvas().h / 5); // (in the game's pixels)
    const SDL_FRect part{(engine.canvas().w - side) / 2, (engine.canvas().h - side) / 2, side, side};
    const float scale = std::min(room.w / engine.canvas().w, room.h / engine.canvas().h);
    const SDL_FRect box{room.x, room.y, std::round(engine.canvas().w * scale), std::round(engine.canvas().h * scale)};
    draw::rect(r, box, kBox);
    const Animation::Look look = a.at(seconds, kAccent, [&](bool across, float place) {
        return across ? placeEdge(place, side, engine.canvas().w) - part.x : placeEdge(-place, side, engine.canvas().h) - part.y;
    }, {engine.canvas().w, engine.canvas().h});
    std::optional<DeveloperUi::Clip> clipped(std::in_place, ui, box);
    const float size = side * scale * look.zoom, cx = box.x + (part.x + side / 2 + look.x) * scale, cy = box.y + (part.y + side / 2 + look.y) * scale;
    const float turn = look.rotation * std::numbers::pi_v<float> / 180.0f, c = std::cos(turn), sn = std::sin(turn);
    auto corner = [&](float x, float y) { return SDL_FPoint{cx + (x * c - y * sn) * size / 2, cy + (x * sn + y * c) * size / 2}; };
    SDL_Color colour = look.colour.value_or(kAccent);
    colour.a = static_cast<Uint8>(std::lround(colour.a * std::clamp(look.opacity, 0.0f, 1.0f)));
    int viewW = 0, viewH = 0; // (what the tools' places are in)
    SDL_RendererLogicalPresentation mode{};
    SDL_GetRenderLogicalPresentation(r, &viewW, &viewH, &mode);
    tools.sampleWiper.draw(r, engine.assets(), {static_cast<float>(viewW), static_cast<float>(viewH)}, {cx - size / 2, cy - size / 2, size, size}, look.wipe,
                           look.wipeShape, [&] { draw::quad(r, corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1), colour); });
    if (look.fade > 0.0f) // (fade=: the whole screen covered, as much as it's seen, as a screen's is)
        draw::rect(r, box, withAlpha(look.fadeColour, std::clamp(look.fade, 0.0f, 1.0f) * std::clamp(look.opacity, 0.0f, 1.0f)));
    clipped.reset(); // (its outline: over the edge)
    draw::outline(r, box, kEdge);
    return box;
}

// An easing's sample: a dot going across a track to its end as that easing has it (a bounce landing, a spring past its end
// and back: the room after it), then a moment at its end, and again (since `since`).
void Developer::drawEasingSample(DeveloperUi& ui, Ease ease, float since, const SDL_FRect& box) {
    SDL_Renderer* r = ui.r;
    const float k = ui.k, pad = std::round(box.h * 0.5f), dot = std::round(box.h * 0.18f);
    const float t = std::fmod(std::max(0.0f, engine.toolsTime() - since), 1.6f); // (a second across, then a moment)
    const float across = (box.w - 2 * pad) / 1.4f, mid = std::round(box.y + box.h / 2); // (room past its end: a spring's first swing goes 1.37 of the way)
    draw::rect(r, box, kBox);
    draw::rect(r, {box.x + pad, mid - std::round(1 * k), across, std::round(2 * k)}, kEdge); // (the track: from its start to its end)
    draw::rect(r, {box.x + pad + across - std::round(1 * k), mid - dot, std::round(2 * k), 2 * dot}, kEdge);
    draw::circle(r, box.x + pad + eased(ease, std::min(t, 1.0f)) * across, mid, dot, kAccent);
    draw::outline(r, box, kEdge);
}

// + New animation: the animations to start from, as + New screen's screens are listed, with the one pointed
// at (else the last one pointed at) playing over them. A click adds it, named as it is (fade_in_2 when there's
// one already), and opens it.
void Developer::drawNewAnimationForm(DeveloperUi& ui, const SDL_FRect& right) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k, buttonH = layout::buttonH * k, gap = layout::gap * k;
    ui.formTitle("New animation"_tr, right.x, right.y);
    float y = right.y + layout::titleH * k;
    const AnimationStart& shown = kAnimationStarts[std::min(d.startShown, kAnimationStarts.size() - 1)];
    const StoryEditor::Timeline timeline = StoryEditor::timelineOf(startLines(shown));
    const float now = engine.toolsTime(), round = timeline.length + (shown.loops ? 0.0f : 0.6f); // (a moment after its end, then again)
    const float at = round > 0.0f ? std::fmod(std::max(0.0f, now - d.startShownAt), round) : 0.0f;
    const float sampleH = std::round(std::min(layout::sampleAnimationH * k, right.h * layout::sampleAnimationPart));
    const SDL_FRect sample = drawAnimationSample(ui, animationOf(timeline, 1, engine.palette()), std::min(at, timeline.length), {right.x, y, right.w, sampleH});
    y = sample.y + sample.h + layout::rowGap * k;
    const SDL_FRect list{right.x, y, right.w, right.y + right.h - y};
    ui.scrollForm(list, buttonH + gap);
    y = list.y - d.formScroll;
    const DeveloperUi::Clip clipped(ui, list);
    auto has = [&](const std::string& n) { return editor.defines("animation", n); };
    for (std::size_t i = 0; i < kAnimationStarts.size(); ++i) {
        const AnimationStart& start = kAnimationStarts[i];
        const SDL_FRect button{right.x, y, right.w, buttonH};
        const std::string stop = d.stopKey(d.keyCount); // (its button's: pointed at, by the pointer, the keys or a controller, it plays)
        if (ui.button(button, tr(start.label), false, true, layout::buttonText)) {
            const std::string name = uniqueName(start.name.empty() ? std::string("new_animation") : std::string(start.name), has);
            std::vector<std::string> lines{std::format("animation {}{}", name, start.loops ? " loop=yes" : "")};
            std::ranges::copy(StoryEditor::stepsOf(StoryEditor::timelineOf(startLines(start))), std::back_inserter(lines)); // (as the timeline writes them)
            d.libraryItem = editor.addDefinition("animation", lines);
            d.pickingAnimation = false;
            d.formScroll = 0.0f;
            return;
        }
        if (d.keyAt == stop && d.startShown != i) d.startShown = i, d.startShownAt = now;
        y += buttonH + gap;
    }
    ui.formDrawn(list, y);
}

} // namespace vn
