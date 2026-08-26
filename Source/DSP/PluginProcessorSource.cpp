#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "OscillatorMode.h"
#include "SubOscMode.h"

using namespace px3::processor_internal;

SubtractiveSettings PX3SynthAudioProcessor::currentSubtractiveSettings() const
{
    SubtractiveSettings settings;
    settings.masterGain = masterGainParam->convertFrom0to1(applyModulationToNormalizedValue(masterGainParam,
                                                                                             static_cast<juce::RangedAudioParameter*>(masterGainParam)->getValue()));
    return settings;
}

SubOscSettings PX3SynthAudioProcessor::currentSubOscillatorSettings() const
{
    SubOscSettings settings;
    settings.enabled = subOscEnabledParam != nullptr && subOscEnabledParam->get();
    // Source generation level is fixed at unity; mixer-stage source level is
    // now authoritative for channel balancing and pre-fader send behavior.
    settings.level = 1.0f;
    settings.pitchSemitones = subOscPitchParam->convertFrom0to1(applyModulationToNormalizedValue(subOscPitchParam,
                                                                                                   static_cast<juce::RangedAudioParameter*>(subOscPitchParam)->getValue()));
    settings.octaveIndex = px3::clampSubOscOctaveIndex(subOscOctaveParam != nullptr ? subOscOctaveParam->getIndex() : 1);
    settings.waveformIndex = px3::clampSubOscWaveformIndex(subOscWaveformParam != nullptr ? subOscWaveformParam->getIndex() : 1);
    return settings;
}

std::array<FilterSettings, kFilterInstanceCount> PX3SynthAudioProcessor::currentFilterSettings() const
{
    std::array<FilterSettings, kFilterInstanceCount> filterSettings;

    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        auto& settings = filterSettings[static_cast<std::size_t>(filterIndex)];
        auto& enabledParam = getFilterEnabledParam(filterIndex);
        auto& cutoffParam = getFilterCutoffParam(filterIndex);
        auto& resonanceParam = getFilterResonanceParam(filterIndex);
        auto& modeParam = getFilterTypeParam(filterIndex);

        settings.enabled = enabledParam.get();
        settings.cutoffHz = cutoffParam.convertFrom0to1(applyModulationToNormalizedValue(
            &cutoffParam,
            static_cast<juce::RangedAudioParameter&>(cutoffParam).getValue()));
        settings.resonanceQ = resonanceParam.convertFrom0to1(applyModulationToNormalizedValue(
            &resonanceParam,
            static_cast<juce::RangedAudioParameter&>(resonanceParam).getValue()));
        settings.modeIndex = modeParam.getIndex();
    }

    return filterSettings;
}

