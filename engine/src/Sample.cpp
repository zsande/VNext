// The samples built into VNext (Sample.hpp): where this player has them, a game folder made with them, and a player without them.
#include "Sample.hpp"

#include "Basics.hpp"
#include "DeveloperLanguage.hpp"
#include "Files.hpp"
#include "Platform.hpp"

#include <cstdint>
#include <string_view>

namespace vn {

namespace {

constexpr std::string_view kSampleMark = "VNXSMPL1";         // (a player's very last bytes, when it carries the sample)
constexpr std::size_t kTail = 8 + kSampleMark.size();       // (before the mark: the sample's size, little-endian)

// The program file the sample is at the end of: the .exe, or the AppImage (a Linux player run as it is: itself). A Mac's
// app has it in its Resources instead.
std::string programPath() {
#if defined(SDL_PLATFORM_WINDOWS)
    return programFile();
#elif defined(SDL_PLATFORM_LINUX)
    if (const char* image = SDL_getenv("APPIMAGE")) return image;
    return "/proc/self/exe";
#else
    return {};
#endif
}

// The size the 8 bytes before the mark say, at the end of `tail` (kTail bytes or more).
std::uint64_t sizeBefore(std::string_view tail) {
    std::uint64_t n = 0;
    for (std::size_t i = 8; i-- > 0;) n = n << 8 | static_cast<unsigned char>(tail[tail.size() - kTail + i]);
    return n;
}

// Where the sample is: the file, where the pack starts in it and how long it is.
struct Found {
    std::string file;
    std::uint64_t at = 0, size = 0;
};

std::uint32_t le(std::string_view bytes, std::size_t at, int n) {
    std::uint32_t v = 0;
    for (int i = n; i-- > 0;) v = v << 8 | static_cast<unsigned char>(bytes[at + static_cast<std::size_t>(i)]);
    return v;
}

// Where a player's own bytes end: its size, or a signed Windows .exe's before its certificates (signing adds them at the
// end, after the sample: its PE header's security directory says where they start, padded to 8 bytes with zeros before).
std::uint64_t ownEnd(SDL_IOStream* io, std::uint64_t size) {
    std::string head(1024, '\0');
    if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io, head.data(), head.size()) != head.size() || !head.starts_with("MZ")) return size;
    const std::size_t pe = le(head, 0x3C, 4), directories = pe + 24 + (pe + 26 <= head.size() && le(head, pe + 24, 2) == 0x20b ? 112 : 96);
    if (directories + 5 * 8 > head.size() || head.compare(pe, 4, std::string_view("PE\0\0", 4)) != 0) return size;
    const std::uint64_t at = le(head, directories + 4 * 8, 4), length = le(head, directories + 4 * 8 + 4, 4); // (the security directory)
    if (at < kTail || length == 0 || at + length != size) return size;
    std::string padding(8, '\0');
    if (SDL_SeekIO(io, static_cast<Sint64>(at - 8), SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io, padding.data(), padding.size()) != padding.size()) return size;
    std::uint64_t end = at;
    while (end > at - 7 && padding[static_cast<std::size_t>(end - (at - 8) - 1)] == '\0') --end;
    return end;
}

std::optional<Found> atEnd(const std::string& file) {
    IOStreamPtr io{SDL_IOFromFile(file.c_str(), "rb")};
    if (!io) return std::nullopt;
    const Sint64 size = SDL_GetIOSize(io.get());
    if (size < static_cast<Sint64>(kTail)) return std::nullopt;
    const std::uint64_t end = ownEnd(io.get(), static_cast<std::uint64_t>(size));
    std::string tail(kTail, '\0');
    if (end < kTail || SDL_SeekIO(io.get(), static_cast<Sint64>(end - kTail), SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io.get(), tail.data(), tail.size()) != tail.size() ||
        !tail.ends_with(kSampleMark))
        return std::nullopt;
    const std::uint64_t n = sizeBefore(tail), before = end - kTail;
    if (n == 0 || n > before) return std::nullopt;
    return Found{.file = file, .at = before - n, .size = n};
}

std::optional<Found> find() {
    if (const std::string program = programPath(); !program.empty())
        if (auto found = atEnd(program)) return found;
    const char* base = SDL_GetBasePath(); // (next to the player: in a Mac's app, its Resources)
    const std::string file = std::string(base ? base : "") + "sample.vnext";
    SDL_PathInfo info{};
    if (SDL_GetPathInfo(file.c_str(), &info) && info.type == SDL_PATHTYPE_FILE && info.size > 0) return Found{.file = file, .at = 0, .size = info.size};
    return std::nullopt;
}

} // namespace

bool hasSample() {
    return find().has_value();
}

std::optional<std::string> builtInSample() {
    const auto found = find();
    if (!found) return std::nullopt;
    IOStreamPtr io{SDL_IOFromFile(found->file.c_str(), "rb")};
    std::string bytes(static_cast<std::size_t>(found->size), '\0');
    if (!io || SDL_SeekIO(io.get(), static_cast<Sint64>(found->at), SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io.get(), bytes.data(), bytes.size()) != bytes.size())
        return std::nullopt;
    return bytes;
}

std::expected<void, std::string> writeSample(const std::string& folder) {
    const auto bytes = builtInSample();
    if (!bytes) return std::unexpected(std::string("This VNext has no samples."_tr));
    if (SDL_GetPathInfo(folder.c_str(), nullptr)) return std::unexpected("There's '{}' already: move it away first."_tr(folder)); // (a new folder only: what's made is all it takes away again)
    if (!SDL_CreateDirectory(folder.c_str())) return std::unexpected("Can't make '{}': {}."_tr(folder, SDL_GetError()));
    const std::string pack = folder + ".sample.vnext"; // (read from a file of its own, which then goes)
    ScopeExit gone([&pack] { SDL_RemovePath(pack.c_str()); });
    ScopeExit unfinished([&folder] { removeAll(folder); }); // (unless every file's written)
    if (!SDL_SaveFile(pack.c_str(), bytes->data(), bytes->size())) return std::unexpected("Can't write '{}': {}."_tr(pack, SDL_GetError()));
    GameFiles files;
    if (auto opened = files.openPack(pack); !opened) return std::unexpected(opened.error());
    for (const std::string& path : files.packed()) {
        if (lower(path).starts_with("saves/")) continue; // (a player's saves, if any: theirs)
        const auto data = files.read(pack + "/" + path);
        const std::string to = folder + path;
        const auto slash = to.find_last_of('/');
        if (!data || (slash != std::string::npos && !SDL_CreateDirectory(to.substr(0, slash).c_str())) || !SDL_SaveFile(to.c_str(), data->data(), data->size()))
            return std::unexpected("Can't write '{}': {}."_tr(to, SDL_GetError()));
    }
    unfinished.dismiss();
    return {};
}

std::string withoutSample(std::string player) {
    if (player.size() >= kTail && std::string_view(player).ends_with(kSampleMark))
        if (const std::uint64_t n = sizeBefore(player); n > 0 && n <= player.size() - kTail) player.resize(player.size() - kTail - static_cast<std::size_t>(n));
    return player;
}

} // namespace vn
