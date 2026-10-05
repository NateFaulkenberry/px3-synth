// REVERB: the six types, measured. The thresholds are set from the
// prototypes and the first C++ measurements (docs/REVERB_DSP_DESIGN.md has
// the numbers); every one guards a defect the old algorithms had or a
// property the redesign was built to have.

#include "TestSupport.h"

#include "ReverbMeasure.h"
#include "ReverbParameters.h"
#include "ReverbPresets.h"

#include "../../products/PX3Reverb/PluginProcessor.h"
#include "../../products/PX3Reverb/PluginEditor.h"

namespace px3tests
{
namespace
{
using namespace px3tests::reverbmeasure;
namespace rv = px3::reverb;

struct Stereo { std::vector<float> l, r; };

ReverbSettings wetSettings(int type)
{
    ReverbSettings s;
    s.algorithmIndex = type;
    s.enabled = true;
    s.amount = 1.0f;
    s.preDelay = 0.0f;
    return s;
}

// Renders `input` (mono, both channels) through a fresh Reverb at `rate`,
// after the MIX smoother has settled. `perBlock(block, settings)` may move a
// control between blocks.
Stereo run(const ReverbSettings& base, const std::vector<float>& input, double rate,
           const std::function<void(int, ReverbSettings&)>& perBlock = {}, const std::vector<float>* inputR = nullptr)
{
    auto reverb = std::make_unique<::Reverb>();
    reverb->prepare(rate);
    auto s = base;
    // Long enough for the MIX smoother to land exactly on its target.
    for (int n = 0; n < static_cast<int>(0.4 * rate); ++n)
    {
        if (n % 512 == 0) reverb->updateForBlock(s, 512);
        float l = 0.0f, r = 0.0f;
        reverb->processSampleFrame(0.0f, 0.0f, l, r);
    }
    Stereo out { std::vector<float>(input.size()), std::vector<float>(input.size()) };
    for (std::size_t n = 0; n < input.size(); ++n)
    {
        if (n % 512 == 0)
        {
            if (perBlock) perBlock(static_cast<int>(n / 512), s);
            reverb->updateForBlock(s, 512);
        }
        reverb->processSampleFrame(input[n], inputR != nullptr ? (*inputR)[n] : input[n], out.l[n], out.r[n]);
    }
    return out;
}

Stereo impulse(const ReverbSettings& s, double rate, double seconds)
{
    std::vector<float> in(static_cast<std::size_t>(rate * seconds), 0.0f);
    in[0] = 1.0f;
    return run(s, in, rate);
}

std::vector<float> noise(double rate, double seconds, float level, int seed = 7)
{
    juce::Random random(seed);
    std::vector<float> v(static_cast<std::size_t>(rate * seconds));
    float lp = 0.0f;
    for (auto& x : v)
    {
        const auto w = random.nextFloat() * 2.0f - 1.0f;
        lp += 0.3f * (w - lp);
        x = level * lp;
    }
    return v;
}

std::vector<float> sine(double rate, double seconds, double hz, float level, double gateSeconds = 1.0e9)
{
    std::vector<float> v(static_cast<std::size_t>(rate * seconds));
    for (std::size_t n = 0; n < v.size(); ++n)
        v[n] = static_cast<double>(n) / rate < gateSeconds
                   ? static_cast<float>(level * std::sin(juce::MathConstants<double>::twoPi * hz * static_cast<double>(n) / rate))
                   : 0.0f;
    return v;
}

bool allFinite(const Stereo& s)
{
    for (std::size_t i = 0; i < s.l.size(); ++i)
        if (! std::isfinite(s.l[i]) || ! std::isfinite(s.r[i])) return false;
    return true;
}

double peakOf(const Stereo& s)
{
    double p = 0.0;
    for (std::size_t i = 0; i < s.l.size(); ++i) p = juce::jmax(p, static_cast<double>(juce::jmax(std::abs(s.l[i]), std::abs(s.r[i]))));
    return p;
}

double dbOf(double energyRatio) { return 10.0 * std::log10(energyRatio + 1.0e-30); }

double bandEnergy(const std::vector<float>& x, std::size_t from, std::size_t to, double hz, double rate)
{
    // Goertzel-style single-bin energy over 100 ms Hann windows, summed.
    double total = 0.0;
    const auto window = static_cast<std::size_t>(0.1 * rate);
    for (auto start = from; start + window <= to && start + window <= x.size(); start += window)
    {
        double c = 0.0, s = 0.0;
        for (std::size_t k = 0; k < window; ++k)
        {
            const auto hann = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * static_cast<double>(k) / static_cast<double>(window - 1));
            const auto w = juce::MathConstants<double>::twoPi * hz * static_cast<double>(start + k) / rate;
            c += hann * x[start + k] * std::cos(w);
            s += hann * x[start + k] * std::sin(w);
        }
        total += c * c + s * s;
    }
    return total;
}

const char* name(int type) { return rv::kTypeNames[type]; }
juce::String id(const char* what, int type) { return juce::String("Reverb") + name(type) + "_" + what; }
} // namespace

