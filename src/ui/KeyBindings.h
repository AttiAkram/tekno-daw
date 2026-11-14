// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/commands/Keymap.h"

namespace tekno::ui
{
/** Keys as text, the default keymap, and the keymap file. The keymap itself is core::Keymap. */
namespace keys
{
    /** "ctrl + Z", "spacebar", "F7": JUCE's description, the form the keymap stores. */
    juce::String describe (const juce::KeyPress& key);

    /** Reads what a person types ("ctrl+shift+s", "Space", "f7") into the stored form; nothing when it
        isn't a key. */
    std::optional<juce::String> normalise (const juce::String& text);

    /** The bindings tekno-daw starts with (and keys.reset brings back). */
    core::Keymap defaults();

    /** $XDG_CONFIG_HOME/tekno-daw/keymap.json, next to settings.xml. */
    juce::File defaultFile();

    /** The saved keymap, or the defaults when there is none. Problems go to `warnings`. */
    core::Keymap load (const juce::File& file, juce::StringArray& warnings);
    bool save (const core::Keymap& keymap, const juce::File& file);
} // namespace keys
} // namespace tekno::ui
