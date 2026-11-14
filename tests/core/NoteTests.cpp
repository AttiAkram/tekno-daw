// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// Lanes hold notes; the step sequencer is a view of them. These pin the conversion both ways, the
// engine's handling of notes off the grid, and the v3 file.

#include <catch2/catch_test_macros.hpp>

#include "TestSessions.h"
#include "core/engine/AudioEngine.h"
#include "core/io/ProjectFile.h"

using namespace tekno::core;

namespace
{
constexpr int tps = Note::ticksPerStep;

std::array<Step, steps::count> stepsOf (std::initializer_list<std::pair<int, Step>> list)
{
    std::array<Step, steps::count> out {};
    for (const auto& [index, step] : list)
        out[static_cast<size_t> (index)] = step;
    return out;
}

/** The first sample louder than the threshold after `from`. */
int onsetAfter (const std::vector<float>& x, int from, float threshold = 1.0e-3f)
{
    for (auto i = static_cast<size_t> (from); i < x.size(); ++i)
        if (std::abs (x[i]) > threshold)
            return static_cast<int> (i);
    return -1;
}

std::vector<float> render (Session& session, int numSamples)
{
    AudioEngine engine (session);
    engine.prepare (48000.0);
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (int pos = 0; pos < numSamples; pos += 256)
    {
        float* ch = out.data() + pos;
        engine.render (&ch, 1, std::min (256, numSamples - pos));
    }
    return out;
}
} // namespace

TEST_CASE ("a note packs into 64 bits and back", "[core][notes]")
{
    const Note n { 1535, 1536, 96, true, true };
    CHECK (Note::unpack (n.pack()) == n);
    CHECK (Note {}.pack() != 0); // 0 is an empty slot
    CHECK (Note { 13, 7, 40 }.onGrid() == false);
    CHECK (Note { 48, 72, 40 }.onGrid());
}

TEST_CASE ("steps become notes: ties lengthen, slide belongs to the last step", "[core][notes]")
{
    const auto grid = stepsOf ({
        { 0, Step { true, true, false, 45 } },
        { 1, Step { true, false, false, 99, true } }, // tie: pitch ignored
        { 2, Step { true, false, true, 45, true } },  // last of the chain: its slide is the note's
        { 3, Step { true, false, false, 52 } },
        { 5, Step { true, false, false, 50, true } }, // tie after an off step: a note of its own
    });
    const auto notes = steps::toNotes (grid, 16);
    REQUIRE (notes.size() == 3);
    CHECK (notes[0] == Note { 0, 3 * tps, 45, true, true });
    CHECK (notes[1] == Note { 3 * tps, tps, 52, false, false });
    CHECK (notes[2] == Note { 5 * tps, tps, 50, false, false });

    // And back: the same steps (a tie shows its note's pitch).
    const auto back = steps::fromNotes (notes, 16);
    CHECK (back[0] == grid[0]);
    CHECK (back[1] == Step { true, false, false, 45, true });
    CHECK (back[2] == Step { true, false, true, 45, true });
    CHECK (back[3] == grid[3]);
    CHECK (back[5] == Step { true, false, false, 50, false });
}

TEST_CASE ("a tie on the first step continues the last step's note into the next round", "[core][notes]")
{
    const auto grid = stepsOf ({
        { 0, Step { true, false, false, 45, true } },
        { 1, Step { true, false, true, 45, true } },
        { 7, Step { true, false, false, 57 } },
    });
    const auto notes = steps::toNotes (grid, 8);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0] == Note { 7 * tps, 3 * tps, 57, false, true }); // runs past the end of an 8-step pattern

    const auto back = steps::fromNotes (notes, 8);
    CHECK (back[7] == Step { true, false, false, 57 });
    CHECK (back[0] == Step { true, false, false, 57, true });
    CHECK (back[1] == Step { true, false, true, 57, true });
}

TEST_CASE ("editing a step leaves notes off the grid alone", "[core][notes]")
{
    Pattern pattern;
    const Note triplet { 16, 16, 60 }, thirtySecond { 4 * tps + 12, 12, 62 };
    REQUIRE (pattern.lane (0).setNotes ({ Note { 0, tps, 45 }, triplet, thirtySecond }));
    CHECK_FALSE (steps::representable (pattern.lane (0).notes()));

    pattern.setStep (0, 2, Step { true, false, false, 48 });
    pattern.setStep (0, 0, Step {});
    const auto notes = pattern.lane (0).notes();
    REQUIRE (notes.size() == 3);
    CHECK (notes[0] == triplet);
    CHECK (notes[1] == Note { 2 * tps, tps, 48 });
    CHECK (notes[2] == thirtySecond);
}

