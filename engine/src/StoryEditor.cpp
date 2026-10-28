// The editor: the game's scripts as the developer tools show them (scenes and their rows, characters,
// what the scripts list by name, screens, settings), each row as a form's fields and back to its line;
// every change is written to the files at once (Developer.hpp has the class).
#include "DeveloperShared.hpp"

namespace vn {

namespace {

// Someone speaking, by the parser's rule: up to three words, then a colon and a space.
bool speech(std::string_view t, std::string_view* prefix = nullptr, std::string_view* words = nullptr) {
    if (isQuoted(t)) return false;
    const auto colon = t.find(':');
    if (colon == npos || colon == 0 || !(colon + 1 == t.size() || t[colon + 1] == ' ')) return false;
    const std::string_view p = trim(t.substr(0, colon));
    if (std::ranges::count(p, ' ') + 1 > 3 || p.find_first_of(",!?\"{};") != npos) return false;
    if (prefix) *prefix = p;
    if (words) *words = trim(t.substr(colon + 1));
    return true;
}

// Whether something in `all` (with a `name`) is called `name` (case doesn't matter).
template <class T> bool hasNamed(const std::vector<T>& all, std::string_view name) {
    const std::string low = lower(name);
    return std::ranges::any_of(all, [&](const T& one) { return lower(one.name) == low; });
}

// "Value", "Value 2"...: the i-th of a sum's, a condition's or an action list's fields.
std::string numbered(const std::string& name, std::size_t i) { return i == 0 ? name : std::format("{} {}", name, i + 1); }

// A script file's path by its name, and whether a line includes that file (`name` in lower case).
std::string scriptPath(std::string_view name) { return std::format("scripts/{}.vnscript", name); }

bool includesScript(std::string_view line, const std::string& name) {
    const std::string t = lower(trim(line));
    return t == "include " + name || t == "include " + name + ".vnscript";
}

// A file name that's what scripts call it (19_YOMI.flac; "19. YOMI.flac" is called 19_YOMI).
bool usableFileName(std::string_view file) {
    if (const auto slash = file.rfind('/'); slash != npos) file.remove_prefix(slash + 1); // (a translation's own)
    file = file.substr(0, file.rfind('.'));
    return !file.empty() && scriptName(file) == file;
}

StoryEditor::Kind classify(std::string_view t, StoryEditor::Kind parent) {
    using Kind = StoryEditor::Kind;
    if (t.starts_with('#')) return Kind::Note;
    if (parent == Kind::Choice || parent == Kind::Message) return Kind::Option; // (whatever its words: the lines under them are options)
    const std::string command = storyCommandOf(t);
    if (!command.empty() && startsWithCapital(t)) return Kind::Other; // ("Show me": text, or a command)
    const std::string second = lower(firstWord(afterFirst(t)));
    if (command == "scene") return Kind::Background;
    if ((command == "show" || command == "hide") && second == "screen") return Kind::Screen;
    if (command == "hide" && second == "textbox") return Kind::Textbox;
    if (command == "hide" && second == "ui") return Kind::Other;
    if (command == "show" || command == "hide") return Kind::Character;
    static constexpr std::pair<std::string_view, Kind> kCommandKinds[]{ // (the plain ones: a word, its kind)
        {"choice", Kind::Choice}, {"if", Kind::If},         {"else", Kind::Else},        {"elif", Kind::Else},      {"set", Kind::Set},
        {"add", Kind::Set},       {"subtract", Kind::Set},  {"toggle", Kind::Set},       {"jump", Kind::Jump},      {"call", Kind::Jump},
        {"return", Kind::End},    {"end", Kind::End},       {"music", Kind::Music},      {"sound", Kind::Sound},    {"voice", Kind::Voice},
        {"stop", Kind::Stop},     {"wait", Kind::Wait},     {"move", Kind::Move},        {"shake", Kind::Shake},    {"flash", Kind::Shake},
        {"effect", Kind::Effect}, {"weather", Kind::Weather}, {"camera", Kind::Camera},  {"movie", Kind::Movie},    {"open", Kind::Screen},
        {"notify", Kind::Notify}, {"textbox", Kind::Textbox}, {"new page", Kind::NewPage}, {"together", Kind::Together},
        {"chapter", Kind::Chapter}, {"chart", Kind::Chapter}, {"message", Kind::Message}, {"phone", Kind::Phone},   {"ask", Kind::Ask},
        {"quick save", Kind::Game}, {"fullscreen", Kind::Game}, {"screenshot", Kind::Game}, {"quit", Kind::Game},
    };
    if (const auto it = std::ranges::find(kCommandKinds, std::string_view(command), &std::pair<std::string_view, Kind>::first); it != std::end(kCommandKinds))
        return it->second;
    if ((command == "ending" || command == "achievement" || command == "tip" || command == "unlock") && rawWords(afterFirst(t)).size() <= 1)
        return Kind::Unlock; // (with a title after the name it lists one)
    if (!command.empty()) return Kind::Other;
    return Kind::Line; // someone speaking, or narration
}

constexpr std::string_view kEases = "inout linear in out";
// Values a box often has, that say what they are (plain numbers are typed: no list for those).
constexpr std::string_view kAcross = "-1:the left edge|-0.5|0:the middle|0.5|1:the right edge", kDown = "-1:the bottom|-0.5|0:the middle|0.5|1:the top",
                           kLayers = "-1:behind the others|1:in front of the others|2", kSpeeds = "0.5:slower|0.75|1.5|2:faster";

constexpr std::string_view kStepKindList = "Opacity|Move across|Move up or down|Zoom|Rotation|Colour|Fade to a colour"; // (what an animation's step changes: kStepKinds' labels)
constexpr Slot kJumpSlots[]{{.type = Type::Word, .name = "Scene", .box = Box::Scene}};
constexpr Slot kMusicSlots[]{{.type = Type::Word, .name = "Song", .box = Box::Sound},
                             {.type = Type::After, .name = "Fade in", .key = "fade", .box = Box::Seconds, .hint = "At once", .label = "Fade in (seconds)"},
                             {.type = Type::After, .name = "Volume", .key = "volume", .box = Box::Volume, .hint = "100%"},
                             {.type = Type::Flag, .name = "Once", .key = "once", .box = Box::YesNo, .choices = "no yes", .label = "Play once"}};

constexpr Slot kSoundSlots[]{{.type = Type::Word, .name = "Sound", .box = Box::Sound},
                             {.type = Type::Flag, .name = "Loop", .key = "loop", .box = Box::YesNo, .choices = "no yes"},
                             {.type = Type::After, .name = "Called", .key = "as", .box = Box::Channel, .label = "Name to stop it by"},
                             {.type = Type::After, .name = "Fade in", .key = "fade", .box = Box::Seconds, .hint = "At once", .label = "Fade in (seconds)"},
                             {.type = Type::After, .name = "Volume", .key = "volume", .box = Box::Volume, .hint = "100%"}};

constexpr Slot kVoiceSlots[]{{.type = Type::Word, .name = "Voice", .box = Box::Sound, .label = "Voice file"},
                             {.type = Type::Flag, .name = "Text follows it", .box = Box::Sync, .choices = "usual sync no_sync", .label = "Text in time with it"},
                             {.type = Type::After, .name = "Volume", .key = "volume", .box = Box::Volume, .hint = "100%"}};

constexpr Slot kStopSlots[]{{.type = Type::Word, .name = "Called", .box = Box::Channel, .hint = "All", .only = "stop_sound", .label = "Sound's name"},
                            {.type = Type::After, .name = "Fade out", .key = "fade", .box = Box::Seconds, .hint = "At once", .label = "Fade out (seconds)"}};

constexpr Slot kWaitSlots[]{{.type = Type::Word, .name = "Seconds", .box = Box::Seconds, .hint = "Until a click", .seconds = true}};
constexpr Slot kMoveSlots[]{{.type = Type::Word, .name = "Who", .box = Box::Shown, .label = "Character or image"},
                            {.type = Type::After, .name = "To", .key = "to", .box = Box::Place, .group = "Where to", .label = "Place"},
                            {.type = Type::Option, .name = "Height", .key = "y", .box = Box::Down, .hint = "As it is", .label = "Up or down"},
                            {.type = Type::Option, .name = "Layer", .key = "layer", .box = Box::Layer, .hint = "As it is"},
                            {.type = Type::Option, .name = "Zoom", .key = "zoom", .box = Box::Zoom, .hint = "As it is", .group = "Look"},
                            {.type = Type::Option, .name = "Opacity", .key = "opacity", .box = Box::Opacity, .hint = "As it is"},
                            {.type = Type::Option, .name = "Turn", .key = "rotate", .box = Box::Rotation, .hint = "As it is", .label = "Rotation"},
                            {.type = Type::Option, .name = "Flip", .key = "flip", .box = Box::YesNoSame, .choices = "same yes no", .labels = "As it is|Flipped|Not flipped"},
                            {.type = Type::Option, .name = "Tint", .key = "tint", .box = Box::Colour, .hint = "As it is"},
                            {.type = Type::After, .name = "Over", .key = "over", .box = Box::Seconds, .hint = "From Settings", .seconds = true, .group = "Movement", .label = "Duration (seconds)"},
                            {.type = Type::Option, .name = "Ease", .key = "ease", .box = Box::Easing, .choices = kEases, .label = "Easing"},
                            {.type = Type::Flag, .name = "The story waits", .key = "and wait", .box = Box::YesNo, .choices = "no yes", .label = "Story waits for it"}};

constexpr Slot kShakeSlots[]{{.type = Type::Word, .name = "Colour", .box = Box::Colour, .hint = "From Settings", .only = "flash", .label = "Flash colour"},
                             {.type = Type::Option, .name = "Strength", .key = "strength", .box = Box::Pixels, .hint = "From Settings", .only = "shake"},
                             {.type = Type::After, .name = "For", .key = "for", .box = Box::Seconds, .hint = "From Settings", .seconds = true, .label = "Duration (seconds)"}};

constexpr Slot kEffectSlots[]{{.type = Type::Words, .name = "Effects", .box = Box::Effects, .choices = "grayscale sepia invert blur pixelate noise glitch none"},
                              {.type = Type::Option, .name = "Amount", .key = "amount", .box = Box::Percent, .hint = "100%", .label = "Strength"},
                              {.type = Type::After, .name = "Fade", .key = "fade", .box = Box::Seconds, .hint = "At once", .label = "Fade in (seconds)"},
                              {.type = Type::After, .name = "Just for", .key = "for", .box = Box::Seconds, .hint = "Until turned off", .seconds = true, .label = "Duration (seconds)"}};

constexpr Slot kWeatherSlots[]{{.type = Type::Word, .name = "Weather", .box = Box::WeatherKind, .choices = "rain snow petals leaves embers", .label = "Kind"},
                               {.type = Type::Option, .name = "Amount", .key = "amount", .box = Box::Percent, .hint = "100%", .group = "Look"},
                               {.type = Type::Option, .name = "Speed", .key = "speed", .box = Box::Speed, .hint = "1"},
                               {.type = Type::Option, .name = "Wind", .key = "wind", .box = Box::Wind, .hint = "None"},
                               {.type = Type::Option, .name = "Colour", .key = "color", .box = Box::Colour, .label = "Tint"},
                               {.type = Type::Option, .name = "Image", .key = "image", .box = Box::Picture},
                               {.type = Type::Option, .name = "Behind people", .key = "behind", .box = Box::YesNo, .choices = "no yes", .label = "Behind characters"},
                               {.type = Type::After, .name = "Fade in", .key = "fade", .box = Box::Seconds, .hint = "At once", .label = "Fade in (seconds)"}};

constexpr Slot kCameraSlots[]{{.type = Type::Flag, .name = "Back to normal", .key = "reset", .box = Box::YesNo, .choices = "no yes", .label = "Reset"},
                              {.type = Type::Option, .name = "Zoom", .key = "zoom", .box = Box::Zoom, .group = "View"},
                              {.type = Type::Option, .name = "Across", .key = "x", .box = Box::Across, .label = "Looks at (across)"},
                              {.type = Type::Option, .name = "Height", .key = "y", .box = Box::Down, .label = "Looks at (up or down)"},
                              {.type = Type::Option, .name = "Turn", .key = "rotate", .box = Box::Rotation, .label = "Rotation"},
                              {.type = Type::After, .name = "Over", .key = "over", .box = Box::Seconds, .hint = "At once", .seconds = true, .group = "Movement", .label = "Duration (seconds)"},
                              {.type = Type::Option, .name = "Ease", .key = "ease", .box = Box::Easing, .choices = kEases, .label = "Easing"},
                              {.type = Type::Flag, .name = "The story waits", .key = "and wait", .box = Box::YesNo, .choices = "no yes", .label = "Story waits for it"}};

constexpr Slot kMovieSlots[]{{.type = Type::Word, .name = "Movie", .box = Box::Movie},
                             {.type = Type::Flag, .name = "Loop", .key = "loop", .box = Box::YesNo, .choices = "no yes"},
                             {.type = Type::Option, .name = "Can be skipped", .key = "skip", .box = Box::YesNo, .choices = "yes no", .label = "Skippable"},
                             {.type = Type::After, .name = "Volume", .key = "volume", .box = Box::Volume, .hint = "100%"}};

constexpr Slot kScreenSlots[]{{.type = Type::Word, .name = "Screen", .box = Box::Screen}};
constexpr Slot kNotifySlots[]{{.type = Type::Text, .name = "Message", .rich = true}};
constexpr Slot kTextboxSlots[]{{.type = Type::Word, .name = "Screen", .box = Box::Screen, .hint = "say", .only = "textbox", .label = "Textbox screen"},
                               {.type = Type::Word, .name = "Screen", .box = Box::Screen, .hint = "All", .only = "hide_textbox", .label = "Textbox screen"}};

constexpr Slot kChapterSlots[]{{.type = Type::Text, .name = "Title", .key = "quoted", .box = Box::Text}};
constexpr Slot kUnlockSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Record}};
constexpr Slot kAskSlots[]{{.type = Type::Word, .name = "Variable", .box = Box::Variable, .label = "Answer goes in"},
                           {.type = Type::Text, .name = "Question", .key = "quoted", .box = Box::Text},
                           {.type = Type::Option, .name = "To start with", .key = "default", .box = Box::Text, .hint = "None", .label = "Default answer"},
                           {.type = Type::Option, .name = "Longest", .key = "max", .box = Box::Count, .hint = "None", .label = "Most letters"}};

constexpr CommandForm kForms[]{
    {StoryEditor::Kind::Jump, "jump call", "Jump|Call (and come back)", kJumpSlots},
    {StoryEditor::Kind::End, "end return", "End of the story|Back to the call", {}},
    {StoryEditor::Kind::Music, "music", "", kMusicSlots},
    {StoryEditor::Kind::Sound, "sound", "", kSoundSlots},
    {StoryEditor::Kind::Voice, "voice", "", kVoiceSlots},
    {StoryEditor::Kind::Stop, "stop_music stop_sound stop_voice stop_weather", "Music|Sound|Voice|Weather", kStopSlots, "Stops"},
    {StoryEditor::Kind::Wait, "wait", "", kWaitSlots},
    {StoryEditor::Kind::Move, "move", "", kMoveSlots},
    {StoryEditor::Kind::Shake, "shake flash", "Shake|Flash", kShakeSlots, "Effect"},
    {StoryEditor::Kind::Effect, "effect", "", kEffectSlots},
    {StoryEditor::Kind::Weather, "weather", "", kWeatherSlots},
    {StoryEditor::Kind::Camera, "camera", "", kCameraSlots},
    {StoryEditor::Kind::Movie, "movie", "", kMovieSlots},
    {StoryEditor::Kind::Screen, "open show_screen hide_screen", "Open (the story waits)|Show|Hide", kScreenSlots, "Action"},
    {StoryEditor::Kind::Notify, "notify", "", kNotifySlots},
    {StoryEditor::Kind::Textbox, "textbox hide_textbox", "Use|Clear", kTextboxSlots, "Action"},
    {StoryEditor::Kind::NewPage, "new_page", "", {}},
    {StoryEditor::Kind::Together, "together", "", {}},
    {StoryEditor::Kind::Chapter, "chapter chart", "Chapter|On the flowchart", kChapterSlots},
    {StoryEditor::Kind::Unlock, "ending achievement tip unlock", "Ending|Achievement|Tip|Gallery", kUnlockSlots, "Unlocks"},
    {StoryEditor::Kind::Phone, "phone_off phone_on", "Take it away|Give it back", {}, "Action"},
    {StoryEditor::Kind::Ask, "ask", "", kAskSlots},
    {StoryEditor::Kind::Game, "quick_save fullscreen screenshot quit", "Quick save|Fullscreen on or off|Screenshot|Quit the game", {}, "Action"},
};

// Characters' and other definitions' forms (the same parts as commands' forms; the line starts with
// the kind's word).
constexpr Slot kCharacterSlots[]{
    {.type = Type::Word, .name = "Name", .box = Box::Name},
    {.type = Type::Option, .name = "Image", .key = "image", .box = Box::Base, .label = "Image name"}, // (empty: the name, in lower case: see showConfigValues)
    {.type = Type::Option, .name = "Colour", .key = "color", .box = Box::Colour, .hint = "Default", .label = "Name colour"},
    {.type = Type::Option, .name = "Words' colour", .key = "text_color", .box = Box::Colour, .hint = "Default", .label = "Text colour"},
    {.type = Type::Option, .name = "Lines appear", .key = "reveal", .box = Box::Reveal, .choices = "usual letter word line", .group = "Lines", .label = "Text appears"},
    {.type = Type::Option, .name = "Textbox", .key = "textbox", .box = Box::Screen, .hint = "say"},
    {.type = Type::Option, .name = "Voices", .key = "voices", .box = Box::VoiceFolder, .hint = "None",
     .suggest = "yes:audio/voices/ and its name|no", .group = "Voice", .label = "Voice files"},
    {.type = Type::Option, .name = "Made-up voice", .key = "babble", .box = Box::Babble, .hint = "None"},
    {.type = Type::Option, .name = "Its speed", .key = "babble_speed", .box = Box::Speed, .hint = "From Settings", .label = "Made-up voice speed"},
    {.type = Type::Option, .name = "Typing sound", .key = "blip", .box = Box::Sound, .hint = "None"},
    {.type = Type::Option, .name = "Blinks", .key = "blink", .box = Box::YesNoSettings, .choices = "usual yes no", .group = "Blinking and talking"},
    {.type = Type::Option, .name = "Blinks every", .key = "blink_every", .box = Box::Seconds, .hint = "From Settings", .label = "Seconds between blinks"},
    {.type = Type::Option, .name = "Mouth moves", .key = "talk", .box = Box::Talk, .choices = "usual yes voice no"},
    {.type = Type::Option, .name = "Mouth's speed", .key = "talk_speed", .box = Box::PerSecond, .hint = "From Settings", .label = "Mouth speed"},
    {.type = Type::Option, .name = "Face", .key = "face", .box = Box::Picture, .hint = "None", .suggest = "yes:its image's name and _face|no",
     .group = "Face", .label = "Image"},
    {.type = Type::Option, .name = "Face's side", .key = "face_side", .box = Box::Side, .choices = "left right", .label = "Side"},
    {.type = Type::Option, .name = "Face copies", .key = "face_sync", .box = Box::FaceCopies, .choices = "expression blink talk", .commas = true, .label = "Copies from stage"},
    {.type = Type::Option, .name = "Face flipped", .key = "face_flip", .box = Box::YesNo, .choices = "no yes", .label = "Flipped"},
    {.type = Type::Option, .name = "Face blinks", .key = "face_blink", .box = Box::YesNoSame, .choices = "same yes no", .labels = "As on stage|Yes|No", .label = "Blinks"},
    {.type = Type::Option, .name = "Face's mouth", .key = "face_talk", .box = Box::FaceTalk, .choices = "same yes voice no", .label = "Mouth"}};

CommandForm definitionForm(std::string_view kind) {
    if (kind == "character") return {StoryEditor::Kind::Other, "character", "", kCharacterSlots};
    const auto it = std::ranges::find(kDefinitionKinds, kind, &DefinitionKind::kind);
    return {StoryEditor::Kind::Other, it != std::end(kDefinitionKinds) ? it->kind : kind, "", it != std::end(kDefinitionKinds) ? it->slots : std::span<const Slot>{}};
}

constexpr std::pair<std::string_view, std::string_view> kAtlasParts[]{{"expressions", "Expressions"}, {"eyes", "Eyes"}, {"mouth", "Mouth"}}; // (each, and what the form calls it)

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
    if (wordsOf(f.verbs).size() > 1) out.push_back({.name = "What", .value = verb, .label = std::string(f.what), .choices = std::string(f.verbs), .labels = std::string(f.labels)});
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
        if (!took) more.push_back(w[i++]);
    }
    for (const Slot& s : f.slots) {
        if (!appliesTo(s, verb)) continue;
        const auto v = std::ranges::find(values, s.name, &std::pair<std::string_view, std::string>::first);
        std::string value = v != values.end() ? v->second : std::string{};
        if (s.commas) { // (not written, or yes: all of them; no: none)
            std::ranges::replace(value, ',', ' ');
            const std::string low = lower(trim(value));
            value = low.empty() || low == "yes" ? std::string(s.choices) : low == "no" ? std::string{} : low;
        } else if (value.empty() && !s.choices.empty() && !(s.type == Type::Words)) {
            value = wordsOf(s.choices).front(); // (the first: none written)
        }
        if (StoryEditor::kindOf(s.box).percent) value = percentText(value);
        out.push_back({.name = std::string(s.name), .value = value, .box = s.box, .choices = std::string(s.choices), .hint = std::string(s.hint), .labels = std::string(s.labels),
                       .multi = (s.type == Type::Words && !s.choices.empty()) || s.commas || s.box == Box::Key, // (a control's keys: several)
                       .context = s.box == Box::Record ? verb : std::string{},
                       .suggest = std::string(s.suggest), .rich = s.rich, .group = std::string(s.group)});
        if (!s.label.empty()) out.back().label = std::string(s.label);
    }
    if (!more.empty()) out.push_back({.name = "More", .value = joined(more), .hidden = true});
    return out;
}

