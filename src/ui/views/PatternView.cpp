// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/views/PatternView.h"

#include "core/model/NoteNames.h"

namespace tekno::ui
{
namespace
{
    using P = tokens::Palette;
    namespace type = tokens::type;
    namespace shape = tokens::shape;
    namespace space = tokens::space;
    using core::Pattern;
    using core::Session;

    constexpr int stepsPerBeat = 4;
    constexpr float maxStepSize = 64.0f;
    constexpr float minStepSize = 28.0f;
    constexpr float chipWidth = 112.0f;
    constexpr float knobSize = 52.0f;
    constexpr float accentMarkHeight = 3.0f;
    constexpr float rowExtra = 52.0f; // accent mark above, slide line + note name below
    constexpr float patternChipSize = 28.0f;
    constexpr int refreshHz = 60;

    juce::String formatBpm (double bpm)
    {
        return juce::String (bpm, juce::exactlyEqual (bpm, std::floor (bpm)) ? 0 : 1) + " bpm";
    }

    juce::String formatSwing (double percent)
    {
        return "swing " + juce::String (juce::roundToInt (percent)) + "%";
    }
} // namespace

PatternView::PatternView (TeknoLookAndFeel& teknoLookAndFeel, Session& sessionToShow, core::CommandRegistry& registry,
                          ProjectActions& projectActions, std::function<AudioStatus()> audioStatusProvider)
    : lnf (teknoLookAndFeel)
    , session (sessionToShow)
    , commands (registry)
    , actions (projectActions)
    , audioStatus (std::move (audioStatusProvider))
    , swingChip (core::TransportState::minSwing, core::TransportState::maxSwing, core::TransportState::minSwing,
                 formatSwing)
    , bpmChip (core::TransportState::minBpm, core::TransportState::maxBpm, core::TransportState::defaultBpm, formatBpm)
    , lengthChip (1, Pattern::maxSteps, Pattern::defaultLength,
                  [] (double steps) { return juce::String (juce::roundToInt (steps)) + " steps"; })
    , knobs {
        { { "cutoff", "synth.cutoff", &sessionToShow.synth.cutoff, core::AcidParams::defaultCutoff, {} },
          { "resonance", "synth.resonance", &sessionToShow.synth.resonance, core::AcidParams::defaultResonance, {} },
          { "env mod", "synth.envmod", &sessionToShow.synth.envMod, core::AcidParams::defaultEnvMod, {} },
          { "decay", "synth.decay", &sessionToShow.synth.decay, core::AcidParams::defaultDecay, {} },
          { "accent", "synth.accent", &sessionToShow.synth.accent, core::AcidParams::defaultAccent, {} } }
    }
{
    setWantsKeyboardFocus (true);

    const std::array<const char*, 4> views { "Pattern", "Piano roll", "Mixer", "Song" };
    for (size_t i = 0; i < navButtons.size(); ++i)
    {
        auto& button = navButtons[i];
        button.setButtonText (views[i]);
        setRole (button, Role::nav);
        button.setMouseClickGrabsKeyboardFocus (false);
        button.setEnabled (i == 0); // the other views arrive in M3 and M6
        addAndMakeVisible (button);
    }
    navButtons[0].setToggleState (true, juce::dontSendNotification);

    swingChip.onChange = [this] (double percent) { run ("transport.swing", { { "percent", percent } }); };
    bpmChip.onChange = [this] (double bpm) { run ("transport.bpm", { { "bpm", bpm } }); };
    lengthChip.onChange = [this] (double steps) { run ("pattern.length", { { "steps", juce::roundToInt (steps) } }); };
    for (auto* chip : { &swingChip, &bpmChip, &lengthChip })
        addAndMakeVisible (chip);

    setRole (playButton, Role::primary);
    playButton.onClick = [this] { run ("transport.toggle"); };
    waveButton.onClick = [this] { run ("synth.waveform", { { "square", ! session.synth.square.load() } }); };
    newButton.onClick = [this] { actions.newProject(); };
    openButton.onClick = [this] { actions.openProject(); };
    saveButton.onClick = [this] { actions.save(); };
    for (auto* b : { &playButton, &waveButton, &newButton, &openButton, &saveButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    for (auto& knob : knobs)
    {
        auto& s = knob.slider;
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRange (0.0, 1.0);
        s.setMouseDragSensitivity (200);
        s.setDoubleClickReturnValue (true, knob.defaultValue);
        s.setValue (knob.value->load(), juce::dontSendNotification);
        s.setMouseClickGrabsKeyboardFocus (false);
        s.onValueChange = [this, &knob] { run (knob.command, { { "value", knob.slider.getValue() } }); };
        addAndMakeVisible (s);
    }

    renameEditor.setFont (lnf.fonts().font (type::viewTitle));
    renameEditor.setJustification (juce::Justification::centredLeft);
    renameEditor.onReturnKey = [this] { finishRename (true); };
    renameEditor.onEscapeKey = [this] { finishRename (false); };
    renameEditor.onFocusLost = [this] { finishRename (true); };
    addChildComponent (renameEditor);

    shown = takeSnapshot();
    shownAudio = audioStatus ? audioStatus() : AudioStatus {};
    timerCallback();
    startTimerHz (refreshHz);
}

void PatternView::run (const juce::String& command, const core::CommandArgs& args)
{
    const auto outcome = commands.execute (command, args);
    if (! outcome.succeeded())
        juce::Logger::writeToLog ("command " + command + " failed: " + outcome.error);
}

// --- state --------------------------------------------------------------------------------------

PatternView::Snapshot PatternView::takeSnapshot() const
{
    Snapshot s;
    const auto& pattern = session.selectedPattern();
    for (int i = 0; i < Pattern::maxSteps; ++i)
        s.steps[static_cast<size_t> (i)] = pattern.getStep (i).pack();
    s.length = pattern.getLength();
    s.selected = session.selected.load();
    s.playingPattern = session.playing.load();
    for (int p = 0; p < Session::maxPatterns; ++p)
        if (! session.pattern (p).isEmpty())
            s.usedPatterns |= 1u << p;
    s.playing = session.transport.playing.load();
    s.currentStep = s.playing && s.playingPattern == s.selected ? session.transport.currentStep.load() : -1;
    s.bpm = session.transport.bpm.load();
    s.swing = session.transport.swing.load();
    s.square = session.synth.square.load();
    s.patternName = session.project.patternNames[static_cast<size_t> (s.selected)];
    s.documentTitle = actions.documentTitle();
    return s;
}

void PatternView::timerCallback()
{
    // Controls follow the session (changed by undo, opening a file, later the terminal or remote
    // clients), unless the user is holding them right now.
    for (auto& knob : knobs)
        if (! knob.slider.isMouseButtonDown())
            knob.slider.setValue (knob.value->load(), juce::dontSendNotification);

    const auto now = takeSnapshot();
    if (! bpmChip.isBeingDragged())
        bpmChip.setValue (now.bpm);
    if (! swingChip.isBeingDragged())
        swingChip.setValue (now.swing);
    if (! lengthChip.isBeingDragged())
        lengthChip.setValue (now.length);
    playButton.setButtonText (now.playing ? "stop" : "play");
    waveButton.setButtonText (now.square ? "square" : "saw");

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        if (window->getName() != "tekno-daw  /  " + now.documentTitle)
            window->setName ("tekno-daw  /  " + now.documentTitle);

    if (++timerTicks % refreshHz == 0 && audioStatus)
    {
        const auto audio = audioStatus();
        if (audio.running != shownAudio.running || audio.deviceName != shownAudio.deviceName ||
            audio.problem != shownAudio.problem)
        {
            shownAudio = audio;
            repaint (leftColumn.toNearestInt());
        }
    }

    if (now != shown)
    {
        const auto relayout = now.length != shown.length;
        shown = now;
        if (relayout)
            layoutSteps();
        repaint();
    }
}

// --- layout -------------------------------------------------------------------------------------

void PatternView::resized()
{
    auto area = getLocalBounds().toFloat();
    topBar = area.removeFromTop (space::topBar);
    leftColumn = area.removeFromLeft (space::leftColumn);
    content = area.reduced (space::gutter * 2.0f, space::gutter);

    // Top bar: views centred, each as wide as its label so the spacing reads as even.
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
        navButtons[i].setBounds (juce::Rectangle<float> (navX, topBar.getCentreY() - shape::pillHeight * 0.5f,
                                                         navWidths[i], shape::pillHeight)
                                     .toNearestInt());
        navX += navWidths[i];
    }

    auto transport = topBar.reduced (space::gutter, 0.0f);
    auto pill = [&transport] (juce::Component& c, float width)
    {
        c.setBounds (transport.removeFromRight (width).withSizeKeepingCentre (width, shape::pillHeight).toNearestInt());
        transport.removeFromRight (space::gap);
    };
    pill (playButton, 112.0f);
    pill (bpmChip, 112.0f);
    pill (swingChip, 128.0f);

    // Left column: project, 303, keys, audio — divided by thin lines.
    auto column = leftColumn.reduced (space::gutter);
    projectArea = column.removeFromTop (112.0f);
    column.removeFromTop (space::gutter);
    synthArea = column.removeFromTop (316.0f);
    column.removeFromTop (space::gutter);
    audioArea = column.removeFromBottom (76.0f);
    column.removeFromBottom (space::gutter);
    keysArea = column;

    auto buttons = projectArea.withTrimmedTop (projectArea.getHeight() - shape::pillHeightCompact);
    const auto buttonWidth = (buttons.getWidth() - space::unit * 2.0f) / 3.0f;
    for (auto* b : { &newButton, &openButton, &saveButton })
    {
        b->setBounds (buttons.removeFromLeft (buttonWidth).toNearestInt());
        buttons.removeFromLeft (space::unit);
    }

    auto knobGrid = synthArea.withTrimmedTop (36.0f);
    const auto cellWidth = knobGrid.getWidth() * 0.5f;
    constexpr float cellHeight = 92.0f;
    for (size_t i = 0; i <= knobs.size(); ++i)
    {
        const auto cell =
            juce::Rectangle<float> (knobGrid.getX() + cellWidth * static_cast<float> (i % 2),
                                    knobGrid.getY() + cellHeight * static_cast<float> (i / 2), cellWidth, cellHeight);
        const auto top = cell.withHeight (knobSize + 8.0f);
        if (i < knobs.size())
            knobs[i].slider.setBounds (top.withSizeKeepingCentre (knobSize, knobSize).toNearestInt());
        else // the sixth cell holds the waveform switch
            waveButton.setBounds (top.withSizeKeepingCentre (cellWidth - space::gap, shape::pillHeight).toNearestInt());
    }

    // Content: title, metadata, dashed divider, pattern bank, step grid.
    auto c = content;
    titleArea = c.removeFromTop (92.0f);
    metaArea = c.removeFromTop (24.0f);
    c.removeFromTop (space::gutter);
    dividerArea = c.removeFromTop (shape::lineWidth);
    c.removeFromTop (space::gutter);
    patternBarArea = c.removeFromTop (shape::pillHeight);
    c.removeFromTop (space::gutter);
    gridArea = c;

    // Pattern bank: 16 squares in groups of 4, the length chip on the right.
    lengthChip.setBounds (patternBarArea.withLeft (patternBarArea.getRight() - 120.0f).toNearestInt());
    auto x = patternBarArea.getX();
    for (int p = 0; p < Session::maxPatterns; ++p)
    {
        patternChips[static_cast<size_t> (p)] = juce::Rectangle<float> (
            x, patternBarArea.getCentreY() - patternChipSize * 0.5f, patternChipSize, patternChipSize);
        x += patternChipSize + ((p + 1) % 4 == 0 ? space::gap : space::unit);
    }

    if (renameEditor.isVisible())
        renameEditor.setBounds (titleArea.withTrimmedTop (titleArea.getHeight() - 56.0f).toNearestInt());

    layoutSteps();
}

void PatternView::layoutSteps()
{
    // Rows of 16 steps = one bar each. Steps shrink to fit the width and the number of rows.
    const auto length = session.selectedPattern().getLength();
    const auto rows = (length + stepsPerRow - 1) / stepsPerRow;
    laidOutLength = length;

    auto g = gridArea;
    beatLabelArea = g.removeFromTop (20.0f);
    g.removeFromTop (space::unit * 3.0f);

    const auto stepGap = space::unit;
    const auto gridWidth = g.getWidth() - chipWidth - space::gutter;
    const auto fixedGaps = space::gap * 3.0f + stepGap * 3.0f * 4.0f;
    const auto byWidth = std::floor ((gridWidth - fixedGaps) / static_cast<float> (stepsPerRow));
    const auto byHeight = std::floor (g.getHeight() / static_cast<float> (rows) - rowExtra - space::gap);
    const auto stepSize = juce::jlimit (minStepSize, maxStepSize, juce::jmin (byWidth, byHeight));

    chipArea = g.withHeight (stepSize).withWidth (chipWidth).withSizeKeepingCentre (chipWidth, shape::pillHeight);
    const auto left = g.getX() + chipWidth + space::gutter;
    for (int s = 0; s < Pattern::maxSteps; ++s)
    {
        const auto row = s / stepsPerRow, col = s % stepsPerRow;
        auto sx = left;
        for (int k = 0; k < col; ++k)
            sx += stepSize + ((k + 1) % stepsPerBeat == 0 ? space::gap : stepGap);
        const auto sy = g.getY() + static_cast<float> (row) * (stepSize + rowExtra + space::gap);
        stepAreas[static_cast<size_t> (s)] = { sx, sy, stepSize, stepSize };
    }
}

// --- painting -----------------------------------------------------------------------------------

void PatternView::paint (juce::Graphics& g)
{
    g.fillAll (lnf.colour (&P::bg));
    paintTopBar (g);
    paintLeftColumn (g);
    paintContent (g);
}

void PatternView::drawText (juce::Graphics& g, const juce::String& text, const tokens::TextStyle& style,
                            juce::Colour colour, juce::Rectangle<float> area, juce::Justification justification) const
{
    g.setFont (lnf.fonts().font (style));
    g.setColour (colour);
    g.drawText (Fonts::format (style, text), area, justification, true);
}

void PatternView::paintBadge (juce::Graphics& g, juce::Rectangle<float> area, bool ok, const juce::String& letter) const
{
    const auto box = area.withSizeKeepingCentre (shape::badgeSize, shape::badgeSize);
    g.setColour (ok ? lnf.colour (&P::ok) : lnf.colour (&P::warn));
    g.fillRoundedRectangle (box, shape::badgeRadius);
    drawText (g, letter, type::control, ok ? lnf.colour (&P::onOk) : lnf.colour (&P::onWarn), box,
              juce::Justification::centred);
}

void PatternView::paintTopBar (juce::Graphics& g) const
{
    drawText (g, "tekno-daw", type::wordmark, lnf.colour (&P::ink),
              topBar.withTrimmedLeft (space::gutter).withWidth (200.0f));
    g.setColour (lnf.colour (&P::line));
    g.fillRect (topBar.withTop (topBar.getBottom() - shape::lineWidth));
}

void PatternView::paintLeftColumn (juce::Graphics& g) const
{
    const auto ink = lnf.colour (&P::ink);
    const auto muted = lnf.colour (&P::inkMuted);

    g.setColour (lnf.colour (&P::line));
    g.fillRect (leftColumn.withLeft (leftColumn.getRight() - shape::lineWidth));
    for (auto section : { synthArea, keysArea, audioArea })
        g.fillRect (juce::Rectangle<float> (leftColumn.getX(), section.getY() - space::gutter * 0.5f,
                                            leftColumn.getWidth() - shape::lineWidth, shape::lineWidth));

    auto project = projectArea;
    drawText (g, "Project", type::sectionTitle, ink, project.removeFromTop (28.0f));
    drawText (g, actions.documentName(), type::meta, ink, project.removeFromTop (22.0f));
    drawText (g, session.project.hasUnsavedChanges() ? "unsaved changes" : "saved", type::meta, muted,
              project.removeFromTop (22.0f));

    drawText (g, "303 / acid", type::sectionTitle, ink, synthArea.withHeight (28.0f));
    for (const auto& knob : knobs)
    {
        const auto b = knob.slider.getBounds().toFloat();
        drawText (g, knob.label, type::meta, muted,
                  b.withY (b.getBottom() + 4.0f).withHeight (20.0f).expanded (30.0f, 0.0f),
                  juce::Justification::centred);
    }

    auto keys = keysArea;
    drawText (g, "Keys", type::sectionTitle, ink, keys.removeFromTop (28.0f));
    const std::array<std::pair<const char*, const char*>, 8> hints { {
        { "Space", "play / stop" },
        { "Click", "step on / off" },
        { "Shift Ctrl Alt", "accent, slide, tie" },
        { "Wheel", "pitch" },
        { "PgUp / PgDn", "pattern" },
        { "Ctrl+Z / Y", "undo / redo" },
        { "Ctrl+S / O", "save / open" },
        { "T", "theme" },
    } };
    for (const auto& [key, what] : hints)
    {
        if (keys.getHeight() < 20.0f)
            break;
        auto line = keys.removeFromTop (20.0f);
        drawText (g, key, type::meta, ink, line.removeFromLeft (92.0f));
        drawText (g, what, type::meta, muted, line);
    }

    auto audio = audioArea;
    drawText (g, "Audio", type::sectionTitle, ink, audio.removeFromTop (28.0f));
    auto line = audio.removeFromTop (22.0f);
    paintBadge (g, line.removeFromLeft (shape::badgeSize), shownAudio.running, shownAudio.running ? "A" : "!");
    line.removeFromLeft (space::unit * 2.0f);
    const auto detail = audio.removeFromTop (22.0f).withTrimmedLeft (shape::badgeSize + space::unit * 2.0f);
    if (shownAudio.running)
    {
        drawText (g, shownAudio.deviceName, type::meta, ink, line);
        drawText (g,
                  juce::String (shownAudio.sampleRate / 1000.0, 1) + " kHz  /  " +
                      juce::String (shownAudio.bufferSize) + " samples",
                  type::meta, muted, detail);
    }
    else
    {
        drawText (g, "no audio output", type::meta, ink, line);
        drawText (g, shownAudio.problem, type::meta, muted, detail);
    }
}

void PatternView::paintContent (juce::Graphics& g) const
{
    const auto ink = lnf.colour (&P::ink);
    const auto muted = lnf.colour (&P::inkMuted);
    const auto line = lnf.colour (&P::line);

    if (! renameEditor.isVisible())
        drawText (g, shown.patternName, type::pageTitle, ink, titleArea, juce::Justification::bottomLeft);

    auto meta = "Pattern " + juce::String (shown.selected + 1) + " of " + juce::String (Session::maxPatterns) +
                "  /  " + formatBpm (shown.bpm) + "  /  " + formatSwing (shown.swing) + "  /  ";
    if (! shown.playing)
        meta << "stopped";
    else if (shown.playingPattern == shown.selected)
        meta << "playing";
    else
        meta << "playing pattern " << (shown.playingPattern + 1) << ", this one starts at the next bar";
    drawText (g, meta, type::meta, muted, metaArea);

    const float dashes[] { shape::dashLength, shape::dashGap };
    g.setColour (line);
    g.drawDashedLine ({ dividerArea.getX(), dividerArea.getY(), dividerArea.getRight(), dividerArea.getY() }, dashes, 2,
                      shape::lineWidth);

    // Pattern bank: filled = editing, outline ink = holds something, dot under = playing.
    for (int p = 0; p < Session::maxPatterns; ++p)
    {
        const auto box = patternChips[static_cast<size_t> (p)].reduced (shape::lineWidth * 0.5f);
        const auto isSelected = p == shown.selected;
        const auto used = (shown.usedPatterns >> p) & 1u;
        if (isSelected)
        {
            g.setColour (ink);
            g.fillRoundedRectangle (box, shape::stepRadius);
        }
        g.setColour (isSelected || used ? ink : line);
        g.drawRoundedRectangle (box, shape::stepRadius, shape::lineWidth);
        drawText (g, juce::String (p + 1), type::meta,
                  isSelected ? lnf.colour (&P::bg)
                  : used     ? ink
                             : muted,
                  box, juce::Justification::centred);
        if (shown.playing && p == shown.playingPattern)
        {
            const auto d = shape::navDotSize;
            g.setColour (ink);
            g.fillEllipse (box.getCentreX() - d * 0.5f, box.getBottom() + 3.0f, d, d);
        }
    }

    for (int beat = 0; beat < stepsPerRow / stepsPerBeat; ++beat)
    {
        const auto first = stepAreas[static_cast<size_t> (beat * stepsPerBeat)];
        drawText (g, juce::String (beat + 1), type::meta, muted,
                  beatLabelArea.withX (first.getX()).withWidth (first.getWidth()));
    }

    const auto chip = chipArea.reduced (shape::lineWidth * 0.5f);
    g.setColour (ink);
    g.fillRoundedRectangle (chip, chip.getHeight() * 0.5f);
    drawText (g, "303", type::control, lnf.colour (&P::bg), chip, juce::Justification::centred);
    for (int row = 1; row * stepsPerRow < shown.length; ++row)
    {
        const auto first = stepAreas[static_cast<size_t> (row * stepsPerRow)];
        drawText (g, "bar " + juce::String (row + 1), type::meta, muted,
                  juce::Rectangle<float> (chipArea.getX(), first.getY(), chipWidth, first.getHeight()));
    }

    for (int s = 0; s < shown.length; ++s)
        paintStep (g, s);
}

void PatternView::paintStep (juce::Graphics& g, int index) const
{
    const auto step = core::Step::unpack (shown.steps[static_cast<size_t> (index)]);
    const auto area = stepAreas[static_cast<size_t> (index)];
    const auto box = area.reduced (shape::lineWidth * 0.5f);
    const auto ink = lnf.colour (&P::ink);
    const auto muted = lnf.colour (&P::inkMuted);

    // Yellow only for the step that is sounding right now; on = filled ink, off = outline.
    const auto sounding = step.on && index == shown.currentStep;
    const auto fill = sounding ? lnf.colour (&P::accent) : ink;

    if (step.on)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (box, shape::stepRadius);

        // A tie fuses the step with the previous one: the gap between them is filled.
        const auto previous =
            index > 0 ? core::Step::unpack (shown.steps[static_cast<size_t> (index - 1)]) : core::Step {};
        if (step.tie && previous.on && index % stepsPerRow != 0)
        {
            const auto prevArea = stepAreas[static_cast<size_t> (index - 1)];
            g.setColour (ink);
            g.fillRect (juce::Rectangle<float> (prevArea.getRight() - shape::stepRadius, box.getY(),
                                                area.getX() - prevArea.getRight() + shape::stepRadius * 2.0f,
                                                box.getHeight()));
            g.setColour (fill);
            g.fillRoundedRectangle (box, shape::stepRadius);
        }
    }
    g.setColour (sounding                            ? lnf.colour (&P::accentOutline)
                 : (step.on || index == hoveredStep) ? ink
                                                     : lnf.colour (&P::line));
    g.drawRoundedRectangle (box, shape::stepRadius, shape::lineWidth);

    const auto slideY = area.getBottom() + space::unit * 2.0f;
    const auto labelArea = area.withY (slideY + space::unit).withHeight (20.0f);
    if (index == shown.currentStep) // the playhead, readable without colour: a dot under the step
    {
        const auto d = shape::navDotSize;
        g.setColour (ink);
        g.fillEllipse (area.getCentreX() - d * 0.5f, labelArea.getBottom() + 2.0f, d, d);
    }

    if (! step.on)
        return;

    g.setColour (ink);
    if (step.accent) // a bar above the step
        g.fillRect (area.getX(), area.getY() - accentMarkHeight - space::unit, area.getWidth(), accentMarkHeight);

    const auto nextIndex = index + 1 < shown.length ? index + 1 : -1;
    const auto next =
        nextIndex >= 0 ? core::Step::unpack (shown.steps[static_cast<size_t> (nextIndex)]) : core::Step {};
    if (step.slide && next.on && nextIndex % stepsPerRow != 0) // a line into the next step
    {
        const auto nextArea = stepAreas[static_cast<size_t> (nextIndex)];
        g.drawLine (area.getCentreX(), slideY, nextArea.getCentreX(), slideY, shape::iconStroke);
        g.fillEllipse (area.getCentreX() - 2.0f, slideY - 2.0f, 4.0f, 4.0f);
    }

    drawText (g, step.tie ? juce::String ("tie") : core::noteName (step.note), type::meta, muted, labelArea,
              juce::Justification::centredTop);
}

// --- interaction --------------------------------------------------------------------------------

int PatternView::stepAt (juce::Point<float> position) const
{
    for (int s = 0; s < shown.length; ++s)
        if (stepAreas[static_cast<size_t> (s)].contains (position))
            return s;
    return -1;
}

int PatternView::patternChipAt (juce::Point<float> position) const
{
    for (int p = 0; p < Session::maxPatterns; ++p)
        if (patternChips[static_cast<size_t> (p)].contains (position))
            return p;
    return -1;
}

void PatternView::selectPattern (int index)
{
    run ("pattern.select", { { "pattern", juce::jlimit (0, Session::maxPatterns - 1, index) + 1 } });
    timerCallback();
}

void PatternView::mouseMove (const juce::MouseEvent& e)
{
    const auto now = stepAt (e.position);
    if (now != hoveredStep)
    {
        hoveredStep = now;
        repaint();
    }
}

void PatternView::mouseExit (const juce::MouseEvent&)
{
    hoveredStep = -1;
    repaint();
}

void PatternView::mouseDown (const juce::MouseEvent& e)
{
    if (const auto chip = patternChipAt (e.position); chip >= 0)
    {
        selectPattern (chip);
        return;
    }

    const auto index = stepAt (e.position);
    if (index < 0)
        return;

    const auto step = session.selectedPattern().getStep (index);
    if (e.mods.isShiftDown())
        run ("pattern.step.accent", { { "step", index }, { "on", ! (step.on && step.accent) } });
    else if (e.mods.isCtrlDown())
        run ("pattern.step.slide", { { "step", index }, { "on", ! (step.on && step.slide) } });
    else if (e.mods.isAltDown())
        run ("pattern.step.tie", { { "step", index }, { "on", ! (step.on && step.tie) } });
    else
        run ("pattern.step.toggle", { { "step", index } });
    timerCallback(); // show the edit now, not on the next tick
}

void PatternView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (titleArea.contains (e.position))
        startRename();
}

