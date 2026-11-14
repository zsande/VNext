// The game's files, in its folder or in its pack (game.vnext, made by Export the game and --pack: its files in one, squeezed and scrambled),
// the player's own files, and the Trash.
#include "Files.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <format>
#include <span>
#include <unordered_map>
#include <utility>

namespace vn {

namespace {

// --- The pack ------------------------------------------------------------------------------
// A header: "VNXPACK1", the number of files (4 bytes), 4 empty bytes, then where the list of files
// starts and how long it is (8 bytes each). Then the files, one after the other, and the list at
// the end: for each file its path (2 bytes saying how long, then the letters), where it starts,
// how long it is in the pack and how long it really is (8 bytes each), how it's packed (1 byte:
// 0 as it is, 1 compressed) and its check (4 bytes). Numbers are little-endian; the list and the
// files are scrambled. Files added later (the players' saves and progress, in saves/) go after the list,
// then a new list, and only then does the header name that one: what's after the list it names is left over.
constexpr std::string_view kMagic = "VNXPACK1";
constexpr std::uint64_t kHeaderSize = 32;
constexpr std::uint64_t kListSeed = 0x564e657874ull; // scrambles the list (a file: where it starts)
constexpr std::string_view kSavesIn = "saves/";      // where in a pack its players' saves and progress are
constexpr std::uint64_t kLeftOver = 4ull << 20;      // left over from replaced saves before they're tidied

struct Entry {
    std::uint64_t offset = 0, stored = 0, size = 0;
    std::uint8_t method = 0;
    std::uint32_t check = 0;
};

void put(std::string& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out += static_cast<char>(value >> (8 * i) & 0xFF);
}

std::uint64_t get(std::string_view in, std::size_t& at, int bytes) {
    std::uint64_t value = 0;
    for (int i = 0; i < bytes; ++i) value |= std::uint64_t{static_cast<unsigned char>(in[at + static_cast<std::size_t>(i)])} << (8 * i);
    at += static_cast<std::size_t>(bytes);
    return value;
}

// Scrambles bytes with a stream of numbers made from `seed` (and unscrambles them: it's the same).
void scramble(std::span<char> bytes, std::uint64_t seed) {
    std::uint64_t s = (seed + 1) * 0x9E3779B97F4A7C15ull;
    if (s == 0) s = 1;
    for (std::size_t i = 0; i < bytes.size(); i += 8) {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        std::uint64_t key = s * 0x2545F4914F6CDD1Dull;
        for (std::size_t j = i; j < std::min(i + 8, bytes.size()); ++j, key >>= 8)
            bytes[j] = static_cast<char>(static_cast<unsigned char>(bytes[j]) ^ (key & 0xFF));
    }
}

std::uint32_t checkOf(std::string_view data) {
    const std::uint64_t h = fnv1a(data);
    return static_cast<std::uint32_t>(h ^ (h >> 32));
}

// A file's line in the list.
void putEntry(std::string& list, std::string_view path, const Entry& e) {
    put(list, path.size(), 2);
    list += path;
    put(list, e.offset, 8);
    put(list, e.stored, 8);
    put(list, e.size, 8);
    put(list, e.method, 1);
    put(list, e.check, 4);
}

// The header, for `count` files and the (scrambled) list at `listAt`, `listSize` long.
std::string headerOf(std::uint64_t count, std::uint64_t listAt, std::uint64_t listSize) {
    std::string header(kMagic);
    put(header, count, 4);
    put(header, 0, 4);
    put(header, listAt, 8);
    put(header, listSize, 8);
    return header;
}

// A file cut to `size` bytes, where the system can (what's after goes).
void cutFile(const std::string& path, std::uint64_t size) {
    std::u8string name(path.size(), u8'\0');
    std::memcpy(name.data(), path.data(), path.size());
    std::error_code error;
    std::filesystem::resize_file(std::filesystem::path(name), size, error);
}

// Compression: LZ77, like LZ4's blocks. A step is a token byte (its high 4 bits: how many bytes
// follow as they are; its low 4 bits: how long the repeat after them is, less 4; 15 means more
// follows, in bytes of 255 and a last smaller one), those bytes, then how far back the repeat
// starts (2 bytes). The last step has no repeat.
void putLength(std::string& out, std::size_t n) {
    for (; n >= 255; n -= 255) out += '\xff';
    out += static_cast<char>(n);
}

std::string compress(std::string_view in) {
    std::string out;
    out.reserve(in.size() / 2 + 16);
    std::vector<std::int64_t> seen(std::size_t{1} << 16, -1); // where 4 bytes with each hash were last
    auto word = [&](std::size_t i) {
        std::uint32_t v = 0;
        std::memcpy(&v, in.data() + i, 4);
        return v;
    };
    std::size_t anchor = 0; // the first byte not written yet
    auto step = [&](std::size_t end, std::size_t distance, std::size_t length) {
        const std::size_t literals = end - anchor, extra = length > 0 ? length - 4 : 0;
        out += static_cast<char>(std::min<std::size_t>(literals, 15) << 4 | std::min<std::size_t>(extra, 15));
        if (literals >= 15) putLength(out, literals - 15);
        out.append(in.substr(anchor, literals));
        if (length == 0) return;
        out += static_cast<char>(distance & 0xFF);
        out += static_cast<char>(distance >> 8);
        if (extra >= 15) putLength(out, extra - 15);
    };
    std::size_t misses = 0;
    for (std::size_t i = 0; i + 4 <= in.size();) {
        const std::uint32_t v = word(i);
        const std::int64_t before = std::exchange(seen[(v * 2654435761u) >> 16], static_cast<std::int64_t>(i));
        if (before >= 0 && i - static_cast<std::size_t>(before) <= 65535 && word(static_cast<std::size_t>(before)) == v) {
            const auto from = static_cast<std::size_t>(before);
            std::size_t length = 4;
            while (i + length < in.size() && in[from + length] == in[i + length]) ++length;
            step(i, i - from, length);
            i += length;
            anchor = i;
            misses = 0;
        } else {
            i += 1 + (misses++ >> 6); // faster through bytes that don't repeat
        }
    }
    step(in.size(), 0, 0);
    return out;
}

std::optional<std::string> decompress(std::string_view in, std::uint64_t size) {
    if (size > in.size() * 256 + 64) return std::nullopt; // more than it could hold
    std::string out(static_cast<std::size_t>(size), '\0');
    std::size_t p = 0, o = 0;
    auto more = [&](std::size_t& n) { // the bytes after a 15
        unsigned char b = 0;
        do {
            if (p >= in.size()) return false;
            b = static_cast<unsigned char>(in[p++]);
            n += b;
        } while (b == 255);
        return true;
    };
    while (p < in.size()) {
        const auto token = static_cast<unsigned char>(in[p++]);
        std::size_t literals = token >> 4;
        if (literals == 15 && !more(literals)) return std::nullopt;
        if (literals > in.size() - p || literals > out.size() - o) return std::nullopt;
        std::memcpy(out.data() + o, in.data() + p, literals);
        p += literals;
        o += literals;
        if (p == in.size()) break; // the last step
        if (in.size() - p < 2) return std::nullopt;
        const std::size_t low = static_cast<unsigned char>(in[p++]);
        const std::size_t distance = low | std::size_t{static_cast<unsigned char>(in[p++])} << 8;
        std::size_t length = (token & 15u) + 4;
        if ((token & 15) == 15 && !more(length)) return std::nullopt;
        if (distance == 0 || distance > o || length > out.size() - o) return std::nullopt;
        for (const std::size_t end = o + length; o < end; ++o) out[o] = out[o - distance];
    }
    if (o != out.size()) return std::nullopt;
    return out;
}

// Files that are compressed already (images, sound, movie) are packed as they are.
bool compressible(std::string_view path) {
    const auto dot = path.find_last_of('.');
    const std::string ext = dot == std::string_view::npos ? std::string() : lower(path.substr(dot));
    static constexpr std::string_view kPacked[]{".png", ".jpg", ".jpeg", ".gif", ".qoi", ".ogg", ".mp3", ".flac"};
    return !std::ranges::contains(kPacked, ext) && !std::ranges::contains(kMovieTypes, ext);
}

// Every file in folder + sub, by its path from folder, but saves/, packs and hidden files.
void collect(const std::string& folder, const std::string& sub, std::vector<std::string>& out) {
    std::vector<std::string> names;
    SDL_EnumerateDirectory(
        (folder + sub).c_str(),
        [](void* user, const char*, const char* name) -> SDL_EnumerationResult {
            return withoutThrowing(SDL_ENUM_FAILURE, [&] {
                static_cast<std::vector<std::string>*>(user)->emplace_back(name);
                return SDL_ENUM_CONTINUE;
            });
        },
        &names);
    for (const std::string& name : names) {
        if (name.starts_with('.') || (sub.empty() && lower(name) == "saves") || lower(name).ends_with(".vnext")) continue;
        const std::string path = sub + name;
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo((folder + path).c_str(), &info)) continue;
        if (info.type == SDL_PATHTYPE_DIRECTORY) collect(folder, path + "/", out);
        else if (info.type == SDL_PATHTYPE_FILE) out.push_back(path);
    }
}

} // namespace

