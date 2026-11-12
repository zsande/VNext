// A game's translations: their files read and written (what's new to translate, what's gone), the texts they
// translate, and the language the player picked.
#include "Engine.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace vn {

void Script::forEachText(const std::function<void(std::string& text, const TextPlace& place)>& f) {
    auto sorted = [](auto& map) { // the same order every time (and in --translate files)
        std::vector<std::pair<const std::string*, decltype(&map.begin()->second)>> out;
        out.reserve(map.size());
        for (auto& [key, value] : map) out.emplace_back(&key, &value);
        std::ranges::sort(out, {}, [](const auto& p) { return *p.first; });
        return out;
    };
    f(gameSettings.title, {.what = "title"});
    for (Instruction& ins : code) {
        const TextPlace place{.line = &ins};
        std::visit(Overloaded{
                       [&](cmd::Say& c) {
                           if (!c.speaker.empty()) f(c.speaker, place);
                           f(c.text, place);
                       },
                       [&](cmd::Menu& c) {
                           for (auto& choice : c.choices) {
                               f(choice.text, place);
                               if (!choice.note.empty()) f(choice.note, place);
                           }
                       },
                       [&](cmd::Message& c) {
                           if (!c.sender.empty()) f(c.sender, place);
                           if (!c.text.empty()) f(c.text, place);
                       },
                       [&](cmd::Chat& c) { f(c.with, place); },
                       [&](cmd::Call& c) {
                           if (!c.sender.empty()) f(c.sender, place);
                       },
                       [](auto&) {},
                   },
                   ins.command);
    }
    for (auto [key, words] : sorted(captions)) f(*words, {.what = "caption", .name = *key});
    for (auto [key, def] : sorted(characters)) {
        f(def->name, {.what = "character", .name = *key});
        if (!def->about.empty()) f(def->about, {.what = "character", .name = *key});
    }
    for (auto [key, screen] : sorted(screens)) {
        const TextPlace place{.what = "screen", .name = *key};
        for (Element& el : screen->elements) {
            if (el.kind == ElementKind::Text || el.kind == ElementKind::Button) f(el.text, place);
            if (!el.confirm.empty()) f(el.confirm, place);
            // (the words parts show: an input's placeholder, what an ending or achievement shows until it's had, the
            // pages' tabs, the saves' questions, a control's while it waits for a key, a phone app's title)
            static constexpr std::string_view kWords[]{"placeholder", "secret", "auto", "quick", "load_confirm", "delete_confirm", "waiting", "title"};
            for (std::string_view word : kWords)
                if (auto it = el.props.find(word); it != el.props.end()) f(it->second, place);
        }
    }
    for (std::size_t k = 0; k < lists.size(); ++k)
        for (Record& r : lists[k]) {
            f(r.title, {.what = kRecordKinds[k], .name = r.name});
            if (!r.text.empty()) f(r.text, {.what = kRecordKinds[k], .name = r.name});
        }
}

void Script::translate(const Translation* translation) {
    if (originals_.empty()) forEachText([&](std::string& text, const TextPlace&) { originals_.push_back(text); });
    translation_ = translation;
    std::size_t i = 0;
    forEachText([&](std::string& text, const TextPlace&) {
        const std::string& original = originals_[i++];
        text = translation ? translation->text(original) : original;
    });
}

std::string Script::translated(std::string_view text) const {
    return translation_ ? translation_->text(std::string(text)) : std::string(text);
}

