// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/views/EffectsView.h"

#include "ui/components/EffectGraph.h"

namespace tekno::ui
{
namespace
{
    using P = tokens::Palette;
    namespace type = tokens::type;
    namespace shape = tokens::shape;
    namespace space = tokens::space;
    using core::EffectType;

    constexpr float headerHeight = 32.0f;
    constexpr float knobSize = 40.0f;
    constexpr float knobCell = 72.0f;
    constexpr float knobRowHeight = knobSize + 36.0f; // the knob, its name and its value
    constexpr float lightSize = 8.0f;
    constexpr float captionHeight = 18.0f;
    constexpr float maxGraphHeight = 160.0f, minGraphHeight = 56.0f;

    juce::String formatValue (const core::EffectParamInfo& param, float value)
    {
        if (param.isChoice())
            return param.choices[static_cast<size_t> (
                std::clamp (static_cast<int> (value), 0, static_cast<int> (param.choices.size()) - 1))];
        const auto unit = juce::String (param.unit);
        if (unit.isEmpty())
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        if (unit == "bits")
            return juce::String (juce::roundToInt (value)) + " bits";
        if (unit == "x")
            return juce::String (value, value < 10.0f ? 1 : 0) + "x";
        if (unit == "steps")
            return juce::String (value, value < 10.0f ? 2 : 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") +
                   " st";
        return juce::String (value, std::abs (value) < 10.0f ? 1 : 0) + " " + unit;
    }
} // namespace

// --- one slot -----------------------------------------------------------------------------------------

class EffectsView::Card final : public juce::Component
{
public:
    Card (EffectsView& ownerView, int slotIndex)
        : owner (ownerView)
        , index (slotIndex)
        , graph (ownerView.lnf)
    {
        graph.onDragStart = [this] { owner.commands.beginGesture(); }; // a whole drag = one undo step
        graph.onDragEnd = [this] { owner.commands.endGesture(); };
        graph.onPointMoved = [this] (int point, float value)
        {
            const auto first = core::effectInfo (EffectType::shaper).paramIndex ("p1");
            set (static_cast<size_t> (first + point), juce::String (value, 3));
        };
        addChildComponent (graph);
        const auto& curves = core::shaperCurves();
        for (size_t c = 0; c < curveButtons.size() && c < curves.size(); ++c)
        {
            auto& b = curveButtons[c];
            b.setButtonText (curves[c].name);
            b.setTooltip (curves[c].help);
            setRole (b, Role::nav);
            b.setMouseClickGrabsKeyboardFocus (false);
            b.onClick = [this, name = juce::String (curves[c].name)]
            { owner.run ("fx.curve", { { "slot", index + 1 }, { "curve", name } }); };
            addChildComponent (b);
        }
        for (auto& knob : knobs)
        {
            knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            knob.setMouseDragSensitivity (200);
            knob.setMouseClickGrabsKeyboardFocus (false);
            knob.onDragStart = [this] { owner.commands.beginGesture(); }; // a whole drag = one undo step
            knob.onDragEnd = [this] { owner.commands.endGesture(); };
            addChildComponent (knob);
        }
        for (auto& b : choices)
        {
            b.setMouseClickGrabsKeyboardFocus (false);
            setRole (b, Role::nav);
            addChildComponent (b);
        }
        for (auto* b : { &bypass, &left, &right, &remove, &add })
        {
            b->setMouseClickGrabsKeyboardFocus (false);
            addChildComponent (b);
        }
        add.onClick = [this] { showAddMenu(); };
        remove.onClick = [this] { owner.run ("fx.remove", { { "slot", index + 1 } }); };
        bypass.onClick = [this]
        {
            owner.run ("fx.bypass", { { "slot", index + 1 },
                                      { "on", ! owner.chain().slots[static_cast<size_t> (index)].bypass.load() } });
        };
        left.onClick = [this] { owner.run ("fx.move", { { "slot", index + 1 }, { "to", index } }); };
        right.onClick = [this] { owner.run ("fx.move", { { "slot", index + 1 }, { "to", index + 2 } }); };
    }

