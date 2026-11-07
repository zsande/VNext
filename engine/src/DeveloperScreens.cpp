// The Screens tab: the game's screens, each one's parts, a preview to click and drag them in, and a
// part's form.
#include "DeveloperShared.hpp"
#include "DeveloperUi.hpp"

#include <array>
#include <numeric>
#include <ranges>
#include <tuple>

namespace vn {

namespace {

// The Screens tab's screens to start from: the screen's name ("" an empty one to fill in), and what the
// list calls it.
struct ScreenStart {
    std::string_view name;
    ToolsText label;
};

const std::array kScreenStarts{
    ScreenStart{"", "Empty screen"},
    ScreenStart{"title", "Title"},
    ScreenStart{"pause", "Pause"},
    ScreenStart{"save", "Save"},
    ScreenStart{"load", "Load"},
    ScreenStart{"settings", "Settings"},
    ScreenStart{"history", "History"},
    ScreenStart{"confirm", "Confirm"},
    ScreenStart{"choice", "Choice"},
    ScreenStart{"saved", "Saved"},
    ScreenStart{"phone", "Phone"},
    ScreenStart{"credits", "Credits"},
    ScreenStart{"calendar", "Calendar"},
    ScreenStart{"input", "Input"},
};

// The ones that start as built-in screens (the choice menu, the Saved notice): a copy, in places already.
bool builtIn(std::string_view name) { return name == "choice" || name == "saved"; }

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
style history_name based_on=history
style history_choice based_on=history colour=dim
style history_button based_on=small_button
style record background=button border=line padding=16 pointed_background=pointed chosen_background=chosen shadow=no
style record_locked based_on=record colour=dim background=field
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
style namebox size=28 background=button padding=20 colour=speaker opacity_follows=yes shadow=no
style dialogue size=28 text_size_follows=yes shadow=no
style indicator colour=accent shadow=no
style textbox_panel border=line opacity_follows=yes shadow=no
style textbox_button size=20 align=centre align_vertically=centre background=button border=line pointed_background=pointed chosen_background=chosen unclickable_colour=dim shadow=no
style textbox_unread size=15 colour=field align=centre align_vertically=centre shadow=no)vnext";

// The keys that open them.
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kScreenControls{{
    {"pause", R"(control menu keys="escape mouse_right pad_start two_finger_tap" action="open pause")"},
    {"history", R"(control history keys="l pad_x swipe_up" action="open history")"},
    {"phone", R"(control phone keys="p pad_back" action="open phone" when=has_phone)"},
}};

constexpr std::string_view kSlotsScreen = R"vnext(screen NAME
    image backdrop x=0 y=0 width=1280 height=720
    text "TITLE" x=60 y=32 width=600 style=screen_title
    pages x=200 y=30 height=34 item_width=44 wide_tab_width=76 spacing=6 mode=MODE auto=Auto quick=Quick
    slots x=60 y=92 columns=3 item_width=360 item_height=262 spacing_across=40 spacing_down=16 mode=MODE title_style=slot_title text_style=slot_text empty_style=slot_empty confirm="Overwrite this save?" load_confirm="Load this save? Progress since your last save will be lost." delete_confirm="Delete this save?"
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape pad_b")vnext";

constexpr std::string_view kHistoryScreen = R"vnext(screen history
    image backdrop x=0 y=0 width=1280 height=720
    text "History" x=60 y=32 width=600 style=screen_title
    history x=60 y=92 width=1160 height=540 name_width=200 spacing=18 close_at_end=yes scrollbar=yes name_style=history_name text_style=history choice_style=history_choice button_style=history_button
    button "Back" x=1080 y=654 width=140 height=40 style=small_button action=close keys="escape l pad_b pad_x")vnext";

constexpr std::string_view kConfirmScreen = R"vnext(screen confirm
    image shade x=0 y=0 width=1280 height=720
    image panel x=310 y=210 width=660 height=300 style=framed
    text "{question}" x=330 y=226 width=620 height=212 style=centred
    button "Yes" x=660 y=454 width=140 height=40 style=small_button action=yes keys="return pad_a"
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
    bar x=1200 y=26 width=24 height=10 value=phone_battery full_at=100 track=#ffffff30 fill=#f0f0f5 when="phone_battery is more than 20"
    bar x=1200 y=26 width=24 height=10 value=phone_battery full_at=100 track=#ffffff30 fill=#ff8c96 when="phone_battery is at most 20"
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
            bar x=right width=196 height=4 value=time_left full_at=100 track=#ffffff28 fill=accent when=time_left
            container list=options x=right width=196 fit=parts spacing=6
                button "{option.text}" x=0 y=0 width=196 height=34 style=phone_small_button action=send clickable=option.on
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
            button "{option.text}" x=0 y=0 width=242 height=34 style=phone_small_button action=send clickable=option.on
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

