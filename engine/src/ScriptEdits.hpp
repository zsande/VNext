#pragma once

// For the developer tools' files only: the scripts' text as the tools change it (ScriptEdits.cpp). Its helpers and the
// forms' are in a namespace of their own (dev) that the developer files use as if each had declared them.

#include "Developer.hpp"
#include "Engine.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vn {

namespace dev {

constexpr std::size_t npos = std::string_view::npos;
constexpr int kIndent = 4; // spaces for each level the editor adds

std::string leading(std::string_view line);
bool blank(std::string_view line);
bool partLine(std::string_view line); // (not a blank line or a note)

// A text's lines (a line break at its end: an empty last line; a CR before one: dropped).
std::vector<std::string> linesOf(std::string_view text);

// An index into a list that may have shrunk: none, once it's past the end.
void clampIndex(std::optional<std::size_t>& index, std::size_t count);
std::string_view afterFirst(std::string_view s);
bool isWordChar(char c);
std::string joined(const std::vector<std::string>& words, std::size_t from = 0, std::string_view sep = " ");

// A number as screens write their places and shares: at most 4 places after the point (0.5, -0.0625).
std::string unitText(float v);

// placeEdge() the other way round: the place that puts the left (or top) edge `at` (from the start of a
// box `span` big) of something `size` big (0: placed by its corner).
float placeFor(float at, float size, float span);

// A setting's value as it's written: in quotes when it has a space (or ; or =) in it.
std::string written(const std::string& value);
bool validName(std::string_view s);

// Words without their text tags ({bold}, {pause}...; {name} stays: it's filled in).
constexpr std::string_view kTextTags[]{"bold", "italic", "underline", "crossed_out", "colour", "speed", "furigana", "tip", "pause", "no_wait", "appears"};

std::string withoutTags(std::string_view value);

// The folder a game folder is in (its pack, story.txt and spreadsheets go there).
std::string parentFolder(const std::string& root); // (root ends with a slash)

// `line` for a game `sy` times as high: its text sizes and padding grown or shrunk to fit (places and
// shares fit any size as they are).
std::string scaled(const std::string& line, float sy);

bool nameChar(char c);

// Screen lines written in a game's pixels (for `width` x `height`), written in places and shares instead.
// `sizes`: the size a part without one of its own has (a text: its words), by line, so it's put with its top
// left where the line has it (else as if it had none; Developer::ownSizes measures them).
std::vector<std::string> placesFromPixels(const std::vector<std::string>& lines, float width, float height, const std::map<std::size_t, SDL_FPoint>& sizes = {});

} // namespace dev

using namespace dev; // (the developer files' own helpers: theirs, as if declared in each)
// What the editor's files (StoryEditor, StoryEditorForms, StoryEditorChanges, StoryEditorScreens, StoryEditorLibrary)
// share: lines without the indentation they share; whether something in `all` (with a `name`) is called `name` (case
// doesn't matter); a numbered field's name ("Value", "Value 2"...); a script's path by its name, and whether a line
// includes it (`name` in lower case); a file name that's what scripts call it; a phone line that does something
// rather than say it ("Mika sends beach"); a definition's form; a form's line; the action groups' and kinds' lists
// (what the first and second boxes list); the game's languages' list; a line with a scene's (a screen's, an
// animation's, an asset's, a variable's) name changed where it's used; a setting line's name and value.
[[nodiscard]] std::vector<std::string> unshared(std::span<const std::string> lines);
template <class T> bool hasNamed(const std::vector<T>& all, std::string_view name) {
    const std::string low = lower(name);
    return std::ranges::any_of(all, [&](const T& one) { return lower(one.name) == low; });
}
[[nodiscard]] std::string numbered(const std::string& name, std::size_t i);
[[nodiscard]] std::string scriptPath(std::string_view name);
[[nodiscard]] bool includesScript(std::string_view line, const std::string& name);
[[nodiscard]] bool usableFileName(std::string_view file);
[[nodiscard]] bool phoneDoes(std::string_view t);
[[nodiscard]] std::string renamed(const std::string& line, std::string_view from, std::string_view to);
[[nodiscard]] std::string screenRenamed(const std::string& line, std::string_view from, std::string_view to);
[[nodiscard]] std::string animationRenamed(std::string line, std::string_view from, std::string_view to);
// `line` with the sound `from` called `to` wherever a story line or an action plays or stops it (play NAME, stop NAME, play
// song NAME); `type`: the sound type `from`, played as (play NAME as TYPE) or stopped (stop TYPE).
[[nodiscard]] std::string soundRenamed(std::string line, std::string_view from, std::string_view to, bool type);
// `line` with the visual `from` called `to` wherever a story line or an action shows or hides it (show NAME, hide NAME);
// `type`: the visual type `from`, shown as (show NAME as TYPE).
[[nodiscard]] std::string visualRenamed(std::string line, std::string_view from, std::string_view to, bool type);
[[nodiscard]] std::string assetRenamed(const std::string& line, std::string_view folder, const std::string& from, const std::string& to,
                                       const std::vector<std::pair<std::string, std::string>>& bases);
[[nodiscard]] std::string lineWithVariable(const std::string& line, std::string_view from, std::string_view to);
[[nodiscard]] std::optional<std::pair<std::string, std::string_view>> settingOf(std::string_view line);

} // namespace vn
