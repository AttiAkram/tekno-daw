// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_core/juce_core.h>

#include "core/model/Session.h"

namespace tekno::core
{
/** Renders `bars` bars of the session's pattern to a 24-bit stereo WAV, plus a short tail, with no
    audio device. Uses its own engine, so it must not run while a device is playing this session:
    it starts and stops the session's transport. */
juce::Result renderToWav (Session& session, const juce::File& file, int bars, double sampleRate = 48000.0);
} // namespace tekno::core