// The Settings screen, in two columns: a heading over each group and a row for each setting (a
// container with its name, and buttons or a slider that change it), without the ones for what the
// game doesn't use (`uses`: made-up voices without made_up_voice=, typing sounds without typing_sound=...; `phone`:
// vibration without a phone); then Keys, a row for each of `controls` with its key and its controller button to
// change (click it, then press the new one), at the end of the shorter column. Longer than the room down to the
// buttons at the bottom, the groups scroll under the title.
std::vector<std::string> settingsScreen(const std::function<bool(std::string_view)>& uses, bool phone, std::span<const std::string> controls) {
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
           {"Text size", "text_size", "slider"}, {"Animations", "animations", onOff}, {"Read aloud", "read_aloud", onOff},
           {"High contrast", "high_contrast", onOff}, {"Language", "language", "arrows", true, " when=\"languages is more than 1\""}}}},
        {{"Sound",
          {{"Master volume", "master_volume", "slider"}, {"Music volume", "music_volume", "slider"}, {"Sound volume", "sound_volume", "slider"},
           {"Voice volume", "voice_volume", "slider", uses("voice") || uses("made_up_voice=")}, {"Made-up voices", "made_up_voices", onOff, uses("made_up_voice=")},
           {"Typing sounds", "typing_sounds", onOff, uses("typing_sound=")}, {"Mouths move", "mouths", "Always:yes|With a voice:voice|Never:no", uses("mouth")},
           {"Vibration", "vibration", onOff, phone}}},
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

// A screen to start from, for a 1280 x 720 game, called `as`; `has`: the screens there will be (the
// title's and the pause menu's buttons open those); `uses`: whether the scripts have these words;
// `controls`: the ones there will be (Settings has a key to change for each).
std::vector<std::string> startScreen(std::string_view name, const std::string& as, const std::function<bool(std::string_view)>& has,
                                     const std::function<bool(std::string_view)>& uses, std::span<const std::string> controls) {
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
        lines = linesOf(text);
    } else if (name == "settings") {
        lines = settingsScreen(uses, has("phone"), controls);
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

// A built-in screen (the textbox, the choice menu) as the game's own: its lines as the game shows them, with the
// look's colours by their names (its parts' styles: the look's, which needsOf() brings), for the
// game's screens file. `phone`, `pause`: whether the game has a phone screen and a pause screen (else the
// textbox's Phone button, and what's in it, or its Menu button stays out: it would open nothing).
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

// The look and keys `lines` need that the game lacks: the styles its parts use (by style=, by
// *_style= and by their kind's name) and the colours those use.
std::vector<std::string> needsOf(const StoryEditor& editor, const std::vector<std::string>& lines, std::string_view screen) {
    std::map<std::string, std::string, std::less<>> colours, styles;
    for (const std::string& line : linesOf(kLook)) {
        auto& to = line.starts_with("colour ") ? colours : styles;
        to.emplace(std::string(firstWord(afterFirst(line))), line);
    }
    std::vector<std::string> wanted, out, used = lines;
    for (const std::string& line : lines) {
        const std::string kind = lower(firstWord(line));
        for (const auto& [part, style] : {std::pair{"button", "button"}, {"slots", "slot"}, {"pages", "tab"}, {"history", "history"}, {"choices", "choice"},
                                          {"namebox", "namebox"}, {"dialogue", "dialogue"}, {"indicator", "indicator"}, {"endings", "record"},
                                          {"songs", "record"}, {"achievements", "record"}, {"tips", "record"}, {"flowchart", "flowchart"}})
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

} // namespace

// Screens written in a game's pixels (x and y from the top left of the part they're in: a list's parts, of
// their item's box; or left, centre and right, top and bottom for where its edges line up; w and h in pixels,
// a container without a size as big as what's in it, unless it fits its parts), written in places and shares
// instead: x and y from -1 to 1 in the box a part is in, w and h (and item_width, item_height, spacing,
// spacing_across, spacing_down, name_width, wide_tab_width, width_at_most) shares of it. They show in the same
// places on a `width` x `height` screen.
std::vector<std::string> dev::placesFromPixels(const std::vector<std::string>& lines, float width, float height, const std::map<std::size_t, SDL_FPoint>& sizes) {
    struct Px {
        std::size_t line = 0;
        int indent = 0, parent = -1;
        std::string kind;
        std::optional<float> x, y, w, h;
        SDL_FRect at{};   // on the screen
        SDL_FRect in{};   // ...and where the parts in it are (a list's: its first item's box)
        bool fits = false; // fit=parts: as big as its parts (no size written)
        std::optional<float> placeX, placeY; // x=left|centre|right, y=top|centre|bottom: places as they are
    };
    auto wordOf = [](const std::vector<std::string>& words, std::string_view key) -> std::string {
        for (const std::string& w : words)
            if (lower(w).starts_with(std::string(key) + "=")) return lower(unquote(std::string_view(w).substr(key.size() + 1)));
        return {};
    };
    auto number = [](const std::vector<std::string>& words, std::string_view key) -> std::optional<float> {
        for (const std::string& word : words)
            if (lower(word).starts_with(std::string(key) + "=")) return parseNumber(unquote(std::string_view(word).substr(key.size() + 1)));
        return std::nullopt;
    };
    const SDL_FRect screen{0.0f, 0.0f, width, height};
    std::vector<Px> parts;
    std::vector<int> in; // (the parts a line is in)
    bool inScreen = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (!partLine(line)) continue;
        const int indent = indentOf(line);
        if (indent == 0) {
            inScreen = lower(firstWord(line)) == "screen";
            in.clear();
            continue;
        }
        if (!inScreen) continue;
        while (!in.empty() && parts[static_cast<std::size_t>(in.back())].indent >= indent) in.pop_back();
        const auto words = rawWords(trim(line));
        Px p{.line = i, .indent = indent, .parent = in.empty() ? -1 : in.back(), .kind = lower(words.front()), .x = number(words, "x"),
             .y = number(words, "y"), .w = number(words, "width"), .h = number(words, "height")};
        p.fits = wordOf(words, "fit") == "parts";
        static constexpr std::pair<std::string_view, float> kAcross[]{{"left", -1.0f}, {"centre", 0.0f}, {"right", 1.0f}}, kDown[]{{"top", 1.0f}, {"centre", 0.0f}, {"bottom", -1.0f}};
        for (const auto& [name, place] : kAcross)
            if (wordOf(words, "x") == name) p.placeX = place;
        for (const auto& [name, place] : kDown)
            if (wordOf(words, "y") == name) p.placeY = place;
        const SDL_FRect box = p.parent >= 0 ? parts[static_cast<std::size_t>(p.parent)].in : screen;
        const auto own = sizes.find(i); // (one sized by what it shows: placed by that size, as the game will)
        p.at = {box.x + p.x.value_or(0.0f), box.y + p.y.value_or(0.0f), p.w.value_or(own != sizes.end() ? own->second.x : 0.0f),
                p.h.value_or(own != sizes.end() ? own->second.y : 0.0f)};
        p.in = p.at;
        if (p.kind == "container" && !wordOf(words, "list").empty()) { // (a list's parts: in its first item's box)
            const float columns = std::max(1.0f, number(words, "columns").value_or(1.0f));
            const float gap = number(words, "spacing_across").value_or(number(words, "spacing").value_or(0.0f));
            p.in.w = number(words, "item_width").value_or((p.at.w - gap * (columns - 1.0f)) / columns);
            p.in.h = number(words, "item_height").value_or(p.at.h);
        }
        parts.push_back(std::move(p));
        in.push_back(static_cast<int>(parts.size()) - 1);
    }
    for (std::size_t i = parts.size(); i-- > 0;) { // (a container without a size: as far as what's in it goes)
        Px& c = parts[i];
        if (c.kind != "container" || (c.w && c.h)) continue;
        float right = c.at.x, bottom = c.at.y;
        for (std::size_t j = i + 1; j < parts.size() && parts[j].indent > c.indent; ++j) {
            right = std::max(right, parts[j].at.x + parts[j].at.w);
            bottom = std::max(bottom, parts[j].at.y + parts[j].at.h);
        }
        if (!c.w) c.at.w = c.in.w = std::max(1.0f, right - c.at.x);
        if (!c.h) c.at.h = c.in.h = std::max(1.0f, bottom - c.at.y);
    }
    std::vector<std::string> out = lines;
    for (const Px& p : parts) {
        const SDL_FRect box = p.parent >= 0 ? parts[static_cast<std::size_t>(p.parent)].in : screen;
        std::vector<std::string> words = rawWords(trim(lines[p.line]));
        auto put = [&](std::string_view key, std::optional<std::string> value) {
            const auto it = std::ranges::find_if(words, [&](const std::string& word) { return lower(word).starts_with(std::string(key) + "="); });
            if (!value) {
                if (it != words.end()) words.erase(it);
                return;
            }
            const std::string word = std::format("{}={}", key, *value);
            if (it != words.end()) *it = word;
            else words.push_back(word);
        };
        const bool container = p.kind == "container" && !p.fits; // (one without a size fills its box now: its size is written; one that fits its parts, none)
        if (p.placeX) put("x", unitText(*p.placeX));
        else if (p.x || p.at.x != box.x) put("x", unitText(placeFor(p.at.x - box.x, p.at.w, box.w)));
        if (p.placeY) put("y", unitText(*p.placeY));
        else if (p.y || p.at.y != box.y) put("y", unitText(-placeFor(p.at.y - box.y, p.at.h, box.h)));
        // (a size in a container that's as big as its parts that way: a share of the box that one's in)
        auto shareBox = [&](bool across) {
            int at = p.parent;
            while (at >= 0 && parts[static_cast<std::size_t>(at)].fits && !(across ? parts[static_cast<std::size_t>(at)].w : parts[static_cast<std::size_t>(at)].h))
                at = parts[static_cast<std::size_t>(at)].parent;
            const SDL_FRect b = at >= 0 ? parts[static_cast<std::size_t>(at)].in : screen;
            return std::max(1.0f, across ? b.w : b.h);
        };
        if (p.w || container) put("width", unitText(p.at.w / shareBox(true)));
        if (p.h || container) put("height", unitText(p.at.h / shareBox(false)));
        if (const auto most = number(words, "width_at_most")) { // (a share of the box it's in; one that fits its parts: of the one that's in)
            int at = p.parent;
            while (at >= 0 && parts[static_cast<std::size_t>(at)].fits && !parts[static_cast<std::size_t>(at)].w) at = parts[static_cast<std::size_t>(at)].parent;
            const float wide = at >= 0 ? parts[static_cast<std::size_t>(at)].in.w : screen.w;
            put("width_at_most", unitText(*most / std::max(1.0f, wide)));
        }
        for (const auto& [key, across] : {std::pair{"item_width", true}, {"spacing_across", true}, {"name_width", true}, {"wide_tab_width", true}, {"item_height", false}, {"spacing_down", false}})
            if (const auto v = number(words, key)) put(key, unitText(*v / (across ? box.w : box.h)));
        if (const auto gap = number(words, "spacing")) { // (the same gap across and down: a share of each; a part whose items go one way, its one gap)
            const bool across = partTakes(p.kind, "spacing_across"), down = partTakes(p.kind, "spacing_down");
            if (across && !number(words, "spacing_across")) put("spacing_across", unitText(*gap / box.w));
            if (down && !number(words, "spacing_down")) put("spacing_down", unitText(*gap / box.h));
            put("spacing", across || down ? std::nullopt : std::optional(unitText(*gap / (p.kind == "pages" ? box.w : box.h)))); // (a row of tabs: across)
        }
        out[p.line] = leading(lines[p.line]) + joined(words);
    }
    return out;
}

namespace {

// What a new part of each kind starts as (for a 1280 x 720 game): where and how big the ready-made
// screens have it (the textbox's parts where the built-in textbox has them, lists in the menus'
// list area, small parts in the menus' button column, panels where the question's is), with the
// look's colours by their names and nothing a part doesn't need.
struct PartStart {
    ToolsText kind; // (what its button says)
    std::string_view line;
};
const PartStart kPartBasics[]{
    {"Container", "container x=310 y=210 width=660 height=300"},
    {"Text", "text \"Text\" x=460 y=345 width=360"},
    {"Button", "button \"Button\" x=460 y=334 width=360 height=52"},
    {"Image", "image \"IMAGE\" x=0 y=0 width=1280 height=720 fit=cover"},
    {"Movie", "movie \"MOVIE\" x=0 y=0 width=1280 height=720 fit=cover"},
};
const PartStart kPartValues[]{
    {"Bar", "bar x=460 y=354 width=360 height=12 value=VARIABLE full_at=10"},
    {"Slider", "slider x=460 y=344 width=360 height=32 setting=master_volume"},
    {"Input", "input x=460 y=334 width=360 height=52 variable=name"},
    {"Control", "control next_line x=460 y=344 width=360 height=32 waiting=\"Press a key (Esc keeps it)\""},
};
const PartStart kPartTextbox[]{
    {"Namebox", "namebox x=64 y=470 height=46"},
    {"Dialogue", "dialogue x=72 y=528 width=1136"},
    {"Indicator", "indicator x=1195 y=648 width=18 height=12"},
    {"Face", "face x=64 y=452 width=190 height=232"},
    {"Choices", "choices x=280 y=40 width=720 height=420 item_height=54 spacing=14"},
};
const PartStart kPartLists[]{
    {"Pages", "pages x=200 y=30 height=34 item_width=44 wide_tab_width=76 spacing=6 auto=Auto quick=Quick"},
    {"Slots", "slots x=60 y=92 columns=3 item_width=360 item_height=262 spacing_across=40 spacing_down=16 confirm=\"Overwrite this save?\" load_confirm=\"Load this save? Progress since your last save will be lost.\" delete_confirm=\"Delete this save?\""},
    {"History", "history x=60 y=92 width=1160 height=540 name_width=200 spacing=18 scrollbar=yes"},
    {"Page", "page x=60 y=92 width=1160 height=560 name_width=200 spacing=18"},
};
const PartStart kPartExtras[]{
    {"Gallery", "gallery x=60 y=92 width=1160 height=560 columns=3 item_height=208 spacing=24 locked_style=record_locked scrollbar=yes"},
    {"Endings", "endings x=60 y=92 width=1160 height=560 item_height=80 spacing=10 secret=??? locked_style=record_locked scrollbar=yes"},
    {"Songs", "songs x=60 y=92 width=1160 height=560 item_height=60 spacing=10 locked_style=record_locked scrollbar=yes"},
    {"Achievements", "achievements x=60 y=92 width=1160 height=560 item_height=80 spacing=10 secret=??? locked_style=record_locked scrollbar=yes"},
    {"Tips", "tips x=60 y=92 width=1160 height=560 item_height=60 spacing=10 locked_style=record_locked scrollbar=yes"},
    {"Flowchart", "flowchart x=60 y=92 width=1160 height=560 unreached_style=flowchart_unreached ending_style=flowchart_ending choice_style=flowchart_choice scrollbar=yes confirm=\"Go back to this point? Progress since your last save will be lost.\""},
};
// (where the ready-made phone has them: its parts in its apps' place)
const PartStart kPartPhone[]{
    {"App", "container app=new_app title=\"New app\" x=962 y=46 width=276 height=394"},
    {"List", "container list=chats x=962 y=80 width=276 height=360 item_height=58 spacing=6 scroll=yes"},
};
// New part's groups, as the Story tab's New line has its lines.
const std::pair<ToolsText, std::span<const PartStart>> kParts[]{
    {"Basics", kPartBasics}, {"Values", kPartValues}, {"Textbox", kPartTextbox}, {"Lists", kPartLists}, {"Extras", kPartExtras}, {"Phone", kPartPhone}};

// Each kind of part's own settings, in the form's order (a list's items' size and spacing, its scrollbar
// and how it glides come after them: the ones it takes, partTakes).
struct PartKind {
    std::string_view kind, keys;
};

constexpr PartKind kPartKinds[]{
    {"container", "fit stack scroll scrollbar scrollbar_thickness glide glide_speed glide_distance edges fading_edges list position item_animation columns item_width item_height spacing spacing_across spacing_down app title icon count"},
    {"text", "width_at_most lines"}, {"image", "bottom fit flip repeat scroll scroll_round"}, {"button", "action confirm click_animation keys clickable drag drop"},
    {"namebox", "bar bar_colour"}, {"indicator", "bob bob_speed pause_image"}, {"choices", "position locked_style picked_before_style"},
    {"history", "name_width close_at_end name_style text_style choice_style voice back button_style"}, {"pages", "wide_tab_width mode auto quick"},
    {"slots", "mode delete confirm load_confirm delete_confirm title_style text_style empty_style"},
    {"face", "fit flip"}, {"control", "slot waiting"}, {"gallery", "action title_style locked_style"}, {"page", "name_width name_style text_style"},
    {"input", "variable most_letters placeholder action confirm clickable"}, {"bar", "value full_at vertical track fill"},
    {"slider", "variable setting minimum maximum step clickable action track fill knob"}, {"movie", "fit loop volume"},
    {"endings", "secret title_style text_style locked_style"}, {"songs", "title_style text_style locked_style"},
    {"achievements", "secret title_style text_style locked_style"}, {"tips", "action title_style locked_style unread_style"},
    {"flowchart", "confirm line ending_style unreached_style choice_style"},
};

// The heading a kind of part's own settings are under (a button's: by what they're for).
ToolsGroup partGroup(std::string_view kind, std::string_view key) {
    if (kind == "button") return key == "drag" || key == "drop" ? "Dragging and dropping"_group : "Clicking"_group;
    if (kind == "container") { // (its parts, a list's items, one of the phone's apps, then how it scrolls)
        static constexpr std::string_view kList[]{"list", "position", "item_animation", "columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down"};
        static constexpr std::string_view kApp[]{"app", "title", "icon", "count"};
        if (key == "fit" || key == "stack") return "Parts"_group;
        if (std::ranges::contains(kList, key)) return "List"_group;
        if (std::ranges::contains(kApp, key)) return "App"_group;
        return key == "edges" || key == "fading_edges" ? "Edges"_group : "Scrolling"_group;
    }
    if (kind == "text") return "Words"_group;
    if ((kind == "slider" || kind == "bar") && (key == "track" || key == "fill" || key == "knob")) return "Colours"_group;
    static const std::pair<std::string_view, ToolsGroup> groups[]{
        {"image", "Image"_group}, {"face", "Face"_group}, {"movie", "Playing"_group},
        {"namebox", "Bar"_group}, {"indicator", "Bobbing"_group}, {"choices", "Choices"_group}, {"history", "Lines"_group}, {"pages", "Tabs"_group},
        {"slots", "Saves"_group}, {"control", "Key"_group}, {"gallery", "Images"_group}, {"page", "Lines"_group}, {"input", "Typing"_group},
        {"bar", "Value"_group}, {"slider", "Value"_group}, {"endings", "List"_group}, {"songs", "List"_group}, {"achievements", "List"_group},
        {"tips", "List"_group}, {"flowchart", "Flowchart"_group}};
    const auto it = std::ranges::find(groups, kind, &std::pair<std::string_view, ToolsGroup>::first);
    return it != std::end(groups) ? it->second : "Settings"_group;
}

constexpr std::string_view kListKeys[]{"columns", "item_width", "item_height", "spacing", "spacing_across", "spacing_down"}; // (a list's: its items' size and spacing)
constexpr std::string_view kScrollbarKeys[]{"scrollbar", "scrollbar_thickness"};          // (one that scrolls: its scrollbar)
constexpr std::string_view kGlideKeys[]{"glide", "glide_speed", "glide_distance"};        // (...and how it glides on after a drag)
// A part's settings: its own, a list's, its style and look (as a style's), then when it shows.
std::vector<PartOption> partOptions(std::string_view kind) {
    std::vector<PartOption> out;
    std::string_view last; // (each heading once: on the first of its settings)
    auto option = [&](std::string_view key) {
        const auto known = std::ranges::find(kPartOptions, key, &PartOption::key);
        if (known == std::end(kPartOptions)) return; // (one with no row: bottom=, which the image's box shows as a gradient)
        PartOption o = *known;
        o.group = partGroup(kind, key);
        if (std::ranges::contains(kListKeys, key) && kind != "container" && partTakes(kind, "item_height")) o.group = "Item size"_group; // (else its gap: with its own)
        if ((std::ranges::contains(kScrollbarKeys, key) || std::ranges::contains(kGlideKeys, key)) && kind != "container") o.group = "Scrolling"_group;
        if (o.group == last) o.group = {};
        else last = o.group;
        if (key == "fit" && kind == "image") o.choices = "stretch contain cover original"_choices;
        if (key == "fit" && kind == "container") o.label = "Fit"_text, o.box = Box::ContainerFit, o.choices = "box parts"_choices;
        if (key == "scroll" && kind == "container") o.label = "Scrolls"_text, o.box = Box::YesNo, o.choices = "no yes"_choices;
        if (key == "position" && kind == "container") o.choices = "top centre bottom"_choices, o.labels = "The top|The centre|The bottom, and its end as more comes"_labels;
        if (key == "confirm" && kind == "slots") o.label = "Confirm saving over a save"_text;
        if (key == "confirm" && kind == "flowchart") o.label = "Confirm going back"_text;
        if (key == "action" && kind == "input") o.label = "Action on Enter"_text;
        if (key == "action" && kind == "slider") o.label = "Action on change"_text;
        if (key == "unread_style" && kind == "tips") o.label = "Unread style"_text;
        out.push_back(o);
    };
    if (const auto k = std::ranges::find(kPartKinds, kind, &PartKind::kind); k != std::end(kPartKinds)) {
        for (const std::string_view key : wordsOf(k->keys)) option(key);
        if (kind != "container") // (a container's are among its own, in its order)
            for (const auto& keys : {std::span<const std::string_view>(kListKeys), std::span<const std::string_view>(kScrollbarKeys), std::span<const std::string_view>(kGlideKeys)})
                for (const std::string_view key : keys)
                    if (partTakes(kind, key)) option(key);
    }
    // Its look: its style's alone (Library > Styles); a button, a box, an image or a text looks as its style's
    // Chosen says while what Chosen when checks is true.
    out.push_back({.key = "style", .label = "Style"_text, .box = Box::Style, .hint = "Default"_hint, .group = "Look"_group});
    static constexpr std::string_view kChoosable[]{"button", "container", "image", "text"};
    if (std::ranges::contains(kChoosable, kind)) {
        option("chosen");
        out.back().group = {};
    }
    out.push_back({.key = "when", .label = "Shown when"_text, .box = Box::Condition, .hint = "Always"_hint, .is = 'c'}); // (still its look: when it's there, and how it moves)
    out.push_back({.key = "animation", .label = "Animation"_text, .box = Box::Animation, .hint = "None"_hint});
    out.push_back({.key = "pointed_animation", .label = "Animation when pointed at"_text, .box = Box::Animation, .hint = "None"_hint});
    out.push_back({.key = "message_animation", .label = "Animation on a new message"_text, .box = Box::Animation, .hint = "None"_hint});
    return out;
}

// An image that depends on a condition: {if(CONDITION, 'YES', 'NO')} (written so in the script; in a form,
// the condition as its checks, as every condition's are, and an image for each).
struct ImageBy {
    std::string condition, yes, no;
};

std::optional<ImageBy> imageByOf(std::string_view v) {
    v = trim(v);
    if (!v.starts_with("{if(") || !v.ends_with(")}")) return std::nullopt;
    const Parts args = splitTop(v.substr(4, v.size() - 6), {","});
    if (args.parts.size() != 3) return std::nullopt;
    auto bare = [](std::string_view a) {
        a = trim(a);
        return std::string(a.size() >= 2 && (a.front() == '\'' || a.front() == '"') && a.back() == a.front() ? a.substr(1, a.size() - 2) : a);
    };
    return ImageBy{std::string(trim(args.parts[0])), bare(args.parts[1]), bare(args.parts[2])};
}

// An image box's fields: one image, or (after "depends on a variable" in its list) what it depends on,
// as a condition's checks (a yes or no variable, or "language is English"), then an image for yes and one
// for no. Named `name` (the image, or the one for yes), `name by` (and its checks': `name by is`...) and
// `name no`. Each box is an image's (an image, or a colour).
void addImageFields(std::vector<StoryEditor::Field>& out, const std::string& name, const std::string& label, std::string_view hint, std::string_view value,
                      const std::vector<std::string>& variables) {
    const auto by = imageByOf(value);
    if (!by) {
        out.push_back({.name = name, .value = std::string(value), .label = label, .box = Box::Image, .hint = std::string(hint), .suggest = std::string(kImageBySuggest)});
        return;
    }
    addCheckFields(out, name + " by", "{} depends on"_tr(tr(label)), "None", by->condition, Box::Condition, variables); // (in the tools' language: made of the label's)
    out.push_back({.name = name, .value = by->yes, .label = "{} if yes"_tr(tr(label)), .box = Box::Image, .hint = std::string(hint)});
    out.push_back({.name = name + " no", .value = by->no, .label = "{} if no"_tr(tr(label)), .box = Box::Image, .hint = std::string(hint)});
}

// ...and back: the image, or {if(CONDITION, 'YES', 'NO')} (back to one image when what it depends on is taken away).
std::string imageFrom(const StoryEditor& editor, const std::vector<StoryEditor::Field>& fields, const std::string& name) {
    std::string v(trim(fieldValue(fields, name)));
    if (hasField(fields, name + " by")) {
        const std::string by = conditionFrom(fields, name + " by", editor.variables());
        if (by.empty()) return v;
        return std::format("{{if({}, '{}', '{}')}}", by, v, trim(fieldValue(fields, name + " no")));
    }
    if (v != kImageBy) return v;
    std::string guess; // (the first yes or no variable it could be)
    for (const StoryEditor::Definition& def : editor.definitions("variable"))
        if (const auto w = rawWords(afterFirst(editor.linesOf(def).front())); w.size() > 1 && parseBool(unquote(w[1]))) {
            guess = w[0];
            break;
        }
    if (guess.empty() && !editor.variables().empty()) guess = editor.variables().front();
    return guess.empty() ? std::string{} : std::format("{{if({}, '', '')}}", guess);
}

// A screen's or a part's settings as fields, and back.
std::vector<StoryEditor::Field> optionFields(const StoryEditor& editor, const StoryEditor::Part& p, std::span<const PartOption> options) {
    std::vector<StoryEditor::Field> out;
    for (const PartOption& o : options) {
        const std::string key(o.key), v = p.option(o.key);
        const std::size_t first = out.size();
        std::string hint(o.hint);
        if (key == "chosen" && p.kind == "button") { // (pressed, it looks chosen; one that sets something to a value, while it's so)
            const auto w = rawWords(unquote(p.option("action")));
            hint = w.size() == 4 && lower(w[0]) == "set" && lower(w[2]) == "to" ? "While {} is {}"_tr(w[1], unquote(w[3]))
                 : w.size() == 3 && lower(w[0]) == "setting"                    ? "While {} is {}"_tr(w[1], w[2])
                                                                                : "While pressed"_tr();
        }
        if (o.is == 'a') addActionFields(out, key, o.label, o.hint, v, editor.variables());
        else if (o.is == 'c') addCheckFields(out, key, o.label, hint, v, o.box, editor.variables());
        else if (o.box == Box::Image) addImageFields(out, key, std::string(o.label), o.hint, v, editor.variables());
        else out.push_back({.name = key, .value = o.choices.empty() ? (StoryEditor::kindOf(o.box).percent ? percentText(v) : v) : v.empty() ? std::string(wordsOf(o.choices).front()) : lower(v),
                            .label = std::string(o.label), .box = o.box, .choices = std::string(o.choices), .hint = std::string(o.hint), .labels = std::string(o.labels),
                            .multi = o.box == Box::Key, .suggest = std::string(o.suggest), // (a button's keys: several, as a control's)
                            .usual = o.choices.empty() ? std::string{} : std::string(wordsOf(o.choices).front())});
        if (first < out.size()) out[first].group = o.group;
    }
    return out;
}

StoryEditor::Part withOptions(const StoryEditor& editor, StoryEditor::Part p, const std::vector<StoryEditor::Field>& fields, std::span<const PartOption> options) {
    for (const PartOption& o : options) {
        const std::string key(o.key);
        if (o.is == 'a') {
            p.set(o.key, actionsFrom(fields, key, editor.variables()));
        } else if (o.is == 'c') {
            p.set(o.key, conditionFrom(fields, key, editor.variables()));
        } else if (o.box == Box::Image && hasField(fields, key)) {
            p.set(o.key, imageFrom(editor, fields, key));
        } else if (const auto f = std::ranges::find(fields, key, &StoryEditor::Field::name); f != fields.end()) {
            const std::string v(trim(f->value));
            p.set(o.key, !o.choices.empty() && v == f->usual ? std::string{} : StoryEditor::kindOf(o.box).percent ? percentText(v) : v); // (what it does without one: none)
        }
    }
    return p;
}

// What the editor calls a part: its name (name=), else its kind and text.
// (with `palette`: an image part's colour by what it's called, never its code)
std::string partTitle(const StoryEditor::Part& p, const Palette* palette = nullptr) {
    if (std::string name = p.option("name"); !name.empty()) return name;
    if (p.kind == "container" && !p.option("app").empty()) return std::format("container  app {}", p.option("app"));
    if (p.kind == "container" && !p.option("list").empty()) return std::format("container  list {}", p.option("list"));
    if (p.text.empty()) return p.kind;
    if (palette && p.kind == "image" && parseColour(p.text, palette)) return std::format("{}  {}", p.kind, colourWords(p.text, palette));
    return std::format("{}  {}", p.kind, p.text);
}

// A name as typed, as name= keeps it (without double quotes: one would end it).
std::string partName(std::string_view typed) {
    std::string out(trim(typed));
    std::erase(out, '"');
    return out;
}

// What a part's text (or word) is, by its kind; empty for kinds without one.
std::string_view textLabel(std::string_view kind) {
    if (kind == "text") return "Text"_text;
    if (kind == "button") return "Text"_text;
    if (kind == "image") return "Image or colour"_text;
    if (kind == "movie") return "Movie"_text;
    if (kind == "control") return "Control"_text;
    return {};
}

} // namespace

// The Screens tab as it is this frame: the screen open (and the screens to pick from), its parts and the
// one chosen, and the screen as read. Its parts are drawn by drawScreensList, drawPartsList, drawPreview, drawNewPart
// and drawScreensForm, each in the room drawPanels gives it.
struct Developer::ScreensFrame {
    struct Entry {
        std::string name;
        std::optional<std::size_t> index; // in editor.screens() (none: built in)
    };
    DeveloperUi& ui;
    DeveloperTools& d;
    const std::vector<Entry>& entries;
    const Entry& entry;
    const Entry* chosen; // (the entry open: entry's place in entries)
    const std::optional<StoryEditor::Screen>& screen; // (none: built in)
    const std::vector<StoryEditor::Part>& parts;
    const StoryEditor::Part* selected;
    ScreenDef* def;
    const std::vector<std::optional<std::size_t>>& elementByPart;
    SDL_FRect middle, right; // the room of the part being drawn: the screen's (or New part's), and the form's
    // New part's buttons (Goes in, with a part chosen; then the kinds of parts, in groups), and what Goes in
    // calls the part chosen.
    std::vector<DeveloperUi::ButtonGroup> newParts{};
    std::string intoName{};
    // What the screen's column found, for the form: the parts chosen (in the script's order), and
    // whether the screen can be changed here (the game's own, read without mistakes).
    std::vector<std::size_t> chosenLines{};
    bool editable = false;
    // The screen as the middle column shows it: where (its pixels times `scale`), the mouse over it (in
    // its pixels), the chosen part's element with its handle (that resizes it), and the one pointed at.
    SDL_FRect shown{};
    float scale = 1.0f, mx = 0.0f, my = 0.0f;
    std::optional<std::size_t> selectedElement{}, hovered{};
    SDL_FRect handle{};

    // A part chosen (or none): the form shows it, and what was chosen with it isn't any more.
    void choose(std::optional<std::size_t> part) const {
        if (d.part != part) d.formScroll = 0.0f;
        d.part = part;
        d.moreParts.clear();
        d.focus.clear();
        d.armed.clear();
        d.asText = false;
        d.drag.reset();
    }
    [[nodiscard]] SDL_FRect onScreen(const SDL_FRect& b) const { return {shown.x + b.x * scale, shown.y + b.y * scale, b.w * scale, b.h * scale}; }
    [[nodiscard]] bool isChosen(std::size_t line) const { return d.part == line || std::ranges::contains(d.moreParts, line); }
    // The chosen parts that aren't in another chosen one (the parts in them go with them).
    [[nodiscard]] std::vector<std::size_t> outerLines() const {
        std::vector<std::size_t> out;
        for (const StoryEditor::Part& p : parts)
            if (isChosen(p.line) && std::ranges::none_of(parts, [&](const StoryEditor::Part& q) { return isChosen(q.line) && q.line < p.line && p.line < q.end; }))
                out.push_back(p.line);
        return out;
    }
    // The screen's element a part's line made, if it's there.
    [[nodiscard]] std::optional<std::size_t> elementOf(std::size_t line) const {
        if (!def || !screen) return std::nullopt;
        for (std::size_t i = 0; i < def->elements.size(); ++i)
            if (def->elements[i].line == static_cast<int>(line) + 1 && d.script->files[def->elements[i].file] == d.editor.path(screen->file)) return i;
        return std::nullopt;
    }
};

// The Screens tab: the game's screens on the left (and the built-in ones), the one open in the
// middle as the game draws it (a click chooses a part, dragging moves it, its corner resizes it),
// and the chosen part's settings on the right (or the screen's own).
void Developer::drawScreens(DeveloperUi& ui, const SDL_FRect& area) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    const float k = ui.k;
    using Part = StoryEditor::Part;
    using Screen = StoryEditor::Screen;

    // The screens: the game's, then the built-in textbox if it hasn't made it its own (the choice menu
    // and the Saved notice are built in too, but only listed once they're the game's: + New screen has them).
    std::vector<ScreensFrame::Entry> entries;
    entries.reserve(editor.screens().size());
    for (std::size_t i = 0; i < editor.screens().size(); ++i) entries.push_back({editor.screens()[i].name, i});
    if (std::ranges::none_of(entries, [&](const ScreensFrame::Entry& e) { return lower(e.name) == "textbox"; })) entries.push_back({"textbox", std::nullopt});
    auto chosen = std::ranges::find_if(entries, [&](const ScreensFrame::Entry& e) { return sameName(e.name, d.screenName); });
    if (chosen == entries.end()) {
        chosen = entries.begin();
        d.screenName = chosen->name;
        d.part.reset();
    }
    const ScreensFrame::Entry entry = *chosen;
    const std::optional<Screen> screen = entry.index ? std::optional(editor.screens()[*entry.index]) : std::nullopt;
    const std::vector<Part> parts = screen ? editor.parts(*screen) : std::vector<Part>{};
    const Part* selected = nullptr;
    if (d.part) {
        const auto it = std::ranges::find(parts, *d.part, &Part::line);
        if (it != parts.end()) selected = &*it;
        else d.part.reset();
    }

    // The screen as read (with its parts' places), and a view of it; the textboxes in it show the
    // story's first line (or choice).
    ScreenDef* def = nullptr;
    if (d.script)
        if (const auto it = d.script->screens.find(lower(entry.name)); it != d.script->screens.end()) def = &it->second;
    if (!def) d.screenView.reset();
    else if (!d.screenView || &d.screenView->def() != def) {
        d.screenView = std::make_unique<ScreenView>(*def, engine, true);
        d.sampleMade = false;
    }
    if (def && !d.sampleMade) {
        d.sampleMade = true;
        makeSample(std::ranges::any_of(def->elements, [](const Element& el) { return el.kind == ElementKind::Choices; }));
    }
    std::vector<std::optional<std::size_t>> elementByPart(parts.size()); // (found once a frame, not for each row of the list)

    auto [left, middle, right] = ui.columns(area);

    ScreensFrame f{.ui = ui, .d = d, .entries = entries, .entry = entry, .chosen = &*chosen, .screen = screen, .parts = parts, .selected = selected, .def = def,
                         .elementByPart = elementByPart, .middle = middle, .right = right};
    for (std::size_t i = 0; i < parts.size(); ++i) elementByPart[i] = f.elementOf(parts[i].line);
    // What the parts share, found before any of them is drawn (any may be drawn first): New part's buttons (with a
    // part chosen, Goes in first: in it if it's a container, else on the screen), whether the screen can be changed
    // here, the parts chosen and the chosen one's element.
    if (d.addIntoOf != d.part) { // (another part chosen)
        d.addIntoOf = d.part;
        const auto now = d.part ? std::ranges::find(parts, *d.part, &Part::line) : parts.end(); // (chosen just now: not `selected` yet)
        d.addInto = now != parts.end() && now->kind == "container";
    }
    if (selected) {
        f.intoName = partTitle(*selected, &engine.palette()); // (its button cuts it to fit)
        f.newParts.push_back({.name = "Goes in"_tr, .labels = {"The screen"_tr, f.intoName}});
    }
    for (const auto& [name, starts] : kParts) {
        DeveloperUi::ButtonGroup& g = f.newParts.emplace_back(DeveloperUi::ButtonGroup{.name = tr(name), .labels = {}});
        for (const PartStart& start : starts) g.labels.push_back(tr(start.kind));
    }
    f.editable = screen && def && d.screenView;
    f.selectedElement = selected ? f.elementOf(selected->line) : std::nullopt;
    for (const Part& p : parts) // (in the script's order)
        if (f.isChosen(p.line)) f.chosenLines.push_back(p.line);

    // Where the tab puts them, side by side: Screens over Parts; the screen over New part (the rest of the column); and
    // the form. The screen is as big as the column's height lets it be: as wide as that takes, from the form (which keeps
    // layout::screensFormPart of its width, and its least).
    const SDL_FPoint chrome = panelChrome(k); // (the screen's part is as tall as it is, New part as what's in it at least)
    const float half = std::round(left.h * 0.42f), headH = layout::previewHeadH * k;
    const float newH = chrome.y + ui.newButtonsH(f.newParts, middle.w - chrome.x); // (as tall at any width: a row a group)
    const float screenH = std::max(0.0f, middle.h - chrome.y - headH - newH), fillW = chrome.x + engine.canvas().w * screenH / engine.canvas().h;
    const float formLeast = std::min(right.w, std::max(layout::formLeast * k, std::round(right.w * layout::screensFormPart)));
    middle.w = std::round(std::clamp(fillW, middle.w, middle.w + right.w - formLeast));
    right = {middle.x + middle.w, right.y, area.x + area.w - middle.x - middle.w, right.h};
    const float scale = std::max(0.01f, std::min((middle.w - chrome.x) / engine.canvas().w, screenH / engine.canvas().h));
    const float previewH = chrome.y + headH + engine.canvas().h * scale;
    std::array panels{
        listPanel(ui, "screens", "Screens"_tr, {left.x, left.y, left.w, half}, 1, [&](const ListPart& at) { return drawScreensList(f, at); }),
        listPanel(ui, "parts", "Parts"_tr, {left.x, left.y + half, left.w, left.h - half}, 0, [&](const ListPart& at) { return drawPartsList(f, at); }),
        Panel{.id = "preview", .at = {middle.x, middle.y, middle.w, previewH}, .draw = [&](const SDL_FRect& box) {
                  f.middle = box;
                  return drawPreview(f);
              }},
        Panel{.id = "new_part", .at = {middle.x, middle.y + previewH, middle.w, std::max(newH, middle.h - previewH)}, .draw = [&](const SDL_FRect& box) {
                  f.middle = box;
                  return drawNewPart(f);
              }},
        formPanel(ui, "form", "Form"_tr, right, [&](const SDL_FRect& box) {
            f.right = box;
            return drawScreensForm(f), false;
        })};
    drawPanels(ui, "screens", panels); // (a screen or part changed: the rest are drawn next frame)
}

// The screens (the game's, then the built-in ones), and + New screen (true: changed, the rest next frame).
bool Developer::drawScreensList(const ScreensFrame& f, const ListPart& at) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    using Screen = StoryEditor::Screen;
    const auto& entries = f.entries;
    const auto* chosen = f.chosen;
    const SDL_FRect& screensBox = at.list;
    // A click opens a screen; the game's own can be renamed with a double click (where it's opened
    // follows) and deleted with their x.
    using ListKind = DeveloperUi::ListEvent::Kind;
    auto own = [&](std::size_t i) { return entries[i].index.has_value(); };
    const auto event = ui.editList("screens", screensBox, d.screensScroll,
                                   {.count = entries.size(),
                                    .label = [&](std::size_t i) { return own(i) ? entries[i].name : "{} (built in)"_tr(entries[i].name); },
                                    .chosen = [&](std::size_t i) { return &entries[i] == &*chosen && !d.picking; },
                                    .wrong =
                                        [&](std::size_t i) {
                                            if (!own(i)) return false;
                                            const Screen& s = editor.screens()[*entries[i].index];
                                            return d.problemWithin(s.file, s.line, s.end);
                                        },
                                    .faint = [&](std::size_t i) { return !own(i); },
                                    .renames = own,
                                    .deletes = own,
                                    .moves = own,
                                    .drops =
                                        [&](std::size_t item, std::size_t slot) { // among the game's screens in its file
                                            const std::size_t file = editor.screens()[*entries[item].index].file;
                                            auto in = [&](std::size_t j) { return j < entries.size() && own(j) && editor.screens()[*entries[j].index].file == file; };
                                            return in(slot) || (slot > 0 && in(slot - 1));
                                        }});
    switch (event.kind) {
    case ListKind::Chosen:
        if (lower(d.screenName) != lower(entries[event.item].name)) d.partsScroll = 0.0f;
        d.screenName = entries[event.item].name;
        d.picking = false;
        f.choose({});
        return true;
    case ListKind::Renamed: {
        if (trim(event.name).empty()) return true; // (emptied: it keeps its name)
        const bool ok = editor.renameScreen(*entries[event.item].index, event.name, std::format("screen-name:{}:{}", event.item, d.focusCount));
        if (ok && lower(d.screenName) == lower(entries[event.item].name)) d.screenName = event.name;
        d.saidRenamed(ok, "A screen's name is one word (letters, numbers, _ and -) that no other screen has."_tr);
        return true;
    }
    case ListKind::Deleted:
        editor.removeScreen(*entries[event.item].index);
        if (lower(d.screenName) == lower(entries[event.item].name)) d.screenName.clear();
        f.choose({});
        return true;
    case ListKind::Moved:
        if (editor.moveScreen(*entries[event.item].index, event.to)) f.choose({});
        return true;
    case ListKind::None:
        break;
    }
    if (ui.button(at.rows[0], "+ New screen"_tr, d.picking, true, layout::buttonText)) {
        d.picking = !d.picking;
        d.formScroll = 0.0f;
        f.choose({});
    }
    return false;
}