const std::string& actionGroupList(); // (lists made from tables further down: what an action is about, and the kinds of each)
const std::string& actionKindList(std::string_view group);
const std::string& languageList();
} // namespace

// The kinds of box (see StoryEditor::Box), each once: every row of that kind lists, suggests and says the same.
const StoryEditor::BoxKind& StoryEditor::kindOf(Box box) {
    static const BoxKind kinds[]{
        {.box = Box::None},
        {.box = Box::Words, .rich = true, .wide = true},
        {.box = Box::Text, .wide = true},
        {.box = Box::Name},
        {.box = Box::Picture, .pick = Pick::PicturesOrColors, .wheel = true, .pictures = true, .wide = true}, // (a picture, or a colour: the square and the wheel, the pictures under it)
        {.box = Box::PictureFile, .pick = Pick::PictureFiles, .pictures = true, .wide = true},
        {.box = Box::Colour, .pick = Pick::Colors, .wheel = true},
        {.box = Box::Sound, .pick = Pick::Sounds, .listen = true, .wide = true},
        {.box = Box::Movie, .pick = Pick::Movies, .wide = true},
        {.box = Box::FontFile, .pick = Pick::Fonts, .wide = true},
        {.box = Box::Font, .pick = Pick::FontNames, .wide = true},
        {.box = Box::Style, .pick = Pick::Styles},
        {.box = Box::Character, .pick = Pick::Characters},
        {.box = Box::Shown, .pick = Pick::Shown, .pictures = true, .wide = true},
        {.box = Box::Expression, .pick = Pick::Expressions, .pictures = true},
        {.box = Box::Scene, .pick = Pick::Scenes},
        {.box = Box::Screen, .pick = Pick::Screens},
        {.box = Box::Variable, .pick = Pick::VariableNames},
        {.box = Box::Setting, .pick = Pick::Settings},
        {.box = Box::Animation, .pick = Pick::Animations},
        {.box = Box::Key, .pick = Pick::Keys},
        {.box = Box::Place, .pick = Pick::Places, .suggest = kAcross},
        {.box = Box::Channel, .pick = Pick::Channels},
        {.box = Box::Record, .pick = Pick::Records, .wide = true},
        {.box = Box::VoiceFolder, .pick = Pick::VoiceFolders, .wide = true},
        {.box = Box::Control, .pick = Pick::Controls},
        {.box = Box::Base, .pick = Pick::Bases, .wide = true},
        {.box = Box::Language, .suggest = languageList(), .wide = true},
        {.box = Box::Across, .suggest = kAcross},
        {.box = Box::Down, .suggest = kDown},
        {.box = Box::Layer, .suggest = kLayers},
        {.box = Box::Speed, .suggest = kSpeeds},
        {.box = Box::Zoom, .suggest = "50%:half its size|100%:its size|150%|200%:twice its size", .percent = true},
        {.box = Box::Opacity, .suggest = "0%:unseen|25%|50%|75%|100%:fully seen", .percent = true},
        {.box = Box::Volume, .suggest = "0%:silent|50%|100%", .percent = true},
        {.box = Box::Percent, .suggest = "50%:half|100%:as it is|150%|200%:double", .percent = true},
        {.box = Box::Rotation, .suggest = "-90|-45|0:straight|45|90|180"},
        {.box = Box::Seconds, .suggest = "0.25|0.5|1|2"},
        {.box = Box::Pixels},
        {.box = Box::Count},
        {.box = Box::Number},
        {.box = Box::PerSecond},
        {.box = Box::LineSpacing, .suggest = "1:single|1.2|1.5:one and a half|2:double"},
        {.box = Box::TextSpeed, .suggest = "0:at once|10:slow|25|45|80:fast"},
        {.box = Box::Pause, .suggest = "click:until a click|0.5:half a second|1:a second|2:two seconds"},
        {.box = Box::Wind, .suggest = "-1:to the left|-0.5|0.5|1:to the right"},
        {.box = Box::WidthHeight, .suggest = "1280 720|1920 1080|1600 900|2560 1440|1024 768|800 600"},
        {.box = Box::Point, .suggest = "0 0|8 8|16 16"},
        {.box = Box::FontStyle, .suggest = "Regular|Bold|Italic|Bold Italic"},
        {.box = Box::Babble, .suggest = "high|medium|low|deep|no"},
        {.box = Box::VariableValue, .suggest = "yes|no|0|1"},
        {.box = Box::Action, .suggest = actionGroupList()},
        {.box = Box::ScreenAction, .suggest = actionKindList("screen")},
        {.box = Box::VariableAction, .suggest = actionKindList("variable")},
        {.box = Box::StoryAction, .suggest = actionKindList("story")},
        {.box = Box::SoundAction, .suggest = actionKindList("sound")},
        {.box = Box::SaveAction, .suggest = actionKindList("saves")},
        {.box = Box::SettingAction, .suggest = actionKindList("settings")},
        {.box = Box::GalleryAction, .suggest = actionKindList("gallery")},
        {.box = Box::ContainerFit, .choices = "box parts", .labels = "Fills its box|As big as its biggest part"},
        {.box = Box::Change, .suggest = kStepKindList},
        {.box = Box::Playing, .suggest = "music:the music|sound:looping sounds|voice:the voice"},
        {.box = Box::KeySlot, .suggest = "1:the first|2:the second|pad:its controller button"},
        {.box = Box::SaveMode, .suggest = "save:saving|load:loading"},
        {.box = Box::TimedTransition, .suggest = "dissolve 0.25|fade 0.5|wipe 0.5|slide 0.5|none:at once"},
        {.box = Box::YesNo, .choices = "yes no", .labels = "Yes|No"},
        {.box = Box::YesNoStyle, .choices = "inherit yes no", .labels = "From its base style|Yes|No"},
        {.box = Box::YesNoSettings, .choices = "usual yes no", .labels = "From Settings|Yes|No"},
        {.box = Box::YesNoSame, .choices = "same yes no", .labels = "As it is|Yes|No"},
        {.box = Box::Easing, .choices = "inout linear in out", .labels = "Smoothly|Evenly|Speeding up|Slowing down"},
        {.box = Box::Condition, .pick = Pick::Conditions},
        {.box = Box::SettingChoice}, // (the list is the setting's own: the Settings tab's box for it, as a list)
        {.box = Box::GameLanguage, .pick = Pick::Languages},
        {.box = Box::Transition, .choices = "usual none fade dissolve wipe slide", .labels = "From Settings|At once|Fade|Dissolve|Wipe|Slide"},
        {.box = Box::Fading, .choices = "none fade", .labels = "At once|Fade"},
        {.box = Box::Fit, .choices = "contain cover stretch original", .labels = "All of it shows|It fills the part|Stretched|Its own size"},
        {.box = Box::Reveal, .choices = "usual letter word line", .labels = "From Settings|A letter at a time|A word|A line"},
        {.box = Box::Talk, .choices = "usual yes voice no", .labels = "From Settings|Yes|With a voice|No"},
        {.box = Box::ImageFilter, .choices = "smooth pixel", .labels = "Smooth|Sharp pixels"},
        {.box = Box::SkipHold, .choices = "ctrl shift alt none", .labels = "Ctrl|Shift|Alt|None"},
        {.box = Box::WindowFill, .choices = "fit integer stretch fill", .labels = "Fit|Whole pixels|Stretch|Fill"},
        {.box = Box::Mouths, .choices = "always voice never", .labels = "Always|With a voice|Never"},
        {.box = Box::Sync, .choices = "usual sync no_sync", .labels = "From Settings|Yes|No"},
        {.box = Box::Side, .choices = "left right", .labels = "Left|Right"},
        {.box = Box::FaceCopies, .choices = "expression blink talk", .labels = "Expression|Blinks|Mouth"},
        {.box = Box::FaceTalk, .choices = "same yes voice no", .labels = "As on stage|Moves|With a voice|Still"},
        {.box = Box::Align, .choices = "inherit left center right", .labels = "From its base style|Left|Centre|Right"},
        {.box = Box::VAlign, .choices = "inherit top center bottom", .labels = "From its base style|Top|Centre|Bottom"},
        {.box = Box::Operation, .choices = "nothing + - * /", .labels = "Nothing more|Plus|Minus|Times|Divided by"},
        {.box = Box::Joiner, .choices = "nothing and or", .labels = "Nothing more|And|Or"},
        {.box = Box::ShowHide, .choices = "show hide", .labels = "Show|Hide"},
        {.box = Box::Visibility, .choices = "hidden locked", .labels = "Hidden|Locked"},
        {.box = Box::SetVerb, .choices = "set add subtract toggle", .labels = "Set to|Add|Subtract|Toggle"},
        {.box = Box::Sent, .choices = "to_player by_player", .labels = "To the player|By the player"},
        {.box = Box::Anchor, .choices = "center top bottom", .labels = "The middle|The top|The bottom"},
        {.box = Box::Repeat, .choices = "no across down both", .labels = "No|Across|Down|Both ways"},
        {.box = Box::Scroll, .choices = "no left right up down", .labels = "No|Left|Right|Up|Down"},
        {.box = Box::Effects, .choices = "grayscale sepia invert blur pixelate noise glitch none", .labels = "Grayscale|Sepia|Invert|Blur|Pixelate|Noise|Glitch|None"},
        {.box = Box::WeatherKind, .choices = "rain snow petals leaves embers", .labels = "Rain|Snow|Petals|Leaves|Embers"},
        {.box = Box::Edges, .choices = "show cut fade", .labels = "Show what's past them|Cut it off|Fade it out"},
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
    const float percent = v.contains('.') || *n <= 1.0f ? *n * 100.0f : *n; // (a fraction, as it used to be written; else a percent already)
    return toString(number(percent)) + "%";
}

namespace {

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
        case Type::Word: // (an empty one in quotes stays, as "", when there's one after it: the others keep their places)
            if (!v.empty()) out += " " + (s.key == "quoted" || v.contains(' ') ? "\"" + v + "\"" : v);
            else if (s.key == "quoted" && (wordsAfter(&s) || s.name == "Title")) out += " \"\""; // (an ending's, a tip's...: without it, the line would be the story reaching one)
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

// A value as a script has it: numbers, yes and no, variables (and sums of them) as they are, anything
// else as text, in quotes (the form's boxes take it without them).
// What screens' conditions and texts know besides the variables.
constexpr std::string_view kScreenWords[]{"in_game", "has_save", "can_save", "can_rollback", "skip", "auto", "speaking", "choosing", "prompt", "text_complete",
                                          "has_face", "fullscreen", "developer", "thumbnail", "save_mode", "face_side", "language", "languages", "cg", "cg_title", "cg_movie",
                                          "cg_unlocked", "cg_total", "tip", "messages_total", "messages_unread", "has_phone", "contact", "dragging", "time_left"};

const std::set<std::string, std::less<>>& expressionWords() {
    static const std::set<std::string, std::less<>> words = [] {
        std::set<std::string, std::less<>> out{"yes", "no", "true", "false", "and", "or", "not", "is", "at", "least", "most", "more", "less", "fewer",
                                               "greater", "than", "random", "min", "max", "abs", "round", "len", "str", "int", "upper", "lower", "if", "answer", "dropped"};
        for (const std::string_view w : kScreenWords) out.emplace(w);
        return out;
    }();
    return words;
}

// Text in single quotes ("english" -> 'english'), as a part's options and actions need it: they're in double ones.
std::string inSingleQuotes(const std::string& v) { return isQuoted(v) && !v.contains('\'') ? "'" + v.substr(1, v.size() - 2) + "'" : v; }

// A value's fields: a box with what it can be (a variable, yes, no, a number, a random number...), and
// for a random number the lowest and highest it can be. Named `name`, `name from` and `name to`.
constexpr std::string_view kValueSuggest = "random:a random number|yes|no|0|1";

std::optional<std::pair<std::string, std::string>> randomOf(std::string_view term) {
    term = trim(term);
    if (!lower(term).starts_with("random(") || !term.ends_with(')')) return std::nullopt;
    const Parts args = splitTop(term.substr(7, term.size() - 8), {","});
    if (args.parts.size() == 2) return std::pair{args.parts[0], args.parts[1]};
    if (const auto n = parseNumber(args.parts[0]); args.parts.size() == 1 && n) return std::pair{std::string("0"), std::format("{}", *n - 1)};
    return std::nullopt;
}

void addValueFields(std::vector<StoryEditor::Field>& out, const std::string& name, std::string_view label, std::string_view hint, std::string_view term,
                    Box box, const std::vector<std::string>& variables, bool asWritten = false) {
    if (const auto r = randomOf(term)) {
        out.push_back({.name = name, .value = "random", .label = std::string(label), .box = box, .suggest = std::string(kValueSuggest)});
        out.push_back({.name = name + " from", .value = r->first, .label = "From", .box = Box::Count});
        out.push_back({.name = name + " to", .value = r->second, .label = "To", .box = Box::Count});
        return;
    }
    out.push_back({.name = name, .value = asWritten ? std::string(term) : shown(term, variables), .label = std::string(label), .box = box,
                   .hint = std::string(hint), .suggest = std::string(kValueSuggest)});
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
        addValueFields(out, n, i == 0 ? label : "Value", hint, sum->terms[i], Box::Variable, variables);
        out.push_back({.name = n + " then", .value = i < sum->ops.size() ? sum->ops[i] : "nothing", .label = "Then", .box = Box::Operation, .choices = "nothing + - * /"});
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
constexpr std::string_view kComparisonLabels = "yes|no|equal to|not equal to|at least|at most|more than|less than";
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

// Commands with ; between them (outside quotes), and their fields: one for each, then an empty one
// for another. Their names: "NAME", "NAME 2"...; the first is called `label`, the others "And then".
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
    std::string_view word, about;
    Box box;                      // the kinds of this group: their box's kind (None: one kind, with no box for it)
    std::string_view how = "How"; // what the form calls that box
};

constexpr ActionGroup kActionGroups[]{
    {"screen", "a screen", Box::ScreenAction},        {"story", "the story", Box::StoryAction},
    {"variable", "a variable", Box::VariableAction},  {"sound", "music and sounds", Box::SoundAction},
    {"saves", "the saves", Box::SaveAction},          {"gallery", "the gallery", Box::GalleryAction},
    {"settings", "a player's setting", Box::SettingAction, "Setting"},
    {"language", "the language", Box::None},          {"message", "show a message", Box::None},
    {"quit", "quit the game", Box::None},             {"screenshot", "take a screenshot", Box::None},
    {"other", "something else, written as a line", Box::None},
};

struct ActionKind {
    std::string_view group, verb, about, detail;
    Box box; // what comes after its word: its box's kind
    std::string_view hint, suggest, context;
    constexpr ActionKind(std::string_view g, std::string_view v, std::string_view a, std::string_view d = {}, Box b = Box::None, std::string_view h = {},
                         std::string_view s = {}, std::string_view c = {})
        : group(g), verb(v), about(a), detail(d), box(b), hint(h), suggest(s), context(c) {}
};

// Every kind: kActionKinds, with a kind for each player setting (from the Settings tab's rows for new
// players' settings: the same words and options) before the resets.
const std::vector<ActionKind>& actionKinds();

bool percentSetting(std::string_view setting);         // (a player setting the Settings tab writes as a percent: "85%")
std::string_view settingOptions(std::string_view setting); // (what it can go to, as a list: the Settings tab's box for it, each option named)
constexpr ActionKind kActionKinds[]{
    {"screen", "open", "open a screen", "Screen", Box::Screen},
    {"screen", "close", "close this screen"},
    {"screen", "show screen", "show a screen over the others", "Screen", Box::Screen},
    {"screen", "hide screen", "hide a screen", "Screen", Box::Screen},
    {"screen", "answer", "close the screen the story waits for, with an answer", "Answer", Box::Text},
    {"screen", "yes", "say yes (on a question screen)"},
    {"screen", "no", "say no (on a question screen)"},
    {"variable", "set", "set a variable", "Variable and value", Box::Variable},
    {"variable", "add", "add to a variable", "Amount and variable", Box::Text},
    {"variable", "subtract", "take from a variable", "Amount and variable", Box::Text},
    {"variable", "toggle", "turn a variable on or off", "Variable", Box::Variable},
    {"story", "start", "start the story", "From", Box::Scene, "Beginning"},
    {"story", "continue", "go on from the last save"},
    {"story", "jump", "go to a scene", "Scene", Box::Scene},
    {"story", "call", "go to a scene, and come back", "Scene", Box::Scene},
    {"story", "advance", "on to the next line"},
    {"story", "rollback", "back a line"},
    {"story", "skip", "skipping on or off"},
    {"story", "auto", "auto-forward on or off"},
    {"story", "hide ui", "hide the textbox until a click"},
    {"sound", "music", "play music", "Song", Box::Sound},
    {"sound", "sound", "play a sound", "Sound", Box::Sound},
    {"sound", "stop", "stop the music or a sound", "Stops", Box::Playing},
    {"saves", "quick save", "save in the quick slot"},
    {"saves", "quick load", "load the quick save"},
    {"saves", "next page", "the next page of saves"},
    {"saves", "previous page", "the page of saves before"},
    {"saves", "delete save", "delete the save chosen"},
    // (a player's setting: the second box names it, the third says what it goes to; the settings themselves
    // come from the Settings tab's rows, in actionKinds(), before these)
    {"settings", "reset settings", "every setting back to how it started"},
    {"settings", "reset controls", "the keys back to how they started"},
    {"settings", "reset progress", "forget what's kept between games"},
    {"gallery", "next cg", "the next image in the gallery"},
    {"gallery", "previous cg", "the image before in the gallery"},
    {"gallery", "unlock", "unlock an image in the gallery", "Image", Box::Record, "", "", "unlock"},
    {"language", "setting language", "the language", "Language", Box::GameLanguage},
    {"message", "notify", "show a message", "Message", Box::Words},
    {"quit", "quit", "quit the game"},
    {"screenshot", "screenshot", "take a screenshot"},
    {"other", "other", "something else, written as a line", "Line", Box::Text},
};

// The window's size: the one setting with no row of its own in the Settings tab (a width and a height,
// in two boxes).
constexpr ActionKind kWindowSizeKind{"settings", "setting window_size", "The window's width and height, in pixels", "Width", Box::Pixels};

// What the group's box calls a kind: its word, or a setting's name alone ("setting vsync": vsync).
std::string_view kindWord(const ActionKind& a) { return a.verb.starts_with("setting ") ? a.verb.substr(8) : a.verb; }

const std::string& actionGroupList() { // (for the first box's list)
    static const std::string list = [] {
        std::string out;
        for (const ActionGroup& g : kActionGroups) out += std::format("{}{}:{}", out.empty() ? "" : "|", g.word, g.about);
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
        if (a.group == group && a.verb != "other") out += std::format("{}{}:{}", out.empty() ? "" : "|", kindWord(a), a.about);
    return lists.emplace(std::string(group), std::move(out)).first->second;
}

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
    if (best.empty()) return {"other", text};
    return {std::string(best), std::string(trim(std::string_view(text).substr(best.size())))};
}

// A set's value (an add's amount) as the action writes it: a word that isn't a variable is text, in
// quotes (single ones: the actions are in double quotes already): english -> 'english'. And as its box
// shows it: without the quotes, as the Library's boxes do, when it would be written in quotes again.
std::string valueWritten(const std::string& value, const std::vector<std::string>& variables) { return inSingleQuotes(scripted(value, variables)); }

std::string valueShown(const std::string& v, const std::vector<std::string>& variables) { return shown(v, variables); }
// A variable action's words after its kind, as the variable and the value: "x to 1" (set x to 1), "1 to x"
// (add 1 to x), "1 from x" (subtract 1 from x), "x" (toggle x).
std::pair<std::string, std::string> variableParts(const std::string& verb, const std::string& rest) {
    const std::string joint = verb == "subtract" ? " from " : " to ";
    const auto at = verb == "toggle" ? npos : lower(rest).find(joint);
    if (at == npos) return {std::string(trim(rest)), std::string{}};
    const std::string left(trim(std::string_view(rest).substr(0, at))), right(trim(std::string_view(rest).substr(at + joint.size())));
    return verb == "set" ? std::pair{left, right} : std::pair{right, left};
}

// An option's parts, as the parser reads them: Text [(if CONDITION) | (needs CONDITION: what it
// needs)] [-> label].
struct OptionParts {
    std::string text, condition, note, label;
    bool locked = false; // (needs ...): shown, but it can't be picked until then
};

OptionParts optionParts(std::string_view line) {
    OptionParts o;
    std::string_view text = trim(line);
    if (text.ends_with(':') && !isQuoted(text)) text.remove_suffix(1);
    if (const auto arrow = text.rfind("->"); arrow != npos && !isQuoted(text)) {
        o.label = trim(text.substr(arrow + 2));
        text = trim(text.substr(0, arrow));
    }
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

// `line` with each whole-word `from` that `used` allows (by what's before it, in lower case) called `to`.
std::string wordRenamed(const std::string& line, std::string_view from, std::string_view to, const std::function<bool(std::string_view)>& used) {
    const std::string low = lower(line), name = lower(from);
    std::string out;
    std::size_t last = 0;
    for (std::size_t at = low.find(name); at != npos; at = low.find(name, at + name.size())) {
        if (at + name.size() < low.size() && isWordChar(low[at + name.size()])) continue;
        if (!used(std::string_view(low).substr(0, at))) continue;
        out += line.substr(last, at - last);
        out += to;
        last = at + name.size();
    }
    return out + line.substr(last);
}

// `line` with the scene `from` called `to` wherever it's gone to (jump, call, start, ->).
std::string renamed(const std::string& line, std::string_view from, std::string_view to) {
    auto endsWithWord = [](std::string_view s, std::string_view word) {
        return s.ends_with(word) && (s.size() == word.size() || !isWordChar(s[s.size() - word.size() - 1]));
    };
    return wordRenamed(line, from, to, [&](std::string_view before) {
        std::size_t b = before.size();
        while (b > 0 && before[b - 1] == ' ') --b;
        const bool spaced = b < before.size();
        before = before.substr(0, b);
        return before.ends_with("->") || (spaced && (endsWithWord(before, "jump") || endsWithWord(before, "call") || endsWithWord(before, "start")));
    });
}

// `line` with the screen `from` called `to` wherever it's used (open, show screen, textbox=...).
std::string screenRenamed(const std::string& line, std::string_view from, std::string_view to) {
    return wordRenamed(line, from, to, [](std::string_view before) {
        if (!before.empty() && isWordChar(before.back())) return false;
        if (before.ends_with("screen=") || before.ends_with("textbox=")) return true;
        if (!before.ends_with(' ')) return false;
        before = trim(before);
        const auto start = before.find_last_of(" \"");
        const std::string_view word = start == npos ? before : before.substr(start + 1);
        return word == "open" || word == "screen" || word == "textbox";
    });
}

// A config line's setting (in lowercase) and value: config NAME VALUE, or config NAME=VALUE.
std::optional<std::pair<std::string, std::string_view>> configOf(std::string_view line) {
    if (indentOf(line) != 0 || lower(firstWord(line)) != "config") return std::nullopt;
    const std::string_view rest = afterFirst(line);
    std::string_view name = firstWord(rest), value = afterFirst(rest);
    if (const auto eq = name.find('='); eq != npos && eq > 0) {
        value = trim(rest.substr(eq + 1));
        name = name.substr(0, eq);
    }
    return std::pair{lower(name), value};
}

} // namespace

void StoryEditor::load(const std::string& root) {
    const std::size_t version = version_; // (it only goes up: what's made from the scripts is made again)
    clear();
    version_ = version + 1;
    root_ = root;
    auto names = listFiles(root + "scripts/", ".vnscript");
    std::ranges::stable_partition(names, [](const std::string& n) { return lower(n) == "main.vnscript"; });
    for (const auto& name : names) {
        if (name.contains('/')) continue;
        File f{.path = "scripts/" + name, .lines = {}};
        if (const auto text = readTextFile(root + f.path)) {
            f.lines = vn::linesOf(*text);
            if (f.lines.size() > 1 && f.lines.back().empty()) f.lines.pop_back(); // (the file's last line break)
        }
        if (SDL_PathInfo info; SDL_GetPathInfo((root + f.path).c_str(), &info)) f.modified = info.modify_time;
        files_.push_back(std::move(f));
    }
    if (const auto map = readTextFile(root + ".map.txt"))
        for (auto&& part : std::views::split(std::string_view(*map), '\n'))
            if (const auto w = rawWords(std::string_view(part.begin(), part.end())); w.size() == 3)
                if (const auto x = parseNumber(w[1]), y = parseNumber(w[2]); x && y) mapPlaces_.insert_or_assign(lower(w[0]), SDL_FPoint{*x, *y});
    rescan();
}

std::optional<SDL_FPoint> StoryEditor::mapPlace(std::string_view scene) const {
    const auto it = mapPlaces_.find(lower(scene));
    return it != mapPlaces_.end() ? std::optional(it->second) : std::nullopt;
}

void StoryEditor::setMapPlace(const std::string& scene, std::optional<SDL_FPoint> at) {
    if (at) mapPlaces_.insert_or_assign(lower(scene), *at);
    else mapPlaces_.erase(lower(scene));
    saveMapPlaces();
}

void StoryEditor::saveMapPlaces() const {
    std::string text;
    for (const auto& [name, at] : mapPlaces_) text += std::format("{} {} {}\n", name, std::lround(at.x), std::lround(at.y));
    if (text.empty()) SDL_RemovePath((root_ + ".map.txt").c_str());
    else SDL_SaveFile((root_ + ".map.txt").c_str(), text.data(), text.size());
}

void StoryEditor::rescan() {
    scenes_.clear();
    screens_.clear();
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i) {
            const std::string& line = files_[f].lines[i];
            if (indentOf(line) != 0) continue;
            const std::string first = lower(firstWord(line));
            if (first == "label") {
                std::string name(afterFirst(line));
                if (name.ends_with(':')) name.pop_back();
                scenes_.push_back({.file = f, .line = i, .end = blockEnd(f, i), .name = name});
            } else if (first == "screen") {
                screens_.push_back({.file = f, .line = i, .end = blockEnd(f, i), .name = std::string(firstWord(afterFirst(line)))});
            }
        }
}

std::size_t StoryEditor::blockEnd(std::size_t file, std::size_t label) const {
    const auto& lines = files_[file].lines;
    std::size_t end = label + 1;
    while (end < lines.size() && (blank(lines[end]) || indentOf(lines[end]) > 0)) ++end;
    while (end > label + 1 && blank(lines[end - 1])) --end;
    return end;
}

const std::vector<StoryEditor::Row>& StoryEditor::rows(const Scene& scene) const {
    auto& kept = rows_[{scene.file, scene.line}];
    if (kept.first == version_) return kept.second;
    std::vector<Row> out;
    const auto& lines = files_[scene.file].lines;
    const std::size_t end = blockEnd(scene.file, scene.line);
    std::vector<std::pair<int, std::size_t>> open; // the rows the next ones may be inside: indentation, row
    for (std::size_t i = scene.line + 1; i < end; ++i) {
        if (blank(lines[i])) continue;
        const int indent = indentOf(lines[i]);
        while (!open.empty() && indent <= open.back().first) open.pop_back();
        Row row{.line = i, .end = end, .depth = static_cast<int>(open.size()), .kind = Kind::Other, .text = std::string(trim(lines[i]))};
        row.kind = classify(row.text, open.empty() ? Kind::Other : out[open.back().second].kind);
        open.emplace_back(indent, out.size());
        out.push_back(std::move(row));
    }
    for (std::size_t k = 0; k < out.size(); ++k)
        for (std::size_t j = k + 1; j < out.size(); ++j)
            if (out[j].depth <= out[k].depth) {
                out[k].end = out[j].line;
                break;
            }
    kept = {version_, std::move(out)};
    return kept.second;
}

const std::vector<StoryEditor::Character>& StoryEditor::characters() const {
    if (characters_.first == version_) return characters_.second;
    std::vector<Character> out;
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i)
            if (const std::string& line = files_[f].lines[i]; indentOf(line) == 0 && lower(firstWord(line)) == "character")
                if (const auto words = rawWords(afterFirst(line)); !words.empty()) out.push_back({.file = f, .line = i, .name = unquote(words[0])});
    characters_ = {version_, std::move(out)};
    return characters_.second;
}

