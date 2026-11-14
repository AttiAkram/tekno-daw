// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <optional>

#include <juce_core/juce_core.h>

#include "ui/theme/Tokens.h"

namespace tekno::app
{
/** Command-line options. Most exist so that the app can be driven by scripts and CI. */
struct AppOptions
{
    bool printVersion = false;                          // --version          print one line and exit
    bool printHelp = false;                             // --help
    bool smokeTest = false;                             // --smoke-test       open the window, quit after a second
    juce::File snapshot;                                // --snapshot=<png>   render the main window to a PNG and quit
    juce::File render;                                  // --render=<wav>     render the pattern, no window/device
    int bars = 4;                                       // --bars=<1..64>     length of --render
    bool noAudio = false;                               // --no-audio         don't open an audio device
    juce::File project;                                 // <file.tekno>       open this project at start
    ui::tokens::Theme theme = ui::tokens::defaultTheme; // --theme=dark|light
    juce::StringArray errors;                           // unknown or malformed arguments

    static AppOptions parse (const juce::StringArray& arguments);
    static juce::String helpText();
};
} // namespace tekno::app