// The open screen's parts, by their names (else by kind and text): a click chooses one, a double click renames it,
// its x deletes it, and dragging one changes which are drawn over which, and (dragged right or left) puts it in the
// part above it, or out of the one it's in (true: changed, the rest next frame). New part adds one (in the part
// chosen, or the screen).
bool Developer::drawPartsList(const ScreensFrame& f, const ListPart& at) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    using Part = StoryEditor::Part;
    using ListKind = DeveloperUi::ListEvent::Kind;
    const auto& screen = f.screen;
    const auto& parts = f.parts;
    const auto& elementByPart = f.elementByPart;
    const SDL_FRect& partsBox = at.list;
    const float inStep = layout::listIndent * k; // (each part it's in, as the Story's lines are)
    const auto partEvent = ui.editList("parts", partsBox, d.partsScroll,
                                       {.count = parts.size(),
                                        .label = [&](std::size_t i) { return partTitle(parts[i]); },
                                        .name = [&](std::size_t i) { return parts[i].option("name"); },
                                        .chosen = [&](std::size_t i) { return d.part == parts[i].line || std::ranges::contains(d.moreParts, parts[i].line); },
                                        .wrong = [&](std::size_t i) { return d.problemAt(screen->file, parts[i].line) != nullptr; },
                                        .faint =
                                            [&](std::size_t i) {
                                                const auto el = elementByPart[i];
                                                return !el || !d.screenView || !d.screenView->drawnPart(*el);
                                            },
                                        .renames = [](std::size_t) { return true; },
                                        .deletes = [](std::size_t) { return true; },
                                        .moves = [](std::size_t) { return true; },
                                        .drops = {},
                                        .place = // (not into itself; as far in as the part above it and one more, as far out as the one under it)
                                            [&](std::size_t item, std::size_t slot, float dx) -> std::optional<int> {
                                                std::size_t after = item + 1; // (past the parts in it)
                                                while (after < parts.size() && parts[after].line < parts[item].end) ++after;
                                                if (slot > item && slot < after) return std::nullopt;
                                                const bool here = slot == item || slot == after; // (the parts around it, but not its own)
                                                const std::size_t up = here ? item : slot, down = here ? after : slot;
                                                const Part* above = up > 0 ? &parts[up - 1] : nullptr;
                                                const Part* below = down < parts.size() ? &parts[down] : nullptr;
                                                const int most = above ? above->depth + 1 : 0, least = std::min(below ? below->depth : 0, most);
                                                const int want = std::clamp(parts[item].depth + static_cast<int>(std::lround(dx / (inStep * 2))), least, most);
                                                if (here && want == parts[item].depth) return std::nullopt;
                                                return want;
                                            },
                                        .indentStep = inStep,
                                        .indent = [&](std::size_t i) { return static_cast<float>(parts[i].depth) * inStep; },
                                        .paint = // (a named one: its kind after its name, in grey; an image part's colour: a square and its name, as its box shows it)
                                            [&](std::size_t i, const SDL_FRect& row) {
                                                const auto el = elementByPart[i];
                                                const bool faint = !el || !d.screenView || !d.screenView->drawnPart(*el);
                                                const bool named = !parts[i].option("name").empty();
                                                const auto colour = !named && parts[i].kind == "image" ? parseColour(parts[i].text, &engine.palette()) : std::nullopt;
                                                const std::string title = colour ? parts[i].kind : partTitle(parts[i]);
                                                float x = row.x + 10 * k;
                                                const float mid = row.y + row.h / 2;
                                                const SDL_Color shade = faint ? theme::dim : theme::text;
                                                const float end = row.x + row.w - layout::listPad * k; // (each cut to fit: the title leaves what's after it its room, up to half)
                                                const std::string after = colour ? colourWords(parts[i].text, &engine.palette()) : named ? parts[i].kind : std::string{};
                                                const float afterW = after.empty() ? 0.0f : (colour ? (layout::swatch + 8) * k : 0.0f) + ui.width(after, colour ? layout::listText : layout::smallText) + 8 * k;
                                                x += ui.textFitted(title, x, mid, layout::listText, shade, std::max(end - x - afterW, (end - x) / 2)) + 8 * k;
                                                if (colour) {
                                                    const auto grad = parseGradient(parts[i].text, &engine.palette());
                                                    const float sx = ui.swatch(x, mid, *colour, false, grad ? std::optional(grad->second) : std::nullopt);
                                                    ui.textFitted(after, sx, mid, layout::listText, shade, end - sx);
                                                }
                                                else if (named) ui.textFitted(after, x, mid, layout::smallText, theme::dim, end - x);
                                            }});
    switch (partEvent.kind) {
    case ListKind::Chosen:
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) && d.part && *d.part != parts[partEvent.item].line) { // (one more chosen, or one less)
            if (const auto it = std::ranges::find(d.moreParts, parts[partEvent.item].line); it != d.moreParts.end()) d.moreParts.erase(it);
            else d.moreParts.push_back(parts[partEvent.item].line);
            d.listDrag.reset();
            break;
        }
        f.choose(parts[partEvent.item].line);
        break;
    case ListKind::Deleted:
        editor.removePart(*screen, parts[partEvent.item]);
        f.choose({});
        return true;
    case ListKind::Moved:
        if (const auto to = editor.movePart(*screen, parts[partEvent.item], partEvent.to, partEvent.depth)) f.choose(*to);
        return true;
    case ListKind::Renamed: { // (as typed: one step for undo while it's typed into)
        Part named = parts[partEvent.item];
        named.set("name", partName(partEvent.name));
        editor.setPart(*screen, named, std::format("part-name:{}:{}", named.line, d.focusCount));
        break;
    }
    case ListKind::None:
        break;
    }
    return false;
}

