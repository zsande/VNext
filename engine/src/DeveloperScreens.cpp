// The Screens tab: the game's screens, each one's parts, a preview to click and drag them in, and a
// part's form.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

namespace vn {

namespace {

// The Screens tab's screens to start from: the screen's name ("" an empty one to fill in, "*" the
// usual menus together), what the list calls it, and what it is.
struct ScreenStart {
    std::string_view name, label, help;
};

constexpr std::array kScreenStarts{
    ScreenStart{"*", "The usual menus", "Title, pause, save, load, settings, history, confirm and phone, together"},
    ScreenStart{"", "Empty screen", "Nothing in it"},
    ScreenStart{"title", "Title", "New game, Continue, Quit and a button for each menu there is"},
    ScreenStart{"pause", "Pause", "Resume, Main menu, Quit and a button for each menu there is"},
    ScreenStart{"save", "Save", "The save slots, on pages"},
    ScreenStart{"load", "Load", "The save slots, to load one"},
    ScreenStart{"settings", "Settings", "What players can change"},
    ScreenStart{"history", "History", "The lines so far"},
    ScreenStart{"confirm", "Confirm", "A Yes or No question, for buttons that ask first"},
    ScreenStart{"choice", "Choice", "The story's choices, as the built-in menu has them"},
    ScreenStart{"phone", "Phone", "Who sent messages, and each conversation, at the side"},
    ScreenStart{"credits", "Credits", "Who made the game"},
};

constexpr std::string_view kUsualMenus[]{"title", "pause", "save", "load", "settings", "history", "confirm", "phone"};

// Their look: colours and styles, each added when the game has none by that name.
constexpr std::string_view kLook = R"vnext(color text #f0f0f5
color dim #9696a5
color accent #96aaff
color line #ffffff2d
color button #16192ae1
color hover #545caaf0
color card_hover #222644f5
color backdrop #080912f5
color panel #141626fa
color tip #7fc8ff
color shade #000000aa
style title size=64
style dim size=18 color=dim
style label size=18 valign=center
style heading color=accent
style button align=center valign=center bg=button border=line hover_bg=hover hover_border=accent selected_bg=hover selected_border=accent disabled_color=dim
style small_button parent=button size=18
style choice parent=button
style tab size=18 color=dim align=center valign=center bg=button border=line hover_border=accent selected_bg=hover selected_border=accent selected_color=text
style slot bg=button border=line hover_bg=card_hover hover_border=accent padding=4
style slot_title size=18 color=accent
style slot_text size=18
style slot_empty size=18 color=dim bg=#10111c
style setting size=18 color=dim align=center valign=center bg=button border=line hover_border=accent selected_bg=hover selected_color=text disabled_color=#5a5a66
style setting_value size=18 valign=center
style history size=24 text_size=yes
style history_name parent=history
style history_choice parent=history color=dim
style history_button color=dim bg=#ffffff10 border=line hover_bg=hover hover_border=accent hover_color=text
style record bg=button border=line padding=16 hover_border=accent selected_bg=hover selected_border=accent
style phone_text size=20 bg=#1d2136 border=line padding=12
style phone_mine parent=phone_text bg=#2b3468
style phone_name size=16 shadow=no
style phone_reply size=18 color=tip align=center valign=center bg=button border=line hover_bg=hover hover_border=accent hover_color=text disabled_color=dim
style phone_contact parent=record padding=12
style phone_preview size=16 color=dim shadow=no
style phone_unread size=15 color=#0d0f1a bg=tip shadow=no
style namebox size=28 bg=card_hover padding=20 color=speaker textbox_opacity=yes
style dialogue size=28 text_size=yes
style indicator color=accent)vnext";

// The keys that open them.
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kScreenControls{{
    {"pause", R"(control menu keys="escape pad_start two_finger_tap" action="open pause")"},
    {"history", R"(control history keys="l pad_x swipe_up" action="open history")"},
    {"phone", R"(control phone keys="p pad_back" action="open phone" if=has_phone)"},
}};

constexpr std::string_view kSlotsScreen = R"vnext(screen NAME
    image backdrop x=0 y=0 w=1280 h=720
    text "TITLE" x=60 y=32
    pages x=200 y=30 h=34 item_w=44 wide_w=76 gap=6 mode=MODE auto=Auto quick=Quick
    slots x=60 y=92 columns=3 item_w=360 item_h=262 gap_x=40 gap_y=16 mode=MODE title_style=slot_title text_style=slot_text empty_style=slot_empty confirm="Overwrite this save?" load_confirm="Load this save? Progress since your last save will be lost." delete_confirm="Delete this save?"
    button "Back" x=1080 y=654 w=140 h=40 style=small_button action=close
    key escape action=close)vnext";

constexpr std::string_view kHistoryScreen = R"vnext(screen history
    image backdrop x=0 y=0 w=1280 h=720
    text "History" x=60 y=32
    history x=60 y=92 w=1160 h=540 name_w=200 gap=18 close_on_bottom=yes name_style=history_name text_style=history choice_style=history_choice button_style=history_button
    button "Back" x=1080 y=654 w=140 h=40 style=small_button action=close
    key escape action=close
    key l action=close)vnext";

constexpr std::string_view kConfirmScreen = R"vnext(screen confirm
    image shade x=0 y=0 w=1280 h=720
    image panel x=310 y=210 w=660 h=300 border=accent
    text "{question}" x=330 y=226 w=620 h=212 align=center valign=center
    button "Yes" x=660 y=454 w=140 h=40 style=small_button action=yes
    button "No" x=810 y=454 w=140 h=40 style=small_button action=no
    key return action=yes
    key escape action=no)vnext";

// The phone: a panel at the side (the story goes on while it's open) listing who sent messages,
// and one person's conversation (CHAT), with the answers to send.
constexpr std::string_view kPhoneScreen = R"vnext(screen phone modal=no
    image panel x=880 y=80 w=380 h=400 border=accent
    text "Messages" x=900 y=96
    contacts x=900 y=140 w=340 h=270 item_h=62 gap=8 style=phone_contact name_style=phone_name text_style=phone_preview unread_style=phone_unread action="open CHAT"
    button "Close" x=1100 y=424 w=140 h=40 style=small_button action=close
    key escape action=close

screen CHAT modal=no
    image panel x=880 y=80 w=380 h=400 border=accent
    text "{contact}" x=900 y=96 w=340
    phone x=900 y=140 w=340 h=270 item_w=270 gap=12 name_style=phone_name text_style=phone_text mine_style=phone_mine reply_style=phone_reply
    button "Back" x=1100 y=424 w=140 h=40 style=small_button action=close
    key escape action=close)vnext";

constexpr std::string_view kCreditsScreen = R"vnext(screen credits
    image backdrop x=0 y=0 w=1280 h=720
    text "Credits" x=60 y=32
    text "Written by you" x=0 y=330 w=1280 align=center
    button "Back" x=1080 y=654 w=140 h=40 style=small_button action=close
    key escape action=close)vnext";

// The Settings screen, in two columns: a heading over each group and a row for each setting (a
// container with its name, and buttons or a slider that change it), without the ones for what the
// game doesn't use (`uses`: made-up voices without babble=, typing sounds without blip=...).
std::vector<std::string> settingsScreen(const std::function<bool(std::string_view)>& uses) {
    struct Row {
        std::string_view label, setting, choices; // "Label:value|..." for buttons, "slider", or "arrows" (bigger/smaller, next/previous)
        bool shown = true;
        std::string_view more = {};               // on the row
    };
    struct Group {
        std::string_view heading;
        std::vector<Row> rows;
    };
    constexpr std::string_view onOff = "On:yes|Off:no";
    const std::vector<Group> columns[]{
        {{"Display", {{"Window", "fullscreen", "Windowed:no|Fullscreen:yes"}, {"Window size", "window_size", "arrows"}}},
         {"Text",
          {{"Text speed", "text_speed", "slider"}, {"Auto-forward", "auto_delay", "slider"}, {"Textbox opacity", "textbox_opacity", "slider"},
           {"Text size", "text_size", "slider"}, {"Transitions", "transitions", onOff}, {"Read aloud", "read_aloud", onOff},
           {"High contrast", "high_contrast", onOff}, {"Language", "language", "arrows", true, " if=\"languages is more than 1\""}}}},
        {{"Audio",
          {{"Master volume", "master_volume", "slider"}, {"Music volume", "music_volume", "slider"}, {"Effects volume", "sound_volume", "slider"},
           {"Voice volume", "voice_volume", "slider", uses("voice") || uses("babble=")}, {"Made-up voices", "babble", onOff, uses("babble=")},
           {"Typing sounds", "typing_sounds", onOff, uses("blip=")}, {"Mouths move", "mouths", "Always:always|With a voice:voice|Never:never", uses("mouth")}}},
         {"Skip",
          {{"Skip", "skip_unread", "Read text:no|All text:yes"}, {"After choices", "skip_after_choices", "Stop skipping:no|Keep skipping:yes"},
           {"Skip movies", "skip_movies", onOff, uses("movie")}, {"Rollback", "rollback", onOff}}}},
    };
    std::vector<std::string> lines{"screen settings", "    image backdrop x=0 y=0 w=1280 h=720", "    text \"Settings\" x=60 y=32"};
    constexpr int labelW = 180, controlX = 190, controlW = 342, rowH = 32;
    for (int c = 0; c < 2; ++c) {
        const int x = c == 0 ? 60 : 680;
        int y = 92;
        for (const Group& g : columns[c]) {
            lines.push_back(std::format("    text \"{}\" x={} y={} style=heading", g.heading, x, y));
            y += 38;
            for (const Row& row : g.rows) {
                if (!row.shown) continue;
                lines.push_back(std::format("    container x={} y={} w={} h={}{}", x, y, controlX + controlW, rowH, row.more));
                lines.push_back(std::format("        text \"{}\" x=0 y=0 w={} h={} style=label", row.label, labelW, rowH));
                if (row.choices == "slider") {
                    lines.push_back(std::format("        slider setting={} x={} y=0 w={} h={}", row.setting, controlX, controlW - 92, rowH));
                    lines.push_back(std::format("        text \"{{{}}}\" x={} y=0 w=80 h={} style=setting_value", row.setting, controlX + controlW - 80, rowH));
                } else if (row.choices == "arrows") {
                    const bool size = row.setting == "window_size";
                    const std::string_view down = size ? "smaller" : "previous", up = size ? "bigger" : "next";
                    const std::string_view enabled = size ? " enabled=\"not fullscreen\"" : "";
                    lines.push_back(std::format("        button \"<\" x={} y=0 w=40 h={} style=setting action=\"setting {} {}\"{}", controlX, rowH, row.setting, down, enabled));
                    lines.push_back(std::format("        text \"{{{}}}\" x={} y=0 w={} h={} style=setting_value align=center", row.setting, controlX + 46, controlW - 92, rowH));
                    lines.push_back(std::format("        button \">\" x={} y=0 w=40 h={} style=setting action=\"setting {} {}\"{}", controlX + controlW - 40, rowH, row.setting, up, enabled));
                } else {
                    std::vector<std::pair<std::string_view, std::string_view>> options; // (label, value)
                    for (auto&& one : std::views::split(row.choices, '|')) {
                        const std::string_view o(one.begin(), one.end());
                        options.emplace_back(o.substr(0, o.find(':')), o.substr(o.find(':') + 1));
                    }
                    const int n = static_cast<int>(options.size()), w = (controlW - 6 * (n - 1)) / n;
                    for (int i = 0; i < n; ++i)
                        lines.push_back(std::format("        button \"{}\" x={} y=0 w={} h={} style=setting action=\"setting {} {}\"", options[static_cast<std::size_t>(i)].first,
                                                    controlX + i * (w + 6), w, rowH, row.setting, options[static_cast<std::size_t>(i)].second));
                }
                y += 36;
            }
            y += 8;
        }
    }
    lines.insert(lines.end(), {"    button \"Reset to defaults\" x=860 y=654 w=200 h=40 style=small_button action=\"reset settings\" "
                               "confirm=\"Reset all settings to their defaults?\"",
                               "    button \"Back\" x=1080 y=654 w=140 h=40 style=small_button action=close", "    key escape action=close"});
    return lines;
}

std::string replaced(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
    return text;
}

