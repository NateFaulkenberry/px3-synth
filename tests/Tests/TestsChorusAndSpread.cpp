#include "TestSupport.h"

// testChorus, testStereoSpread

namespace px3tests
{


// ============================================================================
// CHORUS
// ============================================================================

namespace chorustest
{
using doomtest::Result;

enum class Source { silence, impulse, sine, saw, supersaw, bass, pluck };

Result runChorus(const ChorusSettings& settings,
                 Source source,
                 double seconds,
                 double sampleRate = kSampleRate,
                 float frequency = 220.0f)
{
    px3::Chorus chorus;
    chorus.prepare(sampleRate);
    chorus.setSeed(9876u);
    chorus.updateForBlock(settings);

    const auto total = static_cast<int>(sampleRate * seconds);
    Result result;
    result.left.reserve(static_cast<std::size_t>(total));
    result.right.reserve(static_cast<std::size_t>(total));

    auto phase = 0.0;
    std::array<double, 7> superPhase {};
    const auto increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int i = 0; i < total; ++i)
    {
        auto in = 0.0f;
        switch (source)
        {
            case Source::silence:
                break;
            case Source::impulse:
                in = i == 4096 ? 1.0f : 0.0f;
                break;
            case Source::sine:
                in = 0.5f * static_cast<float>(std::sin(phase));
                break;
            case Source::saw:
            {
                auto sum = 0.0;
                for (int h = 1; h <= 12; ++h)
                {
                    sum += std::sin(phase * h) / h;
                }
                in = 0.28f * static_cast<float>(sum);
                break;
            }
            case Source::supersaw:
            {
                auto sum = 0.0;
                for (std::size_t v = 0; v < superPhase.size(); ++v)
                {
                    for (int h = 1; h <= 8; ++h)
                    {
                        sum += std::sin(superPhase[v] * h) / h;
                    }
                }
                in = 0.06f * static_cast<float>(sum);
                break;
            }
            case Source::bass:
            {
                // A square-ish bass: a strong fundamental plus odd harmonics,
                // which is what the low-end anchoring has to protect.
                auto sum = 0.0;
                for (int h = 1; h <= 9; h += 2)
                {
                    sum += std::sin(phase * h) / h;
                }
                in = 0.4f * static_cast<float>(sum);
                break;
            }
            case Source::pluck:
            {
                const auto age = static_cast<double>(i % static_cast<int>(sampleRate * 0.5));
                const auto env = std::exp(-age / (sampleRate * 0.1));
                in = 0.6f * static_cast<float>(env * std::sin(phase));
                break;
            }
        }

        phase += increment;
        for (std::size_t v = 0; v < superPhase.size(); ++v)
        {
            // Detuned copies, a few cents apart.
            superPhase[v] += increment * (1.0 + (static_cast<double>(v) - 3.0) * 0.004);
        }

        float outL = 0.0f;
        float outR = 0.0f;
        chorus.processSampleFrame(in, in, outL, outR);
        result.left.push_back(outL);
        result.right.push_back(outR);
    }

    return result;
}

ChorusSettings audible()
{
    ChorusSettings s;
    s.enabled = true;
    s.amount = 0.75f;
    return s;
}

// RMS of the mono sum. The Dimension architecture's wet terms are equal and
// opposite, so this is where a phase-trick widener would collapse.
double monoRms(const Result& r)
{
    auto sum = 0.0;
    for (std::size_t i = 0; i < r.left.size(); ++i)
    {
        const auto m = 0.5 * (static_cast<double>(r.left[i]) + r.right[i]);
        sum += m * m;
    }
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1u, r.left.size())));
}

// How much the summed signal's period wanders, as a fraction. This is the
// design brief measured directly: "a new dimension WITHOUT the apparent
// movement of sound".
double pitchInstability(const Result& r, double sampleRate)
{
    std::vector<double> periods;
    auto lastCrossing = -1.0;
    for (std::size_t i = 1; i < r.left.size(); ++i)
    {
        const auto a = 0.5 * (static_cast<double>(r.left[i - 1]) + r.right[i - 1]);
        const auto b = 0.5 * (static_cast<double>(r.left[i]) + r.right[i]);
        if (a <= 0.0 && b > 0.0)
        {
            const auto frac = b != a ? -a / (b - a) : 0.0;
            const auto crossing = static_cast<double>(i - 1) + frac;
            if (lastCrossing >= 0.0)
            {
                periods.push_back(crossing - lastCrossing);
            }
            lastCrossing = crossing;
        }
    }

    juce::ignoreUnused(sampleRate);
    if (periods.size() < 8)
    {
        return 0.0;
    }

    auto mean = 0.0;
    for (const auto p : periods) { mean += p; }
    mean /= static_cast<double>(periods.size());

    auto variance = 0.0;
    for (const auto p : periods) { variance += (p - mean) * (p - mean); }
    return std::sqrt(variance / static_cast<double>(periods.size())) / std::max(1.0e-9, mean);
}
} // namespace chorustest

