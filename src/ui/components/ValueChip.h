// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** A pill showing a number (e.g. "130 BPM") that you change by dragging up/down or with the wheel.
    Shift = fine steps. Double-click = back to the default. */
class ValueChip final : public juce::Component
{
public:
    ValueChip (double minimum, double maximum, double defaultValue, std::function<juce::String (double)> format);

    void setValue (double newValue); // does not call onChange
    double getValue() const noexcept { return value; }
    bool isBeingDragged() const noexcept { return dragging; }

    std::function<void (double)> onChange;
    std::function<void()> onDragStart, onDragEnd; // a drag is one gesture (one undo step)

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void change (double newValue);

    double minimum, maximum, defaultValue, value;
    std::function<juce::String (double)> format;
    double dragStartValue = 0.0;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueChip)
};
} // namespace tekno::ui
