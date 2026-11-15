// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// Playing live: the queues between the threads, MIDI messages as live events, and what the engine
// does with them. The engine is measured offline, as in AudioEngineTests.

#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "TestSessions.h"
#include "core/engine/AudioEngine.h"
#include "core/engine/LiveInput.h"

using namespace tekno::core;
using Type = LiveInput::Event::Type;
using Source = LiveInput::Source;

namespace
{
constexpr double rate = 48000.0;

LiveInput::Event noteOn (int pitch, int channel = 0, bool shortGate = false)
{
    LiveInput::Event e;
    e.type = Type::noteOn;
    e.channel = static_cast<std::int8_t> (channel);
    e.pitch = static_cast<std::uint8_t> (pitch);
    e.shortGate = shortGate;
    e.timeMs = juce::Time::getMillisecondCounterHiRes();
    return e;
}

LiveInput::Event noteOff (int pitch, int channel = 0)
{
    auto e = noteOn (pitch, channel);
    e.type = Type::noteOff;
    return e;
}

LiveInput::Event panic()
{
    LiveInput::Event e;
    e.type = Type::allOff;
    return e;
}

std::string describe (const LiveInput::Event& e)
{
    return (e.type == Type::noteOn ? "on " : e.type == Type::noteOff ? "off " : "all off ") + std::to_string (e.pitch);
}

/** An engine rendered a little at a time, in device-sized blocks, with live notes in between. */
struct Rig
{
    explicit Rig (Session& s)
        : engine (s)
    {
        engine.prepare (rate);
    }

    void push (const LiveInput::Event& e) { REQUIRE (engine.liveInput().push (Source::ui, e)); }

    std::vector<float> play (double seconds)
    {
        std::vector<float> out (static_cast<size_t> (seconds * rate));
        for (size_t pos = 0; pos < out.size(); pos += 256)
        {
            float* channel = out.data() + pos;
            engine.render (&channel, 1, static_cast<int> (std::min<size_t> (256, out.size() - pos)));
        }
        return out;
    }

