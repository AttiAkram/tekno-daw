// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/views/KeysView.h"

#include "core/commands/CommandLine.h"
#include "ui/KeyBindings.h"

namespace tekno::ui
{
namespace
{
    using P = tokens::Palette;
    namespace type = tokens::type;
    namespace shape = tokens::shape;
    namespace space = tokens::space;

    constexpr float rowHeight = 40.0f;
    constexpr float keyColumn = 180.0f;

    juce::String contextTitle (const juce::String& context)
    {
        if (context == "pianoroll")
            return "piano roll";
        if (context == "acid")
            return "acid channels";
        if (context == "sampler")
            return "drum channels";
        return context;
    }

    /** What a key in this context covers when it is pressed: the broader contexts, nearest first. */
    juce::StringArray broaderThan (const juce::String& context)
    {
        if (context == "acid" || context == "sampler")
            return { "pattern", "pianoroll", "mixer", "global" };
        if (context == "global")
            return {};
        return { "global" };
    }
} // namespace

// --- the list of bindings -------------------------------------------------------------------------

class KeysView::List final : public juce::Component
{
public:
    explicit List (KeysView& ownerView)
        : owner (ownerView)
    {
    }

    void update()
    {
        bindings = owner.keymap.inContext (owner.context);
        setSize (getWidth(), static_cast<int> (std::max (1.0f, rowHeight * static_cast<float> (bindings.size()))));
        repaint();
    }

    juce::Rectangle<float> removeArea (int row) const
    {
        return { static_cast<float> (getWidth()) - 32.0f, rowHeight * static_cast<float> (row) + 8.0f, 24.0f, 24.0f };
    }

    void paint (juce::Graphics& g) override
    {
        const auto& lnf = owner.lnf;
        const auto ink = lnf.colour (&P::ink), muted = lnf.colour (&P::inkMuted), line = lnf.colour (&P::line);
        if (bindings.empty())
        {
            g.setFont (lnf.fonts().font (type::meta));
            g.setColour (muted);
            g.drawText ("No keys here yet: press a key below and write what it should do.",
                        getLocalBounds().toFloat().withHeight (rowHeight), juce::Justification::centredLeft, false);
            return;
        }

        for (size_t i = 0; i < bindings.size(); ++i)
        {
            const auto& b = bindings[i];
            const auto row = juce::Rectangle<float> (0.0f, rowHeight * static_cast<float> (i),
                                                     static_cast<float> (getWidth()), rowHeight);
            g.setColour (line);
            g.fillRect (row.withTop (row.getBottom() - shape::lineWidth));

            // The key, in a box, like a keycap.
            g.setFont (lnf.fonts().font (type::control));
            const auto keyText = Fonts::format (type::control, b.key);
            const auto keyWidth =
                juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), keyText) + space::unit * 4.0f;
            const auto cap = juce::Rectangle<float> (keyWidth, shape::pillHeightCompact)
                                 .withPosition (0.0f, row.getCentreY() - shape::pillHeightCompact * 0.5f);
            g.setColour (ink);
            g.drawRoundedRectangle (cap.reduced (0.5f), shape::stepRadius, shape::lineWidth);
            g.drawText (keyText, cap, juce::Justification::centred, false);

            // What it does: the label, then the command line; then what it covers or clashes with.
            auto text = row.withTrimmedLeft (keyColumn).withTrimmedRight (40.0f);
            g.setFont (lnf.fonts().font (type::body));
            g.setColour (ink);
            const auto label = b.label.isNotEmpty() ? b.label : b.command;
            g.drawText (label, text.removeFromLeft (220.0f), juce::Justification::centredLeft, true);
            g.setFont (lnf.fonts().font (type::meta));
            g.setColour (muted);
            juce::String note = b.label.isNotEmpty() ? b.command : juce::String();
            if (const auto covered = owner.keymap.shadowed (b, broaderThan (b.context)))
                note << (note.isEmpty() ? "" : "   ") << "covers " << contextTitle (covered->context) << ": "
                     << (covered->label.isNotEmpty() ? covered->label : covered->command);
            g.drawText (note, text, juce::Justification::centredLeft, true);

            const auto x = removeArea (static_cast<int> (i));
            g.setColour (muted);
            g.drawLine (x.getX() + 7.0f, x.getY() + 7.0f, x.getRight() - 7.0f, x.getBottom() - 7.0f, shape::iconStroke);
            g.drawLine (x.getRight() - 7.0f, x.getY() + 7.0f, x.getX() + 7.0f, x.getBottom() - 7.0f, shape::iconStroke);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (size_t i = 0; i < bindings.size(); ++i)
            if (removeArea (static_cast<int> (i)).expanded (4.0f).contains (e.position))
            {
                owner.remove (bindings[i]);
                return;
            }
    }

private:
    KeysView& owner;
    std::vector<core::Keymap::Binding> bindings;
};

// --- the view -------------------------------------------------------------------------------------

