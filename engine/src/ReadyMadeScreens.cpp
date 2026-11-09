// The ready-made screens the Screens page starts from (ReadyMadeScreens.hpp): their look, their lines, the Settings
// screen made to fit the game, and a ready-made screen made the game's own.
#include "DeveloperUi.hpp"
#include "ReadyMadeScreens.hpp"

#include <cctype>
#include <iterator>
#include <ranges>

namespace vn {

namespace dev {

bool builtIn(std::string_view name) { return name == "choice" || name == "saved"; }

} // namespace dev

namespace {

// Their look: colours and styles, each added when the game has none by that name.
constexpr std::string_view kLook = R"vnext(colour text #f0f0f5
colour dim #9696a5
colour accent #ada2ff
colour line #5b51b8
colour button #2b2563
colour pointed #3b3392
colour chosen #4d41c4
colour field #0c0a22
colour backdrop #141230
colour panel #141230fa
colour shade #000000aa
style title size=64 shadow=no
style dim size=18 colour=dim shadow=no
style label size=18 colour=dim align_vertically=centre shadow=no
style heading colour=accent shadow=no
style screen_title based_on=heading size=32
style calendar size=44 align=centre align_vertically=centre shadow=no
style calendar_time based_on=heading align=centre
style centred align=centre align_vertically=centre shadow=no
style framed border=line shadow=no
style card background=panel border=line padding=16 shadow=no
style shaded background=shade shadow=no
style input size=24 align=centre align_vertically=centre background=field border=line pointed_border=accent padding=12 shadow=no
style button align=centre align_vertically=centre background=button border=line pointed_background=pointed chosen_background=chosen unclickable_colour=dim shadow=no
style small_button based_on=button size=18
style choice based_on=button
style tab size=18 colour=dim align=centre align_vertically=centre background=button border=line pointed_background=pointed chosen_background=chosen chosen_colour=text shadow=no
style slot background=button border=line pointed_background=pointed padding=4 shadow=no
style slot_title size=18 colour=accent shadow=no
style slot_text size=18 shadow=no
style slot_empty size=18 colour=dim background=field shadow=no
style setting size=18 align=centre align_vertically=centre background=button border=line pointed_background=pointed chosen_background=chosen unclickable_colour=dim shadow=no
style setting_value size=18 align_vertically=centre shadow=no
style setting_choice based_on=setting_value align=centre
style history size=24 text_size_follows=yes shadow=no
style history_name based_on=history align=right
style history_choice based_on=history colour=dim
style history_button based_on=small_button
style record size=24 align_vertically=centre background=button border=line padding=16 pointed_background=pointed chosen_background=chosen unclickable_colour=dim shadow=no
style record_locked based_on=record colour=dim background=field
style record_title size=24 align_vertically=centre shadow=no
style record_text size=18 colour=dim shadow=no
style gallery background=field border=line pointed_border=accent unclickable_colour=dim shadow=no
style gallery_title size=18 align_vertically=centre padding=8 shadow=no
style locked size=24 colour=dim align=centre align_vertically=centre shadow=no
style flowchart size=16 align=centre align_vertically=centre background=button border=line padding=8 pointed_background=pointed chosen_background=chosen shadow=no
style flowchart_unreached based_on=flowchart colour=dim background=field
style flowchart_ending based_on=flowchart colour=accent
style flowchart_choice size=14 colour=dim shadow=no
style phone_title size=20 shadow=no
style phone_title_centre based_on=phone_title align=centre
style phone_status size=14 shadow=no padding=4
style phone_status_right based_on=phone_status align=right
style phone_icon size=24 align=centre align_vertically=centre background=button border=line pointed_background=pointed shadow=no
style phone_label size=14 colour=dim shadow=no
style phone_label_centre based_on=phone_label align=centre
style phone_card based_on=record padding=10
style phone_name size=16 shadow=no
style phone_preview size=15 colour=dim shadow=no
style phone_preview_centre based_on=phone_preview align=centre
style phone_preview_right based_on=phone_preview align=right
style phone_time size=13 colour=dim shadow=no
style phone_time_centre based_on=phone_time align=centre
style phone_time_right based_on=phone_time align=right
style phone_unread size=13 colour=field align=centre align_vertically=centre background=accent shadow=no
style phone_initial size=16 colour=field align=centre align_vertically=centre shadow=no
style phone_caller_initial based_on=phone_initial size=48
style phone_missed based_on=phone_name colour=#ff8c96
style phone_text size=17 background=button border=line padding=9 shadow=no
style phone_input size=17 background=field border=line pointed_border=accent padding=9 shadow=no
style phone_mine based_on=phone_text background=chosen
style phone_reaction size=13 background=chosen border=line padding=3 shadow=no
style phone_small_button size=16 align=centre align_vertically=centre background=button border=line pointed_background=pointed chosen_background=chosen unclickable_colour=dim shadow=no
style phone_notice size=15 align_vertically=centre background=button border=line padding=8 pointed_background=pointed shadow=no
style phone_photo pointed_border=accent shadow=no
style phone_viewer background=field shadow=no
style phone_caller size=24 align=centre shadow=no
style phone_answer based_on=phone_small_button background=#2f7a50 pointed_background=#3a9562
style phone_decline based_on=phone_small_button background=#963240 pointed_background=#b03c4c
style phone_clock size=48 align=centre shadow=no
style namebox size=28 align=centre align_vertically=centre background=button padding=20 colour=speaker opacity_follows=yes shadow=no
style dialogue size=28 text_size_follows=yes shadow=no
style indicator size=16 colour=accent shadow=no
style textbox_panel border=line opacity_follows=yes shadow=no
style textbox_button size=20 align=centre align_vertically=centre background=button border=line pointed_background=pointed chosen_background=chosen unclickable_colour=dim shadow=no
style textbox_unread size=15 colour=field align=centre align_vertically=centre shadow=no
style caption size=24 align=centre background=panel padding=10 text_size_follows=yes shadow=no
style keyboard background=panel border=line shadow=no
style keyboard_key size=22 align=centre align_vertically=centre background=field border=line pointed_background=pointed chosen_background=chosen shadow=no)vnext";

// Save and Load: the pages of saves (loading: the Auto and Quick ones too) and the saves on the one open, each with its
// picture, its number and time and its words, and a Delete button; asked first when it would save over one (Save) or
// leave a game going on (Load). TABS: Load's Auto and Quick; PAGES: where the numbered ones start.
constexpr std::string_view kSlotsScreen = R"vnext(screen NAME
    image backdrop x=0 y=0 width=1280 height=720
    text "TITLE" x=60 y=32 width=600 style=screen_titleTABS
    container list=pages x=PAGES y=30 width=444 height=34 columns=9 item_width=44 item_height=34 spacing_across=6
        button "{page.name}" x=0 y=0 width=44 height=34 style=tab action="page {page.name}" chosen=page.open
    container list=saves x=60 y=92 width=1160 height=540 columns=3 item_width=360 item_height=262 spacing_across=40 spacing_down=16
        button x=0 y=0 width=360 height=262 style=slot action="MODE {save.number}" confirm="QUESTION"CLICKABLE
        image "{save.image}" x=4 y=4 width=352 height=198 when=save.image
        image field x=4 y=4 width=352 height=198 when="not save.image"
        text "{save.number}  ·  {save.time}" x=10 y=208 width=340 style=slot_title when=save.used
        text "{save.number}" x=10 y=208 width=340 style=slot_empty when="not save.used"
        text "{save.text}" x=10 y=232 width=340 style=slot_text lines=1 when=save.used
        button "Delete" x=272 y=10 width=78 height=28 style=small_button action="delete save {save.number}" confirm="Delete this save?" when=save.used
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

// The history: the lines read, the newest at the bottom (it opens there; turned on down past it, it closes), each with
// who said it, and buttons to hear its voice again and to go back to it.
constexpr std::string_view kHistoryScreen = R"vnext(screen history
    image backdrop x=0 y=0 width=1280 height=720
    text "History" x=60 y=32 width=600 style=screen_title
    container list=history x=60 y=92 width=1160 height=540 spacing_down=18 position=bottom scroll=yes scrollbar=yes close_at_end=yes
        text "{colour line.colour}{line.name}{/colour}" x=0 y=0 width=200 style=history_name
        text "{line.text}" x=230 y=0 width=720 style=history when="not line.choice"
        text "{line.text}" x=230 y=0 width=720 style=history_choice when=line.choice
        button "Replay" x=960 y=0 width=84 height=30 style=history_button action="replay {line.number}" when=line.voiced
        button "Go back" x=1050 y=0 width=84 height=30 style=history_button action="rollback {line.number}" when=line.back
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape l pad_b pad_x")vnext";

// The extras: the gallery (a picture clicked: big, in its viewer), the music room (a song clicked plays; clicked
// again, it stops), the endings and achievements reached, the tips (one clicked: its words beside the list) and the
// flowchart, each in the Save and History screens' look; what isn't reached yet says ???.
constexpr std::string_view kGalleryScreen = R"vnext(screen gallery
    image backdrop x=0 y=0 width=1280 height=720
    text "Gallery" x=60 y=32 width=600 style=screen_title
    container list=gallery x=60 y=92 width=1160 height=540 columns=3 item_width=370 item_height=208 spacing_across=25 spacing_down=24 scroll=yes scrollbar=yes
        button x=0 y=0 width=370 height=208 style=gallery action="open image {image.name}; open gallery_viewer" clickable="not image.locked"
        image "{image.name}" x=2 y=2 width=366 height=204 fit=cover volume=0% when="not image.locked"
        image panel x=2 y=170 width=366 height=36 when="not image.locked"
        text "{image.title}" x=2 y=170 width=366 height=36 style=gallery_title lines=1 when="not image.locked"
        text "???" x=0 y=0 width=370 height=208 style=locked when=image.locked
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kGalleryViewerScreen = R"vnext(screen gallery_viewer
    image backdrop x=0 y=0 width=1280 height=720
    text "{open_image_text}" x=60 y=32 width=1000 style=screen_title lines=1
    image "{open_image}" x=60 y=92 width=1160 height=540 fit=contain
    button "Previous" x=780 y=654 width=140 height=40 style=small_button action="previous image" keys="left pad_left"
    button "Next" x=930 y=654 width=140 height=40 style=small_button action="next image" keys="right pad_right"
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kMusicRoomScreen = R"vnext(screen music_room
    image backdrop x=0 y=0 width=1280 height=720
    text "Music room" x=60 y=32 width=600 style=screen_title
    container list=songs x=60 y=92 width=1160 height=540 item_height=60 spacing_down=10 scroll=yes scrollbar=yes
        button "{song.title}" x=0 y=0 width=1140 height=60 style=record action="play song {song.name}" chosen=song.playing when="not song.locked"
        button "???" x=0 y=0 width=1140 height=60 style=record_locked clickable=no when=song.locked
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kEndingsScreen = R"vnext(screen endings
    image backdrop x=0 y=0 width=1280 height=720
    text "Endings" x=60 y=32 width=600 style=screen_title
    container list=endings x=60 y=92 width=1160 height=540 item_height=80 spacing_down=10 scroll=yes scrollbar=yes
        container x=0 y=0 width=1140 height=80 style=record when="not ending.locked"
            text "{ending.number}. {ending.title}" x=16 y=0 width=1108 height=80 style=record_title
        container x=0 y=0 width=1140 height=80 style=record_locked when=ending.locked
            text "{ending.number}. ???" x=16 y=12 width=1108 style=record_title
            text "{ending.text}" x=16 y=46 width=1108 style=record_text lines=1
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kAchievementsScreen = R"vnext(screen achievements
    image backdrop x=0 y=0 width=1280 height=720
    text "Achievements" x=60 y=32 width=600 style=screen_title
    container list=achievements x=60 y=92 width=1160 height=540 item_height=80 spacing_down=10 scroll=yes scrollbar=yes
        container x=0 y=0 width=1140 height=80 style=record when="not achievement.locked"
            text "{achievement.title}" x=16 y=12 width=1108 style=record_title
            text "{achievement.text}" x=16 y=46 width=1108 style=record_text lines=1
        container x=0 y=0 width=1140 height=80 style=record_locked when="achievement.locked and not achievement.secret"
            text "{achievement.title}" x=16 y=12 width=1108 style=record_title
            text "{achievement.text}" x=16 y=46 width=1108 style=record_text lines=1
        container x=0 y=0 width=1140 height=80 style=record_locked when="achievement.locked and achievement.secret"
            text "???" x=16 y=12 width=1108 style=record_title
            text "???" x=16 y=46 width=1108 style=record_text
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kTipsScreen = R"vnext(screen tips
    image backdrop x=0 y=0 width=1280 height=720
    text "Tips" x=60 y=32 width=600 style=screen_title
    container list=tips x=60 y=92 width=440 height=540 item_height=60 spacing_down=10 scroll=yes scrollbar=yes
        button "{tip.title}" x=0 y=0 width=420 height=60 style=record action="open tip {tip.name}" chosen=tip.open when="not tip.locked"
        button "???" x=0 y=0 width=420 height=60 style=record_locked clickable=no when=tip.locked
    text "{tip_title}" x=540 y=92 width=680 style=heading
    text "{tip_text}" x=540 y=140 width=680 style=record_title
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kFlowchartScreen = R"vnext(screen flowchart
    image backdrop x=0 y=0 width=1280 height=720
    text "Flowchart" x=60 y=32 width=600 style=screen_title
    flowchart x=60 y=92 width=1160 height=540 unreached_style=flowchart_unreached ending_style=flowchart_ending choice_style=flowchart_choice scrollbar=yes confirm="Go back to this point? Progress since your last save will be lost."
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

// The question (confirm=): asked by the keys or a controller, the ring's on No at once (Enter and A press the one it's
// on); Esc and B say no, as the developer tools' own questions have it.
constexpr std::string_view kConfirmScreen = R"vnext(screen confirm
    image shade x=0 y=0 width=1280 height=720
    image panel x=310 y=210 width=660 height=300 style=framed
    text "{question}" x=330 y=226 width=620 height=212 style=centred
    button "Yes" x=660 y=454 width=140 height=40 style=small_button action=yes
    button "No" x=810 y=454 width=140 height=40 style=small_button action=no keys="escape pad_b")vnext";

