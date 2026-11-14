// The page without a game: what VNext opens and where, New game and Look again, that place's folder, and what a game
// folder holds.
#include "DeveloperUi.hpp"

namespace vn {

namespace {

// What a game folder holds, a folder a row: its name (as a folder's written), and what goes in it.
struct Holds {
    std::string_view folder;
    ToolsText what;
};
const Holds kHolds[]{
    {"scripts/", "the story, its screens and its Library, from main.vnscript"_text},
    {"images/", "the visuals that stand still: backgrounds, the cast and anything else it shows; the gallery's in images/gallery"_text},
    {"sounds/", "music and sounds; the music room's songs in sounds/music, voices in sounds/voices"_text},
    {"movies/", "the visuals that move (the gallery's in movies/gallery), and their subtitles (.srt)"_text},
    {"fonts/", "the fonts its words are written in"_text},
    {"translations/", "the game in other languages"_text},
};

} // namespace

// Laid out from the top of `box` (drawn only when `draw`: else just measured, for its page part): how tall it is.
float Developer::noGamePage(DeveloperUi& ui, const SDL_FRect& box, bool draw) {
    DeveloperTools& d = tools;
    const float k = ui.k, gap = layout::gap * k, x = box.x, col = std::min(box.w, layout::formMost * k);
    float y = box.y + gap;
    auto paragraph = [&](std::string_view words, float at, float size, SDL_Color colour, float wrap) { // (how tall)
        if (draw) ui.text(words, at, y, size, colour, wrap);
        return ui.measure(words, size, wrap).y;
    };
    y += paragraph("There's no game next to VNext yet. VNext opens the one it finds there: a game folder (a game being made) or game.vnext (a finished one)."_tr,
                   x, layout::lineText, theme::text, col) + 3 * gap;

    // New game and Look again: big buttons, each with what it does beside it.
    struct Action {
        int command;
        std::string label, what;
    };
    const Action actions[]{{kNewGame, "New game"_tr,
                            "Makes a game folder next to VNext and opens it in the Story page: an empty game, with its first scene and its menus (a title screen, the pause menu, saving and loading, Settings and the history), or one with VNext's samples: a cast of three, places, music, sounds and every menu, to use or change."_tr},
                           {kLookAgain, "Look again"_tr, "Opens a game folder or game.vnext put next to VNext since."_tr}};
    float buttonW = 0.0f;
    for (const Action& a : actions) buttonW = std::max(buttonW, ui.buttonWidth(a.label, layout::buttonText, layout::widerButtonW));
    const float buttonH = layout::bigButtonH * k, noteX = x + buttonW + 2 * gap, noteW = std::max(1.0f, col - buttonW - 2 * gap);
    for (const Action& a : actions) {
        const float noteH = ui.measure(a.what, layout::noteText, noteW).y, rowH = std::max(buttonH, noteH);
        if (draw) {
            if (ui.button({x, y + (rowH - buttonH) / 2, buttonW, buttonH}, a.label, false, engine.noGame(), layout::buttonText)) d.pressedTool = a.command;
            ui.text(a.what, noteX, y + (rowH - noteH) / 2, layout::noteText, theme::dim, noteW);
        }
        y += rowH + gap;
    }

    // Where that is (cut to fit: its whole in a tip), and its folder.
    y += 2 * gap;
    const std::string where = "Next to VNext"_tr, place = engine.gamePlaces().front(), folder = "Show the folder"_tr;
    const float whereW = ui.width(where, layout::labelText) + 2 * gap, folderW = ui.buttonWidth(folder, layout::buttonText, layout::wideButtonW);
    const float rowH = layout::smallButtonH * k + gap, middle = y + rowH / 2;
    if (draw) { // (the folder's button right after the place, or at the column's end when that's cut)
        ui.textFitted(where, x, middle, layout::labelText, theme::dim, whereW);
        const float placeW = ui.textFitted(place, x + whereW, middle, layout::boxText, theme::text, std::max(1.0f, col - whereW - folderW - 2 * gap));
        if (ui.button({x + whereW + placeW + 2 * gap, y + gap / 2, folderW, layout::smallButtonH * k}, folder, false, true, layout::buttonText)) d.pressedTool = kFolder;
    }
    y += rowH + 3 * gap;

    // What a game folder holds: its folders, a row each.
    y += paragraph("What a game folder holds"_tr, x, layout::labelText, theme::dim, col) + gap;
    float nameW = 0.0f;
    for (const Holds& h : kHolds) nameW = std::max(nameW, ui.width(h.folder, layout::boxText));
    nameW += 3 * gap;
    for (const Holds& h : kHolds) {
        const std::string what(tr(h.what));
        const float whatH = ui.measure(what, layout::noteText, std::max(1.0f, col - nameW)).y, holdsH = std::max(ui.measure(h.folder, layout::boxText).y, whatH);
        if (draw) {
            ui.text(h.folder, x, y, layout::boxText, theme::text);
            ui.text(what, x + nameW, y + (ui.measure(h.folder, layout::boxText).y - ui.measure("Ag", layout::noteText).y) / 2, layout::noteText, theme::dim,
                    std::max(1.0f, col - nameW));
        }
        y += holdsH + gap;
    }
    return y - box.y;
}

} // namespace vn
