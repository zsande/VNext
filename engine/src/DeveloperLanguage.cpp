// The developer tools' own words, in English or translated (a tools translation in their own folder), and making one.
#include "DeveloperUi.hpp"

#include <cctype>
#include <exception>

namespace vn {

std::string Developer::toolsLanguagesFolder() const { return engine.gamePlaces().front() + "developer tools translations/"; }

void Developer::findToolsLanguages() {
    DeveloperTools& d = tools;
    d.toolsLanguages.clear();
    const std::string folder = toolsLanguagesFolder();
    d.toolsLanguageFiles = listFiles(folder, ".vnscript");
    for (const std::string& name : d.toolsLanguageFiles) {
        if (name.contains('/')) continue;
        const auto text = readTextFile(folder + name);
        if (!text) continue;
        std::vector<ScriptError> errors;
        const Translation t = Translation::parse(stemOf(name), *text, errors);
        d.toolsLanguages.push_back({.id = t.id, .name = t.name, .code = t.code});
    }
    std::ranges::sort(d.toolsLanguages, {}, &DeveloperTools::ToolsLanguage::name);
}

void Developer::useToolsLanguage(const std::string& id) {
    DeveloperTools& d = tools;
    d.toolsLanguage.clear();
    useToolsTranslation({});
    if (id.empty()) return;
    const std::string path = toolsLanguagesFolder() + id + ".vnscript";
    const auto text = readTextFile(path);
    if (!text) {
        logWarn("{}", "Can't read the developer tools' language '{}'."_tr(path));
        return;
    }
    std::vector<ScriptError> errors;
    Translation t = Translation::parse(id, *text, errors);
    for (const ScriptError& e : errors) logWarn("developer tools translations/{}.vnscript line {}: {}", id, e.line, e.message);
    useToolsTranslation(std::move(t.texts));
    d.toolsLanguage = id;
}

void Developer::startToolsLanguage() {
    DeveloperTools& d = tools;
    findToolsLanguages();
    if (d.toolsLanguages.empty()) return; // (none there: English, and their own folder isn't looked at, so a player's computer gets none)
    auto known = [&](std::string_view id) { return std::ranges::contains(d.toolsLanguages, id, &DeveloperTools::ToolsLanguage::id); };
    if (const auto chosen = readTextFile(toolsFolder() + "language.txt")) {
        const std::string id(trim(*chosen));
        return useToolsLanguage(known(id) ? id : std::string{}); // (its file gone: English)
    }
    int count = 0; // (none chosen: the computer's language, when there's a translation for it)
    const SdlOwned<SDL_Locale*> locales{SDL_GetPreferredLocales(&count)};
    for (int i = 0; locales && i < count; ++i) {
        const std::string language = lower(locales.get()[i]->language), country = locales.get()[i]->country ? lower(locales.get()[i]->country) : "";
        for (const DeveloperTools::ToolsLanguage& l : d.toolsLanguages)
            if (!l.code.empty() && (l.code == language + "-" + country || l.code == language)) return useToolsLanguage(l.id);
    }
}

void Developer::writeToolsTranslation() {
    DeveloperTools& d = tools;
    const std::string folder = toolsLanguagesFolder();
    SDL_CreateDirectory(folder.c_str());
    const std::string id = d.toolsLanguage.empty() ? "new" : d.toolsLanguage;
    const std::string path = folder + id + ".vnscript";
    const auto existing = readTextFile(path);
    std::vector<ScriptError> errors;
    const Translation had = existing ? Translation::parse(id, *existing, errors) : Translation{};
    std::string out = existing ? *existing
                               : "# The developer tools' texts in another language: under each one, indented, its translation (one left as \"\"\n"
                                 "# stays in English). Keep each {} in it: a name, a number or a file goes there. Change the language line to\n"
                                 "# the language's own name and its code (es, pt-br, ja...), name the file after the language\n"
                                 "# (spanish.vnscript), keep it here, next to VNext, and choose it with its button in the developer tools.\n"
                                 "language \"New language\"\n";
    std::size_t added = 0;
    for (const std::string& text : toolsTexts()) {
        if (text.contains('\n') || (text.contains('"') && text.contains('#')) || had.texts.contains(text)) continue; // (a text a line: as the file has them)
        if (existing && added == 0) out += std::string(out.ends_with('\n') ? "" : "\n") + "\n# Texts added since the file was made.\n";
        out += std::format("\n{}\n    \"\"\n", text.contains('"') ? text : "\"" + text + "\"");
        ++added;
    }
    d.failed = !SDL_SaveFile(path.c_str(), out.data(), out.size());
    if (d.failed) {
        d.status = "Can't write '{}': {}."_tr(path, SDL_GetError());
        return;
    }
    d.status = existing ? "Added {} texts the developer tools show to 'developer tools translations/{}.vnscript'."_tr(added, id)
                        : "Made 'developer tools translations/{}.vnscript', with the {} texts the developer tools show, ready to translate."_tr(id, added);
    findToolsLanguages();
    if (!SDL_OpenURL(fileUrl(folder).c_str())) d.status += " " + std::string("Can't show '{}': {}."_tr(folder, SDL_GetError()));
}

namespace {

ToolsTranslation& toolsTranslation() {
    static ToolsTranslation texts;
    return texts;
}

std::vector<std::pair<std::string_view, TextShape>>& listedTexts() {
    static std::vector<std::pair<std::string_view, TextShape>> texts;
    return texts;
}

} // namespace

std::string_view tr(std::string_view english) {
    const ToolsTranslation& texts = toolsTranslation();
    if (texts.empty()) return english;
    const auto it = texts.find(english);
    return it != texts.end() && !it->second.empty() ? std::string_view(it->second) : english;
}

std::string said(std::string_view english, std::format_args args) {
    for (const std::string_view format : {tr(english), english}) {
        try {
            return std::vformat(format, args);
        } catch (const std::format_error&) { // (a translation's {}s that don't fit: as written)
        }
    }
    return std::string(english);
}

void useToolsTranslation(ToolsTranslation texts) { toolsTranslation() = std::move(texts); }

std::pair<std::string_view, std::string_view> suggestionParts(std::string_view suggestion) {
    const auto colon = suggestion.find(':');
    if (colon == std::string_view::npos || colon + 1 >= suggestion.size() || !(letterByte(suggestion[colon + 1]) || suggestion[colon + 1] == '('))
        return {suggestion, {}};
    return {suggestion.substr(0, colon), suggestion.substr(colon + 1)};
}

void listToolsText(std::string_view text, TextShape shape) {
    if (!text.empty()) listedTexts().emplace_back(text, shape);
}

std::vector<std::string> toolsTexts() {
    static_cast<void>(StoryEditor::kindOf(StoryEditor::Box::None)); // (its table is made when it's first asked for)
    std::vector<std::string> out;
    std::set<std::string, std::less<>> seen;
    auto add = [&](std::string_view text) {
        text = trim(text);
        if (std::ranges::none_of(text, [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || (static_cast<unsigned char>(c) & 0x80); })) return;
        if (seen.emplace(text).second) out.emplace_back(text);
    };
    for (const auto& [text, shape] : listedTexts()) {
        switch (shape) {
        case TextShape::Text:
        case TextShape::Group: add(text); break;
        case TextShape::Labels:
            for (const std::string_view label : wordsOf(text, '|')) add(label);
            break;
        case TextShape::Suggestions: // (values, as written; what one is, in words)
            for (const std::string_view suggestion : wordsOf(text, '|')) add(suggestionParts(suggestion).second);
            break;
        case TextShape::Hint: // (words; not a value, as 100% or 1)
            if (std::isupper(static_cast<unsigned char>(text.front()))) add(text);
            break;
        case TextShape::Choices: // (as their buttons say them)
            for (const std::string_view word : wordsOf(text)) add(upperFirst(spaced(word)));
            break;
        }
    }
    return out;
}

} // namespace vn
