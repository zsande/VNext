// SDL3 main callbacks: SDL drives the loop, which keeps the same code working on desktop,
// mobile and the web (Emscripten), where a blocking while-loop is not allowed.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include "Engine.hpp"

#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <ranges>

#if defined(SDL_PLATFORM_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <climits>
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
#elif defined(SDL_PLATFORM_ANDROID)
    return {"/system/fonts/Roboto-Regular.ttf", "/system/fonts/DroidSans.ttf"};
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

bool isPress(const SDL_Event& e) {
    return e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat);
}

// The player settings a game starts with ("config default.*" lines).
Settings defaultSettings(const GameConfig& cfg) {
    Settings s;
    s.windowWidth = cfg.width;
    s.windowHeight = cfg.height;
    for (const auto& [key, value] : cfg.settingDefaults) s.set(key, value);
    return s;
}

std::string sdlVersion() {
    const int v = SDL_GetVersion();
    return std::format("{}.{}.{}", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
}

} // namespace

Engine::Session::~Session() {
    if (ttf) TTF_Quit();
    SDL_Quit();
}

namespace {
constexpr std::size_t kLogKept = 500; // messages the developer tools can show

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
                self->recent.push_back({priority, message});
                if (self->recent.size() > kLogKept) self->recent.pop_front();
                if (self->io) {
                    writeLogLine(self->io.get(), priority, message);
                    SDL_FlushIO(self->io.get());
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
    for (const Entry& e : recent) writeLogLine(io.get(), e.priority, e.text);
    SDL_FlushIO(io.get());
}

std::vector<Engine::Log::Entry> Engine::Log::newest(std::size_t count, std::size_t skip) {
    const std::scoped_lock _(mutex);
    const std::size_t end = recent.size() - std::min(skip, recent.size());
    const std::size_t begin = end - std::min(count, end);
    return {recent.begin() + static_cast<std::ptrdiff_t>(begin), recent.begin() + static_cast<std::ptrdiff_t>(end)};
}

std::size_t Engine::Log::size() {
    const std::scoped_lock _(mutex);
    return recent.size();
}

Engine::Log::~Log() {
    if (started) SDL_SetLogOutputFunction(previous, previousData);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

SDL_AppResult Engine::init(int argc, char** argv) {
    log_.start();
    std::string gameArg, dataArg, translateId;
    std::optional<std::string> packArg;
    bool listVoices = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--game" && hasValue) gameArg = argv[++i];
        else if (arg == "--data" && hasValue) dataArg = argv[++i];
        else if (arg == "--label" && hasValue) startLabel_ = argv[++i];
        else if (arg == "--screenshot" && hasValue) screenshotPath_ = argv[++i];
        else if (arg == "--voices") listVoices = true;
        else if (arg == "--translate" && hasValue) translateId = lower(argv[++i]);
        else if (arg == "--pack") packArg = hasValue && !std::string_view(argv[i + 1]).starts_with('-') ? argv[++i] : packArg.value_or("");
        else if (arg == "--help" || arg == "-h") {
            SDL_Log("usage: VNext [--game FOLDER or PACK] [--data FOLDER] [--label NAME] [--screenshot FILE.png] [--voices] "
                    "[--translate LANGUAGE] [--pack [FILE]]");
            return SDL_APP_SUCCESS;
        }
    }
    SDL_SetAppMetadata("VNext", kVersion, "dev.vnext.engine");

    // The script comes first: it decides the save folder, resolution and default settings.
    gameRoot_ = findGame(gameArg);
    const std::string problem = openGame(); // with the game itself
    const GameConfig& cfg = script_.config;
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
        const auto packed = packArg->empty() ? finishGame() : pack(*packArg);
        SDL_Log("%s", packed ? packed->c_str() : packed.error().c_str());
        return packed ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"); // touches are turned into clicks and gestures here
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        logError("Can't start SDL: {}.", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!dataArg.empty()) {
        SDL_CreateDirectory(dataArg.c_str());
        prefPath_ = withSlash(dataArg);
        dataFolder_ = true;
    }
    loadPlayerData();
    logFile_ = prefPath_ + "log.txt";
    log_.open(logFile_);
    if (!noGame_) logInfo("Playing the game in '{}'{}.", gameRoot_, cfg.developer ? " (developer mode)" : "");
    logInfo("The player's data is in '{}'.", prefPath_);

    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) audio_.init();
    else logWarn("Can't start the audio: {}, so the game plays without sound.", SDL_GetError());
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
    packed_ = false;
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
    }
    return problem;
}

// The player's own folder for the game (VNext/<save_id>, or --data): settings, things remembered
// and the log. Without a game: VNext/Developer.
void Engine::loadPlayerData() {
    if (!dataFolder_)
        if (const SdlOwned<char> pref{SDL_GetPrefPath("VNext", noGame_ ? "Developer" : config().saveId.c_str())}) prefPath_ = pref.get();
    settings_ = Settings::load(prefPath_ + "settings.txt", defaultSettings(config()));
    readLog_ = loadReadLog(prefPath_ + "read.txt");
    loadPersistent();
    chart_.load(prefPath_ + "chart.txt");
    developer_.loadReached();
}

// What the player shows first: the splash screen, then label start (or --label's label). In developer
// mode, the developer tools, until Play; without a game, they're all there is.
void Engine::begin() {
    if (noGame_) developer_.openDeveloper();
    else if (config().developer && startLabel_.empty()) developer_.backToEditor();
    else if (mode_ == Mode::Error) return;
    else if (!startLabel_.empty()) newGame(startLabel_);
    else play();
}

void Engine::play() {
    waiting_ = false;
    beforeTitle_.clear();
    if (script_.findLabel("splashscreen")) {
        newGame("splashscreen", true); // runs first; its "return" begins the story
        splash_ = true;
    } else {
        beginStory();
    }
}

// The game begins at label start. Its lines before the one that opens the title screen (an opening
// movie...) aren't saved, and New Game goes on from the line after it.
void Engine::beginStory() {
    beforeTitle_.clear(); // (its lines before the title screen run again)
    newGame("start");
    opening_ = titleLine().has_value();
}

void Engine::newStory() {
    if (!storyStart_)
        if (const auto line = titleLine()) storyStart_ = *line + 1;
    if (storyStart_) startAt(*storyStart_, false, &beforeTitle_);
    else newGame("start");
}

std::optional<std::size_t> Engine::titleLine() const {
    const auto start = script_.findLabel("start");
    if (!start) return std::nullopt;
    std::size_t end = script_.code.size();
    for (const auto& [name, pc] : script_.labels)
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
    developer_.dropPreviews(); // (its game uses the old pictures)
    assets_ = std::make_unique<Assets>(renderer_.get(), gameRoot_, files_);
    audio_.clearCache();
    game_ = makeGame(); // (the old one points at the old pictures)
    configure();
    developer_.leave(); // (the game begins without them)
    begin();
    return true;
}

Engine::~Engine() {
    developer_.dropPreviews(); // (its game uses the pictures and the renderer)
    developer_.dropCanvas();
    developer_.stopMovie();
    if (prefPath_.empty()) return;
    try { // (a destructor mustn't throw: what can't be saved is logged)
        if (mode_ == Mode::Game && !splash_ && !opening_) autosave(); // quitting mid-game
        saveSettings();
        savePersistent();
        saveReadLog(readLog_, prefPath_ + "read.txt");
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Can't save everything on quitting: %s", e.what());
    }
}

// --voices: game/voices.txt lists every line of the characters with voices=, with the file it
// plays, as a script for recording them.
bool Engine::writeVoiceList() const {
    std::string out = "# The voice file each line plays (in game/audio, ending .ogg, .mp3, .flac or .wav), and its text.\n";
    std::size_t lines = 0;
    for (const Instruction& ins : script_.code)
        if (const auto* say = std::get_if<cmd::Say>(&ins.command))
            if (const CharacterDef* def = script_.character(say->character); def && !def->voices.empty()) {
                out += std::format("{}/{}\t{}: {}\n", def->voices, say->voiceId, def->name, say->text);
                ++lines;
            }
    if (lines == 0) out += "# No character has voices= yet, like  character Aiko voices=yes\n";
    const std::string file = gameRoot_ + "voices.txt";
    const bool ok = SDL_SaveFile(file.c_str(), out.data(), out.size());
    SDL_Log("%s", (ok ? std::format("Wrote '{}' ({} lines).", file, lines) : std::format("Can't write '{}': {}.", file, SDL_GetError())).c_str());
    return ok;
}

// --translate ID: writes game/translations/<id>.vnscript with every text the game shows, ready to be
// translated; if it's there already, adds the texts it doesn't have yet and takes out the ones the
// game doesn't show any more.
bool Engine::writeTranslation(const std::string& id) {
    if (id.empty() || !std::ranges::all_of(id, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || static_cast<unsigned char>(c) >= 0x80; })) {
        SDL_Log("--translate needs the language's name after it, one word: --translate spanish.");
        return false;
    }
    if (!script_.ok()) {
        for (const auto& e : script_.errors) SDL_Log("%s line %d: %s", e.file.c_str(), e.line, e.message.c_str());
        SDL_Log("Fix the scripts first, so every text can be found.");
        return false;
    }
    const std::string folder = gameRoot_ + "translations/";
    SDL_CreateDirectory(folder.c_str());
    const std::string file = folder + id + ".vnscript";
    const auto existing = readTextFile(file);
    const TranslationFile out = translationFile(script_, id, existing);
    if (!existing || out.added > 0 || !out.removed.empty()) {
        if (!SDL_SaveFile(file.c_str(), out.text.data(), out.text.size())) {
            SDL_Log("Can't write '%s': %s.", file.c_str(), SDL_GetError());
            return false;
        }
        if (!existing) SDL_Log("%s", std::format("Wrote '{}', with {} texts to translate.", file, out.added).c_str());
        else if (out.added > 0) SDL_Log("%s", std::format("Added {} new texts to '{}'.", out.added, file).c_str());
    } else {
        SDL_Log("'%s' already has every text the game shows.", file.c_str());
    }
    if (!out.removed.empty()) { // (changed or taken out of the scripts since the file was made)
        SDL_Log("%s", std::format("Took out {} texts from '{}' that the game doesn't show any more:", out.removed.size(), file).c_str());
        for (std::size_t i = 0; i < std::min<std::size_t>(out.removed.size(), 20); ++i) SDL_Log("    %s", out.removed[i].c_str());
        if (out.removed.size() > 20) SDL_Log("    ...");
    }
    return true;
}

void Engine::loadTranslations() {
    translations_.clear();
    const std::string folder = gameRoot_ + "translations/";
    for (const auto& file : files_.list(folder, ".vnscript"))
        if (const auto text = files_.readText(folder + file))
            translations_.push_back(Translation::parse(lower(file.substr(0, file.rfind('.'))), *text, script_.errors));
}

// The player's choice, else the computer's language if there's a translation for it.
const Translation* Engine::language() const {
    const std::string& want = settings_.language;
    if (want == "original") return nullptr;
    if (!want.empty()) {
        const auto it = std::ranges::find(translations_, want, &Translation::id);
        return it != translations_.end() ? &*it : nullptr;
    }
    const Translation* found = nullptr;
    int count = 0;
    if (const SdlOwned<SDL_Locale*> locales{SDL_GetPreferredLocales(&count)}; locales && count > 0 && locales.get()[0]->language) {
        const SDL_Locale& first = *locales.get()[0]; // (the first one only: the next may be the game's own)
        const std::string lang = lower(first.language);
        const std::string full = first.country ? std::format("{}-{}", lang, lower(first.country)) : lang;
        for (const Translation& t : translations_) {
            std::string code = t.code;
            std::ranges::replace(code, '_', '-');
            if (!code.empty() && (code == full || (code == lang && !found))) found = &t;
        }
    }
    return found;
}

