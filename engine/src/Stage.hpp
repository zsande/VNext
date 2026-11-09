#pragma once

// The stage: a background, and characters and images with their places and looks, moving as they're told; the
// camera, the weather and the effects.

#include "Script.hpp"
#include "Assets.hpp"
#include "Text.hpp"
#include "Shapes.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vn {

// How closed `who`'s eyes are `seconds` in: a blink about every `every` seconds, now and then two.
[[nodiscard]] float blinking(std::string_view who, float seconds, float every = 4.0f);
// How open `who`'s mouth is `seconds` into a line: about `perSecond` flaps a second, each at its own
// moment, of its own length and width (a face that talks on its own rhythm).
[[nodiscard]] float flapping(std::string_view who, float seconds, float perSecond);

// The visual layer: a background plus tagged objects (characters and images) with position,
// anchor, zoom, rotation, opacity, flip, layer and tint; eased tweens; animations (as they come and
// go, and while they're shown); weather; the camera; effects over the whole stage.
// Missing images are drawn as labelled placeholders.
class Stage {
public:
    explicit Stage(const Canvas& size) : canvas(size) {}
    // Everything needed to recreate an object (saves).
    struct ObjectState {
        std::string tag, character, image, expression;
        float x = 0.5f, y = 1.0f, zoom = 1.0f, opacity = 1.0f, rotation = 0.0f; // x, y: 0..1, the part of the image there too
        bool flip = false;
        int layer = 0;
        SDL_Color tint{255, 255, 255, 255};
        std::string animation; // what it plays while it's shown (show ... animation=)
        std::string played;    // one played on it (show ... with), on top of that
    };

    // A new background, playing `entrance` as it comes in over the one before (which goes, with what was
    // on stage, when that ends; none: at once). The one already there (`key` is its name): only
    // `entrance`, played on it. `done`: at its end at once (skipping, or animations off).
    void setBackground(std::string key, std::shared_ptr<const Animation> entrance, bool done);
    [[nodiscard]] bool backgroundComing() const { return !previous_.empty(); } // (the one before still under it)
    // Shows a character (`def`) or an image under `tag`, playing `played` as it comes, or changes the one
    // already there (it slides to a new place in `slide` seconds, and plays `played`).
    void show(const std::string& tag, const CharacterDef* def, const std::string& image, const std::string& expression,
              const StageProps& props, std::shared_ptr<const Animation> played, float slide, bool done);
    [[nodiscard]] bool has(std::string_view tag) const; // (on stage, not going)
    // What an image on stage plays: its own (`own`: while it's shown; else one played on it, on top
    // of that). Null: none. `done`: as it ends (skipping, or a save loaded), unless it's looping. (Nothing
    // with that tag: nothing.) animateBackground(): the same for the background (a new one starts without
    // them).
    void animate(std::string_view tag, std::shared_ptr<const Animation> animation, bool own, bool done);
    void animateBackground(std::shared_ptr<const Animation> animation, bool own, bool done);
    [[nodiscard]] std::string backgroundAnimation(bool own) const; // (its name: "" for none)
    void setExpression(std::string_view tag, const std::string& expression);
    [[nodiscard]] std::string expression(std::string_view tag) const; // what an image on stage shows ("": its own)
    // It plays `exit`, then it's gone (none, or `done`: at once).
    void hide(std::string_view tag, std::shared_ptr<const Animation> exit, bool done);
    [[nodiscard]] bool moving() const; // (a slide, the camera, or an animation played on something that hasn't ended)

    // Weather over the stage; a kind of "" stops it. Fades in or out over `fade` seconds.
    void setWeather(const cmd::Weather& weather, float fade);
    [[nodiscard]] const cmd::Weather& weather() const { return weather_; }

    // Effects over the whole stage ("effect" lines); `instant`: no fading. The ones that stay (not
    // those "for N seconds"), for saves.
    void setEffect(const cmd::Effect& effect, bool instant);
    [[nodiscard]] cmd::Effect effect() const { return {.looks = looks_, .amount = lookAmount_}; }
    // How strongly the controllers rumble now (effect vibration: 0 to 1, with its amount and fade).
    [[nodiscard]] float vibration() const { return strength(kLookVibration) * (burst_ & kLookVibration ? burstAmount_ : lookAmount_); }

