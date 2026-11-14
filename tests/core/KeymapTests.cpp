// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <catch2/catch_test_macros.hpp>

#include "core/commands/Keymap.h"

using tekno::core::Keymap;

TEST_CASE ("a key looks from the most specific context to global", "[core][keys]")
{
    Keymap map;
    map.bind ({ "global", "1", "transport.play", "play" });
    map.bind ({ "acid", "1", "cutoff 0.2", {} });
    map.bind ({ "pianoroll", "ctrl + C", "ui.roll.copy", {} });

    CHECK (map.find ("1", { "acid", "pattern", "global" })->command == "cutoff 0.2");
    CHECK (map.find ("1", { "sampler", "pattern", "global" })->command == "transport.play");
    CHECK (map.find ("CTRL + c", { "acid", "pianoroll", "global" })->command == "ui.roll.copy"); // any case
    CHECK_FALSE (map.find ("ctrl + C", { "acid", "pattern", "global" }).has_value());
    CHECK_FALSE (map.find ("2", { "global" }).has_value());

    const auto covered = map.shadowed (*map.find ("1", { "acid" }), { "pattern", "global" });
    REQUIRE (covered.has_value());
    CHECK (covered->label == "play"); // shown as "covers", not as a conflict
    CHECK (map.conflicts().empty());
}

TEST_CASE ("binding the same key in the same context replaces, and says what", "[core][keys]")
{
    Keymap map;
    CHECK_FALSE (map.bind ({ "acid", "1", "cutoff 0.2", {} }).has_value());
    const auto replaced = map.bind ({ "acid", "1", "cutoff 0.9", {} });
    REQUIRE (replaced.has_value());
    CHECK (replaced->command == "cutoff 0.2");
    CHECK (map.inContext ("acid").size() == 1);
    CHECK (map.unbind ("acid", "1"));
    CHECK_FALSE (map.unbind ("acid", "1"));
}

TEST_CASE ("the keymap file is stable, and a broken one keeps what it can", "[core][keys]")
{
    Keymap map;
    map.bind ({ "sampler", "1", "tune -5", "low" });
    map.bind ({ "global", "spacebar", "transport.toggle", "play / stop" });
    const auto text = map.toJson();
    CHECK (text.indexOf ("\"global\"") < text.indexOf ("\"sampler\"")); // broadest context first

    juce::StringArray warnings;
    const auto again = Keymap::fromJson (text, warnings);
    CHECK (warnings.isEmpty());
    CHECK (again.all() == map.all());
    CHECK (again.toJson() == text);

    const auto handWritten = R"({ "format": "tekno-daw/keymap", "version": 1, "bindings": [
        { "context": "acid", "key": "1", "command": "cutoff 0.1" },
        { "context": "acid", "key": "1", "command": "cutoff 0.3" },
        { "context": "dj", "key": "2", "command": "play" },
        { "context": "global", "key": "", "command": "play" } ] })";
    warnings.clear();
    const auto read = Keymap::fromJson (handWritten, warnings);
    CHECK (read.all().size() == 2);
    CHECK (read.conflicts().size() == 1); // reported, not silently dropped
    CHECK (warnings.size() == 2);
    CHECK (warnings[0].contains ("unknown context 'dj'"));

    warnings.clear();
    CHECK (Keymap::fromJson ("{}", warnings).all().empty());
    CHECK (warnings[0].contains ("not a tekno-daw keymap"));
}
