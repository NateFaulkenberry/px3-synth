// CPU benchmark harness.
//
// Built in the SHIPPING configuration (PX3_DIAGNOSTICS=0) so it measures the
// code the plugin actually runs: the diagnostic taps in PX3Diag add per-sample
// work to the same loops being measured, which would distort every number here.
//
// Each scenario renders the real processor headlessly and times processBlock
// with a monotonic clock. The measured window is deliberately steady state -
// notes are started and allowed to reach sustain before timing begins - so the
// numbers describe a sustained condition rather than an onset transient.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UpdateService.h"
#include "MockUpdateProvider.h"
#include "OscillatorMode.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace
{
// Overridable so one binary can report the sample-rate / block-size matrix a
// release audit needs. Compile-time constants meant every figure came from a
// single 48 kHz / 512 configuration, which hides the fixed per-block overhead
// that dominates at small buffers.
const double kSampleRate = []
{
    const auto v = juce::SystemStats::getEnvironmentVariable("PX3_BENCH_RATE", "48000").getDoubleValue();
    return v > 0.0 ? v : 48000.0;
}();

const int kBlockSize = []
{
    const auto v = juce::SystemStats::getEnvironmentVariable("PX3_BENCH_BLOCK", "512").getIntValue();
    return v > 0 ? v : 512;
}();

// Enough blocks that scheduler noise averages out, repeated so a single
// unlucky sweep cannot set the reported figure.
constexpr int kMeasuredBlocks = 400;
constexpr int kSweeps = 5;
constexpr int kWarmupBlocks = 120;

juce::RangedAudioParameter* findParameter(juce::AudioProcessor& processor, const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            if (withId->paramID == id)
            {
                return withId;
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

void setChoiceIndex(juce::AudioProcessor& processor, const juce::String& id, int index)
{
    if (auto* parameter = findParameter(processor, id))
    {
        const auto steps = juce::jmax(1, parameter->getNumSteps() - 1);
        parameter->setValueNotifyingHost(static_cast<float>(index) / static_cast<float>(steps));
    }
}

// One benchmark configuration. Everything a scenario can vary lives here so a
// scenario is a data row, not a code path.
struct Scenario
{
    const char* name { "" };
    int voices { 0 };
    bool longRelease { false };
    bool allSources { false };   // sub + osc1..3 rather than the default single osc
    bool filters { false };
    bool filterSweep { false };  // cutoff automated every block
    bool modEnvelopes { false };
    bool lfos { false };
    bool vibe { false };
    bool fx { false };
    bool newFx { false };            // doom + lucy + chorus + stereo spread
    bool analog { false };           // the console engine, all four contexts
    bool mixerAutomation { false }; // level/pan/send automated every block
    bool voiceStealing { false };   // retrigger faster than voices can retire
    bool rapidTrigger { false };    // constant note-on/note-off during measurement
    int oscMode { -1 };
    // Bus inserts. Declared after oscMode so the scenario table's positional
    // rows keep meaning what they meant.
    bool eqDry { false };
    bool compDry { false };
    bool eqFx { false };
    bool compFx { false };
    bool insertsBypassed { false };  // parameters loaded, enables off
};

struct Timing
{
    double meanMicros { 0.0 };
    double medianMicros { 0.0 };
    double p99Micros { 0.0 };
    double maxMicros { 0.0 };
    double meanPercent { 0.0 };
    double maxPercent { 0.0 };
    int deadlineMisses { 0 };  // measured blocks that took longer than their audio
};

void configure(PX3SynthAudioProcessor& processor, const Scenario& scenario)
{
    setParameter(processor, "voice.amp.attack", 0.005f);
    setParameter(processor, "voice.amp.decay", 0.100f);
    setParameter(processor, "voice.amp.sustain", 1.0f);
    setParameter(processor, "voice.amp.release", scenario.longRelease ? 6.0f : 0.200f);
    setParameter(processor, "voice.amp.enabled", 1.0f);
    setParameter(processor, "mix.master.level", 0.6f);

    setParameter(processor, "voice.osc1.enabled", 1.0f);
    setParameter(processor, "voice.osc2.enabled", scenario.allSources ? 1.0f : 0.0f);
    setParameter(processor, "voice.osc3.enabled", scenario.allSources ? 1.0f : 0.0f);
    setParameter(processor, "voice.sub.enabled", scenario.allSources ? 1.0f : 0.0f);

    // Bus inserts. Every band is given a non-identity setting so the EQ
    // measurement is the cost of four running biquads and not the cost of the
    // identity skip - which would flatter it.
    {
        auto loadInserts = [&](const char* bus, bool eq, bool comp)
        {
            const auto b = "mix." + juce::String(bus) + ".insert.";
            const auto on = ! scenario.insertsBypassed;
            setParameter(processor, b + "eq.enabled", (eq && on) ? 1.0f : 0.0f);
            for (int band = 1; band <= 4; ++band)
            {
                const auto n = juce::String(band);
                setParameter(processor, b + "eq.gain." + n, band % 2 == 0 ? 4.0f : -3.0f);
                setParameter(processor, b + "eq.q." + n, 1.2f);
            }
            setParameter(processor, b + "comp.enabled", (comp && on) ? 1.0f : 0.0f);
            setParameter(processor, b + "comp.input", 18.0f);
            setParameter(processor, b + "comp.mix", 1.0f);
        };

        const auto anyDry = scenario.eqDry || scenario.compDry;
        const auto anyFx = scenario.eqFx || scenario.compFx;
        if (anyDry || scenario.insertsBypassed) loadInserts("dry", scenario.eqDry, scenario.compDry);
        if (anyFx || scenario.insertsBypassed) loadInserts("fx", scenario.eqFx, scenario.compFx);
    }

    if (scenario.oscMode >= 0)
    {
        for (const auto* id : { "voice.osc1.mode", "voice.osc2.mode", "voice.osc3.mode" })
        {
            setChoiceIndex(processor, id, scenario.oscMode);
        }
    }

    for (const auto* slot : { "1", "2" })
    {
        setParameter(processor, juce::String("voice.filter") + slot + ".enabled", scenario.filters ? 1.0f : 0.0f);
    }
    setParameter(processor, "voice.filter1.cutoff", 1200.0f);
    setParameter(processor, "voice.filter2.cutoff", 3000.0f);

    setParameter(processor, "fx.vibe.enabled", scenario.vibe ? 1.0f : 0.0f);
    setParameter(processor, "fx.vibe.amount", scenario.vibe ? 0.85f : 0.0f);

    setParameter(processor, "fx.delay.enabled", scenario.fx ? 1.0f : 0.0f);
    setParameter(processor, "fx.reverb.enabled", scenario.fx ? 1.0f : 0.0f);
    setParameter(processor, "fx.mood.enabled", scenario.fx ? 1.0f : 0.0f);
    if (scenario.fx)
    {
        setParameter(processor, "fx.delay.amount", 0.5f);
        setParameter(processor, "fx.reverb.amount", 0.5f);
        setParameter(processor, "fx.mood.mix", 0.4f);
        setParameter(processor, "mix.send.fx.level", 0.8f);
    }

    // The newer effects, driven separately. They cost nothing while inaudible -
    // that is deliberate, and it is exactly why they have to be measured with a
    // non-zero amount rather than left at their defaults, where the benchmark
    // would report a cost of zero for four whole engines.
    setParameter(processor, "fx.doom.enabled", scenario.newFx ? 1.0f : 0.0f);
    setParameter(processor, "fx.lucy.enabled", scenario.newFx ? 1.0f : 0.0f);
    setParameter(processor, "fx.chorus.enabled", scenario.newFx ? 1.0f : 0.0f);
    setParameter(processor, "fx.spread.enabled", scenario.newFx ? 1.0f : 0.0f);
    setParameter(processor, "fx.doom.mix", scenario.newFx ? 0.4f : 0.0f);
    setParameter(processor, "fx.lucy.global", scenario.newFx ? 0.5f : 0.0f);
    setParameter(processor, "fx.chorus.amount", scenario.newFx ? 0.6f : 0.0f);
    setParameter(processor, "fx.spread.amount", scenario.newFx ? 0.6f : 0.0f);

    // AnalogEngine runs at four channel stages plus three bus stages, so it is
    // measured with the rest of the console rather than as an effect.
    setParameter(processor, "global.character.enabled", scenario.analog ? 1.0f : 0.0f);
    setParameter(processor, "global.character.profile", scenario.analog ? 0.25f : 0.0f);

    for (int envIndex = 0; envIndex < 3; ++envIndex)
    {
        const auto slot = juce::String(envIndex + 1);
        setParameter(processor, juce::String("mod.env") + slot + ".enabled", scenario.modEnvelopes ? 1.0f : 0.0f);
        setParameter(processor,
                     envIndex == 0 ? juce::String("mod.env1.amount") : juce::String("mod.env") + slot + ".amount",
                     scenario.modEnvelopes ? 0.7f : 0.0f);
        setParameter(processor, juce::String("mod.env") + slot + ".attack", 0.02f + 0.05f * static_cast<float>(envIndex));
        setParameter(processor, juce::String("mod.env") + slot + ".decay", 0.25f);
        setParameter(processor, juce::String("mod.env") + slot + ".sustain", 0.5f);
        setParameter(processor, juce::String("mod.env") + slot + ".release", 0.8f);
    }
    if (scenario.modEnvelopes)
    {
        processor.setEnvelopeAssignmentByParameterId(0, "voice.filter1.cutoff", false);
        processor.setEnvelopeAssignmentByParameterId(1, "voice.osc1.macro.a", false);
        processor.setEnvelopeAssignmentByParameterId(2, "mix.osc1.level", false);
    }

    for (int lfoIndex = 0; lfoIndex < 3; ++lfoIndex)
    {
        const auto slot = juce::String(lfoIndex + 1);
        const auto prefix = juce::String("mod.lfo") + slot + ".";
        setParameter(processor, lfoIndex == 0 ? juce::String("mod.lfo1.enabled") : prefix + "enabled",
                     scenario.lfos ? 1.0f : 0.0f);
        setParameter(processor, lfoIndex == 0 ? juce::String("mod.lfo1.amount") : prefix + "amount",
                     scenario.lfos ? 0.6f : 0.0f);
        setParameter(processor, lfoIndex == 0 ? juce::String("mod.lfo1.frequency") : prefix + "frequency",
                     2.0f + static_cast<float>(lfoIndex));
    }
    if (scenario.lfos)
    {
        processor.setLfoAssignmentByParameterId(0, "voice.filter1.cutoff", false);
        processor.setLfoAssignmentByParameterId(1, "mix.osc1.level", false);
        processor.setLfoAssignmentByParameterId(2, "voice.osc1.pitch.mod", false);
    }
}

// Notes are spread across the keyboard so no two voices share a pitch, which
// would make juce::Synthesiser retarget an existing voice instead of starting
// a new one and quietly reduce the polyphony being measured.
int pitchForVoice(int index)
{
    return 24 + (index % 72);
}

Timing measure(const Scenario& scenario,
               const std::function<void(PX3SynthAudioProcessor&)>& extraSetup = {},
               int measuredBlocks = kMeasuredBlocks,
               int sweeps = kSweeps)
{
    PX3SynthAudioProcessor processor;
    configure(processor, scenario);
    if (extraSetup) { extraSetup(processor); }
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);

    // Warm-up. Notes start here so the measured window sees voices already at
    // sustain, and so first-block lazy work is not attributed to the scenario.
    for (int block = 0; block < kWarmupBlocks; ++block)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        for (int voice = 0; voice < scenario.voices; ++voice)
        {
            if (block == 4 + voice / 8)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, pitchForVoice(voice), 0.9f),
                              (voice % 8) * (kBlockSize / 8));
            }
        }
        processor.processBlock(buffer, midi);
    }

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(measuredBlocks * sweeps));

    int rollingNote = 0;
    for (int sweep = 0; sweep < sweeps; ++sweep)
    {
        for (int block = 0; block < measuredBlocks; ++block)
        {
            buffer.clear();
            juce::MidiBuffer midi;

            if (scenario.rapidTrigger || scenario.voiceStealing)
            {
                // A note-off and a fresh note-on every block: continuous voice
                // allocation, and with voiceStealing the note rate exceeds the
                // rate at which released voices retire.
                const auto stride = scenario.voiceStealing ? 1 : 3;
                if (block % stride == 0)
                {
                    midi.addEvent(juce::MidiMessage::noteOff(1, pitchForVoice(rollingNote)), 0);
                    ++rollingNote;
                    midi.addEvent(juce::MidiMessage::noteOn(1, pitchForVoice(rollingNote), 0.9f),
                                  kBlockSize / 2);
                }
            }

            // Parameter automation is applied outside the timed region: the
            // host does this on its own thread, and including the parameter
            // store would measure JUCE rather than the DSP.
            if (scenario.filterSweep)
            {
                setParameter(processor, "voice.filter1.cutoff",
                             400.0f + 6000.0f * static_cast<float>(block % 32) / 32.0f);
                setParameter(processor, "voice.filter2.cutoff",
                             800.0f + 9000.0f * static_cast<float>((block + 11) % 32) / 32.0f);
            }
            if (scenario.mixerAutomation)
            {
                const auto phase = static_cast<float>(block % 40) / 40.0f;
                for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                {
                    setParameter(processor, juce::String("mix.") + id + ".level", 0.35f + 0.45f * phase);
                    setParameter(processor, juce::String("mix.") + id + ".pan", -0.8f + 1.6f * phase);
                    setParameter(processor, juce::String("mix.") + id + ".send.fx", 0.2f + 0.6f * phase);
                }
            }

            const auto start = std::chrono::steady_clock::now();
            processor.processBlock(buffer, midi);
            const auto end = std::chrono::steady_clock::now();

            samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
        }
    }

    std::sort(samples.begin(), samples.end());

    Timing timing;
    double total = 0.0;
    for (const auto value : samples)
    {
        total += value;
    }
    timing.meanMicros = total / static_cast<double>(samples.size());
    timing.medianMicros = samples[samples.size() / 2];
    timing.p99Micros = samples[static_cast<std::size_t>(static_cast<double>(samples.size()) * 0.99)];
    timing.maxMicros = samples.back();

    const auto blockPeriodMicros = 1.0e6 * static_cast<double>(kBlockSize) / kSampleRate;
    timing.meanPercent = 100.0 * timing.meanMicros / blockPeriodMicros;
    timing.maxPercent = 100.0 * timing.maxMicros / blockPeriodMicros;
    timing.deadlineMisses = static_cast<int>(samples.end()
        - std::upper_bound(samples.begin(), samples.end(), blockPeriodMicros));
    return timing;
}

