// The Library tab: the game's files by kind, and what the scripts list by name (characters, styles,
// pictures, songs...), each with its form; the variables and the voices.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

namespace vn {

namespace {

// `w` by `h` scaled to fit `room`, in its middle.
SDL_FRect fitIn(const SDL_FRect& room, float w, float h) {
    const float s = std::min(room.w / std::max(1.0f, w), room.h / std::max(1.0f, h));
    return {room.x + (room.w - w * s) / 2, room.y + (room.h - h * s) / 2, w * s, h * s};
}

// + New animation's animations to start from, as + New screen has screens: its name ("": an empty one
// to fill in), what the list calls it, what it does, and its lines under it ("|" between them, as the
// timeline writes them; `loops`: looping).
struct AnimationStart {
    std::string_view name;
    ToolsText label, help;
    std::string_view steps;
    bool loops = false;
};

const std::array kAnimationStarts{
    AnimationStart{"", "Empty animation", "Nothing in it", ""},
    AnimationStart{"fade_in", "Fade in", "From unseen to seen, over what was there before", "opacity=0%|opacity=100% over 0.5 seconds"},
    AnimationStart{"fade_out", "Fade out", "From seen to unseen", "opacity=0% over 0.5 seconds"},
    AnimationStart{"fade_through_black", "Fade through black", "All goes black, then it comes out of the black",
                   "opacity=0%|fade=black|opacity=100% over 0.5 seconds|fade=none over 0.5 seconds"},
    AnimationStart{"flash", "Flash", "A white flash that fades away", "fade=white|fade=none over 0.6 seconds"},
    AnimationStart{"slide_in_left", "Slide in from the left", "From just off the left to its place", "x=-2|x=home over 0.6 seconds"},
    AnimationStart{"slide_in_right", "Slide in from the right", "From just off the right to its place", "x=2|x=home over 0.6 seconds"},
    AnimationStart{"slide_out_left", "Slide out to the left", "From its place to just off the left", "x=-2 over 0.6 seconds"},
    AnimationStart{"slide_out_right", "Slide out to the right", "From its place to just off the right", "x=2 over 0.6 seconds"},
    AnimationStart{"wipe_in", "Wipe in", "Shown bit by bit, from the left", "wipe=0%|wipe=100% over 0.75 seconds"},
    AnimationStart{"wipe_out", "Wipe out", "Hidden bit by bit, from the right", "wipe=0% over 0.75 seconds"},
    AnimationStart{"pop_in", "Pop in", "Grows from small to a little past its size, and back", "zoom=60% opacity=0%|zoom=100% opacity=100% over 0.35 seconds ease=overshoot"},
    AnimationStart{"hop", "Hop", "A little jump", "up=4% over 0.15 seconds ease=out|y=home over 0.15 seconds ease=in"},
    AnimationStart{"shake", "Shake", "Side to side, quickly", "left=1% over 0.05 seconds|right=1% over 0.1 seconds|left=1% over 0.1 seconds|x=home over 0.05 seconds"},
    AnimationStart{"float", "Float", "Up and down gently, looping", "up=1% over 1.5 seconds|y=home over 1.5 seconds", true},
};

std::vector<std::string> startLines(const AnimationStart& start) {
    std::vector<std::string> out;
    for (const auto part : std::views::split(start.steps, '|'))
        if (const std::string_view step(part.begin(), part.end()); !step.empty()) out.emplace_back(step);
    return out;
}

} // namespace

// The Library tab: the game's images, audio, voices, movies and font files (to look at, hear, import
// and rename; an image's sub-images and a font file's fonts under it), what the scripts list by name
// (colours, styles, atlases, animations, endings, achievements, tips, input), each as a form, and the
// variables.
// The Library tab as it is this frame: the kind of file (or definition) listed, and where its list, the
// button under it and the form go. Its files are drawn by drawLibraryFiles (the one chosen by
// drawLibraryFile), its definitions by drawLibraryDefinitions.
struct Developer::LibraryFrame {
    DeveloperUi& ui;
    DeveloperTools& d;
    const FileKind* fileKind; // (none: definitions are listed)
    SDL_FRect middle, right, listBox;
    float buttonH, listGap;

