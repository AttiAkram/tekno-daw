// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/commands/SessionCommands.h"

#include <array>

#include "core/io/ProjectFile.h"

namespace tekno::core
{
namespace
{
    using Type = CommandParam::Type;

    const CommandParam stepParam { "step", Type::integer, 1, Pattern::maxSteps, "step number, 1-64" };
    const CommandParam onParam { "on", Type::boolean, 0, 1, "true or false" };
    const CommandParam pathParam { "path", Type::text, 0, 0, "path of a .tekno file" };

    int stepArg (const CommandArgs& args)
    {
        return static_cast<int> (args["step"]) - 1; // commands count from 1, like people do
    }

    /** Changes one step of the selected pattern and returns how to put it back. */
    CommandOutcome editStep (Session& session, int index, const std::function<void (Step&)>& change)
    {
        const auto patternIndex = session.selected.load();
        auto& pattern = session.pattern (patternIndex);
        const auto before = pattern.getStep (index);
        auto after = before;
        change (after);
        pattern.setStep (index, after);
        return CommandOutcome::ok ([&session, patternIndex, index, before]
                                   { session.pattern (patternIndex).setStep (index, before); });
    }

    /** Sets an atomic and returns how to put the old value back. */
    template <typename T, typename V> CommandOutcome setUndoable (std::atomic<T>& target, V newValue)
    {
        const auto before = target.load();
        target.store (static_cast<T> (newValue));
        return CommandOutcome::ok ([&target, before] { target.store (before); });
    }

    void addSynthParam (CommandRegistry& registry, const char* id, const char* what, std::atomic<float>& target)
    {
        CommandDefinition def { id,
                                what,
                                { { "value", Type::number, 0, 1, "0..1" } },
                                [&target] (const CommandArgs& args)
                                { return setUndoable (target, static_cast<double> (args["value"])); } };
        def.modifiesProject = true;
        def.coalesce = true;
        registry.add (std::move (def));
    }

    juce::File pathArg (const CommandArgs& args)
    {
        auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args["path"].toString());
        if (! file.hasFileExtension (io::project::extension))
            file = file.withFileExtension (io::project::extension);
        return file;
    }

    CommandOutcome saveTo (Session& session, const juce::File& file)
    {
        if (const auto result = io::project::save (session, file); result.failed())
            return CommandOutcome::fail (result.getErrorMessage());
        session.project.file = file;
        session.project.savedRevision = session.project.revision;
        return CommandOutcome::ok();
    }

    /** Small helper so the long list below stays readable. */
    void add (CommandRegistry& registry, const char* id, const char* description, std::vector<CommandParam> params,
              std::function<CommandOutcome (const CommandArgs&)> run, bool modifiesProject, bool coalesce = false)
    {
        CommandDefinition def { id, description, std::move (params), std::move (run) };
        def.modifiesProject = modifiesProject;
        def.coalesce = coalesce;
        registry.add (std::move (def));
    }
} // namespace

