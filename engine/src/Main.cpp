// Where the player starts: SDL's main callbacks (SDL runs the loop and hands the player its events and frames).
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include "Engine.hpp"

#include <exception>

namespace {

// The game had to stop: why goes in the log, and the player is told (and where the log is).
void sayStopped(const vn::Engine* engine, const char* why) noexcept {
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Stopped: %s", why);
    const char* log = engine && !engine->logFile().empty() ? engine->logFile().c_str() : nullptr;
    char text[1024];
    SDL_snprintf(text, sizeof text, "Something went wrong, so the game stopped: %s.%s%s", why, log ? "\n\nThe log says more: " : "", log ? log : "");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "The game stopped", text, engine ? engine->window() : nullptr);
}

// SDL's own code (C) calls the four below, so no exception may go back into it: one that gets this far is
// said (sayStopped), and the player stops as it does on any failure (SDL_AppQuit still runs).
template <class F>
SDL_AppResult guarded(void* const* appstate, F&& step) noexcept {
    const auto engine = [&] { return appstate && *appstate ? static_cast<const vn::Engine*>(*appstate) : nullptr; };
    try {
        return std::forward<F>(step)();
    } catch (const std::exception& e) {
        sayStopped(engine(), e.what());
    } catch (...) {
        sayStopped(engine(), "an error that says nothing more");
    }
    return SDL_APP_FAILURE;
}

} // namespace

// SDL runs the player through these: the engine is made here and handed to SDL (as appstate) until
// SDL_AppQuit takes it back, which SDL does even when SDL_AppInit fails.
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    return guarded(appstate, [&] {
        auto engine = std::make_unique<vn::Engine>();
        vn::Engine& e = *engine;
        *appstate = engine.release();
        return e.init(argc, argv);
    });
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    return guarded(&appstate, [&] { return static_cast<vn::Engine*>(appstate)->event(*event); });
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    return guarded(&appstate, [&] { return static_cast<vn::Engine*>(appstate)->iterate(); });
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/) {
    const std::unique_ptr<vn::Engine> engine(static_cast<vn::Engine*>(appstate)); // (taken back from SDL: gone at the end of this)
}