bool StoryEditor::isCharacter(std::string_view name) const { return hasNamed(characters(), name); }
bool StoryEditor::hasScene(std::string_view name) const { return hasNamed(scenes_, name); }
bool StoryEditor::hasScreen(std::string_view name) const { return hasNamed(screens_, name); }

const std::vector<std::string>& StoryEditor::variables() const {
    if (variables_.first == version_) return variables_.second;
    std::vector<std::string> out;
    for (const File& f : files_)
        for (const std::string& line : f.lines) {
            const std::string verb = lower(firstWord(line));
            const std::string_view rest = afterFirst(line);
            std::string name;
            if (verb == "set" || verb == "toggle" || verb == "ask" || (verb == "variable" && indentOf(line) == 0)) name = lower(firstWord(rest));
            else if (verb == "add" || verb == "subtract")
                if (const auto at = lower(rest).rfind(verb == "add" ? " to " : " from "); at != npos)
                    name = lower(trim(rest.substr(at + (verb == "add" ? 4 : 6))));
            if (const auto eq = name.find_first_of("=+-"); eq != npos) name.resize(eq);
            if (!name.empty() && std::ranges::all_of(name, [](char c) { return isWordChar(c) || c == '.'; }) && !std::ranges::contains(out, name))
                out.push_back(std::move(name));
            for (const std::string& word : rawWords(line)) // (inputs' and sliders' var=)
                if (lower(word).starts_with("var=") && validName(unquote(word.substr(4))) && !std::ranges::contains(out, lower(unquote(word.substr(4)))))
                    out.push_back(lower(unquote(word.substr(4))));
        }
    std::ranges::sort(out);
    variables_ = {version_, std::move(out)};
    return variables_.second;
}

std::vector<std::string> StoryEditor::names(Pick pick, std::string_view context) const {
    auto filesIn = [&](const char* folder, std::span<const std::string_view> extensions) {
        std::vector<std::string> out;
        for (const auto ext : extensions)
            for (const std::string& name : listFiles(root_ + folder, ext))
                if (!name.contains('/')) out.push_back(scriptName(name.substr(0, name.size() - ext.size()))); // (what scripts call it)
        std::ranges::sort(out);
        return out;
    };
    auto add = [](std::vector<std::string>& out, std::string name) {
        if (!std::ranges::any_of(out, [&](const std::string& o) { return sameName(o, name); })) out.push_back(std::move(name));
    };
    static constexpr std::string_view kColorNames[]{"white", "black", "gray", "silver", "red", "darkred", "orange", "gold", "yellow",
                                                    "lime", "green", "darkgreen", "teal", "cyan", "skyblue", "lightblue", "blue", "navy",
                                                    "purple", "violet", "magenta", "pink", "hotpink", "brown", "beige", "transparent"};
    std::vector<std::string> out;
    switch (pick) {
    case Pick::None:
        break;
    case Pick::Characters:
    case Pick::Shown:
        for (const Character& c : characters()) out.push_back(c.name);
        if (pick == Pick::Shown) std::ranges::copy(filesIn("images/", kImageTypes), std::back_inserter(out));
        if (pick == Pick::Shown) std::ranges::copy(defined("picture"), std::back_inserter(out));
        break;
    case Pick::Pictures: // (the pictures alone: the first for a new part)
        out = filesIn("images/", kImageTypes);
        for (std::string& p : defined("picture")) add(out, std::move(p));
        break;
    case Pick::PicturesOrColors: // (a picture's box: context "files" leaves out the sub-pictures, for one cut from a file)
        out = filesIn("images/", kImageTypes);
        if (context != "files")
            for (std::string& p : defined("picture")) add(out, std::move(p));
        for (std::string& c : defined("color")) add(out, std::move(c));
        for (const std::string_view c : {"black", "white"}) add(out, std::string(c));
        break;
    case Pick::Bases: // aiko for aiko_happy.png, and the atlases
        for (const std::string& name : filesIn("images/", kImageTypes)) add(out, name.substr(0, name.find('_')));
        for (std::string& a : defined("atlas")) add(out, std::move(a));
        std::ranges::sort(out);
        break;
    case Pick::Sounds:
        out = filesIn("audio/", kAudioTypes);
        break;
    case Pick::VoiceFolders: // audio/voices/NAME/
        for (const auto ext : kAudioTypes)
            for (const std::string& name : listFiles(root_ + "audio/voices/", ext))
                if (const auto slash = name.find('/'); slash != npos) add(out, name.substr(0, slash));
        std::ranges::sort(out);
        break;
    case Pick::Channels: // sound ... as NAME, and looping sounds (by their file)
        for (const File& f : files_)
            for (const std::string& line : f.lines) {
                const auto w = rawWords(line);
                if (w.size() < 3 || lower(w[0]) != "sound") continue;
                for (std::size_t i = 2; i < w.size(); ++i) {
                    if (lower(w[i]) == "as" && i + 1 < w.size()) add(out, lower(unquote(w[i + 1])));
                    if (lower(w[i]) == "loop") add(out, lower(unquote(w[1])));
                }
            }
        std::ranges::sort(out);
        break;
    case Pick::Movies:
        out = filesIn("movies/", kVideoTypes);
        break;
    case Pick::PictureFiles:
        for (const auto ext : kImageTypes)
            for (const std::string& name : listFiles(root_ + "images/", ext))
                if (!name.contains('/')) out.push_back("images/" + name);
        std::ranges::sort(out);
        break;
    case Pick::Fonts: // (as font lines name them: "fonts/Lora.ttf")
        for (const auto ext : kFontTypes)
            for (const std::string& name : listFiles(root_ + "fonts/", ext))
                if (!name.contains('/')) out.push_back("fonts/" + name);
        std::ranges::sort(out);
        break;
    case Pick::FontNames:
        out = {"default"};
        for (std::string& f : defined("font")) add(out, std::move(f));
        break;
    case Pick::Places:
        out = {"left", "center", "right", "farleft", "farright"};
        for (std::string& p : defined("position"))
            if (!std::ranges::contains(out, lower(p))) out.push_back(std::move(p));
        break;
    case Pick::Transitions:
        out = {"fade", "dissolve", "wipe", "slide", "none"};
        break;
    case Pick::Scenes:
        for (const Scene& s : scenes_) out.push_back(s.name);
        break;
    case Pick::Screens:
        for (const Screen& s : screens_) out.push_back(s.name);
        for (const char* builtIn : {"say", "choice"})
            if (!std::ranges::contains(out, std::string(builtIn))) out.emplace_back(builtIn);
        break;
    case Pick::Styles:
        out = defined("style");
        break;
    case Pick::Colors:
        out = defined("color");
        for (const std::string_view c : kColorNames) add(out, std::string(c));
        break;
    case Pick::Records:
        out = defined(context == "unlock" ? "gallery" : context);
        break;
    case Pick::Animations:
        out = defined("animation");
        break;
    case Pick::VariableNames:
        out = variables();
        break;
    case Pick::Conditions: // (and what screens can check)
        out = variables();
        for (const std::string_view w : kScreenWords) add(out, std::string(w));
        break;
    case Pick::Settings: // (the ones a slider can change)
        out = {"master_volume", "music_volume", "sound_volume", "voice_volume", "text_speed", "auto_delay", "textbox_opacity", "text_size"};
        break;
    case Pick::Controls:
        out = defined("control");
        break;
    case Pick::Keys: {
        out = {"escape", "space", "return", "tab", "backspace", "delete", "up", "down", "left", "right", "home", "end", "pageup", "pagedown"};
        for (int n = 1; n <= 12; ++n) out.push_back(std::format("f{}", n));
        for (char c = 'a'; c <= 'z'; ++c) out.emplace_back(1, c);
        for (char c = '0'; c <= '9'; ++c) out.emplace_back(1, c);
        for (const std::string_view k : {"ctrl+s", "ctrl+l", "ctrl+z", "mouse_left", "mouse_right", "mouse_middle", "wheel_up", "wheel_down",
                                         "pad_a", "pad_b", "pad_x", "pad_y", "pad_start", "pad_back", "pad_lb", "pad_rb", "pad_lt", "pad_rt",
                                         "pad_up", "pad_down", "pad_left", "pad_right", "swipe_left", "swipe_right", "swipe_up", "swipe_down",
                                         "long_press", "two_finger_tap"})
            out.emplace_back(k);
        break;
    }
    case Pick::Languages: // (setting language NAME: the game's own by its name, then its translations by their files)
        out.push_back(lower(setting("language").value_or("English")));
        for (const std::string& file : filesIn("translations/", std::array{std::string_view{".vnscript"}})) out.push_back(lower(file));
        break;
    case Pick::Expressions: { // the character's pictures (images/NAME_happy.png) and its atlas's expressions
        std::string image = lower(context);
        std::ranges::replace(image, ' ', '_');
        for (const Character& c : characters())
            if (lower(c.name) == lower(context))
                for (const std::string& word : rawWords(afterFirst(files_[c.file].lines[c.line])))
                    if (lower(word).starts_with("image=")) image = lower(unquote(word.substr(6)));
        for (const std::string& name : filesIn("images/", kImageTypes))
            if (lower(name).starts_with(image + "_") && !lower(name).ends_with("_face")) out.push_back(name.substr(image.size() + 1));
        for (const File& f : files_)
            for (std::size_t i = 0; i < f.lines.size(); ++i)
                if (indentOf(f.lines[i]) == 0 && lower(firstWord(f.lines[i])) == "atlas" && lower(firstWord(afterFirst(f.lines[i]))) == image)
                    for (std::size_t j = i + 1; j < f.lines.size() && (blank(f.lines[j]) || indentOf(f.lines[j]) > 0); ++j)
                        if (lower(firstWord(f.lines[j])) == "expressions")
                            for (const std::string& e : rawWords(afterFirst(f.lines[j])))
                                if (!e.contains('=') && !std::ranges::contains(out, e)) out.push_back(e);
        break;
    }
    }
    return out;
}

std::string_view StoryEditor::kindName(Kind kind) {
    static constexpr std::string_view names[]{"Line",   "Character", "Background", "Choice", "Option", "If",      "Else",    "Set",
                                              "Jump",   "End",       "Music",      "Sound",  "Voice",  "Stop",    "Wait",    "Move",
                                              "Shake",  "Effect",    "Weather",    "Camera", "Movie",  "Screen",  "Notify",  "Textbox",
                                              "New page", "Together", "Chapter",   "Unlock", "Message", "Phone",  "Ask",     "Game",
                                              "Note",     "Command"};
    return names[static_cast<std::size_t>(kind)];
}

namespace {

// An animation's step as written: what it changes (x=-10 opacity=40%), how long it takes and how it
// moves (ease=), and back.
struct StepWords {
    std::string changes, seconds, ease;
};

StepWords stepOf(std::string_view line) {
    StepWords out;
    const auto w = rawWords(line);
    std::vector<std::string> changes;
    for (std::size_t i = 0; i < w.size(); ++i) {
        const std::string word = lower(w[i]);
        if ((word == "over" || word == "wait" || word == "for") && i + 1 < w.size()) out.seconds = w[++i];
        else if (word.starts_with("ease=")) out.ease = word.substr(5);
        else if (word != "seconds" && word != "second") changes.push_back(w[i]);
    }
    out.changes = joined(changes);
    return out;
}

std::string stepLine(const std::string& changes, const std::string& seconds, const std::string& ease) {
    const std::string time = seconds.empty() ? std::string{} : seconds + (seconds == "1" ? " second" : " seconds");
    if (changes.empty()) return time.empty() ? std::string{} : "wait " + time;
    return changes + (time.empty() ? "" : " over " + time) + (ease.empty() || ease == "inout" ? "" : " ease=" + ease);
}

// What a step changes, as its form calls it, and the values it often has: amounts as plain numbers, and places
// on the screen from -1 to 1 like the stage's (home: its own place).
struct StepKind {
    std::string_view key, label;
    Box box;                  // its box's kind (what it suggests, and how it's written)
    std::string_view more;    // (values its box suggests besides its kind's)
    std::string_view start;   // its value when it's picked
};

constexpr StepKind kStepKinds[]{
    {"opacity", "Opacity", Box::Opacity, "", "100%"},
    {"x", "Move across", Box::Across, "home:its own place|-2:just off the left|2:just off the right", "home"},
    {"y", "Move up or down", Box::Down, "home:its own place|-2:just off the bottom|2:just off the top", "home"},
    {"zoom", "Zoom", Box::Zoom, "", "100%"},
    {"rotate", "Rotation", Box::Rotation, "", "0"},
    {"color", "Colour", Box::Colour, "", "white"},
    {"fade", "Fade to a colour", Box::Colour, "none:back from it", "black"}, // (the screen covered with it, as a screen's fade transition does)
};

const StepKind* stepKind(std::string_view keyOrLabel) {
    const auto it = std::ranges::find_if(kStepKinds, [&](const StepKind& k) { return k.key == lower(keyOrLabel) || lower(k.label) == lower(keyOrLabel); });
    return it != std::end(kStepKinds) ? &*it : nullptr;
}

// A step's changes (opacity=40% x=home) as pairs; an amount is shown as a percent, however it was written (0.4 is 40%).
std::vector<std::pair<std::string, std::string>> stepChanges(std::string_view changes) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const std::string& word : rawWords(changes)) {
        const auto eq = word.find('=');
        if (eq == npos || eq == 0) continue;
        std::string key = lower(word.substr(0, eq)), value = unquote(word.substr(eq + 1));
        if (const StepKind* kind = stepKind(key); kind && StoryEditor::kindOf(kind->box).percent) value = percentText(value);
        out.emplace_back(std::move(key), std::move(value));
    }
    return out;
}

