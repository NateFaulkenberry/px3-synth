// PX3Diag reverb-renders <dir>
//
// Offline listening and screening set for the reverb. Synthesises a fixed,
// deterministic set of test material (drums, a vowel-like voice, a mono lead,
// bass, piano-like tones, a plucked guitar line, a pad, a dense loop, a sparse
// melody, an arpeggio and FM percussion), runs each through every reverb type
// at short / medium / long / extreme settings and through every factory reverb
// preset, and writes 24-bit WAVs plus one CSV of objective measures per render.
//
// The measures are the ones the preset screen uses (see ReverbMeasure.h):
//   lowGainDb   wet energy below 150 Hz against the dry's, relative to the
//               broadband wet/dry ratio (positive = low-end build-up)
//   onset       onset sharpness of the mixed signal against the dry (1 = intact)
//   centroid    spectral centroid of the wet, Hz
//   corr        inter-channel correlation of the wet
//   monoLossDb  mono-sum energy loss of the wet against L+R energy
//   ring        worst tail peak over the noise reference (dB, impulse)
//   mix         mixing time (ms, impulse)
//   rt60        broadband RT60 (s, impulse)
//   step        largest sample step of the output / the dry's
//   peak        output peak

#include <JuceHeader.h>

#include "Reverb.h"
#if __has_include("ReverbPresets.h")
 #include "ReverbPresets.h"
 #define PX3_REVERB_HAS_PRESETS 1
#else
 // Rendering the reverb as it was before the redesign, for A/B listening.
 namespace px3::reverb
 {
 inline constexpr int kTypeCount = 4;
 inline constexpr const char* kTypeNames[] = { "ROOM", "PLATE", "HALL", "CLOUD" };
 }
 #define PX3_REVERB_HAS_PRESETS 0
