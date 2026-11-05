#pragma once

// What the developer files (Developer*.cpp, StoryEditor.cpp) share, and only they include: the player
// (Engine.hpp: they all work on it), the tables their forms are made from, and the helpers more than one of
// them uses. They're in a namespace of
// their own (dev) that the developer files use as if each had declared them.

#include "Developer.hpp"
#include "Engine.hpp"

#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <span>

namespace vn::dev {

constexpr std::size_t npos = std::string_view::npos;
constexpr int kIndent = 4; // spaces for each level the editor adds

// For as long as it lives, `place` holds what `standIn` held (the Screens tab's sample story in the engine's
// place, its phone in the player's), and standIn what place held: swapped back when it goes, whatever happens.
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
    ToolsText name;       // (what the form calls it, without a label)
    std::string_view key;
    StoryEditor::Box box = StoryEditor::Box::None; // its box's kind (its pick comes from that)
    ToolsChoices choices;
    ToolsLabels labels;
    ToolsHint hint;
    std::string_view only; // written (and shown) only after these words
    bool seconds = false;
    ToolsSuggestions suggest; // values it often has ("|" between them, "value:what it is")
    bool commas = false;      // an Option that's several of its choices, with commas between them (all: not written)
    bool rich = false;        // text that can be bold, coloured...
    ToolsGroup group;         // the heading it starts in the form
    ToolsText label;          // what the form calls it (else its name)
};

struct CommandForm {
    StoryEditor::Kind kind;
    std::string_view verbs; // the words a line starts with ("_": a space)
    ToolsLabels labels;     // ...and what their buttons say
    std::span<const Slot> slots;
    ToolsText what = "Kind"; // what the form calls the choice of those words
};

using Type = Slot::Type;
using Box = StoryEditor::Box;
inline const Slot kColourSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                             {.type = Type::Word, .name = "Colour", .box = Box::Colour}};

inline const Slot kPlaceSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}, {.type = Type::Word, .name = "Place", .box = Box::Across}};

inline const Slot kFontSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                            {.type = Type::Word, .name = "File", .key = "quoted", .box = Box::FontFile},
                            {.type = Type::Word, .name = "Style", .box = Box::FontStyle, .hint = "Default"}};

// A style's settings (and a screen part's look).
inline const Slot kStyleSlots[]{
    {.type = Type::Word, .name = "Name", .box = Box::Name},
    {.type = Type::Option, .name = "Based on", .key = "based_on", .box = Box::Style, .hint = "default"},
    {.type = Type::Option, .name = "Font", .key = "font", .box = Box::Font, .hint = "From its base style", .group = "Text"},
    {.type = Type::Option, .name = "Size", .key = "size", .box = Box::Pixels, .hint = "From its base style", .label = "Font size"},
    {.type = Type::Option, .name = "Colour", .key = "colour", .box = Box::Colour, .hint = "From its base style", .suggest = "speaker:the speaker's colour", .label = "Text colour"},
    {.type = Type::Option, .name = "Across", .key = "align", .box = Box::Align, .choices = "inherit left centre right", .label = "Align"},
    {.type = Type::Option, .name = "Down", .key = "align_vertically", .box = Box::AlignVertically, .choices = "inherit top centre bottom", .label = "Align vertically"},
    {.type = Type::Option, .name = "Line spacing", .key = "line_spacing", .box = Box::LineSpacing, .hint = "From its base style"},
    {.type = Type::Option, .name = "Shadow", .key = "shadow", .box = Box::YesNoStyle, .choices = "inherit yes no"},
    {.type = Type::Option, .name = "Shadow's colour", .key = "shadow_colour", .box = Box::Colour, .hint = "From its base style", .label = "Shadow colour"},
    {.type = Type::Option, .name = "Shadow's offset", .key = "shadow_offset", .box = Box::Pixels, .hint = "From its base style", .label = "Shadow offset"},
    {.type = Type::Option, .name = "Background", .key = "background", .box = Box::Colour, .hint = "From its base style", .group = "Box", .label = "Background colour"},
    {.type = Type::Option, .name = "Border", .key = "border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Image", .key = "image", .box = Box::Image, .hint = "None"},
    {.type = Type::Option, .name = "Padding", .key = "padding", .box = Box::Pixels, .hint = "From its base style"},
    {.type = Type::Option, .name = "Opacity", .key = "opacity", .box = Box::Opacity, .hint = "From its base style"},
    {.type = Type::Option, .name = "Pointed at: colour", .key = "pointed_colour", .box = Box::Colour, .hint = "From its base style", .group = "Pointed at", .label = "Text colour"},
    {.type = Type::Option, .name = "Pointed at: background", .key = "pointed_background", .box = Box::Colour, .hint = "From its base style", .label = "Background colour"},
    {.type = Type::Option, .name = "Pointed at: border", .key = "pointed_border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Pointed at: image", .key = "pointed_image", .box = Box::Image, .hint = "From its base style", .label = "Image"},
    {.type = Type::Option, .name = "Chosen: colour", .key = "chosen_colour", .box = Box::Colour, .hint = "From its base style", .group = "Chosen", .label = "Text colour"},
    {.type = Type::Option, .name = "Chosen: background", .key = "chosen_background", .box = Box::Colour, .hint = "From its base style", .label = "Background colour"},
    {.type = Type::Option, .name = "Chosen: border", .key = "chosen_border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Chosen: image", .key = "chosen_image", .box = Box::Image, .hint = "From its base style", .label = "Image"},
    {.type = Type::Option, .name = "Unclickable: colour", .key = "unclickable_colour", .box = Box::Colour, .hint = "From its base style", .group = "Unclickable", .label = "Text colour"},
    {.type = Type::Option, .name = "Unclickable: background", .key = "unclickable_background", .box = Box::Colour, .hint = "From its base style", .label = "Background colour"},
    {.type = Type::Option, .name = "Unclickable: border", .key = "unclickable_border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Unclickable: image", .key = "unclickable_image", .box = Box::Image, .hint = "From its base style", .label = "Image"},
    {.type = Type::Option, .name = "Text size follows Settings", .key = "text_size_follows", .box = Box::YesNoStyle, .choices = "inherit yes no", .group = "Follows Settings", .label = "Text size"},
    {.type = Type::Option, .name = "Opacity follows Settings", .key = "opacity_follows", .box = Box::YesNoStyle, .choices = "inherit yes no", .label = "Opacity"},
    {.type = Type::Option, .name = "Sound when pointed at", .key = "pointed_sound", .box = Box::Sound, .hint = "From its base style", .group = "Sounds"},
    {.type = Type::Option, .name = "Sound when clicked", .key = "click_sound", .box = Box::Sound, .hint = "From its base style"}};

