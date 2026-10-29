#pragma once

// What the developer files (Developer*.cpp, StoryEditor.cpp) share, and only they include: the tables
// their forms are made from, and the helpers more than one of them uses. They're in a namespace of
// their own (dev) that the developer files use as if each had declared them.

#include "Developer.hpp"
#include "Engine.hpp"

#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <deque>
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <climits>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <numeric>
#include <ranges>
#include <set>
#include <span>

namespace vn::dev {

constexpr std::size_t npos = std::string_view::npos;
constexpr int kIndent = 4; // spaces for each level the editor adds

// For as long as it lives, `place` holds what `standIn` held (the preview's story in the engine's place, Try
// it's settings in the player's), and standIn what place held: swapped back when it goes, whatever happens.
template <class T>
class SwappedIn {
public:
    SwappedIn(T& place, T& standIn) : place_(place), standIn_(standIn) { std::swap(place_, standIn_); }
    ~SwappedIn() { std::swap(place_, standIn_); }
    SwappedIn(const SwappedIn&) = delete;
    SwappedIn& operator=(const SwappedIn&) = delete;
    SwappedIn(SwappedIn&&) = delete;
    SwappedIn& operator=(SwappedIn&&) = delete;

private:
    T& place_;
    T& standIn_;
};

// For as long as it lives, `place` is `value`; then it's what it was.
template <class T>
class SetFor {
public:
    SetFor(T& place, std::type_identity_t<T> value) : place_(place), was_(std::exchange(place, std::move(value))) {}
    ~SetFor() { place_ = std::move(was_); }
    SetFor(const SetFor&) = delete;
    SetFor& operator=(const SetFor&) = delete;
    SetFor(SetFor&&) = delete;
    SetFor& operator=(SetFor&&) = delete;

private:
    T& place_;
    T was_;
};

std::string leading(std::string_view line);
bool blank(std::string_view line);
bool partLine(std::string_view line); // (not a blank line or a note)

// A text's lines (a line break at its end: an empty last line; a CR before one: dropped).
std::vector<std::string> linesOf(std::string_view text);

// An index into a list that may have shrunk: none, once it's past the end.
void clampIndex(std::optional<std::size_t>& index, std::size_t count);
std::string fieldValue(const std::vector<StoryEditor::Field>& fields, std::string_view name);
std::string_view afterFirst(std::string_view s);
bool isWordChar(char c);
std::string joined(const std::vector<std::string>& words, std::size_t from = 0, std::string_view sep = " ");

// A number as screens write their places and shares: at most 4 places after the point (0.5, -0.0625).
std::string unitText(float v);

// placeEdge() the other way round: the place that puts the left (or top) edge `at` (from the start of a
// box `span` big) of something `size` big (0: placed by its corner).
float placeFor(float at, float size, float span);

// A setting's value as it's written: in quotes when it has a space (or ; or =) in it.
std::string written(const std::string& value);
bool validName(std::string_view s);

// The forms of most commands, as a list of parts: the words the line starts with (a choice of them,
// when there are several), then words in their places, then settings in any order. Words the form
// has no part for are kept as they are (a hidden field).
struct Slot {
    enum class Type : std::uint8_t {
        Word,   // the next word
        Words,  // the next words, up to a setting (with `choices`: several of them, as buttons)
        Text,   // the rest, up to name=value settings; in quotes if `key` is "quoted"
        After,  // the word after `key` ("fade 2", "as rain"); `seconds`: a time ("over 1 second")
        Flag,   // choices "no yes": `key` written for yes; other choices: the chosen one written (but the first)
        Option, // key=value
    };
    Type type;
    std::string_view name, key;
    StoryEditor::Box box = StoryEditor::Box::None; // its box's kind (its pick comes from that)
    std::string_view choices, labels, hint;
    std::string_view only; // written (and shown) only after these words
    bool seconds = false;
    std::string_view suggest; // values it often has ("|" between them, "value:what it is")
    bool commas = false;      // an Option that's several of its choices, with commas between them (all: not written)
    bool rich = false;        // text that can be bold, coloured...
    std::string_view group;   // the heading it starts in the form
    std::string_view label;   // what the form calls it (else its name)
};

struct CommandForm {
    StoryEditor::Kind kind;
    std::string_view verbs, labels; // the words a line starts with ("_": a space), and what their buttons say
    std::span<const Slot> slots;
    std::string_view what = "Kind"; // what the form calls the choice of those words
};

using Type = Slot::Type;
using Box = StoryEditor::Box;
constexpr Slot kColorSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                             {.type = Type::Word, .name = "Colour", .box = Box::Colour}};

constexpr Slot kPositionSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}, {.type = Type::Word, .name = "Place", .box = Box::Across}};

constexpr Slot kFontSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                            {.type = Type::Word, .name = "File", .key = "quoted", .box = Box::FontFile},
                            {.type = Type::Word, .name = "Style", .box = Box::FontStyle, .hint = "Default"}};

