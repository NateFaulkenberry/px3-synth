#include "TestSupport.h"

// testPresets, testIntegration, testFxChain

namespace px3tests
{

//==============================================================================
// PHASE 13 / 14 / 15 - PRESET MANAGER AND STATE
//==============================================================================
// Applies a deliberately unusual configuration: many parameters away from their
// defaults, across every audited component, so a round trip that drops or
// mis-maps any of them is visible.
void applyUnusualConfiguration(PX3SynthAudioProcessor& processor)
{
    setParam(processor, "ampAttack", 0.731f);
    setParam(processor, "ampDecay", 1.234f);
    setParam(processor, "ampSustain", 0.371f);
    setParam(processor, "ampRelease", 2.510f);
    setParam(processor, "ampEnvEnabled", 1.0f);
    setParam(processor, "masterGain", 0.83f);
    setParam(processor, "pitchBendRange", 7.0f);

    setParam(processor, "osc1Enabled", 1.0f);
    setParam(processor, "osc2Enabled", 1.0f);
    setParam(processor, "osc3Enabled", 0.0f);
    setChoice(processor, "osc1Mode", 13);
    setChoice(processor, "osc2Mode", 6);
    setChoice(processor, "osc3Mode", 19);
    setParam(processor, "osc1Coarse", -7.0f);
    setParam(processor, "osc2Coarse", 5.0f);
    setParam(processor, "osc3Coarse", 12.0f);
    setParam(processor, "osc1Fine", -37.0f);
    setParam(processor, "osc2Fine", 63.0f);
    setParam(processor, "osc1Pitch", 0.17f);
    setParam(processor, "osc2MacroA", 0.234f);
    setParam(processor, "osc2MacroB", 0.876f);
    setParam(processor, "osc3MacroC", 0.412f);
    setChoice(processor, "osc1Vowel", 3);
    setParam(processor, "osc1H3", 0.913f);
    setParam(processor, "osc2H7", 0.041f);

    setParam(processor, "subOscEnabled", 1.0f);
    setChoice(processor, "subOscOctave", 2);
    setChoice(processor, "subOscWaveform", 0);
    setParam(processor, "subOscPitch", -0.19f);

    for (const auto* slot : { "1", "2" })
    {
        setParam(processor, juce::String("filter") + slot + "Enabled", 1.0f);
        setParam(processor, juce::String("filter") + slot + "Cutoff", slot[0] == '1' ? 733.0f : 4211.0f);
        setParam(processor, juce::String("filter") + slot + "Resonance", slot[0] == '1' ? 1.73f : 0.41f);

        // The comb's controls go through the same round trip as everything
        // else, so the state test above covers them without a second mechanism.
        setParam(processor, juce::String("filter") + slot + "CombTune", slot[0] == '1' ? 143.5f : 671.25f);
        setParam(processor, juce::String("filter") + slot + "CombDecay", slot[0] == '1' ? 2.35f : 0.44f);
        setParam(processor, juce::String("filter") + slot + "CombDamping", slot[0] == '1' ? 0.62f : 0.11f);
        setParam(processor, juce::String("filter") + slot + "CombDispersion", slot[0] == '1' ? 0.37f : 0.83f);
        setParam(processor, juce::String("filter") + slot + "CombDrive", slot[0] == '1' ? 0.29f : 0.71f);
        setParam(processor, juce::String("filter") + slot + "CombMix", slot[0] == '1' ? 0.66f : 0.24f);
        setParam(processor, juce::String("filter") + slot + "CombInvert", slot[0] == '1' ? 1.0f : 0.0f);
    }
    // The dry bus is a channel like any other, so the round trip has to carry
    // it too.
    setParam(processor, "mix.dry.level", 0.63f);
    setParam(processor, "mix.dry.pan", -0.42f);
    setParam(processor, "mix.dry.mute", 0.0f);
    setParam(processor, "mix.dry.solo", 1.0f);
    setParam(processor, "mix.dry.phase", 1.0f);

    setChoice(processor, "filter1Type", 4);
    // Comb, so the mode itself is part of what the round trip has to restore.
    setChoice(processor, "filter2Type", static_cast<int>(px3::FilterMode::comb));

    for (int envIndex = 0; envIndex < 3; ++envIndex)
    {
        const auto slot = juce::String(envIndex + 1);
        setParam(processor, "env" + slot + "Enabled", 1.0f);
        setParam(processor, "env" + slot + "Attack", 0.11f + 0.23f * static_cast<float>(envIndex));
        setParam(processor, "env" + slot + "Decay", 0.37f + 0.19f * static_cast<float>(envIndex));
        setParam(processor, "env" + slot + "Sustain", 0.29f + 0.17f * static_cast<float>(envIndex));
        setParam(processor, "env" + slot + "Release", 1.13f + 0.41f * static_cast<float>(envIndex));
        setParam(processor, envIndex == 0 ? juce::String("envAmount") : "env" + slot + "Amount",
                 -0.63f + 0.44f * static_cast<float>(envIndex));
    }
    processor.setEnvelopeAssignmentByParameterId(0, "filter1Cutoff", false);
    processor.setEnvelopeAssignmentByParameterId(1, "osc1Level", false);
    processor.setEnvelopeAssignmentByParameterId(2, "filter2Resonance", false);

    for (int lfoIndex = 0; lfoIndex < 3; ++lfoIndex)
    {
        const auto slot = juce::String(lfoIndex + 1);
        const auto prefix = lfoIndex == 0 ? juce::String("lfo") : "lfo" + slot;
        setParam(processor, lfoIndex == 0 ? juce::String("lfoEnabled") : prefix + "Enabled", 1.0f);
        setParam(processor, lfoIndex == 0 ? juce::String("lfoFrequency") : prefix + "Frequency",
                 0.73f + 3.19f * static_cast<float>(lfoIndex));
        setParam(processor, lfoIndex == 0 ? juce::String("lfoAmount") : prefix + "Amount",
                 -0.81f + 0.57f * static_cast<float>(lfoIndex));
        setChoice(processor, lfoIndex == 0 ? juce::String("lfoWaveform") : prefix + "Waveform",
                  (lfoIndex + 2) % 4);
    }
    processor.setLfoAssignmentByParameterId(0, "filter2Cutoff", false);
    processor.setLfoAssignmentByParameterId(1, "mix.osc2.pan", false);
    processor.setLfoAssignmentByParameterId(2, "osc1Pitch", false);

    setParam(processor, "vibeEnabled", 1.0f);
    setParam(processor, "vibeAmount", 0.67f);
    setChoice(processor, "vibeType", 2);

    setParam(processor, "reverbEnabled", 1.0f);
    setParam(processor, "reverbAmount", 0.71f);
    setChoice(processor, "reverbAlgorithm", 3);
    setParam(processor, "reverbSize", 0.83f);
    setParam(processor, "reverbDecay", 0.29f);
    setParam(processor, "reverbDamping", 0.77f);
    setParam(processor, "reverbPreDelay", 0.43f);
    setParam(processor, "reverbWidth", 0.19f);
    setParam(processor, "reverbCloudFeedback", 0.91f);
    setParam(processor, "reverbCloudDiffusion", 0.13f);

    setParam(processor, "moodEnabled", 1.0f);
    setParam(processor, "moodMix", 0.61f);
    setParam(processor, "moodClock", 0.23f);
    setParam(processor, "moodWetTime", 0.87f);
    setParam(processor, "moodFeedback", 0.44f);
    setParam(processor, "moodSpread", 0.71f);
    setParam(processor, "moodDegrade", 0.58f);
    setParam(processor, "moodFreeze", 0.0f);
    setChoice(processor, "moodWetMode", 2);
    setChoice(processor, "moodLoopMode", 1);

    setParam(processor, "delayEnabled", 1.0f);
    setParam(processor, "delayAmount", 0.39f);
    setParam(processor, "delayTime", 0.62f);
    setParam(processor, "delayFeedback", 0.47f);

    setParam(processor, "fxSendGain", 0.73f);
    setParam(processor, "fxReturnGain", 0.58f);
    setParam(processor, "mix.fx.pan", -0.41f);
    setParam(processor, "mix.fx.mute", 0.0f);

    const float levels[] = { 0.31f, 0.79f, 0.52f, 0.94f };
    const float pans[] = { -0.87f, 0.23f, 0.61f, -0.34f };
    const float sends[] = { 0.11f, 0.83f, 0.37f, 0.66f };
    int index = 0;
    for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
    {
        setParam(processor, juce::String("mix.") + id + ".level", levels[index]);
        setParam(processor, juce::String("mix.") + id + ".pan", pans[index]);
        setParam(processor, juce::String("mix.") + id + ".fxSend", sends[index]);
        ++index;
    }
    setParam(processor, "mix.osc3.mute", 1.0f);
    setParam(processor, "mix.osc2.solo", 0.0f);

    processor.setFxProcessingOrder({ { 3, 1, 0, 2 } });
}

// Every host-visible parameter, as normalised values, so a comparison covers
// controls the test author did not think to name.
std::vector<std::pair<juce::String, float>> snapshotParameters(PX3SynthAudioProcessor& processor)
{
    std::vector<std::pair<juce::String, float>> snapshot;
    for (auto* parameter : processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            snapshot.emplace_back(ranged->paramID, ranged->getValue());
        }
    }
    return snapshot;
}

