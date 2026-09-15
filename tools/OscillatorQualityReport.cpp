// PX3Diag oscquality - the oscillator measurement report.
//
// Everything the design record (docs/OSCILLATOR_DSP_DESIGN.md) promises to
// measure, measured on the shipping OscillatorUnit and SubOscillator: pitch
// accuracy, aliasing at the oscillator and after the voice's source stage, DC,
// behaviour across sample rates, noise, per-mode CPU, and spectrograms of pitch
// sweeps written as PNGs.
//
//   PX3Diag oscquality [output directory]

#include "OscillatorDsp.h"
#include "OscillatorUnit.h"
#include "SubOscillator.h"

#include <JuceHeader.h>

#include <chrono>
#include <cstdio>
#include <vector>

namespace
{
using Mode = px3::OscillatorMode;

struct Setup
{
    const char* name;
    int mode;
    float a, b, c;
    float wheel;
    int gridDivisor;     // harmonic grid = fundamental / this
    bool harmonic;       // whether an alias figure means anything for it
    const char* note;
};

std::vector<Setup> setups()
{
    const auto syncA = static_cast<float>(std::pow(1.5 / 10.0, 1.0 / 1.3));
    const auto fmA = static_cast<float>(std::pow(2.6 / 3.8, 1.0 / 1.1));
    const auto fmB = static_cast<float>(std::pow(0.5, 1.0 / 1.35));
    const auto pwmQuarter = static_cast<float>(std::pow(0.15 / 0.8, 1.0 / 1.15));
    return {
        { "SINE", 0, 0.5f, 0.5f, 0.5f, 0.0f, 1, true, "" },
        { "SAW", 1, 0.5f, 0.5f, 0.5f, 0.0f, 1, true, "" },
        { "SQUARE", 2, 0.5f, 0.5f, 0.5f, 0.0f, 1, true, "" },
        { "TRIANGLE", 3, 0.5f, 0.5f, 0.5f, 0.0f, 1, true, "" },
        { "NOISE", 4, 0.5f, 0.5f, 0.5f, 0.0f, 1, false, "no harmonic series" },
        { "PINK NOISE", 5, 0.5f, 0.5f, 0.5f, 0.0f, 1, false, "no harmonic series" },
        { "SUPER SAW", 6, 0.5f, 0.5f, 0.5f, 0.0f, 1, false, "detuned by design" },
        { "PWM 25%", 7, pwmQuarter, 0.5f, 0.5f, 0.5f, 1, true, "" },
        { "WAVETABLE", 8, 0.5f, 0.5f, 0.5f, 0.0f, 1, false, "measured in WAVETABLE_OSCILLATOR_DESIGN.md" },
        { "ADDITIVE", 9, 0.5f, 0.5f, 0.5f, 0.0f, 1, true, "" },
        { "FORMANT", 10, 0.5f, 0.5f, 0.5f, 0.0f, 1, true, "" },
        { "FM r2 i5", 11, fmA, fmB, 0.5f, 0.0f, 1, true, "" },
        { "HARD SYNC r2.5", 12, syncA, 0.5f, 0.5f, 0.0f, 1, true, "" },
        { "ORGAN", 13, 0.5f, 0.0f, 0.5f, 0.0f, 2, true, "" },
        { "DIGITAL", 14, 0.5f, 0.5f, 0.5f, 0.0f, 1, true, "hold aliasing is intentional" },
        { "PHYSICAL", 15, 0.5f, 0.5f, 0.5f, 0.0f, 1, false, "inharmonic by design" },
        { "ROB", 16, 0.5f, 0.5f, 0.0f, 0.0f, 1, false, "inharmonic by design" },
        { "ISAAC", 17, 0.5f, 0.5f, 0.0f, 0.0f, 1, false, "shimmer partial is detuned by design" },
        { "PX3", 18, 0.5f, 0.5f, 0.0f, 0.0f, 1, false, "shimmer partial is detuned by design" },
    };
}

std::vector<double> renderUnit(const Setup& setup, double hz, double rate, int samples, int skip)
{
    auto unit = std::make_unique<OscillatorUnit>();
    unit->prepare(rate);
    OscillatorSettings settings;
    settings.modeIndex = setup.mode;
    settings.macroA = setup.a;
    settings.macroB = setup.b;
    settings.macroC = setup.c;
    unit->setSettings(settings);
    unit->resetForNote(0.0, 12345u);
    OscillatorUnit::RenderContext context;
    context.frequencyHz = hz;
    context.modWheelNorm = setup.wheel;
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < skip + samples; ++i)
    {
        context.noteAgeSamples = i;
        const auto v = unit->renderSample(context);
        if (i >= skip) { out.push_back(v); }
    }
    return out;
}