// A transition as written after "with": its kind, the picture a scene's wipe has the shape of, its
// seconds and the colour it fades through (with fade 2 to white).
struct WithWords {
    std::string kind, shape, seconds, color;
};

WithWords transitionOf(const std::vector<std::string>& w, std::size_t& i) {
    WithWords t;
    if (i >= w.size() || lower(w[i]) != "with") return t;
    ++i;
    if (i < w.size()) t.kind = lower(w[i++]);
    auto word = [&](std::string_view s) { return i < w.size() && lower(w[i]) == s; };
    if (t.kind == "wipe" && i < w.size() && !parseNumber(w[i]) && !word("at") && !word("as") && !word("to") && !w[i].contains('='))
        t.shape = w[i++];
    auto seconds = [&] {
        if (i < w.size() && parseNumber(w[i])) {
            t.seconds = w[i++];
            if (word("seconds") || word("second")) ++i;
        }
    };
    seconds();
    if (word("to") && i + 1 < w.size()) {
        t.color = w[i + 1];
        i += 2;
    } else if (i < w.size() && parseColor(w[i])) {
        t.color = w[i++];
    }
    if (t.seconds.empty()) seconds(); // (with fade to white 2)
    return t;
}

} // namespace

std::vector<StoryEditor::Field> StoryEditor::fields(const Row& row) const {
    const std::string_view t = row.text;
    if (const CommandForm* form = formOf(row.kind)) {
        auto out = formFields(*form, t);
        if (row.kind == Kind::Screen && lower(firstWord(t)) == "open") { // how it comes and goes: with fade [SECONDS] [to COLOUR] [then fade ...] [and close with fade ...]
            const auto more = std::ranges::find(out, std::string("More"), &Field::name);
            auto w = more != out.end() ? rawWords(more->value) : std::vector<std::string>{};
            std::size_t i = 0;
            auto fadesAt = [&](std::size_t& at) { // a fade, and the ones after it
                std::vector<WithWords> fades{transitionOf(w, at)};
                while (fades.front().kind == "fade" && at + 1 < w.size() && lower(w[at]) == "then" && lower(w[at + 1]) == "fade") {
                    w[at] = "with"; // (read like the first)
                    fades.push_back(transitionOf(w, at));
                }
                return fades;
            };
            const std::vector<WithWords> opening = fadesAt(i);
            if (!opening.front().kind.empty() && opening.front().kind != "fade") return out; // (kept as it is: a mistake)
            std::vector<WithWords> closing{WithWords{}};
            if (i + 2 < w.size() && lower(w[i]) == "and" && lower(w[i + 1]) == "close" && lower(w[i + 2]) == "with") {
                std::size_t at = i + 2;
                if (auto fades = fadesAt(at); fades.front().kind == "fade") closing = std::move(fades), i = at;
            }
            if (more != out.end() && i == w.size()) out.erase(more);
            else if (more != out.end()) more->value = joined(std::vector(w.begin() + static_cast<std::ptrdiff_t>(i), w.end()));
            // Each fade: to its colour in half its time; the screen comes (or the story comes back) from the last one's.
            auto addFades = [&](const std::vector<WithWords>& fades, const std::string& prefix, std::string_view group) { // (group: a heading that lasts)
                const std::string kind = fades.front().kind.empty() ? std::string("none") : fades.front().kind;
                out.push_back({.name = prefix + "Transition", .value = kind, .label = "Transition", .box = Box::Fading, .choices = "none fade", .labels = "At once|Fade", .group = std::string(group)});
                if (kind != "fade") return;
                for (std::size_t f = 0; f <= fades.size(); ++f) {
                    const std::string n = f == 0 ? std::string() : std::format(" {}", f + 1);
                    if (f > 0) out.push_back({.name = std::format("{}Then {}", prefix, f), .value = f < fades.size() ? "fade" : "none", .label = "Then",
                                              .box = Box::Fading, .choices = "none fade", .labels = "Nothing more|Another fade"});
                    if (f == fades.size()) break;
                    out.push_back({.name = prefix + "Seconds" + n, .value = fades[f].seconds, .label = "Seconds", .box = Box::Seconds, .hint = "From Settings"});
                    out.push_back({.name = prefix + "Through colour" + n, .value = fades[f].color, .label = "Fade colour", .box = Box::Colour, .hint = "black"});
                }
            };
            addFades(opening, "", "When it opens");
            addFades(closing, "Closing ", "When it closes");
        }
        return out;
    }
    const auto w = rawWords(afterFirst(t));
    std::size_t i = 0;
    auto next = [&] { return i < w.size() ? w[i++] : std::string{}; };
    // A transition's fields: how, and (unless it's none) how long, and for a scene the shape of a wipe
    // and the colour a fade goes through.
    auto addTransition = [](std::vector<Field>& out, const WithWords& tr, bool scene) {
        const std::string kind = tr.kind.empty() ? std::string(scene ? "none" : "usual") : tr.kind;
        if (scene) out.push_back({.name = "Transition", .value = kind, .box = Box::Transition, .choices = "none fade dissolve wipe slide", .group = "Transition"});
        else out.push_back({.name = "Transition", .value = kind, .box = Box::Transition, .choices = "usual fade dissolve wipe slide none", .group = "Transition"});
        if (kind == "none" || kind == "usual") return;
        out.push_back({.name = "Seconds", .value = tr.seconds, .box = Box::Seconds, .hint = "From Settings"});
        if (scene && kind == "wipe") out.push_back({.name = "Wipe's shape", .value = tr.shape, .label = "Wipe shape", .box = Box::Picture, .hint = "Default"});
        if (scene && kind == "fade") out.push_back({.name = "Through colour", .value = tr.color, .label = "Fade colour", .box = Box::Colour, .hint = "black"});
    };
    switch (row.kind) {
    case Kind::Line: {
        std::string speaker, expression, words;
        std::string_view prefix, said;
        if (speech(t, &prefix, &said)) {
            speaker = prefix;
            words = said;
            if (prefix.ends_with(')') && prefix.find('(') != npos) { // Aiko (happy):
                const auto open = prefix.rfind('(');
                expression = trim(prefix.substr(open + 1, prefix.size() - open - 2));
                speaker = trim(prefix.substr(0, open));
            } else if (const auto space = prefix.rfind(' '); space != npos && !isCharacter(prefix) && isCharacter(prefix.substr(0, space))) {
                speaker = prefix.substr(0, space); // Aiko happy:
                expression = prefix.substr(space + 1);
            }
        } else {
            words = unquote(t); // narration
        }
        return {{.name = "Speaker", .value = speaker, .box = Box::Character},
                {.name = "Expression", .value = expression, .box = Box::Expression, .context = speaker},
                {.name = "Words", .value = words, .label = "Text", .rich = true}};
    }
    case Kind::Background: {
        std::vector<Field> out{{.name = "Image", .value = next(), .label = "Background", .box = Box::Picture}};
        if (i < w.size() && lower(w[i]).starts_with("bottom=")) // (bottom=: a colour fading down to another, as one value: navy-black)
            if (const std::string bottom = unquote(w[i++].substr(7)); !bottom.empty() && !out[0].value.contains('-')) out[0].value += "-" + bottom;
        addTransition(out, transitionOf(w, i), true);
        if (i < w.size()) out.push_back({.name = "More", .value = joined(w, i), .hidden = true});
        return out;
    }
    case Kind::Character: {
        const bool hide = lower(firstWord(t)) == "hide";
        std::string who = next(), expression, place, tag;
        std::vector<std::pair<std::string, std::string>> settings; // key=value
        WithWords tr;
        std::vector<std::string> more;
        if (!hide && i < w.size() && !w[i].contains('=') && lower(w[i]) != "at" && lower(w[i]) != "to" && lower(w[i]) != "with" && lower(w[i]) != "as")
            expression = next();
        while (i < w.size()) {
            const std::string word = lower(w[i]);
            if (!hide && i + 1 < w.size() && (word == "at" || word == "to")) {
                place = w[i + 1];
                i += 2;
            } else if (!hide && i + 1 < w.size() && word == "as") {
                tag = w[i + 1];
                i += 2;
            } else if (word == "with") {
                tr = transitionOf(w, i);
            } else if (const auto eq = word.find('='); !hide && eq != npos && eq > 0) {
                const std::string key = word.substr(0, eq), value = unquote(std::string_view(w[i]).substr(eq + 1));
                if (key == "x") place = value; // (the same as at)
                else settings.emplace_back(key, value);
                ++i;
            } else {
                more.push_back(next());
            }
        }
        auto setting = [&](std::string_view key) {
            const auto it = std::ranges::find(settings, key, &std::pair<std::string, std::string>::first);
            return it != settings.end() ? it->second : std::string{};
        };
        std::vector<Field> out{{.name = "Show or hide", .value = hide ? "hide" : "show", .label = "Action", .box = Box::ShowHide, .choices = "show hide"},
                               {.name = "Who", .value = who, .label = "Character or image", .box = Box::Shown}};
        if (!hide) {
            const std::string flip = setting("flip");
            out.insert(out.end(), {{.name = "Expression", .value = expression, .box = Box::Expression, .context = who},
                                   {.name = "Place", .value = place, .box = Box::Place, .group = "Where"},
                                   {.name = "Height", .value = setting("y"), .label = "Up or down", .box = Box::Down},
                                   {.name = "Layer", .value = setting("layer"), .box = Box::Layer, .hint = "0"},
                                   {.name = "Zoom", .value = percentText(setting("zoom")), .box = Box::Zoom, .hint = "100%", .group = "Look"},
                                   {.name = "Opacity", .value = percentText(setting("opacity")), .box = Box::Opacity, .hint = "100%"},
                                   {.name = "Turn", .value = setting("rotate"), .label = "Rotation", .box = Box::Rotation, .hint = "0"},
                                   {.name = "Flip", .value = parseBool(flip).value_or(false) ? "yes" : "no", .box = Box::YesNo, .choices = "no yes"},
                                   {.name = "Tint", .value = setting("tint"), .box = Box::Colour, .hint = "None"},
                                   {.name = "Called", .value = tag, .label = "Name on stage", .box = Box::Name, .group = "Second copy"}});
        }
        addTransition(out, tr, false);
        for (const auto& [key, value] : settings) // (the ones without a box)
            if (key != "y" && key != "zoom" && key != "opacity" && key != "rotate" && key != "flip" && key != "layer" && key != "tint")
                more.push_back(key + "=" + written(value));
        if (!more.empty()) out.push_back({.name = "More", .value = joined(more), .hidden = true});
        return out;
    }
    case Kind::Choice: { // choice QUESTION [optional=yes] [screen=NAME] [for N seconds | for N lines] [return=no]
        std::vector<std::string> q(w.begin(), w.end());
        std::string optional = "no", screen, seconds, lines, returns = "yes";
        while (!q.empty()) {
            const std::string last = lower(q.back());
            const bool forLines = last == "lines" || last == "line", forSeconds = last == "seconds" || last == "second";
            const std::size_t unit = forLines || forSeconds ? 1 : 0;
            if (last.starts_with("optional=")) optional = last.substr(9) == "yes" ? "yes" : "no";
            else if (last.starts_with("return=")) returns = last.substr(7) == "no" ? "no" : "yes";
            else if (last.starts_with("screen=")) screen = q.back().substr(7);
            else if (q.size() >= 2 + unit && lower(q[q.size() - 2 - unit]) == "for" && parseNumber(q[q.size() - 1 - unit])) {
                (forLines ? lines : seconds) = q[q.size() - 1 - unit];
                q.resize(q.size() - 1 - unit); // (and "for", below)
            } else {
                break;
            }
            q.pop_back();
        }
        std::vector<Field> out{{.name = "Question", .value = joined(q), .rich = true},
                               {.name = "Can be passed", .value = optional, .label = "Optional", .box = Box::YesNo, .choices = "no yes", .group = "Choice"},
                               {.name = "Time limit", .value = seconds, .label = "Time limit (seconds)", .box = Box::Seconds, .hint = "None"},
                               {.name = "Up for lines", .value = lines, .label = "Stays up for (lines)", .box = Box::Count, .hint = "None"}};
        if (!lines.empty()) out.push_back({.name = "Comes back", .value = returns, .label = "Returns after", .box = Box::YesNo, .choices = "yes no"});
        out.push_back({.name = "On the screen", .value = screen, .label = "Screen", .box = Box::Screen, .hint = "choice"});
        return out;
    }
    case Kind::Option: {
        const OptionParts o = optionParts(t);
        std::vector<Field> out{{.name = "Text", .value = o.text, .rich = true}, {.name = "Goes to", .value = o.label, .label = "Jumps to", .box = Box::Scene, .hint = "None (its own lines)"}};
        addCheckFields(out, "Only when", "Available when", "Always", o.condition, Box::Variable, variables());
        out[2].group = "Availability";
        if (!o.condition.empty()) out.push_back({.name = "Until then", .value = o.locked ? "locked" : "hidden", .box = Box::Visibility, .choices = "hidden locked"});
        if (o.locked) out.push_back({.name = "What it needs", .value = o.note, .label = "Locked note", .box = Box::Text});
        return out;
    }
    case Kind::If:
    case Kind::Else: {
        std::string_view condition = afterFirst(t);
        if (row.kind == Kind::Else && lower(firstWord(t)) == "else") condition = lower(firstWord(condition)) == "if" ? afterFirst(condition) : std::string_view{};
        if (condition.ends_with(':')) condition.remove_suffix(1);
        std::vector<Field> out;
        addCheckFields(out, "What", "Check", row.kind == Kind::Else ? "Otherwise" : "", condition, Box::Variable, variables());
        return out;
    }
    case Kind::Set: { // set X to V | add V to X | subtract V from X | toggle X
        const std::string verb = lower(firstWord(t));
        const std::string_view rest = afterFirst(t);
        std::string variable, value;
        if (verb == "toggle") {
            variable = rest;
        } else if (verb == "set" && lower(firstWord(rest)) == "to") { // (no variable yet)
            value = afterFirst(rest);
        } else if (verb == "set") {
            std::size_t k = 0;
            while (k < rest.size() && !std::isspace(static_cast<unsigned char>(rest[k])) && rest[k] != '=' && rest[k] != '+' && rest[k] != '-') ++k;
            variable = rest.substr(0, k);
            std::string_view v = trim(rest.substr(k));
            if (v.starts_with('=')) v.remove_prefix(1);
            else if (lower(firstWord(v)) == "to") v = afterFirst(v);
            value = trim(v);
        } else if (const auto at = lower(rest).rfind(verb == "add" ? " to " : " from "); at != npos) {
            value = trim(rest.substr(0, at));
            variable = trim(rest.substr(at + (verb == "add" ? 4 : 6)));
        }
        std::vector<Field> out{{.name = "Change", .value = verb, .label = "Action", .box = Box::SetVerb, .choices = "set add subtract toggle"},
                               {.name = "Variable", .value = variable, .box = Box::Variable}};
        if (verb != "toggle") addSumFields(out, "Value", "Value", "", value, variables());
        return out;
    }
    case Kind::Message: { // message NAME: text | message to NAME: text
        const std::string_view rest = afterFirst(t);
        const auto colon = rest.find(':');
        std::string who(colon == npos ? rest : trim(rest.substr(0, colon)));
        std::string words(colon == npos ? std::string_view{} : trim(rest.substr(colon + 1)));
        const bool mine = lower(firstWord(who)) == "to" && who.size() > 3;
        if (mine) who = trim(std::string_view(who).substr(3));
        return {{.name = "Sent", .value = mine ? "by_player" : "to_player", .label = "Direction", .box = Box::Sent, .choices = "to_player by_player"},
                {.name = "Who", .value = who, .label = "Contact", .box = Box::Character},
                {.name = "Text", .value = unquote(words), .rich = true}};
    }
    case Kind::Note:
        return {{.name = "Note", .value = std::string(trim(t.substr(1))), .box = Box::Text}};
    default:
        break;
    }
    return {{.name = "Line", .value = std::string(t), .box = Box::Text}};
}

