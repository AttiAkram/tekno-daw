// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/commands/SessionCommands.h"

#include <array>

namespace tekno::core
{
namespace
{
    using Type = CommandParam::Type;

    const CommandParam stepParam { "step", Type::integer, 0, Pattern::numSteps - 1, "step index, 0-15" };
    const CommandParam onParam { "on", Type::boolean, 0, 1, "true or false" };

    int stepArg (const CommandArgs& args)
    {
        return static_cast<int> (args["step"]);
    }

    /** Changes one step and returns how to put it back. */
    CommandOutcome editStep (Session& session, int index, const std::function<void (Step&)>& change)
    {
        const auto before = session.pattern.getStep (index);
        auto after = before;
        change (after);
        session.pattern.setStep (index, after);
        return CommandOutcome::ok ([&session, index, before] { session.pattern.setStep (index, before); });
    }

    void addSynthParam (CommandRegistry& registry, const char* id, const char* what, std::atomic<float>& target)
    {
        registry.add ({ id,
                        what,
                        { { "value", Type::number, 0, 1, "0..1" } },
                        [&target] (const CommandArgs& args)
                        {
                            target.store (static_cast<float> (static_cast<double> (args["value"])));
                            return CommandOutcome::ok();
                        } });
    }
} // namespace

void registerSessionCommands (CommandRegistry& registry, Session& session)
{
    auto& transport = session.transport;

    // --- transport ------------------------------------------------------------------------------
    registry.add ({ "transport.play",
                    "Start playing the pattern from the first step",
                    {},
                    [&transport] (const CommandArgs&)
                    {
                        transport.playing.store (true);
                        return CommandOutcome::ok();
                    } });
    registry.add ({ "transport.stop",
                    "Stop playing",
                    {},
                    [&transport] (const CommandArgs&)
                    {
                        transport.playing.store (false);
                        return CommandOutcome::ok();
                    } });
    registry.add ({ "transport.toggle",
                    "Play if stopped, stop if playing",
                    {},
                    [&transport] (const CommandArgs&)
                    {
                        transport.playing.store (! transport.playing.load());
                        return CommandOutcome::ok();
                    } });
    registry.add ({ "transport.bpm",
                    "Set the tempo",
                    { { "bpm", Type::number, TransportState::minBpm, TransportState::maxBpm, "beats per minute" } },
                    [&transport] (const CommandArgs& args)
                    {
                        transport.bpm.store (static_cast<double> (args["bpm"]));
                        return CommandOutcome::ok();
                    } });

    // --- pattern (undoable) ---------------------------------------------------------------------
    registry.add ({ "pattern.step.toggle",
                    "Turn a step on or off",
                    { stepParam },
                    [&session] (const CommandArgs& args)
                    { return editStep (session, stepArg (args), [] (Step& s) { s.on = ! s.on; }); } });
    registry.add ({ "pattern.step.accent",
                    "Set the accent of a step (turns the step on)",
                    { stepParam, onParam },
                    [&session] (const CommandArgs& args)
                    {
                        const bool on = args["on"];
                        return editStep (session, stepArg (args),
                                         [on] (Step& s)
                                         {
                                             s.accent = on;
                                             s.on = s.on || on;
                                         });
                    } });
    registry.add ({ "pattern.step.slide",
                    "Set the slide of a step into the next one (turns the step on)",
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
                    } });
    registry.add ({ "pattern.step.note",
                    "Set the pitch of a step",
                    { stepParam, { "note", Type::integer, Step::minNote, Step::maxNote, "MIDI note number" } },
                    [&session] (const CommandArgs& args)
                    {
                        const int note = args["note"];
                        return editStep (session, stepArg (args), [note] (Step& s) { s.note = note; });
                    } });
    registry.add ({ "pattern.clear",
                    "Turn every step off",
                    {},
                    [&session] (const CommandArgs&)
                    {
                        std::array<Step, Pattern::numSteps> before;
                        for (int i = 0; i < Pattern::numSteps; ++i)
                        {
                            before[static_cast<size_t> (i)] = session.pattern.getStep (i);
                            auto off = before[static_cast<size_t> (i)];
                            off.on = false;
                            session.pattern.setStep (i, off);
                        }
                        return CommandOutcome::ok (
                            [&session, before]
                            {
                                for (int i = 0; i < Pattern::numSteps; ++i)
                                    session.pattern.setStep (i, before[static_cast<size_t> (i)]);
                            });
                    } });

    // --- synth ----------------------------------------------------------------------------------
    auto& synth = session.synth;
    addSynthParam (registry, "synth.cutoff", "Filter cutoff", synth.cutoff);
    addSynthParam (registry, "synth.resonance", "Filter resonance", synth.resonance);
    addSynthParam (registry, "synth.envmod", "How far the envelope opens the filter", synth.envMod);
    addSynthParam (registry, "synth.decay", "How fast the filter envelope closes", synth.decay);
    addSynthParam (registry, "synth.accent", "How hard accented steps hit", synth.accent);
    registry.add ({ "synth.waveform",
                    "Choose the oscillator: saw or square",
                    { { "square", Type::boolean, 0, 1, "true = square, false = saw" } },
                    [&synth] (const CommandArgs& args)
                    {
                        synth.square.store (static_cast<bool> (args["square"]));
                        return CommandOutcome::ok();
                    } });

    // --- history --------------------------------------------------------------------------------
    registry.add ({ "edit.undo",
                    "Undo the last pattern edit",
                    {},
                    [&registry] (const CommandArgs&)
                    { return registry.undo() ? CommandOutcome::ok() : CommandOutcome::fail ("nothing to undo"); } });
    registry.add ({ "edit.redo",
                    "Redo the last undone edit",
                    {},
                    [&registry] (const CommandArgs&)
                    { return registry.redo() ? CommandOutcome::ok() : CommandOutcome::fail ("nothing to redo"); } });
}
} // namespace tekno::core
