// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/dsp/SubVoice.h"

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

namespace tekno::core::dsp
{
namespace
{
    constexpr double twoPi = 6.283185307179586;
    constexpr float attackSeconds = 0.002f;
    constexpr float releaseSeconds = 0.015f;
    constexpr float smoothingSeconds = 0.01f;
    constexpr float accentGain = 1.41f; // +3 dB
    constexpr float outputGain = 0.7f;

    float coefficientFor (double seconds, double sampleRate) noexcept
    {
        return static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    float decayFor (double seconds, double sampleRate) noexcept
    {
        return static_cast<float> (std::exp (-1.0 / (seconds * sampleRate)));
    }
} // namespace

float SubVoice::decaySeconds (float normalised) noexcept
{
    return 0.08f * std::exp2 (std::clamp (normalised, 0.0f, 1.0f) * 5.6f);
}

bool SubVoice::decayHolds (float normalised) noexcept
{
    return normalised >= 0.99f;
}

void SubVoice::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    smoothing = coefficientFor (smoothingSeconds, sampleRate);
    glideSeconds = target.glide;
    glide = coefficientFor (std::max (0.001f, glideSeconds), sampleRate);
    attack = coefficientFor (attackSeconds, sampleRate);
    release = coefficientFor (releaseSeconds, sampleRate);
    punchDecay = decayFor (punchSeconds, sampleRate);
    reset();
}

void SubVoice::reset() noexcept
{
    smoothed = target;
    phase = 0.0;
    gate = sliding = accentedNote = false;
    punchEnv = amp = level = 0.0f;
    levelDecay = 1.0f;
}

void SubVoice::noteOn (int midiNote, bool accented) noexcept
{
    pitch = targetPitch = static_cast<float> (midiNote);
    sliding = false;
    gate = true;
    accentedNote = accented;
    punchEnv = 1.0f;
    level = 1.0f;
    levelDecay = decayHolds (target.decay) ? 1.0f : decayFor (decaySeconds (target.decay), sampleRate);
    if (amp < 1.0e-3f)
        phase = 0.0; // from silence, start on a zero crossing: every attack the same, no click
}

void SubVoice::glideTo (int midiNote) noexcept
{
    targetPitch = static_cast<float> (midiNote);
    sliding = true;
    gate = true;
}

void SubVoice::gateOff() noexcept
{
    gate = false;
}

float SubVoice::process() noexcept
{
    smoothed.drive += (target.drive - smoothed.drive) * smoothing;
    smoothed.punch += (target.punch - smoothed.punch) * smoothing;

    if (! juce::exactlyEqual (target.glide, glideSeconds))
    {
        glideSeconds = target.glide;
        glide = coefficientFor (std::max (0.001f, glideSeconds), sampleRate);
    }
    if (sliding)
        pitch += (targetPitch - pitch) * glide;

    // The punch: the note starts above itself and falls into place. Accents punch harder.
    const auto punch = std::clamp (smoothed.punch, 0.0f, 1.0f) * (accentedNote ? 1.25f : 1.0f);
    const auto now = static_cast<double> (pitch + punchEnv * punch * punchSemitones);
    punchEnv *= punchDecay;
    phase += 440.0 * std::exp2 ((now - 69.0) / 12.0) / sampleRate;
    if (phase >= 1.0)
        phase -= std::floor (phase);
    const auto sine = static_cast<float> (std::sin (twoPi * phase));

    // Saturation: up to 10x into tanh, scaled so a full sine still peaks at 1. The wave gets squarer,
    // and the odd harmonics it gains are what a laptop speaker can play. At drive 0 the gain is so small
    // that tanh is a straight line: a pure sine.
    const auto drive = std::clamp (smoothed.drive, 0.0f, 1.0f);
    const auto preGain = 0.05f + 9.95f * drive * drive;
    const auto shaped = std::tanh (sine * preGain) / std::tanh (preGain);

    if (gate)
        level *= levelDecay;
    amp += ((gate ? level : 0.0f) - amp) * (gate ? attack : release);
    return shaped * amp * outputGain * (accentedNote ? accentGain : 1.0f);
}
} // namespace tekno::core::dsp
