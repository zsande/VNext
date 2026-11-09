// The Library page: the game's files by kind (and files brought in), what the scripts list by name (colours, styles,
// endings...), each with its form, the variables, the voices, and the game's settings (its translations are
// LibraryTranslations.cpp's, its animations LibraryAnimations.cpp's, atlases and sub-images LibraryImages.cpp's).
#include "DeveloperUi.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace vn {

namespace {

// What a rename of a Library definition that can't be says (an ending's, an achievement's or a tip's name is kept in a
// variable's: no -).
std::string_view renameMistake(std::string_view kind) {
    return kind == "ending" || kind == "achievement" || kind == "tip" ? std::string_view("A name is one word (letters, numbers and _) that no other one of these has."_tr)
                                                                       : std::string_view("A name is one word (letters, numbers, _ and -) that no other one of these has."_tr);
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
    for (const auto& [from, as] : files) {
        const auto slash = from.find_last_of("/\\");
        std::string name = from.substr(slash == std::string::npos ? 0 : slash + 1);
        auto dot = name.rfind('.');
        const std::string ext = dot == std::string::npos ? std::string() : lower(name.substr(dot));
        const std::string_view folder = folderOf(ext);
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
        if (as.empty()) {
            SDL_CreateDirectory((engine.gameRoot() + std::string(folder)).c_str());
            const std::string stem = uniqueName(name.substr(0, dot), [&](const std::string& n) { return fileExists(engine.gameRoot() + std::string(folder) + n + name.substr(dot)); });
            to = engine.gameRoot() + std::string(folder) + stem + name.substr(dot);
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
            if (folder == "images/" && firstImage.empty()) firstImage = last.substr(7, last.rfind('.') - 7);
            moves.emplace_back(std::string{}, to);
            if (as.empty()) {
                d.library = folder == "images/" ? "images" : folder == "sounds/" ? "sounds" : folder == "movies/" ? "movies" : "fontfiles";
                d.libraryItem.reset();
                d.librarySub.reset();
            }
        }
    }
    d.editor.movedFiles(std::move(moves));
    if (copied > 0) {
        logInfo("{}", copied == 1 ? "Imported '{}'."_tr(last) : "Imported {} files."_tr(copied));
    } else {
        d.failed = true;
        d.status = !skipped.empty() ? "'{}' isn't an image, sound, movie or font file the game can use there."_tr(skipped) : "Can't copy that into the game folder."_tr();
    }
    // An image dropped on the screen open in the Screens page: a part showing it, where it was dropped
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
    // clock and calendar), no clock with days and parts of the day.
    const std::string timeKind = lower(editor.setting("time").value_or("none"));
    auto shown = [&](const Setting& s) {
        if (s.name == "time_start") return timeKind == "story" || timeKind == "days";
        if (s.name == "clock") return timeKind == "story" || timeKind == "real";
        if (s.name == "time_parts" || s.name == "phone_follows_time" || s.name == "backgrounds_follow_time") return timeKind != "none";
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
    // What an empty box means, as the other settings have it now: the time's start for its kind and clock, the
    // parts of the day on the clock chosen, and the phone's clock and date once it follows the time. Else the
    // setting's own default.
    const bool clock12 = editor.setting("clock").value_or("24") == "12";
    const bool phoneFollows = timeKind != "none" && lower(editor.setting("phone_follows_time").value_or("yes")) != "no";
    auto clockWords = [&](int minutes) {
        const int hours = minutes / 60, m = minutes % 60;
        return clock12 ? std::format("{}:{:02} {}", hours % 12 == 0 ? 12 : hours % 12, m, hours < 12 ? "AM" : "PM") : std::format("{}:{:02}", hours, m);
    };
    auto hintOf = [&](const Setting& s) -> std::string {
        if (s.name == "time_start") {
            if (timeKind == "days") { // (the first part of the day)
                const std::string parts = unquote(editor.setting("time_parts").value_or("morning"));
                return std::string(firstWord(parts));
            }
            return clockWords(8 * 60);
        }
        if (s.name == "time_parts" && clock12) return std::format("morning {}, afternoon {}, evening {}, night {}", clockWords(6 * 60), clockWords(12 * 60), clockWords(18 * 60), clockWords(22 * 60));
        if (s.name == "phone_clock" && phoneFollows) return std::string("The game's time"_hint);
        if (s.name == "phone_date" && phoneFollows) return std::string("The game's date"_hint);
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
            const std::string id = std::format("setting:{}", s.name);
            const bool percent = StoryEditor::kindOf(s.box).percent; // (shown and written as one, as everywhere)
            StoryEditor::Field f{.name = std::string(s.name), .value = written ? percent ? percentText(*written) : *written : std::string{}, .box = s.box,
                                 .hint = hintOf(s), .suggest = std::string(s.suggest)};
            applyBox(f);
            if (const auto value = ui.pickField(id, field, f, layout::boxText)) {
                std::string v = *value;
                if (s.quoted) std::erase(v, '"');
                if (percent && !v.empty()) v = percentText(v);
                if (s.name == "game_size" && !d.sizeFrom) d.sizeFrom = written.value_or(std::string(s.standard)); // (see drawDeveloper)
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
        if (problem) draw::rect(r, {right.x, top, layout::markW * k, boxH}, kBad);
        if (problem) ui.textFitted(problem->message, problemX, middle, layout::smallText, kBad, problemW);
    }
    ui.formDrawn(formBox, y);
}

// The Library page as it is this frame: the kind of file (or definition) listed, and where its list, the row of
// buttons under it and the form go (in the room their page parts were given). Its files are drawn by drawLibraryFiles
// (the one chosen by drawLibraryFile), its definitions by drawLibraryDefinitions, its variables and voiced lines by
// drawVariables and drawVoices: each its list or its form.
struct Developer::LibraryFrame {
    DeveloperUi& ui;
    DeveloperTools& d;
    const FileKind* fileKind; // (none: definitions are listed)
    SDL_FRect listBox, newRow, right;
    float buttonH, listGap;

    // A new definition: its name, ready to type, in its form's Name box.
    void typeName(std::string_view ofKind, std::size_t index) const {
        const StoryEditor::Definition made = d.editor.definitions(ofKind)[index];
        d.formScroll = 0.0f;
        d.focusOn(std::format("def:{}:{}:Name", ofKind, made.line), made.name, true); // (all of it chosen: typed over, as every new one's)
    }
};

// The Library page: the game's images, sounds, voices, movies and font files (to look at, hear, import
// and rename; an image's sub-images and a font file's fonts under it), what the scripts list by name
// (colours, styles, atlases, animations, endings, achievements, tips, controls), each as a form, the
// variables, the translations and the game's settings.
void Developer::drawLibrary(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    // Its columns, side by side: the Library, the kind chosen (as wide: what's in it, under its heading), and the form
    // (the rest, for what it shows: an image, an atlas, an animation).
    const SDL_FRect left = ui.columns(area).left, column{left.x + left.w, left.y, left.w, left.h};
    const SDL_FRect right{column.x + column.w, left.y, std::max(0.0f, area.x + area.w - column.x - column.w), left.h};

    // --- What's listed, in the order a game is made in: its pictures (images, the atlases cut from them, movies),
    // its sounds and voices, how its words and screens look and move, what it keeps track of, its keys, its
    // translations, and its settings (main.vnscript's, once there is one).
    static constexpr std::string_view kOrder[]{"images", "atlas", "movies", "sounds", "voices", "fontfiles", "colour", "style", "animation",
                                               "variables", "ending", "achievement", "tip", "control", "translations", "settings"};
    std::vector<std::pair<std::string_view, std::string_view>> kinds; // id, label
    for (const std::string_view id : kOrder) {
        if (id == "settings" && !editor.hasMain()) continue;
        if (const auto file = std::ranges::find(kFileKinds, id, &FileKind::id); file != std::end(kFileKinds)) kinds.emplace_back(id, file->label);
        else if (const auto def = std::ranges::find(kDefinitionKinds, id, &DefinitionKind::kind); def != std::end(kDefinitionKinds)) kinds.emplace_back(id, def->label);
        else
            kinds.emplace_back(id, id == "voices"       ? std::string_view("Voices"_text)
                                   : id == "variables"  ? std::string_view("Variables"_text)
                                   : id == "translations" ? std::string_view("Translations"_text)
                                                          : std::string_view("Settings"_text));
    }
    if (std::ranges::none_of(kinds, [&](const auto& kind) { return kind.first == d.library; })) d.library = kinds.front().first;
    auto wrongIn = [&](std::string_view kind) { // (images and fonts: what's made from them)
        if (kind == "settings") return std::ranges::any_of(d.problems, [&](const ScriptError& e) { return inSettings(editor, e); });
        const std::string_view of = kind == "variables" ? "variable" : kind == "images" ? "image" : kind == "fontfiles" ? "font" : kind;
        return std::ranges::any_of(editor.definitions(of), [&](const StoryEditor::Definition& def) { return d.problemWithin(def.file, def.line, def.end); });
    };
    const auto fileKind = std::ranges::find(kFileKinds, std::string_view(d.library), &FileKind::id);
    const bool files = fileKind != std::end(kFileKinds);
    LibraryFrame f{.ui = ui, .d = d, .fileKind = files ? fileKind : nullptr, .listBox = {}, .newRow = {}, .right = right, .buttonH = layout::buttonH * k,
                   .listGap = layout::gap * k};
    // The kind chosen's list, or its form, as that kind has them.
    auto drawKind = [&](KindPagePart pagePart) {
        if (d.library == "variables") return drawVariables(f, pagePart);
        if (d.library == "voices") return drawVoices(f, pagePart);
        return files ? drawLibraryFiles(f, pagePart) : drawLibraryDefinitions(f, pagePart);
    };
    // Its page parts: the Library (the kinds), the kind chosen (what's in it, a button under it but for the voiced lines;
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
        listPagePart(ui, "kind", kind != kinds.end() ? std::string(tr(kind->second)) : std::string{}, column, d.library == "voices" ? 0 : 1,
                  [&](const ListRoom& at) {
                      f.listBox = at.list;
                      f.newRow = at.rows.empty() ? SDL_FRect{} : at.rows[0];
                      return drawKind(KindPagePart::List);
                  }),
        formPagePart(ui, "form", "Form"_tr, right, d.libraryItem || d.librarySub || d.pickingAnimation, [&](const SDL_FRect& box) {
            f.right = box;
            return drawKind(KindPagePart::Form);
        })};
    drawPageParts(ui, "library", pageParts);
}

// The files of the kind chosen: listed (the folder's, then the translations' own: images/spanish/...),
// renamed (a double click, then Enter: the lines that use it follow) and imported (or dropped on the
// window). An image's sub-images and a font file's fonts are listed under it, when its arrow opens them.
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
    auto listed = [&] {
        std::vector<std::string> names, translated;
        const std::string folder = engine.gameRoot() + std::string(fileKind->folder);
        for (const std::string_view ext : fileKind->extensions)
            for (const std::string& name : d.cachedFiles(folder, ext)) names.push_back(name);
        for (const std::string& language : d.cachedFiles(engine.gameRoot() + "translations/", ".vnscript"))
            for (const std::string_view ext : fileKind->extensions)
                for (const std::string& name : d.cachedFiles(folder + stemOf(language) + "/", ext))
                    translated.push_back(stemOf(language) + "/" + name);
        std::ranges::sort(names);
        std::ranges::sort(translated);
        names.insert(names.end(), translated.begin(), translated.end());
        return names;
    };
    const std::vector<std::string> names = listed();
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
            if (!names[i].contains('/') && lower(scriptName(names[i].substr(0, names[i].rfind('.')))) == d.libraryFind) d.libraryItem = i;
        d.libraryFind.clear();
    }
    clampIndex(d.libraryItem, names.size());
    // What's made from them: sub-images (image lines) and fonts (font lines), each under the
    // file it's from (one from a file that isn't there: at the end, on its own).
    const std::string subKind = fileKind->id == "images" ? "sub-image" : fileKind->id == "fontfiles" ? "font" : "";
    const auto subs = subKind.empty() ? std::vector<StoryEditor::Definition>{} : editor.definitions(subKind);
    clampIndex(d.librarySub, subs.size());
    std::vector<std::optional<std::size_t>> source; // (each one's file)
    for (const StoryEditor::Definition& def : subs) {
        const auto fields = editor.fields(subKind, def);
        const auto from = std::ranges::find(fields, subKind == "sub-image" ? "Cut from" : "File", &Field::name);
        const std::string what = from == fields.end() ? std::string{} : lower(unquote(from->value));
        std::optional<std::size_t> at;
        for (std::size_t i = 0; i < names.size() && !at; ++i)
            if (!names[i].contains('/') && (subKind == "sub-image" ? lower(scriptName(names[i].substr(0, names[i].rfind('.')))) == what
                                                                 : lower(std::string(fileKind->folder) + names[i]) == what))
                at = i;
        source.push_back(at);
    }
    if (d.librarySub && source[*d.librarySub]) // (the one chosen shows)
        if (const std::string& of = names[*source[*d.librarySub]]; !std::ranges::contains(d.libraryOpen, of)) d.libraryOpen.push_back(of);
    struct Row {
        std::optional<std::size_t> file, sub;
        std::size_t made = 0; // (a file: how many are made from it)
    };
    std::vector<Row> rows;
    for (std::size_t i = 0; i < names.size(); ++i) {
        rows.push_back({.file = i, .sub = std::nullopt, .made = static_cast<std::size_t>(std::ranges::count(source, std::optional(i)))});
        if (rows.back().made > 0 && std::ranges::contains(d.libraryOpen, names[i]))
            for (std::size_t j = 0; j < subs.size(); ++j)
                if (source[j] == i) rows.push_back({.file = std::nullopt, .sub = j});
    }
    for (std::size_t j = 0; j < subs.size(); ++j)
        if (!source[j]) rows.push_back({.file = std::nullopt, .sub = j});
    auto subWrong = [&](std::size_t j) { return d.problemWithin(subs[j].file, subs[j].line, subs[j].end); };
    const bool arrows = std::ranges::any_of(rows, [](const Row& row) { return row.made > 0; }); // (room for them only when there's one)
    const float arrowW = subKind.empty() || !arrows ? 0.0f : 22 * k;
    if (pagePart == KindPagePart::List) { // (the list, then Import...)
        const StoryEditor::Definition* sub = d.librarySub ? &subs[*d.librarySub] : nullptr; // (copied, pasted and duplicated as the other lists' are; a file isn't)
        if (!subKind.empty() && libraryClipboard("files", subKind, sub ? editor.textOf(sub->file, sub->line, sub->end) : std::string{}, sub, [&](std::optional<std::size_t> j) {
                d.librarySub = j;
                d.libraryItem.reset();
                d.formScroll = 0.0f;
            }))
            return true;
        const auto event = ui.editList("files", listBox, d.kindScroll,
                                       {.count = rows.size(),
                                        .label = [&](std::size_t i) { return rows[i].file ? names[*rows[i].file] : subs[*rows[i].sub].name; },
                                        .chosen = [&](std::size_t i) { return rows[i].file ? !d.librarySub && d.libraryItem == rows[i].file : d.librarySub == rows[i].sub; },
                                        .wrong =
                                            [&](std::size_t i) {
                                                if (rows[i].sub) return subWrong(*rows[i].sub);
                                                for (std::size_t j = 0; j < subs.size(); ++j)
                                                    if (source[j] == rows[i].file && subWrong(j)) return true;
                                                return false;
                                            },
                                        .faint = {},
                                        .renames = [&](std::size_t i) { return rows[i].sub || !names[*rows[i].file].contains('/'); },
                                        .deletes = [](std::size_t) { return true; }, // (a file: to the Trash, as a script file in the Story's list goes)
                                        .moves = {},
                                        .drops = {},
                                        .place = {},
                                        .indentStep = 0.0f,
                                        .indent = [&](std::size_t i) { return rows[i].sub && source[*rows[i].sub] ? arrowW : 0.0f; },
                                        .paint =
                                            [&](std::size_t i, const SDL_FRect& at) { // (an arrow: open, or not)
                                                const Row& row = rows[i];
                                                const float cy = at.y + at.h / 2;
                                                if (row.file && row.made > 0) {
                                                    const float ax = at.x + 7 * k;
                                                    if (std::ranges::contains(d.libraryOpen, names[*row.file]))
                                                        draw::quad(r, {ax, cy - 3 * k}, {ax + 10 * k, cy - 3 * k}, {ax + 5 * k, cy + 4 * k}, {ax + 5 * k, cy + 4 * k},
                                                                   theme::dim);
                                                    else
                                                        draw::quad(r, {ax + 2 * k, cy - 5 * k}, {ax + 9 * k, cy}, {ax + 2 * k, cy + 5 * k}, {ax + 2 * k, cy + 5 * k},
                                                                   theme::dim);
                                                }
                                                const std::string& name = row.file ? names[*row.file] : subs[*row.sub].name;
                                                float nx = at.x + (row.file ? arrowW : 0.0f) + 10 * k;
                                                if (fileKind->id == "images") { // (the image before its name, small, as a list of images shows them)
                                                    const float side = at.h - 4 * k;
                                                    if (const Image pic = row.file ? engine.assets().file(lower(stemOf(name))) : engine.assets().image(lower(name))) {
                                                        const float s = std::min(side / std::max(1.0f, pic.w()), side / std::max(1.0f, pic.h()));
                                                        pic.draw(r, {nx + (side - pic.w() * s) / 2, cy - pic.h() * s / 2, pic.w() * s, pic.h() * s}, 1.0f);
                                                    }
                                                    nx += side + 8 * k;
                                                }
                                                const bool hears = fileKind->id == "sounds" && row.file;
                                                if (hears) // (heard, as a list of sounds lets)
                                                    listenButton(ui, {at.x + at.w - at.h, at.y, at.h, at.h}, d.cachedSound(engine.assets(), lower(stemOf(name))), true);
                                                const auto dot = name.rfind('.'); // (an MP4, MOV or M4V: Linux players need GStreamer for it)
                                                const bool gstreamer = row.file && dot != npos && std::ranges::contains(kSystemMovieTypes, std::string_view(lower(name.substr(dot))));
                                                const std::string_view note = gstreamer ? std::string_view("on Linux: needs GStreamer"_tr) : row.file && d.unused.contains(name) ? std::string_view("unused"_tr) : std::string_view{};
                                                // (after the name: the name has half the room at least, the note the rest, each cut to fit)
                                                const float room = at.x + at.w - (hears ? at.h : 0.0f) - layout::listPad * k - nx, gap = 8 * k;
                                                const float noteW = note.empty() ? 0.0f : ui.width(note, layout::smallText) + gap;
                                                const float shown = ui.textFitted(name, nx, cy, layout::listText, theme::text, std::max(room - noteW, room / 2));
                                                if (const float rest = room - shown - gap; !note.empty() && rest >= ui.width("...", layout::smallText) * 2)
                                                    ui.textFitted(note, nx + shown + gap, cy, layout::smallText, gstreamer ? kWarn : theme::dim, rest);
                                            }});
        switch (event.kind) {
        case ListKind::Chosen: {
            const Row& row = rows[event.item];
            if (row.file && row.made > 0 && d.mouseX < listBox.x + arrowW) { // (its arrow: what's made from it shows, or not)
                if (const auto open = std::ranges::find(d.libraryOpen, names[*row.file]); open != d.libraryOpen.end()) d.libraryOpen.erase(open);
                else d.libraryOpen.push_back(names[*row.file]);
            }
            if (row.sub != d.librarySub || row.file != d.libraryItem) d.formScroll = 0.0f;
            d.libraryItem = row.file;
            d.librarySub = row.sub;
            d.focus.clear();
            break;
        }
        case ListKind::Renamed: // (a file's waits for Enter, below)
            if (const auto j = rows[event.item].sub) {
                if (trim(event.name).empty()) return true; // (emptied: it keeps its name)
                d.saidRenamed(editor.renameDefinition(subKind, *j, event.name, std::format("def-name:{}:{}", subKind, d.focusCount)), renameMistake(subKind));
                return true;
            }
            break;
        case ListKind::Deleted:
            if (const auto j = rows[event.item].sub) {
                editor.removeDefinition(subs[*j]);
            } else { // (the file, to the Trash: the system's, so it can be got back)
                const std::string name = names[*rows[event.item].file], path = std::string(fileKind->folder) + name;
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
            if (const auto changed = editor.renameAsset(fileKind->folder, names[i], to)) {
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
        if (ui.button(f.newRow, "Import..."_tr, false, true, layout::buttonText))
            chooseFiles(tr(fileKind->label), fileKind->extensions, {});
        return false;
    }
    // The form: the one chosen (a sub-image's or font's, or the file's: a look at it, or a listen).
    if (d.librarySub) drawDefinition(ui, subKind, subs[*d.librarySub], right);
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
    const FileKind* fileKind = f.fileKind;
    const SDL_FRect& right = f.right;
    const float buttonH = f.buttonH;
    const float playW = std::max(ui.buttonWidth("Play"_tr, layout::buttonText, layout::wideButtonW), ui.buttonWidth("Stop"_tr, layout::buttonText, layout::wideButtonW));
    const std::string path = std::string(fileKind->folder) + name;
    // What the scripts call it; a translation's own is shown instead of the one it's named after.
    const std::string key = lower(stemOf(name));
    const auto slash = key.find('/');
    const std::string called = scriptName(slash == npos ? key : key.substr(slash + 1)); // ("19. YOMI.flac": 19_yomi)
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
        if (slash == npos && pic) { // (+ Sub-image: a part of it with a name of its own; all of it, to cut down)
            const float subW = ui.buttonWidth("+ Sub-image"_tr, layout::buttonText, layout::widerButtonW);
            if (ui.button({right.x + right.w - subW, y, subW, buttonH}, "+ Sub-image"_tr, false, true, layout::buttonText)) {
                const std::string as = uniqueName("new_sub_image", [&](const std::string& n) { return editor.defines("sub-image", n) || engine.assets().hasImageFile(n); });
                d.librarySub = editor.addDefinition("sub-image", {std::format("sub-image {} cut_from={} x=0 y=0 width={} height={}", as, called, pic.w(), pic.h())});
                f.typeName("sub-image", *d.librarySub);
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
        if (ui.button({right.x, y, ui.buttonWidth("+ Font"_tr, layout::buttonText, layout::widerButtonW), buttonH}, "+ Font"_tr, false, true, layout::buttonText)) {
            std::string base;
            for (const char c : lower(called)) base += letterOrDigitByte(c) || c == '_' || c == '-' ? c : '_';
            const std::string as = uniqueName(base, [&](const std::string& n) { return editor.defines("font", n); });
            d.librarySub = editor.addDefinition("font", {std::format("font {} \"{}\"", as, path)});
            f.typeName("font", *d.librarySub);
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
// of named lines (colours, styles, animations...; variables), while nothing's typed into and the list was clicked last,
// as the Story's and the Screens' lists do: through the clipboard as script lines, the one pasted after the one chosen
// (none: after the last), under a name no other one has.
bool Developer::libraryClipboard(const std::string& list, std::string_view kind, const std::string& text, const StoryEditor::Definition* chosen,
                                 const std::function<void(std::optional<std::size_t>)>& choose) {
    DeveloperTools& d = tools;
    if (!d.focus.empty() || d.lastList != list) return false;
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
                choose(std::nullopt);
            }
            return true;
        }
        if (what == 'd' && text.empty()) return true;
        const std::size_t i = d.editor.pasteDefinition(kind, chosen, what == 'd' ? linesOf(text) : clipboardLines());
        if (i < d.editor.definitions(kind).size()) choose(i);
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
        if (libraryClipboard("definitions", kind, text, chosen, [&](std::optional<std::size_t> i) {
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
            std::vector<std::string> lines; // (sub-images and fonts are added to their files, by + Sub-image and + Font)
            if (kind == "colour") lines = {std::format("colour {} #ffffff", unique("new_colour"))};
            else if (kind == "style") lines = {std::format("style {}", unique("new_style"))};
            else if (kind == "atlas") { // (cut from the first image no atlas is cut from yet, else from one called as it is; its frames' names: typed into the form's)
                auto imageOf = [&](const StoryEditor::Definition& atlas) { // (its image=, else its name)
                    const auto written = editor.linesOf(atlas);
                    for (const std::string& w : written.empty() ? std::vector<std::string>{} : rawWords(written.front()))
                        if (lower(w).starts_with("image=")) return lower(unquote(w.substr(6)));
                    return lower(atlas.name);
                };
                std::string image;
                for (const std::string& name : editor.names(StoryEditor::Pick::Images))
                    if (!editor.defines("sub-image", name) && std::ranges::none_of(defs, [&](const auto& atlas) { return imageOf(atlas) == lower(name); })) {
                        image = name;
                        break;
                    }
                lines = {std::format("atlas {}{}", unique("new_atlas"), image.empty() ? "" : " image=" + image)};
            }
            else if (kind == "ending") lines = {std::format("ending {}", unique("new_ending"))};
            else if (kind == "achievement") lines = {std::format("achievement {}", unique("new_achievement"))};
            else if (kind == "tip") lines = {std::format("tip {}", unique("new_tip"))};
            else if (kind == "control") lines = {std::format("control {}", unique("new_control"))};
            d.libraryItem = editor.addDefinition(kind, lines);
            f.typeName(kind, *d.libraryItem);
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
        drawStyleSample(ui, def.name, {right.x, top, right.w, std::round(std::min(layout::sampleStyleH * k, right.w * layout::sampleStylePart))});
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
    if (kind == "style" && d.styleView && d.styleOf.second == def.name) // (an empty box: what the style it's based on gives it)
        showStyleValues(fields, d.styleView->def().elements.front().style, engine.palette());
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
    const std::vector<StoryEditor::Field> fields{{.name = std::string(field),
                                                  .value = title,
                                                  .label = caption ? "Caption"_text : kind == "song" ? "Title in the music room"_text : "Title in the gallery"_text,
                                                  .box = Box::Text,
                                                  .hint = "None"}};
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
                             [&](std::optional<std::size_t> i) {
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
            if (d.libraryItem) {
                d.focusOn(std::format("variables:rename:{}", *d.libraryItem), name, true); // (all of it chosen)
            }
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

// The Library's voices: every line of a character with voice_files=, with the file it plays
// (sounds/voices/NAME/SCENE_N) and whether it's there, and each translation's own.
bool Developer::drawVoices(const LibraryFrame& f, KindPagePart pagePart) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    const SDL_FRect &list = f.listBox, &form = f.right;
    const float k = ui.k;
    using Voiced = DeveloperTools::VoicedLine;
    if (d.voicedVersion != d.checked) { // (the scripts read again)
        d.voicedVersion = d.checked;
        d.voiced.clear();
        if (d.script)
            for (const Instruction& ins : d.script->code)
                if (const auto* say = std::get_if<cmd::Say>(&ins.command))
                    if (const CharacterDef* def = d.script->character(say->character); def && !def->voiceFiles.empty() && d.script->files[ins.file].starts_with("scripts/"))
                        d.voiced.push_back({def->voiceFiles + "/" + say->voiceId, def->name, say->text, d.script->files[ins.file], ins.line});
    }
    const std::vector<Voiced>& lines = d.voiced;
    auto there = [&](const std::string& file, const std::string& language) { // (in the language's folder, if one's given)
        return std::ranges::any_of(kSoundTypes, [&](std::string_view ext) { return d.cachedExists(engine.gameRoot() + "sounds/" + language + file + std::string(ext)); });
    };
    clampIndex(d.libraryItem, lines.size());
    if (pagePart == KindPagePart::List) {
        if (const auto clicked = ui.list(list, d.kindScroll, lines.size(),
                                         [&](std::size_t i) { return std::format("{}: {}", lines[i].who, lines[i].text); },
                                         [&](std::size_t i) { return d.libraryItem == i; }, [](std::size_t) { return false; },
                                         [&](std::size_t i) { return !there(lines[i].file, ""); }))
            d.libraryItem = *clicked;
        return false;
    }
    if (!d.libraryItem) return false;
    const Voiced& v = lines[*d.libraryItem];
    ui.formTitle("Voice: {}"_tr("sounds/" + v.file), form.x, form.y, kAccent, form.w);
    float y = form.y + layout::titleH * k;
    y += ui.text(std::format("{}: {}", v.who, v.text), form.x, y, layout::listText, theme::text, form.w).y + 10 * k;
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
    return false;
}

// A style's sample: a button and some words in it, as the game draws them (pointing at the button
// shows how it looks then).
void Developer::drawStyleSample(DeveloperUi& ui, const std::string& style, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    const float h = engine.canvas().w * box.h / box.w; // (in the game's pixels: as tall as the box is for its width)
    if (d.styleOf != std::pair{d.editor.version(), style}) { // the scripts, with a screen of them at the end of main.vnscript
        d.styleView.reset();
        d.styleScript.reset();
        d.styleOf = {d.editor.version(), style};
        const float bh = std::min(h * 0.7f, 90.0f);
        const std::vector<std::string> pixels{ // (laid out in the game's pixels, in the band the box shows; written in places, as screens are)
            "screen vnext_style_sample",
            std::format("    button \"A button\" style={} x={} y={} width={} height={}", style, std::lround(engine.canvas().w * 0.04f), std::lround((h - bh) / 2),
                        std::lround(engine.canvas().w * 0.4f), std::lround(bh)),
            std::format("    text \"Some words\" style={} x={} y={} width={}", style, std::lround(engine.canvas().w * 0.5f), std::lround(h / 2 - 16),
                        std::lround(engine.canvas().w * 0.46f))};
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
            d.styleView = std::make_unique<ScreenView>(it->second, engine, true);
            d.styleScript = std::move(script);
        }
    }
    draw::rect(ui.r, box, kBox);
    if (!d.styleView) return;
    const float scale = box.w / engine.canvas().w;
    const SDL_Event e = mouseMotionAt(ui.over(box) ? (d.mouseX - box.x) / scale : -1000.0f, ui.over(box) ? (d.mouseY - box.y) / scale : -1000.0f);
    {
        const SetFor _(engine.sampling_, true); // (pointing at it only)
        d.styleView->handleEvent(e);
    }
    if (paintCanvas([&] { d.styleView->draw(engine.renderer()); })) {
        const SDL_FRect part{0.0f, 0.0f, engine.canvas().w, h};
        SDL_RenderTexture(ui.r, d.canvas.get(), &part, &box);
    }
    draw::outline(ui.r, box, kEdge);
}

} // namespace vn
