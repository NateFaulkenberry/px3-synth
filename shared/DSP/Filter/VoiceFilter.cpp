#include "VoiceFilter.h"

#include "FilterResponse.h"

#include <cmath>

void VoiceFilter::prepare(double newSampleRate)
{
    sampleRate = juce::jmax(1.0, newSampleRate);

    // Precomputed once here rather than per sample: this used to call std::exp
    // on every sample, for every filter, source and voice.
    constexpr float filterTauSeconds = 0.005f;
    smoothingCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * filterTauSeconds));

    constexpr float bypassBlendSeconds = 0.008f;
    bypassBlendStep = 1.0f / static_cast<float>(juce::jmax(1.0, sampleRate * bypassBlendSeconds));

    comb.prepare(sampleRate);
    const juce::dsp::ProcessSpec spec { sampleRate, 1, 1 };
    stateVariableA.prepare(spec);
    stateVariableB.prepare(spec);
    stateVariableA.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    stateVariableB.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    reset();
    setCurrentSettingsImmediate(currentSettings);
}

void VoiceFilter::reset()
{
    stageA.reset();
    stageB.reset();
    ladder.reset();
    curtis.reset();
    arp.reset();
    stateVariableA.reset();
    stateVariableB.reset();
    comb.reset();
    filterUpdateCounter = 0;
    modeChangePending = false;
}

void VoiceFilter::setTargetSettings(const FilterSettings& settings)
{
    FilterSettings clamped = settings;
    clamped.cutoffHz = juce::jlimit(20.0f, 20000.0f, clamped.cutoffHz);
    clamped.resonanceQ = juce::jlimit(0.20f, px3::analogfilter::kMaxUserQ, clamped.resonanceQ);
    clamped.modeIndex = px3::clampFilterModeIndex(clamped.modeIndex);

    const auto modeChanged = clamped.modeIndex != targetSettings.modeIndex;
    targetSettings = clamped;

    if (modeChanged)
    {
        // Swapping biquad coefficients under a live state vector is a step
        // discontinuity. Queue the change and let processSample apply it once
        // the crossfade has taken this filter out of circuit.
        pendingModeIndex = clamped.modeIndex;
        modeChangePending = true;
    }
}

void VoiceFilter::setCurrentSettingsImmediate(const FilterSettings& settings)
{
    comb.setCurrentSettingsImmediate(settings.comb);

    setTargetSettings(settings);
    currentSettings = targetSettings;
    modeChangePending = false;
    applyFilter(currentSettings.cutoffHz,
                currentSettings.resonanceQ,
                currentSettings.modeIndex);
    ladder.reset();
    curtis.reset();
    arp.reset();
    filterUpdateCounter = 0;
    // A note start must not fade its filter in.
    bypassBlend = targetSettings.enabled ? 1.0f : 0.0f;
}

