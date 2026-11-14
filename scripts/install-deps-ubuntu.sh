#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build dependencies on Ubuntu 24.04 / Debian. The single list used by the Dockerfile and by CI.
# Source of the list: JUCE's docs/Linux Dependencies.md, minus what tekno-daw disables
# (curl, webkit, ladspa - see cmake/TeknoJuce.cmake), plus xvfb for the window tests.
set -euo pipefail

SUDO=""
[[ $EUID -ne 0 ]] && SUDO="sudo"

$SUDO apt-get update
$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git ca-certificates pkg-config ccache \
    libasound2-dev libjack-jackd2-dev \
    libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxrender-dev libxi-dev \
    libegl-dev libglu1-mesa-dev mesa-common-dev \
    xvfb xauth
