# tekno-daw

An open source, pattern-based DAW for **acid / mental tekno** — built in the spirit of
FL Studio's workflow, with a 303-style acid synth wired in from day one.

> **Status: pre-alpha.** A channel rack of up to 16 channels — 303-style acid synths with an
> antialiased drive, and a drum sampler with a built-in kit — through a mixer with volume, pan, mute
> and solo. 16 patterns of 1 to 64 steps with accent, slide and tie, swing, pattern changes on the bar,
> undo of every edit, a built-in command terminal, and projects saved as readable `.tekno` files.

## Goals

- **Pattern sequencer** — patterns are the unit of work, arrangement comes second.
- **Piano roll** — note editing with the per-step data acid lines need: accent, slide, tie.
- **Built-in acid synth** — a TB-303-style monosynth (resonant lowpass, env mod, decay,
  accent, slide, drive), not an afterthought plugin.
- **VST3 hosting** — load your own instruments and effects, isolated so they can't crash the DAW.
- **Audio export** — offline render to WAV.

Further out: remote control from a phone or browser, AI assistants connected to your project,
your own instruments, per-instrument keyboard maps, tiling panels, a built-in command terminal,
and project version history.

## Roadmap

| | Milestone |
|---|---|
| ✅ M0 | Foundations: build, tests, CI, visual theme, style preview |
| ✅ M1 | MVP: an acid synth playing a 16-step pattern |
| ✅ M2 | Real sequencer: patterns, accent / slide / tie, swing, project files, drive, terminal |
| ▶ M3 | Channel rack, drum sampler, mixer, piano roll (✅ all four), keymaps, tiling panels |
| M4 | VST3 hosting (out of process) and MIDI input |
| M5 | Audio export, full project format, more import formats |
| M6 | Arrangement and automation |
| M7 | Polish and packaging (AppImage, Flatpak, AUR) |

## Quick start (Linux)

```sh
scripts/install-deps-arch.sh        # or scripts/install-deps-ubuntu.sh
cmake --workflow --preset dev       # configure + build + test, one command
cmake --build --preset dev --target run
```

The first configure downloads JUCE and Catch2 once (pinned version + SHA-256) into `.cache/deps/`;
after that everything works offline. A full build takes about a minute on a 12-core machine.

### Start it with one click

```sh
scripts/install-desktop-entry.sh     # adds "tekno-daw" to the application menu
```

Then open tekno-daw from the application menu like any other app (on GNOME: right click → *Pin to
Dash*). Each click runs `scripts/tekno-daw.sh`, which rebuilds only what changed since the last start
and opens the app; progress and build errors show up as desktop notifications, and the full output
goes to `~/.cache/tekno-daw/launch.log`. From a terminal the same script works directly:
`scripts/tekno-daw.sh --theme=light`. Use `TEKNO_PRESET=release` for the optimised build, and
`scripts/install-desktop-entry.sh --uninstall` to remove the launcher.

| Key / mouse | Action |
|---|---|
| Space | play / stop |
| Click a step | on / off (on that row's channel) |
| Click a channel's name, or Up / Down | select the channel: the knobs on the left follow it |
| M / S next to a channel | mute / solo |
| Double-click a channel's name | rename it |
| Wheel beside the steps | scroll the rack when the channels don't fit |
| F6 / F7 / F9 (or the top bar) | step sequencer / piano roll / mixer |
| [ / ], or click "bar 1 of 4" | show the next 16 steps of a longer pattern |
| Shift / Ctrl / Alt + click | accent / slide / tie |
| Wheel on a step | pitch (Shift: octave) |
| Drag or wheel on the BPM pill | tempo (Shift: fine, double-click: 130) |
| Drag the steps pill | pattern length, 1 to 64 |
| Drag the swing pill | swing, 50% straight to 75% |
| PgUp / PgDn, or click 1-16 | select a pattern (while playing it starts on the next bar) |
| Double-click the title | rename the pattern |
| Ctrl+Z / Ctrl+Y | undo / redo |
| Ctrl+S / Ctrl+Shift+S / Ctrl+O / Ctrl+N | save / save as / open / new |
| T | light / dark theme |
| ` (backquote) | open / close the terminal |

### Presets

| Preset | What for |
|---|---|
| `dev` | everyday work: Debug, JUCE assertions, unit tests |
| `release` | optimised build with LTO |
| `asan` | AddressSanitizer + UBSan, including the window tests |
| `clang` | `dev` with Clang |
| `ci` | what CI runs: warnings are errors, window smoke test and UI snapshots |

`cmake --workflow --preset <name>` runs configure, build and tests; `ctest --preset <name>` only tests.
`ccache` and a fast linker (`mold`, else `lld`) are picked up automatically when installed. Parallel
compile jobs are capped by RAM (half of it by default) so a build never pushes other programs out of
memory; raise it with `-DTEKNO_BUILD_MEMORY_MB=<MB>`.

### Clean-room build in Docker

Docker is for **building and testing** on a clean machine, not for running the DAW (audio and display
don't belong in a container):

```sh
docker build -f docker/Dockerfile --target check .
```

This installs only what `scripts/install-deps-ubuntu.sh` lists, then runs the `ci` workflow under Xvfb.

### Command line

```
tekno-daw --theme=dark|light   start with this theme
tekno-daw --snapshot=out.png   render the main window to a PNG and exit
tekno-daw song.tekno           open a project
tekno-daw song.tekno --render=out.wav --bars=8   render its selected pattern to a WAV, no sound card needed
tekno-daw --view=mixer         start on the mixer (or pattern, pianoroll)
tekno-daw --no-audio           start without opening an audio device
tekno-daw --run="bpm 138; swing 62"   open the terminal and run commands at start
tekno-daw --smoke-test         open the window and quit after a second
tekno-daw --version
```

## Terminal

Press `` ` `` to open it. Every action of the DAW is a named command, and the terminal runs them:

```
bpm 138                 transport.bpm 138, by its short name
note 3 C#3              set step 3 to C#3
accent 5 on             accent on step 5
rename acid night       rename the pattern
drive 0.6               push the 303 into distortion
ch 1                    select channel 1
toggle 5 channel=2      edit another channel without selecting it
channel.add sampler name=snare
sample builtin:clap     load a built-in sound, or a WAV / AIFF / FLAC / Ogg file
note.add 344 E3 16      a note at tick 344 (24 per step), a 1/16 triplet long
key La minore           the scale guide: any root, major/minor/phrygian/dorian/harmonic/pentatonic
hear La3                play a note on the selected channel, just to hear it
tune -2                 tune the selected sampler down two semitones
vol -6 channel=1        mixer: volume in dB, also pan, mute, solo, mixer.master
help bpm                what a command takes; help alone lists them all
```

The piano roll (F7) shows the selected channel's notes, with the other channels' notes in grey behind
them. Click to add a note, drag to move it, drag its right edge to change its length, right-click to
delete it, Shift+click for accent, Ctrl+click for slide. Hold and drag on empty space to select several
notes (Shift adds to the selection), drag one of them to move them all, Delete removes them, Ctrl+A
selects everything. The lane at the bottom shows each note's **velocity** (0-127, 100 = as it is):
drag a bar up or down, and with several notes selected it sets them all. Hover a bar or a note to see
which is which, with its name and value. Ctrl+wheel zooms the rows.

**Scale guide.** Pick the project's key with the key button in the piano roll (or `key La minore`,
`key E phrygian`, `key off`): notes outside the scale get darker rows and a warning dot, the root is
marked, and new or moved notes land on the scale (`ui.scalelock off` to place anything). A new project
starts in A minor, the key of its line. Notes play as you place or move them, and clicking a key on the
keyboard plays it (`ui.preview off` for silence). Notes snap to the step, to 1/32, to 1/16
triplets, or nowhere (`ui.snap step|1/32|1/3|free`). The step sequencer and the piano roll edit the same
notes; a row whose notes are off the grid shows them in miniature, and a click opens the piano roll.

