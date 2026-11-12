// The editor's forms: each kind of box, each line's fields and the line made from them again, and the values,
// sums, conditions and actions in them.
#include "Forms.hpp"

#include <cctype>
#include <cmath>
#include <deque>
#include <iterator>
#include <ranges>
#include <tuple>

namespace vn {

namespace {

// A colour as a hint says it: its name, or what it's most like (never a code); none (see-through) as None.
std::string colourText(SDL_Color c, const Palette& palette) {
    return c.a == 0 ? "None" : colourName(c, &palette);
}

// A style's value for a setting, as a form shows it in grey where a box is empty (what the part or
// style gets from its style then): colours by their name, sizes as numbers, nothing as None; empty
// for a setting that isn't a style's.
std::string styleValueText(const Style& s, std::string_view key, const Palette& palette) {
    auto colour = [&](SDL_Color c) { return colourText(c, palette); };
    auto name = [](const std::string& n) { return n.empty() ? std::string("None") : n; };
    auto state = [&](const Style::State& st, std::string_view what) { // (a look's own value, else the usual one)
        return what == "colour"  ? colour(st.colour.value_or(s.colour))
             : what == "background" ? colour(st.background.value_or(s.background))
             : what == "border" ? colour(st.border.value_or(s.border))
                                : name(st.image.empty() ? s.image : st.image);
    };
    std::string out;
    if (key == "font") out = s.font;
    else if (key == "size") out = std::format("{:g}", s.size);
    else if (key == "colour") out = colour(s.colour);
    else if (key == "line_spacing") out = std::format("{:g}", s.lineSpacing);
    else if (key == "shadow_colour") out = colour(s.shadow.colour);
    else if (key == "shadow_offset") out = std::format("{:g}", s.shadow.offset);
    else if (key == "background") out = colour(s.background);
    else if (key == "border") out = colour(s.border);
    else if (key == "image") out = name(s.image);
    else if (key == "padding") out = std::format("{:g}", s.padding);
    else if (key == "opacity") out = std::format("{:g}%", s.opacity * 100.0f);
    else if (key == "pointed_sound") out = name(s.pointedSound);
    else if (key == "click_sound") out = name(s.clickSound);
    else if (key.starts_with("pointed_")) out = state(s.pointed, key.substr(8));
    else if (key.starts_with("chosen_")) out = state(s.chosen, key.substr(7));
    else if (key.starts_with("unclickable_")) out = state(s.unclickable, key.substr(12));
    else return {};
    return out;
}

} // namespace

namespace dev {

void showStyleValues(std::vector<StoryEditor::Field>& fields, const Style& style, const Palette& palette) {
    for (StoryEditor::Field& f : fields) {
        if (f.hint != "From its style" && f.hint != "From its base style") continue;
        std::string_view key = f.name;
        if (const auto slot = std::ranges::find(kStyleSlots, std::string_view(f.name), &Slot::name); slot != std::end(kStyleSlots)) key = slot->key;
        if (std::string value = styleValueText(style, key, palette); !value.empty()) f.hint = std::move(value); // (its own: the text's made for it)
    }
}

std::string secondsText(float seconds) {
    std::string s = std::format("{:.2f}", std::round(std::max(0.0f, seconds) * 100.0f) / 100.0f);
    while (s.ends_with('0')) s.pop_back();
    if (s.ends_with('.')) s.pop_back();
    return s;
}

void showGameSettingValues(std::vector<StoryEditor::Field>& fields, const GameSettings& gameSettings) {
    auto value = [&](std::string_view name) { return lower(fieldValue(fields, name)); };
    for (StoryEditor::Field& f : fields) {
        if (f.name == "Image" && f.hint.empty()) { // (a character's images, an atlas's: named after them)
            f.hint = lower(value("Name"));
            continue;
        }
        if (f.name == "Title" && f.hint.empty() && !value("Name").empty()) { // (an ending's, an achievement's, a tip's: its name)
            f.hint = value("Name");
            continue;
        }
        if (f.name == "Fade out" && f.hint == "None" && !value("Called").empty()) { // (a named sound's: the one before by its name stops at once)
            f.hint = "At once"_hint;
            continue;
        }
        if (f.hint != "From Settings") continue;
        std::string v;
        if (f.name == "Animation on show" || f.name == "Animation on hide") { // (the game's, from Settings: none, or its name)
            const std::string& named = f.name == "Animation on show" ? gameSettings.animationOnShow : gameSettings.animationOnHide;
            v = named.empty() ? std::string("At once"_hint) : named; // (as Settings says it)
        }
        else if (f.name == "Its speed") v = std::format("{:g}", gameSettings.madeUpVoiceSpeed);
        else if (f.name == "Fade out") v = std::format("{:g}", gameSettings.musicCrossfade); // (a music line's: the song before's)
        else if (f.name == "Blinks every") v = std::format("{:g}", gameSettings.secondsBetweenBlinks);
        else if (f.name == "Mouth's speed") v = std::format("{:g}", gameSettings.mouthSpeed);
        else continue;
        f.hint = std::move(v);
    }
}

} // namespace dev

namespace {


const Slot kJumpSlots[]{{.type = Type::Word, .name = "Scene"_text, .box = Box::Scene, .label = "Goes to scene"_text}};

// A length of time as a time line writes it: the number, then what it counts (minutes, else as said), one of it said as
// one (1 hour, 2 hours).
std::string lengthWords(std::string_view number, std::string_view unit) {
    std::string counts = unit.empty() ? std::string("minutes") : std::string(unit);
    if (trim(number) == "1" && counts.ends_with('s')) counts.pop_back();
    return std::string(trim(number)) + " " + counts;
}

// How a sound plays when its line doesn't say: as the sound named by a sound line has it (`named`: its name, else ""),
// else as its type has it; fading out what plays before it: a song's from Settings, a type's that plays one at a time
// in half a second, others' not at all.
struct SoundUsual {
    std::string loop, volume, fade, fadeOut;
};
SoundUsual soundUsual(const StoryEditor& e, std::string_view type, std::string_view named) {
    auto own = [&](std::string_view key) { return named.empty() ? std::string{} : e.soundOption(named, key); };
    const std::string loop = own("loop").empty() ? e.soundTypeOption(type, "loop") : own("loop");
    return {.loop = parseBool(loop).value_or(false) ? "yes" : "no",
            .volume = own("volume").empty() ? std::string("100%") : percentRead(own("volume")),
            .fade = own("fade").empty() ? std::string("At once"_hint) : own("fade"),
            .fadeOut = !own("fade_out").empty()                                                ? own("fade_out")
                       : lower(type) == "music"                                                ? std::string("From Settings"_hint)
                       : parseBool(e.soundTypeOption(type, "one_at_a_time")).value_or(false) ? std::string("0.5")
                                                                                               : std::string("None"_hint)};
}

const Slot kWaitSlots[]{{.type = Type::Word, .name = "Seconds"_text, .box = Box::Seconds, .hint = "Until a click"_hint, .seconds = true, .label = "Waits for (seconds)"_text}};
const Slot kTimeSlots[]{{.type = Type::Words, .name = "When"_text, .box = Box::TimeWhen, .only = "time", .label = "Goes on to"_text},
                        {.type = Type::Option, .name = "Day"_text, .key = "day", .box = Box::Count, .hint = "None"_hint, .only = "time", .label = "Day number"_text},
                        {.type = Type::Words, .name = "How long"_text, .box = Box::Number, .hint = "To the next part of the day"_hint, .only = "time_passes"}}; // (a number, then its Unit: rowFields)

// (an effect started, or the effects stopped: stop effect, fading out)
const Slot kEffectSlots[]{{.type = Type::Words, .name = "Effects"_text, .box = Box::Effects, .choices = "greyscale sepia invert blur pixelate noise glitch vibration"_choices,
                           .only = "effect"},
                          {.type = Type::Option, .name = "Amount"_text, .key = "amount", .box = Box::Percent, .hint = "100%", .only = "effect", .label = "Strength"_text},
                          {.type = Type::Option, .name = "Fade"_text, .key = "fade", .box = Box::Seconds, .hint = "At once"_hint, .only = "effect", .label = "Fade in (seconds)"_text},
                          {.type = Type::After, .name = "Just for"_text, .key = "for", .box = Box::Seconds, .hint = "Until turned off"_hint, .only = "effect", .seconds = true,
                           .label = "Lasts for (seconds)"_text},
                          {.type = Type::Option, .name = "Fade out"_text, .key = "fade", .box = Box::Seconds, .hint = "At once"_hint, .only = "stop_effect", .label = "Fade out (seconds)"_text}};

// (the weather started, or stopped: stop weather, fading out)
const Slot kWeatherSlots[]{{.type = Type::Word, .name = "Weather"_text, .box = Box::WeatherKind, .choices = "rain snow petals leaves embers"_choices, .only = "weather",
                            .label = "Kind"_text},
                           {.type = Type::Option, .name = "Amount"_text, .key = "amount", .box = Box::Percent, .hint = "100%", .only = "weather", .group = "Look"_group},
                           {.type = Type::Option, .name = "Speed"_text, .key = "speed", .box = Box::Speed, .hint = "1", .only = "weather"},
                           {.type = Type::Option, .name = "Wind"_text, .key = "wind", .box = Box::Wind, .hint = "None"_hint, .only = "weather"},
                           {.type = Type::Option, .name = "Tint"_text, .key = "tint", .box = Box::Colour, .only = "weather", .label = "Tint colour"_text},
                           {.type = Type::Option, .name = "Image"_text, .key = "image", .box = Box::Image, .only = "weather", .label = "Drawn as"_text},
                           {.type = Type::Option, .name = "Behind people"_text, .key = "behind", .box = Box::YesNo, .choices = "no yes"_choices, .only = "weather",
                            .label = "Behind visuals"_text},
                           {.type = Type::Option, .name = "Fade in"_text, .key = "fade", .box = Box::Seconds, .hint = "At once"_hint, .only = "weather", .label = "Fade in (seconds)"_text},
                           {.type = Type::Option, .name = "Fade out"_text, .key = "fade", .box = Box::Seconds, .hint = "At once"_hint, .only = "stop_weather", .label = "Fade out (seconds)"_text}};

const Slot kCameraSlots[]{{.type = Type::Option, .name = "Animation"_text, .key = "animation", .box = Box::Animation, .hint = "None"_hint,
                           .suggest = "none"_suggest, .group = "Animations"_group, .label = "Animation from now on"_text},
                          {.type = Type::After, .name = "Animation now"_text, .key = "with", .box = Box::Animation, .suggest = "none"_suggest, .label = "Animation played now"_text},
                          {.type = Type::Flag, .name = "The story waits"_text, .key = "and wait", .box = Box::YesNo, .choices = "no yes"_choices, .label = "Story waits for it"_text}};

const Slot kScreenSlots[]{{.type = Type::Word, .name = "Screen"_text, .box = Box::Screen}};

const CommandForm kForms[]{
    {StoryEditor::Kind::Jump, "jump", "", kJumpSlots},
    {StoryEditor::Kind::Wait, "wait", "", kWaitSlots},
    {StoryEditor::Kind::Time, "time time_passes", "Goes on to|Passes", kTimeSlots, "Action"},
    {StoryEditor::Kind::Effect, "effect stop_effect", "Start|Stop", kEffectSlots, "Action"},
    {StoryEditor::Kind::Weather, "weather stop_weather", "Start|Stop", kWeatherSlots, "Action"},
    {StoryEditor::Kind::Camera, "camera", "", kCameraSlots},
    {StoryEditor::Kind::Screen, "open show_screen hide_screen", "Open|Show|Hide", kScreenSlots, "Action"},
    {StoryEditor::Kind::Together, "together", "", {}},
};

// Characters' and other definitions' forms (the same parts as commands' forms; the line starts with
// the kind's word).
const Slot kCharacterSlots[]{
    {.type = Type::Word, .name = "Name"_text, .box = Box::Name},
    {.type = Type::Option, .name = "Image"_text, .key = "image", .box = Box::Base, .label = "Visuals named"_text}, // (empty: the name, in lower case: see showGameSettingValues)
    {.type = Type::Option, .name = "Colour"_text, .key = "name_colour", .box = Box::Colour, .hint = "Default"_hint, .label = "Name colour"_text},
    {.type = Type::Option, .name = "Words' colour"_text, .key = "text_colour", .box = Box::Colour, .hint = "Default"_hint, .label = "Text colour"_text},
    {.type = Type::Option, .name = "Place"_text, .key = "at", .box = Box::Place, .hint = "centre", .group = "Where"_group, .label = "Across"_text}, // (how they come when a line doesn't say)
    {.type = Type::Option, .name = "Y"_text, .key = "y", .box = Box::Down, .hint = "-1", .label = "Up and down"_text},
    {.type = Type::Option, .name = "Layer"_text, .key = "layer", .box = Box::Layer, .hint = "1"},
    {.type = Type::Option, .name = "Zoom"_text, .key = "zoom", .box = Box::Zoom, .hint = "100%", .group = "Look"_group},
    {.type = Type::Option, .name = "Opacity"_text, .key = "opacity", .box = Box::Opacity, .hint = "100%"},
    {.type = Type::Option, .name = "Rotation"_text, .key = "rotation", .box = Box::Rotation, .hint = "0", .label = "Rotation (degrees)"_text},
    {.type = Type::Option, .name = "Flip"_text, .key = "flip", .box = Box::YesNo, .choices = "no yes"_choices, .label = "Mirrored"_text},
    {.type = Type::Option, .name = "Tint"_text, .key = "tint", .box = Box::Colour, .hint = "None"_hint, .label = "Tint colour"_text},
    {.type = Type::Option, .name = "Animation"_text, .key = "animation", .box = Box::Animation, .hint = "None"_hint, .suggest = "none"_suggest, .group = "Animations"_group,
     .label = "Animation while shown"_text},
    {.type = Type::Option, .name = "Animation on show"_text, .key = "animation_on_show", .box = Box::Animation, .hint = "From Settings"_hint, .suggest = "none"_suggest},
    {.type = Type::Option, .name = "Text appears"_text, .key = "text_appears", .box = Box::TextAppears, .choices = "usual letter word line"_choices, .group = "Lines"_group, .label = "Text appears"_text},
    {.type = Type::Option, .name = "Textbox"_text, .key = "textbox", .box = Box::Textbox, .hint = "textbox", .label = "Their textbox"_text},
    {.type = Type::Option, .name = "Voice files"_text, .key = "voice_files", .box = Box::VoiceFolder, .hint = "None"_hint,
     .suggest = "yes|no"_suggest, .group = "Voice"_group, .label = "Voice files' folder"_text},
    {.type = Type::Option, .name = "Made-up voice"_text, .key = "made_up_voice", .box = Box::MadeUpVoice, .hint = "None"_hint},
    {.type = Type::Option, .name = "Its speed"_text, .key = "made_up_voice_speed", .box = Box::Speed, .hint = "From Settings"_hint, .label = "Made-up voice speed"_text},
    {.type = Type::Option, .name = "Typing sound"_text, .key = "typing_sound", .box = Box::Sound, .hint = "None"_hint},
    {.type = Type::Option, .name = "Blinks"_text, .key = "blink", .box = Box::YesNoSettings, .choices = "usual yes no"_choices, .group = "Blinking and talking"_group},
    {.type = Type::Option, .name = "Blinks every"_text, .key = "seconds_between_blinks", .box = Box::Seconds, .hint = "From Settings"_hint, .label = "Blinks every (seconds)"_text},
    {.type = Type::Option, .name = "Mouth moves"_text, .key = "mouth_moves", .box = Box::MouthMoves, .choices = "usual yes voice no"_choices},
    {.type = Type::Option, .name = "Mouth's speed"_text, .key = "mouth_speed", .box = Box::PerSecond, .hint = "From Settings"_hint, .label = "Mouth speed (moves a second)"_text},
    {.type = Type::Option, .name = "Face"_text, .key = "face", .box = Box::Image, .hint = "None"_hint, .suggest = "yes|no"_suggest,
     .group = "Face"_group, .label = "Face visual"_text},
    {.type = Type::Option, .name = "Face's side"_text, .key = "face_side", .box = Box::Side, .choices = "left right"_choices, .label = "Side of the textbox"_text},
    {.type = Type::Option, .name = "Face copies"_text, .key = "face_copies", .box = Box::FaceCopies, .choices = "expression blink mouth"_choices, .commas = true, .label = "Copies from the stage"_text},
    {.type = Type::Option, .name = "Face flipped"_text, .key = "face_flip", .box = Box::YesNo, .choices = "no yes"_choices, .label = "Mirrored"_text},
    {.type = Type::Option, .name = "Face blinks"_text, .key = "face_blink", .box = Box::YesNoSame, .choices = "same yes no"_choices, .labels = "As on stage|Yes|No"_labels, .label = "Blinks"_text},
    {.type = Type::Option, .name = "Face's mouth"_text, .key = "face_mouth", .box = Box::FaceMouth, .choices = "same yes voice no"_choices, .label = "Mouth moves"_text},
    {.type = Type::Option, .name = "Contact"_text, .key = "contact", .box = Box::Variable, .hint = "Never"_hint, .group = "Phone"_group, .label = "In the contacts when"_text},
    {.type = Type::Option, .name = "Avatar"_text, .key = "avatar", .box = Box::Image, .hint = "Face visual"_hint},
    {.type = Type::Option, .name = "About"_text, .key = "about", .box = Box::Text, .hint = "None"_hint, .label = "About them"_text},
    {.type = Type::Option, .name = "Chat scene"_text, .key = "chat", .box = Box::Scene, .hint = "None"_hint},
    {.type = Type::Option, .name = "Call scene"_text, .key = "call", .box = Box::Scene, .hint = "None"_hint},
    {.type = Type::Option, .name = "Ringtone"_text, .key = "ringtone", .box = Box::Sound, .hint = "From Settings"_hint},
    {.type = Type::Option, .name = "Love"_text, .key = "love", .box = Box::Number, .hint = "None"_hint, .group = "Love"_group, .label = "Love at the start"_text}}; // (a love interest's)

const std::pair<std::string_view, ToolsText> kAtlasParts[]{{"expressions", "Expressions"}, {"eyes", "Eyes"}, {"mouth", "Mouth"}}; // (each, and what the form calls it)

bool appliesTo(const Slot& s, std::string_view verb) {
    return s.only.empty() || std::ranges::contains(wordsOf(s.only), verb);
}

// The verb a line starts with (one of the form's), and the words after it.
std::pair<std::string, std::vector<std::string>> verbOf(const CommandForm& f, std::string_view t) {
    t = trim(t);
    const std::string low = lower(t);
    std::string best;
    for (const std::string_view v : wordsOf(f.verbs)) {
        const std::string words = spaced(v);
        if ((low == words || low.starts_with(words + " ")) && words.size() >= spaced(best).size()) best = v;
    }
    if (best.empty()) best = wordsOf(f.verbs).front();
    const std::size_t at = std::min(t.size(), spaced(best).size());
    return {best, rawWords(t.substr(at))}; // (a space inside quotes doesn't split a word: keys="a b")
}

std::vector<StoryEditor::Field> formFields(const CommandForm& f, std::string_view t) {
    using Field = StoryEditor::Field;
    const auto [verb, w] = verbOf(f, t);
    std::vector<Field> out;
    if (wordsOf(f.verbs).size() > 1) out.push_back({.name = "What"_text, .value = verb, .label = std::string(f.what), .choices = std::string(f.verbs), .labels = std::string(f.labels)});
    std::vector<std::pair<std::string_view, std::string>> values; // by slot name (two slots can share one, for different verbs)
    auto set = [&](const Slot& s, std::string v) { values.emplace_back(s.name, std::move(v)); };
    auto matches = [&](std::size_t i, std::string_view words) { // the words at i
        const auto ws = wordsOf(words);
        if (ws.empty() || i + ws.size() > w.size()) return false;
        for (std::size_t k = 0; k < ws.size(); ++k)
            if (lower(w[i + k]) != ws[k]) return false;
        return true;
    };
    auto setting = [&](std::size_t i) { // a word that starts a setting
        if (f.kind == StoryEditor::Kind::Screen && (lower(w[i]) == "with" || lower(w[i]) == "and")) return true; // (how it opens: no screen picked yet)
        for (const Slot& s : f.slots) {
            if (!appliesTo(s, verb)) continue;
            if (s.type == Type::After && matches(i, s.key)) return true;
            if (s.type == Type::Option && lower(w[i]).starts_with(std::string(s.key) + "=")) return true;
            if (s.type == Type::Flag) {
                if (s.choices == "no yes" ? matches(i, s.key) : std::ranges::any_of(wordsOf(s.choices) | std::views::drop(1), [&](std::string_view c) { return matches(i, spaced(c)); }))
                    return true;
            }
        }
        return false;
    };
    std::size_t i = 0;
    for (const Slot& s : f.slots) {
        if (!appliesTo(s, verb)) continue;
        if (s.type == Type::Word) {
            if (i < w.size() && !setting(i)) {
                set(s, unquote(w[i++]));
                if (s.seconds && i < w.size() && lower(w[i]).starts_with("second")) ++i;
            }
        } else if (s.type == Type::Words) {
            std::vector<std::string> some;
            while (i < w.size() && !setting(i)) some.push_back(lower(w[i++]));
            set(s, joined(some));
        } else if (s.type == Type::Text) {
            std::vector<std::string> some;
            while (i < w.size() && !(w[i].contains('=') && !w[i].starts_with('"') && setting(i))) some.push_back(w[i++]);
            set(s, some.size() == 1 ? unquote(some[0]) : joined(some));
        }
    }
    std::vector<std::string> more;
    while (i < w.size()) {
        bool took = false;
        for (const Slot& s : f.slots) {
            if (!appliesTo(s, verb) || took) continue;
            if (s.type == Type::After && matches(i, s.key) && i + wordsOf(s.key).size() < w.size()) {
                i += wordsOf(s.key).size();
                set(s, unquote(w[i++]));
                if (s.seconds && i < w.size() && lower(w[i]).starts_with("second")) ++i;
                took = true;
            } else if (s.type == Type::Option && lower(w[i]).starts_with(std::string(s.key) + "=")) {
                set(s, unquote(std::string_view(w[i]).substr(s.key.size() + 1)));
                ++i;
                took = true;
            } else if (s.type == Type::Flag) {
                const auto cs = wordsOf(s.choices);
                for (std::size_t k = 1; k < cs.size() && !took; ++k) {
                    const std::string words = s.choices == "no yes" ? std::string(s.key) : spaced(cs[k]);
                    if (matches(i, words)) {
                        i += wordsOf(words).size();
                        set(s, std::string(cs[k]));
                        took = true;
                    }
                }
            }
        }
        if (!took) { // (a word after a setting: a time line's when, "day=2 evening", goes on with its words; others are kept as written)
            const auto words = std::ranges::find_if(f.slots, [&](const Slot& s) { return appliesTo(s, verb) && s.type == Type::Words && s.box == Box::TimeWhen; });
            if (words != std::ranges::end(f.slots)) {
                const auto had = std::ranges::find(values, words->name, &std::pair<std::string_view, std::string>::first);
                if (had != values.end()) had->second += (had->second.empty() ? "" : " ") + lower(w[i++]);
                else set(*words, lower(w[i++]));
            } else {
                more.push_back(w[i++]);
            }
        }
    }
    for (const Slot& s : f.slots) {
        if (!appliesTo(s, verb)) continue;
        const auto v = std::ranges::find(values, s.name, &std::pair<std::string_view, std::string>::first);
        std::string value = v != values.end() ? v->second : std::string{};
        const bool optional = (s.type == Type::Option || s.type == Type::Flag) && !s.commas && !s.choices.empty(); // (its first choice: what none written means)
        if (s.commas) { // (not written, or yes: all of them; no: none)
            std::ranges::replace(value, ',', ' ');
            const std::string low = lower(trim(value));
            value = low.empty() || low == "yes" ? std::string(s.choices) : low == "no" ? std::string{} : low;
        } else if (value.empty() && !s.choices.empty() && !(s.type == Type::Words)) {
            value = wordsOf(s.choices).front(); // (the first: none written)
        }
        if (StoryEditor::kindOf(s.box).percent) value = percentRead(value);
        out.push_back({.name = std::string(s.name), .value = value, .box = s.box, .choices = std::string(s.choices), .hint = std::string(s.hint), .labels = std::string(s.labels),
                       .multi = (s.type == Type::Words && !s.choices.empty()) || s.commas || s.box == Box::Key, // (a control's keys: several)
                       .context = s.box == Box::Record ? verb : std::string{},
                       .suggest = std::string(s.suggest), .rich = s.rich, .group = std::string(s.group),
                       .usual = optional ? std::string(wordsOf(s.choices).front()) : std::string{}});
        if (!s.label.empty()) out.back().label = std::string(s.label);
    }
    if (!more.empty()) out.push_back({.name = "More"_text, .value = joined(more), .hidden = true});
    return out;
}

} // namespace

// The kinds of box (see StoryEditor::Box), each once: every row of that kind lists, suggests and says the same.
const StoryEditor::BoxKind& StoryEditor::kindOf(Box box) {
    static const BoxKind kinds[]{
        {.box = Box::None},
        {.box = Box::Words, .rich = true, .wide = true},
        {.box = Box::Text, .wide = true},
        {.box = Box::Name},
        {.box = Box::Image, .pick = Pick::ImagesOrColours, .wheel = true, .images = true, .wide = true}, // (an image, or a colour: the square and the wheel, the images under it)
        {.box = Box::ImageFile, .pick = Pick::ImageFiles, .images = true, .wide = true},
        {.box = Box::Shape, .pick = Pick::Images, .images = true, .wide = true},
        {.box = Box::Colour, .pick = Pick::Colours, .wheel = true},
        {.box = Box::Sound, .pick = Pick::Sounds, .listen = true, .wide = true},
        {.box = Box::FontFile, .pick = Pick::Fonts, .wide = true},
        {.box = Box::Font, .pick = Pick::FontNames, .wide = true},
        {.box = Box::Style, .pick = Pick::Styles},
        {.box = Box::Character, .pick = Pick::Characters},
        {.box = Box::Shown, .pick = Pick::Shown, .images = true, .wide = true},
        {.box = Box::VisualType, .pick = Pick::VisualTypes},
        {.box = Box::Still, .pick = Pick::Images, .images = true, .wide = true},
        {.box = Box::Movie, .pick = Pick::Movies, .images = true, .wide = true},
        {.box = Box::Accepts, .choices = "character image movie colour"_choices, .labels = "Characters|Images|Movies|Colours"_labels},
        {.box = Box::SoundType, .pick = Pick::SoundTypes},
        {.box = Box::Follows, .choices = "music sound voice"_choices, .labels = "Music|Sound|Voice"_labels}, // (the player's volume of that name)
        {.box = Box::PlayStop, .choices = "play stop"_choices, .labels = "Play|Stop"_labels},
        {.box = Box::Expression, .pick = Pick::Expressions, .images = true},
        {.box = Box::Scene, .pick = Pick::Scenes},
        {.box = Box::Screen, .pick = Pick::Screens},
        {.box = Box::Textbox, .pick = Pick::Textboxes},
        {.box = Box::Variable, .pick = Pick::VariableNames},
        {.box = Box::Setting, .pick = Pick::Settings},
        {.box = Box::Animation, .pick = Pick::Animations},
        {.box = Box::Key, .pick = Pick::Keys},
        {.box = Box::Place, .pick = Pick::Places}, // (a place's name, or a number typed: never examples of one)
        {.box = Box::Channel, .pick = Pick::Channels},
        {.box = Box::Record, .pick = Pick::Records, .wide = true},
        {.box = Box::VoiceFolder, .pick = Pick::VoiceFolders, .wide = true},
        {.box = Box::Control, .pick = Pick::Controls},
        {.box = Box::Base, .pick = Pick::Bases, .wide = true},
        {.box = Box::Language, .suggest = ToolsSuggestions(languageList()), .wide = true}, // (the languages' names: not the tools' own)
        // (numbers: typed, never picked from examples of them)
        {.box = Box::Across},
        {.box = Box::Down},
        {.box = Box::Layer},
        {.box = Box::Speed},
        {.box = Box::Zoom, .percent = true},
        {.box = Box::Opacity, .percent = true},
        {.box = Box::Wipe, .percent = true},
        {.box = Box::Volume, .percent = true},
        {.box = Box::Percent, .percent = true},
        {.box = Box::Rotation},
        {.box = Box::Seconds},
        {.box = Box::Pixels},
        {.box = Box::Count},
        {.box = Box::Number},
        {.box = Box::PerSecond},
        {.box = Box::LineSpacing},
        {.box = Box::TextSpeed},
        {.box = Box::Pause, .suggest = "click"_suggest}, // (until a click; or a number of seconds, typed)
        {.box = Box::Wind},
        {.box = Box::FontStyle, .suggest = "Regular|Bold|Italic|Bold Italic"_suggest},
        {.box = Box::MadeUpVoice, .suggest = "high|medium|low|deep|no"_suggest, .listen = true}, // (a few words heard in its pitch)
        {.box = Box::VariableValue},
        {.box = Box::Action, .suggest = ToolsSuggestions(actionGroupList())},
        {.box = Box::ScreenAction, .suggest = ToolsSuggestions(actionKindList("screen"))},
        {.box = Box::StoryAction, .suggest = ToolsSuggestions(actionKindList("story"))},
        {.box = Box::SoundAction, .suggest = ToolsSuggestions(actionKindList("sound"))},
        {.box = Box::SaveAction, .suggest = ToolsSuggestions(actionKindList("saves"))},
        {.box = Box::SettingAction, .suggest = ToolsSuggestions(actionKindList("settings"))},
        {.box = Box::ExtrasAction, .suggest = ToolsSuggestions(actionKindList("extras"))},
        {.box = Box::PhoneAction, .suggest = ToolsSuggestions(actionKindList("phone"))},
        {.box = Box::ToolsAction, .suggest = ToolsSuggestions(actionKindList("tools"))},
        {.box = Box::ContainerFit, .choices = "box parts"_choices, .labels = "Fills its box|As big as its parts"_labels},
        {.box = Box::Stack, .choices = "no down across"_choices, .labels = "As placed|Down|Across"_labels},
        {.box = Box::List, .suggest = "options|history|lines|pages|saves|gallery|endings|songs|achievements|tips|apps|chats|messages|calls|people|notices|captions|subtitles"_suggest},
        {.box = Box::KeySlot, .suggest = "1|2|pad"_suggest},
        {.box = Box::SaveMode, .choices = "usual save load"_choices, .labels = "By its screen|Saves|Loads"_labels},
        {.box = Box::YesNo, .choices = "yes no"_choices, .labels = "Yes|No"_labels},
        {.box = Box::YesNoStyle, .choices = "inherit yes no"_choices, .labels = "From its base style|Yes|No"_labels},
        {.box = Box::YesNoSettings, .choices = "usual yes no"_choices, .labels = "From Settings|Yes|No"_labels},
        {.box = Box::YesNoSame, .choices = "same yes no"_choices, .labels = "As it is|Yes|No"_labels},
        {.box = Box::Easing, .choices = "inout linear in out bounce spring overshoot"_choices,
         .labels = "Smoothly|Evenly|Speeding up|Slowing down|Bouncing|Wobbling|Overshooting"_labels},
        {.box = Box::Plays, .choices = "once loop times"_choices, .labels = "Once|Looping|A number of times"_labels},
        {.box = Box::Condition, .pick = Pick::Conditions},
        {.box = Box::SettingChoice}, // (the list is the setting's own: the Library's Settings' box for it, as a list)
        {.box = Box::GameLanguage, .pick = Pick::Languages},
        {.box = Box::Fit, .choices = "contain cover stretch original"_choices, .labels = "All of it shows|It fills the part|Stretched|Its own size"_labels},
        {.box = Box::TextAppears, .choices = "usual letter word line"_choices, .labels = "From Settings|A letter at a time|A word|A line"_labels},
        {.box = Box::MouthMoves, .choices = "usual yes voice no"_choices, .labels = "From Settings|Yes|With a voice|No"_labels},
        {.box = Box::ImageScaling, .choices = "smooth pixel"_choices, .labels = "Smooth|Sharp pixels"_labels},
        {.box = Box::HoldToSkip, .choices = "ctrl shift alt none"_choices, .labels = "Ctrl|Shift|Alt|None"_labels},
        {.box = Box::WindowFill, .choices = "fit integer stretch fill"_choices, .labels = "Fit|Whole pixels|Stretch|Fill"_labels},
        {.box = Box::Mouths, .choices = "yes voice no"_choices, .labels = "Yes|With a voice|No"_labels},
        {.box = Box::StoryWhileOpen, .choices = "waits goes_on"_choices, .labels = "Waits|Goes on"_labels},
        {.box = Box::NewMessages, .choices = "button open"_choices, .labels = "Phone button|Phone opens"_labels},
        {.box = Box::ChatMessages, .choices = "typed click"_choices, .labels = "As they're typed|Each after a click"_labels},
        {.box = Box::TimeKind, .choices = "none story real days"_choices, .labels = "None|The story's own|Real time|Days and parts of the day"_labels},
        {.box = Box::Clock, .choices = "24 12"_choices, .labels = "24-hour|12-hour"_labels},
        {.box = Box::Weekday, .pick = Pick::Weekdays}, // (a weekday, or a date typed: 2025-07-14)
        {.box = Box::TimeUnit, .choices = "minutes hours days"_choices, .labels = "Minutes|Hours|Days"_labels},
        {.box = Box::DayParts, .wide = true},
        {.box = Box::TimeWhen, .pick = Pick::TimeWords},
        {.box = Box::MessageKind, .choices = "says sends sticker voice typing deletes edits reacts reads"_choices,
         .labels = "Says|Sends a visual|Sends a sticker|Sends a voice message|Is typing|Deletes their last|Edits their last|Reacts|Reads the other's"_labels},
        {.box = Box::PhoneLine, .choices = "chat call"_choices, .labels = "Chat|Call"_labels},
        {.box = Box::Side, .choices = "left right"_choices, .labels = "Left|Right"_labels},
        {.box = Box::FaceCopies, .choices = "expression blink mouth"_choices, .labels = "Expression|Blinks|Mouth"_labels},
        {.box = Box::FaceMouth, .choices = "same yes voice no"_choices, .labels = "As on stage|Yes|With a voice|No"_labels},
        {.box = Box::Align, .choices = "inherit left centre right"_choices, .labels = "From its base style|Left|Centre|Right"_labels},
        {.box = Box::AlignVertically, .choices = "inherit top centre bottom"_choices, .labels = "From its base style|Top|Centre|Bottom"_labels},
        {.box = Box::Operation, .choices = "nothing + - * /"_choices, .labels = "None|Plus|Minus|Times|Divided by"_labels},
        {.box = Box::Joiner, .choices = "nothing and or"_choices, .labels = "None|And|Or"_labels},
        {.box = Box::ShowHide, .choices = "show hide"_choices, .labels = "Show|Hide"_labels},
        {.box = Box::Visibility, .choices = "hidden locked"_choices, .labels = "Hidden|Locked"_labels},
        {.box = Box::Sent, .choices = "to_player by_player"_choices, .labels = "To the player|By the player"_labels},
        {.box = Box::Position, .choices = "top centre bottom"_choices, .labels = "The top|The centre|The bottom"_labels},
        {.box = Box::Repeat, .choices = "no across down both"_choices, .labels = "No|Across|Down|Both ways"_labels},
        {.box = Box::Scroll, .choices = "no left right up down"_choices, .labels = "No|Left|Right|Up|Down"_labels},
        {.box = Box::Effects, .choices = "greyscale sepia invert blur pixelate noise glitch vibration"_choices, .labels = "Greyscale|Sepia|Invert|Blur|Pixelate|Noise|Glitch|Vibration"_labels},
        {.box = Box::WeatherKind, .choices = "rain snow petals leaves embers"_choices, .labels = "Rain|Snow|Petals|Leaves|Embers"_labels},
        {.box = Box::Edges, .choices = "show cut fade"_choices, .labels = "Shown|Cut off|Faded out"_labels},
        {.box = Box::InputChanges, .choices = "variable setting control value"_choices, .labels = "A variable|A setting|A key|Nothing"_labels},
        {.box = Box::InputAs, .choices = "typing slider"_choices, .labels = "Typing|A slider"_labels},
    };
    const auto it = std::ranges::find(kinds, box, &BoxKind::box);
    return it != std::end(kinds) ? *it : kinds[0];
}

void applyBox(StoryEditor::Field& f) {
    if (f.box == StoryEditor::Box::None) return;
    const StoryEditor::BoxKind& k = StoryEditor::kindOf(f.box);
    if (k.pick != StoryEditor::Pick::None) f.pick = k.pick;
    if (!k.suggest.empty()) f.suggest = f.suggest.empty() ? std::string(k.suggest) : std::string(k.suggest) + "|" + f.suggest;
    if (!k.choices.empty()) {
        if (f.choices.empty()) f.choices = k.choices;
        if (f.labels.empty()) { // (the kind's words in the row's order: their labels in that order)
            const auto words = wordsOf(k.choices), labels = wordsOf(k.labels, '|');
            std::string text;
            for (const std::string_view word : wordsOf(f.choices)) {
                const auto at = std::ranges::find(words, word);
                const std::string_view label = at != words.end() && static_cast<std::size_t>(at - words.begin()) < labels.size() ? labels[static_cast<std::size_t>(at - words.begin())] : word;
                text += (text.empty() ? "" : "|") + std::string(label);
            }
            f.labels = std::move(text);
        }
    }
    f.rich = f.rich || k.rich;
}

std::string percentText(std::string_view value) {
    const std::string_view v = trim(value);
    if (v.empty() || v.ends_with('%')) return std::string(v);
    const auto n = parseNumber(v);
    if (!n) return std::string(v); // (a variable, or words)
    const float percent = v.contains('.') || *n <= 1.0f ? *n * 100.0f : *n; // (a fraction, as scripts may write it; else a percent already)
    return toString(number(percent)) + "%";
}

std::string percentRead(std::string_view value) {
    const std::string_view v = trim(value);
    if (v.empty() || v.ends_with('%')) return std::string(v);
    const auto n = parseNumber(v);
    if (!n) return std::string(v); // (a variable, or words)
    return toString(number(*n * 100.0f)) + "%";
}

namespace {

// In brackets from its start to its end: "(a + b)", not "(a) + (b)".
bool bracketed(std::string_view text) {
    if (text.size() < 2 || text.front() != '(' || text.back() != ')') return false;
    int depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        depth += text[i] == '(' ? 1 : text[i] == ')' ? -1 : 0;
        if (depth == 0 && i + 1 < text.size()) return false;
    }
    return true;
}

// A sum as its values and the signs between them (+ - * /), as the editor writes it: in the order it's
// read, with brackets around what comes before when that has to be worked out first ((a + b) * c).
// None when it's more than that (a + b * c, min(a, b)...): then it's shown whole.
struct Sum {
    std::vector<std::string> terms, ops;
};

bool timesOrBy(std::string_view op) { return op == "*" || op == "/"; }
std::optional<Sum> sumOf(std::string_view text) {
    text = trim(text);
    const Parts p = splitTop(text, {"+", "-", "*", "/"});
    if (p.ops.empty()) {
        if (bracketed(text)) return sumOf(inside(text));
        return Sum{.terms = {std::string(text)}, .ops = {}};
    }
    bool plusBefore = false; // (a * after a +: the sum isn't worked out in the order it reads)
    for (const std::string& op : p.ops) {
        if (timesOrBy(op) && plusBefore) return std::nullopt;
        plusBefore = plusBefore || !timesOrBy(op);
    }
    Sum out;
    if (bracketed(p.parts[0])) {
        const auto first = sumOf(inside(p.parts[0]));
        if (!first) return std::nullopt;
        out = *first;
    } else {
        out.terms = {p.parts[0]};
    }
    for (std::size_t i = 1; i < p.parts.size(); ++i) {
        if (bracketed(p.parts[i]) || p.parts[i].empty()) return std::nullopt;
        out.ops.push_back(p.ops[i - 1]);
        out.terms.push_back(p.parts[i]);
    }
    return out;
}

std::string sumText(const Sum& s) {
    std::string out = s.terms.front();
    bool plus = false; // (the last sign at the top is + or -)
    for (std::size_t i = 0; i < s.ops.size(); ++i) {
        if (timesOrBy(s.ops[i])) {
            if (plus) out = "(" + out + ")";
            plus = false;
        } else {
            plus = true;
        }
        out += std::format(" {} {}", s.ops[i], s.terms[i + 1]);
    }
    return out;
}

const std::set<std::string, std::less<>>& expressionWords() {
    static const std::set<std::string, std::less<>> words = [] {
        std::set<std::string, std::less<>> out{"yes", "no", "true", "false", "and", "or", "not", "is", "at", "least", "most", "more", "less", "fewer",
                                               "greater", "than", "random", "min", "max", "abs", "round", "len", "str", "int", "upper", "lower", "if", "answer", "dropped"};
        for (const std::string_view w : kBuiltinValues) out.emplace(w);
        for (const Setting& s : kSettings) // (the player's settings: the Library's Settings' rows for new players')
            if (s.name.starts_with("default.")) out.emplace(s.name.substr(8));
        return out;
    }();
    return words;
}

// Text in single quotes ("english" -> 'english'), as a part's options and actions need it: they're in double ones.
std::string inSingleQuotes(const std::string& v) { return isQuoted(v) && !v.contains('\'') ? "'" + v.substr(1, v.size() - 2) + "'" : v; }

// A random number's lowest and highest, as written: random(1, 6), or random(6) for 0 to 5.
std::optional<std::pair<std::string, std::string>> randomOf(std::string_view term) {
    term = trim(term);
    if (!lower(term).starts_with("random(") || !term.ends_with(')')) return std::nullopt;
    const Parts args = splitTop(term.substr(7, term.size() - 8), {","});
    if (args.parts.size() == 2) return std::pair{args.parts[0], args.parts[1]};
    if (const auto n = parseNumber(args.parts[0]); args.parts.size() == 1 && n) return std::pair{std::string("0"), std::format("{}", *n - 1)};
    return std::nullopt;
}

// A value's fields: a box with what it can be (a variable from its list, or typed: yes, no, a number, random for a
// random number...), and for a random number the lowest and highest it can be. Named `name`, `name from` and
// `name to`.
void addValueFields(std::vector<StoryEditor::Field>& out, const std::string& name, std::string_view label, std::string_view hint, std::string_view term,
                    Box box, const std::vector<std::string>& variables, bool asWritten = false) {
    if (const auto r = randomOf(term)) {
        out.push_back({.name = name, .value = "random", .label = std::string(label), .box = box});
        out.push_back({.name = name + " from", .value = r->first, .label = "From"_text, .box = Box::Count, .hint = "1"});
        out.push_back({.name = name + " to", .value = r->second, .label = "To"_text, .box = Box::Count, .hint = "6"});
        return;
    }
    out.push_back({.name = name, .value = asWritten ? std::string(term) : shown(term, variables), .label = std::string(label), .box = box,
                   .hint = std::string(hint)}); // (its list: the variables, as its kind lists them)
    out.push_back({.name = name + " as written", .value = std::string(trim(term)), .hidden = true}); // (unchanged, it's written so again)
}

std::string valueFrom(const std::vector<StoryEditor::Field>& fields, const std::string& name, const std::vector<std::string>& variables, bool asWritten = false) {
    std::string v = fieldValue(fields, name);
    if (lower(v) == "random") {
        const std::string from = fieldValue(fields, name + " from"), to = fieldValue(fields, name + " to");
        return std::format("random({}, {})", from.empty() ? "1" : from, to.empty() ? "6" : to);
    }
    if (asWritten) return v;
    const std::string was = fieldValue(fields, name + " as written");
    return hasField(fields, name + " as written") && v == shown(was, variables) ? was : scripted(v, variables);
}

// A sum's fields: a value's (above) for each of its values, with "Then" (nothing more, plus, minus,
// times, divided by) after each; a sum that's more than that is one box, as written.
void addSumFields(std::vector<StoryEditor::Field>& out, const std::string& name, std::string_view label, std::string_view hint, std::string_view text,
                  const std::vector<std::string>& variables) {
    const auto sum = sumOf(text);
    if (!sum) {
        out.push_back({.name = name, .value = std::string(trim(text)), .label = std::string(label), .box = Box::Variable});
        out.push_back({.name = name + " written", .value = "yes", .hidden = true});
        return;
    }
    for (std::size_t i = 0; i < sum->terms.size(); ++i) {
        const std::string n = numbered(name, i);
        addValueFields(out, n, i == 0 ? label : std::string_view("Value"_text), hint, sum->terms[i], Box::Variable, variables);
        out.push_back({.name = n + " then", .value = i < sum->ops.size() ? sum->ops[i] : "nothing", .label = "Then"_text, .box = Box::Operation, .choices = "nothing + - * /"_choices,
                       .usual = "nothing"});
    }
}

std::string sumFrom(const std::vector<StoryEditor::Field>& fields, const std::string& name, const std::vector<std::string>& variables) {
    if (hasField(fields, name + " written")) return fieldValue(fields, name);
    Sum s;
    for (std::size_t i = 0; hasField(fields, numbered(name, i)); ++i) {
        const std::string n = numbered(name, i);
        const std::string term = valueFrom(fields, n, variables);
        if (term.empty() && i > 0) { // (one emptied: the sum ends before it)
            s.ops.pop_back();
            break;
        }
        s.terms.push_back(term);
        const std::string then = fieldValue(fields, n + " then");
        if (then.empty() || then == "nothing") break;
        s.ops.push_back(then);
    }
    if (s.terms.empty()) return {};
    if (s.ops.size() == s.terms.size()) s.terms.emplace_back("1"); // (Plus just clicked: a number to change)
    return sumText(s);
}

// A condition as checks, each "what", how it compares and the value ("trust is at least 3"), joined by
// "and" or "or" ("then"). Is "yes": what's on the left isn't 0 or empty (has_key); "no": it is (not
// has_key). The editor writes the checks in the order they're read, with brackets around what comes
// before an "and" that follows an "or" ((a or b) and c). Anything before the first comparison is
// "what", and a check that's more than one (b and c, after an "or") is kept whole as its "what".
struct Check {
    std::string what, is = "yes", value, then;
};

constexpr std::string_view kComparisons = "yes no is is_not is_at_least is_at_most is_more_than is_less_than";
const ToolsLabels kComparisonLabels = "yes|no|equal to|not equal to|at least|at most|more than|less than";
Check checkOf(std::string_view clause) {
    const auto w = rawWords(trim(clause));
    static constexpr std::pair<std::string_view, std::string_view> symbols[]{
        {"=", "is"}, {"==", "is"}, {"!=", "is_not"}, {">=", "is_at_least"}, {"<=", "is_at_most"}, {">", "is_more_than"}, {"<", "is_less_than"}};
    for (std::size_t i = 1; i < w.size(); ++i) {
        std::string is;
        std::size_t n = 0;
        for (const auto& [symbol, words] : symbols)
            if (w[i] == symbol) is = words, n = 1;
        if (lower(w[i]) == "is") { // the longest of: is, is not, is at least...
            for (const std::string_view words : wordsOf(kComparisons)) {
                const auto ws = wordsOf(words, '_');
                bool ok = ws.front() == "is" && i + ws.size() <= w.size();
                for (std::size_t k = 0; ok && k < ws.size(); ++k) ok = lower(w[i + k]) == ws[k];
                if (ok && ws.size() > n) is = words, n = ws.size();
            }
        }
        if (n == 0) continue;
        return {.what = joined(std::vector(w.begin(), w.begin() + static_cast<std::ptrdiff_t>(i))), .is = is,
                .value = joined(std::vector(w.begin() + static_cast<std::ptrdiff_t>(i + n), w.end()))};
    }
    if (w.size() > 1 && lower(w[0]) == "not") return {.what = std::string(inside(joined(w, 1))), .is = "no"};
    return {.what = joined(w), .is = "yes"};
}

std::vector<Check> checksOf(std::string_view condition) {
    condition = trim(condition);
    const Parts p = splitTop(condition, {"and", "or"});
    if (p.ops.empty()) {
        if (bracketed(condition) && !splitTop(inside(condition), {"and", "or"}).ops.empty()) return checksOf(inside(condition));
        return {checkOf(condition)};
    }
    // "or" last ("and" first): the parts between the "or"s are groups; the first group can be checks of
    // its own, a later one only one check (else it's kept whole).
    std::vector<Check> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= p.ops.size(); ++i) {
        if (i < p.ops.size() && p.ops[i] != "or") continue;
        std::vector<std::string> group(p.parts.begin() + static_cast<std::ptrdiff_t>(start), p.parts.begin() + static_cast<std::ptrdiff_t>(i + 1));
        if (out.empty()) {
            for (std::size_t g = 0; g < group.size(); ++g) {
                auto checks = g == 0 && bracketed(group[g]) ? checksOf(group[g]) : std::vector{checkOf(group[g])};
                if (!out.empty()) out.back().then = "and";
                std::ranges::move(checks, std::back_inserter(out));
            }
        } else {
            std::string whole;
            for (const std::string& part : group) whole += (whole.empty() ? "" : " and ") + part;
            out.back().then = "or";
            out.push_back(group.size() == 1 ? checkOf(whole) : Check{.what = whole, .is = "yes"});
        }
        start = i + 1;
    }
    return out;
}

