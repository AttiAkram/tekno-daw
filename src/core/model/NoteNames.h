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
/** How notes are named to people. The octave number is the same in both: middle C = C4 = Do4 = 60. */
enum class NoteNaming
{
    letters, // C D E F G A B
    latin,   // Do Re Mi Fa Sol La Si
};

/** The naming noteName() uses when not told otherwise: the user's preference. Files always use letters. */
void setNoteNaming (NoteNaming naming) noexcept;
NoteNaming getNoteNaming() noexcept;

/** "A2" (or "La2") for MIDI 45: sharps, scientific octaves. */
juce::String noteName (int midiNote);
juce::String noteName (int midiNote, NoteNaming naming);

/** Parses either system, any case: "A2", "c#3", "Db3", "La2", "sol#3", "Sib2". Nothing for anything else. */
std::optional<int> parseNoteName (const juce::String& text);
} // namespace tekno::core