// The phone: at the side (the story goes on while it's open), above the textbox, made of containers like any
// screen. Along its top its clock, signal and battery; in it its apps (each a container with app=: the home
// screen with an icon for each, then each app's parts; its lists, list=, show a part once for each chat,
// message, call...; the call and lock screens), the image viewer over them, and a Back button: back from an
// image, a chat, an app, then the phone itself.
constexpr std::string_view kPhoneScreen = R"vnext(screen phone story=goes_on
    image panel x=950 y=12 width=300 height=472 style=framed
    text "{phone_clock}" x=962 y=20 width=100 height=22 style=phone_status
    text "No signal" x=1066 y=20 width=126 height=22 style=phone_status_right when="not phone_signal"
    image text x=1174 y=33 width=3 height=4 when=phone_signal
    image text x=1179 y=31 width=3 height=6 when=phone_signal
    image text x=1184 y=29 width=3 height=8 when=phone_signal
    image text x=1189 y=27 width=3 height=10 when=phone_signal
    slider x=1200 y=26 width=24 height=10 value=phone_battery track=#ffffff30 fill=#f0f0f5 when="phone_battery is more than 20"
    slider x=1200 y=26 width=24 height=10 value=phone_battery track=#ffffff30 fill=#ff8c96 when="phone_battery is at most 20"
    image text x=1224 y=29 width=2 height=4
    image "{phone_wallpaper}" x=962 y=46 width=276 height=394 fit=cover when="phone_wallpaper and phone_app is 'home'"
    image "{phone_lock_wallpaper}" x=962 y=46 width=276 height=394 fit=cover when="phone_lock_wallpaper and phone_app is 'lock'"
    container app=home x=962 y=46 width=276 height=394
        container list=apps x=8 y=10 width=260 height=376 columns=3 item_height=96 spacing=10 scroll=yes
            button "{app.initial}" x=11 y=4 width=58 height=58 style=phone_icon action="open app {app.name}"
            image "{app.icon}" x=11 y=4 width=58 height=58 fit=cover when=app.icon
            text "{app.title}" x=0 y=76 width=80 style=phone_label_centre lines=1
            container x=56 y=0 width=22 height=18 style=phone_unread when=app.count
                text "{app.count}" x=0 y=0 width=22 height=18 style=phone_unread
    container app=chats title="Chats" x=962 y=46 width=276 height=394
        text "Chats" x=4 y=0 width=268 style=phone_title when="not open_chat"
        text "{open_chat}" x=4 y=0 width=268 style=phone_title when=open_chat
        text "No chats" x=0 y=190 width=276 style=phone_preview_centre when="not open_chat and chats_total is 0"
        text "No messages" x=0 y=190 width=276 style=phone_preview_centre when="open_chat and open_chat_messages is 0"
        container list=chats x=0 y=34 width=276 height=360 item_height=58 spacing=6 scroll=yes when="not open_chat"
            button x=0 y=0 width=276 height=58 style=phone_card action="open chat {chat.name}" chosen=chat.open
            image "{chat.avatar}" x=10 y=12 width=34 height=34 fit=cover when=chat.avatar
            image "{chat.colour}" x=10 y=12 width=34 height=34 when="not chat.avatar"
            text "{chat.initial}" x=10 y=12 width=34 height=34 style=phone_initial when="not chat.avatar"
            text "{colour chat.colour}{chat.name}{/colour}" x=54 y=9 width=150 style=phone_name lines=1
            text "{chat.time}" x=204 y=11 width=62 style=phone_time_right
            text "{chat.text}" x=54 y=31 width=180 style=phone_preview lines=1
            container x=240 y=32 width=26 height=18 style=phone_unread when=chat.unread
                text "{chat.unread}" x=0 y=0 width=26 height=18 style=phone_unread
        container x=0 y=34 width=276 height=360 stack=down spacing=8 position=bottom scroll=yes when=open_chat
            container list=messages x=0 width=276 fit=parts stack=down spacing=8
                text "{message.day}" x=0 width=276 style=phone_time_centre when=message.day
                text "Chat ended" x=0 width=276 style=phone_time_centre when=message.ended
                container x=0 fit=parts stack=across spacing=6 when="not message.mine and not message.ended"
                    container width=28 height=28
                        image "{message.avatar}" x=0 y=0 width=28 height=28 fit=cover when=message.avatar
                        image "{message.colour}" x=0 y=0 width=28 height=28 when="not message.avatar"
                        text "{message.initial}" x=0 y=0 width=28 height=28 style=phone_initial when="not message.avatar"
                    container fit=parts stack=down style=phone_text when="not message.sticker"
                        text "{message.text}" width_at_most=196 style=phone_text when=message.text
                        text "{italic}Message deleted{/italic}" style=phone_text when=message.deleted
                        container width=160 height=120 when=message.image
                            image "{message.image}" x=0 y=0 width=160 height=120 fit=cover
                            button x=0 y=0 width=160 height=120 style=phone_photo action="open image {message.image}"
                        button "Play the voice message" width=196 height=34 style=phone_small_button action="sound {message.sound}" when=message.sound
                    image "{message.image}" width=96 height=96 fit=contain when=message.sticker
                container x=right fit=parts stack=down style=phone_mine when="message.mine and not message.sticker"
                    text "{message.text}" width_at_most=196 style=phone_mine when=message.text
                    text "{italic}Message deleted{/italic}" style=phone_mine when=message.deleted
                    container width=160 height=120 when=message.image
                        image "{message.image}" x=0 y=0 width=160 height=120 fit=cover
                        button x=0 y=0 width=160 height=120 style=phone_photo action="open image {message.image}"
                    button "Play the voice message" width=196 height=34 style=phone_small_button action="sound {message.sound}" when=message.sound
                image "{message.image}" x=right width=96 height=96 fit=contain when="message.mine and message.sticker"
                container x=34 fit=parts style=phone_reaction when="message.reaction and not message.mine"
                    text "{message.reaction}" style=phone_reaction
                container x=right fit=parts style=phone_reaction when="message.reaction and message.mine"
                    text "{message.reaction}" style=phone_reaction
                text "Edited" x=34 style=phone_time when="message.edited and not message.mine"
                text "Edited" x=right style=phone_time when="message.edited and message.mine"
                text "Seen" x=right style=phone_time when=message.seen
            slider x=right width=196 height=4 value=time_left track=#ffffff28 fill=accent when=time_left
            container list=options x=right width=196 fit=parts spacing=6
                button "{option.text}" x=0 y=0 width=196 height=34 style=phone_small_button action=choose clickable=option.on
    container app=calls title="Calls" x=962 y=46 width=276 height=394
        text "Calls" x=4 y=0 width=268 style=phone_title
        text "No calls" x=0 y=190 width=276 style=phone_preview_centre when="calls_total is 0"
        container list=calls x=0 y=34 width=276 height=360 item_height=52 spacing=6 scroll=yes
            button x=0 y=0 width=276 height=52 style=phone_card action="phone call {call.who}"
            image "{call.avatar}" x=10 y=11 width=30 height=30 fit=cover when=call.avatar
            image "{call.colour}" x=10 y=11 width=30 height=30 when="not call.avatar"
            text "{call.initial}" x=10 y=11 width=30 height=30 style=phone_initial when="not call.avatar"
            text "{colour call.colour}{call.who}{/colour}" x=50 y=6 width=150 style=phone_name lines=1 when="not call.missed"
            text "{call.who}" x=50 y=6 width=150 style=phone_missed lines=1 when=call.missed
            text "Incoming {call.time}" x=50 y=27 width=150 style=phone_preview lines=1 when=call.incoming
            text "Outgoing {call.time}" x=50 y=27 width=150 style=phone_preview lines=1 when=call.outgoing
            text "Missed {call.time}" x=50 y=27 width=150 style=phone_preview lines=1 when=call.missed
            text "Busy {call.time}" x=50 y=27 width=150 style=phone_preview lines=1 when=call.busy
            text "{call.length}" x=200 y=16 width=66 style=phone_preview_right
    container app=people title="People" x=962 y=46 width=276 height=394
        text "People" x=4 y=0 width=268 style=phone_title
        text "No people" x=0 y=190 width=276 style=phone_preview_centre when="people_total is 0"
        container list=people x=0 y=34 width=276 height=360 item_height=66 spacing=6 scroll=yes
            container x=0 y=0 width=276 height=66 style=phone_card
            image "{person.avatar}" x=10 y=14 width=38 height=38 fit=cover when=person.avatar
            image "{person.colour}" x=10 y=14 width=38 height=38 when="not person.avatar"
            text "{person.initial}" x=10 y=14 width=38 height=38 style=phone_initial when="not person.avatar"
            container x=58 y=centre width=128 fit=parts stack=down
                text "{colour person.colour}{person.name}{/colour}" width=128 style=phone_name lines=1
                text "{person.about}" width=128 style=phone_preview lines=1 when=person.about
            button "Chat" x=192 y=6 width=74 height=26 style=phone_small_button action="open chat {person.name}"
            button "Call" x=192 y=34 width=74 height=26 style=phone_small_button action="phone call {person.name}"
    container app=settings title="Settings" x=962 y=46 width=276 height=394
        text "Settings" x=4 y=0 width=268 style=phone_title
        text "Owner" x=10 y=44 width=256 style=phone_label
        input variable=phone_owner x=10 y=64 width=256 height=38 most_letters=20 style=phone_input
        text "Passcode" x=10 y=114 width=256 style=phone_label
        input variable=phone_passcode x=10 y=134 width=256 height=38 most_letters=6 style=phone_input
        text "Home screen wallpaper" x=10 y=184 width=256 style=phone_label
        button "None" x=10 y=204 width=60 height=34 style=phone_small_button action="set phone_wallpaper to ''" chosen="not phone_wallpaper"
        button "Night" x=75 y=204 width=60 height=34 style=phone_small_button action="set phone_wallpaper to '#1b2140'" chosen="phone_wallpaper is '#1b2140'"
        button "Dusk" x=140 y=204 width=60 height=34 style=phone_small_button action="set phone_wallpaper to '#4a2c4f'" chosen="phone_wallpaper is '#4a2c4f'"
        button "Sea" x=205 y=204 width=61 height=34 style=phone_small_button action="set phone_wallpaper to '#1f4f5f'" chosen="phone_wallpaper is '#1f4f5f'"
        text "Lock screen wallpaper" x=10 y=250 width=256 style=phone_label
        button "None" x=10 y=270 width=60 height=34 style=phone_small_button action="set phone_lock_wallpaper to ''" chosen="not phone_lock_wallpaper"
        button "Night" x=75 y=270 width=60 height=34 style=phone_small_button action="set phone_lock_wallpaper to '#1b2140'" chosen="phone_lock_wallpaper is '#1b2140'"
        button "Dusk" x=140 y=270 width=60 height=34 style=phone_small_button action="set phone_lock_wallpaper to '#4a2c4f'" chosen="phone_lock_wallpaper is '#4a2c4f'"
        button "Sea" x=205 y=270 width=61 height=34 style=phone_small_button action="set phone_lock_wallpaper to '#1f4f5f'" chosen="phone_lock_wallpaper is '#1f4f5f'"
    container app=call x=962 y=46 width=276 height=394
        image "{caller_avatar}" x=83 y=40 width=110 height=110 fit=cover when=caller_avatar
        image "{caller_colour}" x=83 y=40 width=110 height=110 when="not caller_avatar"
        text "{caller_initial}" x=83 y=40 width=110 height=110 style=phone_caller_initial when="not caller_avatar"
        text "{caller}" x=0 y=164 width=276 style=phone_caller
        text "Incoming call" x=0 y=198 width=276 style=phone_preview_centre when=call_incoming
        text "Calling..." x=0 y=198 width=276 style=phone_preview_centre when=call_outgoing
        text "Busy" x=0 y=198 width=276 style=phone_preview_centre when=call_busy
        text "{call_time}" x=0 y=198 width=276 style=phone_preview_centre when=call_time
        button "Decline" x=17 y=322 width=112 height=44 style=phone_decline action="decline call" when=call_incoming
        button "Answer" x=147 y=322 width=112 height=44 style=phone_answer action="answer call" when=call_incoming
        container list=options x=17 y=226 width=242 height=88 item_height=34 spacing=6 scroll=yes when=call_choice
            button "{option.text}" x=0 y=0 width=242 height=34 style=phone_small_button action=choose clickable=option.on
        button "Hang up" x=82 y=322 width=112 height=44 style=phone_decline action="hang up" when=call_choice
    container app=lock x=962 y=46 width=276 height=394
        button x=0 y=0 width=276 height=394 style=phone_photo action=unlock when="not phone_needs_code"
        text "{phone_owner}" x=0 y=4 width=276 style=phone_label_centre lines=1 when=phone_owner
        text "{phone_clock}" x=0 y=24 width=276 style=phone_clock
        text "{phone_date}" x=0 y=86 width=276 style=phone_label_centre when=phone_date
        container list=notices x=10 y=118 width=256 height=210 spacing=6 when="not phone_needs_code"
            button "{notice.who}: {notice.text}" x=0 y=0 width=256 height=34 style=phone_notice action="unlock; open chat {notice.chat}" when="not notice.call"
            button "Missed call from {notice.who}" x=0 y=0 width=256 height=34 style=phone_notice action="unlock; open app calls" when=notice.call
        text "Click to unlock" x=0 y=356 width=276 style=phone_label_centre when="not phone_needs_code"
        text "{phone_code}" x=0 y=118 width=276 style=phone_title_centre when=phone_needs_code
        text "Wrong code" x=0 y=150 width=276 style=phone_label_centre when="phone_needs_code and phone_wrong"
        button "1" x=32 y=178 width=64 height=44 style=phone_small_button action="keypad 1" when=phone_needs_code
        button "2" x=106 y=178 width=64 height=44 style=phone_small_button action="keypad 2" when=phone_needs_code
        button "3" x=180 y=178 width=64 height=44 style=phone_small_button action="keypad 3" when=phone_needs_code
        button "4" x=32 y=230 width=64 height=44 style=phone_small_button action="keypad 4" when=phone_needs_code
        button "5" x=106 y=230 width=64 height=44 style=phone_small_button action="keypad 5" when=phone_needs_code
        button "6" x=180 y=230 width=64 height=44 style=phone_small_button action="keypad 6" when=phone_needs_code
        button "7" x=32 y=282 width=64 height=44 style=phone_small_button action="keypad 7" when=phone_needs_code
        button "8" x=106 y=282 width=64 height=44 style=phone_small_button action="keypad 8" when=phone_needs_code
        button "9" x=180 y=282 width=64 height=44 style=phone_small_button action="keypad 9" when=phone_needs_code
        button "0" x=106 y=334 width=64 height=44 style=phone_small_button action="keypad 0" when=phone_needs_code
        button "<" x=180 y=334 width=64 height=44 style=phone_small_button action="keypad back" when=phone_needs_code
    container x=962 y=46 width=276 height=394 style=phone_viewer when=open_image
        image "{open_image}" x=4 y=4 width=268 height=340 fit=contain
        text "{open_image_text}" x=4 y=350 width=268 style=phone_preview_centre lines=2
        button x=0 y=0 width=276 height=394 style=phone_photo action=back
    button "Back" x=1104 y=446 width=134 height=30 style=small_button action=back keys="escape pad_b" when="not in_call and not in_chat")vnext";

