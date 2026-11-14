// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/commands/SessionCommands.h"
#include "core/engine/AudioEngine.h"
#include "core/io/ProjectFile.h"
#include "core/model/NoteNames.h"

using namespace tekno::core;
namespace project = tekno::core::io::project;

namespace
{
/** A session with something non-default everywhere a file can store it. */
void makeInteresting (Session& s)
{
    s.transport.bpm = 138.5;
    s.transport.swing = 62.0f;
    s.synth.cutoff = 0.12f;
    s.synth.resonance = 0.95f;
    s.synth.envMod = 0.33f;
    s.synth.decay = 0.7f;
    s.synth.accent = 1.0f;
    s.synth.square = true;
    s.pattern (0).setStep (5, Step { true, false, false, 61, true }); // a tie
    s.pattern (2).setLength (13);
    s.pattern (2).setStep (12, Step { true, true, true, 96 });
    s.pattern (2).setStep (40, Step { true, false, false, 24 }); // past the length: must survive
    s.project.patternNames[2] = "rumble \"quoted\" / ünïcode";
    s.selected = 2;
}

void requireSame (const Session& a, const Session& b)
{
    REQUIRE (a.transport.bpm.load() == b.transport.bpm.load());
    REQUIRE (a.transport.swing.load() == b.transport.swing.load());
    REQUIRE (a.synth.cutoff.load() == b.synth.cutoff.load());
    REQUIRE (a.synth.resonance.load() == b.synth.resonance.load());
    REQUIRE (a.synth.envMod.load() == b.synth.envMod.load());
    REQUIRE (a.synth.decay.load() == b.synth.decay.load());
    REQUIRE (a.synth.accent.load() == b.synth.accent.load());
    REQUIRE (a.synth.square.load() == b.synth.square.load());
    REQUIRE (a.selected.load() == b.selected.load());
    for (int p = 0; p < Session::maxPatterns; ++p)
    {
        CAPTURE (p);
        REQUIRE (a.project.patternNames[static_cast<size_t> (p)] == b.project.patternNames[static_cast<size_t> (p)]);
        REQUIRE (a.pattern (p).getLength() == b.pattern (p).getLength());
        for (int s = 0; s < Pattern::maxSteps; ++s)
            REQUIRE (a.pattern (p).getStep (s) == b.pattern (p).getStep (s));
    }
}
} // namespace

TEST_CASE ("note names round-trip and accept flats", "[core][notes]")
{
    for (int n = Step::minNote; n <= Step::maxNote; ++n)
        REQUIRE (parseNoteName (noteName (n)) == n);
    CHECK (noteName (45) == "A2");
    CHECK (noteName (60) == "C4");
    CHECK (parseNoteName ("Db3") == 49);
    CHECK (parseNoteName ("c#3") == 49);
    CHECK_FALSE (parseNoteName ("H2").has_value());
    CHECK_FALSE (parseNoteName ("A").has_value());
    CHECK_FALSE (parseNoteName ("A2x").has_value());
}

TEST_CASE ("a project survives saving and opening, exactly", "[core][project]")
{
    Session original;
    makeInteresting (original);

    const auto file = juce::File::createTempFile (project::extension);
    REQUIRE (project::save (original, file).wasOk());

    Session reopened;
    REQUIRE (project::load (file, reopened).wasOk());
    requireSame (original, reopened);

    // And the text is stable: saving the reopened project gives the same bytes.
    CHECK (project::toText (reopened) == file.loadFileAsString());
    file.deleteFile();
}

TEST_CASE ("the file is readable and diffable", "[core][project]")
{
    Session s;
    const auto text = project::toText (s);
    CHECK (text.contains ("\"format\": \"tekno-daw/project\""));
    CHECK (text.contains ("\"name\": \"acid 01\""));
    CHECK (text.contains ("{ \"on\": true, \"note\": \"A2\", \"accent\": true, \"slide\": false, \"tie\": false }"));
    // one step per line, and only patterns that hold something
    CHECK (juce::StringArray::fromLines (text).size() > 16);
    CHECK_FALSE (text.contains ("\"number\": 2"));
}

