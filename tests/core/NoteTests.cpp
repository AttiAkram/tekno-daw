// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// Lanes hold notes; the step sequencer is a view of them. These pin the conversion both ways, the
// engine's handling of notes off the grid, and the v3 file.

#include <catch2/catch_approx.hpp>
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
    CHECK (text.contains ("{ \"at\": 7, \"length\": 13, \"note\": \"D3\", \"accent\": true, \"slide\": false, \"velocity\": 100 }"));

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

// --- note commands (the piano roll's and the terminal's) ------------------------------------------

#include "core/commands/CommandLine.h"
#include "core/commands/SessionCommands.h"

namespace
{
struct Commands
{
    Session session;
    CommandRegistry registry;
    Commands() { registerSessionCommands (registry, session); }

    juce::String line (const juce::String& text)
    {
        const auto parsed = CommandLine::parse (text, registry);
        if (! parsed.command)
            return parsed.error;
        return registry.execute (parsed.command->id, parsed.command->args).error;
    }
    std::vector<Note> acid() const { return session.pattern (0).lane (4).notes(); }
};

bool has (const std::vector<Note>& notes, const Note& n)
{
    return std::find (notes.begin(), notes.end(), n) != notes.end();
}
} // namespace

TEST_CASE ("notes can be added, moved, resized, flagged and removed by name", "[core][commands][notes]")
{
    Commands f; // the acid (channel 5) is selected
    const auto before = f.acid();

    CHECK (f.line ("note.add 344 E3 16").isEmpty()); // a triplet 1/16 near the end
    CHECK (has (f.acid(), Note { 344, 16, 52 }));
    CHECK (f.line ("note.move 344 E3 toAt=336 toNote=G3").isEmpty());
    CHECK (has (f.acid(), Note { 336, 16, 55 }));
    CHECK (f.line ("note.resize 336 G3 30").isEmpty());
    CHECK (f.line ("note.accent 336 G3 on").isEmpty());
    CHECK (f.line ("note.slide 336 G3 on").isEmpty());
    CHECK (has (f.acid(), Note { 336, 30, 55, true, true }));
    CHECK (f.line ("note.remove 336 G3").isEmpty());
    CHECK (f.acid() == before);

    CHECK (f.line ("note.remove 7 C2").contains ("there is no C2 starting at tick 7"));
    CHECK (f.line ("note.add 2000 C2 24").contains ("between"));
    CHECK (f.line ("note.add 0 C4 24 channel=1").isEmpty()); // another lane, named
    CHECK (f.session.pattern (0).lane (0).notes().size() == 5);
}

TEST_CASE ("each note edit is one undo step, and redo comes back to the same lane", "[core][commands][notes]")
{
    Commands f;
    const auto before = f.acid();
    REQUIRE (f.line ("note.add 12 C3 12").isEmpty());
    REQUIRE (f.line ("note.move 12 C3 toAt=36 toNote=D3").isEmpty());
    const auto after = f.acid();

    f.registry.undo();
    CHECK (has (f.acid(), Note { 12, 12, 48 }));
    f.registry.undo();
    CHECK (f.acid() == before);

    f.line ("ch 1"); // redo must not land on the newly selected channel
    f.registry.redo();
    f.registry.redo();
    CHECK (f.acid() == after);
    CHECK (f.session.pattern (0).lane (0).notes().size() == 4);
}

TEST_CASE ("the step view and the notes stay the same data", "[core][commands][notes]")
{
    // The done criterion of M3, in small: a note made in the piano roll is a step, and back.
    Commands f;
    REQUIRE (f.line ("note.add 48 C3 24 channel=1").isEmpty()); // step 3 of the kick lane
    CHECK (f.session.pattern (0).getStep (0, 2) == Step { true, false, false, 48 });
    REQUIRE (f.line ("toggle 3 channel=1").isEmpty()); // off again from the step sequencer
    CHECK_FALSE (has (f.session.pattern (0).lane (0).notes(), Note { 48, 24, 48 }));
    REQUIRE (f.line ("tie 2 on").isEmpty()); // a tie in the step view is a longer note
    CHECK (has (f.acid(), Note { 0, 48, 45, true, false }));
}