// A style's settings (and a screen part's look).
constexpr Slot kStyleSlots[]{
    {.type = Type::Word, .name = "Name", .box = Box::Name},
    {.type = Type::Option, .name = "Builds on", .key = "parent", .box = Box::Style, .hint = "default", .label = "Based on"},
    {.type = Type::Option, .name = "Font", .key = "font", .box = Box::Font, .hint = "From its base style", .group = "Text"},
    {.type = Type::Option, .name = "Size", .key = "size", .box = Box::Pixels, .hint = "From its base style", .label = "Font size"},
    {.type = Type::Option, .name = "Colour", .key = "color", .box = Box::Colour, .hint = "From its base style", .suggest = "speaker:the speaker's colour", .label = "Text colour"},
    {.type = Type::Option, .name = "Across", .key = "align", .box = Box::Align, .choices = "inherit left center right", .label = "Align"},
    {.type = Type::Option, .name = "Down", .key = "valign", .box = Box::VAlign, .choices = "inherit top center bottom", .label = "Align vertically"},
    {.type = Type::Option, .name = "Line spacing", .key = "line_spacing", .box = Box::LineSpacing, .hint = "From its base style"},
    {.type = Type::Option, .name = "Shadow", .key = "shadow", .box = Box::YesNoStyle, .choices = "inherit yes no"},
    {.type = Type::Option, .name = "Shadow's colour", .key = "shadow_color", .box = Box::Colour, .hint = "From its base style", .label = "Shadow colour"},
    {.type = Type::Option, .name = "Shadow's offset", .key = "shadow_offset", .box = Box::Pixels, .hint = "From its base style", .label = "Shadow offset"},
    {.type = Type::Option, .name = "Background", .key = "bg", .box = Box::Colour, .hint = "From its base style", .group = "Box", .label = "Background colour"},
    {.type = Type::Option, .name = "Border", .key = "border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Image", .key = "image", .box = Box::Picture, .hint = "None"},
    {.type = Type::Option, .name = "Padding", .key = "padding", .box = Box::Pixels, .hint = "From its base style"},
    {.type = Type::Option, .name = "Opacity", .key = "opacity", .box = Box::Opacity, .hint = "From its base style"},
    {.type = Type::Option, .name = "Pointed at: colour", .key = "hover_color", .box = Box::Colour, .hint = "From its base style", .group = "Pointed at", .label = "Text colour"},
    {.type = Type::Option, .name = "Pointed at: background", .key = "hover_bg", .box = Box::Colour, .hint = "From its base style", .label = "Background colour"},
    {.type = Type::Option, .name = "Pointed at: border", .key = "hover_border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Pointed at: image", .key = "hover_image", .box = Box::Picture, .hint = "From its base style", .label = "Image"},
    {.type = Type::Option, .name = "Chosen: colour", .key = "selected_color", .box = Box::Colour, .hint = "From its base style", .group = "Chosen", .label = "Text colour"},
    {.type = Type::Option, .name = "Chosen: background", .key = "selected_bg", .box = Box::Colour, .hint = "From its base style", .label = "Background colour"},
    {.type = Type::Option, .name = "Chosen: border", .key = "selected_border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Chosen: image", .key = "selected_image", .box = Box::Picture, .hint = "From its base style", .label = "Image"},
    {.type = Type::Option, .name = "Can't be clicked: colour", .key = "disabled_color", .box = Box::Colour, .hint = "From its base style", .group = "Can't be clicked", .label = "Text colour"},
    {.type = Type::Option, .name = "Can't be clicked: background", .key = "disabled_bg", .box = Box::Colour, .hint = "From its base style", .label = "Background colour"},
    {.type = Type::Option, .name = "Can't be clicked: border", .key = "disabled_border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Can't be clicked: image", .key = "disabled_image", .box = Box::Picture, .hint = "From its base style", .label = "Image"},
    {.type = Type::Option, .name = "Text size follows Settings", .key = "text_size", .box = Box::YesNoStyle, .choices = "inherit yes no", .group = "Follows Settings", .label = "Text size"},
    {.type = Type::Option, .name = "Opacity follows Settings", .key = "textbox_opacity", .box = Box::YesNoStyle, .choices = "inherit yes no", .label = "Opacity"},
    {.type = Type::Option, .name = "Sound when pointed at", .key = "hover_sound", .box = Box::Sound, .hint = "From its base style", .group = "Sounds", .label = "Sound on hover"},
    {.type = Type::Option, .name = "Sound when clicked", .key = "click_sound", .box = Box::Sound, .hint = "From its base style", .label = "Sound on click"}};

constexpr Slot kAtlasSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                             {.type = Type::Option, .name = "Image", .key = "image", .box = Box::Picture},
                             {.type = Type::Option, .name = "Frames in a row", .key = "columns", .box = Box::Count},
                             {.type = Type::Option, .name = "Names like", .key = "like", .box = Box::Record, .label = "Same names as"}};

constexpr Slot kSubPictureSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                                  {.type = Type::Option, .name = "Cut from", .key = "image", .box = Box::Picture},
                                  {.type = Type::Option, .name = "Left", .key = "x", .box = Box::Pixels, .hint = "0"},
                                  {.type = Type::Option, .name = "Top", .key = "y", .box = Box::Pixels, .hint = "0"},
                                  {.type = Type::Option, .name = "Width", .key = "w", .box = Box::Pixels},
                                  {.type = Type::Option, .name = "Height", .key = "h", .box = Box::Pixels}};

constexpr Slot kAnimationSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                                 {.type = Type::Flag, .name = "Over and over", .key = "loop", .box = Box::YesNo, .choices = "no yes", .label = "Loop"}};

constexpr Slot kGallerySlots[]{{.type = Type::Word, .name = "Image", .box = Box::Picture}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text}};

constexpr Slot kEndingSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text},
                              {.type = Type::Option, .name = "Hint", .key = "hint", .box = Box::Text, .hint = "None"}};

constexpr Slot kSongSlots[]{{.type = Type::Word, .name = "Song", .box = Box::Sound}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text}};

constexpr Slot kAchievementSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text},
                                   {.type = Type::Word, .name = "Description", .key = "quoted", .box = Box::Text, .hint = "None"},
                                   {.type = Type::Flag, .name = "Hidden until earned", .key = "hidden", .box = Box::YesNo, .choices = "no yes"}};

constexpr Slot kTipSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text},
                           {.type = Type::Word, .name = "Description", .key = "quoted", .box = Box::Text, .hint = "None"}};

constexpr Slot kControlSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                               {.type = Type::Option, .name = "Key", .key = "keys", .box = Box::Key, .hint = "None"},
                               {.type = Type::Option, .name = "Does", .key = "action", .box = Box::Action, .hint = "None"},
                               {.type = Type::Option, .name = "Also in menus", .key = "everywhere", .box = Box::YesNo, .choices = "no yes", .label = "Works in menus"},
                               {.type = Type::Option, .name = "Only when", .key = "if", .box = Box::Variable, .hint = "Always", .label = "Works when"}};

struct DefinitionKind {
    std::string_view kind, label, one; // what the Library's list calls them, and one of them (its form's title)
    std::span<const Slot> slots;
    bool listed = true; // the Library lists them (sub-images are under their images, fonts under their files; places aren't)
};

