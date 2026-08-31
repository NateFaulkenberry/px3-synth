#include "PluginProcessor.h"
#include "WavetableFactory.h"

#include "CombResonator.h"
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

    fxProcessingOrderPacked.store(packFxOrder(px3::kDefaultFxOrder), std::memory_order_relaxed);
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
        {
            juce::StringArray tableNames;
            for (const auto& definition : px3::factoryWavetables())
            {
                tableNames.add(definition.name);
            }

            oscWtPositionParams[static_cast<std::size_t>(oscIndex)] =
                new juce::AudioParameterFloat(idPrefix + "WtPos",
                                              labelPrefix + "WT Position",
                                              juce::NormalisableRange<float>(0.0f, 1.0f),
                                              // Not 0. Frame 0 of the default
                                              // table is deliberately close to a
                                              // sine, so a scan that starts
                                              // there makes selecting WAVETABLE
                                              // sound like it did nothing.
                                              // Partway in is where the table
                                              // introduces itself.
                                              0.5f);
            oscWtTableParams[static_cast<std::size_t>(oscIndex)] =
                new juce::AudioParameterChoice(idPrefix + "WtTable",
                                               labelPrefix + "Wavetable",
                                               tableNames,
                                               0);
        }

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

        // ---- comb mode ----------------------------------------------------
        // Tune is skewed so the lower half of the knob covers the octaves that
        // matter musically; a linear 50 Hz - 8 kHz sweep would spend most of
        // its travel above the range anyone tunes a resonator to.
        filterCombTuneParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterFloat(
            idPrefix + "CombTune",
            labelPrefix + "Comb Tune",
            juce::NormalisableRange<float>(px3::CombResonator::kMinTuneHz,
                                           px3::CombResonator::kMaxTuneHz,
                                           0.01f,
                                           0.3f),
            220.0f);
        filterCombDecayParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterFloat(
            idPrefix + "CombDecay",
            labelPrefix + "Comb Decay",
            juce::NormalisableRange<float>(px3::CombResonator::kMinDecaySeconds,
                                           px3::CombResonator::kMaxDecaySeconds,
                                           0.001f,
                                           0.35f),
            0.6f);
        filterCombDampingParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterFloat(
            idPrefix + "CombDamping",
            labelPrefix + "Comb Damping",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.25f);
        filterCombDispersionParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterFloat(
            idPrefix + "CombDispersion",
            labelPrefix + "Comb Dispersion",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.0f);
        filterCombDriveParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterFloat(
            idPrefix + "CombDrive",
            labelPrefix + "Comb Drive",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.0f);
        filterCombMixParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterFloat(
            idPrefix + "CombMix",
            labelPrefix + "Comb Mix",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            1.0f);
        filterCombInvertParams[static_cast<std::size_t>(filterIndex)] = new juce::AudioParameterBool(
            idPrefix + "CombInvert",
            labelPrefix + "Comb Invert",
            false);
    }
    attackParam = new juce::AudioParameterFloat("ampAttack",
                                                 "Amp Attack",
                                                 juce::NormalisableRange<float>(0.001f, 3.0f, 0.001f, 0.45f),
                                                 0.005f);
    // Zero by default, so an envelope that has never been touched has exactly
    // the shape it had before the stage existed.
    holdParam = new juce::AudioParameterFloat("ampHold",
                                               "Amp Hold",
                                               juce::NormalisableRange<float>(0.0f, 4.0f, 0.001f, 0.45f),
                                               0.0f);
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
        holdParams[static_cast<std::size_t>(envIndex)] = new juce::AudioParameterFloat(
            idPrefix + "Hold",
            labelPrefix + "Hold",
            juce::NormalisableRange<float>(0.0f, 4.0f, 0.001f, 0.45f),
            0.0f);
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
        mixerPhaseInvertParams[static_cast<std::size_t>(i)] = new juce::AudioParameterBool("mix." + sourceId + ".phase",
                                                                                            sourceName + " Phase Invert",
                                                                                            false);
    }
    fxReturnMuteParam = new juce::AudioParameterBool("mix.fx.mute", "FX Return Mute", false);
    fxReturnSoloParam = new juce::AudioParameterBool("mix.fx.solo", "FX Return Solo", false);
    fxReturnPhaseInvertParam = new juce::AudioParameterBool("mix.fx.phase", "FX Return Phase Invert", false);

    // The dry bus. Its gain range matches a source channel's so the two read
    // the same on the fader, and it defaults to unity - the dry path behaves
    // exactly as it did before this channel existed until someone moves it.
    dryBusGainParam = new juce::AudioParameterFloat("mix.dry.level",
                                                    "Dry Level",
                                                    juce::NormalisableRange<float>(0.0f, px3::processor_internal::channelFaderMaxGain()),
                                                    1.0f);
    dryBusPanParam = new juce::AudioParameterFloat("mix.dry.pan",
                                                   "Dry Pan",
                                                   juce::NormalisableRange<float>(-1.0f, 1.0f),
                                                   0.0f);
    dryBusMuteParam = new juce::AudioParameterBool("mix.dry.mute", "Dry Mute", false);
    dryBusSoloParam = new juce::AudioParameterBool("mix.dry.solo", "Dry Solo", false);
    dryBusPhaseInvertParam = new juce::AudioParameterBool("mix.dry.phase", "Dry Phase Invert", false);
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
    // ---- DOOM ------------------------------------------------------------
    // A BAD MOOD-inspired two-channel processor. See docs/DOOM_DSP_DESIGN.md.
    doomEnabledParam = new juce::AudioParameterBool("doomEnabled", "Doom Enabled", true);
    doomFreezeParam = new juce::AudioParameterBool("doomFreeze", "Doom Freeze", false);
    doomLoopActiveParam = new juce::AudioParameterBool("doomLoopActive", "Doom Looper Active", false);
    doomWetActiveParam = new juce::AudioParameterBool("doomWetActive", "Doom Wet Active", true);
    doomLoopHalfParam = new juce::AudioParameterBool("doomLoopHalf", "Doom Loop Half", false);
    doomClockSmoothParam = new juce::AudioParameterBool("doomClockSmooth", "Doom Clock Smooth", false);
    // Zero by default, matching reverbAmount: adding an effect to the instrument
    // must not change what every existing patch sounds like.
    doomMixParam = new juce::AudioParameterFloat("doomMix", "Doom Mix", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    doomClockParam = new juce::AudioParameterFloat("doomClock", "Doom Clock", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    doomLoopLengthParam = new juce::AudioParameterFloat("doomLoopLength", "Doom Loop Length", juce::NormalisableRange<float>(0.0f, 1.0f), 0.45f);
    doomLoopModifyParam = new juce::AudioParameterFloat("doomLoopModify", "Doom Loop Modify", juce::NormalisableRange<float>(0.0f, 1.0f), 0.50f);
    doomOverdubParam = new juce::AudioParameterFloat("doomOverdub", "Doom Overdub", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    doomFadeParam = new juce::AudioParameterFloat("doomFade", "Doom Fade", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    doomWetTimeParam = new juce::AudioParameterFloat("doomWetTime", "Doom Wet Time", juce::NormalisableRange<float>(0.0f, 1.0f), 0.45f);
    doomWetModifyParam = new juce::AudioParameterFloat("doomWetModify", "Doom Wet Modify", juce::NormalisableRange<float>(0.0f, 1.0f), 0.40f);
    // Off by default: cross is confusing before you know what it does, and the
    // source pedal ships it off for the same reason.
    doomCrossParam = new juce::AudioParameterFloat("doomCross", "Doom Cross", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    doomGlueParam = new juce::AudioParameterFloat("doomGlue", "Doom Glue", juce::NormalisableRange<float>(0.0f, 1.0f), 0.15f);
    doomEqParam = new juce::AudioParameterFloat("doomEq", "Doom EQ", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f);
    doomBalanceParam = new juce::AudioParameterFloat("doomBalance", "Doom Balance", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    doomBlendParam = new juce::AudioParameterFloat("doomBlend", "Doom Blend", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    doomSpreadParam = new juce::AudioParameterFloat("doomSpread", "Doom Spread", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    doomRoutingParam = new juce::AudioParameterChoice("doomRouting",
                                                       "Doom Routing",
                                                       juce::StringArray { "INPUT", "INPUT+LOOP", "LOOP" },
                                                       0);
    doomLoopModeParam = new juce::AudioParameterChoice("doomLoopMode",
                                                        "Doom Loop Mode",
                                                        juce::StringArray { "BURST", "RADIO", "MASK" },
                                                        1);
    doomWetModeParam = new juce::AudioParameterChoice("doomWetMode",
                                                       "Doom Wet Mode",
                                                       juce::StringArray { "SOUP", "RELAY", "FLIP" },
                                                       0);
    doomCrossSourceParam = new juce::AudioParameterChoice("doomCrossSource",
                                                           "Doom Cross Source",
                                                           juce::StringArray { "INPUT", "CHANNEL" },
                                                           0);

    // ---- LUCY ------------------------------------------------------------
    // A Lossy-inspired spectral degradation engine. See docs/LUCY_DSP_DESIGN.md.
    lucyEnabledParam = new juce::AudioParameterBool("lucyEnabled", "Lucy Enabled", true);
    lucyFilterInvertParam = new juce::AudioParameterBool("lucyFilterInvert", "Lucy Filter Invert", false);
    lucyVerbPostParam = new juce::AudioParameterBool("lucyVerbPost", "Lucy Verb Post", false);
    lucyFreezeParam = new juce::AudioParameterBool("lucyFreeze", "Lucy Freeze", false);
    lucyFreezeSlushyParam = new juce::AudioParameterBool("lucyFreezeSlushy", "Lucy Freeze Slushy", false);
    lucyGateParam = new juce::AudioParameterBool("lucyGate", "Lucy Gate", false);
    lucySlowParam = new juce::AudioParameterBool("lucySlow", "Lucy Slow", false);
    // Zero by default, like reverbAmount and doomMix.
    lucyGlobalParam = new juce::AudioParameterFloat("lucyGlobal", "Lucy Global", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    lucyLossParam = new juce::AudioParameterFloat("lucyLoss", "Lucy Loss", juce::NormalisableRange<float>(0.0f, 1.0f), 0.55f);
    lucySpeedParam = new juce::AudioParameterFloat("lucySpeed", "Lucy Speed", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    lucyFilterParam = new juce::AudioParameterFloat("lucyFilter", "Lucy Filter", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    lucyFilterFreqParam = new juce::AudioParameterFloat("lucyFilterFreq", "Lucy Filter Freq", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    lucyVerbParam = new juce::AudioParameterFloat("lucyVerb", "Lucy Verb", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    lucyVerbDecayParam = new juce::AudioParameterFloat("lucyVerbDecay", "Lucy Verb Decay", juce::NormalisableRange<float>(0.0f, 1.0f), 0.45f);
    lucyFreezerParam = new juce::AudioParameterFloat("lucyFreezer", "Lucy Freezer", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    lucyGateCutoffParam = new juce::AudioParameterFloat("lucyGateCutoff", "Lucy Gate Cutoff", juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f);
    lucyThresholdParam = new juce::AudioParameterFloat("lucyThreshold", "Lucy Threshold", juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f);
    lucyAutoGainParam = new juce::AudioParameterFloat("lucyAutoGain", "Lucy Auto Gain", juce::NormalisableRange<float>(0.0f, 1.0f), 0.75f);
    lucyWeightingParam = new juce::AudioParameterFloat("lucyWeighting", "Lucy Weighting", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f);
    lucyGainParam = new juce::AudioParameterFloat("lucyGain", "Lucy Gain", juce::NormalisableRange<float>(-36.0f, 36.0f), 0.0f);
    lucySpreadParam = new juce::AudioParameterFloat("lucySpread", "Lucy Spread", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    lucyModeParam = new juce::AudioParameterChoice("lucyMode",
                                                    "Lucy Mode",
                                                    juce::StringArray { "STANDARD", "INVERSE", "JITTER" },
                                                    0);
    lucyPacketsParam = new juce::AudioParameterChoice("lucyPackets",
                                                       "Lucy Packets",
                                                       juce::StringArray { "CLEAN", "LOSS", "REPEAT" },
                                                       0);
    lucySlopeParam = new juce::AudioParameterChoice("lucySlope",
                                                     "Lucy Slope",
                                                     juce::StringArray { "6 dB", "24 dB", "96 dB" },
                                                     1);

    // ---- CHORUS ----------------------------------------------------------
    // Dimension D-inspired. See docs/CHORUS_DSP_DESIGN.md.
    chorusEnabledParam = new juce::AudioParameterBool("chorusEnabled", "Chorus Enabled", true);
    // Zero by default, like reverbAmount: adding an effect must not change
    // what existing patches sound like.
    chorusAmountParam = new juce::AudioParameterFloat("chorusAmount", "Chorus Amount", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    chorusRateParam = new juce::AudioParameterFloat("chorusRate", "Chorus Rate", juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f);
    chorusDepthParam = new juce::AudioParameterFloat("chorusDepth", "Chorus Depth", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    chorusWidthParam = new juce::AudioParameterFloat("chorusWidth", "Chorus Width", juce::NormalisableRange<float>(0.0f, 1.0f), 0.75f);
    chorusSpreadParam = new juce::AudioParameterFloat("chorusSpread", "Chorus Spread", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    chorusLowCutParam = new juce::AudioParameterFloat("chorusLowCut", "Chorus Low Cut", juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f);
    chorusFeedbackParam = new juce::AudioParameterFloat("chorusFeedback", "Chorus Feedback", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    chorusCharacterParam = new juce::AudioParameterFloat("chorusCharacter", "Chorus Character", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    chorusMixParam = new juce::AudioParameterFloat("chorusMix", "Chorus Mix", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    chorusToneParam = new juce::AudioParameterFloat("chorusTone", "Chorus Tone", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f);
    chorusModeParam = new juce::AudioParameterChoice("chorusMode",
                                                      "Chorus Mode",
                                                      juce::StringArray { "DIM 1", "DIM 2", "DIM 3", "DIM 4",
                                                                          "DIM 1+4", "DIM 2+4", "DIM 3+4",
                                                                          "ENSEMBLE", "CE WARM" },
                                                      1);

    // ---- STEREO SPREAD ---------------------------------------------------
    // See docs/STEREO_SPREAD_DSP_DESIGN.md.
    spreadEnabledParam = new juce::AudioParameterBool("spreadEnabled", "Spread Enabled", true);
    spreadAmountParam = new juce::AudioParameterFloat("spreadAmount", "Spread Amount", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    spreadWidthParam = new juce::AudioParameterFloat("spreadWidth", "Spread Width", juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f);
    spreadDepthParam = new juce::AudioParameterFloat("spreadDepth", "Spread Depth", juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f);
    spreadCenterParam = new juce::AudioParameterFloat("spreadCenter", "Spread Center", juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f);
    spreadLowWidthParam = new juce::AudioParameterFloat("spreadLowWidth", "Spread Low Width", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    spreadHighWidthParam = new juce::AudioParameterFloat("spreadHighWidth", "Spread High Width", juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f);
    spreadLowFreqParam = new juce::AudioParameterFloat("spreadLowFreq", "Spread Low Freq", juce::NormalisableRange<float>(0.0f, 1.0f), 0.55f);
    spreadHighFreqParam = new juce::AudioParameterFloat("spreadHighFreq", "Spread High Freq", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    spreadMixParam = new juce::AudioParameterFloat("spreadMix", "Spread Mix", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    spreadToneParam = new juce::AudioParameterFloat("spreadTone", "Spread Tone", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f);
    spreadModeParam = new juce::AudioParameterChoice("spreadMode",
                                                      "Spread Mode",
                                                      juce::StringArray { "CLASSIC", "WIDE", "DEEP", "MONO SAFE" },
                                                      0);

    // ---- ANALOG ENGINE ---------------------------------------------------
    // Only these two are user-facing. See docs/ANALOG_ENGINE_ARCHITECTURE.md.
    //
    // Off by default: this changes the sound of every existing patch, so it is
    // opt-in until it has been listened to rather than silently rewriting the
    // instrument's tone on upgrade.
    analogEnabledParam = new juce::AudioParameterBool("analogEnabled", "Analog Enabled", false);
    analogProfileParam = new juce::AudioParameterChoice("analogProfile",
                                                         "Analog Profile",
                                                         px3::AnalogEngine::profileNames(),
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
        addParameter(oscWtPositionParams[static_cast<std::size_t>(oscIndex)]);
        addParameter(oscWtTableParams[static_cast<std::size_t>(oscIndex)]);

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
        // Registered like every other parameter, which is what puts them in the
        // DAW's automation list, the session state and preset files - all of
        // which iterate getParameters().
        addParameter(filterCombTuneParams[static_cast<std::size_t>(filterIndex)]);
        addParameter(filterCombDecayParams[static_cast<std::size_t>(filterIndex)]);
        addParameter(filterCombDampingParams[static_cast<std::size_t>(filterIndex)]);
        addParameter(filterCombDispersionParams[static_cast<std::size_t>(filterIndex)]);
        addParameter(filterCombDriveParams[static_cast<std::size_t>(filterIndex)]);
        addParameter(filterCombMixParams[static_cast<std::size_t>(filterIndex)]);
        addParameter(filterCombInvertParams[static_cast<std::size_t>(filterIndex)]);
    }
    addParameter(attackParam);
    addParameter(holdParam);
    addParameter(decayParam);
    addParameter(sustainParam);
    addParameter(releaseParam);
    addParameter(ampEnvEnabledParam);

    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        addParameter(attackParams[static_cast<std::size_t>(envIndex)]);
        addParameter(holdParams[static_cast<std::size_t>(envIndex)]);
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
        addParameter(mixerPhaseInvertParams[static_cast<std::size_t>(i)]);
        addParameter(mixerSoloParams[static_cast<std::size_t>(i)]);
    }
    addParameter(fxReturnMuteParam);
    addParameter(fxReturnPhaseInvertParam);
    addParameter(dryBusGainParam);
    addParameter(dryBusPanParam);
    addParameter(dryBusMuteParam);
    addParameter(dryBusSoloParam);
    addParameter(dryBusPhaseInvertParam);
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

    addParameter(doomEnabledParam);
    addParameter(doomFreezeParam);
    addParameter(doomLoopActiveParam);
    addParameter(doomWetActiveParam);
    addParameter(doomLoopHalfParam);
    addParameter(doomClockSmoothParam);
    addParameter(doomMixParam);
    addParameter(doomClockParam);
    addParameter(doomLoopLengthParam);
    addParameter(doomLoopModifyParam);
    addParameter(doomOverdubParam);
    addParameter(doomFadeParam);
    addParameter(doomWetTimeParam);
    addParameter(doomWetModifyParam);
    addParameter(doomCrossParam);
    addParameter(doomGlueParam);
    addParameter(doomEqParam);
    addParameter(doomBalanceParam);
    addParameter(doomBlendParam);
    addParameter(doomSpreadParam);
    addParameter(doomRoutingParam);
    addParameter(doomLoopModeParam);
    addParameter(doomWetModeParam);
    addParameter(doomCrossSourceParam);

    addParameter(lucyEnabledParam);
    addParameter(lucyFilterInvertParam);
    addParameter(lucyVerbPostParam);
    addParameter(lucyFreezeParam);
    addParameter(lucyFreezeSlushyParam);
    addParameter(lucyGateParam);
    addParameter(lucySlowParam);
    addParameter(lucyGlobalParam);
    addParameter(lucyLossParam);
    addParameter(lucySpeedParam);
    addParameter(lucyFilterParam);
    addParameter(lucyFilterFreqParam);
    addParameter(lucyVerbParam);
    addParameter(lucyVerbDecayParam);
    addParameter(lucyFreezerParam);
    addParameter(lucyGateCutoffParam);
    addParameter(lucyThresholdParam);
    addParameter(lucyAutoGainParam);
    addParameter(lucyWeightingParam);
    addParameter(lucyGainParam);
    addParameter(lucySpreadParam);
    addParameter(lucyModeParam);
    addParameter(lucyPacketsParam);
    addParameter(lucySlopeParam);

    addParameter(chorusEnabledParam);
    addParameter(chorusAmountParam);
    addParameter(chorusRateParam);
    addParameter(chorusDepthParam);
    addParameter(chorusWidthParam);
    addParameter(chorusSpreadParam);
    addParameter(chorusLowCutParam);
    addParameter(chorusFeedbackParam);
    addParameter(chorusCharacterParam);
    addParameter(chorusMixParam);
    addParameter(chorusToneParam);
    addParameter(chorusModeParam);

    addParameter(spreadEnabledParam);

    // Bus inserts. Dry first, FX second - the order the signal meets them.
    createBusInsertParameters(0, "dry", "Dry Bus");
    createBusInsertParameters(1, "fx", "FX Bus");
    addParameter(spreadAmountParam);
    addParameter(spreadWidthParam);
    addParameter(spreadDepthParam);
    addParameter(spreadCenterParam);
    addParameter(spreadLowWidthParam);
    addParameter(spreadHighWidthParam);
    addParameter(spreadLowFreqParam);
    addParameter(spreadHighFreqParam);
    addParameter(spreadMixParam);
    addParameter(spreadToneParam);
    addParameter(spreadModeParam);

    addParameter(analogEnabledParam);
    addParameter(analogProfileParam);
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

// One set of insert parameters for a bus. Namespaced by prefix so a third bus
// adds a prefix rather than a scheme: see docs/EQ_COMP_RESEARCH.md.
//
// Defaults are chosen so that switching an insert on changes nothing until a
// control is moved - a bus processor that colours the sound the moment it is
// enabled cannot be A/B'd, and every band starts at 0 dB for the same reason.
void PX3SynthAudioProcessor::createBusInsertParameters(int bus,
                                                       const juce::String& idPrefix,
                                                       const juce::String& label)
{
    auto& p = busInsertParams[static_cast<std::size_t>(bus)];

    p.eqEnabled = new juce::AudioParameterBool(idPrefix + "EqEnabled", label + " EQ Enabled", false);

    // Band 1 is a low shelf or a high pass, band 4 a high shelf or a low pass;
    // the inner two are bells. The outer bands switch because the most useful
    // move on a bus is a shelf and the most useful move on an FX return that is
    // too bright is a low pass.
    static const juce::StringArray outerLowChoices { "Low Shelf", "High Pass" };
    static const juce::StringArray outerHighChoices { "High Shelf", "Low Pass" };

    static constexpr std::array<float, px3::kEqBandCount> defaultFreq { { 100.0f, 300.0f, 3000.0f, 8000.0f } };
    static constexpr std::array<float, px3::kEqBandCount> defaultQ { { 0.707f, 0.9f, 0.9f, 0.707f } };

    for (int band = 0; band < px3::kEqBandCount; ++band)
    {
        const auto b = static_cast<std::size_t>(band);
        const auto n = juce::String(band + 1);
        const auto bandLabel = label + " EQ " + n + " ";

        // Only the outer bands have a type to choose. The inner two are always
        // bells, so they get no parameter at all rather than a one-entry choice:
        // a choice of one has a zero-width range, and normalising against it
        // divides by zero - which is exactly how the state tests found this.
        if (band == 0 || band == 3)
        {
            p.bandType[b] = new juce::AudioParameterChoice(
                idPrefix + "EqType" + n, bandLabel + "Type",
                band == 0 ? outerLowChoices : outerHighChoices, 0);
        }

        // Logarithmic: an EQ knob that spends half its travel above 10 kHz is
        // unusable, and frequency is perceived logarithmically.
        p.bandFreq[b] = new juce::AudioParameterFloat(
            idPrefix + "EqFreq" + n, bandLabel + "Frequency",
            juce::NormalisableRange<float>(px3::ParametricEQ::kMinFrequencyHz,
                                           px3::ParametricEQ::kMaxFrequencyHz, 0.0f, 0.25f),
            defaultFreq[b]);

        p.bandGain[b] = new juce::AudioParameterFloat(
            idPrefix + "EqGain" + n, bandLabel + "Gain",
            juce::NormalisableRange<float>(px3::ParametricEQ::kMinGainDb,
                                           px3::ParametricEQ::kMaxGainDb), 0.0f);

        p.bandQ[b] = new juce::AudioParameterFloat(
            idPrefix + "EqQ" + n, bandLabel + "Q",
            juce::NormalisableRange<float>(px3::ParametricEQ::kMinQ,
                                           px3::ParametricEQ::kMaxQ, 0.0f, 0.4f),
            defaultQ[b]);
    }

    p.compEnabled = new juce::AudioParameterBool(idPrefix + "CompEnabled", label + " Comp Enabled", false);

    // INPUT is the primary control: the 1176 has no threshold, and driving the
    // input into a fixed threshold is the actual workflow.
    p.compInput = new juce::AudioParameterFloat(
        idPrefix + "CompInput", label + " Comp Input",
        juce::NormalisableRange<float>(-12.0f, 36.0f), 0.0f);
    p.compOutput = new juce::AudioParameterFloat(
        idPrefix + "CompOutput", label + " Comp Output",
        juce::NormalisableRange<float>(-24.0f, 24.0f), 0.0f);

    // 0..1 where 1 is FASTEST, matching the hardware's reversed panel.
    p.compAttack = new juce::AudioParameterFloat(
        idPrefix + "CompAttack", label + " Comp Attack",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f);
    p.compRelease = new juce::AudioParameterFloat(
        idPrefix + "CompRelease", label + " Comp Release",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);

    p.compRatio = new juce::AudioParameterChoice(
        idPrefix + "CompRatio", label + " Comp Ratio",
        juce::StringArray { "4:1", "8:1", "12:1", "20:1", "All Buttons" }, 0);

    p.compMix = new juce::AudioParameterFloat(
        idPrefix + "CompMix", label + " Comp Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);

    p.compLink = new juce::AudioParameterBool(idPrefix + "CompLink", label + " Comp Stereo Link", true);

    // What the movement is wired to. A parameter rather than UI state so it
    // survives a session and travels with a preset - a meter that resets to a
    // different source every time the editor opens is worse than no switch.
    p.compMeterMode = new juce::AudioParameterChoice(
        idPrefix + "CompMeterMode", label + " Comp Meter",
        juce::StringArray { "GR", "IN", "OUT" }, 0);

    addParameter(p.eqEnabled);
    for (int band = 0; band < px3::kEqBandCount; ++band)
    {
        const auto b = static_cast<std::size_t>(band);
        if (p.bandType[b] != nullptr)
        {
            addParameter(p.bandType[b]);
        }
        addParameter(p.bandFreq[b]);
        addParameter(p.bandGain[b]);
        addParameter(p.bandQ[b]);
    }
    addParameter(p.compEnabled);
    addParameter(p.compInput);
    addParameter(p.compOutput);
    addParameter(p.compAttack);
    addParameter(p.compRelease);
    addParameter(p.compRatio);
    addParameter(p.compMix);
    addParameter(p.compLink);
    addParameter(p.compMeterMode);
}

// Read once per block and handed to the chain. The processors smooth their own
// parameters per sample; this only has to deliver targets.
void PX3SynthAudioProcessor::updateBusInsertSettings(int bus)
{
    const auto i = static_cast<std::size_t>(bus);
    const auto& p = busInsertParams[i];
    if (p.eqEnabled == nullptr)
    {
        return;
    }

    px3::EqSettings eq;
    eq.enabled = p.eqEnabled->get();
    for (int band = 0; band < px3::kEqBandCount; ++band)
    {
        const auto b = static_cast<std::size_t>(band);
        auto& target = eq.bands[b];

        // Band 1 chooses between low shelf and high pass, band 4 between high
        // shelf and low pass; the middle two are always bells.
        if (band == 0 && p.bandType[b] != nullptr)
        {
            target.type = p.bandType[b]->getIndex() == 0 ? px3::EqBandType::lowShelf
                                                         : px3::EqBandType::highPass;
        }
        else if (band == 3 && p.bandType[b] != nullptr)
        {
            target.type = p.bandType[b]->getIndex() == 0 ? px3::EqBandType::highShelf
                                                         : px3::EqBandType::lowPass;
        }
        else
        {
            target.type = px3::EqBandType::bell;
        }

        target.frequencyHz = p.bandFreq[b]->get();
        target.gainDb = p.bandGain[b]->get();
        target.q = p.bandQ[b]->get();
    }

    px3::CompressorSettings comp;
    comp.enabled = p.compEnabled->get();
    comp.inputDb = p.compInput->get();
    comp.outputDb = p.compOutput->get();
    comp.attack = p.compAttack->get();
    comp.release = p.compRelease->get();
    comp.ratio = static_cast<px3::CompRatio>(juce::jlimit(0, 4, p.compRatio->getIndex()));
    comp.mix = p.compMix->get();
    comp.stereoLink = p.compLink->get();
    comp.meterMode = static_cast<px3::CompMeterMode>(juce::jlimit(0, 2, p.compMeterMode->getIndex()));

    busInserts[i].setSettings(eq, comp);
}

void PX3SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Synchronously, so the very first block already has a table to read rather
    // than a block of silence while the message thread catches up.
    refreshWavetableSelections();

    currentSampleRateHz = juce::jmax(1.0, sampleRate);
    soundingVoiceBudget.store(soundingVoiceBudgetForRate(currentSampleRateHz),
                              std::memory_order_relaxed);
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
    doomComponent.prepare(sampleRate);
    lucyComponent.prepare(sampleRate);
    chorusComponent.prepare(sampleRate);
    stereoSpreadComponent.prepare(sampleRate);
    // Four mono source channels plus three stereo bus contexts.
    analogEngine.prepare(sampleRate, kMixerSourceCount);
    for (auto& insert : busInserts)
    {
        insert.prepare(sampleRate);
    }
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
            sourcePhaseSmoothers[idx].prepare(sampleRate, kMixerSmoothingSeconds);
            sourcePhaseSmoothers[idx].setCurrent(getMixerPhaseInvertParam(i).get() ? -1.0f : 1.0f);
            sourceSendSmoothers[idx].setCurrent(juce::jlimit(0.0f, 1.0f, getMixerSendParam(i).get()));

            float left = 1.0f;
            float right = 1.0f;
            panToGainsStatic(juce::jlimit(-1.0f, 1.0f, getMixerPanParam(i).get()), left, right);
            sourcePanLeftSmoothers[idx].setCurrent(left);
            sourcePanRightSmoothers[idx].setCurrent(right);
        }
    }
    fxSendGainSmoother.prepare(sampleRate, kMixerSmoothingSeconds);
    dryBusGainSmoother.prepare(sampleRate, kMixerSmoothingSeconds);
    dryBusGainSmoother.setCurrent(dryBusGainParam != nullptr ? dryBusGainParam->get() : 1.0f);
    dryBusPhaseSmoother.prepare(sampleRate, kMixerSmoothingSeconds);
    dryBusPhaseSmoother.setCurrent(dryBusPhaseInvertParam != nullptr && dryBusPhaseInvertParam->get() ? -1.0f : 1.0f);
    dryBusGateSmoother.prepare(sampleRate, 0.010);
    dryBusGateSmoother.setCurrent(dryBusAudible(anyChannelSoloed(),
                                                anySourceSoloed(),
                                                dryBusSoloParam != nullptr && dryBusSoloParam->get()));
    dryBusPanSmoother.reset(sampleRate, 0.012);
    dryBusPanSmoother.setCurrentAndTargetValue(dryBusPanParam != nullptr
                                                   ? juce::jlimit(-1.0f, 1.0f, dryBusPanParam->get())
                                                   : 0.0f);

    fxReturnPhaseSmoother.prepare(sampleRate, kMixerSmoothingSeconds);
    fxReturnPhaseSmoother.setCurrent(fxReturnPhaseInvertParam != nullptr && fxReturnPhaseInvertParam->get() ? -1.0f : 1.0f);
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

    const auto shapedAmp = currentAmpEnvelope();
    std::array<px3::BreakpointEnvelope, kEnvelopeSourceCount> shapedMod;
    auto anyEnvelopeIsShaped = ! shapedAmp.isPlainAdsr();
    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        shapedMod[static_cast<std::size_t>(envIndex)] = currentModEnvelope(envIndex);
        anyEnvelopeIsShaped = anyEnvelopeIsShaped
                              || ! shapedMod[static_cast<std::size_t>(envIndex)].isPlainAdsr();
    }

    for (auto* voice : typedVoices)
    {
        if (voice != nullptr)
        {
            voice->setAmpEnvelope(ampEnvelope);
            voice->setAmpEnvelopeEnabled(ampEnvelopeEnabled);
            voice->setModEnvelopeSettings(modEnvelopeSettings, modEnvelopeEnabled);

            // Only when the envelope is more than ADSR. Pushing the full shape
            // unconditionally would copy 384 bytes per envelope per voice per
            // block to say what the four parameters just said.
            if (anyEnvelopeIsShaped)
            {
                voice->setAmpEnvelopeShape(shapedAmp);
                voice->setModEnvelopeShapes(shapedMod);
            }
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

int PX3SynthAudioProcessor::soundingVoiceBudgetForRate(double sampleRate)
{
    if (sampleRate <= 0.0)
    {
        return kSoundingVoiceBudgetAtReference;
    }

    const auto scaled = static_cast<double>(kSoundingVoiceBudgetAtReference)
                        * kVoiceBudgetReferenceRate / sampleRate;

    return juce::jlimit(kMinimumSoundingVoiceBudget,
                        kSoundingVoiceBudgetAtReference,
                        static_cast<int>(std::lround(scaled)));
}

void PX3SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const auto blockStartTicks = juce::Time::getHighResolutionTicks();

    // Once per block, before anything reads a table: this is the contract that
    // lets WavetableSlot retire a replaced table safely - see its comment.
    for (int osc = 0; osc < kOscillatorSourceCount; ++osc)
    {
        wavetableSlots[static_cast<std::size_t>(osc)].beginBlock();

        // Asking for a table that has not been built yet cannot build it here -
        // that allocates megabytes. Flagging the message thread is lock-free.
        if (getOscillatorWtTableParam(osc).getIndex()
            != loadedWavetableIndex[static_cast<std::size_t>(osc)])
        {
            triggerAsyncUpdate();
        }
    }
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

    const auto shapedAmp = currentAmpEnvelope();
    std::array<px3::BreakpointEnvelope, kEnvelopeSourceCount> shapedMod;
    auto anyEnvelopeIsShaped = ! shapedAmp.isPlainAdsr();
    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        shapedMod[static_cast<std::size_t>(envIndex)] = currentModEnvelope(envIndex);
        anyEnvelopeIsShaped = anyEnvelopeIsShaped
                              || ! shapedMod[static_cast<std::size_t>(envIndex)].isPlainAdsr();
    }

    for (int voiceIndex = 0; voiceIndex < kPolyphonyVoiceCount; ++voiceIndex)
    {
        if (auto* voice = typedVoices[static_cast<std::size_t>(voiceIndex)])
        {
            voice->setAmpEnvelope(ampEnvelope);
            voice->setAmpEnvelopeEnabled(ampEnvelopeEnabled);
            voice->setModEnvelopeSettings(modEnvelopeSettings, modEnvelopeEnabled);

            // Only when the envelope is more than ADSR. Pushing the full shape
            // unconditionally would copy 384 bytes per envelope per voice per
            // block to say what the four parameters just said.
            if (anyEnvelopeIsShaped)
            {
                voice->setAmpEnvelopeShape(shapedAmp);
                voice->setModEnvelopeShapes(shapedMod);
            }
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
    // ---- sounding-voice budget ------------------------------------------
    // Release pruning above caps TAILS. Held notes were exempt, which is right
    // musically and is also why the plugin could be driven past its real-time
    // budget: 64 held voices with every effect enabled measured 108.7% of the
    // block budget at 48 kHz and 211.2% at 96 kHz. The failure mode was
    // dropouts - the whole block late, every voice affected - rather than
    // losing the quietest note, which is what every synth does when it runs
    // out of capacity.
    //
    // So the total number of SOUNDING voices is budgeted too. The quietest go
    // first, oldest breaking ties, through the same fade the tail pruner uses,
    // because a hard stop here is a step discontinuity in the summed output.
#if PX3_DIAGNOSTICS
    if (!px3::diag::state().disableReleasePruning)
#endif
    {
        const auto budget = soundingVoiceBudget.load(std::memory_order_relaxed);

        auto soundingCount = 0;
        for (auto* voice : typedVoices)
        {
            if (voice != nullptr && voice->isVoiceActive() && !voice->isFastReleasing())
            {
                if (soundingCount < static_cast<int>(releaseCandidateScratch.size()))
                {
                    releaseCandidateScratch[static_cast<std::size_t>(soundingCount++)] = voice;
                }
            }
        }

        if (soundingCount > budget)
        {
            std::sort(releaseCandidateScratch.begin(),
                      releaseCandidateScratch.begin() + soundingCount,
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

            const auto overBudget = soundingCount - budget;
            for (int i = 0; i < overBudget; ++i)
            {
                if (auto* voice = releaseCandidateScratch[static_cast<std::size_t>(i)])
                {
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
    doomComponent.updateForBlock(currentDoomSettings());
    lucyComponent.updateForBlock(currentLucySettings());
    chorusComponent.updateForBlock(currentChorusSettings());
    stereoSpreadComponent.updateForBlock(currentStereoSpreadSettings());

    // The engine's amount is a tuning constant; the parameter only gates it.
    // Smoothed inside the engine, so toggling it does not click.
    if (analogProfileParam != nullptr)
    {
        analogEngine.setProfile(static_cast<px3::AnalogEngine::Profile>(
            juce::jlimit(0, px3::AnalogEngine::kProfileCount - 1, analogProfileParam->getIndex())));
    }
    analogEngine.setAmount((analogEnabledParam != nullptr && analogEnabledParam->get()) ? 1.0f : 0.0f);

    for (int bus = 0; bus < kBusInsertCount; ++bus)
    {
        updateBusInsertSettings(bus);
    }
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
        sourcePhaseValues[static_cast<std::size_t>(sourceIndex)] =
            getMixerPhaseInvertParam(sourceIndex).get() ? -1.0f : 1.0f;

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

    const auto drySolo = dryBusSoloParam != nullptr && dryBusSoloParam->get();
    dryBusGateSmoother.setTarget(dryBusAudible(anySolo, anySourceSolo, drySolo));
    dryBusPanSmoother.setTargetValue(dryBusPanParam != nullptr
                                         ? juce::jlimit(-1.0f, 1.0f, dryBusPanParam->get())
                                         : 0.0f);

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
            const auto idxPhase = static_cast<std::size_t>(sourceIndex);
            // Polarity first, so the flip reaches the dry path and the FX send
            // alike - it is a property of the channel, not of one destination.
            // ANALOG CHANNEL. Placed where a console strip sits: after polarity,
            // before the fader and the pan. Mono, because the source channels
            // are mono until they are panned into the bus - which is what a
            // channel strip is.
            //
            // On its own this is not a saturator. It is the forward half of an
            // invertible pair; the bus runs the inverse, so ONE channel through
            // the console is transparent and the character comes from summing.
            const auto sampleValue = analogEngine.processChannelSample(
                sourceIndex,
                oscillatorBusBuffer.getSample(sourceIndex, sample)
                    * sourcePhaseSmoothers[idxPhase].next(sourcePhaseValues[idxPhase]));
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

        // ---- DRY CHANNEL --------------------------------------------------
        // The dry bus gets the same treatment a source channel does - level,
        // pan, polarity, mute/solo - applied to the sum rather than to each
        // source. Its pan is a bus pan: it moves the already-panned mix, which
        // is what a mixer's dry return does.
        {
            const auto dryGain = dryBusGainSmoother.next(dryBusGainParam != nullptr ? dryBusGainParam->get() : 1.0f);
            const auto dryPhase = dryBusPhaseSmoother.next(
                (dryBusPhaseInvertParam != nullptr && dryBusPhaseInvertParam->get()) ? -1.0f : 1.0f);
            const auto dryGate = dryBusGateSmoother.next();

            float dryPanLeft = 1.0f;
            float dryPanRight = 1.0f;
            panToGains(dryBusPanSmoother.getNextValue(), dryPanLeft, dryPanRight);

            // Normalised so centre is unity.
            //
            // panToGains is an equal-power law, which puts 0.707 on each side at
            // centre. That is right for a SOURCE, where the two halves sum to
            // constant power as it moves across the field. Applied to the dry
            // bus it would drop the whole dry path 3 dB the moment this channel
            // existed - which is exactly what the mixer headroom tests caught.
            constexpr auto centreGain = juce::MathConstants<float>::sqrt2;
            dryPanLeft *= centreGain;
            dryPanRight *= centreGain;

            const auto dryScale = dryGain * dryPhase * dryGate;
            dryL *= dryScale * dryPanLeft;
            dryR *= dryScale * dryPanRight;
        }

        // ANALOG DRY BUS. The inverse of what the channels did - so a single
        // channel comes back out unchanged, and several channels summed do not.
        // This is where the character actually appears.
        analogEngine.processBusSample(px3::AnalogEngine::Context::dryBus, dryL, dryR);

        // DRY BUS INSERTS: EQ then compressor. Last stage on the dry bus before
        // the master sum, and post-fader because the invertible console pair
        // above has to stay adjacent to its channel half.
        busInserts[0].processSample(dryL, dryR);

        dryBusBuffer.setSample(0, sample, dryL);
        if (outputChannels > 1)
        {
            dryBusBuffer.setSample(1, sample, dryR);
        }

        // ANALOG FX BUS. On the send sum, BEFORE the chain: an aux send is
        // summed on its own bus and the effects receive what that bus produced.
        // It also has to be here rather than after the chain because the return
        // below is a difference (stage - send), and a stage applied after the
        // chain would have the untouched send subtracted back out of it.
        analogEngine.processBusSample(px3::AnalogEngine::Context::fxBus, fxInL, fxInR);

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

                case 4: // Doom
                    doomComponent.processSampleFrame(stageL, stageR, stageL, stageR);
                    break;

                case 5: // Lucy
                    lucyComponent.processSampleFrame(stageL, stageR, stageL, stageR);
                    break;

                case 6: // Chorus
                    chorusComponent.processSampleFrame(stageL, stageR, stageL, stageR);
                    break;

                case 7: // Stereo Spread
                    stereoSpreadComponent.processSampleFrame(stageL, stageR, stageL, stageR);
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
        const auto fxPhaseTarget = (fxReturnPhaseInvertParam != nullptr && fxReturnPhaseInvertParam->get()) ? -1.0f : 1.0f;
        auto smoothedFxReturnGain = fxReturnGainSmoother.next(fxReturnGain)
                                    * fxReturnPhaseSmoother.next(fxPhaseTarget);
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

        // FX BUS INSERTS: on the recovered wet signal, after the (stage - send)
        // difference and the headroom scale, but BEFORE the return fader. Feed
        // it the difference and the inserts see only wet; put it after the fader
        // and riding the blend would change the compression.
        auto wetL = (stageL - fxInL) * fxHeadroom;
        auto wetR = (stageR - fxInR) * fxHeadroom;
        busInserts[1].processSample(wetL, wetR);

        const auto fxL = wetL * smoothedFxReturnGain * fxPanLeft * fxReturnGate;
        const auto fxR = wetR * smoothedFxReturnGain * fxPanRight * fxReturnGate;
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
        // ANALOG MASTER. An output stage rather than a summing amplifier, so it
        // saturates forward like a channel does, and it works on mid/side
        // because stereo behaviour at a master bus is a property of the summing.
        //
        // Before the output ceiling, never after: the ceiling is the
        // instrument's guarantee that nothing can clip, so it stays last.
        auto masterL = (dryL + fxL) * outputBoostGain;
        auto masterR = (dryR + fxR) * outputBoostGain;
        analogEngine.processBusSample(px3::AnalogEngine::Context::master, masterL, masterR);

        masterBusBuffer.setSample(0, sample, applyCeiling(masterL));
        if (outputChannels > 1)
        {
            masterBusBuffer.setSample(1, sample, applyCeiling(masterR));
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

