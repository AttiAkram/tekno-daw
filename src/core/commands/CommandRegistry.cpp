// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/commands/CommandRegistry.h"

#include <algorithm>
#include <cmath>

#include <juce_events/juce_events.h>

namespace tekno::core
{
namespace
{
    void assertMessageThread()
    {
        // No message loop at all (unit tests, offline tools) is fine; another thread is not.
        [[maybe_unused]] auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        jassert (mm == nullptr || mm->isThisTheMessageThread());
    }

    bool isNumber (const juce::var& v)
    {
        return v.isInt() || v.isInt64() || v.isDouble();
    }
} // namespace

void CommandRegistry::add (CommandDefinition definition)
{
    jassert (definition.id.isNotEmpty() && definition.run != nullptr);
    jassert (commands.find (definition.id) == commands.end()); // ids are unique

    auto id = definition.id;
    commands.insert_or_assign (std::move (id), std::move (definition));
}

const CommandDefinition* CommandRegistry::find (const juce::String& id) const
{
    const auto it = commands.find (id);
    return it != commands.end() ? &it->second : nullptr;
}

std::vector<const CommandDefinition*> CommandRegistry::list() const
{
    std::vector<const CommandDefinition*> result;
    result.reserve (commands.size());
    for (const auto& [id, definition] : commands)
        result.push_back (&definition);
    return result; // std::map keeps them sorted
}

juce::String CommandRegistry::validate (const CommandDefinition& definition, const CommandArgs& args)
{
    for (int i = 0; i < args.size(); ++i)
    {
        const auto name = args.getName (i).toString();
        const auto known = std::any_of (definition.params.begin(), definition.params.end(),
                                        [&] (const CommandParam& p) { return p.name == name; });
        if (! known)
            return "unknown argument '" + name + "' for " + definition.id;
    }

    for (const auto& param : definition.params)
    {
        if (! args.contains (param.name))
            return "missing argument '" + param.name + "' for " + definition.id;

        const auto& value = args[param.name];
        using Type = CommandParam::Type;

        if (param.type == Type::text)
        {
            if (! value.isString())
                return "'" + param.name + "' must be text";
            continue;
        }

        if (param.type == Type::boolean)
        {
            if (! (value.isBool() || value.isInt()))
                return "'" + param.name + "' must be true or false";
            continue;
        }

        if (! isNumber (value))
            return "'" + param.name + "' must be a number";

        const auto number = static_cast<double> (value);
        if (param.type == Type::integer && number - std::floor (number) > 1e-9)
            return "'" + param.name + "' must be a whole number";
        if (number < param.min || number > param.max)
            return "'" + param.name + "' must be between " + juce::String (param.min) + " and " +
                   juce::String (param.max);
    }

    return {};
}

CommandOutcome CommandRegistry::execute (const juce::String& id, const CommandArgs& args)
{
    assertMessageThread();

    const auto* definition = find (id);
    if (definition == nullptr)
        return CommandOutcome::fail ("unknown command '" + id + "'");

    if (const auto error = validate (*definition, args); error.isNotEmpty())
        return CommandOutcome::fail (error);

    auto outcome = definition->run (args);

    if (outcome.succeeded() && outcome.undo != nullptr)
    {
        const auto gesture = gestureOpen ? gestureCounter : 0;
        const auto mergeWithLast = definition->coalesce && gesture != 0 && ! undoStack.empty() &&
                                   undoStack.back().id == id && undoStack.back().gesture == gesture;
        if (mergeWithLast)
            undoStack.back().args = args; // keep the first undo (the value before the gesture); redo goes to the latest
        else
            pushUndo ({ id, args, outcome.undo, gesture });
        redoStack.clear(); // a new action makes the old future unreachable
    }
    if (outcome.succeeded())
        notifyModified (id);
    return outcome;
}

void CommandRegistry::notifyModified (const juce::String& id)
{
    const auto* definition = find (id);
    if (definition != nullptr && definition->modifiesProject && onProjectModified != nullptr)
        onProjectModified();
}

void CommandRegistry::pushUndo (HistoryEntry entry)
{
    undoStack.push_back (std::move (entry));
    if (undoStack.size() > maxHistory)
        undoStack.erase (undoStack.begin());
}

bool CommandRegistry::undo()
{
    assertMessageThread();
    if (undoStack.empty())
        return false;

    auto entry = std::move (undoStack.back());
    undoStack.pop_back();
    entry.undo();
    notifyModified (entry.id);
    redoStack.push_back (std::move (entry));
    return true;
}

bool CommandRegistry::redo()
{
    assertMessageThread();
    if (redoStack.empty())
        return false;

    auto entry = std::move (redoStack.back());
    redoStack.pop_back();

    const auto* definition = find (entry.id);
    if (definition == nullptr)
        return false;

    // Run again with the same arguments; the fresh undo action goes back on the stack.
    auto outcome = definition->run (entry.args);
    if (outcome.succeeded() && outcome.undo != nullptr)
        pushUndo ({ entry.id, entry.args, outcome.undo });
    if (outcome.succeeded())
        notifyModified (entry.id);
    return outcome.succeeded();
}
} // namespace tekno::core