inline const Slot kAtlasSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                             {.type = Type::Option, .name = "Image", .key = "image", .box = Box::Image},
                             {.type = Type::Option, .name = "Frames in a row", .key = "frames_in_a_row", .box = Box::Count, .hint = "None (a row each)"},
                             {.type = Type::Option, .name = "Same names as", .key = "same_names_as", .box = Box::Record}};

inline const Slot kSubImageSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                                  {.type = Type::Option, .name = "Cut from", .key = "cut_from", .box = Box::Image},
                                  {.type = Type::Option, .name = "X", .key = "x", .box = Box::Pixels, .hint = "0"},
                                  {.type = Type::Option, .name = "Y", .key = "y", .box = Box::Pixels, .hint = "0"},
                                  {.type = Type::Option, .name = "Width", .key = "width", .box = Box::Pixels},
                                  {.type = Type::Option, .name = "Height", .key = "height", .box = Box::Pixels}};

inline const Slot kAnimationSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}}; // (how often it plays: StoryEditor::fields; what it does: the timeline)

// What an animation's lines change, a row each on the Library's timeline (StoryEditor::timelineRow): the
// keys written for it (a new change gets the first), what the row is called, its box, the values its
// box suggests besides its kind's, and what a new change on it goes to.
struct TimelineRow {
    std::string_view keys;
    ToolsText label;
    Box box;
    ToolsSuggestions more;
    std::string_view start;
};
inline const TimelineRow kTimelineRows[]{
    {"x left right", "X", Box::Across, "home:its own place|-2:just off the left|2:just off the right|5% left:a little to the left|5% right:a little to the right", "home"},
    {"y up down", "Y", Box::Down, "home:its own place|-2:just off the bottom|2:just off the top|5% up:a little up|5% down:a little down", "home"},
    {"opacity", "Opacity", Box::Opacity, "", "100%"},
    {"zoom", "Zoom", Box::Zoom, "", "100%"},
    {"rotation", "Rotation", Box::Rotation, "", "0"},
    {"colour", "Colour", Box::Colour, "", "white"},
    {"fade", "Fade to colour", Box::Colour, "none:back from it", "black"}, // (covered with it, the whole screen for a screen)
    {"wipe", "Wipe", Box::Wipe, "", "100%"}, // (how much of it shows: from the left, or as its shape has it)
};
static_assert(std::size(kTimelineRows) == StoryEditor::kTimelineRowCount);

inline const Slot kGallerySlots[]{{.type = Type::Word, .name = "Image", .box = Box::Image}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text}};

inline const Slot kEndingSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text},
                              {.type = Type::Option, .name = "Hint", .key = "hint", .box = Box::Text, .hint = "None"}};

inline const Slot kSongSlots[]{{.type = Type::Word, .name = "Song", .box = Box::Sound}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text}};

inline const Slot kAchievementSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text},
                                   {.type = Type::Word, .name = "Description", .key = "quoted", .box = Box::Text, .hint = "None"},
                                   {.type = Type::Flag, .name = "Secret until earned", .key = "secret", .box = Box::YesNo, .choices = "no yes"}};

inline const Slot kTipSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text},
                           {.type = Type::Word, .name = "Description", .key = "quoted", .box = Box::Text, .hint = "None"}};

inline const Slot kControlSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                               {.type = Type::Option, .name = "Keys", .key = "keys", .box = Box::Key, .hint = "None"},
                               {.type = Type::Option, .name = "Action", .key = "action", .box = Box::Action, .hint = "None"},
                               {.type = Type::Option, .name = "In menus", .key = "in_menus", .box = Box::YesNo, .choices = "no yes", .label = "Works in menus"},
                               {.type = Type::Option, .name = "Only when", .key = "when", .box = Box::Variable, .hint = "Always", .label = "Works when"}};

struct DefinitionKind {
    std::string_view kind;
    ToolsText label, one; // what the Library's list calls them, and one of them (its form's title)
    std::span<const Slot> slots;
};