struct GameFiles::Pack {
    std::string file; // its path
    std::string root; // file + "/": the paths of the files in it start with this
    std::unordered_map<std::string, Entry, StringHash, std::equal_to<>> entries; // by lower-case path
    std::vector<std::string> paths;                                              // as written, in order
    std::uint64_t listAt = 0, listSize = 0;                                      // where the list the header names is

    // The path of a file in it, as its list has it (lower case), if it's under its root.
    [[nodiscard]] std::optional<std::string> keyOf(std::string_view path) const {
        if (!path.starts_with(root)) return std::nullopt;
        std::string key = lower(path.substr(root.size()));
        std::ranges::replace(key, '\\', '/');
        return key;
    }
    [[nodiscard]] std::optional<std::string> read(const std::string& key) const {
        const auto it = entries.find(key);
        if (it == entries.end()) {
            SDL_SetError("%s isn't in %s", key.c_str(), file.c_str());
            return std::nullopt;
        }
        const Entry& e = it->second;
        std::string stored(static_cast<std::size_t>(e.stored), '\0');
        IOStreamPtr io{SDL_IOFromFile(file.c_str(), "rb")};
        if (!io) return std::nullopt;
        const bool done = SDL_SeekIO(io.get(), static_cast<Sint64>(e.offset), SDL_IO_SEEK_SET) >= 0 &&
                          SDL_ReadIO(io.get(), stored.data(), stored.size()) == stored.size();
        io.reset();
        if (done) {
            scramble(stored, e.offset);
            auto data = e.method == 1 ? decompress(stored, e.size) : std::optional(std::move(stored));
            if (data && data->size() == e.size && checkOf(*data) == e.check) return data;
        }
        logError("'{}' is damaged: '{}' can't be read from it.", file, key);
        SDL_SetError("%s is damaged", file.c_str());
        return std::nullopt;
    }

