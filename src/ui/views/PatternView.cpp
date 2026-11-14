// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/views/PatternView.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace tekno::ui
{
namespace
{
    juce::String formatBpm (double bpm)
    {
        return juce::String (bpm, juce::exactlyEqual (bpm, std::floor (bpm)) ? 0 : 1) + " bpm";
    }

    using P = tokens::Palette;
    namespace type = tokens::type;
    namespace shape = tokens::shape;
    namespace space = tokens::space;
    using core::Pattern;

    constexpr int stepsPerBeat = 4;
    constexpr float maxStepSize = 64.0f;
    constexpr float chipWidth = 112.0f;
    constexpr float knobSize = 52.0f;
    constexpr float accentMarkHeight = 3.0f;
    constexpr int refreshHz = 60;

    juce::String noteName (int midiNote)
    {
        return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 4); // 45 -> "A2"
    }

    // Keymap of this view. Every key runs a command; a contextual keymap system replaces this later.
    struct KeyBinding
    {
        juce::KeyPress key;
        const char* command;
    };

    const std::array<KeyBinding, 5>& keyBindings()
    {
        using K = juce::KeyPress;
        using M = juce::ModifierKeys;
        static const std::array<KeyBinding, 5> bindings { {
            { K (K::spaceKey), "transport.toggle" },
            { K ('t'), "ui.theme.toggle" },
            { K ('z', M::commandModifier, 0), "edit.undo" },
            { K ('z', M::commandModifier | M::shiftModifier, 0), "edit.redo" },
            { K ('y', M::commandModifier, 0), "edit.redo" },
        } };
        return bindings;
    }
} // namespace

PatternView::PatternView (TeknoLookAndFeel& teknoLookAndFeel, core::Session& sessionToShow,
                          core::CommandRegistry& registry, std::function<AudioStatus()> audioStatusProvider)
    : lnf (teknoLookAndFeel)
    , session (sessionToShow)
    , commands (registry)
    , audioStatus (std::move (audioStatusProvider))
    , bpmChip (core::TransportState::minBpm, core::TransportState::maxBpm, core::TransportState::defaultBpm, formatBpm)
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

    bpmChip.onChange = [this] (double bpm) { run ("transport.bpm", { { "bpm", bpm } }); };
    addAndMakeVisible (bpmChip);

    setRole (playButton, Role::primary);
    playButton.setMouseClickGrabsKeyboardFocus (false);
    playButton.onClick = [this] { run ("transport.toggle"); };
    addAndMakeVisible (playButton);

    waveButton.setMouseClickGrabsKeyboardFocus (false);
    waveButton.onClick = [this] { run ("synth.waveform", { { "square", ! session.synth.square.load() } }); };
    addAndMakeVisible (waveButton);

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
    for (int i = 0; i < Pattern::numSteps; ++i)
        s.steps[static_cast<size_t> (i)] = session.pattern.getStep (i).pack();
    s.currentStep = session.transport.currentStep.load();
    s.playing = session.transport.playing.load();
    s.bpm = session.transport.bpm.load();
    s.square = session.synth.square.load();
    return s;
}