std::vector<double> power(const std::vector<double>& x, int from, int order, bool window)
{
    const auto size = 1 << order;
    juce::dsp::FFT fft(order);
    // juce::dsp::FFT is float; the dynamic range this report needs (to -140 dB)
    // is inside float's, so a float transform of double data is enough.
    std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        const auto w = window ? 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / size) : 1.0;
        data[static_cast<std::size_t>(i)] = static_cast<float>(x[static_cast<std::size_t>(from + i)] * w);
    }
    fft.performFrequencyOnlyForwardTransform(data.data());
    std::vector<double> p(static_cast<std::size_t>(size / 2));
    for (int k = 0; k < size / 2; ++k)
    {
        p[static_cast<std::size_t>(k)] = static_cast<double>(data[static_cast<std::size_t>(k)]) * data[static_cast<std::size_t>(k)];
    }
    return p;
}

struct AliasFigures
{
    double total, below15k, worst;
};

// FM's ratio comes from float macro maths, 2 +- 1e-6, so its sidebands leak a
// bin either side of the harmonic without being aliases: those are not counted.
AliasFigures alias(const std::vector<double>& p, int grid, int fundamental, double rate, int guardBins)
{
    const auto size = static_cast<double>(p.size() * 2);
    double total = 0.0, below = 0.0, worst = 0.0;
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        const auto offset = static_cast<int>(k % static_cast<std::size_t>(grid));
        if (std::min(offset, grid - offset) <= guardBins) { continue; }
        total += p[k];
        if (static_cast<double>(k) * rate / size < 15000.0) { below += p[k]; }
        worst = std::max(worst, p[k]);
    }
    const auto f = std::max(1.0e-30, p[static_cast<std::size_t>(fundamental)]);
    const auto db = [f](double v) { return 10.0 * std::log10(std::max(1.0e-30, v) / f); };
    return { db(total), db(below), db(worst) };
}

double zeroCrossingHz(const std::vector<double>& signal, double rate)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (std::size_t i = 1; i < signal.size(); ++i)
    {
        if (signal[i - 1] < 0.0 && signal[i] >= 0.0)
        {
            const auto t = static_cast<double>(i - 1) + (-signal[i - 1]) / (signal[i] - signal[i - 1]);
            if (first < 0.0) { first = t; }
            last = t;
            ++crossings;
        }
    }
    return crossings > 1 ? static_cast<double>(crossings - 1) * rate / (last - first) : 0.0;
}

double midiHz(int note)
{
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

void writeSpectrogram(const juce::File& file, const std::vector<double>& signal, double rate)
{
    constexpr int order = 11;
    constexpr int hop = 512;
    const auto size = 1 << order;
    const auto frames = static_cast<int>((signal.size() - static_cast<std::size_t>(size)) / hop);
    constexpr int height = 512;
    juce::Image image(juce::Image::RGB, frames, height, true);
    for (int f = 0; f < frames; ++f)
    {
        const auto p = power(signal, f * hop, order, true);
        for (int y = 0; y < height; ++y)
        {
            const auto bin = static_cast<std::size_t>((height - 1 - y) * static_cast<int>(p.size()) / height);
            const auto db = 10.0 * std::log10(std::max(1.0e-30, p[bin] / (size * size * 0.0625)));
            const auto t = juce::jlimit(0.0, 1.0, (db + 120.0) / 120.0);
            // Dark to bright, blue through yellow, so -60 and -100 are told apart.
            const auto r = static_cast<juce::uint8>(255.0 * juce::jlimit(0.0, 1.0, 1.8 * t - 0.6));
            const auto g = static_cast<juce::uint8>(255.0 * juce::jlimit(0.0, 1.0, 1.6 * t - 0.35));
            const auto b = static_cast<juce::uint8>(255.0 * juce::jlimit(0.0, 1.0, t < 0.5 ? 1.6 * t : 1.6 * (1.0 - t)));
            image.setPixelAt(f, y, juce::Colour(r, g, b));
        }
    }
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat().writeImageToStream(image, stream);
    juce::ignoreUnused(rate);
}
} // namespace

