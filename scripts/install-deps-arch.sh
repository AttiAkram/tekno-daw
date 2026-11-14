#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build dependencies on Arch Linux (and derivatives).
set -euo pipefail

packages=(
    base-devel cmake ninja git pkgconf
    alsa-lib freetype2 fontconfig
    libx11 libxcomposite libxcursor libxext libxinerama libxrandr libxrender libxi
    mesa
    ccache mold   # optional, but they make rebuilds and links several times faster
)

# JACK headers: keep whichever JACK the system already has (jack2 and pipewire-jack conflict).
if ! pacman -Q jack2 >/dev/null 2>&1 && ! pacman -Q pipewire-jack >/dev/null 2>&1; then
    packages+=(pipewire-jack)
fi

sudo pacman -S --needed "${packages[@]}"
