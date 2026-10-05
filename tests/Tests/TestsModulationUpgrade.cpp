#include "TestSupport.h"
#include "ModulationGraph.h"
#include <thread>

namespace
{
// Graph source numbers: LFO 1..n, then ENV 1..n, then the macros. Named, so a
// source added to either group does not silently retarget these tests.
constexpr int kEnv1Source = PX3SynthAudioProcessor::kLfoSourceCount;
constexpr int kMacro1Source = PX3SynthAudioProcessor::kLfoSourceCount + PX3SynthAudioProcessor::kEnvelopeSourceCount;
}

// testModulationUpgrade - current 0.8 modulation/routing behavior and state schema.

namespace px3tests
{
namespace
{

constexpr int kRoutingSwitchBlock = 30;

void prepareUpgrade(PX3SynthAudioProcessor& processor)
{
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);
}

// Runs blocks of silence, the first carrying `first` if one is given.
void runUpgradeBlocks(PX3SynthAudioProcessor& processor, int count, juce::MidiBuffer* first = nullptr)
{
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    for (int block = 0; block < count; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock(buffer, (block == 0 && first != nullptr) ? *first : empty);
    }
}

juce::MidiBuffer upgradeNoteOn(int note)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
    return midi;
}

juce::MidiBuffer upgradeNoteOff(int note)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
    return midi;
}

double upgradeDb(double ratio)
{
    return 20.0 * std::log10(juce::jmax(1.0e-12, ratio));
}

int upgradeChoiceIndex(juce::AudioProcessor& processor, const juce::String& id)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(findParameter(processor, id)))
    {
        return choice->getIndex();
    }
    return -1;
}

//==============================================================================
// Envelopes
//==============================================================================
void testEnvelopeTimesUpgrade()
{
    // Every stage time reaches at least 30 s, on the amp envelope and all three
    // modulation envelopes.
    {
        PX3SynthAudioProcessor processor;
        juce::StringArray tooShort;
        auto shortest = 1.0e9f;

        for (const auto* prefix : { "voice.amp.", "mod.env1.", "mod.env2.", "mod.env3.", "mod.env4." })
        {
            for (const auto* stage : { "Attack", "Decay", "Release" })
            {
            const auto id = juce::String(prefix) + juce::String(stage).toLowerCase();
                auto* parameter = findParameter(processor, id);
                if (parameter == nullptr)
                {
                    tooShort.add(id + " missing");
                    continue;
                }

                const auto end = parameter->getNormalisableRange().end;
                shortest = juce::jmin(shortest, end);
                if (end < 30.0f)
                {
                    tooShort.add(id + " ends at " + fmt(end, 1) + " s");
                }
            }
        }

        check("EnvUpgrade_EveryStageTimeReachesThirtySeconds", tooShort.isEmpty(),
              tooShort.isEmpty() ? "12 stage times, shortest ceiling " + fmt(shortest, 1) + " s"
                                 : tooShort.joinIntoString(", "));
    }

    // The documented defaults.
    {
        PX3SynthAudioProcessor processor;
        juce::StringArray wrong;
        const auto expect = [&](const juce::String& id, float want)
        {
            const auto got = getParamValue(processor, id);
            if (std::abs(got - want) > 1.5e-3f)
            {
                wrong.add(id + " " + fmt(got, 4) + " (want " + fmt(want, 4) + ")");
            }
        };

        expect("voice.amp.attack", 0.015f);
        expect("voice.amp.decay", 0.300f);
        expect("voice.amp.sustain", 0.8f);
        expect("voice.amp.release", 0.500f);
        for (const auto* slot : { "1", "2", "3" })
        {
            expect(juce::String("mod.env") + slot + ".attack", 0.250f);
            expect(juce::String("mod.env") + slot + ".decay", 0.600f);
            expect(juce::String("mod.env") + slot + ".sustain", 0.7f);
            expect(juce::String("mod.env") + slot + ".release", 1.000f);
        }

        check("EnvUpgrade_DefaultsAreTheDocumentedValues", wrong.isEmpty(),
              wrong.isEmpty() ? "amp 15 ms / 300 ms / 0.8 / 500 ms; mod 250 ms / 600 ms / 0.7 / 1 s"
                              : wrong.joinIntoString(", "));
    }

    // Widening the range must not cost the fast end its resolution: a quarter of
    // the knob is still percussive territory.
    {
        PX3SynthAudioProcessor processor;
        const auto& range = findParameter(processor, "voice.amp.attack")->getNormalisableRange();
        const auto quarter = range.convertFrom0to1(0.25f);
        const auto half = range.convertFrom0to1(0.5f);
        const auto threeQuarters = range.convertFrom0to1(0.75f);

        check("EnvUpgrade_TheFastEndKeepsItsResolution",
              quarter > 0.0f && quarter <= 0.03f && half <= 1.5f,
              "25% of the knob is " + fmt(quarter * 1000.0f, 1) + " ms, 50% is " + fmt(half, 2)
                  + " s, 75% is " + fmt(threeQuarters, 2) + " s");
    }

    // The generator honours a long time rather than capping it somewhere below
    // the knob. Run at 1 kHz so thirty seconds is cheap.
    {
        constexpr double rate = 1000.0;
        AmpEnvelope envelope;
        envelope.prepare(rate);
        EnvelopeSettings settings;
        settings.attackSeconds = 30.0f;
        settings.decaySeconds = 1.0f;
        settings.sustainLevel = 1.0f;
        settings.releaseSeconds = 30.0f;
        envelope.setSettings(settings);
        envelope.noteOn();

        std::vector<float> rise;
        for (int i = 0; i < static_cast<int>(32.0 * rate); ++i)
        {
            rise.push_back(envelope.getNextSample());
        }
        const auto at = [&rise](double seconds) { return rise[static_cast<std::size_t>(seconds * rate)]; };
        const auto stillRising = at(10.0) < at(20.0) && at(20.0) < at(29.0) && at(29.0) < 0.999f;

        check("EnvUpgrade_AThirtySecondAttackTakesThirtySeconds", stillRising && at(31.0) > 0.99f,
              "level at 10 s " + fmt(at(10.0), 3) + ", 20 s " + fmt(at(20.0), 3) + ", 29 s "
                  + fmt(at(29.0), 4) + ", 31 s " + fmt(at(31.0), 4));

        envelope.noteOff();
        auto activeAtTwenty = false;
        auto levelAtFifteen = 0.0f;
        for (int i = 0; i < static_cast<int>(33.0 * rate); ++i)
        {
            const auto level = envelope.getNextSample();
            if (i == static_cast<int>(15.0 * rate)) { levelAtFifteen = level; }
            if (i == static_cast<int>(20.0 * rate)) { activeAtTwenty = envelope.isActive(); }
        }

        check("EnvUpgrade_AThirtySecondReleaseTakesThirtySeconds",
              activeAtTwenty && levelAtFifteen > 0.0f && ! envelope.isActive(),
              "15 s into release " + fmt(levelAtFifteen, 4) + ", still active at 20 s: "
                  + juce::String(activeAtTwenty ? "yes" : "no") + ", finished by 33 s: "
                  + juce::String(envelope.isActive() ? "no" : "yes"));
    }
}

