// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "app/MainWindow.h"

#include "ui/preview/StylePreview.h"

namespace tekno::app
{
MainWindow::MainWindow (const juce::String& name, ui::TeknoLookAndFeel& teknoLookAndFeel)
    : juce::DocumentWindow (name, teknoLookAndFeel.colour (&ui::tokens::Palette::bg), juce::DocumentWindow::allButtons)
    , lnf (teknoLookAndFeel)
{
    setUsingNativeTitleBar (true); // the window manager (e.g. a tiling one) owns the frame
    setContentOwned (new ui::StylePreview (lnf), false);
    setResizable (true, false);
    setResizeLimits (1024, 640, 16384, 16384);
    centreWithSize (1440, 900);
    setVisible (true);

    if (auto* content = getContentComponent())
        content->grabKeyboardFocus();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::lookAndFeelChanged()
{
    juce::DocumentWindow::lookAndFeelChanged();
    setBackgroundColour (lnf.colour (&ui::tokens::Palette::bg));
}
} // namespace tekno::app
