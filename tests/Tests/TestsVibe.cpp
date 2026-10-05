#include "TestSupport.h"
#include "VibeRegression.h"

#include "UniVibe.h"
#include "../../products/PX3Vibe/PluginProcessor.h"

// testVibe
//
// VIBE is a model of the Uni-Vibe (docs/VIBE_DSP_DESIGN.md). These tests pin
// two things: the plumbing every effect needs (transparent when off, click-free
// switches, smoothing, determinism, any rate and block size), and the CHARACTER
// that makes it a Uni-Vibe rather than a phaser - unmatched stages, a sweep the
// lamp makes lopsided, stages that are not all-passes, and the pedal's output
// switch. The character checks are black-box (VibeRegression.h), so they hold
// against any implementation; the previous VIBE (four ideal all-passes with
// made-up staggered ranges) failed them.

namespace px3tests
{
namespace
{
using namespace px3tests::vibemeasure;

float speedForHz(double hz)
{
    return static_cast<float>(std::log(hz / 0.5) / std::log(16.0));
}

px3::UniVibeSettings settingsFor(float intensity, double hz, int mode)
{
    px3::UniVibeSettings s;
    s.enabled = true;
    s.intensity = intensity;
    s.speed = speedForHz(hz);
    s.mode = mode;
    return s;
}

struct Rendered
{
    std::vector<float> left, right;
};

// Stereo render with a per-block settings callback.
Rendered renderVibe(double rate, int block, int samples,
                    const std::function<float(int, int)>& source, // (sample, channel)
                    const std::function<void(px3::UniVibeSettings&, int)>& perBlock,
                    px3::UniVibeSettings initial = settingsFor(0.6f, 1.5, 0))
{
    px3::UniVibe vibe;
    vibe.updateForBlock(initial);
    vibe.prepare(rate);
    Rendered r;
    r.left.reserve(static_cast<std::size_t>(samples));
    r.right.reserve(static_cast<std::size_t>(samples));
    auto settings = initial;
    for (int start = 0; start < samples; start += block)
    {
        if (perBlock) perBlock(settings, start);
        vibe.updateForBlock(settings);
        for (int n = start; n < juce::jmin(samples, start + block); ++n)
        {
            float l = 0.0f, r2 = 0.0f;
            vibe.processSampleFrame(source(n, 0), source(n, 1), l, r2);
            r.left.push_back(l);
            r.right.push_back(r2);
        }
    }
    return r;
}

float sineAt(int n, double hz, double amplitude, double rate = 48000.0)
{
    return static_cast<float>(amplitude * std::sin(juce::MathConstants<double>::twoPi * hz * n / rate));
}

double rmsOf(const std::vector<float>& x, int from, int to)
{
    double e = 0.0;
    to = juce::jmin(to, static_cast<int>(x.size()));
    for (int i = from; i < to; ++i) e += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
    return std::sqrt(e / juce::jmax(1, to - from));
}

double maxStep(const std::vector<float>& x, int from, int to)
{
    double worst = 0.0;
    to = juce::jmin(to, static_cast<int>(x.size()));
    for (int i = juce::jmax(1, from); i < to; ++i)
        worst = juce::jmax(worst, static_cast<double>(std::abs(x[static_cast<std::size_t>(i)] - x[static_cast<std::size_t>(i - 1)])));
    return worst;
}

// Pink-ish noise (Kellet economy filter), reproducible.
struct PinkSource
{
    explicit PinkSource(int seed) : random(seed) {}
    float next()
    {
        const auto w = random.nextFloat() * 2.0f - 1.0f;
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.05f;
    }
    juce::Random random;
    float b0 { 0.0f }, b1 { 0.0f }, b2 { 0.0f };
};

std::vector<float> pinkNoise(int samples, int seed)
{
    PinkSource p(seed);
    std::vector<float> v(static_cast<std::size_t>(samples));
    for (auto& s : v) s = p.next();
    return v;
}

VibeCharacter measureNewVibe()
{
    return measureCharacter<px3::UniVibe>(
        [](px3::UniVibe& v, float intensity, double hz, int mode)
        {
            v.updateForBlock(settingsFor(intensity, hz, mode));
            v.prepare(kRate);
        },
        [](px3::UniVibe& v, float x)
        {
            float l = 0.0f, r = 0.0f;
            v.processSampleFrame(x, x, l, r);
            return l;
        });
}
} // namespace

void printVibeCalibration()
{
    std::printf("\nVIBE CALIBRATION\n");
    // Cell resistances over a cycle, against DAFx-19 Table 1.
    for (const auto intensity : { 0.0f, 0.3f, 0.6f, 1.0f })
    {
        for (const auto hz : { 0.5, 2.0, 8.0 })
        {
            px3::UniVibe vibe;
            vibe.updateForBlock(settingsFor(intensity, hz, 0));
            vibe.prepare(48000.0);
            std::array<double, 4> lo { 1e30, 1e30, 1e30, 1e30 }, hi {}, sum {};
            int count = 0;
            const auto total = static_cast<int>(48000.0 * (3.0 + 4.0 / hz));
            for (int n = 0; n < total; ++n)
            {
                float l, r;
                vibe.processSampleFrame(0.0f, 0.0f, l, r);
                if (n > 48000 * 3 && n % 16 == 0)
                {
                    const auto cells = vibe.debugCellResistances();
                    for (std::size_t c = 0; c < 4; ++c)
                    {
                        lo[c] = juce::jmin(lo[c], cells[c]);
                        hi[c] = juce::jmax(hi[c], cells[c]);
                        sum[c] += cells[c];
                    }
                    ++count;
                }
            }
            std::printf("  int %.1f %4.1f Hz  cell1 %7.1fk..%7.1fk mean %7.1fk | cell3 %6.1fk..%7.1fk | f3 %7.0f..%7.0f Hz\n",
                        intensity, hz, lo[0] / 1e3, hi[0] / 1e3, sum[0] / count / 1e3, lo[2] / 1e3, hi[2] / 1e3,
                        px3::UniVibe::stageCentreHz(2, hi[2]), px3::UniVibe::stageCentreHz(2, lo[2]));
        }
    }

    // Levels on pink noise, against the dry input.
    const auto noise = pinkNoise(48000 * 6, 11);
    for (const auto mode : { 0, 1 })
    {
        for (const auto intensity : { 0.0f, 0.6f, 1.0f })
        {
            const auto r = renderVibe(48000.0, 512, static_cast<int>(noise.size()),
                                      [&](int n, int) { return noise[static_cast<std::size_t>(n)]; }, {},
                                      settingsFor(intensity, 1.5, mode));
            const auto dry = rmsOf(noise, 48000, static_cast<int>(noise.size()));
            const auto wet = rmsOf(r.left, 48000, static_cast<int>(noise.size()));
            std::printf("  %s int %.1f: level %+.2f dB\n", mode == 0 ? "CHORUS " : "VIBRATO", intensity,
                        20.0 * std::log10(wet / dry));
        }
    }

    const auto c = measureNewVibe();
    std::printf("  character: rise %.3f (low %.3f) span %.2f oct, deep notches %.2f, two-notch frames %.2f, "
                "vibrato bass swing %.1f dB, treble swing %.1f dB, treble-over-bass %+.2f dB\n",
                c.riseFraction, c.riseFractionLow, c.notchSpanOctaves, c.meanDeepNotches, c.framesWithTwoNotches,
                c.vibratoBassSwingDb, c.vibratoTrebleSwingDb, c.vibratoTrebleOverBassDb);

    // Nonlinearity on a static lamp (INTENSITY 0) so no sweep sidebands.
    for (const auto amp : { 0.0625, 0.25, 1.0 })
    {
        const auto r = renderVibe(48000.0, 512, 48000 * 2, [amp](int n, int) { return sineAt(n, 187.5, amp); }, {},
                                  settingsFor(0.0f, 1.5, 1));
        std::printf("  harmonics at %.4f peak: %.5f\n", amp, harmonicToFundamentalRatio(r.left, 187.5, 48000));
    }
}

void testVibe()
{
    suite("VIBE (Uni-Vibe)");

    // ---- defaults -----------------------------------------------------------
    {
        PX3SynthAudioProcessor synth;
        PX3VibeAudioProcessor product;
        const px3::UniVibeSettings defaults;
        auto* synthEnabled = findParameter(synth, "fx.vibe.enabled");
        auto* productEnabled = findParameter(product, "fx.vibe.enabled");
        bool sameRanges = true;
        juce::StringArray ids;
        for (const auto* id : { "fx.vibe.speed", "fx.vibe.intensity", "fx.vibe.level", "fx.vibe.mode", "fx.vibe.stereo" })
        {
            auto* a = findParameter(synth, id);
            auto* b = findParameter(product, id);
            sameRanges = sameRanges && a != nullptr && b != nullptr
                         && a->getNormalisableRange().start == b->getNormalisableRange().start
                         && a->getNormalisableRange().end == b->getNormalisableRange().end
                         && a->getDefaultValue() == b->getDefaultValue();
            if (a == nullptr) ids.add(id);
        }
        check("Vibe_SynthAndProductShareIdsRangesAndDefaults", sameRanges, ids.joinIntoString(", "));
        check("Vibe_OffInTheSynthOnInTheProduct",
              synthEnabled != nullptr && productEnabled != nullptr
                  && synthEnabled->getDefaultValue() < 0.5f && productEnabled->getDefaultValue() > 0.5f,
              "INTENSITY 0 still colours, so only the switch makes it transparent");
        check("Vibe_DefaultSpeedIsAboutOneAndAHalfHertz",
              nearly(px3::UniVibe::speedToHz(defaults.speed), 1.52, 0.05)
                  && nearly(px3::UniVibe::speedToHz(0.0f), 0.5, 1e-4) && nearly(px3::UniVibe::speedToHz(1.0f), 8.0, 1e-3),
              fmt(px3::UniVibe::speedToHz(defaults.speed), 3) + " Hz; range 0.5..8 Hz");
        const auto modes = px3::UniVibe::modeNames();
        const auto stereo = px3::UniVibe::stereoNames();
        check("Vibe_ChoicesAreThePedalsSwitchAndTheStereoMod",
              modes == juce::StringArray { "CHORUS", "VIBRATO" } && stereo == juce::StringArray { "LINKED", "INVERTED" });
    }

    // ---- off is off --------------------------------------------------------
    {
        auto s = settingsFor(1.0f, 4.0, 0);
        s.enabled = false;
        const auto r = renderVibe(48000.0, 512, 48000, [](int n, int ch) { return sineAt(n, 440.0 + ch * 3.0, 0.5); }, {}, s);
        bool identical = true;
        for (int n = 0; n < 48000; ++n)
        {
            identical = identical && r.left[static_cast<std::size_t>(n)] == sineAt(n, 440.0, 0.5)
                        && r.right[static_cast<std::size_t>(n)] == sineAt(n, 443.0, 0.5);
        }
        check("Vibe_SwitchedOffIsBitTransparentFromTheFirstSample", identical);
    }

    // ---- INTENSITY 0 is the idle lamp, not a bypass -------------------------
    {
        px3::UniVibe vibe;
        vibe.updateForBlock(settingsFor(0.0f, 4.0, 0));
        vibe.prepare(48000.0);
        double lo = 1e30, hi = 0.0, diff = 0.0, energy = 0.0;
        juce::Random random(5);
        for (int n = 0; n < 48000 * 2; ++n)
        {
            const auto x = random.nextFloat() * 0.4f - 0.2f;
            float l, r;
            vibe.processSampleFrame(x, x, l, r);
            if (n > 24000)
            {
                const auto cells = vibe.debugCellResistances();
                lo = juce::jmin(lo, cells[0]);
                hi = juce::jmax(hi, cells[0]);
                diff += static_cast<double>(l - x) * (l - x);
                energy += static_cast<double>(x) * x;
            }
        }
        check("Vibe_IntensityZeroHoldsTheLampStill", hi / lo < 1.0001,
              "cell 1 " + fmt(lo / 1e3, 1) + "k .. " + fmt(hi / 1e3, 1) + "k");
        check("Vibe_IntensityZeroStillColoursTheSignal", std::sqrt(diff / energy) > 0.3,
              "difference from dry " + fmt(std::sqrt(diff / energy), 3) + " of the input");
    }

    // ---- the stages: unmatched, capacitor-ratio stagger --------------------
    {
        // At every lamp level the four stage frequencies keep their order and a
        // spread of more than two decades: f3 > f4 > f1 > f2. A matched or
        // evenly staggered phaser (the old VIBE spread ~1.1 decades in order)
        // cannot.
        bool ordered = true;
        double minSpreadDecades = 1e9;
        for (double g = 0.001; g <= 1.5; g *= 1.25)
        {
            std::array<double, 4> f {};
            for (int s = 0; s < 4; ++s) f[static_cast<std::size_t>(s)] = px3::UniVibe::stageCentreHz(s, px3::UniVibe::cellResistance(s, g));
            ordered = ordered && f[2] > f[3] && f[3] > f[0] && f[0] > f[1];
            minSpreadDecades = juce::jmin(minSpreadDecades, std::log10(f[2] / f[1]));
        }
        check("Vibe_StagesKeepTheCapacitorStagger", ordered && minSpreadDecades > 2.0,
              "order f3 > f4 > f1 > f2 at every lamp level; spread >= " + fmt(minSpreadDecades, 2) + " decades");

        // Not all-passes: each stage goes from alpha*kappa_e - beta*kappa_c at
        // DC to -beta at the top (DAFx-19 eq. 9). The 220 nF stage's bass shelf
        // is the deepest.
        const auto r = 100.0e3;
        const auto lowGain = std::abs(px3::UniVibe::stageResponse(1, r, 0.01, 48000.0));
        const auto highGain = std::abs(px3::UniVibe::stageResponse(1, r, 12000.0, 48000.0));
        const auto s3High = std::abs(px3::UniVibe::stageResponse(2, r, 23000.0, 48000.0));
        check("Vibe_StagesAreNotAllPasses",
              nearly(lowGain, 0.98 * (1.0e-6 / 1.22e-6) - 1.09 * (0.22e-6 / 1.22e-6), 0.01) && nearly(highGain, 1.09, 0.01)
                  && s3High > 1.0,
              "220 nF stage: " + fmt(20.0 * std::log10(lowGain), 2) + " dB at DC, " + fmt(20.0 * std::log10(highGain), 2)
                  + " dB at the top");
    }

    // ---- character, black-box ----------------------------------------------
    {
        const auto c = measureNewVibe();
        const auto detail = "rise " + fmt(c.riseFraction, 2) + " (0.3: " + fmt(c.riseFractionLow, 2) + "), span "
                            + fmt(c.notchSpanOctaves, 1) + " oct, notches " + fmt(c.meanDeepNotches, 2) + ", bass swing "
                            + fmt(c.vibratoBassSwingDb, 1) + " dB, treble swing " + fmt(c.vibratoTrebleSwingDb, 1)
                            + " dB, tilt " + fmt(c.vibratoTrebleOverBassDb, 2) + " dB";
        // The lamp snaps bright and drifts dark: the phase it puts on a tone
        // climbs in well under half the cycle. A sine-driven phaser climbs in
        // half (the previous VIBE measured 0.47).
        check("Vibe_SweepIsLopsided", c.riseFraction < 0.42, detail);
        // ...and more so the harder the lamp is driven (DAFx-19, section 4.2).
        check("Vibe_SweepGetsMoreLopsidedWithIntensity", c.riseFraction < c.riseFractionLow - 0.015, detail);
        // The unmatched stages put one deep notch through the mid band at a
        // time; four staggered all-passes put two (the previous VIBE: two in
        // 62% of frames, 1.6 deep notches on average).
        check("Vibe_OneMainNotchAtATime", c.framesWithTwoNotches < 0.3 && c.meanDeepNotches < 1.2,
              "two-notch frames " + fmt(c.framesWithTwoNotches, 2) + ", deep notches per frame " + fmt(c.meanDeepNotches, 2));
        check("Vibe_NotchTravelsWide", c.notchSpanOctaves > 3.0, detail);
        // The throb: in VIBRATO the bass level swings with the sweep (the
        // 220 nF stage's moving shelf); the treble much less. An all-pass
        // chain has no level change at all apart from any added tremolo (the
        // previous VIBE: 1.1 dB, from its "lamp bleed" tremolo).
        check("Vibe_VibratoThrobsInTheBass", c.vibratoBassSwingDb > 6.0
                  && c.vibratoBassSwingDb > c.vibratoTrebleSwingDb + 3.0, detail);
        // Inverting gains above one: the phased signal is brighter than an
        // all-pass leaves it (the previous VIBE: +0.02 dB).
        check("Vibe_PhasedSignalTiltsBright", c.vibratoTrebleOverBassDb > 0.5, detail);
    }

    // ---- VIBRATO is pitch wobble ---------------------------------------------
    {
        const auto r = renderVibe(48000.0, 512, 48000 * 3, [](int n, int) { return sineAt(n, 1000.0, 0.3); }, {},
                                  settingsFor(1.0f, 4.0, 1));
        std::vector<float> tail(r.left.begin() + 48000, r.left.end());
        auto power = [&](double hz) { return std::norm(dft(tail.data(), static_cast<int>(tail.size()), hz)); };
        double side = 0.0;
        for (const auto off : { -8.0, -4.0, 4.0, 8.0 }) side += power(1000.0 + off);
        const auto db = 10.0 * std::log10(side / power(1000.0));
        check("Vibe_VibratoWobblesPitch", db > -20.0, "sidebands at the sweep rate " + fmt(db, 1) + " dB");
    }

    // ---- CHORUS is the equal dry+phased sum; VIBRATO has no dry -----------
    {
        // CHORUS sums dry and phased, so a tone falls into the moving notch;
        // VIBRATO has no dry path to cancel against.
        const auto chorus = renderVibe(48000.0, 512, 48000 * 3, [](int n, int) { return sineAt(n, 1000.0, 0.1); }, {},
                                       settingsFor(1.0f, 2.0, 0));
        const auto vibrato = renderVibe(48000.0, 512, 48000 * 3, [](int n, int) { return sineAt(n, 1000.0, 0.1); }, {},
                                        settingsFor(1.0f, 2.0, 1));
        const auto chorusSwing = toneSwingDb(chorus.left, 1000.0, 48000);
        const auto vibratoSwing = toneSwingDb(vibrato.left, 1000.0, 48000);
        // VIBRATO still swings a little: the stages are not all-passes, so the
        // phased signal is a band-limited tremolo as well (DAFx-19, section 1).
        check("Vibe_ChorusNotchesVibratoDoesNot", chorusSwing > 12.0 && chorusSwing > vibratoSwing + 6.0,
              "1 kHz swing: CHORUS " + fmt(chorusSwing, 1) + " dB, VIBRATO " + fmt(vibratoSwing, 1) + " dB");
    }

    // ---- level ---------------------------------------------------------------
    {
        const auto noise = pinkNoise(48000 * 5, 3);
        auto levelDb = [&](int mode, float levelTrim)
        {
            auto s = settingsFor(0.6f, 1.5, mode);
            s.levelDb = levelTrim;
            const auto r = renderVibe(48000.0, 512, static_cast<int>(noise.size()),
                                      [&](int n, int) { return noise[static_cast<std::size_t>(n)]; }, {}, s);
            return 20.0 * std::log10(rmsOf(r.left, 48000, static_cast<int>(noise.size()))
                                     / rmsOf(noise, 48000, static_cast<int>(noise.size())));
        };
        const auto chorus = levelDb(0, 0.0f);
        const auto vibrato = levelDb(1, 0.0f);
        const auto trimmed = levelDb(0, 6.0f);
        check("Vibe_ModesAreLevelMatched", std::abs(chorus) < 1.5 && std::abs(vibrato) < 1.5 && std::abs(chorus - vibrato) < 1.5,
              "pink noise: CHORUS " + fmt(chorus, 2) + " dB, VIBRATO " + fmt(vibrato, 2) + " dB");
        check("Vibe_LevelTrimsInDecibels", nearly(trimmed - chorus, 6.0, 0.05), fmt(trimmed - chorus, 3) + " dB for +6");
    }

    // ---- transistor character -----------------------------------------------
    {
        auto harmonics = [](double amp)
        {
            const auto r = renderVibe(48000.0, 512, 48000 * 2, [amp](int n, int) { return sineAt(n, 187.5, amp); }, {},
                                      settingsFor(0.0f, 1.5, 1));
            return harmonicToFundamentalRatio(r.left, 187.5, 48000);
        };
        const auto quiet = harmonics(0.0625);
        const auto loud = harmonics(1.0);
        check("Vibe_CleanWhenQuietColouredWhenHot", quiet < 1.0e-4 && loud > 20.0 * quiet && loud > 1.0e-4,
              "harmonic/fundamental energy at -24 dBFS " + fmt(quiet, 7) + ", at 0 dBFS " + fmt(loud, 5));
    }

    // ---- stereo ------------------------------------------------------------
    {
        auto s = settingsFor(1.0f, 2.0, 0);
        const auto linked = renderVibe(48000.0, 512, 48000 * 2, [](int n, int) { return sineAt(n, 700.0, 0.2); }, {}, s);
        bool same = true;
        for (std::size_t i = 0; i < linked.left.size(); ++i) same = same && linked.left[i] == linked.right[i];
        check("Vibe_LinkedStereoIsOneCircuitTwice", same, "identical input gives identical sides");

        s.stereo = 1;
        const auto inverted = renderVibe(48000.0, 512, 48000 * 3, [](int n, int) { return sineAt(n, 1000.0, 0.1); }, {}, s);
        std::vector<float> sum(inverted.left.size());
        for (std::size_t i = 0; i < sum.size(); ++i) sum[i] = inverted.left[i] + inverted.right[i];
        const auto sideSwing = toneSwingDb(inverted.left, 1000.0, 48000);
        const auto sumSwing = toneSwingDb(sum, 1000.0, 48000);
        check("Vibe_InvertedStereoPutsNotchesWhereTheOtherSideHasPeaks", sideSwing > 12.0 && sumSwing < 2.0,
              "1 kHz swing: one side " + fmt(sideSwing, 1) + " dB, L+R " + fmt(sumSwing, 1) + " dB (only the dry survives)");
    }

    // ---- switches and automation are click-free ----------------------------------
    {
        // A low sine and its own largest step: any switch must stay near it.
        constexpr double hz = 110.0;
        const auto steady = renderVibe(48000.0, 512, 48000 * 2, [](int n, int) { return sineAt(n, hz, 0.5); }, {},
                                       settingsFor(0.7f, 2.0, 0));
        const auto reference = maxStep(steady.left, 24000, 96000);

        auto worstWith = [&](const std::function<void(px3::UniVibeSettings&, int)>& change)
        {
            const auto r = renderVibe(48000.0, 512, 48000 * 2, [](int n, int) { return sineAt(n, hz, 0.5); }, change,
                                      settingsFor(0.7f, 2.0, 0));
            return maxStep(r.left, 24000, 96000);
        };
        const auto onOff = worstWith([](px3::UniVibeSettings& s, int at) { s.enabled = (at / 12000) % 2 == 0; });
        const auto mode = worstWith([](px3::UniVibeSettings& s, int at) { s.mode = (at / 12000) % 2; });
        const auto stereoSwitch = worstWith([](px3::UniVibeSettings& s, int at) { s.stereo = (at / 12000) % 2; });
        const auto intensity = worstWith([](px3::UniVibeSettings& s, int at) { s.intensity = (at / 6000) % 2 == 0 ? 0.1f : 1.0f; });
        const auto speed = worstWith([](px3::UniVibeSettings& s, int at) { s.speed = (at / 6000) % 2 == 0 ? 0.0f : 1.0f; });
        const auto level = worstWith([](px3::UniVibeSettings& s, int at) { s.levelDb = (at / 6000) % 2 == 0 ? -12.0f : 12.0f; });
        const auto limit = 1.5 * reference;
        const auto levelLimit = 1.5 * reference * juce::Decibels::decibelsToGain(12.0);
        check("Vibe_SwitchesAndAutomationDoNotClick",
              onOff < limit && mode < limit && stereoSwitch < limit && intensity < limit && speed < limit && level < levelLimit,
              "max step vs steady " + fmt(reference, 4) + ": on/off " + fmt(onOff, 4) + ", mode " + fmt(mode, 4) + ", stereo "
                  + fmt(stereoSwitch, 4) + ", intensity " + fmt(intensity, 4) + ", speed " + fmt(speed, 4) + ", level "
                  + fmt(level, 4));
    }

    // ---- silence, DC, finite ---------------------------------------------------
    {
        const auto silent = renderVibe(48000.0, 512, 48000, [](int, int) { return 0.0f; }, {}, settingsFor(1.0f, 8.0, 0));
        double peak = 0.0;
        for (const auto v : silent.left) peak = juce::jmax(peak, static_cast<double>(std::abs(v)));
        check("Vibe_SilenceInSilenceOut", peak < 1.0e-6, "peak " + fmt(peak, 9));

        const auto dc = renderVibe(48000.0, 512, 48000 * 3, [](int, int) { return 0.5f; }, {}, settingsFor(1.0f, 4.0, 0));
        double mean = 0.0;
        for (int i = 48000 * 2; i < 48000 * 3; ++i) mean += dc.left[static_cast<std::size_t>(i)];
        mean /= 48000.0;
        check("Vibe_BlocksDc", std::abs(mean) < 1.0e-3, "residual " + fmt(mean, 6));

        bool finite = true;
        for (const auto rate : { 22050.0, 44100.0, 96000.0, 192000.0 })
        {
            for (const auto block : { 1, 32, 1024 })
            {
                const auto r = renderVibe(rate, block, static_cast<int>(rate),
                                          [rate](int n, int) { return sineAt(n, 3000.0, 1.0, rate) + sineAt(n, 40.0, 0.8, rate); },
                                          [](px3::UniVibeSettings& s, int at) { s.intensity = (at / 4096) % 2 ? 1.0f : 0.0f; },
                                          settingsFor(1.0f, 8.0, 0));
                for (const auto v : r.left) finite = finite && std::isfinite(v) && std::abs(v) < 8.0f;
            }
        }
        check("Vibe_FiniteAndBoundedAtEveryRateAndBlockSize", finite, "22.05 - 192 kHz, blocks 1 - 1024, full-scale input");
    }

    // ---- determinism and block-size independence -------------------------------
    {
        auto src = [](int n, int ch) { return sineAt(n, 330.0 + 10.0 * ch, 0.4) + sineAt(n, 2500.0, 0.1); };
        const auto a = renderVibe(48000.0, 512, 48000 * 2, src, {}, settingsFor(0.8f, 3.0, 0));
        const auto b = renderVibe(48000.0, 512, 48000 * 2, src, {}, settingsFor(0.8f, 3.0, 0));
        const auto c = renderVibe(48000.0, 64, 48000 * 2, src, {}, settingsFor(0.8f, 3.0, 0));
        check("Vibe_Deterministic", a.left == b.left && a.right == b.right);
        check("Vibe_BlockSizeDoesNotChangeTheOutput", a.left == c.left && a.right == c.right,
              "the lamp runs on its own 16-sample clock, not the host's blocks");
    }

    // ---- sample rate does not change the sweep ----------------------------------
    {
        auto notchSpan = [](double rate)
        {
            px3::UniVibe vibe;
            vibe.updateForBlock(settingsFor(1.0f, 2.0, 0));
            vibe.prepare(rate);
            double lo = 1e30, hi = 0.0;
            for (int n = 0; n < static_cast<int>(rate * 3.0); ++n)
            {
                float l, r;
                vibe.processSampleFrame(0.0f, 0.0f, l, r);
                if (n > static_cast<int>(rate))
                {
                    const auto cells = vibe.debugCellResistances();
                    lo = juce::jmin(lo, cells[2]);
                    hi = juce::jmax(hi, cells[2]);
                }
            }
            return std::log10(hi / lo);
        };
        const auto at44 = notchSpan(44100.0);
        const auto at96 = notchSpan(96000.0);
        check("Vibe_SweepIndependentOfSampleRate", std::abs(at44 - at96) < 0.05,
              "cell 3 sweep " + fmt(at44, 3) + " decades at 44.1k, " + fmt(at96, 3) + " at 96k");
    }

    // ---- reset ------------------------------------------------------------------
    {
        px3::UniVibe vibe;
        vibe.updateForBlock(settingsFor(1.0f, 2.0, 0));
        vibe.prepare(48000.0);
        float l, r;
        for (int n = 0; n < 30000; ++n) vibe.processSampleFrame(sineAt(n, 200.0, 0.5), 0.0f, l, r);
        vibe.reset();
        px3::UniVibe fresh;
        fresh.updateForBlock(settingsFor(1.0f, 2.0, 0));
        fresh.prepare(48000.0);
        bool same = true;
        for (int n = 0; n < 4800; ++n)
        {
            float a, b, c, d;
            vibe.processSampleFrame(sineAt(n, 200.0, 0.5), 0.0f, a, b);
            fresh.processSampleFrame(sineAt(n, 200.0, 0.5), 0.0f, c, d);
            same = same && a == c && b == d;
        }
        check("Vibe_ResetReturnsToAFreshInstance", same);
    }
}
} // namespace px3tests
