// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
namespace
{
    const juce::Identifier roleProperty { "teknoRole" };
} // namespace

void setRole (juce::Component& component, Role role)
{
    component.getProperties().set (roleProperty, static_cast<int> (role));
}

Role getRole (const juce::Component& component)
{
    return static_cast<Role> (static_cast<int> (component.getProperties().getWithDefault (roleProperty, 0)));
}

TeknoLookAndFeel::TeknoLookAndFeel (tokens::Theme initialTheme)
    : theme (initialTheme)
{
    applyColours();
}

void TeknoLookAndFeel::setTheme (tokens::Theme newTheme)
{
    theme = newTheme;
    applyColours();
}

void TeknoLookAndFeel::applyColours()
{
    using P = tokens::Palette;
    const auto bg = colour (&P::bg), surface = colour (&P::surface), ink = colour (&P::ink),
               muted = colour (&P::inkMuted), line = colour (&P::line);

    // windowBackground, widgetBackground, menuBackground, outline, defaultText,
    // defaultFill, highlightedText, highlightedFill, menuText
    setColourScheme ({ bg, bg, surface, line, ink, ink, bg, ink, ink });

    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::textColourId, ink);
    setColour (juce::TextButton::buttonColourId, bg);
    setColour (juce::TextButton::buttonOnColourId, ink);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, bg);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::TooltipWindow::backgroundColourId, ink);
    setColour (juce::TooltipWindow::textColourId, bg);
    setColour (juce::TooltipWindow::outlineColourId, ink);
    setColour (juce::ScrollBar::thumbColourId, line);
    setColour (juce::Slider::rotarySliderOutlineColourId, line);
    setColour (juce::Slider::rotarySliderFillColourId, ink);
    setColour (juce::Slider::textBoxTextColourId, muted);
    setColour (juce::CaretComponent::caretColourId, ink);
}

void TeknoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                             bool isHighlighted, bool isDown)
{
    using P = tokens::Palette;
    const auto role = getRole (button);
    const auto bounds = button.getLocalBounds().toFloat().reduced (tokens::shape::lineWidth * 0.5f);
    const auto on = button.getToggleState();

    if (role == Role::nav)
    {
        if (on) // the active view is marked by a dot, not by a colour
        {
            const auto d = tokens::shape::navDotSize;
            g.setColour (colour (&P::ink));
            g.fillEllipse (bounds.getCentreX() - d * 0.5f, bounds.getBottom() - d, d, d);
        }
        return;
    }

    const auto radius = bounds.getHeight() * 0.5f; // pill: full radius

    juce::Colour fill, edge;
    if (role == Role::primary)
    {
        fill = colour (&P::accent);
        edge = colour (&P::accentOutline);
    }
    else
    {
        fill = on ? colour (&P::ink) : colour (&P::bg);
        edge = (on || isHighlighted) ? colour (&P::ink) : colour (&P::line);
    }

    if (isDown)
        fill = fill.interpolatedWith (colour (&P::surface), 0.25f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (edge);
    g.drawRoundedRectangle (bounds, radius, tokens::shape::lineWidth);
}

void TeknoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    using P = tokens::Palette;
    const auto role = getRole (button);
    const auto on = button.getToggleState();

    juce::Colour text = colour (&P::ink);
    if (role == Role::primary)
        text = colour (&P::onAccent);
    else if (role == Role::nav)
        text = on ? colour (&P::ink) : colour (&P::inkMuted);
    else if (on)
        text = colour (&P::bg);

    auto area = button.getLocalBounds();
    if (role == Role::nav)
        area.removeFromBottom (static_cast<int> (tokens::shape::navDotSize * 2.0f)); // room for the dot

    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.isEnabled() ? text : text.withMultipliedAlpha (0.4f));
    g.drawText (Fonts::format (tokens::type::control, button.getButtonText()), area, juce::Justification::centred,
                false);
}

void TeknoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    // A thin ring and the value as an ink arc: no 3D, no pointer, no gradient.
    using P = tokens::Palette;
    const auto size = static_cast<float> (juce::jmin (width, height));
    const auto ring = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                              static_cast<float> (width), static_cast<float> (height))
                          .withSizeKeepingCentre (size, size)
                          .reduced (tokens::shape::lineWidth);
    const auto active = slider.isMouseOverOrDragging();

    g.setColour (active ? colour (&P::ink) : colour (&P::line));
    g.drawEllipse (ring, tokens::shape::lineWidth);

    const auto radius = ring.getWidth() * 0.5f - 4.0f;
    juce::Path arc;
    arc.addCentredArc (ring.getCentreX(), ring.getCentreY(), radius, radius, 0.0f, startAngle,
                       startAngle + (endAngle - startAngle) * position, true);
    g.setColour (colour (&P::ink));
    g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void TeknoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                         juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    // A fader: a thin track, ink from the bottom up to the cap, the cap a flat ink bar.
    using P = tokens::Palette;
    const auto area = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                              static_cast<float> (width), static_cast<float> (height));
    const auto centreX = area.getCentreX();
    const auto active = slider.isMouseOverOrDragging();

    g.setColour (active ? colour (&P::ink) : colour (&P::line));
    g.fillRect (centreX - tokens::shape::lineWidth * 0.5f, area.getY(), tokens::shape::lineWidth, area.getHeight());
    g.setColour (colour (&P::ink));
    g.fillRect (centreX - 1.0f, sliderPos, 2.0f, area.getBottom() - sliderPos);

    const auto cap = juce::Rectangle<float> (tokens::shape::faderWidth, 10.0f).withCentre ({ centreX, sliderPos });
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (colour (&P::bg));
    g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 10.0f, tokens::shape::lineWidth));
}

int TeknoLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    return slider.getSliderStyle() == juce::Slider::LinearVertical ? 6 : LookAndFeel_V4::getSliderThumbRadius (slider);
}

juce::Font TeknoLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return typefaces.font (tokens::type::control);
}

juce::Font TeknoLookAndFeel::getLabelFont (juce::Label&)
{
    return typefaces.font (tokens::type::body);
}

juce::Font TeknoLookAndFeel::getPopupMenuFont()
{
    return typefaces.font (tokens::type::body);
}

juce::Typeface::Ptr TeknoLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    // Any font that did not ask for a specific face gets Inter, so stray JUCE widgets match too.
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return typefaces.typeface (tokens::Family::sans, font.isBold() ? 700 : 400);

    return juce::LookAndFeel_V4::getTypefaceForFont (font);
}
} // namespace tekno::ui
