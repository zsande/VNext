#pragma once

// The game's images: found by name (and its sounds' and movies' files), loaded, cut from atlases, drawn.

#include "Files.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vn {

// One image holding every look of a character: a grid of equal frames ("atlas" lines). Each
// expression has a frame for every eyes + mouth pair; the mouth changes first, then the eyes,
// then the expression, reading left to right and row by row. The eyes and the mouth can instead
// be parts: small images of just them, drawn over the expression's frame.
struct Atlas {
    // Eyes or a mouth drawn over every frame: a grid with a row per expression and a column per
    // state (the same order as their names).
    struct Part {
        std::string image;
        float x = 0.0f, y = 0.0f; // where its top left goes on the frame, in the frame's pixels
    };
    std::string name;                     // scripts show "<name>" and "<name>_<expression>"
    std::string image;                    // the grid: images/<image>.png
    int columns = 0;                      // frames across; 0: a row per expression (one row of them all when each has one frame)
    std::vector<std::string> expressions; // the first is the default
    std::vector<std::string> eyes;        // open first, closed last (blinks go through them)
    std::vector<std::string> mouth;       // closed first, widest last (talking goes through them)
    std::optional<Part> eyesPart, mouthPart;
};

// A part of an image with a name of its own ("sub-image" lines), shown like any image: a logo on a
// sheet of a menu's pieces.
struct SubImage {
    std::string name;  // lower-case
    std::string image; // the whole image: images/<image>
    SDL_FRect rect{};  // the part, in its pixels
};

// How closed the eyes and how open the mouth are (0..1); an atlas shows its nearest frame.
struct Frame {
    float eyes = 0.0f;
    float mouth = 0.0f;
};

// How the whole stage looks ("effect" lines): bits of a mask, 0 is as it is. The colour ones are
// images changed pixel by pixel; the others are drawn over the stage, but vibration, which is felt: the
// controllers rumble as strongly as it is.
enum StageLook : unsigned {
    kLookGreyscale = 1, kLookSepia = 2, kLookInvert = 4, kLookBlur = 8, kLookPixelate = 16, kLookNoise = 32, kLookGlitch = 64, kLookVibration = 128
};
inline constexpr std::array<std::string_view, 8> kStageLooks{"greyscale", "sepia", "invert", "blur", "pixelate", "noise", "glitch", "vibration"};
inline constexpr unsigned kColourLooks = kLookGreyscale | kLookSepia | kLookInvert;
// A colour with the colour effects of `looks` (greyscale or sepia first, then invert).
[[nodiscard]] SDL_Color looked(SDL_Color c, unsigned looks);

class Assets;

// A whole image, or one frame of an atlas (with its eyes and mouth parts on top).
struct Image {
    struct Overlay {
        SDL_Texture* texture = nullptr;
        SDL_FRect src{};
        SDL_FPoint at{}; // its top left on the image, in the image's pixels
    };
    SDL_Texture* texture = nullptr;
    SDL_FRect src{};
    bool frame = false; // part of an atlas (drawn half a pixel in, so neighbours don't bleed in)
    std::array<Overlay, 2> overlays{};
    std::size_t overlayCount = 0;
    Assets* assets = nullptr; // for see-through drawing of images with parts

    explicit operator bool() const { return texture != nullptr; }
    [[nodiscard]] float w() const { return src.w; }
    [[nodiscard]] float h() const { return src.h; }
    // `part` of it, in its own pixels (without eyes and mouth parts).
    [[nodiscard]] Image crop(const SDL_FRect& part) const;
    // `tint` multiplies its colours (black: a silhouette).
    void draw(SDL_Renderer* r, const SDL_FRect& dst, float alpha, double angle = 0.0, const SDL_FPoint* pivot = nullptr,
              bool flip = false, SDL_Color tint = {255, 255, 255, 255}) const;

private:
    void drawLayers(SDL_Renderer* r, const SDL_FRect& dst, float alpha, double angle, const SDL_FPoint* pivot, bool flip, SDL_Color tint) const;
};

// Finds files in the game folder and caches images. Scripts name files without folder or
// extension ("theme" is sounds/theme.ogg or .mp3...), but full paths work too.
class Assets {
public:
    Assets(SDL_Renderer* renderer, std::string root, const GameFiles& files);
    [[nodiscard]] const GameFiles& files() const { return files_; }

