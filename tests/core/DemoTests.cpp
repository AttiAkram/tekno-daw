// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/commands/CommandRegistry.h"
#include "core/commands/SessionCommands.h"
#include "core/engine/AudioEngine.h"

using namespace tekno::core;

TEST_CASE ("the demo opens with its four patterns and plays without clipping", "[core][demo]")
{
    Session session;
    CommandRegistry registry;
    registerSessionCommands (registry, session);
    const auto opened = registry.execute ("project.demo", {});
    REQUIRE (opened.succeeded());
    CHECK (opened.note.contains ("intro"));
    CHECK (session.project.file == juce::File()); // saving asks for a file: the demo is never overwritten
    CHECK (session.channelCount() == 8);
    CHECK (session.transport.bpm.load() == 162.0);
    CHECK (session.project.key.root == 8); // G#
    CHECK (session.project.key.scale == Scale::minor);
    const juce::StringArray names { "intro", "groove", "acid", "break" };
    for (int p = 0; p < names.size(); ++p)
    {
        CHECK (session.project.patternNames[static_cast<size_t> (p)] == names[p]);
        CHECK (session.pattern (p).getLength() == 64);
    }
    CHECK (session.selected.load() == 2); // it starts on "acid", the full one

    // Two bars of "acid": loud, but under full scale.
    session.transport.playing = true;
    AudioEngine engine (session);
    engine.prepare (48000.0);
    std::vector<float> left (48000 * 3), right (left.size());
    for (size_t pos = 0; pos < left.size(); pos += 512)
    {
        float* channels[] { left.data() + pos, right.data() + pos };
        engine.render (channels, 2, static_cast<int> (std::min<size_t> (512, left.size() - pos)));
    }
    float peak = 0.0f;
    for (size_t i = 0; i < left.size(); ++i)
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
    CAPTURE (peak);
    CHECK (peak > 0.3f);
    CHECK (peak < 0.95f);
}
