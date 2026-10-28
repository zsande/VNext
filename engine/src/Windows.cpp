// What Windows offers the player through its own parts: spelling (its spell checker), reading text
// aloud (SAPI) and videos other than MPEG-1 (Media Foundation: MP4, MOV and M4V). System.hpp says what
// each does; Apple.mm is the Mac's.
#ifndef _WIN32_WINNT // (Windows 10 on: what the spell checker and the video reader need)
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "System.hpp"

#include <windows.h>
#include <objbase.h>
#include <sapi.h>
#include <spellcheck.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vn {

namespace {

// A COM object, released when it goes.
template <class T>
class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& other) noexcept : p_(std::exchange(other.p_, nullptr)) {}
    Com& operator=(Com&& other) noexcept {
        if (this != &other) {
            reset();
            p_ = std::exchange(other.p_, nullptr);
        }
        return *this;
    }
    T* operator->() const { return p_; }
    [[nodiscard]] T* get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** put() { // (for a call that gives one back)
        reset();
        return &p_;
    }
    void reset() {
        if (p_) std::exchange(p_, nullptr)->Release();
    }

private:
    T* p_ = nullptr;
};

// The classes asked for by their ids (the interfaces by __uuidof, through IID_PPV_ARGS).
constexpr GUID kSpellCheckerFactory{0x7ab36653, 0x1796, 0x484b, {0xbd, 0xfa, 0xe7, 0x4f, 0x1d, 0xb7, 0xc1, 0xdc}};
constexpr GUID kSpVoice{0x96749377, 0x3391, 0x11d2, {0x9e, 0xe3, 0x00, 0xc0, 0x4f, 0x79, 0x73, 0x96}};

// COM for this thread, once (SDL may have started it already, as it likes: that does too).
bool comReady() {
    static const bool ready = [] {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }();
    return ready;
}

std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string fromWide(const wchar_t* s) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// A string COM gave (CoTaskMemAlloc'd), freed when it goes.
struct TaskString {
    LPOLESTR s = nullptr;
    TaskString() = default;
    ~TaskString() { CoTaskMemFree(s); }
    TaskString(const TaskString&) = delete;
    TaskString& operator=(const TaskString&) = delete;
    TaskString(TaskString&&) = delete;
    TaskString& operator=(TaskString&&) = delete;
};

// ---------------------------------------------------------------------------
// Spelling
// ---------------------------------------------------------------------------

// The spell checker for a language ("en", "es"...; empty: the user's own), made once: Windows' tag for it
// ("en" is the first "en-..." it has), or none when Windows has no dictionary for it.
ISpellChecker* checkerFor(std::string_view language) {
    static std::map<std::string, Com<ISpellChecker>, std::less<>> checkers;
    if (const auto known = checkers.find(language); known != checkers.end()) return known->second.get();
    Com<ISpellChecker>& checker = checkers[std::string(language)];
    Com<ISpellCheckerFactory> factory;
    if (!comReady() || FAILED(CoCreateInstance(kSpellCheckerFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())))) return nullptr;
    std::wstring want = wide(language);
    if (want.empty()) {
        wchar_t own[LOCALE_NAME_MAX_LENGTH]{};
        if (GetUserDefaultLocaleName(own, LOCALE_NAME_MAX_LENGTH) > 0) want = own;
    }
    std::wstring tag;
    Com<IEnumString> tags;
    if (SUCCEEDED(factory->get_SupportedLanguages(tags.put())))
        for (;;) {
            TaskString one;
            if (tags->Next(1, &one.s, nullptr) != S_OK) break;
            const std::wstring have = one.s;
            if (_wcsicmp(have.c_str(), want.c_str()) == 0) {
                tag = have;
                break;
            }
            if (tag.empty() && have.size() > want.size() && _wcsnicmp(have.c_str(), want.c_str(), want.size()) == 0 && have[want.size()] == L'-') tag = have;
        }
    BOOL supported = FALSE;
    if (tag.empty() || FAILED(factory->IsSupported(tag.c_str(), &supported)) || !supported) return nullptr;
    if (FAILED(factory->CreateSpellChecker(tag.c_str(), checker.put()))) return nullptr;
    return checker.get();
}

} // namespace

