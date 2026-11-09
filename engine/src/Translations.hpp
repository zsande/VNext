#pragma once

// A game's translations: one read (its texts by the game's own), and its file made or brought up to date.

#include "Script.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vn {

// The game's texts in another language: game/translations/<id>.vnscript. Each text is written as
// the scripts have it, with its translation indented under it:
//
//     language "Español" code=es
//     font default "fonts/MyFont.ttf"
//
//     "Hello!"
//         "¡Hola!"
//
// Players choose it in Settings (the "language" setting). Texts it doesn't have show as written.
struct Translation {
    std::string id;   // the file's name ("spanish"); also its own images/<id>/, sounds/<id>/ and movies/<id>/
    std::string name; // as players see it ("Español")
    std::string code; // the language's code ("es", "pt-BR"): players whose computer uses it start with it
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> texts; // as written -> translated
    std::map<std::string, Script::FontFile, std::less<>> fonts; // used instead of the game's own

    // The translation of `original`, else `original`.
    [[nodiscard]] std::string text(const std::string& original) const;
    // Reads a translation file; problems go in `errors` (as translations/<id>.vnscript).
    static Translation parse(std::string id, std::string_view source, std::vector<ScriptError>& errors);
};

// --translate: a translations/<id>.vnscript file with every text `script` shows, ready to be
// translated. With `existing` (that file as it is now), the texts it doesn't have yet are added at
// its end, and the ones the game doesn't show any more (changed or taken out since) are taken out.
struct TranslationFile {
    std::string text;
    std::size_t added = 0;             // texts added
    std::vector<std::string> removed;  // texts `existing` had that the game doesn't show any more
};
TranslationFile translationFile(Script& script, std::string_view id, const std::optional<std::string>& existing);
// A translation file as it is, less the texts `script` doesn't show any more: each one's line, the
// translation under it and its note (a heading left with nothing under it too). `removed` gets them.
std::string prunedTranslation(Script& script, std::string_view id, std::string_view file, std::vector<std::string>& removed);

} // namespace vn