void PatternView::startRename()
{
    renameEditor.setText (shown.patternName, false);
    renameEditor.setBounds (titleArea.withTrimmedTop (titleArea.getHeight() - 56.0f).toNearestInt());
    renameEditor.setVisible (true);
    renameEditor.grabKeyboardFocus();
    renameEditor.selectAll();
    repaint();
}

void PatternView::finishRename (bool keep)
{
    if (! renameEditor.isVisible())
        return;
    renameEditor.setVisible (false);
    if (keep && renameEditor.getText().trim() != shown.patternName)
        run ("pattern.rename", { { "name", renameEditor.getText() } });
    grabKeyboardFocus();
    timerCallback();
    repaint();
}

void PatternView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto index = stepAt (e.position);
    if (index < 0 || juce::exactlyEqual (wheel.deltaY, 0.0f))
        return;

    const auto interval = e.mods.isShiftDown() ? 12 : 1;
    const auto note = session.selectedPattern().getStep (index).note + (wheel.deltaY > 0 ? interval : -interval);
    if (note >= core::Step::minNote && note <= core::Step::maxNote)
        run ("pattern.step.note", { { "step", index }, { "note", note } });
    timerCallback();
}

bool PatternView::keyPressed (const juce::KeyPress& key)
{
    // The keymap of this view: a key runs a command or a project action. Contextual, user-editable
    // keymaps replace this table in M3.
    using K = juce::KeyPress;
    using M = juce::ModifierKeys;
    const auto cmd = M::commandModifier, shift = M::shiftModifier;

    struct Binding
    {
        K key;
        std::function<void()> action;
    };
    const std::array<Binding, 11> bindings { {
        { K (K::spaceKey), [this] { run ("transport.toggle"); } },
        { K ('t'), [this] { run ("ui.theme.toggle"); } },
        { K ('z', cmd, 0), [this] { run ("edit.undo"); } },
        { K ('z', cmd | shift, 0), [this] { run ("edit.redo"); } },
        { K ('y', cmd, 0), [this] { run ("edit.redo"); } },
        { K ('s', cmd, 0), [this] { actions.save(); } },
        { K ('s', cmd | shift, 0), [this] { actions.saveAs(); } },
        { K ('o', cmd, 0), [this] { actions.openProject(); } },
        { K ('n', cmd, 0), [this] { actions.newProject(); } },
        { K (K::pageUpKey), [this] { selectPattern (shown.selected - 1); } },
        { K (K::pageDownKey), [this] { selectPattern (shown.selected + 1); } },
    } };

    for (const auto& binding : bindings)
    {
        if (binding.key == key)
        {
            binding.action();
            timerCallback();
            return true;
        }
    }
    return false;
}
} // namespace tekno::ui
