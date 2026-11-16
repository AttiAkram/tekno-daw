// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "core/model/Sample.h"

namespace tekno::core
{
/** Owns every sample the session has loaded: the built-in kit and files. Message thread only.

    Channels point at samples with plain atomic pointers. A sample nobody points at any more is not
    freed at once, because the audio thread may still be playing it: it is freed only after the
    audio thread has finished two more blocks. The audio thread never frees memory. */
class SampleLibrary
{
public:
    static constexpr double maxSeconds = 60.0;
    static constexpr const char* builtinPrefix = "builtin:";

    SampleLibrary();

    struct Result
    {
        const Sample* sample = nullptr;
        juce::String error; // set when sample is null
    };

    /** The sample for an origin ("builtin:kick", or an absolute path), loading the file if needed. */
    Result get (const juce::String& origin);

    /** Names of the built-in sounds: the kick, hats and clap, then the tribal ones (tom, conga...). */
    static juce::StringArray builtinNames();

    /** Frees samples that no channel uses and that the audio thread can no longer be reading.
        `audioBlocks` = Session::audioBlocks; `inUse` says whether a channel points at a sample. */
    void collectGarbage (std::uint64_t audioBlocks, const std::function<bool (const Sample*)>& inUse);

    size_t size() const noexcept { return entries.size(); }

private:
    struct Entry
    {
        std::unique_ptr<Sample> sample;
        bool builtin = false;
        std::uint64_t unusedSince = 0; // audio block count when first seen unused; 0 = in use
    };

    Result loadFile (const juce::String& path);

    std::vector<Entry> entries;
};
} // namespace tekno::core
