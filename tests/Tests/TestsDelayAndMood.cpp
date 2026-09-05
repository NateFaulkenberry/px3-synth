#include "TestSupport.h"

// testDelay, testMood, testEffectIndependence

namespace px3tests
{


void testDelay()
{
    suite("DELAY");

    static const char* names[] = { "Granular", "Tape", "AnalogBBD", "PingPong",
                                   "Stereo", "Modulated", "Diffusion" };

    // At zero amount the delay is a wire. The previous mix law bottomed out at
    // 6% wet, so six of the seven algorithms coloured the signal with the knob
    // all the way down.
    {
        double worst = 0.0;
        int worstAlgo = 0;
        for (int algo = 0; algo < 7; ++algo)
        {
            const auto bleed = delayZeroAmountBleed(algo);
            if (bleed > worst) { worst = bleed; worstAlgo = algo; }
        }
        check("Delay_ZeroAmountIsTransparent",
              worst < 1.0e-6,
              "worst deviation " + fmt(worst, 8) + " (" + names[worstAlgo] + ")");
    }

    // No algorithm may gain energy in its own feedback loop. Tape used to,
    // because its head bump and its hysteresis bias each added gain that the
    // feedback coefficient did not know about.
    {
        double worstRatio = 0.0;
        int worstAlgo = 0;
        bool allFinite = true;
        for (int algo = 0; algo < 7; ++algo)
        {
            DelaySettings s;
            s.amount = 1.0f;
            s.timeControl = 0.35f;
            s.feedbackControl = 1.0f;
            s.algorithmIndex = algo;
            const auto m = measureDelay(s, kSampleRate, static_cast<int>(kSampleRate * 20.0));
            allFinite = allFinite && m.finite;
            const auto ratio = m.tailRmsEarly > 1.0e-9 ? m.tailRmsLate / m.tailRmsEarly : 0.0;
            if (ratio > worstRatio) { worstRatio = ratio; worstAlgo = algo; }
        }
        check("Delay_MaximumFeedbackDoesNotGrow",
              allFinite && worstRatio <= 1.0,
              "worst late/early ratio " + fmt(worstRatio, 4) + " (" + names[worstAlgo] + ")");
    }

    // Tape's wow used to be added to the sample value rather than to the delay
    // length, which put a sub-audio tone into the feedback path: 99.95% of the
    // algorithm's steady-state energy was below 30 Hz.
    //
    // Measured on sustained input rather than on an impulse tail. As a ratio it
    // needs a denominator with something in it: taken from a near-silent window
    // the granular algorithm's grain-spawn randomness alone moved this between
    // 0.06 and 0.31 from run to run.
    {
        double worst = 0.0;
        int worstAlgo = 0;
        for (int algo = 0; algo < 7; ++algo)
        {
            const auto ratio = delayStress(algo, 0, 0.5f).lowFrequencyEnergyRatio;
            if (ratio > worst) { worst = ratio; worstAlgo = algo; }
        }
        check("Delay_NoAlgorithmGeneratesSubsonicContent",
              worst < 0.25,
              "worst sub-30 Hz energy share on sustained input " + fmt(worst, 4)
                  + " (" + names[worstAlgo] + ")");
    }

    // Ping-pong has to alternate channels. The old implementation read the
    // opposite channel at the same position, which from a mono input produced
    // two identical channels and measured +1.000.
    {
        DelaySettings s;
        s.amount = 0.7f;
        s.timeControl = 0.35f;
        s.feedbackControl = 0.6f;
        s.algorithmIndex = 3;
        const auto corr = measureDelay(s).interChannelCorrelation;
        check("Delay_PingPongAlternatesChannels",
              corr < 0.80,
              "inter-channel correlation " + fmt(corr, 4));
    }

    // Diffusion was four extra taps summed together - a multitap, not a
    // diffuser - and both channels ran identical chains, so it measured +0.950.
    {
        DelaySettings s;
        s.amount = 0.7f;
        s.timeControl = 0.35f;
        s.feedbackControl = 0.6f;
        s.algorithmIndex = 6;
        const auto corr = measureDelay(s).interChannelCorrelation;
        check("Delay_DiffusionDecorrelatesChannels",
              corr < 0.80,
              "inter-channel correlation " + fmt(corr, 4));
    }

    // The TIME knob must move the delay in one direction. BBD is exempt above
    // its ceiling: a bucket-brigade chip runs out of stages, and holding the
    // time there rather than pretending otherwise is the point of the model.
    {
        bool allMonotonic = true;
        juce::String detail;
        for (int algo = 1; algo < 7; ++algo)
        {
            double previous = -1.0;
            bool monotonic = true;
            for (const auto t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                DelaySettings s;
                s.amount = 0.7f;
                s.timeControl = t;
                s.feedbackControl = 0.3f;
                s.algorithmIndex = algo;
                const auto ms = measureDelay(s).firstEchoMs;
                if (ms + 1.0e-6 < previous) monotonic = false;
                previous = ms;
            }
            if (!monotonic)
            {
                allMonotonic = false;
                detail += juce::String(names[algo]) + " ";
            }
        }
        check("Delay_TimeControlIsMonotonic",
              allMonotonic,
              allMonotonic ? "all algorithms" : ("non-monotonic: " + detail));
    }

    // Echo times are in seconds, not samples. Every filter in the delay was
    // written with bare one-pole constants before, which moved an octave
    // whenever the host changed rate.
    {
        double worstSpreadPercent = 0.0;
        int worstAlgo = 0;
        for (int algo = 1; algo < 7; ++algo)
        {
            double lowest = 1.0e9, highest = 0.0;
            for (const auto sr : { 44100.0, 48000.0, 96000.0 })
            {
                DelaySettings s;
                s.amount = 0.7f;
                s.timeControl = 0.35f;
                s.feedbackControl = 0.5f;
                s.algorithmIndex = algo;
                const auto ms = measureDelay(s, sr).firstEchoMs;
                lowest = juce::jmin(lowest, ms);
                highest = juce::jmax(highest, ms);
            }
            const auto spread = lowest > 1.0 ? (highest - lowest) / lowest * 100.0 : 0.0;
            if (spread > worstSpreadPercent) { worstSpreadPercent = spread; worstAlgo = algo; }
        }
        check("Delay_EchoTimeIsSampleRateIndependent",
              worstSpreadPercent < 2.0,
              "worst spread across 44.1/48/96 kHz " + fmt(worstSpreadPercent, 3) + "% ("
                  + names[worstAlgo] + ")");
    }

    // A quarter note at 120 BPM is 500 ms, whatever the TIME knob says.
    {
        bool allCorrect = true;
        juce::String detail;
        for (int algo = 1; algo < 7; ++algo)
        {
            DelaySettings s;
            s.amount = 0.7f;
            s.timeControl = 0.05f;          // deliberately not the synced value
            s.feedbackControl = 0.4f;
            s.algorithmIndex = algo;
            s.syncDivisionIndex = 3;        // 1/4
            s.bpm = 120.0;
            const auto ms = measureDelay(s).firstEchoMs;
            // Stereo runs its left line at two thirds, so it is checked against
            // that rather than against the raw division.
            const auto expected = algo == 4 ? 500.0 * 2.0 / 3.0 : 500.0;
            if (std::abs(ms - expected) > expected * 0.06)
            {
                allCorrect = false;
                detail += juce::String(names[algo]) + " " + fmt(ms, 1) + "ms ";
            }
        }
        check("Delay_TempoSyncIsHonoured",
              allCorrect,
              allCorrect ? "1/4 at 120 BPM lands at 500 ms on every algorithm"
                         : ("wrong: " + detail));
    }

    // The characteristic BBD behaviour: stage count is fixed, so the clock -
    // and with it the bandwidth - is set by the delay time. A long setting has
    // to be audibly darker than a short one. The old implementation used one
    // fixed one-pole and had none of this.
    {
        // Measured as absolute energy above 4 kHz rather than as a share of the
        // total. The dry path carries the same white noise at the same gain in
        // both cases, so it cancels out of a comparison of absolute energies -
        // whereas as a fraction it swamps the wet path entirely and the measure
        // reports no difference even when the filter is doing its job.
        auto highFrequencyEnergy = [](float timeControl)
        {
            Delay delay;
            delay.prepare(kSampleRate);
            delay.reset();
            DelaySettings s;
            s.enabled = true;
            s.amount = 1.0f;
            s.timeControl = timeControl;
            s.feedbackControl = 0.3f;
            s.algorithmIndex = 2;
            delay.updateForBlock(s);

            // White noise in, so every band is equally represented going in and
            // whatever comes out reflects the chip's bandwidth.
            juce::Random random(0x51DE51DEu);
            std::vector<float> out;
            const auto total = static_cast<int>(kSampleRate * 4.0);
            out.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                const auto n = random.nextFloat() * 0.6f - 0.3f;
                float l = 0.0f, r = 0.0f;
                delay.processSampleFrame(n, n, l, r);
                out.push_back(l);
            }

            const auto coeff = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 4000.0f
                                               / static_cast<float>(kSampleRate));
            float lp = 0.0f;
            double high = 0.0;
            const auto from = static_cast<int>(kSampleRate * 2.0);
            for (int i = from; i < total; ++i)
            {
                const auto v = out[static_cast<std::size_t>(i)];
                lp += coeff * (v - lp);
                const auto hp = v - lp;
                high += static_cast<double>(hp) * hp;
            }
            return std::sqrt(high / juce::jmax(1, total - from));
        };

