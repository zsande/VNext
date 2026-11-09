// The story's music, sounds, voices and movies, and their captions and subtitles.
#include "Game.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <climits>
#include <format>
#include <ranges>

namespace vn {

// Music, a voice or a sound played, or stopped (a voice keeps playing through the next line; a sound on a
// channel of its own may loop).
void Game::play(const cmd::Play& c, bool fast) {
    const std::string file = c.file.contains('{') ? fillValues(c.file, lookup_) : c.file; // (a value's: sound {message.sound})
    const std::string path = file.empty() ? std::string{} : ctx_.assets.sound(file);
    if (!file.empty() && !fast && c.track != cmd::Play::Track::Voice) caption(file, c.track == cmd::Play::Track::Music);
    switch (c.track) {
    case cmd::Play::Track::Music:
        music_ = file;
        musicVolume_ = c.volume;
        have(RecordKind::Song, file); // the music room
        if (file.empty()) ctx_.sound.stopMusic(c.fade);
        else ctx_.sound.playMusic(path, c.loop, c.fade, c.volume, c.fadeOut);
        break;
    case cmd::Play::Track::Voice:
        if (file.empty()) return ctx_.sound.stopVoice();
        if (!fast) ctx_.sound.playVoice(path, c.volume);
        pendingVoice_ = LineVoice{file, c.volume, c.textFollows}; // keep playing through the next line
        break;
    case cmd::Play::Track::Sound:
        if (file.empty()) { // stop sound [NAME]
            ctx_.sound.stopChannel(c.channel, c.fade);
            if (c.channel.empty()) loops_.clear();
            else loops_.erase(c.channel);
        } else if (c.channel.empty()) {
            if (!fast) ctx_.sound.playSound(path, c.volume, c.fade, c.fadeOut);
        } else {
            ctx_.sound.playChannel(c.channel, path, c.loop, c.volume, c.fade, c.fadeOut.value_or(0.0f));
            if (c.loop) loops_.insert_or_assign(c.channel, Loop{file, c.volume});
            else loops_.erase(c.channel);
        }
        break;
    }
}

// What a sound (or song) that begins is, as its caption line says: a few seconds at the top (the newest three), on the
// captions screen (which comes up with them while the player's Captions are on, and whenever the game opens it).
void Game::caption(std::string_view sound, bool music) {
    const auto it = script_.captions.find(lower(sound));
    if (it == script_.captions.end()) return;
    std::string text = (music ? "\u266a " : "") + richText(it->second, lookup_, &script_.palette).text;
    std::erase_if(captions_, [&](const Caption& c) { return c.text == text; }); // (again: anew)
    captions_.push_back({.text = std::move(text), .music = music, .left = music ? 4.0f : 3.0f});
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

// The movie's subtitle now (with Captions on): its .srt's line for this moment, if any.
std::string Game::subtitle() const {
    if (!movie_ || subtitles_.empty() || !ctx_.settings.captions) return {};
    const double t = movie_->time();
    const auto it = std::ranges::find_if(subtitles_, [&](const Subtitle& s) { return s.from <= t && t < s.to; });
    return it != subtitles_.end() ? it->text : std::string{};
}

// A movie, full screen: past it when skipping (if it can be skipped); one that can't play is said, and in
// developer mode a card with its name shows for a moment instead.
void Game::playMovie(const cmd::Movie& c, std::size_t here, bool fast) {
    have(RecordKind::Gallery, c.file); // a movie in the gallery
    const bool skippable = c.skippable && ctx_.settings.skipMovies; // (the player can say no too)
    const bool stopsHere = watchThere_ && (*watchThere_)(here); // (playTo() stops at one there: its first image)
    if ((fast && skippable) || (watch_ != SIZE_MAX && !stopsHere)) return; // skipping goes past movies, and so does playTo()
    const auto file = ctx_.assets.findMovie(c.file);
    movie_ = file ? std::make_unique<Movie>(ctx_.renderer, ctx_.sound, *file, c.loop, c.volume) : nullptr;
    subtitles_ = subtitlesOf(ctx_.assets, c.file);
    if (!movie_ || !*movie_) {
        movie_.reset();
        logWarn("{}: {}", script_.where(script_.code[here]),
                file ? std::format("Can't play '{}'.", *file) : std::format("There's no movie called '{}'.", c.file));
        if (!gameSettings().developer) return; // players: the story goes on
        movieStandIn_ = c.file;    // a labelled card for a moment instead
        timer_ = 3.0f;
    }
    movieSkippable_ = skippable;
    ctx_.sound.stopVoice();
    waitFor(Wait::Movie, here);
}

void Game::playMovieRound(float dt) {
    if (!movie_) return;
    const bool loop = movie_->looping();
    movie_->setLoop(true);
    movie_->update(dt);
    movie_->setLoop(loop);
}

void Game::endMovie() {
    movie_.reset();
    subtitles_.clear();
    movieStandIn_.clear();
    wait_ = Wait::None;
    run();
}

} // namespace vn