//==============================================================================
// State migration
//==============================================================================
void testStateUpgrade()
{
    // The 0.8 state reader accepts only schema v1. Unsupported major/legacy
    // roots are rejected atomically rather than adapted to the grouped schema.
    {
        PX3SynthAudioProcessor source;
        for (const auto version : { 0, 11, 12, 13 })
        {
            auto tree = source.createParameterStateTree();
            tree.setProperty("stateVersion", version, nullptr);
            PX3SynthAudioProcessor target;
            setParam(target, "voice.amp.attack", 1.75f);
            const auto before = getParamValue(target, "voice.amp.attack");
            juce::String error;
            const auto accepted = target.applyParameterStateTree(tree, &error);
            check(("StateSchema_RejectsUnsupportedVersion_" + juce::String(version)).toRawUTF8(),
                  ! accepted && nearly(getParamValue(target, "voice.amp.attack"), before, 1.0e-6)
                      && error.containsIgnoreCase("Unsupported"),
                  error);
        }
    }

    // A current state is NOT migrated: a 25 s release stays 25 s.
    {
        PX3SynthAudioProcessor source;
        setParam(source, "voice.amp.release", 25.0f);
        setParam(source, "mod.env1.attack", 12.5f);
        setChoice(source, "mod.lfo1.waveform", 5);
        setParam(source, "mod.lfo1.ramp.time", 45.0f);
        setParam(source, "mod.lfo1.key.sync", 1.0f);
        setParam(source, "voice.osc2.tuning.cents", -7.0f);
        setChoice(source, "voice.filters.routing.mode", 1);
        setParam(source, "voice.filters.routing.balance", 0.27f);

        juce::MemoryBlock block;
        source.getStateInformation(block);
        PX3SynthAudioProcessor target;
        target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));

        const auto ok = std::abs(getParamValue(target, "voice.amp.release") - 25.0f) < 0.01f
                     && std::abs(getParamValue(target, "mod.env1.attack") - 12.5f) < 0.01f
                     && upgradeChoiceIndex(target, "mod.lfo1.waveform") == 5
                     && std::abs(getParamValue(target, "mod.lfo1.ramp.time") - 45.0f) < 0.01f
                     && getParamValue(target, "mod.lfo1.key.sync") > 0.5f
                     && std::abs(getParamValue(target, "voice.osc2.tuning.cents") + 7.0f) < 0.01f
                     && upgradeChoiceIndex(target, "voice.filters.routing.mode") == 1
                     && std::abs(getParamValue(target, "voice.filters.routing.balance") - 0.27f) < 0.002f;

        check("StateUpgrade_NewValuesSurviveARoundTripUnmigrated", ok,
              "release " + fmt(getParamValue(target, "voice.amp.release"), 3) + " s, ramp "
                  + fmt(getParamValue(target, "mod.lfo1.ramp.time"), 2) + " s, waveform "
                  + juce::String(upgradeChoiceIndex(target, "mod.lfo1.waveform")) + ", routing "
                  + juce::String(upgradeChoiceIndex(target, "voice.filters.routing.mode")) + ", balance "
                  + fmt(getParamValue(target, "voice.filters.routing.balance"), 3));
    }

}

