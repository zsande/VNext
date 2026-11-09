// What's said: a line in its textbox (or several, together), its words appearing at their speed, voices and
// mouths moving with them, and a page of lines.
#include "Game.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <format>

namespace vn {

// Starts a new line: fills in {expressions} and reads its text tags.
void Game::say(std::string speaker, SDL_Color colour, std::string_view text, const CharacterDef* def) {
    speaker_ = std::move(speaker);
    speakerKey_ = def ? def->key : std::string{};
    speakerColour_ = colour;
    voiced_ = madeUpVoice_ = textFollowsVoice_ = typing_ = false;
    lineTime_ = 0.0f;
    textColour_ = def ? def->textColour : std::nullopt;
    textbox_ = boxOf(def);
    source_ = fillValues(text, lookup_);
    typingSound_ = def ? def->typingSound : std::string{};
    revealMode_ = def && def->textAppears ? *def->textAppears : gameSettings().textAppears;
    face_ = faceBase_ = faceSide_ = {};
    if (def && !def->face.empty()) {
        faceSide_ = def->faceSide;
        const auto it = expressions_.find(def->key);
        faceBase_ = def->face;
        face_ = def->faceOf(it != expressions_.end() ? it->second : std::string{});
    }
    stops_.clear();
    speeds_.clear();
    noWait_ = false;
    RichText rich = richText(text, lookup_, &script_.palette, [&](std::string_view tag, std::string_view arg, std::size_t at) {
        const auto n = parseNumber(arg);
        if (tag == "pause") stops_.push_back({at, n ? std::max(0.0f, *n) : -1.0f});
        else if (tag == "no_wait") noWait_ = true;
        else if (tag == "speed") speeds_.push_back({at, n ? std::max(0, static_cast<int>(*n)) : -1});
        else if (tag == "/speed") speeds_.push_back({at, -1});
        else if (tag == "appears") revealMode_ = lower(arg) == "line" ? TextAppears::Line : lower(arg) == "word" ? TextAppears::Word : TextAppears::Letter;
    });
    text_ = std::move(rich.text);
    spans_ = std::move(rich.spans);
    length_ = utf8::length(text_);
    reveal_ = autoTimer_ = holdTimer_ = 0.0f;
    passedStops_ = 0;
}

const Game::Stop* Game::pendingStop() const {
    return passedStops_ < stops_.size() ? &stops_[passedStops_] : nullptr;
}

std::string Game::boxOf(const CharacterDef* def) const {
    return def && !def->textbox.empty() ? def->textbox : "textbox";
}

bool Game::othersTyping() const {
    return std::ranges::any_of(others_, [](const BoxLine& l) { return l.reveal < static_cast<float>(l.length); });
}

Game::BoxLine Game::boxLine(std::string box, std::string speaker, SDL_Color colour, std::string source, const CharacterDef* def) const {
    RichText rich = richText(source, lookup_, &script_.palette);
    BoxLine l{.box = std::move(box), .speaker = std::move(speaker), .speakerKey = def ? def->key : std::string{}, .source = std::move(source),
              .text = std::move(rich.text), .speakerColour = colour, .spans = std::move(rich.spans)};
    l.length = utf8::length(l.text);
    if (def && !def->face.empty()) {
        const auto it = expressions_.find(def->key);
        l.face = def->faceOf(it != expressions_.end() ? it->second : std::string{});
    }
    return l;
}

// A line of a together block, before its last: it shows now in its own textbox (typing, without a
// voice), and the story goes on to the next line.
void Game::sayTogether(const cmd::Say& c, const CharacterDef* def) {
    BoxLine l = boxLine(boxOf(def), def ? def->name : c.speaker, def ? def->nameColour : gameSettings().narrationColour, fillValues(c.text, lookup_), def);
    if (skipping() || ctx_.settings.textSpeed == 0) l.reveal = static_cast<float>(l.length);
    std::erase_if(others_, [&](const BoxLine& other) { return other.box == l.box; });
    together_ = true;
    learnTips(c.text);
    ctx_.read.insert(c.id);
    addHistory({.speaker = l.speaker, .colour = l.speakerColour, .text = l.text, .spans = l.spans});
    others_.push_back(std::move(l));
}

std::optional<Game::LineView> Game::line(std::string_view box) const {
    if (showingSay() && box == textbox_)
        return LineView{.speaker = speaker_, .text = text_, .face = face_, .faceBase = faceBase_, .faceSide = faceSide_, .speakerColour = speakerColour_,
                        .textColour = textColour_, .spans = &spans_, .revealed = revealed(), .limit = revealLimit(), .mode = revealMode_,
                        .complete = textComplete(), .atPause = atPause(), .character = speakerCharacter(), .faceFrame = faceFrame()};
    const auto it = std::ranges::find(others_, box, &BoxLine::box);
    if (it == others_.end()) return std::nullopt;
    const CharacterDef* def = script_.character(it->speakerKey);
    const bool typing = it->reveal < static_cast<float>(it->length);
    Frame face;
    if (def) {
        face.mouth = typing ? mouth(def->faceMouth, flapping(it->speakerKey + " face", clock_, def->mouthSpeed), false) : 0.0f; // (said together: no voice)
        if (def->faceBlink) face.eyes = blinking(it->speakerKey, clock_, def->secondsBetweenBlinks);
    }
    return LineView{.speaker = it->speaker, .text = it->text, .face = it->face, .faceBase = def ? std::string_view(def->face) : std::string_view{},
                    .faceSide = def && !def->face.empty() ? std::string_view(def->faceSide) : std::string_view{}, .speakerColour = it->speakerColour,
                    .textColour = def ? def->textColour : std::nullopt, .spans = &it->spans, .revealed = static_cast<std::size_t>(it->reveal),
                    .limit = it->length, .mode = def && def->textAppears ? *def->textAppears : gameSettings().textAppears, .complete = !typing, .character = def, .faceFrame = face};
}

std::vector<std::string> Game::boxes() const {
    std::vector<std::string> out;
    for (const BoxLine& l : others_)
        if (!(showingSay() && l.box == textbox_)) out.push_back(l.box);
    if (showingSay()) out.push_back(textbox_);
    return out;
}

int Game::currentSpeed() const {
    int cps = -1;
    for (const auto& s : speeds_)
        if (s.at <= static_cast<std::size_t>(reveal_)) cps = s.cps;
    return cps < 0 ? ctx_.settings.textSpeed : cps;
}

std::size_t Game::revealLimit() const {
    const Stop* stop = pendingStop();
    return stop ? stop->at : length_;
}

bool Game::textComplete() const {
    if (wait_ == Wait::Menu) return menu()->optional; // a click goes on
    if (wait_ != Wait::Say) return false;
    if (const Stop* stop = pendingStop(); stop && stop->seconds < 0 && reveal_ >= static_cast<float>(stop->at)) return true;
    return reveal_ >= static_cast<float>(length_) && !pendingStop() && !othersTyping();
}

void Game::revealTo(std::size_t position) {
    const auto before = static_cast<std::size_t>(reveal_);
    reveal_ = static_cast<float>(std::min(position, length_));
    if (!typingSound_.empty() && ctx_.settings.typingSounds && !voiced_ && static_cast<std::size_t>(reveal_) > before && typingSoundTimer_ <= 0.0f && !skipping()) {
        ctx_.sound.playSound(ctx_.assets.sound(typingSound_), gameSettings().typingSoundVolume);
        typingSoundTimer_ = gameSettings().typingSoundGap;
    }
}

// A line said (or narrated): its speaker's face and textbox (the other textboxes' lines go, unless said together with it), its
// voice (a voice line just before it, their voice file, or made-up speech), the history, and the story
// waiting for a click.
void Game::sayLine(const cmd::Say& c, std::size_t here, bool fast) {
    const CharacterDef* def = c.character.empty() ? nullptr : script_.character(c.character);
    takeExpression(c, def);
    if (hiddenLines_ > 0) { // gone past unseen (a choice's return=no)
        --hiddenLines_;
        pendingVoice_.reset();
        return;
    }
    if (trigger_ && trigger_->lines-- <= 0) { // a choice up for some lines: its time is over
        trigger_.reset();
        choiceItems_.clear();
    }
    // Lines in other textboxes, said together with the last line: they go.
    if (!together_) others_.clear();
    if (c.together) return sayTogether(c, def);
    together_ = false;
    const std::string box = boxOf(def);
    std::erase_if(others_, [&](const BoxLine& l) { return l.box == box; });
    lineVoice_ = std::exchange(pendingVoice_, std::nullopt).value_or(LineVoice{}); // a voice line just before it
    bool voiced = !lineVoice_.file.empty();
    const std::optional<bool> follows = lineVoice_.textFollows;
    if (!voiced) ctx_.sound.stopVoice();
    say(def ? def->name : c.speaker, def ? def->nameColour : gameSettings().narrationColour, c.text, def);
    // Its voice (kept in the history to hear again): the voice line, else their own voice file
    // for this line (voice_files=), else made-up speech (made_up_voice=).
    HistoryEntry entry{.speaker = speaker_, .colour = speakerColour_, .text = text_, .spans = spans_};
    if (voiced) entry.voice = ctx_.assets.sound(lineVoice_.file), entry.voiceVolume = lineVoice_.volume;
    else if (def && !def->voiceFiles.empty())
        if (const auto file = ctx_.assets.findSound(std::format("{}/{}", def->voiceFiles, c.voiceId))) entry.voice = *file;
    if (!voiced && entry.voice.empty() && def && def->madeUpVoice > 0.0f && ctx_.settings.madeUpVoices) entry.madeUpVoice = def->madeUpVoice, entry.madeUpVoiceSpeed = def->madeUpVoiceSpeed;
    if (!voiced && !fast) {
        if (!entry.voice.empty()) ctx_.sound.playVoice(entry.voice), voiced = true;
        else if (entry.madeUpVoice > 0.0f && !ctx_.settings.readAloud) // (Read aloud: it says the words in its place)
            voiced = madeUpVoice_ = ctx_.sound.playMadeUpVoice(text_, entry.madeUpVoice, entry.madeUpVoiceSpeed);
    }
    voiced_ = voiced;
    textFollowsVoice_ = voiced && follows.value_or(gameSettings().textFollowsVoices);
    menuPrompt_ = false;
    saidAt_ = here;
    lineRead_ = !ctx_.read.insert(c.id).second;
    addToPage();
    addHistory(std::move(entry));
    learnTips(c.text);
    waitFor(Wait::Say, here);
    remember();
}

// Their face, and their image on stage if the face copies it.
void Game::takeExpression(const cmd::Say& c, const CharacterDef* def) {
    if (!def || c.expression.empty()) return;
    expressions_.insert_or_assign(def->key, c.expression);
    if (def->faceCopies.expression || def->face.empty()) stage_.setExpression(def->key, c.expression);
}

// The line the story was on, back as it was once a chat= or call= scene's lines that showed something are over: all there
// at once, without its voice, not in the history again, its page as it was. (A choice, a wait...: done again.)
void Game::sayAgain(std::size_t page) {
    const auto* c = pc_ < script_.code.size() ? std::get_if<cmd::Say>(&script_.code[pc_].command) : nullptr;
    if (!c || c->together) return; // (a line said with others: said again)
    const std::size_t here = current_ = pc_++;
    const CharacterDef* def = c->character.empty() ? nullptr : script_.character(c->character);
    takeExpression(*c, def);
    others_.clear();
    together_ = false;
    say(def ? def->name : c->speaker, def ? def->nameColour : gameSettings().narrationColour, c->text, def);
    reveal_ = static_cast<float>(length_);
    passedStops_ = stops_.size();
    noWait_ = false;
    lineVoice_ = {};
    menuPrompt_ = false;
    saidAt_ = here;
    page_.resize(std::min(page_.size(), page));
    waitFor(Wait::Say, here);
}

void Game::addToPage() {
    page_.push_back({speaker_, speakerColour_, text_, spans_, textColour_});
    if (page_.size() > 60) page_.erase(page_.begin());
}

std::string Game::expressionOf(std::string_view character) const {
    const auto it = expressions_.find(character);
    return it != expressions_.end() ? it->second : std::string{};
}

// A line that follows its voice waits at a {pause} for a click, and so does the voice, once it has
// said the words before it: in the silence there (the one nearest to where the {pause} is in the
// text), else right at that place. A click that shows the text early lets the voice catch up first.
bool Game::voiceAtPause() const {
    if (wait_ != Wait::Say || !textFollowsVoice_) return false;
    const Stop* stop = pendingStop();
    if (!stop || stop->seconds >= 0.0f) return false;
    const auto said = ctx_.sound.voiceProgress();
    if (!said) return false;
    const float at = static_cast<float>(stop->at) / static_cast<float>(std::max<std::size_t>(length_, 1));
    if (const auto silence = ctx_.sound.voiceSilenceNear(at)) return *said >= *silence && ctx_.sound.voiceSilent();
    return *said >= at;
}

// The line on screen: types out (or follows its voice), holds at {pause}s, and goes on by itself
// when skipping, on auto-forward or with {no_wait}.
void Game::updateLine(float dt, bool fast) {
    const auto length = static_cast<float>(length_);
    const Settings& s = ctx_.settings;
    const bool skipLine = fast && (lineRead_ || s.skipUnread);
    if (fast && !skipLine) skip_ = false; // reached text the player hasn't read yet
    if (skipLine) {
        reveal_ = length;
        passedStops_ = stops_.size();
        if ((skipTimer_ += dt) >= gameSettings().skipTime) {
            skipTimer_ = 0.0f;
            nextLine();
        }
        return;
    }
    // A line that follows its voice shows as much as has been said; timed pauses don't apply.
    std::optional<float> said;
    if (textFollowsVoice_) {
        said = ctx_.sound.voiceProgress();
        if (!said) textFollowsVoice_ = false; // the voice ended (or can't play): the rest types as usual
        else
            for (const Stop* p = pendingStop(); p && p->seconds >= 0.0f && static_cast<float>(p->at) <= *said * length; p = pendingStop())
                ++passedStops_;
    }
    const Stop* stop = pendingStop();
    if (stop && reveal_ >= static_cast<float>(stop->at)) {
        if (stop->seconds >= 0.0f) { // timed pause
            if ((holdTimer_ += dt) >= stop->seconds) {
                holdTimer_ = 0.0f;
                ++passedStops_;
            }
        } else if (auto_ && (autoTimer_ += dt) >= static_cast<float>(s.autoDelay) / 1000.0f) {
            autoTimer_ = 0.0f;
            nextLine();
        }
        return;
    }
    const float limit = stop ? static_cast<float>(stop->at) : length;
    float next = limit;
    if (said) next = voiceAtPause() ? limit : std::min(limit, std::max(reveal_, *said * length)); // (the voice waits at a {pause}: all of it)
    else if (const int cps = currentSpeed(); cps > 0) next = std::min(limit, reveal_ + static_cast<float>(cps) * dt);
    if (static_cast<std::size_t>(next) > static_cast<std::size_t>(reveal_)) revealTo(static_cast<std::size_t>(next)); // (its typing sound)
    reveal_ = next;
    typing_ = reveal_ < limit;
    if (reveal_ < length || pendingStop() || othersTyping()) return; // (lines said together with it still typing too)
    // {no_wait} and auto-forward wait for the voice line to end; reading time per letter is only
    // added to lines without a voice (the voice gave the player that time).
    const bool talking = voiced_ && ctx_.sound.voicePlaying();
    if (noWait_) {
        if (!talking) nextLine();
        return;
    }
    const float delay = static_cast<float>(s.autoDelay) / 1000.0f + (voiced_ ? 0.0f : length * gameSettings().autoLetterDelay);
    if (auto_ && !talking && (autoTimer_ += dt) >= delay) nextLine();
}

// How much the speaker is speaking right now: their voice's loudness, or a steady flapping while
// their text types out (lines without a voice). A face that doesn't copy their mouth flaps at its
// own moments instead, opening as wide as the voice is loud.
void Game::updateSpeech() {
    speech_ = faceSpeech_ = 0.0f;
    if (wait_ != Wait::Say || speakerKey_.empty()) return;
    const CharacterDef* def = script_.character(speakerKey_);
    const float flaps = def ? def->mouthSpeed : gameSettings().mouthSpeed;
    float loudness = 1.0f;
    if (voiced_) {
        if (!ctx_.sound.voicePlaying()) return;
        speech_ = loudness = ctx_.sound.voiceLevel();
    } else if (typing_) {
        speech_ = 0.5f - 0.5f * std::cos(lineTime_ * flaps * 6.2831853f);
    } else {
        return;
    }
    faceSpeech_ = flapping(speakerKey_ + " face", lineTime_, flaps) * std::min(1.0f, loudness * 1.5f);
}

float Game::mouth(MouthMoves moves, float speech, bool voiced) const {
    moves = std::max(moves, static_cast<MouthMoves>(std::clamp(ctx_.settings.mouths, 0, 2))); // (MouthMoves goes Yes, Voice, No, as the setting does)
    return moves == MouthMoves::Yes || (moves == MouthMoves::Voice && voiced) ? speech : 0.0f;
}

Frame Game::frame(std::string_view who) const {
    const CharacterDef* def = script_.character(who);
    Frame f;
    if (def ? def->blink : gameSettings().blink) f.eyes = blinking(who, clock_, def ? def->secondsBetweenBlinks : gameSettings().secondsBetweenBlinks);
    if (def && who == speakerKey_) f.mouth = mouth(def->mouthMoves, speech_, voiced_);
    else if (def && std::ranges::any_of(others_, [&](const BoxLine& l) { return l.speakerKey == who && l.reveal < static_cast<float>(l.length); }))
        f.mouth = mouth(def->mouthMoves, flapping(who, clock_, def->mouthSpeed), false); // a line said together: typing without a voice
    return f;
}

Frame Game::faceFrame() const {
    const CharacterDef* def = script_.character(speakerKey_);
    if (!def) return {};
    // A face that copies the character blinks and talks with them; otherwise at its own moments.
    Frame f{.mouth = mouth(def->faceMouth, def->faceCopies.mouth ? speech_ : faceSpeech_, voiced_)};
    if (def->faceBlink) f.eyes = blinking(def->faceCopies.blink ? speakerKey_ : speakerKey_ + " face", clock_, def->secondsBetweenBlinks);
    return f;
}

} // namespace vn