std::array<OscillatorLayerSettings, kOscillatorSourceCount> PX3SynthAudioProcessor::currentOscillatorLayerSettings() const
{
    std::array<OscillatorLayerSettings, kOscillatorSourceCount> layerSettings;

    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        auto& layer = layerSettings[static_cast<std::size_t>(oscIndex)];
        auto& settings = layer.oscillator;

        layer.enabled = getOscillatorEnabledParam(oscIndex).get();
        // Source generation level is fixed at unity; mixer-stage source level
        // is the single gain stage for user-facing channel levels.
        layer.level = 1.0f;
        layer.pitchSemitones = getOscillatorPitchParam(oscIndex).convertFrom0to1(
            applyModulationToNormalizedValue(&getOscillatorPitchParam(oscIndex),
                                             static_cast<juce::RangedAudioParameter&>(getOscillatorPitchParam(oscIndex)).getValue()));
        layer.coarseSemitones = getOscillatorCoarseParam(oscIndex).convertFrom0to1(
            applyModulationToNormalizedValue(&getOscillatorCoarseParam(oscIndex),
                                             static_cast<juce::RangedAudioParameter&>(getOscillatorCoarseParam(oscIndex)).getValue()));
        layer.fineCents = getOscillatorFineParam(oscIndex).convertFrom0to1(
            applyModulationToNormalizedValue(&getOscillatorFineParam(oscIndex),
                                             static_cast<juce::RangedAudioParameter&>(getOscillatorFineParam(oscIndex)).getValue()));

        settings.modeIndex = px3::clampOscillatorModeIndex(getOscillatorModeParam(oscIndex).getIndex());
        settings.macroA = clamp01(getOscillatorMacroAParam(oscIndex).convertFrom0to1(
            applyModulationToNormalizedValue(&getOscillatorMacroAParam(oscIndex),
                                             static_cast<juce::RangedAudioParameter&>(getOscillatorMacroAParam(oscIndex)).getValue())));
        settings.macroB = clamp01(getOscillatorMacroBParam(oscIndex).convertFrom0to1(
            applyModulationToNormalizedValue(&getOscillatorMacroBParam(oscIndex),
                                             static_cast<juce::RangedAudioParameter&>(getOscillatorMacroBParam(oscIndex)).getValue())));
        settings.macroC = clamp01(getOscillatorMacroCParam(oscIndex).convertFrom0to1(
            applyModulationToNormalizedValue(&getOscillatorMacroCParam(oscIndex),
                                             static_cast<juce::RangedAudioParameter&>(getOscillatorMacroCParam(oscIndex)).getValue())));
        settings.vowelIndex = getOscillatorVowelParam(oscIndex).getIndex();

        for (std::size_t harmonicIndex = 0; harmonicIndex < settings.harmonics.size(); ++harmonicIndex)
        {
            auto& harmonicParam = getOscillatorHarmonicParam(oscIndex, static_cast<int>(harmonicIndex));
            settings.harmonics[harmonicIndex] = clamp01(harmonicParam.convertFrom0to1(
                applyModulationToNormalizedValue(&harmonicParam,
                                                 static_cast<juce::RangedAudioParameter&>(harmonicParam).getValue())));
        }
    }

    return layerSettings;
}

EnvelopeSettings PX3SynthAudioProcessor::currentEnvelopeSettings() const
{
    EnvelopeSettings settings;

    // AMP ENV is a dedicated VCA contour and must remain independent of the
    // assignable modulation matrix destination path.
    settings.attackSeconds = attackParam->get();
    settings.decaySeconds = decayParam->get();
    settings.sustainLevel = sustainParam->get();
    settings.releaseSeconds = releaseParam->get();
    return settings;
}

EnvelopeSettings PX3SynthAudioProcessor::currentEnvelopeSettings(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    auto& attack = getEnvelopeAttackParam(idx);
    auto& decay = getEnvelopeDecayParam(idx);
    auto& sustain = getEnvelopeSustainParam(idx);
    auto& release = getEnvelopeReleaseParam(idx);
    auto& enabledParam = getEnvelopeEnabledParam(idx);

    EnvelopeSettings settings;
    const auto envEnabled = enabledParam.get();

    if (!envEnabled)
    {
        settings.attackSeconds = 0.001f;
        settings.decaySeconds = 0.005f;
        settings.sustainLevel = 1.0f;
        settings.releaseSeconds = 0.010f;
        return settings;
    }

    settings.attackSeconds = attack.convertFrom0to1(applyModulationToNormalizedValue(&attack,
                                                                                      static_cast<juce::RangedAudioParameter&>(attack).getValue()));
    settings.decaySeconds = decay.convertFrom0to1(applyModulationToNormalizedValue(&decay,
                                                                                    static_cast<juce::RangedAudioParameter&>(decay).getValue()));
    settings.sustainLevel = sustain.convertFrom0to1(applyModulationToNormalizedValue(&sustain,
                                                                                      static_cast<juce::RangedAudioParameter&>(sustain).getValue()));
    settings.releaseSeconds = release.convertFrom0to1(applyModulationToNormalizedValue(&release,
                                                                                        static_cast<juce::RangedAudioParameter&>(release).getValue()));
    return settings;
}

LfoSettings PX3SynthAudioProcessor::currentLfoSettings() const
{
    return currentLfoSettings(0);
}

