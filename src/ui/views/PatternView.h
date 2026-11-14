// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>
#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/commands/CommandRegistry.h"
#include "core/model/Session.h"
#include "ui/ProjectActions.h"
#include "ui/TileLayout.h"
#include "ui/components/LevelMeters.h"
#include "ui/components/Terminal.h"
#include "ui/components/ValueChip.h"
#include "ui/theme/TeknoLookAndFeel.h"
#include "ui/views/KeysView.h"
#include "ui/views/MixerView.h"
#include "ui/views/PianoRollView.h"

namespace tekno::ui
{
/** What the view shows about the audio device. Filled by the app, which owns the device. */
struct AudioStatus
{
    bool running = false;
    juce::String deviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    juce::String problem; // why there is no audio, when not running
};

/** The main screen: transport, project, the selected channel's controls, the pattern bank and the
    channel rack (one row of steps per channel, 16 steps per page).

    The view never changes the model directly: every edit is a command, and file dialogs go
    through ProjectActions. It reads the shared Session (atomics) on a timer, so what it shows is what
    the audio thread is playing. Edits name their target channel explicitly. */
class PatternView final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    PatternView (TeknoLookAndFeel& lookAndFeel, core::Session& session, core::CommandRegistry& commands,
                 ProjectActions& projectActions, std::function<AudioStatus()> audioStatus, core::Keymap& keymap);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Dropping a .mid file imports its melody (midi.import): on a rack row into that channel,
    // anywhere else into the selected one.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Opens the terminal and runs each line as if typed (used by --run). */
    void runInTerminal (const juce::StringArray& lines);

    /** The work panels in the middle of the window, tiled (ui.view, ui.tile...). */
    using View = TileLayout::Panel;
    void showView (View view);
    void setSnap (PianoRollView::Snap snap);
    void toggleTerminal();
    void showPage (int page);
    int currentPage() const noexcept { return page; }
    void keymapChanged();                 // the hints and the editor follow
    std::function<void()> onKeymapEdited; // the keys editor changed the keymap: the app saves it

    /** Where a key press looks, most specific first: the selected channel's instrument, the view, global. */
    juce::StringArray activeKeyContexts() const;
    PianoRollView& getPianoRoll() noexcept { return pianoRoll; }
    View currentView() const noexcept { return tiles.active(); }

    // Tiling: open a panel beside the active one, close the active one, move between them.
    void openTile (View panel);
    bool closeTile();
    void focusTile (int direction);
    void singleTile();
    const TileLayout& getTiles() const noexcept { return tiles; }
    void setTiles (const TileLayout& layout);
    std::function<void()> onLayoutChanged; // the app saves the layout
    static std::optional<View> viewFromName (const juce::String& name);

    static constexpr int stepsPerPage = 16;
    static constexpr int maxPages = core::Pattern::maxSteps / stepsPerPage;
    static constexpr int maxChannels = core::Session::maxChannels;

private:
    /** A knob bound to a parameter of the selected channel. */
    struct Knob
    {
        enum class Group
        {
            acid,
            sampler,
            mixer, // every channel has these
        };

        const char* label;
        const char* command;
        const char* argument;
        double min, max, defaultValue;
        Group group;
        std::function<float (const core::Channel&)> read;
        juce::Slider slider;
    };

    /** Everything on screen that comes from the Session, to repaint only when something changed. */
    struct Snapshot
    {
        std::array<std::array<std::uint32_t, core::Pattern::maxSteps>, maxChannels> steps {};
        std::array<juce::String, maxChannels> channelNames;
        int channels = 0;
        int selectedChannel = 0;
        std::uint32_t samplers = 0, muted = 0, soloed = 0; // a bit per channel
        juce::String sampleName;                           // of the selected channel, when it is a sampler
        int length = 0;
        int selected = 0;
        int playingPattern = 0;
        std::uint32_t usedPatterns = 0; // bit per pattern that holds something
        int currentStep = -1;
        bool playing = false;
        double bpm = 0.0;
        float swing = 0.0f;
        bool square = false;
        juce::String patternName, documentTitle;
        std::uint32_t freeLanes = 0; // a bit per channel whose notes the steps can't show (off the grid)
        std::array<std::vector<core::Note>, maxChannels> freeNotes; // those lanes' notes, for the miniature

        bool operator== (const Snapshot&) const = default;
        bool isSampler (int channel) const noexcept { return ((samplers >> channel) & 1u) != 0; }
    };

