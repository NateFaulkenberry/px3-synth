// Offline signal-path isolation harness (temporary diagnostic tool).
//
// Renders the real PX3SynthAudioProcessor headlessly, drives it with a
// reproducible "rapid notes + long release" MIDI pattern, and measures
// max |x[n] - x[n-1]| at every stage of the signal path so the first stage that
// becomes discontinuous can be identified without listening tests.
//
// Build: cmake -B build/diag -DPX3_BUILD_DIAGNOSTIC=ON && cmake --build build/diag --target PX3Diag

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "PX3Diagnostics.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct ScheduledMidi
{
    int sampleTime;
    juce::MidiMessage message;
};

juce::RangedAudioParameter* findParameter(juce::AudioProcessor& processor, const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
        {
            if (withId->getParameterID() == id)
            {
                return dynamic_cast<juce::RangedAudioParameter*>(parameter);
            }
        }
    }

    return nullptr;
}

void setParameter(juce::AudioProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = findParameter(processor, id))
    {
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        return;
    }

    std::printf("  !! parameter not found: %s\n", id.toRawUTF8());
}

enum class Pattern
{
    rapid,       // fast repeated single notes -> many overlapping release tails
    denseChords, // stacked chords, forces heavy release-tail pruning
    sustained,   // held polyphonic chord, released together
    legatoRuns   // fast overlapping runs with sustained pedal-like overlap
};

// Reproduction pattern: fast repeated notes with a long AMP release, which is
// the reported artifact condition.
std::vector<ScheduledMidi> buildPattern(Pattern pattern, double sampleRate, int& totalSamplesOut)
{
    std::vector<ScheduledMidi> events;
    auto addNote = [&events, sampleRate](double onSeconds, double holdSeconds, int note, float velocity)
    {
        const auto onSample = static_cast<int>(onSeconds * sampleRate);
        const auto offSample = static_cast<int>((onSeconds + holdSeconds) * sampleRate);
        events.push_back({ onSample, juce::MidiMessage::noteOn(1, note, velocity) });
        events.push_back({ offSample, juce::MidiMessage::noteOff(1, note) });
    };

    double lastEventSeconds = 0.0;

    switch (pattern)
    {
        case Pattern::rapid:
        {
            const int pitches[] = { 60, 64, 67, 72, 71, 67, 64, 60, 62, 65, 69, 74, 72, 69, 65, 62, 60, 67 };
            for (int i = 0; i < 18; ++i)
            {
                const auto on = 0.25 + static_cast<double>(i) * 0.11;
                addNote(on, 0.075, pitches[i], 0.9f);
                lastEventSeconds = on + 0.075;
            }
            break;
        }

        case Pattern::denseChords:
        {
            const int roots[] = { 48, 53, 55, 50, 48, 55, 57, 52 };
            for (int chord = 0; chord < 8; ++chord)
            {
                const auto on = 0.25 + static_cast<double>(chord) * 0.18;
                for (const auto interval : { 0, 4, 7, 11, 14 })
                {
                    addNote(on, 0.12, roots[chord] + interval, 0.85f);
                }
                lastEventSeconds = on + 0.12;
            }
            break;
        }

        case Pattern::sustained:
        {
            for (const auto interval : { 0, 3, 7, 10, 14, 17 })
            {
                addNote(0.25, 2.0, 52 + interval, 0.8f);
            }
            lastEventSeconds = 2.25;
            break;
        }

        case Pattern::legatoRuns:
        {
            const int scale[] = { 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79 };
            for (int i = 0; i < 24; ++i)
            {
                const auto on = 0.25 + static_cast<double>(i) * 0.07;
                addNote(on, 0.16, scale[i % 12], 0.85f);
                lastEventSeconds = on + 0.16;
            }
            break;
        }
    }

    // Run well past the last release so every tail either completes or is cut.
    totalSamplesOut = static_cast<int>((lastEventSeconds + 3.5) * sampleRate);
    return events;
}

struct PatchOptions
{
    float attack { 0.006f };
    float decay { 0.138f };
    float sustain { 1.0f };
    float release { 1.246f };
    bool vibeEnabled { true };
    float vibeAmount { 0.0f };
    bool fxEnabled { true };
    bool modEnvelopes { false };
    bool lfoModulation { false };
    bool pitchModulation { false };
    Pattern pattern { Pattern::rapid };
};

