#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "FilterMode.h"
#include "LfoMode.h"
#include "OscillatorMode.h"
#include "SubOscMode.h"

#include "PluginEditor.h"
#include <cmath>

// File role: core processor orchestration and JUCE lifecycle entry points.
// Keep high-level runtime flow here (prepare/process/editor), and place
// domain-specific helpers in PluginProcessor*.cpp companion files.

using namespace px3::processor_internal;

PX3SynthAudioProcessor::PX3SynthAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    const auto instanceNumber = kInstanceCounter.fetch_add(1u, std::memory_order_relaxed) + 1u;
    kActiveInstanceCount.fetch_add(1, std::memory_order_relaxed);
    debugInstanceId = "PX3-INSTANCE-" + juce::String(static_cast<int>(instanceNumber)).paddedLeft('0', 2);
    debugProcessorCreatedTime = nowTimestamp();

    fxProcessingOrderPacked.store(packFxOrder({ { 0, 1, 3, 2 } }), std::memory_order_relaxed);
    fxOrderRevision.store(0u, std::memory_order_relaxed);

    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        const auto slot = juce::String(oscIndex + 1);
        const auto idPrefix = "osc" + slot;
        const auto labelPrefix = "Osc " + slot + " ";

        oscEnabledParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterBool(idPrefix + "Enabled",
                                                                                              labelPrefix + "Enabled",
                                                                                              oscIndex == 0);
        oscLevelParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterFloat(idPrefix + "Level",
                                                                                             labelPrefix + "Level",
                                                                                             juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                                             1.0f);
        oscCoarseParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterFloat(idPrefix + "Coarse",
                                                                                              labelPrefix + "Coarse",
                                                                                              juce::NormalisableRange<float>(-24.0f, 24.0f, 1.0f),
                                                                                              0.0f);
        oscFineParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterFloat(idPrefix + "Fine",
                                                                                            labelPrefix + "Fine",
                                                                                            juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f),
                                                                                            0.0f);
        oscPitchParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterFloat(
            juce::ParameterID(idPrefix + "Pitch", 1),
            labelPrefix + "Pitch",
            juce::NormalisableRange<float>(-0.24f, 0.24f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int)
            {
                return juce::String(value >= 0.0f ? "+" : "") + juce::String(value, 2) + " st";
            }));
        oscModeParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterChoice(idPrefix + "Mode",
                                                                                             labelPrefix + "Mode",
                                                                                             px3::oscillatorModeChoices(),
                                                                                             0);
        oscMacroAParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterFloat(idPrefix + "MacroA",
                                                                                              labelPrefix + "Macro A",
                                                                                              juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                                              0.5f);
        oscMacroBParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterFloat(idPrefix + "MacroB",
                                                                                              labelPrefix + "Macro B",
                                                                                              juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                                              0.5f);
        oscMacroCParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterFloat(idPrefix + "MacroC",
                                                                                              labelPrefix + "Macro C",
                                                                                              juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                                              0.5f);
        oscVowelParams[static_cast<std::size_t>(oscIndex)] = new juce::AudioParameterChoice(idPrefix + "Vowel",
                                                                                              labelPrefix + "Vowel",
                                                                                              juce::StringArray { "A", "E", "I", "O", "U" },
                                                                                              0);

        oscHarmonicParams[static_cast<std::size_t>(oscIndex)] = { {
            new juce::AudioParameterFloat(idPrefix + "H1", labelPrefix + "Harmonic 1", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f),
            new juce::AudioParameterFloat(idPrefix + "H2", labelPrefix + "Harmonic 2", juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f),
            new juce::AudioParameterFloat(idPrefix + "H3", labelPrefix + "Harmonic 3", juce::NormalisableRange<float>(0.0f, 1.0f), 0.45f),
            new juce::AudioParameterFloat(idPrefix + "H4", labelPrefix + "Harmonic 4", juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f),
            new juce::AudioParameterFloat(idPrefix + "H5", labelPrefix + "Harmonic 5", juce::NormalisableRange<float>(0.0f, 1.0f), 0.2f),
            new juce::AudioParameterFloat(idPrefix + "H6", labelPrefix + "Harmonic 6", juce::NormalisableRange<float>(0.0f, 1.0f), 0.14f),
            new juce::AudioParameterFloat(idPrefix + "H7", labelPrefix + "Harmonic 7", juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f),
            new juce::AudioParameterFloat(idPrefix + "H8", labelPrefix + "Harmonic 8", juce::NormalisableRange<float>(0.0f, 1.0f), 0.07f)
        } };
    }
    subOscEnabledParam = new juce::AudioParameterBool("subOscEnabled", "Sub Osc Enabled", false);
    subOscLevelParam = new juce::AudioParameterFloat("subOscLevel", "Sub Osc Level", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    subOscPitchParam = new juce::AudioParameterFloat(
        juce::ParameterID("subOscPitch", 1),
        "Sub Osc Pitch",
        juce::NormalisableRange<float>(-0.24f, 0.24f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int)
        {
            return juce::String(value >= 0.0f ? "+" : "") + juce::String(value, 2) + " st";
        }));
    subOscOctaveParam = new juce::AudioParameterChoice("subOscOctave",
                                                        "Sub Osc Octave",
                                                        px3::subOscOctaveChoices(),
                                                        1);
    subOscWaveformParam = new juce::AudioParameterChoice("subOscWaveform",
                                                          "Sub Osc Waveform",
                                                          px3::subOscWaveformChoices(),
                                                          1);
    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        const auto slot = juce::String(filterIndex + 1);
        const auto idPrefix = "filter" + slot;
        const auto labelPrefix = "Filter " + slot + " ";
        const auto defaultMode = filterIndex == 0 ? 0 : 6; // LP12 for Filter 1, AllPass for others.

        filterEnabledParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterBool(
            idPrefix + "Enabled",
            labelPrefix + "Enabled",
            true);

        filterCutoffParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterFloat(
            idPrefix + "Cutoff",
            labelPrefix + "Cutoff",
            juce::NormalisableRange<float>(80.0f, 18000.0f, 1.0f, 0.35f),
            12000.0f);
        filterResonanceParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterFloat(
            idPrefix + "Resonance",
            labelPrefix + "Resonance",
            juce::NormalisableRange<float>(0.25f, 2.2f),
            0.8f);
        filterTypeParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterChoice(
            idPrefix + "Type",
            labelPrefix + "Type",
            px3::filterModeChoices(),
            defaultMode);
    }
    attackParam = new juce::AudioParameterFloat("ampAttack",
                                                 "Amp Attack",
                                                 juce::NormalisableRange<float>(0.001f, 3.0f, 0.001f, 0.45f),
                                                 0.005f);
    decayParam = new juce::AudioParameterFloat("ampDecay",
                                                "Amp Decay",
                                                juce::NormalisableRange<float>(0.005f, 4.0f, 0.001f, 0.45f),
                                                0.050f);
    sustainParam = new juce::AudioParameterFloat("ampSustain",
                                                  "Amp Sustain",
                                                  juce::NormalisableRange<float>(0.0f, 1.0f),
                                                  0.8f);
    releaseParam = new juce::AudioParameterFloat("ampRelease",
                                                  "Amp Release",
                                                  juce::NormalisableRange<float>(0.010f, 5.0f, 0.001f, 0.45f),
                                                  0.100f);
    ampEnvEnabledParam = new juce::AudioParameterBool("ampEnvEnabled", "Amp Enabled", true);

    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        const auto slot = juce::String(envIndex + 1);
        const auto idPrefix = juce::String("env") + slot;
        const auto labelPrefix = juce::String("Env ") + slot + " ";

        attackParams[static_cast<std::size_t>(envIndex)] = new juce::AudioParameterFloat(
            idPrefix + "Attack",
            labelPrefix + "Attack",
            juce::NormalisableRange<float>(0.001f, 3.0f, 0.001f, 0.45f),
            0.020f);
        decayParams[static_cast<std::size_t>(envIndex)] = new juce::AudioParameterFloat(
            idPrefix + "Decay",
            labelPrefix + "Decay",
            juce::NormalisableRange<float>(0.005f, 4.0f, 0.001f, 0.45f),
            0.120f);
        sustainParams[static_cast<std::size_t>(envIndex)] = new juce::AudioParameterFloat(
            idPrefix + "Sustain",
            labelPrefix + "Sustain",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.7f);
        releaseParams[static_cast<std::size_t>(envIndex)] = new juce::AudioParameterFloat(
            idPrefix + "Release",
            labelPrefix + "Release",
            juce::NormalisableRange<float>(0.010f, 5.0f, 0.001f, 0.45f),
            0.220f);
        envelopeEnabledParams[static_cast<std::size_t>(envIndex)] = new juce::AudioParameterBool(
            idPrefix + "Enabled",
            labelPrefix + "Enabled",
            true);
    }
    masterGainParam = new juce::AudioParameterFloat("masterGain", "Master Gain", juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f);

    vibeAmountParam = new juce::AudioParameterFloat("vibeAmount", "Vibe", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    vibeEnabledParam = new juce::AudioParameterBool("vibeEnabled", "Vibe Enabled", true);
    vibeTypeParam = new juce::AudioParameterChoice("vibeType",
                                                    "Vibe Type",
                                                    kVibeTypeChoices,
                                                    0);
    delayAmountParam = new juce::AudioParameterFloat("delayAmount", "Delay Amount", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    granularSyncDivisionParam = new juce::AudioParameterChoice("granularSyncDivision",
                                                                "Granular Sync",
                                                                juce::StringArray { "Free", "1 Bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T" },
                                                                0);
    granularModeParam = new juce::AudioParameterChoice("granularMode",
                                                        "Granular Mode",
                                                        juce::StringArray { "CLASSIC", "CLOUD", "SHIMMER", "RHYTHMIC" },
                                                        0);
    delayAlgorithmParam = new juce::AudioParameterChoice("delayAlgorithm",
                                                          "Delay Algorithm",
                                                          juce::StringArray { "Granular", "Tape", "Analog/BBD", "Ping-Pong", "Stereo", "Modulated", "Diffusion" },
                                                          0);
    delayEnabledParam = new juce::AudioParameterBool("delayEnabled", "Delay Enabled", true);
    delayTimeParam = new juce::AudioParameterFloat("delayTime", "Delay Time", juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f);
    delayFeedbackParam = new juce::AudioParameterFloat("delayFeedback", "Delay Feedback", juce::NormalisableRange<float>(0.0f, 1.0f), 0.38f);
    fxSendGainParam = new juce::AudioParameterFloat("fxSendGain", "FX Send", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    fxReturnGainParam = new juce::AudioParameterFloat("fxReturnGain", "FX Return", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    static constexpr std::array<const char*, kMixerSourceCount> mixerIds { { "sub", "osc1", "osc2", "osc3" } };
    static constexpr std::array<const char*, kMixerSourceCount> mixerNames { { "Sub", "Osc 1", "Osc 2", "Osc 3" } };
    for (int i = 0; i < kMixerSourceCount; ++i)
    {
        const auto sourceId = juce::String(mixerIds[static_cast<std::size_t>(i)]);
        const auto sourceName = juce::String(mixerNames[static_cast<std::size_t>(i)]);
        mixerPanParams[static_cast<std::size_t>(i)] = new juce::AudioParameterFloat("mix." + sourceId + ".pan",
                                                                                      sourceName + " Pan",
                                                                                      juce::NormalisableRange<float>(-1.0f, 1.0f),
                                                                                      0.0f);
        mixerLevelParams[static_cast<std::size_t>(i)] = new juce::AudioParameterFloat("mix." + sourceId + ".level",
                                                sourceName + " Level",
                                                juce::NormalisableRange<float>(0.0f, 1.0f),
                                                1.0f);
        mixerSendParams[static_cast<std::size_t>(i)] = new juce::AudioParameterFloat("mix." + sourceId + ".fxSend",
                                                                                       sourceName + " FX Send",
                                                                                       juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                                       1.0f);
        mixerMuteParams[static_cast<std::size_t>(i)] = new juce::AudioParameterBool("mix." + sourceId + ".mute",
                                                                                      sourceName + " Mute",
                                                                                      false);
        mixerSoloParams[static_cast<std::size_t>(i)] = new juce::AudioParameterBool("mix." + sourceId + ".solo",
                                                                                      sourceName + " Solo",
                                                                                      false);
    }
    fxReturnMuteParam = new juce::AudioParameterBool("mix.fx.mute", "FX Return Mute", false);
    fxReturnSoloParam = new juce::AudioParameterBool("mix.fx.solo", "FX Return Solo", false);
    fxReturnPanParam = new juce::AudioParameterFloat("mix.fx.pan", "FX Return Pan", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f);
    reverbAmountParam = new juce::AudioParameterFloat("reverbAmount", "Reverb", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    reverbEnabledParam = new juce::AudioParameterBool("reverbEnabled", "Reverb Enabled", true);
    reverbAlgorithmParam = new juce::AudioParameterChoice("reverbAlgorithm",
                                                           "Reverb Mode",
                                                           juce::StringArray { "ROOM", "PLATE", "HALL", "CLOUD" },
                                                           0);
    moodEnabledParam = new juce::AudioParameterBool("moodEnabled", "Mood Enabled", true);
    moodTrueBypassParam = new juce::AudioParameterBool("moodTrueBypass", "Mood True Bypass", false);
    moodFreezeParam = new juce::AudioParameterBool("moodFreeze", "Mood Freeze", false);
    moodMixParam = new juce::AudioParameterFloat("moodMix", "Mood Mix", juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f);
    moodClockParam = new juce::AudioParameterFloat("moodClock", "Mood Clock", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    moodWetTimeParam = new juce::AudioParameterFloat("moodWetTime", "Mood Wet Time", juce::NormalisableRange<float>(0.0f, 1.0f), 0.40f);
    moodWetModifyParam = new juce::AudioParameterFloat("moodWetModify", "Mood Wet Modify", juce::NormalisableRange<float>(0.0f, 1.0f), 0.45f);
    moodLoopLengthParam = new juce::AudioParameterFloat("moodLoopLength", "Mood Loop Length", juce::NormalisableRange<float>(0.0f, 1.0f), 0.28f);
    moodLoopModifyParam = new juce::AudioParameterFloat("moodLoopModify", "Mood Loop Modify", juce::NormalisableRange<float>(0.0f, 1.0f), 0.50f);
    moodFeedbackParam = new juce::AudioParameterFloat("moodFeedback", "Mood Feedback", juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f);
    moodSpreadParam = new juce::AudioParameterFloat("moodSpread", "Mood Spread", juce::NormalisableRange<float>(0.0f, 1.0f), 0.50f);
    moodDegradeParam = new juce::AudioParameterFloat("moodDegrade", "Mood Degrade", juce::NormalisableRange<float>(0.0f, 1.0f), 0.20f);
    moodRoutingParam = new juce::AudioParameterChoice("moodRouting",
                                                       "Mood Routing",
                                                       juce::StringArray { "DRY->WET", "LOOP->WET", "PARALLEL" },
                                                       0);
    moodWetModeParam = new juce::AudioParameterChoice("moodWetMode",
                                                       "Mood Wet Mode",
                                                       juce::StringArray { "REVERB", "DELAY", "SLIP" },
                                                       0);
    moodLoopModeParam = new juce::AudioParameterChoice("moodLoopMode",
                                                        "Mood Loop Mode",
                                                        juce::StringArray { "ENV", "TAPE", "STRETCH" },
                                                        0);
    reverbSizeParam = new juce::AudioParameterFloat("reverbSize", "Reverb Size", juce::NormalisableRange<float>(0.0f, 1.0f), 0.52f);
    reverbDecayParam = new juce::AudioParameterFloat("reverbDecay", "Reverb Decay", juce::NormalisableRange<float>(0.0f, 1.0f), 0.48f);
    reverbDampingParam = new juce::AudioParameterFloat("reverbDamping", "Reverb Damping", juce::NormalisableRange<float>(0.0f, 1.0f), 0.46f);
    reverbPreDelayParam = new juce::AudioParameterFloat("reverbPreDelay", "Reverb PreDelay", juce::NormalisableRange<float>(0.0f, 1.0f), 0.08f);
    reverbModDepthParam = new juce::AudioParameterFloat("reverbModDepth", "Reverb Mod Depth", juce::NormalisableRange<float>(0.0f, 1.0f), 0.24f);
    reverbModRateParam = new juce::AudioParameterFloat("reverbModRate", "Reverb Mod Rate", juce::NormalisableRange<float>(0.0f, 1.0f), 0.18f);
    reverbWidthParam = new juce::AudioParameterFloat("reverbWidth", "Reverb Width", juce::NormalisableRange<float>(0.0f, 1.0f), 0.86f);
    reverbCloudFeedbackParam = new juce::AudioParameterFloat("reverbCloudFeedback", "Reverb Cloud Feedback", juce::NormalisableRange<float>(0.0f, 1.0f), 0.62f);
    reverbCloudDiffusionParam = new juce::AudioParameterFloat("reverbCloudDiffusion", "Reverb Cloud Diffusion", juce::NormalisableRange<float>(0.0f, 1.0f), 0.54f);
    pitchBendRangeParam = new juce::AudioParameterInt("pitchBendRange",
                                                       "Pitch Bend Range",
                                                       1,
                                                       24,
                                                       2);
    for (int lfoIndex = 0; lfoIndex < kLfoSourceCount; ++lfoIndex)
    {
        const auto slot = juce::String(lfoIndex + 1);
        const auto idPrefix = (lfoIndex == 0) ? juce::String("lfo") : juce::String("lfo") + slot;
        const auto labelPrefix = juce::String("LFO ") + slot + " ";

        lfoEnabledParams[static_cast<std::size_t>(lfoIndex)] = new juce::AudioParameterBool(
            (lfoIndex == 0) ? juce::String("lfoEnabled") : idPrefix + "Enabled",
            labelPrefix + "Enabled",
            true);
        lfoFrequencyParams[static_cast<std::size_t>(lfoIndex)] = new juce::AudioParameterFloat(
            (lfoIndex == 0) ? juce::String("lfoFrequency") : idPrefix + "Frequency",
            labelPrefix + "Frequency",
            juce::NormalisableRange<float>(0.01f, 20.0f, 0.0001f, 0.30f),
            1.0f + static_cast<float>(lfoIndex));
        lfoAmountParams[static_cast<std::size_t>(lfoIndex)] = new juce::AudioParameterFloat(
            (lfoIndex == 0) ? juce::String("lfoAmount") : idPrefix + "Amount",
            labelPrefix + "Amount",
            juce::NormalisableRange<float>(-1.0f, 1.0f),
            0.0f);
        lfoWaveformParams[static_cast<std::size_t>(lfoIndex)] = new juce::AudioParameterChoice(
            (lfoIndex == 0) ? juce::String("lfoWaveform") : idPrefix + "Waveform",
            labelPrefix + "Waveform",
            px3::lfoWaveformChoices(),
            0);

    }

    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        const auto slot = juce::String(envIndex + 1);
        const auto idPrefix = juce::String("env") + slot;
        const auto labelPrefix = juce::String("ENV ") + slot + " ";
        envelopeAmountParams[static_cast<std::size_t>(envIndex)] = new juce::AudioParameterFloat(
            (envIndex == 0) ? juce::String("envAmount") : idPrefix + "Amount",
            labelPrefix + "Amount",
            juce::NormalisableRange<float>(-1.0f, 1.0f),
            0.0f);
    }

    lfoEnabledParam = lfoEnabledParams[0];
    lfoFrequencyParam = lfoFrequencyParams[0];
    lfoAmountParam = lfoAmountParams[0];
    lfoWaveformParam = lfoWaveformParams[0];

    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        addParameter(oscEnabledParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscLevelParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscCoarseParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscFineParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscPitchParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscModeParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscMacroAParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscMacroBParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscMacroCParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscVowelParams[static_cast<std::size_t>(oscIndex)]);

        for (auto* harmonicParam : oscHarmonicParams[static_cast<std::size_t>(oscIndex)])
        {
            addParameter(harmonicParam);
        }
    }
    addParameter(subOscEnabledParam);
    addParameter(subOscLevelParam);
    addParameter(subOscPitchParam);
    addParameter(subOscOctaveParam);
    addParameter(subOscWaveformParam);
    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        addParameter(filterEnabledParams[static_cast<std::size_t>(filterIndex)]);
        addParameter(filterCutoffParams[static_cast<std::size_t>(filterIndex)]);
        addParameter(filterResonanceParams[static_cast<std::size_t>(filterIndex)]);
        addParameter(filterTypeParams[static_cast<std::size_t>(filterIndex)]);
    }
    addParameter(attackParam);
    addParameter(decayParam);
    addParameter(sustainParam);
    addParameter(releaseParam);
    addParameter(ampEnvEnabledParam);

    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        addParameter(attackParams[static_cast<std::size_t>(envIndex)]);
        addParameter(decayParams[static_cast<std::size_t>(envIndex)]);
        addParameter(sustainParams[static_cast<std::size_t>(envIndex)]);
        addParameter(releaseParams[static_cast<std::size_t>(envIndex)]);
        addParameter(envelopeEnabledParams[static_cast<std::size_t>(envIndex)]);
    }
    addParameter(masterGainParam);
    addParameter(vibeAmountParam);
    addParameter(vibeEnabledParam);
    addParameter(vibeTypeParam);
    addParameter(delayAmountParam);
    addParameter(granularSyncDivisionParam);
    addParameter(granularModeParam);
    addParameter(delayAlgorithmParam);
    addParameter(delayEnabledParam);
    addParameter(delayTimeParam);
    addParameter(delayFeedbackParam);
    addParameter(fxSendGainParam);
    addParameter(fxReturnGainParam);
    for (int i = 0; i < kMixerSourceCount; ++i)
    {
        addParameter(mixerLevelParams[static_cast<std::size_t>(i)]);
        addParameter(mixerPanParams[static_cast<std::size_t>(i)]);
        addParameter(mixerSendParams[static_cast<std::size_t>(i)]);
        addParameter(mixerMuteParams[static_cast<std::size_t>(i)]);
        addParameter(mixerSoloParams[static_cast<std::size_t>(i)]);
    }
    addParameter(fxReturnMuteParam);
    addParameter(fxReturnSoloParam);
    addParameter(fxReturnPanParam);
    addParameter(reverbAmountParam);
    addParameter(reverbEnabledParam);
    addParameter(reverbAlgorithmParam);
    addParameter(moodEnabledParam);
    addParameter(moodTrueBypassParam);
    addParameter(moodFreezeParam);
    addParameter(moodMixParam);
    addParameter(moodClockParam);
    addParameter(moodWetTimeParam);
    addParameter(moodWetModifyParam);
    addParameter(moodLoopLengthParam);
    addParameter(moodLoopModifyParam);
    addParameter(moodFeedbackParam);
    addParameter(moodSpreadParam);
    addParameter(moodDegradeParam);
    addParameter(moodRoutingParam);
    addParameter(moodWetModeParam);
    addParameter(moodLoopModeParam);
    addParameter(reverbSizeParam);
    addParameter(reverbDecayParam);
    addParameter(reverbDampingParam);
    addParameter(reverbPreDelayParam);
    addParameter(reverbModDepthParam);
    addParameter(reverbModRateParam);
    addParameter(reverbWidthParam);
    addParameter(reverbCloudFeedbackParam);
    addParameter(reverbCloudDiffusionParam);
    addParameter(pitchBendRangeParam);
    for (int lfoIndex = 0; lfoIndex < kLfoSourceCount; ++lfoIndex)
    {
        addParameter(lfoEnabledParams[static_cast<std::size_t>(lfoIndex)]);
        addParameter(lfoFrequencyParams[static_cast<std::size_t>(lfoIndex)]);
        addParameter(lfoAmountParams[static_cast<std::size_t>(lfoIndex)]);
        addParameter(lfoWaveformParams[static_cast<std::size_t>(lfoIndex)]);
    }
    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        addParameter(envelopeAmountParams[static_cast<std::size_t>(envIndex)]);
    }

    buildLfoAssignableTargets();

    const auto initialEnvelope = currentEnvelopeSettings();
    const auto initialFilter = currentFilterSettings();
    const auto initialSubtractive = currentSubtractiveSettings();
    const auto initialSubOsc = currentSubOscillatorSettings();
    const auto initialOscillatorLayers = currentOscillatorLayerSettings();

    for (int voice = 0; voice < 16; ++voice)
    {
        auto* synthVoice = new SynthVoice();
        synthVoice->setVoiceIndex(voice);
        synthVoice->setEnvelope(initialEnvelope);
        synthVoice->setFilterSettings(initialFilter);
        synthVoice->setSubtractiveSettings(initialSubtractive);
        synthVoice->setSubOscillatorSettings(initialSubOsc);
        synthVoice->setOscillatorLayerSettings(initialOscillatorLayers);
        synth.addVoice(synthVoice);
    }

    synth.addSound(new SynthSound());
    clearAllActiveNotes();

    debugLogEvent("LIFECYCLE", "PROCESSOR_CREATED",
                  "id=" + debugInstanceId + " order=" + debugDescribeOrder(getFxProcessingOrder()));
}

