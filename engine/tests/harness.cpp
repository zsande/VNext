// Checks that drive the player as the editor and the game would (no window: SDL's dummy drivers).
// Build: sh build.sh (after building the player); run: sh run.sh ../build.nosync/tests/harness DIR [names,live,langcode,relaunch,chosen,writing,sheet,drop,unused,caches,bidi,signing,packed,preview,nomessages,forms,licenses,resize,starts,stray,parts,newgame,scenes,character,lines,translation,map,library,letters,emptied,wheel,settings,astext,playon,boxes,original,animations,gradient,buttons,silence,scrolling,dragging]
// where DIR is a scratch folder for the games it makes, and the second argument picks sections.
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <deque>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
// The developer files, included rather than linked, so their own helpers can be checked too (the
// screens to start from, the units in the scripts...).
#include "Developer.cpp"
#include "DeveloperForms.cpp"
#include "DeveloperLibrary.cpp"
#include "DeveloperMap.cpp"
#include "DeveloperPreview.cpp"
#include "DeveloperScreens.cpp"
#include "DeveloperShared.cpp"
#include "DeveloperStory.cpp"
#include "DeveloperTranslations.cpp"
#include "DeveloperUi.cpp"
#include "StoryEditor.cpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
// The checks, as one struct: the player's classes name it a friend (friend struct ::Harness), so the
// checks can look inside them.
struct Harness {
static inline int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static std::string read(const std::string& p) { std::ifstream in(p); return {std::istreambuf_iterator<char>(in), {}}; } // (as it is: a sheet's BOM and CRs too)
// (screens in the checks are written in 1280 x 720 pixels: they go in the scripts as places and shares)
static std::string units(const std::string& text) {
    std::string out;
    for (const auto& l : vn::placesFromPixels(vn::linesOf(text), 1280.0f, 720.0f)) out += (out.empty() ? "" : "\n") + l;
    return out;
}
static void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file) << (file.extension() == ".vnscript" ? units(text) : text);
}

// `seconds` of silence (8-bit mono at 8 kHz) as a wav file: a sound, a song or a voice to play.
static void silentWav(const std::string& file, float seconds = 0.1f) {
    std::string wav = "RIFF";
    auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) wav.push_back(static_cast<char>((v >> (8 * i)) & 0xffu)); };
    auto u16 = [&](std::uint16_t v) { for (int i = 0; i < 2; ++i) wav.push_back(static_cast<char>((v >> (8 * i)) & 0xffu)); };
    const auto samples = static_cast<std::uint32_t>(8000.0f * seconds);
    u32(36 + samples);
    wav += "WAVEfmt ";
    u32(16), u16(1), u16(1), u32(8000), u32(8000), u16(1), u16(8);
    wav += "data";
    u32(samples);
    wav.append(samples, static_cast<char>(128));
    std::ofstream(file, std::ios::binary) << wav;
}

struct Driver {
    vn::Engine engine;
    std::string out;
    void frames(int n = 3) { for (int i = 0; i < n; ++i) engine.iterate(); }
    void shot(const std::string& name) { engine.screenshotRequest_ = out + name; engine.iterate(); }
};

static std::unique_ptr<Driver> start(const std::string& out, const std::string& game, const std::string& data) {
    auto d = std::make_unique<Driver>();
    d->out = out;
    std::vector<std::string> args{"VNext", "--game", game, "--data", data};
    std::vector<char*> av;
    for (auto& a : args) av.push_back(a.data());
    CHECK(d->engine.init(static_cast<int>(av.size()), av.data()) == SDL_APP_CONTINUE);
    d->frames(3);
    return d;
}

// The colour of a pixel of the frame just drawn (the window is the game's size: 1280 x 720).
static SDL_Color pixel(Driver& dr, int x, int y) {
    const std::string file = dr.out + "pixel.png";
    std::filesystem::remove(file);
    dr.shot("pixel.png");
    SDL_Color c{0, 0, 0, 0};
    if (SDL_Surface* s = IMG_Load(file.c_str())) {
        const float k = std::min(static_cast<float>(s->w) / 1280.0f, static_cast<float>(s->h) / 720.0f); // (the game's canvas, scaled to fit the window, in its middle)
        const float left = (static_cast<float>(s->w) - 1280.0f * k) / 2, top = (static_cast<float>(s->h) - 720.0f * k) / 2;
        SDL_ReadSurfacePixel(s, static_cast<int>(left + static_cast<float>(x) * k), static_cast<int>(top + static_cast<float>(y) * k), &c.r, &c.g, &c.b, &c.a);
        SDL_DestroySurface(s);
    }
    return c;
}
static bool near(SDL_Color c, int r, int g, int b) { return std::abs(c.r - r) < 40 && std::abs(c.g - g) < 40 && std::abs(c.b - b) < 40; }

// The mouse moved, pressed or let go, at a point of the canvas (the event carries window coordinates:
// the player turns them back into the canvas's).
static void mouse(Driver& dr, Uint32 type, float x, float y) {
    SDL_RenderCoordinatesToWindow(dr.engine.renderer_.get(), x, y, &x, &y);
    SDL_Event e{};
    e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        e.motion.windowID = SDL_GetWindowID(dr.engine.window_.get());
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.button.windowID = SDL_GetWindowID(dr.engine.window_.get());
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
    }
    dr.engine.event(e);
}

// The mouse wheel turned by `turns` (down: negative) at (x, y) (the game's coordinates); `sideways`: a trackpad's, to the right.
static void wheel(Driver& dr, float turns, float x, float y, float sideways = 0.0f) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.windowID = SDL_GetWindowID(dr.engine.window_.get());
    e.wheel.y = turns;
    e.wheel.x = sideways;
    SDL_RenderCoordinatesToWindow(dr.engine.renderer_.get(), x, y, &e.wheel.mouse_x, &e.wheel.mouse_y);
    dr.engine.event(e);
}

// The mouse moved to (x, y) (the game's coordinates) with the left button down.
static void motion(Driver& dr, float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    SDL_RenderCoordinatesToWindow(dr.engine.renderer_.get(), x, y, &e.motion.x, &e.motion.y);
    e.motion.windowID = SDL_GetWindowID(dr.engine.window_.get());
    e.motion.state = SDL_BUTTON_LMASK;
    dr.engine.event(e);
}

static void key(Driver& dr, SDL_Keycode code) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = code;
    e.key.down = true;
    e.key.windowID = SDL_GetWindowID(dr.engine.window_.get());
    dr.engine.event(e);
}

// A box typed into: `now` in it (the caret at its end), then `text` a byte at a time, then left.
static void typeBox(Driver& dr, vn::DeveloperTools& d, const std::string& id, const std::string& text, const std::string& now = {}) {
    d.focusOn(id, now);
    for (const char c : text) {
        d.typed = std::string(1, c);
        dr.frames(1);
    }
    d.focus.clear();
    dr.frames(2);
}

