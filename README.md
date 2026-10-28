<h1 align="center"><img src=".github/banner.png" alt="VNext" width="800"></h1>

**VNext is a visual novel engine with the editor built right in.** You write your story as scenes and lines in simple forms, see each moment in a live preview, design your menus by dragging things around, and translate your game, all in the same app. No programming is needed, though everything is also saved as plain, readable script files if you'd rather type.

## What's in it

Open VNext in developer mode and you get an editor with a tab for each part of the work:

- **Story**: scenes and their lines as forms, with a live preview of what players see at each line, and "Play from this line".
- **Map**: every scene as a box, with arrows where the story branches and jumps.
- **Screens**: the title screen, menus and textbox, designed by dragging their parts around, with "Try it" to click through them.
- **Library**: images, audio, videos and fonts, and the styles, animations and gallery entries a game uses.
- **Settings** and **Translate**: the game's options, and its translations, with a spreadsheet for translators.

Games made with it can have voices and made-up "speech" sounds, videos, timed and locked choices, phone-style messages, a story flowchart, a gallery, music room and achievements, saving, loading and rewinding, controller and touch support, text read aloud, high-contrast and text-size options, and right-to-left languages. A finished game packs into one file, game.vnext, which also keeps its players' saves: with VNext next to it, it plays on Windows, macOS and Linux.

## How it's made

The idea, the design and the direction of VNext are mine, and every line of its code is written by Claude, an AI made by Anthropic. I decide what VNext should be, test what comes out and say what to change; Claude writes the code and tests it.

## Download

Get VNext from the [Releases page](https://github.com/zsande/VNext/releases): a zip for Windows, macOS or Linux, with the player and licenses.txt. VNext isn't signed by Apple or Microsoft yet, so the first time, macOS asks you to allow it in System Settings → Privacy & Security (Open Anyway), and Windows says it protected your PC (More info, then Run anyway).

## Status

VNext is made and tested on macOS. The Windows and Linux versions are new: GitHub builds and starts them on every change, but they have had less real use. VNext is in active development and changes quickly.

## Building from source

In `engine/`, run `cmake --preset release`, then `cmake --build --preset release`. You need CMake 3.28 or newer and a recent C++ compiler (the automatic builds use Apple Clang, GCC 14 and Visual C++ 2022); SDL3 and the other libraries VNext uses are downloaded the first time. On Linux, first install the development packages listed in [the build workflow](.github/workflows/build.yml), which builds VNext for Windows, Linux and macOS.

## License

VNext is free to use under the [zlib license](LICENSE.txt): you may use it, change it and share it, also in games you sell, as long as you don't claim you wrote it.

## Supporting VNext

If you'd like to support the project, you can become a member on [Patreon](https://www.patreon.com/vnext). It's entirely optional: following along is free, and nobody is expected to pay.