constexpr DefinitionKind kDefinitionKinds[]{
    {.kind = "color", .label = "Colours", .one = "Colour", .slots = kColorSlots},
    {.kind = "style", .label = "Styles", .one = "Style", .slots = kStyleSlots},
    {.kind = "font", .label = "Font", .one = "Font", .slots = kFontSlots, .listed = false},
    {.kind = "position", .label = "Places", .one = "Place", .slots = kPositionSlots, .listed = false},
    {.kind = "atlas", .label = "Atlases", .one = "Atlas", .slots = kAtlasSlots},
    {.kind = "picture", .label = "Sub-image", .one = "Sub-image", .slots = kSubPictureSlots, .listed = false},
    {.kind = "animation", .label = "Animations", .one = "Animation", .slots = kAnimationSlots},
    {.kind = "gallery", .label = "Gallery", .one = "Gallery image", .slots = kGallerySlots, .listed = false},
    {.kind = "ending", .label = "Endings", .one = "Ending", .slots = kEndingSlots},
    {.kind = "song", .label = "Songs", .one = "Song", .slots = kSongSlots, .listed = false},
    {.kind = "achievement", .label = "Achievements", .one = "Achievement", .slots = kAchievementSlots},
    {.kind = "tip", .label = "Tips", .one = "Tip", .slots = kTipSlots},
    {.kind = "control", .label = "Input", .one = "Input", .slots = kControlSlots},
};

const CommandForm* formOf(StoryEditor::Kind kind);

// Words of a list, split by `by`.
std::vector<std::string_view> wordsOf(std::string_view list, char by = ' ');
std::string spaced(std::string_view word); // "stop_music" -> "stop music"

// An expression's parts at its top level (outside brackets and quotes), split at `ops`: words ("and",
// whole words, in any case) or signs ("+"; a "-" only after a value, not a minus sign).
struct Parts {
    std::vector<std::string> parts, ops;
};

Parts splitTop(std::string_view text, std::initializer_list<std::string_view> ops);
std::string_view inside(std::string_view text);
std::string scripted(std::string_view value, const std::vector<std::string>& variables);

// A value as its box shows it: text without its quotes (double or single ones) when it would be written in
// quotes again; in quotes still when it would read as more ("yes" in quotes is the word, not the answer).
std::string shown(std::string_view value, const std::vector<std::string>& variables);
bool hasField(const std::vector<StoryEditor::Field>& fields, std::string_view name);

// A condition's fields: what's checked, how, and the value, for each check, with "Also" (and, or,
// nothing more) between them. `name` starts their names (they're "NAME", "NAME is", "NAME value",
// "NAME also", then "NAME 2"...), `label` is what the first is called and `hint` what it says empty.
// What's checked and the value can be random numbers.
void addCheckFields(std::vector<StoryEditor::Field>& out, const std::string& name, std::string_view label, std::string_view hint,
                    std::string_view condition, Box box, const std::vector<std::string>& variables);
std::string conditionFrom(const std::vector<StoryEditor::Field>& fields, const std::string& name, const std::vector<std::string>& variables);

// A list of actions as fields: for each, what it's about ("What it does", then "And then": one more,
// empty, to add another), its kind ("How": the group's kinds) and what that asks for, when it does. The
// kind it was read as is kept (a hidden field), so another kind picked starts without the last one's words.
void addActionFields(std::vector<StoryEditor::Field>& out, const std::string& name, std::string_view label, std::string_view hint,
                     std::string_view actions, const std::vector<std::string>& variables);
std::string actionsFrom(const std::vector<StoryEditor::Field>& fields, const std::string& name, const std::vector<std::string>& variables);

// Words without their text tags ({bold}, {pause}...; {name} stays: it's filled in).
constexpr std::string_view kTextTags[]{"bold", "italic", "underline", "strike", "color", "speed", "ruby", "tip", "pause", "nowait", "reveal"};

std::string withoutTags(std::string_view value);

// The folder a game folder is in (its pack, story.txt and spreadsheets go there).
std::string parentFolder(const std::string& root); // (root ends with a slash)

using Kind = StoryEditor::Kind;
using View = DeveloperTools::View;

// The Settings tab: config NAME, what the form calls it, its value without a config line, the words
// it's one of (else it's typed) and what it does. One without a name is a heading.
struct Setting {
    std::string_view name, label, standard, choices, help;
    bool quoted = false;      // written in quotes
    bool with = false;        // shares the row with the one before it (a title and its version, a shake's strength and time...)
    Box box = Box::None;      // its box's kind: what it lists, what its buttons say and how it's written, as the same box everywhere
    std::string_view suggest; // values it often has besides its kind's ("value:what it is")
};

