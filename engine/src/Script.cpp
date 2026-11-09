// The scripts as read (Script): reading them (ScriptParser.cpp and the files after it), what they hold looked up by
// name, where a line is, and a screen part's settings.
#include "ScriptParser.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

namespace vn {

Script Script::load(const std::string& entry, const FileReader& read, std::string_view builtin) {
    Script script;
    parse::Parser(script, read).run(entry, builtin);
    std::ranges::stable_sort(script.errors, {}, [](const ScriptError& e) { return std::tie(e.file, e.line); });
    return script;
}

std::optional<std::size_t> Script::findScene(std::string_view name) const {
    if (auto it = scenes.find(name); it != scenes.end()) return it->second;
    return std::nullopt;
}

const CharacterDef* Script::character(std::string_view key) const {
    auto it = characters.find(key);
    return it != characters.end() ? &it->second : nullptr;
}

const Control* Script::control(std::string_view name) const {
    auto it = std::ranges::find(controls, name, &Control::name);
    return it != controls.end() ? &*it : nullptr;
}

const Script::Record* Script::record(RecordKind kind, std::string_view name) const {
    const auto& list = records(kind);
    auto it = std::ranges::find_if(list, [&](const Record& r) { return sameName(r.name, name); });
    return it != list.end() ? &*it : nullptr;
}

bool Script::isRecordVariable(std::string_view name) {
    return std::ranges::any_of(kRecordKinds, [&](std::string_view kind) { return name.starts_with(std::format("persistent.{}.", kind)); });
}

const ScreenDef* Script::screen(std::string_view name) const {
    auto it = screens.find(name);
    return it != screens.end() ? &it->second : nullptr;
}

std::pair<std::string, std::size_t> Script::locate(std::size_t pc) const {
    std::pair<std::string, std::size_t> best{"", pc};
    std::size_t bestPc = 0;
    for (const auto& [name, at] : scenes) {
        if (at <= pc && (best.first.empty() || at > bestPc || (at == bestPc && name < best.first))) {
            best = {name, pc - at};
            bestPc = at;
        }
    }
    return best;
}

std::string storyCommandOf(std::string_view line) {
    return parse::commandOf(line, true);
}

bool spoken(std::string_view line, std::string_view* speaker, std::string_view* words) {
    const std::string_view t = trim(line);
    if (isQuoted(t)) return false;
    const auto colon = t.find(':');
    if (colon == std::string_view::npos || colon == 0 || !(colon + 1 == t.size() || t[colon + 1] == ' ')) return false;
    const std::string_view who = trim(t.substr(0, colon));
    if (std::ranges::count(who, ' ') + 1 > 3 || who.find_first_of(",!?\"{};") != std::string_view::npos) return false;
    if (speaker) *speaker = who;
    if (words) *words = trim(t.substr(colon + 1));
    return true;
}

std::string unknownCommand(std::string_view line) {
    const std::string_view t = trim(line), word = t.substr(0, t.find_first_of(" \t"));
    const bool scriptWord = !word.empty() && word.front() >= 'a' && word.front() <= 'z' &&
                            std::ranges::all_of(word, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
    if (!scriptWord || !storyCommandOf(t).empty() || std::ranges::contains(parse::kDefinitions, word) || spoken(t)) return {};
    return std::string(word);
}

std::string Script::where(const Instruction& ins) const {
    return std::format("{} line {}", ins.file < files.size() ? files[ins.file] : "?", ins.line);
}

float Element::number(std::string_view name, float fallback) const {
    if (auto it = props.find(name); it != props.end())
        if (auto v = parseNumber(it->second)) return *v;
    return fallback;
}

std::string Element::option(std::string_view name, std::string_view fallback) const {
    if (auto it = props.find(name); it != props.end()) return it->second;
    return std::string(fallback);
}

bool Element::scrolls() const { return lower(option("scroll", "no")) == "yes"; }
bool Element::fitsParts() const { return lower(option("fit")) == "parts"; }
std::string Element::edges() const {
    const std::string own = lower(option("edges"));
    return !own.empty() ? own : scrolls() ? "cut" : "show";
}
bool Element::glides(bool usually) const {
    const std::string own = lower(option("glide"));
    return own.empty() ? usually : own != "no";
}
float Element::dragDirection() const {
    switch (kind) {
    case ElementKind::Container:
        return scrolls() ? -1.0f : 0.0f;
    case ElementKind::Flowchart:
        return -1.0f;
    default:
        return 0.0f;
    }
}

SDL_Color Element::colour(std::string_view name, SDL_Color fallback) const {
    if (auto it = colours.find(name); it != colours.end()) return it->second;
    return fallback;
}

const Style& Element::substyle(std::string_view name) const {
    if (auto it = substyles.find(name); it != substyles.end()) return it->second;
    return style;
}

} // namespace vn