// The calendar: as each new day starts (setting time), the date and the time on a card, until a click (on the shade,
// a button) or a key, or a moment has gone by.
constexpr std::string_view kCalendarScreen = R"vnext(screen calendar close_after=2.5
    button x=0 y=0 width=1280 height=720 style=shaded action=close keys="space return escape pad_a pad_b"
    container x=340 y=260 width=600 height=200 style=card
        text "{time_date}" x=0 y=30 width=600 height=80 style=calendar
        text "{time_clock}" x=0 y=120 width=600 style=calendar_time)vnext";

// Input: a question, a box the player types the answer into (it goes in the variable its variable= names), and OK
// (Enter in the box too). The story opens it (open input) and waits until it's closed.
constexpr std::string_view kInputScreen = R"vnext(screen input
    image shade x=0 y=0 width=1280 height=720
    image panel x=340 y=220 width=600 height=280 style=framed
    text "What's your name?" x=360 y=250 width=560 style=centred
    input x=440 y=320 width=400 height=56 variable=name most_letters=24 style=input action=close
    button "OK" x=570 y=420 width=140 height=44 style=small_button action=close)vnext";

constexpr std::string_view kCreditsScreen = R"vnext(screen credits
    image backdrop x=0 y=0 width=1280 height=720
    text "Credits" x=60 y=32 width=600 style=screen_title
    text "Written by you" x=0 y=330 width=1280 style=centred
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

} // namespace

