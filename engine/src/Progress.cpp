// The player's progress, kept across plays: what's persistent, and the lines already read.
#include "Engine.hpp"

#include <charconv>
#include <format>
#include <ranges>

namespace vn {

ReadLog readLogOf(std::string_view saved) {
    ReadLog log;
    for (auto&& part : std::views::split(saved, '\n')) {
        const std::string_view line(part.begin(), part.end());
        std::uint64_t h = 0;
        if (std::from_chars(line.data(), line.data() + line.size(), h, 16).ec == std::errc{}) log.insert(h);
    }
    return log;
}

std::string readLogText(const ReadLog& log) {
    std::string out;
    out.reserve(log.size() * 17);
    for (auto h : log) out += std::format("{:016x}\n", h);
    return out;
}

void Engine::loadPersistent() {
    persistent_.clear();
    const auto text = readProgress("persistent.txt");
    if (!text) return;
    for (auto&& part : std::views::split(std::string_view(*text), '\n')) {
        const std::string_view line(part.begin(), part.end());
        const auto space = line.find(' ');
        if (space != std::string_view::npos) persistent_.insert_or_assign(std::string(line.substr(0, space)), decodeValue(line.substr(space + 1)));
    }
}

void Engine::savePersistent() {
    std::string out;
    for (const auto& [name, value] : persistent_) out += std::format("{} {}\n", name, encodeValue(value));
    if (!writeProgress("persistent.txt", out)) logWarn("Can't save what's kept between games: {}.", SDL_GetError());
}

std::optional<std::string> Engine::readProgress(std::string_view file) const {
    if (inPack_) return files_.readText(gameRoot_ + "saves/" + std::string(file));
    return prefPath_.empty() ? std::nullopt : readTextFile(prefPath_ + std::string(file));
}

bool Engine::writeProgress(std::string_view file, std::string_view text) {
    if (inPack_) return files_.write(gameRoot_ + "saves/" + std::string(file), text);
    return prefPath_.empty() || SDL_SaveFile((prefPath_ + std::string(file)).c_str(), text.data(), text.size());
}

} // namespace vn