bool Developer::drawPreview(ScreensFrame& f) {
    DeveloperTools& d = tools;
    const float k = f.ui.k;
    // --- The screen as big as fits, with its heading over it.
    const SDL_FRect& middle = f.middle;
    const float headH = layout::previewHeadH * k;
    f.scale = std::max(0.01f, std::min(middle.w / engine.canvas().w, (middle.h - headH) / engine.canvas().h));
    f.shown = {middle.x + (middle.w - engine.canvas().w * f.scale) / 2, middle.y + headH, engine.canvas().w * f.scale, engine.canvas().h * f.scale};
    f.mx = (d.mouseX - f.shown.x) / f.scale;
    f.my = (d.mouseY - f.shown.y) / f.scale;
    d.shownScreen = f.shown;
    chooseOnScreen(f);
    if (dragOnScreen(f) || screenKeys(f)) return true;
    paintScreen(f);
    return false;
}

// Choosing parts on the screen, and starting to move or resize them. With a part chosen, the screen
// only moves or resizes it (even under others), so another part is chosen in the list, or after Esc
// lets it go; Shift+click (on the screen or in the list) chooses more, to move together.
void Developer::chooseOnScreen(ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    const Part* selected = f.selected;
    ScreenDef* def = f.def;
    const SDL_FRect& shown = f.shown;
    const float mx = f.mx, my = f.my;
    const bool editable = f.editable;
    const auto& selectedElement = f.selectedElement;
    std::vector<std::size_t>& chosenLines = f.chosenLines;
    auto number = [](const std::string& text, float fallback) { return parseNumber(text).value_or(fallback); };
    if (selected && d.focus.empty())
        std::erase_if(d.keys, [&](const SDL_KeyboardEvent& key) {
            if (key.key == SDLK_ESCAPE) {
                f.choose({});
                return true;
            }
            if (d.isKey(key, ToolKey::Delete)) { // (the chosen parts go)
                editor.removeParts(*screen, chosenLines);
                f.choose({});
                return true;
            }
            return false;
        });
    const bool adding = SDL_GetModState() & SDL_KMOD_SHIFT;
    std::optional<std::size_t>& hovered = f.hovered;
    if (editable && ui.over(shown) && !d.drag && (!selected || adding))
        for (std::size_t i = def->elements.size(); i-- > 0;) {
            const SDL_FRect b = d.screenView->partBounds(i);
            if (d.screenView->drawnPart(i) && pointInRect(mx, my, b)) {
                hovered = i;
                break;
            }
        }
    f.handle = selectedElement ? [&] {
        const SDL_FRect b = f.onScreen(d.screenView->partBounds(*selectedElement));
        return SDL_FRect{b.x + b.w - 5 * k, b.y + b.h - 5 * k, 10 * k, 10 * k};
    }() : SDL_FRect{};
    const SDL_FRect& handle = f.handle;
    auto inside = [&](std::size_t element) {
        const SDL_FRect b = d.screenView->partBounds(element);
        return pointInRect(mx, my, b);
    };
    auto within = [&](std::size_t element, std::size_t outer) { // (a part in that one, or in one in it...)
        for (int at = def->elements[element].parent; at >= 0; at = def->elements[static_cast<std::size_t>(at)].parent)
            if (static_cast<std::size_t>(at) == outer) return true;
        return false;
    };
    if (editable && d.clicked && ui.over(shown)) {
        d.clicked = false;
        d.focus.clear();
        if (adding && hovered) { // one more chosen (or one less)
            const std::size_t line = static_cast<std::size_t>(def->elements[*hovered].line - 1);
            if (!d.part) {
                f.choose(line);
            } else if (line == *d.part) {
                d.part = d.moreParts.empty() ? std::nullopt : std::optional(d.moreParts.front());
                if (!d.moreParts.empty()) d.moreParts.erase(d.moreParts.begin());
            } else if (const auto it = std::ranges::find(d.moreParts, line); it != d.moreParts.end()) {
                d.moreParts.erase(it);
            } else {
                d.moreParts.push_back(line);
            }
            d.lastList = "parts";
        } else {
            const bool resize = selectedElement && ui.over(handle);
            const std::optional<std::size_t> element = [&]() -> std::optional<std::size_t> { // (what the press takes hold of)
                if (!selectedElement) return hovered;
                if (resize || inside(*selectedElement)) return selectedElement;
                for (const std::size_t line : chosenLines) // (or another chosen one: they all move)
                    if (const auto el = f.elementOf(line); el && inside(*el)) return selectedElement;
                return std::nullopt;
            }();
            if (element) {
                const Element& el = def->elements[*element];
                const Part p = editor.part(*screen, static_cast<std::size_t>(el.line - 1));
                const SDL_FRect b = d.screenView->partBounds(*element);
                if (!selected) {
                    f.choose(p.line);
                    chosenLines = {p.line};
                }
                d.lastList = "parts";
                DeveloperTools::Drag drag{.part = p.line, .element = *element, .resize = resize, .from = d.screenView->partRect(*element), .x = mx, .y = my,
                                          .px = number(p.option("x"), 0.0f), .py = number(p.option("y"), 0.0f), .pw = b.w, .ph = b.h, .others = {}};
                // The other chosen parts move with it (but not one in a part that's moving: it goes with that
                // one), and so do the parts in them.
                std::vector<std::size_t> moving{*element};
                if (!resize)
                    for (const std::size_t line : chosenLines)
                        if (const auto other = f.elementOf(line); other && line != p.line) moving.push_back(*other);
                auto carried = [&](std::size_t i) { return std::ranges::any_of(moving, [&](std::size_t m) { return within(i, m); }); };
                drag.own = !carried(*element);
                for (const std::size_t other : moving)
                    if (other != *element && !carried(other)) {
                        const Part q = editor.part(*screen, static_cast<std::size_t>(def->elements[other].line - 1));
                        drag.others.push_back({.part = q.line, .element = other, .from = d.screenView->partRect(other),
                                               .px = number(q.option("x"), 0.0f), .py = number(q.option("y"), 0.0f)});
                    }
                if (!resize)
                    for (std::size_t i = 0; i < def->elements.size(); ++i)
                        if (std::ranges::contains(moving, i) || carried(i)) drag.moved.emplace_back(i, d.screenView->partRect(i));
                d.drag = drag;
            }
        }
    }
}

