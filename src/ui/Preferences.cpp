// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/Preferences.h"

namespace tekno::ui
{
namespace
{
    juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "tekno-daw";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        o.millisecondsBeforeSaving = 0; // write each change at once: a crash loses nothing
        return o;
    }
} // namespace

juce::File Preferences::defaultFile()
{
    const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_CONFIG_HOME", {});
    const auto base = xdg.isNotEmpty() && juce::File::isAbsolutePath (xdg)
                          ? juce::File (xdg)
                          : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config");
    return base.getChildFile ("tekno-daw").getChildFile ("settings.xml");
}

Preferences::Preferences()
    : Preferences (defaultFile())
{
}

Preferences::Preferences (const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    properties = std::make_unique<juce::PropertiesFile> (file, options());
}

std::optional<core::NoteNaming> Preferences::namingFromName (const juce::String& name)
{
    const auto n = name.trim().toLowerCase();
    if (n == "latin" || n == "doremi" || n == "do" || n == "italian" || n == "solfege")
        return core::NoteNaming::latin;
    if (n == "letters" || n == "abc" || n == "c" || n == "english")
        return core::NoteNaming::letters;
    return std::nullopt;
}

juce::String Preferences::namingName (core::NoteNaming naming)
{
    return naming == core::NoteNaming::latin ? "latin" : "letters";
}

core::NoteNaming Preferences::noteNaming() const
{
    return namingFromName (properties->getValue ("noteNames", "letters")).value_or (core::NoteNaming::letters);
}

void Preferences::setNoteNaming (core::NoteNaming naming)
{
    properties->setValue ("noteNames", namingName (naming));
}

juce::String Preferences::snap() const
{
    return properties->getValue ("snap", "step");
}

void Preferences::setSnap (const juce::String& name)
{
    properties->setValue ("snap", name);
}

int Preferences::audioBuffer() const
{
    return properties->getIntValue ("audioBuffer", 256);
}

void Preferences::setAudioBuffer (int samples)
{
    properties->setValue ("audioBuffer", samples);
}

juce::String Preferences::audioDriver() const
{
    return properties->getValue ("audioDriver") == "alsa" ? "alsa" : "jack";
}

void Preferences::setAudioDriver (const juce::String& driver)
{
    properties->setValue ("audioDriver", driver);
}

std::array<int, 2> Preferences::liveOctaves() const
{
    return { properties->getIntValue ("liveAcidOctave", 2), properties->getIntValue ("liveDrumOctave", 4) };
}

int Preferences::liveVelocity() const
{
    return properties->getIntValue ("liveVelocity", 100);
}

bool Preferences::liveShortGate() const
{
    return properties->getBoolValue ("liveShortGate", false);
}

void Preferences::setLive (std::array<int, 2> octaves, int velocity, bool shortGate)
{
    properties->setValue ("liveAcidOctave", octaves[0]);
    properties->setValue ("liveDrumOctave", octaves[1]);
    properties->setValue ("liveVelocity", velocity);
    properties->setValue ("liveShortGate", shortGate);
}

bool Preferences::scaleLock() const
{
    return properties->getBoolValue ("scaleLock", true);
}

void Preferences::setScaleLock (bool on)
{
    properties->setValue ("scaleLock", on);
}

bool Preferences::preview() const
{
    return properties->getBoolValue ("preview", true);
}

void Preferences::setPreview (bool on)
{
    properties->setValue ("preview", on);
}

juce::String Preferences::tiles() const
{
    return properties->getValue ("tiles", {});
}

void Preferences::setTiles (const juce::String& layout)
{
    properties->setValue ("tiles", layout);
}

tokens::Theme Preferences::theme() const
{
    const auto value = properties->getValue ("theme", {});
    if (value == "light")
        return tokens::Theme::light;
    if (value == "dark")
        return tokens::Theme::dark;
    return tokens::defaultTheme;
}

void Preferences::setTheme (tokens::Theme theme)
{
    properties->setValue ("theme", theme == tokens::Theme::light ? "light" : "dark");
}
} // namespace tekno::ui
