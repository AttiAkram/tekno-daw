// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <catch2/catch_test_macros.hpp>

#include "core/commands/SessionCommands.h"

using namespace tekno::core;

namespace
{
struct Fixture
{
    Session session;
    CommandRegistry registry;
    Fixture() { registerSessionCommands (registry, session); }
};
} // namespace

TEST_CASE ("unknown commands and bad arguments are rejected with a reason", "[core][commands]")
{
    Fixture f;
    CHECK (f.registry.execute ("nope").error.contains ("unknown command"));
    CHECK (f.registry.execute ("pattern.step.toggle").error.contains ("missing argument"));
    CHECK (f.registry.execute ("pattern.step.toggle", { { "step", 64 } }).error.contains ("between"));
    CHECK (f.registry.execute ("pattern.step.toggle", { { "step", 2.5 } }).error.contains ("whole number"));
    CHECK (f.registry.execute ("pattern.step.toggle", { { "step", "x" } }).error.contains ("number"));
    CHECK (f.registry.execute ("pattern.step.toggle", { { "step", 1 }, { "extra", 1 } })
               .error.contains ("unknown argument"));
    CHECK (f.registry.execute ("transport.bpm", { { "bpm", 1000 } }).error.isNotEmpty());
    CHECK_FALSE (f.registry.canUndo()); // failures never enter the history
}

TEST_CASE ("pattern edits can be undone and redone", "[core][commands]")
{
    Fixture f;
    const auto original = f.session.pattern (0).getStep (1);

    REQUIRE (f.registry.execute ("pattern.step.toggle", { { "step", 1 } }).succeeded());
    CHECK (f.session.pattern (0).getStep (1).on != original.on);

    REQUIRE (f.registry.execute ("edit.undo").succeeded());
    CHECK (f.session.pattern (0).getStep (1) == original);

    REQUIRE (f.registry.execute ("edit.redo").succeeded());
    CHECK (f.session.pattern (0).getStep (1).on != original.on);
}

TEST_CASE ("a new edit clears the redo history", "[core][commands]")
{
    Fixture f;
    f.registry.execute ("pattern.step.note", { { "step", 0 }, { "note", 60 } });
    f.registry.undo();
    REQUIRE (f.registry.canRedo());
    f.registry.execute ("pattern.step.accent", { { "step", 3 }, { "on", true } });
    CHECK_FALSE (f.registry.canRedo());
}

TEST_CASE ("accent and slide turn a step on", "[core][commands]")
{
    Fixture f;
    f.registry.execute ("pattern.clear");
    f.registry.execute ("pattern.step.accent", { { "step", 5 }, { "on", true } });
    f.registry.execute ("pattern.step.slide", { { "step", 6 }, { "on", true } });
    CHECK (f.session.pattern (0).getStep (5).on);
    CHECK (f.session.pattern (0).getStep (5).accent);
    CHECK (f.session.pattern (0).getStep (6).on);
    CHECK (f.session.pattern (0).getStep (6).slide);

    f.registry.undo();
    f.registry.undo();
    f.registry.undo();                            // the clear
    CHECK (f.session.pattern (0).getStep (0).on); // the default line is back
}

TEST_CASE ("transport and synth commands change the shared state but not the history", "[core][commands]")
{
    Fixture f;
    f.registry.execute ("transport.play");
    CHECK (f.session.transport.playing.load());
    f.registry.execute ("transport.toggle");
    CHECK_FALSE (f.session.transport.playing.load());
    f.registry.execute ("transport.bpm", { { "bpm", 138 } });
    CHECK (f.session.transport.bpm.load() == 138.0);
    f.registry.execute ("synth.cutoff", { { "value", 0.9 } });
    CHECK (f.session.synth.cutoff.load() == 0.9f);
    f.registry.execute ("synth.waveform", { { "square", true } });
    CHECK (f.session.synth.square.load());
    CHECK_FALSE (f.registry.canUndo());
}

TEST_CASE ("every command describes itself", "[core][commands]")
{
    Fixture f;
    const auto all = f.registry.list();
    REQUIRE (all.size() >= 15);
    for (size_t i = 0; i < all.size(); ++i)
    {
        CHECK (all[i]->description.isNotEmpty());
        if (i > 0)
            CHECK (all[i - 1]->id < all[i]->id);
    }
}

TEST_CASE ("pattern commands act on the selected pattern", "[core][commands]")
{
    Fixture f;
    REQUIRE (f.registry.execute ("pattern.select", { { "pattern", 3 } }).succeeded());
    CHECK (f.session.selected.load() == 2);
    CHECK (f.session.playing.load() == 2); // stopped: immediate

    f.registry.execute ("pattern.step.toggle", { { "step", 40 } });
    f.registry.execute ("pattern.length", { { "steps", 48 } });
    f.registry.execute ("pattern.rename", { { "name", "  rumble  " } });
    CHECK (f.session.pattern (2).getStep (40).on);
    CHECK (f.session.pattern (2).getLength() == 48);
    CHECK (f.session.project.patternNames[2] == "rumble");
    CHECK_FALSE (f.session.pattern (0).getStep (40).on);

    f.registry.execute ("pattern.select", { { "pattern", 1 } });
    f.registry.undo(); // undoes the rename on pattern 3, even though pattern 1 is selected now
    CHECK (f.session.project.patternNames[2] == "pattern 3");

    CHECK (f.registry.execute ("pattern.rename", { { "name", "   " } }).error.contains ("needs a name"));
    CHECK (f.registry.execute ("pattern.length", { { "steps", 65 } }).error.isNotEmpty());
    CHECK (f.registry.execute ("pattern.select", { { "pattern", 17 } }).error.isNotEmpty());
    CHECK (f.registry.execute ("pattern.rename", { { "name", 5 } }).error.contains ("text"));
}

TEST_CASE ("tie and swing commands", "[core][commands]")
{
    Fixture f;
    f.registry.execute ("pattern.step.tie", { { "step", 1 }, { "on", true } });
    CHECK (f.session.pattern (0).getStep (1).tie);
    CHECK (f.session.pattern (0).getStep (1).on);
    f.registry.execute ("transport.swing", { { "percent", 66 } });
    CHECK (f.session.transport.swing.load() == 66.0f);
    CHECK (f.registry.execute ("transport.swing", { { "percent", 80 } }).error.isNotEmpty());
}
