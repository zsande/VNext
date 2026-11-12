// Exporting the game (the Game menu's Export the game): a zip for each system, named after the game, to give players
// (on itch.io and the like). In each, a folder named after the game with its player named after it (on a Mac, the game
// in it and its icon its own), the game packed (game.vnext) and licences.txt. The system the tools run on gives its own
// player; the others' come with it: each of VNext's releases has the other two systems' (players/: in a Mac's app, else
// next to the player), so nothing's downloaded.
#include "Developer.hpp"
#include "Engine.hpp"
#include "ScriptEdits.hpp"

#include <SDL3/SDL_process.h>
#include <SDL3_image/SDL_image.h>

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

namespace {

// The systems a game is exported for: what VNext's releases call them (a player that comes with another is players/NAME.zip),
// what the zips say, the player in a release's zip (a folder's name ends with a slash), and what it's called as the game.
struct System {
    std::string_view release, said, player, as;
};
constexpr std::array kSystems{System{"Windows", "Windows", "VNext.exe", ".exe"}, System{"macOS", "Mac", "VNext.app/", ".app"},
                              System{"Linux", "Linux", "VNext-Linux.AppImage", ".AppImage"}};
#if defined(SDL_PLATFORM_WINDOWS)
constexpr std::string_view kHere = "Windows";
#elif defined(SDL_PLATFORM_MACOS)
constexpr std::string_view kHere = "macOS";
#else
constexpr std::string_view kHere = "Linux";
#endif

// CRC-32, as zips check their files with it.
std::uint32_t crc32(std::uint32_t crc, const unsigned char* bytes, std::size_t n) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const unsigned char* p) { return static_cast<std::uint32_t>(le16(p)) | (static_cast<std::uint32_t>(le16(p + 2)) << 16); }
void put16(std::string& out, std::uint16_t v) {
    out += static_cast<char>(v & 0xFFu);
    out += static_cast<char>(v >> 8);
}
void put32(std::string& out, std::uint32_t v) {
    put16(out, static_cast<std::uint16_t>(v & 0xFFFFu));
    put16(out, static_cast<std::uint16_t>(v >> 16));
}

// A zip's file, as its list (the central directory) says it, and where its data starts.
struct ZipEntry {
    std::string name;
    std::uint16_t madeBy = 0, needs = 20, flags = 0, method = 0, time = 0, date = 0, internal = 0;
    std::uint32_t crc = 0, packed = 0, size = 0, external = 0;
    std::uint64_t data = 0;
};

bool readAt(SDL_IOStream* io, std::uint64_t at, void* into, std::size_t n) {
    return SDL_SeekIO(io, static_cast<Sint64>(at), SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(io, into, n) == n;
}

// The files of the zip `io` holds (none: not a zip this reads; one of more than 4 GB isn't).
std::optional<std::vector<ZipEntry>> zipEntries(SDL_IOStream* io) {
    const Sint64 size = SDL_GetIOSize(io);
    if (size < 22) return std::nullopt;
    const auto tail = static_cast<std::size_t>(std::min<Sint64>(size, 65557)); // (its end record, after a comment of up to 64 KB)
    std::string end(tail, '\0');
    if (!readAt(io, static_cast<std::uint64_t>(size) - tail, end.data(), tail)) return std::nullopt;
    const auto* e = reinterpret_cast<const unsigned char*>(end.data());
    std::optional<std::size_t> record;
    for (std::size_t i = tail - 22; i + 1 > 0 && !record; --i)
        if (le32(e + i) == 0x06054b50u) record = i;
    if (!record) return std::nullopt;
    const std::uint16_t count = le16(e + *record + 10);
    const std::uint32_t listSize = le32(e + *record + 12), listAt = le32(e + *record + 16);
    std::string list(listSize, '\0');
    if (!readAt(io, listAt, list.data(), listSize)) return std::nullopt;
    const auto* l = reinterpret_cast<const unsigned char*>(list.data());
    std::vector<ZipEntry> out;
    std::size_t at = 0;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (at + 46 > listSize || le32(l + at) != 0x02014b50u) return std::nullopt;
        ZipEntry z{.madeBy = le16(l + at + 4), .needs = le16(l + at + 6), .flags = le16(l + at + 8), .method = le16(l + at + 10), .time = le16(l + at + 12),
                   .date = le16(l + at + 14), .internal = le16(l + at + 36), .crc = le32(l + at + 16), .packed = le32(l + at + 20), .size = le32(l + at + 24),
                   .external = le32(l + at + 38)};
        const std::uint16_t nameLength = le16(l + at + 28), extra = le16(l + at + 30), comment = le16(l + at + 32);
        if (at + 46 + nameLength > listSize) return std::nullopt;
        z.name.assign(list, at + 46, nameLength);
        unsigned char local[30];
        const std::uint32_t localAt = le32(l + at + 42);
        if (!readAt(io, localAt, local, sizeof local) || le32(local) != 0x04034b50u) return std::nullopt;
        z.data = std::uint64_t{localAt} + 30 + le16(local + 26) + le16(local + 28);
        out.push_back(std::move(z));
        at += 46u + nameLength + extra + comment;
    }
    return out;
}

