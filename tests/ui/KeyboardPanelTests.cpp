// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "ui/views/KeyboardPanel.h"

using tekno::ui::KeyboardPanel;

TEST_CASE ("the PC keyboard plays two octaves, laid out as in trackers", "[ui][keyboard]")
{
    // The lower octave on the bottom row, its black keys on the row above.
    const std::string lower = "zsxdcvgbhnjm,l.";
    for (size_t i = 0; i < lower.size(); ++i)
        CHECK (KeyboardPanel::semitoneFor (static_cast<juce::juce_wchar> (lower[i])) == static_cast<int> (i));

    // The upper octave from Q, its black keys on the digits.
    const std::string upper = "q2w3er5t6y7ui9o0p";
    for (size_t i = 0; i < upper.size(); ++i)
        CHECK (KeyboardPanel::semitoneFor (static_cast<juce::juce_wchar> (upper[i])) == 12 + static_cast<int> (i));

    CHECK (KeyboardPanel::semitoneFor ('Z') == 0); // Shift is the accent, not another note
    CHECK (KeyboardPanel::semitoneFor ('Q') == 12);

    // No black key between E and F, nor between B and C.
    for (const auto gap : { 'a', 'f', 'k', '1', '4', '8' })
        CHECK_FALSE (KeyboardPanel::semitoneFor (static_cast<juce::juce_wchar> (gap)));
}
