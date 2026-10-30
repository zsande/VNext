// The Translate tab: a translation's texts beside the game's, and translation sheets out and in.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

namespace vn {

namespace {

// A spreadsheet's cells (CSV: commas between them, quotes around any with a comma, quote or line
// break in it, and a quote doubled), as rows; and one cell written so.
std::vector<std::vector<std::string>> csvRows(std::string_view text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c != '"') cell += c;
            else if (i + 1 < text.size() && text[i + 1] == '"') cell += text[i++];
            else quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            row.push_back(std::exchange(cell, {}));
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            row.push_back(std::exchange(cell, {}));
            rows.push_back(std::exchange(row, {}));
        } else {
            cell += c;
        }
    }
    if (!cell.empty() || !row.empty()) {
        row.push_back(std::move(cell));
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string csvCell(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        out += c;
        if (c == '"') out += '"';
    }
    return out + "\"";
}

// A language line's name and code: language "Español" code=es.
std::pair<std::string, std::string> languageHeader(std::string_view line) {
    std::string name, code;
    const auto w = rawWords(line);
    if (w.size() > 1 && !w[1].contains('=')) name = unquote(w[1]);
    for (const std::string& word : w)
        if (lower(word).starts_with("code=")) code = word.substr(5);
    return {name, code};
}

// A translation file's name from its language's: letters and numbers, _ between words (spanish,
// portuguese_brazil); empty when nothing of it can be one.
std::string translationFileName(std::string_view name) {
    std::string out;
    for (const char c : lower(name)) {
        const auto ch = static_cast<unsigned char>(c);
        if (std::isalnum(ch) || ch >= 0x80) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return validName(out) ? out : std::string{};
}

// A list entry's name and code ("Japanese (ja)"); text without a code in parentheses: the name alone.
std::pair<std::string, std::string> languageOf(std::string_view text) {
    text = trim(text);
    if (text.ends_with(')'))
        if (const auto open = text.rfind(" ("); open != npos) {
            const std::string_view code = text.substr(open + 2, text.size() - open - 3);
            if (!code.empty() && std::ranges::all_of(code, [](char ch) { return std::isalpha(static_cast<unsigned char>(ch)) || ch == '-'; }))
                return {std::string(trim(text.substr(0, open))), std::string(code)};
        }
    return {std::string(text), {}};
}

// What the Translate tab says when texts the game doesn't show any more went from a file.
std::string tookOut(std::size_t n) { return n == 1 ? "Took out 1 text the game doesn't show any more."_tr() : "Took out {} texts the game doesn't show any more."_tr(n); }

} // namespace

// A translation file's texts: each one as written (a line in quotes at the left), and its translation
// (the first line under it, indented); a new file has each text as its own translation, to be written over.
std::vector<TextPair> textPairs(const std::vector<std::string>& lines) {
    std::vector<TextPair> pairs;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (!partLine(line) || indentOf(line) > 0) continue;
        const std::string head = lower(firstWord(line));
        if ((head == "language" || head == "font") && !trim(line).starts_with('"')) continue;
        TextPair p{.original = i, .translated = std::nullopt};
        for (std::size_t j = i + 1; j < lines.size() && (blank(lines[j]) || indentOf(lines[j]) > 0); ++j)
            if (partLine(lines[j])) {
                p.translated = j;
                break;
            }
        pairs.push_back(p);
    }
    return pairs;
}

bool untranslatedPair(const std::vector<std::string>& lines, const TextPair& p) {
    return !p.translated || unquote(trim(lines[*p.translated])).empty() || unquote(trim(lines[*p.translated])) == unquote(trim(lines[p.original]));
}

void setTranslation(std::vector<std::string>& lines, const TextPair& p, const std::string& v) { // (empty: taken away, shown as written)
    const std::string line = std::string(kIndent, ' ') + "\"" + v + "\"";
    if (v.empty() && p.translated) lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*p.translated));
    else if (p.translated) lines[*p.translated] = line;
    else if (!v.empty()) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(p.original + 1), line);
}

