#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "FilterMode.h"
#include "LfoMode.h"
#include "OscillatorMode.h"
#include "SubOscMode.h"

#include "PluginEditor.h"
#include "OutputCeiling.h"
#include "PX3Diagnostics.h"

#include <algorithm>
#include <cmath>
#include <vector>

// File role: core processor orchestration and JUCE lifecycle entry points.
// Keep high-level runtime flow here (prepare/process/editor), and place
// domain-specific helpers in PluginProcessor*.cpp companion files.

using namespace px3::processor_internal;

namespace
{
// Sources generate 4 dB below full scale so there is headroom for modulation to
// push into. The trim lives HERE, at the source, rather than on the mixer
// faders: a fader is a readout of the channel's gain, and starting it at -4 dB
// made a freshly loaded plugin look as though someone had already pulled every
// channel down.
// (The trim value itself lives in PluginProcessorInternals.h so the source side
// and the fader range cannot disagree.)
//
// Because the sources are trimmed, the faders run to +4 dB rather than stopping
// at unity, so a channel can still be driven to full scale. Fader at its default
// of 0 dB reproduces the old default level exactly, and fader at the top
// reproduces the old maximum exactly - the headroom moved, the gain did not.

// Fixed output boost applied to the whole synth after the master mix, so the
// instrument is louder without moving the user-facing master gain default.
constexpr float kOutputBoostDb = 6.0f;


// Pan-law gain at centre. The FX send is taken pre-pan and placed centrally, so
// this is the fixed per-side gain it uses.
const float kSendCentreGain = std::cos(juce::MathConstants<float>::pi * 0.25f);

// Constant-power pan law, shared by the audio path and by smoother initialisation.
void panToGainsStatic(float pan, float& leftGain, float& rightGain)
{
    const auto angle = (juce::jlimit(-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    leftGain = std::cos(angle);
    rightGain = std::sin(angle);
}
}

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
    fxReturnGainParam = new juce::AudioParameterFloat("fxReturnGain",
                                                       "FX Return",
                                                       juce::NormalisableRange<float>(0.0f, px3::processor_internal::channelFaderMaxGain()),
                                                       1.0f);
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
                                                juce::NormalisableRange<float>(0.0f, px3::processor_internal::channelFaderMaxGain()),
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

    const auto initialAmpEnvelope = currentAmpEnvelopeSettings();
    std::array<EnvelopeSettings, kEnvelopeSourceCount> initialModEnvelopeSettings;
    std::array<bool, kEnvelopeSourceCount> initialModEnvelopeEnabled;
    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        initialModEnvelopeSettings[static_cast<std::size_t>(envIndex)] = currentModEnvelopeSettings(envIndex);
        initialModEnvelopeEnabled[static_cast<std::size_t>(envIndex)] = getEnvelopeEnabledParam(envIndex).get();
    }
    const auto initialFilter = currentFilterSettings();
    const auto initialSubtractive = currentSubtractiveSettings();
    const auto initialSubOsc = currentSubOscillatorSettings();
    const auto initialOscillatorLayers = currentOscillatorLayerSettings();

    for (int voice = 0; voice < kPolyphonyVoiceCount; ++voice)
    {
        auto* synthVoice = new SynthVoice();
        synthVoice->setVoiceIndex(voice);
        synthVoice->setAmpEnvelope(initialAmpEnvelope);
        synthVoice->setAmpEnvelopeEnabled(ampEnvEnabledParam != nullptr ? ampEnvEnabledParam->get() : true);
        synthVoice->setModEnvelopeSettings(initialModEnvelopeSettings, initialModEnvelopeEnabled);
        synthVoice->setFilterSettings(initialFilter);
        synthVoice->setSubtractiveSettings(initialSubtractive);
        synthVoice->setSubOscillatorSettings(initialSubOsc);
        synthVoice->setOscillatorLayerSettings(initialOscillatorLayers);
        synth.addVoice(synthVoice);
        // The voice pool is fixed for the processor's lifetime, so the concrete
        // type is known here once. processBlock walks the voices four times per
        // block; recovering the type with dynamic_cast each time cost 256 RTTI
        // lookups per block for a set of pointers that never changes.
        typedVoices[static_cast<std::size_t>(voice)] = synthVoice;
    }

