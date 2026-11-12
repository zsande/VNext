#pragma once

// For the developer tools' files only: what the forms are made from (each kind of line's slots, the Library's
// definitions and settings, screen parts' options) and the values, conditions and actions in them.

#include "ScriptEdits.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

namespace dev {

std::string fieldValue(const std::vector<StoryEditor::Field>& fields, std::string_view name);

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
    ToolsSuggestions suggest; // values it often has ("|" between them)
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

// A visual type of the game's own: its name, the layer its visuals are drawn at (higher: in front) and what it takes.
inline const Slot kTypeSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                            {.type = Type::Option, .name = "Layer", .key = "layer", .box = Box::Layer, .hint = "0"},
                            {.type = Type::Option, .name = "Takes", .key = "accepts", .box = Box::Accepts, .choices = "character image movie colour", .commas = true}};

// A visual with a name of its own: its type (else an image), what it shows (a picture, a sub-image, a movie or a colour;
// else the one named as it), how it comes when its line doesn't say (where: a background's, the part of a picture bigger
// than the stage that shows; its layer and look, its animations, a movie's playing) and, a colour's box, its size (a part
// of the stage's; none: all of it).
inline const Slot kVisualSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                              {.type = Type::Option, .name = "Type", .key = "type", .box = Box::VisualType, .hint = "image"},
                              {.type = Type::Option, .name = "Shows", .key = "shows", .box = Box::Image, .label = "Visual"},
                              {.type = Type::Option, .name = "Place", .key = "at", .box = Box::Place, .hint = "centre", .group = "Where", .label = "Across"},
                              {.type = Type::Option, .name = "Y", .key = "y", .box = Box::Down, .hint = "0", .label = "Up and down"},
                              {.type = Type::Option, .name = "Layer", .key = "layer", .box = Box::Layer},
                              {.type = Type::Option, .name = "Zoom", .key = "zoom", .box = Box::Zoom, .hint = "100%", .group = "Look"},
                              {.type = Type::Option, .name = "Opacity", .key = "opacity", .box = Box::Opacity, .hint = "100%"},
                              {.type = Type::Option, .name = "Rotation", .key = "rotation", .box = Box::Rotation, .hint = "0", .label = "Rotation (degrees)"},
                              {.type = Type::Option, .name = "Flip", .key = "flip", .box = Box::YesNo, .choices = "no yes", .label = "Mirrored"},
                              {.type = Type::Option, .name = "Tint", .key = "tint", .box = Box::Colour, .hint = "None", .label = "Tint colour"},
                              {.type = Type::Option, .name = "Animation", .key = "animation", .box = Box::Animation, .hint = "None", .suggest = "none", .group = "Animations", .label = "Animation while shown"},
                              {.type = Type::Option, .name = "Animation on show", .key = "animation_on_show", .box = Box::Animation, .hint = "From Settings", .suggest = "none"},
                              {.type = Type::Option, .name = "Loop", .key = "loop", .box = Box::YesNo, .choices = "yes no", .group = "Playing", .label = "Loops"},
                              {.type = Type::Option, .name = "Volume", .key = "volume", .box = Box::Volume, .hint = "100%"},
                              {.type = Type::Option, .name = "Width", .key = "width", .box = Box::Number, .hint = "1", .group = "Size"},
                              {.type = Type::Option, .name = "Height", .key = "height", .box = Box::Number, .hint = "1"}};

// A sound type of the game's own: its name, the player's volume it follows, whether one of it stops the one before (one
// at a time), whether its sounds loop unless they say, and a volume of its own (its sounds' times it). (Its form's rows:
// StoryEditor::fields, as its defaults come from the volume it follows.)
inline const Slot kSoundTypeSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                                 {.type = Type::Option, .name = "Follows", .key = "follows", .box = Box::Follows, .label = "Volume it follows"},
                                 {.type = Type::Option, .name = "One at a time", .key = "one_at_a_time", .box = Box::YesNo},
                                 {.type = Type::Option, .name = "Loop", .key = "loop", .box = Box::YesNo, .label = "Loops"},
                                 {.type = Type::Option, .name = "Volume", .key = "volume", .box = Box::Volume, .hint = "100%"}};

// A sound with a name of its own: its type (else sound), the file it plays (else the one named as it) and how it plays when
// its line doesn't say (its form's rows: StoryEditor::fields, as its defaults come from its type).
inline const Slot kNamedSoundSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                                  {.type = Type::Option, .name = "Type", .key = "type", .box = Box::SoundType},
                                  {.type = Type::Option, .name = "Sound", .key = "plays", .box = Box::Sound},
                                  {.type = Type::Option, .name = "Loop", .key = "loop", .box = Box::YesNo, .label = "Loops"},
                                  {.type = Type::Option, .name = "Volume", .key = "volume", .box = Box::Volume},
                                  {.type = Type::Option, .name = "Fade in", .key = "fade", .box = Box::Seconds},
                                  {.type = Type::Option, .name = "Fade out", .key = "fade_out", .box = Box::Seconds}};