// A translation's file: translations/ID.vnscript in the game folder.
std::string Developer::translationPath(const std::string& id) const { return engine.gameRoot() + "translations/" + id + ".vnscript"; }

// The translation open in the Translate tab, written to its file (its language, as listed, read again).
void Developer::saveTranslation() {
    DeveloperTools& d = tools;
    const std::string text = joinedLines(d.translation.lines);
    const std::string path = translationPath(d.translation.id);
    if (!SDL_SaveFile(path.c_str(), text.data(), text.size())) {
        d.failed = true;
        d.status = "Can't save 'translations/{}.vnscript': {}."_tr(d.translation.id, SDL_GetError());
    }
    d.languageLabels.erase(path);
}

// A spreadsheet for a translator, next to the game folder (ID.csv: a column of the texts, one for their
// translations), and the translations back from it, by each text. Both say how it went.
std::string Developer::translationSheet() const { return parentFolder(engine.gameRoot()) + tools.translation.id + ".csv"; }

std::string Developer::exportTranslationSheet() {
    DeveloperTools& d = tools;
    const auto& lines = d.translation.lines;
    const std::vector<TextPair> pairs = textPairs(lines);
    std::string csv = "\xEF\xBB\xBFText,Translation\r\n";
    for (const TextPair& p : pairs)
        csv += csvCell(unquote(trim(lines[p.original]))) + "," + csvCell(untranslatedPair(lines, p) ? std::string{} : unquote(trim(lines[*p.translated]))) + "\r\n";
    const std::string sheet = translationSheet();
    d.failed = !SDL_SaveFile(sheet.c_str(), csv.data(), csv.size());
    ++d.filesGeneration; // (it's there now: Import a spreadsheet can be clicked)
    return d.failed ? "Can't write '{}': {}."_tr(sheet, SDL_GetError())
                    : "Exported {} texts to '{}': fill in its Translation column, then import it here."_tr(pairs.size(), sheet);
}

std::string Developer::importTranslationSheet() {
    DeveloperTools& d = tools;
    const std::string sheet = translationSheet();
    const auto text = readTextFile(sheet);
    if (!text) {
        d.failed = true;
        return "Can't open '{}': {}."_tr(sheet, SDL_GetError());
    }
    std::map<std::string, std::string> given; // (each text's translation in the sheet)
    for (const auto& row : csvRows(*text))
        if (row.size() >= 2 && !trim(row[1]).empty() && !(given.empty() && row[0] == "Text" && row[1] == "Translation")) given[row[0]] = std::string(trim(row[1]));
    auto& lines = d.translation.lines;
    d.translateKey.clear();
    d.translateUndo.push_back(d.translation);
    d.translateRedo.clear();
    std::size_t imported = 0;
    for (const TextPair& p : textPairs(lines) | std::views::reverse) { // (from the end: the earlier lines stay where they are)
        const auto it = given.find(unquote(trim(lines[p.original])));
        if (it == given.end() || (p.translated && unquote(trim(lines[*p.translated])) == it->second)) continue;
        setTranslation(lines, p, it->second);
        ++imported;
    }
    saveTranslation();
    d.failed = false;
    return imported == 0 ? "'{}' has no translations the file doesn't have already."_tr(sheet) : "Imported {} translations from '{}'."_tr(imported, sheet);
}

// The Translate tab: the game's translations (game/translations/NAME.vnscript), each text the game
// shows with a box for its translation. New language makes a file with every text, Add new texts
// adds the ones written since, and the texts the game doesn't show any more go as the scripts
// change; every change is written to the file at once.
void Developer::drawTranslations(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    std::vector<std::string> ids;
    for (const std::string& name : d.cachedFiles(engine.gameRoot() + "translations/", ".vnscript"))
        if (!name.contains('/')) ids.push_back(stemOf(name));
    std::ranges::sort(ids);
    auto& t = d.translation;
    if (!t.id.empty() && !std::ranges::contains(ids, t.id)) t = {};
    translationKeys();
    pruneTranslation();
    const auto columns = ui.columns(area, false); // (no form: the table takes the rest)
    const SDL_FRect &left = columns.left, &right = columns.middle;
    if (drawLanguages(ui, left, ids)) return; // (a button did its work: the rest next frame)
    if (t.id.empty()) {
        ui.text("A translation is a file in translations/ with every text the game shows, each with its translation under it. Make one with + New language, then pick its language."_tr,
                right.x, right.y, layout::noteText, theme::dim, right.w);
        return;
    }
    drawTranslation(ui, right, ids);
}

