// The Library page: the game's files by kind (images with movies, sounds with the lines' voices; and files brought in),
// what the scripts list by name (colours, styles, endings...), each with its form, the variables, and the game's settings (its translations are
// LibraryTranslations.cpp's, its animations LibraryAnimations.cpp's, atlases and sub-images LibraryImages.cpp's).
#include "DeveloperUi.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace vn {

namespace {

// The kind a file is, by its type: an image, a sound, a movie or a font file.
const FileKind* kindOfFile(std::string_view name) {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) return nullptr;
    const std::string ext = lower(name.substr(dot));
    const auto it = std::ranges::find_if(kFileKinds, [&](const FileKind& f) { return std::ranges::contains(f.extensions, std::string_view(ext)); });
    return it != std::end(kFileKinds) ? &*it : nullptr;
}

// The kinds of file a Library list holds: Images, images and movies (moving images: they go where an image goes); the
// others, their own. And all their types (what Import... takes).
std::vector<const FileKind*> kindsIn(const FileKind& list) {
    std::vector<const FileKind*> out{&list};
    if (list.id == "images") out.push_back(&*std::ranges::find(kFileKinds, std::string_view("movies"), &FileKind::id));
    return out;
}
std::vector<std::string_view> typesIn(const FileKind& list) {
    std::vector<std::string_view> out;
    for (const FileKind* kind : kindsIn(list)) out.insert(out.end(), kind->extensions.begin(), kind->extensions.end());
    return out;
}

// What a rename of a Library definition that can't be says (an ending's, an achievement's or a tip's name is kept in a
// variable's: no -).
std::string_view renameMistake(std::string_view kind) {
    return kind == "ending" || kind == "achievement" || kind == "tip" ? std::string_view("A name is one word (letters, numbers and _) that no other one of these has."_tr)
                                                                       : std::string_view("A name is one word (letters, numbers, _ and -) that no other one of these has."_tr);
}

// A setting's value in its two parts, each a row of its own (Setting::part): the game's size's width and height, the
// pointer's tip's X and Y (two numbers); the time's start's day (a date or a weekday) and its time of day (a time, 6:30 pm
// too, or a part of the day).
std::array<std::string, 2> settingParts(std::string_view name, std::string_view value) {
    std::array<std::string, 2> out;
    const auto words = rawWords(value);
    if (name == "time_start") {
        for (const std::string& w : words) {
            const std::string low = lower(w);
            const bool day = out[0].empty() && out[1].empty() &&
                             (std::ranges::count(low, '-') == 2 || std::ranges::contains(kWeekdays, low, [](std::string_view d) { return lower(d); }));
            std::string& to = day ? out[0] : out[1];
            to += (to.empty() ? "" : " ") + w;
        }
        return out;
    }
    for (std::size_t i = 0; i < words.size() && i < out.size(); ++i) out[i] = words[i];
    return out;
}

// What a setting's other part's row says when it's empty (its own default: the size's other number).
std::string settingPartStandard(std::string_view name, int part) {
    for (const Setting& s : kSettings)
        if (s.name == name && s.part == part) return std::string(s.standard);
    return {};
}

} // namespace

// Files picked in the Library's file chooser, or dropped on the window: copied into the game folder
// (images into images/, sounds into sounds/, movies into movies/, fonts into fonts/), under a name of
// their own if one's there already; or, picked for a place of its own (a translation's own image, a
// line's voice), there, in place of one there already (to the Trash). Undo takes them out again.
void Developer::importFiles() {
    DeveloperTools& d = tools;
    std::vector<std::pair<std::string, std::string>> files;
    {
        const std::scoped_lock _(d.imports.mutex);
        files.swap(d.imports.files);
    }
    if (files.empty() || engine.noGame()) return;
    std::size_t copied = 0;
    std::string last, skipped, firstImage;
    StoryEditor::Moves moves;
    const std::optional<SDL_FPoint> dropAt = std::exchange(d.imports.dropAt, std::nullopt);
    ++d.filesGeneration; // (the lists show what's imported)
    auto folderOf = [](std::string_view ext) -> std::string_view {
        const auto kind = std::ranges::find_if(kFileKinds, [&](const FileKind& f) { return std::ranges::contains(f.extensions, ext); });
        return kind != std::end(kFileKinds) ? kind->folder : std::string_view{};
    };
    for (const auto& [from, to_as] : files) {
        const auto slash = from.find_last_of("/\\");
        std::string name = from.substr(slash == std::string::npos ? 0 : slash + 1);
        auto dot = name.rfind('.');
        const std::string ext = dot == std::string::npos ? std::string() : lower(name.substr(dot));
        const std::string_view folder = folderOf(ext);
        const std::string as = folder == "movies/" && to_as.starts_with("images/gallery/") ? "movies/gallery/" : to_as; // (a movie for the gallery: its own folder)
        if (folder.empty() || (!as.empty() && !as.starts_with(folder))) {
            skipped = name;
            continue;
        }
        { // (named as scripts call it: "19. YOMI.flac" is 19_YOMI.flac)
            std::string stem = scriptName(name.substr(0, dot));
            if (stem.empty()) stem = folder == "images/" ? "image" : folder == "sounds/" ? "sound" : folder == "movies/" ? "movie" : "font";
            name = stem + ext;
            dot = stem.size();
        }
        std::string to;
        if (as.empty() || as.ends_with('/')) { // (into its kind's folder, or one in it: the gallery's, images/gallery/)
            const std::string into = as.empty() ? std::string(folder) : as;
            SDL_CreateDirectory((engine.gameRoot() + into).c_str());
            const std::string stem = uniqueName(name.substr(0, dot), [&](const std::string& n) { return fileExists(engine.gameRoot() + into + n + name.substr(dot)); });
            to = engine.gameRoot() + into + stem + name.substr(dot);
        } else { // (one there by that name, of any kind of its folder's files, goes to the Trash)
            to = engine.gameRoot() + as + ext;
            SDL_CreateDirectory(to.substr(0, to.find_last_of('/')).c_str()); // (and the folders it's in)
            if (const auto kind = std::ranges::find(kFileKinds, folder, &FileKind::folder); kind != std::end(kFileKinds) && folder != "fonts/")
                for (const std::string_view other : kind->extensions)
                    if (const std::string old = engine.gameRoot() + as + std::string(other); fileExists(old))
                        if (const auto trashed = moveToTrash(old); trashed && !trashed->empty()) moves.emplace_back(old, *trashed);
        }
        if (SDL_CopyFile(from.c_str(), to.c_str())) {
            ++copied;
            last = to.substr(engine.gameRoot().size());
            if ((folder == "images/" || folder == "movies/") && firstImage.empty()) firstImage = last.substr(7, last.rfind('.') - 7); // (images/ and movies/: as long)
            moves.emplace_back(std::string{}, to);
            if (as.empty()) {
                d.library = folder == "images/" || folder == "movies/" ? "images" : folder == "sounds/" ? "sounds" : "fontfiles";
                d.libraryItem.reset();
                d.librarySub.reset();
                d.libraryAtlas.reset();
                d.libraryVoice.reset();
            }
        }
    }
    d.editor.movedFiles(std::move(moves));
    if (copied > 0) {
        logInfo("{}", copied == 1 ? "Imported '{}'."_tr(last) : "Imported {} files."_tr(copied));
    } else {
        d.failed = true;
        d.status = !skipped.empty() ? "'{}' isn't a visual, sound or font file the game can use there."_tr(skipped) : "Can't copy that into the game folder."_tr();
    }
    // An image (or a movie) dropped on the screen open in the Screens page: a part showing it, where it was dropped
    // (in the part chosen there when it landed on that, else the screen), placed by its corner.
    bool placed = false;
    if (dropAt && !firstImage.empty() && d.page == DeveloperTools::Page::Screens && d.screenView && d.script && pointInRect(dropAt->x, dropAt->y, d.shownScreen))
        if (const auto sc = std::ranges::find_if(d.editor.screens(), [&](const StoryEditor::Screen& s) { return lower(s.name) == lower(d.screenName); });
            sc != d.editor.screens().end()) {
            const ScreenDef& def = d.screenView->def();
            const float scale = d.shownScreen.w / engine.canvas().w;
            const float px = (dropAt->x - d.shownScreen.x) / scale, py = (dropAt->y - d.shownScreen.y) / scale;
            SDL_FRect box{0.0f, 0.0f, engine.canvas().w, engine.canvas().h};
            std::optional<StoryEditor::Part> into;
            if (d.part)
                for (const Element& el : def.elements)
                    if (el.line == static_cast<int>(*d.part) + 1 && d.script->files[el.file] == d.editor.path(sc->file) && pointInRect(px, py, d.screenView->partRect(d.screenView->partIndex(el)))) {
                        box = d.screenView->partRect(d.screenView->partIndex(el));
                        into = d.editor.part(*sc, *d.part);
                    }
            const std::string line = std::format("image \"{}\" x={} y={}", firstImage, unitText(placeFor(px - box.x, 0.0f, box.w)), unitText(-placeFor(py - box.y, 0.0f, box.h)));
            d.part = d.editor.addPart(*sc, into ? &*into : nullptr, line);
            d.moreParts.clear();
            logInfo("{}", "Imported '{}', and put it on the screen."_tr(last));
            placed = true;
        }
    if (copied > 0 && !placed) d.page = DeveloperTools::Page::Library;
}

