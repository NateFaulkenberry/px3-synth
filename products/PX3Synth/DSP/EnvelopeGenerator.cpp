#include "EnvelopeGenerator.h"

#include <cmath>

void EnvelopeGenerator::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1.0, sampleRate);
    snapshot.rebuild(envelope, sampleRateHz);

    // Keep the ramp short: enough to kill clicks at very fast transients,
    // but not long enough to blur envelope timing.
    constexpr double outputSmoothingSeconds = 0.0008;
    outputSmoother.reset(sampleRateHz, outputSmoothingSeconds);
    // SmoothedValue::reset(double, double) computes its ramp exactly so.
    smootherSteps = juce::jmax(0, static_cast<int>(std::floor(outputSmoothingSeconds * sampleRateHz)));
}

void EnvelopeGenerator::setSettingsAndEnvelope(const EnvelopeSettings& settings, const px3::BreakpointEnvelope& shape)
{
    envelopeSettings = settings;
    setEnvelope(shape);
}

void EnvelopeGenerator::setSettings(const EnvelopeSettings& settings)
{
    envelopeSettings = settings;
    setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));
}

void EnvelopeGenerator::setEnvelope(const px3::BreakpointEnvelope& newEnvelope)
{
    envelope = newEnvelope;
    snapshot.rebuild(envelope, sampleRateHz);
}

void EnvelopeGenerator::noteOn()
{
    heldSeconds = 0.0;
    releasedSeconds = 0.0;
    noteHeld = true;
    inRelease = false;
    finished = false;
    releaseLevelAnchor = 0.0f;

    // An envelope nothing was reading has no smoother state to start from.
    if (outputStale)
    {
        resyncStaleOutput();
    }

    // Where the attack starts from - see the member's comment.
    attackLevelAnchor = juce::jlimit(0.0f, 1.0f, outputSmoother.getCurrentValue());
}

void EnvelopeGenerator::noteOff()
{
    // From wherever the envelope actually is. A modulation envelope released
    // during its attack must not jump to the sustain level on the way out any
    // more than an amplitude one may.
    // A one-shot envelope ignores note-off and plays its trajectory out. The
    // key triggered it; it does not gate it.
    //
    // What actually keeps it travelling is the one-shot branch in
    // getNextSample, which is taken before inRelease is ever consulted. This
    // early return is here so no release state is written that nothing will
    // read - a stale anchor sitting behind a live inRelease flag is the kind
    // of thing that comes back once something else changes.
    if (snapshot.isOneShot())
    {
        noteHeld = false;
        return;
    }

    releaseLevelAnchor = juce::jlimit(0.0f, 1.0f, snapshot.valueAtHeld(heldSeconds));
    releasedSeconds = 0.0;
    noteHeld = false;
    inRelease = true;
}

void EnvelopeGenerator::reset()
{
    heldSeconds = 0.0;
    releasedSeconds = 0.0;
    noteHeld = false;
    inRelease = false;
    finished = true;
    releaseLevelAnchor = 0.0f;
    attackLevelAnchor = 0.0f;
    outputSmoother.setCurrentAndTargetValue(0.0f);
    outputStale = false;
    constantRunSamples = 0;
}

bool EnvelopeGenerator::isActive() const
{
    return noteHeld || inRelease || std::abs(outputSmoother.getCurrentValue()) > 1.0e-5f;
}