// A screen to start from, for a 1280 x 720 game, called `as`; `has`: the screens there will be (the
// title's and the pause menu's buttons open those); `uses`: whether the scripts have these words.
std::vector<std::string> startScreen(std::string_view name, const std::string& as, const std::function<bool(std::string_view)>& has,
                                     const std::function<bool(std::string_view)>& uses) {
    struct Item {
        std::string_view label, action, more, needs;
    };
    auto buttons = [&](float y, float step, float h, std::initializer_list<Item> items) {
        std::vector<std::string> out;
        for (const Item& item : items) {
            if (!item.needs.empty() && !has(item.needs)) continue;
            out.push_back(std::format("    button \"{}\" x=460 y={} w=360 h={} action={}{}", item.label, y, h, item.action, item.more));
            y += step;
        }
        return out;
    };
    std::vector<std::string> lines;
    if (name == "title") {
        lines = {"screen title", "    image backdrop x=0 y=0 w=1280 h=720", "    text \"{title}\" x=60 y=32"};
        std::ranges::copy(buttons(180, 62, 52, {{"New game", "start", "", ""},
                                                {"Continue", "continue", " enabled=has_save", ""},
                                                {"Load", "\"open load\"", " enabled=has_save", "load"},
                                                {"Settings", "\"open settings\"", "", "settings"},
                                                {"Credits", "\"open credits\"", "", "credits"},
                                                {"Quit", "quit", "", ""}}),
                          std::back_inserter(lines));
    } else if (name == "pause") {
        lines = {"screen pause", "    image shade x=0 y=0 w=1280 h=720", "    text \"Paused\" x=60 y=32"};
        std::ranges::copy(buttons(148, 62, 52, {{"Resume", "close", "", ""},
                                                {"Save", "\"open save\"", " enabled=can_save", "save"},
                                                {"Load", "\"open load\"", " enabled=has_save", "load"},
                                                {"History", "\"open history\"", "", "history"},
                                                {"Settings", "\"open settings\"", "", "settings"},
                                                {"Main menu", "\"open title\"", "", "title"},
                                                {"Quit", "quit", "", ""}}),
                          std::back_inserter(lines));
        lines.emplace_back("    key escape action=close");
    } else if (name == "save" || name == "load") {
        const bool save = name == "save";
        std::string text = replaced(std::string(kSlotsScreen), "NAME", name);
        text = replaced(replaced(text, "TITLE", save ? "Save" : "Load"), "MODE", name);
        lines = linesOf(text);
    } else if (name == "settings") {
        lines = settingsScreen(uses);
    } else if (name == "history") {
        lines = linesOf(kHistoryScreen);
    } else if (name == "confirm") {
        lines = linesOf(kConfirmScreen);
    } else if (name == "choice") { // (the built-in one)
        bool in = false;
        for (std::string& line : linesOf(defaultUi())) {
            if (indentOf(line) == 0) in = lower(line).starts_with("screen choice");
            if (in && !trim(line).empty()) lines.push_back(std::move(line));
        }
    } else if (name == "credits") {
        lines = linesOf(kCreditsScreen);
    } else if (name == "phone") {
        const std::string chat = uniqueName("chat", has); // (its conversation screen comes with it)
        lines = linesOf(replaced(std::string(kPhoneScreen), "CHAT", chat));
    } else { // (an empty one: nothing in it)
        lines = {"screen " + as};
    }
    const std::string_view settings = afterFirst(afterFirst(lines.front())); // (called `as`)
    lines.front() = "screen " + as + (settings.empty() ? "" : " " + std::string(settings));
    return lines;
}

// A built-in screen (say, choice) as the game's own: its lines as the game shows them, with the
// look's colours by their names and its parts' look left to the look's styles for their kinds
// (namebox, dialogue, indicator, choice: the ready-made screens' way; needsOf() brings those), for
// the game's screens file.
std::vector<std::string> ownScreenLines(std::string_view name, float sy) {
    static constexpr std::pair<std::string_view, std::string_view> kNamed[]{{"#141626fa", "panel"}, {"#222644f5", "card_hover"}, {"#ffffff2d", "line"}, {"#96aaff", "accent"}};
    static constexpr std::string_view kStyled[]{"namebox", "dialogue", "indicator", "choices"};
    std::vector<std::string> lines;
    bool in = false;
    for (std::string& line : linesOf(defaultUi())) {
        if (indentOf(line) == 0) in = lower(line).starts_with("screen " + lower(name));
        if (!in || trim(line).empty()) continue;
        std::string out = scaled(line, sy);
        for (const auto& [code, named] : kNamed)
            for (auto at = out.find(code); at != npos; at = out.find(code, at)) out.replace(at, code.size(), named);
        if (indentOf(out) > 0 && std::ranges::contains(kStyled, lower(firstWord(out)))) { // (what its style says: left to it)
            std::string kept = leading(out);
            for (const std::string& word : rawWords(trim(out))) {
                const auto eq = word.find('=');
                if (eq != npos && std::ranges::contains(kStyleSlots, std::string_view(word).substr(0, eq), &Slot::key)) continue;
                kept += (kept.empty() || kept.back() == ' ' ? "" : " ") + word;
            }
            out = kept;
        }
        lines.push_back(std::move(out));
    }
    return lines;
}

// The look and keys `lines` need that the game lacks: the styles its parts use (by style=, by
// *_style= and by their kind's name) and the colours those use.
std::vector<std::string> needsOf(const StoryEditor& editor, const std::vector<std::string>& lines, std::string_view screen) {
    std::map<std::string, std::string, std::less<>> colors, styles;
    for (const std::string& line : linesOf(kLook)) {
        auto& to = line.starts_with("color ") ? colors : styles;
        to.emplace(std::string(firstWord(afterFirst(line))), line);
    }
    std::vector<std::string> wanted, out, used = lines;
    for (const std::string& line : lines) {
        const std::string kind = lower(firstWord(line));
        for (const auto& [part, style] : {std::pair{"button", "button"}, {"slots", "slot"}, {"pages", "tab"}, {"history", "history"}, {"choices", "choice"},
                                          {"namebox", "namebox"}, {"dialogue", "dialogue"}, {"indicator", "indicator"}})
            if (kind == part) wanted.emplace_back(style);
        for (const std::string& word : rawWords(line))
            if (const auto eq = word.find('='); eq != std::string::npos && (word.substr(0, eq) == "style" || word.substr(0, eq).ends_with("_style")))
                wanted.push_back(unquote(word.substr(eq + 1)));
    }
    std::set<std::string> added;
    for (std::size_t i = 0; i < wanted.size(); ++i) { // (parents join the list)
        const auto it = styles.find(wanted[i]);
        if (it == styles.end() || added.contains(wanted[i]) || editor.defines("style", wanted[i])) continue;
        added.insert(wanted[i]);
        out.push_back(it->second);
        used.push_back(it->second);
        for (const std::string& word : rawWords(it->second))
            if (word.starts_with("parent=")) wanted.push_back(word.substr(7));
    }
    std::vector<std::string> colorLines;
    std::set<std::string> colorsAdded;
    for (const std::string& line : used) // (an image of a colour: image panel ...)
        if (lower(firstWord(trim(line))) == "image")
            if (const auto c = colors.find(unquote(firstWord(afterFirst(trim(line))))); c != colors.end() && !colorsAdded.contains(c->first) && !editor.defines("color", c->first)) {
                colorsAdded.insert(c->first);
                colorLines.push_back(c->second);
            }
    for (const std::string& line : used)
        for (const std::string& word : rawWords(line))
            if (const auto eq = word.find('='); eq != std::string::npos)
                if (const auto c = colors.find(unquote(word.substr(eq + 1)));
                    c != colors.end() && !colorsAdded.contains(c->first) && !editor.defines("color", c->first)) {
                    colorsAdded.insert(c->first);
                    colorLines.push_back(c->second);
                }
    out.insert(out.begin(), colorLines.begin(), colorLines.end());
    for (const auto& [name, control] : kScreenControls)
        if (name == screen && !editor.defines("control", firstWord(afterFirst(control)))) out.emplace_back(control);
    return out;
}

// Screens written in a game's pixels (x and y from the top left of the part they're in, w and h in
// pixels, a container without a size as big as what's in it), written in places and shares instead: x
// and y from -1 to 1 in the box a part is in, w and h (and item_w, item_h, gap, gap_x, gap_y, name_w,
// wide_w) shares of it. They show in the same places on a `width` x `height` screen.
std::vector<std::string> placesFromPixels(const std::vector<std::string>& lines, float width, float height) {
    struct Px {
        std::size_t line = 0;
        int indent = 0, parent = -1;
        std::string kind;
        std::optional<float> x, y, w, h;
        SDL_FRect at{}; // on the screen
    };
    auto number = [](const std::vector<std::string>& words, std::string_view key) -> std::optional<float> {
        for (const std::string& word : words)
            if (lower(word).starts_with(std::string(key) + "=")) return parseNumber(unquote(std::string_view(word).substr(key.size() + 1)));
        return std::nullopt;
    };
    const SDL_FRect screen{0.0f, 0.0f, width, height};
    std::vector<Px> parts;
    std::vector<int> in; // (the parts a line is in)
    bool inScreen = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (!partLine(line)) continue;
        const int indent = indentOf(line);
        if (indent == 0) {
            inScreen = lower(firstWord(line)) == "screen";
            in.clear();
            continue;
        }
        if (!inScreen) continue;
        while (!in.empty() && parts[static_cast<std::size_t>(in.back())].indent >= indent) in.pop_back();
        const auto words = rawWords(trim(line));
        Px p{.line = i, .indent = indent, .parent = in.empty() ? -1 : in.back(), .kind = lower(words.front()), .x = number(words, "x"),
             .y = number(words, "y"), .w = number(words, "w"), .h = number(words, "h")};
        const SDL_FRect box = p.parent >= 0 ? parts[static_cast<std::size_t>(p.parent)].at : screen;
        p.at = {box.x + p.x.value_or(0.0f), box.y + p.y.value_or(0.0f), p.w.value_or(0.0f), p.h.value_or(0.0f)};
        parts.push_back(std::move(p));
        in.push_back(static_cast<int>(parts.size()) - 1);
    }
    for (std::size_t i = parts.size(); i-- > 0;) { // (a container without a size: as far as what's in it goes)
        Px& c = parts[i];
        if (c.kind != "container" || (c.w && c.h)) continue;
        float right = c.at.x, bottom = c.at.y;
        for (std::size_t j = i + 1; j < parts.size() && parts[j].indent > c.indent; ++j) {
            right = std::max(right, parts[j].at.x + parts[j].at.w);
            bottom = std::max(bottom, parts[j].at.y + parts[j].at.h);
        }
        if (!c.w) c.at.w = std::max(1.0f, right - c.at.x);
        if (!c.h) c.at.h = std::max(1.0f, bottom - c.at.y);
    }
    std::vector<std::string> out = lines;
    for (const Px& p : parts) {
        const SDL_FRect box = p.parent >= 0 ? parts[static_cast<std::size_t>(p.parent)].at : screen;
        std::vector<std::string> words = rawWords(trim(lines[p.line]));
        auto put = [&](std::string_view key, std::optional<std::string> value) {
            const auto it = std::ranges::find_if(words, [&](const std::string& word) { return lower(word).starts_with(std::string(key) + "="); });
            if (!value) {
                if (it != words.end()) words.erase(it);
                return;
            }
            const std::string word = std::format("{}={}", key, *value);
            if (it != words.end()) *it = word;
            else words.push_back(word);
        };
        const bool container = p.kind == "container"; // (one without a size fills its box now: its size is written)
        if (p.x || p.at.x != box.x) put("x", unitText(placeFor(p.at.x - box.x, p.at.w, box.w)));
        if (p.y || p.at.y != box.y) put("y", unitText(-placeFor(p.at.y - box.y, p.at.h, box.h)));
        if (p.w || container) put("w", unitText(p.at.w / box.w));
        if (p.h || container) put("h", unitText(p.at.h / box.h));
        for (const auto& [key, across] : {std::pair{"item_w", true}, {"gap_x", true}, {"name_w", true}, {"wide_w", true}, {"item_h", false}, {"gap_y", false}})
            if (const auto v = number(words, key)) put(key, unitText(*v / (across ? box.w : box.h)));
        if (const auto gap = number(words, "gap")) { // (the same gap across and down: a share of each)
            if (!number(words, "gap_x")) put("gap_x", unitText(*gap / box.w));
            if (!number(words, "gap_y")) put("gap_y", unitText(*gap / box.h));
            put("gap", std::nullopt);
        }
        out[p.line] = leading(lines[p.line]) + joined(words);
    }
    return out;
}

