<h1 align="center"><img src=".github/banner.png" alt="VNext" width="800"></h1>

**Make a visual novel without writing a single line of code.** VNext is one free app for Windows, macOS and Linux: you write the story, design the menus and play the game in the same window, and your characters can even text and call the player.

**[Download VNext](https://github.com/zsande/VNext/releases/latest)**: one app, nothing else to install. It's free, also for games you sell. The **[manual](MANUAL.md)** shows everything it does, and how.

<p align="center"><img src=".github/make-a-scene.gif" width="100%" alt="The VNext editor: a new line is added with a click, its words are typed in, Mika is picked as the speaker, and Play from here shows her (a stand-in silhouette) saying it in the park"></p>

## Why it's easy

Most visual novel engines start with a scripting language to learn or a game engine to install. VNext starts with a window you click in.

- **No code, ever.** A scene is a list of lines you add with a click: pick who speaks and their expression from pictures, then type what they say. Menus are pictures of the game you drag things around on. Everything is also saved as plain, readable script files, if you'd rather type.
- **One app, nothing to set up.** The editor and the game are the same program: no game engine, plugins, Python or command line. Click any line and Play from here shows it in the game; F10 takes you back to the editor.
- **Samples to start with.** New game can start you off with VNext's samples: a cast of three (nine expressions and two close-ups each), six places, five songs, sounds and every menu dressed in pictures of its own, ready to use, change or swap for yours.
- **Menus come ready-made.** A title screen, a pause menu, saving and loading, settings, a history, a confirm question, a phone and more are a click each, already working and all yours to restyle. A line that needs one brings it: a chat brings the phone.
- **Export, and it's a game.** Export the game makes a zip for Windows, macOS and Linux, named after your game, each with its player, every picture, sound and line in one file, and the licences. Exported on a Mac, the Mac one is an app with your game's name and icon.
- **Everywhere.** Make your game on Windows, macOS or Linux and export it for all three: it plays with a mouse, a keyboard, a controller or a touch screen.
- **Made to last.** Since 1.0, a game made with VNext keeps working in every new version: they only add, never take away.
- **Free.** Also for games you sell: no royalties, no account and no splash screen of ours (zlib licence).

## The phone

<p align="center"><img src=".github/phone.gif" width="560" alt="A chat in a VNext game: next to Kaede (a stand-in silhouette), her phone shows her sending Haru a message and deleting it, then “hey”, Haru reading it and typing, his “k” and his invitation to the summer festival, her “...fine. if you insist.” and his heart"></p>

Your characters text the player. Their messages type out as they write them, and they send images, stickers and voice messages, react, read what the player sends, and edit or delete what they just said. A choice in a chat shows its options on the phone, and the one picked goes out as the player's message. Calls ring until they're answered or declined, and when nobody picks up, the missed call has a story of its own. Players can text and call characters themselves, a badge on the textbox counts new messages, and controllers buzz when one comes. The phone has a lock screen with a passcode, wallpapers, apps (the game's gallery, music room, saves and settings among them), a clock, a date, signal and battery, all of which the story can change, and it's made of ordinary screen parts, so every bit of it can be restyled.

## Everything else your story needs

- **Characters** with expressions, eyes that blink and mouths that move as they talk, a face beside the textbox, their own name colour and textbox (speech bubbles too), and made-up "speech" or typing sounds when there's no voice.
- **Words** that can be bold, italic, underlined, crossed out or coloured, speed up or slow down, pause, appear a letter, a word or a line at a time, carry furigana, show a variable, or unlock a tip that explains them, in a textbox or as whole pages of text, novel style.
- **Choices** with a time limit, options locked until something is true or marked once picked, and menus that come back after an option. Variables and conditions keep track of everything, and some of them last between plays.
- **Love interests**: each character's love, raised by the options picked, a Relationships screen with hearts, and endings reached as a scene ends.
- **Time**: the story's own clock and calendar, the player's real one, or days split into morning, afternoon, evening and night. Backgrounds and the phone follow it, and a calendar shows each new day.
- **The stage**: visuals of every kind on layers (backgrounds, characters, images, movies, coloured boxes, and kinds of your own), that come, go and move with animations; effects (greyscale, sepia, invert, blur, pixelate, noise, glitch, and vibration players feel in their controller), weather (rain, snow, petals, leaves, embers), a camera that zooms, pans and shakes, and movies with subtitles wherever a picture goes.
- **Sound**: music that fades in, out and from one song to the next and quietens while someone speaks, sounds and loops of kinds you set up (one at a time or many, looping or not, on the player's music, sound or voice volume), captions that say what they are, and a voice file for every line, with the words keeping time with the voice.
- **Screens**: a title screen, pause menu, saving and loading, settings, a history, a choice menu, questions, the phone, credits, a calendar, relationships, a box to type a name into and an on-screen keyboard, all ready-made, fitting wide and tall games, and yours to restyle. Or build your own from five kinds of part (texts and visuals that do something when clicked, boxes to type in and sliders, the flowchart, and boxes that hold others: lists of anything, scrolling with scrollbars and fading edges), with styles, drag and drop, screens shown inside other screens, and animations made on a timeline.
- **Extras**: a gallery and a music room (pictures and songs dropped in their folders join by themselves), endings, achievements, tips, and a flowchart that takes players back to any choice, each a ready-made screen.
- **For players**: saves with pictures, autosave, quick save and quick load, rewind, skip and auto-play (Menu, Skip, Auto, Q.Save, Q.Load and Phone buttons on the textbox, and keys), a history that replays voices and goes back, and settings for text speed, text size, volumes, high contrast, reading aloud (through VoiceOver, Narrator, NVDA, JAWS or Orca when one is running), captions, time limits, animations, vibration, language and every key and button. Every menu works with a mouse, a keyboard or a controller, with an on-screen keyboard for typing a name. [Accessibility](.github/ACCESSIBILITY.md) says what VNext does for players and its limits.
- **Every language**: translations, with a spreadsheet for translators, right-to-left scripts, Japanese and Chinese line breaking, and the developer tools themselves can be translated too.

<p align="center">
  <img src=".github/game.png" width="48%" alt="A VNext game: Haru (a stand-in silhouette) in a park with falling petals, asking “So. Lanterns first, or food?”, with Phone, Skip, Auto, Q.Save and Q.Load buttons on the textbox">
  <img src=".github/call.png" width="48%" alt="A VNext game: Mika calling the player, her incoming call on the phone with Decline and Answer, while Haru (a stand-in silhouette) waits in the park">
</p>

## The editor

Open VNext in developer mode and you get an editor that fits any window size, in three pages:

- **Story**: scenes and their lines as forms, every kind of line a button away, the kinds of visual and sound the game has, and the map, where every scene is a box: drag one's dot to another and the story jumps there.
- **Screens**: the title screen, menus and textbox, designed by dragging their parts around on a picture of the game; every ready-made screen previewed before it's added.
- **Library**: visuals (pictures and movies), sounds and voices, and fonts (drop files on the window to add them), the colours, styles, animations, variables, endings, achievements, tips and controls a game uses (with the variables' values shown as you play), the game's settings, and its translations, with a spreadsheet for translators.

Every part of a page can be moved and resized. The form of a scene or a line plays it: Play scene, or Play from here to see the game from that line. Along the editor's foot run its pages, Play, and the Edit, Game and View menus (Undo and Redo, Export the game and the rest), with the same keys you know. All of it works with a mouse, the keys or a controller. Every box says in plain words what it sets, lists show what you point at (animations play, styles and screens show), mistakes are marked in red and said in plain words, and every change can be undone.

<p align="center"><img src=".github/design-a-screen.gif" width="100%" alt="The Screens page: the title screen's menu is dragged lower on the preview, its New game button renamed Start the story in its form, and Play shows the title screen with it there"></p>

<p align="center">
  <img src=".github/editor-story.png" width="48%" alt="The Story page: a scene's lines, and the form of the chosen one: Haru saying “Yo. You made it.”">
  <img src=".github/editor-chat.png" width="48%" alt="The Story page on a chat: its phone lines, and the form of Haru's reaction">
  <img src=".github/editor-screens.png" width="48%" alt="The Screens page: the phone's parts, the phone with an app button chosen, and the button's settings">
  <img src=".github/editor-map.png" width="48%" alt="The Story page with its map made big: eight scenes as boxes, with arrows from start to two endings">
</p>

## How it's made

The idea, the design and the direction of VNext are mine. Every line of its code is written by Claude, an AI made by Anthropic, and so are the characters in the pictures, drawn in code. I decide what VNext should be, test what comes out and say what to change; Claude writes the code and tests it.

## Download

Get VNext from GitHub's [Releases page](https://github.com/zsande/VNext/releases): a zip for Windows, macOS or Linux, with the player (on Linux, an AppImage), licences.txt, and the other two systems' players, so Export the game makes all three without downloading anything. VNext isn't signed by Apple or Microsoft yet, so the first time, macOS asks you to allow it in System Settings → Privacy & Security (Open Anyway), and Windows says it protected your PC (More info, then Run anyway). The [code signing policy](CODE_SIGNING.md) says how the players are signed.

On the same page, **Lanterns** (`game.vnext`) is a short game made with the samples: three classmates, one night of lanterns and four endings. Put it next to VNext and open VNext to play it.

## Status

VNext 1.0 is here, and from now on games keep working: new versions add features, and nothing a game uses is renamed or removed. VNext is made and tested on macOS; the Windows and Linux versions are built and started by GitHub on every change, but they have had less real use.

## Building from source

In `engine/`, run `cmake --preset release`, then `cmake --build --preset release`. You need CMake 3.28 or newer and a recent C++ compiler (the automatic builds use Apple Clang, GCC 14 and Visual C++ 2022); SDL3 and the other libraries VNext uses are downloaded the first time. On Linux, first install the development packages listed in [the build workflow](.github/workflows/build.yml), which builds VNext for Windows, Linux and macOS.

## Licence

VNext is free to use under the [zlib licence](LICENCE.txt): you may use it, change it and share it, also in games you sell, as long as you don't claim you wrote it.