std::string conditionOf(const std::vector<Check>& checks) {
    std::string out;
    bool or_ = false; // (the last join at the top is "or")
    std::string join;
    for (const Check& c : checks) {
        if (trim(c.what).empty()) continue;
        const bool several = !splitTop(c.what, {"and", "or"}).ops.empty() && !bracketed(c.what);
        const std::string what = several ? "(" + c.what + ")" : c.what;
        std::string one = what;
        if (c.is == "no") one = "not " + (c.what.contains(' ') && !several && !bracketed(c.what) ? "(" + c.what + ")" : what);
        else if (c.is != "yes") one = std::format("{} {} {}", what, spaced(c.is), trim(c.value).empty() ? "0" : c.value); // (0: to change)
        if (!out.empty()) {
            if (join == "and" && or_) out = "(" + out + ")";
            or_ = join == "or";
            out += std::format(" {} ", join);
        }
        out += one;
        join = c.then.empty() ? "and" : c.then;
    }
    return out;
}

// Commands with ; between them (outside quotes).
std::vector<std::string> actionsOf(std::string_view text) {
    std::vector<std::string> out;
    char quote = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : ';';
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == ';') {
            if (const auto part = trim(text.substr(start, i - start)); !part.empty()) out.emplace_back(part);
            start = i + 1;
        }
    }
    return out;
}

