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
    // Read through the control-source accumulator, so a MACRO assigned to one
    // of these reaches the sound. Reading the parameters raw is why it did not:
    // a macro is applied when a parameter is read, and this was the one place
    // that skipped the read.
    //
    // AMP ENV stays independent of the assignable modulation matrix, as it must
    // - and it does so without a special case here, because ampAttack/Decay/
    // Sustain/Release are excluded from lfoAssignableTargets. An LFO or a mod
    // envelope cannot name them, so the accumulator's only contribution to
    // these four is the macro one.
    const auto through = [this](juce::AudioParameterFloat* parameter)
    {
        return parameter->convertFrom0to1(applyModulationToNormalizedValue(
            parameter, static_cast<juce::RangedAudioParameter*>(parameter)->getValue()));
    };

    EnvelopeSettings settings;
    settings.attackSeconds = through(attackParam);
    settings.decaySeconds = through(decayParam);
    settings.sustainLevel = through(sustainParam);
    settings.releaseSeconds = through(releaseParam);
    return settings;
}

px3::BreakpointEnvelope::Mode PX3SynthAudioProcessor::getEnvelopeMode(int slot) const
{
    return shapedEnvelopes[static_cast<std::size_t>(
               juce::jlimit(0, kShapedEnvelopeCount - 1, slot))].getMode();
}

void PX3SynthAudioProcessor::setEnvelopeMode(int slot, px3::BreakpointEnvelope::Mode mode)
{
    const auto index = static_cast<std::size_t>(juce::jlimit(0, kShapedEnvelopeCount - 1, slot));
    auto& active = shapedEnvelopes[index];

    if (mode == px3::BreakpointEnvelope::Mode::breakpoint
        && ! envelopeSupportsBreakpointMode(slot))
    {
        return;
    }

    if (active.getMode() == mode) { return; }

    if (mode == px3::BreakpointEnvelope::Mode::breakpoint)
    {
        // Seed from the ADSR the FIRST time only, so the user starts somewhere
        // familiar. Every switch after that restores their own shape - deriving
        // it again would overwrite their work each time they glanced at the
        // other mode.
        if (! breakpointInitialised[index])
        {
            // From the ADSR the user can SEE, which means the parameters
            // applied. In ADSR mode the stored shape carries only the curves -
            // the four values live in the parameters - so seeding from it raw
            // handed the editor a default-timed skeleton and threw away the
            // envelope the user had set up.
            breakpointShapes[index]
                = active.withAdsrApplied(slot == 0 ? currentAmpEnvelopeSettings()
                                                   : envelopeParameterSettings(slot - 1));
            breakpointInitialised[index] = true;
        }

        // The ADSR shape stays where it is; only which one is active changes.
        adsrShapes[index] = active;
        active = breakpointShapes[index];
        active.setMode(px3::BreakpointEnvelope::Mode::breakpoint);

        // Never hand the editor a shape with nothing in it to drag, or one
        // collapsed to no duration. Both are put right here.
        active.normaliseForBreakpointEditing();
        return;
    }

    // Back to ADSR. The breakpoint shape is kept as it stands, and the stored
    // ADSR parameters come back unchanged - no attempt is made to derive four
    // values from an arbitrary shape, which would be lossy and would rewrite
    // settings the user never edited.
    breakpointShapes[index] = active;
    breakpointShapes[index].setMode(px3::BreakpointEnvelope::Mode::breakpoint);
    breakpointInitialised[index] = true;

    active = adsrShapes[index];
    active.setMode(px3::BreakpointEnvelope::Mode::adsr);
}

px3::BreakpointEnvelope PX3SynthAudioProcessor::currentAmpEnvelope() const
{
    // A skeleton takes its times and level from the parameters and keeps its
    // own curves, which is how a macro reaches an envelope somebody has bent.
    // Testing isPlainAdsr instead meant a single curve handle being dragged
    // froze the envelope against everything the parameters did afterwards -
    // automation and macros alike.
    // In ADSR mode the parameters own the times and the level and the shape
    // owns the curves. In Breakpoint mode the shape owns everything and the
    // parameters are neither read nor written - which is the whole point of
    // having a mode, and what stops the knobs going stale.
    const auto& stored = shapedEnvelopes[0];
    return stored.isBreakpointMode()
               ? stored
               : stored.withAdsrApplied(currentAmpEnvelopeSettings());
}