        const auto shortSetting = highFrequencyEnergy(0.05f);
        const auto longSetting = highFrequencyEnergy(0.9f);
        check("Delay_BbdBandwidthNarrowsWithDelayTime",
              longSetting < shortSetting * 0.90,
              "RMS above 4 kHz: short " + fmt(shortSetting, 5)
                  + ", long " + fmt(longSetting, 5));
    }

    {
        // The DELAY amount knob is a wet/dry MIX. It used to double as a
        // character control: TAPE scaled its wow and flutter depth by
        // "0.4 + 0.6 * amount" and MODULATED scaled its modulation depth by
        // "0.0009 + 0.0042 * amount", so the modulation sidebands grew deepest
        // exactly where the wet signal was loudest. Measured on a 5 kHz tone as
        // everything that is not the tone or a harmonic of it, TAPE reached
        // -18.6 dB against the tone at full amount and MODULATED -14.0 dB -
        // audible grit, and the reason a delay-heavy patch sounded rough.
        auto nonHarmonicDb = [](int algorithm, float amount)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "ampSustain", 1.0f);
            setParam(processor, "ampRelease", 0.05f);

            // The delay is on the FX BUS and makePlainPatch zeroes every send.
            // Without this the measurement sees dry signal only - which it did
            // at first, reporting an identical -46.4 dB for every algorithm at
            // every amount because the delay was never in the path.
            for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                setParam(processor, juce::String("mix.") + id + ".fxSend", 1.0f);
            setParam(processor, "fxSendGain", 1.0f);
            setParam(processor, "fxReturnGain", 1.0f);

            setParam(processor, "delayEnabled", 1.0f);
            setChoice(processor, "delayAlgorithm", algorithm);
            setParam(processor, "delayTime", 0.34f);
            setParam(processor, "delayFeedback", 0.32f);
            setParam(processor, "delayAmount", amount);

            const auto cap = render(processor, 200000, { { 2000, true, 111, 0.9f } });

            constexpr int order = 15;
            const auto size = 1 << order;
            juce::dsp::FFT fft(order);
            std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
            for (int i = 0; i < size; ++i)
            {
                const auto w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                                      * static_cast<float>(i) / static_cast<float>(size - 1));
                data[static_cast<std::size_t>(i)] = cap.left[static_cast<std::size_t>(120000 + i)] * w;
            }
            fft.performFrequencyOnlyForwardTransform(data.data());

            const auto binsPerHz = static_cast<double>(size) / kSampleRate;
            double tone = 0.0, junk = 0.0;
            for (int b = 1; b < size / 2; ++b)
            {
                const auto hz = static_cast<double>(b) / binsPerHz;
                auto harmonic = hz < 60.0;
                for (int h = 1; h <= 8 && ! harmonic; ++h)
                {
                    harmonic = std::abs(hz - 4978.0 * h) < 60.0;
                }
                const auto e = static_cast<double>(data[(std::size_t) b]) * data[(std::size_t) b];
                (harmonic ? tone : junk) += e;
            }
            return juce::Decibels::gainToDecibels(std::sqrt(junk / juce::jmax(1.0e-12, tone)), -120.0);
        };

        // 5 = MODULATED. Its depth swing was the largest and its sidebands sit
        // far enough from the tone to be unambiguous, so it is the clean case
        // for this measure.
        const auto modulated = nonHarmonicDb(5, 1.0f);

        check("Delay_TurningTheMixUpDoesNotDeepenTheModulation", modulated < -28.0,
              "MODULATED non-harmonic content at full amount: " + fmt(modulated, 1) + " dB");

        // ...and the character must still be there at the mix the presets use,
        // otherwise this "fix" would just be turning the effect off.
        const auto tapeAtPresetMix = nonHarmonicDb(1, 0.28f);
        check("Delay_TapeKeepsItsCharacterAtTheMixThePresetsUse",
              tapeAtPresetMix > -42.0,
              "TAPE at amount 0.28: " + fmt(tapeAtPresetMix, 1) + " dB of non-harmonic content");
    }

    {
        // The tape head bump is a band-pass blended in as
        // "(wet + bump * bumpGain) / (1 + bumpGain)". The band-pass is
        // normalised to unity peak gain, so that expression is exactly unity AT
        // 92 Hz and 1/(1 + bumpGain) everywhere else - not a boost at the bump
        // frequency but a broadband CUT everywhere else, applied on every pass
        // through the feedback loop.
        //
        // At the old gain that was up to 2 dB per repeat, so after a few
        // repeats nothing but the bump was left. Measured on an FM bell, which
        // has no low end of its own, the delay tail was almost entirely 98 Hz:
        // the head bump ringing rather than the instrument.
        auto tailLowShareDb = [](float amount)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 11);          // FM bell, no low end
            setParam(processor, "osc1MacroA", 0.52f);
            setParam(processor, "osc1MacroB", 0.22f);
            setParam(processor, "ampDecay", 0.55f);
            setParam(processor, "ampSustain", 0.05f);
            setParam(processor, "ampRelease", 0.70f);
            for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                setParam(processor, juce::String("mix.") + id + ".fxSend", 1.0f);
            setParam(processor, "fxSendGain", 1.0f);
            setParam(processor, "fxReturnGain", 1.0f);
            setParam(processor, "delayEnabled", 1.0f);
            setChoice(processor, "delayAlgorithm", 1);     // TAPE
            setParam(processor, "delayTime", 0.34f);
            setParam(processor, "delayFeedback", 0.32f);
            setParam(processor, "delayAmount", amount);

            std::vector<NoteEvent> notes;
            for (int n = 0; n < 8; ++n)
            {
                notes.push_back({ 2000 + n * 12000, true, 72 + (n % 3) * 4, 0.9f });
                notes.push_back({ 2000 + n * 12000 + 6000, false, 72 + (n % 3) * 4, 0.0f });
            }
            const auto cap = render(processor, 200000, notes);

            constexpr int order = 15;
            const auto size = 1 << order;
            juce::dsp::FFT fft(order);
            std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
            for (int i = 0; i < size; ++i)
            {
                const auto w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                                      * static_cast<float>(i) / static_cast<float>(size - 1));
                data[static_cast<std::size_t>(i)] = cap.left[static_cast<std::size_t>(120000 + i)] * w;
            }
            fft.performFrequencyOnlyForwardTransform(data.data());

            const auto binsPerHz = static_cast<double>(size) / kSampleRate;
            double low = 0.0, all = 0.0;
            for (int b = 1; b < size / 2; ++b)
            {
                const auto hz = static_cast<double>(b) / binsPerHz;
                const auto e = static_cast<double>(data[(std::size_t) b]) * data[(std::size_t) b];
                all += e;
                if (hz >= 60.0 && hz <= 140.0) low += e;
            }
            return juce::Decibels::gainToDecibels(std::sqrt(low / juce::jmax(1.0e-12, all)), -120.0);
        };

        const auto share = tailLowShareDb(1.0f);

        // The head-bump band must not be the whole tail. It was -0.3 dB of the
        // total - i.e. essentially all of it.
        check("Delay_TapeTailIsNotJustTheHeadBumpResonance", share < -1.5,
              "60-140 Hz share of the tape tail at full amount: " + fmt(share, 1) + " dB of the total");
    }

    {
        // TAPE and MODULATED read the delay line by SLIDING the read pointer
        // rather than crossfading between two taps - that slide is what gives
        // them their pitch glide. The read position is writePos - samples, so
        // its velocity is 1 - d(samples)/dn: change the length by more than one
        // sample per sample and the read pointer stops moving forward, overtakes
        // the write head, and reads samples that have not been written yet.
        //
        // The free time range is 0.015 to 2 seconds, about 86,000 samples, and
        // the control smoothing has an 8 ms time constant - so an end-to-end
        // change asked the pointer to move roughly 224 samples per sample.
        // Snapping the time end to end produced 4 clicks on TAPE and 3 on
        // MODULATED, the worst 35.7 times the local slope. The five algorithms
        // that crossfade were unaffected.
        //
        // Measured at 44.1 kHz with 512-sample blocks. It did NOT show at
        // 128-sample blocks, which is why several earlier passes over this same
        // delay found nothing - the artifact needs a big enough block that the
        // whole slew lands inside one of them.
        juce::StringArray clicking;
        juce::String detail;

        for (int algo = 0; algo < 7; ++algo)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 11);
            setParam(processor, "ampSustain", 0.35f);
            setParam(processor, "ampRelease", 0.70f);
            for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                setParam(processor, juce::String("mix.") + id + ".fxSend", 1.0f);
            setParam(processor, "fxSendGain", 1.0f);
            setParam(processor, "fxReturnGain", 1.0f);
            setParam(processor, "delayEnabled", 1.0f);
            setChoice(processor, "delayAlgorithm", algo);
            setParam(processor, "delayAmount", 1.0f);
            setParam(processor, "delayFeedback", 0.45f);
            setParam(processor, "delayTime", 0.9f);

            constexpr double rate = 44100.0;
            constexpr int block = 512;
            processor.setPlayConfigDetails(0, 2, rate, block);
            processor.prepareToPlay(rate, block);
            juce::AudioBuffer<float> buffer(2, block);
            std::vector<float> left, right;

            for (int b = 0; b < 700; ++b)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                if (b % 60 == 4) midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.9f), 0);
                if (b == 200) setParam(processor, "delayTime", 0.05f);
                if (b == 380) setParam(processor, "delayTime", 0.95f);
                if (b == 560) setParam(processor, "delayTime", 0.10f);
                processor.processBlock(buffer, midi);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    // BOTH channels: TAPE modulates the right differently, so a
                    // fault on the right alone would be invisible.
                    left.push_back(buffer.getSample(0, i));
                    right.push_back(buffer.getSample(1, i));
                }
            }

            double energy = 0.0;
            for (const auto v : left) energy += static_cast<double>(v) * v;
            const auto rms = std::sqrt(
                energy / static_cast<double>(std::max<std::size_t>(1, left.size())));

            constexpr int window = 64;
            auto worst = 0.0;
            auto clicks = 0;
            for (int pass = 0; pass < 2; ++pass)
            {
                const auto& chan = pass == 0 ? left : right;
                for (int i = window; i + window < static_cast<int>(chan.size()); ++i)
                {
                    const auto jump = std::abs(static_cast<double>(chan[(std::size_t) i])
                                               - chan[(std::size_t)(i - 1)]);
                    // A click is large in absolute terms as well as against the
                    // local slope; a decaying tail has a vanishing slope and
                    // makes anything look enormous.
                    if (jump < rms * 0.4) continue;

                    double sum = 0.0;
                    int count = 0;
                    for (int k = i - window; k < i + window; ++k)
                    {
                        if (k == i || k == i - 1) continue;
                        const auto d = static_cast<double>(chan[(std::size_t) k]) - chan[(std::size_t)(k - 1)];
                        sum += d * d;
                        ++count;
                    }
                    const auto reference = std::sqrt(sum / juce::jmax(1, count));
                    if (reference < 1.0e-6) continue;

                    const auto ratio = jump / reference;
                    if (ratio > 6.0) ++clicks;
                    worst = juce::jmax(worst, ratio);
                }
            }

            detail << names[algo] << " " << fmt(worst, 1) << "x  ";
            if (clicks > 0) clicking.add(juce::String(names[algo]) + " (" + juce::String(clicks) + ")");
        }

        check("Delay_SnappingTheTimeDoesNotClick", clicking.isEmpty(),
              clicking.isEmpty() ? "worst jump against the local slope: " + detail
                                 : "clicking: " + clicking.joinIntoString(", "));
    }

    // Nothing may keep ringing after the input stops. This is the fault the
    // static tests could not see: three separate mechanisms only misbehaved
    // while a control was moving, or only on sustained rather than impulsive
    // input. Checked at 25 s, by which point the longest decay the FEEDBACK
    // control can ask for is 75 dB down.
    {
        bool allDecay = true;
        bool allFinite = true;
        juce::String detail;
        double worstRatio = 0.0;
        for (int algo = 0; algo < 7; ++algo)
        {
            for (int sweep = 0; sweep < 5; ++sweep)
            {
                const auto r = delayStress(algo, sweep);
                allFinite = allFinite && r.finite;
                const auto ratio = r.tailAt1s > 1.0e-9 ? r.tailAt25s / r.tailAt1s : 0.0;
                if (ratio > worstRatio) worstRatio = ratio;
                if (ratio > 0.02)
                {
                    allDecay = false;
                    detail += juce::String(names[algo]) + "/sweep" + juce::String(sweep)
                            + " " + fmt(ratio, 4) + " ";
                }
            }
        }
        check("Delay_NothingSustainsAfterTheInputStops",
              allDecay && allFinite,
              allDecay ? ("worst tail at 25 s vs 1 s = " + fmt(worstRatio, 5)
                          + " across all algorithms and control sweeps")
                       : ("still ringing: " + detail));
    }

    // FEEDBACK asks for a decay time, so the same knob position has to mean
    // roughly the same decay whether the delay is short or long. As a raw
    // per-repeat coefficient it did not: 0.98 is a 30 s decay at 100 ms and an
    // 11 minute one at 2 s, which is what "stuck repeating forever" was.
    {
        bool consistent = true;
        juce::String detail;
        double shortest = 1.0e9, longest = 0.0;
        for (const auto timeControl : { 0.15f, 0.5f, 1.0f })
        {
            Delay delay;
            delay.prepare(kSampleRate);
            delay.reset();
            DelaySettings s;
            s.enabled = true;
            s.amount = 0.9f;
            s.timeControl = timeControl;
            s.feedbackControl = 1.0f;
            s.algorithmIndex = 3;
            delay.updateForBlock(s);

            // Impulse in, then measure how long it takes to fall 20 dB.
            const auto total = static_cast<int>(kSampleRate * 40.0);
            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                float l = 0.0f, r = 0.0f;
                delay.processSampleFrame(i < 64 ? 0.9f : 0.0f, i < 64 ? 0.9f : 0.0f, l, r);
                out.push_back(l);
            }

            auto windowRms = [&](int from)
            {
                const auto to = juce::jmin(total, from + static_cast<int>(kSampleRate * 1.0));
                if (to <= from) return 0.0;
                double e = 0.0;
                for (int k = from; k < to; ++k) e += static_cast<double>(out[static_cast<std::size_t>(k)]) * out[static_cast<std::size_t>(k)];
                return std::sqrt(e / (to - from));
            };
            const auto reference = windowRms(static_cast<int>(kSampleRate * 2.0));
            double fellAt = 40.0;
            for (double t = 3.0; t < 39.0; t += 1.0)
            {
                if (windowRms(static_cast<int>(kSampleRate * t)) < reference * 0.1)
                {
                    fellAt = t;
                    break;
                }
            }
            shortest = juce::jmin(shortest, fellAt);
            longest = juce::jmax(longest, fellAt);
            detail += fmt(timeControl, 2) + "->" + fmt(fellAt, 0) + "s ";
        }
        // Within a factor of four across a delay range that spans 30x.
        consistent = longest <= shortest * 4.0 + 2.0;
        check("Delay_FeedbackMeansTheSameDecayAtEveryDelayTime",
              consistent,
              "time to fall 20 dB at maximum feedback: " + detail);
    }

    // Bypassing has to empty the lines. Otherwise switching the delay back on
    // replays whatever was in flight when it was switched off, underneath
    // whatever is playing now.
    {
        double worst = 0.0;
        int worstAlgo = 0;
        for (int algo = 0; algo < 7; ++algo)
        {
            const auto tail = delayTailAfterBypassCycle(algo);
            if (tail > worst) { worst = tail; worstAlgo = algo; }
        }
        check("Delay_BypassClearsTheTail",
              worst < 1.0e-4,
              "loudest sample after bypass and re-enable with silence in: "
                  + fmt(worst, 8) + " (" + names[worstAlgo] + ")");
    }

    // Through the whole plugin, at the extremes, on every algorithm.
    {
        bool allGood = true;
        juce::String detail;
        for (int algo = 0; algo < 7; ++algo)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "delayEnabled", 1.0f);
            setChoice(processor, "delayAlgorithm", algo);
            setParam(processor, "delayAmount", 1.0f);
            setParam(processor, "delayFeedback", 1.0f);
            setParam(processor, "delayTime", 0.3f);
            const auto capture = render(processor, 192000,
                                        { { 2000, true, 45, 0.9f }, { 60000, false, 45, 0.0f } });
            if (!capture.isFinite() || capture.peak() > 1.0)
            {
                allGood = false;
                detail += juce::String(names[algo]) + " peak " + fmt(capture.peak(), 4) + " ";
            }
        }
        check("Delay_AllAlgorithmsStayFiniteAndWithinCeilingInThePlugin",
              allGood,
              allGood ? "all seven algorithms at maximum amount and feedback" : detail);
    }

    // The same, but with the controls moving throughout - which is when the
    // crossfade and companding faults showed themselves.
    {
        bool allGood = true;
        juce::String detail;
        for (int algo = 0; algo < 7; ++algo)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "delayEnabled", 1.0f);
            setChoice(processor, "delayAlgorithm", algo);
            setParam(processor, "delayAmount", 0.9f);
            setParam(processor, "delayFeedback", 0.95f);
            const auto totalBlocks = 192000 / kBlockSize;
            const auto capture = render(processor, 192000,
                                        { { 2000, true, 45, 0.9f }, { 90000, false, 45, 0.0f } },
                                        [&](int block)
                                        {
                                            const auto t = static_cast<float>(block)
                                                         / static_cast<float>(juce::jmax(1, totalBlocks));
                                            setParam(processor, "delayTime", t);
                                        });
            if (!capture.isFinite() || capture.peak() > 1.0)
            {
                allGood = false;
                detail += juce::String(names[algo]) + " peak " + fmt(capture.peak(), 4) + " ";
            }
        }
        check("Delay_ControlSweepsStayWithinCeilingInThePlugin",
              allGood,
              allGood ? "TIME swept end to end on all seven algorithms" : detail);
    }
}

