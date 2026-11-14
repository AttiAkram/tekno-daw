// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/preview/StylePreview.h"

namespace tekno::ui
{
namespace
{
    using P = tokens::Palette;
    namespace type = tokens::type;
    namespace shape = tokens::shape;
    namespace space = tokens::space;

    constexpr double previewBpm = 130.0;
    constexpr int stepsPerBeat = 4;
    constexpr float maxStepSize = 56.0f;
    constexpr float chipWidth = 112.0f;
    constexpr float accentMarkHeight = 3.0f;

    int sixteenthMs (double bpm)
    {
        return juce::roundToInt (60'000.0 / bpm / stepsPerBeat);
    }
} // namespace

StylePreview::StylePreview (TeknoLookAndFeel& teknoLookAndFeel)
    : lnf (teknoLookAndFeel)
{
    setWantsKeyboardFocus (true);

    const std::array<const char*, 4> views { "Pattern", "Piano roll", "Mixer", "Song" };
    for (size_t i = 0; i < navButtons.size(); ++i)
    {
        auto& button = navButtons[i];
        button.setButtonText (views[i]);
        setRole (button, Role::nav);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1);
        button.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (button);
    }
    navButtons[0].setToggleState (true, juce::dontSendNotification);

    bpmChip.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (bpmChip);

    setRole (recordButton, Role::primary);
    recordButton.setClickingTogglesState (true);
    addAndMakeVisible (recordButton);

    setRole (playButton, Role::primary);
    playButton.setMouseClickGrabsKeyboardFocus (false);
    playButton.onClick = [this] { setPlaying (playhead < 0); };
    addAndMakeVisible (playButton);

    // A believable acid line, so accent and slide can be judged in context.
    channels[0].name = "303";
    const std::array<Step, numSteps> acid { {
        { true, true, false },
        { false, false, false },
        { true, false, true },
        { true, false, false },
        { true, false, false },
        { false, false, false },
        { true, true, false },
        { true, false, true },
        { true, false, false },
        { false, false, false },
        { true, false, false },
        { true, true, true },
        { false, false, false },
        { true, false, false },
        { true, false, true },
        { true, false, false },
    } };
    channels[0].steps = acid;

