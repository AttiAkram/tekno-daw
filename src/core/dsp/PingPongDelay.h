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
/** A stereo ping-pong delay synced to the tempo, for dub throws and acid echoes.

    The time is in steps (1/16 notes); when it or the tempo changes the delay glides there, so nothing
    clicks (it bends the pitch of the echoes for a moment, like a tape delay). The feedback path has a
    low-pass (tone), a high-pass (lowcut) and a tanh, so feedback above 1 runs away into a loud but
    bounded howl instead of exploding. With ping-pong at 1 the input enters on the left and each echo
    crosses to the other side. */
class PingPongDelay
{
public:
    enum Param
    {
        time,
        feedback,
        pingpong,
        tone,
        lowcut,
        mix,
    };

    static constexpr double maxSeconds = 2.0;

    void prepare (double sampleRate); // allocates: never on the audio thread
    void reset() noexcept;
    EffectMeters process (float* left, float* right, int numSamples, const std::array<float, maxEffectParams>& params,
                          double bpm) noexcept;

private:
    double sampleRate = 48000.0;
    DelayLine lineLeft, lineRight;
    float delay = 0.0f; // samples, gliding to the target
    float glide = 1.0f;
    float lowLeft = 0.0f, lowRight = 0.0f, highLeft = 0.0f, highRight = 0.0f;
    bool fresh = true;
};
} // namespace tekno::core::dsp
