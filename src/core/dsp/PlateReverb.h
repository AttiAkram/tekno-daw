// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include "core/dsp/EffectTools.h"

namespace tekno::core::dsp
{
/** A plate reverb: Jon Dattorro's published algorithm ("Effect Design, Part 1", JAES 1997).

    Mono in (with a predelay and four input diffusers), then a "tank" of two crossed halves, each a
    modulated allpass, a long delay, a damping low-pass, a second allpass and another delay, feeding
    the other half. The stereo output is read from fixed taps inside the tank, as in the paper. All
    lengths are the paper's, given at 29761 Hz, scaled to the running sample rate. A low cut on the
    output keeps the tail out of the kick. */
class PlateReverb
{
public:
    enum Param
    {
        decay,
        damping,
        predelay,
        lowcut,
        mix,
    };

    /** About how long the tail takes to fall by 60 dB at this decay setting: the graph draws it. From
        the tank's loop (both halves) and the decay gain applied four times on the way round. */
    static float tailSeconds (float decay) noexcept;
    static float tankGain (float decay) noexcept { return 0.2f + 0.77f * std::clamp (decay, 0.0f, 1.0f); }

    void prepare (double sampleRate); // allocates: never on the audio thread
    void reset() noexcept;
    EffectMeters process (float* left, float* right, int numSamples,
                          const std::array<float, maxEffectParams>& params) noexcept;

private:
    struct Allpass
    {
        DelayLine line;
        int length = 1;
        float process (float x, float g) noexcept { return step (x, g, line.read (length)); }
        float process (float x, float g, float modulation) noexcept
        {
            return step (x, g, line.read (static_cast<float> (length) + modulation));
        }
        float step (float x, float g, float delayed) noexcept
        {
            const auto w = x - g * delayed;
            line.push (w);
            return delayed + g * w;
        }
    };

    int scaled (int samplesAt29761) const noexcept;

    double sampleRate = 48000.0;
    DelayLine predelayLine;
    std::array<Allpass, 4> diffusers;
    Allpass leftModulated, leftSecond, rightModulated, rightSecond;
    DelayLine leftFirst, leftLast, rightFirst, rightLast;
    int leftFirstLength = 1, leftLastLength = 1, rightFirstLength = 1, rightLastLength = 1;
    float excursion = 0.0f;
    double lfoPhase = 0.0, lfoStep = 0.0;
    float bandwidthState = 0.0f, dampLeft = 0.0f, dampRight = 0.0f;
    float tankLeft = 0.0f, tankRight = 0.0f; // the last outputs of each half, crossing over
    float cutLeft = 0.0f, cutRight = 0.0f;
};
} // namespace tekno::core::dsp
