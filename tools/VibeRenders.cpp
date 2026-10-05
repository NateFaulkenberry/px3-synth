// PX3Diag vibe-renders <dir>
//
// Offline sonic-validation set for VIBE (the Uni-Vibe) and ANALOG (the per-voice
// drift). Writes WAVs to listen to and prints, per render, the numbers that say
// whether it behaves like the pedal rather than a generic phaser:
//
//   rise    share of the sweep cycle spent rising (phase track at 1 kHz);
//           0.5 is a symmetric (sine-driven) sweep, the lamp gives less
//   notch   lowest / highest frequency of the deepest notch over the cycle
//   step    largest sample-to-sample step of the output against the dry
//           input's own (a zipper or click shows as a ratio well above 1)
//   level   RMS of the output against the dry input, dB
//
// The sources are synthesised here, deterministically: a plucked "guitar"
// (Karplus-Strong), a strummed chord, drums (kick sweep, noise snare, hats), a
// pad, a bass line and a mix of all of them. ANALOG lives inside the Synth's
// voices, so its renders play MIDI through the real processor.

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "UniVibe.h"
#include "../tests/Tests/VibeMeasure.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace
{
using namespace px3tests::vibemeasure;
constexpr double kSr = 48000.0;
constexpr int kSeconds = 6;
constexpr int kTotal = static_cast<int>(kSr) * kSeconds;

struct Stereo
{
    std::vector<float> l, r;
};

void writeStereo(const juce::File& file, const Stereo& s)
{
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr) return;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(), kSr, 2, 24, {}, 0));
    if (writer == nullptr) return;
    juce::AudioBuffer<float> buffer(2, static_cast<int>(s.l.size()));
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        buffer.setSample(0, n, juce::jlimit(-1.0f, 1.0f, s.l[static_cast<std::size_t>(n)]));
        buffer.setSample(1, n, juce::jlimit(-1.0f, 1.0f, s.r[static_cast<std::size_t>(n)]));
    }
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

// ---- sources -------------------------------------------------------------------
std::vector<float> pluck(double hz, int length, juce::Random& random, float brightness = 0.5f)
{
    const auto period = juce::jmax(2, static_cast<int>(kSr / hz));
    std::vector<float> line(static_cast<std::size_t>(period));
    for (auto& v : line) v = random.nextFloat() * 2.0f - 1.0f;
    std::vector<float> out(static_cast<std::size_t>(length));
    int index = 0;
    float previous = 0.0f;
    for (int n = 0; n < length; ++n)
    {
        const auto current = line[static_cast<std::size_t>(index)];
        const auto next = brightness * current + (1.0f - brightness) * previous;
        line[static_cast<std::size_t>(index)] = 0.996f * (0.5f * (current + next));
        previous = current;
        out[static_cast<std::size_t>(n)] = current;
        index = (index + 1) % period;
    }
    return out;
}

void addAt(std::vector<float>& into, const std::vector<float>& what, int at, float gain)
{
    for (std::size_t i = 0; i < what.size() && at + static_cast<int>(i) < static_cast<int>(into.size()); ++i)
        into[static_cast<std::size_t>(at) + i] += what[i] * gain;
}

std::vector<float> guitarLine()
{
    juce::Random random(1);
    std::vector<float> out(static_cast<std::size_t>(kTotal), 0.0f);
    const double notes[] = { 196.0, 246.9, 293.7, 392.0, 329.6, 293.7, 246.9, 220.0 };
    for (int i = 0; i < 16; ++i)
        addAt(out, pluck(notes[i % 8], static_cast<int>(kSr * 1.2), random, 0.7f), static_cast<int>(i * kSr * 0.36), 0.35f);
    return out;
}

std::vector<float> chords()
{
    juce::Random random(2);
    std::vector<float> out(static_cast<std::size_t>(kTotal), 0.0f);
    const std::vector<std::vector<double>> shapes { { 164.8, 246.9, 329.6, 415.3, 493.9 },
                                                    { 146.8, 220.0, 293.7, 370.0, 440.0 } };
    for (int bar = 0; bar < 4; ++bar)
    {
        const auto& shape = shapes[static_cast<std::size_t>(bar % 2)];
        for (std::size_t s = 0; s < shape.size(); ++s)
            addAt(out, pluck(shape[s], static_cast<int>(kSr * 1.6), random, 0.6f),
                  static_cast<int>(bar * kSr * 1.5 + static_cast<double>(s) * 0.012 * kSr), 0.22f);
    }
    return out;
}