// A click at a point of the canvas: the button pressed, then let go there.
static void click(Driver& dr, float x, float y) {
    mouse(dr, SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
    mouse(dr, SDL_EVENT_MOUSE_BUTTON_UP, x, y);
}

static int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::printf("usage: harness DIR [names,live,langcode,relaunch,chosen,writing,sheet,drop,unused,caches,bidi,signing,packed,preview,nomessages,forms,licenses,resize,starts,stray,parts,newgame,scenes,character,lines,translation,map,library,letters,emptied,wheel,settings,astext,playon,boxes,original,animations,gradient,buttons,silence,scrolling,dragging]\n");
        return 2;
    }
    const std::string S = argv[1];
    const std::string out = S + "/h/";
    fs::create_directories(out);
    const std::string only = argc > 2 ? argv[2] : "";
    auto want = [&](std::string_view name) { return only.empty() || std::ranges::contains(std::views::split(std::string_view(only), ','), name, [](auto&& p) { return std::string_view(p.begin(), p.end()); }); };
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    using View = vn::DeveloperTools::View;

    if (want("animations")) {
        std::printf("animations. show_animation= and hide_animation= play when a button opens or closes a screen (in the game, the preview and Try it); fade= covers everything like a screen fade; one that opens from a colour waits for the one under it to fade out, which fades back in after\n");
        const std::string g = S + "/h/animations";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"A\"\nconfig developer yes\ninclude screens\nanimation pop_in\n    opacity=0 over 0 seconds\n    opacity=1 over 0.4 seconds\n"
                                            "animation pop_out\n    opacity=0 over 0.4 seconds\nanimation from_black\n    fade=black over 0 seconds\n    fade=none over 0.4 seconds\n"
                                            "animation to_black\n    fade=black over 0.4 seconds\n\nlabel start\n    open title\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen title show_animation=from_black hide_animation=to_black\n    image #203040 x=0 y=0 w=1280 h=720\n"
                                               "    button \"Options\" x=100 y=100 w=200 h=60 action=\"open options\"\n"
                                               "screen options show_animation=from_black hide_animation=to_black\n    image back x=0 y=0 w=1280 h=720 fit=cover\n"
                                               "    button \"Back\" x=100 y=100 w=200 h=60 action=close\n"
                                               "screen quit show_animation=pop_in hide_animation=pop_out\n    image #c08040 x=0 y=0 w=1280 h=720\n");
        fs::create_directories(g + "/images"); // (its background a JPEG: no alpha of its own, and it fades all the same)
        if (SDL_Surface* pic = SDL_CreateSurface(64, 48, SDL_PIXELFORMAT_RGBA32)) {
            SDL_FillSurfaceRect(pic, nullptr, 0xff4080c0u);
            IMG_SaveJPG(pic, (g + "/images/back.jpg").c_str(), 95);
            SDL_DestroySurface(pic);
        }
        fs::remove_all(S + "/h/animations_data");
        auto dr = start(out, g, S + "/h/animations_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty());
        auto wait = [&](int ms) { // (real time passing, a frame at a time: a frame's step is capped)
            for (int t = 0; t < ms; t += 50) {
                SDL_Delay(50);
                dr->frames(1);
            }
        };
        // fade= steps: linear unless an ease is said, half covered at half time.
        const vn::Animation& toBlack = e.script_.animations.at("to_black");
        const auto half = toBlack.at(0.2f, {255, 255, 255, 255}, [](bool, float) { return 0.0f; });
        CHECK(toBlack.steps.size() == 1 && toBlack.steps[0].ease == vn::Ease::Linear && std::abs(half.fade - 0.5f) < 0.02f && half.fadeColor.r == 0 && half.fadeColor.g == 0);
        CHECK(std::string(vn::kStepKindList).contains("Fade to a colour"));
        // The game: the title fades in from black. Quit (fading in by itself) opens over it at once and fades out on close.
        e.openTitle();
        e.waiting_ = false; // (a game going on, not the editor waiting: the title is drawn)
        d.open = false;
        dr->frames(1);
        const SDL_Color titleFirst = pixel(*dr, 640, 360);
        wait(600);
        CHECK(titleFirst.r < 0x30 && near(pixel(*dr, 640, 360), 0x20, 0x30, 0x40));
        e.open("quit");
        dr->frames(1);
        const SDL_Color opening = pixel(*dr, 640, 360);
        CHECK(e.overlays_ == std::vector<std::string>{"quit"}); // (at once: it doesn't start covered)
        wait(600);
        const SDL_Color opened = pixel(*dr, 640, 360);
        e.close();
        dr->frames(1);
        const SDL_Color closing = pixel(*dr, 640, 360);
        wait(600);
        const SDL_Color closed = pixel(*dr, 640, 360);
        std::printf("   the game, quit: opening %02x%02x%02x, opened %02x%02x%02x, closing %02x%02x%02x, closed %02x%02x%02x\n", opening.r, opening.g, opening.b, opened.r,
                    opened.g, opened.b, closing.r, closing.g, closing.b, closed.r, closed.g, closed.b);
        CHECK(near(opening, 0x20, 0x30, 0x40) && near(opened, 0xc0, 0x80, 0x40) && near(closing, 0xc0, 0x80, 0x40) && near(closed, 0x20, 0x30, 0x40));
        // Options opens from black: the title fades out to black first (it waits), then options fades in. Back: options fades to black, then the title fades in again.
        auto until = [&](auto cond, int ms) { // (frames until it holds, or the time is up)
            for (int t = 0; t < ms && !cond(); t += 20) {
                SDL_Delay(20);
                dr->frames(1);
            }
            return cond();
        };
        e.open("options");
        dr->frames(1);
        CHECK(e.overlays_.empty() && e.view("title")->hasLeft()); // (not open yet: the title is fading out)
        wait(100);
        const SDL_Color titleGoing = pixel(*dr, 640, 360);
        CHECK(until([&] { return !e.overlays_.empty(); }, 1500) && e.overlays_ == std::vector<std::string>{"options"}); // (open once the title has faded out)
        const SDL_Color optionsComing = pixel(*dr, 640, 360); // (just opened: covered, fading in)
        wait(800);
        const SDL_Color optionsThere = pixel(*dr, 640, 360);
        std::printf("   the game, options: title fading out %02x%02x%02x, options coming %02x%02x%02x, there %02x%02x%02x\n", titleGoing.r, titleGoing.g, titleGoing.b, optionsComing.r,
                    optionsComing.g, optionsComing.b, optionsThere.r, optionsThere.g, optionsThere.b);
        CHECK(titleGoing.b < 0x3c && titleGoing.b > 0x06 && optionsComing.r < 0x60 && near(optionsThere, 0xc0, 0x80, 0x40));
        e.close();
        wait(100);
        const SDL_Color optionsGoing = pixel(*dr, 640, 360);
        CHECK(until([&] { return e.overlays_.empty(); }, 1500) && !e.view("title")->hasLeft()); // (closed: the title comes back, its show_animation= again)
        const SDL_Color titleComing = pixel(*dr, 640, 360);
        wait(800);
        const SDL_Color titleBack = pixel(*dr, 640, 360);
        std::printf("   the game, back: options fading out %02x%02x%02x, title coming %02x%02x%02x, back %02x%02x%02x\n", optionsGoing.r, optionsGoing.g, optionsGoing.b, titleComing.r,
                    titleComing.g, titleComing.b, titleBack.r, titleBack.g, titleBack.b);
        CHECK(optionsGoing.r < 0xb8 && optionsGoing.r > 0x20 && titleComing.b < 0x30 && near(titleBack, 0x20, 0x30, 0x40));
        // The Story tab's preview at the line that opens the title: the same waiting, with the closed screen drawn until its animation is over.
        d.open = true;
        d.view = View::Story;
        dr->frames(3);
        const auto rows = d.editor.rows(d.editor.scenes().front());
        d.row = rows[0].line;
        dr->frames(3);
        CHECK(d.preview && d.script);
        e.tried_.push_back(vn::cmd::Ui{.verb = "open", .arg = "options"}); // (as its button does)
        e.developer_.previewDid();
        CHECK(d.previewPending == "options" && d.previewOverlays.empty() && e.developer_.previewView("title")->hasLeft()); // (the title fades out first)
        wait(600);
        vn::ScreenView* options = e.developer_.previewView("options");
        CHECK(d.previewPending.empty() && d.previewOverlays == std::vector<std::string>{"options"} && options && options->shownSince_);
        e.tried_.push_back(vn::cmd::Ui{.verb = "close"});
        e.developer_.previewDid();
        CHECK(d.previewOverlays.empty() && d.previewLeaving == std::vector<std::string>{"options"} && options->leaving());
        dr->frames(2);
        CHECK(d.previewLeaving == std::vector<std::string>{"options"}); // (still drawn: its animation runs)
        wait(600);
        CHECK(d.previewLeaving.empty() && !e.developer_.previewView("title")->hasLeft()); // (the title comes back)
        // The Screens tab's Try it on the title: options opens over it the same way; closing the title itself fades it, then Try it stops.
        d.view = View::Screens;
        d.screenName = "title";
        d.part.reset();
        dr->frames(3);
        d.trying = true; // (as Try it does)
        d.trySettings = e.settings_;
        d.sampleMade = false;
        dr->frames(3);
        CHECK(d.trying && d.tryScreen == "title");
        e.tried_.push_back(vn::cmd::Ui{.verb = "open", .arg = "options"});
        dr->frames(2);
        CHECK(d.tryPending == "options" && d.tryOverlays.empty() && d.screenView->hasLeft());
        wait(600);
        std::printf("   Try it: opened over it '%s', status '%s'\n", d.tryOverlays.empty() ? "-" : d.tryOverlays.back().c_str(), d.status.c_str());
        CHECK(d.tryPending.empty() && d.tryOverlays == std::vector<std::string>{"options"} && d.screenName == "title");
        e.tried_.push_back(vn::cmd::Ui{.verb = "close"});
        dr->frames(2);
        CHECK(d.tryOverlays.empty() && d.tryLeaving == std::vector<std::string>{"options"});
        wait(600);
        CHECK(d.tryLeaving.empty() && d.trying && !d.screenView->hasLeft());
        e.tried_.push_back(vn::cmd::Ui{.verb = "close"});
        dr->frames(2);
        CHECK(d.tryClosing && d.trying);
        wait(600);
        CHECK(!d.trying);
        // Another tab chosen while it's tried, with a screen opened over it: Try it stops, and what it opened goes with it.
        d.trying = true;
        d.trySettings = e.settings_;
        d.sampleMade = false;
        dr->frames(3);
        e.tried_.push_back(vn::cmd::Ui{.verb = "open", .arg = "options"});
        dr->frames(2);
        wait(600);
        CHECK(d.trying && d.tryOverlays == std::vector<std::string>{"options"});
        click(*dr, vn::layout::margin + 10.0f, vn::layout::barH / 2.0f); // (the first tab: Story)
        dr->frames(2);
        CHECK(d.view == View::Story && !d.trying && d.tryOverlays.empty() && d.tryLeaving.empty() && d.tryPending.empty() && d.tryScreen.empty());
    }

    if (want("gradient")) {
        std::printf("gradient. a colour fading down to another is one value, navy-black: drawn so by a scene, an image part and a style's bg=; the colour's box shows it, its popup makes it\n");
        const std::string g = S + "/h/gradient";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"G\"\nconfig developer yes\ninclude screens\nstyle grad bg=white-black\n\nlabel start\n    scene navy bottom=black\n    scene white\n    open panel\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    image navy-black x=0 y=0 w=1280 h=360\n    container x=0 y=360 w=1280 h=360 style=grad\n");
        fs::remove_all(S + "/h/gradient_data");
        auto dr = start(out, g, S + "/h/gradient_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty()); // (navy-black is a colour, not a missing picture)
        // The game: the image part fades navy to black down its half; the style's box white to black down the other.
        e.newGame("start");
        d.open = false;
        for (int n = 0; n < 20 && !near(pixel(*dr, 640, 5), 0x14, 0x1e, 0x5a); ++n) dr->frames(2);
        const SDL_Color top = pixel(*dr, 640, 5), low = pixel(*dr, 640, 350), boxTop = pixel(*dr, 640, 368), boxLow = pixel(*dr, 640, 715);
        std::printf("   drawn: image %02x%02x%02x down to %02x%02x%02x, style box %02x%02x%02x down to %02x%02x%02x\n", top.r, top.g, top.b, low.r, low.g, low.b, boxTop.r, boxTop.g,
                    boxTop.b, boxLow.r, boxLow.g, boxLow.b);
        CHECK(near(top, 0x14, 0x1e, 0x5a) && low.b < 0x18 && boxTop.r > 0xe0 && boxLow.r < 0x18);
        // The Story tab: the scene line's box holds navy-black (bottom= read as that), written back as one word; the box says what it is.
        d.open = true;
        d.view = View::Story;
        dr->frames(3);
        const auto rows = d.editor.rows(d.editor.scenes().front());
        auto fields = d.editor.fields(rows[0]);
        const auto image = std::ranges::find(fields, std::string("Image"), &vn::StoryEditor::Field::name);
        CHECK(image != fields.end() && image->value == "navy-black" && std::ranges::none_of(fields, [](const auto& f) { return f.name == "Bottom"; }));
        CHECK(d.editor.lineOf(rows[0].kind, fields) == "scene navy-black");
        CHECK(vn::colorWords("navy-black") == "navy down to black" && vn::colorWords("#203040-#000000") == "like navy down to black");
        CHECK(vn::parseGradient("navy-black") && vn::parseColor("navy-black")->b == 90 && !vn::parseGradient("navy") && !vn::parseGradient("red-"));
        // Its popup (on the plain scene's box): Gradient makes one from the colour (the wheel on its bottom), One colour takes it back.
        d.row = rows[1].line;
        dr->frames(3);
        const std::string id = std::format("row:{}:Image", rows[1].line);
        d.openList = id;
        dr->frames(3);
        CHECK(d.popup.id == id);
        const float k = d.popup.at.h / vn::layout::boxH, pad = vn::layout::popupPad * k, codeH = vn::layout::codeH * k, itemH = vn::layout::itemH * k;
        const float bw = std::floor((d.popup.box.w - 2 * pad - 2 * pad) / 3), buttonsY = d.popup.box.y + pad + codeH + pad + (itemH - 2 * k) / 2;
        click(*dr, d.popup.box.x + pad + bw / 2, buttonsY); // (Gradient)
        dr->frames(3);
        std::printf("   popup: after Gradient '%s' (wheel on the %s), open %s\n", d.popup.field.value.c_str(), d.popup.editBottom ? "bottom" : "top", d.popup.id == id ? "still" : "no");
        CHECK(d.popup.id == id && d.popup.field.value == "white-white" && d.popup.editBottom);
        click(*dr, d.popup.box.x + pad + 2 * (bw + pad) + bw / 2, buttonsY); // (One colour)
        dr->frames(3);
        CHECK(d.popup.id == id && d.popup.field.value == "white" && !d.popup.editBottom);
        dr->shot("gradient.png");
    }

    if (want("original")) {
        std::printf("original. an image part with fit=original draws the picture at its own pixels, in the middle of the part, cut off at its edges\n");
        const std::string g = S + "/h/original";
        fs::remove_all(g);
        fs::create_directories(g + "/images");
        if (SDL_Surface* pic = SDL_CreateSurface(64, 48, SDL_PIXELFORMAT_RGBA32)) {
            SDL_FillSurfaceRect(pic, nullptr, 0xff4080c0u); // (orange, RGBA32: c0 80 40)
            IMG_SavePNG(pic, (g + "/images/pic.png").c_str());
            SDL_DestroySurface(pic);
        }
        write(g + "/scripts/main.vnscript", "config title \"O\"\nconfig developer yes\ninclude screens\n\nlabel start\n    open panel\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    image #203040 x=0 y=0 w=1280 h=720\n    image pic x=0 y=0 w=1280 h=720 fit=original\n"
                                               "    image pic x=1000 y=600 w=20 h=20 fit=original\n");
        fs::remove_all(S + "/h/original_data");
        auto dr = start(out, g, S + "/h/original_data");
        auto& d = dr->engine.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty());
        dr->engine.newGame("start"); // (the story from its start: the screen opens)
        d.open = false;              // (the game's own frame, not the editor over it)
        for (int n = 0; n < 30 && !near(pixel(*dr, 640, 360), 0xc0, 0x80, 0x40); ++n) dr->frames(2); // (the picture loads in its own time)
        const SDL_Color middle = pixel(*dr, 640, 360), beside = pixel(*dr, 560, 360), above = pixel(*dr, 640, 320), small = pixel(*dr, 1010, 610), past = pixel(*dr, 1030, 610);
        std::printf("   middle %02x%02x%02x, 80 left of it %02x%02x%02x, 40 above %02x%02x%02x; a 20 x 20 part: inside %02x%02x%02x, past it %02x%02x%02x\n", middle.r, middle.g, middle.b,
                    beside.r, beside.g, beside.b, above.r, above.g, above.b, small.r, small.g, small.b, past.r, past.g, past.b);
        CHECK(near(middle, 0xc0, 0x80, 0x40));                                      // (its own 64 x 48 pixels, in the middle)
        CHECK(near(beside, 0x20, 0x30, 0x40) && near(above, 0x20, 0x30, 0x40)); // (not stretched over the part)
        CHECK(near(small, 0xc0, 0x80, 0x40) && near(past, 0x20, 0x30, 0x40));   // (a part smaller than it: cut off at its edges)
        dr->shot("original.png");
        // The form offers it with the other fits.
        using Field = vn::StoryEditor::Field;
        Field fit{.box = vn::StoryEditor::Box::Fit};
        vn::applyBox(fit);
        CHECK(fit.choices == "contain cover stretch original" && fit.labels == "All of it shows|It fills the part|Stretched|Its own size");
    }

    if (want("scrolling")) {
        std::printf("scrolling. a container with scroll=yes scrolls what's in it, down or across, with the wheel, by dragging it or its scrollbar's thumb (scrollbar=yes); edges=cut cuts off what's past its box, edges=fade fades it out towards the edges\n");
        const std::string g = S + "/h/scrolling";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"C\"\nconfig developer yes\nconfig fade_size 64\ninclude screens\n\nlabel start\n    open panel\n    Hi.\n");
        const std::string four = "        button \"A\" x=0 y=0 w=380 h=100 bg=#ff0000 action=close\n        button \"B\" x=0 y=110 w=380 h=100 bg=#ff0000 action=close\n"
                                 "        button \"C\" x=0 y=220 w=380 h=100 bg=#ff0000 action=close\n        button \"D\" x=0 y=330 w=380 h=100 bg=#ff0000 action=close\n";
        write(g + "/scripts/screens.vnscript", "screen panel\n    image #203040 x=0 y=0 w=1280 h=720\n"
                                               "    container x=100 y=100 w=400 h=200 scroll=yes scrollbar=yes scrollbar_size=10 edges=cut glide=no name=Cut\n" + four +
                                               "    container x=700 y=100 w=400 h=200 scroll=yes edges=fade fade_size=10 glide=no name=Fade\n" + four +
                                               "    container x=100 y=400 w=400 h=150 scroll=yes scrollbar=yes edges=cut glide=no name=Across\n"
                                               "        button \"A\" x=0 y=0 w=200 h=130 bg=#ff0000 action=close\n        button \"B\" x=210 y=0 w=200 h=130 bg=#ff0000 action=close\n"
                                               "        button \"C\" x=420 y=0 w=200 h=130 bg=#ff0000 action=close\n        button \"D\" x=630 y=0 w=200 h=130 bg=#ff0000 action=close\n"
                                               "    container x=700 y=400 w=400 h=150 scroll=yes glide_speed=10% name=Glide\n" + four +
                                               "    container x=700 y=560 w=400 h=150 scroll=yes glide_speed=10% glide_distance=300% name=Far\n" + four);
        fs::remove_all(S + "/h/scrolling_data");
        auto dr = start(out, g, S + "/h/scrolling_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty());
        CHECK(d.script->config.fadeSize == 64.0f && d.script->config.scrollbarSize == 6.0f); // (the game's settings: fade_size said, the scrollbar as usual)
        e.newGame("start"); // (the story from its start: the screen opens)
        d.open = false;     // (the game's own frame, not the editor over it)
        dr->frames(3);
        auto panel = [&]() -> vn::ScreenView* { return e.view("panel"); };
        auto rectOf = [&](std::size_t i) { return panel()->rectOf(i); }; // (1: the cut container, 2-5 its A-D; 6: the fading one, 7-10 its)
        CHECK(panel());
        const SDL_Color a = pixel(*dr, 300, 150), past = pixel(*dr, 300, 350);
        std::printf("   cut: D at y %.0f; A %02x%02x%02x, where C reaches past the box %02x%02x%02x\n", rectOf(5).y, a.r, a.g, a.b, past.r, past.g, past.b);
        CHECK(rectOf(5).y == 430.0f && near(a, 0xff, 0, 0) && near(past, 0x20, 0x30, 0x40));
        CHECK(!panel()->targetAt(300.0f, 350.0f)); // (C, past the box: not clickable there)
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, 300.0f, 200.0f);
        wheel(*dr, -3.0f, 300.0f, 200.0f); // (three notches down: 180)
        dr->frames(2);
        const SDL_Color dNow = pixel(*dr, 300, 290), below = pixel(*dr, 300, 310);
        std::printf("   scrolled: D at y %.0f, %02x%02x%02x; past the box's bottom %02x%02x%02x\n", rectOf(5).y, dNow.r, dNow.g, dNow.b, below.r, below.g, below.b);
        CHECK(rectOf(5).y == 250.0f && near(dNow, 0xff, 0, 0) && near(below, 0x20, 0x30, 0x40));
        CHECK(panel()->targetAt(300.0f, 290.0f) && panel()->targetAt(300.0f, 290.0f)->spot.element == 5);
        wheel(*dr, -5.0f, 300.0f, 200.0f); // (past the end: as far as D's bottom)
        dr->frames(2);
        std::printf("   as far as it goes: %.0f\n", panel()->state_[1].scroll);
        CHECK(panel()->state_[1].scroll == 230.0f);
        // Fading edges (fade_size=10 on the part, over the game's 64): red in the middle and 15 in from the top, nearly the background 4 in from the top and the bottom edge.
        const SDL_Color mid = pixel(*dr, 900, 150), top = pixel(*dr, 900, 104), bottom = pixel(*dr, 900, 296), inside = pixel(*dr, 900, 115);
        std::printf("   fade: middle %02x%02x%02x, 4 from the top %02x%02x%02x, 4 from the bottom %02x%02x%02x, 15 from the top %02x%02x%02x\n", mid.r, mid.g, mid.b, top.r, top.g,
                    top.b, bottom.r, bottom.g, bottom.b, inside.r, inside.g, inside.b);
        CHECK(near(mid, 0xff, 0, 0) && near(inside, 0xff, 0, 0));
        CHECK(top.r < 0xa0 && top.r > 0x28 && bottom.r < 0xa0 && bottom.r > 0x28);
        // The scrollbar at the right edge (scrollbar_size=10: 10 wide, 5 in from the edge): the thumb (at the end: at the bottom) brighter than the track.
        const SDL_Color thumb = pixel(*dr, 490, 250), track = pixel(*dr, 490, 130), beside = pixel(*dr, 483, 130), beyond = pixel(*dr, 497, 130);
        std::printf("   scrollbar: thumb %02x%02x%02x, track %02x%02x%02x; left of it %02x%02x%02x, right of it %02x%02x%02x\n", thumb.r, thumb.g, thumb.b, track.r, track.g, track.b,
                    beside.r, beside.g, beside.b, beyond.r, beyond.g, beyond.b);
        CHECK(thumb.r > 0xa0 && track.r < 0x80 && track.r > 0x30 && near(beside, 0x20, 0x30, 0x40) && near(beyond, 0x20, 0x30, 0x40));
        dr->shot("scrolling.png");
        // Dragging its parts scrolls it (and doesn't click the button under the mouse); dragging the thumb too.
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 300.0f, 250.0f);
        motion(*dr, 300.0f, 280.0f);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 300.0f, 280.0f);
        dr->frames(2);
        std::printf("   dragged down by 30: %.0f, the screen %s\n", panel()->state_[1].scroll, panel() ? "still open" : "closed");
        CHECK(panel() && panel()->state_[1].scroll == 200.0f);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 490.0f, 250.0f);
        motion(*dr, 490.0f, 200.0f);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 490.0f, 200.0f);
        dr->frames(2);
        std::printf("   the thumb dragged up by 50: %.1f\n", panel()->state_[1].scroll);
        CHECK(std::abs(panel()->state_[1].scroll - 87.0f) < 3.0f); // (a 190-long bar, a thumb 88 of it: 2.26 for each pixel)
        // Across: a sideways wheel, dragging, and the bar along the bottom (its parts reach past it only across).
        auto& across = panel()->state_[11];
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, 300.0f, 470.0f);
        wheel(*dr, 0.0f, 300.0f, 470.0f, 3.0f);
        dr->frames(2);
        std::printf("   across: reaches %.0f x %.0f; a sideways wheel: %.0f, C at x %.0f\n", across.reachX, across.reach, across.scrollX, rectOf(14).x);
        CHECK(across.reachX == 830.0f && across.reach == 130.0f && across.scrollX == 180.0f && rectOf(14).x == 340.0f && across.scroll == 0.0f);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 300.0f, 470.0f);
        motion(*dr, 250.0f, 470.0f);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 250.0f, 470.0f);
        dr->frames(2);
        const SDL_Color thumbX = pixel(*dr, 300, 544), trackX = pixel(*dr, 150, 544), rightBar = pixel(*dr, 494, 470);
        std::printf("   dragged left by 50: %.0f; the bar along the bottom: thumb %02x%02x%02x, track %02x%02x%02x; no bar at the right %02x%02x%02x\n", across.scrollX,
                    thumbX.r, thumbX.g, thumbX.b, trackX.r, trackX.g, trackX.b, rightBar.r, rightBar.g, rightBar.b);
        CHECK(panel() && across.scrollX == 230.0f && thumbX.r > 0xa0 && trackX.r < 0x80 && trackX.r > 0x30 && near(rightBar, 0x20, 0x30, 0x40));
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 300.0f, 544.0f);
        motion(*dr, 350.0f, 544.0f);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 350.0f, 544.0f);
        dr->frames(2);
        std::printf("   its thumb dragged right by 50: %.1f\n", across.scrollX);
        CHECK(std::abs(across.scrollX - 335.0f) < 4.0f);
        dr->shot("scrolling_across.png");
        // Let go while moving, it glides on (as far as it goes); with glide=no it stays where it was let go.
        auto& glide = panel()->state_[16];
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 900.0f, 480.0f);
        for (const float y : {470.0f, 460.0f, 450.0f}) {
            motion(*dr, 900.0f, y);
            dr->frames(1);
        }
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 900.0f, 450.0f);
        dr->frames(1);
        const float letGo = glide.scroll;
        for (int i = 0; i < 150 && glide.glideY != 0.0f; ++i) { // (until it has come to a stop: slow, or at the end)
            SDL_Delay(20);
            dr->frames(1);
        }
        std::printf("   flicked up by 30 (glide_speed=10%%): let go at %.0f, glided on to %.0f of 280\n", letGo, glide.scroll);
        CHECK(letGo >= 30.0f && glide.scroll > letGo + 10.0f && glide.scroll < 250.0f && glide.glideY == 0.0f);
        auto& far = panel()->state_[21]; // (the same flick, glide_distance=300%: further)
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 900.0f, 640.0f);
        for (const float y : {630.0f, 620.0f, 610.0f}) {
            motion(*dr, 900.0f, y);
            dr->frames(1);
        }
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 900.0f, 610.0f);
        dr->frames(1);
        const float farLetGo = far.scroll;
        for (int i = 0; i < 150 && far.glideY != 0.0f; ++i) {
            SDL_Delay(20);
            dr->frames(1);
        }
        std::printf("   the same with glide_distance=300%%: let go at %.0f, glided on to %.0f\n", farLetGo, far.scroll);
        CHECK(far.scroll - farLetGo > 1.5f * (glide.scroll - letGo) && far.glideY == 0.0f);
        auto& fade = panel()->state_[6];
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 900.0f, 280.0f);
        for (const float y : {270.0f, 260.0f, 250.0f}) {
            motion(*dr, 900.0f, y);
            dr->frames(1);
        }
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 900.0f, 250.0f);
        for (int i = 0; i < 10; ++i) {
            SDL_Delay(20);
            dr->frames(1);
        }
        std::printf("   with glide=no, flicked up by 30: %.0f\n", fade.scroll);
        CHECK(fade.scroll == 30.0f && fade.glideY == 0.0f);
        // A click without moving still clicks: C (action=close) closes the screen.
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 300.0f, 250.0f);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 300.0f, 250.0f);
        dr->frames(3);
        std::printf("   clicked: %zu screens open\n", e.overlays_.size());
        CHECK(e.overlays_.empty());
        // The form offers both settings on a container.
        using Field = vn::StoryEditor::Field;
        Field edges{.box = vn::StoryEditor::Box::Edges};
        vn::applyBox(edges);
        CHECK(edges.choices == "show cut fade" && edges.labels == "Show what's past them|Cut it off|Fade it out");
    }

    if (want("dragging")) {
        std::printf("dragging. a list that scrolls (a history...) scrolls by dragging too, and glides on when let go moving, as the game's glide setting says unless the part says\n");
        const std::string g = S + "/h/dragging";
        fs::remove_all(g);
        std::string story = "config title \"D\"\nconfig developer yes\nconfig glide no\nconfig glide_speed 50%\ninclude screens\n\nlabel start\n";
        for (int i = 1; i <= 100; ++i) story += std::format("    Line {} of the story so far.\n", i);
        write(g + "/scripts/main.vnscript", story);
        write(g + "/scripts/screens.vnscript", "screen history\n    image #203040 x=0 y=0 w=1280 h=720\n"
                                               "    history x=100 y=100 w=500 h=300 name=Still\n    history x=700 y=100 w=500 h=300 glide=yes name=Gliding\n"
                                               "    button \"Back\" x=100 y=600 w=200 h=60 action=close\n");
        fs::remove_all(S + "/h/dragging_data");
        auto dr = start(out, g, S + "/h/dragging_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty());
        CHECK(!d.script->config.glide && d.script->config.glideSpeed == 0.5f); // (the game's settings)
        e.newGame("start");
        d.open = false;
        dr->frames(3);
        for (int i = 0; i < 60; ++i) { // (through some of the lines, as clicks would: each finishes one or goes on to the next)
            e.run(vn::Commands{vn::cmd::Ui{.verb = "advance"}});
            dr->frames(2);
        }
        e.open("history");
        dr->frames(3);
        vn::ScreenView* v = e.view("history");
        CHECK(v);
        auto& still = v->state_[1];
        auto& gliding = v->state_[2];
        std::printf("   %zu lines said: they reach %.0f and %.0f down a box 300 high\n", e.game().history().size(), still.reach, gliding.reach);
        CHECK(still.reach > 300.0f && gliding.reach > 300.0f);
        auto flick = [&](float x) { // (pressed, moved down by 30 in three quick moves, let go)
            mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, x, 250.0f);
            for (const float y : {260.0f, 270.0f, 280.0f}) {
                motion(*dr, x, y);
                dr->frames(1);
            }
            mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, x, 280.0f);
            dr->frames(1);
        };
        flick(350.0f);
        for (int i = 0; i < 10; ++i) {
            SDL_Delay(20);
            dr->frames(1);
        }
        std::printf("   with the game's glide no, dragged down by 30: %.0f (older lines come into view)\n", still.scroll);
        CHECK(still.scroll == 30.0f && still.glideY == 0.0f);
        flick(950.0f);
        const float letGo = gliding.scroll;
        for (int i = 0; i < 150 && gliding.glideY != 0.0f; ++i) {
            SDL_Delay(20);
            dr->frames(1);
        }
        std::printf("   with glide=yes on the part: let go at %.0f, glided on to %.0f\n", letGo, gliding.scroll);
        CHECK(letGo >= 30.0f && gliding.scroll > letGo + 10.0f && gliding.glideY == 0.0f && e.view("history"));
    }

    if (want("silence")) {
        std::printf("silence. stop_music_on_open=yes and stop_voice_on_open=yes stop the music, or the voice line, as a screen opens, and _on_close= as it closes\n");
        const std::string g = S + "/h/silence";
        fs::remove_all(g);
        fs::create_directories(g + "/audio");
        silentWav(g + "/audio/beep.wav", 3.0f);
        write(g + "/scripts/main.vnscript", "config title \"S\"\nconfig developer yes\ninclude screens\n\nlabel start\n    music beep\n    Hi.\n    There.\n");
        write(g + "/scripts/screens.vnscript", "screen hush stop_music_on_open=yes stop_voice_on_open=yes\n    button \"Go\" x=100 y=100 w=200 h=60 action=close\n\n"
                                               "screen after stop_music_on_close=yes\n    button \"Go\" x=100 y=100 w=200 h=60 action=close\n");
        fs::remove_all(S + "/h/silence_data");
        auto dr = start(out, g, S + "/h/silence_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty());
        const vn::ScreenDef* hush = d.script->screen("hush");
        const vn::ScreenDef* after = d.script->screen("after");
        CHECK(hush && hush->stopOnOpen.music && hush->stopOnOpen.voice && !hush->stopOnClose.music && !hush->stopOnClose.voice);
        CHECK(after && !after->stopOnOpen.music && !after->stopOnOpen.voice && after->stopOnClose.music && !after->stopOnClose.voice);
        e.newGame("start"); // (the story from its start: its song plays)
        d.open = false;
        dr->frames(3);
        std::printf("   playing: '%s'\n", e.game().music().c_str());
        CHECK(e.game().music() == "beep");
        e.open("after"); // (the music stops when it closes: not as it opens)
        dr->frames(3);
        CHECK(e.game().music() == "beep");
        e.close();
        dr->frames(3);
        std::printf("   after the screen that stops the music as it closes: '%s'\n", e.game().music().c_str());
        CHECK(e.game().music().empty());
        // The song again, and a voice line: the screen that stops the music leaves the voice; the one that stops both as it opens, stops both.
        e.run(vn::Commands{vn::cmd::Play{.track = vn::cmd::Play::Track::Music, .file = "beep"}, vn::cmd::Play{.track = vn::cmd::Play::Track::Voice, .file = "beep"}});
        dr->frames(3);
        const bool voiced = e.audio_.voicePlaying(); // (with no audio device, no voice plays at all)
        std::printf("   playing again: '%s', the voice line %s\n", e.game().music().c_str(), voiced ? "heard" : "not heard (no audio)");
        CHECK(e.game().music() == "beep");
        e.open("after");
        dr->frames(3);
        e.close();
        dr->frames(3);
        CHECK(e.game().music().empty() && e.audio_.voicePlaying() == voiced);
        e.run(vn::Commands{vn::cmd::Play{.track = vn::cmd::Play::Track::Music, .file = "beep"}});
        dr->frames(3);
        CHECK(e.game().music() == "beep");
        e.open("hush");
        dr->frames(3);
        std::printf("   with the screen that stops both as it opens: '%s', the voice line %s\n", e.game().music().c_str(), e.audio_.voicePlaying() ? "heard" : "stopped");
        CHECK(e.game().music().empty() && !e.audio_.voicePlaying());
        e.close();
        dr->frames(3);
        // The screen's form offers all four, as yes or no, after its actions and animations on open and close.
        d.open = true;
        d.view = View::Screens;
        d.screenName = "hush";
        d.part.reset();
        dr->frames(3);
        dr->shot("silence_form.png");
        // The second screen clicked in the list, then Yes in "Stops music on open": it goes on that screen's line (in its own file).
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, 40.0f, 125.0f);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 40.0f, 125.0f);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 40.0f, 125.0f);
        dr->frames(3);
        std::printf("   clicked in the list: screen '%s'\n", d.screenName.c_str());
        CHECK(d.screenName == "after");
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, 1105.0f, 451.0f);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, 1105.0f, 451.0f);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, 1105.0f, 451.0f);
        dr->frames(3);
        const std::string screens = read(g + "/scripts/screens.vnscript");
        const auto at = screens.find("screen after");
        std::printf("   after the click: %s\n", at == std::string::npos ? "(no screen after)" : screens.substr(at, screens.find('\n', at) - at).c_str());
        CHECK(at != std::string::npos && screens.substr(at, screens.find('\n', at) - at).contains("stop_music_on_open=yes"));
        CHECK(screens.starts_with("screen hush stop_music_on_open=yes stop_voice_on_open=yes\n")); // (the first screen's line as it was)
        // "open title" from a menu over the title screen goes back to the title: the menu closes, doing what it does on closing, and the title opens again (its music starts over).
        write(g + "/scripts/title.vnscript", "screen title on_show=\"music beep\"\n    button \"Go\" x=100 y=100 w=200 h=60 action=\"open menu\"\n"
                                             "screen menu stop_music_on_close=yes\n    button \"Return\" x=100 y=100 w=200 h=60 action=\"open title\"\n");
        write(g + "/scripts/main.vnscript", "config title \"S\"\nconfig developer yes\ninclude screens\ninclude title\n\nlabel start\n    music beep\n    Hi.\n    There.\n");
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty());
        d.open = false;
        e.openTitle();
        dr->frames(3);
        CHECK(e.titleShown_ && e.game().music() == "beep");
        e.open("menu");
        dr->frames(3);
        CHECK(e.overlays_.size() == 1 && e.game().music() == "beep");
        const std::size_t track = e.audio_.current_; // (the mixer's track the song is on: a new play takes the other)
        e.run(vn::Commands{vn::cmd::Ui{.verb = "open", .arg = "title"}}); // (a Return button's action)
        dr->frames(3);
        std::printf("   back to the title: %zu open, music '%s' on %s track\n", e.overlays_.size(), e.game().music().c_str(), e.audio_.current_ == track ? "the same" : "a new");
        CHECK(e.overlays_.empty() && e.titleShown_ && e.game().music() == "beep" && e.audio_.current_ != track);
    }

    if (want("buttons")) {
        std::printf("buttons. a button without a width (or a height) is as big as the picture it shows now, or its words, and is clicked within its usual size too\n");
        const std::string g = S + "/h/buttons";
        fs::remove_all(g);
        fs::create_directories(g + "/images");
        for (const auto& [name, w, h, rgba] : {std::tuple{"pic", 64, 48, 0xff4080c0u}, {"big", 100, 80, 0xff40c080u}}) // (orange; green: RGBA32 bytes 80 c0 40)
            if (SDL_Surface* pic = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32)) {
                SDL_FillSurfaceRect(pic, nullptr, rgba);
                IMG_SavePNG(pic, (g + "/images/" + name + ".png").c_str());
                SDL_DestroySurface(pic);
            }
        write(g + "/scripts/main.vnscript", "config title \"B\"\nconfig developer yes\ninclude screens\n\nlabel start\n    open panel\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    image #203040 x=0 y=0 w=1280 h=720\n" // (in pixels, like every script here: write() turns them into places)
                                               "    button \"\" x=0 y=0 image=pic hover_image=big action=close name=a\n"
                                               "    button \"Hello\" x=640 y=360 padding=8 action=close name=b\n"
                                               "    button \"\" x=960 y=720 w=320 image=pic action=close name=c\n");
        std::ofstream(g + "/scripts/screens.vnscript", std::ios::app) // (in places: write() would size the container for its parts itself)
            << "    container x=0 y=0 fit=parts name=Wrap\n        button \"\" x=-1 y=1 image=pic action=close name=d\n"
               "        text \"Hi\" x=0.9 y=0 name=t\n        button \"\" x=0.9 y=-0.5 action=close name=e\n"; // (placed within it by place: no feeding on itself)
        fs::remove_all(S + "/h/buttons_data");
        auto dr = start(out, g, S + "/h/buttons_data");
        auto& d = dr->engine.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty());
        dr->engine.newGame("start"); // (the story from its start: the screen opens)
        d.open = false;              // (the game's own frame, not the editor over it)
        auto panel = [&]() -> vn::ScreenView* { return dr->engine.view("panel"); };
        auto rectOf = [&](std::size_t i) { return panel()->rectOf(i); };
        for (int n = 0; n < 30 && !(panel() && rectOf(1).w > 0.0f && rectOf(3).h > 0.0f); ++n) dr->frames(2); // (the pictures load in their own time)
        CHECK(panel());
        const SDL_FRect a = rectOf(1), b = rectOf(2), c = rectOf(3);
        std::printf("   by its picture: %.0f,%.0f %.0f x %.0f; by its words: %.0f,%.0f %.0f x %.0f; width written: %.0f,%.0f %.0f x %.0f\n", a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h,
                    c.x, c.y, c.w, c.h);
        CHECK(a.x == 0.0f && a.y == 0.0f && a.w == 64.0f && a.h == 48.0f);                                 // (its picture's pixels, at the top left)
        CHECK(b.w > 16.0f && b.h > 16.0f && b.w == std::round(panel()->state_[2].text.size().x + 16.0f)); // (its words, and 8 of padding each side)
        CHECK(b.x == std::round((1280.0f - b.w) / 2) && b.y == std::round((720.0f - b.h) / 2));            // (placed by that size: in the middle)
        CHECK(c.x == 960.0f && c.y == 672.0f && c.w == 320.0f && c.h == 48.0f);                            // (a width written: that; its height from the picture; at the bottom right)
        const SDL_FRect wrap = rectOf(4), inWrap = rectOf(5); // (a container with fit=parts: as big as the button in it, in the middle, the button in it)
        std::printf("   a container as big as its parts: %.0f,%.0f %.0f x %.0f, its button at %.0f,%.0f\n", wrap.x, wrap.y, wrap.w, wrap.h, inWrap.x, inWrap.y);
        CHECK(wrap.w == 64.0f && wrap.h == 48.0f && wrap.x == 608.0f && wrap.y == 336.0f && inWrap.x == 608.0f && inWrap.y == 336.0f);
        const SDL_FRect words = rectOf(6); // (a text without a size: as big as its words, placed by that near the right edge, inside)
        std::printf("   its text: %.0f,%.0f %.0f x %.0f\n", words.x, words.y, words.w, words.h);
        CHECK(words.w > 8.0f && words.w < 64.0f && words.x + words.w <= wrap.x + wrap.w && words.x >= wrap.x);
        CHECK(near(pixel(*dr, 32, 24), 0xc0, 0x80, 0x40) && near(pixel(*dr, 80, 60), 0x20, 0x30, 0x40)); // (drawn at that size, no bigger)
        // Pointed at, it's as big as its pointed-at picture, and clicked within both sizes: over the bigger
        // picture only, it stays pointed at.
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, 32.0f, 24.0f);
        dr->frames(2);
        const SDL_FRect over = rectOf(1), hit = panel()->state_[1].hit.value_or(SDL_FRect{});
        std::printf("   pointed at: %.0f x %.0f, clicked within %.0f x %.0f\n", over.w, over.h, hit.w, hit.h);
        CHECK(over.w == 100.0f && over.h == 80.0f && hit.w == 100.0f && hit.h == 80.0f);
        CHECK(near(pixel(*dr, 90, 70), 0x80, 0xc0, 0x40));
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, 90.0f, 70.0f);
        dr->frames(2);
        CHECK(rectOf(1).w == 100.0f && rectOf(1).h == 80.0f);
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, 400.0f, 400.0f);
        dr->frames(2);
        CHECK(rectOf(1).w == 64.0f && rectOf(1).h == 48.0f);
        dr->shot("buttons.png");
        // The editor: the part's empty Width and Height boxes say its size now, as shares of its box.
        d.open = true;
        d.view = View::Screens;
        d.screenName = "panel";
        const auto& screen = *std::ranges::find(d.editor.screens(), std::string("panel"), &vn::StoryEditor::Screen::name);
        d.part = d.editor.parts(screen)[1].line;
        dr->frames(3);
        dr->shot("buttons_form.png");
    }

    if (want("names")) {
        std::printf("names. a screen's parts have names: in the script, the Parts list and the form\n");
        const std::string g = S + "/h/names";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"N\"\nconfig developer yes\ninclude screens\n\nlabel start\n    open panel\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    image #203040 x=0 y=0 w=1280 h=720 name=Backdrop\n"
                                               "    button \"\" x=100 y=100 w=200 h=60 action=close\n    text \"Hello\" x=400 y=300 w=300 h=60\n");
        fs::remove_all(S + "/h/names_data");
        auto dr = start(out, g, S + "/h/names_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty()); // (name= is any part's)
        CHECK(near(pixel(*dr, 1200, 650), 0x20, 0x30, 0x40)); // (the game shows the screen as before)
        d.view = View::Screens;
        d.screenName = "panel";
        d.part.reset();
        dr->frames(3);
        auto partsNow = [&] {
            for (const auto& sc : d.editor.screens())
                if (sc.name == "panel") return d.editor.parts(sc);
            return std::vector<vn::StoryEditor::Part>{};
        };
        auto parts = partsNow();
        CHECK(parts.size() == 3 && vn::partTitle(parts[0]) == "Backdrop" && vn::partTitle(parts[1]) == "button" && vn::partTitle(parts[2]) == "text  Hello");
        // Typing into a box, letter by letter.
        auto typeSlowly = [&](const std::string& id, const std::string& now, const std::string& text) { typeBox(*dr, d, id, text, now); };
        // Renamed in the list (a double click there): the button.
        typeSlowly("parts:rename:1", "", "Start \"game\"");
        std::string screens = read(g + "/scripts/screens.vnscript");
        CHECK(screens.contains("button \"\" ") && screens.contains("action=close name=\"Start game\""));
        parts = partsNow();
        CHECK(parts.size() == 3 && vn::partTitle(parts[1]) == "Start game");
        // Its form: the name box, typed into (the text's).
        d.part = parts[2].line;
        dr->frames(2);
        const std::size_t screenLine = d.editor.screens().front().line;
        typeSlowly(std::format("part:{}:{}:name", screenLine, parts[2].line), "", "Greeting");
        screens = read(g + "/scripts/screens.vnscript");
        CHECK(screens.contains("text \"Hello\" ") && screens.contains("name=Greeting"));
        dr->shot("names_list.png");
        // Emptied with Backspace: it has no name (and the list says what it is again).
        d.focus = "parts:rename:0";
        d.buffer = "Backdrop";
        d.caret = d.anchor = d.buffer.size();
        ++d.focusCount;
        d.anchorFocus = d.focusCount;
        for (int i = 0; i < 8; ++i) {
            key(*dr, SDLK_BACKSPACE);
            dr->frames(1);
        }
        d.focus.clear();
        dr->frames(1);
        screens = read(g + "/scripts/screens.vnscript");
        std::printf("%s", screens.c_str());
        CHECK(!screens.contains("Backdrop"));
        CHECK(d.problems.empty());
    }

    if (want("live")) {
        std::printf("live. the game in DIR/live (a copy of the one being made): what the editor would flag\n");
        const std::string g = S + "/live";
        if (!fs::exists(g + "/scripts/main.vnscript")) {
            std::printf("   (no game there: skipped)\n");
        } else {
            fs::remove_all(S + "/h/live_data");
            auto dr = start(out, g, S + "/h/live_data");
            auto& e = dr->engine;
            auto& d = e.developer_.tools;
            std::printf("   problems: %zu\n", d.problems.size());
            for (const auto& p : d.problems) std::printf("   %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
            for (const auto& t : e.translations_) std::printf("   translation %s (%s) code=%s\n", t.id.c_str(), t.name.c_str(), t.code.c_str());
            for (const View v : {View::Story, View::Map, View::Screens, View::Library, View::Settings, View::Translate, View::Log}) {
                d.view = v;
                dr->frames(2);
            }
            d.view = View::Screens;
            for (const auto& sc : d.editor.screens()) {
                d.screenName = sc.name;
                d.part.reset();
                dr->frames(2);
            }
            dr->shot("live_screens.png");
        }
    }

    if (want("relaunch")) {
        std::printf("relaunch. the language picked is kept after a restart, and a language screen shows it: its buttons look chosen by the language that's on, and its pictures and the story follow it; "
                    "a picture's \"depends on\" is a condition like the others, and text typed in a condition is written in single quotes\n");
        const std::string g = S + "/h/relaunch";
        fs::remove_all(g);
        fs::remove_all(S + "/h/relaunch_data");
        write(g + "/scripts/main.vnscript", "config title \"R\"\ninclude screens\n\nvariable went 0\n\nlabel start\n    open lang\n    if language is 'English'\n        set went to 1\n"
                                            "    else\n        set went to 2\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen lang\n"
                                               "    button \"English\" x=240 y=320 w=300 h=80 action=\"setting language english\"\n"
                                               "    button \"Japanese\" x=740 y=320 w=300 h=80 action=\"setting language japanese\"\n"
                                               "    button \"Start\" x=490 y=520 w=300 h=80 action=close image=\"{if(language is 'English', 'start_en', 'start_jp')}\"\n");
        write(g + "/translations/japanese.vnscript", "language \"Japanese\" code=ja\n\n\"Hi.\"\n    \"Yaa.\"\n");
        auto launch = [&] { // (the player opened again: the story starts, and its language screen with it)
            auto dr = start(out, g, S + "/h/relaunch_data");
            dr->frames(5);
            CHECK(std::ranges::contains(dr->engine.overlays_, "lang"));
            return dr;
        };
        auto press = [&](Driver& dr, std::size_t part) { // (a click in the middle of one of the language screen's parts)
            const SDL_FRect r = dr.engine.view("lang")->rectOf(part);
            click(dr, r.x + r.w / 2, r.y + r.h / 2);
            dr.frames(5);
        };
        auto state = [&](Driver& dr) { // the language on / the button that looks chosen / the Start button's picture
            vn::Engine& e = dr.engine;
            vn::ScreenView* v = e.view("lang");
            const auto& parts = v->def_->elements;
            return std::format("{} / {}{} / {}", vn::toString(e.builtin("language").value_or(vn::Value{})), v->selected(parts[0]) ? "English" : "",
                               v->selected(parts[1]) ? "Japanese" : "", v->interpolate(parts[2].style.image));
        };
        auto went = [&](Driver& dr) { return vn::toNumber(dr.engine.game().variable("went")); };
        {
            auto dr = launch();
            std::printf("   first launch: %s\n", state(*dr).c_str());
            press(*dr, 1); // Japanese
            std::printf("   Japanese picked: %s\n", state(*dr).c_str());
            CHECK(state(*dr) == "Japanese / Japanese / start_jp");
            press(*dr, 2); // Start
            CHECK(!std::ranges::contains(dr->engine.overlays_, "lang") && went(*dr) == 2);
        }
        {
            auto dr = launch(); // (restarted: the screen shows the language that's on, and Start keeps it)
            std::printf("   restarted: %s (setting '%s')\n", state(*dr).c_str(), dr->engine.settings_.language.c_str());
            CHECK(dr->engine.settings_.language == "japanese" && state(*dr) == "Japanese / Japanese / start_jp");
            press(*dr, 2);
            CHECK(went(*dr) == 2);
            dr->engine.play(); // (played again, as the editor's Play does: the same)
            dr->frames(5);
            CHECK(std::ranges::contains(dr->engine.overlays_, "lang") && state(*dr) == "Japanese / Japanese / start_jp");
            press(*dr, 0); // English
            std::printf("   English picked: %s\n", state(*dr).c_str());
            CHECK(state(*dr) == "English / English / start_en");
            press(*dr, 2);
            CHECK(went(*dr) == 1);
        }
        {
            auto dr = launch(); // (restarted after English: English)
            std::printf("   restarted: %s\n", state(*dr).c_str());
            CHECK(state(*dr) == "English / English / start_en");
            press(*dr, 2);
            CHECK(went(*dr) == 1);
        }
        // The editor: a condition's box lists the language; a check's text shows without its quotes and, typed, is written
        // in single ones (a part's options are in double ones); a picture that depends on the language: its checks.
        write(g + "/scripts/main.vnscript", "config title \"R\"\nconfig developer yes\ninclude screens\n\nvariable name 'Bob'\n"
                                            "control quit_key keys=q action=quit if=\"language is 'English'\"\n\nlabel start\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen s\n    button \"\" x=100 y=100 w=200 h=60 image=\"{if(language is 'English', 'a', 'b')}\" selected=\"name is 'Bob'\"\n");
        auto dr = start(out, g, S + "/h/relaunch_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty() && std::ranges::contains(d.editor.names(vn::StoryEditor::Pick::Conditions, ""), "language"));
        auto control = d.editor.fields("control", d.editor.definitions("control")[0]);
        auto value = [&](std::string_view name) {
            const auto f = std::ranges::find(control, name, &vn::StoryEditor::Field::name);
            return f != control.end() ? f->value : std::string("(none)");
        };
        std::printf("   works when: %s / %s / %s\n", value("Only when").c_str(), value("Only when is").c_str(), value("Only when value").c_str());
        CHECK(value("Only when") == "language" && value("Only when is") == "is" && value("Only when value") == "English");
        CHECK(d.editor.linesOf("control", control).front().contains("if=\"language is 'English'\"")); // (as it was: kept)
        std::ranges::find(control, std::string("Only when value"), &vn::StoryEditor::Field::name)->value = "Japanese";
        const auto typed = d.editor.linesOf("control", control);
        std::printf("   typed: %s\n", typed.front().c_str());
        CHECK(typed.front().contains("if=\"language is 'Japanese'\""));
        const auto& sc = d.editor.screens().back();
        const auto parts = d.editor.parts(sc);
        CHECK(sc.name == "s" && !parts.empty());
        d.view = View::Screens;
        d.screenName = "s";
        d.part = parts.front().line;
        dr->frames(3);
        dr->shot("relaunch_form.png");
        const std::string pid = std::format("part:{}:{}", sc.line, parts.front().line);
        typeBox(*dr, d, pid + ":image by value", "Japanese");
        typeBox(*dr, d, pid + ":selected value", "Alice");
        const std::string screens = read(g + "/scripts/screens.vnscript");
        std::printf("%s", screens.c_str());
        CHECK(screens.contains("image=\"{if(language is 'Japanese', 'a', 'b')}\"") && screens.contains("selected=\"name is 'Alice'\""));
        for (const float scroll : {500.0f, 1000.0f}) { // (further down the form: what the picture depends on, and when it looks chosen)
            d.formScroll = scroll;
            dr->frames(2);
            dr->shot(std::format("relaunch_form_{}.png", static_cast<int>(scroll)));
        }
        e.developer_.useTool(vn::kReload); // (the line still reads)
        dr->frames(3);
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty());
    }

    if (want("chosen")) {
        std::printf("chosen. a button chosen because of its selected= puts what its action sets in effect, clicked or not (set VARIABLE to VALUE, setting NAME VALUE; nothing else): "
                    "a language screen whose choice is a variable starting at English is in English at every start, Japanese once that's picked; in Try it too\n");
        const std::string g = S + "/h/chosen";
        fs::remove_all(g);
        fs::remove_all(S + "/h/chosen_data");
        const std::string main = "config title \"C\"\ninclude screens\n\nvariable lang_eng yes\nvariable went 0\nvariable opened 0\nvariable flag no\n\n"
                                 "label start\n    open lang\n    if lang_eng\n        set went to 1\n    else\n        set went to 2\n    Hi.\n";
        write(g + "/scripts/main.vnscript", main);
        write(g + "/scripts/screens.vnscript", "screen lang\n"
                                               "    button \"English\" x=240 y=320 w=300 h=80 action=\"set lang_eng to yes; setting language english\" selected=lang_eng\n"
                                               "    button \"Japanese\" x=740 y=320 w=300 h=80 action=\"set lang_eng to no; setting language japanese\" selected=\"not lang_eng\"\n"
                                               "    button \"Start\" x=490 y=520 w=300 h=80 action=close image=\"{if(lang_eng, 'start_en', 'start_jp')}\"\n"
                                               "    button \"Other\" x=40 y=40 w=100 h=40 action=\"add 1 to opened; toggle flag\" selected=lang_eng\n");
        write(g + "/translations/japanese.vnscript", "language \"Japanese\" code=ja\n\n\"Hi.\"\n    \"Yaa.\"\n");
        fs::create_directories(S + "/h/chosen_data");
        std::ofstream(S + "/h/chosen_data/settings.txt") << "language japanese\n"; // (Japanese, picked the time before)
        auto launch = [&] { // (the player opened: the story starts, and its language screen with it)
            auto dr = start(out, g, S + "/h/chosen_data");
            dr->frames(5);
            CHECK(std::ranges::contains(dr->engine.overlays_, "lang"));
            return dr;
        };
        auto press = [&](Driver& dr, std::size_t part) { // (a click in the middle of one of the language screen's parts)
            const SDL_FRect r = dr.engine.view("lang")->rectOf(part);
            click(dr, r.x + r.w / 2, r.y + r.h / 2);
            dr.frames(5);
        };
        auto state = [&](Driver& dr) { // the language on / the button that looks chosen / the Start button's picture
            vn::Engine& e = dr.engine;
            vn::ScreenView* v = e.view("lang");
            const auto& parts = v->def_->elements;
            return std::format("{} / {}{} / {}", vn::toString(e.builtin("language").value_or(vn::Value{})), v->selected(parts[0]) ? "English" : "",
                               v->selected(parts[1]) ? "Japanese" : "", v->interpolate(parts[2].style.image));
        };
        auto number = [&](Driver& dr, std::string_view name) { return vn::toNumber(dr.engine.game().variable(name)); };
        {
            auto dr = launch();
            std::printf("   launched, Japanese picked the time before: %s (setting '%s')\n", state(*dr).c_str(), dr->engine.settings_.language.c_str());
            CHECK(state(*dr) == "English / English / start_en" && dr->engine.settings_.language == "original"); // (English shows chosen: English it is)
            CHECK(number(*dr, "opened") == 0 && number(*dr, "flag") == 0); // (only what it sets: its other actions wait for a click)
            press(*dr, 1); // Japanese
            std::printf("   Japanese clicked: %s\n", state(*dr).c_str());
            CHECK(state(*dr) == "Japanese / Japanese / start_jp" && dr->engine.settings_.language == "japanese");
            press(*dr, 2); // Start
            CHECK(number(*dr, "went") == 2);
        }
        {
            auto dr = launch(); // (restarted: the variable starts at English again, so English is chosen, and English it is)
            std::printf("   restarted: %s (setting '%s')\n", state(*dr).c_str(), dr->engine.settings_.language.c_str());
            CHECK(state(*dr) == "English / English / start_en" && dr->engine.settings_.language == "original");
            press(*dr, 2);
            CHECK(number(*dr, "went") == 1 && number(*dr, "opened") == 0);
        }
        // Try it: the same, with its own story under it and its own settings; the player's as they were.
        write(g + "/scripts/main.vnscript", "config developer yes\n" + main);
        {
            auto dr = start(out, g, S + "/h/chosen_data");
            auto& e = dr->engine;
            auto& d = e.developer_.tools;
            const std::string playerLanguage = e.settings_.language;
            d.view = View::Screens;
            d.screenName = "lang";
            d.part.reset();
            dr->frames(3);
            const SDL_FRect shownAt = d.shownScreen; // (the screen in the tab, and Try it under it)
            click(*dr, shownAt.x + 20.0f, shownAt.y + shownAt.h + 10.0f + vn::layout::mediumButtonH / 2.0f);
            dr->frames(4);
            CHECK(d.trying);
            auto tried = [&] { // as Try it has it (its story and its settings): the button that looks chosen / the Start button's picture / Try it's language
                std::swap(e.game_, d.sample);
                std::swap(e.settings_, d.trySettings);
                const auto& els = d.screenView->def_->elements;
                std::string out = std::format("{}{} / {} / {}", d.screenView->selected(els[0]) ? "English" : "", d.screenView->selected(els[1]) ? "Japanese" : "",
                                              d.screenView->interpolate(els[2].style.image), e.settings_.language);
                std::swap(e.settings_, d.trySettings);
                std::swap(e.game_, d.sample);
                return out;
            };
            auto pressTried = [&](std::size_t part) { // (a click in the middle of a part, in the tab)
                const SDL_FRect r = d.screenView->rectOf(part);
                const float scale = shownAt.w / 1280.0f;
                click(*dr, shownAt.x + (r.x + r.w / 2) * scale, shownAt.y + (r.y + r.h / 2) * scale);
                dr->frames(4);
            };
            std::printf("   Try it: %s\n", tried().c_str());
            CHECK(tried() == "English / start_en / original" && vn::toNumber(d.sample->variable("opened")) == 0);
            pressTried(1); // Japanese
            std::printf("   Japanese clicked in Try it: %s (the player's '%s')\n", tried().c_str(), e.settings_.language.c_str());
            CHECK(tried() == "Japanese / start_jp / japanese" && e.settings_.language == playerLanguage);
            dr->shot("chosen_try.png");
            pressTried(0); // English
            CHECK(tried() == "English / start_en / original");
            key(*dr, SDLK_ESCAPE);
            dr->frames(2);
            CHECK(!d.trying && e.settings_.language == playerLanguage);
        }
    }

    if (want("langcode")) {
        std::printf("langcode. the Translate tab's language list (with codes) and its wrong-code note; the Library marks Mac-only movies; setting language NAME\n");
        const std::string g = S + "/h/langcode";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"L\"\nconfig developer yes\n\nlabel start\n    Hi.\ninclude screens\n");
        write(g + "/scripts/screens.vnscript", "screen lang\n    button \"JP\" x=0 y=0 w=0.2 h=0.1 action=\"setting language Japanese\"\n    button \"EN\" x=0 y=0.3 w=0.2 h=0.1 action=\"setting language english\"\n"
                                              "    button \"Small\" x=0 y=0.6 w=0.2 h=0.1 action=\"setting window_size 1024 576\"\n");
        write(g + "/translations/language.vnscript", "language \"Japanese\" code=jp\n\n\"Hi.\"\n    \"Hi.\"\n");
        write(g + "/movies/opening.mp4", ""); // (listed by its ending)
        write(g + "/movies/credits.mpg", "");
        fs::remove_all(S + "/h/langcode_data");
        auto dr = start(out, g, S + "/h/langcode_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        CHECK(vn::languageCode("Japanese") == "ja" && vn::languageCode(" español ") == "es" && vn::languageCode("Magyar") == "hu" && vn::languageCode("Klingon").empty());
        // setting language NAME: a translation by its name, file or code, the game's own by its name (or "original");
        // next and previous still step through them; a word that's no language changes nothing. The buttons parse
        // (the word kept), and the one naming the language on looks chosen.
        auto language = [&](const char* value) {
            e.run(vn::Commands{vn::cmd::Ui{.verb = "setting", .arg = std::string("language ") + value}});
            return e.settings_.language;
        };
        CHECK(language("japanese") == "language" && e.script_.translation() && language("english") == "original" && !e.script_.translation());
        CHECK(language("klingon") == "original" && language("1") == "language" && language("-1") == "original" && language("jp") == "language" && language("original") == "original");
        e.run(vn::Commands{vn::cmd::Ui{.verb = "setting", .arg = "window_size 1024 576"}}); // (the window's size: a width and a height)
        CHECK(e.settings_.windowWidth == 1024 && e.settings_.windowHeight == 576);
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        const auto& langButtons = d.script->screens.at("lang").elements;
        const auto* jp = std::get_if<vn::cmd::Ui>(&langButtons[0].actions.front());
        CHECK(d.problems.empty() && jp && jp->arg == "language japanese");
        e.run(vn::Commands{vn::cmd::Ui{.verb = "open", .arg = "lang"}});
        dr->frames(2);
        vn::ScreenView* lang = e.view("lang");
        CHECK(lang && !lang->selected(lang->def_->elements[0]) && lang->selected(lang->def_->elements[1]));
        language("japanese");
        CHECK(lang->selected(lang->def_->elements[0]) && !lang->selected(lang->def_->elements[1]));
        language("english");
        e.run(vn::Commands{vn::cmd::Ui{.verb = "open", .arg = "lang"}}); // (on top: closes)
        dr->frames(2);
        CHECK(!std::ranges::contains(e.overlays_, "lang"));
        const auto values = d.editor.names(vn::StoryEditor::Pick::Languages, ""); // (the editor's language list: the game's own, then its translations)
        CHECK(std::ranges::contains(values, "english") && std::ranges::contains(values, "language") && !std::ranges::contains(values, "next"));
        // The Screens tab's words of help go under the preview's buttons: the built-in textbox's, and + New screen's (the one the mouse is over).
        d.view = View::Screens;
        d.screenName = "say";
        d.part.reset();
        dr->frames(3);
        std::printf("   tip: '%s'\n", d.screenTip.c_str());
        CHECK(d.screenTip == "The player's textbox: the speaker's name, the line and the arrow");
        dr->shot("langcode_builtin.png");
        d.picking = true;
        dr->frames(2);
        CHECK(d.screenTip.empty()); // (nothing under the mouse)
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, 1087.0f, 115.0f); // (over the first start, "The usual menus": the list's top row at the right)
        dr->frames(2);
        std::printf("   tip over the list: '%s'\n", d.screenTip.c_str());
        dr->shot("langcode_newscreen.png");
        CHECK(d.screenTip == "Title, pause, save, load, settings, history, confirm and phone, together");
        d.picking = false;
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, 500.0f, 600.0f); // (off the list again)
        d.view = View::Library;
        d.library = "movies";
        dr->frames(3);
        dr->shot("langcode_library.png");
        // The Translate tab with the file open (as a click on it in the list would open it).
        d.view = View::Translate;
        d.translation.id = "language";
        {
            const auto text = vn::readTextFile(g + "/translations/language.vnscript");
            CHECK(text.has_value());
            d.translation.lines = vn::linesOf(*text);
        }
        dr->frames(2);
        dr->shot("langcode_translate.png");
        d.popup.id = "lang:language"; // (the Language box's list open)
        d.popup.shown = true;
        dr->frames(2);
        dr->shot("langcode_list.png");
        d.popup.id.clear();
        d.popup.shown = false;
        dr->frames(1);
        d.view = View::Settings;
        d.settingsScroll = 0.0f;
        dr->frames(2);
        dr->shot("langcode_settings.png");
        d.view = View::Translate;
        dr->frames(2);
        // The one list: a language typed with its code, one picked from the list, and a name that isn't listed.
        auto type = [&](const std::string& id, const std::string& now, const std::string& text) {
            d.focus = id;
            d.buffer = now;
            d.caret = d.anchor = now.size();
            ++d.focusCount;
            d.anchorFocus = d.focusCount;
            d.typed = text;
            dr->frames(2);
            d.focus.clear();
            dr->frames(1);
        };
        auto header = [&] { // (the one file there, named after its language, and its first line)
            std::string file, first;
            for (const auto& f : fs::directory_iterator(g + "/translations"))
                if (f.path().extension() == ".vnscript") {
                    file = f.path().filename().string();
                    const std::string text = read(f.path().string());
                    first = text.substr(0, text.find('\n'));
                }
            return file + ": " + first;
        };
        dr->frames(2); // (the file open, with Japanese in it: named after it)
        std::printf("   %s\n", header().c_str());
        CHECK(header() == "japanese.vnscript: language \"Japanese\" code=jp" && d.translation.id == "japanese");
        type("lang:language", "", "Japanese (ja)");
        std::printf("   %s\n", header().c_str());
        CHECK(header() == "japanese.vnscript: language \"Japanese\" code=ja");
        d.picked = std::pair{std::string("lang:language"), std::string("Hungarian (hu)")}; // (picked from its list)
        dr->frames(3);
        std::printf("   %s\n", header().c_str());
        CHECK(header() == "hungarian.vnscript: language \"Hungarian\" code=hu");
        type("lang:language", "", "Klingon");
        dr->frames(2);
        std::printf("   %s\n", header().c_str());
        CHECK(header() == "klingon.vnscript: language \"Klingon\"");
        type("lang:language", "", "Portuguese, Brazil (pt-BR)");
        dr->frames(2);
        std::printf("   %s\n", header().c_str());
        CHECK(header() == "portuguese_brazil.vnscript: language \"Portuguese, Brazil\" code=pt-BR");
    }

    if (want("writing")) {
        std::printf("writing. word counts, reading time, the story as text, and lines never reached\n");
        CHECK(vn::wordsIn("Hello {bold}world{/bold}, {player}!") == 3);
        CHECK(vn::wordsIn("日本語です") == 5);
        CHECK(vn::wordsIn("") == 0 && vn::wordsIn("{pause}") == 0);
        CHECK(vn::readingTime(50) == "under a minute" && vn::readingTime(450) == "2 min" && vn::readingTime(200 * 90) == "1 h 30 min");
        const std::string g = S + "/h/writing";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript",
              "config title \"W\"\nconfig developer yes\ncharacter Aiko color=pink\n\nlabel start\n    Aiko: Good {bold}morning{/bold}!\n    The rain didn't stop all day.\n"
              "    choice What do you say?\n        Tell the truth\n            Aiko: Thank you.\n        Lie\n            Aiko: Hmm...\n    jump second\n\nlabel second\n    Aiko: Bye.\n");
        fs::remove_all(S + "/h/writing_data");
        auto dr = start(out, g, S + "/h/writing_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        CHECK(d.problems.empty());
        const std::string story = vn::storyText(d.editor);
        std::printf("%s", story.c_str());
        CHECK(story.contains("== start ==\n") && story.contains("Aiko: Good morning!\n") && story.contains("The rain didn't stop all day.\n"));
        CHECK(story.contains("Choice: What do you say?\n") && story.contains("    > Tell the truth\n        Aiko: Thank you.\n") && !story.contains("jump"));
        // The preview's run to the first line counts as a play: a mark already. Played to the first line: that line is reached, and the file says so.
        d.view = View::Story;
        dr->frames(4);
        CHECK(!e.reached_.empty());
        e.developer_.useTool(vn::kPlay);
        dr->frames(6);
        CHECK(!e.reached_.empty());
        for (const auto& key : e.reached_) std::printf("   reached: %s\n", key.c_str());
        CHECK(e.reached_.contains("scripts/main.vnscript\tAiko: Good {bold}morning{/bold}!"));
        CHECK(!e.reached_.contains("scripts/main.vnscript\tAiko: Thank you."));
        CHECK(fs::exists(S + "/h/writing_data/reached.txt"));
        key(*dr, SDLK_F10); // (back to the tools: the Story tab, with the marks and the counts)
        dr->frames(3);
        d.view = View::Story;
        dr->frames(2);
        dr->shot("writing_story.png");
        // The story as a text file, next to the game folder.
        const std::string file = vn::parentFolder(g + "/") + "story.txt";
        CHECK(vn::parentFolder("/a/b/game/") == "/a/b/");
        fs::remove(file);
        std::ofstream(file) << story;
        CHECK(fs::exists(file));
    }

    if (want("sheet")) {
        std::printf("sheet. a translation out to a spreadsheet and back\n");
        const auto rows = vn::csvRows("\xEF\xBB\xBFText,Translation\r\n\"Hi, \"\"you\"\".\",\"Hola\nahí\"\nplain,simple\n");
        CHECK(rows.size() == 3 && rows[1][0] == "Hi, \"you\"." && rows[1][1] == "Hola\nahí" && rows[2][1] == "simple");
        CHECK(vn::csvCell("a \"b\"") == "\"a \"\"b\"\"\"");
        const std::string g = S + "/h/sheet";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"S\"\nconfig developer yes\n\nlabel start\n    Hi.\n    Bye.\n");
        write(g + "/translations/spanish.vnscript", "language \"Spanish\" code=es\n\n\"Hi.\"\n    \"Hola.\"\n\n\"Bye.\"\n    \"Bye.\"\n");
        fs::remove_all(S + "/h/sheet_data");
        auto dr = start(out, g, S + "/h/sheet_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        d.translation.id = "spanish";
        d.translation.lines = vn::linesOf(*vn::readTextFile(g + "/translations/spanish.vnscript"));
        const std::string sheet = e.developer_.translationSheet();
        CHECK(sheet == S + "/h/spanish.csv");
        std::printf("   %s\n", e.developer_.exportTranslationSheet().c_str());
        const std::string csv = read(sheet);
        std::printf("%s", csv.c_str());
        CHECK(csv == "\xEF\xBB\xBFText,Translation\r\n\"Hi.\",\"Hola.\"\r\n\"Bye.\",\"\"\r\n"); // (an untranslated text: an empty cell)
        std::ofstream(sheet) << "Text,Translation\n\"Hi.\",\"Hola.\"\n\"Bye.\",\"Adiós.\"\n\"Not in the game\",\"x\"\n";
        std::printf("   %s\n", e.developer_.importTranslationSheet().c_str());
        const std::string file = read(g + "/translations/spanish.vnscript");
        std::printf("%s", file.c_str());
        CHECK(file.contains("\"Bye.\"\n    \"Adiós.\"\n") && file.contains("\"Hi.\"\n    \"Hola.\"\n") && !file.contains("Not in the game"));
        CHECK(d.translateUndo.size() == 1);
    }

    if (want("drop")) {
        std::printf("drop. an image dropped on the Screens tab's screen becomes a part there\n");
        const std::string g = S + "/h/drop";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"D\"\nconfig developer yes\ninclude screens\n\nlabel start\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    container x=640 y=360 w=640 h=360 bg=#203040\n");
        fs::create_directories(S + "/h/dropfiles");
        if (SDL_Surface* sf = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_ARGB8888)) {
            SDL_FillSurfaceRect(sf, nullptr, 0xFFFF0000);
            SDL_SaveBMP(sf, (S + "/h/dropfiles/Red Dot.bmp").c_str());
            SDL_DestroySurface(sf);
        }
        fs::remove_all(S + "/h/drop_data");
        auto dr = start(out, g, S + "/h/drop_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        d.view = View::Screens;
        d.screenName = "panel";
        d.part.reset();
        dr->frames(3);
        const SDL_FRect shown = d.shownScreen;
        const float scale = shown.w / e.canvas().w;
        // Dropped at (100, 50) of the screen, with no part chosen: on the screen, by its corner.
        {
            const std::scoped_lock lock(d.imports.mutex);
            d.imports.files.emplace_back(S + "/h/dropfiles/Red Dot.bmp", std::string{});
            d.imports.dropAt = SDL_FPoint{shown.x + 100 * scale, shown.y + 50 * scale};
        }
        dr->frames(3);
        std::string screens = read(g + "/scripts/screens.vnscript");
        std::printf("%s", screens.c_str());
        CHECK(screens.contains("    image \"Red_Dot\" x=-0.8438 y=0.8611\n"));
        CHECK(d.view == View::Screens && d.part.has_value() && fs::exists(g + "/images/Red_Dot.bmp"));
        // The container chosen, and a drop inside it: in it, at its own place there.
        for (const auto& sc : d.editor.screens())
            for (const auto& p : d.editor.parts(sc))
                if (p.kind == "container") d.part = p.line;
        dr->frames(2);
        {
            const std::scoped_lock lock(d.imports.mutex);
            d.imports.files.emplace_back(S + "/h/dropfiles/Red Dot.bmp", std::string{});
            d.imports.dropAt = SDL_FPoint{shown.x + 800 * scale, shown.y + 450 * scale};
        }
        dr->frames(3);
        screens = read(g + "/scripts/screens.vnscript");
        std::printf("%s", screens.c_str());
        CHECK(screens.contains("        image \"Red_Dot_2\" x=-0.5 y=0.5\n"));
        dr->shot("drop_screen.png");
    }

    if (want("unused")) {
        std::printf("unused. the Library marks files no script names\n");
        const std::string g = S + "/h/unused";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"U\"\nconfig developer yes\ninclude screens\ncharacter Aiko color=pink\n\nlabel start\n    show Aiko\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    image \"logo\" x=0 y=0\n");
        for (const char* name : {"logo", "aiko", "aiko_happy", "forgotten", "logo2"})
            if (SDL_Surface* sf = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888)) {
                fs::create_directories(g + "/images");
                SDL_SaveBMP(sf, std::format("{}/images/{}.bmp", g, name).c_str());
                SDL_DestroySurface(sf);
            }
        fs::remove_all(S + "/h/unused_data");
        auto dr = start(out, g, S + "/h/unused_data");
        auto& d = dr->engine.developer_.tools;
        CHECK(d.editor.mentions("logo") && !d.editor.mentions("logo2") && !d.editor.mentions("log") && d.editor.mentions("AIKO"));
        d.view = View::Library;
        d.library = "pictures";
        dr->frames(3);
        for (const auto& n : d.unused) std::printf("   unused: %s\n", n.c_str());
        CHECK((d.unused == std::set<std::string>{"forgotten.bmp", "logo2.bmp"}));
        dr->shot("unused_library.png");
    }

    if (want("caches")) {
        std::printf("caches. Find, a variable's uses and the voices' files are looked for again when the scripts (or the files) change, not every frame; a choice's options keep their texts by option; plain text isn't read for tags\n");
        const std::string g = S + "/h/caches";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"C\"\nconfig developer yes\ninclude screens\nvariable mood 1\ncharacter Aiko color=pink voices=aiko\n\n"
                                            "label start\n    set mood to 2\n    Aiko: Hello.\n    choice\n        First (if mood > 5)\n            Yes.\n        Second\n            No.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    text \"Plain\" x=0 y=0\n");
        fs::remove_all(S + "/h/caches_data");
        auto dr = start(out, g, S + "/h/caches_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty());
        // Find: looked for once, and again once the scripts change.
        d.view = View::Story;
        d.find = "mood";
        dr->frames(2);
        const std::size_t found = d.lookedFind.found.size();
        CHECK(found > 0 && d.lookedFind.what == "mood" && d.lookedFind.version == d.editor.version());
        d.editor.pasteScene(0, {"label extra", "    set mood to 3"});
        dr->frames(2);
        std::printf("   Find 'mood': %zu lines, then %zu\n", found, d.lookedFind.found.size());
        CHECK(d.lookedFind.version == d.editor.version() && d.lookedFind.found.size() == d.editor.find("mood").size() && d.lookedFind.found.size() > found);
        d.find.clear();
        // A variable's uses: the same.
        d.view = View::Library;
        d.library = "variables";
        for (std::size_t i = 0; i < 8 && d.lookedUses.what != "mood"; ++i) { // (its place in the list: whatever the list has)
            d.libraryItem = i;
            dr->frames(1);
        }
        const std::size_t uses = d.lookedUses.found.size();
        CHECK(d.lookedUses.what == "mood" && uses == d.editor.uses("mood").size() && uses > 0);
        d.editor.pasteScene(0, {"label more", "    set mood to 4"});
        dr->frames(2);
        std::printf("   lines that use mood: %zu, then %zu\n", uses, d.lookedUses.found.size());
        CHECK(d.lookedUses.found.size() == d.editor.uses("mood").size() && d.lookedUses.found.size() > uses);
        // Voices: the line's file isn't there; once it is, and the files are looked at again, it is.
        d.library = "voices";
        d.libraryItem.reset();
        dr->frames(2);
        CHECK(d.voiced.size() == 1);
        if (d.voiced.size() == 1) {
            const std::string file = d.voiced[0].file;
            CHECK(d.voiceThere.contains(file) && !d.voiceThere.at(file));
            fs::create_directories(fs::path(g + "/audio/" + file).parent_path());
            silentWav(g + "/audio/" + file + ".wav");
            ++d.filesGeneration; // (as twice a second does)
            dr->frames(2);
            std::printf("   the voice %s: there now: %s\n", file.c_str(), d.voiceThere.contains(file) && d.voiceThere.at(file) ? "yes" : "no");
            CHECK(d.voiceThere.contains(file) && d.voiceThere.at(file));
        }
        // A choice whose first option isn't shown (its if): the second option's text stays the second one's.
        d.view = View::Story;
        e.newGame("start");
        d.open = false;
        dr->frames(3);
        for (int i = 0; i < 6 && !e.game().showingChoices(); ++i) {
            e.run(vn::Commands{vn::cmd::Ui{.verb = "advance"}});
            dr->frames(2);
        }
        CHECK(e.game().showingChoices());
        if (vn::ScreenView* v = e.view(e.game().choiceScreen())) {
            const auto st = std::ranges::find_if(v->state_, [](const auto& s) { return !s.items.empty(); });
            CHECK(st != v->state_.end() && st->items.size() == 2 && st->items[0].text().empty() && st->items[1].text() == "Second");
            CHECK(v->rich("Plain").spans.empty() && v->rich("Plain").text == "Plain" && v->rich("{bold}B{/bold}").spans.size() == 1);
        } else {
            CHECK(false);
        }
    }

    if (want("bidi")) {
        std::printf("bidi. a line right to left with left-to-right words and numbers in it reads as Unicode's bidirectional algorithm says\n");
        const std::string g = S + "/h/bidi";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"B\"\nconfig developer yes\n\nlabel start\n    Hi.\n");
        fs::remove_all(S + "/h/bidi_data");
        auto dr = start(out, g, S + "/h/bidi_data");
        const vn::Font& font = dr->engine.fonts_.get("default", 18);
        // Hebrew first (so it reads right to left), then "abc 123": the two stay in their order, abc on the left.
        vn::TextBlock block;
        block.set(font, "\u05D0\u05D1\u05D2 abc 123");
        const auto abc = block.boxes(4, 7), number = block.boxes(8, 11), hebrew = block.boxes(0, 3);
        CHECK(!abc.empty() && !number.empty() && !hebrew.empty());
        if (!abc.empty() && !number.empty() && !hebrew.empty()) {
            std::printf("   abc at %.0f, 123 at %.0f, the Hebrew at %.0f\n", abc[0].x, number[0].x, hebrew[0].x);
            CHECK(abc[0].x < number[0].x && number[0].x < hebrew[0].x);
        }
        // A left-to-right line with a Hebrew word: the word goes between, the line reads left to right.
        block.set(font, "abc \u05D0\u05D1\u05D2 def");
        const auto first = block.boxes(0, 3), word = block.boxes(4, 7), last = block.boxes(8, 11);
        CHECK(!first.empty() && !word.empty() && !last.empty() && first[0].x < word[0].x && word[0].x < last[0].x);
    }

    if (want("signing")) {
        std::printf("signing. a game says what Pack the game signs its Mac app with (a Developer ID, and the profile it's notarized with): the editor's Settings rows read them\n");
        const std::string g = S + "/h/signing";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"Sg\"\nconfig developer yes\nconfig mac_signing \"Developer ID Application: Someone (ABCDE12345)\"\n"
                                            "config notary_profile \"vnext-notary\"\n\nlabel start\n    Hi.\n");
        fs::remove_all(S + "/h/signing_data");
        auto dr = start(out, g, S + "/h/signing_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s\n", p.message.c_str());
        CHECK(d.problems.empty());
        CHECK(e.script_.config.macSigning == "Developer ID Application: Someone (ABCDE12345)" && e.script_.config.notaryProfile == "vnext-notary");
        CHECK(d.editor.setting("mac_signing").value_or("") == "Developer ID Application: Someone (ABCDE12345)" && d.editor.setting("notary_profile").value_or("") == "vnext-notary");
        d.view = View::Settings;
        dr->frames(2);
        CHECK(d.problems.empty());
    }

    if (want("packed")) {
        std::printf("packed. a game packed into game.vnext plays from it: its scripts, pictures, sounds and translations come through the engine's own files\n");
        const std::string g = S + "/h/packed", run = S + "/h/packed_run";
        fs::remove_all(g);
        fs::remove_all(run);
        write(g + "/scripts/main.vnscript", "config title \"P\"\n\nlabel start\n    scene pic\n    play sound beep\n    Hi.\n");
        write(g + "/translations/magyar.vnscript", "language \"Magyar\"\n\n\"Hi.\"\n    \"Szia.\"\n");
        fs::create_directories(g + "/images");
        if (SDL_Surface* sf = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888)) {
            SDL_SaveBMP(sf, (g + "/images/pic.bmp").c_str());
            SDL_DestroySurface(sf);
        }
        fs::create_directories(g + "/audio");
        silentWav(g + "/audio/beep.wav");
        fs::remove_all(S + "/h/packed_data");
        fs::remove_all(S + "/h/packed_data2");
        {
            auto dr = start(out, g, S + "/h/packed_data");
            fs::create_directories(run);
            const auto made = dr->engine.pack(run + "/game.vnext");
            if (!made) std::printf("   %s\n", made.error().c_str());
            CHECK(made.has_value() && fs::exists(run + "/game.vnext"));
        }
        auto dr = start(out, run + "/game.vnext", S + "/h/packed_data2"); // (--game the pack: its folder has nothing else in it)
        auto& e = dr->engine;
        std::printf("   packed: %s, its root %s, %zu translation(s)\n", e.packed_ ? "yes" : "no", e.gameRoot_.c_str(), e.translations_.size());
        CHECK(e.packed_ && e.script_.ok() && !fs::exists(run + "/scripts"));
        CHECK(static_cast<bool>(e.assets_->picture("pic")));
        CHECK(e.files().exists(e.gameRoot_ + "audio/beep.wav") && !vn::fileExists(e.gameRoot_ + "audio/beep.wav")); // (in the pack, not on the disk)
        CHECK(e.files().readText(e.gameRoot_ + "scripts/main.vnscript").value_or("").contains("label start"));
        CHECK(e.translations_.size() == 1);
    }

    if (want("preview")) {
        std::printf("preview. the Screens tab's save, history and phone screens show made-up content (screenshots)\n");
        const std::string g = S + "/h/preview";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"P\"\nconfig developer yes\ninclude screens\n\nlabel start\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen save\n    slots x=40 y=40 w=1200 h=640\n\nscreen history\n    history x=40 y=40 w=1200 h=640\n\n"
                                               "screen phone\n    contacts x=40 y=40 w=400 h=640\n    phone x=480 y=40 w=760 h=640\n");
        fs::remove_all(S + "/h/preview_data");
        auto dr = start(out, g, S + "/h/preview_data");
        auto& d = dr->engine.developer_.tools;
        CHECK(d.problems.empty());
        d.view = View::Screens;
        for (const char* name : {"save", "history", "phone"}) {
            d.screenName = name;
            d.part.reset();
            dr->frames(3);
            dr->shot(std::format("preview_{}.png", name));
        }
    }

    if (want("nomessages")) {
        std::printf("nomessages. no built-in messages: parts say their own words, config text lines are mistakes now\n");
        CHECK(vn::keyWord("mouse_left") == "Left click" && vn::keyWord("pad_a") == "A" && vn::keyWord("nope") == "nope");
        const std::string g = S + "/h/nomessages";
        fs::remove_all(g);
        const std::string main = "config title \"M\"\nconfig developer yes\ninclude screens\nending good \"A good end\"\n\nlabel start\n    Hi.\n    ending good\n";
        write(g + "/scripts/main.vnscript", "config title \"M\"\nconfig developer yes\nconfig text.saved \"Game saved!\"\ninclude screens\nending good \"A good end\"\n\nlabel start\n    Hi.\n    ending good\n");
        write(g + "/scripts/screens.vnscript",
              "screen save\n    pages x=200 y=26 h=34 item_w=44 gap=6 auto=Auto quick=Quick\n"
              "    slots x=60 y=92 w=1160 h=600 confirm=\"Overwrite?\" load_confirm=\"Load?\" delete_confirm=\"Delete?\"\n\n"
              "screen endings\n    endings x=240 y=110 w=800 h=500 hidden=???\n\nscreen controls\n    control advance x=510 y=344 w=260 h=32 waiting=\"Press a key...\"\n\n"
              "screen chart\n    chart x=60 y=92 w=1160 h=560 confirm=\"Go back?\"\n");
        fs::remove_all(S + "/h/nomessages_data");
        auto dr = start(out, g, S + "/h/nomessages_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.size() == 1 && d.problems[0].message == "There's no config setting called 'text.saved'.");
        write(g + "/scripts/main.vnscript", main);
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        CHECK(d.problems.empty());
        // A translation file lists the key names the Controls screen shows, and nothing of the old messages.
        const vn::TranslationFile file = vn::translationFile(*d.script, "spanish", std::nullopt);
        CHECK(file.text.contains("# ---- Key names ----") && file.text.contains("\"Left click\"") && !file.text.contains("config text"));
        d.view = View::Screens;
        for (const char* name : {"save", "endings"}) {
            d.screenName = name;
            d.part.reset();
            dr->frames(3);
            dr->shot(std::format("nomessages_{}.png", name));
        }
    }

    if (want("forms")) {
        std::printf("forms. the Story and Library forms (screenshots: defaults in grey)\n");
        const std::string g = S + "/h/forms";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript",
              "config title \"F\"\nconfig developer yes\ncharacter Aiko color=pink\nstyle big size=30\nanimation pop\n    zoom=1.2 over 0.3 seconds\n\n"
              "label start\n    music theme\n    show Aiko at left\n    move Aiko to right\n    if trust is at least 3\n        shake\n    Aiko: Hi.\n");
        fs::remove_all(S + "/h/forms_data");
        auto dr = start(out, g, S + "/h/forms_data");
        auto& d = dr->engine.developer_.tools;
        d.view = View::Story;
        d.scene = 0;
        const auto rows = d.editor.rows(d.editor.scenes()[0]);
        for (std::size_t i = 0; i < std::min<std::size_t>(rows.size(), 4); ++i) {
            d.row = rows[i].line;
            dr->frames(3);
            dr->shot(std::format("forms_story_{}.png", i));
        }
        d.row.reset();
        d.character = 0;
        dr->frames(3);
        dr->shot("forms_character.png");
        d.view = View::Library;
        for (const char* kind : {"style", "animation"}) {
            d.library = kind;
            d.libraryItem = 0;
            dr->frames(3);
            dr->shot(std::format("forms_{}.png", kind));
        }
    }

    if (want("licenses")) {
        std::printf("licenses. the packed licenses.txt is the About tab's text, with every library built in and its real version\n");
        const std::string g = S + "/h/licenses";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"L\"\nconfig developer yes\n\nlabel start\n    Hi.\n");
        fs::remove_all(S + "/h/licenses_data");
        fs::remove_all(S + "/h/licenses_out");
        fs::create_directories(S + "/h/licenses_out");
        auto dr = start(out, g, S + "/h/licenses_data");
        auto& e = dr->engine;
        auto trimmed = [](std::string t) { while (t.ends_with('\n')) t.pop_back(); return t; };
        const std::string text = trimmed(std::string(vn::licenses()));
        std::string about;
        for (const auto& l : vn::aboutLines()) about += std::string(l.text) + "\n";
        CHECK(trimmed(about) == text);
        const auto packed = e.pack(S + "/h/licenses_out/game.vnext");
        if (!packed) std::printf("   %s\n", packed.error().c_str());
        CHECK(packed.has_value() && fs::exists(S + "/h/licenses_out/licenses.txt"));
        CHECK(trimmed(read(S + "/h/licenses_out/licenses.txt")) == text);
        for (const char* name : {"FreeType 2.13.2", "HarfBuzz 8.5.0", "SheenBidi 3.0.0", "pl_mpeg", "stb_image", "stb_vorbis", "dr_mp3", "dr_flac", "miniz", "tiny_jpeg", "QOI", "NanoSVG",
                                 "HIDAPI", "yuv2rgb", "Unicode", "Wayland"})
            CHECK(text.contains(name));
        for (const auto& [lib, version] : {std::pair{"SDL", SDL_GetVersion()}, {"SDL_image", IMG_Version()}, {"SDL_ttf", TTF_Version()}, {"SDL_mixer", MIX_Version()}}) {
            const std::string named = std::format("{} {}", lib, vn::versionOf(version));
            std::printf("   %s\n", named.c_str());
            CHECK(text.contains(named));
        }
    }

    if (want("resize")) {
        std::printf("resize. a full-screen image of a colour resized by its corner keeps its corner; with Shift it keeps its shape; y is up\n");
        const std::string g = S + "/h/resize";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"R\"\nconfig developer yes\ninclude screens\n\nlabel start\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    image #203040 x=0 y=0 w=1280 h=720\n");
        fs::remove_all(S + "/h/resize_data");
        auto dr = start(out, g, S + "/h/resize_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        d.view = View::Screens;
        d.screenName = "panel";
        for (const auto& sc : d.editor.screens())
            if (sc.name == "panel") d.part = d.editor.parts(sc).front().line;
        dr->frames(3);
        const SDL_FRect shown = d.shownScreen;
        const float scale = shown.w / e.canvas().w;
        auto at = [&](float x, float y) { return SDL_FPoint{shown.x + x * scale, shown.y + y * scale}; };
        auto dragCorner = [&](float dx, float dy) { // (the handle is at the part's bottom-right corner)
            const SDL_FPoint from = at(1280 - 3, 720 - 3), to = at(1280 - 3 + dx, 720 - 3 + dy);
            mouse(*dr, SDL_EVENT_MOUSE_MOTION, from.x, from.y);
            dr->frames(1);
            mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, from.x, from.y);
            dr->frames(2);
            CHECK(d.drag && d.drag->resize);
            mouse(*dr, SDL_EVENT_MOUSE_MOTION, to.x, to.y);
            dr->frames(2);
            mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, to.x, to.y);
            dr->frames(3);
        };
        dragCorner(-320, -180); // 1280x720 -> 960x540, the top-left corner staying
        std::string screens = read(g + "/scripts/screens.vnscript");
        std::printf("%s", screens.c_str());
        CHECK(screens.contains("image #203040 x=-1 y=1 w=0.75 h=0.75\n"));
        // Shift held while dragging (a Shift+click chooses more parts instead): pulled 240 in and only
        // 10 up, it keeps its shape (the width's change wins).
        const SDL_FPoint from = at(960 - 3, 540 - 3), to = at(960 - 3 - 240, 540 - 3 - 10);
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, from.x, from.y);
        dr->frames(1);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, from.x, from.y);
        dr->frames(2);
        CHECK(d.drag && d.drag->resize);
        SDL_SetModState(SDL_KMOD_SHIFT);
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, to.x, to.y);
        dr->frames(2);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, to.x, to.y);
        dr->frames(3);
        SDL_SetModState(SDL_KMOD_NONE);
        screens = read(g + "/scripts/screens.vnscript");
        std::printf("%s", screens.c_str());
        CHECK(screens.contains("image #203040 x=-1 y=1 w=0.5625 h=0.5625\n")); // (720x405: three quarters of 960x540)
        // y: 1 is the top, -1 the bottom; x: -1 the left, 1 the right.
        if (const vn::ScreenDef* def = d.script->screen("panel")) {
            vn::ScreenView v(*def, e);
            v.draw(e.renderer_.get());
            CHECK(v.rectOf(0).x == 0.0f && v.rectOf(0).y == 0.0f);
        }
        CHECK(vn::placeEdge(-1.0f, 100.0f, 1000.0f) == 0.0f && vn::placeEdge(1.0f, 100.0f, 1000.0f) == 900.0f); // (x: left to right)
        CHECK(vn::placeEdge(-1.0f, 100.0f, 720.0f) == 0.0f);                                                     // (y is given as -place: 1 lands at the top)
    }

    if (want("starts")) {
        std::printf("starts. the ready-made screens: the same layout, nothing extra, the empty one empty, and all of them read without mistakes\n");
        auto has = [](std::string_view n) { return !n.starts_with("chat"); }; // (the game has every menu; the phone's chat screen is new)
        auto uses = [](std::string_view) { return true; };                      // (it uses voices, blips, movies: every settings row)
        const auto empty = vn::startScreen("", "new_screen", has, uses);
        CHECK(empty.size() == 1 && empty[0] == "screen new_screen");
        std::string all(vn::kLook);
        for (const char* name : {"save", "load", "settings", "history", "credits"}) { // (the full-screen menus: backdrop, title, Back, Esc)
            const auto lines = vn::startScreen(name, name, has, uses);
            const std::string text = vn::joinedLines(lines);
            CHECK(text.contains("    image backdrop x=0 y=0 w=1280 h=720\n"));
            CHECK(text.contains(" x=60 y=32\n") || text.contains(" x=60 y=32 "));
            CHECK(text.contains("    button \"Back\" x=1080 y=654 w=140 h=40 style=small_button action=close\n"));
            CHECK(text.contains("    key escape action=close\n"));
            std::printf("   %s: %zu lines\n", name, lines.size());
        }
        for (const char* name : {"title", "pause"}) { // (the backdrop, the name at the top left, buttons down the middle)
            const auto lines = vn::startScreen(name, name, has, uses);
            const std::string text = vn::joinedLines(lines);
            CHECK(text.contains("    image ") && text.contains(" x=60 y=32\n") && text.contains(" x=460 y=") && text.contains(" w=360 h=52 action="));
            CHECK(!text.contains("gradient") && !text.contains("style=title"));
        }
        { // (the built-in textbox and choice menu, as the game's own: the palette's colours by name, the parts' look in the look's styles for their kinds)
            const auto say = vn::ownScreenLines("say", 1.0f);
            for (const auto& l : say) std::printf("   %s\n", l.c_str());
            CHECK(say.size() == 5 && say[1].contains("image panel ") && say[1].contains("border=line") && vn::trim(say[2]) == "namebox x=-0.9 y=-0.3947 h=0.0639 bar=3");
            CHECK(vn::trim(say[3]) == "dialogue x=0 y=-0.4667 w=0.8875" && vn::trim(say[4]) == "indicator x=0.8938 y=-0.8305 w=0.0141 h=0.0167");
            CHECK(vn::ownScreenLines("choice", 1.0f).size() == 2);
            CHECK(std::string(vn::defaultUi()).contains("image #141626fa ") && std::string(vn::defaultUi()).contains("border=#ffffff2d")); // (the built-in one looks the same)
            // The look's styles for those parts say just what the built-in lines have on the parts (so the copy looks the same).
            std::map<std::string, std::string> codes;                // colour name -> code
            std::map<std::string, std::set<std::string>> styles;     // kind -> its style's words, colours as codes
            const auto look = vn::linesOf(std::string(vn::kLook));
            for (const std::string& l : look)
                if (const auto w = vn::rawWords(l); w.size() == 3 && w[0] == "color") codes[w[1]] = w[2];
            for (const std::string& l : look) {
                const auto w = vn::rawWords(l);
                if (w.size() < 2 || w[0] != "style") continue;
                for (std::size_t i = 2; i < w.size(); ++i) {
                    std::string word = w[i];
                    const auto eq = word.find('=');
                    if (const auto c = eq == std::string::npos ? codes.end() : codes.find(word.substr(eq + 1)); c != codes.end()) word = word.substr(0, eq + 1) + c->second;
                    styles[w[1]].insert(word);
                }
            }
            CHECK(styles.contains("namebox") && styles.contains("dialogue") && styles.contains("indicator") && styles.contains("choice"));
            bool in = false;
            for (const std::string& l : vn::linesOf(std::string(vn::defaultUi()))) {
                if (vn::indentOf(l) == 0) in = l.starts_with("screen say");
                if (!in || vn::indentOf(l) == 0) continue;
                const auto w = vn::rawWords(vn::trim(l));
                if (!styles.contains(w[0])) continue;
                const std::set<std::string> builtin(w.begin() + 1, w.end());
                std::set<std::string> own, together;
                for (const auto& o : say)
                    if (const auto ow = vn::rawWords(vn::trim(o)); ow[0] == w[0]) own.insert(ow.begin() + 1, ow.end());
                together = own;
                together.insert(styles[w[0]].begin(), styles[w[0]].end());
                std::printf("   %s: %zu words on the part, %zu in its style\n", w[0].c_str(), own.size(), styles[w[0]].size());
                CHECK(together == builtin && own.size() + styles[w[0]].size() == builtin.size());
            }
        }
        for (const char* name : {"confirm", "phone"}) { // (panels: the one panel look, their buttons the size of every menu's)
            const auto lines = vn::startScreen(name, name, has, uses);
            const std::string text = vn::joinedLines(lines);
            CHECK(text.contains("image panel ") && text.contains(" border=accent\n") && text.contains(" w=140 h=40 style=small_button action=") && !text.contains("style=heading"));
        }
        CHECK(!std::string(vn::kPhoneScreen).contains("No messages")); // (an empty phone shows nothing, like empty slots)
        for (const vn::ScreenStart& s : vn::kScreenStarts) { // (the + New screen list: a name, and what's on it, in one form)
            const std::string label(s.label), help(s.help);
            CHECK(!help.contains('(') && !help.contains("...") && !help.ends_with('.') && !label.contains(" screen") == (label != "Empty screen"));
            std::printf("   %s: %s\n", label.c_str(), help.c_str());
        }
        for (const char* name : {"title", "pause", "save", "load", "settings", "history", "confirm", "phone", "credits", "choice"}) {
            auto lines = vn::startScreen(name, name, has, uses);
            if (std::string_view(name) != "choice") lines = vn::placesFromPixels(lines, 1280.0f, 720.0f);
            all += "\n\n";
            for (const auto& l : lines) all += l + "\n";
        }
        for (const auto& [name, control] : vn::kScreenControls) all += std::string(control) + "\n";
        const std::string g = S + "/h/starts";
        fs::remove_all(g);
        fs::create_directories(g + "/scripts");
        std::ofstream(g + "/scripts/screens.vnscript") << all; // (in places and shares already)
        std::ofstream(g + "/scripts/main.vnscript") << "config title \"S\"\nconfig developer yes\ninclude screens\n\nlabel start\n    Hi.\n";
        fs::remove_all(S + "/h/starts_data");
        auto dr = start(out, g, S + "/h/starts_data");
        auto& d = dr->engine.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty());
        d.view = View::Screens;
        for (const char* name : {"title", "pause", "save", "load", "history", "credits", "settings", "confirm", "phone", "chat"}) {
            d.screenName = name;
            d.part.reset();
            dr->frames(3);
            dr->shot(std::format("starts_{}.png", name));
        }
    }

    if (want("parts")) {
        std::printf("parts. a new part starts where the ready-made screens have one (the textbox's where the built-in textbox has them), with the look's colours by name, and every one reads without mistakes\n");
        std::string all;
        for (const vn::PartStart& p : vn::kParts) {
            const std::string line = vn::replaced(vn::replaced(std::string(p.line), "PICTURE", "pic"), "MOVIE", "mov");
            CHECK(!line.contains('#')); // (the look's colours by name, never a code)
            all += line + "\n";
        }
        CHECK(all.contains("button \"Button\" x=460 y=334 w=360 h=52\n") && !all.contains("rect ") && !all.contains("gradient ")); // (an image is a picture, a colour or a fade)
        CHECK(all.contains("slots x=60 y=92 ") && all.contains("gallery x=60 y=92 w=1160 h=560 ") && all.contains("phone x=900 y=140 w=340 h=270 "));
        CHECK(!all.contains("hint=") && all.contains("key escape action=close\n"));
        const auto ui = vn::linesOf(std::string(vn::defaultUi()));
        for (const char* kind : {"namebox", "dialogue", "indicator", "choices"}) { // (in places and shares: the built-in textbox's own lines)
            const auto it = std::ranges::find_if(vn::kParts, [&](const vn::PartStart& p) { return vn::lower(std::string(p.kind)) == kind; });
            CHECK(it != vn::kParts.end());
            const auto in = vn::placesFromPixels({"screen s", "    " + std::string(it->line)}, 1280.0f, 720.0f);
            const std::string units(vn::trim(in.back()));
            const bool found = std::ranges::any_of(ui, [&](const std::string& l) { return std::string(vn::trim(l)).starts_with(units); });
            std::printf("   %s -> %s%s\n", std::string(it->line).c_str(), units.c_str(), found ? "" : "  (NOT the built-in textbox's)");
            CHECK(found);
        }
        const std::string g = S + "/h/parts";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"P\"\nconfig developer yes\ninclude screens\n\nlabel start\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    image #203040 x=0 y=0 w=1280 h=720\n");
        fs::create_directories(g + "/images");
        if (SDL_Surface* pic = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32)) { // (a picture for the image part: + New part picks the game's first)
            SDL_FillSurfaceRect(pic, nullptr, 0xff4080c0u);
            IMG_SavePNG(pic, (g + "/images/pic.png").c_str());
            SDL_DestroySurface(pic);
        }
        fs::remove_all(S + "/h/parts_data");
        auto dr = start(out, g, S + "/h/parts_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        { // (the built-in textbox as the game's own, as Make it the game's own does: its parts' styles and their colours come along)
            const auto own = vn::ownScreenLines("say", 1.0f);
            const auto needs = vn::needsOf(d.editor, own, "say");
            const std::string text = vn::joinedLines(needs);
            std::printf("%s", text.c_str());
            CHECK(text.contains("style namebox size=28 bg=card_hover padding=20 color=speaker textbox_opacity=yes\n") && text.contains("style dialogue size=28 text_size=yes\n"));
            CHECK(text.contains("style indicator color=accent\n") && text.contains("color card_hover #222644f5\n") && text.contains("color accent #96aaff\n") && text.contains("color panel "));
            const auto choice = vn::needsOf(d.editor, vn::ownScreenLines("choice", 1.0f), "choice");
            const std::string ct = vn::joinedLines(choice);
            CHECK(ct.contains("style choice parent=button\n") && ct.contains("style button ") && ct.contains("color hover "));
            d.editor.addScreen(needs, own);
        }
        for (const vn::PartStart& p : vn::kParts) { // (every starter added to the one screen, as + New part adds them; no movie file to play)
            if (p.line.contains("MOVIE")) continue;
            const std::string line = vn::replaced(std::string(p.line), "PICTURE", "pic");
            const std::string units(vn::trim(vn::placesFromPixels({"screen new", "    " + line}, 1280.0f, 720.0f).back()));
            const auto needs = vn::needsOf(d.editor, {"screen new", "    " + units}, "new");
            d.editor.addPart(d.editor.screens().front(), nullptr, units, needs);
        }
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        const std::string screens = read(g + "/scripts/screens.vnscript");
        std::printf("%s", screens.c_str());
        CHECK(screens.contains("color panel #141626fa\n") && screens.contains("style button ") && screens.contains("style slot "));
        CHECK(screens.find("style slot ") < screens.find("screen panel")); // (the look a part needs goes above its screen)
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty());
        d.view = View::Screens;
        for (const char* name : {"panel", "say"}) {
            d.screenName = name;
            d.part.reset();
            dr->frames(3);
            dr->shot(std::format("parts_{}.png", name));
        }
        d.screenName = "panel";
        d.addingPart = true; // (+ New part: the kinds, grouped on their rows)
        dr->frames(3);
        dr->shot("parts_kinds.png");
        d.addingPart = false;
        // A part's look has the kinds of boxes the forms have everywhere: a button's image lists the pictures, its background colour opens the wheel.
        const auto& panel = *std::ranges::find(d.editor.screens(), std::string("panel"), &vn::StoryEditor::Screen::name);
        const auto panelParts = d.editor.parts(panel);
        const auto button = std::ranges::find(panelParts, std::string("button"), &vn::StoryEditor::Part::kind);
        CHECK(button != panelParts.end());
        d.part = button->line;
        dr->frames(3);
        for (const auto& [box, pick] : {std::pair{"image", vn::StoryEditor::Pick::PicturesOrColors}, {"bg", vn::StoryEditor::Pick::Colors}}) {
            const std::string id = std::format("part:{}:{}:{}", panel.line, button->line, box);
            d.openList = id;
            dr->frames(3);
            std::printf("   a button's %s: popup '%s', pick %d, %.0f high\n", box, d.popup.id.c_str(), static_cast<int>(d.popup.field.pick), d.popup.box.h);
            CHECK(d.popup.id == id && d.popup.field.pick == pick && d.popup.box.h > 0.0f);
            if (std::string_view(box) == "image") dr->shot("parts_image_list.png"); // (a picture's box: the wheel, the pictures under it)
            d.popup = {};
        }
        { // The first cell of a picture's list, None, clears the box: the button's picture goes, and the line with it.
            vn::StoryEditor::Part withPic = *button;
            withPic.set("image", "pic");
            d.editor.setPart(panel, withPic, "image");
            dr->frames(3);
            const auto again = d.editor.parts(panel);
            const auto pictured = std::ranges::find(again, std::string("button"), &vn::StoryEditor::Part::kind);
            CHECK(pictured != again.end() && pictured->option("image") == "pic");
            const std::string id = std::format("part:{}:{}:image", panel.line, pictured->line);
            d.openList = id;
            dr->frames(3);
            const float k = d.popup.at.h / vn::layout::boxH, pad = vn::layout::popupPad * k, valueH = vn::layout::codeH * k, itemH = vn::layout::itemH * k;
            const float discD = vn::layout::wheelD * k, cellW = vn::layout::cellW * k, cellH = vn::layout::cellH * k;
            const float listTop = d.popup.box.y + pad + valueH + pad + itemH + pad + discD + pad; // (the code, the gradient buttons, the wheel; then "depends on a variable", then the cells)
            click(*dr, d.popup.box.x + pad + cellW / 2, listTop + itemH + cellH / 2);
            dr->frames(3);
            const auto after = d.editor.parts(panel);
            const auto cleared = std::ranges::find(after, std::string("button"), &vn::StoryEditor::Part::kind);
            std::printf("   None picked for its picture: %s\n", cleared != after.end() ? vn::StoryEditor::lineOf(*cleared).c_str() : "(no button)");
            CHECK(cleared != after.end() && cleared->option("image").empty() && !vn::StoryEditor::lineOf(*cleared).contains("image=") && d.popup.id.empty());
        }
    }

    if (want("newgame")) {
        std::printf("newgame. what New game writes: a one-line note, the setting it needs, label start and a first line; it opens in the Story tab with no mistakes\n");
        const auto lines = vn::linesOf(std::string(vn::kNewGameScript));
        for (const auto& l : lines) std::printf("   %s\n", l.c_str());
        CHECK(lines.size() == 6 && lines[0].starts_with("# ") && !lines[0].contains('(') && lines[1] == "config developer yes" && lines[2].empty());
        CHECK(lines[3] == "label start" && lines[4].starts_with("    ") && !lines[4].contains("config") && lines[5].empty());
        CHECK(!std::string(vn::kNewGameScript).contains("title") && !std::string(vn::kNewGameScript).contains("save_id"));
        const std::string g = S + "/h/newgame";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript));
        fs::remove_all(S + "/h/newgame_data");
        auto dr = start(out, g, S + "/h/newgame_data");
        auto& d = dr->engine.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty() && d.view == View::Story && d.editor.scenes().size() == 1 && d.editor.scenes()[0].name == "start");
        dr->frames(3);
        dr->shot("newgame.png");
    }

    if (want("scenes")) {
        std::printf("scenes. + New scene adds an empty scene called new_scene, + New file a file called new_file with a one-line note (included from main), their names ready to type\n");
        const std::string g = S + "/h/scenes";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript));
        fs::remove_all(S + "/h/scenes_data");
        auto dr = start(out, g, S + "/h/scenes_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        CHECK(d.view == View::Story && d.editor.scenes().size() == 1);
        dr->frames(2);
        click(*dr, 70.0f, 409.0f); // + New scene (under the scenes list)
        dr->frames(3);
        std::printf("   focus '%s', typing '%s'\n", d.focus.c_str(), d.buffer.c_str());
        CHECK(d.editor.scenes().size() == 2 && d.editor.scenes()[1].name == "new_scene" && d.scene == 1);
        CHECK(d.focus == "scenes:rename:1" && d.buffer == "new_scene");
        dr->shot("scenes_new.png");
        std::string main = read(g + "/scripts/main.vnscript");
        CHECK(main.ends_with("    This is the first line of the story.\n\nlabel new_scene\n")); // (nothing in it)
        CHECK(d.editor.scenes()[d.editor.addScene(0)].name == "new_scene_2");
        const std::size_t file = d.editor.addFile();
        CHECK(d.editor.fileName(file) == "new_file" && read(g + "/scripts/new_file.vnscript") == "# More of the story.\n");
        main = read(g + "/scripts/main.vnscript");
        CHECK(main.contains("config developer yes\ninclude new_file\n"));
        CHECK(d.editor.fileName(d.editor.addFile()) == "new_file_2");
        const std::size_t in = d.editor.addScene(file);
        CHECK(d.editor.scenes()[in].name == "new_scene_3" && read(g + "/scripts/new_file.vnscript") == "# More of the story.\n\nlabel new_scene_3\n");
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty());
    }

    if (want("character")) {
        std::printf("character. + New character adds  character Someone  (then Someone 2) above label start, opens its form with the name ready to type\n");
        const std::string g = S + "/h/character";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript));
        fs::remove_all(S + "/h/character_data");
        auto dr = start(out, g, S + "/h/character_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        CHECK(d.view == View::Story && d.editor.characters().empty());
        dr->frames(2);
        click(*dr, 126.0f, 704.0f); // + New character (under the characters list)
        dr->frames(3);
        std::printf("   focus '%s', typing '%s'\n", d.focus.c_str(), d.buffer.c_str());
        const auto& made = d.editor.characters();
        CHECK(made.size() == 1 && made[0].name == "Someone" && d.character == 0);
        CHECK(made.size() == 1 && d.focus == std::format("character:{}:Name", made[0].line) && d.buffer == "Someone");
        dr->shot("character_new.png");
        std::string main = read(g + "/scripts/main.vnscript");
        std::printf("%s", main.c_str());
        CHECK(main.contains("config developer yes\n\ncharacter Someone\n\nlabel start\n")); // (with the other definitions, above the story)
        const std::size_t second = d.editor.addCharacter();
        CHECK(d.editor.characters()[second].name == "Someone 2");
        main = read(g + "/scripts/main.vnscript");
        CHECK(main.contains("character Someone\ncharacter \"Someone 2\"\n\nlabel start\n"));
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty());
    }

    if (want("lines")) {
        std::printf("lines. every add-a-line button writes a bare line of its kind (nothing made up), and the first box is ready to type into or pick from, its list open\n");
        const std::string g = S + "/h/lines";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript));
        fs::remove_all(S + "/h/lines_data");
        auto dr = start(out, g, S + "/h/lines_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        CHECK(d.view == View::Story);
        for (const vn::Adding& add : vn::kAdds) {
            const auto lines = vn::newLines(add.kind, d.editor);
            const auto words = vn::rawWords(lines.front());
            const std::string_view field = vn::mainField(add.kind);
            std::printf("   %-10s %-14s -> %s\n", std::string(add.label).c_str(), lines.front().c_str(), field.empty() ? "(nothing to type)" : std::string(field).c_str());
            CHECK(words.size() <= 2 && (words.size() < 2 || words[0] == "stop" || words[0] == "weather" || words[0] == "phone" || words[0] == "quick" || words[0] == "new")); // (a verb, at most its first variant)
            const auto& scene = d.editor.scenes().front();
            const std::size_t at = d.editor.add(scene, nullptr, false, lines);
            const auto rows = d.editor.rows(d.editor.scenes().front());
            const auto row = std::ranges::find(rows, at, &vn::StoryEditor::Row::line);
            CHECK(row != rows.end() && row->kind == add.kind);
            if (!field.empty()) {
                const auto fields = d.editor.fields(*row);
                CHECK(std::ranges::contains(fields, std::string(field), &vn::StoryEditor::Field::name));
            }
        }
        dr->frames(2);
        click(*dr, 409.0f, 588.0f); // Background (under the lines: the first row's third)
        dr->frames(3);
        std::printf("   focus '%s', list '%s'\n", d.focus.c_str(), d.popup.id.c_str());
        CHECK(d.focus.starts_with("row:") && d.focus.ends_with(":Image") && d.popup.id == d.focus && d.openList.empty());
        dr->shot("lines_background.png");
    }

    if (want("translation")) {
        std::printf("translation. + New language makes translations/new_language.vnscript: a one-line note, its language line and the texts; it takes the language's name once that's picked; texts the game doesn't show any more go\n");
        const std::string g = S + "/h/translation";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript));
        fs::remove_all(S + "/h/translation_data");
        auto dr = start(out, g, S + "/h/translation_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        d.view = View::Translate;
        dr->frames(2);
        click(*dr, 70.0f, 597.0f); // + New language (under the languages list)
        dr->frames(3);
        std::printf("   open '%s': %s\n", d.translation.id.c_str(), d.status.c_str());
        const std::string file = read(g + "/translations/new_language.vnscript");
        std::printf("%s", file.c_str());
        CHECK(d.translation.id == "new_language" && file.starts_with("# Every text the game shows, each with its translation under it.\n\nlanguage \"New language\"\n\n"));
        CHECK(file.contains("\"This is the first line of the story.\"\n    \"This is the first line of the story.\"\n") && std::ranges::count(file, '#') == 3); // (the note, the file's heading, the line's)
        CHECK(!file.contains("VNext") && !file.contains("Key names") && !file.contains("(")); // (not the player's own name, nor key names no screen shows)
        dr->shot("translation_new.png");
        // Its language picked: the file is called after it.
        d.translation.lines[2] = "language \"Japanese\" code=ja";
        std::ofstream(g + "/translations/new_language.vnscript") << vn::joinedLines(d.translation.lines);
        dr->frames(3);
        std::printf("   now '%s'\n", d.translation.id.c_str());
        CHECK(d.translation.id == "japanese" && fs::exists(g + "/translations/japanese.vnscript") && !fs::exists(g + "/translations/new_language.vnscript"));
        // --translate spanish from a terminal: the same file, called Spanish; run again, new texts go at the end under one note.
        const vn::TranslationFile made = vn::translationFile(*d.script, "spanish", std::nullopt);
        CHECK(made.text.starts_with("# Every text the game shows, each with its translation under it.\n\nlanguage \"Spanish\"\n"));
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript) + "    And another.\n");
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        const vn::TranslationFile more = vn::translationFile(*d.script, "spanish", made.text);
        std::printf("   added %zu\n", more.added);
        CHECK(more.added == 1 && more.text.contains("\n# Texts added since the file was made.\n") && !more.text.contains("===="));
        CHECK(vn::translationFile(*d.script, "spanish", more.text).added == 0); // (run again: nothing new, nothing added twice)
        // A Controls screen: then the key names are listed, once.
        write(g + "/scripts/screens.vnscript", "screen controls\n    control advance x=460 y=344 w=360 h=32\n");
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript) + "    And another.\ninclude screens\n");
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        const vn::TranslationFile keys = vn::translationFile(*d.script, "spanish", more.text);
        CHECK(keys.text.contains("# ---- Key names ----\n") && keys.text.contains("\"Left click\"\n") && vn::translationFile(*d.script, "spanish", keys.text).added == 0);
        // Texts the game doesn't show any more (the old player's own messages, say) go from the file with their
        // notes and translations, and a heading left with nothing under it; the rest stays as it was.
        const std::string stale = keys.text + "\n# ---- Messages (config text.*) ----\n\n# text.saved\n\"Saved\"\n    \"Guardado\"\n\n# text.empty_slot\n\"Empty\"\n    \"Empty\"\n";
        const vn::TranslationFile pruned = vn::translationFile(*d.script, "spanish", stale);
        std::printf("   removed %zu, added %zu\n%s", pruned.removed.size(), pruned.added, pruned.text.c_str());
        CHECK((pruned.removed == std::vector<std::string>{"Empty", "Saved"}) && pruned.added == 0);
        CHECK(!pruned.text.contains("Saved") && !pruned.text.contains("Guardado") && !pruned.text.contains("text.") && !pruned.text.contains("Messages") && !pruned.text.contains("\n\n\n"));
        CHECK(pruned.text.contains("\"Left click\"\n") && pruned.text.contains("\"And another.\"\n") && pruned.text.contains("# Texts added since the file was made.\n") && pruned.text.ends_with("\"\n"));
        // The Translate tab takes them out as soon as it shows the file, and undo brings them back. (The
        // file's language line says Spanish: the tab names the file after it, and undo keeps that name.)
        write(g + "/translations/japanese.vnscript", stale);
        d.translation = {.id = "japanese", .lines = vn::linesOf(stale)};
        d.translateChecked = SIZE_MAX;
        dr->frames(3);
        std::printf("   %s (%s)\n", d.status.c_str(), d.translation.id.c_str());
        auto has = [&](const char* line) { return std::ranges::contains(d.translation.lines, std::string(line)); };
        CHECK(!has("\"Saved\"") && !has("\"Empty\"") && has("\"Left click\"") && d.status == "Took out 2 texts the game doesn't show any more.");
        CHECK(d.translation.id == "spanish" && !fs::exists(g + "/translations/japanese.vnscript") && !read(g + "/translations/spanish.vnscript").contains("Saved") && d.translateUndo.size() == 1);
        SDL_Event undo{};
        undo.type = SDL_EVENT_KEY_DOWN;
        undo.key.key = SDLK_Z;
        undo.key.mod = SDL_KMOD_GUI;
        undo.key.down = true;
        undo.key.windowID = SDL_GetWindowID(e.window_.get());
        e.event(undo);
        dr->frames(3);
        CHECK(has("\"Saved\"") && has("\"Empty\"") && d.translation.id == "spanish" && read(g + "/translations/spanish.vnscript").contains("\"Guardado\"")); // (and they stay until the scripts change)
        CHECK(!fs::exists(g + "/translations/japanese.vnscript"));
        // A translation emptied in its box (an earlier text's, while the last text has one): its line goes,
        // and the rows after it are drawn from the lines as they are now (the sanitizers' build says so).
        {
            const std::string two = "language \"Spanish\" code=es\n\n\"One\"\n    \"Uno\"\n\n\"Two\"\n    \"Dos\"\n";
            write(g + "/translations/spanish.vnscript", two);
            std::vector<std::string> lines = vn::linesOf(two);
            while (!lines.empty() && lines.back().empty()) lines.pop_back(); // (as the tab opens a file)
            d.translation = {.id = "spanish", .lines = lines};
            d.translateChecked = d.editor.version(); // (not pruned: these texts aren't the game's)
            dr->frames(2);
            const auto pairs = vn::textPairs(d.translation.lines);
            CHECK(pairs.size() == 2);
            d.focus = std::format("tr:spanish:{}", pairs.empty() ? 0 : pairs[0].original); // ("Uno" chosen in its box, then Backspace)
            d.buffer = "Uno";
            ++d.focusCount;
            d.anchorFocus = d.focusCount;
            d.anchor = 0;
            d.caret = 3;
            SDL_Event back{};
            back.type = SDL_EVENT_KEY_DOWN;
            back.key.key = SDLK_BACKSPACE;
            back.key.down = true;
            back.key.windowID = SDL_GetWindowID(e.window_.get());
            e.event(back);
            dr->frames(1);
            d.focus.clear();
            dr->frames(1);
            const std::string emptied = read(g + "/translations/spanish.vnscript");
            std::printf("%s", emptied.c_str());
            CHECK(emptied == "language \"Spanish\" code=es\n\n\"One\"\n\n\"Two\"\n    \"Dos\"\n");
        }
    }

    if (want("map")) {
        std::printf("map. the Map tab: scenes in columns by jumps from start (the ones nothing jumps to after them), arrows for options, calls and jumps; a click chooses a scene, a double click opens it, its dot dragged to another draws a jump, and a chosen arrow goes with Delete\n");
        const std::string g = S + "/h/map";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"M\"\nconfig developer yes\n\nlabel start\n    Hi.\n    choice\n        Go left -> left\n        Go right\n            call right\n    jump left\n\n"
                                            "label left\n    Left.\n\nlabel right\n    Right.\n    return\n\nlabel lonely\n    Nobody comes here.\n");
        fs::remove_all(S + "/h/map_data");
        auto dr = start(out, g, S + "/h/map_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty());
        const auto& links = d.editor.links();
        std::printf("   %zu scenes, %zu links\n", d.editor.scenes().size(), links.size());
        CHECK(d.editor.scenes().size() == 4 && links.size() == 3);
        d.view = View::Map;
        dr->frames(2);
        dr->shot("map.png");
        // (where the boxes are: the tab's area, its grid's start, a column and a row apart; the window's scale k)
        const float k = e.canvas_.h / 720.0f, x0 = 16 * k + 20 * k, y0 = 60 * k + 40 * k, across = (170 + 90) * k, down = (36 + 16) * k;
        auto box = [&](int column, int row) { return SDL_FPoint{x0 + static_cast<float>(column) * across + 85 * k, y0 + static_cast<float>(row) * down + 18 * k}; };
        const SDL_FPoint left = box(1, 0), right = box(1, 1), lonely = box(2, 0);
        click(*dr, left.x, left.y);
        dr->frames(2);
        std::printf("   chosen: %zu\n", d.scene);
        CHECK(d.scene == 1);
        // A double click on the lonely scene (nothing jumps to it: in the column after the rest) opens it.
        mouse(*dr, SDL_EVENT_MOUSE_MOTION, lonely.x, lonely.y);
        {
            SDL_Event press{};
            press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            press.button.windowID = SDL_GetWindowID(e.window_.get());
            press.button.button = SDL_BUTTON_LEFT;
            press.button.down = true;
            press.button.clicks = 2;
            SDL_RenderCoordinatesToWindow(e.renderer_.get(), lonely.x, lonely.y, &press.button.x, &press.button.y);
            e.event(press);
        }
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, lonely.x, lonely.y);
        dr->frames(2);
        std::printf("   opened: %zu in view %d\n", d.scene, static_cast<int>(d.view));
        CHECK(d.scene == 3 && d.view == View::Story);
        d.view = View::Map;
        dr->frames(2);
        // Its dot dragged to the right scene: it jumps there at its end.
        const float dotX = lonely.x + 85 * k - 7 * k;
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, dotX, lonely.y);
        dr->frames(1);
        for (int i = 1; i <= 4; ++i) {
            motion(*dr, dotX + (right.x - dotX) * static_cast<float>(i) / 4, lonely.y + (right.y - lonely.y) * static_cast<float>(i) / 4);
            dr->frames(1);
        }
        dr->shot("map_jump.png");
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, right.x, right.y);
        dr->frames(2);
        std::string main = read(g + "/scripts/main.vnscript");
        std::printf("   %s\n", d.status.c_str());
        CHECK(main.contains("label lonely\n    Nobody comes here.\n    jump right\n") && d.status == "'lonely' jumps to 'right' at its end now.");
        // The call from start to right: a click on its arrow (half way along) chooses it, and Delete takes it away.
        const float midX = x0 + (170 + 45) * k, midY = (y0 + 18 * k + y0 + down + 18 * k) / 2; // (between start's right edge and right's left edge)
        click(*dr, midX, midY);
        dr->frames(2);
        CHECK(d.mapArrow.has_value());
        key(*dr, SDLK_DELETE);
        dr->frames(2);
        main = read(g + "/scripts/main.vnscript");
        std::printf("   %s\n%s", d.status.c_str(), main.c_str());
        CHECK(!main.contains("call right") && d.status == "'start' no longer calls 'right'." && !d.mapArrow);
        // A game with no scenes at all: the tab is empty (but its buttons), not a crash.
        write(g + "/scripts/main.vnscript", "config title \"M\"\nconfig developer yes\n");
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        std::printf("   %zu scenes\n", d.editor.scenes().size());
        CHECK(d.editor.scenes().empty() && d.view == View::Map);
        dr->shot("map_empty.png");
    }

    if (want("library")) {
        std::printf("library. the Library's + New adds a bare definition (an animation with no made-up steps, a style with no settings, an atlas with no made-up frame names, an ending whose title is its name until typed) with its name ready to type\n");
        const std::string g = S + "/h/library";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript));
        fs::remove_all(S + "/h/library_data");
        auto dr = start(out, g, S + "/h/library_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        d.view = View::Library;
        d.library = "animation";
        dr->frames(2);
        click(*dr, 405.0f, 705.0f); // + New (under the list)
        dr->frames(3);
        auto defs = d.editor.definitions("animation");
        std::printf("   %zu animations, focus '%s', typing '%s'\n", defs.size(), d.focus.c_str(), d.buffer.c_str());
        CHECK(defs.size() == 1 && defs[0].name == "new_animation" && d.libraryItem == 0);
        CHECK(defs.size() == 1 && d.focus == std::format("def:animation:{}:Name", defs[0].line) && d.buffer == "new_animation");
        std::string main = read(g + "/scripts/main.vnscript");
        std::printf("%s", main.c_str());
        CHECK(main.contains("config developer yes\n\nanimation new_animation\n\nlabel start\n")); // (nothing under it: its steps are the dev's)
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.size() == 1 && d.problems[0].message.starts_with("An animation needs its steps")); // (said, like a line with its words missing)
        dr->shot("library_animation.png");
        d.library = "style";
        d.libraryItem.reset();
        dr->frames(2);
        click(*dr, 405.0f, 705.0f);
        dr->frames(3);
        const auto styles = d.editor.definitions("style");
        CHECK(styles.size() == 1 && styles[0].name == "new_style" && d.buffer == "new_style");
        main = read(g + "/scripts/main.vnscript");
        CHECK(main.contains("\nstyle new_style\n") && !main.contains("size=24"));
        click(*dr, 405.0f, 705.0f); // (one more: new_style_2)
        dr->frames(3);
        CHECK(d.editor.definitions("style").size() == 2 && d.buffer == "new_style_2");
        dr->shot("library_style.png");
        d.library = "atlas"; // (an atlas: its name alone; the picture is called the same, and the frames' names are typed into the form's boxes)
        d.libraryItem.reset();
        dr->frames(2);
        click(*dr, 405.0f, 705.0f);
        dr->frames(3);
        const auto atlases = d.editor.definitions("atlas");
        main = read(g + "/scripts/main.vnscript");
        CHECK(atlases.size() == 1 && atlases[0].name == "new_atlas" && d.buffer == "new_atlas" && main.contains("\natlas new_atlas\n") && !main.contains("expressions"));
        const auto fields = d.editor.fields("atlas", atlases[0]);
        CHECK(std::ranges::contains(fields, std::string("Expressions"), &vn::StoryEditor::Field::name) && std::ranges::contains(fields, std::string("Eyes"), &vn::StoryEditor::Field::name));
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.size() == 2 && d.problems[1].message == "There's no image called 'new_atlas'."); // (its picture: said until it's there, like the animation's steps)
        dr->shot("library_atlas.png");
        d.library = "ending"; // (an ending: its name, and "" for the title (without it the line would be the story reaching one); empty, the title is the name)
        d.libraryItem.reset();
        dr->frames(2);
        click(*dr, 405.0f, 705.0f);
        dr->frames(3);
        const auto endings = d.editor.definitions("ending");
        main = read(g + "/scripts/main.vnscript");
        CHECK(endings.size() == 1 && endings[0].name == "new_ending" && d.buffer == "new_ending" && main.contains("\nending new_ending \"\"\n"));
        auto endingFields = d.editor.fields("ending", endings[0]);
        vn::showConfigValues(endingFields, e.config(), e.palette());
        const auto title = std::ranges::find(endingFields, std::string("Title"), &vn::StoryEditor::Field::name);
        const auto hint = std::ranges::find(endingFields, std::string("Hint"), &vn::StoryEditor::Field::name);
        CHECK(title != endingFields.end() && title->value.empty() && title->hint == "new_ending" && hint != endingFields.end() && hint->hint == "None");
        const auto& list = d.script->lists[static_cast<std::size_t>(vn::RecordKind::Ending)];
        CHECK(list.size() == 1 && list[0].name == "new_ending" && list[0].title == "new_ending"); // (the game shows the name until a title is typed)
        CHECK(d.problems.size() == 2);
        dr->shot("library_ending.png");
        d.library = "achievement"; // (the same as an ending, with a description and whether it's hidden until earned)
        d.libraryItem.reset();
        dr->frames(2);
        click(*dr, 405.0f, 705.0f);
        dr->frames(3);
        const auto achievements = d.editor.definitions("achievement");
        main = read(g + "/scripts/main.vnscript");
        CHECK(achievements.size() == 1 && achievements[0].name == "new_achievement" && d.buffer == "new_achievement" && main.contains("\nachievement new_achievement \"\"\n"));
        auto aFields = d.editor.fields("achievement", achievements[0]);
        vn::showConfigValues(aFields, e.config(), e.palette());
        for (const auto& f : aFields) std::printf("   %s = '%s' (empty: %s)\n", f.name.c_str(), f.value.c_str(), std::string(f.hint).c_str());
        const auto aTitle = std::ranges::find(aFields, std::string("Title"), &vn::StoryEditor::Field::name);
        const auto aText = std::ranges::find(aFields, std::string("Description"), &vn::StoryEditor::Field::name);
        const auto aHidden = std::ranges::find(aFields, std::string("Hidden until earned"), &vn::StoryEditor::Field::name);
        CHECK(aTitle != aFields.end() && aTitle->value.empty() && aTitle->hint == "new_achievement");
        CHECK(aText != aFields.end() && aText->value.empty() && aText->hint == "None" && aHidden != aFields.end() && aHidden->value == "no");
        const auto& earned = d.script->lists[static_cast<std::size_t>(vn::RecordKind::Achievement)];
        CHECK(earned.size() == 1 && earned[0].title == "new_achievement" && earned[0].text.empty() && !earned[0].hidden);
        CHECK(d.problems.size() == 2);
        dr->shot("library_achievement.png");
        auto add = [&](const char* kind, float x = 405.0f) { // (+ New in a Library section)
            d.library = kind;
            d.libraryItem.reset();
            dr->frames(2);
            click(*dr, x, 705.0f);
            dr->frames(3);
            main = read(g + "/scripts/main.vnscript");
        };
        add("tip"); // (like an ending: its title is its name until typed)
        CHECK(d.editor.definitions("tip").size() == 1 && d.buffer == "new_tip" && main.contains("\ntip new_tip \"\"\n") && d.script->lists[static_cast<std::size_t>(vn::RecordKind::Tip)][0].title == "new_tip");
        add("color"); // (white: a colour line needs a colour)
        CHECK(d.editor.definitions("color").size() == 1 && d.buffer == "new_color" && main.contains("\ncolor new_color #ffffff\n") && d.problems.size() == 2);
        dr->shot("library_colour.png"); // (a colour: as a square before its name in the list, as in its box)
        add("control"); // (bare: what it does is said to be missing, until picked)
        CHECK(d.editor.definitions("control").size() == 1 && d.buffer == "new_input" && main.contains("\ncontrol new_input\n"));
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.size() == 3 && std::ranges::any_of(d.problems, [](const vn::ScriptError& p) { return p.message == "A control needs action=."; }));
        auto cFields = d.editor.fields("control", d.editor.definitions("control")[0]);
        for (const auto& f : cFields) std::printf("   %s = '%s' (empty: %s)\n", f.name.c_str(), f.value.c_str(), std::string(f.hint).c_str());
        const auto cKey = std::ranges::find(cFields, std::string("Key"), &vn::StoryEditor::Field::name);
        CHECK(cKey != cFields.end() && cKey->hint == "None");
        CHECK(std::ranges::any_of(cFields, [](const vn::StoryEditor::Field& f) { return f.name.starts_with("Does") && f.hint == "None"; }));
        // Actions by what they're about, then by kind: "set x to 1; open options" shows as variable / set / x to 1, then screen / open / options.
        // Another kind picked starts without the words; another group, with its first kind.
        d.editor.setDefinition(d.editor.definitions("control")[0], {"control new_input action=\"set x to 1; open options\""}, "k");
        auto doesFields = d.editor.fields("control", d.editor.definitions("control")[0]);
        auto doesValue = [&](std::string_view name) {
            const auto f = std::ranges::find(doesFields, name, &vn::StoryEditor::Field::name);
            return f != doesFields.end() ? f->value : std::string("(none)");
        };
        std::printf("   actions: %s / %s / %s / %s, then %s / %s / %s, then '%s'\n", doesValue("Does").c_str(), doesValue("Does how").c_str(), doesValue("Does variable").c_str(),
                    doesValue("Does value").c_str(), doesValue("Does 2").c_str(), doesValue("Does 2 how").c_str(), doesValue("Does 2 what").c_str(), doesValue("Does 3").c_str());
        CHECK(doesValue("Does") == "variable" && doesValue("Does how") == "set" && doesValue("Does variable") == "x" && doesValue("Does value") == "1" && doesValue("Does what") == "(none)");
        CHECK(doesValue("Does 2") == "screen" && doesValue("Does 2 how") == "open" && doesValue("Does 2 what") == "options" && doesValue("Does 3").empty());
        std::ranges::find(doesFields, std::string("Does value"), &vn::StoryEditor::Field::name)->value = "english"; // (a word that isn't a variable: text, in quotes)
        const auto worded = d.editor.linesOf("control", doesFields);
        std::ranges::find(doesFields, std::string("Does how"), &vn::StoryEditor::Field::name)->value = "add"; // (another kind: the variable and the amount kept)
        const auto added = d.editor.linesOf("control", doesFields);
        std::ranges::find(doesFields, std::string("Does how"), &vn::StoryEditor::Field::name)->value = "toggle";
        const auto toggled = d.editor.linesOf("control", doesFields);
        std::ranges::find(doesFields, std::string("Does"), &vn::StoryEditor::Field::name)->value = "sound"; // (another group: its first kind, from the start)
        const auto sounded = d.editor.linesOf("control", doesFields);
        std::printf("   english typed: %s; add picked: %s; toggle picked: %s; sound picked: %s\n", worded.front().c_str(), added.front().c_str(), toggled.front().c_str(), sounded.front().c_str());
        CHECK(worded.front().contains("action=\"set x to 'english'; open options\"") && added.front().contains("action=\"add 'english' to x; open options\""));
        CHECK(toggled.front().contains("action=\"toggle x; open options\"") && sounded.front().contains("action=\"music; open options\""));
        // A setting: its group, the setting's name (the box is called Setting), then what it goes to; the language,
        // quit and a screenshot are entries of the first box on their own. Another setting picked: what it goes to starts again.
        d.editor.setDefinition(d.editor.definitions("control")[0], {"control new_input action=\"setting vsync yes; setting language japanese; quit; notify Hi\""}, "k");
        doesFields = d.editor.fields("control", d.editor.definitions("control")[0]);
        dr->frames(2);
        dr->shot("library_input_setting.png");
        std::printf("   actions: %s / %s / %s, then %s / %s / %s, then %s / %s\n", doesValue("Does").c_str(), doesValue("Does how").c_str(), doesValue("Does what").c_str(),
                    doesValue("Does 2").c_str(), doesValue("Does 2 how").c_str(), doesValue("Does 2 what").c_str(), doesValue("Does 3").c_str(), doesValue("Does 3 what").c_str());
        CHECK(doesValue("Does") == "settings" && doesValue("Does how") == "vsync" && doesValue("Does what") == "yes" && std::ranges::find(doesFields, std::string("Does how"), &vn::StoryEditor::Field::name)->label == "Setting");
        CHECK(doesValue("Does 2") == "language" && doesValue("Does 2 how") == "(none)" && doesValue("Does 2 what") == "japanese" && doesValue("Does 3") == "quit" && doesValue("Does 3 what") == "(none)");
        CHECK(doesValue("Does 4") == "message" && doesValue("Does 4 what") == "Hi"); // (a message: an entry of its own too)
        std::ranges::find(doesFields, std::string("Does how"), &vn::StoryEditor::Field::name)->value = "text_speed";
        std::ranges::find(doesFields, std::string("Does 3"), &vn::StoryEditor::Field::name)->value = "screenshot";
        const auto reset = d.editor.linesOf("control", doesFields);
        std::printf("   text_speed and screenshot picked: %s\n", reset.front().c_str());
        CHECK(reset.front().contains("action=\"setting text_speed; setting language japanese; screenshot; notify Hi\""));
        // The window's size is a width and a height, two boxes; a percent setting is written with its % as the Settings tab has it;
        // the Setting list says what each setting is as the Settings tab does, and what one goes to lists its options by name.
        d.editor.setDefinition(d.editor.definitions("control")[0], {"control new_input action=\"setting window_size 1280 720; setting textbox_opacity 85; setting scaling integer\""}, "k");
        doesFields = d.editor.fields("control", d.editor.definitions("control")[0]);
        std::printf("   actions: %s / %s / %s x %s, then %s / %s, then %s / %s\n", doesValue("Does").c_str(), doesValue("Does how").c_str(), doesValue("Does what").c_str(), doesValue("Does height").c_str(),
                    doesValue("Does 2 how").c_str(), doesValue("Does 2 what").c_str(), doesValue("Does 3 how").c_str(), doesValue("Does 3 what").c_str());
        CHECK(doesValue("Does") == "settings" && doesValue("Does how") == "window_size" && doesValue("Does what") == "1280" && doesValue("Does height") == "720");
        CHECK(doesValue("Does 2 how") == "textbox_opacity" && doesValue("Does 2 what") == "85" && doesValue("Does 3 how") == "scaling" && doesValue("Does 3 what") == "integer");
        auto settingBox = [&](std::string_view name) { // (as the form draws it: its kind's list)
            vn::StoryEditor::Field f = *std::ranges::find(doesFields, name, &vn::StoryEditor::Field::name);
            vn::applyBox(f);
            return std::string(f.suggest);
        };
        std::printf("   settings: %.120s...\n   scaling: %s\n   opacity: %s\n", settingBox("Does how").c_str(), settingBox("Does 3 what").c_str(), settingBox("Does 2 what").c_str());
        CHECK(settingBox("Does how").contains("fullscreen:Whether the game starts fullscreen|window_size:The window's width and height, in pixels|scaling:How the game fills the window"));
        CHECK(settingBox("Does how").contains("|reset settings:") && settingBox("Does 3 what") == "fit|integer:whole pixels|stretch|fill" && settingBox("Does 2 what").starts_with("0%:unseen|"));
        dr->frames(2);
        dr->shot("library_input_window.png"); // (the width and height boxes)
        d.popup.id = std::format("def:control:{}:Does how", d.editor.definitions("control")[0].line); // (the Setting list open: each with what it is, in grey)
        d.popup.shown = true;
        dr->frames(2);
        dr->shot("library_setting_list.png");
        d.popup.id.clear();
        d.popup.shown = false;
        dr->frames(1);
        std::ranges::find(doesFields, std::string("Does height"), &vn::StoryEditor::Field::name)->value = "600";
        const auto sized = d.editor.linesOf("control", doesFields);
        std::printf("   height typed: %s\n", sized.front().c_str());
        CHECK(sized.front().contains("action=\"setting window_size 1280 600; setting textbox_opacity 85%; setting scaling integer\""));
        d.editor.setDefinition(d.editor.definitions("control")[0], {"control new_input action=\"set x to 1; open options\""}, "k");
        dr->frames(2);
        dr->shot("library_input.png");
        // Long words: the boxes as wide as they need, on as many rows as that takes, nothing cut off.
        d.editor.setDefinition(d.editor.definitions("control")[0], {"control new_input action=\"set language_choice to 'british_english'; open settings_and_more\""}, "k");
        dr->frames(3);
        dr->shot("library_input_long.png");
        add("variables"); // (a variable: its name alone; it starts at 0)
        std::printf("   variables: focus '%s', typing '%s'\n", d.focus.c_str(), d.buffer.c_str());
        CHECK(d.editor.definitions("variable").size() == 1 && d.editor.definitions("variable")[0].name == "new_variable" && main.contains("\nvariable new_variable\n") && d.buffer == "new_variable");
        CHECK(d.problems.size() == 3);
        dr->shot("library_variable.png"); // (its form: the value it starts at, what it's for, and whether it's kept between games)
        // Kept between games: the persistent. name, and every line that uses it renamed with it; and back.
        write(g + "/scripts/story.vnscript", "label more\n    set new_variable to 3\n    if new_variable is 3\n        Hi.\n");
        write(g + "/scripts/main.vnscript", read(g + "/scripts/main.vnscript") + "include story\n");
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        const auto kept = d.editor.renameVariable("new_variable", "persistent.new_variable");
        std::printf("   kept between games: %s lines follow; %s\n", kept ? std::to_string(*kept).c_str() : "not renamed,", read(g + "/scripts/story.vnscript").c_str());
        CHECK(kept && *kept == 2 && read(g + "/scripts/story.vnscript").contains("set persistent.new_variable to 3") &&
              std::ranges::contains(d.editor.variables(), std::string("persistent.new_variable")));
        const auto back = d.editor.renameVariable("persistent.new_variable", "new_variable");
        CHECK(back && *back == 2 && read(g + "/scripts/story.vnscript").contains("if new_variable is 3") && !read(g + "/scripts/story.vnscript").contains("persistent."));
        // A picture and a font file, for + Sub-image and + Font (where they are: seen on the screenshots).
        fs::create_directories(g + "/images");
        fs::create_directories(g + "/fonts");
        if (SDL_Surface* pic = SDL_CreateSurface(64, 48, SDL_PIXELFORMAT_RGBA32)) {
            SDL_FillSurfaceRect(pic, nullptr, 0xff4080c0u);
            IMG_SavePNG(pic, (g + "/images/sheet.png").c_str());
            SDL_DestroySurface(pic);
        }
        fs::copy_file("/System/Library/Fonts/Supplemental/Arial.ttf", g + "/fonts/arial.ttf", fs::copy_options::overwrite_existing);
        e.developer_.useTool(vn::kReload);
        dr->frames(3);
        d.library = "pictures";
        d.libraryItem = 0;
        d.librarySub.reset();
        dr->frames(3);
        click(*dr, 1180.0f, 121.0f); // + Sub-image, at the right of the line that says what the picture is (all of it, to cut down; called new_picture)
        dr->frames(3);
        main = read(g + "/scripts/main.vnscript");
        std::printf("   sub-image: focus '%s', typing '%s'\n", d.focus.c_str(), d.buffer.c_str());
        const auto pictures = d.editor.definitions("picture");
        CHECK(pictures.size() == 1 && pictures[0].name == "new_picture" && main.contains("\npicture new_picture image=sheet x=0 y=0 w=64 h=48\n"));
        CHECK(pictures.size() == 1 && d.librarySub == 0 && d.focus == std::format("def:picture:{}:Name", pictures[0].line) && d.buffer == "new_picture");
        dr->shot("library_picture.png");
        d.library = "fontfiles";
        d.libraryItem = 0;
        d.librarySub.reset();
        dr->frames(3);
        click(*dr, 685.0f, 115.0f); // + Font (named after its file)
        dr->frames(3);
        main = read(g + "/scripts/main.vnscript");
        std::printf("   font: focus '%s', typing '%s'\n", d.focus.c_str(), d.buffer.c_str());
        const auto fonts = d.editor.definitions("font");
        CHECK(fonts.size() == 1 && fonts[0].name == "arial" && main.contains("\nfont arial \"fonts/arial.ttf\"\n"));
        CHECK(fonts.size() == 1 && d.focus == std::format("def:font:{}:Name", fonts[0].line) && d.buffer == "arial");
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.size() == 3); // (no new ones: a whole picture and a font file are complete as they are)
        dr->shot("library_fontfile.png");
    }

    if (want("letters")) {
        std::printf("letters. names can have letters of any alphabet (ősz, 日本); a definition's name emptied in its form keeps the one it has\n");
        CHECK(vn::validName("ősz") && vn::validName("Őrs_1") && vn::validName("日本") && !vn::validName("1ab") && !vn::validName("a b") && vn::isWordChar("ő"[0]));
        // Capitals and small letters are the same name in every alphabet the player knows: Őrs is ŐRS.
        CHECK(vn::lower("ŐRS Vezér ÁÉÍÓÖÚÜ ŰŐ ΑΒΓ ДЖ Ÿ İ 日本") == "őrs vezér áéíóöúü űő αβγ дж ÿ i 日本" && vn::lower("Plain ASCII 123") == "plain ascii 123");
        CHECK(vn::translationFileName("Español") == "español" && vn::translationFileName("Magyar (Ő)") == "magyar_ő");
        CHECK(vn::upper("őrs vezér ß ÿ ς αβ дж") == "ŐRS VEZÉR ß Ÿ Σ ΑΒ ДЖ" && vn::upperFirst("őrség") == "Őrség" && vn::upperFirst("yes") == "Yes" && vn::upperFirst("").empty());
        CHECK(vn::startsWithCapital("Őrs") && vn::startsWithCapital("Abc") && !vn::startsWithCapital("őrs") && !vn::startsWithCapital("abc") && !vn::startsWithCapital("日本") && !vn::startsWithCapital(""));
        const std::string g = S + "/h/letters";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript",
              "config developer yes\n\nvariable kedv 1 \"jókedv\"\nvariable őrült 2\ncolor égkék #80c0ff\nanimation lebegés loop\n    opacity=0.5 over 1 second\n    opacity=1 over 1 second\n\n"
              "character Őrs color=ÉGKÉK\n\nlabel start\n    ŐRS: Szia!\n    set ŐRÜLT to őrült + 1\n    if őrült is 3 and kedv\n        jump ŐSZ\n    end\n\nlabel ősz\n    Ősz van.\n    end\n");
        fs::remove_all(S + "/h/letters_data");
        auto dr = start(out, g, S + "/h/letters_data");
        auto& d = dr->engine.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty());
        CHECK(d.editor.scenes().size() == 2 && d.editor.scenes()[1].name == "ősz" && d.editor.definitions("animation").size() == 1 && d.editor.definitions("animation")[0].name == "lebegés");
        CHECK(d.editor.characters().size() == 1 && d.editor.characters()[0].name == "Őrs");
        CHECK(vn::translationFile(*d.script, "őrség", std::nullopt).text.contains("language \"Őrség\"\n")); // (a language's name from its file's)
        { // ("Ősz van.": narration, as "Autumn." is: a capital of any alphabet)
            const auto rows = d.editor.rows(d.editor.scenes()[1]);
            CHECK(rows.size() == 2 && rows[0].kind == vn::StoryEditor::Kind::Line);
        }
        const auto before = read(g + "/scripts/main.vnscript");
        d.editor.setDefinition(d.editor.definitions("animation")[0], {"animation", "    opacity=0.5 over 1 second"}, "x"); // (the Name box emptied)
        CHECK(read(g + "/scripts/main.vnscript") == before);
        d.editor.setDefinition(d.editor.definitions("color")[0], {"color"}, "y");
        CHECK(read(g + "/scripts/main.vnscript") == before);
        d.editor.setDefinition(d.editor.definitions("color")[0], {"color égkék #ff0000"}, "z"); // (a change: written)
        CHECK(read(g + "/scripts/main.vnscript").contains("color égkék #ff0000\n"));
    }

    if (want("emptied")) {
        std::printf("emptied. a name box emptied keeps the name it has, without a word about it: a scene's, a screen's, a colour's, a variable's, a translation's language\n");
        const std::string g = S + "/h/emptied";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript) + "\nvariable kedv 1\ncolor égkék #80c0ff\n\nscreen panel\n    image #203040 x=0 y=0 w=1 h=1\n");
        fs::remove_all(S + "/h/emptied_data");
        auto dr = start(out, g, S + "/h/emptied_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        auto never = [&](std::string_view complaint) { return !d.failed && !d.status.starts_with(complaint); };
        auto empty = [&](const std::string& id, const std::string& now, std::string_view complaint) { // (the box: its name taken out letter by letter)
            d.focus = id;
            d.buffer = now;
            d.caret = d.anchor = now.size();
            ++d.focusCount;
            d.anchorFocus = d.focusCount;
            for (std::size_t i = 0; i < now.size(); ++i) {
                key(*dr, SDLK_BACKSPACE);
                dr->frames(1);
                CHECK(never(complaint));
            }
            CHECK(d.buffer.empty());
        };
        auto type = [&](const std::string& text) { // (a letter at a time, however many bytes each)
            for (std::size_t i = 0; i < text.size();) {
                std::size_t n = 1;
                while (i + n < text.size() && (static_cast<unsigned char>(text[i + n]) & 0xC0) == 0x80) ++n;
                d.typed = text.substr(i, n);
                dr->frames(1);
                i += n;
            }
            d.focus.clear();
            dr->frames(1);
        };
        // A scene: + New scene, its name emptied, then ősz typed: never a complaint, never a nameless label.
        d.view = View::Story;
        dr->frames(2);
        click(*dr, 70.0f, 409.0f);
        dr->frames(3);
        CHECK(d.focus == "scenes:rename:1" && d.buffer == "new_scene");
        empty("scenes:rename:1", "new_scene", "A scene's name");
        CHECK(d.editor.scenes().size() == 2 && !d.editor.scenes()[1].name.empty());
        type("ősz");
        std::string main = read(g + "/scripts/main.vnscript");
        CHECK(d.editor.scenes()[1].name == "ősz" && main.contains("\nlabel ősz\n") && !main.contains("\nlabel\n") && never("A scene's name"));
        // A screen, in the Screens list.
        d.view = View::Screens;
        d.screenName = "panel";
        dr->frames(2);
        empty("screens:rename:0", "panel", "A screen's name");
        CHECK(d.editor.screens().size() == 1 && !d.editor.screens()[0].name.empty());
        type("tábla");
        main = read(g + "/scripts/main.vnscript");
        CHECK(d.editor.screens().size() == 1 && d.editor.screens()[0].name == "tábla" && main.contains("\nscreen tábla\n") && never("A screen's name"));
        // A colour, in the Library's list.
        d.view = View::Library;
        d.library = "color";
        d.libraryItem = 0;
        dr->frames(2);
        empty("library:rename:0", "égkék", "A name is");
        CHECK(d.editor.definitions("color").size() == 1 && !d.editor.definitions("color")[0].name.empty());
        type("ég");
        main = read(g + "/scripts/main.vnscript");
        CHECK(d.editor.definitions("color")[0].name == "ég" && main.contains("\ncolor ég #80c0ff\n") && never("A name is"));
        // A variable: its name is taken on Enter; emptied, Enter changes nothing.
        d.library = "variables";
        d.libraryItem = 0;
        dr->frames(2);
        empty("variables:rename:0", "kedv", "A variable's name");
        key(*dr, SDLK_RETURN);
        dr->frames(2);
        d.focus.clear();
        dr->frames(1);
        main = read(g + "/scripts/main.vnscript");
        CHECK(main.contains("\nvariable kedv 1\n") && never("A variable's name"));
        // A translation's language: its box emptied keeps the language line; a name typed renames the file after it.
        d.view = View::Translate;
        dr->frames(2);
        click(*dr, 70.0f, 597.0f);
        dr->frames(3);
        CHECK(d.translation.id == "new_language");
        empty("lang:language", "New language", "x");
        CHECK(std::ranges::any_of(d.translation.lines, [](const std::string& l) { return l.starts_with("language \"") && l != "language \"\""; }));
        type("Magyar");
        dr->frames(3);
        std::printf("   translation '%s': %s\n", d.translation.id.c_str(), d.translation.lines.empty() ? "" : d.translation.lines[2].c_str());
        CHECK(d.translation.id == "magyar" && fs::exists(g + "/translations/magyar.vnscript") && read(g + "/translations/magyar.vnscript").contains("language \"Magyar\" code=hu\n"));
    }

    if (want("wheel")) {
        std::printf("wheel. a colour's box isn't typed into: a click opens its wheel, with the colour's code to type above it and the game's colours under it\n");
        const std::string g = S + "/h/wheel";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript) + "    scene white\n\ncolor ég #2040ff\ncolor égkék #80c0ff\n");
        fs::remove_all(S + "/h/wheel_data");
        auto dr = start(out, g, S + "/h/wheel_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        d.view = View::Library;
        d.library = "color";
        const auto defs = d.editor.definitions("color");
        const auto egkek = std::ranges::find(defs, std::string("égkék"), &vn::StoryEditor::Definition::name);
        CHECK(egkek != defs.end());
        d.libraryItem = static_cast<std::size_t>(egkek - defs.begin());
        dr->frames(2);
        const std::string id = std::format("def:color:{}:Colour", egkek->line);
        click(*dr, 1000.0f, 155.0f); // the Colour box (the form's second row)
        dr->frames(3);
        std::printf("   popup '%s' at %.0f,%.0f %.0fx%.0f, k %.2f\n", d.popup.id.c_str(), d.popup.box.x, d.popup.box.y, d.popup.box.w, d.popup.box.h, d.popup.at.h / vn::layout::boxH);
        CHECK(d.popup.id == id && d.focus.empty()); // (open, and the box took no focus: it isn't typed into)
        d.typed = "x";
        dr->frames(2);
        auto line = [&] { // (the colour's line, as written)
            const std::string main = read(g + "/scripts/main.vnscript");
            const auto at = main.find("color égkék ");
            return at == std::string::npos ? std::string{} : main.substr(at, main.find('\n', at) - at);
        };
        CHECK(line() == "color égkék #80c0ff");
        const float k = d.popup.at.h / vn::layout::boxH, pad = vn::layout::popupPad * k, valueH = vn::layout::codeH * k, discD = vn::layout::wheelD * k, R = discD / 2;
        const float cx = d.popup.box.x + pad + R, cy = d.popup.box.y + pad + valueH + pad + vn::layout::itemH * k + pad + R; // (the gradient buttons between)
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, cx + R * 0.95f, cy); // the wheel's right edge: red, full saturation
        dr->frames(2);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, cx + R * 0.95f, cy);
        dr->frames(2);
        std::printf("   wheel: %s\n", line().c_str());
        CHECK(line().starts_with("color égkék #ff") && d.popup.id == id); // (red, and the wheel stays open)
        const SDL_FRect valueBar{cx + R + vn::layout::barGap * k, cy - R, vn::layout::barW * k, discD};
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, valueBar.x + valueBar.w / 2, valueBar.y + 2 * k); // dragged down the value bar: black
        dr->frames(1);
        motion(*dr, valueBar.x + valueBar.w / 2, valueBar.y + valueBar.h - 1.0f);
        dr->frames(2);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, valueBar.x + valueBar.w / 2, valueBar.y + valueBar.h - 1.0f);
        dr->frames(2);
        std::printf("   value bar: %s\n", line().c_str());
        CHECK(line().starts_with("color égkék #0") && line().size() == std::string("color égkék #000000").size() && line() <= "color égkék #030303" && d.popup.id == id); // (black, near enough)
        dr->shot("wheel.png");
        // The code, typed by hand above the wheel.
        d.focus = "popup:color";
        d.buffer.clear();
        d.caret = d.anchor = 0;
        ++d.focusCount;
        d.anchorFocus = d.focusCount;
        for (const char c : std::string("#123456")) {
            d.typed = std::string(1, c);
            dr->frames(1);
        }
        d.focus.clear();
        dr->frames(2);
        std::printf("   typed: %s\n", line().c_str());
        CHECK(line() == "color égkék #123456" && d.popup.id == id);
        // Under the wheel: Transparent first (a colour's box with no usual value), then the game's colours (not the built-in names): ég picked.
        const float itemH = vn::layout::itemH * k, listTop = d.popup.box.y + pad + valueH + pad + itemH + pad + discD + pad; // (the gradient buttons, then the wheel)
        click(*dr, d.popup.box.x + 40 * k, listTop + itemH / 2);
        dr->frames(3);
        std::printf("   transparent picked: %s\n", line().c_str());
        CHECK(line() == "color égkék #00000000" && d.popup.id.empty());
        d.openList = id;
        dr->frames(3);
        click(*dr, d.popup.box.x + 40 * k, listTop + itemH * 1.5f);
        dr->frames(3);
        std::printf("   picked: %s\n", line().c_str());
        CHECK(line() == "color égkék ég" && d.popup.id.empty()); // (picked from the list: it closes)
        // A background's box, in the Story tab: the same wheel (the pictures would be under it).
        d.view = View::Story;
        const auto rows = d.editor.rows(d.editor.scenes().front());
        const auto scene = std::ranges::find(rows, vn::StoryEditor::Kind::Background, &vn::StoryEditor::Row::kind);
        CHECK(scene != rows.end());
        d.row = scene->line;
        d.openList = std::format("row:{}:Image", scene->line);
        dr->frames(3);
        std::printf("   background popup '%s' at %.0f,%.0f %.0fx%.0f\n", d.popup.id.c_str(), d.popup.box.x, d.popup.box.y, d.popup.box.w, d.popup.box.h);
        CHECK(d.popup.id == std::format("row:{}:Image", scene->line) && d.focus.empty());
        const float bx = d.popup.box.x + pad + R, by = d.popup.box.y + pad + valueH + pad + vn::layout::itemH * k + pad + R;
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, bx, by - R * 0.95f); // the wheel's top: hue 270 (purple-blue), full saturation
        dr->frames(2);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, bx, by - R * 0.95f);
        dr->frames(2);
        const std::string main = read(g + "/scripts/main.vnscript");
        const auto at = main.find("    scene ");
        const std::string sceneLine = at == std::string::npos ? std::string{} : main.substr(at + 4, main.find('\n', at) - at - 4);
        std::printf("   background: %s\n", sceneLine.c_str());
        CHECK(sceneLine.starts_with("scene #") && sceneLine.size() == 13 && sceneLine != "scene #ffffff" && d.popup.id == std::format("row:{}:Image", scene->line));
        dr->shot("wheel_background.png");
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty()); // (a colour code is a background the game takes)
        // An image part whose picture or colour depends on a variable: the box for yes and the one for no pick the same way as the one box did.
        d.editor.addDefinition("variable", {"variable night no"});
        d.editor.addScreen({}, {"screen s", "    image \"{if(night, 'black', 'white')}\" x=0 y=0 w=1280 h=720"});
        e.developer_.useTool(vn::kReload);
        dr->frames(2);
        const auto& sc = d.editor.screens().back();
        const auto parts = d.editor.parts(sc);
        CHECK(sc.name == "s" && !parts.empty());
        d.view = View::Screens;
        d.screenName = "s";
        d.part = parts.front().line;
        const std::string pid = std::format("part:{}:{}", sc.line, parts.front().line);
        for (const char* box : {"text", "text no"}) {
            d.openList = pid + ":" + box;
            dr->frames(3);
            std::printf("   '%s' popup '%s' %.0fx%.0f, focus '%s'\n", box, d.popup.id.c_str(), d.popup.box.w, d.popup.box.h, d.focus.c_str());
            CHECK(d.popup.id == pid + ":" + box && d.focus.empty() && d.popup.field.pick == vn::StoryEditor::Pick::PicturesOrColors); // (a wheel, the pictures and colours under it)
        }
        dr->shot("wheel_depends.png");
        // A name's start typed above the wheel finds the pictures and colours under it: "wh" leaves white, the first cell.
        d.openList = pid + ":text";
        dr->frames(3);
        CHECK(d.popup.id == pid + ":text");
        d.focus = "popup:color";
        d.buffer.clear();
        d.caret = d.anchor = 0;
        ++d.focusCount;
        d.anchorFocus = d.focusCount;
        for (const char c : std::string("wh")) {
            d.typed = std::string(1, c);
            dr->frames(1);
        }
        dr->frames(2);
        const float ck = d.popup.at.h / vn::layout::boxH, cpad = vn::layout::popupPad * ck;
        const float cellTop = d.popup.box.y + cpad + vn::layout::codeH * ck + cpad + vn::layout::itemH * ck + cpad + vn::layout::wheelD * ck + cpad;
        const float cellX = d.popup.box.x + cpad + vn::layout::cellW / 2 * ck, cellY = cellTop + vn::layout::cellH / 2 * ck;
        dr->shot("wheel_found.png");
        click(*dr, cellX, cellY);
        dr->frames(3);
        const std::string written = read(g + "/scripts/main.vnscript") + read(g + "/scripts/screens.vnscript");
        const auto imageAt = written.find("    image ");
        std::printf("   found by name: %s\n", imageAt == std::string::npos ? "" : written.substr(imageAt + 4, written.find('\n', imageAt) - imageAt - 4).c_str());
        CHECK(written.contains("image \"{if(night, 'white', 'white')}\"") && d.popup.id.empty());
        // The text format bar's Colour button: the wheel's colour goes around the words chosen, and changes there while the wheel stays open.
        d.view = View::Story;
        d.popup = {};
        d.focus.clear();
        const auto rowsNow = d.editor.rows(d.editor.scenes().front());
        const auto said = std::ranges::find(rowsNow, vn::StoryEditor::Kind::Line, &vn::StoryEditor::Row::kind);
        CHECK(said != rowsNow.end());
        d.row = said->line;
        const std::string wid = std::format("row:{}:Words", said->line);
        dr->frames(2);
        d.focus = wid;
        d.buffer = "Hello";
        d.anchor = 0;
        d.caret = 5;
        ++d.focusCount;
        d.anchorFocus = d.focusCount;
        dr->frames(2);
        d.popup = {.id = wid + ":format:Colour", .field = {.name = "colour", .box = vn::StoryEditor::Box::Colour}, .at = {400.0f, 300.0f, 100.0f, 26.0f}, .shown = true};
        dr->frames(3);
        std::printf("   format popup '%s' %.0fx%.0f\n", d.popup.id.c_str(), d.popup.box.w, d.popup.box.h);
        CHECK(d.popup.id == wid + ":format:Colour" && d.popup.box.h > 0.0f && d.focus == wid);
        const float fk = d.popup.at.h / vn::layout::barButtonH, fpad = vn::layout::popupPad * fk, fR = vn::layout::wheelD / 2 * fk;
        const float fcx = d.popup.box.x + fpad + fR, fcy = d.popup.box.y + fpad + vn::layout::codeH * fk + fpad + vn::layout::itemH * fk + fpad + fR;
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, fcx + fR * 0.95f, fcy); // the wheel's right edge
        dr->frames(2);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, fcx + fR * 0.95f, fcy);
        dr->frames(2);
        const std::string once = d.buffer;
        std::printf("   coloured: %s\n", once.c_str());
        CHECK(once.starts_with("{color #") && once.ends_with("}Hello{/color}") && d.popup.id == wid + ":format:Colour");
        const float alphaX = fcx + fR + (vn::layout::barGap + vn::layout::barW + vn::layout::barGap + vn::layout::barW / 2) * fk; // the alpha bar's middle: the same tag, half see-through now
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_DOWN, alphaX, fcy);
        dr->frames(2);
        mouse(*dr, SDL_EVENT_MOUSE_BUTTON_UP, alphaX, fcy);
        dr->frames(2);
        std::printf("   recoloured: %s\n", d.buffer.c_str());
        CHECK(d.buffer != once && d.buffer.starts_with("{color #") && d.buffer.ends_with("}Hello{/color}") && d.buffer.find("{color ") == d.buffer.rfind("{color "));
        CHECK(d.anchor == d.buffer.find("Hello") && d.caret == d.anchor + 5);
        std::printf("   above the wheel: '%s'\n", d.popup.field.value.c_str());
        CHECK(d.popup.field.value == d.buffer.substr(7, d.buffer.find('}') - 7)); // (the code above the wheel is the colour picked)
        dr->shot("wheel_format.png");
        d.popup = {};
        d.focus.clear();
        dr->frames(2);
    }

    if (want("astext")) {
        std::printf("astext. As text for a Library entry and a character, as lines and parts have it; a sound's box has a button to hear it; the Icon list shows the pictures\n");
        const std::string g = S + "/h/astext";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"A\"\nconfig developer yes\n\ncolor sky #2040ff\ncharacter Aiko color=sky\n\nlabel start\n    music beep\n    Hi.\n");
        fs::create_directories(g + "/images");
        fs::create_directories(g + "/audio");
        if (SDL_Surface* pic = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32)) {
            SDL_FillSurfaceRect(pic, nullptr, 0xff4080c0u);
            IMG_SavePNG(pic, (g + "/images/pic.png").c_str());
            SDL_DestroySurface(pic);
        }
        silentWav(g + "/audio/beep.wav");
        fs::remove_all(S + "/h/astext_data");
        auto dr = start(out, g, S + "/h/astext_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        auto typeInto = [&](const std::string& id, const std::string& text) { typeBox(*dr, d, id, text); };
        // The Library: a colour's line, as text.
        d.view = View::Library;
        d.library = "color";
        d.libraryItem = 0;
        d.asText = true;
        dr->frames(2);
        const auto sky = d.editor.definitions("color").front();
        CHECK(sky.name == "sky");
        dr->shot("astext_library.png");
        typeInto(std::format("def:color:{}:raw", sky.line), "color sky #ff0000");
        std::string main = read(g + "/scripts/main.vnscript");
        std::printf("   colour line: %s\n", main.contains("\ncolor sky #ff0000\n") ? "color sky #ff0000" : "NOT written");
        CHECK(main.contains("\ncolor sky #ff0000\n"));
        // The Story's character, as text.
        d.view = View::Story;
        d.character = 0;
        dr->frames(2);
        const auto aiko = d.editor.characters().front();
        CHECK(aiko.name == "Aiko");
        typeInto(std::format("character:{}:raw", aiko.line), "character Aiko color=sky blip=beep");
        main = read(g + "/scripts/main.vnscript");
        std::printf("   character line: %s\n", main.contains("\ncharacter Aiko color=sky blip=beep\n") ? "character Aiko color=sky blip=beep" : "NOT written");
        CHECK(main.contains("\ncharacter Aiko color=sky blip=beep\n"));
        d.asText = false;
        dr->frames(3);
        dr->shot("astext_character.png"); // (its form: the typing sound's box, with the button beside it to hear it)
        // The music line: its song's box has the button too.
        d.character.reset();
        const auto rows = d.editor.rows(d.editor.scenes().front());
        const auto music = std::ranges::find(rows, vn::StoryEditor::Kind::Music, &vn::StoryEditor::Row::kind);
        CHECK(music != rows.end());
        d.row = music->line;
        dr->frames(3);
        dr->shot("astext_music.png");
        // The Library's Audio: under the sound, a row for each translation says it plays this one, or has its own.
        write(g + "/translations/japanese.vnscript", "language \"Japanese\" code=ja\n");
        ++d.filesGeneration;
        d.view = View::Library;
        d.library = "sounds";
        d.libraryItem = 0;
        dr->frames(3);
        dr->shot("astext_sound.png");
        // Settings: the Icon list shows the pictures, as the Mouse pointer's does.
        d.view = View::Settings;
        d.openList = "setting:icon";
        dr->frames(3);
        std::printf("   icon popup '%s' %.0fx%.0f\n", d.popup.id.c_str(), d.popup.box.w, d.popup.box.h);
        CHECK(d.popup.id == "setting:icon" && d.popup.box.h > 70.0f); // (cells with the pictures, not rows of names)
        dr->shot("astext_icon.png");
        d.popup = {};
        e.developer_.useTool(vn::kReload);
        dr->frames(2);
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(d.problems.empty());
    }

    if (want("playon")) {
        std::printf("playon. the preview played on: the line it's at is lit in the list and kept in view; Preview on turns it off (none is made) and back on\n");
        const std::string g = S + "/h/playon";
        fs::remove_all(g);
        std::string main = "config title \"P\"\nconfig developer yes\n\nlabel start\n";
        for (int i = 1; i <= 30; ++i) main += std::format("    Line {}.\n", i);
        write(g + "/scripts/main.vnscript", main);
        fs::remove_all(S + "/h/playon_data");
        auto dr = start(out, g, S + "/h/playon_data");
        auto& d = dr->engine.developer_.tools;
        CHECK(d.view == View::Story && d.problems.empty());
        const auto rows = d.editor.rows(d.editor.scenes().front());
        CHECK(rows.size() == 30);
        d.row = rows.front().line;
        dr->frames(3);
        CHECK(d.preview && !d.previewPlaying && !d.previewAt);
        for (int n = 0; n < 40; ++n) { // played on: a click a line (one finishes a line's text, the next goes on)
            d.previewPlaying = true;
            d.preview->advance();
            dr->frames(2);
        }
        std::printf("   at %s line %zu: row %zu lit, the list scrolled %.0f; %zu lines have run (the preview's plays count)\n", d.previewAt ? d.previewAt->first.c_str() : "-",
                    d.previewAt ? d.previewAt->second : 0, d.previewRow.value_or(0), d.rowsScroll, dr->engine.reachedLines().size());
        CHECK(dr->engine.reachedLines().size() >= 20); // (a line run in the preview has run: no grey mark on it; two clicks a line)
        dr->shot("playon_counts.png");                 // (the counts line under the lines, on two lines when it needs them)
        CHECK(d.previewAt && d.previewAt->first == "scripts/main.vnscript" && d.previewRow && *d.previewRow >= 14 && d.rowsScroll > 0.0f);
        dr->shot("playon_lit.png");
        d.previewOn = false;
        dr->frames(3);
        CHECK(!d.preview && !d.previewPlaying && !d.previewAt);
        dr->shot("playon_off.png");
        d.previewOn = true;
        dr->frames(3);
        CHECK(d.preview && !d.previewPlaying);
        // It takes clicks as the game does: a screen the story opens has a button, clicked on the preview, that closes it
        // (and the story goes on); a click on nothing goes on to the next line.
        write(g + "/scripts/main.vnscript", "config title \"P\"\nconfig developer yes\n\nscreen menu on_show=\"set opened to yes\" on_hide=\"set opened to no\"\n    button \"Go\" x=100 y=100 w=200 h=60 action=close\n\n"
                                            "label start\n    Hi.\n    open menu\n    Bye.\n    The end.\n");
        dr->engine.developer_.useTool(vn::kReload);
        dr->frames(3);
        const auto rows2 = d.editor.rows(d.editor.scenes().front());
        CHECK(rows2.size() == 4 && d.problems.empty());
        d.row = rows2[1].line; // (the line that opens the menu: the preview shows it, waiting)
        dr->frames(3);
        CHECK(d.preview && d.preview->openingScreen() && d.previewShown.w > 0.0f);
        CHECK(vn::truthy(d.preview->variable("opened"))); // (its on_show= ran, as the game runs it: a title's music would play)
        auto on = [&](float x, float y) { return SDL_FPoint{d.previewShown.x + x * d.previewShown.w / 1280.0f, d.previewShown.y + y * d.previewShown.h / 720.0f}; };
        const SDL_FPoint button = on(200.0f, 130.0f), blank = on(640.0f, 100.0f);
        click(*dr, button.x, button.y);
        dr->frames(3);
        std::printf("   the menu's button clicked: opening %s, at line %zu\n", d.preview->openingScreen() ? "still" : "no more", d.previewAt ? d.previewAt->second : 0);
        CHECK(!d.preview->openingScreen() && d.previewPlaying && d.previewAt && d.previewAt->second == rows2[2].line);
        CHECK(!vn::truthy(d.preview->variable("opened"))); // (and its on_hide=, once it closed)
        click(*dr, blank.x, blank.y); // (the first click finishes the line's text, as in the game)
        dr->frames(3);
        click(*dr, blank.x, blank.y);
        dr->frames(3);
        std::printf("   two clicks on nothing: at line %zu\n", d.previewAt ? d.previewAt->second : 0);
        CHECK(d.previewAt && d.previewAt->second == rows2[3].line);
        dr->shot("playon_menu.png");
        // The Screens tab's Try it runs the screen's on_show= too (its music would be heard with Audio on), and its on_hide= when it stops.
        d.view = View::Screens;
        d.screenName = "menu";
        d.part.reset();
        dr->frames(3);
        d.trying = true; // (as Try it does)
        d.trySettings = dr->engine.settings_;
        d.sampleMade = false;
        dr->frames(3);
        std::printf("   tried: on_show ran %s\n", d.sample && vn::truthy(d.sample->variable("opened")) ? "yes" : "NO");
        CHECK(d.trying && d.tryScreen == "menu" && d.sample && vn::truthy(d.sample->variable("opened")));
    }

    if (want("boxes")) {
        std::printf("boxes. every box is of a kind: no table row without one; a show line's, a style's and an animation step's opacity list and suggest the same, show amounts as percents and write them so\n");
        const auto unkinded = vn::StoryEditor::boxesWithoutKind();
        for (const std::string& row : unkinded) std::printf("   no kind: %s\n", row.c_str());
        CHECK(unkinded.empty()); // (a padding, a title, a fade: the same box wherever it is, by construction)
        const std::string g = S + "/h/boxes";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"B\"\nconfig developer yes\ncharacter Aiko color=hotpink\nstyle big size=30 opacity=0.5\nanimation pop\n    opacity=0.4 over 1 second\n\n"
                                            "label start\n    music theme\n    show Aiko at left zoom=1.5\n    Hi.\n    scene #203040\n");
        fs::remove_all(S + "/h/boxes_data");
        auto dr = start(out, g, S + "/h/boxes_data");
        auto& editor = dr->engine.developer_.tools.editor;
        using Box = vn::StoryEditor::Box;
        using Field = vn::StoryEditor::Field;
        auto boxed = [&](std::vector<Field> fields, std::string_view name) { // (a field as its form draws it)
            for (Field& f : fields) vn::applyBox(f);
            return *std::ranges::find(fields, name, &Field::name);
        };
        const auto rows = editor.rows(editor.scenes().front());
        CHECK(rows.size() == 4);
        const Field once = boxed(editor.fields(rows[0]), "Once"), shown = boxed(editor.fields(rows[1]), "Opacity"), zoom = boxed(editor.fields(rows[1]), "Zoom");
        const Field style = boxed(editor.fields("style", editor.definitions("style").front()), "Opacity");
        const auto stepFields = editor.fields("animation", editor.definitions("animation").front());
        const Field step = boxed(stepFields, "Step 1 change 1 to");
        std::printf("   opacity: a show line's '%s', a style's '%s', a step's '%s'; the zoom shown '%s', the style's opacity '%s', the step's '%s'\n",
                    std::string(shown.suggest).c_str(), std::string(style.suggest).c_str(), std::string(step.suggest).c_str(), zoom.value.c_str(), style.value.c_str(), step.value.c_str());
        CHECK(shown.box == Box::Opacity && style.box == Box::Opacity && step.box == Box::Opacity);
        CHECK(shown.suggest == vn::StoryEditor::kindOf(Box::Opacity).suggest && style.suggest == shown.suggest && step.suggest == shown.suggest);
        CHECK(zoom.value == "150%" && style.value == "50%" && step.value == "40%"); // (shown as percents, however the scripts wrote them)
        CHECK(once.box == Box::YesNo && once.labels == "No|Yes"); // (a flag's buttons say the same everywhere, in the row's own order)
        const Field transition = boxed(editor.fields(rows[1]), "Transition");
        CHECK(transition.box == Box::Transition && transition.labels == "From Settings|Fade|Dissolve|Wipe|Slide|At once");
        // Written as percents too, however they're typed.
        auto showFields = editor.fields(rows[1]);
        std::ranges::find(showFields, std::string("Zoom"), &Field::name)->value = "0.5";
        std::ranges::find(showFields, std::string("Opacity"), &Field::name)->value = "75";
        const std::string showLine = editor.lineOf(rows[1].kind, showFields);
        auto styleFields = editor.fields("style", editor.definitions("style").front());
        std::ranges::find(styleFields, std::string("Opacity"), &Field::name)->value = "0.25";
        const auto styleLines = editor.linesOf("style", styleFields);
        auto stepEdit = stepFields;
        std::ranges::find(stepEdit, std::string("Step 1 change 1 to"), &Field::name)->value = "80%";
        const auto stepLines = editor.linesOf("animation", stepEdit);
        std::printf("   written: %s | %s | %s\n", showLine.c_str(), styleLines.front().c_str(), stepLines.size() > 1 ? stepLines[1].c_str() : "-");
        CHECK(showLine.contains("zoom=50%") && showLine.contains("opacity=75%"));
        CHECK(styleLines.front().contains("opacity=25%"));
        CHECK(stepLines.size() > 1 && stepLines[1].contains("opacity=80%"));
        // A style's padding and font size are pixels, like a part's look's (the same table), and a chapter's title is text like a record's.
        CHECK(boxed(editor.fields("style", editor.definitions("style").front()), "Padding").box == Box::Pixels);
        CHECK(boxed(editor.fields("style", editor.definitions("style").front()), "Size").box == Box::Pixels);
        const Field fade = boxed(editor.fields(rows[0]), "Fade in");
        CHECK(fade.box == Box::Seconds && fade.hint == "At once"); // (the kind shared, the row's own empty meaning kept)
        // A colour's box says the colour's name wherever it is: its own, or the built-in one it's most like (never a code).
        const vn::Palette own{{"aiko", {255, 136, 153, 255}}};
        std::printf("   colour names: %s, %s, %s, %s, %s, %s\n", vn::colorName({20, 30, 90, 255}).c_str(), vn::colorName({0, 0, 0, 128}).c_str(),
                    vn::colorName({150, 170, 255, 255}).c_str(), vn::colorName({34, 38, 68, 255}).c_str(), vn::colorName({255, 136, 153, 255}, &own).c_str(),
                    vn::colorName({255, 136, 153, 255}).c_str());
        CHECK(vn::colorName({20, 30, 90, 255}) == "navy" && vn::colorName({0, 0, 0, 128}) == "black, 50%");
        CHECK(vn::colorName({150, 170, 255, 255}) == "like lightblue" && vn::colorName({34, 38, 68, 255}) == "like navy");
        CHECK(vn::colorName({255, 136, 153, 255}, &own) == "aiko" && vn::colorName({255, 136, 153, 255}) == "like pink");
        CHECK(vn::colorName({51, 51, 51, 255}) == "like black" && vn::colorName({204, 204, 204, 255}) == "like silver");
        // The Story tab's lists say the same: the character's name colour as a square, a background's colour as a square and its name.
        dr->engine.developer_.tools.view = View::Story;
        dr->frames(3);
        dr->shot("boxes_story.png");
    }

    if (want("settings")) {
        std::printf("settings. the Settings tab: related settings share a row (a title and its version, a shake's strength and time...), each box still its own\n");
        const std::string g = S + "/h/settings";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", std::string(vn::kNewGameScript));
        fs::remove_all(S + "/h/settings_data");
        auto dr = start(out, g, S + "/h/settings_data");
        auto& e = dr->engine;
        auto& d = e.developer_.tools;
        d.view = View::Settings;
        dr->frames(3);
        dr->shot("settings.png");
        d.settingsScroll = 330.0f; // (the Text group: a colour code's box says what it's like; buttons that don't fit a half row go on under it)
        dr->frames(2);
        dr->shot("settings_text.png");
        auto typeInto = [&](const std::string& id, const std::string& text) { typeBox(*dr, d, id, text); };
        typeInto("setting:save_slots", "8");  // (the second of a pair)
        typeInto("setting:save_pages", "3");  // (the first)
        d.settingsScroll = 700.0f; // (down to the Images group: a row only takes typing while it's on screen)
        dr->frames(2);
        typeInto("setting:shake_time", "0.2");
        d.settingsScroll = 1500.0f; // (the new players' settings: percents shown as such, volumes with their list, buttons wrapping as a form's)
        dr->frames(2);
        dr->shot("settings_players.png");
        const std::string main = read(g + "/scripts/main.vnscript");
        std::printf("%s", main.c_str());
        CHECK(main.contains("config save_slots 8\n") && main.contains("config save_pages 3\n") && main.contains("config shake_time 0.2\n"));
        CHECK(d.editor.setting("save_slots") == "8" && d.editor.setting("save_pages") == "3" && d.problems.empty());
    }

    if (want("stray")) {
        std::printf("stray. a part line outside any screen is a mistake, not a hang\n");
        const std::string g = S + "/h/stray";
        fs::remove_all(g);
        write(g + "/scripts/main.vnscript", "config title \"X\"\nconfig developer yes\ninclude screens\n\nlabel start\n    Hi.\n");
        write(g + "/scripts/screens.vnscript", "screen panel\n    image #203040 x=0 y=0 w=1280 h=720\nbutton \"Back\" x=1080 y=654 w=140 h=40 action=close\n    key escape action=close\n");
        fs::remove_all(S + "/h/stray_data");
        auto dr = start(out, g, S + "/h/stray_data");
        auto& d = dr->engine.developer_.tools;
        for (const auto& p : d.problems) std::printf("   problem: %s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
        CHECK(!d.problems.empty());
        d.view = View::Screens;
        d.screenName = "panel";
        dr->frames(3);
    }

    std::printf(failures ? "%d FAILED\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
};

int main(int argc, char** argv) { return Harness::main(argc, argv); }
