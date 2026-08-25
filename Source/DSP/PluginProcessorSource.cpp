#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "OscillatorMode.h"
#include "SubOscMode.h"

using namespace px3::processor_internal;

SubtractiveSettings PX3SynthAudioProcessor::currentSubtractiveSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    SubtractiveSettings settings;
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

std::array<FilterSettings, kFilterInstanceCount> PX3SynthAudioProcessor::currentFilterSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    std::array<FilterSettings, kFilterInstanceCount> filterSettings;

    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        auto& settings = filterSettings[static_cast<std::size_t>(filterIndex)];
        auto& enabledParam = getFilterEnabledParam(filterIndex);
        auto& cutoffParam = getFilterCutoffParam(filterIndex);
        auto& resonanceParam = getFilterResonanceParam(filterIndex);
        auto& modeParam = getFilterTypeParam(filterIndex);

        settings.enabled = enabledParam.get();
        settings.cutoffHz = cutoffParam.convertFrom0to1(applyLfoToNormalizedValue(
            &cutoffParam,
            static_cast<juce::RangedAudioParameter&>(cutoffParam).getValue(),
            lfoSignal));
        settings.resonanceQ = resonanceParam.convertFrom0to1(applyLfoToNormalizedValue(
            &resonanceParam,
            static_cast<juce::RangedAudioParameter&>(resonanceParam).getValue(),
            lfoSignal));
        settings.modeIndex = modeParam.getIndex();
    }

    return filterSettings;
}

std::array<OscillatorLayerSettings, kOscillatorSourceCount> PX3SynthAudioProcessor::currentOscillatorLayerSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    std::array<OscillatorLayerSettings, kOscillatorSourceCount> layerSettings;

    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        auto& layer = layerSettings[static_cast<std::size_t>(oscIndex)];
        auto& settings = layer.oscillator;

        layer.enabled = getOscillatorEnabledParam(oscIndex).get();
        layer.level = getOscillatorLevelParam(oscIndex).convertFrom0to1(
            applyLfoToNormalizedValue(&getOscillatorLevelParam(oscIndex),
                                      static_cast<juce::RangedAudioParameter&>(getOscillatorLevelParam(oscIndex)).getValue(),
                                      lfoSignal));
        layer.coarseSemitones = getOscillatorCoarseParam(oscIndex).convertFrom0to1(
            applyLfoToNormalizedValue(&getOscillatorCoarseParam(oscIndex),
                                      static_cast<juce::RangedAudioParameter&>(getOscillatorCoarseParam(oscIndex)).getValue(),
                                      lfoSignal));
        layer.fineCents = getOscillatorFineParam(oscIndex).convertFrom0to1(
            applyLfoToNormalizedValue(&getOscillatorFineParam(oscIndex),
                                      static_cast<juce::RangedAudioParameter&>(getOscillatorFineParam(oscIndex)).getValue(),
                                      lfoSignal));

        settings.modeIndex = px3::clampOscillatorModeIndex(getOscillatorModeParam(oscIndex).getIndex());
        settings.macroA = clamp01(getOscillatorMacroAParam(oscIndex).convertFrom0to1(
            applyLfoToNormalizedValue(&getOscillatorMacroAParam(oscIndex),
                                      static_cast<juce::RangedAudioParameter&>(getOscillatorMacroAParam(oscIndex)).getValue(),
                                      lfoSignal)));
        settings.macroB = clamp01(getOscillatorMacroBParam(oscIndex).convertFrom0to1(
            applyLfoToNormalizedValue(&getOscillatorMacroBParam(oscIndex),
                                      static_cast<juce::RangedAudioParameter&>(getOscillatorMacroBParam(oscIndex)).getValue(),
                                      lfoSignal)));
        settings.macroC = clamp01(getOscillatorMacroCParam(oscIndex).convertFrom0to1(
            applyLfoToNormalizedValue(&getOscillatorMacroCParam(oscIndex),
                                      static_cast<juce::RangedAudioParameter&>(getOscillatorMacroCParam(oscIndex)).getValue(),
                                      lfoSignal)));
        settings.vowelIndex = getOscillatorVowelParam(oscIndex).getIndex();

        for (std::size_t harmonicIndex = 0; harmonicIndex < settings.harmonics.size(); ++harmonicIndex)
        {
            auto& harmonicParam = getOscillatorHarmonicParam(oscIndex, static_cast<int>(harmonicIndex));
            settings.harmonics[harmonicIndex] = clamp01(harmonicParam.convertFrom0to1(
                applyLfoToNormalizedValue(&harmonicParam,
                                          static_cast<juce::RangedAudioParameter&>(harmonicParam).getValue(),
                                          lfoSignal)));
        }
    }

    return layerSettings;
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