    // A new definition: its name, ready to type, in its form's Name box.
    void typeName(std::string_view ofKind, std::size_t index) const {
        const StoryEditor::Definition made = d.editor.definitions(ofKind)[index];
        d.formScroll = 0.0f;
        d.focusOn(std::format("def:{}:{}:Name", ofKind, made.line), made.name);
    }
};

void Developer::drawLibrary(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    const auto [left, column, right] = ui.columns(area); // (column: what's in the kind chosen, under its heading)

    // --- What's listed.
    std::vector<std::pair<std::string_view, std::string_view>> kinds; // id, label
    for (const FileKind& file : kFileKinds) {
        kinds.emplace_back(file.id, file.label);
        if (file.id == "sounds") kinds.emplace_back("voices", "Voices"_text);
    }
    for (const DefinitionKind& def : kDefinitionKinds)
        if (def.listed) kinds.emplace_back(def.kind, def.label);
    kinds.emplace_back("variables", "Variables"_text);
    auto wrongIn = [&](std::string_view kind) { // (images and fonts: what's made from them)
        const std::string_view of = kind == "variables" ? "variable" : kind == "pictures" ? "picture" : kind == "fontfiles" ? "font" : kind;
        return std::ranges::any_of(editor.definitions(of), [&](const StoryEditor::Definition& def) { return d.problemWithin(def.file, def.line, def.end); });
    };
    const float headingH = layout::headingH * k;
    ui.heading("Library"_tr, left.x, left.y);
    const SDL_FRect kindsBox{left.x, left.y + headingH, left.w, left.h - headingH};
    if (const auto clicked = ui.list(kindsBox, d.kindsScroll, kinds.size(), [&](std::size_t i) { return std::string(tr(kinds[i].second)); },
                                     [&](std::size_t i) { return kinds[i].first == d.library; }, [&](std::size_t i) { return wrongIn(kinds[i].first); })) {
        d.library = kinds[*clicked].first;
        d.libraryItem.reset();
        d.librarySub.reset();
        d.pickingAnimation = false;
        d.libraryScroll = d.formScroll = 0.0f;
        d.focus.clear();
        d.armed.clear();
    }
    // The kind chosen: its heading over its list, as every list has.
    if (const auto kind = std::ranges::find(kinds, std::string_view(d.library), &std::pair<std::string_view, std::string_view>::first); kind != kinds.end())
        ui.heading(tr(kind->second), column.x, column.y);
    const SDL_FRect middle{column.x, column.y + headingH, column.w, column.h - headingH};
    if (d.library == "variables") return drawVariables(ui, middle, right);
    if (d.library == "voices") return drawVoices(ui, middle, right);
    const auto fileKind = std::ranges::find(kFileKinds, std::string_view(d.library), &FileKind::id);
    const bool files = fileKind != std::end(kFileKinds);
    const float buttonH = layout::buttonH * k, listGap = layout::gap * k;
    const SDL_FRect listBox{middle.x, middle.y, middle.w, middle.h - buttonH - listGap};
    const LibraryFrame f{.ui = ui, .d = d, .fileKind = files ? fileKind : nullptr, .middle = middle, .right = right, .listBox = listBox,
                         .buttonH = buttonH, .listGap = listGap};
    if (files) drawLibraryFiles(f);
    else drawLibraryDefinitions(f);
}

// The files of the kind chosen: listed (the folder's, then the translations' own: images/spanish/...),
// renamed (a double click, then Enter: the lines that use it follow) and imported (or dropped on the
// window). An image's sub-images and a font file's fonts are listed under it, when its arrow opens them.
void Developer::drawLibraryFiles(const LibraryFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    SDL_Renderer* r = f.ui.r;
    const float k = f.ui.k;
    using Field = StoryEditor::Field;
    using ListKind = DeveloperUi::ListEvent::Kind;
    const FileKind* fileKind = f.fileKind;
    const SDL_FRect& middle = f.middle;
    const SDL_FRect& right = f.right;
    const SDL_FRect& listBox = f.listBox;
    const float buttonH = f.buttonH;
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
    // What's made from them: sub-images (picture lines) and fonts (font lines), each under the
    // file it's from (one from a file that isn't there: at the end, on its own).
    const std::string subKind = fileKind->id == "pictures" ? "picture" : fileKind->id == "fontfiles" ? "font" : "";
    const auto subs = subKind.empty() ? std::vector<StoryEditor::Definition>{} : editor.definitions(subKind);
    clampIndex(d.librarySub, subs.size());
    std::vector<std::optional<std::size_t>> source; // (each one's file)
    for (const StoryEditor::Definition& def : subs) {
        const auto fields = editor.fields(subKind, def);
        const auto from = std::ranges::find(fields, subKind == "picture" ? "Cut from" : "File", &Field::name);
        const std::string what = from == fields.end() ? std::string{} : lower(unquote(from->value));
        std::optional<std::size_t> at;
        for (std::size_t i = 0; i < names.size() && !at; ++i)
            if (!names[i].contains('/') && (subKind == "picture" ? lower(scriptName(names[i].substr(0, names[i].rfind('.')))) == what
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
    const float arrowW = subKind.empty() ? 0.0f : 22 * k;
    const auto event = ui.editList("files", listBox, d.libraryScroll,
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
                                    .deletes = [&](std::size_t i) { return rows[i].sub.has_value(); },
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
                                            if (fileKind->id == "pictures") { // (the picture before its name, small, as a list of pictures shows them)
                                                const float side = at.h - 4 * k;
                                                if (const Picture pic = engine.assets().picture(row.file ? lower(stemOf(name)) : lower(name))) {
                                                    const float s = std::min(side / std::max(1.0f, pic.w()), side / std::max(1.0f, pic.h()));
                                                    pic.draw(r, {nx + (side - pic.w() * s) / 2, cy - pic.h() * s / 2, pic.w() * s, pic.h() * s}, 1.0f);
                                                }
                                                nx += side + 8 * k;
                                            }
                                            ui.textAround(name, nx, cy, layout::listText, theme::text);
                                            if (fileKind->id == "sounds" && row.file) // (heard, as a list of sounds lets)
                                                listenButton(ui, {at.x + at.w - at.h, at.y, at.h, at.h}, d.cachedAudio(engine.assets(), lower(stemOf(name))), true);
                                            const auto dot = name.rfind('.'); // (an MP4, MOV or M4V: Linux players need GStreamer for it)
                                            if (row.file && dot != npos && std::ranges::contains(kSystemVideoTypes, std::string_view(lower(name.substr(dot)))))
                                                ui.textAround("on Linux: needs GStreamer"_tr, nx + ui.width(name, layout::listText) + 8 * k, cy, layout::smallText, kWarn);
                                            else if (row.file && d.unused.contains(name)) ui.textAround("unused"_tr, nx + ui.width(name, layout::listText) + 8 * k, cy, layout::smallText, theme::dim);
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
            if (trim(event.name).empty()) return; // (emptied: it keeps its name)
            d.saidRenamed(editor.renameDefinition(subKind, *j, event.name, std::format("def-name:{}:{}", subKind, d.focusCount)), "A name is one word (letters, numbers, _ and -) that no other one of these has."_tr);
            return;
        }
        break;
    case ListKind::Deleted:
        if (const auto j = rows[event.item].sub) editor.removeDefinition(subs[*j]);
        d.librarySub.reset();
        return;
    case ListKind::Moved:
    case ListKind::None:
        break;
    }
    if (d.entered.starts_with("files:rename:")) {
        const std::size_t row = std::strtoul(d.entered.c_str() + 13, nullptr, 10);
        if (row >= rows.size() || !rows[row].file) return;
        const std::size_t i = *rows[row].file;
        const std::string to(trim(d.buffer));
        if (to.empty()) return; // (emptied: it keeps its name)
        if (const auto changed = editor.renameAsset(fileKind->folder, names[i], to)) {
            ++d.filesGeneration;
            const auto now = listed();
            const std::string name = to.contains('.') ? to : to + names[i].substr(names[i].rfind('.'));
            const auto at = std::ranges::find_if(now, [&](const std::string& n) { return lower(n) == lower(name); });
            d.libraryItem = at != now.end() ? std::optional(static_cast<std::size_t>(at - now.begin())) : std::nullopt;
            if (const auto open = std::ranges::find(d.libraryOpen, names[i]); open != d.libraryOpen.end()) *open = name;
            d.failed = false;
            d.status = *changed == 0 ? "Renamed it {}."_tr(name)
                     : *changed == 1 ? "Renamed it {}, and the line that uses it."_tr(name)
                                     : "Renamed it {}, and the {} lines that use it."_tr(name, *changed);
        } else {
            d.failed = true;
            d.status = "A file's name is one word (letters, numbers, _ and -) that no other file there has."_tr;
        }
        return;
    }
    if (ui.button({middle.x, listBox.y + listBox.h + layout::gap * k, middle.w, buttonH}, "Import..."_tr, false, true, layout::buttonText))
        chooseFiles(tr(fileKind->label), fileKind->extensions, {});
    if (d.librarySub) return drawDefinition(ui, subKind, subs[*d.librarySub], right);
    if (!d.libraryItem) {
        ui.text("The game's {} are in {}{}. Import more here, or drop them on this window: each goes in its folder. Double-click one to rename it."_tr(
                    lower(tr(fileKind->label)), engine.gameRoot(), fileKind->folder),
                right.x, right.y, layout::noteText, theme::dim, right.w);
        return;
    }
    drawLibraryFile(f, names[*d.libraryItem]);
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
    const std::string translation = slash == npos ? std::string{} : " " + "The {} translation shows it instead."_tr(key.substr(0, slash)); // (said after what it is)
    ui.formTitle(std::format("{}: {}", tr(fileKind->one), name), right.x, right.y);
    // To the Trash (the system's, so it can be got back): a second click does it.
    const std::string armId = "trash:" + path;
    const bool armed = d.armed == armId;
    const float trashW = std::max(ui.buttonWidth("Move to the Trash"_tr, layout::buttonText, layout::widerButtonW), // (one size, either way, as its words say)
                                  ui.buttonWidth("Click again to move it"_tr, layout::buttonText, layout::widerButtonW));
    if (ui.button({right.x + right.w - trashW, right.y, trashW, buttonH}, armed ? "Click again to move it"_tr : "Move to the Trash"_tr, armed, true, layout::buttonText)) {
        if (!armed) {
            d.armed = armId;
        } else {
            d.armed.clear();
            engine.audio().stopAll(0.0f);
            d.stopMovie();
            d.fontSampleOf.clear();
            d.fontSample.reset();
            if (const auto there = moveToTrash(engine.gameRoot() + path)) {
                ++d.filesGeneration;
                if (!there->empty()) editor.movedFiles({{engine.gameRoot() + path, *there}}); // (undo takes it out again)
                d.failed = false;
                d.status = "'{}' is in the Trash now."_tr(name);
                d.libraryItem.reset();
                ++d.checked; // (the previews are made again)
                return;
            }
            d.failed = true;
            d.status = "Can't move '{}' to the Trash."_tr(name);
        }
    }
    float y = right.y + layout::titleH * k;
    // Each translation's own (a picture with words on it in its language, audio in it), to import there: a
    // row for each, saying whether it has one or uses this one.
    auto languages = [&] {
        if (slash != npos || fileKind->id == "fontfiles") return;
        const std::string translations = engine.gameRoot() + "translations/";
        const std::vector<std::string>& files = d.cachedFiles(translations, ".vnscript");
        if (files.empty()) return;
        const bool pictures = fileKind->id == "pictures";
        y += ui.text(pictures ? "What each translation shows: this one, or a file of its own (an image with its words on it)."_tr
                              : "What each translation plays: this one, or a file of its own (one in its language)."_tr,
                     right.x, y, layout::smallText, theme::dim, right.w).y + 4 * k;
        for (const std::string& file : files) {
            const std::string id = stemOf(file), as = std::string(fileKind->folder) + id + "/" + called;
            const auto own = std::ranges::find_if(fileKind->extensions, [&](std::string_view ext) { return d.cachedExists(engine.gameRoot() + as + std::string(ext)); });
            const bool has = own != std::end(fileKind->extensions);
            const float bw = ui.buttonWidth(has ? "Replace..."_tr : "Its own file..."_tr, layout::buttonText, layout::wideButtonW);
            ui.textAround(has ? (pictures ? "{} shows {}"_tr : "{} plays {}"_tr)(id, as + std::string(*own)) : (pictures ? "{} shows this one"_tr : "{} plays this one"_tr)(id),
                          right.x, y + buttonH / 2, layout::noteText, theme::dim,
                          right.w - bw - 10 * k);
            if (ui.button({right.x + right.w - bw, y, bw, buttonH}, has ? "Replace..."_tr : "Its own file..."_tr, false, true, layout::buttonText))
                chooseFiles(tr(fileKind->label), fileKind->extensions, as);
            y += buttonH + layout::gap * k;
        }
        y += 4 * k;
    };
    if (fileKind->id == "pictures") {
        const Picture pic = engine.assets().whole(key);
        const bool cuts = slash == npos && pic; // (+ Sub-image: on the row that says what it is)
        const float subW = ui.buttonWidth("+ Sub-image"_tr, layout::buttonText, layout::widerButtonW);
        const float infoH = ui.text("{} x {} pixels. Scripts call it {}."_tr(pic.w(), pic.h(), called) + translation, right.x, y + (cuts ? 6 * k : 0.0f), layout::noteText,
                                    theme::dim, right.w - (cuts ? subW + 10 * k : 0.0f)).y;
        if (cuts && ui.button({right.x + right.w - subW, y, subW, buttonH}, "+ Sub-image"_tr, false, true, layout::buttonText)) { // (a part of it with a name of its own: all of it, to cut down)
            const std::string as = uniqueName("new_picture", [&](const std::string& n) { return editor.defines("picture", n) || engine.assets().hasImageFile(n); });
            d.librarySub = editor.addDefinition("picture", {std::format("picture {} image={} x=0 y=0 w={} h={}", as, called, pic.w(), pic.h())});
            f.typeName("picture", *d.librarySub);
            return;
        }
        y += std::max(infoH + (cuts ? 6 * k : 0.0f), cuts ? buttonH : 0.0f) + 10 * k;
        if (slash == npos) drawRecordTitle(ui, "gallery", called, right.x, right.w, y);
        languages();
        if (pic) {
            const float s = std::min({1.0f, right.w / std::max(1.0f, pic.w()), (right.y + right.h - y) / std::max(1.0f, pic.h())});
            draw::rect(r, {right.x, y, pic.w() * s, pic.h() * s}, {40, 40, 48, 255});
            pic.draw(r, {right.x, y, pic.w() * s, pic.h() * s}, 1.0f);
        }
    } else if (fileKind->id == "sounds") {
        const float infoH = ui.text("Scripts call it {} (music {}, sound {})."_tr(called, called, called) + translation, right.x, y + 6 * k, layout::noteText, theme::dim, right.w - playW - 10 * k).y;
        listenButton(ui, {right.x + right.w - playW, y, playW, buttonH}, engine.assets().audio(key)); // (Play: on the row that says what it is)
        y += std::max(infoH + 6 * k, buttonH) + 10 * k;
        if (slash == npos) drawRecordTitle(ui, "song", called, right.x, right.w, y);
        languages();
    } else if (fileKind->id == "movies") {
        // A still of its first picture, and Play: it plays there, with its sound (and every other sound waits).
        auto still = [&] {
            d.movie = std::make_unique<Movie>(r, d.silence, engine.gameRoot() + path, false, 1.0f);
            d.movie->showFirst();
            d.movieOf = path;
            d.moviePlaying = false;
        };
        if (d.movieOf != path) still();
        Movie& movie = *d.movie;
        const std::string about = movie ? "{} x {} pixels, {:.1f} seconds."_tr(movie.width(), movie.height(), movie.duration()) : "It doesn't play: the log says why."_tr();
        const float infoH = ui.text("Scripts call it {} (movie {})."_tr(called, called) + " " + about + translation, right.x, y + 6 * k, layout::noteText, theme::dim, right.w - playW - 10 * k).y;
        d.listenShown = d.listenShown || d.moviePlaying;
        if (ui.button({right.x + right.w - playW, y, playW, buttonH}, d.moviePlaying ? "Stop"_tr : "Play"_tr, d.moviePlaying, static_cast<bool>(movie), layout::buttonText)) { // (on the row that says what it is)
            if (d.moviePlaying) {
                engine.audio().stopListening();
                still();
            } else {
                d.movie = std::make_unique<Movie>(r, engine.audio(), engine.gameRoot() + path, false, 1.0f);
                engine.audio().listen(path, d.movie->sound());
                d.moviePlaying = d.listenShown = true;
                d.movieTicks = SDL_GetTicks();
            }
        }
        y += std::max(infoH + 6 * k, buttonH) + 10 * k;
        if (slash == npos) drawRecordTitle(ui, "gallery", called, right.x, right.w, y);
        languages();
        if (d.moviePlaying) {
            const Uint64 now = SDL_GetTicks();
            d.movie->update(std::min(0.1f, static_cast<float>(now - d.movieTicks) / 1000.0f));
            d.movieTicks = now;
            if (d.movie->ended()) {
                engine.audio().stopListening();
                still();
            }
        }
        if (SDL_Texture* picture = d.movie->texture()) {
            const float s = std::min({1.0f, right.w / std::max(1.0f, d.movie->width()), (right.y + right.h - y) / std::max(1.0f, d.movie->height())});
            const SDL_FRect shown{right.x, y, d.movie->width() * s, d.movie->height() * s};
            SDL_RenderTexture(r, picture, nullptr, &shown);
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
        y += buttonH + 14 * k;
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

// The definitions of the kind chosen: listed (renamed, deleted, dragged), and the one chosen as a form.
void Developer::drawLibraryDefinitions(const LibraryFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = f.ui.k;
    using ListKind = DeveloperUi::ListEvent::Kind;
    const SDL_FRect& middle = f.middle;
    const SDL_FRect& right = f.right;
    const SDL_FRect& listBox = f.listBox;
    const float buttonH = f.buttonH;
    const float listGap = f.listGap;
    const std::string kind = d.library;
    const auto defs = editor.definitions(kind);
    clampIndex(d.libraryItem, defs.size());
    const bool picks = kind == "animation"; // (+ New animation: the ones to start from, as + New screen's)
    std::function<void(std::size_t, const SDL_FRect&)> paintColor; // (a colour: as a square before its name, as its box shows it)
    if (kind == "color")
        paintColor = [&](std::size_t i, const SDL_FRect& at) {
            float x = at.x + layout::listPad * k;
            if (const auto c = parseColor(fieldValue(editor.fields(kind, defs[i]), "Colour"), &engine.palette())) x = ui.swatch(x, at.y + at.h / 2, *c);
            ui.textAround(defs[i].name, x, at.y + at.h / 2, layout::listText, theme::text);
        };
    const auto event = ui.editList("library", listBox, d.libraryScroll,
                                   {.count = defs.size(),
                                    .label = [&](std::size_t i) { return defs[i].name; },
                                    .chosen = [&](std::size_t i) { return d.libraryItem == i && !(picks && d.pickingAnimation); },
                                    .wrong =
                                        [&](std::size_t i) { return d.problemWithin(defs[i].file, defs[i].line, defs[i].end); },
                                    .faint = {},
                                    .renames = [](std::size_t) { return true; },
                                    .deletes = [](std::size_t) { return true; },
                                    .moves = [](std::size_t) { return true; },
                                    .drops =
                                        [&](std::size_t item, std::size_t slot) {
                                            return (slot < defs.size() && defs[slot].file == defs[item].file) || (slot > 0 && defs[slot - 1].file == defs[item].file);
                                        },
                                    .paint = paintColor});
    switch (event.kind) {
    case ListKind::Chosen:
        if (d.libraryItem != event.item || d.pickingAnimation) d.formScroll = 0.0f;
        d.libraryItem = event.item;
        d.pickingAnimation = false;
        d.focus.clear();
        break;
    case ListKind::Renamed:
        if (trim(event.name).empty()) return; // (emptied: it keeps its name)
        d.saidRenamed(editor.renameDefinition(kind, event.item, event.name, std::format("def-name:{}:{}", kind, d.focusCount)), "A name is one word (letters, numbers, _ and -) that no other one of these has."_tr);
        return;
    case ListKind::Deleted:
        editor.removeDefinition(defs[event.item]);
        d.libraryItem.reset();
        return;
    case ListKind::Moved:
        if (const auto to = editor.moveDefinition(kind, event.item, event.to)) d.libraryItem = *to;
        return;
    case ListKind::None:
        break;
    }
    const DefinitionKind& of = *std::ranges::find(kDefinitionKinds, std::string_view(kind), &DefinitionKind::kind);
    const std::string_view one = tr(of.one); // (as every tab's: + New scene, + New screen...)
    const std::string newLabel = "+ New {}"_tr(one == of.one ? lower(one) : std::string(one));
    if (ui.button({middle.x, listBox.y + listBox.h + listGap, middle.w, buttonH}, newLabel, picks && d.pickingAnimation, true, layout::buttonText)) {
        if (picks) {
            d.pickingAnimation = !d.pickingAnimation;
            d.startShownAt = engine.toolsTime();
            d.formScroll = 0.0f;
            d.focus.clear();
            return;
        }
        auto unique = [&](const std::string& base) { // a name none of them has
            return uniqueName(base, [&](const std::string& n) { return std::ranges::any_of(defs, [&](const StoryEditor::Definition& def) { return lower(def.name) == n; }); });
        };
        std::vector<std::string> lines; // (sub-images and fonts are added to their files, by + Sub-image and + Font)
        if (kind == "color") lines = {std::format("color {} #ffffff", unique("new_color"))};
        else if (kind == "style") lines = {std::format("style {}", unique("new_style"))};
        else if (kind == "atlas") lines = {std::format("atlas {}", unique("new_atlas"))}; // (its image is called the same; its frames' names: typed into the form's)
        else if (kind == "ending") lines = {std::format("ending {} \"\"", unique("new_ending"))};
        else if (kind == "achievement") lines = {std::format("achievement {} \"\"", unique("new_achievement"))};
        else if (kind == "tip") lines = {std::format("tip {} \"\"", unique("new_tip"))};
        else if (kind == "control") lines = {std::format("control {}", unique("new_input"))};
        d.libraryItem = editor.addDefinition(kind, lines);
        f.typeName(kind, *d.libraryItem);
        return;
    }
    if (picks && d.pickingAnimation) return drawNewAnimationForm(ui, right);
    if (!d.libraryItem) {
        ui.text("Click one of the {} to change it, or add one with {}. Double-click one to rename it: where it's used changes too."_tr(lower(tr(of.label)), newLabel),
                right.x, right.y, layout::noteText, theme::dim, right.w);
        return;
    }
    drawDefinition(ui, kind, defs[*d.libraryItem], right);
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
    ui.formTitle(std::format("{}: {}", label, def.name), right.x, right.y);
    asTextButton(ui, right.x + right.w, right.y);
    const float boxH = layout::boxH * k, rowGap = layout::rowGap * k, labelW = std::round(right.w * layout::labelPart);
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
    if (kind == "picture") { // (the image it's cut from, to cut it out on)
        const float h = std::round(std::min(layout::sampleSubPictureH * k, (right.h - layout::titleH * k) * layout::sampleSubPicturePart));
        drawSubPicture(ui, def, {right.x, top, right.w, h});
        top += h + 10 * k;
    }
    const SDL_FRect formBox{right.x, top, right.w, right.y + right.h - top};
    ui.scrollForm(formBox, boxH + rowGap);
    float y = formBox.y - d.formScroll;
    const DeveloperUi::Clip clipped(ui, formBox, true);
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
        showConfigValues(fields, engine.config());
    if (kind == "style" && d.styleView && d.styleOf.second == def.name) // (an empty box: what the style it's based on gives it)
        showStyleValues(fields, d.styleView->def().elements.front().style, engine.palette());
    const std::string id = std::format("def:{}:{}", kind, def.line);
    if (const auto edit = drawForm(ui, id, fields, right.x, right.w, labelW, y)) {
        if (edit->typed) {
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
    if (kind == "picture") drawRecordTitle(ui, "gallery", lower(def.name), right.x, right.w, y); // (its title in the gallery, under its form)
    ui.formDrawn(formBox, y);
}

// An image's title in the gallery, or a song's in the music room: its gallery or song line, made
// when one is typed, and taken out when it's emptied.
void Developer::drawRecordTitle(DeveloperUi& ui, std::string_view kind, const std::string& name, float x, float w, float& y) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const auto defs = editor.definitions(kind);
    const auto def = std::ranges::find_if(defs, [&](const StoryEditor::Definition& one) { return sameName(one.name, name); });
    const std::string title = def != defs.end() ? fieldValue(editor.fields(kind, *def), "Title") : std::string{};
    const bool song = kind == "song";
    const std::vector<StoryEditor::Field> fields{{.name = "Title", .value = title, .label = song ? "Title in the music room"_text : "Title in the gallery"_text, .box = Box::Text,
                                                  .hint = "None"}};
    if (const auto edit = drawForm(ui, std::format("title:{}:{}", kind, name), fields, x, w, std::round(w * 0.3f), y)) {
        if (trim(edit->value).empty()) {
            if (def != defs.end()) editor.removeDefinition(*def);
        } else if (def != defs.end()) {
            editor.setDefinition(*def, {std::format("{} {} \"{}\"", kind, name, edit->value)}, std::format("title:{}:{}:{}", kind, name, d.focusCount));
        } else {
            editor.addDefinition(kind, {std::format("{} {} \"{}\"", kind, name, edit->value)});
        }
    }
}

// The Library's variables: the ones the scripts change or ask for, or list with a variable line (and
// while a game is going on, the story's own too). Each has its value then (to change), what it starts
// at and what it's for (its variable line), and the lines that use it. + New variable lists one; a double click
// renames one (Enter: in every line that uses it), and its x takes its variable line out.
void Developer::drawVariables(DeveloperUi& ui, const SDL_FRect& area, const SDL_FRect& form) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k, buttonH = layout::buttonH * k;
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
    const SDL_FRect list{area.x, area.y, area.w, area.h - buttonH - layout::gap * k};
    const auto event = ui.editList("variables", list, d.libraryScroll,
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
                                            const float pad = layout::listPad * k;
                                            ui.textAround(names[i], box.x + pad, box.y + box.h / 2, layout::listText, theme::text);
                                            if (!playing) return;
                                            const std::string v = ui.fitted(valueOf(names[i]), layout::noteText, box.w - 3 * pad - ui.width(names[i], layout::listText)); // (in the room the name leaves)
                                            ui.textAround(v, box.x + box.w - pad - ui.width(v, layout::noteText), box.y + box.h / 2, layout::noteText, theme::dim);
                                        }});
    switch (event.kind) {
    case DeveloperUi::ListEvent::Kind::Chosen:
        d.libraryItem = event.item;
        d.findScroll = 0.0f;
        break;
    case DeveloperUi::ListEvent::Kind::Deleted:
        if (const auto def = definition(names[event.item])) {
            editor.removeDefinition(*def);
            d.libraryItem.reset();
            const auto left = editor.uses(names[event.item]).size();
            d.failed = false;
            d.status = left == 0 ? "{} is gone."_tr(names[event.item])
                     : left == 1 ? "{}'s variable line is gone; 1 line still uses it."_tr(names[event.item])
                                 : "{}'s variable line is gone; {} lines still use it."_tr(names[event.item], left);
        }
        return;
    default:
        break;
    }
    if (d.entered.starts_with("variables:rename:")) {
        const std::size_t i = std::strtoul(d.entered.c_str() + 17, nullptr, 10);
        const std::string to = lower(trim(d.buffer));
        if (i >= names.size() || to.empty()) return; // (emptied: it keeps its name)
        if (const auto uses = editor.renameVariable(names[i], to)) {
            if (playing) engine.game_->setVariable(to, engine.game_->variable(names[i])); // (the game keeps its value under the new name)
            choose(to);
            d.failed = false;
            d.status = *uses == 0 ? "Renamed it {}."_tr(to)
                     : *uses == 1 ? "Renamed it {}, and the line that uses it."_tr(to)
                                  : "Renamed it {}, and the {} lines that use it."_tr(to, *uses);
        } else {
            d.failed = true;
            d.status = "A variable's name is one word (letters, numbers and _), not a word conditions use (and, or, is...), that no other variable has."_tr;
        }
        return;
    }
    if (ui.button({area.x, list.y + list.h + layout::gap * k, area.w, buttonH}, "+ New variable"_tr, false, true, layout::buttonText)) { // (its name, ready to type over)
        const std::string name = uniqueName("new_variable", [&](const std::string& n) { return std::ranges::contains(names, n); });
        editor.addDefinition("variable", {std::format("variable {}", name)}); // (it starts at 0: typed into the form when it's something else)
        choose(name);
        if (d.libraryItem) {
            d.focusOn(std::format("variables:rename:{}", *d.libraryItem), name, true); // (all of it chosen)
        }
        return;
    }
    if (!d.libraryItem) {
        ui.text(names.empty() ? "Variables keep what happens in the story: a Set line changes one, and If lines and options ask about it. + New variable lists one with the value it starts at."_tr
                : playing     ? "Click a variable to see the lines that use it and change its value. Double-click one to rename it (Enter): every line that uses it changes too."_tr
                              : "Click a variable to see the lines that use it. Double-click one to rename it (Enter): every line that uses it changes too."_tr,
                form.x, form.y, layout::noteText, theme::dim, form.w);
        return;
    }
    const std::string& name = names[*d.libraryItem];
    ui.formTitle("Variable: {}"_tr(name), form.x, form.y);
    const float boxH = layout::boxH * k, labelW = std::round(form.w * layout::labelPart), gap = layout::rowGap * k;
    float y = form.y + layout::titleH * k;
    const auto def = definition(name);
    if (def)
        if (const ScriptError* problem = d.problemAt(def->file, def->line)) y += ui.text(problem->message, form.x, y, layout::noteText, kBad, form.w).y + 8 * k;
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
    if (const auto edit = drawForm(ui, "variable:" + name + ":def", fields, form.x, form.w, labelW, y)) {
        if (edit->name == "Kept between games") {
            const std::string bare = kept ? name.substr(11) : name, to = edit->value == "yes" ? "persistent." + bare : bare;
            if (const auto uses = editor.renameVariable(name, to)) {
                choose(to);
                d.failed = false;
                d.status = (edit->value == "yes" ? "Kept between games now, as {}: the {} lines that use it follow."_tr : "Kept in a game only now, as {}: the {} lines that use it follow."_tr)(to, *uses);
            } else {
                d.failed = true;
                d.status = "There's a variable called {} already."_tr(to);
            }
            return;
        }
        std::string value = edit->name == "Starts at" ? edit->value : start, note = edit->name == "What it's for" ? edit->value : about;
        std::string line = "variable " + name;
        if (!trim(value).empty() || !note.empty()) line += " " + (trim(value).empty() ? std::string("0") : scripted(value, editor.variables()));
        if (!note.empty()) line += " \"" + note + "\"";
        if (def) editor.setDefinition(*def, {line}, std::format("variable:{}:{}{}", name, edit->name, d.focusCount));
        else editor.addDefinition("variable", {line});
    }
    ui.textAround("Now"_tr, form.x, y + boxH / 2, layout::smallText, theme::dim, labelW - 8 * k);
    if (playing) { // (a number, yes or no, or words)
        const StoryEditor::Field now{.name = "now", .value = valueOf(name), .box = Box::VariableValue}; // (as every value's box)
        if (const auto typed = ui.pickField("variable:" + name, {form.x + labelW, y, form.w - labelW, boxH}, now, layout::boxText)) {
            const std::string t(trim(*typed));
            const auto n = parseDecimal(t);
            engine.game_->setVariable(name, t == "yes" ? Value(1) : t == "no" ? Value(0) : n ? number(*n) : Value(t));
        }
    } else {
        ui.textAround("Shown while a game is going on: Play, then F10."_tr, form.x + labelW, y + boxH / 2, layout::noteText, theme::dim, form.w - labelW);
    }
    y += boxH + gap + layout::gap * k;
    if (d.lookedUses.what != name || d.lookedUses.version != editor.version()) // (looked for again when the variable or the scripts change)
        d.lookedUses = {.what = name, .version = editor.version(), .found = editor.uses(name)};
    const auto& found = d.lookedUses.found;
    ui.heading(found.size() == 1 ? "Used in 1 line"_tr() : "Used in {} lines"_tr(found.size()), form.x, y); // (over its list, as every list has)
    y += layout::headingH * k;
    if (const auto clicked = ui.list({form.x, y, form.w, std::max(0.0f, form.y + form.h - y)}, d.findScroll, found.size(),
                                     [&](std::size_t i) { return std::format("{}:  {}", editor.placeOf(found[i].file, found[i].line), found[i].text); },
                                     [](std::size_t) { return false; }, [&](std::size_t i) { return d.problemAt(found[i].file, found[i].line) != nullptr; }))
        goTo(editor.path(found[*clicked].file), found[*clicked].line);
}

// The Library's voices: every line of a character with voices=, with the file it plays
// (audio/voices/NAME/SCENE_N) and whether it's there, and each translation's own.
void Developer::drawVoices(DeveloperUi& ui, const SDL_FRect& list, const SDL_FRect& form) {
    DeveloperTools& d = tools;
    const float k = ui.k;
    using Voiced = DeveloperTools::VoicedLine;
    if (d.voicedVersion != d.checked) { // (the scripts read again)
        d.voicedVersion = d.checked;
        d.voiced.clear();
        if (d.script)
            for (const Instruction& ins : d.script->code)
                if (const auto* say = std::get_if<cmd::Say>(&ins.command))
                    if (const CharacterDef* def = d.script->character(say->character); def && !def->voices.empty() && d.script->files[ins.file].starts_with("scripts/"))
                        d.voiced.push_back({def->voices + "/" + say->voiceId, def->name, say->text, d.script->files[ins.file], ins.line});
    }
    const std::vector<Voiced>& lines = d.voiced;
    auto there = [&](const std::string& file, const std::string& language) { // (in the language's folder, if one's given)
        return std::ranges::any_of(kAudioTypes, [&](std::string_view ext) { return d.cachedExists(engine.gameRoot() + "audio/" + language + file + std::string(ext)); });
    };
    clampIndex(d.libraryItem, lines.size());
    if (const auto clicked = ui.list(list, d.libraryScroll, lines.size(),
                                     [&](std::size_t i) { return std::format("{}: {}", lines[i].who, lines[i].text); },
                                     [&](std::size_t i) { return d.libraryItem == i; }, [](std::size_t) { return false; },
                                     [&](std::size_t i) { return !there(lines[i].file, ""); }))
        d.libraryItem = *clicked;
    if (!d.libraryItem) {
        const auto recorded = std::ranges::count_if(lines, [&](const Voiced& v) { return there(v.file, ""); });
        ui.text(lines.empty() ? "A character with Voices (in their form) says each line with a file of its own: audio/voices/NAME/SCENE_N. Their lines are listed here, with the file each one plays."_tr()
                              : "{} lines with voices, {} of them recorded (the ones not recorded yet are dim). Click one to hear it, or to see which file it needs."_tr(lines.size(), recorded),
                form.x, form.y, layout::noteText, theme::dim, form.w);
        return;
    }
    const Voiced& v = lines[*d.libraryItem];
    ui.formTitle("Voice: {}"_tr("audio/" + v.file), form.x, form.y);
    float y = form.y + layout::titleH * k;
    y += ui.text(std::format("{}: {}", v.who, v.text), form.x, y, layout::listText, theme::text, form.w).y + 10 * k;
    const float buttonH = layout::buttonH * k;
    const bool here = there(v.file, "");
    const float importW = ui.buttonWidth(here ? "Replace..."_tr : "Import..."_tr, layout::buttonText, layout::wideButtonW);
    ui.textAround(here ? "It's there."_tr : "It isn't recorded yet: the line plays without a voice."_tr, form.x, y + buttonH / 2, layout::noteText, here ? kGood : theme::dim,
                  form.w - importW - 10 * k);
    if (ui.button({form.x + form.w - importW, y, importW, buttonH}, here ? "Replace..."_tr : "Import..."_tr, false, true, layout::buttonText))
        chooseFiles("Audio"_tr, kAudioTypes, "audio/" + v.file);
    y += buttonH + layout::gap * k;
    const std::string translations = engine.gameRoot() + "translations/";
    const std::vector<std::string>& languages = d.cachedFiles(translations, ".vnscript"); // (each translation's own)
    if (!languages.empty()) y += ui.text("What each translation plays: this one, or a voice of its own in its language."_tr, form.x, y, layout::smallText, theme::dim, form.w).y + 4 * k;
    for (const std::string& language : languages) {
        const std::string id = stemOf(language);
        const bool translated = there(v.file, id + "/");
        const float ownW = ui.buttonWidth(translated ? "Replace..."_tr : "Its own file..."_tr, layout::buttonText, layout::wideButtonW);
        ui.textAround(translated ? "{} plays {}"_tr(id, "audio/" + id + "/" + v.file) : "{} plays this one"_tr(id), form.x, y + buttonH / 2, layout::noteText,
                      theme::dim, form.w - ownW - 10 * k);
        if (ui.button({form.x + form.w - ownW, y, ownW, buttonH}, translated ? "Replace..."_tr : "Its own file..."_tr, false, true, layout::buttonText))
            chooseFiles("Audio"_tr, kAudioTypes, "audio/" + id + "/" + v.file);
        y += buttonH + layout::gap * k;
    }
    y += 8 * k;
    const float playW = std::max(ui.buttonWidth("Play"_tr, layout::buttonText, layout::wideButtonW), ui.buttonWidth("Stop"_tr, layout::buttonText, layout::wideButtonW));
    if (here) listenButton(ui, {form.x, y, playW, buttonH}, engine.assets().audio(v.file));
    if (ui.button({form.x + (here ? playW + layout::gap * k : 0.0f), y, ui.buttonWidth("Go to the line"_tr, layout::buttonText, layout::widerButtonW), buttonH}, "Go to the line"_tr, false, true, layout::buttonText))
        goTo(v.path, static_cast<std::size_t>(std::max(v.line - 1, 0)));
}

// An atlas over its form: on the left its picture, the frames marked on it and each expression's first
// named (a click shows that expression on the right); on the right that frame, as big as fits, with its
// eyes and mouth parts where they go: dragging one moves it there (its x= and y=).
void Developer::drawAtlas(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    draw::rect(r, box, kBox);
    const Atlas* a = nullptr;
    if (d.script)
        for (const Atlas& each : d.script->atlases)
            if (each.name == lower(def.name)) a = &each;
    const Picture sheet = a ? engine.assets().picture(a->image) : Picture{};
    if (!a || !sheet) {
        ui.textAround(a ? "There's no image called '{}'."_tr(a->image) : "Its image shows once the scripts' mistakes are fixed."_tr(), box.x + 10 * k,
                      box.y + box.h / 2, layout::noteText, theme::dim, box.w - 20 * k);
        draw::outline(r, box, kEdge);
        return;
    }
    // The frames, as the player cuts them (Assets::picture).
    const int expressions = std::max<int>(1, static_cast<int>(a->expressions.size()));
    const int eyes = a->eyesPart ? 1 : std::max<int>(1, static_cast<int>(a->eyes.size()));
    const int mouths = a->mouthPart ? 1 : std::max<int>(1, static_cast<int>(a->mouth.size()));
    const int perExpression = eyes * mouths;
    const int columns = a->columns > 0 ? a->columns : perExpression > 1 ? perExpression : expressions;
    const int rows = (expressions * perExpression + columns - 1) / columns;
    const float fw = sheet.w() / static_cast<float>(columns), fh = sheet.h() / static_cast<float>(rows);
    d.atlasExpression = std::min<std::size_t>(d.atlasExpression, static_cast<std::size_t>(expressions - 1));
    const auto fit = fitIn;
    const float pad = 8 * k, half = (box.w - 3 * pad) / 2;
    const SDL_FRect left = fit({box.x + pad, box.y + pad, half, box.h - 2 * pad}, sheet.w(), sheet.h());
    sheet.draw(r, left, 1.0f);
    const float s = left.w / sheet.w();
    for (int i = 0; i < expressions * perExpression; ++i) {
        const SDL_FRect cell{left.x + static_cast<float>(i % columns) * fw * s, left.y + static_cast<float>(i / columns) * fh * s, fw * s, fh * s};
        const int expression = i / perExpression;
        const bool first = i % perExpression == 0, chosen = expression == static_cast<int>(d.atlasExpression);
        draw::outline(r, cell, chosen ? kAccent : SDL_Color{kEdge.r, kEdge.g, kEdge.b, 140});
        if (first && expression < static_cast<int>(a->expressions.size())) {
            const DeveloperUi::Clip clipped(ui, cell);
            ui.text(a->expressions[static_cast<std::size_t>(expression)], cell.x + 3 * k, cell.y + 2 * k, layout::sampleText, chosen ? kAccent : theme::text);
        }
        if (ui.click(cell)) d.atlasExpression = static_cast<std::size_t>(expression);
    }
    // The chosen expression's frame, with its parts.
    const int index = static_cast<int>(d.atlasExpression) * perExpression;
    const Picture frame = sheet.crop({fw * static_cast<float>(index % columns), fh * static_cast<float>(index / columns), fw, fh});
    const SDL_FRect right = fit({box.x + 2 * pad + half, box.y + pad, half, box.h - 2 * pad - 18 * k}, fw, fh); // (the words under it)
    draw::rect(r, right, {40, 40, 48, 255});
    frame.draw(r, right, 1.0f);
    const float fs = right.w / fw;
    struct Shown {
        bool mouth;
        SDL_FRect at;
    };
    std::vector<Shown> parts;
    for (const bool mouth : {false, true}) {
        const auto& part = mouth ? a->mouthPart : a->eyesPart;
        const std::size_t states = mouth ? a->mouth.size() : a->eyes.size();
        if (!part || states == 0) continue;
        const Picture pic = engine.assets().picture(part->image);
        if (!pic) continue;
        const float cw = pic.w() / static_cast<float>(states), ch = pic.h() / static_cast<float>(expressions);
        float px = part->x, py = part->y;
        if (d.partDrag && d.partDrag->mouth == mouth) { // (where it's being dragged)
            px = std::round(d.partDrag->px + (d.mouseX - d.partDrag->x) / fs);
            py = std::round(d.partDrag->py + (d.mouseY - d.partDrag->y) / fs);
        }
        const SDL_FRect at{right.x + px * fs, right.y + py * fs, cw * fs, ch * fs};
        pic.crop({0.0f, ch * static_cast<float>(d.atlasExpression), cw, ch}).draw(r, at, 1.0f);
        draw::outline(r, at, ui.over(at) || (d.partDrag && d.partDrag->mouth == mouth) ? kAccent : kOptionArrow);
        parts.push_back({mouth, at});
    }
    for (const Shown& p : parts | std::views::reverse) // (the one on top first)
        if (ui.click(p.at)) {
            const auto& part = p.mouth ? a->mouthPart : a->eyesPart;
            d.partDrag = DeveloperTools::PartDrag{.mouth = p.mouth, .x = d.mouseX, .y = d.mouseY, .px = part->x, .py = part->y, .key = ++d.focusCount};
        }
    if (d.partDrag && !d.held) { // let go: where it is now, written
        const DeveloperTools::PartDrag g = *d.partDrag;
        d.partDrag.reset();
        const float px = std::round(g.px + (d.mouseX - g.x) / fs), py = std::round(g.py + (d.mouseY - g.y) / fs);
        if (px != g.px || py != g.py) {
            auto fields = d.editor.fields("atlas", def);
            const std::string n = g.mouth ? "Mouth" : "Eyes";
            for (auto& f : fields)
                if (f.name == n + "' x") f.value = std::format("{}", std::lround(px));
                else if (f.name == n + "' y") f.value = std::format("{}", std::lround(py));
            d.editor.setDefinition(def, d.editor.linesOf("atlas", fields), std::format("atlas-part:{}", g.key));
        }
    }
    if (parts.empty()) ui.textAround(a->expressions.empty() ? "" : a->expressions[d.atlasExpression], right.x + 6 * k, right.y + 12 * k, layout::sampleText, theme::text);
    else ui.textAround("Drag the eyes or the mouth into place"_tr, box.x + 2 * pad + half, box.y + box.h - pad - 9 * k, layout::sampleText, theme::dim);
    draw::outline(r, box, kEdge);
}

namespace {

// The piece of a picture at a point (in its pixels): the box around the pixels joined to it (gaps of a
// pixel bridged), on a see-through background or one the colour of its top left corner. None on the
// background.
std::optional<SDL_FRect> pieceAt(SDL_Surface* pixels, SDL_FPoint at) {
    if (!pixels || pixels->format != SDL_PIXELFORMAT_RGBA32) return std::nullopt;
    const int w = pixels->w, h = pixels->h, x0 = static_cast<int>(at.x), y0 = static_cast<int>(at.y);
    if (x0 < 0 || y0 < 0 || x0 >= w || y0 >= h) return std::nullopt;
    const auto* bytes = static_cast<const Uint8*>(pixels->pixels);
    auto pixel = [&](int x, int y) { return bytes + static_cast<std::ptrdiff_t>(y) * pixels->pitch + x * 4; };
    bool seeThrough = false;
    for (int y = 0; y < h && !seeThrough; ++y)
        for (int x = 0; x < w && !seeThrough; ++x) seeThrough = pixel(x, y)[3] < 255;
    const Uint8* corner = pixel(0, 0);
    auto solid = [&](int x, int y) {
        const Uint8* c = pixel(x, y);
        return seeThrough ? c[3] >= 24 : std::abs(c[0] - corner[0]) + std::abs(c[1] - corner[1]) + std::abs(c[2] - corner[2]) > 48;
    };
    if (!solid(x0, y0)) return std::nullopt;
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    std::vector<int> next{y0 * w + x0};
    seen[static_cast<std::size_t>(next.back())] = 1;
    int left = x0, top = y0, right = x0, bottom = y0;
    while (!next.empty()) {
        const int i = next.back();
        next.pop_back();
        const int x = i % w, y = i / w;
        left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                std::uint8_t& s = seen[static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx)];
                if (s || !solid(nx, ny)) continue;
                s = 1;
                next.push_back(ny * w + nx);
            }
    }
    return SDL_FRect{static_cast<float>(left), static_cast<float>(top), static_cast<float>(right - left + 1), static_cast<float>(bottom - top + 1)};
}

} // namespace

namespace {

constexpr std::pair<std::string_view, Ease> kEaseNames[]{{"linear", Ease::Linear}, {"in", Ease::In},         {"out", Ease::Out},
                                                         {"inout", Ease::InOut},   {"bounce", Ease::Bounce}, {"spring", Ease::Spring},
                                                         {"overshoot", Ease::Overshoot}};

bool byAmount(std::string_view key) { return key == "left" || key == "right" || key == "up" || key == "down"; }

// A new change's easing on the timeline's row `row`: a fade's is even (as the scripts have it), the rest smooth.
std::string_view usualEase(std::size_t row) { return wordsOf(kTimelineRows[row].keys).front() == "fade" ? "linear" : "inout"; }

// A timeline's changes as the game plays them, `times` times (0: again and again). (A value that
// doesn't read: as if it didn't change.)
Animation animationOf(const StoryEditor::Timeline& t, int times, const Palette& palette) {
    using V = Animation::Value;
    Animation a{.length = t.length, .times = times};
    for (const StoryEditor::Bar& b : t.bars) {
        const auto ease = std::ranges::find(kEaseNames, b.ease, &std::pair<std::string_view, Ease>::first);
        Animation::Change c{.start = b.start, .seconds = b.seconds, .ease = ease != std::end(kEaseNames) ? ease->second : Ease::InOut};
        const auto n = parseNumber(b.value);
        if (b.key == "x" || b.key == "y") {
            c.what = b.key == "x" ? V::X : V::Y;
            c.to = n && lower(b.value) != "home" ? *n : std::numeric_limits<float>::quiet_NaN();
        } else if (byAmount(b.key)) {
            c.what = b.key == "left" || b.key == "right" ? V::X : V::Y;
            c.by = true;
            c.to = (b.key == "left" || b.key == "down" ? -1.0f : 1.0f) * n.value_or(0.0f);
        } else if (b.key == "wipe") {
            c.what = V::Wipe;
            c.to = std::clamp(n.value_or(1.0f), 0.0f, 1.0f);
            c.shape = b.shape;
        } else if (b.key == "color" || b.key == "fade") {
            c.what = b.key == "color" ? V::Color : V::Fade;
            const SDL_Color usual = b.key == "fade" ? SDL_Color{0, 0, 0, 255} : SDL_Color{255, 255, 255, 255};
            c.color = b.key == "fade" && lower(b.value) == "none" ? SDL_Color{0, 0, 0, 0} : parseColor(b.value, &palette).value_or(usual);
        } else {
            c.what = b.key == "opacity" ? V::Opacity : b.key == "zoom" ? V::Zoom : V::Rotate;
            c.to = n.value_or(b.key == "rotate" ? 0.0f : 1.0f);
        }
        a.changes.push_back(c);
    }
    std::ranges::stable_sort(a.changes, {}, &Animation::Change::start);
    return a;
}

// What a change goes to as its box shows it: an amount with its way (5% left), a percent as one.
std::string barShown(const StoryEditor::Bar& b) {
    if (byAmount(b.key)) return percentText(b.value) + " " + b.key;
    if (b.key == "opacity" || b.key == "zoom" || b.key == "wipe") return percentText(b.value);
    return b.value;
}

// ...and back: what's written for a value typed into it, on the timeline's row `row` ("5% left" across: left=5%).
StoryEditor::Bar barWritten(StoryEditor::Bar b, std::size_t row, std::string_view typed) {
    const auto keys = wordsOf(kTimelineRows[row].keys);
    b.key = std::string(keys.front());
    b.value = std::string(trim(typed));
    if (const auto w = rawWords(b.value); w.size() > 1)
        for (const std::string_view way : std::span(keys).subspan(1))
            if (lower(w.back()) == way) {
                b.key = std::string(way);
                b.value = percentText(joined(std::vector<std::string>(w.begin(), w.end() - 1)));
            }
    if (b.key == "opacity" || b.key == "zoom" || b.key == "wipe") b.value = percentText(b.value);
    if (b.key != "wipe") b.shape.clear();
    return b;
}


float lastEnd(const std::vector<StoryEditor::Bar>& bars) {
    float last = 0.0f;
    for (const StoryEditor::Bar& b : bars) last = std::max(last, b.start + b.seconds);
    return last;
}

// How many seconds the timeline shows across: a round number, a little past `seconds`.
float roundSpan(float seconds) {
    for (const float step : {0.5f, 1.0f, 2.0f, 5.0f, 10.0f})
        if (seconds <= step * 10.0f) return std::ceil(seconds / step) * step;
    return std::ceil(seconds / 30.0f) * 30.0f;
}

// The animation written again with `t` (its own line as it is; one time through as long as it was after
// its last change, or as `t` says when that's longer), and the change `chosen` still chosen.
void writeTimeline(DeveloperTools& d, const StoryEditor::Definition& def, StoryEditor::Timeline t, const std::optional<StoryEditor::Bar>& chosen,
                   const std::string& key) {
    t.length = std::max(t.length, lastEnd(t.bars));
    const std::vector<std::string> now = d.editor.linesOf(def);
    std::vector<std::string> lines{now.front()}; // (its own line; the notes under it, on lines of their own or after one, under that)
    for (std::size_t i = 1; i < now.size(); ++i)
        if (const std::string_view note = trim(splitNote(now[i]).second); !note.empty()) lines.emplace_back(note);
    const std::vector<std::string> steps = StoryEditor::stepsOf(t);
    lines.insert(lines.end(), steps.begin(), steps.end());
    d.animation.bar.reset();
    if (chosen) { // (where it is in the order they're in now)
        const auto again = StoryEditor::timelineOf(steps).bars;
        for (std::size_t i = 0; i < again.size(); ++i)
            if (again[i].key == chosen->key && again[i].value == chosen->value && sameMoment(again[i].start, chosen->start) && sameMoment(again[i].seconds, chosen->seconds))
                d.animation.bar = i;
    }
    d.editor.setDefinition(def, lines, key);
}

} // namespace

void Developer::drawAnimation(DeveloperUi& ui, const StoryEditor::Definition& given, const SDL_FRect& room) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Played = DeveloperTools::AnimationView;
    using Field = StoryEditor::Field;
    const StoryEditor::Definition def = given; // (a copy: writing it makes the Library's list again)
    Played& v = d.animation;
    const float k = ui.k, buttonH = layout::buttonH * k, boxH = layout::boxH * k, rowGap = layout::rowGap * k, gap = layout::gap * k;
    const std::vector<std::string> lines = editor.linesOf(def);
    const StoryEditor::Timeline timeline = StoryEditor::timelineOf(std::span(lines).subspan(1));
    auto fields = editor.fields("animation", def);
    const std::string plays = fieldValue(fields, "Plays");
    if (v.name != def.name) v = Played{.name = def.name, .playedAt = engine.toolsTime()};
    if (v.bar && *v.bar >= timeline.bars.size()) v.bar.reset();
    // The playhead goes on while it plays: round again after the end (a moment after, unless it plays again and again).
    const float now = engine.toolsTime();
    if (v.playing && v.drag != Played::Drag::Playhead) {
        v.playhead += now - v.playedAt;
        if (v.playhead > timeline.length + (plays == "loop" ? 0.0f : 0.6f)) v.playhead = 0.0f;
    }
    v.playedAt = now;

    // Its preview, under a heading with Play or Pause (as every preview has its buttons), then the rest
    // under it (that scrolls).
    const float sampleH = std::round(std::min(layout::sampleAnimationH * k, room.h * layout::sampleAnimationPart));
    const SDL_FRect headRow{room.x, room.y, room.w, layout::headingH * k};
    PreviewHead head(ui, headRow);
    if (head.button(v.playing ? "Pause"_tr : "Play"_tr, false, timeline.length > 0.0f)) {
        v.playing = !v.playing;
        if (v.playing && v.playhead >= timeline.length) v.playhead = 0.0f;
    }
    const std::string at = secondsText(std::min(v.playhead, timeline.length)), of = secondsText(timeline.length);
    head.title(of == "1" ? "Preview ({} of 1 second)"_tr(at) : "Preview ({} of {} seconds)"_tr(at, of));
    const SDL_FRect shown = drawAnimationSample(ui, animationOf(timeline, 1, engine.palette()), std::min(v.playhead, timeline.length),
                                                {room.x, room.y + layout::previewHeadH * k, room.w, sampleH});
    const float top = shown.y + shown.h + rowGap;
    const SDL_FRect formBox{room.x, top, room.w, room.y + room.h - top};
    ui.scrollForm(formBox, boxH + rowGap);
    float y = formBox.y - d.formScroll;
    const DeveloperUi::Clip clipped(ui, formBox, true);
    const float labelW = std::round(room.w * layout::labelPart);
    for (std::size_t l = def.line; l < def.end; ++l)
        if (const ScriptError* problem = d.problemAt(def.file, l)) y += ui.text(problem->message, room.x, y, layout::noteText, kBad, room.w).y + rowGap;

    // Its name and how often it plays.
    const std::string id = std::format("def:animation:{}", def.line);
    if (const auto edit = drawForm(ui, id, fields, room.x, room.w, labelW, y)) {
        auto changed = fields;
        std::ranges::find(changed, edit->name, &Field::name)->value = edit->value;
        std::vector<std::string> written = editor.linesOf("animation", changed);
        written.insert(written.end(), lines.begin() + 1, lines.end()); // (the lines under it as they are)
        editor.setDefinition(def, written, edit->typed ? std::format("{}:{}{}", id, edit->name, d.focusCount) : std::string{});
        if (!edit->typed) d.focus.clear();
        return; // (written: drawn again from it next frame, as tall as it was)
    }
    y += rowGap;

    // Its timeline (a panel of its own, as a list is), with + New change under it (at the playhead: on the
    // chosen change's row, else Opacity's).
    ui.heading("Timeline"_tr, room.x, y);
    y += layout::headingH * k;
    if (!timeline.ok) // (a line it can't show: the timeline only shows it, until that's fixed as text)
        y += ui.text("The timeline can't change it while a line under it has a mistake."_tr, room.x, y, layout::noteText, kBad, room.w).y + gap;
    const float drawn = drawTimeline(ui, def, timeline, room.x, y, room.w);
    if (drawn < 0.0f) return; // (written: drawn again from it next frame, as tall as it was)
    y += drawn + gap;
    const float after = std::max(0.0f, timeline.length - lastEnd(timeline.bars)); // (time after its last change: kept as changes move)
    if (ui.button({room.x, y, room.w, buttonH}, "+ New change"_tr, false, timeline.ok, layout::buttonText)) {
        const std::size_t row = v.bar ? StoryEditor::timelineRow(timeline.bars[*v.bar].key) : 2;
        StoryEditor::Bar bar{.key = std::string(wordsOf(kTimelineRows[row].keys).front()), .value = std::string(kTimelineRows[row].start),
                             .start = std::round(std::min(v.playhead, timeline.length) * 20.0f) / 20.0f, .seconds = 0.5f, .ease = std::string(usualEase(row))};
        StoryEditor::Timeline added = timeline;
        added.bars.push_back(bar);
        added.length = lastEnd(added.bars) + after;
        writeTimeline(d, def, added, bar, {});
        return;
    }
    y += buttonH + rowGap;

    // The change chosen on it, as a group of the form (what it changes is its heading); else how the timeline works.
    if (!v.bar || !timeline.ok) {
        y += ui.text("Click a change to change it, or add one with + New change or by double-clicking a row. Drag a change to move it, or its ends to change how long it takes; drag the orange line to leave time at the end."_tr,
                     room.x, y, layout::noteText, theme::dim, room.w).y;
        ui.formDrawn(formBox, y);
        return;
    }
    const StoryEditor::Bar& b = timeline.bars[*v.bar];
    const std::size_t row = StoryEditor::timelineRow(b.key);
    std::vector<Field> change{
        {.name = "To"_text, .value = barShown(b), .box = kTimelineRows[row].box, .suggest = std::string(kTimelineRows[row].more), .group = std::string(kTimelineRows[row].label)},
        {.name = "Starts at"_text, .value = secondsText(b.start), .label = "Starts at (seconds)"_text, .box = StoryEditor::Box::Seconds, .hint = "0"},
        {.name = "Duration"_text, .value = b.seconds > 0.0f ? secondsText(b.seconds) : std::string{}, .label = "Duration (seconds)"_text, .box = StoryEditor::Box::Seconds,
         .hint = "At once"_hint, .beside = true},
        {.name = "Easing"_text, .value = b.ease, .box = StoryEditor::Box::Easing}};
    if (b.key == "wipe") // (where it shows first: the picture's darkest parts)
        change.insert(change.begin() + 1, {.name = "Shape"_text, .value = b.shape, .box = StoryEditor::Box::Shape, .hint = "None"_hint});
    const std::string changeId = std::format("{}:change", id);
    if (const auto edit = drawForm(ui, changeId, change, room.x, room.w, labelW, y)) {
        StoryEditor::Bar changed = b;
        if (edit->name == "To") changed = barWritten(changed, row, edit->value);
        else if (edit->name == "Shape") changed.shape = std::string(trim(edit->value));
        else if (edit->name == "Starts at") changed.start = std::max(0.0f, parseNumber(edit->value).value_or(0.0f));
        else if (edit->name == "Duration") changed.seconds = std::max(0.0f, parseNumber(edit->value).value_or(0.0f));
        else if (edit->name == "Easing") changed.ease = edit->value;
        StoryEditor::Timeline t = timeline;
        t.bars[*v.bar] = changed;
        t.length = lastEnd(t.bars) + after;
        writeTimeline(d, def, t, changed, edit->typed ? std::format("{}:{}{}", changeId, edit->name, d.focusCount) : std::string{});
        if (!edit->typed) d.focus.clear();
        return;
    }
    ui.formDrawn(formBox, y);
}

float Developer::drawTimeline(DeveloperUi& ui, const StoryEditor::Definition& def, const StoryEditor::Timeline& timeline, float x, float y, float w) {
    DeveloperTools& d = tools;
    SDL_Renderer* r = ui.r;
    using Played = DeveloperTools::AnimationView;
    Played& v = d.animation;
    const float k = ui.k;
    const float labelW = layout::timelineLabelW * k, rulerH = layout::timelineRulerH * k, rowH = layout::timelineRowH * k;
    const SDL_FRect grid{x, y, w, rulerH + rowH * static_cast<float>(StoryEditor::kTimelineRowCount)};
    const float trackX = x + labelW, trackW = std::max(1.0f, w - labelW);
    // How many seconds it shows across: all of it and a little more, in a round number (kept while something's dragged).
    if (v.drag == Played::Drag::None || v.span <= 0.0f) v.span = roundSpan(std::max(1.0f, std::max(timeline.length, lastEnd(timeline.bars)) * 1.15f));
    const float span = v.span, perSecond = trackW / span;
    auto xOf = [&](float t) { return trackX + t * perSecond; };
    const float pointer = std::max(0.0f, (d.mouseX - trackX) / perSecond);
    // Where a dragged moment goes: onto one near it (the start, the end, the playhead, another change's
    // ends), else to a twentieth of a second.
    auto snap = [&](float t, std::optional<std::size_t> skip) {
        float best = std::round(t * 20.0f) / 20.0f, within = layout::timelineSnap * k / perSecond;
        auto near = [&](float m) {
            if (std::abs(m - t) < within) best = m, within = std::abs(m - t);
        };
        near(0.0f);
        near(v.playhead);
        near(timeline.length);
        for (std::size_t i = 0; i < timeline.bars.size(); ++i)
            if (i != skip) near(timeline.bars[i].start), near(timeline.bars[i].start + timeline.bars[i].seconds);
        return std::max(0.0f, best);
    };
    // The changes, and the end, as they are dragged.
    std::vector<StoryEditor::Bar> bars = timeline.bars;
    float length = timeline.length;
    const bool barDrag = v.drag == Played::Drag::Bar || v.drag == Played::Drag::Start || v.drag == Played::Drag::End;
    if (barDrag && v.bar && *v.bar < bars.size()) {
        StoryEditor::Bar& b = bars[*v.bar];
        const float moved = pointer - v.grab, end = v.was.start + v.was.seconds;
        if (v.drag == Played::Drag::Bar) b.start = snap(v.was.start + moved, v.bar);
        else if (v.drag == Played::Drag::End) b.seconds = std::max(0.0f, snap(end + moved, v.bar) - b.start);
        else b.start = std::min(end, snap(v.was.start + moved, v.bar)), b.seconds = end - b.start;
        length = lastEnd(bars) + std::max(0.0f, timeline.length - lastEnd(timeline.bars));
    }
    if (v.drag == Played::Drag::Length) length = std::max(lastEnd(bars), snap(v.wasLength + pointer - v.grab, std::nullopt));
    if (v.drag == Played::Drag::Playhead) v.playhead = std::clamp(pointer, 0.0f, span);

    // The rows, the seconds over them, and what's past the end of one time through (darker).
    draw::rect(r, grid, kBox);
    for (std::size_t row = 0; row < StoryEditor::kTimelineRowCount; ++row) {
        const float top = y + rulerH + rowH * static_cast<float>(row);
        if (row % 2 == 1) draw::rect(r, {trackX, top, trackW, rowH}, {255, 255, 255, 8});
        const std::string label(tr(kTimelineRows[row].label));
        ui.textAround(ui.fitted(label, layout::smallText, labelW - 14 * k), x + 8 * k, top + rowH / 2, layout::smallText, theme::dim);
    }
    if (xOf(length) < trackX + trackW) draw::rect(r, {xOf(length), y + rulerH, trackX + trackW - xOf(length), grid.h - rulerH}, {0, 0, 0, 110});
    float step = 30.0f;
    for (const float s : {0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f})
        if (s * perSecond >= layout::timelineTickW * k) {
            step = s;
            break;
        }
    for (int i = 0; static_cast<float>(i) * step <= span + 0.0001f; ++i) {
        const float tx = xOf(static_cast<float>(i) * step);
        draw::rect(r, {tx, y + rulerH - 6 * k, 1.0f, 6 * k}, kEdge);
        draw::rect(r, {tx, y + rulerH, 1.0f, grid.h - rulerH}, {255, 255, 255, 10});
        ui.textAround(secondsText(static_cast<float>(i) * step), tx + 6 * k, y + rulerH / 2 + 1 * k, layout::sampleText, theme::dim);
    }
    draw::rect(r, {trackX, y + rulerH, trackW, 1.0f}, kEdge);
    draw::rect(r, {trackX - 1.0f, y, 1.0f, grid.h}, kEdge);

    // The changes: a bar from when one begins to when it ends, saying what it goes to; a diamond (over
    // the bars) for one at once, saying it too where there's room before the next on its row.
    const float edge = layout::timelineEdge * k, diamond = layout::timelineDiamond * k;
    std::vector<SDL_FRect> boxes(bars.size());
    auto rowTop = [&](const StoryEditor::Bar& b) {
        return y + rulerH + rowH * static_cast<float>(std::min(StoryEditor::timelineRow(b.key), StoryEditor::kTimelineRowCount - 1));
    };
    auto atOnce = [&](const StoryEditor::Bar& b) { return b.seconds <= 0.0f; };
    auto say = [&](const StoryEditor::Bar& b, float at, float mid, float room, SDL_Color color) { // (a colour: its square and its name, as a colour box shows it)
        const bool colour = (b.key == "color" || b.key == "fade") && lower(b.value) != "none";
        if (const auto c = colour ? parseColor(b.value, &engine.palette()) : std::nullopt; c && room > layout::timelineSwatch * k + 16 * k) {
            const float after = ui.swatch(at, mid, *c, false, std::nullopt, layout::timelineSwatch);
            room -= after - at;
            at = after;
        }
        if (room > 16 * k) ui.textAround(ui.fitted(colour ? colorWords(b.value, &engine.palette()) : barShown(b), layout::sampleText, room), at, mid, layout::sampleText, color);
    };
    for (const bool instant : {false, true})
        for (std::size_t i = 0; i < bars.size(); ++i) {
            const StoryEditor::Bar& b = bars[i];
            if (atOnce(b) != instant) continue;
            const float top = rowTop(b), mid = top + rowH / 2;
            const bool chosen = v.bar == i;
            const SDL_Color fill = withAlpha(kAccent, chosen ? 0.9f : 0.45f);
            std::optional<float> next; // (where the next change on its row begins, after this one)
            for (const StoryEditor::Bar& o : bars)
                if (&o != &b && rowTop(o) == top && o.start > b.start + 0.001f) next = std::min(next.value_or(o.start), o.start);
            const float room = (next ? xOf(*next) : trackX + trackW) - 6 * k;
            if (!instant) {
                boxes[i] = {xOf(b.start), top + 4 * k, std::max(2 * k, b.seconds * perSecond), rowH - 8 * k};
                draw::rect(r, boxes[i], fill);
                draw::outline(r, boxes[i], chosen ? theme::text : kEdge);
                const bool marked = std::ranges::any_of(bars, [&](const StoryEditor::Bar& o) { return atOnce(o) && rowTop(o) == top && sameMoment(o.start, b.start); });
                const float textX = boxes[i].x + (marked ? diamond / 2 + 4 * k : 4 * k);
                say(b, textX, mid, boxes[i].x + boxes[i].w - 4 * k - textX, chosen ? kBox : theme::text);
            } else {
                const float cx = xOf(b.start), h = diamond / 2;
                boxes[i] = {cx - h, mid - h, diamond, diamond};
                draw::quad(r, {cx, mid - h - 1.0f}, {cx + h + 1.0f, mid}, {cx, mid + h + 1.0f}, {cx - h - 1.0f, mid}, kBox); // (an edge around it, over a bar)
                draw::quad(r, {cx, mid - h}, {cx + h, mid}, {cx, mid + h}, {cx - h, mid}, chosen ? theme::text : withAlpha(kAccent, 0.8f));
                const bool covered = std::ranges::any_of(bars, [&](const StoryEditor::Bar& o) {
                    return !atOnce(o) && rowTop(o) == top && o.start <= b.start + 0.001f && o.start + o.seconds > b.start + 0.001f;
                });
                if (!covered) say(b, cx + h + 4 * k, mid, room - (cx + h + 4 * k), theme::dim);
            }
        }
    // The chosen change: an x after it (before it, at the end of the row), as a list's chosen item has;
    // clicked, it says Delete, and clicked again the change goes.
    const std::string armId = v.bar ? std::format("timeline:{}:{}", def.line, *v.bar) : std::string{};
    std::optional<SDL_FRect> cross;
    if (v.bar && *v.bar < bars.size() && timeline.ok && v.drag == Played::Drag::None) {
        const bool armed = d.armed == armId;
        const SDL_FRect& at = boxes[*v.bar];
        const float xw = (armed ? layout::crossArmedW : layout::crossW) * k, h = rowH - 6 * k, mid = at.y + at.h / 2;
        cross = SDL_FRect{at.x + at.w + 3 * k, mid - h / 2, xw, h};
        if (cross->x + xw > trackX + trackW) cross->x = at.x - 3 * k - xw;
        draw::rect(r, *cross, armed ? SDL_Color{150, 50, 64, 255} : ui.over(*cross) ? kButtonHover : kButton);
        draw::outline(r, *cross, armed ? kBad : kEdge);
        ui.textIn(armed ? std::string_view("Delete"_tr) : std::string_view("x"), *cross, layout::smallButtonText, theme::text);
    }

    // The end of one time through (its handle on the seconds, to drag), and the playhead.
    const float endX = xOf(length), playX = xOf(std::clamp(v.playhead, 0.0f, span));
    draw::rect(r, {endX - 1.0f, y, 2.0f, grid.h}, withAlpha(kWarn, 0.7f));
    const SDL_FRect endHandle{endX - edge, y, 2 * edge, rulerH};
    draw::quad(r, {endX - 5 * k, y}, {endX + 5 * k, y}, {endX + 1.0f, y + 8 * k}, {endX - 1.0f, y + 8 * k}, kWarn);
    draw::rect(r, {playX - 1.0f, y, 2.0f, grid.h}, theme::text);
    draw::quad(r, {playX - 5 * k, y}, {playX + 5 * k, y}, {playX + 1.0f, y + 7 * k}, {playX - 1.0f, y + 7 * k}, theme::text);
    draw::outline(r, grid, kEdge);

    // What the pointer takes: the end's handle, the seconds (the playhead), a change (or one of its ends),
    // or a row: once to put the playhead there, twice to add a change there.
    if (v.drag == Played::Drag::None && timeline.ok) {
        if (cross && ui.over(*cross) && ui.click(*cross)) {
            if (d.armed != armId) {
                d.armed = armId;
                return grid.h;
            }
            d.armed.clear();
            StoryEditor::Timeline t = timeline;
            t.bars.erase(t.bars.begin() + static_cast<std::ptrdiff_t>(*v.bar));
            t.length = lastEnd(t.bars) + std::max(0.0f, timeline.length - lastEnd(timeline.bars));
            writeTimeline(d, def, t, std::nullopt, {});
            return -1.0f;
        }
        if (d.clicked && ui.over(grid) && d.armed.starts_with("timeline:")) d.armed.clear(); // (a click anywhere else on it: the x again)
        if (ui.over(endHandle) && ui.click(endHandle)) {
            v.drag = Played::Drag::Length;
            v.grab = pointer;
            v.wasLength = length;
        } else if (const SDL_FRect ruler{trackX, y, trackW, rulerH}; ui.click(ruler)) {
            v.drag = Played::Drag::Playhead;
            v.playing = false;
            v.playhead = std::clamp(pointer, 0.0f, span);
        } else {
            for (std::size_t i = bars.size(); i-- > 0;) {
                const SDL_FRect hit{boxes[i].x - 2 * k, boxes[i].y - 3 * k, boxes[i].w + 4 * k, boxes[i].h + 6 * k};
                if (!ui.over(hit) || !ui.click(hit)) continue;
                const bool timed = bars[i].seconds > 0.0f && boxes[i].w > 3 * edge;
                v.drag = timed && d.mouseX > boxes[i].x + boxes[i].w - edge ? Played::Drag::End : timed && d.mouseX < boxes[i].x + edge ? Played::Drag::Start : Played::Drag::Bar;
                v.bar = i;
                v.was = bars[i];
                v.grab = pointer;
                d.focus.clear();
                break;
            }
            for (std::size_t row = 0; v.drag == Played::Drag::None && row < StoryEditor::kTimelineRowCount; ++row) {
                const SDL_FRect lane{trackX, y + rulerH + rowH * static_cast<float>(row), trackW, rowH};
                if (!ui.click(lane)) continue;
                if (d.doubleClick) { // (a change there, going to what a new one on the row does, for half a second)
                    StoryEditor::Bar bar{.key = std::string(wordsOf(kTimelineRows[row].keys).front()), .value = std::string(kTimelineRows[row].start),
                                         .start = snap(pointer, std::nullopt), .seconds = 0.5f, .ease = std::string(usualEase(row))};
                    StoryEditor::Timeline t = timeline;
                    t.bars.push_back(bar);
                    t.length = lastEnd(t.bars) + std::max(0.0f, timeline.length - lastEnd(timeline.bars));
                    writeTimeline(d, def, t, bar, {});
                    return -1.0f;
                }
                v.bar.reset();
                v.drag = Played::Drag::Playhead;
                v.playing = false;
                v.playhead = std::clamp(pointer, 0.0f, span);
            }
        }
    }
    // Let go: a change written where it was taken to, or the end.
    if (v.drag != Played::Drag::None && !d.held) {
        const Played::Drag was = std::exchange(v.drag, Played::Drag::None);
        StoryEditor::Timeline t = timeline;
        t.bars = bars;
        t.length = length;
        const bool moved = barDrag && v.bar && *v.bar < bars.size() && (!sameMoment(bars[*v.bar].start, v.was.start) || !sameMoment(bars[*v.bar].seconds, v.was.seconds));
        if (moved || (was == Played::Drag::Length && !sameMoment(length, timeline.length))) {
            writeTimeline(d, def, t, v.bar ? std::optional(bars[*v.bar]) : std::nullopt, {});
            return -1.0f;
        }
    }
    // The keys: Delete takes the chosen change away, Esc lets it go.
    if (v.bar && *v.bar < bars.size() && d.focus.empty() && timeline.ok) {
        bool removed = false;
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            if (key.key == SDLK_ESCAPE) {
                v.bar.reset();
                return true;
            }
            if ((key.key == SDLK_DELETE || key.key == SDLK_BACKSPACE) && !removed && v.bar) {
                removed = true;
                return true;
            }
            return false;
        });
        if (removed) {
            StoryEditor::Timeline t = timeline;
            t.bars.erase(t.bars.begin() + static_cast<std::ptrdiff_t>(*v.bar));
            t.length = lastEnd(t.bars) + std::max(0.0f, timeline.length - lastEnd(timeline.bars));
            writeTimeline(d, def, t, std::nullopt, {});
            return -1.0f;
        }
    }
    return grid.h;
}

