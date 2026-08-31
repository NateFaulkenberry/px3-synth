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
    // The sub generates 4 dB below full scale, leaving headroom for modulation
    // to push into. The mixer channel remains the single USER-facing gain stage
    // and its fader reads true gain; this is a fixed source trim, not a control.
    settings.level = px3::processor_internal::sourceHeadroomGain();
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

        // Comb controls take the same modulation path as cutoff and resonance,
        // so they are modulation destinations for free rather than through a
        // second mechanism.
        const auto modulated = [this](juce::AudioParameterFloat& param)
        {
            return param.convertFrom0to1(applyModulationToNormalizedValue(
                &param,
                static_cast<juce::RangedAudioParameter&>(param).getValue()));
        };

        settings.comb.tuneHz = modulated(getFilterCombTuneParam(filterIndex));
        settings.comb.decaySeconds = modulated(getFilterCombDecayParam(filterIndex));
        settings.comb.damping = modulated(getFilterCombDampingParam(filterIndex));
        settings.comb.dispersion = modulated(getFilterCombDispersionParam(filterIndex));
        settings.comb.drive = modulated(getFilterCombDriveParam(filterIndex));
        settings.comb.mix = modulated(getFilterCombMixParam(filterIndex));
        settings.comb.invertPolarity = getFilterCombInvertParam(filterIndex).get();
    }

    return filterSettings;
}

float PX3SynthAudioProcessor::getModulatedWavetablePosition(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    auto& parameter = getOscillatorWtPositionParam(idx);
    return juce::jlimit(0.0f, 1.0f, parameter.convertFrom0to1(
        applyModulationToNormalizedValue(&parameter,
                                         static_cast<juce::RangedAudioParameter&>(parameter).getValue())));
}