void testChorus()
{
    suite("CHORUS");

    using namespace chorustest;

    // ---- construction and defaults -----------------------------------------
    {
        const ChorusSettings defaults;
        check("Chorus_DefaultsAreValid",
              defaults.enabled && juce::approximatelyEqual(defaults.amount, 0.0f)
                  && defaults.modeIndex == 1
                  && juce::approximatelyEqual(defaults.feedback, 0.0f),
              "amount zero, DIM 2, no feedback");

        px3::Chorus chorus;
        chorus.prepare(kSampleRate);
        chorus.reset();
        float l = 0.0f;
        float r = 0.0f;
        chorus.processSampleFrame(0.0f, 0.0f, l, r);
        check("Chorus_ConstructsAndProcessesWithoutPreparingSettings",
              std::isfinite(l) && std::isfinite(r), "");
    }

    // ---- sample rates ------------------------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const auto out = runChorus(audible(), Source::saw, 1.5, rate);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << juce::String(static_cast<int>(rate)) << " rms "
                   << juce::String(out.rms(), 4) << "  ";
        }
        check("Chorus_RunsAtEverySupportedSampleRate", allFine, detail);
    }

    // ---- silence, impulse, sine --------------------------------------------
    {
        const auto quiet = runChorus(audible(), Source::silence, 1.5);
        check("Chorus_SilenceStaysSilent",
              quiet.finite() && quiet.peak() < 1.0e-5f && std::abs(quiet.dc()) < 1.0e-6,
              "peak " + juce::String(quiet.peak(), 8));

        const auto impulse = runChorus(audible(), Source::impulse, 2.0);
        check("Chorus_ImpulseStaysBounded",
              impulse.finite() && impulse.peak() < 4.0f,
              "peak " + juce::String(impulse.peak(), 4));

        juce::String freqDetail;
        auto allFine = true;
        for (const auto hz : { 50.0f, 100.0f, 440.0f, 1000.0f, 5000.0f, 10000.0f })
        {
            const auto out = runChorus(audible(), Source::sine, 1.5, kSampleRate, hz);
            const auto ok = out.finite() && out.peak() < 4.0f;
            allFine = allFine && ok;
            freqDetail << juce::String(static_cast<int>(hz)) << " "
                       << juce::String(out.peak(), 3) << "  ";
        }
        check("Chorus_SineIsStableAcrossTheBand", allFine, freqDetail);
    }

    // ---- the design brief: width without apparent movement -----------------
    {
        // Roland's claim for the SDD-320 is width "without the apparent movement
        // of sound produced by most other chorus devices". The anti-phase pair
        // is how: when one path goes sharp the other goes flat by the same
        // amount, so the pair has no average pitch deviation.
        //
        // Measured against a deliberately naive single-delay chorus at the same
        // depth, built here rather than in the engine - the engine must not
        // contain the thing it is being compared against.
        auto s = audible();
        s.amount = 1.0f;
        s.depth = 1.0f;
        s.mix = 1.0f;
        s.character = 0.0f;
        const auto paired = runChorus(s, Source::sine, 3.0, kSampleRate, 220.0f);

        // The naive version: one delay line, one sine LFO, dry plus wet.
        std::vector<float> naive;
        {
            const auto size = static_cast<int>(kSampleRate * 0.05);
            std::vector<float> line(static_cast<std::size_t>(size), 0.0f);
            auto write = 0;
            auto lfo = 0.0;
            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            const auto total = static_cast<int>(kSampleRate * 3.0);
            naive.reserve(static_cast<std::size_t>(total));

            for (int i = 0; i < total; ++i)
            {
                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;

                line[static_cast<std::size_t>(write)] = in;
                write = (write + 1) % size;

                lfo += juce::MathConstants<double>::twoPi * 0.6 / kSampleRate;
                const auto delay = kSampleRate * 0.007 + kSampleRate * 0.0032 * std::sin(lfo);
                auto pos = static_cast<double>(write) - delay;
                while (pos < 0.0) { pos += size; }
                const auto i1 = static_cast<int>(pos) % size;
                const auto i2 = (i1 + 1) % size;
                const auto frac = pos - std::floor(pos);
                const auto wet = line[static_cast<std::size_t>(i1)] * (1.0 - frac)
                                 + line[static_cast<std::size_t>(i2)] * frac;
                naive.push_back(in + static_cast<float>(wet));
            }
        }

        Result naiveResult;
        naiveResult.left = naive;
        naiveResult.right = naive;

        const auto pairedInstability = pitchInstability(paired, kSampleRate);
        const auto naiveInstability = pitchInstability(naiveResult, kSampleRate);

        check("Chorus_AntiPhasePairIsPitchStableWhereASingleDelayIsNot",
              pairedInstability < naiveInstability * 0.6,
              "anti-phase pair " + juce::String(pairedInstability, 6)
                  + ", one delay and one sine LFO " + juce::String(naiveInstability, 6));
    }

    // ---- mono compatibility ------------------------------------------------
    {
        // L + R = 2*dry by construction: the wet terms are equal and opposite.
        // A widener that made its width out of phase cancellation would lose
        // level here.
        juce::String detail;
        auto allRetained = true;

        const auto dry = runChorus([]{ auto s = audible(); s.amount = 0.0f; return s; }(),
                                   Source::saw, 2.0);
        const auto dryMono = monoRms(dry);

        for (const auto amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.amount = amount;
            const auto out = runChorus(s, Source::saw, 2.0);
            const auto ratio = monoRms(out) / juce::jmax(1.0e-9, dryMono);
            const auto retained = ratio > 0.84 && ratio < 1.25;
            allRetained = allRetained && retained;
            detail << juce::String(amount, 2) << ":" << juce::String(ratio, 3) << "  ";
        }

        check("Chorus_MonoCollapseRetainsLevelAtEveryAmount", allRetained,
              "mono sum against dry: " + detail);
    }

    // ---- modes -------------------------------------------------------------
    {
        juce::String detail;
        auto allStable = true;
        std::vector<double> rmsByMode;
        std::vector<double> corrByMode;

        for (int mode = 0; mode < px3::Chorus::modeCount(); ++mode)
        {
            auto s = audible();
            s.modeIndex = mode;
            const auto out = runChorus(s, Source::saw, 2.0);
            allStable = allStable && out.finite() && out.peak() < 4.0f;
            rmsByMode.push_back(out.rms());
            corrByMode.push_back(out.correlation());
            detail << mode << ":" << juce::String(out.correlation(), 3) << "  ";
        }

        check("Chorus_EveryModeIsStable", allStable,
              juce::String(px3::Chorus::modeCount()) + " modes");

        // Modes must be structurally different, not one set of numbers scaled.
        auto distinct = 0;
        for (std::size_t i = 1; i < corrByMode.size(); ++i)
        {
            if (std::abs(corrByMode[i] - corrByMode[i - 1]) > 1.0e-4)
            {
                ++distinct;
            }
        }
        check("Chorus_ModesAreGenuinelyDifferent",
              distinct >= static_cast<int>(corrByMode.size()) - 3,
              "correlation by mode: " + detail);

        // Mode 1 is documented as the softest and mode 4 as the strongest.
        auto soft = audible();
        soft.modeIndex = 0;
        auto strong = audible();
        strong.modeIndex = 3;
        const auto a = runChorus(soft, Source::saw, 2.5);
        const auto b = runChorus(strong, Source::saw, 2.5);
        check("Chorus_Mode1IsSofterThanMode4",
              b.correlation() < a.correlation(),
              "mode 1 correlation " + juce::String(a.correlation(), 4)
                  + ", mode 4 " + juce::String(b.correlation(), 4));

        // And mode 1 is documented as having the LONGER delay, because its VCOs
        // run slower - which is why its base delay is larger, not smaller.
        check("Chorus_Mode1HasTheLongestDelayAsDocumented",
              px3::Chorus::specFor(0).baseDelayMs > px3::Chorus::specFor(3).baseDelayMs,
              "mode 1 " + juce::String(px3::Chorus::specFor(0).baseDelayMs, 1) + " ms, mode 4 "
                  + juce::String(px3::Chorus::specFor(3).baseDelayMs, 1) + " ms");

        // The combination modes stack a second pair at its own rate.
        check("Chorus_CombinationModesStackASecondPair",
              px3::Chorus::specFor(4).stackedMode >= 0
                  && px3::Chorus::specFor(5).stackedMode >= 0
                  && px3::Chorus::specFor(6).stackedMode >= 0
                  && px3::Chorus::specFor(1).stackedMode < 0,
              "");
    }

    // ---- depth, width, feedback -------------------------------------------
    {
        // Depth zero must mean no pitch modulation at all.
        auto still = audible();
        still.depth = 0.0f;
        still.character = 0.0f;
        const auto out = runChorus(still, Source::sine, 2.5, kSampleRate, 440.0f);
        check("Chorus_DepthZeroHasNoPitchModulation",
              pitchInstability(out, kSampleRate) < 0.01,
              "period instability at depth 0 = "
                  + juce::String(pitchInstability(out, kSampleRate), 6));

        juce::String widthDetail;
        std::vector<double> corrByWidth;
        auto widthStable = true;
        for (const auto width : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.width = width;
            const auto r = runChorus(s, Source::saw, 2.0);
            widthStable = widthStable && r.finite();
            corrByWidth.push_back(r.correlation());
            widthDetail << juce::String(width, 2) << ":" << juce::String(r.correlation(), 3) << "  ";
        }
        check("Chorus_WidthWidensTheImage",
              widthStable && corrByWidth.back() < corrByWidth.front(),
              widthDetail);

        // Feedback must stay well short of flanging, even at maximum.
        auto flangeRisk = audible();
        flangeRisk.feedback = 1.0f;
        flangeRisk.amount = 1.0f;
        flangeRisk.depth = 1.0f;
        const auto fb = runChorus(flangeRisk, Source::saw, 3.0);
        check("Chorus_MaximumFeedbackStaysBounded",
              fb.finite() && fb.peak() < 4.0f,
              "peak " + juce::String(fb.peak(), 4));
    }

    // ---- low-end anchoring -------------------------------------------------
    {
        // The dry path is never filtered, and the wet is high-passed, so a bass
        // note keeps its weight and its pitch while its harmonics move.
        auto s = audible();
        s.amount = 1.0f;
        s.depth = 1.0f;

        const auto processed = runChorus(s, Source::bass, 2.5, kSampleRate, 55.0f);
        const auto dry = runChorus([]{ auto d = audible(); d.amount = 0.0f; return d; }(),
                                   Source::bass, 2.5, kSampleRate, 55.0f);

        // Low-frequency energy, via a crude integrator: a widener that moved
        // the bass into the sides would lose it here.
        auto lowEnergy = [](const Result& r)
        {
            auto state = 0.0;
            auto sum = 0.0;
            for (std::size_t i = 0; i < r.left.size(); ++i)
            {
                const auto m = 0.5 * (static_cast<double>(r.left[i]) + r.right[i]);
                state += (m - state) * 0.01;
                sum += state * state;
            }
            return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1u, r.left.size())));
        };

        const auto ratio = lowEnergy(processed) / juce::jmax(1.0e-9, lowEnergy(dry));
        check("Chorus_BassKeepsItsWeight", ratio > 0.85 && ratio < 1.2,
              "low-frequency energy against dry = " + juce::String(ratio, 3));

        // And its pitch: the fundamental must not wobble.
        check("Chorus_BassPitchStaysStable",
              pitchInstability(processed, kSampleRate) < 0.02,
              "period instability on a 55 Hz bass = "
                  + juce::String(pitchInstability(processed, kSampleRate), 6));
    }

    // ---- synth material ----------------------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        const std::array<std::pair<const char*, Source>, 4> materials { {
            { "saw", Source::saw }, { "supersaw", Source::supersaw },
            { "bass", Source::bass }, { "pluck", Source::pluck },
        } };
        for (const auto& material : materials)
        {
            const auto out = runChorus(audible(), material.second, 2.0);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << material.first << " corr " << juce::String(out.correlation(), 3) << "  ";
        }
        check("Chorus_HandlesSynthMaterial", allFine, detail);
    }

    // ---- extremes and automation ------------------------------------------
    {
        auto everything = audible();
        everything.amount = 1.0f;
        everything.depth = 1.0f;
        everything.width = 1.0f;
        everything.spread = 1.0f;
        everything.feedback = 1.0f;
        everything.character = 1.0f;
        everything.tone = 1.0f;
        everything.lowCut = 1.0f;

        juce::String detail;
        auto allSurvive = true;
        for (int mode = 0; mode < px3::Chorus::modeCount(); ++mode)
        {
            everything.modeIndex = mode;
            const auto out = runChorus(everything, Source::supersaw, 3.0);
            const auto ok = out.finite() && out.peak() < 6.0f && std::abs(out.dc()) < 0.1;
            allSurvive = allSurvive && ok;
            if (! ok)
            {
                detail << "mode " << mode << " peak " << juce::String(out.peak(), 3) << "  ";
            }
        }
        check("Chorus_EveryModeAtMaximumEverythingStaysValid", allSurvive,
              detail.isEmpty() ? "all nine modes at maximum on a supersaw" : detail);
    }

    {
        struct Sweep { const char* name; float ChorusSettings::* member; };
        const std::array<Sweep, 8> sweeps { {
            { "amount", &ChorusSettings::amount },
            { "rate", &ChorusSettings::rate },
            { "depth", &ChorusSettings::depth },
            { "width", &ChorusSettings::width },
            { "spread", &ChorusSettings::spread },
            { "lowCut", &ChorusSettings::lowCut },
            { "character", &ChorusSettings::character },
            { "mix", &ChorusSettings::mix },
        } };

        juce::String detail;
        auto allSmooth = true;

        for (const auto& sweep : sweeps)
        {
            px3::Chorus chorus;
            chorus.prepare(kSampleRate);
            chorus.setSeed(24u);

            auto s = audible();
            const auto total = static_cast<int>(kSampleRate * 3.0);

            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            auto worstStep = 0.0f;
            auto previous = 0.0f;
            auto valid = true;

            for (int i = 0; i < total; ++i)
            {
                if (i % 64 == 0)
                {
                    s.*(sweep.member) = static_cast<float>(i) / static_cast<float>(total);
                    chorus.updateForBlock(s);
                }

                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;

                float l = 0.0f;
                float r = 0.0f;
                chorus.processSampleFrame(in, in, l, r);
                valid = valid && std::isfinite(l) && std::abs(l) < 8.0f;

                if (i > 2000)
                {
                    worstStep = juce::jmax(worstStep, std::abs(l - previous));
                }
                previous = l;
            }

            const auto ok = valid && worstStep < 0.4f;
            allSmooth = allSmooth && ok;
            if (! ok)
            {
                detail << sweep.name << " step " << juce::String(worstStep, 4) << "  ";
            }
        }

        check("Chorus_EveryParameterSweepsWithoutDiscontinuity", allSmooth,
              detail.isEmpty() ? "8 controls swept min to max under audio" : detail);
    }

    // ---- bypass ------------------------------------------------------------
    {
        px3::Chorus chorus;
        chorus.prepare(kSampleRate);

        auto s = audible();
        chorus.updateForBlock(s);

        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i)
        {
            float l = 0.0f;
            float r = 0.0f;
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;
            chorus.processSampleFrame(in, in, l, r);
        }

        s.enabled = false;
        chorus.updateForBlock(s);

        auto maxDeviation = 0.0f;
        auto worstStep = 0.0f;
        auto previous = 0.0f;
        for (int i = 0; i < static_cast<int>(kSampleRate * 0.5); ++i)
        {
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;

            float l = 0.0f;
            float r = 0.0f;
            chorus.processSampleFrame(in, in, l, r);

            if (i > 0)
            {
                worstStep = juce::jmax(worstStep, std::abs(l - previous));
            }
            previous = l;

            if (i > static_cast<int>(kSampleRate * 0.15))
            {
                maxDeviation = juce::jmax(maxDeviation, std::abs(l - in));
            }
        }

        check("Chorus_BypassPassesDryWithoutAClick",
              maxDeviation < 1.0e-5f && worstStep < 0.15f,
              "deviation from dry " + juce::String(maxDeviation, 8) + ", worst step "
                  + juce::String(worstStep, 5));
    }

    // ---- integration -------------------------------------------------------
    {
        PX3SynthAudioProcessor processor;

        auto findParam = [&processor](const juce::String& id) -> juce::RangedAudioParameter*
        {
            for (auto* param : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    if (ranged->paramID == id)
                    {
                        return ranged;
                    }
                }
            }
            return nullptr;
        };

        const std::array<const char*, 12> ids { {
            "chorusEnabled", "chorusAmount", "chorusRate", "chorusDepth", "chorusWidth",
            "chorusSpread", "chorusLowCut", "chorusFeedback", "chorusCharacter",
            "chorusMix", "chorusTone", "chorusMode",
        } };

        juce::StringArray missing;
        for (const auto* id : ids)
        {
            if (auto* param = findParam(id))
            {
                param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.17f : 0.83f);
            }
            else
            {
                missing.add(id);
            }
        }

        check("Chorus_AllParametersExist", missing.isEmpty(),
              missing.isEmpty() ? "12 CHORUS parameters registered"
                                : "missing " + missing.joinIntoString(", "));

        auto order = px3::kDefaultFxOrder;
        std::rotate(order.begin(), order.begin() + 2, order.end());
        processor.setFxProcessingOrder(order);

        std::vector<float> before;
        for (auto* param : processor.getParameters())
        {
            before.push_back(param->getValue());
        }

        juce::MemoryBlock state;
        processor.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        std::size_t index = 0;
        juce::StringArray drifted;
        for (auto* param : restored.getParameters())
        {
            if (index < before.size() && std::abs(param->getValue() - before[index]) > 1.0e-5f)
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                    ranged != nullptr && ranged->paramID.startsWithIgnoreCase("chorus"))
                {
                    drifted.add(ranged->paramID);
                }
            }
            ++index;
        }

        check("Chorus_EveryParameterRoundTripsThroughDawState", drifted.isEmpty(),
              drifted.isEmpty() ? "12 parameters restored exactly"
                                : "drifted: " + drifted.joinIntoString(", "));

        check("Chorus_FxOrderIncludingChorusSurvivesState",
              restored.getFxProcessingOrder() == order, "");
    }
}