// What buttons, keys and screens can do: by what it's about (the first box lists these groups, and the
// actions that stand alone: the language, quit, a screenshot), then by kind (the second box lists the
// group's: its word, what the list says it does; a setting's name), and what comes after the word
// (what the third box asks for, and what it lists; none: nothing more).
struct ActionGroup {
    std::string_view word;
    Box box;               // the kinds of this group: their box's kind (None: one kind, with no box for it)
    ToolsText how = "What it does"; // what the form calls that box
};

const ActionGroup kActionGroups[]{
    {"screen", Box::ScreenAction},
    {"story", Box::StoryAction},
    {"variable", Box::None}, // (set: one kind)
    {"sound", Box::SoundAction},
    {"saves", Box::SaveAction},
    {"extras", Box::ExtrasAction},
    {"phone", Box::PhoneAction},
    {"settings", Box::SettingAction, "Setting"},
    {"language", Box::None},
    {"quit", Box::None},
    {"screenshot", Box::None},
    {"tools", Box::ToolsAction}, // (the developer tools' own keys: nothing in the game)
    {"other", Box::None},
};

struct ActionKind {
    std::string_view group, verb;
    ToolsText detail;
    Box box; // what comes after its word: its box's kind
    ToolsHint hint;
    ToolsSuggestions suggest;
    std::string_view context;
    ActionKind(std::string_view g, std::string_view v, ToolsText d = {}, Box b = Box::None, ToolsHint h = {}, ToolsSuggestions s = {}, std::string_view c = {})
        : group(g), verb(v), detail(d), box(b), hint(h), suggest(s), context(c) {}
};

