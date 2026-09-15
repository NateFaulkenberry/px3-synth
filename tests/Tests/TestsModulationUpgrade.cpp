#include "TestSupport.h"

// testModulationUpgrade - 0.7.5's modulation and filter routing work:
// envelope times to 40 s, timed ramps, key sync, the full-range modulation
// rule, the pitch-mod destinations, series/parallel filter routing, and the
// state migration that keeps sessions saved before any of it sounding the same.

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

// Loads a tree the way a host hands a session back.
void loadUpgradeTree(PX3SynthAudioProcessor& target, const juce::ValueTree& tree)
{
    if (auto xml = tree.createXml())
    {
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(*xml, block);
        target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    }
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

        for (const auto* prefix : { "amp", "env1", "env2", "env3" })
        {
            for (const auto* stage : { "Attack", "Decay", "Release" })
            {
                const auto id = juce::String(prefix) + stage;
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

        expect("ampAttack", 0.015f);
        expect("ampDecay", 0.300f);
        expect("ampSustain", 0.8f);
        expect("ampRelease", 0.500f);
        for (const auto* slot : { "1", "2", "3" })
        {
            expect(juce::String("env") + slot + "Attack", 0.250f);
            expect(juce::String("env") + slot + "Decay", 0.600f);
            expect(juce::String("env") + slot + "Sustain", 0.7f);
            expect(juce::String("env") + slot + "Release", 1.000f);
        }

        check("EnvUpgrade_DefaultsAreTheDocumentedValues", wrong.isEmpty(),
              wrong.isEmpty() ? "amp 15 ms / 300 ms / 0.8 / 500 ms; mod 250 ms / 600 ms / 0.7 / 1 s"
                              : wrong.joinIntoString(", "));
    }

    // Widening the range must not cost the fast end its resolution: a quarter of
    // the knob is still percussive territory.
    {
        PX3SynthAudioProcessor processor;
        const auto& range = findParameter(processor, "ampAttack")->getNormalisableRange();
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
    // A version-11 session stored envelope times and LFO waveforms normalised
    // under the OLD ranges. They have to come back as the same seconds and the
    // same shape.
    {
        PX3SynthAudioProcessor source;
        auto tree = source.createParameterStateTree();
        tree.setProperty("stateVersion", 11, nullptr);

        const juce::NormalisableRange<float> oldAttack(0.0f, 3.0f, 0.001f, 0.45f);
        const juce::NormalisableRange<float> oldDecay(0.0f, 4.0f, 0.001f, 0.45f);
        const juce::NormalisableRange<float> oldRelease(0.0f, 5.0f, 0.001f, 0.45f);
        tree.setProperty("ampAttack", oldAttack.convertTo0to1(0.250f), nullptr);
        tree.setProperty("ampRelease", oldRelease.convertTo0to1(1.100f), nullptr);
        tree.setProperty("env2Decay", oldDecay.convertTo0to1(2.000f), nullptr);
        tree.setProperty("env3Release", oldRelease.convertTo0to1(5.000f), nullptr);
        tree.setProperty("lfoWaveform", 1.0f, nullptr);          // SQUARE, last of four
        tree.setProperty("lfo2Waveform", 1.0f / 3.0f, nullptr);  // TRIANGLE

        // The LFO source children carry the waveform as an INDEX and would
        // restore it on their own, hiding whether the parameter path migrates.
        for (int i = tree.getNumChildren(); --i >= 0;)
        {
            if (tree.getChild(i).getType().toString().containsIgnoreCase("lfo"))
            {
                tree.removeChild(i, nullptr);
            }
        }

        PX3SynthAudioProcessor target;
        loadUpgradeTree(target, tree);

        const auto attack = getParamValue(target, "ampAttack");
        const auto release = getParamValue(target, "ampRelease");
        const auto decay = getParamValue(target, "env2Decay");
        const auto longRelease = getParamValue(target, "env3Release");
        const auto timesKept = std::abs(attack - 0.250f) < 0.003f && std::abs(release - 1.100f) < 0.003f
                            && std::abs(decay - 2.000f) < 0.003f && std::abs(longRelease - 5.000f) < 0.003f;

        check("StateUpgrade_OldEnvelopeTimesKeepTheirSeconds", timesKept,
              "amp attack " + fmt(attack, 3) + " s (was 0.250), amp release " + fmt(release, 3)
                  + " s (was 1.100), env 2 decay " + fmt(decay, 3) + " s (was 2.000), env 3 release "
                  + fmt(longRelease, 3) + " s (was 5.000)");

        const auto square = upgradeChoiceIndex(target, "lfoWaveform");
        const auto triangle = upgradeChoiceIndex(target, "lfo2Waveform");
        check("StateUpgrade_OldLfoWaveformsKeepTheirShape", square == 3 && triangle == 1,
              "SQUARE came back as " + px3::lfoWaveformChoices()[juce::jmax(0, square)]
                  + ", TRIANGLE as " + px3::lfoWaveformChoices()[juce::jmax(0, triangle)]);
    }

    // A current state is NOT migrated: a 25 s release stays 25 s.
    {
        PX3SynthAudioProcessor source;
        setParam(source, "ampRelease", 25.0f);
        setParam(source, "env1Attack", 12.5f);
        setChoice(source, "lfoWaveform", 5);
        setParam(source, "lfoRampTime", 45.0f);
        setParam(source, "lfoKeySync", 1.0f);
        setParam(source, "osc2Fine", -7.0f);
        setChoice(source, "filterRouting", 1);
        setParam(source, "filterParallelBalance", 0.27f);

        juce::MemoryBlock block;
        source.getStateInformation(block);
        PX3SynthAudioProcessor target;
        target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));

        const auto ok = std::abs(getParamValue(target, "ampRelease") - 25.0f) < 0.01f
                     && std::abs(getParamValue(target, "env1Attack") - 12.5f) < 0.01f
                     && upgradeChoiceIndex(target, "lfoWaveform") == 5
                     && std::abs(getParamValue(target, "lfoRampTime") - 45.0f) < 0.01f
                     && getParamValue(target, "lfoKeySync") > 0.5f
                     && std::abs(getParamValue(target, "osc2Fine") + 7.0f) < 0.01f
                     && upgradeChoiceIndex(target, "filterRouting") == 1
                     && std::abs(getParamValue(target, "filterParallelBalance") - 0.27f) < 0.002f;

        check("StateUpgrade_NewValuesSurviveARoundTripUnmigrated", ok,
              "release " + fmt(getParamValue(target, "ampRelease"), 3) + " s, ramp "
                  + fmt(getParamValue(target, "lfoRampTime"), 2) + " s, waveform "
                  + juce::String(upgradeChoiceIndex(target, "lfoWaveform")) + ", routing "
                  + juce::String(upgradeChoiceIndex(target, "filterRouting")) + ", balance "
                  + fmt(getParamValue(target, "filterParallelBalance"), 3));
    }

    // A session from before the new controls existed means their defaults,
    // whatever the instance it is loaded into was set to.
    {
        PX3SynthAudioProcessor source;
        auto tree = source.createParameterStateTree();
        tree.setProperty("stateVersion", 11, nullptr);
        for (const auto* id : { "filterRouting", "filterParallelBalance",
                                "lfoRampTime", "lfo2RampTime", "lfo3RampTime",
                                "lfoKeySync", "lfo2KeySync", "lfo3KeySync",
                                "osc1PitchMod", "osc2PitchMod", "osc3PitchMod", "subOscPitchMod" })
        {
            tree.removeProperty(id, nullptr);
        }

        PX3SynthAudioProcessor target;
        setChoice(target, "filterRouting", 1);
        setParam(target, "filterParallelBalance", 0.9f);
        setParam(target, "lfoKeySync", 1.0f);
        setParam(target, "lfoRampTime", 30.0f);
        setParam(target, "osc1PitchMod", 12.0f);
        loadUpgradeTree(target, tree);

        const auto ok = upgradeChoiceIndex(target, "filterRouting") == 0
                     && std::abs(getParamValue(target, "filterParallelBalance") - 0.5f) < 1.0e-3f
                     && getParamValue(target, "lfoKeySync") < 0.5f
                     && std::abs(getParamValue(target, "lfoRampTime") - 4.0f) < 0.01f
                     && std::abs(getParamValue(target, "osc1PitchMod")) < 1.0e-3f;

        check("StateUpgrade_ASessionFromBeforeTheControlsLoadsTheirDefaults", ok,
              "routing " + juce::String(upgradeChoiceIndex(target, "filterRouting")) + ", balance "
                  + fmt(getParamValue(target, "filterParallelBalance"), 3) + ", key sync "
                  + fmt(getParamValue(target, "lfoKeySync"), 0) + ", ramp "
                  + fmt(getParamValue(target, "lfoRampTime"), 2) + " s, pitch mod "
                  + fmt(getParamValue(target, "osc1PitchMod"), 2) + " st");
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
        const auto ok = choices.size() == 6 && choices[0] == "SINE" && choices[1] == "TRIANGLE"
                     && choices[2] == "SAW" && choices[3] == "SQUARE"
                     && choices[rampUp] == "RAMP UP" && choices[rampDown] == "RAMP DOWN";
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
        for (const auto* id : { "lfoRampTime", "lfo2RampTime", "lfo3RampTime" })
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
        setParam(processor, "lfoEnabled", 1.0f);
        setParam(processor, "lfoFrequency", 0.5f);
        setParam(processor, "lfoAmount", 1.0f);
        setChoice(processor, "lfoWaveform", waveform);
        setParam(processor, "lfoRampTime", 1.0f);
        setParam(processor, "lfoKeySync", keySync ? 1.0f : 0.0f);
        processor.setLfoAssignmentByParameterId(0, "filter1Cutoff", false);
        findParameter(processor, "filter1Cutoff")->setValueNotifyingHost(0.5f);
        prepareUpgrade(processor);
    };
    // How far the LFO has the cutoff from its base, before the fold.
    const auto deviation = [](PX3SynthAudioProcessor& processor)
    {
        return processor.getUnclampedModulatedNormalisedValue(*findParameter(processor, "filter1Cutoff")) - 0.5f;
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
        setParam(processor, "lfoEnabled", 1.0f);
        setParam(processor, "lfoFrequency", 0.01f);   // stays in the square's high half throughout
        setParam(processor, "lfoAmount", 1.0f);
        setChoice(processor, "lfoWaveform", 3);
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
        auto* cutoff = findParameter(processor, "filter1Cutoff");
        cutoff->setValueNotifyingHost(cutoff->getDefaultValue());
        setParam(processor, "lfoEnabled", 1.0f);
        setParam(processor, "lfoFrequency", 2.0f);
        setParam(processor, "lfoAmount", 1.0f);
        setChoice(processor, "lfoWaveform", 0);
        processor.setLfoAssignmentByParameterId(0, "filter1Cutoff", false);
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
            setParam(processor, "lfoEnabled", 1.0f);
            setParam(processor, "lfoFrequency", 0.01f);
            setParam(processor, "lfoAmount", amount);
            setChoice(processor, "lfoWaveform", 3);
            processor.setLfoAssignmentByParameterId(0, "osc1PitchMod", false);
            prepareUpgrade(processor);
            runUpgradeBlocks(processor, 2);
            auto* pitchMod = findParameter(processor, "osc1PitchMod");
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
    setChoice(processor, "osc1Mode", oscMode);

    const auto apply = [&processor](const juce::String& prefix, const RoutingStage& stage)
    {
        setParam(processor, prefix + "Enabled", stage.enabled ? 1.0f : 0.0f);
        setChoice(processor, prefix + "Type", stage.type);
        setParam(processor, prefix + "Cutoff", stage.cutoffHz);
        setParam(processor, prefix + "Resonance", 0.707f);
    };
    apply("filter1", first);
    apply("filter2", second);
    setChoice(processor, "filterRouting", parallel ? 1 : 0);
    setParam(processor, "filterParallelBalance", balance);

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
        const auto routing = upgradeChoiceIndex(processor, "filterRouting");
        const auto balance = getParamValue(processor, "filterParallelBalance");
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
                                               if (block == kRoutingSwitchBlock) { setChoice(processor, "filterRouting", 1); }
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
                                               if (block == kRoutingSwitchBlock) { setParam(processor, "filterParallelBalance", 1.0f); }
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
        setChoice(source, "filterRouting", 1);
        setParam(source, "filterParallelBalance", 0.27f);
        juce::MemoryBlock block;
        source.getStateInformation(block);
        PX3SynthAudioProcessor target;
        target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        check("FilterRouting_SurvivesAStateRoundTrip",
              upgradeChoiceIndex(target, "filterRouting") == 1
                  && std::abs(getParamValue(target, "filterParallelBalance") - 0.27f) < 0.002f,
              "routing " + juce::String(upgradeChoiceIndex(target, "filterRouting")) + ", balance "
                  + fmt(getParamValue(target, "filterParallelBalance"), 3));
    }
}

} // namespace

void testModulationUpgrade()
{
    suite("MODULATION UPGRADE");
    testEnvelopeTimesUpgrade();
    testStateUpgrade();
    testRampsAndKeySyncUpgrade();
    testModulationRuleUpgrade();
    testFilterRoutingUpgrade();
}

} // namespace px3tests
