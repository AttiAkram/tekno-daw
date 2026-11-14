// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <functional>
#include <map>
#include <vector>

#include <juce_core/juce_core.h>

namespace tekno::core
{
/** Arguments of a command, by name: { { "step", 3 }, { "on", true } } */
using CommandArgs = juce::NamedValueSet;

struct CommandParam
{
    enum class Type
    {
        integer,
        number,
        boolean,
        text,
    };

    juce::String name;
    Type type = Type::number;
    double min = 0.0;
    double max = 1.0;
    juce::String description;
};

/** What running a command produced. A command that sets `undo` becomes undoable. */
struct CommandOutcome
{
    juce::String error; // empty = success
    std::function<void()> undo;

    bool succeeded() const noexcept { return error.isEmpty(); }

    static CommandOutcome ok (std::function<void()> undoAction = {}) { return { {}, std::move (undoAction) }; }
    static CommandOutcome fail (juce::String why) { return { std::move (why), {} }; }
};

struct CommandDefinition
{
    juce::String id;          // stable, public: "pattern.step.toggle". Renaming it breaks keymaps and scripts.
    juce::String description; // one line, for the terminal, help and remote clients
    std::vector<CommandParam> params;
    std::function<CommandOutcome (const CommandArgs&)> run;
    bool modifiesProject = false; // true when its effect is saved in the project file
};

/** The single entry point for every action that changes the DAW (ADR 0014). Mouse, keyboard,
    and later the terminal, remote clients and scripts all go through execute().

    Message thread only. The audio thread never runs commands: it sees their effects through the
    lock-free state in Session. */
class CommandRegistry
{
public:
    static constexpr size_t maxHistory = 512;

    void add (CommandDefinition definition);

    const CommandDefinition* find (const juce::String& id) const;
    std::vector<const CommandDefinition*> list() const; // sorted by id

    /** Validates the arguments against the definition, then runs the command. */
    CommandOutcome execute (const juce::String& id, const CommandArgs& args = {});

    /** Called after every successful change to the project: a command with modifiesProject, or an
        undo/redo of one. The session uses it to know there are unsaved changes. */
    std::function<void()> onProjectModified;

    /** Forget undo/redo history, e.g. after opening another project. */
    void clearHistory() noexcept
    {
        undoStack.clear();
        redoStack.clear();
    }

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    bool undo();
    bool redo();

private:
    struct HistoryEntry
    {
        juce::String id;
        CommandArgs args;
        std::function<void()> undo;
    };

    static juce::String validate (const CommandDefinition&, const CommandArgs&);
    void pushUndo (HistoryEntry entry);
    void notifyModified (const juce::String& id);

    std::map<juce::String, CommandDefinition> commands;
    std::vector<HistoryEntry> undoStack, redoStack;
};
} // namespace tekno::core