LfoSettings PX3SynthAudioProcessor::currentLfoSettings(int lfoIndex) const
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    LfoSettings settings;
    settings.enabled = getLfoEnabledParam(idx).get();
    settings.frequencyHz = juce::jlimit(0.01f, 20.0f, getLfoFrequencyParam(idx).get());
    settings.waveformIndex = getLfoWaveformParam(idx).getIndex();
    return settings;
}

VibeSettings PX3SynthAudioProcessor::currentVibeSettings() const
{
    VibeSettings settings;
    settings.enabled = vibeEnabledParam != nullptr && vibeEnabledParam->get();
    settings.globalAmount = vibeAmountParam->convertFrom0to1(applyModulationToNormalizedValue(vibeAmountParam,
                                                                                               static_cast<juce::RangedAudioParameter*>(vibeAmountParam)->getValue()));
    settings.typeIndex = vibeTypeParam != nullptr ? vibeTypeParam->getIndex() : 0;
    return settings;
}

DelaySettings PX3SynthAudioProcessor::currentDelaySettings() const
{
    DelaySettings settings;
    settings.enabled = delayEnabledParam != nullptr && delayEnabledParam->get();
    settings.algorithmIndex = delayAlgorithmParam != nullptr ? delayAlgorithmParam->getIndex() : 0;
    settings.granularModeIndex = granularModeParam != nullptr ? granularModeParam->getIndex() : 0;
    settings.syncDivisionIndex = granularSyncDivisionParam != nullptr ? granularSyncDivisionParam->getIndex() : 0;
    settings.amount = delayAmountParam->convertFrom0to1(applyModulationToNormalizedValue(delayAmountParam,
                                                                                          static_cast<juce::RangedAudioParameter*>(delayAmountParam)->getValue()));
    settings.timeControl = delayTimeParam->convertFrom0to1(applyModulationToNormalizedValue(delayTimeParam,
                                                                                             static_cast<juce::RangedAudioParameter*>(delayTimeParam)->getValue()));
    settings.feedbackControl = delayFeedbackParam->convertFrom0to1(applyModulationToNormalizedValue(delayFeedbackParam,
                                                                                                     static_cast<juce::RangedAudioParameter*>(delayFeedbackParam)->getValue()));
    settings.bpm = currentBpm;
    return settings;
}

ReverbSettings PX3SynthAudioProcessor::currentReverbSettings() const
{
    ReverbSettings settings;
    settings.enabled = reverbEnabledParam != nullptr && reverbEnabledParam->get();
    settings.algorithmIndex = reverbAlgorithmParam != nullptr ? reverbAlgorithmParam->getIndex() : 0;
    settings.amount = reverbAmountParam->convertFrom0to1(applyModulationToNormalizedValue(reverbAmountParam,
                                                                                           static_cast<juce::RangedAudioParameter*>(reverbAmountParam)->getValue()));
    settings.size = reverbSizeParam->convertFrom0to1(applyModulationToNormalizedValue(reverbSizeParam,
                                                                                       static_cast<juce::RangedAudioParameter*>(reverbSizeParam)->getValue()));
    settings.decay = reverbDecayParam->convertFrom0to1(applyModulationToNormalizedValue(reverbDecayParam,
                                                                                         static_cast<juce::RangedAudioParameter*>(reverbDecayParam)->getValue()));
    settings.damping = reverbDampingParam->convertFrom0to1(applyModulationToNormalizedValue(reverbDampingParam,
                                                                                             static_cast<juce::RangedAudioParameter*>(reverbDampingParam)->getValue()));
    settings.preDelay = reverbPreDelayParam->convertFrom0to1(applyModulationToNormalizedValue(reverbPreDelayParam,
                                                                                               static_cast<juce::RangedAudioParameter*>(reverbPreDelayParam)->getValue()));
    settings.modDepth = reverbModDepthParam->convertFrom0to1(applyModulationToNormalizedValue(reverbModDepthParam,
                                                                                               static_cast<juce::RangedAudioParameter*>(reverbModDepthParam)->getValue()));
    settings.modRate = reverbModRateParam->convertFrom0to1(applyModulationToNormalizedValue(reverbModRateParam,
                                                                                             static_cast<juce::RangedAudioParameter*>(reverbModRateParam)->getValue()));
    settings.width = reverbWidthParam->convertFrom0to1(applyModulationToNormalizedValue(reverbWidthParam,
                                                                                         static_cast<juce::RangedAudioParameter*>(reverbWidthParam)->getValue()));
    settings.cloudFeedback = reverbCloudFeedbackParam->convertFrom0to1(applyModulationToNormalizedValue(reverbCloudFeedbackParam,
                                                                                                         static_cast<juce::RangedAudioParameter*>(reverbCloudFeedbackParam)->getValue()));
    settings.cloudDiffusion = reverbCloudDiffusionParam->convertFrom0to1(applyModulationToNormalizedValue(reverbCloudDiffusionParam,
                                                                                                           static_cast<juce::RangedAudioParameter*>(reverbCloudDiffusionParam)->getValue()));
    return settings;
}

