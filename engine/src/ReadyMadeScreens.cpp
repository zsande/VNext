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
colour heart #ff7eb6
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
style hearts size=30 colour=heart shadow=no
style hearts_empty based_on=hearts colour=line
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
// leave a game going on (Load). TABS: Load's Auto and Quick.
constexpr std::string_view kSlotsScreen = R"vnext(screen NAME
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        container width=1 fit=parts stack=across spacing_across=16px spacing_down=8px
            text "TITLE" style=screen_titleTABS
            container list=pages width=444px fit=parts item_width=44px item_height=34px spacing=6px
                text "{page.name}" x=0 y=0 width=1 height=1 style=tab action="page {page.name}" chosen=page.open
        container list=saves item_width=360px item_height=262px spacing_across=40px spacing_down=16px scroll=yes
            text x=0 y=0 width=1 height=1 style=slot action="MODE {save.number}" confirm="QUESTION"CLICKABLE
            image "{save.image}" x=0 y=0.875 width=0.9778 height=0.7557 when=save.image
            image field x=0 y=0.875 width=0.9778 height=0.7557 when="not save.image"
            container x=0 y=-0.95 width=0.9444 fit=parts stack=down
                text "{save.number}  ·  {save.time}" width=1 style=slot_title when=save.used
                text "{save.number}" width=1 style=slot_empty when="not save.used"
                text "{save.text}" width=1 style=slot_text lines=1 when=save.used
            text "Delete" x=0.9291 y=0.9145 width=78px height=28px style=small_button action="delete save {save.number}" confirm="Delete this save?" when=save.used
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

// The history: the lines read, the newest at the bottom (it opens there; turned on down past it, it closes), each with
// who said it, and buttons to hear its voice again and to go back to it.
constexpr std::string_view kHistoryScreen = R"vnext(screen history
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "History" width=1 style=screen_title
        container list=history spacing_down=18px position=bottom scroll=yes scrollbar=yes close_at_end=yes
            container width=0.9828 fit=parts stack=across spacing_across=10px spacing_down=4px
                text "{colour line.colour}{line.name}{/colour}" width=220px style=history_name
                text "{line.text}" width=720px style=history when="not line.choice"
                text "{line.text}" width=720px style=history_choice when=line.choice
                text "Replay" width=84px height=30px style=history_button action="replay {line.number}" when=line.voiced
                text "Go back" width=84px height=30px style=history_button action="rollback {line.number}" when=line.back
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape l pad_b pad_x")vnext";

// The extras: the gallery (a picture clicked: big, in its viewer), the music room (a song clicked plays; clicked
// again, it stops), the endings and achievements reached, the tips (one clicked: its words beside the list) and the
// flowchart, each in the Save and History screens' look; what isn't reached yet says ???.
constexpr std::string_view kGalleryScreen = R"vnext(screen gallery
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "Gallery" width=1 style=screen_title
        container list=gallery item_width=370px item_height=208px spacing_across=25px spacing_down=24px scroll=yes scrollbar=yes
            text x=0 y=0 width=1 height=1 style=gallery action="open image {image.name}; open gallery_viewer" clickable="not image.locked"
            image "{image.name}" x=0 y=0 width=0.9892 height=0.9808 fit=cover volume=0% when="not image.locked"
            image panel x=0 y=-0.9767 width=0.9892 height=36px when="not image.locked"
            text "{image.title}" x=0 y=-0.9767 width=0.9892 height=36px style=gallery_title lines=1 when="not image.locked"
            text "???" x=0 y=0 width=1 height=1 style=locked when=image.locked
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kGalleryViewerScreen = R"vnext(screen gallery_viewer
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "{open_image_text}" width=1 style=screen_title lines=1
        container
            image "{open_image}" x=0 y=0 width=1 height=1 fit=contain
        container x=1 fit=parts stack=across spacing=10px
            text "Previous" width=140px height=40px style=small_button action="previous image" keys="left pad_left"
            text "Next" width=140px height=40px style=small_button action="next image" keys="right pad_right"
            text "Back" width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kMusicRoomScreen = R"vnext(screen music_room
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "Music room" width=1 style=screen_title
        container list=songs item_height=60px spacing_down=10px scroll=yes scrollbar=yes
            text "{song.title}" x=-1 y=0 width=0.9828 height=1 style=record action="play song {song.name}" chosen=song.playing when="not song.locked"
            text "???" x=-1 y=0 width=0.9828 height=1 style=record_locked clickable=no when=song.locked
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kEndingsScreen = R"vnext(screen endings
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "Endings" width=1 style=screen_title
        container list=endings item_height=80px spacing_down=10px scroll=yes scrollbar=yes
            container x=-1 y=0 width=0.9828 height=1 style=record stack=across when="not ending.locked"
                container width=16px
                container stack=down position=centre
                    text "{ending.number}. {ending.title}" width=1 style=record_title lines=1
                container width=16px
            container x=-1 y=0 width=0.9828 height=1 style=record_locked stack=across when=ending.locked
                container width=16px
                container stack=down position=centre
                    text "{ending.number}. ???" width=1 style=record_title lines=1
                    text "{ending.text}" width=1 style=record_text lines=1
                container width=16px
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

