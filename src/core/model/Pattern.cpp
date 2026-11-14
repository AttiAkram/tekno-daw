// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/Pattern.h"

#include <algorithm>

namespace tekno::core
{
// --- Step and Note packing ------------------------------------------------------------------------

std::uint32_t Step::pack() const noexcept
{
    const auto clampedNote = static_cast<std::uint32_t> (std::clamp (note, minNote, maxNote));
    const auto clampedVelocity = static_cast<std::uint32_t> (std::clamp (velocity, 0, 127));
    return (on ? 1u : 0u) | (accent ? 2u : 0u) | (slide ? 4u : 0u) | (tie ? 8u : 0u) | (clampedNote << 8) |
           (clampedVelocity << 16);
}

Step Step::unpack (std::uint32_t bits) noexcept
{
    Step s;
    s.on = (bits & 1u) != 0;
    s.accent = (bits & 2u) != 0;
    s.slide = (bits & 4u) != 0;
    s.tie = (bits & 8u) != 0;
    s.note = static_cast<int> ((bits >> 8) & 0x7fu);
    s.velocity = static_cast<int> ((bits >> 16) & 0x7fu);
    return s;
}

std::uint64_t Note::pack() const noexcept
{
    const auto s = static_cast<std::uint64_t> (std::clamp (start, 0, maxTicks));
    const auto l = static_cast<std::uint64_t> (std::clamp (length, 1, maxTicks));
    const auto p = static_cast<std::uint64_t> (std::clamp (pitch, 0, 127));
    const auto v = static_cast<std::uint64_t> (std::clamp (velocity, 0, 127));
    return s | (l << 16) | (p << 32) | (accent ? 1ull << 39 : 0) | (slide ? 1ull << 40 : 0) | (v << 41) |
           (1ull << 63);
}

Note Note::unpack (std::uint64_t bits) noexcept
{
    Note n;
    n.start = static_cast<int> (bits & 0xffffu);
    n.length = static_cast<int> ((bits >> 16) & 0xffffu);
    n.pitch = static_cast<int> ((bits >> 32) & 0x7fu);
    n.accent = ((bits >> 39) & 1u) != 0;
    n.slide = ((bits >> 40) & 1u) != 0;
    n.velocity = static_cast<int> ((bits >> 41) & 0x7fu);
    return n;
}

// --- Lane -----------------------------------------------------------------------------------------

std::vector<Note> Lane::notes() const
{
    std::vector<Note> list;
    for (int i = 0; i < maxNotes; ++i)
        if (const auto bits = slot (i); bits != 0)
            list.push_back (Note::unpack (bits));
    std::sort (list.begin(), list.end(), earlier);
    return list;
}

bool Lane::setNotes (const std::vector<Note>& wanted)
{
    if (wanted.size() > static_cast<size_t> (maxNotes))
        return false;

    constexpr auto relaxed = std::memory_order_relaxed;
    std::array<bool, maxNotes> slotKept {};
    std::vector<bool> placed (wanted.size(), false);

    // 1. Notes already there stay in their slot, untouched.
    for (int i = 0; i < maxNotes; ++i)
    {
        const auto bits = slot (i);
        if (bits == 0)
            continue;
        for (size_t w = 0; w < wanted.size(); ++w)
            if (! placed[w] && wanted[w].pack() == bits)
            {
                placed[w] = slotKept[static_cast<size_t> (i)] = true;
                break;
            }
    }
    // 2. A changed note that starts where an old one did takes its slot: one store, no gap.
    for (size_t w = 0; w < wanted.size(); ++w)
    {
        if (placed[w])
            continue;
        for (int i = 0; i < maxNotes; ++i)
        {
            const auto bits = slot (i);
            if (bits != 0 && ! slotKept[static_cast<size_t> (i)] && Note::unpack (bits).start == wanted[w].start)
            {
                slots[static_cast<size_t> (i)].store (wanted[w].pack(), relaxed);
                placed[w] = slotKept[static_cast<size_t> (i)] = true;
                break;
            }
        }
    }
    // 3. Old notes nobody wants go; 4. new ones fill free slots.
    for (int i = 0; i < maxNotes; ++i)
        if (! slotKept[static_cast<size_t> (i)])
            slots[static_cast<size_t> (i)].store (0, relaxed);
    for (size_t w = 0, i = 0; w < wanted.size(); ++w)
    {
        if (placed[w])
            continue;
        while (slotKept[i])
            ++i;
        slots[i].store (wanted[w].pack(), relaxed);
        slotKept[i] = true;
    }
    return true;
}

bool Lane::isBlank() const noexcept
{
    return std::all_of (slots.begin(), slots.end(),
                        [] (const auto& s) { return s.load (std::memory_order_relaxed) == 0; });
}

void Lane::clear() noexcept
{
    for (auto& s : slots)
        s.store (0, std::memory_order_relaxed);
}

// --- steps <-> notes ------------------------------------------------------------------------------

namespace steps
{
    namespace
    {
        constexpr int tps = Note::ticksPerStep;
    }

