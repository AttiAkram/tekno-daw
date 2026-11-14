// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/commands/CommandLine.h"

#include "core/model/NoteNames.h"

namespace tekno::core
{
namespace
{
    /** Splits on spaces, keeping "quoted text" together (\" escapes a quote inside). */
    std::optional<juce::StringArray> tokenize (const juce::String& line, juce::String& error)
    {
        juce::StringArray tokens;
        juce::String current;
        bool inQuotes = false, hasToken = false;

        for (auto p = line.getCharPointer(); ! p.isEmpty();)
        {
            const auto c = p.getAndAdvance();
            if (inQuotes)
            {
                if (c == '\\' && *p == '"')
                    current << juce::String::charToString (p.getAndAdvance());
                else if (c == '"')
                    inQuotes = false;
                else
                    current << juce::String::charToString (c);
            }
            else if (c == '"')
            {
                inQuotes = hasToken = true;
            }
            else if (juce::CharacterFunctions::isWhitespace (c))
            {
                if (hasToken)
                    tokens.add (current);
                current.clear();
                hasToken = false;
            }
            else
            {
                current << juce::String::charToString (c);
                hasToken = true;
            }
        }

        if (inQuotes)
        {
            error = "a quote is not closed";
            return std::nullopt;
        }
        if (hasToken)
            tokens.add (current);
        return tokens;
    }

    std::optional<juce::var> convert (const CommandParam& param, const juce::String& text, juce::String& error)
    {
        using Type = CommandParam::Type;
        switch (param.type)
        {
        case Type::text:
            return juce::var (text);

        case Type::boolean:
        {
            const auto t = text.toLowerCase();
            if (t == "true" || t == "on" || t == "yes" || t == "1")
                return juce::var (true);
            if (t == "false" || t == "off" || t == "no" || t == "0")
                return juce::var (false);
            error = "'" + param.name + "' must be on/off or true/false, not '" + text + "'";
            return std::nullopt;
        }

        case Type::integer:
        case Type::number:
        {
            if (param.name == "note")
                if (const auto note = parseNoteName (text))
                    return juce::var (*note);

            const auto t = text.trim();
            if (t.isEmpty() || ! t.containsOnly ("-+.0123456789") || t.indexOfChar ('.') != t.lastIndexOfChar ('.'))
            {
                error = "'" + param.name + "' must be a number, not '" + text + "'";
                return std::nullopt;
            }
            if (param.type == Type::integer && ! t.containsChar ('.'))
                return juce::var (t.getIntValue());
            return juce::var (t.getDoubleValue());
        }
        }
        return std::nullopt;
    }

    /** Edit distance, to suggest the command that was probably meant. */
    int distance (const juce::String& a, const juce::String& b)
    {
        std::vector<int> row (static_cast<size_t> (b.length()) + 1);
        for (size_t j = 0; j < row.size(); ++j)
            row[j] = static_cast<int> (j);
        for (int i = 1; i <= a.length(); ++i)
        {
            auto diagonal = row[0];
            row[0] = i;
            for (int j = 1; j <= b.length(); ++j)
            {
                const auto above = row[static_cast<size_t> (j)];
                row[static_cast<size_t> (j)] = std::min (
                    { above + 1, row[static_cast<size_t> (j - 1)] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1) });
                diagonal = above;
            }
        }
        return row.back();
    }

    juce::String describeParams (const CommandDefinition& def)
    {
        juce::String s;
        for (const auto& p : def.params)
            s << " <" << p.name << ">";
        return s;
    }
} // namespace

const std::vector<std::pair<juce::String, juce::String>>& CommandLine::aliases()
{
    static const std::vector<std::pair<juce::String, juce::String>> list {
        { "play", "transport.play" },      { "stop", "transport.stop" },
        { "bpm", "transport.bpm" },        { "swing", "transport.swing" },
        { "select", "pattern.select" },    { "length", "pattern.length" },
        { "rename", "pattern.rename" },    { "toggle", "pattern.step.toggle" },
        { "note", "pattern.step.note" },   { "accent", "pattern.step.accent" },
        { "slide", "pattern.step.slide" }, { "tie", "pattern.step.tie" },
        { "undo", "edit.undo" },           { "redo", "edit.redo" },
        { "save", "project.save" },        { "open", "project.open" },
        { "cutoff", "synth.cutoff" },      { "reso", "synth.resonance" },
        { "envmod", "synth.envmod" },      { "decay", "synth.decay" },
        { "drive", "synth.drive" },
    };
    return list;
}