void applyPatch(PX3SynthAudioProcessor& processor, const PatchOptions& patch)
{
    setParameter(processor, "ampAttack", patch.attack);
    setParameter(processor, "ampDecay", patch.decay);
    setParameter(processor, "ampSustain", patch.sustain);
    setParameter(processor, "ampRelease", patch.release);
    setParameter(processor, "ampEnvEnabled", 1.0f);
    setParameter(processor, "vibeEnabled", patch.vibeEnabled ? 1.0f : 0.0f);
    setParameter(processor, "vibeAmount", patch.vibeAmount);
    setParameter(processor, "delayEnabled", patch.fxEnabled ? 1.0f : 0.0f);
    setParameter(processor, "reverbEnabled", patch.fxEnabled ? 1.0f : 0.0f);
    setParameter(processor, "moodEnabled", patch.fxEnabled ? 1.0f : 0.0f);
    if (patch.fxEnabled)
    {
        setParameter(processor, "delayAmount", 0.4f);
        setParameter(processor, "reverbAmount", 0.4f);
        setParameter(processor, "moodMix", 0.3f);
    }

    for (int envIndex = 0; envIndex < 3; ++envIndex)
    {
        const auto slot = juce::String(envIndex + 1);
        setParameter(processor, "env" + slot + "Enabled", patch.modEnvelopes ? 1.0f : 0.0f);
        setParameter(processor, envIndex == 0 ? juce::String("envAmount") : "env" + slot + "Amount",
                     patch.modEnvelopes ? 0.7f : 0.0f);
        setParameter(processor, "env" + slot + "Attack", 0.02f + 0.05f * static_cast<float>(envIndex));
        setParameter(processor, "env" + slot + "Decay", 0.25f);
        setParameter(processor, "env" + slot + "Sustain", 0.5f);
        setParameter(processor, "env" + slot + "Release", 0.8f);
    }
    if (patch.modEnvelopes)
    {
        processor.setEnvelopeAssignmentByParameterId(0, "filter1Cutoff", false);
        processor.setEnvelopeAssignmentByParameterId(1, "osc1MacroA", false);
        processor.setEnvelopeAssignmentByParameterId(2, "osc1Level", false);
    }

    for (int lfoIndex = 0; lfoIndex < 3; ++lfoIndex)
    {
        const auto slot = juce::String(lfoIndex + 1);
        const auto prefix = lfoIndex == 0 ? juce::String("lfo") : "lfo" + slot;
        setParameter(processor, lfoIndex == 0 ? juce::String("lfoEnabled") : prefix + "Enabled",
                     patch.lfoModulation ? 1.0f : 0.0f);
        setParameter(processor, lfoIndex == 0 ? juce::String("lfoAmount") : prefix + "Amount",
                     patch.lfoModulation ? 0.6f : 0.0f);
        setParameter(processor, lfoIndex == 0 ? juce::String("lfoFrequency") : prefix + "Frequency",
                     2.0f + static_cast<float>(lfoIndex));
    }
    if (patch.lfoModulation)
    {
        processor.setLfoAssignmentByParameterId(0, "filter1Cutoff", false);
        processor.setLfoAssignmentByParameterId(1, "osc1Level", false);
    }
    if (patch.pitchModulation)
    {
        processor.setLfoAssignmentByParameterId(2, "osc1Pitch", false);
        setParameter(processor, "lfo3Enabled", 1.0f);
        setParameter(processor, "lfo3Amount", 0.8f);
        setParameter(processor, "lfo3Frequency", 5.0f);
    }
}

struct ModeOptions
{
    const char* name { "" };
    const char* description { "" };
    bool bypassAmpEnvGain { false };
    bool fixedPolyGain { false };
    bool disableReleasePruning { false };
    bool disableOnsetGuard { false };
    bool disableReleaseTailFilter { false };
    bool freezeVibeReleaseSwitch { false };
};

