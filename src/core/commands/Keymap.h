// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

namespace tekno::core
{
/** What a key does where: a key, in a context, runs a command line (the terminal's language).

    Contexts: "global", a view ("pattern", "pianoroll", "mixer", "keys") and an instrument ("acid",
    "sampler", the kind of the selected channel). A key press looks in the active contexts from the
    most specific to "global" and runs the first binding it finds.

    Keys are text in JUCE's KeyPress description format ("ctrl + Z", "spacebar", "F7"); this class
    compares them without caring about case and never needs a GUI. */
class Keymap
{
public:
    struct Binding
    {
        juce::String context, key, command, label; // label: a short human name, may be empty

        friend bool operator== (const Binding&, const Binding&) = default;
    };

    static const juce::StringArray& contexts(); // every known context, broadest first

    /** Adds or replaces the binding of this key in this context. Returns what it replaced, if anything. */
    std::optional<Binding> bind (const Binding& binding);
    bool unbind (const juce::String& context, const juce::String& key);
    void clear() { bindings.clear(); }

    /** The binding a key press runs, looking through `activeContexts` from the first to the last. */
    std::optional<Binding> find (const juce::String& key, const juce::StringArray& activeContexts) const;

    /** Bindings in one context, in order of key. */
    std::vector<Binding> inContext (const juce::String& context) const;
    const std::vector<Binding>& all() const noexcept { return bindings; }

    /** The binding of the same key in a broader context that this one hides, if any. */
    std::optional<Binding> shadowed (const Binding& binding, const juce::StringArray& broaderContexts) const;

    /** The same key twice in one context: the only real conflict. Only a file written by hand has them. */
    std::vector<std::pair<Binding, Binding>> conflicts() const;

    /** Deterministic JSON: the same keymap always gives the same text. */
    juce::String toJson() const;
    /** Reads what it can; anything unusable is skipped and described in `warnings`. Duplicates are kept,
        so conflicts() can report them. */
    static Keymap fromJson (const juce::String& text, juce::StringArray& warnings);

    static constexpr const char* formatId = "tekno-daw/keymap";

private:
    std::vector<Binding> bindings;
};
} // namespace tekno::core