const Scenario kScenarios[] = {
    { "idle (no voices)",              0, false , false , false , false , false , false , false , false , false , false , false , false , false , -1 },
    { "1 voice",                       1, false , false , false , false , false , false , false , false , false , false , false , false , false , -1 },
    { "4 voices",                      4, false , false , false , false , false , false , false , false , false , false , false , false , false , -1 },
    { "8 voices",                      8, false , false , false , false , false , false , false , false , false , false , false , false , false , -1 },
    { "16 voices (typical)",           16, false , false , false , false , false , false , false , false , false , false , false , false , false , -1 },
    { "64 voices (max)",               64, false , false , false , false , false , false , false , false , false , false , false , false , false , -1 },
    { "64 voices + long release",      64, true  , false , false , false , false , false , false , false , false , false , false , false , false , -1 },
    { "16 voices, all 4 sources",      16, false , true  , false , false , false , false , false , false , false , false , false , false , false , -1 },
    { "16 voices + filters",           16, false , true  , true  , false , false , false , false , false , false , false , false , false , false , -1 },
    { "16 voices + filter sweep",      16, false , true  , true  , true  , false , false , false , false , false , false , false , false , false , -1 },
    { "16 voices + mod envelopes",     16, false , true  , false , false , true  , false , false , false , false , false , false , false , false , -1 },
    { "16 voices + LFOs",              16, false , true  , false , false , false , true  , false , false , false , false , false , false , false , -1 },
    { "16 voices + vibe",              16, false , true  , false , false , false , false , true  , false , false , false , false , false , false , -1 },
    { "16 voices + FX chain",          16, false , true  , false , false , false , false , false , true  , true  , true  , false , false , false , -1 },
    { "16 voices + mixer automation",  16, false , true  , false , false , false , false , false , false , false , false , true  , false , false , -1 },
    { "16 voices, EVERYTHING on",      16, false , true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , false , false , -1 },
    { "64 voices, EVERYTHING on",      64, true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , false , false , -1 },
    { "56 voices, EVERYTHING on",      56, true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , false , false , -1 },
    { "48 voices, EVERYTHING on",      48, true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , false , false , -1 },
    { "40 voices, EVERYTHING on",      40, true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , false , false , -1 },
    { "32 voices, EVERYTHING on",      32, true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , false , false , -1 },
    { "rapid triggering",              16, false , true  , true  , false , false , false , false , true  , true  , true  , false , false , true  , -1 },
    { "voice stealing stress",         64, true  , true  , true  , false , false , false , false , true  , true  , true  , false , true  , false , -1 },
    { "16 voices, SINE",               16, false , true  , true  , false , false , false , false , false , false , false , false , false , false , 0 },
    { "16 voices, SUPERSAW",           16, false , true  , true  , false , false , false , false , false , false , false , false , false , false , 6 },
    { "16 voices + new FX only",       16, false , true  , false , false , false , false , false , false , true  , true  , false , false , false , -1 },
    { "16 voices + analog only",       16, false , true  , false , false , false , false , false , false , false , true  , false , false , false , -1 },
    { "16 voices, PX3",                16, false , true  , true  , false , false , false , false , false , false , false , false , false , false , 18 },
    // WAVETABLE against the modes it has to live beside. SINE is the floor and
    // SUPERSAW the ceiling among the cheap modes, so the interesting number is
    // where the table lookup lands between them - and whether it scales with
    // polyphony any differently, since it is the only mode that reads memory
    // rather than computing a waveform.
    { "1 voice, WAVETABLE",             1, false , true  , true  , false , false , false , false , false , false , false , false , false , false , 8 },
    { "8 voices, WAVETABLE",            8, false , true  , true  , false , false , false , false , false , false , false , false , false , false , 8 },
    { "16 voices, WAVETABLE",          16, false , true  , true  , false , false , false , false , false , false , false , false , false , false , 8 },
    { "32 voices, WAVETABLE",          32, true  , true  , true  , false , false , false , false , false , false , false , false , false , false , 8 },
    { "64 voices, WAVETABLE",          64, true  , true  , true  , false , false , false , false , false , false , false , false , false , false , 8 },
    { "16 voices, WAVETABLE + LFO scan", 16, false, true  , true  , false , false , false , true  , false , false , false , false , false , false , 8 },

    // ---- bus inserts (v0.3.1) ------------------------------------------------
    // Measured against "16 voices, all 4 sources" - the row directly above the
    // group is the baseline these are read against.
    { "16 voices + inserts bypassed",  16, false , true  , false , false , false , false , false , false , false , false , false , false , false , -1,
      false, false, false, false, true },
    { "16 voices + dry EQ",            16, false , true  , false , false , false , false , false , false , false , false , false , false , false , -1,
      true,  false, false, false, false },
    { "16 voices + dry COMP",          16, false , true  , false , false , false , false , false , false , false , false , false , false , false , -1,
      false, true,  false, false, false },
    { "16 voices + dry EQ + COMP",     16, false , true  , false , false , false , false , false , false , false , false , false , false , false , -1,
      true,  true,  false, false, false },
    // The FX rows carry a send and an FX chain, so they need their own
    // baseline: reading them against the dry rows would charge the inserts for
    // the delay.
    { "16 voices + FX send, no inserts", 16, false , true  , false , false , false , false , false , true  , false , false , false , false , false , -1 },
    { "16 voices + FX EQ + COMP",      16, false , true  , false , false , false , false , false , true  , false , false , false , false , false , -1,
      false, false, true,  true,  false },
    { "16 voices + both buses",        16, false , true  , false , false , false , false , false , true  , false , false , false , false , false , -1,
      true,  true,  true,  true,  false },
    { "64 voices + both buses",        64, true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , true  , false , false , -1,
      true,  true,  true,  true,  false },
};