inline const Slot kFontSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                            {.type = Type::Word, .name = "File", .key = "quoted", .box = Box::FontFile},
                            {.type = Type::Word, .name = "Style", .box = Box::FontStyle, .hint = "Default", .label = "Font style"}};

// A style's settings (and a screen part's look).
inline const Slot kStyleSlots[]{
    {.type = Type::Word, .name = "Name", .box = Box::Name},
    {.type = Type::Option, .name = "Based on", .key = "based_on", .box = Box::Style, .hint = "default"},
    {.type = Type::Option, .name = "Font", .key = "font", .box = Box::Font, .hint = "From its base style", .group = "Text"},
    {.type = Type::Option, .name = "Size", .key = "size", .box = Box::Pixels, .hint = "From its base style", .label = "Font size"},
    {.type = Type::Option, .name = "Colour", .key = "colour", .box = Box::Colour, .hint = "From its base style", .suggest = "speaker", .label = "Text colour"},
    {.type = Type::Option, .name = "Across", .key = "align", .box = Box::Align, .choices = "inherit left centre right", .label = "Align across"},
    {.type = Type::Option, .name = "Down", .key = "align_vertically", .box = Box::AlignVertically, .choices = "inherit top centre bottom", .label = "Align up and down"},
    {.type = Type::Option, .name = "Line spacing", .key = "line_spacing", .box = Box::LineSpacing, .hint = "From its base style"},
    {.type = Type::Option, .name = "Shadow", .key = "shadow", .box = Box::YesNoStyle, .choices = "inherit yes no"},
    {.type = Type::Option, .name = "Shadow's colour", .key = "shadow_colour", .box = Box::Colour, .hint = "From its base style", .label = "Shadow colour"},
    {.type = Type::Option, .name = "Shadow's offset", .key = "shadow_offset", .box = Box::Pixels, .hint = "From its base style", .label = "Shadow offset (pixels)"},
    {.type = Type::Option, .name = "Background", .key = "background", .box = Box::Colour, .hint = "From its base style", .group = "Box", .label = "Background colour"},
    {.type = Type::Option, .name = "Border", .key = "border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Image", .key = "image", .box = Box::Image, .hint = "None", .label = "Background visual"},
    {.type = Type::Option, .name = "Padding", .key = "padding", .box = Box::Pixels, .hint = "From its base style", .label = "Padding (pixels)"},
    {.type = Type::Option, .name = "Opacity", .key = "opacity", .box = Box::Opacity, .hint = "From its base style"},
    {.type = Type::Option, .name = "Pointed at: colour", .key = "pointed_colour", .box = Box::Colour, .hint = "From its base style", .group = "Pointed at", .label = "Text colour"},
    {.type = Type::Option, .name = "Pointed at: background", .key = "pointed_background", .box = Box::Colour, .hint = "From its base style", .label = "Background colour"},
    {.type = Type::Option, .name = "Pointed at: border", .key = "pointed_border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Pointed at: image", .key = "pointed_image", .box = Box::Image, .hint = "From its base style", .label = "Background visual"},
    {.type = Type::Option, .name = "Chosen: colour", .key = "chosen_colour", .box = Box::Colour, .hint = "From its base style", .group = "Chosen", .label = "Text colour"},
    {.type = Type::Option, .name = "Chosen: background", .key = "chosen_background", .box = Box::Colour, .hint = "From its base style", .label = "Background colour"},
    {.type = Type::Option, .name = "Chosen: border", .key = "chosen_border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Chosen: image", .key = "chosen_image", .box = Box::Image, .hint = "From its base style", .label = "Background visual"},
    {.type = Type::Option, .name = "Unclickable: colour", .key = "unclickable_colour", .box = Box::Colour, .hint = "From its base style", .group = "Unclickable", .label = "Text colour"},
    {.type = Type::Option, .name = "Unclickable: background", .key = "unclickable_background", .box = Box::Colour, .hint = "From its base style", .label = "Background colour"},
    {.type = Type::Option, .name = "Unclickable: border", .key = "unclickable_border", .box = Box::Colour, .hint = "From its base style", .label = "Border colour"},
    {.type = Type::Option, .name = "Unclickable: image", .key = "unclickable_image", .box = Box::Image, .hint = "From its base style", .label = "Background visual"},
    {.type = Type::Option, .name = "Text size follows Settings", .key = "text_size_follows", .box = Box::YesNoStyle, .choices = "inherit yes no", .group = "Follows Settings", .label = "Text size"},
    {.type = Type::Option, .name = "Opacity follows Settings", .key = "opacity_follows", .box = Box::YesNoStyle, .choices = "inherit yes no", .label = "Opacity"},
    {.type = Type::Option, .name = "Sound when pointed at", .key = "pointed_sound", .box = Box::Sound, .hint = "From its base style", .group = "Sounds"},
    {.type = Type::Option, .name = "Sound on click", .key = "click_sound", .box = Box::Sound, .hint = "From its base style"}};