    // Files added, replaced (data given) or taken out (none): written from `at` (after the list the
    // header names, when none: what's there is left over), then the new list, and only then the header
    // naming it, so stopping halfway leaves the pack as it was.
    bool change(const std::vector<std::pair<std::string, std::optional<std::string>>>& changes, std::optional<std::uint64_t> at = std::nullopt) {
        auto now = entries;
        auto order = paths;
        const std::uint64_t from = at.value_or(listAt + listSize);
        std::uint64_t end = from;
        std::string written;
        for (const auto& [path, data] : changes) {
            std::string key = lower(path);
            if (!data) {
                if (now.erase(key) > 0) std::erase_if(order, [&](const std::string& p) { return lower(p) == key; });
                continue;
            }
            std::string stored = *data;
            scramble(stored, end);
            if (!now.contains(key)) order.push_back(path);
            now.insert_or_assign(std::move(key), Entry{.offset = end, .stored = stored.size(), .size = data->size(), .method = 0, .check = checkOf(*data)});
            end += stored.size();
            written += stored;
        }
        std::string list;
        for (const std::string& p : order) putEntry(list, p, now.find(lower(p))->second);
        scramble(list, kListSeed);
        const std::string header = headerOf(order.size(), end, list.size());
        IOStreamPtr io{SDL_IOFromFile(file.c_str(), "r+b")};
        const bool ok = io && SDL_SeekIO(io.get(), static_cast<Sint64>(from), SDL_IO_SEEK_SET) >= 0 &&
                        SDL_WriteIO(io.get(), written.data(), written.size()) == written.size() &&
                        SDL_WriteIO(io.get(), list.data(), list.size()) == list.size() && SDL_FlushIO(io.get()) &&
                        SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) == 0 && SDL_WriteIO(io.get(), header.data(), header.size()) == header.size();
        if (!io || !SDL_CloseIO(io.release()) || !ok) return false; // (closed here: what it says about the last writes counts)
        entries = std::move(now);
        paths = std::move(order);
        listAt = end;
        listSize = list.size();
        return true;
    }

    // Once what replaced saves left over is more than the saves themselves (and kLeftOver), they're
    // written again straight after its own files: first after the list, as any change, then from where
    // its own files end, which is all left over by then; and the file is cut after that.
    void tidy() {
        std::uint64_t own = kHeaderSize, saved = 0; // (where its own files end; what the saves take)
        std::vector<std::pair<std::string, std::optional<std::string>>> saves;
        for (const std::string& p : paths) {
            const Entry& e = entries.find(lower(p))->second;
            if (lower(p).starts_with(kSavesIn)) {
                saved += e.stored;
                saves.emplace_back(p, std::nullopt);
            } else {
                own = std::max(own, e.offset + e.stored);
            }
        }
        if (listAt < own + saved || listAt - own - saved <= std::max(kLeftOver, saved)) return;
        for (auto& [path, data] : saves)
            if (!(data = read(lower(path)))) return; // (a damaged save stays where it is)
        if (!change(saves)) return;
        if (own + saved + listSize > listAt - saved) return; // (the second copy would reach the first: kept as it is)
        if (change(saves, own)) cutFile(file, listAt + listSize);
    }
};

