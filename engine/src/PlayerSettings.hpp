#pragma once

// The player's settings (settings.txt), and the setting parts that change them.

#include "Basics.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

struct GameSettings; // (Script.hpp)

// Player preferences, stored as "name value" lines in settings.txt in the save folder.
// Defaults come from "setting default.<name> value" lines in the script.
struct Settings {
    // Display
    bool fullscreen = false;
    int windowWidth = 1280;
    int windowHeight = 720;
    int scaling = 0; // fit, integer, stretch, fill
    bool vsync = true;
    int frameLimit = 0; // frames per second, 0 = unlimited
    // Sound (percent)
    int masterVolume = 100;
    int musicVolume = 80;
    int soundVolume = 100;
    int voiceVolume = 100;
    bool madeUpVoices = true; // made-up voices (characters with made_up_voice=)
    bool typingSounds = true; // the little sounds as text types (characters with typing_sound=)
    int mouths = 0;           // when mouths move: 0 as the characters say (mouth_moves=), 1 only with a voice, 2 never
    // Text
    int textSpeed = 45;      // characters per second, 0 = instant
    int autoDelay = 500;     // milliseconds before auto-advance (plus a little per letter on lines without a voice); scripts say seconds
    bool timeLimits = true;  // a choice's time limit and a call's ringing time count (off: they wait for the player)
    int textboxOpacity = 85; // percent
    int textSize = 100;      // percent, for text in styles with text_size_follows=yes
    bool animations = true; // (off: no motion: animations show how they end, and looping ones stand still)
    // Skipping and going back
    bool skipUnread = false;       // false: skipping stops at text that hasn't been read yet
    bool skipAfterChoices = false; // false: making a choice ends skip mode
    bool rollback = true;          // going back to earlier lines (the rollback action, the history's Go back buttons)
    bool skipMovies = true;        // clicks and skipping go past movies (unless the story says skip=no)
    // Seeing and hearing
    bool readAloud = false;    // the system reads the lines, choices and the buttons pointed at aloud
    bool highContrast = false; // screens drawn in white on black, the button pointed at in yellow
    bool captions = false;     // what sounds and songs are (their caption lines) shows as they begin, and movies' subtitles
    bool vibration = true;     // controllers rumble: the phone buzzing with a message, ringing with a call
    // Language: a translation's id, "original" (as written), or empty: the computer's language when
    // there's a translation for it.
    std::string language;
    // Keys the player chose (with a screen's inputs for controls' keys: Settings' Keys), by control name ("none" for an emptied slot).
    std::map<std::string, std::vector<std::string>, std::less<>> keys;

    // Sets one value by its name (settings.txt, setting default.NAME); false for unknown names.
    bool set(std::string_view key, int value);
    [[nodiscard]] std::optional<int> get(std::string_view key) const; // (yes: 1, no: 0)
    [[nodiscard]] static bool has(std::string_view key);              // there's a setting called that
    static Settings load(const std::string& path, const Settings& defaults);
    bool save(const std::string& path) const;
};

// A player setting a slider can change (an input's setting=NAME): its place on the slider (0 to 1), and
// how it reads ({NAME} on screens: "45 cps", "80%").
struct SettingControl {
    std::function<float(const Settings&)> value; // where its slider is (0 to 1)
    std::function<void(Settings&, float)> setValue;
    std::function<double(const Settings&)> number; // what it is to screens' conditions and {texts} (a screen says its unit: "{master_volume}%")
    bool testSound = false; // play a short sound after changing it
};
const SettingControl* findSettingControl(std::string_view name);

// The player settings a game starts with ("setting default.*" lines), and what Reset goes back to: with a screen
// reader running, Read aloud on too (lines and menus are read to it from the first).
Settings defaultSettings(const GameSettings& game);

} // namespace vn
