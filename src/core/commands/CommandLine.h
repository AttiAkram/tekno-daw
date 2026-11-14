// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <optional>

#include "core/commands/CommandRegistry.h"

namespace tekno::core
{
/** Turns a line of text into a command call — the language of the built-in terminal, and later of
    scripts and remote clients.

        transport.bpm 138            positional arguments, in the order the command declares them
        transport.bpm bpm=138        or by name
        bpm 138                      short aliases for the common commands
        pattern.step.note 3 A2       note names wherever a note number is expected
        rename acid night            a trailing text argument takes the rest of the line
        rename "acid night"          or quotes
*/
class CommandLine
{
public:
    struct Parsed
    {
        juce::String id;
        CommandArgs args;
    };

    struct Result
    {
        std::optional<Parsed> command;
        juce::String error;
    };

    static Result parse (const juce::String& line, const CommandRegistry& registry);

    /** Command ids and aliases starting with `partial`, sorted. */
    static juce::StringArray complete (const juce::String& partial, const CommandRegistry& registry);

    /** `help` text: the list of commands, or the details of the ones matching `topic`. */
    static juce::String help (const CommandRegistry& registry, const juce::String& topic = {});

    /** alias -> command id */
    static const std::vector<std::pair<juce::String, juce::String>>& aliases();
};
} // namespace tekno::core
