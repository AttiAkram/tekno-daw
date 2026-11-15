// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/commands/Keymap.h"

#include <algorithm>

namespace tekno::core
{
namespace
{
    bool sameKey (const juce::String& a, const juce::String& b)
    {
        return a.trim().equalsIgnoreCase (b.trim());
    }

    void sortBindings (std::vector<Keymap::Binding>& list)
    {
        const auto& order = Keymap::contexts();
        std::stable_sort (list.begin(), list.end(),
                          [&order] (const Keymap::Binding& a, const Keymap::Binding& b)
                          {
                              const auto ca = order.indexOf (a.context), cb = order.indexOf (b.context);
                              if (ca != cb)
                                  return ca < cb;
                              return a.key.compareIgnoreCase (b.key) < 0;
                          });
    }
} // namespace

const juce::StringArray& Keymap::contexts()
{
    static const juce::StringArray list { "global",   "pattern", "pianoroll", "mixer",  "effects",
                                          "keyboard", "keys",    "acid",      "sampler" };
    return list;
}

std::optional<Keymap::Binding> Keymap::bind (const Binding& binding)
{
    for (auto& b : bindings)
        if (b.context == binding.context && sameKey (b.key, binding.key))
        {
            const auto replaced = b;
            b = binding;
            return replaced;
        }
    bindings.push_back (binding);
    sortBindings (bindings);
    return std::nullopt;
}

bool Keymap::unbind (const juce::String& context, const juce::String& key)
{
    const auto before = bindings.size();
    std::erase_if (bindings, [&] (const Binding& b) { return b.context == context && sameKey (b.key, key); });
    return bindings.size() != before;
}

std::optional<Keymap::Binding> Keymap::find (const juce::String& key, const juce::StringArray& activeContexts) const
{
    for (const auto& context : activeContexts)
        for (const auto& b : bindings)
            if (b.context == context && sameKey (b.key, key))
                return b;
    return std::nullopt;
}

std::vector<Keymap::Binding> Keymap::inContext (const juce::String& context) const
{
    std::vector<Binding> list;
    for (const auto& b : bindings)
        if (b.context == context)
            list.push_back (b);
    return list;
}

std::optional<Keymap::Binding> Keymap::shadowed (const Binding& binding, const juce::StringArray& broaderContexts) const
{
    for (const auto& context : broaderContexts)
        if (context != binding.context)
            for (const auto& b : bindings)
                if (b.context == context && sameKey (b.key, binding.key))
                    return b;
    return std::nullopt;
}

std::vector<std::pair<Keymap::Binding, Keymap::Binding>> Keymap::conflicts() const
{
    std::vector<std::pair<Binding, Binding>> found;
    for (size_t i = 0; i < bindings.size(); ++i)
        for (size_t j = i + 1; j < bindings.size(); ++j)
            if (bindings[i].context == bindings[j].context && sameKey (bindings[i].key, bindings[j].key))
                found.push_back ({ bindings[i], bindings[j] });
    return found;
}

juce::String Keymap::toJson() const
{
    auto sorted = bindings;
    sortBindings (sorted);
    auto quoted = [] (const juce::String& s) { return juce::JSON::toString (juce::var (s)); };

    juce::StringArray lines;
    lines.add ("{");
    lines.add ("  \"format\": " + quoted (formatId) + ",");
    lines.add ("  \"version\": 1,");
    lines.add ("  \"bindings\": [");
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const auto& b = sorted[i];
        lines.add ("    { \"context\": " + quoted (b.context) + ", \"key\": " + quoted (b.key) + ", \"command\": " +
                   quoted (b.command) + (b.label.isNotEmpty() ? ", \"label\": " + quoted (b.label) : juce::String()) +
                   " }" + (i + 1 < sorted.size() ? "," : ""));
    }
    lines.add ("  ]");
    lines.add ("}");
    return lines.joinIntoString ("\n") + "\n";
}

Keymap Keymap::fromJson (const juce::String& text, juce::StringArray& warnings)
{
    Keymap map;
    juce::var root;
    if (juce::JSON::parse (text, root).failed() || root["format"].toString() != formatId)
    {
        warnings.add ("not a tekno-daw keymap");
        return map;
    }
    const auto list = root["bindings"];
    for (int i = 0; list.isArray() && i < list.size(); ++i)
    {
        const auto item = list[i];
        Binding b { item["context"].toString().trim(), item["key"].toString().trim(), item["command"].toString().trim(),
                    item["label"].toString().trim() };
        const auto where = "bindings[" + juce::String (i) + "]";
        if (! contexts().contains (b.context))
            warnings.add (where + ": unknown context '" + b.context + "'");
        else if (b.key.isEmpty() || b.command.isEmpty())
            warnings.add (where + ": needs a key and a command");
        else
            map.bindings.push_back (b); // duplicates kept: conflicts() reports them
    }
    sortBindings (map.bindings);
    return map;
}
} // namespace tekno::core