// What a new part of each kind starts as (for a 1280 x 720 game): where and how big the ready-made
// screens have it (the textbox's parts where the built-in textbox has them, lists in the menus'
// list area, small parts in the menus' button column, panels where the question's is), with the
// look's colours by their names and nothing a part doesn't need.
struct PartStart {
    std::string_view kind, line;
};
constexpr PartStart kPartBasics[]{
    {"Container", "container x=310 y=210 w=660 h=300"},
    {"Text", "text \"Text\" x=460 y=345 w=360 align=center"},
    {"Button", "button \"Button\" x=460 y=334 w=360 h=52"},
    {"Image", "image \"PICTURE\" x=0 y=0 w=1280 h=720 fit=cover"},
    {"Movie", "movie \"MOVIE\" x=0 y=0 w=1280 h=720 fit=cover"},
    {"Key", "key escape action=close"},
};
constexpr PartStart kPartValues[]{
    {"Bar", "bar x=460 y=354 w=360 h=12 value=points max=10"},
    {"Slider", "slider x=460 y=344 w=360 h=32 setting=master_volume"},
    {"Input", "input x=460 y=334 w=360 h=52 var=name"},
    {"Control", "control advance x=460 y=344 w=360 h=32 waiting=\"Press a key...\""},
};
constexpr PartStart kPartTextbox[]{
    {"Namebox", "namebox x=64 y=470 h=46"},
    {"Dialogue", "dialogue x=72 y=528 w=1136"},
    {"Indicator", "indicator x=1195 y=648 w=18 h=12"},
    {"Face", "face x=64 y=452 w=190 h=232"},
    {"Choices", "choices x=280 y=40 w=720 h=420 item_h=54 gap=14"},
    {"Message", "message x=1220 y=32 w=560 align=right"},
};
constexpr PartStart kPartLists[]{
    {"Pages", "pages x=200 y=30 h=34 item_w=44 wide_w=76 gap=6 auto=Auto quick=Quick"},
    {"Slots", "slots x=60 y=92 columns=3 item_w=360 item_h=262 gap_x=40 gap_y=16 confirm=\"Overwrite this save?\" load_confirm=\"Load this save? Progress since your last save will be lost.\" delete_confirm=\"Delete this save?\""},
    {"History", "history x=60 y=92 w=1160 h=540 name_w=200 gap=18"},
    {"Page", "page x=60 y=92 w=1160 h=560 name_w=200 gap=18"},
    {"Gallery", "gallery x=60 y=92 w=1160 h=560 columns=3 item_h=208 gap=24"},
    {"Endings", "endings x=60 y=92 w=1160 h=560 item_h=80 gap=10 hidden=???"},
    {"Songs", "songs x=60 y=92 w=1160 h=560 item_h=60 gap=10"},
    {"Achievements", "achievements x=60 y=92 w=1160 h=560 item_h=80 gap=10 hidden=???"},
    {"Tips", "tips x=60 y=92 w=1160 h=560 item_h=60 gap=10"},
    {"Chart", "chart x=60 y=92 w=1160 h=560 confirm=\"Go back to this point? Progress since your last save will be lost.\""},
    {"Phone", "phone x=900 y=140 w=340 h=270 item_w=270 gap=12"},
    {"Contacts", "contacts x=900 y=140 w=340 h=270 item_h=62 gap=8"},
};
// New part's groups, as the Story tab's New line has its lines.
constexpr std::pair<std::string_view, std::span<const PartStart>> kParts[]{
    {"Basics", kPartBasics}, {"Values", kPartValues}, {"Textbox", kPartTextbox}, {"Lists", kPartLists}};

// Each kind of part's own settings (and whether it's a list, with the settings of one).
struct PartKind {
    std::string_view kind, keys;
    bool list = false;
};

constexpr PartKind kPartKinds[]{
    {"container", "fit scroll scrollbar scrollbar_size glide glide_speed glide_distance edges fade_size"}, {"image", "bottom fit flip repeat scroll scroll_time"}, {"button", "action confirm click_animation key enabled drag drop"}, {"key", "action confirm"},
    {"namebox", "bar bar_color"}, {"indicator", "bob bob_speed pause_image"}, {"choices", "anchor locked_style seen_style", true},
    {"history", "name_w close_on_bottom name_style text_style choice_style voice back button_style", true}, {"pages", "wide_w mode auto quick", true},
    {"slots", "mode delete confirm load_confirm delete_confirm title_style text_style empty_style", true},
    {"face", "fit flip"}, {"control", "slot waiting"}, {"gallery", "action title_style locked_style", true}, {"page", "name_w name_style text_style", true},
    {"input", "var max hint action confirm enabled"}, {"bar", "value max vertical track fill"},
    {"slider", "var setting min max step track fill knob enabled action"}, {"movie", "fit loop volume"},
    {"endings", "hidden title_style text_style locked_style", true}, {"songs", "title_style text_style locked_style", true},
    {"achievements", "hidden title_style text_style locked_style", true}, {"tips", "action title_style locked_style new_style", true},
    {"chart", "confirm line chapter_style ending_style stub_style label_style", true}, {"phone", "name_style text_style mine_style reply_style", true},
    {"contacts", "action name_style text_style unread_style", true},
};

// The heading a kind of part's own settings are under (a button's: by what they're for).
std::string_view partGroup(std::string_view kind, std::string_view key) {
    if (kind == "button") return key == "drag" || key == "drop" ? "Dragging and dropping" : "Clicking";
    if (kind == "container") return key == "fit" ? "Size" : key == "edges" || key == "fade_size" ? "Edges" : "Scrolling";
    if ((kind == "slider" || kind == "bar") && (key == "track" || key == "fill" || key == "knob")) return "Colours";
    static constexpr std::pair<std::string_view, std::string_view> groups[]{
        {"image", "Picture"}, {"face", "Face"}, {"movie", "Playing"}, {"key", "Action"},
        {"namebox", "Bar"}, {"indicator", "Bobbing"}, {"choices", "Choices"}, {"history", "Lines"}, {"pages", "Tabs"},
        {"slots", "Saves"}, {"control", "Key"}, {"gallery", "Images"}, {"page", "Lines"}, {"input", "Typing"},
        {"bar", "Value"}, {"slider", "Value"}, {"endings", "List"}, {"songs", "List"}, {"achievements", "List"},
        {"tips", "List"}, {"chart", "Flowchart"}, {"phone", "Messages"}, {"contacts", "List"}};
    const auto it = std::ranges::find(groups, kind, &std::pair<std::string_view, std::string_view>::first);
    return it != std::end(groups) ? it->second : std::string_view("Settings");
}

constexpr std::string_view kListKeys[]{"columns", "gap", "item_w", "item_h", "gap_x", "gap_y"}; // (a list's: its items' size and spacing)
constexpr std::string_view kGlideKeys[]{"glide", "glide_speed", "glide_distance"};        // (one that scrolls: how it glides on after a drag)
constexpr std::string_view kDraggable[]{"history", "phone", "gallery", "endings", "songs", "achievements", "tips", "contacts", "chart"};
// A part's settings: its own, a list's, its style and look (as a style's), then when it shows.
std::vector<PartOption> partOptions(std::string_view kind) {
    std::vector<PartOption> out;
    std::string_view last; // (each heading once: on the first of its settings)
    auto option = [&](std::string_view key) {
        const auto known = std::ranges::find(kPartOptions, key, &PartOption::key);
        if (known == std::end(kPartOptions)) return; // (one with no row: bottom=, which the picture's box shows as a gradient)
        PartOption o = *known;
        o.group = partGroup(kind, key);
        if (std::ranges::contains(kListKeys, key)) o.group = "Item size";
        if (std::ranges::contains(kGlideKeys, key) && kind != "container") o.group = "Scrolling";
        if (o.group == last) o.group = {};
        else last = o.group;
        if (key == "fit" && kind == "image") o.choices = "stretch contain cover original";
        if (key == "fit" && kind == "container") o.label = "Size", o.box = Box::ContainerFit, o.choices = "box parts";
        if (key == "scroll" && kind == "container") o.label = "Scrolls", o.box = Box::YesNo, o.choices = "no yes";
        if (key == "action" && (kind == "gallery" || kind == "tips" || kind == "contacts")) o.label = "Action on click";
        if (key == "action" && kind == "key") o.label = "Action";
        if (key == "confirm" && kind == "slots") o.label = "Question before saving over a save";
        if (key == "confirm" && kind == "chart") o.label = "Question before going back";
        if (key == "action" && kind == "input") o.label = "Action on Enter";
        if (key == "action" && kind == "slider") o.label = "Action on change";
        if (key == "max" && kind == "input") o.label = "Most letters", o.box = Box::None;
        if (key == "max" && kind == "bar") o.label = "Full at";
        out.push_back(o);
    };
    if (const auto k = std::ranges::find(kPartKinds, kind, &PartKind::kind); k != std::end(kPartKinds)) {
        for (const std::string_view key : wordsOf(k->keys)) option(key);
        if (k->list)
            for (const std::string_view key : kListKeys) option(key);
        if (std::ranges::contains(kDraggable, kind))
            for (const std::string_view key : kGlideKeys) option(key);
    }
    if (kind == "key") return out; // (nothing to see)
    // Its look: a style, and what it changes of it (its text, its box, and the rest).
    out.push_back({.key = "style", .label = "Style", .box = Box::Style, .hint = "Default", .group = "Look"});
    // (a button, a box, an image or a text: chosen while what "When" checks is true, it looks as Chosen says)
    static constexpr std::string_view kChoosable[]{"button", "container", "image", "text"};
    for (const Slot& s : std::span(kStyleSlots).subspan(2)) {
        std::string_view group = s.group;
        if (s.key == "selected_color" && std::ranges::contains(kChoosable, kind)) {
            option("selected");
            out.back().group = "Chosen";
            group = {};
        }
        out.push_back({.key = s.key, .label = s.label.empty() ? s.name : s.label, .box = s.box, .choices = s.choices, .labels = s.labels,
                       .hint = s.hint, .suggest = s.suggest, .group = group, .asStyle = true});
    }
    out.push_back({.key = "if", .label = "Shown when", .box = Box::Condition, .hint = "Always", .is = 'c', .group = "Showing"});
    out.push_back({.key = "animation", .label = "Animation", .box = Box::Animation, .hint = "None"});
    return out;
}

// A picture that depends on a condition: {if(CONDITION, 'YES', 'NO')} (written so in the script; in a form,
// the condition as its checks, as every condition's are, and a picture for each).
struct PictureBy {
    std::string condition, yes, no;
};

std::optional<PictureBy> pictureByOf(std::string_view v) {
    v = trim(v);
    if (!v.starts_with("{if(") || !v.ends_with(")}")) return std::nullopt;
    const Parts args = splitTop(v.substr(4, v.size() - 6), {","});
    if (args.parts.size() != 3) return std::nullopt;
    auto bare = [](std::string_view a) {
        a = trim(a);
        return std::string(a.size() >= 2 && (a.front() == '\'' || a.front() == '"') && a.back() == a.front() ? a.substr(1, a.size() - 2) : a);
    };
    return PictureBy{std::string(trim(args.parts[0])), bare(args.parts[1]), bare(args.parts[2])};
}

// A picture box's fields: one picture, or (after "depends on a variable" in its list) what it depends on,
// as a condition's checks (a yes or no variable, or "language is English"), then a picture for yes and one
// for no. Named `name` (the picture, or the one for yes), `name by` (and its checks': `name by is`...) and
// `name no`. Each box is a picture's (a picture, or a colour).
void addPictureFields(std::vector<StoryEditor::Field>& out, const std::string& name, const std::string& label, std::string_view hint, std::string_view value,
                      const std::vector<std::string>& variables) {
    const auto by = pictureByOf(value);
    if (!by) {
        out.push_back({.name = name, .value = std::string(value), .label = label, .box = Box::Picture, .hint = std::string(hint), .suggest = std::string(kPictureBySuggest)});
        return;
    }
    addCheckFields(out, name + " by", label + " depends on", "None", by->condition, Box::Condition, variables);
    out.push_back({.name = name, .value = by->yes, .label = label + " if yes", .box = Box::Picture, .hint = std::string(hint)});
    out.push_back({.name = name + " no", .value = by->no, .label = label + " if no", .box = Box::Picture, .hint = std::string(hint)});
}

// ...and back: the picture, or {if(CONDITION, 'YES', 'NO')} (back to one picture when what it depends on is taken away).
std::string pictureFrom(const StoryEditor& editor, const std::vector<StoryEditor::Field>& fields, const std::string& name) {
    std::string v(trim(fieldValue(fields, name)));
    if (hasField(fields, name + " by")) {
        const std::string by = conditionFrom(fields, name + " by", editor.variables());
        if (by.empty()) return v;
        return std::format("{{if({}, '{}', '{}')}}", by, v, trim(fieldValue(fields, name + " no")));
    }
    if (v != kPictureBy) return v;
    std::string guess; // (the first yes or no variable it could be)
    for (const StoryEditor::Definition& def : editor.definitions("variable"))
        if (const auto w = rawWords(afterFirst(editor.linesOf(def).front())); w.size() > 1 && parseBool(unquote(w[1]))) {
            guess = w[0];
            break;
        }
    if (guess.empty() && !editor.variables().empty()) guess = editor.variables().front();
    return guess.empty() ? std::string{} : std::format("{{if({}, '', '')}}", guess);
}

// A screen's or a part's settings as fields, and back.
std::vector<StoryEditor::Field> optionFields(const StoryEditor& editor, const StoryEditor::Part& p, std::span<const PartOption> options) {
    std::vector<StoryEditor::Field> out;
    for (const PartOption& o : options) {
        const std::string key(o.key), v = p.option(o.key);
        const std::size_t first = out.size();
        std::string hint(o.hint);
        if (key == "selected" && p.kind == "button") { // (pressed, it looks chosen; one that sets something to a value, while it's so)
            const auto w = rawWords(unquote(p.option("action")));
            hint = w.size() == 4 && lower(w[0]) == "set" && lower(w[2]) == "to" ? std::format("While {} is {}", w[1], unquote(w[3]))
                 : w.size() == 3 && lower(w[0]) == "setting"                    ? std::format("While {} is {}", w[1], w[2])
                                                                                : std::string("While pressed");
        }
        if (o.is == 'a') addActionFields(out, key, o.label, o.hint, v, editor.variables());
        else if (o.is == 'c') addCheckFields(out, key, o.label, hint, v, o.box, editor.variables());
        else if (o.box == Box::Picture) addPictureFields(out, key, std::string(o.label), o.hint, v, editor.variables());
        else out.push_back({.name = key, .value = o.choices.empty() ? (StoryEditor::kindOf(o.box).percent ? percentText(v) : v) : v.empty() ? std::string(wordsOf(o.choices).front()) : lower(v),
                            .label = std::string(o.label), .box = o.box, .choices = std::string(o.choices), .hint = std::string(o.hint), .labels = std::string(o.labels), .suggest = std::string(o.suggest)});
        if (first < out.size()) out[first].group = o.group;
        if (o.asStyle) // (a part's look: what an empty box gives it is its style's)
            for (std::size_t i = first; i < out.size(); ++i)
                for (std::string* words : {&out[i].labels, &out[i].hint})
                    if (const auto at = words->find("From its base style"); at != npos) words->replace(at, 19, "From its style");
    }
    return out;
}

