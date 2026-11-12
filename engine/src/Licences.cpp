// The licences of the software built into the player: engine/licences.txt, built into it by CMakeLists.
// About VNext in the developer tools shows them, Export the game and --pack write them next to game.vnext, and the
// player's zip on GitHub has the file itself.
#include "Engine.hpp"

namespace vn {

namespace {

#include "licences_txt.inc" // (kLicencesTxt: its bytes)

} // namespace

std::string_view licences() {
    return {reinterpret_cast<const char*>(kLicencesTxt), sizeof kLicencesTxt};
}

} // namespace vn
