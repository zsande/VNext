// The licenses of the software built into the player: engine/licenses.txt, built into it by CMakeLists.
// About in the developer tools shows them, Pack the game writes them next to game.vnext, and the
// player's zip on GitHub has the file itself.
#include "Engine.hpp"

namespace vn {

namespace {
#include "licenses_txt.inc" // (kLicensesTxt: its bytes)
} // namespace

std::string_view licenses() {
    return {reinterpret_cast<const char*>(kLicensesTxt), sizeof kLicensesTxt};
}

} // namespace vn