//==============================================================================
// Timed ramps and key sync
//==============================================================================
void testRampsAndKeySyncUpgrade()
{
    const auto rampUp = px3::lfoWaveformToIndex(px3::LfoWaveform::rampUp);
    const auto rampDown = px3::lfoWaveformToIndex(px3::LfoWaveform::rampDown);

    const auto runRamp = [](int waveform, float seconds, double rate, double totalSeconds)
    {
        LfoGenerator lfo;
        lfo.prepare(rate);
        LfoSettings settings;
        settings.waveformIndex = waveform;
        settings.rampSeconds = seconds;
        lfo.setSettings(settings);
        lfo.retrigger();

        std::vector<float> values;
        const auto count = static_cast<int>(totalSeconds * rate);
        values.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            values.push_back(lfo.getNextSample());
        }
        return values;
    };

    // Appended, so every stored index keeps meaning what it meant.
    {
        const auto choices = px3::lfoWaveformChoices();
        const auto ok = choices.size() == 8 && choices[0] == "SINE" && choices[1] == "TRIANGLE"
                     && choices[2] == "SAW" && choices[3] == "SQUARE"
                     && choices[rampUp] == "RAMP UP" && choices[rampDown] == "RAMP DOWN"
                     && choices[6] == "S&H" && choices[7] == "SMOOTH RND";
        check("RampUpgrade_ExistingWaveformsKeepTheirIndices", ok, choices.joinIntoString(", "));
    }

    for (const auto waveform : { rampUp, rampDown })
    {
        const auto name = juce::String(waveform == rampUp ? "RampUpgrade_RampUp" : "RampUpgrade_RampDown");
        const auto direction = waveform == rampUp ? 1.0f : -1.0f;
        const auto values = runRamp(waveform, 2.0f, kSampleRate, 5.0);
        const auto at = [&values](double seconds) { return values[static_cast<std::size_t>(seconds * kSampleRate)]; };

        auto monotonic = true;
        for (std::size_t i = 1; i < static_cast<std::size_t>(2.0 * kSampleRate); ++i)
        {
            monotonic = monotonic && direction * (values[i] - values[i - 1]) >= 0.0f;
        }
        const auto travels = std::abs(at(0.0) + direction) < 1.0e-3f && std::abs(at(1.0)) < 2.0e-3f
                          && std::abs(at(2.0) - direction) < 1.0e-3f && monotonic;
        check((name + "_TravelsEndToEndOverItsDuration").toRawUTF8(), travels,
              "at 0 s " + fmt(at(0.0), 4) + ", 1 s " + fmt(at(1.0), 4) + ", 2 s " + fmt(at(2.0), 4)
                  + (monotonic ? ", monotonic" : ", NOT monotonic"));

        auto holds = true;
        for (auto i = static_cast<std::size_t>(2.0 * kSampleRate); i < values.size(); ++i)
        {
            holds = holds && values[i] == direction;
        }
        check((name + "_HoldsItsEndValueAfterwards").toRawUTF8(), holds,
              "held " + fmt(direction, 0) + " from 2 s to 5 s: " + juce::String(holds ? "yes" : "no"));
    }

    // The same duration at every sample rate.
    {
        juce::String detail;
        auto worstMs = 0.0;
        for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto values = runRamp(rampUp, 3.0f, rate, 4.0);
            auto crossing = 0;
            while (crossing < static_cast<int>(values.size()) && values[static_cast<std::size_t>(crossing)] < 0.0f)
            {
                ++crossing;
            }
            const auto errorMs = std::abs(crossing / rate - 1.5) * 1000.0;
            worstMs = juce::jmax(worstMs, errorMs);
            detail << fmt(rate, 0) << " Hz crosses at " << fmt(crossing / rate, 5) << " s  ";
        }
        check("RampUpgrade_DurationDoesNotDependOnSampleRate", worstMs < 0.1,
              detail + "(worst " + fmt(worstMs, 3) + " ms from 1.5 s)");
    }

    // And at every block size: the value read for a block is the ramp at that
    // block's midpoint, and the clock counts exactly the samples processed.
    {
        juce::String detail;
        auto worstError = 0.0;
        auto clocksAgree = true;
        for (const auto block : { 16, 128, 512, 2048 })
        {
            LfoGenerator lfo;
            lfo.prepare(kSampleRate);
            LfoSettings settings;
            settings.waveformIndex = rampUp;
            settings.rampSeconds = 3.0f;
            lfo.setSettings(settings);
            lfo.retrigger();

            auto samples = 0;
            auto blockWorst = 0.0;
            while (samples < static_cast<int>(3.5 * kSampleRate))
            {
                const auto value = lfo.getMidpointSignalAndAdvance(block);
                const auto midpointSeconds = (samples + block * 0.5) / kSampleRate;
                const auto expected = juce::jlimit(-1.0, 1.0, -1.0 + 2.0 * midpointSeconds / 3.0);
                blockWorst = juce::jmax(blockWorst, std::abs(value - expected));
                samples += block;
            }
            worstError = juce::jmax(worstError, blockWorst);
            clocksAgree = clocksAgree && std::abs(lfo.getRampElapsedSeconds() - samples / kSampleRate) < 1.0e-9;
            detail << block << "-sample blocks worst " << fmt(blockWorst, 6) << "  ";
        }
        check("RampUpgrade_DurationDoesNotDependOnBlockSize", worstError < 1.0e-4 && clocksAgree, detail);
    }

    // Past the 30 s the specification asks for, end to end.
    {
        PX3SynthAudioProcessor processor;
        auto ceilingOk = px3::lfoMaxRampSeconds >= 30.0f;
        for (const auto* id : { "mod.lfo1.ramp.time", "mod.lfo2.ramp.time", "mod.lfo3.ramp.time", "mod.lfo4.ramp.time" })
        {
            auto* parameter = findParameter(processor, id);
            ceilingOk = ceilingOk && parameter != nullptr && parameter->getNormalisableRange().end >= 30.0f;
        }

        LfoGenerator lfo;
        lfo.prepare(kSampleRate);
        LfoSettings settings;
        settings.waveformIndex = rampUp;
        settings.rampSeconds = 45.0f;
        lfo.setSettings(settings);
        lfo.retrigger();

        auto atHalfway = -2.0f, atEnd = -2.0f;
        while (lfo.getRampElapsedSeconds() < 46.0)
        {
            const auto before = lfo.getRampElapsedSeconds();
            const auto value = lfo.getMidpointSignalAndAdvance(4096);
            if (before < 22.5 && lfo.getRampElapsedSeconds() >= 22.5) { atHalfway = value; }
            atEnd = value;
        }

        check("RampUpgrade_RampsRunForThirtySecondsAndBeyond",
              ceilingOk && std::abs(atHalfway) < 0.01f && std::abs(atEnd - 1.0f) < 1.0e-6f,
              "ceiling " + fmt(px3::lfoMaxRampSeconds, 0) + " s; a 45 s ramp reads " + fmt(atHalfway, 4)
                  + " at 22.5 s and " + fmt(atEnd, 4) + " at 46 s");
    }

    // Retrigger itself.
    {
        LfoGenerator lfo;
        lfo.prepare(kSampleRate);
        LfoSettings settings;
        settings.waveformIndex = rampUp;
        settings.rampSeconds = 2.0f;
        lfo.setSettings(settings);
        lfo.retrigger();
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i) { lfo.getNextSample(); }
        const auto midway = lfo.getNextSample();
        lfo.retrigger();
        const auto restarted = lfo.getNextSample();
        check("RampUpgrade_RetriggerRestartsARamp", std::abs(restarted + 1.0f) < 1.0e-6f,
              "midway " + fmt(midway, 4) + ", after retrigger " + fmt(restarted, 6));
    }
    {
        LfoGenerator played, fresh;
        LfoSettings settings;
        settings.frequencyHz = 3.0f;
        settings.waveformIndex = 0;
        played.prepare(kSampleRate); played.setSettings(settings);
        fresh.prepare(kSampleRate); fresh.setSettings(settings);
        for (int i = 0; i < 12345; ++i) { played.getNextSample(); }
        played.retrigger();
        fresh.retrigger();
        auto identical = true;
        for (int i = 0; i < 4096; ++i)
        {
            identical = identical && played.getNextSample() == fresh.getNextSample();
        }
        check("RampUpgrade_RetriggerRestartsACycleAtPhaseZero", identical);
    }

    // ---- key sync, through the processor -----------------------------------
    const auto makeLfoPatch = [](PX3SynthAudioProcessor& processor, int waveform, bool keySync)
    {
        makePlainPatch(processor);
        setParam(processor, "mod.lfo1.enabled", 1.0f);
        setParam(processor, "mod.lfo1.frequency", 0.5f);
        setParam(processor, "mod.lfo1.amount", 1.0f);
        setChoice(processor, "mod.lfo1.waveform", waveform);
        setParam(processor, "mod.lfo1.ramp.time", 1.0f);
        setParam(processor, "mod.lfo1.key.sync", keySync ? 1.0f : 0.0f);
        processor.setLfoAssignmentByParameterId(0, "voice.filter1.cutoff", false);
        findParameter(processor, "voice.filter1.cutoff")->setValueNotifyingHost(0.5f);
        prepareUpgrade(processor);
    };
    // How far the LFO has the cutoff from its base, before the fold.
    const auto deviation = [](PX3SynthAudioProcessor& processor)
    {
        return processor.getUnclampedModulatedNormalisedValue(*findParameter(processor, "voice.filter1.cutoff")) - 0.5f;
    };

    // A cyclic shape. 47 blocks is a quarter-cycle at 0.5 Hz, so a free-running
    // sine is near its peak when the note lands; a synced one is back at zero.
    {
        PX3SynthAudioProcessor synced, freeRunning;
        makeLfoPatch(synced, 0, true);
        makeLfoPatch(freeRunning, 0, false);
        runUpgradeBlocks(synced, 47);
        runUpgradeBlocks(freeRunning, 47);

        auto noteA = upgradeNoteOn(60);
        runUpgradeBlocks(synced, 1, &noteA);
        auto noteB = upgradeNoteOn(60);
        runUpgradeBlocks(freeRunning, 1, &noteB);
        const auto syncedAtNote = deviation(synced);
        const auto freeAtNote = deviation(freeRunning);

        check("KeySync_ANewNoteRestartsTheLfo", std::abs(syncedAtNote) < 0.02f,
              "synced: " + fmt(syncedAtNote, 4) + " from the base on the note's block");
        check("KeySync_WithoutItACyclicLfoRunsFreely", std::abs(freeAtNote) > 0.3f,
              "free-running: " + fmt(freeAtNote, 4) + " from the base on the same block");

        // Global: a second note while the first is still held restarts it again.
        runUpgradeBlocks(synced, 30);
        const auto beforeSecond = deviation(synced);
        auto second = upgradeNoteOn(64);
        runUpgradeBlocks(synced, 1, &second);
        const auto afterSecond = deviation(synced);
        check("KeySync_EveryNewNoteRetriggersIt", std::abs(beforeSecond) > 0.2f && std::abs(afterSecond) < 0.02f,
              "before the second note " + fmt(beforeSecond, 4) + ", on it " + fmt(afterSecond, 4));

        // A note-off is not a new note.
        runUpgradeBlocks(synced, 30);
        auto release = upgradeNoteOff(64);
        runUpgradeBlocks(synced, 1, &release);
        const auto afterRelease = deviation(synced);
        check("KeySync_ANoteOffDoesNotRetrigger", std::abs(afterRelease) > 0.2f,
              "on the note-off's block " + fmt(afterRelease, 4));
    }

    // A ramp without key sync: first note after silence restarts it, legato
    // notes ride it.
    {
        PX3SynthAudioProcessor processor;
        makeLfoPatch(processor, rampUp, false);
        runUpgradeBlocks(processor, 200);
        const auto idle = deviation(processor);

        auto first = upgradeNoteOn(60);
        runUpgradeBlocks(processor, 1, &first);
        const auto onFirst = deviation(processor);

        runUpgradeBlocks(processor, 40);
        auto legato = upgradeNoteOn(64);
        runUpgradeBlocks(processor, 1, &legato);
        const auto onLegato = deviation(processor);

        auto offA = upgradeNoteOff(60);
        runUpgradeBlocks(processor, 1, &offA);
        auto offB = upgradeNoteOff(64);
        runUpgradeBlocks(processor, 1, &offB);
        runUpgradeBlocks(processor, 10);
        auto afterSilence = upgradeNoteOn(67);
        runUpgradeBlocks(processor, 1, &afterSilence);
        const auto onAfterSilence = deviation(processor);

        check("KeySync_ARampStartsOnTheFirstNoteAfterSilence", idle > 0.45f && onFirst < -0.45f,
              "finished ramp " + fmt(idle, 3) + ", on the first note " + fmt(onFirst, 3));
        check("KeySync_ALegatoNoteRidesTheSameRamp", onLegato > -0.3f,
              "on a legato note 0.44 s in " + fmt(onLegato, 3));
        check("KeySync_ARampRestartsOnceEveryKeyIsReleased", onAfterSilence < -0.45f,
              "on the next note after silence " + fmt(onAfterSilence, 3));
    }
    {
        PX3SynthAudioProcessor processor;
        makeLfoPatch(processor, rampUp, true);
        auto first = upgradeNoteOn(60);
        runUpgradeBlocks(processor, 1, &first);
        runUpgradeBlocks(processor, 40);
        auto legato = upgradeNoteOn(64);
        runUpgradeBlocks(processor, 1, &legato);
        const auto onLegato = deviation(processor);
        check("KeySync_WithKeySyncEveryNoteRestartsARamp", onLegato < -0.45f,
              "on a legato note 0.44 s in " + fmt(onLegato, 3));
    }
}