void testPresets()
{
    suite("PRESET / STATE");

    // The comb's parameters specifically. The round trip below already compares
    // every parameter, but a failure there names one index out of hundreds;
    // this says which control was lost.
    {
        PX3SynthAudioProcessor processor;
        applyUnusualConfiguration(processor);

        const std::array<juce::String, 7> combIds { {
            "CombTune", "CombDecay", "CombDamping", "CombDispersion",
            "CombDrive", "CombMix", "CombInvert",
        } };

        std::vector<std::pair<juce::String, float>> before;
        for (int filterIndex = 1; filterIndex <= kFilterInstanceCount; ++filterIndex)
        {
            for (const auto& id : combIds)
            {
                const auto full = "filter" + juce::String(filterIndex) + id;
                if (auto* param = findParameter(processor, full))
                {
                    before.push_back({ full, param->getValue() });
                }
            }
        }

        juce::MemoryBlock state;
        processor.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        juce::StringArray lost;
        for (const auto& entry : before)
        {
            auto* param = findParameter(restored, entry.first);
            if (param == nullptr)
            {
                lost.add(entry.first + " (missing)");
            }
            else if (std::abs(param->getValue() - entry.second) > 1.0e-5f)
            {
                lost.add(entry.first);
            }
        }

        check("Preset_CombParametersSurviveTheRoundTrip",
              lost.isEmpty() && before.size() == combIds.size() * kFilterInstanceCount,
              lost.isEmpty() ? juce::String(static_cast<int>(before.size()))
                                   + " comb parameters restored exactly"
                             : "lost: " + lost.joinIntoString(", "));

        check("Preset_CombModeSurvivesTheRoundTrip",
              restored.getFilterTypeParam(1).getIndex() == static_cast<int>(px3::FilterMode::comb),
              "filter 2 restored as mode index "
                  + juce::String(restored.getFilterTypeParam(1).getIndex()));
    }

    // Full DAW-session round trip: save, reset to defaults, restore, compare
    // every parameter.
    {
        PX3SynthAudioProcessor processor;
        applyUnusualConfiguration(processor);
        const auto before = snapshotParameters(processor);
        const auto orderBefore = processor.getFxProcessingOrder();
        const auto envAssignmentsBefore = std::array<int, 3> {
            processor.getEnvelopeAssignmentIndex(0),
            processor.getEnvelopeAssignmentIndex(1),
            processor.getEnvelopeAssignmentIndex(2)
        };
        const auto lfoAssignmentsBefore = std::array<int, 3> {
            processor.getLfoAssignmentIndex(0),
            processor.getLfoAssignmentIndex(1),
            processor.getLfoAssignmentIndex(2)
        };

        juce::MemoryBlock state;
        processor.getStateInformation(state);
        check("Preset_StateSerialisesToNonEmptyPayload", state.getSize() > 0,
              juce::String(static_cast<int>(state.getSize())) + " bytes");

        // A fresh processor is at defaults; restoring must reproduce the patch.
        PX3SynthAudioProcessor restored;
        const auto defaults = snapshotParameters(restored);
        int differingFromDefaults = 0;
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            if (std::abs(before[i].second - defaults[i].second) > 1.0e-6f) ++differingFromDefaults;
        }
        check("Preset_TestConfigurationActuallyDiffersFromDefaults",
              differingFromDefaults > 50,
              juce::String(differingFromDefaults) + " of "
                  + juce::String(static_cast<int>(before.size())) + " parameters moved");

        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        const auto after = snapshotParameters(restored);

        int mismatches = 0;
        juce::String firstMismatch;
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            if (before[i].first != after[i].first)
            {
                ++mismatches;
                continue;
            }
            if (std::abs(before[i].second - after[i].second) > 1.0e-5f)
            {
                ++mismatches;
                if (firstMismatch.isEmpty())
                {
                    firstMismatch = before[i].first + " " + fmt(before[i].second, 6)
                                    + " -> " + fmt(after[i].second, 6);
                }
            }
        }
        check("Preset_RoundTripRestoresAllParameters", mismatches == 0,
              mismatches == 0
                  ? juce::String(static_cast<int>(before.size())) + " parameters matched"
                  : juce::String(mismatches) + " mismatched, first: " + firstMismatch);

        check("Preset_RoundTripRestoresFxProcessingOrder",
              restored.getFxProcessingOrder() == orderBefore);
        check("Preset_RoundTripRestoresEnvelopeAssignments",
              restored.getEnvelopeAssignmentIndex(0) == envAssignmentsBefore[0]
                  && restored.getEnvelopeAssignmentIndex(1) == envAssignmentsBefore[1]
                  && restored.getEnvelopeAssignmentIndex(2) == envAssignmentsBefore[2]);
        check("Preset_RoundTripRestoresLfoAssignments",
              restored.getLfoAssignmentIndex(0) == lfoAssignmentsBefore[0]
                  && restored.getLfoAssignmentIndex(1) == lfoAssignmentsBefore[1]
                  && restored.getLfoAssignmentIndex(2) == lfoAssignmentsBefore[2]);
    }

    // The restored patch must also SOUND the same, which parameter equality
    // alone does not guarantee.
    {
        PX3SynthAudioProcessor original;
        applyUnusualConfiguration(original);
        // FX and modes that draw from the shared system Random are switched off
        // for this comparison so the two renders are comparable at all.
        setParam(original, "reverbEnabled", 0.0f);
        setParam(original, "moodEnabled", 0.0f);
        setParam(original, "delayEnabled", 0.0f);
        setChoice(original, "osc1Mode", 1);
        setChoice(original, "osc2Mode", 3);
        setParam(original, "mix.osc3.mute", 0.0f);

        juce::MemoryBlock state;
        original.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        // Compared against the patch's own run-to-run variation. Voice start
        // phase is seeded from a global note counter, so two renders of the
        // same patch are never sample-identical; the question is whether the
        // restored patch differs by MORE than the patch differs from itself.
        const auto originalA = render(original, 48000, { { 2000, true, 45, 0.9f } });
        const auto originalB = render(original, 48000, { { 2000, true, 45, 0.9f } });
        const auto restoredCapture = render(restored, 48000, { { 2000, true, 45, 0.9f } });

        const auto selfVariation = std::abs(originalA.rms() - originalB.rms());
        const auto restoredDelta = std::abs(originalA.rms() - restoredCapture.rms());
        check("Preset_RoundTripPreservesRenderedAudioLevel",
              restoredDelta <= juce::jmax(selfVariation * 2.0, originalA.rms() * 0.05),
              "original rms " + fmt(originalA.rms(), 6) + ", restored " + fmt(restoredCapture.rms(), 6)
                  + "; restored differs by " + fmt(restoredDelta, 6)
                  + " vs self-variation " + fmt(selfVariation, 6));
    }

    // Malformed input must be rejected safely and must not corrupt live state.
    {
        auto survivesPayload = [](const char* name, const void* data, int size)
        {
            PX3SynthAudioProcessor processor;
            applyUnusualConfiguration(processor);
            const auto before = snapshotParameters(processor);
            processor.setStateInformation(data, size);
            const auto after = snapshotParameters(processor);

            // Either the state is unchanged (rejected) or it is some valid
            // state; what must never happen is a non-finite or out-of-range
            // parameter, or a crash.
            bool valid = true;
            juce::String offender;
            for (const auto& entry : after)
            {
                if (! std::isfinite(entry.second) || entry.second < -0.001f || entry.second > 1.001f)
                {
                    valid = false;
                    if (offender.isEmpty()) offender = entry.first + "=" + fmt(entry.second, 6);
                }
            }
            const auto capture = render(processor, 16000, { { 1000, true, 57, 0.9f } });
            const auto audioFinite = capture.isFinite();
            check(name, valid && audioFinite && before.size() == after.size(),
                  juce::String(before.size() == after.size() ? "parameter set intact" : "PARAMETER SET CHANGED")
                      + (valid ? ", all values finite and in range" : ", BAD PARAMETER " + offender)
                      + (audioFinite ? ", audio finite" : ", AUDIO NOT FINITE")
                      + ", peak " + fmt(capture.peak(), 6));
        };

        survivesPayload("Preset_MalformedNullPayloadIsRejectedSafely", nullptr, 0);

        const char garbage[] = "this is not a plugin state payload at all, not even close";
        survivesPayload("Preset_GarbagePayloadIsRejectedSafely", garbage, static_cast<int>(sizeof(garbage)));

        // A truncated version of a genuine payload.
        {
            PX3SynthAudioProcessor source;
            applyUnusualConfiguration(source);
            juce::MemoryBlock good;
            source.getStateInformation(good);
            survivesPayload("Preset_TruncatedPayloadIsRejectedSafely",
                            good.getData(), static_cast<int>(good.getSize() / 3));
        }

        // Well-formed XML carrying hostile values: NaN, infinity, out-of-range
        // numbers, an unknown parameter and a wrong-typed field.
        {
            PX3SynthAudioProcessor source;
            applyUnusualConfiguration(source);
            auto tree = source.createParameterStateTree();
            tree.setProperty("ampSustain", std::numeric_limits<double>::quiet_NaN(), nullptr);
            tree.setProperty("ampAttack", std::numeric_limits<double>::infinity(), nullptr);
            tree.setProperty("masterGain", -17.5, nullptr);
            tree.setProperty("filter1Cutoff", 9999.0, nullptr);
            tree.setProperty("aParameterThatDoesNotExist", 0.5, nullptr);
            tree.setProperty("osc1Coarse", "not a number", nullptr);
            if (auto xml = tree.createXml())
            {
                juce::MemoryBlock block;
                juce::AudioProcessor::copyXmlToBinary(*xml, block);
                survivesPayload("Preset_HostileValuesAreClampedOrRejectedSafely",
                                block.getData(), static_cast<int>(block.getSize()));
            }
        }

        // A preset saved before moodTrueBypass was removed still carries that
        // property. Loading one must apply everything else normally and simply
        // ignore the retired entry - this is the compatibility question the
        // removal turns on, so it is tested directly rather than inferred from
        // the generic unknown-property case.
        {
            PX3SynthAudioProcessor source;
            applyUnusualConfiguration(source);
            auto tree = source.createParameterStateTree();
            tree.setProperty("moodTrueBypass", 1.0, nullptr);
            const auto expected = snapshotParameters(source);

            if (auto xml = tree.createXml())
            {
                juce::MemoryBlock block;
                juce::AudioProcessor::copyXmlToBinary(*xml, block);

                PX3SynthAudioProcessor target;
                target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
                const auto actual = snapshotParameters(target);

                int mismatches = 0;
                juce::String firstMismatch;
                for (std::size_t i = 0; i < expected.size(); ++i)
                {
                    if (std::abs(expected[i].second - actual[i].second) > 1.0e-5f)
                    {
                        ++mismatches;
                        if (firstMismatch.isEmpty())
                        {
                            firstMismatch = expected[i].first + " " + fmt(expected[i].second, 6)
                                            + " -> " + fmt(actual[i].second, 6);
                        }
                    }
                }
                check("Preset_SavedBeforeTrueBypassRemovalStillLoadsCompletely",
                      mismatches == 0,
                      mismatches == 0
                          ? juce::String("retired moodTrueBypass entry ignored, all ")
                                + juce::String(static_cast<int>(expected.size()))
                                + " live parameters restored"
                          : juce::String(mismatches) + " mismatched, first: " + firstMismatch);
            }
        }

        // A payload with most parameters simply missing.
        {
            juce::ValueTree sparse("PX3State");
            sparse.setProperty("masterGain", 0.5, nullptr);
            if (auto xml = sparse.createXml())
            {
                juce::MemoryBlock block;
                juce::AudioProcessor::copyXmlToBinary(*xml, block);
                survivesPayload("Preset_MissingParametersAreHandledSafely",
                                block.getData(), static_cast<int>(block.getSize()));
            }
        }
    }

    // Preset FILE round trip through PresetManager, into a temporary directory
    // so the user's own preset library is never touched.
    {
        PX3SynthAudioProcessor processor;
        applyUnusualConfiguration(processor);
        const auto before = snapshotParameters(processor);

        auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("px3-component-tests");
        tempDirectory.createDirectory();
        const auto presetFile = tempDirectory.getChildFile("round-trip.px3preset");
        presetFile.deleteFile();

        PresetManager manager(processor);
        juce::String error;
        PresetManager::PresetMetadata metadata;
        metadata.name = "Round Trip";
        metadata.category = "Test";
        metadata.author = "component tests";

        int serializedBytes = 0;
        const auto saved = manager.dumpCurrentStateToPresetFile(presetFile, metadata, true, true,
                                                                error, &serializedBytes);
        check("Preset_SaveWritesAPresetFile", saved && presetFile.existsAsFile(),
              saved ? juce::String(serializedBytes) + " bytes" : "error: " + error);

        if (saved)
        {
            PX3SynthAudioProcessor target;
            PresetManager targetManager(target);
            juce::String loadError;
            const auto loaded = targetManager.loadPresetFile(presetFile, loadError);
            check("Preset_LoadReadsAPresetFile", loaded, loaded ? juce::String() : "error: " + loadError);

            if (loaded)
            {
                const auto after = snapshotParameters(target);
                int mismatches = 0;
                juce::String firstMismatch;
                for (std::size_t i = 0; i < before.size(); ++i)
                {
                    if (std::abs(before[i].second - after[i].second) > 1.0e-5f)
                    {
                        ++mismatches;
                        if (firstMismatch.isEmpty())
                        {
                            firstMismatch = before[i].first + " " + fmt(before[i].second, 6)
                                            + " -> " + fmt(after[i].second, 6);
                        }
                    }
                }
                check("Preset_FileRoundTripRestoresAllParameters", mismatches == 0,
                      mismatches == 0 ? juce::String("all parameters matched")
                                      : juce::String(mismatches) + " mismatched, first: " + firstMismatch);
            }

            // Overwriting an existing preset must succeed when asked to.
            juce::String overwriteError;
            const auto overwritten = manager.dumpCurrentStateToPresetFile(presetFile, metadata, true, false,
                                                                          overwriteError, nullptr);
            check("Preset_OverwriteExistingFileSucceeds", overwritten,
                  overwritten ? juce::String() : "error: " + overwriteError);

            // Refusing to overwrite must be honoured.
            juce::String refuseError;
            const auto refused = manager.dumpCurrentStateToPresetFile(presetFile, metadata, false, false,
                                                                      refuseError, nullptr);
            check("Preset_OverwriteIsRefusedWhenNotRequested", ! refused,
                  refused ? "file was overwritten without permission" : "refused: " + refuseError);
        }

        // A corrupt preset file must fail to load rather than crash or apply.
        {
            const auto corruptFile = tempDirectory.getChildFile("corrupt.px3preset");
            corruptFile.replaceWithText("<<<<not xml at all >>>> \x01\x02\x03");
            PX3SynthAudioProcessor target;
            applyUnusualConfiguration(target);
            const auto before2 = snapshotParameters(target);
            PresetManager targetManager(target);
            juce::String loadError;
            const auto loaded = targetManager.loadPresetFile(corruptFile, loadError);
            const auto after2 = snapshotParameters(target);
            bool unchanged = true;
            for (std::size_t i = 0; i < before2.size(); ++i)
            {
                if (std::abs(before2[i].second - after2[i].second) > 1.0e-6f) unchanged = false;
            }
            check("Preset_CorruptFileFailsToLoadAndLeavesStateIntact",
                  ! loaded && unchanged,
                  loaded ? "corrupt file reported success" : "rejected: " + loadError);
            corruptFile.deleteFile();
        }

        presetFile.deleteFile();
        tempDirectory.deleteRecursively();
    }
}
//==============================================================================
// PHASE 18 / 19 / 20 / 21 - LIFECYCLE, POLYPHONY, EDGE CASES, ARTIFACTS
//==============================================================================
void testIntegration()
{
    suite("INTEGRATION / LIFECYCLE / EDGE CASES");

    // Reset and reuse: prepare, render, reset, render again must give a
    // comparable result rather than degraded or silent output.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        const auto first = render(processor, 32000, { { 2000, true, 57, 0.9f } });
        processor.reset();
        const auto second = render(processor, 32000, { { 2000, true, 57, 0.9f } });
        check("Processor_ResetThenRenderAgainProducesComparableAudio",
              second.rms() > first.rms() * 0.8 && second.rms() < first.rms() * 1.2
                  && second.isFinite(),
              "before reset " + fmt(first.rms(), 5) + ", after reset " + fmt(second.rms(), 5));
    }

    // Note A, release, note B on the same voice: the second note must not carry
    // the first one's tail or state.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 0);
        setParam(processor, "ampRelease", 0.020f);
        const auto capture = render(processor, 96000,
                                    { { 2000, true, 57, 0.9f },
                                      { 20000, false, 57, 0.0f },
                                      { 48000, true, 69, 0.9f },
                                      { 70000, false, 69, 0.0f } });
        // Between the two notes the synth must reach silence.
        const auto gap = capture.rmsOver(30000, 46000);
        check("VoiceReuse_SilenceBetweenNotesIsComplete", gap < 1.0e-5,
              "rms in the gap between notes " + fmt(gap, 8));

        const auto secondNoteHz = estimateFrequency(capture.left, 56000, 12000, 200.0, 900.0);
        check("VoiceReuse_SecondNoteTracksItsOwnPitch",
              nearly(secondNoteHz, 440.0, 12.0),
              "second note expected 440 Hz, measured " + fmt(secondNoteHz, 2) + " Hz");
    }

    // Polyphonic isolation: releasing one voice must not disturb another that
    // is still held.
    {
        auto renderHeldVoice = [](bool withSecondVoice)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "ampRelease", 0.300f);
            std::vector<NoteEvent> events { { 2000, true, 57, 0.9f } };
            if (withSecondVoice)
            {
                // A second voice that starts and is released mid-render.
                events.push_back({ 4000, true, 72, 0.9f });
                events.push_back({ 30000, false, 72, 0.0f });
            }
            return render(processor, 96000, events);
        };

        const auto alone = renderHeldVoice(false);
        const auto withNeighbour = renderHeldVoice(true);

        // Pitch is the property that must be untouched: a shared or corrupted
        // oscillator state would show here immediately.
        const auto aloneHz = estimateFrequency(alone.left, 70000, 20000, 100.0, 500.0);
        const auto neighbourHz = estimateFrequency(withNeighbour.left, 70000, 20000, 100.0, 500.0);
        check("Polyphony_HeldVoiceKeepsItsPitchWhenANeighbourIsReleased",
              nearly(aloneHz, neighbourHz, 0.5) && nearly(aloneHz, 220.0, 6.0),
              "held voice alone " + fmt(aloneHz, 2) + " Hz, with a neighbour released "
                  + fmt(neighbourHz, 2) + " Hz");

        check("Polyphony_HeldVoiceKeepsSoundingWhenANeighbourIsReleased",
              withNeighbour.rmsOver(70000, 94000) > 0.01,
              "held-voice rms after the neighbour released "
                  + fmt(withNeighbour.rmsOver(70000, 94000), 5));

        // Absolute LEVEL is deliberately shared: the polyphony gain responds to
        // total voice load and, by design, recovers over about 2.5 s so a
        // decaying tail cannot lift its own gain back up. So a neighbour coming
        // and going does move a held voice's level - what must be true is that
        // the disturbance is transient, and the level returns to the solo value
        // once the gain has recovered.
        auto renderLong = [](bool withSecondVoice)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "ampRelease", 0.300f);
            std::vector<NoteEvent> events { { 2000, true, 57, 0.9f } };
            if (withSecondVoice)
            {
                events.push_back({ 4000, true, 72, 0.9f });
                events.push_back({ 30000, false, 72, 0.0f });
            }
            return render(processor, 576000, events); // 12 s, well past recovery
        };
        const auto longAlone = renderLong(false);
        const auto longWithNeighbour = renderLong(true);
        const auto settledAlone = longAlone.rmsOver(480000, 570000);
        const auto settledWithNeighbour = longWithNeighbour.rmsOver(480000, 570000);
        check("Polyphony_HeldVoiceLevelRecoversAfterANeighbourIsReleased",
              nearly(settledAlone, settledWithNeighbour, settledAlone * 0.05),
              "settled rms alone " + fmt(settledAlone, 5)
                  + ", after a neighbour came and went " + fmt(settledWithNeighbour, 5));
    }

    // Maximum polyphony must stay finite and bounded.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "osc2Enabled", 1.0f);
        setParam(processor, "osc3Enabled", 1.0f);
        setParam(processor, "subOscEnabled", 1.0f);
        setParam(processor, "ampRelease", 3.0f);
        std::vector<NoteEvent> events;
        for (int voice = 0; voice < 64; ++voice)
        {
            events.push_back({ 2000 + voice * 200, true, 24 + (voice % 72), 0.9f });
        }
        const auto capture = render(processor, 96000, events);
        check("Polyphony_MaximumVoicesStayFiniteAndBounded",
              capture.isFinite() && capture.peak() <= 1.0001,
              "peak " + fmt(capture.peak(), 5) + ", rms " + fmt(capture.rms(), 5));
    }

    {
        // The pool is 64 voices, but the number allowed to SOUND at once is
        // budgeted against the sample rate: a voice costs the same work
        // whatever the rate, while the time available to compute it halves as
        // the rate doubles. Measured with every effect, the analog console and
        // vibe enabled, 64 held voices took 108.7% of the block budget at
        // 48 kHz and 211.2% at 96 kHz - the whole block late, rather than the
        // quietest note being dropped.
        juce::String detail;
        auto sensible = true;

        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            const auto budget = PX3SynthAudioProcessor::soundingVoiceBudgetForRate(rate);
            detail << juce::String(rate / 1000.0, 1) << "k:" << budget << "  ";

            // Never more than the reference budget, never below the floor, and
            // never more than the pool can supply.
            sensible = sensible
                       && budget >= PX3SynthAudioProcessor::kMinimumSoundingVoiceBudget
                       && budget <= PX3SynthAudioProcessor::kSoundingVoiceBudgetAtReference
                       && budget <= PX3SynthAudioProcessor::kPolyphonyVoiceCount;
        }

        // Higher rate, fewer voices - the whole point of scaling it.
        sensible = sensible
                   && PX3SynthAudioProcessor::soundingVoiceBudgetForRate(96000.0)
                          < PX3SynthAudioProcessor::soundingVoiceBudgetForRate(48000.0);

        check("Voices_BudgetScalesWithTheSampleRate", sensible, detail);
    }

    {
        // Holding far more notes than the budget must stay clean: the excess is
        // faded out through the same path the tail pruner uses, so there is no
        // step in the summed output, and the result must not be silence either.
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "vibeEnabled", 1.0f);
        setParam(processor, "vibeAmount", 0.7f);
        setParam(processor, "analogEnabled", 1.0f);
        setParam(processor, "filter1Enabled", 1.0f);

        std::vector<NoteEvent> chord;
        for (int i = 0; i < 64; ++i)
        {
            chord.push_back({ 500 + i * 40, true, 24 + i, 0.8f });
        }

        const auto capture = render(processor, 96000, chord);

        auto worstStep = 0.0f;
        for (std::size_t i = 40001; i < capture.left.size(); ++i)
        {
            worstStep = juce::jmax(worstStep,
                                   std::abs(capture.left[i] - capture.left[i - 1]));
        }

        check("Voices_HoldingMoreNotesThanTheBudgetStaysCleanAndAudible",
              capture.isFinite() && capture.peak() <= 1.0001 && capture.rms() > 0.01
                  && worstStep < 0.35f,
              "64 held notes against the budget: rms " + fmt(capture.rms(), 5)
                  + ", peak " + fmt(capture.peak(), 5)
                  + ", worst sample step " + fmt(worstStep, 5));
    }

    // Voice stealing under sustained pressure.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "ampRelease", 4.0f);
        std::vector<NoteEvent> events;
        for (int note = 0; note < 200; ++note)
        {
            events.push_back({ 1000 + note * 400, true, 30 + (note % 60), 0.9f });
            events.push_back({ 1000 + note * 400 + 300, false, 30 + (note % 60), 0.0f });
        }
        const auto capture = render(processor, 96000, events);
        check("VoiceStealing_UnderSustainedPressureStaysFiniteAndBounded",
              capture.isFinite() && capture.peak() <= 1.0001,
              "peak " + fmt(capture.peak(), 5) + ", rms " + fmt(capture.rms(), 5));
    }

    // Edge cases: parameter extremes across the whole synth at once.
    {
        struct EdgeCase { const char* name; std::function<void(PX3SynthAudioProcessor&)> apply; };
        const EdgeCase cases[] = {
            { "AllOscillatorsDisabled", [](PX3SynthAudioProcessor& p)
              {
                  for (int i = 1; i <= 3; ++i) setParam(p, "osc" + juce::String(i) + "Enabled", 0.0f);
                  setParam(p, "subOscEnabled", 0.0f);
              } },
            { "ShortestEnvelopes", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "ampAttack", 0.001f);
                  setParam(p, "ampDecay", 0.005f);
                  setParam(p, "ampSustain", 0.0f);
                  setParam(p, "ampRelease", 0.010f);
              } },
            { "LongestEnvelopes", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "ampAttack", 3.0f);
                  setParam(p, "ampDecay", 4.0f);
                  setParam(p, "ampSustain", 1.0f);
                  setParam(p, "ampRelease", 5.0f);
              } },
            { "MaximumResonanceBothFilters", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "filter1Enabled", 1.0f);
                  setParam(p, "filter2Enabled", 1.0f);
                  setParam(p, "filter1Resonance", 2.2f);
                  setParam(p, "filter2Resonance", 2.2f);
                  setParam(p, "filter1Cutoff", 80.0f);
                  setParam(p, "filter2Cutoff", 18000.0f);
              } },
            { "MaximumModulationEverywhere", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "filter1Enabled", 1.0f);
                  for (int i = 0; i < 3; ++i)
                  {
                      const auto slot = juce::String(i + 1);
                      setParam(p, "env" + slot + "Enabled", 1.0f);
                      setParam(p, i == 0 ? juce::String("envAmount") : "env" + slot + "Amount", 1.0f);
                      const auto prefix = i == 0 ? juce::String("lfo") : "lfo" + slot;
                      setParam(p, i == 0 ? juce::String("lfoEnabled") : prefix + "Enabled", 1.0f);
                      setParam(p, i == 0 ? juce::String("lfoAmount") : prefix + "Amount", 1.0f);
                      setParam(p, i == 0 ? juce::String("lfoFrequency") : prefix + "Frequency", 20.0f);
                  }
                  p.setLfoAssignmentByParameterId(0, "filter1Cutoff", false);
                  p.setLfoAssignmentByParameterId(1, "osc1Pitch", false);
                  p.setLfoAssignmentByParameterId(2, "mix.osc1.pan", false);
                  p.setEnvelopeAssignmentByParameterId(0, "filter1Resonance", false);
                  p.setEnvelopeAssignmentByParameterId(1, "osc1Level", false);
                  p.setEnvelopeAssignmentByParameterId(2, "filter2Cutoff", false);
              } },
            { "AllFxAtMaximum", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "reverbEnabled", 1.0f);
                  setParam(p, "reverbAmount", 1.0f);
                  setParam(p, "moodEnabled", 1.0f);
                  setParam(p, "moodMix", 1.0f);
                  setParam(p, "moodFeedback", 1.0f);
                  setParam(p, "delayEnabled", 1.0f);
                  setParam(p, "delayAmount", 1.0f);
                  setParam(p, "delayFeedback", 1.0f);
                  setParam(p, "vibeEnabled", 1.0f);
                  setParam(p, "vibeAmount", 1.0f);
                  setParam(p, "fxSendGain", 1.0f);
                  setParam(p, "fxReturnGain", 1.0f);
                  for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                  {
                      setParam(p, juce::String("mix.") + id + ".fxSend", 1.0f);
                  }
              } },
            { "EverythingAtMaximum", [](PX3SynthAudioProcessor& p)
              {
                  for (int i = 1; i <= 3; ++i)
                  {
                      setParam(p, "osc" + juce::String(i) + "Enabled", 1.0f);
                      setParam(p, "osc" + juce::String(i) + "MacroA", 1.0f);
                      setParam(p, "osc" + juce::String(i) + "MacroB", 1.0f);
                      setParam(p, "osc" + juce::String(i) + "MacroC", 1.0f);
                  }
                  setParam(p, "subOscEnabled", 1.0f);
                  setParam(p, "masterGain", 1.0f);
                  for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                  {
                      setParam(p, juce::String("mix.") + id + ".level", 1.0f);
                  }
                  setParam(p, "vibeEnabled", 1.0f);
                  setParam(p, "vibeAmount", 1.0f);
                  setParam(p, "reverbEnabled", 1.0f);
                  setParam(p, "reverbAmount", 1.0f);
                  setParam(p, "fxReturnGain", 1.0f);
              } },
        };

        for (const auto& edgeCase : cases)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            edgeCase.apply(processor);
            const auto capture = render(processor, 96000,
                                        { { 2000, true, 45, 0.9f },
                                          { 2000, true, 52, 0.9f },
                                          { 2000, true, 57, 0.9f },
                                          { 40000, false, 45, 0.0f },
                                          { 40000, false, 52, 0.0f },
                                          { 40000, false, 57, 0.0f } });
            check((juce::String("EdgeCase_") + edgeCase.name + "_StaysFiniteAndWithinCeiling").toRawUTF8(),
                  capture.isFinite() && capture.peak() <= 1.0001,
                  "peak " + fmt(capture.peak(), 5) + ", rms " + fmt(capture.rms(), 5)
                      + ", dc " + fmt(capture.dcOffset(), 6));
        }
    }

    // Enable/disable transitions while a note sounds must not step the signal.
    // A large sample-to-sample jump mid-note is an audible click; the threshold
    // is the largest step the same patch produces with no switching at all.
    {
        // An empty parameter id means "change nothing", which gives the control
        // reading: how much the signal steps on its own.
        auto stepWithSwitching = [](const juce::String& toggleParameterId)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 0); // SINE: nothing of its own to mask a step
            setParam(processor, "osc2Enabled", 1.0f);
            setParam(processor, "subOscEnabled", 1.0f);

            std::function<void(int)> perBlock;
            if (toggleParameterId.isNotEmpty())
            {
                perBlock = [&processor, toggleParameterId](int blockIndex)
                {
                    // Toggle every 20 blocks, well inside the sustained note.
                    if (blockIndex > 15 && blockIndex % 20 == 0)
                    {
                        setParam(processor, toggleParameterId, (blockIndex / 20) % 2 == 0 ? 1.0f : 0.0f);
                    }
                };
            }
            return render(processor, 64000, { { 2000, true, 57, 0.9f } }, perBlock);
        };

        const auto steady = stepWithSwitching({});
        const auto steadyStep = steady.maxStep(8000, 60000);

        struct SwitchCase { const char* name; juce::String parameterId; };
        const SwitchCase switches[] = {
            { "OscillatorEnable", "osc2Enabled" },
            { "SubOscillatorEnable", "subOscEnabled" },
            { "FilterEnable", "filter1Enabled" },
            { "ReverbEnable", "reverbEnabled" },
        };

        for (const auto& switchCase : switches)
        {
            const auto capture = stepWithSwitching(switchCase.parameterId);
            const auto step = capture.maxStep(8000, 60000);
            check((juce::String("Artifact_") + switchCase.name + "_ToggleDoesNotStepTheSignal").toRawUTF8(),
                  capture.isFinite() && step < juce::jmax(0.02, steadyStep * 3.0),
                  "largest step " + fmt(step, 6) + " vs steady " + fmt(steadyStep, 6));
        }
    }

    // Rapid parameter jumps must not produce non-finite output or overshoot.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "filter1Enabled", 1.0f);
        const auto capture = render(processor, 64000, { { 2000, true, 45, 0.9f } },
                                    [&processor](int blockIndex)
                                    {
                                        // Extremes on alternate blocks, across
                                        // several unrelated controls at once.
                                        const auto high = (blockIndex % 2) == 0;
                                        setParam(processor, "filter1Cutoff", high ? 18000.0f : 80.0f);
                                        setParam(processor, "filter1Resonance", high ? 2.2f : 0.25f);
                                        setParam(processor, "mix.osc1.level", high ? 1.0f : 0.0f);
                                        setParam(processor, "mix.osc1.pan", high ? 1.0f : -1.0f);
                                        setParam(processor, "masterGain", high ? 1.0f : 0.0f);
                                    });
        check("Artifact_RapidParameterJumpsStayFiniteAndWithinCeiling",
              capture.isFinite() && capture.peak() <= 1.0001,
              "peak " + fmt(capture.peak(), 5));
    }

    // Long release tails must terminate rather than hang.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "ampRelease", 1.0f);
        std::vector<NoteEvent> events;
        for (int note = 0; note < 24; ++note)
        {
            events.push_back({ 2000 + note * 800, true, 40 + note, 0.9f });
            events.push_back({ 2000 + note * 800 + 600, false, 40 + note, 0.0f });
        }
        // Rendered well past the last release plus the full release time.
        const auto capture = render(processor, 240000, events);
        check("LongRelease_AllTailsReachSilence", capture.rmsOver(200000, 238000) < 1.0e-5,
              "rms long after the last release " + fmt(capture.rmsOver(200000, 238000), 8));
    }

    // Sample-rate and block-size independence: the synth must render sensibly
    // at rates and block sizes other than the ones every other test uses.
    {
        struct Config { double sampleRate; int blockSize; };
        const Config configs[] = {
            { 44100.0, 64 }, { 44100.0, 512 }, { 48000.0, 32 },
            { 88200.0, 256 }, { 96000.0, 1024 }, { 192000.0, 128 }
        };
        for (const auto& config : configs)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "filter1Enabled", 1.0f);
            setParam(processor, "reverbEnabled", 1.0f);
            setParam(processor, "reverbAmount", 0.6f);
            processor.setPlayConfigDetails(0, 2, config.sampleRate, config.blockSize);
            processor.prepareToPlay(config.sampleRate, config.blockSize);

            juce::AudioBuffer<float> buffer(2, config.blockSize);
            const auto totalBlocks = static_cast<int>(config.sampleRate / config.blockSize);
            double peak = 0.0, energy = 0.0;
            juce::int64 count = 0;
            bool finite = true;
            for (int block = 0; block < totalBlocks; ++block)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                if (block == 2) midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.9f), 0);
                processor.processBlock(buffer, midi);
                for (int i = 0; i < config.blockSize; ++i)
                {
                    const auto v = buffer.getSample(0, i);
                    if (! std::isfinite(v)) finite = false;
                    peak = juce::jmax(peak, static_cast<double>(std::abs(v)));
                    energy += static_cast<double>(v) * v;
                    ++count;
                }
            }
            const auto rms = std::sqrt(energy / static_cast<double>(juce::jmax<juce::int64>(1, count)));
            check((juce::String("SampleRate_") + juce::String(static_cast<int>(config.sampleRate))
                   + "Hz_Block" + juce::String(config.blockSize) + "_RendersCorrectly").toRawUTF8(),
                  finite && rms > 0.005 && peak <= 1.0001,
                  "rms " + fmt(rms, 5) + ", peak " + fmt(peak, 5));
        }
    }
}