// The Library's Settings: the game's setting lines as a form, its groups' headings and rows as every form's (the page part's
// title says Settings). An empty box, or the choice a setting starts with, takes its line out of main.vnscript (it
// has its default again).
void Developer::drawSettingsForm(DeveloperUi& ui, const SDL_FRect& right) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    SDL_Renderer* r = ui.r;
    const float k = ui.k, boxH = layout::boxH * k, rowGap = layout::rowGap * k;
    // The time's rows that apply to its kind: none of them without time, no start in real time (the computer's
    // clock and calendar), no clock with days and parts of the day; and the phone's own clock and date only while it
    // doesn't follow the time (when it does, it shows the game's).
    const std::string timeKind = lower(editor.setting("time").value_or("none"));
    const bool phoneFollows = timeKind != "none" && lower(editor.setting("phone_follows_time").value_or("yes")) != "no";
    auto shown = [&](const Setting& s) {
        if (s.name == "time_start") return timeKind == "story" || timeKind == "days";
        if (s.name == "clock") return timeKind == "story" || timeKind == "real";
        if (s.name == "time_parts" || s.name == "phone_follows_time" || s.name == "backgrounds_follow_time") return timeKind != "none";
        if (s.name == "phone_clock" || s.name == "phone_date") return !phoneFollows;
        return true;
    };
    // Mistakes on setting lines the form shows no row for (setting default..., one that isn't a setting, a time row
    // its kind hides), at the top.
    std::vector<const ScriptError*> others;
    for (const ScriptError& e : d.problems)
        if (inSettings(editor, e) && std::ranges::none_of(kSettings, [&](const Setting& s) {
                return !s.name.empty() && shown(s) && editor.settingLine(s.name) == static_cast<std::size_t>(e.line - 1);
            }))
            others.push_back(&e);
    const float labelW = std::round(std::min(layout::settingLabelW * k, right.w * layout::settingLabelPart)), fieldW = std::round(std::min(layout::settingFieldW * k, right.w * layout::settingFieldPart));
    // It scrolls (Page Up and Down too, while nothing's typed into; the arrows go from setting to setting, scrolling one
    // out of sight into it).
    const SDL_FRect& formBox = right;
    if (d.focus.empty())
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            switch (key.key) {
            case SDLK_PAGEUP: d.formScroll -= formBox.h * 0.9f; return true;
            case SDLK_PAGEDOWN: d.formScroll += formBox.h * 0.9f; return true;
            default: return false;
            }
        });
    ui.scrollForm(formBox, boxH + rowGap);

    const float fieldX = right.x + labelW, problemX = fieldX + fieldW + layout::settingProblemGap * k, problemW = std::max(0.0f, right.x + right.w - problemX);
    const DeveloperUi::Clip clipped(ui, formBox);
    float y = formBox.y - d.formScroll;
    for (const ScriptError* e : others)
        y += ui.text("main.vnscript line {}: {}"_tr(e->line, e->message), right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
    // What an empty box means, as the other settings have it now: the time's start for its kind and clock, and the
    // parts of the day on the clock chosen. Else the setting's own default.
    const bool clock12 = editor.setting("clock").value_or("24") == "12";
    auto clockWords = [&](int minutes) {
        const int hours = minutes / 60, m = minutes % 60;
        return clock12 ? std::format("{}:{:02} {}", hours % 12 == 0 ? 12 : hours % 12, m, hours < 12 ? "AM" : "PM") : std::format("{}:{:02}", hours, m);
    };
    auto hintOf = [&](const Setting& s) -> std::string {
        if (s.name == "time_start" && s.part == 2) { // (its time of day: the day's own start)
            if (timeKind == "days") { // (the first part of the day)
                const std::string parts = unquote(editor.setting("time_parts").value_or("morning"));
                return std::string(firstWord(parts));
            }
            return clockWords(8 * 60);
        }
        if (s.name == "time_parts" && clock12) return std::format("morning {}, afternoon {}, evening {}, night {}", clockWords(6 * 60), clockWords(12 * 60), clockWords(18 * 60), clockWords(22 * 60));
        if (s.name == "save_folder") return saveFolderNameOf({}, unquote(editor.setting("title").value_or("VNext"))); // (the title's, as the folder is called)
        return !s.standard.empty() ? std::string(s.standard) : !s.hint.empty() ? std::string(s.hint) : std::string("None");
    };
    // A setting's control in `field` (buttons for its choices, else a box with its default in grey)
    // and its problem, if its line has one.
    auto control = [&](const Setting& s, const SDL_FRect& field) -> const ScriptError* {
        const auto written = editor.setting(s.name);
        const auto line = editor.settingLine(s.name);
        const ScriptError* problem = line ? d.problemAt(0, *line) : nullptr;
        if (!s.choices.empty() || !StoryEditor::kindOf(s.box).choices.empty()) { // (its kind's buttons, as every form draws them)
            StoryEditor::Field f{.name = std::string(s.name), .value = lower(written.value_or(std::string(s.standard))), .box = s.box, .choices = std::string(s.choices),
                                 .usual = std::string(s.standard)};
            applyBox(f);
            if (const auto word = ui.choices(std::format("setting:{}", s.name), field, f)) editor.setSetting(s.name, *word == s.standard ? "" : *word, false, {});
        } else {
            const std::string id = s.part ? std::format("setting:{}:{}", s.name, s.part) : std::format("setting:{}", s.name);
            const bool percent = StoryEditor::kindOf(s.box).percent; // (shown and written as one, as everywhere)
            const auto parts = settingParts(s.name, written.value_or(std::string{}));
            const std::string shownValue = s.part ? parts[static_cast<std::size_t>(s.part - 1)] : written.value_or(std::string{});
            StoryEditor::Field f{.name = std::string(s.name), .value = percent && !shownValue.empty() ? percentText(shownValue) : shownValue, .box = s.box,
                                 .hint = hintOf(s), .suggest = std::string(s.suggest)};
            applyBox(f);
            if (s.name == "mouse_pointer") f.context = "still"; // (the pointer: no movie; the phone's wallpapers can be)
            if (s.name == "time_start" && s.part == 2) f.context = "time"; // (its time of day: the game's parts of the day, or a time typed)
            if (const auto value = ui.pickField(id, field, f, layout::boxText)) {
                std::string v = *value;
                if (s.quoted) std::erase(v, '"');
                if (percent && !v.empty()) v = percentText(v);
                if (s.part) { // (this part, with the other as it is: two numbers both written, the other's default when it has none; a start's day and time either)
                    auto now = parts;
                    now[static_cast<std::size_t>(s.part - 1)] = std::string(trim(v));
                    if (s.name != "time_start" && (!now[0].empty() || !now[1].empty()))
                        for (int i = 0; i < 2; ++i)
                            if (now[static_cast<std::size_t>(i)].empty()) now[static_cast<std::size_t>(i)] = settingPartStandard(s.name, i + 1);
                    v = std::string(trim(now[0] + " " + now[1]));
                }
                if (s.name == "game_size" && !d.sizeFrom) // (see drawDeveloper)
                    d.sizeFrom = written.value_or(settingPartStandard(s.name, 1) + " " + settingPartStandard(s.name, 2));
                editor.setSetting(s.name, v, s.quoted, id + std::to_string(d.focusCount));
                if (s.name == "game_size") d.sizeKey = id + std::to_string(d.focusCount);
            }
        }
        return problem;
    };
    bool first = true;
    for (const Setting& s : kSettings) {
        if (!shown(s)) continue;
        const float top = y + (s.name.empty() && !first ? layout::groupGap * k : 0.0f);
        y = top + (s.name.empty() ? layout::groupStep * k : boxH + rowGap);
        first = false;
        if (s.name.empty()) { // (a group's heading, as every form has them, and its settings in under it; those out of sight are drawn too, cut off: the keys go to them)
            ui.text(tr(s.label), right.x, top, layout::groupText, kAccent);
            continue;
        }
        const float middle = top + boxH / 2, in = layout::groupIndent * k;
        ui.textFitted(tr(s.label), right.x + in, middle, layout::labelText, theme::dim, labelW - in - 8 * k);
        const ScriptError* problem = control(s, {fieldX, top, fieldW, boxH});
        if (s.part > 1) problem = nullptr; // (a setting in parts: its mistake beside its first)
        if (problem) draw::rect(r, {right.x, top, layout::markW * k, boxH}, kBad);
        if (problem) ui.textFitted(problem->message, problemX, middle, layout::smallText, kBad, problemW);
    }
    ui.formDrawn(formBox, y);
}

// The Library page as it is this frame: the kind of file (or definition) listed, and where its list, the row of
// buttons under it and the form go (in the room their page parts were given). Its files are drawn by drawLibraryFiles
// (the one chosen by drawLibraryFile), its definitions by drawLibraryDefinitions, its variables and voiced lines by
// drawVariables (and a voiced line's form by drawVoiceForm): each its list or its form.
struct Developer::LibraryFrame {
    DeveloperUi& ui;
    DeveloperTools& d;
    const FileKind* fileKind; // (none: definitions are listed)
    SDL_FRect listBox, newRow, right;
    float buttonH, listGap;
    SDL_FRect secondRow{}; // (images' and sounds': Import to the gallery... or the music room..., under Import...)


};

// The Library page: the game's images and movies, sounds (and the lines' voices) and font files (to look at, hear,
// import and rename; an image's atlases and sub-images and a font file's fonts under it), what the scripts list by name
// (colours, styles, animations, endings, achievements, tips, controls), each as a form, the variables, the
// translations and the game's settings.
void Developer::drawLibrary(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    // Its columns, side by side: the Library, the kind chosen (as wide: what's in it, under its heading), and the form
    // (the rest, for what it shows: an image, an atlas, an animation).
    const SDL_FRect left = ui.columns(area).left, column{left.x + left.w, left.y, left.w, left.h};
    const SDL_FRect right{column.x + column.w, left.y, std::max(0.0f, area.x + area.w - column.x - column.w), left.h};

    // --- What's listed, in the order a game is made in: its pictures (images and movies, the atlases cut from them),
    // its sounds (with the lines' voices), how its words and screens look and move, what it keeps track of, its keys, its
    // translations, and its settings (main.vnscript's, once there is one).
    static constexpr std::string_view kOrder[]{"images",    "sounds", "fontfiles",   "colour", "style",   "animation",    "variables",
                                               "ending",    "achievement", "tip",    "control", "translations", "settings"};
    std::vector<std::pair<std::string_view, std::string_view>> kinds; // id, label
    for (const std::string_view id : kOrder) {
        if (id == "settings" && !editor.hasMain()) continue;
        if (const auto file = std::ranges::find(kFileKinds, id, &FileKind::id); file != std::end(kFileKinds)) kinds.emplace_back(id, file->label);
        else if (const auto def = std::ranges::find(kDefinitionKinds, id, &DefinitionKind::kind); def != std::end(kDefinitionKinds)) kinds.emplace_back(id, def->label);
        else
            kinds.emplace_back(id, id == "variables"      ? std::string_view("Variables"_text)
                                   : id == "translations" ? std::string_view("Translations"_text)
                                                          : std::string_view("Settings"_text));
    }
    if (std::ranges::none_of(kinds, [&](const auto& kind) { return kind.first == d.library; })) d.library = kinds.front().first;
    auto wrongIn = [&](std::string_view kind) { // (images and font files: what's made from them)
        if (kind == "settings") return std::ranges::any_of(d.problems, [&](const ScriptError& e) { return inSettings(editor, e); });
        auto wrong = [&](std::string_view of) {
            return std::ranges::any_of(editor.definitions(of), [&](const StoryEditor::Definition& def) { return d.problemWithin(def.file, def.line, def.end); });
        };
        if (kind == "images") return wrong("atlas") || wrong("sub-image");
        return wrong(kind == "variables" ? "variable" : kind == "fontfiles" ? "font" : kind);
    };
    const auto fileKind = std::ranges::find(kFileKinds, std::string_view(d.library), &FileKind::id);
    const bool files = fileKind != std::end(kFileKinds);
    LibraryFrame f{.ui = ui, .d = d, .fileKind = files ? fileKind : nullptr, .listBox = {}, .newRow = {}, .right = right, .buttonH = layout::buttonH * k,
                   .listGap = layout::gap * k};
    // The kind chosen's list, or its form, as that kind has them.
    auto drawKind = [&](KindPagePart pagePart) {
        if (d.library == "variables") return drawVariables(f, pagePart);
        return files ? drawLibraryFiles(f, pagePart) : drawLibraryDefinitions(f, pagePart);
    };
    // Its page parts: the Library (the kinds), the kind chosen (what's in it, a button under it; images and sounds, two;
    // the translations, four) and the chosen one's form (a translation's: its texts); or Settings, a form as wide as both.
    const auto kind = std::ranges::find(kinds, std::string_view(d.library), &std::pair<std::string_view, std::string_view>::first);
    const PagePart libraryPagePart =
        listPagePart(ui, "library", "Library"_tr, left, 0, [&](const ListRoom& at) {
            const auto clicked = ui.list(at.list, d.libraryScroll, kinds.size(), [&](std::size_t i) { return std::string(tr(kinds[i].second)); },
                                         [&](std::size_t i) { return kinds[i].first == d.library; }, [&](std::size_t i) { return wrongIn(kinds[i].first); });
            if (!clicked || kinds[*clicked].first == d.library) return false;
            d.library = kinds[*clicked].first;
            d.libraryItem.reset();
            d.librarySub.reset();
            d.libraryAtlas.reset();
            d.libraryVoice.reset();
            d.pickingAnimation = false;
            d.kindScroll = d.formScroll = 0.0f;
            d.focus.clear();
            d.armed.clear();
            return true; // (another kind: its list and form from the next frame)
        });
    if (d.library == "settings") {
        std::array pageParts{libraryPagePart, formPagePart(ui, "settings", "Settings"_tr, {column.x, column.y, right.x + right.w - column.x, column.h}, true,
                                                [&](const SDL_FRect& box) { return drawSettingsForm(ui, box), false; })};
        drawPageParts(ui, "library", pageParts);
        return;
    }
    if (d.library == "translations") {
        const std::vector<std::string> ids = translationIds();
        std::array pageParts{libraryPagePart, listPagePart(ui, "kind", "Translations"_tr, column, 4, [&](const ListRoom& at) { return drawTranslationsList(ui, at, ids); }),
                          formPagePart(ui, "form", "Form"_tr, right, !d.translation.id.empty(), [&](const SDL_FRect& box) {
                              if (!d.translation.id.empty()) drawTranslationForm(ui, box, ids);
                              return false;
                          })};
        drawPageParts(ui, "library", pageParts);
        return;
    }
    std::array pageParts{
        libraryPagePart,
        listPagePart(ui, "kind", kind != kinds.end() ? std::string(tr(kind->second)) : std::string{}, column,
                     d.library == "images" || d.library == "sounds" ? 2 : 1,
                     [&](const ListRoom& at) {
                         f.listBox = at.list;
                         f.newRow = at.rows.empty() ? SDL_FRect{} : at.rows[0];
                         f.secondRow = at.rows.size() > 1 ? at.rows[1] : SDL_FRect{};
                         return drawKind(KindPagePart::List);
                     }),
        formPagePart(ui, "form", "Form"_tr, right, d.libraryItem || d.librarySub || d.libraryAtlas || d.libraryVoice || d.pickingAnimation, [&](const SDL_FRect& box) {
            f.right = box;
            return drawKind(KindPagePart::Form);
        })};
    drawPageParts(ui, "library", pageParts);
}