// --- velocity and several notes at once ------------------------------------------------------------

TEST_CASE ("velocity survives the step view, the file and the packing", "[core][notes][velocity]")
{
    CHECK (Note::unpack (Note { 0, 24, 45, false, false, 37 }.pack()).velocity == 37);
    CHECK (Step::unpack (Step { true, false, false, 45, false, 99 }.pack()).velocity == 99);

    Pattern pattern;
    REQUIRE (pattern.lane (0).setNotes ({ Note { 0, 48, 45, false, false, 64 } }));
    pattern.setStep (0, 4, Step { true, false, false, 50 }); // another step: the long note keeps its velocity
    CHECK (pattern.lane (0).notes()[0].velocity == 64);
    CHECK (pattern.getStep (0, 1).velocity == 64); // a tie shows its note's velocity

    Session s;
    REQUIRE (s.pattern (0).lane (4).setNotes ({ Note { 0, 24, 45, true, false, 12 } }));
    const auto text = tekno::core::io::project::toText (s);
    CHECK (text.contains ("\"velocity\": 12"));
    Session reopened;
    REQUIRE (tekno::core::io::project::fromText (text, reopened).wasOk());
    CHECK (reopened.pattern (0).lane (4).notes()[0].velocity == 12);
    // A file without velocities (written before they existed) reads as 100: nothing changes.
    REQUIRE (tekno::core::io::project::fromText (text.replace (", \"velocity\": 12", ""), reopened).wasOk());
    CHECK (reopened.pattern (0).lane (4).notes()[0].velocity == Note::defaultVelocity);
}

TEST_CASE ("velocity is loudness: 100 changes nothing, 50 is half", "[core][engine][velocity]")
{
    auto peakWith = [] (int velocity)
    {
        Session session;
        while (session.channelCount() > 1)
            session.removeChannel (1); // the kick alone: a one-shot, the same every time
        REQUIRE (
            session.pattern (0).lane (0).setNotes ({ Note { 0, tps, Step::samplerRootNote, false, false, velocity } }));
        session.transport.playing = true;
        const auto out = render (session, 6000);
        float peak = 0.0f;
        for (auto v : out)
            peak = std::max (peak, std::abs (v));
        return peak;
    };
    const auto unity = peakWith (100);
    CHECK (peakWith (50) == Catch::Approx (unity * 0.5f).epsilon (0.001));
    CHECK (peakWith (127) == Catch::Approx (unity * 1.27f).epsilon (0.001));
    CHECK (peakWith (0) == 0.0f);
    CHECK (Note::velocityGain (100) == 1.0f); // exactly: old projects sound bit for bit the same
}

TEST_CASE ("several notes move, change and go together, as one undo step", "[core][commands][velocity]")
{
    Commands f;
    const auto before = f.acid();

    CHECK (f.line ("notes.move \"0:A2 48:A3\" by=12 semitones=2").isEmpty());
    CHECK (has (f.acid(), Note { 12, 24, 47, true, false }));
    CHECK (has (f.acid(), Note { 60, 24, 59, false, true }));
    f.registry.undo();
    CHECK (f.acid() == before);

    CHECK (f.line ("notes.velocity \"0:La2, 96:Do3\" 40").isEmpty()); // names in either system, commas fine
    CHECK (f.acid()[0].velocity == 40);
    CHECK (f.line ("note.velocity 48 A3 127").isEmpty());
    CHECK (f.line ("notes.remove \"0:A2 48:A3\"").isEmpty());
    CHECK (f.acid().size() == before.size() - 2);

    CHECK (f.line ("notes.move \"0:C2\" by=1 semitones=0").contains ("there is no C2 starting at tick 0"));
    CHECK (f.line ("notes.move \"96:C3\" by=-200 semitones=0").contains ("outside the pattern"));
    CHECK (f.line ("notes.remove \"hello\"").contains ("position:pitch"));
    CHECK (f.line ("note.add 0 C3 24 velocity=200").contains ("between"));
}