// The love interests (character ... love=), each with its love as hearts: five dim ones, and over them one for each
// point of love it has (five at most).
constexpr std::string_view kRelationshipsScreen = R"vnext(screen relationships
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "Relationships" width=1 style=screen_title
        container list=loves item_height=96px spacing_down=10px scroll=yes scrollbar=yes
            container x=-1 y=0 width=0.9828 height=1 style=record stack=across
                container width=16px
                container stack=down position=centre
                    text "{love.name}" width=1 style=record_title lines=1
                    container width=1 height=36px
                        text "♥♥♥♥♥" style=hearts_empty
                        text "{love.hearts}" style=hearts
                image "{love.avatar}" y=0 width=72px height=72px fit=contain when=love.avatar
                container width=12px
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kAchievementsScreen = R"vnext(screen achievements
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "Achievements" width=1 style=screen_title
        container list=achievements item_height=80px spacing_down=10px scroll=yes scrollbar=yes
            container x=-1 y=0 width=0.9828 height=1 style=record stack=across when="not achievement.locked"
                container width=16px
                container stack=down position=centre
                    text "{achievement.title}" width=1 style=record_title lines=1
                    text "{achievement.text}" width=1 style=record_text lines=1
                container width=16px
            container x=-1 y=0 width=0.9828 height=1 style=record_locked stack=across when="achievement.locked and not achievement.secret"
                container width=16px
                container stack=down position=centre
                    text "{achievement.title}" width=1 style=record_title lines=1
                    text "{achievement.text}" width=1 style=record_text lines=1
                container width=16px
            container x=-1 y=0 width=0.9828 height=1 style=record_locked stack=across when="achievement.locked and achievement.secret"
                container width=16px
                container stack=down position=centre
                    text "???" width=1 style=record_title
                    text "???" width=1 style=record_text
                container width=16px
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kTipsScreen = R"vnext(screen tips
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "Tips" width=1 style=screen_title
        container stack=across spacing_across=40px spacing_down=16px
            container list=tips width=440px height=1 item_height=60px spacing_down=10px scroll=yes scrollbar=yes
                text "{tip.title}" x=-1 y=0 width=0.9545 height=1 style=record action="open tip {tip.name}" chosen=tip.open when="not tip.locked"
                text "???" x=-1 y=0 width=0.9545 height=1 style=record_locked clickable=no when=tip.locked
            container width=680px fit=parts stack=down spacing=22px
                text "{tip_title}" width=1 style=heading
                text "{tip_text}" width=1 style=record_title
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kFlowchartScreen = R"vnext(screen flowchart
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "Flowchart" width=1 style=screen_title
        container
            flowchart x=0 y=0 width=1 height=1 unreached_style=flowchart_unreached ending_style=flowchart_ending choice_style=flowchart_choice scrollbar=yes confirm="Go back to this point? Progress since your last save will be lost."
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

// The question (confirm=): asked by the keys or a controller, the ring's on No at once (Enter and A press the one it's
// on); Esc and B say no, as the developer tools' own questions have it.
constexpr std::string_view kConfirmScreen = R"vnext(screen confirm
    image shade x=0 y=0 width=1 height=1
    container x=0 y=0 width=660px height=300px
        image panel x=0 y=0 width=1 height=1 style=framed
        text "{question}" x=0 y=0.6364 width=0.9394 height=0.7067 style=centred
        container x=0.8919 y=-0.877 fit=parts stack=across spacing=10px
            text "Yes" width=140px height=40px style=small_button action=yes
            text "No" width=140px height=40px style=small_button action=no keys="escape pad_b")vnext";