    /** Shows the slot as it is now; rebuilds the controls only when the effect changed. */
    void sync()
    {
        auto& slot = owner.chain().slots[static_cast<size_t> (index)];
        const auto now = slot.type.load();
        if (now != shownType || ! built)
        {
            shownType = now;
            built = true;
            rebuild();
        }
        const auto& info = core::effectInfo (now);
        for (size_t p = 0; p < info.params.size(); ++p)
        {
            const auto value = slot.values[p].load();
            if (info.params[p].isChoice())
                for (size_t c = 0; c < info.params[p].choices.size(); ++c)
                    choiceFor (p, c).setToggleState (static_cast<int> (value) == static_cast<int> (c),
                                                     juce::dontSendNotification);
            else if (! knobs[p].isMouseButtonDown())
                knobs[p].setValue (value, juce::dontSendNotification);
        }
        bypass.setButtonText (slot.bypass.load() ? "off" : "on");
        left.setEnabled (index > 0);
        right.setEnabled (index < core::effectSlots - 1);

        // The light: take what the engine left, let it fall slowly. The graph's band does the same.
        const auto activity = slot.activity.exchange (0.0f), clip = slot.clip.exchange (0.0f);
        light = std::max (activity > 0.001f ? 1.0f : 0.0f, light * 0.85f);
        clipping = std::max (clip > 0.5f ? 1.0f : 0.0f, clipping * 0.9f);
        reach = std::max (slot.level.exchange (0.0f), reach * 0.9f);
        std::array<float, core::maxEffectParams> values {};
        for (size_t p = 0; p < values.size(); ++p)
            values[p] = slot.values[p].load();
        graph.show (now, values, reach);

        // The ready-made curve the points match, if any, shows as chosen.
        if (now == EffectType::shaper)
        {
            const auto first = static_cast<size_t> (core::effectInfo (now).paramIndex ("p1"));
            const auto& curves = core::shaperCurves();
            for (size_t c = 0; c < curveButtons.size() && c < curves.size(); ++c)
            {
                auto same = true;
                for (size_t k = 0; k < curves[c].points.size(); ++k)
                    same = same && std::abs (values[first + k] - curves[c].points[k]) < 1.0e-3f;
                curveButtons[c].setToggleState (same, juce::dontSendNotification);
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& lnf = owner.lnf;
        const auto ink = lnf.colour (&P::ink), muted = lnf.colour (&P::inkMuted), line = lnf.colour (&P::line);
        const auto bounds = getLocalBounds().toFloat().reduced (shape::lineWidth * 0.5f);
        g.setColour (shownType == EffectType::none ? line : ink.withAlpha (0.35f));
        g.drawRoundedRectangle (bounds, shape::contentRadius, shape::lineWidth);

        auto header = bounds.withHeight (headerHeight).reduced (space::unit * 2.0f, 0.0f);
        g.setFont (lnf.fonts().font (type::meta));
        g.setColour (muted);
        g.drawText (juce::String (index + 1), header.removeFromLeft (16.0f), juce::Justification::centredLeft, false);
        if (shownType == EffectType::none)
            return;

        // The light, left of the title: ink while it sounds, the warning colour when it clips hard.
        const auto lightArea =
            header.removeFromLeft (lightSize + space::unit * 2.0f).withSizeKeepingCentre (lightSize, lightSize);
        if (clipping > 0.05f)
            g.setColour (lnf.colour (&P::warn).withAlpha (std::max (0.4f, clipping)));
        else
            g.setColour (light > 0.05f ? ink.withAlpha (std::max (0.3f, light)) : line);
        g.fillEllipse (lightArea);

        const auto bypassed = owner.chain().slots[static_cast<size_t> (index)].bypass.load();
        g.setFont (lnf.fonts().font (type::sectionTitle));
        g.setColour (bypassed ? muted : ink);
        g.drawText (core::effectInfo (shownType).title, header, juce::Justification::centredLeft, true);

        // How to read the graph, under it.
        g.setFont (lnf.fonts().font (type::meta));
        if (graph.isVisible())
        {
            g.setColour (muted);
            g.drawText (
                EffectGraph::caption (shownType),
                graph.getBounds().toFloat().withY (static_cast<float> (graph.getBottom())).withHeight (captionHeight),
                juce::Justification::centredLeft, true);
        }

        // Knob labels and values.
        const auto& info = core::effectInfo (shownType);
        for (size_t p = 0; p < info.params.size(); ++p)
        {
            if (info.params[p].isChoice() || info.params[p].graph)
                continue;
            const auto k = knobs[p].getBounds().toFloat();
            g.setColour (muted);
            g.drawText (info.params[p].name, k.withY (k.getBottom()).withHeight (14.0f).expanded (16.0f, 0.0f),
                        juce::Justification::centred, false);
            g.setColour (ink);
            g.drawText (formatValue (info.params[p], static_cast<float> (knobs[p].getValue())),
                        k.withY (k.getBottom() + 14.0f).withHeight (14.0f).expanded (16.0f, 0.0f),
                        juce::Justification::centred, false);
        }
    }

    void resized() override { layout(); }

private:
    juce::TextButton& choiceFor (size_t param, size_t choice) { return choices[param * 4 + choice]; }

    void rebuild()
    {
        for (auto& k : knobs)
            k.setVisible (false);
        for (auto& b : choices)
            b.setVisible (false);
        const auto empty = shownType == EffectType::none;
        add.setVisible (empty);
        for (auto* b : { &bypass, &left, &right, &remove })
            b->setVisible (! empty);
        graph.setVisible (! empty);
        for (auto& b : curveButtons)
            b.setVisible (shownType == EffectType::shaper && b.getButtonText().isNotEmpty());
        if (! empty)
        {
            const auto& info = core::effectInfo (shownType);
            for (size_t p = 0; p < info.params.size(); ++p)
            {
                const auto& param = info.params[p];
                if (param.isChoice())
                {
                    for (size_t c = 0; c < param.choices.size() && c < 4; ++c)
                    {
                        auto& b = choiceFor (p, c);
                        b.setButtonText (param.choices[c]);
                        b.setTooltip (param.help);
                        b.onClick = [this, p, c]
                        { set (p, juce::String (core::effectInfo (shownType).params[p].choices[c])); };
                        b.setVisible (true);
                    }
                    continue;
                }
                if (param.graph)
                    continue; // dragged on the graph
                auto& knob = knobs[p];
                knob.setRange (param.min, param.max);
                if (juce::String (param.unit) == "bits")
                    knob.setRange (param.min, param.max, 1.0);
                knob.setDoubleClickReturnValue (true, param.defaultValue);
                knob.setTooltip (juce::String (param.help) + "  (double-click: back to default)");
                knob.onValueChange = [this, p, &knob] { set (p, juce::String (knob.getValue(), 4)); };
                knob.setVisible (true);
            }
        }
        layout();
        repaint();
    }

    void set (size_t param, const juce::String& value)
    {
        owner.run ("fx.set", { { "slot", index + 1 },
                               { "param", juce::String (core::effectInfo (shownType).params[param].name) },
                               { "value", value } });
    }

    void showAddMenu()
    {
        juce::PopupMenu menu;
        for (const auto t : core::effectTypes())
            menu.addItem (core::effectInfo (t).title,
                          [this, t]
                          {
                              owner.run ("fx.add", { { "type", juce::String (core::effectInfo (t).name) },
                                                     { "slot", index + 1 } });
                          });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (add));
    }

    void layout()
    {
        auto area = getLocalBounds().toFloat().reduced (space::unit * 2.0f);
        add.setBounds (
            area.withSizeKeepingCentre (std::min (area.getWidth(), 96.0f), shape::pillHeightCompact).toNearestInt());

        // The title has the whole top row; the buttons sit along the bottom.
        area.removeFromTop (headerHeight - space::unit * 2.0f);
        auto footer = area.removeFromBottom (shape::pillHeightCompact);
        bypass.setBounds (footer.removeFromLeft (48.0f).toNearestInt());
        remove.setBounds (footer.removeFromRight (28.0f).toNearestInt());
        right.setBounds (footer.removeFromRight (28.0f).toNearestInt());
        left.setBounds (footer.removeFromRight (28.0f).toNearestInt());
        area.removeFromTop (space::unit * 2.0f);

        if (shownType == EffectType::none)
            return;
        const auto& info = core::effectInfo (shownType);

        // Choices first, as a row of words (the waveshaper's curves too); then the graph; then the knobs
        // in a grid. The graph takes what the knobs leave, and gives way on a short card.
        for (size_t p = 0; p < info.params.size(); ++p)
            if (info.params[p].isChoice())
            {
                auto row = area.removeFromTop (shape::pillHeightCompact);
                const auto width = row.getWidth() / static_cast<float> (info.params[p].choices.size());
                for (size_t c = 0; c < info.params[p].choices.size(); ++c)
                    choiceFor (p, c).setBounds (row.removeFromLeft (width).toNearestInt());
                area.removeFromTop (space::unit * 2.0f);
            }
        if (shownType == EffectType::shaper) // two rows of three: the names stay whole
        {
            constexpr size_t perRow = 3;
            for (size_t first = 0; first < curveButtons.size(); first += perRow)
            {
                auto row = area.removeFromTop (shape::pillHeightCompact);
                const auto width = row.getWidth() / static_cast<float> (perRow);
                for (size_t c = first; c < std::min (first + perRow, curveButtons.size()); ++c)
                    curveButtons[c].setBounds (row.removeFromLeft (width).toNearestInt());
            }
            area.removeFromTop (space::unit * 2.0f);
        }

        const auto columns = std::max (1, static_cast<int> (area.getWidth() / knobCell));
        auto knobCount = 0;
        for (const auto& param : info.params)
            knobCount += param.isChoice() || param.graph ? 0 : 1;
        const auto knobRows = (knobCount + columns - 1) / columns;
        const auto graphHeight =
            std::min (maxGraphHeight, area.getHeight() - static_cast<float> (knobRows) * knobRowHeight - captionHeight -
                                          space::unit * 2.0f);
        graph.setVisible (graphHeight >= minGraphHeight);
        if (graph.isVisible())
        {
            graph.setBounds (area.removeFromTop (graphHeight).toNearestInt());
            area.removeFromTop (captionHeight + space::unit * 2.0f);
        }

        auto n = 0;
        for (size_t p = 0; p < info.params.size(); ++p)
        {
            if (info.params[p].isChoice() || info.params[p].graph)
                continue;
            const auto cell = juce::Rectangle<float> (
                area.getX() + static_cast<float> (n % columns) * (area.getWidth() / static_cast<float> (columns)),
                area.getY() + static_cast<float> (n / columns) * knobRowHeight,
                area.getWidth() / static_cast<float> (columns), knobSize);
            knobs[p].setBounds (cell.withSizeKeepingCentre (knobSize, knobSize).toNearestInt());
            ++n;
        }
    }

    EffectsView& owner;
    const int index;
    EffectType shownType = EffectType::none;
    bool built = false; // the first sync builds the controls, empty slot included
    std::array<juce::Slider, core::maxEffectParams> knobs;
    std::array<juce::TextButton, core::maxEffectParams * 4> choices;
    juce::TextButton bypass { "on" }, left { "<" }, right { ">" }, remove { "x" }, add { "+ add" };
    EffectGraph graph;
    std::array<juce::TextButton, 6> curveButtons; // the waveshaper's ready-made curves
    float light = 0.0f, clipping = 0.0f, reach = 0.0f;
};

// --- the view -----------------------------------------------------------------------------------------

EffectsView::EffectsView (TeknoLookAndFeel& teknoLookAndFeel, core::Session& sessionToShow,
                          core::CommandRegistry& registry)
    : lnf (teknoLookAndFeel)
    , session (sessionToShow)
    , commands (registry)
{
    for (auto* tab : { &channelTab, &masterTab })
    {
        setRole (*tab, Role::nav);
        tab->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (tab);
    }
    channelTab.onClick = [this]
    {
        master = false;
        timerCallback();
    };
    masterTab.setButtonText ("master");
    masterTab.onClick = [this]
    {
        master = true;
        timerCallback();
    };
    for (size_t i = 0; i < cards.size(); ++i)
    {
        cards[i] = std::make_unique<Card> (*this, static_cast<int> (i));
        addAndMakeVisible (*cards[i]);
    }
    timerCallback();
    startTimerHz (30);
}

EffectsView::~EffectsView() = default;

core::EffectChain& EffectsView::chain() noexcept
{
    return master ? session.master.effects : session.selectedChannelRef().effects;
}

juce::String EffectsView::targetName() const
{
    const auto c = std::clamp (session.selectedChannel.load(), 0, std::max (0, session.channelCount() - 1));
    return session.project.channelNames[static_cast<size_t> (c)];
}

void EffectsView::run (const juce::String& command, core::CommandArgs args)
{
    if (master)
        args.set ("master", true);
    else
        args.set ("channel", session.selectedChannel.load() + 1);
    const auto outcome = commands.execute (command, args);
    if (! outcome.succeeded())
        juce::Logger::writeToLog ("command " + command + " failed: " + outcome.error);
    timerCallback();
}

void EffectsView::timerCallback()
{
    if (! isShowing())
        return;
    const auto name = targetName();
    if (channelTab.getButtonText() != name)
        channelTab.setButtonText (name);
    channelTab.setToggleState (! master, juce::dontSendNotification);
    masterTab.setToggleState (master, juce::dontSendNotification);
    for (auto& card : cards)
        card->sync();
}

void EffectsView::resized()
{
    auto area = getLocalBounds().toFloat();
    auto tabs = area.removeFromTop (shape::pillHeight);
    titleArea = tabs.removeFromLeft (96.0f);
    channelTab.setBounds (tabs.removeFromLeft (140.0f).toNearestInt());
    masterTab.setBounds (tabs.removeFromLeft (100.0f).toNearestInt());
    area.removeFromTop (space::gap);

    // Four cards side by side when there is room, two by two when not.
    const auto across = area.getWidth() >= 4.0f * 230.0f ? 4 : 2;
    const auto rowsCount = core::effectSlots / across;
    const auto width = (area.getWidth() - space::gap * static_cast<float> (across - 1)) / static_cast<float> (across);
    const auto height =
        (area.getHeight() - space::gap * static_cast<float> (rowsCount - 1)) / static_cast<float> (rowsCount);
    for (int i = 0; i < core::effectSlots; ++i)
    {
        const auto col = i % across, row = i / across;
        cards[static_cast<size_t> (i)]->setBounds (
            juce::Rectangle<float> (area.getX() + static_cast<float> (col) * (width + space::gap),
                                    area.getY() + static_cast<float> (row) * (height + space::gap), width, height)
                .toNearestInt());
    }
}

void EffectsView::paint (juce::Graphics& g)
{
    g.setFont (lnf.fonts().font (type::sectionTitle));
    g.setColour (lnf.colour (&P::ink));
    g.drawText ("Effects", titleArea, juce::Justification::centredLeft, false);
}
} // namespace tekno::ui