    // The view, played as an image's animations are (`own`: one it keeps playing; else one played on it
    // now, from where the one before left the view; none: back to normal). `done`: at its end at once.
    // Its places are what it looks at (x=-1: the left edge in the middle), its moves by a share of the
    // screen move what it looks at; its opacity, colour, cover and wipe are the whole stage's.
    void animateCamera(std::shared_ptr<const Animation> animation, bool own, bool done);
    [[nodiscard]] std::string cameraAnimation(bool own) const; // (its name: "" for none)
    [[nodiscard]] const Animation::Look& cameraStart() const { return camera_.playedFrom; } // (where the one played on it started)
    void restoreCamera(std::shared_ptr<const Animation> own, std::shared_ptr<const Animation> played, const Animation::Look& start); // (a save's: as they ended)
    void clearObjects(); // (the background changes: they go with the one before)
    void reset();
    void restore(const ObjectState& state, const CharacterDef* def);

    void update(float dt);
    void finish();                                // what's under way, at its end (a click, or skipping)
    // Animations off: they show how they end and looping ones stand still, and so do the weather (where it is), a glitch's
    // strips and the noise's grain; effects come and go at once.
    void setStill(bool still) { still_ = still; }

    // `labels`: write the expected file name on placeholders (setting developer yes). `frameOf` says which
    // eyes and mouth each object shows (blinking and talking atlases).
    using FrameOf = std::function<Frame(const ObjectState&)>;
    void draw(SDL_Renderer* renderer, Assets& assets, const Font& labelFont, bool labels, float characterHeight, const FrameOf& frameOf);

    [[nodiscard]] const std::string& background() const { return background_; }
    [[nodiscard]] std::vector<ObjectState> objects() const;

private:
    const Canvas& canvas; // the game's size
    [[nodiscard]] SDL_FRect whole() const { return {0.0f, 0.0f, canvas.w, canvas.h}; } // (all of it)
    static constexpr std::size_t kTweened = 8; // x, y, zoom, opacity, rotation, tint (red, green, blue)

    // An image's animations: its own (while it's shown) and one played on it (on top), and how far
    // into each. Their time goes on to their end, or round and round (kept past one time through,
    // which is what waiting for a played one waits for).
    struct Animations {
        std::shared_ptr<const Animation> own, played;
        float ownTime = 0.0f, playedTime = 0.0f;
        Animation::Look playedFrom; // where the played one starts (the camera's: where the one before left it)
        void set(std::shared_ptr<const Animation> animation, bool isOwn, bool done); // (its own, the same one: goes on)
        void onward(float dt, bool still);
        void finish();                    // (skipping: the ones that end, at their end; a played loop past one time through)
        [[nodiscard]] bool going() const; // a played one hasn't ended (one time through, if it plays again and again)
        // How they have it look (`tint`: where a colour starts from; places and amounts as Animation::at's),
        // and how the played one alone has it.
        [[nodiscard]] Animation::Look look(SDL_Color tint, const Animation::Offset& offset, SDL_FPoint screen) const;
        [[nodiscard]] Animation::Look playedLook(SDL_Color tint, const Animation::Offset& offset, SDL_FPoint screen) const;
    };

    struct Object {
        ObjectState s;
        CharacterDef def; // for characters: image prefix, placeholder colour and name
        int order = 0;    // draw order within a layer
        bool leaving = false;  // hidden: going once its animation ends
        bool oldBackground = false; // there when the background changed: drawn with the one before, and gone with it
        std::array<float, kTweened> from{}, to{};
        float tweenTime = 1.0f, tweenDuration = 0.0f;
        Ease ease = Ease::InOut;
        Animations animations; // (its own, animation=; and the one its line plays, with)
    };