// The phone: a box of its own size at the side (the story goes on while it's open), above the textbox, made of
// containers like any screen; its parts in it, placed from its top left. Along its top its clock, signal and battery; in it its apps (each a container with app=: the home
// screen with an icon for each, then each app's parts; its lists, list=, show a part once for each chat,
// message, call...; the call and lock screens), the image viewer over them, and a Back button: back from an
// image, a chat, an app, then the phone itself. With it, an app for each of the game's screens it opens (kGalleryApp...).
constexpr std::string_view kPhoneScreen = R"vnext(screen phone story=goes_on
    container x=950 y=12 width=300px height=472px
        image panel x=0 y=0 width=300 height=472 style=framed
        text "{phone_clock}" x=12 y=8 width=100 height=22 style=phone_status
        text "No signal" x=116 y=8 width=126 height=22 style=phone_status_right when="not phone_signal"
        image text x=224 y=21 width=3 height=4 when=phone_signal
        image text x=229 y=19 width=3 height=6 when=phone_signal
        image text x=234 y=17 width=3 height=8 when=phone_signal
        image text x=239 y=15 width=3 height=10 when=phone_signal
        input x=250 y=14 width=24 height=10 value=phone_battery track=#ffffff30 fill=#f0f0f5 when="phone_battery is more than 20"
        input x=250 y=14 width=24 height=10 value=phone_battery track=#ffffff30 fill=#ff8c96 when="phone_battery is at most 20"
        image text x=274 y=17 width=2 height=4
        image "{phone_wallpaper}" x=12 y=34 width=276 height=394 fit=cover when="phone_wallpaper and phone_app is 'home'"
        image "{phone_lock_wallpaper}" x=12 y=34 width=276 height=394 fit=cover when="phone_lock_wallpaper and phone_app is 'lock'"
        container app=home x=12 y=34 width=276 height=394
            container list=apps x=8 y=10 width=260 height=376 columns=3 item_height=96 spacing=10 scroll=yes
                text "{app.initial}" x=11 y=4 width=58 height=58 style=phone_icon action="open app {app.name}"
                image "{app.icon}" x=11 y=4 width=58 height=58 fit=cover when=app.icon
                text "{app.title}" x=0 y=76 width=80 style=phone_label_centre lines=1
                container x=56 y=0 width=22 height=18 style=phone_unread when=app.count
                    text "{app.count}" x=0 y=0 width=22 height=18 style=phone_unread
        container app=chats title="Chats" x=12 y=34 width=276 height=394
            text "Chats" x=4 y=0 width=268 style=phone_title when="not open_chat"
            text "{open_chat}" x=4 y=0 width=268 style=phone_title when=open_chat
            text "No chats" x=0 y=190 width=276 style=phone_preview_centre when="not open_chat and chats_total is 0"
            text "No messages" x=0 y=190 width=276 style=phone_preview_centre when="open_chat and open_chat_messages is 0"
            container list=chats x=0 y=34 width=276 height=360 item_height=58 spacing=6 scroll=yes when="not open_chat"
                text x=0 y=0 width=276 height=58 style=phone_card action="open chat {chat.name}" chosen=chat.open
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
                                text x=0 y=0 width=160 height=120 style=phone_photo action="open image {message.image}"
                            text "Play the voice message" width=196 height=34 style=phone_small_button action="play {message.sound} as voice" when=message.sound
                        image "{message.image}" width=96 height=96 fit=contain when=message.sticker
                    container x=right fit=parts stack=down style=phone_mine when="message.mine and not message.sticker"
                        text "{message.text}" width_at_most=196 style=phone_mine when=message.text
                        text "{italic}Message deleted{/italic}" style=phone_mine when=message.deleted
                        container width=160 height=120 when=message.image
                            image "{message.image}" x=0 y=0 width=160 height=120 fit=cover
                            text x=0 y=0 width=160 height=120 style=phone_photo action="open image {message.image}"
                        text "Play the voice message" width=196 height=34 style=phone_small_button action="play {message.sound} as voice" when=message.sound
                    image "{message.image}" x=right width=96 height=96 fit=contain when="message.mine and message.sticker"
                    container x=34 fit=parts style=phone_reaction when="message.reaction and not message.mine"
                        text "{message.reaction}" style=phone_reaction
                    container x=right fit=parts style=phone_reaction when="message.reaction and message.mine"
                        text "{message.reaction}" style=phone_reaction
                    text "Edited" x=34 style=phone_time when="message.edited and not message.mine"
                    text "Edited" x=right style=phone_time when="message.edited and message.mine"
                    text "Seen" x=right style=phone_time when=message.seen
                input x=right width=196 height=4 value=time_left track=#ffffff28 fill=accent when=time_left
                container list=options x=right width=196 fit=parts spacing=6
                    text "{option.text}" x=0 y=0 width=196 height=34 style=phone_small_button action=choose clickable=option.on
        container app=calls title="Calls" x=12 y=34 width=276 height=394
            text "Calls" x=4 y=0 width=268 style=phone_title
            text "No calls" x=0 y=190 width=276 style=phone_preview_centre when="calls_total is 0"
            container list=calls x=0 y=34 width=276 height=360 item_height=52 spacing=6 scroll=yes
                text x=0 y=0 width=276 height=52 style=phone_card action="phone call {call.who}"
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
        container app=people title="People" x=12 y=34 width=276 height=394
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
                text "Chat" x=192 y=6 width=74 height=26 style=phone_small_button action="open chat {person.name}"
                text "Call" x=192 y=34 width=74 height=26 style=phone_small_button action="phone call {person.name}"
        container app=settings title="Settings" x=12 y=34 width=276 height=394
            text "Settings" x=4 y=0 width=268 style=phone_title
            text "Owner" x=10 y=44 width=256 style=phone_label
            input variable=phone_owner x=10 y=64 width=256 height=38 most_letters=20 style=phone_input
            text "Passcode" x=10 y=114 width=256 style=phone_label
            input variable=phone_passcode x=10 y=134 width=256 height=38 most_letters=6 style=phone_input
            text "Home screen wallpaper" x=10 y=184 width=256 style=phone_label
            text "None" x=10 y=204 width=60 height=34 style=phone_small_button action="set phone_wallpaper to ''" chosen="not phone_wallpaper"
            text "Night" x=75 y=204 width=60 height=34 style=phone_small_button action="set phone_wallpaper to '#1b2140'" chosen="phone_wallpaper is '#1b2140'"
            text "Dusk" x=140 y=204 width=60 height=34 style=phone_small_button action="set phone_wallpaper to '#4a2c4f'" chosen="phone_wallpaper is '#4a2c4f'"
            text "Sea" x=205 y=204 width=61 height=34 style=phone_small_button action="set phone_wallpaper to '#1f4f5f'" chosen="phone_wallpaper is '#1f4f5f'"
            text "Lock screen wallpaper" x=10 y=250 width=256 style=phone_label
            text "None" x=10 y=270 width=60 height=34 style=phone_small_button action="set phone_lock_wallpaper to ''" chosen="not phone_lock_wallpaper"
            text "Night" x=75 y=270 width=60 height=34 style=phone_small_button action="set phone_lock_wallpaper to '#1b2140'" chosen="phone_lock_wallpaper is '#1b2140'"
            text "Dusk" x=140 y=270 width=60 height=34 style=phone_small_button action="set phone_lock_wallpaper to '#4a2c4f'" chosen="phone_lock_wallpaper is '#4a2c4f'"
            text "Sea" x=205 y=270 width=61 height=34 style=phone_small_button action="set phone_lock_wallpaper to '#1f4f5f'" chosen="phone_lock_wallpaper is '#1f4f5f'"
        container app=call x=12 y=34 width=276 height=394
            image "{caller_avatar}" x=83 y=40 width=110 height=110 fit=cover when=caller_avatar
            image "{caller_colour}" x=83 y=40 width=110 height=110 when="not caller_avatar"
            text "{caller_initial}" x=83 y=40 width=110 height=110 style=phone_caller_initial when="not caller_avatar"
            text "{caller}" x=0 y=164 width=276 style=phone_caller
            text "Incoming call" x=0 y=198 width=276 style=phone_preview_centre when=call_incoming
            text "Calling..." x=0 y=198 width=276 style=phone_preview_centre when=call_outgoing
            text "Busy" x=0 y=198 width=276 style=phone_preview_centre when=call_busy
            text "{call_time}" x=0 y=198 width=276 style=phone_preview_centre when=call_time
            text "Decline" x=17 y=322 width=112 height=44 style=phone_decline action="decline call" when=call_incoming
            text "Answer" x=147 y=322 width=112 height=44 style=phone_answer action="answer call" when=call_incoming
            container list=options x=17 y=226 width=242 height=88 item_height=34 spacing=6 scroll=yes when=call_choice
                text "{option.text}" x=0 y=0 width=242 height=34 style=phone_small_button action=choose clickable=option.on
            text "Hang up" x=82 y=322 width=112 height=44 style=phone_decline action="hang up" when=call_choice
        container app=lock x=12 y=34 width=276 height=394
            text x=0 y=0 width=276 height=394 style=phone_photo action=unlock when="not phone_needs_code"
            text "{phone_owner}" x=0 y=4 width=276 style=phone_label_centre lines=1 when=phone_owner
            text "{phone_clock}" x=0 y=24 width=276 style=phone_clock
            text "{phone_date}" x=0 y=86 width=276 style=phone_label_centre when=phone_date
            container list=notices x=10 y=118 width=256 height=210 spacing=6 when="not phone_needs_code"
                text "{notice.who}: {notice.text}" x=0 y=0 width=256 height=34 style=phone_notice action="unlock; open chat {notice.chat}" when="not notice.call"
                text "Missed call from {notice.who}" x=0 y=0 width=256 height=34 style=phone_notice action="unlock; open app calls" when=notice.call
            text "Click to unlock" x=0 y=356 width=276 style=phone_label_centre when="not phone_needs_code"
            text "{phone_code}" x=0 y=118 width=276 style=phone_title_centre when=phone_needs_code
            text "Wrong code" x=0 y=150 width=276 style=phone_label_centre when="phone_needs_code and phone_wrong"
            text "1" x=32 y=178 width=64 height=44 style=phone_small_button action="keypad 1" when=phone_needs_code
            text "2" x=106 y=178 width=64 height=44 style=phone_small_button action="keypad 2" when=phone_needs_code
            text "3" x=180 y=178 width=64 height=44 style=phone_small_button action="keypad 3" when=phone_needs_code
            text "4" x=32 y=230 width=64 height=44 style=phone_small_button action="keypad 4" when=phone_needs_code
            text "5" x=106 y=230 width=64 height=44 style=phone_small_button action="keypad 5" when=phone_needs_code
            text "6" x=180 y=230 width=64 height=44 style=phone_small_button action="keypad 6" when=phone_needs_code
            text "7" x=32 y=282 width=64 height=44 style=phone_small_button action="keypad 7" when=phone_needs_code
            text "8" x=106 y=282 width=64 height=44 style=phone_small_button action="keypad 8" when=phone_needs_code
            text "9" x=180 y=282 width=64 height=44 style=phone_small_button action="keypad 9" when=phone_needs_code
            text "0" x=106 y=334 width=64 height=44 style=phone_small_button action="keypad 0" when=phone_needs_code
            text "<" x=180 y=334 width=64 height=44 style=phone_small_button action="keypad back" when=phone_needs_code
        container x=12 y=34 width=276 height=394 style=phone_viewer when=open_image
            image "{open_image}" x=4 y=4 width=268 height=340 fit=contain
            text "{open_image_text}" x=4 y=350 width=268 style=phone_preview_centre lines=2
            text x=0 y=0 width=276 height=394 style=phone_photo action=back
        text "Back" x=154 y=434 width=134 height=30 style=small_button action=back keys="escape pad_b" when="not in_call and not in_chat")vnext";

