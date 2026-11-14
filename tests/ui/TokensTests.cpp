// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// Pins the numbers of the written spec (design-system.md). Change the spec first, then the test.

#include <catch2/catch_test_macros.hpp>

#include "ui/theme/Tokens.h"

using namespace tekno::ui::tokens;

TEST_CASE ("typography sizes stay inside the spec ranges", "[ui][tokens]")
{
    CHECK (type::pageTitle.size >= 72.0f);
    CHECK (type::pageTitle.size <= 96.0f);
    CHECK (type::pageTitle.uppercase);
    CHECK (type::pageTitle.family == Family::condensed);

    CHECK (type::body.size >= 15.0f);
    CHECK (type::body.size <= 16.0f);
    CHECK (type::meta.size == 13.0f);
    CHECK (type::sectionTitle.size == 17.0f);
    CHECK (type::sectionTitle.weight == 600);

    CHECK_FALSE (type::wordmark.uppercase); // the wordmark is lowercase
}

TEST_CASE ("shapes follow the spec", "[ui][tokens]")
{
    CHECK (shape::lineWidth == 1.0f);
    CHECK (shape::iconStroke == 1.5f);
    CHECK (shape::pillHeight == 36.0f);
    CHECK (shape::iconButtonSize == 40.0f);
    CHECK (shape::badgeSize == 18.0f);
    CHECK (shape::fieldRadius >= 10.0f);
    CHECK (shape::fieldRadius <= 12.0f);
    CHECK (shape::contentRadius >= 2.0f);
    CHECK (shape::contentRadius <= 4.0f);
}

TEST_CASE ("layout and motion follow the spec", "[ui][tokens]")
{
    CHECK (space::gap == 12.0f);
    CHECK (space::gridColumns == 4);
    CHECK (space::leftColumn == 250.0f);
    CHECK (motion::fastMs >= 120);
    CHECK (motion::normalMs <= 150);
}

TEST_CASE ("palette() maps themes", "[ui][tokens]")
{
    CHECK (&palette (Theme::light) == &lightPalette);
    CHECK (&palette (Theme::dark) == &darkPalette);
    CHECK (lightPalette.bg == 0xffffffff);
    CHECK (darkPalette.bg == 0xff111111);
}