StoryEditor::Part withOptions(const StoryEditor& editor, StoryEditor::Part p, const std::vector<StoryEditor::Field>& fields, std::span<const PartOption> options) {
    for (const PartOption& o : options) {
        const std::string key(o.key);
        if (o.is == 'a') {
            p.set(o.key, actionsFrom(fields, key, editor.variables()));
        } else if (o.is == 'c') {
            p.set(o.key, conditionFrom(fields, key, editor.variables()));
        } else if (o.box == Box::Picture && hasField(fields, key)) {
            p.set(o.key, pictureFrom(editor, fields, key));
        } else if (const auto f = std::ranges::find(fields, key, &StoryEditor::Field::name); f != fields.end()) {
            const std::string v(trim(f->value));
            p.set(o.key, !o.choices.empty() && v == wordsOf(o.choices).front() ? std::string{} : StoryEditor::kindOf(o.box).percent ? percentText(v) : v);
        }
    }
    return p;
}

// What the editor calls a part: its name (name=), else its kind and text.
// (with `palette`: an image part's colour by what it's called, never its code)
std::string partTitle(const StoryEditor::Part& p, const Palette* palette = nullptr) {
    if (std::string name = p.option("name"); !name.empty()) return name;
    if (p.text.empty()) return p.kind;
    if (palette && p.kind == "image" && parseColor(p.text, palette)) return std::format("{}  {}", p.kind, colorWords(p.text, palette));
    return std::format("{}  {}", p.kind, p.text);
}

// A name as typed, as name= keeps it (without double quotes: one would end it).
std::string partName(std::string_view typed) {
    std::string out(trim(typed));
    std::erase(out, '"');
    return out;
}

// What a part's text (or word) is, by its kind; empty for kinds without one.
std::string_view textLabel(std::string_view kind) {
    if (kind == "text") return "Text";
    if (kind == "button") return "Label";
    if (kind == "image") return "Image or colour";
    if (kind == "movie") return "Movie";
    if (kind == "key") return "Key";
    if (kind == "control") return "Control";
    return {};
}

} // namespace

// The Screens tab: the game's screens on the left (and the built-in ones), the one open in the
// middle as the game draws it (a click chooses a part, dragging moves it, its corner resizes it),
// and the chosen part's settings on the right (or the screen's own).
// The Screens tab as it is this frame: the screen open (and the screens to pick from), its parts and the
// one chosen, the screen as read, and where the lists, the screen and the form go. Its three columns are
// drawn by drawScreenLists, drawScreenPreview and drawScreenForm.
struct Developer::ScreensFrame {
    struct Entry {
        std::string name;
        std::optional<std::size_t> index; // in editor.screens() (none: built in)
    };
    DeveloperUi& ui;
    DeveloperTools& d;
    const std::vector<Entry>& entries;
    const Entry& entry;
    const Entry* chosen; // (the entry open: entry's place in entries)
    const std::optional<StoryEditor::Screen>& screen; // (none: built in)
    const std::vector<StoryEditor::Part>& parts;
    const StoryEditor::Part* selected;
    ScreenDef* def;
    const std::vector<std::optional<std::size_t>>& elementByPart;
    SDL_FRect left, middle, right;
    float gap;
    const std::string& tip; // (what the form said last frame: shown under the screen's buttons)
    // New part, under the screen and its help: its buttons (Goes in, with a part chosen; then the kinds
    // of parts, in groups) and where they go; what Goes in calls the part chosen.
    std::vector<DeveloperUi::ButtonGroup> newParts{};
    DeveloperUi::Grouped newPartPlaces{};
    std::string intoName{};
    float helpH = 0.0f; // (the room under the screen for the tab's help: three lines)
    // What the screen's column found, for the form: the parts chosen (in the script's order), and
    // whether the screen can be changed here (the game's own, read without mistakes).
    std::vector<std::size_t> chosenLines{};
    bool editable = false;
    // The screen as the middle column shows it: where (its pixels times `scale`), the mouse over it (in
    // its pixels), the chosen part's element with its handle (that resizes it), and the one pointed at.
    SDL_FRect shown{};
    float scale = 1.0f, mx = 0.0f, my = 0.0f;
    std::optional<std::size_t> selectedElement{}, hovered{};
    SDL_FRect handle{};

    // A part chosen (or none): the form shows it, and what was chosen with it isn't any more.
    void choose(std::optional<std::size_t> part) const {
        if (d.part != part) d.formScroll = 0.0f;
        d.part = part;
        d.moreParts.clear();
        d.focus.clear();
        d.armed.clear();
        d.asText = false;
        d.drag.reset();
    }
    [[nodiscard]] bool problemIn(std::size_t from, std::size_t to) const { return screen && d.problemWithin(screen->file, from, to); }
    [[nodiscard]] SDL_FRect onScreen(const SDL_FRect& b) const { return {shown.x + b.x * scale, shown.y + b.y * scale, b.w * scale, b.h * scale}; }
    [[nodiscard]] bool isChosen(std::size_t line) const { return d.part == line || std::ranges::contains(d.moreParts, line); }
    // The chosen parts that aren't in another chosen one (the parts in them go with them).
    [[nodiscard]] std::vector<std::size_t> outerLines() const {
        std::vector<std::size_t> out;
        for (const StoryEditor::Part& p : parts)
            if (isChosen(p.line) && std::ranges::none_of(parts, [&](const StoryEditor::Part& q) { return isChosen(q.line) && q.line < p.line && p.line < q.end; }))
                out.push_back(p.line);
        return out;
    }
    // The screen's element a part's line made, if it's there.
    [[nodiscard]] std::optional<std::size_t> elementOf(std::size_t line) const {
        if (!def || !screen) return std::nullopt;
        for (std::size_t i = 0; i < def->elements.size(); ++i)
            if (def->elements[i].line == static_cast<int>(line) + 1 && d.script->files[def->elements[i].file] == d.editor.path(screen->file)) return i;
        return std::nullopt;
    }
};

void Developer::drawScreens(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    using Part = StoryEditor::Part;
    using Screen = StoryEditor::Screen;

    // The screens: the game's, then the built-in textbox if it hasn't made it its own (the choice menu
    // is built in too, but only listed once it's the game's: + New screen has it).
    std::vector<ScreensFrame::Entry> entries;
    entries.reserve(editor.screens().size());
    for (std::size_t i = 0; i < editor.screens().size(); ++i) entries.push_back({editor.screens()[i].name, i});
    if (std::ranges::none_of(entries, [&](const ScreensFrame::Entry& e) { return lower(e.name) == "say"; })) entries.push_back({"say", std::nullopt});
    auto chosen = std::ranges::find_if(entries, [&](const ScreensFrame::Entry& e) { return sameName(e.name, d.screenName); });
    if (chosen == entries.end()) {
        chosen = entries.begin();
        d.screenName = chosen->name;
        d.part.reset();
    }
    const ScreensFrame::Entry entry = *chosen;
    const std::optional<Screen> screen = entry.index ? std::optional(editor.screens()[*entry.index]) : std::nullopt;
    const std::vector<Part> parts = screen ? editor.parts(*screen) : std::vector<Part>{};
    const Part* selected = nullptr;
    if (d.part) {
        const auto it = std::ranges::find(parts, *d.part, &Part::line);
        if (it != parts.end()) selected = &*it;
        else d.part.reset();
    }

    // The screen as read (with its parts' places), and a view of it; the textboxes in it show the
    // story's first line (or choice).
    ScreenDef* def = nullptr;
    if (d.script)
        if (const auto it = d.script->screens.find(lower(entry.name)); it != d.script->screens.end()) def = &it->second;
    if (!def) d.screenView.reset();
    else if (!d.screenView || &d.screenView->def() != def) {
        d.screenView = std::make_unique<ScreenView>(*def, engine, true);
        d.sampleMade = false;
    }
    if (def && !d.sampleMade) {
        d.sampleMade = true;
        makeSample(std::ranges::any_of(def->elements, [](const Element& el) { return el.kind == ElementKind::Choices; }));
    }
    std::vector<std::optional<std::size_t>> elementByPart(parts.size()); // (found once a frame, not for each row of the list)

    const auto [left, middle, right] = ui.columns(area);
    const float gap = layout::columnGap * k;
    const std::string tip = std::exchange(d.screenTip, {}); // (what the form said last frame: the form is drawn after the screen it's shown under)

    ScreensFrame f{.ui = ui, .d = d, .entries = entries, .entry = entry, .chosen = &*chosen, .screen = screen, .parts = parts, .selected = selected, .def = def,
                         .elementByPart = elementByPart, .left = left, .middle = middle, .right = right, .gap = gap, .tip = tip};
    for (std::size_t i = 0; i < parts.size(); ++i) elementByPart[i] = f.elementOf(parts[i].line);
    if (drawScreenLists(f) || drawScreenPreview(f) || drawNewPart(f)) return; // (a screen or part changed: the rest is drawn next frame)
    drawScreenForm(f);
}

