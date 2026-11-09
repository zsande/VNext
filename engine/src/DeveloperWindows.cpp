// The developer tools' messages and questions (the system's windows; the tools' own for a controller, which can't
// answer those), and choosing files (the system's picker; the tools' own list of a folder for a controller).
#include "DeveloperUi.hpp"

#include <cmath>
#include <mutex>
#include <ranges>

namespace vn {

// What a tool says (how it went wrong, or what it made where), in a window of its own with OK, as packing says how
// it went. With SDL's offscreen video (the recorder's) there's no window: the log has it.
void Developer::tell(const std::string& message, bool failed) {
    ask("VNext", message, {std::string("OK"_tr)}, 0, failed ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION);
}

void Developer::ask(std::string title, std::string body, std::vector<std::string> buttons, std::size_t keep, SDL_MessageBoxFlags kind,
                    std::function<void(std::size_t)> then) {
    const char* video = SDL_GetCurrentVideoDriver();
    const bool window = video && std::string_view(video) != "offscreen";
    keep = std::min(keep, buttons.size() - 1);
    if (engine.padInUse()) { // (the tools' own, which a controller can answer)
        if (engine.screenReader()) { // (each its own sentence)
            std::string words;
            for (const std::string_view w : {std::string_view(title), std::string_view(body), std::string_view(buttons[keep])})
                if (!trim(w).empty()) words += (words.empty() || words.ends_with('.') || words.ends_with('?') || words.ends_with('!') ? " " : ". ") + std::string(trim(w));
            announce(trim(words));
        }
        tools.asking = DeveloperTools::Asking{.title = std::move(title), .body = std::move(body), .buttons = std::move(buttons), .at = keep, .keep = keep,
                                              .then = std::move(then), .boxes = {}};
        tools.keysTakeOver(); // (asked of the controller: the ring on the answer that keeps things as they are, at once)
        return;
    }
    std::size_t answer = keep;
    if (window) {
        std::vector<SDL_MessageBoxButtonData> data;
        for (std::size_t i = 0; i < buttons.size(); ++i)
            data.push_back({.flags = i == keep ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0u,
                            .buttonID = static_cast<int>(i),
                            .text = buttons[i].c_str()});
        const SDL_MessageBoxData box{.flags = kind | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
                                     .window = engine.window(),
                                     .title = title.c_str(),
                                     .message = body.c_str(),
                                     .numbuttons = static_cast<int>(data.size()),
                                     .buttons = data.data(),
                                     .colorScheme = nullptr};
        int clicked = static_cast<int>(keep);
        if (!SDL_ShowMessageBox(&box, &clicked)) logWarn("Can't show a message: {}.", SDL_GetError());
        else if (clicked >= 0) answer = static_cast<std::size_t>(clicked);
    }
    if (then) then(answer);
}

// While the tools' own question or file picker is open, it takes the keys, the clicks and the wheel (nothing under it gets
// them).
bool Developer::modalFrame() {
    DeveloperTools& d = tools;
    if (!d.asking && !d.filePicker) return false;
    const auto keys = std::exchange(d.keys, {});
    if (!keys.empty()) d.keysTakeOver();
    d.typed.clear();
    if (d.asking) {
        DeveloperTools::Asking& a = *d.asking;
        const std::size_t n = a.buttons.size(), was = a.at;
        std::optional<std::size_t> answer;
        for (const SDL_KeyboardEvent& key : keys) {
            if (d.isKey(key, ToolKey::Left, true) || d.isKey(key, ToolKey::Previous, true)) a.at = (a.at + n - 1) % n;
            else if (d.isKey(key, ToolKey::Right, true) || d.isKey(key, ToolKey::Next, true)) a.at = (a.at + 1) % n;
            else if (d.isKey(key, ToolKey::Press)) answer = a.at;
            else if (d.isKey(key, ToolKey::Back)) answer = a.keep;
            if (answer) break;
        }
        if (d.clicked)
            for (std::size_t i = 0; i < a.boxes.size() && !answer; ++i)
                if (pointInRect(d.mouseX, d.mouseY, a.boxes[i])) answer = i;
        if (a.at != was && engine.screenReader()) announce(a.buttons[a.at]);
        if (answer) {
            auto then = std::move(a.then);
            d.asking.reset();
            if (then) then(*answer);
        }
    } else {
        DeveloperTools::FilePicker& p = *d.filePicker;
        const std::size_t was = p.at;
        auto open = [&](std::size_t i) { // (a folder: into it; a file: imported, and for a place of its own, closing)
            if (i >= p.entries.size()) return d.filePicker.reset();
            const auto& [name, folder] = p.entries[i];
            if (folder) {
                if (name == "..") {
                    std::string up = p.folder.substr(0, p.folder.size() - 1);
                    p.folder = up.substr(0, up.find_last_of('/') + 1);
                } else {
                    p.folder += name + "/";
                }
                d.pickerFolder = p.folder;
                return readPickerFolder();
            }
            {
                const std::scoped_lock _(d.imports.mutex);
                d.imports.files.emplace_back(p.folder + name, p.as);
            }
            p.taken.insert(name);
            if (!p.as.empty()) d.filePicker.reset();
        };
        std::optional<std::size_t> opened;
        bool close = false;
        for (const SDL_KeyboardEvent& key : keys) {
            if (d.isKey(key, ToolKey::Up, true)) p.at = p.at > 0 ? p.at - 1 : 0;
            else if (d.isKey(key, ToolKey::Down, true)) p.at = std::min(p.at + 1, p.entries.empty() ? 0 : p.entries.size() - 1);
            else if (d.isKey(key, ToolKey::Press)) opened = p.at;
            else if (d.isKey(key, ToolKey::Back)) close = true;
            if (opened || close) break;
        }
        if (d.clicked)
            for (std::size_t i = 0; i < p.boxes.size() && !opened; ++i)
                if (pointInRect(d.mouseX, d.mouseY, p.boxes[i])) opened = i; // (the last one: Done)
        p.scroll -= d.wheel * 3.0f;
        if (close) d.filePicker.reset();
        else if (opened) open(*opened);
        else if (p.at != was && engine.screenReader() && p.at < p.entries.size()) announce(p.entries[p.at].first);
    }
    d.clicked = false;
    d.wheel = d.wheelAcross = 0.0f;
    return true;
}

void Developer::readPickerFolder() {
    DeveloperTools::FilePicker& p = *tools.filePicker;
    std::vector<std::string> names;
    SDL_EnumerateDirectory(
        p.folder.c_str(),
        [](void* user, const char*, const char* name) -> SDL_EnumerationResult {
            return withoutThrowing(SDL_ENUM_FAILURE, [&] {
                static_cast<std::vector<std::string>*>(user)->emplace_back(name);
                return SDL_ENUM_CONTINUE;
            });
        },
        &names);
    std::ranges::sort(names, {}, [](const std::string& n) { return lower(n); });
    p.entries.clear();
    if (p.folder.size() > 1) p.entries.emplace_back("..", true);
    std::vector<std::string> files;
    for (const std::string& name : names) {
        if (name.starts_with('.')) continue;
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo((p.folder + name).c_str(), &info)) continue;
        const auto dot = name.find_last_of('.');
        if (info.type == SDL_PATHTYPE_DIRECTORY) p.entries.emplace_back(name, true);
        else if (info.type == SDL_PATHTYPE_FILE && dot != std::string::npos && std::ranges::contains(p.extensions, lower(name.substr(dot)))) files.push_back(name);
    }
    for (std::string& f : files) p.entries.emplace_back(std::move(f), false);
    p.at = 0;
    p.scroll = 0.0f;
    p.taken.clear();
    if (engine.screenReader()) announce(std::format("{}. {}", p.folder, p.entries.empty() ? std::string("Empty"_tr) : p.entries.front().first));
}

