// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>
#include <memory>

#include <juce_data_structures/juce_data_structures.h>

#include "core/model/NoteNames.h"
#include "ui/theme/Tokens.h"

namespace tekno::ui
{
/** The person's settings, kept between runs in $XDG_CONFIG_HOME/tekno-daw/settings.xml (by default
    ~/.config/tekno-daw/). They belong to the person, not to a project: no project file holds them.
    Every change is written at once. */
class Preferences
{
public:
    Preferences();                                 // the default location
    explicit Preferences (const juce::File& file); // tests

    static juce::File defaultFile();

    core::NoteNaming noteNaming() const;
    void setNoteNaming (core::NoteNaming naming);
    static std::optional<core::NoteNaming> namingFromName (const juce::String& name);
    static juce::String namingName (core::NoteNaming naming);

    juce::String snap() const; // a PianoRollView snap name, "step" by default
    void setSnap (const juce::String& name);

    // Playing live and the audio device.
    int audioBuffer() const; // samples per audio block to ask the device for (default 256: ~6 ms)
    void setAudioBuffer (int samples);
    juce::String audioDriver() const; // "jack" (default: ALSA when no JACK server answers) or "alsa"
    void setAudioDriver (const juce::String& driver);
    std::array<int, 2> liveOctaves() const; // acid (default 2), drums (default 4)
    int liveVelocity() const;
    bool liveShortGate() const;
    void setLive (std::array<int, 2> octaves, int velocity, bool shortGate);

    bool scaleLock() const; // new notes land on the key's scale (on by default)
    void setScaleLock (bool on);
    bool preview() const; // hear notes as they are placed (on by default)
    void setPreview (bool on);

    juce::String tiles() const; // the panel layout, as TileLayout::toString; empty = one panel
    void setTiles (const juce::String& layout);

    tokens::Theme theme() const;
    void setTheme (tokens::Theme theme);

private:
    std::unique_ptr<juce::PropertiesFile> properties;
};
} // namespace tekno::ui
