// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace tekno::core
{
/** A sound a sampler can play: one or two channels of float audio. Immutable once made, so the audio
    thread can read it while the message thread looks at it, without locks. Owned by SampleLibrary. */
struct Sample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    juce::String origin; // "builtin:kick" or an absolute file path: how the project refers to it

    int length() const noexcept { return audio.getNumSamples(); }
    double seconds() const noexcept { return length() / sampleRate; }
};
} // namespace tekno::core
