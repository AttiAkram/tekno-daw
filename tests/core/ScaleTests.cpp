// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// The scale guide (keys) and auditioning notes.

#include <catch2/catch_test_macros.hpp>

#include "TestSessions.h"
#include "core/commands/CommandLine.h"
#include "core/commands/SessionCommands.h"
#include "core/engine/AudioEngine.h"
#include "core/io/ProjectFile.h"
#include "core/model/Scale.h"

using namespace tekno::core;

namespace
{
struct Fixture
{
    Session session;
    CommandRegistry registry;
    Fixture() { registerSessionCommands (registry, session); }

    juce::String line (const juce::String& text)
    {
        const auto parsed = CommandLine::parse (text, registry);
        if (! parsed.command)
            return parsed.error;
        return registry.execute (parsed.command->id, parsed.command->args).error;
    }
};

float peakOf (Session& session, int samples)
{
    AudioEngine engine (session);
    engine.prepare (48000.0);
    std::vector<float> out (static_cast<size_t> (samples));
    float* ch = out.data();
    session.requestPreview (session.selectedChannel.load(), 45, 100); // after prepare: a new request
    engine.render (&ch, 1, samples);
    float peak = 0.0f;
    for (auto v : out)
        peak = std::max (peak, std::abs (v));
    return peak;
}
} // namespace

TEST_CASE ("a key knows its scale: A minor is the white keys from A", "[core][scale]")
{
    const Key aMinor { 9, Scale::minor };
    for (int pitch = 57; pitch < 69; ++pitch) // A3 .. G#4
    {
        const auto pc = pitch % 12;
        const auto white = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
        CAPTURE (pitch);
        CHECK (aMinor.contains (pitch) == white);
    }
    CHECK (aMinor.isRoot (45));
    CHECK_FALSE (aMinor.isRoot (48));
    CHECK (aMinor.nearestInScale (54) == 53); // F#3 -> F3 (ties go down)
    CHECK (aMinor.nearestInScale (57) == 57);

    const Key ePhrygian { 4, Scale::phrygian }; // also the white keys, from E
    CHECK (ePhrygian.contains (53));            // F: the dark second
    CHECK_FALSE (ePhrygian.contains (54));
    CHECK_FALSE (Key {}.active());
    CHECK (Key {}.contains (61)); // no key: everything goes

    CHECK (parseScale ("frigio") == Scale::phrygian);
    CHECK (parseScale ("MINORE") == Scale::minor);
    CHECK (parseRoot ("La") == 9);
    CHECK (parseRoot ("Sib") == 10);
    CHECK (parseRoot ("C#") == 1);
    CHECK_FALSE (parseRoot ("H").has_value());
    CHECK (rootName (9, true) == "A");
}

TEST_CASE ("key.set chooses the key, is undoable and is saved with the project", "[core][scale][commands]")
{
    Fixture f;
    CHECK (f.session.project.key == Key { 9, Scale::minor }); // a new project: La minore, like its line

    CHECK (f.line ("key Mi frigio").isEmpty());
    CHECK (f.session.project.key == Key { 4, Scale::phrygian });
    CHECK (f.line ("key.set F# major").isEmpty());
    CHECK (f.session.project.key == Key { 6, Scale::major });
    f.registry.undo();
    CHECK (f.session.project.key == Key { 4, Scale::phrygian });

    const auto text = io::project::toText (f.session);
    CHECK (text.contains ("\"key\": { \"root\": \"E\", \"scale\": \"phrygian\" }"));
    Session reopened;
    REQUIRE (io::project::fromText (text, reopened).wasOk());
    CHECK (reopened.project.key == f.session.project.key);

    CHECK (f.line ("key off").isEmpty());
    CHECK_FALSE (f.session.project.key.active());
    CHECK_FALSE (io::project::toText (f.session).contains ("\"key\""));
    CHECK (f.line ("key H minor").contains ("not a note"));
    CHECK (f.line ("key La blues").contains ("not a scale"));
    CHECK (
        io::project::fromText (text.replace ("phrygian", "lydian"), reopened).getErrorMessage().contains ("key.scale"));
}

TEST_CASE ("a note can be auditioned while stopped, on acid and on drums", "[core][engine][preview]")
{
    Session acid;
    tekno::test::acidOnly (acid);
    CHECK (peakOf (acid, 4800) > 0.05f); // a quarter of a second of 303

    Session drums;
    drums.selectedChannel = 0; // the kick
    CHECK (peakOf (drums, 4800) > 0.05f);

    // Nothing asked, nothing played: a stopped engine stays silent.
    Session quiet;
    AudioEngine engine (quiet);
    engine.prepare (48000.0);
    std::vector<float> out (4800);
    float* ch = out.data();
    engine.render (&ch, 1, 4800);
    CHECK (std::all_of (out.begin(), out.end(), [] (float v) { return v == 0.0f; }));
}

TEST_CASE ("an audition ends by itself after a quarter of a second", "[core][engine][preview]")
{
    Session session;
    tekno::test::acidOnly (session);
    session.channel (0).acid.decay = 0.0f;
    AudioEngine engine (session);
    engine.prepare (48000.0);
    session.requestPreview (0, 45, 100);
    std::vector<float> out (48000);
    for (size_t pos = 0; pos < out.size(); pos += 512)
    {
        float* ch = out.data() + pos;
        engine.render (&ch, 1, static_cast<int> (std::min<size_t> (512, out.size() - pos)));
    }
    float late = 0.0f;
    for (size_t i = 30000; i < out.size(); ++i)
        late = std::max (late, std::abs (out[i]));
    CHECK (late < 0.001f); // the gate closed at 12000 and the note has died away
}

TEST_CASE ("note.preview changes nothing in the project", "[core][commands][preview]")
{
    Fixture f;
    const auto revision = f.session.project.revision;
    CHECK (f.line ("hear La3").isEmpty());
    CHECK (f.session.project.revision == revision);
    CHECK_FALSE (f.registry.canUndo());
    CHECK ((f.session.preview.load() & 0xffu) == 57);
}
