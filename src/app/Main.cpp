// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <cstdlib>
#include <iostream>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AppOptions.h"
#include "app/MainWindow.h"
#include "core/Version.h"
#include "core/commands/CommandLine.h"
#include "core/commands/SessionCommands.h"
#include "core/engine/AudioEngine.h"
#include "core/engine/OfflineRender.h"
#include "core/model/Session.h"
#include "ui/KeyBindings.h"
#include "ui/Preferences.h"
#include "ui/ProjectActions.h"
#include "ui/theme/TeknoLookAndFeel.h"
#include "ui/views/PatternView.h"

namespace tekno::app
{
/** Owns everything and wires it together: session, commands, audio engine and device, window. */
class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "tekno-daw"; }
    const juce::String getApplicationVersion() override { return core::versionString(); }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        const auto options = AppOptions::parse (getCommandLineParameterArray());

        if (! options.errors.isEmpty())
        {
            std::cerr << options.errors.joinIntoString ("\n") << "\n\n" << AppOptions::helpText();
            exitEarly (2);
            return;
        }
        if (options.printHelp || options.printVersion)
        {
            std::cout << (options.printHelp ? AppOptions::helpText() : core::buildDescription() + "\n");
            exitEarly (0);
            return;
        }
        core::registerSessionCommands (commands, session);

        if (options.project != juce::File())
        {
            const auto opened = commands.execute ("project.open", { { "path", options.project.getFullPathName() } });
            if (! opened.succeeded())
            {
                std::cerr << opened.error << "\n";
                exitEarly (1);
                return;
            }
        }

        if (options.render != juce::File())
        {
            // The command line renders the project it is given, or the plain default one: never the demo,
            // so scripted renders stay the same.
            const auto result = core::renderToWav (session, options.render, options.bars);
            if (result.wasOk())
                std::cout << "rendered " << options.bars << " bars: " << options.render.getFullPathName() << "\n";
            else
                std::cerr << "render failed: " << result.getErrorMessage() << "\n";
            exitEarly (result.wasOk() ? 0 : 1);
            return;
        }

        juce::Logger::writeToLog (core::buildDescription());

        // Without a display (no X server, or no permission to use it) JUCE cannot create a window
        // and crashes later; say why and stop here instead. --render above works without one.
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            std::cerr << "tekno-daw: no display available (is DISPLAY set, and XAUTHORITY when on "
                         "Wayland?). To make a WAV without a window use --render=<file>.\n";
            exitEarly (1);
            return;
        }

        // With no project to open, the window starts on the demo: something to play straight away.
        // NEW goes back to the plain starting project.
        if (options.project == juce::File())
            if (const auto demo = commands.execute ("project.demo"); ! demo.succeeded())
                std::cerr << "tekno-daw: " << demo.error << "\n";

        projectActions = std::make_unique<ui::ProjectActions> (session, commands);

        preferences = std::make_unique<ui::Preferences>();
        core::setNoteNaming (preferences->noteNaming());
        lookAndFeel = std::make_unique<ui::TeknoLookAndFeel> (options.theme.value_or (preferences->theme()));
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        registerUiCommands();

        if (options.noAudio)
            audioProblem = "disabled with --no-audio";
        else
            startAudio();

        juce::StringArray keymapWarnings;
        keymap = ui::keys::load (ui::keys::defaultFile(), keymapWarnings);
        for (const auto& w : keymapWarnings)
            juce::Logger::writeToLog ("keymap: " + w);

        auto view = std::make_unique<ui::PatternView> (
            *lookAndFeel, session, commands, *projectActions, [this] { return audioStatus(); }, keymap,
            engine.liveInput(), [this] { return midiStatus(); });
        auto* viewPtr = view.get();
        mainView = viewPtr;
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *lookAndFeel, std::move (view));
        if (options.width > 0)
            mainWindow->centreWithSize (options.width, options.height);
        if (const auto snap = ui::PianoRollView::snapFromName (preferences->snap()))
            mainView->setSnap (*snap);
        mainView->onKeymapEdited = [this] { keymapChanged(); };
        {
            auto& keyboard = mainView->getKeyboard();
            const auto octaves = preferences->liveOctaves();
            keyboard.setOctaves (octaves[0], octaves[1]);
            keyboard.setVelocity (preferences->liveVelocity());
            keyboard.setShortGate (preferences->liveShortGate());
            keyboard.onSettingsChanged = [this, &keyboard]
            {
                preferences->setLive ({ keyboard.getAcidOctave(), keyboard.getSamplerOctave() }, keyboard.getVelocity(),
                                      keyboard.isShortGate());
            };
        }
        mainView->getPianoRoll().setScaleLock (preferences->scaleLock());
        mainView->getPianoRoll().setPreview (preferences->preview());

        // The panels as they were last time, unless --view asks for one (tests and snapshots do).
        if (options.view.isNotEmpty())
        {
            if (const auto outcome = commands.execute ("ui.view", { { "view", options.view } }); ! outcome.succeeded())
                std::cerr << "tekno-daw: " << outcome.error << "\n";
        }
        else if (const auto saved = ui::TileLayout::fromString (preferences->tiles()))
            mainView->setTiles (*saved);
        mainView->onLayoutChanged = [this] { preferences->setTiles (mainView->getTiles().toString()); };

        auto startupCommands = options.run;
        startupCommands.trim();
        startupCommands.removeEmptyStrings();
        if (! startupCommands.isEmpty())
            viewPtr->runInTerminal (startupCommands);

        if (options.snapshot != juce::File())
            juce::Timer::callAfterDelay (options.waitMs > 0 ? options.waitMs : 750,
                                         [this, file = options.snapshot] { writeSnapshotAndQuit (file); });
        else if (options.smokeTest)
        {
            // Every default key must run a command that exists. Only here are all of them registered.
            const auto defaults = ui::keys::defaults();
            for (const auto& b : defaults.all())
                if (! core::CommandLine::parse (b.command, commands).command)
                {
                    std::cerr << "tekno-daw: the default key " << b.key << " (" << b.context
                              << ") runs no command: " << b.command << "\n";
                    setApplicationReturnValue (1);
                }
            juce::Timer::callAfterDelay (options.waitMs > 0 ? options.waitMs : 1000, [this] { systemRequestedQuit(); });
        }
    }

    void shutdown() override
    {
        // Order matters: stop the audio before anything it reads goes away, and destroy components
        // before the LookAndFeel they point to.
        midiDevicesChanged.reset();
        if (virtualMidiInput != nullptr)
            virtualMidiInput->stop();
        virtualMidiInput.reset();
        if (deviceManager != nullptr)
        {
            deviceManager->removeMidiInputDeviceCallback ({}, &midiToLive);
            deviceManager->removeAudioCallback (&engine);
            deviceManager->closeAudioDevice();
            deviceManager.reset();
        }
        mainView = nullptr;
        mainWindow.reset();
        projectActions.reset();
        preferences.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override
    {
        if (projectActions != nullptr)
            projectActions->confirmDiscard ([this] { quit(); });
        else
            quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    /** JUCE's device manager with the drivers in our order. Through PipeWire, JACK plays with the
        shortest delay: one block after ours, where ALSA's ring buffer added three (measured 2026-11-15).
        JUCE's own order puts ALSA first. With no JACK server the JACK type lists no device, and JUCE
        opens ALSA. */
    struct DeviceManager final : juce::AudioDeviceManager
    {
        explicit DeviceManager (bool jackFirstIn)
            : jackFirst (jackFirstIn)
        {
        }

        void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override
        {
            auto* jack = juce::AudioIODeviceType::createAudioIODeviceType_JACK();
            auto* alsa = juce::AudioIODeviceType::createAudioIODeviceType_ALSA();
            for (auto* type : { jackFirst ? jack : alsa, jackFirst ? alsa : jack })
                if (type != nullptr)
                    types.add (type);
        }

        const bool jackFirst;
    };

    /** A JACK client can't choose its block size. Through PipeWire it asks for one before it connects,
        with this variable; a real JACK server ignores it and keeps the size it was started with. */
    static void requestJackBuffer (int samples)
    {
        ::setenv ("PIPEWIRE_LATENCY", (juce::String (samples) + "/48000").toRawUTF8(), 1);
    }

    /** Blocks of `samples` from the open device. JACK only changes size with a new client. */
    juce::String setBufferSize (int samples)
    {
        if (deviceManager->getCurrentAudioDeviceType() == "JACK")
        {
            requestJackBuffer (samples);
            deviceManager->closeAudioDevice();
            deviceManager->restartLastAudioDevice();
            return deviceManager->getCurrentAudioDevice() != nullptr ? juce::String()
                                                                     : juce::String ("the JACK device did not reopen");
        }
        auto setup = deviceManager->getAudioDeviceSetup();
        setup.bufferSize = samples;
        return deviceManager->setAudioDeviceSetup (setup, true);
    }

    void startAudio()
    {
        // A short buffer for playing live (default 256 samples, about 5 ms); the device may round it.
        const auto jack = preferences->audioDriver() == "jack";
        if (jack)
            requestJackBuffer (preferences->audioBuffer());
        deviceManager = std::make_unique<DeviceManager> (jack);
        audioProblem = deviceManager->initialiseWithDefaultDevices (0, 2);
        if (audioProblem.isEmpty() && deviceManager->getCurrentAudioDevice() == nullptr)
            audioProblem = "no output device found";

        if (audioProblem.isEmpty())
        {
            if (deviceManager->getCurrentAudioDeviceType() != "JACK")
                if (const auto problem = setBufferSize (preferences->audioBuffer()); problem.isNotEmpty())
                    juce::Logger::writeToLog ("audio: buffer of " + juce::String (preferences->audioBuffer()) +
                                              " refused: " + problem);
            deviceManager->addAudioCallback (&engine);
            startMidi();
            juce::Logger::writeToLog ("audio: " + deviceDescription());
        }
        else
        {
            juce::Logger::writeToLog ("audio: not available (" + audioProblem + ")");
        }
    }

    /** Every MIDI input there is, plus a virtual port "tekno-daw" others can connect to, all going
        to the selected channel. Only where the ALSA sequencer exists (JUCE asserts without it). */
    void startMidi()
    {
        if (! juce::File ("/dev/snd/seq").exists())
            return;
        enableMidiInputs();
        deviceManager->addMidiInputDeviceCallback ({}, &midiToLive);
        midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableMidiInputs(); });
        virtualMidiInput = juce::MidiInput::createNewDevice ("tekno-daw", &midiToLive);
        if (virtualMidiInput != nullptr)
            virtualMidiInput->start();
    }

    /** The keyboards, at the start and whenever one is plugged in: the ALSA sequencer's kernel clients
        (sound cards, USB keyboards, Midi Through). Programs connect to our "tekno-daw" port instead:
        listening to them too would play each of their notes twice. */
    void enableMidiInputs()
    {
        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (isKeyboard (device) && ! deviceManager->isMidiInputDeviceEnabled (device.identifier))
                deviceManager->setMidiInputDeviceEnabled (device.identifier, true);
    }

    /** JUCE names ALSA ports "client-port"; programs get client numbers from 128 up. An identifier in
        another form counts as a keyboard: a note heard twice is better than a keyboard not heard. */
    static bool isKeyboard (const juce::MidiDeviceInfo& device)
    {
        const auto client = device.identifier.upToFirstOccurrenceOf ("-", false, false);
        const auto isProgram = client.isNotEmpty() && client.containsOnly ("0123456789") && client.getIntValue() >= 128;
        return ! isProgram && device.name != "tekno-daw";
    }

    /** "JACK: Speaker, 48000 Hz, 256 samples" */
    juce::String deviceDescription() const
    {
        auto* device = deviceManager != nullptr ? deviceManager->getCurrentAudioDevice() : nullptr;
        if (device == nullptr)
            return "no device";
        return device->getTypeName() + ": " + device->getName() + ", " +
               juce::String (juce::roundToInt (device->getCurrentSampleRate())) + " Hz, " +
               juce::String (device->getCurrentBufferSizeSamples()) + " samples";
    }

    juce::String midiStatus() const
    {
        juce::StringArray inputs;
        if (deviceManager != nullptr)
            for (const auto& device : juce::MidiInput::getAvailableDevices())
                if (deviceManager->isMidiInputDeviceEnabled (device.identifier) && device.name != "tekno-daw")
                    inputs.add (device.name);
        juce::String text = inputs.isEmpty() ? juce::String ("no keyboard connected") : inputs.joinIntoString (", ");
        if (virtualMidiInput != nullptr)
            text << "; virtual port \"tekno-daw\"";
        return text;
    }

    /** MIDI notes, from the MIDI thread into the engine's own queue for that thread. Omni: every MIDI
        channel plays the selected channel. A velocity of 100 or more is an accent, as on 303 clones. */
    struct MidiToLive final : juce::MidiInputCallback
    {
        explicit MidiToLive (core::Session& s, core::AudioEngine& e)
            : session (s)
            , engine (e)
        {
        }

        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override
        {
            if (const auto event = core::LiveInput::fromMidi (message, session.selectedChannel.load(),
                                                              juce::Time::getMillisecondCounterHiRes()))
                engine.liveInput().push (core::LiveInput::Source::midi, *event);
        }

        core::Session& session;
        core::AudioEngine& engine;
    };

    ui::AudioStatus audioStatus() const
    {
        ui::AudioStatus status;
        auto* device = deviceManager != nullptr ? deviceManager->getCurrentAudioDevice() : nullptr;
        if (device != nullptr && device->isPlaying())
        {
            status.running = true;
            status.driver = device->getTypeName();
            status.deviceName = device->getName();
            status.sampleRate = device->getCurrentSampleRate();
            status.bufferSize = device->getCurrentBufferSizeSamples();
            status.outputLatency = device->getOutputLatencyInSamples();
            status.dropouts = device->getXRunCount();
            if (status.outputLatency == 0 && status.driver == "JACK")
            {
                // JUCE asks with an old JACK call that PipeWire answers with 0. The current call says a
                // PipeWire client is heard one block after it writes (checked 2026-11-15).
                status.outputLatency = status.bufferSize;
                status.outputLatencyGuessed = true;
            }
        }
        else
        {
            status.problem = audioProblem.isNotEmpty() ? audioProblem : "device stopped";
        }
        return status;
    }

    void registerUiCommands()
    {
        commands.add ({ "ui.theme.toggle",
                        "Switch between the light and the dark theme",
                        {},
                        [this] (const core::CommandArgs&)
                        {
                            using ui::tokens::Theme;
                            lookAndFeel->setTheme (lookAndFeel->getTheme() == Theme::dark ? Theme::light : Theme::dark);
                            preferences->setTheme (lookAndFeel->getTheme());
                            if (mainWindow != nullptr)
                                mainWindow->sendLookAndFeelChange();
                            return core::CommandOutcome::ok();
                        } });
        commands.add ({ "ui.snap",
                        "Where the piano roll puts notes: step, 1/32, 1/3 (triplets) or free",
                        { { "snap", core::CommandParam::Type::text, 0, 0, "step, 1/32, 1/3 or free" } },
                        [this] (const core::CommandArgs& args)
                        {
                            const auto name = args["snap"].toString().trim().toLowerCase();
                            const auto snap = ui::PianoRollView::snapFromName (name);
                            if (! snap)
                                return core::CommandOutcome::fail ("unknown snap '" + name +
                                                                   "': use step, 1/32, 1/3 or free");
                            if (mainView != nullptr)
                                mainView->setSnap (*snap);
                            preferences->setSnap (ui::PianoRollView::snapName (*snap));
                            return core::CommandOutcome::ok();
                        } });
        auto addToggle = [this] (const char* id, const char* description,
                                 std::function<void (ui::PianoRollView&, bool)> apply,
                                 std::function<void (ui::Preferences&, bool)> save)
        {
            commands.add ({ id,
                            description,
                            { { "on", core::CommandParam::Type::boolean, 0, 1, "on or off" } },
                            [this, apply, save] (const core::CommandArgs& args)
                            {
                                const bool on = args["on"];
                                if (mainView != nullptr)
                                    apply (mainView->getPianoRoll(), on);
                                save (*preferences, on);
                                return core::CommandOutcome::ok();
                            } });
        };
        addToggle (
            "ui.scalelock", "With a key chosen, put new and moved notes on the scale",
            [] (ui::PianoRollView& roll, bool on) { roll.setScaleLock (on); },
            [] (ui::Preferences& prefs, bool on) { prefs.setScaleLock (on); });
        addToggle (
            "ui.preview", "Hear notes as they are placed or moved in the piano roll",
            [] (ui::PianoRollView& roll, bool on) { roll.setPreview (on); },
            [] (ui::Preferences& prefs, bool on) { prefs.setPreview (on); });
        // Actions that only exist in the window, as commands so any key can run them.
        auto addAction = [this] (const char* id, const char* description, std::function<void()> action)
        {
            commands.add ({ id,
                            description,
                            {},
                            [this, action] (const core::CommandArgs&)
                            {
                                if (mainView == nullptr)
                                    return core::CommandOutcome::fail ("there is no window");
                                action();
                                return core::CommandOutcome::ok();
                            } });
        };
        addAction ("ui.terminal", "Open or close the terminal", [this] { mainView->toggleTerminal(); });
        addAction ("ui.new", "New project, asking about unsaved changes", [this] { projectActions->newProject(); });
        addAction ("ui.open", "Open a project with a file dialog", [this] { projectActions->openProject(); });
        addAction ("ui.save", "Save, asking for a file the first time", [this] { projectActions->save(); });
        addAction ("ui.saveAs", "Save to a new file, with a file dialog", [this] { projectActions->saveAs(); });
        addAction ("ui.roll.copy", "Copy the piano roll's selected notes",
                   [this] { mainView->getPianoRoll().copySelection(); });
        addAction ("ui.roll.paste", "Paste after the copied notes", [this] { mainView->getPianoRoll().paste(); });
        addAction ("ui.roll.duplicate", "Copy and paste the selection",
                   [this] { mainView->getPianoRoll().duplicate(); });
        addAction ("ui.roll.delete", "Delete the selected notes",
                   [this] { mainView->getPianoRoll().deleteSelection(); });
        addAction ("ui.roll.selectall", "Select every note in the piano roll",
                   [this] { mainView->getPianoRoll().selectAll(); });
        addAction ("ui.roll.deselect", "Clear the piano roll's selection",
                   [this] { mainView->getPianoRoll().deselect(); });
        auto panelArg = [] (const core::CommandArgs& args, juce::String& error) -> std::optional<ui::PatternView::View>
        {
            const auto name = args["panel"].toString().trim().toLowerCase();
            const auto panel = ui::PatternView::viewFromName (name);
            if (! panel)
                error = "unknown panel '" + name + "': use pattern, pianoroll, effects, mixer or keys";
            return panel;
        };
        commands.add (
            { "ui.tile",
              "Open a panel beside the active one: pattern, pianoroll, effects, mixer or keys",
              { { "panel", core::CommandParam::Type::text, 0, 0, "pattern, pianoroll, effects, mixer or keys" } },
              [this, panelArg] (const core::CommandArgs& args)
              {
                  juce::String error;
                  const auto panel = panelArg (args, error);
                  if (! panel)
                      return core::CommandOutcome::fail (error);
                  if (mainView != nullptr)
                      mainView->openTile (*panel);
                  return core::CommandOutcome::ok();
              } });
        commands.add ({ "ui.tile.close",
                        "Close the active panel",
                        {},
                        [this] (const core::CommandArgs&)
                        {
                            if (mainView == nullptr || ! mainView->closeTile())
                                return core::CommandOutcome::fail ("the last panel stays open");
                            return core::CommandOutcome::ok();
                        } });
        commands.add ({ "ui.tile.focus",
                        "Make the next or previous panel the active one",
                        { { "direction", core::CommandParam::Type::text, 0, 0, "next or previous" } },
                        [this] (const core::CommandArgs& args)
                        {
                            const auto direction = args["direction"].toString().trim().toLowerCase();
                            if (direction != "next" && direction != "previous")
                                return core::CommandOutcome::fail ("use next or previous");
                            if (mainView != nullptr)
                                mainView->focusTile (direction == "next" ? 1 : -1);
                            return core::CommandOutcome::ok();
                        } });
        addAction ("ui.tile.single", "Keep only the active panel", [this] { mainView->singleTile(); });
        commands.add ({ "ui.page",
                        "Show the next or previous 16 steps of a long pattern",
                        { { "direction", core::CommandParam::Type::text, 0, 0, "next or previous" } },
                        [this] (const core::CommandArgs& args)
                        {
                            const auto direction = args["direction"].toString().trim().toLowerCase();
                            if (direction != "next" && direction != "previous")
                                return core::CommandOutcome::fail ("use next or previous");
                            if (mainView != nullptr)
                                mainView->showPage (mainView->currentPage() + (direction == "next" ? 1 : -1));
                            return core::CommandOutcome::ok();
                        } });

        // The keymap, from the terminal (the keys view does the same with the mouse).
        commands.add ({ "keys.bind",
                        "Make a key run a command line in a context: keys.bind acid ctrl+1 cutoff 0.2",
                        { { "context", core::CommandParam::Type::text, 0, 0,
                            "global, pattern, pianoroll, mixer, effects, acid or sampler" },
                          { "key", core::CommandParam::Type::text, 0, 0, "e.g. ctrl+1, F5, space (no spaces inside)" },
                          { "command", core::CommandParam::Type::text, 0, 0, "the command line it runs" } },
                        [this] (const core::CommandArgs& args)
                        {
                            const auto context = args["context"].toString().trim().toLowerCase();
                            if (! core::Keymap::contexts().contains (context))
                                return core::CommandOutcome::fail ("unknown context '" + context + "': use " +
                                                                   core::Keymap::contexts().joinIntoString (", "));
                            const auto key = ui::keys::normalise (args["key"].toString());
                            if (! key)
                                return core::CommandOutcome::fail ("'" + args["key"].toString() + "' is not a key");
                            const auto command = args["command"].toString().trim();
                            if (const auto parsed = core::CommandLine::parse (command, commands); ! parsed.command)
                                return core::CommandOutcome::fail ("the command doesn't work: " + parsed.error);
                            const auto replaced = keymap.bind ({ context, *key, command, {} });
                            keymapChanged();
                            auto outcome = core::CommandOutcome::ok();
                            outcome.note = *key + " in " + context + " now runs \"" + command + "\"" +
                                           (replaced ? " (it ran \"" + replaced->command + "\")" : juce::String());
                            return outcome;
                        } });
        commands.add ({ "keys.unbind",
                        "Remove a key from a context",
                        { { "context", core::CommandParam::Type::text, 0, 0, "the context" },
                          { "key", core::CommandParam::Type::text, 0, 0, "e.g. ctrl+1" } },
                        [this] (const core::CommandArgs& args)
                        {
                            const auto key = ui::keys::normalise (args["key"].toString());
                            if (! key || ! keymap.unbind (args["context"].toString().trim().toLowerCase(), *key))
                                return core::CommandOutcome::fail ("no such key in that context");
                            keymapChanged();
                            return core::CommandOutcome::ok();
                        } });
        commands.add ({ "keys.list",
                        "Show the keys of a context, or all of them",
                        { { "context", core::CommandParam::Type::text, 0, 0, "a context (default: all)", true } },
                        [this] (const core::CommandArgs& args)
                        {
                            const auto only = args.contains ("context")
                                                  ? args["context"].toString().trim().toLowerCase()
                                                  : juce::String();
                            juce::StringArray lines;
                            for (const auto& b : keymap.all())
                                if (only.isEmpty() || b.context == only)
                                    lines.add (b.context + "  " + b.key + "  ->  " + b.command +
                                               (b.label.isNotEmpty() ? "   (" + b.label + ")" : juce::String()));
                            auto outcome = core::CommandOutcome::ok();
                            outcome.note = lines.isEmpty() ? juce::String ("no keys") : lines.joinIntoString ("\n");
                            return outcome;
                        } });
        commands.add ({ "keys.reset",
                        "Put every key back as tekno-daw ships it",
                        {},
                        [this] (const core::CommandArgs&)
                        {
                            keymap = ui::keys::defaults();
                            keymapChanged();
                            return core::CommandOutcome::ok();
                        } });

        // Playing live: the settings of the keyboard panel, and notes for the terminal and tests.
        auto keyboard = [this]() -> ui::KeyboardPanel*
        { return mainView != nullptr ? &mainView->getKeyboard() : nullptr; };
        auto intParam = [] (const char* name, double min, double max, const char* what)
        { return core::CommandParam { name, core::CommandParam::Type::integer, min, max, what }; };
        commands.add ({ "live.octave",
                        "Octave of the PC keyboard's Z key, for the selected channel's instrument",
                        { intParam ("octave", 0, 7, "0-7: Z plays C of this octave") },
                        [keyboard] (const core::CommandArgs& args)
                        {
                            if (auto* k = keyboard())
                                k->setOctave (args["octave"]);
                            return core::CommandOutcome::ok();
                        } });
        commands.add ({ "live.octave.shift",
                        "Move the PC keyboard up or down by octaves",
                        { intParam ("by", -7, 7, "octaves, negative = down") },
                        [keyboard] (const core::CommandArgs& args)
                        {
                            if (auto* k = keyboard())
                                k->setOctave (k->getOctave() + static_cast<int> (args["by"]));
                            return core::CommandOutcome::ok();
                        } });
        commands.add ({ "live.velocity",
                        "How hard the PC keyboard plays",
                        { intParam ("velocity", 1, 127, "1-127") },
                        [keyboard] (const core::CommandArgs& args)
                        {
                            if (auto* k = keyboard())
                                k->setVelocity (args["velocity"]);
                            return core::CommandOutcome::ok();
                        } });
        commands.add ({ "live.velocity.shift",
                        "Play the PC keyboard harder or softer",
                        { intParam ("by", -127, 127, "negative = softer") },
                        [keyboard] (const core::CommandArgs& args)
                        {
                            if (auto* k = keyboard())
                                k->setVelocity (k->getVelocity() + static_cast<int> (args["by"]));
                            return core::CommandOutcome::ok();
                        } });
        commands.add ({ "live.gate",
                        "hold: a note lasts as long as its key; short: one 303 step, always",
                        { { "mode", core::CommandParam::Type::text, 0, 0, "hold, short or toggle" } },
                        [keyboard] (const core::CommandArgs& args)
                        {
                            const auto mode = args["mode"].toString().trim().toLowerCase();
                            auto* k = keyboard();
                            if (mode != "hold" && mode != "short" && mode != "toggle")
                                return core::CommandOutcome::fail ("use hold, short or toggle");
                            if (k != nullptr)
                                k->setShortGate (mode == "toggle" ? ! k->isShortGate() : mode == "short");
                            return core::CommandOutcome::ok();
                        } });
        auto pushLive = [this] (core::LiveInput::Event::Type type, const core::CommandArgs& args)
        {
            core::LiveInput::Event e;
            e.type = type;
            e.timeMs = juce::Time::getMillisecondCounterHiRes();
            e.channel = static_cast<std::int8_t> (args.contains ("channel") ? static_cast<int> (args["channel"]) - 1
                                                                            : session.selectedChannel.load());
            if (args.contains ("note"))
                e.pitch = static_cast<std::uint8_t> (static_cast<int> (args["note"]));
            e.velocity =
                static_cast<std::uint8_t> (args.contains ("velocity") ? static_cast<int> (args["velocity"]) : 100);
            e.accent = args.contains ("accent") && static_cast<bool> (args["accent"]);
            return engine.liveInput().push (core::LiveInput::Source::ui, e)
                       ? core::CommandOutcome::ok()
                       : core::CommandOutcome::fail ("the audio isn't taking notes (is it running?)");
        };
        const core::CommandParam liveChannel { "channel",
                                               core::CommandParam::Type::integer,
                                               1,
                                               core::Session::maxChannels,
                                               "channel number (default: the selected one)",
                                               true };
        commands.add ({ "live.note",
                        "Start a note live, as if a key were pressed (live.release stops it)",
                        { { "note", core::CommandParam::Type::integer, 0, 127, "A2, La2 or 45" },
                          { "velocity", core::CommandParam::Type::integer, 1, 127, "1-127 (default 100)", true },
                          { "accent", core::CommandParam::Type::boolean, 0, 1, "on or off", true },
                          liveChannel },
                        [pushLive] (const core::CommandArgs& args)
                        { return pushLive (core::LiveInput::Event::Type::noteOn, args); } });
        commands.add ({ "live.release",
                        "Stop a live note",
                        { { "note", core::CommandParam::Type::integer, 0, 127, "A2, La2 or 45" }, liveChannel },
                        [pushLive] (const core::CommandArgs& args)
                        { return pushLive (core::LiveInput::Event::Type::noteOff, args); } });
        commands.add ({ "live.off",
                        "Stop every live note, on every channel",
                        {},
                        [this, pushLive] (const core::CommandArgs& args)
                        {
                            if (mainView != nullptr)
                                mainView->getKeyboard().allOff();
                            return pushLive (core::LiveInput::Event::Type::allOff, args);
                        } });
        commands.add ({ "audio.buffer",
                        "Samples per audio block: smaller = less latency, more CPU (default 256)",
                        { intParam ("samples", 32, 4096, "e.g. 128, 256, 512") },
                        [this] (const core::CommandArgs& args)
                        {
                            const int samples = args["samples"];
                            preferences->setAudioBuffer (samples);
                            if (deviceManager == nullptr || deviceManager->getCurrentAudioDevice() == nullptr)
                                return core::CommandOutcome::fail ("no audio device: saved for the next start");
                            if (const auto problem = setBufferSize (samples); problem.isNotEmpty())
                                return core::CommandOutcome::fail (problem);
                            auto outcome = core::CommandOutcome::ok();
                            outcome.note = deviceDescription();
                            return outcome;
                        } });
        commands.add ({ "audio.driver",
                        "Play through JACK (the shortest delay, PipeWire included) or ALSA. Default jack",
                        { { "driver", core::CommandParam::Type::text, 0, 0, "jack or alsa" } },
                        [this] (const core::CommandArgs& args)
                        {
                            const auto name = args["driver"].toString().trim().toLowerCase();
                            if (name != "jack" && name != "alsa")
                                return core::CommandOutcome::fail ("unknown driver '" + name + "': use jack or alsa");
                            preferences->setAudioDriver (name);
                            if (deviceManager == nullptr)
                                return core::CommandOutcome::fail ("audio is off: saved for the next start");

                            const juce::String typeName = name == "jack" ? "JACK" : "ALSA";
                            auto* type = [&]() -> juce::AudioIODeviceType*
                            {
                                for (auto* t : deviceManager->getAvailableDeviceTypes())
                                    if (t->getTypeName() == typeName)
                                        return t;
                                return nullptr;
                            }();
                            if (type == nullptr)
                                return core::CommandOutcome::fail (typeName + " is not built in");
                            type->scanForDevices();
                            if (type->getDeviceNames (false).isEmpty())
                                return core::CommandOutcome::fail (
                                    name == "jack" ? "no JACK server answers (PipeWire needs pipewire-jack)"
                                                   : "ALSA lists no output device");

                            if (name == "jack")
                                requestJackBuffer (preferences->audioBuffer());
                            deviceManager->setCurrentAudioDeviceType (typeName, true); // JUCE waits 1.5 s in between
                            if (deviceManager->getCurrentAudioDevice() == nullptr)
                                return core::CommandOutcome::fail ("the " + typeName + " device did not open");
                            if (name == "alsa")
                                setBufferSize (preferences->audioBuffer());
                            auto outcome = core::CommandOutcome::ok();
                            outcome.note = deviceDescription();
                            return outcome;
                        } });
        commands.add ({ "midi.inputs",
                        "Show the MIDI inputs tekno-daw listens to",
                        {},
                        [this] (const core::CommandArgs&)
                        {
                            auto outcome = core::CommandOutcome::ok();
                            outcome.note = midiStatus();
                            return outcome;
                        } });

        commands.add ({ "ui.notes",
                        "Name notes with letters (C D E) or as in Italy (Do Re Mi). Same octaves: C4 = Do4",
                        { { "names", core::CommandParam::Type::text, 0, 0, "letters or latin (abc, doremi)" } },
                        [this] (const core::CommandArgs& args)
                        {
                            const auto name = args["names"].toString();
                            const auto naming = ui::Preferences::namingFromName (name);
                            if (! naming)
                                return core::CommandOutcome::fail ("unknown note names '" + name.trim() +
                                                                   "': use letters (C D E) or latin (Do Re Mi)");
                            core::setNoteNaming (*naming);
                            preferences->setNoteNaming (*naming);
                            if (mainWindow != nullptr)
                                mainWindow->repaint();
                            return core::CommandOutcome::ok();
                        } });
        commands.add ({ "ui.view",
                        "Show a panel where the active one is: pattern (F6), pianoroll (F7), effects (F8), mixer (F9) "
                        "or keys (F10)",
                        { { "view", core::CommandParam::Type::text, 0, 0, "pattern, pianoroll or mixer" } },
                        [this] (const core::CommandArgs& args)
                        {
                            const auto name = args["view"].toString().trim().toLowerCase();
                            const auto view = ui::PatternView::viewFromName (name);
                            if (! view)
                                return core::CommandOutcome::fail ("unknown view '" + name +
                                                                   "': use pattern, pianoroll or mixer");
                            if (mainView != nullptr)
                                mainView->showView (*view);
                            return core::CommandOutcome::ok();
                        } });
    }

    /** The keymap changed: save it, and let the window show it. */
    void keymapChanged()
    {
        if (! ui::keys::save (keymap, ui::keys::defaultFile()))
            juce::Logger::writeToLog ("keymap: cannot save " + ui::keys::defaultFile().getFullPathName());
        if (mainView != nullptr)
            mainView->keymapChanged();
    }

    void exitEarly (int code)
    {
        setApplicationReturnValue (code);
        quit();
    }

    void writeSnapshotAndQuit (const juce::File& file)
    {
        auto* content = mainWindow != nullptr ? mainWindow->getContentComponent() : nullptr;
        auto ok = false;

        if (content != nullptr && file.getParentDirectory().createDirectory())
        {
            const auto image = content->createComponentSnapshot (content->getLocalBounds());
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            ok = out.openedOk() && png.writeImageToStream (image, out);
        }

        if (ok)
            std::cout << "snapshot written: " << file.getFullPathName() << "\n";
        else
            std::cerr << "could not write snapshot: " << file.getFullPathName() << "\n";

        exitEarly (ok ? 0 : 1);
    }

    core::Session session;
    core::CommandRegistry commands;
    core::AudioEngine engine { session };
    std::unique_ptr<juce::AudioDeviceManager> deviceManager;
    MidiToLive midiToLive { session, engine };
    std::unique_ptr<juce::MidiInput> virtualMidiInput;
    juce::MidiDeviceListConnection midiDevicesChanged; // enables keyboards plugged in later
    juce::String audioProblem;

    std::unique_ptr<ui::ProjectActions> projectActions;
    std::unique_ptr<ui::TeknoLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<ui::Preferences> preferences;
    core::Keymap keymap;                 // the person's keys: ~/.config/tekno-daw/keymap.json
    ui::PatternView* mainView = nullptr; // owned by mainWindow; the target of ui.view
};
} // namespace tekno::app

START_JUCE_APPLICATION (tekno::app::Application)