void Engine::useLanguage() {
    const Translation* t = language();
    script_.translate(t);
    if (window_) SDL_SetWindowTitle(window_.get(), windowTitle().c_str());
    assets_->setLanguage(t ? t->id : std::string{});
    configureFonts();
}

std::vector<std::string> Engine::languageNames() const {
    std::vector<std::string> names{config().language};
    for (const Translation& t : translations_) names.push_back(t.name);
    return names;
}

int Engine::languageIndex() const {
    const Translation* t = script_.translation();
    return t ? static_cast<int>(t - translations_.data()) + 1 : 0;
}

std::string Engine::languageId(int index) const {
    return index <= 0 || static_cast<std::size_t>(index) > translations_.size() ? "original" : translations_[static_cast<std::size_t>(index) - 1].id;
}

std::optional<std::string> Engine::languageIdNamed(std::string_view word) const {
    const std::string want = lower(trim(word));
    if (want.empty()) return std::nullopt;
    if (want == "original" || want == lower(config().language)) return "original";
    for (const Translation& t : translations_)
        if (want == t.id || want == lower(t.name) || (!t.code.empty() && want == lower(t.code))) return t.id;
    return std::nullopt;
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
// when macOS runs a downloaded app from a hidden copy of it), then inside the app.
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
    Script script = Script::load("scripts/main.vnscript", [this](const std::string& p) { return files_.readText(gameRoot_ + p); }, defaultUi());
    if (packed_) script.config.developer = false;
    auto count = [](std::size_t n, std::string_view what) { return std::format("{} {}{}", n, what, n == 1 ? "" : "s"); };
    const auto files = static_cast<std::size_t>(std::ranges::count_if(script.files, [](const std::string& f) { return !f.starts_with('<'); }));
    logInfo("Read {}: {}, {} and {}.", count(files, "script file"), count(script.labels.size(), "label"), count(script.characters.size(), "character"),
            count(script.screens.size(), "screen"));
    return script;
}

// Where the developer tools' Pack the game puts the pack: next to the game folder.
std::string Engine::packPath() const {
    const std::string folder = gameRoot_.substr(0, gameRoot_.size() - 1);
    const auto slash = folder.find_last_of("/\\");
    return (slash == std::string::npos ? std::string{} : folder.substr(0, slash + 1)) + std::string(kPackName);
}

// Packs the game into `file`, and writes the licenses of what's built into the player next to it
// (licenses.txt), to go out with them.
std::expected<std::string, std::string> Engine::pack(const std::string& file) const {
    if (packed_) return std::unexpected("This game is a pack already: open its game folder to pack it again.");
    if (!script_.ok()) return std::unexpected("Fix the scripts first: a game with mistakes in it can't be packed.");
    const Uint64 start = SDL_GetTicksNS();
    const auto report = writePack(gameRoot_, file);
    if (!report) return std::unexpected(report.error());
    const std::string notices = file.substr(0, file.find_last_of("/\\") + 1) + "licenses.txt";
    if (!SDL_SaveFile(notices.c_str(), licenses().data(), licenses().size()))
        return std::unexpected(std::format("Packed the game, but can't write '{}': {}.", notices, SDL_GetError()));
    return std::format("Packed {} files ({:.1f} MB) into {} ({:.1f} MB) in {:.1f} seconds, with licenses.txt next to it.", report->files,
                       megabytes(report->bytes), file, megabytes(report->packed), static_cast<double>(SDL_GetTicksNS() - start) / 1e9);
}