inline const Slot kAtlasSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                             {.type = Type::Option, .name = "Image", .key = "image", .box = Box::Image, .label = "Sheet"},
                             {.type = Type::Option, .name = "Frames in a row", .key = "frames_in_a_row", .box = Box::Count, .hint = "None (a row each)"},
                             {.type = Type::Option, .name = "Same names as", .key = "same_names_as", .box = Box::Record, .label = "Same frame names as"}};

inline const Slot kSubImageSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name},
                                  {.type = Type::Option, .name = "Cut from", .key = "cut_from", .box = Box::Image},
                                  {.type = Type::Option, .name = "X", .key = "x", .box = Box::Pixels, .hint = "0", .label = "From the left (pixels)"},
                                  {.type = Type::Option, .name = "Y", .key = "y", .box = Box::Pixels, .hint = "0", .label = "From the top (pixels)"},
                                  {.type = Type::Option, .name = "Width", .key = "width", .box = Box::Pixels, .label = "Width (pixels)"},
                                  {.type = Type::Option, .name = "Height", .key = "height", .box = Box::Pixels, .label = "Height (pixels)"}};

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
    {"x left right", "Across", Box::Across, "home:its own place|-2:just off the left|2:just off the right|5% left:a little to the left|5% right:a little to the right", "home"},
    {"y up down", "Up and down", Box::Down, "home:its own place|-2:just off the bottom|2:just off the top|5% up:a little up|5% down:a little down", "home"},
    {"opacity", "Opacity", Box::Opacity, "", "100%"},
    {"zoom", "Zoom", Box::Zoom, "", "100%"},
    {"rotation", "Rotation (degrees)", Box::Rotation, "", "0"},
    {"colour", "Colour", Box::Colour, "", "white"},
    {"fade", "Fade to colour", Box::Colour, "none:back from it", "black"}, // (covered with it, the whole screen for a screen)
    {"wipe", "Revealed", Box::Wipe, "", "100%"}, // (how much of it shows: from the left, or as its shape has it)
};
static_assert(std::size(kTimelineRows) == StoryEditor::kTimelineRowCount);

inline const Slot kGallerySlots[]{{.type = Type::Word, .name = "Image", .box = Box::Image, .label = "Visual"}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text}};

inline const Slot kEndingSlots[]{{.type = Type::Word, .name = "Name", .box = Box::Name}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text},
                              {.type = Type::Option, .name = "Hint", .key = "hint", .box = Box::Text, .hint = "None"},
                              {.type = Type::Option, .name = "Scene", .key = "scene", .box = Box::Scene, .hint = "None", .label = "Reached as this scene ends"}};

inline const Slot kSongSlots[]{{.type = Type::Word, .name = "Song", .box = Box::Sound}, {.type = Type::Word, .name = "Title", .key = "quoted", .box = Box::Text}};

inline const Slot kCaptionSlots[]{{.type = Type::Word, .name = "Sound", .box = Box::Sound}, {.type = Type::Word, .name = "Caption", .key = "quoted", .box = Box::Text, .rich = true}};

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
    {.kind = "type", .label = "Visual types", .one = "Visual type", .slots = kTypeSlots},
    {.kind = "visual", .label = "Named visuals", .one = "Visual", .slots = kVisualSlots},
    {.kind = "sound type", .label = "Sound types", .one = "Sound type", .slots = kSoundTypeSlots},
    {.kind = "sound", .label = "Named sounds", .one = "Sound", .slots = kNamedSoundSlots},
    {.kind = "atlas", .label = "Atlases", .one = "Atlas", .slots = kAtlasSlots},
    {.kind = "sub-image", .label = "Sub-images", .one = "Sub-image", .slots = kSubImageSlots},
    {.kind = "animation", .label = "Animations", .one = "Animation", .slots = kAnimationSlots},
    {.kind = "gallery", .label = "Gallery", .one = "Gallery visual", .slots = kGallerySlots},
    {.kind = "ending", .label = "Endings", .one = "Ending", .slots = kEndingSlots},
    {.kind = "song", .label = "Songs", .one = "Song", .slots = kSongSlots},
    {.kind = "caption", .label = "Captions", .one = "Caption", .slots = kCaptionSlots},
    {.kind = "achievement", .label = "Achievements", .one = "Achievement", .slots = kAchievementSlots},
    {.kind = "tip", .label = "Tips", .one = "Tip", .slots = kTipSlots},
    {.kind = "control", .label = "Controls", .one = "Control", .slots = kControlSlots},
};