// The phone's apps for the game's other screens, each for one it has, showing it in the app (laid out to fit there):
// the gallery's and the music room's; a journal with Save and Load (a tab for each, with both); and in its settings,
// the game's (Game) beside the phone's own (Phone).
constexpr std::string_view kGalleryApp = R"vnext(        container app=gallery title="Gallery" screen=gallery x=12 y=34 width=276 height=394)vnext";
constexpr std::string_view kMusicApp = R"vnext(        container app=music title="Music" screen=music_room x=12 y=34 width=276 height=394)vnext";
constexpr std::string_view kTabs = R"vnext(            container x=0 width=276 fit=parts stack=across spacing=6)vnext";
constexpr std::string_view kPhoneSettings = R"vnext(            container x=0 width=276 height=358 when="phone_settings is 'phone'"
                text "Owner" x=10 y=8 width=256 style=phone_label
                input variable=phone_owner x=10 y=28 width=256 height=38 most_letters=20 style=phone_input
                text "Passcode" x=10 y=78 width=256 style=phone_label
                input variable=phone_passcode x=10 y=98 width=256 height=38 most_letters=6 style=phone_input
                text "Home screen wallpaper" x=10 y=148 width=256 style=phone_label
                text "None" x=10 y=168 width=60 height=34 style=phone_small_button action="set phone_wallpaper to ''" chosen="not phone_wallpaper"
                text "Night" x=75 y=168 width=60 height=34 style=phone_small_button action="set phone_wallpaper to '#1b2140'" chosen="phone_wallpaper is '#1b2140'"
                text "Dusk" x=140 y=168 width=60 height=34 style=phone_small_button action="set phone_wallpaper to '#4a2c4f'" chosen="phone_wallpaper is '#4a2c4f'"
                text "Sea" x=205 y=168 width=61 height=34 style=phone_small_button action="set phone_wallpaper to '#1f4f5f'" chosen="phone_wallpaper is '#1f4f5f'"
                text "Lock screen wallpaper" x=10 y=214 width=256 style=phone_label
                text "None" x=10 y=234 width=60 height=34 style=phone_small_button action="set phone_lock_wallpaper to ''" chosen="not phone_lock_wallpaper"
                text "Night" x=75 y=234 width=60 height=34 style=phone_small_button action="set phone_lock_wallpaper to '#1b2140'" chosen="phone_lock_wallpaper is '#1b2140'"
                text "Dusk" x=140 y=234 width=60 height=34 style=phone_small_button action="set phone_lock_wallpaper to '#4a2c4f'" chosen="phone_lock_wallpaper is '#4a2c4f'"
                text "Sea" x=205 y=234 width=61 height=34 style=phone_small_button action="set phone_lock_wallpaper to '#1f4f5f'" chosen="phone_lock_wallpaper is '#1f4f5f'")vnext";

// A tab in one of the phone's apps: chosen while `variable` is `value` (the first, while it isn't the other's).
std::string phoneTab(std::string_view label, std::string_view variable, std::string_view value, std::string_view other) {
    return std::format("                text \"{}\" width=135 height=30 style=phone_small_button action=\"set {} to '{}'\" chosen=\"{} is {}'{}'\"", label, variable, value, variable,
                       other.empty() ? "" : "not ", other.empty() ? value : other);
}

