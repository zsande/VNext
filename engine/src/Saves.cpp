// Saves: the game as a save's lines and back (the story's place, its values, the stage, the phone...), rollback's
// moments, the save slots on disk, and saving and loading as the player does it.
#include "Engine.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <sstream>
#include <utility>
#include <variant>

namespace vn {

void SaveStore::setFolder(std::string folder, GameFiles* pack) {
    folder_ = withSlash(std::move(folder));
    pack_ = pack;
    if (!pack_) SDL_CreateDirectory(folder_.c_str());
}

void SaveStore::configure(int pages, int slotsPerPage) {
    pages_ = {"auto", "quick"};
    for (int p = 1; p <= pages; ++p) pages_.push_back(std::to_string(p));
    slots_ = slotsPerPage;
}

namespace {

constexpr std::string_view kThumbnail = "thumbnail png "; // then its size in bytes, a newline and the image

std::string pngBytes(SDL_Surface* surface) {
    std::string out;
    const IOStreamPtr io{surface ? SDL_IOFromDynamicMem() : nullptr};
    if (!io) return out;
    if (IMG_SavePNG_IO(surface, io.get(), false)) {
        const auto* bytes = static_cast<const char*>(SDL_GetPointerProperty(SDL_GetIOProperties(io.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
        const Sint64 size = SDL_GetIOSize(io.get());
        if (bytes && size > 0) out.assign(bytes, static_cast<std::size_t>(size));
    }
    return out;
}

} // namespace

std::optional<SaveStore::File> SaveStore::load(std::string_view slot) const {
    const auto data = pack_ ? pack_->readText(path(slot)) : readTextFile(path(slot));
    if (!data) return std::nullopt;
    // The text ends at the thumbnail line (no line of the state starts with "thumbnail").
    File file;
    const std::size_t at = data->find(std::format("\n{}", kThumbnail));
    file.text = data->substr(0, at == std::string::npos ? std::string::npos : at + 1);
    if (at != std::string::npos) {
        const std::size_t number = at + 1 + kThumbnail.size(), end = data->find('\n', number);
        std::size_t bytes = 0;
        if (end != std::string::npos && std::from_chars(data->data() + number, data->data() + end, bytes).ec == std::errc{} &&
            data->size() - end - 1 == bytes)
            file.png = data->substr(end + 1);
    }
    return file;
}

std::optional<std::string> SaveStore::read(std::string_view slot) const {
    auto file = load(slot);
    return file ? std::optional(std::move(file->text)) : std::nullopt;
}

std::string SaveStore::thumbnail(std::string_view slot) const {
    auto file = load(slot);
    return file ? std::move(file->png) : std::string();
}

SaveInfo SaveStore::info(std::string_view slot) const {
    SaveInfo info;
    SDL_PathInfo file{}; // (none for a pack's: its saves say when they were made)
    if (pack_ ? !pack_->exists(path(slot)) : !SDL_GetPathInfo(path(slot).c_str(), &file) || file.type != SDL_PATHTYPE_FILE) return info;
    const auto save = load(slot);
    if (!save) return info;
    info.exists = true;
    info.time = file.modify_time; // used when the save has no metadata
    {
        std::istringstream in(save->text);
        std::string line;
        while (std::getline(in, line)) {
            if (line.starts_with("meta time ")) std::from_chars(line.data() + 10, line.data() + line.size(), info.time);
            else if (line.starts_with("meta summary ")) info.summary = line.substr(13);
        }
    }
    return info;
}

bool SaveStore::write(std::string_view slot, std::string_view state, std::string_view summary, SDL_Surface* thumbnail) {
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    std::string flat(summary);
    std::ranges::replace(flat, '\n', ' ');
    std::string data = std::format("{}meta time {}\nmeta summary {}\n", state, now, flat);
    if (const std::string png = pngBytes(thumbnail); !png.empty()) data += std::format("{}{}\n{}", kThumbnail, png.size(), png);
    if (pack_) return pack_->write(path(slot), data); // (the pack keeps the old one whole until the new one is in)
    // Written next to it first, so stopping halfway (a crash, a full disk) leaves the old save whole.
    const std::string file = path(slot), part = file + ".part";
    if (!SDL_SaveFile(part.c_str(), data.data(), data.size()) || !SDL_RenamePath(part.c_str(), file.c_str())) {
        SDL_RemovePath(part.c_str());
        return false;
    }
    return true;
}

void SaveStore::remove(std::string_view slot) {
    if (pack_) pack_->remove(path(slot));
    else SDL_RemovePath(path(slot).c_str());
}

std::string SaveStore::rotatingSlot(std::string_view page) const {
    std::string oldest;
    SDL_Time oldestTime = 0;
    for (int i = 0; i < slots_; ++i) {
        std::string name = slotName(page, i);
        const SaveInfo info = this->info(name);
        if (!info.exists) return name;
        if (oldest.empty() || info.time < oldestTime) {
            oldest = std::move(name);
            oldestTime = info.time;
        }
    }
    return oldest;
}

std::optional<std::string> SaveStore::newest(std::string_view page) const {
    std::optional<std::string> best;
    SDL_Time bestTime = 0;
    for (const auto& p : pages_) {
        if (!page.empty() && p != page) continue;
        for (int i = 0; i < slots_; ++i) {
            std::string name = slotName(p, i);
            const SaveInfo info = this->info(name);
            if (info.exists && (!best || info.time > bestTime)) {
                best = std::move(name);
                bestTime = info.time;
            }
        }
    }
    return best;
}

void SlotBrowser::open(Mode mode) {
    mode_ = mode;
    pages_.clear();
    for (const auto& p : store_->pages())
        if (mode == Mode::Load || (p != "auto" && p != "quick")) pages_.push_back(p);
    // Saving reopens the last page used; loading opens on the page with the newest save.
    std::string want = last_[mode == Mode::Save ? 0 : 1];
    if (mode == Mode::Load)
        if (const auto newest = store_->newest()) want = newest->substr(0, newest->rfind('-'));
    const auto it = std::ranges::find(pages_, want);
    setPage(it != pages_.end() ? static_cast<std::size_t>(it - pages_.begin()) : 0);
}

void SlotBrowser::setPage(std::size_t page) {
    if (pages_.empty()) return;
    page_ = std::min(page, pages_.size() - 1);
    last_[mode_ == Mode::Save ? 0 : 1] = pages_[page_];
    cards_.clear();
    cards_.resize(static_cast<std::size_t>(store_->slotsPerPage()));
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        cards_[i].slot = SaveStore::slotName(pages_[page_], static_cast<int>(i));
        refresh(i);
    }
}

void SlotBrowser::turn(int delta) {
    if (pages_.empty()) return;
    const auto n = static_cast<int>(pages_.size());
    setPage(static_cast<std::size_t>((static_cast<int>(page_) + delta + n) % n));
}

void SlotBrowser::refresh(std::size_t index) {
    if (index >= cards_.size()) return;
    Card& card = cards_[index];
    card.info = store_->info(card.slot);
    card.thumbnail.reset();
    if (!card.info.exists) return;
    const std::string png = store_->thumbnail(card.slot);
    if (!png.empty()) card.thumbnail.reset(IMG_LoadTexture_IO(renderer_, SDL_IOFromConstMem(png.data(), png.size()), true));
    if (card.thumbnail) SDL_SetTextureScaleMode(card.thumbnail.get(), SDL_SCALEMODE_LINEAR);
}

namespace {

constexpr std::string_view kSaveMagic = "vnext-save 5"; // (5: 2026-11-11, looping sounds with their name and sound type)

// Saves keep one value per line: spaces, backslashes and newlines are escaped.
std::string escape(std::string_view s) {
    return encodeValue(std::string(s)).substr(2);
}

std::string unescape(std::string_view s) {
    return toString(decodeValue(std::format("s {}", s)));
}

// A text as one word of a save line (spaces and all; nothing: "~"), and back.
std::string field(std::string_view s) { return s.empty() ? std::string("~") : "=" + escape(s); }
std::string unfield(std::string_view s) { return s.size() < 2 || s.front() != '=' ? std::string{} : unescape(s.substr(1)); }

// A movie's way of playing as a save has it (" loop=0 volume=500"; nothing for again and again, as loud as it is).
std::string movieFields(bool loop, float volume) {
    return std::string(loop ? "" : " loop=0") + (volume == 1.0f ? std::string{} : std::format(" volume={}", std::lround(volume * 1000.0f)));
}

// An image on stage as a save has it ("object TAG char=... x=..."), and read back (after "object").
std::string objectLine(const Stage::ObjectState& o) {
    const std::string box = o.image.starts_with('#') ? std::format(" width={} height={}", std::lround(o.width * 10000.0f), std::lround(o.height * 10000.0f)) : ""; // (a colour's)
    return std::format("object {} char={} image={} expr={} x={} y={} zoom={} alpha={} rotation={} flip={} layer={} tint=#{:02x}{:02x}{:02x}{}{}{}{}\n",
                       escape(o.tag), escape(o.character), escape(o.image), escape(o.expression), std::lround(o.x * 10000.0f),
                       std::lround(o.y * 10000.0f), std::lround(o.zoom * 10000.0f), std::lround(o.opacity * 10000.0f), std::lround(o.rotation * 100.0f),
                       o.flip ? 1 : 0, o.layer, o.tint.r, o.tint.g, o.tint.b, o.animation.empty() ? "" : " animation=" + escape(o.animation),
                       o.played.empty() ? "" : " played=" + escape(o.played), movieFields(o.loop, o.volume), box);
}
Stage::ObjectState readObject(std::istream& ls) {
    Stage::ObjectState o;
    std::string tag, field;
    ls >> tag;
    o.tag = unescape(tag);
    while (ls >> field) {
        const auto eq = field.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = field.substr(0, eq), v = unescape(field.substr(eq + 1));
        const float n = static_cast<float>(toInt(v));
        if (k == "char") o.character = v;
        else if (k == "image") o.image = v;
        else if (k == "expr") o.expression = v;
        else if (k == "x") o.x = n / 10000.0f;
        else if (k == "y") o.y = n / 10000.0f;
        else if (k == "zoom") o.zoom = n / 10000.0f;
        else if (k == "alpha") o.opacity = n / 10000.0f;
        else if (k == "rotation") o.rotation = n / 100.0f;
        else if (k == "flip") o.flip = n != 0.0f;
        else if (k == "layer") o.layer = static_cast<int>(n);
        else if (k == "tint") o.tint = parseColour(v).value_or(o.tint);
        else if (k == "animation") o.animation = v;
        else if (k == "played") o.played = v;
        else if (k == "loop") o.loop = n != 0.0f;
        else if (k == "volume") o.volume = n / 1000.0f;
        else if (k == "width") o.width = n / 10000.0f;
        else if (k == "height") o.height = n / 10000.0f;
    }
    return o;
}

} // namespace

std::string Game::save(bool history) const {
    auto located = [&](std::size_t pc) {
        auto [label, offset] = script_.locate(pc);
        return std::format("{} {} {}", label.empty() ? "-" : label, offset, pc);
    };
    std::string out = std::format("{}\nhash {}\nresume {}\nrandom {}\n", kSaveMagic, script_.hash, located(resumePc_), resumeRandom_);
    for (auto pc : callStack_) out += std::format("call {}\n", located(pc));
    for (const auto& [name, value] : vars_) out += std::format("variable {} {}\n", name, encodeValue(value));
    for (const auto& o : stage_.objects()) out += objectLine(o);
    if (!stage_.background().empty())
        out += std::format("background {}{}{}{} x={} y={}\n", stage_.background(), stage_.backgroundAnimation(true).empty() ? "" : " animation=" + escape(stage_.backgroundAnimation(true)),
                           stage_.backgroundAnimation(false).empty() ? "" : " played=" + escape(stage_.backgroundAnimation(false)),
                           movieFields(stage_.backgroundLoops(), stage_.backgroundVolume()), std::lround(stage_.backgroundPart().x * 10000.0f),
                           std::lround(stage_.backgroundPart().y * 10000.0f));
    if (!flowchartNode_.empty()) out += std::format("flowchartpoint {} {}\n", flowchartNode_, flowchartOption_);
    if (!music_.empty()) out += std::format("musicvol {}\nmusic {}\n", std::lround(musicVolume_ * 1000.0f), music_);
    for (const auto& [channel, loop] : loops_)
        out += std::format("loop {} {} {} {} {}\n", escape(channel), std::lround(loop.volume * 1000.0f), escape(loop.type), escape(loop.name), loop.file);
    for (const auto& screen : screens_) out += std::format("screen {}\n", screen);
    // The phone: how many messages have come (pphone), its messages and calls, and a chat scene or a call under
    // way. Texts are one word each (field()); a command, "-" or where it is.
    auto message = [&](std::string_view key, const MessageItem& m) {
        return std::format("{} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {}\n", key, static_cast<int>(m.kind), m.read ? 1 : 0, m.seen ? 1 : 0,
                           m.deleted ? 1 : 0, m.edited ? 1 : 0, std::lround(m.typing * 1000.0f), m.number, m.colour.r, m.colour.g, m.colour.b, field(m.from),
                           field(m.with), field(m.character), field(m.expression), field(m.text), field(m.file), field(m.time), field(m.date),
                           field(m.reaction));
    };
    out += std::format("pphone {} {}\n", messageCount_, arrivals_);
    for (const MessageItem& m : phone_.messages) out += message("pmsg", m);
    for (const CallItem& c : phone_.calls)
        out += std::format("pcall {} {} {} {} {} {} {} {} {} {} {}\n", c.outgoing ? 1 : 0, c.missed ? 1 : 0, c.busy ? 1 : 0, c.seen ? 1 : 0, c.seconds, c.colour.r,
                           c.colour.g, c.colour.b, field(c.who), field(c.time), field(c.date));
    if (!chat_.empty()) out += std::format("pscene {} {}\n", field(chat_), wait_ == Wait::Chat && !change_ ? chatWait_ : 0); // (and the message it waits for)
    if (call_ && call_->talking && call_->at) // (one ringing rings again: its command is done again)
        out += std::format("pcallnow {} {} {} {} {} {} {} {}\n", call_->outgoing ? 1 : 0, std::lround(call_->time * 1000.0f), call_->colour.r, call_->colour.g,
                           call_->colour.b, field(call_->who), field(call_->character), located(*call_->at));
    for (std::size_t i = 0; i + (wait_ == Wait::Say ? 1 : 0) < page_.size(); ++i) { // (the line on screen is said again)
        const PageLine& p = page_[i];
        out += std::format("page {} {} {} {} {}\n", p.colour.r, p.colour.g, p.colour.b, escape(p.speaker.empty() ? "-" : p.speaker), escape(p.text));
    }
    // The history (but the line on screen: it's said again), each line's words, looks and voice; and a choice's
    // question, the line on screen with it.
    if (history)
        for (std::size_t i = 0; i + (wait_ == Wait::Say ? 1 : 0) < history_.size(); ++i) {
            const HistoryEntry& h = history_[i];
            out += std::format("hist {} {} {} {} {} {} {} {} {} {}", h.colour.r, h.colour.g, h.colour.b, h.choice ? 1 : 0, std::lround(h.madeUpVoice * 1000.0f),
                               std::lround(h.madeUpVoiceSpeed * 1000.0f), field(h.speaker), field(h.voice), field(h.text), h.spans.size());
            for (const TextSpan& s : h.spans)
                out += std::format(" {} {} {} {} {}", s.begin, s.end, s.colour ? std::format("#{:02x}{:02x}{:02x}{:02x}", s.colour->r, s.colour->g, s.colour->b, s.colour->a) : "-",
                                   static_cast<unsigned>(s.style), field(s.ruby));
            out += "\n";
        }
    if (wait_ == Wait::Menu && menuPrompt_ && saidAt_ < script_.code.size()) out += std::format("prompt {}\n", located(saidAt_));
    if (const cmd::Weather& w = stage_.weather(); !w.kind.empty())
        out += std::format("weather {} {} {} {} {} {} {}\n", w.kind, std::lround(w.amount * 1000.0f), std::lround(w.speed * 1000.0f),
                           std::lround(w.wind * 1000.0f), w.behind ? 1 : 0,
                           w.tint ? std::format("#{:02x}{:02x}{:02x}{:02x}", w.tint->r, w.tint->g, w.tint->b, w.tint->a) : "-",
                           w.image.empty() ? "-" : w.image);
    if (const cmd::Effect e = stage_.effect(); e.looks != 0) out += std::format("effect {} {}\n", e.looks, std::lround(e.amount * 1000.0f));
    if (trigger_) out += std::format("trigger {} {}\n", located(trigger_->at), trigger_->lines + (wait_ == Wait::Say ? 1 : 0)); // (the line is said again)
    for (const BoxLine& l : others_)
        out += std::format("box {} {} {} {} {} {} {} {}\n", l.speakerColour.r, l.speakerColour.g, l.speakerColour.b, escape(l.box),
                           escape(l.speakerKey.empty() ? "-" : l.speakerKey), escape(l.speaker.empty() ? "-" : l.speaker), escape(l.face.empty() ? "-" : l.face),
                           escape(l.source));
    if (pass_) out += std::format("pass {} {}\n", pass_->depth, pass_->lines);
    if (hiddenLines_ > 0) out += std::format("hidden {}\n", hiddenLines_);
    if (const std::string own = stage_.cameraAnimation(true), played = stage_.cameraAnimation(false); !own.empty() || !played.empty()) {
        // (its animations, then where the one played on it started: its numbers, colours and wipe's shape)
        const Animation::Look& s = stage_.cameraStart();
        auto hex = [](SDL_Color c) { return std::format("#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a); };
        out += std::format("camera {} {} {} {} {} {} {} {} {} {} {} {}\n", own.empty() ? "-" : escape(own), played.empty() ? "-" : escape(played),
                           std::lround(s.x * 1000.0f), std::lround(s.y * 1000.0f), std::lround(s.opacity * 1000.0f), std::lround(s.zoom * 1000.0f),
                           std::lround(s.rotation * 1000.0f), std::lround(s.fade * 1000.0f), std::lround(s.wipe * 1000.0f), hex(s.fadeColour),
                           s.colour ? hex(*s.colour) : "-", s.wipeShape.empty() ? "-" : escape(s.wipeShape));
    }
    for (const auto& [character, expression] : expressions_) out += std::format("expr {} {}\n", escape(character), escape(expression));
    return out;
}

std::expected<std::string, std::string> Game::load(std::string_view data) {
    auto note = restore(data, std::nullopt); // (a game of its own: rollback goes back as far as the line it goes on at)
    autosave_ = false; // reaching a choice by loading is not a new decision point
    return note;
}

void Game::remember() {
    if (gameSettings().rollbackLines <= 0 || asking()) return; // (a chat= or call= scene's lines: the story's line is the place to go back to)
    const bool line = wait_ == Wait::Say;
    past_.push_back({save(false), historyDropped_ + history_.size() - (line && !history_.empty() ? 1 : 0), !(line && noWait_), ctx_.persistent});
    if (past_.size() > static_cast<std::size_t>(gameSettings().rollbackLines) + 1) past_.erase(past_.begin());
}

bool Game::canRollback() const {
    if (!ctx_.settings.rollback || asking() || (wait_ != Wait::Say && wait_ != Wait::Menu) || past_.size() < 2) return false; // (not in a chat= or call= scene's lines: once it's over)
    return std::any_of(past_.begin(), past_.end() - 1, [](const Moment& m) { return m.stop; });
}

bool Game::rollback() {
    if (!canRollback()) return false;
    std::size_t moment = past_.size() - 2; // before the line on screen now ({no_wait} lines go by)
    while (!past_[moment].stop) --moment;
    return rewind(moment);
}

std::optional<std::size_t> Game::momentOf(std::size_t entry) const {
    if (!ctx_.settings.rollback || asking() || (wait_ != Wait::Say && wait_ != Wait::Menu)) return std::nullopt;
    const std::size_t counted = historyDropped_ + entry; // a line's entry is its moment's history; a choice's, its menu's
    for (std::size_t i = 0; i + 1 < past_.size(); ++i)
        if (past_[i].stop && past_[i].history == counted) return i;
    return std::nullopt;
}

bool Game::rollbackTo(std::size_t entry) {
    const auto moment = momentOf(entry);
    return moment && rewind(*moment);
}

void Game::addHistory(HistoryEntry entry) {
    history_.push_back(std::move(entry));
    const auto limit = static_cast<std::size_t>(std::max(1, gameSettings().historyLength));
    if (history_.size() > limit) {
        const std::size_t drop = history_.size() - limit;
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(drop));
        historyDropped_ += drop;
    }
}

bool Game::rewind(std::size_t moment) {
    past_.erase(past_.begin() + static_cast<std::ptrdiff_t>(moment) + 1, past_.end());
    Moment back = std::move(past_.back());
    past_.pop_back(); // showing that line again remembers it again
    skip_ = false;
    // What the story did to persistent variables since then is undone too, so going forward again
    // doesn't do it twice (a visit counter, say). Gallery images, endings, songs and achievements
    // stay: what was seen stays seen.
    std::erase_if(ctx_.persistent, [&](const auto& v) { return !Script::isRecordVariable(v.first) && !back.persistent.contains(v.first); });
    for (auto& [name, value] : back.persistent)
        if (!Script::isRecordVariable(name)) ctx_.persistent.insert_or_assign(name, std::move(value));
    persistentChanged_ = true;
    const bool ok = restore(back.state, back.history).has_value();
    autosave_ = false; // going back to a choice isn't a new decision
    return ok;
}

std::expected<std::string, std::string> Game::restore(std::string_view data, std::optional<std::size_t> keepHistory) {
    std::istringstream in{std::string(data)};
    std::string line;
    if (!std::getline(in, line) || line != kSaveMagic) return std::unexpected("not a VNext save file");

    std::uint64_t hash = 0;
    std::string note;
    std::optional<std::size_t> resume;
    std::optional<std::uint64_t> random; // (none written: the game's own goes on)
    std::optional<Trigger> trigger;
    std::optional<Pass> pass;
    int hidden = 0;
    std::vector<BoxLine> others;
    std::vector<std::size_t> stack;
    Variables vars;
    std::vector<Stage::ObjectState> objects;
    std::string background, backgroundAnimation, backgroundPlayed, music, flowchartNode;
    bool backgroundLoop = true;
    float backgroundVolume = 1.0f;
    SDL_FPoint backgroundPart{0.5f, 0.5f};
    int flowchartOption = -1;
    float musicVolume = 1.0f;
    std::map<std::string, Loop, std::less<>> loops;
    std::vector<std::string> screens;
    PhoneData phone;
    PhoneData* ph = nullptr; // (once its line is read: its things come after it)
    std::string chatScene;
    std::uint32_t chatWaits = 0; // (the message the chat scene waits for, still coming)
    std::uint32_t messageCount = 0, arrivals = 0;
    std::optional<CallNow> callNow;
    std::map<std::string, std::string, std::less<>> expressions;
    std::vector<PageLine> page;
    std::vector<HistoryEntry> history;
    std::optional<std::size_t> prompt;
    cmd::Weather weather;
    cmd::Effect effect;
    std::string cameraOwn, cameraPlayed;
    Animation::Look cameraStart;

    // The exact command when the script is unchanged; otherwise scene + offset.
    auto resolve = [&](std::istream& s) -> std::optional<std::size_t> {
        std::string label;
        std::size_t offset = 0, pc = 0;
        if (!(s >> label >> offset >> pc)) return std::nullopt;
        if (hash == script_.hash && pc <= script_.code.size()) return pc; // (the end: a scene played on its own returns there)
        if (auto at = script_.findScene(label)) {
            note = std::format("The script changed since this save, so it goes on near '{}'.", label);
            return std::min(*at + offset, script_.code.size());
        }
        return std::nullopt;
    };

    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        std::string rest;
        if (const auto sp = line.find(' '); sp != std::string::npos) rest = line.substr(sp + 1);

        if (key == "hash") ls >> hash;
        else if (key == "resume") resume = resolve(ls);
        else if (std::uint64_t seed = 0; key == "random" && ls >> seed) random = seed;
        else if (key == "trigger") {
            const auto at = resolve(ls);
            if (int lines = 0; at && ls >> lines) trigger = Trigger{.at = *at, .lines = lines};
        } else if (Pass passed; key == "pass" && ls >> passed.depth >> passed.lines) pass = passed;
        else if (key == "hidden") ls >> hidden;
        else if (key == "box") { // a line in another textbox: all there
            int r = 0, g = 0, b = 0;
            std::string box, speakerKey, speaker, face, source;
            ls >> r >> g >> b >> box >> speakerKey >> speaker >> face >> source;
            auto none = [](const std::string& s) { return s == "-" ? std::string{} : s; };
            BoxLine l = boxLine(unescape(box), none(unescape(speaker)), {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255},
                                unescape(source), script_.character(none(unescape(speakerKey))));
            l.face = none(unescape(face));
            l.reveal = static_cast<float>(l.length);
            others.push_back(std::move(l));
        } else if (key == "call") {
            if (auto pc = resolve(ls)) stack.push_back(*pc);
        } else if (key == "variable") {
            std::string name, value;
            ls >> name;
            std::getline(ls >> std::ws, value);
            vars.insert_or_assign(name, decodeValue(value));
        } else if (key == "object") {
            objects.push_back(readObject(ls));
        } else if (key == "background") { // (its name, then its animations: key=value)
            ls >> background;
            std::string field;
            while (ls >> field)
                if (field.starts_with("animation=")) backgroundAnimation = unescape(field.substr(10));
                else if (field.starts_with("played=")) backgroundPlayed = unescape(field.substr(7));
                else if (field.starts_with("loop=")) backgroundLoop = toInt(Value(field.substr(5))) != 0;
                else if (field.starts_with("volume=")) backgroundVolume = static_cast<float>(toInt(Value(field.substr(7)))) / 1000.0f;
                else if (field.starts_with("x=")) backgroundPart.x = static_cast<float>(toInt(Value(field.substr(2)))) / 10000.0f; // (the part that shows)
                else if (field.starts_with("y=")) backgroundPart.y = static_cast<float>(toInt(Value(field.substr(2)))) / 10000.0f;
        } else if (key == "flowchartpoint") {
            ls >> flowchartNode >> flowchartOption;
        } else if (key == "music") {
            music = rest;
        } else if (key == "musicvol") {
            musicVolume = static_cast<float>(toInt(rest)) / 1000.0f;
        } else if (key == "loop") { // (its channel, how loud, its sound type, its name, its file)
            std::string channel, type, name, file;
            long volume = 1000;
            ls >> channel >> volume >> type >> name;
            std::getline(ls >> std::ws, file);
            loops.insert_or_assign(unescape(channel), Loop{unescape(name), file, static_cast<float>(volume) / 1000.0f, unescape(type)});
        } else if (key == "screen") {
            screens.push_back(rest);
        } else if (key == "pphone") { // (its things come next)
            ls >> messageCount >> arrivals;
            ph = &phone;
        } else if (key == "pmsg") {
            int kind = 0, read = 0, seen = 0, deleted = 0, edited = 0, r = 0, g = 0, b = 0;
            long typing = 0;
            MessageItem m;
            std::string from, with, character, expression, text, file, time, date, reaction;
            ls >> kind >> read >> seen >> deleted >> edited >> typing >> m.number >> r >> g >> b >> from >> with >> character >> expression >> text >> file >> time >>
                date >> reaction;
            m.kind = static_cast<MessageItem::Kind>(kind);
            m.read = read != 0;
            m.seen = seen != 0;
            m.deleted = deleted != 0;
            m.edited = edited != 0;
            m.typing = static_cast<float>(typing) / 1000.0f;
            m.colour = {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255};
            m.from = unfield(from);
            m.with = unfield(with);
            m.character = unfield(character);
            m.expression = unfield(expression);
            m.text = unfield(text);
            m.file = unfield(file);
            m.time = unfield(time);
            m.date = unfield(date);
            m.reaction = unfield(reaction);
            if (ph) ph->messages.push_back(std::move(m));
        } else if (key == "pcall" && ph) {
            int outgoing = 0, missed = 0, busy = 0, seen = 0, r = 0, g = 0, b = 0;
            CallItem c;
            std::string who, time, date;
            ls >> outgoing >> missed >> busy >> seen >> c.seconds >> r >> g >> b >> who >> time >> date;
            c.outgoing = outgoing != 0;
            c.missed = missed != 0;
            c.busy = busy != 0;
            c.seen = seen != 0;
            c.colour = {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255};
            c.who = unfield(who);
            c.time = unfield(time);
            c.date = unfield(date);
            ph->calls.push_back(std::move(c));
        } else if (key == "pscene") {
            std::string with;
            ls >> with >> chatWaits;
            chatScene = unfield(with);
        } else if (key == "pcallnow") {
            int outgoing = 0, r = 0, g = 0, b = 0;
            long time = 0;
            std::string who, character;
            ls >> outgoing >> time >> r >> g >> b >> who >> character;
            if (const auto pc = resolve(ls))
                callNow = CallNow{.who = unfield(who), .character = unfield(character), .colour = {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255},
                                  .outgoing = outgoing != 0, .talking = true, .time = static_cast<float>(time) / 1000.0f, .at = *pc};
        } else if (key == "expr") {
            std::string character, expression;
            ls >> character >> expression;
            expressions.insert_or_assign(unescape(character), unescape(expression));
        } else if (key == "hist") {
            int r = 0, g = 0, b = 0, choice = 0;
            long madeUp = 0, madeUpSpeed = 1000;
            std::string speaker, voiceFile, text;
            std::size_t spans = 0;
            if (!(ls >> r >> g >> b >> choice >> madeUp >> madeUpSpeed >> speaker >> voiceFile >> text >> spans)) continue;
            HistoryEntry h{.speaker = unfield(speaker), .colour = {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255}, .text = unfield(text),
                           .choice = choice != 0, .voice = unfield(voiceFile), .madeUpVoice = static_cast<float>(madeUp) / 1000.0f,
                           .madeUpVoiceSpeed = static_cast<float>(madeUpSpeed) / 1000.0f};
            for (std::size_t i = 0; i < spans; ++i) {
                TextSpan s;
                std::string colour, ruby;
                unsigned style = 0;
                if (!(ls >> s.begin >> s.end >> colour >> style >> ruby)) break;
                if (colour != "-") s.colour = parseColour(colour);
                s.style = static_cast<TTF_FontStyleFlags>(style);
                s.ruby = unfield(ruby);
                h.spans.push_back(std::move(s));
            }
            history.push_back(std::move(h));
        } else if (key == "prompt") {
            prompt = resolve(ls);
        } else if (key == "page") {
            int r = 0, g = 0, b = 0;
            std::string speaker, text;
            ls >> r >> g >> b >> speaker >> text;
            speaker = unescape(speaker);
            page.push_back({speaker == "-" ? std::string{} : speaker, {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255},
                            unescape(text), {}, std::nullopt});
        } else if (key == "weather") {
            long amount = 1000, speed = 1000, wind = 0;
            int behind = 0;
            std::string colourText, image;
            ls >> weather.kind >> amount >> speed >> wind >> behind >> colourText >> image;
            weather.amount = static_cast<float>(amount) / 1000.0f;
            weather.speed = static_cast<float>(speed) / 1000.0f;
            weather.wind = static_cast<float>(wind) / 1000.0f;
            weather.behind = behind != 0;
            if (colourText != "-") weather.tint = parseColour(colourText);
            if (image != "-") weather.image = image;
        } else if (key == "effect") {
            long amount = 1000;
            ls >> effect.looks >> amount;
            effect.amount = static_cast<float>(amount) / 1000.0f;
        } else if (key == "camera") {
            std::string own, played, fadeColour, colour, shape;
            long x = 0, y = 0, opacity = 1000, zoom = 1000, rotation = 0, fade = 0, wipe = 1000;
            ls >> own >> played >> x >> y >> opacity >> zoom >> rotation >> fade >> wipe >> fadeColour >> colour >> shape;
            auto named = [](const std::string& s) { return s == "-" ? std::string{} : unescape(s); };
            auto thousandths = [](long n) { return static_cast<float>(n) / 1000.0f; };
            cameraOwn = named(own);
            cameraPlayed = named(played);
            cameraStart = {.x = thousandths(x), .y = thousandths(y), .opacity = thousandths(opacity), .zoom = thousandths(zoom), .rotation = thousandths(rotation),
                           .colour = colour == "-" ? std::nullopt : parseColour(colour), .fade = thousandths(fade),
                           .fadeColour = parseColour(fadeColour).value_or(SDL_Color{0, 0, 0, 255}), .wipe = thousandths(wipe), .wipeShape = named(shape)};
        }
    }
    if (!resume) return std::unexpected("this save points to a scene that no longer exists");

    stage_.reset();
    for (const auto& o : objects) {
        const CharacterDef* def = o.character.empty() ? nullptr : script_.character(o.character);
        if (!o.character.empty() && !def) continue; // that character no longer exists
        stage_.restore(o, def);
        // Its animations: from their start if they play again and again, else as they ended. (One no
        // longer in the scripts: none.)
        stage_.animate(o.tag, animation(o.animation), true, true);
        stage_.animate(o.tag, animation(o.played), false, true);
    }
    if (!background.empty()) {
        stage_.setBackground(background, nullptr, true, backgroundLoop, backgroundVolume, backgroundPart);
        stage_.animateBackground(animation(backgroundAnimation), true, true);
        stage_.animateBackground(animation(backgroundPlayed), false, true);
    }
    stage_.skipMovies(); // (movies that play once were over by then: at their last image)
    subtitles_ = stage_.backgroundMovie() ? subtitlesOf(ctx_.assets, background) : std::vector<Subtitle>{};
    flowchartNode_ = flowchartNode;
    flowchartOption_ = flowchartOption;
    stage_.setWeather(weather, 0.0f);
    stage_.setEffect(effect, true);
    stage_.restoreCamera(animation(cameraOwn), animation(cameraPlayed), cameraStart);
    page_ = std::move(page);
    vars_ = std::move(vars);
    callStack_ = std::move(stack);
    screens_ = std::move(screens);
    phone_ = std::move(phone);
    messageCount_ = messageCount;
    arrivals_ = arrivals;
    const bool phoneUp = !chat_.empty() || call_; // (the story's chat scene or call: rolling back to before it puts the phone away)
    chat_ = chatScene;
    chatWait_ = chatWaits;
    call_ = callNow;
    change_.reset();
    stopRinging();
    if (!chat_.empty()) requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "chat", .phone = chat_}); // (on with it)
    if (call_) requests_.push_back(cmd::Ui{.verb = "show phone", .arg = "call"});
    if (phoneUp && chat_.empty() && !call_) requests_.push_back(cmd::Ui{.verb = "hide phone"});
    expressions_ = std::move(expressions);
    music_ = music;
    musicVolume_ = musicVolume;
    ctx_.sound.stopVoice();
    if (music.empty()) ctx_.sound.stopMusic(0.5f);
    else ctx_.sound.playMusic(ctx_.assets.sound(script_.soundOf(music, "music").file), true, 0.5f, musicVolume); // the same song keeps playing
    // Looping sounds: a load starts them over; rollback keeps those that are already right (each as its type follows).
    const bool rollingBack = keepHistory.has_value();
    if (!rollingBack) ctx_.sound.stopChannel("", 0.3f);
    for (const auto& [channel, loop] : loops_)
        if (rollingBack && (!loops.contains(channel) || loops.at(channel).file != loop.file)) ctx_.sound.stopChannel(channel, 0.3f);
    for (const auto& [channel, loop] : loops)
        if (!rollingBack || !loops_.contains(channel) || loops_.at(channel).file != loop.file) {
            const auto t = script_.soundTypes.find(loop.type);
            ctx_.sound.playChannel(channel, ctx_.assets.sound(loop.file), true, loop.volume, 0.5f, 0.0f, t != script_.soundTypes.end() ? t->second.follows : SoundSlider::Sound,
                                   loop.type);
        }
    typePlaying_.clear(); // (a type's one: what loops on it, stopped by its name)
    for (const auto& [channel, loop] : loops)
        if (channel.starts_with("type:")) typePlaying_.insert_or_assign(channel.substr(5), loop.name);
    loops_ = std::move(loops);

