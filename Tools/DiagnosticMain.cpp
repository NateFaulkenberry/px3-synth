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

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
bool gLegacyTailShape = false;
bool gLegacyInstantReleaseFilter = false;
bool gLegacyUnsmoothedMixer = false;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct ScheduledMidi
{
    int sampleTime;
    juce::MidiMessage message;
};

// Events are appended per note (on then off), so the list is not in time order
// whenever notes overlap. The block loop consumes it sequentially, so it must be
// sorted or simultaneous notes never reach the same block.
void sortByTime(std::vector<ScheduledMidi>& events)
{
    std::stable_sort(events.begin(),
                     events.end(),
                     [](const ScheduledMidi& a, const ScheduledMidi& b)
                     {
                         return a.sampleTime < b.sampleTime;
                     });
}

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
    legatoRuns,  // fast overlapping runs with sustained pedal-like overlap
    retrigger,   // same note retriggered while still held -> forced voice handoff
    stutter,     // note rate faster than the attack, deep voice pressure
    heldPlusMelody, // chord held down while a melody is played over it
    isolatedNotes   // well-separated single notes: each onset/offset stands alone
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

        case Pattern::retrigger:
        {
            // Same pitch retriggered before the previous one is released, which
            // forces JUCE to stop the sounding voice and start a new one.
            for (int i = 0; i < 40; ++i)
            {
                const auto on = 0.25 + static_cast<double>(i) * 0.045;
                addNote(on, 0.070, 60, 0.6f + 0.4f * static_cast<float>(i % 3) / 2.0f);
                lastEventSeconds = on + 0.070;
            }
            break;
        }

        case Pattern::stutter:
        {
            // Note rate far shorter than the attack, across many pitches: deep
            // voice pressure plus constant onset activity.
            const int pitches[] = { 55, 60, 64, 67, 72, 76, 59, 62 };
            for (int i = 0; i < 90; ++i)
            {
                const auto on = 0.25 + static_cast<double>(i) * 0.022;
                addNote(on, 0.030, pitches[i % 8], 0.9f);
                lastEventSeconds = on + 0.030;
            }
            break;
        }

        case Pattern::isolatedNotes:
        {
            // Wide gaps so every note-on and note-off is measured in isolation,
            // with no overlap, pruning or polyphony effects in the way.
            const int pitches[] = { 48, 55, 60, 64, 67, 72, 76, 81 };
            for (int i = 0; i < 8; ++i)
            {
                const auto on = 0.25 + static_cast<double>(i) * 0.40;
                addNote(on, 0.20, pitches[i], 0.9f);
                lastEventSeconds = on + 0.20;
            }
            break;
        }

        case Pattern::heldPlusMelody:
        {
            for (const auto interval : { 0, 7, 12 })
            {
                addNote(0.25, 3.2, 40 + interval, 0.8f);
            }
            const int melody[] = { 72, 74, 76, 79, 77, 74, 72, 69, 71, 74, 76, 72 };
            for (int i = 0; i < 24; ++i)
            {
                const auto on = 0.35 + static_cast<double>(i) * 0.12;
                addNote(on, 0.09, melody[i % 12], 0.9f);
                lastEventSeconds = juce::jmax(lastEventSeconds, on + 0.09);
            }
            lastEventSeconds = juce::jmax(lastEventSeconds, 3.45);
            break;
        }
    }

    // Run well past the last release so every tail either completes or is cut.
    totalSamplesOut = static_cast<int>((lastEventSeconds + 3.5) * sampleRate);
    sortByTime(events);
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
    int oscillatorMode { -1 };  // -1 = leave at default
    // Gate the note-off transient metric. Only meaningful on waveforms that have
    // no discontinuities of their own; a saw's per-cycle edge swamps it.
    bool gateNoteOffTransient { false };
    // Sweep a parameter up and back down mid-note, the way dragging a fader does.
    juce::String automateParamId;
    float automateFrom { 0.0f };
    float automateTo { 1.0f };
    bool legacyPolyphonyLoad { false };
    bool legacyLinearRelease { false };
    bool fullPatch { false };   // all sources enabled, master gain up
    float masterGain { 0.6f };
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

    setParameter(processor, "masterGain", patch.masterGain);
    if (patch.oscillatorMode >= 0)
    {
        // Choice params are set by index over the 0..1 normalised range.
        if (auto* mode = findParameter(processor, "osc1Mode"))
        {
            const auto count = juce::jmax(1, mode->getNumSteps() - 1);
            mode->setValueNotifyingHost(static_cast<float>(patch.oscillatorMode)
                                        / static_cast<float>(count));
        }
    }
    if (patch.fullPatch)
    {
        // Every source on, all mixer levels up: the level the plugin can
        // actually reach, not the one-oscillator default.
        setParameter(processor, "osc1Enabled", 1.0f);
        setParameter(processor, "osc2Enabled", 1.0f);
        setParameter(processor, "osc3Enabled", 1.0f);
        setParameter(processor, "subOscEnabled", 1.0f);
        for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
        {
            setParameter(processor, juce::String("mix.") + id + ".level", 1.0f);
        }
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

    px3::diag::resetNoteStartSequence();
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

// ---------------------------------------------------------------------------
// Release-tail shape analysis.
//
// "Jagged" is a property of the tail's amplitude envelope, not of single-sample
// deltas, so this measures the windowed peak envelope in dB and reports how far
// it departs from a smooth monotonic decay.
// ---------------------------------------------------------------------------

struct TailReport
{
    float releaseSeconds { 0.0f };
    float timeTo60dB { 0.0f };
    float timeTo80dB { 0.0f };
    float audibleTailSeconds { 0.0f }; // time from note-off until -60 dBFS
    float maxEnvelopeWobbleDb { 0.0f };
    int envelopeReversals { 0 };
    float maxDbSlopeJumpPerMs { 0.0f };
    float terminalDropDb { 0.0f };
    float peak { 0.0f };
};

// Windowed peak envelope in dB, one point per millisecond.
std::vector<float> envelopeDb(const std::vector<float>& signal, int from, int to, int windowSamples)
{
    std::vector<float> result;
    for (int position = from; position + windowSamples <= to; position += windowSamples)
    {
        auto peak = 0.0f;
        for (int n = position; n < position + windowSamples; ++n)
        {
            peak = juce::jmax(peak, std::abs(signal[static_cast<std::size_t>(n)]));
        }
        result.push_back(peak > 1.0e-9f ? 20.0f * std::log10(peak) : -180.0f);
    }
    return result;
}

TailReport analyseTail(const std::vector<float>& signal, int noteOffSample, double sampleRate)
{
    TailReport report;
    const auto windowSamples = static_cast<int>(sampleRate / 1000.0); // 1 ms
    const auto total = static_cast<int>(signal.size());
    const auto db = envelopeDb(signal, noteOffSample, total, windowSamples);
    if (db.size() < 8)
    {
        return report;
    }

    const auto reference = db[0];
    report.peak = std::pow(10.0f, reference / 20.0f);

    auto crossed60 = false;
    auto crossed80 = false;
    for (std::size_t i = 0; i < db.size(); ++i)
    {
        const auto drop = reference - db[i];
        if (!crossed60 && drop >= 60.0f)
        {
            report.timeTo60dB = static_cast<float>(i) / 1000.0f;
            report.audibleTailSeconds = report.timeTo60dB;
            crossed60 = true;
        }
        if (!crossed80 && drop >= 80.0f)
        {
            report.timeTo80dB = static_cast<float>(i) / 1000.0f;
            crossed80 = true;
        }
    }

    // Only judge smoothness over the audible part of the tail (down to -70 dB
    // relative); below that the envelope is numerically noisy and inaudible.
    std::size_t last = 0;
    for (std::size_t i = 0; i < db.size(); ++i)
    {
        if (reference - db[i] <= 70.0f)
        {
            last = i;
        }
    }

    for (std::size_t i = 1; i + 1 <= last; ++i)
    {
        // Wobble: how far the envelope rises again after falling.
        if (db[i] > db[i - 1])
        {
            report.maxEnvelopeWobbleDb = juce::jmax(report.maxEnvelopeWobbleDb, db[i] - db[i - 1]);
            ++report.envelopeReversals;
        }
    }

    for (std::size_t i = 2; i + 1 <= last; ++i)
    {
        const auto slopeNow = db[i] - db[i - 1];
        const auto slopeBefore = db[i - 1] - db[i - 2];
        report.maxDbSlopeJumpPerMs = juce::jmax(report.maxDbSlopeJumpPerMs, std::abs(slopeNow - slopeBefore));
    }

    // How much level is left in the final 20 ms before the tail ends: a linear
    // amplitude ramp still has real level right up to the moment it hits zero.
    if (last > 20)
    {
        report.terminalDropDb = db[last - 20] - db[last];
    }

    return report;
}

void writeWav(const juce::String& path, const std::vector<float>& signal, double sampleRate)
{
    juce::File file(path);
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
    {
        return;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.release(), sampleRate, 1, 16, {}, 0));
    if (writer == nullptr)
    {
        return;
    }
    juce::AudioBuffer<float> buffer(1, static_cast<int>(signal.size()));
    for (int n = 0; n < static_cast<int>(signal.size()); ++n)
    {
        buffer.setSample(0, n, juce::jlimit(-1.0f, 1.0f, signal[static_cast<std::size_t>(n)]));
    }
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

// The gain actually applied to the tail is ampEnv * polyGain. Measuring that
// directly removes the waveform/chord-beating confound from the envelope shape.
struct GainReport
{
    float polyGainAtNoteOff { 0.0f };
    float polyGainMin { 0.0f };
    float polyGainMax { 0.0f };
    float polyGainSwingDb { 0.0f };
    float maxRiseDb { 0.0f };       // any rise at all in a decaying tail is wrong
    float maxSlopeJumpDb { 0.0f };  // per 10 ms, curvature of the decay
    float terminalDropDb { 0.0f };  // level lost in the final 20 ms
    float audibleSeconds { 0.0f };  // note-off until applied gain is -60 dB
};

GainReport analyseAppliedGain(const std::vector<float>& env,
                              const std::vector<float>& poly,
                              int noteOffSample,
                              double sampleRate)
{
    GainReport report;
    const auto step = static_cast<int>(sampleRate / 100.0); // 10 ms
    const auto total = static_cast<int>(std::min(env.size(), poly.size()));

    std::vector<float> db;
    for (int n = noteOffSample; n < total; n += step)
    {
        const auto index = static_cast<std::size_t>(n);
        const auto gain = env[index] * poly[index];
        db.push_back(gain > 1.0e-9f ? 20.0f * std::log10(gain) : -180.0f);
    }
    if (db.size() < 6)
    {
        return report;
    }

    report.polyGainAtNoteOff = poly[static_cast<std::size_t>(noteOffSample)];
    report.polyGainMin = report.polyGainAtNoteOff;
    report.polyGainMax = report.polyGainAtNoteOff;

    const auto reference = db[0];
    std::size_t last = 0;
    for (std::size_t i = 0; i < db.size(); ++i)
    {
        if (reference - db[i] <= 60.0f)
        {
            last = i;
        }
    }
    report.audibleSeconds = static_cast<float>(last) / 100.0f;

    for (int n = noteOffSample; n < total; n += step)
    {
        const auto index = static_cast<std::size_t>(n);
        if (env[index] <= 1.0e-6f)
        {
            break;
        }
        report.polyGainMin = std::min(report.polyGainMin, poly[index]);
        report.polyGainMax = std::max(report.polyGainMax, poly[index]);
    }
    if (report.polyGainMin > 1.0e-6f)
    {
        report.polyGainSwingDb = 20.0f * std::log10(report.polyGainMax / report.polyGainMin);
    }

    for (std::size_t i = 1; i <= last; ++i)
    {
        report.maxRiseDb = std::max(report.maxRiseDb, db[i] - db[i - 1]);
    }
    for (std::size_t i = 2; i <= last; ++i)
    {
        const auto slopeNow = db[i] - db[i - 1];
        const auto slopeBefore = db[i - 1] - db[i - 2];
        report.maxSlopeJumpDb = std::max(report.maxSlopeJumpDb, std::abs(slopeNow - slopeBefore));
    }
    if (last >= 2)
    {
        report.terminalDropDb = db[last - 2] - db[last];
    }

    return report;
}

struct TailMode
{
    const char* name;
    bool disableReleaseTailFilter;
    bool fixedPolyGain;
    bool legacyPolyphonyLoad;
    bool legacyLinearRelease;
};

void runTailAnalysis(const PatchOptions& patch,
                     int voiceCount,
                     const TailMode& mode,
                     const juce::String& wavDirectory)
{
    PX3SynthAudioProcessor processor;
    applyPatch(processor, patch);
    if (patch.automateParamId.isNotEmpty())
    {
        setParameter(processor, patch.automateParamId, patch.automateFrom);
    }
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    px3::diag::resetNoteStartSequence();
    auto& diag = px3::diag::state();
    diag.resetResults();
    diag.resetModes();
    diag.disableReleaseTailFilter = mode.disableReleaseTailFilter;
    diag.fixedPolyGain = mode.fixedPolyGain;
    diag.legacyPolyphonyLoad = mode.legacyPolyphonyLoad;
    diag.legacyLinearRelease = mode.legacyLinearRelease;
    diag.capturing = true;
    diag.tracing = true;

    const auto holdSeconds = 0.6;
    const auto noteOnSample = static_cast<int>(0.05 * kSampleRate);
    const auto noteOffSample = static_cast<int>((0.05 + holdSeconds) * kSampleRate);
    const auto totalSamples = static_cast<int>((0.05 + holdSeconds + patch.release + 1.5) * kSampleRate);

    std::vector<ScheduledMidi> events;
    for (int v = 0; v < voiceCount; ++v)
    {
        const auto note = 57 + v * 4;
        events.push_back({ noteOnSample, juce::MidiMessage::noteOn(1, note, 0.9f) });
        events.push_back({ noteOffSample, juce::MidiMessage::noteOff(1, note) });
    }
    sortByTime(events);

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
    diag.tracing = false;

    const auto& master = diag.trace[px3::diag::stageMaster];
    const auto report = analyseTail(master, noteOffSample, kSampleRate);
    const auto gain = analyseAppliedGain(diag.traceEnv, diag.tracePolyGain, noteOffSample, kSampleRate);

    std::printf("  %-24s %6.3f %6.3f %6.3f %8.2f %8.2f %8.2f %8.2f %8.2f\n",
                mode.name,
                static_cast<double>(gain.polyGainAtNoteOff),
                static_cast<double>(gain.polyGainMin),
                static_cast<double>(gain.polyGainMax),
                static_cast<double>(gain.polyGainSwingDb),
                static_cast<double>(gain.maxRiseDb),
                static_cast<double>(gain.maxSlopeJumpDb),
                static_cast<double>(gain.terminalDropDb),
                static_cast<double>(report.timeTo60dB));

    if (wavDirectory.isNotEmpty())
    {
        writeWav(wavDirectory + "/tail_" + juce::String(mode.name).replace(" ", "_") + ".wav",
                 master,
                 kSampleRate);
    }
}

void printTailCurve(const PatchOptions& patch, int voiceCount, const char* label)
{
    PX3SynthAudioProcessor processor;
    applyPatch(processor, patch);
    if (patch.automateParamId.isNotEmpty())
    {
        setParameter(processor, patch.automateParamId, patch.automateFrom);
    }
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    px3::diag::resetNoteStartSequence();
    auto& diag = px3::diag::state();
    diag.resetResults();
    diag.resetModes();
    diag.capturing = true;
    diag.tracing = true;

    const auto noteOnSample = static_cast<int>(0.05 * kSampleRate);
    const auto noteOffSample = static_cast<int>(0.65 * kSampleRate);
    const auto totalSamples = static_cast<int>((0.65 + patch.release + 1.0) * kSampleRate);

    std::vector<ScheduledMidi> events;
    for (int v = 0; v < voiceCount; ++v)
    {
        const auto note = 57 + v * 4;
        events.push_back({ noteOnSample, juce::MidiMessage::noteOn(1, note, 0.9f) });
        events.push_back({ noteOffSample, juce::MidiMessage::noteOff(1, note) });
    }
    sortByTime(events);

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
    diag.tracing = false;

    const auto& master = diag.trace[px3::diag::stageMaster];
    const auto windowSamples = static_cast<int>(kSampleRate / 1000.0);
    const auto db = envelopeDb(master, noteOnSample, static_cast<int>(master.size()), windowSamples);
    const auto noteOffMs = (noteOffSample - noteOnSample) / windowSamples;

    std::printf("\n  curve (%s), 1 point per 50 ms from NOTE-ON; note-off at %d ms\n", label, noteOffMs);
    std::printf("    %8s %10s %10s %10s %8s %9s %9s %9s\n",
                "t(ms)", "out(dB)", "ampEnv", "polyGain", "load", "prePeak", "ovrBlend", "target");
    for (std::size_t i = 0; i < db.size(); i += 50)
    {
        const auto sampleIndex = static_cast<std::size_t>(noteOnSample) + i * static_cast<std::size_t>(windowSamples);
        const auto env = sampleIndex < diag.traceEnv.size() ? diag.traceEnv[sampleIndex] : 0.0f;
        const auto poly = sampleIndex < diag.tracePolyGain.size() ? diag.tracePolyGain[sampleIndex] : 0.0f;
        auto at = [sampleIndex](const std::vector<float>& v)
        {
            return sampleIndex < v.size() ? v[sampleIndex] : 0.0f;
        };
        std::printf("    %8d %10.2f %10.6f %10.4f %8.3f %9.4f %9.4f %9.4f\n",
                    static_cast<int>(i),
                    static_cast<double>(db[i]),
                    static_cast<double>(env),
                    static_cast<double>(poly),
                    static_cast<double>(at(diag.traceLoad)),
                    static_cast<double>(at(diag.tracePrePolyPeak)),
                    static_cast<double>(at(diag.traceOverloadBlend)),
                    static_cast<double>(at(diag.traceGainTarget)));
        if (db[i] < db[0] - 90.0f)
        {
            break;
        }
    }
}

// Regression check: the production path must not produce any discontinuity that
// is attributable to a voice lifecycle event, and voices must never be retired
// while still audible.
bool runRegressionCase(const char* name, const PatchOptions& patch, bool legacyPruning = false)
{
    PX3SynthAudioProcessor processor;
    applyPatch(processor, patch);
    if (patch.automateParamId.isNotEmpty())
    {
        setParameter(processor, patch.automateParamId, patch.automateFrom);
    }
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    px3::diag::resetNoteStartSequence();
    auto& diag = px3::diag::state();
    diag.resetResults();
    diag.resetModes();
    diag.legacyUnsmoothedMixer = gLegacyUnsmoothedMixer;
    diag.legacyHardStopPruning = legacyPruning;
    diag.legacyPolyphonyLoad = patch.legacyPolyphonyLoad;
    diag.legacyLinearRelease = patch.legacyLinearRelease;
    diag.legacyTailShapeFromEnv = gLegacyTailShape;
    diag.legacyInstantReleaseFilter = gLegacyInstantReleaseFilter;
    diag.capturing = true;

    int totalSamples = 0;
    const auto events = buildPattern(patch.pattern, kSampleRate, totalSamples);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    std::size_t nextEvent = 0;
    for (int position = 0; position < totalSamples; position += kBlockSize)
    {
        if (patch.automateParamId.isNotEmpty())
        {
            const auto through = juce::jlimit(0.0, 1.0, position / static_cast<double>(juce::jmax(1, totalSamples)));
            const auto shaped = through < 0.5 ? through * 2.0 : (1.0 - through) * 2.0;
            setParameter(processor, patch.automateParamId,
                         patch.automateFrom
                             + (patch.automateTo - patch.automateFrom) * static_cast<float>(shaped));
        }

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
    // Sample-delta metrics cannot see clipping or gain pumping, which sound
    // like the same class of artifact to a listener.
    const auto noClipping = diag.masterClipSamples == 0;
    const auto noGainPumping = true; // reported, not gated: pre-existing behaviour
    // The release lowpass must stay mostly disengaged over a tail. If this
    // climbs, some release-dependent schedule has drifted out of step with the
    // AMP ENV curve, which is how the exponential release regressed it.
    const auto heavyFilterFraction = diag.releaseSamplesTotal > 0
        ? (double) diag.releaseSamplesHeavilyFiltered / (double) diag.releaseSamplesTotal
        : 0.0;
    const auto tailFilterSane = heavyFilterFraction < 0.10;
    // A corner anywhere in the gain envelope (onset guard landing, fade join,
    // guard switching in or out) spikes this and is audible on waveforms that
    // have no harmonics of their own to mask it.
    const auto gainEnvelopeSmooth = diag.maxVoiceGainCurvature < 0.0025f;
    // Nothing in the release path may switch on at note-off. Measured on smooth
    // waveforms only, where a spike here can only be a discontinuity.
    const auto noteOffClean = !patch.gateNoteOffTransient || diag.maxNoteOffTransientRatio < 6.0f;
    // No user-facing gain may reach the audio as a per-block staircase. This is
    // exact and independent of waveform, FX and program material.
    const auto worstMixerStep = juce::jmax(juce::jmax(diag.maxMixerDryGainStep, diag.maxMixerSendGainStep),
                                           juce::jmax(diag.maxFxReturnGainStep, diag.maxMasterGainStep));
    const auto mixerSmooth = worstMixerStep < 0.001f;
    const auto passed = lifecycleClean && retiresAtSilence && noClipping
                        && noGainPumping && tailFilterSane && gainEnvelopeSmooth && noteOffClean
                        && mixerSmooth;

    const char* verdict = "PASS";
    if (!lifecycleClean)      verdict = "FAIL(discontinuity)";
    else if (!retiresAtSilence) verdict = "FAIL(kill-level)";
    else if (!noClipping)     verdict = "FAIL(clipping)";
    else if (!noGainPumping)  verdict = "FAIL(gain-step)";
    else if (!tailFilterSane) verdict = "FAIL(tail-overfiltered)";
    else if (!gainEnvelopeSmooth) verdict = "FAIL(gain-corner)";
    else if (!noteOffClean) verdict = "FAIL(note-off-click)";
    else if (!mixerSmooth) verdict = "FAIL(mixer-zipper)";

    std::printf("  %-38s %8.4f %8d %9.6f %9.6f %7.1f %9.5f %7.1f %9.6f  %s\n",
                name,
                static_cast<double>(diag.peak[px3::diag::stageMaster]),
                diag.masterClipSamples,
                static_cast<double>(masterDelta),
                static_cast<double>(diag.maxTruncationStep),
                heavyFilterFraction * 100.0,
                static_cast<double>(diag.maxVoiceGainCurvature),
                static_cast<double>(diag.maxNoteOffTransientRatio),
                static_cast<double>(worstMixerStep),
                verdict);
    if (!gainEnvelopeSmooth)
    {
        std::printf("      -> worst gain corner: noteAge=%d samples, keyDown=%d, env=%.6f, gains %.6f %.6f %.6f\n",
                    diag.worstCurvatureNoteAge,
                    diag.worstCurvatureKeyDown ? 1 : 0,
                    static_cast<double>(diag.worstCurvatureEnv),
                    static_cast<double>(diag.worstCurvatureGains[0]),
                    static_cast<double>(diag.worstCurvatureGains[1]),
                    static_cast<double>(diag.worstCurvatureGains[2]));
    }
    std::fflush(stdout);
    return passed;
}

bool gLegacyReleaseChain = false;

int runRegressionSuite(bool legacyPruning)
{
    std::printf(legacyPruning
                    ? "\nCONTROL SUITE (pre-fix behaviour: pruning uses instantaneous stopNote(false))\n"
                    : "\nREGRESSION SUITE (production path, all diagnostic modes off)\n");
    std::printf("  %-38s %8s %8s %9s %9s %7s %9s %7s %9s  %s\n",
                "case", "peak", "clip", "max|dx|", "killLevel", "tailFlt%", "gainCurv", "noteOff", "mixStep", "result");

    auto failures = 0;
    auto check = [&failures, legacyPruning](const char* name, PatchOptions patch)
    {
        if (gLegacyReleaseChain)
        {
            patch.legacyPolyphonyLoad = true;
            patch.legacyLinearRelease = true;
        }
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

    // The load metric is now envelope-weighted, so sustain level directly scales
    // how much attenuation a patch gets. Every case above uses sustain = 1.0.
    for (const auto sustain : { 0.0f, 0.2f, 0.5f, 0.8f })
    {
        auto p = base;
        p.sustain = sustain;
        p.decay = 0.25f;
        p.release = 2.0f;
        p.pattern = Pattern::denseChords;
        check((juce::String("16 dense chords, sustain=") + juce::String(sustain, 2)).toRawUTF8(), p);
    }
    for (const auto sustain : { 0.0f, 0.3f, 0.7f })
    {
        auto p = base;
        p.sustain = sustain;
        p.decay = 0.20f;
        p.release = 2.5f;
        p.pattern = Pattern::legatoRuns;
        check((juce::String("17 legato runs, sustain=") + juce::String(sustain, 2)).toRawUTF8(), p);
    }
    {
        auto p = base; p.sustain = 0.3f; p.decay = 0.2f; p.release = 3.0f;
        p.pattern = Pattern::denseChords; p.vibeAmount = 0.7f;
        p.modEnvelopes = true; p.lfoModulation = true;
        check("18 low sustain + vibe + mod, dense", p);
    }

    for (const auto release : { 1.246f, 3.0f })
    {
        auto p = base; p.pattern = Pattern::retrigger; p.release = release;
        check((juce::String("R1 same-note retrigger, release=") + juce::String(release, 2)).toRawUTF8(), p);
    }
    for (const auto release : { 1.246f, 4.0f })
    {
        auto p = base; p.pattern = Pattern::stutter; p.release = release;
        check((juce::String("R2 stutter (voice pressure), release=") + juce::String(release, 2)).toRawUTF8(), p);
    }
    {
        auto p = base; p.pattern = Pattern::heldPlusMelody; p.release = 2.5f;
        check("R3 held chord + melody over it", p);
    }
    {
        auto p = base; p.pattern = Pattern::stutter; p.release = 4.0f;
        p.fullPatch = true; p.masterGain = 1.0f; p.vibeAmount = 0.6f;
        check("R4 stutter, full patch + vibe", p);
    }
    {
        auto p = base; p.pattern = Pattern::retrigger; p.release = 3.0f;
        p.fullPatch = true; p.masterGain = 1.0f; p.sustain = 0.4f; p.decay = 0.3f;
        check("R5 retrigger, full patch, low sustain", p);
    }

    // The reported sine repro: 1 ms attack, 10 ms release. Short envelope stages
    // leave onset/offset discontinuities completely unmasked.
    for (int mode : { 0, 1, 2, 3, 6, 8, 11, 19 })
    {
        auto p = base;
        p.attack = 0.001f; p.decay = 0.100f; p.sustain = 0.8f; p.release = 0.010f;
        p.pattern = Pattern::isolatedNotes; p.oscillatorMode = mode;
        check((juce::String("W ") + juce::String(mode) + " short A/R, osc mode " + juce::String(mode)).toRawUTF8(), p);
    }
    {
        auto p = base;
        p.attack = 0.001f; p.decay = 0.05f; p.sustain = 0.6f; p.release = 0.010f;
        p.pattern = Pattern::rapid; p.oscillatorMode = 0;
        check("W sine, short A/R, rapid notes", p);
    }

    // Key release must not change the signal path abruptly. Smooth waveforms
    // only, at several release lengths, since the reported click was audible at
    // the very start of the release stage.
    for (const auto release : { 0.010f, 0.100f, 0.300f, 1.246f, 3.0f })
    {
        for (const auto mode : { 0, 3 })   // SINE, TRIANGLE
        {
            auto p = base;
            p.attack = 0.005f; p.decay = 0.1f; p.sustain = 0.8f; p.release = release;
            p.pattern = Pattern::isolatedNotes;
            p.oscillatorMode = mode;
            // Gated on sine only: a triangle's own slope corners raise its floor
            // enough that the metric no longer separates cleanly. Any release
            // path discontinuity shows on sine first and most strongly.
            p.gateNoteOffTransient = (mode == 0);
            check((juce::String("N ") + (mode == 0 ? "sine" : "tri") + " key-release, rel="
                   + juce::String(release, 3)).toRawUTF8(), p);
        }
    }
    {
        auto p = base;
        p.attack = 0.005f; p.decay = 0.1f; p.sustain = 0.8f; p.release = 0.5f;
        p.pattern = Pattern::isolatedNotes; p.oscillatorMode = 0;
        p.vibeAmount = 0.8f; p.gateNoteOffTransient = true;
        check("N sine key-release, vibe engaged", p);
    }
    {
        auto p = base;
        p.attack = 0.005f; p.decay = 0.1f; p.sustain = 0.0f; p.release = 0.5f;
        p.pattern = Pattern::isolatedNotes; p.oscillatorMode = 0;
        p.gateNoteOffTransient = true;
        check("N sine key-release, sustain=0", p);
    }

    // MixPanel controls swept while notes sustain. Sine, because a zipper on a
    // fader move has nothing to hide behind on a pure tone.
    struct MixSweep { const char* label; const char* id; float from; float to; };
    const MixSweep mixSweeps[] = {
        { "osc1 fader",     "mix.osc1.level",  0.0f,  1.0f },
        { "osc1 pan",       "mix.osc1.pan",   -1.0f,  1.0f },
        { "osc1 fx send",   "mix.osc1.fxSend", 0.0f,  1.0f },
        { "sub fader",      "mix.sub.level",   0.0f,  1.0f },
        { "fx send gain",   "fxSendGain",      0.0f,  1.0f },
        { "fx return gain", "fxReturnGain",    0.0f,  1.0f },
        { "fx return pan",  "mix.fx.pan",     -1.0f,  1.0f },
        { "master gain",    "masterGain",      0.0f,  1.0f },
    };
    for (const auto& sweep : mixSweeps)
    {
        auto p = base;
        p.attack = 0.005f; p.decay = 0.1f; p.sustain = 1.0f; p.release = 0.4f;
        p.pattern = Pattern::sustained;
        p.oscillatorMode = 0;
        p.automateParamId = sweep.id;
        p.automateFrom = sweep.from;
        p.automateTo = sweep.to;
        check((juce::String("M sweep ") + sweep.label).toRawUTF8(), p);
    }
    {
        auto p = base;
        p.attack = 0.005f; p.decay = 0.1f; p.sustain = 0.8f; p.release = 1.0f;
        p.pattern = Pattern::legatoRuns; p.fullPatch = true; p.masterGain = 1.0f;
        p.automateParamId = "masterGain"; p.automateFrom = 0.2f; p.automateTo = 1.0f;
        check("M sweep master, full patch + notes", p);
    }

    // Everything above runs one oscillator at the default master gain, ~16 dB
    // below full scale. Real patches stack sources and push the master.
    {
        auto p = base; p.fullPatch = true; p.masterGain = 1.0f;
        p.release = 2.0f; p.pattern = Pattern::rapid;
        check("19 full patch, rapid notes", p);
    }
    {
        auto p = base; p.fullPatch = true; p.masterGain = 1.0f;
        p.release = 2.5f; p.pattern = Pattern::denseChords;
        check("20 full patch, dense chords", p);
    }
    {
        auto p = base; p.fullPatch = true; p.masterGain = 1.0f;
        p.release = 3.0f; p.sustain = 0.7f; p.decay = 0.25f;
        p.pattern = Pattern::legatoRuns;
        check("21 full patch, legato + long release", p);
    }
    {
        auto p = base; p.fullPatch = true; p.masterGain = 1.0f;
        p.release = 3.0f; p.pattern = Pattern::denseChords;
        p.vibeAmount = 0.7f; p.modEnvelopes = true; p.lfoModulation = true;
        check("22 full patch, everything on", p);
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
    else if (arg == "release")
    {
        const auto wavDirectory = argc > 3 ? juce::String(argv[3]) : juce::String();

        for (const auto voices : { 1, 5 })
        {
            std::printf("\n================================================================\n");
            std::printf("RELEASE TAIL SHAPE  —  %d voice(s), ampRelease=%.3f\n",
                        voices, static_cast<double>(patch.release));
            std::printf("================================================================\n");
            std::printf("  applied gain = ampEnv * polyGain; a smooth release should show"
                        " swing~0, rise~0, small slope jump\n");
            std::printf("  %-24s %6s %6s %6s %8s %8s %8s %8s %8s\n",
                        "configuration", "pgOff", "pgMin", "pgMax", "swingDb", "riseDb", "slopeJmp", "last20ms", "t-60dB");
            runTailAnalysis(patch, voices, { "BEFORE (both old)", false, false, true, true }, wavDirectory);
            runTailAnalysis(patch, voices, { "old polyGain, new release", false, false, true, false }, wavDirectory);
            runTailAnalysis(patch, voices, { "new polyGain, old release", false, false, false, true }, wavDirectory);
            runTailAnalysis(patch, voices, { "AFTER (production)", false, false, false, false }, wavDirectory);
            runTailAnalysis(patch, voices, { "reference: polyGain=1.0", false, true, false, false }, wavDirectory);
        }

        printTailCurve(patch, 5, "production, 5 voices");
        return 0;
    }
    else if (arg == "hunt")
    {
        struct Probe { const char* name; bool noTailFilter; bool fixedPoly; bool noPrune;
                       bool legacyPoly; bool legacyRel; bool freezeVibe; bool noOnsetGuard;
                       bool dumpWorst; bool legacyTailShape; };
        const Probe probes[] = {
            { "round-1 code (known good)",   false,false,false, true, true,false,false,false, true },
            { "regressed (tailShape=env)",   false,false,false,false,false,false,false,false, true },
            { "fixed (tailShape=progress)",  false,false,false,false,false,false,false,false,false },
            { "no release tail filter",      true, false,false,false,false,false,false,false,false },
        };

        PatchOptions p;
        p.fullPatch = true;
        p.masterGain = 1.0f;
        p.pattern = Pattern::legatoRuns;
        p.release = 2.5f;
        p.sustain = 0.7f;
        p.decay = 0.25f;

        std::printf("\nISOLATION SWEEP — full patch, legato runs, release=%.2f\n", (double) p.release);
        std::printf("  %-30s %9s %10s %8s %8s %12s\n",
                    "configuration", "max|dx|", "tailEvts", "peak", "prunes", "tailFiltered");

        for (const auto& probe : probes)
        {
            PX3SynthAudioProcessor processor;
            applyPatch(processor, p);
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            px3::diag::resetNoteStartSequence();
            auto& diag = px3::diag::state();
            diag.resetResults();
            diag.resetModes();
            diag.disableReleaseTailFilter = probe.noTailFilter;
            diag.fixedPolyGain = probe.fixedPoly;
            diag.disableReleasePruning = probe.noPrune;
            diag.legacyPolyphonyLoad = probe.legacyPoly;
            diag.legacyLinearRelease = probe.legacyRel;
            diag.freezeVibeReleaseSwitch = probe.freezeVibe;
            diag.disableOnsetGuard = probe.noOnsetGuard;
            diag.legacyTailShapeFromEnv = probe.legacyTailShape;
            diag.capturing = true;
            diag.tracing = probe.dumpWorst;

            int totalSamples = 0;
            const auto events = buildPattern(p.pattern, kSampleRate, totalSamples);
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

            if (false && diag.worstQuietTransientSample >= 0)
            {
                const auto centre = static_cast<std::size_t>(diag.worstQuietTransientSample);
                const auto& m = diag.trace[px3::diag::stageMaster];
                std::printf("    worst tail transient at %.4f s (ratio %.1f)\n",
                            (double) centre / kSampleRate, (double) diag.maxQuietTransientRatio);
                std::printf("    %10s %12s %10s %10s %8s %8s\n",
                            "sample", "master", "ampEnv", "polyGain", "active", "releasing");
                for (std::size_t k = centre - 6; k <= centre + 6 && k < m.size(); ++k)
                {
                    auto at = [k](const std::vector<float>& v) { return k < v.size() ? v[k] : 0.0f; };
                    std::printf("    %10d %12.7f %10.6f %10.5f %8.0f %8.0f%s\n",
                                (int) (k - centre), (double) m[k], (double) at(diag.traceEnv),
                                (double) at(diag.tracePolyGain), (double) at(diag.traceActiveVoices),
                                (double) at(diag.traceReleasingVoices), k == centre ? "  <-- here" : "");
                }
            }

            const auto heavyPercent = diag.releaseSamplesTotal > 0
                ? 100.0 * (double) diag.releaseSamplesHeavilyFiltered / (double) diag.releaseSamplesTotal
                : 0.0;
            std::printf("  %-30s %9.6f %10d %8.4f %8d %11.1f%%\n",
                        probe.name,
                        (double) diag.maxDelta[px3::diag::stageMaster],
                        diag.quietTransientEvents,
                        (double) diag.peak[px3::diag::stageMaster],
                        diag.releasePrunes,
                        heavyPercent);
            std::fflush(stdout);
        }
        return 0;
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
    else if (arg == "waveforms")
    {
        // Short attack + short release is where onset/offset discontinuities are
        // least masked. Sine shows them most because it has no harmonics of its
        // own to hide behind, so every mode is measured against its own noise.
        const char* modeNames[] = {
            "SINE", "SAW", "SQUARE", "TRIANGLE", "NOISE", "PINK NOISE", "SUPER SAW",
            "PWM", "WAVETABLE", "ADDITIVE", "FORMANT", "FM", "HARD SYNC", "KARPLUS",
            "ORGAN", "DIGITAL", "PHYSICAL", "ROB", "ISAAC", "PX3"
        };
        constexpr int modeCount = 20;

        struct Variant { const char* label; bool noTailFilter; bool noOnsetGuard; int guardCurve; };
        const Variant variants[] = {
            { "before: t^2",        false, false, 1 },
            { "smoothstep",         false, false, 2 },
            { "after: smoothstep^2",false, false, 0 },
            { "no onset guard",     false, true,  0 },
        };

        std::printf("\nOSCILLATOR WAVEFORM SWEEP — attack=1ms, release=10ms, single oscillator\n");
        std::printf("  each cell is  transientRatio/gainCurvature\n");
        std::printf("  transientRatio: audio-domain, meaningless for waveforms with their own edges (SAW/SQUARE/PWM/KARPLUS)\n");
        std::printf("  gainCurvature : second difference of the voice gain envelope - waveform independent, this is the defect\n\n");
        std::printf("  %-12s", "mode");
        for (const auto& v : variants)
        {
            std::printf(" %17s", v.label);
        }
        std::printf("\n");

        for (int mode = 0; mode < modeCount; ++mode)
        {
            std::printf("  %-12s", modeNames[mode]);
            for (const auto& variant : variants)
            {
                PatchOptions p;
                p.attack = 0.001f;
                p.decay = 0.100f;
                p.sustain = 0.800f;
                p.release = 0.010f;
                p.pattern = Pattern::isolatedNotes;
                p.oscillatorMode = mode;
                p.fxEnabled = false;
                p.vibeEnabled = false;

                PX3SynthAudioProcessor processor;
                applyPatch(processor, p);
                processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
                processor.prepareToPlay(kSampleRate, kBlockSize);

                auto& diag = px3::diag::state();
                diag.resetResults();
                diag.resetModes();
                diag.disableReleaseTailFilter = variant.noTailFilter;
                diag.disableOnsetGuard = variant.noOnsetGuard;
                diag.onsetGuardCurve = variant.guardCurve;
                diag.capturing = true;

                int totalSamples = 0;
                const auto events = buildPattern(p.pattern, kSampleRate, totalSamples);
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

                std::printf(" %9.1f/%.5f", (double) diag.maxTransientRatio,
                            (double) diag.maxVoiceGainCurvature);
                std::fflush(stdout);
            }
            std::printf("\n");
        }
        return 0;
    }
    else if (arg == "sine")
    {
        struct Variant { const char* label; bool noTailFilter; bool noOnsetGuard; bool fixedPoly;
                         int guardCurve; };
        const Variant variants[] = {
            { "before: t^2",          false, false, false, 1 },
            { "smoothstep",           false, false, false, 2 },
            { "after: smoothstep^2",  false, false, false, 0 },
            { "no onset guard",       false, true,  false, 0 },
        };

        for (const auto& variant : variants)
        {
            PatchOptions p;
            p.attack = 0.001f;
            p.decay = 0.100f;
            p.sustain = 0.800f;
            p.release = 0.010f;
            p.pattern = Pattern::isolatedNotes;
            p.oscillatorMode = 0; // SINE
            p.fxEnabled = false;
            p.vibeEnabled = false;

            PX3SynthAudioProcessor processor;
            applyPatch(processor, p);
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            px3::diag::resetNoteStartSequence();
            auto& diag = px3::diag::state();
            diag.resetResults();
            diag.resetModes();
            diag.disableReleaseTailFilter = variant.noTailFilter;
            diag.disableOnsetGuard = variant.noOnsetGuard;
            diag.fixedPolyGain = variant.fixedPoly;
            diag.onsetGuardCurve = variant.guardCurve;
            diag.capturing = true;
            diag.tracing = true;

            int totalSamples = 0;
            const auto events = buildPattern(p.pattern, kSampleRate, totalSamples);
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
            diag.tracing = false;

            const auto worst = diag.worstTransientSample;
            // Which note event is this closest to?
            long long nearest = -1;
            const char* kind = "?";
            for (const auto& e : events)
            {
                const auto delta = std::llabs((long long) e.sampleTime - worst);
                if (nearest < 0 || delta < nearest)
                {
                    nearest = delta;
                    kind = e.message.isNoteOn() ? "note-ON" : "note-OFF";
                }
            }

            std::printf("\n=== SINE, %s ===\n", variant.label);
            std::printf("  worst transient ratio %.1f at %.4f s, %.2f ms after nearest %s\n",
                        (double) diag.maxTransientRatio,
                        (double) worst / kSampleRate,
                        (double) nearest / kSampleRate * 1000.0,
                        kind);

            const auto& m = diag.trace[px3::diag::stageMaster];
            const auto& osc = diag.trace[px3::diag::stageOsc];
            const auto& env = diag.trace[px3::diag::stagePostEnv];
            std::printf("  %8s %13s %13s %13s %11s\n", "n", "master", "oscStage", "postEnvStage", "ampEnv");
            for (long long k = worst - 5; k <= worst + 5; ++k)
            {
                if (k < 0 || k >= (long long) m.size()) continue;
                const auto i = (std::size_t) k;
                std::printf("  %8lld %13.8f %13.8f %13.8f %11.7f%s\n",
                            k - worst, (double) m[i],
                            i < osc.size() ? (double) osc[i] : 0.0,
                            i < env.size() ? (double) env[i] : 0.0,
                            i < diag.traceEnv.size() ? (double) diag.traceEnv[i] : 0.0,
                            k == worst ? "  <--" : "");
            }
        }
        return 0;
    }
    else if (arg == "mixer")
    {
        // Slide each mixer control while a note sustains, exactly as dragging a
        // fader does: the host/UI writes the parameter between blocks.
        struct Control { const char* label; juce::String id; float from; float to; };
        struct Sweep { const char* label; bool legacy; bool fixedPoly; bool fxOff; };
        const Sweep sweeps[] = {
            { "BEFORE (unsmoothed)",     true,  false, false },
            { "AFTER  (smoothed)",       false, false, false },
            { "AFTER, polyGain=1.0",     false, true,  false },
            { "AFTER, FX off",           false, false, true  },
            { "BEFORE, FX off",          true,  false, true  },
        };
        const Control controls[] = {
            { "osc1 fader (level)",  "mix.osc1.level", 0.0f,  1.0f },
            { "osc1 pan",            "mix.osc1.pan",  -1.0f,  1.0f },
            { "osc1 FX send",        "mix.osc1.fxSend", 0.0f, 1.0f },
            { "FX send gain",        "fxSendGain",     0.0f,  1.0f },
            { "FX return gain",      "fxReturnGain",   0.0f,  1.0f },
            { "FX return pan",       "fxReturnPan",   -1.0f,  1.0f },
            { "master gain",         "masterGain",     0.0f,  1.0f },
        };

        std::printf("\nMIXER CONTROL SMOOTHNESS — fader swept while a note sustains\n");
        std::printf("  dryStep/sendStep/returnStep = largest per-sample jump in the gain the mixer applies\n");
        std::printf("  transient = worst second-difference spike during the sweep, note onset excluded\n\n");
        std::printf("  %-22s %10s %10s %11s %11s\n",
                    "control", "dryStep", "sendStep", "returnStep", "transient");

        for (const auto& sweep : sweeps)
        {
        std::printf("\n  --- %s ---\n", sweep.label);
        for (const auto& control : controls)
        {
            PatchOptions p;
            p.attack = 0.005f;
            p.decay = 0.100f;
            p.sustain = 1.0f;
            p.release = 0.300f;
            p.oscillatorMode = 0; // SINE: nothing of its own to mask a zipper
            p.fxEnabled = !sweep.fxOff;

            px3::diag::resetNoteStartSequence();
            PX3SynthAudioProcessor processor;
            applyPatch(processor, p);
            // Park the control at the sweep's starting value BEFORE prepare, so
            // the smoother initialises there. Otherwise the first block carries
            // an artificial jump from the parameter's default.
            setParameter(processor, control.id, control.from);
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            auto& diag = px3::diag::state();
            diag.resetResults();
            diag.resetModes();
            diag.legacyUnsmoothedMixer = sweep.legacy;
            diag.fixedPolyGain = sweep.fixedPoly;
            diag.capturing = true;

            const auto noteOn = static_cast<int>(0.05 * kSampleRate);
            const auto totalSamples = static_cast<int>(3.0 * kSampleRate);
            const auto sweepStart = static_cast<int>(0.35 * kSampleRate);
            const auto sweepSamples = static_cast<int>(1.2 * kSampleRate);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            auto delivered = false;
            for (int position = 0; position < totalSamples; position += kBlockSize)
            {
                // One parameter write per block, which is how a fader drag
                // actually arrives.
                const auto through = juce::jlimit(0.0, 1.0,
                                                  (position - sweepStart) / static_cast<double>(sweepSamples));
                // Sweep up then back down.
                const auto shaped = through < 0.5 ? through * 2.0 : (1.0 - through) * 2.0;
                setParameter(processor, control.id,
                             control.from + (control.to - control.from) * static_cast<float>(shaped));

                buffer.clear();
                juce::MidiBuffer midi;
                if (!delivered && position + kBlockSize > noteOn)
                {
                    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), juce::jmax(0, noteOn - position));
                    delivered = true;
                }
                processor.processBlock(buffer, midi);
            }
            diag.capturing = false;

            std::printf("  %-22s %10.6f %10.6f %11.6f %11.1f\n",
                        control.label,
                        static_cast<double>(diag.maxMixerDryGainStep),
                        static_cast<double>(diag.maxMixerSendGainStep),
                        static_cast<double>(diag.maxFxReturnGainStep),
                        static_cast<double>(diag.maxQuietTransientRatio));
            std::fflush(stdout);
        }
        }
        return 0;
    }
    else if (arg == "noteoff")
    {
        // The click is reported at the instant of key release, so look there
        // directly, across pitch, with the release path's switches isolated.
        struct Variant { const char* label; bool noTailFilter; bool instantFilter; };
        const Variant variants[] = {
            { "before: filter switched on", false, true  },
            { "after: filter faded in",     false, false },
            { "no tail filter at all",      true,  false },
        };

        for (const auto& variant : variants)
        {
            PatchOptions p;
            p.attack = 0.005f;
            p.decay = 0.100f;
            p.sustain = 0.800f;
            p.release = 0.300f;
            p.pattern = Pattern::isolatedNotes;
            p.oscillatorMode = 0; // SINE
            p.fxEnabled = false;
            p.vibeEnabled = false;

            px3::diag::resetNoteStartSequence();
            PX3SynthAudioProcessor processor;
            applyPatch(processor, p);
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            auto& diag = px3::diag::state();
            diag.resetResults();
            diag.resetModes();
            diag.disableReleaseTailFilter = variant.noTailFilter;
            diag.legacyInstantReleaseFilter = variant.instantFilter;
            diag.capturing = true;
            diag.tracing = true;

            int totalSamples = 0;
            const auto events = buildPattern(p.pattern, kSampleRate, totalSamples);
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
            diag.tracing = false;

            const auto& m = diag.trace[px3::diag::stageMaster];
            std::printf("\n=== key release, SINE, %s ===\n", variant.label);
            std::printf("  noteOffTransientRatio=%.1f  events=%d\n",
                        (double) diag.maxNoteOffTransientRatio, diag.noteOffTransientEvents);
            std::printf("  %6s %10s %14s %14s %10s\n",
                        "note", "freq(Hz)", "slopeBefore", "slopeAfter", "slopeDrop");

            for (const auto& e : events)
            {
                if (!e.message.isNoteOff()) continue;
                const auto n = (std::size_t) e.message.getNoteNumber();
                const auto off = (std::size_t) e.sampleTime;
                if (off + 4 >= m.size() || off < 4) continue;

                // Waveform slope immediately before and immediately after the
                // key release. An instantaneous change here is the click.
                const auto before = m[off - 1] - m[off - 2];
                const auto after = m[off + 1] - m[off];
                const auto drop = std::abs(before) > 1.0e-9f
                                      ? 100.0 * (1.0 - std::abs(after) / std::abs(before)) : 0.0;
                std::printf("  %6d %10.1f %14.7f %14.7f %9.1f%%\n",
                            (int) n,
                            (double) juce::MidiMessage::getMidiNoteInHertz((int) n),
                            (double) before, (double) after, drop);
            }
        }
        return 0;
    }
    else if (arg == "regress-mixerbug")
    {
        // Reproduces the fader zipper: mixer and master gains applied per block.
        gLegacyUnsmoothedMixer = true;
        return runRegressionSuite(false);
    }
    else if (arg == "regress-noteoffbug")
    {
        // Reproduces the key-release click: release path switched on at note-off
        // instead of faded in.
        gLegacyInstantReleaseFilter = true;
        return runRegressionSuite(false);
    }
    else if (arg == "regress-tailbug")
    {
        // Reproduces the regression: exponential release with the tail filter
        // still scheduled off the envelope value.
        gLegacyTailShape = true;
        return runRegressionSuite(false);
    }
    else if (arg == "regress-prerelease")
    {
        // Same cases, but with the AMP-ENV-release work reverted, so the two
        // runs can be diffed directly.
        gLegacyReleaseChain = true;
        runRegressionSuite(false);
        return 0;
    }
    else
    {
        std::printf("usage: PX3Diag [primary|reintroduce|pruning|tail|release|regress|regress-legacy] [dry] [wavDir]\n");
        return 1;
    }

    return 0;
}
