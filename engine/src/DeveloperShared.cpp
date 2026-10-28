// The developer tools' helpers that more than one tab uses (DeveloperShared.hpp declares them): events
// at a place, the clipboard, spelling, problems and where they are, and the like.
#include "DeveloperShared.hpp"

namespace vn {

namespace {

// A colour as a hint says it: its name, or what it's most like (never a code); none (see-through) as None.
std::string colorText(SDL_Color c, const Palette& palette) {
    return c.a == 0 ? "None" : colorName(c, &palette);
}

// A style's value for a setting, as a form shows it in grey where a box is empty (what the part or
// style gets from its style then): colours by their name, sizes as numbers, nothing as None; empty
// for a setting that isn't a style's.
std::string styleValueText(const Style& s, std::string_view key, const Palette& palette) {
    auto color = [&](SDL_Color c) { return colorText(c, palette); };
    auto name = [](const std::string& n) { return n.empty() ? std::string("None") : n; };
    auto state = [&](const Style::State& st, std::string_view what) { // (a look's own value, else the usual one)
        return what == "color"  ? color(st.color.value_or(s.color))
             : what == "bg"     ? color(st.bg.value_or(s.bg))
             : what == "border" ? color(st.border.value_or(s.border))
                                : name(st.image.empty() ? s.image : st.image);
    };
    std::string out;
    if (key == "font") out = s.font;
    else if (key == "size") out = std::format("{:g}", s.size);
    else if (key == "color") out = color(s.color);
    else if (key == "line_spacing") out = std::format("{:g}", s.lineSpacing);
    else if (key == "shadow_color") out = color(s.shadow.color);
    else if (key == "shadow_offset") out = std::format("{:g}", s.shadow.offset);
    else if (key == "bg") out = color(s.bg);
    else if (key == "border") out = color(s.border);
    else if (key == "image") out = name(s.image);
    else if (key == "padding") out = std::format("{:g}", s.padding);
    else if (key == "opacity") out = std::format("{:g}%", s.opacity * 100.0f);
    else if (key == "hover_sound") out = name(s.hoverSound);
    else if (key == "click_sound") out = name(s.clickSound);
    else if (key.starts_with("hover_")) out = state(s.hover, key.substr(6));
    else if (key.starts_with("selected_")) out = state(s.selected, key.substr(9));
    else if (key.starts_with("disabled_")) out = state(s.disabled, key.substr(9));
    else return {};
    return out;
}

} // namespace

namespace dev {

std::string scaled(const std::string& line, float sy) {
    if (sy == 1.0f) return line;
    static const std::set<std::string_view> down{"size", "padding", "bar"};
    std::string out = std::string(line.substr(0, line.find_first_not_of(' ')));
    for (const std::string& word : rawWords(line)) {
        const auto eq = word.find('=');
        const std::string key = eq == std::string::npos ? std::string() : word.substr(0, eq);
        const auto number = eq == std::string::npos ? std::nullopt : parseNumber(word.substr(eq + 1));
        std::string w = word;
        if (number && down.contains(key)) w = std::format("{}={}", key, std::lround(*number * sy));
        out += (out.empty() || out.back() == ' ' ? "" : " ") + w;
    }
    return out;
}



void showStyleValues(std::vector<StoryEditor::Field>& fields, const Style& style, const Palette& palette) {
    for (StoryEditor::Field& f : fields) {
        if (f.hint != "From its style" && f.hint != "From its base style") continue;
        std::string_view key = f.name;
        if (const auto slot = std::ranges::find(kStyleSlots, std::string_view(f.name), &Slot::name); slot != std::end(kStyleSlots)) key = slot->key;
        if (std::string value = styleValueText(style, key, palette); !value.empty()) f.hint = std::move(value); // (its own: the text's made for it)
    }
}

void showConfigValues(std::vector<StoryEditor::Field>& fields, const GameConfig& config, const Palette& palette) {
    auto value = [&](std::string_view name) { return lower(fieldValue(fields, name)); };
    for (StoryEditor::Field& f : fields) {
        if (f.name == "Image" && f.hint.empty()) { // (a character's pictures, an atlas's: named after them)
            f.hint = lower(value("Name"));
            continue;
        }
        if (f.name == "Title" && f.hint.empty() && !value("Name").empty()) { // (an ending's, an achievement's, a tip's: its name)
            f.hint = value("Name");
            continue;
        }
        if (f.hint != "From Settings") continue;
        std::string v;
        if (f.name == "Over") v = std::format("{:g}", config.moveTime);
        else if (f.name == "Strength") v = std::format("{:g}", config.shakeStrength);
        else if (f.name == "For") v = std::format("{:g}", value("What") == "flash" ? config.flashTime : config.shakeTime);
        else if (f.name == "Colour") v = colorText(config.flashColor, palette);
        else if (f.name.ends_with("Seconds")) v = std::format("{:g}", f.name == "Seconds" && value("Transition") != "fade" ? config.showTransition.duration : config.fadeTime);
        else if (f.name == "Its speed") v = std::format("{:g}", config.babbleSpeed);
        else if (f.name == "Blinks every") v = std::format("{:g}", config.blinkEvery);
        else if (f.name == "Mouth's speed") v = std::format("{:g}", config.talkSpeed);
        else continue;
        f.hint = std::move(v);
    }
}




std::optional<std::size_t> sceneOf(const StoryEditor& editor, const ScriptError& e) {
    const auto line = static_cast<std::size_t>(std::max(e.line - 1, 0));
    const auto& scenes = editor.scenes();
    for (std::size_t i = 0; i < scenes.size(); ++i)
        if (editor.path(scenes[i].file) == e.file && scenes[i].line < line && line < scenes[i].end) return i;
    return std::nullopt;
}


bool inSettings(const StoryEditor& editor, const ScriptError& e) {
    return e.line > 0 && e.file == "scripts/main.vnscript" && editor.isSetting(static_cast<std::size_t>(e.line - 1));
}

SDL_Event mouseEventAt(Uint32 type, float x, float y, bool twice) {
    SDL_Event e{};
    e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.clicks = twice ? 2 : 1;
        e.button.x = x;
        e.button.y = y;
    }
    return e;
}

SDL_Event wheelEventAt(float turns, float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.y = turns;
    e.wheel.mouse_x = x;
    e.wheel.mouse_y = y;
    return e;
}

SDL_Event textEvent(const char* text) {
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = text;
    return e;
}

SDL_Event keyEvent(const SDL_KeyboardEvent& key) {
    SDL_Event e{};
    e.key = key;
    e.type = SDL_EVENT_KEY_DOWN;
    return e;
}



std::vector<std::string> clipboardLines() {
    std::vector<std::string> out;
    if (const SdlOwned<char> text{SDL_GetClipboardText()}) out = linesOf(text.get());
    while (!out.empty() && blank(out.back())) out.pop_back();
    return out;
}

char clipboardKey(const SDL_KeyboardEvent& key) {
    if (!(key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) return 0;
    return key.key == SDLK_C ? 'c' : key.key == SDLK_X ? 'x' : key.key == SDLK_V ? 'v' : key.key == SDLK_D ? 'd' : 0;
}

std::size_t previousLetter(const std::string& s, std::size_t i) {
    if (i == 0) return 0;
    do --i;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}

std::string languageCode(std::string_view name) {
    const std::string want = lower(trim(name));
    if (want.empty()) return {};
    const auto it = std::ranges::find_if(kLanguages, [&](const Language& l) { return lower(l.name) == want || (!l.own.empty() && lower(l.own) == want); });
    return it != std::end(kLanguages) ? std::string(it->code) : std::string{};
}

std::string spellingLanguage(std::string_view language) {
    const std::string code = languageCode(language);
    return code == "no" ? "nb" : code; // (the spell checker's Norwegian is Bokmål)
}

std::vector<std::pair<std::size_t, std::size_t>> misspelled(std::map<std::string, bool, std::less<>>& known, const std::string& text, const std::string& language,
                                                            const std::vector<std::string>& names) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    auto letter = [](unsigned char c) { return std::isalpha(c) || c >= 0x80; };
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '{') {
            const auto close = text.find('}', i);
            i = close == npos ? text.size() : close + 1;
            continue;
        }
        if (!letter(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        std::size_t j = i;
        bool digits = false;
        while (j < text.size() && (letter(static_cast<unsigned char>(text[j])) || std::isdigit(static_cast<unsigned char>(text[j])) ||
                                   (text[j] == '\'' && j + 1 < text.size() && letter(static_cast<unsigned char>(text[j + 1])))))
            digits = digits || std::isdigit(static_cast<unsigned char>(text[j++]));
        const std::string word = text.substr(i, j - i);
        if (!digits && word.size() > 1 && !std::ranges::contains(names, lower(word))) {
            const std::string key = language + "|" + word;
            auto it = known.find(key);
            if (it == known.end()) it = known.emplace(key, spelledRight(word, language)).first;
            if (!it->second) out.emplace_back(i, j);
        }
        i = j;
    }
    return out;
}

} // namespace dev

