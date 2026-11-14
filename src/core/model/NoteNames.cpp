// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/NoteNames.h"

namespace tekno::core
{
namespace
{
    constexpr const char* sharpNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr int naturals[] { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
} // namespace

juce::String noteName (int midiNote)
{
    const auto octave = midiNote / 12 - 1;
    return juce::String (sharpNames[((midiNote % 12) + 12) % 12]) + juce::String (octave);
}

std::optional<int> parseNoteName (const juce::String& raw)
{
    const auto text = raw.trim();
    if (text.length() < 2)
        return std::nullopt;

    const auto letter = juce::CharacterFunctions::toUpperCase (text[0]);
    if (letter < 'A' || letter > 'G')
        return std::nullopt;
    auto pitchClass = naturals[letter - 'A'];

    int pos = 1;
    if (text[pos] == '#')
    {
        ++pitchClass;
        ++pos;
    }
    else if (text[pos] == 'b')
    {
        --pitchClass;
        ++pos;
    }

    const auto octaveText = text.substring (pos);
    if (octaveText.isEmpty() || ! octaveText.containsOnly ("-0123456789"))
        return std::nullopt;

    return (octaveText.getIntValue() + 1) * 12 + pitchClass;
}
} // namespace tekno::core