// The translations' languages, and the buttons under them: a new language, the texts written since
// added, and a spreadsheet for a translator, and the translations back from it (true: one did its work).
bool Developer::drawLanguages(DeveloperUi& ui, const SDL_FRect& left, const std::vector<std::string>& ids) {
    DeveloperTools& d = tools;
    const float k = ui.k;
    using ListKind = DeveloperUi::ListEvent::Kind;
    const auto& t = d.translation;
    const float headingH = layout::headingH * k, listButtonH = layout::buttonH * k, listGap = layout::gap * k;
    ui.heading("Languages"_tr, left.x, left.y);
    const SDL_FRect listBox{left.x, left.y + headingH, left.w, left.h - headingH - 4 * (listButtonH + listGap)};
    const auto event = ui.editList("languages", listBox, d.kindsScroll,
                                   {.count = ids.size(),
                                    .label = [&](std::size_t i) { return languageLabel(ids[i]); },
                                    .chosen = [&](std::size_t i) { return ids[i] == t.id; },
                                    .wrong = {},
                                    .faint = {},
                                    .renames = {},
                                    .deletes = {},
                                    .moves = {}});
    if (event.kind == ListKind::Chosen && ids[event.item] != t.id) openTranslation(ids[event.item]);
    float by = listBox.y + listBox.h + listGap;
    if (ui.button({left.x, by, left.w, listButtonH}, "+ New language"_tr, false, true, layout::buttonText)) {
        const std::string id = uniqueName("new_language", [&](const std::string& n) { return std::ranges::contains(ids, n); }); // (named after its language once that's picked)
        fillTranslation(id);
        return true;
    }
    by += listButtonH + listGap;
    if (ui.button({left.x, by, left.w, listButtonH}, "Add new texts"_tr, false, !t.id.empty(), layout::buttonText)) {
        rememberTranslation({});
        fillTranslation(t.id);
        return true;
    }
    // A spreadsheet for a translator, and the translations back from it.
    by += listButtonH + listGap;
    if (ui.button({left.x, by, left.w, listButtonH}, "Export a spreadsheet"_tr, false, !t.id.empty(), layout::buttonText)) {
        d.status = exportTranslationSheet();
        return true;
    }
    by += listButtonH + listGap;
    if (ui.button({left.x, by, left.w, listButtonH}, "Import a spreadsheet"_tr, false, !t.id.empty() && d.cachedExists(translationSheet()), layout::buttonText)) {
        d.status = importTranslationSheet();
        return true;
    }
    return false;
}