inline const DefinitionKind kDefinitionKinds[]{
    {.kind = "colour", .label = "Colours", .one = "Colour", .slots = kColourSlots},
    {.kind = "style", .label = "Styles", .one = "Style", .slots = kStyleSlots},
    {.kind = "font", .label = "Font", .one = "Font", .slots = kFontSlots},
    {.kind = "place", .label = "Places", .one = "Place", .slots = kPlaceSlots},
    {.kind = "atlas", .label = "Atlases", .one = "Atlas", .slots = kAtlasSlots},
    {.kind = "sub-image", .label = "Sub-images", .one = "Sub-image", .slots = kSubImageSlots},
    {.kind = "animation", .label = "Animations", .one = "Animation", .slots = kAnimationSlots},
    {.kind = "gallery", .label = "Gallery", .one = "Gallery image", .slots = kGallerySlots},
    {.kind = "ending", .label = "Endings", .one = "Ending", .slots = kEndingSlots},
    {.kind = "song", .label = "Songs", .one = "Song", .slots = kSongSlots},
    {.kind = "achievement", .label = "Achievements", .one = "Achievement", .slots = kAchievementSlots},
    {.kind = "tip", .label = "Tips", .one = "Tip", .slots = kTipSlots},
    {.kind = "control", .label = "Controls", .one = "Control", .slots = kControlSlots},
};

const CommandForm* formOf(StoryEditor::Kind kind);

// Words of a list, split by `by`.
std::vector<std::string_view> wordsOf(std::string_view list, char by = ' ');
// A suggestion ("value:what it is") as its value and what it is (none: empty); a time's colon (8:00) is the value's.
std::pair<std::string_view, std::string_view> suggestionOf(std::string_view entry);
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
constexpr std::string_view kTextTags[]{"bold", "italic", "underline", "crossed_out", "colour", "speed", "furigana", "tip", "pause", "no_wait", "appears"};

std::string withoutTags(std::string_view value);

// The folder a game folder is in (its pack, story.txt and spreadsheets go there).
std::string parentFolder(const std::string& root); // (root ends with a slash)

using Kind = StoryEditor::Kind;
using View = DeveloperTools::View;

// The Settings tab: setting NAME, what the form calls it, its value without a setting line, the words
// it's one of (else it's typed) and what it does. One without a name is a heading.
struct Setting {
    std::string_view name;
    ToolsText label;
    std::string_view standard;
    ToolsChoices choices;
    ToolsText help;
    bool quoted = false;      // written in quotes
    Box box = Box::None;      // its box's kind: what it lists, what its buttons say and how it's written, as the same box everywhere
    ToolsSuggestions suggest; // values it often has besides its kind's ("value:what it is")
    ToolsHint hint;           // what its empty box means, when it has no value then (else None)
};

