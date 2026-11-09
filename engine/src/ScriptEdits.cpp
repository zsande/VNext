// The scripts' text as the developer tools change it: lines, indents, names renamed everywhere they're used, and
// file names.
#include "Forms.hpp"

#include <climits>
#include <cmath>
#include <ranges>

namespace vn {

namespace dev {

std::string scaled(const std::string& line, float sy) {
    if (sy == 1.0f) return line;
    static const std::set<std::string_view> down{"size", "padding"};
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

} // namespace dev

// (the developer files share these: ScriptEdits.hpp)
// Lines without the indentation they share (spaces and tabs: a tab, an indent's width).
std::vector<std::string> unshared(std::span<const std::string> lines) {
    int shared = INT_MAX;
    for (const std::string& line : lines)
        if (!blank(line)) shared = std::min(shared, indentOf(line));
    std::vector<std::string> out;
    for (const std::string& line : lines) {
        std::size_t cut = 0; // (`shared` columns of spaces and tabs)
        for (int n = 0; cut < line.size() && n < shared && (line[cut] == ' ' || line[cut] == '\t'); ++cut) n += line[cut] == '\t' ? kIndent : 1;
        out.push_back(line.substr(cut));
    }
    return out;
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

// A phone line that does something rather than say it: "Mika sends beach", "Mika is typing", "Haru deletes
// it"... (a word for it after who, before any colon).
bool phoneDoes(std::string_view t) {
    static constexpr std::string_view kVerbs[]{"sends", "is", "deletes", "edits", "reacts", "reads"};
    const auto words = rawWords(t.substr(0, t.find(':')));
    for (std::size_t i = 1; i < words.size() && i <= 3; ++i) {
        const std::string w = lower(words[i]);
        if (!std::ranges::contains(kVerbs, std::string_view(w))) continue;
        if ((w == "is" || w == "deletes" || w == "edits" || w == "reads") && i + 1 < words.size()) {
            const std::string next = lower(words[i + 1]);
            if (next != (w == "is" ? "typing" : "it") && next != "it:") continue;
        }
        return !words.front().starts_with('"');
    }
    return false;
}

// `line` with the animation `from` called `to` wherever it's played by name: after "with" in a background,
// show, hide, open or camera command (open's "and close with" too), as a story line or as an action (in quotes
// after action=, drop=, action_on_open= or action_on_close=, or after another one there with ";"); and as the setting
// animation_on_show or animation_on_hide. (animation= and the other options: with the rest of the options.)
std::string animationRenamed(std::string line, std::string_view from, std::string_view to) {
    const std::string name = lower(from);
    std::vector<std::size_t> starts{std::min(line.size(), line.find_first_not_of(' '))}; // (where each command begins)
    bool quoted = false, actions = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (quoted && line[i] == '\\') {
            ++i;
        } else if (line[i] == '"') {
            quoted = !quoted;
            actions = false;
            if (quoted && i > 0 && line[i - 1] == '=') {
                const std::size_t key = line.find_last_of(' ', i - 1) == npos ? 0 : line.find_last_of(' ', i - 1) + 1;
                static constexpr std::array<std::string_view, 4> kActions{"action", "drop", "action_on_open", "action_on_close"};
                actions = std::ranges::contains(kActions, lower(line.substr(key, i - 1 - key)));
                if (actions) starts.push_back(i + 1);
            }
        } else if (actions && line[i] == ';') {
            starts.push_back(i + 1);
        }
    }
    for (auto s = starts.rbegin(); s != starts.rend(); ++s) { // (the last first: renaming one leaves the places before it as they were)
        const std::size_t end = std::min(line.size(), line.find_first_of(";\"", *s));
        std::vector<std::pair<std::size_t, std::size_t>> words; // (where each of its words is, and how long)
        for (std::size_t i = *s; i < end;) {
            while (i < end && line[i] == ' ') ++i;
            std::size_t j = i;
            while (j < end && line[j] != ' ') ++j;
            if (j > i) words.emplace_back(i, j - i);
            i = j;
        }
        auto word = [&](std::size_t k) { return lower(line.substr(words[k].first, words[k].second)); };
        std::vector<std::size_t> named; // (the words that are it)
        if (const std::string verb = words.empty() ? std::string{} : word(0); verb == "background" || verb == "show" || verb == "hide" || verb == "open" || verb == "camera") {
            for (std::size_t k = 1; k + 1 < words.size(); ++k)
                if (word(k) == "with" && word(k + 1) == name) named.push_back(k + 1);
        } else if (verb == "setting" && words.size() >= 3 && (word(1) == "animation_on_show" || word(1) == "animation_on_hide") && word(2) == name) {
            named.push_back(2);
        }
        for (auto k = named.rbegin(); k != named.rend(); ++k) line.replace(words[*k].first, words[*k].second, to);
    }
    return line;
}

namespace {

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

} // namespace

// `line` with the scene `from` called `to` wherever it's gone to (jump NAME, start NAME), and where a
// character names it as their chat or call scene (chat=NAME, call=NAME).
std::string renamed(const std::string& line, std::string_view from, std::string_view to) {
    auto endsWithWord = [](std::string_view s, std::string_view word) {
        return s.ends_with(word) && (s.size() == word.size() || !isWordChar(s[s.size() - word.size() - 1]));
    };
    return wordRenamed(line, from, to, [&](std::string_view before) {
        if (trim(before).starts_with("character ")) {
            const std::string_view setting = before.ends_with('"') ? before.substr(0, before.size() - 1) : before;
            return endsWithWord(setting, "chat=") || endsWithWord(setting, "call=");
        }
        std::size_t b = before.size();
        while (b > 0 && before[b - 1] == ' ') --b;
        const bool spaced = b < before.size();
        before = before.substr(0, b);
        return spaced && (endsWithWord(before, "jump") || endsWithWord(before, "start"));
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

// A setting line's name (in lowercase) and value: setting NAME VALUE.
std::optional<std::pair<std::string, std::string_view>> settingOf(std::string_view line) {
    if (indentOf(line) != 0 || lower(firstWord(line)) != "setting") return std::nullopt;
    const std::string_view rest = afterFirst(line);
    return std::pair{lower(firstWord(rest)), afterFirst(rest)};
}

// The helpers the other developer files use too (ScriptEdits.hpp declares them, with what they do).

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

bool nameChar(char c) { // (a variable's: persistent.trust)
    return letterOrDigitByte(c) || c == '_' || c == '.';
}

} // namespace dev

namespace {

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

// A line with its settings that are code (when=, action=...) changed by `change`, each one's value
// without its quotes.
std::string codeSettings(const std::string& line, const std::function<std::string(std::string_view)>& change) {
    static constexpr std::string_view kCode[]{"when", "variable", "action", "drop", "action_on_open", "action_on_close", "clickable", "chosen"};
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

} // namespace

// (the developer files share these: ScriptEdits.hpp)
// `line` with the file `from` (in `folder`: images/, sounds/, movies/ or fonts/) called `to`, where
// it's used: by its name without the extension (background park, music rain, image=..., avatar=..., image "logo",
// a chat's "Mika sends park", a phone setting's), as a path ("fonts/Lora.ttf"), or for a character's image
// (aiko_happy) as its expression. `bases`: each character's name and the name its images start with.
std::string assetRenamed(const std::string& line, std::string_view folder, const std::string& from, const std::string& to,
                         const std::vector<std::pair<std::string, std::string>>& bases) {
    auto stem = [](const std::string& file) { return lower(scriptName(stemOf(file))); }; // (what scripts call it)
    const std::string name = stem(from), now = stemOf(to), path = lower(std::string(folder) + from);
    const bool images = folder == "images/", sounds = folder == "sounds/", movies = folder == "movies/";
    if (!lower(line).contains(name) && !(images && lower(line).contains(name.substr(name.rfind('_') + 1)))) return line; // (aiko_happy: show aiko happy)
    const auto spans = wordSpans(line);
    auto word = [&](std::size_t i) { return i < spans.size() ? lower(std::string_view(line).substr(spans[i].first, spans[i].second)) : std::string{}; };
    const std::string verb = word(0);
    // The expression a character's image is: who it is (a character with that image's start) and
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
            const bool image = key == "image" || key.ends_with("_image") || key == "face" || key == "shape" || key == "avatar" || key == "icon";
            const bool sound = key.ends_with("sound") || key == "ringtone";
            if (value == name && ((images && image) || (sounds && sound))) {
                const bool q = isQuoted(std::string_view(w).substr(eq + 1));
                changes.emplace_back(i, std::string(std::string_view(line).substr(spans[i].first, eq + 1)) + (q ? "\"" + now + "\"" : now));
            }
            continue;
        }
        const std::string plain = unquote(w);
        // (a chat's: WHO sends NAME, sends sticker NAME, sends voice NAME, a caption after a colon)
        const bool sent = i >= 2 && ((images && word(i - 1) == "sends") || (word(i - 2) == "sends" && word(i - 1) == (images ? "sticker" : "voice")));
        const bool colon = sent && plain.ends_with(':');
        const std::string bare = colon ? plain.substr(0, plain.size() - 1) : plain;
        bool use = false;
        if (images) use = (i == 1 && plain == name && (verb == "background" || verb == "show" || verb == "hide" || verb == "gallery" || verb == "sub-image" || verb == "image")) ||
                          (i == 2 && plain == name && verb == "setting" && (word(1) == "mouse_pointer" || word(1) == "phone_wallpaper" || word(1) == "phone_lock_wallpaper")) ||
                          (sent && bare == name);
        if (sounds) use = (i == 1 && plain == name && (verb == "music" || verb == "sound" || verb == "voice" || verb == "song" || verb == "caption")) ||
                          (i == 2 && plain == name && verb == "stop" && word(1) == "sound") ||
                          (i == 2 && plain == name && verb == "setting" && word(1).starts_with("phone_") && (word(1).ends_with("_sound") || word(1) == "phone_ringtone")) ||
                          (sent && bare == name);
        if (movies) use = i == 1 && plain == name && (verb == "movie" || verb == "gallery" || verb == "image");
        if (use) {
            changes.emplace_back(i, (isQuoted(w) ? "\"" + now + "\"" : now) + (colon ? ":" : ""));
        } else if (images && verb == "show" && i == 2 && !w.contains('=')) { // show aiko happy
            for (const auto& [character, base] : bases)
                if (character == lower(unquote(word(1))) && base + "_" + plain == name)
                    if (const auto e = expressionOf(word(1))) changes.emplace_back(i, *e);
        }
    }
    std::string out = line;
    for (const auto& [i, with] : changes | std::views::reverse) out.replace(spans[i].first, spans[i].second, with);
    // Someone speaking with an expression: Aiko happy: ... or Aiko (happy): ...
    if (std::string_view said; images && changes.empty() && spoken(trim(out), &said)) {
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

// A line with the variable `from` called `to`: everywhere in a line that's code (set, if...), and
// in the rest only in braces, an option's condition and settings that are code.
std::string lineWithVariable(const std::string& line, std::string_view from, std::string_view to) {
    const std::string_view t = trim(line);
    if (t.empty() || t.starts_with('#') || !lower(line).contains(lower(from))) return line;
    static constexpr std::string_view kCode[]{"set", "if", "else", "variable"};
    if (std::ranges::contains(kCode, lower(firstWord(t)))) return leading(line) + variableRenamed(t, from, to, true);
    std::string out = leading(line) + variableRenamed(t, from, to, false);
    // An option's condition: (if CONDITION) or (needs CONDITION: what it needs).
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

// Screens written in a game's pixels (x and y from the top left of the part they're in: a list's parts, of
// their item's box; or left, centre and right, top and bottom for where its edges line up; w and h in pixels,
// a container without a size as big as what's in it, unless it fits its parts), written in places and shares
// instead: x and y from -1 to 1 in the box a part is in, w and h (and item_width, item_height, spacing,
// spacing_across, spacing_down, width_at_most) shares of it. They show in the same
// places on a `width` x `height` screen.
std::vector<std::string> dev::placesFromPixels(const std::vector<std::string>& lines, float width, float height, const std::map<std::size_t, SDL_FPoint>& sizes) {
    struct Px {
        std::size_t line = 0;
        int indent = 0, parent = -1;
        std::string kind;
        std::optional<float> x, y, w, h;
        SDL_FRect at{};   // on the screen
        SDL_FRect in{};   // ...and where the parts in it are (a list's: its first item's box)
        bool fits = false; // fit=parts: as big as its parts (no size written)
        std::optional<float> placeX, placeY; // x=left|centre|right, y=top|centre|bottom: places as they are
    };
    auto wordOf = [](const std::vector<std::string>& words, std::string_view key) -> std::string {
        for (const std::string& w : words)
            if (lower(w).starts_with(std::string(key) + "=")) return lower(unquote(std::string_view(w).substr(key.size() + 1)));
        return {};
    };
    auto number = [](const std::vector<std::string>& words, std::string_view key) -> std::optional<float> {
        for (const std::string& word : words)
            if (lower(word).starts_with(std::string(key) + "=")) return parseNumber(unquote(std::string_view(word).substr(key.size() + 1)));
        return std::nullopt;
    };
    const SDL_FRect screen{0.0f, 0.0f, width, height};
    std::vector<Px> parts;
    std::vector<int> in; // (the parts a line is in)
    bool inScreen = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (!partLine(line)) continue;
        const int indent = indentOf(line);
        if (indent == 0) {
            inScreen = lower(firstWord(line)) == "screen";
            in.clear();
            continue;
        }
        if (!inScreen) continue;
        while (!in.empty() && parts[static_cast<std::size_t>(in.back())].indent >= indent) in.pop_back();
        const auto words = rawWords(trim(line));
        Px p{.line = i, .indent = indent, .parent = in.empty() ? -1 : in.back(), .kind = lower(words.front()), .x = number(words, "x"),
             .y = number(words, "y"), .w = number(words, "width"), .h = number(words, "height")};
        p.fits = wordOf(words, "fit") == "parts";
        static constexpr std::pair<std::string_view, float> kAcross[]{{"left", -1.0f}, {"centre", 0.0f}, {"right", 1.0f}}, kDown[]{{"top", 1.0f}, {"centre", 0.0f}, {"bottom", -1.0f}};
        for (const auto& [name, place] : kAcross)
            if (wordOf(words, "x") == name) p.placeX = place;
        for (const auto& [name, place] : kDown)
            if (wordOf(words, "y") == name) p.placeY = place;
        const SDL_FRect box = p.parent >= 0 ? parts[static_cast<std::size_t>(p.parent)].in : screen;
        const auto own = sizes.find(i); // (one sized by what it shows: placed by that size, as the game will)
        p.at = {box.x + p.x.value_or(0.0f), box.y + p.y.value_or(0.0f), p.w.value_or(own != sizes.end() ? own->second.x : 0.0f),
                p.h.value_or(own != sizes.end() ? own->second.y : 0.0f)};
        p.in = p.at;
        if (p.kind == "container" && !wordOf(words, "list").empty()) { // (a list's parts: in its first item's box)
            const float columns = std::max(1.0f, number(words, "columns").value_or(1.0f));
            const float gap = number(words, "spacing_across").value_or(number(words, "spacing").value_or(0.0f));
            p.in.w = number(words, "item_width").value_or((p.at.w - gap * (columns - 1.0f)) / columns);
            p.in.h = number(words, "item_height").value_or(p.at.h);
        }
        parts.push_back(std::move(p));
        in.push_back(static_cast<int>(parts.size()) - 1);
    }
    for (std::size_t i = parts.size(); i-- > 0;) { // (a container without a size: as far as what's in it goes)
        Px& c = parts[i];
        if (c.kind != "container" || (c.w && c.h)) continue;
        float right = c.at.x, bottom = c.at.y;
        for (std::size_t j = i + 1; j < parts.size() && parts[j].indent > c.indent; ++j) {
            right = std::max(right, parts[j].at.x + parts[j].at.w);
            bottom = std::max(bottom, parts[j].at.y + parts[j].at.h);
        }
        if (!c.w) c.at.w = c.in.w = std::max(1.0f, right - c.at.x);
        if (!c.h) c.at.h = c.in.h = std::max(1.0f, bottom - c.at.y);
    }
    std::vector<std::string> out = lines;
    for (const Px& p : parts) {
        const SDL_FRect box = p.parent >= 0 ? parts[static_cast<std::size_t>(p.parent)].in : screen;
        std::vector<std::string> words = rawWords(trim(lines[p.line]));
        auto put = [&](std::string_view key, std::optional<std::string> value) {
            const auto it = std::ranges::find_if(words, [&](const std::string& word) { return lower(word).starts_with(std::string(key) + "="); });
            if (!value) {
                if (it != words.end()) words.erase(it);
                return;
            }
            const std::string word = std::format("{}={}", key, *value);
            if (it != words.end()) *it = word;
            else words.push_back(word);
        };
        const bool container = p.kind == "container" && !p.fits; // (one without a size fills its box now: its size is written; one that fits its parts, none)
        if (p.placeX) put("x", unitText(*p.placeX));
        else if (p.x || p.at.x != box.x) put("x", unitText(placeFor(p.at.x - box.x, p.at.w, box.w)));
        if (p.placeY) put("y", unitText(*p.placeY));
        else if (p.y || p.at.y != box.y) put("y", unitText(-placeFor(p.at.y - box.y, p.at.h, box.h)));
        // (a size in a container that's as big as its parts that way: a share of the box that one's in)
        auto shareBox = [&](bool across) {
            int at = p.parent;
            while (at >= 0 && parts[static_cast<std::size_t>(at)].fits && !(across ? parts[static_cast<std::size_t>(at)].w : parts[static_cast<std::size_t>(at)].h))
                at = parts[static_cast<std::size_t>(at)].parent;
            const SDL_FRect b = at >= 0 ? parts[static_cast<std::size_t>(at)].in : screen;
            return std::max(1.0f, across ? b.w : b.h);
        };
        if (p.w || container) put("width", unitText(p.at.w / shareBox(true)));
        if (p.h || container) put("height", unitText(p.at.h / shareBox(false)));
        if (const auto most = number(words, "width_at_most")) { // (a share of the box it's in; one that fits its parts: of the one that's in)
            int at = p.parent;
            while (at >= 0 && parts[static_cast<std::size_t>(at)].fits && !parts[static_cast<std::size_t>(at)].w) at = parts[static_cast<std::size_t>(at)].parent;
            const float wide = at >= 0 ? parts[static_cast<std::size_t>(at)].in.w : screen.w;
            put("width_at_most", unitText(*most / std::max(1.0f, wide)));
        }
        for (const auto& [key, across] : {std::pair{"item_width", true}, {"spacing_across", true}, {"item_height", false}, {"spacing_down", false}})
            if (const auto v = number(words, key)) put(key, unitText(*v / (across ? box.w : box.h)));
        if (const auto gap = number(words, "spacing")) { // (the same gap across and down: a share of each; a part whose items go one way, its one gap)
            const bool across = partTakes(p.kind, "spacing_across"), down = partTakes(p.kind, "spacing_down");
            if (across && !number(words, "spacing_across")) put("spacing_across", unitText(*gap / box.w));
            if (down && !number(words, "spacing_down")) put("spacing_down", unitText(*gap / box.h));
            put("spacing", across || down ? std::nullopt : std::optional(unitText(*gap / box.h)));
        }
        out[p.line] = leading(lines[p.line]) + joined(words);
    }
    return out;
}

} // namespace vn
