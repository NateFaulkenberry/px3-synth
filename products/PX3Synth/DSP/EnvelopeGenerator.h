#pragma once

#include <JuceHeader.h>

#include "EnvelopeTypes.h"
#include "BreakpointEnvelope.h"

// Generic runtime ADSR generator. It does not assume destination usage
// (amplitude, filter cutoff modulation, etc.).
class EnvelopeGenerator
{
public:
    void prepare(double sampleRateHz);
    void setSettings(const EnvelopeSettings& settings);

    // The full shape, when the envelope is more than the four ADSR numbers can
    // describe. setSettings is the same call with an ADSR built for it.
    void setEnvelope(const px3::BreakpointEnvelope& envelope);
    void noteOn();
    void noteOff();
    void reset();
    bool isActive() const;
    float getNextSample();
    // How fast this note runs the contour; set per note by keyboard tracking.
    void setTimeScale(double scale) noexcept { timeScale = juce::jlimit(0.05, 20.0, scale); }

    // setSettings with the shape that replaces the ADSR the settings describe.
    // The settings still own LOOP. Same end state as setSettings followed by
    // setEnvelope, without building the ADSR shape only to discard it.
    void setSettingsAndEnvelope(const EnvelopeSettings& settings, const px3::BreakpointEnvelope& shape);

    // How the voice runs this envelope for one block of `numSamples`.
    //
    // perSample: call getNextSample() numSamples times, as before.
    //
    // steady: the output has settled on a level the state guarantees cannot
    // move this block - a held sustain, or finished at zero. The clocks have
    // already been advanced exactly as numSamples getNextSample() calls would
    // have (the same additions, in the same order) and `level` is what every
    // one of those calls would have returned. Bit-identical, so it is taken
    // whether or not anything reads the envelope.
    //
    // unobserved: nothing reads this envelope (no route from it), so only its
    // state advanced: the clocks, the stage, LOOP restarts, the release end.
    // Those are functions of time and state alone, so they are exact. The one
    // thing that is not is the 0.8 ms output smoother, a per-sample IIR whose
    // state depends on every level before it. While the levels are a known
    // constant it is stepped exactly; across a moving segment it goes stale,
    // and becomes exact again after a constant stretch as long as the smoother
    // (by then it has provably settled on that constant). `level` and `peak`
    // are a block-rate approximation of the output while stale.
    //
    // When a route appears and this returns perSample for an envelope whose
    // smoother is stale, the smoother restarts from the current level: the
    // difference from full evaluation is the smoother's lag, at most one
    // 0.8 ms ramp, and only for a route added while the envelope is moving.
    enum class BlockPath { perSample, steady, unobserved };
    BlockPath beginBlock(int numSamples, bool observed, float& level, float& peak) noexcept;

    // Where the envelope currently is, for drawing it. See EnvelopePosition.
    EnvelopePosition currentPosition() const noexcept
    {
        EnvelopePosition position;
        position.active = ! finished;
        position.inRelease = inRelease;
        position.sustainSeconds = snapshot.sustainTimeSeconds();
        // Clamped at the sustain point for an ADSR, because that is where an
        // ADSR waits. A one-shot envelope never waits, so its elapsed time is
        // reported whole - clamping it would stop the fill part way along a
        // trajectory the envelope is still travelling.
        position.heldSeconds = snapshot.isOneShot()
                                 ? heldSeconds
                                 : juce::jmin(heldSeconds, position.sustainSeconds);
        position.releasedSeconds = releasedSeconds;
        return position;
    }

private:
    // The level getNextSample would compute next, before smoothing.
    float currentRawLevel() const noexcept;
    // True, with that level, when the raw level is constant from here for as
    // long as nothing outside the envelope changes it (note-off, a new shape).
    bool rawLevelIsConstant(float& raw) const noexcept;
    void advanceUnobserved(int numSamples) noexcept;
    // `count` samples whose raw level was `raw`, as far as the smoother goes.
    void smoothConstant(float raw, int count) noexcept;
    // Samples whose levels were not computed: the smoother no longer knows.
    void smoothUnknown() noexcept { outputStale = true; constantRunSamples = 0; }
    void resyncStaleOutput() noexcept;

    double sampleRateHz { 44100.0 };
    double timeScale { 1.0 };
    EnvelopeSettings envelopeSettings;

    px3::BreakpointEnvelope envelope;
    px3::BreakpointEnvelope::Snapshot snapshot;

    double heldSeconds { 0.0 };
    double releasedSeconds { 0.0 };
    bool noteHeld { false };
    bool inRelease { false };
    bool finished { true };
    float releaseLevelAnchor { 0.0f };

    // Where the attack begins. Zero on a fresh voice; on a retrigger it is the
    // level the envelope had reached, so the new attack rises from there
    // rather than diving to silence first. Same reason as the amp envelope's.
    float attackLevelAnchor { 0.0f };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputSmoother;
    // The smoother's ramp length in samples, computed the way SmoothedValue
    // computes it: after this many samples at one target it has reached it.
    int smootherSteps { 0 };
    // See beginBlock. While stale, the smoother's state is not what per-sample
    // evaluation would have left; constantRunSamples counts the samples since
    // the raw level last became the known constant constantRunLevel.
    bool outputStale { false };
    int constantRunSamples { 0 };
    float constantRunLevel { 0.0f };
    float unobservedPeak { 0.0f };
};
