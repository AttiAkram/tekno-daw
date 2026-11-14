// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/commands/CommandRegistry.h"
#include "core/commands/Keymap.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** The keys editor: one tab per context, one row per binding, and a bar to add one: press a key, write
    the command line it should run (checked by the terminal's parser), give it a short name.
    The same key twice in one context is the only real conflict, shown in the warning colour; a key
    that covers a broader one is just noted. Changes go through `onChange`, which saves them. */
class KeysView final : public juce::Component
{
public:
    KeysView (TeknoLookAndFeel& lookAndFeel, core::Keymap& keymap, core::CommandRegistry& commands);
    ~KeysView() override;

    std::function<void()> onChange;

    void showContext (const juce::String& context);
    void refresh(); // after the keymap changed elsewhere (terminal, reset)

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class List;

    void startCapture();
    void add();
    void remove (const core::Keymap::Binding& binding);
    void setMessage (const juce::String& text, bool problem);

    TeknoLookAndFeel& lnf;
    core::Keymap& keymap;
    core::CommandRegistry& commands;

    juce::String context { "global" };
    std::vector<std::unique_ptr<juce::TextButton>> tabs;
    std::unique_ptr<List> list;
    juce::Viewport viewport;

    juce::TextButton captureButton { "press a key" }, addButton { "add" };
    juce::TextEditor commandField, labelField;
    std::optional<juce::String> capturedKey;
    bool capturing = false;
    juce::String message;
    bool messageIsProblem = false;
    juce::Rectangle<float> addBar, messageArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeysView)
};
} // namespace tekno::ui
