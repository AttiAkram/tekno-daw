// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/dsp/Shaper.h"

namespace tekno::core::dsp
{
Shaper::Curve Shaper::Curve::through (const std::array<float, shaperPoints>& points) noexcept
{
    Curve c;
    c.y = points;
    c.integral[0] = 0.0;
    for (size_t k = 1; k < c.y.size(); ++k) // trapezoids: exact for straight segments
        c.integral[k] =
            c.integral[k - 1] + spacing * 0.5 * (static_cast<double> (c.y[k - 1]) + static_cast<double> (c.y[k]));
    return c;
}

double Shaper::Curve::at (double x) const noexcept
{
    if (x <= -1.0)
        return y.front();
    if (x >= 1.0)
        return y.back();
    const auto position = (x + 1.0) / spacing;
    const auto k = std::min (static_cast<size_t> (position), y.size() - 2);
    const auto t = position - static_cast<double> (k);
    return static_cast<double> (y[k]) + (static_cast<double> (y[k + 1]) - static_cast<double> (y[k])) * t;
}

double Shaper::Curve::integralAt (double x) const noexcept
{
    if (x <= -1.0)
        return static_cast<double> (y.front()) * (x + 1.0);
    if (x >= 1.0)
        return integral.back() + static_cast<double> (y.back()) * (x - 1.0);
    const auto position = (x + 1.0) / spacing;
    const auto k = std::min (static_cast<size_t> (position), y.size() - 2);
    const auto d = x - (-1.0 + static_cast<double> (k) * spacing); // into the segment
    const auto slope = (static_cast<double> (y[k + 1]) - static_cast<double> (y[k])) / spacing;
    return integral[k] + static_cast<double> (y[k]) * d + 0.5 * slope * d * d;
}

void Shaper::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    smoothing = onePole (1.0f / 0.01f / 6.283185307f, sampleRate); // ~10 ms
    reset();
}

void Shaper::reset() noexcept
{
    channels = {};
    fresh = true;
}

EffectMeters Shaper::process (float* left, float* right, int numSamples,
                              const std::array<float, maxEffectParams>& p) noexcept
{
    std::array<float, shaperPoints> target {};
    for (size_t k = 0; k < target.size(); ++k)
        target[k] = std::clamp (p[firstPoint + k], -1.0f, 1.0f);
    const auto targetDrive = decibelsToGain (p[drive]);
    const auto targetOutput = decibelsToGain (p[output]);
    if (fresh)
    {
        points = target;
        curve = Curve::through (points);
        driveGain = targetDrive;
        outputGain = targetOutput;
        fresh = false;
    }
    auto gliding = false;
    for (size_t k = 0; k < target.size(); ++k)
        gliding = gliding || std::abs (target[k] - points[k]) > 1.0e-5f;
    const auto [dryGain, wetGain] = mixGains (p[mix]);

    EffectMeters meters;
    float* io[] { left, right };
    for (int i = 0; i < numSamples; ++i)
    {
        driveGain += (targetDrive - driveGain) * smoothing;
        outputGain += (targetOutput - outputGain) * smoothing;
        if (gliding)
        {
            for (size_t k = 0; k < points.size(); ++k)
                points[k] += (target[k] - points[k]) * smoothing;
            curve = Curve::through (points);
        }

        for (size_t c = 0; c < 2; ++c)
        {
            auto& s = channels[c];
            const auto dry = io[c][i];
            const auto x = static_cast<double> (dry * driveGain);
            const auto delta = x - s.previous;
            const auto y = std::abs (delta) < 1.0e-6 ? curve.at (0.5 * (x + s.previous))
                                                     : (curve.integralAt (x) - curve.integralAt (s.previous)) / delta;
            s.previous = x;
            meters.level = std::max (meters.level, static_cast<float> (std::abs (x)));
            meters.clip = std::max (meters.clip, static_cast<float> (std::abs (x)) - 1.0f);

            const auto wet = static_cast<float> (y);
            const auto blocked = wet - s.dcIn + 0.995f * s.dcOut; // DC blocker
            s.dcIn = wet;
            s.dcOut = blocked;

            const auto out = (dry * dryGain + blocked * wetGain) * outputGain;
            io[c][i] = out;
            meters.peak = std::max (meters.peak, std::abs (out));
        }
    }
    if (gliding) // land exactly, so the next block knows it is done
    {
        auto close = true;
        for (size_t k = 0; k < points.size(); ++k)
            close = close && std::abs (target[k] - points[k]) < 1.0e-4f;
        if (close)
        {
            points = target;
            curve = Curve::through (points);
        }
    }
    return meters;
}
} // namespace tekno::core::dsp
