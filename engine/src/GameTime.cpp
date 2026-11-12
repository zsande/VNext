// The game's time: its days, dates and clock, the parts of the day, and time passing in the story.
#include "Game.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace vn {

int daysFromDate(int year, int month, int day) { // (the proleptic Gregorian calendar's days, from 1970)
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int ofEra = year - era * 400;
    const int ofYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    return era * 146097 + ofEra * 365 + ofEra / 4 - ofEra / 100 + ofYear - 719468;
}

std::array<int, 3> dateFromDays(int days) {
    days += 719468;
    const int era = (days >= 0 ? days : days - 146096) / 146097;
    const int ofEra = days - era * 146097;
    const int year = (ofEra - ofEra / 1460 + ofEra / 36524 - ofEra / 146096) / 365;
    const int ofYear = ofEra - (365 * year + year / 4 - year / 100);
    const int m = (5 * ofYear + 2) / 153;
    const int month = m < 10 ? m + 3 : m - 9;
    return {year + era * 400 + (month <= 2 ? 1 : 0), month, ofYear - (153 * m + 2) / 5 + 1};
}

namespace {

// `text` with each {name} in it replaced by its value.
std::string filledIn(std::string text, std::initializer_list<std::pair<std::string_view, std::string>> values) {
    for (const auto& [name, value] : values) {
        const std::string tag = std::format("{{{}}}", name);
        for (std::size_t at = text.find(tag); at != std::string::npos; at = text.find(tag, at + value.size())) text.replace(at, tag.size(), value);
    }
    return text;
}

} // namespace

// (the game's files share it: Game.hpp)
RealNow realNow() {
    SDL_Time now = 0;
    SDL_DateTime t{};
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &t, true)) return {};
    return {daysFromDate(t.year, t.month, t.day), t.hour * 60 + t.minute, static_cast<double>(now) / 1e9};
}

Game::When Game::timeNow() const {
    const GameSettings& g = gameSettings();
    auto kept = [&](std::string_view name) -> std::optional<int> { // (the variable, as the story has it)
        const auto it = vars_.find(name);
        return it != vars_.end() ? std::optional(static_cast<int>(std::lround(toNumber(it->second)))) : std::nullopt;
    };
    if (g.time == TimeKind::Real) {
        const RealNow now = realNow();
        return {std::max(1, now.date - kept("time_started").value_or(now.date) + 1), now.minutes};
    }
    return {std::max(1, kept("time_day").value_or(1)), ((kept("time_minutes").value_or(g.timeStartMinutes) % 1440) + 1440) % 1440};
}

std::string Game::partAt(int minutes) const {
    const auto& parts = gameSettings().dayParts;
    std::string out = parts.back().first; // (before the first one starts: the last one's, from the night before)
    for (const auto& [name, start] : parts)
        if (start <= minutes) out = name;
    return out;
}

std::string Game::clockText(int minutes) const {
    const GameSettings& g = gameSettings();
    if (g.time == TimeKind::Days) return script_.translated(upperFirst(partAt(minutes))); // (no clock: the part of the day)
    const int hours = minutes / 60, m = minutes % 60;
    if (!g.clock12) return std::format("{}:{:02}", hours, m);
    return std::format("{}:{:02} {}", hours % 12 == 0 ? 12 : hours % 12, m, script_.translated(hours < 12 ? "AM" : "PM"));
}

int Game::dateOf(int day) const {
    const GameSettings& g = gameSettings();
    if (g.time == TimeKind::Real) {
        const auto started = vars_.find("time_started");
        return started != vars_.end() ? static_cast<int>(std::lround(toNumber(started->second))) + day - 1 : realNow().date;
    }
    return g.timeStartDate >= 0 ? g.timeStartDate + day - 1 : -1;
}

int Game::weekdayOn(int day) const {
    if (const int date = dateOf(day); date >= 0) return weekdayOf(date);
    const int first = gameSettings().timeStartWeekday;
    return first >= 0 ? (first + day - 1) % 7 : -1;
}

std::string Game::dateText(int day) const {
    const int date = dateOf(day), weekday = weekdayOn(day);
    const std::string named = weekday >= 0 ? script_.translated(kWeekdays[static_cast<std::size_t>(weekday)]) : std::string{};
    if (date < 0) return weekday >= 0 ? named : filledIn(script_.translated("Day {day}"), {{"day", std::to_string(day)}});
    const auto [year, month, dom] = dateFromDays(date);
    return filledIn(script_.translated("{weekday} {day} {month}"),
                    {{"weekday", named}, {"day", std::to_string(dom)}, {"month", script_.translated(kMonths[static_cast<std::size_t>(month - 1)])}, {"year", std::to_string(year)}});
}