inline const std::array kSettings{
    Setting{.label = "Game"},
    Setting{.name = "title", .label = "Title", .standard = "VNext", .help = "The game's name: the window's title, and {title} on screens", .quoted = true, .box = Box::Text},
    Setting{.name = "version", .label = "Version", .standard = "1.0", .help = "{version} on screens", .quoted = true, .box = Box::Text},
    Setting{.name = "save_folder", .label = "Save folder name", .standard = "VNext", .help = "The players' folder for its saves and settings: every game needs its own", .quoted = true, .box = Box::Name},
    Setting{.name = "language", .label = "Language", .standard = "English", .help = "The language the scripts are written in, as Settings shows it", .quoted = true, .box = Box::Language},
    Setting{.name = "game_size", .label = "Game size", .standard = "1280 720", .help = "Width and height in pixels: the screens are laid out on it", .box = Box::WidthHeight},
    Setting{.name = "icon", .label = "Icon", .help = "The window's icon, and the packed game's", .quoted = true, .box = Box::ImageFile, .hint = "Default"},
    Setting{.name = "letterbox_colour", .label = "Letterbox colour", .standard = "black", .help = "The colour around the game when the window isn't the same shape", .box = Box::Colour},
    Setting{.name = "resizable", .label = "Resizable window", .standard = "yes", .help = "The window can be resized by dragging its edges, in the game and in the developer tools", .box = Box::YesNo},
    Setting{.name = "mouse_pointer", .label = "Mouse pointer", .help = "An image for the mouse pointer, in place of the system's", .quoted = true, .box = Box::Image, .hint = "Default"},
    Setting{.name = "pointer_tip", .label = "Pointer's tip", .standard = "0 0", .help = "The pixel of the pointer's image that points", .box = Box::Point},
    Setting{.label = "Saves"},
    Setting{.name = "autosave", .label = "Autosave", .standard = "yes", .help = "The game saves at every choice and when the player leaves it", .box = Box::YesNo},
    Setting{.name = "save_pages", .label = "Save pages", .standard = "9", .help = "Numbered pages of save slots", .box = Box::Count},
    Setting{.name = "page_slots", .label = "Slots on a page", .standard = "6", .help = "Save slots on each page", .box = Box::Count},
    Setting{.label = "Text"},
    Setting{.name = "text_appears", .label = "Text appears", .standard = "letter", .choices = "letter word line", .help = "A letter, a word or a whole line at a time", .box = Box::TextAppears},
    Setting{.name = "narration_colour", .label = "Narration colour", .standard = "#96aaff", .help = "The speaker's colour for narration, and for speakers who aren't characters", .box = Box::Colour},
    Setting{.name = "history_length", .label = "History length", .standard = "25", .help = "Latest lines kept for History parts to show", .box = Box::Count},
    Setting{.name = "rollback_lines", .label = "Rollback lines", .standard = "100", .help = "Lines and choices players can go back (0: none)", .box = Box::Count},
    Setting{.name = "skip_time", .label = "Skip time per line (seconds)", .standard = "0.05", .help = "How long each line shows while skipping", .box = Box::Seconds},
    Setting{.name = "hold_to_skip", .label = "Hold to skip", .standard = "ctrl", .choices = "ctrl shift alt none", .help = "The key that skips while it's held", .box = Box::HoldToSkip},
    Setting{.name = "auto_letter_delay", .label = "Auto delay per letter (seconds)", .standard = "0.01", .help = "How much longer auto-forward waits for each letter of a line without a voice", .box = Box::Seconds},
    Setting{.label = "Images"},
    Setting{.name = "image_scaling", .label = "Image scaling", .standard = "smooth", .choices = "smooth pixel", .help = "How scaled images look: smooth, or sharp pixels for pixel art", .box = Box::ImageScaling},
    Setting{.name = "character_height", .label = "Character height", .standard = "95%", .help = "The tallest a character may be, of the screen's height", .box = Box::Percent},
    Setting{.name = "animation_on_show", .label = "Animation on show", .help = "What characters and images play as they come, when their line names none", .box = Box::Animation, .hint = "At once"},
    Setting{.name = "animation_on_hide", .label = "Animation on hide", .help = "What characters and images play as they go, when their line names none", .box = Box::Animation, .hint = "At once"},
    Setting{.name = "move_time", .label = "Move time (seconds)", .standard = "0.4", .help = "How long someone on stage takes to slide to another place", .box = Box::Seconds},
    Setting{.name = "load_ahead", .label = "Lines to load ahead", .standard = "30", .help = "Story lines looked ahead for images to load early (0: none)", .box = Box::Count},
    Setting{.name = "image_memory", .label = "Image memory", .standard = "512", .help = "MB of images kept loaded (0: no limit)", .box = Box::Count, .suggest = "0:no limit|256|512|1024"},
    Setting{.label = "Screens"},
    Setting{.name = "glide", .label = "Lists glide after a drag", .standard = "yes", .help = "Lists and containers that scroll keep gliding when let go while moving (a part can say otherwise)", .box = Box::YesNo},
    Setting{.name = "glide_speed", .label = "Glide speed", .standard = "100%", .help = "How fast they glide, of the speed they were let go at", .box = Box::Percent},
    Setting{.name = "glide_distance", .label = "Glide distance", .standard = "100%", .help = "How far they carry: 200% slows down half as fast", .box = Box::Percent},
    Setting{.name = "scrollbar_thickness", .label = "Scrollbar thickness", .standard = "6", .help = "Pixels thick a scrollbar is (a part can say otherwise)", .box = Box::Pixels},
    Setting{.name = "fading_edges", .label = "Fading edges", .standard = "0", .help = "Pixels a fading edge spans (0: a fifth of the box, 64 at most; a container can say otherwise)", .box = Box::Pixels,
            .suggest = "0:a fifth of the box|32|64|96"},
    Setting{.label = "Sound"},
    Setting{.name = "music_crossfade", .label = "Music crossfade (seconds)", .standard = "0.5", .help = "How long the old song fades out when the music changes", .box = Box::Seconds},
    Setting{.name = "music_under_voices", .label = "Music under voices", .standard = "70%", .help = "How loud music plays while someone speaks (100%: no change)", .box = Box::Volume},
    Setting{.name = "text_follows_voices", .label = "Text follows voices", .standard = "no", .help = "Voiced lines type out in time with the voice", .box = Box::YesNo},
    Setting{.name = "made_up_voice_speed", .label = "Made-up voice speed", .standard = "1", .help = "How fast made-up voices talk", .box = Box::Speed},
    Setting{.name = "typing_sound_gap", .label = "Typing sound gap (seconds)", .standard = "0.06", .help = "How often a character's typing sound plays as their line appears", .box = Box::Seconds},
    Setting{.name = "typing_sound_volume", .label = "Typing sound volume", .standard = "60%", .help = "How loud characters' typing sounds are", .box = Box::Volume},
    Setting{.label = "Characters"},
    Setting{.name = "mouths_move", .label = "Mouths move", .standard = "yes", .choices = "yes voice no", .help = "When mouths move as characters speak (a character can say otherwise)", .box = Box::MouthMoves},
    Setting{.name = "mouth_speed", .label = "Mouth speed", .standard = "4", .help = "Mouth movements a second without a voice", .box = Box::PerSecond},
    Setting{.name = "blink", .label = "Blinks", .standard = "yes", .help = "Characters blink (a character can say otherwise)", .box = Box::YesNo},
    Setting{.name = "seconds_between_blinks", .label = "Seconds between blinks", .standard = "4", .help = "How often characters blink, on average", .box = Box::Seconds},
    Setting{.label = "Time"},
    Setting{.name = "time", .label = "Time", .standard = "none", .choices = "none story real days", .help = "The story's own clock and calendar, the player's real ones (the story waits for them), or days and parts of the day", .box = Box::TimeKind},
    Setting{.name = "time_start", .label = "Starts at", .help = "A date (2025-07-14) or a weekday, then a time or a part of the day: without either, days go by number", .box = Box::TimeStart, .hint = "8:00"},
    Setting{.name = "time_parts", .label = "Parts of the day", .standard = "morning 6:00, afternoon 12:00, evening 18:00, night 22:00", .help = "Each part of the day and when it starts: {time_part}, and time lines' words", .quoted = true, .box = Box::DayParts},
    Setting{.name = "clock", .label = "Clock", .standard = "24", .choices = "24 12", .help = "How times show: 18:30, or 6:30 PM", .box = Box::Clock},
    Setting{.name = "phone_follows_time", .label = "The phone follows it", .standard = "yes", .help = "The phone's clock and date, and the times on its messages and calls, are the game's time", .box = Box::YesNo},
    Setting{.name = "backgrounds_follow_time", .label = "Backgrounds follow it", .standard = "yes", .help = "A background's version for the part of the day shows when there is one: park_evening for park", .box = Box::YesNo},
    Setting{.label = "Phone"},
    Setting{.name = "has_phone", .label = "Has a phone when", .help = "The player has their phone while this is true (a variable)", .box = Box::Variable, .hint = "Always"},
    Setting{.name = "phone_owner", .label = "Owner", .help = "Whose phone it is, as it starts: the variable phone_owner (the player can change it in its Settings app)", .quoted = true, .box = Box::Text},
    Setting{.name = "phone_wallpaper", .label = "Home screen wallpaper", .help = "The image behind its apps, as it starts: the variable phone_wallpaper (the player can change it in its Settings app)", .box = Box::Image},
    Setting{.name = "phone_lock_wallpaper", .label = "Lock screen wallpaper", .help = "The image behind its lock screen, as it starts: the variable phone_lock_wallpaper (the player can change it in its Settings app)", .box = Box::Image},
    Setting{.name = "phone_passcode", .label = "Passcode", .help = "The code its lock screen asks for, as it starts: the variable phone_passcode (the player can change it in its Settings app)", .quoted = true, .box = Box::Text},
    Setting{.name = "phone_clock", .label = "Clock", .help = "The time it shows, as it starts: the variable phone_clock (none: the computer's clock; when it follows the time, the time's)", .quoted = true, .box = Box::Text, .hint = "The computer's"},
    Setting{.name = "phone_date", .label = "Date", .help = "The date its lock screen and chats show, as it starts: the variable phone_date (when it follows the time, the time's)", .quoted = true, .box = Box::Text},
    Setting{.name = "phone_signal", .label = "Signal", .standard = "yes", .help = "It has a signal, as it starts: the variable phone_signal (without one, calls don't get through and the player can't reply)", .box = Box::YesNo},
    Setting{.name = "phone_battery", .label = "Battery", .standard = "100", .help = "How full its battery is, 0 to 100, as it starts: the variable phone_battery (at 0 the phone is off)", .box = Box::Count},
    Setting{.name = "new_messages", .label = "New messages", .standard = "button", .choices = "button open", .help = "How players learn a message has come to their phone", .box = Box::NewMessages},
    Setting{.name = "chat_messages", .label = "Chat messages come", .standard = "typed", .choices = "typed click", .help = "How a chat scene's messages come: as they're typed, or each after a click", .box = Box::ChatMessages},
    Setting{.name = "phone_typing", .label = "Phone typing speed", .standard = "6", .help = "Letters a second people type messages on the phone (0: at once)", .box = Box::TextSpeed},
    Setting{.name = "phone_history", .label = "Messages in the history", .standard = "no", .help = "The phone's messages show in the history too", .box = Box::YesNo},
    Setting{.name = "phone_message_sound", .label = "Message sound", .help = "What plays when a message comes", .box = Box::Sound},
    Setting{.name = "phone_send_sound", .label = "Sending sound", .help = "What plays when the player sends one", .box = Box::Sound},
    Setting{.name = "phone_typing_sound", .label = "Typing sound", .help = "What plays when someone starts typing", .box = Box::Sound},
    Setting{.name = "phone_ringtone", .label = "Ringtone", .help = "What plays while a call comes (a character's own ringtone instead, if they have one)", .box = Box::Sound},
    Setting{.name = "phone_calling_sound", .label = "Calling sound", .help = "What plays while the player's call rings", .box = Box::Sound},
    Setting{.label = "New players' settings"},
    Setting{.name = "default.fullscreen", .label = "Fullscreen", .standard = "no", .help = "The game takes the whole screen", .box = Box::YesNo},
    Setting{.name = "default.scaling", .label = "Scaling", .standard = "fit", .choices = "fit integer stretch fill", .help = "How the game fills the window", .box = Box::WindowFill},
    Setting{.name = "default.vsync", .label = "VSync", .standard = "yes", .help = "Frames in step with the screen", .box = Box::YesNo},
    Setting{.name = "default.frame_limit", .label = "Frame limit", .standard = "0", .help = "Frames a second (0: no limit)", .box = Box::Count, .suggest = "0:no limit|30|60|120|144"},
    Setting{.name = "default.text_speed", .label = "Text speed", .standard = "45", .help = "Letters a second (0: at once)", .box = Box::TextSpeed},
    Setting{.name = "default.auto_delay", .label = "Auto delay (seconds)", .standard = "0.5", .help = "How long auto-forward waits after a line", .box = Box::Seconds},
    Setting{.name = "default.textbox_opacity", .label = "Textbox opacity", .standard = "85%", .help = "How solid the textbox is", .box = Box::Opacity},
    Setting{.name = "default.text_size", .label = "Text size", .standard = "100%", .help = "The size of text that follows this setting", .box = Box::Percent},
    Setting{.name = "default.animations", .label = "Animations", .standard = "yes", .help = "Characters, images and screens move, fade and wipe", .box = Box::YesNo},
    Setting{.name = "default.master_volume", .label = "Master volume", .standard = "100%", .help = "Music, sounds and voices", .box = Box::Volume},
    Setting{.name = "default.music_volume", .label = "Music volume", .standard = "80%", .help = "Music", .box = Box::Volume},
    Setting{.name = "default.sound_volume", .label = "Sound volume", .standard = "100%", .help = "Sounds that aren't music or a voice", .box = Box::Volume},
    Setting{.name = "default.voice_volume", .label = "Voice volume", .standard = "100%", .help = "Voices", .box = Box::Volume},
    Setting{.name = "default.made_up_voices", .label = "Made-up voices", .standard = "yes", .help = "Characters' made-up voices play", .box = Box::YesNo},
    Setting{.name = "default.typing_sounds", .label = "Typing sounds", .standard = "yes", .help = "Characters' typing sounds play", .box = Box::YesNo},
    Setting{.name = "default.mouths", .label = "Mouths move", .standard = "yes", .choices = "yes voice no", .help = "When mouths move as characters speak", .box = Box::Mouths},
    Setting{.name = "default.skip_unread", .label = "Skip unread text", .standard = "no", .help = "Skipping goes past lines not read yet", .box = Box::YesNo},
    Setting{.name = "default.skip_after_choices", .label = "Skip after choices", .standard = "no", .help = "Skipping goes on after a choice", .box = Box::YesNo},
    Setting{.name = "default.skip_movies", .label = "Skip movies", .standard = "yes", .help = "Clicks and skipping go past movies", .box = Box::YesNo},
    Setting{.name = "default.rollback", .label = "Rollback", .standard = "yes", .help = "The mouse wheel goes back to earlier lines", .box = Box::YesNo},
    Setting{.name = "default.read_aloud", .label = "Read aloud", .standard = "no", .help = "The computer reads the lines, choices and buttons pointed at aloud (on Linux: with speech-dispatcher)", .box = Box::YesNo},
    Setting{.name = "default.vibration", .label = "Vibration", .standard = "yes", .help = "Controllers vibrate when a message comes and while the phone rings", .box = Box::YesNo},
    Setting{.name = "default.high_contrast", .label = "High contrast", .standard = "no", .help = "Screens show white on black, with what's pointed at in yellow", .box = Box::YesNo},
};