// The calendar: as each new day starts (setting time), the date and the time on a card, until a click (on the shade,
// a button) or a key, or a moment has gone by.
constexpr std::string_view kCalendarScreen = R"vnext(screen calendar close_after=2.5
    text x=0 y=0 width=1 height=1 style=shaded action=close keys="space return escape pad_a pad_b"
    container x=0 y=0 width=600px height=200px style=card
        text "{time_date}" x=0 y=0.5 width=1 height=80px style=calendar
        text "{time_clock}" x=0 y=-0.4 width=1 style=calendar_time)vnext";

// Input: a question, a box the player types the answer into (it goes in the variable its variable= names), and OK
// (Enter in the box too). The story opens it (open input) and waits until it's closed.
constexpr std::string_view kInputScreen = R"vnext(screen input
    image shade x=0 y=0 width=1 height=1
    container x=0 y=0 width=600px height=280px
        image panel x=0 y=0 width=1 height=1 style=framed
        text "What's your name?" x=0 y=0.762 width=0.9333 style=centred
        input x=0 y=0.1 width=400px height=56px variable=name most_letters=24 style=input action=close
        text "OK" x=0 y=-0.695 width=140px height=44px style=small_button action=close)vnext";

constexpr std::string_view kCreditsScreen = R"vnext(screen credits
    image backdrop x=0 y=0 width=1 height=1
    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px
        text "Credits" width=1 style=screen_title
        container
            text "Written by you" x=0 y=0 width=1 style=centred
        text "Back" x=1 width=140px height=40px style=small_button action=close keys="escape pad_b")vnext";

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
    // Laid out to fit any box: two columns of groups side by side, one under the other where there's no room (a phone's
    // app); in each row, a label and its control after it, or under it; the groups scroll under the title when they're
    // longer than the room.
    constexpr std::string_view group = "                    ", row = "                        ", part = "                            ", in = "                                ";
    auto controlOf = [&](const Row& r) { // (its parts, in its control's box)
        std::vector<std::string> out;
        if (r.choices == "slider") { // (its value after it, in its unit; the text speed's fastest, At once)
            const std::string_view unit = r.setting == "text_speed" || r.setting == "auto_delay" ? "" : "%";
            out.push_back(std::format("{}input setting={} x=-1 y=0 width=0.7212 height=1", in, r.setting));
            out.push_back(std::format("{}text \"{{{}}}{}\" x=1 y=0 width=0.2424 height=1 style=setting_value{}", in, r.setting, unit,
                                      r.setting == "text_speed" ? " when=\"text_speed is not 0\"" : ""));
            if (r.setting == "text_speed") out.push_back(std::format("{}text \"At once\" x=1 y=0 width=0.2424 height=1 style=setting_value when=\"text_speed is 0\"", in));
        } else if (r.choices == "arrows") {
            const bool size = r.setting == "window_size";
            const std::string_view down = size ? "smaller" : "previous", up = size ? "bigger" : "next", enabled = size ? " clickable=\"not fullscreen\"" : "";
            out.push_back(std::format("{}text \"<\" x=-1 y=0 width=40px height=1 style=setting action=\"setting {} {}\"{}", in, r.setting, down, enabled));
            out.push_back(std::format("{}text \"{{{}}}\" x=0 y=0 width=0.7212 height=1 style=setting_choice", in, r.setting));
            out.push_back(std::format("{}text \">\" x=1 y=0 width=40px height=1 style=setting action=\"setting {} {}\"{}", in, r.setting, up, enabled));
        } else {
            std::vector<std::pair<std::string_view, std::string_view>> options; // (label, value)
            for (auto&& one : std::views::split(r.choices, '|')) {
                const std::string_view o(one.begin(), one.end());
                options.emplace_back(o.substr(0, o.find(':')), o.substr(o.find(':') + 1));
            }
            const auto n = static_cast<int>(options.size());
            const float w = (330.0f - 6.0f * static_cast<float>(n - 1)) / static_cast<float>(n) / 330.0f; // (a share of the control, 6 pixels apart)
            for (int i = 0; i < n; ++i)
                out.push_back(std::format("{}text \"{}\" x={} y=0 width={} height=1 style=setting action=\"setting {} {}\"", in, options[static_cast<std::size_t>(i)].first,
                                          unitText(n == 1 ? 0.0f : -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(n - 1)), unitText(w), r.setting,
                                          options[static_cast<std::size_t>(i)].second));
        }
        return out;
    };
    auto rowOf = [&](std::string_view label, std::string_view more, const std::vector<std::string>& control) {
        std::vector<std::string> out{std::format("{}container width=1 fit=parts stack=across spacing_across=10px spacing_down=4px{}", row, more),
                                     std::format("{}text \"{}\" width=180px height=32px style=label", part, label),
                                     std::format("{}container width=330px height=32px", part)};
        std::ranges::copy(control, std::back_inserter(out));
        return out;
    };
    std::vector<std::string> blocks[2]; // (each column's lines)
    int counts[2]{};                    // (and how many rows and headings it has: Keys go in the shorter)
    for (int c = 0; c < 2; ++c)
        for (const Group& g : columns[c]) {
            blocks[c].push_back(std::format("{}container width=1 fit=parts stack=down spacing=4px", group));
            blocks[c].push_back(std::format("{}text \"{}\" width=1 style=heading", row, g.heading));
            ++counts[c];
            for (const Row& r : g.rows)
                if (r.shown) {
                    std::ranges::copy(rowOf(r.label, r.more, controlOf(r)), std::back_inserter(blocks[c]));
                    ++counts[c];
                }
        }
    if (!controls.empty()) { // (a key to change for each control: its keyboard key's, then its controller button's)
        auto& keys = blocks[counts[0] <= counts[1] ? 0 : 1];
        keys.push_back(std::format("{}container width=1 fit=parts stack=down spacing=4px", group));
        keys.push_back(std::format("{}text \"Keys\" width=1 style=heading", row));
        for (const std::string& control : controls) {
            std::string label = control == "menu" ? std::string("Pause menu") : control; // (the pause menu's is menu; the rest as they're called)
            std::ranges::replace(label, '_', ' ');
            label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            std::ranges::copy(rowOf(label, "", {std::format("{}input control={} x=-1 y=0 width=0.4909 height=1 waiting=\"Press a key\"", in, control),
                                               std::format("{}input control={} slot=pad x=1 y=0 width=0.4909 height=1 waiting=\"Press a button\"", in, control)}),
                              std::back_inserter(keys));
        }
    }
    std::vector<std::string> lines{"screen settings", "    image backdrop x=0 y=0 width=1 height=1",
                                   "    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px", "        text \"Settings\" width=1 style=screen_title",
                                   "        container scroll=yes scrollbar=yes", "            container width=1 fit=parts stack=across spacing_across=88px spacing_down=12px"};
    for (const auto& block : blocks) {
        lines.emplace_back("                container width=520px fit=parts stack=down spacing=12px");
        std::ranges::copy(block, std::back_inserter(lines));
    }
    lines.insert(lines.end(), {"        container x=1 fit=parts stack=across spacing=20px",
                               "            text \"Reset to defaults\" width=200px height=40px style=small_button action=\"reset settings\" confirm=\"Reset all settings to their defaults?\"",
                               "            text \"Back\" width=140px height=40px style=small_button action=close keys=\"escape pad_b\""});
    return lines;
}

