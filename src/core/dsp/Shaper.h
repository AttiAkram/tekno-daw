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
/** The waveshaper: the input, after the drive, goes through a curve the user draws. Nine points at
    x = -1, -0.75 ... 1, joined by straight segments; past +-1 the curve stays flat (it clips at the end
    points). Then a DC blocker (a lopsided curve moves the zero), the mix and the output level.

    Antialiased with first-order ADAA, exactly: the antiderivative of straight segments is made of
    parabolas, so the output is the average of the curve between two inputs, as in the distortion. When
    the points move they glide (~10 ms), so dragging them on the graph doesn't click. */
class Shaper
{
public:
    enum Param
    {
        drive,
        firstPoint,
        mix = firstPoint + shaperPoints,
        output,
    };

    /** The curve as segments, with the integral from -1 up to each point: what the audio and the graph
        both read. */
    struct Curve
    {
        std::array<float, shaperPoints> y {};
        std::array<double, shaperPoints> integral {};

        static Curve through (const std::array<float, shaperPoints>& points) noexcept;
        double at (double x) const noexcept;         // the curve
        double integralAt (double x) const noexcept; // its antiderivative, 0 at x = -1
    };
    static constexpr double spacing = 2.0 / (shaperPoints - 1); // between two points on x

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    EffectMeters process (float* left, float* right, int numSamples,
                          const std::array<float, maxEffectParams>& params) noexcept;

private:
    struct ChannelState
    {
        double previous = 0.0; // last curve input (ADAA)
        float dcIn = 0.0f, dcOut = 0.0f;
    };

    double sampleRate = 48000.0;
    std::array<ChannelState, 2> channels;
    std::array<float, shaperPoints> points {}; // where the points are now, gliding to the knobs'
    Curve curve;
    float driveGain = 1.0f, outputGain = 1.0f; // smoothed
    float smoothing = 1.0f;
    bool fresh = true;
};
} // namespace tekno::core::dsp
