// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <juce_core/juce_core.h>

#include "core/model/Channel.h"
#include "core/model/Pattern.h"
#include "core/model/SampleLibrary.h"
#include "core/model/Scale.h"

namespace tekno::core
{
/** Transport state. `playing`, `bpm`, `swing` are written by commands; `currentStep` by the audio thread. */
struct TransportState
{
    static constexpr double minBpm = 40.0;
    static constexpr double maxBpm = 300.0;
    static constexpr double defaultBpm = 130.0;

    /** Swing in MPC notation: the share of a pair of 16ths taken by the first one.
        50 = straight, 66.7 = triplet shuffle, 75 = dotted. */
    static constexpr float minSwing = 50.0f;
    static constexpr float maxSwing = 75.0f;

    std::atomic<bool> playing { false };
    std::atomic<double> bpm { defaultBpm };
    std::atomic<float> swing { minSwing };
    std::atomic<int> currentStep { -1 }; // the step being played, -1 when stopped
};

/** The master bus. Volume written by commands; peaks by the audio thread (the GUI takes them with
    exchange (0)). */
struct MasterState
{
    std::atomic<float> volumeDb { 0.0f };
    std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };
};

/** A whole channel as plain values: what a file holds, what undo keeps of a removed channel. */
struct ChannelData
{
    InstrumentKind kind = InstrumentKind::acid;
    juce::String name;
    juce::String sample; // origin: "builtin:kick", an absolute path, or empty
    AcidSettings acid;
    SamplerSettings sampler;
    MixerSettings mixer;
    std::array<std::vector<Note>, 16> lanes {}; // the channel's notes in each pattern
};

/** Facts about the project as a document. Message thread only: the audio thread never touches it. */
struct ProjectInfo
{
    juce::File file;                 // empty until saved or opened
    std::uint64_t revision = 0;      // bumped by every change that would be saved
    std::uint64_t savedRevision = 0; // revision at the last save or open
    std::array<juce::String, 16> patternNames;
    std::array<juce::String, 16> channelNames;
    std::array<juce::String, 16> channelSamples; // origin of each channel's sample, kept even if missing
    juce::StringArray warnings;                  // from the last open: missing samples and the like
    Key key;                                     // the scale guide; off until chosen

    bool hasUnsavedChanges() const noexcept { return revision != savedRevision; }
};

/** Everything the running DAW is playing, shared between the message thread and the audio thread.
    Only commands write to it; the audio thread only reads it, except where noted.

    Channels: up to 16 preallocated slots, the first `channelCount` in use. Every pattern
    has one lane per slot. Adding or removing a channel never allocates. */
class Session
{
public:
    static constexpr int maxPatterns = 16;
    static constexpr int maxChannels = Pattern::maxLanes;

    Session();

    /** A new project: a kit (kick, hat, open hat, clap) and an acid channel, with a loop in pattern 1. */
    void loadDefaultProject();

    /** No channels, empty patterns, default tempo: the starting point for opening a file. */
    void clearProject();

    // --- patterns ---------------------------------------------------------------------------------
    Pattern& pattern (int index) noexcept { return patterns[patternSlot (index)]; }
    const Pattern& pattern (int index) const noexcept { return patterns[patternSlot (index)]; }
    Pattern& selectedPattern() noexcept { return pattern (selected.load()); }
    const Pattern& selectedPattern() const noexcept { return pattern (selected.load()); }

    static juce::String defaultPatternName (int index) { return "pattern " + juce::String (index + 1); }

    // --- channels ---------------------------------------------------------------------------------
    int channelCount() const noexcept { return numChannels.load(); }
    Channel& channel (int index) noexcept { return channels[channelSlot (index)]; }
    const Channel& channel (int index) const noexcept { return channels[channelSlot (index)]; }
    Channel& selectedChannelRef() noexcept { return channel (selectedChannel.load()); }

    /** Adds a channel at the end with default settings and empty lanes. Returns its index, or -1 when
        all 16 are in use. Message thread. */
    int addChannel (InstrumentKind kind, const juce::String& name);

    /** Puts a whole channel at `index`, moving the later ones one place down. False when full. If its
        sample can't be found the channel is silent and `warning` says why. Message thread. */
    bool insertChannel (int index, const ChannelData& data, juce::String* warning = nullptr);

    /** Removes a channel; the later ones move one place up. Message thread. */
    void removeChannel (int index);

    /** A copy of everything about a channel, lanes of every pattern included. Message thread. */
    ChannelData captureChannel (int index) const;

    /** Points a sampler channel at a sample ("builtin:kick" or an absolute path; empty = none).
        Returns an error, or an empty string. The channel keeps the origin even when it fails, so a
        missing file is not forgotten when the project is saved again. Message thread. */
    juce::String setChannelSample (int index, const juce::String& origin);

    /** Frees samples no channel uses any more, once the audio thread can't be reading them. */
    void collectSampleGarbage();

    // --- state --------------------------------------------------------------------------------------
    TransportState transport;
    MasterState master;
    std::array<Pattern, maxPatterns> patterns;

    std::atomic<int> selected { 0 };        // the pattern being edited; playback switches to it at the next wrap
    std::atomic<int> playing { 0 };         // the pattern being played: written by the audio thread
    std::atomic<int> selectedChannel { 0 }; // the channel the step grid, knobs and terminal act on

    /** Counted by the audio thread at the end of every render(): tells the message thread when an
        old sample can no longer be in use. */
    std::atomic<std::uint64_t> audioBlocks { 0 };

    /** Audition: "play this note on this channel now", from the message thread (placing a note,
        clicking a key). Only the latest request counts; the engine plays it for a moment.
        Layout: request number << 32 | channel << 16 | velocity << 8 | pitch. 0 = none yet. */
    std::atomic<std::uint64_t> preview { 0 };
    void requestPreview (int channel, int pitch, int velocity) noexcept;

    ProjectInfo project;   // message thread only
    SampleLibrary samples; // message thread only

    static_assert (std::atomic<double>::is_always_lock_free);
    static_assert (std::atomic<float>::is_always_lock_free);
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);

private:
    static size_t patternSlot (int index) noexcept
    {
        return static_cast<size_t> (std::clamp (index, 0, maxPatterns - 1));
    }
    static size_t channelSlot (int index) noexcept
    {
        return static_cast<size_t> (std::clamp (index, 0, maxChannels - 1));
    }

    /** Writes a channel's values into slot `index` (does not change channelCount). */
    juce::String writeChannel (int index, const ChannelData& data);
    void clearChannelSlot (int index);

    std::array<Channel, maxChannels> channels;
    std::atomic<int> numChannels { 0 };
};
} // namespace tekno::core