std::string replaced(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
    return text;
}

std::span<const MenuItem> menuItems(std::string_view menu) {
    static constexpr MenuItem kTitle[]{{"New game", "start", "", ""},
                                       {"Continue", "continue", "clickable=has_save", ""},
                                       {"Load", "open load", "clickable=has_save", "load"},
                                       {"Settings", "open settings", "", "settings"},
                                       {"Extras", "open extras", "", "extras"},
                                       {"Credits", "open credits", "", "credits"},
                                       {"Quit", "quit", "", ""}};
    static constexpr MenuItem kPause[]{{"Resume", "close", "keys=\"escape mouse_right pad_b pad_start\"", ""},
                                       {"Save", "open save", "clickable=can_save", "save"},
                                       {"Load", "open load", "clickable=has_save", "load"},
                                       {"History", "open history", "", "history"},
                                       {"Relationships", "open relationships", "", "relationships"},
                                       {"Settings", "open settings", "", "settings"},
                                       {"Main menu", "open title", "", "title"},
                                       {"Quit", "quit", "", ""}};
    static constexpr MenuItem kExtras[]{{"Gallery", "open gallery", "", "gallery"},       {"Music room", "open music_room", "", "music_room"},
                                        {"Endings", "open endings", "", "endings"},       {"Achievements", "open achievements", "", "achievements"},
                                        {"Tips", "open tips", "", "tips"},                {"Flowchart", "open flowchart", "", "flowchart"}};
    if (menu == "title") return kTitle;
    if (menu == "pause") return kPause;
    if (menu == "extras") return kExtras;
    return {};
}

std::string menuButton(const MenuItem& item) {
    const std::string action = item.action.contains(' ') ? std::format("\"{}\"", item.action) : std::string(item.action);
    return std::format("text \"{}\" x=0 width=1 height=52px action={}{}{}", item.label, action, item.more.empty() ? "" : " ", item.more);
}

