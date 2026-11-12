// The player's built-in screens and keys: what a game without its own has.
#include "Script.hpp"

#include <string_view>

namespace vn {

// The player's built-in screens: just what a story needs to play (its lines, with Skip, Auto and quick saves, a Phone
// button when the game has a phone and a Menu button when it has a pause menu, side by side at the right; its choices,
// a moment's "Saved", the captions and the on-screen keyboard), and its keys: the next line, skipping and auto-forward, quick saving and loading, and on
// screens, pointing at what's there (the arrow keys, a controller's d-pad or stick) and pressing it; then the developer
// tools' own (tools WHAT: the keys of their buttons, which do nothing in the game). A game's own screens and controls
// replace these, and add the rest (a title screen, menus...). Written for a 1280 x 720 game and fitting a stage of any
// shape (a tall one too): their pixels scaled by its shorter side (lookScale), what keeps its shape in pixels (the
// textbox's box at the bottom, its name and buttons on the panel's top edge, y=1.5; the options' room above it; the
// keyboard; Saved), what spans the stage in shares of it.

std::string_view defaultUi() {
    return R"vnext(
control next_line keys="space mouse_left return kp_enter pad_a" action="next line"
control skip keys="tab pad_rb" action=skip
control auto keys="a pad_lb" action=auto
control quick_save keys="f5" action="quick save; open saved" when=can_save
control quick_load keys="f9" action="quick load" when=has_quick_save
control move_up keys="up pad_up" action="move up" in_menus=yes
control move_down keys="down pad_down" action="move down" in_menus=yes
control move_left keys="left pad_left" action="move left" in_menus=yes
control move_right keys="right pad_right" action="move right" in_menus=yes
control press keys="return space kp_enter pad_a" action=press in_menus=yes
control tools_undo keys="cmd+z ctrl+z" action="tools undo"
control tools_redo keys="cmd+shift+z ctrl+shift+z cmd+y ctrl+y" action="tools redo"
control tools_cut keys="cmd+x ctrl+x" action="tools cut"
control tools_copy keys="cmd+c ctrl+c" action="tools copy"
control tools_paste keys="cmd+v ctrl+v" action="tools paste"
control tools_duplicate keys="cmd+d ctrl+d" action="tools duplicate"
control tools_delete keys="delete backspace" action="tools delete"
control tools_select_all keys="cmd+a ctrl+a" action="tools select all"
control tools_reload keys="cmd+r ctrl+r" action="tools reload"
control tools_quit keys="cmd+q ctrl+q" action="tools quit"
control tools_story keys="cmd+1 ctrl+1" action="tools story"
control tools_screens keys="cmd+2 ctrl+2" action="tools screens"
control tools_library keys="cmd+3 ctrl+3" action="tools library"
control tools_play keys="f10 pad_guide" action="tools play"
control tools_stop_here keys="f9" action="tools stop here"
control tools_next keys="tab pad_rb" action="tools next"
control tools_previous keys="shift+tab pad_lb" action="tools previous"
control tools_press keys="return kp_enter space pad_a" action="tools press"
control tools_up keys="up pad_up" action="tools up"
control tools_down keys="down pad_down" action="tools down"
control tools_left keys="left pad_left" action="tools left"
control tools_right keys="right pad_right" action="tools right"
control tools_back keys="escape pad_b" action="tools back"
control tools_next_page keys="ctrl+tab pad_rt" action="tools next page"
control tools_previous_page keys="ctrl+shift+tab pad_lt" action="tools previous page"

style choice size=24 align=centre align_vertically=centre background=#2b2563 border=#5b51b8 pointed_background=#3b3392 chosen_background=#4d41c4 unclickable_colour=#9696a5 shadow=no

style textbox_panel border=#5b51b8 opacity_follows=yes shadow=no
style namebox size=28 align=centre align_vertically=centre background=#2b2563 padding=20 colour=speaker opacity_follows=yes shadow=no
style dialogue size=28 text_size_follows=yes shadow=no
style indicator size=16 colour=#ada2ff shadow=no
animation go_on loop=yes
    down=0.4% over 0.6 seconds ease=inout
    y=home over 0.6 seconds ease=inout
style textbox_button size=20 align=centre align_vertically=centre background=#2b2563 border=#5b51b8 pointed_background=#3b3392 chosen_background=#4d41c4 unclickable_colour=#9696a5 shadow=no
style textbox_unread size=15 colour=#0c0a22 align=centre align_vertically=centre shadow=no

screen textbox story=goes_on
    container x=0 y=-1 width=0.9375 height=226px
        container x=0 y=1 width=1 height=190px
            image #141230fa x=0 y=0 width=1 height=1 style=textbox_panel
            text "{dialogue}" x=0 y=0.6421 width=0.9467
            text "▼" x=0.9541 y=-0.726 style=indicator animation=go_on when=text_complete
            container x=0 y=1.5 width=0.96 height=46px
                container x=-1 y=0 height=1 fit=parts style=namebox when=speaker
                    text "{speaker}" x=0 y=0 height=1 style=namebox
                container x=1 y=0 height=1 fit=parts stack=across spacing_across=0.0069
                    container width=0.1111 height=1 when=phone_button
                        text "Phone" x=0 y=0 width=1 height=1 style=textbox_button action="open phone"
                        image #ada2ff x=1.358 y=1.4302 width=28px height=28px when="phone_unread is more than 0"
                        text "{phone_unread}" x=1.358 y=1.4302 width=28px height=28px style=textbox_unread when="phone_unread is more than 0"
                    text "Menu" width=0.1111 height=1 style=textbox_button action="open pause" when=menu_button
                    text "Skip" width=0.1111 height=1 style=textbox_button action=skip chosen=skip
                    text "Auto" width=0.1111 height=1 style=textbox_button action=auto chosen=auto
                    text "Q.Save" width=0.1111 height=1 style=textbox_button action="quick save; open saved" clickable=can_save
                    text "Q.Load" width=0.1111 height=1 style=textbox_button action="quick load" clickable=has_quick_save

screen choice story=goes_on
    container x=0 y=0 width=0.9 height=1 stack=down
        container height=40px
        container list=options x=0 width=720px item_height=54px spacing_down=14px position=bottom
            text "{option.text}" width=1 height=1 style=choice action=choose when="not option.locked"
            text "{option.text}  ({option.note})" width=1 height=1 style=choice clickable=no when=option.locked
        container height=260px

screen saved story=goes_on close_after=1.5
    container x=0.95 y=0.92 width=179px height=46px style=textbox_button
        text "Saved" x=0 y=0 width=1 height=1 style=textbox_button

style caption size=24 align=centre background=#141230e6 padding=10 text_size_follows=yes shadow=no
style keyboard background=#141230fa border=#5b51b8 shadow=no
style keyboard_key size=22 align=centre align_vertically=centre background=#0c0a22 border=#5b51b8 pointed_background=#3b3392 chosen_background=#4d41c4 shadow=no

screen captions story=goes_on
    container list=captions x=0 y=0.9 width=0.8 fit=parts stack=down spacing=8px
        container x=0 fit=parts style=caption
            text "{caption.text}" width_at_most=1 style=caption
    container list=subtitles x=0 y=-0.88 width=0.9 fit=parts stack=down
        container x=0 fit=parts style=caption
            text "{subtitle.text}" width_at_most=1 style=caption

screen keyboard story=goes_on
    container x=0 y=-0.944 width=690px height=239px style=keyboard
        container x=0 y=0 fit=parts stack=down spacing_down=0.0251
            container x=0 fit=parts stack=across spacing_across=0.0087
                text action="type `" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 1" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 2" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 3" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 4" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 5" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 6" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 7" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 8" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 9" width=0.0667 height=0.1632 style=keyboard_key
                text action="type 0" width=0.0667 height=0.1632 style=keyboard_key
                text action="type -" width=0.0667 height=0.1632 style=keyboard_key
                text action="type =" width=0.0667 height=0.1632 style=keyboard_key
            container x=0 fit=parts stack=across spacing_across=0.0087
                text action="type q" width=0.0667 height=0.1632 style=keyboard_key
                text action="type w" width=0.0667 height=0.1632 style=keyboard_key
                text action="type e" width=0.0667 height=0.1632 style=keyboard_key
                text action="type r" width=0.0667 height=0.1632 style=keyboard_key
                text action="type t" width=0.0667 height=0.1632 style=keyboard_key
                text action="type y" width=0.0667 height=0.1632 style=keyboard_key
                text action="type u" width=0.0667 height=0.1632 style=keyboard_key
                text action="type i" width=0.0667 height=0.1632 style=keyboard_key
                text action="type o" width=0.0667 height=0.1632 style=keyboard_key
                text action="type p" width=0.0667 height=0.1632 style=keyboard_key
                text action="type [" width=0.0667 height=0.1632 style=keyboard_key
                text action="type ]" width=0.0667 height=0.1632 style=keyboard_key
                text action="type \" width=0.0667 height=0.1632 style=keyboard_key
            container x=-0.75 fit=parts stack=across spacing_across=0.0087
                text action="type a" width=0.0667 height=0.1632 style=keyboard_key
                text action="type s" width=0.0667 height=0.1632 style=keyboard_key
                text action="type d" width=0.0667 height=0.1632 style=keyboard_key
                text action="type f" width=0.0667 height=0.1632 style=keyboard_key
                text action="type g" width=0.0667 height=0.1632 style=keyboard_key
                text action="type h" width=0.0667 height=0.1632 style=keyboard_key
                text action="type j" width=0.0667 height=0.1632 style=keyboard_key
                text action="type k" width=0.0667 height=0.1632 style=keyboard_key
                text action="type l" width=0.0667 height=0.1632 style=keyboard_key
                text action="type semicolon" width=0.0667 height=0.1632 style=keyboard_key
                text action="type apostrophe" width=0.0667 height=0.1632 style=keyboard_key
            container x=-0.6667 fit=parts stack=across spacing_across=0.0087
                text action="type z" width=0.0667 height=0.1632 style=keyboard_key
                text action="type x" width=0.0667 height=0.1632 style=keyboard_key
                text action="type c" width=0.0667 height=0.1632 style=keyboard_key
                text action="type v" width=0.0667 height=0.1632 style=keyboard_key
                text action="type b" width=0.0667 height=0.1632 style=keyboard_key
                text action="type n" width=0.0667 height=0.1632 style=keyboard_key
                text action="type m" width=0.0667 height=0.1632 style=keyboard_key
                text action="type ," width=0.0667 height=0.1632 style=keyboard_key
                text action="type ." width=0.0667 height=0.1632 style=keyboard_key
                text action="type /" width=0.0667 height=0.1632 style=keyboard_key
            container x=0 fit=parts stack=across spacing_across=0.0087
                text "Shift" action="type shift" width=0.142 height=0.1632 style=keyboard_key keys="pad_lt" chosen=keyboard_shift
                text "Space" action="type space" width=0.3681 height=0.1632 style=keyboard_key keys="pad_y"
                text "←" action="type left" width=0.0667 height=0.1632 style=keyboard_key keys="pad_lb"
                text "→" action="type right" width=0.0667 height=0.1632 style=keyboard_key keys="pad_rb"
                text "Delete" action="type delete" width=0.142 height=0.1632 style=keyboard_key keys="pad_x"
                text "Done" action="type done" width=0.142 height=0.1632 style=keyboard_key keys="pad_start"
)vnext";
}

} // namespace vn
