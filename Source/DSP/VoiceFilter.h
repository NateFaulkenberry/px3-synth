#pragma once

#include <JuceHeader.h>

#include "CombResonator.h"
#include "FilterMode.h"
#include "FilterTypes.h"

// Generic runtime filter unit for synth voices. It encapsulates filtering mode,
// smoothing, and coefficient updates without any synth-specific UI/state logic.
class VoiceFilter
{
public:
    void prepare(double newSampleRate);
    void reset();

    void setTargetSettings(const FilterSettings& settings);
    void setCurrentSettingsImmediate(const FilterSettings& settings);

    // Owns its own bypass. Callers always process every sample through every
    // filter instance; whether the filter is in circuit is this class's
    // business, so no caller needs a special case per filter.
    //
    // A voice runs 4 sources through 2 filter instances, so this is called
    // 8 times per sample per voice - 262144 calls per block at 64 voices - and
    // in a patch with the filters off every one of them exists only to hand
    // back the input. Profiling a 64-voice, filters-off render attributed 32%
    // of all CPU to this function for exactly that reason.
    //
    // The settled-bypass case is therefore tested inline, where it costs a
    // predictable branch instead of a call. It is not a shortcut: when the
    // filter is out of circuit, disabled, and has no queued type change, the
    // out-of-line body below provably returns inputSample and leaves
    // bypassBlend clamped at zero, so this is the same computation.
    float processSample(float inputSample)
    {
        if (isOutOfCircuit())
        {
            return inputSample;
        }

        return processSampleActive(inputSample);
    }

    // True when this filter is settled out of circuit: it returns its input
    // unchanged and holds no state that advancing would alter. A caller whose
    // input is silent can then skip the whole chain rather than pushing zeros
    // through it.
    bool isOutOfCircuit() const noexcept
    {
        return bypassBlend <= 0.0f && ! targetSettings.enabled && ! modeChangePending;
    }

private:
    float processSampleActive(float inputSample);
    void applyFilter(float cutoffHz, float resonanceQ, int modeIndex);

    juce::dsp::IIR::Filter<float> stageA;
    juce::dsp::IIR::Filter<float> stageB;
    // The comb is a different kind of filter, not another biquad response, so
    // it gets its own unit rather than being forced through the IIR stages.
    px3::CombResonator comb;

    FilterSettings targetSettings;
    FilterSettings currentSettings;
    double sampleRate { 0.0 };
    int filterUpdateCounter { 0 };
    float smoothingCoefficient { 1.0f };

    // Bypass and type changes are discontinuous by nature: there is no correct
    // state to carry across them. Both are therefore taken through a short
    // crossfade to dry rather than switched, which is the transition itself
    // rather than smoothing applied to hide one.
    float bypassBlend { 1.0f };
    float bypassBlendStep { 1.0f };
    int pendingModeIndex { 0 };
    bool modeChangePending { false };
};
