// The extras: what the player has unlocked (the gallery, endings, songs, achievements, tips) and the music room.
#include "Engine.hpp"

#include <SDL3/SDL.h>
#include <algorithm>

namespace vn {

void Game::have(RecordKind kind, std::string_view name) {
    if (!script_.record(kind, name)) return;
    if (const std::string var = Script::recordVariable(kind, name); !truthy(variable(var))) setVariable(var, 1); // (a tip read stays read: 2)
}

// The tips a text shows ({tip NAME}...{/tip}): learned when it's said.
void Game::learnTips(std::string_view text) {
    (void)expand(text, lookup_, [&](std::string_view tag, std::string_view name, std::size_t) {
        if (tag == "tip") have(RecordKind::Tip, name);
    });
}

bool Engine::has(RecordKind kind, std::size_t index) const {
    const auto& list = script_.records(kind);
    return index < list.size() && truthy(game_->variable(Script::recordVariable(kind, list[index].name)));
}

int Engine::count(RecordKind kind) const {
    int n = 0;
    for (std::size_t i = 0; i < script_.records(kind).size(); ++i) n += has(kind, i) ? 1 : 0;
    return n;
}

// A tip is remembered as 1 when it's learned and 2 once it has been read.
void Engine::viewTip(std::size_t index) {
    if (!has(RecordKind::Tip, index)) return;
    tip_ = index;
    game_->setVariable(Script::recordVariable(RecordKind::Tip, script_.records(RecordKind::Tip)[index].name), 2);
}

bool Engine::newTip(std::size_t index) const {
    const auto& tips = script_.records(RecordKind::Tip);
    return index < tips.size() && toInt(game_->variable(Script::recordVariable(RecordKind::Tip, tips[index].name))) == 1;
}

void Engine::playSong(std::size_t index) {
    if (sampling_) return;
    if (!has(RecordKind::Song, index)) return; // not heard yet
    if (!musicBeforeSongs_) musicBeforeSongs_ = sound_.music();
    const std::string path = assets_->sound(script_.records(RecordKind::Song)[index].name);
    if (sound_.music() == path) sound_.stopMusic(0.5f); // the one playing: a click stops it
    else sound_.playMusic(path, true, 0.5f);
}

bool Engine::songsOpen() const {
    for (const auto& name : overlays_)
        if (const ScreenDef* def = script_.screen(name))
            if (std::ranges::any_of(def->elements, [](const Element& el) { return el.kind == ElementKind::Container && el.option("list") == "songs"; })) return true;
    return false;
}

} // namespace vn