GameFiles::GameFiles() = default;
GameFiles::~GameFiles() = default;

const GameFiles& GameFiles::disk() {
    static const GameFiles none;
    return none;
}

bool GameFiles::exists(const std::string& path) const {
    if (const auto key = pack_ ? pack_->keyOf(path) : std::nullopt) return pack_->entries.contains(*key);
    return fileExists(path);
}

std::optional<std::string> GameFiles::read(const std::string& path) const {
    if (const auto key = pack_ ? pack_->keyOf(path) : std::nullopt) return pack_->read(*key);
    return readFile(path);
}

std::optional<std::string> GameFiles::readText(const std::string& path) const {
    auto text = read(path);
    if (text && text->starts_with("\xEF\xBB\xBF")) text->erase(0, 3); // UTF-8 BOM
    return text;
}

IOStreamPtr GameFiles::open(const std::string& path) const {
    const auto key = pack_ ? pack_->keyOf(path) : std::nullopt;
    if (!key) return openFile(path);
    const auto data = pack_->read(*key);
    if (!data) return nullptr;
    SdlOwned<> memory{SDL_malloc(std::max<std::size_t>(data->size(), 1))};
    if (!memory) return nullptr;
    std::memcpy(memory.get(), data->data(), data->size());
    IOStreamPtr io{SDL_IOFromConstMem(memory.get(), data->size())};
    if (!io || !SDL_SetPointerProperty(SDL_GetIOProperties(io.get()), SDL_PROP_IOSTREAM_MEMORY_FREE_FUNC_POINTER, reinterpret_cast<void*>(&SDL_free)))
        return nullptr; // (the stream closed, then `memory` frees the bytes)
    static_cast<void>(memory.release()); // (the stream frees it once it's closed)
    return io;
}

std::vector<std::string> GameFiles::list(const std::string& folder, std::string_view extension) const {
    const auto key = pack_ ? pack_->keyOf(withSlash(folder)) : std::nullopt;
    if (!key) return listFiles(folder, extension);
    std::vector<std::string> names;
    const std::string ext = lower(extension);
    for (const std::string& path : pack_->paths)
        if (const std::string low = lower(path); low.starts_with(*key) && low.ends_with(ext) && low.find('/', key->size()) == std::string::npos)
            names.push_back(path.substr(key->size()));
    std::ranges::sort(names);
    return names;
}

std::vector<std::string> GameFiles::packed() const {
    return pack_ ? pack_->paths : std::vector<std::string>{};
}

