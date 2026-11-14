// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "app/AppOptions.h"

namespace tekno::app
{
AppOptions AppOptions::parse (const juce::StringArray& arguments)
{
    AppOptions options;

    for (const auto& arg : arguments)
    {
        if (arg == "--version")
            options.printVersion = true;
        else if (arg == "--help" || arg == "-h")
            options.printHelp = true;
        else if (arg == "--smoke-test")
            options.smokeTest = true;
        else if (arg.startsWith ("--snapshot="))
            options.snapshot =
                juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false));
        else if (arg.startsWith ("--render="))
            options.render =
                juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false));
        else if (arg.startsWith ("--bars="))
        {
            options.bars = arg.fromFirstOccurrenceOf ("=", false, false).getIntValue();
            if (options.bars < 1 || options.bars > 64)
                options.errors.add ("--bars must be between 1 and 64");
        }
        else if (arg == "--no-audio")
            options.noAudio = true;
        else if (arg == "--theme=dark")
            options.theme = ui::tokens::Theme::dark;
        else if (arg == "--theme=light")
            options.theme = ui::tokens::Theme::light;
        else
            options.errors.add ("unknown argument: " + arg);
    }

    return options;
}

juce::String AppOptions::helpText()
{
    return "usage: tekno-daw [options]\n"
           "  --theme=dark|light   start with this theme\n"
           "  --snapshot=<file>    render the main window to a PNG and exit\n"
           "  --render=<file.wav>  render the pattern to a WAV (no window, no audio device) and exit\n"
           "  --bars=<n>           how many bars --render writes (default 4)\n"
           "  --no-audio           start without opening an audio device\n"
           "  --smoke-test         open the window, quit after one second (used by tests)\n"
           "  --version            print the version and exit\n"
           "  --help               show this text\n";
}
} // namespace tekno::app