CommandLine::Result CommandLine::parse (const juce::String& line, const CommandRegistry& registry)
{
    Result result;
    const auto tokens = tokenize (line.trim(), result.error);
    if (! tokens)
        return result;
    if (tokens->isEmpty())
    {
        result.error = "empty command";
        return result;
    }

    auto id = (*tokens)[0];
    for (const auto& [alias, target] : aliases())
        if (id == alias)
            id = target;

    const auto* def = registry.find (id);
    if (def == nullptr)
    {
        result.error = "unknown command '" + (*tokens)[0] + "'";
        juce::String best;
        auto bestDistance = 4; // only suggest close matches
        for (const auto* candidate : registry.list())
            if (const auto d = distance ((*tokens)[0], candidate->id); d < bestDistance)
            {
                bestDistance = d;
                best = candidate->id;
            }
        for (const auto& [alias, target] : aliases())
            if (const auto d = distance ((*tokens)[0], alias); d < bestDistance)
            {
                bestDistance = d;
                best = alias;
            }
        if (best.isNotEmpty())
            result.error << " - did you mean '" << best << "'?";
        else
            result.error << " - type 'help' for the list";
        return result;
    }

    CommandArgs args;
    size_t nextPositional = 0;
    for (int t = 1; t < tokens->size(); ++t)
    {
        const auto& token = (*tokens)[t];
        const CommandParam* param = nullptr;
        juce::String valueText;

        if (token.containsChar ('=') && ! token.startsWithChar ('='))
        {
            const auto name = token.upToFirstOccurrenceOf ("=", false, false);
            for (const auto& p : def->params)
                if (p.name == name)
                    param = &p;
            if (param == nullptr)
            {
                result.error = def->id + " has no argument '" + name + "'";
                return result;
            }
            valueText = token.fromFirstOccurrenceOf ("=", false, false);
        }
        else
        {
            if (nextPositional >= def->params.size())
            {
                result.error =
                    "too many arguments for " + def->id + " (usage: " + def->id + describeParams (*def) + ")";
                return result;
            }
            param = &def->params[nextPositional++];
            valueText = token;

            // A trailing text argument swallows the rest of the line: `rename acid night`.
            if (param->type == CommandParam::Type::text && nextPositional == def->params.size())
                for (int rest = t + 1; rest < tokens->size(); ++rest, ++t)
                    valueText << " " << (*tokens)[rest];
        }

        const auto value = convert (*param, valueText, result.error);
        if (! value)
            return result;
        args.set (param->name, *value);
    }

    result.command = Parsed { def->id, args };
    return result;
}

juce::StringArray CommandLine::complete (const juce::String& partial, const CommandRegistry& registry)
{
    juce::StringArray matches;
    for (const auto* def : registry.list())
        if (def->id.startsWith (partial))
            matches.add (def->id);
    for (const auto& [alias, target] : aliases())
        if (alias.startsWith (partial))
            matches.addIfNotAlreadyThere (alias);
    matches.sort (false);
    return matches;
}

juce::String CommandLine::help (const CommandRegistry& registry, const juce::String& topic)
{
    juce::StringArray lines;
    if (topic.isEmpty())
    {
        for (const auto* def : registry.list())
            lines.add (def->id.paddedRight (' ', 22) + def->description);
        lines.add ({});
        lines.add ("help <command or prefix> for the details, Tab to complete.");
        return lines.joinIntoString ("\n");
    }

    auto id = topic;
    for (const auto& [alias, target] : aliases())
        if (id == alias)
            id = target;

    for (const auto* def : registry.list())
    {
        if (! def->id.startsWith (id))
            continue;
        lines.add (def->id + describeParams (*def) + "  -  " + def->description);
        for (const auto& p : def->params)
            lines.add ("    " + p.name + ": " + p.description);
    }
    return lines.isEmpty() ? "no command matches '" + topic + "'" : lines.joinIntoString ("\n");
}
} // namespace tekno::core