// What the developer tools keep, looked up and kept up to date (Developer.hpp has them).

std::optional<SDL_Color> DeveloperTools::characterColor(const Palette& palette, std::string_view name) {
    if (colourVersion != editor.version()) { // (found again after a change, not every frame)
        colourVersion = editor.version();
        characterColours.clear();
        for (const StoryEditor::Character& c : editor.characters()) characterColours[lower(c.name)] = parseColor(fieldValue(editor.fields(c), "Colour"), &palette);
    }
    const auto it = characterColours.find(lower(name));
    return it != characterColours.end() ? it->second : std::nullopt;
}

const std::string& DeveloperTools::cachedAudio(const Assets& assets, const std::string& name) {
    if (audioGeneration != filesGeneration) {
        audioGeneration = filesGeneration;
        audioPaths.clear();
        foundAudio.clear();
    }
    const auto it = audioPaths.find(name);
    return it != audioPaths.end() ? it->second : audioPaths.emplace(name, assets.audio(name)).first->second;
}

std::optional<std::string> DeveloperTools::cachedFoundAudio(const Assets& assets, const std::string& name) {
    static_cast<void>(cachedAudio(assets, name)); // (the generation)
    const auto it = foundAudio.find(name);
    return it != foundAudio.end() ? it->second : foundAudio.emplace(name, assets.findAudio(name)).first->second;
}