// The tools' own question or file picker, over all the rest (a shade between).
void Developer::drawModal(DeveloperUi& ui) {
    DeveloperTools& d = tools;
    if (!d.asking && !d.filePicker) return;
    const float k = ui.k, pad = std::round(16.0f * k), rowH = std::round(layout::listItemH * k), buttonH = std::round(layout::buttonH * k);
    draw::rect(ui.r, {0, 0, d.size.x, d.size.y}, SDL_Color{0, 0, 0, 150});
    const bool keys = d.keyRing; // (while the keys are in charge: the ring on the one they're on)
    auto button = [&](const SDL_FRect& box, std::string_view label, bool on) {
        draw::rect(ui.r, box, on || pointInRect(d.mouseX, d.mouseY, box) ? kButtonHover : kButton);
        draw::outline(ui.r, box, on ? kAccent : kEdge);
        if (on) draw::outline(ui.r, {box.x + 1, box.y + 1, box.w - 2, box.h - 2}, kAccent);
        ui.textIn(label, box, layout::buttonText, theme::text);
    };
    if (d.asking) {
        DeveloperTools::Asking& a = *d.asking;
        const float w = std::min(560.0f * k, d.size.x - 40.0f * k), textW = w - 2 * pad;
        const SDL_FPoint bodySize = ui.measure(a.body, layout::lineText, textW);
        const float h = pad + std::round(layout::titleH * k) + bodySize.y + pad + buttonH + pad;
        const SDL_FRect window{std::round((d.size.x - w) / 2), std::round((d.size.y - h) / 2), w, h};
        draw::rect(ui.r, window, kPanel);
        draw::outline(ui.r, window, kEdge);
        ui.formTitle(a.title, window.x + pad, window.y + pad, kAccent, textW);
        ui.text(a.body, window.x + pad, window.y + pad + std::round(layout::titleH * k), layout::lineText, theme::text, textW);
        a.boxes.clear();
        float x = window.x + window.w - pad;
        std::vector<SDL_FRect> boxes(a.buttons.size());
        for (std::size_t i = a.buttons.size(); i-- > 0;) { // (right to left: the last at the right)
            const float bw = ui.buttonWidth(a.buttons[i], layout::buttonText, 100.0f);
            x -= bw;
            boxes[i] = {x, window.y + window.h - pad - buttonH, bw, buttonH};
            x -= std::round(8.0f * k);
        }
        for (std::size_t i = 0; i < boxes.size(); ++i) button(boxes[i], a.buttons[i], keys && a.at == i);
        a.boxes = std::move(boxes);
        return;
    }
    DeveloperTools::FilePicker& p = *d.filePicker;
    const float w = std::min(720.0f * k, d.size.x - 40.0f * k), h = std::min(540.0f * k, d.size.y - 40.0f * k);
    const SDL_FRect window{std::round((d.size.x - w) / 2), std::round((d.size.y - h) / 2), w, h};
    draw::rect(ui.r, window, kPanel);
    draw::outline(ui.r, window, kEdge);
    ui.formTitle(p.label, window.x + pad, window.y + pad, kAccent, w - 2 * pad);
    const float pathY = window.y + pad + std::round(layout::titleH * k);
    ui.textFitted(p.folder, window.x + pad, pathY + rowH / 2, layout::lineText, theme::dim, w - 2 * pad);
    p.list = {window.x + pad, pathY + rowH + std::round(6.0f * k), w - 2 * pad, window.y + h - pad - buttonH - std::round(10.0f * k) - (pathY + rowH + std::round(6.0f * k))};
    draw::rect(ui.r, p.list, kBox);
    const float total = static_cast<float>(p.entries.size()) * rowH;
    const float top = static_cast<float>(p.at) * rowH; // (the keys' one in sight)
    p.scroll = std::clamp(std::clamp(p.scroll, top + rowH - p.list.h, top), 0.0f, std::max(0.0f, total - p.list.h));
    p.boxes.clear();
    {
        const DeveloperUi::Clip clip(ui, p.list);
        for (std::size_t i = 0; i < p.entries.size(); ++i) {
            const SDL_FRect row{p.list.x, p.list.y + static_cast<float>(i) * rowH - p.scroll, p.list.w, rowH};
            p.boxes.push_back(row.y + row.h <= p.list.y || row.y >= p.list.y + p.list.h ? SDL_FRect{} : row);
            if (row.y + row.h <= p.list.y || row.y >= p.list.y + p.list.h) continue;
            const auto& [name, folder] = p.entries[i];
            if (i == p.at && keys) draw::rect(ui.r, row, kChosen);
            else if (pointInRect(d.mouseX, d.mouseY, row)) draw::rect(ui.r, row, kButtonHover);
            const std::string shown = name == ".." ? std::string("..") : folder ? name + "/" : p.taken.contains(name) ? name + "  \u2713" : name;
            ui.textFitted(shown, row.x + std::round(layout::listPad * k), row.y + row.h / 2, layout::listText, theme::text, row.w - 2 * std::round(layout::listPad * k));
        }
    }
    if (p.entries.empty()) ui.textAround("Empty"_tr, p.list.x + std::round(layout::listPad * k), p.list.y + rowH / 2, layout::listText, theme::dim);
    const float bw = ui.buttonWidth("Done"_tr, layout::buttonText, 100.0f);
    const SDL_FRect done{window.x + window.w - pad - bw, window.y + window.h - pad - buttonH, bw, buttonH};
    button(done, "Done"_tr, false);
    p.boxes.push_back(done);
}

