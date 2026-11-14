// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/dsp/Sampler.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/model/Step.h"

namespace tekno::core::dsp
{
void Sampler::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    attackStep = static_cast<float> (1.0 / (0.0005 * sampleRate));
    fadeStep = static_cast<float> (1.0 / (0.002 * sampleRate));
    reset();
}

void Sampler::reset() noexcept
{
    for (auto& v : voices)
        v.active = false;
}

void Sampler::setSample (const Sample* newSample) noexcept
{
    if (newSample != sample)
    {
        reset(); // never read the old sample again: it may be freed two blocks from now
        sample = newSample;
    }
}

double Sampler::decaySeconds (float decay) noexcept
{
    if (decay >= 0.999f)
        return std::numeric_limits<double>::infinity();
    return 0.015 * std::pow (400.0, static_cast<double> (std::clamp (decay, 0.0f, 1.0f))); // 15 ms .. 6 s
}

Sampler::Voice* Sampler::freeVoice() noexcept
{
    for (auto& v : voices)
        if (! v.active)
            return &v;

    // All busy (only possible with fading voices about to end): reuse the one that faded the most.
    return &*std::min_element (voices.begin(), voices.end(),
                               [] (const Voice& a, const Voice& b) { return a.fade < b.fade; });
}

void Sampler::trigger (int midiNote, bool accented, float gain) noexcept
{
    if (sample == nullptr || sample->length() < 2)
        return;

    // Over the limit: the oldest sounding voice fades out (2 ms) to make room.
    int sounding = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.fading)
        {
            ++sounding;
            if (oldest == nullptr || v.started < oldest->started)
                oldest = &v;
        }
    if (sounding >= maxVoices && oldest != nullptr)
        oldest->fading = true;

    auto& v = *freeVoice();
    const auto semitones = static_cast<double> (midiNote - Step::samplerRootNote) + current.tune;
    v.active = true;
    v.fading = false;
    v.position = 0.0;
    v.rate = std::pow (2.0, semitones / 12.0) * sample->sampleRate / sampleRate;
    v.gain = (accented ? 1.0f + current.accent : 1.0f) * gain;
    v.envelope = 1.0f;
    const auto seconds = decaySeconds (current.decay);
    v.decayFactor =
        std::isinf (seconds) ? 1.0f : static_cast<float> (std::exp (std::log (0.001) / (seconds * sampleRate)));
    v.attack = 0.0f;
    v.fade = 1.0f;
    v.started = ++triggers;
}

void Sampler::process (float& left, float& right) noexcept
{
    if (sample == nullptr)
        return;

    const auto length = sample->length();
    const auto* l = sample->audio.getReadPointer (0);
    const auto* r = sample->audio.getReadPointer (sample->audio.getNumChannels() > 1 ? 1 : 0);

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto index = static_cast<int> (v.position);
        if (index + 1 >= length)
        {
            v.active = false;
            continue;
        }
        const auto frac = static_cast<float> (v.position - index);
        const auto amp = v.gain * v.envelope * v.attack * v.fade;
        left += (l[index] + (l[index + 1] - l[index]) * frac) * amp;
        right += (r[index] + (r[index + 1] - r[index]) * frac) * amp;

        v.position += v.rate;
        v.attack = std::min (1.0f, v.attack + attackStep);
        v.envelope *= v.decayFactor;
        if (v.fading)
        {
            v.fade -= fadeStep;
            if (v.fade <= 0.0f)
                v.active = false;
        }
        if (v.envelope < 1.0e-4f) // -80 dB: done
            v.active = false;
    }
}

int Sampler::activeVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.active; }));
}
} // namespace tekno::core::dsp
