#include "PX3Diagnostics.h"

// Keeps this translation unit non-empty when diagnostics are compiled out.
namespace px3::diag
{
}

#if PX3_DIAGNOSTICS

#include <algorithm>

namespace px3::diag
{
State& state()
{
    static State instance;
    return instance;
}

void analyseBlock(const float* postPoly, const float* master, int numSamples)
{
    auto& s = state();
    if (!s.capturing || numSamples <= 0)
    {
        return;
    }

    std::array<float, stageCount> current { {} };

    for (int n = 0; n < numSamples; ++n)
    {
        const auto idx = static_cast<std::size_t>(n);
        current[stageOsc] = s.voiceStage[stageOsc][idx];
        current[stagePostEnv] = s.voiceStage[stagePostEnv][idx];
        current[stageVoiceOut] = s.voiceStage[stageVoiceOut][idx];
        current[stagePostPoly] = postPoly[n];
        current[stageMaster] = master[n];

        const auto lifecycleHere = s.lifecycleMark[idx] != 0;
        s.samplesSinceNoteOff = s.noteOffMark[idx] != 0 ? 0 : s.samplesSinceNoteOff + 1;

        if (std::abs(current[stageMaster]) > 1.0f)
        {
            ++s.masterClipSamples;
        }

        {
            const auto x = current[stageMaster];

            const auto secondDifference = std::abs(x - 2.0f * s.transientPrev1 + s.transientPrev2);
            // Only score where there is actually signal, so the numerical noise
            // floor after a tail ends cannot register as a click.
            if (std::abs(x) > 1.0e-4f && s.transientRunningMean > 1.0e-9f)
            {
                const auto ratio = secondDifference / s.transientRunningMean;
                if (ratio > s.maxTransientRatio)
                {
                    s.maxTransientRatio = ratio;
                    s.worstTransientSample = s.globalSampleBase + n;
                    s.worstTransientAtLifecycle = lifecycleHere;
                }
                if (ratio > 8.0f)
                {
                    ++s.transientEventCount;
                }

                // First 3 ms after a key release.
                if (s.samplesSinceNoteOff < 144)
                {
                    if (ratio > s.maxNoteOffTransientRatio)
                    {
                        s.noteOffWorstAbsSecondDiff = secondDifference;
                        s.noteOffWorstRunningMean = s.transientRunningMean;
                        s.noteOffWorstLevel = std::abs(x);
                        s.noteOffWorstSample = s.globalSampleBase + n;
                        s.noteOffWorstAtLifecycle = lifecycleHere;
                        s.noteOffWorstSamplesSinceLifecycle = s.samplesSinceLifecycle;
                        s.noteOffWorstSamplesSinceNoteOff = s.samplesSinceNoteOff;
                    }
                    s.maxNoteOffTransientRatio = std::max(s.maxNoteOffTransientRatio, ratio);
                    if (ratio > 8.0f)
                    {
                        ++s.noteOffTransientEvents;
                    }
                }

                // 50 ms clear of any voice start/stop.
                if (s.samplesSinceLifecycle > 2400)
                {
                    if (ratio > s.maxQuietTransientRatio)
                    {
                        s.maxQuietTransientRatio = ratio;
                        s.worstQuietTransientSample = s.globalSampleBase + n;
                    }
                    if (ratio > 8.0f)
                    {
                        ++s.quietTransientEvents;
                    }
                }
            }
            // ~20 ms running mean of the second difference.
            s.samplesSinceLifecycle = lifecycleHere ? 0 : s.samplesSinceLifecycle + 1;
            constexpr float alpha = 1.0f / 960.0f;
            s.transientRunningMean += (secondDifference - s.transientRunningMean) * alpha;
            s.transientPrev2 = s.transientPrev1;
            s.transientPrev1 = x;
        }

        if (s.hasPrev)
        {
            Event event;
            event.blockIndex = s.blockIndex;
            event.sampleInBlock = n;
            event.globalSample = s.globalSampleBase + n;
            event.lifecycleEventHere = lifecycleHere;

            for (int stage = 0; stage < stageCount; ++stage)
            {
                const auto delta = std::abs(current[stage] - s.prevSample[stage]);
                event.delta[static_cast<std::size_t>(stage)] = delta;
                s.maxDelta[static_cast<std::size_t>(stage)] =
                    std::max(s.maxDelta[static_cast<std::size_t>(stage)], delta);
                if (!lifecycleHere)
                {
                    s.maxDeltaNoLifecycle[static_cast<std::size_t>(stage)] =
                        std::max(s.maxDeltaNoLifecycle[static_cast<std::size_t>(stage)], delta);
                }
            }

            // Keep the handful of worst master-stage jumps, with every stage's
            // delta at that exact sample.
            const auto masterDelta = event.delta[stageMaster];
            if (s.worst.size() < 12 || masterDelta > s.worst.back().delta[stageMaster])
            {
                s.worst.push_back(event);
                std::sort(s.worst.begin(),
                          s.worst.end(),
                          [](const Event& a, const Event& b)
                          {
                              return a.delta[stageMaster] > b.delta[stageMaster];
                          });
                if (s.worst.size() > 12)
                {
                    s.worst.resize(12);
                }
            }
        }

        {
            // 50 ms window over the applied polyphony gain.
            const auto gain = s.blockPolyGain[idx];
            s.polyGainHistory.push_back(gain);
            s.polyGainRunMin = std::min(s.polyGainRunMin, gain);
            s.polyGainRunMax = std::max(s.polyGainRunMax, gain);
            constexpr std::size_t window = 2400;
            if (s.polyGainHistory.size() > window)
            {
                const auto past = s.polyGainHistory[s.polyGainHistory.size() - 1 - window];
                s.maxPolyGainDropPer50ms = std::max(s.maxPolyGainDropPer50ms, past - gain);
                s.maxPolyGainRisePer50ms = std::max(s.maxPolyGainRisePer50ms, gain - past);
                if (gain > 1.0e-6f && past > 1.0e-6f)
                {
                    const auto changeDb = std::abs(20.0f * std::log10(gain / past));
                    if (changeDb > 3.0f)
                    {
                        ++s.polyGainLurchSamples;
                    }
                }
            }
        }

        if (s.tracing)
        {
            for (int stage = 0; stage < stageCount; ++stage)
            {
                s.trace[static_cast<std::size_t>(stage)].push_back(current[static_cast<std::size_t>(stage)]);
            }
            s.traceEnv.push_back(s.blockEnv[idx]);
            s.tracePolyGain.push_back(s.blockPolyGain[idx]);
            s.traceLoad.push_back(s.blockLoad);
            s.tracePrePolyPeak.push_back(s.blockPrePolyPeak);
            s.traceOverloadBlend.push_back(s.blockOverloadBlend);
            s.traceGainTarget.push_back(s.blockGainTarget);
            s.traceActiveVoices.push_back(s.blockActiveVoices);
            s.traceReleasingVoices.push_back(s.blockReleasingVoices);
        }

        for (int stage = 0; stage < stageCount; ++stage)
        {
            s.prevSample[static_cast<std::size_t>(stage)] = current[static_cast<std::size_t>(stage)];
            s.peak[static_cast<std::size_t>(stage)] =
                std::max(s.peak[static_cast<std::size_t>(stage)], std::abs(current[static_cast<std::size_t>(stage)]));
        }

        s.hasPrev = true;
    }

    s.polyGainLurchSeconds = static_cast<float>(s.polyGainLurchSamples) / 48000.0f;
    s.globalSampleBase += numSamples;
    ++s.blockIndex;
}
}

#endif
