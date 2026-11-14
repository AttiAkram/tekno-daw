// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

namespace tekno::core::dsp
{
/** Four-pole lowpass ladder, zero-delay-feedback (topology-preserving transform, after
    V. Zavalishin, "The Art of VA Filter Design"). The feedback loop is solved exactly, so it stays
    stable and in tune up to self-oscillation (k = 4). A tanh on the loop input gives the soft,
    "rubbery" resonance of analogue ladders.

    v0 of the acid filter: 24 dB/oct. The 303's diode ladder is closer to 18 dB/oct; see the
    sound-design notes before changing the topology. */
class LadderFilter
{
public:
    void setSampleRate (double newSampleRate) noexcept { sampleRate = static_cast<float> (newSampleRate); }
    void reset() noexcept { state = {}; }

    /** cutoffHz is clamped to a safe range; k is resonance, 0..~4. */
    float process (float input, float cutoffHz, float k) noexcept
    {
        const auto fc = std::clamp (cutoffHz, 20.0f, sampleRate * 0.45f);
        const auto g = std::tan (juce::MathConstants<float>::pi * fc / sampleRate);
        const auto G = g / (1.0f + g); // one-pole gain
        const auto beta = 1.0f / (1.0f + g);

        // Output of the 4th stage = G^4 * u + S, where S collects the stages' states.
        const auto G2 = G * G;
        const auto S = beta * (G2 * G * state[0] + G2 * state[1] + G * state[2] + state[3]);
        const auto y4 = (G2 * G2 * input + S) / (1.0f + k * G2 * G2);

        auto u = std::tanh (input - k * y4);
        for (auto& s : state)
        {
            const auto v = (u - s) * G;
            const auto y = v + s;
            s = y + v;
            u = y;
        }

        // A ladder loses low end as resonance rises; give part of it back.
        return u * (1.0f + 0.6f * k);
    }

private:
    float sampleRate = 48000.0f;
    std::array<float, 4> state {};
};
} // namespace tekno::core::dsp
