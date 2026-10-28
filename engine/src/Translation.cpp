// Translations: the texts players read, in another language (translations/NAME.vnscript), read and written.
#include "Script.hpp"
#include "System.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <expected>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vn {

// ---------------------------------------------------------------------------
// Translations
// ---------------------------------------------------------------------------

namespace {

// A text as a translation file writes it: in quotes, unless it has quote marks of its own.
std::string written(const std::string& text) {
    return text.find('"') == std::string::npos ? std::format("\"{}\"", text) : text;
}

constexpr std::string_view kAddedNote = "# Texts added since the file was made."; // (over the texts --translate adds to a file)

// Whether a screen shows what keys do (a Controls screen): then the player's names for the mouse
// buttons, controller buttons and gestures show too, so a translation file lists them.
bool showsKeyNames(const Script& script) {
    return std::ranges::any_of(script.screens, [](const auto& screen) {
        return std::ranges::any_of(screen.second.elements, [](const Element& el) { return el.kind == ElementKind::Control; });
    });
}

// Every text the game shows (what a translation file lists): the scripts' own, and the key names
// when a screen shows them; not the player's own name, when the game has no name of its own.
std::unordered_set<std::string> shownTexts(Script& script) {
    std::unordered_set<std::string> shown;
    script.forEachText([&](std::string& text, const Script::TextPlace& place) {
        if (!text.empty() && !(place.what == "title" && text == GameConfig{}.title)) shown.insert(text);
    });
    if (showsKeyNames(script))
        for (const auto& [key, word] : kKeyWords) shown.insert(std::string(word));
    return shown;
}

} // namespace

std::string Translation::text(const std::string& original) const {
    const auto it = texts.find(original);
    return it != texts.end() ? it->second : original;
}

Translation Translation::parse(std::string id, std::string_view source, std::vector<ScriptError>& errors) {
    Translation t;
    t.id = std::move(id);
    t.name = upperFirst(t.id);
    const std::string file = std::format("translations/{}.vnscript", t.id);
    auto error = [&](int line, std::string message) { errors.push_back({file, line, std::move(message)}); };

    std::optional<std::string> original; // the text being translated
    int originalLine = 0;
    bool translated = false, started = false;
    auto finish = [&] {
        if (!original || translated) return;
        const std::string head = lower(firstWord(*original));
        error(originalLine, head == "language" || head == "font"
                                ? "language and font lines go at the top, before the texts."
                                : "This text has no translation under it.");
    };
    int number = 0;
    for (auto&& part : std::views::split(source, '\n')) {
        ++number;
        std::string_view raw(part.begin(), part.end());
        if (raw.ends_with('\r')) raw.remove_suffix(1);
        if (number == 1 && raw.starts_with("\xEF\xBB\xBF")) raw.remove_prefix(3); // saved "with BOM"
        const std::string_view text = trim(stripNote(raw));
        if (text.empty()) continue;

        if (indentOf(raw) > 0) {
            if (!original) {
                error(number, "This line is indented, but there's no text above it to translate.");
            } else if (translated) {
                error(number, "A text has one line of translation.");
            } else {
                t.texts.insert_or_assign(*original, unquote(text));
                translated = true;
            }
            continue;
        }
        const std::string head = lower(firstWord(text));
        if (!started && !isQuoted(text) && (head == "language" || head == "font")) {
            const auto split = splitWords(trim(text.substr(head.size())));
            if (!split) {
                error(number, std::format("In this line {}.", split.error()));
                continue;
            }
            std::vector<std::string> w;
            for (const ScriptWord& word : *split) w.push_back(word.text);
            if (head == "language") { // language "Name" [code=xx]
                if (w.empty() || w[0].find('=') != std::string::npos) {
                    error(number, "A language line needs the language's name.");
                    continue;
                }
                t.name = w[0];
                for (std::size_t i = 1; i < w.size(); ++i) {
                    if (w[i].starts_with("code=") && w[i].size() > 5) t.code = lower(w[i].substr(5));
                    else error(number, std::format("'{}' doesn't belong in a language line.", w[i]));
                }
            } else { // font [NAME] FILE [STYLE], like in scripts
                if (w.empty() || w.size() > 3) {
                    error(number, "A font line needs a font file.");
                    continue;
                }
                if (w.size() == 1) t.fonts.insert_or_assign("default", Script::FontFile{w[0], ""});
                else t.fonts.insert_or_assign(lower(w[0]), Script::FontFile{w[1], w.size() > 2 ? w[2] : ""});
            }
            continue;
        }
        finish();
        started = true;
        original = unquote(text);
        originalLine = number;
        translated = false;
    }
    finish();
    return t;
}