namespace {

// A picture as a Mac app's icon (.icns): square, in the sizes macOS shows, each as a PNG.
bool writeIcns(SDL_Surface* picture, const std::string& file) {
    static constexpr std::pair<int, std::string_view> kSizes[]{{16, "icp4"}, {32, "icp5"}, {64, "icp6"}, {128, "ic07"},
                                                              {256, "ic08"}, {512, "ic09"}, {1024, "ic10"}};
    std::string out;
    auto put32 = [](std::string& b, std::uint32_t v) {
        for (int shift = 24; shift >= 0; shift -= 8) b += static_cast<char>((v >> shift) & 0xFF);
    };
    for (const auto& [size, type] : kSizes) {
        SurfacePtr square{SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32)};
        if (!square) return false;
        SDL_FillSurfaceRect(square.get(), nullptr, 0); // (see-through around it)
        const float s = std::min(static_cast<float>(size) / static_cast<float>(picture->w), static_cast<float>(size) / static_cast<float>(picture->h));
        const int w = std::max(1, static_cast<int>(std::lround(static_cast<float>(picture->w) * s)));
        const int h = std::max(1, static_cast<int>(std::lround(static_cast<float>(picture->h) * s)));
        const SDL_Rect at{(size - w) / 2, (size - h) / 2, w, h};
        SDL_SetSurfaceBlendMode(picture, SDL_BLENDMODE_NONE);
        SDL_BlitSurfaceScaled(picture, nullptr, square.get(), &at, SDL_SCALEMODE_LINEAR);
        const IOStreamPtr png{SDL_IOFromDynamicMem()};
        if (!png || !IMG_SavePNG_IO(square.get(), png.get(), false)) return false;
        const auto bytes = static_cast<std::size_t>(SDL_GetIOSize(png.get()));
        const auto* data = static_cast<const char*>(SDL_GetPointerProperty(SDL_GetIOProperties(png.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
        out += type;
        put32(out, static_cast<std::uint32_t>(8 + bytes));
        out.append(data, bytes);
    }
    std::string icns = "icns";
    put32(icns, static_cast<std::uint32_t>(8 + out.size()));
    icns += out;
    return SDL_SaveFile(file.c_str(), icns.data(), icns.size());
}

// A plist's <string> under `key` set to `value` (added before its end when it has none).
void setPlistString(std::string& plist, std::string_view key, std::string_view value) {
    std::string escaped;
    for (const char c : value) escaped += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string(1, c);
    const std::string tag = std::format("<key>{}</key>", key);
    if (const auto at = plist.find(tag); at != std::string::npos) {
        const auto open = plist.find("<string>", at), close = plist.find("</string>", at);
        if (open != std::string::npos && close != std::string::npos && open < close) {
            plist.replace(open + 8, close - open - 8, escaped);
            return;
        }
    }
    if (const auto end = plist.rfind("</dict>"); end != std::string::npos) plist.insert(end, std::format("    {}\n    <string>{}</string>\n", tag, escaped));
}

} // namespace

#if defined(SDL_PLATFORM_MACOS)
namespace {
// A signed app sent to Apple to be notarized (notarytool, with the keychain profile `profile`; it waits for
// the answer), and the ticket stapled to it: what to tell the developer.
std::string notarized(const std::string& app, const std::string& profile) {
    const std::string zip = app + ".notarizing.zip";
    if (!runProgram({"/usr/bin/ditto", "-c", "-k", "--keepParent", app, zip})) return " It's signed, but couldn't be zipped to be notarized.";
    const ScopeExit _([&] { SDL_RemovePath(zip.c_str()); }); // (however it ends)
    if (!runProgram({"/usr/bin/xcrun", "notarytool", "submit", zip, "--keychain-profile", profile, "--wait"}))
        return std::format(" It's signed, but Apple didn't notarize it (notarytool, with the profile '{}').", profile);
    if (!runProgram({"/usr/bin/xcrun", "stapler", "staple", app})) return " It's signed and notarized, but the ticket couldn't be stapled to it.";
    return " It's signed and notarized.";
}
} // namespace
#endif

// Pack the game: next to the game folder, a folder named after the game with the player, renamed
// after it (on a Mac My Game.app, with the game's icon and the pack inside it; elsewhere My Game.exe
// or My Game, with game.vnext beside it) and licenses.txt; and the same folder as a zip. One made
// before goes to the Trash first.
std::expected<std::string, std::string> Engine::finishGame() const {
    if (packed_) return std::unexpected("This game is a pack already: open its game folder to pack it again.");
    if (!script_.ok()) return std::unexpected("Fix the scripts first: a game with mistakes in it can't be packed.");
    const Uint64 start = SDL_GetTicksNS();
    std::string name; // (the game's title, as a file's name)
    for (const char c : script_.config.title)
        if (!std::string_view("/\\:*?\"<>|").contains(c) && static_cast<unsigned char>(c) >= 32) name += c;
    name = std::string(trim(name));
    while (name.starts_with('.')) name.erase(0, 1);
    if (name.empty()) name = "Game";
    const std::string next = packPath().substr(0, packPath().size() - kPackName.size()); // (where the game folder is)
    const std::string folder = next + name, zip = folder + ".zip";
    for (const std::string& old : {folder, zip})
        if (SDL_GetPathInfo(old.c_str(), nullptr) && !moveToTrash(old)) return std::unexpected(std::format("Can't replace '{}': move it away first.", old));
    if (!SDL_CreateDirectory(folder.c_str())) return std::unexpected(std::format("Can't make '{}': {}.", folder, SDL_GetError()));
    const std::string player = playerPath();
    const bool app = player.ends_with(".app");
#if defined(_WIN32)
    const std::string copy = folder + "/" + name + (app ? ".app" : ".exe");
#else
    const std::string copy = folder + "/" + name + (app ? ".app" : "");
#endif
    if (player.empty() || !copyFiles(player, copy)) return std::unexpected(std::format("Can't copy the player '{}' into '{}'.", player, folder));
    std::string notes; // (what didn't work, said after)
    if (const char* base = SDL_GetBasePath(); !app && base && script_.config.icon.empty()) // (the window's icon, without one of the game's: the player's, next to it)
        if (!SDL_CopyFile((std::string(base) + "icon.png").c_str(), (folder + "/icon.png").c_str())) notes += " The player's icon can't be copied next to it.";
    const std::string inside = app ? copy + "/Contents/Resources/" : folder + "/"; // (where the pack goes)
    if (app) { // its name, its own identifier (so macOS keeps its permissions apart from other games'), and the game's icon
        SDL_CreateDirectory(inside.c_str());
        SDL_RemovePath((inside + std::string(kPackName)).c_str()); // (a player with a game in it already)
        const std::string plistPath = copy + "/Contents/Info.plist";
        if (auto plist = readTextFile(plistPath)) {
            std::string id;
            for (const char c : lower(script_.config.saveId.empty() ? name : script_.config.saveId))
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') id += c;
            setPlistString(*plist, "CFBundleName", name);
            setPlistString(*plist, "CFBundleDisplayName", name);
            setPlistString(*plist, "CFBundleIdentifier", "dev.vnext.game." + (id.empty() ? std::string("game") : id));
            setPlistString(*plist, "CFBundleShortVersionString", script_.config.version.empty() ? std::string("1.0") : script_.config.version);
            if (!script_.config.icon.empty())
                if (SurfacePtr icon{IMG_Load_IO(files_.open(gameRoot_ + script_.config.icon).release(), true)}) {
                    if (writeIcns(icon.get(), inside + "icon.icns")) setPlistString(*plist, "CFBundleIconFile", "icon");
                    else notes += " Its icon can't be made.";
                }
            if (!SDL_SaveFile(plistPath.c_str(), plist->data(), plist->size())) notes += std::format(" Its name and icon can't be set: {}.", SDL_GetError());
        }
    }
    const auto report = writePack(gameRoot_, inside + std::string(kPackName));
    if (!report) return std::unexpected(report.error());
    for (const std::string& at : {inside, folder + "/"})
        if (!SDL_SaveFile((at + "licenses.txt").c_str(), licenses().data(), licenses().size()))
            return std::unexpected(std::format("Can't write '{}licenses.txt': {}.", at, SDL_GetError()));
#if defined(SDL_PLATFORM_MACOS)
    if (app) { // (signed again: its name and icon changed what it was signed with)
        runProgram({"/usr/bin/xattr", "-cr", copy});
        const std::string& identity = script_.config.macSigning;
        if (identity.empty()) {
            if (!runProgram({"/usr/bin/codesign", "--force", "--deep", "--sign", "-", copy})) notes += " It can't be signed again, so it may not open: try Pack the game again.";
        } else if (!runProgram({"/usr/bin/codesign", "--force", "--deep", "--options", "runtime", "--timestamp", "--sign", identity, copy})) {
            notes += std::format(" It can't be signed with '{}' (is that identity in your keychain?), so other Macs may not open it.", identity);
        } else if (const std::string& profile = script_.config.notaryProfile; !profile.empty()) {
            notes += notarized(copy, profile);
        } else {
            notes += std::format(" It's signed with '{}' (not notarized: no notary profile).", identity);
        }
    }
#endif
    if (const auto problem = writeZip(folder, zip)) return std::unexpected(*problem);
    return std::format("Made {} ({}, and licenses.txt) and {}.zip next to the game folder, ready to share: {} files packed ({:.1f} MB) in {:.1f} seconds.{}",
                       folder, app ? name + ".app with the game in it" : name + " with game.vnext", name, report->files, megabytes(report->packed),
                       static_cast<double>(SDL_GetTicksNS() - start) / 1e9, notes);
}


// Applies what the script sets up: canvas, window title, save pages, fonts, screens. Script
// errors switch to the error screen.
void Engine::configure() {
    const GameConfig& cfg = script_.config;
    canvas_ = {static_cast<float>(cfg.width), static_cast<float>(cfg.height)};
    defaults_ = defaultSettings(cfg);
    useLanguage(); // texts, window title, fonts
    // The window's icon: the game's (config icon), else the player's own (icon.png with the program: in the app).
    SurfacePtr icon{noGame_ || cfg.icon.empty() ? nullptr : IMG_Load_IO(files_.open(gameRoot_ + cfg.icon).release(), true)}; // (a missing file is fine)
    if (const char* base = SDL_GetBasePath(); !icon && base) icon.reset(IMG_Load((std::string(base) + "icon.png").c_str()));
    if (icon) SDL_SetWindowIcon(window_.get(), icon.get());
    // Saves go in game/saves, unless --data was given, the game is a pack (finished games keep them
    // in the player's own folder), or the folder can't take them.
    std::string saves = prefPath_ + "saves/";
    if (!dataFolder_ && !packed_ && !noGame_ && gameRoot_.find(".app/Contents/Resources/") == std::string::npos &&
        SDL_CreateDirectory((gameRoot_ + "saves").c_str()))
        saves = gameRoot_ + "saves/";
    saves_.setFolder(saves);
    assets_->setPixelArt(cfg.pixelArt);
    // The mouse pointer: a picture from images/ (config cursor), or the system's.
    cursor_.reset();
    if (!cfg.cursor.empty()) {
        const std::string file = cfg.cursor.find('.') == std::string::npos ? std::format("images/{}.png", cfg.cursor) : cfg.cursor;
        if (SurfacePtr picture{IMG_Load_IO(files_.open(gameRoot_ + file).release(), true)})
            cursor_.reset(SDL_CreateColorCursor(picture.get(), cfg.cursorHotspot.x, cfg.cursorHotspot.y));
        if (!cursor_) logWarn("Can't use '{}' as the mouse pointer: {}.", file, SDL_GetError());
    }
    SDL_SetCursor(cursor_ ? cursor_.get() : SDL_GetDefaultCursor());
    assets_->setAtlases(script_.atlases);
    assets_->setSubPictures(script_.subPictures);
    movies_.clear();
    audio_.setCrossfade(cfg.musicCrossfade);
    audio_.setDucking(cfg.musicWhileSpeaking);
    assets_->setMemoryLimit(static_cast<std::size_t>(cfg.imageMemory) * 1024 * 1024);
    saves_.configure(cfg.savePages, cfg.saveSlots);
    hasSave_ = saves_.newest().has_value();
    if (!noGame_) logInfo("Saves go in '{}'.", saves);
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
    return std::make_unique<Game>(Context{renderer_.get(), *assets_, audio_, settings_, readLog_, persistent_, chart_, canvas_,
                                          [this](std::string_view name) { return builtin(name); }},
                                  script_);
}

// Names the engine knows: usable in conditions and {texts} like variables.
std::optional<Value> Engine::builtin(std::string_view name) const {
    auto flag = [](bool b) { return std::optional<Value>(b ? 1 : 0); };
    const Game* g = game_.get();
    const bool playing = mode_ == Mode::Game && g;
    if (name == "in_game") return flag(playing);
    if (name == "has_save") return flag(hasSave_);
    if (name == "can_save") return flag(playing && g->canSave());
    if (name == "can_rollback") return flag(playing && g->canRollback());
    if (name == "skip") return flag(playing && g->skipping());
    if (name == "auto") return flag(playing && g->autoMode());
    if (name == "speaking") return flag(playing && g->showingSay());
    if (name == "choosing") return flag(playing && g->showingChoices());
    if (name == "prompt") return flag(playing && g->menuPrompt());
    if (name == "text_complete") return flag(playing && g->textComplete());
    if (name == "time_left") { // a timed choice's time, or the time before the screen on top closes by itself: 100 down to 0
        const ScreenDef* top = overlays_.empty() ? nullptr : script_.screen(overlays_.back());
        const std::optional<float> left = top && top->closeAfter > 0.0f ? std::optional(1.0f - (time_ - topSince_) / top->closeAfter)
                                        : playing ? g->timeLeft() : std::nullopt;
        return static_cast<int>(std::lround(std::clamp(left.value_or(0.0f), 0.0f, 1.0f) * 100.0f));
    }
    const auto& gallery = script_.records(RecordKind::Gallery);
    if (name == "cg") return cg_ < gallery.size() ? gallery[cg_].name : std::string{};
    if (name == "cg_title") return cg_ < gallery.size() ? gallery[cg_].title : std::string{};
    if (name == "cg_movie") return flag(galleryMovie(cg_));
    if (name == "tip" || name == "tip_title" || name == "tip_text") { // the tip being read (none after forgetting progress)
        const Script::Record* tip = tip_ && has(RecordKind::Tip, *tip_) ? &script_.records(RecordKind::Tip)[*tip_] : nullptr;
        return !tip ? std::string{} : name == "tip" ? tip->name : name == "tip_title" ? tip->title : tip->text;
    }
    if (name == "chart_places") return static_cast<int>(chart_.nodes().size());
    // How many pictures, endings, songs, achievements and tips there are, and how many the player has.
    static constexpr std::array<std::pair<std::string_view, std::string_view>, kRecordKinds.size()> counts{
        {{"cg_total", "cg_unlocked"}, {"endings_total", "endings_reached"}, {"songs_total", "songs_heard"},
         {"achievements_total", "achievements_earned"}, {"tips_total", "tips_found"}}};
    for (std::size_t k = 0; k < counts.size(); ++k) {
        if (name == counts[k].first) return static_cast<int>(script_.lists[k].size());
        if (name == counts[k].second) return count(static_cast<RecordKind>(k));
    }
    if (name == "chapter") return g ? g->chapter() : std::string{};
    if (name == "messages_total") return g ? static_cast<int>(g->messages().size()) : 0;
    if (name == "contact") return contact_;
    if (name == "has_phone") return flag(playing && g->hasPhone());
    if (name == "messages_unread") // (not the ones still being typed)
        return g ? static_cast<int>(std::ranges::count_if(g->messages(), [](const MessageItem& m) { return !m.read && m.typing <= 0.0f; })) : 0;
    if (name == "dragging") { // what the player holds, in the screen on top
        for (const auto& screen : overlays_ | std::views::reverse)
            if (auto it = views_.find(screen); it != views_.end()) return it->second.held();
        return std::string{};
    }
    if (name == "has_face") return flag(playing && g->showingSay() && !g->faceBase().empty());
    if (name == "face_side") return playing && g->showingSay() ? g->faceSide() : std::string{};
    if (name == "fullscreen") return flag(settings_.fullscreen);
    if (name == "developer") return flag(config().developer);
    if (name == "thumbnail") return flag(thumbnail_);
    if (name == "save_mode") return flag(slots_.mode() == SlotBrowser::Mode::Save);
    if (name == "title") return config().title;
    if (name == "language") return languageNames()[static_cast<std::size_t>(languageIndex())];
    if (name == "languages") return static_cast<int>(translations_.size()) + 1;
    if (name == "version") return config().version;
    if (name == "engine_version") return std::string(kVersion);
    if (name == "sdl_version") return sdlVersion();
    if (name == "speaker") return g ? g->speaker() : std::string{};
    if (name == "message") return notice_;
    if (name == "question") return question_;
    if (name == "page" && !slots_.pages().empty()) {
        const auto& p = slots_.pages()[slots_.page()];
        return p;
    }
    // The player's settings: as a slider's reads ("45 cps", "80%"), the window's size, or a number (yes: 1).
    if (name == "window_size") return std::format("{} x {}", settings_.windowWidth, settings_.windowHeight);
    if (const SettingControl* c = findSettingControl(name)) return c->text(settings_, config());
    if (const auto v = settings_.get(name)) return *v;
    return std::nullopt;
}

void Engine::updatePixelScale() {
    // Rasterize text at the size the canvas is actually shown at (depends on the scaling mode).
    SDL_FRect shown{};
    if (!renderer_ || !SDL_GetRenderLogicalPresentationRect(renderer_.get(), &shown) || shown.w <= 0 || shown.h <= 0) return;
    fonts_.setPixelScale(std::max(shown.w / canvas_.w, shown.h / canvas_.h));
}

void Engine::refreshWindowSizes() {
    static constexpr std::array factors{0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f};
    SDL_Rect usable{0, 0, static_cast<int>(canvas_.w), static_cast<int>(canvas_.h)};
    SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_.get()), &usable);
    windowSizes_.clear();
    for (float f : factors) {
        const SDL_Point p{static_cast<int>(std::lround(canvas_.w * f)), static_cast<int>(std::lround(canvas_.h * f))};
        if (p.x >= 320 && ((p.x <= usable.w && p.y <= usable.h) || f == 1.0f)) windowSizes_.push_back(p);
    }
}

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

