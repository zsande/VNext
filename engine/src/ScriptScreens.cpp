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

ElementKind partKindOf(std::string_view kind, std::string_view text, const std::function<bool(std::string_view)>& says) {
    const std::string word = lower(kind);
    if (word == "text") {
        if (trim(text) == "{dialogue}") return ElementKind::Dialogue;
        for (const std::string_view key : {"action", "keys", "confirm", "drag", "drop", "click_animation", "clickable"})
            if (says(key)) return ElementKind::Button;
        return ElementKind::Text;
    }
    if (word == "image") return trim(text) == "{face}" ? ElementKind::Face : ElementKind::Image;
    if (word == "input") {
        const InputIs is = inputIs(says);
        return is == InputIs::Key ? ElementKind::Control : is == InputIs::Typed ? ElementKind::Input : ElementKind::Slider;
    }
    const auto spec = std::ranges::find(parse::kKinds, std::string_view(word), &parse::KindSpec::name);
    return spec != parse::kKinds.end() ? spec->kind : ElementKind::Container;
}

std::optional<float> madeUpVoicePitch(std::string_view value) {
    static constexpr std::pair<std::string_view, float> kHeights[]{{"high", 250.0f}, {"medium", 190.0f}, {"low", 135.0f}, {"deep", 105.0f}};
    if (const auto height = std::ranges::find(kHeights, lower(value), &std::pair<std::string_view, float>::first); height != std::end(kHeights)) return height->second;
    if (const auto hz = parseNumber(value); hz && *hz >= 50.0f && *hz <= 600.0f) return *hz;
    return std::nullopt;
}

InputIs inputIs(const std::function<bool(std::string_view)>& says) {
    if (says("control")) return InputIs::Key;
    if (says("value")) return InputIs::Shown;
    if (says("setting")) return InputIs::Setting;
    for (const std::string_view key : {"minimum", "maximum", "step", "vertical", "track", "fill", "knob"})
        if (says(key)) return InputIs::Slid;
    return InputIs::Typed;
}

std::span<const std::string_view> inputTakes(InputIs is) {
    static constexpr std::string_view kTyped[]{"variable", "most_letters", "placeholder", "action", "confirm", "clickable"};
    static constexpr std::string_view kSlid[]{"variable", "minimum", "maximum", "step", "vertical", "action", "clickable", "track", "fill", "knob"};
    static constexpr std::string_view kSetting[]{"setting", "vertical", "action", "clickable", "track", "fill", "knob"}; // (its own range: its steps a twentieth)
    static constexpr std::string_view kKey[]{"control", "slot", "waiting"};
    static constexpr std::string_view kShown[]{"value", "minimum", "maximum", "vertical", "track", "fill", "knob"}; // (not dragged)
    switch (is) {
    case InputIs::Typed: return kTyped;
    case InputIs::Slid: return kSlid;
    case InputIs::Setting: return kSetting;
    case InputIs::Key: return kKey;
    case InputIs::Shown: return kShown;
    }
    return kTyped;
}

namespace {

// What's said of a setting its word takes but a part doesn't, by what it is (kFlavours, inputTakes).
std::string noSetting(ElementKind kind, std::optional<InputIs> input, std::string_view key) {
    if (input) switch (*input) {
        case InputIs::Typed: return "An input to type in has no setting '{}'."_tr(key);
        case InputIs::Slid: return "A slider has no setting '{}'."_tr(key);
        case InputIs::Setting: return "A slider changing a player setting has no setting '{}'."_tr(key);
        case InputIs::Key: return "An input for a control's key has no setting '{}'."_tr(key);
        case InputIs::Shown: return "A slider showing a value has no setting '{}'."_tr(key);
        }
    return kind == ElementKind::Face ? "A visual showing {{face}} has no setting '{}'."_tr(key) : "A text showing {{dialogue}} has no setting '{}'."_tr(key);
}

} // namespace

