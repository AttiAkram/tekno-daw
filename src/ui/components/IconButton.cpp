// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/components/IconButton.h"

#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
IconButton::IconButton (const juce::String& name, juce::Path iconToUse)
    : juce::Button (name)
    , icon (std::move (iconToUse))
{
    setMouseClickGrabsKeyboardFocus (false);
}

void IconButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto* lnf = dynamic_cast<TeknoLookAndFeel*> (&getLookAndFeel());
    if (lnf == nullptr)
        return;

    using P = tokens::Palette;
    const auto size =
        juce::jmin (static_cast<float> (getWidth()), static_cast<float> (getHeight()), tokens::shape::iconButtonSize);
    const auto circle =
        getLocalBounds().toFloat().withSizeKeepingCentre (size, size).reduced (tokens::shape::lineWidth * 0.5f);

    const auto active = getToggleState();
    const auto primary = active && getRole (*this) == Role::primary;

    auto fill = primary ? lnf->colour (&P::accent) : active ? lnf->colour (&P::ink) : lnf->colour (&P::bg);
    auto edge = primary                     ? lnf->colour (&P::accentOutline)
                : (active || isHighlighted) ? lnf->colour (&P::ink)
                                            : lnf->colour (&P::line);
    auto stroke = primary ? lnf->colour (&P::onAccent) : active ? lnf->colour (&P::bg) : lnf->colour (&P::ink);

    if (isDown)
        fill = fill.interpolatedWith (lnf->colour (&P::surface), 0.25f);

    g.setColour (fill);
    g.fillEllipse (circle);
    g.setColour (edge);
    g.drawEllipse (circle, tokens::shape::lineWidth);

    const auto iconBox = circle.withSizeKeepingCentre (size * 0.35f, size * 0.35f);
    auto path = icon;
    path.applyTransform (juce::AffineTransform::scale (iconBox.getWidth(), iconBox.getHeight())
                             .translated (iconBox.getX(), iconBox.getY()));
    g.setColour (stroke);
    g.strokePath (path, juce::PathStrokeType (tokens::shape::iconStroke, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

juce::Path IconButton::playIcon()
{
    juce::Path p;
    p.addTriangle (0.15f, 0.0f, 0.15f, 1.0f, 1.0f, 0.5f);
    return p;
}

juce::Path IconButton::stopIcon()
{
    juce::Path p;
    p.addRectangle (0.1f, 0.1f, 0.8f, 0.8f);
    return p;
}

juce::Path IconButton::recordIcon()
{
    juce::Path p;
    p.addEllipse (0.0f, 0.0f, 1.0f, 1.0f);
    return p;
}
} // namespace tekno::ui