//==============================================================================
// The modulation rule and the pitch destinations
//==============================================================================
void testModulationRuleUpgrade()
{
    // The destination audit. Every assignable parameter, one square LFO at 100%
    // on a base of 0.5: every one must swing a full half-range. Anything less is
    // a destination scaled down somewhere, which is the "subtle at 100%" report.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "mod.lfo1.enabled", 1.0f);
        setParam(processor, "mod.lfo1.frequency", 0.01f);   // stays in the square's high half throughout
        setParam(processor, "mod.lfo1.amount", 1.0f);
        setChoice(processor, "mod.lfo1.waveform", 3);
        prepareUpgrade(processor);

        juce::StringArray attenuated, unreported;
        auto audited = 0;
        for (auto* parameter : processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            if (ranged == nullptr) { continue; }

            const auto id = ranged->getParameterID();
            if (! processor.setLfoAssignmentByParameterId(0, id, false)) { continue; }

            const auto original = ranged->getValue();
            ranged->setValueNotifyingHost(0.5f);
            runUpgradeBlocks(processor, 1);
            const auto raw = processor.getUnclampedModulatedNormalisedValue(*ranged);
            ranged->setValueNotifyingHost(original);
            ++audited;

            if (raw < -0.75f)
            {
                unreported.add(id);
            }
            else if (std::abs((raw - 0.5f) - 0.5f) > 0.01f)
            {
                attenuated.add(id + " " + fmt(raw - 0.5f, 3));
            }
        }

        check("ModRule_EveryDestinationSwingsFullyAtFullAmount",
              attenuated.isEmpty() && unreported.isEmpty() && audited > 50,
              juce::String(audited) + " destinations audited"
                  + (attenuated.isEmpty() ? juce::String() : "; attenuated: " + attenuated.joinIntoString(", "))
                  + (unreported.isEmpty() ? juce::String() : "; not reported as modulated: " + unreported.joinIntoString(", ")));
    }

    // The reported symptom, measured where it was reported: cutoff at its
    // default. The old nearer-side rule gave a 100% LFO 0.68 of an octave here.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        auto* cutoff = findParameter(processor, "voice.filter1.cutoff");
        cutoff->setValueNotifyingHost(cutoff->getDefaultValue());
        setParam(processor, "mod.lfo1.enabled", 1.0f);
        setParam(processor, "mod.lfo1.frequency", 2.0f);
        setParam(processor, "mod.lfo1.amount", 1.0f);
        setChoice(processor, "mod.lfo1.waveform", 0);
        processor.setLfoAssignmentByParameterId(0, "voice.filter1.cutoff", false);
        prepareUpgrade(processor);

        auto lowHz = 1.0e9f, highHz = 0.0f;
        for (int block = 0; block < 100; ++block)
        {
            runUpgradeBlocks(processor, 1);
            const auto hz = cutoff->convertFrom0to1(processor.getModulatedNormalisedValue(*cutoff));
            lowHz = juce::jmin(lowHz, hz);
            highHz = juce::jmax(highHz, hz);
        }
        const auto octaves = std::log2(highHz / juce::jmax(1.0f, lowHz));
        check("ModRule_FullAmountSweepsTheDefaultCutoffOverOctaves", octaves > 3.5f,
              "a 100% sine on the 12 kHz default sweeps " + fmt(lowHz, 0) + ".." + fmt(highHz, 0)
                  + " Hz, " + fmt(octaves, 2) + " octaves");
    }

    // Pitch mod at 100% is two octaves each way.
    {
        const auto semitonesAt = [](float amount)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "mod.lfo1.enabled", 1.0f);
            setParam(processor, "mod.lfo1.frequency", 0.01f);
            setParam(processor, "mod.lfo1.amount", amount);
            setChoice(processor, "mod.lfo1.waveform", 3);
            processor.setLfoAssignmentByParameterId(0, "voice.osc1.pitch.mod", false);
            prepareUpgrade(processor);
            runUpgradeBlocks(processor, 2);
            auto* pitchMod = findParameter(processor, "voice.osc1.pitch.mod");
            return pitchMod->convertFrom0to1(processor.getModulatedNormalisedValue(*pitchMod));
        };
        const auto up = semitonesAt(1.0f);
        const auto down = semitonesAt(-1.0f);
        check("ModRule_PitchModAtFullAmountIsTwoOctaves",
              std::abs(up - 24.0f) < 0.05f && std::abs(down + 24.0f) < 0.05f,
              "+100% " + fmt(up, 2) + " st, -100% " + fmt(down, 2) + " st");
    }


}

