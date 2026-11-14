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
#include "core/commands/SessionCommands.h"
#include "core/engine/AudioEngine.h"
#include "core/engine/OfflineRender.h"
#include "core/model/Session.h"
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

        auto view = std::make_unique<ui::PatternView> (*lookAndFeel, session, commands, *projectActions,
                                                       [this] { return audioStatus(); });
        auto* viewPtr = view.get();
        mainView = viewPtr;
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *lookAndFeel, std::move (view));
        if (const auto snap = ui::PianoRollView::snapFromName (preferences->snap()))
            mainView->setSnap (*snap);

        if (options.view.isNotEmpty())
            if (const auto outcome = commands.execute ("ui.view", { { "view", options.view } }); ! outcome.succeeded())
                std::cerr << "tekno-daw: " << outcome.error << "\n";

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
                        "Show a view: pattern (F6), pianoroll (F7) or mixer (F9)",
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
    ui::PatternView* mainView = nullptr; // owned by mainWindow; the target of ui.view
};
} // namespace tekno::app

START_JUCE_APPLICATION (tekno::app::Application)
