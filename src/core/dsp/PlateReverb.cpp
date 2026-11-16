// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/dsp/PlateReverb.h"

namespace tekno::core::dsp
{
namespace
{
    constexpr double paperRate = 29761.0;
    constexpr int tap (int samples) noexcept
    {
        return samples;
    } // marks the paper's tap positions
} // namespace

float PlateReverb::tailSeconds (float decayParam) noexcept
{
    // The tank's loop: both halves' delays and allpasses, as the paper gives them at 29761 Hz.
    constexpr double loopSeconds = (672.0 + 4453.0 + 1800.0 + 3720.0 + 908.0 + 4217.0 + 2656.0 + 3163.0) / paperRate;
    const auto perLoopDb = -20.0 * 4.0 * std::log10 (static_cast<double> (tankGain (decayParam)));
    return static_cast<float> (60.0 / perLoopDb * loopSeconds);
}

int PlateReverb::scaled (int samples) const noexcept
{
    return std::max (1, static_cast<int> (std::lround (samples * sampleRate / paperRate)));
}

void PlateReverb::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    excursion = static_cast<float> (16.0 * sampleRate / paperRate);

    predelayLine.allocate (static_cast<int> (std::ceil (0.2 * sampleRate)) + 2);
    const int diffuserLengths[] { 142, 107, 379, 277 };
    for (size_t i = 0; i < diffusers.size(); ++i)
    {
        diffusers[i].length = scaled (diffuserLengths[i]);
        diffusers[i].line.allocate (diffusers[i].length + 2);
    }
    auto setUp = [this] (Allpass& a, int length, bool modulated)
    {
        a.length = scaled (length);
        a.line.allocate (a.length + (modulated ? static_cast<int> (std::ceil (excursion)) + 4 : 2));
    };
    setUp (leftModulated, 672, true);
    setUp (leftSecond, 1800, false);
    setUp (rightModulated, 908, true);
    setUp (rightSecond, 2656, false);
    leftFirstLength = scaled (4453);
    leftLastLength = scaled (3720);
    rightFirstLength = scaled (4217);
    rightLastLength = scaled (3163);
    leftFirst.allocate (leftFirstLength + 2);
    leftLast.allocate (leftLastLength + 2);
    rightFirst.allocate (rightFirstLength + 2);
    rightLast.allocate (rightLastLength + 2);

    lfoStep = 6.283185307179586 * 0.8 / sampleRate; // a slow wobble: the plate's shimmer
    reset();
}

void PlateReverb::reset() noexcept
{
    predelayLine.clear();
    for (auto& d : diffusers)
        d.line.clear();
    for (auto* a : { &leftModulated, &leftSecond, &rightModulated, &rightSecond })
        a->line.clear();
    for (auto* d : { &leftFirst, &leftLast, &rightFirst, &rightLast })
        d->clear();
    lfoPhase = 0.0;
    bandwidthState = dampLeft = dampRight = tankLeft = tankRight = cutLeft = cutRight = 0.0f;
}

EffectMeters PlateReverb::process (float* left, float* right, int numSamples,
                                   const std::array<float, maxEffectParams>& p) noexcept
{
    const auto tankDecay = tankGain (p[decay]); // up to 0.97: very long tails
    const auto damp = 0.05f + 0.8f * std::clamp (p[damping], 0.0f, 1.0f);
    const auto predelaySamples = static_cast<int> (std::clamp (p[predelay], 0.0f, 200.0f) * 0.001f * sampleRate);
    const auto cutCoeff = onePole (20.0f * std::pow (40.0f, std::clamp (p[lowcut], 0.0f, 1.0f)), sampleRate);
    const auto [dryGain, wetGain] = mixGains (p[mix]);
    constexpr float bandwidth = 0.9995f, inputDiffusion1 = 0.75f, inputDiffusion2 = 0.625f;
    constexpr float decayDiffusion1 = 0.7f, decayDiffusion2 = 0.5f;

    EffectMeters meters;
    for (int i = 0; i < numSamples; ++i)
    {
        const auto dryLeft = left[i], dryRight = right[i];
        const auto x = 0.5f * (dryLeft + dryRight);

        predelayLine.push (x);
        const auto delayed = predelaySamples < 1 ? x : predelayLine.read (predelaySamples);
        bandwidthState += (delayed - bandwidthState) * bandwidth;
        auto diffused = bandwidthState;
        diffused = diffusers[0].process (diffused, inputDiffusion1);
        diffused = diffusers[1].process (diffused, inputDiffusion1);
        diffused = diffusers[2].process (diffused, inputDiffusion2);
        diffused = diffusers[3].process (diffused, inputDiffusion2);

        lfoPhase += lfoStep;
        if (lfoPhase > 6.283185307179586)
            lfoPhase -= 6.283185307179586;
        const auto wobble = static_cast<float> (std::sin (lfoPhase)) * excursion;

        // The tank: each half takes the input plus the other half's last output.
        auto l = diffused + tankDecay * tankRight;
        l = leftModulated.process (l, -decayDiffusion1, wobble);
        leftFirst.push (l);
        dampLeft += (leftFirst.read (leftFirstLength) - dampLeft) * (1.0f - damp);
        l = leftSecond.process (dampLeft * tankDecay, decayDiffusion2);
        leftLast.push (l);

        auto r = diffused + tankDecay * tankLeft;
        r = rightModulated.process (r, -decayDiffusion1, -wobble);
        rightFirst.push (r);
        dampRight += (rightFirst.read (rightFirstLength) - dampRight) * (1.0f - damp);
        r = rightSecond.process (dampRight * tankDecay, decayDiffusion2);
        rightLast.push (r);

        tankLeft = leftLast.read (leftLastLength);
        tankRight = rightLast.read (rightLastLength);

        // The paper's output taps (positions given at 29761 Hz).
        auto s = [this] (int samples) { return scaled (tap (samples)); };
        auto wetLeft = rightFirst.read (s (266)) + rightFirst.read (s (2974)) - rightSecond.line.read (s (1913)) +
                       rightLast.read (s (1996)) - leftFirst.read (s (1990)) - leftSecond.line.read (s (187)) -
                       leftLast.read (s (1066));
        auto wetRight = leftFirst.read (s (353)) + leftFirst.read (s (3627)) - leftSecond.line.read (s (1228)) +
                        leftLast.read (s (2673)) - rightFirst.read (s (2111)) - rightSecond.line.read (s (335)) -
                        rightLast.read (s (121));
        wetLeft *= 0.6f;
        wetRight *= 0.6f;

        // Low cut on the tail.
        cutLeft += (wetLeft - cutLeft) * cutCoeff;
        cutRight += (wetRight - cutRight) * cutCoeff;
        wetLeft -= cutLeft;
        wetRight -= cutRight;

        left[i] = dryLeft * dryGain + wetLeft * wetGain;
        right[i] = dryRight * dryGain + wetRight * wetGain;
        meters.peak = std::max (meters.peak, std::max (std::abs (wetLeft), std::abs (wetRight)) * wetGain);
    }
    return meters;
}
} // namespace tekno::core::dsp
