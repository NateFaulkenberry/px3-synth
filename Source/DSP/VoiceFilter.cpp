#include "VoiceFilter.h"

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

    reset();
    setCurrentSettingsImmediate(currentSettings);
}

void VoiceFilter::reset()
{
    stageA.reset();
    stageB.reset();
    comb.reset();
    filterUpdateCounter = 0;
    modeChangePending = false;
}

void VoiceFilter::setTargetSettings(const FilterSettings& settings)
{
    FilterSettings clamped = settings;
    clamped.cutoffHz = juce::jlimit(20.0f, 20000.0f, clamped.cutoffHz);
    clamped.resonanceQ = juce::jlimit(0.20f, 10.0f, clamped.resonanceQ);
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

    auto output = stageA.processSample(inputSample);
    if (mode == px3::FilterMode::lp24 || mode == px3::FilterMode::hp24)
    {
        output = stageB.processSample(output);
    }
    else if (mode == px3::FilterMode::notch)
    {
        output = inputSample - output * 0.92f;
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
    // Cutoff is clamped below Nyquist as well as to the parameter range: JUCE's
    // coefficient builders require frequency <= sampleRate / 2, and a high
    // cutoff at a low sample rate would otherwise cross it.
    const auto maxCutoff = static_cast<float>(sampleRate * 0.5) * 0.98f;
    const auto cutoff = juce::jlimit(20.0f, juce::jmax(20.0f, maxCutoff), cutoffHz);
    const auto q = juce::jlimit(0.20f, 10.0f, resonanceQ);

    // ArrayCoefficients returns the coefficients by value and assigns them into
    // the existing Coefficients object. The Coefficients::makeXxx factories
    // used previously each did "*new Coefficients(...)", which allocated on the
    // audio thread on every coefficient update.
    using Array = juce::dsp::IIR::ArrayCoefficients<float>;

    switch (mode)
    {
        case px3::FilterMode::lp12:
        case px3::FilterMode::lp24:
            *stageA.coefficients = Array::makeLowPass(sampleRate, cutoff, q);
            *stageB.coefficients = Array::makeLowPass(sampleRate, cutoff, q);
            break;
        case px3::FilterMode::hp12:
        case px3::FilterMode::hp24:
            *stageA.coefficients = Array::makeHighPass(sampleRate, cutoff, q);
            *stageB.coefficients = Array::makeHighPass(sampleRate, cutoff, q);
            break;
        case px3::FilterMode::bp:
            *stageA.coefficients = Array::makeBandPass(sampleRate, cutoff, q);
            break;
        case px3::FilterMode::notch:
            *stageA.coefficients = Array::makeBandPass(sampleRate, cutoff, q);
            break;
        case px3::FilterMode::allPass:
            *stageA.coefficients = Array::makeAllPass(sampleRate, cutoff, q);
            break;
        default:
            break;
    }
}