namespace {

// A text as a translation file writes it: in quotes, unless it has quote marks of its own.
std::string written(const std::string& text) {
    return text.find('"') == std::string::npos ? std::format("\"{}\"", text) : text;
}

constexpr std::string_view kAddedNote = "# Texts added since the file was made."; // (over the texts --translate adds to a file)

// Whether a screen shows what keys do (an input for a control's key): then the player's names for the mouse
// buttons, controller buttons and gestures show too, so a translation file lists them.
bool showsKeyNames(const Script& script) {
    return std::ranges::any_of(script.screens, [](const auto& screen) {
        return std::ranges::any_of(screen.second.elements, [](const Element& el) { return el.kind == ElementKind::Control; });
    });
}

// Whether a screen has a box to type in (an input part) or saves (a list of them): then Read aloud can say its own words
// for them (a keyboard's keys, a save's delete button).
bool saysOwnWords(const Script& script) {
    return std::ranges::any_of(script.screens, [](const auto& screen) {
        return std::ranges::any_of(screen.second.elements, [](const Element& el) {
            return el.kind == ElementKind::Input || (el.kind == ElementKind::Container && el.option("list") == "saves");
        });
    });
}

// The words the game's time shows (setting time), as the game fills them in: its weekdays, months, dates, parts of
// the day (shown in place of a clock, with days and parts of the day), AM and PM, and what the story waiting for
// the player's clock says.
std::vector<std::string> timeWords(const Script& script) {
    const GameSettings& g = script.gameSettings;
    std::vector<std::string> out;
    if (g.time == TimeKind::None) return out;
    const bool real = g.time == TimeKind::Real, dates = real || g.timeStartDate >= 0;
    if (dates || g.timeStartWeekday >= 0) out.insert(out.end(), kWeekdays.begin(), kWeekdays.end());
    if (dates) {
        out.insert(out.end(), kMonths.begin(), kMonths.end());
        out.emplace_back("{weekday} {day} {month}");
    } else if (g.timeStartWeekday < 0) {
        out.emplace_back("Day {day}");
    }
    if (g.time == TimeKind::Days)
        for (const auto& [part, start] : g.dayParts) out.push_back(upperFirst(part));
    else if (g.clock12)
        out.insert(out.end(), {"AM", "PM"});
    if (real) out.emplace_back("The story goes on at {time}.");
    return out;
}

// Every text the game shows (what a translation file lists): the scripts' own, and the key names
// when a screen shows them; not the player's own name, when the game has no name of its own.
std::unordered_set<std::string> shownTexts(Script& script) {
    std::unordered_set<std::string> shown;
    script.forEachText([&](std::string& text, const Script::TextPlace& place) {
        if (!text.empty() && !(place.what == "title" && text == GameSettings{}.title)) shown.insert(text);
    });
    if (showsKeyNames(script))
        for (const auto& [key, word] : kKeyWords) shown.insert(std::string(word));
    if (saysOwnWords(script))
        for (std::string_view word : kReadAloudWords) shown.insert(std::string(word));
    for (std::string& word : timeWords(script)) shown.insert(std::move(word));
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
                                ? "The language and font lines go at the top, before the texts."_tr()
                                : "This text has no translation under it."_tr());
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
                error(number, "This line is indented, but there's no text above it to translate."_tr());
            } else if (translated) {
                error(number, "A text has one line of translation."_tr());
            } else { // (one without words: the original shows, and it's a mistake to read nothing in its language)
                if (const std::string words = unquote(text); trim(words).empty()) error(number, "This translation has no words."_tr());
                else t.texts.insert_or_assign(*original, words);
                translated = true;
            }
            continue;
        }
        const std::string head = lower(firstWord(text));
        if (!started && !isQuoted(text) && (head == "language" || head == "font")) {
            const auto split = splitWords(trim(text.substr(head.size())));
            if (!split) {
                error(number, "In this line {}."_tr(split.error()));
                continue;
            }
            std::vector<std::string> w;
            for (const ScriptWord& word : *split) w.push_back(word.text);
            if (head == "language") { // language "Name" [code=xx]
                if (w.empty() || w[0].find('=') != std::string::npos) {
                    error(number, "A language line needs the language's name."_tr());
                    continue;
                }
                t.name = w[0];
                for (std::size_t i = 1; i < w.size(); ++i) {
                    if (w[i].starts_with("code=") && w[i].size() > 5) t.code = lower(w[i].substr(5));
                    else error(number, "'{}' doesn't belong in a language line."_tr(w[i]));
                }
            } else { // font [NAME] FILE [STYLE], like in scripts
                if (w.empty() || w.size() > 3) {
                    error(number, "A font line needs a font file."_tr());
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
        if (place.what == "title" && text == GameSettings{}.title) return; // (the player's own name, not the game's)
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
                note += &text == &m->sender ? ", the sender's name" : ", a message from " + from;
            }
        } else if (place.what == "title") {
            heading = "The game's name";
        } else if (place.what == "character") {
            heading = "Character names";
        } else if (place.what == "screen") {
            heading = std::format("Screen: {}", place.name);
        } else if (place.what == "caption") {
            heading = "Captions";
            note = place.name;
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
    if (saysOwnWords(script)) {
        std::string_view note = "what Read aloud calls a save's delete button and the on-screen keyboard's signs"; // (over the first of them)
        for (std::string_view word : kReadAloudWords)
            if (seen.insert(std::string(word)).second && !have.texts.contains(std::string(word))) {
                add(std::string(word), "Read aloud", std::string(note));
                note = {};
            }
    }
    for (const std::string& word : timeWords(script)) // ({day}, {weekday}...: filled in as it shows; keep them in the translation)
        if (seen.insert(word).second && !have.texts.contains(word)) add(word, "Time", word.contains('{') ? "keep each {name}: filled in as it shows" : "");

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

// The language changed: the line, choices or question on screen again in the new one (the line
// types out again unless it was all there; its voice goes on).
void Game::retranslate() {
    if (wait_ != Wait::Say && wait_ != Wait::Menu) return;
    const Command& command = script_.code[resumePc_].command;
    auto again = [&](const std::string& speaker, SDL_Color colour, const std::string& text, const CharacterDef* def, bool whole) {
        const bool voiced = voiced_, madeUp = madeUpVoice_, follows = textFollowsVoice_;
        const float time = lineTime_;
        say(speaker, colour, text, def);
        voiced_ = voiced;
        madeUpVoice_ = madeUp;
        textFollowsVoice_ = follows;
        lineTime_ = time;
        if (whole) {
            reveal_ = static_cast<float>(length_);
            passedStops_ = stops_.size();
        }
        if (!page_.empty()) page_.back() = {speaker_, speakerColour_, text_, spans_, textColour_};
    };
    if (const auto* c = std::get_if<cmd::Say>(&command)) {
        const CharacterDef* def = c->character.empty() ? nullptr : script_.character(c->character);
        again(def ? def->name : c->speaker, def ? def->nameColour : gameSettings().narrationColour, c->text, def, textComplete() && !pendingStop());
        if (!history_.empty()) {
            HistoryEntry& h = history_.back();
            h.speaker = speaker_;
            h.colour = speakerColour_;
            h.text = text_;
            h.spans = spans_;
        }
    } else if (const auto* m = std::get_if<cmd::Menu>(&command)) {
        listChoices(*m);
    }
}

// --translate ID: writes game/translations/<id>.vnscript with every text the game shows, ready to be
// translated; if it's there already, adds the texts it doesn't have yet and takes out the ones the
// game doesn't show any more.
bool Engine::writeTranslation(const std::string& id) {
    if (id.empty() || !std::ranges::all_of(id, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || static_cast<unsigned char>(c) >= 0x80; })) {
        SDL_Log("--translate needs the language's name after it, one word: --translate spanish.");
        return false;
    }
    if (!script_.ok()) {
        for (const auto& e : script_.errors) SDL_Log("%s line %d: %s", e.file.c_str(), e.line, e.message.c_str());
        SDL_Log("Fix the scripts first, so every text can be found.");
        return false;
    }
    const std::string folder = gameRoot_ + "translations/";
    SDL_CreateDirectory(folder.c_str());
    const std::string file = folder + id + ".vnscript";
    const auto existing = readTextFile(file);
    const TranslationFile out = translationFile(script_, id, existing);
    if (!existing || out.added > 0 || !out.removed.empty()) {
        if (!SDL_SaveFile(file.c_str(), out.text.data(), out.text.size())) {
            SDL_Log("Can't write '%s': %s.", file.c_str(), SDL_GetError());
            return false;
        }
        if (!existing) SDL_Log("%s", std::format("Wrote '{}', with {} texts to translate.", file, out.added).c_str());
        else if (out.added > 0) SDL_Log("%s", std::format("Added {} new texts to '{}'.", out.added, file).c_str());
    } else {
        SDL_Log("'%s' already has every text the game shows.", file.c_str());
    }
    if (!out.removed.empty()) { // (changed or taken out of the scripts since the file was made)
        SDL_Log("%s", std::format("Took out {} texts from '{}' that the game doesn't show any more:", out.removed.size(), file).c_str());
        for (std::size_t i = 0; i < std::min<std::size_t>(out.removed.size(), 20); ++i) SDL_Log("    %s", out.removed[i].c_str());
        if (out.removed.size() > 20) SDL_Log("    ...");
    }
    return true;
}

void Engine::loadTranslations() {
    translations_.clear();
    const std::string folder = gameRoot_ + "translations/";
    for (const auto& file : files_.list(folder, ".vnscript"))
        if (const auto text = files_.readText(folder + file))
            translations_.push_back(Translation::parse(lower(file.substr(0, file.rfind('.'))), *text, script_.errors));
}

// The player's choice, else the computer's language if there's a translation for it.
const Translation* Engine::language() const {
    const std::string& want = settings_.language;
    if (want == "original") return nullptr;
    if (!want.empty()) {
        const auto it = std::ranges::find(translations_, want, &Translation::id);
        return it != translations_.end() ? &*it : nullptr;
    }
    const Translation* found = nullptr;
    int count = 0;
    if (const SdlOwned<SDL_Locale*> locales{SDL_GetPreferredLocales(&count)}; locales && count > 0 && locales.get()[0]->language) {
        const SDL_Locale& first = *locales.get()[0]; // (the first one only: the next may be the game's own)
        const std::string lang = lower(first.language);
        const std::string full = first.country ? std::format("{}-{}", lang, lower(first.country)) : lang;
        for (const Translation& t : translations_) {
            std::string code = t.code;
            std::ranges::replace(code, '_', '-');
            if (!code.empty() && (code == full || (code == lang && !found))) found = &t;
        }
    }
    return found;
}

void Engine::useLanguage() {
    const Translation* t = language();
    script_.translate(t);
    showTitle();
    assets_->setLanguage(t ? t->id : std::string{});
    configureFonts();
}

std::vector<std::string> Engine::languageNames() const {
    std::vector<std::string> names{gameSettings().language};
    for (const Translation& t : translations_) names.push_back(t.name);
    return names;
}

int Engine::languageIndex() const {
    const Translation* t = script_.translation();
    return t ? static_cast<int>(t - translations_.data()) + 1 : 0;
}

std::string Engine::languageId(int index) const {
    return index <= 0 || static_cast<std::size_t>(index) > translations_.size() ? "original" : translations_[static_cast<std::size_t>(index) - 1].id;
}

std::optional<std::string> Engine::languageIdNamed(std::string_view word) const {
    const std::string want = lower(trim(word));
    if (want.empty()) return std::nullopt;
    if (want == "original" || want == lower(gameSettings().language)) return "original";
    for (const Translation& t : translations_)
        if (want == t.id || want == lower(t.name) || (!t.code.empty() && want == lower(t.code))) return t.id;
    return std::nullopt;
}

} // namespace vn