int runOscillatorQualityReport(const juce::String& outputDirectory)
{
    const auto modes = setups();
    std::printf("\nPX3 OSCILLATOR QUALITY REPORT\n");

    // ---- pitch -------------------------------------------------------------------
    std::printf("\nPITCH - cents error, zero-crossing interpolated over 2 s\n");
    std::printf("  %-14s %8s", "source", "rate");
    for (int note = 36; note <= 120; note += 12) { std::printf("  MIDI%-4d", note); }
    std::printf("\n");
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        for (const auto index : { 0, 1, 2, 3 })
        {
            std::printf("  %-14s %6.1fk", modes[static_cast<std::size_t>(index)].name, rate / 1000.0);
            for (int note = 36; note <= 120; note += 12)
            {
                const auto x = renderUnit(modes[static_cast<std::size_t>(index)], midiHz(note), rate, static_cast<int>(rate * 2.0), 2048);
                std::printf("  %+8.4f", 1200.0 * std::log2(zeroCrossingHz(x, rate) / midiHz(note)));
            }
            std::printf("\n");
        }
        for (const auto waveform : { 0, 1 })
        {
            SubOscillator sub;
            sub.prepare(rate);
            SubOscSettings settings;
            settings.enabled = true;
            settings.level = 1.0f;
            settings.coarseOctaves = 0.0f;
            settings.waveformIndex = waveform;
            std::printf("  %-14s %6.1fk", waveform == 0 ? "SUB SINE" : "SUB SQUARE", rate / 1000.0);
            for (int note = 36; note <= 120; note += 12)
            {
                sub.setSettings(settings);
                sub.resetForNote(0.0);
                std::vector<double> x;
                for (int i = 0; i < static_cast<int>(rate * 2.0) + 2048; ++i)
                {
                    const auto v = sub.renderSample(midiHz(note));
                    if (i >= 2048) { x.push_back(v); }
                }
                std::printf("  %+8.4f", 1200.0 * std::log2(zeroCrossingHz(x, rate) / midiHz(note)));
            }
            std::printf("\n");
        }
    }

    // ---- aliasing ----------------------------------------------------------------
    for (const auto rate : { 48000.0, 96000.0 })
    {
        std::printf("\nALIAS at %.0f Hz - bin-snapped tones; dB against the fundamental: total / below 15 kHz / worst bin\n", rate);
        std::printf("  oscillator output, then after the voice's source stage (the ADAA soft clip)\n");
        constexpr int order = 15;
        const auto size = 1 << order;
        for (const auto& setup : modes)
        {
            if (!setup.harmonic)
            {
                std::printf("  %-16s n/a: %s\n", setup.name, setup.note);
                continue;
            }
            for (const auto stage : { 0, 1 })
            {
                std::printf("  %-16s %-6s", stage == 0 ? setup.name : "", stage == 0 ? "osc" : "voice");
                for (int note = 36; note <= 120; note += 12)
                {
                    auto grid = static_cast<int>(std::lround(midiHz(note) * size / rate / setup.gridDivisor));
                    if (grid % 2 == 0) { ++grid; }
                    const auto fundamental = grid * setup.gridDivisor;
                    const auto hz = fundamental * rate / size;
                    auto x = renderUnit(setup, hz, rate, 2 * size, 16384);
                    if (stage == 1)
                    {
                        px3::dsp::Adaa clip;
                        for (auto& v : x) { v = clip.process(px3::dsp::kSourceClipAdaa, v); }
                    }
                    const auto p = power(x, size, order, false);
                    const auto figures = alias(p, grid, fundamental, rate, setup.mode == static_cast<int>(Mode::fm) ? 2 : 0);
                    std::printf("  %6.1f/%6.1f/%6.1f", figures.total, figures.below15k, figures.worst);
                }
                std::printf("   %s\n", stage == 0 ? setup.note : "");
            }
        }
        std::printf("  columns: MIDI 36 48 60 72 84 96 108 120\n");
    }

    // ---- DC ------------------------------------------------------------------------
    std::printf("\nDC - mean over RMS, %%, at 110 Hz, all macros at 0 / 0.5 / 1\n");
    for (const auto& setup : modes)
    {
        std::printf("  %-16s", setup.name);
        for (const auto macro : { 0.0f, 0.5f, 1.0f })
        {
            auto s = setup;
            s.a = s.b = s.c = macro;
            const auto x = renderUnit(s, 110.0, 48000.0, 96000, 9600);
            double sum = 0.0, energy = 0.0;
            for (const auto v : x) { sum += v; energy += v * v; }
            const auto rms = std::sqrt(energy / static_cast<double>(x.size()));
            std::printf("  %8.4f", rms > 0.0 ? 100.0 * std::abs(sum / static_cast<double>(x.size())) / rms : 0.0);
        }
        std::printf("\n");
    }

    // ---- sample rates ----------------------------------------------------------------
    std::printf("\nSAMPLE RATE - 220 Hz: 50 Hz-10 kHz level (dB against 48 kHz) and spectral centroid (Hz)\n");
    std::printf("  %-16s %20s %20s %20s %20s\n", "mode", "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz");
    for (const auto& setup : modes)
    {
        std::printf("  %-16s", setup.name);
        double reference = 0.0;
        auto referenceTaken = false;
        for (const auto rate : { 48000.0, 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const auto order = rate > 50000.0 ? 17 : 16;
            const auto x = renderUnit(setup, 220.0, rate, 1 << order, static_cast<int>(rate * 0.1));
            const auto p = power(x, 0, order, true);
            const auto binHz = rate / (1 << order);
            double band = 0.0, weighted = 0.0;
            for (std::size_t k = 1; k < p.size(); ++k)
            {
                const auto hz = static_cast<double>(k) * binHz;
                if (hz >= 50.0 && hz <= 10000.0) { band += p[k]; weighted += hz * p[k]; }
            }
            const auto level = 10.0 * std::log10(std::max(1.0e-30, band / (static_cast<double>(p.size()) * static_cast<double>(p.size()))));
            if (!referenceTaken) { reference = level; referenceTaken = true; continue; }
            std::printf("   %+6.2f dB %7.0f Hz", level - reference, band > 0.0 ? weighted / band : 0.0);
        }
        std::printf("\n");
    }

    // ---- CPU ---------------------------------------------------------------------------
    std::printf("\nCPU - one OscillatorUnit, ns per sample, 220 Hz, macros at 0.5 (FM and PX3 include their 2x decimator)\n");
    for (const auto rate : { 48000.0, 96000.0 })
    {
        std::printf("  %.0f Hz\n", rate);
        for (const auto& setup : modes)
        {
            auto unit = std::make_unique<OscillatorUnit>();
            unit->prepare(rate);
            OscillatorSettings settings;
            settings.modeIndex = setup.mode;
            settings.macroA = setup.a;
            settings.macroB = setup.b;
            settings.macroC = setup.c;
            unit->setSettings(settings);
            unit->resetForNote(0.0, 1u);
            OscillatorUnit::RenderContext context;
            context.frequencyHz = 220.0;
            constexpr int samples = 1 << 21;
            volatile double sink = 0.0;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < samples; ++i)
            {
                context.noteAgeSamples = i;
                sink = unit->renderSample(context);
            }
            const auto end = std::chrono::steady_clock::now();
            juce::ignoreUnused(sink);
            std::printf("    %-16s %7.2f\n", setup.name, std::chrono::duration<double, std::nano>(end - start).count() / samples);
        }
        SubOscillator sub;
        sub.prepare(rate);
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 1.0f;
        settings.waveformIndex = 1;
        sub.setSettings(settings);
        sub.resetForNote(0.0);
        constexpr int samples = 1 << 21;
        volatile double sink = 0.0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < samples; ++i) { sink = sub.renderSample(220.0); }
        const auto end = std::chrono::steady_clock::now();
        juce::ignoreUnused(sink);
        std::printf("    %-16s %7.2f\n", "SUB SQUARE", std::chrono::duration<double, std::nano>(end - start).count() / samples);

        px3::dsp::Adaa clip;
        double x = 0.0;
        const auto clipStart = std::chrono::steady_clock::now();
        for (int i = 0; i < samples; ++i)
        {
            x += 0.0137; if (x > 1.0) x -= 2.0;
            sink = clip.process(px3::dsp::kSourceClipAdaa, 0.75 * x);
        }
        const auto clipEnd = std::chrono::steady_clock::now();
        std::printf("    %-16s %7.2f\n", "voice source ADAA", std::chrono::duration<double, std::nano>(clipEnd - clipStart).count() / samples);
    }

    // ---- spectrograms --------------------------------------------------------------------
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile(outputDirectory);
    directory.createDirectory();
    std::printf("\nSPECTROGRAMS - MIDI 24 to 120 over 6 s at 48 kHz, 0 to 24 kHz, -120 to 0 dB, written to %s\n",
                directory.getFullPathName().toRawUTF8());
    constexpr double rate = 48000.0;
    const auto sweepSamples = static_cast<int>(rate * 6.0);
    const auto sweepHz = [](int i) { return midiHz(24) * std::pow(2.0, (96.0 / 12.0) * i / sweepSamples); };
    for (const auto& setup : modes)
    {
        if (setup.mode == static_cast<int>(Mode::wavetable)) { continue; }
        auto unit = std::make_unique<OscillatorUnit>();
        unit->prepare(rate);
        OscillatorSettings settings;
        settings.modeIndex = setup.mode;
        settings.macroA = setup.a;
        settings.macroB = setup.b;
        settings.macroC = setup.c;
        unit->setSettings(settings);
        unit->resetForNote(0.0, 1u);
        OscillatorUnit::RenderContext context;
        context.modWheelNorm = setup.wheel;
        std::vector<double> x;
        x.reserve(static_cast<std::size_t>(sweepSamples));
        for (int i = 0; i < sweepSamples; ++i)
        {
            context.frequencyHz = sweepHz(i);
            context.noteAgeSamples = i;
            x.push_back(unit->renderSample(context));
        }
        const auto file = directory.getChildFile(juce::String(setup.name).replaceCharacters(" %.", "___") + ".png");
        writeSpectrogram(file, x, rate);
        std::printf("  %s\n", file.getFileName().toRawUTF8());
    }
    {
        // The naive saw PX3 shipped, for comparison.
        std::vector<double> x;
        double phase = 0.0;
        for (int i = 0; i < sweepSamples; ++i)
        {
            phase += sweepHz(i) / rate;
            phase -= std::floor(phase);
            x.push_back((2.0 * phase - 1.0) * 0.6);
        }
        const auto file = directory.getChildFile("reference_naive_saw.png");
        writeSpectrogram(file, x, rate);
        std::printf("  %s\n", file.getFileName().toRawUTF8());
    }

    std::printf("\n");
    std::fflush(stdout);
    return 0;
}

