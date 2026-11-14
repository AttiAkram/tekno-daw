// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/engine/OfflineRender.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/engine/AudioEngine.h"

namespace tekno::core
{
juce::Result renderToWav (Session& session, const juce::File& file, int bars, double sampleRate)
{
    if (bars < 1)
        return juce::Result::fail ("bars must be at least 1");
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("cannot create " + file.getParentDirectory().getFullPathName());

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream&> (*stream).openedOk())
        return juce::Result::fail ("cannot write " + file.getFullPathName());

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (
        stream,
        juce::AudioFormatWriterOptions {}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr)
        return juce::Result::fail ("the WAV writer refused these settings");

    AudioEngine engine (session);
    engine.prepare (sampleRate);

    const auto bpm = session.transport.bpm.load();
    const auto playSamples =
        static_cast<juce::int64> (std::ceil (AudioEngine::samplesPerStep (sampleRate, bpm) * Pattern::numSteps * bars));
    const auto tailSamples = static_cast<juce::int64> (sampleRate * 0.5);

    constexpr int blockSize = 512;
    juce::AudioBuffer<float> buffer (2, blockSize);

    auto renderSpan = [&] (juce::int64 total)
    {
        for (juce::int64 done = 0; done < total;)
        {
            const auto n = static_cast<int> (std::min<juce::int64> (blockSize, total - done));
            engine.render (buffer.getArrayOfWritePointers(), 2, n);
            if (! writer->writeFromAudioSampleBuffer (buffer, 0, n))
                return false;
            done += n;
        }
        return true;
    };

    const auto wasPlaying = session.transport.playing.exchange (true);
    auto ok = renderSpan (playSamples);
    session.transport.playing.store (false);
    ok = ok && renderSpan (tailSamples);
    session.transport.playing.store (wasPlaying);

    return ok ? juce::Result::ok() : juce::Result::fail ("writing the WAV failed (disk full?)");
}
} // namespace tekno::core