// Messages that come while one is showing wait their turn (an ending, then an achievement...).
void Engine::notify(std::string message) {
    logInfo("{}", message);
    if (noticeTime_ > 0.0f && !notice_.empty()) {
        if (message != notice_ && (noticeQueue_.empty() || noticeQueue_.back() != message)) noticeQueue_.push_back(std::move(message));
        if (noticeQueue_.size() > 4) noticeQueue_.pop_front(); // (not a long line of old news)
        return;
    }
    notice_ = std::move(message);
    noticeTime_ = config().notifyTime;
}

void Engine::showErrors(const std::vector<std::string>& lines) {
    std::string text;
    for (std::size_t i = 0; i < lines.size() && i < 16; ++i) text += lines[i] + '\n';
    if (lines.size() > 16) text += std::format("... and {} more (see log.txt)\n", lines.size() - 16);
    errorText_.set(fonts_.get("", 18.0f * canvas_.h / 720.0f), std::move(text), canvas_.w - 120);
    closeAll();
    mode_ = Mode::Error;
}

void Engine::leaveStory(bool keepSound) {
    if (mode_ == Mode::Game && !splash_ && !opening_) autosave(); // leaving a game in progress
    splash_ = opening_ = false;
    if (!keepSound) audio_.stopAll(1.0f);
    game_ = makeGame(); // idle: title buttons can still set variables and play music
    if (!prefPath_.empty()) saveReadLog(readLog_, prefPath_ + "read.txt");
    closeAll();
    syncShownScreens();
    mode_ = Mode::Title;
}

void Engine::openTitle(bool keepSound) {
    leaveStory(keepSound);
    for (const auto& [name, value] : beforeTitle_) game_->setVariable(name, value); // (its buttons see what was picked before it)
    titleClosing_.reset();
    hasSave_ = saves_.newest().has_value();
    if (ScreenView* title = view("title")) {
        title->reset();
        titleShown_ = true;
        screenHook(title->def(), true);
    } else if (storyStarted_) {
        quit(); // the story is over, and there's no title screen to go back to
    } else if (script_.findLabel("start")) {
        newGame("start"); // a game without a title screen starts at once
    }
}

// In developer mode the player doesn't close by itself (Quit, or the story's end without a title screen):
// it goes back to the developer tools.
void Engine::quit() {
    if (config().developer) developer_.backToEditor();
    else quit_ = true;
}

void Engine::newGame(std::string_view label, bool subroutine) {
    const std::string name(label.empty() ? "start" : label);
    const auto pc = script_.findLabel(name);
    if (!pc && name != "start") return logWarn("There's no label called '{}' to start from.", name);
    // (without "label start" the story starts at its first line; from the title screen, with what was picked before it)
    startAt(pc.value_or(0), subroutine, mode_ == Mode::Title && name != "start" ? &beforeTitle_ : nullptr);
}

void Engine::veiled(const Transition& t, std::function<void()> then) {
    const bool fast = mode_ == Mode::Game && game_->skipping();
    float seconds = t.duration; // (a first fade of 0 seconds goes straight to its colour: the ones after it still fade)
    for (const Transition& more : t.then) seconds += std::max(0.0f, more.duration);
    if (t.kind != TransitionKind::Fade || seconds <= 0.0f || !settings_.transitions || fast || veil_) return then();
    veil_ = Veil{.fades = {{t.color, t.duration}}, .time = 0.0f, .then = std::move(then)};
    for (const Transition& more : t.then) veil_->fades.emplace_back(more.color, std::max(0.0f, more.duration));
}

float Engine::Veil::opensAt() const {
    float at = 0.0f;
    for (const auto& [color, seconds] : fades) at += seconds / 2;
    return at;
}

SDL_Color Engine::Veil::color() const {
    auto mix = [](SDL_Color a, SDL_Color b, float f) {
        auto m = [&](Uint8 x, Uint8 y) { return static_cast<Uint8>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * f)); };
        return SDL_Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
    };
    float t = time;
    for (std::size_t i = 0; i < fades.size(); ++i) { // (to each one's colour, from the last one's: the first from nothing)
        const float half = fades[i].second / 2;
        if (t < half) {
            const SDL_Color to = fades[i].first;
            const SDL_Color from = i == 0 ? SDL_Color{to.r, to.g, to.b, 0} : fades[i - 1].first;
            return mix(from, to, half > 0.0f ? t / half : 1.0f);
        }
        t -= half;
    }
    const SDL_Color last = fades.empty() ? SDL_Color{} : fades.back().first; // (then away from it)
    const float half = fades.empty() ? 0.0f : fades.back().second / 2;
    return mix(last, SDL_Color{last.r, last.g, last.b, 0}, half > 0.0f ? std::clamp(t / half, 0.0f, 1.0f) : 1.0f);
}

// The title screen goes when a game starts or is loaded: its on_hide runs (stop music...).
void Engine::leaveTitle() {
    titleClosing_.reset();
    if (!std::exchange(titleShown_, false)) return;
    if (ScreenView* title = view("title")) screenHook(title->def(), false);
}

// Its hide_animation= first, then its closing fade (when the story opened it "and close with fade").
void Engine::fromTitle(std::function<void()> then) {
    if (mode_ != Mode::Title) return then();
    if (ScreenView* title = view("title"); title && title->def().hideAnimation && !title->hasLeft() && !viewsFrom_) {
        title->leave();
        leaving_.push_back({"title", [this, then = std::move(then)]() mutable { fromTitle(std::move(then)); }});
        return;
    }
    if (titleClosing_) return veiled(*std::exchange(titleClosing_, std::nullopt), std::move(then));
    then();
}

void Engine::startAt(std::size_t pc, bool subroutine, const Variables* carried) {
    leaveTitle();
    closeAll();
    game_ = makeGame();
    if (carried) game_->carry(*carried);
    mode_ = Mode::Game;
    splash_ = opening_ = false;
    storyStarted_ = storyStarted_ || !subroutine;
    game_->start(pc, subroutine);
}

ScreenView* Engine::view(std::string_view name) {
    if (viewsFrom_) { // (made when first needed, until the editor's scripts change)
        return developer_.previewScreen(name, *viewsFrom_);
    }
    auto it = views_.find(name);
    return it != views_.end() ? &it->second : nullptr;
}

ScreenView* Engine::textbox() {
    if (ScreenView* v = view(game_->textbox())) return v;
    return view("say");
}

// Runs on_show / on_hide for screens that "show screen" / "hide screen" changed.
void Engine::syncShownScreens() {
    const std::vector<std::string> now = mode_ == Mode::Game ? game_->shownScreens() : std::vector<std::string>{};
    const std::vector<std::string> before = std::exchange(shown_, now);
    for (const auto& name : before)
        if (!std::ranges::contains(now, name))
            if (ScreenView* v = view(name)) {
                screenHook(v->def(), false);
                left(name);
            }
    for (const auto& name : now)
        if (!std::ranges::contains(before, name))
            if (ScreenView* v = view(name)) {
                v->opened();
                screenHook(v->def(), true);
            }
}

ScreenView* Engine::topView() {
    return !overlays_.empty() ? view(overlays_.back()) : mode_ == Mode::Title ? view("title") : nullptr;
}

bool Engine::open(const std::string& name) {
    ScreenView* v = view(name);
    if (!v) {
        logWarn("There's no screen called '{}' to open.", name);
        return false;
    }
    if (!overlays_.empty() && overlays_.back() == name) return true;
    // Animations wait for each other: a screen whose show_animation= starts covered (fade=) opens once the
    // one under it has faded out with its hide_animation= (ending covered), not over it at once.
    if (v->startsCovered() && !viewsFrom_)
        if (ScreenView* under = topView(); under && under != v && under->endsCovered() && !under->hasLeft()) {
            under->leave();
            leaving_.push_back({!overlays_.empty() ? overlays_.back() : std::string("title"), [this, name] { openNow(name); }});
            return true;
        }
    return openNow(name);
}

bool Engine::openNow(const std::string& name) {
    ScreenView* v = view(name);
    if (!v) return false;
    if (!overlays_.empty() && overlays_.back() == name) return true;
    closingFades_.erase(name);
    for (const auto& el : v->def().elements)
        if (el.kind == ElementKind::Slots)
            slots_.open(el.option("mode", name == "save" ? "save" : "load") == "save" ? SlotBrowser::Mode::Save : SlotBrowser::Mode::Load);
    v->reset();
    backTo_.reset(); // (something opening over the rest: no longer going back)
    overlays_.push_back(name);
    screenHook(v->def(), true);
    return true;
}

void Engine::close(bool fade) {
    if (overlays_.empty()) return;
    if (!fade) return closeNow();
    const std::string name = overlays_.back();
    ScreenView* v = view(name);
    if (v && v->hasLeft()) return; // (going already)
    if (v && v->def().hideAnimation && !viewsFrom_) { // its hide_animation= plays, and then it closes
        v->leave();
        leaving_.push_back({name, [this, name] {
                                if (overlays_.empty() || overlays_.back() != name) return;
                                const ScreenView* gone = view(name);
                                coveredClose_ = gone && gone->endsCovered(); // (everything covered: what was under it comes back into view)
                                closeFaded();
                            }});
        return;
    }
    closeFaded();
}

void Engine::closeFaded() {
    if (overlays_.empty()) return;
    const std::string name = overlays_.back();
    if (const auto it = closingFades_.find(name); it != closingFades_.end()) { // (opened "and close with fade": it fades first)
        const Transition t = std::move(it->second);
        closingFades_.erase(it);
        if (!veil_)
            return veiled(t, [this, name] {
                if (!overlays_.empty() && overlays_.back() == name) closeNow();
            });
    }
    closeNow();
}

void Engine::closeNow() {
    if (overlays_.empty()) return;
    const std::string name = overlays_.back();
    overlays_.pop_back();
    if (name == "confirm") confirmYes_ = {};
    saveSettings();
    if (ScreenView* v = view(name)) screenHook(v->def(), false);
    // What was under it comes back into view when the closing ended covered, or when it had faded out for
    // the one closing to open: its show_animation= plays again, after.
    const bool covered = std::exchange(coveredClose_, false);
    if (ScreenView* top = topView(); top && (covered || top->hasLeft())) top->opened();
    if (calledDepth_ > overlays_.size()) { // the story was waiting for this screen
        calledDepth_ = 0;
        game_->screenClosed(std::exchange(answer_, std::nullopt));
    }
    closeTowards();
}

void Engine::backTo(const std::string& name) {
    backTo_ = name;
    closeTowards();
}

void Engine::closeTowards() {
    if (!backTo_) return;
    if (!overlays_.empty() && overlays_.back() != *backTo_) return close();
    const std::string name = *std::exchange(backTo_, std::nullopt);
    if (ScreenView* v = view(name); v && (!overlays_.empty() || (name == "title" && titleShown_))) screenHook(v->def(), true); // (on top again: opened)
}

void Engine::left(const std::string& name) {
    ScreenView* v = view(name);
    if (!v || !v->def().hideAnimation || viewsFrom_) return;
    v->leave();
    std::erase_if(leaving_, [&](const Leaving& l) { return l.name == name; });
    leaving_.push_back({name, {}});
}

void Engine::closeAll() {
    overlays_.clear();
    backTo_.reset();
    closingFades_.clear();
    contact_.clear();
    confirmYes_ = {};
    calledDepth_ = 0;
}

void Engine::ask(std::string question, std::function<void()> yes) {
    ScreenView* confirm = view("confirm");
    if (question.empty() || !confirm) return yes(); // no question, or no confirm screen: just do it
    question_ = std::move(question);
    confirmYes_ = std::move(yes);
    confirm->reset();
    overlays_.push_back("confirm");
}

