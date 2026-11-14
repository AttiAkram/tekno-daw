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
/** One step of a lane. Accent, slide and tie are part of the step, not global settings: they are what
    makes an acid line move. A sampler uses `on`, `accent` and `note` (its pitch) and ignores the rest. */
struct Step
{
    static constexpr int minNote = 24;         // C1
    static constexpr int maxNote = 96;         // C7
    static constexpr int samplerRootNote = 60; // C4: a sampler plays its sample at the original pitch here

    bool on = false;
    bool accent = false;
    bool slide = false; // keep the gate open and glide into the next step
    int note = 45;      // MIDI note number, A2
    bool tie = false;   // don't play a new note: hold the previous one through this step
    int velocity = 100; // 0..127, the note's loudness (Note::defaultVelocity)

    /** Packs the step into 32 bits, so a whole step can be stored in one lock-free atomic. */
    std::uint32_t pack() const noexcept;
    static Step unpack (std::uint32_t bits) noexcept;

    friend bool operator== (const Step&, const Step&) = default;
};
} // namespace tekno::core
