// What Linux offers the player, where the player's system has it: spelling (Hunspell, with the
// dictionaries in /usr/share/hunspell), reading text aloud (speech-dispatcher) and videos other than
// MPEG-1 (GStreamer: MP4, MOV and M4V, with the plugins for them). Each library is looked for once by
// its name; without it the player does as on any system without one (System.hpp says what each does;
// Apple.mm and Windows.cpp are the Mac's and Windows' own). Nothing here needs those libraries' headers.
#include "System.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vn {

namespace {

// A shared library, opened by the first of its names that's there (none of them: it isn't), and closed
// when this goes; its functions are looked up by their names.
class Library {
public:
    Library(std::initializer_list<const char*> names) {
        for (const char* name : names)
            if ((handle_ = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
    }
    ~Library() {
        if (handle_) dlclose(handle_);
    }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    Library(Library&&) = delete;
    Library& operator=(Library&&) = delete;
    // A function by its name (none: no such function, or no library).
    template <class F>
    [[nodiscard]] F function(const char* name) const {
        static_assert(sizeof(F) == sizeof(void*));
        F f = nullptr;
        if (void* found = handle_ ? dlsym(handle_, name) : nullptr) std::memcpy(&f, &found, sizeof f); // (an object pointer to a function: POSIX's way)
        return f;
    }

private:
    void* handle_ = nullptr;
};

// A file of its own in the temporary folder (a video from a pack, for the player library to open), deleted
// when this goes.
class TempFile {
public:
    TempFile() = default;
    ~TempFile() {
        if (!path_.empty()) unlink(path_.c_str());
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;
    // `bytes` in it, its name ending in `ending` (".mp4": what it is goes by its ending).
    bool write(const std::string& bytes, std::string_view ending) {
        const char* folder = std::getenv("TMPDIR");
        std::string name = std::string(folder && *folder ? folder : "/tmp") + "/vnvideoXXXXXX" + std::string(ending);
        const int fd = mkstemps(name.data(), static_cast<int>(ending.size()));
        if (fd < 0) return false;
        path_ = name;
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto n = ::write(fd, bytes.data() + done, bytes.size() - done);
            if (n <= 0) break;
            done += static_cast<std::size_t>(n);
        }
        close(fd);
        return done == bytes.size();
    }
    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::string path_;
};

// The user's language as SDL has it ("en", "en_US"): what an empty language means.
std::string ownLanguage() {
    int count = 0;
    if (const SdlOwned<SDL_Locale*> locales{SDL_GetPreferredLocales(&count)}; locales && count > 0 && locales.get()[0]->language) {
        const SDL_Locale& first = *locales.get()[0];
        return first.country ? std::string(first.language) + "_" + first.country : std::string(first.language);
    }
    return "en";
}

// ---------------------------------------------------------------------------
// Spelling: Hunspell
// ---------------------------------------------------------------------------

struct Hunhandle; // (Hunspell's own)

class Hunspell {
public:
    static Hunspell& get() {
        static Hunspell one;
        return one;
    }
    Hunspell(const Hunspell&) = delete;
    Hunspell& operator=(const Hunspell&) = delete;
    Hunspell(Hunspell&&) = delete;
    Hunspell& operator=(Hunspell&&) = delete;
    ~Hunspell() = default;

    // The dictionary for a language ("en", "es", "en_US"...; empty: the user's), opened once: none when the
    // system has none for it (or no Hunspell).
    Hunhandle* checker(std::string_view language) {
        if (!create_ || !destroy_ || !spell_) return nullptr;
        std::string want(language.empty() ? ownLanguage() : std::string(language));
        std::ranges::replace(want, '-', '_');
        if (const auto known = open_.find(want); known != open_.end()) return known->second.get();
        Owned& made = open_[want];
        for (const char* folder : {"/usr/share/hunspell/", "/usr/share/myspell/", "/usr/share/myspell/dicts/"}) {
            std::string pick; // (the language itself, else its first country: en is en_US, say)
            for (const std::string& file : listFiles(folder, ".dic")) {
                const std::string stem = file.substr(0, file.size() - 4);
                if (lower(stem) == lower(want)) {
                    pick = stem;
                    break;
                }
                if (pick.empty() && lower(stem).starts_with(lower(want) + "_")) pick = stem;
            }
            if (pick.empty()) continue;
            const std::string base = folder + pick;
            made = Owned(create_((base + ".aff").c_str(), (base + ".dic").c_str()), Destroy{destroy_});
            if (made) break;
        }
        return made.get();
    }
    bool right(Hunhandle* h, std::string_view word) { return spell_(h, written(h, word).c_str()) != 0; }
    std::vector<std::string> suggestions(Hunhandle* h, std::string_view word) {
        std::vector<std::string> out;
        if (!suggest_ || !freeList_) return out;
        char** list = nullptr;
        const int n = suggest_(h, &list, written(h, word).c_str());
        const ScopeExit _([&] { freeList_(h, &list, n); });
        for (int i = 0; i < n; ++i) out.push_back(read(h, list[i]));
        return out;
    }
    void learn(std::string_view word) { // (to each dictionary open: for this run of the player)
        if (!add_) return;
        for (const auto& [language, h] : open_)
            if (h) add_(h.get(), written(h.get(), word).c_str());
    }

private:
    using Create = Hunhandle* (*)(const char*, const char*);
    using Free = void (*)(Hunhandle*);
    using Spell = int (*)(Hunhandle*, const char*);
    using Suggest = int (*)(Hunhandle*, char***, const char*);
    using FreeList = void (*)(Hunhandle*, char***, int);
    using Add = int (*)(Hunhandle*, const char*);
    using Encoding = char* (*)(Hunhandle*);
    struct Destroy {
        Free destroy = nullptr;
        void operator()(Hunhandle* h) const noexcept {
            if (destroy && h) destroy(h);
        }
    };
    using Owned = std::unique_ptr<Hunhandle, Destroy>;

    Library library_{"libhunspell-1.7.so.0", "libhunspell-1.7.so", "libhunspell-1.6.so.0", "libhunspell.so"};
    Create create_ = library_.function<Create>("Hunspell_create");
    Free destroy_ = library_.function<Free>("Hunspell_destroy");
    Spell spell_ = library_.function<Spell>("Hunspell_spell");
    Suggest suggest_ = library_.function<Suggest>("Hunspell_suggest");
    FreeList freeList_ = library_.function<FreeList>("Hunspell_free_list");
    Add add_ = library_.function<Add>("Hunspell_add");
    Encoding encoding_ = library_.function<Encoding>("Hunspell_get_dic_encoding");
    std::map<std::string, Owned, std::less<>> open_; // (after the library: they go first)

    Hunspell() = default;

    // A word as the dictionary's encoding has it (most are UTF-8, as the player is), and back.
    [[nodiscard]] std::string converted(Hunhandle* h, std::string_view text, bool toDictionary) const {
        const char* encoding = encoding_ ? encoding_(h) : nullptr;
        if (!encoding || SDL_strcasecmp(encoding, "UTF-8") == 0) return std::string(text);
        const std::string in(text);
        const SdlOwned<char> out{SDL_iconv_string(toDictionary ? encoding : "UTF-8", toDictionary ? "UTF-8" : encoding, in.c_str(), in.size() + 1)};
        return out ? std::string(out.get()) : in;
    }
    [[nodiscard]] std::string written(Hunhandle* h, std::string_view word) const { return converted(h, word, true); }
    [[nodiscard]] std::string read(Hunhandle* h, const char* word) const { return converted(h, word, false); }
};

} // namespace

bool spelledRight(std::string_view word, std::string_view language) {
    Hunspell& hunspell = Hunspell::get();
    Hunhandle* h = word.empty() ? nullptr : hunspell.checker(language);
    return !h || hunspell.right(h, word);
}

std::vector<std::string> spellings(std::string_view word, std::string_view language) {
    Hunspell& hunspell = Hunspell::get();
    Hunhandle* h = hunspell.checker(language);
    return h ? hunspell.suggestions(h, word) : std::vector<std::string>{};
}

void learnSpelling(std::string_view word) {
    if (!word.empty()) Hunspell::get().learn(word);
}

// ---------------------------------------------------------------------------
// Reading aloud: speech-dispatcher
// ---------------------------------------------------------------------------

namespace {

struct SPDConnection; // (speech-dispatcher's own)

class SpeechDispatcher {
public:
    static SpeechDispatcher& get() {
        static SpeechDispatcher one;
        return one;
    }
    SpeechDispatcher(const SpeechDispatcher&) = delete;
    SpeechDispatcher& operator=(const SpeechDispatcher&) = delete;
    SpeechDispatcher(SpeechDispatcher&&) = delete;
    SpeechDispatcher& operator=(SpeechDispatcher&&) = delete;
    ~SpeechDispatcher() = default;

