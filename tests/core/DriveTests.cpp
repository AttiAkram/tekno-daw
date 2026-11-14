// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// The drive is judged by measuring: harmonics added, level kept in check, aliasing kept low.

#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <juce_dsp/juce_dsp.h>

#include "core/engine/AudioEngine.h"

using namespace tekno::core;

namespace
{
constexpr double sampleRate = 48000.0;

/** A held note: every step on and sliding, same pitch, so the gate never closes. */
std::vector<float> heldNote (int midiNote, float drive, float cutoff = 1.0f, int numSamples = 48000)
{
    Session s;
    s.transport.bpm = 120.0;
    s.synth.envMod = 0.0f;
    s.synth.resonance = 0.0f;
    s.synth.cutoff = cutoff;
    s.synth.drive = drive;
    for (int i = 0; i < Pattern::defaultLength; ++i)
        s.pattern (0).setStep (i, Step { true, false, true, midiNote });
    s.transport.playing = true;

    AudioEngine engine (s);
    engine.prepare (sampleRate);
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (int pos = 0; pos < numSamples; pos += 512)
    {
        float* ch = out.data() + pos;
        engine.render (&ch, 1, std::min (512, numSamples - pos));
    }
    return out;
}

double rms (const std::vector<float>& x, size_t from)
{
    double sum = 0.0;
    for (auto i = from; i < x.size(); ++i)
        sum += static_cast<double> (x[i]) * x[i];
    return std::sqrt (sum / static_cast<double> (x.size() - from));
}

/** Energy away from the harmonics of f0, relative to the total, in dB. Aliasing lands there. */
double inharmonicDb (const std::vector<float>& x, double f0)
{
    constexpr int order = 15, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (size * 2, 0.0f);
    const auto offset = x.size() - size;
    for (int i = 0; i < size; ++i)
    {
        const auto window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / size);
        data[static_cast<size_t> (i)] = x[offset + static_cast<size_t> (i)] * window;
    }
    fft.performFrequencyOnlyForwardTransform (data.data());

    const auto binHz = sampleRate / size;
    double harmonic = 0.0, other = 0.0;
    for (int bin = 2; bin < size / 2; ++bin)
    {
        const auto hz = bin * binHz;
        const auto nearestHarmonic = std::round (hz / f0) * f0;
        const auto energy = static_cast<double> (data[static_cast<size_t> (bin)]) * data[static_cast<size_t> (bin)];
        (std::abs (hz - nearestHarmonic) < 4.0 * binHz ? harmonic : other) += energy;
    }
    return 10.0 * std::log10 (other / (harmonic + other));
}

double treble (const std::vector<float>& x)
{
    double t = 0.0, level = 0.0;
    for (size_t i = 2; i < x.size(); ++i)
    {
        t += std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]);
        level += std::abs (x[i]);
    }
    return t / level;
}
} // namespace

TEST_CASE ("drive measurements", "[core][drive][.measure]")
{
    for (auto d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const auto low = heldNote (33, d, 0.6f);
        const auto high = heldNote (84, d);
        WARN ("drive " << d << ": rms " << rms (low, 4800) << ", treble " << treble (low) << ", inharmonic C6 "
                       << inharmonicDb (high, 440.0 * std::exp2 ((84 - 69) / 12.0)) << " dB");
    }
}

TEST_CASE ("drive adds harmonics without just getting louder", "[core][drive]")
{
    const auto clean = heldNote (33, 0.0f, 0.6f);
    CHECK (treble (heldNote (33, 1.0f, 0.6f)) > treble (clean) * 2.5);
    for (auto d : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        CAPTURE (d);
        const auto ratio = rms (heldNote (33, d, 0.6f), 4800) / rms (clean, 4800);
        CHECK (ratio > 0.8);
        CHECK (ratio < 1.6);
    }
}

TEST_CASE ("full drive on a high note stays nearly free of aliasing", "[core][drive]")
{
    const auto f0 = 440.0 * std::exp2 ((84 - 69) / 12.0); // C6
    const auto clean = inharmonicDb (heldNote (84, 0.0f), f0);
    const auto driven = inharmonicDb (heldNote (84, 1.0f), f0);
    CAPTURE (clean, driven);
    CHECK (driven < -33.0);
    CHECK (driven < clean + 5.0); // ADAA: the shaper adds very little energy off the harmonics
}

TEST_CASE ("drive never exceeds full scale, even with everything up", "[core][drive]")
{
    Session s;
    s.synth.drive = 1.0f;
    s.synth.resonance = 1.0f;
    s.synth.accent = 1.0f;
    s.synth.envMod = 1.0f;
    for (int i = 0; i < Pattern::defaultLength; ++i)
        s.pattern (0).setStep (i, Step { true, true, i % 2 == 0, 33 + (i * 7) % 36 });
    s.transport.playing = true;
    AudioEngine engine (s);
    engine.prepare (sampleRate);
    std::vector<float> out (48000);
    float* ch = out.data();
    engine.render (&ch, 1, 48000);
    for (auto v : out)
    {
        REQUIRE (std::isfinite (v));
        REQUIRE (std::abs (v) <= 1.0f);
    }
}