void testReverb()
{
    suite("REVERB");

    // ---- transparency and the mix law ----------------------------------------
    {
        // MIX 0 must be the input to the bit: an untouched knob may not colour
        // the FX bus at all (the Synth's default).
        auto s = wetSettings(rv::hall);
        s.amount = 0.0f;
        const auto in = noise(48000.0, 1.0, 0.3f);
        const auto out = run(s, in, 48000.0);
        double worst = 0.0;
        for (std::size_t i = 0; i < in.size(); ++i) worst = juce::jmax(worst, static_cast<double>(std::abs(out.l[i] - in[i])));
        check("Reverb_MixZeroIsBitExactlyDry", worst == 0.0, "worst difference " + fmt(worst, 9));

        // MIX 1 is fully wet: the impulse itself must not come through.
        const auto wet = impulse(wetSettings(rv::hall), 48000.0, 0.05);
        check("Reverb_MixFullIsFullyWet", std::abs(wet.l[0]) < 0.02 && std::abs(wet.r[0]) < 0.02,
              "first sample " + fmt(wet.l[0], 5));

        // Nothing nonlinear on the way: twice the input is twice the output.
        auto half = noise(48000.0, 1.5, 0.15f), full = noise(48000.0, 1.5, 0.3f);
        const auto a = run(wetSettings(rv::plate), half, 48000.0), b = run(wetSettings(rv::plate), full, 48000.0);
        double err = 0.0, ref = 0.0;
        for (std::size_t i = 0; i < a.l.size(); ++i) { err += std::pow(2.0 * a.l[i] - b.l[i], 2.0); ref += std::pow(b.l[i], 2.0); }
        check("Reverb_IsLinear", dbOf(err / ref) < -80.0, "residual " + fmt(dbOf(err / ref), 1) + " dB");
    }

    // ---- quality, per type ----------------------------------------------------
    struct Floor { double mixMs, ringOverNoiseDb, flatness; };
    const Floor floors[rv::kTypeCount] = {
        { 55.0, 5.0, 0.20 },   // ROOM: reflections first; colour from them is the room
        { 35.0, 4.0, 0.40 },   // PLATE
        { 80.0, 3.5, 0.40 },   // HALL
        { 0.0, 3.5, 0.40 },    // CLOUD: mixing time is the bloom, checked separately
        { 0.0, 8.0, 0.0 },     // SPRING: periodic by nature (it is a spring)
        { 60.0, 0.0, 0.0 },    // GATED: ringing measured inside the burst instead
    };
    for (int type = 0; type < rv::kTypeCount; ++type)
    {
        for (const auto decay : { 0.45f, 0.85f })
        {
            auto s = wetSettings(type);
            s.decay = decay;
            const auto seconds = type == rv::cloud ? 30.0 : (decay > 0.6f ? 16.0 : 8.0);
            const auto ir = impulse(s, 48000.0, seconds);
            const auto rt = rt60(ir.l, 48000.0);
            const auto tag = juce::String(decay > 0.6f ? "Long" : "");
            if (floors[type].mixMs > 0.0)
            {
                const auto mix = mixingTimeMs(ir.l, 48000.0);
                check(id(("BecomesDiffuseQuickly" + tag).toRawUTF8(), type).toRawUTF8(), mix <= floors[type].mixMs,
                      "mixing time " + fmt(mix, 0) + " ms (limit " + fmt(floors[type].mixMs, 0) + ")");
            }
            if (floors[type].ringOverNoiseDb > 0.0)
            {
                const auto start = rt > 4.0 ? 300.0 : 100.0;
                const auto dur = juce::jlimit(300.0, 1500.0, 0.6 * rt * 1000.0);
                const auto top = type == rv::spring ? 2500.0 : 8000.0;
                const auto r = ringing(ir.l, 48000.0, start, dur, rt, top);
                const auto refDb = ringingNoiseReferenceDb(48000.0, start, dur, rt, top);
                check(id(("TailDoesNotRing" + tag).toRawUTF8(), type).toRawUTF8(), r.worstDb - refDb <= floors[type].ringOverNoiseDb,
                      "worst peak " + fmt(r.worstDb, 1) + " dB at " + fmt(r.worstHz, 0) + " Hz; decaying noise reads "
                          + fmt(refDb, 1) + " (limit +" + fmt(floors[type].ringOverNoiseDb, 1) + ")");
            }
            if (floors[type].flatness > 0.0)
            {
                const auto flat = spectralFlatness(ir.l, 48000.0, 200.0);
                check(id(("TailIsSpectrallyFlat" + tag).toRawUTF8(), type).toRawUTF8(), flat >= floors[type].flatness,
                      "flatness " + fmt(flat, 3) + " (decaying noise ~0.55)");
            }
            if (type != rv::gated)
            {
                const auto corr = correlation(ir.l, ir.r, static_cast<std::size_t>(4800), static_cast<std::size_t>(48000));
                check(id(("IsDecorrelated" + tag).toRawUTF8(), type).toRawUTF8(), std::abs(corr) <= 0.2,
                      "inter-channel correlation " + fmt(corr, 3));
            }
            const auto bal = dbOf(energy(ir.l) / juce::jmax(1.0e-30, energy(ir.r)));
            check(id(("IsBalanced" + tag).toRawUTF8(), type).toRawUTF8(), std::abs(bal) <= 1.0, "L/R " + fmt(bal, 2) + " dB");
            if (type != rv::gated && type != rv::room)
            {
                const auto nonlin = decayNonlinearityDb(ir.l);
                check(id(("DecaysSmoothly" + tag).toRawUTF8(), type).toRawUTF8(), nonlin <= 3.0,
                      "ISO 3382 decay-curve nonlinearity " + fmt(nonlin, 2) + " dB");
            }
        }
    }
    {
        // CLOUD blooms rather than mixing at once, but is fully diffuse by 400 ms.
        const auto ir = impulse(wetSettings(rv::cloud), 48000.0, 3.0);
        const auto ed = echoDensity(ir.l, samplesFor(400.0, 48000.0), samplesFor(40.0, 48000.0));
        check("ReverbCLOUD_IsDiffuseByTheTimeItBlooms", ed >= 0.8, "echo density at 400 ms " + fmt(ed, 2));
    }

    // ---- DECAY is a time, at every rate -------------------------------------
    for (int type = 0; type < rv::kTypeCount; ++type)
    {
        if (type == rv::gated) continue;
        for (const auto d : { 0.25f, 0.7f })
        {
            auto s = wetSettings(type);
            s.decay = d;
            const auto target = rv::decaySeconds(type, d);
            const auto seconds = juce::jlimit(4.0, 50.0, 3.0 * static_cast<double>(target));
            const auto measured = bandRt60(impulse(s, 48000.0, seconds).l, 48000.0, 1000.0);
            check(id(("DecayIsTheMarkedTime_" + juce::String(d, 2)).toRawUTF8(), type).toRawUTF8(),
                  measured > 0.7 * target && measured < 1.3 * target,
                  "1 kHz RT60 " + fmt(measured, 2) + " s for " + fmt(target, 2) + " s");
        }
        auto s = wetSettings(type);
        const auto at48 = bandRt60(impulse(s, 48000.0, 20.0).l, 48000.0, 1000.0);
        double worst = 0.0;
        for (const auto rate : { 44100.0, 96000.0, 192000.0 })
            worst = juce::jmax(worst, std::abs(bandRt60(impulse(s, rate, type == rv::cloud ? 20.0 : 8.0).l, rate, 1000.0) / at48 - 1.0));
        check(id("DecayIsTheSameAtEverySampleRate", type).toRawUTF8(), worst < 0.1,
              "worst deviation from 48 kHz " + fmt(100.0 * worst, 1) + " % (RT60 " + fmt(at48, 2) + " s)");
    }

    // ---- DAMPING darkens, LOW lengthens the low end -----------------------
    for (int type = 0; type < rv::kTypeCount; ++type)
    {
        if (type == rv::gated) continue;
        auto bright = wetSettings(type), dark = wetSettings(type);
        bright.damping = 0.0f;
        dark.damping = 1.0f;
        const auto seconds = type == rv::cloud ? 30.0 : 10.0;
        const auto hb = bandRt60(impulse(bright, 48000.0, seconds).l, 48000.0, type == rv::spring ? 2000.0 : 4000.0);
        const auto hd = bandRt60(impulse(dark, 48000.0, seconds).l, 48000.0, type == rv::spring ? 2000.0 : 4000.0);
        check(id("DampingShortensTheHighEnd", type).toRawUTF8(), hd < 0.7 * hb,
              "4 kHz RT60 " + fmt(hb, 2) + " s at DAMPING 0, " + fmt(hd, 2) + " s at 1");
        if (rv::isLive(rv::Control::low, type))
        {
            auto shortLow = wetSettings(type), longLow = wetSettings(type);
            shortLow.low = 0.0f;
            longLow.low = 1.0f;
            const auto ls = bandRt60(impulse(shortLow, 48000.0, seconds).l, 48000.0, 125.0);
            const auto ll = bandRt60(impulse(longLow, 48000.0, seconds).l, 48000.0, 125.0);
            check(id("LowSetsTheBassDecay", type).toRawUTF8(), ll > 1.8 * ls,
                  "125 Hz RT60 " + fmt(ls, 2) + " s at LOW 0, " + fmt(ll, 2) + " s at 1");
        }
    }

    // ---- stability at the extremes, every rate ------------------------------
    for (int type = 0; type < rv::kTypeCount; ++type)
    {
        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto s = wetSettings(type);
            s.decay = 1.0f;
            s.low = 1.0f;
            s.damping = 0.0f;
            s.diffusion = 1.0f;
            s.modulation = 1.0f;
            s.shimmer = 1.0f;
            s.drip = 1.0f;
            s.size = 1.0f;
            const auto tailSeconds = rate > 100000.0 ? 4.0 : 8.0;
            auto in = noise(rate, 2.0 + tailSeconds, 0.5f);
            std::fill(in.begin() + static_cast<std::ptrdiff_t>(2.0 * rate), in.end(), 0.0f);
            const auto out = run(s, in, rate);
            const auto second = static_cast<std::size_t>(rate);
            const auto first = energy(out.l, 2 * second, 3 * second);
            const auto last = energy(out.l, out.l.size() - second, out.l.size());
            check(id(("StableAtTheExtremes_" + juce::String(static_cast<int>(rate))).toRawUTF8(), type).toRawUTF8(),
                  allFinite(out) && peakOf(out) < 4.0 && last < first,
                  "peak " + fmt(peakOf(out), 3) + ", first tail second " + fmt(dbOf(first), 1) + " dB, last " + fmt(dbOf(last), 1) + " dB");
        }
    }

    // ---- bad input, silence, reset, switching --------------------------------
    {
        auto in = noise(48000.0, 2.0, 0.3f);
        for (int k = 0; k < 16; ++k) in[static_cast<std::size_t>(1000 + k)] = std::numeric_limits<float>::quiet_NaN();
        in[5000] = std::numeric_limits<float>::infinity();
        bool ok = true;
        for (int type = 0; type < rv::kTypeCount; ++type)
        {
            auto s = wetSettings(type);
            s.amount = 0.5f;
            const auto out = run(s, in, 48000.0);
            ok = ok && allFinite(out) && energy(out.l, 48000, 96000) > 1.0e-6;
        }
        check("Reverb_NonFiniteInputIsAbsorbed", ok, "NaN and Inf in, finite reverb out, still working afterwards");
    }
    {
        // Denormal-range input must neither poison the output nor keep the
        // engine awake.
        auto reverb = std::make_unique<::Reverb>();
        reverb->prepare(48000.0);
        auto s = wetSettings(rv::cloud);
        s.decay = 1.0f;
        bool finite = true;
        for (int n = 0; n < 48000 * 3; ++n)
        {
            if (n % 512 == 0) reverb->updateForBlock(s, 512);
            float l = 0.0f, r = 0.0f;
            reverb->processSampleFrame(n < 48000 ? 0.3f * std::sin(0.05f * static_cast<float>(n)) : 1.0e-30f, 1.0e-30f, l, r);
            finite = finite && std::isfinite(l) && std::isfinite(r);
        }
        check("Reverb_DenormalInputIsHarmless", finite, "a 60 s tail fed 1e-30 for 2 s stays finite");
        // ... and a long tail into silence sleeps once it is inaudible.
        auto r2 = std::make_unique<::Reverb>();
        r2->prepare(48000.0);
        auto h = wetSettings(rv::room);
        for (int n = 0; n < 48000 * 12; ++n)
        {
            if (n % 512 == 0) r2->updateForBlock(h, 512);
            float l = 0.0f, r = 0.0f;
            r2->processSampleFrame(n < 4800 ? 0.3f : 0.0f, n < 4800 ? 0.3f : 0.0f, l, r);
        }
        check("Reverb_SleepsInSilence", ! r2->isRunning(), "running after 12 s of silence: " + juce::String(r2->isRunning() ? "yes" : "no"));
    }
    {
        // reset() leaves nothing behind.
        auto reverb = std::make_unique<::Reverb>();
        reverb->prepare(48000.0);
        auto s = wetSettings(rv::hall);
        s.decay = 1.0f;
        for (int n = 0; n < 48000; ++n)
        {
            if (n % 512 == 0) reverb->updateForBlock(s, 512);
            float l = 0.0f, r = 0.0f;
            reverb->processSampleFrame(0.3f * std::sin(0.03f * static_cast<float>(n)), 0.0f, l, r);
        }
        reverb->reset();
        double residual = 0.0;
        for (int n = 0; n < 48000; ++n)
        {
            if (n % 512 == 0) reverb->updateForBlock(s, 512);
            float l = 0.0f, r = 0.0f;
            reverb->processSampleFrame(0.0f, 0.0f, l, r);
            residual = juce::jmax(residual, static_cast<double>(std::abs(l) + std::abs(r)));
        }
        check("Reverb_ResetClearsEverything", residual == 0.0, "largest sample after reset " + fmt(residual, 9));
    }
    {
        // No stale tail: a type switched away from and back, or MIX taken to
        // zero and back up, must not replay what was in the lines.
        auto in = noise(48000.0, 6.0, 0.3f);
        std::fill(in.begin() + 48000, in.end(), 0.0f);
        auto s = wetSettings(rv::hall);
        s.decay = 1.0f;
        const auto switched = run(s, in, 48000.0, [](int block, ReverbSettings& st)
        {
            if (block == 120) st.algorithmIndex = rv::plate;
            if (block == 160) st.algorithmIndex = rv::hall;
        });
        const auto replay = energy(switched.l, static_cast<std::size_t>(170 * 512), static_cast<std::size_t>(260 * 512));
        check("Reverb_TypeSwitchDoesNotReplayAnOldTail", replay < 1.0e-10, "energy after switching back " + fmt(dbOf(replay), 1) + " dB");
        const auto muted = run(s, in, 48000.0, [](int block, ReverbSettings& st)
        {
            if (block == 120) st.amount = 0.0f;
            if (block == 200) st.amount = 1.0f;
        });
        const auto back = energy(muted.l, static_cast<std::size_t>(210 * 512), static_cast<std::size_t>(300 * 512));
        check("Reverb_MixBackUpDoesNotReplayAnOldTail", back < 1.0e-10, "energy after MIX returns " + fmt(dbOf(back), 1) + " dB");
    }
    {
        // Deterministic: two instances, same input, same output to the bit.
        const auto in = noise(48000.0, 2.0, 0.3f);
        bool same = true;
        for (int type = 0; type < rv::kTypeCount; ++type)
        {
            auto s = wetSettings(type);
            s.shimmer = 0.6f;
            const auto a = run(s, in, 48000.0), b = run(s, in, 48000.0);
            same = same && a.l == b.l && a.r == b.r;
        }
        check("Reverb_IsDeterministic", same, "every type, two instances, bit-identical");
    }

    // ---- no zipper, no click, whatever moves -------------------------------
    // Measured as the crest of the output's second difference in 10 ms
    // windows: a step stands far out of its window, a glide or new
    // high-frequency content (SHIMMER) does not.
    {
        const auto in = sine(48000.0, 4.0, 220.0, 0.3f);
        constexpr double kLimit = 9.0;
        for (int type = 0; type < rv::kTypeCount; ++type)
        {
            auto s = wetSettings(type);
            s.amount = 0.6f;
            const auto still = worstSecondDifferenceCrest(run(s, in, 48000.0).l, 48000, 48000.0);
            double worst = 0.0;
            juce::String worstName;
            for (int c = 0; c < rv::kControlCount; ++c)
            {
                const auto control = static_cast<rv::Control>(c);
                const auto moved = run(s, in, 48000.0, [control](int block, ReverbSettings& st)
                {
                    // 0 -> 1 -> 0 over ~3 s, in block-sized steps, as a host
                    // or an LFO would deliver it.
                    const auto t = static_cast<float>(block) / 280.0f;
                    rv::settingsField(st, control) = t < 0.5f ? 2.0f * t : juce::jmax(0.0f, 2.0f - 2.0f * t);
                });
                const auto crest = worstSecondDifferenceCrest(moved.l, 48000, 48000.0);
                if (crest > worst) { worst = crest; worstName = rv::spec(control).id; }
            }
            const auto mixed = run(s, in, 48000.0, [](int block, ReverbSettings& st)
            { st.amount = 0.5f + 0.5f * std::sin(static_cast<float>(block) * 0.05f); });
            const auto mixCrest = worstSecondDifferenceCrest(mixed.l, 48000, 48000.0);
            if (mixCrest > worst) { worst = mixCrest; worstName = "fx.reverb.amount"; }
            check(id("NoZipperOrClickOnAnyControl", type).toRawUTF8(), worst < kLimit,
                  "worst second-difference crest " + fmt(worst, 2) + " (" + worstName + "), still " + fmt(still, 2));
        }
        auto s = wetSettings(rv::room);
        s.amount = 0.6f;
        const auto switching = run(s, in, 48000.0, [](int block, ReverbSettings& st)
        { st.algorithmIndex = (block / 30) % rv::kTypeCount; });
        const auto crest = worstSecondDifferenceCrest(switching.l, 48000, 48000.0);
        check("Reverb_TypeSwitchesAreClickFree", crest < kLimit, "worst second-difference crest " + fmt(crest, 2) + ", switching every 0.3 s");
    }

    // ---- stereo -----------------------------------------------------------------
    for (int type = 0; type < rv::kTypeCount; ++type)
    {
        auto narrow = wetSettings(type), wide = wetSettings(type);
        narrow.width = 0.0f;
        const auto in = noise(48000.0, 2.0, 0.3f);
        const auto a = run(narrow, in, 48000.0), b = run(wide, in, 48000.0);
        double diff = 0.0, monoA = 0.0, monoB = 0.0;
        for (std::size_t i = 0; i < a.l.size(); ++i)
        {
            diff = juce::jmax(diff, static_cast<double>(std::abs(a.l[i] - a.r[i])));
            monoA += std::pow(0.5 * (a.l[i] + a.r[i]), 2.0);
            monoB += std::pow(0.5 * (b.l[i] + b.r[i]), 2.0);
        }
        check(id("WidthZeroIsMono", type).toRawUTF8(), diff < 1.0e-6, "largest L-R " + fmt(diff, 8));
        check(id("WidthNeverChangesTheMonoSum", type).toRawUTF8(), std::abs(dbOf(monoA / monoB)) < 0.05,
              "mono sum at WIDTH 0 vs 1: " + fmt(dbOf(monoA / monoB), 3) + " dB");
    }
    {
        // A source panned hard left keeps its side: the reverb is true stereo.
        const auto left = noise(48000.0, 2.0, 0.3f);
        const std::vector<float> silent(left.size(), 0.0f);
        for (const auto type : { rv::room, rv::hall, rv::cloud })
        {
            const auto out = run(wetSettings(type), left, 48000.0, {}, &silent);
            const auto side = dbOf(energy(out.l) / juce::jmax(1.0e-30, energy(out.r)));
            check(id("KeepsAPannedSourceOnItsSide", type).toRawUTF8(), side > 1.0, "left over right " + fmt(side, 2) + " dB");
        }
    }

    // ---- PRE-DELAY --------------------------------------------------------------
    {
        // Against the type's own first arrival at zero pre-delay (a plate's
        // tank answers after ~8 ms, a room's first reflection after ~4).
        const auto onsetMs = [](int type, float ms)
        {
            auto s = wetSettings(type);
            s.preDelay = rv::preDelayNormalised(ms);
            const auto ir = impulse(s, 48000.0, 0.6);
            std::size_t onset = 0;
            while (onset < ir.l.size() && std::abs(ir.l[onset]) + std::abs(ir.r[onset]) < 1.0e-5f) ++onset;
            return static_cast<double>(onset) / 48.0;
        };
        for (const auto type : { rv::plate, rv::room })
        {
            const auto base = onsetMs(type, 0.0f);
            for (const auto ms : { 50.0f, 250.0f })
            {
                const auto added = onsetMs(type, ms) - base;
                check((id(("PreDelayIs_" + juce::String(static_cast<int>(ms)) + "ms").toRawUTF8(), type)).toRawUTF8(),
                      std::abs(added - ms) <= 0.5, "adds " + fmt(added, 2) + " ms to a first arrival at " + fmt(base, 2) + " ms");
            }
        }
    }

    // ---- level match across types -------------------------------------------
    {
        const auto in = noise(48000.0, 4.0, 0.3f);
        double lo = 1.0e9, hi = -1.0e9;
        juce::String detail;
        for (int type = 0; type < rv::kTypeCount; ++type)
        {
            const auto out = run(wetSettings(type), in, 48000.0);
            const auto level = dbOf(energy(out.l, 96000, out.l.size()) / energy(in, 96000, in.size()));
            lo = juce::jmin(lo, level);
            hi = juce::jmax(hi, level);
            detail << name(type) << " " << juce::String(level, 1) << "  ";
        }
        check("Reverb_TypesAreLevelMatched", hi - lo < 3.0, detail + "dB");
    }

    // ---- inert by design --------------------------------------------------------
    {
        const auto in = noise(48000.0, 1.0, 0.3f);
        juce::StringArray live;
        for (int type = 0; type < rv::kTypeCount; ++type)
            for (int c = 0; c < rv::kControlCount; ++c)
            {
                const auto control = static_cast<rv::Control>(c);
                if (rv::isLive(control, type)) continue;
                auto a = wetSettings(type), b = wetSettings(type);
                rv::settingsField(a, control) = 0.0f;
                rv::settingsField(b, control) = 1.0f;
                if (run(a, in, 48000.0).l != run(b, in, 48000.0).l) live.add(juce::String(name(type)) + ":" + rv::spec(control).id);
            }
        check("Reverb_InertControlsAreReallyInert", live.isEmpty(),
              live.isEmpty() ? juce::String("every control a type does not read leaves it bit-identical") : "moves: " + live.joinIntoString(", "));
        juce::StringArray dead;
        for (int type = 0; type < rv::kTypeCount; ++type)
            for (int c = 0; c < rv::kControlCount; ++c)
            {
                const auto control = static_cast<rv::Control>(c);
                if (! rv::isLive(control, type) || control == rv::Control::preDelay) continue;
                auto a = wetSettings(type), b = wetSettings(type);
                rv::settingsField(a, control) = 0.1f;
                rv::settingsField(b, control) = 0.9f;
                const auto oa = run(a, in, 48000.0), ob = run(b, in, 48000.0);
                double d = 0.0;
                for (std::size_t i = 0; i < oa.l.size(); ++i) d += std::pow(oa.l[i] - ob.l[i], 2.0) + std::pow(oa.r[i] - ob.r[i], 2.0);
                if (d < 1.0e-6 * energy(oa.l)) dead.add(juce::String(name(type)) + ":" + rv::spec(control).id);
            }
        check("Reverb_EveryLiveControlChangesTheSound", dead.isEmpty(), dead.isEmpty() ? juce::String("all live") : "no effect: " + dead.joinIntoString(", "));
    }

    // ---- SHIMMER --------------------------------------------------------------
    {
        const auto in = sine(48000.0, 8.0, 440.0, 0.3f, 1.0);
        double plain[3] {}, shim[3] {};
        double earlyE = 0.0, lateE = 0.0;
        for (const auto amount : { 0.0f, 0.6f })
        {
            auto s = wetSettings(rv::cloud);
            s.decay = 0.5f;
            s.shimmer = amount;
            const auto out = run(s, in, 48000.0);
            auto& dst = amount > 0.0f ? shim : plain;
            const auto from = static_cast<std::size_t>(2 * 48000), to = static_cast<std::size_t>(6 * 48000);
            const auto fundamental = bandEnergy(out.l, from, to, 440.0, 48000.0);
            dst[0] = dbOf(bandEnergy(out.l, from, to, 880.0, 48000.0) / fundamental);
            dst[1] = dbOf(bandEnergy(out.l, from, to, 660.0, 48000.0) / fundamental);
            dst[2] = dbOf(bandEnergy(out.l, from, to, 1760.0, 48000.0) / fundamental);
            if (amount > 0.0f)
            {
                earlyE = energy(out.l, 48000 * 2, 48000 * 3);
                lateE = energy(out.l, 48000 * 7, 48000 * 8);
            }
        }
        check("ReverbCLOUD_ShimmerRegeneratesAnOctaveUp", shim[0] > plain[0] + 25.0 && shim[2] > plain[2] + 20.0,
              "880 Hz re 440: off " + fmt(plain[0], 1) + " dB, on " + fmt(shim[0], 1) + " dB; 1760 Hz off " + fmt(plain[2], 1) + ", on " + fmt(shim[2], 1));
        check("ReverbCLOUD_ShimmerIsCleanOctaves", shim[1] < -30.0, "660 Hz (no octave) re 440 with shimmer: " + fmt(shim[1], 1) + " dB");
        check("ReverbCLOUD_ShimmerStillDecays", lateE < earlyE * 0.1, "second 7 against second 2: " + fmt(dbOf(lateE / earlyE), 1) + " dB");

        // Alias floor at 44.1 kHz: a 20 kHz tone shifted an octave would fold
        // to 4.1 kHz without the band-limit in front of the shifter.
        const auto tone = sine(44100.0, 4.0, 20000.0, 0.5f);
        double fold[2] {};
        for (int k = 0; k < 2; ++k)
        {
            auto s = wetSettings(rv::cloud);
            s.damping = 0.0f;
            s.shimmer = k == 0 ? 0.0f : 1.0f;
            const auto out = run(s, tone, 44100.0);
            fold[k] = bandEnergy(out.l, 44100, 4 * 44100, 4100.0, 44100.0) / bandEnergy(tone, 44100, 4 * 44100, 20000.0, 44100.0);
        }
        check("ReverbCLOUD_ShimmerDoesNotAlias", dbOf(fold[1]) < -60.0,
              "4.1 kHz fold of a 20 kHz input: " + fmt(dbOf(fold[1]), 1) + " dB (shimmer off " + fmt(dbOf(fold[0]), 1) + ")");
    }

    // ---- SPRING disperses -------------------------------------------------------
    {
        auto s = wetSettings(rv::spring);
        s.diffusion = 0.0f;
        s.drip = 0.2f;
        const auto ir = impulse(s, 48000.0, 2.0);
        const auto low = echoPeriodMs(ir.l, 48000.0, 500.0), high = echoPeriodMs(ir.l, 48000.0, 2000.0);
        check("ReverbSPRING_HighsTakeLongerEveryRoundTrip", high > 1.25 * low,
              "echo period 500 Hz " + fmt(low, 1) + " ms, 2 kHz " + fmt(high, 1) + " ms");
        const auto a = firstArrivalMs(ir.l, 48000.0, 300.0, 60.0), b = firstArrivalMs(ir.l, 48000.0, 2000.0, 60.0);
        check("ReverbSPRING_EachEchoIsAnUpwardChirp", b > a + 5.0, "first arrival 300 Hz " + fmt(a, 1) + " ms, 2 kHz " + fmt(b, 1) + " ms");
    }

    // ---- GATED: a shaped burst that stops ---------------------------------------
    {
        for (const auto shape : { 0.0f, 0.5f, 1.0f })
        {
            auto s = wetSettings(rv::gated);
            s.shape = shape;
            const auto ir = impulse(s, 48000.0, 1.5);
            const auto length = rv::decaySeconds(rv::gated, s.decay);
            const auto third = static_cast<std::size_t>(length / 3.0 * 48000.0);
            const auto e1 = energy(ir.l, 0, third), e3 = energy(ir.l, 2 * third, 3 * third);
            const auto after = energy(ir.l, static_cast<std::size_t>((length + 0.04) * 48000.0), ir.l.size());
            const auto total = energy(ir.l);
            const auto label = juce::String(shape < 0.25f ? "Reverse" : (shape > 0.75f ? "Falling" : "Flat"));
            check(("ReverbGATED_StopsAfterItsLength_" + label).toRawUTF8(), dbOf(after / total) < -50.0,
                  "energy after the gate " + fmt(dbOf(after / total), 1) + " dB");
            const auto tilt = dbOf(e3 / e1);
            const auto ok = shape < 0.25f ? tilt > 3.0 : (shape > 0.75f ? tilt < -3.0 : std::abs(tilt) < 4.0);
            check(("ReverbGATED_ShapeDrawsTheEnvelope_" + label).toRawUTF8(), ok, "last third against first " + fmt(tilt, 1) + " dB");
        }
    }

    // ---- memory -------------------------------------------------------------
    {
        const auto at48 = ::Reverb::arenaFloatsFor(48000.0) * sizeof(float);
        const auto at192 = ::Reverb::arenaFloatsFor(192000.0) * sizeof(float);
        check("Reverb_ArenaIsSizedForTheLargestTypeOnly", at48 < 1300000 && at192 < 5200000,
              "arena " + fmt(static_cast<double>(at48) / 1.0e6, 2) + " MB at 48 kHz, " + fmt(static_cast<double>(at192) / 1.0e6, 2) + " MB at 192 kHz");
    }

    // ---- presets -------------------------------------------------------------
    {
        bool countsOk = true, namesOk = true;
        juce::StringArray seen;
        juce::String counts;
        for (int type = 0; type < rv::kTypeCount; ++type)
        {
            const auto list = rv::presetsForType(type);
            counts << name(type) << " " << static_cast<int>(list.size()) << "  ";
            countsOk = countsOk && list.size() >= 4 && list.size() <= 8;
            for (const auto& p : list)
            {
                const juce::String n(p.name);
                namesOk = namesOk && ! n.containsIgnoreCase("default") && ! n.containsIgnoreCase("preset") && ! seen.contains(n)
                          && juce::String(p.purpose).isNotEmpty();
                seen.add(n);
                // Each preset must reproduce itself.
                ReverbSettings s;
                rv::applyPresetToSettings(p, s);
                namesOk = namesOk && rv::presetMatches(p, type, [&s](rv::Control c) { return rv::settingsField(s, c); });
            }
        }
        check("ReverbPresets_FourToEightPerType", countsOk, counts);
        check("ReverbPresets_NamedByUseAndUnique", namesOk, juce::String(seen.size()) + " presets");
    }
    {
        // Applying one through the processor sets every control, leaves MIX
        // alone, survives a state round trip and reads "modified" once a knob moves.
        PX3SynthAudioProcessor processor;
        setParam(processor, "fx.reverb.amount", 0.42f);
        processor.applyReverbPreset(rv::cloud, "Shimmer Wash");
        const auto* preset = rv::findPreset(rv::cloud, "Shimmer Wash");
        auto current = [&processor](rv::Control c) { return static_cast<juce::RangedAudioParameter&>(processor.getReverbControlParam(c)).getValue(); };
        check("ReverbPresets_ApplySetsTheTypeAndControls",
              preset != nullptr && processor.getReverbAlgorithmParam().getIndex() == rv::cloud && rv::presetMatches(*preset, rv::cloud, current)
                  && std::abs(processor.getReverbAmountParam().get() - 0.42f) < 1.0e-4f,
              "selection " + processor.getReverbPresetSelection());
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        auto restoredCurrent = [&restored](rv::Control c) { return static_cast<juce::RangedAudioParameter&>(restored.getReverbControlParam(c)).getValue(); };
        check("ReverbPresets_SelectionSurvivesTheState",
              restored.getReverbPresetSelection() == "CLOUD/Shimmer Wash" && preset != nullptr && rv::presetMatches(*preset, rv::cloud, restoredCurrent),
              "restored '" + restored.getReverbPresetSelection() + "'");
        setParam(processor, "fx.reverb.shimmer", 0.9f);
        check("ReverbPresets_MovingAKnobMarksItModified", preset != nullptr && ! rv::presetMatches(*preset, rv::cloud, current), "shimmer moved");

        PX3ReverbAudioProcessor fx;
        fx.applyPreset(rv::plate, "Short Decay");
        juce::MemoryBlock fxState;
        fx.getStateInformation(fxState);
        PX3ReverbAudioProcessor fxRestored;
        fxRestored.setStateInformation(fxState.getData(), static_cast<int>(fxState.getSize()));
        check("ReverbPresets_StandaloneKeepsItsSelection", fxRestored.getPresetSelection() == "PLATE/Short Decay"
                                                               && fxRestored.algorithm().getIndex() == rv::plate,
              "restored '" + fxRestored.getPresetSelection() + "'");
    }
    {
        // The card: PRESET lists the type's presets only; the type slot shows
        // the type's own control; inert controls are dimmed; "Name*" once moved.
        PX3ReverbAudioProcessor fx;
        std::unique_ptr<juce::AudioProcessorEditor> editor(fx.createEditor());
        auto* reverbEditor = dynamic_cast<PX3ReverbAudioProcessorEditor*>(editor.get());
        bool ok = reverbEditor != nullptr;
        juce::String detail;
        if (ok)
        {
            fx.algorithm().setValueNotifyingHost(fx.algorithm().convertTo0to1(static_cast<float>(rv::cloud)));
            reverbEditor->debugRefresh();
            fx.applyPreset(rv::cloud, "Octave Bloom");
            reverbEditor->debugRefresh();
            juce::ComboBox* preset = nullptr;
            std::function<void(juce::Component&)> find = [&](juce::Component& c)
            {
                if (auto* card = dynamic_cast<px3::ui::FxCardComponent*>(&c)) { preset = card->choice("preset"); return; }
                for (auto* child : c.getChildren()) if (preset == nullptr) find(*child);
            };
            find(*editor);
            ok = preset != nullptr && preset->getNumItems() == static_cast<int>(rv::presetsForType(rv::cloud).size())
                 && preset->getText() == "Octave Bloom";
            detail << "items " << (preset != nullptr ? preset->getNumItems() : -1) << ", shows '" << (preset != nullptr ? preset->getText() : "") << "'";
            fx.control(rv::Control::shimmer).setValueNotifyingHost(0.1f);
            reverbEditor->debugRefresh();
            ok = ok && preset != nullptr && preset->getText() == "Octave Bloom*";
            detail << ", after a knob '" << (preset != nullptr ? preset->getText() : juce::String()) << "'";
        }
        check("ReverbCard_PresetMenuFollowsTypeAndMarksEdits", ok, detail);
    }
}