// ---------------------------------------------------------------------------
// Audio fingerprint
//
// An optimization is only acceptable if the audio is unchanged, and "sounds the
// same" is not a measurement. Every fingerprint config renders the real
// processor over a fixed MIDI pattern and reduces the output to a bitwise
// checksum over the raw float bits, so a single altered sample is visible.
// RMS and peak are printed alongside so a mismatch can be judged for magnitude
// rather than only detected.
// ---------------------------------------------------------------------------

struct Fingerprint
{
    juce::uint64 checksum { 0 };
    double rms { 0.0 };
    double peak { 0.0 };
};

void hashSample(juce::uint64& checksum, float value)
{
    juce::uint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    checksum ^= static_cast<juce::uint64>(bits);
    checksum *= 1099511628211ull; // FNV-1a prime
}

// Some voice state is seeded from juce::Random::getSystemRandom(), which JUCE
// deliberately refuses to reseed (Random::setSeed early-returns for the system
// instance). Those configs therefore cannot be compared bitwise at all - not a
// limitation of this harness, a property of the synth. They are compared
// statistically instead: repeated renders give a mean and spread for RMS and
// peak, and an optimization must land inside the baseline's own spread.
//
// Everything else is bitwise deterministic and compared exactly.
bool isRandomSeeded(const Scenario& scenario)
{
    // SUPERSAW / PHYSICAL consume the shared Random in resetForNote;
    // Delay and Mood consume it while processing.
    return scenario.oscMode == 6 || scenario.oscMode == 15
           || scenario.fx;
}

