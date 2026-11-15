// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

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
            *lookAndFeel, session, commands, *projectActions, [this] { return audioStatus(); }, keymap);
        auto* viewPtr = view.get();
        mainView = viewPtr;
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *lookAndFeel, std::move (view));
        if (const auto snap = ui::PianoRollView::snapFromName (preferences->snap()))
            mainView->setSnap (*snap);
        mainView->onKeymapEdited = [this] { keymapChanged(); };
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
            juce::Timer::callAfterDelay (750, [this, file = options.snapshot] { writeSnapshotAndQuit (file); });
        else if (options.smokeTest)
            juce::Timer::callAfterDelay (1000, [this] { systemRequestedQuit(); });
    }

    void shutdown() override
    {
        // Order matters: stop the audio before anything it reads goes away, and destroy components
        // before the LookAndFeel they point to.
        if (deviceManager != nullptr)
        {
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
    void startAudio()
    {
        deviceManager = std::make_unique<juce::AudioDeviceManager>();
        audioProblem = deviceManager->initialiseWithDefaultDevices (0, 2);
        if (audioProblem.isEmpty() && deviceManager->getCurrentAudioDevice() == nullptr)
            audioProblem = "no output device found";

        if (audioProblem.isEmpty())
        {
            deviceManager->addAudioCallback (&engine);
            auto* device = deviceManager->getCurrentAudioDevice();
            juce::Logger::writeToLog ("audio: " + device->getName() + ", " +
                                      juce::String (device->getCurrentSampleRate()) + " Hz, " +
                                      juce::String (device->getCurrentBufferSizeSamples()) + " samples");
        }
        else
        {
            juce::Logger::writeToLog ("audio: not available (" + audioProblem + ")");
        }
    }

    ui::AudioStatus audioStatus() const
    {
        ui::AudioStatus status;
        auto* device = deviceManager != nullptr ? deviceManager->getCurrentAudioDevice() : nullptr;
        if (device != nullptr && device->isPlaying())
        {
            status.running = true;
            status.deviceName = device->getName();
            status.sampleRate = device->getCurrentSampleRate();
            status.bufferSize = device->getCurrentBufferSizeSamples();
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
        commands.add ({ "ui.tile",
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