std::vector<float> drums()
{
    juce::Random random(3);
    std::vector<float> out(static_cast<std::size_t>(kTotal), 0.0f);
    const auto beat = static_cast<int>(kSr * 0.25);
    for (int step = 0; step * beat < kTotal; ++step)
    {
        const auto at = step * beat;
        if (step % 4 == 0)
        {
            double phase = 0.0;
            for (int n = 0; n < 9000 && at + n < kTotal; ++n)
            {
                const auto t = n / kSr;
                phase += juce::MathConstants<double>::twoPi * (45.0 + 110.0 * std::exp(-t * 30.0)) / kSr;
                out[static_cast<std::size_t>(at + n)] += static_cast<float>(0.7 * std::sin(phase) * std::exp(-t * 9.0));
            }
        }
        if (step % 4 == 2)
            for (int n = 0; n < 7000 && at + n < kTotal; ++n)
                out[static_cast<std::size_t>(at + n)] += (random.nextFloat() * 2.0f - 1.0f) * 0.35f * static_cast<float>(std::exp(-n / (kSr * 0.04)));
        // Hi-hat: differentiated (so high-passed) noise, short.
        float previousNoise = 0.0f;
        for (int n = 0; n < 1500 && at + n < kTotal; ++n)
        {
            const auto w = random.nextFloat() * 2.0f - 1.0f;
            out[static_cast<std::size_t>(at + n)] += (w - previousNoise) * 0.05f
                                                     * static_cast<float>(std::exp(-n / (kSr * 0.01)));
            previousNoise = w;
        }
    }
    return out;
}

std::vector<float> pad()
{
    std::vector<float> out(static_cast<std::size_t>(kTotal), 0.0f);
    const double notes[] = { 220.0, 277.2, 329.6, 440.0 };
    std::array<double, 8> phases {};
    for (int n = 0; n < kTotal; ++n)
    {
        double acc = 0.0;
        for (std::size_t v = 0; v < 8; ++v)
        {
            const auto hz = notes[v % 4] * (v < 4 ? 1.0 : 1.004);
            phases[v] += hz / kSr;
            phases[v] -= std::floor(phases[v]);
            acc += 2.0 * phases[v] - 1.0; // saw
        }
        const auto env = juce::jmin(1.0, n / (kSr * 0.8));
        out[static_cast<std::size_t>(n)] = static_cast<float>(acc * 0.04 * env);
    }
    // gentle low-pass
    float s = 0.0f;
    for (auto& v : out) { s += (v - s) * 0.18f; v = s; }
    return out;
}

std::vector<float> bass()
{
    std::vector<float> out(static_cast<std::size_t>(kTotal), 0.0f);
    const double notes[] = { 55.0, 55.0, 73.4, 65.4 };
    double phase = 0.0;
    float lp = 0.0f;
    for (int n = 0; n < kTotal; ++n)
    {
        const auto step = n / static_cast<int>(kSr * 0.375);
        const auto within = n % static_cast<int>(kSr * 0.375);
        phase += notes[step % 4] / kSr;
        phase -= std::floor(phase);
        const auto square = phase < 0.5 ? 1.0f : -1.0f;
        lp += (square - lp) * 0.04f;
        out[static_cast<std::size_t>(n)] = lp * 0.45f * std::exp(-static_cast<float>(within) / static_cast<float>(kSr * 0.6));
    }
    return out;
}

std::vector<float> mix(const std::vector<std::vector<float>>& parts)
{
    std::vector<float> out(static_cast<std::size_t>(kTotal), 0.0f);
    for (const auto& p : parts)
        for (std::size_t i = 0; i < out.size(); ++i) out[i] += p[i] * 0.6f;
    return out;
}

// ---- measurement ---------------------------------------------------------------
double rmsDb(const std::vector<float>& x)
{
    double e = 0.0;
    for (const auto v : x) e += static_cast<double>(v) * v;
    return 10.0 * std::log10(e / static_cast<double>(x.empty() ? 1 : x.size()) + 1e-30);
}

double maxStep(const std::vector<float>& x)
{
    double worst = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i) worst = juce::jmax(worst, static_cast<double>(std::abs(x[i] - x[i - 1])));
    return worst;
}

struct Config
{
    const char* name;
    float intensity;
    float speed;
    int mode;
    bool enabled;
};

