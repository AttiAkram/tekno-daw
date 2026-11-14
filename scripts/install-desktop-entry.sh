#!/usr/bin/env bash
# tekno-daw - a pattern-based DAW for acid and mental tekno
# Copyright (C) 2026 tekno-daw contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Add tekno-daw to the desktop's application menu (GNOME, KDE, any freedesktop desktop).
# The entry runs scripts/tekno-daw.sh from this checkout, so a click always opens the latest code.
#
#   scripts/install-desktop-entry.sh             install (or refresh after moving the checkout)
#   scripts/install-desktop-entry.sh --uninstall remove it
#
# Only touches files under ~/.local/share; no root needed.
set -euo pipefail

root="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
entry="$data/applications/tekno-daw.desktop"
icon="$data/icons/hicolor/scalable/apps/tekno-daw.svg"

refresh_caches() {
    command -v update-desktop-database >/dev/null && update-desktop-database "$data/applications" || true
    command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t "$data/icons/hicolor" || true
}

if [[ "${1:-}" == "--uninstall" ]]; then
    rm -f "$entry" "$icon"
    refresh_caches
    echo "Removed the tekno-daw launcher."
    exit 0
fi

mkdir -p "$(dirname "$entry")" "$(dirname "$icon")"
install -m 644 "$root/assets/icons/tekno-daw.svg" "$icon"

cat >"$entry" <<DESKTOP
[Desktop Entry]
Type=Application
Name=tekno-daw
GenericName=Digital Audio Workstation
Comment=Pattern-based DAW for acid and mental tekno
Exec="$root/scripts/tekno-daw.sh"
Path=$root
Icon=tekno-daw
Terminal=false
Categories=AudioVideo;Audio;Midi;Music;
Keywords=daw;acid;303;techno;tekno;sequencer;music;
StartupWMClass=tekno-daw
StartupNotify=true
Actions=light;no-audio;

[Desktop Action light]
Name=Open with the light theme
Exec="$root/scripts/tekno-daw.sh" --theme=light

[Desktop Action no-audio]
Name=Open without audio
Exec="$root/scripts/tekno-daw.sh" --no-audio
DESKTOP
chmod 644 "$entry"

if command -v desktop-file-validate >/dev/null; then
    desktop-file-validate "$entry"
fi
refresh_caches
echo "Installed: search \"tekno-daw\" in the application menu (right click on it: Pin to Dash)."