void testMood()
{
    suite("MOOD");

    // No read pointer may wrap without a crossfade. A looping playhead that
    // simply jumps from the end of its loop back to the start steps the signal,
    // and that step is a click once per loop, slice or window - which is what
    // the component actually sounded like: TAPE ticked 17 times in four
    // seconds, SLIP eight, and ENV's detector froze the output on a single
    // sample for the length of each hold.
    //
    // Measured against what the programme itself can do: a 220 Hz sine at 0.5
    // moves at most 0.0144 per sample, so a jump far above that did not come
    // from the signal.
    {
        auto worstStep = [](int loopMode, int wetMode, float routing)
        {
            Mood mood;
            mood.prepare(kSampleRate);
            mood.reset();
            MoodSettings s;
            s.enabled = true;
            s.mix = 1.0f;
            s.loopModeIndex = loopMode;
            s.wetModeIndex = wetMode;
            s.routing = routing;
            s.clock = 1.0f;      // full rate, so stepping here is not the clock
            s.degrade = 0.0f;    // and not the lo-fi control either
            s.spread = 0.5f;
            s.feedback = 0.4f;
            s.loopLength = 0.35f;
            s.loopModify = 0.62f;
            mood.updateForBlock(s);

            const auto total = static_cast<int>(kSampleRate * 6.0);
            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                const auto in = std::sin(juce::MathConstants<float>::twoPi * 220.0f
                                         * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.5f;
                float l = 0.0f, r = 0.0f;
                mood.processSampleFrame(in, in, l, r);
                out.push_back(l);
            }

            double worst = 0.0;
            for (int i = static_cast<int>(kSampleRate * 2.0) + 1; i < total; ++i)
            {
                worst = juce::jmax(worst, std::abs(static_cast<double>(
                    out[static_cast<std::size_t>(i)] - out[static_cast<std::size_t>(i - 1)])));
            }
            return worst;
        };

        double worst = 0.0;
        juce::String detail;
        bool clean = true;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                const auto step = worstStep(loopMode, wetMode, 0.5f);
                worst = juce::jmax(worst, step);
                if (step > 0.05)
                {
                    clean = false;
                    detail += "loop" + juce::String(loopMode) + "/wet" + juce::String(wetMode)
                            + " " + fmt(step, 4) + " ";
                }
            }
        }
        check("Mood_NoReadPointerWrapsWithoutACrossfade",
              clean,
              clean ? ("worst sample-to-sample step across all nine pairings " + fmt(worst, 5))
                    : ("discontinuities: " + detail));
    }

    // Every mode pairing with FEEDBACK, SPREAD and DEGRADE all at maximum. The
    // widening controls raise the peak if they are not level-compensated - a
    // mid/side widener that is not compensated pushed this to 1.14 - and a
    // stereo effect that clips when it is turned up is not usable at the top of
    // its range.
    {
        bool allGood = true;
        juce::String detail;
        double worstPeak = 0.0;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.loopModeIndex = loopMode;
                s.wetModeIndex = wetMode;
                s.spread = 1.0f;
                s.feedback = 1.0f;
                s.degrade = 1.0f;
                s.routing = 1.0f;
                s.wetModify = 1.0f;
                const auto m = measureMood(s);
                worstPeak = juce::jmax(worstPeak, m.peak);
                if (!m.finite || m.peak > 1.0)
                {
                    allGood = false;
                    detail += "loop" + juce::String(loopMode) + "/wet" + juce::String(wetMode)
                            + " peak " + fmt(m.peak, 4) + " ";
                }
            }
        }
        check("Mood_MaximumSettingsStayFiniteAndWithinCeiling",
              allGood,
              allGood ? ("worst peak with feedback, spread and degrade at maximum "
                         + fmt(worstPeak, 4))
                      : detail);
    }

    // ROUTING has to do what its labels say. The parameter is exposed as a
    // three-way choice - DRY->WET, LOOP->WET, PARALLEL - and reaches the DSP as
    // index/2. The middle and top settings used to be swapped relative to their
    // labels: "LOOP->WET" fed the wet channel the input as well, and "PARALLEL"
    // fed it the loop alone.
    //
    // Distinguished by feeding a signal and comparing against a silent-input
    // render: on LOOP->WET the wet channel never sees the input directly, so
    // the immediate response to a transient is much smaller than on the two
    // settings that pass the input through.
    {
        auto immediacy = [](float routing)
        {
            Mood mood;
            mood.prepare(kSampleRate);
            mood.reset();
            MoodSettings ms;
            ms.enabled = true;
            ms.mix = 1.0f;
            ms.routing = routing;
            ms.loopModeIndex = 1;      // TAPE
            ms.wetModeIndex = 1;       // DELAY
            ms.wetTime = 0.0f;         // 30 ms, so the input's own echo lands inside the window
            ms.wetModify = 0.0f;
            ms.loopLength = 0.9f;
            ms.feedback = 0.0f;
            ms.spread = 0.0f;
            ms.degrade = 0.0f;
            mood.updateForBlock(ms);

            // A short burst, measured over the window immediately after it, so
            // only signal that reached the output promptly is counted. The loop
            // is still empty this early, which is exactly what separates a
            // routing that passes the input from one that does not.
            double energy = 0.0;
            const auto burst = static_cast<int>(kSampleRate * 0.05);
            const auto window = static_cast<int>(kSampleRate * 0.30);
            for (int i = 0; i < burst + window; ++i)
            {
                const auto in = i < burst
                    ? std::sin(juce::MathConstants<float>::twoPi * 440.0f
                               * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.6f
                    : 0.0f;
                float l = 0.0f, r = 0.0f;
                mood.processSampleFrame(in, in, l, r);
                if (i >= burst) energy += static_cast<double>(l) * l;
            }
            return std::sqrt(energy / window);
        };

        const auto dryToWet = immediacy(0.0f);    // index 0
        const auto loopToWet = immediacy(0.5f);   // index 1
        const auto parallel = immediacy(1.0f);    // index 2
        check("Mood_RoutingMatchesItsLabels",
              loopToWet < dryToWet * 0.5 && parallel > loopToWet,
              "prompt response: DRY->WET " + fmt(dryToWet, 6)
                  + ", LOOP->WET " + fmt(loopToWet, 6)
                  + ", PARALLEL " + fmt(parallel, 6));
    }

    // SPREAD has to make a stereo image out of every mode pairing, not just the
    // ones that happen to pan. Before the rewrite six of the nine combinations
    // measured a side-to-mid ratio of exactly 0.0000 - the output was mono -
    // and the control NARROWED what stereo there was, because all it did was
    // mix each channel into the other.
    //
    // Averaged over renders: the granular and gated modes use the system
    // Random, which cannot be seeded, so a single render moves by a third
    // either way.
    {
        auto meanSideToMid = [](int loopMode, int wetMode, float spread)
        {
            double total = 0.0;
            constexpr int renders = 4;
            for (int i = 0; i < renders; ++i)
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.loopModeIndex = loopMode;
                s.wetModeIndex = wetMode;
                s.spread = spread;
                s.routing = 1.0f;
                s.feedback = 0.4f;
                total += measureMood(s).sideToMidRatio;
            }
            return total / renders;
        };

        bool allWiden = true;
        juce::String detail;
        double worstGain = 1.0e9;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                const auto narrow = meanSideToMid(loopMode, wetMode, 0.0f);
                const auto wide = meanSideToMid(loopMode, wetMode, 1.0f);
                const auto gain = wide / juce::jmax(1.0e-4, narrow);
                worstGain = juce::jmin(worstGain, gain);
                // The absolute floor is low because ENV is duty-cycled by
                // design: it holds the incoming image until its detector fires
                // and only pans while it is open, so its time-averaged width is
                // legitimately a fraction of what the always-on modes reach.
                // It still clears this by more than an order of magnitude
                // (0.0015 -> 0.065, a 34x gain), and a mode that had gone mono
                // would measure around 0.001. The ratio is the real assertion.
                if (wide < 0.03 || gain < 1.5)
                {
                    allWiden = false;
                    detail += "loop" + juce::String(loopMode) + "/wet" + juce::String(wetMode)
                            + " " + fmt(narrow, 4) + "->" + fmt(wide, 4) + " ";
                }
            }
        }
        check("Mood_SpreadWidensEveryModeCombination",
              allWiden,
              allWiden ? ("side-to-mid rises on all nine pairings; smallest gain x"
                          + fmt(worstGain, 2))
                       : ("did not widen: " + detail));
    }

    // With SPREAD down the incoming image must survive. A source hard to one
    // side has to come out on that side, which is what makes the control a
    // choice rather than a permanent stereoiser.
    {
        bool allPreserve = true;
        juce::String detail;
        double worstDb = 1.0e9;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.loopModeIndex = loopMode;
                s.wetModeIndex = wetMode;
                s.spread = 0.0f;
                s.routing = 1.0f;
                const auto sep = measureMood(s, true).channelSeparationDb;
                worstDb = juce::jmin(worstDb, sep);
                if (sep < 20.0)
                {
                    allPreserve = false;
                    detail += "loop" + juce::String(loopMode) + "/wet" + juce::String(wetMode)
                            + " " + fmt(sep, 1) + "dB ";
                }
            }
        }
        check("Mood_SpreadOffPreservesTheIncomingImage",
              allPreserve,
              allPreserve ? ("worst channel separation with a hard-left input "
                             + fmt(worstDb, 1) + " dB")
                          : ("image collapsed: " + detail));
    }

    // TAPE's stereo treatment is specific: with SPREAD up the right channel
    // plays the loop forward and the left plays the same loop in reverse. Two
    // copies of the same material running opposite ways are uncorrelated, so
    // this is checkable rather than merely describable.
    {
        MoodSettings s;
        s.mix = 1.0f;
        s.loopModeIndex = 1;      // TAPE
        s.wetModeIndex = 1;
        s.spread = 1.0f;
        s.routing = 1.0f;
        s.feedback = 0.4f;
        const auto corr = measureMood(s).interChannelCorrelation;
        check("Mood_TapeSpreadPlaysTheLoopForwardAndReverse",
              std::abs(corr) < 0.35,
              "inter-channel correlation at full spread " + fmt(corr, 4));
    }

    // CLOCK is the engine's sample rate. Audio captured at one rate and played
    // back at another changes speed and pitch together, so turning the clock
    // down transposes a captured loop - and it must do so in the harmonised
    // steps the control is specified with, over a full three octaves.
    //
    // Measured against a FROZEN loop. While the looper is still recording,
    // capture and playback happen at the same rate and the pitch is preserved
    // by definition, so a render that holds the clock constant shows nothing
    // however well the control works.
    {
        auto playbackHz = [](float clock)
        {
            Mood mood;
            mood.prepare(kSampleRate);
            mood.reset();
            MoodSettings ms;
            ms.enabled = true;
            ms.mix = 1.0f;
            ms.loopModeIndex = 1;
            ms.loopModify = 0.70f;
            ms.loopLength = 0.5f;
            ms.wetModeIndex = 1;
            ms.wetModify = 0.0f;
            ms.wetTime = 0.0f;
            ms.routing = 1.0f;
            ms.spread = 0.0f;
            ms.degrade = 0.0f;
            ms.feedback = 0.0f;
            ms.clock = 1.0f;
            mood.updateForBlock(ms);

            for (int i = 0; i < static_cast<int>(kSampleRate * 4.0); ++i)
            {
                const auto in = std::sin(juce::MathConstants<float>::twoPi * 440.0f
                                         * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.6f;
                float l = 0.0f, r = 0.0f;
                mood.processSampleFrame(in, in, l, r);
            }

            ms.clock = clock;
            ms.freeze = true;
            mood.updateForBlock(ms);

            const auto total = static_cast<int>(kSampleRate * 6.0);
            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                float l = 0.0f, r = 0.0f;
                mood.processSampleFrame(0.0f, 0.0f, l, r);
                out.push_back(l);
            }
            return estimateFrequency(out, static_cast<int>(kSampleRate * 3.0),
                                     static_cast<int>(kSampleRate * 2.0), 20.0, 2000.0);
        };

        const auto full = playbackHz(1.0f);
        const auto threeQuarter = playbackHz(0.75f);
        const auto half = playbackHz(0.5f);
        const auto quarter = playbackHz(0.25f);
        const auto lowest = playbackHz(0.0f);

        const auto monotonic = full > threeQuarter && threeQuarter > half
                            && half > quarter && quarter > lowest;
        // Three octaves across the knob, i.e. a factor of eight.
        const auto span = full / juce::jmax(1.0, lowest);
        check("Mood_ClockTransposesACapturedLoop",
              monotonic && span > 6.0 && span < 10.0,
              "playback pitch " + fmt(full, 0) + " / " + fmt(threeQuarter, 0) + " / "
                  + fmt(half, 0) + " / " + fmt(quarter, 0) + " / " + fmt(lowest, 0)
                  + " Hz, span x" + fmt(span, 2));
    }

    // Same requirement as the other two effects: bypass empties the history
    // and wet buffers so re-enabling does not replay an old loop.
    {
        double worst = 0.0;
        int worstCombo = 0;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                MoodSettings settings;
                settings.mix = 1.0f;
                settings.loopModeIndex = loopMode;
                settings.wetModeIndex = wetMode;
                settings.feedback = 0.8f;
                settings.routing = 0.5f;
                const auto tail = tailAfterBypassCycle<Mood, MoodSettings>(
                    settings,
                    [](Mood& m, const MoodSettings& s) { m.updateForBlock(s); });
                if (tail > worst) { worst = tail; worstCombo = loopMode * 3 + wetMode; }
            }
        }
        check("Mood_BypassClearsTheTail",
              worst < 1.0e-4,
              "loudest sample after bypass and re-enable with silence in: "
                  + fmt(worst, 8) + " (loop " + juce::String(worstCombo / 3)
                  + ", wet " + juce::String(worstCombo % 3) + ")");
    }

    auto runMood = [](const MoodSettings& settings, int inputSamples, int totalSamples)
    {
        ::Mood mood;
        mood.prepare(kSampleRate);
        mood.reset();
        mood.updateForBlock(settings);

        Capture capture;
        double phase = 0.0;
        for (int i = 0; i < totalSamples; ++i)
        {
            // A tone rather than an impulse: granular processing needs material
            // in its history buffer to work with.
            const auto input = i < inputSamples
                                   ? static_cast<float>(0.5 * std::sin(phase))
                                   : 0.0f;
            phase += juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            float outL = 0.0f, outR = 0.0f;
            mood.processSampleFrame(input, input, outL, outR);
            capture.left.push_back(outL);
            capture.right.push_back(outR);
        }
        return capture;
    };

    MoodSettings base;
    base.enabled = true;
    base.mix = 0.8f;

    {
        const auto capture = runMood(base, 24000, 72000);
        check("Mood_ProducesOutputFromInput", capture.rms() > 1.0e-5 && capture.isFinite(),
              "rms " + fmt(capture.rms(), 6));
        check("Mood_IsStableAndDoesNotRunAway", capture.peak() < 4.0,
              "peak " + fmt(capture.peak(), 5));
        check("Mood_HasNoSignificantDcOffset", std::abs(capture.dcOffset()) < 0.01,
              "dc " + fmt(capture.dcOffset(), 7));
    }

    // `enabled` is the one and only bypass. A second control, moodTrueBypass,
    // used to be exposed to the host and saved into every preset while Mood
    // never read it; it was removed rather than implemented. This guards
    // against it being reintroduced as an inert control.
    {
        PX3SynthAudioProcessor processor;
        bool found = false;
        for (auto* parameter : processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            {
                if (ranged->paramID.equalsIgnoreCase("moodTrueBypass")) found = true;
            }
        }
        check("Mood_NoOrphanedTrueBypassParameterIsExposed", ! found,
              "the host must not be offered a bypass that does nothing");
    }

    // Freeze must stop new material entering the history buffer.
    {
        auto frozen = base;
        frozen.freeze = true;
        auto flowing = base;
        flowing.freeze = false;
        const auto frozenCapture = runMood(frozen, 24000, 60000);
        const auto flowingCapture = runMood(flowing, 24000, 60000);
        bool differs = false;
        for (std::size_t i = 0; i < frozenCapture.left.size(); ++i)
        {
            if (std::abs(frozenCapture.left[i] - flowingCapture.left[i]) > 1.0e-6f) differs = true;
        }
        check("Mood_FreezeChangesWhatIsPlayedBack", differs,
              "frozen rms " + fmt(frozenCapture.rms(), 6)
                  + ", flowing rms " + fmt(flowingCapture.rms(), 6));
    }

    // Disabled must not process.
    {
        auto settings = base;
        settings.enabled = false;
        const auto capture = runMood(settings, 4800, 24000);
        check("Mood_DisabledProducesNoWetTail", capture.rmsOver(9600, 24000) < 1.0e-6,
              "tail rms while disabled " + fmt(capture.rmsOver(9600, 24000), 9));
    }

    // Reset must clear the history so old material cannot leak into new audio.
    {
        ::Mood mood;
        mood.prepare(kSampleRate);
        mood.reset();
        mood.updateForBlock(base);
        double phase = 0.0;
        for (int i = 0; i < 24000; ++i)
        {
            float outL = 0.0f, outR = 0.0f;
            mood.processSampleFrame(static_cast<float>(0.5 * std::sin(phase)),
                                    static_cast<float>(0.5 * std::sin(phase)), outL, outR);
            phase += juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        }
        mood.reset();
        double residual = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            float outL = 0.0f, outR = 0.0f;
            mood.processSampleFrame(0.0f, 0.0f, outL, outR);
            residual = juce::jmax(residual, static_cast<double>(std::abs(outL)));
        }
        check("Mood_ResetClearsPreviousMaterial", residual < 1.0e-6,
              "largest sample after reset with silent input " + fmt(residual, 9));
    }

    // Every continuous parameter must measurably change the output.
    {
        auto captureFor = [&runMood, base](const std::function<void(MoodSettings&)>& tweak)
        {
            auto settings = base;
            tweak(settings);
            return runMood(settings, 24000, 60000);
        };

        // As with the reverb, the difference SIGNAL between the two settings is
        // the sensitive measure; two RMS scalars can match while the waveform
        // is completely different.
        auto relativeDifference = [](const Capture& a, const Capture& b)
        {
            double difference = 0.0, reference = 0.0;
            for (std::size_t i = 4800; i < 57600; ++i)
            {
                const auto d = static_cast<double>(a.left[i]) - b.left[i];
                difference += d * d;
                reference += static_cast<double>(a.left[i]) * a.left[i];
            }
            return reference > 1.0e-18 ? std::sqrt(difference / reference) : 0.0;
        };

        struct ParamCase { const char* name; std::function<void(MoodSettings&)> low, high; };
        const ParamCase cases[] = {
            { "Mix",        [](MoodSettings& s) { s.mix = 0.0f; },        [](MoodSettings& s) { s.mix = 1.0f; } },
            { "Clock",      [](MoodSettings& s) { s.clock = 0.0f; },      [](MoodSettings& s) { s.clock = 1.0f; } },
            { "WetTime",    [](MoodSettings& s) { s.wetTime = 0.0f; },    [](MoodSettings& s) { s.wetTime = 1.0f; } },
            { "WetModify",  [](MoodSettings& s) { s.wetModify = 0.0f; },  [](MoodSettings& s) { s.wetModify = 1.0f; } },
            { "LoopLength", [](MoodSettings& s) { s.loopLength = 0.0f; }, [](MoodSettings& s) { s.loopLength = 1.0f; } },
            { "LoopModify", [](MoodSettings& s) { s.loopModify = 0.0f; }, [](MoodSettings& s) { s.loopModify = 1.0f; } },
            { "Feedback",   [](MoodSettings& s) { s.feedback = 0.0f; },   [](MoodSettings& s) { s.feedback = 1.0f; } },
            { "Degrade",    [](MoodSettings& s) { s.degrade = 0.0f; },    [](MoodSettings& s) { s.degrade = 1.0f; } },
        };

        for (const auto& parameter : cases)
        {
            const auto low = captureFor(parameter.low);
            const auto high = captureFor(parameter.high);
            const auto difference = relativeDifference(low, high);
            check((juce::String("Mood_") + parameter.name + "_ChangesTheOutput").toRawUTF8(),
                  difference > 0.02,
                  "output differs by " + fmt(100.0 * difference, 2) + "% between min and max");
        }
    }

    // SPREAD places grains across the stereo field, so it is a stereo control:
    // measured on one channel it is invisible. Measured as the divergence
    // between channels, at spread 0 every grain is centred and the two channels
    // must match closely.
    {
        auto stereoDivergence = [&runMood, base](float spread)
        {
            auto settings = base;
            settings.spread = spread;
            // STRETCH is the loop mode that spawns grains continuously, and
            // grains are the only thing spread acts on.
            settings.loopModeIndex = 2;
            const auto capture = runMood(settings, 24000, 60000);
            double difference = 0.0, reference = 0.0;
            for (std::size_t i = 4800; i < 57600; ++i)
            {
                const auto d = static_cast<double>(capture.left[i]) - capture.right[i];
                difference += d * d;
                reference += static_cast<double>(capture.left[i]) * capture.left[i];
            }
            return reference > 1.0e-18 ? std::sqrt(difference / reference) : 0.0;
        };
        // Averaged over several renders. Grain pan is drawn from the shared
        // system Random, which JUCE will not let anything reseed, so a single
        // render of the wide setting varies enough to make a single-shot
        // comparison flaky - it was measured at both 0.029 and 0.016 for the
        // same settings. The centred setting has no such variance because every
        // grain is placed at dead centre, so the mean of a handful of runs is a
        // stable basis for the comparison.
        constexpr int kRepeats = 5;
        double centred = 0.0, wide = 0.0;
        for (int repeat = 0; repeat < kRepeats; ++repeat)
        {
            centred += stereoDivergence(0.0f);
            wide += stereoDivergence(1.0f);
        }
        centred /= kRepeats;
        wide /= kRepeats;
        check("Mood_Spread_WidensTheStereoField", wide > centred * 1.8,
              "mean channel divergence over " + juce::String(kRepeats) + " renders: spread 0 "
                  + fmt(centred, 5) + ", spread 1 " + fmt(wide, 5));
    }

    // Discrete mode selectors must all render stably.
    {
        for (int wetMode = 0; wetMode < 3; ++wetMode)
        {
            auto settings = base;
            settings.wetModeIndex = wetMode;
            const auto capture = runMood(settings, 24000, 60000);
            check((juce::String("Mood_WetMode") + juce::String(wetMode) + "_IsStable").toRawUTF8(),
                  capture.isFinite() && capture.peak() < 4.0,
                  "rms " + fmt(capture.rms(), 6) + ", peak " + fmt(capture.peak(), 5));
        }
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            auto settings = base;
            settings.loopModeIndex = loopMode;
            const auto capture = runMood(settings, 24000, 60000);
            check((juce::String("Mood_LoopMode") + juce::String(loopMode) + "_IsStable").toRawUTF8(),
                  capture.isFinite() && capture.peak() < 4.0,
                  "rms " + fmt(capture.rms(), 6) + ", peak " + fmt(capture.peak(), 5));
        }
    }

    // Maximum feedback is where an unstable loop would show as runaway gain.
    {
        auto settings = base;
        settings.feedback = 1.0f;
        settings.mix = 1.0f;
        const auto capture = runMood(settings, 24000, 192000);
        const auto early = capture.rmsOver(24000, 48000);
        const auto late = capture.rmsOver(160000, 190000);
        check("Mood_MaximumFeedbackDoesNotRunAway",
              capture.isFinite() && capture.peak() < 4.0 && late <= early * 4.0,
              "early rms " + fmt(early, 6) + ", late rms " + fmt(late, 6)
                  + ", peak " + fmt(capture.peak(), 5));
    }

    // Transient and silence-to-signal handling.
    {
        ::Mood mood;
        mood.prepare(kSampleRate);
        mood.reset();
        mood.updateForBlock(base);
        Capture capture;
        for (int i = 0; i < 48000; ++i)
        {
            // Silence, then a hard transient, then silence again.
            const auto input = (i >= 12000 && i < 12100) ? 0.9f : 0.0f;
            float outL = 0.0f, outR = 0.0f;
            mood.processSampleFrame(input, input, outL, outR);
            capture.left.push_back(outL);
            capture.right.push_back(outR);
        }
        check("Mood_HandlesSilenceToTransientWithoutInstability",
              capture.isFinite() && capture.peak() < 4.0,
              "peak " + fmt(capture.peak(), 5));
    }
}

