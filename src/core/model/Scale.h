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
/** The scales the guide knows: the ones acid and tekno use most. */
enum class Scale
{
    none, // no guide: every note is as good as any other
    major,
    minor, // natural minor
    phrygian,
    dorian,
    harmonic,   // harmonic minor
    pentatonic, // minor pentatonic
};

/** A key: the root (0 = C/Do ... 11 = B/Si) and a scale. */
struct Key
{
    int root = 9; // A / La
    Scale scale = Scale::none;

    bool active() const noexcept { return scale != Scale::none; }
    bool contains (int midiNote) const noexcept; // always true when not active
    bool isRoot (int midiNote) const noexcept;
    int nearestInScale (int midiNote) const noexcept; // ties go down

    /** "A minor" / "La minore", following the note naming preference; "no key" when off. */
    juce::String describe() const;

    friend bool operator== (const Key&, const Key&) = default;
};

juce::String scaleName (Scale scale);                       // "minor": what files and commands use
std::optional<Scale> parseScale (const juce::String& name); // English or Italian: "minor", "minore", "frigio"
std::optional<int> parseRoot (const juce::String& name);    // "A", "La", "C#", "Sib" (no octave)
juce::String rootName (int root, bool forFile = false);     // letters for files, the preference otherwise
} // namespace tekno::core
