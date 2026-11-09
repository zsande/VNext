// Read aloud (the player's setting): the story's lines and choices, a screen's words as it opens, and what the keys
// (or the pointer) are on, said by the system's voice or a screen reader.
#include "Engine.hpp"

#include <algorithm>
#include <cctype>

namespace vn {

namespace {

// Words said one after another: each its own sentence (a full stop after one that has none).
void sayAfter(std::string& out, std::string_view words) {
    words = trim(words);
    if (words.empty()) return;
    if (!out.empty()) out += out.ends_with('.') || out.ends_with('!') || out.ends_with('?') || out.ends_with(':') ? " " : ". ";
    out += words;
}

// Words that say something (not only signs: a "<" button says nothing).
bool hasWords(std::string_view s) {
    return std::ranges::any_of(s, [](char c) { return static_cast<unsigned char>(c) >= 0x80 || std::isalnum(static_cast<unsigned char>(c)); });
}

// A letter by itself, as an avatar's initial is ("M" for Mika), or an app's on its icon: a letter of any script, not a
// digit, a sign or an emoji.
bool loneLetter(std::string_view s) {
    s = trim(s);
    if (s.empty() || utf8::length(s) != 1) return false;
    const auto b = static_cast<unsigned char>(s[0]);
    if (b < 0x80) return std::isalpha(b) != 0;
    const std::size_t n = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
    char32_t c = b & (0x7Fu >> n);
    for (std::size_t k = 1; k < n && k < s.size(); ++k) c = (c << 6) | (static_cast<unsigned char>(s[k]) & 0x3Fu);
    return c >= 0xC0 && c != 0xD7 && c != 0xF7 && !(c >= 0x2000 && c < 0x2C00) && !(c >= 0x3000 && c < 0x3040) && c < 0x1F000;
}

// Texts said one after another; a letter by itself only when there's nothing else to say.
std::string sayAll(const std::vector<std::string>& texts) {
    const bool more = std::ranges::any_of(texts, [](const std::string& s) { return hasWords(s) && !loneLetter(s); });
    std::string out;
    for (const std::string& s : texts)
        if (!more || !loneLetter(s)) sayAfter(out, s);
    return out;
}

} // namespace

std::string ScreenView::pointedText(bool mouse) {
    const Target* t = focusedTarget();
    if (!t && mouse && mouse_.x >= 0.0f) t = targetAt(mouse_.x, mouse_.y);
    if (!t || t->spot.element >= def_->elements.size()) return {};
    const std::size_t at = t->spot.element;
    const Element& el = def_->elements[at];
    const auto i = static_cast<std::size_t>(std::max(0, t->spot.item));
    // The words a part shows now (a text's, a button's, a box's, a control's key).
    auto words = [&](std::size_t j) {
        const ElementKind kind = def_->elements[j].kind;
        const bool shows = kind == ElementKind::Text || kind == ElementKind::Button || kind == ElementKind::Input || kind == ElementKind::Control;
        return shows && live(j) ? std::string(trim(state_[j].text.text())) : std::string{};
    };
    std::string out;
    // Whether part j is in part `box` (in a part in it too).
    auto inside = [&](std::size_t j, std::size_t box) {
        for (int p = def_->elements[j].parent; p >= 0; p = def_->elements[static_cast<std::size_t>(p)].parent)
            if (static_cast<std::size_t>(p) == box) return true;
        return false;
    };
    if (el.kind == ElementKind::Container) { // (a part pressed whole, a list's item: the texts in it)
        std::vector<std::string> texts;
        for (std::size_t j = at + 1; j < def_->elements.size(); ++j)
            if (def_->elements[j].kind == ElementKind::Text && inside(j, at)) texts.push_back(words(j));
        return sayAll(texts);
    }
    if (el.kind != ElementKind::Button && el.kind != ElementKind::Slider && el.kind != ElementKind::Input && el.kind != ElementKind::Control) {
        const ElementState& st = state_[at]; // (what the part draws itself: the flowchart's points: its item's words)
        if (i < st.items.size()) sayAfter(out, st.items[i].text());
        if (i < st.extra.size()) sayAfter(out, st.extra[i].text());
        return out;
    }
    // A button, a slider, a box: the texts beside it in the part it's in. Its row: the ones it's level with (a label at
    // their left); in a part, a label over it, close by, when there's none at its left.
    auto boxOf = [&](std::size_t j) {
        SDL_FRect b = rectOf(j);
        const SDL_FPoint size = state_[j].text.size(); // (a text without a size of its own: as big as its words)
        if (b.w <= 0.0f) b.w = size.x;
        if (b.h <= 0.0f) b.h = size.y;
        return b;
    };
    // (a row ends at a wide gap: two columns side by side are two rows)
    const SDL_FRect me = boxOf(at);
    std::vector<std::pair<SDL_FRect, std::size_t>> level; // (the parts level with it, it too: what's there, and texts)
    std::optional<std::size_t> over, under;
    float overGap = me.h, underGap = me.h * 0.5f;
    for (std::size_t j = 0; j < def_->elements.size(); ++j) {
        const ElementKind kind = def_->elements[j].kind;
        const bool text = kind == ElementKind::Text, part = kind == ElementKind::Button || kind == ElementKind::Slider || kind == ElementKind::Input || kind == ElementKind::Control;
        if (def_->elements[j].parent != el.parent || !(text || part) || !live(j) || (text && (words(j).empty() || loneLetter(words(j))))) continue;
        const SDL_FRect b = boxOf(j);
        if (std::min(b.y + b.h, me.y + me.h) - std::max(b.y, me.y) > 0.5f * std::min(b.h, me.h)) {
            level.emplace_back(b, j);
        } else if (text && el.parent >= 0 && b.y + b.h <= me.y + 1.0f && me.y - (b.y + b.h) <= overGap && b.x < me.x + me.w && b.x + b.w > me.x) {
            over = j;
            overGap = me.y - (b.y + b.h);
        } else if (text && b.y >= me.y + me.h - 1.0f && b.y - (me.y + me.h) <= underGap && b.x < me.x + me.w && b.x + b.w > me.x) {
            under = j; // (a caption under it, as under an icon)
            underGap = b.y - (me.y + me.h);
        }
    }
    std::ranges::sort(level, [](const auto& a, const auto& b) { return a.first.x != b.first.x ? a.first.x < b.first.x : a.first.y < b.first.y; });
    const float wide = std::max(2.0f * me.h, 40.0f);
    std::size_t from = 0, to = level.size();
    for (std::size_t k = 0; k < level.size(); ++k) {
        const bool gap = k > 0 && level[k].first.x - (level[k - 1].first.x + level[k - 1].first.w) > wide;
        if (gap && std::ranges::any_of(level.begin() + static_cast<std::ptrdiff_t>(k), level.end(), [&](const auto& p) { return p.second == at; })) from = k;
        else if (gap && to == level.size() && std::ranges::any_of(level.begin() + static_cast<std::ptrdiff_t>(from), level.begin() + static_cast<std::ptrdiff_t>(k), [&](const auto& p) { return p.second == at; })) to = k;
    }
    std::vector<std::pair<float, std::size_t>> row; // (the texts in its stretch of the row)
    for (std::size_t k = from; k < to; ++k)
        if (level[k].second != at && def_->elements[level[k].second].kind == ElementKind::Text) row.emplace_back(level[k].first.x, level[k].second);
    std::optional<std::size_t> label = !row.empty() && row.front().first < me.x ? std::optional(row.front().second) : over;
    std::string own = words(at);
    if (const std::string letter = keyLetter(el); el.text.empty() && !letter.empty()) { // (an on-screen keyboard's key: its letter, or a sign's name)
        static constexpr std::pair<std::string_view, std::string_view> kSigns[]{{"'", "Apostrophe"}, {",", "Comma"}, {".", "Full stop"}, {"-", "Hyphen"}};
        const auto sign = std::ranges::find(kSigns, std::string_view(letter), &std::pair<std::string_view, std::string_view>::first);
        return sign != std::end(kSigns) ? engine_.script().translated(sign->second) : letter;
    }
    if (const auto* u = el.actions.size() == 1 ? std::get_if<cmd::Ui>(&el.actions.front()) : nullptr; u && u->verb == "type" && u->arg == "done")
        if (const std::string typed = engine_.keyboardText(); !typed.empty()) own += ". " + typed; // (Done: with what the box holds, to hear it)
    if (!label && (!hasWords(own) || loneLetter(own)) && under) { // (an icon: the caption under it names it, not its letter)
        label = under;
        own.clear();
    }
    std::vector<std::string> said;
    if (label) said.push_back(words(*label));
    if (hasWords(own)) said.push_back(own);
    for (const auto& [x, j] : row)
        if (j != label) said.push_back(words(j));
    // In a list's item, its name (its first text) first, when it isn't said anyway: "Mika. Call", in a list of people.
    if (el.parent >= 0 && isItemBox(static_cast<std::size_t>(el.parent))) {
        const auto item = static_cast<std::size_t>(el.parent);
        for (std::size_t j = item + 1; j < def_->elements.size() && inside(j, item); ++j) {
            const std::string name = def_->elements[j].kind == ElementKind::Text ? words(j) : std::string{};
            if (!hasWords(name) || loneLetter(name)) continue;
            if (std::ranges::find(said, name) == said.end()) said.insert(said.begin(), name);
            break;
        }
    }
    for (const std::string& w : said) sayAfter(out, w); // (its own words even when they're a letter: a choice "A", the key A)
    return out;
}

std::string ScreenView::openingText() {
    const bool phone = std::ranges::any_of(source_->elements, [](const Element& el) { return el.kind == ElementKind::Container && !el.option("app").empty(); });
    const std::string app = phone ? engine_.phoneApp(*source_) : std::string{};
    std::vector<std::string> texts;
    for (std::size_t j = 0; j < def_->elements.size(); ++j) {
        const Element& el = def_->elements[j];
        if (el.kind != ElementKind::Text || !live(j)) continue;
        bool skip = false, inApp = false;
        for (int p = el.parent; p >= 0 && !skip; p = def_->elements[static_cast<std::size_t>(p)].parent) {
            const Element& up = def_->elements[static_cast<std::size_t>(p)];
            skip = !up.option("list").empty() || lower(up.option("scroll", "no")) == "yes";
            inApp = inApp || (!app.empty() && up.option("app") == app);
        }
        if (!skip && (!phone || inApp)) texts.emplace_back(state_[j].text.text());
    }
    return sayAll(texts);
}

// Read aloud (the player's setting): what isn't spoken, each once, what's new together (stopping what was read before):
// what came to the phone; the screen on top as it opens (a phone: as its app changes), its title or question; a new
// line (who says it, then what) without a voice of its own, or a choice's options (after the line that asks it, if
// that has none), unless a screen that stops the story is over it; and what the keys are on (on the screen on top, or
// the title, what the mouse is on too).
void Engine::readAloud() {
    if (!settings_.readAloud || developer_.isOpen() || waiting_) {
        if (readAny_ && !screenReader()) speak({});
        readAny_ = readOpened_ = false;
        readLine_.clear(), readScreen_.clear(), readPointed_.clear();
        if (game_) (void)game_->takeNews(); // (what came meanwhile isn't said later)
        return;
    }
    std::vector<std::string> toSay;
    if (mode_ == Mode::Game && game_)
        for (std::string& news : game_->takeNews()) toSay.push_back(std::move(news));
    const std::string topName = !overlays_.empty() ? overlays_.back() : mode_ == Mode::Title ? std::string("title") : std::string{};
    ScreenView* top = topName.empty() ? nullptr : view(topName);
    const ScreenDef* topDef = top ? script_.screen(topName) : nullptr;
    const std::string opened = top && topDef ? topName + "/" + (showsPhone(topName) ? phoneApp(*topDef) : std::string{}) : std::string{};
    if (opened != readScreen_) readScreen_ = opened, readOpened_ = false;
    if (top && !readOpened_) // (once it's drawn: its words then)
        if (std::string words = top->openingText(); !words.empty()) {
            toSay.push_back(std::move(words));
            readOpened_ = true;
        }
    std::string line;
    if (mode_ == Mode::Game && game_ && !(top && top->modal())) {
        const bool heard = game_->lineVoiced() && settings_.voiceVolume > 0 && settings_.masterVolume > 0; // (its voice, unless the player turned it off)
        if (game_->showingChoices() && !game_->choices().empty()) {
            if (game_->menuPrompt() && !heard) line = game_->dialogue() + " ";
            for (const ChoiceItem& c : game_->choices()) line += expand(c.text, lookup()) + ". ";
        } else if (game_->showingSay() && !heard) {
            line = game_->speaker().empty() ? game_->dialogue() : game_->speaker() + ": " + game_->dialogue();
        }
    }
    if (line != readLine_ && !(readLine_ = line).empty()) toSay.push_back(line);
    std::string pointed;
    for (ScreenView* v : keyViews())
        if (!(pointed = v->pointedText(v == top)).empty()) break;
    if (pointed != readPointed_ && !(readPointed_ = pointed).empty()) toSay.push_back(pointed);
    if (toSay.empty()) return;
    std::string all;
    for (const std::string& words : toSay) sayAfter(all, words);
    if (all.empty()) return;
    readAny_ = true;
    say(all);
}

bool Engine::screenReader() {
    if (const Uint64 now = SDL_GetTicks(); now >= screenReaderAt_) {
        screenReaderAt_ = now + 2000;
        screenReader_ = screenReaderOn();
    }
    return screenReader_;
}

void Engine::say(std::string_view words) {
    if (screenReader()) announce(words);
    else speak(words);
}

} // namespace vn