bool Developer::drawScreenLists(const ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    using Part = StoryEditor::Part;
    using Screen = StoryEditor::Screen;
    const auto& entries = f.entries;
    const auto* chosen = f.chosen;
    const auto& screen = f.screen;
    const auto& parts = f.parts;
    const auto& elementByPart = f.elementByPart;
    const SDL_FRect& left = f.left;
    // --- The screens and the parts of the one open.
    const float half = std::round(left.h * 0.42f);
    const float headingH = layout::headingH * k, listButtonH = layout::buttonH * k, listGap = layout::gap * k;
    ui.heading("Screens", left.x, left.y);
    const SDL_FRect screensBox{left.x, left.y + headingH, left.w, half - headingH - listButtonH - 2 * listGap};
    // A click opens a screen; the game's own can be renamed with a double click (where it's opened
    // follows) and deleted with their x.
    using ListKind = DeveloperUi::ListEvent::Kind;
    auto own = [&](std::size_t i) { return entries[i].index.has_value(); };
    const auto event = ui.editList("screens", screensBox, d.screensScroll,
                                   {.count = entries.size(),
                                    .label = [&](std::size_t i) { return own(i) ? entries[i].name : entries[i].name + " (built in)"; },
                                    .chosen = [&](std::size_t i) { return &entries[i] == &*chosen && !d.picking; },
                                    .wrong =
                                        [&](std::size_t i) {
                                            if (!own(i)) return false;
                                            const Screen& s = editor.screens()[*entries[i].index];
                                            return d.problemWithin(s.file, s.line, s.end);
                                        },
                                    .faint = [&](std::size_t i) { return !own(i); },
                                    .renames = own,
                                    .deletes = own,
                                    .moves = own,
                                    .drops =
                                        [&](std::size_t item, std::size_t slot) { // among the game's screens in its file
                                            const std::size_t file = editor.screens()[*entries[item].index].file;
                                            auto in = [&](std::size_t j) { return j < entries.size() && own(j) && editor.screens()[*entries[j].index].file == file; };
                                            return in(slot) || (slot > 0 && in(slot - 1));
                                        }});
    switch (event.kind) {
    case ListKind::Chosen:
        if (lower(d.screenName) != lower(entries[event.item].name)) d.partsScroll = 0.0f;
        d.screenName = entries[event.item].name;
        d.picking = false;
        f.choose({});
        return true;
    case ListKind::Renamed:
        if (trim(event.name).empty()) return true; // (emptied: it keeps its name)
        if (editor.renameScreen(*entries[event.item].index, event.name, std::format("screen-name:{}:{}", event.item, d.focusCount))) {
            if (lower(d.screenName) == lower(entries[event.item].name)) d.screenName = event.name;
            if (d.status.starts_with("A screen's name")) d.status.clear();
        } else {
            d.failed = true;
            d.status = "A screen's name is one word (letters, numbers, _ and -) that no other screen has.";
        }
        return true;
    case ListKind::Deleted:
        editor.removeScreen(*entries[event.item].index);
        if (lower(d.screenName) == lower(entries[event.item].name)) d.screenName.clear();
        f.choose({});
        return true;
    case ListKind::Moved:
        if (editor.moveScreen(*entries[event.item].index, event.to)) f.choose({});
        return true;
    case ListKind::None:
        break;
    }
    if (ui.button({left.x, screensBox.y + screensBox.h + listGap, left.w, listButtonH}, "+ New screen", d.picking, true, layout::buttonText)) {
        d.picking = !d.picking;
        d.formScroll = 0.0f;
        f.choose({});
    }
    // Its parts, by their names (else by kind and text): a click chooses one, a double click renames it,
    // its x deletes it, and dragging one changes which are drawn over which, and (dragged right or left)
    // puts it in the part above it, or out of the one it's in. + New part adds one (in the part chosen, or
    // the screen).
    ui.heading("Parts", left.x, left.y + half);
    const SDL_FRect partsBox{left.x, left.y + half + headingH, left.w, left.h - half - headingH}; // (New part is under the screen)
    const float inStep = layout::listIndent * k; // (each part it's in, as the Story's lines are)
    const auto partEvent = ui.editList("parts", partsBox, d.partsScroll,
                                       {.count = parts.size(),
                                        .label = [&](std::size_t i) { return partTitle(parts[i]); },
                                        .name = [&](std::size_t i) { return parts[i].option("name"); },
                                        .chosen = [&](std::size_t i) { return d.part == parts[i].line || std::ranges::contains(d.moreParts, parts[i].line); },
                                        .wrong = [&](std::size_t i) { return d.problemAt(screen->file, parts[i].line) != nullptr; },
                                        .faint =
                                            [&](std::size_t i) {
                                                const auto el = elementByPart[i];
                                                return !el || !d.screenView || !d.screenView->drawn(*el);
                                            },
                                        .renames = [](std::size_t) { return true; },
                                        .deletes = [](std::size_t) { return true; },
                                        .moves = [](std::size_t) { return true; },
                                        .drops = {},
                                        .place = // (not into itself; as far in as the part above it and one more, as far out as the one under it)
                                            [&](std::size_t item, std::size_t slot, float dx) -> std::optional<int> {
                                                std::size_t after = item + 1; // (past the parts in it)
                                                while (after < parts.size() && parts[after].line < parts[item].end) ++after;
                                                if (slot > item && slot < after) return std::nullopt;
                                                const bool here = slot == item || slot == after; // (the parts around it, but not its own)
                                                const std::size_t up = here ? item : slot, down = here ? after : slot;
                                                const Part* above = up > 0 ? &parts[up - 1] : nullptr;
                                                const Part* below = down < parts.size() ? &parts[down] : nullptr;
                                                const int most = above ? above->depth + 1 : 0, least = std::min(below ? below->depth : 0, most);
                                                const int want = std::clamp(parts[item].depth + static_cast<int>(std::lround(dx / (inStep * 2))), least, most);
                                                if (here && want == parts[item].depth) return std::nullopt;
                                                return want;
                                            },
                                        .indentStep = inStep,
                                        .indent = [&](std::size_t i) { return static_cast<float>(parts[i].depth) * inStep; },
                                        .paint = // (a named one: its kind after its name, in grey; an image part's colour: a square and its name, as its box shows it)
                                            [&](std::size_t i, const SDL_FRect& at) {
                                                const auto el = elementByPart[i];
                                                const bool faint = !el || !d.screenView || !d.screenView->drawn(*el);
                                                const bool named = !parts[i].option("name").empty();
                                                const auto color = !named && parts[i].kind == "image" ? parseColor(parts[i].text, &engine.palette()) : std::nullopt;
                                                const std::string title = color ? parts[i].kind : partTitle(parts[i]);
                                                float x = at.x + 10 * k;
                                                const float mid = at.y + at.h / 2;
                                                const SDL_Color shade = faint ? theme::dim : theme::text;
                                                ui.textAround(title, x, mid, layout::listText, shade);
                                                x += ui.width(title, layout::listText) + 8 * k;
                                                if (color) {
                                                    const auto grad = parseGradient(parts[i].text, &engine.palette());
                                                    ui.textAround(colorWords(parts[i].text, &engine.palette()), ui.swatch(x, mid, *color, false, grad ? std::optional(grad->second) : std::nullopt), mid, layout::listText, shade);
                                                }
                                                else if (named) ui.textAround(parts[i].kind, x, mid, layout::smallText, theme::dim);
                                            }});
    switch (partEvent.kind) {
    case ListKind::Chosen:
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) && d.part && *d.part != parts[partEvent.item].line) { // (one more chosen, or one less)
            if (const auto it = std::ranges::find(d.moreParts, parts[partEvent.item].line); it != d.moreParts.end()) d.moreParts.erase(it);
            else d.moreParts.push_back(parts[partEvent.item].line);
            d.listDrag.reset();
            break;
        }
        f.choose(parts[partEvent.item].line);
        break;
    case ListKind::Deleted:
        editor.removePart(*screen, parts[partEvent.item]);
        f.choose({});
        return true;
    case ListKind::Moved:
        if (const auto to = editor.movePart(*screen, parts[partEvent.item], partEvent.to, partEvent.depth)) f.choose(*to);
        return true;
    case ListKind::Renamed: { // (as typed: one step for undo while it's typed into)
        Part named = parts[partEvent.item];
        named.set("name", partName(partEvent.name));
        editor.setPart(*screen, named, std::format("part-name:{}:{}", named.line, d.focusCount));
        break;
    }
    case ListKind::None:
        break;
    }
    return false;
}

bool Developer::drawScreenPreview(ScreensFrame& f) {
    DeveloperTools& d = tools;
    const float k = f.ui.k;
    using Part = StoryEditor::Part;
    // --- The screen, with its heading over it (Try it, its sound), the tab's help under it, and New part
    // under that (drawNewPart).
    const SDL_FRect& middle = f.middle;
    if (d.addIntoOf != d.part) { // (another part chosen: New part goes in it if it's a container, else on the screen)
        d.addIntoOf = d.part;
        const auto chosen = d.part ? std::ranges::find(f.parts, *d.part, &Part::line) : f.parts.end(); // (chosen just now: not f.selected yet)
        d.addInto = chosen != f.parts.end() && chosen->kind == "container";
    }
    if (f.selected) {
        f.intoName = f.ui.fitted(partTitle(*f.selected, &engine.palette()), layout::smallButtonText, middle.w / 2);
        f.newParts.push_back({.name = "Goes in", .labels = {"The screen", f.intoName}});
    }
    for (const auto& [name, starts] : kParts) {
        DeveloperUi::ButtonGroup& g = f.newParts.emplace_back(DeveloperUi::ButtonGroup{.name = name, .labels = {}});
        for (const PartStart& start : starts) g.labels.push_back(start.kind);
    }
    f.newPartPlaces = f.ui.grouped(f.newParts, middle.w);
    f.helpH = 3 * f.ui.measure("Ag", layout::noteText).y + 2 * layout::gap * k;
    const float headH = layout::previewHeadH * k, newH = layout::headingH * k + f.newPartPlaces.h;
    f.scale = std::max(0.01f, std::min(middle.w / engine.canvas().w, (middle.h - headH - f.helpH - newH) / engine.canvas().h));
    f.shown = {middle.x + (middle.w - engine.canvas().w * f.scale) / 2, middle.y + headH, engine.canvas().w * f.scale, engine.canvas().h * f.scale};
    f.editable = f.screen && f.def && d.screenView;
    f.mx = (d.mouseX - f.shown.x) / f.scale;
    f.my = (d.mouseY - f.shown.y) / f.scale;
    d.shownScreen = f.shown;
    // Try it (runTryIt): the screen gets the mouse and keys as in the game. Esc stops.
    const bool canTry = f.def && d.screenView && d.sample;
    if (d.trying && !canTry) stopTrying();
    if (d.trying && d.tryClosing && !d.screenView->leaving()) stopTrying(); // (the screen tried closed itself: its hide_animation= is over)
    if (d.trying) runTryIt(f);
    f.selectedElement = f.selected ? f.elementOf(f.selected->line) : std::nullopt;
    f.chosenLines.clear(); // (in the script's order)
    for (const Part& p : f.parts)
        if (f.isChosen(p.line)) f.chosenLines.push_back(p.line);
    chooseOnScreen(f);
    if (dragOnScreen(f) || screenKeys(f)) return true;
    paintScreen(f, canTry);
    return false;
}

// Choosing parts on the screen, and starting to move or resize them. With a part chosen, the screen
// only moves or resizes it (even under others), so another part is chosen in the list, or after Esc
// lets it go; Shift+click (on the screen or in the list) chooses more, to move together.
void Developer::chooseOnScreen(ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    const Part* selected = f.selected;
    ScreenDef* def = f.def;
    const SDL_FRect& shown = f.shown;
    const float mx = f.mx, my = f.my;
    const bool editable = f.editable;
    const auto& selectedElement = f.selectedElement;
    std::vector<std::size_t>& chosenLines = f.chosenLines;
    auto number = [](const std::string& text, float fallback) { return parseNumber(text).value_or(fallback); };
    if (selected && d.focus.empty())
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            if (key.key == SDLK_ESCAPE) {
                f.choose({});
                return true;
            }
            if (key.key == SDLK_DELETE || key.key == SDLK_BACKSPACE) { // (the chosen parts go)
                editor.removeParts(*screen, chosenLines);
                f.choose({});
                return true;
            }
            return false;
        });
    const bool adding = SDL_GetModState() & SDL_KMOD_SHIFT;
    std::optional<std::size_t>& hovered = f.hovered;
    if (editable && !d.trying && ui.over(shown) && !d.drag && (!selected || adding))
        for (std::size_t i = def->elements.size(); i-- > 0;) {
            const SDL_FRect b = d.screenView->bounds(i);
            if (def->elements[i].kind != ElementKind::Key && d.screenView->drawn(i) && pointInRect(mx, my, b)) {
                hovered = i;
                break;
            }
        }
    f.handle = selectedElement ? [&] {
        const SDL_FRect b = f.onScreen(d.screenView->bounds(*selectedElement));
        return SDL_FRect{b.x + b.w - 5 * k, b.y + b.h - 5 * k, 10 * k, 10 * k};
    }() : SDL_FRect{};
    const SDL_FRect& handle = f.handle;
    auto inside = [&](std::size_t element) {
        const SDL_FRect b = d.screenView->bounds(element);
        return pointInRect(mx, my, b);
    };
    auto within = [&](std::size_t element, std::size_t outer) { // (a part in that one, or in one in it...)
        for (int at = def->elements[element].parent; at >= 0; at = def->elements[static_cast<std::size_t>(at)].parent)
            if (static_cast<std::size_t>(at) == outer) return true;
        return false;
    };
    if (editable && d.clicked && ui.over(shown)) {
        d.clicked = false;
        d.focus.clear();
        if (adding && hovered) { // one more chosen (or one less)
            const std::size_t line = static_cast<std::size_t>(def->elements[*hovered].line - 1);
            if (!d.part) {
                f.choose(line);
            } else if (line == *d.part) {
                d.part = d.moreParts.empty() ? std::nullopt : std::optional(d.moreParts.front());
                if (!d.moreParts.empty()) d.moreParts.erase(d.moreParts.begin());
            } else if (const auto it = std::ranges::find(d.moreParts, line); it != d.moreParts.end()) {
                d.moreParts.erase(it);
            } else {
                d.moreParts.push_back(line);
            }
            d.lastList = "parts";
        } else {
            const bool resize = selectedElement && ui.over(handle);
            const std::optional<std::size_t> element = [&]() -> std::optional<std::size_t> { // (what the press takes hold of)
                if (!selectedElement) return hovered;
                if (resize || inside(*selectedElement)) return selectedElement;
                for (const std::size_t line : chosenLines) // (or another chosen one: they all move)
                    if (const auto el = f.elementOf(line); el && inside(*el)) return selectedElement;
                return std::nullopt;
            }();
            if (element) {
                const Element& el = def->elements[*element];
                const Part p = editor.part(*screen, static_cast<std::size_t>(el.line - 1));
                const SDL_FRect b = d.screenView->bounds(*element);
                if (!selected) {
                    f.choose(p.line);
                    chosenLines = {p.line};
                }
                d.lastList = "parts";
                DeveloperTools::Drag drag{.part = p.line, .element = *element, .resize = resize, .from = d.screenView->rectOf(*element), .x = mx, .y = my,
                                          .px = number(p.option("x"), 0.0f), .py = number(p.option("y"), 0.0f), .pw = b.w, .ph = b.h, .others = {}};
                // The other chosen parts move with it (but not one in a part that's moving: it goes with that
                // one), and so do the parts in them.
                std::vector<std::size_t> moving{*element};
                if (!resize)
                    for (const std::size_t line : chosenLines)
                        if (const auto other = f.elementOf(line); other && line != p.line) moving.push_back(*other);
                auto carried = [&](std::size_t i) { return std::ranges::any_of(moving, [&](std::size_t m) { return within(i, m); }); };
                drag.own = !carried(*element);
                for (const std::size_t other : moving)
                    if (other != *element && !carried(other)) {
                        const Part q = editor.part(*screen, static_cast<std::size_t>(def->elements[other].line - 1));
                        drag.others.push_back({.part = q.line, .element = other, .from = d.screenView->rectOf(other),
                                               .px = number(q.option("x"), 0.0f), .py = number(q.option("y"), 0.0f)});
                    }
                if (!resize)
                    for (std::size_t i = 0; i < def->elements.size(); ++i)
                        if (std::ranges::contains(moving, i) || carried(i)) drag.moved.emplace_back(i, d.screenView->rectOf(i));
                d.drag = drag;
            }
        }
    }
}