// The files of the kind chosen (Images: the movies too, each marked so): listed (the folders', then the translations' own:
// images/spanish/...), renamed (a double click, then Enter: the lines that use it follow) and imported (or dropped on the
// window). An image's sub-images and a font file's fonts are listed under it, when its arrow opens them. Sounds: after
// them, the lines that play a voice file (its own voice=, or its speaker's voice_files=), each marked as a voice.
bool Developer::drawLibraryFiles(const LibraryFrame& f, KindPagePart pagePart) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    SDL_Renderer* r = f.ui.r;
    const float k = f.ui.k;
    using Field = StoryEditor::Field;
    using ListKind = DeveloperUi::ListEvent::Kind;
    const FileKind* fileKind = f.fileKind;
    const SDL_FRect& right = f.right;
    const SDL_FRect& listBox = f.listBox;
    // (the gallery's pictures and the music room's songs, in their folders: gallery/beach.png, gallery/rain.mpg, music/theme.ogg)
    auto recordFolderOf = [](const FileKind& kind) -> std::string_view { return kind.id == "images" || kind.id == "movies" ? "gallery/" : kind.id == "sounds" ? "music/" : ""; };
    const std::string_view recordFolder = recordFolderOf(*fileKind);
    const std::vector<const FileKind*> kinds = kindsIn(*fileKind);
    auto listed = [&] {
        std::vector<std::string> names, translated;
        for (const FileKind* kind : kinds) {
            const std::string folder = engine.gameRoot() + std::string(kind->folder);
            const std::string_view records = recordFolderOf(*kind);
            for (const std::string_view ext : kind->extensions)
                for (const std::string& name : d.cachedFiles(folder, ext)) names.push_back(name);
            if (!records.empty())
                for (const std::string_view ext : kind->extensions)
                    for (const std::string& name : d.cachedFiles(folder + std::string(records), ext)) names.push_back(std::string(records) + name);
            for (const std::string& language : d.cachedFiles(engine.gameRoot() + "translations/", ".vnscript"))
                for (const std::string_view ext : kind->extensions)
                    for (const std::string& name : d.cachedFiles(folder + stemOf(language) + "/", ext))
                        translated.push_back(stemOf(language) + "/" + name);
        }
        std::ranges::sort(names);
        std::ranges::sort(translated);
        names.insert(names.end(), translated.begin(), translated.end());
        return names;
    };
    const std::vector<std::string> names = listed();
    auto folderOf = [&](const std::string& name) { // (its kind's: an Images list's movie is in movies/)
        const FileKind* kind = kindOfFile(name);
        return kind ? kind->folder : fileKind->folder;
    };
    auto isMovie = [&](const std::string& name) { const FileKind* kind = kindOfFile(name); return kind && kind->id == "movies"; };
    // Sounds: the lines that play a voice file, after the files (one whose file isn't there yet: faint).
    const std::vector<DeveloperTools::VoicedLine> noVoices;
    const std::vector<DeveloperTools::VoicedLine>& voices = fileKind->id == "sounds" ? voicedLines() : noVoices;
    clampIndex(d.libraryVoice, voices.size());
    auto voiceThere = [&](const DeveloperTools::VoicedLine& v) {
        return std::ranges::any_of(kSoundTypes, [&](std::string_view ext) { return d.cachedExists(engine.gameRoot() + "sounds/" + v.file + std::string(ext)); });
    };
    // Files no script line names (aiko_happy is used when aiko is: a look of hers): marked "unused".
    if (d.unusedVersion != editor.version() || d.unusedOf != d.library || d.unusedCount != names.size()) {
        d.unusedVersion = editor.version();
        d.unusedOf = d.library;
        d.unusedCount = names.size();
        d.unused.clear();
        for (const std::string& n : names) {
            const std::string base = n.substr(n.find('/') == npos ? 0 : n.find('/') + 1);
            std::string stem = scriptName(stemOf(base));
            bool used = false;
            while (!stem.empty() && !used) {
                used = editor.mentions(stem);
                const auto cut = stem.rfind('_');
                stem = cut == npos ? std::string{} : stem.substr(0, cut);
            }
            if (!used) d.unused.insert(n);
        }
    }
    if (!d.libraryFind.empty()) {
        for (std::size_t i = 0; i < names.size(); ++i)
            if (const std::string& n = names[i]; !n.contains('/') || (!recordFolder.empty() && n.starts_with(recordFolder))) // (not a translation's own)
                if (lower(n.starts_with(recordFolder) && !recordFolder.empty() ? std::string(recordFolder) + scriptName(n.substr(recordFolder.size(), n.rfind('.') - recordFolder.size())) : scriptName(n.substr(0, n.rfind('.')))) == d.libraryFind)
                    d.libraryItem = i;
        d.libraryFind.clear();
    }
    clampIndex(d.libraryItem, names.size());
    // What's made from them, each under the file it's from (one from a file that isn't there: at the end, on its own):
    // an image's atlases (atlas lines: from their image=, else the image named as they are) and sub-images (sub-image
    // lines), a font file's fonts (font lines).
    struct Made {
        std::string_view kind; // "atlas", "sub-image" or "font"
        std::size_t index;     // (in the editor's definitions of its kind)
        StoryEditor::Definition def;
        std::optional<std::size_t> file; // (the one it's from)
    };
    auto chosenOf = [&](std::string_view kind) -> std::optional<std::size_t>& { return kind == "atlas" ? d.libraryAtlas : d.librarySub; };
    const std::vector<std::string_view> madeKinds = fileKind->id == "images"      ? std::vector<std::string_view>{"atlas", "sub-image"}
                                                    : fileKind->id == "fontfiles" ? std::vector<std::string_view>{"font"}
                                                                                  : std::vector<std::string_view>{};
    std::vector<Made> made;
    for (const std::string_view kind : madeKinds) {
        const auto defs = editor.definitions(kind);
        clampIndex(chosenOf(kind), defs.size());
        for (std::size_t j = 0; j < defs.size(); ++j) {
            const auto fields = editor.fields(kind, defs[j]);
            const auto from = std::ranges::find(fields, kind == "atlas" ? "Image" : kind == "sub-image" ? "Cut from" : "File", &Field::name);
            std::string what = from == fields.end() ? std::string{} : lower(unquote(from->value));
            if (kind == "atlas" && what.empty()) what = lower(defs[j].name);
            std::optional<std::size_t> at;
            for (std::size_t i = 0; i < names.size() && !at; ++i)
                if (!names[i].contains('/') && !isMovie(names[i]) &&
                    (kind == "font" ? lower(std::string(fileKind->folder) + names[i]) == what : lower(scriptName(stemOf(names[i]))) == what))
                    at = i;
            made.push_back({kind, j, defs[j], at});
        }
    }
    std::optional<std::size_t> chosen; // (the one of them chosen)
    for (std::size_t m = 0; m < made.size() && !chosen; ++m)
        if (chosenOf(made[m].kind) == made[m].index) chosen = m;
    if (chosen && made[*chosen].file) // (the one chosen shows)
        if (const std::string& of = names[*made[*chosen].file]; !std::ranges::contains(d.libraryOpen, of)) d.libraryOpen.push_back(of);
    struct Row {
        std::optional<std::size_t> file, made, voice; // (made: one of `made`)
        std::size_t count = 0; // (a file: how many are made from it)
    };
    std::vector<Row> rows;
    for (std::size_t i = 0; i < names.size(); ++i) {
        rows.push_back({.file = i, .count = static_cast<std::size_t>(std::ranges::count(made, std::optional(i), &Made::file))});
        if (rows.back().count > 0 && std::ranges::contains(d.libraryOpen, names[i]))
            for (std::size_t m = 0; m < made.size(); ++m)
                if (made[m].file == i) rows.push_back({.made = m});
    }
    for (std::size_t m = 0; m < made.size(); ++m)
        if (!made[m].file) rows.push_back({.made = m});
    for (std::size_t v = 0; v < voices.size(); ++v) rows.push_back({.voice = v});
    auto voiceLabel = [&](const DeveloperTools::VoicedLine& v) { return v.who.empty() ? v.text : std::format("{}: {}", v.who, v.text); };
    auto madeWrong = [&](std::size_t m) { return d.problemWithin(made[m].def.file, made[m].def.line, made[m].def.end); };
    const bool arrows = std::ranges::any_of(rows, [](const Row& row) { return row.count > 0; }); // (room for them only when there's one)
    const float arrowW = madeKinds.empty() || !arrows ? 0.0f : 22 * k;
    if (pagePart == KindPagePart::List) { // (the list, then Import...)
        // (what's made from a file: copied, pasted and duplicated as the other lists' are, a file isn't; pasted, an atlas or a
        // sub-image by its first word)
        const StoryEditor::Definition* picked = chosen ? &made[*chosen].def : nullptr;
        if (!madeKinds.empty() &&
            libraryClipboard(
                "files", chosen ? made[*chosen].kind : madeKinds.back(), picked ? editor.textOf(picked->file, picked->line, picked->end) : std::string{}, picked,
                [&](std::string_view kind, std::optional<std::size_t> j) {
                    d.librarySub.reset();
                    d.libraryAtlas.reset();
                    chosenOf(kind) = j;
                    d.libraryItem.reset();
                    d.formScroll = 0.0f;
                },
                madeKinds))
            return true;
        const auto event = ui.editList("files", listBox, d.kindScroll,
                                       {.count = rows.size(),
                                        .label = [&](std::size_t i) { return rows[i].file ? names[*rows[i].file] : rows[i].voice ? voiceLabel(voices[*rows[i].voice]) : made[*rows[i].made].def.name; },
                                        .chosen = [&](std::size_t i) { return rows[i].voice ? d.libraryVoice == rows[i].voice
                                                                       : rows[i].file ? !chosen && !d.libraryVoice && d.libraryItem == rows[i].file : chosen == rows[i].made; },
                                        .wrong =
                                            [&](std::size_t i) {
                                                if (rows[i].voice) return false;
                                                if (rows[i].made) return madeWrong(*rows[i].made);
                                                for (std::size_t m = 0; m < made.size(); ++m)
                                                    if (made[m].file == rows[i].file && madeWrong(m)) return true;
                                                return false;
                                            },
                                        .faint = [&](std::size_t i) { return rows[i].voice && !voiceThere(voices[*rows[i].voice]); }, // (a voice not recorded yet)
                                        .renames = [&](std::size_t i) { return rows[i].made || (rows[i].file && !names[*rows[i].file].contains('/')); },
                                        .deletes = [&](std::size_t i) { return !rows[i].voice; }, // (a file: to the Trash, as a script file in the Story's list goes; a line's voice: in its line)
                                        .moves = {},
                                        .drops = {},
                                        .place = {},
                                        .indentStep = 0.0f,
                                        .indent = [&](std::size_t i) { return rows[i].made && made[*rows[i].made].file ? arrowW : 0.0f; },
                                        .fold =
                                            [&](std::size_t i) { // (a file's arrow, by Right and Left; Left on one made from it goes to it)
                                                if (rows[i].file && rows[i].count > 0) return std::ranges::contains(d.libraryOpen, names[*rows[i].file]) ? -1 : 1;
                                                return rows[i].made && made[*rows[i].made].file ? -1 : 0;
                                            },
                                        .paint =
                                            [&](std::size_t i, const SDL_FRect& at) { // (an arrow: open, or not)
                                                const Row& row = rows[i];
                                                const float cy = at.y + at.h / 2;
                                                if (row.file && row.count > 0) {
                                                    const float ax = at.x + 7 * k;
                                                    if (std::ranges::contains(d.libraryOpen, names[*row.file]))
                                                        draw::quad(r, {ax, cy - 3 * k}, {ax + 10 * k, cy - 3 * k}, {ax + 5 * k, cy + 4 * k}, {ax + 5 * k, cy + 4 * k},
                                                                   theme::dim);
                                                    else
                                                        draw::quad(r, {ax + 2 * k, cy - 5 * k}, {ax + 9 * k, cy}, {ax + 2 * k, cy + 5 * k}, {ax + 2 * k, cy + 5 * k},
                                                                   theme::dim);
                                                }
                                                const std::string name = row.file ? names[*row.file] : row.voice ? voiceLabel(voices[*row.voice]) : made[*row.made].def.name;
                                                const bool movie = row.file && isMovie(name);
                                                float nx = at.x + (row.file ? arrowW : 0.0f) + 10 * k;
                                                if (fileKind->id == "images") { // (the image before its name, small, as a list of images shows them; a movie's first image)
                                                    const float side = at.h - 4 * k;
                                                    // (what's made from it: once the scripts read have it)
                                                    if (const Image pic = movie      ? movieThumbnail(engine.gameRoot() + "movies/" + name)
                                                                          : row.file ? engine.assets().file(lower(stemOf(name)))
                                                                          : engine.assets().hasImage(name) ? engine.assets().image(lower(name)) : Image{}) {
                                                        const float s = std::min(side / std::max(1.0f, pic.w()), side / std::max(1.0f, pic.h()));
                                                        pic.draw(r, {nx + (side - pic.w() * s) / 2, cy - pic.h() * s / 2, pic.w() * s, pic.h() * s}, 1.0f);
                                                    }
                                                    nx += side + 8 * k;
                                                }
                                                const bool voiceHere = row.voice && voiceThere(voices[*row.voice]);
                                                const bool hears = fileKind->id == "sounds" && (row.file || voiceHere);
                                                if (hears) // (heard, as a list of sounds lets)
                                                    listenButton(ui, {at.x + at.w - at.h, at.y, at.h, at.h},
                                                                 d.cachedSound(engine.assets(), row.voice ? voices[*row.voice].file : lower(stemOf(name))), true);
                                                const auto dot = name.rfind('.'); // (an MP4, MOV or M4V: Linux players need GStreamer for it)
                                                const bool gstreamer = row.file && dot != npos && std::ranges::contains(kSystemMovieTypes, std::string_view(lower(name.substr(dot))));
                                                // (a voice says so, and what's made from a file what it is; then GStreamer's need, else unused)
                                                std::string note = row.voice                                 ? std::string("voice"_tr)
                                                                   : !row.made                               ? std::string{}
                                                                   : made[*row.made].kind == "atlas"         ? std::string("atlas"_tr)
                                                                   : made[*row.made].kind == "sub-image"     ? std::string("sub-image"_tr)
                                                                                                             : std::string("font"_tr);
                                                if (const std::string_view also = gstreamer ? std::string_view("on Linux: needs GStreamer"_tr) : row.file && d.unused.contains(name) ? std::string_view("unused"_tr) : std::string_view{};
                                                    !also.empty())
                                                    note += (note.empty() ? "" : ", ") + std::string(also);
                                                // (after the name: the name has half the room at least, the note the rest, each cut to fit)
                                                const float room = at.x + at.w - (hears ? at.h : 0.0f) - layout::listPad * k - nx, gap = 8 * k;
                                                const float noteW = note.empty() ? 0.0f : ui.width(note, layout::smallText) + gap;
                                                const SDL_Color colour = row.voice && !voiceHere ? theme::dim : theme::text; // (a voice not recorded yet: faint)
                                                // (a font file's name in it, and a font's made from it in that one)
                                                const SDL_Texture* inFont = fileKind->id != "fontfiles" || row.voice ? nullptr
                                                                            : row.file ? wordsInFont(ui, engine.gameRoot() + std::string(fileKind->folder) + name, {}, name, layout::listText * k)
                                                                                       : fontLabel(ui, name, name, layout::listText * k);
                                                const float shown = inFont ? drawFontWords(ui, inFont, name, nx, cy, std::max(room - noteW, room / 2))
                                                                           : ui.textFitted(name, nx, cy, layout::listText, colour, std::max(room - noteW, room / 2));
                                                if (const float rest = room - shown - gap; !note.empty() && rest >= ui.width("...", layout::smallText) * 2)
                                                    ui.textFitted(note, nx + shown + gap, cy, layout::smallText, gstreamer ? kWarn : theme::dim, rest);
                                            }});
        switch (event.kind) {
        case ListKind::Chosen: {
            const Row& row = rows[event.item];
            if (row.file && row.count > 0 && event.clicked && d.mouseX < listBox.x + arrowW) { // (its arrow clicked: what's made from it shows, or not)
                if (const auto open = std::ranges::find(d.libraryOpen, names[*row.file]); open != d.libraryOpen.end()) d.libraryOpen.erase(open);
                else d.libraryOpen.push_back(names[*row.file]);
            }
            if (row.made != chosen || row.file != d.libraryItem || row.voice != d.libraryVoice) d.formScroll = 0.0f;
            d.libraryItem = row.file;
            d.librarySub.reset();
            d.libraryAtlas.reset();
            if (row.made) chosenOf(made[*row.made].kind) = made[*row.made].index;
            d.libraryVoice = row.voice;
            d.focus.clear();
            break;
        }
        case ListKind::Renamed: // (a file's waits for Enter, below)
            if (const auto m = rows[event.item].made) {
                if (trim(event.name).empty()) return true; // (emptied: it keeps its name)
                const std::string kind(made[*m].kind);
                d.saidRenamed(editor.renameDefinition(kind, made[*m].index, event.name, std::format("def-name:{}:{}", kind, d.focusCount)), renameMistake(kind));
                return true;
            }
            break;
        case ListKind::Deleted:
            if (const auto m = rows[event.item].made) {
                editor.removeDefinition(made[*m].def);
            } else if (rows[event.item].file) { // (the file, to the Trash: the system's, so it can be got back)
                const std::string name = names[*rows[event.item].file], path = std::string(folderOf(name)) + name;
                engine.sound().stopAll(0.0f);
                d.stopMovie();
                d.fontSampleOf.clear();
                d.fontSample.reset();
                if (const auto there = moveToTrash(engine.gameRoot() + path)) {
                    ++d.filesGeneration;
                    if (!there->empty()) editor.movedFiles({{engine.gameRoot() + path, *there}}); // (undo takes it out again)
                    logInfo("{}", "'{}' is in the Trash now (undo brings it back)."_tr(name));
                    d.libraryItem.reset();
                    ++d.checked; // (the previews are made again)
                } else {
                    d.failed = true;
                    d.status = "Can't move '{}' to the Trash."_tr(name);
                }
            }
            d.librarySub.reset();
            d.libraryAtlas.reset();
            return true;
        case ListKind::Opened: // (Right: what's made from it shows)
            if (const auto i = rows[event.item].file; i && !std::ranges::contains(d.libraryOpen, names[*i])) d.libraryOpen.push_back(names[*i]);
            return true;
        case ListKind::Closed: // (Left: hidden again; on one made from a file, to that file first)
            if (const auto m = rows[event.item].made; m && made[*m].file) {
                d.libraryItem = made[*m].file;
                d.librarySub.reset();
                d.libraryAtlas.reset();
                d.formScroll = 0.0f;
            } else if (const auto i = rows[event.item].file) {
                if (const auto open = std::ranges::find(d.libraryOpen, names[*i]); open != d.libraryOpen.end()) d.libraryOpen.erase(open);
            }
            return true;
        case ListKind::Moved:
        case ListKind::None:
            break;
        }
        if (d.entered.starts_with("files:rename:")) {
            const std::size_t row = std::strtoul(d.entered.c_str() + 13, nullptr, 10);
            if (row >= rows.size() || !rows[row].file) return true;
            const std::size_t i = *rows[row].file;
            const std::string to(trim(d.buffer));
            if (to.empty()) return true; // (emptied: it keeps its name)
            if (const auto changed = editor.renameAsset(folderOf(names[i]), names[i], to)) {
                ++d.filesGeneration;
                const auto now = listed();
                const std::string name = to.contains('.') ? to : to + names[i].substr(names[i].rfind('.'));
                const auto at = std::ranges::find_if(now, [&](const std::string& n) { return lower(n) == lower(name); });
                d.libraryItem = at != now.end() ? std::optional(static_cast<std::size_t>(at - now.begin())) : std::nullopt;
                if (const auto open = std::ranges::find(d.libraryOpen, names[i]); open != d.libraryOpen.end()) *open = name;
                logInfo("{}", *changed == 0 ? "Renamed it {}."_tr(name)
                             : *changed == 1 ? "Renamed it {}, and the line that uses it."_tr(name)
                                             : "Renamed it {}, and the {} lines that use it."_tr(name, *changed));
            } else {
                d.failed = true;
                d.status = "A file's name is one word (letters, numbers, _ and -) that no other file there has."_tr;
            }
            return true;
        }
        // Import... (into its folder), and under it into the gallery's or the music room's (their pictures and songs).
        const std::vector<std::string_view> types = typesIn(*fileKind); // (Images: images and movies)
        if (ui.button(f.newRow, "Import..."_tr, false, true, layout::buttonText)) chooseFiles(tr(fileKind->label), types, {});
        if (const std::string_view into = recordFolder == "gallery/" ? "Import to the gallery..."_tr : recordFolder == "music/" ? "Import to the music room..."_tr : std::string_view{};
            !into.empty() && f.secondRow.w > 0.0f && ui.button(f.secondRow, into, false, true, layout::buttonText))
            chooseFiles(tr(fileKind->label), types, std::string(fileKind->folder) + std::string(recordFolder)); // (a movie: movies/gallery/, importFiles)
        return false;
    }
    // The form: the one chosen (an atlas's, a sub-image's or a font's, a voice's, or the file's: a look at it, or a listen).
    if (chosen) drawDefinition(ui, std::string(made[*chosen].kind), made[*chosen].def, right);
    else if (d.libraryVoice) drawVoiceForm(f, voices[*d.libraryVoice]);
    else if (d.libraryItem) drawLibraryFile(f, names[*d.libraryItem]);
    return false;
}

