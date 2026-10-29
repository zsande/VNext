# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

## What is signed

The Windows player, `VNext.exe` in `VNext-Windows.zip` on the [Releases page](https://github.com/zsande/VNext/releases). GitHub Actions builds it from this repository's source for each release, with the [build workflow](.github/workflows/build.yml), and each signing is approved by hand on SignPath. The libraries built into the player are compiled from their own source in the same build; [licenses.txt](engine/licenses.txt) lists them.

The macOS and Linux players aren't signed yet.

## Team

- Committers and reviewers: [zsande](https://github.com/zsande)
- Approvers: [zsande](https://github.com/zsande)

VNext's code is written by Claude, an AI made by Anthropic, as the [README](README.md#how-its-made) says; zsande reviews, tests and commits every change.

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it. Neither do the libraries built into it. Games made with VNext keep their saves and settings on the player's computer.