// The translation open: its language (the file takes its name), and its texts, each with a box for its
// translation.
void Developer::drawTranslation(DeveloperUi& ui, const SDL_FRect& right, const std::vector<std::string>& ids) {
    DeveloperTools& d = tools;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    auto& t = d.translation;
    // The language's name and code, on its first line: language "Español" code=es.
    std::optional<std::size_t> header;
    for (std::size_t i = 0; i < t.lines.size(); ++i)
        if (indentOf(t.lines[i]) == 0 && lower(firstWord(t.lines[i])) == "language") {
            header = i;
            break;
        }
    std::string name, code;
    if (header) std::tie(name, code) = languageHeader(t.lines[*header]);
    const float boxH = layout::boxH * k, labelW = ui.width("Language"_tr, layout::labelText) + 16 * k;
    float y = right.y;
    // One row: the language, picked from the world's, each with its code (typing narrows the list; a
    // name typed that isn't listed is kept as it is, with the code its name says, if any), then how
    // many texts there are and the button that shows only the untranslated ones.
    ui.textAround("Language"_tr, right.x, y + boxH / 2, layout::labelText, theme::dim);
    const StoryEditor::Field languageField{.name = "language", .value = code.empty() ? name : std::format("{} ({})", name, code), .box = Box::Language};
    if (const auto v = ui.pickField("lang:language", {right.x + labelW, y, std::min(layout::languageW * k, right.w - labelW), boxH}, languageField, layout::boxText);
        v && !languageOf(*v).first.empty()) { // (emptied: it keeps its language)
        auto [n, c] = languageOf(*v);
        if (c.empty()) c = n == name ? code : languageCode(n);
        rememberTranslation("lang:language" + std::to_string(d.focusCount));
        const std::string line = std::format("language \"{}\"{}", n, c.empty() ? "" : " code=" + c);
        if (header) t.lines[*header] = line;
        else t.lines.insert(t.lines.begin(), line);
        saveTranslation();
    }
    y += boxH + 8 * k;
    if (d.focus != "lang:language" && !name.empty()) // (picked, not still typed: the file takes its name)
        if (const std::string want = translationFileName(name); !want.empty() && want != t.id && !std::ranges::contains(ids, want)) renameTranslation(t.id, want);
    // (a code that isn't the language's: a computer set to it wouldn't pick this translation)
    if (const std::string known = languageCode(name); !known.empty() && !code.empty() && lower(code) != known && !lower(code).starts_with(known + "-"))
        y += ui.text("Computers set to {} say {}, not {}: with code={} they'd start with this translation."_tr(name, known, code, known), right.x, y,
                     layout::noteText, kWarn, right.w).y + 8 * k;

    const std::vector<TextPair> pairs = textPairs(t.lines); // (after the language's line: it may have come first just now)
    auto untranslated = [&](const TextPair& p) { return untranslatedPair(t.lines, p); };
    const auto todo = std::ranges::count_if(pairs, untranslated);
    const float onlyW = std::max(ui.buttonWidth("Show every text"_tr, layout::smallButtonText, layout::widerButtonW), ui.buttonWidth("Only untranslated"_tr, layout::smallButtonText, layout::widerButtonW)); // (one size, either way)
    const SDL_FRect only{right.x + right.w - onlyW, right.y + 1 * k, onlyW, boxH - 2 * k};
    if (ui.button(only, d.untranslatedOnly ? "Show every text"_tr : "Only untranslated"_tr, d.untranslatedOnly, true, layout::smallButtonText)) d.untranslatedOnly = !d.untranslatedOnly;
    const std::string count = pairs.size() == 1 ? "1 text, {} not translated yet"_tr(todo) : "{} texts, {} not translated yet"_tr(pairs.size(), todo);
    ui.textAround(count, only.x - 12 * k - ui.width(count, layout::smallText), right.y + boxH / 2, layout::smallText, theme::dim);
    const SDL_FRect table{right.x, y + 6 * k, right.w, right.y + right.h - y - 6 * k};
    draw::rect(r, table, kBox);
    const float rowH = layout::tableRowH * k, half = std::round((table.w - 30 * k) / 2);
    std::vector<const TextPair*> shown;
    for (const TextPair& p : pairs)
        if (!d.untranslatedOnly || untranslated(p)) shown.push_back(&p);
    d.translateScroll = std::clamp(d.translateScroll - ui.wheelOver(table) * rowH * 2, 0.0f,
                                   std::max(0.0f, static_cast<float>(shown.size()) * rowH - table.h + 12 * k));
    std::optional<DeveloperUi::Clip> clipped(std::in_place, ui, table, true);
    struct Typed {
        TextPair pair;
        std::string value, key;
    };
    std::optional<Typed> typed; // (written once every row is drawn: a line added or taken away moves the rows after it)
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const float ry = table.y + 6 * k + static_cast<float>(i) * rowH - d.translateScroll;
        if (ry + rowH < table.y || ry > table.y + table.h) continue;
        const TextPair& p = *shown[i];
        const std::string original = unquote(trim(t.lines[p.original]));
        ui.textAround(ui.fitted(original, layout::listText, half - 10 * k), table.x + 10 * k, ry + rowH / 2 - 3 * k, layout::listText, theme::text); // (one too long for its column ends with ...)
        const std::string now = p.translated ? unquote(trim(t.lines[*p.translated])) : std::string{};
        const std::string id = std::format("tr:{}:{}", t.id, p.original);
        if (const auto v = ui.field(id, {table.x + half + 20 * k, ry + 2 * k, half, rowH - 10 * k}, now, layout::boxText))
            typed = Typed{.pair = p, .value = *v, .key = id + std::to_string(d.focusCount)};
    }
    clipped.reset();
    if (typed) {
        rememberTranslation(typed->key);
        setTranslation(t.lines, typed->pair, typed->value);
        saveTranslation();
    }
}

