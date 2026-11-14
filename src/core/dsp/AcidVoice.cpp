// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/dsp/AcidVoice.h"

namespace tekno::core::dsp
{
namespace
{
    constexpr float accentDecaySeconds = 0.2f; // accented notes always snap shut quickly, like the 303
    constexpr float slideSeconds = 0.035f;     // fixed-time glide, independent of the interval
    constexpr float attackSeconds = 0.002f;
    constexpr float releaseSeconds = 0.008f;
    constexpr float holdSeconds = 3.0f; // slow fall of the amp while the gate is held
    constexpr float smoothingSeconds = 0.01f;

    float coefficientFor (double seconds, double sampleRate) noexcept
    {
        return static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    float decayFor (double seconds, double sampleRate) noexcept
    {
        return static_cast<float> (std::exp (-1.0 / (seconds * sampleRate)));
    }

    // Removes the aliasing of a hard edge around phase 0 (polynomial band-limited step).
    // log(cosh(x)), the antiderivative of tanh, written so it can't overflow for large |x|.
    double logCosh (double x) noexcept
    {
        const auto a = std::abs (x);
        constexpr double ln2 = 0.69314718055994530942;
        return a + std::log1p (std::exp (-2.0 * a)) - ln2;
    }

    double polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }
} // namespace

float AcidVoice::cutoffHz (float normalised) noexcept
{
    return 40.0f * std::exp2 (std::clamp (normalised, 0.0f, 1.0f) * 8.5f);
}

float AcidVoice::decaySeconds (float normalised) noexcept
{
    return 0.15f * std::exp2 (std::clamp (normalised, 0.0f, 1.0f) * 4.06f);
}

void AcidVoice::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    filter.setSampleRate (sampleRate);
    smoothing = coefficientFor (smoothingSeconds, sampleRate);
    glide = coefficientFor (slideSeconds, sampleRate);
    attack = coefficientFor (attackSeconds, sampleRate);
    release = coefficientFor (releaseSeconds, sampleRate);
    ampHoldDecay = decayFor (holdSeconds, sampleRate);
    reset();
}

void AcidVoice::reset() noexcept
{
    filter.reset();
    smoothed = target;
    phase = 0.0;
    gate = sliding = accentedNote = false;
    filterEnv = amp = ampHold = 0.0f;
    shaperPrevious = 0.0;
    toneState = 0.0f;
}

float AcidVoice::shape (double input) noexcept
{
    // ADAA1: the average of tanh between the previous and the current input, instead of tanh at a
    // single point. Most of the aliasing a hard waveshaper creates simply never happens.
    const auto delta = input - shaperPrevious;
    const auto out = std::abs (delta) < 1.0e-5 ? std::tanh (0.5 * (input + shaperPrevious))
                                               : (logCosh (input) - logCosh (shaperPrevious)) / delta;
    shaperPrevious = input;
    return static_cast<float> (out);
}

void AcidVoice::noteOn (int midiNote, bool accented) noexcept
{
    pitch = targetPitch = static_cast<float> (midiNote);
    sliding = false;
    gate = true;
    accentedNote = accented;
    filterEnv = 1.0f;
    filterEnvDecay = decayFor (accented ? accentDecaySeconds : decaySeconds (target.decay), sampleRate);
    ampHold = 1.0f;
}

void AcidVoice::glideTo (int midiNote) noexcept
{
    targetPitch = static_cast<float> (midiNote);
    sliding = true;
    gate = true;
}

void AcidVoice::gateOff() noexcept
{
    gate = false;
}

float AcidVoice::process() noexcept
{
    // Smooth the knobs.
    smoothed.cutoff += (target.cutoff - smoothed.cutoff) * smoothing;
    smoothed.resonance += (target.resonance - smoothed.resonance) * smoothing;
    smoothed.envMod += (target.envMod - smoothed.envMod) * smoothing;
    smoothed.accent += (target.accent - smoothed.accent) * smoothing;
    smoothed.drive += (target.drive - smoothed.drive) * smoothing;

    // Pitch, with slide.
    if (sliding)
        pitch += (targetPitch - pitch) * glide;
    const auto hz = 440.0 * std::exp2 ((static_cast<double> (pitch) - 69.0) / 12.0);
    const auto dt = hz / sampleRate;

    // Oscillator.
    phase += dt;
    if (phase >= 1.0)
        phase -= 1.0;

    double osc;
    if (target.square)
    {
        osc = (phase < 0.5 ? 1.0 : -1.0) + polyBlep (phase, dt) - polyBlep (std::fmod (phase + 0.5, 1.0), dt);
    }
    else
    {
        osc = 2.0 * phase - 1.0 - polyBlep (phase, dt);
    }

    // Filter: the envelope sweeps the cutoff up by octaves; accent pushes it further and harder.
    filterEnv *= filterEnvDecay;
    const auto accentAmount = accentedNote ? smoothed.accent : 0.0f;
    const auto sweepOctaves = filterEnv * (smoothed.envMod * 4.5f + accentAmount * 2.0f);
    const auto fc = cutoffHz (smoothed.cutoff) * std::exp2 (sweepOctaves);
    const auto k = std::min (3.95f, smoothed.resonance * 3.6f + accentAmount * 0.3f);
    auto y = filter.process (static_cast<float> (osc), fc, k);

    // Amp.
    if (gate)
        ampHold *= ampHoldDecay;
    const auto ampTarget = gate ? ampHold : 0.0f;
    amp += (ampTarget - amp) * (gate ? attack : release);

    const auto gain = 0.45f * (1.0f + accentAmount * 0.9f);

    // Drive: up to ~31x into the shaper (quadratic taper: the first half of the knob stays musical),
    // level brought back down so turning it up doesn't just make it louder. Accents hit it harder.
    const auto drive = smoothed.drive;
    const auto preGain = 1.0f + 30.0f * drive * drive;
    const auto shaped = shape (static_cast<double> (y * amp * gain * preGain));

    // Tone: one-pole lowpass from ~16 kHz (clean) down to ~5 kHz (full drive).
    const auto toneHz = 16000.0f - 11000.0f * drive;
    const auto toneCoeff =
        1.0f - std::exp (-juce::MathConstants<float>::twoPi * toneHz / static_cast<float> (sampleRate));
    toneState += (shaped - toneState) * toneCoeff;

    // Fitted on measurements (DriveTests): saturation adds level fast, then flattens out.
    const auto makeup = 1.0f / (1.0f + 2.8f * (1.0f - std::exp (-4.0f * drive)));
    return toneState * makeup; // |shaped| <= 1, so the output never exceeds 1.0
}
} // namespace tekno::core::dsp