Stereo runVibe(const Config& c, const std::vector<float>& in)
{
    px3::UniVibe vibe;
    px3::UniVibeSettings s;
    s.enabled = c.enabled;
    s.intensity = c.intensity;
    s.speed = c.speed;
    s.mode = c.mode;
    vibe.updateForBlock(s);
    vibe.prepare(kSr);
    Stereo out { std::vector<float>(in.size()), std::vector<float>(in.size()) };
    for (std::size_t n = 0; n < in.size(); ++n)
    {
        if (n % 512 == 0) vibe.updateForBlock(s);
        vibe.processSampleFrame(in[n], in[n], out.l[n], out.r[n]);
    }
    return out;
}

// Rise fraction and notch range for a configuration, from probes.
void characterise(const Config& c, double& rise, double& notchLoHz, double& notchHiHz)
{
    rise = 0.5;
    notchLoHz = notchHiHz = 0.0;
    if (! c.enabled) return;
    const auto hz = px3::UniVibe::speedToHz(c.speed);
    const auto settle = static_cast<int>(kSr * 1.5);
    const auto cycles = 6;
    const auto length = settle + static_cast<int>(kSr / hz * cycles);
    {
        // Phase track at 1 kHz in VIBRATO.
        Config probe = c;
        probe.mode = 1;
        std::vector<float> tone(static_cast<std::size_t>(length));
        for (int n = 0; n < length; ++n)
            tone[static_cast<std::size_t>(n)] = static_cast<float>(0.1 * std::sin(juce::MathConstants<double>::twoPi * 1000.0 * n / kSr));
        const auto out = runVibe(probe, tone);
        constexpr int frame = 240;
        const auto framesPerCycle = static_cast<int>(std::lround(kSr / frame / hz));
        std::vector<double> track;
        double previous = 0.0, offset = 0.0;
        for (int start = settle; start + frame <= length; start += frame)
        {
            auto phase = std::arg(dft(out.l.data() + start, frame, 1000.0) / dft(tone.data() + start, frame, 1000.0));
            if (! track.empty())
            {
                while (phase + offset - previous > juce::MathConstants<double>::pi) offset -= juce::MathConstants<double>::twoPi;
                while (phase + offset - previous < -juce::MathConstants<double>::pi) offset += juce::MathConstants<double>::twoPi;
            }
            previous = phase + offset;
            track.push_back(previous);
        }
        // Whole cycles only (the period is rounded to whole 5 ms frames).
        rise = riseFraction(track, framesPerCycle);
    }
    {
        Config probe = c;
        probe.mode = 0;
        std::vector<float> in(static_cast<std::size_t>(length));
        for (int n = 0; n < length; ++n) in[static_cast<std::size_t>(n)] = probeSignal(n);
        const auto out = runVibe(probe, in);
        const auto frames = responseFrames(in, out.l, settle);
        double lo = 1e9, hi = -1e9;
        for (const auto& f : frames)
        {
            const auto l = notchLog2Hz(f);
            lo = juce::jmin(lo, l);
            hi = juce::jmax(hi, l);
        }
        notchLoHz = std::pow(2.0, lo);
        notchHiHz = std::pow(2.0, hi);
    }
}

// ANALOG through the Synth: a held chord, rendered with ANALOG and/or VIBE.
Stereo synthChord(bool analogOn, bool vibeOn)
{
    PX3SynthAudioProcessor processor;
    auto set = [&processor](const juce::String& id, float value)
    {
        for (auto* p : processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id)
                r->setValueNotifyingHost(r->convertTo0to1(value));
    };
    set("voice.osc1.mode", 1.0f);
    set("voice.osc2.enabled", 1.0f);
    set("voice.osc2.mode", 1.0f);
    set("voice.osc2.tuning.cents", -7.0f);
    set("voice.filter1.enabled", 1.0f);
    set("voice.filter1.cutoff", 2600.0f);
    set("voice.amp.attack", 0.02f);
    set("voice.amp.release", 0.8f);
    set("fx.delay.enabled", 0.0f);
    set("fx.reverb.enabled", 0.0f);
    set("fx.mood.enabled", 0.0f);
    set("fx.analog.enabled", analogOn ? 1.0f : 0.0f);
    set("fx.analog.amount", analogOn ? 0.6f : 0.0f);
    set("fx.analog.type", 3.0f);
    set("fx.vibe.enabled", vibeOn ? 1.0f : 0.0f);
    set("fx.vibe.intensity", 0.7f);
    set("fx.vibe.speed", 0.4f);
    constexpr int block = 512;
    processor.setPlayConfigDetails(0, 2, kSr, block);
    processor.prepareToPlay(kSr, block);
    juce::AudioBuffer<float> buffer(2, block);
    Stereo out;
    for (int start = 0; start < kTotal; start += block)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        if (start == 0)
            for (const auto note : { 48, 55, 60, 64, 67 }) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 100);
        if (start <= kTotal - static_cast<int>(kSr) && start + block > kTotal - static_cast<int>(kSr))
            for (const auto note : { 48, 55, 60, 64, 67 }) midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        processor.processBlock(buffer, midi);
        for (int i = 0; i < block; ++i)
        {
            out.l.push_back(buffer.getSample(0, i));
            out.r.push_back(buffer.getSample(1, i));
        }
    }
    return out;
}
} // namespace