    AudioEngine engine;
};

/** The acid channel alone, as a plain saw (filter open, no resonance), so its pitch can be counted. */
void plainSaw (Session& s)
{
    tekno::test::acidOnly (s);
    auto acid = s.channel (0).acid.load();
    acid.cutoff = 1.0f;
    acid.resonance = 0.0f;
    acid.envMod = 0.0f;
    s.channel (0).acid.store (acid);
}

/** Hz, from the saw's falling edges (one per period) in the last `seconds` of x. */
double pitchAtEnd (const std::vector<float>& x, double seconds = 0.1)
{
    const auto from = x.size() - static_cast<size_t> (seconds * rate);
    int edges = 0;
    for (auto i = from + 1; i < x.size(); ++i)
        if (x[i - 1] > 0.0f && x[i] <= 0.0f)
            ++edges;
    return edges / seconds;
}

float peakBetween (const std::vector<float>& x, double fromSeconds, double toSeconds = 1e9)
{
    float p = 0.0f;
    const auto to = std::min (x.size(), static_cast<size_t> (std::min (toSeconds * rate, 1e9)));
    for (auto i = static_cast<size_t> (fromSeconds * rate); i < to; ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

bool near (double hz, double expected)
{
    return std::abs (hz - expected) <= 15.0; // whole periods in 0.1 s: 10 Hz steps
}
} // namespace

// --- the queues ---------------------------------------------------------------------------------------

TEST_CASE ("live events come out in the order they went in, the screen's first", "[core][live]")
{
    LiveInput live;
    REQUIRE (live.push (Source::midi, noteOn (50)));
    REQUIRE (live.push (Source::ui, noteOn (60)));
    REQUIRE (live.push (Source::ui, noteOff (60)));
    std::vector<std::string> seen;
    live.drain ([&] (const LiveInput::Event& e) { seen.push_back (describe (e)); });
    CHECK (seen == std::vector<std::string> { "on 60", "off 60", "on 50" });

    seen.clear();
    live.drain ([&] (const LiveInput::Event& e) { seen.push_back (describe (e)); });
    CHECK (seen.empty()); // drained means gone
}

TEST_CASE ("a full queue refuses notes instead of waiting", "[core][live]")
{
    LiveInput live;
    int accepted = 0;
    while (accepted < 1000 && live.push (Source::ui, noteOn (60)))
        ++accepted;
    CHECK (accepted == LiveInput::capacity - 1);                                      // a FIFO keeps one slot free
    CHECK (live.notesReceived (Source::ui) == static_cast<std::uint32_t> (accepted)); // the refused one isn't
    CHECK (live.push (Source::midi, noteOn (60)));                                    // each source has its own

    int drained = 0;
    live.drain ([&] (const LiveInput::Event&) { ++drained; });
    CHECK (drained == accepted + 1);
    CHECK (live.push (Source::ui, noteOn (60))); // room again
}

TEST_CASE ("notes received are counted per source, key-downs only", "[core][live]")
{
    LiveInput live;
    live.push (Source::ui, noteOn (60));
    live.push (Source::ui, noteOff (60));
    live.push (Source::midi, noteOn (62));
    live.push (Source::midi, noteOn (64));
    live.push (Source::midi, panic());
    CHECK (live.notesReceived (Source::ui) == 1);
    CHECK (live.notesReceived (Source::midi) == 2);
}

TEST_CASE ("the longest wait is kept until a longer one comes", "[core][live]")
{
    LiveInput live;
    live.noteWait (3.0);
    live.noteWait (1.0);
    CHECK (live.lastWaitMs.load() == 1.0f);
    CHECK (live.maxWaitMs.load() == 3.0f);
    live.noteWait (-2.0); // two threads' clocks: never a negative wait
    CHECK (live.lastWaitMs.load() == 0.0f);
    CHECK (live.maxWaitMs.load() == 3.0f);
}

TEST_CASE ("MIDI notes become live events for the selected channel, on any MIDI channel", "[core][live][midi]")
{
    const auto soft = LiveInput::fromMidi (juce::MidiMessage::noteOn (3, 57, (juce::uint8)90), 2, 12.5);
    REQUIRE (soft);
    CHECK (soft->type == Type::noteOn);
    CHECK (soft->pitch == 57);
    CHECK (soft->velocity == 90);
    CHECK_FALSE (soft->accent);
    CHECK (soft->channel == 2);
    CHECK (soft->timeMs == 12.5);

    const auto hard = LiveInput::fromMidi (juce::MidiMessage::noteOn (1, 57, (juce::uint8)100), 0, 0.0);
    REQUIRE (hard);
    CHECK (hard->accent); // 100 and up, as on 303 clones

    for (const auto& release :
         { juce::MidiMessage::noteOff (1, 57), juce::MidiMessage::noteOn (1, 57, (juce::uint8)0) })
    {
        const auto e = LiveInput::fromMidi (release, 0, 0.0);
        REQUIRE (e);
        CHECK (e->type == Type::noteOff);
        CHECK (e->pitch == 57);
    }
    CHECK (LiveInput::fromMidi (juce::MidiMessage::allNotesOff (1), 0, 0.0)->type == Type::allOff);
    CHECK (LiveInput::fromMidi (juce::MidiMessage::allSoundOff (1), 0, 0.0)->type == Type::allOff);
    CHECK_FALSE (LiveInput::fromMidi (juce::MidiMessage::controllerEvent (1, 74, 64), 0, 0.0));
    CHECK_FALSE (LiveInput::fromMidi (juce::MidiMessage::pitchWheel (1, 9000), 0, 0.0));
}

// --- the engine ---------------------------------------------------------------------------------------

TEST_CASE ("a live note plays while stopped and ends when its key goes up", "[core][engine][live]")
{
    Session s;
    plainSaw (s);
    Rig rig (s);
    rig.push (noteOn (45)); // A2
    const auto held = rig.play (0.3);
    CHECK (peakBetween (held, 0.2) > 0.02f);
    CHECK (near (pitchAtEnd (held), 110.0));

    rig.push (noteOff (45));
    CHECK (peakBetween (rig.play (0.3), 0.1) < 0.001f);
}

TEST_CASE ("keys held together slide, and the note goes back to a key still down", "[core][engine][live]")
{
    Session s;
    plainSaw (s);
    Rig rig (s);
    rig.push (noteOn (45)); // A2, 110 Hz
    rig.play (0.2);
    rig.push (noteOn (57)); // A3 while A2 is down
    CHECK (near (pitchAtEnd (rig.play (0.2)), 220.0));

    SECTION ("letting the top key go slides back to the one still held")
    {
        rig.push (noteOff (57));
        const auto back = rig.play (0.2);
        CHECK (near (pitchAtEnd (back), 110.0));
        CHECK (peakBetween (back, 0.1) > 0.02f);
        rig.push (noteOff (45));
        CHECK (peakBetween (rig.play (0.3), 0.1) < 0.001f); // the last key up ends it
    }
    SECTION ("letting the bottom key go changes nothing")
    {
        rig.push (noteOff (45));
        CHECK (near (pitchAtEnd (rig.play (0.2)), 220.0));
        rig.push (noteOff (57));
        CHECK (peakBetween (rig.play (0.3), 0.1) < 0.001f);
    }
}

TEST_CASE ("a short gate ends a live note by itself, however long the key is held", "[core][engine][live]")
{
    Session s;
    plainSaw (s);
    Rig rig (s);
    rig.push (noteOn (45, 0, true));
    rig.push (noteOff (45)); // the key up early: the note still lasts its step's gate
    const auto out = rig.play (0.5);
    CHECK (peakBetween (out, 0.01, 0.04) > 0.02f); // 55% of a 16th at 170 BPM is 48 ms
    CHECK (peakBetween (out, 0.3) < 0.001f);

    rig.push (noteOn (45, 0, true)); // held and never let go
    const auto again = rig.play (0.5);
    CHECK (peakBetween (again, 0.3) < 0.001f);
}

TEST_CASE ("drums play one whole hit per key", "[core][engine][live]")
{
    auto hit = [] (bool keyUp)
    {
        Session s; // the default project: channel 0 is the kick, a sampler
        Rig rig (s);
        rig.push (noteOn (Step::samplerRootNote));
        if (keyUp)
            rig.push (noteOff (Step::samplerRootNote));
        return rig.play (0.5);
    };
    const auto held = hit (false), released = hit (true);
    CHECK (peakBetween (held, 0.0) > 0.05f);
    CHECK (held == released); // the key going up doesn't cut a one-shot
}

TEST_CASE ("panic silences every live note", "[core][engine][live]")
{
    Session s;
    plainSaw (s);
    Rig rig (s);
    rig.push (noteOn (45));
    rig.push (noteOn (57));
    rig.play (0.1);
    rig.push (panic());
    CHECK (peakBetween (rig.play (0.3), 0.1) < 0.001f);

    rig.push (noteOff (57)); // keys going up afterwards bring nothing back
    rig.push (noteOff (45));
    CHECK (peakBetween (rig.play (0.2), 0.0) < 0.001f);
}

TEST_CASE ("while a live key is held, the pattern doesn't play over it", "[core][engine][live]")
{
    Session s;
    plainSaw (s);
    for (int i = 0; i < Pattern::defaultLength; ++i)
        s.pattern (0).setStep (0, i, Step { true, false, true, 45 }); // A2 on every step, slid: one long note
    s.transport.playing = true;
    Rig rig (s);
    CHECK (near (pitchAtEnd (rig.play (0.5)), 110.0)); // the pattern alone

    rig.push (noteOn (57)); // A3, held across several steps
    CHECK (near (pitchAtEnd (rig.play (1.0), 0.5), 220.0));

    rig.push (noteOff (57)); // the pattern takes the channel back at its next step
    CHECK (near (pitchAtEnd (rig.play (0.5)), 110.0));
}

TEST_CASE ("a live note says how long it waited for the audio", "[core][engine][live]")
{
    Session s;
    plainSaw (s);
    Rig rig (s);
    auto late = noteOn (45);
    late.timeMs -= 5.0; // made 5 ms before the engine got to it
    rig.push (late);
    rig.play (0.01);
    const auto& live = rig.engine.liveInput();
    CHECK (live.lastWaitMs.load() >= 5.0f);
    CHECK (live.lastWaitMs.load() < 1000.0f);
    CHECK (live.maxWaitMs.load() >= 5.0f);
}

TEST_CASE ("a live note for a channel that isn't there is ignored", "[core][engine][live]")
{
    Session s;
    plainSaw (s); // one channel
    Rig rig (s);
    rig.push (noteOn (45, 5));
    rig.push (noteOn (45, -1));
    CHECK (peakBetween (rig.play (0.1), 0.0) == 0.0f);
}