#endif
#include "../tests/Tests/ReverbMeasure.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace
{
using namespace px3tests::reverbmeasure;
constexpr double kSr = 48000.0;
constexpr int kSeconds = 7;
constexpr int kSourceSeconds = 5;   // material, then two seconds of tail
constexpr int kTotal = static_cast<int>(kSr) * kSeconds;
constexpr int kSource = static_cast<int>(kSr) * kSourceSeconds;
constexpr double kTwoPi = 6.283185307179586;

struct Stereo { std::vector<float> l, r; };

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

std::vector<float> silence() { return std::vector<float>(static_cast<std::size_t>(kTotal), 0.0f); }

void addAt(std::vector<float>& into, const std::vector<float>& what, int at, float gain)
{
    for (std::size_t i = 0; i < what.size() && at + static_cast<int>(i) < kSource; ++i)
        into[static_cast<std::size_t>(at) + i] += what[i] * gain;
}

std::vector<float> pluckNote(double hz, int length, juce::Random& random, float brightness)
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

// Band-limited-ish saw by additive partials (no aliasing to colour the tail).
float saw(double phase, double hz)
{
    double acc = 0.0;
    const auto partials = juce::jlimit(1, 60, static_cast<int>(18000.0 / juce::jmax(20.0, hz)));
    for (int k = 1; k <= partials; ++k) acc += std::sin(kTwoPi * k * phase) / k;
    return static_cast<float>(acc * 0.55);
}

std::vector<float> drums()
{
    juce::Random random(3);
    auto out = silence();
    const auto beat = static_cast<int>(kSr * 0.25);
    for (int step = 0; step * beat < kSource - beat; ++step)
    {
        const auto at = step * beat;
        if (step % 4 == 0)
        {
            double phase = 0.0;
            for (int n = 0; n < 9000; ++n)
            {
                const auto t = n / kSr;
                phase += kTwoPi * (45.0 + 110.0 * std::exp(-t * 30.0)) / kSr;
                out[static_cast<std::size_t>(at + n)] += static_cast<float>(0.7 * std::sin(phase) * std::exp(-t * 9.0));
            }
        }
        if (step % 4 == 2)
            for (int n = 0; n < 7000; ++n)
                out[static_cast<std::size_t>(at + n)] += (random.nextFloat() * 2.0f - 1.0f) * 0.35f
                                                         * static_cast<float>(std::exp(-n / (kSr * 0.04)));
        float previousNoise = 0.0f;
        for (int n = 0; n < 1500; ++n)
        {
            const auto w = random.nextFloat() * 2.0f - 1.0f;
            out[static_cast<std::size_t>(at + n)] += (w - previousNoise) * 0.06f * static_cast<float>(std::exp(-n / (kSr * 0.01)));
            previousNoise = w;
        }
    }
    return out;
}

// A sung vowel: glottal-ish saw through three formant resonators, with
// vibrato and phrase gaps.
std::vector<float> voice()
{
    auto out = silence();
    const double notes[] = { 220.0, 246.9, 261.6, 293.7, 261.6, 220.0 };
    double phase = 0.0;
    struct Res { double f, bw, gain, y1 = 0, y2 = 0; };
    Res res[] = { { 700, 110, 1.0 }, { 1220, 120, 0.5 }, { 2600, 160, 0.25 } };
    for (int n = 0; n < kSource; ++n)
    {
        const auto t = n / kSr;
        const auto note = static_cast<int>(t / 0.8);
        const auto within = std::fmod(t, 0.8);
        const auto gate = within < 0.65 ? juce::jmin(1.0, within / 0.04) * juce::jmin(1.0, (0.65 - within) / 0.05) : 0.0;
        const auto hz = notes[note % 6] * (1.0 + 0.006 * std::sin(kTwoPi * 5.2 * t));
        phase += hz / kSr;
        phase -= std::floor(phase);
        const auto src = 2.0 * phase - 1.0;
        double y = 0.0;
        for (auto& r : res)
        {
            const auto rr = std::exp(-juce::MathConstants<double>::pi * r.bw / kSr);
            const auto a1 = -2.0 * rr * std::cos(kTwoPi * r.f / kSr), a2 = rr * rr;
            const auto v = src * (1.0 - rr) - a1 * r.y1 - a2 * r.y2;
            r.y2 = r.y1; r.y1 = v;
            y += v * r.gain;
        }
        out[static_cast<std::size_t>(n)] = static_cast<float>(y * gate * 0.5);
    }
    return out;
}

std::vector<float> lead()
{
    auto out = silence();
    const double notes[] = { 440.0, 523.3, 587.3, 659.3, 587.3, 523.3, 440.0, 392.0 };
    double phase = 0.0;
    float lp = 0.0f;
    for (int n = 0; n < kSource; ++n)
    {
        const auto t = n / kSr;
        const auto note = static_cast<int>(t / 0.5);
        const auto within = std::fmod(t, 0.5);
        const auto env = within < 0.42 ? juce::jmin(1.0, within / 0.005) : juce::jmax(0.0, 1.0 - (within - 0.42) / 0.02);
        const auto hz = notes[note % 8];
        phase += hz / kSr;
        phase -= std::floor(phase);
        lp += (saw(phase, hz) - lp) * 0.35f;
        out[static_cast<std::size_t>(n)] = lp * static_cast<float>(env) * 0.3f;
    }
    return out;
}

std::vector<float> bass()
{
    auto out = silence();
    const double notes[] = { 55.0, 55.0, 73.4, 65.4 };
    double phase = 0.0;
    float lp = 0.0f;
    for (int n = 0; n < kSource; ++n)
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

// Struck, inharmonic, two-stage decay - enough of a piano for a reverb to show
// whether it smears attacks or rings on sustained partials.
std::vector<float> piano()
{
    auto out = silence();
    const double notes[] = { 261.6, 329.6, 392.0, 523.3, 392.0, 329.6, 293.7, 246.9 };
    for (int i = 0; i < 9; ++i)
    {
        const auto f0 = notes[i % 8];
        const auto at = static_cast<int>(i * kSr * 0.5);
        for (int n = 0; n < static_cast<int>(kSr * 2.0); ++n)
        {
            const auto t = n / kSr;
            double y = 0.0;
            for (int k = 1; k <= 10; ++k)
            {
                const auto fk = f0 * k * std::sqrt(1.0 + 0.0004 * k * k);
                if (fk > 16000.0) break;
                y += std::sin(kTwoPi * fk * t) / (k * 1.3) * (0.6 * std::exp(-t * (1.5 + 0.8 * k)) + 0.4 * std::exp(-t * 0.6 * k));
            }
            const auto attack = juce::jmin(1.0, t / 0.002);
            if (at + n < kSource) out[static_cast<std::size_t>(at + n)] += static_cast<float>(y * attack * 0.22);
        }
    }
    return out;
}

std::vector<float> guitar()
{
    juce::Random random(1);
    auto out = silence();
    const double notes[] = { 196.0, 246.9, 293.7, 392.0, 329.6, 293.7, 246.9, 220.0 };
    for (int i = 0; i < 13; ++i)
        addAt(out, pluckNote(notes[i % 8], static_cast<int>(kSr * 1.2), random, 0.7f), static_cast<int>(i * kSr * 0.36), 0.35f);
    return out;
}

std::vector<float> pad()
{
    auto out = silence();
    const double notes[] = { 220.0, 277.2, 329.6, 440.0 };
    std::array<double, 8> phases {};
    float s = 0.0f;
    for (int n = 0; n < kSource; ++n)
    {
        double acc = 0.0;
        for (std::size_t v = 0; v < 8; ++v)
        {
            const auto hz = notes[v % 4] * (v < 4 ? 1.0 : 1.004);
            phases[v] += hz / kSr;
            phases[v] -= std::floor(phases[v]);
            acc += saw(phases[v], hz);
        }
        const auto env = juce::jmin(1.0, n / (kSr * 0.8)) * juce::jmin(1.0, (kSource - n) / (kSr * 0.3));
        s += (static_cast<float>(acc * 0.06 * env) - s) * 0.18f;
        out[static_cast<std::size_t>(n)] = s;
    }
    return out;
}

std::vector<float> arpeggio()
{
    auto out = silence();
    const double notes[] = { 261.6, 329.6, 392.0, 523.3, 659.3, 523.3, 392.0, 329.6 };
    double phase = 0.0;
    float lp = 0.0f;
    for (int n = 0; n < kSource; ++n)
    {
        const auto t = n / kSr;
        const auto step = static_cast<int>(t / 0.125);
        const auto within = std::fmod(t, 0.125);
        const auto hz = notes[step % 8];
        phase += hz / kSr;
        phase -= std::floor(phase);
        const auto cutoff = 0.05f + 0.5f * static_cast<float>(std::exp(-within * 30.0));
        lp += (saw(phase, hz) - lp) * cutoff;
        out[static_cast<std::size_t>(n)] = lp * static_cast<float>(std::exp(-within * 14.0)) * 0.35f;
    }
    return out;
}

// Two-operator FM percussion: metallic, wideband, short - the material that
// exposes metallic tails and smeared transients.
std::vector<float> fmPerc()
{
    auto out = silence();
    const double pitches[] = { 180.0, 240.0, 320.0, 150.0, 400.0 };
    for (int i = 0; i * kSr * 0.3 < kSource - kSr * 0.3; ++i)
    {
        const auto at = static_cast<int>(i * kSr * 0.3);
        const auto f = pitches[i % 5];
        for (int n = 0; n < static_cast<int>(kSr * 0.3); ++n)
        {
            const auto t = n / kSr;
            const auto index = 6.0 * std::exp(-t * 25.0);
            const auto y = std::sin(kTwoPi * f * t + index * std::sin(kTwoPi * f * 1.41 * t)) * std::exp(-t * 18.0);
            out[static_cast<std::size_t>(at + n)] += static_cast<float>(y * 0.4);
        }
    }
    return out;
}

std::vector<float> sparse()
{
    auto out = silence();
    const double notes[] = { 659.3, 0.0, 784.0, 0.0, 0.0, 587.3, 0.0, 523.3 };
    for (int i = 0; i < 8; ++i)
    {
        if (notes[i] <= 0.0) continue;
        const auto at = static_cast<int>(i * kSr * 0.6);
        for (int n = 0; n < static_cast<int>(kSr * 0.6); ++n)
        {
            const auto t = n / kSr;
            const auto y = (std::sin(kTwoPi * notes[i] * t) + 0.3 * std::sin(kTwoPi * notes[i] * 2.0 * t))
                           * juce::jmin(1.0, t / 0.003) * std::exp(-t * 5.0);
            if (at + n < kSource) out[static_cast<std::size_t>(at + n)] += static_cast<float>(y * 0.35);
        }
    }
    return out;
}

std::vector<float> dense(const std::vector<std::vector<float>>& parts)
{
    auto out = silence();
    for (const auto& p : parts)
        for (std::size_t i = 0; i < out.size(); ++i) out[i] += p[i] * 0.5f;
    return out;
}

Stereo runReverb(const ReverbSettings& base, const std::vector<float>& in, float amount)
{
    auto reverb = std::make_unique<::Reverb>();
    reverb->prepare(kSr);
    auto s = base;
    s.enabled = true;
    s.amount = amount;
    // Let the mix and the type fade settle before the material starts.
    for (int b = 0; b < 40; ++b)
    {
        reverb->updateForBlock(s, 512);
        for (int i = 0; i < 512; ++i) { float l = 0.0f, r = 0.0f; reverb->processSampleFrame(0.0f, 0.0f, l, r); }
    }
    Stereo out { std::vector<float>(in.size()), std::vector<float>(in.size()) };
    for (std::size_t n = 0; n < in.size(); ++n)
    {
        if (n % 512 == 0) reverb->updateForBlock(s, 512);
        reverb->processSampleFrame(in[n], in[n], out.l[n], out.r[n]);
    }
    return out;
}

double bandEnergy(const std::vector<float>& x, double hiHz)
{
    // One-pole cascade low-pass: good enough for a ratio.
    const auto c = std::exp(-kTwoPi * hiHz / kSr);
    double a = 0, b = 0, e = 0;
    for (const auto v : x)
    {
        a = (1 - c) * v + c * a;
        b = (1 - c) * a + c * b;
        e += b * b;
    }
    return e;
}

// Onset sharpness: mean, over the dry's onsets, of the mixed signal's
// 0-5 ms energy rise against its own 5-30 ms energy, relative to the dry's.
double onsetSharpness(const std::vector<float>& dry, const std::vector<float>& wet)
{
    double ratio = 0.0;
    int count = 0;
    const auto w = static_cast<std::size_t>(kSr * 0.005);
    for (std::size_t n = w * 4; n + w * 7 < static_cast<std::size_t>(kSource); n += w)
    {
        const auto before = energy(dry, n - w * 4, n);
        const auto after = energy(dry, n, n + w);
        if (after < 1e-4 || after < before * 8.0) continue;   // not an onset
        const auto dryRatio = after / (energy(dry, n + w, n + w * 6) / 5.0 + 1e-12);
        const auto wetRatio = energy(wet, n, n + w) / (energy(wet, n + w, n + w * 6) / 5.0 + 1e-12);
        ratio += juce::jmin(1.5, wetRatio / juce::jmax(1e-9, dryRatio));
        ++count;
        n += w * 20;
    }
    return count > 0 ? ratio / count : 1.0;
}

struct ImpulseStats { double rt { 0 }, mixMs { 0 }, ring { 0 }; };

ImpulseStats impulseStats(const ReverbSettings& s)
{
    std::vector<float> in(static_cast<std::size_t>(kSr * 6.0), 0.0f);
    in[0] = 1.0f;
    const auto out = runReverb(s, in, 1.0f);
    ImpulseStats st;
    st.rt = rt60(out.l, kSr);
    st.mixMs = mixingTimeMs(out.l, kSr);
    // The window follows the decay: a short room is measured early, before
    // its tail has fallen into the numerical floor.
    const auto dur = juce::jlimit(200.0, 1500.0, 0.6 * st.rt * 1000.0);
    const auto start = st.rt > 4.0 ? 300.0 : (st.rt < 0.6 ? 20.0 : 100.0);
    // SPRING ends near 3 kHz; GATED is a burst, not a decay, so its "tail"
    // window holds nothing worth measuring.
    const auto top = s.algorithmIndex == px3::reverb::spring ? 2500.0 : 8000.0;
    if (s.algorithmIndex != px3::reverb::gated && static_cast<double>(out.l.size()) / kSr * 1000.0 > start + dur + 20.0)
        st.ring = ringing(out.l, kSr, start, dur, st.rt, top).worstDb - ringingNoiseReferenceDb(kSr, start, dur, st.rt, top);
    return st;
}

struct Config { juce::String name; ReverbSettings settings; };
} // namespace

int runReverbRenders(const juce::String& outDir)
{
    const juce::File dir(outDir);
    dir.createDirectory();
    std::printf("\nREVERB SONIC VALIDATION  -> %s\n", dir.getFullPathName().toRawUTF8());

    const auto dr = drums(), vo = voice(), ld = lead(), bs = bass(), pn = piano(), gt = guitar(), pd = pad(),
               ar = arpeggio(), fm = fmPerc(), sp = sparse();
    const std::vector<std::pair<const char*, std::vector<float>>> sources {
        { "drums", dr }, { "voice", vo }, { "lead", ld }, { "bass", bs }, { "piano", pn }, { "guitar", gt },
        { "pad", pd }, { "arp", ar }, { "fmperc", fm }, { "sparse", sp }, { "dense", dense({ dr, bs, pd, ar }) }
    };
    for (const auto& [name, signal] : sources)
        writeStereo(dir.getChildFile(juce::String("dry-") + name + ".wav"), { signal, signal });

    // Every type at four lengths, then every preset.
    std::vector<Config> configs;
    for (int type = 0; type < px3::reverb::kTypeCount; ++type)
    {
        const char* lengths[] = { "short", "medium", "long", "extreme" };
        const float decays[] = { 0.15f, 0.45f, 0.75f, 1.0f };
        for (int k = 0; k < 4; ++k)
        {
            ReverbSettings s;
            s.algorithmIndex = type;
            s.decay = decays[k];
            configs.push_back({ juce::String(px3::reverb::kTypeNames[type]) + "-" + lengths[k], s });
        }
    }
#if PX3_REVERB_HAS_PRESETS
    for (int type = 0; type < px3::reverb::kTypeCount; ++type)
        for (const auto& preset : px3::reverb::presetsForType(type))
        {
            ReverbSettings s;
            px3::reverb::applyPresetToSettings(preset, s);
            configs.push_back({ juce::String("preset-") + px3::reverb::kTypeNames[type] + "-"
                                    + juce::String(preset.name).replaceCharacter(' ', '_').removeCharacters("*'"), s });
        }
#endif

    auto csv = dir.getChildFile("metrics.csv");
    csv.deleteFile();
    juce::FileOutputStream table(csv);
    table << "config,source,rt60,mixMs,ringDb,lowGainDb,onset,centroidHz,corr,monoLossDb,step,peak\n";
    std::printf("  %-34s %6s %6s %6s | %7s %6s %8s %6s %7s %6s  screen\n", "config", "rt60", "mixMs", "ring",
                "lowDb", "onset", "centroid", "corr", "monoDb", "peak");
    std::printf("  (lowDb worst source; onset median of the percussive sources; centroid = wet / dry centroid, median;\n"
                "   corr mean over the non-bass, non-drum sources; monoDb worst source)\n");
    // PX3_RV_ONLY=<text> renders only the configs whose name contains it;
    // PX3_RV_NOWAV=1 measures without writing audio.
    const auto only = juce::SystemStats::getEnvironmentVariable("PX3_RV_ONLY", "");
    const auto writeWavs = juce::SystemStats::getEnvironmentVariable("PX3_RV_NOWAV", "").isEmpty();
    for (const auto& c : configs)
    {
        if (only.isNotEmpty() && ! c.name.contains(only)) continue;
        const auto imp = impulseStats(c.settings);
        double worstLow = -99, worstMono = 0, worstPeak = 0, corrSum = 0;
        std::vector<double> tilts;
        int corrCount = 0;
        std::vector<double> onsets;
        for (const auto& [sourceName, signal] : sources)
        {
            const auto wet = runReverb(c.settings, signal, 1.0f);
            const auto mixed = runReverb(c.settings, signal, 0.35f);
            const auto lowGain = 10.0 * std::log10((bandEnergy(wet.l, 150.0) + 1e-20) / (bandEnergy(signal, 150.0) + 1e-20))
                                 - 10.0 * std::log10((energy(wet.l) + 1e-20) / (energy(signal) + 1e-20));
            const auto onset = onsetSharpness(signal, mixed.l);
            const auto centroid = spectralCentroidHz(wet.l, kSr);
            const auto corr = correlation(wet.l, wet.r, 0, wet.l.size());
            std::vector<float> mono(wet.l.size());
            for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (wet.l[i] + wet.r[i]);
            const auto monoLoss = 10.0 * std::log10((2.0 * energy(mono) + 1e-20) / (0.5 * (energy(wet.l) + energy(wet.r)) + 1e-20) / 2.0);
            double peak = 0.0;
            for (std::size_t i = 0; i < mixed.l.size(); ++i)
                peak = juce::jmax(peak, static_cast<double>(juce::jmax(std::abs(mixed.l[i]), std::abs(mixed.r[i]))));
            const auto step = maxStep(mixed.l) / juce::jmax(1e-9, maxStep(signal));
            table << c.name << "," << sourceName << "," << imp.rt << "," << imp.mixMs << "," << imp.ring << "," << lowGain << ","
                  << onset << "," << centroid << "," << corr << "," << monoLoss << "," << step << "," << peak << "\n";
            worstLow = juce::jmax(worstLow, lowGain);
            const juce::String src(sourceName);
            if (src == "drums" || src == "arp" || src == "fmperc" || src == "sparse" || src == "guitar" || src == "piano") onsets.push_back(onset);
            if (src != "bass" && src != "drums" && src != "dense")
                tilts.push_back(centroid / juce::jmax(1.0, spectralCentroidHz(signal, kSr)));
            if (src != "bass" && src != "drums" && src != "dense") { corrSum += corr; ++corrCount; }
            worstMono = juce::jmin(worstMono, monoLoss);
            worstPeak = juce::jmax(worstPeak, peak);
            if (writeWavs) writeStereo(dir.getChildFile(c.name + "-" + sourceName + ".wav"), mixed);
        }
        std::sort(onsets.begin(), onsets.end());
        const auto onsetMedian = onsets.empty() ? 1.0 : onsets[onsets.size() / 2];
        const auto corrMean = corrCount > 0 ? corrSum / corrCount : 0.0;
        std::sort(tilts.begin(), tilts.end());
        const auto tilt = tilts.empty() ? 1.0 : tilts[tilts.size() / 2];
        // The preset screen. Thresholds: docs/REVERB_DSP_DESIGN.md.
        juce::StringArray why;
        if (worstLow > 3.0) why.add("low build-up");
        if (onsetMedian < 0.6) why.add("smears transients");
        if (tilt < 0.45 || tilt > 1.6) why.add(tilt < 0.45 ? "muddy" : "harsh");
        if (corrMean > 0.5) why.add("narrow");
        if (worstMono < -6.0) why.add("phasey in mono");
        if (imp.ring > 5.0 && c.settings.algorithmIndex != px3::reverb::spring) why.add("rings");
        std::printf("  %-34s %6.2f %6.0f %+6.1f | %+7.1f %6.2f %8.2f %+6.2f %+7.2f %6.3f  %s\n", c.name.toRawUTF8(), imp.rt,
                    imp.mixMs, imp.ring, worstLow, onsetMedian, tilt, corrMean, worstMono, worstPeak,
                    why.isEmpty() ? "pass" : ("REJECT: " + why.joinIntoString(", ")).toRawUTF8());
        std::fflush(stdout);
    }
    std::printf("\n  wrote WAVs (mixed at 35%%) and metrics.csv to %s\n\n", dir.getFullPathName().toRawUTF8());
    return 0;
}

// PX3Diag reverb-metrics [rate]: the impulse-response measures of every type
// at its default controls, plus the wet level for steady noise (what the
// per-type level table is calibrated from).
int runReverbMetrics(double rate)
{
    std::printf("\nREVERB METRICS  %.0f Hz (impulse, fully wet, default controls)\n", rate);
    std::printf("  %-7s %6s %28s %6s %11s %6s %6s %6s %6s %7s %8s\n", "type", "rt60", "rt60 @ 250/1k/4k/8k", "mixMs",
                "ring(ref)", "flat", "nonlin", "corr", "crest", "peak", "noiseDb");
    for (int type = 0; type < px3::reverb::kTypeCount; ++type)
    {
        ReverbSettings s;
        s.algorithmIndex = type;
        s.preDelay = 0.0f;
        s.enabled = true;
        s.amount = 1.0f;
        // Overrides for quick experiments: PX3_RV_<CONTROL>=value (normalised).
        for (int c = 0; c < px3::reverb::kControlCount; ++c)
        {
            const auto key = juce::String("PX3_RV_") + juce::String(px3::reverb::kParameterSpecs[c].id).fromLastOccurrenceOf(".", false, false).toUpperCase();
            const auto v = juce::SystemStats::getEnvironmentVariable(key, "");
            if (v.isNotEmpty()) px3::reverb::settingsField(s, px3::reverb::kParameterSpecs[c].control) = v.getFloatValue();
        }
        const auto seconds = type == px3::reverb::cloud ? 30.0 : 8.0;
        std::vector<float> in(static_cast<std::size_t>(rate * seconds), 0.0f);
        in[0] = 1.0f;
        auto reverb = std::make_unique<::Reverb>();
        reverb->prepare(rate);
        for (int n = 0; n < static_cast<int>(0.5 * rate); ++n)   // MIX settles before the impulse
        {
            if (n % 512 == 0) reverb->updateForBlock(s, 512);
            float l = 0.0f, r = 0.0f;
            reverb->processSampleFrame(0.0f, 0.0f, l, r);
        }
        Stereo out { std::vector<float>(in.size()), std::vector<float>(in.size()) };
        for (std::size_t n = 0; n < in.size(); ++n)
        {
            if (n % 512 == 0) reverb->updateForBlock(s, 512);
            reverb->processSampleFrame(in[n], in[n], out.l[n], out.r[n]);
        }
        const auto rt = rt60(out.l, rate);
        const auto start = rt > 4.0 ? 300.0 : 100.0;
        const auto dur = juce::jlimit(300.0, 1500.0, 0.6 * rt * 1000.0);
        // SPRING's bandwidth ends near 3 kHz; above it there is nothing to ring.
        const auto top = type == px3::reverb::spring ? 2500.0 : 8000.0;
        const auto ring = ringing(out.l, rate, start, dur, rt, top);
        const auto ref = ringingNoiseReferenceDb(rate, start, dur, rt, top);
        double peak = 0.0;
        for (const auto v : out.l) peak = juce::jmax(peak, static_cast<double>(std::abs(v)));
        // Steady noise, 3 s, wet level against the input's.
        juce::Random random(11);
        std::vector<float> noise(static_cast<std::size_t>(rate * 3.0));
        // Roughly pink (-3 dB/oct above ~1 kHz): closer to music than white.
        float lp1 = 0.0f, lp2 = 0.0f;
        for (auto& v : noise)
        {
            const auto w = random.nextFloat() * 2.0f - 1.0f;
            lp1 += 0.12f * (w - lp1);
            lp2 += 0.6f * (w - lp2);
            v = (0.7f * lp1 + 0.3f * lp2) * 0.6f;
        }
        auto r2 = std::make_unique<::Reverb>();
        r2->prepare(rate);
        double eIn = 0.0, eOut = 0.0;
        for (std::size_t n = 0; n < noise.size(); ++n)
        {
            if (n % 512 == 0) r2->updateForBlock(s, 512);
            float l = 0.0f, r = 0.0f;
            r2->processSampleFrame(noise[n], noise[n], l, r);
            if (n > noise.size() / 2) { eIn += static_cast<double>(noise[n]) * noise[n]; eOut += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r); }
        }
        std::printf("  %-7s %6.2f %6.2f/%5.2f/%5.2f/%5.2f       %6.0f %5.1f(%4.1f)@%-5.0f %6.3f %6.2f %+6.2f %6.1f %7.3f %+8.2f\n",
                    px3::reverb::kTypeNames[type], rt, bandRt60(out.l, rate, 250.0), bandRt60(out.l, rate, 1000.0),
                    bandRt60(out.l, rate, 4000.0), bandRt60(out.l, rate, 8000.0), mixingTimeMs(out.l, rate),
                    ring.worstDb, ref, ring.worstHz, spectralFlatness(out.l, rate, 200.0), decayNonlinearityDb(out.l),
                    correlation(out.l, out.r, static_cast<std::size_t>(0.1 * rate), static_cast<std::size_t>(1.0 * rate)),
                    worstCrest(out.l, rate, 1.0, 80.0), peak, 10.0 * std::log10((eOut + 1e-20) / (eIn + 1e-20)));
        if (type == px3::reverb::spring)
            std::printf("          spring: echo period 500 Hz %.1f / 2 kHz %.1f ms; first arrival 300 Hz %.1f, 1 kHz %.1f, 2 kHz %.1f, 3 kHz %.1f ms\n",
                        echoPeriodMs(out.l, rate, 500.0), echoPeriodMs(out.l, rate, 2000.0),
                        firstArrivalMs(out.l, rate, 300.0, 60.0), firstArrivalMs(out.l, rate, 1000.0, 60.0),
                        firstArrivalMs(out.l, rate, 2000.0, 60.0), firstArrivalMs(out.l, rate, 3000.0, 60.0));
        std::fflush(stdout);
    }
    return 0;
}