bool spelledRight(std::string_view word, std::string_view language) {
    ISpellChecker* checker = checkerFor(language);
    if (!checker || word.empty()) return true;
    Com<IEnumSpellingError> errors;
    if (FAILED(checker->Check(wide(word).c_str(), errors.put())) || !errors) return true;
    Com<ISpellingError> error;
    return errors->Next(error.put()) != S_OK;
}

std::vector<std::string> spellings(std::string_view word, std::string_view language) {
    std::vector<std::string> out;
    ISpellChecker* checker = checkerFor(language);
    Com<IEnumString> list;
    if (!checker || FAILED(checker->Suggest(wide(word).c_str(), list.put())) || !list) return out;
    for (;;) {
        TaskString one;
        if (list->Next(1, &one.s, nullptr) != S_OK) break;
        out.push_back(fromWide(one.s));
    }
    return out;
}

void learnSpelling(std::string_view word) {
    if (ISpellChecker* checker = checkerFor(""); checker && !word.empty()) checker->Add(wide(word).c_str()); // (the user's own dictionary)
}

// ---------------------------------------------------------------------------
// Reading aloud
// ---------------------------------------------------------------------------

void speak(std::string_view words) {
    static Com<ISpVoice> voice = [] {
        Com<ISpVoice> made;
        if (comReady()) CoCreateInstance(kSpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(made.put()));
        return made;
    }();
    if (!voice) return;
    if (words.empty()) {
        voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr); // (it just stops)
        return;
    }
    voice->Speak(wide(words).c_str(), static_cast<DWORD>(SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML), nullptr);
}

// ---------------------------------------------------------------------------
// Videos
// ---------------------------------------------------------------------------

