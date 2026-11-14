// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/io/MidiImport.h"

#include <algorithm>
#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

#include "core/model/Pattern.h"

namespace tekno::core::io
{
juce::String MidiMelody::summary() const
{
    juce::String s;
    s << "imported " << static_cast<int> (notes.size()) << " notes from track " << (track + 1);
    if (tracksWithNotes > 1)
        s << " (" << tracksWithNotes << " tracks have notes: track=N picks another)";
    s << ", " << lengthInSteps / 16 << (lengthInSteps == 16 ? " bar" : " bars");
    if (octavesShifted != 0)
        s << ", moved " << std::abs (octavesShifted) << (std::abs (octavesShifted) == 1 ? " octave " : " octaves ")
          << (octavesShifted > 0 ? "up" : "down");
    if (droppedNotes > 0)
        s << "; " << droppedNotes << " notes left out (a pattern holds 4 bars)";
    return s;
}

juce::Result readMidiMelody (const juce::File& file, int wantedTrack, MidiMelody& out)
{
    juce::FileInputStream stream (file);
    if (! stream.openedOk())
        return juce::Result::fail ("cannot read " + file.getFullPathName());

    juce::MidiFile midi;
    if (! midi.readFrom (stream))
        return juce::Result::fail (file.getFileName() + " is not a MIDI file");
    const auto ticksPerBeat = static_cast<int> (midi.getTimeFormat());
    if (ticksPerBeat <= 0)
        return juce::Result::fail (file.getFileName() + " uses SMPTE time, which is not supported");

    // Which tracks have notes; pick the one asked for, or the first.
    std::vector<int> withNotes;
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        const auto& events = *midi.getTrack (t);
        for (int e = 0; e < events.getNumEvents(); ++e)
            if (events.getEventPointer (e)->message.isNoteOn())
            {
                withNotes.push_back (t);
                break;
            }
    }
    if (withNotes.empty())
        return juce::Result::fail (file.getFileName() + " has no notes");

    out = {};
    out.tracksWithNotes = static_cast<int> (withNotes.size());
    if (wantedTrack < 0)
        out.track = withNotes.front();
    else if (wantedTrack < midi.getNumTracks() &&
             std::find (withNotes.begin(), withNotes.end(), wantedTrack) != withNotes.end())
        out.track = wantedTrack;
    else
        return juce::Result::fail ("track " + juce::String (wantedTrack + 1) + " has no notes (" +
                                   juce::String (out.tracksWithNotes) + " tracks do)");

    juce::MidiMessageSequence sequence (*midi.getTrack (out.track));
    sequence.updateMatchedPairs();

    // To our ticks: 24 per step, 96 per beat.
    constexpr auto perBeat = Note::ticksPerStep * 4;
    auto toTicks = [ticksPerBeat] (double midiTicks)
    { return static_cast<int> (std::lround (midiTicks * perBeat / ticksPerBeat)); };

    std::vector<Note> notes;
    for (int e = 0; e < sequence.getNumEvents(); ++e)
    {
        const auto* event = sequence.getEventPointer (e);
        if (! event->message.isNoteOn())
            continue;
        const auto start = toTicks (event->message.getTimeStamp());
        const auto end = event->noteOffObject != nullptr ? toTicks (event->noteOffObject->message.getTimeStamp())
                                                         : start + Note::ticksPerStep;
        notes.push_back ({ start, std::max (1, end - start), event->message.getNoteNumber(), false, false,
                           static_cast<int> (event->message.getVelocity()) });
    }
    std::sort (notes.begin(), notes.end(), earlier);

    // Drop the empty bars before the first note.
    constexpr auto bar = Note::ticksPerStep * 16;
    const auto offset = (notes.front().start / bar) * bar;
    for (auto& n : notes)
        n.start -= offset;

    // Fit the range C1..C7 by whole octaves, if the melody fits at all.
    auto low = 127, high = 0;
    for (const auto& n : notes)
        low = std::min (low, n.pitch), high = std::max (high, n.pitch);
    while (low < Step::minNote && high + 12 <= Step::maxNote)
        low += 12, high += 12, ++out.octavesShifted;
    while (high > Step::maxNote && low - 12 >= Step::minNote)
        low -= 12, high -= 12, --out.octavesShifted;

    // At most 4 bars and 128 notes; whatever is left out is counted.
    constexpr auto lastTick = Pattern::maxSteps * Note::ticksPerStep;
    auto lastEnd = 0;
    for (auto n : notes)
    {
        n.pitch = std::clamp (n.pitch + 12 * out.octavesShifted, static_cast<int> (Step::minNote),
                              static_cast<int> (Step::maxNote));
        if (n.start >= lastTick || static_cast<int> (out.notes.size()) >= Lane::maxNotes)
        {
            ++out.droppedNotes;
            continue;
        }
        n.length = std::min (n.length, Note::maxTicks);
        out.notes.push_back (n);
        lastEnd = std::max (lastEnd, std::min (n.end(), lastTick));
    }
    const auto bars = std::clamp ((lastEnd + bar - 1) / bar, 1, Pattern::maxSteps / 16);
    out.lengthInSteps = bars * 16;
    return juce::Result::ok();
}
} // namespace tekno::core::io
