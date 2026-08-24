#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "OscillatorMode.h"
#include "SubOscMode.h"

using namespace px3::processor_internal;

SubtractiveSettings PX3SynthAudioProcessor::currentSubtractiveSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    SubtractiveSettings settings;
    settings.sineMix = oscSineParam->convertFrom0to1(applyLfoToNormalizedValue(oscSineParam,
                                                                                static_cast<juce::RangedAudioParameter*>(oscSineParam)->getValue(),
                                                                                lfoSignal));
    settings.sawMix = oscSawParam->convertFrom0to1(applyLfoToNormalizedValue(oscSawParam,
                                                                              static_cast<juce::RangedAudioParameter*>(oscSawParam)->getValue(),
                                                                              lfoSignal));
    settings.squareMix = oscSquareParam->convertFrom0to1(applyLfoToNormalizedValue(oscSquareParam,
                                                                                    static_cast<juce::RangedAudioParameter*>(oscSquareParam)->getValue(),
                                                                                    lfoSignal));
    settings.masterGain = masterGainParam->convertFrom0to1(applyLfoToNormalizedValue(masterGainParam,
                                                                                      static_cast<juce::RangedAudioParameter*>(masterGainParam)->getValue(),
                                                                                      lfoSignal));
    return settings;
}

SubOscSettings PX3SynthAudioProcessor::currentSubOscillatorSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    SubOscSettings settings;
    settings.enabled = subOscEnabledParam != nullptr && subOscEnabledParam->get();
    settings.level = subOscLevelParam->convertFrom0to1(applyLfoToNormalizedValue(subOscLevelParam,
                                                                                  static_cast<juce::RangedAudioParameter*>(subOscLevelParam)->getValue(),
                                                                                  lfoSignal));
    settings.octaveIndex = px3::clampSubOscOctaveIndex(subOscOctaveParam != nullptr ? subOscOctaveParam->getIndex() : 1);
    settings.waveformIndex = px3::clampSubOscWaveformIndex(subOscWaveformParam != nullptr ? subOscWaveformParam->getIndex() : 1);
    return settings;
}

FilterSettings PX3SynthAudioProcessor::currentFilterSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    FilterSettings settings;
    settings.cutoffHz = filterCutoffParam->convertFrom0to1(applyLfoToNormalizedValue(filterCutoffParam,
                                                                                      static_cast<juce::RangedAudioParameter*>(filterCutoffParam)->getValue(),
                                                                                      lfoSignal));
    settings.resonanceQ = filterResonanceParam->convertFrom0to1(applyLfoToNormalizedValue(filterResonanceParam,
                                                                                           static_cast<juce::RangedAudioParameter*>(filterResonanceParam)->getValue(),
                                                                                           lfoSignal));
    settings.modeIndex = filterTypeParam->getIndex();
    return settings;
}

OscillatorSettings PX3SynthAudioProcessor::currentOscillatorSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    OscillatorSettings settings;
    settings.modeIndex = px3::clampOscillatorModeIndex(oscModeParam->getIndex());
    settings.macroA = clamp01(oscMacroAParam->convertFrom0to1(applyLfoToNormalizedValue(oscMacroAParam,
                                                                                          static_cast<juce::RangedAudioParameter*>(oscMacroAParam)->getValue(),
                                                                                          lfoSignal)));
    settings.macroB = clamp01(oscMacroBParam->convertFrom0to1(applyLfoToNormalizedValue(oscMacroBParam,
                                                                                          static_cast<juce::RangedAudioParameter*>(oscMacroBParam)->getValue(),
                                                                                          lfoSignal)));
    settings.macroC = clamp01(oscMacroCParam->convertFrom0to1(applyLfoToNormalizedValue(oscMacroCParam,
                                                                                          static_cast<juce::RangedAudioParameter*>(oscMacroCParam)->getValue(),
                                                                                          lfoSignal)));
    settings.vowelIndex = oscVowelParam->getIndex();
    for (std::size_t i = 0; i < settings.harmonics.size(); ++i)
    {
        if (oscHarmonicParams[i] != nullptr)
        {
            settings.harmonics[i] = clamp01(oscHarmonicParams[i]->convertFrom0to1(
                applyLfoToNormalizedValue(oscHarmonicParams[i],
                                          static_cast<juce::RangedAudioParameter*>(oscHarmonicParams[i])->getValue(),
                                          lfoSignal)));
        }
    }
    return settings;
}

EnvelopeSettings PX3SynthAudioProcessor::currentEnvelopeSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    EnvelopeSettings settings;
    settings.attackSeconds = attackParam->convertFrom0to1(applyLfoToNormalizedValue(attackParam,
                                                                                     static_cast<juce::RangedAudioParameter*>(attackParam)->getValue(),
                                                                                     lfoSignal));
    settings.decaySeconds = decayParam->convertFrom0to1(applyLfoToNormalizedValue(decayParam,
                                                                                   static_cast<juce::RangedAudioParameter*>(decayParam)->getValue(),
                                                                                   lfoSignal));
    settings.sustainLevel = sustainParam->convertFrom0to1(applyLfoToNormalizedValue(sustainParam,
                                                                                     static_cast<juce::RangedAudioParameter*>(sustainParam)->getValue(),
                                                                                     lfoSignal));
    settings.releaseSeconds = releaseParam->convertFrom0to1(applyLfoToNormalizedValue(releaseParam,
                                                                                       static_cast<juce::RangedAudioParameter*>(releaseParam)->getValue(),
                                                                                       lfoSignal));
    return settings;
}

LfoSettings PX3SynthAudioProcessor::currentLfoSettings() const
{
    LfoSettings settings;
    settings.frequencyHz = juce::jlimit(0.01f, 20.0f, lfoFrequencyParam->get());
    settings.waveformIndex = lfoWaveformParam != nullptr ? lfoWaveformParam->getIndex() : 0;
    return settings;
}

