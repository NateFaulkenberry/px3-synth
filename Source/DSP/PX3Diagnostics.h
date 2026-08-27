#pragma once

// Temporary signal-path isolation instrumentation.
//
// Everything in this header is compiled out unless PX3_DIAGNOSTICS=1, which is
// only defined for the offline PX3Diag console target. The plugin targets build
// byte-identical to before.
//
// Purpose: find the FIRST stage in the signal path at which the waveform
// becomes discontinuous, instead of tuning envelope/poly-gain constants.

#ifndef PX3_DIAGNOSTICS
 #define PX3_DIAGNOSTICS 0
#endif

#if PX3_DIAGNOSTICS

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace px3::diag
{
// Taps in signal-path order. A discontinuity that is absent at stage N and
// present at stage N+1 was created between them.
enum Stage
{
    stageOsc = 0,     // per-voice filtered source sum, BEFORE any voice gain
    stagePostEnv,     // * voiceGain (AMP ENV * velocity * masterGain * guards)
    stageVoiceOut,    // final per-voice output (post release-tail filter / VCA)
    stagePostPoly,    // oscillator bus after the polyphony gain multiply
    stageMaster,      // master bus (post mixer/FX)
    stageCount
};

inline const char* stageName(int stage)
{
    switch (stage)
    {
        case stageOsc:      return "OSC        (pre voice gain)";
        case stagePostEnv:  return "POST-AMPENV(* voiceGain)   ";
        case stageVoiceOut: return "VOICE-OUT  (post tail/VCA) ";
        case stagePostPoly: return "POST-POLY  (* polyGain)    ";
        case stageMaster:   return "MASTER                     ";
        default:            return "?";
    }
}

// One recorded worst-case discontinuity, with every stage's delta at the very
// same sample so the responsible boundary is directly readable.
struct Event
{
    long long globalSample { 0 };
    int blockIndex { 0 };
    int sampleInBlock { 0 };
    std::array<float, stageCount> delta { {} };
    float polyGainDelta { 0.0f };
    bool lifecycleEventHere { false };
};

struct State
{
    // ---------------- isolation modes (set by the harness) ----------------
    bool bypassAmpEnvGain { false };      // AMP ENV still advances; not multiplied in
    bool fixedPolyGain { false };         // polyphony gain forced to exactly 1.0
    bool disableReleasePruning { false };  // release-tail budget not enforced at all
    bool legacyHardStopPruning { false };  // control: pre-fix instantaneous stopNote(false)
    bool legacyPolyphonyLoad { false };    // control: pre-fix key-state load + no attenuation hold
    bool legacyLinearRelease { false };    // control: pre-fix linear AMP ENV release ramp
    bool legacyTailShapeFromEnv { false }; // control: tail filter scheduled off env value
    int onsetGuardCurve { 0 };  // 0=production, 1=legacy t^2, 2=smoothstep
    bool legacyInstantReleaseFilter { false }; // control: release lowpass switched on, not faded in
    bool disableOnsetGuard { false };
    bool disableReleaseTailFilter { false };
    bool freezeVibeReleaseSwitch { false }; // keep held-note vibe path during release

    // ---------------- capture state ---------------------------------------
    bool capturing { false };
    int blockIndex { 0 };
    long long globalSampleBase { 0 };

    // Per-block accumulators filled by the voices (indexed by sample in block).
    std::vector<float> voiceStage[stageCount];
    std::vector<unsigned char> lifecycleMark; // 1 = voice start/stop happened here
    std::vector<unsigned char> noteOffMark;   // 1 = a key was released here
    std::vector<float> postPolySum;           // oscillator bus summed after poly gain
    std::vector<float> blockEnv;              // per-sample AMP ENV of the last voice rendered
    std::vector<float> blockPolyGain;         // per-sample polyphony gain

    // ---------------- per-sample trace (release-tail analysis) -------------
    bool tracing { false };
    std::vector<float> trace[stageCount];
    std::vector<float> traceEnv;
    std::vector<float> tracePolyGain;

    // Per-block inputs to the polyphony gain target, held for the whole block.
    float blockLoad { 0.0f };
    float blockPrePolyPeak { 0.0f };
    float blockOverloadBlend { 0.0f };
    float blockGainTarget { 1.0f };
    float blockActiveVoices { 0.0f };
    float blockReleasingVoices { 0.0f };
    std::vector<float> traceLoad;
    std::vector<float> tracePrePolyPeak;
    std::vector<float> traceOverloadBlend;
    std::vector<float> traceGainTarget;
    std::vector<float> traceActiveVoices;
    std::vector<float> traceReleasingVoices;

    // Carried across blocks so block boundaries are analysed too.
    std::array<float, stageCount> prevSample { {} };
    bool hasPrev { false };

    // ---------------- results ---------------------------------------------
    std::array<float, stageCount> maxDelta { {} };
    std::array<float, stageCount> maxDeltaNoLifecycle { {} };
    std::array<float, stageCount> peak { {} };

    float maxPolyGainDelta { 0.0f };
    float maxPolyGainBlockStep { 0.0f };
    float minPolyGain { 1.0f };
    float maxAmpEnvDelta { 0.0f };
    float maxVoiceGainDelta { 0.0f };
    // Second difference of the per-voice gain envelope. A corner in the gain
    // (slope changing abruptly) spikes this, and unlike an audio-domain metric
    // it is completely independent of which waveform is playing.
    float maxVoiceGainCurvature { 0.0f };
    long long worstCurvatureSample { -1 };
    int worstCurvatureNoteAge { -1 };
    bool worstCurvatureKeyDown { false };
    float worstCurvatureGains[3] { 0.0f, 0.0f, 0.0f };
    float worstCurvatureEnv { 0.0f };

    // Lifecycle counters over the capture window.
    int noteStarts { 0 };
    int oscillatorResets { 0 };
    int hardStops { 0 };            // stopNote(allowTailOff=false)
    int releasePrunes { 0 };
    int clearCurrentNoteEvents { 0 };
    int clearFromReleaseFloor { 0 }; // env fell below the silence threshold
    int clearFromEnvInactive { 0 };  // envelope reported inactive

    // How loud a voice still was at the instant it was truncated.
    float maxTruncationStep { 0.0f };
    float maxHardStopEnv { 0.0f };

    // Loudness / gain-modulation health, which sample-delta metrics cannot see.
    int masterClipSamples { 0 };
    // How much of each release tail is spent at the release lowpass's most
    // aggressive setting (~150 Hz), which is a direct read of whether that
    // filter's schedule still matches the envelope shape.
    long long releaseSamplesTotal { 0 };
    long long releaseSamplesHeavilyFiltered { 0 };

    // Transient/click detection: a click is a spike in the second difference
    // relative to the local signal, so it is visible even at low level where an
    // absolute sample-delta threshold sees nothing.
    float transientPrev1 { 0.0f };
    float transientPrev2 { 0.0f };
    float transientRunningMean { 0.0f };
    float maxTransientRatio { 0.0f };
    int transientEventCount { 0 };
    // Same detector, but scored only well away from any voice start/stop, so it
    // sees artifacts that happen in the middle of a sustaining tail rather than
    // the legitimate transient of a note attack.
    int samplesSinceLifecycle { 1 << 30 };
    // Scored only in the first few ms after a key release, which is where an
    // instantaneous switch in the release path shows up.
    int samplesSinceNoteOff { 1 << 30 };
    float maxNoteOffTransientRatio { 0.0f };
    int noteOffTransientEvents { 0 };
    float maxNoteOffSlopeDrop { 0.0f };
    float maxQuietTransientRatio { 0.0f };
    int quietTransientEvents { 0 };
    long long worstQuietTransientSample { -1 };
    long long worstTransientSample { -1 };
    bool worstTransientAtLifecycle { false };
    // Gain excursion over a perceptually relevant window. Measuring the step
    // across a block boundary only proves the smoother is continuous; it says
    // nothing about the gain lurching around while notes are played.
    std::vector<float> polyGainHistory;
    float maxPolyGainDropPer50ms { 0.0f };
    float maxPolyGainRisePer50ms { 0.0f };
    // How often the gain lurches, not just how far: repeated large moves at
    // note rate are heard as pumping / artifacting even when each move is
    // individually click-free.
    int polyGainLurchSamples { 0 };
    float polyGainLurchSeconds { 0.0f };
    float polyGainRunMin { 2.0f };
    float polyGainRunMax { 0.0f };

    std::vector<Event> worst; // kept sorted descending by master delta

    void resetResults()
    {
        maxDelta.fill(0.0f);
        maxDeltaNoLifecycle.fill(0.0f);
        peak.fill(0.0f);
        prevSample.fill(0.0f);
        hasPrev = false;
        maxPolyGainDelta = 0.0f;
        maxPolyGainBlockStep = 0.0f;
        minPolyGain = 1.0f;
        maxAmpEnvDelta = 0.0f;
        maxVoiceGainDelta = 0.0f;
        maxVoiceGainCurvature = 0.0f;
        worstCurvatureSample = -1;
        worstCurvatureNoteAge = -1;
        worstCurvatureKeyDown = false;
        worstCurvatureGains[0] = worstCurvatureGains[1] = worstCurvatureGains[2] = 0.0f;
        worstCurvatureEnv = 0.0f;
        noteStarts = 0;
        oscillatorResets = 0;
        hardStops = 0;
        releasePrunes = 0;
        clearCurrentNoteEvents = 0;
        clearFromReleaseFloor = 0;
        clearFromEnvInactive = 0;
        maxTruncationStep = 0.0f;
        maxHardStopEnv = 0.0f;
        masterClipSamples = 0;
        releaseSamplesTotal = 0;
        releaseSamplesHeavilyFiltered = 0;
        transientPrev1 = 0.0f;
        transientPrev2 = 0.0f;
        transientRunningMean = 0.0f;
        maxTransientRatio = 0.0f;
        transientEventCount = 0;
        samplesSinceLifecycle = 1 << 30;
        samplesSinceNoteOff = 1 << 30;
        maxNoteOffTransientRatio = 0.0f;
        noteOffTransientEvents = 0;
        maxNoteOffSlopeDrop = 0.0f;
        maxQuietTransientRatio = 0.0f;
        quietTransientEvents = 0;
        worstQuietTransientSample = -1;
        worstTransientSample = -1;
        worstTransientAtLifecycle = false;
        polyGainHistory.clear();
        maxPolyGainDropPer50ms = 0.0f;
        maxPolyGainRisePer50ms = 0.0f;
        polyGainLurchSamples = 0;
        polyGainLurchSeconds = 0.0f;
        polyGainRunMin = 2.0f;
        polyGainRunMax = 0.0f;
        blockIndex = 0;
        globalSampleBase = 0;
        worst.clear();
        for (auto& buffer : trace)
        {
            buffer.clear();
        }
        traceEnv.clear();
        tracePolyGain.clear();
        traceLoad.clear();
        tracePrePolyPeak.clear();
        traceOverloadBlend.clear();
        traceGainTarget.clear();
        traceActiveVoices.clear();
        traceReleasingVoices.clear();
    }

    void resetModes()
    {
        bypassAmpEnvGain = false;
        fixedPolyGain = false;
        disableReleasePruning = false;
        legacyHardStopPruning = false;
        legacyPolyphonyLoad = false;
        legacyLinearRelease = false;
        legacyTailShapeFromEnv = false;
        onsetGuardCurve = 0;
        legacyInstantReleaseFilter = false;
        disableOnsetGuard = false;
        disableReleaseTailFilter = false;
        freezeVibeReleaseSwitch = false;
    }

    void beginBlock(int numSamples)
    {
        for (auto& buffer : voiceStage)
        {
            buffer.assign(static_cast<std::size_t>(numSamples), 0.0f);
        }
        lifecycleMark.assign(static_cast<std::size_t>(numSamples), 0);
        noteOffMark.assign(static_cast<std::size_t>(numSamples), 0);
        blockEnv.assign(static_cast<std::size_t>(numSamples), 0.0f);
        blockPolyGain.assign(static_cast<std::size_t>(numSamples), 1.0f);
    }

    void setEnvSample(int sampleIndex, float value)
    {
        if (sampleIndex >= 0 && static_cast<std::size_t>(sampleIndex) < blockEnv.size())
        {
            blockEnv[static_cast<std::size_t>(sampleIndex)] = value;
        }
    }

    void setPolyGainSample(int sampleIndex, float value)
    {
        if (sampleIndex >= 0 && static_cast<std::size_t>(sampleIndex) < blockPolyGain.size())
        {
            blockPolyGain[static_cast<std::size_t>(sampleIndex)] = value;
        }
    }

    void addVoiceSample(int stage, int sampleIndex, float value)
    {
        auto& buffer = voiceStage[stage];
        if (sampleIndex >= 0 && static_cast<std::size_t>(sampleIndex) < buffer.size())
        {
            buffer[static_cast<std::size_t>(sampleIndex)] += value;
        }
    }

    void markNoteOff(int sampleIndex)
    {
        if (sampleIndex >= 0 && static_cast<std::size_t>(sampleIndex) < noteOffMark.size())
        {
            noteOffMark[static_cast<std::size_t>(sampleIndex)] = 1;
        }
    }

    void markLifecycle(int sampleIndex)
    {
        if (sampleIndex >= 0 && static_cast<std::size_t>(sampleIndex) < lifecycleMark.size())
        {
            lifecycleMark[static_cast<std::size_t>(sampleIndex)] = 1;
        }
    }

    void noteEnvDelta(float delta)
    {
        maxAmpEnvDelta = std::max(maxAmpEnvDelta, std::abs(delta));
    }

    void noteVoiceGainDelta(float delta)
    {
        maxVoiceGainDelta = std::max(maxVoiceGainDelta, std::abs(delta));
    }
};

// Makes voice start phase reproducible so two runs of the same material are
// directly comparable. Defined in SynthVoice.cpp.
void resetNoteStartSequence();

// Single process-wide instance. The offline harness is single-threaded, so no
// synchronisation is needed here.
State& state();

// Analyses one finished block. postPoly/master are read straight out of the
// processor's own buffers; the voice stages come from the per-block
// accumulators above.
void analyseBlock(const float* postPoly, const float* master, int numSamples);
}

#endif