KeysView::KeysView (TeknoLookAndFeel& teknoLookAndFeel, core::Keymap& keymapToEdit, core::CommandRegistry& registry)
    : lnf (teknoLookAndFeel)
    , keymap (keymapToEdit)
    , commands (registry)
{
    setWantsKeyboardFocus (true);
    for (const auto& c : core::Keymap::contexts())
    {
        if (c == "keys")
            continue; // this editor has no keys of its own worth showing
        auto& tab = *tabs.emplace_back (std::make_unique<juce::TextButton> (contextTitle (c)));
        setRole (tab, Role::nav);
        tab.setMouseClickGrabsKeyboardFocus (false);
        tab.setComponentID (c);
    }
    for (auto& tab : tabs)
    {
        tab->onClick = [this, button = tab.get()] { showContext (button->getComponentID()); };
        addAndMakeVisible (*tab);
    }

    list = std::make_unique<List> (*this);
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    captureButton.onClick = [this] { startCapture(); };
    addButton.onClick = [this] { add(); };
    for (auto* b : { &captureButton, &addButton })
        addAndMakeVisible (b);

    commandField.setTextToShowWhenEmpty ("what it does, as in the terminal: cutoff 0.8", lnf.colour (&P::inkMuted));
    labelField.setTextToShowWhenEmpty ("short name (optional)", lnf.colour (&P::inkMuted));
    commandField.onReturnKey = [this] { add(); };
    labelField.onReturnKey = [this] { add(); };
    for (auto* f : { &commandField, &labelField })
    {
        f->setFont (lnf.fonts().font (type::body));
        addAndMakeVisible (f);
    }
    showContext ("global");
}

KeysView::~KeysView()
{
    viewport.setViewedComponent (nullptr, false);
}

void KeysView::showContext (const juce::String& newContext)
{
    context = newContext;
    for (auto& tab : tabs)
        tab->setToggleState (tab->getComponentID() == context, juce::dontSendNotification);
    refresh();
}

void KeysView::refresh()
{
    list->setSize (viewport.getMaximumVisibleWidth(), list->getHeight());
    list->update();
    repaint();
}

void KeysView::resized()
{
    auto area = getLocalBounds().toFloat();
    auto tabRow = area.removeFromTop (shape::pillHeight);
    for (auto& tab : tabs)
    {
        const auto width = juce::GlyphArrangement::getStringWidth (
                               lnf.fonts().font (type::control), Fonts::format (type::control, tab->getButtonText())) +
                           space::gutter;
        tab->setBounds (tabRow.removeFromLeft (width).toNearestInt());
    }
    area.removeFromTop (space::gutter);

    addBar = area.removeFromBottom (shape::pillHeight);
    area.removeFromBottom (space::unit * 2.0f);
    messageArea = area.removeFromBottom (22.0f);
    area.removeFromBottom (space::gutter);
    viewport.setBounds (area.toNearestInt());

    auto bar = addBar;
    captureButton.setBounds (bar.removeFromLeft (keyColumn - space::gap).toNearestInt());
    bar.removeFromLeft (space::gap);
    addButton.setBounds (bar.removeFromRight (96.0f).toNearestInt());
    bar.removeFromRight (space::gap);
    labelField.setBounds (bar.removeFromRight (220.0f).toNearestInt());
    bar.removeFromRight (space::gap);
    commandField.setBounds (bar.toNearestInt());
    refresh();
}

void KeysView::paint (juce::Graphics& g)
{
    g.setFont (lnf.fonts().font (type::meta));
    g.setColour (messageIsProblem ? lnf.colour (&P::warn) : lnf.colour (&P::inkMuted));
    g.drawText (message.isNotEmpty()
                    ? message
                    : "A key does what you write, in " + contextTitle (context) +
                          ". The most specific context wins: drum or acid channel, then the view, then global.",
                messageArea, juce::Justification::centredLeft, true);
}

void KeysView::setMessage (const juce::String& text, bool problem)
{
    message = text;
    messageIsProblem = problem;
    repaint (messageArea.toNearestInt());
}

void KeysView::startCapture()
{
    capturing = true;
    captureButton.setButtonText ("press it now...");
    grabKeyboardFocus();
}

bool KeysView::keyPressed (const juce::KeyPress& key)
{
    if (! capturing)
        return false; // the window's keys still work in this view
    capturing = false;
    capturedKey = keys::describe (key);
    captureButton.setButtonText (*capturedKey);
    if (const auto existing = keymap.find (*capturedKey, { context }))
        setMessage ("in " + contextTitle (context) + " this key already does \"" +
                        (existing->label.isNotEmpty() ? existing->label : existing->command) + "\": adding replaces it",
                    true);
    else
        setMessage ({}, false);
    commandField.grabKeyboardFocus();
    return true;
}

void KeysView::add()
{
    if (! capturedKey)
    {
        setMessage ("press \"press a key\" first, then the key", true);
        return;
    }
    const auto command = commandField.getText().trim();
    if (command.isEmpty())
    {
        setMessage ("write what the key should do, as you would in the terminal", true);
        return;
    }
    if (const auto parsed = core::CommandLine::parse (command, commands); ! parsed.command)
    {
        setMessage (parsed.error, true);
        return;
    }
    const auto replaced = keymap.bind ({ context, *capturedKey, command, labelField.getText().trim() });
    setMessage (replaced ? "replaced \"" + replaced->command + "\"" : "added " + *capturedKey, false);
    capturedKey.reset();
    captureButton.setButtonText ("press a key");
    commandField.clear();
    labelField.clear();
    refresh();
    if (onChange)
        onChange();
}

void KeysView::remove (const core::Keymap::Binding& binding)
{
    keymap.unbind (binding.context, binding.key);
    setMessage ("removed " + binding.key, false);
    refresh();
    if (onChange)
        onChange();
}
} // namespace tekno::ui