bool GameFiles::writable() const {
    return pack_ && IOStreamPtr{SDL_IOFromFile(pack_->file.c_str(), "r+b")} != nullptr;
}

bool GameFiles::write(const std::string& path, std::string_view data) {
    const auto key = pack_ ? pack_->keyOf(path) : std::nullopt;
    if (!key || !pack_->change({{*key, std::string(data)}})) return false;
    pack_->tidy();
    return true;
}

bool GameFiles::remove(const std::string& path) {
    const auto key = pack_ ? pack_->keyOf(path) : std::nullopt;
    if (!key || !pack_->entries.contains(*key) || !pack_->change({{*key, std::nullopt}})) return false;
    pack_->tidy();
    return true;
}

std::expected<void, std::string> GameFiles::openPack(const std::string& file) {
    const std::string name = file.substr(file.find_last_of("/\\") + 1);
    const auto damaged = std::unexpected(std::format("'{}' is damaged, or isn't a VNext pack: make it again in developer mode.", name));
    IOStreamPtr io{SDL_IOFromFile(file.c_str(), "rb")};
    if (!io) { // (SDL's words name the file too)
        const std::string why = SDL_GetError();
        return std::unexpected(SDL_GetPathInfo(file.c_str(), nullptr) ? std::format("Can't open '{}': {}.", name, why) : std::format("There's no game at '{}'.", file));
    }
    std::string header(kHeaderSize, '\0'), list;
    bool ok = SDL_ReadIO(io.get(), header.data(), header.size()) == header.size() && header.starts_with(kMagic);
    std::size_t at = kMagic.size();
    std::uint64_t count = 0, listAt = 0, listSize = 0;
    if (ok) {
        count = get(header, at, 4);
        at += 4;
        listAt = get(header, at, 8);
        listSize = get(header, at, 8);
        const Sint64 size = SDL_GetIOSize(io.get());
        ok = size >= 0 && listAt >= kHeaderSize && listSize < (std::uint64_t{1} << 30) &&
             listAt + listSize <= static_cast<std::uint64_t>(size); // (what's after it: left over from a change that stopped halfway)
        if (ok) {
            list.resize(static_cast<std::size_t>(listSize));
            ok = SDL_SeekIO(io.get(), static_cast<Sint64>(listAt), SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(io.get(), list.data(), list.size()) == list.size();
        }
    }
    io.reset();
    if (!ok) return damaged;
    scramble(list, kListSeed);
    auto opened = std::make_unique<Pack>(Pack{.file = file, .root = file + "/", .entries = {}, .paths = {}, .listAt = listAt, .listSize = listSize});
    at = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (list.size() - at < 2) return damaged;
        const auto length = static_cast<std::size_t>(get(list, at, 2));
        if (list.size() - at < length + 29) return damaged;
        std::string path = list.substr(at, length);
        at += length;
        Entry e;
        e.offset = get(list, at, 8);
        e.stored = get(list, at, 8);
        e.size = get(list, at, 8);
        e.method = static_cast<std::uint8_t>(get(list, at, 1));
        e.check = static_cast<std::uint32_t>(get(list, at, 4));
        if (e.offset < kHeaderSize || e.stored > listAt || e.offset > listAt - e.stored || e.method > 1 ||
            (e.method == 0 && e.stored != e.size))
            return damaged;
        opened->entries.insert_or_assign(lower(path), e);
        opened->paths.push_back(std::move(path));
    }
    if (at != list.size()) return damaged;
    pack_ = std::move(opened);
    return {};
}

bool fileExists(const std::string& path) {
    SDL_PathInfo info{};
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

void removeAll(const std::string& path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info)) return;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        std::vector<std::string> inside;
        SDL_EnumerateDirectory(
            path.c_str(),
            [](void* data, const char*, const char* name) {
                static_cast<std::vector<std::string>*>(data)->emplace_back(name);
                return SDL_ENUM_CONTINUE;
            },
            &inside);
        for (const std::string& name : inside) removeAll(withSlash(path) + name);
    }
    SDL_RemovePath(path.c_str());
}

std::string withSlash(std::string path) {
    if (!path.empty() && !path.ends_with('/') && !path.ends_with('\\')) path += '/';
    return path;
}