void registerSessionCommands (CommandRegistry& registry, Session& session)
{
    auto& transport = session.transport;
    registry.onProjectModified = [&session] { ++session.project.revision; };

    // --- transport ------------------------------------------------------------------------------
    add (
        registry, "transport.play", "Start playing the selected pattern from its first step", {},
        [&transport] (const CommandArgs&)
        {
            transport.playing.store (true);
            return CommandOutcome::ok();
        },
        false);
    add (
        registry, "transport.stop", "Stop playing", {},
        [&transport] (const CommandArgs&)
        {
            transport.playing.store (false);
            return CommandOutcome::ok();
        },
        false);
    add (
        registry, "transport.toggle", "Play if stopped, stop if playing", {},
        [&transport] (const CommandArgs&)
        {
            transport.playing.store (! transport.playing.load());
            return CommandOutcome::ok();
        },
        false);
    add (
        registry, "transport.bpm", "Set the tempo",
        { { "bpm", Type::number, TransportState::minBpm, TransportState::maxBpm, "beats per minute" } },
        [&transport] (const CommandArgs& args)
        { return setUndoable (transport.bpm, static_cast<double> (args["bpm"])); }, true, true);
    add (
        registry, "transport.swing", "Set the swing (MPC style: 50 straight, 66 triplet, 75 dotted)",
        { { "percent", Type::number, TransportState::minSwing, TransportState::maxSwing, "50..75" } },
        [&transport] (const CommandArgs& args)
        { return setUndoable (transport.swing, static_cast<double> (args["percent"])); }, true, true);

    // --- patterns -------------------------------------------------------------------------------
    add (
        registry, "pattern.select", "Choose the pattern to edit; while playing it starts at the next bar",
        { { "pattern", Type::integer, 1, Session::maxPatterns, "pattern number, 1-16" } },
        [&session] (const CommandArgs& args)
        {
            session.selected.store (static_cast<int> (args["pattern"]) - 1);
            if (! session.transport.playing.load())
                session.playing.store (session.selected.load());
            return CommandOutcome::ok();
        },
        false);
    add (
        registry, "pattern.length", "Set how many steps the selected pattern plays (1-64)",
        { { "steps", Type::integer, 1, Pattern::maxSteps, "number of steps" } },
        [&session] (const CommandArgs& args)
        {
            const auto index = session.selected.load();
            const auto before = session.pattern (index).getLength();
            session.pattern (index).setLength (static_cast<int> (args["steps"]));
            return CommandOutcome::ok ([&session, index, before] { session.pattern (index).setLength (before); });
        },
        true, true);
    add (
        registry, "pattern.rename", "Rename the selected pattern", { { "name", Type::text, 0, 0, "new name" } },
        [&session] (const CommandArgs& args)
        {
            const auto name = args["name"].toString().trim().substring (0, 40);
            if (name.isEmpty())
                return CommandOutcome::fail ("a pattern needs a name");
            const auto index = static_cast<size_t> (session.selected.load());
            const auto before = session.project.patternNames[index];
            session.project.patternNames[index] = name;
            return CommandOutcome::ok ([&session, index, before] { session.project.patternNames[index] = before; });
        },
        true);
    add (
        registry, "pattern.clear", "Turn every step of the selected pattern off", {},
        [&session] (const CommandArgs&)
        {
            const auto index = session.selected.load();
            std::array<Step, Pattern::maxSteps> before;
            for (int i = 0; i < Pattern::maxSteps; ++i)
            {
                before[static_cast<size_t> (i)] = session.pattern (index).getStep (i);
                auto off = before[static_cast<size_t> (i)];
                off.on = false;
                session.pattern (index).setStep (i, off);
            }
            return CommandOutcome::ok (
                [&session, index, before]
                {
                    for (int i = 0; i < Pattern::maxSteps; ++i)
                        session.pattern (index).setStep (i, before[static_cast<size_t> (i)]);
                });
        },
        true);

    // --- steps of the selected pattern (undoable) -----------------------------------------------
    add (
        registry, "pattern.step.toggle", "Turn a step on or off", { stepParam }, [&session] (const CommandArgs& args)
        { return editStep (session, stepArg (args), [] (Step& s) { s.on = ! s.on; }); }, true);
    add (
        registry, "pattern.step.accent", "Set the accent of a step (turns the step on)", { stepParam, onParam },
        [&session] (const CommandArgs& args)
        {
            const bool on = args["on"];
            return editStep (session, stepArg (args),
                             [on] (Step& s)
                             {
                                 s.accent = on;
                                 s.on = s.on || on;
                             });
        },
        true);
    add (
        registry, "pattern.step.slide", "Set the slide of a step into the next one (turns the step on)",
        { stepParam, onParam },
        [&session] (const CommandArgs& args)
        {
            const bool on = args["on"];
            return editStep (session, stepArg (args),
                             [on] (Step& s)
                             {
                                 s.slide = on;
                                 s.on = s.on || on;
                             });
        },
        true);
    add (
        registry, "pattern.step.tie", "Make a step hold the previous note instead of playing a new one (turns it on)",
        { stepParam, onParam },
        [&session] (const CommandArgs& args)
        {
            const bool on = args["on"];
            return editStep (session, stepArg (args),
                             [on] (Step& s)
                             {
                                 s.tie = on;
                                 s.on = s.on || on;
                             });
        },
        true);
    add (
        registry, "pattern.step.note", "Set the pitch of a step",
        { stepParam, { "note", Type::integer, Step::minNote, Step::maxNote, "MIDI note number" } },
        [&session] (const CommandArgs& args)
        {
            const int note = args["note"];
            return editStep (session, stepArg (args), [note] (Step& s) { s.note = note; });
        },
        true);

    // --- synth ----------------------------------------------------------------------------------
    auto& synth = session.synth;
    addSynthParam (registry, "synth.cutoff", "Filter cutoff", synth.cutoff);
    addSynthParam (registry, "synth.resonance", "Filter resonance", synth.resonance);
    addSynthParam (registry, "synth.envmod", "How far the envelope opens the filter", synth.envMod);
    addSynthParam (registry, "synth.decay", "How fast the filter envelope closes", synth.decay);
    addSynthParam (registry, "synth.accent", "How hard accented steps hit", synth.accent);
    addSynthParam (registry, "synth.drive", "Distortion after the filter, the acid pedal", synth.drive);
    add (
        registry, "synth.waveform", "Choose the oscillator: saw or square",
        { { "square", Type::boolean, 0, 1, "true = square, false = saw" } }, [&synth] (const CommandArgs& args)
        { return setUndoable (synth.square, static_cast<bool> (args["square"])); }, true);

    // --- project --------------------------------------------------------------------------------
    add (
        registry, "project.new", "Start a new project (does not ask about unsaved changes)", {},
        [&session, &registry] (const CommandArgs&)
        {
            session.loadDefaultProject();
            registry.clearHistory();
            return CommandOutcome::ok();
        },
        false);
    add (
        registry, "project.open", "Open a .tekno project (does not ask about unsaved changes)", { pathParam },
        [&session, &registry] (const CommandArgs& args)
        {
            const auto file = pathArg (args);
            if (const auto result = io::project::load (file, session); result.failed())
                return CommandOutcome::fail (result.getErrorMessage());
            session.project.file = file;
            session.project.revision = session.project.savedRevision = 0;
            registry.clearHistory();
            return CommandOutcome::ok();
        },
        false);
    add (
        registry, "project.save", "Save the project to its file", {},
        [&session] (const CommandArgs&)
        {
            if (session.project.file == juce::File())
                return CommandOutcome::fail ("the project has no file yet: use project.saveAs");
            return saveTo (session, session.project.file);
        },
        false);
    add (
        registry, "project.saveAs", "Save the project to a new file (overwrites without asking)", { pathParam },
        [&session] (const CommandArgs& args) { return saveTo (session, pathArg (args)); }, false);

    // --- history --------------------------------------------------------------------------------
    add (
        registry, "edit.undo", "Undo the last pattern edit", {}, [&registry] (const CommandArgs&)
        { return registry.undo() ? CommandOutcome::ok() : CommandOutcome::fail ("nothing to undo"); }, false);
    add (
        registry, "edit.redo", "Redo the last undone edit", {}, [&registry] (const CommandArgs&)
        { return registry.redo() ? CommandOutcome::ok() : CommandOutcome::fail ("nothing to redo"); }, false);
}
} // namespace tekno::core
