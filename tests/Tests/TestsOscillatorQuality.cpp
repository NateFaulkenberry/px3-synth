#include "TestSupport.h"
#include "OscillatorDsp.h"
#include "OscillatorUnit.h"

// testOscillatorQuality - a regression test for every bug the oscillator audit
// confirmed, and the measured quality envelope of the rebuilt oscillators
// (docs/OSCILLATOR_DSP_DESIGN.md holds the measurements these defend).

namespace px3tests
{
namespace
{
using Mode = px3::OscillatorMode;

struct UnitSetup
{
    int mode { 0 };
    float a { 0.5f };
    float b { 0.5f };
    float c { 0.5f };
    int vowel { 0 };
    float wheel { 0.0f };
    std::uint32_t seed { 1u };
};

std::vector<double> renderUnit(const UnitSetup& setup, double hz, double sampleRate, int samples, int skip = 4096)
{
    auto unit = std::make_unique<OscillatorUnit>();
    unit->prepare(sampleRate);
    OscillatorSettings settings;
    settings.modeIndex = setup.mode;
    settings.macroA = setup.a;
    settings.macroB = setup.b;
    settings.macroC = setup.c;
    settings.vowelIndex = setup.vowel;
    unit->setSettings(settings);
    unit->resetForNote(0.0, setup.seed);

    OscillatorUnit::RenderContext context;
    context.frequencyHz = hz;
    context.modWheelNorm = setup.wheel;

    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < skip + samples; ++i)
    {
        context.noteAgeSamples = i;
        const auto v = unit->renderSample(context);
        if (i >= skip)
        {
            out.push_back(v);
        }
    }
    return out;
}

// Power of an exactly periodic frame: no window, so nothing smears.
std::vector<double> periodicPower(const std::vector<double>& x, int order)
{
    const auto size = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        data[static_cast<std::size_t>(i)] = static_cast<float>(x[static_cast<std::size_t>(i)]);
    }
    fft.performFrequencyOnlyForwardTransform(data.data());
    std::vector<double> p(static_cast<std::size_t>(size / 2));
    for (int k = 0; k < size / 2; ++k)
    {
        p[static_cast<std::size_t>(k)] = static_cast<double>(data[static_cast<std::size_t>(k)]) * data[static_cast<std::size_t>(k)];
    }
    return p;
}

// Blackman-Harris (4-term, -92 dB sidelobes) power, for a tone that is not
// exactly periodic in the frame but whose aliases have to be told apart from
// its harmonics' leakage.
std::vector<double> blackmanHarrisPower(const std::vector<double>& x, int order)
{
    const auto size = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        const auto t = 2.0 * juce::MathConstants<double>::pi * i / size;
        const auto w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
        data[static_cast<std::size_t>(i)] = static_cast<float>(x[static_cast<std::size_t>(i)] * w);
    }
    fft.performFrequencyOnlyForwardTransform(data.data());
    std::vector<double> p(static_cast<std::size_t>(size / 2));
    for (int k = 0; k < size / 2; ++k)
    {
        p[static_cast<std::size_t>(k)] = static_cast<double>(data[static_cast<std::size_t>(k)]) * data[static_cast<std::size_t>(k)];
    }
    return p;
}

// Hann-windowed power, for signals that are not periodic in the frame.
std::vector<double> windowedPower(const std::vector<double>& x, int from, int order)
{
    const auto size = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / size);
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

double centroidHz(const std::vector<double>& p, double sampleRate, double lowHz, double highHz)
{
    const auto binHz = sampleRate / static_cast<double>(p.size() * 2);
    double weighted = 0.0, total = 0.0;
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        const auto hz = static_cast<double>(k) * binHz;
        if (hz < lowHz || hz > highHz) { continue; }
        weighted += hz * p[k];
        total += p[k];
    }
    return total > 0.0 ? weighted / total : 0.0;
}

double bandLevelDb(const std::vector<double>& p, double sampleRate, double lowHz, double highHz)
{
    const auto binHz = sampleRate / static_cast<double>(p.size() * 2);
    double total = 0.0;
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        const auto hz = static_cast<double>(k) * binHz;
        if (hz >= lowHz && hz <= highHz) { total += p[k]; }
    }
    // Normalised by the frame length so frames of different lengths compare.
    return 10.0 * std::log10(juce::jmax(1.0e-30, total / static_cast<double>(p.size() * p.size())));
}

double peakDbNear(const std::vector<double>& p, double sampleRate, double hz, int spreadBins = 2)
{
    const auto binHz = sampleRate / static_cast<double>(p.size() * 2);
    const auto centre = static_cast<int>(std::lround(hz / binHz));
    double best = 1.0e-30;
    for (int k = centre - spreadBins; k <= centre + spreadBins; ++k)
    {
        if (k > 0 && k < static_cast<int>(p.size())) { best = juce::jmax(best, p[static_cast<std::size_t>(k)]); }
    }
    return 10.0 * std::log10(best);
}