// The parts being moved or resized, as the mouse goes (in steps of 5 pixels, lined up with the other
// parts' edges and middles, and the screen's); written to the script when it's let go (true).
bool Developer::dragOnScreen(ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    ScreenDef* def = f.def;
    const float mx = f.mx, my = f.my;
    const bool editable = f.editable;
    auto snap = [](float v) { return std::round(v / 5.0f) * 5.0f; };
    d.guideX.reset();
    d.guideY.reset();
    if (d.drag && editable && d.drag->element < def->elements.size()) {
        const DeveloperTools::Drag g = *d.drag;
        float dx = snap(mx - g.x), dy = snap(my - g.y); // (in steps of 5 pixels, or lined up with something)
        // The edges and middles to line up with: the screen's, and those of the parts that aren't moving.
        std::vector<float> xs{0.0f, engine.canvas().w / 2, engine.canvas().w}, ys{0.0f, engine.canvas().h / 2, engine.canvas().h};
        for (std::size_t i = 0; i < def->elements.size(); ++i) {
            if (i == g.element || !d.screenView->drawnPart(i) ||
                std::ranges::any_of(g.moved, [&](const auto& m) { return m.first == i; }))
                continue;
            const SDL_FRect b = d.screenView->partBounds(i);
            xs.insert(xs.end(), {b.x, b.x + b.w / 2, b.x + b.w});
            ys.insert(ys.end(), {b.y, b.y + b.h / 2, b.y + b.h});
        }
        auto lineUp = [](std::initializer_list<float> edges, const std::vector<float>& targets, float& by, std::optional<float>& guide) {
            float best = 7.0f; // (closer than this lines up)
            for (const float e : edges)
                for (const float t : targets)
                    if (std::abs(t - e) < std::abs(best)) {
                        best = t - e;
                        guide = t;
                    }
            if (std::abs(best) < 7.0f) by += best;
        };
        const SDL_FRect from = g.from; // (only across the way it's being moved: a drag sideways stays at its height)
        if (g.resize) {
            if (dx != 0.0f) lineUp({from.x + g.pw + dx}, xs, dx, d.guideX);
            if (dy != 0.0f) lineUp({from.y + g.ph + dy}, ys, dy, d.guideY);
        } else {
            if (dx != 0.0f) lineUp({from.x + dx, from.x + dx + from.w / 2, from.x + dx + from.w}, xs, dx, d.guideX);
            if (dy != 0.0f) lineUp({from.y + dy, from.y + dy + from.h / 2, from.y + dy + from.h}, ys, dy, d.guideY);
        }
        float w = std::max(5.0f, g.pw + dx), h = std::max(5.0f, g.ph + dy);
        if (g.resize && (SDL_GetModState() & SDL_KMOD_SHIFT) && g.pw > 0.0f && g.ph > 0.0f) { // (Shift: its shape kept, by the way it's pulled most)
            const float across = w / g.pw, down = h / g.ph, grow = std::abs(across - 1.0f) >= std::abs(down - 1.0f) ? across : down;
            w = std::max(5.0f, g.pw * grow);
            h = std::max(5.0f, g.ph * grow);
        }
        Element& el = def->elements[g.element];
        // (as places and shares in the box each is in: the parts in them go with them)
        auto placed = [&](std::size_t i, SDL_FPoint edge, float pw, float ph) {
            Element& m = def->elements[i];
            const SDL_FRect box = d.screenView->partBox(i);
            m.placeX = placeFor(edge.x - box.x, pw, box.w);
            m.placeY = -placeFor(edge.y - box.y, ph, box.h);
        };
        if (g.resize) {
            const SDL_FRect box = d.screenView->partBox(g.element);
            el.shareWidth = w / box.w;
            el.shareHeight = h / box.h;
            placed(g.element, {g.from.x, g.from.y}, w, h);
        } else {
            if (g.own) placed(g.element, {g.from.x + dx, g.from.y + dy}, g.from.w, g.from.h);
            for (const auto& o : g.others) placed(o.element, {o.from.x + dx, o.from.y + dy}, o.from.w, o.from.h);
        }
        if (!d.held) { // let go: written to the script (one step for undo)
            d.drag.reset();
            d.guideX.reset();
            d.guideY.reset();
            if (dx != 0.0f || dy != 0.0f) {
                const std::string key = std::format("drag:{}", ++d.focusCount);
                auto write = [&](std::size_t part, std::size_t element, bool size) {
                    Part q = editor.part(*screen, part);
                    const Element& m = def->elements[element];
                    q.set("x", unitText(m.placeX.value_or(-1.0f)));
                    q.set("y", unitText(m.placeY.value_or(1.0f)));
                    if (size) {
                        q.set("width", unitText(m.shareWidth.value_or(0.0f)));
                        q.set("height", unitText(m.shareHeight.value_or(0.0f)));
                    }
                    editor.setPart(*screen, q, key);
                };
                if (g.resize || g.own) write(g.part, g.element, g.resize);
                for (const auto& o : g.others) write(o.part, o.element, false);
                return true;
            }
        }
    }
    return false;
}

