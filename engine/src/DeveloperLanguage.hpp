#pragma once

// The developer tools' own words ("..."_tr and the like), in English or in the tools' language, and listed for a
// tools translation.

#include "Basics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vn {

// (In this header, not the developer tools' own, so that the scripts' mistakes can be said in it too;
// DeveloperLanguage.cpp has the code.)
// The developer tools' texts are written in English, and shown in the language chosen for them (their language buttons):
// a translation's text for each one (Developer::useToolsLanguage), else as written. A text in the code
// is "..."_tr, shown in that language where it's used (as text, or with values put in its {}s as
// std::format does: "Imported '{}'."_tr(name)); one kept in English, because the code tells it apart by
// its words, is "..."_text (tr() shows it). The tables' texts are English as well (ToolsWords), and
// shown with tr(). Every one is listed as the player starts (listToolsText), whether its code runs or
// not, for the file a translator starts from (toolsTexts).
//
// How a text is made up: one text; labels, "|" between them; suggestions, "value:what it is" with "|"
// between them (what each is is the text); what an empty box shows (a text when it's words, not a
// value); a form's heading ("+" before it: folded at first); the words a box can be (their buttons say
// them with a capital, each a text).
enum class TextShape : std::uint8_t { Text, Labels, Suggestions, Hint, Group, Choices };
using ToolsTranslation = std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>;
[[nodiscard]] std::string_view tr(std::string_view english);
[[nodiscard]] std::string said(std::string_view english, std::format_args args); // tr()'s, with values in its {}s (as written, when a translation's {}s don't fit them)
void useToolsTranslation(ToolsTranslation texts); // (none: English)
void listToolsText(std::string_view text, TextShape shape);
[[nodiscard]] std::vector<std::string> toolsTexts(); // each one, once, in the order they were listed
// One of a box's suggestions: its value, and what it is when it says, after a colon ("0:a fifth of the box" is 0, listed
// as "0: a fifth of the box"; "Japanese:(ja)" is Japanese, listed as "Japanese (ja)"; a time's colon, 8:00, has a
// number after it).
[[nodiscard]] std::pair<std::string_view, std::string_view> suggestionParts(std::string_view suggestion);

// A text of the code, as written: a template argument, so that each one is listed as the player starts.
template <std::size_t N> struct ToolsLiteral {
    char text[N]{};
    consteval ToolsLiteral(const char (&s)[N]) { std::copy_n(s, N, text); }
    [[nodiscard]] constexpr std::string_view view() const { return {text, N - 1}; }
};
template <ToolsLiteral L, TextShape Shape> struct ListedText {
    static inline const bool listed = (listToolsText(L.view(), Shape), true);
};
// "..."_tr: the text in the tools' language, as text; called with values, those put in its {}s.
struct Tr {
    std::string_view english;
    operator std::string_view() const { return tr(english); }
    operator std::string() const { return std::string(tr(english)); }
    template <typename... Args> [[nodiscard]] std::string operator()(const Args&... args) const { return said(english, std::make_format_args(args...)); }
};
template <ToolsLiteral L> Tr operator""_tr() {
    static_cast<void>(ListedText<L, TextShape::Text>::listed);
    return Tr{L.view()};
}
// A table's text: English as written (a string_view), listed when the table is made, as the player starts.
template <TextShape Shape> struct ToolsWords : std::string_view {
    constexpr ToolsWords() = default;
    template <std::size_t N> ToolsWords(const char (&s)[N]) : std::string_view(s, N - 1) { listToolsText(*this, Shape); }
    explicit constexpr ToolsWords(std::string_view s) : std::string_view(s) {} // (not listed: words that aren't the tools' own, the languages')
    operator std::string() const { return std::string(std::string_view(*this)); } // (into a form's fields)
};
using ToolsText = ToolsWords<TextShape::Text>;
using ToolsLabels = ToolsWords<TextShape::Labels>;
using ToolsSuggestions = ToolsWords<TextShape::Suggestions>;
using ToolsHint = ToolsWords<TextShape::Hint>;
using ToolsGroup = ToolsWords<TextShape::Group>;
using ToolsChoices = ToolsWords<TextShape::Choices>;
// The code's texts kept in English, of each shape: "Speaker"_text, "Waits|Goes on"_labels, "0:no limit|256"_suggest,
// "Default"_hint, "Look"_group, "left right"_choices.
template <ToolsLiteral L, TextShape Shape> ToolsWords<Shape> listedWords() {
    static_cast<void>(ListedText<L, Shape>::listed);
    return ToolsWords<Shape>(L.view());
}
template <ToolsLiteral L> ToolsText operator""_text() { return listedWords<L, TextShape::Text>(); }
template <ToolsLiteral L> ToolsLabels operator""_labels() { return listedWords<L, TextShape::Labels>(); }
template <ToolsLiteral L> ToolsSuggestions operator""_suggest() { return listedWords<L, TextShape::Suggestions>(); }
template <ToolsLiteral L> ToolsHint operator""_hint() { return listedWords<L, TextShape::Hint>(); }
template <ToolsLiteral L> ToolsGroup operator""_group() { return listedWords<L, TextShape::Group>(); }
template <ToolsLiteral L> ToolsChoices operator""_choices() { return listedWords<L, TextShape::Choices>(); }

} // namespace vn

// A table's text in std::format: as the string_view it is (GCC 14's library doesn't see that by itself).
template <vn::TextShape Shape> struct std::formatter<vn::ToolsWords<Shape>, char> : std::formatter<std::string_view, char> {
    template <class Context> auto format(const vn::ToolsWords<Shape>& words, Context& context) const {
        return std::formatter<std::string_view, char>::format(std::string_view(words), context);
    }
};