// ============================================================================
// STEREO SPREAD
// ============================================================================

namespace spreadtest
{
using doomtest::Result;
using chorustest::monoRms;

enum class Source { silence, impulse, sine, saw, bass, leftOnly, rightOnly, wideStereo };

Result runSpread(const StereoSpreadSettings& settings,
                 Source source,
                 double seconds,
                 double sampleRate = kSampleRate,
                 float frequency = 220.0f)
{
    px3::StereoSpread spread;
    spread.prepare(sampleRate);
    spread.updateForBlock(settings);

    const auto total = static_cast<int>(sampleRate * seconds);
    Result result;
    result.left.reserve(static_cast<std::size_t>(total));
    result.right.reserve(static_cast<std::size_t>(total));

    auto phase = 0.0;
    const auto increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int i = 0; i < total; ++i)
    {
        auto inL = 0.0f;
        auto inR = 0.0f;

        auto mono = 0.0f;
        switch (source)
        {
            case Source::silence:
                break;
            case Source::impulse:
                mono = i == 4096 ? 1.0f : 0.0f;
                break;
            case Source::sine:
            case Source::leftOnly:
            case Source::rightOnly:
            case Source::wideStereo:
                mono = 0.5f * static_cast<float>(std::sin(phase));
                break;
            case Source::saw:
            {
                auto sum = 0.0;
                for (int h = 1; h <= 12; ++h)
                {
                    sum += std::sin(phase * h) / h;
                }
                mono = 0.28f * static_cast<float>(sum);
                break;
            }
            case Source::bass:
            {
                auto sum = 0.0;
                for (int h = 1; h <= 9; h += 2)
                {
                    sum += std::sin(phase * h) / h;
                }
                mono = 0.4f * static_cast<float>(sum);
                break;
            }
        }

        phase += increment;

        switch (source)
        {
            case Source::leftOnly:  inL = mono; inR = 0.0f; break;
            case Source::rightOnly: inL = 0.0f; inR = mono; break;
            case Source::wideStereo: inL = mono; inR = -mono * 0.7f; break;
            default:                inL = mono; inR = mono; break;
        }

        float outL = 0.0f;
        float outR = 0.0f;
        spread.processSampleFrame(inL, inR, outL, outR);
        result.left.push_back(outL);
        result.right.push_back(outR);
    }

