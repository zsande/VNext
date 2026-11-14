#pragma once

// The game's files (in its folder, or in its pack: game.vnext) and the player's own; packing a game; the Trash.

#include "Basics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

[[nodiscard]] std::string withSlash(std::string path); // a folder's path, with a slash at its end
[[nodiscard]] bool isDirectory(const std::string& path);

// The game's files, read from its folder or from its pack: game.vnext, the whole folder (but its
// saves) in one file, made in developer mode. A pack can't be opened and changed like a folder:
// its files are compressed and scrambled, and each one is checked when it's read.
//
// The engine reads the game through its GameFiles: once a pack is open there, the files in it have
// paths as if it were a folder ("<pack>/images/aiko.png"); everything else comes from the disk.

inline constexpr std::string_view kPackName = "game.vnext";

// The files the player reads, by their endings (in lower case): images, sounds, movies (MPEG-1;
// the others where the system plays them, see kSystemMovieTypes) and fonts.
inline constexpr std::array<std::string_view, 6> kImageTypes{".png", ".jpg", ".jpeg", ".bmp", ".qoi", ".gif"};
inline constexpr std::array<std::string_view, 4> kSoundTypes{".ogg", ".mp3", ".flac", ".wav"};
inline constexpr std::array<std::string_view, 5> kMovieTypes{".mpg", ".mpeg", ".mp4", ".mov", ".m4v"};
inline constexpr std::array<std::string_view, 3> kFontTypes{".ttf", ".otf", ".ttc"};

// The game's files: its pack's, once one is open (for the paths under its root), else the disk's. The
// engine has one (Engine::files()); what reads the game (images, sounds, fonts, movies, scripts) is
// given it.
class GameFiles {
public:
    GameFiles();
    ~GameFiles();
    GameFiles(const GameFiles&) = delete;
    GameFiles& operator=(const GameFiles&) = delete;
    GameFiles(GameFiles&&) = delete;
    GameFiles& operator=(GameFiles&&) = delete;
    // None open: just the disk (as what never reads a pack has it: the editor, a silent sample).
    [[nodiscard]] static const GameFiles& disk();
    // Opens `file` as the game's pack (the error says why it can't be used).
    [[nodiscard]] std::expected<void, std::string> openPack(const std::string& file);
    [[nodiscard]] bool exists(const std::string& path) const;
    // The whole file, or nullopt if it can't be read; the same as text (without a UTF-8 BOM).
    [[nodiscard]] std::optional<std::string> read(const std::string& path) const;
    [[nodiscard]] std::optional<std::string> readText(const std::string& path) const;
    // A stream that reads the file (closed when it goes: .release() it to an SDL call that closes it), or null.
    [[nodiscard]] IOStreamPtr open(const std::string& path) const;
    // The names of the files in `folder` that end with `extension` (in any case), in order.
    [[nodiscard]] std::vector<std::string> list(const std::string& folder, std::string_view extension) const;
    // Every file in the pack open (none open: none), its path in it as written ("images/aiko.png"), in the pack's order.
    [[nodiscard]] std::vector<std::string> packed() const;
    // The pack keeps its players' saves too: whether its file can be written to, and a file written into
    // it (added or replaced) or taken out of it, after its own files, which stay as they are. Only with a
    // pack open, for a path in it: whether it went well.
    [[nodiscard]] bool writable() const;
    bool write(const std::string& path, std::string_view data);
    bool remove(const std::string& path);

private:
    struct Pack;
    std::unique_ptr<Pack> pack_; // (none: no pack open)
};

// The same, for files on the disk (the player's own data, the game folder in developer mode).
[[nodiscard]] bool fileExists(const std::string& path);
[[nodiscard]] std::optional<std::string> readFile(const std::string& path);
[[nodiscard]] std::optional<std::string> readTextFile(const std::string& path);
[[nodiscard]] IOStreamPtr openFile(const std::string& path);
[[nodiscard]] std::vector<std::string> listFiles(const std::string& folder, std::string_view extension);
// A folder and everything in it, gone for good (a file: it alone): what VNext made itself and doesn't keep.
void removeAll(const std::string& path);
// What scripts call a file (its name without the ending): one word, so "19. YOMI.flac" is 19_YOMI
// (spaces, dots and the like are _; letters of any language stay).
[[nodiscard]] std::string scriptName(std::string_view stem);
// Moves a file (not in a pack) to the system's Trash or Recycle Bin, where it can be got back from:
// where it is now (empty where the system doesn't say: the Recycle Bin), or none when it can't be
// moved (it stays where it is).
std::optional<std::string> moveToTrash(const std::string& path);

// Packs the game folder `folder` (with a slash at the end) into the file `file`: everything in it
// but saves/ and hidden files.
struct PackReport {
    std::size_t files = 0;
    std::uint64_t bytes = 0;  // the files' own size
    std::uint64_t packed = 0; // the pack's
};
[[nodiscard]] std::expected<PackReport, std::string> writePack(const std::string& folder, const std::string& file);

// A file:// address for the folder `path` (spaces and other letters written as %xx).
std::string fileUrl(std::string_view path);

} // namespace vn
