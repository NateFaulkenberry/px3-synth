#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "OscillatorMode.h"

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
