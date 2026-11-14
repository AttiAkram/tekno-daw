// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "core/model/Step.h"

namespace tekno::core
{
/** The steps of one channel inside one pattern: up to 64, each a lock-free atomic, so the message
    thread edits and the audio thread plays without ever waiting for each other. */
class Lane
{
public:
    static constexpr int maxSteps = 64;

    Lane() noexcept { clear(); }

    Step getStep (int index) const noexcept;
    void setStep (int index, const Step& step) noexcept;

    /** True when every step is untouched (`Step {}`): nothing to save. */
    bool isBlank() const noexcept;
    void clear() noexcept;

    static constexpr bool isValidIndex (int index) noexcept { return index >= 0 && index < maxSteps; }

private:
    std::array<std::atomic<std::uint32_t>, maxSteps> steps;

    static_assert (std::atomic<std::uint32_t>::is_always_lock_free);
};

/** A pattern: one lane per channel and one length for all of them, so switching pattern switches a
    whole section of the track, like in FL Studio. */
class Pattern
{
public:
    static constexpr int maxSteps = Lane::maxSteps;
    static constexpr int maxLanes = 16; // one per channel: Session::maxChannels
    static constexpr int defaultLength = 16;

    Pattern() noexcept { clear(); }

    Lane& lane (int channel) noexcept { return lanes[laneIndex (channel)]; }
    const Lane& lane (int channel) const noexcept { return lanes[laneIndex (channel)]; }

    /** How many steps play before the pattern wraps: 1..64. Odd lengths are welcome. */
    int getLength() const noexcept { return length.load (std::memory_order_relaxed); }
    void setLength (int newLength) noexcept;

    /** True when nothing would be saved for it: default length and no step on in any lane. */
    bool isEmpty() const noexcept;

    /** Back to a new, empty pattern. */
    void clear() noexcept;

    static constexpr bool isValidIndex (int index) noexcept { return Lane::isValidIndex (index); }

private:
    static size_t laneIndex (int channel) noexcept
    {
        return static_cast<size_t> (channel < 0 ? 0 : (channel >= maxLanes ? maxLanes - 1 : channel));
    }

    std::array<Lane, maxLanes> lanes;
    std::atomic<int> length { defaultLength };
};
} // namespace tekno::core