// A zip being written: files of another zip copied in as they are (not unpacked), under names of their own, and files
// added as they are (stored).
class ZipWriter {
public:
    explicit ZipWriter(const std::string& path) : out_(SDL_IOFromFile(path.c_str(), "wb")) { ok_ = out_ != nullptr; }
    [[nodiscard]] bool ok() const { return ok_; }

    // A file of `from` (an entry of its list), under another name.
    void copy(SDL_IOStream* from, ZipEntry e, std::string name) {
        e.name = std::move(name);
        e.flags = static_cast<std::uint16_t>((e.flags & ~0x0008u) | 0x0800u); // (its sizes in its own header; its name UTF-8)
        write(e, [&](SDL_IOStream* out) {
            std::string chunk(1 << 16, '\0');
            if (SDL_SeekIO(from, static_cast<Sint64>(e.data), SDL_IO_SEEK_SET) < 0) return false;
            for (std::uint32_t left = e.packed; left > 0;) {
                const std::size_t n = std::min<std::size_t>(left, chunk.size());
                if (SDL_ReadIO(from, chunk.data(), n) != n || SDL_WriteIO(out, chunk.data(), n) != n) return false;
                left -= static_cast<std::uint32_t>(n);
            }
            return true;
        });
    }
    // Bytes as a file (`mode`: its Unix permissions, 0755 a program).
    void add(std::string name, std::string_view bytes, std::uint32_t mode) {
        if (bytes.size() > 0xFFFFFFFFu) {
            ok_ = false;
            return;
        }
        ZipEntry e{.name = std::move(name), .madeBy = (3u << 8) | 20u, .needs = 10, .flags = 0x0800, .method = 0, .time = time_, .date = date_,
                   .crc = crc32(0, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()), .packed = static_cast<std::uint32_t>(bytes.size()),
                   .size = static_cast<std::uint32_t>(bytes.size()), .external = (0100000u | mode) << 16};
        write(e, [&](SDL_IOStream* out) { return SDL_WriteIO(out, bytes.data(), bytes.size()) == bytes.size(); });
    }
    void folder(std::string name) { // (ends with a slash)
        const ZipEntry e{.name = std::move(name), .madeBy = (3u << 8) | 20u, .needs = 10, .flags = 0x0800, .time = time_, .date = date_, .external = ((040755u) << 16) | 0x10u};
        write(e, [](SDL_IOStream*) { return true; });
    }
    // Its list and its end: whether it all went well.
    bool finish() {
        if (!out_) return false;
        const std::uint64_t listAt = at_;
        if (ok_ && SDL_WriteIO(out_, list_.data(), list_.size()) != list_.size()) ok_ = false;
        std::string end;
        put32(end, 0x06054b50u);
        put16(end, 0), put16(end, 0), put16(end, count_), put16(end, count_);
        put32(end, static_cast<std::uint32_t>(list_.size())), put32(end, static_cast<std::uint32_t>(listAt));
        put16(end, 0);
        if (ok_ && SDL_WriteIO(out_, end.data(), end.size()) != end.size()) ok_ = false;
        if (!SDL_CloseIO(std::exchange(out_, nullptr))) ok_ = false;
        return ok_ && listAt <= 0xFFFFFFFFu;
    }
    ~ZipWriter() {
        if (out_) SDL_CloseIO(out_);
    }
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

private:
    template <class Data> void write(const ZipEntry& e, Data&& data) {
        if (!ok_ || count_ == 0xFFFFu || at_ > 0xFFFFFFFFu) {
            ok_ = false;
            return;
        }
        std::string header;
        put32(header, 0x04034b50u);
        put16(header, e.needs), put16(header, e.flags), put16(header, e.method), put16(header, e.time), put16(header, e.date);
        put32(header, e.crc), put32(header, e.packed), put32(header, e.size);
        put16(header, static_cast<std::uint16_t>(e.name.size())), put16(header, 0);
        header += e.name;
        put32(list_, 0x02014b50u);
        put16(list_, e.madeBy), put16(list_, e.needs), put16(list_, e.flags), put16(list_, e.method), put16(list_, e.time), put16(list_, e.date);
        put32(list_, e.crc), put32(list_, e.packed), put32(list_, e.size);
        put16(list_, static_cast<std::uint16_t>(e.name.size())), put16(list_, 0), put16(list_, 0), put16(list_, 0), put16(list_, e.internal);
        put32(list_, e.external), put32(list_, static_cast<std::uint32_t>(at_));
        list_ += e.name;
        ++count_;
        if (SDL_WriteIO(out_, header.data(), header.size()) != header.size() || !data(out_)) ok_ = false;
        at_ += header.size() + e.packed;
    }
    SDL_IOStream* out_ = nullptr;
    bool ok_ = false;
    std::string list_;
    std::uint16_t count_ = 0;
    std::uint64_t at_ = 0;
    std::uint16_t time_ = 0, date_ = (46u << 9) | (1u << 5) | 1u; // (written as of 2026-01-01: the same zip every time)
};