const CommandForm* formOf(StoryEditor::Kind kind);

// Words of a list, split by `by`.
std::vector<std::string_view> wordsOf(std::string_view list, char by = ' ');
std::string spaced(std::string_view word); // "stop_weather" -> "stop weather"

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
                     std::string_view actions, const StoryEditor& editor);
std::string actionsFrom(const std::vector<StoryEditor::Field>& fields, const std::string& name, const std::vector<std::string>& variables);

using Kind = StoryEditor::Kind;
using Page = DeveloperTools::Page;

// The Library's Settings: setting NAME, what the form calls it, its value without a setting line, and the
// words it's one of (else it's typed). One without a name is a heading. The players' own settings (default.NAME: what
// a new player starts with, changed in the game's Settings) are in the groups they belong to, as the ready-made
// Settings screen has them (its Accessibility group too).
struct Setting {
    std::string_view name;
    ToolsText label;
    std::string_view standard;
    ToolsChoices choices;
    bool quoted = false;      // written in quotes
    Box box = Box::None;      // its box's kind: what it lists, what its buttons say and how it's written, as the same box everywhere
    ToolsSuggestions suggest; // values it always can be besides its kind's (never examples of one)
    ToolsHint hint;           // what its empty box means, when it has no value then (else None)
    int part = 0;             // a part of its value in a row of its own (1, 2: a size's width and height, a start's day and time of day); 0: all of it
};