// The file chosen in the Library: what the scripts call it, the Trash, each translation's own, and a look at it
// (or a listen).
void Developer::drawLibraryFile(const LibraryFrame& f, const std::string& name) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    SDL_Renderer* r = f.ui.r;
    const float k = f.ui.k;
    const FileKind* fileKind = kindOfFile(name) ? kindOfFile(name) : f.fileKind; // (its own: an Images list's movie is a movie)
    const SDL_FRect& right = f.right;
    const float buttonH = f.buttonH;
    const float playW = std::max(ui.buttonWidth("Play"_tr, layout::buttonText, layout::wideButtonW), ui.buttonWidth("Stop"_tr, layout::buttonText, layout::wideButtonW));
    const std::string path = std::string(fileKind->folder) + name;
    // What the scripts call it; a translation's own is shown instead of the one it's named after. One in the gallery's
    // or the music room's folder is the game's own, called by its name there (gallery/beach).
    const std::string key = lower(stemOf(name));
    const std::string_view recordFolder = fileKind->id == "images" || fileKind->id == "movies" ? "gallery/" : fileKind->id == "sounds" ? "music/" : "";
    const bool inRecordFolder = !recordFolder.empty() && key.starts_with(recordFolder);
    const auto slash = inRecordFolder ? npos : key.find('/');
    const std::string called = inRecordFolder ? std::string(recordFolder) + scriptName(key.substr(recordFolder.size())) : scriptName(slash == npos ? key : key.substr(slash + 1)); // ("19. YOMI.flac": 19_yomi)
    ui.formTitle(std::format("{}: {}", tr(fileKind->one), name), right.x, right.y, kAccent, right.w);
    float y = right.y + layout::titleH * k;
    // Each translation's own (an image with words on it in its language, sounds in it), to import there: a
    // row for each, saying whether it has one or uses this one.
    auto languages = [&] {
        if (slash != npos || fileKind->id == "fontfiles") return;
        const std::string translations = engine.gameRoot() + "translations/";
        const std::vector<std::string>& files = d.cachedFiles(translations, ".vnscript");
        if (files.empty()) return;
        const bool images = fileKind->id == "images";
        for (const std::string& file : files) {
            const std::string id = stemOf(file), as = std::string(fileKind->folder) + id + "/" + called;
            const auto own = std::ranges::find_if(fileKind->extensions, [&](std::string_view ext) { return d.cachedExists(engine.gameRoot() + as + std::string(ext)); });
            const bool has = own != std::end(fileKind->extensions);
            const float bw = ui.buttonWidth(has ? "Replace..."_tr : "Its own file..."_tr, layout::buttonText, layout::wideButtonW);
            const std::string language = languageLabel(id); // (as the Translations list names it)
            const std::string says = has ? (images ? "{} shows {}"_tr : "{} plays {}"_tr)(language, as + std::string(*own)) : (images ? "{} shows this one"_tr : "{} plays this one"_tr)(language);
            ui.textFitted(says, right.x, y + buttonH / 2, layout::noteText, theme::dim, right.w - bw - 10 * k);
            if (ui.button({right.x + right.w - bw, y, bw, buttonH}, has ? "Replace..."_tr : "Its own file..."_tr, false, true, layout::buttonText))
                chooseFiles(tr(fileKind->label), fileKind->extensions, as);
            y += buttonH + layout::gap * k;
        }
        y += 4 * k;
    };
    if (fileKind->id == "images") {
        const Image pic = engine.assets().file(key);
        // (+ Atlas: its frames, each a look, named as the image while no other visual is, as a character's sheet is; + Sub-image:
        // a part of it with a name of its own, all of it to cut down)
        if (slash == npos && pic) {
            auto taken = [&](const std::string& n) {
                return editor.defines("atlas", n) || editor.defines("sub-image", n) || (n != lower(called) && engine.assets().hasImageFile(n));
            };
            const float subW = ui.buttonWidth("+ Sub-image"_tr, layout::buttonText, layout::widerButtonW), subX = right.x + right.w - subW;
            const float atlasW = ui.buttonWidth("+ Atlas"_tr, layout::buttonText, layout::widerButtonW), atlasX = subX - layout::gap * k - atlasW;
            if (ui.button({atlasX, y, atlasW, buttonH}, "+ Atlas"_tr, false, true, layout::buttonText)) {
                std::string base;
                for (const char c : lower(called)) base += letterOrDigitByte(c) || c == '_' || c == '-' ? c : '_'; // (gallery/beach: gallery_beach)
                d.libraryAtlas = editor.addDefinition("atlas", {std::format("atlas {} image={}", uniqueName(base, taken), called)});
                d.librarySub.reset();
                d.formScroll = 0.0f;
                return;
            }
            if (ui.button({subX, y, subW, buttonH}, "+ Sub-image"_tr, false, true, layout::buttonText)) {
                d.librarySub = editor.addDefinition(
                    "sub-image", {std::format("sub-image {} cut_from={} x=0 y=0 width={} height={}", uniqueName("new_sub_image", taken), called, pic.w(), pic.h())});
                d.libraryAtlas.reset();
                d.formScroll = 0.0f;
                return;
            }
            y += buttonH + 10 * k;
        }
        if (slash == npos) drawRecordTitle(ui, "gallery", called, right.x, right.w, y);
        languages();
        if (pic) {
            const float s = std::min({1.0f, right.w / std::max(1.0f, pic.w()), (right.y + right.h - y) / std::max(1.0f, pic.h())});
            draw::rect(r, {right.x, y, pic.w() * s, pic.h() * s}, {40, 40, 48, 255});
            pic.draw(r, {right.x, y, pic.w() * s, pic.h() * s}, 1.0f);
        }
    } else if (fileKind->id == "sounds") {
        listenButton(ui, {right.x + right.w - playW, y, playW, buttonH}, engine.assets().sound(key));
        y += buttonH + 10 * k;
        if (slash == npos) { // (its two rows: their labels in one column)
            const float labelW = labelColumn(ui, std::round(right.w * layout::labelPart), right.w, {}, {"Title in the music room"_tr, "Caption"_tr});
            drawRecordTitle(ui, "song", called, right.x, right.w, y, labelW);
            drawRecordTitle(ui, "caption", called, right.x, right.w, y, labelW);
        }
        languages();
    } else if (fileKind->id == "movies") {
        // A still of its first image, and Play: it plays there, with its sound (and every other sound waits).
        auto still = [&] {
            d.movie = std::make_unique<Movie>(r, d.silence, engine.gameRoot() + path, false, 1.0f);
            d.movie->showFirst();
            d.movieOf = path;
            d.moviePlaying = false;
        };
        if (d.movieOf != path) still();
        Movie& movie = *d.movie;
        if (!movie) ui.textFitted("It doesn't play: {} says why."_tr(engine.logFile()), right.x, y + buttonH / 2, layout::noteText, theme::dim, right.w - playW - 10 * k);
        d.listenShown = d.listenShown || d.moviePlaying;
        if (ui.button({right.x + right.w - playW, y, playW, buttonH}, d.moviePlaying ? "Stop"_tr : "Play"_tr, d.moviePlaying, static_cast<bool>(movie), layout::buttonText)) {
            if (d.moviePlaying) {
                engine.sound().stopListening();
                still();
            } else {
                d.movie = std::make_unique<Movie>(r, engine.sound(), engine.gameRoot() + path, false, 1.0f);
                engine.sound().listen(path, d.movie->stream());
                d.moviePlaying = d.listenShown = true;
                d.movieTicks = SDL_GetTicks();
            }
        }
        y += buttonH + 10 * k;
        if (slash == npos) drawRecordTitle(ui, "gallery", called, right.x, right.w, y);
        languages();
        if (d.moviePlaying) {
            const Uint64 now = SDL_GetTicks();
            d.movie->update(std::min(0.1f, static_cast<float>(now - d.movieTicks) / 1000.0f));
            d.movieTicks = now;
            if (d.movie->ended()) {
                engine.sound().stopListening();
                still();
            }
        }
        if (SDL_Texture* image = d.movie->texture()) {
            const float s = std::min({1.0f, right.w / std::max(1.0f, d.movie->width()), (right.y + right.h - y) / std::max(1.0f, d.movie->height())});
            const SDL_FRect shown{right.x, y, d.movie->width() * s, d.movie->height() * s};
            SDL_RenderTexture(r, image, nullptr, &shown);
        }
    } else { // a font file: a sample of it, and + Font makes a font from it (a style of it, under a name of its own)
        const float fontW = ui.buttonWidth("+ Font"_tr, layout::buttonText, layout::widerButtonW); // (at the right, as + Atlas and + Sub-image are)
        if (ui.button({right.x + right.w - fontW, y, fontW, buttonH}, "+ Font"_tr, false, true, layout::buttonText)) {
            std::string base;
            for (const char c : lower(called)) base += letterOrDigitByte(c) || c == '_' || c == '-' ? c : '_';
            const std::string as = uniqueName(base, [&](const std::string& n) { return editor.defines("font", n); });
            d.librarySub = editor.addDefinition("font", {std::format("font {} \"{}\"", as, path)});
            d.formScroll = 0.0f;
            return;
        }
        y += buttonH + 10 * k;
        if (d.fontSampleOf != path) {
            d.fontSampleOf = path;
            d.fontSample.reset();
            const std::string sample = "The quick brown fox jumps over the lazy dog."_tr() + "\n0123456789 ?!"; // (every letter, in the tools' language)
            if (const FontPtr font{TTF_OpenFont((engine.gameRoot() + path).c_str(), 30 * k)})
                if (const SurfacePtr text{TTF_RenderText_Blended_Wrapped(font.get(), sample.c_str(), 0,
                                                                         SDL_Color{240, 240, 245, 255}, static_cast<int>(right.w))})
                    d.fontSample.reset(SDL_CreateTextureFromSurface(r, text.get()));
        }
        if (d.fontSample) {
            float tw = 0.0f, th = 0.0f;
            SDL_GetTextureSize(d.fontSample.get(), &tw, &th);
            const SDL_FRect at{right.x, y, tw, th};
            SDL_RenderTexture(r, d.fontSample.get(), nullptr, &at);
        }
    }
}

