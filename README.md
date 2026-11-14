# tekno-daw

An open source, pattern-based DAW for **acid / mental tekno** — built in the spirit of
FL Studio's workflow, with a 303-style acid synth wired in from day one.

> **Status: pre-alpha.** Milestone M0 is done: the build, test and CI foundations, the visual theme,
> and a *non-functional* preview of the main screen (`src/ui/preview`). Nothing makes sound yet:
> that is milestone M1 (see the roadmap below).

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
| ▶ M1 | MVP: an acid synth playing a 16-step pattern |
| M2 | Real sequencer: patterns, accent / slide / tie, swing, project files |
| M3 | Channel rack, mixer, piano roll, drum sampler |
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

In the preview: **T** switches light/dark theme, **Space** runs a fake playhead, click a step to
toggle it, **Shift+click** adds an accent, **Ctrl+click** a slide.

### Presets

| Preset | What for |
|---|---|
| `dev` | everyday work: Debug, JUCE assertions, unit tests |
| `release` | optimised build with LTO |
| `asan` | AddressSanitizer + UBSan, including the window tests |
| `clang` | `dev` with Clang |
| `ci` | what CI runs: warnings are errors, window smoke test and UI snapshots |

`cmake --workflow --preset <name>` runs configure, build and tests; `ctest --preset <name>` only tests.
`ccache` and a fast linker (`mold`, else `lld`) are picked up automatically when installed.

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
tekno-daw --smoke-test         open the window and quit after a second
tekno-daw --version
```

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
cmake/                dependencies (pinned + hashed), compiler options, JUCE setup
scripts/              system dependency installers (one list per distro)
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