// ---- focused tools for CPU work --------------------------------------------------------
namespace
{
std::vector<Setup> pickSetups(const juce::String& filter)
{
    std::vector<Setup> chosen;
    for (const auto& setup : setups())
    {
        if (filter.isEmpty() || juce::StringArray::fromTokens(filter, ",", "").contains(setup.name, true)
            || juce::String(setup.name).startsWithIgnoreCase(filter))
        {
            chosen.push_back(setup);
        }
    }
    return chosen;
}
} // namespace

// PX3Diag oscbench [MODE,MODE...]: ns per sample for one OscillatorUnit per mode at
// 48 kHz, best of five runs so scheduler noise does not set the figure.
int runOscillatorBench(const juce::String& filter)
{
    for (const auto& setup : pickSetups(filter))
    {
        auto best = 1.0e30;
        for (int run = 0; run < 5; ++run)
        {
            auto unit = std::make_unique<OscillatorUnit>();
            unit->prepare(48000.0);
            OscillatorSettings settings;
            settings.modeIndex = setup.mode;
            settings.macroA = setup.a;
            settings.macroB = setup.b;
            settings.macroC = setup.c;
            unit->setSettings(settings);
            unit->resetForNote(0.0, 1u);
            OscillatorUnit::RenderContext context;
            context.frequencyHz = 220.0;
            constexpr int samples = 1 << 20;
            volatile double sink = 0.0;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < samples; ++i)
            {
                context.noteAgeSamples = i;
                sink = unit->renderSample(context);
            }
            const auto end = std::chrono::steady_clock::now();
            juce::ignoreUnused(sink);
            best = std::min(best, std::chrono::duration<double, std::nano>(end - start).count() / samples);
        }
        std::printf("  %-16s %7.2f ns/sample\n", setup.name, best);
    }
    return 0;
}

