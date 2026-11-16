// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/dsp/Distortion.h"

namespace tekno::core::dsp
{
namespace
{
    /** First-order ADAA of a shaper f with antiderivative F. */
    template <typename Shaper, typename Integral>
    double antialiased (double x, double previous, Shaper f, Integral F) noexcept
    {
        const auto delta = x - previous;
        if (std::abs (delta) < 1.0e-6)
            return f (0.5 * (x + previous));
        return (F (x) - F (previous)) / delta;
    }

    /** Where x falls in the fold's period of 4, as 0..4 (the fold is a triangle wave). */
    double foldPhase (double x) noexcept
    {
        auto m = std::fmod (x + 1.0, 4.0);
        return m < 0.0 ? m + 4.0 : m;
    }
} // namespace

double Distortion::hardClip (double x) noexcept
{
    return std::clamp (x, -1.0, 1.0);
}

double Distortion::hardClipIntegral (double x) noexcept
{
    const auto a = std::abs (x);
    return a <= 1.0 ? 0.5 * x * x : a - 0.5;
}

double Distortion::foldback (double x) noexcept
{
    // A triangle through (0, 0), (1, 1), (3, -1): what goes past full scale folds back down.
    return 1.0 - std::abs (foldPhase (x) - 2.0);
}

double Distortion::foldbackIntegral (double x) noexcept
{
    // The integral over one period is 0, so this repeats with the triangle; the constant doesn't
    // matter to ADAA, which only uses differences.
    const auto m = foldPhase (x);
    return m <= 2.0 ? 0.5 * m * m - m : 3.0 * m - 0.5 * m * m - 4.0;
}

double Distortion::transfer (int shaperMode, float driveDb, float bits, double x) noexcept
{
    const auto driven = x * static_cast<double> (decibelsToGain (driveDb));
    if (shaperMode == crush)
    {
        const auto levels = static_cast<double> (std::exp2 (std::round (std::clamp (bits, 2.0f, 16.0f)) - 1.0f));
        return std::round (hardClip (driven) * levels) / levels;
    }
    return shaperMode == fold ? foldback (driven) : hardClip (driven);
}

void Distortion::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    smoothing = onePole (1.0f / 0.01f / 6.283185307f, sampleRate); // ~10 ms
    reset();
}

void Distortion::reset() noexcept
{
    channels = {};
    fresh = true;
}

EffectMeters Distortion::process (float* left, float* right, int numSamples,
                                  const std::array<float, maxEffectParams>& p) noexcept
{
    const auto shaperMode = static_cast<int> (std::lround (p[mode]));
    const auto targetDrive = decibelsToGain (p[drive]);
    const auto targetOutput = decibelsToGain (p[output]);
    if (fresh)
    {
        driveGain = targetDrive;
        outputGain = targetOutput;
        fresh = false;
    }
    const auto toneOn = p[tone] < 0.999f;
    const auto toneCoeff = onePole (1000.0f * std::pow (20.0f, std::clamp (p[tone], 0.0f, 1.0f)), sampleRate);
    const auto levels = std::exp2 (std::round (std::clamp (p[bits], 2.0f, 16.0f)) - 1.0f);
    const auto holdEvery = std::clamp (p[rate], 1.0f, 32.0f);
    const auto [dryGain, wetGain] = mixGains (p[mix]);

    EffectMeters meters;
    float* io[] { left, right };
    for (int i = 0; i < numSamples; ++i)
    {
        driveGain += (targetDrive - driveGain) * smoothing;
        outputGain += (targetOutput - outputGain) * smoothing;

        for (size_t c = 0; c < 2; ++c)
        {
            auto& s = channels[c];
            const auto dry = io[c][i];
            meters.level = std::max (meters.level, std::abs (dry));
            const auto x = static_cast<double> (dry * driveGain);
            double y;
            if (shaperMode == crush)
            {
                // Sample and hold every `rate` samples, then fewer levels: the grit is the aliasing.
                if ((s.holdPhase += 1.0f) >= holdEvery)
                {
                    s.holdPhase -= holdEvery;
                    if (s.holdPhase >= holdEvery)
                        s.holdPhase = 0.0f;
                    s.held = std::round (static_cast<float> (hardClip (x)) * levels) / levels;
                }
                y = s.held;
            }
            else if (shaperMode == fold)
                y = antialiased (x, s.previous, foldback, foldbackIntegral);
            else
                y = antialiased (x, s.previous, hardClip, hardClipIntegral);
            s.previous = x;
            meters.clip = std::max (meters.clip, static_cast<float> (std::abs (x)) - 1.0f);

            auto wet = static_cast<float> (y);
            if (toneOn)
                wet = (s.tone += (wet - s.tone) * toneCoeff);
            const auto blocked = wet - s.dcIn + 0.995f * s.dcOut; // DC blocker
            s.dcIn = wet;
            s.dcOut = blocked;

            const auto out = (dry * dryGain + blocked * wetGain) * outputGain;
            io[c][i] = out;
            meters.peak = std::max (meters.peak, std::abs (out));
        }
    }
    return meters;
}
} // namespace tekno::core::dsp