// How an animation looks `seconds` into it: a square (a fifth of the game's screen high) in the middle
// of a small screen of the game's size, as a part there would. Gives where it's drawn.
SDL_FRect Developer::drawAnimationSample(DeveloperUi& ui, const Animation& a, float seconds, const SDL_FRect& room) {
    SDL_Renderer* r = ui.r;
    const float side = std::round(engine.canvas().h / 5); // (in the game's pixels)
    const SDL_FRect part{(engine.canvas().w - side) / 2, (engine.canvas().h - side) / 2, side, side};
    const float scale = std::min(room.w / engine.canvas().w, room.h / engine.canvas().h);
    const SDL_FRect box{room.x, room.y, std::round(engine.canvas().w * scale), std::round(engine.canvas().h * scale)};
    draw::rect(r, box, kBox);
    const Animation::Look look = a.at(seconds, kAccent, [&](bool across, float place) {
        return across ? placeEdge(place, side, engine.canvas().w) - part.x : placeEdge(-place, side, engine.canvas().h) - part.y;
    }, {engine.canvas().w, engine.canvas().h});
    std::optional<DeveloperUi::Clip> clipped(std::in_place, ui, box);
    const float size = side * scale * look.zoom, cx = box.x + (part.x + side / 2 + look.x) * scale, cy = box.y + (part.y + side / 2 + look.y) * scale;
    const float turn = look.rotate * std::numbers::pi_v<float> / 180.0f, c = std::cos(turn), sn = std::sin(turn);
    auto corner = [&](float x, float y) { return SDL_FPoint{cx + (x * c - y * sn) * size / 2, cy + (x * sn + y * c) * size / 2}; };
    SDL_Color color = look.color.value_or(kAccent);
    color.a = static_cast<Uint8>(std::lround(color.a * std::clamp(look.opacity, 0.0f, 1.0f)));
    int viewW = 0, viewH = 0; // (what the tools' places are in)
    SDL_RendererLogicalPresentation mode{};
    SDL_GetRenderLogicalPresentation(r, &viewW, &viewH, &mode);
    tools.sampleWiper.draw(r, engine.assets(), {static_cast<float>(viewW), static_cast<float>(viewH)}, {cx - size / 2, cy - size / 2, size, size}, look.wipe,
                           look.wipeShape, [&] { draw::quad(r, corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1), color); });
    if (look.fade > 0.0f) // (fade=: the whole screen covered, as much as it's seen, as a screen's is)
        draw::rect(r, box, withAlpha(look.fadeColor, std::clamp(look.fade, 0.0f, 1.0f) * std::clamp(look.opacity, 0.0f, 1.0f)));
    clipped.reset(); // (its outline: over the edge)
    draw::outline(r, box, kEdge);
    return box;
}

