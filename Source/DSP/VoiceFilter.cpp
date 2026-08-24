#include "VoiceFilter.h"

#include <cmath>

void VoiceFilter::prepare(double newSampleRate)
{
    sampleRate = juce::jmax(1.0, newSampleRate);
    reset();
    setCurrentSettingsImmediate(currentSettings);
}

void VoiceFilter::reset()
{
    stageA.reset();
    stageB.reset();
    filterUpdateCounter = 0;
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
        currentSettings.modeIndex = targetSettings.modeIndex;
        currentSettings.cutoffHz = targetSettings.cutoffHz;
        currentSettings.resonanceQ = targetSettings.resonanceQ;
        applyFilterResponse(currentSettings.cutoffHz,
                            currentSettings.resonanceQ,
                            currentSettings.modeIndex);
        filterUpdateCounter = 0;
    }
}

void VoiceFilter::setCurrentSettingsImmediate(const FilterSettings& settings)
{
    setTargetSettings(settings);
    currentSettings = targetSettings;
    applyFilterResponse(currentSettings.cutoffHz,
                        currentSettings.resonanceQ,
                        currentSettings.modeIndex);
    filterUpdateCounter = 0;
}

float VoiceFilter::processSample(float inputSample)
{
    if (sampleRate <= 0.0)
    {
        return inputSample;
    }

    constexpr float filterTauSec = 0.005f;
    const auto coeff = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * filterTauSec));

    currentSettings.cutoffHz += (targetSettings.cutoffHz - currentSettings.cutoffHz) * coeff;
    currentSettings.resonanceQ += (targetSettings.resonanceQ - currentSettings.resonanceQ) * coeff;

    if ((filterUpdateCounter++ & 0x07) == 0)
    {
        applyFilterResponse(currentSettings.cutoffHz,
                            currentSettings.resonanceQ,
                            currentSettings.modeIndex);
    }

    const auto mode = static_cast<px3::FilterMode>(px3::clampFilterModeIndex(currentSettings.modeIndex));

    auto output = stageA.processSample(inputSample);
    if (mode == px3::FilterMode::lp24 || mode == px3::FilterMode::hp24)
    {
        output = stageB.processSample(output);
    }
    else if (mode == px3::FilterMode::notch)
    {
        output = inputSample - output * 0.92f;
    }

    return output;
}

void VoiceFilter::applyFilterResponse(float cutoffHz, float resonanceQ, int modeIndex)
{
    if (sampleRate <= 0.0)
    {
        return;
    }

    const auto mode = static_cast<px3::FilterMode>(px3::clampFilterModeIndex(modeIndex));
    const auto cutoff = juce::jlimit(20.0f, 20000.0f, cutoffHz);
    const auto q = juce::jlimit(0.20f, 10.0f, resonanceQ);

    switch (mode)
    {
        case px3::FilterMode::lp12:
        case px3::FilterMode::lp24:
            stageA.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, cutoff, q);
            stageB.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, cutoff, q);
            break;
        case px3::FilterMode::hp12:
        case px3::FilterMode::hp24:
            stageA.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, cutoff, q);
            stageB.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, cutoff, q);
            break;
        case px3::FilterMode::bp:
            stageA.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(sampleRate, cutoff, q);
            break;
        case px3::FilterMode::notch:
            stageA.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(sampleRate, cutoff, q);
            break;
        case px3::FilterMode::allPass:
            stageA.coefficients = juce::dsp::IIR::Coefficients<float>::makeAllPass(sampleRate, cutoff, q);
            break;
        default:
            break;
    }
}