// Copy, cut, paste and duplicate (their controls' keys: Cmd/Ctrl+C, X, V and D at first) the one chosen in a Library list
// of named lines (colours, styles, animations...; variables; what's made from files), while nothing's typed into and the
// list was clicked last, as the Story's and the Screens' lists do: through the clipboard as script lines, the one pasted
// after the one chosen (none: after the last), under a name no other one has. `kind` is the chosen one's; a list of more
// kinds than one (`pastes`: an image's atlases and sub-images) pastes the one its first word says.
bool Developer::libraryClipboard(const std::string& list, std::string_view kind, const std::string& text, const StoryEditor::Definition* chosen,
                                 const std::function<void(std::string_view, std::optional<std::size_t>)>& choose, std::span<const std::string_view> pastes) {
    DeveloperTools& d = tools;
    if (!d.focus.empty() || d.lastList != list) return false;
    d.acts.cut = d.acts.copy = d.acts.duplicate = !text.empty();
    d.acts.paste = SDL_HasClipboardText();
    for (auto key = d.keys.begin(); key != d.keys.end(); ++key) {
        const char what = clipboardKey(d, *key);
        if (!what) continue;
        d.keys.erase(key);
        if (what == 'c' || what == 'x') {
            if (text.empty()) return true;
            SDL_SetClipboardText(text.c_str());
            d.clipText = text;
            if (what == 'x' && chosen) {
                d.editor.removeDefinition(*chosen);
                choose(kind, std::nullopt);
            }
            return true;
        }
        if (what == 'd' && text.empty()) return true;
        const std::vector<std::string> lines = what == 'd' ? linesOf(text) : clipboardLines();
        std::string_view as = kind;
        if (what != 'd')
            if (const auto first = std::ranges::find_if(lines, [](const std::string& line) { return !blank(line); }); first != lines.end())
                if (const auto said = std::ranges::find(pastes, lower(firstWord(trim(*first)))); said != pastes.end()) as = *said;
        const std::size_t i = d.editor.pasteDefinition(as, chosen, lines);
        if (i < d.editor.definitions(as).size()) choose(as, i);
        return true;
    }
    return false;
}