std::string StoryEditor::lineOf(Kind kind, const std::vector<Field>& f) const {
    auto value = [&](std::string_view name) { return fieldValue(f, name); };
    auto with = [](std::string_view word, const std::string& v) { return v.empty() ? std::string{} : std::format(" {}{}{}", word, word.empty() ? "" : " ", v); };
    auto transition = [&] {
        const std::string how = value("Transition");
        if (how.empty() || how == "usual" || (how == "none" && value("Show or hide").empty())) return std::string{}; // (a scene's none: not written)
        return " with " + how + with("", value("Wipe's shape")) + with("", value("Seconds")) + with("to", value("Through colour"));
    };
    if (const CommandForm* form = formOf(kind)) {
        const bool opens = kind == Kind::Screen && (value("What").empty() || value("What") == "open"); // (a screen opened, or closed, with fade)
        auto fades = [&](const std::string& prefix) { // with fade [SECONDS] [to COLOUR] [then fade ...]
            if (!opens || value(prefix + "Transition") != "fade") return std::string{};
            std::string out = " with fade" + with("", value(prefix + "Seconds")) + with("to", value(prefix + "Through colour"));
            for (int n = 2; value(std::format("{}Then {}", prefix, n - 1)) == "fade"; ++n)
                out += " then fade" + with("", value(std::format("{}Seconds {}", prefix, n))) + with("to", value(std::format("{}Through colour {}", prefix, n)));
            return out;
        };
        const std::string closing = fades("Closing ");
        return formLine(*form, f) + fades("") + (closing.empty() ? std::string{} : " and close" + closing);
    }
    switch (kind) {
    case Kind::Line: {
        const std::string speaker = value("Speaker"), expression = value("Expression"), words = value("Words");
        if (speaker.empty()) { // narration (in quotes when it would read as something else)
            const bool plain = !words.empty() && storyCommandOf(words).empty() && !speech(words) && !words.starts_with('#') && !words.starts_with('"');
            return plain ? words : "\"" + words + "\"";
        }
        const std::string prefix = expression.empty() ? speaker : isCharacter(speaker) ? speaker + " " + expression : speaker + " (" + expression + ")";
        return std::format("{}: {}", prefix, words);
    }
    case Kind::Background:
        return "scene " + value("Image") + transition() + with("", value("More"));
    case Kind::Character: {
        if (value("Show or hide") == "hide") return "hide " + value("Who") + transition() + with("", value("More"));
        std::string out = "show " + value("Who") + with("", value("Expression")) + with("at", value("Place"));
        for (const auto& [name, key] : {std::pair{"Height", "y"}, {"Zoom", "zoom"}, {"Opacity", "opacity"}, {"Turn", "rotate"}, {"Flip", "flip"},
                                        {"Layer", "layer"}, {"Tint", "tint"}})
            if (const std::string v = value(name); !v.empty() && !(key == std::string_view("flip") && v == "no")) {
                const auto field = std::ranges::find(f, std::string_view(name), &Field::name);
                out += std::format(" {}={}", key, written(field != f.end() && kindOf(field->box).percent ? percentText(v) : v));
            }
        return out + with("as", value("Called")) + with("", value("More")) + transition();
    }
    case Kind::Choice: {
        std::string out = "choice" + with("", value("Question"));
        if (value("Can be passed") == "yes") out += " optional=yes";
        if (!value("On the screen").empty()) out += " screen=" + value("On the screen");
        if (!value("Up for lines").empty()) out += " for " + value("Up for lines") + (value("Up for lines") == "1" ? " line" : " lines");
        else if (!value("Time limit").empty()) out += " for " + value("Time limit") + (value("Time limit") == "1" ? " second" : " seconds");
        if (value("Comes back") == "no" && !value("Up for lines").empty()) out += " return=no";
        return out;
    }
    case Kind::Option: {
        // Text [(if CONDITION) | (needs CONDITION: what it needs)] [-> scene]; the text in quotes when
        // it would read as more than that.
        const std::string text = value("Text"), when = conditionFrom(f, "Only when", variables()), needs = value("What it needs"), to = value("Goes to");
        const bool plain = !text.empty() && !text.starts_with('#') && !text.starts_with('"') && !text.contains("->") && !text.ends_with(')') &&
                           !text.ends_with(':');
        std::string out = plain ? text : "\"" + text + "\"";
        if (!when.empty()) out += value("Until then") == "locked" ? " (needs " + when + (needs.empty() ? "" : ": " + needs) + ")" : " (if " + when + ")";
        return out + with("->", to);
    }
    case Kind::If:
    case Kind::Else: {
        const std::string condition = conditionFrom(f, "What", variables());
        if (kind == Kind::Else) return condition.empty() ? "else" : "else if " + condition;
        return "if" + with("", condition);
    }
    case Kind::Set: {
        const std::string change = value("Change"), variable = value("Variable"), v = sumFrom(f, "Value", variables());
        if (change == "toggle") return "toggle" + with("", variable);
        if (change == "add") return "add" + with("", v) + with("to", variable);
        if (change == "subtract") return "subtract" + with("", v) + with("from", variable);
        return "set" + with("", variable) + with("to", v);
    }
    case Kind::Message: {
        const bool mine = value("Sent") == "by_player";
        if (value("Who").empty() && value("Text").empty()) return "message";
        return std::format("message {}{}: {}", mine ? "to " : "", value("Who"), value("Text"));
    }
    case Kind::Note:
        return "# " + value("Note");
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
    if (const CommandForm* form = formOf(row.kind)) { // the choice it makes, and what's in its places
        std::string out;
        if (const auto verbs = wordsOf(form->verbs); verbs.size() > 1) {
            const auto labels = wordsOf(form->labels, '|');
            const auto at = static_cast<std::size_t>(std::ranges::find(verbs, std::string_view(v("What"))) - verbs.begin());
            out = at < labels.size() ? std::string(labels[at].substr(0, labels[at].find(" ("))) : v("What"); // ("Call", not "Call (and come back)")
            if (out == kindName(row.kind)) out.clear();
        }
        for (const Slot& s : form->slots)
            if ((s.type == Type::Word || s.type == Type::Words || s.type == Type::Text) && appliesTo(s, v("What").empty() ? wordsOf(form->verbs).front() : v("What")))
                if (const std::string value = v(s.name); !value.empty()) out += (out.empty() ? "" : " ") + value;
        return out;
    }
    switch (row.kind) {
    case Kind::Line:
        if (v("Speaker").empty()) return v("Words");
        return std::format("{}{}: {}", v("Speaker"), v("Expression").empty() ? "" : " (" + v("Expression") + ")", v("Words"));
    case Kind::Background:
        return v("Image") + (v("Transition").empty() || v("Transition") == "usual" ? "" : v("Transition") == "none" ? ", at once" : ", with " + v("Transition"));
    case Kind::Character:
        if (v("Show or hide") == "hide") return "Hide " + v("Who");
        return "Show " + v("Who") + (v("Expression").empty() ? "" : " (" + v("Expression") + ")") + (v("Place").empty() ? "" : ", at " + v("Place"));
    case Kind::Option:
        return v("Text") + (v("Goes to").empty() ? "" : "  -> " + v("Goes to"));
    case Kind::If:
    case Kind::Else: {
        const std::string c = conditionFrom(f, "What", variables());
        return row.kind == Kind::Else && c.empty() ? "Otherwise" : c;
    }
    case Kind::Set:
        return lineOf(Kind::Set, f);
    case Kind::Message:
        return std::format("{} {}: {}", v("Sent") == "by_player" ? "To" : "From", v("Who"), v("Text"));
    case Kind::Together:
        return "These lines at once";
    default:
        return f.empty() ? std::string{} : f.front().value;
    }
}

std::vector<StoryEditor::Field> StoryEditor::fields(const Character& c) const {
    return formFields(definitionForm("character"), files_[c.file].lines[c.line]);
}

std::vector<StoryEditor::Field> StoryEditor::fields(std::string_view kind, const Definition& d) const {
    const auto lines = linesOf(d);
    auto out = formFields(definitionForm(kind), lines.front());
    if (kind == "font" && rawWords(afterFirst(lines.front())).size() == 1) { // (font FILE: the file is in the name's place)
        out[1].value = out[0].value;
        out[0].value.clear();
    }
    if (kind == "atlas") // the frames' names under it, and the picture the eyes and the mouth are in (and where)
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
            out.push_back({.name = n, .value = names, .box = Box::Text});
            if (part == "expressions") continue;
            out.push_back({.name = n + "' image", .value = setting("image"), .label = n + " image", .box = Box::Picture});
            out.push_back({.name = n + "' x", .value = setting("x"), .label = n + " X", .box = Box::Pixels, .hint = "0"});
            out.push_back({.name = n + "' y", .value = setting("y"), .label = n + " Y", .box = Box::Pixels, .hint = "0"});
        }
    if (kind == "animation") { // its steps: what each changes, how long it takes and how it moves (and one more, empty)
        std::size_t n = 0;
        for (std::size_t i = 1; i <= lines.size(); ++i) {
            const StepWords step = i < lines.size() ? stepOf(lines[i]) : StepWords{};
            const bool more = i == lines.size(); // (to add one)
            const std::string name = std::format("Step {}", ++n);
            // What it changes (a list), and to what (a list of the values it often has); "And" adds another.
            const auto changes = stepChanges(step.changes);
            for (std::size_t c = 0; c <= changes.size() && !(more && c > 0); ++c) {
                const std::string cn = std::format("{} change {}", name, c + 1);
                const StepKind* change = c < changes.size() ? stepKind(changes[c].first) : nullptr;
                out.push_back({.name = cn, .value = change ? std::string(change->label) : std::string{}, .label = c > 0 ? std::string("And") : more && n > 1 ? std::string("Then") : name,
                               .box = Box::Change, .hint = c > 0 ? "None" : "None (waits)"});
                out.push_back({.name = cn + " was", .value = change ? std::string(change->key) : std::string{}, .hidden = true});
                if (change)
                    out.push_back({.name = cn + " to", .value = changes[c].second, .label = "To", .box = change->box, .suggest = std::string(change->more)});
            }
            out.push_back({.name = name + " seconds", .value = step.seconds, .label = "Duration (seconds)", .box = Box::Seconds, .hint = "0"});
            if (!more)
                out.push_back({.name = name + " ease", .value = step.ease.empty() ? std::string("inout") : step.ease, .label = "Easing",
                               .box = Box::Easing, .choices = "inout linear in out"});
        }
    }
    for (Field& f : out)
        if (f.box == Box::Record) f.context = "atlas";
        else if (kind == "picture" && f.box == Box::Picture) f.context = "files"; // (cut from a picture file)
    if (kind == "control") { // what it does as a list, and when it works as checks
        auto at = std::ranges::find(out, std::string("Does"), &Field::name);
        const std::string does = at->value;
        at = out.erase(at);
        std::vector<Field> actions;
        addActionFields(actions, "Does", "Action", "None", does, variables());
        at = out.insert(at, actions.begin(), actions.end());
        at = std::ranges::find(out, std::string("Only when"), &Field::name);
        const std::string when = at->value;
        at = out.erase(at);
        std::vector<Field> checks;
        addCheckFields(checks, "Only when", "Works when", "Always", when, Box::Condition, variables());
        out.insert(at, checks.begin(), checks.end());
    }
    return out;
}

std::vector<std::string> StoryEditor::linesOf(std::string_view kind, const std::vector<Field>& fields) const {
    auto value = [&](std::string_view name) { return fieldValue(fields, name); };
    std::vector<Field> filled = fields; // (a control's actions and checks, as one value each)
    if (kind == "control") {
        if (const auto does = std::ranges::find(filled, std::string("Does"), &Field::name); does != filled.end()) does->value = actionsFrom(fields, "Does", variables());
        if (const auto when = std::ranges::find(filled, std::string("Only when"), &Field::name); when != filled.end())
            when->value = conditionFrom(fields, "Only when", variables());
    }
    std::vector<std::string> out{formLine(definitionForm(kind), filled)};
    auto with = [](const std::string& v) { return v.empty() ? v : " " + v; };
    if (kind == "font" && value("Name").empty()) out.front() = std::format("font \"{}\"", value("File")) + (value("Style").empty() ? "" : " " + value("Style"));
    if (kind == "animation")
        for (std::size_t n = 1; std::ranges::contains(fields, std::format("Step {} change 1", n), &Field::name); ++n) {
            const std::string name = std::format("Step {}", n);
            std::string changes;
            for (std::size_t c = 1; std::ranges::contains(fields, std::format("{} change {}", name, c), &Field::name); ++c) {
                const std::string cn = std::format("{} change {}", name, c);
                const StepKind* change = stepKind(value(cn));
                if (!change) continue;
                std::string to = value(cn + " to");
                if (value(cn + " was") != change->key || to.empty()) to = change->start; // (another change, or one just picked: its value starts again)
                changes += std::format("{}{}={}", changes.empty() ? "" : " ", change->key, written(kindOf(change->box).percent ? percentText(to) : to));
            }
            if (std::string line = stepLine(changes, value(name + " seconds"), value(name + " ease")); !line.empty()) out.push_back(std::move(line));
        }
    if (kind == "atlas")
        for (const auto& [part, partLabel] : kAtlasParts) {
            const std::string n(partLabel);
            std::string line = std::string(part) + with(value(n));
            for (const auto& [suffix, key] : {std::pair{"' image", "image"}, {"' x", "x"}, {"' y", "y"}})
                if (const std::string v = value(n + suffix); !v.empty()) line += std::format(" {}={}", key, written(v));
            if (line != part) out.push_back(line); // (like= can give it the names: then only where its picture is)
        }
    return out;
}

// --- Changes ---------------------------------------------------------------------------------

StoryEditor::Snapshot StoryEditor::snapshot() const {
    Snapshot out;
    for (const File& f : files_) out.emplace_back(f.path, f.lines);
    return out;
}

// The files as they were: changed ones written, missing ones made again, and ones that weren't there
// then taken away.
void StoryEditor::restore(const Snapshot& snapshot) {
    std::vector<std::string> gone;
    for (const File& f : files_)
        if (std::ranges::none_of(snapshot, [&](const auto& s) { return s.first == f.path; })) gone.push_back(f.path);
    std::vector<File> next;
    std::vector<std::size_t> changed;
    for (const auto& [path, lines] : snapshot) {
        const auto it = std::ranges::find(files_, path, &File::path);
        if (it == files_.end() || it->lines != lines) changed.push_back(next.size());
        next.push_back({.path = path, .lines = lines, .modified = it != files_.end() ? it->modified : 0});
    }
    files_ = std::move(next);
    for (const std::size_t f : changed) write(f);
    for (const std::string& path : gone) {
        SDL_RemovePath((root_ + path).c_str());
        ++version_;
    }
    rescan();
}

void StoryEditor::remember(const std::string& key) {
    if (!key.empty() && key == lastKey_) return; // (still typing into the same field)
    lastKey_ = key;
    undo_.push_back({.files = snapshot(), .renamed = {}});
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
}

void StoryEditor::write(std::size_t file) {
    const std::string text = joinedLines(files_[file].lines);
    const std::string path = root_ + files_[file].path;
    if (!SDL_SaveFile(path.c_str(), text.data(), text.size())) problem_ = std::format("Can't save '{}': {}.", files_[file].path, SDL_GetError());
    if (SDL_PathInfo info; SDL_GetPathInfo(path.c_str(), &info)) files_[file].modified = info.modify_time;
    ++version_;
}

void StoryEditor::writeAll() {
    for (std::size_t f = 0; f < files_.size(); ++f) write(f);
}

void StoryEditor::movedFiles(Moves moves) {
    if (moves.empty()) return;
    remember({});
    undo_.back().renamed = std::move(moves);
    ++version_;
}

StoryEditor::Moves StoryEditor::undoMoves(const Moves& moves) {
    Moves again;
    for (const auto& [from, to] : moves | std::views::reverse) {
        if (from.empty()) { // (imported: to the Trash, and back from there)
            if (const auto there = moveToTrash(to); there && !there->empty()) again.emplace_back(*there, to);
        } else if (SDL_RenamePath(to.c_str(), from.c_str())) {
            again.emplace_back(from, to);
        }
    }
    std::ranges::reverse(again);
    return again;
}

StoryEditor::Moves StoryEditor::redoMoves(const Moves& moves) {
    Moves again;
    for (const auto& [from, to] : moves)
        if (SDL_RenamePath(from.c_str(), to.c_str())) again.emplace_back(from, to);
    return again;
}

// (files moved in the step go back too, or again)
bool StoryEditor::undo() {
    if (undo_.empty()) return false;
    const Step before = std::move(undo_.back());
    undo_.pop_back();
    const Snapshot now = snapshot();
    redo_.push_back({.files = now, .renamed = undoMoves(before.renamed)});
    if (!before.renamed.empty()) ++version_;
    restore(before.files);
    lastKey_.clear();
    return true;
}

bool StoryEditor::redo() {
    if (redo_.empty()) return false;
    const Step after = std::move(redo_.back());
    redo_.pop_back();
    const Snapshot now = snapshot();
    undo_.push_back({.files = now, .renamed = redoMoves(after.renamed)});
    if (!after.renamed.empty()) ++version_;
    restore(after.files);
    lastKey_.clear();
    return true;
}

bool StoryEditor::changedOutside() const {
    if (files_.empty()) return false;
    std::size_t there = 0;
    for (const std::string& name : listFiles(root_ + "scripts/", ".vnscript")) {
        if (name.contains('/')) continue;
        if (std::ranges::none_of(files_, [&](const File& f) { return f.path == "scripts/" + name; })) return true; // (a new one)
        ++there;
    }
    if (there != files_.size()) return true; // (one went)
    for (const File& f : files_)
        if (SDL_PathInfo info; SDL_GetPathInfo((root_ + f.path).c_str(), &info) && info.modify_time != f.modified) return true;
    return false;
}

std::string StoryEditor::textOf(std::size_t file, std::size_t from, std::size_t until) const {
    const auto& lines = files_[file].lines;
    until = std::min(until, lines.size());
    int shared = INT_MAX;
    for (std::size_t i = from; i < until; ++i)
        if (!blank(lines[i])) shared = std::min(shared, indentOf(lines[i]));
    std::string out;
    for (std::size_t i = from; i < until; ++i) {
        const std::string& line = lines[i];
        std::size_t cut = 0; // (`shared` columns of spaces and tabs)
        for (int n = 0; cut < line.size() && n < shared && (line[cut] == ' ' || line[cut] == '\t'); ++cut) n += line[cut] == '\t' ? kIndent : 1;
        out += line.substr(cut) + (i + 1 < until ? "\n" : "");
    }
    return out;
}

std::size_t StoryEditor::pasteScene(std::size_t after, std::vector<std::string> lines) {
    std::string base(trim(afterFirst(lines.front())));
    if (base.ends_with(':')) base.pop_back();
    if (!validName(base)) base = "scene";
    const std::string name = uniqueName(base, [&](const std::string& n) { return hasScene(n); });
    lines.front() = "label " + name;
    const Scene s = scenes_[after];
    remember({});
    lines.insert(lines.begin(), "");
    auto& file = files_[s.file].lines;
    file.insert(file.begin() + static_cast<std::ptrdiff_t>(s.end), lines.begin(), lines.end());
    write(s.file);
    rescan();
    return static_cast<std::size_t>(std::ranges::find(scenes_, name, &Scene::name) - scenes_.begin());
}

std::size_t StoryEditor::pasteCharacter(std::size_t after, const std::string& line) {
    auto words = rawWords(afterFirst(line));
    if (words.empty()) return after;
    const std::string base = unquote(words[0]);
    const std::string name = uniqueName(base, [&](const std::string& n) { return isCharacter(n); }, " ");
    words[0] = name.contains(' ') ? "\"" + name + "\"" : name;
    const Character c = characters()[after];
    remember({});
    files_[c.file].lines.insert(files_[c.file].lines.begin() + static_cast<std::ptrdiff_t>(c.line + 1), "character " + joined(words));
    write(c.file);
    rescan();
    const auto now = characters();
    return static_cast<std::size_t>(std::ranges::find(now, name, &Character::name) - now.begin());
}

std::string StoryEditor::fileName(std::size_t file) const {
    std::string name = files_[file].path.substr(8); // (scripts/)
    return stemOf(name); // (.vnscript)
}

void StoryEditor::include(std::string_view name) {
    if (!hasMain()) return;
    auto& main = files_[0].lines;
    if (std::ranges::any_of(main, [&](const std::string& l) { return includesScript(l, lower(name)); })) return;
    std::size_t at = 0; // after the other include and config lines (or the notes at the top)
    for (std::size_t i = 0; i < main.size(); ++i)
        if (lower(firstWord(main[i])) == "include" || configOf(main[i])) at = i + 1;
    if (at == 0)
        while (at < main.size() && trim(main[at]).starts_with('#')) ++at;
    main.insert(main.begin() + static_cast<std::ptrdiff_t>(at), "include " + std::string(name));
    write(0);
}

bool StoryEditor::storyFile(std::size_t file) const {
    return std::ranges::contains(scenes_, file, &Scene::file) || std::ranges::all_of(files_[file].lines, [](const std::string& l) {
               const std::string_view t = trim(l);
               return t.empty() || t.starts_with('#') || t.starts_with("include ");
           });
}

std::size_t StoryEditor::addFile() {
    const std::string name = uniqueName("new_file", [&](const std::string& n) { // (one there already: new_file_2...)
        return std::ranges::any_of(files_, [&](const File& f) { return lower(f.path) == scriptPath(n); }) || fileExists(root_ + scriptPath(n));
    });
    remember({});
    files_.push_back({.path = scriptPath(name), .lines = {"# More of the story."}});
    write(files_.size() - 1);
    include(name);
    rescan();
    return files_.size() - 1;
}

bool StoryEditor::renameFile(std::size_t file, const std::string& name, const std::string& key) {
    const std::string from = fileName(file), path = scriptPath(name);
    if (lower(name) == lower(from)) return true;
    if (file == 0 || !validName(name) || std::ranges::any_of(files_, [&](const File& f) { return lower(f.path) == lower(path); }) ||
        fileExists(root_ + path))
        return false;
    remember(key);
    const std::string old = files_[file].path;
    files_[file].path = path;
    write(file);
    SDL_RemovePath((root_ + old).c_str());
    changeLines([&](std::size_t, std::size_t, const std::string& line) -> std::optional<std::string> { // (its include lines follow)
        if (!includesScript(line, lower(from))) return std::nullopt;
        return leading(line) + "include " + name;
    });
    rescan();
    return true;
}

void StoryEditor::removeFile(std::size_t file) {
    if (file == 0 || file >= files_.size()) return;
    remember({});
    const std::string name = lower(fileName(file));
    for (std::size_t f = 0; f < files_.size(); ++f)
        if (f != file && std::erase_if(files_[f].lines, [&](const std::string& l) { return includesScript(l, name); }) > 0) write(f);
    if (!moveToTrash(root_ + files_[file].path)) SDL_RemovePath((root_ + files_[file].path).c_str()); // (undo has its lines still)
    files_.erase(files_.begin() + static_cast<std::ptrdiff_t>(file));
    ++version_;
    rescan();
}

std::optional<std::size_t> StoryEditor::moveSceneTo(std::size_t scene, std::size_t file, std::size_t to) {
    const Scene s = scenes_[scene];
    if (file == s.file) return moveScene(scene, to);
    if (lower(s.name) == "start") return std::nullopt; // (it stays in main.vnscript)
    remember({});
    auto& source = files_[s.file].lines;
    const std::vector<std::string> block(source.begin() + static_cast<std::ptrdiff_t>(s.line), source.begin() + static_cast<std::ptrdiff_t>(s.end));
    std::size_t from = s.line, until = s.end;
    if (until < source.size() && blank(source[until])) ++until;
    else if (from > 0 && blank(source[from - 1])) --from;
    source.erase(source.begin() + static_cast<std::ptrdiff_t>(from), source.begin() + static_cast<std::ptrdiff_t>(until));
    auto& target = files_[file].lines;
    const std::size_t at = to < scenes_.size() && scenes_[to].file == file ? scenes_[to].line : target.size();
    std::vector<std::string> put = block;
    if (at > 0 && !blank(target[at - 1])) put.insert(put.begin(), "");
    if (at < target.size() && !blank(target[at])) put.emplace_back();
    target.insert(target.begin() + static_cast<std::ptrdiff_t>(at), put.begin(), put.end());
    write(s.file);
    write(file);
    rescan();
    for (std::size_t i = 0; i < scenes_.size(); ++i)
        if (scenes_[i].file == file && lower(scenes_[i].name) == lower(s.name)) return i;
    return std::nullopt;
}

void StoryEditor::setText(const Scene& scene, const Row& row, const std::string& text, const std::string& key) {
    std::string& line = files_[scene.file].lines[row.line];
    if (trim(line) == text) return;
    remember(key);
    line = leading(line) + text;
    write(scene.file);
}

std::size_t StoryEditor::add(const Scene& scene, const Row* row, bool inside, const std::vector<std::string>& lines) {
    remember({});
    auto& file = files_[scene.file].lines;
    const auto rs = rows(scene);
    std::size_t at = row ? row->end : blockEnd(scene.file, scene.line);
    std::string indent = row ? leading(file[row->line]) + (inside ? std::string(kIndent, ' ') : "")
                       : rs.empty() ? std::string(kIndent, ' ') : leading(file[rs.front().line]);
    if (row && inside) // (as far in as the lines already inside it)
        for (const Row& r : rs)
            if (r.line > row->line && r.line < row->end && r.depth == row->depth + 1) {
                indent = leading(file[r.line]);
                break;
            }
    for (std::size_t i = 0; i < lines.size(); ++i) file.insert(file.begin() + static_cast<std::ptrdiff_t>(at + i), indent + lines[i]);
    write(scene.file);
    rescan();
    return at;
}

void StoryEditor::eraseLines(std::size_t file, std::size_t from, std::size_t until) {
    remember({});
    auto& lines = files_[file].lines;
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(until));
    write(file);
    rescan();
}

void StoryEditor::remove(const Scene& scene, const Row& row) { eraseLines(scene.file, row.line, row.end); }

std::size_t StoryEditor::addScene(std::size_t file) {
    const std::string name = uniqueName("new_scene", [&](const std::string& n) { return hasScene(n); }); // (one there already: new_scene_2...)
    remember({});
    auto& lines = files_[file].lines;
    if (!lines.empty() && !blank(lines.back())) lines.emplace_back();
    lines.push_back("label " + name); // (empty: its lines are added in the Story tab)
    write(file);
    rescan();
    return static_cast<std::size_t>(std::ranges::find(scenes_, name, &Scene::name) - scenes_.begin());
}

