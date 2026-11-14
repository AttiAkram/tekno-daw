// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include "ui/theme/Tokens.h"

namespace tekno::ui
{
/** The typefaces embedded in the binary (assets/fonts), turned into juce::Fonts from tokens.
    Loading parses the font files, so create one instance and share it: TeknoLookAndFeel owns it. */
class Fonts
{
public:
    Fonts();

    /** The embedded face closest to the requested weight. */
    juce::Typeface::Ptr typeface (tokens::Family family, int weight) const;

    juce::Font font (const tokens::TextStyle& style) const;

    /** Applies the style's case rule: "acid 01" -> "ACID 01" for uppercase styles. */
    static juce::String format (const tokens::TextStyle& style, const juce::String& text);

private:
    juce::Typeface::Ptr sansRegular, sansSemiBold, sansBold, condensedExtraBold;
};
} // namespace tekno::ui