bool percentSetting(std::string_view setting);         // (a player setting the Library's Settings writes as a percent: "85%")
std::string_view settingOptions(std::string_view setting); // (what it can go to, as a list: as the Library's Settings' box for it has it)
const ActionKind kActionKinds[]{
    {"screen", "open", "Screen", Box::Screen},
    {"screen", "close"},
    {"screen", "show screen", "Screen", Box::Screen},
    {"screen", "hide screen", "Screen", Box::Screen},
    {"screen", "answer", "Answer", Box::Text},
    {"screen", "yes"},
    {"screen", "no"},
    {"screen", "move up"},
    {"screen", "move down"},
    {"screen", "move left"},
    {"screen", "move right"},
    {"screen", "press"},
    {"screen", "scroll up"},
    {"screen", "scroll down"},
    {"screen", "scroll to the top"},
    {"screen", "scroll to the end"},
    {"screen", "put back"},
    {"variable", "set", "Variable and value", Box::Variable},
    {"story", "start", "From", Box::Scene, "Beginning"},
    {"story", "continue"},
    {"story", "jump", "Scene", Box::Scene},
    {"story", "next line"},
    {"story", "rollback", "Back to line", Box::Count, "A step back"_hint}, // (the history's line N; none: a step back)
    {"story", "skip"},
    {"story", "auto"},
    {"story", "hide ui"},
    {"story", "choose", "Option", Box::Count, "The one it's on"_hint}, // (the choice's option N; none, in a list of options: its own)
    {"story", "replay", "Line", Box::Count}, // (the history's line N's voice, again)
    {"sound", "play", "Sound", Box::Sound}, // (and as a type: its own box, in addActionFields)
    {"sound", "stop", "Sound", Box::Channel},
    {"saves", "quick save"},
    {"saves", "quick load"},
    {"saves", "next page"},
    {"saves", "previous page"},
    {"saves", "page", "Page", Box::Text}, // (3, auto, quick; in a list of pages: {page.name})
    {"saves", "save", "Save", Box::Count}, // (the page open's save N; in a list of saves: {save.number})
    {"saves", "load", "Save", Box::Count},
    {"saves", "delete save", "Save", Box::Count},
    // (a player's setting: the second box names it, the third says what it goes to; the settings themselves
    // come from the Library's Settings' rows, in actionKinds(), before these)
    {"settings", "reset settings"},
    {"settings", "reset controls"},
    {"settings", "reset progress"},
    {"extras", "open image", "Visual", Box::Image}, // (the gallery's, or one from a chat: {open_image})
    {"extras", "next image"},
    {"extras", "previous image"},
    {"extras", "play song", "Song", Box::Sound},
    {"extras", "open tip", "Tip", Box::Text},
    {"phone", "open app", "App", Box::Name},
    {"phone", "open chat", "With", Box::Text},
    {"phone", "back"},
    {"phone", "phone call", "Who", Box::Text},
    {"phone", "answer call"},
    {"phone", "hang up"},
    {"phone", "decline call"},
    {"phone", "keypad", "Key", Box::Text},
    {"phone", "unlock"},
    {"language", "setting language", "Language", Box::GameLanguage},
    {"quit", "quit"},
    {"screenshot", "screenshot"},
    {"tools", "tools undo"},
    {"tools", "tools redo"},
    {"tools", "tools cut"},
    {"tools", "tools copy"},
    {"tools", "tools paste"},
    {"tools", "tools duplicate"},
    {"tools", "tools delete"},
    {"tools", "tools select all"},
    {"tools", "tools reload"},
    {"tools", "tools quit"},
    {"tools", "tools story"},
    {"tools", "tools screens"},
    {"tools", "tools library"},
    {"tools", "tools play"},
    {"tools", "tools stop here"},
    {"tools", "tools next"},
    {"tools", "tools previous"},
    {"tools", "tools press"},
    {"tools", "tools up"},
    {"tools", "tools down"},
    {"tools", "tools left"},
    {"tools", "tools right"},
    {"tools", "tools back"},
    {"tools", "tools next page"},
    {"tools", "tools previous page"},
    {"other", "other", "Line", Box::Text},
};

// The window's size: the one setting with no row of its own in the Library's Settings (a width and a height,
// in two boxes).
const ActionKind kWindowSizeKind{"settings", "setting window_size", "Width", Box::Pixels};

// The Library's Settings' row for a player setting's default ("setting vsync" or "vsync": default.vsync), if any.
const Setting* settingRow(std::string_view setting) {
    if (setting.starts_with("setting ")) setting.remove_prefix(8);
    const std::string name = "default." + std::string(setting);
    const auto it = std::ranges::find(kSettings, std::string_view(name), &Setting::name);
    return it != kSettings.end() ? &*it : nullptr;
}

bool percentSetting(std::string_view setting) {
    const Setting* row = settingRow(setting);
    return row && StoryEditor::kindOf(row->box).percent;
}

std::string_view settingOptions(std::string_view setting) {
    static std::map<std::string, std::string, std::less<>> lists; // (made once each)
    if (const auto it = lists.find(setting); it != lists.end()) return it->second;
    const Setting* row = settingRow(setting);
    std::string list; // (its box's buttons' words as a list; else what its box suggests)
    if (row) {
        const StoryEditor::BoxKind& kind = StoryEditor::kindOf(row->box);
        if (!kind.choices.empty())
            for (const std::string_view choice : wordsOf(kind.choices)) list += std::format("{}{}", list.empty() ? "" : "|", choice);
        else list = std::string(row->suggest.empty() ? kind.suggest : row->suggest);
    }
    return lists.emplace(std::string(setting), std::move(list)).first->second;
}

const std::vector<ActionKind>& actionKinds() {
    static const std::vector<ActionKind> kinds = [] {
        static std::deque<std::string> words; // (the kinds' words, kept)
        auto kept = [](std::string text) -> std::string_view { return words.emplace_back(std::move(text)); };
        std::vector<ActionKind> out;
        bool settings = false;
        for (const ActionKind& a : kActionKinds) {
            if (a.group == "settings" && !settings) { // (the settings themselves, from the Library's Settings' rows, before the resets)
                settings = true;
                for (const Setting& s : kSettings) { // (what each goes to: settingOptions(), once its box is asked for: the kinds of box aren't known yet here)
                    if (!s.name.starts_with("default.")) continue;
                    out.emplace_back("settings", kept("setting " + std::string(s.name.substr(8))), "To"_text, Box::SettingChoice);
                    if (s.name == "default.fullscreen") out.push_back(kWindowSizeKind);
                }
            }
            out.push_back(a);
        }
        return out;
    }();
    return kinds;
}

// What the group's box calls a kind: its word, or a setting's name alone ("setting vsync": vsync), or what the tools do ("tools copy": copy).
std::string_view kindWord(const ActionKind& a) { return a.verb.starts_with("setting ") ? a.verb.substr(8) : a.verb.starts_with("tools ") ? a.verb.substr(6) : a.verb; }

const ActionKind* kindInGroup(std::string_view group, std::string_view word) { // (as the group's box has it)
    for (const ActionKind& a : actionKinds())
        if (a.group == group && kindWord(a) == word) return &a;
    return nullptr;
}

const ActionKind* firstKindOf(std::string_view group) {
    const auto it = std::ranges::find(actionKinds(), group, &ActionKind::group);
    return it != actionKinds().end() ? &*it : nullptr;
}

const ActionGroup* actionGroup(std::string_view word) {
    const auto it = std::ranges::find(kActionGroups, word, &ActionGroup::word);
    return it != std::end(kActionGroups) ? &*it : nullptr;
}

const ActionKind* actionKind(std::string_view verb) {
    const auto it = std::ranges::find(actionKinds(), verb, &ActionKind::verb);
    return it != actionKinds().end() ? &*it : nullptr;
}

// An action's kind and what comes after its word ("open settings": open, settings); one the list
// has no kind for is "other", all of it after.
std::pair<std::string, std::string> actionParts(std::string_view action) {
    const std::string text(trim(action)), low = lower(text);
    std::string_view best;
    for (const ActionKind& a : actionKinds())
        if (a.verb != "other" && (low == a.verb || low.starts_with(std::string(a.verb) + " ")) && a.verb.size() > best.size()) best = a.verb;
    const std::string rest(trim(std::string_view(text).substr(best.size())));
    if (best.empty() || (best == "stop" && (lower(rest) == "weather" || lower(rest) == "effect" || lower(firstWord(rest)) == "weather" || lower(firstWord(rest)) == "effect")))
        return {"other", text}; // (stop weather and stop effect: the weather's and the effects', as they are; not a sound's)
    return {std::string(best), rest};
}

// A set's value as the action writes it: a word that isn't a variable is text, in
// quotes (single ones: the actions are in double quotes already): english -> 'english'. (Its box shows it
// as shown() has it: without the quotes, as the Library's boxes do, when it would be written in quotes again.)
std::string valueWritten(const std::string& value, const std::vector<std::string>& variables) { return inSingleQuotes(scripted(value, variables)); }

// A set action's words after "set", as the variable and the value: "x to 1" (set x to 1).
std::pair<std::string, std::string> variableParts(const std::string& rest) {
    const auto at = lower(rest).find(" to ");
    if (at == npos) return {std::string(trim(rest)), std::string{}};
    return {std::string(trim(std::string_view(rest).substr(0, at))), std::string(trim(std::string_view(rest).substr(at + 4)))};
}

// An option's parts, as the parser reads them: Text [(if CONDITION) | (needs CONDITION: what it
// needs)].
struct OptionParts {
    std::string text, condition, note;
    bool locked = false; // (needs ...): shown, but it can't be picked until then
};

OptionParts optionParts(std::string_view line) {
    OptionParts o;
    std::string_view text = trim(line);
    if (text.ends_with(':') && !isQuoted(text)) text.remove_suffix(1);
    if (const auto open = text.rfind("(if "); text.ends_with(')') && open != npos && !isQuoted(text)) {
        o.condition = trim(text.substr(open + 4, text.size() - open - 5));
        text = trim(text.substr(0, open));
    } else if (const auto needs = text.rfind("(needs "); text.ends_with(')') && needs != npos && !isQuoted(text)) {
        std::string_view inside = text.substr(needs + 7, text.size() - needs - 8);
        std::size_t colon = npos;
        for (std::size_t k = 0, quotes = 0; k < inside.size(); ++k) {
            if (inside[k] == '"') ++quotes;
            else if (inside[k] == ':' && quotes % 2 == 0) colon = k;
        }
        if (colon != npos) {
            o.note = unquote(trim(inside.substr(colon + 1)));
            inside = trim(inside.substr(0, colon));
        }
        o.condition = inside;
        o.locked = true;
        text = trim(text.substr(0, needs));
    }
    o.text = unquote(text);
    return o;
}

} // namespace

std::vector<StoryEditor::Field> StoryEditor::fields(const Row& row) const {
    auto out = rowFields(row);
    // A phone line starts with which of them it is (not a chat's own lines: they're its messages).
    if (const std::string_view word = phoneWord(row.kind); !word.empty())
        out.insert(out.begin(), {.name = "Phone line"_text, .value = std::string(word), .label = "Kind"_text, .box = Box::PhoneLine,
                                 .choices = std::string(kindOf(Box::PhoneLine).choices)});
    return out;
}

void StoryEditor::timesToCheck(std::vector<Field>& fields) const {
    for (Field& f : fields) { // (a check's value, when what it checks is the part of the day, the weekday or the month: those, as the values are)
        if (!f.name.ends_with(" value")) continue;
        const std::string what = lower(fieldValue(fields, std::string_view(f.name).substr(0, f.name.size() - 6)));
        std::string list;
        if (what == "time_part")
            for (const std::string& part : names(Pick::TimeWords, "time")) list += (list.empty() ? "" : "|") + part;
        if (what == "time_weekday")
            for (const std::string_view day : kWeekdays) list += (list.empty() ? "" : "|") + std::string(day);
        if (what == "time_month")
            for (const std::string_view month : kMonths) list += (list.empty() ? "" : "|") + std::string(month);
        if (!list.empty()) f.suggest = list + (f.suggest.empty() ? "" : "|" + f.suggest);
    }
}

void StoryEditor::lovesToCheck(std::vector<Field>& fields) const {
    if (loveInterests().empty()) return;
    for (Field& f : fields) // (what's checked: not a value it's checked against)
        if (f.box == Box::Variable && !f.name.ends_with(" value")) f.suggest += (f.suggest.empty() ? "" : "|") + std::string("most_loved");
}

