// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/KeyBindings.h"

#include "ui/Preferences.h"

namespace tekno::ui::keys
{
juce::String describe (const juce::KeyPress& key)
{
    return key.getTextDescription();
}

std::optional<juce::String> normalise (const juce::String& raw)
{
    // People write "ctrl+s" or "Space"; JUCE wants "ctrl + S" and "spacebar".
    auto text = raw.trim().replace ("+", " + ");
    while (text.contains ("  "))
        text = text.replace ("  ", " ");
    const auto lower = text.toLowerCase();
    if (lower == "space")
        text = "spacebar";
    else if (lower.endsWith (" space"))
        text = text.dropLastCharacters (5) + "spacebar";
    const auto key = juce::KeyPress::createFromDescription (text);
    if (! key.isValid())
        return std::nullopt;

    // JUCE reads almost anything as some key; accept it only if it reads back as what was written
    // (modifiers in any order, any case).
    auto parts = [] (const juce::String& description)
    {
        auto tokens = juce::StringArray::fromTokens (description.toLowerCase(), "+", "");
        tokens.trim();
        tokens.removeEmptyStrings();
        tokens.sort (false);
        return tokens;
    };
    const auto result = describe (key);
    if (parts (result) != parts (text))
        return std::nullopt;
    return result;
}

core::Keymap defaults()
{
    using K = juce::KeyPress;
    using M = juce::ModifierKeys;
    const auto ctrl = M::commandModifier, shift = M::shiftModifier;
    core::Keymap map;
    auto add = [&map] (const char* context, const K& key, const char* command, const char* label)
    { map.bind ({ context, describe (key), command, label }); };

    add ("global", K (K::spaceKey), "transport.toggle", "play / stop");
    add ("global", K ('z', ctrl, 0), "edit.undo", "undo");
    add ("global", K ('z', ctrl | shift, 0), "edit.redo", "redo");
    add ("global", K ('y', ctrl, 0), "edit.redo", "redo");
    add ("global", K ('s', ctrl, 0), "ui.save", "save");
    add ("global", K ('s', ctrl | shift, 0), "ui.saveAs", "save as");
    add ("global", K ('o', ctrl, 0), "ui.open", "open");
    add ("global", K ('n', ctrl, 0), "ui.new", "new project");
    add ("global", K ('t'), "ui.theme.toggle", "theme");
    add ("global", K ('`'), "ui.terminal", "terminal");
    add ("global", K (K::F6Key), "ui.view pattern", "step sequencer");
    add ("global", K (K::F7Key), "ui.view pianoroll", "piano roll");
    add ("global", K (K::F9Key), "ui.view mixer", "mixer");
    add ("global", K (K::F10Key), "ui.view keys", "edit the keys");
    add ("global", K (K::pageUpKey), "pattern.previous", "previous pattern");
    add ("global", K (K::pageDownKey), "pattern.next", "next pattern");
    add ("global", K (K::upKey), "channel.previous", "previous channel");
    add ("global", K (K::downKey), "channel.next", "next channel");
    const auto alt = M::altModifier;
    add ("global", K (K::F6Key, alt, 0), "ui.tile pattern", "steps beside");
    add ("global", K (K::F7Key, alt, 0), "ui.tile pianoroll", "piano roll beside");
    add ("global", K (K::F9Key, alt, 0), "ui.tile mixer", "mixer beside");
    add ("global", K (K::F10Key, alt, 0), "ui.tile keys", "keys beside");
    add ("global", K ('q', alt, 0), "ui.tile.close", "close panel");
    add ("global", K (K::rightKey, alt, 0), "ui.tile.focus next", "next panel");
    add ("global", K (K::leftKey, alt, 0), "ui.tile.focus previous", "previous panel");
    add ("pattern", K ('['), "ui.page previous", "previous bar");
    add ("pattern", K (']'), "ui.page next", "next bar");
    add ("pianoroll", K ('c', ctrl, 0), "ui.roll.copy", "copy");
    add ("pianoroll", K ('v', ctrl, 0), "ui.roll.paste", "paste");
    add ("pianoroll", K ('d', ctrl, 0), "ui.roll.duplicate", "duplicate");
    add ("pianoroll", K ('a', ctrl, 0), "ui.roll.selectall", "select all");
    add ("pianoroll", K (K::deleteKey), "ui.roll.delete", "delete");
    add ("pianoroll", K (K::backspaceKey), "ui.roll.delete", "delete");
    add ("pianoroll", K (K::escapeKey), "ui.roll.deselect", "deselect");
    return map;
}

juce::File defaultFile()
{
    return Preferences::defaultFile().getSiblingFile ("keymap.json");
}

core::Keymap load (const juce::File& file, juce::StringArray& warnings)
{
    if (! file.existsAsFile())
        return defaults();
    auto map = core::Keymap::fromJson (file.loadFileAsString(), warnings);

    // Keys written by hand may be in any form: store them the one way, or drop them with a reason.
    core::Keymap clean;
    for (auto b : map.all())
    {
        if (const auto key = normalise (b.key))
        {
            b.key = *key;
            clean.bind (b);
        }
        else
            warnings.add ("'" + b.key + "' is not a key (in " + b.context + ")");
    }
    for (const auto& [a, b] : map.conflicts())
        warnings.add ("in " + a.context + ", '" + a.key + "' was bound twice: kept \"" + b.command + "\"");
    return clean;
}

bool save (const core::Keymap& keymap, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    const juce::TemporaryFile temp (file);
    return temp.getFile().replaceWithText (keymap.toJson(), false, false, "\n") &&
           temp.overwriteTargetFileWithTemporary();
}
} // namespace tekno::ui::keys