inline const std::array kSettings{
    Setting{.label = "Game"},
    Setting{.name = "title", .label = "Title", .standard = "VNext", .quoted = true, .box = Box::Text},
    Setting{.name = "version", .label = "Version", .standard = "1.0", .quoted = true, .box = Box::Text},
    Setting{.name = "save_folder", .label = "Save folder name", .quoted = true, .box = Box::Name}, // (empty: the title's, saveFolderNameOf)
    Setting{.name = "language", .label = "Written in", .standard = "English", .quoted = true, .box = Box::Language},
    Setting{.name = "game_size", .label = "Game width (pixels)", .standard = "1280", .box = Box::Pixels, .part = 1},
    Setting{.name = "game_size", .label = "Game height (pixels)", .standard = "720", .box = Box::Pixels, .part = 2},
    Setting{.name = "icon", .label = "App icon", .quoted = true, .box = Box::ImageFile, .hint = "Default"},
    Setting{.name = "letterbox_colour", .label = "Letterbox colour", .standard = "black", .box = Box::Colour},
    Setting{.name = "resizable", .label = "Resizable window", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "default.fullscreen", .label = "Fullscreen", .standard = "no", .box = Box::YesNo},
    Setting{.name = "default.scaling", .label = "Window scaling", .standard = "fit", .choices = "fit integer stretch fill", .box = Box::WindowFill},
    Setting{.name = "default.vsync", .label = "VSync", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "default.frame_limit", .label = "Frames a second at most", .box = Box::Count, .hint = "No limit"}, // (0, as none written: no limit)
    Setting{.name = "mouse_pointer", .label = "Mouse pointer", .quoted = true, .box = Box::Image, .hint = "Default"},
    Setting{.name = "pointer_tip", .label = "Pointer's tip from the left (pixels)", .standard = "0", .box = Box::Pixels, .part = 1},
    Setting{.name = "pointer_tip", .label = "Pointer's tip from the top (pixels)", .standard = "0", .box = Box::Pixels, .part = 2},
    Setting{.label = "Saves"},
    Setting{.name = "autosave", .label = "Autosave", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "save_pages", .label = "Save pages", .standard = "9", .box = Box::Count},
    Setting{.name = "page_slots", .label = "Slots on a page", .standard = "6", .box = Box::Count},
    Setting{.label = "Text"},
    Setting{.name = "text_appears", .label = "Text appears", .standard = "letter", .choices = "letter word line", .box = Box::TextAppears},
    Setting{.name = "default.text_speed", .label = "Text speed (letters a second)", .standard = "45", .box = Box::TextSpeed},
    Setting{.name = "default.auto_delay", .label = "Auto waits after a line (seconds)", .standard = "0.5", .box = Box::Seconds},
    Setting{.name = "auto_letter_delay", .label = "Auto waits per letter (seconds)", .standard = "0.01", .box = Box::Seconds},
    Setting{.name = "default.textbox_opacity", .label = "Textbox opacity", .standard = "85%", .box = Box::Opacity},
    Setting{.name = "narration_colour", .label = "Narration colour", .standard = "#96aaff", .box = Box::Colour},
    Setting{.name = "history_length", .label = "History length (lines)", .standard = "25", .box = Box::Count},
    Setting{.name = "default.rollback", .label = "Rollback", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "rollback_lines", .label = "Lines rolled back at most", .standard = "100", .box = Box::Count},
    Setting{.name = "default.skip_unread", .label = "Skip unread text", .standard = "no", .box = Box::YesNo},
    Setting{.name = "default.skip_after_choices", .label = "Skip after choices", .standard = "no", .box = Box::YesNo},
    Setting{.name = "default.skip_movies", .label = "Skip movies", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "skip_time", .label = "Skip time per line (seconds)", .standard = "0.05", .box = Box::Seconds},
    Setting{.name = "hold_to_skip", .label = "Hold to skip", .standard = "ctrl", .choices = "ctrl shift alt none", .box = Box::HoldToSkip},
    Setting{.label = "Accessibility"},
    Setting{.name = "default.text_size", .label = "Text size", .standard = "100%", .box = Box::Percent},
    Setting{.name = "default.high_contrast", .label = "High contrast", .standard = "no", .box = Box::YesNo},
    Setting{.name = "default.read_aloud", .label = "Read aloud", .standard = "no", .box = Box::YesNo},
    Setting{.name = "default.captions", .label = "Captions", .standard = "no", .box = Box::YesNo},
    Setting{.name = "default.animations", .label = "Animations", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "default.time_limits", .label = "Time limits", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "default.vibration", .label = "Vibration", .standard = "yes", .box = Box::YesNo},
    Setting{.label = "Visuals"},
    Setting{.name = "image_scaling", .label = "Visual scaling", .standard = "smooth", .choices = "smooth pixel", .box = Box::ImageScaling},
    Setting{.name = "character_height", .label = "Character height", .standard = "95%", .box = Box::Percent},
    Setting{.name = "animation_on_show", .label = "Visuals' animation on show", .box = Box::Animation, .hint = "At once"},
    Setting{.name = "animation_on_hide", .label = "Visuals' animation on hide", .box = Box::Animation, .hint = "At once"},
    Setting{.name = "move_time", .label = "Visuals' move time (seconds)", .standard = "0.4", .box = Box::Seconds},
    Setting{.name = "load_ahead", .label = "Lines to load ahead", .standard = "30", .box = Box::Count},
    Setting{.name = "image_memory", .label = "Visual memory (MB)", .standard = "512", .box = Box::Count},
    Setting{.label = "Screens"},
    Setting{.name = "glide", .label = "Lists glide after a drag", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "glide_speed", .label = "Glide speed", .standard = "100%", .box = Box::Percent},
    Setting{.name = "glide_distance", .label = "Glide distance", .standard = "100%", .box = Box::Percent},
    Setting{.name = "scrollbar_thickness", .label = "Scrollbar thickness (pixels)", .standard = "6", .box = Box::Pixels},
    Setting{.name = "fading_edges", .label = "Fading edges' width (pixels)", .box = Box::Pixels, .hint = "A fifth of the box, at most 64"},
    Setting{.label = "Sound"},
    Setting{.name = "default.master_volume", .label = "Master volume", .standard = "100%", .box = Box::Volume},
    Setting{.name = "default.music_volume", .label = "Music volume", .standard = "80%", .box = Box::Volume},
    Setting{.name = "default.sound_volume", .label = "Sound volume", .standard = "100%", .box = Box::Volume},
    Setting{.name = "default.voice_volume", .label = "Voice volume", .standard = "100%", .box = Box::Volume},
    Setting{.name = "music_crossfade", .label = "Music crossfade (seconds)", .standard = "0.5", .box = Box::Seconds},
    Setting{.name = "music_under_voices", .label = "Music's volume under voices", .standard = "70%", .box = Box::Volume},
    Setting{.name = "text_follows_voices", .label = "Text follows voices", .standard = "no", .box = Box::YesNo},
    Setting{.name = "default.made_up_voices", .label = "Made-up voices", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "made_up_voice_speed", .label = "Made-up voice speed", .standard = "1", .box = Box::Speed},
    Setting{.name = "default.typing_sounds", .label = "Typing sounds", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "typing_sound_gap", .label = "Typing sound gap (seconds)", .standard = "0.06", .box = Box::Seconds},
    Setting{.name = "typing_sound_volume", .label = "Typing sound volume", .standard = "60%", .box = Box::Volume},
    Setting{.label = "Characters"},
    Setting{.name = "mouths_move", .label = "Mouths move", .standard = "yes", .choices = "yes voice no", .box = Box::MouthMoves},
    Setting{.name = "default.mouths", .label = "Mouths move for players", .standard = "yes", .choices = "yes voice no", .box = Box::Mouths},
    Setting{.name = "mouth_speed", .label = "Mouth speed (moves a second)", .standard = "4", .box = Box::PerSecond},
    Setting{.name = "blink", .label = "Blinks", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "seconds_between_blinks", .label = "Blinks every (seconds)", .standard = "4", .box = Box::Seconds},
    Setting{.label = "Time"},
    Setting{.name = "time", .label = "Kind of time", .standard = "none", .choices = "none story real days", .box = Box::TimeKind},
    Setting{.name = "time_start", .label = "Start day", .box = Box::Weekday, .part = 1},
    Setting{.name = "time_start", .label = "Start time", .box = Box::TimeWhen, .hint = "8:00", .part = 2},
    Setting{.name = "time_parts", .label = "Parts of the day", .standard = "morning 6:00, afternoon 12:00, evening 18:00, night 22:00", .quoted = true, .box = Box::DayParts},
    Setting{.name = "clock", .label = "Clock", .standard = "24", .choices = "24 12", .box = Box::Clock},
    Setting{.name = "phone_follows_time", .label = "The phone follows the time", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "backgrounds_follow_time", .label = "Backgrounds follow the time", .standard = "yes", .box = Box::YesNo},
    Setting{.label = "Phone"},
    Setting{.name = "has_phone", .label = "Has a phone when", .box = Box::Variable, .hint = "Always"},
    Setting{.name = "phone_owner", .label = "Owner's name", .quoted = true, .box = Box::Text},
    Setting{.name = "phone_wallpaper", .label = "Home screen wallpaper", .box = Box::Image},
    Setting{.name = "phone_lock_wallpaper", .label = "Lock screen wallpaper", .box = Box::Image},
    Setting{.name = "phone_passcode", .label = "Passcode", .quoted = true, .box = Box::Text},
    Setting{.name = "phone_clock", .label = "Clock shows", .quoted = true, .box = Box::Text, .hint = "The computer's"},
    Setting{.name = "phone_date", .label = "Date shows", .quoted = true, .box = Box::Text},
    Setting{.name = "phone_signal", .label = "Shows signal", .standard = "yes", .box = Box::YesNo},
    Setting{.name = "phone_battery", .label = "Battery (%)", .standard = "100", .box = Box::Count},
    Setting{.name = "new_messages", .label = "On a new message", .standard = "button", .choices = "button open", .box = Box::NewMessages},
    Setting{.name = "chat_messages", .label = "Chat messages come", .standard = "typed", .choices = "typed click", .box = Box::ChatMessages},
    Setting{.name = "phone_typing", .label = "Phone typing speed (letters a second)", .standard = "6", .box = Box::TextSpeed},
    Setting{.name = "phone_history", .label = "Messages in the history", .standard = "no", .box = Box::YesNo},
    Setting{.name = "phone_message_sound", .label = "Message sound", .box = Box::Sound},
    Setting{.name = "phone_send_sound", .label = "Sending sound", .box = Box::Sound},
    Setting{.name = "phone_typing_sound", .label = "Typing sound", .box = Box::Sound},
    Setting{.name = "phone_ringtone", .label = "Ringtone", .box = Box::Sound},
    Setting{.name = "phone_calling_sound", .label = "Calling sound", .box = Box::Sound},
};

