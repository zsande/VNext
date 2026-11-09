#pragma once

// The save slots on disk: what each holds, its picture, and the pages they're shown on.

#include "Files.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

struct SaveInfo {
    bool exists = false;
    SDL_Time time = 0;
    std::string summary; // speaker and line shown when the save was made
};

// Save slots: one file each, <folder>/<page>-<n>.vnsave, on the disk or in the game's pack. It holds the
// game state as lines of text (metadata last), then a "thumbnail png <bytes>" line and the thumbnail image.
// Pages are "auto" and "quick" (written round-robin) plus the numbered pages "1".."N".
class SaveStore {
public:
    // Where the saves go: a folder on the disk, or (`pack` given) the one in the game's pack that keeps
    // them, game.vnext/saves/.
    void setFolder(std::string folder, GameFiles* pack = nullptr);
    void configure(int pages, int slotsPerPage);

    [[nodiscard]] const std::vector<std::string>& pages() const { return pages_; }
    [[nodiscard]] int slotsPerPage() const { return slots_; }
    [[nodiscard]] static std::string slotName(std::string_view page, int index) { return std::format("{}-{}", page, index + 1); }
    [[nodiscard]] std::string path(std::string_view slot) const { return std::format("{}{}.vnsave", folder_, slot); }

    [[nodiscard]] SaveInfo info(std::string_view slot) const;
    [[nodiscard]] std::optional<std::string> read(std::string_view slot) const; // the game state
    [[nodiscard]] std::string thumbnail(std::string_view slot) const;           // PNG bytes (empty: none)
    bool write(std::string_view slot, std::string_view state, std::string_view summary, SDL_Surface* thumbnail);
    void remove(std::string_view slot);

    // First empty slot of a page, otherwise its oldest one (for autosaves and quick saves).
    [[nodiscard]] std::string rotatingSlot(std::string_view page) const;
    // Most recent save on one page, or on any page when `page` is empty.
    [[nodiscard]] std::optional<std::string> newest(std::string_view page = {}) const;

private:
    struct File {
        std::string text; // the state and metadata
        std::string png;  // the thumbnail
    };
    [[nodiscard]] std::optional<File> load(std::string_view slot) const;

    std::string folder_;
    GameFiles* pack_ = nullptr; // (none: the saves are files on the disk)
    std::vector<std::string> pages_{"auto", "quick"};
    int slots_ = 6;
};

// What the Save / Load screens are showing: mode, page, the slots on it (with thumbnails).
class SlotBrowser {
public:
    enum class Mode : std::uint8_t { Save, Load };
    struct Card {
        std::string slot;
        SaveInfo info;
        TexturePtr thumbnail;
    };

    void attach(SaveStore& store, SDL_Renderer* renderer) {
        store_ = &store;
        renderer_ = renderer;
    }
    void open(Mode mode);
    [[nodiscard]] Mode mode() const { return mode_; }
    [[nodiscard]] const std::vector<std::string>& pages() const { return pages_; }
    [[nodiscard]] std::size_t page() const { return page_; }
    void setPage(std::size_t page);
    void turn(int delta);
    [[nodiscard]] std::vector<Card>& cards() { return cards_; }
    void refresh(std::size_t index);

private:
    SaveStore* store_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    Mode mode_ = Mode::Load;
    std::vector<std::string> pages_;
    std::size_t page_ = 0;
    std::vector<Card> cards_;
    std::array<std::string, 2> last_; // last page per mode
};

} // namespace vn