PX3SynthAudioProcessor::~PX3SynthAudioProcessor()
{
    debugLogEvent("LIFECYCLE", "PROCESSOR_DESTROYED",
                  "id=" + debugInstanceId + " order=" + debugDescribeOrder(getFxProcessingOrder()));
    kActiveInstanceCount.fetch_sub(1, std::memory_order_relaxed);
}

const juce::String PX3SynthAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PX3SynthAudioProcessor::acceptsMidi() const
{
    return true;
}

bool PX3SynthAudioProcessor::producesMidi() const
{
    return false;
}

bool PX3SynthAudioProcessor::isMidiEffect() const
{
    return false;
}

double PX3SynthAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PX3SynthAudioProcessor::getNumPrograms()
{
    return 1;
}

int PX3SynthAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PX3SynthAudioProcessor::setCurrentProgram(int)
{
}

const juce::String PX3SynthAudioProcessor::getProgramName(int)
{
    return {};
}

void PX3SynthAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void PX3SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRateHz = juce::jmax(1.0, sampleRate);
    synth.setCurrentPlaybackSampleRate(sampleRate);
    for (int lfoIndex = 0; lfoIndex < kLfoSourceCount; ++lfoIndex)
    {
        lfoGenerators[static_cast<std::size_t>(lfoIndex)].prepare(sampleRate);
        lfoGenerators[static_cast<std::size_t>(lfoIndex)].setSettings(currentLfoSettings(lfoIndex));
    }
    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        modulationEnvelopeGenerators[static_cast<std::size_t>(envIndex)].prepare(sampleRate);
        modulationEnvelopeGenerators[static_cast<std::size_t>(envIndex)].setSettings(currentEnvelopeSettings(envIndex));
        modulationEnvelopeGenerators[static_cast<std::size_t>(envIndex)].reset();
        modulationEnvelopeValues[static_cast<std::size_t>(envIndex)].store(0.0f, std::memory_order_relaxed);
    }
    modulationEnvelopeGateOpen = false;
    vibeComponent.prepare(sampleRate, synth.getNumVoices(), vibeComponent.getSeed());
    delayComponent.prepare(sampleRate);
    moodComponent.prepare(sampleRate);
    reverb.prepare(sampleRate);

    const auto busChannels = juce::jmax(kMixerSourceCount, getTotalNumOutputChannels());
    const auto busSamples = juce::jmax(1, samplesPerBlock);
    oscillatorBusBuffer.setSize(busChannels, busSamples, false, false, true);
    dryBusBuffer.setSize(busChannels, busSamples, false, false, true);
    fxBusBuffer.setSize(busChannels, busSamples, false, false, true);
    masterBusBuffer.setSize(busChannels, busSamples, false, false, true);
    oscillatorBusBuffer.clear();
    dryBusBuffer.clear();
    fxBusBuffer.clear();
    masterBusBuffer.clear();

    for (int i = 0; i < kMixerSourceCount; ++i)
    {
        auto& dryGate = sourceDryGateSmoothers[static_cast<std::size_t>(i)];
        auto& sendGate = sourceSendGateSmoothers[static_cast<std::size_t>(i)];
        dryGate.reset(sampleRate, 0.010);
        sendGate.reset(sampleRate, 0.010);
        dryGate.setCurrentAndTargetValue(1.0f);
        sendGate.setCurrentAndTargetValue(1.0f);
    }

    fxReturnGateSmoother.reset(sampleRate, 0.010);
    fxReturnGateSmoother.setCurrentAndTargetValue(1.0f);

    const auto envelope = currentEnvelopeSettings();
    const auto filter = currentFilterSettings();
    const auto subtractive = currentSubtractiveSettings();
    const auto subOsc = currentSubOscillatorSettings();
    const auto oscillatorLayers = currentOscillatorLayerSettings();

    for (int voiceIndex = 0; voiceIndex < synth.getNumVoices(); ++voiceIndex)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(voiceIndex)))
        {
            voice->setEnvelope(envelope);
            voice->setFilterSettings(filter);
            voice->setSubtractiveSettings(subtractive);
            voice->setSubOscillatorSettings(subOsc);
            voice->setOscillatorLayerSettings(oscillatorLayers);
        }
    }
    juce::ignoreUnused(samplesPerBlock);
}