    bool representable (const std::vector<Note>& notes)
    {
        return std::all_of (notes.begin(), notes.end(), [] (const Note& n) { return n.onGrid(); });
    }

    std::array<Step, count> fromNotes (const std::vector<Note>& notes, int patternLength)
    {
        const auto length = std::clamp (patternLength, 1, count);
        std::array<Step, count> out {};
        std::array<bool, count> isStart {};

        auto sorted = notes;
        std::sort (sorted.begin(), sorted.end(), earlier);
        for (const auto& n : sorted)
            if (n.onGrid() && n.start / tps < count)
                isStart[static_cast<size_t> (n.start / tps)] = true;

        for (const auto& n : sorted)
        {
            if (! n.onGrid() || n.start / tps >= count)
                continue;
            const auto first = n.start / tps;
            const auto stepsLong = n.length / tps;
            out[static_cast<size_t> (first)] = Step { true, n.accent, stepsLong == 1 && n.slide, n.pitch, false, n.velocity };

            for (int j = 1; j < stepsLong; ++j)
            {
                auto index = first + j;
                if (first < length && index >= length)
                    index -= length; // a note running past the end goes on at the start of the next round
                if (index >= count || index == first || isStart[static_cast<size_t> (index)])
                    break;
                out[static_cast<size_t> (index)] =
                    Step { true, false, j == stepsLong - 1 && n.slide, n.pitch, true, n.velocity };
            }
        }
        return out;
    }

    std::vector<Note> toNotes (const std::array<Step, count>& in, int patternLength,
                               const std::vector<Note>& offGridNotesToKeep)
    {
        const auto length = std::clamp (patternLength, 1, count);
        std::vector<Note> notes;

        // Ties at the start that continue the last step's note, from the round before.
        int leading = 0;
        if (length > 1 && in[static_cast<size_t> (length - 1)].on)
            while (leading < length - 1 && in[static_cast<size_t> (leading)].on &&
                   in[static_cast<size_t> (leading)].tie)
                ++leading;

        int open = -1; // the note the previous step belongs to
        for (int i = 0; i < count; ++i)
        {
            const auto& s = in[static_cast<size_t> (i)];
            if (! s.on)
            {
                open = -1;
                continue;
            }
            if (i < leading)
                continue;
            if (s.tie && open >= 0 && notes[static_cast<size_t> (open)].end() == i * tps)
            {
                notes[static_cast<size_t> (open)].length += tps;
                notes[static_cast<size_t> (open)].slide = s.slide;
                continue;
            }
            notes.push_back ({ i * tps, tps, s.note, s.accent, s.slide, s.velocity });
            open = static_cast<int> (notes.size()) - 1;
        }

        if (leading > 0)
            for (auto& n : notes)
                if (n.start < length * tps && n.end() >= length * tps)
                {
                    n.length += leading * tps;
                    n.slide = in[static_cast<size_t> (leading - 1)].slide;
                    break;
                }

        for (const auto& n : offGridNotesToKeep)
            if (! n.onGrid())
                notes.push_back (n);
        std::sort (notes.begin(), notes.end(), earlier);
        return notes;
    }
} // namespace steps

// --- Pattern --------------------------------------------------------------------------------------

Step Pattern::getStep (int channel, int index) const
{
    if (! isValidIndex (index))
        return {};
    return steps::fromNotes (lane (channel).notes(), getLength())[static_cast<size_t> (index)];
}

void Pattern::setStep (int channel, int index, const Step& step)
{
    if (! isValidIndex (index))
        return;
    auto& l = lane (channel);
    const auto notes = l.notes();
    auto grid = steps::fromNotes (notes, getLength());
    grid[static_cast<size_t> (index)] = step;
    l.setNotes (steps::toNotes (grid, getLength(), notes));
}

void Pattern::clear() noexcept
{
    for (auto& l : lanes)
        l.clear();
    length.store (defaultLength, std::memory_order_relaxed);
}

void Pattern::setLength (int newLength) noexcept
{
    length.store (std::clamp (newLength, 1, maxSteps), std::memory_order_relaxed);
}

bool Pattern::isEmpty() const noexcept
{
    return getLength() == defaultLength &&
           std::all_of (lanes.begin(), lanes.end(), [] (const Lane& l) { return l.isBlank(); });
}
} // namespace tekno::core