constexpr std::array kSettings{
    Setting{.label = "Game"},
    Setting{.name = "title", .label = "Title", .standard = "VNext", .help = "The game's name: the window's title, and {title} on screens", .quoted = true, .box = Box::Text},
    Setting{.name = "version", .label = "Version", .standard = "1.0", .help = "{version} on screens", .quoted = true, .with = true, .box = Box::Text},
    Setting{.name = "save_id", .label = "Save folder name", .standard = "VNext", .help = "The players' folder for its saves and settings: every game needs its own", .quoted = true, .box = Box::Name},
    Setting{.name = "language", .label = "Language", .standard = "English", .help = "The language the scripts are written in, as Settings shows it", .quoted = true, .box = Box::Language},
    Setting{.name = "resolution", .label = "Game size", .standard = "1280 720", .help = "Width and height in pixels: the screens are laid out on it", .box = Box::WidthHeight},
    Setting{.name = "icon", .label = "Icon", .help = "The window's icon", .quoted = true, .box = Box::PictureFile},
    Setting{.name = "letterbox_color", .label = "Letterbox colour", .standard = "black", .help = "The colour around the game when the window isn't the same shape", .box = Box::Colour},
    Setting{.name = "resizable", .label = "Resizable window", .standard = "yes", .help = "Whether the window can be resized by dragging its edges, in the game and in the developer tools", .box = Box::YesNo},
    Setting{.name = "cursor", .label = "Mouse pointer", .help = "An image in images/ for the mouse pointer (empty: the system's)", .quoted = true, .box = Box::Picture},
    Setting{.name = "cursor_hotspot", .label = "Pointer's tip", .standard = "0 0", .help = "The pixel of that image that points", .with = true, .box = Box::Point},
    Setting{.label = "Saves"},
    Setting{.name = "autosave", .label = "Autosave", .standard = "yes", .help = "Save at every choice and when leaving a game", .box = Box::YesNo},
    Setting{.name = "save_pages", .label = "Save pages", .standard = "9", .help = "Numbered pages of save slots", .box = Box::Count},
    Setting{.name = "save_slots", .label = "Slots on a page", .standard = "6", .help = "Save slots on each page", .with = true, .box = Box::Count},
    Setting{.label = "Text"},
    Setting{.name = "reveal", .label = "Text appears", .standard = "letter", .choices = "letter word line", .help = "A letter, a word or a whole line at a time", .box = Box::Reveal},
    Setting{.name = "narrator_color", .label = "Narration colour", .standard = "#96aaff", .help = "The \"speaker\" colour of narration", .box = Box::Colour},
    Setting{.name = "history_limit", .label = "History length", .standard = "25", .help = "Lines kept for the History screen", .box = Box::Count},
    Setting{.name = "rollback", .label = "Rollback lines", .standard = "100", .help = "Lines and choices players can go back (0: none)", .with = true, .box = Box::Count},
    Setting{.name = "skip_delay", .label = "Skip speed", .standard = "0.05", .help = "Seconds each line shows while skipping", .box = Box::Seconds},
    Setting{.name = "skip_hold", .label = "Hold to skip", .standard = "ctrl", .choices = "ctrl shift alt none", .help = "The key that skips while it's held", .with = true, .box = Box::SkipHold},
    Setting{.name = "auto_per_char", .label = "Auto-forward per letter", .standard = "0.01", .help = "Extra seconds auto-forward waits for each letter of a line without a voice", .box = Box::Seconds},
    Setting{.name = "notify_time", .label = "Message time", .standard = "2.5", .help = "Seconds a notify message stays", .box = Box::Seconds},
    Setting{.label = "Images"},
    Setting{.name = "image_filter", .label = "Image scaling", .standard = "smooth", .choices = "smooth pixel", .help = "pixel: sharp pixels when images are scaled (pixel art)", .box = Box::ImageFilter},
    Setting{.name = "sprite_height", .label = "Character height", .standard = "95%", .help = "The tallest a character may be, of the screen's height", .box = Box::Percent},
    Setting{.name = "show_transition", .label = "Show transition", .standard = "dissolve 0.25", .help = "show without with: fade, dissolve, wipe, slide or none, and seconds", .box = Box::TimedTransition},
    Setting{.name = "hide_transition", .label = "Hide transition", .standard = "dissolve 0.25", .help = "The same for hide", .with = true, .box = Box::TimedTransition},
    Setting{.name = "fade_time", .label = "Fade time", .standard = "1", .help = "Seconds of with fade without a length", .box = Box::Seconds},
    Setting{.name = "dissolve_time", .label = "Dissolve time", .standard = "0.5", .help = "Seconds of with dissolve without a length", .with = true, .box = Box::Seconds},
    Setting{.name = "move_time", .label = "Move time", .standard = "0.4", .help = "Seconds someone on stage takes to slide to another place", .box = Box::Seconds},
    Setting{.name = "shake_strength", .label = "Shake strength", .standard = "12", .help = "How far shake moves the screen, in pixels", .box = Box::Pixels},
    Setting{.name = "shake_time", .label = "Shake time", .standard = "0.5", .help = "Seconds a shake lasts", .with = true, .box = Box::Seconds},
    Setting{.name = "flash_color", .label = "Flash colour", .standard = "white", .help = "The colour of flash", .box = Box::Colour},
    Setting{.name = "flash_time", .label = "Flash time", .standard = "0.4", .help = "Seconds a flash lasts", .with = true, .box = Box::Seconds},
    Setting{.name = "preload", .label = "Lines to load ahead", .standard = "30", .help = "Story lines looked ahead for images to load early (0: none)", .box = Box::Count},
    Setting{.name = "image_memory", .label = "Image memory", .standard = "512", .help = "MB of images kept loaded (0: no limit)", .with = true, .box = Box::Count, .suggest = "0:no limit|256|512|1024"},
    Setting{.label = "Screens"},
    Setting{.name = "glide", .label = "Lists glide after a drag", .standard = "yes", .help = "Lists and containers that scroll keep gliding when let go while moving (a part can say otherwise)", .box = Box::YesNo},
    Setting{.name = "glide_speed", .label = "Glide speed", .standard = "100%", .help = "How fast they glide, of the speed they were let go at", .box = Box::Percent},
    Setting{.name = "glide_distance", .label = "Glide distance", .standard = "100%", .help = "How far they carry: 200% slows down half as fast", .box = Box::Percent},
    Setting{.name = "scrollbar_size", .label = "Scrollbar thickness", .standard = "6", .help = "Pixels thick a container's scrollbar is (a part can say otherwise)", .box = Box::Pixels},
    Setting{.name = "fade_size", .label = "Fading edges", .standard = "0", .help = "Pixels a fading edge spans (a part can say otherwise)", .with = true, .box = Box::Pixels,
            .suggest = "0:a fifth of the box|32|64|96"},
    Setting{.label = "Audio"},
    Setting{.name = "music_crossfade", .label = "Music crossfade", .standard = "0.5", .help = "Seconds the old song fades out when the music changes", .box = Box::Seconds},
    Setting{.name = "music_while_speaking", .label = "Music under voices", .standard = "70%", .help = "How loud music plays while someone speaks (100%: no change)", .with = true, .box = Box::Volume},
    Setting{.name = "voice_sync", .label = "Text follows voices", .standard = "no", .help = "Voiced lines type out in time with the voice", .box = Box::YesNo},
    Setting{.name = "babble_speed", .label = "Made-up voice speed", .standard = "1", .help = "How fast made-up voices (babble=) talk", .box = Box::Speed},
    Setting{.name = "blip_every", .label = "Seconds between blips", .standard = "0.06", .help = "Seconds between blips (blip=)", .box = Box::Seconds},
    Setting{.name = "blip_volume", .label = "Blip volume", .standard = "60%", .help = "How loud blips (blip=) are", .with = true, .box = Box::Volume},
    Setting{.label = "Characters"},
    Setting{.name = "talk", .label = "Mouths move", .standard = "yes", .choices = "yes voice no", .help = "voice: only with a voice line (a character's own talk= still counts)", .box = Box::Talk},
    Setting{.name = "talk_speed", .label = "Mouth speed", .standard = "4", .help = "Mouth movements a second without a voice", .with = true, .box = Box::PerSecond},
    Setting{.name = "blink", .label = "Blinking", .standard = "yes", .help = "Whether everyone blinks (a character's own blink= still counts)", .box = Box::YesNo},
    Setting{.name = "blink_every", .label = "Seconds between blinks", .standard = "4", .help = "Seconds between blinks, on average", .with = true, .box = Box::Seconds},
    Setting{.name = "phone_typing", .label = "Phone typing speed", .standard = "6", .help = "Letters a second people type answers on the phone (0: at once)", .box = Box::TextSpeed},
    Setting{.label = "New players' settings"},
    Setting{.name = "default.fullscreen", .label = "Fullscreen", .standard = "no", .help = "Whether the game starts fullscreen", .box = Box::YesNo},
    Setting{.name = "default.scaling", .label = "Scaling", .standard = "fit", .choices = "fit integer stretch fill", .help = "How the game fills the window", .box = Box::WindowFill},
    Setting{.name = "default.vsync", .label = "VSync", .standard = "yes", .help = "Frames in step with the screen", .box = Box::YesNo},
    Setting{.name = "default.frame_limit", .label = "Frame limit", .standard = "0", .help = "Frames a second (0: no limit)", .with = true, .box = Box::Count, .suggest = "0:no limit|30|60|120|144"},
    Setting{.name = "default.text_speed", .label = "Text speed", .standard = "45", .help = "Letters a second (0: at once)", .box = Box::TextSpeed},
    Setting{.name = "default.auto_delay", .label = "Auto delay", .standard = "500", .help = "Milliseconds auto-forward waits after a line", .with = true, .box = Box::Count},
    Setting{.name = "default.textbox_opacity", .label = "Textbox opacity", .standard = "85%", .help = "How solid the textbox is", .box = Box::Opacity},
    Setting{.name = "default.text_size", .label = "Text size", .standard = "100%", .help = "For text that follows the setting", .with = true, .box = Box::Percent},
    Setting{.name = "default.transitions", .label = "Transitions", .standard = "yes", .help = "Fades and wipes between images", .box = Box::YesNo},
    Setting{.name = "default.master_volume", .label = "Volume", .standard = "100%", .help = "All audio", .box = Box::Volume},
    Setting{.name = "default.music_volume", .label = "Music volume", .standard = "80%", .help = "Music", .with = true, .box = Box::Volume},
    Setting{.name = "default.sound_volume", .label = "Sound volume", .standard = "100%", .help = "Sounds that aren't music or a voice", .box = Box::Volume},
    Setting{.name = "default.voice_volume", .label = "Voice volume", .standard = "100%", .help = "Voices", .with = true, .box = Box::Volume},
    Setting{.name = "default.babble", .label = "Made-up voices", .standard = "yes", .help = "Characters' babble= voices", .box = Box::YesNo},
    Setting{.name = "default.typing_sounds", .label = "Typing sounds", .standard = "yes", .help = "Characters' blips (blip=)", .with = true, .box = Box::YesNo},
    Setting{.name = "default.mouths", .label = "Mouths move", .standard = "always", .choices = "always voice never", .help = "voice: only while a voice plays", .box = Box::Mouths},
    Setting{.name = "default.skip_unread", .label = "Skip unread text", .standard = "no", .help = "Skipping goes past lines not read yet", .box = Box::YesNo},
    Setting{.name = "default.skip_after_choices", .label = "Skip after choices", .standard = "no", .help = "Skipping goes on after a choice", .with = true, .box = Box::YesNo},
    Setting{.name = "default.skip_movies", .label = "Skip movies", .standard = "yes", .help = "Clicks and skipping go past movies", .box = Box::YesNo},
    Setting{.name = "default.rollback", .label = "Rollback", .standard = "yes", .help = "The mouse wheel goes back to earlier lines", .box = Box::YesNo},
    Setting{.name = "default.read_aloud", .label = "Read aloud", .standard = "no", .help = "The computer reads the lines, choices and buttons pointed at aloud (on Linux: with speech-dispatcher)", .box = Box::YesNo},
    Setting{.name = "default.high_contrast", .label = "High contrast", .standard = "no", .help = "Screens in white on black, what's pointed at in yellow", .with = true, .box = Box::YesNo},
};

