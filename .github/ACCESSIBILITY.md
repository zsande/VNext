# Accessibility

I want anyone to be able to make and play visual novels with VNext: without a mouse, without sight or sound, without motion, without hurrying, and with text big and clear enough to read. This page says what VNext does for that today, what it doesn't do yet, and how to tell me when something gets in your way. VNext hasn't been checked against a standard such as WCAG: this is what it does as I've built and tested it.

## Playing a game made with VNext

- **A mouse, a keyboard, a controller or a touch screen.** Every menu works with each of them. A ring shows what the keys are on, and an on-screen keyboard comes up for typing a name with a controller or a click (a built-in screen: a game can make it its own, and give it letters with accents). Every key and controller button can be changed in Settings.
- **Text you can read.** Text size (75% to 150%), text speed (or all at once), the auto-play delay and the textbox's opacity are in Settings. High contrast draws the menus and the textbox in white on black, with the button you're on in yellow.
- **Read aloud.** Your computer's own voice reads what isn't spoken: each line without a voice of its own (who says it, then what; it takes the place of made-up speech), a choice's options, new phone messages, and on a menu its title or question as it opens, then what the keys are on with its label and value ("Text speed, 45", "Next line, Space"), the on-screen keyboard's keys too. A voiced line plays its voice, unless voices are turned down to nothing.
- **Screen readers.** With VoiceOver, Narrator, NVDA, JAWS or Orca running, what Read aloud says goes through your screen reader instead (in its voice, and on a braille display where it has one), and Read aloud starts on for a new player who has one.
- **No need to hear it.** Every line is written on screen, also when it's voiced, and the history shows recent lines again and replays their voices. Captions say what a sound or song is as it begins, and movies show their subtitles. Music, sounds, voices, made-up voices, typing sounds and controller vibration each have their own setting.
- **No need to hurry.** With Time limits off, a choice waits for you however long it has, and a call rings until you answer or decline it. Skip, auto-play, rewind and the history are a button or a key away, and saves and quick saves keep your place.
- **Less motion.** With Animations off, characters, backgrounds, the camera, the menus, the weather and the glitch and noise effects stand still, and what would fade in or out is there or gone at once. (Eyes still blink, and Mouths move has a setting of its own.)
- **Languages.** Translations, right-to-left scripts, and Japanese and Chinese line breaking.

These come with the ready-made Settings screen, most of them in its Accessibility group (Text size, High contrast, Read aloud, Captions, Animations, Time limits and Vibration). A game's maker can restyle it or leave settings out, so games can differ.

## Making a game

- **The keys.** Tab and Shift+Tab move through a page's lists, boxes and buttons (a ring shows where), Enter or Space presses a button or starts typing in a box, up and down move in a list, and a box's list opens with Enter (or down, while typing in it), the arrows move in it and Enter picks. Tab from a box of words being typed in goes on to the buttons under it (Bold, Colour, Pause...), and one pressed goes back to typing. The Edit, Game and View menus at the window's foot open with Enter too: up and down move in one, left and right go to the menu beside it, Enter picks and Esc shuts it. Keys do the common things too: Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select all, the three pages (Cmd or Ctrl and 1 to 3), Play (F10) and Stop here (F9). Enter renames what's chosen in a list and Delete deletes it, Ctrl+Tab and Ctrl+Shift+Tab go to the next and the last page, and while page parts are moved and resized (View, Move and resize page parts), Tab goes to each page part and its right and bottom sides, and the arrows move them. Every one of these keys is a control in the Library's Controls (tools_next, tools_press...), to change.
- **A controller.** The shoulder buttons move as Tab and Shift+Tab do, the d-pad (or the left stick) as the arrows, A presses, B goes back (a list or a box let go of), the triggers go from page to page, the right stick scrolls, and the home button goes between the editor and the game. A on a box brings up an on-screen keyboard (X deletes, Y is a space, Start is Done), and messages, questions and the file chooser for Import come in the editor's own windows, which a controller can answer. Its buttons are in the same controls.
- **Only a mouse.** Every button, list and box takes clicks, and a click into a box brings up the on-screen keyboard (a real keyboard's key puts it away, and the next click brings it back).
- **Screen readers** are told the page and what the keys are on (a box's label and value, a list's chosen item, the item they're on in a box's list); the editor's messages come in the system's own windows, which they read.
- The editor follows your computer's display scaling (Windows at 150%, for example), fits any window size, and scrolls a page part that's too small for what's in it.
- Mistakes are marked in red and also said in words.
- A game is plain text script files, so you can also write it in any text editor, with the tools you already use.

If you make games, a few choices help your players: keep the Settings screen, let your own text styles follow its text size (Follows Settings: Text size, in a style's form), give your sounds and songs a caption (their Caption box, in their form in the Library or under a Sound line) and your movies a subtitle file (movies/NAME.srt), give choices with a time limit plenty of time (or none), and don't let colour alone carry meaning. The captions and the on-screen keyboard are built-in screens, like the textbox: every game has them, they come up by themselves when they're needed, a script can open them too, and they can be made the game's own to restyle (the keyboard's keys too).

## Known limits

- **Screen readers** can't explore VNext's windows on their own, as they would a web page, because VNext draws them itself: they're told what's new and what the keys are on.
- **Read aloud** says the words on screen, so in a game it doesn't say which of a setting's buttons is the chosen one (On or Off): pressing one sets it, and a slider says its value.
- **Dragging** in the editor (on the map, a screen's preview, an atlas's eyes and mouth, an animation's timeline, the part of an image cut out) needs the mouse, but what each does can be done in lists and forms too: Jump lines, the arrows that move a screen's parts, and each item's As text.
- **The on-screen keyboards** have a US keyboard's letters and signs (a game's, until the game adds others to its own): letters with accents and other scripts need a real keyboard, or the computer's own on-screen one.
- **Captions** show for the sounds and songs that have one, and subtitles for the movies that have a subtitle file.
- **On Windows and Linux** the screen reader support is new, and has had less use than on macOS.

## Where it runs

Windows, macOS 13.3 or newer (Apple chip or Intel) and Linux (as new as Ubuntu 22.04 or newer), with a mouse, a keyboard, a controller or a touch screen. VNext is made and tested on macOS; the Windows and Linux versions have had less real use.

## Telling me about a barrier

Open an issue with the [accessibility form](https://github.com/zsande/VNext/issues/new?template=accessibility.yml), or email zsande.vnext@gmail.com. Say what you were doing, what got in the way and what you use (a screen reader, a controller, a setting...). You don't need to say why you need it. I'll answer, offer a way around it when there is one, and what keeps you from playing or making a game at all comes first.