void runMode(const ModeOptions& mode, const PatchOptions& patch)
{
    PX3SynthAudioProcessor processor;

    applyPatch(processor, patch);

    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    auto& diag = px3::diag::state();
    diag.resetResults();
    diag.resetModes();
    diag.bypassAmpEnvGain = mode.bypassAmpEnvGain;
    diag.fixedPolyGain = mode.fixedPolyGain;
    diag.disableReleasePruning = mode.disableReleasePruning;
    diag.disableOnsetGuard = mode.disableOnsetGuard;
    diag.disableReleaseTailFilter = mode.disableReleaseTailFilter;
    diag.freezeVibeReleaseSwitch = mode.freezeVibeReleaseSwitch;
    diag.capturing = true;

    int totalSamples = 0;
    const auto events = buildPattern(patch.pattern, kSampleRate, totalSamples);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    std::size_t nextEvent = 0;

    for (int position = 0; position < totalSamples; position += kBlockSize)
    {
        buffer.clear();

        juce::MidiBuffer midi;
        while (nextEvent < events.size() && events[nextEvent].sampleTime < position + kBlockSize)
        {
            midi.addEvent(events[nextEvent].message,
                          juce::jmax(0, events[nextEvent].sampleTime - position));
            ++nextEvent;
        }

        processor.processBlock(buffer, midi);
    }

    diag.capturing = false;

    std::printf("\n================================================================\n");
    std::printf("MODE %s  —  %s\n", mode.name, mode.description);
    std::printf("  ampEnvGain=%s  polyGain=%s  pruning=%s  onsetGuard=%s  tailFilter=%s  vibeRelSwitch=%s\n",
                mode.bypassAmpEnvGain ? "BYPASSED" : "normal",
                mode.fixedPolyGain ? "FIXED 1.0" : "dynamic",
                mode.disableReleasePruning ? "DISABLED" : "normal",
                mode.disableOnsetGuard ? "DISABLED" : "normal",
                mode.disableReleaseTailFilter ? "DISABLED" : "normal",
                mode.freezeVibeReleaseSwitch ? "FROZEN" : "normal");
    std::printf("================================================================\n");

    std::printf("  %-28s %12s %14s %16s\n", "stage", "peak", "max|dx|", "max|dx| no-lifecycle");
    for (int stage = 0; stage < px3::diag::stageCount; ++stage)
    {
        std::printf("  %-28s %12.6f %14.6f %16.6f\n",
                    px3::diag::stageName(stage),
                    static_cast<double>(diag.peak[static_cast<std::size_t>(stage)]),
                    static_cast<double>(diag.maxDelta[static_cast<std::size_t>(stage)]),
                    static_cast<double>(diag.maxDeltaNoLifecycle[static_cast<std::size_t>(stage)]));
    }

    std::printf("\n  control-signal movement\n");
    std::printf("    max |ampEnv[n]-ampEnv[n-1]|   = %.9f\n", static_cast<double>(diag.maxAmpEnvDelta));
    std::printf("    max |voiceGain[n]-[n-1]|      = %.9f\n", static_cast<double>(diag.maxVoiceGainDelta));
    std::printf("    max |polyGain[n]-polyGain[n-1]| = %.9f\n", static_cast<double>(diag.maxPolyGainDelta));
    std::printf("    max per-block polyGain target step = %.9f\n", static_cast<double>(diag.maxPolyGainBlockStep));
    std::printf("    min polyGain                  = %.6f\n", static_cast<double>(diag.minPolyGain));

    std::printf("\n  voice lifecycle over window\n");
    std::printf("    noteStarts                = %d\n", diag.noteStarts);
    std::printf("    oscillatorResets          = %d\n", diag.oscillatorResets);
    std::printf("    hardStops stopNote(false) = %d\n", diag.hardStops);
    std::printf("    releaseVoicesPruned       = %d\n", diag.releasePrunes);
    std::printf("    clearCurrentNote total    = %d\n", diag.clearCurrentNoteEvents);
    std::printf("      via release floor       = %d\n", diag.clearFromReleaseFloor);
    std::printf("      via envelope inactive   = %d\n", diag.clearFromEnvInactive);
    std::printf("    max |voice out| at kill   = %.6f\n", static_cast<double>(diag.maxTruncationStep));
    std::printf("    max ampEnv at hard stop   = %.6f\n", static_cast<double>(diag.maxHardStopEnv));

    std::printf("\n  worst master discontinuities (all stages at the same sample)\n");
    std::printf("    %8s %8s %10s %10s %10s %10s %10s  %s\n",
                "block", "sample", "OSC", "POSTENV", "VOICEOUT", "POSTPOLY", "MASTER", "lifecycle");
    const std::size_t shown = diag.worst.size() < 8 ? diag.worst.size() : 8;
    for (std::size_t i = 0; i < shown; ++i)
    {
        const auto& event = diag.worst[i];
        std::printf("    %8d %8d %10.6f %10.6f %10.6f %10.6f %10.6f  %s\n",
                    event.blockIndex,
                    event.sampleInBlock,
                    static_cast<double>(event.delta[px3::diag::stageOsc]),
                    static_cast<double>(event.delta[px3::diag::stagePostEnv]),
                    static_cast<double>(event.delta[px3::diag::stageVoiceOut]),
                    static_cast<double>(event.delta[px3::diag::stagePostPoly]),
                    static_cast<double>(event.delta[px3::diag::stageMaster]),
                    event.lifecycleEventHere ? "YES" : "-");
    }

    std::fflush(stdout);
}

