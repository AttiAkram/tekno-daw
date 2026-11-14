// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tekno::ui
{
/** Round 40px button with a thin line icon (design-system.md, "Forme").
    The icon is a juce::Path in a 0..1 box: it scales cleanly and follows the theme.
    When toggled on with Role::primary it turns yellow (e.g. record armed). */
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, juce::Path icon);

    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

    /** Icons, drawn in a unit square. */
    static juce::Path playIcon();
    static juce::Path stopIcon();
    static juce::Path recordIcon();

private:
    juce::Path icon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};
} // namespace tekno::ui
