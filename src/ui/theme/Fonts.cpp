// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/theme/Fonts.h"

#include <TeknoFonts.h>

namespace tekno::ui
{
namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        auto face = juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size));
        jassert (face != nullptr); // a font file in assets/fonts is missing or corrupt
        return face;
    }
} // namespace

Fonts::Fonts()
    : sansRegular (load (tekno_fonts::InterRegular_ttf, tekno_fonts::InterRegular_ttfSize))
    , sansSemiBold (load (tekno_fonts::InterSemiBold_ttf, tekno_fonts::InterSemiBold_ttfSize))
    , sansBold (load (tekno_fonts::InterBold_ttf, tekno_fonts::InterBold_ttfSize))
    , condensedExtraBold (
          load (tekno_fonts::BarlowCondensedExtraBold_ttf, tekno_fonts::BarlowCondensedExtraBold_ttfSize))
{
}

juce::Typeface::Ptr Fonts::typeface (tokens::Family family, int weight) const
{
    if (family == tokens::Family::condensed)
        return condensedExtraBold; // the only condensed weight the design uses

    if (weight >= 700)
        return sansBold;
    if (weight >= 550)
        return sansSemiBold;
    return sansRegular;
}

juce::Font Fonts::font (const tokens::TextStyle& style) const
{
    // FontOptions{} has a default style ("Regular"), which withTypeface() asserts against in JUCE 9:
    // the typeface constructor is the supported way to name an exact face.
    return juce::Font (juce::FontOptions (typeface (style.family, style.weight))
                           .withPointHeight (style.size)
                           .withKerningFactor (style.tracking));
}

juce::String Fonts::format (const tokens::TextStyle& style, const juce::String& text)
{
    return style.uppercase ? text.toUpperCase() : text;
}
} // namespace tekno::ui
