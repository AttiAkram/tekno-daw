// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <catch2/catch_test_macros.hpp>

#include "core/model/Session.h"

using namespace tekno::core;

TEST_CASE ("a step survives packing into 32 bits", "[core][pattern]")
{
    for (const auto& step :
         { Step {}, Step { true, true, true, 96 }, Step { true, false, true, 24 }, Step { false, true, false, 60 } })
        CHECK (Step::unpack (step.pack()) == step);
}

TEST_CASE ("notes outside the playable range are clamped when stored", "[core][pattern]")
{
    CHECK (Step::unpack (Step { true, false, false, 200 }.pack()).note == Step::maxNote);
    CHECK (Step::unpack (Step { true, false, false, -5 }.pack()).note == Step::minNote);
}

TEST_CASE ("pattern ignores out-of-range steps", "[core][pattern]")
{
    Pattern pattern;
    pattern.setStep (16, Step { true });
    pattern.setStep (-1, Step { true });
    CHECK (pattern.getStep (16) == Step {});
    for (int i = 0; i < Pattern::numSteps; ++i)
        CHECK_FALSE (pattern.getStep (i).on);
}

TEST_CASE ("the default session has a playable acid line", "[core][pattern]")
{
    Session session;
    int on = 0, accents = 0, slides = 0;
    for (int i = 0; i < Pattern::numSteps; ++i)
    {
        const auto s = session.pattern.getStep (i);
        on += s.on ? 1 : 0;
        accents += s.accent ? 1 : 0;
        slides += s.slide ? 1 : 0;
    }
    CHECK (on == 12);
    CHECK (accents == 3);
    CHECK (slides == 4);
    CHECK_FALSE (session.transport.playing.load());
}
