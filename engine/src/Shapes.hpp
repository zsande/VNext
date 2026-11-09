#pragma once

// The basic shapes everything is drawn with, and wipes.

#include "Basics.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vn {

class Assets;

namespace draw {

void rect(SDL_Renderer* r, const SDL_FRect& rect, SDL_Color c);
void outline(SDL_Renderer* r, const SDL_FRect& rect, SDL_Color c);
void gradient(SDL_Renderer* r, const SDL_FRect& rc, SDL_Color top, SDL_Color bottom);
void circle(SDL_Renderer* r, float cx, float cy, float radius, SDL_Color c);
void quad(SDL_Renderer* r, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color colour);
// A box with round corners `radius` across, smoothed over a pixel (on whole pixels, sharp along its straight
// edges), or over `soft` pixels all round (a shadow's).
void roundRect(SDL_Renderer* r, const SDL_FRect& given, float radius, SDL_Color c, float soft = 1.0f);
SDL_Color hsv(float h, float s, float v, Uint8 alpha = 255);

} // namespace draw

// Something drawn with only part of it showing, as an animation's wipe has it (Animation::Look::wipe):
// `shown` of it, straight from the left of `box`, or where the image `shape` is darkest first (its
// pixels stretched over `box`, softly at the edge). paint() draws it as it is; `canvas`: the game's
// size, what the drawing's places are in. Where the graphics can't shape it, a straight one.
class Wiper {
public:
    Wiper() = default;
    Wiper(const Wiper&) VN_DELETED("its textures are its own");
    Wiper& operator=(const Wiper&) VN_DELETED("its textures are its own");
    Wiper(Wiper&&) = default;
    Wiper& operator=(Wiper&&) = default;
    ~Wiper() = default;
    void draw(SDL_Renderer* r, Assets& assets, SDL_FPoint canvas, const SDL_FRect& box, float shown, const std::string& shape,
              const std::function<void()>& paint);

private:
    // A shape's image as read (at most 480 across): how bright each pixel is, the mask made of them, and
    // how much that shows now. (No texture: the image can't be read, or the graphics can't cut with it.)
    struct Mask {
        std::vector<Uint8> brightness;
        int w = 0, h = 0;
        TexturePtr texture;
        float shown = -1.0f;
    };
    Mask* maskOf(SDL_Renderer* r, Assets& assets, const std::string& shape); // (each read once: several can wipe at a time; none: a straight wipe)
    std::map<std::string, Mask, std::less<>> masks_;
    TexturePtr layer_; // (what's wiped in a shape is drawn here first)
    int layerW_ = 0, layerH_ = 0;
};

} // namespace vn
