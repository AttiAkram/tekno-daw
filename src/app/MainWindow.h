// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::app
{
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, ui::TeknoLookAndFeel& teknoLookAndFeel);

    void closeButtonPressed() override;
    void lookAndFeelChanged() override;

private:
    ui::TeknoLookAndFeel& lnf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
} // namespace tekno::app
