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
/** The waveshaper for mental kicks and sub-basses: hard clip, foldback or bitcrush, then a tone
    low-pass, a DC blocker, the mix and the output level.

    Clip and fold use first-order antiderivative antialiasing (ADAA1), as the 303's drive does: the
    output is the average of the shaper over the step between two inputs, so most of the aliasing a
    hard shaper makes never happens, with no oversampling and no latency. Crush aliases on purpose. */
class Distortion
{
public:
    enum Param
    {
        mode,
        drive,
        tone,
        bits,
        rate,
        mix,
        output,
    };
    enum Mode
    {
        clip,
        fold,
        crush,
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    EffectMeters process (float* left, float* right, int numSamples,
                          const std::array<float, maxEffectParams>& params) noexcept;

    // The shapers and their antiderivatives, public for the tests.
    static double hardClip (double x) noexcept;
    static double hardClipIntegral (double x) noexcept;
    static double foldback (double x) noexcept;
    static double foldbackIntegral (double x) noexcept;

private:
    struct ChannelState
    {
        double previous = 0.0; // last shaper input (ADAA)
        float held = 0.0f, holdPhase = 1.0e9f;
        float tone = 0.0f, dcIn = 0.0f, dcOut = 0.0f;
    };

    double sampleRate = 48000.0;
    std::array<ChannelState, 2> channels;
    float driveGain = 1.0f, outputGain = 1.0f; // smoothed
    float smoothing = 1.0f;
    bool fresh = true;
};
} // namespace tekno::core::dsp