    Object* find(std::string_view tag);
    void apply(Object& o, const StageProps& props, float seconds, Ease ease = Ease::InOut); // 0 seconds: at once
    void dropPrevious(); // the new background has come in: the one before goes, with what was on stage then
    // The background (`previous`: the one going, while a new one comes in), as its animations have it.
    void drawBackground(SDL_Renderer* r, Assets& assets, const Font* font, bool previous);
    void drawObject(SDL_Renderer* r, Assets& assets, const Font* font, const Object& o, float characterHeight, Frame frame);
    // How an image's animations have it look now, drawn at `box` (where it is without them): moved (in
    // pixels), faded, zoomed, turned, tinted (from `tint`), covered with a colour, wiped.
    [[nodiscard]] Animation::Look lookOf(const Animations& animations, SDL_Color tint, const SDL_FRect& box) const;
    // An image drawn at `box` covered `amount` of the way with a colour: its shape filled with it.
    // (Where the graphics can't fill a shape: tinted towards it.)
    void drawCovered(SDL_Renderer* r, const Image& pic, const SDL_FRect& box, float angle, const SDL_FPoint& pivot, bool flip, SDL_Color colour,
                     float amount);
    TexturePtr cover_; // (what drawCovered fills, as big as the biggest so far)
    int coverW_ = 0, coverH_ = 0;
    Wiper wiper_;
    bool still_ = false;
    void drawStage(SDL_Renderer* r, Assets& assets, const Font* font, float characterHeight, const FrameOf& frameOf);
    void drawView(SDL_Renderer* r, Assets& assets, const Font* font, float characterHeight, const FrameOf& frameOf); // through the camera
    void drawWeather(SDL_Renderer* r, Assets& assets);
    void drawEffects(SDL_Renderer* r, SDL_Texture* view); // blur, pixelate, glitch over the stage drawn into `view`
    void drawNoise(SDL_Renderer* r, float alpha);
    void updateWeather(float dt);
    void updateEffects(float dt);
    // An image as the colour effects show it (fading from or to as it is), and a colour the same way.
    // A tint multiplies an image's colours, so it turns grey or brown with them but isn't inverted
    // (white, no tint, would make everything black).
    void paint(Assets& assets, std::string_view name, Frame frame, const std::function<void(const Image&, float)>& draw) const;
    [[nodiscard]] SDL_Color shade(SDL_Color c, bool tint = false) const;
    // A texture to draw into, w x h pixels (made again when the size changes).
    SDL_Texture* canvasTexture(SDL_Renderer* r, TexturePtr& texture, int w, int h, SDL_ScaleMode mode = SDL_SCALEMODE_LINEAR);
    TextBlock& label(const Font& font, const std::string& text);

    std::string background_, previous_;
    Animations backgroundAnimations_, previousAnimations_; // (the one going goes on as it was, while the new one comes in)
    std::vector<Object> objects_;
    int nextOrder_ = 0;
    float clock_ = 0.0f;

    struct Particle {
        float x = 0, y = 0, vx = 0, vy = 0, size = 1, angle = 0, spin = 0, phase = 0, sway = 0;
        SDL_Color colour{255, 255, 255, 255};
    };
    cmd::Weather weather_, fading_;       // the weather, and the one fading out
    std::vector<Particle> particles_, fadingParticles_;
    float weatherAlpha_ = 0.0f, fadingAlpha_ = 0.0f, weatherFade_ = 1.0f;
    std::mt19937 rng_{12345}; // (always the same seed: the weather looks the same each time)
    void spawn(const cmd::Weather& w, std::vector<Particle>& out);

    Animations camera_;
    [[nodiscard]] Animation::Offset cameraPlaces() const;
    [[nodiscard]] Animation::Look cameraLook() const; // (how its animations have it now)
    [[nodiscard]] Animation::Look cameraStartFor(const Animation& next) const;
    Wiper cameraWiper_;
    TexturePtr drawn_; // (the stage drawn, for the camera to move)

    // Effects: the ones that stay and the ones for a while, how strong each is now (0..1, fading
    // toward on or off), and the colour ones being drawn (they change from one to another at once).
    unsigned looks_ = 0, burst_ = 0;
    float burstTime_ = 0.0f, lookAmount_ = 1.0f, burstAmount_ = 1.0f, lookFade_ = 0.0f;
    std::array<float, kStageLooks.size()> strength_{};
    [[nodiscard]] float strength(StageLook look) const { return strength_[static_cast<std::size_t>(std::countr_zero(static_cast<unsigned>(look)))]; }
    unsigned colourMask_ = 0;
    float colourAlpha_ = 0.0f;
    TexturePtr view_, small_, smaller_, blocks_, noise_; // the stage drawn whole; blurred; pixelated; TV static
    struct Strip {
        float y, h, dx;
        SDL_Color tint;
    };
    std::vector<Strip> strips_; // a glitch's, for a moment
    float glitchUntil_ = 0.0f;
    SDL_FPoint noiseAt_{-1.0f, -1.0f}; // the part of the noise shown (still: the same one; none yet: below 0)
    std::unordered_map<std::string, TextBlock, StringHash, std::equal_to<>> labels_;
};

} // namespace vn