void PatternView::timerCallback()
{
    // Controls follow the session (changed by undo, and later by the terminal or remote clients),
    // unless the user is holding them right now.
    for (auto& knob : knobs)
        if (! knob.slider.isMouseButtonDown())
            knob.slider.setValue (knob.value->load(), juce::dontSendNotification);

    const auto now = takeSnapshot();
    if (! bpmChip.isBeingDragged())
        bpmChip.setValue (now.bpm);
    playButton.setButtonText (now.playing ? "stop" : "play");
    waveButton.setButtonText (now.square ? "square" : "saw");

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
        shown = now;
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
    playButton.setBounds (
        transport.removeFromRight (112.0f).withSizeKeepingCentre (112.0f, shape::pillHeight).toNearestInt());
    transport.removeFromRight (space::gap);
    bpmChip.setBounds (
        transport.removeFromRight (112.0f).withSizeKeepingCentre (112.0f, shape::pillHeight).toNearestInt());

    // Left column: browser, 303, keys, audio — divided by thin lines.
    auto column = leftColumn.reduced (space::gutter);
    browserArea = column.removeFromTop (112.0f);
    column.removeFromTop (space::gutter);
    synthArea = column.removeFromTop (316.0f);
    column.removeFromTop (space::gutter);
    audioArea = column.removeFromBottom (76.0f);
    column.removeFromBottom (space::gutter);
    keysArea = column;

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

    // Content: title, metadata, dashed divider, the step row.
    auto c = content;
    titleArea = c.removeFromTop (92.0f);
    metaArea = c.removeFromTop (24.0f);
    c.removeFromTop (space::gutter);
    dividerArea = c.removeFromTop (shape::lineWidth);
    c.removeFromTop (space::gutter * 1.5f);
    beatLabelArea = c.removeFromTop (24.0f);
    c.removeFromTop (space::unit * 3.0f);

    // 16 steps = 4 beats: the design's 4-column grid IS the bar (gap 12 between beats).
    const auto stepGap = space::unit;
    const auto gridWidth = c.getWidth() - chipWidth - space::gutter;
    const auto fixedGaps = space::gap * 3.0f + stepGap * 3.0f * 4.0f;
    const auto stepSize =
        juce::jmin (maxStepSize, std::floor ((gridWidth - fixedGaps) / static_cast<float> (Pattern::numSteps)));
    auto row = c.removeFromTop (stepSize);

    chipArea = row.removeFromLeft (chipWidth).withSizeKeepingCentre (chipWidth, shape::pillHeight);
    row.removeFromLeft (space::gutter);
    auto x = row.getX();
    for (int s = 0; s < Pattern::numSteps; ++s)
    {
        stepAreas[static_cast<size_t> (s)] = { x, row.getY(), stepSize, stepSize };
        x += stepSize + ((s + 1) % stepsPerBeat == 0 ? space::gap : stepGap);
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

    auto browser = browserArea;
    drawText (g, "Browser", type::sectionTitle, ink, browser.removeFromTop (28.0f));
    drawText (g, "Presets, samples and plugins", type::meta, muted, browser.removeFromTop (22.0f));
    drawText (g, "arrive with M3 and M4", type::meta, muted, browser.removeFromTop (22.0f));

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
    const std::array<std::pair<const char*, const char*>, 9> hints { {
        { "Space", "play / stop" },
        { "Click", "step on / off" },
        { "Shift+click", "accent" },
        { "Ctrl+click", "slide" },
        { "Wheel", "pitch" },
        { "Shift+wheel", "octave" },
        { "Ctrl+Z", "undo" },
        { "Ctrl+Y", "redo" },
        { "T", "light / dark theme" },
    } };
    for (const auto& [key, what] : hints)
    {
        if (keys.getHeight() < 22.0f)
            break;
        auto line = keys.removeFromTop (22.0f);
        drawText (g, key, type::meta, ink, line.removeFromLeft (92.0f));
        drawText (g, what, type::meta, muted, line);
    }

    auto audio = audioArea;
    drawText (g, "Audio", type::sectionTitle, ink, audio.removeFromTop (28.0f));
    auto line = audio.removeFromTop (22.0f);
    paintBadge (g, line.removeFromLeft (shape::badgeSize), shownAudio.running, shownAudio.running ? "A" : "!");
    line.removeFromLeft (space::unit * 2.0f);
    if (shownAudio.running)
    {
        drawText (g, shownAudio.deviceName, type::meta, ink, line);
        drawText (g,
                  juce::String (shownAudio.sampleRate / 1000.0, 1) + " kHz  /  " +
                      juce::String (shownAudio.bufferSize) + " samples",
                  type::meta, muted,
                  audio.removeFromTop (22.0f).withTrimmedLeft (shape::badgeSize + space::unit * 2.0f));
    }
    else
    {
        drawText (g, "no audio output", type::meta, ink, line);
        drawText (g, shownAudio.problem, type::meta, muted,
                  audio.removeFromTop (22.0f).withTrimmedLeft (shape::badgeSize + space::unit * 2.0f));
    }
}

void PatternView::paintContent (juce::Graphics& g) const
{
    const auto ink = lnf.colour (&P::ink);
    const auto muted = lnf.colour (&P::inkMuted);

    drawText (g, "acid 01", type::pageTitle, ink, titleArea, juce::Justification::bottomLeft);
    drawText (g,
              "Pattern 1  /  16 steps  /  " + formatBpm (shown.bpm) + "  /  " + (shown.playing ? "playing" : "stopped"),
              type::meta, muted, metaArea);

    const float dashes[] { shape::dashLength, shape::dashGap };
    g.setColour (lnf.colour (&P::line));
    g.drawDashedLine ({ dividerArea.getX(), dividerArea.getY(), dividerArea.getRight(), dividerArea.getY() }, dashes, 2,
                      shape::lineWidth);

    for (int beat = 0; beat < Pattern::numSteps / stepsPerBeat; ++beat)
    {
        const auto first = stepAreas[static_cast<size_t> (beat * stepsPerBeat)];
        drawText (g, juce::String (beat + 1), type::meta, muted,
                  beatLabelArea.withX (first.getX()).withWidth (first.getWidth()));
    }
    if (shown.currentStep >= 0) // the playhead is a dot, readable without colour
    {
        const auto column = stepAreas[static_cast<size_t> (shown.currentStep)];
        const auto d = shape::navDotSize;
        g.setColour (ink);
        g.fillEllipse (column.getCentreX() - d * 0.5f, beatLabelArea.getBottom() - d, d, d);
    }

    const auto chip = chipArea.reduced (shape::lineWidth * 0.5f);
    g.setColour (ink);
    g.fillRoundedRectangle (chip, chip.getHeight() * 0.5f);
    drawText (g, "303", type::control, lnf.colour (&P::bg), chip, juce::Justification::centred);

    for (int s = 0; s < Pattern::numSteps; ++s)
        paintStep (g, s);
}

void PatternView::paintStep (juce::Graphics& g, int index) const
{
    const auto step = core::Step::unpack (shown.steps[static_cast<size_t> (index)]);
    const auto area = stepAreas[static_cast<size_t> (index)];
    const auto box = area.reduced (shape::lineWidth * 0.5f);
    const auto ink = lnf.colour (&P::ink);

    // Yellow only for the step that is sounding right now; on = filled ink, off = outline.
    const auto sounding = step.on && index == shown.currentStep;

    if (step.on)
    {
        g.setColour (sounding ? lnf.colour (&P::accent) : ink);
        g.fillRoundedRectangle (box, shape::stepRadius);
    }
    g.setColour (sounding                            ? lnf.colour (&P::accentOutline)
                 : (step.on || index == hoveredStep) ? ink
                                                     : lnf.colour (&P::line));
    g.drawRoundedRectangle (box, shape::stepRadius, shape::lineWidth);

    if (! step.on)
        return;

    g.setColour (ink);
    if (step.accent) // a bar above the step
        g.fillRect (area.getX(), area.getY() - accentMarkHeight - space::unit, area.getWidth(), accentMarkHeight);

    const auto next = core::Step::unpack (shown.steps[static_cast<size_t> ((index + 1) % Pattern::numSteps)]);
    const auto slideY = area.getBottom() + space::unit * 2.0f;
    if (step.slide && next.on && index + 1 < Pattern::numSteps) // a line into the next step
    {
        const auto nextArea = stepAreas[static_cast<size_t> (index + 1)];
        g.drawLine (area.getCentreX(), slideY, nextArea.getCentreX(), slideY, shape::iconStroke);
        g.fillEllipse (area.getCentreX() - 2.0f, slideY - 2.0f, 4.0f, 4.0f);
    }

    drawText (g, noteName (step.note), type::meta, lnf.colour (&P::inkMuted),
              area.withY (slideY + space::unit).withHeight (20.0f), juce::Justification::centredTop);
}

// --- interaction --------------------------------------------------------------------------------

int PatternView::stepAt (juce::Point<float> position) const
{
    for (int s = 0; s < Pattern::numSteps; ++s)
        if (stepAreas[static_cast<size_t> (s)].contains (position))
            return s;
    return -1;
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
    const auto index = stepAt (e.position);
    if (index < 0)
        return;

    const auto step = session.pattern.getStep (index);
    if (e.mods.isShiftDown())
        run ("pattern.step.accent", { { "step", index }, { "on", ! (step.on && step.accent) } });
    else if (e.mods.isCtrlDown())
        run ("pattern.step.slide", { { "step", index }, { "on", ! (step.on && step.slide) } });
    else
        run ("pattern.step.toggle", { { "step", index } });
    timerCallback(); // show the edit now, not on the next tick
}

void PatternView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto index = stepAt (e.position);
    if (index < 0 || juce::exactlyEqual (wheel.deltaY, 0.0f))
        return;

    const auto interval = e.mods.isShiftDown() ? 12 : 1;
    const auto note = session.pattern.getStep (index).note + (wheel.deltaY > 0 ? interval : -interval);
    if (note >= core::Step::minNote && note <= core::Step::maxNote)
        run ("pattern.step.note", { { "step", index }, { "note", note } });
    timerCallback();
}

bool PatternView::keyPressed (const juce::KeyPress& key)
{
    for (const auto& binding : keyBindings())
    {
        if (binding.key == key)
        {
            run (binding.command);
            timerCallback();
            return true;
        }
    }
    return false;
}
} // namespace tekno::ui