px3::BreakpointEnvelope PX3SynthAudioProcessor::currentModEnvelope(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    const auto& stored = shapedEnvelopes[static_cast<std::size_t>(idx + 1)];
    return stored.isBreakpointMode()
             ? stored
             : stored.withAdsrApplied(currentModEnvelopeSettings(idx));
}

void PX3SynthAudioProcessor::publishEnvelopeProgress()
{
    // Audio thread. Picks the newest sounding voice and stores where each of
    // its envelopes has got to; stores "idle" when nothing is sounding, so the
    // graphs clear rather than keeping the last note's fill.
    const SynthVoice* newest = nullptr;
    for (auto* voice : typedVoices)
    {
        if (voice == nullptr || ! voice->isVoiceActive()) { continue; }
        if (newest == nullptr || voice->noteStartSequence() > newest->noteStartSequence())
        {
            newest = voice;
        }
    }

    const auto store = [](ProgressSlot& target, const EnvelopePosition& position)
    {
        target.inRelease.store(position.inRelease, std::memory_order_relaxed);
        target.held.store(position.heldSeconds, std::memory_order_relaxed);
        target.released.store(position.releasedSeconds, std::memory_order_relaxed);
        target.sustain.store(position.sustainSeconds, std::memory_order_relaxed);
        target.active.store(position.active, std::memory_order_release);
    };

    if (newest == nullptr)
    {
        for (auto& slot : envelopeProgress) { slot.active.store(false, std::memory_order_relaxed); }
        return;
    }

    store(envelopeProgress[0], newest->currentAmpEnvelopePosition());

    for (int env = 0; env < kEnvelopeSlots - 1; ++env)
    {
        store(envelopeProgress[static_cast<std::size_t>(env + 1)],
              newest->currentModEnvelopePosition(env));
    }
}

EnvelopePosition PX3SynthAudioProcessor::getEnvelopeProgress(int slot) const
{
    const auto& source = envelopeProgress[
        static_cast<std::size_t>(juce::jlimit(0, kEnvelopeSlots - 1, slot))];

    EnvelopePosition position;
    position.active = source.active.load(std::memory_order_acquire);
    position.inRelease = source.inRelease.load(std::memory_order_relaxed);
    position.heldSeconds = source.held.load(std::memory_order_relaxed);
    position.releasedSeconds = source.released.load(std::memory_order_relaxed);
    position.sustainSeconds = source.sustain.load(std::memory_order_relaxed);
    return position;
}

void PX3SynthAudioProcessor::setShapedEnvelope(int index, const px3::BreakpointEnvelope& envelope)
{
    const auto slot = juce::jlimit(0, kShapedEnvelopeCount - 1, index);
    auto stored = envelope;

    // The choke point for the stored shape, which is what makes AMP ENV's
    // restriction hold everywhere. setEnvelopeMode is the UI's door; state
    // restore does not use it - it builds a shape, sets the mode on it and
    // stores it here - so a session or preset carrying a breakpoint AMP ENV
    // would otherwise walk straight past the rule.
    if (! envelopeSupportsBreakpointMode(slot))
    {
        // A shape that ARRIVES claiming Breakpoint mode is reduced, not just
        // relabelled: a six-point drawing wearing an ADSR label is a worse
        // state than the one it came from, because isAdsrSkeleton is then false
        // and the four knobs stop reaching it - the stale-knob defect the mode
        // work removed.
        //
        // Only that case. A shape already calling itself an ADSR is stored
        // exactly as given, however many points it has, so nothing this slot
        // already plays is changed by the restriction. That matters: AMP ENV
        // becoming ADSR-only is a UI rule, and it must not quietly rewrite
        // envelopes that were never in Breakpoint mode to begin with.
        if (stored.isBreakpointMode() && ! stored.hasAdsrSkeletonShape())
        {
            stored = stored.reducedToAdsr();
        }

        stored.setMode(px3::BreakpointEnvelope::Mode::adsr);
    }

    shapedEnvelopes[static_cast<std::size_t>(slot)] = stored;
}