**Help for people who don't read letter names.** `ui.notes latin` names notes Do Re Mi everywhere
(`ui.notes letters` goes back to C D E); the terminal understands both (`note 3 La2`). Octaves are the
same in both: C4 = Do4, middle C. **Import a melody** by dragging a MIDI file (`.mid`) onto the window: on
a channel's row it goes into that channel, anywhere else into the selected one, and the piano roll opens.
The same from the terminal: `midi.import song.mid track=2`. Timing is kept as it is, up to 4 bars.

Your settings (note names, snap, theme) are kept in `~/.config/tekno-daw/settings.xml`.

The mixer view (F9) has a strip per channel — fader, pan, mute, solo, a level meter with a peak line and
a clip light (click it to reset) — and the master. A whole fader drag is one undo step.

Commands act on the selected pattern and channel; `pattern=` and `channel=` pick another one. The
built-in kit (`kick`, `hat`, `openhat`, `clap`) is synthesised by code, so a new project plays a loop
straight away without shipping anyone's samples.

Tab completes, Up/Down walk the history, and `tekno-daw --run="bpm 138; drive 0.5"` runs commands at
start.

## Project files

Projects are `.tekno` files: plain JSON, written in a fixed order with one note per line and pitches
by name, so they are easy to read, edit by hand and compare between versions. Time is in ticks, 24 per
step (96 per beat), so 1/16, 1/32 and triplets are all whole numbers:

```json
{ "at": 0, "length": 24, "note": "A2", "accent": true, "slide": false },
```

The step sequencer is a view of these notes: a step is a note one step long on the grid, and ties are
longer notes.

Sample files inside the project's folder are stored relative to it, so the folder can be moved whole.
A missing sample doesn't stop a project from opening: that channel stays silent and keeps its
reference, and you are told which files are missing. Projects from older versions open and are
upgraded when saved.

## Stack

| | |
|---|---|
| Language | C++20 |
| Framework | [JUCE](https://juce.com) 9.0.5 (audio I/O, DSP, GUI, VST3 hosting) |
| Build | CMake ≥ 3.25 with presets, Ninja |
| Tests | Catch2 3 + CTest |
| Platform | Linux (ALSA / JACK / PipeWire). Runs on Wayland through XWayland. |
| License | AGPL-3.0-or-later |

## Repository layout

```
src/
  core/               engine and model — must never include GUI code (checked by a test)
  ui/                 theme (design tokens, fonts, LookAndFeel), components, views
  app/                the executable: main, window, command-line options
tests/                unit tests, layering check, window smoke tests
assets/fonts/         Inter and Barlow Condensed (SIL OFL 1.1), embedded in the binary
assets/icons/         application icon
cmake/                dependencies (pinned + hashed), compiler options, JUCE setup
scripts/              dependency installers (one list per distro), launcher and menu entry
docker/               clean-room build image
```

## Contributing

1. Open an issue to discuss bigger changes before writing them.
2. Keep the layering: `core` never includes `ui` or `app` (a test enforces it).
3. Format with the repository's `.clang-format`; CI builds with warnings as errors.

## License

Copyright (C) 2026 tekno-daw contributors.

tekno-daw is free software: you can redistribute it and/or modify it under the terms of the GNU
Affero General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY. See
[LICENSE](LICENSE).

It is built on JUCE, used under the AGPLv3. Bundled fonts are under the SIL Open Font License 1.1
(see `assets/fonts/*/OFL.txt`).