// PX3Diag oscdump <file>: raw doubles from every mode, at two pitches, with a
// macro change ramped in halfway - the evidence that an optimisation left the
// sound alone.
int runOscillatorDump(const juce::String& path)
{
    juce::File file(path);
    file.deleteFile();
    juce::FileOutputStream out(file);
    std::size_t total = 0;
    for (const auto& setup : setups())
    {
        for (const auto hz : { 220.0, 1500.0 })
        {
            auto unit = std::make_unique<OscillatorUnit>();
            unit->prepare(48000.0);
            OscillatorSettings settings;
            settings.modeIndex = setup.mode;
            settings.macroA = setup.a;
            settings.macroB = setup.b;
            settings.macroC = setup.c;
            unit->setSettings(settings);
            unit->resetForNote(0.0, 7u);
            OscillatorUnit::RenderContext context;
            context.frequencyHz = hz;
            context.modWheelNorm = setup.wheel;
            for (int i = 0; i < 48000; ++i)
            {
                if (i == 24000)
                {
                    settings.macroA = 1.0f - setup.a * 0.6f;
                    settings.macroB = 0.2f + setup.b * 0.5f;
                    settings.macroC = 0.9f - setup.c * 0.8f;
                    unit->setSettings(settings, 512);
                }
                context.noteAgeSamples = i;
                const auto v = unit->renderSample(context);
                out.write(&v, sizeof(v));
                ++total;
            }
        }
    }
    std::printf("  wrote %zu samples to %s\n", total, file.getFullPathName().toRawUTF8());
    return 0;
}