float EnvelopeGenerator::getNextSample()
{
    const auto step = timeScale / sampleRateHz;

    float raw = 0.0f;

    // level = f(t), on one clock, advancing whether or not the key is down.
    if (snapshot.isOneShot())
    {
        if (! finished)
        {
            raw = juce::jlimit(0.0f, 1.0f, snapshot.valueAtElapsed(heldSeconds));
            heldSeconds += step;

            if (heldSeconds >= snapshot.totalSeconds())
            {
                finished = true;
                noteHeld = false;
            }
        }
    }
    else if (inRelease)
    {
        raw = juce::jlimit(0.0f, 1.0f,
                           snapshot.valueAtReleased(releasedSeconds, releaseLevelAnchor));
        releasedSeconds += step;

        if (snapshot.releaseProgress(releasedSeconds) >= 1.0f)
        {
            inRelease = false;
            finished = true;
        }
    }
    else if (! finished)
    {
        raw = juce::jlimit(0.0f, 1.0f, snapshot.valueAtHeld(heldSeconds, attackLevelAnchor));
        if (noteHeld)
        {
            heldSeconds += step;
            // LOOP: back to the start of the contour on reaching sustain,
            // attacking from the current level so the restart has no jump.
            if (envelopeSettings.loop && heldSeconds >= snapshot.sustainTimeSeconds()
                && snapshot.sustainTimeSeconds() > 0.002)
            {
                heldSeconds = 0.0;
                attackLevelAnchor = raw;
            }
        }
    }

    outputSmoother.setTargetValue(raw);
    return outputSmoother.getNextValue();
}

// ---------------------------------------------------------------------------
// Block-level paths (see beginBlock in the header). Everything below mirrors
// getNextSample branch for branch; a change to one is a change to both.
// ---------------------------------------------------------------------------

float EnvelopeGenerator::currentRawLevel() const noexcept
{
    if (snapshot.isOneShot())
    {
        return finished ? 0.0f : juce::jlimit(0.0f, 1.0f, snapshot.valueAtElapsed(heldSeconds));
    }
    if (inRelease)
    {
        return juce::jlimit(0.0f, 1.0f, snapshot.valueAtReleased(releasedSeconds, releaseLevelAnchor));
    }
    if (! finished)
    {
        return juce::jlimit(0.0f, 1.0f, snapshot.valueAtHeld(heldSeconds, attackLevelAnchor));
    }
    return 0.0f;
}

bool EnvelopeGenerator::rawLevelIsConstant(float& raw) const noexcept
{
    if (snapshot.isOneShot())
    {
        if (! finished) { return false; }
        raw = 0.0f;
        return true;
    }
    if (inRelease) { return false; }
    if (finished)
    {
        raw = 0.0f;
        return true;
    }

    if (noteHeld)
    {
        // LOOP restarts the contour on reaching sustain: never constant.
        if (envelopeSettings.loop && snapshot.sustainTimeSeconds() > 0.002) { return false; }

        // Past the sustain point valueAtHeld(t, anchor) is the sustain value
        // for every later t - once the attack lift has also ended, which on an
        // ADSR it always has by then (the attack ends before the sustain).
        const auto plateau = juce::jmax(snapshot.sustainTimeSeconds(), snapshot.firstSegmentEnd());
        if (heldSeconds < plateau) { return false; }
    }
    // Not held and not released (a one-shot reshaped after note-off): the
    // clock does not advance, so neither does the level.

    raw = juce::jlimit(0.0f, 1.0f, snapshot.valueAtHeld(heldSeconds, attackLevelAnchor));
    return true;
}

void EnvelopeGenerator::smoothConstant(float raw, int count) noexcept
{
    if (count <= 0) { return; }

    if (! outputStale)
    {
        // Exactly what count getNextSample calls do to the smoother: the first
        // setTargetValue(raw) is the only one that can change anything, and
        // once the countdown ends further calls return the target untouched.
        outputSmoother.setTargetValue(raw);
        auto i = 0;
        for (; i < count && outputSmoother.isSmoothing(); ++i)
        {
            unobservedPeak = juce::jmax(unobservedPeak, outputSmoother.getNextValue());
        }
        if (i < count)
        {
            unobservedPeak = juce::jmax(unobservedPeak, outputSmoother.getTargetValue());
        }
        return;
    }

    // Stale. Whatever the smoother held, smootherSteps samples at one target
    // end its ramp on that target, so after a long enough constant stretch its
    // state is known again.
    if (constantRunSamples == 0 || ! (raw == constantRunLevel))
    {
        constantRunSamples = 0;
        constantRunLevel = raw;
    }
    constantRunSamples = juce::jmin(constantRunSamples + count, 1 << 30);
    unobservedPeak = juce::jmax(unobservedPeak, raw);
    if (constantRunSamples >= smootherSteps)
    {
        outputSmoother.setCurrentAndTargetValue(raw);
        outputStale = false;
        constantRunSamples = 0;
    }
}