    channels[1].name = "kick";
    channels[2].name = "hat";
    for (int beat = 0; beat < numSteps / stepsPerBeat; ++beat)
    {
        channels[1].steps[static_cast<size_t> (beat * stepsPerBeat)].on = true;
        channels[2].steps[static_cast<size_t> (beat * stepsPerBeat + 2)].on = true;
    }
}

// --- layout -------------------------------------------------------------------------------------

void StylePreview::resized()
{
    auto area = getLocalBounds().toFloat();
    topBar = area.removeFromTop (space::topBar);
    leftColumn = area.removeFromLeft (space::leftColumn);
    content = area.reduced (space::gutter * 2.0f, space::gutter);

    // Top bar: wordmark left (painted), views centred, transport right.
    // Each view label is as wide as its text plus equal padding, so the spacing reads as even.
    const auto barCentreY = topBar.getCentreY();
    const auto navFont = lnf.fonts().font (type::control);
    std::array<float, 4> navWidths {};
    auto navTotal = 0.0f;
    for (size_t i = 0; i < navButtons.size(); ++i)
    {
        const auto label = Fonts::format (type::control, navButtons[i].getButtonText());
        navWidths[i] = std::ceil (juce::GlyphArrangement::getStringWidth (navFont, label)) + space::gutter * 2.0f;
        navTotal += navWidths[i];
    }
    auto navX = topBar.getCentreX() - navTotal * 0.5f;
    for (size_t i = 0; i < navButtons.size(); ++i)
    {
        navButtons[i].setBounds (
            juce::Rectangle<float> (navX, barCentreY - shape::pillHeight * 0.5f, navWidths[i], shape::pillHeight)
                .toNearestInt());
        navX += navWidths[i];
    }

    auto transport = topBar.reduced (space::gutter, 0.0f);
    playButton.setBounds (
        transport.removeFromRight (112.0f).withSizeKeepingCentre (112.0f, shape::pillHeight).toNearestInt());
    transport.removeFromRight (space::gap);
    recordButton.setBounds (transport.removeFromRight (shape::iconButtonSize)
                                .withSizeKeepingCentre (shape::iconButtonSize, shape::iconButtonSize)
                                .toNearestInt());
    transport.removeFromRight (space::gap);
    bpmChip.setBounds (
        transport.removeFromRight (104.0f).withSizeKeepingCentre (104.0f, shape::pillHeight).toNearestInt());

    // Left column: three sections divided by thin lines.
    auto column = leftColumn.reduced (space::gutter);
    browserArea = column.removeFromTop (140.0f);
    column.removeFromTop (space::gutter);
    synthArea = column.removeFromTop (232.0f);
    column.removeFromTop (space::gutter);
    keysArea = column;

    auto knobs = synthArea.withTrimmedTop (36.0f);
    const auto knobCell = juce::Rectangle<float> (0.0f, 0.0f, knobs.getWidth() * 0.5f, 96.0f);
    for (size_t i = 0; i < knobAreas.size(); ++i)
        knobAreas[i] = knobCell.withPosition (knobs.getX() + knobCell.getWidth() * static_cast<float> (i % 2),
                                              knobs.getY() + knobCell.getHeight() * static_cast<float> (i / 2));

    // Content: title, metadata, dashed divider, then the step grid.
    auto c = content;
    titleArea = c.removeFromTop (92.0f);
    metaArea = c.removeFromTop (24.0f);
    c.removeFromTop (space::gutter);
    dividerArea = c.removeFromTop (shape::lineWidth);
    c.removeFromTop (space::gutter * 1.5f);
    beatLabelArea = c.removeFromTop (24.0f);
    c.removeFromTop (space::unit * 3.0f);

    // 16 steps = 4 beats. The 4-column grid of the design IS the bar: gap 12 between beats.
    const auto stepGap = space::unit;
    const auto gridWidth = c.getWidth() - chipWidth - space::gutter;
    const auto fixedGaps = space::gap * 3.0f + stepGap * 3.0f * 4.0f;
    const auto stepSize =
        juce::jmin (maxStepSize, std::floor ((gridWidth - fixedGaps) / static_cast<float> (numSteps)));
    const auto rowHeight = stepSize + 24.0f; // room for the accent mark above and the slide line below

    for (size_t ch = 0; ch < channels.size(); ++ch)
    {
        auto row = c.removeFromTop (rowHeight);
        c.removeFromTop (space::gap);

        chipAreas[ch] = row.removeFromLeft (chipWidth).withSizeKeepingCentre (chipWidth, shape::pillHeight);
        row.removeFromLeft (space::gutter);

        auto x = row.getX();
        for (int s = 0; s < numSteps; ++s)
        {
            stepAreas[ch][static_cast<size_t> (s)] = { x, row.getCentreY() - stepSize * 0.5f, stepSize, stepSize };
            const auto endOfBeat = (s + 1) % stepsPerBeat == 0;
            x += stepSize + (endOfBeat ? space::gap : stepGap);
        }
    }
}

// --- painting -----------------------------------------------------------------------------------

void StylePreview::paint (juce::Graphics& g)
{
    g.fillAll (lnf.colour (&P::bg));
    paintTopBar (g);
    paintLeftColumn (g);
    paintContent (g);
}

void StylePreview::drawText (juce::Graphics& g, const juce::String& text, const tokens::TextStyle& style,
                             juce::Colour colour, juce::Rectangle<float> area, juce::Justification justification) const
{
    g.setFont (lnf.fonts().font (style));
    g.setColour (colour);
    g.drawText (Fonts::format (style, text), area, justification, true);
}

void StylePreview::paintTopBar (juce::Graphics& g) const
{
    drawText (g, "tekno-daw", type::wordmark, lnf.colour (&P::ink),
              topBar.withTrimmedLeft (space::gutter).withWidth (200.0f));

    g.setColour (lnf.colour (&P::line));
    g.fillRect (topBar.withTop (topBar.getBottom() - shape::lineWidth));
}

void StylePreview::paintLeftColumn (juce::Graphics& g) const
{
    const auto ink = lnf.colour (&P::ink);
    const auto muted = lnf.colour (&P::inkMuted);

    g.setColour (lnf.colour (&P::line));
    g.fillRect (leftColumn.withLeft (leftColumn.getRight() - shape::lineWidth));
    for (auto section : { synthArea, keysArea })
        g.fillRect (juce::Rectangle<float> (leftColumn.getX(), section.getY() - space::gutter * 0.5f,
                                            leftColumn.getWidth() - shape::lineWidth, shape::lineWidth));

    // Browser
    auto browser = browserArea;
    drawText (g, "Browser", type::sectionTitle, ink, browser.removeFromTop (28.0f));
    for (auto* item : { "Presets", "Samples", "Plugins" })
        drawText (g, item, type::body, ink, browser.removeFromTop (26.0f));
    drawText (g, "empty until M3", type::meta, muted, browser.removeFromTop (22.0f));

    // 303
    drawText (g, "303 / acid", type::sectionTitle, ink, synthArea.withHeight (28.0f));
    const std::array<std::pair<const char*, float>, 4> knobs {
        { { "cutoff", 0.35f }, { "resonance", 0.8f }, { "env mod", 0.6f }, { "decay", 0.45f } }
    };
    for (size_t i = 0; i < knobs.size(); ++i)
        paintKnob (g, knobAreas[i], knobs[i].first, knobs[i].second);

    // Keys of this preview
    auto keys = keysArea;
    drawText (g, "Preview keys", type::sectionTitle, ink, keys.removeFromTop (28.0f));
    const std::array<std::pair<const char*, const char*>, 5> hints { { { "T", "theme light / dark" },
                                                                       { "Space", "play / stop" },
                                                                       { "Click", "step on / off" },
                                                                       { "Shift+click", "accent" },
                                                                       { "Ctrl+click", "slide" } } };
    for (const auto& [key, what] : hints)
    {
        auto line = keys.removeFromTop (22.0f);
        drawText (g, key, type::meta, ink, line.removeFromLeft (92.0f));
        drawText (g, what, type::meta, muted, line);
    }
}

void StylePreview::paintKnob (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label,
                              float value) const
{
    // Proposal for open question 8: a thin ring, the value as an ink arc. No 3D, no pointer.
    constexpr float diameter = 52.0f;
    constexpr float startAngle = -juce::MathConstants<float>::pi * 0.75f;
    constexpr float sweep = juce::MathConstants<float>::pi * 1.5f;

    const auto circle = area.removeFromTop (diameter + 8.0f).withSizeKeepingCentre (diameter, diameter);

    g.setColour (lnf.colour (&P::line));
    g.drawEllipse (circle, shape::lineWidth);

    juce::Path arc;
    arc.addCentredArc (circle.getCentreX(), circle.getCentreY(), diameter * 0.5f - 4.0f, diameter * 0.5f - 4.0f, 0.0f,
                       startAngle, startAngle + sweep * value, true);
    g.setColour (lnf.colour (&P::ink));
    g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    drawText (g, label, type::meta, lnf.colour (&P::inkMuted), area.removeFromTop (20.0f),
              juce::Justification::centred);
}

void StylePreview::paintContent (juce::Graphics& g) const
{
    const auto ink = lnf.colour (&P::ink);
    const auto muted = lnf.colour (&P::inkMuted);

    drawText (g, "acid 01", type::pageTitle, ink, titleArea, juce::Justification::bottomLeft);
    drawText (g, "Pattern 1  /  16 steps  /  130 bpm  /  style preview, nothing here makes sound yet", type::meta,
              muted, metaArea);

    // Dashed divider between header and content
    const float dashes[] { shape::dashLength, shape::dashGap };
    g.setColour (lnf.colour (&P::line));
    g.drawDashedLine ({ dividerArea.getX(), dividerArea.getY(), dividerArea.getRight(), dividerArea.getY() }, dashes, 2,
                      shape::lineWidth);

    // Beat numbers over the first step of each beat, and the playhead as a dot (not a colour).
    for (int beat = 0; beat < numSteps / stepsPerBeat; ++beat)
    {
        const auto first = stepAreas[0][static_cast<size_t> (beat * stepsPerBeat)];
        drawText (g, juce::String (beat + 1), type::meta, muted,
                  beatLabelArea.withX (first.getX()).withWidth (first.getWidth()));
    }
    if (playhead >= 0)
    {
        const auto column = stepAreas[0][static_cast<size_t> (playhead)];
        const auto d = shape::navDotSize;
        g.setColour (ink);
        g.fillEllipse (column.getCentreX() - d * 0.5f, beatLabelArea.getBottom() - d, d, d);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto chip = chipAreas[static_cast<size_t> (ch)].reduced (shape::lineWidth * 0.5f);
        const auto selected = ch == selectedChannel;
        const auto radius = chip.getHeight() * 0.5f;
        if (selected)
        {
            g.setColour (ink);
            g.fillRoundedRectangle (chip, radius);
        }
        g.setColour (selected ? ink : lnf.colour (&P::line));
        g.drawRoundedRectangle (chip, radius, shape::lineWidth);
        drawText (g, channels[static_cast<size_t> (ch)].name, type::control, selected ? lnf.colour (&P::bg) : ink, chip,
                  juce::Justification::centred);

        for (int s = 0; s < numSteps; ++s)
            paintStep (g, ch, s);
    }
}

void StylePreview::paintStep (juce::Graphics& g, int channel, int stepIndex) const
{
    const auto& step = channels[static_cast<size_t> (channel)].steps[static_cast<size_t> (stepIndex)];
    const auto area = stepAreas[static_cast<size_t> (channel)][static_cast<size_t> (stepIndex)];
    const auto box = area.reduced (shape::lineWidth * 0.5f);
    const auto ink = lnf.colour (&P::ink);

    // Proposal for open question 2: yellow only for a step that is ON and under the playhead,
    // i.e. the one actually sounding. Everything else is ink: filled = on, outline = off.
    const auto sounding = step.on && stepIndex == playhead;
    const auto isHovered = hovered == StepRef { channel, stepIndex };

    if (step.on)
    {
        g.setColour (sounding ? lnf.colour (&P::accent) : ink);
        g.fillRoundedRectangle (box, shape::stepRadius);
    }
    g.setColour (sounding ? lnf.colour (&P::accentOutline) : (step.on || isHovered) ? ink : lnf.colour (&P::line));
    g.drawRoundedRectangle (box, shape::stepRadius, shape::lineWidth);

    // Proposal for open question 4: accent = bar above, slide = line to the next step.
    g.setColour (ink);
    if (step.accent)
        g.fillRect (area.getX(), area.getY() - accentMarkHeight - space::unit, area.getWidth(), accentMarkHeight);

    if (step.slide && stepIndex + 1 < numSteps)
    {
        const auto next = stepAreas[static_cast<size_t> (channel)][static_cast<size_t> (stepIndex + 1)];
        const auto y = area.getBottom() + space::unit * 2.0f;
        g.drawLine (area.getCentreX(), y, next.getCentreX(), y, shape::iconStroke);
        g.fillEllipse (area.getCentreX() - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
}

// --- interaction --------------------------------------------------------------------------------

StylePreview::StepRef StylePreview::stepAt (juce::Point<float> position) const
{
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSteps; ++s)
            if (stepAreas[static_cast<size_t> (ch)][static_cast<size_t> (s)].contains (position))
                return { ch, s };
    return {};
}

void StylePreview::mouseMove (const juce::MouseEvent& e)
{
    const auto now = stepAt (e.position);
    if (now != hovered)
    {
        hovered = now;
        repaint();
    }
}

void StylePreview::mouseExit (const juce::MouseEvent&)
{
    hovered = {};
    repaint();
}

void StylePreview::mouseDown (const juce::MouseEvent& e)
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (chipAreas[static_cast<size_t> (ch)].contains (e.position))
        {
            selectedChannel = ch;
            repaint();
            return;
        }
    }

