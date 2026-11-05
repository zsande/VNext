<h1 align="center"><img src=".github/banner.png" alt="VNext" width="800"></h1>

**VNext is a free visual novel engine with the editor built right in, and your characters can even text and call the player.** Write your story as scenes and lines in simple forms, lay out your menus by dragging things around, and translate your game, all in one app. No programming is needed, and everything is also saved as plain, readable script files if you'd rather type.

**[Download VNext](https://github.com/zsande/VNext/releases/latest)** for Windows, macOS or Linux. It's free, also for games you sell.

<p align="center"><img src=".github/phone.gif" width="320" alt="A chat in a VNext game: Kaede types a message to Haru, deletes it, sends “Hey.”, sees it read, and gets “k”, then an invitation to the summer festival"></p>

## The phone

Your characters text the player. Their messages type out as they write them, and they send images, stickers and voice messages, react, read what the player sends, and edit or delete what they just said. A choice in a chat shows its options on the phone, and the one picked goes out as the player's message. Calls ring until they're answered or declined, and when nobody picks up, the missed call has a story of its own. Players can text and call characters themselves, a badge on the textbox counts new messages, and controllers buzz when one comes. The phone has a lock screen with a passcode, wallpapers, apps, a clock, a date, signal and battery, all of which the story can change, and it's made of ordinary screen parts, so every bit of it can be restyled.

## Everything else your story needs

- **Characters** with expressions, eyes that blink and mouths that move as they talk, a face beside the textbox, their own name colour and textbox (speech bubbles too), and made-up "speech" or typing sounds when there's no voice.
- **Words** that can be bold, italic, underlined, crossed out or coloured, speed up or slow down, pause, appear a letter, a word or a line at a time, carry furigana, show a variable, or unlock a tip that explains them, in a textbox or as whole pages of text, novel style.
- **Choices** with a time limit, options locked until something is true or marked once picked, and menus that come back after an option. Variables and conditions keep track of everything, and some of them last between plays.
- **Time**: the story's own clock and calendar, the player's real one, or days split into morning, afternoon, evening and night. Backgrounds and the phone follow it, and a calendar shows each new day.
- **The stage**: backgrounds (images, colours or gradients), characters that come, go and move with animations, effects (greyscale, sepia, invert, blur, pixelate, noise, glitch), weather (rain, snow, petals, leaves, embers), a camera that zooms, pans and shakes, and movies.
- **Sound**: music that fades in, out and from one song to the next and quietens while someone speaks, sounds and loops, and a voice file for every line, with the words keeping time with the voice.
- **Screens**: a title screen, pause menu, saving and loading, settings, a history, a choice menu, questions, the phone, credits, a calendar and a box to type a name into, all ready-made and yours to restyle. Or build your own from parts (buttons, sliders, bars, text boxes, lists, scrolling boxes with scrollbars and fading edges), with styles, drag and drop, and animations made on a timeline.
- **Extras**: a gallery, a music room, endings, achievements, tips, and a flowchart that takes players back to any choice.
- **For players**: saves with pictures, autosave and quick save, rewind, skip, auto-play, a history that replays voices and goes back, and settings for text speed, text size, volumes, high contrast, text read aloud, language and every key, on keyboards, controllers and touch screens.
- **Every language**: translations, with a spreadsheet for translators, right-to-left scripts, Japanese and Chinese line breaking, and the developer tools themselves in English, German, Spanish and Japanese.
- **One file to share**: a finished game packs into game.vnext, which also keeps its players' saves and progress; with VNext next to it, it plays on Windows, macOS and Linux.

## The editor

Open VNext in developer mode and you get an editor that fits any window size, with a tab for each part of the work:

- **Story**: scenes and their lines as forms, every kind of line a button away, and "Play from this line" to see the game from any of them.
- **Map**: every scene as a box; drag one's dot to another and the story jumps there.
- **Screens**: the title screen, menus and textbox, designed by dragging their parts around on a picture of the game.
- **Library**: images, sounds, voices, movies and fonts (drop files on the window to add them), and the colours, styles, animations, variables, endings, achievements, tips and controls a game uses, with the variables' values shown as you play.
- **Settings** and **Translate**: the game's options, and its translations.
- **Log**: what happened, where a click takes you to a line with a mistake. Mistakes are said in plain words, and every change can be undone.

<p align="center">
  <img src=".github/editor-story.png" width="48%" alt="The Story tab: a scene's lines, and the form of the chosen one, showing Haru at the centre of the park">
  <img src=".github/editor-chat.png" width="48%" alt="The Story tab on a chat: its phone lines, and the form of Haru's reaction">
  <img src=".github/editor-screens.png" width="48%" alt="The Screens tab: the phone's parts, the phone with an app button chosen, and the button's settings">
  <img src=".github/editor-map.png" width="48%" alt="The Map tab: eight scenes as boxes, with arrows from start to two endings">
</p>

## How it's made

The idea, the design and the direction of VNext are mine. Every line of its code is written by Claude, an AI made by Anthropic, and so are the characters in the pictures, drawn in code. I decide what VNext should be, test what comes out and say what to change; Claude writes the code and tests it.

## Download

Get VNext from GitHub's [Releases page](https://github.com/zsande/VNext/releases) or [itch.io](https://zsande.itch.io/vnext): a zip for Windows, macOS or Linux, with the player and licences.txt (on Linux, the player is an AppImage). VNext isn't signed by Apple or Microsoft yet, so the first time, macOS asks you to allow it in System Settings → Privacy & Security (Open Anyway), and Windows says it protected your PC (More info, then Run anyway). The [code signing policy](CODE_SIGNING.md) says how the players are signed.

## Status

VNext is made and tested on macOS. The Windows and Linux versions are new: GitHub builds and starts them on every change, but they have had less real use. VNext is in active development and changes quickly.

## Building from source

In `engine/`, run `cmake --preset release`, then `cmake --build --preset release`. You need CMake 3.28 or newer and a recent C++ compiler (the automatic builds use Apple Clang, GCC 14 and Visual C++ 2022); SDL3 and the other libraries VNext uses are downloaded the first time. On Linux, first install the development packages listed in [the build workflow](.github/workflows/build.yml), which builds VNext for Windows, Linux and macOS.

## Licence

VNext is free to use under the [zlib licence](LICENCE.txt): you may use it, change it and share it, also in games you sell, as long as you don't claim you wrote it.
