# The VNext manual

Everything VNext does, and how. You can make a whole game without reading a word of this: every box in the editor says what it sets, and every line you add with a click is written for you. Read on when you want to know what something can do, or to type a scene instead of clicking it.

This manual is for VNext 1.0 and newer. Since 1.0, nothing a game writes is renamed or removed, so everything here keeps working.

**Contents:** [Starting](#starting) · [The editor](#the-editor) · [Scripts](#scripts) · [The story](#the-story) · [Characters](#characters) · [Visuals](#visuals) · [Animations and the camera](#animations-and-the-camera) · [Effects and weather](#effects-and-weather) · [Sound](#sound) · [Time](#time) · [The phone](#the-phone) · [Things to collect](#things-to-collect) · [Screens](#screens) · [Styles](#styles) · [Actions](#actions) · [Values](#values) · [Controls](#controls) · [Settings](#settings) · [Translations](#translations) · [Sharing your game](#sharing-your-game)

## Starting

- **Open VNext.** With no game next to it, it opens in developer mode on a page that says what it opens: a game folder (a game being made) or game.vnext (a finished one). **New game** makes a game folder next to VNext, with its first scene and its menus (a title screen, the pause menu, saving and loading, Settings and the history), and opens it in the Story page. **Look again** opens a game folder or game.vnext put next to VNext since.
- **Play it.** **Play** at the editor's foot runs the game in the same window; F10 (or a controller's home button) goes back and forth between the game and the editor. A line's form has **Play from here**, and a scene's **Play scene**.
- **Everything saves as you go.** Every change is written into the game folder's script files at once, and every change can be undone.

A game folder holds:

