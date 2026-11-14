#!/usr/bin/env bash
# tekno-daw - a pattern-based DAW for acid and mental tekno
# Copyright (C) 2026 tekno-daw contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Start tekno-daw with one command: rebuild what changed, then open the app.
#
#   scripts/tekno-daw.sh [app options...]     e.g. scripts/tekno-daw.sh --theme=light
#   TEKNO_PRESET=release scripts/tekno-daw.sh  use the optimised build (first build is slower)
#
# Works from a terminal and from the desktop launcher (scripts/install-desktop-entry.sh).
# Without a terminal, progress and errors are shown as desktop notifications and the build
# output goes to ~/.cache/tekno-daw/launch.log.
set -euo pipefail

root="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
preset="${TEKNO_PRESET:-dev}"
log_dir="${XDG_CACHE_HOME:-$HOME/.cache}/tekno-daw"
log="$log_dir/launch.log"
mkdir -p "$log_dir"

interactive=false
[[ -t 1 ]] && interactive=true

notify() {
    if ! $interactive && command -v notify-send >/dev/null; then
        notify-send --app-name=tekno-daw --icon=tekno-daw "tekno-daw" "$1" || true
    fi
    echo "tekno-daw: $1"
}

# Run a build step. In a terminal the output is shown live; otherwise it goes to the log.
run_step() {
    if $interactive; then
        "$@" 2>&1 | tee -a "$log"
        return "${PIPESTATUS[0]}"
    fi
    "$@" >>"$log" 2>&1
}

cd "$root"
: >"$log"

if [[ ! -f "build/$preset/build.ninja" ]]; then
    notify "First start: configuring the '$preset' build (needs internet once for the dependencies)..."
    if ! run_step cmake --preset "$preset"; then
        notify "Configure failed. Details: $log"
        exit 1
    fi
fi

# Always build: when nothing changed this takes a fraction of a second. Only the app target, since
# tests are not needed to make music; low CPU priority, and jobs are already capped by RAM, so the
# rest of the desktop stays responsive. Say "building" only if it takes longer than a moment.
( sleep 2 && notify "Building the latest changes..." ) &
announcer=$!
if ! run_step nice -n 10 cmake --build --preset "$preset" --target tekno-daw; then
    kill "$announcer" 2>/dev/null || true
    notify "Build failed, the app was not started. Details: $log"
    exit 1
fi
kill "$announcer" 2>/dev/null || true

app="$(find "build/$preset/src/app/tekno-daw_artefacts" -type f -name tekno-daw -perm -u+x 2>/dev/null | head -n 1)"
if [[ -z "$app" ]]; then
    notify "Built, but the executable was not found under build/$preset."
    exit 1
fi

# From the desktop there is nobody to read the app's messages, so they join the build output.
if $interactive; then
    exec "$app" "$@"
fi
exec "$app" "$@" >>"$log" 2>&1