    synth.addSound(new SynthSound());
    // Preserve AMP release tails during dense performance by avoiding hard
    // stopNote(false) steals whenever possible.
    synth.setNoteStealingEnabled(false);
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
        modulationEnvelopeValues[static_cast<std::size_t>(envIndex)].store(0.0f, std::memory_order_relaxed);
    }
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
        dryGate.prepare(sampleRate, 0.010);
        sendGate.prepare(sampleRate, 0.010);
        dryGate.setCurrent(sourceDryAudible(i, anyChannelSoloed()));
        sendGate.setCurrent(sourceSendAudible(i,
                                              anyChannelSoloed(),
                                              anySourceSoloed(),
                                              fxReturnSoloParam != nullptr && fxReturnSoloParam->get()));
    }

    fxReturnGateSmoother.prepare(sampleRate, 0.010);
    fxReturnGateSmoother.setCurrent(fxReturnAudible(anyChannelSoloed(),
                                                    anySourceSoloed(),
                                                    fxReturnSoloParam != nullptr && fxReturnSoloParam->get()));

    // Fader/pan/send smoothing. Long enough to kill the block-rate staircase,
    // short enough that a fader still feels immediate.
    constexpr double kMixerSmoothingSeconds = 0.015;
    {
        for (int i = 0; i < kMixerSourceCount; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            sourceLevelSmoothers[idx].prepare(sampleRate, kMixerSmoothingSeconds);
            sourcePanLeftSmoothers[idx].prepare(sampleRate, kMixerSmoothingSeconds);
            sourcePanRightSmoothers[idx].prepare(sampleRate, kMixerSmoothingSeconds);
            sourceSendSmoothers[idx].prepare(sampleRate, kMixerSmoothingSeconds);

            sourceLevelSmoothers[idx].setCurrent(juce::jlimit(0.0f, 1.0f, getMixerLevelParam(i).get()));
            sourceSendSmoothers[idx].setCurrent(juce::jlimit(0.0f, 1.0f, getMixerSendParam(i).get()));

            float left = 1.0f;
            float right = 1.0f;
            panToGainsStatic(juce::jlimit(-1.0f, 1.0f, getMixerPanParam(i).get()), left, right);
            sourcePanLeftSmoothers[idx].setCurrent(left);
            sourcePanRightSmoothers[idx].setCurrent(right);
        }
    }
    fxSendGainSmoother.prepare(sampleRate, kMixerSmoothingSeconds);
    fxReturnGainSmoother.prepare(sampleRate, kMixerSmoothingSeconds);
    fxSendGainSmoother.setCurrent(fxSendGainParam != nullptr ? juce::jlimit(0.0f, 1.0f, fxSendGainParam->get()) : 1.0f);
    fxReturnGainSmoother.setCurrent(fxReturnGainParam != nullptr ? juce::jlimit(0.0f, 1.0f, fxReturnGainParam->get()) : 1.0f);

    fxReturnPanSmoother.reset(sampleRate, 0.012);
    const auto initialFxPan = fxReturnPanParam != nullptr ? fxReturnPanParam->get() : 0.0f;
    fxReturnPanSmoother.setCurrentAndTargetValue(juce::jlimit(-1.0f, 1.0f, initialFxPan));

    polyphonyGainSmoother.reset(sampleRate, 0.035);
    polyphonyGainSmoother.setCurrentAndTargetValue(1.0f);
    polyphonyGainHold = 1.0f;

    const auto ampEnvelope = currentAmpEnvelopeSettings();
    const auto ampEnvelopeEnabled = ampEnvEnabledParam != nullptr ? ampEnvEnabledParam->get() : true;
    std::array<EnvelopeSettings, kEnvelopeSourceCount> modEnvelopeSettings;
    std::array<bool, kEnvelopeSourceCount> modEnvelopeEnabled;
    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        modEnvelopeSettings[static_cast<std::size_t>(envIndex)] = currentModEnvelopeSettings(envIndex);
        modEnvelopeEnabled[static_cast<std::size_t>(envIndex)] = getEnvelopeEnabledParam(envIndex).get();
    }
    const auto filter = currentFilterSettings();
    const auto subtractive = currentSubtractiveSettings();
    const auto subOsc = currentSubOscillatorSettings();
    const auto oscillatorLayers = currentOscillatorLayerSettings();

    for (auto* voice : typedVoices)
    {
        if (voice != nullptr)
        {
            voice->setAmpEnvelope(ampEnvelope);
            voice->setAmpEnvelopeEnabled(ampEnvelopeEnabled);
            voice->setModEnvelopeSettings(modEnvelopeSettings, modEnvelopeEnabled);
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

void PX3SynthAudioProcessor::collectModulationEnvelopeValuesFromVoices()
{
    std::array<float, kEnvelopeSourceCount> sums { { 0.0f, 0.0f, 0.0f } };
    auto activeVoiceCount = 0;

    auto anyHeldNotes = false;
    for (const auto& noteCount : activeNoteCounts)
    {
        if (noteCount.load(std::memory_order_relaxed) > 0)
        {
            anyHeldNotes = true;
            break;
        }
    }

    for (auto* voice : typedVoices)
    {
        if (voice == nullptr || !voice->isVoiceActive())
        {
            continue;
        }

        ++activeVoiceCount;
        for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
        {
            sums[static_cast<std::size_t>(envIndex)] += voice->getModEnvelopeValue(envIndex);
        }
    }

    if (activeVoiceCount <= 0)
    {
        // If notes are currently held but voices have not yet been advanced in
        // this block, keep the previous modulation value instead of snapping to
        // zero and losing ENV impact on downstream destinations.
        if (anyHeldNotes)
        {
            return;
        }

        for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
        {
            modulationEnvelopeValues[static_cast<std::size_t>(envIndex)].store(0.0f, std::memory_order_relaxed);
        }
        return;
    }

    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        const auto value = sums[static_cast<std::size_t>(envIndex)] / static_cast<float>(activeVoiceCount);
        modulationEnvelopeValues[static_cast<std::size_t>(envIndex)].store(juce::jlimit(0.0f, 1.0f, value),
                                                                            std::memory_order_relaxed);
    }
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

#if PX3_DIAGNOSTICS
    px3::diag::state().beginBlock(blockSamples);
#endif

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

    // MOD ENV signals are voice-owned and sampled from active voices.
    collectModulationEnvelopeValuesFromVoices();

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

    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        auto baseNorm = 0.0f;
        auto effectiveNorm = 0.0f;
        auto contributionNorm = 0.0f;

        const auto assignment = getEnvelopeAssignmentIndex(envIndex);
        if (assignment > 0 && assignment < static_cast<int>(lfoAssignableTargets.size()))
        {
            const auto& target = lfoAssignableTargets[static_cast<std::size_t>(assignment)];
            if (target.parameter != nullptr)
            {
                const auto envSignal = juce::jlimit(0.0f,
                                                    1.0f,
                                                    modulationEnvelopeValues[static_cast<std::size_t>(envIndex)].load(std::memory_order_relaxed));
                const auto envAmount = juce::jlimit(-1.0f, 1.0f, getEnvelopeAmountParam(envIndex).get());
                contributionNorm = target.normalizedDepth * (envSignal * envAmount);

                const auto baseNormalized = target.parameter->getValue();
                juce::ignoreUnused(applyModulationToNormalizedValue(target.parameter,
                                                                     baseNormalized,
                                                                     &baseNorm,
                                                                     &effectiveNorm));
            }
        }

        debugEnvelopeContributionNormalized[static_cast<std::size_t>(envIndex)].store(contributionNorm,
                                                                                       std::memory_order_relaxed);
        debugEnvelopeDestinationBaseNormalized[static_cast<std::size_t>(envIndex)].store(baseNorm,
                                                                                          std::memory_order_relaxed);
        debugEnvelopeDestinationEffectiveNormalized[static_cast<std::size_t>(envIndex)].store(effectiveNorm,
                                                                                               std::memory_order_relaxed);
    }

    const auto ampEnvelope = currentAmpEnvelopeSettings();
    const auto ampEnvelopeEnabled = ampEnvEnabledParam != nullptr ? ampEnvEnabledParam->get() : true;
    std::array<EnvelopeSettings, kEnvelopeSourceCount> modEnvelopeSettings;
    std::array<bool, kEnvelopeSourceCount> modEnvelopeEnabled;
    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        modEnvelopeSettings[static_cast<std::size_t>(envIndex)] = currentModEnvelopeSettings(envIndex);
        modEnvelopeEnabled[static_cast<std::size_t>(envIndex)] = getEnvelopeEnabledParam(envIndex).get();
    }
    const auto filter = currentFilterSettings();
    const auto subtractive = currentSubtractiveSettings();
    const auto subOsc = currentSubOscillatorSettings();
    const auto oscillatorLayers = currentOscillatorLayerSettings();
    // The previous block's source level drives the supply sag. One block of
    // delay is what a real reservoir capacitor does anyway - it responds to
    // current already drawn, not current about to be drawn.
    const auto vibeLoad = juce::jlimit(0.0f, 1.0f,
                                       debugOscillatorBusRms.load(std::memory_order_relaxed) * 3.0f);
    vibeComponent.updateForBlock(currentVibeSettings(), buffer.getNumSamples(), vibeLoad);
    const auto vibeShared = vibeComponent.getSharedState();
    const auto vibeTuning = vibeComponent.getTuning();
    const auto vibeBypass = vibeComponent.isBypassed();
    const auto vibeAmount = vibeBypass ? 0.0f : vibeComponent.getEffectiveAmount();
    for (int voiceIndex = 0; voiceIndex < kPolyphonyVoiceCount; ++voiceIndex)
    {
        if (auto* voice = typedVoices[static_cast<std::size_t>(voiceIndex)])
        {
            voice->setAmpEnvelope(ampEnvelope);
            voice->setAmpEnvelopeEnabled(ampEnvelopeEnabled);
            voice->setModEnvelopeSettings(modEnvelopeSettings, modEnvelopeEnabled);
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

    auto releaseVoicesPrunedThisBlock = 0;
#if PX3_DIAGNOSTICS
    if (!px3::diag::state().disableReleasePruning)
#endif
    {
        auto releaseCandidateCount = 0;
        auto heldVoicesForBudget = 0;

        for (auto* voice : typedVoices)
        {
            if (voice == nullptr || !voice->isVoiceActive())
            {
                continue;
            }

            if (voice->isKeyDown())
            {
                ++heldVoicesForBudget;
            }
            else if (!voice->isFastReleasing())
            {
                // Voices already fading out under the budget are on their way
                // to silence; counting them again would prune still-audible
                // tails for capacity that is about to free itself.
                if (releaseCandidateCount < static_cast<int>(releaseCandidateScratch.size()))
                {
                    releaseCandidateScratch[static_cast<std::size_t>(releaseCandidateCount++)] = voice;
                }
            }
        }

        auto maxReleaseVoices = 8;
        if (heldVoicesForBudget >= 4)
        {
            maxReleaseVoices = 10;
        }
        else if (heldVoicesForBudget >= 1)
        {
            // Keep held-note feel intact while capping release-tail density in
            // the low-held/high-release zone where residual artifacts persist.
            maxReleaseVoices = 4;
        }
        if (releaseCandidateCount > maxReleaseVoices)
        {
            std::sort(releaseCandidateScratch.begin(),
                      releaseCandidateScratch.begin() + releaseCandidateCount,
                      [](const SynthVoice* a, const SynthVoice* b)
                      {
                          const auto envA = a->getCurrentAmpEnvelopeValue();
                          const auto envB = b->getCurrentAmpEnvelopeValue();
                          if (std::abs(envA - envB) > 1.0e-5f)
                          {
                              return envA < envB;
                          }
                          return a->getNoteAgeSamples() > b->getNoteAgeSamples();
                      });

            const auto pruneCount = releaseCandidateCount - maxReleaseVoices;
            for (int i = 0; i < pruneCount; ++i)
            {
                if (auto* voice = releaseCandidateScratch[static_cast<std::size_t>(i)])
                {
                    // Fade the tail out over a few ms instead of cutting it at
                    // its current amplitude; a hard stop here is a step
                    // discontinuity in the summed output, i.e. an audible click.
#if PX3_DIAGNOSTICS
                    if (px3::diag::state().legacyHardStopPruning)
                    {
                        voice->stopNote(0.0f, false);
                    }
                    else
#endif
                    voice->beginFastRelease();
                    ++releaseVoicesPrunedThisBlock;
                }
            }
        }
    }
    debugReleaseVoicesPruned.store(releaseVoicesPrunedThisBlock, std::memory_order_relaxed);
#if PX3_DIAGNOSTICS
    if (px3::diag::state().capturing)
    {
        px3::diag::state().releasePrunes += releaseVoicesPrunedThisBlock;
    }
#endif

    // OSCILLATOR BUS: currently receives all active voice audio.
    // Each voice now explicitly mixes oscillator sources -> filter -> amp before
    // contributing here, so future sources can sum in parallel at the same point.
    synth.renderNextBlock(oscillatorBusBuffer, midiMessages, 0, blockSamples);

    auto prePolyPeak = 0.0f;
    auto prePolyClipSamples = 0;
    for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
    {
        const auto* sourceData = oscillatorBusBuffer.getReadPointer(sourceIndex);
        for (int sample = 0; sample < blockSamples; ++sample)
        {
            const auto value = std::abs(sourceData[sample]);
            prePolyPeak = juce::jmax(prePolyPeak, value);
            if (value > 1.0f)
            {
                ++prePolyClipSamples;
            }
        }
    }
    debugOscillatorBusPrePolyPeak.store(prePolyPeak, std::memory_order_relaxed);
    debugOscillatorBusPrePolyClipSamples.store(prePolyClipSamples, std::memory_order_relaxed);

    constexpr float kNearSilentReleaseThreshold = 1.0e-4f;
    constexpr float kPolyphonyReferenceVoices = 0.50f;
    constexpr float kPolyphonyGainFloor = 0.10f;
    // How long the polyphony gain takes to recover once the load drops. This is
    // deliberately longer than a musical release so a decaying tail cannot lift
    // its own gain back up while it is still audible.
    constexpr float kPolyphonyLoadReleaseSeconds = 2.5f;
    // Scaled by the output boost so this still predicts the peak that actually
    // reaches the output, and the overload protection engages at the same
    // fraction of full scale as before.
    const auto kEstimatedMasterFromSource = 2.8f * juce::Decibels::decibelsToGain(kOutputBoostDb);
    constexpr float kAttenuationStartPeak = 0.30f;
    constexpr float kAttenuationFullPeak = 0.85f;
    constexpr float kPolyGainUnityDeadband = 0.97f;
    constexpr float kTailOnlyBypassPrePolyPeakThreshold = 0.25f;
    constexpr int kLowHeldBypassMaxHeldVoices = 3;
    constexpr float kLowHeldBypassPrePolyPeakThreshold = 0.19f;

    auto activeVoiceCount = 0;
    auto heldVoiceCount = 0;
    auto releasingVoiceCount = 0;
    auto nearSilentReleaseVoiceCount = 0;
    auto releasingVoiceEnergyEquivalent = 0.0f;
    auto activeVoiceEnergyEquivalent = 0.0f;
    auto blockVoicePeak = 0.0f;
    std::array<float, kMixerSourceCount> blockVoiceSourcePeak { { 0.0f, 0.0f, 0.0f, 0.0f } };
    for (auto* voice : typedVoices)
    {
        if (voice == nullptr || !voice->isVoiceActive())
        {
            continue;
        }

        ++activeVoiceCount;

        // Load is measured by how much signal each voice is actually
        // contributing, not by whether its key is still down. Weighting
        // releasing voices differently made the load jump by 2.5x at note-off
        // with no change in the audio, which stepped the polyphony gain down
        // and then swung it back up across the whole release tail.
        const auto env = juce::jlimit(0.0f, 1.0f, voice->getCurrentAmpEnvelopeValue());
        activeVoiceEnergyEquivalent += env;

        if (voice->isKeyDown())
        {
            ++heldVoiceCount;
        }
        else
        {
            ++releasingVoiceCount;
            releasingVoiceEnergyEquivalent += env;
            if (env <= kNearSilentReleaseThreshold)
            {
                ++nearSilentReleaseVoiceCount;
            }
        }

        blockVoicePeak = juce::jmax(blockVoicePeak, voice->getLastBlockPeak());
        for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
        {
            blockVoiceSourcePeak[static_cast<std::size_t>(sourceIndex)] = juce::jmax(
                blockVoiceSourcePeak[static_cast<std::size_t>(sourceIndex)],
                voice->getLastBlockSourcePeak(sourceIndex));
        }
    }

#if PX3_DIAGNOSTICS
    const auto legacyPolyphonyLoad = px3::diag::state().legacyPolyphonyLoad;
    const auto effectiveVoiceLoad =
        legacyPolyphonyLoad
            ? juce::jmax(1.0f,
                         static_cast<float>(heldVoiceCount)
                             + static_cast<float>(releasingVoiceCount) * 0.35f
                             + releasingVoiceEnergyEquivalent * 2.20f)
            : juce::jmax(1.0f, activeVoiceEnergyEquivalent);
#else
    const auto effectiveVoiceLoad = juce::jmax(1.0f, activeVoiceEnergyEquivalent);
#endif
    auto polyphonyGainFromLoad = 1.0f;
    if (effectiveVoiceLoad > kPolyphonyReferenceVoices)
    {
        polyphonyGainFromLoad = std::sqrt(kPolyphonyReferenceVoices / effectiveVoiceLoad);
    }
    polyphonyGainFromLoad = juce::jlimit(kPolyphonyGainFloor, 1.0f, polyphonyGainFromLoad);

    // The polyphony gain's thresholds are calibrated against a source running at
    // full scale. Sources are now trimmed by the headroom amount, so the peak is
    // referred back to full scale before the thresholds are applied - otherwise
    // moving the trim upstream would silently recalibrate the overload
    // protection and it would stop engaging (measured: it did exactly that, and
    // the synth came out 3.4 dB louder).
    const auto calibratedPeak = prePolyPeak / juce::jmax(1.0e-6f, px3::processor_internal::sourceHeadroomGain());
    const auto predictedMasterPeak = calibratedPeak * kEstimatedMasterFromSource;
    const auto overloadBlend = juce::jlimit(0.0f,
                                            1.0f,
                                            (predictedMasterPeak - kAttenuationStartPeak)
                                                / juce::jmax(1.0e-5f, (kAttenuationFullPeak - kAttenuationStartPeak)));
    const auto tailOnlyBypass = heldVoiceCount == 0 && calibratedPeak <= kTailOnlyBypassPrePolyPeakThreshold;
    const auto lowHeldBypass = heldVoiceCount > 0
                               && heldVoiceCount <= kLowHeldBypassMaxHeldVoices
                               && calibratedPeak <= kLowHeldBypassPrePolyPeakThreshold;
    const auto polyphonyBypass = tailOnlyBypass || lowHeldBypass;
    auto polyphonyGainTarget = polyphonyBypass
                                   ? 1.0f
                                   : juce::jlimit(kPolyphonyGainFloor,
                                                  1.0f,
                                                  1.0f - overloadBlend * (1.0f - polyphonyGainFromLoad));
    if (polyphonyGainTarget >= kPolyGainUnityDeadband)
    {
        polyphonyGainTarget = 1.0f;
    }

    // Attenuate immediately, recover slowly.
    //
    // Every input to the target above - the voice load, the predicted peak, and
    // the tail bypass thresholds - falls as a release tail decays. Following
    // them upwards re-shapes the AMP ENV release into a swell: measured at up to
    // 13.6 dB of gain rise across a single 1.25 s tail. Recovery is therefore
    // slower than any musical release, so the gain is effectively constant for
    // the lifetime of a tail. Silence is the one moment the gain can move for
    // free, so it resets there rather than holding attenuation into the next note.
#if PX3_DIAGNOSTICS
    if (legacyPolyphonyLoad)
    {
        polyphonyGainHold = polyphonyGainTarget;
    }
    else
#endif
    if (activeVoiceCount == 0)
    {
        polyphonyGainHold = polyphonyGainTarget;
    }
    else if (polyphonyGainTarget < polyphonyGainHold)
    {
        polyphonyGainHold = polyphonyGainTarget;
    }
    else
    {
        const auto recoveryPerBlock = 1.0f
                                      - std::exp(-static_cast<float>(blockSamples)
                                                 / static_cast<float>(juce::jmax(1.0, currentSampleRateHz)
                                                                      * kPolyphonyLoadReleaseSeconds));
        polyphonyGainHold += (polyphonyGainTarget - polyphonyGainHold) * recoveryPerBlock;
    }

#if PX3_DIAGNOSTICS
    {
        auto& d = px3::diag::state();
        d.blockLoad = effectiveVoiceLoad;
        d.blockPrePolyPeak = prePolyPeak;
        d.blockOverloadBlend = overloadBlend;
        d.blockGainTarget = polyphonyGainTarget;
        d.blockActiveVoices = static_cast<float>(activeVoiceCount);
        d.blockReleasingVoices = static_cast<float>(releasingVoiceCount);
    }
#endif

    polyphonyGainSmoother.setTargetValue(polyphonyGainHold);

#if PX3_DIAGNOSTICS
    auto& diagState = px3::diag::state();
    const auto polyGainAtBlockStart = diagState.fixedPolyGain ? 1.0f : polyphonyGainSmoother.getCurrentValue();
    auto diagPrevPolyGain = polyGainAtBlockStart;
    if (diagState.capturing)
    {
        diagState.maxPolyGainBlockStep = juce::jmax(diagState.maxPolyGainBlockStep,
                                                    std::abs(polyphonyGainTarget - polyGainAtBlockStart));
    }
#endif

    for (int sample = 0; sample < blockSamples; ++sample)
    {
        auto polyGain = polyphonyGainSmoother.getNextValue();
#if PX3_DIAGNOSTICS
        if (diagState.fixedPolyGain)
        {
            polyGain = 1.0f;
        }
        if (diagState.capturing)
        {
            diagState.setPolyGainSample(sample, polyGain);
            diagState.maxPolyGainDelta = juce::jmax(diagState.maxPolyGainDelta,
                                                    std::abs(polyGain - diagPrevPolyGain));
            diagState.minPolyGain = juce::jmin(diagState.minPolyGain, polyGain);
            diagPrevPolyGain = polyGain;
        }
#endif
        for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
        {
            const auto scaled = oscillatorBusBuffer.getSample(sourceIndex, sample) * polyGain;
            oscillatorBusBuffer.setSample(sourceIndex, sample, scaled);
        }
    }

    debugActiveVoiceCount.store(activeVoiceCount, std::memory_order_relaxed);
    debugHeldVoiceCount.store(heldVoiceCount, std::memory_order_relaxed);
    debugReleasingVoiceCount.store(releasingVoiceCount, std::memory_order_relaxed);
    debugNearSilentReleaseVoiceCount.store(nearSilentReleaseVoiceCount, std::memory_order_relaxed);
    debugVoicePeak.store(blockVoicePeak, std::memory_order_relaxed);
    for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
    {
        debugVoiceSourcePeak[static_cast<std::size_t>(sourceIndex)].store(blockVoiceSourcePeak[static_cast<std::size_t>(sourceIndex)],
                                                                           std::memory_order_relaxed);
    }
    debugEffectiveVoiceLoad.store(effectiveVoiceLoad, std::memory_order_relaxed);
    debugReleaseEnergyEquivalent.store(releasingVoiceEnergyEquivalent, std::memory_order_relaxed);
    debugPolyphonyGainTarget.store(polyphonyGainTarget, std::memory_order_relaxed);
    debugPolyGainTailBypassActive.store(polyphonyBypass ? 1 : 0, std::memory_order_relaxed);
    debugPolyphonyGainApplied.store(polyphonyGainSmoother.getCurrentValue(), std::memory_order_relaxed);

    // Capture the newest per-voice modulation envelope values for downstream
    // modulation reads and debug snapshots in the next processing stage.
    collectModulationEnvelopeValuesFromVoices();

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
                                                 px3::processor_internal::channelFaderMaxGain(),
                                                 fxReturnGainParam->convertFrom0to1(applyModulationToNormalizedValue(
                                                     fxReturnGainParam,
                                                     static_cast<juce::RangedAudioParameter*>(fxReturnGainParam)->getValue())))
                                  : 1.0f;
    const auto fxPanTarget = fxReturnPanParam != nullptr
                                 ? juce::jlimit(-1.0f,
                                                1.0f,
                                                fxReturnPanParam->convertFrom0to1(applyModulationToNormalizedValue(
                                                    fxReturnPanParam,
                                                    static_cast<juce::RangedAudioParameter*>(fxReturnPanParam)->getValue())))
                                 : 0.0f;
    fxReturnPanSmoother.setTargetValue(fxPanTarget);

    std::array<float, kMixerSourceCount> sourceLevelValues { { 1.0f, 1.0f, 1.0f, 1.0f } };
    std::array<float, kMixerSourceCount> sourcePanValues { { 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<float, kMixerSourceCount> sourceSendValues { { 0.0f, 0.0f, 0.0f, 0.0f } };
    for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
    {
        auto& levelParam = getMixerLevelParam(sourceIndex);
        auto& panParam = getMixerPanParam(sourceIndex);
        auto& sendParam = getMixerSendParam(sourceIndex);

        sourceLevelValues[static_cast<std::size_t>(sourceIndex)] = juce::jlimit(0.0f,
                                                                                 px3::processor_internal::channelFaderMaxGain(),
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
        sourceDryGateSmoothers[static_cast<std::size_t>(sourceIndex)].setTarget(sourceDryAudible(sourceIndex, anySolo));
        sourceSendGateSmoothers[static_cast<std::size_t>(sourceIndex)].setTarget(
            sourceSendAudible(sourceIndex, anySolo, anySourceSolo, fxSolo));
    }
    fxReturnGateSmoother.setTarget(fxReturnAudible(anySolo, anySourceSolo, fxSolo));

    auto panToGains = [](float pan, float& leftGain, float& rightGain)
    {
        panToGainsStatic(pan, leftGain, rightGain);
    };

    std::array<float, kMixerSourceCount> sourcePanLeft { { 1.0f, 1.0f, 1.0f, 1.0f } };
    std::array<float, kMixerSourceCount> sourcePanRight { { 1.0f, 1.0f, 1.0f, 1.0f } };
    for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
    {
        panToGains(sourcePanValues[static_cast<std::size_t>(sourceIndex)],
                   sourcePanLeft[static_cast<std::size_t>(sourceIndex)],
                   sourcePanRight[static_cast<std::size_t>(sourceIndex)]);
    }

    std::array<double, kMixerSourceCount> mixerSourceEnergy { { 0.0, 0.0, 0.0, 0.0 } };
    double fxReturnEnergy = 0.0;

    auto outputBoostGain = juce::Decibels::decibelsToGain(kOutputBoostDb);
#if PX3_DIAGNOSTICS
    if (diagState.disableOutputBoost)
    {
        outputBoostGain = 1.0f;
    }
    const auto ceilingEnabled = !diagState.disableOutputCeiling;
#else
    constexpr auto ceilingEnabled = true;
#endif
    const auto applyCeiling = [&](float value)
    {
        return ceilingEnabled ? px3::applyOutputCeiling(value) : value;
    };

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

        // Master FX send trim is one shared control, so its smoother must be
        // advanced exactly once per sample. Advancing it inside the per-source
        // loop stepped it four times per sample and handed each source a
        // different value, which is shared mutable state between channels.
        auto masterSendTrim = fxSendGainSmoother.next(fxSendGain);
#if PX3_DIAGNOSTICS
        if (diagState.legacyUnsmoothedMixer)
        {
            masterSendTrim = fxSendGain;
        }
#endif

        for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
        {
            const auto sampleValue = oscillatorBusBuffer.getSample(sourceIndex, sample);
            const auto dryGate = sourceDryGateSmoothers[static_cast<std::size_t>(sourceIndex)].next();
            const auto sendGate = sourceSendGateSmoothers[static_cast<std::size_t>(sourceIndex)].next();
            const auto idxSource = static_cast<std::size_t>(sourceIndex);
            auto panL = sourcePanLeftSmoothers[idxSource].next(sourcePanLeft[idxSource]);
            auto panR = sourcePanRightSmoothers[idxSource].next(sourcePanRight[idxSource]);
            auto sourceLevel = sourceLevelSmoothers[idxSource].next(sourceLevelValues[idxSource]);
#if PX3_DIAGNOSTICS
            if (diagState.legacyUnsmoothedMixer)
            {
                panL = sourcePanLeft[idxSource];
                panR = sourcePanRight[idxSource];
                sourceLevel = sourceLevelValues[idxSource];
            }
#endif
            const auto sourceDryL = sampleValue * sourceLevel * panL * dryGate;
            const auto sourceDryR = sampleValue * sourceLevel * panR * dryGate;

            dryL += sourceDryL;
            dryR += sourceDryR;

            auto sendGain = sourceSendSmoothers[idxSource].next(sourceSendValues[idxSource]) * masterSendTrim;
#if PX3_DIAGNOSTICS
            if (diagState.legacyUnsmoothedMixer)
            {
                sendGain = sourceSendValues[idxSource] * fxSendGain;
            }
#endif
            // The FX send is an independent, centred contribution to the FX bus.
            // It deliberately does NOT use panL/panR: a source's dry pan places
            // its dry signal only, and must not steer that source's send. The
            // centre pan-law gain keeps the send level identical to what a
            // centred source produced before.
#if PX3_DIAGNOSTICS
            const auto sendPanL = diagState.legacyPostPanSend ? panL : kSendCentreGain;
            const auto sendPanR = diagState.legacyPostPanSend ? panR : kSendCentreGain;
#else
            const auto sendPanL = kSendCentreGain;
            const auto sendPanR = kSendCentreGain;
#endif
            const auto sendContributionL = sampleValue * sendPanL * sendGain * sendGate;
            const auto sendContributionR = sampleValue * sendPanR * sendGain * sendGate;
            fxInL += sendContributionL;
            fxInR += sendContributionR;

            mixerSourceEnergy[static_cast<std::size_t>(sourceIndex)] += static_cast<double>(sourceDryL) * static_cast<double>(sourceDryL)
                                                                         + static_cast<double>(sourceDryR) * static_cast<double>(sourceDryR);

#if PX3_DIAGNOSTICS
            if (diagState.capturing)
            {
                const auto di = static_cast<std::size_t>(sourceIndex);
                diagState.sourceDryEnergyL[di] += static_cast<double>(sourceDryL) * static_cast<double>(sourceDryL);
                diagState.sourceDryEnergyR[di] += static_cast<double>(sourceDryR) * static_cast<double>(sourceDryR);
                diagState.sourceSendEnergyL[di] += static_cast<double>(sendContributionL) * static_cast<double>(sendContributionL);
                diagState.sourceSendEnergyR[di] += static_cast<double>(sendContributionR) * static_cast<double>(sendContributionR);
            }
#endif

#if PX3_DIAGNOSTICS
            if (diagState.capturing)
            {
                const auto idx = static_cast<std::size_t>(sourceIndex);
                const auto dryGain = sourceLevel * panL * dryGate;
                const auto sendGainNow = sendGain * sendPanL * sendGate;
                if (diagState.hasPrevMixerGain)
                {
                    diagState.maxMixerDryGainStep =
                        juce::jmax(diagState.maxMixerDryGainStep,
                                   std::abs(dryGain - diagState.prevMixerDryGain[idx]));
                    diagState.maxMixerSendGainStep =
                        juce::jmax(diagState.maxMixerSendGainStep,
                                   std::abs(sendGainNow - diagState.prevMixerSendGain[idx]));
                }
                diagState.prevMixerDryGain[idx] = dryGain;
                diagState.prevMixerSendGain[idx] = sendGainNow;
            }
#endif
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
        const auto fxReturnGate = fxReturnGateSmoother.next();
        float fxPanLeft = 1.0f;
        float fxPanRight = 1.0f;
        panToGains(fxReturnPanSmoother.getNextValue(), fxPanLeft, fxPanRight);
        auto smoothedFxReturnGain = fxReturnGainSmoother.next(fxReturnGain);
#if PX3_DIAGNOSTICS
        if (diagState.legacyUnsmoothedMixer)
        {
            smoothedFxReturnGain = fxReturnGain;
        }
#endif
#if PX3_DIAGNOSTICS
        if (diagState.capturing)
        {
            const auto returnGainNow = smoothedFxReturnGain * fxPanLeft * fxReturnGate;
            if (diagState.hasPrevMixerGain)
            {
                diagState.maxFxReturnGainStep =
                    juce::jmax(diagState.maxFxReturnGainStep,
                               std::abs(returnGainNow - diagState.prevFxReturnGain));
            }
            diagState.prevFxReturnGain = returnGainNow;
            diagState.hasPrevMixerGain = true;
        }
#endif

        // The FX return carries the same source-side headroom as the
        // oscillators, so its fader is a true gain readout too.
        const auto fxHeadroom = px3::processor_internal::sourceHeadroomGain();
        const auto fxL = (stageL - fxInL) * fxHeadroom * smoothedFxReturnGain * fxPanLeft * fxReturnGate;
        const auto fxR = (stageR - fxInR) * fxHeadroom * smoothedFxReturnGain * fxPanRight * fxReturnGate;
        fxReturnEnergy += static_cast<double>(fxL) * static_cast<double>(fxL)
                  + static_cast<double>(fxR) * static_cast<double>(fxR);

#if PX3_DIAGNOSTICS
        if (diagState.capturing)
        {
            diagState.fxReturnEnergyL += static_cast<double>(fxL) * static_cast<double>(fxL);
            diagState.fxReturnEnergyR += static_cast<double>(fxR) * static_cast<double>(fxR);
            const auto mL = dryL + fxL;
            const auto mR = dryR + fxR;
            diagState.masterEnergyL += static_cast<double>(mL) * static_cast<double>(mL);
            diagState.masterEnergyR += static_cast<double>(mR) * static_cast<double>(mR);
            ++diagState.mixerSampleCount;
        }
#endif
        fxBusBuffer.setSample(0, sample, fxL);
        if (outputChannels > 1)
        {
            fxBusBuffer.setSample(1, sample, fxR);
        }

        // MASTER BUS: DRY + FX return, then the fixed output boost. Applied here
        // so the debug meters and clip counters see the level that actually
        // leaves the plugin.
        masterBusBuffer.setSample(0, sample, applyCeiling((dryL + fxL) * outputBoostGain));
        if (outputChannels > 1)
        {
            masterBusBuffer.setSample(1, sample, applyCeiling((dryR + fxR) * outputBoostGain));
        }
    }

    reverb.applyPostBlockCompensation(masterBusBuffer);

    for (int channel = 0; channel < outputChannels; ++channel)
    {
        buffer.copyFrom(channel, 0, masterBusBuffer, channel, 0, blockSamples);
    }

#if PX3_DIAGNOSTICS
    if (diagState.capturing && blockSamples > 0)
    {
        diagState.postPolySum.assign(static_cast<std::size_t>(blockSamples), 0.0f);
        for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
        {
            const auto* sourceData = oscillatorBusBuffer.getReadPointer(sourceIndex);
            for (int sample = 0; sample < blockSamples; ++sample)
            {
                diagState.postPolySum[static_cast<std::size_t>(sample)] += sourceData[sample];
            }
        }

        px3::diag::analyseBlock(diagState.postPolySum.data(),
                                masterBusBuffer.getReadPointer(0),
                                blockSamples);
    }
#endif

    if (blockSamples > 0)
    {
        // RMS, peak and the clip count are gathered in a single pass per bus.
        // Read separately this walked every bus three times, so the meters
        // touched roughly eighteen buffer-lengths per block whether or not the
        // editor was open. The reductions are unchanged: same accumulation
        // order, same double precision for energy.
        struct BusMeter
        {
            float rms { 0.0f };
            float peak { 0.0f };
            int clipSamples { 0 };
        };

        auto measureBus = [blockSamples, outputChannels](const juce::AudioBuffer<float>& src)
        {
            BusMeter meter;
            const auto chCount = juce::jmin(2, juce::jmin(outputChannels, src.getNumChannels()));
            if (chCount <= 0)
            {
                return meter;
            }

            double energy = 0.0;
            auto peak = 0.0f;
            auto clipped = 0;
            for (int ch = 0; ch < chCount; ++ch)
            {
                const auto* data = src.getReadPointer(ch);
                for (int i = 0; i < blockSamples; ++i)
                {
                    const auto value = data[i];
                    const auto s = static_cast<double>(value);
                    energy += s * s;
                    const auto magnitude = std::abs(value);
                    peak = juce::jmax(peak, magnitude);
                    if (magnitude > 1.0f)
                    {
                        ++clipped;
                    }
                }
            }

            const auto mean = energy / static_cast<double>(chCount * blockSamples);
            meter.rms = static_cast<float>(std::sqrt(juce::jmax(0.0, mean)));
            meter.peak = peak;
            meter.clipSamples = clipped;
            return meter;
        };

        const auto oscillatorMeter = measureBus(oscillatorBusBuffer);
        const auto dryMeter = measureBus(dryBusBuffer);
        const auto fxMeter = measureBus(fxBusBuffer);
        const auto masterMeter = measureBus(masterBusBuffer);

        debugOscillatorBusRms.store(oscillatorMeter.rms, std::memory_order_relaxed);
        debugDryBusRms.store(dryMeter.rms, std::memory_order_relaxed);
        debugFxBusRms.store(fxMeter.rms, std::memory_order_relaxed);
        debugMasterBusRms.store(masterMeter.rms, std::memory_order_relaxed);
        debugOscillatorBusPeak.store(oscillatorMeter.peak, std::memory_order_relaxed);
        debugDryBusPeak.store(dryMeter.peak, std::memory_order_relaxed);
        debugFxBusPeak.store(fxMeter.peak, std::memory_order_relaxed);
        debugMasterPreOutputPeak.store(masterMeter.peak, std::memory_order_relaxed);
        debugMasterBusPeak.store(masterMeter.peak, std::memory_order_relaxed);
        debugMasterClipSamples.store(masterMeter.clipSamples, std::memory_order_relaxed);

        const auto norm = static_cast<double>(juce::jmax(1, blockSamples)) * 2.0;
        for (int sourceIndex = 0; sourceIndex < kMixerSourceCount; ++sourceIndex)
        {
            const auto rms = static_cast<float>(std::sqrt(juce::jmax(0.0, mixerSourceEnergy[static_cast<std::size_t>(sourceIndex)] / norm)));
            debugMixerSourceRms[static_cast<std::size_t>(sourceIndex)].store(rms, std::memory_order_relaxed);

            auto sourcePeak = 0.0f;
            const auto sourceChannel = sourceIndex;
            if (sourceChannel < oscillatorBusBuffer.getNumChannels())
            {
                const auto* sourceData = oscillatorBusBuffer.getReadPointer(sourceChannel);
                for (int i = 0; i < blockSamples; ++i)
                {
                    sourcePeak = juce::jmax(sourcePeak, std::abs(sourceData[i]));
                }
            }
            debugMixerSourcePeak[static_cast<std::size_t>(sourceIndex)].store(sourcePeak, std::memory_order_relaxed);
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

