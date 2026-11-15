// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <catch2/catch_test_macros.hpp>

#include "core/commands/CommandLine.h"
#include "core/commands/CommandRegistry.h"
#include "core/commands/SessionCommands.h"
#include "ui/KeyBindings.h"

using namespace tekno;

TEST_CASE ("keys typed by people become JUCE's key descriptions", "[ui][keys]")
{
    CHECK (ui::keys::normalise ("ctrl+s") == juce::String ("ctrl + S"));
    CHECK (ui::keys::normalise ("Ctrl + Shift + z") == juce::String ("ctrl + shift + Z"));
    CHECK (ui::keys::normalise ("space") == ui::keys::describe (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (ui::keys::normalise ("F7") == ui::keys::describe (juce::KeyPress (juce::KeyPress::F7Key)));
    CHECK (ui::keys::normalise ("1") == juce::String ("1"));
    CHECK_FALSE (ui::keys::normalise ("").has_value());
}

TEST_CASE ("the default keys have no conflicts and every one runs a real command line", "[ui][keys]")
{
    const auto map = ui::keys::defaults();
    CHECK (map.conflicts().empty());
    CHECK (map.all().size() > 20);

    // The app registers the window's own commands (ui.*) and those for playing live and for the audio
    // device (live.*, audio.*); its smoke test checks those keys. The rest must parse here.
    core::Session session;
    core::CommandRegistry registry;
    core::registerSessionCommands (registry, session);
    for (const auto& b : map.all())
    {
        CAPTURE (b.key, b.command);
        if (! b.command.startsWith ("ui.") && ! b.command.startsWith ("live.") && ! b.command.startsWith ("audio."))
            CHECK (core::CommandLine::parse (b.command, registry).command.has_value());
    }
}

TEST_CASE ("every panel opens with Alt and its place in the top bar, as well as with an F key", "[ui][keys]")
{
    // Many laptops send media keys for F1-F12 unless Fn is held: numbers always work.
    const auto map = ui::keys::defaults();
    const juce::StringArray panels { "pattern", "pianoroll", "keyboard", "effects", "mixer" };
    for (int i = 0; i < panels.size(); ++i)
    {
        const auto key = ui::keys::describe (juce::KeyPress ('1' + i, juce::ModifierKeys::altModifier, 0));
        const auto binding = map.find (key, { "global" });
        CAPTURE (key);
        REQUIRE (binding);
        CHECK (binding->command == "ui.view " + panels[i]);
    }
}

TEST_CASE ("a keymap file with keys in any form is cleaned on load", "[ui][keys]")
{
    const auto file = juce::File::createTempFile (".json");
    file.replaceWithText (R"({ "format": "tekno-daw/keymap", "version": 1, "bindings": [
        { "context": "acid", "key": "ctrl+1", "command": "cutoff 0.2" },
        { "context": "acid", "key": "not a key at all ++", "command": "cutoff 0.3" } ] })");
    juce::StringArray warnings;
    const auto map = ui::keys::load (file, warnings);
    REQUIRE (map.all().size() == 1);
    CHECK (map.all()[0].key == "ctrl + 1");
    CHECK (warnings.size() == 1);

    CHECK (ui::keys::load (juce::File ("/nope/keymap.json"), warnings).all() == ui::keys::defaults().all());
    file.deleteFile();
}