    const auto ref = stepAt (e.position);
    if (! ref.isValid())
        return;

    auto& step = channels[static_cast<size_t> (ref.channel)].steps[static_cast<size_t> (ref.step)];
    if (e.mods.isShiftDown())
    {
        step.accent = ! step.accent;
        step.on = step.on || step.accent;
    }
    else if (e.mods.isCtrlDown())
    {
        step.slide = ! step.slide;
        step.on = step.on || step.slide;
    }
    else
    {
        step = { ! step.on, false, false };
    }
    repaint();
}

bool StylePreview::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        setPlaying (playhead < 0);
        return true;
    }
    if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')
    {
        toggleTheme();
        return true;
    }
    return false;
}

void StylePreview::setPlaying (bool shouldPlay)
{
    if (shouldPlay)
    {
        playhead = 0;
        startTimer (sixteenthMs (previewBpm)); // visual only: a GUI timer is NOT how M1 will keep time
    }
    else
    {
        stopTimer();
        playhead = -1;
    }
    playButton.setButtonText (shouldPlay ? "stop" : "play");
    repaint();
}

void StylePreview::timerCallback()
{
    playhead = (playhead + 1) % numSteps;
    repaint (content.toNearestInt());
}

void StylePreview::toggleTheme()
{
    lnf.setTheme (lnf.getTheme() == tokens::Theme::dark ? tokens::Theme::light : tokens::Theme::dark);
    if (auto* top = getTopLevelComponent())
        top->sendLookAndFeelChange();
    else
        sendLookAndFeelChange();
}
} // namespace tekno::ui