// Regression check: the production path must not produce any discontinuity that
// is attributable to a voice lifecycle event, and voices must never be retired
// while still audible.
bool runRegressionCase(const char* name, const PatchOptions& patch, bool legacyPruning = false)
{
    PX3SynthAudioProcessor processor;
    applyPatch(processor, patch);
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    auto& diag = px3::diag::state();
    diag.resetResults();
    diag.resetModes();
    diag.legacyHardStopPruning = legacyPruning;
    diag.capturing = true;

    int totalSamples = 0;
    const auto events = buildPattern(patch.pattern, kSampleRate, totalSamples);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    std::size_t nextEvent = 0;
    for (int position = 0; position < totalSamples; position += kBlockSize)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        while (nextEvent < events.size() && events[nextEvent].sampleTime < position + kBlockSize)
        {
            midi.addEvent(events[nextEvent].message, juce::jmax(0, events[nextEvent].sampleTime - position));
            ++nextEvent;
        }
        processor.processBlock(buffer, midi);
    }
    diag.capturing = false;

    const auto masterDelta = diag.maxDelta[px3::diag::stageMaster];
    const auto masterDeltaClean = diag.maxDeltaNoLifecycle[px3::diag::stageMaster];

    // A lifecycle sample must not be worse than the natural waveform slope.
    const auto lifecycleClean = masterDelta <= masterDeltaClean * 1.05f + 1.0e-6f;
    const auto retiresAtSilence = diag.maxTruncationStep < 0.002f;
    const auto passed = lifecycleClean && retiresAtSilence;

    std::printf("  %-40s %10.6f %10.6f %8.6f %7d %7d %11.6f  %s\n",
                name,
                static_cast<double>(diag.peak[px3::diag::stageMaster]),
                static_cast<double>(masterDelta),
                static_cast<double>(masterDeltaClean),
                diag.releasePrunes,
                diag.noteStarts,
                static_cast<double>(diag.maxTruncationStep),
                passed ? "PASS" : (lifecycleClean ? "FAIL(kill-level)" : "FAIL(discontinuity)"));
    std::fflush(stdout);
    return passed;
}

