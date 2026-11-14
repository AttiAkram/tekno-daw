// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// The palette is a design decision (ADR 0007) with measurable properties. These tests keep a future
// "small tweak" to a colour from silently breaking readability.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/theme/Contrast.h"
#include "ui/theme/Tokens.h"

using namespace tekno::ui;
using Catch::Matchers::WithinAbs;

TEST_CASE ("contrast ratio follows WCAG", "[ui][contrast]")
{
    CHECK_THAT (contrastRatio (0xff000000, 0xffffffff), WithinAbs (21.0, 1e-9));
    CHECK_THAT (contrastRatio (0xff777777, 0xff777777), WithinAbs (1.0, 1e-9));
    CHECK (contrastRatio (0xff111111, 0xffffffff) == contrastRatio (0xffffffff, 0xff111111));
    CHECK_THAT (contrastRatio (0xff6b6b6b, 0xffffffff), WithinAbs (5.33, 0.01)); // value in design-system.md
}

TEST_CASE ("every theme keeps text readable", "[ui][palette]")
{
    const auto theme = GENERATE (tokens::Theme::dark, tokens::Theme::light);
    const auto& p = tokens::palette (theme);
    CAPTURE (theme == tokens::Theme::dark ? "dark" : "light");

    CHECK (contrastRatio (p.ink, p.bg) >= wcag::normalText);
    CHECK (contrastRatio (p.ink, p.surface) >= wcag::normalText);
    CHECK (contrastRatio (p.inkMuted, p.bg) >= wcag::normalText);
    CHECK (contrastRatio (p.inkMuted, p.surface) >= wcag::normalText);
    CHECK (contrastRatio (p.bg, p.ink) >= wcag::normalText); // text on an active (filled) pill
    CHECK (contrastRatio (p.onAccent, p.accent) >= wcag::normalText);
    CHECK (contrastRatio (p.onOk, p.ok) >= wcag::normalText);
    CHECK (contrastRatio (p.onWarn, p.warn) >= wcag::normalText);
}

TEST_CASE ("the yellow always has a visible edge", "[ui][palette]")
{
    const auto theme = GENERATE (tokens::Theme::dark, tokens::Theme::light);
    const auto& p = tokens::palette (theme);

    // Yellow on white is 1.41:1: the shape itself would disappear, so accentOutline must carry it.
    CHECK (contrastRatio (p.accentOutline, p.bg) >= wcag::graphics);
}

TEST_CASE ("dividers stay quiet", "[ui][palette]")
{
    const auto theme = GENERATE (tokens::Theme::dark, tokens::Theme::light);
    const auto& p = tokens::palette (theme);

    // Lines separate, they must not compete with ink.
    CHECK (contrastRatio (p.line, p.bg) < 2.0);
}

TEST_CASE ("both themes share the single accent", "[ui][palette]")
{
    CHECK (tokens::lightPalette.accent == tokens::darkPalette.accent);
    CHECK (tokens::lightPalette.accent == 0xffffd60a);
}