//==============================================================================
// Filter routing
//==============================================================================
struct RoutingStage
{
    bool enabled;
    int type;
    float cutoffHz;
};

Capture renderRouting(int oscMode,
                      bool parallel,
                      float balance,
                      RoutingStage first,
                      RoutingStage second,
                      const std::function<void(PX3SynthAudioProcessor&, int)>& perBlock = {})
{
    PX3SynthAudioProcessor processor;
    makePlainPatch(processor);
    setChoice(processor, "voice.osc1.mode", oscMode);

    const auto apply = [&processor](const juce::String& prefix, const RoutingStage& stage)
    {
        setParam(processor, prefix + "enabled", stage.enabled ? 1.0f : 0.0f);
        setChoice(processor, prefix + "type", stage.type);
        setParam(processor, prefix + "cutoff", stage.cutoffHz);
        setParam(processor, prefix + "resonance", 0.707f);
    };
    apply("voice.filter1.", first);
    apply("voice.filter2.", second);
    setChoice(processor, "voice.filters.routing.mode", parallel ? 1 : 0);
    setParam(processor, "voice.filters.routing.balance", balance);

    std::function<void(int)> hook;
    if (perBlock)
    {
        hook = [&processor, &perBlock](int block) { perBlock(processor, block); };
    }
    return render(processor, 40000, { { 0, true, 57, 0.9f } }, hook);
}

void testFilterRoutingUpgrade()
{
    constexpr int sine = 0;
    constexpr int rich = 1;
    const RoutingStage lowPass { true, 0, 300.0f };
    const RoutingStage highPass { true, 2, 1500.0f };
    const RoutingStage bypassed { false, 6, 1000.0f };
    const auto level = [](const Capture& capture) { return capture.rmsOver(12000, 40000); };

    {
        PX3SynthAudioProcessor processor;
        const auto routing = upgradeChoiceIndex(processor, "voice.filters.routing.mode");
        const auto balance = getParamValue(processor, "voice.filters.routing.balance");
        check("FilterRouting_DefaultsToSeriesWithACentredBalance", routing == 0 && std::abs(balance - 0.5f) < 1.0e-6f,
              "routing " + juce::String(routing) + ", balance " + fmt(balance, 3));
    }

    {
        const auto lowAlone = level(renderRouting(rich, false, 0.5f, lowPass, bypassed));
        const auto highAlone = level(renderRouting(rich, false, 0.5f, bypassed, highPass));
        const auto series = level(renderRouting(rich, false, 0.5f, lowPass, highPass));
        const auto parallelFirst = level(renderRouting(rich, true, 0.0f, lowPass, highPass));
        const auto parallelSecond = level(renderRouting(rich, true, 1.0f, lowPass, highPass));
        const auto parallelHalf = level(renderRouting(rich, true, 0.5f, lowPass, highPass));

        check("FilterRouting_SeriesStillRunsFilterOneIntoFilterTwo",
              series < juce::jmin(lowAlone, highAlone) * 0.5,
              "LP 300 alone " + fmt(upgradeDb(lowAlone), 1) + " dB, HP 1500 alone " + fmt(upgradeDb(highAlone), 1)
                  + " dB, in series " + fmt(upgradeDb(series), 1) + " dB");
        check("FilterRouting_ParallelBalanceZeroIsFilterOneAlone",
              std::abs(upgradeDb(parallelFirst / lowAlone)) < 0.2,
              fmt(upgradeDb(parallelFirst / lowAlone), 3) + " dB from filter 1 alone");
        check("FilterRouting_ParallelBalanceOneIsFilterTwoAlone",
              std::abs(upgradeDb(parallelSecond / highAlone)) < 0.2,
              fmt(upgradeDb(parallelSecond / highAlone), 3) + " dB from filter 2 alone");
        check("FilterRouting_ParallelIsNotSeries", parallelHalf > series * 2.0,
              "parallel at 0.5 " + fmt(upgradeDb(parallelHalf), 1) + " dB, series " + fmt(upgradeDb(series), 1) + " dB");
    }

    // Two matching filters in parallel must not come out louder than one: the
    // outputs are correlated, so a constant-power blend would add 3 dB here.
    {
        const RoutingStage matching { true, 0, 1200.0f };
        const auto single = level(renderRouting(rich, false, 0.5f, matching, bypassed));
        const auto both = level(renderRouting(rich, true, 0.5f, matching, matching));
        check("FilterRouting_MatchingFiltersInParallelAddNoGain", std::abs(upgradeDb(both / single)) < 0.2,
              fmt(upgradeDb(both / single), 3) + " dB against one filter");
    }

    // Switching the routing, or throwing the balance end to end, mid-note. On a
    // sine the chain is nearly silent in series and loud in parallel, so an
    // instant switch would be a step of half the signal.
    {
        const auto capture = renderRouting(sine, false, 0.5f, lowPass, highPass,
                                           [](PX3SynthAudioProcessor& processor, int block)
                                           {
                                               if (block == kRoutingSwitchBlock) { setChoice(processor, "voice.filters.routing.mode", 1); }
                                           });
        const auto before = capture.maxStep(8000, (kRoutingSwitchBlock - 1) * kBlockSize);
        const auto around = capture.maxStep((kRoutingSwitchBlock - 1) * kBlockSize, (kRoutingSwitchBlock + 6) * kBlockSize);
        const auto after = capture.maxStep((kRoutingSwitchBlock + 10) * kBlockSize, 40000);
        check("FilterRouting_SwitchingMidNoteDoesNotClick", around <= juce::jmax(before, after) * 1.25 + 1.0e-6,
              "largest step before " + fmt(before, 5) + ", across the switch " + fmt(around, 5)
                  + ", after " + fmt(after, 5));
    }
    {
        const auto capture = renderRouting(sine, true, 0.0f, lowPass, highPass,
                                           [](PX3SynthAudioProcessor& processor, int block)
                                           {
                                               if (block == kRoutingSwitchBlock) { setParam(processor, "voice.filters.routing.balance", 1.0f); }
                                           });
        const auto before = capture.maxStep(8000, (kRoutingSwitchBlock - 1) * kBlockSize);
        const auto around = capture.maxStep((kRoutingSwitchBlock - 1) * kBlockSize, (kRoutingSwitchBlock + 6) * kBlockSize);
        const auto after = capture.maxStep((kRoutingSwitchBlock + 10) * kBlockSize, 40000);
        check("FilterRouting_ThrowingTheBalanceMidNoteDoesNotClick", around <= juce::jmax(before, after) * 1.25 + 1.0e-6,
              "largest step before " + fmt(before, 5) + ", across the change " + fmt(around, 5)
                  + ", after " + fmt(after, 5));
    }

    {
        PX3SynthAudioProcessor source;
        setChoice(source, "voice.filters.routing.mode", 1);
        setParam(source, "voice.filters.routing.balance", 0.27f);
        juce::MemoryBlock block;
        source.getStateInformation(block);
        PX3SynthAudioProcessor target;
        target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        check("FilterRouting_SurvivesAStateRoundTrip",
              upgradeChoiceIndex(target, "voice.filters.routing.mode") == 1
                  && std::abs(getParamValue(target, "voice.filters.routing.balance") - 0.27f) < 0.002f,
              "routing " + juce::String(upgradeChoiceIndex(target, "voice.filters.routing.mode")) + ", balance "
                  + fmt(getParamValue(target, "voice.filters.routing.balance"), 3));
    }
}

} // namespace