// + New animation: the animations to start from, as + New screen's screens are listed, with the one pointed
// at (else the last one pointed at) playing over them, and what it does. A click adds it, named as it is
// (fade_in_2 when there's one already), and opens it.
void Developer::drawNewAnimationForm(DeveloperUi& ui, const SDL_FRect& right) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k, buttonH = layout::buttonH * k, gap = layout::gap * k;
    ui.formTitle("New animation"_tr, right.x, right.y);
    float y = right.y + layout::titleH * k;
    const AnimationStart& shown = kAnimationStarts[std::min(d.startShown, kAnimationStarts.size() - 1)];
    const StoryEditor::Timeline timeline = StoryEditor::timelineOf(startLines(shown));
    const float now = engine.toolsTime(), round = timeline.length + (shown.loops ? 0.0f : 0.6f); // (a moment after its end, then again)
    const float at = round > 0.0f ? std::fmod(std::max(0.0f, now - d.startShownAt), round) : 0.0f;
    const float sampleH = std::round(std::min(layout::sampleAnimationH * k, right.h * layout::sampleAnimationPart));
    const SDL_FRect sample = drawAnimationSample(ui, animationOf(timeline, 1, engine.palette()), std::min(at, timeline.length), {right.x, y, right.w, sampleH});
    y = sample.y + sample.h + layout::rowGap * k;
    ui.text(tr(shown.help), right.x, y, layout::noteText, theme::dim, right.w);
    y += ui.measure("A\nA", layout::noteText).y + gap; // (room for two lines, whichever it says: the list stays where it is)
    const SDL_FRect list{right.x, y, right.w, right.y + right.h - y};
    ui.scrollForm(list, buttonH + gap);
    y = list.y - d.formScroll;
    const DeveloperUi::Clip clipped(ui, list, true);
    auto has = [&](const std::string& n) { return editor.defines("animation", n); };
    for (std::size_t i = 0; i < kAnimationStarts.size(); ++i) {
        const AnimationStart& start = kAnimationStarts[i];
        const SDL_FRect button{right.x, y, right.w, buttonH};
        if (ui.over(button) && ui.over(list) && d.startShown != i) d.startShown = i, d.startShownAt = now;
        if (ui.button(button, tr(start.label), false, true, layout::buttonText)) {
            const std::string name = uniqueName(start.name.empty() ? std::string("new_animation") : std::string(start.name), has);
            std::vector<std::string> lines{std::format("animation {}{}", name, start.loops ? " loop" : "")};
            std::ranges::copy(StoryEditor::stepsOf(StoryEditor::timelineOf(startLines(start))), std::back_inserter(lines)); // (as the timeline writes them)
            d.libraryItem = editor.addDefinition("animation", lines);
            d.pickingAnimation = false;
            d.formScroll = 0.0f;
            return;
        }
        y += buttonH + gap;
    }
    ui.formDrawn(list, y);
}