void Engine::screenHook(const ScreenDef& def, bool opening) {
    const ScreenDef::Stops& stops = opening ? def.stopOnOpen : def.stopOnClose;
    Commands stop;
    if (stops.music) stop.emplace_back(cmd::Play{.track = cmd::Play::Track::Music, .fade = 0.5f});
    if (stops.voice) stop.emplace_back(cmd::Play{.track = cmd::Play::Track::Voice});
    run(stop);
    run(opening ? def.onShow : def.onHide);
}

void Engine::run(const Commands& commands, const std::string& confirm) {
    if (commands.empty()) return;
    if (trying_) {
        tried_.insert(tried_.end(), commands.begin(), commands.end());
        return;
    }
    if (!confirm.empty()) return ask(expand(confirm, lookup()), [this, commands] { run(commands); });
    for (const Command& c : commands) {
        command(c);
        if (quit_) break;
    }
}

// One command from a button, key or screen hook.
void Engine::command(const Command& c) {
    if (const auto* u = std::get_if<cmd::Ui>(&c)) return ui(*u);
    if (const auto* jump = std::get_if<cmd::Jump>(&c)) {
        if (mode_ != Mode::Game) return newGame(jump->label); // from the title screen
        closeAll();
    }
    game_->perform(c);
}

// Menu commands, from buttons or from the story.
void Engine::ui(const cmd::Ui& c) {
    const std::string& v = c.verb;
    if (v == "start") fromTitle([this, label = c.arg] { label.empty() ? newStory() : newGame(label); });
    else if (v == "continue") {
        if (const auto newest = saves_.newest()) fromTitle([this, slot = *newest] { loadFromSlot(slot); });
    }
    else if (v == "open") {
        if (c.arg.find("://") != std::string::npos || c.arg.starts_with("www.")) SDL_OpenURL(c.arg.c_str());
        else if (!overlays_.empty() && overlays_.back() == c.arg) close(); // open already, on top: put it away, so one key opens and closes it
        else if (std::ranges::contains(overlays_, c.arg) || (c.arg == "title" && titleShown_)) backTo(c.arg); // open under others: back to it
        else veiled(c.transition, [this, name = c.arg, closing = c.closing] {
            if (open(name) && closing.kind == TransitionKind::Fade) closingFades_.insert_or_assign(name, closing);
        });
    }
    else if (v == "close") { // (the title screen, with nothing over it: the story goes on, as with New Game)
        if (overlays_.empty() && mode_ == Mode::Title) fromTitle([this] { newStory(); });
        else close();
    }
    else if (v == "answer") {
        if (calledDepth_ == overlays_.size() && calledDepth_ > 0) answer_ = expand(c.arg, lookup());
        close();
    }
    else if (v == "notify") notify(fillValues(c.arg, lookup())); // its text tags are read when it's shown
    else if (v == "quit") quit();
    else if (v == "quick save") quickSave();
    else if (v == "quick load") quickLoad();
    else if (v == "skip") game_->toggleSkip();
    else if (v == "auto") game_->toggleAuto();
    else if (v == "hide ui") game_->setUiHidden(true);
    else if (v == "rollback") {
        if (mode_ == Mode::Game) game_->rollback();
    }
    else if (v == "advance") {
        if (mode_ != Mode::Game) return;
        if (game_->skipMode()) game_->toggleSkip(); // any input stops skipping
        else game_->advance();
    }
    else if (v == "fullscreen") toggleFullscreen();
    else if (v == "screenshot") screenshotRequest_ = std::format("{}screenshot-{}.png", prefPath_, SDL_GetTicks());
    else if (v == "reload") { if (developerMode()) reloadRequested_ = true; }
    else if (v == "yes" || v == "no") {
        auto yes = std::exchange(confirmYes_, {});
        close();
        if (v == "yes" && yes) yes();
    }
    else if (v == "next page") slots_.turn(+1);
    else if (v == "previous page") slots_.turn(-1);
    else if (v == "delete save") { if (slots_.selected) slotDelete(*slots_.selected, {}); }
    else if (v == "next cg" || v == "previous cg") { // the next unlocked picture in the gallery
        const std::size_t n = script_.records(RecordKind::Gallery).size();
        for (std::size_t step = 1; step < n; ++step) {
            const std::size_t i = (cg_ + (v == "next cg" ? step : n - step)) % n;
            if (has(RecordKind::Gallery, i)) {
                cg_ = i;
                break;
            }
        }
    }
    else if (v == "setting") {
        changeSetting(settings_, c.arg);
        settingsChanged(c.arg.starts_with("master_volume") || c.arg.starts_with("sound_volume"));
    }
    else if (v == "reset settings" || v == "reset controls") {
        if (v == "reset settings") settings_ = defaults_;
        else settings_.keys.clear();
        refreshKeys();
        applySettings();
        saveSettings();
    }
    else if (v == "reset progress") { // forget what's kept between playthroughs (not settings or saves)
        persistent_.clear();
        readLog_.clear();
        chart_.clear();
        savePersistent();
        if (!prefPath_.empty()) saveReadLog(readLog_, prefPath_ + "read.txt");
    }
}

// A button's "setting NAME VALUE" (as the parser leaves it: "NAME NUMBER"; the window's size and the
// language go up or down a step, the window's size can be given ("window_size 1280 720") and the
// language named ("language japanese")).
void Engine::changeSetting(Settings& s, std::string_view what) {
    const auto space = what.find(' ');
    const std::string_view name = what.substr(0, space);
    int n = 0; // (the number after the name: the first of them, if there are two)
    if (space != std::string_view::npos) std::from_chars(what.data() + space + 1, what.data() + what.size(), n);
    if (name == "window_size" && n != 1 && n != -1) { // a width and a height ("window_size 1280 720")
        const std::string_view rest = space == std::string_view::npos ? std::string_view{} : what.substr(space + 1);
        const auto gap = rest.find(' ');
        int w = 0, h = 0;
        if (gap != std::string_view::npos) {
            std::from_chars(rest.data(), rest.data() + gap, w); // NOLINT(bugprone-suspicious-stringview-data-usage): its end is given
            std::from_chars(rest.data() + gap + 1, rest.data() + rest.size(), h);
        }
        if (w > 0 && h > 0) {
            s.windowWidth = w;
            s.windowHeight = h;
        }
    } else if (name == "window_size") { // the next size that fits the display, up or down
        refreshWindowSizes();
        if (s.fullscreen || windowSizes_.empty()) return;
        const auto count = static_cast<int>(windowSizes_.size());
        int at = -1;
        for (int i = 0; i < count; ++i)
            if (windowSizes_[static_cast<std::size_t>(i)].x == s.windowWidth && windowSizes_[static_cast<std::size_t>(i)].y == s.windowHeight) at = i;
        if (at < 0) { // (a size of the player's own: the one past it that way)
            int below = -1, above = count;
            for (int i = 0; i < count; ++i)
                if (windowSizes_[static_cast<std::size_t>(i)].x < s.windowWidth) below = i;
                else if (above == count) above = i;
            at = n > 0 ? above - n : below - n;
        }
        const auto& size = windowSizes_[static_cast<std::size_t>(std::clamp(at + n, 0, count - 1))];
        s.windowWidth = size.x;
        s.windowHeight = size.y;
    } else if (name == "language") {
        const auto count = static_cast<int>(languageNames().size());
        const std::string_view word = space == std::string_view::npos ? std::string_view{} : what.substr(space + 1);
        if (n != 0) {
            if (count > 0) s.language = languageId(((languageIndex() + n) % count + count) % count);
        } else if (const auto id = languageIdNamed(word)) {
            s.language = *id;
        } else {
            logWarn("There's no language called '{}'.", word);
        }
    } else {
        s.set(name, n);
    }
}

void Engine::loadPersistent() {
    persistent_.clear();
    const auto text = readTextFile(prefPath_ + "persistent.txt");
    if (!text) return;
    for (auto&& part : std::views::split(std::string_view(*text), '\n')) {
        const std::string_view line(part.begin(), part.end());
        const auto space = line.find(' ');
        if (space != std::string_view::npos) persistent_.insert_or_assign(std::string(line.substr(0, space)), decodeValue(line.substr(space + 1)));
    }
}

void Engine::savePersistent() {
    if (prefPath_.empty()) return;
    std::string out;
    for (const auto& [name, value] : persistent_) out += std::format("{} {}\n", name, encodeValue(value));
    if (!SDL_SaveFile((prefPath_ + "persistent.txt").c_str(), out.data(), out.size()))
        logWarn("Can't save what's kept between games: {}.", SDL_GetError());
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

bool Engine::galleryMovie(std::size_t index) const {
    const auto& gallery = script_.records(RecordKind::Gallery);
    if (index >= gallery.size()) return false;
    auto [it, fresh] = movies_.try_emplace(gallery[index].name, false);
    if (fresh) it->second = assets_->findMovie(gallery[index].name).has_value();
    return it->second;
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

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

std::vector<std::string> Engine::keyNames(const Control& control) const {
    if (auto it = settings_.keys.find(control.name); it != settings_.keys.end()) return it->second;
    std::vector<std::string> names;
    names.reserve(control.keys.size());
    for (const auto& k : control.keys) names.push_back(k.name());
    return names;
}

void Engine::refreshKeys() {
    controlKeys_.clear();
    for (const auto& control : script_.controls) {
        auto& keys = controlKeys_.emplace_back();
        for (const auto& name : keyNames(control))
            if (auto key = parseKeyChord(name)) keys.push_back(*key);
    }
}

std::string Engine::keyLabel(const KeyChord& key) const {
    std::string out;
    if (key.mods & SDL_KMOD_CTRL) out += "Ctrl+";
    if (key.mods & SDL_KMOD_SHIFT) out += "Shift+";
    if (key.mods & SDL_KMOD_ALT) out += "Alt+";
    if (key.mods & SDL_KMOD_GUI) out += "Cmd+";
    switch (key.device) {
    case KeyChord::Device::Mouse:
        return out + script_.translated(keyWord(key.button == SDL_BUTTON_LEFT ? "mouse_left" : key.button == SDL_BUTTON_RIGHT ? "mouse_right" : "mouse_middle"));
    case KeyChord::Device::Wheel:
        return out + script_.translated(keyWord(key.wheel > 0 ? "wheel_up" : "wheel_down"));
    case KeyChord::Device::Pad:
    case KeyChord::Device::Touch:
        return script_.translated(keyWord(key.name())); // pad_a, swipe_left...
    case KeyChord::Device::Keyboard:
        break;
    }
    const std::string name = SDL_GetKeyName(key.key);
    return out + (name == "Return" ? std::string("Enter") : name);
}

namespace {

bool padOrTouch(const std::string& name) {
    const auto key = parseKeyChord(name);
    return key && (key->device == KeyChord::Device::Pad || key->device == KeyChord::Device::Touch);
}

} // namespace

// Where a control's keyboard and mouse keys are in its list: slot 1, 2... count only those
// (controller buttons and touch gestures have their own column, or none).
std::vector<std::size_t> Engine::keySlots(const std::vector<std::string>& names) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < names.size(); ++i)
        if (!padOrTouch(names[i])) out.push_back(i);
    return out;
}