namespace dev {

std::vector<std::string> settingsScreen(const std::function<bool(std::string_view)>& uses, bool phone, bool timed, std::span<const std::string> controls) {
    struct Row {
        std::string_view label, setting, choices; // "Label:value|..." for buttons, "slider", or "arrows" (bigger/smaller, next/previous)
        bool shown = true;
        std::string_view more = {};               // on the row
    };
    struct Group {
        std::string_view heading;
        std::vector<Row> rows;
    };
    constexpr std::string_view onOff = "On:yes|Off:no";
    const std::vector<Group> columns[]{
        {{"Display", {{"Window", "fullscreen", "Windowed:no|Fullscreen:yes"}, {"Window size", "window_size", "arrows"}}},
         {"Text",
          {{"Text speed", "text_speed", "slider"}, {"Auto delay", "auto_delay", "slider"}, {"Textbox opacity", "textbox_opacity", "slider"},
           {"Language", "language", "arrows", true, " when=\"languages is more than 1\""}}},
         {"Accessibility",
          {{"Text size", "text_size", "slider"}, {"High contrast", "high_contrast", onOff}, {"Read aloud", "read_aloud", onOff},
           {"Captions", "captions", onOff, uses("caption ") || uses("movie")}, {"Animations", "animations", onOff},
           {"Time limits", "time_limits", onOff, timed}, {"Vibration", "vibration", onOff, phone}}}},
        {{"Sound",
          {{"Master volume", "master_volume", "slider"}, {"Music volume", "music_volume", "slider"}, {"Sound volume", "sound_volume", "slider"},
           {"Voice volume", "voice_volume", "slider", uses("voice") || uses("made_up_voice=")}, {"Made-up voices", "made_up_voices", onOff, uses("made_up_voice=")},
           {"Typing sounds", "typing_sounds", onOff, uses("typing_sound=")}, {"Mouths move", "mouths", "Always:yes|With a voice:voice|Never:no", uses("mouth")}}},
         {"Skip",
          {{"Skip", "skip_unread", "Read text:no|All text:yes"}, {"After choices", "skip_after_choices", "Stop skipping:no|Keep skipping:yes"},
           {"Skip movies", "skip_movies", onOff, uses("movie")}, {"Rollback", "rollback", onOff}}}},
    };
    std::vector<std::string> lines{"screen settings", "    image backdrop x=0 y=0 width=1280 height=720", "    text \"Settings\" x=60 y=32 width=600 style=screen_title"};
    constexpr int labelW = 180, controlX = 190, controlW = 342, rowH = 32, keyW = (controlW - 6) / 2; // (keyW: a key's box, its keyboard key's and its controller button's)
    const std::size_t first = lines.size(); // (the groups' lines, from here)
    int ends[2]{};                          // (where each column's groups end)
    for (int c = 0; c < 2; ++c) {
        const int x = c == 0 ? 60 : 680;
        int& y = ends[c] = 92;
        for (const Group& g : columns[c]) {
            lines.push_back(std::format("    text \"{}\" x={} y={} width={} style=heading", g.heading, x, y, controlX + controlW));
            y += 38;
            for (const Row& row : g.rows) {
                if (!row.shown) continue;
                // (its parts on the screen; a row shown only sometimes: in a container that says when)
                const bool sometimes = !row.more.empty();
                const int rx = sometimes ? 0 : x, ry = sometimes ? 0 : y;
                const std::string_view in = sometimes ? "        " : "    ";
                if (sometimes) lines.push_back(std::format("    container x={} y={} width={} height={}{}", x, y, controlX + controlW, rowH, row.more));
                lines.push_back(std::format("{}text \"{}\" x={} y={} width={} height={} style=label", in, row.label, rx, ry, labelW, rowH));
                if (row.choices == "slider") { // (its value after it, in its unit; the text speed's fastest, At once)
                    lines.push_back(std::format("{}slider setting={} x={} y={} width={} height={}", in, row.setting, rx + controlX, ry, controlW - 92, rowH));
                    const std::string_view unit = row.setting == "text_speed" || row.setting == "auto_delay" ? "" : "%";
                    const std::string_view when = row.setting == "text_speed" ? " when=\"text_speed is not 0\"" : "";
                    lines.push_back(std::format("{}text \"{{{}}}{}\" x={} y={} width=80 height={} style=setting_value{}", in, row.setting, unit, rx + controlX + controlW - 80, ry, rowH, when));
                    if (row.setting == "text_speed")
                        lines.push_back(std::format("{}text \"At once\" x={} y={} width=80 height={} style=setting_value when=\"text_speed is 0\"", in, rx + controlX + controlW - 80, ry, rowH));
                } else if (row.choices == "arrows") {
                    const bool size = row.setting == "window_size";
                    const std::string_view down = size ? "smaller" : "previous", up = size ? "bigger" : "next";
                    const std::string_view enabled = size ? " clickable=\"not fullscreen\"" : "";
                    lines.push_back(std::format("{}button \"<\" x={} y={} width=40 height={} style=setting action=\"setting {} {}\"{}", in, rx + controlX, ry, rowH, row.setting, down, enabled));
                    lines.push_back(std::format("{}text \"{{{}}}\" x={} y={} width={} height={} style=setting_choice", in, row.setting, rx + controlX + 46, ry, controlW - 92, rowH));
                    lines.push_back(std::format("{}button \">\" x={} y={} width=40 height={} style=setting action=\"setting {} {}\"{}", in, rx + controlX + controlW - 40, ry, rowH, row.setting, up, enabled));
                } else {
                    std::vector<std::pair<std::string_view, std::string_view>> options; // (label, value)
                    for (auto&& one : std::views::split(row.choices, '|')) {
                        const std::string_view o(one.begin(), one.end());
                        options.emplace_back(o.substr(0, o.find(':')), o.substr(o.find(':') + 1));
                    }
                    const int n = static_cast<int>(options.size()), w = (controlW - 6 * (n - 1)) / n;
                    for (int i = 0; i < n; ++i)
                        lines.push_back(std::format("{}button \"{}\" x={} y={} width={} height={} style=setting action=\"setting {} {}\"", in, options[static_cast<std::size_t>(i)].first,
                                                    rx + controlX + i * (w + 6), ry, w, rowH, row.setting, options[static_cast<std::size_t>(i)].second));
                }
                y += 36;
            }
            y += 8;
        }
    }
    if (!controls.empty()) {
        const int c = ends[0] <= ends[1] ? 0 : 1, x = c == 0 ? 60 : 680;
        int& y = ends[c];
        lines.push_back(std::format("    text \"Keys\" x={} y={} width={} style=heading", x, y, controlX + controlW));
        y += 38;
        for (const std::string& control : controls) {
            std::string label = control == "menu" ? std::string("Pause menu") : control; // (the pause menu's is menu; the rest as they're called)
            std::ranges::replace(label, '_', ' ');
            label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            lines.push_back(std::format("    text \"{}\" x={} y={} width={} height={} style=label", label, x, y, labelW, rowH));
            lines.push_back(std::format("    control {} x={} y={} width={} height={} waiting=\"Press a key\"", control, x + controlX, y, keyW, rowH));
            lines.push_back(std::format("    control {} slot=pad x={} y={} width={} height={} waiting=\"Press a button\"", control, x + controlX + keyW + 6, y, keyW, rowH));
            y += 36;
        }
        y += 8;
    }
    constexpr int top = 92, bottom = 646; // (the room under the title, down to the buttons)
    if (std::max(ends[0], ends[1]) - 12 > bottom) { // (a column's last row past it: the groups in a box that scrolls, its bar at the screen's edge)
        for (std::size_t i = first; i < lines.size(); ++i) {
            std::string& line = lines[i];
            if (const auto at = line.find(" y="); indentOf(line) == 4 && at != npos) { // (a row of its own: from the box's top; one in a container stays)
                const auto end = line.find(' ', at + 3);
                line.replace(at + 3, end - at - 3, std::to_string(std::stoi(line.substr(at + 3, end - at - 3)) - top));
            }
            line.insert(0, "    ");
        }
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(first), std::format("    container x=0 y={} width=1280 height={} scroll=yes scrollbar=yes", top, bottom - top));
    }
    lines.insert(lines.end(), {"    button \"Reset to defaults\" x=860 y=654 width=200 height=40 style=small_button action=\"reset settings\" "
                               "confirm=\"Reset all settings to their defaults?\"",
                               "    button \"Back\" x=1080 y=654 width=140 height=40 style=small_button action=close keys=\"escape pad_b\""});
    return lines;
}