TEST_CASE ("a broken file says where and leaves the session alone", "[core][project]")
{
    Session s;
    makeInteresting (s);
    const auto before = project::toText (s);

    auto broken = project::toText (Session {}).replace ("\"note\": \"A3\"", "\"note\": \"H3\"");
    auto result = project::fromText (broken, s);
    REQUIRE (result.failed());
    CHECK (result.getErrorMessage().contains ("patterns[0].steps[2].note"));
    CHECK (result.getErrorMessage().contains ("H3"));
    CHECK (project::toText (s) == before);

    CHECK (project::fromText ("not json at all", s).failed());
    CHECK (project::fromText ("{ \"hello\": 1 }", s).getErrorMessage().contains ("not a tekno-daw project"));
    CHECK (project::fromText (project::toText (s).replace ("\"version\": 1", "\"version\": 9"), s)
               .getErrorMessage()
               .contains ("newer tekno-daw"));
    CHECK (project::fromText (project::toText (s).replace ("\"bpm\": 138.5", "\"bpm\": 9000"), s)
               .getErrorMessage()
               .contains ("tempo.bpm"));
    CHECK (project::toText (s) == before);
}

TEST_CASE ("missing optional fields fall back to defaults", "[core][project]")
{
    Session s;
    REQUIRE (project::fromText ("{ \"format\": \"tekno-daw/project\", \"version\": 1 }", s).wasOk());
    CHECK (s.transport.bpm.load() == TransportState::defaultBpm);
    CHECK (s.pattern (0).isEmpty());
}

TEST_CASE ("project commands save, mark changes and open", "[core][project][commands]")
{
    Session s;
    CommandRegistry r;
    registerSessionCommands (r, s);
    const auto file = juce::File::createTempFile (project::extension);

    CHECK (r.execute ("project.save").error.contains ("saveAs"));
    CHECK_FALSE (s.project.hasUnsavedChanges());
    r.execute ("pattern.step.toggle", { { "step", 2 } });
    CHECK (s.project.hasUnsavedChanges());
    r.execute ("transport.play"); // playing is not a change to the project
    REQUIRE (r.execute ("project.saveAs", { { "path", file.getFullPathName() } }).succeeded());
    CHECK_FALSE (s.project.hasUnsavedChanges());
    CHECK (s.project.file == file);

    r.execute ("edit.undo"); // undo is a change too
    CHECK (s.project.hasUnsavedChanges());

    REQUIRE (r.execute ("project.open", { { "path", file.getFullPathName() } }).succeeded());
    CHECK (s.pattern (0).getStep (1).on); // the saved state, not the undone one
    CHECK_FALSE (s.project.hasUnsavedChanges());
    CHECK_FALSE (r.canUndo()); // history belongs to the previous document
    CHECK_FALSE (s.transport.playing.load());

    CHECK (r.execute ("project.open", { { "path", "/nope/missing.tekno" } }).error.contains ("not found"));
    file.deleteFile();
}

TEST_CASE ("a saved and reopened project sounds identical, sample for sample", "[core][project][engine]")
{
    // M2's done criterion, as a test: write a line, save, reopen, and hear exactly the same thing.
    auto renderSelected = [] (Session& s)
    {
        tekno::core::AudioEngine engine (s);
        engine.prepare (48000.0);
        s.transport.playing = true;
        std::vector<float> out (48000 * 4);
        for (size_t pos = 0; pos < out.size(); pos += 512)
        {
            float* ch = out.data() + pos;
            engine.render (&ch, 1, static_cast<int> (std::min<size_t> (512, out.size() - pos)));
        }
        s.transport.playing = false;
        return out;
    };

    Session original;
    makeInteresting (original);
    original.selected = 0; // the acid line, with the tie added by makeInteresting
    const auto file = juce::File::createTempFile (project::extension);
    REQUIRE (project::save (original, file).wasOk());

    Session reopened;
    REQUIRE (project::load (file, reopened).wasOk());

    const auto a = renderSelected (original);
    const auto b = renderSelected (reopened);
    REQUIRE (a.size() == b.size());
    CHECK (std::equal (a.begin(), a.end(), b.begin()));
    file.deleteFile();
}
