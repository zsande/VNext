// Reading what the Library lists: characters, colours, places, fonts, variables, styles, atlases, animations,
// sub-images, controls, the gallery, endings, songs, achievements, tips, captions, and the game's settings.
#include "ScriptParser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

namespace parse {

// -- Definitions ----------------------------------------------------------------
void Parser::definitions() {
    // Colours first so everything else can use them, then the rest in a fixed order; within a
    // group, lines take effect in the order they were written (so the last one wins).
    static constexpr std::array<std::array<std::string_view, 2>, 13> order{{
        {"colour", ""}, {"setting", ""}, {"place", ""}, {"font", ""}, {"atlas", "sub-image"}, {"character", ""}, {"style", ""},
        {"control", ""}, {"gallery", "ending"}, {"song", "achievement"}, {"tip", "animation"}, {"screen", "caption"}, {"variable", ""},
    }};
    for (const auto& group : order)
        for (const Line* line : defs_)
            if (const std::string kind = headOf(line->text); kind == group[0] || kind == group[1]) {
                const bool lenient = startsWithCapital(line->text) && line->children.empty();
                if (!attempt(lenient, [&] { define(*line, kind); })) asText_.insert(line);
            }
    startTime();
    for (const auto& [key, line] : animationSettings_) // (setting animation_on_show and animation_on_hide: animations the scripts have)
        if (const std::string& name = key == "animation_on_show" ? s_.gameSettings.animationOnShow : s_.gameSettings.animationOnHide; !name.empty() && !s_.animations.contains(name))
            error(*line, "There's no animation called '{}'."_tr(name));
}

// setting time_start: a date (2025-07-14) or a weekday, then a time of day or a part of the day; none said: the first
// part of the day for days and parts, else 8:00. Real time doesn't read it.
void Parser::startTime() {
    GameSettings& g = s_.gameSettings;
    g.timeStartMinutes = g.time == TimeKind::Days ? g.dayParts.front().second : 480;
    if (!timeStart_.second || g.time == TimeKind::Real) return; // (real time: the computer's clock and calendar, never a start)
    const Line& line = *timeStart_.second;
    const auto w = piecesOf(timeStart_.first);
    bool clock = false;
    for (std::size_t i = 0; i < w.size(); ++i) {
        const std::string word = lower(w[i]);
        int year = 0, month = 0, day = 0;
        const auto dashes = piecesOf(word, '-');
        const auto part = std::ranges::find(g.dayParts, word, &std::pair<std::string, int>::first);
        std::string at = word; // (6:30 pm)
        if (i + 1 < w.size() && (lower(w[i + 1]) == "am" || lower(w[i + 1]) == "pm")) at += lower(w[++i]);
        if (dashes.size() == 3 && std::from_chars(dashes[0].data(), dashes[0].data() + dashes[0].size(), year).ec == std::errc{} &&
            std::from_chars(dashes[1].data(), dashes[1].data() + dashes[1].size(), month).ec == std::errc{} &&
            std::from_chars(dashes[2].data(), dashes[2].data() + dashes[2].size(), day).ec == std::errc{} && month >= 1 && month <= 12 && day >= 1 &&
            dateFromDays(daysFromDate(year, month, day))[2] == day) { // (31 June isn't a day)
            g.timeStartDate = daysFromDate(year, month, day);
            g.timeStartWeekday = weekdayOf(g.timeStartDate);
        } else if (const auto weekday = weekdayNamed(word); weekday && g.timeStartDate < 0) {
            g.timeStartWeekday = *weekday;
        } else if (const auto minutes = clockMinutes(at); minutes && !clock) {
            g.timeStartMinutes = *minutes;
            clock = true;
        } else if (part != g.dayParts.end() && !clock) {
            g.timeStartMinutes = part->second;
            clock = true;
        } else {
            return error(line, "Starts at is a date (2025-07-14) or a weekday, then a time of day (8:00) or a part of the day: not '{}'."_tr(w[i]));
        }
    }
}

void Parser::define(const Line& line, std::string_view kind) {
    auto cursor = words(line, afterWords(line.text, 1));
    if (!cursor) return;
    Cursor& c = *cursor;
    if (kind != "screen" && kind != "atlas" && kind != "animation" && !line.children.empty())
        error(line, "Lines indented under this one don't belong to it."_tr());

    if (kind == "colour") {
        const auto name = c.next();
        const auto value = c.next();
        const auto col = value ? parseColour(*value, &s_.palette) : std::nullopt;
        if (!name || !col || !c.done()) return error(line, "A colour line needs a name and a colour."_tr());
        s_.palette.insert_or_assign(lower(*name), *col);
    } else if (kind == "variable") { // variable NAME [VALUE] ["what it's for"]
        const auto name = c.next();
        if (!name || !isVariable(*name)) return error(line, "A variable line needs the variable's name: one word (letters, numbers and _)."_tr());
        Value start = 0;
        if (!c.done()) {
            const ScriptWord& w = c.peek();
            if (w.quoted) start = w.text;
            else if (const auto e = Expr::parse(w.text)) start = e->eval({});
            else return error(line, "'{}' isn't a value a variable can start with: a number, yes or no, or \"text\"."_tr(w.text));
            c.skip(1);
        }
        if (!c.done() && c.peek().quoted) c.skip(1); // (what it's for)
        if (!c.done()) return error(line, "A variable line is its name, the value it starts with, and what it's for, in quotes."_tr());
        s_.startValues.insert_or_assign(lower(*name), std::move(start));
    } else if (kind == "place") { // place NAME VALUE
        const auto [name, value] = nameValue(c);
        const auto n = value ? stageCoordinate(*value, false) : std::nullopt;
        if (!name || !n || !c.done()) return error(line, "A place line needs a name and a number from -1 to 1."_tr());
        places_.insert_or_assign(lower(*name), *n);
    } else if (kind == "font") { // font [NAME] FILE [STYLE]
        const auto a = c.next(), b = c.next(), style = c.next();
        if (!a || !c.done())
            return error(line, "A font line needs a font file."_tr());
        if (b) s_.fonts.insert_or_assign(lower(*a), Script::FontFile{*b, style.value_or("")});
        else s_.fonts.insert_or_assign("default", Script::FontFile{*a, ""});
    } else if (kind == "setting") { // setting NAME VALUE...
        if (line.file > builtinFiles_) // (the built-in menus' come before main.vnscript)
            return error(line, "'setting' lines belong in main.vnscript, with the game's other settings."_tr());
        const auto [key, value] = nameValue(c);
        if (!key) return error(line, "A setting line needs a name and a value."_tr());
        std::vector<std::string> values;
        if (value) values.push_back(*value);
        while (auto v = c.next()) values.push_back(*v);
        gameSettings(line, lower(*key), values);
    } else if (kind == "atlas") {
        atlas(line, c);
    } else if (kind == "sub-image") {
        subImage(line, c);
    } else if (kind == "animation") {
        animation(line, c);
    } else if (kind == "character") {
        character(line, c);
    } else if (kind == "style") {
        const auto name = c.next();
        if (!name) return error(line, "A style line needs a name."_tr());
        styleLines_[lower(*name)].push_back(&line);
    } else if (kind == "control") {
        control(line, c);
    } else if (const auto record = std::ranges::find(kRecordKinds, kind); record != kRecordKinds.end()) {
        this->record(line, static_cast<RecordKind>(record - kRecordKinds.begin()), c);
    } else if (kind == "caption") {
        caption(line, c);
    } else if (kind == "screen") {
        const auto name = c.next();
        if (!name) return error(line, "A screen line needs a name."_tr());
        screenLines_.insert_or_assign(lower(*name), &line);
    }
}

// atlas NAME [image=FILE] [frames_in_a_row=N] [same_names_as=ATLAS], with the frames' names indented below it:
//     expressions neutral happy sad
//     eyes open half closed
//     mouth closed half open
void Parser::atlas(const Line& line, Cursor& c) {
    const auto name = c.next();
    if (!name || !isName(*name))
        return error(line, "An atlas line needs a name."_tr());
    Atlas a{.name = lower(*name), .image = *name};
    std::vector<std::pair<std::string, std::string>> options;
    while (!c.done()) {
        auto opt = c.option();
        if (!opt) return error(line, "'{}' doesn't belong in an atlas line."_tr(c.peek().text));
        if (opt->first == "same_names_as") { // the same frames as an atlas above (its settings come first)
            const auto other = std::ranges::find(s_.atlases, lower(opt->second), &Atlas::name);
            if (other == s_.atlases.end())
                return error(line, "There's no atlas called '{}' above this line."_tr(opt->second));
            a.columns = other->columns;
            a.expressions = other->expressions;
            a.eyes = other->eyes;
            a.mouth = other->mouth;
        } else {
            options.push_back(std::move(*opt));
        }
    }
    for (const auto& [key, value] : options) {
        if (key == "image") {
            a.image = imageName(value);
        } else if (key == "frames_in_a_row") {
            const auto n = number(line, value, key);
            if (!n) return;
            if (*n < 1.0f) return error(line, "'frames_in_a_row' needs a number from 1 up, not '{}'."_tr(*n));
            a.columns = static_cast<int>(*n);
        } else {
            return error(line, "Atlases have no setting '{}'."_tr(key));
        }
    }
    for (const Line& part : line.children) {
        std::string text = part.text; // "eyes: open, half, closed" works too
        std::ranges::replace(text, ',', ' ');
        std::ranges::replace(text, ':', ' ');
        auto w = words(part, text);
        if (!w) continue;
        const std::string what = lower(*w->next());
        std::vector<std::string>* list = what == "expressions" ? &a.expressions : what == "eyes" ? &a.eyes : what == "mouth" ? &a.mouth : nullptr;
        if (!list) {
            error(part, "'{}' isn't part of an atlas."_tr(what));
            continue;
        }
        // The names, then (for eyes and mouth) image= x= y= when they're a part drawn on top.
        std::vector<std::string> names;
        std::optional<Atlas::Part> layer;
        while (!w->done()) {
            if (auto opt = w->option()) {
                if (list == &a.expressions || (opt->first != "image" && opt->first != "x" && opt->first != "y")) {
                    error(part, "'{}' has no setting '{}'."_tr(what, opt->first));
                    break;
                }
                if (!layer) layer.emplace();
                if (opt->first == "image") layer->image = imageName(opt->second);
                else if (const auto n = number(part, opt->second, opt->first)) (opt->first == "x" ? layer->x : layer->y) = *n;
            } else {
                names.push_back(lower(*w->next()));
            }
        }
        if (!names.empty()) *list = std::move(names); // (same_names_as= may have given the names already)
        if (list->empty()) error(part, "'{}' needs a name for each frame."_tr(what));
        if (layer && layer->image.empty()) error(part, "Which image holds the {} is missing."_tr(what));
        if (list == &a.eyes) a.eyesPart = layer;
        if (list == &a.mouth) a.mouthPart = layer;
    }
    std::erase_if(s_.atlases, [&](const Atlas& other) { return other.name == a.name; }); // the last one wins
    s_.atlases.push_back(std::move(a));
}

// animation NAME [loop=yes|no|N], with what it changes on the lines under it:
//     x=-2 opacity=0
//     x=home over 1 second ease=out
//     opacity=100% from 0 to 0.5 seconds
//     up=5% at 2 seconds
//     wait 0.5 seconds
void Parser::animation(const Line& line, Cursor& c) {
    const auto name = c.next();
    if (!name || !isName(*name)) return error(line, "An animation line needs a name."_tr());
    Animation a{.name = lower(*name)};
    if (c.atOption("loop")) { // loop=yes (for as long as it's there), loop=3 (three times), loop=no (once)
        const std::string value = c.option()->second;
        if (const auto b = parseBool(value)) a.times = *b ? 0 : 1;
        else if (const auto n = parseNumber(value); n && *n >= 1.0f && *n == std::floor(*n)) a.times = static_cast<int>(*n);
        else return error(line, "'loop' can be yes, no or how many times it plays, not '{}'."_tr(value));
    }
    if (!c.done()) return error(line, "'{}' doesn't belong in an animation line (what it changes goes on the lines under it)."_tr(c.peek().text));
    float ended = 0.0f; // where the line before ended
    for (const Line& part : line.children) {
        auto w = words(part, part.text);
        if (!w) continue;
        if (w->take("wait")) {
            const auto n = w->number();
            if (!n || !w->done()) {
                error(part, "'wait' needs a number of seconds."_tr());
                continue;
            }
            ended += std::max(0.0f, *n);
            a.length = std::max(a.length, ended);
            continue;
        }
        std::vector<Animation::Change> changes;
        std::optional<float> start, seconds;
        std::optional<std::string> shape; // (shape=: the line's wipe's)
        Ease ease = Ease::InOut;
        bool easeSaid = false; // (a fade without one is linear, as a screen's fade is)
        bool ok = true;
        while (ok && !w->done()) {
            if (w->take("over") || w->take("for")) {
                const auto n = w->number();
                if (!n) error(part, "'over' needs a number of seconds."_tr()), ok = false;
                else seconds = std::max(0.0f, *n);
            } else if (w->take("from")) { // from 0.5 to 1 second
                const auto begins = w->number();
                const auto ends = begins && w->take("to") ? w->number() : std::nullopt;
                if (!begins || !ends) error(part, "'from' needs when it starts and when it ends."_tr()), ok = false;
                else if (*ends < *begins) error(part, "It ends before it starts: from {} to {} seconds."_tr(*begins, *ends)), ok = false;
                else start = std::max(0.0f, *begins), seconds = *ends - std::max(0.0f, *begins);
            } else if (w->take("at")) { // at 2 seconds: at once, then
                const auto n = w->number();
                if (!n) error(part, "'at' needs a number of seconds."_tr()), ok = false;
                else start = std::max(0.0f, *n), seconds = 0.0f;
            } else if (w->atOption("ease")) {
                const auto e = oneOf<Ease>(part, "ease", w->option()->second, kEases);
                if (!e) ok = false;
                else ease = *e, easeSaid = true;
            } else if (w->atOption("shape")) { // shape=IMAGE: where the wipe on this line shows it first
                shape = imageName(w->option()->second);
            } else if (const auto opt = w->option()) {
                const auto& [key, value] = *opt;
                using V = Animation::Value;
                Animation::Change change;
                if (key == "colour") {
                    change.what = V::Colour;
                    const auto col = colour(part, value);
                    if (!col) ok = false;
                    else change.colour = *col;
                } else if (key == "fade") { // fade=COLOUR: covered with it by the end; fade=none: uncovered again
                    change.what = V::Fade;
                    const auto col = lower(value) == "none" ? std::optional<SDL_Color>(SDL_Color{0, 0, 0, 0}) : colour(part, value);
                    if (!col) ok = false;
                    else change.colour = *col;
                } else if (key == "x" || key == "y") { // places, as the stage's (home: its own)
                    change.what = key == "x" ? V::X : V::Y;
                    if (lower(value) == "home") change.to = std::numeric_limits<float>::quiet_NaN();
                    else if (const auto n = number(part, value, key)) change.to = *n;
                    else ok = false;
                } else if (key == "left" || key == "right" || key == "up" || key == "down") { // by a share of the screen, from its own place
                    change.what = key == "left" || key == "right" ? V::X : V::Y;
                    change.by = true;
                    if (const auto n = number(part, value, key)) change.to = key == "left" || key == "down" ? -*n : *n;
                    else ok = false;
                } else if (key == "opacity" || key == "zoom" || key == "rotation") {
                    change.what = key == "opacity" ? V::Opacity : key == "zoom" ? V::Zoom : V::Rotation;
                    if (const auto n = number(part, value, key)) change.to = *n;
                    else ok = false;
                } else if (key == "wipe") { // how much of it shows (100%: all of it), from the left or as shape= has it
                    change.what = V::Wipe;
                    if (const auto n = number(part, value, key)) change.to = std::clamp(*n, 0.0f, 1.0f);
                    else ok = false;
                } else {
                    error(part, "An animation can change x, y, left, right, up, down, opacity, zoom, rotation, colour, fade and wipe, not '{}'."_tr(key)), ok = false;
                }
                if (ok) changes.push_back(change);
            } else {
                error(part, "'{}' doesn't belong in an animation's line."_tr(w->peek().text)), ok = false;
            }
        }
        if (ok && shape) { // (the wipe's shape)
            const auto wipe = std::ranges::find(changes, Animation::Value::Wipe, &Animation::Change::what);
            if (wipe == changes.end()) error(part, "'shape=' goes with a wipe on its line."_tr()), ok = false;
            else wipe->shape = *shape;
        }
        if (!ok) continue;
        if (changes.empty()) {
            error(part, "This line needs what it changes."_tr());
            continue;
        }
        for (Animation::Change& change : changes) {
            change.start = start.value_or(ended);
            change.seconds = seconds.value_or(0.0f);
            change.ease = change.what == Animation::Value::Fade && !easeSaid ? Ease::Linear : ease;
            a.changes.push_back(change);
        }
        ended = start.value_or(ended) + seconds.value_or(0.0f);
        a.length = std::max(a.length, ended);
    }
    std::ranges::stable_sort(a.changes, {}, &Animation::Change::start);
    if (a.changes.empty()) return error(line, "An animation needs what it changes, on the lines under it."_tr());
    s_.animations.insert_or_assign(a.name, std::move(a)); // the last one wins
}

// sub-image NAME cut_from=FILE x=N y=N width=N height=N: a part of an image, shown by its own name.
void Parser::subImage(const Line& line, Cursor& c) {
    const auto name = c.next();
    if (!name || !isName(*name)) return error(line, "A sub-image line needs a name."_tr());
    SubImage p{.name = lower(*name)};
    while (!c.done()) {
        const auto opt = c.option();
        if (!opt) return error(line, "'{}' doesn't belong in a sub-image line."_tr(c.peek().text));
        const auto& [key, value] = *opt;
        float* at = key == "x" ? &p.rect.x : key == "y" ? &p.rect.y : key == "width" ? &p.rect.w : key == "height" ? &p.rect.h : nullptr;
        if (key == "cut_from") p.image = imageName(value);
        else if (!at) return error(line, "A sub-image line has no setting '{}'."_tr(key));
        else if (const auto n = number(line, value, key)) *at = *n;
        else return;
    }
    if (p.image.empty() || p.rect.w <= 0.0f || p.rect.h <= 0.0f)
        return error(line, "A sub-image line needs the image it's cut from and the part of it."_tr());
    if (lower(p.image) == p.name) return error(line, "A sub-image can't be cut from itself: give it a name of its own."_tr());
    std::erase_if(s_.subImages, [&](const SubImage& other) { return other.name == p.name; }); // the last one wins
    s_.subImages.push_back(std::move(p));
}

// "NAME VALUE" (setting and place lines).
std::pair<std::optional<std::string>, std::optional<std::string>> Parser::nameValue(Cursor& c) {
    auto name = c.next();
    if (!name) return {};
    return {name, c.next()};
}

// A place on stage as written: -1 to 1 around the middle, y going up (-1 the left edge or the
// bottom). Returns the engine's own 0 to 1 from the top left.
std::optional<float> Parser::stageCoordinate(std::string_view text, bool vertical) const {
    const auto n = text.ends_with('%') ? std::nullopt : parseNumber(text);
    if (!n) return std::nullopt;
    return vertical ? (1.0f - *n) * 0.5f : (*n + 1.0f) * 0.5f;
}

// control NAME keys="space mouse_left" action="..." [in_menus=yes] [when=CONDITION]
void Parser::control(const Line& line, Cursor& c) {
    const auto name = c.next();
    if (!name || !isName(*name)) return error(line, "A control line needs a name."_tr());
    ControlLine control{.line = &line, .name = lower(*name)};
    while (!c.done()) {
        const auto opt = c.option();
        if (!opt) return error(line, "'{}' doesn't belong in a control line."_tr(c.peek().text));
        const auto& [key, value] = *opt;
        if (key == "keys") {
            std::string list(value);
            std::ranges::replace(list, ',', ' ');
            auto keyWords = splitWords(list);
            for (const auto& w : keyWords.value_or(std::vector<ScriptWord>{})) {
                const auto chord = parseKeyChord(w.text);
                if (!chord)
                    return error(line, "'{}' isn't a key name."_tr(w.text));
                control.keys.push_back(*chord);
            }
        } else if (key == "action") {
            control.action = value;
        } else if (key == "in_menus") {
            control.inMenus = boolean(line, value, key).value_or(false);
        } else if (key == "when") {
            control.condition = value;
        } else {
            return error(line, "Controls have no setting '{}'."_tr(key));
        }
    }
    if (control.action.empty()) return error(line, "The control's action is missing."_tr());
    std::erase_if(controlLines_, [&](const ControlLine& other) { return other.name == control.name; }); // the last one wins
    controlLines_.push_back(std::move(control));
}

// character NAME setting=value ...
void Parser::character(const Line& line, Cursor& c) {
    const auto name = c.next();
    if (!name) return error(line, "A character line needs a name."_tr());
    const GameSettings& game = s_.gameSettings;
    CharacterDef def{.key = lower(*name), .name = *name, .image = characterImageName(*name), .madeUpVoiceSpeed = game.madeUpVoiceSpeed,
                     .blink = game.blink, .secondsBetweenBlinks = game.secondsBetweenBlinks, .mouthMoves = game.mouthsMove, .mouthSpeed = game.mouthSpeed};
    std::optional<bool> faceBlink;
    std::optional<MouthMoves> faceMouth;
    while (!c.done()) {
        const auto opt = c.option();
        if (!opt) return error(line, "'{}' doesn't belong in a character line."_tr(c.peek().text));
        const auto& [key, value] = *opt;
        // Each sets its field when the value is right, and says whether it was.
        auto flag = [&](bool& out) { return set(out, boolean(line, value, key)); };
        auto within = [&](float& out, float lo, float hi) {
            const auto n = number(line, value, key);
            return set(out, n ? std::optional(std::clamp(*n, lo, hi)) : std::nullopt);
        };
        auto sync = [&](FaceCopies& out) -> bool { // yes, no, or what it copies: face_copies=expression,blink
            if (const auto b = parseBool(value)) return out = {*b, *b, *b}, true;
            out = {false, false, false};
            for (auto&& part : std::views::split(std::string_view(value), ',')) {
                const std::string_view written = trim(std::string_view(part.begin(), part.end()));
                const std::string word = lower(written);
                if (word == "expression") out.expression = true;
                else if (word == "blink") out.blink = true;
                else if (word == "mouth") out.mouth = true;
                else return error(line, "'face_copies' can be yes, no, or what the face copies (expression, blink, mouth), not '{}'."_tr(written)), false;
            }
            return true;
        };
        bool ok = true;
        if (key == "name_colour" || key == "text_colour") {
            const auto col = colour(line, value);
            ok = col.has_value();
            if (col && key == "name_colour") def.nameColour = *col;
            else if (col) def.textColour = *col;
        }
        else if (key == "image") def.image = imageName(value);
        else if (key == "text_appears") ok = (def.textAppears = oneOf<TextAppears>(line, key, value, kTextAppears)).has_value();
        else if (key == "textbox") def.textbox = lower(value);
        else if (key == "typing_sound") def.typingSound = value;
        else if (key == "voice_files") { // yes: sounds/voices/<name>/, or sounds/voices/<value>/
            const auto b = parseBool(value);
            def.voiceFiles = b == false ? "" : std::format("voices/{}", b ? characterImageName(*name) : imageName(value));
        }
        else if (key == "made_up_voice") { // high, medium, low, deep, no, or a pitch in Hz
            static constexpr std::pair<std::string_view, float> heights[]{{"high", 250.0f}, {"medium", 190.0f}, {"low", 135.0f}, {"deep", 105.0f}};
            const auto height = std::ranges::find(heights, lower(value), &std::pair<std::string_view, float>::first);
            if (height != std::end(heights)) def.madeUpVoice = height->second;
            else if (parseBool(value) == false) def.madeUpVoice = 0.0f;
            else if (const auto hz = parseNumber(value); hz && *hz >= 50.0f && *hz <= 600.0f) def.madeUpVoice = *hz;
            else return error(line, "'made_up_voice' can be high, medium, low, deep, no or a pitch, not '{}'."_tr(value));
        }
        else if (key == "made_up_voice_speed") ok = within(def.madeUpVoiceSpeed, 0.25f, 4.0f);
        else if (key == "blink") ok = flag(def.blink);
        else if (key == "seconds_between_blinks") ok = within(def.secondsBetweenBlinks, 0.5f, 60.0f);
        else if (key == "mouth_moves") ok = set(def.mouthMoves, mouthMoves(line, key, value));
        else if (key == "mouth_speed") ok = within(def.mouthSpeed, 0.5f, 20.0f);
        else if (key == "face") def.face = value; // an image name, or yes / no
        else if (key == "face_side") {
            if (!isName(value)) return error(line, "'face_side' is one word, not '{}'."_tr(value));
            def.faceSide = lower(value);
        }
        else if (key == "face_copies") ok = sync(def.faceCopies);
        else if (key == "avatar") def.avatar = imageName(value);
        else if (key == "about") {
            def.about = value;
            checkTextOf(line, value);
        }
        else if (key == "contact") def.contact = formula(line, value); // (in the phone's people while it's true)
        else if (key == "ringtone") def.ringtone = value;
        else if (key == "chat" || key == "call") { // the scene played when the player opens their chat, or calls them
            if (!isName(lower(value))) return error(line, "'{}' needs a scene's name, not '{}'."_tr(key, value));
            (key == "chat" ? def.chatScene : def.callScene) = lower(value);
            phoneSceneNames_.push_back({.file = line.file, .number = line.number, .scene = lower(value)});
        }
        else if (key == "face_flip") ok = flag(def.faceFlip);
        else if (key == "face_blink") ok = (faceBlink = boolean(line, value, key)).has_value();
        else if (key == "face_mouth") ok = (faceMouth = mouthMoves(line, key, value)).has_value();
        else return error(line, "Characters have no setting '{}'."_tr(key));
        if (!ok) return;
    }
    if (const auto yes = parseBool(def.face)) def.face = *yes ? def.image + "_face" : ""; // face=yes: images/<image>_face.png
    else def.face = imageName(def.face);
    def.faceBlink = faceBlink.value_or(def.blink); // the face does what the image on stage does, unless told
    def.faceMouth = faceMouth.value_or(def.mouthMoves);
    if (!def.textbox.empty()) textboxes_.emplace_back(&line, def.textbox);
    s_.characters.insert_or_assign(def.key, std::move(def));
}

// gallery IMAGE "Title" | ending NAME "Title" [hint="..."] | song NAME "Title" | achievement NAME "Title" "Description" [secret=yes]
// | tip NAME "Title" "Description"
void Parser::record(const Line& line, RecordKind kind, Cursor& c) {
    const auto name = c.next();
    if (!name || (kind != RecordKind::Gallery && !isName(*name))) return error(line, "'{}' needs a name."_tr(kRecordKinds[static_cast<std::size_t>(kind)]));
    if ((kind == RecordKind::Ending || kind == RecordKind::Achievement || kind == RecordKind::Tip) && name->contains('-')) // (what keeps it: persistent.ending.NAME)
        return error(line, "'{}' can't be its name (one word of letters, numbers and _)."_tr(*name));
    Script::Record r{.name = *name, .title = c.next().value_or(*name)};
    if (r.title.empty()) r.title = *name; // (a title left empty, "": the name)
    while (!c.done()) {
        if (kind == RecordKind::Achievement && c.atOption("secret")) {
            const auto b = boolean(line, c.option()->second, "secret");
            if (!b) return;
            r.secret = *b;
        }
        else if (kind == RecordKind::Ending && c.atOption("hint")) r.text = c.option()->second;
        else if ((kind == RecordKind::Achievement || kind == RecordKind::Tip) && r.text.empty() && (c.peek().quoted || !c.peek().text.contains('=')))
            r.text = *c.next();
        else return error(line, "'{}' doesn't belong in this {} line."_tr(c.peek().text, kRecordKinds[static_cast<std::size_t>(kind)]));
    }
    auto& list = s_.lists[static_cast<std::size_t>(kind)];
    std::erase_if(list, [&](const Script::Record& other) { return sameName(other.name, r.name); }); // the last one wins
    list.push_back(std::move(r));
}

// caption SOUND "Words": what the player's Captions show as that sound (or song) begins
void Parser::caption(const Line& line, Cursor& c) {
    const auto name = c.next();
    const auto words = c.next();
    if (!name || !words || words->empty()) return error(line, "A caption line needs a sound and its words: caption door \"A door slams\"."_tr());
    if (!c.done()) return error(line, "'{}' doesn't belong in this caption line."_tr(c.peek().text));
    s_.captions.insert_or_assign(lower(*name), *words); // (the last one wins)
}

// setting NAME VALUE (one value, or two for game_size and pointer_tip)
void Parser::gameSettings(const Line& line, const std::string& key, const std::vector<std::string>& v) {
    GameSettings& c = s_.gameSettings;
    struct Number {
        std::string_view key;
        float GameSettings::*field;
        float lo, hi;
    };
    static constexpr std::array numbers{
        Number{"skip_time", &GameSettings::skipTime, 0.0f, 10.0f},
        Number{"auto_letter_delay", &GameSettings::autoLetterDelay, 0.0f, 10.0f},
        Number{"character_height", &GameSettings::characterHeight, 0.05f, 10.0f},
        Number{"move_time", &GameSettings::moveTime, 0.0f, 60.0f},
        Number{"music_crossfade", &GameSettings::musicCrossfade, 0.0f, 60.0f},
        Number{"glide_speed", &GameSettings::glideSpeed, 0.0f, 10.0f},
        Number{"glide_distance", &GameSettings::glideDistance, 0.05f, 10.0f},
        Number{"scrollbar_thickness", &GameSettings::scrollbarThickness, 1.0f, 64.0f},
        Number{"fading_edges", &GameSettings::fadingEdges, 0.0f, 1000.0f},
        Number{"typing_sound_gap", &GameSettings::typingSoundGap, 0.01f, 10.0f},
        Number{"typing_sound_volume", &GameSettings::typingSoundVolume, 0.0f, 1.0f},
        Number{"made_up_voice_speed", &GameSettings::madeUpVoiceSpeed, 0.25f, 4.0f},
        Number{"seconds_between_blinks", &GameSettings::secondsBetweenBlinks, 0.5f, 60.0f},
        Number{"mouth_speed", &GameSettings::mouthSpeed, 0.5f, 20.0f},
        Number{"phone_typing", &GameSettings::phoneTyping, 0.0f, 100.0f},
        Number{"music_under_voices", &GameSettings::musicUnderVoices, 0.0f, 1.0f},
    };
    struct Whole {
        std::string_view key;
        int GameSettings::*field;
        int lo, hi;
    };
    static constexpr std::array wholes{
        Whole{"save_pages", &GameSettings::savePages, 1, 99}, Whole{"page_slots", &GameSettings::pageSlots, 1, 60},
        Whole{"history_length", &GameSettings::historyLength, 1, 100000}, Whole{"rollback_lines", &GameSettings::rollbackLines, 0, 10000},
        Whole{"load_ahead", &GameSettings::loadAhead, 0, 1000}, Whole{"image_memory", &GameSettings::imageMemory, 0, 65536},
    };
    static const std::map<std::string_view, bool GameSettings::*> flags{
        {"autosave", &GameSettings::autosave}, {"text_follows_voices", &GameSettings::textFollowsVoices}, {"developer", &GameSettings::developer}, {"glide", &GameSettings::glide},
        {"blink", &GameSettings::blink}, {"resizable", &GameSettings::resizable}, {"phone_history", &GameSettings::phoneHistory},
        {"phone_follows_time", &GameSettings::phoneFollowsTime}, {"backgrounds_follow_time", &GameSettings::backgroundsFollowTime}};
    static const std::map<std::string_view, SDL_Color GameSettings::*> colours{
        {"narration_colour", &GameSettings::narrationColour},
        {"letterbox_colour", &GameSettings::letterboxColour}};
    static const std::map<std::string_view, std::string GameSettings::*> texts{
        {"title", &GameSettings::title}, {"version", &GameSettings::version}, {"save_folder", &GameSettings::saveFolder}, {"icon", &GameSettings::icon},
        {"mouse_pointer", &GameSettings::mousePointer}, {"language", &GameSettings::language}, {"phone_message_sound", &GameSettings::phoneMessageSound},
        {"phone_send_sound", &GameSettings::phoneSendSound}, {"phone_typing_sound", &GameSettings::phoneTypingSound},
        {"phone_ringtone", &GameSettings::phoneRingtone}, {"phone_calling_sound", &GameSettings::phoneCallingSound}};
    static constexpr std::pair<std::string_view, bool> filters[]{{"smooth", false}, {"pixel", true}};
    static constexpr std::pair<std::string_view, SDL_Keymod> holds[]{
        {"ctrl", SDL_KMOD_CTRL}, {"shift", SDL_KMOD_SHIFT}, {"alt", SDL_KMOD_ALT}, {"none", 0}};

    const std::size_t needs = key == "game_size" && v.size() == 1 && v[0].contains('x') ? 1
                            : key == "game_size" || key == "pointer_tip"               ? 2
                                                                                           : 1;
    if (v.size() < needs) return error(line, needs == 1 ? "'setting {}' needs 1 value."_tr(key) : "'setting {}' needs {} values."_tr(key, needs));
    const std::string& value = v[0];
    if (const auto n = std::ranges::find(numbers, std::string_view(key), &Number::key); n != numbers.end()) {
        if (auto x = number(line, value, key)) c.*(n->field) = std::clamp(*x, n->lo, n->hi);
    } else if (const auto w = std::ranges::find(wholes, std::string_view(key), &Whole::key); w != wholes.end()) {
        if (auto x = number(line, value, key)) c.*(w->field) = std::clamp(static_cast<int>(*x), w->lo, w->hi);
    } else if (const auto f = flags.find(key); f != flags.end()) {
        set(c.*(f->second), boolean(line, value, key));
    } else if (const auto col = colours.find(key); col != colours.end()) {
        set(c.*(col->second), colour(line, value));
    } else if (const auto t = texts.find(key); t != texts.end()) {
        c.*(t->second) = value;
    } else if (key == "game_size") { // game_size 1280 720 (or 1280x720)
        const bool joined = v.size() == 1;
        const auto width = number(line, joined ? value.substr(0, value.find('x')) : value, key);
        const auto height = number(line, joined ? value.substr(value.find('x') + 1) : v[1], key);
        if (width && height) {
            c.width = std::clamp(static_cast<int>(*width), 160, 7680);
            c.height = std::clamp(static_cast<int>(*height), 120, 4320);
        }
    } else if (key == "pointer_tip") { // pointer_tip X Y
        const auto x = number(line, value, key), y = number(line, v[1], key);
        if (x && y) c.pointerTip = {static_cast<int>(*x), static_cast<int>(*y)};
    } else if (key == "image_scaling") {
        set(c.pixelArt, oneOf<bool>(line, key, value, filters));
    } else if (key == "hold_to_skip") {
        set(c.holdToSkip, oneOf<SDL_Keymod>(line, key, value, holds));
    } else if (key == "mouths_move") { // yes, voice or no: for every character without mouth_moves=
        set(c.mouthsMove, mouthMoves(line, key, value));
    } else if (key == "text_appears") {
        set(c.textAppears, oneOf<TextAppears>(line, key, value, kTextAppears));
    } else if (key == "has_phone") { // has_phone CONDITION: the player has their phone while it's true
        c.hasPhone = formula(line, value);
    } else if (key == "phone_owner" || key == "phone_wallpaper" || key == "phone_lock_wallpaper" || key == "phone_passcode" || key == "phone_clock" ||
               key == "phone_date") { // (the phone's variables, as they start)
        s_.startValues.insert_or_assign(key, unquote(value));
    } else if (key == "phone_signal") {
        if (const auto on = boolean(line, value, key)) s_.startValues.insert_or_assign(key, *on ? 1 : 0);
    } else if (key == "phone_battery") { // (how full: 0 to 100)
        if (const auto full = number(line, value, key)) s_.startValues.insert_or_assign(key, std::clamp(static_cast<int>(std::lround(*full)), 0, 100));
    } else if (key == "new_messages") { // button (the Phone button counts it) or open (the phone opens): what tells players a message has come
        set(c.newMessages, oneOf<NewMessages>(line, key, value, kNewMessages));
    } else if (key == "chat_messages") { // typed or click: how a chat scene's messages come
        set(c.chatMessages, oneOf<ChatMessages>(line, key, value, kChatMessages));
    } else if (key == "time") { // none, story, real or days: the story's time
        set(c.time, oneOf<TimeKind>(line, key, value, kTimeKinds));
    } else if (key == "time_start") { // a date or a weekday, then a time or a part of the day (read once the parts are)
        std::string all;
        for (const std::string& word : v) all += (all.empty() ? "" : " ") + word;
        timeStart_ = {all, &line};
    } else if (key == "time_parts") { // "morning 6:00, afternoon 12:00, ...": each part of the day and when it starts
        std::vector<std::pair<std::string, int>> parts;
        std::string all;
        for (const std::string& word : v) all += (all.empty() ? "" : " ") + word;
        for (const std::string_view part : piecesOf(all, ',')) {
            const auto said = piecesOf(part);
            const auto starts = said.size() >= 2 ? clockMinutes(part.substr(said.front().size())) : std::nullopt;
            if (!starts || !isName(said.front()) || std::ranges::contains(parts, lower(said.front()), &std::pair<std::string, int>::first))
                return error(line, "time_parts lists each part of the day with when it starts, commas between them: morning 6:00, evening 18:00."_tr());
            parts.emplace_back(lower(said.front()), *starts);
        }
        if (parts.empty()) return error(line, "time_parts lists each part of the day with when it starts, commas between them: morning 6:00, evening 18:00."_tr());
        std::ranges::sort(parts, {}, &std::pair<std::string, int>::second);
        c.dayParts = std::move(parts);
    } else if (key == "clock") { // 24 or 12: how times show (18:30, or 6:30 PM)
        if (value == "24" || value == "12") c.clock12 = value == "12";
        else error(line, "'clock' can be 24 or 12, not '{}'."_tr(value));
    } else if (key == "animation_on_show" || key == "animation_on_hide") { // what show and hide lines play when they name none (checked once the animations are read)
        (key == "animation_on_show" ? c.animationOnShow : c.animationOnHide) = lower(value) == "none" ? std::string{} : lower(value);
        std::erase_if(animationSettings_, [&](const auto& kept) { return kept.first == key; });
        animationSettings_.emplace_back(key, &line);
    } else if (key.starts_with("default.")) { // a player setting's starting value: a number, yes / no, or (scaling, mouths) its word
        const std::string setting = key.substr(8);
        if (!std::ranges::contains(kSettingNames, setting) || setting == "window_size" || setting == "language")
            return error(line, "There's no player setting called '{}'."_tr(setting));
        if (const auto start = settingNumber(line, setting, key, value)) c.settingDefaults[setting] = *start;
    } else {
        error(line, "There's no setting called '{}'."_tr(key));
    }
}

// -- Styles -----------------------------------------------------------------------

// Returns false when `key` is not a style property.
bool Parser::styleProp(Style& s, std::string_view key, std::string_view value, const Line& line) {
    auto at = [&](float lo) { // a number, at least `lo`
        const auto n = number(line, value, key);
        return n ? std::optional(std::max(lo, *n)) : std::nullopt;
    };
    auto state = [&](std::string_view prefix) -> Style::State* {
        return prefix == "pointed" ? &s.pointed : prefix == "chosen" ? &s.chosen : prefix == "unclickable" ? &s.unclickable : nullptr;
    };
    const auto us = key.find('_');
    Style::State* st = us == std::string_view::npos ? nullptr : state(key.substr(0, us));
    const std::string_view field = st ? key.substr(us + 1) : key;

    if (st && (field == "colour" || field == "background" || field == "border")) { // pointed_colour= chosen_background= unclickable_border=...
        const auto c = colour(line, value);
        if (c) (field == "colour" ? st->colour : field == "background" ? st->background : st->border) = c;
        if (c && field == "background") { // (navy-black: fading down to another)
            const auto grad = parseGradient(value, &s_.palette);
            st->backgroundBottom = grad ? std::optional(grad->second) : std::nullopt;
        }
    }
    else if (st && field == "image") st->image = std::string(value);
    else if (key == "font") s.font = lower(value);
    else if (key == "size") set(s.size, at(1.0f));
    else if (key == "colour") {
        s.speakerColour = value == "speaker";
        if (!s.speakerColour) set(s.colour, colour(line, value));
    }
    else if (key == "background") { // (navy-black: fading down to another)
        set(s.background, colour(line, value));
        const auto grad = parseGradient(value, &s_.palette);
        s.backgroundBottom = grad ? std::optional(grad->second) : std::nullopt;
    }
    else if (key == "border") set(s.border, colour(line, value));
    else if (key == "image") s.image = std::string(value);
    else if (key == "align" || key == "align_vertically") {
        if (auto a = toAlign(value, key == "align_vertically")) (key == "align" ? s.align : s.alignVertically) = *a;
        else error(line, "'{}' can be left, centre or right (top, centre or bottom for align_vertically), not '{}'."_tr(key, value));
    }
    else if (key == "padding") set(s.padding, at(0.0f));
    else if (key == "opacity") {
        if (const auto n = number(line, value, key)) s.opacity = std::clamp(*n, 0.0f, 1.0f);
    }
    else if (key == "shadow") set(s.shadow.on, boolean(line, value, key));
    else if (key == "shadow_colour") set(s.shadow.colour, colour(line, value));
    else if (key == "shadow_offset") set(s.shadow.offset, at(0.0f));
    else if (key == "line_spacing") set(s.lineSpacing, at(0.1f));
    else if (key == "opacity_follows") set(s.opacityFollows, boolean(line, value, key));
    else if (key == "text_size_follows") set(s.textSizeFollows, boolean(line, value, key));
    else if (key == "pointed_sound") s.pointedSound = std::string(value);
    else if (key == "click_sound") s.clickSound = std::string(value);
    else return false;
    return true;
}

std::optional<Style> Parser::style(const std::string& name, const Line& user, int depth) {
    if (auto it = styles_.find(name); it != styles_.end()) return it->second;
    const auto raw = styleLines_.find(name);
    if (raw == styleLines_.end()) return std::nullopt;
    if (depth > 16) {
        error(user, "Style '{}' builds on itself."_tr(name));
        return Style{};
    }
    std::vector<std::pair<const Line*, std::pair<std::string, std::string>>> props;
    for (const Line* line : raw->second) {
        auto c = words(*line, afterWords(line->text, 2));
        if (!c) continue;
        while (!c->done()) {
            if (auto opt = c->option()) {
                if (line->file < builtinFiles_) scaleBuiltIn(*opt);
                props.emplace_back(line, std::move(*opt));
            } else {
                error(*line, "'{}' doesn't belong in a style line."_tr(*c->next()));
            }
        }
    }
    std::string parent = name == "default" ? "" : "default";
    for (const auto& [line, kv] : props)
        if (kv.first == "based_on") parent = lower(kv.second);
    Style s;
    if (!parent.empty()) {
        if (auto base = style(parent, *raw->second.front(), depth + 1)) s = *base;
        else if (parent != "default") error(*raw->second.front(), "There's no style called '{}'."_tr(parent));
    }
    for (const auto& [line, kv] : props)
        if (kv.first != "based_on" && !styleProp(s, kv.first, kv.second, *line))
            error(*line, "Styles have no setting '{}'."_tr(kv.first));
    return styles_.emplace(name, s).first->second;
}

} // namespace parse

} // namespace vn