const ScriptError* DeveloperTools::problemAt(std::size_t file, std::size_t line) const {
    const std::string& path = editor.path(file);
    const auto it = std::ranges::find_if(problems, [&](const ScriptError& e) { return e.file == path && e.line == static_cast<int>(line) + 1; });
    return it != problems.end() ? &*it : nullptr;
}

const std::vector<std::string>& DeveloperTools::cachedFiles(const std::string& folder, std::string_view ext) {
    auto& [generation, names] = fileLists[folder + "|" + std::string(ext)];
    if (generation != filesGeneration) {
        names = listFiles(folder, ext);
        generation = filesGeneration;
    }
    return names;
}

bool DeveloperTools::problemWithin(std::size_t file, std::size_t from, std::size_t end) const {
    const std::string& path = editor.path(file);
    return std::ranges::any_of(problems, [&](const ScriptError& e) { return e.file == path && e.line > static_cast<int>(from) && e.line <= static_cast<int>(end); });
}

const std::vector<bool>& DeveloperTools::wrongScenes() {
    if (wrongScenesVersion != editor.version()) {
        wrongScenesVersion = editor.version();
        scenesWrong.assign(editor.scenes().size(), false);
        for (const ScriptError& e : problems)
            if (const auto i = sceneOf(editor, e)) scenesWrong[*i] = true;
    }
    return scenesWrong;
}

DeveloperTools::Edited DeveloperTools::edited(const std::string& id, StoryEditor::Kind kind, const std::vector<StoryEditor::Field>& fields, bool wasTyped,
                                                const std::string& name, const std::string& value) {
    if (wasTyped) {
        if (const std::string owner = id + ":" + name; focusOwner != owner) {
            focusOwner = owner;
            focusKind = kind;
            focusFields = fields;
        }
        if (const auto f = std::ranges::find(focusFields, name, &StoryEditor::Field::name); f != focusFields.end()) f->value = value;
        return {focusFields, std::format("{}:{}{}", id, name, focusCount), focusKind};
    }
    Edited out{fields, {}, kind};
    std::ranges::find(out.fields, name, &StoryEditor::Field::name)->value = value;
    focus.clear();
    return out;
}

void DeveloperTools::saidRenamed(bool ok, std::string_view mistake) {
    if (!ok) {
        failed = true;
        status = std::string(mistake);
    } else if (status == mistake) {
        status.clear();
    }
}

} // namespace vn
