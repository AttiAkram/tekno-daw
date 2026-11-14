// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/theme/Fonts.h"

using namespace tekno::ui;

TEST_CASE ("embedded fonts load and are the expected families", "[ui][fonts]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const Fonts fonts;

    const auto sans = fonts.typeface (tokens::Family::sans, 400);
    const auto condensed = fonts.typeface (tokens::Family::condensed, 800);
    REQUIRE (sans != nullptr);
    REQUIRE (condensed != nullptr);

    CHECK (sans->getName().containsIgnoreCase ("Inter"));
    CHECK (condensed->getName().containsIgnoreCase ("Barlow"));
    CHECK (fonts.typeface (tokens::Family::sans, 700) != sans); // bold is a real face, not faked
}

TEST_CASE ("uppercase styles format their text", "[ui][fonts]")
{
    CHECK (Fonts::format (tokens::type::pageTitle, "acid 01") == "ACID 01");
    CHECK (Fonts::format (tokens::type::body, "acid 01") == "acid 01");
}