float VoiceFilter::processSampleActive(float inputSample)
{
    if (sampleRate <= 0.0)
    {
        return inputSample;
    }

    // Out of circuit when bypassed, or while a queued type change is being
    // crossfaded through.
    const auto wantInCircuit = targetSettings.enabled && !modeChangePending;
    bypassBlend = juce::jlimit(0.0f, 1.0f, bypassBlend + (wantInCircuit ? bypassBlendStep : -bypassBlendStep));

    if (bypassBlend <= 0.0f)
    {
        if (modeChangePending)
        {
            // Fully dry: the filter is inaudible, so the new type and a clean
            // state can be installed without any discontinuity reaching the output.
            currentSettings.modeIndex = pendingModeIndex;
            currentSettings.cutoffHz = targetSettings.cutoffHz;
            currentSettings.resonanceQ = targetSettings.resonanceQ;
            stageA.reset();
            stageB.reset();
            ladder.reset();
            curtis.reset();
            arp.reset();
            stateVariableA.reset();
            stateVariableB.reset();
            comb.reset();
            applyFilter(currentSettings.cutoffHz, currentSettings.resonanceQ, currentSettings.modeIndex);
            filterUpdateCounter = 0;
            modeChangePending = false;
        }
        return inputSample;
    }

    const auto coeff = smoothingCoefficient;

    currentSettings.cutoffHz += (targetSettings.cutoffHz - currentSettings.cutoffHz) * coeff;
    currentSettings.resonanceQ += (targetSettings.resonanceQ - currentSettings.resonanceQ) * coeff;

    const auto mode = static_cast<px3::FilterMode>(px3::clampFilterModeIndex(currentSettings.modeIndex));

    if (mode == px3::FilterMode::comb)
    {
        // The comb does its own per-sample smoothing of every loop parameter,
        // so the settings go straight through rather than being smoothed twice.
        comb.setTargetSettings(targetSettings.comb);

        const auto combOut = comb.processSample(inputSample);
        const auto combBlend = bypassBlend * bypassBlend * (3.0f - 2.0f * bypassBlend);
        return inputSample + (combOut - inputSample) * combBlend;
    }

    if ((filterUpdateCounter++ & 0x07) == 0)
    {
        applyFilter(currentSettings.cutoffHz,
                currentSettings.resonanceQ,
                currentSettings.modeIndex);
    }

    const auto stateVariable = mode == px3::FilterMode::stateVariable12 || mode == px3::FilterMode::stateVariable24;
    const auto ladderMode = mode == px3::FilterMode::ladder12 || mode == px3::FilterMode::ladder24;
    auto output = ladderMode ? ladder.process(inputSample)
                : mode == px3::FilterMode::curtis24 ? curtis.process(inputSample)
                : mode == px3::FilterMode::arp12 ? arp.process(inputSample)
                : stateVariable ? stateVariableA.processSample(0, inputSample) : stageA.processSample(inputSample);
    if (mode == px3::FilterMode::stateVariable24)
    {
        output = stateVariableB.processSample(0, output);
    }
    else if (mode == px3::FilterMode::lp24 || mode == px3::FilterMode::hp24)
    {
        output = stageB.processSample(output);
    }

    // Smoothstep so the crossfade lands at both ends with zero slope.
    const auto blend = bypassBlend * bypassBlend * (3.0f - 2.0f * bypassBlend);
    return inputSample + (output - inputSample) * blend;
}

void VoiceFilter::applyFilter(float cutoffHz, float resonanceQ, int modeIndex)
{
    if (sampleRate <= 0.0)
    {
        return;
    }
    const auto mode = static_cast<px3::FilterMode>(px3::clampFilterModeIndex(modeIndex));
    const auto amount = px3::analogfilter::resonanceAmountFromQ(resonanceQ);
    if (mode == px3::FilterMode::ladder12 || mode == px3::FilterMode::ladder24)
    {
        ladder.set(cutoffHz, amount, sampleRate, mode == px3::FilterMode::ladder12 ? 2 : 4);
        return;
    }
    if (mode == px3::FilterMode::curtis24)
    {
        curtis.set(cutoffHz, amount, sampleRate);
        return;
    }
    if (mode == px3::FilterMode::arp12)
    {
        arp.set(cutoffHz, amount, sampleRate);
        return;
    }
    if (mode == px3::FilterMode::stateVariable12 || mode == px3::FilterMode::stateVariable24)
    {
        const auto cutoff = juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.49), cutoffHz);
        const auto q = juce::jlimit(0.20f, px3::analogfilter::kMaxUserQ, resonanceQ);
        stateVariableA.setCutoffFrequency(cutoff);
        stateVariableB.setCutoffFrequency(cutoff);
        stateVariableA.setResonance(mode == px3::FilterMode::stateVariable24 ? px3::kFourPoleFlatQ : q);
        stateVariableB.setResonance(px3::fourPoleResonantQ(q));
        return;
    }

    // Built by the shared response code, which is also what the filter card's
    // graph draws from - so the curve on screen is this filter and not a
    // drawing of one.
    //
    // ArrayCoefficients returns by value and is assigned into the existing
    // Coefficients object. The Coefficients::makeXxx factories used previously
    // each did "*new Coefficients(...)", allocating on the audio thread on
    // every coefficient update.
    const auto pair = px3::makeFilterCoefficients(modeIndex, sampleRate, cutoffHz, resonanceQ);

    *stageA.coefficients = juce::dsp::IIR::Coefficients<float> {
        pair.stageA[0], pair.stageA[1], pair.stageA[2],
        pair.stageA[3], pair.stageA[4], pair.stageA[5] };

    if (pair.usesStageB)
    {
        *stageB.coefficients = juce::dsp::IIR::Coefficients<float> {
            pair.stageB[0], pair.stageB[1], pair.stageB[2],
            pair.stageB[3], pair.stageB[4], pair.stageB[5] };
    }
}