int runVibeRenders(const juce::String& outDir)
{
    const juce::File dir(outDir);
    dir.createDirectory();
    std::printf("\nVIBE / ANALOG SONIC VALIDATION  -> %s\n", dir.getFullPathName().toRawUTF8());

    const Config configs[] = {
        { "off", 0.6f, 0.4f, 0, false },
        { "low", 0.3f, 0.4f, 0, true },
        { "high", 0.9f, 0.4f, 0, true },
        { "slow", 0.7f, 0.1f, 0, true },
        { "fast", 0.7f, 0.9f, 0, true },
        { "chorus", 0.7f, 0.4f, 0, true },
        { "vibrato", 0.7f, 0.4f, 1, true },
    };

    std::printf("\n  config characteristics (probes)\n  %-8s %6s %6s %18s\n", "config", "Hz", "rise", "notch range Hz");
    for (const auto& c : configs)
    {
        double rise = 0.5, lo = 0.0, hi = 0.0;
        characterise(c, rise, lo, hi);
        std::printf("  %-8s %6.2f %6.3f %8.0f .. %6.0f\n", c.name, px3::UniVibe::speedToHz(c.speed), rise, lo, hi);
    }

    const auto g = guitarLine();
    const auto ch = chords();
    const auto dr = drums();
    const auto pd = pad();
    const auto bs = bass();
    const std::vector<std::pair<const char*, std::vector<float>>> sources {
        { "guitar", g }, { "chords", ch }, { "drums", dr }, { "pad", pd }, { "bass", bs }, { "mix", mix({ g, ch, dr, pd, bs }) }
    };

    std::printf("\n  renders: level = output RMS vs dry (dB); step = output max |dx| / dry max |dx|\n");
    std::printf("  %-8s %-8s %8s %8s %8s\n", "source", "config", "level", "step", "peak");
    for (const auto& [sourceName, signal] : sources)
    {
        writeStereo(dir.getChildFile(juce::String(sourceName) + "-dry.wav"), { signal, signal });
        const auto dryStep = maxStep(signal);
        const auto dryDb = rmsDb(signal);
        for (const auto& c : configs)
        {
            if (! c.enabled) continue;
            const auto out = runVibe(c, signal);
            double peak = 0.0;
            for (const auto v : out.l) peak = juce::jmax(peak, static_cast<double>(std::abs(v)));
            std::printf("  %-8s %-8s %+8.2f %8.2f %8.3f\n", sourceName, c.name, rmsDb(out.l) - dryDb,
                        maxStep(out.l) / juce::jmax(1e-9, dryStep), peak);
            writeStereo(dir.getChildFile(juce::String(sourceName) + "-vibe-" + c.name + ".wav"), out);
        }
    }

    std::printf("\n  synth chord (ANALOG lives in the voices)\n");
    const std::pair<const char*, std::pair<bool, bool>> synthCases[] = {
        { "synth-clean", { false, false } }, { "synth-analog", { true, false } },
        { "synth-vibe", { false, true } }, { "synth-analog+vibe", { true, true } }
    };
    std::vector<float> clean;
    for (const auto& [name, flags] : synthCases)
    {
        const auto out = synthChord(flags.first, flags.second);
        if (clean.empty()) clean = out.l;
        double diff = 0.0, e = 0.0;
        for (std::size_t i = 0; i < out.l.size(); ++i)
        {
            const auto d = static_cast<double>(out.l[i]) - clean[i];
            diff += d * d;
            e += static_cast<double>(clean[i]) * clean[i];
        }
        std::printf("  %-18s level %+6.2f dB, departure from clean %.3f, max step %.4f\n", name,
                    rmsDb(out.l) - rmsDb(clean), std::sqrt(diff / juce::jmax(1e-30, e)), maxStep(out.l));
        writeStereo(dir.getChildFile(juce::String(name) + ".wav"), out);
    }
    std::printf("\n  wrote WAVs to %s\n\n", dir.getFullPathName().toRawUTF8());
    return 0;
}