std::size_t Engine::slotOf(const Element& el) {
    if (el.option("slot") == "pad") return kPadSlot;
    return static_cast<std::size_t>(std::max(1.0f, el.number("slot", 1.0f))) - 1;
}

std::string Engine::keyLabel(std::string_view control, std::size_t slot) const {
    const Control* c = script_.control(control);
    const auto names = c ? keyNames(*c) : std::vector<std::string>{};
    if (slot == kPadSlot) { // the first controller button it has
        const auto pad = std::ranges::find_if(names, [](const std::string& n) { return n.starts_with("pad_"); });
        if (pad != names.end())
            if (auto key = parseKeyChord(*pad)) return keyLabel(*key);
        return {};
    }
    const auto keys = keySlots(names);
    if (slot < keys.size())
        if (auto key = parseKeyChord(names[keys[slot]])) return keyLabel(*key);
    return {};
}

// While the Controls screen waits for a key: Esc cancels, Backspace / Delete empties the slot,
// anything else (a key, possibly with Ctrl/Shift/Alt/Cmd, a mouse button or the wheel) is taken.
bool Engine::captureKey(const SDL_Event& e) {
    const auto mods = static_cast<SDL_Keymod>(SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI));
    KeyChord key;
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        if (e.key.repeat) return true;
        switch (e.key.key) {
        case SDLK_ESCAPE: rebinding_.reset(); return true;
        case SDLK_BACKSPACE:
        case SDLK_DELETE: setKey("none"); return true;
        case SDLK_LCTRL: case SDLK_RCTRL: case SDLK_LSHIFT: case SDLK_RSHIFT:
        case SDLK_LALT: case SDLK_RALT: case SDLK_LGUI: case SDLK_RGUI:
            return true; // wait for the key that goes with it
        default: break;
        }
        key = {.key = e.key.key, .mods = mods};
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button > SDL_BUTTON_RIGHT) return true;
        key = {.device = KeyChord::Device::Mouse, .mods = mods, .button = e.button.button};
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.wheel.y == 0) return true;
        key = {.device = KeyChord::Device::Wheel, .mods = mods, .wheel = e.wheel.y > 0 ? 1 : -1};
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        key = {.device = KeyChord::Device::Pad, .pad = e.gbutton.button};
        break;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_TEXT_INPUT:
        return true;
    default:
        return false;
    }
    if (rebinding_->second == kPadSlot && key.device != KeyChord::Device::Pad) return true; // the Controller column takes buttons only
    setKey(key.name());
    return true;
}

void Engine::setKey(const std::string& name) {
    if (!rebinding_) return;
    const auto [control, slot] = *std::exchange(rebinding_, std::nullopt);
    const Control* target = script_.control(control);
    if (!target) return;
    if (name != "none") // a key does one thing: take it away from the others
        for (const auto& other : script_.controls) {
            if (other.name == control) continue;
            auto names = keyNames(other);
            if (std::ranges::contains(names, name)) {
                if (padOrTouch(name)) std::erase(names, name);              // its column just goes empty
                else std::ranges::replace(names, name, std::string("none")); // the others keep their slots
                settings_.keys.insert_or_assign(other.name, std::move(names));
            }
        }
    auto names = keyNames(*target);
    if (slot == kPadSlot) { // replaces its controller button (or adds one)
        const auto pad = std::ranges::find_if(names, [](const std::string& n) { return n.starts_with("pad_"); });
        if (name == "none") {
            if (pad != names.end()) names.erase(pad);
        } else if (pad != names.end()) {
            *pad = name;
        } else {
            names.push_back(name);
        }
    } else {
        auto keys = keySlots(names);
        while (keys.size() <= slot) {
            names.emplace_back("none");
            keys.push_back(names.size() - 1);
        }
        names[keys[slot]] = name;
    }
    settings_.keys.insert_or_assign(control, std::move(names));
    refreshKeys();
    saveSettings();
}

void Engine::settingsChanged(bool testSound) {
    if (trying_) return;
    applySettings();
    if (testSound) audio_.playTestSound();
}

void Engine::slotPressed(std::size_t index, std::string overwriteQuestion, std::string loadQuestion) {
    if (trying_) return tried_.push_back(cmd::Ui{.verb = "save or load", .arg = {}});
    auto& cards = slots_.cards();
    if (index >= cards.size()) return;
    const std::string slot = cards[index].slot;
    const bool exists = cards[index].info.exists;
    if (slots_.mode() == SlotBrowser::Mode::Save) {
        auto save = [this, index, slot] {
            if (saveToSlot(slot)) slots_.refresh(index);
        };
        if (exists) ask(std::move(overwriteQuestion), save);
        else save();
    } else if (exists) {
        auto load = [this, slot] { loadFromSlot(slot); };
        if (mode_ == Mode::Game) ask(std::move(loadQuestion), load);
        else load();
    }
}

void Engine::slotDelete(std::size_t index, std::string question) {
    if (trying_) return tried_.push_back(cmd::Ui{.verb = "delete save", .arg = {}});
    auto& cards = slots_.cards();
    if (index >= cards.size() || !cards[index].info.exists) return;
    ask(std::move(question), [this, index, slot = cards[index].slot] {
        saves_.remove(slot);
        slots_.refresh(index);
        hasSave_ = saves_.newest().has_value();
    });
}

void Engine::playSong(std::size_t index) {
    if (trying_) return tried_.push_back(cmd::Ui{.verb = "play song", .arg = {}});
    if (!has(RecordKind::Song, index)) return; // not heard yet
    if (!musicBeforeSongs_) musicBeforeSongs_ = audio_.music();
    const std::string path = assets_->audio(script_.records(RecordKind::Song)[index].name);
    if (audio_.music() == path) audio_.stopMusic(0.5f); // the one playing: a click stops it
    else audio_.playMusic(path, true, 0.5f);
}

bool Engine::playing() const {
    return mode_ == Mode::Game;
}

void Engine::goToPlace(const std::string& id, std::string question) {
    if (trying_) return tried_.push_back(cmd::Ui{.verb = "go to place", .arg = {}});
    auto go = [this, id] {
        if (const Chart::Node* place = chart_.node(id); place && !place->state.empty())
            if (const auto went = resume(place->state); !went) logWarn("Can't go back to that place in the story: {}.", went.error());
    };
    if (mode_ == Mode::Game) ask(std::move(question), go);
    else go();
}

std::optional<std::size_t> Engine::placeCommand(std::string_view id) const {
    const auto colon = id.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const auto start = script_.findLabel(id.substr(0, colon));
    std::size_t offset = 0;
    if (!start || std::from_chars(id.data() + colon + 1, id.data() + id.size(), offset).ec != std::errc{}) return std::nullopt;
    if (*start + offset >= script_.code.size()) return std::nullopt;
    return *start + offset;
}

bool Engine::songsOpen() const {
    for (const auto& name : overlays_)
        if (const ScreenDef* def = script_.screen(name))
            if (std::ranges::any_of(def->elements, [](const Element& el) { return el.kind == ElementKind::Songs; })) return true;
    return false;
}

void Engine::historyVoice(std::size_t entry) {
    if (trying_) return tried_.push_back(cmd::Ui{.verb = "hear again", .arg = {}});
    if (mode_ != Mode::Game || entry >= game_->history().size()) return;
    const HistoryEntry& h = game_->history()[entry];
    if (!h.voice.empty()) audio_.replayVoice(h.voice, h.voiceVolume);
    else if (h.babble > 0.0f && settings_.babble) audio_.replayBabble(h.text, h.babble, h.babbleSpeed);
}

void Engine::historyBack(std::size_t entry) {
    if (trying_) return tried_.push_back(cmd::Ui{.verb = "go back", .arg = {}});
    if (mode_ == Mode::Game && game_->rollbackTo(entry)) closeAll();
}

void Engine::drop(const std::string& what, const Commands& commands) {
    game_->setVariable("dropped", what);
    run(commands);
}

void Engine::reply(std::size_t message, std::size_t option) {
    if (trying_) return tried_.push_back(cmd::Ui{.verb = "send answer", .arg = {}});
    if (mode_ == Mode::Game && game_->reply(message, option)) closeAll();
}

// Read aloud (the player's setting): a new line (who says it, then what), a choice's question and
// options, and on menus what the mouse or the keys are on; each once, stopping what was read before.
void Engine::readAloud() {
    if (!settings_.readAloud || developer_.isOpen() || waiting_) {
        if (!spoken_.empty()) speak({});
        spoken_.clear();
        return;
    }
    std::string now;
    if (ScreenView* menu = !overlays_.empty() ? view(overlays_.back()) : mode_ == Mode::Title ? view("title") : nullptr) {
        now = menu->pointedText();
    } else if (mode_ == Mode::Game && game_->showingChoices() && !game_->choices().empty()) {
        if (game_->menuPrompt()) now = game_->dialogue() + " ";
        for (const ChoiceItem& c : game_->choices()) now += expand(c.text, lookup()) + ". ";
    } else if (mode_ == Mode::Game && game_->showingSay()) {
        now = game_->speaker().empty() ? game_->dialogue() : game_->speaker() + ": " + game_->dialogue();
    }
    if (now == spoken_) return;
    spoken_ = now;
    if (!now.empty()) speak(now);
}

// Saving and loading say nothing by themselves: a game's buttons say what they want (an action, then
// notify). What goes wrong is logged.
void Engine::quickSave() {
    saveToSlot(saves_.rotatingSlot("quick"));
}

void Engine::quickLoad() {
    if (const auto slot = saves_.newest("quick")) loadFromSlot(*slot);
    else logWarn("There's no quick save to load.");
}

void Engine::autosave() {
    if (config().autosave && mode_ == Mode::Game && game_->canSave()) saveToSlot(saves_.rotatingSlot("auto"));
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
    if (script_.findLabel("after_load")) game_->perform(cmd::Jump{.label = "after_load", .call = true});
    return true;
}

// Plays on from a saved state (a save, a place on the flowchart, the game before F6), in a fresh
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
    splash_ = opening_ = false;
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