// PX3Diag reverb-zip <type> <control|amount|type>: where the worst second
// difference lands while one control sweeps (debugging aid for the zipper test).
int runReverbZip(int type, const juce::String& what)
{
    const double rate = 48000.0;
    std::vector<float> in(static_cast<std::size_t>(rate * 4.0));
    for (std::size_t n = 0; n < in.size(); ++n) in[n] = static_cast<float>(0.3 * std::sin(6.283185307 * 220.0 * static_cast<double>(n) / rate));
    auto render = [&](bool sweep)
    {
        auto reverb = std::make_unique<::Reverb>();
        reverb->prepare(rate);
        ReverbSettings s;
        s.algorithmIndex = type;
        s.amount = 0.6f;
        s.preDelay = 0.0f;
        std::vector<float> out(in.size());
        for (std::size_t n = 0; n < in.size(); ++n)
        {
            if (n % 512 == 0 && sweep)
            {
                const auto t = static_cast<float>(n / 512) / 280.0f;
                const auto v = t < 0.5f ? 2.0f * t : juce::jmax(0.0f, 2.0f - 2.0f * t);
                if (what == "type") s.algorithmIndex = static_cast<int>(n / 512 / 30) % px3::reverb::kTypeCount;
                else if (what == "amount") s.amount = v;
                else
                    for (const auto& spec : px3::reverb::kParameterSpecs)
                        if (juce::String(spec.id).endsWith(what)) px3::reverb::settingsField(s, spec.control) = v;
            }
            if (n % 512 == 0) reverb->updateForBlock(s, 512);
            float l = 0.0f, r = 0.0f;
            reverb->processSampleFrame(in[n], in[n], l, r);
            out[n] = l;
        }
        return out;
    };
    const auto still = render(false), moved = render(true);
    auto worst = [](const std::vector<float>& x, std::size_t& at)
    {
        double w = 0.0;
        for (std::size_t i = 48002; i < x.size(); ++i)
        {
            const auto d = std::abs(static_cast<double>(x[i]) - 2.0 * x[i - 1] + x[i - 2]);
            if (d > w) { w = d; at = i; }
        }
        return w;
    };
    std::size_t a = 0, b = 0;
    const auto ws = worst(still, a), wm = worst(moved, b);
    std::printf("still %.6f at %zu; moved %.6f at %zu (block %zu, offset %zu, mod32 %zu)\n", ws, a, wm, b, b / 512, b % 512, b % 32);
    for (std::size_t i = b - 4; i < b + 4; ++i) std::printf("  %zu: %+.6f\n", i, static_cast<double>(moved[i]));
    return 0;
}