// The keys for what's chosen on the screen: copy, cut, paste and duplicate, and the arrows that nudge
// (true: the script changed).
bool Developer::screenKeys(ScreensFrame& f) {
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    using Part = StoryEditor::Part;
    const auto& entry = f.entry;
    const auto& screen = f.screen;
    const auto& parts = f.parts;
    const Part* selected = f.selected;
    const bool editable = f.editable;
    const std::vector<std::size_t>& chosenLines = f.chosenLines;
    // Copy, cut, paste and duplicate the chosen parts, or the screen (in the list clicked last).
    if (d.focus.empty() && !d.drag)
        for (auto key = d.keys.begin(); key != d.keys.end(); ++key) {
            const char what = clipboardKey(d, *key);
            if (!what) continue;
            d.keys.erase(key);
            const bool onPart = selected && d.lastList != "screens";
            std::string text;
            if (onPart)
                for (const std::size_t line : f.outerLines())
                    for (const std::string& l : editor.partLines(*screen, editor.part(*screen, line))) text += (text.empty() ? "" : "\n") + l;
            else if (screen)
                text = editor.textOf(screen->file, screen->line, screen->end);
            if (what == 'c' || what == 'x') {
                if (text.empty()) return true;
                SDL_SetClipboardText(text.c_str());
                d.clipText = text;
                if (what == 'x' && onPart) {
                    editor.removeParts(*screen, chosenLines);
                    f.choose({});
                } else if (what == 'x' && screen) {
                    editor.removeScreen(*entry.index);
                    d.screenName.clear();
                    f.choose({});
                }
                return true;
            }
            std::vector<std::string> lines;
            if (what == 'd')
                lines = linesOf(text);
            else
                lines = clipboardLines();
            if (lines.empty()) return true;
            if (lower(firstWord(lines.front())) == "screen") {
                d.screenName = editor.screens()[editor.pasteScreen(lines)].name;
                f.choose({});
            } else if (screen) {
                std::erase_if(lines, [](const std::string& l) { return blank(l); });
                const auto last = chosenLines.empty() ? parts.end() : std::ranges::find(parts, chosenLines.back(), &Part::line);
                const Part* after = last != parts.end() ? &*last : nullptr;
                const std::size_t first = editor.addParts(*screen, after, lines);
                f.choose(first);
                for (std::size_t i = 1; i < lines.size(); ++i) // (the pasted ones stay chosen together: the outer ones)
                    if (indentOf(lines[i]) <= indentOf(lines.front())) d.moreParts.push_back(first + i);
            }
            return true;
        }
    // Arrow keys nudge the chosen parts (Shift: 10 pixels).
    if (editable && selected && d.focus.empty() && !d.drag)
        for (const SDL_KeyboardEvent& key : d.keys) {
            const float step = key.mod & SDL_KMOD_SHIFT ? 10.0f : 1.0f;
            const float dx = key.key == SDLK_LEFT ? -step : key.key == SDLK_RIGHT ? step : 0.0f;
            const float dy = key.key == SDLK_UP ? -step : key.key == SDLK_DOWN ? step : 0.0f;
            if (dx == 0.0f && dy == 0.0f) continue;
            std::erase_if(d.keys, [&](const SDL_KeyboardEvent& other) { return &other == &key; });
            const std::string stepKey = std::format("nudge:{}", ++d.focusCount);
            for (const std::size_t line : f.outerLines()) { // (the parts in them go with them)
                const auto el = f.elementOf(line);
                if (!el || !d.screenView) continue;
                const SDL_FRect at = d.screenView->partRect(*el), box = d.screenView->partBox(*el);
                Part p = editor.part(*screen, line);
                p.set("x", unitText(placeFor(at.x + dx - box.x, at.w, box.w)));
                p.set("y", unitText(-placeFor(at.y + dy - box.y, at.h, box.h)));
                editor.setPart(*screen, p, stepKey);
            }
            return true;
        }

    return false;
}