// A screen's settings, and its parts', as their forms have them: the setting, what the form calls it,
// and what it lists or is one of (the first of `choices`: not written). `is`: 'a', what it does (its
// actions); 'c', a condition; 's', sound types (the game's, several, with commas between them).
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
    {.key = "stop_on_open", .label = "On open, stops", .box = Box::SoundType, .hint = "None", .is = 's'}, // (sound types: several, as buttons)
    {.key = "stop_on_close", .label = "On close, stops", .box = Box::SoundType, .hint = "None", .is = 's'},
    {.key = "close_after", .label = "Open for (seconds)", .box = Box::Seconds, .hint = "Until closed"},
};

inline const PartOption kPartOptions[]{
    {.key = "action", .label = "Action on click", .box = Box::Action, .hint = "None", .is = 'a'},
    {.key = "confirm", .label = "Confirmation question", .box = Box::Text},
    {.key = "waiting", .label = "Text while waiting", .box = Box::Text},
    {.key = "keys", .label = "Keys that click it", .box = Box::Key, .hint = "None"},
    {.key = "clickable", .label = "Clickable when", .box = Box::Condition, .hint = "Always", .is = 'c'},
    {.key = "chosen", .label = "Looks chosen when", .box = Box::Condition, .hint = "Never", .is = 'c'},
    {.key = "drag", .label = "Dragged as", .box = Box::Name},
    {.key = "drop", .label = "Action on drop", .box = Box::Action, .hint = "None", .is = 'a'},
    {.key = "click_animation", .label = "Animation on click", .box = Box::Animation, .hint = "None"},
    {.key = "fit", .label = "How it fits", .box = Box::Fit, .choices = "contain cover stretch"},
    {.key = "type", .label = "Type", .box = Box::VisualType, .hint = "image"}, // (a visual's, as the Visual line has them)
    {.key = "expression", .label = "Expression", .box = Box::Expression, .hint = "As it is"},
    {.key = "zoom", .label = "Zoom", .box = Box::Zoom, .hint = "100%"},
    {.key = "opacity", .label = "Opacity", .box = Box::Opacity, .hint = "100%"},
    {.key = "rotation", .label = "Rotation (degrees)", .box = Box::Rotation, .hint = "0"},
    {.key = "flip", .label = "Mirrored", .box = Box::YesNo, .choices = "no yes"},
    {.key = "tint", .label = "Tint colour", .box = Box::Colour, .hint = "None"},
    {.key = "animation_on_show", .label = "Animation on show", .box = Box::Animation, .hint = "None", .suggest = "none"},
    {.key = "position", .label = "Its parts at", .box = Box::Position, .choices = "top centre bottom"},
    {.key = "close_at_end", .label = "Closes the screen at the end", .box = Box::YesNo, .choices = "no yes"},
    {.key = "mode", .label = "Saves or loads", .box = Box::SaveMode, .choices = "usual save load"},
    {.key = "track", .label = "Track colour", .box = Box::Colour, .hint = "Default"},
    {.key = "fill", .label = "Fill colour", .box = Box::Colour, .hint = "Default"},
    {.key = "knob", .label = "Knob colour", .box = Box::Colour, .hint = "Default"},
    {.key = "repeat", .label = "Repeated", .box = Box::Repeat, .choices = "no across down both"},
    {.key = "scroll", .label = "Scrolls", .box = Box::Scroll, .choices = "no left right up down"},
    {.key = "scroll_round", .label = "One round takes (seconds)", .box = Box::Seconds, .hint = "10"},
    {.key = "edges", .label = "What's past its edges", .box = Box::Edges, .choices = "show cut fade"},
    {.key = "scrollbar", .label = "Has a scrollbar", .box = Box::YesNo, .choices = "no yes"},
    {.key = "scrollbar_thickness", .label = "Scrollbar thickness (pixels)", .box = Box::Pixels, .hint = "From Settings"},
    {.key = "fading_edges", .label = "Fading edges' width (pixels)", .box = Box::Pixels, .hint = "From Settings"},
    {.key = "glide", .label = "Glides on after a drag", .box = Box::YesNo, .choices = "yes no"},
    {.key = "glide_speed", .label = "Glide speed", .box = Box::Percent, .hint = "100%"},
    {.key = "glide_distance", .label = "Glide distance", .box = Box::Percent, .hint = "100%"},
    {.key = "slot", .label = "Which of its keys", .box = Box::KeySlot, .hint = "1"},
    {.key = "variable", .label = "Variable", .box = Box::Variable},
    {.key = "setting", .label = "Player setting", .box = Box::Setting, .hint = "None"},
    {.key = "value", .label = "Value shown", .box = Box::Variable, .hint = "None"},
    {.key = "minimum", .label = "Minimum", .box = Box::Variable, .hint = "0"},
    {.key = "maximum", .label = "Maximum", .box = Box::Variable, .hint = "100"},
    {.key = "most_letters", .label = "Letters at most", .box = Box::Variable, .hint = "64"},
    {.key = "step", .label = "Steps of", .box = Box::Number, .hint = "1"},
    {.key = "placeholder", .label = "Shown while empty", .box = Box::Text, .hint = "None"},
    {.key = "vertical", .label = "Vertical", .box = Box::YesNo, .choices = "no yes"},
    {.key = "loop", .label = "Loops", .box = Box::YesNo, .choices = "yes no"},
    {.key = "volume", .label = "Volume", .box = Box::Volume, .hint = "100%"},
    {.key = "line", .label = "Line colour", .box = Box::Colour, .hint = "Default"},
    {.key = "choice_style", .label = "Choices' style", .box = Box::Style, .hint = "Default"},
    {.key = "ending_style", .label = "Endings' style", .box = Box::Style, .hint = "Default"},
    {.key = "unreached_style", .label = "Unreached points' style", .box = Box::Style, .hint = "Default"},
    {.key = "stack", .label = "Its parts go", .box = Box::Stack, .choices = "no down across"},
    {.key = "list", .label = "Once for each of", .box = Box::List, .hint = "None"},
    {.key = "item_animation", .label = "Animation on a new item", .box = Box::Animation, .hint = "None"},
    {.key = "app", .label = "App's name", .box = Box::Name, .hint = "None"},
    {.key = "title", .label = "App's title", .box = Box::Text, .hint = "None"},
    {.key = "icon", .label = "App's icon", .box = Box::Image, .hint = "None"},
    {.key = "width_at_most", .label = "Width at most", .box = Box::Number, .hint = "None"},
    {.key = "lines", .label = "Lines at most", .box = Box::Count, .hint = "None"},
    {.key = "count", .label = "Count on its icon", .box = Box::Variable, .hint = "New items"},
    {.key = "screen", .label = "Shows the screen", .box = Box::Screen, .hint = "None"}, // (its parts in it, laid out in its box)
    {.key = "columns", .label = "Columns", .box = Box::Count, .hint = "1"},
    {.key = "item_width", .label = "Each item's width", .box = Box::Pixels, .hint = "Default"},
    {.key = "item_height", .label = "Each item's height", .box = Box::Pixels, .hint = "Default"},
    {.key = "spacing", .label = "Gap between items", .box = Box::Pixels, .hint = "Default"},
    {.key = "spacing_across", .label = "Gap between columns", .box = Box::Pixels, .hint = "Default"},
    {.key = "spacing_down", .label = "Gap between rows", .box = Box::Pixels, .hint = "Default"},
};