//==============================================================================
// PHASE 11 / 12 - EFFECT INDEPENDENCE AND THE FX SIGNAL PATH
//==============================================================================
void testEffectIndependence()
{
    suite("FX INDEPENDENCE AND SIGNAL PATH");

    // Each effect is measured alone; changing another effect's parameters must
    // leave it where it was. Compared against the measured self-variation, as
    // elsewhere, because voice start phase is not reproducible.
    auto renderWithFx = [](const std::function<void(PX3SynthAudioProcessor&)>& configure)
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
        {
            setParam(processor, juce::String("mix.") + id + ".fxSend", 0.8f);
        }
        setParam(processor, "fxSendGain", 1.0f);
        setParam(processor, "fxReturnGain", 0.8f);
        configure(processor);
        return render(processor, 64000, { { 2000, true, 45, 0.9f }, { 30000, false, 45, 0.0f } });
    };

    auto independence = [&renderWithFx](const char* name,
                                        const std::function<void(PX3SynthAudioProcessor&)>& subject,
                                        const std::function<void(PX3SynthAudioProcessor&)>& other)
    {
        const auto controlA = renderWithFx(subject);
        const auto controlB = renderWithFx(subject);
        auto combined = [&subject, &other](PX3SynthAudioProcessor& p) { subject(p); other(p); };
        const auto withOther = renderWithFx(combined);

        const auto selfVariation = std::abs(controlA.rms() - controlB.rms());
        const auto delta = std::abs(controlA.rms() - withOther.rms());
        check(name, delta <= juce::jmax(selfVariation * 3.0, controlA.rms() * 0.02),
              "rms moved " + fmt(delta, 6) + " (self-variation " + fmt(selfVariation, 6) + ")");
    };

    const auto reverbOnly = [](PX3SynthAudioProcessor& p)
    {
        setParam(p, "reverbEnabled", 1.0f);
        setParam(p, "reverbAmount", 0.7f);
        setParam(p, "delayEnabled", 0.0f);
        setParam(p, "moodEnabled", 0.0f);
    };
    const auto moodOnly = [](PX3SynthAudioProcessor& p)
    {
        setParam(p, "moodEnabled", 1.0f);
        setParam(p, "moodMix", 0.7f);
        setParam(p, "reverbEnabled", 0.0f);
        setParam(p, "delayEnabled", 0.0f);
    };

    // Changing a disabled effect's parameters must not reach the enabled one.
    independence("Reverb_UnaffectedByMoodParameterChanges", reverbOnly,
                 [](PX3SynthAudioProcessor& p)
                 {
                     setParam(p, "moodMix", 1.0f);
                     setParam(p, "moodFeedback", 0.9f);
                     setParam(p, "moodDegrade", 0.8f);
                 });
    independence("Mood_UnaffectedByReverbParameterChanges", moodOnly,
                 [](PX3SynthAudioProcessor& p)
                 {
                     setParam(p, "reverbSize", 1.0f);
                     setParam(p, "reverbDecay", 1.0f);
                     setParam(p, "reverbDamping", 0.0f);
                 });
    independence("Reverb_UnaffectedByVibeParameterChanges", reverbOnly,
                 [](PX3SynthAudioProcessor& p)
                 {
                     // Vibe is a voice-stage component; with vibeEnabled off its
                     // amount must not reach the FX bus at all.
                     setParam(p, "vibeEnabled", 0.0f);
                     setParam(p, "vibeAmount", 1.0f);
                 });

    // FX send / return topology. A source panned hard left must place its DRY
    // signal left without steering its send, and the FX return pan must not
    // move the dry signal.
    {
        // Wet-only is reached by pulling the channel FADER to zero, not by
        // muting: the mixer contract is that the send is pre-fader but a mute
        // kills the send as well, so a muted channel contributes nothing at all.
        auto renderPanned = [](float sourcePan, float fxReturnPan, bool wetOnly, bool fxActive)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "mix.osc1.pan", sourcePan);
            setParam(processor, "mix.osc1.level", wetOnly ? 0.0f : 0.8f);
            setParam(processor, "mix.osc1.fxSend", fxActive ? 1.0f : 0.0f);
            setParam(processor, "mix.fx.pan", fxReturnPan);
            setParam(processor, "fxSendGain", 1.0f);
            setParam(processor, "fxReturnGain", fxActive ? 1.0f : 0.0f);
            setParam(processor, "reverbEnabled", fxActive ? 1.0f : 0.0f);
            setParam(processor, "reverbAmount", 1.0f);
            return render(processor, 48000, { { 2000, true, 45, 0.9f } });
        };

        auto balance = [](const Capture& capture)
        {
            double left = 0.0, right = 0.0;
            for (std::size_t i = 12000; i < capture.left.size(); ++i)
            {
                left += std::abs(static_cast<double>(capture.left[i]));
                right += std::abs(static_cast<double>(capture.right[i]));
            }
            const auto total = left + right;
            return total > 1.0e-12 ? (left - right) / total : 0.0;
        };

        // Dry pan, measured with the FX path shut off so the wet cannot dilute
        // the reading.
        const auto dryLeft = balance(renderPanned(-1.0f, 0.0f, false, false));
        const auto dryRight = balance(renderPanned(1.0f, 0.0f, false, false));
        const auto dryCentre = balance(renderPanned(0.0f, 0.0f, false, false));
        check("MixerPan_HardLeftSourcePlacesDrySignalLeft", dryLeft > 0.9,
              "left/right balance " + fmt(dryLeft, 4));
        check("MixerPan_HardRightSourcePlacesDrySignalRight", dryRight < -0.9,
              "left/right balance " + fmt(dryRight, 4));
        check("MixerPan_CentreSourceIsBalanced", std::abs(dryCentre) < 0.02,
              "left/right balance " + fmt(dryCentre, 4));

        // Listening to the FX return alone. The send is centred by design, so
        // panning the source must not move the return: the test is that the
        // balance does not FOLLOW the pan. An absolute-balance test would
        // instead be measuring the reverb algorithm's own stereo pattern, which
        // is deliberately asymmetric and is not what this is about.
        const auto wetWithLeftSource = balance(renderPanned(-1.0f, 0.0f, true, true));
        const auto wetWithRightSource = balance(renderPanned(1.0f, 0.0f, true, true));
        check("FxSend_IsNotSteeredBySourceDryPan",
              std::abs(wetWithLeftSource - wetWithRightSource) < 0.10,
              "FX-return-only balance: source hard left " + fmt(wetWithLeftSource, 4)
                  + ", hard right " + fmt(wetWithRightSource, 4));

        // The FX return has its own pan.
        const auto returnLeft = balance(renderPanned(0.0f, -1.0f, true, true));
        const auto returnRight = balance(renderPanned(0.0f, 1.0f, true, true));
        check("FxReturnPan_MovesTheReturnSignal", returnLeft > 0.5 && returnRight < -0.5,
              "FX-return-only balance: pan left " + fmt(returnLeft, 4)
                  + ", pan right " + fmt(returnRight, 4));

        // With no FX return in the output at all, moving the return pan must
        // leave the dry signal exactly where it was.
        const auto dryWithReturnPanned = balance(renderPanned(-0.5f, 1.0f, false, false));
        const auto dryWithReturnCentred = balance(renderPanned(-0.5f, 0.0f, false, false));
        check("FxReturnPan_DoesNotMoveTheDrySignal",
              nearly(dryWithReturnPanned, dryWithReturnCentred, 0.01),
              "dry balance with return centred " + fmt(dryWithReturnCentred, 4)
                  + ", with return hard right " + fmt(dryWithReturnPanned, 4));
    }

    // Dry-only and wet-only paths must both be reachable.
    {
        PX3SynthAudioProcessor dryOnly, wetOnly;
        for (auto* p : { &dryOnly, &wetOnly })
        {
            makePlainPatch(*p);
            setChoice(*p, "osc1Mode", 1);
            setParam(*p, "reverbEnabled", 1.0f);
            setParam(*p, "reverbAmount", 1.0f);
            setParam(*p, "fxSendGain", 1.0f);
        }
        setParam(dryOnly, "mix.osc1.fxSend", 0.0f);
        setParam(dryOnly, "fxReturnGain", 0.0f);
        setParam(wetOnly, "mix.osc1.fxSend", 1.0f);
        setParam(wetOnly, "fxReturnGain", 1.0f);
        // Fader down, not muted: the send is pre-fader, so this is wet-only.
        setParam(wetOnly, "mix.osc1.level", 0.0f);

        const auto dry = render(dryOnly, 48000, { { 2000, true, 45, 0.9f } });
        const auto wet = render(wetOnly, 48000, { { 2000, true, 45, 0.9f } });
        check("FxPath_DryOnlyProducesAudio", dry.rms() > 0.01, "rms " + fmt(dry.rms(), 5));
        check("FxPath_WetOnlyProducesAudio", wet.rms() > 1.0e-4, "rms " + fmt(wet.rms(), 6));

        // Muting a channel must kill its send as well as its dry path, which is
        // the established mixer contract and the reason wet-only uses the fader.
        PX3SynthAudioProcessor muted;
        makePlainPatch(muted);
        setChoice(muted, "osc1Mode", 1);
        setParam(muted, "reverbEnabled", 1.0f);
        setParam(muted, "reverbAmount", 1.0f);
        setParam(muted, "fxSendGain", 1.0f);
        setParam(muted, "mix.osc1.fxSend", 1.0f);
        setParam(muted, "fxReturnGain", 1.0f);
        setParam(muted, "mix.osc1.mute", 1.0f);
        const auto mutedCapture = render(muted, 48000, { { 2000, true, 45, 0.9f } });
        check("MixerMute_KillsTheSendAsWellAsTheDryPath", mutedCapture.peak() < 1.0e-5,
              "peak with the channel muted and send at maximum " + fmt(mutedCapture.peak(), 8));
    }

    // An FX tail must survive the note that produced it.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "ampRelease", 0.020f);
        setParam(processor, "mix.osc1.fxSend", 1.0f);
        setParam(processor, "fxSendGain", 1.0f);
        setParam(processor, "fxReturnGain", 1.0f);
        setParam(processor, "reverbEnabled", 1.0f);
        setParam(processor, "reverbAmount", 1.0f);
        setParam(processor, "reverbDecay", 0.9f);
        setParam(processor, "reverbSize", 0.9f);
        const auto capture = render(processor, 96000,
                                    { { 2000, true, 45, 0.9f }, { 20000, false, 45, 0.0f } });
        check("FxPath_ReverbTailOutlivesTheNote", capture.rmsOver(30000, 60000) > 1.0e-5,
              "rms well after note-off " + fmt(capture.rmsOver(30000, 60000), 7));
    }

    // EVERY STAGE OF THE CHAIN HAS TO REACH THE OUTPUT.
    //
    // The eight stages are dispatched from one switch in processBlock, the FX
    // panel addresses them by the same ids, and nothing else asserts that a
    // given stage is still wired to audio. A stage dropped from that switch, or
    // an amount parameter that stopped being read, would leave the whole suite
    // green: the per-effect tests all drive their DSP classes directly, and the
    // routing tests above only ever use the reverb.
    //
    // So: for each stage, render the same patch twice - once with only that
    // stage engaged, once with the whole chain off - and require the two to
    // differ. The measure is the RMS of the difference against the RMS of the
    // dry render, which is a proportion rather than a level and so does not
    // move when gain structure is retuned.
    {
        struct Stage
        {
            const char* name;
            std::vector<std::pair<const char*, float>> settings;
        };

        // Each stage's own mix or amount control, at a setting well clear of
        // transparent. VIBE is the odd one: it runs in the voice stage rather
        // than on the bus, so it is audible with the sends down - it is checked
        // here anyway because it is stage 0 of the same chain order.
        const std::vector<Stage> stages {
            { "Vibe",         { { "vibeEnabled", 1.0f }, { "vibeAmount", 1.0f } } },
            { "Delay",        { { "delayEnabled", 1.0f }, { "delayAmount", 1.0f },
                                { "delayTime", 0.35f }, { "delayFeedback", 0.40f } } },
            { "Reverb",       { { "reverbEnabled", 1.0f }, { "reverbAmount", 1.0f },
                                { "reverbSize", 0.8f }, { "reverbDecay", 0.8f } } },
            { "Mood",         { { "moodEnabled", 1.0f }, { "moodMix", 1.0f } } },
            { "Doom",         { { "doomEnabled", 1.0f }, { "doomMix", 1.0f } } },
            { "Lucy",         { { "lucyEnabled", 1.0f }, { "lucyGlobal", 1.0f } } },
            { "Chorus",       { { "chorusEnabled", 1.0f }, { "chorusAmount", 1.0f },
                                { "chorusMix", 1.0f } } },
            { "StereoSpread", { { "spreadEnabled", 1.0f }, { "spreadAmount", 1.0f },
                                { "spreadWidth", 1.0f }, { "spreadMix", 1.0f } } }
        };

        // makePlainPatch silences VIBE, DELAY, REVERB and MOOD but leaves the
        // four newer stages enabled, so the baseline turns all eight off by
        // hand. Their amounts default to zero, so this is belt and braces - but
        // a future default change must not quietly poison the comparison.
        auto renderStage = [&stages](int engaged) -> Capture
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "ampSustain", 1.0f);

            for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                setParam(processor, juce::String("mix.") + id + ".fxSend", 1.0f);
            setParam(processor, "fxSendGain", 1.0f);
            setParam(processor, "fxReturnGain", 1.0f);

            for (const auto* id : { "vibeEnabled", "delayEnabled", "reverbEnabled",
                                    "moodEnabled", "doomEnabled", "lucyEnabled",
                                    "chorusEnabled", "spreadEnabled" })
                setParam(processor, id, 0.0f);

            if (engaged >= 0)
            {
                for (const auto& [id, value] : stages[static_cast<std::size_t>(engaged)].settings)
                    setParam(processor, id, value);
            }

            return render(processor, 96000, { { 2000, true, 45, 0.9f } });
        };

        const auto dry = renderStage(-1);
        const auto dryRms = dry.rms();

        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            const auto wet = renderStage(static_cast<int>(i));

            const auto count = juce::jmin(dry.left.size(), wet.left.size());
            double sum = 0.0;
            for (std::size_t n = 0; n < count; ++n)
            {
                const auto dl = static_cast<double>(wet.left[n] - dry.left[n]);
                const auto dr = static_cast<double>(wet.right[n] - dry.right[n]);
                sum += dl * dl + dr * dr;
            }
            const auto difference = count > 0 ? std::sqrt(sum / static_cast<double>(count * 2)) : 0.0;
            const auto proportion = dryRms > 1.0e-9 ? difference / dryRms : 0.0;

            // 1% of the dry level. Every stage measured far above this - the
            // quietest was well into double figures of a percent - so the bar
            // is set to catch a stage that does nothing at all, not to pin how
            // strong any one effect is.
            const auto name = juce::String("FxBus_") + stages[i].name + "IsAudibleInTheChain";
            check(name.toRawUTF8(),
                  proportion > 0.01,
                  juce::String("difference against dry ") + fmt(proportion * 100.0, 2) + "%");
        }
    }
}

} // namespace px3tests