namespace {

bool mediaReady() { // (Media Foundation, once)
    static const bool ready = comReady() && SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    return ready;
}

// A file of its own in the system's temporary folder, deleted when this goes.
class TempFile {
public:
    TempFile() = default;
    ~TempFile() {
        if (!path_.empty()) DeleteFileW(path_.c_str());
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;
    // `bytes` in it, its name ending in `ending` (".mp4": what it is goes by its ending).
    bool write(const std::string& bytes, std::string_view ending) {
        wchar_t folder[MAX_PATH + 1]{}, name[MAX_PATH + 1]{};
        const DWORD n = GetTempPathW(MAX_PATH + 1, folder);
        if (n == 0 || n > MAX_PATH || GetTempFileNameW(folder, L"vnv", 0, name) == 0) return false;
        DeleteFileW(name); // (its name is ours now: with the ending)
        path_ = std::wstring(name) + wide(ending);
        const IOStreamPtr out{SDL_IOFromFile(fromWide(path_.c_str()).c_str(), "wb")};
        return out && SDL_WriteIO(out.get(), bytes.data(), bytes.size()) == bytes.size();
    }
    [[nodiscard]] const std::wstring& path() const { return path_; }

private:
    std::wstring path_;
};

class WindowsVideo final : public SystemVideo {
public:
    bool start(const std::string& path, const std::string* data) {
        if (!mediaReady()) return false;
        Com<IMFAttributes> options;
        if (FAILED(MFCreateAttributes(options.put(), 1)) || FAILED(options->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE))) return false;
        std::wstring file = wide(path);
        if (data) { // (one in a pack: its bytes in a file of their own while it plays, as the reader reads files)
            const auto dot = path.rfind('.');
            if (!temp_.write(*data, dot == std::string::npos ? std::string_view{} : std::string_view(path).substr(dot))) return false;
            file = temp_.path();
        }
        if (FAILED(MFCreateSourceReaderFromURL(file.c_str(), options.get(), reader_.put()))) return false;
        Com<IMFMediaType> pictures;
        if (FAILED(MFCreateMediaType(pictures.put())) || FAILED(pictures->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(pictures->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
            FAILED(reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, pictures.get())) || !readPictureType())
            return false;
        Com<IMFMediaType> sound;
        sound_ = SUCCEEDED(MFCreateMediaType(sound.put())) && SUCCEEDED(sound->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
                 SUCCEEDED(sound->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float)) &&
                 SUCCEEDED(reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, sound.get())) && readSoundType();
        if (!sound_) reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), FALSE);
        PROPVARIANT length;
        PropVariantInit(&length);
        double seconds = 0.0;
        if (SUCCEEDED(reader_->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &length)) && length.vt == VT_UI8)
            seconds = static_cast<double>(length.uhVal.QuadPart) / 1e7;
        PropVariantClear(&length);
        describe({.width = static_cast<int>(area_.w), .height = static_cast<int>(area_.h), .duration = seconds, .sound = sound_});
        return area_.w > 0 && area_.h > 0;
    }

    bool nextPicture(std::vector<std::uint8_t>& pixels, int& pitch, double& time) override {
        Com<IMFSample> sample;
        for (;;) {
            DWORD flags = 0;
            LONGLONG stamp = 0;
            if (FAILED(reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &stamp, sample.put()))) return false;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && !readPictureType()) return false;
            time = static_cast<double>(stamp) / 1e7;
            if (sample) break; // (none: a gap in the stream, on to the next)
        }
        Com<IMFMediaBuffer> buffer;
        BYTE* bytes = nullptr;
        DWORD length = 0;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.put())) || FAILED(buffer->Lock(&bytes, nullptr, &length))) return false;
        const ScopeExit _([&] { buffer->Unlock(); });
        const std::size_t row = static_cast<std::size_t>(std::abs(stride_));
        if (static_cast<std::size_t>(length) < row * frameH_) return false;
        pitch = static_cast<int>(area_.w * 4);
        pixels.resize(static_cast<std::size_t>(pitch) * area_.h);
        for (std::uint32_t y = 0; y < area_.h; ++y) { // (the shown part only; rows that run upward turned the right way)
            const std::uint32_t from = stride_ >= 0 ? area_.y + y : frameH_ - 1 - (area_.y + y);
            std::uint8_t* to = pixels.data() + static_cast<std::size_t>(pitch) * y;
            std::memcpy(to, bytes + row * from + std::size_t{area_.x} * 4, static_cast<std::size_t>(pitch));
            for (std::size_t x = 3; x < static_cast<std::size_t>(pitch); x += 4) to[x] = 255; // (RGB32's fourth byte is unset)
        }
        return true;
    }

    bool nextSound(std::vector<float>& samples) override {
        samples.clear();
        if (!sound_) return false;
        Com<IMFSample> sample;
        for (;;) {
            DWORD flags = 0;
            LONGLONG stamp = 0;
            if (FAILED(reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, &stamp, sample.put()))) return false;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && !readSoundType()) return false;
            if (sample) break;
        }
        Com<IMFMediaBuffer> buffer;
        BYTE* bytes = nullptr;
        DWORD length = 0;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.put())) || FAILED(buffer->Lock(&bytes, nullptr, &length))) return false;
        std::vector<float> stereo; // (its frames as two sides: one side said twice, or the first two of more)
        {
            const ScopeExit _([&] { buffer->Unlock(); });
            const std::size_t count = length / sizeof(float) / channels_;
            stereo.resize(count * 2);
            for (std::size_t i = 0; i < count; ++i) {
                float left = 0.0f, right = 0.0f;
                std::memcpy(&left, bytes + (i * channels_) * sizeof(float), sizeof(float));
                if (channels_ > 1) std::memcpy(&right, bytes + (i * channels_ + 1) * sizeof(float), sizeof(float));
                else right = left;
                stereo[2 * i] = left;
                stereo[2 * i + 1] = right;
            }
        }
        resampled(std::move(stereo), samples);
        return true;
    }

    void restart() override {
        PROPVARIANT at;
        PropVariantInit(&at);
        at.vt = VT_I8;
        at.hVal.QuadPart = 0;
        reader_->SetCurrentPosition(GUID{}, at); // (GUID_NULL: in 100 ns steps)
        PropVariantClear(&at);
        left_.clear();
        position_ = 0.0;
    }