void testFxChain()
{
    suite("FX CHAIN");

    using namespace px3::ui;

    auto asVector = [](const px3::FxOrder& order)
    {
        return std::vector<int>(order.begin(), order.end());
    };
    auto asText = [](const std::vector<int>& order)
    {
        juce::String text;
        for (const auto entry : order)
        {
            text << juce::String(entry) << " ";
        }
        return text.trim();
    };

    // ---- reordering --------------------------------------------------------
    {
        const std::vector<int> start { 0, 1, 3, 2 };

        const auto forward = moveChainEntry(start, 0, 2);
        check("FxChain_MoveForwardSlidesThePassedEntriesBack",
              forward == std::vector<int>({ 1, 3, 0, 2 }),
              "0 1 3 2, move index 0 -> 2 gives " + asText(forward));

        const auto backward = moveChainEntry(start, 3, 0);
        check("FxChain_MoveBackwardSlidesThePassedEntriesForward",
              backward == std::vector<int>({ 2, 0, 1, 3 }),
              "0 1 3 2, move index 3 -> 0 gives " + asText(backward));

        check("FxChain_MoveToItsOwnIndexIsANoOp",
              moveChainEntry(start, 2, 2) == start, "");

        // A drag that ends outside the strip must not silently reorder into
        // whatever index clamping would have produced.
        check("FxChain_OutOfRangeMoveLeavesTheOrderAlone",
              moveChainEntry(start, -1, 2) == start && moveChainEntry(start, 1, 9) == start, "");

        // Every move is a permutation: no effect can be lost or duplicated.
        auto isPermutation = [&start](const std::vector<int>& order)
        {
            auto a = order;
            auto b = start;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            return a == b;
        };

        auto permutationHolds = true;
        for (int from = 0; from < 4; ++from)
        {
            for (int to = 0; to < 4; ++to)
            {
                permutationHolds = permutationHolds && isPermutation(moveChainEntry(start, from, to));
            }
        }
        check("FxChain_EveryMoveIsAPermutation", permutationHolds,
              "all 16 from/to pairs preserve the set of effects");
    }

    // ---- signal-flow slots -------------------------------------------------
    {
        const juce::Rectangle<float> area { 0.0f, 0.0f, 424.0f, 34.0f };
        const auto slots = signalFlowSlots(area, 4, 26, 48);

        check("FxChain_SlotsSpanTheStripWithGapsBetween",
              slots.size() == 4
                  && juce::approximatelyEqual(slots[0].getWidth(), 86.5f)
                  && juce::approximatelyEqual(slots[1].getX(), 112.5f)
                  && juce::approximatelyEqual(slots[3].getRight(), 424.0f),
              "424 wide, 3 gaps of 26 -> 4 nodes of "
                  + juce::String(slots.empty() ? 0.0f : slots[0].getWidth(), 1));

        const auto narrow = signalFlowSlots({ 0.0f, 0.0f, 100.0f, 34.0f }, 4, 26, 48);
        check("FxChain_SlotsNeverGoBelowTheMinimumWidth",
              narrow.size() == 4 && narrow[0].getWidth() >= 48.0f,
              "100px for 4 nodes -> width " + juce::String(narrow.empty() ? 0.0f : narrow[0].getWidth(), 1));

        check("FxChain_NoNodesMeansNoSlots",
              signalFlowSlots(area, 0, 26, 48).empty(), "");

        // Insertion follows slot centres, so a node swaps at the halfway point.
        check("FxChain_InsertionIndexTracksSlotCentres",
              insertionIndexForCentre(slots, 0.0f) == 0
                  && insertionIndexForCentre(slots, slots[1].getCentreX() + 1.0f) == 1
                  && insertionIndexForCentre(slots, slots[1].getCentreX() - 1.0f) == 0
                  && insertionIndexForCentre(slots, 9999.0f) == 3,
              "");
    }

    // ---- the wrapping grid -------------------------------------------------
    {
        const auto cells = fxGridCells(800, 4, 4, 8, 300);
        check("FxChain_GridPlacesFourEffectsOnOneRow",
              cells.size() == 4
                  && cells[0].getY() == 0 && cells[3].getY() == 0
                  && cells[0].getWidth() == 194 && cells[3].getRight() == 800,
              "800 wide, 4 columns, 8px gaps -> cell width "
                  + juce::String(cells.empty() ? 0 : cells[0].getWidth()));

        const auto wrapped = fxGridCells(800, 6, 4, 8, 300);
        check("FxChain_GridWrapsPastTheColumnCount",
              wrapped.size() == 6
                  && wrapped[3].getY() == 0
                  && wrapped[4].getY() == 308
                  && wrapped[4].getX() == wrapped[0].getX(),
              "6 effects in 4 columns -> row 2 starts at y "
                  + juce::String(wrapped.size() > 4 ? wrapped[4].getY() : -1));

        // The grid must survive counts it was not designed around, because the
        // effect list is meant to grow without the layout being revisited.
        auto scalesCleanly = true;
        for (int count = 0; count <= 9; ++count)
        {
            const auto scaled = fxGridCells(800, count, 4, 8, 300);
            scalesCleanly = scalesCleanly && static_cast<int>(scaled.size()) == count;
            for (const auto& cell : scaled)
            {
                scalesCleanly = scalesCleanly && cell.getX() >= 0 && cell.getRight() <= 800;
            }
        }
        check("FxChain_GridHandlesZeroThroughNineEffects", scalesCleanly,
              "cells stay inside the content width at every count");

        check("FxChain_GridContentHeightGrowsARowAtATime",
              fxGridContentHeight(0, 4, 8, 300) == 0
                  && fxGridContentHeight(4, 4, 8, 300) == 300
                  && fxGridContentHeight(5, 4, 8, 300) == 608
                  && fxGridContentHeight(8, 4, 8, 300) == 608,
              "5 effects need two rows: "
                  + juce::String(fxGridContentHeight(5, 4, 8, 300)) + "px");

        // Scrolling exists precisely when the content is taller than the view.
        check("FxChain_ContentTallerThanTheViewportIsWhatScrolls",
              fxGridContentHeight(8, 4, 8, 300) > 400 && fxGridContentHeight(4, 4, 8, 300) <= 400,
              "");

        check("FxChain_SingleColumnIsAVerticalStack",
              fxGridCells(300, 3, 1, 8, 200).size() == 3
                  && fxGridCells(300, 3, 1, 8, 200)[2].getY() == 416
                  && fxGridCells(300, 3, 1, 8, 200)[2].getWidth() == 300,
              "");

        check("FxChain_ZeroColumnsIsTreatedAsOne",
              fxGridCells(300, 2, 0, 8, 200).size() == 2
                  && fxGridCells(300, 2, 0, 8, 200)[1].getY() == 208,
              "a bad config must not divide by zero");
    }

    // ---- the strip's style is real configuration ---------------------------
    {
        juce::String error;
        const auto config = UIConfig::fromJsonText(R"({"fx":{"signalFlow":{
            "nodeGap":40,"minNodeWidth":10,"insetX":12,"insetY":9,"cornerRadius":3,
            "reflowRate":0.5,"fontSize":17,"accentBarHeight":6,"hoverBrighten":0.4,
            "dragBrighten":0.6,"inactiveSaturation":0.5,
            "nodeColour":"#101112","textColour":"#ABCDEF","inactiveTextColour":"#123456",
            "connectorColour":"#FF000080","borderColour":"#00FF0040","dropHighlightColour":"#0000FF20"}}})",
                                                 error);

        FxSignalFlow strip;
        strip.setUIConfig(config);
        const auto& style = strip.style();

        check("FxChain_SignalFlowStyleComesFromConfig",
              style.nodeGap == 40 && style.minNodeWidth == 10 && style.insetX == 12 && style.insetY == 9
                  && juce::approximatelyEqual(style.cornerRadius, 3.0f)
                  && juce::approximatelyEqual(style.reflowRate, 0.5f)
                  && juce::approximatelyEqual(style.fontSize, 17.0f)
                  && juce::approximatelyEqual(style.accentBarHeight, 6.0f)
                  && juce::approximatelyEqual(style.hoverBrighten, 0.4f)
                  && juce::approximatelyEqual(style.dragBrighten, 0.6f)
                  && juce::approximatelyEqual(style.inactiveSaturation, 0.5f)
                  && style.nodeColour == juce::Colour::fromRGB(0x10, 0x11, 0x12)
                  && style.textColour == juce::Colour::fromRGB(0xAB, 0xCD, 0xEF)
                  && style.inactiveTextColour == juce::Colour::fromRGB(0x12, 0x34, 0x56)
                  && style.connectorColour == juce::Colour::fromRGBA(0xFF, 0x00, 0x00, 0x80)
                  && style.borderColour == juce::Colour::fromRGBA(0x00, 0xFF, 0x00, 0x40)
                  && style.dropHighlightColour == juce::Colour::fromRGBA(0x00, 0x00, 0xFF, 0x20),
              "all 17 fx.signalFlow properties reach the style");

        // The inset and gap are not decoration: they have to move the slots.
        strip.setNodes({ { 0, "A", juce::Colours::red, true },
                         { 1, "B", juce::Colours::green, true } });
        strip.setBounds(0, 0, 200, 40);
        strip.resized();

        const auto& slots = strip.slotBounds();
        check("FxChain_SignalFlowStyleMovesTheSlots",
              slots.size() == 2
                  && juce::approximatelyEqual(slots[0].getX(), 12.0f)
                  && juce::approximatelyEqual(slots[0].getY(), 9.0f)
                  && juce::approximatelyEqual(slots[1].getX(), slots[0].getRight() + 40.0f),
              "insetX 12, insetY 9, nodeGap 40 -> first slot at "
                  + juce::String(slots.empty() ? -1.0f : slots[0].getX(), 1));
    }

    {
        // The strip's nodes take their colour from the card blocks in the
        // config, read in sectionAccent at the moment the nodes are BUILT - and
        // they are built in the panel's constructor, from addCard, and from
        // setChainOrder, every one of which runs before any config exists. When
        // sectionAccent has no config it returns the panel's own accent, so all
        // eight nodes came out the same blue.
        //
        // setUIConfig used to store the config without rebuilding them, so they
        // stayed blue until something unrelated rebuilt them: clicking a node
        // reordered the chain, which called setChainOrder, which refreshed the
        // nodes, and the whole strip snapped to its real colours at once. That
        // is the "they all reset when I click one" half of the report.
        //
        // Driven through FxPanel rather than the editor because in this build
        // resolveUiConfigFile only probes the bundle - the source-tree and cwd
        // candidates are behind JUCE_DEBUG || PX3_DEBUG_PANEL - so the editor
        // under test never loads a config at all and every node would be blue
        // for a reason that has nothing to do with this bug.
        UIConfigManager manager;
        manager.setConfigFile(shippingUiConfigFile());
        manager.loadInitial();
        const auto config = manager.getConfig();

        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());

        FxPanel* panel = nullptr;
        px3::ui::FxSignalFlow* strip = nullptr;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            if (auto* p = dynamic_cast<FxPanel*>(&c)) panel = p;
            if (auto* f = dynamic_cast<px3::ui::FxSignalFlow*>(&c)) strip = f;
            for (auto* child : c.getChildren()) walk(*child);
        };
        if (editor != nullptr) walk(*editor);

        if (panel != nullptr)
        {
            panel->setUIConfig(config);
        }

        juce::String detail;
        auto distinct = panel != nullptr && strip != nullptr && strip->nodeList().size() >= 2;

        if (strip != nullptr)
        {
            const auto& list = strip->nodeList();
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                detail << list[i].name << " " << list[i].accent.toDisplayString(false) << "  ";
                for (std::size_t j = i + 1; j < list.size(); ++j)
                {
                    distinct = distinct && list[i].accent != list[j].accent;
                }
            }
        }

        check("FxChain_EveryStripNodeKeepsItsOwnColour", distinct,
              panel == nullptr ? "no FX panel found in the editor" : detail);
    }

    // ---- the shipping config parses and is complete ------------------------
    {
        // Read from the file the plugin actually ships, so a property that
        // exists only in a test literal cannot pass for a real one.
        UIConfigManager manager;
        manager.setConfigFile(shippingUiConfigFile());
        manager.loadInitial();
        const auto config = manager.getConfig();
        check("FxChain_ShippingConfigDefinesTheStripAndGrid",
              config != nullptr
                  && config->getInt("fx.signalFlow.height", -1) > 0
                  && config->getInt("fx.signalFlow.nodeGap", -1) > 0
                  && config->getInt("fx.grid.columns", -1) == 4
                  && config->getInt("fx.grid.rowHeight", -1) > 0,
              "");
    }

    // ---- the processor is the authority ------------------------------------
    {
        PX3SynthAudioProcessor processor;

        // Every stage present exactly once, in an order nothing defaults to.
        auto reordered = px3::kDefaultFxOrder;
        std::reverse(reordered.begin(), reordered.end());
        processor.setFxProcessingOrder(reordered);

        check("FxChain_ProcessorKeepsTheOrderItWasGiven",
              processor.getFxProcessingOrder() == reordered,
              asText(asVector(processor.getFxProcessingOrder())));

        juce::MemoryBlock state;
        processor.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("FxChain_OrderSurvivesADawSession",
              restored.getFxProcessingOrder() == reordered,
              "restored " + asText(asVector(restored.getFxProcessingOrder())));
    }

    {
        // Reordering must not change what the chain does to silence, and every
        // order must still produce audio: a bad permutation that dropped a
        // stage would otherwise pass unnoticed.
        auto order = px3::kDefaultFxOrder;
        auto ordersProduceAudio = true;
        juce::String detail;

        for (int rotation = 0; rotation < 4; ++rotation)
        {
            PX3SynthAudioProcessor processor;
            processor.setFxProcessingOrder(order);
            const auto capture = render(processor, 24000, { { 1000, true, 60, 0.9f } });
            const auto rms = capture.rms();
            ordersProduceAudio = ordersProduceAudio && rms > 1.0e-4f && std::isfinite(rms);
            detail << asText(asVector(order)) << " rms " << juce::String(rms, 5) << "  ";

            const auto rotated = moveChainEntry(asVector(order), 0, px3::kFxStageCount - 1);
            std::copy(rotated.begin(), rotated.end(), order.begin());
        }

        check("FxChain_EveryOrderStillRendersAudio", ordersProduceAudio, detail);
    }
}

} // namespace px3tests