std::array<OscillatorLayerSettings, kOscillatorSourceCount> PX3SynthAudioProcessor::currentOscillatorLayerSettings() const
{
    std::array<OscillatorLayerSettings, kOscillatorSourceCount> layerSettings;

    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        auto& layer = layerSettings[static_cast<std::size_t>(oscIndex)];
        auto& settings = layer.oscillator;

        layer.enabled = getOscillatorEnabledParam(oscIndex).get();
        // Oscillators generate 4 dB below full scale, leaving headroom for
        // modulation. The mixer channel remains the single USER-facing gain
        // stage and its fader reads true gain.
        layer.level = px3::processor_internal::sourceHeadroomGain();
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

        // The SAME call the display makes, not the same expression written out
        // twice. Two copies of this calculation is exactly how a visualisation
        // drifts from the sound it claims to be showing; one function cannot.
        settings.wtPosition = getModulatedWavetablePosition(oscIndex);

        // Borrowed for the duration of the block. See WavetableSlot.
        settings.table = wavetableSlots[static_cast<std::size_t>(oscIndex)].current();

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

EnvelopeSettings PX3SynthAudioProcessor::currentAmpEnvelopeSettings() const
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

px3::BreakpointEnvelope PX3SynthAudioProcessor::currentAmpEnvelope() const
{
    const auto& stored = shapedEnvelopes[0];
    return stored.isPlainAdsr()
               ? px3::BreakpointEnvelope::fromAdsrWithoutHold(currentAmpEnvelopeSettings())
               : stored;
}

px3::BreakpointEnvelope PX3SynthAudioProcessor::currentModEnvelope(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    const auto& stored = shapedEnvelopes[static_cast<std::size_t>(idx + 1)];
    return stored.isPlainAdsr()
             ? px3::BreakpointEnvelope::fromAdsr(currentModEnvelopeSettings(idx))
             : stored;
}

void PX3SynthAudioProcessor::setShapedEnvelope(int index, const px3::BreakpointEnvelope& envelope)
{
    // Slot 0 is AMP ENV, which has no hold stage.
    //
    // Normalised HERE, at the one door every shape comes through - the default
    // construction of the array, a preset load, and the editor writing back.
    // BreakpointEnvelope's default constructor builds the five-point form with
    // a hold, so the amp slot started life holding a stage the amp envelope
    // does not have. currentAmpEnvelope() rebuilt it correctly for the DSP,
    // which is why the sound was right and only the picture was wrong: the
    // editor is handed the STORED shape, so it drew a hold handle until the
    // first refresh replaced it.
    //
    // Storing the right shape means the UI and the DSP cannot disagree even
    // for one frame, rather than each caller having to remember to convert.
    const auto slot = juce::jlimit(0, kShapedEnvelopeCount - 1, index);
    if (slot == 0)
    {
        shapedEnvelopes[static_cast<std::size_t>(slot)] = px3::withoutHoldStage(envelope);
        return;
    }

    shapedEnvelopes[static_cast<std::size_t>(juce::jlimit(0, kShapedEnvelopeCount - 1, index))]
        = envelope;
}

px3::BreakpointEnvelope PX3SynthAudioProcessor::getShapedEnvelope(int index) const
{
    const auto slot = juce::jlimit(0, kShapedEnvelopeCount - 1, index);
    const auto& stored = shapedEnvelopes[static_cast<std::size_t>(slot)];

    // The array is default-constructed, and the default is the five-point form
    // with a hold - so the amp slot answers with one until something writes to
    // it. Converted on the way out as well as on the way in, so there is no
    // window in which the wrong shape is visible.
    if (slot == 0)
    {
        return px3::withoutHoldStage(stored);
    }

    return stored;
}

EnvelopeSettings PX3SynthAudioProcessor::currentModEnvelopeSettings(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    auto& attack = getEnvelopeAttackParam(idx);
    auto& hold = getEnvelopeHoldParam(idx);
    auto& decay = getEnvelopeDecayParam(idx);
    auto& sustain = getEnvelopeSustainParam(idx);
    auto& release = getEnvelopeReleaseParam(idx);
    auto& enabledParam = getEnvelopeEnabledParam(idx);

    EnvelopeSettings settings;
    const auto envEnabled = enabledParam.get();

    if (!envEnabled)
    {
        settings.attackSeconds = 0.001f;
        settings.holdSeconds = 0.0f;
        settings.decaySeconds = 0.005f;
        settings.sustainLevel = 1.0f;
        settings.releaseSeconds = 0.010f;
        return settings;
    }

    settings.attackSeconds = attack.convertFrom0to1(applyModulationToNormalizedValue(&attack,
                                                                                      static_cast<juce::RangedAudioParameter&>(attack).getValue()));
    settings.holdSeconds = hold.convertFrom0to1(applyModulationToNormalizedValue(&hold,
                                                                                  static_cast<juce::RangedAudioParameter&>(hold).getValue()));
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

DoomSettings PX3SynthAudioProcessor::currentDoomSettings() const
{
    // Every continuous control goes through applyModulationToNormalizedValue,
    // which is what makes it a modulation destination - there is no DOOM-side
    // modulation plumbing.
    auto modulated = [this](juce::AudioParameterFloat* param)
    {
        return param->convertFrom0to1(
            applyModulationToNormalizedValue(param,
                                             static_cast<juce::RangedAudioParameter*>(param)->getValue()));
    };

    DoomSettings settings;
    settings.enabled = doomEnabledParam != nullptr && doomEnabledParam->get();
    settings.freeze = doomFreezeParam != nullptr && doomFreezeParam->get();
    settings.loopActive = doomLoopActiveParam != nullptr && doomLoopActiveParam->get();
    settings.wetActive = doomWetActiveParam != nullptr && doomWetActiveParam->get();
    settings.loopHalf = doomLoopHalfParam != nullptr && doomLoopHalfParam->get();
    settings.clockSmooth = doomClockSmoothParam != nullptr && doomClockSmoothParam->get();

    settings.mix = modulated(doomMixParam);
    settings.clock = modulated(doomClockParam);
    settings.loopLength = modulated(doomLoopLengthParam);
    settings.loopModify = modulated(doomLoopModifyParam);
    settings.overdub = modulated(doomOverdubParam);
    settings.fade = modulated(doomFadeParam);
    settings.wetTime = modulated(doomWetTimeParam);
    settings.wetModify = modulated(doomWetModifyParam);
    settings.cross = modulated(doomCrossParam);
    settings.glue = modulated(doomGlueParam);
    settings.eq = modulated(doomEqParam);
    settings.balance = modulated(doomBalanceParam);
    settings.blend = modulated(doomBlendParam);
    settings.spread = modulated(doomSpreadParam);

    settings.routingIndex = doomRoutingParam != nullptr ? doomRoutingParam->getIndex() : 0;
    settings.loopModeIndex = doomLoopModeParam != nullptr ? doomLoopModeParam->getIndex() : 1;
    settings.wetModeIndex = doomWetModeParam != nullptr ? doomWetModeParam->getIndex() : 0;
    settings.crossSourceIndex = doomCrossSourceParam != nullptr ? doomCrossSourceParam->getIndex() : 0;

    return settings;
}

LucySettings PX3SynthAudioProcessor::currentLucySettings() const
{
    auto modulated = [this](juce::AudioParameterFloat* param)
    {
        return param->convertFrom0to1(
            applyModulationToNormalizedValue(param,
                                             static_cast<juce::RangedAudioParameter*>(param)->getValue()));
    };

    LucySettings settings;
    settings.enabled = lucyEnabledParam != nullptr && lucyEnabledParam->get();
    settings.filterInvert = lucyFilterInvertParam != nullptr && lucyFilterInvertParam->get();
    settings.verbPost = lucyVerbPostParam != nullptr && lucyVerbPostParam->get();
    settings.freeze = lucyFreezeParam != nullptr && lucyFreezeParam->get();
    settings.freezeSlushy = lucyFreezeSlushyParam != nullptr && lucyFreezeSlushyParam->get();
    settings.gate = lucyGateParam != nullptr && lucyGateParam->get();
    settings.slow = lucySlowParam != nullptr && lucySlowParam->get();

    settings.global = modulated(lucyGlobalParam);
    settings.loss = modulated(lucyLossParam);
    settings.speed = modulated(lucySpeedParam);
    settings.filterWidth = modulated(lucyFilterParam);
    settings.filterFreq = modulated(lucyFilterFreqParam);
    settings.verb = modulated(lucyVerbParam);
    settings.verbDecay = modulated(lucyVerbDecayParam);
    settings.freezer = modulated(lucyFreezerParam);
    settings.gateCutoff = modulated(lucyGateCutoffParam);
    settings.threshold = modulated(lucyThresholdParam);
    settings.autoGain = modulated(lucyAutoGainParam);
    settings.weighting = modulated(lucyWeightingParam);
    settings.gainDb = modulated(lucyGainParam);
    settings.spread = modulated(lucySpreadParam);

    settings.modeIndex = lucyModeParam != nullptr ? lucyModeParam->getIndex() : 0;
    settings.packetIndex = lucyPacketsParam != nullptr ? lucyPacketsParam->getIndex() : 0;
    settings.slopeIndex = lucySlopeParam != nullptr ? lucySlopeParam->getIndex() : 1;

    return settings;
}

ChorusSettings PX3SynthAudioProcessor::currentChorusSettings() const
{
    auto modulated = [this](juce::AudioParameterFloat* param)
    {
        return param->convertFrom0to1(
            applyModulationToNormalizedValue(param,
                                             static_cast<juce::RangedAudioParameter*>(param)->getValue()));
    };

    ChorusSettings settings;
    settings.enabled = chorusEnabledParam != nullptr && chorusEnabledParam->get();
    settings.amount = modulated(chorusAmountParam);
    settings.rate = modulated(chorusRateParam);
    settings.depth = modulated(chorusDepthParam);
    settings.width = modulated(chorusWidthParam);
    settings.spread = modulated(chorusSpreadParam);
    settings.lowCut = modulated(chorusLowCutParam);
    settings.feedback = modulated(chorusFeedbackParam);
    settings.character = modulated(chorusCharacterParam);
    settings.mix = modulated(chorusMixParam);
    settings.tone = modulated(chorusToneParam);
    settings.modeIndex = chorusModeParam != nullptr ? chorusModeParam->getIndex() : 1;
    return settings;
}

StereoSpreadSettings PX3SynthAudioProcessor::currentStereoSpreadSettings() const
{
    auto modulated = [this](juce::AudioParameterFloat* param)
    {
        return param->convertFrom0to1(
            applyModulationToNormalizedValue(param,
                                             static_cast<juce::RangedAudioParameter*>(param)->getValue()));
    };

    StereoSpreadSettings settings;
    settings.enabled = spreadEnabledParam != nullptr && spreadEnabledParam->get();
    settings.amount = modulated(spreadAmountParam);
    settings.width = modulated(spreadWidthParam);
    settings.depth = modulated(spreadDepthParam);
    settings.center = modulated(spreadCenterParam);
    settings.lowWidth = modulated(spreadLowWidthParam);
    settings.highWidth = modulated(spreadHighWidthParam);
    settings.lowFreq = modulated(spreadLowFreqParam);
    settings.highFreq = modulated(spreadHighFreqParam);
    settings.mix = modulated(spreadMixParam);
    settings.tone = modulated(spreadToneParam);
    settings.modeIndex = spreadModeParam != nullptr ? spreadModeParam->getIndex() : 0;
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
