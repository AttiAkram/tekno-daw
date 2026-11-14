// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <cstdint>

namespace tekno::core
{
/** A note in a lane. Time is in ticks, 24 per step (96 per beat), so 1/16, 1/32 and 1/16 triplets
    are all whole numbers. A step of the step sequencer is a note one step long on the grid. */
struct Note
{
    static constexpr int ticksPerStep = 24;
    static constexpr int maxTicks = 0xffff; // start and length are 16 bits
    static constexpr int defaultVelocity = 100;

    int start = 0; // ticks from the start of the pattern
    int length = ticksPerStep;
    int pitch = 45; // MIDI note number
    bool accent = false;
    bool slide = false;             // glide into a note that starts where this one ends, keeping the gate open
    int velocity = defaultVelocity; // 0..127: loudness. 100 is unity gain

    /** Linear gain of a velocity: velocity / 100, so the default changes nothing. */
    static float velocityGain (int velocity) noexcept
    {
        return static_cast<float> (velocity < 0 ? 0 : (velocity > 127 ? 127 : velocity)) / 100.0f;
    }

    int end() const noexcept { return start + length; }
    bool onGrid() const noexcept { return start % ticksPerStep == 0 && length % ticksPerStep == 0; }

    /** 64 bits, so a whole note fits in one lock-free atomic. 0 never comes out: it means "empty". */
    std::uint64_t pack() const noexcept;
    static Note unpack (std::uint64_t bits) noexcept;

    friend bool operator== (const Note&, const Note&) = default;
};

/** Orders notes by start, then pitch: how files list them and how lanes return them. */
inline bool earlier (const Note& a, const Note& b) noexcept
{
    return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
}
} // namespace tekno::core