VibeSettings PX3SynthAudioProcessor::currentVibeSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    VibeSettings settings;
    settings.enabled = vibeEnabledParam != nullptr && vibeEnabledParam->get();
    settings.globalAmount = vibeAmountParam->convertFrom0to1(applyLfoToNormalizedValue(vibeAmountParam,
                                                                                        static_cast<juce::RangedAudioParameter*>(vibeAmountParam)->getValue(),
                                                                                        lfoSignal));
    settings.typeIndex = vibeTypeParam != nullptr ? vibeTypeParam->getIndex() : 0;
    return settings;
}

DelaySettings PX3SynthAudioProcessor::currentDelaySettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    DelaySettings settings;
    settings.enabled = delayEnabledParam != nullptr && delayEnabledParam->get();
    settings.algorithmIndex = delayAlgorithmParam != nullptr ? delayAlgorithmParam->getIndex() : 0;
    settings.granularModeIndex = granularModeParam != nullptr ? granularModeParam->getIndex() : 0;
    settings.syncDivisionIndex = granularSyncDivisionParam != nullptr ? granularSyncDivisionParam->getIndex() : 0;
    settings.amount = delayAmountParam->convertFrom0to1(applyLfoToNormalizedValue(delayAmountParam,
                                                                                   static_cast<juce::RangedAudioParameter*>(delayAmountParam)->getValue(),
                                                                                   lfoSignal));
    settings.timeControl = delayTimeParam->convertFrom0to1(applyLfoToNormalizedValue(delayTimeParam,
                                                                                      static_cast<juce::RangedAudioParameter*>(delayTimeParam)->getValue(),
                                                                                      lfoSignal));
    settings.feedbackControl = delayFeedbackParam->convertFrom0to1(applyLfoToNormalizedValue(delayFeedbackParam,
                                                                                              static_cast<juce::RangedAudioParameter*>(delayFeedbackParam)->getValue(),
                                                                                              lfoSignal));
    settings.bpm = currentBpm;
    return settings;
}

ReverbSettings PX3SynthAudioProcessor::currentReverbSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    ReverbSettings settings;
    settings.enabled = reverbEnabledParam != nullptr && reverbEnabledParam->get();
    settings.algorithmIndex = reverbAlgorithmParam != nullptr ? reverbAlgorithmParam->getIndex() : 0;
    settings.amount = reverbAmountParam->convertFrom0to1(applyLfoToNormalizedValue(reverbAmountParam,
                                                                                    static_cast<juce::RangedAudioParameter*>(reverbAmountParam)->getValue(),
                                                                                    lfoSignal));
    settings.size = reverbSizeParam->convertFrom0to1(applyLfoToNormalizedValue(reverbSizeParam,
                                                                                static_cast<juce::RangedAudioParameter*>(reverbSizeParam)->getValue(),
                                                                                lfoSignal));
    settings.decay = reverbDecayParam->convertFrom0to1(applyLfoToNormalizedValue(reverbDecayParam,
                                                                                  static_cast<juce::RangedAudioParameter*>(reverbDecayParam)->getValue(),
                                                                                  lfoSignal));
    settings.damping = reverbDampingParam->convertFrom0to1(applyLfoToNormalizedValue(reverbDampingParam,
                                                                                      static_cast<juce::RangedAudioParameter*>(reverbDampingParam)->getValue(),
                                                                                      lfoSignal));
    settings.preDelay = reverbPreDelayParam->convertFrom0to1(applyLfoToNormalizedValue(reverbPreDelayParam,
                                                                                        static_cast<juce::RangedAudioParameter*>(reverbPreDelayParam)->getValue(),
                                                                                        lfoSignal));
    settings.modDepth = reverbModDepthParam->convertFrom0to1(applyLfoToNormalizedValue(reverbModDepthParam,
                                                                                        static_cast<juce::RangedAudioParameter*>(reverbModDepthParam)->getValue(),
                                                                                        lfoSignal));
    settings.modRate = reverbModRateParam->convertFrom0to1(applyLfoToNormalizedValue(reverbModRateParam,
                                                                                      static_cast<juce::RangedAudioParameter*>(reverbModRateParam)->getValue(),
                                                                                      lfoSignal));
    settings.width = reverbWidthParam->convertFrom0to1(applyLfoToNormalizedValue(reverbWidthParam,
                                                                                  static_cast<juce::RangedAudioParameter*>(reverbWidthParam)->getValue(),
                                                                                  lfoSignal));
    settings.cloudFeedback = reverbCloudFeedbackParam->convertFrom0to1(applyLfoToNormalizedValue(reverbCloudFeedbackParam,
                                                                                                  static_cast<juce::RangedAudioParameter*>(reverbCloudFeedbackParam)->getValue(),
                                                                                                  lfoSignal));
    settings.cloudDiffusion = reverbCloudDiffusionParam->convertFrom0to1(applyLfoToNormalizedValue(reverbCloudDiffusionParam,
                                                                                                    static_cast<juce::RangedAudioParameter*>(reverbCloudDiffusionParam)->getValue(),
                                                                                                    lfoSignal));
    return settings;
}

void PX3SynthAudioProcessor::updateTransportState()
{
    currentBpm = 120.0;
    currentTimelineSeconds = 0.0;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
            {
                currentBpm = juce::jmax(20.0, *bpm);
            }

            if (const auto time = position->getTimeInSeconds())
            {
                currentTimelineSeconds = *time;
            }
            else if (const auto ppq = position->getPpqPosition())
            {
                currentTimelineSeconds = (*ppq * 60.0) / currentBpm;
            }
        }
    }
}