// The parts being moved or resized, as the mouse goes (in steps of 5 pixels, lined up with the other
// parts' edges and middles, and the screen's); written to the script when it's let go (true).
bool Developer::dragOnScreen(ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    ScreenDef* def = f.def;
    const float mx = f.mx, my = f.my;
    const bool editable = f.editable;
    auto snap = [](float v) { return std::round(v / 5.0f) * 5.0f; };
    d.guideX.reset();
    d.guideY.reset();
    if (d.drag && editable && d.drag->element < def->elements.size()) {
        const DeveloperTools::Drag g = *d.drag;
        float dx = snap(mx - g.x), dy = snap(my - g.y); // (in steps of 5 pixels, or lined up with something)
        // The edges and middles to line up with: the screen's, and those of the parts that aren't moving.
        std::vector<float> xs{0.0f, engine.canvas().w / 2, engine.canvas().w}, ys{0.0f, engine.canvas().h / 2, engine.canvas().h};
        for (std::size_t i = 0; i < def->elements.size(); ++i) {
            if (i == g.element || def->elements[i].kind == ElementKind::Key || !d.screenView->drawn(i) ||
                std::ranges::any_of(g.moved, [&](const auto& m) { return m.first == i; }))
                continue;
            const SDL_FRect b = d.screenView->bounds(i);
            xs.insert(xs.end(), {b.x, b.x + b.w / 2, b.x + b.w});
            ys.insert(ys.end(), {b.y, b.y + b.h / 2, b.y + b.h});
        }
        auto lineUp = [](std::initializer_list<float> edges, const std::vector<float>& targets, float& by, std::optional<float>& guide) {
            float best = 7.0f; // (closer than this lines up)
            for (const float e : edges)
                for (const float t : targets)
                    if (std::abs(t - e) < std::abs(best)) {
                        best = t - e;
                        guide = t;
                    }
            if (std::abs(best) < 7.0f) by += best;
        };
        const SDL_FRect from = g.from; // (only across the way it's being moved: a drag sideways stays at its height)
        if (g.resize) {
            if (dx != 0.0f) lineUp({from.x + g.pw + dx}, xs, dx, d.guideX);
            if (dy != 0.0f) lineUp({from.y + g.ph + dy}, ys, dy, d.guideY);
        } else {
            if (dx != 0.0f) lineUp({from.x + dx, from.x + dx + from.w / 2, from.x + dx + from.w}, xs, dx, d.guideX);
            if (dy != 0.0f) lineUp({from.y + dy, from.y + dy + from.h / 2, from.y + dy + from.h}, ys, dy, d.guideY);
        }
        float w = std::max(5.0f, g.pw + dx), h = std::max(5.0f, g.ph + dy);
        if (g.resize && (SDL_GetModState() & SDL_KMOD_SHIFT) && g.pw > 0.0f && g.ph > 0.0f) { // (Shift: its shape kept, by the way it's pulled most)
            const float across = w / g.pw, down = h / g.ph, grow = std::abs(across - 1.0f) >= std::abs(down - 1.0f) ? across : down;
            w = std::max(5.0f, g.pw * grow);
            h = std::max(5.0f, g.ph * grow);
        }
        Element& el = def->elements[g.element];
        // (as places and shares in the box each is in: the parts in them go with them)
        auto placed = [&](std::size_t i, SDL_FPoint edge, float pw, float ph) {
            Element& m = def->elements[i];
            const SDL_FRect box = d.screenView->boxOf(i);
            m.placeX = placeFor(edge.x - box.x, pw, box.w);
            m.placeY = -placeFor(edge.y - box.y, ph, box.h);
        };
        if (g.resize) {
            const SDL_FRect box = d.screenView->boxOf(g.element);
            el.shareW = w / box.w;
            el.shareH = h / box.h;
            placed(g.element, {g.from.x, g.from.y}, w, h);
        } else {
            if (g.own) placed(g.element, {g.from.x + dx, g.from.y + dy}, g.from.w, g.from.h);
            for (const auto& o : g.others) placed(o.element, {o.from.x + dx, o.from.y + dy}, o.from.w, o.from.h);
        }
        if (!d.held) { // let go: written to the script (one step for undo)
            d.drag.reset();
            d.guideX.reset();
            d.guideY.reset();
            if (dx != 0.0f || dy != 0.0f) {
                const std::string key = std::format("drag:{}", ++d.focusCount);
                auto write = [&](std::size_t part, std::size_t element, bool size) {
                    Part q = editor.part(*screen, part);
                    const Element& m = def->elements[element];
                    q.set("x", unitText(m.placeX.value_or(-1.0f)));
                    q.set("y", unitText(m.placeY.value_or(1.0f)));
                    if (size) {
                        q.set("w", unitText(m.shareW.value_or(0.0f)));
                        q.set("h", unitText(m.shareH.value_or(0.0f)));
                    }
                    editor.setPart(*screen, q, key);
                };
                if (g.resize || g.own) write(g.part, g.element, g.resize);
                for (const auto& o : g.others) write(o.part, o.element, false);
                return true;
            }
        }
    }
    return false;
}

// The keys for what's chosen on the screen: copy, cut, paste and duplicate, and the arrows that nudge
// (true: the script changed).
bool Developer::screenKeys(ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Part = StoryEditor::Part;
    const auto& entry = f.entry;
    const auto& screen = f.screen;
    const auto& parts = f.parts;
    const Part* selected = f.selected;
    const bool editable = f.editable;
    const std::vector<std::size_t>& chosenLines = f.chosenLines;
    // Copy, cut, paste and duplicate the chosen parts, or the screen (in the list clicked last).
    if (d.focus.empty() && !d.drag)
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key) {
            const char what = clipboardKey(*key);
            if (!what) continue;
            d.keys.erase(key);
            const bool onPart = selected && d.lastList != "screens";
            std::string text;
            if (onPart)
                for (const std::size_t line : f.outerLines())
                    for (const std::string& l : editor.partLines(*screen, editor.part(*screen, line))) text += (text.empty() ? "" : "\n") + l;
            else if (screen)
                text = editor.textOf(screen->file, screen->line, screen->end);
            if (what == 'c' || what == 'x') {
                if (text.empty()) return true;
                SDL_SetClipboardText(text.c_str());
                d.clipText = text;
                if (what == 'x' && onPart) {
                    editor.removeParts(*screen, chosenLines);
                    f.choose({});
                } else if (what == 'x' && screen) {
                    editor.removeScreen(*entry.index);
                    d.screenName.clear();
                    f.choose({});
                }
                return true;
            }
            std::vector<std::string> lines;
            if (what == 'd')
                lines = linesOf(text);
            else
                lines = clipboardLines();
            if (lines.empty()) return true;
            if (lower(firstWord(lines.front())) == "screen") {
                d.screenName = editor.screens()[editor.pasteScreen(lines)].name;
                f.choose({});
            } else if (screen) {
                std::erase_if(lines, [](const std::string& l) { return blank(l); });
                const auto last = chosenLines.empty() ? parts.end() : std::ranges::find(parts, chosenLines.back(), &Part::line);
                const Part* after = last != parts.end() ? &*last : nullptr;
                const std::size_t first = editor.addParts(*screen, after, lines);
                f.choose(first);
                for (std::size_t i = 1; i < lines.size(); ++i) // (the pasted ones stay chosen together: the outer ones)
                    if (indentOf(lines[i]) <= indentOf(lines.front())) d.moreParts.push_back(first + i);
            }
            return true;
        }
    // Arrow keys nudge the chosen parts (Shift: 10 pixels).
    if (editable && selected && d.focus.empty() && !d.drag)
        for (const SDL_KeyboardEvent& key : d.keys) {
            const float step = key.mod & SDL_KMOD_SHIFT ? 10.0f : 1.0f;
            const float dx = key.key == SDLK_LEFT ? -step : key.key == SDLK_RIGHT ? step : 0.0f;
            const float dy = key.key == SDLK_UP ? -step : key.key == SDLK_DOWN ? step : 0.0f;
            if (dx == 0.0f && dy == 0.0f) continue;
            std::erase_if(d.keys, [&](const SDL_KeyboardEvent& other) { return &other == &key; });
            const std::string stepKey = std::format("nudge:{}", ++d.focusCount);
            for (const std::size_t line : f.outerLines()) { // (the parts in them go with them)
                const auto el = f.elementOf(line);
                if (!el || !d.screenView) continue;
                const SDL_FRect at = d.screenView->rectOf(*el), box = d.screenView->boxOf(*el);
                Part p = editor.part(*screen, line);
                p.set("x", unitText(placeFor(at.x + dx - box.x, at.w, box.w)));
                p.set("y", unitText(-placeFor(at.y + dy - box.y, at.h, box.h)));
                editor.setPart(*screen, p, stepKey);
            }
            return true;
        }

    return false;
}

// The screen drawn (what's chosen on it outlined, the lines things line up with), and Try it and the
// sound's button under it, with what the form has to say.
void Developer::paintScreen(const ScreensFrame& f, bool canTry) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    const auto& entry = f.entry;
    const auto& screen = f.screen;
    const ScreenDef* def = f.def;
    const std::string& tip = f.tip;
    const SDL_FRect& shown = f.shown;
    const float scale = f.scale;
    const auto& hovered = f.hovered;
    const auto& selectedElement = f.selectedElement;
    const SDL_FRect& handle = f.handle;
    draw::rect(r, shown, kBox);
    if (def && d.screenView &&
        paintCanvas([&] {
            std::optional<SwappedIn<std::unique_ptr<Game>>> sampleGame;
            if (d.sample) sampleGame.emplace(engine.game_, d.sample);
            std::optional<SwappedIn<Settings>> ownSettings;
            if (d.trying) ownSettings.emplace(engine.settings_, d.trySettings);
            if (lower(entry.name) != "title") engine.game_->draw(engine.fonts_.get("default", 18)); // (the story under it)
            d.screenView->draw(engine.renderer());
            for (const std::string& name : d.tryOverlays) // (Try it: the screens its buttons opened over it, and the ones closing)
                if (ScreenView* v = previewView(name)) v->draw(engine.renderer());
            for (const std::string& name : d.tryLeaving)
                if (ScreenView* v = previewView(name)) v->draw(engine.renderer());
        })) {
        SDL_RenderTexture(r, d.canvas.get(), nullptr, &shown);
        const DeveloperUi::Clip clipped(ui, shown);
        if (hovered) draw::outline(r, f.onScreen(d.screenView->bounds(*hovered)), kEdge);
        for (const std::size_t line : d.moreParts)
            if (const auto el = f.elementOf(line)) draw::outline(r, f.onScreen(d.screenView->bounds(*el)), kAccent);
        if (d.trying) draw::outline(r, shown, kGood);
        else if (selectedElement) {
            draw::outline(r, f.onScreen(d.screenView->bounds(*selectedElement)), d.screenView->drawn(*selectedElement) ? kAccent : kWarn);
            draw::rect(r, handle, kAccent);
        }
        if (d.guideX) draw::rect(r, {shown.x + *d.guideX * scale, shown.y, 1 * k, shown.h}, kOptionArrow); // (what it lines up with)
        if (d.guideY) draw::rect(r, {shown.x, shown.y + *d.guideY * scale, shown.w, 1 * k}, kOptionArrow);
    } else {
        d.screenTip = f.problemIn(screen ? screen->line : 0, screen ? screen->end : 0) ? "The screen shows once its mistakes are fixed." : "The screen shows once the scripts' mistakes are fixed.";
    }
    draw::outline(r, shown, kEdge);
    PreviewHead head(ui, PreviewHead::over(shown, k)); // (as the Story tab's preview has it)
    if (head.button("Try it", d.trying, canTry)) {
        if (d.trying) {
            stopTrying();
        } else {
            f.choose({});
            d.trying = true;
            d.tryOverlays.clear();
            d.tryLeaving.clear();
            d.tryClosing = false;
            d.tryPending.clear();
            if (d.screenView) d.screenView->reset(); // (it opens: its show_animation= plays, as in the game)
            d.trySettings = engine.settings_;
            d.sampleMade = false; // (the story under it is made again: heard, with its sound on)
            d.failed = false;
            d.status = "Trying it: its buttons work as in the game, and what they'd do there is written to the log. Esc stops.";
        }
    }
    // Its sound (only while no game is going on behind the tools: it would cut into that one's).
    if (engine.waiting_ && head.button(d.previewSound ? "Audio on" : "Audio off", d.previewSound)) {
        d.previewSound = !d.previewSound;
        d.sampleMade = false; // (made again, heard or not)
        if (d.heard) d.heard->stopAll(0.2f);
        d.heard = nullptr;
    }
    head.title("Preview");
    // Every word of help the tab has to say goes here, under the screen: what the form would say of what's chosen.
    if (d.trying) d.screenTip = "Its buttons work as in the game, and what they'd do there goes to the log. Esc stops.";
    if (!tip.empty()) ui.text(tip, shown.x, shown.y + shown.h + layout::gap * k, layout::noteText, theme::dim, shown.w);
}

// A screen tried stops: the story under it as it was, the screens it opened gone, its music stopped.
void Developer::stopTrying() {
    DeveloperTools& d = tools;
    d.trying = false;
    d.sampleMade = false; // (the story under it as it was)
    d.tryScreen.clear();
    d.tryOverlays.clear();
    d.tryLeaving.clear();
    d.tryClosing = false;
    d.tryPending.clear();
    if (d.heard) d.heard->stopAll(0.2f); // (its music, if it was heard)
    d.heard = nullptr;
    if (d.screenView) d.screenView->reset();
    if (d.status.starts_with("Trying")) d.status.clear();
}

