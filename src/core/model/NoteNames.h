// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <optional>

#include <juce_core/juce_core.h>

namespace tekno::core
{
/** "A2" for MIDI 45: sharps, scientific octaves (middle C = C4 = 60). */
juce::String noteName (int midiNote);

/** Parses "A2", "c#3", "Db3", "Cb4"... Returns nothing for anything else. */
std::optional<int> parseNoteName (const juce::String& text);
} // namespace tekno::core