int runRegressionSuite(bool legacyPruning)
{
    std::printf(legacyPruning
                    ? "\nCONTROL SUITE (pre-fix behaviour: pruning uses instantaneous stopNote(false))\n"
                    : "\nREGRESSION SUITE (production path, all diagnostic modes off)\n");
    std::printf("  %-40s %10s %10s %8s %7s %7s %11s  %s\n",
                "case", "peak", "max|dx|", "no-lifec", "prunes", "notes", "killLevel", "result");

    auto failures = 0;
    auto check = [&failures, legacyPruning](const char* name, const PatchOptions& patch)
    {
        if (!runRegressionCase(name, patch, legacyPruning))
        {
            ++failures;
        }
    };

    PatchOptions base;

    {
        auto p = base; p.attack = 0.002f; p.release = 0.05f;
        check("1 short attack + short release", p);
    }
    {
        auto p = base; p.attack = 0.002f; p.release = 2.5f;
        check("2 short attack + long release", p);
    }
    {
        auto p = base; p.attack = 1.2f; p.release = 2.5f;
        check("3 long attack + long release", p);
    }
    {
        auto p = base; p.pattern = Pattern::rapid;
        check("4 rapid repeated notes", p);
    }
    {
        auto p = base; p.pattern = Pattern::sustained; p.release = 2.0f;
        check("5 polyphonic sustained chord", p);
    }
    {
        auto p = base; p.pattern = Pattern::legatoRuns; p.release = 3.0f;
        check("6 many overlapping release tails", p);
    }
    {
        auto p = base; p.pattern = Pattern::denseChords; p.release = 3.0f;
        check("7 dense chords (heavy tail pruning)", p);
    }
    {
        auto p = base; p.pattern = Pattern::denseChords; p.release = 4.0f; p.attack = 0.001f;
        check("8 amp env stress: 4s release chords", p);
    }
    {
        auto p = base; p.modEnvelopes = true; p.pattern = Pattern::legatoRuns; p.release = 2.0f;
        check("9 ENV1/ENV2/ENV3 modulation", p);
    }
    {
        auto p = base; p.lfoModulation = true; p.pattern = Pattern::legatoRuns; p.release = 2.0f;
        check("10 LFO modulation", p);
    }
    {
        auto p = base; p.pitchModulation = true; p.pattern = Pattern::rapid; p.release = 2.0f;
        check("11 oscillator pitch modulation", p);
    }
    {
        auto p = base; p.fxEnabled = true; p.pattern = Pattern::denseChords; p.release = 2.5f;
        check("12 FX path (delay+reverb+mood)", p);
    }
    {
        auto p = base; p.fxEnabled = false; p.pattern = Pattern::denseChords; p.release = 2.5f;
        check("13 master output, FX bypassed", p);
    }
    {
        auto p = base; p.vibeAmount = 0.8f; p.pattern = Pattern::legatoRuns; p.release = 2.0f;
        check("14 vibe engaged (analog drift path)", p);
    }
    {
        auto p = base;
        p.modEnvelopes = true; p.lfoModulation = true; p.pitchModulation = true;
        p.vibeAmount = 0.6f; p.pattern = Pattern::denseChords; p.release = 3.0f;
        check("15 everything at once", p);
    }

    std::printf("\n  %d failure(s)\n", failures);
    return failures;
}
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto arg = argc > 1 ? juce::String(argv[1]) : juce::String("primary");

    PatchOptions patch;
    if (argc > 2 && juce::String(argv[2]) == "dry")
    {
        patch.vibeEnabled = false;
        patch.fxEnabled = false;
    }

    std::printf("PX3 signal-path isolation harness\n");
    std::printf("  sampleRate=%.0f blockSize=%d\n", kSampleRate, kBlockSize);
    std::printf("  ampAttack=%.4f ampDecay=%.4f ampSustain=%.4f ampRelease=%.4f\n",
                static_cast<double>(patch.attack),
                static_cast<double>(patch.decay),
                static_cast<double>(patch.sustain),
                static_cast<double>(patch.release));
    std::printf("  vibe=%s fx=%s\n", patch.vibeEnabled ? "on" : "off", patch.fxEnabled ? "on" : "off");

    if (arg == "primary")
    {
        runMode({ "A", "baseline: AMP ENV normal, poly gain normal" }, patch);
        runMode({ "B", "AMP ENV multiplication bypassed, poly gain normal",
                  true, false, false, false, false, false }, patch);
        runMode({ "C", "AMP ENV normal, poly gain fixed at 1.0",
                  false, true, false, false, false, false }, patch);
        runMode({ "D", "rawest path: AMP ENV bypassed + poly gain fixed 1.0",
                  true, true, false, false, false, false }, patch);
    }
    else if (arg == "reintroduce")
    {
        // Start from the simplest path and add exactly one mechanism at a time.
        runMode({ "D0", "control: env bypassed, poly fixed, no pruning, no guard, no tail filter, vibe frozen",
                  true, true, true, true, true, true }, patch);
        runMode({ "D1", "D0 + release tail filter",
                  true, true, true, true, false, true }, patch);
        runMode({ "D2", "D1 + vibe release switch",
                  true, true, true, true, false, false }, patch);
        runMode({ "D3", "D2 + onset guard",
                  true, true, true, false, false, false }, patch);
        runMode({ "D4", "D3 + AMP ENV multiplication",
                  false, true, true, false, false, false }, patch);
        runMode({ "D5", "D4 + dynamic poly gain",
                  false, false, true, false, false, false }, patch);
        runMode({ "D6", "D5 + release-voice pruning (== full production path)",
                  false, false, false, false, false, false }, patch);
    }
    else if (arg == "pruning")
    {
        runMode({ "P0", "production path", false, false, false, false, false, false }, patch);
        runMode({ "P1", "production path with release pruning DISABLED",
                  false, false, true, false, false, false }, patch);
    }
    else if (arg == "tail")
    {
        runMode({ "T0", "production path", false, false, false, false, false, false }, patch);
        runMode({ "T1", "production path with release tail filter DISABLED",
                  false, false, false, false, true, false }, patch);
        runMode({ "T2", "production path with vibe release switch FROZEN",
                  false, false, false, false, false, true }, patch);
        runMode({ "T3", "production path, tail filter disabled + vibe switch frozen",
                  false, false, false, false, true, true }, patch);
    }
    else if (arg == "regress")
    {
        return runRegressionSuite(false);
    }
    else if (arg == "regress-legacy")
    {
        runRegressionSuite(true);
        return 0;
    }
    else
    {
        std::printf("usage: PX3Diag [primary|reintroduce|pruning|tail|regress|regress-legacy] [dry]\n");
        return 1;
    }

    return 0;
}