std::optional<Value> Game::timeValue(std::string_view name) const {
    const When now = timeNow();
    const int date = dateOf(now.day), weekday = weekdayOn(now.day);
    if (name == "time_day") return now.day;
    if (name == "time_hour") return now.minutes / 60;
    if (name == "time_minute") return now.minutes % 60;
    // (the part of the day as the game names it, the weekday and the month in English, as the editor writes them: for if
    // lines, if time_weekday is "Friday"; shown to players: time_date's, in their language)
    if (name == "time_part") return partAt(now.minutes);
    if (name == "time_weekday") return weekday >= 0 ? std::string(kWeekdays[static_cast<std::size_t>(weekday)]) : std::string{};
    if (name == "time_month") return date >= 0 ? std::string(kMonths[static_cast<std::size_t>(dateFromDays(date)[1] - 1)]) : std::string{};
    if (name == "time_year") return date >= 0 ? dateFromDays(date)[0] : 0;
    if (name == "time_clock") return clockText(now.minutes); // (as it shows: in the player's language)
    if (name == "time_date") return dateText(now.day);
    if (name == "time_waiting") return wait_ == Wait::Clock ? 1 : 0; // (setting time real: the story waits for the player's clock...)
    if (name == "time_until") { // (...until then)
        const auto until = vars_.find("time_wait");
        if (wait_ != Wait::Clock || until == vars_.end()) return std::string{};
        const double left = toNumber(until->second) - realNow().seconds;
        return clockText(static_cast<int>(std::lround(now.minutes + std::max(0.0, left) / 60.0)) % 1440);
    }
    return std::nullopt;
}

std::string Game::backgroundFor(std::string_view image) const {
    const GameSettings& g = gameSettings();
    std::string base(image); // (one named for a part of the day already: its own name first)
    for (const auto& [part, start] : g.dayParts)
        if (base.size() > part.size() + 1 && base.ends_with("_" + part) && ctx_.assets.hasImage(base.substr(0, base.size() - part.size() - 1)))
            base.resize(base.size() - part.size() - 1);
    if (g.time == TimeKind::None || !g.backgroundsFollowTime || base.empty() || base.front() == '#') return std::string(image);
    const std::string version = base + "_" + partAt(timeNow().minutes);
    return ctx_.assets.hasImage(version) ? version : base == image ? std::string(image) : base;
}

void Game::timeMoved(When from, When to, bool fast) {
    if (!stage_.background().empty()) // (the background as it is in the new part of the day)
        if (const std::string shown = backgroundFor(stage_.background()); shown != stage_.background()) stage_.setBackground(shown, nullptr, true, stage_.backgroundLoops(), stage_.backgroundVolume(), stage_.backgroundPart());
    if (to.day != from.day && !fast && script_.screen("calendar")) waitForScreen({.name = "calendar"}, pc_); // (a new day: the calendar, when the screens have one)
}

void Game::passTime(const cmd::Time& c, std::size_t here) {
    const GameSettings& g = gameSettings();
    const When from = timeNow();
    const int first = g.dayParts.front().second; // (a day's first part: the next one after the last)
    When to = from;
    if (c.passes && c.passing > 0) {
        const int total = from.minutes + c.passing;
        to = {from.day + total / 1440, total % 1440};
    } else if (c.passes) { // to the next part of the day
        const auto next = std::ranges::find_if(g.dayParts, [&](const auto& p) { return p.second > from.minutes; });
        to = next != g.dayParts.end() ? When{from.day, next->second} : When{from.day + 1, first};
    } else {
        int at = c.minutes >= 0 ? c.minutes : from.minutes; // (no time of day said: the same time)
        if (!c.part.empty())
            if (const auto part = std::ranges::find(g.dayParts, c.part, &std::pair<std::string, int>::first); part != g.dayParts.end()) at = part->second;
        const bool already = c.part.empty() ? at == from.minutes : partAt(from.minutes) == c.part; // (it's then already)
        if (c.day > 0) { // that day, then
            to = c.day == from.day && already ? from : When{c.day, at};
        } else {
            const int days = c.weekday >= 0 && weekdayOn(from.day) >= 0 ? (c.weekday - weekdayOn(from.day) + 7) % 7 : 0;
            if (days == 0 && already) to = from;
            else if (days == 0 && at < from.minutes) to = {from.day + (c.weekday >= 0 ? 7 : 1), at}; // (past it: the next one)
            else to = {from.day + days, at};
        }
    }
    if (g.time != TimeKind::Real) {
        vars_.insert_or_assign("time_day", to.day);
        vars_.insert_or_assign("time_minutes", to.minutes);
        return timeMoved(from, to, skipping());
    }
    // The player's clock: the story waits for it, the textbox saying until when (developer mode, and the story
    // played to a line by the developer tools: on at once). A save made meanwhile waits for the same moment.
    const RealNow now = realNow();
    const auto kept = vars_.find("time_wait"), keptLine = vars_.find("time_wait_line");
    const double until = kept != vars_.end() && keptLine != vars_.end() && std::lround(toNumber(keptLine->second)) == static_cast<long>(here)
                           ? toNumber(kept->second)
                           : now.seconds - std::fmod(now.seconds, 60.0) + ((to.day - from.day) * 1440.0 + (to.minutes - from.minutes)) * 60.0;
    if (g.developer || watch_ != SIZE_MAX || until <= now.seconds) {
        vars_.erase("time_wait");
        vars_.erase("time_wait_line");
        return;
    }
    vars_.insert_or_assign("time_wait", until);
    vars_.insert_or_assign("time_wait_line", static_cast<int>(here));
    waitFrom_ = from;
    say({}, g.narrationColour, filledIn(script_.translated("The story goes on at {time}."), {{"time", clockText(to.minutes)}}), nullptr);
    reveal_ = static_cast<float>(length_);
    waitFor(Wait::Clock, here);
}

} // namespace vn
