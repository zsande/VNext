// Made-up voices: a line's words as syllables sung in a character's pitch, for characters without recorded voices.
#include "Sound.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <random>

namespace vn {

namespace {

constexpr float kTau = 6.2831853f;

struct Vowel {
    float f1, f2; // formants (Hz): what makes an "a" sound like an "a"
};
constexpr std::array<Vowel, 5> kVowels{{{850, 1220}, {600, 2300}, {350, 2700}, {500, 900}, {380, 950}}}; // a e i o u
constexpr Vowel kHum{280, 1600}; // ん, through the nose

const Vowel* vowel(char c) {
    switch (c) {
    case 'a': return &kVowels[0];
    case 'e': return &kVowels[1];
    case 'i':
    case 'y': return &kVowels[2];
    case 'o': return &kVowels[3];
    case 'u': return &kVowels[4];
    default: return nullptr;
    }
}

// Hiragana from ぁ (U+3041) to ゖ (U+3096); katakana are the same 0x60 further on. A vowel letter
// is a syllable with that vowel (with a consonant after the first ten); a capital is a small kana,
// which changes the vowel of the one before (きゃ, ファ); Q is っ (a short stop), N is ん (a hum).
constexpr std::string_view kKana = "AaIiUuEeOo" "aaiiuueeoo" "aaiiuueeoo" "aaiiQuueeoo" "aiueo" "aaaiiiuuueeeooo" "aiueo"
                                   "AaUuOo" "aiueo" "AaieoNuAE";
// ...and how each starts: k a stop (k t p), g a voiced one (g d b), s a hiss (s sh ch ts h f), z a
// voiced hiss (z j), n a soft one (n m r y w), - just the vowel.
constexpr std::string_view kKanaStarts = "----------" "kgkgkgkgkg" "szszszszsz" "kgsz-szkgkg" "nnnnn" "sgksgksgksgksgk" "nnnnn"
                                         "-n-n-n" "nnnnn" "-nnn--g--";
// Korean: the vowel of each syllable block, by its middle letter (ㅏ ㅐ ㅑ ... ㅣ).
constexpr std::string_view kHangulVowels = "aeaeoeoeoaeeouoeiuuii";

struct Syllable {
    float start, length;
    Vowel sound;
    bool consonant;         // starts with a puff of noise (a mora: with its kind of consonant, see kKanaStarts)
    char starts = '-';      // a mora's consonant
    bool mora = false;      // Japanese: morae flow into each other without a break
    float level = 1.0f;
    float from = 0, to = 0; // pitch glide
};

} // namespace

std::vector<float> madeUpVoice(std::string_view text, float pitch, int rate, float speed) {
    const float slow = 1.0f / std::clamp(speed, 0.25f, 4.0f); // stretches every syllable and pause
    std::mt19937 rng(static_cast<std::uint32_t>(fnv1a(text)));
    auto jitter = [&](int range) { return static_cast<float>(static_cast<int>(rng() % static_cast<unsigned>(range))); };
    std::vector<Syllable> syllables;
    std::size_t sentence = 0; // first syllable of the sentence being read
    float t = 0.05f;
    std::string word;

    auto add = [&](Vowel sound, bool consonant, bool last) {
        const float length = (0.085f + jitter(40) / 1000.0f + (last ? 0.04f : 0.0f)) * slow;
        syllables.push_back({t, length, sound, consonant});
        t += length + 0.012f * slow;
    };
    auto addMora = [&](Vowel sound, char starts, float level = 1.0f) { // Japanese: even beats, one running into the next
        const float length = (0.1f + jitter(10) / 1000.0f) * slow;
        syllables.push_back({.start = t, .length = length, .sound = sound, .consonant = starts != '-', .starts = starts, .mora = true, .level = level});
        t += length;
    };
    auto vowelOf = [](char v) { return *vowel(static_cast<char>(std::tolower(static_cast<unsigned char>(v)))); };
    // Japanese: kana say their own vowels, a mora each, and kanji are read as one or two morae. A
    // line without kana is Chinese: a syllable a character.
    bool japanese = false;
    const char* p = text.data();
    for (std::size_t n = text.size(); n > 0 && !japanese;) {
        const Uint32 c = SDL_StepUTF8(&p, &n);
        japanese = c >= 0x3041 && c <= 0x30FA;
    }
    auto endWord = [&] { // a syllable for each group of vowels
        std::vector<std::pair<const Vowel*, bool>> groups;
        bool consonant = false;
        for (std::size_t i = 0; i < word.size(); ++i) {
            const Vowel* v = vowel(word[i]);
            if (!v) {
                consonant = true;
                continue;
            }
            if (i == 0 || !vowel(word[i - 1])) groups.emplace_back(v, consonant);
            consonant = false;
        }
        if (groups.empty() && !word.empty()) groups.emplace_back(&kVowels[4], true); // "hm", "shh"
        for (std::size_t k = 0; k < groups.size(); ++k) add(*groups[k].first, groups[k].second, k + 1 == groups.size());
        if (!groups.empty()) t += 0.03f * slow;
        word.clear();
    };
    auto endSentence = [&](char end) { // the voice falls through a sentence; a question rises at the end
        const std::size_t n = syllables.size() - sentence;
        for (std::size_t k = 0; k < n; ++k) {
            Syllable& s = syllables[sentence + k];
            const float pos = n > 1 ? static_cast<float>(k) / static_cast<float>(n - 1) : 0.0f;
            const bool last = k + 1 == n;
            if (s.mora) { // a smooth line: up after the first mora, then slowly down, each glide starting where the last one ended
                float tone = pitch * (k == 0 ? 0.95f : 1.08f - 0.16f * pos) * (end == '!' ? 1.06f : 1.0f);
                if (end == '?' && last) tone = pitch * 1.25f;
                const Syllable* before = k > 0 ? &syllables[sentence + k - 1] : nullptr;
                s.from = before && before->mora ? before->to : tone;
                s.to = last && end != '?' ? tone * 0.9f : tone;
                continue;
            }
            float tone = pitch * (1.1f - 0.2f * pos) * (1.0f + (jitter(60) - 30.0f) / 1000.0f);
            if (end == '!') tone *= 1.08f;
            if (end == '?' && last) tone = pitch * 1.3f;
            s.from = tone;
            s.to = tone * (last ? (end == '?' ? 1.12f : 0.88f) : 0.98f);
        }
        sentence = syllables.size();
    };

    const char* at = text.data();
    std::size_t left = text.size();
    while (left > 0) {
        const Uint32 c = SDL_StepUTF8(&at, &left);
        if (c < 0x80) {
            const auto ch = static_cast<char>(std::tolower(static_cast<int>(c)));
            if ((ch >= 'a' && ch <= 'z') || ch == '\'') {
                word += ch;
            } else if (ch >= '0' && ch <= '9') { // a syllable a digit
                endWord();
                add(kVowels[static_cast<std::size_t>(ch - '0') % kVowels.size()], true, true);
            } else if (ch == ',' || ch == ';' || ch == ':') {
                endWord();
                t += 0.16f * slow;
            } else if (ch == '.' && left >= 2 && at[0] == '.' && at[1] == '.') { // ...
                endWord();
                t += 0.45f * slow;
                at += 2;
                left -= 2;
            } else if (ch == '.' || ch == '!' || ch == '?') {
                endWord();
                endSentence(ch);
                t += 0.32f * slow;
            } else {
                endWord();
            }
        } else if (c == 0x3002 || c == 0xFF01 || c == 0xFF1F || c == 0xFF0E) { // 。！？．
            endWord();
            endSentence(c == 0xFF1F ? '?' : c == 0xFF01 ? '!' : '.');
            t += 0.32f * slow;
        } else if (c == 0x3001 || c == 0xFF0C) { // 、，
            endWord();
            t += 0.16f * slow;
        } else if (c == 0x2026) { // …
            endWord();
            t += 0.45f * slow;
        } else if ((c >= 0x2000 && c < 0x2070) || (c >= 0x3000 && c < 0x3040) || (c >= 0xFF00 && c < 0xFF10)) {
            endWord(); // other punctuation and spaces
        } else if (const Uint32 k = c >= 0x30A1 && c <= 0x30F6 ? c - 0x60 : c; k >= 0x3041 && k <= 0x3096) { // kana
            endWord();
            const char v = kKana[k - 0x3041];
            if (v == 'Q') t += 0.07f * slow; // っ: a short stop
            else if (v == 'N') addMora(kHum, '-', 0.6f);
            else if (std::isupper(static_cast<unsigned char>(v)) && !syllables.empty() && syllables.back().start + syllables.back().length + 0.02f >= t)
                syllables.back().sound = vowelOf(v); // small kana: きゃ, ファ
            else addMora(vowelOf(v), kKanaStarts[k - 0x3041]);
        } else if (c == 0x30FC && !syllables.empty()) { // ー: the vowel goes on
            syllables.back().length += 0.09f * slow;
            t += 0.09f * slow;
        } else if (c >= 0xAC00 && c <= 0xD7A3) { // Korean: a syllable a block, with its vowel; ㅇ first is no consonant
            endWord();
            const Uint32 block = c - 0xAC00;
            add(vowelOf(kHangulVowels[(block / 28) % 21]), block / 588 != 11, false);
        } else { // a letter of another script (kanji, hanzi...): a syllable each, or one or two morae in Japanese
            endWord();
            const std::uint64_t h = fnv1a(std::string_view(reinterpret_cast<const char*>(&c), sizeof c));
            for (int n = japanese && h % 5 < 3 ? 2 : 1, i = 0; i < n; ++i) {
                const Vowel sound = kVowels[(h >> (8 * i)) % kVowels.size()];
                if (japanese) addMora(sound, "kgsznn-"[(h >> (8 * i + 4)) % 7]);
                else add(sound, true, false);
            }
        }
    }
    endWord();
    if (sentence < syllables.size()) endSentence('.');
    if (syllables.empty()) return {};

    // Each syllable: a buzz at the voice's pitch, its harmonics loudest near the vowel's formants
    // (lower voices get lower formants), faded in and out.
    const float scale = std::clamp(0.82f + (pitch - 110.0f) / 500.0f, 0.82f, 1.0f);
    constexpr int kMostHarmonics = 64;
    std::array<float, kMostHarmonics> loudness{};
    std::vector<float> out(static_cast<std::size_t>((t + 0.15f) * static_cast<float>(rate)), 0.0f);
    const auto fr = static_cast<float>(rate);
    float phase = 0.0f;
    for (std::size_t index = 0; index < syllables.size(); ++index) {
        const Syllable& s = syllables[index];
        const auto first = static_cast<std::size_t>(s.start * fr);
        // A mora right after another flows on from it: the same buzz goes on, gliding to the new vowel.
        const Syllable* before = index > 0 && s.mora && syllables[index - 1].mora && std::abs(syllables[index - 1].start + syllables[index - 1].length - s.start) < 0.001f
                                     ? &syllables[index - 1] : nullptr;
        const bool after = index + 1 < syllables.size() && syllables[index + 1].mora && std::abs(s.start + s.length - syllables[index + 1].start) < 0.001f;
        if (s.consonant && !s.mora) {
            const auto n = static_cast<std::size_t>(0.03f * fr);
            float last = 0.0f;
            for (std::size_t i = 0; i < n && first >= n; ++i) {
                const float noise = (jitter(2001) - 1000.0f) / 1000.0f;
                out[first - n + i] += 0.05f * (noise - last) * std::sin(3.14159265f * static_cast<float>(i) / static_cast<float>(n));
                last = noise;
            }
        }
        const auto count = static_cast<std::size_t>(s.length * fr);
        if (!before) phase = 0.0f;
        int harmonics = 0;
        float last = 0.0f; // (the hiss's last noise sample)
        for (std::size_t i = 0; i < count && first + i < out.size(); ++i) {
            const float tt = static_cast<float>(i) / fr;
            const float f0 = (s.from + (s.to - s.from) * tt / s.length) * (1.0f + 0.012f * std::sin(kTau * 5.2f * (s.start + tt)));
            if (i % 128 == 0) { // the pitch moves slowly, so this is worked out now and then
                const float glide = before ? std::min(1.0f, tt / 0.035f) : 1.0f; // from the vowel before
                const Vowel v{before ? before->sound.f1 + (s.sound.f1 - before->sound.f1) * glide : s.sound.f1,
                              before ? before->sound.f2 + (s.sound.f2 - before->sound.f2) * glide : s.sound.f2};
                harmonics = std::min(kMostHarmonics, static_cast<int>(4500.0f / f0));
                for (int h = 1; h <= harmonics; ++h) {
                    const float f = f0 * static_cast<float>(h);
                    auto peak = [&](float centre, float width) { return std::exp(-std::pow((f - centre * scale) / width, 2.0f)); };
                    loudness[static_cast<std::size_t>(h - 1)] = peak(v.f1, 110) + 0.7f * peak(v.f2, 170) + 0.35f * peak(2900, 250) + 0.2f / static_cast<float>(h);
                }
            }
            phase = std::fmod(phase + kTau * f0 / fr, kTau);
            const float c1 = std::cos(phase), s1 = std::sin(phase);
            float ch = c1, sh = s1; // cos and sin of h * phase, by turning h times
            float v = 0.0f;
            for (int h = 0; h < harmonics; ++h) {
                v += loudness[static_cast<std::size_t>(h)] * sh;
                const float next = ch * c1 - sh * s1;
                sh = sh * c1 + ch * s1;
                ch = next;
            }
            if (!s.mora) {
                const float env = std::min(1.0f, tt / 0.018f) * std::min(1.0f, (s.length - tt) / 0.04f) * (0.75f + 0.25f * std::sin(3.14159265f * tt / s.length));
                out[first + i] += v * env * 0.12f;
                continue;
            }
            // A mora: faded in and out only where the voice starts and stops, and its consonant as a dip
            // in the voice: silence for a stop (k), less for a voiced one (g), a hiss (s, with some voice
            // for z), or just a little quieter (n).
            const float close = s.starts == 'k' ? 0.032f : s.starts == 's' ? 0.04f : s.starts == '-' ? 0.0f : 0.026f;
            const float floor = s.starts == 'g' ? 0.3f : s.starts == 'z' ? 0.35f : s.starts == 'n' ? 0.55f : 0.0f;
            const float open = close > 0.0f ? std::clamp((tt - close) / 0.012f, 0.0f, 1.0f) : 1.0f;
            const float env = (before ? 1.0f : std::min(1.0f, tt / 0.015f)) * (after ? 1.0f : std::min(1.0f, (s.length - tt) / 0.04f)) * (floor + (1.0f - floor) * open);
            float hiss = 0.0f;
            if ((s.starts == 's' || s.starts == 'z' || s.starts == 'k') && tt < close + 0.006f) { // breath (a stop: a short burst as it opens)
                const float noise = (jitter(2001) - 1000.0f) / 1000.0f;
                const float shape = s.starts == 'k' ? std::max(0.0f, 1.0f - std::abs(tt - close) / 0.006f) : std::sin(3.14159265f * std::min(1.0f, tt / (close + 0.006f)));
                hiss = (s.starts == 'z' ? 0.02f : 0.035f) * (noise - last) * shape;
                last = noise;
            }
            out[first + i] += (v * env * 0.12f + hiss) * s.level;
        }
    }
    float top = 0.0f;
    for (float v : out) top = std::max(top, std::abs(v));
    if (top > 0.0f)
        for (float& v : out) v *= 0.7f / top;
    return out;
}

} // namespace vn