| Folder | What's in it |
|---|---|
| `scripts/` | the story, its screens and its Library, from `main.vnscript` |
| `images/` | the visuals that stand still: backgrounds, the cast and anything else it shows; the gallery's in `images/gallery` |
| `sounds/` | music and sounds; the music room's songs in `sounds/music`, voices in `sounds/voices` |
| `movies/` | the visuals that move (the gallery's in `movies/gallery`), and their subtitles (`.srt`) |
| `fonts/` | the fonts its words are written in |
| `translations/` | the game in other languages |

Drop pictures, sounds, movies or fonts on the editor's window to add them. Pictures can be `.png`, `.jpg`, `.bmp`, `.qoi` or `.gif`; sounds `.ogg`, `.mp3`, `.flac` or `.wav`; movies `.mpg`, and through the system (Windows, macOS, and Linux with GStreamer) `.mp4`, `.mov` or `.m4v`; fonts `.ttf`, `.otf` or `.ttc`.

## The editor

The editor fits any window size, in three pages:

- **Story:** the **Scenes**, a scene's **Lines**, the **Form** of the line or scene chosen, **New line** (in three groups: **Words**: Line, Choice, Phone, Together; **Stage**: Visual, Sound, Effect, Weather, Camera; **Flow**: If, Set, Jump, Wait, Time, Screen), the **Visual types** and **Sound types** with the characters, named visuals and named sounds under them, the **Map** (every scene a box; drag one's dot to another and the story jumps there) and the **Whole story** (its words and how long it takes to read).
- **Screens:** the **Screens**, a screen's **Parts**, a **Preview** of it you drag its parts around on, its **Form**, and **New part** (Container, Text, Visual, Input, Flowchart). **+ New screen** lists the ready-made screens, each previewed as you point at it.
- **Library:** **Visuals** (pictures and movies, with the atlases and sub-images cut from them), **Sounds** (with the lines' voices), **Fonts**, **Colours**, **Styles**, **Animations** (each a timeline), **Variables** (with their values while the game plays), **Endings**, **Achievements**, **Tips**, **Controls**, **Translations** and the game's **Settings**.

Along the foot: the three pages, **Play**, the **Edit** menu (Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select all), the **Game** menu (Reload the game, Export the game, Show the game's folder), the **View** menu (move and resize page parts, reset the page's layout, the editor's language and Translate the developer tools), **About VNext** (the version and the licences of what's built in) and **Quit**.

- **Every box says what it sets**, in plain words with its unit; an empty one shows its default in grey. Lists show what you point at: animations play, styles, screens and fonts show, made-up voices can be heard.
- **Mistakes are marked in red** and said in plain words: on the line, its scene, the Map and the page's button. A line or Library item you just made keeps its own mistakes to itself while it's chosen.
- **As text** shows a line or a scene the way it's written in its script file.
- **Mouse, keys or a controller:** Tab and Shift+Tab go through every list, box and button, Enter or Space presses, Ctrl+Tab goes to the next page. A controller's shoulders, d-pad, A and B do the same, its triggers change page, and an on-screen keyboard types into boxes.

## Scripts

Everything the editor makes is plain text in the game's `scripts/` folder, so you can type a scene as well as click it. The game starts at `scripts/main.vnscript`, which has the story's beginning (`scene start`) and the game's settings; other files come in with `include`, and the editor makes them as you add characters and screens.

```
include mika
setting title "Lanterns"

scene start
    show park as background
    show mika smile
    Mika: Hi! You made it.
    "The lanterns were already lit." # a note: everything after a lone # is ignored
```

- **Indentation** is what's under what: a scene's lines, an option's lines, a screen's parts. Use spaces or tabs, as long as a block is lined up.
- **Words with spaces go in quotes**, `"like this"`; a line of narration is in quotes too.
- **Names** (of scenes, variables, characters, screens...) are one word of letters, numbers, `_` and `-`. Case doesn't matter.
- **A note** starts with `#`, at the start of a line or after a space (`#ff0000` is still a colour).
- **Amounts** are percentages or fractions: `zoom=150%` and `zoom=1.5` are the same. Times are in seconds: `fade=0.5`.

## The story

### Scenes and lines

A story is scenes, and a scene is lines. `jump` goes to another scene; a scene that runs out of lines ends the story, unless something jumped there and comes back.

```
scene festival
    show park as background
    show haru calm at left
    Haru: Yo. You made it.
    Haru smile: Lanterns first, or food?
    Haru (happy): Either way, I'm buying.
    "The sky was turning orange."
    Haru: Come on. voice=haru_come_on
    jump lanterns
```

- **Someone says something:** `Name: words`, or with their expression, `Name expression: words` (or `Name (expression): words`). Someone who isn't one of your characters can speak too: `Stranger: Excuse me...`
- **Narration** is a line in quotes.
- **A voice** for the line: `voice=NAME` at its end, a sound in `sounds/` (or `sounds/voices/`). A character can also have a voice file for every line (`voice_files=` on their character line), and without one, a made-up voice: babble as the line types out.

### Words

Text in lines, choices, captions and screens can be styled with tags:

| Tag | What it does |
|---|---|
| `{bold}...{/bold}` `{italic}...{/italic}` `{underline}...{/underline}` `{crossed_out}...{/crossed_out}` | the look |
| `{colour red}...{/colour}` | a colour (a name, or `#rrggbb`) |
| `{speed 20}...{/speed}` | letters a second for the words in it |
| `{pause 1}` | stops for a second (none: until a click) |
| `{appears word}` | the rest appears a word at a time (or `letter`, or `line`) |
| `{no_wait}` | the line goes on to the next by itself |
| `{furigana かんじ}漢字{/furigana}` | small reading text over the words |
| `{tip NAME}words{/tip}` | the words open a tip, and the player has found it |
| `{NAME}` | a value: a variable, `{player_name}`, or a built-in one like `{time_clock}` |

```
Mika: {bold}Wait.{/bold} You've been here {colour #ffd25a}{visits}{/colour} times?{pause 0.5} Really?
```

A whole page of text, novel style, is a textbox screen of its own (see [Screens](#screens)).

### Choices

```
choice
    "Share your umbrella"
        set kindness to kindness + 1
        Haru: ...Thanks.
    "Ignore him"
        jump alone
    "Ask about the festival" (needs asked_mika: Ask Mika first)
        Haru: Mika told you?
    "Leave early" (if time_part is 'night')
        jump home
```

- **An option's lines** run when it's picked; then the story goes on after the choice. An option without lines just goes on.
- `(needs CONDITION)` shows the option locked until the condition is true, and `(needs CONDITION: words)` says what it needs. `(if CONDITION)` shows the option only when it's true.
- **A time limit:** `choice for 5 seconds` (the player's Time limits setting can turn it off). Without an answer, the story goes on after the choice.
- `choice repeat=yes` comes back after each option until one jumps away; `choice optional=yes` can be clicked past.
- `choice for 3 lines` stays up while the next three lines are said, and `return=yes` comes back to where the story was after an option.
- **Options picked before** are marked in the choice menu, and the flowchart can take players back to any choice.

### Conditions, variables and set

```
variable kindness 0 "how kind the player has been"
variable player_name "Sam"

set kindness to kindness + 1
set seen_ending to yes
if kindness is at least 3 and not seen_ending
    Haru happy: You're... really nice, you know?
else if kindness is 0
    Haru cold: ...
else
    Haru: Hm.
```

- **Values** are numbers, `yes` and `no`, and `"text"`. `set` takes a formula: `+ - * /`, brackets, `and`, `or`, `not`, `is`, `is not`, `is more than` (or `greater than`), `is less than` (or `fewer than`), `is at least`, `is at most`, and `random(1, 6)`, `min`, `max`, `round`, `abs`, `int`, `str`, `len`, `upper`, `lower` and `if(condition, then, otherwise)`.
- **Variables last** for the play-through (saved with it). One named `persistent.NAME` lasts between play-throughs: `set persistent.met_mika to yes`.
- `variable` lines say what a variable starts as and what it's for; the Library's Variables lists them, with their values while the game plays.

### Jump, wait, together

```
jump rooftop                   # goes to the scene
wait 1.5                       # waits a second and a half (wait alone: until a click)
together
    Mika: Happy birthday!
    Kaede: Happy birthday!
```

`together` shows its lines at once, each in its speaker's textbox, and the story waits for the last.

## Characters

```
character Mika name_colour=#b48ad6 made_up_voice=high love=0
```

A character line names them, with how they look and sound when their lines don't say. Their pictures are the visuals named after them (`mika.png`, or an atlas called `mika`); their form in the Story page has every setting:

| Setting | What it sets |
|---|---|
| `image=` | the name their visuals start with (none: their own name) |
| `name_colour=` `text_colour=` | their name's colour and their words' |
| `at=` `y=` `layer=` `zoom=` `opacity=` `rotation=` `flip=yes` `tint=` | where they stand and how they look, unless a line says |
| `animation=` `animation_on_show=` | what they play while shown, and as they come |
| `text_appears=` | `letter`, `word` or `line` for their lines |
| `textbox=` | a textbox screen of their own (a speech bubble, say) |
| `voice_files=` | a folder with a voice file for every line |
| `made_up_voice=` `made_up_voice_speed=` | `high`, `medium`, `low`, `deep` or a pitch in Hz, or `no` |
| `typing_sound=` | a sound for each letter, instead of a voice |
| `blink=` `seconds_between_blinks=` `mouth_moves=` `mouth_speed=` | blinking and talking |
| `face=` `face_side=` `face_copies=` `face_flip=` `face_blink=` `face_mouth=` | a face beside the textbox |
| `contact=` `avatar=` `about=` `chat=` `call=` `ringtone=` | the phone: in the contacts when, their picture, and what happens when the player texts or calls them |
| `love=` | a love interest, with their love at the start |

**Expressions, blinking and talking** come from an atlas: one picture with a frame for each expression, and the eyes and mouth in frames of their own, which VNext moves as they blink and talk:

```
atlas mika frames_in_a_row=3
    expressions calm smile happy surprised flustered annoyed sad shocked cold
    eyes open half closed image=mika_eyes x=330 y=530
    mouth closed half open image=mika_mouth x=490 y=750
```

`show mika happy`, or a line `Mika happy: ...`, shows that frame. A second atlas, `mika_close`, gives expressions like `close_smile` from it. Pictures named `mika_happy.png` and so on work too, without an atlas.

**Love:** a character with `love=` has a love value, `{mika.love}`, that options raise (`set mika.love to mika.love + 10`). `{most_loved}` is who's loved most, `container list=loves` lists them, and the ready-made Relationships screen shows them in hearts.

## Visuals

Everything on the stage is a visual: backgrounds, characters, pictures, movies and coloured boxes. Each has a type, and each type a layer: higher layers are in front.

| Type | Layer | |
|---|---|---|
| `background` | 0 | the scene's back: one at a time |
| `character` | 1 | the cast |
| `image` | 2 | anything else |
| `movie` | 3 | moving pictures |

```
show park as background
show mika smile at left
show haru calm at right y=-1 zoom=110% with fade_in
show letter at 0 y=0.3 as image
show sunset_movie as background
show mika happy                 # just a new expression: she stays where she is
hide mika with fade_out and wait
```

- **Where:** `at` a place, from `-1` (left) to `1` (right): `left`, `centre`, `right`, or a name from a `place` line (`place window_seat -0.6`); `y=` from `-1` (the bottom) to `1` (the top). A visual shown again comes to the front of its layer.
- **How it looks:** `zoom=`, `opacity=`, `rotation=` (degrees), `flip=yes`, `tint=` (a colour it's lit by), `layer=` (any whole number; below 0 is behind the background), `width=` and `height=` (a coloured box's, or a named visual's).
- **Animations:** `with ANIMATION` plays one as it comes (or goes, on `hide`), `animation=NAME` plays one for as long as it's shown, and `and wait` waits for it.
- **Backgrounds** can be a picture, a colour (`show #102040 as background`), a colour fading to another (`show navy as background bottom=black`), or a movie. `at` and `y=` pick the part of a picture bigger than the stage that shows.
- **Movies** play wherever a picture goes: `loop=no` plays one once, `volume=` sets its sound. Subtitles come from an `.srt` file with the movie's name.

**Types of your own:** `type NAME layer=N accepts=character,image,movie,colour`, then `show NAME as TYPE`. A colour shown as a type that takes colours is a box: `show blood_dark as shade opacity=34%`.

**Named visuals:** `visual NAME type=TYPE shows=WHAT` gives a visual a name of its own, with how it comes (`at=`, `y=`, `zoom=`, `opacity=`, `rotation=`, `flip=`, `tint=`, `layer=`, `animation=`, `animation_on_show=`; a movie's `loop=` and `volume=`; a box's `width=` and `height=`):

```
type shade layer=7 accepts=colour
colour blood_dark #1c0006
visual rain type=image shows=rain_tile at=0 y=1 animation=rainfall
```

**Atlases and sub-images** cut pictures from one file: `sub-image NAME cut_from=FILE x= y= width= height=` (in pixels), and `atlas` lines as above.

## Animations and the camera

An animation is what it changes, line by line:

```
animation slide_in
    x=-2 opacity=0
    x=home opacity=100% over 0.4 seconds ease=out

animation heartbeat loop=yes
    zoom=110% from 0 to 0.1 seconds ease=out
    zoom=100% from 0.1 to 0.4 seconds ease=in
```

- **What it changes:** `x=` (or `left=`, `right=`) and `y=` (or `up=`, `down=`), where `home` is its own place and `-2`/`2` just off the stage; `opacity=`, `zoom=`, `rotation=`, `colour=`, `fade=` (covered by a colour; `none`: back from it) and `wipe=` (how much of it shows, `shape=IMAGE` for the shape it shows in).
- **When:** a line without a time is where it starts; `over N seconds` after the line before; `from A to B seconds`; `at N seconds` (at once); `wait N seconds`.
- `ease=` `linear`, `in`, `out`, `inout`, `bounce`, `spring` or `overshoot`; `loop=yes` (or a number of times) on the animation's line.
- The Library's Animations has each one as a timeline, playing as you edit it.

**The camera** moves the whole stage with an animation: `camera with shake` plays one now, `camera animation=drift` plays one from now on, and `and wait` waits for it. Screens stay where they are, so a textbox doesn't shake.

```
animation shake
    x=0.03 zoom=110%
    x=-0.03 over 0.05 seconds
    x=0 zoom=100% over 0.2 seconds ease=out

camera with shake
```

## Effects and weather

```
effect greyscale fade=1          # the stage in grey, fading in over a second
effect glitch for 0.3 seconds    # just for a moment
effect blur amount=40%
stop effect fade=1

weather rain amount=150% wind=-0.5
weather petals image=heart behind=yes
stop weather fade=2
```

- **Effects:** `greyscale`, `sepia`, `invert`, `blur`, `pixelate`, `noise`, `glitch` and `vibration` (the players' controllers rumble while it lasts), several at once; `amount=` how strong, `fade=` to fade in, `for N seconds` to last just that long.
- **Weather:** `rain`, `snow`, `petals`, `leaves` or `embers`; `amount=`, `speed=`, `wind=` (from left to right: below 0 blows left), `tint=`, `image=` (a picture to fall instead), `behind=yes` (behind the visuals), `fade=`.
- `stop effect` and `stop weather`, each with `fade=`, end them.

## Sound

```
play rain_loop
play theme as music fade=2
play door as sound volume=50%
stop music fade=3
stop rain_loop
```

`play NAME` plays a sound from `sounds/` (or a named sound), and `stop` stops one by its name, or all of a type: `[as TYPE] [loop=yes|no] [volume=] [fade=] [fade_out=]` (`fade=` fades it in; `fade_out=` the one before out).

**Sound types:** every game has `music` (one at a time, looping, on the player's music volume, fading from one song to the next and quieter while someone speaks), `sound` and `voice`. Make your own with `type NAME follows=music|sound|voice one_at_a_time=yes loop=yes volume=60%`, and give a sound a name with how it plays: `sound NAME type=TYPE plays=FILE loop= volume= fade= fade_out=`.

```
type ambience follows=sound one_at_a_time=yes loop=yes volume=60%
sound night_city type=ambience plays=city_loop fade=2
```

**Captions** say what a sound is for players with Captions on: `caption door "A door slams"` (it can be bold, coloured and the rest). **Voices** on lines: `voice=NAME` (see [Scenes and lines](#scenes-and-lines)); the words can keep time with the voice (Settings: Text follows voices).

## Time

A game can have its own clock and calendar, the player's real one, or days in parts: `setting time story` (or `real`, or `days`), with where it starts (`time_start`: a weekday or a date, and a time), its parts of the day (`time_parts`) and a 12- or 24-hour `clock` in Settings.

```
setting time story
setting time_start Monday 8:00

scene start
    time Friday 18:00 day=3   # goes on to Friday evening, day 3
    time evening              # on to the evening
    time passes 2 hours
```

`{time_date}`, `{time_clock}`, `{time_day}`, `{time_part}`, `{time_weekday}`, `{time_month}` and `{time_year}` are the time now, for texts and conditions (`if time_weekday is 'Friday'`). Backgrounds follow the time (`park.png` in the morning, `park_night.png` at night, by the part's name), so does the phone, and the ready-made calendar shows each new day.

## The phone

The ready-made phone is one screen, `phone`, with a lock screen, apps and a clock, all yours to restyle. A Phone line in the Story page starts a chat:

```
chat Haru
    me: hey
    Haru is typing for 2 seconds
    Haru: k
    Haru sends sticker cat_wave
    Haru: wanna go to the summer festival with me?
    choice
        "...fine. if you insist."
            Haru reacts ♡
        "Who's asking?"
            me: who's asking?
            me deletes it
```

- **A chat's lines:** `Name: words`, `me: words` (the player), `Name sends IMAGE` (`sends sticker NAME`, or `sends voice NAME: what it says`), `Name is typing [for N seconds]`, `Name reads it`, `Name reacts ♡`, `Name edits it: new words`, `Name deletes it`. A choice in a chat shows its options on the phone, and the one picked goes out as the player's message.
- **A call:**

```
phone call Mika for 10 seconds
    Mika: Where are you two?!
    missed:
        Mika: Fine. Don't answer.
```

The call rings until it's answered or declined; its lines are the call, and `missed:` is what happens when nobody picks up.

- **The player texts and calls:** a character with `contact=yes` is in the contacts, and their `chat=SCENE` or `call=SCENE` is what happens when the player opens their chat or calls them.
- **Its settings** (Settings' Phone group): who has it (`has_phone`), its owner, wallpapers, passcode, clock and date, signal and battery, how new messages come, typing speed, and its sounds. A badge on the textbox counts new messages, and controllers buzz when one comes.

## Things to collect

| Line | What it is | Reached by |
|---|---|---|
| `gallery IMAGE "Title"` | a gallery picture | showing it, or `set persistent.gallery.IMAGE to yes` |
| `song NAME "Title"` | a music room song | playing it, or `set persistent.song.NAME to yes` |
| `ending NAME "Title" [hint="..."] [scene=SCENE]` | an ending | its scene ending, or `set persistent.ending.NAME to yes` |
| `achievement NAME "Title" "What for" [secret=yes]` | an achievement | `set persistent.achievement.NAME to yes` |
| `tip NAME "Title" "What it says"` | a tip | a line showing `{tip NAME}...{/tip}` |

Pictures in `images/gallery` (and movies in `movies/gallery`) and songs in `sounds/music` join the gallery and the music room by themselves. All of it lasts between play-throughs; a button with the `reset progress` action forgets it. The ready-made Extras, Gallery, Music room, Endings, Achievements, Tips and Flowchart screens show them.

## Screens

Every menu is a screen made of parts: the title screen, the pause menu, saving and loading, Settings, the history, choices, questions, the phone... **+ New screen** adds a ready-made one (Title, Pause, Save, Load, Settings, History, Confirm, Choice, Saved, Phone, Credits, Calendar, Relationships, Input, Extras, Gallery, Music room, Endings, Achievements, Tips and Flowchart), already working and all yours to restyle; the menus get a button for each screen added after them, and Pause, History and Phone bring the keys that open them. The game also has built-in screens it uses unless you make your own with the same name: `textbox`, `choice`, `captions`, `keyboard` and `saved` (the notice after a quick save).

```
screen title story=waits animation_on_open=fade_in
    image title_park x=0 y=0 width=1 height=1
    text "Lanterns" x=0 y=0.55 width=0.9 height=0.2 style=logo
    container x=0 y=-0.35 width=360px stack=down spacing=10px
        text "New game" x=0 width=1 height=52px style=menu_button action=start
        text "Load" x=0 width=1 height=52px style=menu_button action="open load" clickable=has_save
        text "Quit" x=0 width=1 height=52px style=menu_button action=quit confirm="Quit the game?"
```

- **A screen's line:** `story=goes_on` lets the story go on under it (else it waits), `action_on_open=` and `action_on_close=`, `animation_on_open=` and `animation_on_close=`, `stop_on_open=` and `stop_on_close=` (sound types it stops), `close_after=` (seconds).
- **In the story:** `open NAME [with ANIMATION] [and close with ANIMATION]` opens one and waits for it to close; `show screen NAME` and `hide screen NAME` show one over the story.
- **Places and sizes:** `x=` and `y=` from `-1` to `1` in the box it's in; `width=` and `height=` as a part of that box (`width=0.5`) or in pixels (`width=360px`). A container without a size in a stack takes the room the others leave.
- **Any part:** `when=CONDITION` shows it only then, `style=` its look (or a style's settings right on it: `size=40 colour=red`), `animation=` while shown, and with `action=` it's a button: `keys=`, `confirm=`, `clickable=`, `chosen=`, `click_animation=`, `drag=` and `drop=`.

The five kinds of part:

| Part | What it is |
|---|---|
| `container` | a box for other parts: stacked (`stack=down` or `across`, wrapping onto rows), scrolling (`scroll=`, `scrollbar=yes`, `edges=fade`), a list (`list=`), an app on the phone (`app=NAME title= icon=`), or another screen inside it (`screen=NAME`) |
| `text "Words"` | words, wrapped in its width (`width_at_most=`, `lines=`); with an action, a button. A text showing `{dialogue}` is the line said |
| `image NAME` | a visual: a picture, a colour (`bottom=` for one fading to another), a movie (`loop=`, `volume=`), a named visual or a character (`expression=`); `fit=contain`, `cover` or `stretch`, `repeat=`, `scroll=`. An image showing `{face}` is the speaker's face |
| `input` | what the player sets: a box to type in (`variable=NAME most_letters= placeholder=`), a slider (a variable with `minimum=`, `maximum=`, `step=`, `vertical=yes`, `track=`, `fill=`, `knob=`; or `setting=` a player setting; `value=` just shows one), or a control's key (`control=NAME slot=N waiting=`) |
| `flowchart` | the story's choices and endings as a tree; clicking one goes back to it |

**Lists:** a container with `list=` repeats its parts once for each item, `columns=`, `item_width=`, `item_height=`, `spacing=`: `options` (a choice's), `history` and `lines`, `pages` and `saves`, `gallery`, `endings`, `songs`, `achievements`, `tips`, `apps`, `chats`, `messages`, `calls`, `people`, `notices`, `captions`, `subtitles`, `loves`. Its parts read their item's values: `{option.text}`, `{save.number}`, `{chat.name}`, `{song.name}`, `{achievement.title}`...

```
container list=options x=0 y=0 width=0.6 height=0.5 item_height=0.08 spacing_down=0.01
    text "{option.text}" x=0 y=0 width=1 height=1 style=choice action=choose when="not option.locked"
```

## Styles

A style is a look to give parts: `style NAME [based_on=STYLE]` and its settings.

```
style menu_button size=36 font=rounded colour=#ffffff align=centre align_vertically=centre background=#2b2563 border=#5b51b8 pointed_background=#3b3392 chosen_background=#4d41c4 unclickable_colour=#9696a5
```

- **Text:** `font=`, `size=`, `colour=` (`speaker`: the speaker's name colour), `align=` (`left`, `centre`, `right`), `align_vertically=` (`top`, `centre`, `bottom`), `line_spacing=`, `shadow=`, `shadow_colour=`, `shadow_offset=`.
- **Box:** `background=`, `border=`, `image=` (a picture behind it), `padding=`, `opacity=`.
- **Looks:** `pointed_`, `chosen_` and `unclickable_` with `colour`, `background`, `border` or `image`, for a button pointed at, chosen or locked.
- **Settings it follows:** `text_size_follows=yes` and `opacity_follows=yes` (the player's Text size and Textbox opacity). **Sounds:** `pointed_sound=` and `click_sound=`.
- **Fonts:** `font NAME "file.ttf" [style]`, from `fonts/`. **Colours:** `colour NAME #rrggbb` (`#rrggbbaa` with its opacity), usable anywhere a colour is.

## Actions

What a button, a key or a screen does, in `action=` (several with `;` between them: `action="quick save; open saved"`). Most story lines work as actions too (`show`, `hide`, `play`, `stop`, `set`, `jump`, `effect`, `weather`, `camera`, `open`).

| Action | What it does |
|---|---|
| `start` (`start SCENE`), `continue`, `quit` | play from the beginning (or that scene), carry on from the last save, quit |
| `close`, `back`, `yes`, `no`, `answer WORDS` | close the screen on top; answer a question |
| `next line`, `skip`, `auto`, `rollback` (`rollback N`) | the story's own: on, skipping, auto-play, back |
| `quick save`, `quick load`, `save N`, `load N`, `delete save N`, `page N` (`auto`, `quick`), `next page`, `previous page` | saves |
| `choose` (`choose N`), `replay N`, `play song NAME`, `open image NAME`, `next image`, `previous image`, `open tip NAME`, `put back` | lists' items |
| `open chat NAME`, `phone call NAME`, `answer call`, `decline call`, `hang up`, `open app NAME`, `keypad N`, `unlock` | the phone |
| `type LETTER` (`shift`, `space`, `left`, `right`, `delete`, `done`) | the on-screen keyboard |
| `setting NAME VALUE`, `reset settings`, `reset controls`, `reset progress`, `fullscreen`, `screenshot` | the player's settings and progress |
| `move up`, `move down`, `move left`, `move right`, `press`, `scroll up`, `scroll down`, `scroll to the top`, `scroll to the end` | moving through a menu |
| `open https://...` | a web page |

## Values

Anything in `{braces}` in a text, or in a condition, is a value: your variables, a list item's, and these built-in ones.

- **The story:** `speaker`, `dialogue`, `speaking`, `choosing`, `text_complete`, `time_left` (a timed choice's, 100 down to 0), `skip`, `auto`, `in_game`, `can_save`, `can_rollback`, `has_save`, `has_quick_save`, `page`, `save_mode`, `question`, `prompt`, `title`, `version`, `language`, `languages`, `developer`.
- **Collections:** `gallery_total`, `gallery_unlocked`, `endings_total`, `endings_reached`, `songs_total`, `songs_heard`, `achievements_total`, `achievements_earned`, `tips_total`, `tips_found`, `tip`, `tip_title`, `tip_text`, `open_image`, `open_image_text`, `flowchart_points`, `NAME.love`, `most_loved`.
- **The phone:** `has_phone`, `phone_app`, `phone_locked`, `phone_needs_code`, `phone_code`, `phone_wrong`, `phone_unread`, `in_chat`, `open_chat`, `open_chat_messages`, `messages_total`, `messages_unread`, `chats_total`, `people_total`, `in_call`, `caller`, `caller_avatar`, `caller_colour`, `caller_initial`, `call_incoming`, `call_outgoing`, `call_busy`, `call_choice`, `call_time`, `calls_total`, `calls_missed`.
- **Time:** `time_date`, `time_clock`, `time_day`, `time_part`, `time_weekday`, `time_month`, `time_year`.
- **The player's settings,** by their names (`text_speed`, `master_volume`...), and `window_size`, `fullscreen`, `keyboard_text`, `keyboard_shift`, `has_face`, `face_side`, `menu_button`, `phone_button`, `dragging`, `thumbnail`.

## Controls

Every key, mouse button and controller button is a control the player can change in Settings:

```
control quick_save keys="f5" action="quick save; open saved" when=can_save
control history keys="l pad_x swipe_up" action="open history"
```

`keys=` lists its keys (`space`, `return`, `escape`, `a`, `f5`, `cmd+s`, `mouse_left`, `mouse_right`, `pad_a`, `pad_start`, `pad_back`, `swipe_up`, `two_finger_tap`...), `action=` what it does, `in_menus=yes` whether it works with a menu open, and `when=` a condition. Every game has `next_line`, `skip`, `auto`, `quick_save`, `quick_load`, `move_up`, `move_down`, `move_left`, `move_right` and `press`, and the developer tools' own (`tools_undo` and the rest), all changeable the same way.

## Settings

`setting NAME VALUE` lines in `main.vnscript`, which the Library's Settings shows as a form. Those starting with `default.` are what a new player starts with; players change them in the game's Settings.

| Group | Settings |
|---|---|
| Game | `title`, `version`, `save_folder`, `language`, `game_size W H` (1280 720), `icon`, `letterbox_colour`, `resizable`, `default.fullscreen`, `default.scaling` (`fit`, `integer`, `stretch`, `fill`), `default.vsync`, `default.frame_limit`, `mouse_pointer`, `pointer_tip X Y` |
| Saves | `autosave`, `save_pages`, `page_slots` |
| Text | `text_appears`, `default.text_speed`, `default.auto_delay`, `auto_letter_delay`, `default.textbox_opacity`, `narration_colour`, `history_length`, `default.rollback`, `rollback_lines`, `default.skip_unread`, `default.skip_after_choices`, `default.skip_movies`, `skip_time`, `hold_to_skip` |
| Accessibility | `default.text_size`, `default.high_contrast`, `default.read_aloud`, `default.captions`, `default.animations`, `default.time_limits`, `default.vibration` |
| Visuals | `image_scaling` (`smooth`, `pixel`), `character_height`, `animation_on_show`, `animation_on_hide`, `move_time`, `load_ahead`, `image_memory` |
| Screens | `glide`, `glide_speed`, `glide_distance`, `scrollbar_thickness`, `fading_edges` |
| Sound | `default.master_volume`, `default.music_volume`, `default.sound_volume`, `default.voice_volume`, `music_crossfade`, `music_under_voices`, `text_follows_voices`, `default.made_up_voices`, `made_up_voice_speed`, `default.typing_sounds`, `typing_sound_gap`, `typing_sound_volume` |
| Characters | `mouths_move`, `default.mouths`, `mouth_speed`, `blink`, `seconds_between_blinks` |
| Time | `time` (`none`, `story`, `real`, `days`), `time_start`, `time_parts`, `clock` (`24`, `12`), `phone_follows_time`, `backgrounds_follow_time` |
| Phone | `has_phone`, `phone_owner`, `phone_wallpaper`, `phone_lock_wallpaper`, `phone_passcode`, `phone_clock`, `phone_date`, `phone_signal`, `phone_battery`, `new_messages`, `chat_messages`, `phone_typing`, `phone_history`, `phone_message_sound`, `phone_send_sound`, `phone_typing_sound`, `phone_ringtone`, `phone_calling_sound` |

**For players:** saves with pictures, autosave, quick save and load, rewind (rollback), skip and auto-play, a history that replays voices and goes back, and settings for text speed, text size, volumes, high contrast, reading aloud (through VoiceOver, Narrator, NVDA, JAWS or Orca when one is running), captions, time limits, animations, vibration, language and every control. [Accessibility](.github/ACCESSIBILITY.md) says what VNext does for players and its limits.

## Translations

The Library's **Translations** adds a language and shows every text of the game beside its translation; it exports a spreadsheet (`.csv`, next to the game folder) for a translator to fill in, and imports it back. Each language is a file in `translations/` (`translations/fr.vnscript`); players pick one in Settings, and a game can switch with `setting language next` (or `previous`, or a language). Right-to-left scripts and Japanese and Chinese line breaking work as they should. The developer tools can be translated too (View: Translate the developer tools).

## Sharing your game

- **Export the game** (the Game menu) makes a zip for Windows, macOS and Linux next to the game folder, named after the game, each with its player, every picture, sound and line in one file (`game.vnext`), and `licences.txt`. Exported on a Mac, the Mac one is an app with your game's name and icon. Nothing is downloaded: every VNext carries the other two systems' players.
- **A finished game** never opens the editor. A `game.vnext` put next to VNext plays too, and keeps its players' saves and progress in that file; otherwise saves go in the player's own folder, named after the game's title (or `save_folder`).
- **From the command line:** `VNext [--game FOLDER or PACK] [--data FOLDER] [--scene NAME] [--screenshot FILE.png] [--voices] [--translate LANGUAGE] [--pack [FILE]]`: play a game from somewhere else, keep the player's data in a folder, start at a scene, take a screenshot, write `voices.txt` (every line that plays a voice file, a script for recording them), write or bring up to date a translation's file (`translations/LANGUAGE.vnscript`), or pack the game as `game.vnext`.