bool StoryEditor::renameScene(std::size_t scene, const std::string& name, const std::string& key) {
    const std::string from = scenes_[scene].name;
    if (lower(name) == lower(from)) return true;
    if (!validName(name) || hasScene(name)) return false;
    remember(key);
    if (const auto at = mapPlace(from)) { // (its place on the Map goes with it)
        mapPlaces_.erase(lower(from));
        mapPlaces_.insert_or_assign(lower(name), *at);
        saveMapPlaces();
    }
    const Scene s = scenes_[scene];
    files_[s.file].lines[s.line] = "label " + name;
    changeLines([&](std::size_t, std::size_t, const std::string& line) -> std::optional<std::string> {
        std::string after = renamed(line, from, name);
        return after != line ? std::optional(std::move(after)) : std::nullopt;
    }, s.file);
    rescan();
    return true;
}

void StoryEditor::removeScene(std::size_t scene) {
    const Scene s = scenes_[scene];
    const std::size_t from = s.line > 0 && blank(files_[s.file].lines[s.line - 1]) ? s.line - 1 : s.line; // (the blank line before it too)
    eraseLines(s.file, from, blockEnd(s.file, s.line));
}

std::size_t StoryEditor::moveLines(std::size_t file, std::size_t from, std::size_t until, std::size_t at, bool spaced) {
    auto& lines = files_[file].lines;
    const std::vector<std::string> block(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(until));
    if (spaced) { // (with the blank line next to it)
        if (until < lines.size() && blank(lines[until])) ++until;
        else if (from > 0 && blank(lines[from - 1])) --from;
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(until));
    if (at >= until) at -= until - from;
    std::vector<std::string> put = block;
    if (spaced && at > 0 && !blank(lines[at - 1])) put.insert(put.begin(), "");
    if (spaced && at < lines.size() && !blank(lines[at])) put.emplace_back();
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), put.begin(), put.end());
    return spaced && at > 0 && put.front().empty() ? at + 1 : at;
}

template <class T, class EndOf, class Again>
std::optional<std::size_t> StoryEditor::moveRecord(const std::vector<T>& all, std::size_t index, std::size_t to, bool spaced, EndOf endOf, Again again) {
    const T& it = all[index];
    std::optional<std::size_t> at; // the line it goes before
    if (to < all.size() && all[to].file == it.file) at = all[to].line;
    else if (to > 0 && all[to - 1].file == it.file) at = endOf(all[to - 1]);
    if (!at || to == index || to == index + 1) return std::nullopt;
    remember({});
    moveLines(it.file, it.line, endOf(it), *at, spaced);
    write(it.file);
    rescan();
    const auto now = again();
    for (std::size_t i = 0; i < now.size(); ++i)
        if (now[i].file == it.file && lower(now[i].name) == lower(it.name)) return i;
    return std::nullopt;
}

std::optional<std::size_t> StoryEditor::moveScene(std::size_t scene, std::size_t to) {
    return moveRecord(scenes_, scene, to, true, [](const Scene& s) { return s.end; }, [&] { return scenes_; });
}

namespace {

bool holdsLines(StoryEditor::Kind kind) {
    using Kind = StoryEditor::Kind;
    return kind == Kind::Option || kind == Kind::If || kind == Kind::Else || kind == Kind::Together || kind == Kind::Choice || kind == Kind::Message;
}

// The rows a row's block covers: [item, end).
std::size_t blockEndOf(const std::vector<StoryEditor::Row>& rows, std::size_t item) {
    std::size_t j = item + 1;
    while (j < rows.size() && rows[j].depth > rows[item].depth) ++j;
    return j;
}

// The rows above and below a slot, not counting the item's own block (none: -1 / rows.size()).
std::pair<std::ptrdiff_t, std::size_t> around(const std::vector<StoryEditor::Row>& rows, std::size_t item, std::size_t slot) {
    const std::size_t end = blockEndOf(rows, item);
    const std::ptrdiff_t above = slot > item && slot <= end ? static_cast<std::ptrdiff_t>(item) - 1 : static_cast<std::ptrdiff_t>(slot) - 1;
    const std::size_t below = slot >= item && slot < end ? end : slot;
    return {above, below};
}

} // namespace

bool StoryEditor::fits(const std::vector<Row>& rows, std::size_t item, std::size_t slot, int depth) const {
    if (item >= rows.size() || slot > rows.size()) return false;
    const std::size_t end = blockEndOf(rows, item);
    if (slot > item && slot < end) return false; // (into itself)
    const auto [above, below] = around(rows, item, slot);
    const int lowest = below < rows.size() ? rows[below].depth : 0;
    const int highest = above >= 0 ? rows[static_cast<std::size_t>(above)].depth + (holdsLines(rows[static_cast<std::size_t>(above)].kind) ? 1 : 0) : 0;
    if (depth < lowest || depth > highest) return false;
    if ((slot == item || slot == end) && depth == rows[item].depth) return false; // (where it is)
    // What it would be inside: the nearest row above at one level less.
    std::optional<Kind> parent;
    for (std::ptrdiff_t j = above; j >= 0 && depth > 0; --j)
        if (rows[static_cast<std::size_t>(j)].depth == depth - 1) {
            parent = rows[static_cast<std::size_t>(j)].kind;
            break;
        }
    const bool underChoice = parent && (*parent == Kind::Choice || *parent == Kind::Message);
    if (depth > 0 && !parent) return false;
    return (rows[item].kind == Kind::Option) == underChoice;
}

std::optional<std::size_t> StoryEditor::moveRowTo(const Scene& scene, const Row& row, std::size_t slot, int depth) {
    const auto rs = rows(scene);
    const auto item = static_cast<std::size_t>(std::ranges::find(rs, row.line, &Row::line) - rs.begin());
    if (!fits(rs, item, slot, depth)) return std::nullopt;
    const auto moved = moveRowsTo(scene, {row.line}, slot, depth);
    return moved.empty() ? std::nullopt : std::optional(moved.front());
}

namespace {

// The rows (their places in `rows`) of these lines, in order, leaving out the ones inside another.
std::vector<std::size_t> blocksOf(const std::vector<StoryEditor::Row>& rows, std::vector<std::size_t> lines) {
    std::ranges::sort(lines);
    std::vector<std::size_t> out;
    std::size_t end = 0;
    for (const std::size_t line : lines) {
        const auto it = std::ranges::find(rows, line, &StoryEditor::Row::line);
        if (it == rows.end() || (!out.empty() && line < end)) continue;
        out.push_back(static_cast<std::size_t>(it - rows.begin()));
        end = it->end;
    }
    return out;
}

} // namespace

void StoryEditor::removeRows(const Scene& scene, const std::vector<std::size_t>& lines) {
    const auto rs = rows(scene);
    const auto blocks = blocksOf(rs, lines);
    if (blocks.empty()) return;
    remember({});
    auto& file = files_[scene.file].lines;
    for (auto b = blocks.rbegin(); b != blocks.rend(); ++b)
        file.erase(file.begin() + static_cast<std::ptrdiff_t>(rs[*b].line), file.begin() + static_cast<std::ptrdiff_t>(rs[*b].end));
    write(scene.file);
    rescan();
}

std::string StoryEditor::textOfRows(const Scene& scene, const std::vector<std::size_t>& lines) const {
    const auto rs = rows(scene);
    std::string out;
    for (const std::size_t b : blocksOf(rs, lines)) out += (out.empty() ? "" : "\n") + textOf(scene.file, rs[b].line, rs[b].end); // (whole blocks)
    return out;
}

std::vector<std::size_t> StoryEditor::moveRowsTo(const Scene& scene, const std::vector<std::size_t>& lines, std::size_t slot, int depth) {
    const auto rs = rows(scene);
    const auto blocks = blocksOf(rs, lines);
    if (blocks.empty() || slot > rs.size()) return {};
    auto moving = [&](std::size_t i) { return std::ranges::any_of(blocks, [&](std::size_t b) { return i >= b && i < blockEndOf(rs, b); }); };
    if (slot < rs.size() && moving(slot) && !std::ranges::contains(blocks, slot)) return {}; // (into one of them)
    // The rows around the place (not the ones moving), what it'd be inside, and its indentation there.
    std::ptrdiff_t above = static_cast<std::ptrdiff_t>(slot) - 1;
    while (above >= 0 && moving(static_cast<std::size_t>(above))) --above;
    std::size_t below = slot;
    while (below < rs.size() && moving(below)) ++below;
    const int lowest = below < rs.size() ? rs[below].depth : 0;
    const int highest = above >= 0 ? rs[static_cast<std::size_t>(above)].depth + (holdsLines(rs[static_cast<std::size_t>(above)].kind) ? 1 : 0) : 0;
    if (depth < lowest || depth > highest) return {};
    std::optional<Kind> parent;
    for (std::ptrdiff_t j = above; j >= 0 && depth > 0; --j)
        if (rs[static_cast<std::size_t>(j)].depth == depth - 1) {
            parent = rs[static_cast<std::size_t>(j)].kind;
            break;
        }
    if (depth > 0 && !parent) return {};
    const bool underChoice = parent && (*parent == Kind::Choice || *parent == Kind::Message);
    if (std::ranges::any_of(blocks, [&](std::size_t b) { return (rs[b].kind == Kind::Option) != underChoice; })) return {};
    auto& lines_ = files_[scene.file].lines;
    std::string indent;
    for (std::ptrdiff_t j = above; j >= 0; --j) {
        const Row& r = rs[static_cast<std::size_t>(j)];
        if (r.depth <= depth) {
            indent = leading(lines_[r.line]) + (r.depth < depth ? std::string(kIndent, ' ') : std::string{});
            break;
        }
    }
    if (indent.empty()) indent = below < rs.size() && rs[below].depth == depth ? leading(lines_[rs[below].line]) : std::string(kIndent, ' ');
    remember({});
    std::vector<std::string> block;
    std::vector<std::size_t> starts;
    for (const std::size_t b : blocks) {
        starts.push_back(block.size());
        const std::string old = leading(lines_[rs[b].line]);
        for (std::size_t i = rs[b].line; i < rs[b].end; ++i) {
            const std::string& l = lines_[i];
            block.push_back(blank(l) ? l : l.starts_with(old) ? indent + l.substr(old.size()) : indent + std::string(trim(l)));
        }
    }
    std::size_t to = below < rs.size() ? rs[below].line : blockEnd(scene.file, scene.line);
    for (auto b = blocks.rbegin(); b != blocks.rend(); ++b) {
        lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(rs[*b].line), lines_.begin() + static_cast<std::ptrdiff_t>(rs[*b].end));
        if (rs[*b].line < to) to -= rs[*b].end - rs[*b].line;
    }
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(to), block.begin(), block.end());
    write(scene.file);
    rescan();
    for (std::size_t& start : starts) start += to;
    return starts;
}

void StoryEditor::moveRowToScene(const Scene& scene, const Row& row, const Scene& to) {
    if (scene.file == to.file && scene.line == to.line) return;
    remember({});
    auto& source = files_[scene.file].lines;
    const std::string old = leading(source[row.line]);
    const std::vector<std::string> block(source.begin() + static_cast<std::ptrdiff_t>(row.line), source.begin() + static_cast<std::ptrdiff_t>(row.end));
    source.erase(source.begin() + static_cast<std::ptrdiff_t>(row.line), source.begin() + static_cast<std::ptrdiff_t>(row.end));
    rescan();
    const auto target = std::ranges::find_if(scenes_, [&](const Scene& s) { return s.file == to.file && lower(s.name) == lower(to.name); });
    if (target == scenes_.end()) return;
    const auto there = rows(*target);
    auto& lines = files_[target->file].lines;
    const std::string indent = there.empty() ? std::string(kIndent, ' ') : leading(lines[there.front().line]);
    std::vector<std::string> put;
    put.reserve(block.size());
    for (const std::string& l : block) put.push_back(blank(l) ? l : l.starts_with(old) ? indent + l.substr(old.size()) : indent + std::string(trim(l)));
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(target->end), put.begin(), put.end());
    write(scene.file);
    if (target->file != scene.file) write(target->file);
    rescan();
}

std::vector<StoryEditor::Found> StoryEditor::find(std::string_view what) const {
    std::vector<Found> out;
    const std::string low = lower(trim(what));
    if (low.empty()) return out;
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i)
            if (const std::string& line = files_[f].lines[i]; lower(line).contains(low)) out.push_back({.file = f, .line = i, .text = std::string(trim(line))});
    return out;
}

std::optional<std::string> StoryEditor::lineText(std::string_view path, std::size_t line) const {
    for (const File& f : files_)
        if (f.path == path) return line < f.lines.size() ? std::optional(std::string(trim(f.lines[line]))) : std::nullopt;
    return std::nullopt;
}

bool StoryEditor::mentions(std::string_view word) const {
    const std::string low = lower(word);
    if (low.empty()) return false;
    for (const File& f : files_)
        for (const std::string& line : f.lines) {
            const std::string l = lower(line);
            for (auto at = l.find(low); at != npos; at = l.find(low, at + 1))
                if ((at == 0 || !isWordChar(l[at - 1])) && (at + low.size() == l.size() || !isWordChar(l[at + low.size()]))) return true;
        }
    return false;
}

std::string StoryEditor::placeOf(std::size_t file, std::size_t line) const {
    for (const Scene& s : scenes_)
        if (s.file == file && s.line <= line && line < std::max(s.end, s.line + 1)) return s.name;
    for (const Screen& s : screens_)
        if (s.file == file && s.line <= line && line < std::max(s.end, s.line + 1)) return "screen " + s.name;
    for (const Character& c : characters())
        if (c.file == file && c.line == line) return "character " + c.name;
    for (const DefinitionKind& kind : kDefinitionKinds)
        for (const Definition& def : definitions(kind.kind))
            if (def.file == file && def.line <= line && line < std::max(def.end, def.line + 1)) return std::format("{} {}", kind.kind, def.name);
    if (file == 0 && isSetting(line)) return "Settings";
    return fileName(file) + ".vnscript";
}

std::size_t StoryEditor::changeLines(const std::function<std::optional<std::string>(std::size_t, std::size_t, const std::string&)>& change,
                                     std::optional<std::size_t> also) {
    std::size_t count = 0;
    for (std::size_t f = 0; f < files_.size(); ++f) {
        bool touched = also == f;
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i)
            if (auto now = change(f, i, files_[f].lines[i])) {
                files_[f].lines[i] = std::move(*now);
                touched = true;
                ++count;
            }
        if (touched) write(f);
    }
    return count;
}

std::size_t StoryEditor::replaceAll(std::string_view what, const std::string& with) {
    const std::string low = lower(what);
    if (low.empty()) return 0;
    remember({});
    const std::size_t count = changeLines([&](std::size_t, std::size_t, const std::string& line) -> std::optional<std::string> {
        std::string out;
        std::size_t last = 0;
        const std::string l = lower(line);
        for (std::size_t at = l.find(low); at != npos; at = l.find(low, at + low.size())) {
            out += line.substr(last, at - last) + with;
            last = at + low.size();
        }
        if (last == 0) return std::nullopt;
        return out + line.substr(last);
    });
    if (count == 0) undo_.pop_back(); // (nothing changed: no step to undo)
    rescan();
    return count;
}

namespace {

bool nameChar(char c) { // (a variable's: persistent.trust)
    return letterOrDigitByte(c) || c == '_' || c == '.';
}

// `text` with the variable `from` called `to`: as code (outside quotes, and in braces inside them),
// or as words (only in braces: {name}).
std::string variableRenamed(std::string_view text, std::string_view from, std::string_view to, bool code) {
    const std::string name = lower(from);
    std::string out;
    bool inQuotes = false;
    int braces = 0;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        const bool live = (code && !inQuotes) || braces > 0;
        if (live && (i == 0 || !nameChar(text[i - 1])) && lower(text.substr(i, name.size())) == name &&
            (i + name.size() == text.size() || !nameChar(text[i + name.size()]))) {
            out += to;
            i += name.size();
            continue;
        }
        if (c == '"' && braces == 0) inQuotes = !inQuotes;
        else if (c == '{') ++braces;
        else if (c == '}' && braces > 0) --braces;
        out += c;
        ++i;
    }
    return out;
}

// A line with its settings that are code (if=, action=...) changed by `change`, each one's value
// without its quotes.
std::string codeSettings(const std::string& line, const std::function<std::string(std::string_view)>& change) {
    static constexpr std::string_view kCode[]{"if", "var", "action", "drop", "on_show", "on_hide", "enabled", "selected"};
    std::string out = line;
    const auto spans = wordSpans(line);
    for (const auto& [at, size] : spans | std::views::reverse) {
        const std::string_view word = std::string_view(line).substr(at, size);
        const auto eq = word.find('=');
        if (eq == npos || !std::ranges::contains(kCode, lower(word.substr(0, eq)))) continue;
        const std::string_view value = word.substr(eq + 1);
        const bool q = isQuoted(value);
        const std::string now = change(q ? value.substr(1, value.size() - 2) : value);
        out.replace(at + eq + 1, value.size(), q ? "\"" + now + "\"" : now);
    }
    return out;
}

// A line with the variable `from` called `to`: everywhere in a line that's code (set, if...), and
// in the rest only in braces, an option's condition and settings that are code.
std::string lineWithVariable(const std::string& line, std::string_view from, std::string_view to) {
    const std::string_view t = trim(line);
    if (t.empty() || t.starts_with('#') || !lower(line).contains(lower(from))) return line;
    static constexpr std::string_view kCode[]{"set", "add", "subtract", "toggle", "if", "else", "elif", "ask", "variable"};
    if (std::ranges::contains(kCode, lower(firstWord(t)))) return leading(line) + variableRenamed(t, from, to, true);
    std::string out = leading(line) + variableRenamed(t, from, to, false);
    // An option's condition: (if CONDITION) or (needs CONDITION: what it needs), before its arrow.
    const std::string low = lower(out);
    for (const std::string_view open : {" (if ", " (needs "}) {
        const auto at = low.rfind(open);
        if (at == npos) continue;
        const auto close = out.find(')', at);
        if (close == npos) continue;
        const std::size_t from0 = at + open.size();
        std::size_t until = close;
        if (open == " (needs ") until = std::min(close, out.find(':', from0));
        out.replace(from0, until - from0, variableRenamed(std::string_view(out).substr(from0, until - from0), from, to, true));
        break;
    }
    return codeSettings(out, [&](std::string_view value) { return variableRenamed(value, from, to, true); });
}

} // namespace

std::vector<StoryEditor::Found> StoryEditor::uses(std::string_view variable) const {
    std::vector<Found> out;
    const std::string marked = std::string(variable) + "\x7f";
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i)
            if (const std::string& line = files_[f].lines[i]; lineWithVariable(line, variable, marked) != line && lower(firstWord(line)) != "variable")
                out.push_back({.file = f, .line = i, .text = std::string(trim(line))});
    return out;
}

std::optional<std::size_t> StoryEditor::renameVariable(const std::string& from, const std::string& to) {
    static constexpr std::string_view kWords[]{"and", "or", "not", "is", "to", "yes", "no", "true", "false", "random", "from", "by", "at", "with", "if", "else"};
    const std::string name = lower(to);
    if (name == lower(from)) return 0;
    if (name.empty() || !letterByte(name[0]) || !std::ranges::all_of(name, nameChar) || name.ends_with('.') ||
        std::ranges::contains(kWords, name) || std::ranges::contains(variables(), name))
        return std::nullopt;
    remember({});
    std::size_t uses = 0;
    changeLines([&](std::size_t, std::size_t, const std::string& line) -> std::optional<std::string> {
        std::string now = lineWithVariable(line, from, to);
        if (now == line) return std::nullopt;
        if (lower(firstWord(trim(line))) != "variable") ++uses; // (its own line isn't a use)
        return now;
    });
    rescan();
    return uses;
}

void StoryEditor::scaleLayout(const std::function<std::string(const std::string&)>& scale, const std::string& key) {
    remember(key);
    std::string block; // the definition the lines indented under are in
    std::size_t inFile = SIZE_MAX;
    changeLines([&](std::size_t f, std::size_t, const std::string& line) -> std::optional<std::string> {
        if (f != std::exchange(inFile, f)) block.clear(); // (each file's own)
        if (!partLine(line)) return std::nullopt;
        if (indentOf(line) == 0) block = lower(firstWord(line));
        if (!(indentOf(line) == 0 ? block == "style" : block == "screen" || block == "animation")) return std::nullopt;
        std::string now = scale(line);
        return now != line ? std::optional(std::move(now)) : std::nullopt;
    });
    rescan();
}