TEST_CASE ("changing one note touches only its slot", "[core][notes]")
{
    Lane lane;
    std::vector<Note> notes;
    for (int i = 0; i < 16; ++i)
        notes.push_back ({ i * tps, tps, 45 + i });
    REQUIRE (lane.setNotes (notes));
    std::array<std::uint64_t, Lane::maxNotes> before {};
    for (int i = 0; i < Lane::maxNotes; ++i)
        before[static_cast<size_t> (i)] = lane.slot (i);

    notes[5].length = 2 * tps; // longer, same start: rewritten in place
    REQUIRE (lane.setNotes (notes));
    int changed = 0;
    for (int i = 0; i < Lane::maxNotes; ++i)
        changed += lane.slot (i) != before[static_cast<size_t> (i)] ? 1 : 0;
    CHECK (changed == 1);

    CHECK_FALSE (lane.setNotes (std::vector<Note> (Lane::maxNotes + 1)));
    CHECK (lane.notes() == notes); // a refused edit changes nothing
}

TEST_CASE ("a note off the grid plays on its exact sample, swing included", "[core][engine][notes]")
{
    // The kick: a one-shot starts the same way every time (the acid's oscillator runs free, so its
    // first audible sample depends on the phase). Compared with the same hit on the step's edge.
    auto onsetOf = [] (float swing, int start)
    {
        Session session;
        while (session.channelCount() > 1)
            session.removeChannel (1);
        session.pattern (0).lane (0).clear();
        session.transport.bpm = 120.0;
        session.transport.swing = swing;
        REQUIRE (session.pattern (0).lane (0).setNotes ({ Note { start, tps / 2, Step::samplerRootNote } }));
        session.transport.playing = true;
        return onsetAfter (render (session, 24000), 0);
    };

    for (const auto swing : { 50.0f, 62.0f })
    {
        CAPTURE (swing);
        // Half way through step 2: the off-beat 16th, which swing delays and shortens.
        const auto straight = AudioEngine::samplesPerStep (48000.0, 120.0);
        const auto second = AudioEngine::swungStepLength (straight, swing, 1);
        const auto delay = onsetOf (swing, tps + tps / 2) - onsetOf (swing, tps);
        CHECK (std::abs (delay - second * 0.5) <= 1.0);
    }
}

TEST_CASE ("a long note keeps the gate open until 45% of a step before its end", "[core][engine][notes]")
{
    auto energyBetween = [] (const std::vector<float>& x, double from, double to)
    {
        double sum = 0.0;
        for (auto i = static_cast<size_t> (from); i < static_cast<size_t> (to); ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return sum / (to - from);
    };

    Session session;
    tekno::test::acidOnly (session);
    session.transport.bpm = 120.0;
    session.channel (0).acid.decay = 1.0f;
    REQUIRE (session.pattern (0).lane (0).setNotes ({ Note { 0, 4 * tps, 45 } }));
    session.transport.playing = true;
    const auto out = render (session, 48000);

    const auto stepLength = AudioEngine::samplesPerStep (48000.0, 120.0);
    const auto release = stepLength * 3.55;
    // Held through three and a half steps, then the release fades it: the 303's 55% on the last step.
    CHECK (energyBetween (out, release - 2000, release - 200) >
           energyBetween (out, release + 1500, release + 3000) * 4.0);
}

TEST_CASE ("version 3 files check their notes", "[core][project][notes]")
{
    Session s;
    REQUIRE (s.pattern (0).lane (4).setNotes ({ Note { 7, 13, 50, true, false } }));
    const auto text = tekno::core::io::project::toText (s);
    CHECK (text.contains ("{ \"at\": 7, \"length\": 13, \"note\": \"D3\", \"accent\": true, \"slide\": false }"));

    Session reopened;
    REQUIRE (tekno::core::io::project::fromText (text, reopened).wasOk());
    CHECK (reopened.pattern (0).lane (4).notes() == s.pattern (0).lane (4).notes());

    CHECK (tekno::core::io::project::fromText (text.replace ("\"at\": 7", "\"at\": 99999"), reopened)
               .getErrorMessage()
               .contains ("notes[0].at"));
    CHECK (tekno::core::io::project::fromText (text.replace ("\"ticksPerStep\": 24", "\"ticksPerStep\": 96"), reopened)
               .getErrorMessage()
               .contains ("ticksPerStep"));
}
