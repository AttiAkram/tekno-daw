// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace tekno::ui
{
/** How a control should be drawn, beyond what its JUCE class says. */
enum class Role
{
    standard, // pill, 1px border, filled ink when on
    primary,  // the yellow pill: play/record only (ADR 0007)
    nav,      // text only, a dot under it when on
};

void setRole (juce::Component& component, Role role);
Role getRole (const juce::Component& component);

/** The single LookAndFeel of the app. Every colour, font and size it uses comes from Tokens.h.
    Flat on purpose: no gradients, no shadows (ADR 0007).

    After setTheme(), call sendLookAndFeelChange() on the top-level component to repaint. */
class TeknoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit TeknoLookAndFeel (tokens::Theme theme = tokens::defaultTheme);

    void setTheme (tokens::Theme newTheme);
    tokens::Theme getTheme() const noexcept { return theme; }
    const tokens::Palette& palette() const noexcept { return tokens::palette (theme); }
    const Fonts& fonts() const noexcept { return typefaces; }

    /** Shorthand: lnf.colour (&tokens::Palette::ink) */
    juce::Colour colour (std::uint32_t tokens::Palette::* token) const noexcept
    {
        return juce::Colour (palette().*token);
    }

    // juce::LookAndFeel_V4
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isHighlighted,
                               bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position, float startAngle,
                           float endAngle, juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

private:
    void applyColours();

    tokens::Theme theme;
    Fonts typefaces;
};
} // namespace tekno::ui