std::string replaced(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
    return text;
}

std::vector<std::string> startScreen(std::string_view name, const std::string& as, const std::function<bool(std::string_view)>& has,
                                     const std::function<bool(std::string_view)>& uses, bool timed, std::span<const std::string> controls) {
    struct Item {
        std::string_view label, action, more, needs;
    };
    auto buttons = [&](float y, float step, float h, std::initializer_list<Item> items) {
        std::vector<std::string> out;
        for (const Item& item : items) {
            if (!item.needs.empty() && !has(item.needs)) continue;
            out.push_back(std::format("    button \"{}\" x=460 y={} width=360 height={} action={}{}", item.label, y, h, item.action, item.more));
            y += step;
        }
        return out;
    };
    std::vector<std::string> lines;
    if (name == "title") {
        lines = {"screen title", "    image backdrop x=0 y=0 width=1280 height=720", "    text \"{title}\" x=60 y=32 width=1160 style=title"};
        std::ranges::copy(buttons(180, 62, 52, {{"New game", "start", "", ""},
                                                {"Continue", "continue", " clickable=has_save", ""},
                                                {"Load", "\"open load\"", " clickable=has_save", "load"},
                                                {"Settings", "\"open settings\"", "", "settings"},
                                                {"Extras", "\"open extras\"", "", "extras"},
                                                {"Credits", "\"open credits\"", "", "credits"},
                                                {"Quit", "quit", "", ""}}),
                          std::back_inserter(lines));
    } else if (name == "pause") {
        lines = {"screen pause", "    image shade x=0 y=0 width=1280 height=720", "    text \"Paused\" x=60 y=32 width=600 style=screen_title"};
        std::ranges::copy(buttons(148, 62, 52, {{"Resume", "close", " keys=\"escape mouse_right pad_b pad_start\"", ""},
                                                {"Save", "\"open save\"", " clickable=can_save", "save"},
                                                {"Load", "\"open load\"", " clickable=has_save", "load"},
                                                {"History", "\"open history\"", "", "history"},
                                                {"Settings", "\"open settings\"", "", "settings"},
                                                {"Main menu", "\"open title\"", "", "title"},
                                                {"Quit", "quit", "", ""}}),
                          std::back_inserter(lines));
    } else if (name == "save" || name == "load") {
        const bool save = name == "save";
        std::string text = replaced(std::string(kSlotsScreen), "NAME", name);
        text = replaced(replaced(text, "TITLE", save ? "Save" : "Load"), "MODE", name);
        text = replaced(text, "TABS", save ? "" // (loading: the Auto and Quick pages first)
                                           : "\n    button \"Auto\" x=200 y=30 width=76 height=34 style=tab action=\"page auto\" chosen=\"page is 'auto'\""
                                             "\n    button \"Quick\" x=282 y=30 width=76 height=34 style=tab action=\"page quick\" chosen=\"page is 'quick'\"");
        text = replaced(text, "PAGES", save ? "200" : "364");
        text = replaced(text, "QUESTION", save ? "Overwrite this save?" : "Load this save? Progress since your last save will be lost.");
        text = replaced(text, "CLICKABLE", save ? "" : " clickable=save.used");
        lines = linesOf(text);
    } else if (name == "settings") {
        lines = settingsScreen(uses, has("phone"), timed, controls);
    } else if (name == "history") {
        lines = linesOf(kHistoryScreen);
    } else if (name == "confirm") {
        lines = linesOf(kConfirmScreen);
    } else if (builtIn(name)) { // (the built-in one)
        bool in = false;
        for (std::string& line : linesOf(defaultUi())) {
            if (indentOf(line) == 0) in = lower(line).starts_with("screen " + std::string(name));
            if (in && !trim(line).empty()) lines.push_back(std::move(line));
        }
    } else if (name == "credits") {
        lines = linesOf(kCreditsScreen);
    } else if (name == "extras") { // (a button for each extras screen the game has)
        lines = {"screen extras", "    image backdrop x=0 y=0 width=1280 height=720", "    text \"Extras\" x=60 y=32 width=600 style=screen_title"};
        std::ranges::copy(buttons(148, 62, 52, {{"Gallery", "\"open gallery\"", "", "gallery"},
                                                {"Music room", "\"open music_room\"", "", "music_room"},
                                                {"Endings", "\"open endings\"", "", "endings"},
                                                {"Achievements", "\"open achievements\"", "", "achievements"},
                                                {"Tips", "\"open tips\"", "", "tips"},
                                                {"Flowchart", "\"open flowchart\"", "", "flowchart"}}),
                          std::back_inserter(lines));
        lines.emplace_back("    button \"Back\" x=1080 y=654 width=140 height=40 style=small_button action=close keys=\"escape pad_b\"");
    } else if (name == "gallery") {
        lines = linesOf(kGalleryScreen);
    } else if (name == "gallery_viewer") {
        lines = linesOf(kGalleryViewerScreen);
    } else if (name == "music_room") {
        lines = linesOf(kMusicRoomScreen);
    } else if (name == "endings") {
        lines = linesOf(kEndingsScreen);
    } else if (name == "achievements") {
        lines = linesOf(kAchievementsScreen);
    } else if (name == "tips") {
        lines = linesOf(kTipsScreen);
    } else if (name == "flowchart") {
        lines = linesOf(kFlowchartScreen);
    } else if (name == "calendar") {
        lines = linesOf(kCalendarScreen);
    } else if (name == "input") {
        lines = linesOf(kInputScreen);
    } else if (name == "phone") {
        lines = linesOf(kPhoneScreen);
    } else { // (an empty one: nothing in it)
        lines = {"screen " + as};
    }
    const std::string_view settings = afterFirst(afterFirst(lines.front())); // (called `as`)
    lines.front() = "screen " + as + (settings.empty() ? "" : " " + std::string(settings));
    return lines;
}