std::vector<StoryEditor::Field> StoryEditor::rowFields(const Row& row) const {
    const std::string_view t = row.text;
    if (const CommandForm* form = formOf(row.kind)) {
        auto out = formFields(*form, t);
        if (row.kind == Kind::Weather)
            for (Field& f : out)
                if (f.box == Box::Image) f.context = "still"; // (its specks: no movie)
        if (row.kind == Kind::Time) // (its when as two boxes: the day, a weekday or the same one; then the time of day, a part, a clock time or the same one)
            if (const auto when = std::ranges::find(out, std::string_view("When"), &Field::name); when != out.end()) {
                std::string day, time;
                for (const std::string& word : rawWords(when->value))
                    if (const auto named = std::ranges::find(kWeekdays, lower(word), [](std::string_view d) { return lower(d); }); day.empty() && named != kWeekdays.end())
                        day = std::string(*named); // (as the weekday's value is: Friday)
                    else time += (time.empty() ? "" : " ") + word;
                Field dayBox = *when, timeBox = *when;
                dayBox.name = "Day of the week"_text;
                dayBox.label = "Weekday"_text;
                dayBox.value = day;
                dayBox.hint = "The same day"_hint;
                dayBox.context = "day";
                timeBox.name = "Time of day"_text;
                timeBox.label = "Time of day"_text;
                timeBox.value = time;
                timeBox.hint = "The same time"_hint;
                timeBox.context = "time";
                const bool weekdays = !names(Pick::TimeWords, "day").empty(); // (the game knows them: else the same day is all there is)
                const auto at = out.erase(when);
                out.insert(at, timeBox);
                if (weekdays || !day.empty()) out.insert(std::ranges::find(out, std::string_view("Time of day"), &Field::name), dayBox);
            }
        if (row.kind == Kind::Time) // (how long: a number, then what it counts, minutes, hours or days; with days and parts of the day, days)
            if (const auto lasts = std::ranges::find(out, std::string_view("How long"), &Field::name); lasts != out.end()) {
                const auto w = rawWords(lasts->value);
                std::string unit = w.size() > 1 ? lower(w[1]) : std::string("minutes");
                if (!unit.ends_with('s')) unit += 's'; // (1 hour: hours)
                const bool days = lower(setting("time").value_or("none")) == "days";
                lasts->value = w.empty() ? std::string{} : w[0];
                if (days) lasts->label = "How long (days)"_text;
                out.insert(lasts + 1, {.name = "Unit"_text, .value = days ? std::string("days") : unit, .label = "Counted in"_text, .box = Box::TimeUnit, .hidden = days || w.empty()});
            }
        if (row.kind == Kind::Screen && lower(firstWord(t)) == "open") { // with NAME [and close with NAME]: what it plays this time, as it opens and closes
            const auto more = std::ranges::find(out, std::string("More"), &Field::name);
            const auto w = more != out.end() ? rawWords(more->value) : std::vector<std::string>{};
            std::size_t i = 0;
            std::string opening, closing;
            if (i + 1 < w.size() && lower(w[i]) == "with") opening = w[i + 1], i += 2;
            if (i + 3 < w.size() && lower(w[i]) == "and" && lower(w[i + 1]) == "close" && lower(w[i + 2]) == "with") closing = w[i + 3], i += 4;
            if (more != out.end() && i == w.size()) out.erase(more);
            else if (more != out.end()) more->value = joined(std::vector(w.begin() + static_cast<std::ptrdiff_t>(i), w.end()));
            std::string ownOpening, ownClosing; // (without them: the screen's own)
            if (const auto s = std::ranges::find_if(screens_, [&](const Screen& x) { return sameName(x.name, fieldValue(out, "Screen")); }); s != screens_.end()) {
                const Part own = part(*s, s->line);
                ownOpening = own.option("animation_on_open"), ownClosing = own.option("animation_on_close");
            }
            out.push_back({.name = "Animation on open"_text, .value = opening, .box = Box::Animation, .hint = ownOpening.empty() ? std::string("None"_hint) : ownOpening,
                           .suggest = "none"_suggest, .group = "Animations"_group});
            out.push_back({.name = "Animation on close"_text, .value = closing, .box = Box::Animation, .hint = ownClosing.empty() ? std::string("None"_hint) : ownClosing,
                           .suggest = "none"_suggest});
        }
        return out;
    }
    const auto w = rawWords(afterFirst(t));
    std::size_t i = 0;
    auto next = [&] { return i < w.size() ? w[i++] : std::string{}; };
    switch (row.kind) {
    case Kind::Line: {
        std::string speaker, expression, words;
        std::optional<std::string> voice;
        const std::string_view line = withoutVoice(t, &voice);
        std::string_view prefix, said;
        if (spoken(line, &prefix, &said)) {
            std::tie(speaker, expression) = speakerOf(prefix); // (Aiko (happy): or Aiko happy:)
            words = said;
        } else {
            words = unquote(line); // narration
        }
        return {{.name = "Speaker"_text, .value = speaker, .box = Box::Character, .hint = "None (narration)"_hint},
                {.name = "Expression"_text, .value = expression, .box = Box::Expression, .hint = "As it is"_hint, .context = speaker},
                {.name = "Words"_text, .value = words, .label = "Text"_text, .rich = true, .typesOut = true},
                {.name = "Voice"_text, .value = voice.value_or(""), .label = "Voice"_text, .box = Box::Sound, .hint = "None"_hint}};
    }
    case Kind::Visual: { // show WHO [EXPRESSION] [as TYPE] [at PLACE] [key=value...] [with ANIMATION] [and wait] | hide WHO [with ANIMATION] [and wait]
        const bool hide = lower(firstWord(t)) == "hide";
        auto keyword = [&](std::size_t at) { // (what comes after what it shows)
            const std::string word = lower(w[at]);
            return word == "at" || word == "as" || word == "with" || word == "and" || word.contains('=');
        };
        std::string who, expression, place, type, with, waits = "no";
        for (std::size_t n = std::min<std::size_t>(3, w.size()); n >= 1 && who.empty(); --n) { // (a character's name, as the engine reads it: up to three words)
            std::string name;
            for (std::size_t k = 0; k < n; ++k) name += (k ? " " : "") + unquote(w[k]);
            if (isCharacter(name)) who = name, i = n;
        }
        if (who.empty() && i < w.size() && !keyword(i)) who = next();
        std::vector<std::pair<std::string, std::string>> settings; // key=value
        std::vector<std::string> more;
        if (!hide && i < w.size() && !keyword(i)) expression = next();
        while (i < w.size()) {
            const std::string word = lower(w[i]);
            if (!hide && i + 1 < w.size() && word == "at") {
                place = w[i + 1];
                i += 2;
            } else if (!hide && i + 1 < w.size() && word == "as") {
                type = w[i + 1];
                i += 2;
            } else if (word == "with" && i + 1 < w.size()) {
                with = w[i + 1];
                i += 2;
            } else if (word == "and" && i + 1 < w.size() && lower(w[i + 1]) == "wait") {
                waits = "yes";
                i += 2;
            } else if (const auto eq = word.find('='); !hide && eq != npos && eq > 0) {
                settings.emplace_back(word.substr(0, eq), unquote(std::string_view(w[i]).substr(eq + 1)));
                ++i;
            } else {
                more.push_back(next());
            }
        }
        auto setting = [&](std::string_view key) {
            const auto it = std::ranges::find(settings, key, &std::pair<std::string, std::string>::first);
            return it != settings.end() ? it->second : std::string{};
        };
        // Its type: what the line says (as TYPE), else what it shows (a character, an image or a movie; nothing yet: a
        // character), said in grey. One every game has that it is already (show aiko as character) puts it at that type's
        // layer: its Layer box says so.
        const std::string is = visualKindOf(who), usual = !is.empty() ? is : who.empty() ? "character" : "image";
        std::string layer = setting("layer");
        if (!type.empty() && lower(type) == is && layer.empty()) layer = std::to_string(typeLayer(is));
        if (lower(type) == is) type.clear();
        const std::string kind = type.empty() ? usual : lower(type);
        const bool back = !hide && kind == "background";
        const bool character = is == "character" || (who.empty() && kind == "character"), movie = is == "movie" || (who.empty() && kind == "movie");
        if (back) // (a colour fading down to another, as one value: navy-black, its bottom= too)
            if (const std::string bottom = setting("bottom"); !bottom.empty() && !who.contains('-')) who += "-" + bottom;
        // What it shows, by its type: a background's an image, a movie or a colour; a character; an image; a movie; the
        // game's own type's what it takes (a colour too: its colour square; hiding: anything).
        const bool colours = std::ranges::contains(wordsOf(typeAccepts(kind)), std::string_view("colour"));
        const Box box = hide ? Box::Shown : back ? Box::Image : kind == "character" ? Box::Character : kind == "image" ? Box::Still : kind == "movie" ? Box::Movie
                                                                                                    : colours          ? Box::Image
                                                                                                                       : Box::Shown;
        // How it comes, as the chosen character's or visual's own line has it (else where every game has one come, the middle
        // with a character standing at the bottom, at its type's layer, as it looks; a background's place: the part of a
        // picture bigger than the stage that shows).
        const std::string across = comes(who, "at", character), down = comes(who, "y", character);
        auto own = [&](std::string_view key, std::string_view otherwise) {
            std::string v = visualOption(who, key, character);
            return v.empty() ? std::string(otherwise) : kindOf(key == "zoom" ? Box::Zoom : key == "opacity" ? Box::Opacity : key == "volume" ? Box::Volume : Box::Text).percent
                                                        ? percentRead(v)
                                                        : v;
        };
        const std::string flipUsual = parseBool(own("flip", "no")).value_or(false) ? "yes" : "no", loopUsual = parseBool(own("loop", "yes")).value_or(true) ? "yes" : "no";
        const std::string shows = character ? std::string{} : visualOption(who, "shows", false); // (a visual named by a visual line: what it shows; none: its name)
        const bool boxed = !hide && !character && !who.empty() && colourBox(shows.empty() ? who : shows, kind);
        // (the line's own rows first, then the ones its visual's own form has, in that form's order)
        std::vector<Field> out{{.name = "Show or hide"_text, .value = hide ? "hide" : "show", .label = "Action"_text, .box = Box::ShowHide, .choices = "show hide"_choices,
                                .hidden = back}, // (a background stays until the next)
                               {.name = "Story waits"_text, .value = waits, .label = "Story waits for it"_text, .box = Box::YesNo, .choices = "no yes"_choices, .hidden = back},
                               {.name = "Type"_text, .value = type, .box = Box::VisualType, .hint = usual, .hidden = hide},
                               {.name = "Who"_text, .value = who, .label = !hide && kind == "character" ? "Character"_text : "Visual"_text, .box = box, .hint = "None"_hint,
                                .context = hide || kind == "character" ? std::string{} : "type:" + kind}}; // (its list: the visuals named as of its type too)
        if (!hide) {
            const std::string flip = setting("flip"), loop = setting("loop");
            out.insert(out.end(), {{.name = "Expression"_text, .value = expression, .box = Box::Expression, .hint = "As it is"_hint, .context = who, .hidden = !character},
                                   {.name = "Place"_text, .value = place, .label = "Across"_text, .box = Box::Place, .hint = across, .group = "Where"_group}, // (where one comes)
                                   {.name = "Y"_text, .value = setting("y"), .label = "Up and down"_text, .box = Box::Down, .hint = down},
                                   {.name = "Layer"_text, .value = layer, .box = Box::Layer, .hint = own("layer", std::to_string(typeLayer(kind))), .hidden = back}, // (its own, else its type's; changed when needed)
                                   {.name = "Zoom"_text, .value = percentRead(setting("zoom")), .box = Box::Zoom, .hint = own("zoom", "100%"), .hidden = back, .group = "Look"_group},
                                   {.name = "Opacity"_text, .value = percentRead(setting("opacity")), .box = Box::Opacity, .hint = own("opacity", "100%"), .hidden = back},
                                   {.name = "Rotation"_text, .value = setting("rotation"), .label = "Rotation (degrees)"_text, .box = Box::Rotation, .hint = own("rotation", "0"), .hidden = back},
                                   {.name = "Flip"_text, .value = flip.empty() ? flipUsual : parseBool(flip).value_or(false) ? "yes" : "no", .label = "Mirrored"_text, .box = Box::YesNo,
                                    .choices = "no yes"_choices, .hidden = back, .usual = flipUsual},
                                   {.name = "Tint"_text, .value = setting("tint"), .label = "Tint colour"_text, .box = Box::Colour, .hint = own("tint", "None"_hint), .hidden = back},
                                   {.name = "Animation"_text, .value = setting("animation"), .label = "Animation while shown"_text, .box = Box::Animation, .hint = own("animation", "None"_hint),
                                    .suggest = "none"_suggest, .group = "Animations"_group},
                                   {.name = "Animation on show"_text, .value = with, .box = Box::Animation,
                                    .hint = own("animation_on_show", back ? "At once"_hint : "From Settings"_hint), .suggest = "none"_suggest},
                                   {.name = "Loop"_text, .value = loop.empty() ? loopUsual : parseBool(loop).value_or(true) ? "yes" : "no", .label = "Loops"_text, .box = Box::YesNo,
                                    .choices = "yes no"_choices, .hidden = !movie, .group = "Playing"_group, .usual = loopUsual}, // (a movie's)
                                   {.name = "Volume"_text, .value = percentRead(setting("volume")), .box = Box::Volume, .hint = own("volume", "100%"), .hidden = !movie},
                                   {.name = "Width"_text, .value = setting("width"), .box = Box::Number, .hint = own("width", "1"), .hidden = !boxed, .group = "Size"_group}, // (a colour's box's)
                                   {.name = "Height"_text, .value = setting("height"), .box = Box::Number, .hint = own("height", "1"), .hidden = !boxed}});
        } else {
            out.push_back({.name = "Animation on hide"_text, .value = with, .box = Box::Animation, .hint = "From Settings"_hint, .suggest = "none"_suggest, .group = "Animations"_group});
        }
        for (const auto& [key, value] : settings) // (the ones without a box; a background's bottom= is in its Visual box)
            if (key != "y" && key != "zoom" && key != "opacity" && key != "rotation" && key != "flip" && key != "layer" && key != "tint" && key != "animation" &&
                key != "loop" && key != "volume" && key != "width" && key != "height" && !(back && key == "bottom"))
                more.push_back(key + "=" + written(value));
        if (!more.empty()) out.push_back({.name = "More"_text, .value = joined(more), .hidden = true});
        return out;
    }
    case Kind::Sound: { // play NAME [as TYPE] [loop=yes|no] [volume=P] [fade=S] [fade_out=S] | stop NAME|TYPE [fade=S]
        const bool stop = lower(firstWord(t)) == "stop";
        std::string what, type;
        if (i < w.size() && !w[i].contains('=') && lower(w[i]) != "as") what = unquote(next());
        std::vector<std::pair<std::string, std::string>> settings; // key=value
        std::vector<std::string> more;
        while (i < w.size()) {
            const std::string word = lower(w[i]);
            if (!stop && word == "as" && i + 1 < w.size()) {
                type = w[i + 1];
                i += 2;
            } else if (const auto eq = word.find('='); eq != npos && eq > 0) {
                settings.emplace_back(word.substr(0, eq), unquote(std::string_view(w[i]).substr(eq + 1)));
                ++i;
            } else {
                more.push_back(next());
            }
        }
        auto setting = [&](std::string_view key) {
            const auto it = std::ranges::find(settings, key, &std::pair<std::string, std::string>::first);
            return it != settings.end() ? it->second : std::string{};
        };
        auto kept = [&](std::initializer_list<std::string_view> boxed) { // (the settings without a box, as written)
            for (const auto& [key, value] : settings)
                if (!std::ranges::contains(boxed, std::string_view(key))) more.push_back(key + "=" + written(value));
        };
        if (stop) { // (a sound by its name, or all of a type)
            std::vector<Field> out{{.name = "Play or stop"_text, .value = "stop", .label = "Action"_text, .box = Box::PlayStop, .choices = "play stop"_choices},
                                   {.name = "Sound"_text, .value = what, .box = Box::Channel, .hint = "None"_hint},
                                   {.name = "Fade out"_text, .value = setting("fade"), .label = "Fade out (seconds)"_text, .box = Box::Seconds, .hint = "At once"_hint}};
            kept({"fade"});
            if (!more.empty()) out.push_back({.name = "More"_text, .value = joined(more), .hidden = true});
            return out;
        }
        // Its type: what the line says (as TYPE), else what it plays is (a sound named by a sound line: its type; a song:
        // music; else sound), said in grey; how it plays, as the named sound or the type has it (the line's own rows first,
        // then the ones the named sound's own form has, in that form's order).
        const std::string is = soundKindOf(what), usualType = is.empty() ? std::string("sound") : is;
        if (lower(type) == usualType) type.clear();
        const std::string kind = type.empty() ? usualType : lower(type);
        const SoundUsual usual = soundUsual(*this, kind, namedSoundType(what).empty() ? std::string_view{} : std::string_view(what));
        const std::string loop = setting("loop");
        std::vector<Field> out{{.name = "Play or stop"_text, .value = "play", .label = "Action"_text, .box = Box::PlayStop, .choices = "play stop"_choices},
                               {.name = "Type"_text, .value = type, .box = Box::SoundType, .hint = usualType},
                               {.name = "Sound"_text, .value = what, .box = Box::Sound, .hint = "None"_hint, .context = type.empty() ? std::string{} : "type:" + kind},
                               {.name = "Loop"_text, .value = loop.empty() ? usual.loop : parseBool(loop).value_or(false) ? "yes" : "no", .label = "Loops"_text, .box = Box::YesNo,
                                .choices = "yes no"_choices, .group = "Playing"_group, .usual = usual.loop},
                               {.name = "Volume"_text, .value = percentRead(setting("volume")), .box = Box::Volume, .hint = usual.volume},
                               {.name = "Fade in"_text, .value = setting("fade"), .label = "Fade in (seconds)"_text, .box = Box::Seconds, .hint = usual.fade},
                               {.name = "Fade out"_text, .value = setting("fade_out"), .label = "Fade out (seconds)"_text, .box = Box::Seconds, .hint = usual.fadeOut}};
        kept({"loop", "volume", "fade", "fade_out"});
        if (!more.empty()) out.push_back({.name = "More"_text, .value = joined(more), .hidden = true});
        return out;
    }
    case Kind::Choice: { // choice [optional=yes] [repeat=yes] [for N seconds | for N lines] [return=no]
        std::vector<std::string> q(w.begin(), w.end());
        std::string optional = "no", repeats = "no", seconds, lines, returns = "yes";
        while (!q.empty()) {
            const std::string last = lower(q.back());
            const bool forLines = last == "lines" || last == "line", forSeconds = last == "seconds" || last == "second";
            const std::size_t unit = forLines || forSeconds ? 1 : 0;
            if (last.starts_with("optional=")) optional = last.substr(9) == "yes" ? "yes" : "no";
            else if (last.starts_with("repeat=")) repeats = last.substr(7) == "yes" ? "yes" : "no";
            else if (last.starts_with("return=")) returns = last.substr(7) == "no" ? "no" : "yes";
            else if (q.size() >= 2 + unit && lower(q[q.size() - 2 - unit]) == "for" && parseNumber(q[q.size() - 1 - unit])) {
                (forLines ? lines : seconds) = q[q.size() - 1 - unit];
                q.resize(q.size() - 1 - unit); // (and "for", below)
            } else {
                break;
            }
            q.pop_back();
        }
        std::vector<Field> out{{.name = "Can be passed"_text, .value = optional, .label = "Can be clicked past"_text, .box = Box::YesNo, .choices = "no yes"_choices}};
        if (lines.empty()) out.push_back({.name = "Repeats"_text, .value = repeats, .label = "Asked again after an option"_text, .box = Box::YesNo, .choices = "no yes"_choices});
        out.push_back({.name = "Time limit"_text, .value = seconds, .label = "Time limit (seconds)"_text, .box = Box::Seconds, .hint = "None"_hint});
        out.push_back({.name = "Up for lines"_text, .value = lines, .label = "Stays up for (lines)"_text, .box = Box::Count, .hint = "None"_hint});
        if (!lines.empty()) out.push_back({.name = "Comes back"_text, .value = returns, .label = "Goes back after an option"_text, .box = Box::YesNo, .choices = "yes no"_choices});
        if (!q.empty()) out.push_back({.name = "More"_text, .value = joined(q), .hidden = true});
        return out;
    }
    case Kind::Option: {
        const OptionParts o = optionParts(t);
        std::vector<Field> out{{.name = "Text"_text, .value = o.text, .rich = true}};
        addCheckFields(out, "Only when", "Available when"_text, "Always"_hint, o.condition, Box::Variable, variables());
        lovesToCheck(out);
        timesToCheck(out);
        out[1].group = "Availability"_group;
        if (!o.condition.empty()) out.push_back({.name = "Until then"_text, .value = o.locked ? "locked" : "hidden", .box = Box::Visibility, .choices = "hidden locked"_choices});
        if (o.locked) out.push_back({.name = "What it needs"_text, .value = o.note, .label = "Says when locked"_text, .box = Box::Text});
        return out;
    }
    case Kind::If:
    case Kind::Else: {
        std::string_view condition = afterFirst(t);
        if (row.kind == Kind::Else && lower(firstWord(t)) == "else") condition = lower(firstWord(condition)) == "if" ? afterFirst(condition) : std::string_view{};
        if (condition.ends_with(':')) condition.remove_suffix(1);
        std::vector<Field> out;
        addCheckFields(out, "What", "Check"_text, row.kind == Kind::Else ? std::string_view("Otherwise"_hint) : std::string_view{}, condition, Box::Variable, variables());
        lovesToCheck(out);
        timesToCheck(out);
        return out;
    }
    case Kind::Set: { // set X to V (V a sum: score + 1)
        const std::string_view rest = afterFirst(t);
        std::string variable, value;
        if (lower(firstWord(rest)) == "to") { // (no variable yet)
            value = afterFirst(rest);
        } else {
            variable = firstWord(rest);
            const std::string_view v = afterFirst(rest);
            value = lower(firstWord(v)) == "to" ? afterFirst(v) : v;
        }
        std::vector<Field> out{{.name = "Variable"_text, .value = variable, .box = Box::Variable}};
        addSumFields(out, "Value", "Value"_text, "", value, variables());
        return out;
    }
    case Kind::Message: { // a chat's: WHO sends IMAGE, WHO is typing, me deletes it... (me: the player)
        const PhoneLine p = phoneLineOf(t);
        std::vector<Field> out{{.name = "Sent"_text, .value = p.mine ? "by_player" : "to_player", .label = "Sent"_text, .box = Box::Sent,
                                .choices = "to_player by_player"_choices}};
        if (!p.mine) {
            out.push_back({.name = "Who"_text, .value = p.who, .label = "From"_text, .box = Box::Character});
            out.push_back({.name = "Expression"_text, .value = p.expression, .box = Box::Expression, .hint = "As it is"_hint, .context = p.who});
        }
        out.push_back({.name = "Does"_text, .value = p.does, .label = "Action"_text, .box = Box::MessageKind, .choices = "says sends sticker voice typing deletes edits reacts reads"_choices});
        if (p.does == "says" || p.does == "edits" || p.does == "reacts" || p.does == "sends" || p.does == "sticker" || p.does == "voice")
            out.push_back({.name = "Text"_text, .value = p.text, .label = p.does == "reacts" ? "Reaction"_text : p.does == "says" || p.does == "edits" ? "Text"_text : "Caption"_text,
                           .hint = "None"_hint, .rich = p.does != "reacts"});
        if (p.does == "sends" || p.does == "sticker") out.push_back({.name = "Image"_text, .value = p.file, .label = "Visual"_text, .box = Box::Image});
        if (p.does == "voice") out.push_back({.name = "Voice"_text, .value = p.file, .label = "Voice"_text, .box = Box::Sound});
        if (p.does == "typing") out.push_back({.name = "Seconds"_text, .value = p.seconds, .label = "Typing for (seconds)"_text, .box = Box::Seconds, .hint = "2"});
        return out;
    }
    case Kind::Chat: { // chat WHO
        std::string rest(trim(afterFirst(t)));
        if (rest.ends_with(':')) rest.pop_back();
        return {{.name = "Who"_text, .value = rest, .label = "With"_text, .box = Box::Character}};
    }
    case Kind::Call: { // phone call WHO [for N seconds]
        std::string rest(trim(afterFirst(afterFirst(t))));
        if (rest.ends_with(':')) rest.pop_back();
        std::string seconds;
        if (const auto at = lower(rest).rfind(" for "); at != npos) {
            seconds = std::string(firstWord(std::string_view(rest).substr(at + 5)));
            rest = std::string(trim(std::string_view(rest).substr(0, at)));
        }
        return {{.name = "Who"_text, .value = rest, .label = "From"_text, .box = Box::Character},
                {.name = "Rings for"_text, .value = seconds, .label = "Rings for (seconds)"_text, .box = Box::Seconds, .hint = "None"_hint}};
    }
    case Kind::Missed:
        return {};
    default:
        break;
    }
    return {{.name = "Line"_text, .value = std::string(t), .box = Box::Text}};
}

