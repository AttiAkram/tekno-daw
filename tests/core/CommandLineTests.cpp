// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <catch2/catch_test_macros.hpp>

#include "core/commands/CommandLine.h"
#include "core/commands/SessionCommands.h"

using namespace tekno::core;

namespace
{
struct Fixture
{
    Session session;
    CommandRegistry registry;
    Fixture() { registerSessionCommands (registry, session); }

    juce::String run (const juce::String& line)
    {
        const auto parsed = CommandLine::parse (line, registry);
        if (! parsed.command)
            return parsed.error;
        return registry.execute (parsed.command->id, parsed.command->args).error;
    }
};
} // namespace

TEST_CASE ("positional, named and aliased arguments", "[core][terminal]")
{
    Fixture f;
    CHECK (f.run ("transport.bpm 138").isEmpty());
    CHECK (f.session.transport.bpm.load() == 138.0);
    CHECK (f.run ("transport.bpm bpm=141.5").isEmpty());
    CHECK (f.session.transport.bpm.load() == 141.5);
    CHECK (f.run ("bpm 125").isEmpty());
    CHECK (f.session.transport.bpm.load() == 125.0);
    CHECK (f.run ("play").isEmpty());
    CHECK (f.session.transport.playing.load());
    CHECK (f.run ("drive 0.6").isEmpty());
    CHECK (f.session.synth.drive.load() == 0.6f);
    CHECK (f.run ("reso 0.9").isEmpty());
    CHECK (f.session.synth.resonance.load() == 0.9f);
}

TEST_CASE ("notes by name, booleans in words, the rest of the line as text", "[core][terminal]")
{
    Fixture f;
    CHECK (f.run ("pattern.step.note 3 C#3").isEmpty());
    CHECK (f.session.pattern (0).getStep (2).note == 49);
    CHECK (f.run ("accent 2 on").isEmpty());
    CHECK (f.session.pattern (0).getStep (1).accent);
    CHECK (f.run ("slide step=4 on=off").isEmpty());
    CHECK_FALSE (f.session.pattern (0).getStep (3).slide);
    CHECK (f.run ("rename acid night  01").isEmpty());
    CHECK (f.session.project.patternNames[0] == "acid night 01");
    CHECK (f.run ("rename \"with \\\"quotes\\\"\"").isEmpty());
    CHECK (f.session.project.patternNames[0] == "with \"quotes\"");
}

TEST_CASE ("mistakes get a useful message", "[core][terminal]")
{
    Fixture f;
    CHECK (f.run ("transport.bmp 120").contains ("did you mean 'transport.bpm'"));
    CHECK (f.run ("paly").contains ("did you mean 'play'"));
    CHECK (f.run ("zzzzzz").contains ("help"));
    CHECK (f.run ("bpm fast").contains ("must be a number"));
    CHECK (f.run ("bpm 120 130").contains ("too many arguments"));
    CHECK (f.run ("bpm tempo=120").contains ("no argument 'tempo'"));
    CHECK (f.run ("accent 2 maybe").contains ("on/off"));
    CHECK (f.run ("rename \"open quote").contains ("not closed"));
    CHECK (f.run ("bpm 9000").contains ("between")); // the registry still validates ranges
    CHECK (f.run ("note 3 H2").contains ("must be a number"));
}

TEST_CASE ("completion and help", "[core][terminal]")
{
    Fixture f;
    const auto steps = CommandLine::complete ("pattern.step.", f.registry);
    CHECK (steps.contains ("pattern.step.tie"));
    CHECK (steps.size() == 5);
    CHECK (CommandLine::complete ("pl", f.registry) == juce::StringArray { "play" });

    CHECK (CommandLine::help (f.registry).contains ("transport.bpm"));
    const auto detail = CommandLine::help (f.registry, "bpm");
    CHECK (detail.contains ("transport.bpm <bpm>"));
    CHECK (detail.contains ("beats per minute"));
    CHECK (CommandLine::help (f.registry, "nothing").contains ("no command matches"));
}
