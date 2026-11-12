// The story's music and sounds and their captions, and the background movie's subtitles.
#include "Game.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <climits>
#include <format>
#include <ranges>

namespace vn {

// A sound played as its type plays it (the line saying otherwise): music the song, the one before fading out; a type
// played one at a time on its own channel, the one there fading out; one played many at once by its name when it has
// one (a named sound, or one that loops: stopped by it), else as it comes. Or stopped: by its name, or all of a type.
void Game::play(const cmd::Play& c, bool fast) {
    const std::string name = c.name.contains('{') ? fillValues(c.name, lookup_) : c.name; // (a value's: play {message.sound})
    if (c.stop) return stopSound(lower(trim(name)), c.fade.value_or(0.0f));
    if (trim(name).empty()) return;
    Script::SoundPlay s = script_.soundOf(name, c.type);
    if (c.loop) s.loop = *c.loop;
    if (c.volume) { // (the line's own, times its type's)
        const auto t = script_.soundTypes.find(s.type);
        s.volume = *c.volume * (t != script_.soundTypes.end() ? t->second.volume : 1.0f);
    }
    if (c.fade) s.fade = *c.fade;
    if (c.fadeOut) s.fadeOut = c.fadeOut;
    const std::string path = ctx_.assets.sound(s.file), key = lower(trim(name));
    if (!fast) caption(name, s.file, s.slider == SoundSlider::Music);
    if (s.type == "music") { // the song (the music room's: unlocked)
        music_ = name;
        musicVolume_ = s.volume;
        have(RecordKind::Song, s.file);
        if (lower(s.file) != key) have(RecordKind::Song, name);
        ctx_.sound.playMusic(path, s.loop, s.fade, s.volume, s.fadeOut);
        return;
    }
    const std::string channel = s.oneAtATime ? "type:" + s.type : s.named || s.loop ? key : std::string{};
    if (channel.empty()) {
        if (!fast) ctx_.sound.playSound(path, s.volume, s.fade, s.fadeOut, s.slider, s.type);
        return;
    }
    ctx_.sound.playChannel(channel, path, s.loop, s.volume, s.fade, s.fadeOut.value_or(s.oneAtATime ? 0.5f : 0.0f), s.slider, s.type);
    if (s.oneAtATime) typePlaying_.insert_or_assign(s.type, key);
    if (s.loop) loops_.insert_or_assign(channel, Loop{key, s.file, s.volume, s.type});
    else loops_.erase(channel);
}

// stop NAME|TYPE: the song (music, or the one playing as music by its name), a type's own (one at a time: what plays on
// it; many at once: every one of it), or one by its name (its own channel, or its type's when that's what plays there).
void Game::stopSound(const std::string& what, float fade) {
    auto stopChannel = [&](const std::string& channel) {
        ctx_.sound.stopChannel(channel, fade);
        loops_.erase(channel);
    };
    if (script_.soundTypes.contains(what)) {
        if (what == "music") {
            music_.clear();
            ctx_.sound.stopMusic(fade);
            return;
        }
        ctx_.sound.stopType(what, fade);
        if (what == "voice") ctx_.sound.stopVoice(); // (the line's too)
        std::erase_if(loops_, [&](const auto& entry) { return entry.second.type == what; });
        typePlaying_.erase(what);
        return;
    }
    if (!music_.empty() && lower(music_) == what) {
        music_.clear();
        ctx_.sound.stopMusic(fade);
    }
    stopChannel(what);
    for (auto it = typePlaying_.begin(); it != typePlaying_.end();)
        if (it->second == what) {
            stopChannel("type:" + it->first);
            it = typePlaying_.erase(it);
        } else {
            ++it;
        }
}

void Game::playNamed(std::string_view name, float volume) {
    if (trim(name).empty()) return;
    const Script::SoundPlay s = script_.soundOf(name);
    ctx_.sound.playSound(ctx_.assets.sound(s.file), s.volume * volume, s.fade, std::nullopt, s.slider, s.type);
}

// What a sound (or song) that begins is, as its caption line says (a named sound's, else its file's): a few seconds at
// the top (the newest three), on the captions screen (which comes up with them while the player's Captions are on, and
// whenever the game opens it).
void Game::caption(std::string_view name, std::string_view file, bool music) {
    auto it = script_.captions.find(lower(trim(name)));
    if (it == script_.captions.end()) it = script_.captions.find(lower(file));
    if (it == script_.captions.end()) return;
    RichText words = richText(it->second, lookup_, &script_.palette);
    if (music)
        for (TextSpan& s : words.spans) s.begin += 2, s.end += 2; // (after the note and its space)
    std::string text = (music ? "\u266a " : "") + words.text;
    std::erase_if(captions_, [&](const Caption& c) { return c.text == text; }); // (again: anew)
    captions_.push_back({.text = std::move(text), .spans = std::move(words.spans), .music = music, .left = music ? 4.0f : 3.0f});
    if (captions_.size() > 3) captions_.erase(captions_.begin());
}

// A movie's subtitles: its .srt (SubRip: blocks of a number, a time line "00:00:01,000 --> 00:00:04,000" and its
// words, a blank line after each; tags such as <i> left out).
std::vector<Game::Subtitle> Game::subtitlesOf(const Assets& assets, std::string_view movie) {
    std::vector<Subtitle> out;
    const auto path = assets.findSubtitles(movie);
    const auto text = path ? assets.files().readText(*path) : std::nullopt;
    if (!text) return out;
    auto seconds = [](std::string_view s) -> std::optional<double> { // (HH:MM:SS,mmm, or a point for the comma)
        s = trim(s);
        auto read = [&](int& n, std::string_view then) { // (a whole number, then one of `then`'s characters if it has any)
            const auto [stop, error] = std::from_chars(s.data(), s.data() + s.size(), n);
            if (error != std::errc{}) return false;
            s.remove_prefix(static_cast<std::size_t>(stop - s.data()));
            if (then.empty()) return true;
            if (s.empty() || !then.contains(s.front())) return false;
            s.remove_prefix(1);
            return true;
        };
        int h = 0, m = 0, sec = 0, ms = 0;
        if (!read(h, ":") || !read(m, ":") || !read(sec, ",.") || !read(ms, {})) return std::nullopt;
        return h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
    };
    Subtitle now;
    bool timed = false;
    auto done = [&] {
        if (timed && !now.text.empty()) out.push_back(std::move(now));
        now = {};
        timed = false;
    };
    for (auto&& part : std::views::split(std::string_view(*text), '\n')) {
        const std::string_view line = trim(std::string_view(part.begin(), part.end())); // (\r too)
        if (line.empty()) {
            done();
        } else if (const auto arrow = line.find("-->"); !timed && arrow != std::string_view::npos) {
            const auto from = seconds(line.substr(0, arrow)), to = seconds(line.substr(arrow + 3));
            if (from && to) now.from = *from, now.to = *to, timed = true;
        } else if (timed) {
            bool tag = false;
            if (!now.text.empty()) now.text += '\n';
            for (const char ch : line)
                if (ch == '<') tag = true;
                else if (ch == '>') tag = false;
                else if (!tag) now.text += ch;
        }
    }
    done();
    return out;
}

// The background movie's subtitle now (with Captions on): its .srt's line for this moment, if any, while it plays.
std::string Game::subtitle() const {
    const Movie* movie = stage_.backgroundMovie();
    if (!movie || movie->ended() || subtitles_.empty() || !ctx_.settings.captions) return {};
    const double t = movie->time();
    const auto it = std::ranges::find_if(subtitles_, [&](const Subtitle& s) { return s.from <= t && t < s.to; });
    return it != subtitles_.end() ? it->text : std::string{};
}

} // namespace vn