std::vector<std::string> ownScreenLines(std::string_view name, float sy, bool phone, bool pause) {
    static constexpr std::pair<std::string_view, std::string_view> kNamed[]{{"#141230fa", "panel"}, {"#5b51b8", "line"}, {"#ada2ff", "accent"},
                                                                            {"#2b2563", "button"}, {"#3b3392", "pointed"}, {"#4d41c4", "chosen"}};
    std::vector<std::string> lines;
    bool in = false;
    std::optional<int> leftOut; // (a line left out: the lines in it, deeper, go too)
    for (std::string& line : linesOf(defaultUi())) {
        if (indentOf(line) == 0) in = lower(line).starts_with("screen " + lower(name));
        if (leftOut && !trim(line).empty() && indentOf(line) <= *leftOut) leftOut.reset();
        if (in && ((!phone && line.contains("phone_button")) || (!pause && line.contains("menu_button")))) leftOut = indentOf(line);
        if (!in || trim(line).empty() || leftOut) continue;
        std::string out = scaled(line, sy);
        for (const auto& [code, named] : kNamed)
            for (auto at = out.find(code); at != npos; at = out.find(code, at)) out.replace(at, code.size(), named);
        lines.push_back(std::move(out));
    }
    return lines;
}

std::vector<std::string> needsOf(const StoryEditor& editor, const std::vector<std::string>& lines, std::string_view screen) {
    std::map<std::string, std::string, std::less<>> colours, styles;
    for (const std::string& line : linesOf(kLook)) {
        auto& to = line.starts_with("colour ") ? colours : styles;
        to.emplace(std::string(firstWord(afterFirst(line))), line);
    }
    std::vector<std::string> wanted, out, used = lines;
    for (const std::string& line : lines) {
        const std::string kind = lower(firstWord(line));
        for (const auto& [part, style] : {std::pair{"button", "button"}, {"dialogue", "dialogue"}, {"flowchart", "flowchart"}})
            if (kind == part) wanted.emplace_back(style);
        for (const std::string& word : rawWords(line))
            if (const auto eq = word.find('='); eq != std::string::npos && (word.substr(0, eq) == "style" || word.substr(0, eq).ends_with("_style")))
                wanted.push_back(unquote(word.substr(eq + 1)));
    }
    std::set<std::string> added;
    for (std::size_t i = 0; i < wanted.size(); ++i) { // (parents join the list)
        const auto it = styles.find(wanted[i]);
        if (it == styles.end() || added.contains(wanted[i]) || editor.defines("style", wanted[i])) continue;
        added.insert(wanted[i]);
        out.push_back(it->second);
        used.push_back(it->second);
        for (const std::string& word : rawWords(it->second))
            if (word.starts_with("based_on=")) wanted.push_back(word.substr(9));
    }
    std::vector<std::string> colourLines;
    std::set<std::string> coloursAdded;
    for (const std::string& line : used) // (an image of a colour: image panel ...)
        if (lower(firstWord(trim(line))) == "image")
            if (const auto c = colours.find(unquote(firstWord(afterFirst(trim(line))))); c != colours.end() && !coloursAdded.contains(c->first) && !editor.defines("colour", c->first)) {
                coloursAdded.insert(c->first);
                colourLines.push_back(c->second);
            }
    for (const std::string& line : used)
        for (const std::string& word : rawWords(line))
            if (const auto eq = word.find('='); eq != std::string::npos)
                if (const auto c = colours.find(unquote(word.substr(eq + 1)));
                    c != colours.end() && !coloursAdded.contains(c->first) && !editor.defines("colour", c->first)) {
                    coloursAdded.insert(c->first);
                    colourLines.push_back(c->second);
                }
    out.insert(out.begin(), colourLines.begin(), colourLines.end());
    for (const auto& [name, control] : kScreenControls)
        if (name == screen && !editor.defines("control", firstWord(afterFirst(control)))) out.emplace_back(control);
    return out;
}

} // namespace dev