namespace {

// `line` with the file `from` (in `folder`: images/, audio/, movies/ or fonts/) called `to`, where
// it's used: by its name without the extension (scene park, music rain, image=..., image "logo"), as
// a path ("fonts/Lora.ttf"), or for a character's picture (aiko_happy) as its expression. `bases`:
// each character's name and the name its pictures start with.
std::string assetRenamed(const std::string& line, std::string_view folder, const std::string& from, const std::string& to,
                         const std::vector<std::pair<std::string, std::string>>& bases) {
    auto stem = [](const std::string& file) { return lower(scriptName(stemOf(file))); }; // (what scripts call it)
    const std::string name = stem(from), now = stemOf(to), path = lower(std::string(folder) + from);
    const bool images = folder == "images/", sounds = folder == "audio/", movies = folder == "movies/";
    if (!lower(line).contains(name) && !(images && lower(line).contains(name.substr(name.rfind('_') + 1)))) return line; // (aiko_happy: show aiko happy)
    const auto spans = wordSpans(line);
    auto word = [&](std::size_t i) { return i < spans.size() ? lower(std::string_view(line).substr(spans[i].first, spans[i].second)) : std::string{}; };
    const std::string verb = word(0);
    // The expression a character's picture is: who it is (a character with that picture's start) and
    // the expression, when `to` starts the same way.
    auto expressionOf = [&](const std::string& who) -> std::optional<std::string> {
        for (const auto& [character, base] : bases)
            if (character == lower(unquote(who)) && name.starts_with(base + "_") && lower(now).starts_with(base + "_")) return now.substr(base.size() + 1);
        return std::nullopt;
    };
    std::vector<std::pair<std::size_t, std::string>> changes; // (word, its new text)
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const std::string w = word(i);
        if (isQuoted(w) && unquote(w) == path) { // a path: "fonts/Lora.ttf"
            changes.emplace_back(i, "\"" + std::string(folder) + to + "\"");
            continue;
        }
        if (const auto eq = w.find('='); eq != npos) {
            const std::string key = w.substr(0, eq), value = unquote(std::string_view(w).substr(eq + 1));
            const bool picture = key == "image" || key.ends_with("_image") || key == "face" || key == "shape";
            const bool sound = key == "blip" || key.ends_with("sound");
            if (value == name && ((images && picture) || (sounds && sound))) {
                const bool q = isQuoted(std::string_view(w).substr(eq + 1));
                changes.emplace_back(i, std::string(std::string_view(line).substr(spans[i].first, eq + 1)) + (q ? "\"" + now + "\"" : now));
            }
            continue;
        }
        const std::string plain = unquote(w);
        bool use = false;
        if (images) use = (i == 1 && plain == name && (verb == "scene" || verb == "show" || verb == "hide" || verb == "move" || verb == "gallery" || verb == "image")) ||
                          (i >= 2 && plain == name && word(i - 1) == "wipe" && word(i - 2) == "with") ||
                          (i == 2 && plain == name && verb == "config" && word(1) == "cursor");
        if (sounds) use = (i == 1 && plain == name && (verb == "music" || verb == "sound" || verb == "voice" || verb == "song")) ||
                          (i == 2 && plain == name && verb == "stop" && word(1) == "sound");
        if (movies) use = i == 1 && plain == name && (verb == "movie" || verb == "gallery");
        if (use) {
            changes.emplace_back(i, isQuoted(w) ? "\"" + now + "\"" : now);
        } else if (images && verb == "show" && i == 2 && !w.contains('=')) { // show aiko happy
            for (const auto& [character, base] : bases)
                if (character == lower(unquote(word(1))) && base + "_" + plain == name)
                    if (const auto e = expressionOf(word(1))) changes.emplace_back(i, *e);
        }
    }
    std::string out = line;
    for (const auto& [i, with] : changes | std::views::reverse) out.replace(spans[i].first, spans[i].second, with);
    // Someone speaking with an expression: Aiko happy: ... or Aiko (happy): ...
    if (std::string_view said; images && changes.empty() && speech(trim(out), &said)) {
        const std::string prefix(said);
        std::string who(prefix), expression;
        const bool brackets = prefix.ends_with(')') && prefix.contains('(');
        if (brackets) {
            who = trim(prefix.substr(0, prefix.rfind('(')));
            expression = trim(prefix.substr(prefix.rfind('(') + 1, prefix.size() - prefix.rfind('(') - 2));
        } else if (const auto space = prefix.rfind(' '); space != npos) {
            who = prefix.substr(0, space);
            expression = prefix.substr(space + 1);
        }
        for (const auto& [character, base] : bases)
            if (!expression.empty() && character == lower(who) && base + "_" + lower(expression) == name)
                if (const auto e = expressionOf(who)) {
                    out.replace(out.find(prefix), prefix.size(), brackets ? std::format("{} ({})", who, *e) : std::format("{} {}", who, *e));
                    break;
                }
    }
    return out;
}

} // namespace

std::optional<std::size_t> StoryEditor::renameAsset(std::string_view folder, const std::string& from, const std::string& to) {
    const std::string dot = from.substr(from.rfind('.') == npos ? from.size() : from.rfind('.'));
    const std::string name = to.contains('.') ? to : to + dot; // (the extension it had, if none is typed)
    if (name == from) return 0;
    const std::string there = root_ + std::string(folder);
    if (!usableFileName(name) || name.contains('/') || !fileExists(there + from) || (lower(name) != lower(from) && fileExists(there + name))) return std::nullopt;
    remember({});
    if (!SDL_RenamePath((there + from).c_str(), (there + name).c_str())) {
        undo_.pop_back();
        return std::nullopt;
    }
    undo_.back().renamed.emplace_back(there + from, there + name);
    const auto bases = pictureBases();
    const std::size_t count = changeLines([&](std::size_t, std::size_t, const std::string& line) -> std::optional<std::string> {
        std::string now = assetRenamed(line, folder, from, name, bases);
        return now != line ? std::optional(std::move(now)) : std::nullopt;
    });
    ++version_;
    rescan();
    return count;
}

std::vector<std::pair<std::string, std::string>> StoryEditor::pictureBases() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Character& c : characters()) {
        std::string base = lower(c.name);
        std::ranges::replace(base, ' ', '_');
        for (const std::string& word : rawWords(afterFirst(files_[c.file].lines[c.line])))
            if (lower(word).starts_with("image=")) base = lower(unquote(word.substr(6)));
        out.emplace_back(lower(c.name), base);
    }
    return out;
}

bool StoryEditor::jumpAtEnd(std::size_t scene, const std::string& to) {
    const Scene s = scenes_[scene];
    const auto all = rows(s);
    const Row* last = nullptr; // (its last line right under it)
    for (const Row& r : all)
        if (r.depth == 0) last = &r;
    const std::string verb = last ? lower(firstWord(last->text)) : std::string{};
    if (verb == "jump" || verb == "end") {
        setText(s, *last, "jump " + to, {});
        return true;
    }
    add(s, nullptr, false, {"jump " + to});
    return false;
}

const std::vector<StoryEditor::Link>& StoryEditor::links() const {
    if (links_.first == version_) return links_.second;
    std::vector<Link> out;
    for (std::size_t i = 0; i < scenes_.size(); ++i)
        for (const Row& row : rows(scenes_[i])) {
            if (row.kind == Kind::Jump) {
                const auto w = rawWords(row.text);
                if (w.size() >= 2) out.push_back({.from = i, .to = lower(w[1]), .call = lower(w[0]) == "call", .option = false, .line = row.line});
            } else if (row.kind == Kind::Option) {
                if (const OptionParts o = optionParts(row.text); !o.label.empty())
                    out.push_back({.from = i, .to = lower(o.label), .call = false, .option = true, .line = row.line});
            }
        }
    links_ = {version_, std::move(out)};
    return links_.second;
}

void StoryEditor::removeLink(const Link& link) {
    if (link.from >= scenes_.size()) return;
    const Scene s = scenes_[link.from];
    const auto all = rows(s);
    const auto row = std::ranges::find(all, link.line, &Row::line);
    if (row == all.end()) return;
    if (!link.option) return remove(s, *row);
    auto f = fields(*row);
    if (const auto to = std::ranges::find(f, std::string_view("Goes to"), &Field::name); to != f.end()) to->value.clear();
    setText(s, *row, lineOf(Kind::Option, f), {});
}

std::size_t StoryEditor::addCharacter() {
    const std::string name = uniqueName("Someone", [&](const std::string& n) { return isCharacter(n); }, " ");
    remember({});
    auto& lines = files_[0].lines;
    const std::string line = std::format("character {}", name.contains(' ') ? "\"" + name + "\"" : name);
    std::optional<std::size_t> last; // the main file's last character line
    for (const Character& c : characters())
        if (c.file == 0) last = c.line;
    if (last) {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*last + 1), line);
    } else if (const auto label = std::ranges::find_if(lines, [](const std::string& l) { return lower(firstWord(l)) == "label"; });
               label != lines.end()) {
        lines.insert(lines.insert(label, ""), line);
    } else {
        lines.push_back(line);
    }
    write(0);
    rescan();
    const auto now = characters();
    return static_cast<std::size_t>(std::ranges::find(now, name, &Character::name) - now.begin());
}

void StoryEditor::setCharacter(const Character& c, const std::vector<Field>& f, const std::string& key) {
    const auto it = std::ranges::find(f, std::string_view("Name"), &Field::name);
    const std::string name = it != f.end() ? std::string(trim(it->value)) : std::string{};
    if (name.empty()) return;
    const std::string line = formLine(definitionForm("character"), f);
    if (files_[c.file].lines[c.line] == line) return;
    remember(key);
    files_[c.file].lines[c.line] = line;
    if (lower(name) != lower(c.name)) renameSpeaker(c.name, name);
    writeAll();
    rescan();
}

void StoryEditor::removeCharacter(const Character& c) { eraseLines(c.file, c.line, c.line + 1); }

std::optional<std::size_t> StoryEditor::moveCharacter(std::size_t c, std::size_t to) {
    return moveRecord(characters(), c, to, false, [](const Character& who) { return who.line + 1; }, [&] { return characters(); });
}

// The lines `from` says, and shows and hides of them, now say `to`.
void StoryEditor::renameSpeaker(const std::string& from, const std::string& to) {
    const std::string low = lower(from);
    for (File& f : files_)
        for (std::string& line : f.lines) {
            const std::string indent = leading(line);
            const std::string text(trim(line));
            std::string_view prefix;
            if (speech(text, &prefix)) {
                const std::string p = lower(prefix);
                if (p == low || p.starts_with(low + " ") || p.starts_with(low + "("))
                    line = indent + to + text.substr(from.size());
            } else if (const std::string verb = lower(firstWord(text)); verb == "show" || verb == "hide") {
                auto words = rawWords(text);
                if (words.size() > 1 && lower(unquote(words[1])) == low) {
                    words[1] = to.contains(' ') ? "\"" + to + "\"" : to;
                    line = indent + joined(words);
                }
            }
        }
}

// --- Screens ---------------------------------------------------------------------------------

std::string StoryEditor::Part::option(std::string_view name) const {
    const auto it = std::ranges::find(options, name, &std::pair<std::string, std::string>::first);
    return it != options.end() ? it->second : std::string{};
}

void StoryEditor::Part::set(std::string_view name, const std::string& value) {
    const auto it = std::ranges::find(options, name, &std::pair<std::string, std::string>::first);
    if (value.empty()) {
        if (it != options.end()) options.erase(it);
    } else if (it != options.end()) {
        it->second = value;
    } else {
        options.emplace_back(name, value);
    }
}

StoryEditor::Part StoryEditor::part(const Screen& screen, std::size_t line) const {
    const auto& lines = files_[screen.file].lines;
    Part p{.line = line, .end = line + 1};
    // The parts in it (indented under it), and the ones it's in.
    for (std::size_t i = line + 1; i < screen.end && (!partLine(lines[i]) || indentOf(lines[i]) > indentOf(lines[line])); ++i)
        if (partLine(lines[i])) p.end = i + 1;
    for (std::size_t i = line, in = static_cast<std::size_t>(indentOf(lines[line])); i-- > screen.line + 1;)
        if (partLine(lines[i]) && static_cast<std::size_t>(indentOf(lines[i])) < in) {
            ++p.depth;
            in = static_cast<std::size_t>(indentOf(lines[i]));
        }
    const auto [body, note] = splitNote(lines[line]);
    p.note = note;
    const auto words = rawWords(body);
    if (words.empty()) return p;
    p.kind = lower(words[0]);
    std::size_t i = 1;
    if (i < words.size() && (words[i].starts_with('"') || !words[i].contains('='))) {
        p.quoted = isQuoted(words[i]);
        p.text = unquote(words[i++]);
    }
    for (; i < words.size(); ++i)
        if (const auto eq = words[i].find('='); eq != npos && eq > 0 && !words[i].starts_with('"'))
            p.options.emplace_back(lower(words[i].substr(0, eq)), unquote(words[i].substr(eq + 1)));
        else
            p.words.push_back(words[i]);
    return p;
}

const std::vector<StoryEditor::Part>& StoryEditor::parts(const Screen& screen) const {
    auto& kept = parts_[{screen.file, screen.line}];
    if (kept.first == version_) return kept.second;
    std::vector<Part> out;
    for (std::size_t i = screen.line + 1; i < screen.end; ++i)
        if (partLine(files_[screen.file].lines[i])) out.push_back(part(screen, i));
    kept = {version_, std::move(out)};
    return kept.second;
}

std::string StoryEditor::lineOf(const Part& p) {
    std::string out = p.kind;
    if (p.quoted || !p.text.empty()) out += " " + (p.quoted ? "\"" + p.text + "\"" : p.text);
    for (const auto& [name, value] : p.options)
        if (!value.empty()) out += " " + name + "=" + written(value);
    for (const std::string& word : p.words) out += " " + word;
    if (!p.note.empty()) out += "   " + p.note;
    return out;
}

void StoryEditor::setPart(const Screen& screen, const Part& p, const std::string& key) {
    std::string& line = files_[screen.file].lines[p.line];
    const std::string text = leading(line) + lineOf(p);
    if (line == text) return;
    remember(key);
    line = text;
    write(screen.file);
    rescan();
}

namespace {

// The indentation of a screen's parts that are in no other part.
std::string partIndent(const std::vector<std::string>& lines, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i)
        if (partLine(lines[i])) return leading(lines[i]);
    return std::string(kIndent, ' ');
}

} // namespace

std::size_t StoryEditor::addPart(const Screen& screen, const Part* into, const std::string& text, const std::vector<std::string>& needs) {
    remember({});
    auto& lines = files_[screen.file].lines;
    std::size_t at = into ? into->end : screen.end;
    const std::string indent = into ? leading(lines[into->line]) + std::string(kIndent, ' ') : partIndent(lines, screen.line + 1, screen.end);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), indent + text);
    if (!needs.empty()) { // (the colours and styles it uses that the game lacks: above its screen)
        std::vector<std::string> block = needs;
        block.emplace_back();
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(screen.line), block.begin(), block.end());
        at += block.size();
    }
    write(screen.file);
    rescan();
    return at;
}

std::size_t StoryEditor::addParts(const Screen& screen, const Part* after, const std::vector<std::string>& text) {
    remember({});
    auto& lines = files_[screen.file].lines;
    const std::size_t at = after ? after->end : screen.end;
    const std::string indent = after ? leading(lines[after->line]) : partIndent(lines, screen.line + 1, screen.end);
    int least = INT_MAX; // (the lines' own indentation, under the first's)
    for (const std::string& line : text)
        if (!blank(line)) least = std::min(least, indentOf(line));
    std::size_t n = 0;
    for (const std::string& line : text)
        if (!blank(line))
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at + n++), indent + line.substr(static_cast<std::size_t>(std::min(least, indentOf(line)))));
    write(screen.file);
    rescan();
    return at;
}

std::vector<std::string> StoryEditor::partLines(const Screen& screen, const Part& p) const {
    const auto& lines = files_[screen.file].lines;
    const auto own = static_cast<std::size_t>(indentOf(lines[p.line]));
    std::vector<std::string> out;
    for (std::size_t i = p.line; i < p.end; ++i)
        if (partLine(lines[i])) out.push_back(lines[i].substr(std::min(own, static_cast<std::size_t>(indentOf(lines[i])))));
    return out;
}

std::size_t StoryEditor::pasteScreen(std::vector<std::string> lines) {
    const std::string base(firstWord(afterFirst(lines.front())));
    const std::string name = uniqueName(base, [&](const std::string& n) { return hasScreen(n); });
    const std::string_view settings = afterFirst(afterFirst(lines.front()));
    lines.front() = "screen " + name + (settings.empty() ? "" : " " + std::string(settings));
    return addScreen({}, lines);
}

void StoryEditor::removePart(const Screen& screen, const Part& p) {
    removeParts(screen, {p.line});
}

void StoryEditor::removeParts(const Screen& screen, const std::vector<std::size_t>& lines) {
    if (lines.empty()) return;
    remember({});
    std::vector<std::pair<std::size_t, std::size_t>> blocks; // (each with the parts in it; one in another chosen part goes with that one)
    for (const std::size_t line : lines) {
        const Part p = part(screen, line);
        blocks.emplace_back(p.line, p.end);
    }
    std::ranges::sort(blocks);
    auto& file = files_[screen.file].lines;
    std::size_t covered = 0;
    std::vector<std::pair<std::size_t, std::size_t>> gone;
    for (const auto& [from, to] : blocks)
        if (from >= covered) {
            gone.emplace_back(from, to);
            covered = to;
        }
    for (const auto& [from, to] : gone | std::views::reverse) // (from the bottom, so the others stay where they are)
        file.erase(file.begin() + static_cast<std::ptrdiff_t>(from), file.begin() + static_cast<std::ptrdiff_t>(to));
    write(screen.file);
    rescan();
}

std::optional<std::size_t> StoryEditor::movePart(const Screen& screen, const Part& p, std::size_t to, int depth) {
    const auto all = parts(screen);
    if (to > all.size()) return std::nullopt;
    const std::size_t at = to < all.size() ? all[to].line : screen.end;
    if (at > p.line && at < p.end) return std::nullopt; // (into itself)
    auto& lines = files_[screen.file].lines;
    const std::string base = partIndent(lines, screen.line + 1, screen.end);
    const int was = indentOf(lines[p.line]), now = static_cast<int>(base.size()) + depth * kIndent;
    if ((at == p.line || at == p.end) && was == now) return std::nullopt; // (where it is)
    remember({});
    const std::size_t moved = moveLines(screen.file, p.line, p.end, at, false);
    for (std::size_t i = moved; i < moved + (p.end - p.line); ++i) // (in further, or out: the parts in it too)
        if (!blank(lines[i])) {
            const int indent = std::max(0, indentOf(lines[i]) + now - was);
            lines[i] = std::string(static_cast<std::size_t>(indent), ' ') + std::string(trim(lines[i]));
        }
    write(screen.file);
    rescan();
    return moved;
}

std::size_t StoryEditor::addScreen(const std::vector<std::string>& needs, const std::vector<std::string>& lines) {
    // The screens' file, made (empty, so undo empties it) if it isn't there.
    auto file = std::ranges::find_if(files_, [](const File& f) { return lower(f.path) == "scripts/screens.vnscript"; });
    if (file == files_.end()) {
        files_.push_back({.path = "scripts/screens.vnscript", .lines = {}});
        file = files_.end() - 1;
    }
    const auto f = static_cast<std::size_t>(file - files_.begin());
    remember({});
    auto& to = files_[f].lines;
    if (to.empty()) to.emplace_back("# The game's screens: its menus, and its own textboxes and images.");
    for (const auto* block : {&needs, &lines}) {
        if (block->empty()) continue;
        if (!blank(to.back())) to.emplace_back();
        to.insert(to.end(), block->begin(), block->end());
    }
    write(f);
    include("screens"); // (main.vnscript reads it)
    rescan();
    const std::string name(firstWord(afterFirst(lines.front())));
    // A title screen shows when the story opens it: at the start of label start, unless it does already.
    if (const auto start = std::ranges::find(scenes_, std::string("start"), &Scene::name); lower(name) == "title" && start != scenes_.end()) {
        auto& in = files_[start->file].lines;
        const auto opens = [](const std::string& l) { return lower(trim(l)) == "open title"; };
        if (!std::any_of(in.begin() + static_cast<std::ptrdiff_t>(start->line), in.begin() + static_cast<std::ptrdiff_t>(start->end), opens)) {
            std::string indent = "    ";
            for (std::size_t i = start->line + 1; i < start->end; ++i)
                if (!blank(in[i])) {
                    indent = leading(in[i]);
                    break;
                }
            in.insert(in.begin() + static_cast<std::ptrdiff_t>(start->line) + 1, indent + "open title");
            write(start->file);
            rescan();
        }
    }
    for (std::size_t i = screens_.size(); i-- > 0;)
        if (lower(screens_[i].name) == lower(name)) return i;
    return 0;
}

std::optional<std::size_t> StoryEditor::moveScreen(std::size_t screen, std::size_t to) {
    return moveRecord(screens_, screen, to, true, [](const Screen& s) { return s.end; }, [&] { return screens_; });
}

bool StoryEditor::renameScreen(std::size_t screen, const std::string& name, const std::string& key) {
    const std::string from = screens_[screen].name;
    if (lower(name) == lower(from)) return true;
    if (!validName(name) || hasScreen(name)) return false;
    remember(key);
    const Screen s = screens_[screen];
    Part header = part(s, s.line);
    header.text = name;
    files_[s.file].lines[s.line] = lineOf(header);
    changeLines([&](std::size_t f, std::size_t i, const std::string& line) -> std::optional<std::string> {
        if (f == s.file && i == s.line) return std::nullopt;
        std::string after = screenRenamed(line, from, name);
        return after != line ? std::optional(std::move(after)) : std::nullopt;
    }, s.file);
    rescan();
    return true;
}