std::vector<std::string> startScreen(std::string_view name, const std::string& as, const std::function<bool(std::string_view)>& has,
                                     const std::function<bool(std::string_view)>& uses, bool timed, std::span<const std::string> controls) {
    // A menu: its title, and its buttons one under another in a box under it (only the ones for the screens there are:
    // one added later goes in among them, and one taken away takes its button with it), the box no wider than the
    // screen's, or than the box the screen's shown in.
    auto menu = [&](std::string_view screen, std::string_view behind, std::string_view title, std::string_view style, std::string_view of) {
        std::vector<std::string> out{std::format("screen {}", screen), std::format("    image {} x=0 y=0 width=1 height=1", behind),
                                     "    container x=0 y=0 width=0.9063 height=0.9111 stack=down spacing=16px", std::format("        text \"{}\" width=1 style={}", title, style),
                                     "        container height=40px", "        container x=0 width=360px fit=parts stack=down spacing=10px"};
        for (const MenuItem& item : menuItems(of))
            if (item.needs.empty() || has(item.needs)) out.push_back("            " + menuButton(item));
        return out;
    };
    std::vector<std::string> lines;
    if (name == "title") {
        lines = menu("title", "backdrop", "{title}", "title", "title");
    } else if (name == "pause") {
        lines = menu("pause", "shade", "Paused", "screen_title", "pause");
    } else if (name == "save" || name == "load") {
        const bool save = name == "save";
        std::string text = replaced(std::string(kSlotsScreen), "NAME", name);
        text = replaced(replaced(text, "TITLE", save ? "Save" : "Load"), "MODE", name);
        text = replaced(text, "TABS", save ? "" // (loading: the Auto and Quick pages first)
                                           : "\n            text \"Auto\" width=76px height=34px style=tab action=\"page auto\" chosen=\"page is 'auto'\""
                                             "\n            text \"Quick\" width=76px height=34px style=tab action=\"page quick\" chosen=\"page is 'quick'\"");
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
    } else if (name == "extras") { // (a button for each extras screen the game has; Back at the bottom)
        lines = menu("extras", "backdrop", "Extras", "screen_title", "extras");
        lines.insert(lines.end(), {"        container", "        text \"Back\" x=1 width=140px height=40px style=small_button action=close keys=\"escape pad_b\""});
    } else if (name == "gallery") {
        lines = linesOf(kGalleryScreen);
    } else if (name == "gallery_viewer") {
        lines = linesOf(kGalleryViewerScreen);
    } else if (name == "music_room") {
        lines = linesOf(kMusicRoomScreen);
    } else if (name == "endings") {
        lines = linesOf(kEndingsScreen);
    } else if (name == "relationships") {
        lines = linesOf(kRelationshipsScreen);
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
    } else if (name == "phone") { // (an app for each of the game's screens it shows: kGalleryApp...)
        lines = linesOf(kPhoneScreen);
        std::vector<std::string> apps;
        if (has("gallery")) apps.emplace_back(kGalleryApp);
        if (has("music_room")) apps.emplace_back(kMusicApp);
        if (const bool save = has("save"), load = has("load"); save || load) { // (its tabs with both)
            apps.emplace_back("        container app=journal title=\"Journal\" x=12 y=34 width=276 height=394 stack=down spacing=6");
            if (save && load) apps.insert(apps.end(), {std::string(kTabs), phoneTab("Save", "phone_journal", "save", "load"), phoneTab("Load", "phone_journal", "load", "")});
            if (save) apps.push_back(std::string("            container screen=save") + (load ? " when=\"phone_journal is not 'load'\"" : ""));
            if (load) apps.push_back(std::string("            container screen=load") + (save ? " when=\"phone_journal is 'load'\"" : ""));
        }
        const auto settingsApp = std::ranges::find_if(lines, [](const std::string& l) { return l.starts_with("        container app=settings "); });
        const auto at = lines.insert(settingsApp, apps.begin(), apps.end()) + static_cast<std::ptrdiff_t>(apps.size());
        if (has("settings")) { // (the game's settings, Game, and the phone's own, Phone: what the app had)
            const auto past = std::find_if(at + 1, lines.end(), [](const std::string& l) { return indentOf(l) <= 8; });
            std::vector<std::string> tabbed{"        container app=settings title=\"Settings\" x=12 y=34 width=276 height=394 stack=down spacing=6", std::string(kTabs),
                                            phoneTab("Game", "phone_settings", "game", "phone"), phoneTab("Phone", "phone_settings", "phone", ""),
                                            "            container screen=settings when=\"phone_settings is not 'phone'\""};
            std::ranges::copy(linesOf(kPhoneSettings), std::back_inserter(tabbed));
            lines.insert(lines.erase(at, past), tabbed.begin(), tabbed.end());
        }
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
        { // (a button's, the line said's and the flowchart's own style: by what each is, as it's written)
            const auto words = rawWords(trim(line));
            const std::string text = words.size() > 1 && !words[1].contains('=') ? unquote(words[1]) : std::string{};
            const ElementKind is = words.empty() ? ElementKind::Container : partKindOf(words[0], text, [&](std::string_view key) {
                return std::ranges::any_of(words, [&](const std::string& w) { return lower(w).starts_with(std::string(key) + "="); });
            });
            if (is == ElementKind::Button) wanted.emplace_back("button");
            if (is == ElementKind::Dialogue) wanted.emplace_back("dialogue");
            if (is == ElementKind::Flowchart) wanted.emplace_back("flowchart");
        }
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
        // (a box showing another screen, screen=, measured as it is: the screen's parts aren't its own)
        std::string own = leading(placed[i]);
        for (const std::string& word : rawWords(trim(placed[i])))
            if (!lower(word).starts_with("screen=")) own += (own.size() == leading(placed[i]).size() ? "" : " ") + word;
        more += scaled(own, scale) + "\n";
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

// A ready-made screen as the game would have it, `name` ("" an empty one; one there already: another, pause_2...): its
// lines as the game has them (its size; the screens there are, for its buttons; the keys there are, for Settings) and
// what they need (styles, colours, its keys; the gallery's viewer with the gallery), before it.
Developer::ReadyMade Developer::readyMade(std::string_view name) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Screen = StoryEditor::Screen;
    auto has = [&](std::string_view n) { return std::ranges::any_of(editor.screens(), [&](const Screen& s) { return lower(s.name) == n; }); };
    auto will = [&](std::string_view n) { return has(n) || n == name; };
    std::vector<std::string> controls; // (the built-in ones, the game's as its files have them now, then the one it comes with: not the tools' own)
    for (const std::string& line : linesOf(defaultUi()))
        if (lower(firstWord(line)) == "control" && !line.contains("action=\"tools ")) controls.push_back(lower(firstWord(afterFirst(line))));
    for (const std::string& control : editor.defined("control")) {
        const Control* made = d.script ? d.script->control(lower(control)) : nullptr;
        if (!std::ranges::contains(controls, lower(control)) && !(made && toolsVerbOf(*made))) controls.push_back(lower(control));
    }
    for (const auto& [screen, line] : kScreenControls)
        if (const std::string control(firstWord(afterFirst(line))); screen == name && !std::ranges::contains(controls, control)) controls.push_back(control);
    const float sy = lookScale(engine.canvas().w, engine.canvas().h);
    const std::string as = uniqueName(name.empty() ? std::string("new_screen") : std::string(name), has);
    const bool timed = d.script && std::ranges::any_of(d.script->code, [](const Instruction& in) { // (a choice or a call with a time limit)
        if (const auto* m = std::get_if<cmd::Menu>(&in.command)) return m->seconds > 0.0f;
        const auto* c = std::get_if<cmd::Call>(&in.command);
        return c && c->seconds > 0.0f;
    });
    auto uses = [this](std::string_view w) { return gameUses(w); };
    std::vector<std::string> lines = startScreen(name, as, will, uses, timed, controls);
    // (the gallery's viewer comes with it, when the game has none: a picture clicked shows there)
    std::vector<std::string> viewer = name == "gallery" && !has("gallery_viewer") ? startScreen("gallery_viewer", "gallery_viewer", will, uses, timed, controls)
                                                                                  : std::vector<std::string>{};
    std::vector<std::string> needs = needsOf(editor, lines, name);
    for (std::string& l : needsOf(editor, viewer, "gallery_viewer"))
        if (!std::ranges::contains(needs, l)) needs.push_back(std::move(l));
    for (std::string& l : needs) l = scaled(l, sy);
    // (written as the game will have them: places, shares and pixels that fit any box, for a 1280 x 720 game, their
    // pixels scaled to its size; the phone, a box of its own size at the side, in pixels: its places and shares fit any size)
    if (name == "phone") lines = placesFromPixels(lines, 1280.0f, 720.0f, ownSizes(lines, needs, 1280.0f, 720.0f, sy));
    for (std::string& l : lines) l = scaled(l, sy);
    if (!viewer.empty()) { // (after what they need, before the gallery)
        needs.emplace_back();
        for (const std::string& l : viewer) needs.push_back(scaled(l, sy));
    }
    return {.as = as, .needs = std::move(needs), .lines = std::move(lines), .controls = std::move(controls), .timed = timed};
}

// Whether the scripts have a word ("movie": a movie in movies/, which plays where an image goes).
bool Developer::gameUses(std::string_view word) {
    if (word == "movie")
        return std::ranges::any_of(kMovieTypes, [&](std::string_view ext) { return !tools.cachedFiles(engine.gameRoot() + "movies/", ext).empty(); });
    return !tools.editor.find(word).empty();
}

// ...made the game's own: then a button for it in the menus that have one for it (the title screen's Load...), as they
// are, and an app for it on the phone. Gives its place among the screens.
std::size_t Developer::addReadyMadeScreen(std::string_view name) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Screen = StoryEditor::Screen;
    const auto [as, needs, lines, controls, timed] = readyMade(name);
    editor.addScreen(needs, lines);
    if (!name.empty() && as == name) // (a button for it in the menus with one for it: not for a second one)
        for (const std::string_view menu : {"title", "pause", "extras"}) {
            const auto items = menuItems(menu);
            const auto at = std::ranges::find(items, name, &MenuItem::needs);
            const auto screen = std::ranges::find_if(editor.screens(), [&](const Screen& s) { return lower(s.name) == menu; });
            if (at == items.end() || screen == editor.screens().end()) continue;
            std::vector<std::string> order;
            for (const MenuItem& item : items) order.emplace_back(item.action);
            editor.addMenuButton(*screen, order, static_cast<std::size_t>(at - items.begin()), std::string(at->label), std::string(at->more));
        }
    if (!name.empty() && as == name) addToPhone(name, [this](std::string_view w) { return gameUses(w); }, timed, controls);
    const auto made = std::ranges::find_if(editor.screens(), [&](const Screen& s) { return lower(s.name) == lower(as); });
    return static_cast<std::size_t>(made - editor.screens().begin());
}