bool isDirectory(const std::string& path) {
    SDL_PathInfo info{};
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

std::optional<std::string> readFile(const std::string& path) {
    std::size_t size = 0;
    const SdlOwned<> data{SDL_LoadFile(path.c_str(), &size)};
    if (!data) return std::nullopt;
    return std::string(static_cast<const char*>(data.get()), size);
}

std::optional<std::string> readTextFile(const std::string& path) {
    auto text = readFile(path);
    if (text && text->starts_with("\xEF\xBB\xBF")) text->erase(0, 3); // UTF-8 BOM
    return text;
}

IOStreamPtr openFile(const std::string& path) {
    return IOStreamPtr{SDL_IOFromFile(path.c_str(), "rb")};
}

std::string scriptName(std::string_view stem) {
    std::string out;
    for (const char c : stem) {
        const bool word = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || static_cast<unsigned char>(c) >= 0x80;
        if (word) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (out.ends_with('_') && out.size() > 1 && !stem.ends_with('_')) out.pop_back();
    return out;
}

std::vector<std::string> listFiles(const std::string& folder, std::string_view extension) {
    std::vector<std::string> names;
    const std::string ext = lower(extension);
    int count = 0;
    if (const SdlOwned<char*> found{SDL_GlobDirectory(folder.c_str(), ("*" + ext).c_str(), SDL_GLOB_CASEINSENSITIVE, &count)})
        for (int i = 0; i < count; ++i) names.emplace_back(found.get()[i]);
    std::ranges::sort(names);
    return names;
}

std::expected<PackReport, std::string> writePack(const std::string& folder, const std::string& file) {
    std::vector<std::string> paths;
    collect(folder, "", paths);
    std::ranges::sort(paths);
    if (std::ranges::none_of(paths, [](const std::string& p) { return lower(p) == "scripts/main.vnscript"; }))
        return std::unexpected(std::format("'{}' has no scripts/main.vnscript, so it isn't a game folder.", folder));
    const std::string part = file + ".part";
    ScopeExit unfinished([&part] { SDL_RemovePath(part.c_str()); }); // (the part written so far goes, unless the pack is finished: closed before that, as `out` goes first)
    IOStreamPtr out{SDL_IOFromFile(part.c_str(), "wb")};
    if (!out) return std::unexpected(std::format("Can't write '{}': {}.", file, SDL_GetError()));
    auto fail = [](std::string message) { return std::unexpected(std::move(message)); };
    const std::string blank(kHeaderSize, '\0'); // the header goes here at the end
    if (SDL_WriteIO(out.get(), blank.data(), blank.size()) != blank.size()) return fail(std::format("Can't write '{}': {}.", file, SDL_GetError()));
    PackReport report;
    std::string list;
    std::uint64_t at = kHeaderSize;
    for (const std::string& path : paths) {
        auto data = readFile(folder + path);
        if (!data) return fail(std::format("Can't read '{}': {}.", path, SDL_GetError()));
        if (path.size() > 0xFFFF) return fail(std::format("Can't pack '{}': its path is too long.", path));
        Entry e{.offset = at, .stored = 0, .size = data->size(), .method = 0, .check = checkOf(*data)};
        if (compressible(path))
            if (std::string small = compress(*data); small.size() < data->size() - data->size() / 16) {
                *data = std::move(small);
                e.method = 1;
            }
        e.stored = data->size();
        scramble(*data, at);
        if (SDL_WriteIO(out.get(), data->data(), data->size()) != data->size()) return fail(std::format("Can't write '{}': {}.", file, SDL_GetError()));
        putEntry(list, path, e);
        at += e.stored;
        ++report.files;
        report.bytes += e.size;
    }
    scramble(list, kListSeed);
    const std::string header = headerOf(paths.size(), at, list.size());
    if (SDL_WriteIO(out.get(), list.data(), list.size()) != list.size() || SDL_SeekIO(out.get(), 0, SDL_IO_SEEK_SET) != 0 ||
        SDL_WriteIO(out.get(), header.data(), header.size()) != header.size())
        return fail(std::format("Can't write '{}': {}.", file, SDL_GetError()));
    if (!SDL_CloseIO(out.release()) || !SDL_RenamePath(part.c_str(), file.c_str())) // (closed here: what it says about the last writes counts)
        return fail(std::format("Can't write '{}': {}.", file, SDL_GetError()));
    unfinished.dismiss();
    report.packed = at + list.size();
    return report;
}

} // namespace vn

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <objc/message.h>
#include <objc/runtime.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX // (MinGW's C++ library has it already)
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace vn {

std::optional<std::string> moveToTrash(const std::string& path) {
#if defined(__APPLE__) // NSFileManager's trashItemAtURL (through the Objective-C runtime, from C++)
    const Owned<CFURLRef, CFRelease> url{CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false)};
    if (!url) return std::nullopt;
    const auto manager = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(objc_getClass("NSFileManager"), sel_registerName("defaultManager"));
    id there = nullptr; // (an NSURL, which is a CFURL)
    const bool ok = manager && reinterpret_cast<BOOL (*)(id, SEL, id, id*, id*)>(objc_msgSend)(
                                   manager, sel_registerName("trashItemAtURL:resultingItemURL:error:"), reinterpret_cast<id>(const_cast<__CFURL*>(url.get())), &there, nullptr);
    if (!ok) return std::nullopt;
    char where[4096]{};
    if (there && CFURLGetFileSystemRepresentation(reinterpret_cast<CFURLRef>(there), true, reinterpret_cast<UInt8*>(where), sizeof(where))) return std::string(where);
    return std::string{};
#elif defined(_WIN32) // the Recycle Bin: a delete that can be undone (where it is there, the player can't tell)
    std::string native = path;
    std::ranges::replace(native, '/', '\\');
    const int n = MultiByteToWideChar(CP_UTF8, 0, native.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::nullopt;
    std::wstring wide(static_cast<std::size_t>(n) + 1, L'\0'); // (ends with two zeros)
    MultiByteToWideChar(CP_UTF8, 0, native.c_str(), -1, wide.data(), n);
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = wide.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) return std::string{};
    return std::nullopt;
#else // the freedesktop.org Trash: the file in Trash/files, and where it was (and when) in Trash/info
    const char* data = SDL_getenv("XDG_DATA_HOME");
    const char* home = SDL_getenv("HOME");
    const std::string trash = data && *data ? std::string(data) + "/Trash" : home && *home ? std::string(home) + "/.local/share/Trash" : std::string{};
    if (trash.empty() || !SDL_CreateDirectory((trash + "/files").c_str()) || !SDL_CreateDirectory((trash + "/info").c_str())) return std::nullopt;
    std::string full = path;
    if (!full.starts_with('/'))
        if (const SdlOwned<char> here{SDL_GetCurrentDirectory()}) full = here.get() + full;
    const std::string base = full.substr(full.find_last_of('/') + 1);
    const std::string name = uniqueName(base, [&](const std::string& n) {
        return SDL_GetPathInfo((trash + "/files/" + n).c_str(), nullptr) || SDL_GetPathInfo((trash + "/info/" + n + ".trashinfo").c_str(), nullptr);
    }, ".");
    const std::string escaped = percentEscaped(full);
    SDL_DateTime t{};
    SDL_Time now = 0;
    if (SDL_GetCurrentTime(&now)) SDL_TimeToDateTime(now, &t, true);
    const std::string info = std::format("[Trash Info]\nPath={}\nDeletionDate={:04}-{:02}-{:02}T{:02}:{:02}:{:02}\n", escaped, t.year, t.month, t.day, t.hour, t.minute, t.second);
    const std::string infoPath = trash + "/info/" + name + ".trashinfo", to = trash + "/files/" + name;
    if (!SDL_SaveFile(infoPath.c_str(), info.data(), info.size())) return std::nullopt;
    if (SDL_RenamePath(full.c_str(), to.c_str()) || (SDL_CopyFile(full.c_str(), to.c_str()) && SDL_RemovePath(full.c_str()))) return to;
    SDL_RemovePath(infoPath.c_str());
    return std::nullopt;
#endif
}

std::string fileUrl(std::string_view path) {
    std::string slashed(path);
    std::ranges::replace(slashed, '\\', '/');
    return (path.starts_with('/') ? "file://" : "file:///") + percentEscaped(slashed, "/-_.~:"); // (Windows: file:///C:/...)
}

} // namespace vn