// `line` for a game `sy` times as high: its text sizes, padding and bars grown or shrunk to fit (places
// and shares fit any size as they are).
std::string scaled(const std::string& line, float sy);

// A screen's settings, and its parts', as their forms have them: the setting, what the form calls it,
// and what it lists or is one of (the first of `choices`: not written). `is`: 'a', what it does (its
// actions); 'c', a condition.
struct PartOption {
    std::string_view key;
    ToolsText label;
    Box box = Box::None;
    ToolsChoices choices;
    ToolsLabels labels;
    ToolsHint hint;
    ToolsSuggestions suggest;
    char is = 0;
    ToolsGroup group; // the heading it starts in the form
};

inline const PartOption kScreenOptions[]{
    {.key = "story", .label = "Story while open", .box = Box::StoryWhileOpen, .choices = "waits goes_on", .group = "While open"},
    {.key = "action_on_open", .label = "Action on open", .box = Box::Action, .hint = "None", .is = 'a', .group = "Opening and closing"},
    {.key = "action_on_close", .label = "Action on close", .box = Box::Action, .hint = "None", .is = 'a'},
    {.key = "animation_on_open", .label = "Animation on open", .box = Box::Animation, .hint = "None"},
    {.key = "animation_on_close", .label = "Animation on close", .box = Box::Animation, .hint = "None"},
    {.key = "stop_music_on_open", .label = "Stops music on open", .box = Box::YesNo, .choices = "no yes"},
    {.key = "stop_music_on_close", .label = "Stops music on close", .box = Box::YesNo, .choices = "no yes"},
    {.key = "stop_voice_on_open", .label = "Stops voice on open", .box = Box::YesNo, .choices = "no yes"},
    {.key = "stop_voice_on_close", .label = "Stops voice on close", .box = Box::YesNo, .choices = "no yes"},
    {.key = "close_after", .label = "Closes itself after (seconds)", .box = Box::Seconds, .hint = "None"},
};