    // Rollback keeps the entries before its line (counting those let go); a load has the save's.
    if (keepHistory) history_.resize(std::min(history_.size(), *keepHistory > historyDropped_ ? *keepHistory - historyDropped_ : 0));
    else {
        historyDropped_ = 0;
        history_ = std::move(history);
    }
    choiceItems_.clear();
    trigger_ = trigger;
    if (const cmd::Menu* m = trigger && trigger->at < script_.code.size() ? std::get_if<cmd::Menu>(&script_.code[trigger->at].command) : nullptr)
        listChoices(*m);
    else trigger_.reset();
    pass_ = pass;
    asked_.reset();
    parked_.reset();
    chatOpen_.clear();
    putAway_.clear();
    hiddenLines_ = hidden;
    others_ = std::move(others);
    together_ = !others_.empty(); // (the last line of a together block is said again)
    error_.clear();
    skip_ = uiHidden_ = menuPrompt_ = false;
    pc_ = resumePc_ = *resume;
    if (random) random_ = *random;
    // A choice with its question on screen: that line back as it was, all there at once, then the choice.
    if (const auto* asks = prompt && *prompt < script_.code.size() ? std::get_if<cmd::Say>(&script_.code[*prompt].command) : nullptr) {
        const CharacterDef* def = asks->character.empty() ? nullptr : script_.character(asks->character);
        say(def ? def->name : asks->speaker, def ? def->nameColour : gameSettings().narrationColour, asks->text, def);
        reveal_ = static_cast<float>(length_);
        passedStops_ = stops_.size();
        noWait_ = false;
        saidAt_ = *prompt;
    } else {
        text_.clear(); // (none: no question with it, nor one left from before rolling back)
    }
    if (std::ranges::any_of(phone_.messages, [&](const MessageItem& m) { return chatWait_ && m.number == chatWait_ && m.typing > 0.0f; }))
        wait_ = Wait::Chat; // (a chat scene's message still coming: on once it's come)
    else {
        wait_ = Wait::None;
        run();
    }
    return note;
}

Variables Game::savedVariables(std::string_view state) {
    Variables vars;
    std::istringstream in{std::string(state)};
    for (std::string line; std::getline(in, line);)
        if (line.starts_with("variable ")) {
            std::istringstream ls(line.substr(9));
            std::string name, value;
            ls >> name;
            std::getline(ls >> std::ws, value);
            vars.insert_or_assign(name, decodeValue(value));
        }
    return vars;
}

void Engine::saveSlot(std::size_t index, std::string question) {
    if (sampling_ || mode_ != Mode::Game) return;
    auto& cards = slots_.cards();
    if (index >= cards.size()) return;
    auto save = [this, index, slot = cards[index].slot] {
        if (saveToSlot(slot)) slots_.refresh(index);
    };
    if (cards[index].info.exists) ask(std::move(question), save);
    else save();
}

void Engine::loadSlot(std::size_t index, std::string question) {
    if (sampling_) return;
    auto& cards = slots_.cards();
    if (index >= cards.size() || !cards[index].info.exists) return;
    auto load = [this, slot = cards[index].slot] { loadFromSlot(slot); };
    if (mode_ == Mode::Game) ask(std::move(question), load);
    else load();
}

void Engine::slotDelete(std::size_t index, std::string question) {
    if (sampling_) return;
    auto& cards = slots_.cards();
    if (index >= cards.size() || !cards[index].info.exists) return;
    ask(std::move(question), [this, index, slot = cards[index].slot] {
        saves_.remove(slot);
        slots_.refresh(index);
        countSaves();
    });
}

// Saving and loading say nothing by themselves (a game's button can show a screen that does). What
// goes wrong is logged.
void Engine::quickSave() {
    saveToSlot(saves_.rotatingSlot("quick"));
}

void Engine::quickLoad() {
    if (const auto slot = saves_.newest("quick")) loadFromSlot(*slot);
    else logWarn("There's no quick save to load.");
}

void Engine::countSaves() {
    hasSave_ = saves_.newest().has_value();
    hasQuickSave_ = saves_.newest("quick").has_value();
}

void Engine::autosave() {
    if (gameSettings().autosave && mode_ == Mode::Game && game_->canSave()) saveToSlot(saves_.rotatingSlot("auto"));
}

bool Engine::saveToSlot(const std::string& slot) {
    if (mode_ != Mode::Game || !game_->canSave()) {
        logWarn("Can't save now: the story isn't at a line that can be saved.");
        return false;
    }
    const SurfacePtr thumbnail = captureThumbnail();
    if (!saves_.write(slot, game_->save(), game_->summary(), thumbnail.get())) {
        logWarn("Can't write the save '{}': {}.", slot, SDL_GetError());
        return false;
    }
    hasSave_ = true;
    hasQuickSave_ = hasQuickSave_ || slot.starts_with("quick-");
    return true;
}

bool Engine::loadFromSlot(const std::string& slot) {
    const auto data = saves_.read(slot);
    if (!data) {
        logWarn("Can't read the save '{}'.", slot);
        return false;
    }
    const auto note = resume(*data);
    if (!note) return false;
    if (!note->empty()) logInfo("{}", *note);
    return true;
}

// Plays on from a saved state (a save, a point on the flowchart, the game before a reload), in a fresh
// game, so a broken one leaves the game that was on alone.
std::expected<std::string, std::string> Engine::resume(std::string_view state) {
    auto game = makeGame();
    auto note = game->load(state);
    if (!note) {
        logWarn("Can't load the save: {}.", note.error());
        return note;
    }
    leaveTitle();
    game_ = std::move(game);
    mode_ = Mode::Game;
    opening_ = false;
    storyStarted_ = true;
    closeAll();
    return note;
}

SurfacePtr Engine::captureThumbnail() {
    // Draw just the game (no menus) into an offscreen canvas-sized target, then shrink it.
    SDL_Renderer* r = renderer_.get();
    TexturePtr target{SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                                        static_cast<int>(canvas_.w), static_cast<int>(canvas_.h))};
    if (!target) return nullptr;
    SurfacePtr full;
    {
        const RenderTarget _(r, target.get());
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        drawGame(true);
        full.reset(SDL_RenderReadPixels(r, nullptr));
    }
    if (!full) return nullptr;

    // Two smaller steps look much smoother than one big linear downscale.
    const int w = 480, h = static_cast<int>(std::lround(480.0f * canvas_.h / canvas_.w));
    SurfacePtr half{SDL_CreateSurface(w * 4 / 3, h * 4 / 3, SDL_PIXELFORMAT_RGBA32)};
    SurfacePtr thumbnail{SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32)};
    if (!half || !thumbnail) return nullptr;
    SDL_SetSurfaceBlendMode(full.get(), SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(half.get(), SDL_BLENDMODE_NONE);
    SDL_BlitSurfaceScaled(full.get(), nullptr, half.get(), nullptr, SDL_SCALEMODE_LINEAR);
    SDL_BlitSurfaceScaled(half.get(), nullptr, thumbnail.get(), nullptr, SDL_SCALEMODE_LINEAR);
    return thumbnail;
}

} // namespace vn