std::string prunedTranslation(Script& script, std::string_view id, std::string_view file, std::vector<std::string>& removed) {
    std::vector<ScriptError> ignored;
    const Translation have = Translation::parse(std::string(id), file, ignored);
    const std::unordered_set<std::string> shown = shownTexts(script);
    removed.clear();
    for (const auto& original : have.texts | std::views::keys)
        if (!shown.contains(original)) removed.push_back(original);
    std::ranges::sort(removed);
    if (removed.empty()) return std::string(file);
    std::vector<std::string_view> lines;
    for (auto&& part : std::views::split(file, '\n')) {
        std::string_view raw(part.begin(), part.end());
        if (raw.ends_with('\r')) raw.remove_suffix(1);
        lines.push_back(raw);
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back(); // (the line break at the end)
    auto note = [](std::string_view l) { return trim(l).starts_with('#'); };
    auto heading = [](std::string_view l) { return trim(l).starts_with("# ----"); };
    auto addedNote = [](std::string_view l) { return trim(l) == kAddedNote; };
    auto text = [&](std::string_view l) { // a text's own line: at the left, not a note, a language or a font line
        if (indentOf(l) > 0 || note(l)) return false;
        const std::string_view t = trim(stripNote(l));
        if (t.empty()) return false;
        const std::string head = lower(firstWord(t));
        return t.starts_with('"') || (head != "language" && head != "font");
    };
    // Each text taken out goes with the translation under it and the note just above it...
    std::vector<std::string_view> kept;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view l = lines[i];
        if (text(l) && std::ranges::binary_search(removed, unquote(trim(stripNote(l))))) {
            while (!kept.empty() && note(kept.back()) && !heading(kept.back()) && !addedNote(kept.back())) kept.pop_back();
            while (i + 1 < lines.size() && indentOf(lines[i + 1]) > 0 && !trim(lines[i + 1]).empty()) ++i;
            continue;
        }
        kept.push_back(l);
    }
    // ...then a heading with no text left under it (and the note over texts added, when none are left), and
    // blank lines left doubled.
    auto emptyAfter = [&](std::size_t i, auto stops) {
        for (std::size_t j = i + 1; j < kept.size() && !stops(kept[j]); ++j)
            if (text(kept[j])) return false;
        return true;
    };
    std::string out;
    bool wasBlank = false;
    for (std::size_t i = 0; i < kept.size(); ++i) {
        const std::string_view l = kept[i];
        if (heading(l) && emptyAfter(i, [&](std::string_view m) { return heading(m) || addedNote(m); })) continue;
        if (addedNote(l) && emptyAfter(i, addedNote)) continue;
        const bool isBlank = trim(l).empty();
        if (isBlank && (wasBlank || out.empty())) continue;
        wasBlank = isBlank;
        out += std::string(l) + "\n";
    }
    while (out.ends_with("\n\n")) out.pop_back();
    return out;
}

TranslationFile translationFile(Script& script, std::string_view id, const std::optional<std::string>& existing) {
    TranslationFile out;
    std::vector<ScriptError> ignored;
    const Translation have = existing ? Translation::parse(std::string(id), *existing, ignored) : Translation{};
    std::unordered_set<std::string> seen; // each text is written once
    std::string body, section;
    auto add = [&](const std::string& text, const std::string& heading, const std::string& note) {
        if (heading != section) body += std::format("\n# ---- {} ----\n", section = heading);
        body += note.empty() ? "\n" : std::format("\n# {}\n", note);
        body += std::format("{}\n    {}\n", written(text), written(text));
        ++out.added;
    };
    script.forEachText([&](std::string& text, const Script::TextPlace& place) {
        if (place.what == "title" && text == GameConfig{}.title) return; // (the player's own name, not the game's)
        if (text.empty() || !seen.insert(text).second || have.texts.contains(text)) return;
        std::string heading, note;
        if (const Instruction* ins = place.line) {
            std::string_view file = script.files[ins->file];
            if (file.starts_with("scripts/")) file.remove_prefix(8);
            heading = file;
            note = std::format("line {}", ins->line);
            if (const auto* say = std::get_if<cmd::Say>(&ins->command)) {
                if (&text == &say->speaker) note += ", the speaker's name";
                else if (const CharacterDef* def = script.character(say->character)) note += ", " + def->name;
                else if (!say->speaker.empty()) note += ", " + say->speaker;
            } else if (const auto* m = std::get_if<cmd::Message>(&ins->command)) {
                const CharacterDef* def = script.character(m->character);
                const std::string from = m->mine ? "the player" : def ? def->name : m->sender;
                note += &text == &m->sender ? ", the sender's name" : &text == &m->text ? ", a message from " + from : ", an answer to it";
            }
        } else if (place.what == "title") {
            heading = "The game's name";
        } else if (place.what == "character") {
            heading = "Character names";
        } else if (place.what == "screen") {
            heading = std::format("Screen: {}", place.name);
        } else if (place.what == "control") {
            heading = "Input";
        } else { // a record: the gallery, endings, songs, achievements, tips
            static const std::map<std::string_view, std::string_view> headings{
                {"gallery", "Gallery"}, {"ending", "Endings"}, {"song", "Music room"}, {"achievement", "Achievements"}, {"tip", "Tips"}};
            heading = headings.at(place.what);
            note = place.name;
        }
        add(text, heading, note);
    });
    if (showsKeyNames(script))
        for (const auto& [key, word] : kKeyWords)
            if (seen.insert(std::string(word)).second && !have.texts.contains(std::string(word))) add(std::string(word), "Key names", "");

    if (existing) {
        out.text = prunedTranslation(script, id, *existing, out.removed);
        if (out.added > 0) {
            if (!out.text.empty() && !out.text.ends_with('\n')) out.text += '\n';
            out.text += std::format("\n{}\n", kAddedNote) + body;
        }
        return out;
    }
    std::string name(id); // (the language's name, from the file's: spanish -> Spanish, new_language -> New language)
    for (char& ch : name)
        if (ch == '_') ch = ' ';
    name = upperFirst(name);
    out.text = std::format("# Every text the game shows, each with its translation under it.\n\nlanguage \"{}\"\n", name) + body;
    return out;
}

} // namespace vn