// `line` for a game of another size than 1280 x 720: its text sizes, padding and bars grown or shrunk to
// fit (places and shares fit any size as they are).
std::string scaled(const std::string& line, float sy);

// A screen's settings, and its parts', as their forms have them: the setting, what the form calls it,
// and what it lists or is one of (the first of `choices`: not written). `is`: 'a', what it does (its
// actions); 'c', a condition.
struct PartOption {
    std::string_view key, label;
    Box box = Box::None;
    std::string_view choices, labels, hint, suggest;
    char is = 0;
    std::string_view group; // the heading it starts in the form
    bool asStyle = false;   // a part's look: what it gets is its style's (its words say so, not "its base style")
};

constexpr PartOption kScreenOptions[]{
    {.key = "modal", .label = "Story while open", .box = Box::YesNo, .choices = "yes no", .labels = "Waits|Goes on", .group = "While open"},
    {.key = "keyboard", .label = "Arrow-key navigation", .box = Box::YesNoSettings, .choices = "usual yes no", .labels = "When the story waits|Yes|No"},
    {.key = "on_show", .label = "Action on open", .hint = "None", .is = 'a', .group = "Opening and closing"},
    {.key = "on_hide", .label = "Action on close", .hint = "None", .is = 'a'},
    {.key = "show_animation", .label = "Animation on open", .box = Box::Animation, .hint = "None"},
    {.key = "hide_animation", .label = "Animation on close", .box = Box::Animation, .hint = "None"},
    {.key = "stop_music_on_open", .label = "Stops music on open", .box = Box::YesNo, .choices = "no yes"},
    {.key = "stop_music_on_close", .label = "Stops music on close", .box = Box::YesNo, .choices = "no yes"},
    {.key = "stop_voice_on_open", .label = "Stops voice on open", .box = Box::YesNo, .choices = "no yes"},
    {.key = "stop_voice_on_close", .label = "Stops voice on close", .box = Box::YesNo, .choices = "no yes"},
    {.key = "close_after", .label = "Closes itself after (seconds)", .box = Box::Seconds, .hint = "None"},
    {.key = "keep", .label = "Keeps its line", .box = Box::YesNo, .choices = "no yes", .group = "As a textbox"},
    {.key = "text_speed", .label = "Text speed", .box = Box::Speed, .hint = "1"},
};

