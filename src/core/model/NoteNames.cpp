// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/NoteNames.h"

#include <atomic>
#include <cstring>

namespace tekno::core
{
namespace
{
    constexpr const char* letterNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr const char* latinNames[] {
        "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"
    };

    struct Name
    {
        const char* text;
        int pitchClass;
    };
    // Longest first, so "sol" wins over "so..." and "si" is not read as "s" + "i".
    constexpr Name natural[] { { "sol", 7 }, { "do", 0 },  { "re", 2 }, { "mi", 4 }, { "fa", 5 },
                               { "la", 9 },  { "si", 11 }, { "c", 0 },  { "d", 2 },  { "e", 4 },
                               { "f", 5 },   { "g", 7 },   { "a", 9 },  { "b", 11 } };

    std::atomic<NoteNaming> preferred { NoteNaming::letters };
} // namespace

void setNoteNaming (NoteNaming naming) noexcept
{
    preferred.store (naming);
}

NoteNaming getNoteNaming() noexcept
{
    return preferred.load();
}

juce::String noteName (int midiNote)
{
    return noteName (midiNote, getNoteNaming());
}

juce::String noteName (int midiNote, NoteNaming naming)
{
    const auto octave = (midiNote >= 0 ? midiNote / 12 : (midiNote - 11) / 12) - 1;
    const auto pitchClass = ((midiNote % 12) + 12) % 12;
    const auto* names = naming == NoteNaming::latin ? latinNames : letterNames;
    return juce::String (names[pitchClass]) + juce::String (octave);
}

std::optional<int> parseNoteName (const juce::String& raw)
{
    const auto text = raw.trim().toLowerCase();

    for (const auto& name : natural)
    {
        if (! text.startsWith (name.text))
            continue;
        auto pitchClass = name.pitchClass;
        auto pos = static_cast<int> (std::strlen (name.text));
        if (text[pos] == '#')
            ++pitchClass, ++pos;
        else if (text[pos] == 'b')
            --pitchClass, ++pos;

        const auto octaveText = text.substring (pos);
        if (octaveText.isEmpty() || ! octaveText.containsOnly ("-0123456789"))
            return std::nullopt;
        return (octaveText.getIntValue() + 1) * 12 + pitchClass;
    }
    return std::nullopt;
}
} // namespace tekno::core
