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
#include <vector>

#include "core/model/Effects.h"

namespace tekno::core::dsp
{
/** What an effect tells the meters after a block: its loudest output sample, for a distortion how far
    past full scale its input went (0 = never clipped), and for an effect with a curve how far along the
    curve's input the sound reached (the band on its graph). */
struct EffectMeters
{
    float peak = 0.0f;
    float clip = 0.0f;
    float level = 0.0f;
};

/** Dry and wet levels for a mix knob: both full at 0.5, so an insert delay or reverb adds to the dry
    signal at low settings instead of thinning it. */
inline std::pair<float, float> mixGains (float mix) noexcept
{
    mix = std::clamp (mix, 0.0f, 1.0f);
    return { std::min (1.0f, 2.0f * (1.0f - mix)), std::min (1.0f, 2.0f * mix) };
}

inline float decibelsToGain (float db) noexcept
{
    return std::pow (10.0f, db / 20.0f);
}

/** One-pole coefficient for a cutoff in Hz. */
inline float onePole (float hz, double sampleRate) noexcept
{
    return 1.0f - std::exp (-6.283185307f * hz / static_cast<float> (sampleRate));
}

/** A delay line for the effects: written one sample at a time, read any number of samples back. */
class DelayLine
{
public:
    void allocate (int maxDelay) { buffer.assign (static_cast<size_t> (maxDelay + 2), 0.0f); }
    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f), write = 0; }
    int capacity() const noexcept { return static_cast<int> (buffer.size()) - 2; }

    void push (float x) noexcept
    {
        buffer[static_cast<size_t> (write)] = x;
        if (++write == static_cast<int> (buffer.size()))
            write = 0;
    }

    /** The sample pushed `delay` pushes ago (1 = the last one). */
    float read (int delay) const noexcept
    {
        auto i = write - std::clamp (delay, 1, capacity());
        if (i < 0)
            i += static_cast<int> (buffer.size());
        return buffer[static_cast<size_t> (i)];
    }

    /** Between samples, linearly: for modulated and gliding delays. */
    float read (float delay) const noexcept
    {
        delay = std::clamp (delay, 1.0f, static_cast<float> (capacity() - 1));
        const auto whole = static_cast<int> (delay);
        const auto frac = delay - static_cast<float> (whole);
        const auto a = read (whole), b = read (whole + 1);
        return a + (b - a) * frac;
    }

private:
    std::vector<float> buffer { 0.0f, 0.0f, 0.0f };
    int write = 0;
};
} // namespace tekno::core::dsp