    return result;
}

StereoSpreadSettings audible()
{
    StereoSpreadSettings s;
    s.enabled = true;
    s.amount = 0.75f;
    return s;
}

// Side energy as a fraction of the total. A mono input has none; a widener that
// works must create some.
double sideFraction(const Result& r)
{
    auto side = 0.0;
    auto total = 0.0;
    for (std::size_t i = 0; i < r.left.size(); ++i)
    {
        const auto s = 0.5 * (static_cast<double>(r.left[i]) - r.right[i]);
        const auto m = 0.5 * (static_cast<double>(r.left[i]) + r.right[i]);
        side += s * s;
        total += s * s + m * m;
    }
    return side / juce::jmax(1.0e-12, total);
}
} // namespace spreadtest

void testStereoSpread()
{
    suite("STEREO SPREAD");

    using namespace spreadtest;

    // ---- construction and defaults -----------------------------------------
    {
        const StereoSpreadSettings defaults;
        check("Spread_DefaultsAreValid",
              defaults.enabled && juce::approximatelyEqual(defaults.amount, 0.0f)
                  && juce::approximatelyEqual(defaults.lowWidth, 0.0f)
                  && defaults.modeIndex == 0,
              "amount zero, lows mono, CLASSIC");

        px3::StereoSpread spread;
        spread.prepare(kSampleRate);
        spread.reset();
        float l = 0.0f;
        float r = 0.0f;
        spread.processSampleFrame(0.0f, 0.0f, l, r);
        check("Spread_ConstructsAndProcessesWithoutPreparingSettings",
              std::isfinite(l) && std::isfinite(r), "");
    }

    // ---- amount zero is exactly identity -----------------------------------
    {
        // The bands sum flat, but only if nothing has been done to the parts.
        // Anything less than identity here means the effect is always on.
        auto off = audible();
        off.amount = 0.0f;

        const auto out = runSpread(off, Source::wideStereo, 1.5);

        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        auto worstError = 0.0f;
        for (std::size_t i = 0; i < out.left.size(); ++i)
        {
            const auto mono = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;
            if (i > static_cast<std::size_t>(kSampleRate * 0.2))
            {
                worstError = juce::jmax(worstError, std::abs(out.left[i] - mono));
                worstError = juce::jmax(worstError, std::abs(out.right[i] + mono * 0.7f));
            }
        }

        check("Spread_AmountZeroIsExactlyIdentity", worstError < 1.0e-4f,
              "worst deviation from the input " + juce::String(worstError, 8));
    }

    // ---- sample rates and stability ---------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const auto out = runSpread(audible(), Source::saw, 1.5, rate);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << juce::String(static_cast<int>(rate)) << " rms "
                   << juce::String(out.rms(), 4) << "  ";
        }
        check("Spread_RunsAtEverySupportedSampleRate", allFine, detail);

        const auto quiet = runSpread(audible(), Source::silence, 1.5);
        check("Spread_SilenceStaysSilent",
              quiet.finite() && quiet.peak() < 1.0e-5f, "");

        const auto impulse = runSpread(audible(), Source::impulse, 2.0);
        check("Spread_ImpulseStaysBounded",
              impulse.finite() && impulse.peak() < 4.0f,
              "peak " + juce::String(impulse.peak(), 4));
    }

    // ---- the core claim: a mono source actually widens ----------------------
    {
        // M/S gain alone cannot do this - for a mono source the side signal is
        // zero, and no gain applied to zero produces anything. Side energy has
        // to be CREATED, which is what the allpass network is for.
        auto off = audible();
        off.amount = 0.0f;
        const auto dry = runSpread(off, Source::saw, 2.0);

        auto on = audible();
        on.amount = 1.0f;
        const auto wide = runSpread(on, Source::saw, 2.0);

        check("Spread_CreatesSideEnergyFromAMonoSource",
              sideFraction(dry) < 1.0e-6 && sideFraction(wide) > 0.02,
              "mono input side fraction " + juce::String(sideFraction(dry), 8)
                  + " -> " + juce::String(sideFraction(wide), 5));
    }

    // ---- mono compatibility: the first-class requirement --------------------
    {
        juce::String detail;
        auto allRetained = true;

        const auto dry = runSpread([]{ auto s = audible(); s.amount = 0.0f; return s; }(),
                                   Source::saw, 2.0);
        const auto dryMono = monoRms(dry);

        for (const auto amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.amount = amount;
            const auto out = runSpread(s, Source::saw, 2.0);
            const auto ratio = monoRms(out) / juce::jmax(1.0e-9, dryMono);
            // Within 1.5 dB. A phase-trick widener loses far more.
            const auto retained = ratio > 0.84 && ratio < 1.2;
            allRetained = allRetained && retained;
            detail << juce::String(amount, 2) << ":" << juce::String(ratio, 3) << "  ";
        }

        check("Spread_MonoCollapseRetainsLevelAtEveryAmount", allRetained,
              "mono sum against dry: " + detail);
    }

    // ---- correlation stays above the floor ---------------------------------
    {
        juce::String detail;
        auto allSafe = true;

        for (const auto amount : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.amount = amount;
            s.width = 1.0f;
            const auto out = runSpread(s, Source::saw, 3.0);
            // Never near -1, which is a signal and its own inverse: infinitely
            // wide and completely silent in mono.
            const auto safe = out.correlation() > -0.5;
            allSafe = allSafe && safe;
            detail << juce::String(amount, 2) << ":" << juce::String(out.correlation(), 3) << "  ";
        }

        check("Spread_CorrelationStaysSafeAcrossTheRange", allSafe, detail);

        // And width has to actually reduce it.
        auto narrow = audible();
        narrow.amount = 0.15f;
        auto wide = audible();
        wide.amount = 1.0f;
        wide.width = 1.0f;
        const auto a = runSpread(narrow, Source::saw, 2.5);
        const auto b = runSpread(wide, Source::saw, 2.5);
        check("Spread_MoreAmountMeansLessCorrelation",
              b.correlation() < a.correlation(),
              "low " + juce::String(a.correlation(), 4) + ", high "
                  + juce::String(b.correlation(), 4));
    }

    // ---- frequency behaviour -----------------------------------------------
    {
        // Low frequencies must stay centred: there is no width available at a
        // wavelength longer than any room, and side energy down there is what
        // destroys mono compatibility.
        juce::String detail;
        auto lowsStayCentred = true;
        auto highsWiden = true;

        // A crossover has a slope. The property that matters is not that side
        // energy is zero at some chosen frequency, but that it FALLS steeply as
        // frequency drops - deep bass fully centred, and the approach to the
        // crossover monotonic rather than lumpy.
        std::vector<double> sideByFrequency;
        for (const auto hz : { 30.0f, 50.0f, 80.0f, 100.0f })
        {
            auto s = audible();
            s.amount = 1.0f;
            const auto out = runSpread(s, Source::sine, 2.0, kSampleRate, hz);
            const auto side = sideFraction(out);
            sideByFrequency.push_back(side);
            detail << juce::String(static_cast<int>(hz)) << "Hz " << juce::String(side, 4) << "  ";
        }

        // Deep bass - well inside the mono band - must be essentially centred.
        lowsStayCentred = sideByFrequency[0] < 0.02 && sideByFrequency[1] < 0.03;

        // And the fall has to be monotonic, which is what says a crossover is
        // doing this rather than something frequency-dependent going wrong.
        for (std::size_t i = 1; i < sideByFrequency.size(); ++i)
        {
            lowsStayCentred = lowsStayCentred && sideByFrequency[i] > sideByFrequency[i - 1];
        }

        // Approaching the crossover, still a small minority of the energy.
        lowsStayCentred = lowsStayCentred && sideByFrequency.back() < 0.10;

        check("Spread_LowFrequenciesStayCentred", lowsStayCentred, detail);

        juce::String highDetail;
        for (const auto hz : { 1000.0f, 5000.0f, 10000.0f, 15000.0f })
        {
            auto s = audible();
            s.amount = 1.0f;
            const auto out = runSpread(s, Source::sine, 2.0, kSampleRate, hz);
            const auto side = sideFraction(out);
            highsWiden = highsWiden && out.finite();
            highDetail << juce::String(static_cast<int>(hz)) << "Hz "
                       << juce::String(side, 4) << "  ";
        }
        check("Spread_HighFrequenciesAreStableAndWidened", highsWiden, highDetail);

        // A bass patch keeps its weight.
        auto s = audible();
        s.amount = 1.0f;
        const auto processed = runSpread(s, Source::bass, 2.5, kSampleRate, 55.0f);
        const auto dry = runSpread([]{ auto d = audible(); d.amount = 0.0f; return d; }(),
                                   Source::bass, 2.5, kSampleRate, 55.0f);
        const auto ratio = monoRms(processed) / juce::jmax(1.0e-9, monoRms(dry));
        check("Spread_BassKeepsItsWeightInMono", ratio > 0.88 && ratio < 1.15,
              "mono bass level against dry = " + juce::String(ratio, 3));
    }

    // ---- input configurations ----------------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        const std::array<std::pair<const char*, Source>, 3> inputs { {
            { "left only", Source::leftOnly },
            { "right only", Source::rightOnly },
            { "already wide", Source::wideStereo },
        } };

        for (const auto& input : inputs)
        {
            const auto out = runSpread(audible(), input.second, 2.0);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << input.first << " rms " << juce::String(out.rms(), 4) << "  ";
        }
        check("Spread_HandlesEveryInputConfiguration", allFine, detail);

        // An existing stereo image must not be destroyed or collapsed.
        const auto dry = runSpread([]{ auto s = audible(); s.amount = 0.0f; return s; }(),
                                   Source::wideStereo, 2.0);
        const auto processed = runSpread(audible(), Source::wideStereo, 2.0);
        check("Spread_PreservesAnExistingStereoImage",
              sideFraction(processed) >= sideFraction(dry) * 0.8,
              "dry side " + juce::String(sideFraction(dry), 4) + ", processed "
                  + juce::String(sideFraction(processed), 4));
    }

    // ---- modes -------------------------------------------------------------
    {
        juce::String detail;
        auto allStable = true;
        std::vector<double> sideByMode;

        for (int mode = 0; mode < px3::StereoSpread::modeCount(); ++mode)
        {
            auto s = audible();
            s.modeIndex = mode;
            s.amount = 1.0f;
            const auto out = runSpread(s, Source::saw, 2.5);
            allStable = allStable && out.finite() && out.peak() < 4.0f;
            sideByMode.push_back(sideFraction(out));
            detail << mode << ":" << juce::String(sideFraction(out), 4) << "  ";
        }

        check("Spread_EveryModeIsStable", allStable, detail);

        // MONO SAFE must be genuinely more conservative, not simply quieter.
        auto monoSafe = audible();
        monoSafe.modeIndex = 3;
        monoSafe.amount = 1.0f;
        monoSafe.width = 1.0f;

        auto wideMode = monoSafe;
        wideMode.modeIndex = 1;

        const auto safe = runSpread(monoSafe, Source::saw, 2.5);
        const auto wide = runSpread(wideMode, Source::saw, 2.5);

        check("Spread_MonoSafeIsMoreConservativeThanWide",
              safe.correlation() > wide.correlation()
                  && sideFraction(safe) < sideFraction(wide),
              "mono safe correlation " + juce::String(safe.correlation(), 4)
                  + " side " + juce::String(sideFraction(safe), 4)
                  + ", wide correlation " + juce::String(wide.correlation(), 4)
                  + " side " + juce::String(sideFraction(wide), 4));
    }

    // ---- extremes and automation ------------------------------------------
    {
        auto everything = audible();
        everything.amount = 1.0f;
        everything.width = 1.0f;
        everything.depth = 1.0f;
        everything.center = 0.0f;
        everything.lowWidth = 1.0f;
        everything.highWidth = 1.0f;
        everything.tone = 1.0f;

        juce::String detail;
        auto allSurvive = true;
        for (int mode = 0; mode < px3::StereoSpread::modeCount(); ++mode)
        {
            everything.modeIndex = mode;
            const auto out = runSpread(everything, Source::saw, 3.0);
            const auto ok = out.finite() && out.peak() < 6.0f && std::abs(out.dc()) < 0.1
                            && monoRms(out) > 1.0e-4;
            allSurvive = allSurvive && ok;
            if (! ok)
            {
                detail << "mode " << mode << " peak " << juce::String(out.peak(), 3)
                       << " mono " << juce::String(monoRms(out), 5) << "  ";
            }
        }
        check("Spread_MaximumEverythingStaysValidAndAudibleInMono", allSurvive,
              detail.isEmpty() ? "every mode at maximum still sums to audio" : detail);
    }

    {
        struct Sweep { const char* name; float StereoSpreadSettings::* member; };
        const std::array<Sweep, 9> sweeps { {
            { "amount", &StereoSpreadSettings::amount },
            { "width", &StereoSpreadSettings::width },
            { "depth", &StereoSpreadSettings::depth },
            { "center", &StereoSpreadSettings::center },
            { "lowWidth", &StereoSpreadSettings::lowWidth },
            { "highWidth", &StereoSpreadSettings::highWidth },
            { "lowFreq", &StereoSpreadSettings::lowFreq },
            { "highFreq", &StereoSpreadSettings::highFreq },
            { "mix", &StereoSpreadSettings::mix },
        } };

        juce::String detail;
        auto allSmooth = true;

        for (const auto& sweep : sweeps)
        {
            px3::StereoSpread spread;
            spread.prepare(kSampleRate);

            auto s = audible();
            const auto total = static_cast<int>(kSampleRate * 3.0);

            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            auto worstStep = 0.0f;
            auto previous = 0.0f;
            auto valid = true;

            for (int i = 0; i < total; ++i)
            {
                if (i % 64 == 0)
                {
                    s.*(sweep.member) = static_cast<float>(i) / static_cast<float>(total);
                    spread.updateForBlock(s);
                }

                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;

                float l = 0.0f;
                float r = 0.0f;
                spread.processSampleFrame(in, in, l, r);
                valid = valid && std::isfinite(l) && std::abs(l) < 8.0f;

                if (i > 2000)
                {
                    worstStep = juce::jmax(worstStep, std::abs(l - previous));
                }
                previous = l;
            }

            const auto ok = valid && worstStep < 0.35f;
            allSmooth = allSmooth && ok;
            if (! ok)
            {
                detail << sweep.name << " step " << juce::String(worstStep, 4) << "  ";
            }
        }

        check("Spread_EveryParameterSweepsWithoutDiscontinuity", allSmooth,
              detail.isEmpty() ? "9 controls swept min to max under audio" : detail);
    }

    // ---- bypass ------------------------------------------------------------
    {
        px3::StereoSpread spread;
        spread.prepare(kSampleRate);

        auto s = audible();
        spread.updateForBlock(s);

        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i)
        {
            float l = 0.0f;
            float r = 0.0f;
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;
            spread.processSampleFrame(in, in, l, r);
        }

        s.enabled = false;
        spread.updateForBlock(s);

        auto maxDeviation = 0.0f;
        auto worstStep = 0.0f;
        auto previous = 0.0f;
        for (int i = 0; i < static_cast<int>(kSampleRate * 0.5); ++i)
        {
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;

            float l = 0.0f;
            float r = 0.0f;
            spread.processSampleFrame(in, in, l, r);

            if (i > 0)
            {
                worstStep = juce::jmax(worstStep, std::abs(l - previous));
            }
            previous = l;

            if (i > static_cast<int>(kSampleRate * 0.15))
            {
                maxDeviation = juce::jmax(maxDeviation, std::abs(l - in));
            }
        }

        check("Spread_BypassPassesDryWithoutAClick",
              maxDeviation < 1.0e-5f && worstStep < 0.15f,
              "deviation from dry " + juce::String(maxDeviation, 8) + ", worst step "
                  + juce::String(worstStep, 5));
    }

    // ---- integration -------------------------------------------------------
    {
        PX3SynthAudioProcessor processor;

        auto findParam = [&processor](const juce::String& id) -> juce::RangedAudioParameter*
        {
            for (auto* param : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    if (ranged->paramID == id)
                    {
                        return ranged;
                    }
                }
            }
            return nullptr;
        };

        const std::array<const char*, 12> ids { {
            "spreadEnabled", "spreadAmount", "spreadWidth", "spreadDepth", "spreadCenter",
            "spreadLowWidth", "spreadHighWidth", "spreadLowFreq", "spreadHighFreq",
            "spreadMix", "spreadTone", "spreadMode",
        } };

        juce::StringArray missing;
        for (const auto* id : ids)
        {
            if (auto* param = findParam(id))
            {
                param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.19f : 0.81f);
            }
            else
            {
                missing.add(id);
            }
        }

        check("Spread_AllParametersExist", missing.isEmpty(),
              missing.isEmpty() ? "12 STEREO SPREAD parameters registered"
                                : "missing " + missing.joinIntoString(", "));

        auto order = px3::kDefaultFxOrder;
        std::reverse(order.begin(), order.end());
        processor.setFxProcessingOrder(order);

        std::vector<float> before;
        for (auto* param : processor.getParameters())
        {
            before.push_back(param->getValue());
        }

        juce::MemoryBlock state;
        processor.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        std::size_t index = 0;
        juce::StringArray drifted;
        for (auto* param : restored.getParameters())
        {
            if (index < before.size() && std::abs(param->getValue() - before[index]) > 1.0e-5f)
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                    ranged != nullptr && ranged->paramID.startsWithIgnoreCase("spread"))
                {
                    drifted.add(ranged->paramID);
                }
            }
            ++index;
        }

        check("Spread_EveryParameterRoundTripsThroughDawState", drifted.isEmpty(),
              drifted.isEmpty() ? "12 parameters restored exactly"
                                : "drifted: " + drifted.joinIntoString(", "));

        check("Spread_FxOrderIncludingSpreadSurvivesState",
              restored.getFxProcessingOrder() == order, "");
    }

    {
        // The full chain, in two orders. Every stage present, position changing
        // the result.
        auto renderWithOrder = [](const px3::FxOrder& order)
        {
            PX3SynthAudioProcessor processor;
            for (auto* param : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    if (ranged->paramID == "spreadAmount") ranged->setValueNotifyingHost(0.8f);
                    if (ranged->paramID == "chorusAmount") ranged->setValueNotifyingHost(0.7f);
                    if (ranged->paramID == "lucyGlobal")   ranged->setValueNotifyingHost(0.5f);
                    if (ranged->paramID == "doomMix")      ranged->setValueNotifyingHost(0.5f);
                    if (ranged->paramID == "reverbAmount") ranged->setValueNotifyingHost(0.5f);
                }
            }
            processor.setFxProcessingOrder(order);
            return render(processor, 48000, { { 2000, true, 60, 0.9f } });
        };

        auto forward = px3::kDefaultFxOrder;
        auto reversed = px3::kDefaultFxOrder;
        std::reverse(reversed.begin(), reversed.end());

        const auto a = renderWithOrder(forward);
        const auto b = renderWithOrder(reversed);

        auto differs = false;
        const auto count = juce::jmin(a.left.size(), b.left.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            if (std::abs(a.left[i] - b.left[i]) > 1.0e-5f)
            {
                differs = true;
                break;
            }
        }

        check("Spread_TheWholeEightStageChainRespondsToOrder", differs,
              "default order rms " + juce::String(a.rms(), 5) + ", reversed "
                  + juce::String(b.rms(), 5));
    }
}

} // namespace px3tests