Fingerprint fingerprint(const Scenario& scenario)
{
    PX3SynthAudioProcessor processor;
    configure(processor, scenario);
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);

    Fingerprint result;
    result.checksum = 14695981039346656037ull; // FNV-1a offset basis
    double energy = 0.0;
    juce::int64 total = 0;

    constexpr int kBlocks = 260; // ~2.8 s: onset, sustain, note-off and tail
    const auto voices = juce::jmax(1, scenario.voices);

    for (int block = 0; block < kBlocks; ++block)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (scenario.voiceStealing || scenario.rapidTrigger)
        {
            // Continuous allocation and retirement, so note reuse and stealing
            // are part of what the checksum covers.
            const auto stride = scenario.voiceStealing ? 1 : 3;
            if (block % stride == 0 && block < 200)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, pitchForVoice(block / stride)), 0);
                midi.addEvent(juce::MidiMessage::noteOn(1, pitchForVoice(block / stride + 1), 0.9f),
                              kBlockSize / 2);
            }
        }
        else
        {
            for (int voice = 0; voice < voices; ++voice)
            {
                if (block == 2 + voice / 4)
                {
                    midi.addEvent(juce::MidiMessage::noteOn(1, pitchForVoice(voice), 0.9f),
                                  (voice % 4) * (kBlockSize / 4));
                }
                if (block == 120 + voice / 4)
                {
                    midi.addEvent(juce::MidiMessage::noteOff(1, pitchForVoice(voice)),
                                  (voice % 4) * (kBlockSize / 4));
                }
            }
        }

        if (scenario.filterSweep)
        {
            setParameter(processor, "voice.filter1.cutoff",
                         400.0f + 6000.0f * static_cast<float>(block % 32) / 32.0f);
        }
        if (scenario.mixerAutomation)
        {
            const auto phase = static_cast<float>(block % 40) / 40.0f;
            for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
            {
                setParameter(processor, juce::String("mix.") + id + ".level", 0.35f + 0.45f * phase);
                setParameter(processor, juce::String("mix.") + id + ".pan", -0.8f + 1.6f * phase);
                setParameter(processor, juce::String("mix.") + id + ".send.fx", 0.2f + 0.6f * phase);
            }
        }

        processor.processBlock(buffer, midi);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer(channel);
            for (int i = 0; i < kBlockSize; ++i)
            {
                hashSample(result.checksum, data[i]);
                const auto value = static_cast<double>(data[i]);
                energy += value * value;
                result.peak = juce::jmax(result.peak, std::abs(value));
                ++total;
            }
        }
    }

    result.rms = std::sqrt(energy / static_cast<double>(juce::jmax<juce::int64>(1, total)));
    return result;
}

