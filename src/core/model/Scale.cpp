// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/Scale.h"

#include <array>

#include "core/model/NoteNames.h"

namespace tekno::core
{
namespace
{
    /** Which of the 12 pitch classes, counted up from the root, belong to the scale. */
    std::array<bool, 12> mask (Scale scale)
    {
        auto from = [] (std::initializer_list<int> steps)
        {
            std::array<bool, 12> m {};
            for (const auto s : steps)
                m[static_cast<size_t> (s)] = true;
            return m;
        };
        switch (scale)
        {
        case Scale::major:
            return from ({ 0, 2, 4, 5, 7, 9, 11 });
        case Scale::minor:
            return from ({ 0, 2, 3, 5, 7, 8, 10 });
        case Scale::phrygian:
            return from ({ 0, 1, 3, 5, 7, 8, 10 });
        case Scale::dorian:
            return from ({ 0, 2, 3, 5, 7, 9, 10 });
        case Scale::harmonic:
            return from ({ 0, 2, 3, 5, 7, 8, 11 });
        case Scale::pentatonic:
            return from ({ 0, 3, 5, 7, 10 });
        case Scale::none:
            break;
        }
        std::array<bool, 12> all {};
        all.fill (true);
        return all;
    }

    struct Named
    {
        const char* name;
        Scale scale;
    };
    constexpr Named scaleNames[] {
        { "major", Scale::major },
        { "maggiore", Scale::major },
        { "minor", Scale::minor },
        { "minore", Scale::minor },
        { "phrygian", Scale::phrygian },
        { "frigio", Scale::phrygian },
        { "frigia", Scale::phrygian },
        { "dorian", Scale::dorian },
        { "dorico", Scale::dorian },
        { "dorica", Scale::dorian },
        { "harmonic", Scale::harmonic },
        { "armonica", Scale::harmonic },
        { "pentatonic", Scale::pentatonic },
        { "pentatonica", Scale::pentatonic },
        { "off", Scale::none },
        { "none", Scale::none },
    };

    const char* italianName (Scale scale)
    {
        switch (scale)
        {
        case Scale::major:
            return "maggiore";
        case Scale::minor:
            return "minore";
        case Scale::phrygian:
            return "frigia";
        case Scale::dorian:
            return "dorica";
        case Scale::harmonic:
            return "minore armonica";
        case Scale::pentatonic:
            return "pentatonica minore";
        case Scale::none:
            break;
        }
        return "";
    }

    int pitchClass (int midiNote) noexcept
    {
        return ((midiNote % 12) + 12) % 12;
    }
} // namespace

bool Key::contains (int midiNote) const noexcept
{
    return mask (scale)[static_cast<size_t> (pitchClass (midiNote - root))];
}

bool Key::isRoot (int midiNote) const noexcept
{
    return active() && pitchClass (midiNote) == root;
}

int Key::nearestInScale (int midiNote) const noexcept
{
    for (int distance = 0; distance < 12; ++distance)
    {
        if (contains (midiNote - distance))
            return midiNote - distance;
        if (contains (midiNote + distance))
            return midiNote + distance;
    }
    return midiNote;
}

juce::String Key::describe() const
{
    if (! active())
        return "no key";
    if (getNoteNaming() == NoteNaming::latin)
        return rootName (root) + " " + italianName (scale);
    return rootName (root) + " " + scaleName (scale);
}

juce::String scaleName (Scale scale)
{
    for (const auto& n : scaleNames)
        if (n.scale == scale)
            return n.name;
    return "off";
}

std::optional<Scale> parseScale (const juce::String& name)
{
    const auto n = name.trim().toLowerCase();
    for (const auto& s : scaleNames)
        if (n == s.name)
            return s.scale;
    return std::nullopt;
}

std::optional<int> parseRoot (const juce::String& name)
{
    // A note name with a made-up octave, so the same parser reads both systems.
    if (const auto note = parseNoteName (name.trim() + "4"))
        return pitchClass (*note);
    return std::nullopt;
}

juce::String rootName (int root, bool forFile)
{
    const auto name = noteName (60 + pitchClass (root), forFile ? NoteNaming::letters : getNoteNaming());
    return name.dropLastCharacters (1); // without the octave
}
} // namespace tekno::core
