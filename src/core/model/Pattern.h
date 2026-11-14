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
/** A pattern of up to 64 steps, shared by the message thread (which edits it) and the audio thread
    (which plays it). Every step and the length are atomics, so neither side ever locks or waits. */
class Pattern
{
public:
    static constexpr int maxSteps = 64;
    static constexpr int defaultLength = 16;

    Pattern() noexcept;

    Step getStep (int index) const noexcept;
    void setStep (int index, const Step& step) noexcept;

    /** How many steps play before the pattern wraps: 1..64. Odd lengths are welcome (polymeters). */
    int getLength() const noexcept { return length.load (std::memory_order_relaxed); }
    void setLength (int newLength) noexcept;

    /** True when nothing would be saved for it: default length and no step on. */
    bool isEmpty() const noexcept;

    /** Back to a new, empty pattern. */
    void clear() noexcept;

    static constexpr bool isValidIndex (int index) noexcept { return index >= 0 && index < maxSteps; }

private:
    std::array<std::atomic<std::uint32_t>, maxSteps> steps;
    std::atomic<int> length { defaultLength };

    static_assert (std::atomic<std::uint32_t>::is_always_lock_free);
};
} // namespace tekno::core