// The definitions of the kind chosen: listed (renamed, deleted, dragged), and the one chosen as a form.
bool Developer::drawLibraryDefinitions(const LibraryFrame& f, KindPagePart pagePart) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = f.ui.k;
    using ListKind = DeveloperUi::ListEvent::Kind;
    const SDL_FRect& right = f.right;
    const SDL_FRect& listBox = f.listBox;
    const std::string kind = d.library;
    const auto defs = editor.definitions(kind);
    // (the controls built into every game that it hasn't made its own: listed after its own, faint)
    std::vector<std::pair<std::string, std::string>> builtIn; // name, line
    if (kind == "control")
        for (const std::string& line : linesOf(defaultUi()))
            if (lower(firstWord(line)) == "control")
                if (std::string name = lower(firstWord(afterFirst(line))); std::ranges::none_of(defs, [&](const StoryEditor::Definition& def) { return lower(def.name) == name; }))
                    builtIn.emplace_back(std::move(name), line);
    auto own = [&](std::size_t i) { return i < defs.size(); };
    clampIndex(d.libraryItem, defs.size() + builtIn.size());
    const bool picks = kind == "animation"; // (+ New animation: the ones to start from, as + New screen's)
    const DefinitionKind& of = *std::ranges::find(kDefinitionKinds, std::string_view(kind), &DefinitionKind::kind);
    const std::string_view one = tr(of.one); // (as every page's: + New scene, + New screen...)
    const std::string newLabel = "+ New {}"_tr(one == of.one ? lower(one) : std::string(one));
    if (pagePart == KindPagePart::List) { // (the list, then + New ...)
        const std::optional<std::size_t> item = picks && d.pickingAnimation ? std::nullopt : d.libraryItem; // (as the list shows it chosen)
        const StoryEditor::Definition* chosen = item && own(*item) ? &defs[*item] : nullptr;
        const std::string text = chosen ? editor.textOf(chosen->file, chosen->line, chosen->end) : item ? builtIn[*item - defs.size()].second : std::string{};
        if (libraryClipboard("definitions", kind, text, chosen, [&](std::string_view, std::optional<std::size_t> i) {
                d.libraryItem = i;
                d.pickingAnimation = false;
                d.formScroll = 0.0f;
            }))
            return true;
        std::function<void(std::size_t, const SDL_FRect&)> paintColour; // (a colour: as a square before its name, as its box shows it)
        if (kind == "colour")
            paintColour = [&](std::size_t i, const SDL_FRect& at) {
                float x = at.x + layout::listPad * k;
                if (const auto c = parseColour(fieldValue(editor.fields(kind, defs[i]), "Colour"), &engine.palette())) x = ui.swatch(x, at.y + at.h / 2, *c);
                ui.textFitted(defs[i].name, x, at.y + at.h / 2, layout::listText, theme::text, at.x + at.w - layout::listPad * k - x);
            };
        const auto event = ui.editList("definitions", listBox, d.kindScroll,
                                       {.count = defs.size() + builtIn.size(),
                                        .label = [&](std::size_t i) { return own(i) ? defs[i].name : "{} (built in)"_tr(builtIn[i - defs.size()].first); },
                                        .chosen = [&](std::size_t i) { return d.libraryItem == i && !(picks && d.pickingAnimation); },
                                        .wrong =
                                            [&](std::size_t i) { return own(i) && d.problemWithin(defs[i].file, defs[i].line, defs[i].end); },
                                        .faint = [&](std::size_t i) { return !own(i); },
                                        .renames = own,
                                        .deletes = own,
                                        .moves = own,
                                        .drops =
                                            [&](std::size_t moved, std::size_t slot) {
                                                return own(moved) && ((own(slot) && defs[slot].file == defs[moved].file) || (slot > 0 && own(slot - 1) && defs[slot - 1].file == defs[moved].file));
                                            },
                                        .paint = paintColour});
        switch (event.kind) {
        case ListKind::Chosen:
            if (d.libraryItem != event.item || d.pickingAnimation) d.formScroll = 0.0f;
            d.libraryItem = event.item;
            d.pickingAnimation = false;
            d.focus.clear();
            break;
        case ListKind::Renamed:
            if (trim(event.name).empty()) return true; // (emptied: it keeps its name)
            d.saidRenamed(editor.renameDefinition(kind, event.item, event.name, std::format("def-name:{}:{}", kind, d.focusCount)), renameMistake(kind));
            return true;
        case ListKind::Deleted:
            editor.removeDefinition(defs[event.item]);
            d.libraryItem.reset();
            return true;
        case ListKind::Moved:
            if (const auto to = editor.moveDefinition(kind, event.item, event.to)) d.libraryItem = *to;
            return true;
        case ListKind::Opened:
        case ListKind::Closed:
        case ListKind::None:
            break;
        }
        if (ui.button(f.newRow, newLabel, picks && d.pickingAnimation, true, layout::buttonText)) {
            if (picks) {
                d.pickingAnimation = !d.pickingAnimation;
                d.startShownAt = engine.toolsTime();
                d.formScroll = 0.0f;
                d.focus.clear();
                return true;
            }
            auto unique = [&](const std::string& base) { // a name none of them has
                return uniqueName(base, [&](const std::string& n) { return std::ranges::any_of(defs, [&](const StoryEditor::Definition& def) { return lower(def.name) == n; }); });
            };
            std::vector<std::string> lines; // (atlases, sub-images and fonts are added to their files, by + Atlas, + Sub-image and + Font)
            if (kind == "colour") lines = {std::format("colour {} #ffffff", unique("new_colour"))};
            else if (kind == "style") lines = {std::format("style {}", unique("new_style"))};
            else if (kind == "ending") lines = {std::format("ending {}", unique("new_ending"))};
            else if (kind == "achievement") lines = {std::format("achievement {}", unique("new_achievement"))};
            else if (kind == "tip") lines = {std::format("tip {}", unique("new_tip"))};
            else if (kind == "control") lines = {std::format("control {}", unique("new_control"))};
            d.libraryItem = editor.addDefinition(kind, lines);
            d.formScroll = 0.0f;
            if (const auto& made = editor.definitions(kind); *d.libraryItem < made.size()) { // (its own mistakes wait while it's chosen: a control's action still to pick)
                const StoryEditor::Definition& def = made[*d.libraryItem];
                d.freshRow = DeveloperTools::FreshRow{.scene = 0, .path = editor.path(def.file), .from = def.line, .end = def.end, .page = DeveloperTools::Page::Library,
                                                      .library = d.library, .item = d.libraryItem};
                d.showProblems();
            }
            return true;
        }
        return false;
    }
    // The form: + New animation's, else the one chosen's (a built-in control's: to make it the game's own).
    if (picks && d.pickingAnimation) drawNewAnimationForm(ui, right);
    else if (d.libraryItem && !own(*d.libraryItem)) drawBuiltInControl(ui, builtIn[*d.libraryItem - defs.size()].first, builtIn[*d.libraryItem - defs.size()].second, right);
    else if (d.libraryItem) drawDefinition(ui, kind, defs[*d.libraryItem], right);
    return false;
}

// A control built into every game (the next line's, pointing at and pressing what's on a screen, and the developer
// tools' own): what it does and its keys, and a button that makes it the game's own (a copy in its scripts, used
// instead: its keys and what it does can change then).
void Developer::drawBuiltInControl(DeveloperUi& ui, const std::string& name, const std::string& line, const SDL_FRect& right) {
    DeveloperTools& d = tools;
    const float k = ui.k;
    const DefinitionKind& of = *std::ranges::find(kDefinitionKinds, std::string_view("control"), &DefinitionKind::kind);
    float y = right.y;
    ui.formTitle(std::format("{}: {}", tr(of.one), name), right.x, y, kAccent, right.w);
    y += layout::titleH * k;
    // Its keys and its action, as its line has them (as a control's Keys box and the rest say them: made the game's
    // own, they can change).
    std::string keys, action;
    for (const std::string& word : rawWords(line))
        if (lower(word).starts_with("keys=")) keys = unquote(std::string_view(word).substr(5));
        else if (lower(word).starts_with("action=")) action = unquote(std::string_view(word).substr(7));
    const float labelW = labelColumn(ui, std::round(right.w * layout::labelPart), right.w, {}, {"Keys"_tr, "Action"_tr}), boxH = layout::boxH * k;
    for (const auto& [label, value] : {std::pair{std::string_view("Keys"_tr), keys.empty() ? std::string("None"_tr()) : keys}, std::pair{std::string_view("Action"_tr), action}}) {
        ui.textFitted(label, right.x, y + boxH / 2, layout::labelText, theme::dim, labelW - 8 * k);
        ui.textFitted(value, right.x + labelW, y + boxH / 2, layout::boxText, theme::text, right.w - labelW);
        y += boxH + layout::rowGap * k; // (as every form's rows)
    }
    if (ui.button({right.x, y, std::min(right.w, ui.buttonWidth("Make it the game's own"_tr, layout::lineText, layout::widerButtonW)), layout::bigButtonH * k}, "Make it the game's own"_tr)) {
        d.libraryItem = d.editor.addDefinition("control", {line});
        d.formScroll = 0.0f;
        d.focus.clear();
    }
}