inline const PartOption kPartOptions[]{
    {.key = "action", .label = "Action on click", .box = Box::Action, .hint = "None", .is = 'a'},
    {.key = "confirm", .label = "Confirmation question", .box = Box::Text},
    {.key = "load_confirm", .label = "Confirm loading mid-game", .box = Box::Text},
    {.key = "delete_confirm", .label = "Confirm deleting", .box = Box::Text},
    {.key = "auto", .label = "Auto-save tab says", .box = Box::Text, .hint = "auto"},
    {.key = "quick", .label = "Quick-save tab says", .box = Box::Text, .hint = "quick"},
    {.key = "waiting", .label = "While waiting for a key", .box = Box::Text},
    {.key = "secret", .label = "Secret entries say", .box = Box::Text},
    {.key = "keys", .label = "Keys", .box = Box::Key, .hint = "None"},
    {.key = "clickable", .label = "Clickable when", .box = Box::Condition, .hint = "Always", .is = 'c'},
    {.key = "chosen", .label = "Chosen when", .box = Box::Condition, .hint = "Never", .is = 'c'},
    {.key = "drag", .label = "Drag name", .box = Box::Name},
    {.key = "drop", .label = "Action on drop", .box = Box::Action, .hint = "None", .is = 'a'},
    {.key = "click_animation", .label = "Animation on click", .box = Box::Animation, .hint = "None"},
    {.key = "fit", .label = "Fit", .box = Box::Fit, .choices = "contain cover stretch"},
    {.key = "flip", .label = "Flipped", .box = Box::YesNo, .choices = "no yes"},
    {.key = "bar", .label = "Bar height", .box = Box::Pixels, .hint = "None"},
    {.key = "bar_colour", .label = "Bar colour", .box = Box::Colour, .hint = "Speaker's colour"},
    {.key = "bob", .label = "Bob distance", .box = Box::Pixels, .hint = "3"},
    {.key = "bob_speed", .label = "Bob speed", .box = Box::PerSecond, .hint = "5"},
    {.key = "pause_image", .label = "Image at a pause", .box = Box::Image, .hint = "Same image"},
    {.key = "position", .label = "Position", .box = Box::Position, .choices = "centre top bottom"},
    {.key = "name_width", .label = "Name width", .box = Box::Pixels, .hint = "200"},
    {.key = "close_at_end", .label = "Closes when scrolled to the end", .box = Box::YesNo, .choices = "no yes"},
    {.key = "voice", .label = "Replay voice buttons", .box = Box::YesNo, .choices = "yes no"},
    {.key = "back", .label = "Go back buttons", .box = Box::YesNo, .choices = "yes no"},
    {.key = "wide_tab_width", .label = "Auto and Quick tab width", .box = Box::Pixels, .hint = "76"},
    {.key = "mode", .label = "Mode", .box = Box::SaveMode, .hint = "As opened"},
    {.key = "delete", .label = "Delete buttons", .box = Box::YesNo, .choices = "yes no"},
    {.key = "track", .label = "Track colour", .box = Box::Colour, .hint = "Default"},
    {.key = "fill", .label = "Fill colour", .box = Box::Colour, .hint = "Default"},
    {.key = "knob", .label = "Knob colour", .box = Box::Colour, .hint = "Default"},
    {.key = "repeat", .label = "Repeat", .box = Box::Repeat, .choices = "no across down both"},
    {.key = "scroll", .label = "Scroll", .box = Box::Scroll, .choices = "no left right up down"},
    {.key = "scroll_round", .label = "Scroll round (seconds)", .box = Box::Seconds, .hint = "10"},
    {.key = "edges", .label = "Its edges", .box = Box::Edges, .choices = "show cut fade"},
    {.key = "scrollbar", .label = "Scrollbar", .box = Box::YesNo, .choices = "no yes"},
    {.key = "scrollbar_thickness", .label = "Scrollbar thickness", .box = Box::Pixels, .hint = "From Settings"},
    {.key = "fading_edges", .label = "Fading edges", .box = Box::Pixels, .hint = "From Settings"},
    {.key = "glide", .label = "Glides on after a drag", .box = Box::YesNo, .choices = "yes no"},
    {.key = "glide_speed", .label = "Glide speed", .box = Box::Percent, .hint = "100%"},
    {.key = "glide_distance", .label = "Glide distance", .box = Box::Percent, .hint = "100%"},
    {.key = "slot", .label = "Key slot", .box = Box::KeySlot, .hint = "1"},
    {.key = "variable", .label = "Variable", .box = Box::Variable},
    {.key = "setting", .label = "Player setting", .box = Box::Setting, .hint = "None"},
    {.key = "value", .label = "Value", .box = Box::Variable},
    {.key = "minimum", .label = "Minimum", .box = Box::Variable, .hint = "0"},
    {.key = "maximum", .label = "Maximum", .box = Box::Variable, .hint = "100"},
    {.key = "full_at", .label = "Full at", .box = Box::Variable, .hint = "100"},
    {.key = "most_letters", .label = "Most letters", .box = Box::Variable, .hint = "64"},
    {.key = "step", .label = "Step", .box = Box::Number, .hint = "1"},
    {.key = "placeholder", .label = "Placeholder text", .box = Box::Text, .hint = "None"},
    {.key = "vertical", .label = "Vertical", .box = Box::YesNo, .choices = "no yes"},
    {.key = "loop", .label = "Loop", .box = Box::YesNo, .choices = "yes no"},
    {.key = "volume", .label = "Volume", .box = Box::Volume, .hint = "100%"},
    {.key = "line", .label = "Line colour", .box = Box::Colour, .hint = "Default"},
    {.key = "title_style", .label = "Title style", .box = Box::Style, .hint = "Default"},
    {.key = "text_style", .label = "Text style", .box = Box::Style, .hint = "Default"},
    {.key = "name_style", .label = "Name style", .box = Box::Style, .hint = "Default"},
    {.key = "locked_style", .label = "Locked style", .box = Box::Style, .hint = "Default"},
    {.key = "picked_before_style", .label = "Picked-before style", .box = Box::Style, .hint = "Default"},
    {.key = "empty_style", .label = "Empty slot style", .box = Box::Style, .hint = "Default"},
    {.key = "choice_style", .label = "Choice style", .box = Box::Style, .hint = "Default"},
    {.key = "button_style", .label = "Button style", .box = Box::Style, .hint = "Default"},
    {.key = "ending_style", .label = "Ending style", .box = Box::Style, .hint = "Default"},
    {.key = "unreached_style", .label = "Unreached style", .box = Box::Style, .hint = "Default"},
    {.key = "unread_style", .label = "Unread count style", .box = Box::Style, .hint = "Default"},
    {.key = "stack", .label = "Its parts go", .box = Box::Stack, .choices = "no down across"},
    {.key = "list", .label = "Once for each of", .box = Box::List, .hint = "None"},
    {.key = "item_animation", .label = "Animation on a new item", .box = Box::Animation, .hint = "None"},
    {.key = "app", .label = "The phone's app", .box = Box::Name, .hint = "None"},
    {.key = "title", .label = "Title", .box = Box::Text, .hint = "None"},
    {.key = "icon", .label = "Icon", .box = Box::Image, .hint = "None"},
    {.key = "width_at_most", .label = "Width at most", .box = Box::Number, .hint = "None"},
    {.key = "lines", .label = "Lines at most", .box = Box::Count, .hint = "None"},
    {.key = "count", .label = "Count on its icon", .box = Box::Variable, .hint = "New items"},
    {.key = "columns", .label = "Columns", .box = Box::Count, .hint = "1"},
    {.key = "item_width", .label = "Item width", .box = Box::Pixels, .hint = "Default"},
    {.key = "item_height", .label = "Item height", .box = Box::Pixels, .hint = "Default"},
    {.key = "spacing", .label = "Spacing", .box = Box::Pixels, .hint = "Default"},
    {.key = "spacing_across", .label = "Spacing across", .box = Box::Pixels, .hint = "Default"},
    {.key = "spacing_down", .label = "Spacing down", .box = Box::Pixels, .hint = "Default"},
};

