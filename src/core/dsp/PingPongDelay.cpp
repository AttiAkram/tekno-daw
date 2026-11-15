// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/dsp/PingPongDelay.h"

namespace tekno::core::dsp
{
void PingPongDelay::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    const auto samples = static_cast<int> (std::ceil (maxSeconds * sampleRate)) + 4;
    lineLeft.allocate (samples);
    lineRight.allocate (samples);
    glide = onePole (1.0f / 0.08f / 6.283185307f, sampleRate); // ~80 ms to a new time
    reset();
}

void PingPongDelay::reset() noexcept
{
    lineLeft.clear();
    lineRight.clear();
    lowLeft = lowRight = highLeft = highRight = 0.0f;
    fresh = true;
}

EffectMeters PingPongDelay::process (float* left, float* right, int numSamples,
                                     const std::array<float, maxEffectParams>& p, double bpm) noexcept
{
    const auto stepSamples = sampleRate * 60.0 / std::max (1.0, bpm) / 4.0;
    const auto target = static_cast<float> (
        std::clamp (static_cast<double> (p[time]) * stepSamples, 1.0, static_cast<double> (lineLeft.capacity() - 2)));
    if (fresh)
    {
        delay = target;
        fresh = false;
    }
    const auto amount = std::clamp (p[feedback], 0.0f, 1.1f);
    const auto cross = std::clamp (p[pingpong], 0.0f, 1.0f);
    const auto lowCoeff = onePole (500.0f * std::pow (32.0f, std::clamp (p[tone], 0.0f, 1.0f)), sampleRate);
    const auto highCoeff = onePole (20.0f * std::pow (50.0f, std::clamp (p[lowcut], 0.0f, 1.0f)), sampleRate);
    const auto [dryGain, wetGain] = mixGains (p[mix]);

    EffectMeters meters;
    for (int i = 0; i < numSamples; ++i)
    {
        delay += (target - delay) * glide;
        const auto echoLeft = lineLeft.read (delay), echoRight = lineRight.read (delay);

        // Feedback: crossed for ping-pong, darkened, thinned, then saturated.
        auto backLeft = echoLeft * (1.0f - cross) + echoRight * cross;
        auto backRight = echoRight * (1.0f - cross) + echoLeft * cross;
        lowLeft += (backLeft - lowLeft) * lowCoeff;
        lowRight += (backRight - lowRight) * lowCoeff;
        highLeft += (lowLeft - highLeft) * highCoeff;
        highRight += (lowRight - highRight) * highCoeff;
        backLeft = std::tanh (amount * (lowLeft - highLeft));
        backRight = std::tanh (amount * (lowRight - highRight));

        const auto dryLeft = left[i], dryRight = right[i];
        const auto mono = 0.5f * (dryLeft + dryRight);
        lineLeft.push (dryLeft * (1.0f - cross) + mono * cross + backLeft);
        lineRight.push (dryRight * (1.0f - cross) + backRight);

        left[i] = dryLeft * dryGain + echoLeft * wetGain;
        right[i] = dryRight * dryGain + echoRight * wetGain;
        meters.peak = std::max (meters.peak, std::max (std::abs (echoLeft), std::abs (echoRight)) * wetGain);
    }
    return meters;
}
} // namespace tekno::core::dsp
