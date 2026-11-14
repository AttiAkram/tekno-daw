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

namespace tekno::core::io
{
/** The project file: UTF-8 JSON, written in a fixed order with one step per line, so that two versions
    of a project can be compared line by line (and with git). Notes are written by name ("A2").

    Reading is all-or-nothing: a file with any error leaves the session untouched and says where the
    error is ("patterns[0].steps[3].note: 'H2' is not a note"). */
namespace project
{
    inline constexpr const char* extension = ".tekno";
    inline constexpr const char* formatId = "tekno-daw/project";
    inline constexpr int formatVersion = 1;

    /** The project as text. Deterministic: the same session always gives the same bytes. */
    juce::String toText (const Session& session);

    /** Replaces the session's content with the project in `text`. On error the session is unchanged. */
    juce::Result fromText (const juce::String& text, Session& session);

    /** Writes through a temporary file, so a crash or a full disk can't leave half a project behind. */
    juce::Result save (const Session& session, const juce::File& file);
    juce::Result load (const juce::File& file, Session& session);
} // namespace project
} // namespace tekno::core::io