constexpr std::string_view kImageBy = "?";
inline const ToolsSuggestions kImageBySuggest = "?";

// In a form's fields: where an empty box means "from its style", the style's value instead (a field is
// named by its setting, or, a style's own form, by the setting's label).
void showStyleValues(std::vector<StoryEditor::Field>& fields, const Style& style, const Palette& palette);

// ...and in a story line's or a character's form: where an empty box means the setting in Settings
// (a move's time, the animation on show, blinking...), that value; where it hangs on another box (a
// character's image: their name; a named sound's fade out), what that box makes it.
void showGameSettingValues(std::vector<StoryEditor::Field>& fields, const GameSettings& gameSettings);

// Seconds as the editor writes them: at most two places, no zeros after them (0.5, 1.25, 2); and two
// times the same to a hundredth.
std::string secondsText(float seconds);
inline bool sameMoment(float a, float b) { return std::abs(a - b) < 0.005f; }

// An animation's change's usual easing, for what it changes (its key): a fade's is even, as a screen's fade is.
inline std::string_view usualEase(std::string_view key) { return key == "fade" ? "linear" : "inout"; }

// The Library's files: each kind's list, what a file's form calls one, its folder and its file types.
struct FileKind {
    std::string_view id;
    ToolsText label, one;
    std::string_view folder;
    std::span<const std::string_view> extensions;
};

inline const FileKind kFileKinds[]{{"images", "Visuals", "Visual", "images/", kImageTypes}, // (images and movies: visuals, one list)
                                {"sounds", "Sounds", "Sound", "sounds/", kSoundTypes},
                                {"movies", "Visuals", "Visual", "movies/", kMovieTypes},
                                {"fontfiles", "Fonts", "Font file", "fonts/", kFontTypes}};

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

// A language's code from its name (in English or in itself, however it's capitalised); empty for one that isn't
// listed.
std::string languageCode(std::string_view name);

// Spelling: the language a game's words are in, as the spell checker names it (setting language;
// empty: the system's own), and the words of a text it doesn't know (where each is; what's in braces,
// and words with numbers in them, aren't checked, nor `names`). Each word is looked up once.
std::string spellingLanguage(std::string_view language);

} // namespace dev

[[nodiscard]] CommandForm definitionForm(std::string_view kind);
[[nodiscard]] std::string formLine(const CommandForm& f, const std::vector<StoryEditor::Field>& fields);
[[nodiscard]] const std::string& actionGroupList();
[[nodiscard]] const std::string& actionKindList(std::string_view group);
[[nodiscard]] const std::string& languageList();

} // namespace vn