constexpr PartOption kPartOptions[]{
    {.key = "action", .label = "Action on click", .hint = "None", .is = 'a'},
    {.key = "confirm", .label = "Confirmation question", .box = Box::Text},
    {.key = "load_confirm", .label = "Question before loading mid-game", .box = Box::Text},
    {.key = "delete_confirm", .label = "Question before deleting", .box = Box::Text},
    {.key = "auto", .label = "Auto-save tab says", .box = Box::Text},
    {.key = "quick", .label = "Quick-save tab says", .box = Box::Text},
    {.key = "waiting", .label = "While waiting for a key", .box = Box::Text},
    {.key = "hidden", .label = "Secret entries say", .box = Box::Text},
    {.key = "key", .label = "Shortcut key", .box = Box::Key, .hint = "None"},
    {.key = "enabled", .label = "Clickable when", .box = Box::Condition, .hint = "Always", .is = 'c'},
    {.key = "selected", .label = "Chosen when", .box = Box::Condition, .hint = "Never", .is = 'c'},
    {.key = "drag", .label = "Drag name", .box = Box::Name},
    {.key = "drop", .label = "Action on drop", .hint = "None", .is = 'a'},
    {.key = "click_animation", .label = "Animation on click", .box = Box::Animation, .hint = "None"},
    {.key = "fit", .label = "Fit", .box = Box::Fit, .choices = "contain cover stretch"},
    {.key = "flip", .label = "Flipped", .box = Box::YesNo, .choices = "no yes"},
    {.key = "bar", .label = "Bar height", .box = Box::Pixels, .hint = "None"},
    {.key = "bar_color", .label = "Bar colour", .box = Box::Colour, .hint = "Speaker's colour"},
    {.key = "bob", .label = "Bob distance", .box = Box::Pixels, .hint = "3"},
    {.key = "bob_speed", .label = "Bob speed", .box = Box::PerSecond, .hint = "5"},
    {.key = "pause_image", .label = "Image at a pause", .box = Box::Picture, .hint = "Same image"},
    {.key = "anchor", .label = "Position", .box = Box::Anchor, .choices = "center top bottom"},
    {.key = "name_w", .label = "Name width", .box = Box::Pixels, .hint = "200"},
    {.key = "close_on_bottom", .label = "Closes when scrolled to the end", .box = Box::YesNo, .choices = "no yes"},
    {.key = "voice", .label = "Replay voice buttons", .box = Box::YesNo, .choices = "yes no"},
    {.key = "back", .label = "Go back buttons", .box = Box::YesNo, .choices = "yes no"},
    {.key = "wide_w", .label = "Auto and Quick tab width", .box = Box::Pixels, .hint = "76"},
    {.key = "mode", .label = "Mode", .box = Box::SaveMode, .hint = "As opened"},
    {.key = "delete", .label = "Delete buttons", .box = Box::YesNo, .choices = "yes no"},
    {.key = "track", .label = "Track colour", .box = Box::Colour, .hint = "Default"},
    {.key = "fill", .label = "Fill colour", .box = Box::Colour, .hint = "Default"},
    {.key = "knob", .label = "Knob colour", .box = Box::Colour, .hint = "Default"},
    {.key = "repeat", .label = "Repeat", .box = Box::Repeat, .choices = "no across down both"},
    {.key = "scroll", .label = "Scroll", .box = Box::Scroll, .choices = "no left right up down"},
    {.key = "scroll_time", .label = "Scroll round (seconds)", .box = Box::Seconds, .hint = "10"},
    {.key = "edges", .label = "Its edges", .box = Box::Edges, .choices = "show cut fade"},
    {.key = "scrollbar", .label = "Scrollbar", .box = Box::YesNo, .choices = "no yes"},
    {.key = "scrollbar_size", .label = "Scrollbar thickness", .box = Box::Pixels, .hint = "From Settings"},
    {.key = "fade_size", .label = "Fading edges", .box = Box::Pixels, .hint = "From Settings"},
    {.key = "glide", .label = "Glides on after a drag", .box = Box::YesNo, .choices = "yes no"},
    {.key = "glide_speed", .label = "Glide speed", .box = Box::Percent, .hint = "100%"},
    {.key = "glide_distance", .label = "Glide distance", .box = Box::Percent, .hint = "100%"},
    {.key = "slot", .label = "Key slot", .box = Box::KeySlot, .hint = "1"},
    {.key = "var", .label = "Variable", .box = Box::Variable},
    {.key = "setting", .label = "Player setting", .box = Box::Setting, .hint = "None"},
    {.key = "value", .label = "Value", .box = Box::Variable},
    {.key = "min", .label = "Minimum", .box = Box::Variable, .hint = "0"},
    {.key = "max", .label = "Maximum", .box = Box::Variable, .hint = "None"},
    {.key = "step", .label = "Step", .box = Box::Number, .hint = "1"},
    {.key = "hint", .label = "Placeholder text", .box = Box::Text, .hint = "None"},
    {.key = "vertical", .label = "Vertical", .box = Box::YesNo, .choices = "no yes"},
    {.key = "loop", .label = "Loop", .box = Box::YesNo, .choices = "yes no"},
    {.key = "volume", .label = "Volume", .box = Box::Volume, .hint = "100%"},
    {.key = "line", .label = "Line colour", .box = Box::Colour, .hint = "Default"},
    {.key = "title_style", .label = "Title style", .box = Box::Style, .hint = "Default"},
    {.key = "text_style", .label = "Text style", .box = Box::Style, .hint = "Default"},
    {.key = "name_style", .label = "Name style", .box = Box::Style, .hint = "Default"},
    {.key = "locked_style", .label = "Locked style", .box = Box::Style, .hint = "Default"},
    {.key = "seen_style", .label = "Picked-before style", .box = Box::Style, .hint = "Default"},
    {.key = "new_style", .label = "Unread style", .box = Box::Style, .hint = "Default"},
    {.key = "empty_style", .label = "Empty slot style", .box = Box::Style, .hint = "Default"},
    {.key = "choice_style", .label = "Choice style", .box = Box::Style, .hint = "Default"},
    {.key = "button_style", .label = "Button style", .box = Box::Style, .hint = "Default"},
    {.key = "chapter_style", .label = "Chapter style", .box = Box::Style, .hint = "Default"},
    {.key = "ending_style", .label = "Ending style", .box = Box::Style, .hint = "Default"},
    {.key = "stub_style", .label = "Unreached place style", .box = Box::Style, .hint = "Default"},
    {.key = "label_style", .label = "Label style", .box = Box::Style, .hint = "Default"},
    {.key = "mine_style", .label = "Player's message style", .box = Box::Style, .hint = "Default"},
    {.key = "reply_style", .label = "Answer style", .box = Box::Style, .hint = "Default"},
    {.key = "unread_style", .label = "Unread count style", .box = Box::Style, .hint = "Default"},
    {.key = "columns", .label = "Columns", .box = Box::Count, .hint = "1"},
    {.key = "item_w", .label = "Item width", .box = Box::Pixels, .hint = "Default"},
    {.key = "item_h", .label = "Item height", .box = Box::Pixels, .hint = "Default"},
    {.key = "gap", .label = "Spacing", .box = Box::Pixels, .hint = "Default"},
    {.key = "gap_x", .label = "Spacing across", .box = Box::Pixels, .hint = "Default"},
    {.key = "gap_y", .label = "Spacing down", .box = Box::Pixels, .hint = "Default"},
};