void Developer::chooseFiles(std::string_view label, std::span<const std::string_view> extensions, const std::string& as) {
    DeveloperTools& d = tools;
    if (engine.padInUse()) { // (a controller: the tools' own list of a folder's files, which it can go through)
        if (d.pickerFolder.empty()) {
            const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
            d.pickerFolder = home ? withSlash(home) : std::string("/");
        }
        d.filePicker = DeveloperTools::FilePicker{.label = std::string(label), .as = as, .folder = d.pickerFolder, .extensions = {}, .entries = {}, .at = 0,
                                                  .scroll = 0.0f, .taken = {}, .boxes = {}, .list = {}};
        for (const std::string_view ext : extensions) d.filePicker->extensions.push_back(lower(ext));
        readPickerFolder();
        d.keysTakeOver(); // (the controller's: the ring on its first entry at once)
        return;
    }
    {
        const std::scoped_lock _(d.imports.mutex);
        if (d.imports.open) { // (its filter's words stay as they are until it's answered)
            d.status = "A window for choosing files is open already."_tr;
            return;
        }
        d.imports.open = true;
        d.imports.as = as;
        d.imports.label = label;
        d.imports.pattern.clear();
        for (const std::string_view ext : extensions) d.imports.pattern += (d.imports.pattern.empty() ? "" : ";") + std::string(ext.substr(1));
        d.imports.filter = {d.imports.label.c_str(), d.imports.pattern.c_str()};
    }
    SDL_ShowOpenFileDialog(
        [](void* state, const char* const* chosen, int) {
            auto& imports = static_cast<DeveloperTools*>(state)->imports;
            withoutThrowing([&] { // (SDL calls this, maybe from another thread)
                const std::scoped_lock _(imports.mutex);
                const ScopeExit answered([&] { // (whatever could be kept of it)
                    imports.as.clear(); // (dropped on the window later: by their kind)
                    imports.open = false;
                });
                for (; chosen && *chosen; ++chosen) imports.files.emplace_back(*chosen, imports.as);
            });
        },
        &d, engine.window(), &d.imports.filter, 1, nullptr, as.empty());
}

} // namespace vn