// A definition as a form (the Library's), over what it looks like: a style's sample, an atlas's
// frames, an animation playing, a sub-image on the image it's cut from.
void Developer::drawDefinition(DeveloperUi& ui, const std::string& kind, const StoryEditor::Definition& def, const SDL_FRect& right) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    using Field = StoryEditor::Field;
    const auto it = std::ranges::find(kDefinitionKinds, std::string_view(kind), &DefinitionKind::kind);
    const std::string_view label = it != std::end(kDefinitionKinds) ? tr(it->one) : std::string_view(kind); // (in the tools' language)
    // The form (it scrolls when it's longer than the room it has).
    const float toggleW = asTextButton(ui, right.x + right.w, right.y) + layout::gap * k;
    ui.formTitle(std::format("{}: {}", label, def.name), right.x, right.y, kAccent, right.w - toggleW);
    const float boxH = layout::boxH * k, rowGap = layout::rowGap * k;
    float top = right.y + layout::titleH * k;
    if (kind == "style") { // (how it looks, over its form)
        drawStyleSample(ui, def.name, {right.x, top, right.w, std::round(std::min(layout::sampleStyleH * k, right.w * layout::sampleStylePart))}, d.styleSample);
        top += std::round(std::min(layout::sampleStyleH * k, right.w * layout::sampleStylePart)) + 10 * k;
    }
    if (kind == "atlas") { // (its frames, and its eyes and mouth to drag into place)
        drawAtlas(ui, def, {right.x, top, right.w, std::round(std::min(layout::sampleAtlasH * k, right.w * layout::sampleAtlasPart))});
        top += std::round(std::min(layout::sampleAtlasH * k, right.w * layout::sampleAtlasPart)) + 10 * k;
    }
    if (kind == "animation" && !d.asText) // (how it looks, its timeline and the change chosen on it)
        return drawAnimation(ui, def, {right.x, top, right.w, right.y + right.h - top});
    if (kind == "sub-image") { // (the image it's cut from, to cut it out on)
        const float h = std::round(std::min(layout::sampleSubImageH * k, (right.h - layout::titleH * k) * layout::sampleSubImagePart));
        drawSubImage(ui, def, {right.x, top, right.w, h});
        top += h + 10 * k;
    }
    const SDL_FRect formBox{right.x, top, right.w, right.y + right.h - top};
    ui.scrollForm(formBox, boxH + rowGap);
    float y = formBox.y - d.formScroll;
    const DeveloperUi::Clip clipped(ui, formBox);
    for (std::size_t l = def.line; l < def.end; ++l)
        if (const ScriptError* problem = d.problemAt(def.file, l)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
    if (d.asText) { // its line as the script has it (the lines under it, an animation's steps or an atlas's frames, stay as they are)
        const std::string id = std::format("def:{}:{}:raw", kind, def.line);
        auto lines = editor.linesOf(def);
        if (const auto text = drawRawLine(ui, id, lines.empty() ? std::string{} : lines.front(), right.x, right.w, y); text && !lines.empty()) {
            lines.front() = *text;
            editor.setDefinition(def, lines, id + std::to_string(d.focusCount));
        }
        ui.formDrawn(formBox, y);
        return;
    }
    auto fields = editor.fields(kind, def);
    if (kind == "atlas" || kind == "ending" || kind == "achievement" || kind == "tip") // (an empty Image or Title box: named after it)
        showGameSettingValues(fields, engine.gameSettings());
    if (kind == "style" && d.styleSample.view && std::get<1>(d.styleSample.of) == def.name) // (an empty box: what the style it's based on gives it)
        showStyleValues(fields, d.styleSample.view->def().elements.front().style, engine.palette());
    const std::string id = std::format("def:{}:{}", kind, def.line);
    const float least = std::round(right.w * layout::labelPart); // (the labels' column: with a sub-image's title in the gallery under the form, its too)
    const float labelW = kind == "sub-image" ? labelColumn(ui, least, right.w, {fields}, {"Title in the gallery"_tr}) : labelColumn(ui, least, right.w, {fields});
    if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
        if (edit->name == "Name" && edit->typed) { // (renamed: where it's used follows, as a rename in its list does)
            const auto all = editor.definitions(kind);
            const auto at = std::ranges::find_if(all, [&](const StoryEditor::Definition& one) { return one.file == def.file && one.line == def.line; });
            if (at != all.end() && !trim(edit->value).empty())
                d.saidRenamed(editor.renameDefinition(kind, static_cast<std::size_t>(at - all.begin()), std::string(trim(edit->value)), std::format("{}:Name{}", id, d.focusCount)),
                              renameMistake(kind));
        } else if (edit->typed) {
            if (d.focusOwner != id + ":" + edit->name) {
                d.focusOwner = id + ":" + edit->name;
                d.focusFields = std::move(fields);
            }
            std::ranges::find(d.focusFields, edit->name, &Field::name)->value = edit->value;
            editor.setDefinition(def, editor.linesOf(kind, d.focusFields), std::format("{}:{}{}", id, edit->name, d.focusCount));
        } else {
            auto changed = fields;
            std::ranges::find(changed, edit->name, &Field::name)->value = edit->value;
            editor.setDefinition(def, editor.linesOf(kind, changed), {});
            d.focus.clear();
        }
    }
    if (kind == "sub-image") drawRecordTitle(ui, "gallery", lower(def.name), right.x, right.w, y, labelW); // (its title in the gallery, under its form)
    ui.formDrawn(formBox, y);
}

// An image's title in the gallery, a song's in the music room, or a sound's caption (what the player's Captions
// show as it begins): its gallery, song or caption line, made when one is typed, and taken out when it's emptied.
void Developer::drawRecordTitle(DeveloperUi& ui, std::string_view kind, const std::string& name, float x, float w, float& y, float labelW) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const auto defs = editor.definitions(kind);
    const auto def = std::ranges::find_if(defs, [&](const StoryEditor::Definition& one) { return sameName(one.name, name); });
    const bool caption = kind == "caption";
    const std::string_view field = caption ? "Caption" : "Title";
    const std::string title = def != defs.end() ? fieldValue(editor.fields(kind, *def), field) : std::string{};
    std::string none = "None"; // (one in its folder, images/gallery or sounds/music: titled as its file is called)
    if (kind == "gallery" || kind == "song")
        for (const Script::Record& r : folderRecords(kind == "gallery" ? RecordKind::Gallery : RecordKind::Song,
                                                     [&](const std::string& folder) { return d.cachedFiles(engine.gameRoot() + folder, ""); }))
            if (sameName(r.name, name)) none = r.title;
    const std::vector<StoryEditor::Field> fields{{.name = std::string(field),
                                                  .value = title,
                                                  .label = caption ? "Caption"_text : kind == "song" ? "Title in the music room"_text : "Title in the gallery"_text,
                                                  .box = Box::Text,
                                                  .hint = std::move(none),
                                                  .rich = caption}}; // (a caption's words: bold, coloured... as the captions show them)
    if (labelW <= 0.0f) labelW = labelColumn(ui, std::round(w * layout::labelPart), w, {fields});
    if (const auto edit = drawForm(ui, std::format("title:{}:{}", kind, name), fields, x, w, labelW, y)) {
        if (trim(edit->value).empty()) {
            if (def != defs.end()) editor.removeDefinition(*def);
        } else if (def != defs.end()) {
            editor.setDefinition(*def, {std::format("{} {} \"{}\"", kind, name, edit->value)}, std::format("title:{}:{}:{}", kind, name, d.focusCount));
        } else {
            editor.addDefinition(kind, {std::format("{} {} \"{}\"", kind, name, edit->value)});
        }
    }
}

// The Library's variables: the ones the scripts change (or an input or a slider sets), or list with a variable line (and
// while a game is going on, the story's own too). Each has its value then (to change), what it starts
// at and what it's for (its variable line), and the lines that use it. + New variable lists one; a double click
// renames one (Enter: in every line that uses it), and its x takes its variable line out.
bool Developer::drawVariables(const LibraryFrame& f, KindPagePart pagePart) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const SDL_FRect& form = f.right;
    const float k = ui.k;
    const bool playing = engine.mode() == Engine::Mode::Game && engine.game_;
    auto listed = [&] {
        std::vector<std::string> names = editor.variables();
        if (playing)
            for (const auto& [name, value] : engine.game_->variables())
                if (!std::ranges::contains(names, lower(name))) names.push_back(lower(name));
        std::ranges::sort(names);
        return names;
    };
    const std::vector<std::string> names = listed();
    const auto defs = editor.definitions("variable");
    auto definition = [&](const std::string& name) { // its variable line, if it has one
        const auto it = std::ranges::find_if(defs, [&](const StoryEditor::Definition& def) { return lower(def.name) == name; });
        return it != defs.end() ? std::optional(*it) : std::nullopt;
    };
    auto valueOf = [&](const std::string& name) { return toString(engine.game_->variable(name)); };
    clampIndex(d.libraryItem, names.size());
    auto choose = [&](const std::string& name) {
        const auto now = listed();
        const auto at = std::ranges::find(now, name);
        d.libraryItem = at != now.end() ? std::optional(static_cast<std::size_t>(at - now.begin())) : std::nullopt;
    };
    if (pagePart == KindPagePart::List) { // (the list, then + New variable)
        const auto chosen = d.libraryItem ? definition(names[*d.libraryItem]) : std::nullopt;
        if (libraryClipboard("variables", "variable", chosen ? editor.textOf(chosen->file, chosen->line, chosen->end) : std::string{}, chosen ? &*chosen : nullptr,
                             [&](std::string_view, std::optional<std::size_t> i) {
                                 if (i) choose(lower(editor.definitions("variable")[*i].name));
                                 else d.libraryItem.reset();
                             }))
            return true;
        const auto event = ui.editList("variables", f.listBox, d.kindScroll,
                                       {.count = names.size(),
                                        .label = [&](std::size_t i) { return names[i]; },
                                        .chosen = [&](std::size_t i) { return d.libraryItem == i; },
                                        .wrong =
                                            [&](std::size_t i) {
                                                const auto def = definition(names[i]);
                                                return def && d.problemAt(def->file, def->line) != nullptr;
                                            },
                                        .faint = {},
                                        .renames = [&](std::size_t i) { return std::ranges::contains(editor.variables(), names[i]); },
                                        .deletes = [&](std::size_t i) { return definition(names[i]).has_value(); },
                                        .moves = {},
                                        .drops = {},
                                        .place = {},
                                        .indentStep = 0.0f,
                                        .indent = {},
                                        .paint =
                                            [&](std::size_t i, const SDL_FRect& box) {
                                                const float pad = layout::listPad * k, mid = box.y + box.h / 2;
                                                const float nameW = ui.textFitted(names[i], box.x + pad, mid, layout::listText, theme::text, box.w - 2 * pad);
                                                if (!playing) return;
                                                const std::string value = valueOf(names[i]), v = ui.fitted(value, layout::noteText, box.w - 3 * pad - nameW); // (in the room the name leaves, at the right)
                                                const SDL_FPoint vs = ui.measure(v, layout::noteText);
                                                ui.textAround(v, box.x + box.w - pad - vs.x, mid, layout::noteText, theme::dim);
                                                if (v != value) ui.tip({box.x + box.w - pad - vs.x, mid - vs.y / 2, vs.x, vs.y}, value);
                                            }});
        switch (event.kind) {
        case DeveloperUi::ListEvent::Kind::Chosen:
            d.libraryItem = event.item;
            d.usesScroll = 0.0f;
            break;
        case DeveloperUi::ListEvent::Kind::Deleted:
            if (const auto def = definition(names[event.item])) {
                editor.removeDefinition(*def);
                d.libraryItem.reset();
                const auto left = editor.uses(names[event.item]).size();
                logInfo("{}", left == 0 ? "{} is gone."_tr(names[event.item])
                             : left == 1 ? "{}'s variable line is gone; 1 line still uses it."_tr(names[event.item])
                                         : "{}'s variable line is gone; {} lines still use it."_tr(names[event.item], left));
            }
            return true;
        default:
            break;
        }
        if (d.entered.starts_with("variables:rename:")) {
            const std::size_t i = std::strtoul(d.entered.c_str() + 17, nullptr, 10);
            const std::string to = lower(trim(d.buffer));
            if (i >= names.size() || to.empty()) return true; // (emptied: it keeps its name)
            if (const auto uses = editor.renameVariable(names[i], to)) {
                if (playing) engine.game_->setVariable(to, engine.game_->variable(names[i])); // (the game keeps its value under the new name)
                choose(to);
                logInfo("{}", *uses == 0 ? "Renamed it {}."_tr(to)
                             : *uses == 1 ? "Renamed it {}, and the line that uses it."_tr(to)
                                          : "Renamed it {}, and the {} lines that use it."_tr(to, *uses));
            } else {
                d.failed = true;
                d.status = "A variable's name is one word (letters, numbers and _), not a word conditions use (and, or, is...), that no other variable has."_tr;
            }
            return true;
        }
        if (ui.button(f.newRow, "+ New variable"_tr, false, true, layout::buttonText)) { // (its name, ready to type over)
            const std::string name = uniqueName("new_variable", [&](const std::string& n) { return std::ranges::contains(names, n); });
            editor.addDefinition("variable", {std::format("variable {}", name)}); // (it starts at 0: typed into the form when it's something else)
            choose(name);
            return true;
        }
        return false;
    }
    if (!d.libraryItem) return false;
    const std::string& name = names[*d.libraryItem];
    const auto def = definition(name);
    const float toggleW = def ? asTextButton(ui, form.x + form.w, form.y) + layout::gap * k : 0.0f; // (with its variable line: As text, as every definition)
    ui.formTitle("Variable: {}"_tr(name), form.x, form.y, kAccent, form.w - toggleW);
    const float boxH = layout::boxH * k, gap = layout::rowGap * k;
    float y = form.y + layout::titleH * k;
    if (def)
        if (const ScriptError* problem = d.problemAt(def->file, def->line)) y += ui.text(problem->message, form.x, y, layout::noteText, kBad, form.w).y + 8 * k;
    if (def && d.asText) { // its line as the script has it (the lines that use it still listed under it)
        const std::string id = std::format("variable:{}:raw", name);
        const auto lines = editor.linesOf(*def);
        if (const auto text = drawRawLine(ui, id, lines.empty() ? std::string{} : lines.front(), form.x, form.w, y)) editor.setDefinition(*def, {*text}, id + std::to_string(d.focusCount));
    }
    // What it starts at and what it's for: its variable line (made when one is typed).
    std::string start, about;
    if (def) { // variable NAME VALUE "what it's for"
        const auto w = rawWords(afterFirst(editor.linesOf(*def).front()));
        if (w.size() > 1) start = unquote(w[1]);
        if (w.size() > 2) about = unquote(w[2]);
    }
    // (kept between games: a persistent. name; Yes or No renames it so, and every line that uses it)
    const bool kept = name.starts_with("persistent.");
    const std::vector<StoryEditor::Field> fields{{.name = "Starts at", .value = start, .label = "Starting value"_text, .box = Box::VariableValue, .hint = "0"},
                                                 {.name = "What it's for", .value = about, .label = "Description"_text, .box = Box::Text},
                                                 {.name = "Kept between games"_text, .value = kept ? "yes" : "no", .box = Box::YesNo, .choices = "no yes"}};
    const float labelW = labelColumn(ui, std::round(form.w * layout::labelPart), form.w, {fields}, {"Now"_tr}); // (with Now's, under the form)
    if (const auto edit = def && d.asText ? std::nullopt : drawForm(ui, "variable:" + name + ":def", fields, form.x, form.w, labelW, y)) {
        if (edit->name == "Kept between games") {
            const std::string bare = kept ? name.substr(11) : name, to = edit->value == "yes" ? "persistent." + bare : bare;
            if (const auto uses = editor.renameVariable(name, to)) {
                choose(to);
                logInfo("{}", (edit->value == "yes" ? "Kept between games now, as {}: the {} lines that use it follow."_tr : "Kept in a game only now, as {}: the {} lines that use it follow."_tr)(to, *uses));
            } else {
                d.failed = true;
                d.status = "There's a variable called {} already."_tr(to);
            }
            return true;
        }
        std::string value = edit->name == "Starts at" ? edit->value : start, note = edit->name == "What it's for" ? edit->value : about;
        std::string line = "variable " + name;
        if (!trim(value).empty() || !note.empty()) line += " " + (trim(value).empty() ? std::string("0") : scripted(value, editor.variables()));
        if (!note.empty()) line += " \"" + note + "\"";
        if (def) editor.setDefinition(*def, {line}, std::format("variable:{}:{}{}", name, edit->name, d.focusCount));
        else editor.addDefinition("variable", {line});
    }
    if (playing) { // (its value now, while a game is going on: a number, yes or no, or words)
        ui.textFitted("Now"_tr, form.x, y + boxH / 2, layout::labelText, theme::dim, labelW - 8 * k);
        const StoryEditor::Field now{.name = "now", .value = valueOf(name), .box = Box::VariableValue}; // (as every value's box)
        if (const auto typed = ui.pickField("variable:" + name, {form.x + labelW, y, form.w - labelW, boxH}, now, layout::boxText)) {
            const std::string t(trim(*typed));
            const auto n = parseDecimal(t);
            engine.game_->setVariable(name, t == "yes" ? Value(1) : t == "no" ? Value(0) : n ? number(*n) : Value(t));
        }
        y += boxH + gap;
    }
    y += layout::gap * k;
    if (d.lookedUses.what != name || d.lookedUses.version != editor.version()) // (looked for again when the variable or the scripts change)
        d.lookedUses = {.what = name, .version = editor.version(), .found = editor.uses(name)};
    const auto& found = d.lookedUses.found;
    ui.heading(found.size() == 1 ? "Used in 1 line"_tr() : "Used in {} lines"_tr(found.size()), form.x, y, form.w);
    y += layout::headingH * k;
    const ListRoom uses = ui.listRoom({form.x, y, form.w, std::max(0.0f, form.y + form.h - y)}, 0);
    if (const auto clicked = ui.list(uses.list, d.usesScroll, found.size(),
                                     [&](std::size_t i) { return std::format("{}:  {}", editor.placeOf(found[i].file, found[i].line), found[i].text); },
                                     [](std::size_t) { return false; }, [&](std::size_t i) { return d.problemAt(found[i].file, found[i].line) != nullptr; }))
        goTo(editor.path(found[*clicked].file), found[*clicked].line);
    return false;
}