// A sub-picture over its form: on the left the picture it's cut from, with the part marked (drag a
// box on it, or click a piece of it to cut out just that piece), and on the right the part.
void Developer::drawSubPicture(DeveloperUi& ui, const StoryEditor::Definition& def, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    draw::rect(r, box, kBox);
    const SubPicture* p = nullptr;
    if (d.script)
        for (const SubPicture& each : d.script->subPictures)
            if (each.name == lower(def.name)) p = &each;
    const Picture sheet = p ? engine.assets().whole(p->image) : Picture{};
    if (!p || !sheet) {
        ui.textAround(p ? "There's no image called '{}'."_tr(p->image) : "It shows once the scripts' mistakes are fixed."_tr(), box.x + 10 * k,
                      box.y + box.h / 2, layout::noteText, theme::dim, box.w - 20 * k);
        draw::outline(r, box, kEdge);
        return;
    }
    const auto fit = fitIn;
    const float pad = 8 * k, hintH = 18 * k, leftW = std::round((box.w - 3 * pad) * 0.66f);
    const SDL_FRect at = fit({box.x + pad, box.y + pad, leftW, box.h - 2 * pad - hintH}, sheet.w(), sheet.h());
    draw::rect(r, at, {40, 40, 48, 255});
    sheet.draw(r, at, 1.0f);
    const float s = at.w / sheet.w();
    auto inPicture = [&](float x, float y) {
        return SDL_FPoint{std::round(std::clamp((x - at.x) / s, 0.0f, sheet.w())), std::round(std::clamp((y - at.y) / s, 0.0f, sheet.h()))};
    };
    // The part: as it's written, or the box being dragged.
    SDL_FRect part{};
    const SDL_FRect all{0.0f, 0.0f, sheet.w(), sheet.h()};
    if (d.cutDrag) {
        const SDL_FPoint a = d.cutDrag->from, b = inPicture(d.mouseX, d.mouseY);
        part = {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
    } else if (!SDL_GetRectIntersectionFloat(&p->rect, &all, &part)) {
        part = {};
    }
    const SDL_FRect shown{at.x + part.x * s, at.y + part.y * s, part.w * s, part.h * s};
    const SDL_Color shade{0, 0, 0, 150}; // (the rest of the picture, darker)
    draw::rect(r, {at.x, at.y, at.w, shown.y - at.y}, shade);
    draw::rect(r, {at.x, shown.y + shown.h, at.w, at.y + at.h - shown.y - shown.h}, shade);
    draw::rect(r, {at.x, shown.y, shown.x - at.x, shown.h}, shade);
    draw::rect(r, {shown.x + shown.w, shown.y, at.x + at.w - shown.x - shown.w, shown.h}, shade);
    draw::outline(r, shown, kAccent);
    if (ui.click(at)) d.cutDrag = DeveloperTools::CutDrag{.from = inPicture(d.mouseX, d.mouseY), .x = d.mouseX, .y = d.mouseY};
    if (d.cutDrag && !d.held) { // let go: a click takes the piece there, a drag the box
        const DeveloperTools::CutDrag g = *d.cutDrag;
        d.cutDrag.reset();
        std::optional<SDL_FRect> cut = part;
        if (std::abs(d.mouseX - g.x) + std::abs(d.mouseY - g.y) < 4 * k) {
            const SurfacePtr pixels = engine.assets().pixels(p->image);
            cut = pieceAt(pixels.get(), g.from);
            if (!cut) d.status = "That's the image's background: click a piece of it, or drag a box around what to cut out."_tr;
        }
        if (cut && cut->w >= 1.0f && cut->h >= 1.0f) {
            auto fields = d.editor.fields("picture", def);
            for (auto& f : fields)
                if (f.name == "Left") f.value = std::format("{}", std::lround(cut->x));
                else if (f.name == "Top") f.value = std::format("{}", std::lround(cut->y));
                else if (f.name == "Width") f.value = std::format("{}", std::lround(cut->w));
                else if (f.name == "Height") f.value = std::format("{}", std::lround(cut->h));
            d.editor.setDefinition(def, d.editor.linesOf("picture", fields), {});
            d.status.clear();
        }
    }
    // The part, as big as it fits (up to four times its size).
    if (part.w >= 1.0f && part.h >= 1.0f) {
        const SDL_FRect room{box.x + 2 * pad + leftW, box.y + pad, box.w - 3 * pad - leftW, box.h - 2 * pad - hintH};
        const float scale = std::min({4.0f, room.w / part.w, room.h / part.h});
        const SDL_FRect drawn{room.x + (room.w - part.w * scale) / 2, room.y + (room.h - part.h * scale) / 2, part.w * scale, part.h * scale};
        draw::rect(r, drawn, {40, 40, 48, 255});
        sheet.crop(part).draw(r, drawn, 1.0f);
        draw::outline(r, drawn, kEdge);
    }
    ui.textAround("Drag a box on the image, or click a piece of it to cut out just that piece. {} x {} pixels."_tr(std::lround(part.w),
                              std::lround(part.h)),
                  box.x + pad, box.y + box.h - pad - hintH / 2, layout::sampleText, theme::dim, box.w - 2 * pad);
    draw::outline(r, box, kEdge);
}

// A style's sample: a button and some words in it, as the game draws them (pointing at the button
// shows how it looks then).
void Developer::drawStyleSample(DeveloperUi& ui, const std::string& style, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    const float k = ui.k, h = engine.canvas().w * box.h / box.w; // (in the engine.canvas()'s pixels: as tall as the box is for its width)
    if (d.styleOf != std::pair{d.editor.version(), style}) { // the scripts, with a screen of them at the end of main.vnscript
        d.styleView.reset();
        d.styleScript.reset();
        d.styleOf = {d.editor.version(), style};
        const float bh = std::min(h * 0.7f, 90.0f);
        const std::vector<std::string> pixels{ // (laid out in the game's pixels, in the band the box shows; written in places, as screens are)
            "screen vnext_style_sample",
            std::format("    button \"A button\" style={} x={} y={} w={} h={}", style, std::lround(engine.canvas().w * 0.04f), std::lround((h - bh) / 2),
                        std::lround(engine.canvas().w * 0.4f), std::lround(bh)),
            std::format("    text \"Some words\" style={} x={} y={}", style, std::lround(engine.canvas().w * 0.5f), std::lround(h / 2 - 16))};
        std::string sample = "\n";
        for (const std::string& line : placesFromPixels(pixels, engine.canvas().w, engine.canvas().h)) sample += line + "\n";
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
    if (!d.styleView) {
        ui.textAround("The sample shows once the scripts' mistakes are fixed."_tr, box.x + 10 * k, box.y + box.h / 2, layout::noteText, theme::dim, box.w - 20 * k);
        return;
    }
    const float scale = box.w / engine.canvas().w;
    const SDL_Event e = mouseEventAt(SDL_EVENT_MOUSE_MOTION, ui.over(box) ? (d.mouseX - box.x) / scale : -1000.0f, ui.over(box) ? (d.mouseY - box.y) / scale : -1000.0f);
    {
        const SetFor _(engine.trying_, true); // (pointing at it only)
        d.styleView->handleEvent(e);
    }
    if (paintCanvas([&] { d.styleView->draw(engine.renderer()); })) {
        const SDL_FRect part{0.0f, 0.0f, engine.canvas().w, h};
        SDL_RenderTexture(ui.r, d.canvas.get(), &part, &box);
    }
    draw::outline(ui.r, box, kEdge);
}

} // namespace vn