void PX3SynthAudioProcessor::releaseResources()
{
}

bool PX3SynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
           || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

float PX3SynthAudioProcessor::currentLfoSignalForBlock(int numSamples)
{
    return currentLfoSignalForBlock(0, numSamples);
}

float PX3SynthAudioProcessor::currentLfoSignalForBlock(int lfoIndex, int numSamples)
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    auto& generator = lfoGenerators[static_cast<std::size_t>(idx)];

    const auto lfoSettings = currentLfoSettings(idx);
    generator.setSettings(lfoSettings);

    if (!lfoSettings.enabled)
    {
        lfoPhaseForDebug[static_cast<std::size_t>(idx)].store(generator.getPhaseRadians(), std::memory_order_relaxed);
        lfoCurrentValues[static_cast<std::size_t>(idx)].store(0.0f, std::memory_order_relaxed);
        return 0.0f;
    }

    const auto signal = generator.getMidpointSignalAndAdvance(numSamples);

    lfoPhaseForDebug[static_cast<std::size_t>(idx)].store(generator.getPhaseRadians(), std::memory_order_relaxed);
    lfoCurrentValues[static_cast<std::size_t>(idx)].store(signal, std::memory_order_relaxed);
    return signal;
}

void PX3SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const auto blockStartTicks = juce::Time::getHighResolutionTicks();
    const auto ticksPerSecond = juce::Time::getHighResolutionTicksPerSecond();
    const auto blockSamples = buffer.getNumSamples();
    const auto outputChannels = buffer.getNumChannels();

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
    {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    if (blockSamples > oscillatorBusBuffer.getNumSamples() || kMixerSourceCount > oscillatorBusBuffer.getNumChannels())
    {
        // Host block size should not exceed prepareToPlay max block size.
        buffer.clear();
        return;
    }

    for (int channel = 0; channel < oscillatorBusBuffer.getNumChannels(); ++channel)
    {
        oscillatorBusBuffer.clear(channel, 0, blockSamples);
        if (channel < dryBusBuffer.getNumChannels())
        {
            dryBusBuffer.clear(channel, 0, blockSamples);
        }
        if (channel < fxBusBuffer.getNumChannels())
        {
            fxBusBuffer.clear(channel, 0, blockSamples);
        }
        if (channel < masterBusBuffer.getNumChannels())
        {
            masterBusBuffer.clear(channel, 0, blockSamples);
        }
    }

    juce::MidiBuffer combinedMidi;
    combinedMidi.addEvents(midiMessages, 0, buffer.getNumSamples(), 0);

    {
        // Virtual keyboard events are produced on the message thread. Lock scope
        // is kept minimal so the audio thread only copies and clears queued MIDI.
        const juce::ScopedLock lock(virtualMidiLock);
        combinedMidi.addEvents(virtualMidiMessages, 0, -1, 0);
        virtualMidiMessages.clear();
    }

    midiMessages.swapWith(combinedMidi);
    updateActiveNotesFromMidi(midiMessages);

    auto anyActiveNotes = false;
    for (const auto& noteCount : activeNoteCounts)
    {
        if (noteCount.load(std::memory_order_relaxed) > 0)
        {
            anyActiveNotes = true;
            break;
        }
    }

    if (anyActiveNotes && !modulationEnvelopeGateOpen)
    {
        for (auto& env : modulationEnvelopeGenerators)
        {
            env.noteOn();
        }
        modulationEnvelopeGateOpen = true;
    }
    else if (!anyActiveNotes && modulationEnvelopeGateOpen)
    {
        for (auto& env : modulationEnvelopeGenerators)
        {
            env.noteOff();
        }
        modulationEnvelopeGateOpen = false;
    }

    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        auto& generator = modulationEnvelopeGenerators[static_cast<std::size_t>(envIndex)];
        generator.setSettings(currentEnvelopeSettings(envIndex));

        auto sampleValue = modulationEnvelopeValues[static_cast<std::size_t>(envIndex)].load(std::memory_order_relaxed);
        auto blockSum = 0.0f;
        for (int i = 0; i < blockSamples; ++i)
        {
            sampleValue = generator.getNextSample();
            blockSum += sampleValue;
        }

        // Use block-mean envelope magnitude for per-block modulation sampling.
        // This preserves fast transients better than end-of-block sampling while
        // avoiding peak-latched tails that can overextend modulation influence.
        const auto blockMean = blockSamples > 0 ? (blockSum / static_cast<float>(blockSamples)) : sampleValue;
        modulationEnvelopeValues[static_cast<std::size_t>(envIndex)].store(blockMean, std::memory_order_relaxed);
    }

    const auto pitchBend = juce::jlimit(-1.0f, 1.0f, pitchBendNormalized.load(std::memory_order_relaxed));
    const auto modWheel = juce::jlimit(0.0f, 1.0f, modWheelNormalized.load(std::memory_order_relaxed));
    const auto bendRange = static_cast<float>(pitchBendRangeParam->get());
    constexpr float vibratoRateHz = 5.0f;
    constexpr float vibratoMaxDepthSemitones = 1.0f;

    for (int lfoIndex = 0; lfoIndex < kLfoSourceCount; ++lfoIndex)
    {
        juce::ignoreUnused(currentLfoSignalForBlock(lfoIndex, buffer.getNumSamples()));
    }

    const auto lfoAssignedIndex = getLfoAssignmentIndex(0);
    if (lfoAssignedIndex > 0 && lfoAssignedIndex < static_cast<int>(lfoAssignableTargets.size()))
    {
        const auto& target = lfoAssignableTargets[static_cast<std::size_t>(lfoAssignedIndex)];
        if (target.parameter != nullptr)
        {
            float baseNorm = 0.0f;
            float effectiveNorm = 0.0f;
            juce::ignoreUnused(applyModulationToNormalizedValue(target.parameter,
                                                                 target.parameter->getValue(),
                                                                 &baseNorm,
                                                                 &effectiveNorm));
            lfoDebugBaseNormalized.store(baseNorm, std::memory_order_relaxed);
            lfoDebugEffectiveNormalized.store(effectiveNorm, std::memory_order_relaxed);
        }
    }
    else
    {
        lfoDebugBaseNormalized.store(0.0f, std::memory_order_relaxed);
        lfoDebugEffectiveNormalized.store(0.0f, std::memory_order_relaxed);
    }

    const auto envelope = currentEnvelopeSettings();
    const auto filter = currentFilterSettings();
    const auto subtractive = currentSubtractiveSettings();
    const auto subOsc = currentSubOscillatorSettings();
    const auto oscillatorLayers = currentOscillatorLayerSettings();
    vibeComponent.updateForBlock(currentVibeSettings(), buffer.getNumSamples());
    const auto vibeShared = vibeComponent.getSharedState();
    const auto vibeTuning = vibeComponent.getTuning();
    const auto vibeBypass = vibeComponent.isBypassed();
    const auto vibeAmount = vibeBypass ? 0.0f : vibeComponent.getEffectiveAmount();
    for (int voiceIndex = 0; voiceIndex < synth.getNumVoices(); ++voiceIndex)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(voiceIndex)))
        {
            voice->setEnvelope(envelope);
            voice->setFilterSettings(filter);
            voice->setSubtractiveSettings(subtractive);
            voice->setSubOscillatorSettings(subOsc);
            voice->setOscillatorLayerSettings(oscillatorLayers);
            voice->setPerformanceModulation(pitchBend,
                                            modWheel,
                                            bendRange,
                                            vibratoPhaseRadians,
                                            vibratoRateHz,
                                            vibratoMaxDepthSemitones);
            const auto voiceVariation = vibeComponent.getVoiceVariation(voiceIndex);
            voice->setVibeState(vibeAmount, vibeBypass, vibeShared, voiceVariation, vibeTuning);
        }
    }

    // OSCILLATOR BUS: currently receives all active voice audio.
    // Each voice now explicitly mixes oscillator sources -> filter -> amp before
    // contributing here, so future sources can sum in parallel at the same point.
    synth.renderNextBlock(oscillatorBusBuffer, midiMessages, 0, blockSamples);

    updateTransportState();

    delayComponent.updateForBlock(currentDelaySettings());
    moodComponent.updateForBlock(currentMoodSettings());
    reverb.updateForBlock(currentReverbSettings(), buffer.getNumSamples());
    const auto fxOrder = getFxProcessingOrder();
    const auto fxSendGain = fxSendGainParam != nullptr
                                ? juce::jlimit(0.0f,
                                               1.0f,
                                               fxSendGainParam->convertFrom0to1(applyModulationToNormalizedValue(
                                                   fxSendGainParam,
                                                   static_cast<juce::RangedAudioParameter*>(fxSendGainParam)->getValue())))
                                : 1.0f;
    const auto fxReturnGain = fxReturnGainParam != nullptr
                                  ? juce::jlimit(0.0f,
                                                 1.0f,
                                                 fxReturnGainParam->convertFrom0to1(applyModulationToNormalizedValue(
                                                     fxReturnGainParam,
                                                     static_cast<juce::RangedAudioParameter*>(fxReturnGainParam)->getValue())))
                                  : 1.0f;
    const auto fxPan = fxReturnPanParam != nullptr
                           ? juce::jlimit(-1.0f,
                                          1.0f,
                                          fxReturnPanParam->convertFrom0to1(applyModulationToNormalizedValue(
                                              fxReturnPanParam,
                                              static_cast<juce::RangedAudioParameter*>(fxReturnPanParam)->getValue())))
                           : 0.0f;

    std::array<float, kMixerSourceCount> sourceLevelValues { { 1.0f, 1.0f, 1.0f, 1.0f } };
    std::array<float, kMixerSourceCount> sourcePanValues { { 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<float, kMixerSourceCount> sourceSendValues { { 0.0f, 0.0f, 0.0f, 0.0f } };
    for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
    {
        auto& levelParam = getMixerLevelParam(sourceIndex);
        auto& panParam = getMixerPanParam(sourceIndex);
        auto& sendParam = getMixerSendParam(sourceIndex);

        sourceLevelValues[static_cast<std::size_t>(sourceIndex)] = juce::jlimit(0.0f,
                                                                                 1.0f,
                                                                                 levelParam.convertFrom0to1(applyModulationToNormalizedValue(
                                                                                     &levelParam,
                                                                                     static_cast<juce::RangedAudioParameter&>(levelParam).getValue())));
        sourcePanValues[static_cast<std::size_t>(sourceIndex)] = juce::jlimit(-1.0f,
                                                                               1.0f,
                                                                               panParam.convertFrom0to1(applyModulationToNormalizedValue(
                                                                                   &panParam,
                                                                                   static_cast<juce::RangedAudioParameter&>(panParam).getValue())));
        sourceSendValues[static_cast<std::size_t>(sourceIndex)] = juce::jlimit(0.0f,
                                                                                1.0f,
                                                                                sendParam.convertFrom0to1(applyModulationToNormalizedValue(
                                                                                    &sendParam,
                                                                                    static_cast<juce::RangedAudioParameter&>(sendParam).getValue())));
    }

    const auto anySolo = anyChannelSoloed();
    const auto anySourceSolo = anySourceSoloed();
    const auto fxSolo = fxReturnSoloParam != nullptr && fxReturnSoloParam->get();

    for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
    {
        sourceDryGateSmoothers[static_cast<std::size_t>(sourceIndex)].setTargetValue(sourceDryAudible(sourceIndex, anySolo) ? 1.0f : 0.0f);
        sourceSendGateSmoothers[static_cast<std::size_t>(sourceIndex)].setTargetValue(
            sourceSendAudible(sourceIndex, anySolo, anySourceSolo, fxSolo) ? 1.0f : 0.0f);
    }
    fxReturnGateSmoother.setTargetValue(fxReturnAudible(anySolo, anySourceSolo, fxSolo) ? 1.0f : 0.0f);

    auto panToGains = [](float pan, float& leftGain, float& rightGain)
    {
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        leftGain = std::cos(angle);
        rightGain = std::sin(angle);
    };

    std::array<float, kMixerSourceCount> sourcePanLeft { { 1.0f, 1.0f, 1.0f, 1.0f } };
    std::array<float, kMixerSourceCount> sourcePanRight { { 1.0f, 1.0f, 1.0f, 1.0f } };
    for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
    {
        panToGains(sourcePanValues[static_cast<std::size_t>(sourceIndex)],
                   sourcePanLeft[static_cast<std::size_t>(sourceIndex)],
                   sourcePanRight[static_cast<std::size_t>(sourceIndex)]);
    }

    float fxPanLeft = 1.0f;
    float fxPanRight = 1.0f;
    panToGains(fxPan, fxPanLeft, fxPanRight);

    std::array<double, kMixerSourceCount> mixerSourceEnergy { { 0.0, 0.0, 0.0, 0.0 } };
    double fxReturnEnergy = 0.0;

    const auto blockPhaseAdvance = juce::MathConstants<float>::twoPi * vibratoRateHz
                                   * (static_cast<float>(blockSamples) / static_cast<float>(juce::jmax(1.0, currentSampleRateHz)));
    vibratoPhaseRadians += blockPhaseAdvance;
    while (vibratoPhaseRadians >= juce::MathConstants<float>::twoPi)
    {
        vibratoPhaseRadians -= juce::MathConstants<float>::twoPi;
    }

    const auto activityDecay = std::exp(-static_cast<float>(blockSamples)
                                        / (static_cast<float>(juce::jmax(1.0, currentSampleRateHz)) * 0.25f));
    pitchBendActivity.store(pitchBendActivity.load(std::memory_order_relaxed) * activityDecay, std::memory_order_relaxed);
    modWheelActivity.store(modWheelActivity.load(std::memory_order_relaxed) * activityDecay, std::memory_order_relaxed);

    for (int sample = 0; sample < blockSamples; ++sample)
    {
        float dryL = 0.0f;
        float dryR = 0.0f;
        float fxInL = 0.0f;
        float fxInR = 0.0f;

        for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
        {
            const auto sampleValue = oscillatorBusBuffer.getSample(sourceIndex, sample);
            const auto dryGate = sourceDryGateSmoothers[static_cast<std::size_t>(sourceIndex)].getNextValue();
            const auto sendGate = sourceSendGateSmoothers[static_cast<std::size_t>(sourceIndex)].getNextValue();
            const auto panL = sourcePanLeft[static_cast<std::size_t>(sourceIndex)];
            const auto panR = sourcePanRight[static_cast<std::size_t>(sourceIndex)];
            const auto sourceLevel = sourceLevelValues[static_cast<std::size_t>(sourceIndex)];
            const auto sourceDryL = sampleValue * sourceLevel * panL * dryGate;
            const auto sourceDryR = sampleValue * sourceLevel * panR * dryGate;

            dryL += sourceDryL;
            dryR += sourceDryR;

            const auto sendGain = sourceSendValues[static_cast<std::size_t>(sourceIndex)] * fxSendGain;
            fxInL += sampleValue * panL * sendGain * sendGate;
            fxInR += sampleValue * panR * sendGain * sendGate;

            mixerSourceEnergy[static_cast<std::size_t>(sourceIndex)] += static_cast<double>(sourceDryL) * static_cast<double>(sourceDryL)
                                                                         + static_cast<double>(sourceDryR) * static_cast<double>(sourceDryR);
        }

        dryBusBuffer.setSample(0, sample, dryL);
        if (outputChannels > 1)
        {
            dryBusBuffer.setSample(1, sample, dryR);
        }

        auto stageL = fxInL;
        auto stageR = fxInR;

        for (const auto stage : fxOrder)
        {
            switch (stage)
            {
                case 0: // VIBE (distributed in voice stage)
                    break;

                case 1: // Delay
                    delayComponent.processSampleFrame(stageL, stageR, stageL, stageR);
                    break;

                case 2: // Reverb
                    reverb.processSampleFrame(stageL, stageR, stageL, stageR);
                    break;

                case 3: // Mood
                    moodComponent.processSampleFrame(stageL, stageR, stageL, stageR);
                    break;

                default:
                    break;
            }
        }

        // FX BUS: net FX return relative to dry path.
        const auto fxReturnGate = fxReturnGateSmoother.getNextValue();
        const auto fxL = (stageL - fxInL) * fxReturnGain * fxPanLeft * fxReturnGate;
        const auto fxR = (stageR - fxInR) * fxReturnGain * fxPanRight * fxReturnGate;
        fxReturnEnergy += static_cast<double>(fxL) * static_cast<double>(fxL)
                  + static_cast<double>(fxR) * static_cast<double>(fxR);
        fxBusBuffer.setSample(0, sample, fxL);
        if (outputChannels > 1)
        {
            fxBusBuffer.setSample(1, sample, fxR);
        }

        // MASTER BUS: DRY + FX return.
        masterBusBuffer.setSample(0, sample, dryL + fxL);
        if (outputChannels > 1)
        {
            masterBusBuffer.setSample(1, sample, dryR + fxR);
        }
    }

    reverb.applyPostBlockCompensation(masterBusBuffer);

    for (int channel = 0; channel < outputChannels; ++channel)
    {
        buffer.copyFrom(channel, 0, masterBusBuffer, channel, 0, blockSamples);
    }

    if (blockSamples > 0)
    {
        auto computeStereoRms = [blockSamples, outputChannels](const juce::AudioBuffer<float>& src)
        {
            const auto chCount = juce::jmin(2, juce::jmin(outputChannels, src.getNumChannels()));
            if (chCount <= 0)
            {
                return 0.0f;
            }

            double energy = 0.0;
            for (int ch = 0; ch < chCount; ++ch)
            {
                const auto* data = src.getReadPointer(ch);
                for (int i = 0; i < blockSamples; ++i)
                {
                    const auto s = static_cast<double>(data[i]);
                    energy += s * s;
                }
            }

            const auto mean = energy / static_cast<double>(chCount * blockSamples);
            return static_cast<float>(std::sqrt(juce::jmax(0.0, mean)));
        };

        debugOscillatorBusRms.store(computeStereoRms(oscillatorBusBuffer), std::memory_order_relaxed);
        debugDryBusRms.store(computeStereoRms(dryBusBuffer), std::memory_order_relaxed);
        debugFxBusRms.store(computeStereoRms(fxBusBuffer), std::memory_order_relaxed);
        debugMasterBusRms.store(computeStereoRms(masterBusBuffer), std::memory_order_relaxed);

        const auto norm = static_cast<double>(juce::jmax(1, blockSamples)) * 2.0;
        for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
        {
            const auto rms = static_cast<float>(std::sqrt(juce::jmax(0.0, mixerSourceEnergy[static_cast<std::size_t>(sourceIndex)] / norm)));
            debugMixerSourceRms[static_cast<std::size_t>(sourceIndex)].store(rms, std::memory_order_relaxed);
        }
        debugFxReturnRms.store(static_cast<float>(std::sqrt(juce::jmax(0.0, fxReturnEnergy / norm))), std::memory_order_relaxed);
    }

    if (blockSamples > 0 && currentSampleRateHz > 0.0 && ticksPerSecond > 0)
    {
        const auto blockEndTicks = juce::Time::getHighResolutionTicks();
        const auto elapsedTicks = juce::jmax<juce::int64>(1, blockEndTicks - blockStartTicks);
        const auto elapsedSeconds = static_cast<double>(elapsedTicks) / static_cast<double>(ticksPerSecond);
        const auto blockDurationSeconds = static_cast<double>(blockSamples) / currentSampleRateHz;

        const auto rawLoadPercent = static_cast<float>(juce::jmax(0.0, (elapsedSeconds / blockDurationSeconds) * 100.0));
        const auto previous = debugInstanceCpuLoadPercent.load(std::memory_order_relaxed);
        constexpr float smoothing = 0.18f;
        const auto smoothed = previous + (rawLoadPercent - previous) * smoothing;
        debugInstanceCpuLoadPercent.store(smoothed, std::memory_order_relaxed);
    }
}

bool PX3SynthAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PX3SynthAudioProcessor::createEditor()
{
    debugLogEvent("LIFECYCLE", "CREATE_EDITOR", "order=" + debugDescribeOrder(getFxProcessingOrder()));
    return new PX3SynthAudioProcessorEditor(*this);
}