    [[nodiscard]] const std::string& root() const { return root_; }
    [[nodiscard]] std::string path(std::string_view relative) const { return root_ + std::string(relative); }
    // A translation's id: its own images (images/<id>/), sounds (sounds/<id>/) and movies (movies/<id>/) are used first.
    // Forgets loaded images when it changes.
    void setLanguage(std::string id);
    // The sound file a script means: sounds/<name>.ogg/.mp3/.flac/.wav, or `name` as written.
    // A missing file gives the path it was expected at (the player then makes up a placeholder).
    [[nodiscard]] std::string sound(std::string_view name) const;
    // The sound file a script means, if it exists.
    [[nodiscard]] std::optional<std::string> findSound(std::string_view name) const;
    // Whether images/ (or the language's) has an image by this name (not an atlas's frame).
    [[nodiscard]] bool hasImageFile(std::string_view name) const;
    // Whether there's an image by this name: a file, a sub-image, or an atlas or one of its expressions (without the
    // warning a missing one gets when drawn).
    [[nodiscard]] bool hasImage(std::string_view name) const {
        const std::string key = lower(name);
        return frames_.contains(key) || subImages_.contains(key) || hasImageFile(name);
    }
    // The movie a script means: movies/<name>.mpg (or .mpeg, .mp4, .mov, .m4v), or `name` as written.
    [[nodiscard]] std::optional<std::string> findMovie(std::string_view name) const;
    // Its subtitles: movies/<name>.srt (the language's own first), if there are any.
    [[nodiscard]] std::optional<std::string> findSubtitles(std::string_view movie) const;

    // The images atlases add: "<name>" and "<name>_<expression>".
    void setAtlases(std::vector<Atlas> atlases);
    void setSubImages(const std::vector<SubImage>& parts);
    // Sharp pixels when images are scaled (pixel art), else smooth. Forgets loaded images.
    void setPixelArt(bool pixelArt);
    // images/<name>.png (or another of kImageTypes), an atlas frame or a sub-image; empty when missing.
    // `looks`: with colour effects (a copy, made when first needed).
    Image image(std::string_view name, Frame frame = {}, unsigned looks = 0);
    // The image file itself, never an atlas or a sub-image of that name (an atlas mostly has its image's name):
    // what the Library shows of a file, and what sub-images are cut from.
    Image file(std::string_view name, unsigned looks = 0);
    // The image's pixels (RGBA32), for reading them (an image-shaped wipe).
    [[nodiscard]] SurfacePtr pixels(std::string_view name) const;
    // Loads an image (with colour effects) before it's needed; false if it already was.
    bool preload(std::string_view name, unsigned looks = 0);
    // Images not drawn for a while are let go when they take more than this (0: no limit).
    void setMemoryLimit(std::size_t bytes) { limit_ = bytes; }
    void beginFrame() { ++frame_; }
    void clear() {
        images_.clear();
        ownNames_.clear();
        used_ = 0;
    }
    // A texture to put an image together in, at least w x h (see Image::draw).
    SDL_Texture* scratch(int w, int h);

private:
    struct LoadedImage {
        TexturePtr texture; // null: missing (remembered, so placeholders don't hit the disk every frame)
        std::size_t bytes = 0;
        std::uint64_t lastFrame = 0;
    };
    [[nodiscard]] bool exists(const std::string& path) const;
    [[nodiscard]] std::optional<std::string> find(std::string_view folder, std::string_view name, std::span<const std::string_view> types) const;
    // The file's own name for what scripts call it: as written, or one with spaces, dots... in its
    // name ("19. YOMI" for 19_YOMI).
    [[nodiscard]] std::string ownName(const std::string& folder, std::string_view name, std::span<const std::string_view> types) const;
    mutable std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> ownNames_; // by folder: script name -> its own (looked up once)
    [[nodiscard]] SurfacePtr filePixels(std::string_view name) const;
    [[nodiscard]] std::string imageFolder(std::string_view key) const; // images/, or the language's own when it has that image
    SDL_Texture* loadTexture(std::string_view name, unsigned looks = 0);
    Image frameOrFile(std::string_view name, Frame frame, unsigned looks); // an atlas's frame, else the file
    void trim();

    SDL_Renderer* renderer_;
    std::string root_;
    const GameFiles& files_; // (the engine's)
    std::unordered_map<std::string, LoadedImage, StringHash, std::equal_to<>> images_;
    std::vector<Atlas> atlases_;
    std::unordered_map<std::string, SubImage, StringHash, std::equal_to<>> subImages_; // by lower-case name
    bool pixelArt_ = false;
    std::string language_; // a translation's folder, with a slash ("spanish/"), or empty
    // lower-case image name -> atlas and expression
    std::unordered_map<std::string, std::pair<std::size_t, std::size_t>, StringHash, std::equal_to<>> frames_;
    std::size_t used_ = 0, limit_ = 0; // bytes of images loaded, and the most wanted
    std::uint64_t frame_ = 0;
    TexturePtr scratch_;
};

} // namespace vn