std::string_view partStyle(ElementKind kind, const std::function<bool(std::string_view)>& hasStyle) {
    static constexpr std::pair<ElementKind, std::string_view> kStyles[]{{ElementKind::Text, "text"},       {ElementKind::Button, "button"},  {ElementKind::Dialogue, "dialogue"},
                                                                     {ElementKind::Face, "face"},       {ElementKind::Control, "setting"}, {ElementKind::Input, "input"},
                                                                     {ElementKind::Slider, "slider"},   {ElementKind::Flowchart, "flowchart"}};
    const auto it = std::ranges::find(kStyles, kind, &std::pair<ElementKind, std::string_view>::first);
    return it != std::end(kStyles) && hasStyle(it->second) ? it->second : "default";
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
            else if (key == "stop_on_open" || key == "stop_on_close") { // (sound types, with commas between them)
                auto& stops = key == "stop_on_open" ? def.stopOnOpen : def.stopOnClose;
                for (const auto part : std::views::split(std::string_view(value), ',')) {
                    const std::string type = lower(trim(std::string_view(part.begin(), part.end())));
                    if (type.empty()) continue;
                    if (s_.soundTypes.contains(type)) stops.push_back(type);
                    else error(*line, s_.visualTypes.contains(type) ? "'{}' is a visual type, not a sound one."_tr(type) : "There's no sound type called '{}'."_tr(type));
                }
            }
            else error(*line, "Screens have no setting '{}'."_tr(key));
        }
        elements(def, line->children, -1);
        s_.screens.insert_or_assign(name, std::move(def));
    }
    // A textbox (the game's, and each one a character has as their own) shows the words of the lines said in it: without
    // a text showing {dialogue} they'd be heard, a voice or made-up speech, with nothing to read.
    auto showsWords = [&](const std::string& screen) {
        const auto s = s_.screens.find(screen);
        return s == s_.screens.end() || std::ranges::any_of(s->second.elements, [](const Element& e) { return e.kind == ElementKind::Dialogue; });
    };
    if (const auto own = screenLines_.find("textbox"); own != screenLines_.end() && !showsWords("textbox"))
        error(*own->second, "A textbox needs a text showing {{dialogue}}, where the words of the lines said in it show."_tr());
    for (const auto& [line, screen] : textboxes_)
        if (!s_.screens.contains(screen)) error(*line, "There's no screen called '{}' to use as a textbox."_tr(screen));
        else if (!showsWords(screen)) error(*line, "'{}' needs a text showing {{dialogue}} to be a textbox, where the words of the lines said in it show."_tr(screen));
}

