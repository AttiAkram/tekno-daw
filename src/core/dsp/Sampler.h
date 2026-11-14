// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>
#include <cstdint>

#include "core/model/Channel.h"
#include "core/model/Sample.h"

namespace tekno::core::dsp
{
/** The drum sampler of one channel: one-shot playback of a Sample, 8 voices, pitch per step
    with linear interpolation, an exponential decay, accent as extra level.

    Real-time safe: no allocation, no locks. The sample is set once per block with setSample(); when it
    changes, every voice playing the old one stops at once, so the old sample is never read again. */
class Sampler
{
public:
    static constexpr int maxVoices = 8;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept; // silence now

    void setSample (const Sample* sample) noexcept;
    void setSettings (const SamplerSettings& settings) noexcept { current = settings; }

    /** `gain` scales this hit (the note's velocity): 1 = as the sample is. */
    void trigger (int midiNote, bool accented, float gain = 1.0f) noexcept;

    /** Adds this sample's output to left and right. */
    void process (float& left, float& right) noexcept;

    int activeVoices() const noexcept;

    /** Seconds for the tail to fall by 60 dB at a given decay (0..1); infinite at 1. */
    static double decaySeconds (float decay) noexcept;

private:
    // Two spare voices: a stolen voice fades out for 2 ms while the new one starts.
    static constexpr int poolSize = maxVoices + 2;

    struct Voice
    {
        bool active = false;
        bool fading = false;
        double position = 0.0, rate = 1.0;
        float gain = 1.0f;
        float envelope = 1.0f, decayFactor = 1.0f;
        float attack = 0.0f; // ramps 0 -> 1 in half a millisecond: no click at the start
        float fade = 1.0f;   // 1 -> 0 while fading out after being stolen
        std::uint64_t started = 0;
    };

    Voice* freeVoice() noexcept;

    std::array<Voice, poolSize> voices;
    const Sample* sample = nullptr;
    SamplerSettings current;
    double sampleRate = 48000.0;
    float attackStep = 1.0f, fadeStep = 1.0f;
    std::uint64_t triggers = 0;
};
} // namespace tekno::core::dsp