// A program run till it ends (its name first, found as the system finds programs): whether it went well.
bool run(const std::vector<std::string>& words) {
    std::vector<const char*> argv;
    for (const std::string& w : words) argv.push_back(w.c_str());
    argv.push_back(nullptr);
    SDL_Process* process = SDL_CreateProcess(argv.data(), false);
    if (!process) return false;
    int code = -1;
    const bool waited = SDL_WaitProcess(process, true, &code);
    SDL_DestroyProcess(process);
    return waited && code == 0;
}

// A folder and everything in it, gone (a file: it alone).
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

std::string escapedXml(std::string_view text) {
    std::string out;
    for (const char c : text) out += c == '&' ? std::string("&amp;") : c == '<' ? std::string("&lt;") : c == '>' ? std::string("&gt;") : std::string(1, c);
    return out;
}

// A Mac app's Info.plist with one of its settings (a string) as `value`, added when it hasn't one.
std::string withPlistString(std::string plist, std::string_view key, std::string_view value) {
    const std::string k = std::format("<key>{}</key>", key);
    if (const auto at = plist.find(k); at != std::string::npos) {
        const auto open = plist.find("<string>", at), close = plist.find("</string>", open);
        if (open != std::string::npos && close != std::string::npos) plist.replace(open + 8, close - open - 8, escapedXml(value));
    } else if (const auto end = plist.rfind("</dict>"); end != std::string::npos) {
        plist.insert(end, std::format("\t{}\n\t<string>{}</string>\n", k, escapedXml(value)));
    }
    return plist;
}