void EnvelopeGenerator::resyncStaleOutput() noexcept
{
    outputSmoother.setCurrentAndTargetValue(currentRawLevel());
    outputStale = false;
    constantRunSamples = 0;
}

void EnvelopeGenerator::advanceUnobserved(int numSamples) noexcept
{
    const auto step = timeScale / sampleRateHz;
    auto remaining = numSamples;

    while (remaining > 0)
    {
        float raw = 0.0f;
        if (rawLevelIsConstant(raw))
        {
            // Constant from here to the end of the block. Only a held sustain
            // has a clock still running.
            if (! snapshot.isOneShot() && ! inRelease && ! finished && noteHeld)
            {
                for (auto i = 0; i < remaining; ++i) { heldSeconds += step; }
            }
            smoothConstant(raw, remaining);
            return;
        }

        smoothUnknown();

        if (snapshot.isOneShot())
        {
            while (remaining > 0 && ! finished)
            {
                heldSeconds += step;
                --remaining;
                if (heldSeconds >= snapshot.totalSeconds())
                {
                    finished = true;
                    noteHeld = false;
                }
            }
        }
        else if (inRelease)
        {
            while (remaining > 0 && inRelease)
            {
                releasedSeconds += step;
                --remaining;
                if (snapshot.releaseProgress(releasedSeconds) >= 1.0f)
                {
                    inRelease = false;
                    finished = true;
                }
            }
        }
        else
        {
            // Held and moving: rawLevelIsConstant has ruled out everything else.
            const auto sustainTime = snapshot.sustainTimeSeconds();
            if (envelopeSettings.loop && sustainTime > 0.002)
            {
                for (; remaining > 0; --remaining)
                {
                    // The restart anchors on the level of the sample that
                    // reached sustain - the one level LOOP needs computed.
                    const auto before = heldSeconds;
                    heldSeconds += step;
                    if (heldSeconds >= sustainTime)
                    {
                        attackLevelAnchor = juce::jlimit(0.0f, 1.0f, snapshot.valueAtHeld(before, attackLevelAnchor));
                        heldSeconds = 0.0;
                    }
                }
            }
            else
            {
                const auto plateau = juce::jmax(sustainTime, snapshot.firstSegmentEnd());
                while (remaining > 0 && heldSeconds < plateau)
                {
                    heldSeconds += step;
                    --remaining;
                }
            }
        }
    }
}

EnvelopeGenerator::BlockPath EnvelopeGenerator::beginBlock(int numSamples, bool observed,
                                                           float& level, float& peak) noexcept
{
    float raw = 0.0f;
    // juce::approximatelyEqual is the test SmoothedValue::setTargetValue makes:
    // when it holds, getNextSample's setTargetValue(raw) does nothing and
    // getNextValue returns the target, sample after sample.
    if (! outputStale && ! outputSmoother.isSmoothing() && rawLevelIsConstant(raw)
        && juce::approximatelyEqual(raw, outputSmoother.getTargetValue()))
    {
        if (! snapshot.isOneShot() && ! inRelease && ! finished && noteHeld)
        {
            const auto step = timeScale / sampleRateHz;
            for (auto i = 0; i < numSamples; ++i) { heldSeconds += step; }
        }
        level = outputSmoother.getTargetValue();
        peak = level;
        return BlockPath::steady;
    }

    if (! observed)
    {
        unobservedPeak = 0.0f;
        advanceUnobserved(numSamples);
        level = outputStale ? currentRawLevel() : outputSmoother.getCurrentValue();
        peak = juce::jmax(unobservedPeak, level);
        return BlockPath::unobserved;
    }

    if (outputStale)
    {
        resyncStaleOutput();
    }
    return BlockPath::perSample;
}