constexpr std::string_view kImageBy = "?";
inline const ToolsSuggestions kImageBySuggest = "?:depends on a variable";

// In a form's fields: where an empty box means "from its style", the style's value instead (a field is
// named by its setting, or, a style's own form, by the setting's label).
void showStyleValues(std::vector<StoryEditor::Field>& fields, const Style& style, const Palette& palette);

// ...and in a story line's or a character's form: where an empty box means the setting in Settings
// (a move's time, the animation on show, blinking...), that value.
void showGameSettingValues(std::vector<StoryEditor::Field>& fields, const GameSettings& gameSettings);

// Screen lines written in a game's pixels (for `width` x `height`), written in places and shares instead.
// `sizes`: the size a part without one of its own has (a text: its words), by line, so it's put with its top
// left where the line has it (else as if it had none; Developer::ownSizes measures them).
std::vector<std::string> placesFromPixels(const std::vector<std::string>& lines, float width, float height, const std::map<std::size_t, SDL_FPoint>& sizes = {});

// Seconds as the editor writes them: at most two places, no zeros after them (0.5, 1.25, 2); and two
// times the same to a hundredth.
std::string secondsText(float seconds);
inline bool sameMoment(float a, float b) { return std::abs(a - b) < 0.005f; }

// An animation's change's usual easing, for what it changes (its key): a fade's is even, as a screen's fade is.
inline std::string_view usualEase(std::string_view key) { return key == "fade" ? "linear" : "inout"; }