// A screen's parts, each followed by the ones in it (indented under it: their places are in its box). A container
// with screen= shows that screen in its box: its parts, as it has them, under the container's own (one that only
// closes it, a Back, gives way to what the container's in: in the phone's app, the phone's Back).
void Parser::elements(ScreenDef& def, const std::vector<Line>& lines, int parent) {
    for (const Line& line : lines) {
        auto el = element(line);
        if (!el) continue;
        if (!showing_.empty() && el->kind == ElementKind::Button && el->actions.size() == 1)
            if (const auto* u = std::get_if<cmd::Ui>(&el->actions.front()); u && u->verb == "close") continue;
        el->parent = parent;
        const auto list = std::ranges::find(kLists, std::string_view(el->option("list")), &std::pair<std::string_view, std::string_view>::first);
        const std::string shows = lower(el->option("screen"));
        def.elements.push_back(std::move(*el));
        const int at = static_cast<int>(def.elements.size()) - 1;
        if (!shows.empty()) {
            const auto screen = screenLines_.find(shows);
            if (screen == screenLines_.end()) error(line, "There's no screen called '{}'."_tr(shows));
            else if (shows == lower(def.name) || std::ranges::contains(showing_, shows)) { // (said where the loop closes, once, even in a screen being shown)
                const int was = std::exchange(quiet_, 0);
                const std::size_t before = s_.errors.size();
                error(line, "A screen can't show itself, in it or in a screen it shows."_tr());
                if (std::any_of(s_.errors.begin(), s_.errors.begin() + static_cast<std::ptrdiff_t>(before), [&](const ScriptError& e) {
                        return e.file == s_.errors.back().file && e.line == s_.errors.back().line && e.message == s_.errors.back().message;
                    }))
                    s_.errors.pop_back();
                quiet_ = was;
            }
            else {
                showing_.push_back(shows);
                ++quiet_;
                elements(def, screen->second->children, at);
                --quiet_;
                showing_.pop_back();
            }
        }
        if (list != kLists.end()) items_.emplace_back(list->second); // (the parts in it read its items' values: {chat.name}...)
        elements(def, line.children, at);
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
    // What it is, as it's written: a text clicked a button, {dialogue} the line said, {face} the speaker's face; an input a
    // key, a slider or a box to type in (the control it changes: its name, as a control's part had it).
    auto says = [&](std::string_view key) { return std::ranges::contains(props, key, &std::pair<std::string, std::string>::first); };
    el.kind = partKindOf(kind, el.text, says);
    const std::optional<InputIs> input = kind == "input" ? std::optional(inputIs(says)) : std::nullopt;
    if (el.kind == ElementKind::Dialogue || el.kind == ElementKind::Face) el.text.clear();
    if (el.kind == ElementKind::Control)
        for (auto it = props.begin(); it != props.end(); ++it)
            if (it->first == "control") {
                el.text = lower(it->second);
                props.erase(it);
                break;
            }

    // Base style: style=NAME, else the one named after what it is (a button's "button"...), else "default".
    std::string styleName(partStyle(el.kind, [&](std::string_view name) { return styleLines_.contains(name); }));
    for (const auto& [k, v] : props)
        if (k == "style") {
            styleName = lower(v);
            if (!styleLines_.contains(styleName)) error(line, "There's no style called '{}'."_tr(v));
        }
    el.style = style(styleName, line).value_or(Style{});

    // (what it takes of its word's settings, by what it is: a text showing {dialogue} none of a text's own, an input by what it
    // changes...)
    const auto flavour = std::ranges::find(kFlavours, el.kind, &FlavourSpec::kind);
    auto allowed = [&](std::string_view key) {
        if (!std::ranges::contains(spec->options, key)) return false;
        if (input) return std::ranges::contains(inputTakes(*input), key);
        return flavour == kFlavours.end() || std::ranges::contains(flavour->options, key);
    };
    for (const auto& [k, v] : props) {
        if (k == "style" || k == "name") continue; // (name=: what the editor calls it)
        if (k == "animation" || k == "pointed_animation" || k == "message_animation" ||
            (allowed(k) && (k == "click_animation" || k == "item_animation" || k == "animation_on_show"))) { // (any part's; a button's when it's clicked; a list's items'; a visual's as it comes)
            if (const auto a = s_.animations.find(lower(v)); a != s_.animations.end())
                (k == "animation" ? el.animation : k == "pointed_animation" ? el.pointedAnimation : k == "message_animation" ? el.messageAnimation
                 : k == "item_animation" ? el.itemAnimation : k == "animation_on_show" ? el.showAnimation : el.clickAnimation) = std::make_shared<const Animation>(a->second);
            else if (lower(v) != "none" || (k != "animation" && k != "animation_on_show")) error(line, "There's no animation called '{}'."_tr(v));
        } else if (k == "x" && places_.contains(lower(v))) { // (a place's name, as a show line's: left, the game's own...)
            el.placeX = places_.find(lower(v))->second * 2.0f - 1.0f;
        } else if (k == "x" || k == "y" || k == "width" || k == "height") { // (places from -1 to 1, shares of its box, or pixels: 140px)
            const bool pixels = (k == "width" || k == "height") && lower(v).ends_with("px");
            if (auto n = number(line, pixels ? v.substr(0, v.size() - 2) : v, k)) (k == "x" ? el.placeX : k == "y" ? el.placeY : k == "width" ? el.shareWidth : el.shareHeight) = *n;
            if (pixels) (k == "width" ? el.pixelWidth : el.pixelHeight) = true;
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
        } else if ((input || flavour != kFlavours.end()) && std::ranges::contains(spec->options, k)) { // (its word's, but not what it is)
            error(line, noSetting(el.kind, input, k));
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
        if (el.text.empty() && !el.colours.contains("bottom") && el.style.background.a == 0) error(line, "Which visual to show is missing."_tr());
        screenVisual(line, el, props);
        break;
    case ElementKind::Input:
    case ElementKind::Slider: // (a variable it changes, a player setting that's one: the rest is what it takes)
        if (!el.option("setting").empty() && !findSettingControl(lower(el.option("setting"))))
            error(line, "'{}' isn't a player setting a slider can change."_tr(el.option("setting")));
        if (input != InputIs::Typed && input != InputIs::Slid) break;
        if (el.option("variable").empty()) error(line, input == InputIs::Typed ? "Which variable to type into is missing."_tr() : "Which variable to change is missing."_tr());
        else if (!isVariable(el.option("variable"))) error(line, "'{}' can't be a variable name."_tr(el.option("variable")));
        else wrote(el.option("variable"));
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

// A screen's visual as the story shows one: a visual named by a visual line, or a character (with an expression), its own
// settings where the part says none (where it is, its look, its animations, a movie's playing, a colour's box's size);
// its type (its own, or the part's: one every game has or the game's own) taking what it shows; its look's numbers.
void Parser::screenVisual(const Line& line, Element& el, const std::vector<std::pair<std::string, std::string>>& props) {
    auto said = [&](std::string_view key) { return std::ranges::any_of(props, [&](const auto& kv) { return kv.first == key; }); };
    const bool named = !el.text.empty() && !el.text.contains('{'); // (a value's, {open_image}: whatever it is then)
    const std::string key = lower(el.text);
    const Script::NamedVisual* own = nullptr;
    if (named)
        if (const auto v = s_.visuals.find(key); v != s_.visuals.end()) own = &v->second;
    const CharacterDef* who = named && !own ? s_.character(key) : nullptr;
    const std::string type = said("type") ? lower(el.option("type")) : own ? own->type : who ? std::string("character") : std::string{};
    if (!type.empty()) {
        if (const auto t = s_.visualTypes.find(type); t == s_.visualTypes.end())
            error(line, s_.soundTypes.contains(type) ? "'{}' is a sound type, not a visual one."_tr(el.option("type")) : "There's no type called '{}'."_tr(el.option("type")));
        else if (named) { // (what it takes: a character, or not; an image, a movie or a colour is told apart by the files, the editor's)
            if (who && !t->second.accepts.contains("character")) error(line, "'{}' doesn't take characters."_tr(type));
            if (!who && t->second.accepts == "character") error(line, "'{}' takes characters: there's no character called '{}'."_tr(type, el.text));
        }
        el.props.insert_or_assign("type", type);
    }
    if (said("expression") && named && !who) error(line, "'expression' is a character's, and '{}' isn't one."_tr(el.text));
    for (const std::string_view k : {"zoom", "opacity", "rotation"}) // (zoom and opacity in percent too: 150%)
        if (said(k)) {
            if (const auto n = parseNumber(el.option(k))) el.props.insert_or_assign(std::string(k), std::format("{}", *n));
            else error(line, "'{}' needs a number, not '{}'."_tr(k, el.option(k)));
        }
    if (said("flip")) (void)boolean(line, el.option("flip"), "flip");
    // (its own settings: where it is, its look and its animations; a visual's movie's playing and its colour's box's size)
    if (const StageProps* comes = own ? &own->comes : who ? &who->comes : nullptr) {
        if (comes->x && !said("x")) el.placeX = *comes->x * 2.0f - 1.0f;
        if (comes->y && !said("y")) el.placeY = 1.0f - *comes->y * 2.0f;
        if (comes->zoom && !said("zoom")) el.props.insert_or_assign("zoom", std::format("{}", *comes->zoom));
        if (comes->opacity && !said("opacity")) el.props.insert_or_assign("opacity", std::format("{}", *comes->opacity));
        if (comes->rotation && !said("rotation")) el.props.insert_or_assign("rotation", std::format("{}", *comes->rotation));
        if (comes->flip && !said("flip")) el.props.insert_or_assign("flip", *comes->flip ? "yes" : "no");
        if (comes->tint && !said("tint")) el.colours.insert_or_assign("tint", *comes->tint);
    }
    auto animationOf = [&](const std::optional<std::string>& name) -> std::shared_ptr<const Animation> {
        const auto a = name && !name->empty() ? s_.animations.find(*name) : s_.animations.end();
        return a != s_.animations.end() ? std::make_shared<const Animation>(a->second) : nullptr;
    };
    if (!said("animation") && (own || who)) el.animation = animationOf(own ? own->animation : who->animation);
    if (!said("animation_on_show") && (own || who)) el.showAnimation = animationOf(own ? own->animationOnShow : who->animationOnShow);
    if (own) {
        if (own->loop && !said("loop")) el.props.insert_or_assign("loop", *own->loop ? "yes" : "no");
        if (own->volume && !said("volume")) el.props.insert_or_assign("volume", std::format("{}", *own->volume));
        if (own->width && !said("width")) el.shareWidth = *own->width, el.pixelWidth = false;
        if (own->height && !said("height")) el.shareHeight = *own->height, el.pixelHeight = false;
    }
}

// A built-in screen's (or style's) text size or padding, for a game of another size than the
// 1280 x 720 they're written for (places and shares fit any size).
void Parser::scaleBuiltIn(std::pair<std::string, std::string>& kv) const {
    const float sy = lookScale(static_cast<float>(s_.gameSettings.width), static_cast<float>(s_.gameSettings.height));
    static constexpr std::string_view kSizes[]{"size", "padding"};
    if (sy == 1.0f) return;
    if (lower(kv.second).ends_with("px")) { // (a size or gap in pixels)
        if (const auto n = parseNumber(std::string_view(kv.second).substr(0, kv.second.size() - 2))) kv.second = std::format("{}px", std::lround(*n * sy));
    } else if (std::ranges::contains(kSizes, kv.first)) {
        if (const auto n = parseNumber(kv.second)) kv.second = std::format("{}", std::lround(*n * sy));
    }
}

} // namespace parse

} // namespace vn
