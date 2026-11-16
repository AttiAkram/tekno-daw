// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/SampleLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/dsp/BuiltinKit.h"

namespace tekno::core
{
SampleLibrary::SampleLibrary()
{
    for (const auto& name : builtinNames())
    {
        auto sample = std::make_unique<Sample>();
        sample->sampleRate = dsp::builtinKit::sampleRate;
        sample->audio = dsp::builtinKit::make (name);
        sample->origin = builtinPrefix + name;
        entries.push_back ({ std::move (sample), true, 0 });
    }
}

juce::StringArray SampleLibrary::builtinNames()
{
    return { "kick", "hat", "openhat", "clap", "snare", "tom", "conga", "rim", "shaker" };
}

SampleLibrary::Result SampleLibrary::get (const juce::String& origin)
{
    for (auto& entry : entries)
        if (entry.sample->origin == origin)
        {
            entry.unusedSince = 0;
            return { entry.sample.get(), {} };
        }

    if (origin.startsWith (builtinPrefix))
        return { nullptr, "there is no built-in sound '" + origin.fromFirstOccurrenceOf (builtinPrefix, false, false) +
                              "' (there are: " + builtinNames().joinIntoString (", ") + ")" };
    return loadFile (origin);
}

SampleLibrary::Result SampleLibrary::loadFile (const juce::String& path)
{
    const juce::File file (path);
    if (! juce::File::isAbsolutePath (path))
        return { nullptr, "not an absolute path: " + path };
    if (! file.existsAsFile())
        return { nullptr, "file not found: " + path };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg Vorbis
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return { nullptr, file.getFileName() + " is not an audio file this version can read (WAV, AIFF, FLAC, Ogg)" };
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return { nullptr, file.getFileName() + " is empty" };
    if (static_cast<double> (reader->lengthInSamples) / reader->sampleRate > maxSeconds)
        return { nullptr, file.getFileName() + " is longer than " + juce::String (maxSeconds, 0) +
                              " seconds: too long for a drum sampler" };

    auto sample = std::make_unique<Sample>();
    const auto channels = static_cast<int> (std::min (2u, reader->numChannels));
    const auto length = static_cast<int> (reader->lengthInSamples);
    sample->audio.setSize (channels, length);
    if (! reader->read (&sample->audio, 0, length, 0, true, channels > 1))
        return { nullptr, "could not read " + file.getFileName() };
    sample->sampleRate = reader->sampleRate;
    sample->origin = path;

    const auto* loaded = sample.get();
    entries.push_back ({ std::move (sample), false, 0 });
    return { loaded, {} };
}

void SampleLibrary::collectGarbage (std::uint64_t audioBlocks, const std::function<bool (const Sample*)>& inUse)
{
    // Two whole audio blocks after we first saw it unused, no render can still hold the pointer: the
    // engine reads each channel's sample at the start of every block.
    constexpr std::uint64_t safeBlocks = 2;

    std::erase_if (entries,
                   [&] (Entry& entry)
                   {
                       if (entry.builtin || inUse (entry.sample.get()))
                       {
                           entry.unusedSince = 0;
                           return false;
                       }
                       if (entry.unusedSince == 0)
                       {
                           entry.unusedSince = audioBlocks + 1; // +1: 0 means "in use"
                           return false;
                       }
                       return audioBlocks + 1 >= entry.unusedSince + safeBlocks;
                   });
}
} // namespace tekno::core
