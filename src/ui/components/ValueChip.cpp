// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/components/ValueChip.h"

namespace tekno::ui
{
namespace
{
    constexpr double coarseStepPerPixel = 0.5;
    constexpr double fineStepPerPixel = 0.05;
} // namespace

ValueChip::ValueChip (double min, double max, double def, std::function<juce::String (double)> formatter)
    : minimum (min)
    , maximum (max)
    , defaultValue (def)
    , value (def)
    , format (std::move (formatter))
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (true);
}

void ValueChip::setValue (double newValue)
{
    newValue = std::clamp (newValue, minimum, maximum);
    if (! juce::exactlyEqual (newValue, value))
    {
        value = newValue;
        repaint();
    }
}

void ValueChip::change (double newValue)
{
    const auto before = value;
    setValue (newValue);
    if (! juce::exactlyEqual (value, before) && onChange != nullptr)
        onChange (value);
}

void ValueChip::paint (juce::Graphics& g)
{
    auto* lnf = dynamic_cast<TeknoLookAndFeel*> (&getLookAndFeel());
    if (lnf == nullptr)
        return;

    using P = tokens::Palette;
    const auto bounds = getLocalBounds().toFloat().reduced (tokens::shape::lineWidth * 0.5f);
    const auto radius = bounds.getHeight() * 0.5f;
    g.setColour (lnf->colour (&P::bg));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (dragging || isMouseOver() ? lnf->colour (&P::ink) : lnf->colour (&P::line));
    g.drawRoundedRectangle (bounds, radius, tokens::shape::lineWidth);

    g.setFont (lnf->fonts().font (tokens::type::control));
    g.setColour (lnf->colour (&P::ink));
    g.drawText (Fonts::format (tokens::type::control, format (value)), bounds, juce::Justification::centred, false);
}

void ValueChip::mouseDown (const juce::MouseEvent&)
{
    dragStartValue = value;
    dragging = true;
    if (onDragStart)
        onDragStart();
    repaint();
}

void ValueChip::mouseDrag (const juce::MouseEvent& e)
{
    const auto perPixel = e.mods.isShiftDown() ? fineStepPerPixel : coarseStepPerPixel;
    const auto raw = dragStartValue - e.getDistanceFromDragStartY() * perPixel;
    change (e.mods.isShiftDown() ? std::round (raw * 10.0) / 10.0 : std::round (raw));
}

void ValueChip::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    if (onDragEnd)
        onDragEnd();
    repaint();
}

void ValueChip::mouseDoubleClick (const juce::MouseEvent&)
{
    change (defaultValue);
}

void ValueChip::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto step = e.mods.isShiftDown() ? 0.1 : 1.0;
    change (value + (wheel.deltaY > 0 ? step : -step));
}
} // namespace tekno::ui
