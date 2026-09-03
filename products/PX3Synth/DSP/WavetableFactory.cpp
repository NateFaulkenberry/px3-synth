#include "WavetableFactory.h"

#include <cmath>

namespace px3
{
namespace
{
constexpr int kFrames = Wavetable::kDefaultFrameCount;
constexpr int kHarmonics = Wavetable::kMaxHarmonics;

// 0 at the first frame, 1 at the last.
double scan(int frame)
{
    return kFrames > 1 ? static_cast<double>(frame) / (kFrames - 1) : 0.0;
}

FrameSpectrum emptyFrame()
{
    FrameSpectrum s;
    s.amplitude.assign(kHarmonics + 1, 0.0f);
    s.phase.assign(kHarmonics + 1, 0.0f);
    return s;
}

// Equal loudness across the scan.
//
// Without this every table gets quieter as it gets brighter, because energy is
// being spread over more harmonics. The scan would then read as a volume
// control with a timbre side-effect, which is the opposite of what it is for.
void normalise(FrameSpectrum& frame)
{
    double energy = 0.0;
    for (const auto a : frame.amplitude)
    {
        energy += static_cast<double>(a) * a;
    }
    if (energy <= 0.0)
    {
        return;
    }

    const auto gain = static_cast<float>(0.9 / std::sqrt(2.0 * energy));
    for (auto& a : frame.amplitude)
    {
        a *= gain;
    }
}

std::vector<FrameSpectrum> build(const std::function<void(FrameSpectrum&, double)>& shape)
{
    std::vector<FrameSpectrum> frames;
    frames.reserve(kFrames);
    for (int f = 0; f < kFrames; ++f)
    {
        auto frame = emptyFrame();
        shape(frame, scan(f));
        normalise(frame);
        frames.push_back(std::move(frame));
    }
    return frames;
}

// ---------------------------------------------------------------- recipes ---

// Sine through to sawtooth. The reference table: if a scan sounds wrong here it
// is the oscillator, not the recipe.
std::vector<FrameSpectrum> sawFold()
{
    return build([](FrameSpectrum& frame, double t)
    {
        // Tilt from very steep (only the fundamental survives) to 1/h, the
        // sawtooth.
        //
        // Curved so the interesting part is not squeezed into the last third.
        // A linear tilt - or worse, one that eases IN - leaves the first half of
        // the scan sounding like a sine with the volume changing, because a
        // rolloff of 1/h^4 and one of 1/h^7 are both, to the ear, a sine.
        const auto tilt = 1.0 + 6.0 * std::pow(1.0 - t, 2.2);
        for (int h = 1; h <= kHarmonics; ++h)
        {
            frame.amplitude[static_cast<std::size_t>(h)] =
                static_cast<float>(std::pow(1.0 / h, tilt));
        }
    });
}

// Pulse width, done properly: the harmonic series of a rectangular wave of
// width w is |sin(pi*h*w)| / h. Sweeping w is a real PWM sweep rather than a
// crossfade between two shapes that happen to be named after it.
std::vector<FrameSpectrum> pulseWidth()
{
    return build([](FrameSpectrum& frame, double t)
    {
        const auto width = 0.5 - 0.45 * t;
        for (int h = 1; h <= kHarmonics; ++h)
        {
            frame.amplitude[static_cast<std::size_t>(h)] = static_cast<float>(
                std::abs(std::sin(juce::MathConstants<double>::pi * h * width)) / h);
        }
    });
}

// Drawbars. Nine footages, pulled in over the scan in the order an organist
// reaches for them, which is why the sub and the quint appear early.
std::vector<FrameSpectrum> drawbars()
{
    static constexpr int footage[] = { 1, 3, 2, 4, 6, 8, 10, 12, 16 };
    return build([](FrameSpectrum& frame, double t)
    {
        for (int i = 0; i < 9; ++i)
        {
            const auto h = footage[i];
            if (h > kHarmonics) { continue; }
            // Each drawbar arrives in turn and stays - except the first, which
            // is out before the scan begins. Ramping every drawbar from zero
            // leaves frame 0 completely silent, which is a scan that starts
            // from nothing rather than from a sound.
            const auto arrival = static_cast<double>(i) / 10.0;
            const auto pulled = i == 0 ? 1.0 : juce::jlimit(0.0, 1.0, (t - arrival) * 7.0);
            frame.amplitude[static_cast<std::size_t>(h)] +=
                static_cast<float>(pulled / std::sqrt(static_cast<double>(i + 1)));
        }
    });
}

// Vowels. Formants are resonances of a fixed SIZE, so they sit at fixed
// frequencies; the table is baked against a 110 Hz reference and the scan walks
// the vowel from "oo" to "ah" to "ee".
std::vector<FrameSpectrum> vowelMorph()
{
    return build([](FrameSpectrum& frame, double t)
    {
        // Three formants, each sweeping its own path across the scan.
        const double f1 = 320.0 + 480.0 * std::sin(juce::MathConstants<double>::pi * t * 0.5);
        const double f2 = 800.0 + 1500.0 * t;
        const double f3 = 2500.0 + 400.0 * t;
        const double reference = 110.0;

        for (int h = 1; h <= kHarmonics; ++h)
        {
            const auto hz = h * reference;
            auto amplitude = 0.0;
            for (const auto& formant : { std::make_pair(f1, 1.0), std::make_pair(f2, 0.55),
                                         std::make_pair(f3, 0.28) })
            {
                const auto width = formant.first * 0.28;
                const auto d = (hz - formant.first) / width;
                amplitude += formant.second * std::exp(-d * d);
            }
            // A source spectrum under the formants, or the vowel has no voice.
            frame.amplitude[static_cast<std::size_t>(h)] =
                static_cast<float>(amplitude / std::pow(h, 0.55));
        }
    });
}

// Struck metal. Harmonics thin out into a sparse, high, uneven set - the ear
// hears inharmonicity even when every partial is a whole number, because what
// it reads is the GAPS.
std::vector<FrameSpectrum> bellPartials()
{
    return build([](FrameSpectrum& frame, double t)
    {
        static constexpr int partials[] = { 1, 2, 5, 9, 14, 22, 31, 43, 58, 77 };
        for (int i = 0; i < 10; ++i)
        {
            const auto h = partials[i];
            if (h > kHarmonics) { continue; }
            // Early frames are nearly pure; later ones ring with the whole set.
            // The fundamental is always there - a bell that starts from silence
            // is not a quiet bell.
            const auto present = i == 0 ? 1.0 : juce::jlimit(0.0, 1.0, t * 9.0 - (i - 1) * 0.8);
            frame.amplitude[static_cast<std::size_t>(h)] =
                static_cast<float>(present / std::pow(static_cast<double>(i + 1), 0.8));
        }
    });
}

// A comb across the harmonic series, tightening as it scans. The spectral shape
// of a delayed copy of itself, which is where the hollow, phasey, distinctly
// digital character comes from.
std::vector<FrameSpectrum> combDigital()
{
    return build([](FrameSpectrum& frame, double t)
    {
        // From two teeth, not one: at one tooth the comb lands on every
        // harmonic at once and cancels the entire frame.
        const auto teeth = 2.0 + 10.0 * t;
        for (int h = 1; h <= kHarmonics; ++h)
        {
            const auto comb = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * h / teeth);
            frame.amplitude[static_cast<std::size_t>(h)] =
                static_cast<float>(comb / std::pow(h, 0.85));
        }
    });
}

// Written as a WAVESHAPER rather than as a spectrum: a sine driven into a fold
// has a harmonic series nobody would write out by hand, and this is what
// analyseFrame exists for.
std::vector<FrameSpectrum> waveFolder()
{
    std::vector<FrameSpectrum> frames;
    frames.reserve(kFrames);

    std::vector<float> cycle(static_cast<std::size_t>(Wavetable::kFrameSize), 0.0f);
    for (int f = 0; f < kFrames; ++f)
    {
        const auto drive = 1.0 + 6.0 * std::pow(scan(f), 1.4);
        for (int i = 0; i < Wavetable::kFrameSize; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * i / Wavetable::kFrameSize;
            // A triangle fold: sin() through a triangle wave folds the peaks
            // back on themselves instead of clipping them flat.
            const auto x = std::sin(phase) * drive;
            cycle[static_cast<std::size_t>(i)] =
                static_cast<float>(std::asin(std::sin(x * juce::MathConstants<double>::halfPi))
                                   / juce::MathConstants<double>::halfPi);
        }

        auto spectrum = analyseFrame(cycle.data(), Wavetable::kFrameSize);
        spectrum.amplitude.resize(static_cast<std::size_t>(kHarmonics) + 1, 0.0f);
        spectrum.phase.resize(static_cast<std::size_t>(kHarmonics) + 1, 0.0f);
        normalise(spectrum);
        frames.push_back(std::move(spectrum));
    }
    return frames;
}

// Soft, slightly asymmetric, and it stays that way. The even harmonics ride a
// little above the odd ones, which is what "warm" means when measured rather
// than described.
std::vector<FrameSpectrum> warmAsymmetry()
{
    return build([](FrameSpectrum& frame, double t)
    {
        const auto tilt = 3.2 - 2.0 * t;
        const auto evenLift = 0.05 + 0.95 * t;
        for (int h = 1; h <= kHarmonics; ++h)
        {
            const auto even = (h % 2 == 0) ? 1.0 + evenLift : 1.0;
            frame.amplitude[static_cast<std::size_t>(h)] =
                static_cast<float>(even * std::pow(1.0 / h, tilt));
        }
    });
}

const std::vector<FactoryWavetable>& tables()
{
    static const std::vector<FactoryWavetable> list {
        { "Saw Fold", "CLASSIC", "Sine opening into a sawtooth", sawFold },
        { "Pulse Width", "CLASSIC", "A true rectangular-wave width sweep", pulseWidth },
        { "Drawbars", "HARMONIC", "Nine footages pulled in one at a time", drawbars },
        { "Vowel Morph", "VOCAL", "Three formants walking oo - ah - ee", vowelMorph },
        { "Bell Partials", "METALLIC", "Sparse struck-metal partials", bellPartials },
        { "Comb Digital", "DIGITAL", "A tightening comb across the harmonics", combDigital },
        { "Wave Folder", "AGGRESSIVE", "A sine driven into a triangle fold", waveFolder },
        { "Warm Asymmetry", "WARM", "Soft rolloff with the even harmonics lifted", warmAsymmetry },
    };
    return list;
}
} // namespace

const std::vector<FactoryWavetable>& factoryWavetables()
{
    return tables();
}

std::shared_ptr<const Wavetable> buildFactoryWavetable(int index)
{
    const auto& list = tables();
    if (index < 0 || index >= static_cast<int>(list.size()))
    {
        return nullptr;
    }

    const auto& definition = list[static_cast<std::size_t>(index)];
    return Wavetable::build(definition.name, definition.category, definition.generate());
}

std::shared_ptr<const Wavetable> buildFactoryWavetable(const juce::String& name)
{
    const auto& list = tables();
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (name == list[i].name)
        {
            return buildFactoryWavetable(static_cast<int>(i));
        }
    }
    return nullptr;
}

} // namespace px3
