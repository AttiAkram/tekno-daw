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
    using core::InstrumentKind;
    using core::Pattern;
    using core::Session;

    constexpr int stepsPerBeat = 4;
    constexpr float maxStepSize = 56.0f;
    constexpr float minStepSize = 30.0f; // below this the rack scrolls instead of shrinking
    constexpr float pillWidth = 100.0f;
    constexpr float toggleSize = 20.0f; // the mute and solo boxes
    constexpr float rackLabelWidth = pillWidth + (space::unit + toggleSize) * 2.0f;
    constexpr float knobSize = 48.0f;
    constexpr float knobCellHeight = 84.0f;
    constexpr float terminalHeight = 220.0f;
    constexpr float accentMarkHeight = 3.0f;
    constexpr float rowTop = accentMarkHeight + space::unit * 2.0f; // room for the accent mark
    constexpr float acidRowBottom = 36.0f;                          // slide line + note name
    constexpr float samplerRowBottom = space::unit * 2.0f;
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

    juce::String describeSample (const juce::String& origin, bool loaded)
    {
        if (origin.isEmpty())
            return "no sample";
        juce::String name =
            origin.startsWith (core::SampleLibrary::builtinPrefix)
                ? origin.fromFirstOccurrenceOf (core::SampleLibrary::builtinPrefix, false, false) + " (built-in)"
                : juce::File (origin).getFileName();
        return loaded ? name : name + " (missing)";
    }

    float rowBottom (bool sampler)
    {
        return sampler ? samplerRowBottom : acidRowBottom;
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
    , knobs { {
          { "cutoff",
            "synth.cutoff",
            "value",
            0,
            1,
            core::AcidSettings {}.cutoff,
            Knob::Group::acid,
            [] (const core::Channel& c) { return c.acid.cutoff.load(); },
            {} },
          { "resonance",
            "synth.resonance",
            "value",
            0,
            1,
            core::AcidSettings {}.resonance,
            Knob::Group::acid,
            [] (const core::Channel& c) { return c.acid.resonance.load(); },
            {} },
          { "env mod",
            "synth.envmod",
            "value",
            0,
            1,
            core::AcidSettings {}.envMod,
            Knob::Group::acid,
            [] (const core::Channel& c) { return c.acid.envMod.load(); },
            {} },
          { "decay",
            "synth.decay",
            "value",
            0,
            1,
            core::AcidSettings {}.decay,
            Knob::Group::acid,
            [] (const core::Channel& c) { return c.acid.decay.load(); },
            {} },
          { "accent",
            "synth.accent",
            "value",
            0,
            1,
            core::AcidSettings {}.accent,
            Knob::Group::acid,
            [] (const core::Channel& c) { return c.acid.accent.load(); },
            {} },
          { "drive",
            "synth.drive",
            "value",
            0,
            1,
            core::AcidSettings {}.drive,
            Knob::Group::acid,
            [] (const core::Channel& c) { return c.acid.drive.load(); },
            {} },
          { "tune",
            "sampler.tune",
            "semitones",
            core::SamplerSettings::minTune,
            core::SamplerSettings::maxTune,
            core::SamplerSettings {}.tune,
            Knob::Group::sampler,
            [] (const core::Channel& c) { return c.sampler.tune.load(); },
            {} },
          { "decay",
            "sampler.decay",
            "value",
            0,
            1,
            core::SamplerSettings {}.decay,
            Knob::Group::sampler,
            [] (const core::Channel& c) { return c.sampler.decay.load(); },
            {} },
          { "accent",
            "sampler.accent",
            "value",
            0,
            1,
            core::SamplerSettings {}.accent,
            Knob::Group::sampler,
            [] (const core::Channel& c) { return c.sampler.accent.load(); },
            {} },
          { "volume",
            "mixer.volume",
            "db",
            core::MixerSettings::minVolumeDb,
            core::MixerSettings::maxVolumeDb,
            0,
            Knob::Group::mixer,
            [] (const core::Channel& c) { return c.mixer.volumeDb.load(); },
            {} },
          { "pan",
            "mixer.pan",
            "pan",
            -1,
            1,
            0,
            Knob::Group::mixer,
            [] (const core::Channel& c) { return c.mixer.pan.load(); },
            {} },
      } }
    , terminal (teknoLookAndFeel, registry)
    , meters (sessionToShow)
    , mixer (teknoLookAndFeel, sessionToShow, registry, meters)
{
    setWantsKeyboardFocus (true);

    const std::array<const char*, 4> views { "Pattern", "Piano roll", "Mixer", "Song" };
    for (size_t i = 0; i < navButtons.size(); ++i)
    {
        auto& button = navButtons[i];
        button.setButtonText (views[i]);
        setRole (button, Role::nav);
        button.setMouseClickGrabsKeyboardFocus (false);
        button.setEnabled (i == 0 || i == 2); // piano roll arrives later in M3, song in M6
        addAndMakeVisible (button);
    }
    navButtons[0].setToggleState (true, juce::dontSendNotification);
    navButtons[0].onClick = [this] { run ("ui.view", { { "view", "pattern" } }); };
    navButtons[2].onClick = [this] { run ("ui.view", { { "view", "mixer" } }); };

    addChildComponent (mixer);
    meters.onChange = [this]
    {
        if (mixer.isVisible())
            mixer.metersChanged();
    };

    swingChip.onChange = [this] (double percent) { run ("transport.swing", { { "percent", percent } }); };
    bpmChip.onChange = [this] (double bpm) { run ("transport.bpm", { { "bpm", bpm } }); };
    lengthChip.onChange = [this] (double steps) { run ("pattern.length", { { "steps", juce::roundToInt (steps) } }); };
    for (auto* chip : { &swingChip, &bpmChip, &lengthChip })
    {
        chip->onDragStart = [this] { commands.beginGesture(); }; // a whole drag = one undo step
        chip->onDragEnd = [this] { commands.endGesture(); };
        addAndMakeVisible (chip);
    }

    setRole (playButton, Role::primary);
    playButton.onClick = [this] { run ("transport.toggle"); };
    waveButton.onClick = [this]
    {
        run ("synth.waveform", { { "square", ! session.selectedChannelRef().acid.square.load() },
                                 { "channel", session.selectedChannel.load() + 1 } });
    };
    sampleButton.onClick = [this] { actions.chooseSample (session.selectedChannel.load()); };
    newButton.onClick = [this] { actions.newProject(); };
    openButton.onClick = [this] { actions.openProject(); };
    saveButton.onClick = [this] { actions.save(); };
    for (auto* b : { &playButton, &waveButton, &sampleButton, &newButton, &openButton, &saveButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    for (auto& knob : knobs)
    {
        auto& s = knob.slider;
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRange (knob.min, knob.max, juce::String (knob.argument) == "semitones" ? 1.0 : 0.0);
        s.setMouseDragSensitivity (200);
        s.setDoubleClickReturnValue (true, knob.defaultValue);
        s.setValue (knob.read (session.selectedChannelRef()), juce::dontSendNotification);
        s.setMouseClickGrabsKeyboardFocus (false);
        s.onValueChange = [this, &knob]
        {
            run (knob.command,
                 { { knob.argument, knob.slider.getValue() }, { "channel", session.selectedChannel.load() + 1 } });
        };
        s.onDragStart = [this] { commands.beginGesture(); }; // a whole drag = one undo step
        s.onDragEnd = [this] { commands.endGesture(); };
        addAndMakeVisible (s);
    }

    renameEditor.setFont (lnf.fonts().font (type::viewTitle));
    renameEditor.setJustification (juce::Justification::centredLeft);
    renameEditor.onReturnKey = [this] { finishRename (true); };
    renameEditor.onEscapeKey = [this] { finishRename (false); };
    renameEditor.onFocusLost = [this] { finishRename (true); };
    addChildComponent (renameEditor);

    terminal.onCommandRun = [this] { timerCallback(); };
    terminal.onEscape = [this] { grabKeyboardFocus(); };
    terminal.onToggleKey = [this] { toggleTerminal(); };
    addChildComponent (terminal);

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

bool PatternView::knobVisible (const Knob& knob) const
{
    if (knob.group == Knob::Group::mixer)
        return shown.channels > 0;
    return (knob.group == Knob::Group::sampler) == shown.isSampler (shown.selectedChannel);
}

// --- state --------------------------------------------------------------------------------------

PatternView::Snapshot PatternView::takeSnapshot() const
{
    Snapshot s;
    const auto& pattern = session.selectedPattern();
    s.channels = session.channelCount();
    s.selectedChannel = std::clamp (session.selectedChannel.load(), 0, std::max (0, s.channels - 1));
    for (int c = 0; c < s.channels; ++c)
    {
        const auto slot = static_cast<size_t> (c);
        const auto laneSteps = core::steps::fromNotes (pattern.lane (c).notes(), pattern.getLength());
        for (int i = 0; i < Pattern::maxSteps; ++i)
            s.steps[slot][static_cast<size_t> (i)] = laneSteps[static_cast<size_t> (i)].pack();
        s.channelNames[slot] = session.project.channelNames[slot];
        const auto& channel = session.channel (c);
        if (channel.kind.load() == InstrumentKind::sampler)
            s.samplers |= 1u << c;
        if (channel.mixer.mute.load())
            s.muted |= 1u << c;
        if (channel.mixer.solo.load())
            s.soloed |= 1u << c;
    }
    if (s.isSampler (s.selectedChannel))
        s.sampleName = describeSample (session.project.channelSamples[static_cast<size_t> (s.selectedChannel)],
                                       session.channel (s.selectedChannel).sample.load() != nullptr);

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
    s.square = session.selectedChannelRef().acid.square.load();
    s.patternName = session.project.patternNames[static_cast<size_t> (s.selected)];
    s.documentTitle = actions.documentTitle();
    return s;
}

void PatternView::timerCallback()
{
    const auto now = takeSnapshot();

    // Controls follow the session (changed by undo, opening a file, the terminal, later remote
    // clients), unless the user is holding them right now.
    const auto& channel = session.channel (now.selectedChannel);
    for (auto& knob : knobs)
        if (! knob.slider.isMouseButtonDown())
            knob.slider.setValue (knob.read (channel), juce::dontSendNotification);

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
        const auto selectionMoved = now.selectedChannel != shown.selectedChannel;
        const auto relayoutRack = now.channels != laidOutChannels || now.samplers != laidOutSamplers;
        const auto relayoutKnobs = now.isSampler (now.selectedChannel) != shown.isSampler (shown.selectedChannel) ||
                                   now.channels != shown.channels;
        shown = now;
        page = std::clamp (page, 0, (shown.length - 1) / stepsPerPage);
        if (relayoutRack)
            layoutRack();
        if (relayoutKnobs)
            layoutKnobs();
        if (selectionMoved || relayoutRack) // from anywhere: a click, the terminal, undo
            ensureChannelVisible (shown.selectedChannel);
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

    // Left column: project, the selected channel, keys, audio — divided by thin lines.
    auto column = leftColumn.reduced (space::gutter);
    projectArea = column.removeFromTop (112.0f);
    column.removeFromTop (space::gutter);
    synthArea = column.removeFromTop (36.0f + knobCellHeight * 3.0f + shape::pillHeight);
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

    // Content: title, metadata, dashed divider, pattern bank, channel rack.
    auto c = content;
    titleArea = c.removeFromTop (92.0f);
    metaArea = c.removeFromTop (24.0f);
    c.removeFromTop (space::gutter);
    dividerArea = c.removeFromTop (shape::lineWidth);
    c.removeFromTop (space::gutter);
    patternBarArea = c.removeFromTop (shape::pillHeight);
    c.removeFromTop (space::gutter);
    if (terminalOpen)
    {
        terminalArea = c.removeFromBottom (terminalHeight);
        c.removeFromBottom (space::gutter);
        terminal.setBounds (terminalArea.toNearestInt());
    }
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

    if (renameEditor.isVisible() && renamingChannel < 0)
        renameEditor.setBounds (titleArea.withTrimmedTop (titleArea.getHeight() - 56.0f).toNearestInt());

    // The mixer takes everything under the divider; the pattern bank and rack belong to the pattern view.
    mixer.setBounds (patternBarArea.getUnion (gridArea).toNearestInt());
    mixer.setVisible (view == View::mixer);
    lengthChip.setVisible (view == View::pattern);

    layoutKnobs();
    layoutRack();
}

std::optional<PatternView::View> PatternView::viewFromName (const juce::String& name)
{
    if (name == "pattern" || name == "rack" || name == "channels")
        return View::pattern;
    if (name == "mixer")
        return View::mixer;
    return std::nullopt;
}

void PatternView::showView (View newView)
{
    if (newView == view)
        return;
    finishRename (false);
    view = newView;
    navButtons[0].setToggleState (view == View::pattern, juce::dontSendNotification);
    navButtons[2].setToggleState (view == View::mixer, juce::dontSendNotification);
    resized();
    repaint();
}

void PatternView::layoutKnobs()
{
    // A 3-column grid. Acid: two rows of knobs and the waveform switch. Sampler: one row of knobs, then
    // the sample's name and the load button. The mixer row is always the third, so it doesn't jump.
    const auto grid = synthArea.withTrimmedTop (36.0f);
    constexpr int columnsInGrid = 3;
    const auto cellWidth = grid.getWidth() / columnsInGrid;
    auto cell = [&] (int row, int col)
    {
        return juce::Rectangle<float> (grid.getX() + cellWidth * static_cast<float> (col),
                                       grid.getY() + knobCellHeight * static_cast<float> (row), cellWidth,
                                       knobCellHeight);
    };

    int acidIndex = 0, samplerIndex = 0, mixerIndex = 0;
    for (auto& knob : knobs)
    {
        juce::Rectangle<float> area;
        if (knob.group == Knob::Group::acid)
            area = cell (acidIndex / columnsInGrid, acidIndex % columnsInGrid), ++acidIndex;
        else if (knob.group == Knob::Group::sampler)
            area = cell (0, samplerIndex++);
        else
            area = cell (2, mixerIndex++);
        knob.slider.setBounds (
            area.withHeight (knobSize + 8.0f).withSizeKeepingCentre (knobSize, knobSize).toNearestInt());
        knob.slider.setVisible (knobVisible (knob));
    }

    const auto sampler = shown.isSampler (shown.selectedChannel);
    const auto buttonRow = grid.getY() + knobCellHeight * 3.0f;
    waveButton.setBounds (
        juce::Rectangle<float> (grid.getX(), buttonRow, cellWidth * 2.0f - space::gap, shape::pillHeight)
            .toNearestInt());
    waveButton.setVisible (! sampler && shown.channels > 0);

    const auto sampleRow = cell (1, 0).withWidth (grid.getWidth());
    sampleNameArea = sampleRow.withHeight (22.0f).translated (0.0f, space::unit * 2.0f);
    sampleButton.setBounds (juce::Rectangle<float> (grid.getX(), sampleNameArea.getBottom() + space::unit * 2.0f,
                                                    cellWidth * 2.0f - space::gap, shape::pillHeightCompact)
                                .toNearestInt());
    sampleButton.setVisible (sampler);
}

void PatternView::runInTerminal (const juce::StringArray& lines)
{
    if (! terminalOpen)
        toggleTerminal();
    for (const auto& line : lines)
        terminal.runLine (line);
}

void PatternView::toggleTerminal()
{
    terminalOpen = ! terminalOpen;
    terminal.setVisible (terminalOpen);
    resized();
    if (terminalOpen)
        terminal.focusInput();
    else
        grabKeyboardFocus();
    repaint();
}

void PatternView::layoutRack()
{
    // One row per channel, 16 step columns (one bar). Steps shrink to fit the width and the rows.
    laidOutChannels = shown.channels;
    laidOutSamplers = shown.samplers;

    auto g = gridArea;
    beatLabelArea = g.removeFromTop (20.0f);
    pageLabelArea = beatLabelArea.withWidth (rackLabelWidth);
    g.removeFromTop (space::unit);
    rackArea = g;

    const auto stepGap = space::unit;
    const auto gridWidth = g.getWidth() - rackLabelWidth - space::gutter;
    const auto fixedGaps = space::gap * 3.0f + stepGap * 3.0f * 4.0f;
    const auto byWidth = std::floor ((gridWidth - fixedGaps) / static_cast<float> (stepsPerPage));

    const auto rowCount = std::max (1, shown.channels);
    auto extras = 0.0f;
    for (int c = 0; c < shown.channels; ++c)
        extras += rowTop + rowBottom (shown.isSampler (c));
    const auto byHeight =
        std::floor ((g.getHeight() - extras - space::unit * static_cast<float> (rowCount - 1)) / rowCount);
    stepSize = juce::jlimit (minStepSize, maxStepSize, juce::jmin (byWidth, byHeight));

    const auto left = g.getX() + rackLabelWidth + space::gutter;
    auto sx = left;
    for (int col = 0; col < stepsPerPage; ++col)
    {
        columns[static_cast<size_t> (col)] = { sx, 0.0f, stepSize, 0.0f };
        sx += stepSize + ((col + 1) % stepsPerBeat == 0 ? space::gap : stepGap);
    }

    // Rows that don't fit scroll: rackScroll moves them up, painting clips them to rackArea.
    rackContentHeight = 0.0f;
    for (int c = 0; c < shown.channels; ++c)
        rackContentHeight += rowTop + stepSize + rowBottom (shown.isSampler (c)) + space::unit;
    rackScroll = std::clamp (rackScroll, 0.0f, std::max (0.0f, rackContentHeight - g.getHeight()));

    auto y = g.getY() - rackScroll;
    for (int c = 0; c < maxChannels; ++c)
    {
        auto& row = rows[static_cast<size_t> (c)];
        row.stepY = y + rowTop;
        const auto centre = row.stepY + stepSize * 0.5f;
        row.pill = { g.getX(), centre - shape::pillHeightCompact * 0.5f, pillWidth, shape::pillHeightCompact };
        row.mute = { row.pill.getRight() + space::unit, centre - toggleSize * 0.5f, toggleSize, toggleSize };
        row.solo = row.mute.translated (toggleSize + space::unit, 0.0f);
        y = row.stepY + stepSize + rowBottom (c < shown.channels && shown.isSampler (c)) + space::unit;
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

    if (shown.channels > 0)
    {
        const auto sampler = shown.isSampler (shown.selectedChannel);
        auto title = synthArea.withHeight (28.0f);
        drawText (g, shown.channelNames[static_cast<size_t> (shown.selectedChannel)], type::sectionTitle, ink, title);
        drawText (g, sampler ? "sampler" : "303 acid", type::meta, muted, title, juce::Justification::centredRight);
        if (sampler)
            drawText (g, shown.sampleName, type::meta, ink, sampleNameArea);
    }
    for (const auto& knob : knobs)
    {
        if (! knob.slider.isVisible())
            continue;
        const auto b = knob.slider.getBounds().toFloat();
        drawText (g, knob.label, type::meta, muted,
                  b.withY (b.getBottom() + 4.0f).withHeight (20.0f).expanded (30.0f, 0.0f),
                  juce::Justification::centred);
    }

    auto keys = keysArea;
    drawText (g, "Keys", type::sectionTitle, ink, keys.removeFromTop (28.0f));
    const std::array<std::pair<const char*, const char*>, 12> hints { {
        { "Space", "play / stop" },
        { "Click", "step on / off" },
        { "Shift Ctrl Alt", "accent, slide, tie" },
        { "Wheel", "pitch" },
        { "Up / Down", "channel" },
        { "F6 / F9", "pattern / mixer" },
        { "PgUp / PgDn", "pattern" },
        { "[  ]", "bar of the pattern" },
        { "Ctrl+Z / Y", "undo / redo" },
        { "Ctrl+S / O", "save / open" },
        { "T", "theme" },
        { "`", "terminal" },
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

    const float dashes[] { shape::dashLength, shape::dashGap };
    auto divider = [&]
    {
        g.setColour (line);
        g.drawDashedLine ({ dividerArea.getX(), dividerArea.getY(), dividerArea.getRight(), dividerArea.getY() },
                          dashes, 2, shape::lineWidth);
    };

    if (view == View::mixer)
    {
        drawText (g, "Mixer", type::pageTitle, ink, titleArea, juce::Justification::bottomLeft);
        const auto masterDb = session.master.volumeDb.load();
        drawText (g,
                  juce::String (shown.channels) + (shown.channels == 1 ? " channel" : " channels") + "  /  master " +
                      (masterDb <= core::MixerSettings::minVolumeDb ? juce::String ("off")
                                                                    : juce::String (masterDb, 1) + " dB") +
                      "  /  " + (shown.playing ? "playing" : "stopped"),
                  type::meta, muted, metaArea);
        divider();
        return; // the MixerView component draws the rest
    }

    if (! (renameEditor.isVisible() && renamingChannel < 0))
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
    divider();

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

    // Above the rack: which bar is shown (when there is more than one), beat numbers, the playhead.
    const auto pages = (shown.length + stepsPerPage - 1) / stepsPerPage;
    if (pages > 1)
        drawText (g, "bar " + juce::String (page + 1) + " of " + juce::String (pages), type::meta, ink, pageLabelArea);
    for (int beat = 0; beat < stepsPerPage / stepsPerBeat; ++beat)
    {
        if (page * stepsPerPage + beat * stepsPerBeat >= shown.length)
            break;
        const auto first = columns[static_cast<size_t> (beat * stepsPerBeat)];
        drawText (g, juce::String (page * stepsPerPage / stepsPerBeat + beat + 1), type::meta, muted,
                  beatLabelArea.withX (first.getX()).withWidth (first.getWidth() * 2.0f));
    }
    if (shown.currentStep >= 0 && shown.currentStep / stepsPerPage == page)
    {
        const auto column = columns[static_cast<size_t> (shown.currentStep % stepsPerPage)];
        const auto d = shape::navDotSize;
        g.setColour (ink);
        g.fillEllipse (column.getCentreX() - d * 0.5f, beatLabelArea.getBottom() - d, d, d);
    }

    {
        const juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (rackArea.toNearestInt());
        for (int c = 0; c < shown.channels; ++c)
            paintRow (g, c);
    }

    // When the rows don't fit: a thin bar on the right says how much is shown, and where.
    if (rackContentHeight > rackArea.getHeight())
    {
        const auto track = rackArea.withLeft (rackArea.getRight() - space::unit);
        const auto shownShare = rackArea.getHeight() / rackContentHeight;
        const auto thumb = track.withHeight (track.getHeight() * shownShare)
                               .withY (track.getY() + track.getHeight() * rackScroll / rackContentHeight);
        g.setColour (line);
        g.fillRoundedRectangle (track, track.getWidth() * 0.5f);
        g.setColour (ink);
        g.fillRoundedRectangle (thumb, track.getWidth() * 0.5f);
    }
}

void PatternView::paintRow (juce::Graphics& g, int channel) const
{
    const auto& row = rows[static_cast<size_t> (channel)];
    const auto ink = lnf.colour (&P::ink);
    const auto bg = lnf.colour (&P::bg);
    const auto line = lnf.colour (&P::line);
    const auto isSelected = channel == shown.selectedChannel;
    const auto isMuted = ((shown.muted >> channel) & 1u) != 0;
    const auto isSoloed = ((shown.soloed >> channel) & 1u) != 0;

    // The pill: filled = the channel being edited.
    const auto pill = row.pill.reduced (shape::lineWidth * 0.5f);
    if (isSelected)
    {
        g.setColour (ink);
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    }
    g.setColour (isSelected || (hovered.what == RackHit::What::pill && hovered.channel == channel) ? ink : line);
    g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, shape::lineWidth);
    drawText (g, shown.channelNames[static_cast<size_t> (channel)], type::meta,
              isSelected ? bg : (isMuted ? lnf.colour (&P::inkMuted) : ink), pill.reduced (space::unit * 2.0f, 0.0f),
              juce::Justification::centred);

    auto toggle = [&] (juce::Rectangle<float> area, bool on, const char* letter)
    {
        const auto box = area.reduced (shape::lineWidth * 0.5f);
        if (on)
        {
            g.setColour (ink);
            g.fillRoundedRectangle (box, shape::stepRadius);
        }
        g.setColour (on ? ink : line);
        g.drawRoundedRectangle (box, shape::stepRadius, shape::lineWidth);
        drawText (g, letter, type::meta, on ? bg : lnf.colour (&P::inkMuted), box, juce::Justification::centred);
    };
    toggle (row.mute, isMuted, "M");
    toggle (row.solo, isSoloed, "S");

    for (int col = 0; col < stepsPerPage; ++col)
        if (page * stepsPerPage + col < shown.length)
            paintStep (g, channel, col);
}

void PatternView::paintStep (juce::Graphics& g, int channel, int column) const
{
    const auto index = page * stepsPerPage + column;
    const auto& lane = shown.steps[static_cast<size_t> (channel)];
    const auto step = core::Step::unpack (lane[static_cast<size_t> (index)]);
    const auto& row = rows[static_cast<size_t> (channel)];
    const auto area =
        juce::Rectangle<float> (columns[static_cast<size_t> (column)].getX(), row.stepY, stepSize, stepSize);
    const auto box = area.reduced (shape::lineWidth * 0.5f);
    const auto ink = lnf.colour (&P::ink);
    const auto muted = lnf.colour (&P::inkMuted);
    const auto sampler = shown.isSampler (channel);
    const auto isHovered = hovered.what == RackHit::What::step && hovered.channel == channel && hovered.step == index;

    // Yellow only for the step that is sounding right now; on = filled ink, off = outline.
    const auto sounding = step.on && index == shown.currentStep;
    const auto fill = sounding ? lnf.colour (&P::accent) : ink;

    if (step.on)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (box, shape::stepRadius);

        // A tie fuses the step with the previous one: the gap between them is filled.
        const auto previous = index > 0 ? core::Step::unpack (lane[static_cast<size_t> (index - 1)]) : core::Step {};
        if (! sampler && step.tie && previous.on && column > 0)
        {
            const auto prevRight = columns[static_cast<size_t> (column - 1)].getRight();
            g.setColour (ink);
            g.fillRect (juce::Rectangle<float> (prevRight - shape::stepRadius, box.getY(),
                                                area.getX() - prevRight + shape::stepRadius * 2.0f, box.getHeight()));
            g.setColour (fill);
            g.fillRoundedRectangle (box, shape::stepRadius);
        }
    }
    g.setColour (sounding ? lnf.colour (&P::accentOutline) : (step.on || isHovered) ? ink : lnf.colour (&P::line));
    g.drawRoundedRectangle (box, shape::stepRadius, shape::lineWidth);

    if (! step.on)
        return;

    g.setColour (ink);
    if (step.accent) // a bar above the step
        g.fillRect (area.getX(), area.getY() - accentMarkHeight - space::unit, area.getWidth(), accentMarkHeight);

    if (sampler)
    {
        // Drums: the pitch only when it is not the sample's own.
        if (step.note != core::Step::samplerRootNote)
            drawText (g, core::noteName (step.note), type::meta,
                      sounding ? lnf.colour (&P::onAccent) : lnf.colour (&P::bg), box, juce::Justification::centred);
        return;
    }

    const auto slideY = area.getBottom() + space::unit * 2.0f;
    const auto nextIndex = index + 1 < shown.length ? index + 1 : -1;
    const auto next = nextIndex >= 0 ? core::Step::unpack (lane[static_cast<size_t> (nextIndex)]) : core::Step {};
    if (step.slide && next.on && column + 1 < stepsPerPage) // a line into the next step
    {
        const auto nextColumn = columns[static_cast<size_t> (column + 1)];
        g.drawLine (area.getCentreX(), slideY, nextColumn.getCentreX(), slideY, shape::iconStroke);
        g.fillEllipse (area.getCentreX() - 2.0f, slideY - 2.0f, 4.0f, 4.0f);
    }

    const auto labelArea = area.withY (slideY + space::unit).withHeight (20.0f);
    drawText (g, step.tie ? juce::String ("tie") : core::noteName (step.note), type::meta, muted, labelArea,
              juce::Justification::centredTop);
}

// --- interaction --------------------------------------------------------------------------------

PatternView::RackHit PatternView::rackAt (juce::Point<float> position) const
{
    using W = RackHit::What;
    if (view != View::pattern || ! rackArea.contains (position))
        return {};
    for (int c = 0; c < shown.channels; ++c)
    {
        const auto& row = rows[static_cast<size_t> (c)];
        if (row.pill.contains (position))
            return { W::pill, c, -1 };
        if (row.mute.contains (position))
            return { W::mute, c, -1 };
        if (row.solo.contains (position))
            return { W::solo, c, -1 };
        for (int col = 0; col < stepsPerPage; ++col)
        {
            const auto index = page * stepsPerPage + col;
            const juce::Rectangle<float> area (columns[static_cast<size_t> (col)].getX(), row.stepY, stepSize,
                                               stepSize);
            if (index < shown.length && area.contains (position))
                return { W::step, c, index };
        }
    }
    return {};
}

int PatternView::patternChipAt (juce::Point<float> position) const
{
    if (view != View::pattern)
        return -1;
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

void PatternView::selectChannel (int index)
{
    if (shown.channels == 0)
        return;
    run ("channel.select", { { "channel", juce::jlimit (0, shown.channels - 1, index) + 1 } });
    timerCallback(); // also scrolls the rack to it
}

void PatternView::scrollRack (float delta)
{
    const auto before = rackScroll;
    rackScroll += delta;
    layoutRack(); // clamps
    if (! juce::exactlyEqual (before, rackScroll))
    {
        hovered = {};
        repaint();
    }
}

void PatternView::ensureChannelVisible (int channel)
{
    if (channel < 0 || channel >= shown.channels)
        return;
    const auto& row = rows[static_cast<size_t> (channel)];
    const auto top = row.stepY - rowTop;
    const auto bottom = row.stepY + stepSize + rowBottom (shown.isSampler (channel));
    if (top < rackArea.getY())
        scrollRack (top - rackArea.getY());
    else if (bottom > rackArea.getBottom())
        scrollRack (bottom - rackArea.getBottom());
}

void PatternView::showPage (int newPage)
{
    const auto pages = (shown.length + stepsPerPage - 1) / stepsPerPage;
    newPage = juce::jlimit (0, std::max (0, pages - 1), newPage);
    if (newPage != page)
    {
        page = newPage;
        repaint();
    }
}

void PatternView::mouseMove (const juce::MouseEvent& e)
{
    const auto now = rackAt (e.position);
    if (now.what != hovered.what || now.channel != hovered.channel || now.step != hovered.step)
    {
        hovered = now;
        repaint();
    }
}

void PatternView::mouseExit (const juce::MouseEvent&)
{
    hovered = {};
    repaint();
}

void PatternView::mouseDown (const juce::MouseEvent& e)
{
    if (const auto chip = patternChipAt (e.position); chip >= 0)
    {
        selectPattern (chip);
        return;
    }
    if (view == View::pattern && pageLabelArea.contains (e.position))
    {
        const auto pages = (shown.length + stepsPerPage - 1) / stepsPerPage;
        showPage ((page + 1) % std::max (1, pages));
        return;
    }

    const auto hit = rackAt (e.position);
    const auto channelArg = hit.channel + 1;
    using W = RackHit::What;
    switch (hit.what)
    {
    case W::nothing:
        return;
    case W::pill:
        selectChannel (hit.channel);
        return;
    case W::mute:
        run ("mixer.mute", { { "on", ((shown.muted >> hit.channel) & 1u) == 0 }, { "channel", channelArg } });
        break;
    case W::solo:
        run ("mixer.solo", { { "on", ((shown.soloed >> hit.channel) & 1u) == 0 }, { "channel", channelArg } });
        break;
    case W::step:
    {
        const auto step =
            core::Step::unpack (shown.steps[static_cast<size_t> (hit.channel)][static_cast<size_t> (hit.step)]);
        core::CommandArgs args { { "step", hit.step + 1 }, { "channel", channelArg } };
        if (e.mods.isShiftDown())
            args.set ("on", ! (step.on && step.accent)), run ("pattern.step.accent", args);
        else if (e.mods.isCtrlDown())
            args.set ("on", ! (step.on && step.slide)), run ("pattern.step.slide", args);
        else if (e.mods.isAltDown())
            args.set ("on", ! (step.on && step.tie)), run ("pattern.step.tie", args);
        else
            run ("pattern.step.toggle", args);
        if (hit.channel != shown.selectedChannel)
            run ("channel.select", { { "channel", channelArg } }); // editing a row selects its channel
        break;
    }
    }
    timerCallback(); // show the edit now, not on the next tick
}

void PatternView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (view != View::pattern)
        return;
    if (const auto hit = rackAt (e.position); hit.what == RackHit::What::pill)
        startRename (hit.channel);
    else if (titleArea.contains (e.position))
        startRename (-1);
}

void PatternView::startRename (int channel)
{
    renamingChannel = channel;
    if (channel < 0)
    {
        renameEditor.setFont (lnf.fonts().font (type::viewTitle));
        renameEditor.setText (shown.patternName, false);
        renameEditor.setBounds (titleArea.withTrimmedTop (titleArea.getHeight() - 56.0f).toNearestInt());
    }
    else
    {
        renameEditor.setFont (lnf.fonts().font (type::meta));
        renameEditor.setText (shown.channelNames[static_cast<size_t> (channel)], false);
        renameEditor.setBounds (rows[static_cast<size_t> (channel)].pill.toNearestInt());
    }
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
    const auto text = renameEditor.getText().trim();
    if (renamingChannel < 0)
    {
        if (keep && text != shown.patternName)
            run ("pattern.rename", { { "name", text } });
    }
    else if (keep && text != shown.channelNames[static_cast<size_t> (renamingChannel)])
        run ("channel.rename", { { "name", text }, { "channel", renamingChannel + 1 } });
    renamingChannel = -1;
    grabKeyboardFocus();
    timerCallback();
    repaint();
}

void PatternView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (juce::exactlyEqual (wheel.deltaY, 0.0f))
        return;
    const auto hit = rackAt (e.position);
    if (hit.what != RackHit::What::step)
    {
        // Off the steps, the wheel scrolls the rack (on a step it changes the pitch).
        if (view == View::pattern && rackArea.contains (e.position))
            scrollRack (-wheel.deltaY * 120.0f);
        return;
    }

    const auto interval = e.mods.isShiftDown() ? 12 : 1;
    const auto current =
        core::Step::unpack (shown.steps[static_cast<size_t> (hit.channel)][static_cast<size_t> (hit.step)]);
    const auto note = current.note + (wheel.deltaY > 0 ? interval : -interval);
    if (note >= core::Step::minNote && note <= core::Step::maxNote)
        run ("pattern.step.note", { { "step", hit.step + 1 }, { "note", note }, { "channel", hit.channel + 1 } });
    timerCallback();
}

bool PatternView::keyPressed (const juce::KeyPress& key)
{
    // The keymap of this view: a key runs a command or a project action. Contextual, user-editable
    // keymaps replace this table later in M3.
    using K = juce::KeyPress;
    using M = juce::ModifierKeys;
    const auto cmd = M::commandModifier, shift = M::shiftModifier;

    struct Binding
    {
        K key;
        std::function<void()> action;
    };
    const std::array<Binding, 18> bindings { {
        { K ('`'), [this] { toggleTerminal(); } },
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
        { K (K::upKey), [this] { selectChannel (shown.selectedChannel - 1); } },
        { K (K::downKey), [this] { selectChannel (shown.selectedChannel + 1); } },
        { K (K::F6Key), [this] { run ("ui.view", { { "view", "pattern" } }); } },
        { K (K::F9Key), [this] { run ("ui.view", { { "view", "mixer" } }); } },
        { K ('['), [this] { showPage (page - 1); } },
        { K (']'), [this] { showPage (page + 1); } },
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