constexpr std::string_view kPictureBy = "?", kPictureBySuggest = "?:depends on a variable";

// In a form's fields: where an empty box means "from its style", the style's value instead (a field is
// named by its setting, or, a style's own form, by the setting's label).
void showStyleValues(std::vector<StoryEditor::Field>& fields, const Style& style, const Palette& palette);

// ...and in a story line's or a character's form: where an empty box means the setting in Settings
// (a move's time, a shake's strength, a transition's seconds, blinking...), that value.
void showConfigValues(std::vector<StoryEditor::Field>& fields, const GameConfig& config, const Palette& palette);

constexpr SDL_Color kPanel{16, 18, 28, 255}, kBar{30, 36, 56, 255}, kAccent{130, 200, 255, 255}, kButton{44, 52, 80, 255},
    kButtonHover{66, 82, 126, 255}, kEdge{92, 112, 168, 255}, kChosen{70, 92, 150, 255}, kBox{8, 9, 14, 255}, kGood{140, 230, 160, 255},
    kBad{255, 140, 150, 255}, kWarn{255, 205, 120, 255}, kOptionArrow{210, 150, 255, 255};

// The scene a mistake is in, if any.
std::optional<std::size_t> sceneOf(const StoryEditor& editor, const ScriptError& e);

// A mistake on a config line.
bool inSettings(const StoryEditor& editor, const ScriptError& e);

// Events as the game gets them, for the preview, Try it and the style sample to feed their screens.
SDL_Event mouseEventAt(Uint32 type, float x, float y, bool twice = false);
SDL_Event wheelEventAt(float turns, float x, float y);
SDL_Event textEvent(const char* text);
SDL_Event keyEvent(const SDL_KeyboardEvent& key);

// The Library's files: each kind's list, what a file's form calls one, its folder and its file types.
struct FileKind {
    std::string_view id, label, one, folder;
    std::span<const std::string_view> extensions;
};

constexpr FileKind kFileKinds[]{{"pictures", "Images", "Image", "images/", kImageTypes},
                                {"sounds", "Audio", "Audio", "audio/", kAudioTypes},
                                {"movies", "Movies", "Movie", "movies/", kVideoTypes},
                                {"fontfiles", "Fonts", "Font file", "fonts/", kFontTypes}};

// The clipboard's text as lines (without the blank ones at its end).
std::vector<std::string> clipboardLines();

// Which of Cmd+C (copy), X (cut), V (paste) and D (duplicate) a key is, if any.
char clipboardKey(const SDL_KeyboardEvent& key);

// The caret steps over whole letters (UTF-8).
std::size_t previousLetter(const std::string& s, std::size_t i);

// The world's language codes (ISO 639-1: what computers say, ja for Japanese), each with its name in
// English and, for the common ones, in itself; a few with a country after them.
struct Language {
    std::string_view code, name;
    std::string_view own = {}; // (its name in itself, for the common ones)
};

