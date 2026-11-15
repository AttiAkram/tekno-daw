// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_core/juce_core.h>

namespace tekno::ui
{
/** What the views show about the audio device. Filled by the app, which owns the device. */
struct AudioStatus
{
    bool running = false;
    juce::String driver; // "JACK" or "ALSA"
    juce::String deviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    int outputLatency = 0;             // samples the device adds after our buffer, as it reports them
    bool outputLatencyGuessed = false; // it reported nothing (JACK through PipeWire): one buffer assumed
    int dropouts = -1;                 // blocks the device missed since it started, -1 when it can't tell
    juce::String problem;              // why there is no audio, when not running
};
} // namespace tekno::ui
