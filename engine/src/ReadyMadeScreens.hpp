#pragma once

// For the developer tools' files only: the ready-made screens the Screens page starts from (ReadyMadeScreens.cpp).

#include "Forms.hpp"

#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vn {

namespace dev {

// The Screens page's screens to start from: the screen's name ("" an empty one to fill in), and what the
// list calls it.
struct ScreenStart {
    std::string_view name;
    ToolsText label;
};

inline const std::array kScreenStarts{
    ScreenStart{"", "Empty screen"},
    ScreenStart{"title", "Title"},
    ScreenStart{"pause", "Pause"},
    ScreenStart{"save", "Save"},
    ScreenStart{"load", "Load"},
    ScreenStart{"settings", "Settings"},
    ScreenStart{"history", "History"},
    ScreenStart{"confirm", "Confirm"},
    ScreenStart{"choice", "Choice"},
    ScreenStart{"saved", "Saved"},
    ScreenStart{"phone", "Phone"},
    ScreenStart{"credits", "Credits"},
    ScreenStart{"calendar", "Calendar"},
    ScreenStart{"relationships", "Relationships"},
    ScreenStart{"input", "Input"},
    ScreenStart{"extras", "Extras"},
    ScreenStart{"gallery", "Gallery"},
    ScreenStart{"music_room", "Music room"},
    ScreenStart{"endings", "Endings"},
    ScreenStart{"achievements", "Achievements"},
    ScreenStart{"tips", "Tips"},
    ScreenStart{"flowchart", "Flowchart"},
};

// The keys that open them.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kScreenControls{{
    {"pause", R"(control menu keys="escape mouse_right pad_start two_finger_tap" action="open pause")"},
    {"history", R"(control history keys="l pad_x swipe_up" action="open history")"},
    {"phone", R"(control phone keys="p pad_back" action="open phone" when=has_phone)"},
}};

// The ones that start as built-in screens (the choice menu, the Saved notice): a copy, in places already.
bool builtIn(std::string_view name);

std::string replaced(std::string text, std::string_view from, std::string_view to);

// The Settings screen, in two columns (Display, Text and Accessibility; Sound and Skip): a heading over each group and a
// row for each setting (a container with its name, and buttons or a slider that change it), without the ones for what
// the game doesn't use (`uses`: made-up voices without made_up_voice=, typing sounds without typing_sound=, captions
// without caption lines or a movie...; `phone`: vibration without a phone; `timed`: time limits without a timed choice
// or call); then Keys, a row for each of
// `controls` with its key and its controller button to change (click it, then press the new one), at the end of the
// shorter column. Longer than the room down to the buttons at the bottom, the groups scroll under the title.
std::vector<std::string> settingsScreen(const std::function<bool(std::string_view)>& uses, bool phone, bool timed, std::span<const std::string> controls);

// The buttons of the menus that open other screens (the title screen's, the pause menu's, the extras'), in their
// order: their words, what they do, their other settings, and the screen they open (none: always there). A menu
// made has the ones for the screens the game has; one added later gets its button in the menus that have one for it
// (Developer::addReadyMadeScreen). menuButton: one's line, as the ready-made menus have it.
struct MenuItem {
    std::string_view label, action, more, needs;
};
std::span<const MenuItem> menuItems(std::string_view menu);
std::string menuButton(const MenuItem& item);

// A screen to start from, for a 1280 x 720 game, called `as`; `has`: the screens there will be (the
// title's and the pause menu's buttons open those); `uses`: whether the scripts have these words; `timed`: whether
// a choice or a call has a time limit; `controls`: the ones there will be (Settings has a key to change for each).
std::vector<std::string> startScreen(std::string_view name, const std::string& as, const std::function<bool(std::string_view)>& has,
                                     const std::function<bool(std::string_view)>& uses, bool timed, std::span<const std::string> controls);

// A built-in screen (the textbox, the choice menu) as the game's own: its lines as the game shows them, with the
// look's colours by their names (its parts' styles: the look's, which needsOf() brings), for the
// game's screens file. `phone`, `pause`: whether the game has a phone screen and a pause screen (else the
// textbox's Phone button, and what's in it, or its Menu button stays out: it would open nothing).
std::vector<std::string> ownScreenLines(std::string_view name, float sy, bool phone, bool pause);

// The look and keys `lines` need that the game lacks: the styles its parts use (by style=, by
// *_style= and by their kind's name) and the colours those use.
std::vector<std::string> needsOf(const StoryEditor& editor, const std::vector<std::string>& lines, std::string_view screen);

} // namespace dev

} // namespace vn
