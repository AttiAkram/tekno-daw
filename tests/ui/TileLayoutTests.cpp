// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <catch2/catch_test_macros.hpp>

#include "ui/TileLayout.h"

using tekno::ui::TileLayout;
using Panel = TileLayout::Panel;

namespace
{
const juce::Rectangle<float> screen { 0.0f, 0.0f, 1200.0f, 600.0f };
constexpr float gap = 10.0f;
} // namespace

TEST_CASE ("opening panels splits the active one along its longer side", "[ui][tiles]")
{
    TileLayout tiles;
    CHECK (tiles.count() == 1);
    tiles.open (Panel::pianoRoll, screen, gap); // wide screen: side by side
    auto t = tiles.tiles (screen, gap);
    REQUIRE (t.size() == 2);
    CHECK (t[0].panel == Panel::pattern);
    CHECK (t[1].panel == Panel::pianoRoll);
    CHECK (t[0].bounds.getRight() + gap == t[1].bounds.getX());
    CHECK (tiles.active() == Panel::pianoRoll);

    tiles.open (Panel::mixer, screen, gap); // the piano roll is 595 x 600, near square: split top/bottom
    t = tiles.tiles (screen, gap);
    REQUIRE (t.size() == 3);
    CHECK (t[1].panel == Panel::pianoRoll);
    CHECK (t[2].panel == Panel::mixer);
    CHECK (t[1].bounds.getX() == t[2].bounds.getX());
    CHECK (t[2].bounds.getY() > t[1].bounds.getY());

    tiles.open (Panel::pattern, screen, gap); // already open: it just becomes active
    CHECK (tiles.count() == 3);
    CHECK (tiles.active() == Panel::pattern);
}

TEST_CASE ("closing gives the space back, and the last panel stays", "[ui][tiles]")
{
    TileLayout tiles;
    tiles.open (Panel::pianoRoll, screen, gap);
    tiles.open (Panel::mixer, screen, gap);
    REQUIRE (tiles.close()); // the mixer
    CHECK (tiles.count() == 2);
    CHECK_FALSE (tiles.isOpen (Panel::mixer));
    CHECK (tiles.active() == Panel::pianoRoll);
    CHECK (tiles.tiles (screen, gap)[1].bounds.getHeight() == screen.getHeight());

    REQUIRE (tiles.close());
    CHECK_FALSE (tiles.close()); // one left
    CHECK (tiles.panels() == std::vector<Panel> { Panel::pattern });
}

TEST_CASE ("show replaces the active panel; focus moves around", "[ui][tiles]")
{
    TileLayout tiles;
    tiles.open (Panel::pianoRoll, screen, gap);
    tiles.show (Panel::mixer); // where the piano roll was
    CHECK (tiles.panels() == std::vector<Panel> { Panel::pattern, Panel::mixer });
    tiles.show (Panel::pattern); // open elsewhere: just focus it
    CHECK (tiles.active() == Panel::pattern);
    CHECK (tiles.count() == 2);
    tiles.focusNext (1);
    CHECK (tiles.active() == Panel::mixer);
    tiles.focusNext (1);
    CHECK (tiles.active() == Panel::pattern);
    tiles.focusNext (-1);
    CHECK (tiles.active() == Panel::mixer);
    tiles.single();
    CHECK (tiles.panels() == std::vector<Panel> { Panel::mixer });
}

TEST_CASE ("dividers resize within limits, and the layout is saved as text", "[ui][tiles]")
{
    TileLayout tiles;
    tiles.open (Panel::pianoRoll, screen, gap);
    tiles.open (Panel::mixer, screen, gap);
    const auto dividers = tiles.dividers (screen, gap);
    REQUIRE (dividers.size() == 2);
    CHECK (dividers[0].vertical);
    tiles.setRatio (dividers[0].id, 0.95f); // limited to 80%
    CHECK (tiles.tiles (screen, gap)[0].bounds.getWidth() == std::round ((1200.0f - gap) * 0.8f));

    const auto text = tiles.toString();
    CHECK (text == "h(0.800,pattern,v(0.500,pianoroll,mixer));mixer");
    const auto again = TileLayout::fromString (text);
    REQUIRE (again.has_value());
    CHECK (again->toString() == text);

    CHECK_FALSE (TileLayout::fromString ("h(0.5,pattern,pattern)").has_value()); // a panel once
    CHECK_FALSE (TileLayout::fromString ("h(0.5,pattern").has_value());
    CHECK_FALSE (TileLayout::fromString ("song").has_value());
    CHECK (TileLayout::fromString ("mixer")->panels() == std::vector<Panel> { Panel::mixer });
}