// The tools' look: VNext's own indigo (its icon's, from #5a4dde to #231b6e, on its banner's #110e31), with
// VNext's text colours (theme::text and theme::dim: the ones its screens use too).
constexpr SDL_Color kPanel{20, 18, 48, 255}, kBar{31, 26, 76, 255}, kAccent{173, 162, 255, 255}, kButton{43, 37, 99, 255},
    kButtonHover{59, 51, 146, 255}, kEdge{91, 81, 184, 255}, kChosen{77, 65, 196, 255}, kBox{12, 10, 34, 255}, kGood{140, 230, 160, 255},
    kBad{255, 140, 150, 255}, kWarn{255, 205, 120, 255}, kOptionArrow{210, 150, 255, 255};

// The scene a mistake is in, if any.
std::optional<std::size_t> sceneOf(const StoryEditor& editor, const ScriptError& e);

// A mistake on a setting line.
bool inSettings(const StoryEditor& editor, const ScriptError& e);

// The mouse moving to a place (in the game's pixels), as the game gets it: the Library's style sample is pointed at with it.
SDL_Event mouseMotionAt(float x, float y);

// The Library's files: each kind's list, what a file's form calls one, its folder and its file types.
struct FileKind {
    std::string_view id;
    ToolsText label, one;
    std::string_view folder;
    std::span<const std::string_view> extensions;
};

inline const FileKind kFileKinds[]{{"images", "Images", "Image", "images/", kImageTypes},
                                {"sounds", "Sounds", "Sound", "sounds/", kSoundTypes},
                                {"movies", "Movies", "Movie", "movies/", kMovieTypes},
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

// Spelling: the language a game's words are in, as the spell checker names it (setting language;
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
