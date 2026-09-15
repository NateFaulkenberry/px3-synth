#include "SubOscillator.h"
#include "OscillatorTuning.h"

#include <cmath>

void SubOscillator::prepare(double newSampleRateHz)
{
    sampleRateHz = juce::jmax(1.0, newSampleRateHz);
    fadeLength = juce::jmax(1, static_cast<int>(std::lround(kWaveformCrossfadeSeconds * sampleRateHz)));
}

void SubOscillator::setSettings(const SubOscSettings& newSettings, int rampSamples)
{
    settings.enabled = newSettings.enabled;
    settings.level = juce::jlimit(0.0f, 1.0f, newSettings.level);
    settings.coarseOctaves = juce::jlimit(px3::tuning::kCoarseMinOctaves, px3::tuning::kCoarseMaxOctaves, newSettings.coarseOctaves);
    settings.fineCents = juce::jlimit(px3::tuning::kFineMinCents, px3::tuning::kFineMaxCents, newSettings.fineCents);
    settings.pitchModSemitones = juce::jlimit(-px3::tuning::kPitchModRangeSemitones, px3::tuning::kPitchModRangeSemitones,
                                              newSettings.pitchModSemitones);
    settings.waveformIndex = px3::clampSubOscWaveformIndex(newSettings.waveformIndex);

    // The ratio is computed once per control block, not once per sample, and
    // ramped across the block so a modulated Pitch Mod is a glide, not a stair.
    const auto ratio = px3::tuning::pitchRatio(settings.coarseOctaves, settings.fineCents, settings.pitchModSemitones);
    if (!configured || rampSamples <= 0)
    {
        ratioStart = ratioTarget = ratioCurrent = ratio;
        rampPosition = rampLength = 1;
    }
    else if (ratio != ratioTarget)
    {
        ratioStart = ratioCurrent;
        ratioTarget = ratio;
        rampLength = rampSamples;
        rampPosition = 0;
    }

    if (!configured)
    {
        activeWaveform = settings.waveformIndex;
        configured = true;
    }
    else if (settings.waveformIndex != activeWaveform && fadeRemaining == 0)
    {
        fadingWaveform = activeWaveform;
        activeWaveform = settings.waveformIndex;
        fadeRemaining = fadeLength;
        (activeWaveform == 1 ? squareLine.reset() : sineDelay.reset());
    }
}

void SubOscillator::resetForNote(double startPhase)
{
    phase = startPhase - std::floor(startPhase);
    ratioStart = ratioCurrent = ratioTarget;
    rampPosition = rampLength;
    activeWaveform = settings.waveformIndex;
    fadingWaveform = -1;
    fadeRemaining = 0;
    squareLine.reset();
    sineDelay.reset();
}

double SubOscillator::renderSample(double baseFrequencyHz)
{
    if (!settings.enabled || settings.level <= 0.0001f)
    {
        return 0.0;
    }

    if (rampPosition < rampLength)
    {
        ++rampPosition;
        ratioCurrent = ratioStart + (ratioTarget - ratioStart) * static_cast<double>(rampPosition) / static_cast<double>(rampLength);
    }

    const auto increment = px3::dsp::phaseIncrement(baseFrequencyHz * ratioCurrent, sampleRateHz);
    const auto wrapped = px3::dsp::advancePhase(phase, increment);
    const auto tau = wrapped && increment > 0.0 ? phase / increment : 0.0;

    auto out = renderWaveform(activeWaveform, increment, wrapped, tau);
    if (fadeRemaining > 0)
    {
        const auto outgoing = renderWaveform(fadingWaveform, increment, wrapped, tau);
        out += (outgoing - out) * static_cast<double>(fadeRemaining) / static_cast<double>(fadeLength);
        if (--fadeRemaining == 0)
        {
            fadingWaveform = -1;
        }
    }

    return out * kSourceTrim;
}

double SubOscillator::renderWaveform(int waveform, double increment, bool wrapped, double tau)
{
    if (px3::clampSubOscWaveformIndex(waveform) == static_cast<int>(px3::SubOscWaveform::square))
    {
        if (increment > 0.0)
        {
            if (wrapped)
            {
                if (phase + 1.0 - increment < 0.5)
                {
                    squareLine.step((phase + 0.5) / increment, -2.0);
                }
                squareLine.step(tau, 2.0);
                if (phase >= 0.5)
                {
                    squareLine.step((phase - 0.5) / increment, -2.0);
                }
            }
            else if (phase - increment < 0.5 && phase >= 0.5)
            {
                squareLine.step((phase - 0.5) / increment, -2.0);
            }
        }
        return squareLine.push(phase < 0.5 ? 1.0 : -1.0);
    }

    return sineDelay.push(std::sin(px3::dsp::kTwoPi * phase));
}