// The screen drawn (what's chosen on it outlined, the lines things line up with), under its heading.
void Developer::paintScreen(const ScreensFrame& f) {
    DeveloperTools& d = tools;
    DeveloperUi& ui = f.ui;
    SDL_Renderer* r = ui.r;
    const float k = ui.k;
    const auto& entry = f.entry;
    const ScreenDef* def = f.def;
    const SDL_FRect& shown = f.shown;
    const float scale = f.scale;
    const auto& hovered = f.hovered;
    const auto& selectedElement = f.selectedElement;
    const SDL_FRect& handle = f.handle;
    draw::rect(r, shown, kBox);
    if (def && d.screenView &&
        paintCanvas([&] {
            std::optional<SwappedIn<std::unique_ptr<Game>>> sampleGame;
            if (d.sample) sampleGame.emplace(engine.game_, d.sample);
            // The phone, on the app the chosen part is in (or the chosen app), on a chat for a part of one.
            // Another part of an app that doesn't show so (as a "Chats" heading with a chat open) shows with a
            // chat open, or none, instead.
            auto phone = engine.phone_;
            std::optional<std::string> otherChat;
            if (selectedElement) {
                const auto& els = def->elements;
                for (int at = static_cast<int>(*selectedElement); at >= 0; at = els[static_cast<std::size_t>(at)].parent)
                    if (const std::string app = els[static_cast<std::size_t>(at)].option("app"); !app.empty()) {
                        phone.app = app;
                        phone.pinned = true;
                        break;
                    }
                phone.locked = phone.app == "lock";
                phone.image.clear();
                // (a part with the messages in it, in them, or with them in it: on the newest chat; with the chats: none)
                auto near = [&](std::string_view list) {
                    for (int at = static_cast<int>(*selectedElement); at >= 0; at = els[static_cast<std::size_t>(at)].parent)
                        if (els[static_cast<std::size_t>(at)].option("list") == list) return true;
                    for (std::size_t j = *selectedElement + 1; j < els.size(); ++j) {
                        int p = els[j].parent;
                        while (p >= 0 && static_cast<std::size_t>(p) != *selectedElement) p = els[static_cast<std::size_t>(p)].parent;
                        if (p >= 0 && els[j].option("list") == list) return true;
                    }
                    return false;
                };
                const auto chats = engine.game().chats();
                const std::string newest = chats.empty() ? std::string{} : chats.front().name;
                if (near("messages")) phone.chat = newest;
                else if (near("chats")) phone.chat.clear();
                else if (phone.pinned) otherChat = phone.chat.empty() ? newest : std::string{};
            }
            const SwappedIn samplePhone(engine.phone_, phone);
            auto paint = [&] {
                if (lower(entry.name) != "title") engine.game_->draw(engine.fonts_.get("default", 18)); // (the story under it)
                d.screenView->draw(engine.renderer());
            };
            paint();
            if (otherChat && *otherChat != engine.phone_.chat && !d.screenView->drawnPart(*selectedElement)) {
                engine.phone_.chat = *otherChat;
                SDL_SetRenderDrawColor(engine.renderer(), 0, 0, 0, 255);
                SDL_RenderClear(engine.renderer());
                paint();
            }
        })) {
        SDL_RenderTexture(r, d.canvas.get(), nullptr, &shown);
        const DeveloperUi::Clip clipped(ui, shown);
        if (hovered) draw::outline(r, f.onScreen(d.screenView->partBounds(*hovered)), kEdge);
        for (const std::size_t line : d.moreParts)
            if (const auto el = f.elementOf(line)) draw::outline(r, f.onScreen(d.screenView->partBounds(*el)), kAccent);
        if (selectedElement) {
            draw::outline(r, f.onScreen(d.screenView->partBounds(*selectedElement)), d.screenView->drawnPart(*selectedElement) ? kAccent : kWarn);
            draw::rect(r, handle, kAccent);
        }
        if (d.guideX) draw::rect(r, {shown.x + *d.guideX * scale, shown.y, 1 * k, shown.h}, kOptionArrow); // (what it lines up with)
        if (d.guideY) draw::rect(r, {shown.x, shown.y + *d.guideY * scale, shown.w, 1 * k}, kOptionArrow);
    }
    draw::outline(r, shown, kEdge);
    PreviewHead(ui, PreviewHead::over(shown, k)).title("Preview"_tr);
}

void Developer::drawScreensForm(const ScreensFrame& f) {
    if (tools.picking) return drawNewScreenForm(f);
    if (!f.screen) return drawBuiltInForm(f);
    if (f.selected && !tools.moreParts.empty()) return drawLineUpForm(f);
    drawPartForm(f);
}

// + New screen: the screens to start from (the list scrolls when it's longer than its room).
void Developer::drawNewScreenForm(const ScreensFrame& f) {
    using Screen = StoryEditor::Screen;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    const float titleH = layout::titleH * k;
    ui.formTitle("New screen"_tr, right.x, y);
    const SDL_FRect list{right.x, y + titleH, right.w, right.y + right.h - y - titleH};
    ui.scrollForm(list, (layout::buttonH + layout::gap) * k); // (a button a step, as + New animation's)
    y = list.y - d.formScroll;
    const DeveloperUi::Clip clipped(ui, list, true);
    auto has = [&](std::string_view name) { return std::ranges::any_of(editor.screens(), [&](const Screen& s) { return lower(s.name) == name; }); };
    for (const ScreenStart& start : kScreenStarts) {
        if (ui.button({right.x, y, right.w, layout::buttonH * k}, tr(start.label), false, true, layout::buttonText)) {
            const std::string_view name = start.name;
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
            const float sy = engine.canvas().h / 720.0f;
            // (one there already: the new one is another, pause_2...)
            const std::string as = uniqueName(name.empty() ? std::string("new_screen") : std::string(name), has);
            std::vector<std::string> lines = startScreen(name, as, will, [&](std::string_view w) { return !editor.find(w).empty(); }, controls);
            std::vector<std::string> needs = needsOf(editor, lines, name);
            for (std::string& l : needs) l = scaled(l, sy);
            if (!builtIn(name)) // (written for 1280 x 720: places and shares fit any size; its texts placed by their sizes)
                lines = placesFromPixels(lines, 1280.0f, 720.0f, ownSizes(lines, needs, 1280.0f, 720.0f, sy));
            for (std::string& l : lines) l = scaled(l, sy);
            const std::size_t made = editor.addScreen(needs, lines);
            d.picking = false;
            d.screenName = editor.screens()[made].name;
            f.choose({});
            break;
        }
        y += layout::buttonH * k + layout::gap * k;
    }
    ui.formDrawn(list, y);
}

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

// New part, under the screen, as the Story tab's New line is under its lines: Goes in (the
// screen, or the part chosen), then a button for each kind of part, in groups. A part goes in at the top
// left of the part it goes in.
bool Developer::drawNewPart(ScreensFrame& f) {
    using Part = StoryEditor::Part;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const auto& screen = f.screen;
    const Part* selected = f.selected;
    const std::size_t goesIn = selected ? 2 : 0; // (its two buttons first)
    const std::vector<std::size_t> chosen{d.addInto ? std::size_t{1} : std::size_t{0}};
    const auto clicked = ui.newButtons("New part"_tr, f.newParts, f.middle, selected ? std::span(chosen) : std::span<const std::size_t>{}, screen.has_value());
    if (!clicked || !screen) return false;
    if (*clicked < goesIn) {
        d.addInto = *clicked == 1;
        return false;
    }
    const PartStart* start = nullptr;
    for (std::size_t i = *clicked - goesIn; const auto& [name, starts] : kParts) {
        if (i < starts.size()) {
            start = &starts[i];
            break;
        }
        i -= starts.size();
    }
    if (!start) return false;
    const Part* into = d.addInto ? selected : nullptr;
    std::string line(start->line);
    const auto images = editor.names(StoryEditor::Pick::Images), movies = editor.names(StoryEditor::Pick::Movies); // (the first, or none to pick)
    const auto& variables = editor.variables(); // (the game's own)
    line = replaced(line, "IMAGE", images.empty() ? "" : images.front());
    line = replaced(line, "MOVIE", movies.empty() ? "" : movies.front());
    line = variables.empty() ? replaced(line, " value=VARIABLE", "") : replaced(line, "VARIABLE", variables.front());
    line = std::string(trim(scaled("    " + line, engine.canvas().h / 720.0f)));
    // (written for 1280 x 720: as places and shares, on the screen or at the top left of the part it goes in)
    std::vector<std::string> lines{"screen new", "    " + line};
    if (into)
        if (const auto el = f.elementOf(into->line); el && d.screenView) {
            const SDL_FRect b = d.screenView->partBounds(*el);
            const float sx = 1280.0f / engine.canvas().w, sy = 720.0f / engine.canvas().h;
            std::string at;
            for (const std::string& word : rawWords(line))
                at += (at.empty() ? "" : " ") + (word.starts_with("x=") ? std::string("x=0") : word.starts_with("y=") ? std::string("y=0") : word);
            lines = {"screen new", std::format("    container x={} y={} width={} height={}", b.x * sx, b.y * sy, b.w * sx, b.h * sy), "        " + at};
        }
    std::vector<std::string> needs = needsOf(editor, {"screen new", "    " + line}, "new"); // (the colours and styles it uses that the game lacks)
    for (std::string& l : needs) l = scaled(l, engine.canvas().h / 720.0f);
    line = std::string(trim(placesFromPixels(lines, 1280.0f, 720.0f, ownSizes(lines, needs, 1280.0f, 720.0f, 1.0f)).back()));
    d.picking = false;
    f.choose(editor.addPart(*screen, into, line, needs));
    return true;
}

// A built-in screen (the textbox, the choice menu): what it is, and a button that makes it the game's own.
void Developer::drawBuiltInForm(const ScreensFrame& f) {
    const auto& entry = f.entry;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    const float titleH = layout::titleH * k;
    ui.formTitle(entry.name, right.x, y, kAccent, right.w);
    y += titleH;
    if (ui.button({right.x, y, std::min(right.w, ui.buttonWidth("Make it the game's own"_tr, layout::lineText, layout::widerButtonW)), layout::bigButtonH * k}, "Make it the game's own"_tr)) { // (a copy in the game's screens, used instead)
        const float sy = engine.canvas().h / 720.0f;
        const std::vector<std::string> lines = ownScreenLines(entry.name, sy, editor.defines("screen", "phone"), editor.defines("screen", "pause"));
        std::vector<std::string> needs = needsOf(editor, lines, entry.name); // (the colours and styles it uses that the game lacks)
        for (std::string& l : needs) l = scaled(l, sy);
        if (!lines.empty()) editor.addScreen(needs, lines);
        f.choose({});
        return;
    }
}