// Every oscillator mode, every filter mode, and each modulation/FX subsystem in
// isolation, so a fingerprint mismatch localises to a subsystem by itself.
int runFingerprints()
{
    std::printf("\nPX3 AUDIO FINGERPRINT\n");
    std::printf("  bitwise checksum of rendered output; any change to the signal changes it\n\n");
    std::printf("  %-34s %20s %14s %12s\n", "config", "checksum", "rms", "peak");
    std::printf("  %-34s %20s %14s %12s\n",
                "----------------------------------", "--------------------",
                "--------------", "------------");

    static constexpr const char* kModeNames[] = {
        "SINE", "SAW", "SQUARE", "TRIANGLE", "NOISE", "PINK", "SUPERSAW", "PWM",
        "WAVETABLE", "ADDITIVE", "FORMANT", "FM", "HARDSYNC", "KARPLUS", "ORGAN",
        "DIGITAL", "PHYSICAL", "ROB", "ISAAC", "PX3"
    };

    auto report = [](const juce::String& label, const Scenario& scenario)
    {
        if (isRandomSeeded(scenario))
        {
            // Mean and spread over repeated renders. The spread is what an
            // optimization has to stay inside; a real signal change moves the
            // mean well beyond the run-to-run variation.
            constexpr int kRepeats = 8;
            double rmsSum = 0.0, rmsSquares = 0.0, peakSum = 0.0;
            for (int repeat = 0; repeat < kRepeats; ++repeat)
            {
                const auto print = fingerprint(scenario);
                rmsSum += print.rms;
                rmsSquares += print.rms * print.rms;
                peakSum += print.peak;
            }
            const auto meanRms = rmsSum / kRepeats;
            const auto variance = juce::jmax(0.0, rmsSquares / kRepeats - meanRms * meanRms);
            std::printf("  %-34s %20s %14.9f %12.8f   RANDOM-SEEDED sd=%.9f\n",
                        label.toRawUTF8(),
                        "(not reproducible)",
                        meanRms,
                        peakSum / kRepeats,
                        std::sqrt(variance));
            std::fflush(stdout);
            return;
        }

        const auto print = fingerprint(scenario);
        std::printf("  %-34s %20llu %14.9f %12.8f\n",
                    label.toRawUTF8(),
                    static_cast<unsigned long long>(print.checksum),
                    print.rms,
                    print.peak);
        std::fflush(stdout);
    };

    for (int mode = 0; mode < 20; ++mode)
    {
        Scenario scenario { "", 4, false, true, false, false, false, false, false,
                            false, false, false, false, false, false, mode };
        report(juce::String("osc mode ") + kModeNames[mode], scenario);
    }

    struct Variant
    {
        const char* label;
        Scenario scenario;
    };

    const Variant variants[] = {
        { "1 voice, default",       { "", 1, false, false, false, false, false, false, false, false, false, false, false, false, false, -1 } },
        { "8 voices, default",      { "", 8, false, false, false, false, false, false, false, false, false, false, false, false, false, -1 } },
        { "32 voices, all sources", { "", 32, false, true, false, false, false, false, false, false, false, false, false, false, false, -1 } },
        { "long release tails",     { "", 16, true, true, false, false, false, false, false, false, false, false, false, false, false, -1 } },
        { "filters active",         { "", 8, false, true, true, false, false, false, false, false, false, false, false, false, false, -1 } },
        { "filter cutoff sweep",    { "", 8, false, true, true, true, false, false, false, false, false, false, false, false, false, -1 } },
        { "mod envelopes",          { "", 8, false, true, false, false, true, false, false, false, false, false, false, false, false, -1 } },
        { "LFOs",                   { "", 8, false, true, false, false, false, true, false, false, false, false, false, false, false, -1 } },
        { "vibe",                   { "", 8, false, true, false, false, false, false, true, false, false, false, false, false, false, -1 } },
        { "FX chain",               { "", 8, false, true, false, false, false, false, false, true, true, true, false, false, false, -1 } },
        { "mixer automation",       { "", 8, false, true, false, false, false, false, false, false, false, false, true, false, false, -1 } },
        { "everything on",          { "", 16, true, true, true, true, true, true, true, true, true, true, true, false, false, -1 } },
        { "voice stealing",         { "", 64, true, true, true, false, false, false, false, true, true, true, false, true, false, -1 } },
        // FX-free twins of the two combined-path configs above, so the same
        // routing is still covered by an exact bitwise comparison.
        { "everything on, no FX",   { "", 16, true, true, true, true, true, true, true, false, false, false, true, false, false, -1 } },
        { "voice stealing, no FX",  { "", 64, true, true, true, false, false, false, false, false, false, false, false, true, false, -1 } },
        { "rapid retrigger, no FX", { "", 16, false, true, true, false, false, false, false, false, false, false, false, false, true, -1 } },
    };

    for (const auto& variant : variants)
    {
        report(variant.label, variant.scenario);
    }

    std::printf("\n");
    return 0;
}