// A translation's language as the list shows it ("Japanese (ja)"; one without a language line: its
// name), read again when its file changes.
std::string Developer::languageLabel(const std::string& id) {
    const std::string path = translationPath(id);
    DeveloperTools::LanguageLabel& known = tools.languageLabels[path]; // (by path)
    if (!known.label.empty() && known.generation == tools.filesGeneration) return known.label; // (looked at once a generation, not every frame)
    known.generation = tools.filesGeneration;
    SDL_PathInfo info{};
    SDL_GetPathInfo(path.c_str(), &info);
    std::string& label = known.label;
    if (label.empty() || known.read != info.modify_time) {
        known.read = info.modify_time;
        label = id;
        if (const auto text = readTextFile(path))
            for (const std::string& line : linesOf(*text))
                if (indentOf(line) == 0 && lower(firstWord(line)) == "language") {
                    const auto [n, c] = languageHeader(line);
                    if (!n.empty()) label = c.empty() ? n : std::format("{} ({})", n, c);
                    break;
                }
    }
    return label;
}

// A translation opened: its lines (without blank ones at the end), from the top, checked against the
// game's texts again.
void Developer::openTranslation(const std::string& id) {
    DeveloperTools& d = tools;
    auto& t = d.translation;
    t = {.id = id, .lines = {}};
    if (const auto text = readTextFile(translationPath(id)))
        t.lines = linesOf(*text);
    while (!t.lines.empty() && blank(t.lines.back())) t.lines.pop_back();
    d.translateScroll = 0.0f;
    d.translateChecked = SIZE_MAX; // (checked against the game's texts again)
    d.focus.clear();
}

// Every text the game shows, into a translation's file (made, or the texts it doesn't have yet at its
// end; the ones it doesn't show any more go), and the file opened; said, either way.
void Developer::fillTranslation(const std::string& id) {
    DeveloperTools& d = tools;
    if (!d.script || !d.script->errors.empty()) {
        d.failed = true;
        d.status = "Fix the scripts' mistakes first, so every text can be found."_tr;
        return;
    }
    SDL_CreateDirectory((engine.gameRoot() + "translations/").c_str());
    const auto existing = readTextFile(translationPath(id));
    ++d.filesGeneration;
    const TranslationFile out = translationFile(*d.script, id, existing);
    if ((!existing || out.added > 0 || !out.removed.empty()) && !SDL_SaveFile(translationPath(id).c_str(), out.text.data(), out.text.size())) {
        d.failed = true;
        d.status = "Can't write 'translations/{}.vnscript': {}."_tr(id, SDL_GetError());
        return;
    }
    d.failed = false;
    const std::string gone = out.removed.empty() ? std::string{} : tookOut(out.removed.size());
    d.status = !existing     ? "Made 'translations/{}.vnscript', with {} texts to translate."_tr(id, out.added)
             : out.added > 0 ? (out.added == 1 ? "Added 1 new text."_tr() : "Added {} new texts."_tr(out.added)) + (gone.empty() ? "" : " " + gone)
             : !gone.empty() ? gone
                             : "It has every text the game shows."_tr();
    openTranslation(id);
}