constexpr Language kLanguages[]{
    {"aa", "Afar"}, {"ab", "Abkhazian"}, {"ae", "Avestan"}, {"af", "Afrikaans"}, {"ak", "Akan"}, {"am", "Amharic"}, {"an", "Aragonese"},
    {"ar", "Arabic", "العربية"}, {"as", "Assamese"}, {"av", "Avaric"}, {"ay", "Aymara"}, {"az", "Azerbaijani"}, {"ba", "Bashkir"}, {"be", "Belarusian"},
    {"bg", "Bulgarian", "Български"}, {"bh", "Bihari"}, {"bi", "Bislama"}, {"bm", "Bambara"}, {"bn", "Bengali", "বাংলা"}, {"bo", "Tibetan"}, {"br", "Breton"},
    {"bs", "Bosnian"}, {"ca", "Catalan", "Català"}, {"ce", "Chechen"}, {"ch", "Chamorro"}, {"co", "Corsican"}, {"cr", "Cree"}, {"cs", "Czech", "Čeština"},
    {"cu", "Church Slavic"}, {"cv", "Chuvash"}, {"cy", "Welsh", "Cymraeg"}, {"da", "Danish", "Dansk"}, {"de", "German", "Deutsch"}, {"dv", "Divehi"},
    {"dz", "Dzongkha"}, {"ee", "Ewe"}, {"el", "Greek", "Ελληνικά"}, {"en", "English"}, {"en-GB", "English, United Kingdom"}, {"en-US", "English, United States"},
    {"eo", "Esperanto"}, {"es", "Spanish", "Español"}, {"es-MX", "Spanish, Mexico"}, {"et", "Estonian", "Eesti"}, {"eu", "Basque"}, {"fa", "Persian", "فارسی"},
    {"ff", "Fulah"}, {"fi", "Finnish", "Suomi"}, {"fj", "Fijian"}, {"fo", "Faroese"}, {"fr", "French", "Français"}, {"fr-CA", "French, Canada"},
    {"fy", "Western Frisian"}, {"ga", "Irish", "Gaeilge"}, {"gd", "Scottish Gaelic"}, {"gl", "Galician"}, {"gn", "Guarani"}, {"gu", "Gujarati"}, {"gv", "Manx"},
    {"ha", "Hausa"}, {"he", "Hebrew", "עברית"}, {"hi", "Hindi", "हिन्दी"}, {"ho", "Hiri Motu"}, {"hr", "Croatian", "Hrvatski"}, {"ht", "Haitian"},
    {"hu", "Hungarian", "Magyar"}, {"hy", "Armenian"}, {"hz", "Herero"}, {"ia", "Interlingua"}, {"id", "Indonesian", "Bahasa Indonesia"}, {"ie", "Interlingue"},
    {"ig", "Igbo"}, {"ii", "Sichuan Yi"}, {"ik", "Inupiaq"}, {"io", "Ido"}, {"is", "Icelandic", "Íslenska"}, {"it", "Italian", "Italiano"}, {"iu", "Inuktitut"},
    {"ja", "Japanese", "日本語"}, {"jv", "Javanese"}, {"ka", "Georgian"}, {"kg", "Kongo"}, {"ki", "Kikuyu"}, {"kj", "Kuanyama"}, {"kk", "Kazakh"},
    {"kl", "Kalaallisut"}, {"km", "Khmer"}, {"kn", "Kannada"}, {"ko", "Korean", "한국어"}, {"kr", "Kanuri"}, {"ks", "Kashmiri"}, {"ku", "Kurdish"}, {"kv", "Komi"},
    {"kw", "Cornish"}, {"ky", "Kyrgyz"}, {"la", "Latin"}, {"lb", "Luxembourgish"}, {"lg", "Ganda"}, {"li", "Limburgish"}, {"ln", "Lingala"}, {"lo", "Lao"},
    {"lt", "Lithuanian", "Lietuvių"}, {"lu", "Luba-Katanga"}, {"lv", "Latvian", "Latviešu"}, {"mg", "Malagasy"}, {"mh", "Marshallese"}, {"mi", "Maori"},
    {"mk", "Macedonian"}, {"ml", "Malayalam"}, {"mn", "Mongolian"}, {"mr", "Marathi"}, {"ms", "Malay", "Bahasa Melayu"}, {"mt", "Maltese"}, {"my", "Burmese"},
    {"na", "Nauru"}, {"nb", "Norwegian Bokmål"}, {"nd", "North Ndebele"}, {"ne", "Nepali"}, {"ng", "Ndonga"}, {"nl", "Dutch", "Nederlands"},
    {"nn", "Norwegian Nynorsk"}, {"no", "Norwegian", "Norsk"}, {"nr", "South Ndebele"}, {"nv", "Navajo"}, {"ny", "Chichewa"}, {"oc", "Occitan"}, {"oj", "Ojibwa"},
    {"om", "Oromo"}, {"or", "Oriya"}, {"os", "Ossetian"}, {"pa", "Punjabi"}, {"pi", "Pali"}, {"pl", "Polish", "Polski"}, {"ps", "Pashto"},
    {"pt", "Portuguese", "Português"}, {"pt-BR", "Portuguese, Brazil"}, {"qu", "Quechua"}, {"rm", "Romansh"}, {"rn", "Rundi"}, {"ro", "Romanian", "Română"},
    {"ru", "Russian", "Русский"}, {"rw", "Kinyarwanda"}, {"sa", "Sanskrit"}, {"sc", "Sardinian"}, {"sd", "Sindhi"}, {"se", "Northern Sami"}, {"sg", "Sango"},
    {"si", "Sinhala"}, {"sk", "Slovak", "Slovenčina"}, {"sl", "Slovenian", "Slovenščina"}, {"sm", "Samoan"}, {"sn", "Shona"}, {"so", "Somali"}, {"sq", "Albanian"},
    {"sr", "Serbian", "Српски"}, {"ss", "Swati"}, {"st", "Southern Sotho"}, {"su", "Sundanese"}, {"sv", "Swedish", "Svenska"}, {"sw", "Swahili", "Kiswahili"},
    {"ta", "Tamil", "தமிழ்"}, {"te", "Telugu"}, {"tg", "Tajik"}, {"th", "Thai", "ไทย"}, {"ti", "Tigrinya"}, {"tk", "Turkmen"}, {"tl", "Tagalog"}, {"tn", "Tswana"},
    {"to", "Tongan"}, {"tr", "Turkish", "Türkçe"}, {"ts", "Tsonga"}, {"tt", "Tatar"}, {"tw", "Twi"}, {"ty", "Tahitian"}, {"ug", "Uyghur"},
    {"uk", "Ukrainian", "Українська"}, {"ur", "Urdu"}, {"uz", "Uzbek"}, {"ve", "Venda"}, {"vi", "Vietnamese", "Tiếng Việt"}, {"vo", "Volapük"}, {"wa", "Walloon"},
    {"wo", "Wolof"}, {"xh", "Xhosa"}, {"yi", "Yiddish"}, {"yo", "Yoruba"}, {"za", "Zhuang"}, {"zh", "Chinese", "中文"}, {"zh-CN", "Chinese, Simplified"},
    {"zh-TW", "Chinese, Traditional"}, {"zu", "Zulu"}};

// A language's code from its name (in English or in itself; lower() leaves letters beyond ASCII as they
// are, so the names above are as usually written); empty for one that isn't listed.
std::string languageCode(std::string_view name);

// Spelling: the language a game's words are in, as the spell checker names it (config language;
// empty: the system's own), and the words of a text it doesn't know (where each is; what's in braces,
// and words with numbers in them, aren't checked, nor `names`). Each word is looked up once.
std::string spellingLanguage(std::string_view language);

// The words of `text` the dictionary doesn't have (not names, nor {tags}); `known`: what it said of each word before.
std::vector<std::pair<std::size_t, std::size_t>> misspelled(std::map<std::string, bool, std::less<>>& known, const std::string& text, const std::string& language,
                                                            const std::vector<std::string>& names);

} // namespace vn::dev

namespace vn {
using namespace dev; // (the developer files' own helpers: theirs, as if declared in each)
} // namespace vn