// The lines that play a voice file (their own voice=NAME, or their speaker's voice_files=: sounds/voices/NAME/SCENE_N), as of
// the scripts read: the Sounds list's voices.
const std::vector<DeveloperTools::VoicedLine>& Developer::voicedLines() {
    DeveloperTools& d = tools;
    if (d.voicedVersion != d.checked) { // (the scripts read again)
        d.voicedVersion = d.checked;
        d.voiced.clear();
        if (d.script)
            for (const Instruction& ins : d.script->code)
                if (const auto* say = std::get_if<cmd::Say>(&ins.command); say && d.script->files[ins.file].starts_with("scripts/"))
                    if (std::string file = d.script->voiceOf(*say); !file.empty() && !file.contains('{')) {
                        const CharacterDef* def = say->character.empty() ? nullptr : d.script->character(say->character);
                        d.voiced.push_back({std::move(file), def ? def->name : say->speaker, say->text, d.script->files[ins.file], ins.line});
                    }
    }
    return d.voiced;
}

// A line's voice, chosen in Sounds: the file it plays and whether it's there (Import... or Replace...), each translation's
// own, Play, and Go to the line.
void Developer::drawVoiceForm(const LibraryFrame& f, const DeveloperTools::VoicedLine& v) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    const SDL_FRect& form = f.right;
    const float k = ui.k;
    auto there = [&](const std::string& file, const std::string& language) { // (in the language's folder, if one's given)
        return std::ranges::any_of(kSoundTypes, [&](std::string_view ext) { return d.cachedExists(engine.gameRoot() + "sounds/" + language + file + std::string(ext)); });
    };
    ui.formTitle("Voice: {}"_tr("sounds/" + v.file), form.x, form.y, kAccent, form.w);
    float y = form.y + layout::titleH * k;
    y += ui.text(v.who.empty() ? v.text : std::format("{}: {}", v.who, v.text), form.x, y, layout::listText, theme::text, form.w).y + 10 * k;
    const float buttonH = layout::buttonH * k;
    const bool here = there(v.file, "");
    const float importW = ui.buttonWidth(here ? "Replace..."_tr : "Import..."_tr, layout::buttonText, layout::wideButtonW);
    if (ui.button({form.x + form.w - importW, y, importW, buttonH}, here ? "Replace..."_tr : "Import..."_tr, false, true, layout::buttonText))
        chooseFiles("Sounds"_tr, kSoundTypes, "sounds/" + v.file);
    y += buttonH + layout::gap * k;
    const std::string translations = engine.gameRoot() + "translations/";
    const std::vector<std::string>& languages = d.cachedFiles(translations, ".vnscript"); // (each translation's own)
    for (const std::string& language : languages) {
        const std::string id = stemOf(language);
        const bool translated = there(v.file, id + "/");
        const float ownW = ui.buttonWidth(translated ? "Replace..."_tr : "Its own file..."_tr, layout::buttonText, layout::wideButtonW);
        ui.textFitted(translated ? "{} plays {}"_tr(languageLabel(id), "sounds/" + id + "/" + v.file) : "{} plays this one"_tr(languageLabel(id)), form.x, y + buttonH / 2, layout::noteText,
                      theme::dim, form.w - ownW - 10 * k);
        if (ui.button({form.x + form.w - ownW, y, ownW, buttonH}, translated ? "Replace..."_tr : "Its own file..."_tr, false, true, layout::buttonText))
            chooseFiles("Sounds"_tr, kSoundTypes, "sounds/" + id + "/" + v.file);
        y += buttonH + layout::gap * k;
    }
    y += 8 * k;
    const float playW = std::max(ui.buttonWidth("Play"_tr, layout::buttonText, layout::wideButtonW), ui.buttonWidth("Stop"_tr, layout::buttonText, layout::wideButtonW));
    if (here) listenButton(ui, {form.x, y, playW, buttonH}, engine.assets().sound(v.file));
    if (ui.button({form.x + (here ? playW + layout::gap * k : 0.0f), y, ui.buttonWidth("Go to the line"_tr, layout::buttonText, layout::widerButtonW), buttonH}, "Go to the line"_tr, false, true, layout::buttonText))
        goTo(v.path, static_cast<std::size_t>(std::max(v.line - 1, 0)));
}

// A style's sample: a button and some words in it, as the game draws them (pointing at the button
// shows how it looks then).
void Developer::drawStyleSample(DeveloperUi& ui, const std::string& style, const SDL_FRect& box, DeveloperTools::StyleSample& kept, float across) {
    DeveloperTools& d = tools;
    const float wide = across > 0.0f ? across : engine.canvas().w, h = wide * box.h / box.w; // (in the game's pixels: as tall as the box is for its width)
    if (kept.of != std::tuple{d.editor.version(), style, wide}) { // the scripts, with a screen of them at the end of main.vnscript
        kept.view.reset();
        kept.script.reset();
        kept.of = {d.editor.version(), style, wide};
        const float bh = std::min(h * 0.7f, 90.0f);
        const std::vector<std::string> pixels{ // (laid out in the game's pixels, in the band the box shows; written in places, as screens are)
            "screen vnext_style_sample",
            // (a button doing nothing: pointed at, it looks so)
            std::format("    text \"A button\" style={} clickable=yes x={} y={} width={} height={}", style, std::lround(wide * 0.04f), std::lround((h - bh) / 2),
                        std::lround(wide * 0.4f), std::lround(bh)),
            std::format("    text \"Some words\" style={} x={} y={} width={}", style, std::lround(wide * 0.5f), std::lround(h / 2 - 16), std::lround(wide * 0.46f))};
        std::string sample = "\n";
        for (const std::string& line : placesFromPixels(pixels, engine.canvas().w, engine.canvas().h, ownSizes(pixels, {}, engine.canvas().w, engine.canvas().h, 1.0f)))
            sample += line + "\n";
        auto script = std::make_unique<Script>(Script::load(
            "scripts/main.vnscript",
            [&](const std::string& p) {
                auto text = readTextFile(engine.gameRoot() + p);
                if (text && lower(p) == "scripts/main.vnscript") *text += sample;
                return text;
            },
            defaultUi()));
        if (const auto it = script->screens.find("vnext_style_sample"); it != script->screens.end()) {
            kept.view = std::make_unique<ScreenView>(it->second, engine, true);
            kept.script = std::move(script);
        }
    }
    draw::rect(ui.r, box, kBox);
    if (!kept.view) return;
    const float scale = box.w / wide;
    const SDL_Event e = mouseMotionAt(ui.over(box) ? (d.mouseX - box.x) / scale : -1000.0f, ui.over(box) ? (d.mouseY - box.y) / scale : -1000.0f);
    {
        const SetFor _(engine.sampling_, true); // (pointing at it only)
        kept.view->handleEvent(e);
    }
    if (paintCanvas([&] { kept.view->draw(engine.renderer()); })) {
        const SDL_FRect part{0.0f, 0.0f, wide, h};
        SDL_RenderTexture(ui.r, d.canvas.get(), &part, &box);
    }
    draw::outline(ui.r, box, kEdge);
}

} // namespace vn