// The game's icon as a Mac app's (.icns: the picture at 128, 256, 512 and 1024 pixels, each a PNG), or none.
std::optional<std::string> icnsOf(const std::string& image, const std::string& work) {
    const SurfacePtr picture{IMG_Load(image.c_str())};
    if (!picture) return std::nullopt;
    std::string out;
    for (const auto& [type, side] : {std::pair{"ic07", 128}, std::pair{"ic08", 256}, std::pair{"ic09", 512}, std::pair{"ic10", 1024}}) {
        const SurfacePtr scaled{SDL_ScaleSurface(picture.get(), side, side, SDL_SCALEMODE_LINEAR)};
        const std::string png = work + "icon.png";
        if (!scaled || !IMG_SavePNG(scaled.get(), png.c_str())) return std::nullopt;
        const auto bytes = readFile(png);
        if (!bytes) return std::nullopt;
        const auto n = static_cast<std::uint32_t>(bytes->size() + 8);
        out += type;
        for (const int shift : {24, 16, 8, 0}) out += static_cast<char>((n >> shift) & 0xFFu); // (big-endian, as icns is)
        out += *bytes;
    }
    const auto n = static_cast<std::uint32_t>(out.size() + 8);
    std::string head = "icns";
    for (const int shift : {24, 16, 8, 0}) head += static_cast<char>((n >> shift) & 0xFFu);
    return head + out;
}

} // namespace

// Where the other systems' players come with this one: in the app (a Mac's Resources/players), else in a folder next to the
// player (next to the AppImage).
std::string Developer::playersFolder() const {
    const char* at = SDL_GetBasePath();
    std::string here = at ? at : "";
#if defined(SDL_PLATFORM_LINUX)
    if (const char* image = SDL_getenv("APPIMAGE")) {
        const std::string path = image;
        here = path.substr(0, path.find_last_of('/') + 1);
    }
#endif
    return here + "players/";
}

// The player the tools run in, as the game would have it for this system: the app (on a Mac), VNext.exe, or the AppImage; none
// for one that isn't (a player built and run as it is).
std::string Developer::ownPlayer() const {
    const char* at = SDL_GetBasePath();
    const std::string here = at ? at : "";
#if defined(SDL_PLATFORM_MACOS)
    constexpr std::string_view bundleTail = "Contents/Resources/";
    if (here.ends_with(std::string(".app/") + std::string(bundleTail))) return here.substr(0, here.size() - bundleTail.size() - 1); // (".../VNext.app")
    return {};
#elif defined(SDL_PLATFORM_WINDOWS)
    return fileExists(here + "VNext.exe") ? here + "VNext.exe" : std::string{};
#else
    const char* image = SDL_getenv("APPIMAGE");
    return image ? std::string(image) : std::string{};
#endif
}

// Export the game: the game packed into the tools' own folder, the folder for the zips made (one exported before goes to
// the Trash first, as asked), then the zips, in a thread of their own.
void Developer::startExport() {
    DeveloperTools& d = tools;
    const std::string folder = engine.finishedFolder(), work = toolsFolder() + "export/";
    auto fail = [&](const std::string& why) { ask("Can't export the game"_tr(), why, {"OK"_tr()}, 0, SDL_MESSAGEBOX_ERROR, {}); };
    removeAll(work);
    if (!SDL_CreateDirectory(work.c_str())) return fail("Can't make '{}': {}."_tr(work, SDL_GetError()));
    if (SDL_GetPathInfo(folder.c_str(), nullptr) && !moveToTrash(folder)) return fail("Can't replace '{}': move it away first."_tr(folder));
    if (!SDL_CreateDirectory(folder.c_str())) return fail("Can't make '{}': {}."_tr(folder, SDL_GetError()));
    if (const auto packed = engine.packInto(work + std::string(kPackName)); !packed) return fail(packed.error());
    std::string name = fileNameOf(engine.gameSettings().title);
    if (name.empty()) name = "Game";
    const std::string icon = engine.gameSettings().icon;
    const ExportPlan plan{.name = name, .folder = folder, .work = work, .players = playersFolder(), .ownPlayer = ownPlayer(),
                          .icon = icon.empty() ? std::string{} : engine.gameRoot() + icon};
    d.exported = false;
    d.exporting = std::jthread([this, plan] {
        tools.exportSaid = exportGame(plan);
        tools.exported = true;
    });
}