std::string StoryEditor::lineOf(Kind kind, const std::vector<Field>& f) const {
    auto value = [&](std::string_view name) { return fieldValue(f, name); };
    if (const Kind to = phoneKind(value("Phone line")); to != Kind::Other && to != kind) return phoneLines(to).front(); // (another of the phone's lines: as one starts)
    auto with = [](std::string_view word, const std::string& v) { return v.empty() ? std::string{} : std::format(" {}{}{}", word, word.empty() ? "" : " ", v); };
    if (const CommandForm* form = formOf(kind)) {
        if (kind == Kind::Time) { // (its day and time of day: one when, as the line has it)
            std::vector<Field> together = f;
            std::erase_if(together, [](const Field& x) { return x.name == "Day of the week" || x.name == "Time of day"; });
            together.push_back({.name = "When"_text, .value = std::string(trim(value("Day of the week") + " " + value("Time of day")))});
            if (const auto lasts = std::ranges::find(together, std::string_view("How long"), &Field::name); lasts != together.end() && !trim(lasts->value).empty())
                lasts->value = lengthWords(value("How long"), value("Unit")); // (and what it counts: 30 minutes, 1 hour)
            return formLine(*form, together);
        }
        const bool opens = kind == Kind::Screen && (value("What").empty() || value("What") == "open"); // (a screen opened: with what it plays this time)
        if (!opens) return formLine(*form, f);
        return formLine(*form, f) + with("with", value("Animation on open")) + with("and close with", value("Animation on close"));
    }
    switch (kind) {
    case Kind::Line: {
        const std::string speaker = value("Speaker"), expression = value("Expression"), words = value("Words");
        const std::string voice = value("Voice").empty() ? std::string{} : " voice=" + written(value("Voice"));
        const bool voiceLike = withoutVoice(words).size() != trim(words).size(); // (words ending as a voice does: in quotes)
        if (speaker.empty()) { // narration (in quotes when it would read as something else)
            const bool plain = !words.empty() && storyCommandOf(words).empty() && unknownCommand(words).empty() && !spoken(words) && !words.starts_with('#') &&
                               !words.starts_with('"') && !voiceLike;
            return (plain ? words : "\"" + words + "\"") + voice;
        }
        const std::string prefix = expression.empty() ? speaker : isCharacter(speaker) ? speaker + " " + expression : speaker + " (" + expression + ")";
        return std::format("{}: {}{}", prefix, voiceLike ? "\"" + words + "\"" : words, voice);
    }
    case Kind::Visual: {
        const std::string waits = value("Story waits") == "yes" ? " and wait" : "", is = visualKindOf(value("Who"));
        const std::string type = value("Type").empty() ? is : lower(value("Type")); // (none: what it shows is)
        if (value("Show or hide") == "hide") return "hide" + with("", value("Who")) + with("with", value("Animation on hide")) + waits + with("", value("More"));
        auto usualOf = [&](std::string_view name, std::string_view fallback) { // (a choice's default: the visual's own, else every game's)
            const auto field = std::ranges::find(f, name, &Field::name);
            return field != f.end() && !field->usual.empty() ? field->usual : std::string(fallback);
        };
        if (type == "background") // (the background: what it shows, the part of it that shows, what it plays, as a movie how, and how it comes; a visual named as one says so itself)
            return "show" + with("", value("Who")) + (is == "background" ? "" : " as background") + with("at", value("Place")) +
                   (value("Y").empty() ? std::string{} : " y=" + written(value("Y"))) +
                   (value("Animation").empty() ? std::string{} : " animation=" + written(value("Animation"))) +
                   (value("Loop").empty() || value("Loop") == usualOf("Loop", "yes") ? std::string{} : " loop=" + value("Loop")) +
                   (value("Volume").empty() ? std::string{} : " volume=" + percentText(value("Volume"))) + with("", value("More")) + with("with", value("Animation on show"));
        // (its type, when what it shows isn't of it already: the game's own, or one every game has with nothing to show yet;
        // a character's expression, only a character's)
        const bool named = !namedVisualType(value("Who")).empty(); // (a visual named by a visual line: of its type unless the line says another)
        const bool says = !type.empty() && type != is && (value("Who").empty() ? type != "character" : named || !std::ranges::contains(kVisualTypes, type));
        const std::string expression = visualKindOf(value("Who")) == "character" || value("Who").empty() ? value("Expression") : std::string{};
        std::string out = "show" + with("", value("Who")) + with("", expression) + with("as", says ? value("Type") : std::string{}) + with("at", value("Place"));
        const std::string shows = visualOption(value("Who"), "shows", false); // (a colour's box's size: only while it is one)
        const bool boxed = is != "character" && !value("Who").empty() && colourBox(shows.empty() ? value("Who") : shows, type);
        for (const auto& [name, key] : {std::pair{"Y", "y"}, {"Zoom", "zoom"}, {"Opacity", "opacity"}, {"Rotation", "rotation"}, {"Flip", "flip"}, {"Layer", "layer"},
                                        {"Tint", "tint"}, {"Animation", "animation"}, {"Loop", "loop"}, {"Volume", "volume"}, {"Width", "width"}, {"Height", "height"}})
            if (const std::string v = value(name); !v.empty() && !(key == std::string_view("flip") && v == usualOf("Flip", "no")) && !(key == std::string_view("loop") && v == usualOf("Loop", "yes")) &&
                                                   (boxed || (key != std::string_view("width") && key != std::string_view("height")))) {
                const auto field = std::ranges::find(f, std::string_view(name), &Field::name);
                out += std::format(" {}={}", key, written(field != f.end() && kindOf(field->box).percent ? percentText(v) : v));
            }
        return out + with("", value("More")) + with("with", value("Animation on show")) + waits;
    }
    case Kind::Sound: {
        const std::string what = value("Sound").contains(' ') ? "\"" + value("Sound") + "\"" : value("Sound");
        if (value("Play or stop") == "stop")
            return "stop" + with("", what) + (value("Fade out").empty() ? std::string{} : " fade=" + written(value("Fade out"))) + with("", value("More"));
        const std::string is = soundKindOf(value("Sound")), usualType = is.empty() ? std::string("sound") : is;
        std::string out = "play" + with("", what) + (lower(value("Type")) == usualType ? std::string{} : with("as", value("Type"))); // (its type, when what it plays isn't of it)
        const auto loop = std::ranges::find(f, std::string_view("Loop"), &Field::name);
        if (loop != f.end() && !loop->value.empty() && loop->value != loop->usual) out += " loop=" + loop->value;
        for (const auto& [name, key] : {std::pair{"Volume", "volume"}, {"Fade in", "fade"}, {"Fade out", "fade_out"}})
            if (const std::string v = value(name); !v.empty()) out += std::format(" {}={}", key, written(key == std::string_view("volume") ? percentText(v) : v));
        return out + with("", value("More"));
    }
    case Kind::Choice: {
        std::string out = "choice" + with("", value("More"));
        if (value("Can be passed") == "yes") out += " optional=yes";
        if (value("Repeats") == "yes" && value("Up for lines").empty()) out += " repeat=yes";
        if (!value("Up for lines").empty()) out += " for " + value("Up for lines") + (value("Up for lines") == "1" ? " line" : " lines");
        else if (!value("Time limit").empty()) out += " for " + value("Time limit") + (value("Time limit") == "1" ? " second" : " seconds");
        if (value("Comes back") == "no" && !value("Up for lines").empty()) out += " return=no";
        return out;
    }
    case Kind::Option: {
        // Text [(if CONDITION) | (needs CONDITION: what it needs)]; the text in quotes when it would read as
        // more than that.
        const std::string text = value("Text"), when = conditionFrom(f, "Only when", variables()), needs = value("What it needs");
        const bool plain = !text.empty() && !text.starts_with('#') && !text.starts_with('"') && !text.ends_with(')') && !text.ends_with(':');
        std::string out = plain ? text : "\"" + text + "\"";
        if (!when.empty()) out += value("Until then") == "locked" ? " (needs " + when + (needs.empty() ? "" : ": " + needs) + ")" : " (if " + when + ")";
        return out;
    }
    case Kind::If:
    case Kind::Else: {
        const std::string condition = conditionFrom(f, "What", variables());
        if (kind == Kind::Else) return condition.empty() ? "else" : "else if " + condition;
        return "if" + with("", condition);
    }
    case Kind::Set: {
        return "set" + with("", value("Variable")) + with("to", sumFrom(f, "Value", variables()));
    }
    case Kind::Message: {
        const bool mine = value("Sent") == "by_player";
        const std::string does = value("Does").empty() ? "says" : value("Does");
        std::string who = mine ? "me" : value("Who");
        if (!mine && !value("Expression").empty()) who += isCharacter(who) ? " " + value("Expression") : " (" + value("Expression") + ")";
        const std::string text = value("Text"), file = value("Image").empty() ? value("Voice") : value("Image");
        std::string what;
        if (does == "says") what = ": " + text;
        else if (does == "sends" || does == "sticker" || does == "voice")
            what = std::format(" sends {}{}{}", does == "sends" ? "" : does + " ", file, text.empty() ? "" : ": " + text);
        else if (does == "typing") what = " is typing" + (value("Seconds").empty() ? std::string{} : " for " + value("Seconds") + (value("Seconds") == "1" ? " second" : " seconds"));
        else if (does == "deletes" || does == "reads") what = " " + does + " it";
        else if (does == "edits") what = " edits it: " + text;
        else what = " reacts " + text;
        return who + what;
    }
    case Kind::Chat: return "chat" + with("", value("Who"));
    case Kind::Call: {
        const std::string seconds = value("Rings for");
        return "phone call" + with("", value("Who")) +
               (seconds.empty() ? std::string{} : " for " + seconds + (seconds == "1" ? " second" : " seconds"));
    }
    case Kind::Missed:
        return "missed:";
    default:
        break;
    }
    return value("Line").empty() ? "#" : value("Line");
}