    void speak(std::string_view words) {
        if (!connection_ || !cancel_) return;
        cancel_(connection_.get()); // (what it was reading stops)
        if (!words.empty() && say_) say_(connection_.get(), kText, std::string(words).c_str());
    }

private:
    using Open = SPDConnection* (*)(const char*, const char*, const char*, int);
    using Say = int (*)(SPDConnection*, int, const char*);
    using Cancel = int (*)(SPDConnection*);
    using Close = void (*)(SPDConnection*);
    struct Closer {
        Close close = nullptr;
        void operator()(SPDConnection* c) const noexcept {
            if (close && c) close(c);
        }
    };
    static constexpr int kSingle = 0; // SPD_MODE_SINGLE
    static constexpr int kText = 3;   // SPD_TEXT: text to read, after anything more pressing

    Library library_{"libspeechd.so.2", "libspeechd.so"};
    Open open_ = library_.function<Open>("spd_open");
    Say say_ = library_.function<Say>("spd_say");
    Cancel cancel_ = library_.function<Cancel>("spd_cancel");
    std::unique_ptr<SPDConnection, Closer> connection_{open_ ? open_("VNext", "main", nullptr, kSingle) : nullptr, Closer{library_.function<Close>("spd_close")}};

    SpeechDispatcher() = default;
};

} // namespace

void speak(std::string_view words) {
    SpeechDispatcher::get().speak(words);
}

// ---------------------------------------------------------------------------
// Videos: GStreamer
// ---------------------------------------------------------------------------

namespace {

// GStreamer's own, as its headers have them (they stay the same through 1.x).
struct GstElement;
struct GstSample;
struct GstBuffer;
struct GstCaps;
struct GstStructure;
struct GstMapInfo {
    void* memory = nullptr;
    int flags = 0;
    std::uint8_t* data = nullptr;
    std::size_t size = 0, maxsize = 0;
    void* user_data[4]{};
    void* reserved[4]{};
};

class GStreamer {
public:
    static GStreamer& get() {
        static GStreamer one;
        return one;
    }
    GStreamer(const GStreamer&) = delete;
    GStreamer& operator=(const GStreamer&) = delete;
    GStreamer(GStreamer&&) = delete;
    GStreamer& operator=(GStreamer&&) = delete;
    ~GStreamer() = default;

