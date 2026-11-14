// The player (Engine): starting (the window, the game found and read, the player's data), the title screen, a new
// game and the story's end, packing the game, and reading the scripts again.
#include "Engine.hpp"
#include "Sample.hpp"

#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <mutex>

#if defined(SDL_PLATFORM_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <climits>
#include <dlfcn.h>
#endif

namespace vn {

namespace {

double megabytes(std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

#if defined(SDL_PLATFORM_MACOS)
// An app opened straight from a download runs from a hidden copy of just the .app ("App
// Translocation"), away from the game folder next to it. This finds where the app really is.
// The functions are looked up when needed, so if macOS ever drops them nothing breaks.
std::string originalAppPath(const std::string& app) {
    using IsTranslocated = Boolean (*)(CFURLRef, bool*, CFErrorRef*);
    using OriginalPath = CFURLRef (*)(CFURLRef, CFErrorRef*);
    static void* security = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
    const auto isTranslocated = security ? reinterpret_cast<IsTranslocated>(dlsym(security, "SecTranslocateIsTranslocatedURL")) : nullptr;
    const auto originalPath = security ? reinterpret_cast<OriginalPath>(dlsym(security, "SecTranslocateCreateOriginalPathForURL")) : nullptr;
    if (!isTranslocated || !originalPath) return app;
    std::string out = app;
    using Url = Owned<CFURLRef, CFRelease>;
    if (const Url url{CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(app.data()), static_cast<CFIndex>(app.size()), true)}) {
        bool translocated = false;
        if (isTranslocated(url.get(), &translocated, nullptr) && translocated)
            if (const Url original{originalPath(url.get(), nullptr)}) {
                char path[PATH_MAX];
                if (CFURLGetFileSystemRepresentation(original.get(), true, reinterpret_cast<UInt8*>(path), sizeof path)) out = path;
            }
    }
    return out;
}
#endif

std::vector<std::string> systemFonts() {
#if defined(SDL_PLATFORM_WINDOWS)
    const char* windir = SDL_getenv("WINDIR");
    const std::string fonts = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
    return {fonts + "segoeui.ttf", fonts + "arial.ttf", fonts + "tahoma.ttf"};
#elif defined(SDL_PLATFORM_MACOS)
    return {"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf",
            "/System/Library/Fonts/Helvetica.ttc"};
#elif defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_BSD)
    return {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/local/share/fonts/dejavu/DejaVuSans.ttf"};
#else
    return {};
#endif
}

// Fonts with Japanese, Chinese and Korean letters, used for letters the game's fonts don't have.
std::vector<std::string> systemFallbackFonts() {
    std::vector<std::string> candidates;
#if defined(SDL_PLATFORM_WINDOWS)
    const char* windir = SDL_getenv("WINDIR");
    const std::string fonts = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
    candidates = {fonts + "YuGothM.ttc", fonts + "msgothic.ttc", fonts + "msyh.ttc", fonts + "malgun.ttf",
                  fonts + "arial.ttf", fonts + "Nirmala.ttf", fonts + "LeelawUI.ttf"}; // Arabic and Hebrew; Indian scripts; Thai
#elif defined(SDL_PLATFORM_MACOS)
    candidates = {"/System/Library/Fonts/\u30d2\u30e9\u30ae\u30ce\u89d2\u30b4\u30b7\u30c3\u30af W4.ttc",
                  "/System/Library/Fonts/Hiragino Sans GB.ttc", "/System/Library/Fonts/AppleSDGothicNeo.ttc",
                  "/System/Library/Fonts/SFArabic.ttf", "/System/Library/Fonts/GeezaPro.ttc", "/System/Library/Fonts/SFHebrew.ttf",
                  "/System/Library/Fonts/ArialHB.ttc", "/System/Library/Fonts/Kohinoor.ttc", "/System/Library/Fonts/ThonburiUI.ttc"};
#elif defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_BSD)
    candidates = {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                  "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"};
    for (const char* script : {"Arabic", "Hebrew", "Devanagari", "Thai"})
        for (const char* folder : {"/usr/share/fonts/truetype/noto/", "/usr/share/fonts/noto/", "/usr/share/fonts/google-noto/"})
            candidates.push_back(std::format("{}NotoSans{}-Regular.ttf", folder, script));
#endif
    std::erase_if(candidates, [](const std::string& path) { return !SDL_GetPathInfo(path.c_str(), nullptr); });
    return candidates;
}

} // namespace

std::string sdlVersion() {
    const int v = SDL_GetVersion();
    return std::format("{}.{}.{}", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
}

Engine::Session::~Session() {
    if (ttf) TTF_Quit();
    SDL_Quit();
}

namespace {

constexpr std::size_t kLogKept = 500; // messages kept for the log file before it's open

void writeLogLine(SDL_IOStream* io, SDL_LogPriority priority, std::string_view text) {
    static constexpr const char* names[] = {"", "TRACE", "VERBOSE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};
    const auto p = static_cast<std::size_t>(priority);
    SDL_IOprintf(io, "%8.3f %-5s %.*s\n", static_cast<double>(SDL_GetTicks()) / 1000.0, p < std::size(names) ? names[p] : "",
                 static_cast<int>(text.size()), text.data());
}

} // namespace

void Engine::Log::start() {
    started = true;
    SDL_GetLogOutputFunction(&previous, &previousData);
    SDL_SetLogOutputFunction(
        [](void* userdata, int category, SDL_LogPriority priority, const char* message) {
            auto* self = static_cast<Log*>(userdata);
            if (self->previous) self->previous(self->previousData, category, priority, message);
            withoutThrowing([&] { // (SDL calls this, from any thread)
                const std::scoped_lock _(self->mutex);
                if (self->io) {
                    writeLogLine(self->io.get(), priority, message);
                    SDL_FlushIO(self->io.get());
                } else {
                    self->early.push_back({priority, message});
                    if (self->early.size() > kLogKept) self->early.pop_front();
                }
            });
        },
        this);
}

void Engine::Log::open(const std::string& path) {
    const std::scoped_lock _(mutex);
    io.reset(SDL_IOFromFile(path.c_str(), "w"));
    if (!io) return;
    SDL_IOprintf(io.get(), "VNext %s on %s, with SDL %s\n", kVersion, SDL_GetPlatform(), sdlVersion().c_str()); // (on screen, About says)
    for (const Entry& e : early) writeLogLine(io.get(), e.priority, e.text);
    early.clear();
    SDL_FlushIO(io.get());
}

Engine::Log::~Log() {
    if (started) SDL_SetLogOutputFunction(previous, previousData);
}

SDL_AppResult Engine::init(int argc, char** argv) {
    log_.start();
    std::string gameArg, dataArg, translateId, sampleArg;
    std::optional<std::string> packArg;
    bool listVoices = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--game" && hasValue) gameArg = argv[++i];
        else if (arg == "--data" && hasValue) dataArg = argv[++i];
        else if (arg == "--scene" && hasValue) startScene_ = argv[++i];
        else if (arg == "--screenshot" && hasValue) screenshotPath_ = argv[++i];
        else if (arg == "--voices") listVoices = true;
        else if (arg == "--translate" && hasValue) translateId = lower(argv[++i]);
        else if (arg == "--sample" && hasValue) sampleArg = argv[++i];
        else if (arg == "--pack") packArg = hasValue && !std::string_view(argv[i + 1]).starts_with('-') ? argv[++i] : packArg.value_or("");
        else if (arg == "--help" || arg == "-h") {
            SDL_Log("usage: VNext [--game FOLDER or PACK] [--data FOLDER] [--scene NAME] [--screenshot FILE.png] [--voices] "
                    "[--translate LANGUAGE] [--pack [FILE]] [--sample FOLDER]");
            return SDL_APP_SUCCESS;
        }
    }
    SDL_SetAppMetadata("VNext", kVersion, "dev.vnext.engine");
    if (!dataArg.empty()) {
        SDL_CreateDirectory(dataArg.c_str());
        prefPath_ = withSlash(dataArg);
        dataFolder_ = true;
    }
    developer_.startToolsLanguage(); // (before the scripts are read: what's said about their mistakes is in it)
    // --sample FOLDER: a game folder with the samples built into VNext written there (as New game makes it), then quit.
    if (!sampleArg.empty()) {
        const auto written = writeSample(withSlash(sampleArg));
        const std::string said = written ? "Made a game folder with the samples in '{}'."_tr(sampleArg) : written.error();
        SDL_Log("%s", said.c_str());
        return written ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    // The script comes first: it decides the save folder, the game size and default settings.
    gameRoot_ = findGame(gameArg);
    const std::string problem = openGame(); // with the game itself
    const GameSettings& game = script_.gameSettings;
    // Tools for making the game, which do their work and quit.
    if (listVoices || !translateId.empty() || packArg) {
        if (noGame_ || !problem.empty() || packed_) {
            const std::string why = noGame_            ? std::format("There's no game folder in {}.", gamePlaces().front())
                                    : !problem.empty() ? problem
                                                       : "This works on a game folder, not on a pack.";
            SDL_Log("%s", why.c_str());
            return SDL_APP_FAILURE;
        }
        if (listVoices) return writeVoiceList() ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (!translateId.empty()) return writeTranslation(translateId) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (const std::string target = packArg->empty() ? finishedFolder() : *packArg; SDL_GetPathInfo(target.c_str(), nullptr)) {
            SDL_Log("%s", std::format("There's a packed game in '{}' already: move it away first, then pack again.", target).c_str());
            return SDL_APP_FAILURE;
        }
        const auto packed = packArg->empty() ? finishGame() : pack(*packArg);
        SDL_Log("%s", packed ? packed->c_str() : packed.error().c_str());
        return packed ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"); // touches are turned into clicks and gestures here
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        logError("Can't start SDL: {}.", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    loadPlayerData();
    logFile_ = prefPath_ + "log.txt";
    log_.open(logFile_);
    if (!noGame_) logInfo("Playing the game in '{}'{}.", gameRoot_, game.developer ? " (developer mode)" : "");
    logInfo("The player's data is in '{}'.", prefPath_);

    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) sound_.init();
    else logWarn("Can't start the sound: {}, so the game plays silently.", SDL_GetError());
    session_.ttf = TTF_Init();
    if (!session_.ttf) logWarn("Can't start SDL_ttf (it draws the text): {}.", SDL_GetError());

    // Window: the saved size, shrunk if it no longer fits the display.
    if (SDL_Rect usable; SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)) {
        settings_.windowWidth = std::clamp(settings_.windowWidth, 320, std::max(320, usable.w));
        settings_.windowHeight = std::clamp(settings_.windowHeight, 240, std::max(240, usable.h));
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (settings_.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(windowTitle().c_str(), settings_.windowWidth, settings_.windowHeight, flags, &window, &renderer)) {
        logError("Can't open the window: {}.", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    window_.reset(window);
    renderer_.reset(renderer);
    SDL_SetWindowMinimumSize(window, 320, 240);
    toolsFonts_.configure({}, systemFonts(), systemFallbackFonts());
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    assets_ = std::make_unique<Assets>(renderer, gameRoot_, files_);
    slots_.attach(saves_, renderer);
    applied_ = settings_; // the window already matches; apply the rest
    game_ = makeGame();
    configure();
    begin();
    lastTicks_ = SDL_GetTicksNS();
    return SDL_APP_CONTINUE;
}

// Reads the game gameRoot_ points to (a folder, or a pack): its scripts and translations. No game:
// the player opens in developer mode. Gives what's wrong with the game itself, if anything.
std::string Engine::openGame() {
    noGame_ = gameRoot_.empty();
    packed_ = cantOpen_ = false;
    script_ = Script{};
    translations_.clear();
    if (noGame_) return {};
    std::string problem;
    if (!gameRoot_.ends_with('/') && !gameRoot_.ends_with('\\')) { // a pack
        if (const auto opened = files_.openPack(gameRoot_)) packed_ = true;
        else problem = opened.error();
        gameRoot_ += '/';
    }
    if (problem.empty()) {
        script_ = readScript();
        loadTranslations();
    } else {
        script_.errors.push_back({"", 0, problem});
        cantOpen_ = true;
    }
    return problem;
}

// The player's own folder for the game (VNext/<its save_folder setting, else its title>, or --data): settings, the log, and saves and
// progress, unless the game is a pack that keeps those itself: game.vnext does, unless --data was given
// or it can't (its file can't be written to, or it's inside an app). Without a game: VNext/Developer.
void Engine::loadPlayerData() {
    if (!dataFolder_)
        if (const SdlOwned<char> pref{SDL_GetPrefPath("VNext", noGame_ ? "Developer" : saveFolderNameOf(gameSettings().saveFolder, gameSettings().title).c_str())})
            prefPath_ = pref.get();
    inPack_ = packed_ && !dataFolder_ && gameRoot_.find(".app/Contents/Resources/") == std::string::npos && files_.writable();
    settings_ = Settings::load(prefPath_ + "settings.txt", defaultSettings(gameSettings()));
    readLog_ = readLogOf(readProgress("read.txt").value_or(""));
    loadPersistent();
    flowchart_.read(readProgress("flowchart.txt").value_or(""));
    developer_.loadReached();
    developer_.loadStops();
}

// What the player shows first: scene start (a game folder's: or --scene's scene, to try one; a pack, a
// finished game, always begins at scene start). In developer mode, the developer tools, until Play;
// without a game, they're all there is.
void Engine::begin() {
    if (packed_) startScene_.clear();
    if (noGame_) developer_.openDeveloper();
    else if (gameSettings().developer && startScene_.empty()) developer_.backToEditor();
    else if (mode_ == Mode::Error) return;
    else if (!startScene_.empty()) newGame(startScene_);
    else play();
}

void Engine::play() {
    waiting_ = false;
    beginStory();
}

// The game begins at scene start. Its lines before the one that opens the title screen (an opening
// movie...) aren't saved, and New Game goes on from the line after it.
void Engine::beginStory() {
    beforeTitle_.clear(); // (its lines before the title screen run again)
    newGame("start");
    opening_ = titleLine().has_value();
}

void Engine::newStory() {
    if (!storyStart_)
        if (const auto line = titleLine()) storyStart_ = *line + 1;
    if (storyStart_) startAt(*storyStart_, &beforeTitle_);
    else newGame("start");
}

std::optional<std::size_t> Engine::titleLine() const {
    const auto start = script_.findScene("start");
    if (!start) return std::nullopt;
    std::size_t end = script_.code.size();
    for (const auto& [name, pc] : script_.scenes)
        if (pc > *start && pc < end) end = pc;
    for (std::size_t pc = *start; pc < end; ++pc)
        if (const auto* open = std::get_if<cmd::Ui>(&script_.code[pc].command); open && open->verb == "open" && open->arg == "title") return pc;
    return std::nullopt;
}

// Developer mode without a game: looks for one again (Look again), and plays it once it's there.
bool Engine::lookForGame() {
    gameRoot_ = findGame({});
    if (gameRoot_.empty()) return false;
    savePersistent();
    saveSettings();
    openGame();
    loadPlayerData();
    logInfo("Found the game in '{}'; the player's data is in '{}'.", gameRoot_, prefPath_);
    developer_.dropPreviews(); // (its game uses the old images)
    assets_ = std::make_unique<Assets>(renderer_.get(), gameRoot_, files_);
    sound_.clearCache();
    game_ = makeGame(); // (the old one points at the old images)
    configure();
    developer_.leave(); // (the game begins without them)
    begin();
    return true;
}

Engine::~Engine() {
    developer_.dropPreviews(); // (its game uses the images and the renderer)
    developer_.dropCanvas();
    developer_.stopMovie();
    if (prefPath_.empty()) return;
    try { // (a destructor mustn't throw: what can't be saved is logged)
        if (mode_ == Mode::Game && !opening_) autosave(); // quitting mid-game
        saveSettings();
        savePersistent();
        writeProgress("read.txt", readLogText(readLog_));
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Can't save everything on quitting: %s", e.what());
    }
}

// --voices: game/voices.txt lists every line that plays a voice file (its own voice=, or its speaker's voice_files=),
// with the file, as a script for recording them.
bool Engine::writeVoiceList() const {
    std::string out = "# The voice file each line plays (in game/sounds, ending .ogg, .mp3, .flac or .wav), and its text.\n";
    std::size_t lines = 0;
    for (const Instruction& ins : script_.code)
        if (const auto* say = std::get_if<cmd::Say>(&ins.command))
            if (const std::string voice = script_.voiceOf(*say); !voice.empty() && !voice.contains('{')) {
                const CharacterDef* def = say->character.empty() ? nullptr : script_.character(say->character);
                const std::string who = def ? def->name : say->speaker;
                out += std::format("{}\t{}{}\n", voice, who.empty() ? "" : who + ": ", say->text);
                ++lines;
            }
    if (lines == 0) out += "# No line plays a voice file yet: give one voice=NAME at its end, or a character voice_files=yes.\n";
    const std::string file = gameRoot_ + "voices.txt";
    const bool ok = SDL_SaveFile(file.c_str(), out.data(), out.size());
    SDL_Log("%s", (ok ? std::format("Wrote '{}' ({} lines).", file, lines) : std::format("Can't write '{}': {}.", file, SDL_GetError())).c_str());
    return ok;
}

// Fonts: the script's, with the language's own in their place.
void Engine::configureFonts() {
    auto fonts = script_.fonts;
    if (const Translation* t = script_.translation())
        for (const auto& [name, font] : t->fonts) fonts.insert_or_assign(name, font);
    std::map<std::string, FontCache::File, std::less<>> files;
    std::vector<std::string> fallbacks; // "font fallback ..." lines first, then the computer's own
    for (const auto& [name, font] : fonts) {
        files.emplace(name, FontCache::File{gameRoot_ + font.file, font.style});
        if (name.starts_with("fallback")) fallbacks.push_back(gameRoot_ + font.file);
    }
    if (!files.contains("default") && !noGame_) files.emplace("default", FontCache::File{gameRoot_ + "fonts/default.ttf", ""});
    for (auto& path : systemFallbackFonts()) fallbacks.push_back(std::move(path));
    fonts_.configure(std::move(files), systemFonts(), std::move(fallbacks));
}

// The folders a game may be in, first to last: next to the player (next to the .app on macOS, even
// when macOS runs a downloaded app from a hidden copy of it; next to the AppImage on Linux, which runs
// from a folder of its own), then inside the app.
std::vector<std::string> Engine::gamePlaces() const {
    const char* basePath = SDL_GetBasePath();
    const std::string base = basePath ? basePath : "";
    std::vector<std::string> places{base};
    constexpr std::string_view bundleTail = ".app/Contents/Resources/";
    if (base.ends_with(bundleTail)) {
        std::string app = base.substr(0, base.size() - bundleTail.size() + 4); // ".../Name.app"
#if defined(SDL_PLATFORM_MACOS)
        app = originalAppPath(app);
#endif
        places.insert(places.begin(), app.substr(0, app.find_last_of('/') + 1));
    }
#if defined(SDL_PLATFORM_LINUX)
    // (only when this player is the AppImage's own: a program another AppImage starts has its variables too)
    const char* appDir = SDL_getenv("APPDIR");
    const char* appImage = SDL_getenv("APPIMAGE");
    if (std::string dir = appDir ? appDir : ""; appImage && !dir.empty() && base.starts_with(dir.ends_with('/') ? dir : dir + "/")) {
        const std::string image = appImage;
        places.insert(places.begin(), image.substr(0, image.find_last_of('/') + 1));
    }
#endif
    return places;
}

// The game: --game (a folder or a pack); else in one of gamePlaces() the game folder, which is the
// game being made, or game.vnext, the finished game in one file. A folder ends with a slash;
// nothing found: empty.
std::string Engine::findGame(const std::string& requested) const {
    if (!requested.empty()) return isDirectory(requested) ? withSlash(requested) : requested;
    for (const auto& place : gamePlaces()) {
        if (isDirectory(place + "game")) return place + "game/";
        if (fileExists(place + std::string(kPackName))) return place + std::string(kPackName);
    }
    return {};
}

// Every game starts in scripts/main.vnscript; the built-in menus come first, and the game's own replace
// them. A pack is a finished game, so it never runs in developer mode.
Script Engine::readScript() const {
    Script script = Script::load("scripts/main.vnscript", [this](const std::string& p) { return files_.readText(gameRoot_ + p); }, defaultUi(),
                                 [this](const std::string& folder) { return files_.list(gameRoot_ + folder, ""); });
    if (packed_) script.gameSettings.developer = false;
    auto count = [](std::size_t n, std::string_view what) { return std::format("{} {}{}", n, what, n == 1 ? "" : "s"); };
    const auto files = static_cast<std::size_t>(std::ranges::count_if(script.files, [](const std::string& f) { return !f.starts_with('<'); }));
    logInfo("Read {}: {}, {} and {}.", count(files, "script file"), count(script.scenes.size(), "scene"), count(script.characters.size(), "character"),
            count(script.screens.size(), "screen"));
    return script;
}

// What packing says, a sentence or two a line (the developer tools' window shows the lines apart, the log on
// one line), in the developer tools' language (on the command line, English).

namespace {

std::string packedAlready() { return "Can't pack the game: it's a pack already."_tr() + "\n" + "Open its game folder to pack it again."_tr(); }
std::string scriptsWrong() { return "Can't pack the game: its scripts have mistakes."_tr() + "\n" + "The developer tools mark them in red."_tr(); }

} // namespace

// Packs the game into `file`, and writes the licences of what's built into the player next to it
// (licences.txt), to go out with them.
std::expected<std::string, std::string> Engine::pack(const std::string& file) const {
    if (packed_) return std::unexpected(packedAlready());
    if (!script_.ok()) return std::unexpected(scriptsWrong());
    const auto holds = packInto(file);
    if (!holds) return holds;
    return "Packed the game into '{}', with 'licences.txt' next to it."_tr(file) + "\n" + *holds;
}

// The pack written as `file`, with licences.txt next to it: what it holds and how long it took to make.
std::expected<std::string, std::string> Engine::packInto(const std::string& file) const {
    const Uint64 start = SDL_GetTicksNS();
    const auto report = writePack(gameRoot_, file);
    if (!report) return std::unexpected(report.error());
    const std::string notices = file.substr(0, file.find_last_of("/\\") + 1) + "licences.txt";
    if (!SDL_SaveFile(notices.c_str(), licences().data(), licences().size()))
        return std::unexpected("Packed the game, but can't write '{}': {}."_tr(notices, SDL_GetError()));
    const double size = megabytes(report->bytes), packed = megabytes(report->packed), took = static_cast<double>(SDL_GetTicksNS() - start) / 1e9;
    return report->files == 1 ? "The pack holds 1 file ({:.1f} MB) in {:.1f} MB, made in {:.1f} seconds."_tr(size, packed, took)
                              : "The pack holds {} files ({:.1f} MB) in {:.1f} MB, made in {:.1f} seconds."_tr(report->files, size, packed, took);
}

// --pack: next to the game folder, a folder named after the game with game.vnext (the game in
// one file, which keeps its players' saves and progress in it too) and licences.txt. To play it, the player goes
// next to game.vnext: VNext.app, VNext.exe or VNext-Linux.AppImage, from VNext's releases on GitHub. (One there already:
// the command says to move it away first.)
std::expected<std::string, std::string> Engine::finishGame() const {
    if (packed_) return std::unexpected(packedAlready());
    if (!script_.ok()) return std::unexpected(scriptsWrong());
    const std::string folder = finishedFolder();
    if (!SDL_CreateDirectory(folder.c_str())) return std::unexpected("Can't make '{}': {}."_tr(folder, SDL_GetError()));
    const auto holds = packInto(folder + "/" + std::string(kPackName));
    if (!holds) return holds;
    return "Packed the game: 'game.vnext' and 'licences.txt' are in '{}'."_tr(folder) + "\n" + *holds + "\n" +
           "To play it, put VNext next to 'game.vnext': VNext.app, VNext.exe or VNext-Linux.AppImage, from VNext's releases on GitHub."_tr();
}

std::string Engine::finishedFolder() const {
    std::string name = fileNameOf(script_.gameSettings.title); // (the game's title, as a folder's name)
    if (name.empty()) name = "Game";
    const std::string game = gameRoot_.substr(0, gameRoot_.size() - 1); // (next to the game folder)
    const auto slash = game.find_last_of("/\\");
    return (slash == std::string::npos ? std::string{} : game.substr(0, slash + 1)) + name;
}

namespace {

#include "icon_png.inc" // (kIconPng: the bytes of the player's icon.png)

} // namespace

// Applies what the script sets up: canvas, window title, save pages, fonts, screens. Script
// errors switch to the error screen.
void Engine::configure() {
    const GameSettings& game = script_.gameSettings;
    canvas_ = {static_cast<float>(game.width), static_cast<float>(game.height)};
    defaults_ = defaultSettings(game);
    useLanguage(); // texts, window title, fonts
    // The window's icon: the game's (setting icon), else the player's own (icon.png, built into it).
    SurfacePtr icon{noGame_ || game.icon.empty() ? nullptr : IMG_Load_IO(files_.open(gameRoot_ + game.icon).release(), true)}; // (a missing file is fine)
    if (!icon) icon.reset(IMG_Load_IO(SDL_IOFromConstMem(kIconPng, sizeof kIconPng), true));
    if (icon) SDL_SetWindowIcon(window_.get(), icon.get());
    SDL_SetWindowResizable(window_.get(), game.resizable); // (the developer tools too: one setting for both)
    // Saves go into game.vnext for a pack that keeps them (inPack_), in game/saves for a game folder
    // (unless --data was given, or it's inside an app, or the folder can't take them), else in the
    // player's own folder.
    std::string saves = prefPath_ + "saves/";
    if (inPack_ || (!dataFolder_ && !packed_ && !noGame_ && gameRoot_.find(".app/Contents/Resources/") == std::string::npos &&
                    SDL_CreateDirectory((gameRoot_ + "saves").c_str())))
        saves = gameRoot_ + "saves/";
    saves_.setFolder(saves, inPack_ ? &files_ : nullptr);
    assets_->setPixelArt(game.pixelArt);
    assets_->setAtlases(script_.atlases);
    assets_->setSubImages(script_.subImages);
    // The mouse pointer (setting mouse_pointer): a picture or a sub-image (a visual named by a visual line or a character:
    // what it shows; a file by its path: images/arrow.png), or the system's.
    cursor_.reset();
    if (!game.mousePointer.empty()) {
        const std::string shows = script_.visualOf(game.mousePointer);
        if (SurfacePtr image = shows.contains('.') ? SurfacePtr{IMG_Load_IO(files_.open(gameRoot_ + shows).release(), true)} : assets_->pixels(shows))
            cursor_.reset(SDL_CreateColorCursor(image.get(), game.pointerTip.x, game.pointerTip.y));
        if (!cursor_) logWarn("Can't use '{}' as the mouse pointer: {}.", game.mousePointer, SDL_GetError());
    }
    SDL_SetCursor(cursor_ ? cursor_.get() : SDL_GetDefaultCursor());
    sound_.setCrossfade(game.musicCrossfade);
    sound_.setDucking(game.musicUnderVoices);
    assets_->setMemoryLimit(static_cast<std::size_t>(game.imageMemory) * 1024 * 1024);
    saves_.configure(game.savePages, game.pageSlots);
    countSaves();
    if (inPack_) logInfo("Saves and progress go in '{}'.", gameRoot_.substr(0, gameRoot_.size() - 1));
    else if (!noGame_) logInfo("Saves go in '{}'.", saves);
    applySettings();

    overlays_.clear();
    shown_.clear();
    calledDepth_ = 0;
    views_.clear();
    for (const auto& [name, def] : script_.screens) views_.try_emplace(name, def, *this);
    rebinding_.reset();
    refreshKeys();

    if (!script_.ok()) {
        std::vector<std::string> lines;
        for (const auto& e : script_.errors) {
            const std::string_view file = std::string_view(e.file).starts_with("scripts/") ? std::string_view(e.file).substr(8) : e.file;
            lines.push_back(e.line > 0     ? std::format("{} line {}: {}", file, e.line, e.message)
                            : file.empty() ? e.message
                                           : std::format("{}: {}", file, e.message));
            logError("{}", lines.back());
        }
        showErrors(lines);
    }
}

std::unique_ptr<Game> Engine::makeGame() {
    auto game = std::make_unique<Game>(Context{renderer_.get(), *assets_, sound_, settings_, readLog_, persistent_, flowchart_, canvas_,
                                               [this](std::string_view name) { return builtin(name); }},
                                       script_);
    game->stopWhere([this](std::size_t pc) { return developerMode() && developer_.stopsBefore(pc); }); // (Stop here, Next line)
    return game;
}

void Engine::leaveStory(bool keepSound) {
    if (mode_ == Mode::Game && !opening_) autosave(); // leaving a game in progress
    opening_ = false;
    if (!keepSound) sound_.stopAll(1.0f);
    game_ = makeGame(); // idle: title buttons can still set variables and play music
    writeProgress("read.txt", readLogText(readLog_));
    closeAll();
    syncShownScreens();
    mode_ = Mode::Title;
}

void Engine::openTitle(bool keepSound) {
    leaveStory(keepSound);
    for (const auto& [name, value] : beforeTitle_) game_->setVariable(name, value); // (its buttons see what was picked before it)
    countSaves();
    if (ScreenView* title = view("title")) {
        title->reset();
        titleShown_ = true;
        screenHook(title->def(), true);
    } else if (storyStarted_) {
        quit(); // the story is over, and there's no title screen to go back to
    } else if (script_.findScene("start")) {
        newGame("start"); // a game without a title screen starts at once
    }
}

// In developer mode the player doesn't close by itself (Quit, or the story's end without a title screen):
// it goes back to the developer tools.
void Engine::quit() {
    if (gameSettings().developer) developer_.backToEditor();
    else quit_ = true;
}

void Engine::newGame(std::string_view label) {
    const std::string name(label.empty() ? "start" : label);
    const auto pc = script_.findScene(name);
    if (!pc && name != "start") return logWarn("There's no scene called '{}' to start from.", name);
    // (without "scene start" the story starts at its first line; from the title screen, with what was picked before it)
    startAt(pc.value_or(0), mode_ == Mode::Title && name != "start" ? &beforeTitle_ : nullptr);
}

// The title screen goes when a game starts or is loaded: its action_on_close runs (stop music...).
void Engine::leaveTitle() {
    if (!std::exchange(titleShown_, false)) return;
    if (ScreenView* title = view("title")) screenHook(title->def(), false);
}

// Its hide animation first (its own, or the one the story opened it to close with).
void Engine::fromTitle(std::function<void()> then) {
    if (mode_ != Mode::Title) return then();
    if (ScreenView* title = view("title"); title && title->hideAnimation() && !title->hasLeft()) {
        title->leave();
        leaving_.push_back({"title", [this, then = std::move(then)]() mutable { fromTitle(std::move(then)); }});
        return;
    }
    then();
}

void Engine::startAt(std::size_t pc, const Variables* carried) {
    leaveTitle();
    closeAll();
    game_ = makeGame();
    if (carried) game_->carry(*carried);
    mode_ = Mode::Game;
    opening_ = false;
    storyStarted_ = true;
    game_->start(pc);
}

Image Engine::image(std::string_view name, Frame frame) {
    return assets_->image(name, frame);
}

bool Engine::playing() const {
    return mode_ == Mode::Game;
}

// Reload the game (the developer tools'), or a reload action: reads the scripts, images and sounds again and continues where the player was.
void Engine::reload() {
    std::optional<std::string> snapshot;
    if (mode_ == Mode::Game && game_->canSave()) snapshot = game_->save();
    mode_ = Mode::Title;
    assets_->clear();
    sound_.clearCache();
    views_.clear(); // (the screens and the story point into the script: let go of before it's replaced)
    game_.reset();
    script_ = readScript();
    storyStart_.reset();
    loadTranslations();
    game_ = makeGame(); // (the old one points into the old script)
    configure();
    if (mode_ == Mode::Error || waiting_) return; // (waiting: the game starts with Play)
    if (!snapshot || !resume(*snapshot)) {
        storyStarted_ = false; // (without a title screen, the story starts again)
        openTitle();
    }
}

} // namespace vn