// Export the game, in a thread of its own: a zip for each system it has a player for, into `plan.folder`; what's said of
// how it went.
std::string Developer::exportGame(const ExportPlan& plan) {
    std::vector<std::string> made, missed;
    const std::string pack = plan.work + std::string(kPackName), name = plan.name;
    for (const System& system : kSystems) {
        const std::string zip = std::format("{}/{}-{}.zip", plan.folder, name, system.said);
        const bool own = system.release == kHere && !plan.ownPlayer.empty();
        const std::string kept = plan.players + std::string(system.release) + ".zip"; // (its release's zip, as it comes with this one)
        if (!own && !fileExists(kept)) {
            missed.push_back("{}: this VNext has no player for it (VNext's releases have)"_tr(system.said));
            continue;
        }
        bool done = false;
        if (system.release == "macOS" && kHere == "macOS") { // (on a Mac: the app as the game's own, signed again)
            const std::string folder = plan.work + "mac/" + name + "/", app = folder + name + ".app";
            removeAll(plan.work + "mac");
            SDL_CreateDirectory(folder.c_str());
            if (own) done = run({"ditto", plan.ownPlayer, app});
            else done = run({"ditto", "-x", "-k", kept, plan.work + "mac/release"}) && run({"ditto", plan.work + "mac/release/VNext.app", app});
            removeAll(app + "/Contents/Resources/players"); // (the other systems' players: VNext's, not the game's)
            if (done) {
                const std::string plist = app + "/Contents/Info.plist";
                std::string text = readTextFile(plist).value_or("");
                std::string id;
                for (const char c : lower(name)) id += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
                text = withPlistString(withPlistString(withPlistString(text, "CFBundleName", name), "CFBundleDisplayName", name), "CFBundleIdentifier", "org.vnext.game." + id);
                done = !text.empty() && SDL_SaveFile(plist.c_str(), text.data(), text.size()) && SDL_CopyFile(pack.c_str(), (app + "/Contents/Resources/" + std::string(kPackName)).c_str());
                if (done && !plan.icon.empty())
                    if (const auto icns = icnsOf(plan.icon, plan.work)) done = SDL_SaveFile((app + "/Contents/Resources/VNext.icns").c_str(), icns->data(), icns->size());
                done = done && SDL_SaveFile((folder + "licences.txt").c_str(), licences().data(), licences().size()) && run({"codesign", "--force", "--sign", "-", app}) &&
                       run({"ditto", "-c", "-k", "--norsrc", "--noextattr", "--keepParent", plan.work + "mac/" + name, zip});
            }
            removeAll(plan.work + "mac");
        } else if (own) { // (this system's player: named as the game, next to it)
            const auto player = readFile(plan.ownPlayer), game = readFile(pack);
            ZipWriter z(zip);
            if (player && game) {
                z.folder(name + "/");
                z.add(name + "/" + name + std::string(system.as), *player, 0755);
                z.add(name + "/licences.txt", licences(), 0644);
                z.add(name + "/" + std::string(kPackName), *game, 0644);
            }
            done = player && game && z.finish();
        } else { // (VNext's release for it, its player named as the game: an app stays VNext.app, as only a Mac signs one again)
            const IOStreamPtr from = openFile(kept);
            const auto entries = from ? zipEntries(from.get()) : std::nullopt;
            const auto game = readFile(pack);
            ZipWriter z(zip);
            if (entries && game) {
                z.folder(name + "/");
                for (const ZipEntry& e : *entries) // (a player in one file renamed; an app's folder, VNext.app/, kept as it is)
                    if (e.name != kPackName)
                        z.copy(from.get(), e, name + "/" + (e.name == system.player && !e.name.ends_with('/') ? name + std::string(system.as) : e.name));
                z.add(name + "/" + std::string(kPackName), *game, 0644);
            }
            done = entries && game && z.finish();
        }
        if (done) made.push_back(std::format("{}-{}.zip", name, system.said));
        else {
            SDL_RemovePath(zip.c_str());
            missed.push_back("{}: can't make its zip"_tr(system.said));
        }
    }
    std::string said = made.empty() ? std::string("Exported nothing."_tr) : "Exported the game: {} in '{}'."_tr(joined(made, 0, ", "), plan.folder);
    for (const std::string& m : missed) said += "\n" + m + ".";
    return said;
}

} // namespace vn