    static constexpr int kNull = 1, kPaused = 3, kPlaying = 4;     // GstState
    static constexpr int kTime = 3;                                // GST_FORMAT_TIME
    static constexpr int kFlush = 1 << 0, kKeyUnit = 1 << 2;       // GstSeekFlags
    static constexpr int kRead = 1 << 0;                           // GST_MAP_READ
    static constexpr std::uint64_t kSecond = 1'000'000'000;        // a second, in its nanoseconds
    static constexpr int kFailure = 0;                             // GST_STATE_CHANGE_FAILURE

    using Init = int (*)(int*, char***, void**);
    using ParseLaunch = GstElement* (*)(const char*, void**);
    using FilenameToUri = char* (*)(const char*, void**);
    using GFree = void (*)(void*);
    using ByName = GstElement* (*)(GstElement*, const char*);
    using SetState = int (*)(GstElement*, int);
    using GetState = int (*)(GstElement*, int*, int*, std::uint64_t);
    using QueryDuration = int (*)(GstElement*, int, std::int64_t*);
    using SeekSimple = int (*)(GstElement*, int, int, std::int64_t);
    using Unref = void (*)(void*);
    using Pull = GstSample* (*)(GstElement*, std::uint64_t);
    using IsEos = int (*)(GstElement*);
    using SampleBuffer = GstBuffer* (*)(GstSample*);
    using SampleCaps = GstCaps* (*)(GstSample*);
    using CapsStructure = GstStructure* (*)(const GstCaps*, unsigned);
    using StructureInt = int (*)(const GstStructure*, const char*, int*);
    using StructureFraction = int (*)(const GstStructure*, const char*, int*, int*);
    using Map = int (*)(GstBuffer*, GstMapInfo*, int);
    using Unmap = void (*)(GstBuffer*, GstMapInfo*);