// F6: reads the scripts, images and audio again and continues where the player was.
void Engine::reload() {
    std::optional<std::string> snapshot;
    if (mode_ == Mode::Game && game_->canSave()) snapshot = game_->save();
    mode_ = Mode::Title;
    assets_->clear();
    audio_.clearCache();
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

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

void Engine::saveSettings() {
    if (!prefPath_.empty() && !settings_.save(prefPath_ + "settings.txt")) logWarn("Can't save the settings: {}.", SDL_GetError());
}

void Engine::toggleFullscreen() {
    settings_.fullscreen = !settings_.fullscreen;
    applySettings();
    saveSettings();
}

void Engine::applySettings() {
    const Settings& s = settings_;
    SDL_Window* window = window_.get();
    SDL_Renderer* renderer = renderer_.get();
    if (!window || !renderer) return;

    if (s.fullscreen != applied_.fullscreen) SDL_SetWindowFullscreen(window, s.fullscreen);
    if (!s.fullscreen && (s.windowWidth != applied_.windowWidth || s.windowHeight != applied_.windowHeight)) {
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(window);
        SDL_SetWindowSize(window, s.windowWidth, s.windowHeight);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    static constexpr std::array modes{SDL_LOGICAL_PRESENTATION_LETTERBOX, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE,
                                      SDL_LOGICAL_PRESENTATION_STRETCH, SDL_LOGICAL_PRESENTATION_OVERSCAN};
    SDL_SetRenderLogicalPresentation(renderer, static_cast<int>(canvas_.w), static_cast<int>(canvas_.h),
                                     modes[static_cast<std::size_t>(s.scaling)]);
    if (!SDL_SetRenderVSync(renderer, s.vsync ? 1 : 0)) logWarn("Can't change VSync: {}.", SDL_GetError());
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, std::to_string(s.frameLimit).c_str());
    audio_.setVolume(static_cast<float>(s.masterVolume) / 100.0f, static_cast<float>(s.musicVolume) / 100.0f,
                     static_cast<float>(s.soundVolume) / 100.0f, static_cast<float>(s.voiceVolume) / 100.0f);
    if (s.language != applied_.language && language() != script_.translation()) {
        useLanguage();
        if (game_) game_->retranslate();
    }
    applied_ = s;
    updatePixelScale();
}

void Engine::screenshot(const std::string& path) {
    // Read the whole window: with "Fill" scaling the logical viewport extends past its edges.
    SDL_Renderer* r = renderer_.get();
    int w = 0, h = 0;
    SDL_RendererLogicalPresentation mode{};
    SDL_GetRenderLogicalPresentation(r, &w, &h, &mode);
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SurfacePtr shot{SDL_RenderReadPixels(r, nullptr)};
    SDL_SetRenderLogicalPresentation(r, w, h, mode);
    if (shot && IMG_SavePNG(shot.get(), path.c_str())) logInfo("Saved the screenshot as '{}'.", path);
    else logWarn("Can't save the screenshot: {}.", SDL_GetError());
}

void Engine::drawGame(bool thumbnail) {
    thumbnail_ = thumbnail;
    SDL_Renderer* r = renderer_.get();
    game_->draw(fonts_.get("default", 18));
    if (!game_->uiHidden() && !game_->playingMovie()) { // (a video covers everything but menus)
        for (const auto& name : game_->shownScreens())
            if (ScreenView* v = view(name)) v->draw(r);
        for (const std::string& box : game_->boxes()) // every textbox with a line, the line on screen's on top
            if (ScreenView* v = view(box)) v->draw(r);
        if (ScreenView* v = view(game_->choiceScreen()); v && game_->showingChoices()) v->draw(r);
        if (ScreenView* v = view("hud"); v && !thumbnail) v->draw(r);
    }
    thumbnail_ = false;
}

// In developer mode the window says so, and how to open the developer tools (without a game, that's all
// there is).
std::string Engine::windowTitle() const {
    if (noGame_) return "VNext: developer mode";
    return config().developer ? std::format("{}  (developer mode: F10 for the developer tools)", config().title) : config().title;
}

void Engine::drawErrors() {
    SDL_Renderer* r = renderer_.get();
    draw::rect(r, {0, 0, canvas_.w, canvas_.h}, {40, 12, 18, 255});
    const float k = canvas_.h / 720.0f; // keep the layout proportional on bigger canvases
    errorTitle_.set(fonts_.get("", 24 * k), "Something in the script needs fixing");
    errorTitle_.draw(r, 60 * k, 50 * k, {255, 140, 150, 255});
    errorText_.draw(r, 60 * k, 100 * k, theme::text, TextBlock::kAll, {.on = false});
    errorHint_.set(fonts_.get("", 18 * k), config().developer ? "Fix the lines above, save, then press F6 to try again  ·  F10: developer tools  ·  Esc to quit"
                                                                 : "Fix the lines above, save, then press F6 to try again  ·  Esc to quit");
    errorHint_.draw(r, 60 * k, canvas_.h - 50 * k, theme::dim);
}

// ---------------------------------------------------------------------------
// Events and frames
// ---------------------------------------------------------------------------

// Fingers: a tap clicks, a quick swipe, a long press or a two-finger tap is a gesture (for
// controls), and dragging scrolls menus.
SDL_AppResult Engine::touch(const SDL_Event& e) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_.get(), &ww, &wh);
    const float x = e.tfinger.x * static_cast<float>(ww), y = e.tfinger.y * static_cast<float>(wh);
    auto mouse = [&](Uint32 type) {
        SDL_Event m{};
        m.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            m.motion.windowID = SDL_GetWindowID(window_.get());
            m.motion.x = x;
            m.motion.y = y;
        } else if (type == SDL_EVENT_MOUSE_WHEEL) {
            m.wheel.windowID = SDL_GetWindowID(window_.get());
            m.wheel.mouse_x = x;
            m.wheel.mouse_y = y;
        } else {
            m.button.windowID = SDL_GetWindowID(window_.get());
            m.button.button = SDL_BUTTON_LEFT;
            m.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            m.button.clicks = 1;
            m.button.x = x;
            m.button.y = y;
        }
        return m;
    };
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN: {
        if (touch_.down) { // another finger
            touch_.second = true;
            return SDL_APP_CONTINUE;
        }
        touch_ = {.finger = e.tfinger.fingerID, .down = true, .x = x, .y = y, .startX = x, .startY = y, .scrolledY = y, .start = SDL_GetTicks()};
        SDL_Event hover = mouse(SDL_EVENT_MOUSE_MOTION);
        return event(hover);
    }
    case SDL_EVENT_FINGER_MOTION:
        if (!touch_.down || e.tfinger.fingerID != touch_.finger) return SDL_APP_CONTINUE;
        touch_.x = x;
        touch_.y = y;
        if (std::hypot(x - touch_.startX, y - touch_.startY) > static_cast<float>(std::min(ww, wh)) * 0.03f) touch_.moved = true;
        if (touch_.moved && !overlays_.empty()) // dragging scrolls lists in menus, a wheel step each 40 points
            for (const float step = 40.0f; std::abs(y - touch_.scrolledY) >= step; touch_.scrolledY += y > touch_.scrolledY ? step : -step) {
                SDL_Event wheel = mouse(SDL_EVENT_MOUSE_WHEEL);
                wheel.wheel.y = y > touch_.scrolledY ? 1.0f : -1.0f;
                event(wheel);
            }
        return SDL_APP_CONTINUE;
    default: { // up or cancelled
        if (!touch_.down || e.tfinger.fingerID != touch_.finger) return SDL_APP_CONTINUE;
        const Touch t = std::exchange(touch_, {});
        if (e.type == SDL_EVENT_FINGER_CANCELED || t.longPressed) return SDL_APP_CONTINUE;
        const Uint64 held = SDL_GetTicks() - t.start;
        if (t.second && !t.moved && held < 600) return gesture(KeyChord::Gesture::TwoFingerTap);
        if (!t.moved && held < 600) { // a tap: a click where it was
            SDL_Event down = mouse(SDL_EVENT_MOUSE_BUTTON_DOWN), up = mouse(SDL_EVENT_MOUSE_BUTTON_UP);
            event(down);
            return event(up);
        }
        const float dx = x - t.startX, dy = y - t.startY;
        if (!t.moved || held > 700 || !overlays_.empty()) return SDL_APP_CONTINUE;
        if (std::abs(dx) > std::abs(dy) && std::abs(dx) > static_cast<float>(ww) * 0.12f)
            return gesture(dx < 0 ? KeyChord::Gesture::SwipeLeft : KeyChord::Gesture::SwipeRight);
        if (std::abs(dy) > static_cast<float>(wh) * 0.12f) return gesture(dy < 0 ? KeyChord::Gesture::SwipeUp : KeyChord::Gesture::SwipeDown);
        return SDL_APP_CONTINUE;
    }
    }
}

SDL_AppResult Engine::gesture(KeyChord::Gesture g) {
    SDL_Event u{};
    u.type = gestureEventType();
    u.user.code = static_cast<Sint32>(g);
    u.user.windowID = SDL_GetWindowID(window_.get());
    return event(u);
}

// Sticks and triggers act as buttons: the left stick as the d-pad (for menus), the triggers as
// pad_lt / pad_rt, once pushed most of the way.
SDL_AppResult Engine::padAxis(const SDL_Event& e) {
    const auto axis = static_cast<std::size_t>(e.gaxis.axis);
    if (axis >= axes_.size()) return SDL_APP_CONTINUE;
    const float v = static_cast<float>(e.gaxis.value) / 32767.0f;
    int& was = axes_[axis];
    const int now = v > 0.6f ? 1 : v < -0.6f ? -1 : std::abs(v) < 0.3f ? 0 : was;
    if (now == was) return SDL_APP_CONTINUE;
    was = now;
    int button = -1;
    switch (e.gaxis.axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: button = now > 0 ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT : SDL_GAMEPAD_BUTTON_DPAD_LEFT; break;
    case SDL_GAMEPAD_AXIS_LEFTY: button = now > 0 ? SDL_GAMEPAD_BUTTON_DPAD_DOWN : SDL_GAMEPAD_BUTTON_DPAD_UP; break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: button = KeyChord::kLeftTrigger; break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: button = KeyChord::kRightTrigger; break;
    default: break;
    }
    if (now == 0 || button < 0) return SDL_APP_CONTINUE;
    SDL_Event press{};
    press.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    press.gbutton.which = e.gaxis.which;
    press.gbutton.button = static_cast<Uint8>(button);
    press.gbutton.down = true;
    return event(press);
}

