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
#include <vector>

#include "core/model/Note.h"
#include "core/model/Step.h"

namespace tekno::core
{
/** The notes of one channel inside one pattern: up to 128, each in a lock-free atomic slot, so the
    message thread edits and the audio thread plays without ever waiting for each other. Slots are in
    no particular order; an empty slot holds 0. */
class Lane
{
public:
    static constexpr int maxNotes = 128;

    Lane() noexcept { clear(); }

    /** Every note, sorted (earlier()). Message thread: it allocates. */
    std::vector<Note> notes() const;

    /** Makes the lane hold exactly `notes`, touching only the slots that change, so a playing lane
        never sees more than the edit. False (and nothing changed) when there are more than maxNotes. */
    bool setNotes (const std::vector<Note>& notes);

    /** For the audio thread: the raw slot, 0 when empty. */
    std::uint64_t slot (int index) const noexcept
    {
        return slots[static_cast<size_t> (index)].load (std::memory_order_relaxed);
    }

    bool isBlank() const noexcept;
    void clear() noexcept;

private:
    std::array<std::atomic<std::uint64_t>, maxNotes> slots;

    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
};

/** The step sequencer's view of a lane, both ways. A step is on when a note starts on it; the steps a
    longer note covers are ties, wrapping to the start of the pattern when the note runs past its end.
    Notes off the grid are not steps: toSteps() skips them and fromSteps() keeps them as they are. */
namespace steps
{
    inline constexpr int count = 64;

    std::array<Step, count> fromNotes (const std::vector<Note>& notes, int patternLength);
    std::vector<Note> toNotes (const std::array<Step, count>& steps, int patternLength,
                               const std::vector<Note>& offGridNotesToKeep = {});

    /** True when every note sits on the grid, so the step sequencer shows the lane completely. */
    bool representable (const std::vector<Note>& notes);
} // namespace steps

/** A pattern: one lane per channel and one length for all of them, so switching pattern switches a
    whole section of the track, like in FL Studio. */
class Pattern
{
public:
    static constexpr int maxSteps = steps::count;
    static constexpr int maxLanes = 16; // one per channel: Session::maxChannels
    static constexpr int defaultLength = 16;

    Pattern() noexcept { clear(); }

    Lane& lane (int channel) noexcept { return lanes[laneIndex (channel)]; }
    const Lane& lane (int channel) const noexcept { return lanes[laneIndex (channel)]; }

    /** The step sequencer's view (see steps::). Message thread. */
    Step getStep (int channel, int index) const;
    void setStep (int channel, int index, const Step& step);

    /** How many steps play before the pattern wraps: 1..64. Odd lengths are welcome. */
    int getLength() const noexcept { return length.load (std::memory_order_relaxed); }
    int lengthInTicks() const noexcept { return getLength() * Note::ticksPerStep; }
    void setLength (int newLength) noexcept;

    /** True when nothing would be saved for it: default length and no notes in any lane. */
    bool isEmpty() const noexcept;

    /** Back to a new, empty pattern. */
    void clear() noexcept;

    static constexpr bool isValidIndex (int index) noexcept { return index >= 0 && index < maxSteps; }

private:
    static size_t laneIndex (int channel) noexcept
    {
        return static_cast<size_t> (channel < 0 ? 0 : (channel >= maxLanes ? maxLanes - 1 : channel));
    }

    std::array<Lane, maxLanes> lanes;
    std::atomic<int> length { defaultLength };
};
} // namespace tekno::core
