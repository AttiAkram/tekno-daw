// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/Session.h"

namespace tekno::core
{
static_assert (std::tuple_size_v<decltype (ChannelData::lanes)> == Session::maxPatterns);

Session::Session()
{
    loadDefaultProject();
}

void Session::clearProject()
{
    transport.playing = false;
    transport.bpm = TransportState::defaultBpm;
    transport.swing = TransportState::minSwing;
    master.volumeDb = 0.0f;

    numChannels = 0;
    for (int c = 0; c < maxChannels; ++c)
        clearChannelSlot (c);

    for (int i = 0; i < maxPatterns; ++i)
    {
        pattern (i).clear();
        project.patternNames[static_cast<size_t> (i)] = defaultPatternName (i);
    }
    selected = 0;
    playing = 0;
    selectedChannel = 0;

    project.file = juce::File();
    project.revision = project.savedRevision = 0;
    project.warnings.clear();
    project.key = {};
}

void Session::loadDefaultProject()
{
    clearProject();

    auto addSampler = [this] (const char* name, const char* sound, float volumeDb)
    {
        const auto index = addChannel (InstrumentKind::sampler, name);
        setChannelSample (index, SampleLibrary::builtinPrefix + juce::String (sound));
        channel (index).mixer.volumeDb = volumeDb;
        return index;
    };
    const auto kick = addSampler ("kick", "kick", -6.0f);
    const auto hat = addSampler ("hat", "hat", -16.0f);
    addSampler ("open hat", "openhat", -18.0f);
    const auto clap = addSampler ("clap", "clap", -13.0f);
    const auto acid = addChannel (InstrumentKind::acid, "acid");
    channel (acid).mixer.volumeDb = -8.0f;

    project.patternNames[0] = "acid 01";
    auto& first = pattern (0);

    // Four on the floor, closed hat on the off-beats, clap on 2 and 4.
    const Step hit { true, false, false, Step::samplerRootNote };
    for (int beat = 0; beat < 4; ++beat)
    {
        first.setStep (kick, beat * 4, hit);
        first.setStep (hat, beat * 4 + 2, hit);
    }
    first.setStep (clap, 4, hit);
    first.setStep (clap, 12, hit);

    // A minor acid line: A2 root with octave jumps, accents on the off-beats, slides into the jumps.
    constexpr int A2 = 45, C3 = 48, D3 = 50, E3 = 52, G3 = 55, A3 = 57;
    auto on = [] (int note, bool accent = false, bool slide = false) { return Step { true, accent, slide, note }; };
    const Step off { false, false, false, A2 };
    const Step line[Pattern::defaultLength] = {
        on (A2, true),
        off,
        on (A3, false, true),
        on (A2),
        on (C3),
        off,
        on (A2, true),
        on (E3, false, true),
        on (G3),
        off,
        on (A2),
        on (A3, true, true),
        off,
        on (D3),
        on (A2, false, true),
        on (A3),
    };
    for (int i = 0; i < Pattern::defaultLength; ++i)
        first.setStep (acid, i, line[i]);

    selectedChannel = acid;
    project.key = { 9, Scale::minor }; // La minore: the scale of the line above
    project.revision = project.savedRevision = 0;
}

// --- channels -------------------------------------------------------------------------------------

void Session::clearChannelSlot (int index)
{
    auto& c = channel (index);
    c.kind = InstrumentKind::acid;
    c.acid.store ({});
    c.sampler.store ({});
    c.mixer.store ({});
    c.sample = nullptr;
    c.peak = 0.0f;
    ++c.generation;
    for (auto& p : patterns)
        p.lane (index).clear();
    project.channelNames[channelSlot (index)] = {};
    project.channelSamples[channelSlot (index)] = {};
}

juce::String Session::writeChannel (int index, const ChannelData& data)
{
    auto& c = channel (index);
    c.kind = data.kind;
    c.acid.store (data.acid);
    c.sampler.store (data.sampler);
    c.mixer.store (data.mixer);
    c.peak = 0.0f;
    for (size_t p = 0; p < data.lanes.size(); ++p)
        patterns[p].lane (index).setNotes (data.lanes[p]);
    project.channelNames[channelSlot (index)] = data.name;
    const auto error = setChannelSample (index, data.sample);
    ++c.generation;
    return error;
}

ChannelData Session::captureChannel (int index) const
{
    const auto& c = channel (index);
    ChannelData data;
    data.kind = c.kind.load();
    data.name = project.channelNames[channelSlot (index)];
    data.sample = project.channelSamples[channelSlot (index)];
    data.acid = c.acid.load();
    data.sampler = c.sampler.load();
    data.mixer = c.mixer.load();
    for (size_t p = 0; p < data.lanes.size(); ++p)
        data.lanes[p] = patterns[p].lane (index).notes();
    return data;
}

int Session::addChannel (InstrumentKind kind, const juce::String& name)
{
    const auto index = channelCount();
    if (index >= maxChannels)
        return -1;
    ChannelData data;
    data.kind = kind;
    data.name = name;
    writeChannel (index, data);
    numChannels = index + 1; // last: the audio thread only reads slots below the count
    return index;
}

bool Session::insertChannel (int index, const ChannelData& data, juce::String* warning)
{
    const auto count = channelCount();
    if (count >= maxChannels)
        return false;
    index = std::clamp (index, 0, count);
    for (int c = count - 1; c >= index; --c)
        writeChannel (c + 1, captureChannel (c));
    const auto error = writeChannel (index, data);
    if (warning != nullptr)
        *warning = error;
    numChannels = count + 1;
    return true;
}

void Session::removeChannel (int index)
{
    const auto count = channelCount();
    if (index < 0 || index >= count)
        return;
    numChannels = count - 1; // first: the audio thread stops reading the last slot
    for (int c = index; c < count - 1; ++c)
        writeChannel (c, captureChannel (c + 1));
    clearChannelSlot (count - 1);
    selectedChannel = std::clamp (selectedChannel.load(), 0, std::max (0, count - 2));
}

juce::String Session::setChannelSample (int index, const juce::String& origin)
{
    auto& c = channel (index);
    project.channelSamples[channelSlot (index)] = origin;
    if (origin.isEmpty())
    {
        c.sample = nullptr;
        return {};
    }
    const auto result = samples.get (origin);
    c.sample = result.sample;
    return result.error;
}

void Session::requestPreview (int channel, int pitch, int velocity) noexcept
{
    const auto number = (preview.load() >> 32) + 1;
    preview.store ((number << 32) | (static_cast<std::uint64_t> (std::clamp (channel, 0, maxChannels - 1)) << 16) |
                   (static_cast<std::uint64_t> (std::clamp (velocity, 0, 127)) << 8) |
                   static_cast<std::uint64_t> (std::clamp (pitch, 0, 127)));
}

void Session::collectSampleGarbage()
{
    samples.collectGarbage (audioBlocks.load(),
                            [this] (const Sample* s)
                            {
                                return std::any_of (channels.begin(), channels.end(),
                                                    [s] (const Channel& c) { return c.sample.load() == s; });
                            });
}
} // namespace tekno::core