    /** Where something on the rack is: a step, a channel pill, or its mute/solo box. */
    struct RackHit
    {
        enum class What
        {
            nothing,
            step,
            pill,
            mute,
            solo,
        };
        What what = What::nothing;
        int channel = -1;
        int step = -1; // absolute step index, for What::step
    };

    void timerCallback() override;
    Snapshot takeSnapshot() const;
    void run (const juce::String& command, const core::CommandArgs& args = {});
    RackHit rackAt (juce::Point<float> position) const;
    int patternChipAt (juce::Point<float> position) const;
    void selectPattern (int index);
    void selectChannel (int index);
    void startRename (int channel); // -1 = the pattern
    void finishRename (bool keep);
    void ensureChannelVisible (int channel);
    int dropTargetAt (juce::Point<float> position) const;
    void showStatus (const juce::String& text);
    void scrollRack (float delta);
    void layoutRack();
    void layoutKnobs();
    bool knobVisible (const Knob& knob) const;

    void paintTopBar (juce::Graphics&) const;
    void paintLeftColumn (juce::Graphics&) const;
    void paintContent (juce::Graphics&) const;
    void paintPatternHeader (juce::Graphics&) const;
    void paintRow (juce::Graphics&, int channel) const;
    void paintStep (juce::Graphics&, int channel, int column) const;
    void paintBadge (juce::Graphics&, juce::Rectangle<float> area, bool ok, const juce::String& letter) const;
    void drawText (juce::Graphics&, const juce::String& text, const tokens::TextStyle& style, juce::Colour colour,
                   juce::Rectangle<float> area,
                   juce::Justification justification = juce::Justification::centredLeft) const;

    TeknoLookAndFeel& lnf;
    core::Session& session;
    core::CommandRegistry& commands;
    ProjectActions& actions;
    std::function<AudioStatus()> audioStatus;

    std::array<juce::TextButton, 4> navButtons;
    ValueChip swingChip, bpmChip, lengthChip;
    juce::TextButton playButton { "play" };
    juce::TextButton waveButton { "saw" };
    juce::TextButton sampleButton { "load" };
    juce::TextButton newButton { "new" }, openButton { "open" }, saveButton { "save" };
    juce::TextEditor renameEditor;
    std::array<Knob, 11> knobs;
    Terminal terminal;
    bool terminalOpen = false;
    LevelMeters meters;
    MixerView mixer;
    PianoRollView pianoRoll;
    core::Keymap& keymap;
    KeysView keysEditor;
    juce::TextButton editKeysButton { "edit" };
    std::array<juce::TextButton, 4> snapButtons;
    juce::TextButton keyButton; // the project's key: opens a menu of roots and scales
    void showKeyMenu();
    void showChannelMenu (int channel); // right-click on a channel's name
    TileLayout tiles;
    juce::Rectangle<float> tilesArea;
    juce::String draggingDivider; // the split being resized, if any
    void layoutChanged();
    void focusPanelAt (juce::Point<float> position);
    int renamingChannel = -1; // which name the rename editor is changing; -1 = the pattern's
    int dropChannel = -1;     // where a dragged file would go; -1 = no file over the window
    juce::String status;      // a short message in place of the meta line, e.g. after an import
    juce::uint32 statusUntil = 0;

    Snapshot shown;
    AudioStatus shownAudio;
    RackHit hovered;
    int page = 0;            // which 16 steps of the pattern the rack shows
    float rackScroll = 0.0f; // how far the rack is scrolled down, when its rows don't fit
    int timerTicks = 0;

    // Layout, computed in resized()
    juce::Rectangle<float> topBar, leftColumn, content;
    juce::Rectangle<float> projectArea, synthArea, keysArea, audioArea, sampleNameArea;
    juce::Rectangle<float> titleArea, metaArea, dividerArea, patternBarArea, gridArea, beatLabelArea, pageLabelArea;
    juce::Rectangle<float> terminalArea, rackArea; // rackArea: where the rows are visible
    float rackContentHeight = 0.0f;
    std::array<juce::Rectangle<float>, core::Session::maxPatterns> patternChips;
    std::array<juce::Rectangle<float>, stepsPerPage> columns; // x and width of each step column
    struct RowLayout
    {
        juce::Rectangle<float> pill, mute, solo;
        float stepY = 0.0f;
    };
    std::array<RowLayout, maxChannels> rows;
    float stepSize = 0.0f;
    int laidOutChannels = -1;
    std::uint32_t laidOutSamplers = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternView)
};
} // namespace tekno::ui