// Several parts chosen: lined up by their edges or middles, or spread out evenly between the outer two
// (one step for undo).
void Developer::drawLineUpForm(const ScreensFrame& f) {
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    const auto& chosenLines = f.chosenLines;
    const bool editable = f.editable;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    ui.formTitle("{} parts"_tr(chosenLines.size()), right.x, y);
    y += layout::titleH * k;
    struct Item {
        std::size_t line = 0, element = 0;
        SDL_FRect b{};
    };
    std::vector<Item> items;
    if (editable)
        for (const std::size_t line : chosenLines)
            if (const auto el = f.elementOf(line); el && d.screenView->drawnPart(*el)) items.push_back({line, *el, d.screenView->partBounds(*el)});
    static const std::array<std::string_view, 6> kLineUp{"Left"_text, "Centre"_text, "Right"_text, "Top"_text, "Middle"_text, "Bottom"_text}; // (told apart by their English)
    static const std::array<std::string_view, 2> kSpread{"Across"_text, "Down"_text};
    const float labelW = labelColumn(ui, std::round(right.w * layout::labelPart), right.w, {}, {"Line up"_tr, "Spread evenly"_tr}), bh = layout::boxH * k;
    std::optional<std::string_view> clicked;
    for (const auto& [label, words, need] : {std::tuple{std::string_view("Line up"_tr), std::span<const std::string_view>(kLineUp), std::size_t{2}},
                                             {std::string_view("Spread evenly"_tr), std::span<const std::string_view>(kSpread), std::size_t{3}}}) {
        ui.textFitted(label, right.x, y + bh / 2, layout::labelText, theme::dim, labelW - 8 * k); // (each a row: its buttons on one line)
        std::vector<std::string_view> shown;
        for (const std::string_view word : words) shown.push_back(tr(word));
        if (const auto i = ui.buttons(right.x + labelW, y, right.w - labelW, bh, shown, std::vector<bool>(words.size(), items.size() >= need), layout::smallButtonText)) clicked = words[*i];
        y += bh + layout::rowGap * k; // (as every form's rows)
    }
    if (!clicked) return;
    const bool across = *clicked == "Left" || *clicked == "Centre" || *clicked == "Right" || *clicked == "Across";
    auto from = [&](const Item& i) { return across ? i.b.x : i.b.y; };
    auto size = [&](const Item& i) { return across ? i.b.w : i.b.h; };
    std::vector<float> by(items.size()); // how far each one moves
    float low = FLT_MAX, high = -FLT_MAX;
    for (const Item& i : items) low = std::min(low, from(i)), high = std::max(high, from(i) + size(i));
    if (*clicked == "Across" || *clicked == "Down") { // (in their order, the same space between them)
        std::vector<std::size_t> order(items.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::ranges::sort(order, {}, [&](std::size_t i) { return from(items[i]); });
        float sizes = 0.0f;
        for (const Item& i : items) sizes += size(i);
        const float space = (high - low - sizes) / static_cast<float>(items.size() - 1);
        float at = low;
        for (const std::size_t i : order) {
            by[i] = at - from(items[i]);
            at += size(items[i]) + space;
        }
    } else {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const bool start = *clicked == "Left" || *clicked == "Top", end = *clicked == "Right" || *clicked == "Bottom";
            by[i] = start ? low - from(items[i]) : end ? high - from(items[i]) - size(items[i]) : (low + high) / 2 - from(items[i]) - size(items[i]) / 2;
        }
    }
    const std::string key = std::format("line-up:{}", ++d.focusCount);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (std::lround(by[i]) == 0) continue;
        const SDL_FRect at = d.screenView->partRect(items[i].element), box = d.screenView->partBox(items[i].element);
        Part q = editor.part(*screen, items[i].line);
        q.set(across ? "x" : "y", unitText(across ? placeFor(at.x + by[i] - box.x, at.w, box.w) : -placeFor(at.y + by[i] - box.y, at.h, box.h)));
        editor.setPart(*screen, q, key);
    }
}

// The chosen part's form (or, with none chosen, the screen's): its name, its text, where it is and its
// settings; or its line as text.
void Developer::drawPartForm(const ScreensFrame& f) {
    using Part = StoryEditor::Part;
    const auto& screen = f.screen;
    const Part* selected = f.selected;
    ScreenDef* def = f.def;
    DeveloperTools& d = tools;
    StoryEditor& editor = d.editor;
    DeveloperUi& ui = f.ui;
    const float k = ui.k;
    const SDL_FRect& right = f.right;
    float y = right.y;
    const float titleH = layout::titleH * k;
    const bool header = !selected;
    const Part p = header ? editor.part(*screen, screen->line) : *selected;
    const float toggleW = header ? 0.0f : asTextButton(ui, right.x + right.w, y) + layout::gap * k;
    ui.formTitle(header ? "Screen: {}"_tr(p.text) : p.option("name").empty() ? std::string(tr(upperFirst(p.kind))) : std::format("{}: {}", tr(upperFirst(p.kind)), p.option("name")),
                 right.x, y, kAccent, right.w - toggleW);
    y += titleH;
    const std::string id = std::format("part:{}:{}", screen->line, p.line);
    auto change = [&](const Part& changed, std::string_view what) { editor.setPart(*screen, changed, std::format("{}:{}:{}", id, what, d.focusCount)); };
    if (d.asText && !header) {
        if (const ScriptError* problem = d.problemAt(screen->file, p.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
        if (const auto text = drawRawLine(ui, id + ":raw", StoryEditor::lineOf(p), right.x, right.w, y)) {
            Part raw = p;
            raw.kind = *text;
            raw.text.clear();
            raw.quoted = false;
            raw.options.clear();
            raw.words.clear();
            raw.note.clear();
            change(raw, "raw");
        }
        return;
    }
    // The form (it scrolls when it's longer than its room): a part's text, where it is, then its
    // settings; or the screen's own.
    const SDL_FRect formBox{right.x, y, right.w, std::max(0.0f, right.y + right.h - y)};
    ui.scrollForm(formBox, (layout::boxH + layout::rowGap) * k);
    y -= d.formScroll;
    const DeveloperUi::Clip clipped(ui, formBox, true);
    const float least = std::round(right.w * layout::partLabelPart); // (the labels' column: wider when they need it, the same for all the form's rows)
    if (const ScriptError* problem = d.problemAt(screen->file, p.line)) y += ui.text(problem->message, right.x, y, layout::noteText, kBad, right.w).y + 8 * k;
    // A change from the form: typed (one step for undo while one box is typed into) or picked.
    auto apply = [&](const std::optional<FormEdit>& edit, const std::vector<StoryEditor::Field>& fields, auto&& changed) {
        if (!edit) return;
        auto now = fields;
        std::ranges::find(now, edit->name, &StoryEditor::Field::name)->value = edit->value;
        if (edit->typed) change(changed(now), edit->name);
        else editor.setPart(*screen, changed(now), {}), d.focus.clear(); // (each click a step for undo)
    };
    if (header) {
        const auto fields = optionFields(editor, p, kScreenOptions);
        apply(drawForm(ui, id, fields, right.x, right.w, labelColumn(ui, least, right.w, {fields}), y), fields, [&](const auto& now) { return withOptions(editor, p, now, kScreenOptions); });
    } else {
        // Its name (what the Parts list calls it: the game doesn't show it), its text (or the word after its
        // kind) and its settings, their labels in one column.
        const std::vector<StoryEditor::Field> name{{.name = "name", .value = p.option("name"), .label = "Name", .box = Box::Name, .hint = "None"}};
        const std::string_view textName = textLabel(p.kind);
        std::vector<StoryEditor::Field> text;
        if (!textName.empty() && p.kind == "image") { // (an image or a colour; or one for yes and one for no)
            std::string imageText = p.text; // (bottom=: a colour fading down to another, as one value: navy-black)
            if (const std::string fadesTo = p.option("bottom"); !fadesTo.empty() && parseColour(imageText, &engine.palette()) && !imageText.contains('-')) imageText += "-" + fadesTo;
            addImageFields(text, "text", std::string(textName), "", imageText, editor.variables());
        } else if (!textName.empty()) {
            const Box box = p.kind == "movie" ? Box::Movie : p.kind == "control" ? Box::Control : Box::None;
            text.push_back({.name = "text", .value = p.text, .label = std::string(textName), .box = box, .rich = p.kind == "text" || p.kind == "button"});
        }
        const auto el = f.elementOf(p.line);
        const auto options = partOptions(p.kind);
        auto fields = optionFields(editor, p, options);
        // Its style without one: the one named after its kind (Library > Styles), else default; its other styles (a
        // title's, a locked one's...) without one: its style.
        const std::string_view kindStyle = partStyle(p.kind, [&](std::string_view style) { return editor.defines("style", style); });
        for (StoryEditor::Field& field : fields)
            if (field.name == "style") field.hint = kindStyle;
            else if (field.name.ends_with("_style") && field.box == Box::Style) field.hint = p.option("style").empty() ? std::string(kindStyle) : lower(p.option("style"));
        // ...and where one without its own goes by the game's Settings (Library > Settings), what they say.
        const GameSettings& game = engine.gameSettings();
        for (StoryEditor::Field& field : fields)
            if (field.name == "glide") {
                field.usual = game.glide ? "yes" : "no";
                if (p.option("glide").empty()) field.value = field.usual;
            } else if (field.name == "glide_speed" || field.name == "glide_distance") {
                field.hint = std::format("{:g}%", 100.0f * (field.name == "glide_speed" ? game.glideSpeed : game.glideDistance));
            } else if (field.name == "scrollbar_thickness" || field.name == "fading_edges") {
                field.hint = std::format("{:g}", field.name == "scrollbar_thickness" ? game.scrollbarThickness : game.fadingEdges);
            }
        // Where it is: x, y, width and height (empty: the top left corner, and its own size, shown as it is now).
        std::vector<StoryEditor::Field> place;
        for (std::size_t i = 0; i < 4; ++i) {
            static constexpr std::array names{"x", "y", "width", "height"};
            static const std::array<std::string_view, 4> labels{"X"_text, "Y"_text, "Width"_text, "Height"_text};
            std::string hint = i == 0 ? "-1" : i == 1 ? "1" : "None";
            if (i >= 2 && el && d.screenView) { // (its own size now, as a share of its box)
                const Element& elDef = def->elements[*el];
                const SDL_FRect elBox = elDef.parent >= 0 ? d.screenView->partRect(static_cast<std::size_t>(elDef.parent)) : SDL_FRect{0.0f, 0.0f, engine.canvas().w, engine.canvas().h};
                const SDL_FRect autoSize = d.screenView->partBounds(*el);
                const float of = i == 2 ? elBox.w : elBox.h;
                if (of > 0.0f) hint = unitText((i == 2 ? autoSize.w : autoSize.h) / of);
            }
            place.push_back({.name = names[i], .value = p.option(names[i]), .label = std::string(labels[i]), .box = i == 0 ? Box::Across : i == 1 ? Box::Down : Box::Number,
                             .hint = hint, .group = i == 0 ? std::string("Place and size"_text) : std::string{}});
        }
        const float labelW = labelColumn(ui, least, right.w, {name, text, place, fields});
        if (const auto edit = drawForm(ui, id, name, right.x, right.w, labelW, y)) {
            Part changed = p;
            changed.set("name", partName(edit->value));
            change(changed, "name");
        }
        // Its text, as a box with what it can be to pick from.
        if (!textName.empty()) {
            if (const auto edit = drawForm(ui, id, text, right.x, right.w, labelW, y)) {
                Part changed = p;
                changed.text = edit->value;
                if (p.kind == "image") {
                    auto now = text;
                    if (const auto edited = std::ranges::find(now, edit->name, &StoryEditor::Field::name); edited != now.end()) edited->value = edit->value;
                    changed.text = imageFrom(editor, now, "text");
                    if (parseGradient(changed.text, &engine.palette())) changed.set("bottom", ""); // (one value now: navy-black)
                }
                changed.quoted = p.kind == "text" || p.kind == "button" || p.kind == "image" || p.kind == "movie";
                change(changed, "text");
            }
        }
        if (const auto edit = drawForm(ui, id, place, right.x, right.w, labelW, y)) {
            Part changed = p;
            changed.set(edit->name, std::string(trim(edit->value)));
            change(changed, edit->name);
        }
        y += layout::groupGap * k;
        apply(drawForm(ui, id, fields, right.x, right.w, labelW, y), fields, [&](const auto& now) { return withOptions(editor, p, now, options); });
    }
    ui.formDrawn(formBox, y);
}

} // namespace vn
