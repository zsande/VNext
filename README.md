<h1 align="center"><img src=".github/banner.png" alt="VNext" width="800"></h1>

**VNext is a free visual novel engine with the editor built right in, and your characters can even text and call the player.** You write your story as scenes and lines in simple forms, see each moment in a live preview, design your menus by dragging things around, and translate your game, all in the same app. No programming is needed, though everything is also saved as plain, readable script files if you'd rather type.

**[Download VNext](https://github.com/zsande/VNext/releases/latest)** for Windows, macOS or Linux. It's free, also for games you sell.

<p align="center"><img src=".github/phone.gif" width="320" alt="A chat in a VNext game: Kaede types a message to Haru, deletes it, sends “Hey.”, sees it read, and gets “k”, then an invitation to the summer festival"></p>

## The editor

Open VNext in developer mode and you get an editor that fits any window size, with a tab for each part of the work:

- **Story**: scenes and their lines as forms, with a live preview of what players see at each line, and "Play from this line".
- **Map**: every scene as a box, with arrows where the story branches and jumps.
- **Screens**: the title screen, menus and textbox, designed by dragging their parts around, with "Try it" to click through them.
- **Library**: images, sounds, movies and fonts, and the styles, animations (on a timeline) and gallery entries a game uses.
- **Settings** and **Translate**: the game's options, and its translations, with a spreadsheet for translators.

<p align="center">
  <img src=".github/editor-story.png" width="48%" alt="The Story tab: a scene's lines, the chosen line's form and a live preview of Haru in a park">
  <img src=".github/editor-chat.png" width="48%" alt="The Story tab on a chat: the phone line's form, and a preview with the chat on Kaede's phone">
  <img src=".github/editor-screens.png" width="48%" alt="The Screens tab: the phone's parts, a preview with an app button chosen, and the button's settings">
  <img src=".github/editor-map.png" width="48%" alt="The Map tab: eight scenes as boxes, with arrows from start to two endings">
</p>

## The phone

Characters text the player: messages type out, people send images, stickers and voice messages, react, read yours, and edit or delete their own. A choice in a chat shows its options on the phone. Calls ring and can be missed, and players can text and call characters themselves. The ready-made phone, with its chats, calls and apps, is made of ordinary screen parts, so every bit of it can be restyled.

## And more

Games made with VNext can have voices and made-up "speech" sounds, movies, timed and locked choices, a story flowchart, a gallery, music room and achievements, saving, loading and rewinding, controller and touch support, text read aloud, high-contrast and text-size options, and right-to-left languages. A finished game packs into one file, game.vnext, which also keeps its players' saves and progress: with VNext next to it, it plays on Windows, macOS and Linux.

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