private:
    struct Area {
        std::uint32_t x = 0, y = 0, w = 0, h = 0;
    };
    TempFile temp_; // (before the reader: it goes after it, once the file's closed)
    Com<IMFSourceReader> reader_;
    Area area_;                // the part of each picture that's shown (a 1080p video is often 1088 high, with 8 hidden)
    std::uint32_t frameH_ = 0; // each picture's whole height
    LONG stride_ = 0;          // bytes from one row to the next (less than none: the rows run upward)
    std::uint32_t channels_ = 0, rate_ = 0;
    bool sound_ = false;
    std::vector<float> left_; // stereo frames still to be resampled
    double position_ = 0.0;   // where the next one at kSoundRate falls among them

    bool readPictureType() {
        Com<IMFMediaType> type;
        UINT32 w = 0, h = 0;
        if (FAILED(reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), type.put())) ||
            FAILED(MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0)
            return false;
        UINT32 stride = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) stride_ = static_cast<LONG>(stride);
        else if (FAILED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &stride_))) stride_ = static_cast<LONG>(w * 4);
        frameH_ = h;
        area_ = {0, 0, w, h};
        MFVideoArea shown{};
        if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&shown), sizeof shown, nullptr)) && shown.Area.cx > 0 &&
            shown.Area.cy > 0 && shown.OffsetX.value >= 0 && shown.OffsetY.value >= 0 && static_cast<UINT32>(shown.OffsetX.value + shown.Area.cx) <= w &&
            static_cast<UINT32>(shown.OffsetY.value + shown.Area.cy) <= h)
            area_ = {static_cast<std::uint32_t>(shown.OffsetX.value), static_cast<std::uint32_t>(shown.OffsetY.value), static_cast<std::uint32_t>(shown.Area.cx),
                     static_cast<std::uint32_t>(shown.Area.cy)};
        return true;
    }

    bool readSoundType() {
        Com<IMFMediaType> type;
        if (FAILED(reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), type.put())) ||
            FAILED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels_)) || FAILED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate_)))
            return false;
        return channels_ > 0 && rate_ > 0;
    }

    // Stereo frames at the video's rate, as frames at kSoundRate (between two of them: a mix of both).
    void resampled(std::vector<float> stereo, std::vector<float>& out) {
        if (rate_ == static_cast<std::uint32_t>(kSoundRate)) {
            out = std::move(stereo);
            return;
        }
        left_.insert(left_.end(), stereo.begin(), stereo.end());
        const std::size_t frames = left_.size() / 2;
        const double step = static_cast<double>(rate_) / kSoundRate;
        while (position_ + 1.0 < static_cast<double>(frames)) {
            const auto i = static_cast<std::size_t>(position_);
            const auto t = static_cast<float>(position_ - static_cast<double>(i));
            out.push_back(std::lerp(left_[2 * i], left_[2 * i + 2], t));
            out.push_back(std::lerp(left_[2 * i + 1], left_[2 * i + 3], t));
            position_ += step;
        }
        const auto used = static_cast<std::size_t>(position_);
        left_.erase(left_.begin(), left_.begin() + static_cast<std::ptrdiff_t>(2 * std::min(used, frames)));
        position_ -= static_cast<double>(used);
    }
};

} // namespace

std::unique_ptr<SystemVideo> SystemVideo::open(const std::string& path, const std::string* data) {
    auto video = std::make_unique<WindowsVideo>();
    if (!video->start(path, data)) return nullptr;
    return video;
}

} // namespace vn