// The oscillator matrix: every mode, one oscillator or all four sources, at 1,
// 16 and 64 voices. Driven only through parameters, so the same code measures
// the engine before and after a DSP change. Run once per sample rate with
// PX3_BENCH_RATE.
int runOscillatorMatrix()
{
    const auto modes = px3::oscillatorModeChoices();
    const auto budget = 1.0e6 * static_cast<double>(kBlockSize) / kSampleRate;
    constexpr int blocks = 120;
    constexpr int passes = 3;

    std::printf("\nPX3 OSCILLATOR CPU MATRIX\n");
    std::printf("  %.0f Hz, %d-sample blocks: median microseconds per block (%% of the real-time budget)\n\n",
                kSampleRate, kBlockSize);
    std::printf("  %-12s %18s %18s %18s %18s %18s\n", "mode", "1 osc x1", "1 osc x16", "1 osc x64",
                "3 osc+sub x16", "3 osc+sub x64");

    for (int mode = 0; mode < modes.size(); ++mode)
    {
        std::printf("  %-12s", modes[mode].toRawUTF8());
        for (const auto& config : { std::pair<int, bool> { 1, false }, { 16, false }, { 64, false }, { 16, true }, { 64, true } })
        {
            Scenario scenario {};
            scenario.name = "";
            scenario.voices = config.first;
            scenario.allSources = config.second;
            scenario.oscMode = mode;
            const auto timing = measure(scenario, {}, blocks, passes);
            std::printf(" %9.1f (%5.2f%%)", timing.medianMicros, 100.0 * timing.medianMicros / budget);
            std::fflush(stdout);
        }
        std::printf("\n");
    }

    std::printf("  %-12s", "SUB only");
    for (const auto voices : { 1, 16, 64 })
    {
        Scenario scenario {};
        scenario.name = "";
        scenario.voices = voices;
        scenario.oscMode = 0;
        const auto timing = measure(scenario, [](PX3SynthAudioProcessor& processor)
        {
            setParameter(processor, "voice.osc1.enabled", 0.0f);
            setParameter(processor, "voice.sub.enabled", 1.0f);
        }, blocks, passes);
        std::printf(" %9.1f (%5.2f%%)", timing.medianMicros, 100.0 * timing.medianMicros / budget);
        std::fflush(stdout);
    }
    std::printf("\n\n");
    return 0;
}
}