    Library glib{"libglib-2.0.so.0"};
    Library gst{"libgstreamer-1.0.so.0"};
    Library app{"libgstapp-1.0.so.0"};
    GFree gFree = glib.function<GFree>("g_free");
    Init init = gst.function<Init>("gst_init_check");
    ParseLaunch parseLaunch = gst.function<ParseLaunch>("gst_parse_launch");
    FilenameToUri filenameToUri = gst.function<FilenameToUri>("gst_filename_to_uri");
    ByName byName = gst.function<ByName>("gst_bin_get_by_name");
    SetState setState = gst.function<SetState>("gst_element_set_state");
    GetState getState = gst.function<GetState>("gst_element_get_state");
    QueryDuration queryDuration = gst.function<QueryDuration>("gst_element_query_duration");
    SeekSimple seekSimple = gst.function<SeekSimple>("gst_element_seek_simple");
    Unref unref = gst.function<Unref>("gst_object_unref");
    Unref sampleUnref = gst.function<Unref>("gst_mini_object_unref");
    Pull pullSample = app.function<Pull>("gst_app_sink_try_pull_sample");
    Pull pullPreroll = app.function<Pull>("gst_app_sink_try_pull_preroll");
    IsEos isEos = app.function<IsEos>("gst_app_sink_is_eos");
    SampleBuffer sampleBuffer = gst.function<SampleBuffer>("gst_sample_get_buffer");
    SampleCaps sampleCaps = gst.function<SampleCaps>("gst_sample_get_caps");
    CapsStructure capsStructure = gst.function<CapsStructure>("gst_caps_get_structure");
    StructureInt structureInt = gst.function<StructureInt>("gst_structure_get_int");
    StructureFraction structureFraction = gst.function<StructureFraction>("gst_structure_get_fraction");
    Map map = gst.function<Map>("gst_buffer_map");
    Unmap unmap = gst.function<Unmap>("gst_buffer_unmap");
    // Whether every one of them is there, and GStreamer started.
    const bool ready = gFree && init && parseLaunch && filenameToUri && byName && setState && getState && queryDuration && seekSimple && unref &&
                       sampleUnref && pullSample && pullPreroll && isEos && sampleBuffer && sampleCaps && capsStructure && structureInt &&
                       structureFraction && map && unmap && init(nullptr, nullptr, nullptr) != 0;

private:
    GStreamer() = default;
};

// Owners of GStreamer's objects: a pipeline (stopped first), an element, a sample.
struct PipelineStop {
    void operator()(GstElement* p) const noexcept {
        GStreamer& g = GStreamer::get();
        g.setState(p, GStreamer::kNull);
        g.unref(p);
    }
};
struct ElementUnref {
    void operator()(GstElement* e) const noexcept { GStreamer::get().unref(e); }
};
struct SampleUnref {
    void operator()(GstSample* s) const noexcept { GStreamer::get().sampleUnref(s); }
};
using Pipeline = std::unique_ptr<GstElement, PipelineStop>;
using Element = std::unique_ptr<GstElement, ElementUnref>;
using Sample = std::unique_ptr<GstSample, SampleUnref>;

class LinuxVideo final : public SystemVideo {
public:
    bool start(const std::string& path, const std::string* data) {
        GStreamer& g = GStreamer::get();
        if (!g.ready) return false;
        std::string file = path;
        if (data) { // (one in a pack: its bytes in a file of their own while it plays)
            const auto dot = path.rfind('.');
            if (!temp_.write(*data, dot == std::string::npos ? std::string_view{} : std::string_view(path).substr(dot))) return false;
            file = temp_.path();
        }
        char* uri = g.filenameToUri(file.c_str(), nullptr);
        if (!uri) return false;
        const ScopeExit _([&] { g.gFree(uri); });
        // Pictures as blue, green, red and alpha; sound as two sides at kSoundRate, each read as soon
        // as it's asked for (not at its time: the player times them). A few pictures wait at most.
        const std::string description =
            std::string("playbin uri=\"") + uri +
            "\" video-sink=\"videoconvert ! video/x-raw,format=BGRA ! appsink name=pictures sync=false max-buffers=4\" "
            "audio-sink=\"audioconvert ! audioresample ! audio/x-raw,format=F32LE,channels=2,rate=48000,layout=interleaved ! appsink name=sound sync=false\"";
        pipeline_ = Pipeline(g.parseLaunch(description.c_str(), nullptr));
        if (!pipeline_) return false;
        pictures_ = Element(g.byName(pipeline_.get(), "pictures"));
        sound_ = Element(g.byName(pipeline_.get(), "sound"));
        if (!pictures_ || g.setState(pipeline_.get(), GStreamer::kPaused) == GStreamer::kFailure) return false;
        int state = 0, pending = 0;
        if (g.getState(pipeline_.get(), &state, &pending, 5 * GStreamer::kSecond) == GStreamer::kFailure) return false;
        const Sample first{g.pullPreroll(pictures_.get(), 5 * GStreamer::kSecond)};
        if (!first) return false;
        int w = 0, h = 0, num = 0, den = 1;
        if (const GstStructure* s = g.capsStructure(g.sampleCaps(first.get()), 0)) {
            g.structureInt(s, "width", &w);
            g.structureInt(s, "height", &h);
            if (!g.structureFraction(s, "framerate", &num, &den) || num <= 0 || den <= 0) num = 30, den = 1;
        }
        if (w <= 0 || h <= 0) return false;
        frameTime_ = static_cast<double>(den) / num;
        const Sample heard{sound_ ? g.pullPreroll(sound_.get(), GStreamer::kSecond / 2) : nullptr};
        hasSound_ = static_cast<bool>(heard);
        std::int64_t length = 0;
        const double seconds = g.queryDuration(pipeline_.get(), GStreamer::kTime, &length) && length > 0 ? static_cast<double>(length) / 1e9 : 0.0;
        describe({.width = w, .height = h, .duration = seconds, .sound = hasSound_});
        width_ = w;
        height_ = h;
        return g.setState(pipeline_.get(), GStreamer::kPlaying) != GStreamer::kFailure;
    }

