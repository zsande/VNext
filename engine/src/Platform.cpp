// What the player does without the system's help (Apple.mm, Windows.cpp and Linux.cpp have the Mac's, Windows' and
// Linux's): no spelling, no voice, no screen reader, no movies but MPEG-1.
#include "Platform.hpp"

namespace vn {

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__linux__) // (Apple.mm, Windows.cpp and Linux.cpp have their own: here, none)
bool spelledRight(std::string_view, std::string_view) { return true; }
std::vector<std::string> spellings(std::string_view, std::string_view) { return {}; }
void learnSpelling(std::string_view) {}
void speak(std::string_view) {}
bool screenReaderOn() { return false; }
void announce(std::string_view) {}
std::unique_ptr<SystemMovie> SystemMovie::open(const std::string&, const std::string*) { return nullptr; }
#endif

} // namespace vn
