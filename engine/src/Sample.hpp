#pragma once

// The samples built into VNext: a game of them (engine/sample: a cast of three, places, music, sounds, every menu with
// pictures of its own, a first scene), packed by the build as sample.vnext. New game makes a game folder with them,
// --sample writes one, and Export leaves them out of the games it makes. A Mac's app has them in its Resources; a Windows
// .exe or a Linux AppImage carries them at its very end (the pack, its size in 8 bytes, then "VNXSMPL1"); a player built
// and run as it is has them next to it. (Lanterns, the demo, is made of them too: engine/lanterns, packed on its own.)

#include <expected>
#include <optional>
#include <string>

namespace vn {

[[nodiscard]] bool hasSample();                           // whether this VNext carries the samples
[[nodiscard]] std::optional<std::string> builtInSample(); // its pack, or none
// The samples as a game folder: their files written into `folder` (with a slash at its end), which it makes (one there
// already: an error), and takes away again if they can't all be written.
[[nodiscard]] std::expected<void, std::string> writeSample(const std::string& folder);
// A player's bytes without the samples at their end (as they are when there's none, or when it's a signed .exe: taking it
// out would break the signature after it, so the game's player keeps it).
[[nodiscard]] std::string withoutSample(std::string player);

} // namespace vn