// tools/NativeUiBenchmark.cpp
int runNativeUiBenchmark();

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String filter;
    if (argc > 1)
    {
        filter = argv[1];
    }

    if (filter == "filters")
    {
        std::printf("\nPX3 FILTER CPU MATRIX  %.0f Hz, %d samples\n", kSampleRate, kBlockSize);
        const auto choices = px3::filterModeChoices();
        for (const auto mode : { px3::FilterMode::lp12, px3::FilterMode::lp24, px3::FilterMode::stateVariable12,
                                px3::FilterMode::stateVariable24, px3::FilterMode::ladder12, px3::FilterMode::ladder24 })
        {
            for (const auto voices : { 1, 16, 64 })
            {
                Scenario scenario {};
                scenario.name = "filter matrix";
                scenario.voices = voices;
                scenario.filters = true;
                const auto timing = measure(scenario, [mode](PX3SynthAudioProcessor& processor)
                {
                    for (int index = 0; index < kFilterInstanceCount; ++index)
                    {
                        auto& type = processor.getFilterTypeParam(index);
                        type.setValueNotifyingHost(type.convertTo0to1(static_cast<float>(mode)));
                    }
                }, 120, 3);
                std::printf("  %-10s %2d voices: median %8.1f us, p99 %8.1f us\n",
                            choices[static_cast<int>(mode)].toRawUTF8(), voices, timing.medianMicros, timing.p99Micros);
            }
        }
        return 0;
    }

    if (filter == "drive")
    {
        // DRIVE runs 8x oversampled ADAA in double at 44.1/48 kHz; it is a
        // stereo FX-chain stage, so its cost does not scale with voices.
        const auto budget = 1.0e6 * static_cast<double>(kBlockSize) / kSampleRate;
        std::printf("\nPX3 DRIVE CPU  %.0f Hz, %d samples (median us per block, %% of budget)\n", kSampleRate, kBlockSize);
        for (const auto voices : { 1, 16 })
        {
            for (const auto on : { false, true })
            {
                Scenario scenario {};
                scenario.name = "drive";
                scenario.voices = voices;
                const auto timing = measure(scenario, [on](PX3SynthAudioProcessor& processor)
                {
                    setParameter(processor, "fx.distortion.enabled", on ? 1.0f : 0.0f);
                    setParameter(processor, "fx.distortion.mix", on ? 1.0f : 0.0f);
                    setParameter(processor, "fx.distortion.drive", 0.7f);
                }, 200, 3);
                std::printf("  %2d voices, DRIVE %-3s  median %8.1f us (%5.2f%%), p99 %8.1f us\n", voices, on ? "on" : "off",
                            timing.medianMicros, 100.0 * timing.medianMicros / budget, timing.p99Micros);
            }
        }
        return 0;
    }

    if (filter == "osc")
    {
        return runOscillatorMatrix();
    }

    if (filter == "fingerprint")
    {
        return runFingerprints();
    }

    if (filter == "uinative")
    {
        px3::update::installDefaultConfiguration();
        px3::update::UpdateService::getInstance().setProvider(std::make_unique<px3::update::MockUpdateProvider>());
        return runNativeUiBenchmark();
    }

    if (filter == "ui")
    {
        px3::update::installDefaultConfiguration();
        px3::update::UpdateService::getInstance().setProvider(std::make_unique<px3::update::MockUpdateProvider>());
        // The editor paints into an offscreen image here rather than a window,
        // so this measures the plugin's own paint work without a compositor in
        // the way. Full repaints are what the 30 Hz timer triggers whenever a
        // key is down, so that is the case worth timing.
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        const auto createStart = std::chrono::steady_clock::now();
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        const auto createEnd = std::chrono::steady_clock::now();
        if (editor == nullptr)
        {
            std::printf("  editor could not be created\n");
            return 1;
        }
        const auto requestedWidth = juce::SystemStats::getEnvironmentVariable("PX3_BENCH_UI_WIDTH", "0").getIntValue();
        const auto requestedHeight = juce::SystemStats::getEnvironmentVariable("PX3_BENCH_UI_HEIGHT", "0").getIntValue();
        if (requestedWidth > 0 && requestedHeight > 0) { editor->setSize(requestedWidth, requestedHeight); }

        const auto width = juce::jmax(64, editor->getWidth());
        const auto height = juce::jmax(64, editor->getHeight());
        std::printf("\nPX3 UI BENCHMARK\n");
        std::printf("  editor %d x %d\n", width, height);
        std::printf("  editor construction: %.2f ms\n",
                    std::chrono::duration<double, std::milli>(createEnd - createStart).count());

        juce::Image target(juce::Image::ARGB, width, height, true);

        auto timePaints = [&editor, &target](const char* label, int repeats, juce::Rectangle<int> clip)
        {
            std::vector<double> samples;
            samples.reserve(static_cast<std::size_t>(repeats));
            for (int i = 0; i < repeats; ++i)
            {
                juce::Graphics g(target);
                if (! clip.isEmpty())
                {
                    g.reduceClipRegion(clip);
                }
                const auto start = std::chrono::steady_clock::now();
                editor->paintEntireComponent(g, false);
                const auto end = std::chrono::steady_clock::now();
                samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
            }
            std::sort(samples.begin(), samples.end());
            double total = 0.0;
            for (const auto v : samples) total += v;
            std::printf("  %-38s mean %7.3f ms   median %7.3f ms   max %7.3f ms\n",
                        label, total / static_cast<double>(samples.size()),
                        samples[samples.size() / 2], samples.back());
            std::fflush(stdout);
            return total / static_cast<double>(samples.size());
        };

        // Warm-up: first paints build fonts, glyph caches and image caches.
        timePaints("warm-up (discarded)", 12, {});
        const auto full = timePaints("full editor repaint", 60, {});
        // The exact region the logo animation now invalidates: a 150 x 104
        // panel at (24, 24), expanded by the 8 px animation margin.
        const auto logo = timePaints("logo-panel repaint (animation region)", 60,
                                     juce::Rectangle<int>(16, 16, 166, 120));
        if (auto* synthEditor = dynamic_cast<PX3SynthAudioProcessorEditor*>(editor.get()))
        {
            const auto timeFrames = [&](const char* label, bool automation)
            {
                std::vector<double> samples;
                for (int frame = 0; frame < 60; ++frame)
                {
                    const auto start = std::chrono::steady_clock::now();
                    if (automation) { setParameter(processor, "voice.filter1.cutoff", frame % 2 == 0 ? 400.0f : 12000.0f); }
                    synthEditor->debugTimerTick();
                    juce::Graphics graphics(target);
                    editor->paintEntireComponent(graphics, false);
                    samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                }
                std::sort(samples.begin(), samples.end());
                std::printf("  %-38s median %7.3f ms   p99 %7.3f ms\n", label, samples[samples.size() / 2], samples.back());
            };
            timeFrames("timer + idle animation repaint", false);
            juce::AudioBuffer<float> audio(2, kBlockSize);
            audio.clear();
            juce::MidiBuffer note;
            note.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
            processor.processBlock(audio, note);
            timeFrames("timer + live note + automation repaint", true);
        }

        std::printf("\n  full repaint at 30 Hz costs %.1f%% of one core\n", full * 3.0);
        std::printf("  a logo-area repaint instead would cost %.1f%%\n", logo * 3.0);
        return 0;
    }

    if (filter == "selftest")
    {
        // The fingerprint is only evidence if it repeats. Voice start draws from
        // the shared system Random, so this checks that seeding actually pins
        // the sequence rather than assuming it does.
        const Scenario superSaw { "", 4, false, true, false, false, false, false, false, false, false, false, false, false, false, 6 };
        const Scenario physical { "", 4, false, true, false, false, false, false, false, false, false, false, false, false, false, 15 };
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            const auto a = fingerprint(superSaw);
            const auto b = fingerprint(physical);
            std::printf("  repeat %d: SUPERSAW=%llu PHYSICAL=%llu\n",
                        repeat,
                        static_cast<unsigned long long>(a.checksum),
                        static_cast<unsigned long long>(b.checksum));
        }
        return 0;
    }

    std::printf("\nPX3 CPU BENCHMARK\n");
    std::printf("  %.0f Hz, %d-sample blocks (%.2f ms real-time budget per block)\n",
                kSampleRate, kBlockSize, 1000.0 * kBlockSize / kSampleRate);
    std::printf("  %d blocks x %d sweeps per scenario, shipping build (PX3_DIAGNOSTICS=0)\n\n",
                kMeasuredBlocks, kSweeps);

    std::printf("  %-32s %9s %9s %9s %9s %8s %8s %6s\n",
                "scenario", "mean us", "med us", "p99 us", "max us", "mean %", "max %", "miss");
    std::printf("  %-32s %9s %9s %9s %9s %8s %8s %6s\n",
                "--------------------------------", "---------", "---------",
                "---------", "---------", "--------", "--------", "------");

    for (const auto& scenario : kScenarios)
    {
        if (filter.isNotEmpty() && ! juce::String(scenario.name).containsIgnoreCase(filter))
        {
            continue;
        }

        const auto timing = measure(scenario);
        std::printf("  %-32s %9.1f %9.1f %9.1f %9.1f %7.2f%% %7.2f%% %6d\n",
                    scenario.name,
                    timing.meanMicros,
                    timing.medianMicros,
                    timing.p99Micros,
                    timing.maxMicros,
                    timing.meanPercent,
                    timing.maxPercent,
                    timing.deadlineMisses);
        std::fflush(stdout);
    }

    std::printf("\n");
    return 0;
}