std::string StoryEditor::summary(const Row& row) const {
    const auto f = fields(row);
    auto v = [&](std::string_view name) { // (words without what's in braces: {bold}, {pause}...; {name} stays)
        const auto it = std::ranges::find(f, name, &Field::name);
        if (it == f.end()) return std::string{};
        return it->rich ? withoutTags(it->value) : it->value;
    };
    auto with = [&](std::string said, std::string_view field) { // (and the animation it plays as it comes or goes)
        return v(field).empty() ? said : "{}, with {}"_tr(said, v(field));
    };
    if (row.kind == Kind::Camera) return v("Animation now").empty() ? v("Animation") : "with {}"_tr(v("Animation now")); // (what it plays now, else what it keeps playing)
    if (const CommandForm* form = formOf(row.kind)) { // the choice it makes, and what's in its places
        std::string out;
        if (const auto verbs = wordsOf(form->verbs); verbs.size() > 1) {
            const auto labels = wordsOf(form->labels, '|');
            const auto at = static_cast<std::size_t>(std::ranges::find(verbs, std::string_view(v("What"))) - verbs.begin());
            out = at < labels.size() ? std::string(tr(labels[at])) : v("What"); // (in the tools' language)
            if (out == tr(kindName(row.kind))) out.clear();
        }
        if (row.kind == Kind::Time) { // (its day first: "day 2, evening")
            const std::string words = v("What") == "time_passes" ? (v("How long").empty() ? std::string{} : lengthWords(v("How long"), v("Unit")))
                                                                 : std::string(trim(v("Day of the week") + " " + v("Time of day")));
            const std::string day = v("Day").empty() || v("What") == "time_passes" ? std::string{} : std::string("day {}"_tr(v("Day")));
            const std::string when = day.empty() ? words : words.empty() ? day : day + ", " + words;
            return out + (when.empty() || out.empty() ? "" : " ") + when;
        }
        for (const Slot& s : form->slots)
            if ((s.type == Type::Word || s.type == Type::Words || s.type == Type::Text) && appliesTo(s, v("What").empty() ? wordsOf(form->verbs).front() : v("What")))
                if (const std::string value = v(s.name); !value.empty()) out += (out.empty() ? "" : " ") + value;
        return with(std::move(out), "Animation on open");
    }
    switch (row.kind) {
    case Kind::Line:
        if (v("Speaker").empty()) return v("Words");
        return std::format("{}{}: {}", v("Speaker"), v("Expression").empty() ? "" : " (" + v("Expression") + ")", v("Words"));
    case Kind::Visual: { // (its type when the line says it: the background, or the game's own)
        if (v("Show or hide") == "hide") return with(v("Who").empty() ? std::string("Hide"_tr) : "Hide {}"_tr(v("Who")), "Animation on hide");
        const std::string type = lower(v("Type"));
        std::string said = v("Who").empty() ? std::string("Show"_tr) : "Show {}"_tr(v("Who") + (v("Expression").empty() || type == "background" ? "" : " (" + v("Expression") + ")"));
        if (!type.empty() && (type == "background" || !std::ranges::contains(kVisualTypes, type))) said = "{}, as {}"_tr(said, v("Type"));
        if (!v("Place").empty()) said = "{}, at {}"_tr(said, v("Place"));
        return with(said, "Animation on show");
    }
    case Kind::Sound: { // (its type when the line says it)
        if (v("Play or stop") == "stop") return v("Sound").empty() ? std::string("Stop"_tr) : "Stop {}"_tr(v("Sound"));
        const std::string said = v("Sound").empty() ? std::string("Play"_tr) : "Play {}"_tr(v("Sound"));
        return v("Type").empty() ? said : "{}, as {}"_tr(said, v("Type"));
    }
    case Kind::Option:
        return v("Text");
    case Kind::Choice: // (what's set for it, as the script has it)
        return std::string(trim(afterFirst(lineOf(Kind::Choice, f))));
    case Kind::If:
    case Kind::Else: {
        const std::string c = conditionFrom(f, "What", variables());
        return row.kind == Kind::Else && c.empty() ? "Otherwise"_tr() : c;
    }
    case Kind::Set:
        return lineOf(Kind::Set, f);
    // The phone's lines: which of them it is (as its first box says), who it's with and what it says.
    case Kind::Message: return lineOf(Kind::Message, f); // (a chat's: what someone does in it)
    case Kind::Chat:
        return v("Who").empty() ? std::string("Chat"_tr()) : "Chat with {}"_tr(v("Who"));
    case Kind::Call:
        return v("Who").empty() ? std::string("Call"_tr()) : "Call from {}"_tr(v("Who"));
    case Kind::Missed:
        return "If nobody answers"_tr();
    case Kind::Together:
        return "These lines at once"_tr();
    default:
        return f.empty() ? std::string{} : f.front().value;
    }
}

std::vector<StoryEditor::Field> StoryEditor::fields(const Character& c) const {
    auto out = formFields(definitionForm("character"), files_[c.file].lines[c.line]);
    for (Field& f : out)
        if (f.name == "Face") f.context = "still"; // (its face: a still, with eyes and a mouth)
    return out;
}

std::vector<StoryEditor::Field> StoryEditor::fields(std::string_view kind, const Definition& d) const {
    const auto lines = linesOf(d);
    auto out = formFields(definitionForm(kind), lines.front());
    if (kind == "font" && rawWords(afterFirst(lines.front())).size() == 1) { // (font FILE: the file is in the name's place)
        out[1].value = out[0].value;
        out[0].value.clear();
    }
    if (kind == "atlas") // the frames' names under it, and the image the eyes and the mouth are in (and where)
        for (const auto& [part, partLabel] : kAtlasParts) {
            std::string names;
            std::vector<std::pair<std::string, std::string>> settings;
            for (std::size_t i = 1; i < lines.size(); ++i) {
                std::string text = lines[i];
                std::ranges::replace(text, ',', ' ');
                std::ranges::replace(text, ':', ' ');
                if (lower(firstWord(text)) != part) continue;
                names.clear();
                for (const std::string& word : rawWords(afterFirst(text))) {
                    if (const auto eq = word.find('='); eq != npos && eq > 0) settings.emplace_back(lower(word.substr(0, eq)), unquote(word.substr(eq + 1)));
                    else names += (names.empty() ? "" : " ") + word;
                }
            }
            auto setting = [&](std::string_view key) {
                const auto it = std::ranges::find(settings, key, &std::pair<std::string, std::string>::first);
                return it != settings.end() ? it->second : std::string{};
            };
            const std::string n(partLabel);
            static_cast<void>(std::array{"Eyes visual"_text, "Eyes from the left (pixels)"_text, "Eyes from the top (pixels)"_text, "Mouth visual"_text,
                                         "Mouth from the left (pixels)"_text, "Mouth from the top (pixels)"_text}); // (the labels made below, listed)
            out.push_back({.name = n, .value = names, .box = Box::Text});
            if (part == "expressions") continue;
            out.push_back({.name = n + "' image", .value = setting("image"), .label = n + " visual", .box = Box::Image});
            out.push_back({.name = n + "' x", .value = setting("x"), .label = n + " from the left (pixels)", .box = Box::Pixels, .hint = "0"});
            out.push_back({.name = n + "' y", .value = setting("y"), .label = n + " from the top (pixels)", .box = Box::Pixels, .hint = "0"});
        }
    if (kind == "animation") { // how often it plays: once, again and again (loop=yes), or a few times (loop=3)
        const auto w = rawWords(lines.front());
        std::string plays = "once", times;
        for (std::size_t i = 2; i < w.size(); ++i)
            if (const std::string word = lower(w[i]); word.starts_with("loop=")) {
                const std::string v = word.substr(5);
                if (parseBool(v)) plays = *parseBool(v) ? "loop" : "once";
                else if (parseNumber(v)) plays = "times", times = v;
            }
        std::erase_if(out, [](const Field& f) { return f.name == "More"; }); // (those words: these boxes')
        out.push_back({.name = "Plays"_text, .value = plays, .box = Box::Plays, .usual = "once"});
        if (plays == "times") out.push_back({.name = "Times"_text, .value = times, .label = "Number of times"_text, .box = Box::Count, .hint = "2"});
    }
    for (Field& f : out)
        if (f.box == Box::Record) f.context = "atlas";
        else if (kind == "sub-image" && f.box == Box::Image) f.context = "files"; // (cut from an image file)
        else if (kind == "style" && f.box == Box::Image) f.context = "still"; // (a style's: stills)
        else if (kind == "atlas" && f.box == Box::Image) f.context = "files"; // (an atlas's sheet: a picture file)
        else if (kind == "visual" && f.name == "Shows") f.context = "shows"; // (what a visual shows: a picture, a sub-image, a movie or a colour; not another visual)
    if (kind == "control") { // what it does as a list, and when it works as checks
        auto at = std::ranges::find(out, std::string("Action"), &Field::name);
        const std::string does = at->value;
        at = out.erase(at);
        std::vector<Field> actions;
        addActionFields(actions, "Action", "Action"_text, "None"_hint, does, *this);
        at = out.insert(at, actions.begin(), actions.end());
        at = std::ranges::find(out, std::string("Only when"), &Field::name);
        const std::string when = at->value;
        at = out.erase(at);
        std::vector<Field> checks;
        addCheckFields(checks, "Only when", "Works when"_text, "Always"_hint, when, Box::Condition, variables());
        timesToCheck(checks);
        out.insert(at, checks.begin(), checks.end());
    }
    // What empty boxes are as written: an atlas's image, and a gallery image's, ending's, song's, achievement's or
    // tip's title, its name.
    auto namedAfter = [&](std::string_view box, std::string_view by) {
        const auto at = std::ranges::find(out, box, &Field::name), name = std::ranges::find(out, by, &Field::name);
        if (at != out.end() && name != out.end() && !name->value.empty()) at->hint = name->value;
    };
    if (kind == "atlas") namedAfter("Image", "Name");
    // A sound type's choices not written: as the volume it follows has them. A named sound's how it plays: as its type
    // has it; the file it plays, the one named as it.
    auto choice = [&](std::string_view name, std::string_view usual) {
        if (const auto f = std::ranges::find(out, name, &Field::name); f != out.end()) {
            f->value = f->value.empty() ? std::string(usual) : parseBool(f->value).value_or(false) ? "yes" : "no";
            f->choices = "yes no"_choices;
            f->usual = usual;
        }
    };
    if (kind == "sound type") {
        const auto follows = std::ranges::find(out, std::string("Follows"), &Field::name);
        if (follows != out.end() && follows->value.empty()) follows->value = "sound";
        const std::string with = follows != out.end() ? lower(follows->value) : std::string("sound");
        choice("One at a time", with == "sound" ? "no" : "yes");
        choice("Loop", with == "music" ? "yes" : "no");
    }
    if (kind == "sound") {
        const std::string type = lower(fieldValue(out, "Type"));
        const SoundUsual usual = soundUsual(*this, type.empty() ? "sound" : type, {});
        choice("Loop", usual.loop);
        for (Field& f : out)
            if (f.name == "Type") f.hint = "sound";
            else if (f.name == "Sound") f.context = "files"; // (what it plays: a file, not another named sound)
            else if (f.name == "Loop") f.group = "Playing"_group;
            else if (f.name == "Volume") f.hint = usual.volume;
            else if (f.name == "Fade in") f.label = "Fade in (seconds)"_text, f.hint = usual.fade;
            else if (f.name == "Fade out") f.label = "Fade out (seconds)"_text, f.hint = usual.fadeOut;
        namedAfter("Sound", "Name");
    }
    if (const auto colour = std::ranges::find(out, std::string("Colour"), &Field::name); kind == "colour" && colour != out.end())
        colour->context = out.front().value; // (a colour's own: its code isn't called by its name, but what it's like)
    if (std::ranges::contains(kRecordKinds, kind) && !out.empty()) namedAfter("Title", out.front().name);
    return out;
}

std::vector<std::string> StoryEditor::linesOf(std::string_view kind, const std::vector<Field>& fields) const {
    auto value = [&](std::string_view name) { return fieldValue(fields, name); };
    std::vector<Field> filled = fields; // (a control's actions and checks, as one value each)
    if (kind == "control") {
        if (const auto does = std::ranges::find(filled, std::string("Action"), &Field::name); does != filled.end()) does->value = actionsFrom(fields, "Action", variables());
        if (const auto when = std::ranges::find(filled, std::string("Only when"), &Field::name); when != filled.end())
            when->value = conditionFrom(fields, "Only when", variables());
    }
    std::vector<std::string> out{formLine(definitionForm(kind), filled)};
    auto with = [](const std::string& v) { return v.empty() ? v : " " + v; };
    auto said = [&](std::string_view name) { // (a choice not as its default was: written)
        const auto f = std::ranges::find(fields, name, &Field::name);
        return f != fields.end() && !f->value.empty() && f->value != f->usual ? f->value : std::string{};
    };
    if (kind == "sound type") { // type NAME follows=WHICH (always: it says it's a sound type), then what isn't as that volume's type has it
        std::string line = "type " + value("Name") + " follows=" + (value("Follows").empty() ? std::string("sound") : lower(value("Follows")));
        if (const std::string v = said("One at a time"); !v.empty()) line += " one_at_a_time=" + v;
        if (const std::string v = said("Loop"); !v.empty()) line += " loop=" + v;
        if (!value("Volume").empty()) line += " volume=" + percentText(value("Volume"));
        out.front() = line + with(value("More"));
    }
    if (kind == "sound") { // sound NAME, then its type (not sound), what it plays and what isn't as its type has it
        std::string line = "sound " + value("Name");
        if (const std::string type = lower(value("Type")); !type.empty() && type != "sound") line += " type=" + value("Type");
        if (!value("Sound").empty()) line += " plays=" + written(value("Sound"));
        if (const std::string v = said("Loop"); !v.empty()) line += " loop=" + v;
        if (!value("Volume").empty()) line += " volume=" + percentText(value("Volume"));
        if (!value("Fade in").empty()) line += " fade=" + written(value("Fade in"));
        if (!value("Fade out").empty()) line += " fade_out=" + written(value("Fade out"));
        out.front() = line + with(value("More"));
    }
    if (kind == "font" && value("Name").empty()) out.front() = std::format("font \"{}\"", value("File")) + (value("Style").empty() ? "" : " " + value("Style"));
    if (kind == "animation") { // (its line only: the lines under it stay as they are)
        const std::string plays = value("Plays"), times = value("Times");
        if (plays == "loop") out.front() += " loop=yes";
        else if (plays == "times") out.front() += std::format(" loop={}", times.empty() || times == "0" ? "2" : times); // (empty: twice)
    }
    if (kind == "atlas")
        for (const auto& [part, partLabel] : kAtlasParts) {
            const std::string n(partLabel);
            std::string line = std::string(part) + with(value(n));
            for (const auto& [suffix, key] : {std::pair{"' image", "image"}, {"' x", "x"}, {"' y", "y"}})
                if (const std::string v = value(n + suffix); !v.empty()) line += std::format(" {}={}", key, written(v));
            if (line != part) out.push_back(line); // (same_names_as= can give it the names: then only where its image is)
        }
    return out;
}