    bool nextPicture(std::vector<std::uint8_t>& pixels, int& pitch, double& time) override {
        GStreamer& g = GStreamer::get();
        const Sample sample{g.pullSample(pictures_.get(), 2 * GStreamer::kSecond)};
        if (!sample) return false; // (the end, or nothing came)
        GstBuffer* buffer = g.sampleBuffer(sample.get());
        GstMapInfo bytes;
        if (!buffer || !g.map(buffer, &bytes, GStreamer::kRead)) return false;
        const ScopeExit _([&] { g.unmap(buffer, &bytes); });
        pitch = width_ * 4;
        const std::size_t size = static_cast<std::size_t>(pitch) * static_cast<std::size_t>(height_);
        if (bytes.size < size) return false;
        pixels.assign(bytes.data, bytes.data + size);
        time = static_cast<double>(frames_++) * frameTime_; // (each picture's time, by the frame rate)
        return true;
    }

    bool nextSound(std::vector<float>& samples) override {
        samples.clear();
        if (!hasSound_) return false;
        GStreamer& g = GStreamer::get();
        const Sample sample{g.pullSample(sound_.get(), 2 * GStreamer::kSecond)};
        if (!sample) return false;
        GstBuffer* buffer = g.sampleBuffer(sample.get());
        GstMapInfo bytes;
        if (!buffer || !g.map(buffer, &bytes, GStreamer::kRead)) return false;
        const ScopeExit _([&] { g.unmap(buffer, &bytes); });
        samples.resize(bytes.size / sizeof(float));
        std::memcpy(samples.data(), bytes.data, samples.size() * sizeof(float));
        return true;
    }

    void restart() override {
        GStreamer::get().seekSimple(pipeline_.get(), GStreamer::kTime, GStreamer::kFlush | GStreamer::kKeyUnit, 0);
        frames_ = 0;
    }

private:
    TempFile temp_;       // (before the pipeline: it goes after it, once the file's closed)
    Pipeline pipeline_;   // (before its parts: they go first)
    Element pictures_, sound_;
    int width_ = 0, height_ = 0;
    double frameTime_ = 1.0 / 30;
    std::uint64_t frames_ = 0;
    bool hasSound_ = false;
};

} // namespace

std::unique_ptr<SystemVideo> SystemVideo::open(const std::string& path, const std::string* data) {
    auto video = std::make_unique<LinuxVideo>();
    if (!video->start(path, data)) return nullptr;
    return video;
}

} // namespace vn
