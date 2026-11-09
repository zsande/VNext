// About: the versions, then the licences.
#include "DeveloperUi.hpp"

#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cmath>
#include <ranges>

namespace vn {

namespace {

// The licences, line by line; a heading is a line with a line of - or = under it.
struct AboutLine {
    std::string_view text;
    bool heading = false;
};

const std::vector<AboutLine>& aboutLines() {
    static const std::vector<AboutLine> lines = [] {
        std::vector<AboutLine> out;
        for (auto&& part : std::views::split(licences(), '\n')) out.push_back({std::string_view(part.begin(), part.end())});
        for (std::size_t i = 0; i + 1 < out.size(); ++i)
            out[i].heading = !out[i + 1].text.empty() && out[i + 1].text.find_first_not_of("-=") == std::string_view::npos;
        return out;
    }();
    return lines;
}

std::string versionOf(int v) {
    return std::format("{}.{}.{}", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
}

// The versions of what's built in, at the top of About.
std::string libraries() {
    int ft[3]{}, hb[3]{};
    TTF_GetFreeTypeVersion(&ft[0], &ft[1], &ft[2]);
    TTF_GetHarfBuzzVersion(&hb[0], &hb[1], &hb[2]);
    return std::format("VNext {}: SDL {}, SDL_image {}, SDL_ttf {} (FreeType {}.{}.{}, HarfBuzz {}.{}.{}), SDL_mixer {} and pl_mpeg", kVersion,
                       versionOf(SDL_GetVersion()), versionOf(IMG_Version()), versionOf(TTF_Version()), ft[0], ft[1], ft[2], hb[0], hb[1],
                       hb[2], versionOf(MIX_Version()));
}

} // namespace

// About's widest line, its room at its sides and its scrollbar's (measured once a size: the licences don't change).
float Developer::aboutLeastW(DeveloperUi& ui) const {
    static float at = 0.0f, widest = 0.0f;
    if (at != ui.k) {
        const Font& font = ui.font(layout::listText);
        widest = font.measure(libraries());
        for (const AboutLine& line : aboutLines()) widest = std::max(widest, font.measure(line.text));
        at = ui.k;
    }
    return widest + (20 + layout::scrollbarRoom) * ui.k;
}

// About, from the top: the versions, then the licences.
void Developer::drawAbout(DeveloperUi& ui, const SDL_FRect& box) {
    DeveloperTools& d = tools;
    const float k = ui.k, rowHeight = layout::aboutRowH * k;
    draw::rect(ui.r, box, kBox);
    const std::size_t pageLines = static_cast<std::size_t>(std::max(1.0f, (box.h - 12 * k) / rowHeight)); // (that fit)
    auto scroll = [&](float lines) { // (up is positive: to earlier lines)
        d.aboutScroll = lines < 0 ? d.aboutScroll + static_cast<std::size_t>(-lines) : d.aboutScroll - std::min(d.aboutScroll, static_cast<std::size_t>(lines));
    };
    if (const float turns = ui.wheelOver(box); turns != 0.0f) scroll(turns * 3.0f);
    const auto screenful = static_cast<float>(pageLines);
    (void)ui.keyStop(DeveloperTools::KeyStop::Kind::Press, box, std::string("About"_tr), "about", {}, DeveloperTools::kOwnUp | DeveloperTools::kOwnDown); // (up and down: its own)
    std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
        switch (key.key) {
        case SDLK_PAGEUP: scroll(screenful); return true;
        case SDLK_PAGEDOWN: scroll(-screenful); return true;
        default:
            if (d.isKey(key, ToolKey::Up, true)) scroll(1);
            else if (d.isKey(key, ToolKey::Down, true)) scroll(-1);
            else return false;
            return true;
        }
    });
    // Its scrollbar, by lines.
    const auto& all = aboutLines();
    const std::size_t total = all.size() + 2, most = total > pageLines ? total - pageLines : 0;
    d.aboutScroll = std::min(d.aboutScroll, most);
    float down = static_cast<float>(d.aboutScroll);
    ui.scrollbar(&d.aboutScroll, ui.barInside(box), screenful, static_cast<float>(total), down);
    d.aboutScroll = std::min(most, static_cast<std::size_t>(std::lround(down)));
    const float w = box.w - (total > pageLines ? layout::scrollbarRoom * k : 0.0f); // (its lines: beside its scrollbar)
    const DeveloperUi::Clip clipped(ui, box);
    float y = box.y + 6 * k;
    for (std::size_t i = d.aboutScroll; i < std::min(total, d.aboutScroll + pageLines); ++i) {
        const AboutLine* line = i < 2 ? nullptr : &all[i - 2];
        const std::string text = i == 0 ? libraries() : line ? std::string(line->text) : std::string{};
        const SDL_Color colour = !line ? theme::text : line->heading ? kAccent : line->text.starts_with("--") || line->text.starts_with("==") ? theme::dim : theme::text;
        ui.textFitted(text, box.x + 10 * k, y + ui.measure("Ag", layout::listText).y / 2, layout::listText, colour, w - 20 * k); // (one line: cut to fit, its whole in a tip)
        y += rowHeight;
    }
}

} // namespace vn