// (the developer files share these: Forms.hpp)
CommandForm definitionForm(std::string_view kind) {
    if (kind == "character") return {StoryEditor::Kind::Other, "character", "", kCharacterSlots};
    if (kind == "sound type") return {StoryEditor::Kind::Other, "type", "", kSoundTypeSlots}; // (its line starts as a visual type's)
    const auto it = std::ranges::find(kDefinitionKinds, kind, &DefinitionKind::kind);
    return {StoryEditor::Kind::Other, it != std::end(kDefinitionKinds) ? it->kind : kind, "", it != std::end(kDefinitionKinds) ? it->slots : std::span<const Slot>{}};
}

std::string formLine(const CommandForm& f, const std::vector<StoryEditor::Field>& fields) {
    auto value = [&](std::string_view name) { return fieldValue(fields, name); };
    std::string verb = value("What");
    if (verb.empty()) verb = wordsOf(f.verbs).front();
    std::string out = spaced(verb);
    auto wordsAfter = [&](const Slot* from) { // (a word in a later place has a value)
        for (const Slot* s = from + 1; s != f.slots.data() + f.slots.size(); ++s)
            if (appliesTo(*s, verb) && (s->type == Type::Word || s->type == Type::Text) && !value(s->name).empty()) return true;
        return false;
    };
    for (const Slot& s : f.slots) {
        if (!appliesTo(s, verb)) continue;
        const std::string v = value(s.name);
        const std::string first = s.choices.empty() ? std::string{} : std::string(wordsOf(s.choices).front());
        switch (s.type) {
        case Type::Word: // (an empty one in quotes stays, as "", when there's one after it: the others keep their places; one of choices, its
                         // first, as its buttons show it chosen: the weather started again after a stop rains)
            if (!v.empty()) out += " " + (s.key == "quoted" || v.contains(' ') ? "\"" + v + "\"" : v);
            else if (!s.choices.empty()) out += " " + first;
            else if (s.key == "quoted" && wordsAfter(&s)) out += " \"\"";
            break;
        case Type::Words:
            if (!v.empty()) out += " " + v;
            break;
        case Type::Text:
            if (!v.empty()) out += " " + (s.key == "quoted" ? "\"" + v + "\"" : v);
            break;
        case Type::After:
            if (!v.empty()) out += std::format(" {} {}{}", s.key, StoryEditor::kindOf(s.box).percent ? percentText(v) : v, !s.seconds ? "" : v == "1" ? " second" : " seconds");
            break;
        case Type::Flag:
            if (!v.empty() && v != first) out += " " + (s.choices == "no yes" ? std::string(s.key) : spaced(v));
            break;
        case Type::Option:
            if (s.commas) {
                std::string list;
                for (const std::string& w : rawWords(v)) list += (list.empty() ? "" : ",") + w;
                if (v != s.choices) out += std::format(" {}={}", s.key, list.empty() ? std::string("no") : list);
            } else if (!v.empty() && v != first) {
                out += std::format(" {}={}", s.key, written(StoryEditor::kindOf(s.box).percent ? percentText(v) : v));
            }
            break;
        }
    }
    if (const std::string more = value("More"); !more.empty()) out += " " + more; // (kept as it was)
    return out;
}

const std::string& actionGroupList() { // (for the first box's list)
    static const std::string list = [] {
        std::string out;
        for (const ActionGroup& g : kActionGroups) out += std::format("{}{}", out.empty() ? "" : "|", g.word);
        return out;
    }();
    return list;
}

const std::string& actionKindList(std::string_view group) { // (for the second box's list: the group's kinds)
    static std::map<std::string, std::string, std::less<>> lists;
    const auto it = lists.find(group);
    if (it != lists.end()) return it->second;
    std::string out;
    for (const ActionKind& a : actionKinds())
        if (a.group == group && a.verb != "other") out += std::format("{}{}", out.empty() ? "" : "|", kindWord(a));
    return lists.emplace(std::string(group), std::move(out)).first->second;
}

namespace dev {

std::string fieldValue(const std::vector<StoryEditor::Field>& fields, std::string_view name) {
    const auto it = std::ranges::find(fields, name, &StoryEditor::Field::name);
    return it != fields.end() ? std::string(trim(it->value)) : std::string{};
}

const CommandForm* formOf(StoryEditor::Kind kind) {
    const auto it = std::ranges::find(kForms, kind, &CommandForm::kind);
    return it != std::end(kForms) ? &*it : nullptr;
}

std::vector<std::string_view> wordsOf(std::string_view list, char by) {
    std::vector<std::string_view> out;
    for (auto&& w : std::views::split(list, by))
        if (!std::string_view(w.begin(), w.end()).empty()) out.emplace_back(w.begin(), w.end());
    return out;
}

std::string spaced(std::string_view word) {
    std::string out(word);
    std::ranges::replace(out, '_', ' ');
    return out;
}

Parts splitTop(std::string_view text, std::initializer_list<std::string_view> ops) {
    Parts out;
    int depth = 0;
    char quote = 0;
    bool value = false; // a value came last (so a "-" takes it away)
    std::size_t from = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == quote) quote = 0, value = true;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '(' || c == ')') {
            depth += c == '(' ? 1 : -1;
            value = c == ')';
            continue;
        }
        std::string_view found;
        if (depth == 0)
            for (const std::string_view op : ops) {
                const bool word = std::isalpha(static_cast<unsigned char>(op[0]));
                if (word ? i > 0 && text[i - 1] == ' ' && i + op.size() < text.size() && text[i + op.size()] == ' ' && lower(text.substr(i, op.size())) == op
                         : text.substr(i, op.size()) == op && (op != "-" || value))
                    found = op;
            }
        if (!found.empty()) {
            out.parts.emplace_back(trim(text.substr(from, i - from)));
            out.ops.emplace_back(found);
            i += found.size() - 1;
            from = i + 1;
            value = false;
        } else if (isWordChar(c) || c == '.') {
            value = true;
        } else if (c != ' ') {
            value = false;
        }
    }
    out.parts.emplace_back(trim(text.substr(from)));
    return out;
}

std::string_view inside(std::string_view text) { return bracketed(text) ? trim(text.substr(1, text.size() - 2)) : text; }

// A value as a script has it: numbers, yes and no, variables (and sums of them) as they are, anything
// else as text, in quotes (the form's boxes take it without them).
std::string scripted(std::string_view value, const std::vector<std::string>& variables) {
    std::string v(trim(value));
    if (v.empty() || isQuoted(v) || (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') || parseNumber(v)) return v;
    bool known = true; // every word in it is a variable (or one sums use)
    for (std::size_t i = 0; i < v.size() && known;) {
        if (!(letterByte(v[i]) || v[i] == '_')) {
            if (v[i] == '"' || v[i] == '\'') known = false;
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < v.size() && (isWordChar(v[j]) || v[j] == '.')) ++j;
        const std::string word = lower(std::string_view(v).substr(i, j - i));
        known = expressionWords().contains(word) || std::ranges::contains(variables, word) || word.contains('.'); // (persistent.x)
        i = j;
    }
    if (known && Expr::parse(v)) return v;
    return v.contains('"') ? "'" + v + "'" : "\"" + v + "\"";
}

std::string shown(std::string_view value, const std::vector<std::string>& variables) {
    std::string v(trim(value));
    if (v.size() < 2 || !(isQuoted(v) || (v.front() == '\'' && v.back() == '\''))) return v;
    const std::string inner = v.substr(1, v.size() - 2), again = scripted(inner, variables);
    return isQuoted(again) || (again.size() >= 2 && again.front() == '\'') ? inner : v;
}

bool hasField(const std::vector<StoryEditor::Field>& fields, std::string_view name) { return std::ranges::find(fields, name, &StoryEditor::Field::name) != fields.end(); }

void addCheckFields(std::vector<StoryEditor::Field>& out, const std::string& name, std::string_view label, std::string_view hint,
                    std::string_view condition, Box box, const std::vector<std::string>& variables) {
    const auto checks = checksOf(condition);
    for (std::size_t i = 0; i < checks.size(); ++i) {
        const Check& c = checks[i];
        const std::string n = numbered(name, i);
        addValueFields(out, n, i == 0 ? label : std::string_view("What"_text), i == 0 ? hint : "", c.what, box, variables, true);
        if (c.what.empty() && i == 0) return;
        out.push_back({.name = n + " is", .value = c.is, .label = "Is"_text, .choices = std::string(kComparisons), .labels = std::string(kComparisonLabels)});
        if (c.is != "yes" && c.is != "no") addValueFields(out, n + " value", "Value"_text, "", c.value, box, variables);
        out.push_back({.name = n + " also", .value = c.then.empty() ? "nothing" : c.then, .label = "Also"_text, .box = Box::Joiner, .choices = "nothing and or"_choices, .usual = "nothing"});
    }
}

std::string conditionFrom(const std::vector<StoryEditor::Field>& fields, const std::string& name, const std::vector<std::string>& variables) {
    std::vector<Check> checks;
    for (std::size_t i = 0;; ++i) {
        const std::string n = numbered(name, i);
        if (!hasField(fields, n)) break;
        std::string value = valueFrom(fields, n + " value", variables);
        if (value != fieldValue(fields, n + " value as written")) value = inSingleQuotes(value); // (typed: text in single quotes, as a part's "..." option needs; as it was: kept)
        Check c{.what = valueFrom(fields, n, variables, true), .is = fieldValue(fields, n + " is"), .value = std::move(value), .then = fieldValue(fields, n + " also")};
        if (c.is.empty()) c.is = "yes";
        if (c.then == "nothing") c.then.clear();
        if (c.what.empty() && !checks.empty()) { // (one taken out: the one before it is the last)
            checks.back().then.clear();
            break;
        }
        checks.push_back(std::move(c));
        if (checks.back().then.empty()) break;
    }
    if (!checks.empty() && !checks.back().then.empty()) checks.push_back({.what = checks.back().what}); // (And or Or just clicked: another, to change)
    return conditionOf(checks);
}

void addActionFields(std::vector<StoryEditor::Field>& out, const std::string& name, std::string_view label, std::string_view hint,
                     std::string_view actions, const StoryEditor& editor) {
    const std::vector<std::string>& variables = editor.variables();
    auto list = actionsOf(actions);
    list.emplace_back();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string n = numbered(name, i);
        const auto [verb, rest] = list[i].empty() ? std::pair<std::string, std::string>{} : actionParts(list[i]);
        const ActionKind* kind = actionKind(verb);
        const ActionGroup* group = kind ? actionGroup(kind->group) : nullptr;
        out.push_back({.name = n, .value = group ? std::string(group->word) : std::string{}, .label = i == 0 ? std::string(label) : std::string("And then"_text), .box = Box::Action,
                       .hint = i == 0 ? std::string(hint) : std::string("None"_hint)});
        out.push_back({.name = n + " was", .value = verb, .hidden = true});
        if (group && group->box != Box::None)
            out.push_back({.name = n + " how", .value = std::string(kindWord(*kind)), .label = std::string(group->how), .box = group->box});
        if (group && group->word == "variable") { // (the variable, and the value it's set to or the amount: boxes of their own)
            const auto [var, val] = variableParts(rest);
            out.push_back({.name = n + " variable", .value = var, .label = "Variable"_text, .box = Box::Variable});
            out.push_back({.name = n + " value", .value = shown(val, variables), .label = "Value"_text, .box = Box::VariableValue});
        } else if (kind && kind->verb == "play") { // (what it plays, and as which type, as a Sound line has them: boxes of their own; the rest as written)
            const auto words = rawWords(rest);
            std::string what = words.empty() ? std::string{} : unquote(words[0]), type;
            std::vector<std::string> more;
            for (std::size_t k = 1; k < words.size(); ++k)
                if (lower(words[k]) == "as" && k + 1 < words.size()) type = words[++k];
                else more.push_back(words[k]);
            const std::string is = editor.soundKindOf(what), usualType = is.empty() ? std::string("sound") : is;
            if (lower(type) == usualType) type.clear();
            out.push_back({.name = n + " what", .value = what, .label = "Sound"_text, .box = Box::Sound, .hint = "None"_hint});
            out.push_back({.name = n + " type", .value = type, .label = "Type"_text, .box = Box::SoundType, .hint = usualType});
            if (!more.empty()) out.push_back({.name = n + " more", .value = joined(more), .hidden = true});
        } else if (kind && kind->verb == kWindowSizeKind.verb) { // (a width and a height: boxes of their own)
            const auto words = rawWords(rest);
            out.push_back({.name = n + " what", .value = words.empty() ? std::string{} : words[0], .label = "Window width (pixels)"_text, .box = Box::Pixels});
            out.push_back({.name = n + " height", .value = words.size() < 2 ? std::string{} : words[1], .label = "Window height (pixels)"_text, .box = Box::Pixels});
        } else if (kind && !kind->detail.empty()) {
            out.push_back({.name = n + " what", .value = rest, .label = std::string(kind->detail), .box = kind->box, .hint = std::string(kind->hint), .context = std::string(kind->context),
                           .suggest = std::string(kind->box == Box::SettingChoice ? settingOptions(kind->verb) : std::string_view(kind->suggest))});
        }
    }
}

std::string actionsFrom(const std::vector<StoryEditor::Field>& fields, const std::string& name, const std::vector<std::string>& variables) {
    std::string out;
    for (std::size_t i = 0;; ++i) {
        const std::string n = numbered(name, i);
        const auto it = std::ranges::find(fields, n, &StoryEditor::Field::name);
        if (it == fields.end()) break;
        const std::string group(trim(it->value));
        if (group.empty()) continue;
        // (its kind: the group's one picked, else the group's first; what it's about: kept while the kind stays, words for another kind start again)
        const ActionKind* kind = kindInGroup(group, trim(fieldValue(fields, n + " how")));
        if (!kind) kind = firstKindOf(group);
        const std::string verb = kind ? std::string(kind->verb) : std::string{};
        if (verb.empty()) continue;
        std::string action;
        if (group == "variable") { // (its variable and value kept from kind to kind)
            const std::string var(trim(fieldValue(fields, n + " variable"))), val(trim(fieldValue(fields, n + " value")));
            if (var.empty()) action = verb;
            else if (val.empty()) action = verb + " " + var;
            else action = "set " + var + " to " + valueWritten(val, variables);
        } else {
            std::string rest = fieldValue(fields, n + " was") == verb ? std::string(trim(fieldValue(fields, n + " what"))) : std::string{};
            if (kind->verb == "play" && !rest.empty()) { // (its type and the rest, after what it plays)
                if (rest.contains(' ')) rest = "\"" + rest + "\"";
                if (const auto type = std::ranges::find(fields, n + " type", &StoryEditor::Field::name); type != fields.end() && !trim(type->value).empty() &&
                                                                                                    lower(trim(type->value)) != lower(type->hint)) // (not what it plays is already)
                    rest += " as " + std::string(trim(type->value));
                if (const std::string more(trim(fieldValue(fields, n + " more"))); !more.empty()) rest += " " + more;
            } else if (kind->verb == kWindowSizeKind.verb) { // (its width and height)
                const std::string height(trim(fieldValue(fields, n + " height")));
                if (!rest.empty() && !height.empty()) rest += " " + height;
            } else if (percentSetting(verb)) {
                rest = percentText(rest); // (85 -> 85%, as the Library's Settings writes it)
            }
            action = verb == "other" ? rest : rest.empty() ? verb : verb + " " + rest;
        }
        if (!action.empty()) out += (out.empty() ? "" : "; ") + action;
    }
    return out;
}

} // namespace dev

} // namespace vn
