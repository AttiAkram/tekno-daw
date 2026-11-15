// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/engine/LiveInput.h"

namespace tekno::core
{
bool LiveInput::push (Source source, Event event) noexcept
{
    auto& q = source == Source::ui ? uiQueue : midiQueue;
    const auto scope = q.fifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
        return false;
    q.events[static_cast<size_t> (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = event;
    if (event.type == Event::Type::noteOn)
        q.notes.fetch_add (1, std::memory_order_relaxed);
    return true;
}

std::uint32_t LiveInput::notesReceived (Source source) const noexcept
{
    return (source == Source::ui ? uiQueue : midiQueue).notes.load (std::memory_order_relaxed);
}

std::optional<LiveInput::Event> LiveInput::fromMidi (const juce::MidiMessage& message, int channel, double timeMs)
{
    Event event;
    event.timeMs = timeMs;
    event.channel = static_cast<std::int8_t> (channel);
    if (message.isNoteOn())
    {
        event.type = Event::Type::noteOn;
        event.pitch = static_cast<std::uint8_t> (message.getNoteNumber());
        event.velocity = message.getVelocity();
        event.accent = message.getVelocity() >= 100;
    }
    else if (message.isNoteOff()) // includes a note-on with velocity 0
    {
        event.type = Event::Type::noteOff;
        event.pitch = static_cast<std::uint8_t> (message.getNoteNumber());
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        event.type = Event::Type::allOff;
    else
        return std::nullopt;
    return event;
}

void LiveInput::noteWait (double waitMs) noexcept
{
    const auto wait = static_cast<float> (std::max (0.0, waitMs));
    lastWaitMs.store (wait, std::memory_order_relaxed);
    auto current = maxWaitMs.load (std::memory_order_relaxed);
    while (wait > current && ! maxWaitMs.compare_exchange_weak (current, wait, std::memory_order_relaxed))
    {
    }
}
} // namespace tekno::core
