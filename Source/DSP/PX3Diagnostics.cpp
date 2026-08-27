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
        }

        for (int stage = 0; stage < stageCount; ++stage)
        {
            s.prevSample[static_cast<std::size_t>(stage)] = current[static_cast<std::size_t>(stage)];
            s.peak[static_cast<std::size_t>(stage)] =
                std::max(s.peak[static_cast<std::size_t>(stage)], std::abs(current[static_cast<std::size_t>(stage)]));
        }

        s.hasPrev = true;
    }

    s.globalSampleBase += numSamples;
    ++s.blockIndex;
}
}

#endif