void StoryEditor::removeScreen(std::size_t screen) {
    remember({});
    const Screen s = screens_[screen];
    auto& lines = files_[s.file].lines;
    const std::size_t from = s.line > 0 && blank(lines[s.line - 1]) ? s.line - 1 : s.line;
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(s.end));
    // The keys and buttons that only opened it go too (a key opening a screen that isn't there is a mistake).
    const std::string opens = "open " + lower(s.name);
    for (std::size_t f = 0; f < files_.size(); ++f) {
        const auto before = files_[f].lines.size();
        std::erase_if(files_[f].lines, [&](const std::string& line) {
            const auto words = rawWords(splitNote(line).first);
            if (words.empty()) return false;
            const std::string kind = lower(words[0]);
            if (!(indentOf(line) == 0 ? kind == "control" : kind == "button" || kind == "key")) return false;
            return std::ranges::any_of(words, [&](const std::string& w) { return lower(w).starts_with("action=") && lower(unquote(w.substr(7))) == opens; });
        });
        if (f == s.file || files_[f].lines.size() != before) write(f);
    }
    rescan();
}

std::vector<std::string> StoryEditor::defined(std::string_view what) const {
    std::vector<std::string> out;
    for (const File& f : files_)
        for (const std::string& line : f.lines)
            if (indentOf(line) == 0 && lower(firstWord(line)) == what)
                if (const std::string name(firstWord(afterFirst(line))); !name.empty() && !std::ranges::contains(out, name)) out.push_back(name);
    return out;
}

bool StoryEditor::defines(std::string_view what, std::string_view name) const {
    for (const File& f : files_)
        for (const std::string& line : f.lines)
            if (indentOf(line) == 0 && lower(firstWord(line)) == what && lower(firstWord(afterFirst(line))) == lower(name)) return true;
    return false;
}

// --- Things listed by name ---------------------------------------------------------------------

const std::vector<StoryEditor::Definition>& StoryEditor::definitions(std::string_view kind) const {
    auto& kept = definitions_[std::string(kind)];
    if (kept.first == version_) return kept.second;
    std::vector<Definition> out;
    const bool titled = kind == "ending" || kind == "achievement" || kind == "tip"; // (else it's the story reaching one)
    for (std::size_t f = 0; f < files_.size(); ++f)
        for (std::size_t i = 0; i < files_[f].lines.size(); ++i) {
            const std::string& line = files_[f].lines[i];
            if (indentOf(line) != 0 || lower(firstWord(line)) != kind) continue;
            const auto w = rawWords(splitNote(afterFirst(line)).first);
            if (w.empty() || (titled && w.size() < 2)) continue;
            std::string name = unquote(w[0]);
            if (kind == "font" && (w.size() == 1 || name.contains('/') || name.contains('.'))) name = "default"; // (font FILE)
            out.push_back({.file = f, .line = i, .end = blockEnd(f, i), .name = name});
        }
    kept = {version_, std::move(out)};
    return kept.second;
}

std::vector<std::string> StoryEditor::linesOf(const Definition& d) const {
    std::vector<std::string> out;
    for (std::size_t i = d.line; i < d.end; ++i)
        if (!blank(files_[d.file].lines[i])) out.emplace_back(trim(files_[d.file].lines[i]));
    return out;
}

std::size_t StoryEditor::addDefinition(std::string_view kind, const std::vector<std::string>& lines) {
    const auto existing = definitions(kind);
    remember({});
    std::size_t file = 0, at = 0;
    if (!existing.empty()) {
        file = existing.back().file;
        at = existing.back().end;
    } else { // main.vnscript, before its first scene (and its notes), or at its end
        auto& main = files_[0].lines;
        at = main.size();
        for (std::size_t i = 0; i < main.size(); ++i)
            if (indentOf(main[i]) == 0 && lower(firstWord(main[i])) == "label") {
                at = i;
                while (at > 0 && trim(main[at - 1]).starts_with('#')) --at;
                break;
            }
    }
    auto& to = files_[file].lines;
    std::vector<std::string> put;
    put.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) put.push_back((i > 0 ? std::string(kIndent, ' ') : std::string{}) + lines[i]);
    if (existing.empty() && at < to.size() && !blank(to[at])) put.emplace_back();
    to.insert(to.begin() + static_cast<std::ptrdiff_t>(at), put.begin(), put.end());
    write(file);
    rescan();
    const auto now = definitions(kind);
    for (std::size_t i = 0; i < now.size(); ++i)
        if (now[i].file == file && now[i].line == at) return i;
    return 0;
}

void StoryEditor::setDefinition(const Definition& d, const std::vector<std::string>& lines, const std::string& key) {
    if (lines.empty() || trim(afterFirst(trim(lines.front()))).empty()) return; // (its name emptied in the form: it keeps the one it has)
    auto& file = files_[d.file].lines;
    std::vector<std::string> put;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (i == 0 || !blank(lines[i])) put.push_back((i > 0 ? std::string(kIndent, ' ') : std::string{}) + std::string(trim(lines[i])));
    if (std::equal(file.begin() + static_cast<std::ptrdiff_t>(d.line), file.begin() + static_cast<std::ptrdiff_t>(d.end), put.begin(), put.end())) return;
    remember(key);
    file.erase(file.begin() + static_cast<std::ptrdiff_t>(d.line), file.begin() + static_cast<std::ptrdiff_t>(d.end));
    file.insert(file.begin() + static_cast<std::ptrdiff_t>(d.line), put.begin(), put.end());
    write(d.file);
    rescan();
}

void StoryEditor::removeDefinition(const Definition& d) { eraseLines(d.file, d.line, d.end); }

std::optional<std::size_t> StoryEditor::moveDefinition(std::string_view kind, std::size_t index, std::size_t to) {
    return moveRecord(definitions(kind), index, to, false, [](const Definition& d) { return d.end; }, [&] { return definitions(kind); });
}

bool StoryEditor::renameDefinition(std::string_view kind, std::size_t index, const std::string& name, const std::string& key) {
    const auto all = definitions(kind);
    const Definition& d = all[index];
    if (sameName(name, d.name)) return true;
    if (!validName(name) || (kind == "font" && d.name == "default") || hasNamed(all, name)) return false;
    remember(key);
    { // its own line
        std::string& line = files_[d.file].lines[d.line];
        auto w = rawWords(line);
        if (w.size() > 1) w[1] = name;
        line = joined(w);
    }
    // Where it's used: settings that name it (style=, parent=, ..._style=; colours; font=; like=,
    // image=), places (at NAME, to NAME), keys (control parts), and the story reaching it.
    const std::string from = lower(d.name);
    auto names = [&](std::string_view setting) {
        if (kind == "style") return setting == "style" || setting == "parent" || setting.ends_with("_style");
        if (kind == "color")
            return setting.contains("color") || setting.contains("bg") || setting.contains("border") || setting.contains("tint") || setting == "bottom" ||
                   setting == "fill" || setting == "track" || setting == "knob";
        if (kind == "font") return setting == "font";
        if (kind == "atlas") return setting == "like" || setting == "image";
        if (kind == "animation") return setting == "animation" || setting.ends_with("_animation"); // (a part's, a click's, a screen's)
        return false;
    };
    const auto bases = kind == "picture" ? pictureBases() : std::vector<std::pair<std::string, std::string>>{}; // (a sub-picture is used like a picture file)
    changeLines([&](std::size_t f, std::size_t i, const std::string& line) -> std::optional<std::string> {
        if (f == d.file && i == d.line) return std::nullopt;
        const std::string indent = leading(line);
        auto w = rawWords(line);
        bool changed = false;
        for (std::size_t j = 0; j < w.size(); ++j) {
            const auto eq = w[j].find('=');
            if (eq != npos && eq > 0 && names(lower(w[j].substr(0, eq))) && lower(unquote(w[j].substr(eq + 1))) == from) {
                w[j] = w[j].substr(0, eq + 1) + name;
                changed = true;
            } else if (j > 0 && lower(w[j]) == from &&
                       ((kind == "position" && (lower(w[j - 1]) == "at" || lower(w[j - 1]) == "to")) ||
                        (kind == "control" && j == 1 && indentOf(line) > 0 && lower(w[0]) == "control") ||
                        ((kind == "ending" || kind == "achievement" || kind == "tip") && j == 1 && w.size() == 2 && indentOf(line) > 0 &&
                         lower(w[0]) == kind))) {
                w[j] = name;
                changed = true;
            }
        }
        std::string now = changed ? indent + joined(w) : line;
        if (kind == "picture") now = assetRenamed(now, "images/", d.name, name, bases);
        if (kind == "tip") { // {tip NAME}words{/tip}
            const std::string tag = "{tip " + from + "}";
            for (std::size_t at = lower(now).find(tag); at != npos; at = lower(now).find(tag, at + 1)) now.replace(at, tag.size(), "{tip " + name + "}");
        }
        return now != line ? std::optional(std::move(now)) : std::nullopt;
    }, d.file);
    rescan();
    return true;
}

// --- The game's settings ---------------------------------------------------------------------

bool StoryEditor::hasMain() const {
    return !files_.empty() && lower(files_[0].path) == "scripts/main.vnscript";
}

std::optional<std::size_t> StoryEditor::settingLine(std::string_view name) const {
    if (!hasMain()) return std::nullopt;
    if (settingLines_.first != version_) { // (each setting's line, the last one counting: found once a version)
        settingLines_ = {version_, {}};
        for (std::size_t i = 0; i < files_[0].lines.size(); ++i)
            if (const auto c = configOf(files_[0].lines[i])) settingLines_.second.insert_or_assign(c->first, i);
    }
    const auto it = settingLines_.second.find(name);
    return it != settingLines_.second.end() ? std::optional(it->second) : std::nullopt;
}

bool StoryEditor::isSetting(std::size_t line) const {
    return hasMain() && line < files_[0].lines.size() && configOf(files_[0].lines[line]);
}

std::optional<std::string> StoryEditor::setting(std::string_view name) const {
    const auto line = settingLine(name);
    if (!line) return std::nullopt;
    const auto config = configOf(splitNote(files_[0].lines[*line]).first); // (not its note)
    if (!config) return std::nullopt;
    std::vector<std::string> words;
    for (const std::string& word : rawWords(config->second)) words.push_back(unquote(word));
    return joined(words);
}

void StoryEditor::setSetting(std::string_view name, const std::string& value, bool quote, const std::string& key) {
    if (!hasMain()) return;
    auto& lines = files_[0].lines;
    const auto at = settingLine(name);
    const std::string v(trim(value));
    const std::string_view note = at ? splitNote(lines[*at]).second : std::string_view{}; // (kept at its end)
    const std::string line = std::format("config {} {}", name, quote ? "\"" + v + "\"" : v) + (note.empty() ? "" : "  " + std::string(note));
    if (at ? lines[*at] == line : v.empty()) return;
    remember(key);
    if (v.empty()) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*at));
        removed_ = {std::string(name), *at};
    } else if (at) {
        lines[*at] = line;
    } else if (removed_.first == name && removed_.second <= lines.size()) { // (a box emptied to type in it again)
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(removed_.second), line);
    } else { // after the other config lines, or the notes at the top
        std::optional<std::size_t> last;
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (configOf(lines[i])) last = i;
        std::size_t to = 0;
        if (last) to = *last + 1;
        else
            while (to < lines.size() && trim(lines[to]).starts_with('#')) ++to;
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(to), line);
        if (!last && to + 1 < lines.size() && !blank(lines[to + 1])) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(to + 1), "");
    }
    write(0);
    rescan();
}

namespace {

// The Settings tab's row for a player setting's default ("setting vsync" or "vsync": default.vsync), if any.
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
    std::string list; // (its box's buttons as a list, each with what it says when that's more than the word; else what its box suggests)
    if (row) {
        const StoryEditor::BoxKind& kind = StoryEditor::kindOf(row->box);
        if (!kind.choices.empty()) {
            const auto choices = wordsOf(kind.choices), labels = wordsOf(kind.labels, '|');
            for (std::size_t i = 0; i < choices.size(); ++i) {
                std::string said(i < labels.size() ? labels[i] : std::string_view{});
                if (!said.empty()) said[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(said[0])));
                list += std::format("{}{}{}", list.empty() ? "" : "|", choices[i], said.empty() || said == choices[i] ? "" : ":" + said);
            }
        } else {
            list = std::string(row->suggest.empty() ? kind.suggest : row->suggest);
        }
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
            if (a.group == "settings" && !settings) { // (the settings themselves, from the Settings tab's rows, before the resets)
                settings = true;
                for (const Setting& s : kSettings) { // (what each goes to: settingOptions(), once its box is asked for: the kinds of box aren't known yet here)
                    if (!s.name.starts_with("default.")) continue;
                    out.emplace_back("settings", kept("setting " + std::string(s.name.substr(8))), s.help, "To", Box::SettingChoice);
                    if (s.name == "default.fullscreen") out.push_back(kWindowSizeKind);
                }
            }
            out.push_back(a);
        }
        return out;
    }();
    return kinds;
}

// The languages as a box's list, by name (the code comes with the name: languageCode).
const std::string& languageList() {
    static const std::string list = [] {
        std::vector<std::string> entries;
        for (const Language& l : kLanguages) entries.emplace_back(l.name);
        std::ranges::sort(entries);
        return joined(entries, 0, "|");
    }();
    return list;
}

} // namespace

// The rows of every table whose box has no kind (buttons of its own or not): none, so a padding, a
// title or a fade is the same box in every form (the harness checks).
std::vector<std::string> StoryEditor::boxesWithoutKind() {
    std::vector<std::string> out;
    auto slots = [&](std::string_view table, std::span<const Slot> rows) {
        for (const Slot& s : rows)
            if (s.box == Box::None && !s.rich) out.push_back(std::format("{}: {}", table, s.name));
    };
    for (const CommandForm& f : kForms) slots(f.verbs, f.slots);
    slots("character", kCharacterSlots);
    for (const DefinitionKind& k : kDefinitionKinds) slots(k.kind, k.slots);
    auto options = [&](std::string_view table, std::span<const PartOption> rows) {
        for (const PartOption& o : rows)
            if (o.box == Box::None && !o.is) out.push_back(std::format("{}: {}", table, o.key));
    };
    options("screen", kScreenOptions);
    options("part", kPartOptions);
    for (const Setting& s : kSettings)
        if (!s.name.empty() && s.box == Box::None) out.push_back(std::format("setting: {}", s.name));
    for (const ActionKind& a : actionKinds())
        if (!a.detail.empty() && a.box == Box::None) out.push_back(std::format("action: {}", a.verb));
    for (const StepKind& s : kStepKinds)
        if (s.box == Box::None) out.push_back(std::format("step: {}", s.key));
    return out;
}

// The helpers the other developer files use too (DeveloperShared.hpp declares them, with what they do).
namespace dev {

std::string leading(std::string_view line) {
    return std::string(line.substr(0, std::min(line.find_first_not_of(" \t"), line.size())));
}

bool blank(std::string_view line) {
    return trim(line).empty();
}

bool partLine(std::string_view line) {
    const std::string_view text = trim(line);
    return !text.empty() && !text.starts_with('#');
}

std::vector<std::string> linesOf(std::string_view text) {
    std::vector<std::string> out;
    for (auto&& part : std::views::split(text, '\n')) {
        std::string line(part.begin(), part.end());
        if (line.ends_with('\r')) line.pop_back();
        out.push_back(std::move(line));
    }
    return out;
}

void clampIndex(std::optional<std::size_t>& index, std::size_t count) {
    if (index && *index >= count) index.reset();
}

std::string fieldValue(const std::vector<StoryEditor::Field>& fields, std::string_view name) {
    const auto it = std::ranges::find(fields, name, &StoryEditor::Field::name);
    return it != fields.end() ? std::string(trim(it->value)) : std::string{};
}

std::string_view afterFirst(std::string_view s) {
    s = trim(s);
    const auto space = s.find_first_of(" \t");
    return space == npos ? std::string_view{} : trim(s.substr(space));
}

bool isWordChar(char c) {
    return letterOrDigitByte(c) || c == '_' || c == '-';
}

std::string joined(const std::vector<std::string>& words, std::size_t from, std::string_view sep) {
    std::string out;
    for (std::size_t i = from; i < words.size(); ++i) {
        if (!out.empty()) out += sep;
        out += words[i];
    }
    return out;
}

std::string unitText(float v) {
    return toString(number(v));
}

float placeFor(float at, float size, float span) {
    const float room = span - size, lo = std::min(0.0f, room), hi = std::max(0.0f, room);
    if (at >= lo && at <= hi) return room != 0.0f ? 2.0f * at / room - 1.0f : 0.0f;
    if (size <= 0.0f) return span != 0.0f ? 2.0f * at / span - 1.0f : 0.0f;
    if (room >= 0.0f) return at < 0.0f ? at / size - 1.0f : 1.0f + (at - room) / size;
    return at > 0.0f ? 1.0f + (at - room) / size : at / size - 1.0f; // (bigger than its box)
}

std::string written(const std::string& value) {
    return value.find_first_of(" ;=#") != npos ? "\"" + value + "\"" : value;
}

bool validName(std::string_view s) {
    return !s.empty() && (letterByte(s[0]) || s[0] == '_') && std::ranges::all_of(s, isWordChar);
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
        addValueFields(out, n, i == 0 ? label : "What", i == 0 ? hint : "", c.what, box, variables, true);
        if (c.what.empty() && i == 0) return;
        out.push_back({.name = n + " is", .value = c.is, .label = "Is", .choices = std::string(kComparisons), .labels = std::string(kComparisonLabels)});
        if (c.is != "yes" && c.is != "no") addValueFields(out, n + " value", "Value", "", c.value, box, variables);
        out.push_back({.name = n + " also", .value = c.then.empty() ? "nothing" : c.then, .label = "Also", .box = Box::Joiner, .choices = "nothing and or"});
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
                     std::string_view actions, const std::vector<std::string>& variables) {
    auto list = actionsOf(actions);
    list.emplace_back();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string n = numbered(name, i);
        const auto [verb, rest] = list[i].empty() ? std::pair<std::string, std::string>{} : actionParts(list[i]);
        const ActionKind* kind = actionKind(verb);
        const ActionGroup* group = kind ? actionGroup(kind->group) : nullptr;
        out.push_back({.name = n, .value = group ? std::string(group->word) : std::string{}, .label = i == 0 ? std::string(label) : "And then", .box = Box::Action,
                       .hint = i == 0 ? std::string(hint) : std::string("None")});
        out.push_back({.name = n + " was", .value = verb, .hidden = true});
        if (group && group->box != Box::None)
            out.push_back({.name = n + " how", .value = std::string(kindWord(*kind)), .label = std::string(group->how), .box = group->box, .beside = true});
        if (group && group->word == "variable") { // (the variable, and the value it's set to or the amount: boxes of their own)
            const auto [var, val] = variableParts(verb, rest);
            out.push_back({.name = n + " variable", .value = var, .label = "Variable", .box = Box::Variable, .beside = true});
            if (verb != "toggle")
                out.push_back({.name = n + " value", .value = valueShown(val, variables), .label = verb == "set" ? "Value" : "Amount", .box = verb == "set" ? Box::VariableValue : Box::Number,
                               .beside = true});
        } else if (kind && kind->verb == kWindowSizeKind.verb) { // (a width and a height: boxes of their own)
            const auto words = rawWords(rest);
            out.push_back({.name = n + " what", .value = words.empty() ? std::string{} : words[0], .label = "Width", .box = Box::Pixels, .beside = true});
            out.push_back({.name = n + " height", .value = words.size() < 2 ? std::string{} : words[1], .label = "Height", .box = Box::Pixels, .beside = true});
        } else if (kind && !kind->detail.empty()) {
            out.push_back({.name = n + " what", .value = rest, .label = std::string(kind->detail), .box = kind->box, .hint = std::string(kind->hint), .context = std::string(kind->context),
                           .suggest = std::string(kind->box == Box::SettingChoice ? settingOptions(kind->verb) : kind->suggest), .beside = true});
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
            else if (verb == "toggle" || val.empty()) action = verb + " " + var;
            else if (verb == "set") action = "set " + var + " to " + valueWritten(val, variables);
            else action = verb + " " + valueWritten(val, variables) + (verb == "add" ? " to " : " from ") + var;
        } else {
            std::string rest = fieldValue(fields, n + " was") == verb ? std::string(trim(fieldValue(fields, n + " what"))) : std::string{};
            if (kind->verb == kWindowSizeKind.verb) { // (its width and height)
                const std::string height(trim(fieldValue(fields, n + " height")));
                if (!rest.empty() && !height.empty()) rest += " " + height;
            } else if (percentSetting(verb)) {
                rest = percentText(rest); // (85 -> 85%, as the Settings tab writes it)
            }
            action = verb == "other" ? rest : rest.empty() ? verb : verb + " " + rest;
        }
        if (!action.empty()) out += (out.empty() ? "" : "; ") + action;
    }
    return out;
}

std::string withoutTags(std::string_view value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto close = value[i] == '{' ? value.find('}', i) : npos;
        if (close == npos) {
            out += value[i];
            continue;
        }
        const std::string_view tag = value.substr(i + 1, close - i - 1);
        const std::string_view word = tag.substr(tag.starts_with('/') ? 1 : 0, tag.find(' ') == npos ? npos : tag.find(' ') - (tag.starts_with('/') ? 1 : 0));
        if (std::ranges::contains(kTextTags, lower(word))) i = close;
        else out += value[i];
    }
    return out;
}

std::string parentFolder(const std::string& root) {
    const auto slash = root.find_last_of('/', root.size() - 2);
    return slash == npos ? std::string{} : root.substr(0, slash + 1);
}

} // namespace dev

} // namespace vn