// PX3Tests reverbmetrics: the measures of every type at three decays.
int runReverbMetricsReport()
{
    std::printf("\nREVERB METRICS (impulse, fully wet; ring = worst tail peak over decaying noise)\n");
    std::printf("  %-7s %5s %6s %6s %6s %6s %6s %6s %7s\n", "type", "decay", "rt60", "mixMs", "ring+", "flat", "nonlin", "corr", "bal dB");
    for (int type = 0; type < rv::kTypeCount; ++type)
        for (const auto decay : { 0.2f, 0.45f, 0.85f })
        {
            auto s = wetSettings(type);
            s.decay = decay;
            const auto ir = impulse(s, 48000.0, type == rv::cloud ? 40.0 : 16.0);
            const auto rt = rt60(ir.l, 48000.0);
            const auto start = rt > 4.0 ? 300.0 : 100.0;
            const auto dur = juce::jlimit(300.0, 1500.0, 0.6 * rt * 1000.0);
            const auto top = type == rv::spring ? 2500.0 : 8000.0;
            const auto ring = ringing(ir.l, 48000.0, start, dur, rt, top).worstDb - ringingNoiseReferenceDb(48000.0, start, dur, rt, top);
            std::printf("  %-7s %5.2f %6.2f %6.0f %+6.1f %6.3f %6.2f %+6.2f %+7.2f\n", name(type), decay, rt, mixingTimeMs(ir.l, 48000.0), ring,
                        spectralFlatness(ir.l, 48000.0, 200.0), decayNonlinearityDb(ir.l),
                        correlation(ir.l, ir.r, 4800, 48000), dbOf(energy(ir.l) / energy(ir.r)));
            std::fflush(stdout);
        }
    return 0;
}
} // namespace px3tests
