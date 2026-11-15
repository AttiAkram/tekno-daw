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
#include <optional>

#include <juce_audio_basics/juce_audio_basics.h>

namespace tekno::core
{
/** Notes played live (the PC keyboard, a MIDI keyboard, clicks on the on-screen piano) on their way to
    the audio thread. They are events, not commands: they change nothing in the project.

    Two single-producer queues, so nobody ever locks: one filled by the message thread, one by the MIDI
    thread. The engine that owns this empties both at the start of every block. Each event carries the
    time it was made, so the engine can say how long notes waited: the measured part of the latency. */
class LiveInput
{
public:
    enum class Source
    {
        ui,   // the message thread
        midi, // the MIDI input thread
    };

    struct Event
    {
        enum class Type : std::uint8_t
        {
            noteOn,
            noteOff,
            allOff, // panic: every live note on every channel
        };
        Type type = Type::noteOn;
        std::int8_t channel = 0; // the channel to play (resolved by whoever pushes it)
        std::uint8_t pitch = 60, velocity = 100;
        bool accent = false;
        bool shortGate = false; // a 303 step's length, whatever happens to the key
        double timeMs = 0.0;    // juce::Time::getMillisecondCounterHiRes() when it was made
    };

    /** A MIDI message as a live event for `channel`, or nothing for messages that aren't notes. Omni:
        the MIDI channel doesn't matter. A velocity of 100 or more is an accent, as on 303 clones. */
    static std::optional<Event> fromMidi (const juce::MidiMessage& message, int channel, double timeMs);

    /** Producer side: each Source from its own one thread only. False when the queue is full (256
        events waiting: the audio has stopped), and the event is dropped. */
    bool push (Source source, Event event) noexcept;

    /** Audio side: every waiting event, oldest first, from both queues. */
    template <typename Handler> void drain (Handler&& handle) noexcept
    {
        for (auto* q : { &uiQueue, &midiQueue })
        {
            const auto scope = q->fifo.read (q->fifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i)
                handle (q->events[static_cast<size_t> (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i)
                handle (q->events[static_cast<size_t> (scope.startIndex2 + i)]);
        }
    }

    /** Written by the audio thread: how long the last note waited between being made and being played,
        and the longest wait since reset, in milliseconds. Read by the UI. */
    std::atomic<float> lastWaitMs { 0.0f }, maxWaitMs { 0.0f };
    void noteWait (double waitMs) noexcept;

    /** Notes a source got into the queue since the start: proof that a keyboard really arrives. */
    std::uint32_t notesReceived (Source source) const noexcept;

    static constexpr int capacity = 256;

private:
    struct Queue
    {
        juce::AbstractFifo fifo { capacity };
        std::array<Event, capacity> events {};
        std::atomic<std::uint32_t> notes { 0 }; // written by the producer only
    };
    Queue uiQueue, midiQueue;
};
} // namespace tekno::core