// A translation's file named after its language (japanese.vnscript), and its own pictures, sounds and
// movies (images/japanese/...) with it.
void Developer::renameTranslation(const std::string from, const std::string& to) { // (from is a copy: it can be t.id, which changes below)
    DeveloperTools& d = tools;
    auto& t = d.translation;
    if (!SDL_RenamePath(translationPath(from).c_str(), translationPath(to).c_str())) return;
    ++d.filesGeneration;
    for (const std::string_view sub : {"images/", "audio/", "movies/"}) {
        const std::string was = engine.gameRoot() + std::string(sub) + from;
        if (SDL_PathInfo info; SDL_GetPathInfo(was.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY &&
                               !SDL_RenamePath(was.c_str(), (engine.gameRoot() + std::string(sub) + to).c_str())) {
            d.failed = true; // (its own pictures, sounds or movies stay under the old name: said)
            d.status = "Can't move '{}{}' to '{}{}': {}."_tr(sub, from, sub, to, SDL_GetError());
        }
    }
    if (t.id == from) t.id = to;
    for (auto* steps : {&d.translateUndo, &d.translateRedo})
        for (DeveloperTools::Translation& step : *steps)
            if (step.id == from) step.id = to;
    d.languageLabels.erase(translationPath(from));
}

// Undo (Cmd/Ctrl+Z) and redo in the Translate tab: each change a step (typing into one box: one, by its
// key), the file written again.
void Developer::rememberTranslation(const std::string& key) {
    DeveloperTools& d = tools;
    if (!key.empty() && key == d.translateKey) return;
    d.translateKey = key;
    d.translateUndo.push_back(d.translation);
    if (d.translateUndo.size() > 100) d.translateUndo.erase(d.translateUndo.begin());
    d.translateRedo.clear();
}

void Developer::translationKeys() {
    DeveloperTools& d = tools;
    auto& t = d.translation;
    std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
        if (!(key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) || (key.key != SDLK_Z && key.key != SDLK_Y)) return false;
        const bool redo = key.key == SDLK_Y || (key.mod & SDL_KMOD_SHIFT);
        auto& from = redo ? d.translateRedo : d.translateUndo;
        auto& to = redo ? d.translateUndo : d.translateRedo;
        d.focus.clear();
        d.translateKey.clear();
        if (from.empty()) {
            d.status = redo ? "Nothing to redo."_tr : "Nothing to undo."_tr;
            return true;
        }
        DeveloperTools::Translation step = std::move(from.back()); // (its file as it is now, the other way)
        from.pop_back();
        DeveloperTools::Translation now{.id = step.id, .lines = {}};
        if (t.id == step.id) now = t;
        else if (const auto text = readTextFile(translationPath(step.id))) now.lines = linesOf(*text);
        to.push_back(std::move(now));
        t = std::move(step);
        d.translateScroll = 0.0f;
        saveTranslation();
        return true;
    });
}

// The texts the game doesn't show any more (changed or taken out of the scripts since the file was
// made) go from the file, with their translations, as the scripts change; undo brings them back.
void Developer::pruneTranslation() {
    DeveloperTools& d = tools;
    auto& t = d.translation;
    if (!t.id.empty() && d.script && d.script->errors.empty() && d.translateChecked != d.editor.version()) {
        d.translateChecked = d.editor.version();
        std::vector<std::string> removed;
        const std::string pruned = prunedTranslation(*d.script, t.id, joinedLines(t.lines), removed);
        if (!removed.empty()) {
            rememberTranslation({});
            t.lines = linesOf(pruned);
            while (!t.lines.empty() && blank(t.lines.back())) t.lines.pop_back();
            d.focus.clear();
            saveTranslation();
            d.status = tookOut(removed.size());
        }
    }
}
} // namespace vn
