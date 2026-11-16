# tekno-daw

An open source, pattern-based DAW for **acid / mental tekno** — built in the spirit of
FL Studio's workflow, with a 303-style acid synth wired in from day one.

> **Status: pre-alpha.** A channel rack of up to 16 channels — 303-style acid synths with an
> antialiased drive, and a drum sampler with a built-in kit — through a mixer with volume, pan, mute,
> solo and insert effects (distortion, ping-pong delay, plate reverb). 16 patterns of 1 to 64 steps with accent, slide and tie, swing, pattern changes on the bar,
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
| ✅ M3 | Channel rack, drum sampler, mixer, piano roll, keymaps per context, tiling panels |
| ▶ M4 | Native effects, live play from the PC keyboard and MIDI, sub bass, effect graphs, waveshaper, demo (✅); macros, VST3 hosting |
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

The window opens on a **demo**: an original freetekno / hardtek loop set at 162 BPM in G# minor. Press PLAY,
then switch between its patterns while it plays: 1 intro, 2 groove, 3 acid, 4 break (a new pattern starts at the next
bar). NEW starts from a plain loop instead; `project.demo` in the terminal brings the demo back. The demo
is also a normal project file, [`examples/demo.tekno`](examples/demo.tekno).

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
tekno-daw --view=mixer         start on the mixer (or pattern, pianoroll, keyboard, effects, keys)
tekno-daw --size=1024x640      open the window at this size (the smallest it allows)
tekno-daw --no-audio           start without opening an audio device
tekno-daw --run="bpm 138; swing 62"   open the terminal and run commands at start
tekno-daw --smoke-test         open the window and quit after a second
tekno-daw --version
```

### Instruments

"+ channel", under the last channel of the step sequencer, adds one:

- **303 acid** — the bass line synth: cutoff, resonance, envelope, decay, accent, drive, a saw-to-square
  shape and the glide of the slides.
- **Sub bass** — a sine with a punch (it starts high and falls into the note, like a kick), drive that adds
  the harmonics small speakers need, decay (a pluck at 0, held at 1) and glide. Same slides and ties as
  the acid. New steps start on A1.
- **Drums** — built-in sounds made by code: kick, hat, open hat, clap, snare, tom, conga, rim, shaker, or a
  sound from a file. Toms and congas play C4 as they are; other notes tune them, so one tom channel is a
  whole set of toms.

Rest the mouse on a knob to see what it does; double-click it to go back to its default. From the
terminal: `channel.add sub`, `sub.drive 0.6`, `channel.add sampler name=toms sample=builtin:tom`.

### Effects

Each channel, and the master, has four insert slots, before the fader. Open the effects panel from the
top bar, with Alt+4 or with F8:
"+ add" puts a distortion, a waveshaper, a ping-pong delay or a plate reverb in a slot, and each card has
a graph of what the effect does, its knobs, on/off, move left/right and remove. A light on each card shows
when it is making sound, and turns red when a distortion clips hard. The mixer shows each strip's effects
by name.

- **Distortion** — hard clip, foldback or bitcrush, with drive, tone, bits, rate, mix and output. Clip and
  fold are antialiased (ADAA), crush aliases on purpose. Made for distorted kicks and sub-basses. Its
  graph is the curve after the drive, with a grey band for how far your sound reaches into it.
- **Waveshaper** — a curve you draw: drag its nine points up and down on the graph, or start from soft,
  hard, fold, tube, rectify or clean. Antialiased exactly (ADAA on the straight segments), with drive,
  mix and output.
- **Ping-pong delay** — synced to the tempo in steps (3 = a dotted eighth), feedback up to 1.1 for dub
  runaways (it stays bounded), a tone and a low cut in the feedback, ping-pong amount. Its graph shows the
  echoes over two bars, left above and right below.
- **Plate reverb** — Jon Dattorro's plate (JAES 1997): decay, damping, predelay, low cut, mix. Its graph
  shows the tail, and dashed, how much sooner the highs die.

From the terminal: `fx.add delay`, `fx.set 1 feedback 0.8`, `fx.set 2 mode fold`, `fx.curve 1 tube`, `fx.bypass 1 on`,
`fx.move 2 1`, `fx.remove 1`, `fx.list`, with `channel=N` or `master=on`. A project can have up to 8
delays and 8 reverbs. The acid synth also has a **shape** knob (saw to square, morphing) and a **glide**
knob (how long a slide takes). New projects start at 170 BPM.

### Play live

The keyboard panel (KEYBOARD in the top bar, Alt+3 or F5) plays the selected channel from the PC keyboard,
laid out as in trackers:
Z S X D C V G B H N J M for one octave (white keys on the bottom row, black keys on the row above), then
Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P for the next. Shift plays an accent, on any keyboard layout.

- **Acid channels** are mono, like a 303 played by hand: a key pressed while another is held slides to it,
  and letting go slides back to the key still down.
- **Drum channels** play one whole hit per key.
- **+** and **-** (or Left / Right) change the octave, Up / Down the velocity. Tab switches the gate between
  `hold` (as long as the key) and `short` (one 303 step, whatever the key does), and Esc stops everything.
  Clicking or dragging on the on-screen piano plays too.
- While a key is held, the pattern doesn't play over it on that channel.

MIDI keyboards are opened as soon as they are plugged in. Other programs, such as a virtual keyboard like
vmpk or a sequencer, send notes to the `tekno-daw` MIDI port. Every MIDI channel plays the selected
channel, and a velocity of 100 or more is an accent.

Sound goes out through JACK when a JACK server answers (PipeWire with `pipewire-jack` is one), otherwise
through ALSA; `audio.driver jack|alsa` chooses. The panel shows the latency as it is measured: how long
the last note waited for the audio, plus the buffer, plus what the device adds. It also counts the
blocks the device dropped. `audio.buffer 128` halves the default of 256 (about 8 ms instead of 13 on a
laptop with PipeWire); if the panel counts dropouts, go back up.

From the terminal: `live.octave 3`, `live.velocity 110`, `live.gate short`, `live.note A2`,
`live.release A2`, `live.off`, `midi.inputs`.

### Panels side by side

The step sequencer, the piano roll, the keyboard, the effects and the mixer are in the top bar, and
Alt+1 to Alt+5 open them in that order (on many laptops the F keys need Fn; F5 to F10 work too). They are
panels, tiled like a tiling window manager: Alt+F5 to Alt+F10 open one beside the active panel (splitting it
along its longer side), Alt+Q closes the active one, Alt+Left / Alt+Right move between them, and the gaps
between panels can be dragged. Opening one from the top bar swaps the active panel for it. The active panel has a
dark frame, and it decides which keys apply. The layout is remembered. From the terminal: `ui.tile mixer`,
`ui.tile.close`, `ui.tile.focus next`, `ui.tile.single`.

### Your own keys

Every key runs a command line, the same language as the terminal, so a key can do anything you can
type: `cutoff 0.8`, `ch 3`, `key E phrygian`. Keys belong to a context: global, a view (step
sequencer, piano roll, mixer) or the kind of the selected channel (acid or drums). The most specific
wins, so `1` can set the cutoff on an acid channel and the tuning on a drum channel. Edit them in the
keys view (F10, or EDIT next to "Keys"): pick a context, press "press a key", write what it should do,
add. The same key twice in one context is the only conflict, and you are told before replacing. From the
terminal: `keys.bind acid 1 cutoff 0.2`, `keys.unbind acid 1`, `keys.list`, `keys.reset`. They are saved
in `~/.config/tekno-daw/keymap.json`, which is plain JSON you can edit by hand.

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
lane.repeat channel=1   copy the kick's first bar to the end of the pattern (bars=2 for two)
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
which is which, with its name and value. Ctrl+wheel zooms the rows, Ctrl+Shift+wheel zooms time,
Shift+wheel scrolls along it. Ctrl+C / Ctrl+V copy and paste the selection (pasting right after the copy,
in any channel or pattern), Ctrl+D duplicates it. Right-click a channel's name in the step sequencer to
repeat its first bar to the end of the pattern, move its notes onto the scale, clear it or rename it.

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
