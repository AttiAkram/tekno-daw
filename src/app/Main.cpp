// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <iostream>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AppOptions.h"
#include "app/MainWindow.h"
#include "core/Version.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::app
{
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

        juce::Logger::writeToLog (core::buildDescription());

        lookAndFeel = std::make_unique<ui::TeknoLookAndFeel> (options.theme);
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *lookAndFeel);

        if (options.snapshot != juce::File())
            juce::Timer::callAfterDelay (750, [this, file = options.snapshot] { writeSnapshotAndQuit (file); });
        else if (options.smokeTest)
            juce::Timer::callAfterDelay (1000, [this] { systemRequestedQuit(); });
    }

    void shutdown() override
    {
        // Order matters: components must go before the LookAndFeel they point to.
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override {}

private:
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

    std::unique_ptr<ui::TeknoLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace tekno::app

START_JUCE_APPLICATION (tekno::app::Application)