px3::BreakpointEnvelope PX3SynthAudioProcessor::getShapedEnvelope(int index) const
{
    return shapedEnvelopes[static_cast<std::size_t>(juce::jlimit(0, kShapedEnvelopeCount - 1, index))];
}

EnvelopeSettings PX3SynthAudioProcessor::envelopeParameterSettings(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    auto& attack = getEnvelopeAttackParam(idx);
    auto& decay = getEnvelopeDecayParam(idx);
    auto& sustain = getEnvelopeSustainParam(idx);
    auto& release = getEnvelopeReleaseParam(idx);

    EnvelopeSettings settings;
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

EnvelopeSettings PX3SynthAudioProcessor::currentModEnvelopeSettings(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);

    // What the VOICE should run. A bypassed modulation envelope has to sit at
    // full level and get out of the way, which is a contour of its own rather
    // than the one the parameters describe.
    //
    // Not what the GRAPH should draw: bypass is a mute, not an edit, and
    // drawing this collapsed the curve the moment a card was switched off.
    // Anything showing the user their envelope wants envelopeParameterSettings.
    if (! getEnvelopeEnabledParam(idx).get())
    {
        EnvelopeSettings bypassed;
        bypassed.attackSeconds = 0.001f;
        bypassed.decaySeconds = 0.005f;
        bypassed.sustainLevel = 1.0f;
        bypassed.releaseSeconds = 0.010f;
        return bypassed;
    }

    return envelopeParameterSettings(idx);
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

px3::LucyUserParameters PX3SynthAudioProcessor::currentLucyUserParameters() const
{
    auto modulated = [this](juce::AudioParameterFloat* param)
    {
        return param->convertFrom0to1(
            applyModulationToNormalizedValue(param,
                                             static_cast<juce::RangedAudioParameter*>(param)->getValue()));
    };

    px3::LucyUserParameters user;
    user.enabled = lucyEnabledParam != nullptr && lucyEnabledParam->get();
    user.filterInvert = lucyFilterInvertParam != nullptr && lucyFilterInvertParam->get();
    user.verbPost = lucyVerbPostParam != nullptr && lucyVerbPostParam->get();
    user.gate = lucyGateParam != nullptr && lucyGateParam->get();
    user.slow = lucySlowParam != nullptr && lucySlowParam->get();

    // The six primary knobs.
    user.global = modulated(lucyGlobalParam);
    user.loss = modulated(lucyLossParam);
    user.speed = modulated(lucySpeedParam);
    user.filter = modulated(lucyFilterParam);
    user.filterFreq = modulated(lucyFilterFreqParam);
    user.verb = modulated(lucyVerbParam);

    // Their alternate functions. Every one is a real parameter with its own
    // modulation and automation; "alternate" is where it lives on the panel.
    user.gateThreshold = modulated(lucyGateThresholdParam);
    user.freezer = modulated(lucyFreezerParam);
    user.verbDecay = modulated(lucyVerbDecayParam);
    user.limiterThreshold = modulated(lucyLimiterThresholdParam);
    user.autoGain = modulated(lucyAutoGainParam);
    user.lossGainDb = modulated(lucyLossGainParam);

    user.spread = modulated(lucySpreadParam);

    user.mode = static_cast<px3::LucyLossMode>(lucyModeParam != nullptr ? lucyModeParam->getIndex() : 0);
    user.packets = static_cast<px3::LucyPacketMode>(lucyPacketsParam != nullptr ? lucyPacketsParam->getIndex() : 0);
    user.slope = static_cast<px3::LucyFilterSlope>(lucySlopeParam != nullptr ? lucySlopeParam->getIndex() : 1);
    user.weighting = static_cast<px3::LucyWeighting>(lucyWeightingParam != nullptr ? lucyWeightingParam->getIndex() : 1);
    user.freeze = static_cast<px3::LucyFreezeMode>(lucyFreezeParam != nullptr ? lucyFreezeParam->getIndex() : 0);

    return user;
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