SDL_AppResult Engine::event(SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        return touch(e);
    case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_Gamepad* pad = SDL_OpenGamepad(e.gdevice.which)) pads_.emplace_back(pad);
        return SDL_APP_CONTINUE;
    case SDL_EVENT_GAMEPAD_REMOVED:
        std::erase_if(pads_, [&](const GamepadPtr& pad) { return SDL_GetGamepadID(pad.get()) == e.gdevice.which; }); // (closed as it goes)
        return SDL_APP_CONTINUE;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        return padAxis(e);
    default:
        break;
    }
    SDL_ConvertEventToRenderCoordinates(renderer_.get(), &e);
    switch (e.type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        updatePixelScale();
        refreshWindowSizes();
        return SDL_APP_CONTINUE;
    case SDL_EVENT_WINDOW_RESIZED: // remember sizes the player drags the window to
        if (!(SDL_GetWindowFlags(window_.get()) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))) {
            settings_.windowWidth = applied_.windowWidth = e.window.data1;
            settings_.windowHeight = applied_.windowHeight = e.window.data2;
        }
        return SDL_APP_CONTINUE;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: // also covers the OS's own fullscreen button
        settings_.fullscreen = applied_.fullscreen = e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        return SDL_APP_CONTINUE;
    default:
        break;
    }
    auto done = [this] { return quit_ ? SDL_APP_SUCCESS : SDL_APP_CONTINUE; };
    auto pressed = [&](std::size_t i) {
        return std::ranges::any_of(controlKeys_[i], [&](const KeyChord& k) { return k.matches(e); });
    };

    // Developer mode: F10 opens and closes the developer tools, which take every key and click while open,
    // and F6 reloads the game (as it does on the error screen).
    if (developerMode() && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_F10) {
        if (noGame_) return done(); // (without a game there's nothing else to show)
        if (developer_.isOpen()) developer_.backToGame();
        else developer_.openDeveloper();
        return done();
    }
    if ((developerMode() || mode_ == Mode::Error) && !developer_.isOpen() && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_F6) {
        reloadRequested_ = true;
        return done();
    }
    if (developer_.isOpen()) {
        developer_.developerEvent(e);
        return done();
    }

    if (veil_ && (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_WHEEL ||
                  e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN))
        return done(); // (a screen fading in)
    if (rebinding_ && captureKey(e)) return done(); // the Controls screen is waiting for a key

    // Controls with everywhere=yes work on every screen (fullscreen, screenshot, reload...).
    auto works = [&](std::size_t i) { return !script_.controls[i].condition || script_.controls[i].condition->test(lookup()); }; // (if=)
    for (std::size_t i = 0; i < script_.controls.size(); ++i)
        if (script_.controls[i].everywhere && pressed(i) && works(i)) {
            run(script_.controls[i].actions);
            return done();
        }

    if (mode_ == Mode::Error) {
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_ESCAPE) quit_ = true;
        return done();
    }

    // A button pressed on a screen is clicked (or not) when the mouse comes up: that screen takes it.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
        for (auto& [name, v] : views_)
            if (v.pressing()) {
                v.handleEvent(e);
                return done();
            }

    // Open menus, top first. Copy the list: buttons may open or close screens.
    const std::vector<std::string> open = overlays_;
    for (const auto& name : open | std::views::reverse)
        if (ScreenView* v = view(name); v && (v->handleEvent(e) || v->modal())) return done();

    if (mode_ == Mode::Title) {
        if (ScreenView* title = view("title")) title->handleEvent(e);
        return done();
    }
    if (game_->uiHidden()) {
        if (isPress(e)) game_->setUiHidden(false); // any key or click brings the UI back
        return done();
    }
    if (ScreenView* v = view(game_->choiceScreen()); v && game_->showingChoices() && v->handleEvent(e)) return done();
    if (ScreenView* v = textbox(); v && game_->showingSay() && v->handleEvent(e)) return done();
    const std::vector<std::string> shown = game_->shownScreens();
    for (const auto& name : shown | std::views::reverse)
        if (ScreenView* v = view(name); v && (v->handleEvent(e) || v->modal())) return done();
    if (ScreenView* v = view("hud"); v && v->handleEvent(e)) return done();
    for (std::size_t i = 0; i < script_.controls.size(); ++i)
        if (!script_.controls[i].everywhere && pressed(i) && works(i)) {
            run(script_.controls[i].actions);
            break;
        }
    return done();
}

SDL_AppResult Engine::iterate() {
    // While the developer tools are open, the first click on the window, with another program in front
    // (picking files in Finder...), clicks what it's on too: it doesn't only bring the window forward.
    if (developer_.isOpen() != clickThrough_) {
        clickThrough_ = developer_.isOpen();
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, clickThrough_ ? "1" : "0");
    }
    if (std::exchange(reloadRequested_, false)) {
        developer_.dropPreviews(); // (its game uses the pictures let go of)
        reload();
        developer_.reloaded();
    }
    developer_.updateDeveloper();
    const Uint64 now = SDL_GetTicksNS();
    const float frame = std::min(0.1f, static_cast<float>(now - lastTicks_) / 1e9f);
    lastTicks_ = now;
    toolsTime_ += frame;
    // While the developer tools are open over a game (F10), it stands still: its clock, its timers,
    // its sounds and videos, and the story.
    if (const bool paused = developer_.isOpen() && !noGame_ && !waiting_; paused != paused_) {
        paused_ = paused;
        audio_.holdGame(paused);
    }
    const float dt = paused_ ? 0.0f : frame;
    time_ += dt;
    noticeTime_ -= dt;
    if (noticeTime_ <= 0.0f && !noticeQueue_.empty()) { // the next message waiting
        notice_ = std::move(noticeQueue_.front());
        noticeQueue_.pop_front();
        noticeTime_ = config().notifyTime;
    }
    audio_.update(dt);
    std::vector<std::pair<Commands, std::string>> pressed; // (buttons whose click animations are over: what they do; and what chosen ones set)
    for (auto& [name, v] : views_) {
        if (auto did = v.takePressed()) pressed.push_back(std::move(*did));
        if (Commands kept = v.takeChosen(); !kept.empty()) pressed.emplace_back(std::move(kept), std::string{});
    }
    for (const auto& [commands, confirm] : pressed) run(commands, confirm);
    if (veil_) { // (the screen opens at the last fade's colour)
        veil_->time += dt;
        if (veil_->then && veil_->time >= veil_->opensAt()) std::exchange(veil_->then, {})();
        if (veil_ && veil_->time >= veil_->length()) veil_.reset();
    }

    assets_->beginFrame();
    ++frameNumber_;
    // A menu covers the story and stops it (one with modal=no, like the phone, lets it go on).
    const bool covered = mode_ == Mode::Game && (developer_.isOpen() || std::ranges::any_of(overlays_, [&](const std::string& name) {
                                                     const ScreenDef* screen = script_.screen(name);
                                                     return !screen || screen->modal;
                                                 }));
    if (covered != covered_) {
        covered_ = covered;
        game_->setCovered(covered); // its voice line and video wait under menus
        if (!covered) audio_.stopReplay(); // a voice heard again from the history stops with it
    }
    if (touch_.down && !touch_.moved && !touch_.second && !touch_.longPressed && SDL_GetTicks() - touch_.start >= 600) {
        touch_.longPressed = true; // a finger held still: a long press
        gesture(KeyChord::Gesture::LongPress);
    }
    if (mode_ == Mode::Game) {
        game_->updatePhone(dt); // (answers are typed while the phone is open too)
        if (!game_->hasPhone()) // "phone off": screens showing it close
            for (std::size_t i = 0; i < overlays_.size(); ++i)
                if (const ScreenView* v = view(overlays_[i]); v && std::ranges::any_of(v->def().elements, [](const Element& el) {
                        return el.kind == ElementKind::Phone || el.kind == ElementKind::Contacts;
                    })) {
                    while (overlays_.size() > i) close(false);
                    break;
                }
        // Loads one of the pictures the story is about to show, so it doesn't stop to load it then.
        for (const auto& [name, looks] : game_->upcomingPictures(config().preload))
            if (assets_->preload(name, looks)) break;
        if (!covered) {
            game_->update(dt);
            developer_.recordReached();
            if (game_->takeAutosaveRequest() && !splash_ && !opening_) autosave();
        }
        for (const auto& request : game_->takeRequests()) ui(request); // notify, open... from the story
    }
    if (mode_ == Mode::Game) {
        if (game_->takeScreenRequest()) { // "open", "ask", a chapter's title card: show it and wait
            const ScreenRequest& request = game_->request();
            if (!request.prompt.empty()) question_ = request.prompt;
            const Game* asked = game_.get();
            if (request.name == "title") { // the story opens the title screen: New Game goes on from the next line
                opening_ = true; // (not saved: Continue doesn't come back to it)
                veiled(request.transition, [this, asked, start = game_->here() + 1, closing = request.closing] {
                    if (game_.get() != asked || mode_ != Mode::Game) return;
                    storyStart_ = start;
                    beforeTitle_ = game_->variables();
                    openTitle(true);
                    if (closing.kind == TransitionKind::Fade) titleClosing_ = closing;
                });
            } else {
                veiled(request.transition, [this, asked, name = request.name, closing = request.closing] {
                    if (game_.get() != asked || mode_ != Mode::Game) return;
                    if (!open(name)) return game_->screenClosed(std::nullopt);
                    calledDepth_ = overlays_.size();
                    if (closing.kind == TransitionKind::Fade) closingFades_.insert_or_assign(name, closing);
                });
            }
        }
        if (mode_ == Mode::Game) syncShownScreens();
        if (mode_ == Mode::Game && game_->finished()) {
            if (!game_->error().empty()) showErrors({game_->error()});
            else if (splash_) beginStory(); // the splash screen's "return"
            else openTitle();
        }
    }
    if (game_->takePersistentChanged()) savePersistent();
    if (chart_.takeChanged() && !prefPath_.empty() && !chart_.save(prefPath_ + "chart.txt"))
        logWarn("Can't save the flowchart: {}.", SDL_GetError());
    // Screens whose hide_animation= is over: closed now (and the story goes on, if it waited).
    for (std::size_t i = 0; i < leaving_.size();) {
        const ScreenView* v = view(leaving_[i].name);
        if (v && v->hasLeft() && v->leaving()) {
            ++i;
            continue;
        }
        auto then = std::move(leaving_[i].then);
        leaving_.erase(leaving_.begin() + static_cast<std::ptrdiff_t>(i));
        if (then && v && v->hasLeft()) then(); // (not if it opened again meanwhile)
    }
    // A screen with close_after= closes by itself (a chapter's title card...).
    if (const std::string top = overlays_.empty() ? std::string() : overlays_.back(); top != topScreen_) {
        topScreen_ = top;
        topSince_ = time_;
    } else if (const ScreenDef* def = top.empty() ? nullptr : script_.screen(top); def && def->closeAfter > 0.0f && time_ - topSince_ >= def->closeAfter && !veil_) {
        close();
    }
    if (musicBeforeSongs_ && !songsOpen()) { // the music room closed: back to what played before
        if (mode_ == Mode::Game && !game_->music().empty()) audio_.playMusic(assets_->audio(game_->music()), true, 0.5f, game_->musicVolume());
        else if (musicBeforeSongs_->empty()) audio_.stopMusic(0.5f);
        else audio_.playMusic(*musicBeforeSongs_, true, 0.5f);
        musicBeforeSongs_.reset();
    }

    readAloud();

    SDL_Renderer* r = renderer_.get();
    const SDL_Color bars = config().letterboxColor;
    SDL_SetRenderDrawColor(r, bars.r, bars.g, bars.b, 255);
    SDL_RenderClear(r);
    textInputWanted_ = false; // input fields drawn below ask for it again
    switch (mode_) {
    case Mode::Title:
        if (ScreenView* title = view("title"); title && !waiting_) title->draw(r);
        break;
    case Mode::Game:
        drawGame(false);
        break;
    case Mode::Error:
        drawErrors();
        break;
    }
    for (const auto& name : overlays_)
        if (ScreenView* v = view(name)) v->draw(r);
    for (const Leaving& l : leaving_) // ("show screen" ones hidden, going: the others are drawn where they are)
        if (ScreenView* v = !l.then && !std::ranges::contains(overlays_, l.name) ? view(l.name) : nullptr) v->draw(r);
    if (veil_) draw::rect(r, {0.0f, 0.0f, canvas_.w, canvas_.h}, veil_->color());
    if (ScreenView* v = view("notify"); v && noticeTime_ > 0.0f) v->draw(r, std::min(1.0f, noticeTime_ * 2.0f));
    if (developer_.isOpen()) developer_.drawDeveloper();
    if (textInputWanted_ != textInputActive_) { // on-screen keyboards / IME only while typing
        textInputActive_ = textInputWanted_;
        if (textInputActive_) SDL_StartTextInput(window_.get());
        else SDL_StopTextInput(window_.get());
    }

    for (auto& [name, v] : views_) v.endFrame(frameNumber_); // videos of screens no longer shown stop

    // The frame can only be read before it is presented.
    if (!screenshotRequest_.empty()) screenshot(std::exchange(screenshotRequest_, {}));
    if (!screenshotPath_.empty() && ++frames_ == 30) {
        screenshot(screenshotPath_);
        return SDL_APP_SUCCESS;
    }
    SDL_RenderPresent(r);
    return quit_ ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

} // namespace vn

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
