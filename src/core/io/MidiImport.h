// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "core/model/Note.h"

namespace tekno::core::io
{
/** A melody read from a standard MIDI file, ready to become a lane. */
struct MidiMelody
{
    std::vector<Note> notes; // sorted, at most Lane::maxNotes, within 64 steps
    int lengthInSteps = 16;  // whole bars, 16 to 64
    int track = 0;           // the track it came from (0-based)
    int tracksWithNotes = 0; // how many tracks had notes, to suggest track=
    int droppedNotes = 0;    // past 4 bars or over the per-lane limit
    int octavesShifted = 0;  // moved to fit C1..C7 (positive = up)

    /** One line for the person who imported it: what came in, and what didn't. */
    juce::String summary() const;
};

/** Reads one track of a .mid file: the first with notes when `track` is negative. Timing is kept as
    it is (no quantising), at 96 ticks per beat; leading empty bars are dropped. */
juce::Result readMidiMelody (const juce::File& file, int track, MidiMelody& out);
} // namespace tekno::core::io
