// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/Session.h"

namespace tekno::core
{
void AcidParams::reset() noexcept
{
    cutoff = defaultCutoff;
    resonance = defaultResonance;
    envMod = defaultEnvMod;
    decay = defaultDecay;
    accent = defaultAccent;
    square = false;
}

Session::Session()
{
    loadDefaultProject();
}

void Session::loadDefaultProject()
{
    transport.playing = false;
    transport.bpm = TransportState::defaultBpm;
    transport.swing = TransportState::minSwing;
    synth.reset();

    for (int i = 0; i < maxPatterns; ++i)
    {
        pattern (i).clear();
        project.patternNames[static_cast<size_t> (i)] = defaultPatternName (i);
    }
    project.patternNames[0] = "acid 01";
    selected = 0;
    playing = 0;

    // A minor acid line: A2 root with octave jumps, accents on the off-beats, slides into the jumps.
    constexpr int A2 = 45, C3 = 48, D3 = 50, E3 = 52, G3 = 55, A3 = 57;
    auto on = [] (int note, bool accent = false, bool slide = false) { return Step { true, accent, slide, note }; };
    const Step off { false, false, false, A2 };
    const Step line[Pattern::defaultLength] = {
        on (A2, true),
        off,
        on (A3, false, true),
        on (A2),
        on (C3),
        off,
        on (A2, true),
        on (E3, false, true),
        on (G3),
        off,
        on (A2),
        on (A3, true, true),
        off,
        on (D3),
        on (A2, false, true),
        on (A3),
    };
    for (int i = 0; i < Pattern::defaultLength; ++i)
        patterns[0].setStep (i, line[i]);

    project.file = juce::File();
    project.revision = project.savedRevision = 0;
}
} // namespace tekno::core