namespace {

// A part's line with another's place and size: an app's, as the phone's others are.
std::string placedLike(const std::string& line, const StoryEditor::Part& like) {
    std::string out = leading(line);
    for (std::string word : rawWords(line)) {
        if (const auto eq = word.find('='); eq != npos)
            if (const std::string key = lower(word.substr(0, eq)); (key == "x" || key == "y" || key == "width" || key == "height") && !like.option(key).empty())
                word = key + "=" + written(like.option(key));
        out += (out.size() == leading(line).size() ? "" : " ") + word;
    }
    return out;
}

} // namespace

// The phone's apps for the game's screens, in their order: a screen made gets its own (the gallery's, the music room's;
// a journal, for Save or Load, when it has none), as the ready-made phone made now would have it, when the game has the
// phone and it has none: among its apps, placed as they are (a phone made otherwise gets none).
void Developer::addToPhone(std::string_view name, const std::function<bool(std::string_view)>& uses, bool timed, std::span<const std::string> controls) {
    StoryEditor& editor = tools.editor;
    using Screen = StoryEditor::Screen;
    using Part = StoryEditor::Part;
    static constexpr std::string_view kApps[]{"chats", "calls", "people", "gallery", "music", "journal", "settings"};
    const std::string_view app = name == "gallery" ? "gallery" : name == "music_room" ? "music" : name == "save" || name == "load" ? "journal" : "";
    const auto phone = std::ranges::find_if(editor.screens(), [](const Screen& s) { return lower(s.name) == "phone"; });
    if (app.empty() || phone == editor.screens().end()) return;
    const Screen screen = *phone;
    const std::vector<Part> parts = editor.parts(screen);
    if (std::ranges::any_of(parts, [&](const Part& p) { return p.kind == "container" && lower(p.option("app")) == app; })) return;
    // (the ready-made phone, made now: its lines for it)
    auto has = [&](std::string_view n) { return std::ranges::any_of(editor.screens(), [&](const Screen& s) { return lower(s.name) == n; }); };
    const float sy = lookScale(engine.canvas().w, engine.canvas().h);
    auto needsFor = [&](const std::vector<std::string>& lines) {
        std::vector<std::string> out = needsOf(editor, lines, "");
        for (std::string& l : out) l = scaled(l, sy);
        return out;
    };
    std::vector<std::string> made = startScreen("phone", "phone", has, uses, timed, controls);
    made = placesFromPixels(made, 1280.0f, 720.0f, ownSizes(made, needsFor(made), 1280.0f, 720.0f, sy));
    for (std::string& l : made) l = scaled(l, sy);
    const auto first = std::ranges::find_if(made, [&](const std::string& l) { return std::ranges::contains(rawWords(l), "app=" + std::string(app)); });
    if (first == made.end()) return;
    std::vector<std::string> block{*first};
    for (auto it = std::next(first); it != made.end() && (blank(*it) || indentOf(*it) > indentOf(*first)); ++it)
        if (!blank(*it)) block.push_back(*it);
    // (after the last of the phone's apps before it in their order, else before the first after it)
    const auto k = std::ranges::find(kApps, app) - std::begin(kApps);
    const Part *before = nullptr, *after = nullptr;
    std::ptrdiff_t beforeAt = -1, afterAt = std::ssize(kApps);
    for (const Part& p : parts)
        if (const std::string a = lower(p.option("app")); p.kind == "container" && std::ranges::contains(kApps, std::string_view(a))) {
            const auto j = std::ranges::find(kApps, std::string_view(a)) - std::begin(kApps);
            if (j < k && j >= beforeAt) before = &p, beforeAt = j;
            if (j > k && j < afterAt) after = &p, afterAt = j;
        }
    const Part* like = before ? before : after;
    if (!like) return;
    block.front() = placedLike(block.front(), *like);
    editor.insertParts(screen, before ? before->end : after->line, *like, block, needsFor(block));
}

} // namespace vn
