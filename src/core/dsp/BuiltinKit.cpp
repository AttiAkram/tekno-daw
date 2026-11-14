// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/dsp/BuiltinKit.h"

#include <cmath>
#include <cstdint>

namespace tekno::core::dsp::builtinKit
{
namespace
{
    constexpr double twoPi = 6.283185307179586;

    /** White noise from a fixed seed: the kit must be the same on every run and machine. */
    class Noise
    {
    public:
        explicit Noise (std::uint32_t seed)
            : state (seed)
        {
        }
        float next() noexcept
        {
            state = state * 1664525u + 1013904223u; // classic LCG, plenty for noise
            return static_cast<float> (state >> 8) / static_cast<float> (1u << 23) - 1.0f;
        }

    private:
        std::uint32_t state;
    };

    /** RBJ cookbook biquad, enough to shape noise and metal. */
    class Biquad
    {
    public:
        static Biquad highPass (double hz, double q) { return make (hz, q, true); }
        static Biquad bandPass (double hz, double q) { return make (hz, q, false); }

        float process (float x) noexcept
        {
            const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return static_cast<float> (y);
        }

    private:
        static Biquad make (double hz, double q, bool high)
        {
            const auto w = twoPi * hz / sampleRate;
            const auto alpha = std::sin (w) / (2.0 * q);
            const auto cosw = std::cos (w);
            const auto a0 = 1.0 + alpha;
            Biquad f;
            if (high)
            {
                f.b0 = (1.0 + cosw) / 2.0 / a0;
                f.b1 = -(1.0 + cosw) / a0;
                f.b2 = f.b0;
            }
            else
            {
                f.b0 = alpha / a0;
                f.b1 = 0.0;
                f.b2 = -alpha / a0;
            }
            f.a1 = -2.0 * cosw / a0;
            f.a2 = (1.0 - alpha) / a0;
            return f;
        }

        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };

    juce::AudioBuffer<float> buffer (double seconds)
    {
        juce::AudioBuffer<float> b (1, static_cast<int> (seconds * sampleRate));
        b.clear();
        return b;
    }

    void normalise (juce::AudioBuffer<float>& b, float peak = 0.9f)
    {
        const auto current = b.getMagnitude (0, 0, b.getNumSamples());
        if (current > 0.0f)
            b.applyGain (peak / current);
    }

    /** Sine with a fast falling pitch (the punch) settling on a low body, a touch of saturation and a
        short click on top: the tekno kick. */
    juce::AudioBuffer<float> kick()
    {
        auto b = buffer (0.55);
        auto* out = b.getWritePointer (0);
        Noise noise (1);
        double phase = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = i / sampleRate;
            const auto hz = 46.0 + 180.0 * std::exp (-t / 0.012) + 60.0 * std::exp (-t / 0.06);
            phase += twoPi * hz / sampleRate;
            const auto body = std::sin (phase) * std::exp (-t / 0.28);
            const auto tail = t > 0.45 ? 1.0 - (t - 0.45) / 0.1 : 1.0; // fade the last 100 ms to zero
            const auto click = noise.next() * std::exp (-t / 0.0015) * 0.3;
            out[i] = static_cast<float> (std::tanh (1.6 * body) * tail + click);
        }
        normalise (b);
        return b;
    }

    /** Six detuned square waves (metal) and noise through a high-pass: the analogue hi-hat recipe. */
    juce::AudioBuffer<float> hat (double decaySeconds, double lengthSeconds, std::uint32_t seed)
    {
        auto b = buffer (lengthSeconds);
        auto* out = b.getWritePointer (0);
        constexpr double partials[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        double phases[6] {};
        Noise noise (seed);
        auto highPass1 = Biquad::highPass (7000.0, 0.8);
        auto highPass2 = Biquad::highPass (7000.0, 0.8);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = i / sampleRate;
            double metal = 0.0;
            for (size_t p = 0; p < 6; ++p)
            {
                phases[p] += partials[p] * 2.0 / sampleRate; // 2x: an octave up, brighter metal
                phases[p] -= std::floor (phases[p]);
                metal += phases[p] < 0.5 ? 1.0 : -1.0;
            }
            const auto raw = static_cast<float> (metal / 6.0 * 0.6 + noise.next() * 0.4);
            const auto env = std::exp (-t / decaySeconds) * std::min (1.0, t / 0.0005);
            out[i] = highPass2.process (highPass1.process (raw)) * static_cast<float> (env);
        }
        normalise (b);
        return b;
    }

    /** Band-passed noise in three quick bursts, then a short room tail. */
    juce::AudioBuffer<float> clap()
    {
        auto b = buffer (0.35);
        auto* out = b.getWritePointer (0);
        Noise noise (7);
        auto band = Biquad::bandPass (1300.0, 1.4);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = i / sampleRate;
            double env = 0.0;
            for (const auto start : { 0.0, 0.011, 0.022 })
                if (t >= start)
                    env = std::max (env, std::exp (-(t - start) / 0.006));
            if (t >= 0.030)
                env = std::max (env, 0.55 * std::exp (-(t - 0.030) / 0.09));
            out[i] = band.process (noise.next()) * static_cast<float> (env);
        }
        normalise (b);
        return b;
    }
} // namespace

juce::AudioBuffer<float> make (const juce::String& name)
{
    if (name == "kick")
        return kick();
    if (name == "hat")
        return hat (0.018, 0.12, 3);
    if (name == "openhat")
        return hat (0.16, 0.6, 5);
    if (name == "clap")
        return clap();
    return {};
}
} // namespace tekno::core::dsp::builtinKit
