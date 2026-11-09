// How .vnscript files are written, line by line (scripts and translations): notes, quotes and words.
#include "Script.hpp"

#include <cctype>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

std::optional<std::size_t> noteStart(std::string_view line) {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        if (line[i] != '#' || quoted) continue;
        const bool atStart = line.find_first_not_of(" \t") == i;
        const bool spaced = i > 0 && std::isspace(static_cast<unsigned char>(line[i - 1])) &&
                            (i + 1 == line.size() || std::isspace(static_cast<unsigned char>(line[i + 1])));
        if (atStart || spaced) return i;
    }
    return std::nullopt;
}

std::string_view stripNote(std::string_view line) {
    const auto at = noteStart(line);
    return at ? line.substr(0, *at) : line;
}

std::pair<std::string_view, std::string_view> splitNote(std::string_view line) {
    const auto at = noteStart(line);
    if (!at) return {trim(line), {}};
    return {trim(line.substr(0, *at)), line.substr(*at)};
}

bool isQuoted(std::string_view s, char q) {
    return s.size() >= 2 && s.front() == q && s.back() == q && s.substr(1, s.size() - 2).find(q) == std::string_view::npos;
}

std::string unquote(std::string_view s, bool single) {
    s = trim(s);
    if (isQuoted(s) || (single && isQuoted(s, '\''))) s = s.substr(1, s.size() - 2);
    return std::string(s);
}

std::string_view firstWord(std::string_view text) {
    const auto start = text.find_first_not_of(" \t");
    if (start == std::string_view::npos) return {};
    text.remove_prefix(start);
    return text.substr(0, text.find_first_of(" \t"));
}

std::vector<std::pair<std::size_t, std::size_t>> wordSpans(std::string_view line) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    bool inQuotes = false;
    std::optional<std::size_t> start;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        const bool end = i == line.size() || (std::isspace(static_cast<unsigned char>(line[i])) && !inQuotes);
        if (i < line.size() && line[i] == '"') inQuotes = !inQuotes;
        if (end) {
            if (start) out.emplace_back(*start, i - *start);
            start.reset();
        } else if (!start) {
            start = i;
        }
    }
    return out;
}

std::vector<std::string> rawWords(std::string_view line) {
    std::vector<std::string> out;
    for (const auto [at, n] : wordSpans(line)) out.emplace_back(line.substr(at, n));
    return out;
}

std::expected<std::vector<ScriptWord>, std::string> splitWords(std::string_view line) {
    std::vector<ScriptWord> out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
            continue;
        }
        ScriptWord w{.quoted = line[i] == '"'};
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) {
            if (line[i] != '"') {
                w.text += line[i++];
                continue;
            }
            const auto close = line.find('"', i + 1);
            if (close == std::string_view::npos) return std::unexpected("a quote mark (\") is opened but never closed"_tr());
            w.text += line.substr(i + 1, close - i - 1);
            i = close + 1;
        }
        out.push_back(std::move(w));
    }
    return out;
}

} // namespace vn
