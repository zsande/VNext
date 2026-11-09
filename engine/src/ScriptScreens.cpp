// Reading the screens: each screen's parts, and what each kind of part takes.
#include "ScriptParser.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

bool partTakes(std::string_view kind, std::string_view key) {
    const auto spec = std::ranges::find(parse::kKinds, kind, &parse::KindSpec::name);
    return spec != parse::kKinds.end() && std::ranges::contains(spec->options, key);
}

std::string_view partStyle(std::string_view kind, const std::function<bool(std::string_view)>& hasStyle) {
    const auto spec = std::ranges::find(parse::kKinds, kind, &parse::KindSpec::name);
    return spec != parse::kKinds.end() && !spec->style.empty() && hasStyle(spec->style) ? spec->style : "default";
}

namespace parse {

void Parser::screens() {
    for (const auto& [name, line] : screenLines_) {
        ScreenDef def{.name = name};
        auto c = words(*line, afterWords(line->text, 2));
        while (c && !c->done()) {
            const auto opt = c->option();
            if (!opt) {
                error(*line, "'{}' doesn't belong in a screen line."_tr(*c->next()));
                continue;
            }
            const auto& [key, value] = *opt;
            if (key == "story") {
                if (const std::string v = lower(value); v == "waits" || v == "goes_on") def.storyWaits = v == "waits";
                else error(*line, "'{}' can be {}, not '{}'."_tr(key, "waits or goes_on", value));
            }
            else if (key == "action_on_open") def.actionOnOpen = actions(*line, value);
            else if (key == "close_after") def.closeAfter = std::max(0.0f, number(*line, value, key).value_or(0.0f));
            else if (key == "action_on_close") def.actionOnClose = actions(*line, value);
            else if (key == "animation_on_open" || key == "animation_on_close") {
                if (const auto a = s_.animations.find(lower(value)); a != s_.animations.end())
                    (key == "animation_on_open" ? def.animationOnOpen : def.animationOnClose) = std::make_shared<const Animation>(a->second);
                else error(*line, "There's no animation called '{}'."_tr(value));
            }
            else if (key == "stop_music_on_open") def.stopOnOpen.music = boolean(*line, value, key).value_or(false);
            else if (key == "stop_music_on_close") def.stopOnClose.music = boolean(*line, value, key).value_or(false);
            else if (key == "stop_voice_on_open") def.stopOnOpen.voice = boolean(*line, value, key).value_or(false);
            else if (key == "stop_voice_on_close") def.stopOnClose.voice = boolean(*line, value, key).value_or(false);
            else error(*line, "Screens have no setting '{}'."_tr(key));
        }
        elements(def, line->children, -1);
        s_.screens.insert_or_assign(name, std::move(def));
    }
    for (const auto& [line, screen] : textboxes_)
        if (!s_.screens.contains(screen)) error(*line, "There's no screen called '{}' to use as a textbox."_tr(screen));
}

// A screen's parts, each followed by the ones in it (indented under it: their places are in its box).
void Parser::elements(ScreenDef& def, const std::vector<Line>& lines, int parent) {
    for (const Line& line : lines) {
        auto el = element(line);
        if (!el) continue;
        el->parent = parent;
        const auto list = std::ranges::find(kLists, std::string_view(el->option("list")), &std::pair<std::string_view, std::string_view>::first);
        def.elements.push_back(std::move(*el));
        if (list != kLists.end()) items_.emplace_back(list->second); // (the parts in it read its items' values: {chat.name}...)
        elements(def, line.children, static_cast<int>(def.elements.size()) - 1);
        if (list != kLists.end()) items_.pop_back();
    }
}

std::optional<Element> Parser::element(const Line& line) {
    auto cursor = words(line, line.text);
    if (!cursor) return std::nullopt;
    Cursor& c = *cursor;
    const std::string kind = lower(*c.next());
    const auto spec = std::ranges::find(kKinds, std::string_view(kind), &KindSpec::name);
    if (spec == kKinds.end()) {
        error(line, "'{}' isn't a screen part."_tr(kind));
        return std::nullopt;
    }
    Element el{.kind = spec->kind, .file = line.file, .line = line.number};
    if (!c.done() && (c.peek().quoted || c.peek().text.find('=') == std::string::npos)) el.text = *c.next();
    std::vector<std::pair<std::string, std::string>> props;
    while (!c.done()) {
        if (auto opt = c.option()) props.push_back(std::move(*opt));
        else error(line, "'{}' isn't a setting."_tr(*c.next()));
    }
    if (line.file < builtinFiles_) // (written for 1280 x 720)
        for (auto& kv : props) scaleBuiltIn(kv);

    // Base style: style=NAME, else the style named after the element kind, else "default".
    std::string styleName(partStyle(spec->name, [&](std::string_view name) { return styleLines_.contains(name); }));
    for (const auto& [k, v] : props)
        if (k == "style") {
            styleName = lower(v);
            if (!styleLines_.contains(styleName)) error(line, "There's no style called '{}'."_tr(v));
        }
    el.style = style(styleName, line).value_or(Style{});

    auto allowed = [&](std::string_view key) { return std::ranges::contains(spec->options, key); };
    for (const auto& [k, v] : props) {
        if (k == "style" || k == "name") continue; // (name=: what the editor calls it)
        if (k == "animation" || k == "pointed_animation" || k == "message_animation" || (allowed(k) && (k == "click_animation" || k == "item_animation"))) { // (any part's; a button's when it's clicked; a list's items')
            if (const auto a = s_.animations.find(lower(v)); a != s_.animations.end())
                (k == "animation" ? el.animation : k == "pointed_animation" ? el.pointedAnimation : k == "message_animation" ? el.messageAnimation
                 : k == "item_animation" ? el.itemAnimation : el.clickAnimation) = std::make_shared<const Animation>(a->second);
            else error(line, "There's no animation called '{}'."_tr(v));
        } else if (k == "x" || k == "y" || k == "width" || k == "height") { // (places from -1 to 1, shares of its box)
            if (auto n = number(line, v, k)) (k == "x" ? el.placeX : k == "y" ? el.placeY : k == "width" ? el.shareWidth : el.shareHeight) = *n;
        } else if (k == "when") {
            el.visible = formula(line, v);
        } else if (allowed(k) && k == "action") {
            el.actions = actions(line, v);
        } else if (allowed(k) && k == "drop") {
            el.drop = actions(line, v);
        } else if (allowed(k) && k == "confirm") {
            el.confirm = v;
        } else if (allowed(k) && (std::ranges::contains(kFormulaOptions, k) || (k == "count" && el.kind == ElementKind::Container))) {
            if (auto e = formula(line, v)) el.exprs.insert_or_assign(k, std::move(*e));
        } else if (allowed(k) && k == "keys") { // (as a control's: several, by spaces or commas)
            std::string list(v);
            std::ranges::replace(list, ',', ' ');
            for (const std::string& word : rawWords(list))
                if (const auto chord = parseKeyChord(word)) el.keys.push_back(*chord);
                else error(line, "'{}' isn't a key name."_tr(word));
        } else if (allowed(k) && (k == "clickable" || k == "chosen")) {
            (k == "clickable" ? el.clickable : el.chosen) = formula(line, v);
        } else if (allowed(k) && k.ends_with("_style")) {
            if (auto s = style(lower(v), line)) el.substyles.insert_or_assign(k, *s);
            else error(line, "There's no style called '{}'."_tr(v));
        } else if (allowed(k) && std::ranges::contains(kColourOptions, k)) {
            if (auto col = colour(line, v)) el.colours.insert_or_assign(k, *col);
        } else if (allowed(k)) {
            el.props.insert_or_assign(k, k == "variable" ? lower(v) : v);
        } else { // (its look: its style's alone, style=)
            error(line, "'{}' has no setting '{}'."_tr(kind, k));
        }
    }
    if (el.kind == ElementKind::Text || el.kind == ElementKind::Button) checkTextOf(line, el.text);
    else readValues(line, el.text); // (an image's or a movie's name: {chat.avatar}...)
    if (!el.confirm.empty()) checkTextOf(line, el.confirm);
    for (const auto& [key, value] : el.props) readValues(line, value); // (an app's title, an input's placeholder...)

    switch (el.kind) {
    case ElementKind::Image: // (an image by name, a colour, or a box of its style's background; one fading down with bottom=)
        if (el.text.empty() && !el.colours.contains("bottom") && el.style.background.a == 0) error(line, "Which image to show is missing."_tr());
        break;
    case ElementKind::Input:
    case ElementKind::Slider:
        if (el.kind == ElementKind::Slider && el.exprs.contains("value")) { // (a value shown, filling it: not dragged)
            if (!el.option("variable").empty() || !el.option("setting").empty())
                error(line, "A slider shows a value ('value=') or changes a variable ('variable=') or a player setting ('setting='), one of them."_tr());
            break;
        }
        if (el.kind == ElementKind::Slider && !el.option("setting").empty()) { // (a player setting: a volume, the text's speed...)
            const SettingControl* setting = findSettingControl(lower(el.option("setting")));
            if (!setting) error(line, "'{}' isn't a player setting a slider can change."_tr(el.option("setting")));
            if (!el.option("variable").empty()) error(line, "A slider changes a variable ('variable=') or a player setting ('setting='), not both."_tr());
            break;
        }
        if (el.kind == ElementKind::Slider && el.option("variable").empty()) error(line, "Which variable or player setting the slider changes is missing (or the value it shows: value=)."_tr());
        if (!el.option("variable").empty() && !isVariable(el.option("variable")))
            error(line, "'{}' can't be a variable name."_tr(el.option("variable")));
        else if (!el.option("variable").empty()) wrote(el.option("variable"));
        break;
    case ElementKind::Container: { // (list=, stack=, position=, app=: each one of its words)
        if (const std::string list = lower(el.option("list")); !list.empty()) {
            if (std::ranges::find(kLists, std::string_view(list), &std::pair<std::string_view, std::string_view>::first) == kLists.end())
                error(line, "There's no list called '{}'."_tr(el.option("list")));
            el.props.insert_or_assign("list", list);
        }
        static constexpr std::pair<std::string_view, std::string_view> kWordsOf[]{
            {"stack", "no down across"}, {"position", "top centre bottom"}, {"mode", "save load"}, {"close_at_end", "yes no"}};
        for (const auto& [key, words] : kWordsOf)
            if (const std::string v = lower(el.option(key)); !v.empty()) {
                if (!std::ranges::contains(std::views::split(words, ' ') | std::views::transform([](auto&& w) { return std::string_view(w.begin(), w.end()); }), std::string_view(v)))
                    error(line, "'{}' can be {}, not '{}'."_tr(key, key == "stack" ? "no, down or across" : key == "position" ? "top, centre or bottom" : key == "mode" ? "save or load" : "yes or no",
                                                             el.option(key)));
                el.props.insert_or_assign(std::string(key), v);
            }
        if (!el.option("mode").empty() && el.option("list") != "saves") error(line, "'mode' is for a list of saves (list=saves): whether they're saved into or loaded."_tr());
        if (const std::string app = lower(el.option("app")); !app.empty()) {
            if (!isName(app)) error(line, "'{}' can't be an app name (one word of letters, numbers, _ and -)."_tr(el.option("app")));
            el.props.insert_or_assign("app", app);
        }
        break;
    }
    case ElementKind::Control:
        el.text = lower(el.text);
        if (std::ranges::none_of(controlLines_, [&](const ControlLine& known) { return known.name == el.text; }))
            error(line, "There's no control called '{}'."_tr(el.text));
        if (el.number("slot", 1.0f) < 1.0f) error(line, "'slot' needs a number from 1 up, not '{}'."_tr(el.number("slot", 1.0f)));
        break;
    default:
        break;
    }
    return el;
}

// A built-in screen's (or style's) text size or padding, for a game of another size than the
// 1280 x 720 they're written for (places and shares fit any size).
void Parser::scaleBuiltIn(std::pair<std::string, std::string>& kv) const {
    const float sy = static_cast<float>(s_.gameSettings.height) / 720.0f;
    static constexpr std::string_view kSizes[]{"size", "padding"};
    if (sy == 1.0f || !std::ranges::contains(kSizes, kv.first)) return;
    if (const auto n = parseNumber(kv.second)) kv.second = std::format("{}", std::lround(*n * sy));
}

} // namespace parse

} // namespace vn