// Try it, this frame: the screen gets the mouse and keys as in the game (the mouse in its pixels), over
// the story's first line, with settings of its own; what its buttons would do there is said instead (a
// screen it opens is shown here, and variables it sets change). Esc stops.
void Developer::runTryIt(const ScreensFrame& f) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    const SDL_FRect& shown = f.shown;
    const float mx = f.mx, my = f.my;
    const auto& entry = f.entry;
    const ScreenDef* def = f.def;
    auto underTried = [&]() -> ScreenView* { return !d.tryOverlays.empty() ? previewView(d.tryOverlays.back()) : d.screenView.get(); }; // (on top)
    std::erase_if(d.tryLeaving, [&](const std::string& name) { // (the screens closed over it: drawn while their hide_animation= plays)
        const ScreenView* v = previewView(name);
        if (v && v->leaving()) return false;
        if (ScreenView* top = underTried(); top && ((v && v->endsCovered()) || top->hasLeft())) top->opened(); // (what was under comes back into view)
        return true;
    });
    if (!d.tryPending.empty()) // (opening once the screen under it has faded out)
        if (ScreenView* u = underTried(); !u || !u->leaving())
            if (ScreenView* v = previewView(d.tryPending)) {
                d.tryOverlays.push_back(std::exchange(d.tryPending, {}));
                v->reset();
                tryScreenHook(v->def(), true);
            }
    if (d.tryScreen != entry.name) { // its on_show=, as the game runs it when it opens (the one tried before it: its on_hide=)
        runScreenHook(d.tryScreen, false);
        runScreenHook(entry.name, true);
        d.tryScreen = entry.name;
    }
    auto send = [&](SDL_Event e) { // (the screens opened over it first, the top one before the others, as in the game)
        const SetFor asTried(engine.trying_, true);
        const SwappedIn ownSettings(engine.settings_, d.trySettings);
        const SwappedIn sampleGame(engine.game_, d.sample);
        bool taken = false;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) // (a button pressed is clicked when the mouse comes up: its screen takes it)
            for (const std::string& name : d.tryOverlays)
                if (ScreenView* v = previewView(name); v && v->pressing()) {
                    v->handleEvent(e);
                    taken = true;
                    break;
                }
        const std::vector<std::string> over = d.tryOverlays; // (a copy: a button may open or close one)
        for (const std::string& name : over | std::views::reverse) {
            if (taken) break;
            if (ScreenView* v = previewView(name); v && (v->handleEvent(e) || v->modal())) taken = true;
        }
        if (!taken && !d.tryClosing) d.screenView->handleEvent(e);
    };
    send(mouseEventAt(SDL_EVENT_MOUSE_MOTION, mx, my));
    if (d.clicked && ui.over(shown)) {
        d.clicked = false;
        d.focus.clear();
        send(mouseEventAt(SDL_EVENT_MOUSE_BUTTON_DOWN, mx, my, d.doubleClick));
        d.tryHeld = true;
    }
    if (d.tryHeld && !d.held) {
        d.tryHeld = false;
        send(mouseEventAt(SDL_EVENT_MOUSE_BUTTON_UP, mx, my));
    }
    if (const float turns = ui.wheelOver(shown); turns != 0.0f) send(wheelEventAt(turns, mx, my));
    if (d.focus.empty()) {
        if (!d.typed.empty()) {
            send(textEvent(d.typed.c_str()));
            d.typed.clear();
        }
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            if (key.key == SDLK_ESCAPE) return stopTrying(), true;
            if (key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) return false; // (undo...)
            send(keyEvent(key));
            return true;
        });
    }
    for (const std::string& name : d.tryOverlays) // (the screens opened over it: their buttons too)
        if (ScreenView* v = previewView(name))
            if (auto did = v->takePressed()) {
                runTrying(did->first, did->second);
            }
    if (auto did = d.screenView->takePressed()) { // (a button whose click animation is over)
        runTrying(did->first, did->second);
    }
    // (buttons chosen because of their selected=: what their actions set holds in Try it too, as in the game)
    for (const std::string& name : d.tryOverlays)
        if (ScreenView* v = previewView(name))
            if (const Commands kept = v->takeChosen(); !kept.empty()) runTrying(kept, {});
    if (const Commands kept = d.screenView->takeChosen(); !kept.empty()) runTrying(kept, {});
    // What its buttons (and its on_show=) did: the story's own commands done to the story under it (a
    // variable set, music, which is heard with its sound on), another screen opened here, and the rest said.
    std::vector<std::string> said;
    auto closeTop = [&] { // (the top one opened over it closes, with what it does on closing and its hide_animation=)
        const std::string name = d.tryOverlays.back();
        d.tryOverlays.pop_back();
        if (ScreenView* v = previewView(name)) {
            tryScreenHook(v->def(), false);
            if (v->def().hideAnimation) {
                v->leave();
                d.tryLeaving.push_back(name);
            }
        }
    };
    for (const Command& c : std::exchange(engine.tried_, {})) {
        const auto* u = std::get_if<cmd::Ui>(&c);
        const auto* jump = std::get_if<cmd::Jump>(&c);
        if (!u && !jump && d.sample) {
            d.sample->perform(c);
            continue;
        }
        if (u && u->verb == "setting") { // (its own settings change: the buttons show it)
            engine.changeSetting(d.trySettings, u->arg);
            continue;
        }
        if (u && u->verb == "open" && !u->arg.contains("://") && !u->arg.starts_with("www."))
            if (ScreenView* v = previewView(u->arg)) { // (opened over the one tried, as in the game: its on_show= runs, its show_animation= plays)
                if (!d.tryOverlays.empty() && d.tryOverlays.back() == u->arg) { // (open already, on top: it closes, as in the game)
                    closeTop();
                    d.failed = false;
                    d.status = std::format("Trying '{}': '{}' closed again, as in the game. Esc stops.", entry.name, u->arg);
                    continue;
                }
                if (u->arg == d.tryScreen || std::ranges::contains(d.tryOverlays, u->arg)) { // (open under others: back to it, the ones over it closing, and it opens again)
                    while (!d.tryOverlays.empty() && d.tryOverlays.back() != u->arg) closeTop();
                    tryScreenHook(v->def(), true);
                    d.failed = false;
                    d.status = std::format("Trying '{}': back to '{}', opened again, as in the game. Esc stops.", entry.name, u->arg);
                    continue;
                }
                std::erase(d.tryLeaving, u->arg);
                ScreenView* under = !d.tryOverlays.empty() ? previewView(d.tryOverlays.back()) : d.screenView.get();
                if (v->startsCovered() && under && under != v && under->endsCovered() && !under->hasLeft()) { // (the one under fades out first)
                    under->leave();
                    d.tryPending = u->arg;
                } else if (!std::ranges::contains(d.tryOverlays, u->arg)) {
                    d.tryOverlays.push_back(u->arg);
                    v->reset();
                    tryScreenHook(v->def(), true);
                }
                d.failed = false;
                d.status = std::format("Trying '{}': '{}' opened over it, as in the game. Esc stops.", entry.name, u->arg);
                continue;
            }
        if (u && (u->verb == "close" || u->verb == "answer")) { // (the top one opened closes, with its hide_animation=; the one tried: Try it stops after its own)
            if (!d.tryOverlays.empty()) {
                closeTop();
            } else if (def->hideAnimation && !d.tryClosing) {
                d.screenView->leave();
                d.tryClosing = true;
            } else {
                said.emplace_back("close the screen");
            }
            continue;
        }
        if (u) said.push_back(u->arg.empty() ? u->verb : u->verb + " " + u->arg);
        else if (jump) said.push_back((jump->call ? "call " : "jump to ") + jump->label);
        else said.emplace_back("go on with the story"); // (no story under it to do it to)
    }
    if (!said.empty()) {
        std::string all;
        for (const std::string& one : said) all += (all.empty() ? "" : ", then ") + one;
        d.failed = false;
        d.status = std::format("Trying it: in the game, that would {}.", all);
    }
}

void Developer::drawScreenForm(const ScreensFrame& f) {
    if (tools.picking) return drawNewScreenForm(f);
    if (!f.screen) return drawBuiltInForm(f);
    if (f.selected && !tools.moreParts.empty()) return drawLineUpForm(f);
    drawPartForm(f);
}

// + New screen: the screens to start from (the list scrolls when it's longer than its room).
void Developer::drawNewScreenForm(const ScreensFrame& f) {
    using Screen = StoryEditor::Screen;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    const float titleH = layout::titleH * k;
    ui.formTitle("New screen", right.x, y);
    const SDL_FRect list{right.x, y + titleH, right.w, right.y + right.h - y - titleH};
    ui.scrollForm(list, 60 * k);
    y = list.y - d.formScroll;
    const DeveloperUi::Clip clipped(ui, list, true);
    auto has = [&](std::string_view name) { return std::ranges::any_of(editor.screens(), [&](const Screen& s) { return lower(s.name) == name; }); };
    for (const ScreenStart& start : kScreenStarts) {
        const bool all = start.name == "*";
        if (all && std::ranges::all_of(kUsualMenus, has)) continue; // (they're all there)
        if (ui.button({right.x, y, right.w, layout::buttonH * k}, start.label, false, true, layout::buttonText)) {
            std::vector<std::string_view> names;
            if (all) std::ranges::copy_if(kUsualMenus, std::back_inserter(names), [&](std::string_view n) { return !has(n); });
            else names.push_back(start.name);
            auto will = [&](std::string_view n) { return has(n) || std::ranges::contains(names, n); };
            const float sy = engine.canvas().h / 720.0f;
            std::size_t made = 0;
            for (const std::string_view name : names) {
                // (one there already: the new one is another, pause_2...)
                const std::string base = name.empty() ? std::string("new_screen") : std::string(name);
                const std::string as = uniqueName(base, has);
                std::vector<std::string> lines = startScreen(name, as, will, [&](std::string_view w) { return !editor.find(w).empty(); });
                if (name != "choice") lines = placesFromPixels(lines, 1280.0f, 720.0f); // (written for 1280 x 720: places and shares fit any size)
                std::vector<std::string> needs = needsOf(editor, lines, name);
                for (std::string& l : lines) l = scaled(l, sy);
                for (std::string& l : needs) l = scaled(l, sy);
                made = editor.addScreen(needs, lines);
            }
            d.picking = false;
            d.screenName = all ? std::string("title") : editor.screens()[made].name;
            f.choose({});
            break;
        }
        if (ui.over({right.x, y, right.w, layout::buttonH * k}) && ui.over(list)) d.screenTip = std::string(start.help);
        y += layout::buttonH * k + layout::gap * k;
    }
    ui.formDrawn(list, y);
}

// New part, under the screen and its help, as the Story tab's New line is under its lines: Goes in (the
// screen, or the part chosen), then a button for each kind of part, in groups. A part goes in at the top
// left of the part it goes in.
bool Developer::drawNewPart(ScreensFrame& f) {
    using Part = StoryEditor::Part;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const auto& screen = f.screen;
    const Part* selected = f.selected;
    const float y = f.shown.y + f.shown.h + f.helpH; // (under the help's room)
    ui.heading(!screen ? "New part (once the screen is the game's own)" : "New part", f.middle.x, y, f.middle.w);
    const std::size_t goesIn = selected ? 2 : 0; // (its two buttons first)
    const std::vector<std::size_t> chosen{d.addInto ? std::size_t{1} : std::size_t{0}};
    const auto clicked = ui.groupedButtons(f.newParts, f.newPartPlaces, f.middle.x, y + layout::headingH * k, selected ? std::span(chosen) : std::span<const std::size_t>{},
                                           screen.has_value() && !d.trying);
    if (!clicked || !screen) return false;
    if (*clicked < goesIn) {
        d.addInto = *clicked == 1;
        return false;
    }
    const PartStart* start = nullptr;
    for (std::size_t i = *clicked - goesIn; const auto& [name, starts] : kParts) {
        if (i < starts.size()) {
            start = &starts[i];
            break;
        }
        i -= starts.size();
    }
    if (!start) return false;
    const Part* into = d.addInto ? selected : nullptr;
    std::string line(start->line);
    const auto pictures = editor.names(StoryEditor::Pick::Pictures), movies = editor.names(StoryEditor::Pick::Movies); // (the first, or none to pick)
    line = replaced(line, "PICTURE", pictures.empty() ? "" : pictures.front());
    line = replaced(line, "MOVIE", movies.empty() ? "" : movies.front());
    line = std::string(trim(scaled("    " + line, engine.canvas().h / 720.0f)));
    // (written for 1280 x 720: as places and shares, on the screen or at the top left of the part it goes in)
    std::vector<std::string> lines{"screen new", "    " + line};
    if (into)
        if (const auto el = f.elementOf(into->line); el && d.screenView) {
            const SDL_FRect b = d.screenView->bounds(*el);
            const float sx = 1280.0f / engine.canvas().w, sy = 720.0f / engine.canvas().h;
            std::string at;
            for (const std::string& word : rawWords(line))
                at += (at.empty() ? "" : " ") + (word.starts_with("x=") ? std::string("x=0") : word.starts_with("y=") ? std::string("y=0") : word);
            lines = {"screen new", std::format("    container x={} y={} w={} h={}", b.x * sx, b.y * sy, b.w * sx, b.h * sy), "        " + at};
        }
    line = std::string(trim(placesFromPixels(lines, 1280.0f, 720.0f).back()));
    std::vector<std::string> needs = needsOf(editor, {"screen new", "    " + line}, "new"); // (the colours and styles it uses that the game lacks)
    for (std::string& l : needs) l = scaled(l, engine.canvas().h / 720.0f);
    d.picking = false;
    f.choose(editor.addPart(*screen, into, line, needs));
    return true;
}

