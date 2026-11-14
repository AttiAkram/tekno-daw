// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/commands/SessionCommands.h"

#include <array>
#include <optional>

#include "core/io/ProjectFile.h"

namespace tekno::core
{
namespace
{
    using Type = CommandParam::Type;

    const CommandParam stepParam { "step", Type::integer, 1, Pattern::maxSteps, "step number, 1-64" };
    const CommandParam onParam { "on", Type::boolean, 0, 1, "true or false" };
    const CommandParam pathParam { "path", Type::text, 0, 0, "path of a .tekno file" };
    const CommandParam unitParam { "value", Type::number, 0, 1, "0..1" };

    // Optional targets: left out, a command acts on the selection and records which one.
    const CommandParam patternTarget {
        "pattern", Type::integer, 1, Session::maxPatterns, "pattern number (default: the selected pattern)", true
    };
    const CommandParam channelTarget {
        "channel", Type::integer, 1, Session::maxChannels, "channel number (default: the selected channel)", true
    };

    int stepArg (const CommandArgs& args)
    {
        return static_cast<int> (args["step"]) - 1; // commands count from 1, like people do
    }

    int patternArg (const Session& session, const CommandArgs& args)
    {
        return args.contains ("pattern") ? static_cast<int> (args["pattern"]) - 1 : session.selected.load();
    }

    /** The channel a command acts on, checked: it must exist and, if asked, play the right instrument. */
    struct ChannelTarget
    {
        int index = -1;
        juce::String error;
    };

    ChannelTarget channelArg (const Session& session, const CommandArgs& args,
                              std::optional<InstrumentKind> required = std::nullopt)
    {
        const auto index =
            args.contains ("channel") ? static_cast<int> (args["channel"]) - 1 : session.selectedChannel.load();
        const auto count = session.channelCount();
        if (index < 0 || index >= count)
            return { -1, "there is no channel " + juce::String (index + 1) + " (the project has " +
                             juce::String (count) + ")" };

        const auto kind = session.channel (index).kind.load();
        if (required && kind != *required)
            return { -1, "channel " + juce::String (index + 1) + " (" +
                             session.project.channelNames[static_cast<size_t> (index)] + ") is " +
                             (kind == InstrumentKind::sampler ? "a sampler" : "an acid synth") +
                             ": this command is for " + (*required == InstrumentKind::sampler ? "sampler" : "acid") +
                             " channels" };
        return { index, {} };
    }

    /** Records the targets the command really used, so redo goes back to them. */
    CommandOutcome withTargets (CommandOutcome outcome, const CommandArgs& args, int pattern, int channel)
    {
        if (! outcome.succeeded())
            return outcome;
        outcome.resolvedArgs = args;
        if (pattern >= 0)
            outcome.resolvedArgs.set ("pattern", pattern + 1);
        if (channel >= 0)
            outcome.resolvedArgs.set ("channel", channel + 1);
        return outcome;
    }

    /** Changes one step and returns how to put it back. */
    CommandOutcome editStep (Session& session, const CommandArgs& args, const std::function<void (Step&)>& change)
    {
        const auto target = channelArg (session, args);
        if (target.index < 0)
            return CommandOutcome::fail (target.error);
        const auto patternIndex = patternArg (session, args);
        const auto channel = target.index;
        const auto index = stepArg (args);

        auto& lane = session.pattern (patternIndex).lane (channel);
        const auto before = lane.getStep (index);
        auto after = before;
        change (after);

        // A sampler plays its sample at its own pitch on C4: an untouched step turned on starts there.
        if (! before.on && after.on && before == Step {} &&
            session.channel (channel).kind.load() == InstrumentKind::sampler)
            after.note = Step::samplerRootNote;

        lane.setStep (index, after);
        return withTargets (
            CommandOutcome::ok ([&session, patternIndex, channel, index, before]
                                { session.pattern (patternIndex).lane (channel).setStep (index, before); }),
            args, patternIndex, channel);
    }