void testModulationUpgrade()
{
    suite("MODULATION UPGRADE");

    {
        // MIDI clock: with no host timing, an external clock drives tempo and
        // transport; when its ticks stop, timing is released again.
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, 48000.0, 512);
        processor.prepareToPlay(48000.0, 512);
        setParam(processor, "mod.lfo1.clock.mode", 1.0f);   // TEMPO
        juce::AudioBuffer<float> buffer(2, 512);
        const auto tickInterval = 60.0 * 48000.0 / (140.0 * 24.0);
        double nextTick = 0.0;
        for (int block = 0; block < 200; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0) midi.addEvent(juce::MidiMessage::midiStart(), 0);
            const auto blockStart = static_cast<double>(block) * 512.0;
            while (nextTick < blockStart + 512.0)
            {
                midi.addEvent(juce::MidiMessage::midiClock(), static_cast<int>(nextTick - blockStart));
                nextTick += tickInterval;
            }
            buffer.clear();
            processor.processBlock(buffer, midi);
        }
        const auto following = processor.isFollowingMidiClock();
        const auto bpm = processor.getMidiClockBpm();
        const auto lfoClocked = processor.isLfoClockAvailable(0);
        for (int block = 0; block < 100; ++block)
        {
            juce::MidiBuffer none;
            buffer.clear();
            processor.processBlock(buffer, none);
        }
        check("LfoClock_FollowsAnExternalMidiClockWithoutAHost",
              following && std::abs(bpm - 140.0) < 1.0 && lfoClocked && ! processor.isFollowingMidiClock(),
              "following " + juce::String(following ? "yes" : "no") + " at " + juce::String(bpm, 2)
                  + " BPM (sent 140); LFO clock available " + juce::String(lfoClocked ? "yes" : "no")
                  + "; released after ticks stop " + juce::String(processor.isFollowingMidiClock() ? "no" : "yes"));

        // ENV SYNC: at 120 BPM (a beat is 0.5 s) a 0.3 s attack - 0.6 of a beat -
        // snaps to the nearest division, an eighth note: 0.25 s. Free again
        // once the clock has gone.
        setParam(processor, "mod.env1.attack", 0.3f);
        setParam(processor, "mod.env1.sync", 1.0f);
        const auto unsynced = processor.currentModEnvelopeSettings(0).attackSeconds;
        nextTick = 0.0;
        for (int block = 0; block < 120; ++block)
        {
            juce::MidiBuffer midi;
            const auto blockStart = static_cast<double>(block) * 512.0;
            while (nextTick < blockStart + 512.0)
            {
                midi.addEvent(juce::MidiMessage::midiClock(), static_cast<int>(nextTick - blockStart));
                nextTick += 60.0 * 48000.0 / (120.0 * 24.0);
            }
            buffer.clear();
            processor.processBlock(buffer, midi);
        }
        const auto synced = processor.currentModEnvelopeSettings(0).attackSeconds;
        check("ModEnv_SyncSnapsTimesToTheTempo", std::abs(synced - 0.25f) < 0.01f && std::abs(unsynced - 0.3f) < 0.01f,
              "0.3 s attack with no clock " + juce::String(unsynced, 3) + " s, at 120 BPM " + juce::String(synced, 3) + " s");
    }

    {
        // Envelopes are per voice: a note's filter follows ITS envelope, not an
        // average over the chord. B starts while A's two-second attack is half
        // way up; B's own envelope is near zero, so B must sound the same as
        // when A starts with it. Averaging used to open B's filter to A's level.
        auto bFundamental = [](int aStartSample, bool playB = true)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "voice.osc1.mode", 1);
            setParam(processor, "voice.amp.sustain", 1.0f);
            setParam(processor, "voice.filter1.enabled", 1.0f);
            setChoice(processor, "voice.filter1.type", 1);   // LP24
            setParam(processor, "voice.filter1.cutoff", 200.0f);
            setParam(processor, "mod.env1.enabled", 1.0f);
            setParam(processor, "mod.env1.amount", 1.0f);
            setParam(processor, "mod.env1.attack", 2.0f);
            setParam(processor, "mod.env1.sustain", 1.0f);
            processor.setEnvelopeAssignmentByParameterId(0, "voice.filter1.cutoff", false);
            std::vector<NoteEvent> events { { aStartSample, true, 45, 0.9f } };
            if (playB) events.push_back({ 48000, true, 84, 0.9f });
            const auto capture = render(processor, 48000 * 3 / 2, events);
            // Hann-windowed so A's harmonics (110 Hz apart, the nearest 54 Hz
            // away) cannot leak into B's bin.
            double c = 0.0, sn = 0.0;
            for (int k = 0; k < 4800; ++k)
            {
                const auto n = 48960 + k;
                const auto hann = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * k / 4799.0);
                const auto w = juce::MathConstants<double>::twoPi * 1046.502 * n / 48000.0;
                c += hann * capture.left[static_cast<std::size_t>(n)] * std::cos(w);
                sn += hann * capture.left[static_cast<std::size_t>(n)] * std::sin(w);
            }
            return 10.0 * std::log10(c * c + sn * sn + 1.0e-30);
        };
        const auto aEarlier = bFundamental(0);
        const auto together = bFundamental(48000);
        const auto aAlone = bFundamental(0, false);
        check("ModGraph_EnvelopesModulateEachVoiceOnItsOwn", std::abs(aEarlier - together) < 3.0 && aAlone < aEarlier - 10.0,
              "B's fundamental with A's envelope half open " + juce::String(aEarlier, 1) + " dB, with A starting alongside "
                  + juce::String(together, 1) + " dB (A alone in that bin " + juce::String(aAlone, 1) + " dB)");
    }
    {
        using namespace px3::synth;
        CompiledModulationGraph graph;
        std::string error;
        const std::array<ModulationSourceDescriptor, 3> sources { {
            { ModulationScope::global, true }, { ModulationScope::global, false },
            { ModulationScope::voice, false } } };
        const std::array<ModulationDestinationDescriptor, 3> destinations { {
            { ModulationScope::global, -1 }, { ModulationScope::global, 1 },
            { ModulationScope::voice, 2 } } };
        const std::array<ModulationRoute, 3> routes { {
            { 0, 0, 0, 0.8f }, { 1, 1, 0, -0.5f }, { 2, 0, 1, 0.2f } } };
        const auto compiled = graph.compile(sources, destinations, routes, error);
        const std::array<float, 3> signals { 1.0f, 0.5f, 0.0f };
        check("ModGraph_CompilesFanOutAndAddsAllSourcesBeforeClamping",
              compiled && graph.getRouteCount() == 3
                  && std::abs(graph.deltaFor(0, 0.5f, signals) - 0.275f) < 1.0e-6f);
        const std::array<ModulationRoute, 1> cycle { { { 0, 1, 1, 1.0f } } };
        check("ModGraph_RejectsCyclesWithoutReplacingThePublishedPlan",
              ! graph.compile(sources, destinations, cycle, error) && graph.getRouteCount() == 3);
        const std::array<ModulationRoute, 1> incompatible { { { 0, 2, 0, 1.0f } } };
        check("ModGraph_RejectsPerVoiceToGlobalScope",
              ! graph.compile(sources, destinations, incompatible, error));
        auto invalid = routes;
        invalid[1].slot = 0;
        check("ModGraph_RejectsDuplicateAutomationSlots", ! graph.compile(sources, destinations, invalid, error));
        invalid = routes;
        invalid[0].depth = std::numeric_limits<float>::quiet_NaN();
        check("ModGraph_RejectsNonFiniteRouteDepths", ! graph.compile(sources, destinations, invalid, error));
        const std::array<float, 3> depths { 0.0f, 0.0f, 0.0f };
        check("ModGraph_UsesLiveAutomationDepthWithoutRecompiling",
              graph.deltaFor(0, 0.5f, signals, depths) == 0.0f);
          const std::array<bool, 3> disabledSources { false, false, false };
          check("ModGraph_DisabledSourcesNeverContributeAfterPolarityConversion",
              graph.deltaFor(0, 0.5f, signals, {}, disabledSources) == 0.0f);
          ModulationGraphPublication publication;
          CompiledModulationGraph empty;
          const auto firstPublished = publication.publish(graph);
          auto firstReader = publication.read();
          const auto secondPublished = publication.publish(empty);
          auto secondReader = publication.read();
          const auto thirdPublished = publication.publish(graph);
          const auto rejectedWhilePinned = ! publication.publish(empty);
          check("ModGraph_PublicationNeverOverwritesPinnedReaders",
              firstPublished && secondPublished && thirdPublished && rejectedWhilePinned
                && firstReader && firstReader->getRouteCount() == 3
                && secondReader && secondReader->getRouteCount() == 0);
    }
    {
        PX3SynthAudioProcessor processor;
        juce::String error;
        const auto first = processor.setGraphRoute(0, { kMacro1Source, "voice.filter1.cutoff" }, error);
        const auto second = processor.setGraphRoute(1, { kMacro1Source, "mix.osc1.pan" }, error);
        processor.getMacroParam(0).setValueNotifyingHost(1.0f);
        auto& firstDepth = processor.getGraphRouteDepthParam(0);
        auto& secondDepth = processor.getGraphRouteDepthParam(1);
        firstDepth.setValueNotifyingHost(firstDepth.convertTo0to1(0.5f));
        secondDepth.setValueNotifyingHost(secondDepth.convertTo0to1(-0.5f));
        auto* cutoff = dynamic_cast<juce::RangedAudioParameter*>(findParameter(processor, "voice.filter1.cutoff"));
        auto* pan = dynamic_cast<juce::RangedAudioParameter*>(findParameter(processor, "mix.osc1.pan"));
        check("ModGraph_ProcessorSupportsFanOutWithAutomatedDepthSlots",
              first && second && cutoff != nullptr && pan != nullptr
                  && processor.getUnclampedModulatedNormalisedValue(*cutoff) > cutoff->getValue()
                  && processor.getUnclampedModulatedNormalisedValue(*pan) < pan->getValue(), error);
        const auto state = processor.createParameterStateTree();
        PX3SynthAudioProcessor restored;
        const auto loaded = restored.applyParameterStateTree(state, &error);
        check("ModGraph_EndpointsAndDepthsSurviveStateRoundTrip",
              loaded && restored.getGraphRoute(0).source == kMacro1Source
                  && restored.getGraphRoute(1).destination == "mix.osc1.pan"
                  && std::abs(restored.getGraphRouteDepthParam(0).get() - 0.5f) < 1.0e-5f, error);
        auto malformed = state.createCopy();
        malformed.getChildWithName("MODULATION_GRAPH").getChild(0).setProperty("source", "unknown", nullptr);
        const auto before = restored.createParameterStateTree().toXmlString();
        check("ModGraph_MalformedStateIsRejectedBeforeAnyMutation",
              ! restored.applyParameterStateTree(malformed, &error)
                  && restored.createParameterStateTree().toXmlString() == before);
        check("ModGraph_InvalidRouteEditLeavesEndpointsIntact",
              ! restored.setGraphRoute(0, { kMacro1Source, "missing" }, error)
                  && restored.getGraphRoute(0).destination == "voice.filter1.cutoff");
          check("ModGraph_SourceControlsAcceptAcyclicRoutes",
              restored.setGraphRoute(2, { kMacro1Source, "mod.lfo1.frequency" }, error)
                && restored.setGraphRoute(3, { 0, "mod.env1.attack" }, error), error);
          check("ModGraph_ProcessorRejectsSourceControlCyclesAtomically",
              ! restored.setGraphRoute(4, { kEnv1Source, "mod.lfo1.frequency" }, error)
                && restored.getGraphRoute(4).source == -1);
          auto& rateDepth = restored.getGraphRouteDepthParam(2);
          rateDepth.setValueNotifyingHost(rateDepth.convertTo0to1(0.5f));
          auto& rate = restored.getLfoFrequencyParam(0);
          check("ModGraph_MacroCanModulateLfoRate",
              restored.getUnclampedModulatedNormalisedValue(rate)
                  > static_cast<juce::RangedAudioParameter&>(rate).getValue());
    }
    {
        using namespace px3::synth;
        CompiledModulationGraph populated;
        CompiledModulationGraph empty;
        std::string error;
        const std::array<ModulationSourceDescriptor, 1> sources {};
        const std::array<ModulationDestinationDescriptor, 1> destinations {};
        const std::array<ModulationRoute, 1> routes { { { 0, 0, 0, 1.0f } } };
        populated.compile(sources, destinations, routes, error);
        ModulationGraphPublication publication;
        publication.publish(populated);
        auto coherent = false;
        {
            const ModulationGraphPublication::ScopedRead frame(publication);
            publication.publish(empty);
            auto reader = publication.read();
            coherent = reader && reader->getRouteCount() == 1;
        }
        auto nextFrame = publication.read();
        check("ModGraph_CallbackReadsOnePlanUntilItsScopeEnds",
              coherent && nextFrame && nextFrame->getRouteCount() == 0);
    }
    {
        PX3SynthAudioProcessor source;
        setParam(source, "mod.lfo1.frequency", 4.5f);
        const auto state = source.createParameterStateTree();
        const auto lfo = state.getChildWithName(px3::processor_internal::kLfoSourcesStateId).getChild(0);
        PX3SynthAudioProcessor restored;
        juce::String error;
        check("ModGraph_CatalogIsTheOnlyLfoParameterStateAuthority",
              ! lfo.hasProperty(px3::processor_internal::kLfoFrequencyId)
                  && ! lfo.hasProperty(px3::processor_internal::kLfoEnabledId)
                  && ! lfo.hasProperty(px3::processor_internal::kLfoWaveformId)
                  && restored.applyParameterStateTree(state, &error)
                  && std::abs(restored.getLfoFrequencyParam(0).get() - 4.5f) < 1.0e-4f, error);
    }
    {
        PX3SynthAudioProcessor processor;
        auto assigned = 0;
        juce::String overflow;
        for (const auto& entry : processor.getParameterCatalog().entries())
        {
            if (! entry.modulationDestination || entry.sourceControl) { continue; }
            if (assigned < 64)
            {
                if (processor.toggleMacroDestination(0, entry.id)) { ++assigned; }
            }
            else { overflow = entry.id; break; }
        }
        const auto rejected = overflow.isNotEmpty() && ! processor.toggleMacroDestination(0, overflow);
        check("ModGraph_MacroCapacityRejectsWithoutChangingAuthoringOrRuntime",
              assigned == 64 && rejected && processor.getMacroDestinations(0).size() == 64
                  && ! processor.isMacroDestination(0, overflow));
        const auto first = processor.getMacroDestinations(0).front().parameterId;
        const auto depth = processor.getMacroDestinationDepth(0, first);
        check("ModGraph_NonFiniteMacroDepthDoesNotReplaceLiveDepth",
              ! processor.setMacroDestinationDepth(0, first, std::numeric_limits<float>::quiet_NaN())
                  && processor.getMacroDestinationDepth(0, first) == depth);
    }
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        prepareUpgrade(processor);
        std::atomic<bool> done { false };
        std::atomic<bool> writerOk { true };
        std::thread writer([&]
        {
            for (int edit = 0; edit < 256; ++edit)
            {
                juce::String error;
                const auto destination = edit % 2 == 0 ? "voice.filter1.cutoff" : "mix.osc1.pan";
                if (! processor.setGraphRoute(0, { kMacro1Source, destination }, error)) { writerOk.store(false); }
                processor.createParameterStateTree();
            }
            done.store(true, std::memory_order_release);
        });
        auto finite = true;
        auto audible = false;
        auto firstBlock = true;
        juce::AudioBuffer<float> buffer(2, kBlockSize);
        while (! done.load(std::memory_order_acquire))
        {
            buffer.clear();
            juce::MidiBuffer midi;
            if (firstBlock) { midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0); firstBlock = false; }
            processor.processBlock(buffer, midi);
            audible = audible || buffer.getMagnitude(0, buffer.getNumSamples()) > 0.001f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    finite = finite && std::isfinite(buffer.getSample(channel, sample));
                }
            }
        }
        writer.join();
        check("ModGraph_RoutePublicationAndStateSnapshotsCoexistWithRendering", writerOk.load() && finite && audible);
    }
    {
        LfoGenerator generator;
        generator.prepare(48000.0);
        LfoSettings settings;
        settings.clockMode = LfoClockMode::tempo;
        settings.clockDivision = 4;
        settings.tempoBpm = 120.0;
        settings.clockAvailable = true;
        generator.setSettings(settings);
        generator.getMidpointSignalAndAdvance(6000);
        check("LfoClock_TempoDivisionDerivesItsRateFromHostBeats",
              std::abs(generator.getPhaseRadians() - juce::MathConstants<float>::halfPi) < 1.0e-5f);
        settings.clockMode = LfoClockMode::transport;
        settings.transportPpq = 0.25;
        settings.transportPlaying = false;
        generator.setSettings(settings);
        const auto held = generator.getMidpointSignalAndAdvance(512);
        check("LfoClock_StoppedTransportHoldsThePpqPhase", std::abs(held - 1.0f) < 1.0e-6f);
        settings.clockAvailable = false;
        generator.setSettings(settings);
        check("LfoClock_MissingHostTimingDoesNotPretendToFreeRun", generator.getMidpointSignalAndAdvance(512) == 0.0f);
          settings.clockMode = LfoClockMode::tempo;
          settings.clockAvailable = true;
          settings.clockRateScale = 2.0f;
          generator.retrigger();
          generator.setSettings(settings);
          generator.getMidpointSignalAndAdvance(3000);
          check("LfoClock_SyncedRatesRetainGraphRateModulation",
              std::abs(generator.getPhaseRadians() - juce::MathConstants<float>::halfPi) < 1.0e-5f);
    }
    {
        class PlayHead final : public juce::AudioPlayHead
        {
        public:
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo position;
                position.setBpm(120.0);
                position.setPpqPosition(0.25);
                position.setIsPlaying(false);
                return position;
            }
        } playHead;
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "mod.lfo1.enabled", 1.0f);
        setParam(processor, "mod.lfo1.amount", 1.0f);
        setChoice(processor, "mod.lfo1.clock.mode", 2);
        processor.setLfoAssignmentByParameterId(0, "voice.filter1.cutoff", false);
        auto* cutoff = findParameter(processor, "voice.filter1.cutoff");
        cutoff->setValueNotifyingHost(0.5f);
        processor.setPlayHead(&playHead);
        prepareUpgrade(processor);
        runUpgradeBlocks(processor, 1);
        check("LfoClock_ProcessorConsumesHostPpqAndTempo",
              processor.isLfoClockAvailable(0)
                  && processor.getUnclampedModulatedNormalisedValue(*cutoff) > 0.99f);
        processor.setPlayHead(nullptr);
        runUpgradeBlocks(processor, 1);
        check("LfoClock_ProcessorReportsMissingHostWithoutFreeRateFallback",
              ! processor.isLfoClockAvailable(0)
                  && std::abs(processor.getUnclampedModulatedNormalisedValue(*cutoff) - cutoff->getValue()) < 1.0e-6f,
              "base=" + fmt(cutoff->getValue(), 8) + " effective="
                  + fmt(processor.getUnclampedModulatedNormalisedValue(*cutoff), 8));
        juce::String error;
        processor.setGraphRoute(0, { 0, "voice.filter1.cutoff", px3::synth::ModulationPolarity::unipolar }, error);
        auto& depth = processor.getGraphRouteDepthParam(0);
        depth.setValueNotifyingHost(depth.convertTo0to1(1.0f));
        runUpgradeBlocks(processor, 1);
        check("LfoClock_UnavailableClockDoesNotCreateAUnipolarOffset",
              std::abs(processor.getUnclampedModulatedNormalisedValue(*cutoff) - cutoff->getValue()) < 1.0e-6f);
    }
    testEnvelopeTimesUpgrade();
    testStateUpgrade();
    testRampsAndKeySyncUpgrade();
    testModulationRuleUpgrade();
    testFilterRoutingUpgrade();
}

} // namespace px3tests