// The size each text in `lines` without a height of its own will have (its words as its style draws them,
// wrapped to its width; a {value}'s as one line), in the pixels `lines` are written in, by line: for
// placesFromPixels. `needs`: what they bring (their styles), as the game will have them; `scale`: the game's
// pixels for one of `lines`'. None when the screen doesn't read as one (the scripts' mistakes).
std::map<std::size_t, SDL_FPoint> Developer::ownSizes(const std::vector<std::string>& lines, const std::vector<std::string>& needs, float width, float height, float scale) {
    std::map<std::size_t, SDL_FPoint> out;
    // (as the game will read them: at the end of main.vnscript, after what they need, the screen under a name of its own)
    std::string more = "\n";
    for (const std::string& line : needs) more += line + "\n";
    std::vector<std::size_t> parts; // (the screen's part lines, in the order its parts are read)
    const std::vector<std::string> placed = placesFromPixels(lines, width, height);
    bool inScreen = false;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        if (!partLine(placed[i])) continue;
        if (indentOf(placed[i]) == 0) {
            if (inScreen) break; // (one screen)
            inScreen = lower(firstWord(placed[i])) == "screen";
            if (inScreen) more += "screen vnext_sizes\n";
            continue;
        }
        if (!inScreen) continue;
        parts.push_back(i);
        more += scaled(placed[i], scale) + "\n";
    }
    const Script script = Script::load(
        "scripts/main.vnscript",
        [&](const std::string& p) {
            auto text = readTextFile(engine.gameRoot() + p);
            if (text && lower(p) == "scripts/main.vnscript") *text += more;
            return text;
        },
        defaultUi());
    const auto it = script.screens.find("vnext_sizes");
    if (it == script.screens.end() || it->second.elements.size() != parts.size()) return out;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        const Element& el = it->second.elements[k];
        const auto words = rawWords(trim(lines[parts[k]]));
        auto option = [&](std::string_view key) -> std::optional<float> {
            for (const std::string& word : words)
                if (lower(word).starts_with(std::string(key) + "=")) return parseNumber(unquote(std::string_view(word).substr(key.size() + 1)));
            return std::nullopt;
        };
        if (el.kind != ElementKind::Text || option("height")) continue;
        // (its words without their tags and {values}: a value, as one line of letters)
        std::string letters;
        for (std::size_t at = 0; at < el.text.size();) {
            const std::size_t open = el.text.find('{', at);
            letters += el.text.substr(at, open == std::string::npos ? std::string::npos : open - at);
            if (open == std::string::npos) break;
            const std::size_t close = el.text.find('}', open);
            at = close == std::string::npos ? el.text.size() : close + 1;
        }
        if (trim(letters).empty()) letters = "Ag";
        const Style& s = el.style;
        const float across = option("width").value_or(option("width_at_most").value_or(0.0f)) * scale;
        TextBlock block;
        block.set(engine.font(s.font, s.size), letters, across > 0.0f ? std::max(1.0f, across - 2 * s.padding) : 0.0f, s.lineSpacing);
        SDL_FPoint size = block.size();
        if (const auto most = option("lines"); most && *most >= 1.0f && static_cast<float>(block.lines()) > *most)
            size.y = size.y * *most / static_cast<float>(block.lines());
        out[parts[k]] = {(size.x + 2 * s.padding) / scale, (size.y + 2 * s.padding) / scale};
    }
    return out;
}

} // namespace vn