    /** Sets an atomic and returns how to put the old value back. */
    template <typename T, typename V> CommandOutcome setUndoable (std::atomic<T>& target, V newValue)
    {
        const auto before = target.load();
        target.store (static_cast<T> (newValue));
        return CommandOutcome::ok ([&target, before] { target.store (before); });
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

    /** A continuous control of one channel (a knob, a fader): undoable, coalescing within a gesture. */
    template <typename Field>
    void addChannelControl (CommandRegistry& registry, Session& session, const char* id, const char* description,
                            const CommandParam& value, std::optional<InstrumentKind> kind, Field field)
    {
        add (
            registry, id, description, { value, channelTarget },
            [&session, kind, field, name = value.name] (const CommandArgs& args)
            {
                const auto target = channelArg (session, args, kind);
                if (target.index < 0)
                    return CommandOutcome::fail (target.error);
                auto& atomic = field (session.channel (target.index));
                return withTargets (setUndoable (atomic, args[name]), args, -1, target.index);
            },
            true, true);
    }

    std::optional<InstrumentKind> instrumentFromName (const juce::String& name)
    {
        if (name == "acid" || name == "303")
            return InstrumentKind::acid;
        if (name == "sampler" || name == "drum" || name == "drums")
            return InstrumentKind::sampler;
        return std::nullopt;
    }

    juce::String limitName (const juce::var& value)
    {
        return value.toString().trim().substring (0, 40);
    }

    /** "builtin:kick" stays as it is; anything else is a file, made absolute against the working folder. */
    juce::String sampleOrigin (const juce::String& text)
    {
        const auto trimmed = text.trim().unquoted();
        if (trimmed.startsWith (SampleLibrary::builtinPrefix))
            return trimmed;
        return juce::File::getCurrentWorkingDirectory().getChildFile (trimmed).getFullPathName();
    }

    juce::String warningsNote (const Session& session)
    {
        const auto& warnings = session.project.warnings;
        if (warnings.isEmpty())
            return {};
        return "opened with " + juce::String (warnings.size()) +
               (warnings.size() == 1 ? " warning:\n" : " warnings:\n") + warnings.joinIntoString ("\n");
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
        registry, "pattern.length", "Set how many steps a pattern plays (1-64)",
        { { "steps", Type::integer, 1, Pattern::maxSteps, "number of steps" }, patternTarget },
        [&session] (const CommandArgs& args)
        {
            const auto index = patternArg (session, args);
            const auto before = session.pattern (index).getLength();
            session.pattern (index).setLength (static_cast<int> (args["steps"]));
            return withTargets (
                CommandOutcome::ok ([&session, index, before] { session.pattern (index).setLength (before); }), args,
                index, -1);
        },
        true, true);
    add (
        registry, "pattern.rename", "Rename a pattern", { { "name", Type::text, 0, 0, "new name" }, patternTarget },
        [&session] (const CommandArgs& args)
        {
            const auto name = limitName (args["name"]);
            if (name.isEmpty())
                return CommandOutcome::fail ("a pattern needs a name");
            const auto index = patternArg (session, args);
            auto& slot = session.project.patternNames[static_cast<size_t> (index)];
            const auto before = slot;
            slot = name;
            return withTargets (CommandOutcome::ok ([&slot, before] { slot = before; }), args, index, -1);
        },
        true);
    add (
        registry, "pattern.clear", "Turn every step of a pattern off, in every channel", { patternTarget },
        [&session] (const CommandArgs& args)
        {
            const auto index = patternArg (session, args);
            auto& pattern = session.pattern (index);
            std::array<std::array<Step, Pattern::maxSteps>, Session::maxChannels> before;
            for (int c = 0; c < Session::maxChannels; ++c)
                for (int s = 0; s < Pattern::maxSteps; ++s)
                {
                    auto step = pattern.lane (c).getStep (s);
                    before[static_cast<size_t> (c)][static_cast<size_t> (s)] = step;
                    step.on = false;
                    pattern.lane (c).setStep (s, step);
                }
            return withTargets (CommandOutcome::ok (
                                    [&pattern, before]
                                    {
                                        for (int c = 0; c < Session::maxChannels; ++c)
                                            for (int s = 0; s < Pattern::maxSteps; ++s)
                                                pattern.lane (c).setStep (
                                                    s, before[static_cast<size_t> (c)][static_cast<size_t> (s)]);
                                    }),
                                args, index, -1);
        },
        true);

    // --- steps (undoable) -------------------------------------------------------------------------
    add (
        registry, "pattern.step.toggle", "Turn a step on or off", { stepParam, patternTarget, channelTarget },
        [&session] (const CommandArgs& args) { return editStep (session, args, [] (Step& s) { s.on = ! s.on; }); },
        true);
    add (
        registry, "pattern.step.accent", "Set the accent of a step (turns the step on)",
        { stepParam, onParam, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const bool on = args["on"];
            return editStep (session, args,
                             [on] (Step& s)
                             {
                                 s.accent = on;
                                 s.on = s.on || on;
                             });
        },
        true);
    add (
        registry, "pattern.step.slide", "Set the slide of a step into the next one (turns the step on)",
        { stepParam, onParam, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const bool on = args["on"];
            return editStep (session, args,
                             [on] (Step& s)
                             {
                                 s.slide = on;
                                 s.on = s.on || on;
                             });
        },
        true);
    add (
        registry, "pattern.step.tie", "Make a step hold the previous note instead of playing a new one (turns it on)",
        { stepParam, onParam, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const bool on = args["on"];
            return editStep (session, args,
                             [on] (Step& s)
                             {
                                 s.tie = on;
                                 s.on = s.on || on;
                             });
        },
        true);
    add (
        registry, "pattern.step.note", "Set the pitch of a step (for a sampler, C4 is the sample's own pitch)",
        { stepParam,
          { "note", Type::integer, Step::minNote, Step::maxNote, "MIDI note number" },
          patternTarget,
          channelTarget },
        [&session] (const CommandArgs& args)
        {
            const int note = args["note"];
            return editStep (session, args, [note] (Step& s) { s.note = note; });
        },
        true);

    // --- channels ---------------------------------------------------------------------------------
    add (
        registry, "channel.select", "Choose the channel the steps, knobs and commands act on",
        { { "channel", Type::integer, 1, Session::maxChannels, "channel number" } },
        [&session] (const CommandArgs& args)
        {
            const auto index = static_cast<int> (args["channel"]) - 1;
            if (index >= session.channelCount())
                return CommandOutcome::fail ("there is no channel " + juce::String (index + 1) + " (the project has " +
                                             juce::String (session.channelCount()) + ")");
            session.selectedChannel.store (index);
            return CommandOutcome::ok();
        },
        false);
    add (
        registry, "channel.add", "Add a channel at the end and select it: acid or sampler",
        { { "instrument", Type::text, 0, 0, "acid or sampler" },
          { "name", Type::text, 0, 0, "name of the channel (default: the instrument)", true } },
        [&session] (const CommandArgs& args)
        {
            const auto instrument = args["instrument"].toString().trim().toLowerCase();
            const auto kind = instrumentFromName (instrument);
            if (! kind)
                return CommandOutcome::fail ("unknown instrument '" + instrument + "': use acid or sampler");
            auto name = args.contains ("name") ? limitName (args["name"]) : juce::String();
            if (name.isEmpty())
                name = instrumentName (*kind);

            const auto selectedBefore = session.selectedChannel.load();
            const auto index = session.addChannel (*kind, name);
            if (index < 0)
                return CommandOutcome::fail ("all " + juce::String (Session::maxChannels) + " channels are in use");
            session.selectedChannel.store (index);
            return CommandOutcome::ok (
                [&session, index, selectedBefore]
                {
                    session.removeChannel (index);
                    session.selectedChannel.store (selectedBefore);
                });
        },
        true);
    add (
        registry, "channel.remove", "Remove a channel and its steps in every pattern", { channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = channelArg (session, args);
            if (target.index < 0)
                return CommandOutcome::fail (target.error);
            if (session.channelCount() == 1)
                return CommandOutcome::fail ("a project needs at least one channel");

            const auto data = session.captureChannel (target.index);
            const auto selectedBefore = session.selectedChannel.load();
            session.removeChannel (target.index);
            session.collectSampleGarbage();
            return withTargets (CommandOutcome::ok (
                                    [&session, index = target.index, data, selectedBefore]
                                    {
                                        session.insertChannel (index, data);
                                        session.selectedChannel.store (selectedBefore);
                                    }),
                                args, -1, target.index);
        },
        true);
    add (
        registry, "channel.rename", "Rename a channel", { { "name", Type::text, 0, 0, "new name" }, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = channelArg (session, args);
            if (target.index < 0)
                return CommandOutcome::fail (target.error);
            const auto name = limitName (args["name"]);
            if (name.isEmpty())
                return CommandOutcome::fail ("a channel needs a name");
            auto& slot = session.project.channelNames[static_cast<size_t> (target.index)];
            const auto before = slot;
            slot = name;
            return withTargets (CommandOutcome::ok ([&slot, before] { slot = before; }), args, -1, target.index);
        },
        true);

    // --- acid synth -------------------------------------------------------------------------------
    constexpr auto acid = InstrumentKind::acid;
    addChannelControl (registry, session, "synth.cutoff", "Filter cutoff", unitParam, acid,
                       [] (Channel& c) -> auto& { return c.acid.cutoff; });
    addChannelControl (registry, session, "synth.resonance", "Filter resonance", unitParam, acid,
                       [] (Channel& c) -> auto& { return c.acid.resonance; });
    addChannelControl (registry, session, "synth.envmod", "How far the envelope opens the filter", unitParam, acid,
                       [] (Channel& c) -> auto& { return c.acid.envMod; });
    addChannelControl (registry, session, "synth.decay", "How fast the filter envelope closes", unitParam, acid,
                       [] (Channel& c) -> auto& { return c.acid.decay; });
    addChannelControl (registry, session, "synth.accent", "How hard accented steps hit", unitParam, acid,
                       [] (Channel& c) -> auto& { return c.acid.accent; });
    addChannelControl (registry, session, "synth.drive", "Distortion after the filter, the acid pedal", unitParam, acid,
                       [] (Channel& c) -> auto& { return c.acid.drive; });
    add (
        registry, "synth.waveform", "Choose the oscillator: saw or square",
        { { "square", Type::boolean, 0, 1, "true = square, false = saw" }, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = channelArg (session, args, InstrumentKind::acid);
            if (target.index < 0)
                return CommandOutcome::fail (target.error);
            return withTargets (
                setUndoable (session.channel (target.index).acid.square, static_cast<bool> (args["square"])), args, -1,
                target.index);
        },
        true);

    // --- sampler ----------------------------------------------------------------------------------
    constexpr auto sampler = InstrumentKind::sampler;
    addChannelControl (registry, session, "sampler.tune", "Tune the sample, in semitones",
                       { "semitones", Type::number, SamplerSettings::minTune, SamplerSettings::maxTune, "-24..24" },
                       sampler, [] (Channel& c) -> auto& { return c.sampler.tune; });
    addChannelControl (registry, session, "sampler.decay", "Shorten the sound's tail (1 = the whole sample)", unitParam,
                       sampler, [] (Channel& c) -> auto& { return c.sampler.decay; });
    addChannelControl (registry, session, "sampler.accent", "How much louder accented steps hit", unitParam, sampler,
                       [] (Channel& c) -> auto& { return c.sampler.accent; });
    add (
        registry, "sampler.load", "Load a sound: builtin:kick, builtin:hat, builtin:openhat, builtin:clap, or a file",
        { { "sample", Type::text, 0, 0, "builtin:<name>, or the path of a WAV, AIFF, FLAC or Ogg file" },
          channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = channelArg (session, args, InstrumentKind::sampler);
            if (target.index < 0)
                return CommandOutcome::fail (target.error);

            const auto origin = sampleOrigin (args["sample"].toString());
            if (const auto loaded = session.samples.get (origin); loaded.sample == nullptr)
                return CommandOutcome::fail (loaded.error);

            const auto index = target.index;
            const auto before = session.project.channelSamples[static_cast<size_t> (index)];
            session.setChannelSample (index, origin);
            session.collectSampleGarbage();
            auto outcome = CommandOutcome::ok ([&session, index, before] { session.setChannelSample (index, before); });
            auto resolved = withTargets (std::move (outcome), args, -1, index);
            resolved.resolvedArgs.set ("sample", origin); // absolute: redo must not depend on the folder
            return resolved;
        },
        true);

    // --- mixer ------------------------------------------------------------------------------------
    addChannelControl (registry, session, "mixer.volume", "Set a channel's volume in dB (-60 = silent)",
                       { "db", Type::number, MixerSettings::minVolumeDb, MixerSettings::maxVolumeDb, "-60..6 dB" },
                       std::nullopt, [] (Channel& c) -> auto& { return c.mixer.volumeDb; });
    addChannelControl (registry, session, "mixer.pan", "Set a channel's position: -1 left, 0 centre, 1 right",
                       { "pan", Type::number, -1, 1, "-1..1" }, std::nullopt,
                       [] (Channel& c) -> auto& { return c.mixer.pan; });
    add (
        registry, "mixer.mute", "Mute or unmute a channel", { onParam, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = channelArg (session, args);
            if (target.index < 0)
                return CommandOutcome::fail (target.error);
            return withTargets (setUndoable (session.channel (target.index).mixer.mute, static_cast<bool> (args["on"])),
                                args, -1, target.index);
        },
        true);
    add (
        registry, "mixer.solo", "Solo a channel: while any channel is soloed, only soloed ones play",
        { onParam, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = channelArg (session, args);
            if (target.index < 0)
                return CommandOutcome::fail (target.error);
            return withTargets (setUndoable (session.channel (target.index).mixer.solo, static_cast<bool> (args["on"])),
                                args, -1, target.index);
        },
        true);
    add (
        registry, "mixer.master", "Set the master volume in dB",
        { { "db", Type::number, MixerSettings::minVolumeDb, MixerSettings::maxVolumeDb, "-60..6 dB" } },
        [&session] (const CommandArgs& args)
        { return setUndoable (session.master.volumeDb, static_cast<double> (args["db"])); }, true, true);

    // --- project --------------------------------------------------------------------------------
    add (
        registry, "project.new", "Start a new project (does not ask about unsaved changes)", {},
        [&session, &registry] (const CommandArgs&)
        {
            session.loadDefaultProject();
            session.collectSampleGarbage();
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
            session.collectSampleGarbage();
            registry.clearHistory();
            auto outcome = CommandOutcome::ok();
            outcome.note = warningsNote (session);
            return outcome;
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
        registry, "edit.undo", "Undo the last edit", {}, [&registry] (const CommandArgs&)
        { return registry.undo() ? CommandOutcome::ok() : CommandOutcome::fail ("nothing to undo"); }, false);
    add (
        registry, "edit.redo", "Redo the last undone edit", {}, [&registry] (const CommandArgs&)
        { return registry.redo() ? CommandOutcome::ok() : CommandOutcome::fail ("nothing to redo"); }, false);
}
} // namespace tekno::core