// PX3Diag reverb-onset <type> <warmup>: first non-zero wet sample after an impulse.
int runReverbOnset(int type, int warmup)
{
    auto reverb = std::make_unique<::Reverb>();
    reverb->prepare(48000.0);
    ReverbSettings s;
    s.algorithmIndex = type;
    s.amount = 1.0f;
    s.preDelay = 0.0f;
    for (int n = 0; n < warmup; ++n)
    {
        if (n % 512 == 0) reverb->updateForBlock(s, 512);
        float l = 0.0f, r = 0.0f;
        reverb->processSampleFrame(0.0f, 0.0f, l, r);
    }
    std::printf("running before impulse: %d\n", reverb->isRunning() ? 1 : 0);
    for (int n = 0; n < 2000; ++n)
    {
        if ((n + warmup) % 512 == 0) reverb->updateForBlock(s, 512);
        float l = 0.0f, r = 0.0f;
        reverb->processSampleFrame(n == 0 ? 1.0f : 0.0f, n == 0 ? 1.0f : 0.0f, l, r);
        if (std::abs(l) + std::abs(r) > 1.0e-5f) { std::printf("first wet at %d (%.3f ms), running %d\n", n, n / 48.0, reverb->isRunning() ? 1 : 0); return 0; }
    }
    return 0;
}