MoodSettings PX3SynthAudioProcessor::currentMoodSettings() const
{
    MoodSettings settings;
    settings.enabled = moodEnabledParam != nullptr && moodEnabledParam->get();
    settings.trueBypass = false;
    settings.freeze = moodFreezeParam != nullptr && moodFreezeParam->get();

    settings.mix = moodMixParam->convertFrom0to1(applyModulationToNormalizedValue(moodMixParam,
                                                                                   static_cast<juce::RangedAudioParameter*>(moodMixParam)->getValue()));
    settings.clock = moodClockParam->convertFrom0to1(applyModulationToNormalizedValue(moodClockParam,
                                                                                       static_cast<juce::RangedAudioParameter*>(moodClockParam)->getValue()));
    settings.wetTime = moodWetTimeParam->convertFrom0to1(applyModulationToNormalizedValue(moodWetTimeParam,
                                                                                           static_cast<juce::RangedAudioParameter*>(moodWetTimeParam)->getValue()));
    settings.wetModify = moodWetModifyParam->convertFrom0to1(applyModulationToNormalizedValue(moodWetModifyParam,
                                                                                               static_cast<juce::RangedAudioParameter*>(moodWetModifyParam)->getValue()));
    settings.loopLength = moodLoopLengthParam->convertFrom0to1(applyModulationToNormalizedValue(moodLoopLengthParam,
                                                                                                 static_cast<juce::RangedAudioParameter*>(moodLoopLengthParam)->getValue()));
    settings.loopModify = moodLoopModifyParam->convertFrom0to1(applyModulationToNormalizedValue(moodLoopModifyParam,
                                                                                                 static_cast<juce::RangedAudioParameter*>(moodLoopModifyParam)->getValue()));
    settings.feedback = moodFeedbackParam->convertFrom0to1(applyModulationToNormalizedValue(moodFeedbackParam,
                                                                                             static_cast<juce::RangedAudioParameter*>(moodFeedbackParam)->getValue()));
    settings.spread = moodSpreadParam->convertFrom0to1(applyModulationToNormalizedValue(moodSpreadParam,
                                                                                         static_cast<juce::RangedAudioParameter*>(moodSpreadParam)->getValue()));
    settings.degrade = moodDegradeParam->convertFrom0to1(applyModulationToNormalizedValue(moodDegradeParam,
                                                                                           static_cast<juce::RangedAudioParameter*>(moodDegradeParam)->getValue()));

    settings.routing = moodRoutingParam != nullptr
                           ? juce::jlimit(0.0f, 1.0f, static_cast<float>(moodRoutingParam->getIndex()) / 2.0f)
                           : 0.0f;
    settings.wetModeIndex = moodWetModeParam != nullptr ? moodWetModeParam->getIndex() : 0;
    settings.loopModeIndex = moodLoopModeParam != nullptr ? moodLoopModeParam->getIndex() : 0;
    settings.bpm = currentBpm;
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