struct Alias
{
    double total { 0.0 };
    double below15k { 0.0 };
};

// `guardBins` either side of each harmonic are not counted: for a mode whose
// ratio comes from float macro maths (FM's 2 is 2 +- 1e-6), sidebands drift a
// fraction of a bin and leak beside the harmonic without being aliases.
Alias aliasAgainstFundamental(const std::vector<double>& p, int grid, int fundamentalBin, double sampleRate, int guardBins = 0)
{
    const auto size = static_cast<double>(p.size() * 2);
    double alias = 0.0, below = 0.0;
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        const auto offset = static_cast<int>(k % static_cast<std::size_t>(grid));
        if (juce::jmin(offset, grid - offset) <= guardBins) { continue; }
        alias += p[k];
        if (static_cast<double>(k) * sampleRate / size < 15000.0) { below += p[k]; }
    }
    const auto fundamental = juce::jmax(1.0e-30, p[static_cast<std::size_t>(fundamentalBin)]);
    return { 10.0 * std::log10(juce::jmax(1.0e-30, alias) / fundamental),
             10.0 * std::log10(juce::jmax(1.0e-30, below) / fundamental) };
}

// An odd FFT bin near `hz`, so folded components cannot land on a harmonic.
int oddBinNear(double hz, double sampleRate, int order)
{
    auto bin = juce::jmax(1, static_cast<int>(std::lround(hz * (1 << order) / sampleRate)));
    return (bin % 2) == 0 ? bin + 1 : bin;
}

double zeroCrossingHz(const std::vector<double>& signal, double sampleRate)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (std::size_t i = 1; i < signal.size(); ++i)
    {
        const auto a = signal[i - 1];
        const auto b = signal[i];
        if (a < 0.0 && b >= 0.0)
        {
            const auto t = static_cast<double>(i - 1) + (-a) / (b - a);
            if (first < 0.0) { first = t; }
            last = t;
            ++crossings;
        }
    }
    return crossings > 1 ? static_cast<double>(crossings - 1) * sampleRate / (last - first) : 0.0;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = 0; i < juce::jmin(a.size(), b.size()); ++i)
    {
        ab += a[i] * b[i];
        aa += a[i] * a[i];
        bb += b[i] * b[i];
    }
    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt(aa * bb) : 0.0;
}

juce::String modeName(int mode)
{
    return px3::oscillatorModeChoices()[mode];
}
} // namespace

