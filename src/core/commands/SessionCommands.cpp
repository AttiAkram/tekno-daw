// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/commands/SessionCommands.h"

#include <algorithm>
#include <array>
#include <optional>

#include "core/io/DemoProject.h"
#include "core/io/MidiImport.h"
#include "core/io/ProjectFile.h"
#include "core/model/NoteNames.h"

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
                             (kind == InstrumentKind::sampler ? "a sampler"
                              : kind == InstrumentKind::sub   ? "a sub bass"
                                                              : "an acid synth") +
                             ": this command is for " + instrumentName (*required) + " channels" };
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

    /** Changes one step (through the step view of the lane) and returns how to put the lane back. */
    CommandOutcome editStep (Session& session, const CommandArgs& args, const std::function<void (Step&)>& change,
                             bool stepMustBeOn = false)
    {
        const auto target = channelArg (session, args);
        if (target.index < 0)
            return CommandOutcome::fail (target.error);
        const auto patternIndex = patternArg (session, args);
        const auto channel = target.index;
        const auto index = stepArg (args);

        auto& pattern = session.pattern (patternIndex);
        const auto current = pattern.getStep (channel, index);
        if (stepMustBeOn && ! current.on)
            return CommandOutcome::fail ("step " + juce::String (index + 1) + " is off: turn it on first");

        auto after = current;
        change (after);
        // A step turned on starts at the instrument's own pitch: A1 for the sub, A2 for acid, C4 (the
        // sample as it is) for drums.
        if (! current.on && after.on)
            after.note = defaultNoteFor (session.channel (channel).kind.load());

        const auto before = pattern.lane (channel).notes();
        pattern.setStep (channel, index, after);
        return withTargets (CommandOutcome::ok ([&session, patternIndex, channel, before]
                                                { session.pattern (patternIndex).lane (channel).setNotes (before); }),
                            args, patternIndex, channel);
    }

    /** Changes the notes of one lane; `change` returns an error or an empty string. Undo puts the lane back. */
    CommandOutcome editLane (Session& session, const CommandArgs& args,
                             const std::function<juce::String (std::vector<Note>&)>& change)
    {
        const auto target = channelArg (session, args);
        if (target.index < 0)
            return CommandOutcome::fail (target.error);
        const auto patternIndex = patternArg (session, args);
        const auto channel = target.index;
        auto& lane = session.pattern (patternIndex).lane (channel);

        const auto before = lane.notes();
        auto after = before;
        if (const auto error = change (after); error.isNotEmpty())
            return CommandOutcome::fail (error);
        std::sort (after.begin(), after.end(), earlier);
        if (! lane.setNotes (after))
            return CommandOutcome::fail ("a lane holds at most " + juce::String (Lane::maxNotes) + " notes");
        return withTargets (CommandOutcome::ok ([&session, patternIndex, channel, before]
                                                { session.pattern (patternIndex).lane (channel).setNotes (before); }),
                            args, patternIndex, channel);
    }

    /** The note at `at` with pitch `note` (the first one, if two are identical), or end(). */
    std::vector<Note>::iterator findNote (std::vector<Note>& notes, const CommandArgs& args)
    {
        const int at = args["at"];
        const int pitch = args["note"];
        return std::find_if (notes.begin(), notes.end(),
                             [at, pitch] (const Note& n) { return n.start == at && n.pitch == pitch; });
    }

    /** "48:A2 72:Do3" (position in ticks : pitch, by name or number), spaces or commas between. */
    std::optional<std::vector<std::pair<int, int>>> parseNoteList (const juce::String& text, juce::String& error)
    {
        std::vector<std::pair<int, int>> list;
        auto tokens = juce::StringArray::fromTokens (text.replaceCharacter (',', ' '), " ", "\"");
        tokens.removeEmptyStrings();
        for (const auto& token : tokens)
        {
            const auto at = token.upToFirstOccurrenceOf (":", false, false);
            const auto pitchText = token.fromFirstOccurrenceOf (":", false, false);
            auto pitch = parseNoteName (pitchText);
            if (! pitch && pitchText.isNotEmpty() && pitchText.containsOnly ("0123456789"))
                pitch = pitchText.getIntValue();
            if (! token.containsChar (':') || at.isEmpty() || ! at.containsOnly ("0123456789") || ! pitch)
            {
                error = "'" + token + "' is not a note: write position:pitch, like 48:A2";
                return std::nullopt;
            }
            list.push_back ({ at.getIntValue(), *pitch });
        }
        if (list.empty())
        {
            error = "no notes given: write them as position:pitch, like \"0:A2 48:C3\"";
            return std::nullopt;
        }
        return list;
    }

    /** Applies `change` to every listed note of the lane; an error names the first one missing. */
    juce::String forListedNotes (std::vector<Note>& notes, const juce::String& listText,
                                 const std::function<void (Note&)>& change)
    {
        juce::String error;
        const auto list = parseNoteList (listText, error);
        if (! list)
            return error;
        std::vector<size_t> found;
        for (const auto& [at, pitch] : *list)
        {
            auto index = notes.size();
            for (size_t i = 0; i < notes.size(); ++i)
                if (notes[i].start == at && notes[i].pitch == pitch &&
                    std::find (found.begin(), found.end(), i) == found.end())
                {
                    index = i;
                    break;
                }
            if (index == notes.size())
                return "there is no " + noteName (pitch) + " starting at tick " + juce::String (at);
            found.push_back (index);
        }
        for (const auto i : found)
            change (notes[i]);
        return {};
    }

    juce::String noSuchNote (const CommandArgs& args)
    {
        return "there is no " + noteName (static_cast<int> (args["note"])) + " starting at tick " +
               juce::String (static_cast<int> (args["at"]));
    }

    /** The insert chain a command acts on: master=on, or a channel (the selected one by default). */
    struct ChainTarget
    {
        EffectChain* chain = nullptr;
        int channel = -1; // -1 = the master
        juce::String label, error;
    };

    ChainTarget chainArg (Session& session, const CommandArgs& args)
    {
        if (args.contains ("master") && static_cast<bool> (args["master"]))
            return { &session.master.effects, -1, "master", {} };
        const auto target = channelArg (session, args);
        if (target.index < 0)
            return { nullptr, -1, {}, target.error };
        return { &session.channel (target.index).effects,
                 target.index,
                 session.project.channelNames[static_cast<size_t> (target.index)],
                 {} };
    }

    /** Changes a chain; `change` returns an error or an empty string, and may leave a note. Undo puts the
        whole chain back. */
    CommandOutcome editChain (Session& session, const CommandArgs& args,
                              const std::function<juce::String (EffectChainSettings&, juce::String&)>& change)
    {
        const auto target = chainArg (session, args);
        if (target.chain == nullptr)
            return CommandOutcome::fail (target.error);
        const auto before = target.chain->load();
        auto after = before;
        juce::String note;
        if (const auto problem = change (after, note); problem.isNotEmpty())
            return CommandOutcome::fail (problem);
        target.chain->store (after);
        const auto channel = target.channel;
        auto outcome = CommandOutcome::ok (
            [&session, channel, before]
            { (channel < 0 ? session.master.effects : session.channel (channel).effects).store (before); });
        outcome.note = note.isEmpty() ? juce::String() : note + " of " + target.label;
        return channel < 0 ? outcome : withTargets (std::move (outcome), args, -1, channel);
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
        if (name == "sub" || name == "bass")
            return InstrumentKind::sub;
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
        registry, "pattern.clear", "Remove every note of a pattern, in every channel", { patternTarget },
        [&session] (const CommandArgs& args)
        {
            const auto index = patternArg (session, args);
            auto& pattern = session.pattern (index);
            std::array<std::vector<Note>, Session::maxChannels> before;
            for (int c = 0; c < Session::maxChannels; ++c)
            {
                before[static_cast<size_t> (c)] = pattern.lane (c).notes();
                pattern.lane (c).clear();
            }
            return withTargets (CommandOutcome::ok (
                                    [&pattern, before]
                                    {
                                        for (int c = 0; c < Session::maxChannels; ++c)
                                            pattern.lane (c).setNotes (before[static_cast<size_t> (c)]);
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
          { "note", Type::integer, Step::minNote, Step::maxNote, "pitch: A2 or La2, or 45" },
          patternTarget,
          channelTarget },
        [&session] (const CommandArgs& args)
        {
            const int note = args["note"];
            return editStep (session, args, [note] (Step& s) { s.note = note; }, true);
        },
        true);

    // --- notes (the piano roll; ticks, 24 per step) ------------------------------------------------
    constexpr auto lastTick = Pattern::maxSteps * Note::ticksPerStep;
    const CommandParam atParam { "at", Type::integer, 0, lastTick - 1, "position in ticks, 24 per step" };
    const CommandParam pitchParam { "note", Type::integer, Step::minNote, Step::maxNote,
                                    "pitch: A2 or La2, C#3 or Do#3, or 45" };
    const CommandParam lengthParam { "length", Type::integer, 1, lastTick, "length in ticks, 24 per step" };

    add (
        registry, "note.add", "Add a note to a lane",
        { atParam,
          pitchParam,
          lengthParam,
          { "accent", Type::boolean, 0, 1, "true or false", true },
          { "slide", Type::boolean, 0, 1, "true or false", true },
          { "velocity", Type::integer, 0, 127, "loudness 0-127 (default 100)", true },
          patternTarget,
          channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (session, args,
                             [&args] (std::vector<Note>& notes)
                             {
                                 notes.push_back ({ static_cast<int> (args["at"]), static_cast<int> (args["length"]),
                                                    static_cast<int> (args["note"]),
                                                    args.contains ("accent") && static_cast<bool> (args["accent"]),
                                                    args.contains ("slide") && static_cast<bool> (args["slide"]),
                                                    args.contains ("velocity") ? static_cast<int> (args["velocity"])
                                                                               : Note::defaultVelocity });
                                 return juce::String();
                             });
        },
        true);
    add (
        registry, "note.remove", "Remove a note", { atParam, pitchParam, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (session, args,
                             [&args] (std::vector<Note>& notes)
                             {
                                 const auto it = findNote (notes, args);
                                 if (it == notes.end())
                                     return noSuchNote (args);
                                 notes.erase (it);
                                 return juce::String();
                             });
        },
        true);
    add (
        registry, "note.move", "Move a note in time and pitch",
        { atParam,
          pitchParam,
          { "toAt", Type::integer, 0, lastTick - 1, "new position in ticks" },
          { "toNote", Type::integer, Step::minNote, Step::maxNote, "new pitch" },
          patternTarget,
          channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (session, args,
                             [&args] (std::vector<Note>& notes)
                             {
                                 const auto it = findNote (notes, args);
                                 if (it == notes.end())
                                     return noSuchNote (args);
                                 it->start = args["toAt"];
                                 it->pitch = args["toNote"];
                                 return juce::String();
                             });
        },
        true);
    add (
        registry, "note.resize", "Change how long a note is",
        { atParam, pitchParam, lengthParam, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (session, args,
                             [&args] (std::vector<Note>& notes)
                             {
                                 const auto it = findNote (notes, args);
                                 if (it == notes.end())
                                     return noSuchNote (args);
                                 it->length = args["length"];
                                 return juce::String();
                             });
        },
        true);
    for (const auto isAccent : { true, false })
        add (
            registry, isAccent ? "note.accent" : "note.slide",
            isAccent ? "Set a note's accent" : "Set a note's slide into the next one",
            { atParam, pitchParam, onParam, patternTarget, channelTarget },
            [&session, isAccent] (const CommandArgs& args)
            {
                return editLane (session, args,
                                 [&args, isAccent] (std::vector<Note>& notes)
                                 {
                                     const auto it = findNote (notes, args);
                                     if (it == notes.end())
                                         return noSuchNote (args);
                                     (isAccent ? it->accent : it->slide) = static_cast<bool> (args["on"]);
                                     return juce::String();
                                 });
            },
            true);

    const CommandParam velocityParam { "velocity", Type::integer, 0, 127, "loudness 0-127, 100 = as it is" };
    add (
        registry, "note.velocity", "Set how loud a note is",
        { atParam, pitchParam, velocityParam, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (session, args,
                             [&args] (std::vector<Note>& notes)
                             {
                                 const auto it = findNote (notes, args);
                                 if (it == notes.end())
                                     return noSuchNote (args);
                                 it->velocity = args["velocity"];
                                 return juce::String();
                             });
        },
        true);

    // Several notes at once (a selection in the piano roll), listed as "48:A2 72:C3".
    const CommandParam listParam { "notes", Type::text, 0, 0, "the notes, as position:pitch, e.g. \"0:A2 48:C3\"" };
    add (
        registry, "notes.move", "Move several notes together, in time (ticks) and pitch (semitones)",
        { listParam,
          { "by", Type::integer, -lastTick, lastTick, "ticks, negative = earlier" },
          { "semitones", Type::integer, -72, 72, "negative = down" },
          patternTarget,
          channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (
                session, args,
                [&args] (std::vector<Note>& notes)
                {
                    const int by = args["by"];
                    const int semitones = args["semitones"];
                    const auto error = forListedNotes (notes, args["notes"].toString(),
                                                       [by, semitones] (Note& n)
                                                       {
                                                           n.start += by;
                                                           n.pitch += semitones;
                                                       });
                    if (error.isNotEmpty())
                        return error;
                    for (const auto& n : notes)
                        if (n.start < 0 || n.start >= lastTick || n.pitch < Step::minNote || n.pitch > Step::maxNote)
                            return juce::String ("that move takes a note outside the pattern or the range C1-C7");
                    return juce::String();
                });
        },
        true);
    add (
        registry, "notes.remove", "Remove several notes", { listParam, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (session, args,
                             [&args] (std::vector<Note>& notes)
                             {
                                 constexpr auto removed = -1;
                                 const auto error = forListedNotes (notes, args["notes"].toString(),
                                                                    [] (Note& n) { n.start = removed; });
                                 std::erase_if (notes, [] (const Note& n) { return n.start == removed; });
                                 return error;
                             });
        },
        true);
    add (
        registry, "notes.velocity", "Set the loudness of several notes",
        { listParam, velocityParam, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (session, args,
                             [&args] (std::vector<Note>& notes)
                             {
                                 const int velocity = args["velocity"];
                                 return forListedNotes (notes, args["notes"].toString(),
                                                        [velocity] (Note& n) { n.velocity = velocity; });
                             });
        },
        true);

    add (
        registry, "note.preview", "Play a note for a moment on a channel, to hear it (changes nothing)",
        { pitchParam, { "velocity", Type::integer, 0, 127, "loudness 0-127 (default 100)", true }, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = channelArg (session, args);
            if (target.index < 0)
                return CommandOutcome::fail (target.error);
            session.requestPreview (target.index, args["note"],
                                    args.contains ("velocity") ? static_cast<int> (args["velocity"])
                                                               : Note::defaultVelocity);
            return CommandOutcome::ok();
        },
        false);
    add (
        registry, "key.set", "Choose the project's key for the scale guide: \"La minore\", \"A phrygian\", or off",
        { { "key", Type::text, 0, 0,
            "root and scale (major, minor, phrygian, dorian, harmonic, pentatonic), or off" } },
        [&session] (const CommandArgs& args)
        {
            auto words = juce::StringArray::fromTokens (args["key"].toString(), " ", "");
            words.removeEmptyStrings();
            Key key;
            if (words.isEmpty() || words[0].equalsIgnoreCase ("off"))
                key.scale = Scale::none;
            else
            {
                const auto root = parseRoot (words[0]);
                const auto scale = parseScale (words.size() > 1 ? words[1] : juce::String ("minor"));
                if (! root)
                    return CommandOutcome::fail ("'" + words[0] + "' is not a note: try La, A, Do#, F#");
                if (! scale)
                    return CommandOutcome::fail (
                        "'" + words[1] + "' is not a scale: major, minor, phrygian, dorian, harmonic, pentatonic");
                key = { *root, *scale };
            }
            const auto before = session.project.key;
            session.project.key = key;
            return CommandOutcome::ok ([&session, before] { session.project.key = before; });
        },
        true);

    add (
        registry, "notes.add", "Add several notes at once: \"at:note:length[:velocity[:as]]\" (a = accent, s = slide)",
        { { "notes", Type::text, 0, 0, "e.g. \"0:A2:24 24:C3:24:90:a\"" }, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (
                session, args,
                [&args] (std::vector<Note>& notes)
                {
                    auto tokens =
                        juce::StringArray::fromTokens (args["notes"].toString().replaceCharacter (',', ' '), " ", "\"");
                    tokens.removeEmptyStrings();
                    if (tokens.isEmpty())
                        return juce::String ("no notes given: write at:note:length, like 0:A2:24");
                    for (const auto& token : tokens)
                    {
                        const auto parts = juce::StringArray::fromTokens (token, ":", "");
                        const auto pitch = parts.size() >= 3 ? parseNoteName (parts[1]) : std::nullopt;
                        const auto at = parts[0].getIntValue(), length = parts[2].getIntValue();
                        const auto velocity = parts.size() >= 4 ? parts[3].getIntValue() : Note::defaultVelocity;
                        const auto flags = parts.size() >= 5 ? parts[4].toLowerCase() : juce::String();
                        const auto number = parts.size() >= 3 && parts[1].containsOnly ("0123456789")
                                                ? std::optional<int> (parts[1].getIntValue())
                                                : pitch;
                        if (parts.size() < 3 || ! number || ! parts[0].containsOnly ("0123456789") ||
                            ! parts[2].containsOnly ("0123456789") || at >= lastTick || length < 1 ||
                            *number < Step::minNote || *number > Step::maxNote || velocity < 0 || velocity > 127)
                            return "'" + token + "' is not a note: write at:note:length[:velocity[:as]]";
                        notes.push_back ({ at, length > lastTick ? static_cast<int> (lastTick) : length, *number,
                                           flags.containsChar ('a'), flags.containsChar ('s'), velocity });
                    }
                    return juce::String();
                });
        },
        true);
    add (
        registry, "lane.repeat", "Copy the first bars of a lane to the end of the pattern",
        { { "bars", Type::integer, 1, 3, "how many bars to repeat (default 1)", true }, patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto lengthInTicks = session.pattern (patternArg (session, args)).lengthInTicks();
            return editLane (
                session, args,
                [&args, lengthInTicks] (std::vector<Note>& notes)
                {
                    constexpr auto bar = Pattern::defaultLength * Note::ticksPerStep;
                    const auto block = (args.contains ("bars") ? static_cast<int> (args["bars"]) : 1) * bar;
                    if (block >= lengthInTicks)
                        return juce::String ("the pattern is not longer than that: nothing to repeat into");
                    std::vector<Note> first;
                    for (const auto& n : notes)
                        if (n.start < block)
                            first.push_back (n);
                    notes = first;
                    for (auto offset = block; offset < lengthInTicks; offset += block)
                        for (auto n : first)
                            if (n.start + offset < lengthInTicks)
                            {
                                n.start += offset;
                                notes.push_back (n);
                            }
                    return juce::String();
                });
        },
        true);
    add (
        registry, "lane.clear", "Remove every note of a lane", { patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            return editLane (session, args,
                             [] (std::vector<Note>& notes)
                             {
                                 notes.clear();
                                 return juce::String();
                             });
        },
        true);
    add (
        registry, "lane.toscale", "Move every note outside the project's key to the nearest note of the scale",
        { patternTarget, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto key = session.project.key;
            if (! key.active())
                return CommandOutcome::fail ("the project has no key: choose one first, e.g. key La minore");
            return editLane (session, args,
                             [key] (std::vector<Note>& notes)
                             {
                                 for (auto& n : notes)
                                     n.pitch =
                                         std::clamp (key.nearestInScale (n.pitch), static_cast<int> (Step::minNote),
                                                     static_cast<int> (Step::maxNote));
                                 return juce::String();
                             });
        },
        true);

    add (
        registry, "midi.import", "Put a melody from a MIDI file (.mid) into a lane, replacing its notes",
        { { "path", Type::text, 0, 0, "path of a .mid file" },
          { "track", Type::integer, 1, 128, "which track (default: the first with notes)", true },
          patternTarget,
          channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = channelArg (session, args);
            if (target.index < 0)
                return CommandOutcome::fail (target.error);
            const auto file =
                juce::File::getCurrentWorkingDirectory().getChildFile (args["path"].toString().trim().unquoted());
            io::MidiMelody melody;
            const auto track = args.contains ("track") ? static_cast<int> (args["track"]) - 1 : -1;
            if (const auto result = io::readMidiMelody (file, track, melody); result.failed())
                return CommandOutcome::fail (result.getErrorMessage());

            const auto patternIndex = patternArg (session, args);
            auto& pattern = session.pattern (patternIndex);
            const auto channel = target.index;
            const auto notesBefore = pattern.lane (channel).notes();
            const auto lengthBefore = pattern.getLength();
            pattern.setLength (melody.lengthInSteps);
            pattern.lane (channel).setNotes (melody.notes);

            auto outcome = withTargets (CommandOutcome::ok (
                                            [&session, patternIndex, channel, notesBefore, lengthBefore]
                                            {
                                                auto& p = session.pattern (patternIndex);
                                                p.lane (channel).setNotes (notesBefore);
                                                p.setLength (lengthBefore);
                                            }),
                                        args, patternIndex, channel);
            outcome.resolvedArgs.set ("path", file.getFullPathName()); // redo must not depend on the folder
            outcome.note = melody.summary();
            return outcome;
        },
        true);

    // --- effects (inserts on a channel or on the master) ---------------------------------------
    const CommandParam masterTarget { "master", Type::boolean, 0, 1, "on = the master's inserts", true };
    const CommandParam slotParam { "slot", Type::integer, 1, effectSlots, "insert slot, 1-4" };

    add (
        registry, "fx.add", "Add an effect to a channel (or master=on): distortion, delay or reverb",
        { { "type", Type::text, 0, 0, "distortion, delay or reverb" },
          { "slot", Type::integer, 1, effectSlots, "which slot (default: the first free)", true },
          channelTarget,
          masterTarget },
        [&session] (const CommandArgs& args)
        {
            const auto type = effectFromName (args["type"].toString());
            if (! type)
                return CommandOutcome::fail ("unknown effect '" + args["type"].toString().trim() +
                                             "': use distortion, delay or reverb");
            // Each engine has a fixed pool of the big effects.
            const auto limit = *type == EffectType::delay ? maxDelays : *type == EffectType::reverb ? maxReverbs : 1000;
            auto inUse = 0;
            for (int c = 0; c <= session.channelCount(); ++c)
            {
                const auto& chain = c == session.channelCount() ? session.master.effects : session.channel (c).effects;
                for (const auto& slot : chain.slots)
                    inUse += slot.type.load() == *type ? 1 : 0;
            }
            if (inUse >= limit)
                return CommandOutcome::fail ("a project can have at most " + juce::String (limit) + " " +
                                             effectInfo (*type).name + "s");

            return editChain (session, args,
                              [&args, type] (EffectChainSettings& chain, juce::String& note)
                              {
                                  auto index = -1;
                                  if (args.contains ("slot"))
                                  {
                                      index = static_cast<int> (args["slot"]) - 1;
                                      if (chain[static_cast<size_t> (index)].type != EffectType::none)
                                          return "slot " + juce::String (index + 1) + " already has " +
                                                 effectInfo (chain[static_cast<size_t> (index)].type).name +
                                                 ": fx.remove it first, or pick another slot";
                                  }
                                  else
                                      for (int i = 0; i < effectSlots && index < 0; ++i)
                                          if (chain[static_cast<size_t> (i)].type == EffectType::none)
                                              index = i;
                                  if (index < 0)
                                      return juce::String ("all 4 slots are taken");
                                  chain[static_cast<size_t> (index)] = EffectSettings::defaults (*type);
                                  note =
                                      juce::String (effectInfo (*type).name) + " in slot " + juce::String (index + 1);
                                  return juce::String();
                              });
        },
        true);
    add (
        registry, "fx.remove", "Remove the effect in a slot", { slotParam, channelTarget, masterTarget },
        [&session] (const CommandArgs& args)
        {
            return editChain (session, args,
                              [&args] (EffectChainSettings& chain, juce::String&)
                              {
                                  auto& slot = chain[static_cast<size_t> (static_cast<int> (args["slot"]) - 1)];
                                  if (slot.type == EffectType::none)
                                      return juce::String ("that slot is empty");
                                  slot = {};
                                  return juce::String();
                              });
        },
        true);
    add (
        registry, "fx.set", "Set a parameter of an effect: fx.set 1 feedback 0.7, fx.set 1 mode fold",
        { slotParam,
          { "param", Type::text, 0, 0, "the parameter's name" },
          { "value", Type::text, 0, 0, "a number, or a choice's name" },
          channelTarget,
          masterTarget },
        [&session] (const CommandArgs& args)
        {
            return editChain (session, args,
                              [&args] (EffectChainSettings& chain, juce::String&)
                              {
                                  auto& slot = chain[static_cast<size_t> (static_cast<int> (args["slot"]) - 1)];
                                  if (slot.type == EffectType::none)
                                      return juce::String ("that slot is empty");
                                  const auto& info = effectInfo (slot.type);
                                  const auto index = info.paramIndex (args["param"].toString().trim());
                                  if (index < 0)
                                  {
                                      juce::StringArray names;
                                      for (const auto& p : info.params)
                                          names.add (p.name);
                                      return juce::String (info.name) + " has no '" + args["param"].toString().trim() +
                                             "': it has " + names.joinIntoString (", ");
                                  }
                                  const auto& param = info.params[static_cast<size_t> (index)];
                                  const auto text = args["value"].toString().trim();
                                  float value = 0.0f;
                                  auto choice = -1;
                                  for (size_t c = 0; c < param.choices.size(); ++c)
                                      if (text.equalsIgnoreCase (param.choices[c]))
                                          choice = static_cast<int> (c);
                                  if (choice >= 0)
                                      value = static_cast<float> (choice);
                                  else if (text.containsOnly ("-+.0123456789") && text.isNotEmpty())
                                      value = text.getFloatValue();
                                  else
                                      return "'" + text + "' is not a value for " + param.name;
                                  if (value < param.min || value > param.max)
                                      return juce::String (param.name) + " must be between " +
                                             juce::String (param.min) + " and " + juce::String (param.max);
                                  slot.values[static_cast<size_t> (index)] =
                                      param.isChoice() ? std::round (value) : value;
                                  return juce::String();
                              });
        },
        true, true);
    add (
        registry, "fx.curve", "Give a waveshaper a ready-made curve: soft, hard, fold, tube, rectify or clean",
        { slotParam,
          { "curve", Type::text, 0, 0, "soft, hard, fold, tube, rectify or clean" },
          channelTarget,
          masterTarget },
        [&session] (const CommandArgs& args)
        {
            return editChain (session, args,
                              [&args] (EffectChainSettings& chain, juce::String&)
                              {
                                  auto& slot = chain[static_cast<size_t> (static_cast<int> (args["slot"]) - 1)];
                                  if (slot.type != EffectType::shaper)
                                      return juce::String ("that slot is not a waveshaper");
                                  const auto name = args["curve"].toString().trim();
                                  const auto curve = shaperCurve (name);
                                  if (! curve)
                                  {
                                      juce::StringArray names;
                                      for (const auto& c : shaperCurves())
                                          names.add (c.name);
                                      return "no curve '" + name + "': there are " + names.joinIntoString (", ");
                                  }
                                  const auto first = effectInfo (EffectType::shaper).paramIndex ("p1");
                                  for (size_t k = 0; k < curve->size(); ++k)
                                      slot.values[static_cast<size_t> (first) + k] = (*curve)[k];
                                  return juce::String();
                              });
        },
        true);
    add (
        registry, "fx.bypass", "Switch an effect off without removing it",
        { slotParam, onParam, channelTarget, masterTarget },
        [&session] (const CommandArgs& args)
        {
            return editChain (session, args,
                              [&args] (EffectChainSettings& chain, juce::String&)
                              {
                                  auto& slot = chain[static_cast<size_t> (static_cast<int> (args["slot"]) - 1)];
                                  if (slot.type == EffectType::none)
                                      return juce::String ("that slot is empty");
                                  slot.bypass = args["on"];
                                  return juce::String();
                              });
        },
        true);
    add (
        registry, "fx.move", "Move an effect to another slot (the order is the order of the sound)",
        { slotParam, { "to", Type::integer, 1, effectSlots, "the new slot" }, channelTarget, masterTarget },
        [&session] (const CommandArgs& args)
        {
            return editChain (session, args,
                              [&args] (EffectChainSettings& chain, juce::String&)
                              {
                                  const auto from = static_cast<int> (args["slot"]) - 1,
                                             to = static_cast<int> (args["to"]) - 1;
                                  if (chain[static_cast<size_t> (from)].type == EffectType::none)
                                      return juce::String ("that slot is empty");
                                  const auto moving = chain[static_cast<size_t> (from)];
                                  std::vector<EffectSettings> order (chain.begin(), chain.end());
                                  order.erase (order.begin() + from);
                                  order.insert (order.begin() + to, moving);
                                  std::copy (order.begin(), order.end(), chain.begin());
                                  return juce::String();
                              });
        },
        true);
    add (
        registry, "fx.list", "Show the effects of a channel (or master=on)", { channelTarget, masterTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = chainArg (session, args);
            if (target.chain == nullptr)
                return CommandOutcome::fail (target.error);
            const auto& label = target.label;
            juce::StringArray lines;
            const auto settings = target.chain->load();
            for (size_t i = 0; i < settings.size(); ++i)
            {
                const auto& slot = settings[i];
                if (slot.type == EffectType::none)
                    continue;
                const auto& info = effectInfo (slot.type);
                juce::String line;
                line << (i + 1) << "  " << info.name << (slot.bypass ? " (bypassed)" : "") << ":";
                for (size_t p = 0; p < info.params.size(); ++p)
                {
                    const auto& param = info.params[p];
                    line
                        << " " << param.name << "="
                        << (param.isChoice()
                                ? juce::String (param.choices[static_cast<size_t> (slot.values[p])])
                                : juce::String (slot.values[p], 3).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."));
                }
                lines.add (line);
            }
            auto outcome = CommandOutcome::ok();
            outcome.note = lines.isEmpty() ? label + ": no effects" : label + "\n" + lines.joinIntoString ("\n");
            return outcome;
        },
        false);

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
    for (const auto next : { true, false })
    {
        add (
            registry, next ? "pattern.next" : "pattern.previous",
            next ? "Select the next pattern" : "Select the previous pattern", {},
            [&session, next] (const CommandArgs&)
            {
                const auto index = std::clamp (session.selected.load() + (next ? 1 : -1), 0, Session::maxPatterns - 1);
                session.selected.store (index);
                if (! session.transport.playing.load())
                    session.playing.store (index);
                return CommandOutcome::ok();
            },
            false);
        add (
            registry, next ? "channel.next" : "channel.previous",
            next ? "Select the next channel" : "Select the previous channel", {},
            [&session, next] (const CommandArgs&)
            {
                session.selectedChannel.store (std::clamp (session.selectedChannel.load() + (next ? 1 : -1), 0,
                                                           std::max (0, session.channelCount() - 1)));
                return CommandOutcome::ok();
            },
            false);
    }
    add (
        registry, "channel.add", "Add a channel at the end and select it: acid, sub or sampler",
        { { "instrument", Type::text, 0, 0, "acid, sub or sampler" },
          { "name", Type::text, 0, 0, "name of the channel (default: the instrument)", true },
          { "sample", Type::text, 0, 0, "for a sampler: builtin:<name> or a file, loaded in the same step", true } },
        [&session] (const CommandArgs& args)
        {
            const auto instrument = args["instrument"].toString().trim().toLowerCase();
            const auto kind = instrumentFromName (instrument);
            if (! kind)
                return CommandOutcome::fail ("unknown instrument '" + instrument + "': use acid, sub or sampler");
            auto name = args.contains ("name") ? limitName (args["name"]) : juce::String();
            if (name.isEmpty())
                name = instrumentName (*kind);
            juce::String origin;
            if (args.contains ("sample"))
            {
                if (*kind != InstrumentKind::sampler)
                    return CommandOutcome::fail ("only a sampler takes a sample");
                origin = sampleOrigin (args["sample"].toString());
                if (const auto loaded = session.samples.get (origin); loaded.sample == nullptr)
                    return CommandOutcome::fail (loaded.error);
            }

            const auto selectedBefore = session.selectedChannel.load();
            const auto index = session.addChannel (*kind, name);
            if (index < 0)
                return CommandOutcome::fail ("all " + juce::String (Session::maxChannels) + " channels are in use");
            if (origin.isNotEmpty())
                session.setChannelSample (index, origin);
            session.selectedChannel.store (index);
            auto outcome = CommandOutcome::ok (
                [&session, index, selectedBefore]
                {
                    session.removeChannel (index);
                    session.selectedChannel.store (selectedBefore);
                });
            if (origin.isNotEmpty()) // absolute: redo must not depend on the folder
            {
                outcome.resolvedArgs = args;
                outcome.resolvedArgs.set ("sample", origin);
            }
            return outcome;
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
    addChannelControl (registry, session, "synth.shape", "Morph the oscillator from saw (0) to square (1)", unitParam,
                       acid, [] (Channel& c) -> auto& { return c.acid.shape; });
    addChannelControl (registry, session, "synth.glide", "How long a slide takes, in seconds",
                       { "seconds", Type::number, AcidSettings::minGlide, AcidSettings::maxGlide, "0.005..0.5 s" },
                       acid, [] (Channel& c) -> auto& { return c.acid.glide; });
    add (
        registry, "synth.waveform", "Choose the oscillator: saw or square (synth.shape blends them)",
        { { "square", Type::boolean, 0, 1, "true = square, false = saw" }, channelTarget },
        [&session] (const CommandArgs& args)
        {
            const auto target = channelArg (session, args, InstrumentKind::acid);
            if (target.index < 0)
                return CommandOutcome::fail (target.error);
            return withTargets (setUndoable (session.channel (target.index).acid.shape,
                                             static_cast<bool> (args["square"]) ? 1.0f : 0.0f),
                                args, -1, target.index);
        },
        true);

    // --- sub bass ---------------------------------------------------------------------------------
    constexpr auto sub = InstrumentKind::sub;
    addChannelControl (registry, session, "sub.drive",
                       "Saturation: 0 is a pure sine, more adds the harmonics small speakers can play", unitParam, sub,
                       [] (Channel& c) -> auto& { return c.sub.drive; });
    addChannelControl (registry, session, "sub.punch", "How far above the note it starts, for the attack", unitParam,
                       sub, [] (Channel& c) -> auto& { return c.sub.punch; });
    addChannelControl (registry, session, "sub.decay", "How long a note lasts: 0 is a pluck, 1 holds", unitParam, sub,
                       [] (Channel& c) -> auto& { return c.sub.decay; });
    addChannelControl (registry, session, "sub.glide", "How long a slide takes, in seconds",
                       { "seconds", Type::number, AcidSettings::minGlide, AcidSettings::maxGlide, "0.005..0.5 s" }, sub,
                       [] (Channel& c) -> auto& { return c.sub.glide; });

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
        registry, "sampler.load",
        "Load a sound: builtin:kick, hat, openhat, clap, snare, tom, conga, rim or shaker, or a file",
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
        registry, "project.demo",
        "Open the demo: a freetekno loop set in four patterns (does not ask about unsaved changes)", {},
        [&session, &registry] (const CommandArgs&)
        {
            if (const auto result = io::project::fromText (io::demoProjectText(), session); result.failed())
                return CommandOutcome::fail ("the demo didn't open: " + result.getErrorMessage());
            session.project.file = juce::File();
            session.project.revision = session.project.savedRevision = 0;
            session.collectSampleGarbage();
            registry.clearHistory();
            auto outcome = CommandOutcome::ok();
            outcome.note = "demo: patterns 1 intro, 2 groove, 3 acid, 4 break; switch them while it plays";
            return outcome;
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
