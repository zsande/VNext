# Accessibility

I want anyone to be able to make and play visual novels with VNext: without a mouse, without sound, without motion, and with text big and clear enough to read. This page says what VNext does for that today, what it doesn't do yet, and how to tell me when something gets in your way. VNext hasn't been checked against a standard such as WCAG: this is what it does as I've built and tested it.

## Playing a game made with VNext

- **A mouse, a keyboard, a controller or a touch screen.** Every menu works with each of them. A ring shows what the keys are on, and an on-screen keyboard comes up for typing a name with a controller. Every key and controller button can be changed in Settings.
- **Text you can read.** Text size (50% to 200%), text speed (or all at once), the auto-play delay and the textbox's opacity are in Settings. High contrast draws the menus and the textbox in white on black, with the button you're on in yellow.
- **Read aloud.** Your computer's own voice reads each line (who says it, then what), a choice's options, and the button you're on in a menu.
- **No need to hear it.** Every line is written on screen, also when it's voiced, and the history shows recent lines again and replays their voices. Music, sounds, voices, made-up voices, typing sounds and controller vibration each have their own setting.
- **No need to hurry.** Skip, auto-play, rewind and the history are a button or a key away, and saves and quick saves keep your place.
- **Less motion.** With Animations off, characters, backgrounds, the camera and the menus stand still.
- **Languages.** Translations, right-to-left scripts, and Japanese and Chinese line breaking.

These come with the ready-made Settings screen. A game's maker can restyle it or leave settings out, so games can differ.

## Making a game

- The editor follows your computer's display scaling (Windows at 150%, for example) and fits any window size.
- Mistakes are marked in red and also said in words.
- Keys do the common things: Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select all, the three panels (Cmd or Ctrl and 1 to 3), Play (F10) and Stop here (F9).
- A game is plain text script files, so you can also write it in any text editor, with the tools you already use.

If you make games, a few choices help your players: keep the Settings screen, let your own text styles follow its text size (Follows Settings: Text size, in a style's form), give choices with a time limit plenty of time (or none), and don't let colour alone carry meaning.

## Known limits

- **Screen readers** (VoiceOver, Narrator, NVDA, Orca) can't read VNext's windows, because VNext draws them itself. In games, Read aloud covers the lines, choices and buttons. The editor has nothing like it yet, but its script files can be written in a text editor.
- **Read aloud** doesn't read phone messages yet, or a menu's text other than its buttons. On Linux it needs speech-dispatcher.
- **Animations off** doesn't stop weather or the glitch and noise effects yet, and those can flicker.
- **Time limits:** players can't turn off a choice's time limit or a call's ringing time. The game decides them.
- **The editor needs a mouse** for most of its work: Tab doesn't move between boxes yet.
- **Captions:** sounds and music have none, and movies have no subtitles unless the game adds them as text.

## Where it runs

Windows, macOS 13.3 or newer (Apple chip or Intel) and Linux (as new as Ubuntu 22.04 or newer), with a mouse, a keyboard, a controller or a touch screen. VNext is made and tested on macOS; the Windows and Linux versions have had less real use.

## Telling me about a barrier

Open an issue with the [accessibility form](https://github.com/zsande/VNext/issues/new?template=accessibility.yml), or email zsande.vnext@gmail.com. Say what you were doing, what got in the way and what you use (a screen reader, a controller, a setting...). You don't need to say why you need it. I'll answer, offer a way around it when there is one, and what keeps you from playing or making a game at all comes first.
