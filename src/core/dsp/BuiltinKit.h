// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace tekno::core::dsp::builtinKit
{
/** The built-in drum kit is synthesised by code, so the DAW plays a loop on first start without
    shipping anyone else's samples. Deterministic: the same sounds on every run. */
inline constexpr double sampleRate = 48000.0;

/** Mono audio for "kick", "hat", "openhat" or "clap"; empty for anything else. Peaks at about 0.9. */
juce::AudioBuffer<float> make (const juce::String& name);
} // namespace tekno::core::dsp::builtinKit