void testOscillatorQuality()
{
    suite("OSCILLATOR QUALITY");
    constexpr double fs = 48000.0;

    // ---- the mod wheel is not a volume control ------------------------------------
    {
        juce::StringArray moved;
        for (int mode = 0; mode < px3::oscillatorModeCount; ++mode)
        {
            if (mode == static_cast<int>(Mode::pwm)) { continue; }
            UnitSetup rest;
            rest.mode = mode;
            auto full = rest;
            full.wheel = 1.0f;
            if (renderUnit(rest, 220.0, fs, 4800) != renderUnit(full, 220.0, fs, 4800)) { moved.add(modeName(mode)); }
        }
        check("OscQuality_ModWheelChangesNoModeButPwm", moved.isEmpty(),
              moved.isEmpty() ? juce::String("every mode but PWM renders identically with the wheel at rest and at full")
                              : "the wheel changed: " + moved.joinIntoString(", "));

        UnitSetup rest;
        rest.mode = static_cast<int>(Mode::pwm);
        rest.a = 0.4f;
        auto full = rest;
        full.wheel = 1.0f;
        check("OscQuality_ModWheelStillMovesPwmWidth", renderUnit(rest, 220.0, fs, 4800) != renderUnit(full, 220.0, fs, 4800),
              "PWM with the wheel at rest and at full renders differently");
    }

    // ---- a hidden macro knob cannot change the sound -----------------------------------
    {
        juce::StringArray changed;
        for (int mode = 0; mode < px3::oscillatorModeCount; ++mode)
        {
            const auto count = px3::oscillatorModeMacroCount(mode);
            if (count == 3) { continue; }
            UnitSetup shown;
            shown.mode = mode;
            shown.a = 0.3f;
            shown.b = 0.6f;
            shown.c = 0.4f;
            auto hidden = shown;
            if (count < 1) { hidden.a = 0.95f; }
            if (count < 2) { hidden.b = 0.05f; }
            hidden.c = 0.9f;
            if (renderUnit(shown, 220.0, fs, 4800) != renderUnit(hidden, 220.0, fs, 4800)) { changed.add(modeName(mode)); }
        }
        check("OscQuality_HiddenMacrosDoNotChangeTheSound", changed.isEmpty(),
              changed.isEmpty() ? juce::String("every mode with fewer than three knobs ignores the ones it hides")
                                : "a hidden knob changed: " + changed.joinIntoString(", "));
    }

    // ---- ORGAN's 16' and 5 1/3' are real sub-octave and quint partials --------------------
    {
        constexpr int order = 15;
        const auto size = 1 << order;
        constexpr int grid = 37;   // odd; the grid is HALF the note, where the 16' sits
        const auto hz = 2.0 * grid * fs / size;
        UnitSetup organ;
        organ.mode = static_cast<int>(Mode::organ);
        organ.b = 0.0f;   // no key click: nothing but drawbars
        const auto p = periodicPower(renderUnit(organ, hz, fs, size, 8192), order);
        const auto fundamental = p[static_cast<std::size_t>(2 * grid)];
        const auto sub = 10.0 * std::log10(p[static_cast<std::size_t>(grid)] / fundamental);
        const auto quint = 10.0 * std::log10(p[static_cast<std::size_t>(3 * grid)] / fundamental);
        double off = 0.0, total = 0.0;
        for (std::size_t k = 1; k < p.size(); ++k)
        {
            total += p[k];
            if (k % grid != 0) { off += p[k]; }
        }
        const auto offDb = 10.0 * std::log10(juce::jmax(1.0e-30, off) / total);
        check("OscQuality_Organ16And513DrawbarsSitAtHalfAndOneAndAHalf",
              sub > -30.0 && quint > -30.0 && offDb < -60.0,
              "16' " + fmt(sub, 1) + " dB and 5 1/3' " + fmt(quint, 1) + " dB against 8'; energy off the half-note grid "
                  + fmt(offDb, 1) + " dB");
    }

    // ---- stretched partials keep their own frequency --------------------------------------
    {
        constexpr int order = 16;
        UnitSetup isaac;
        isaac.mode = static_cast<int>(Mode::isaac);
        isaac.a = 0.0f;
        isaac.c = 1.0f;   // full STRETCH: the 8th partial at 8 * (1 + 1.1 * 0.03 * 8)
        const auto p = windowedPower(renderUnit(isaac, 110.0, fs, 1 << order, 8192), 0, order);
        const auto stretched = peakDbNear(p, fs, 110.0 * 8.0 * (1.0 + 1.1 * 0.03 * 8.0));
        const auto harmonic = peakDbNear(p, fs, 1100.0);
        check("OscQuality_IsaacStretchedPartialsAreReallyInharmonic", stretched > harmonic + 15.0,
              "at the stretched 8th partial " + fmt(stretched, 1) + " dB, at the 10th harmonic it used to fold onto "
                  + fmt(harmonic, 1) + " dB");

        UnitSetup rob;
        rob.mode = static_cast<int>(Mode::rob);
        rob.a = 0.0f;
        rob.b = 0.5f;
        rob.c = 0.0f;
        const auto stretch = 1.0 + 0.45 * std::pow(0.5, 0.72);
        const auto q = windowedPower(renderUnit(rob, 110.0, fs, 1 << order, 48000), 0, order);
        const auto body = peakDbNear(q, fs, 110.0 * stretch);
        const auto note = peakDbNear(q, fs, 110.0);
        check("OscQuality_RobBodySitsAtItsStretchedRatio", body > note + 10.0,
              "at " + fmt(stretch, 3) + "x the note " + fmt(body, 1) + " dB, at the note itself " + fmt(note, 1) + " dB");
    }

    // ---- every source starts at the same phase ----------------------------------------------
    {
        const auto rmsOf = [](std::initializer_list<const char*> oscillators, bool sub)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            for (const auto* slot : { "1", "2", "3" })
            {
                auto on = false;
                for (const auto* chosen : oscillators) { on = on || juce::String(chosen) == slot; }
                setParam(processor, juce::String("osc") + slot + "Enabled", on ? 1.0f : 0.0f);
            }
            setParam(processor, "subOscEnabled", sub ? 1.0f : 0.0f);
            setChoice(processor, "subOscWaveform", 0);
            setParam(processor, "subOscCoarse", 0.0f);
            return render(processor, 24000, { { 0, true, 57, 0.9f } }).rmsOver(8000, 22000);
        };
        const auto single = rmsOf({ "1" }, false);
        const auto three = rmsOf({ "1", "2", "3" }, false);
        const auto withSub = rmsOf({ "1" }, true);
        const auto subAlone = rmsOf({}, true);

        // In phase, three normalised sources sum to sqrt(3) of one; 120 degrees
        // apart - how they used to start - they cancel.
        check("OscQuality_ThreeOscillatorsStartInPhase", three > single * 1.6,
              "one sine " + fmt(single, 4) + ", three at the same pitch " + fmt(three, 4) + " (x" + fmt(three / single, 3) + ")");
        check("OscQuality_TheSubStartsInPhaseWithTheOscillators", withSub > single * 1.3,
              "osc alone " + fmt(single, 4) + ", with the sub at the same pitch " + fmt(withSub, 4) + " (x" + fmt(withSub / single, 3) + ")");
        check("OscQuality_TheSubSitsAtAnOscillatorsLevel", std::abs(20.0 * std::log10(subAlone / single)) < 1.0,
              "sub sine " + fmt(20.0 * std::log10(subAlone / single), 2) + " dB against an oscillator sine");
    }

    // ---- FORMANT morphs onward from the selected vowel ---------------------------------------
    {
        juce::StringArray wrong;
        for (int vowel = 0; vowel < 5; ++vowel)
        {
            UnitSetup quarter;
            quarter.mode = static_cast<int>(Mode::formant);
            quarter.vowel = vowel;
            quarter.a = 0.25f;
            auto next = quarter;
            next.vowel = (vowel + 1) % 5;
            next.a = 0.0f;
            // Compared by shape: MORPH also enters the mode's level trim, so the
            // two differ by a gain and nothing else.
            const auto shape = correlation(renderUnit(quarter, 110.0, fs, 9600), renderUnit(next, 110.0, fs, 9600));
            if (shape < 0.99999)
            {
                wrong.add(juce::String(vowel) + " (" + fmt(shape, 6) + ")");
            }
        }
        check("OscQuality_FormantMorphStepsToTheNextVowel", wrong.isEmpty(),
              wrong.isEmpty() ? juce::String("a quarter of MORPH is the next vowel's waveform, from every vowel")
                              : "not the next vowel from vowel " + wrong.joinIntoString(", "));

        const auto centroidAt = [](float morph)
        {
            UnitSetup setup;
            setup.mode = static_cast<int>(Mode::formant);
            setup.vowel = 2;
            setup.a = morph;
            return centroidHz(windowedPower(renderUnit(setup, 110.0, 48000.0, 16384), 0, 14), 48000.0, 150.0, 4000.0);
        };
        const auto below = centroidAt(0.249f);
        const auto above = centroidAt(0.251f);
        check("OscQuality_FormantMorphIsContinuousAtAQuarter", std::abs(above - below) < 0.03 * below,
              "spectral centroid at MORPH 0.249 " + fmt(below, 1) + " Hz, at 0.251 " + fmt(above, 1) + " Hz");
    }

    // ---- macros ramp inside the block ---------------------------------------------------------
    {
        auto unit = std::make_unique<OscillatorUnit>();
        unit->prepare(fs);
        OscillatorSettings settings;
        settings.modeIndex = static_cast<int>(Mode::pwm);
        settings.macroA = 0.2f;
        unit->setSettings(settings);
        unit->resetForNote(0.0, 1u);

        OscillatorUnit::RenderContext context;
        context.frequencyHz = 1000.0;   // 48 samples a cycle
        context.modWheelNorm = 0.5f;    // no wheel offset on the width

        std::vector<double> y;
        for (int block = 0; block < 40; ++block)
        {
            settings.macroA = 0.2f + 0.01f * static_cast<float>(block);
            unit->setSettings(settings, 512);
            for (int i = 0; i < 512; ++i)
            {
                context.noteAgeSamples = static_cast<int>(y.size());
                y.push_back(unit->renderSample(context));
            }
        }

        std::vector<double> widths;
        for (int k = 24; k < static_cast<int>(y.size()) / 48 - 2; ++k)
        {
            const auto start = 48 * k + px3::dsp::kOscillatorLatencySamples;
            const auto high = y[static_cast<std::size_t>(start + 6)];
            const auto low = y[static_cast<std::size_t>(start + 40)];
            const auto threshold = 0.5 * (high + low);
            for (int n = start + 7; n < start + 36; ++n)
            {
                const auto before = y[static_cast<std::size_t>(n - 1)];
                const auto after = y[static_cast<std::size_t>(n)];
                if (before >= threshold && after < threshold)
                {
                    widths.push_back((static_cast<double>(n - 1 - start) + (before - threshold) / (before - after)) / 48.0);
                    break;
                }
            }
        }
        double sum = 0.0, largest = 0.0;
        for (std::size_t i = 1; i < widths.size(); ++i)
        {
            const auto step = widths[i] - widths[i - 1];
            sum += step;
            largest = juce::jmax(largest, step);
        }
        const auto mean = widths.size() > 1 ? sum / static_cast<double>(widths.size() - 1) : 0.0;
        // Stepped once per block, the width would sit still for ten cycles and
        // then jump: the largest step would be ten times the mean.
        check("OscQuality_MacroChangesRampAcrossTheBlock", mean > 0.0 && largest < 2.5 * mean,
              juce::String(static_cast<int>(widths.size())) + " cycles: mean width step " + fmt(mean * 100.0, 4)
                  + "%, largest " + fmt(largest * 100.0, 4) + "%");
    }

    // ---- ADDITIVE's ROLL changes the timbre, not the level --------------------------------------
    {
        std::array<double, 3> centroid {};
        std::array<double, 3> level {};
        const std::array<float, 3> rolls { { 0.0f, 0.5f, 1.0f } };
        for (std::size_t i = 0; i < rolls.size(); ++i)
        {
            UnitSetup additive;
            additive.mode = static_cast<int>(Mode::additive);
            additive.c = rolls[i];
            const auto x = renderUnit(additive, 220.0, fs, 32768);
            const auto p = windowedPower(x, 0, 15);
            centroid[i] = centroidHz(p, fs, 50.0, 10000.0);
            double energy = 0.0;
            for (const auto v : x) { energy += v * v; }
            level[i] = 10.0 * std::log10(energy / static_cast<double>(x.size()));
        }
        check("OscQuality_AdditiveRollMovesTheSpectrum", centroid[0] < 0.8 * centroid[1] && centroid[2] > 1.2 * centroid[1],
              "centroid at ROLL 0 / 0.5 / 1: " + fmt(centroid[0], 0) + " / " + fmt(centroid[1], 0) + " / " + fmt(centroid[2], 0) + " Hz");
        const auto spread = juce::jmax(level[0], level[1], level[2]) - juce::jmin(level[0], level[1], level[2]);
        check("OscQuality_AdditiveRollKeepsTheLevel", spread < 1.5,
              "level spread across ROLL " + fmt(spread, 2) + " dB");
    }

    // ---- noise: independent, reproducible ------------------------------------------------------
    {
        UnitSetup white;
        white.mode = static_cast<int>(Mode::noise);
        white.a = 1.0f;
        const auto a = renderUnit(white, 220.0, fs, 48000);
        auto other = white;
        other.seed = px3::dsp::streamSeed(1u, 1u, 0u);
        const auto b = renderUnit(other, 220.0, fs, 48000);
        check("OscQuality_EveryOscillatorHasItsOwnNoiseStream", std::abs(correlation(a, b)) < 0.02,
              "correlation between two voices' noise " + fmt(correlation(a, b), 4) + " (the shipped generator: 0.9999)");
        check("OscQuality_ANoiseStreamRepeatsFromItsSeed", a == renderUnit(white, 220.0, fs, 48000),
              "the same seed renders the same noise");

        for (const auto mode : { Mode::superSaw, Mode::physical, Mode::noise })
        {
            const auto once = [mode]
            {
                PX3SynthAudioProcessor processor;
                makePlainPatch(processor);
                setChoice(processor, "osc1Mode", static_cast<int>(mode));
                return render(processor, 24000, { { 0, true, 57, 0.9f }, { 4000, true, 64, 0.9f } }).left;
            };
            check((juce::String("OscQuality_") + modeName(static_cast<int>(mode)).removeCharacters(" ") + "RendersTheSameTwice").toRawUTF8(),
                  once() == once(), "two fresh processors given the same notes render identically");
        }
    }

    // ---- DC ----------------------------------------------------------------------------------------
    {
        juce::StringArray offset;
        auto worst = 0.0;
        for (int mode = 0; mode < px3::oscillatorModeCount; ++mode)
        {
            if (mode == static_cast<int>(Mode::wavetable) || mode == static_cast<int>(Mode::noise)
                || mode == static_cast<int>(Mode::pinkNoise))
            {
                continue;
            }
            for (const auto macro : { 0.0f, 0.5f, 1.0f })
            {
                UnitSetup setup;
                setup.mode = mode;
                setup.a = setup.b = setup.c = macro;
                const auto x = renderUnit(setup, 110.0, fs, 96000, 9600);
                double sum = 0.0, energy = 0.0;
                for (const auto v : x) { sum += v; energy += v * v; }
                const auto count = static_cast<double>(x.size());
                const auto ratio = std::abs(sum / count) / juce::jmax(1.0e-12, std::sqrt(energy / count));
                worst = juce::jmax(worst, ratio);
                if (ratio > 0.01) { offset.add(modeName(mode) + " @" + fmt(macro, 1) + " " + fmt(ratio * 100.0, 2) + "%"); }
            }
        }
        check("OscQuality_NoModeCarriesDc", offset.isEmpty(),
              offset.isEmpty() ? "worst mean over RMS " + fmt(worst * 100.0, 3) + "% across every mode and macro extreme"
                               : "DC above 1% of RMS: " + offset.joinIntoString(", "));
    }

    // ---- pitch ----------------------------------------------------------------------------------------
    {
        auto worst = 0.0;
        juce::String where;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            for (const auto mode : { Mode::sine, Mode::saw, Mode::square, Mode::triangle })
            {
                for (int note = 36; note <= 120; note += 12)
                {
                    const auto hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
                    UnitSetup setup;
                    setup.mode = static_cast<int>(mode);
                    const auto measured = zeroCrossingHz(renderUnit(setup, hz, rate, static_cast<int>(rate * 2.0), 2048), rate);
                    const auto cents = 1200.0 * std::log2(measured / hz);
                    if (std::abs(cents) > worst)
                    {
                        worst = std::abs(cents);
                        where = modeName(static_cast<int>(mode)) + " MIDI " + juce::String(note) + " at " + fmt(rate / 1000.0, 1) + " kHz";
                    }
                }
            }
        }
        check("OscQuality_PitchIsWithinATenthOfACent", worst < 0.1,
              "SINE, SAW, SQUARE, TRIANGLE, MIDI 36-120, 44.1-96 kHz: worst " + fmt(worst, 4) + " ct (" + where + ")");
    }

    // ---- the measured alias envelope ---------------------------------------------------------------
    {
        constexpr int order = 15;
        const auto size = 1 << order;
        const auto measure = [&](const UnitSetup& setup, int note, bool voiceStage, int guard)
        {
            const auto bin = oddBinNear(440.0 * std::pow(2.0, (note - 69) / 12.0), fs, order);
            auto x = renderUnit(setup, bin * fs / size, fs, size, 8192);
            if (voiceStage)
            {
                px3::dsp::Adaa clip;
                for (auto& v : x) { v = clip.process(px3::dsp::kSourceClipAdaa, v); }
                // Re-render one frame through a primed clip so the frame is periodic.
                auto again = x;
                for (auto& v : again) { v = 0.0; }
                const auto source = renderUnit(setup, bin * fs / size, fs, 2 * size, 8192);
                px3::dsp::Adaa primed;
                for (std::size_t i = 0; i < source.size(); ++i)
                {
                    const auto y = primed.process(px3::dsp::kSourceClipAdaa, source[i]);
                    if (i >= static_cast<std::size_t>(size)) { again[i - static_cast<std::size_t>(size)] = y; }
                }
                x = again;
            }
            // A guard means the tone's ratio is not exact (FM's 2 comes from float
            // macro maths): window it, and mask its harmonics' main lobes.
            return aliasAgainstFundamental(guard > 0 ? blackmanHarrisPower(x, order) : periodicPower(x, order), bin, bin, fs, guard);
        };

        UnitSetup saw;
        saw.mode = static_cast<int>(Mode::saw);
        UnitSetup square;
        square.mode = static_cast<int>(Mode::square);
        UnitSetup triangle;
        triangle.mode = static_cast<int>(Mode::triangle);
        UnitSetup sync;
        sync.mode = static_cast<int>(Mode::hardSync);
        sync.a = static_cast<float>(std::pow(1.5 / 10.0, 1.0 / 1.3));   // ratio 2.5
        sync.b = 0.0f;
        UnitSetup fm;
        fm.mode = static_cast<int>(Mode::fm);
        fm.a = static_cast<float>(std::pow(2.6 / 3.8, 1.0 / 1.1));      // ratio 2
        fm.b = static_cast<float>(std::pow(0.5, 1.0 / 1.35));          // index 5

        // Limits sit a few dB under what was measured, so they catch a
        // regression rather than restate the design goal. HARD SYNC includes
        // its DRIVE stage: tanh at drive 1 on a full-scale sync saw.
        struct Case { const char* name; UnitSetup setup; int note; bool voice; int guard; double limit; };
        const std::array<Case, 6> cases { {
            { "SawAtC5", saw, 72, false, 0, -58.0 },
            { "SquareAtC5", square, 72, false, 0, -60.0 },
            { "TriangleAtC7", triangle, 96, false, 0, -80.0 },
            { "HardSyncRatio2_5AtC5", sync, 72, false, 0, -38.0 },
            { "FmRatio2Index5AtC7", fm, 96, false, 5, -70.0 },
            { "SawAtC5ThroughTheVoiceSourceStage", saw, 72, true, 0, -52.0 },
        } };
        for (const auto& c : cases)
        {
            const auto result = measure(c.setup, c.note, c.voice, c.guard);
            check((juce::String("OscQuality_Alias_") + c.name).toRawUTF8(), result.below15k < c.limit,
                  "alias below 15 kHz " + fmt(result.below15k, 1) + " dB (limit " + fmt(c.limit, 0) + "), total "
                      + fmt(result.total, 1) + " dB, against the fundamental");
        }
    }

    // ---- a mode change crossfades ------------------------------------------------------------------
    {
        auto unit = std::make_unique<OscillatorUnit>();
        unit->prepare(fs);
        OscillatorSettings settings;
        settings.modeIndex = static_cast<int>(Mode::saw);
        unit->setSettings(settings);
        unit->resetForNote(0.0, 1u);
        OscillatorUnit::RenderContext context;
        context.frequencyHz = 220.0;
        std::vector<double> y;
        for (int i = 0; i < 24000; ++i) { y.push_back(unit->renderSample(context)); }
        settings.modeIndex = static_cast<int>(Mode::square);
        unit->setSettings(settings, 512);
        for (int i = 0; i < 24000; ++i) { y.push_back(unit->renderSample(context)); }

        const auto maxStep = [&y](int from, int to)
        {
            auto best = 0.0;
            for (int i = from + 1; i < to; ++i) { best = juce::jmax(best, std::abs(y[static_cast<std::size_t>(i)] - y[static_cast<std::size_t>(i - 1)])); }
            return best;
        };
        const auto periodRms = [&y](int from)
        {
            double e = 0.0;
            for (int i = from; i < from + 218; ++i) { e += y[static_cast<std::size_t>(i)] * y[static_cast<std::size_t>(i)]; }
            return std::sqrt(e / 218.0);
        };
        const auto steady = juce::jmax(maxStep(12000, 23900), maxStep(36000, 47900));
        const auto fade = maxStep(23990, 24400);
        auto quietest = 1.0e9;
        for (int i = 23900; i < 24400; i += 8) { quietest = juce::jmin(quietest, periodRms(i)); }
        const auto steadyRms = juce::jmin(periodRms(20000), periodRms(40000));
        check("OscQuality_ModeChangeCrossfadesWithoutAClick", fade <= 1.05 * steady && quietest > 0.7 * steadyRms,
              "largest step during the change " + fmt(fade, 4) + " against " + fmt(steady, 4) + " in steady state; quietest cycle "
                  + fmt(quietest, 4) + " against " + fmt(steadyRms, 4));
    }

    // ---- sample-rate independence -------------------------------------------------------------------
    {
        juce::StringArray drift;
        for (int mode = 0; mode < px3::oscillatorModeCount; ++mode)
        {
            if (mode == static_cast<int>(Mode::wavetable)) { continue; }
            UnitSetup setup;
            setup.mode = mode;
            const auto levelAt = [&setup](double rate)
            {
                const auto order = rate > 50000.0 ? 17 : 16;
                const auto x = renderUnit(setup, 220.0, rate, 1 << order, static_cast<int>(rate * 0.1));
                return bandLevelDb(windowedPower(x, 0, order), rate, 50.0, 10000.0);
            };
            const auto reference = levelAt(48000.0);
            const auto tolerance = mode == static_cast<int>(Mode::digital) ? 2.0 : 1.0;
            for (const auto rate : { 44100.0, 88200.0, 96000.0 })
            {
                const auto difference = levelAt(rate) - reference;
                if (std::abs(difference) > tolerance)
                {
                    drift.add(modeName(mode) + " at " + fmt(rate / 1000.0, 1) + " kHz " + fmt(difference, 2) + " dB");
                }
            }
        }
        check("OscQuality_EveryModeHoldsItsLevelAcrossSampleRates", drift.isEmpty(),
              drift.isEmpty() ? juce::String("50 Hz-10 kHz level within 1 dB of 48 kHz at 44.1, 88.2 and 96 kHz (DIGITAL within 2 dB)")
                              : drift.joinIntoString(", "));

        // A time constant: PHYSICAL's ring-down, from the strike to 300 ms later.
        juce::String decays;
        auto spread = 0.0, first = 0.0;
        auto firstSet = false;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            UnitSetup physical;
            physical.mode = static_cast<int>(Mode::physical);
            physical.a = 0.3f;
            const auto x = renderUnit(physical, 220.0, rate, static_cast<int>(rate * 0.35), 0);
            const auto window = static_cast<int>(rate * 0.02);
            const auto rmsAt = [&x, window](int from)
            {
                double e = 0.0;
                for (int i = from; i < from + window; ++i) { e += x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)]; }
                return std::sqrt(e / window);
            };
            const auto decay = 20.0 * std::log10(rmsAt(static_cast<int>(rate * 0.3)) / rmsAt(0));
            decays << fmt(rate / 1000.0, 1) << " kHz " << fmt(decay, 2) << " dB  ";
            if (!firstSet) { first = decay; firstSet = true; }
            spread = juce::jmax(spread, std::abs(decay - first));
        }
        check("OscQuality_PhysicalDecaysInSecondsAtEveryRate", spread < 0.5, decays + "(spread " + fmt(spread, 2) + " dB)");

        // DIGITAL's hold is a duration. At 48 and 96 kHz it is exactly the same
        // time, and at 44.1 and 88.2 kHz exactly the same time as each other;
        // between the two families it differs by the whole-sample rounding
        // (0.5%), which moves the hold's images enough to shift the spectrum.
        // As a sample count, 96 kHz would have held half as long as 48 kHz.
        juce::String centroids;
        std::array<double, 4> centroidAtRate {};
        std::size_t rateIndex = 0;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            UnitSetup digital;
            digital.mode = static_cast<int>(Mode::digital);
            digital.b = 1.0f;
            const auto order = rate > 50000.0 ? 16 : 15;
            const auto centroid = centroidHz(windowedPower(renderUnit(digital, 220.0, rate, 1 << order), 0, order), rate, 50.0, 4000.0);
            centroids << fmt(rate / 1000.0, 1) << " kHz " << fmt(centroid, 1) << " Hz  ";
            centroidAtRate[rateIndex++] = centroid;
        }
        check("OscQuality_DigitalHoldIsADuration",
              std::abs(centroidAtRate[3] / centroidAtRate[1] - 1.0) < 0.02 && std::abs(centroidAtRate[2] / centroidAtRate[0] - 1.0) < 0.02,
              centroids + "(48 against 96 and 44.1 against 88.2 within 2%)");

        // Pink noise stays pink.
        juce::String slopes;
        auto slopeWorst = 0.0;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            UnitSetup pink;
            pink.mode = static_cast<int>(Mode::pinkNoise);
            pink.a = 1.0f;
            const auto order = rate > 50000.0 ? 15 : 14;
            const auto segment = 1 << order;
            constexpr int segments = 48;
            const auto x = renderUnit(pink, 220.0, rate, segment * segments, 8192);
            std::vector<double> averaged(static_cast<std::size_t>(segment / 2), 0.0);
            for (int s = 0; s < segments; ++s)
            {
                const auto p = windowedPower(x, s * segment, order);
                for (std::size_t k = 0; k < p.size(); ++k) { averaged[k] += p[k]; }
            }
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            int n = 0;
            for (double centre = 50.0; centre <= 10000.0; centre *= std::pow(2.0, 1.0 / 3.0))
            {
                const auto lo = static_cast<int>(centre * std::pow(2.0, -1.0 / 6.0) * segment / rate);
                const auto hi = static_cast<int>(centre * std::pow(2.0, 1.0 / 6.0) * segment / rate);
                double sum = 0.0;
                int count = 0;
                for (int k = juce::jmax(1, lo); k <= hi; ++k) { sum += averaged[static_cast<std::size_t>(k)]; ++count; }
                if (count == 0) { continue; }
                const auto xLog = std::log2(centre);
                const auto yDb = 10.0 * std::log10(sum / count);
                sx += xLog; sy += yDb; sxx += xLog * xLog; sxy += xLog * yDb; ++n;
            }
            const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
            slopes << fmt(rate / 1000.0, 1) << " kHz " << fmt(slope, 2) << "  ";
            slopeWorst = juce::jmax(slopeWorst, std::abs(slope + 3.01));
        }
        check("OscQuality_PinkNoiseIsPinkAtEveryRate", slopeWorst < 0.25, slopes + "dB/oct (ideal -3.01)");
    }

    // ---- PHYSICAL and ROB -------------------------------------------------------------------------------
    {
        juce::String peaks;
        auto worst = 0.0;
        for (const auto material : { 0.0f, 1.0f })
        {
            UnitSetup physical;
            physical.mode = static_cast<int>(Mode::physical);
            physical.b = material;
            const auto p = windowedPower(renderUnit(physical, 220.0, fs, 32768, 2048), 0, 15);
            const auto binHz = fs / 32768.0;
            auto bestBin = 1;
            for (int k = static_cast<int>(150.0 / binHz); k < static_cast<int>(300.0 / binHz); ++k)
            {
                if (p[static_cast<std::size_t>(k)] > p[static_cast<std::size_t>(bestBin)]) { bestBin = k; }
            }
            const auto peakHz = bestBin * binHz;
            peaks << "MATERIAL " << fmt(material, 0) << ": " << fmt(peakHz, 1) << " Hz  ";
            worst = juce::jmax(worst, std::abs(peakHz / 220.0 - 1.0));
        }
        check("OscQuality_PhysicalMaterialKeepsTheFundamentalAtThePlayedPitch", worst < 0.02, peaks + "(played 220 Hz)");

        UnitSetup quiet;
        quiet.mode = static_cast<int>(Mode::rob);
        quiet.c = 0.0f;
        auto otherSeed = quiet;
        otherSeed.seed = 99u;
        const auto a = renderUnit(quiet, 220.0, fs, 24000, 48000);
        const auto b = renderUnit(otherSeed, 220.0, fs, 24000, 48000);
        auto largest = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) { largest = juce::jmax(largest, std::abs(a[i] - b[i])); }
        check("OscQuality_RobAddsNoChaosAtChaosZero", largest < 1.0e-9,
              "after the transient, two different noise seeds differ by at most " + juce::String(largest, 12));
    }
}

} // namespace px3tests