// A built-in screen (the textbox, the choice menu): what it is, and a button that makes it the game's own.
void Developer::drawBuiltInForm(const ScreensFrame& f) {
    const auto& entry = f.entry;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    const float titleH = layout::titleH * k;
    ui.formTitle(entry.name, right.x, y);
    y += titleH;
    d.screenTip = entry.name == "say" ? "The player's textbox: the speaker's name, the line and the arrow" : "The player's choice menu";
    if (ui.button({right.x, y, 220 * k, layout::bigButtonH * k}, "Make it the game's own")) { // (a copy in the game's screens, used instead)
        const float sy = engine.canvas().h / 720.0f;
        const std::vector<std::string> lines = ownScreenLines(entry.name, sy);
        std::vector<std::string> needs = needsOf(editor, lines, entry.name); // (the colours and styles it uses that the game lacks)
        for (std::string& l : needs) l = scaled(l, sy);
        if (!lines.empty()) editor.addScreen(needs, lines);
        f.choose({});
        return;
    }
}

// Several parts chosen: lined up by their edges or middles, or spread out evenly between the outer two
// (one step for undo).
void Developer::drawLineUpForm(const ScreensFrame& f) {
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    const auto& chosenLines = f.chosenLines;
    const bool editable = f.editable;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    ui.formTitle(std::format("{} parts", chosenLines.size()), right.x, y);
    y += layout::titleH * k;
    struct Item {
        std::size_t line = 0, element = 0;
        SDL_FRect b{};
    };
    std::vector<Item> items;
    if (editable)
        for (const std::size_t line : chosenLines)
            if (const auto el = f.elementOf(line); el && d.screenView->drawn(*el)) items.push_back({line, *el, d.screenView->bounds(*el)});
    static constexpr std::array<std::string_view, 6> kLineUp{"Left", "Centre", "Right", "Top", "Middle", "Bottom"};
    static constexpr std::array<std::string_view, 2> kSpread{"Across", "Down"};
    const float labelW = std::round(right.w * layout::labelPart), bh = layout::mediumButtonH * k;
    std::optional<std::string_view> clicked;
    for (const auto& [label, words, need] : {std::tuple{std::string_view("Line up"), std::span<const std::string_view>(kLineUp), std::size_t{2}},
                                             {std::string_view("Spread evenly"), std::span<const std::string_view>(kSpread), std::size_t{3}}}) {
        ui.textAround(label, right.x, y + bh / 2, layout::labelText, theme::dim, labelW - 8 * k);
        const std::vector<bool> with(words.size() == 6 ? std::vector<bool>{false, true, true, false, true, true} : std::vector<bool>{}); // (across together, down together)
        const auto places = ui.flow({words.begin(), words.end()}, right.w - labelW, bh, layout::barButtonGap * k, layout::smallButtonText, with);
        for (std::size_t i = 0; i < words.size(); ++i)
            if (ui.button({right.x + labelW + places[i].x, y + places[i].y, places[i].w, places[i].h}, words[i], false, items.size() >= need, layout::smallButtonText))
                clicked = words[i];
        y += places.back().y + bh + 10 * k;
    }
    d.screenTip = "Drag one of them to move them all; the arrow keys nudge them.";
    if (!clicked) return;
    const bool across = *clicked == "Left" || *clicked == "Centre" || *clicked == "Right" || *clicked == "Across";
    auto from = [&](const Item& i) { return across ? i.b.x : i.b.y; };
    auto size = [&](const Item& i) { return across ? i.b.w : i.b.h; };
    std::vector<float> by(items.size()); // how far each one moves
    float low = FLT_MAX, high = -FLT_MAX;
    for (const Item& i : items) low = std::min(low, from(i)), high = std::max(high, from(i) + size(i));
    if (*clicked == "Across" || *clicked == "Down") { // (in their order, the same space between them)
        std::vector<std::size_t> order(items.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::ranges::sort(order, {}, [&](std::size_t i) { return from(items[i]); });
        float sizes = 0.0f;
        for (const Item& i : items) sizes += size(i);
        const float space = (high - low - sizes) / static_cast<float>(items.size() - 1);
        float at = low;
        for (const std::size_t i : order) {
            by[i] = at - from(items[i]);
            at += size(items[i]) + space;
        }
    } else {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const bool start = *clicked == "Left" || *clicked == "Top", end = *clicked == "Right" || *clicked == "Bottom";
            by[i] = start ? low - from(items[i]) : end ? high - from(items[i]) - size(items[i]) : (low + high) / 2 - from(items[i]) - size(items[i]) / 2;
        }
    }
    const std::string key = std::format("line-up:{}", ++d.focusCount);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (std::lround(by[i]) == 0) continue;
        const SDL_FRect at = d.screenView->rectOf(items[i].element), box = d.screenView->boxOf(items[i].element);
        Part q = editor.part(*screen, items[i].line);
        q.set(across ? "x" : "y", unitText(across ? placeFor(at.x + by[i] - box.x, at.w, box.w) : -placeFor(at.y + by[i] - box.y, at.h, box.h)));
        editor.setPart(*screen, q, key);
    }
}

// The chosen part's form (or, with none chosen, the screen's): its name, its text, where it is and its
// settings; or its line as text.
void Developer::drawPartForm(const ScreensFrame& f) {
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    const Part* selected = f.selected;
    ScreenDef* def = f.def;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    const float labelH = layout::labelAboveH * k, boxH = layout::boxH * k, rowGap = layout::rowGap * k, titleH = layout::titleH * k;
    auto field = [&](std::string_view label, const std::string& id, const std::string& value, float x, float w, std::string_view hint = {}) {
        ui.text(label, x, y, layout::smallText, theme::dim);
        return ui.field(id, {x, y + labelH, w, boxH}, value, layout::boxText, hint);
    };
    const bool header = !selected;
    const Part p = header ? editor.part(*screen, screen->line) : *selected;
    ui.formTitle(header ? std::format("Screen: {}", p.text) : p.option("name").empty() ? upperFirst(p.kind) : std::format("{}: {}", upperFirst(p.kind), p.option("name")),
                 right.x, y);
    if (!header) asTextButton(ui, right.x + right.w, y);
    y += titleH;
    const std::string id = std::format("part:{}:{}", screen->line, p.line);
    auto change = [&](const Part& changed, std::string_view what) { editor.setPart(*screen, changed, std::format("{}:{}:{}", id, what, d.focusCount)); };
    if (d.asText && !header) {
        if (const ScriptError* problem = d.problemAt(screen->file, p.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
        if (const auto text = drawRawLine(ui, id + ":raw", StoryEditor::lineOf(p), right.x, right.w, y)) {
            Part raw = p;
            raw.kind = *text;
            raw.text.clear();
            raw.quoted = false;
            raw.options.clear();
            raw.words.clear();
            raw.note.clear();
            change(raw, "raw");
        }
        return;
    }
    // The form (it scrolls when it's longer than its room): a part's text, where it is, then its
    // settings; or the screen's own.
    const SDL_FRect formBox{right.x, y, right.w, std::max(0.0f, right.y + right.h - y)};
    ui.scrollForm(formBox, 40 * k);
    y -= d.formScroll;
    const DeveloperUi::Clip clipped(ui, formBox, true);
    const float labelW = std::round(right.w * layout::partLabelPart);
    if (const ScriptError* problem = d.problemAt(screen->file, p.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
    // A change from the form: typed (one step for undo while one box is typed into) or picked.
    auto apply = [&](const std::optional<FormEdit>& edit, const std::vector<StoryEditor::Field>& fields, auto&& changed) {
        if (!edit) return;
        auto now = fields;
        std::ranges::find(now, edit->name, &StoryEditor::Field::name)->value = edit->value;
        if (edit->typed) change(changed(now), edit->name);
        else editor.setPart(*screen, changed(now), {}), d.focus.clear(); // (each click a step for undo)
    };
    if (header) {
        const auto fields = optionFields(editor, p, kScreenOptions);
        apply(drawForm(ui, id, fields, right.x, right.w, labelW, y), fields, [&](const auto& now) { return withOptions(editor, p, now, kScreenOptions); });
        y += 8 * k;
        y += ui.text("Click a part on the screen (or in the list) to choose it: drag it to move it, or its corner to resize it (with Shift: keeping "
                     "its shape), and the arrow keys nudge it (Shift: 10 pixels). Esc lets it go.",
                     right.x, y, layout::noteText, theme::dim, right.w).y;
    } else {
        // Its name: what the Parts list calls it (the game doesn't show it).
        const std::vector<StoryEditor::Field> name{{.name = "name", .value = p.option("name"), .label = "Name", .box = Box::Name, .hint = "None"}};
        if (const auto edit = drawForm(ui, id, name, right.x, right.w, labelW, y)) {
            Part changed = p;
            changed.set("name", partName(edit->value));
            change(changed, "name");
        }
        // Its text (or the word after its kind), as a box with what it can be to pick from.
        if (const std::string_view label = textLabel(p.kind); !label.empty()) {
            const Box box = p.kind == "movie" ? Box::Movie : p.kind == "key" ? Box::Key : p.kind == "control" ? Box::Control : Box::None;
            std::vector<StoryEditor::Field> text;
            if (p.kind == "image") { // (a picture or a colour; or one for yes and one for no)
                std::string pictureText = p.text; // (bottom=: a colour fading down to another, as one value: navy-black)
                if (const std::string fadesTo = p.option("bottom"); !fadesTo.empty() && parseColor(pictureText, &engine.palette()) && !pictureText.contains('-')) pictureText += "-" + fadesTo;
                addPictureFields(text, "text", std::string(label), "", pictureText, editor.variables());
            } else {
                text.push_back({.name = "text", .value = p.text, .label = std::string(label), .box = box, .rich = p.kind == "text" || p.kind == "button"});
            }
            if (const auto edit = drawForm(ui, id, text, right.x, right.w, labelW, y)) {
                Part changed = p;
                changed.text = edit->value;
                if (p.kind == "image") {
                    auto now = text;
                    if (const auto edited = std::ranges::find(now, edit->name, &StoryEditor::Field::name); edited != now.end()) edited->value = edit->value;
                    changed.text = pictureFrom(editor, now, "text");
                    if (parseGradient(changed.text, &engine.palette())) changed.set("bottom", ""); // (one value now: navy-black)
                }
                changed.quoted = p.kind == "text" || p.kind == "button" || p.kind == "image" || p.kind == "movie";
                change(changed, "text");
            }
        }
        // Where it is: x, y, width and height (empty: the top left corner, and its own size, shown as it is now).
        ui.text("Place and size", right.x, y, layout::groupText, kAccent);
        y += layout::groupStep * k;
        const float quarter = (right.w - 3 * 6 * k) / 4;
        const auto el = f.elementOf(p.line);
        for (int i = 0; i < 4; ++i) {
            static constexpr std::array names{"x", "y", "w", "h"}, labels{"X", "Y", "Width", "Height"};
            const std::string option = names[static_cast<std::size_t>(i)];
            std::string hint = i == 0 ? "-1" : i == 1 ? "1" : "";
            if (i >= 2 && el && d.screenView) { // (its own size now, as a share of its box)
                const Element& elDef = def->elements[*el];
                const SDL_FRect elBox = elDef.parent >= 0 ? d.screenView->rectOf(static_cast<std::size_t>(elDef.parent)) : SDL_FRect{0.0f, 0.0f, engine.canvas().w, engine.canvas().h};
                const SDL_FRect autoSize = d.screenView->bounds(*el);
                const float of = i == 2 ? elBox.w : elBox.h;
                if (of > 0.0f) hint = unitText((i == 2 ? autoSize.w : autoSize.h) / of);
            }
            if (const auto value = field(labels[static_cast<std::size_t>(i)], id + ":" + option, p.option(option), right.x + static_cast<float>(i) * (quarter + 6 * k), quarter, hint)) {
                Part changed = p;
                changed.set(option, std::string(trim(*value)));
                change(changed, option);
            }
        }
        y += labelH + boxH + rowGap;
        const auto options = partOptions(p.kind);
        auto fields = optionFields(editor, p, options);
        if (el) showStyleValues(fields, def->elements[*el].style, engine.palette()); // (an empty look box: what its style gives it)
        apply(drawForm(ui, id, fields, right.x, right.w, labelW, y), fields, [&](const auto& now) { return withOptions(editor, p, now, options); });
    }
    ui.formDrawn(formBox, y);
}

} // namespace vn
