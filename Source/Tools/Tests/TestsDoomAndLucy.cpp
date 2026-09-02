#include "TestSupport.h"

// testDoom, testLucy

namespace px3tests
{


// ============================================================================
// DOOM
// ============================================================================


void testDoom()
{
    suite("DOOM");

    using namespace doomtest;

    // ---- construction and defaults -----------------------------------------
    {
        const DoomSettings defaults;
        check("Doom_DefaultsAreValid",
              defaults.enabled
                  && juce::approximatelyEqual(defaults.clock, 1.0f)
                  && defaults.loopModeIndex == 1 && defaults.wetModeIndex == 0
                  && juce::approximatelyEqual(defaults.cross, 0.0f)
                  && ! defaults.loopActive && defaults.wetActive,
              "cross off by default, looper listening, wet channel on");

        px3::Doom doom;
        doom.prepare(kSampleRate);
        doom.reset();
        float l = 0.0f;
        float r = 0.0f;
        doom.processSampleFrame(0.0f, 0.0f, l, r);
        check("Doom_ConstructsAndProcessesWithoutPreparingSettings",
              std::isfinite(l) && std::isfinite(r), "");
    }

    // ---- the clock ---------------------------------------------------------
    {
        // Harmonised steps: the whole point of the control is that each step is
        // a musical interval, so the ratios must be simple.
        juce::StringArray ratios;
        auto allMusical = true;
        for (int i = 0; i < px3::Doom::clockStepCount(); ++i)
        {
            const auto c = static_cast<float>(i) / static_cast<float>(px3::Doom::clockStepCount() - 1);
            const auto ratio = px3::Doom::clockRatioFor(c, false);
            ratios.add(juce::String(ratio, 4));

            // Simple = p/q with q <= 16 and p <= 3.
            auto simple = false;
            for (int q = 1; q <= 16 && ! simple; ++q)
            {
                for (int p = 1; p <= 3; ++p)
                {
                    if (std::abs(ratio - static_cast<float>(p) / static_cast<float>(q)) < 1.0e-5f)
                    {
                        simple = true;
                        break;
                    }
                }
            }
            allMusical = allMusical && simple;
        }
        check("Doom_ClockStepsAreSimpleRatios", allMusical, ratios.joinIntoString(" "));

        check("Doom_ClockIsMonotonicAndBounded",
              juce::approximatelyEqual(px3::Doom::clockRatioFor(1.0f, false), 1.0f)
                  && px3::Doom::clockRatioFor(0.0f, false) < 0.1f
                  && px3::Doom::clockRatioFor(0.5f, false) > px3::Doom::clockRatioFor(0.2f, false),
              "");

        // Halving the clock has to halve the loop speed, which means halving the
        // pitch. Measured rather than asserted: this is the one relationship the
        // whole control rests on.
        check("Doom_SmoothClockIsContinuousAndMatchesTheStepRange",
              juce::approximatelyEqual(px3::Doom::clockRatioFor(1.0f, true), 1.0f)
                  && std::abs(px3::Doom::clockRatioFor(0.5f, true) - px3::Doom::clockRatioFor(0.5f, false)) < 0.2f
                  && px3::Doom::clockRatioFor(0.31f, true) != px3::Doom::clockRatioFor(0.32f, true),
              "smooth sweeps where the stepped version holds");

        // Every clock position has to be stable, not just the named ones.
        juce::String detail;
        auto allStable = true;
        for (int i = 0; i <= px3::Doom::clockStepCount(); ++i)
        {
            auto s = audible();
            s.clock = static_cast<float>(i) / static_cast<float>(px3::Doom::clockStepCount());
            s.wetTime = 0.6f;
            const auto out = runDoom(s, Source::burst, 1.2);
            const auto stable = out.finite() && out.peak() < 4.0f;
            allStable = allStable && stable;
            if (! stable)
            {
                detail << "clock " << juce::String(s.clock, 2) << " peak "
                       << juce::String(out.peak(), 3) << "  ";
            }
        }
        check("Doom_EveryClockPositionIsStable", allStable, detail);
    }

    // ---- silence, impulse, sine --------------------------------------------
    {
        auto s = audible();
        s.wetTime = 0.9f;
        s.glue = 0.5f;
        const auto quiet = runDoom(s, Source::silence, 2.0);
        check("Doom_SilenceStaysSilent",
              quiet.finite() && quiet.peak() < 1.0e-4f && std::abs(quiet.dc()) < 1.0e-5f,
              "peak " + juce::String(quiet.peak(), 8) + ", dc " + juce::String(quiet.dc(), 8));

        const auto impulse = runDoom(s, Source::impulse, 3.0);
        check("Doom_ImpulseProducesABoundedTail",
              impulse.finite() && impulse.peak() < 4.0f && impulse.tailRms() > 1.0e-6,
              "peak " + juce::String(impulse.peak(), 4) + ", tail rms "
                  + juce::String(impulse.tailRms(), 8));

        juce::String freqDetail;
        auto allFine = true;
        for (const auto hz : { 50.0f, 110.0f, 220.0f, 440.0f, 1000.0f, 5000.0f })
        {
            const auto out = runDoom(s, Source::sine, 1.5, kSampleRate, hz);
            const auto ok = out.finite() && out.peak() < 4.0f;
            allFine = allFine && ok;
            freqDetail << juce::String(static_cast<int>(hz)) << "Hz "
                       << juce::String(out.peak(), 3) << "  ";
        }
        check("Doom_SineIsStableAcrossTheBand", allFine, freqDetail);
    }

    // ---- sample rates and block sizes --------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            auto s = audible();
            s.loopActive = true;
            s.wetTime = 0.5f;
            const auto out = runDoom(s, Source::burst, 1.5, rate, 220.0f, 12345u, 1.0);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << juce::String(static_cast<int>(rate)) << " rms "
                   << juce::String(out.rms(), 4) << "  ";
        }
        check("Doom_RunsAtEverySupportedSampleRate", allFine, detail);
    }

    // ---- the micro-looper --------------------------------------------------
    {
        // Always listening: engaging the looper has to produce audio
        // IMMEDIATELY, from material recorded before it was engaged. A looper
        // that starts recording on engage would be silent here.
        px3::Doom doom;
        doom.prepare(kSampleRate);
        doom.setSeed(999u);

        auto listening = audible();
        listening.loopActive = false;
        listening.wetActive = false;
        doom.updateForBlock(listening);

        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        for (int i = 0; i < static_cast<int>(kSampleRate * 1.5); ++i)
        {
            float l = 0.0f;
            float r = 0.0f;
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;
            doom.processSampleFrame(in, in, l, r);
        }

        // Engage, then feed SILENCE. Anything that comes out came from history.
        auto playing = listening;
        playing.loopActive = true;
        playing.loopModeIndex = 2;      // MASK with threshold 0 = the pure loop
        playing.loopModify = 0.0f;
        playing.balance = 0.0f;         // looper only
        doom.updateForBlock(playing);

        auto captured = 0.0;
        const auto captureLength = static_cast<int>(kSampleRate * 0.5);
        for (int i = 0; i < captureLength; ++i)
        {
            float l = 0.0f;
            float r = 0.0f;
            doom.processSampleFrame(0.0f, 0.0f, l, r);
            captured += static_cast<double>(l) * l + static_cast<double>(r) * r;
        }
        captured = std::sqrt(captured / static_cast<double>(captureLength * 2));

        check("Doom_LooperIsAlwaysListening", captured > 0.01,
              "engaged the looper, fed silence, got rms " + juce::String(captured, 5));
    }

    {
        // Every loop mode, and every Radio station, has to be stable and has to
        // actually produce something.
        static const char* loopModeNames[] = { "BURST", "RADIO", "MASK" };
        juce::String detail;
        auto allFine = true;

        for (int mode = 0; mode < 3; ++mode)
        {
            auto s = audible();
            s.loopActive = true;
            s.wetActive = false;
            s.loopModeIndex = mode;
            s.balance = 0.0f;
            const auto out = runDoom(s, Source::burst, 2.5, kSampleRate, 220.0f, 12345u, 1.5);
            const auto ok = out.finite() && out.peak() < 4.0f && out.tailRms() > 1.0e-5;
            allFine = allFine && ok;
            detail << loopModeNames[mode] << " " << juce::String(out.tailRms(), 5) << "  ";
        }
        check("Doom_EveryLoopModeProducesStableAudio", allFine, detail);

        static const char* stations[] = { "TAPE", "AMBIENT", "ORCHESTRAL", "SHOEGAZE", "DANCE" };
        juce::String stationDetail;
        auto stationsFine = true;
        for (int station = 0; station < 5; ++station)
        {
            auto s = audible();
            s.loopActive = true;
            s.wetActive = false;
            s.loopModeIndex = 1;     // RADIO
            s.loopModify = static_cast<float>(station) / 4.0f;   // parked on a centre
            s.balance = 0.0f;
            const auto out = runDoom(s, Source::burst, 2.5, kSampleRate, 220.0f, 12345u, 1.5);
            const auto ok = out.finite() && out.peak() < 4.0f && out.tailRms() > 1.0e-5;
            stationsFine = stationsFine && ok;
            stationDetail << stations[station] << " " << juce::String(out.tailRms(), 4) << "  ";
        }
        check("Doom_EveryRadioStationProducesStableAudio", stationsFine, stationDetail);

        // Between two stations there is interference; parked on one there is
        // not. That difference is the mode's defining behaviour.
        auto onStation = audible();
        onStation.loopActive = true;
        onStation.wetActive = false;
        onStation.loopModeIndex = 1;
        onStation.loopModify = 0.25f;    // exactly on station 2 of 5
        onStation.balance = 0.0f;
        onStation.loopLength = 0.5f;

        auto betweenStations = onStation;
        betweenStations.loopModify = 0.125f;   // halfway between two

        // Measured on silence after a capture, so what is left IS the static.
        const auto clean = runDoom(onStation, Source::silence, 1.0, kSampleRate, 220.0f, 12345u, 1.5);
        const auto noisy = runDoom(betweenStations, Source::silence, 1.0, kSampleRate, 220.0f, 12345u, 1.5);

        // Measured as high-frequency content relative to level, not as level:
        // off-station the tuned signal is deliberately weaker too, so plain RMS
        // would report the attenuation rather than the interference.
        auto noisiness = [](const Result& r)
        {
            auto hf = 0.0;
            auto total = 0.0;
            for (std::size_t i = 1; i < r.left.size(); ++i)
            {
                const auto d = r.left[i] - r.left[i - 1];
                hf += static_cast<double>(d) * d;
                total += static_cast<double>(r.left[i]) * r.left[i];
            }
            return hf / juce::jmax(1.0e-12, total);
        };

        check("Doom_RadioStaticRisesBetweenStations",
              noisiness(noisy) > noisiness(clean),
              "on station " + juce::String(noisiness(clean), 6) + ", between "
                  + juce::String(noisiness(noisy), 6));
    }

    {
        // MASK at threshold zero is documented as the pure loop, and is the
        // position you build a loop up in - so it has to be clean.
        auto pure = audible();
        pure.loopActive = true;
        pure.wetActive = false;
        pure.loopModeIndex = 2;
        pure.loopModify = 0.0f;
        pure.balance = 0.0f;

        auto masked = pure;
        masked.loopModify = 1.0f;      // always masked
        masked.loopLength = 0.5f;

        const auto clean = runDoom(pure, Source::burst, 2.0, kSampleRate, 220.0f, 12345u, 1.5);
        const auto disguised = runDoom(masked, Source::burst, 2.0, kSampleRate, 220.0f, 12345u, 1.5);

        auto differs = false;
        for (std::size_t i = 0; i < clean.left.size(); ++i)
        {
            if (std::abs(clean.left[i] - disguised.left[i]) > 1.0e-4f)
            {
                differs = true;
                break;
            }
        }
        check("Doom_MaskThresholdChangesTheLoop",
              differs && clean.finite() && disguised.finite(),
              "threshold 0 vs 1 produce different audio");
    }

    {
        // HALF has to actually halve the loop, and playback rate has to change
        // the loop's speed - the buffer itself must be manipulable, not hidden
        // behind a pitch shifter.
        auto normal = audible();
        normal.loopActive = true;
        normal.wetActive = false;
        normal.loopModeIndex = 1;
        normal.loopModify = 0.0f;     // TAPE
        normal.loopLength = 0.5f;     // TAPE: rate = 0 at 0.5, so this is a stop
        normal.balance = 0.0f;

        auto halfSpeed = normal;
        halfSpeed.loopLength = 0.625f;   // TAPE maps 0..1 to -2x..+2x, so this is 0.5x

        auto doubleSpeed = normal;
        doubleSpeed.loopLength = 1.0f;   // +2x

        auto reverse = normal;
        reverse.loopLength = 0.25f;      // -1x

        const auto slow = runDoom(halfSpeed, Source::burst, 2.0, kSampleRate, 220.0f, 12345u, 1.5);
        const auto fast = runDoom(doubleSpeed, Source::burst, 2.0, kSampleRate, 220.0f, 12345u, 1.5);
        const auto back = runDoom(reverse, Source::burst, 2.0, kSampleRate, 220.0f, 12345u, 1.5);

        check("Doom_TapeSupportsHalfDoubleAndReverse",
              slow.finite() && fast.finite() && back.finite()
                  && slow.tailRms() > 1.0e-5 && fast.tailRms() > 1.0e-5 && back.tailRms() > 1.0e-5,
              "0.5x " + juce::String(slow.tailRms(), 5) + ", 2x " + juce::String(fast.tailRms(), 5)
                  + ", -1x " + juce::String(back.tailRms(), 5));

        auto halfLoop = normal;
        halfLoop.loopHalf = true;
        halfLoop.loopLength = 0.625f;
        const auto halved = runDoom(halfLoop, Source::burst, 2.0, kSampleRate, 220.0f, 12345u, 1.5);
        auto differs = false;
        for (std::size_t i = 0; i < slow.left.size(); ++i)
        {
            if (std::abs(slow.left[i] - halved.left[i]) > 1.0e-4f)
            {
                differs = true;
                break;
            }
        }
        check("Doom_LoopHalfChangesTheLoopLength", differs && halved.finite(), "");
    }

    {
        // Overdub has to reach the loop, and FADE has to make it evolve.
        auto noOverdub = audible();
        noOverdub.loopActive = true;
        noOverdub.wetActive = false;
        noOverdub.loopModeIndex = 2;
        noOverdub.loopModify = 0.0f;
        noOverdub.balance = 0.0f;

        auto overdubbing = noOverdub;
        overdubbing.overdub = 0.8f;

        auto fading = overdubbing;
        fading.fade = 0.3f;

        const auto a = runDoom(noOverdub, Source::burst, 2.5, kSampleRate, 220.0f, 12345u, 1.5);
        const auto b = runDoom(overdubbing, Source::burst, 2.5, kSampleRate, 220.0f, 12345u, 1.5);
        const auto c = runDoom(fading, Source::burst, 2.5, kSampleRate, 220.0f, 12345u, 1.5);

        auto overdubDiffers = false;
        auto fadeDiffers = false;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            overdubDiffers = overdubDiffers || std::abs(a.left[i] - b.left[i]) > 1.0e-4f;
            fadeDiffers = fadeDiffers || std::abs(b.left[i] - c.left[i]) > 1.0e-4f;
        }
        check("Doom_OverdubAndFadeReachTheLoop",
              overdubDiffers && fadeDiffers && a.finite() && b.finite() && c.finite(),
              "");
    }


    // ---- artifacts ---------------------------------------------------------
    {
        // A discontinuity measured against the LOCAL SLOPE rather than against
        // an absolute threshold, because a loud passage legitimately has large
        // sample-to-sample deltas and a click in a quiet one does not.
        //
        // This found two real faults: BURST read the loop with a bare wrap
        // rather than a splice, and MASK's reversal and pitch disguises were
        // derived from the playback position, so they inherited ITS wrap and
        // landed where their own crossfade was not. 27x and 21x the local slope
        // respectively; both are now around 2.
        auto worstRatio = [](const std::vector<float>& x)
        {
            constexpr int kWindow = 96;
            const auto skip = static_cast<int>(kSampleRate * 2.0);
            auto worst = 0.0;

            for (int i = skip + kWindow; i + kWindow < static_cast<int>(x.size()); ++i)
            {
                const auto jump = std::abs(static_cast<double>(x[static_cast<std::size_t>(i)])
                                           - x[static_cast<std::size_t>(i - 1)]);
                double sum = 0.0;
                int count = 0;
                for (int k = i - kWindow; k < i + kWindow; ++k)
                {
                    if (k == i || k == i - 1)
                    {
                        continue;
                    }
                    const auto d = static_cast<double>(x[static_cast<std::size_t>(k)])
                                   - x[static_cast<std::size_t>(k - 1)];
                    sum += d * d;
                    ++count;
                }
                const auto reference = std::sqrt(sum / juce::jmax(1, count));
                if (reference > 1.0e-7)
                {
                    worst = juce::jmax(worst, jump / reference);
                }
            }
            return worst;
        };

        auto runLoopMode = [&](int loopMode, float modify)
        {
            auto s = audible();
            s.loopActive = true;
            s.wetActive = false;
            s.loopModeIndex = loopMode;
            s.loopModify = modify;
            s.loopLength = 0.5f;
            s.balance = 0.0f;
            return runDoom(s, Source::sine, 7.0, kSampleRate, 220.0f, 4242u, 4.0);
        };

        juce::String detail;
        auto allClean = true;

        static const char* names[] = { "BURST", "RADIO", "MASK" };
        for (int mode = 0; mode < 3; ++mode)
        {
            for (const auto modify : { 0.0f, 0.5f, 1.0f })
            {
                const auto ratio = worstRatio(runLoopMode(mode, modify).left);
                allClean = allClean && ratio < 6.0;
                if (ratio >= 6.0)
                {
                    detail << names[mode] << " " << juce::String(modify, 1) << " = "
                           << juce::String(ratio, 1) << "  ";
                }
            }
        }

        check("Doom_NoLoopModeClicksAtItsLoopWrap", allClean,
              detail.isEmpty() ? "every loop mode stays under 6x the local slope" : detail);
    }

    // ---- the wet channel ---------------------------------------------------
    {
        static const char* wetNames[] = { "SOUP", "RELAY", "FLIP" };
        juce::String detail;
        auto allFine = true;
        for (int mode = 0; mode < 3; ++mode)
        {
            auto s = audible();
            s.wetModeIndex = mode;
            s.wetTime = 0.5f;
            s.wetModify = 0.5f;
            s.balance = 1.0f;    // wet channel only
            const auto out = runDoom(s, Source::burst, 2.5);
            const auto ok = out.finite() && out.peak() < 4.0f && out.tailRms() > 1.0e-5;
            allFine = allFine && ok;
            detail << wetNames[mode] << " " << juce::String(out.tailRms(), 5) << "  ";
        }
        check("Doom_EveryWetModeProducesStableAudio", allFine, detail);
    }

    {
        // SOUP is a spectral reverb, so a longer TIME has to leave more energy
        // behind after the input stops. This is the one measurement that says
        // the magnitude accumulator is actually decaying rather than gating.
        auto shortDecay = audible();
        shortDecay.wetModeIndex = 0;
        shortDecay.wetTime = 0.15f;
        shortDecay.balance = 1.0f;

        auto longDecay = shortDecay;
        longDecay.wetTime = 0.95f;

        // One impulse, then silence: what is left is the tail.
        const auto quick = runDoom(shortDecay, Source::impulse, 4.0);
        const auto slow = runDoom(longDecay, Source::impulse, 4.0);

        check("Doom_SoupDecayTimeLengthensTheTail",
              slow.tailRms() > quick.tailRms() * 1.5 && slow.finite() && quick.finite(),
              "short " + juce::String(quick.tailRms(), 8) + ", long "
                  + juce::String(slow.tailRms(), 8));

        // Character is the synthetic axis: high character randomises phase and
        // blurs the magnitude spectrum, which must change the output.
        auto coherent = longDecay;
        coherent.wetModify = 0.0f;
        auto scattered = longDecay;
        scattered.wetModify = 1.0f;

        const auto a = runDoom(coherent, Source::burst, 2.0);
        const auto b = runDoom(scattered, Source::burst, 2.0);
        auto differs = false;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            if (std::abs(a.left[i] - b.left[i]) > 1.0e-3f)
            {
                differs = true;
                break;
            }
        }
        check("Doom_SoupCharacterChangesTheResynthesis",
              differs && a.finite() && b.finite(),
              "coherent rms " + juce::String(a.rms(), 5) + ", scattered "
                  + juce::String(b.rms(), 5));
    }

    {
        // RELAY's repeats do NOT fade: that is the whole mode. Measured as the
        // level of the last repeat against the first - a feedback delay would
        // show a geometric decay here.
        auto s = audible();
        s.wetModeIndex = 1;
        s.wetTime = 0.35f;
        s.wetModify = 0.75f;    // several repeats
        s.balance = 1.0f;
        s.glue = 0.0f;

        const auto out = runDoom(s, Source::impulse, 3.0);

        // The N loudest 20ms windows ARE the N repeats. Comparing the quietest
        // of them to the loudest is the whole claim: under geometric feedback
        // decay the last repeat would be a small fraction of the first.
        //
        // Measured as window ENERGY, not peak. A one-sample impulse landing at
        // a fractional delay is split across two samples by the interpolator,
        // so its peak depends on the fractional part and varies by up to 6 dB
        // between taps - while its energy does not.
        std::vector<double> windowEnergy;
        const auto windowSize = static_cast<std::size_t>(kSampleRate * 0.02);
        for (std::size_t w = 0; w + windowSize < out.left.size(); w += windowSize)
        {
            auto sum = 0.0;
            for (std::size_t i = w; i < w + windowSize; ++i)
            {
                sum += static_cast<double>(out.left[i]) * out.left[i];
            }
            windowEnergy.push_back(std::sqrt(sum / static_cast<double>(windowSize)));
        }

        std::sort(windowEnergy.begin(), windowEnergy.end(), std::greater<double>());

        // How many repeats there are is the mode's business, not the test's -
        // counting the windows that carry energy avoids restating its mapping
        // here, where a copy could drift out of step with the real one.
        auto repeats = 0;
        for (const auto energy : windowEnergy)
        {
            if (energy > 1.0e-5)
            {
                ++repeats;
            }
        }

        auto ratio = 0.0;
        if (repeats >= 2)
        {
            ratio = windowEnergy[static_cast<std::size_t>(repeats - 1)]
                    / juce::jmax(1.0e-9, windowEnergy.front());
        }

        juce::String levels;
        for (std::size_t i = 0; i < std::min<std::size_t>(8u, windowEnergy.size()); ++i)
        {
            levels << juce::String(windowEnergy[i], 5) << " ";
        }

        check("Doom_RelayRepeatsDoNotFade",
              repeats >= 2 && ratio > 0.6 && out.finite(),
              "quietest of " + juce::String(repeats) + " repeats is "
                  + juce::String(ratio, 3) + " of the loudest [" + levels.trim() + "]");

        // More repeats must not mean more level.
        auto few = s;
        few.wetModify = 0.1f;
        auto many = s;
        many.wetModify = 0.9f;
        const auto a = runDoom(few, Source::burst, 2.0);
        const auto b = runDoom(many, Source::burst, 2.0);
        check("Doom_RelayRepeatCountIsLevelNeutral",
              b.rms() < a.rms() * 3.0 && b.finite(),
              "1 repeat rms " + juce::String(a.rms(), 5) + ", many "
                  + juce::String(b.rms(), 5));

        // The looper position sustains rather than exploding.
        auto infinite = s;
        infinite.wetModify = 1.0f;
        const auto held = runDoom(infinite, Source::burst, 6.0);
        check("Doom_RelayAtMaximumSustainsWithoutRunaway",
              held.finite() && held.peak() < 4.0f && held.tailRms() > 1.0e-5,
              "peak " + juce::String(held.peak(), 4) + ", tail "
                  + juce::String(held.tailRms(), 5));
    }

    {
        // FLIP has to produce actual harmonies, and more of them as MODIFY
        // rises. Measured by the count of distinct spectral peaks.
        juce::String detail;
        auto allFine = true;
        for (const auto modify : { 0.0f, 0.3f, 0.6f, 1.0f })
        {
            auto s = audible();
            s.wetModeIndex = 2;
            s.wetTime = 0.2f;
            s.wetModify = modify;
            s.balance = 1.0f;
            const auto out = runDoom(s, Source::sine, 2.0, kSampleRate, 220.0f);
            const auto ok = out.finite() && out.peak() < 4.0f && out.tailRms() > 1.0e-5;
            allFine = allFine && ok;
            detail << juce::String(modify, 1) << ":" << juce::String(out.tailRms(), 4) << "  ";
        }
        check("Doom_FlipProducesStableHarmoniesAcrossTheTable", allFine, detail);
    }

    {
        // Freeze holds each mode's sound: it must sustain on silence.
        static const char* wetNames[] = { "SOUP", "RELAY", "FLIP" };
        juce::String detail;
        auto allFine = true;

        for (int mode = 0; mode < 3; ++mode)
        {
            px3::Doom doom;
            doom.prepare(kSampleRate);
            doom.setSeed(4242u);

            auto s = audible();
            s.wetModeIndex = mode;
            s.wetTime = 0.5f;
            s.balance = 1.0f;
            doom.updateForBlock(s);

            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            for (int i = 0; i < static_cast<int>(kSampleRate * 1.5); ++i)
            {
                float l = 0.0f;
                float r = 0.0f;
                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;
                doom.processSampleFrame(in, in, l, r);
            }

            s.freeze = true;
            doom.updateForBlock(s);

            auto energy = 0.0;
            const auto length = static_cast<int>(kSampleRate * 2.0);
            auto peak = 0.0f;
            for (int i = 0; i < length; ++i)
            {
                float l = 0.0f;
                float r = 0.0f;
                doom.processSampleFrame(0.0f, 0.0f, l, r);
                energy += static_cast<double>(l) * l + static_cast<double>(r) * r;
                peak = juce::jmax(peak, std::abs(l), std::abs(r));
            }
            energy = std::sqrt(energy / static_cast<double>(length * 2));

            const auto ok = std::isfinite(peak) && peak < 4.0f && energy > 1.0e-5;
            allFine = allFine && ok;
            detail << wetNames[mode] << " " << juce::String(energy, 5) << "  ";
        }
        check("Doom_FreezeSustainsInEveryWetMode", allFine, detail);
    }

    // ---- CROSS -------------------------------------------------------------
    {
        juce::String detail;
        auto allStable = true;
        std::vector<double> rmsByDepth;

        for (const auto depth : { 0.0f, 0.25f, 0.5f, 1.0f })
        {
            auto s = audible();
            s.cross = depth;
            s.wetTime = 0.5f;
            s.balance = 1.0f;
            const auto out = runDoom(s, Source::burst, 3.0);
            allStable = allStable && out.finite() && out.peak() < 4.0f;
            rmsByDepth.push_back(out.rms());
            detail << juce::String(depth, 2) << ":" << juce::String(out.rms(), 5) << "  ";
        }

        check("Doom_CrossIsStableAtEveryIntensity", allStable, detail);

        // Increasing Cross must actually change the behaviour, not just sit
        // there - it interferes with amplitude, so the level moves.
        check("Doom_CrossChangesBehaviourAsItRises",
              std::abs(rmsByDepth.back() - rmsByDepth.front()) > 1.0e-4,
              "off " + juce::String(rmsByDepth.front(), 6) + ", max "
                  + juce::String(rmsByDepth.back(), 6));

        // Both sources have to work, and the channel-modulates-channel case is
        // the one that could become an algebraic loop.
        auto channelSource = audible();
        channelSource.cross = 1.0f;
        channelSource.crossSourceIndex = 1;
        channelSource.loopActive = true;
        channelSource.wetTime = 0.7f;
        const auto crossed = runDoom(channelSource, Source::burst, 4.0);
        check("Doom_CrossChannelToChannelStaysBounded",
              crossed.finite() && crossed.peak() < 4.0f,
              "peak " + juce::String(crossed.peak(), 4));
    }

    // ---- GLUE --------------------------------------------------------------
    {
        juce::String detail;
        auto allStable = true;
        std::vector<double> rmsByGlue;

        for (const auto glue : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.glue = glue;
            s.wetTime = 0.4f;
            s.balance = 1.0f;
            const auto out = runDoom(s, Source::sine, 2.0);
            allStable = allStable && out.finite() && out.peak() < 4.0f
                        && std::abs(out.dc()) < 0.05;
            rmsByGlue.push_back(out.rms());
            detail << juce::String(glue, 2) << ":" << juce::String(out.rms(), 4) << "  ";
        }

        check("Doom_GlueIsStableAndDcFreeAtEveryAmount", allStable, detail);

        // Level-compensated: turning GLUE up changes character, not loudness.
        // Without the per-region makeup this ratio runs away.
        auto maxRatio = 0.0;
        for (const auto rms : rmsByGlue)
        {
            maxRatio = juce::jmax(maxRatio, rms / juce::jmax(1.0e-9, rmsByGlue.front()));
        }
        check("Doom_GlueIsRoughlyLevelNeutral", maxRatio < 3.0,
              "loudest/cleanest = " + juce::String(maxRatio, 3));

        // And it has to actually do something.
        check("Doom_GlueChangesTheSignal",
              std::abs(rmsByGlue.back() - rmsByGlue.front()) > 1.0e-5,
              "");
    }

    // ---- EQ, balance, blend, spread ---------------------------------------
    {
        auto flat = audible();
        flat.wetTime = 0.4f;
        flat.balance = 1.0f;

        auto dark = flat;
        dark.eq = -1.0f;
        auto bright = flat;
        bright.eq = 1.0f;

        const auto a = runDoom(dark, Source::noise, 1.5);
        const auto b = runDoom(bright, Source::noise, 1.5);

        // A tilt: one direction removes highs, the other removes lows. Measured
        // as the high-frequency energy, which must be larger on the bright side.
        auto highEnergy = [](const Result& r)
        {
            auto sum = 0.0;
            for (std::size_t i = 1; i < r.left.size(); ++i)
            {
                const auto d = r.left[i] - r.left[i - 1];
                sum += static_cast<double>(d) * d;
            }
            return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1u, r.left.size() - 1)));
        };

        check("Doom_EqTiltsRatherThanCuts",
              highEnergy(b) > highEnergy(a) && a.finite() && b.finite(),
              "dark hf " + juce::String(highEnergy(a), 6) + ", bright hf "
                  + juce::String(highEnergy(b), 6));

        // Balance crossfades the two channels.
        auto loopOnly = audible();
        loopOnly.loopActive = true;
        loopOnly.balance = 0.0f;
        auto wetOnly = loopOnly;
        wetOnly.balance = 1.0f;
        const auto l = runDoom(loopOnly, Source::burst, 2.0, kSampleRate, 220.0f, 12345u, 1.5);
        const auto w = runDoom(wetOnly, Source::burst, 2.0, kSampleRate, 220.0f, 12345u, 1.5);
        auto balanceDiffers = false;
        for (std::size_t i = 0; i < l.left.size(); ++i)
        {
            if (std::abs(l.left[i] - w.left[i]) > 1.0e-4f)
            {
                balanceDiffers = true;
                break;
            }
        }
        check("Doom_BalanceCrossfadesTheTwoChannels",
              balanceDiffers && l.finite() && w.finite(), "");

        // Spread has to widen the image rather than just being carried around.
        auto narrow = audible();
        narrow.loopActive = true;
        narrow.loopModeIndex = 1;
        narrow.spread = 0.0f;
        narrow.balance = 0.0f;
        auto wide = narrow;
        wide.spread = 1.0f;

        const auto n = runDoom(narrow, Source::burst, 2.5, kSampleRate, 220.0f, 12345u, 1.5);
        const auto wd = runDoom(wide, Source::burst, 2.5, kSampleRate, 220.0f, 12345u, 1.5);
        check("Doom_SpreadWidensTheImage",
              wd.correlation() < n.correlation() + 0.01 && n.finite() && wd.finite(),
              "narrow corr " + juce::String(n.correlation(), 4) + ", wide "
                  + juce::String(wd.correlation(), 4));
    }

    // ---- routing -----------------------------------------------------------
    {
        // ROUTING only means anything when both channels are on, as documented.
        juce::String detail;
        auto allFine = true;
        std::vector<double> byRouting;

        for (int routing = 0; routing < 3; ++routing)
        {
            auto s = audible();
            s.loopActive = true;
            s.wetActive = true;
            s.routingIndex = routing;
            s.wetTime = 0.5f;
            const auto out = runDoom(s, Source::burst, 2.5, kSampleRate, 220.0f, 12345u, 1.5);
            allFine = allFine && out.finite() && out.peak() < 4.0f;
            byRouting.push_back(out.rms());
            detail << routing << ":" << juce::String(out.rms(), 5) << "  ";
        }

        check("Doom_EveryRoutingIsStable", allFine, detail);
        check("Doom_RoutingChangesWhatTheWetChannelHears",
              std::abs(byRouting[0] - byRouting[2]) > 1.0e-5,
              "INPUT " + juce::String(byRouting[0], 6) + ", LOOP "
                  + juce::String(byRouting[2], 6));
    }

    // ---- hostile combinations ---------------------------------------------
    {
        struct Hostile { const char* name; DoomSettings settings; };

        auto everything = audible();
        everything.loopActive = true;
        everything.wetActive = true;
        everything.loopModeIndex = 0;
        everything.wetModeIndex = 1;
        everything.wetTime = 1.0f;
        everything.wetModify = 1.0f;
        everything.loopLength = 1.0f;
        everything.loopModify = 1.0f;
        everything.cross = 1.0f;
        everything.crossSourceIndex = 1;
        everything.glue = 1.0f;
        everything.overdub = 1.0f;
        everything.fade = 1.0f;
        everything.clock = 0.0f;
        everything.spread = 1.0f;
        everything.freeze = true;

        auto soupExtreme = everything;
        soupExtreme.wetModeIndex = 0;
        soupExtreme.clock = 1.0f;

        auto flipExtreme = everything;
        flipExtreme.wetModeIndex = 2;
        flipExtreme.clock = 0.5f;

        auto noFade = everything;
        noFade.fade = 0.0f;
        noFade.freeze = false;

        const std::array<Hostile, 4> cases { {
            { "everything+relay+lowest clock", everything },
            { "everything+soup+highest clock", soupExtreme },
            { "everything+flip", flipExtreme },
            { "everything, fade 0, unfrozen", noFade },
        } };

        juce::String detail;
        auto allSurvive = true;
        for (const auto& c : cases)
        {
            const auto out = runDoom(c.settings, Source::noise, 6.0, kSampleRate, 220.0f, 12345u, 1.0);
            const auto ok = out.finite() && out.peak() < 8.0f && std::abs(out.dc()) < 0.2;
            allSurvive = allSurvive && ok;
            if (! ok)
            {
                detail << c.name << " peak " << juce::String(out.peak(), 3)
                       << " dc " << juce::String(out.dc(), 4) << "  ";
            }
        }
        check("Doom_HostileCombinationsStayValidAudio", allSurvive,
              detail.isEmpty() ? "all four survive 6s of noise at maximum everything" : detail);
    }

    // ---- automation --------------------------------------------------------
    {
        // Every continuous control swept min to max while audio runs. What this
        // is looking for is a discontinuity the signal itself cannot explain.
        struct Sweep { const char* name; float DoomSettings::* member; };
        const std::array<Sweep, 10> sweeps { {
            { "clock", &DoomSettings::clock },
            { "mix", &DoomSettings::mix },
            { "loopLength", &DoomSettings::loopLength },
            { "loopModify", &DoomSettings::loopModify },
            { "wetTime", &DoomSettings::wetTime },
            { "wetModify", &DoomSettings::wetModify },
            { "cross", &DoomSettings::cross },
            { "glue", &DoomSettings::glue },
            { "balance", &DoomSettings::balance },
            { "spread", &DoomSettings::spread },
        } };

        juce::String detail;
        auto allSmooth = true;

        for (const auto& sweep : sweeps)
        {
            px3::Doom doom;
            doom.prepare(kSampleRate);
            doom.setSeed(777u);

            auto s = audible();
            s.loopActive = true;
            s.wetTime = 0.5f;

            const auto total = static_cast<int>(kSampleRate * 3.0);
            const auto blockSize = 64;

            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            auto worstStep = 0.0f;
            auto previous = 0.0f;
            auto valid = true;

            for (int i = 0; i < total; ++i)
            {
                if (i % blockSize == 0)
                {
                    s.*(sweep.member) = static_cast<float>(i) / static_cast<float>(total);
                    doom.updateForBlock(s);
                }

                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;

                float l = 0.0f;
                float r = 0.0f;
                doom.processSampleFrame(in, in, l, r);
                valid = valid && std::isfinite(l) && std::isfinite(r) && std::abs(l) < 8.0f;

                if (i > 1000)
                {
                    worstStep = juce::jmax(worstStep, std::abs(l - previous));
                }
                previous = l;
            }

            // The input itself steps by up to 2*pi*f/fs * amplitude per sample.
            // The threshold is generous: this is looking for a click, not for
            // slope.
            const auto ok = valid && worstStep < 0.75f;
            allSmooth = allSmooth && ok;
            if (! ok)
            {
                detail << sweep.name << " step " << juce::String(worstStep, 4) << "  ";
            }
        }

        check("Doom_EveryParameterSweepsWithoutDiscontinuity", allSmooth,
              detail.isEmpty() ? "10 controls swept min to max under audio" : detail);
    }

    // ---- determinism -------------------------------------------------------
    {
        auto s = audible();
        s.loopActive = true;
        s.loopModeIndex = 0;      // BURST, whose fills are stochastic
        s.loopModify = 1.0f;      // maximum sensitivity, so fills fire often

        const auto a = runDoom(s, Source::burst, 2.0, kSampleRate, 220.0f, 555u, 1.5);
        const auto b = runDoom(s, Source::burst, 2.0, kSampleRate, 220.0f, 555u, 1.5);
        const auto c = runDoom(s, Source::burst, 2.0, kSampleRate, 220.0f, 556u, 1.5);

        auto sameSeedMatches = a.left.size() == b.left.size();
        for (std::size_t i = 0; i < a.left.size() && sameSeedMatches; ++i)
        {
            sameSeedMatches = juce::approximatelyEqual(a.left[i], b.left[i]);
        }

        auto differentSeedDiffers = false;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            if (std::abs(a.left[i] - c.left[i]) > 1.0e-6f)
            {
                differentSeedDiffers = true;
                break;
            }
        }

        check("Doom_SeededRandomnessIsDeterministic",
              sameSeedMatches && differentSeedDiffers,
              "same seed is sample-identical; a different seed is not");
    }

    // ---- bypass ------------------------------------------------------------
    {
        // Bypass is not mix = 0: it stops the processing. What matters audibly
        // is that the dry signal comes through unchanged and without a click.
        px3::Doom doom;
        doom.prepare(kSampleRate);

        auto s = audible();
        s.wetTime = 0.8f;
        doom.updateForBlock(s);

        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i)
        {
            float l = 0.0f;
            float r = 0.0f;
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;
            doom.processSampleFrame(in, in, l, r);
        }

        s.enabled = false;
        doom.updateForBlock(s);

        auto worstStep = 0.0f;
        auto previous = 0.0f;
        auto maxDeviation = 0.0f;
        const auto length = static_cast<int>(kSampleRate * 0.5);
        for (int i = 0; i < length; ++i)
        {
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;

            float l = 0.0f;
            float r = 0.0f;
            doom.processSampleFrame(in, in, l, r);

            if (i > 0)
            {
                worstStep = juce::jmax(worstStep, std::abs(l - previous));
            }
            previous = l;

            // After the bypass ramp has settled, the output IS the input.
            if (i > static_cast<int>(kSampleRate * 0.1))
            {
                maxDeviation = juce::jmax(maxDeviation, std::abs(l - in));
            }
        }

        check("Doom_BypassPassesDryWithoutAClick",
              maxDeviation < 1.0e-5f && worstStep < 0.2f,
              "deviation from dry " + juce::String(maxDeviation, 8) + ", worst step "
                  + juce::String(worstStep, 5));
    }

    // ---- the card ----------------------------------------------------------
    {
        // The card owns 24 controls and attaches every one. A typo in an id
        // would leave a control silently unattached, which is invisible until
        // someone turns it and nothing happens - so it is checked here.
        px3::ui::FxCardComponent card("doom", "DOOM");
        card.addKnobRow({ { "a", "A", "" }, { "b", "B", "" } });
        card.addChoiceRow({ { "c", "C", "", juce::StringArray { "X", "Y" } } });
        card.addToggleRow({ { "d", "ON", "OFF", "" } });
        card.addFeatureKnobRow({ "e", "E", "" });
        card.setBounds(0, 0, 300, 400);
        card.resized();

        check("FxCard_LooksUpEveryControlItDeclares",
              card.knob("a") != nullptr && card.knob("b") != nullptr
                  && card.choice("c") != nullptr && card.toggle("d") != nullptr
                  && card.knob("e") != nullptr,
              "");

        check("FxCard_ReturnsNullForAnIdItDoesNotHave",
              card.knob("nope") == nullptr && card.choice("nope") == nullptr
                  && card.toggle("nope") == nullptr,
              "an unattached control must be a null here, not a silent no-op");

        check("FxCard_LaysEveryControlInsideItself",
              card.getLocalBounds().contains(card.knob("a")->getBounds())
                  && card.getLocalBounds().contains(card.choice("c")->getBounds())
                  && card.getLocalBounds().contains(card.toggle("d")->getBounds())
                  && card.getLocalBounds().contains(card.knob("e")->getBounds()),
              "");

        // The feature knob is the macro the rest of the card feeds; it has to
        // actually be drawn larger than the ordinary ones.
        check("FxCard_FeatureKnobIsLargerThanTheRest",
              card.knob("e")->getWidth() > card.knob("a")->getWidth(),
              "feature " + juce::String(card.knob("e")->getWidth()) + "px, ordinary "
                  + juce::String(card.knob("a")->getWidth()) + "px");

        card.setActive(false);
        check("FxCard_BypassDisablesEveryControl",
              ! card.knob("a")->isEnabled() && ! card.choice("c")->isEnabled()
                  && ! card.toggle("d")->isEnabled(),
              "");
    }

    {
        // Styling comes from UIConfig, so a card block is real configuration
        // rather than a set of properties nothing reads.
        juce::String error;
        const auto config = UIConfig::fromJsonText(R"({"cards":{"doom":{"controls":{
            "knobSize":40,"featureKnobSize":120,"choiceWidth":50,"toggleWidth":60}}}})",
                                                   error);

        px3::ui::FxCardComponent styled("doom", "DOOM");
        styled.addKnobRow({ { "a", "A", "" } });
        styled.addFeatureKnobRow({ "b", "B", "" });
        styled.setUIConfig(config);
        styled.setBounds(0, 0, 400, 400);
        styled.resized();

        px3::ui::FxCardComponent plain("doom", "DOOM");
        plain.addKnobRow({ { "a", "A", "" } });
        plain.addFeatureKnobRow({ "b", "B", "" });
        plain.setBounds(0, 0, 400, 400);
        plain.resized();

        check("FxCard_ControlSizesComeFromUIConfig",
              styled.knob("a")->getWidth() != plain.knob("a")->getWidth()
                  && styled.knob("b")->getWidth() != plain.knob("b")->getWidth(),
              "configured " + juce::String(styled.knob("a")->getWidth()) + "px vs default "
                  + juce::String(plain.knob("a")->getWidth()) + "px");
    }

    {
        // The shipping config has to define DOOM's card, or it falls back to
        // defaults and looks like nothing else in the plugin.
        UIConfigManager manager;
        manager.setConfigFile(juce::File::getCurrentWorkingDirectory()
                                  .getChildFile("Source/UI/UIConfig.json"));
        manager.loadInitial();
        const auto config = manager.getConfig();

        // The cardInner block is checked for EXISTENCE, not for particular
        // numbers. Its padding may be written as "padding" or as per-side
        // "paddingTop"/etc, and any of those may legitimately be zero - a card
        // whose contents run to its edge is a styling decision, not a missing
        // config. Pinning a magnitude here just fails the suite whenever the
        // layout is tuned.
        const auto inner = px3::ui::CardInnerStyle::fromConfig(config.get(), "cards.doom.cardInner", 5);
        const auto declaresRows = ! config->getValue("cards.doom.cardInner.rows.row1.height").isVoid();

        check("Doom_ShippingConfigStylesTheCard",
              config != nullptr
                  && config->getColour("cards.doom.border.color", juce::Colours::black)
                         != juce::Colours::black
                  && config->getFloat("cards.doom.controls.knobSize", -1.0f) > 0.0f
                  && declaresRows
                  && static_cast<int>(inner.rows.size()) == 5,
              "rows declared " + juce::String(declaresRows ? 1 : 0)
                  + ", parsed " + juce::String((int) inner.rows.size())
                  + ", padding t " + juce::String(inner.padding.top, 1)
                  + " r " + juce::String(inner.padding.right, 1)
                  + " b " + juce::String(inner.padding.bottom, 1)
                  + " l " + juce::String(inner.padding.left, 1));
    }



    {
        // Where does the vertical space in the top block actually go? Built at
        // the real card size with the real config, then the laid-out bounds are
        // printed rather than reasoned about.
        UIConfigManager manager;
        manager.setConfigFile(juce::File::getCurrentWorkingDirectory()
                                  .getChildFile("Source/UI/UIConfig.json"));
        manager.loadInitial();
        const auto config = manager.getConfig();

        px3::ui::FxCardComponent card("doom", "DOOM");
        card.addToggleRow({ { "a", "A", "A", "" }, { "b", "B", "B", "" }, { "c", "C", "C", "" },
                            { "d", "D", "D", "" }, { "e", "E", "E", "" }, { "f", "F", "F", "" } });
        card.addChoiceRow({ { "g", "G", "", juce::StringArray { "1", "2" } },
                            { "h", "H", "", juce::StringArray { "1", "2" } },
                            { "i", "I", "", juce::StringArray { "1", "2" } } });
        card.addKnobRow({ { "k1", "K", "" }, { "k2", "K", "" } });
        card.addKnobRow({ { "k3", "K", "" }, { "k4", "K", "" } });
        card.addFeatureKnobRow({ "m", "M", "" });
        card.setUIConfig(config);

        const auto cellWidth = (1320 - 14 - 8 * 3) / 4;
        card.setBounds(0, 0, cellWidth, config->getInt("fx.grid.rowHeight", 400));
        card.resized();

        juce::String detail;
        detail << "\n      chips line1 y=" << card.toggle("a")->getY()
               << " h=" << card.toggle("a")->getHeight();
        detail << "\n      chips line2 y=" << card.toggle("d")->getY()
               << " h=" << card.toggle("d")->getHeight();
        detail << "\n      dropdown    y=" << card.choice("g")->getY()
               << " h=" << card.choice("g")->getHeight();

        const auto line1Bottom = card.toggle("a")->getBottom();
        const auto line2Top = card.toggle("d")->getY();
        const auto line2Bottom = card.toggle("d")->getBottom();
        const auto choiceTop = card.choice("g")->getY();

        detail << "\n      gap line1->line2 = " << (line2Top - line1Bottom) << "px";
        detail << "\n      gap line2->dropdown = " << (choiceTop - line2Bottom) << "px";

        // And the rows themselves, so the slack can be attributed rather than
        // inferred from where the controls ended up.
        {
            px3::ui::CardHost host;
            host.setStyleKey("doom");
            host.setConfig(config);
            host.layout(card.getLocalBounds());

            px3::ui::CardInner probe;
            probe.setStylePath("cards.doom.cardInner");
            probe.setConfig(config);
            probe.setRowCount(5);
            probe.layout(host.contentBelowTitle());

            detail << "\n      contentBelowTitle h=" << host.contentBelowTitle().getHeight();
            for (int i = 0; i < 3; ++i)
            {
                const auto r = probe.rowContent(i);
                detail << "\n      row" << (i + 1) << " y=" << r.getY() << " h=" << r.getHeight();
            }
        }

        // Pinned, not just printed. These two rows carry fixed-height controls,
        // so they are sized in PIXELS rather than as a percentage - a percentage
        // leaves slack that shows up as vertical gap, and it was 16px between
        // the chip lines and 50px above the dropdowns before that change.
        //
        // The remaining gap above the dropdowns is their own 12px label, which
        // is a real element rather than slack, plus whatever paddingTop the
        // card's config asks for. The padding is READ rather than budgeted for:
        // this test exists to catch slack nobody asked for, and hard-coding an
        // allowance would make it fail the next time somebody deliberately
        // spaces the row - which is exactly what happened.
        const auto configuredPadding =
            config != nullptr ? config->getInt("cards.doom.cardInner.rows.row2.paddingTop", 0) : 0;

        check("FxCard_TopBlockIsVerticallyTight",
              (line2Top - line1Bottom) <= 4
                  && (choiceTop - line2Bottom) <= 28 + configuredPadding,
              detail + "\n      allowed 28 + " + juce::String(configuredPadding)
                  + "px of configured paddingTop");
    }

    {
        // DOOM and LUCY cap their toggle row at three across and put three
        // dropdowns on one line. Both are pinned here because a control a few
        // pixels too wide silently reflows instead of failing, which is how a
        // layout drifts without anyone noticing.
        UIConfigManager manager;
        manager.setConfigFile(juce::File::getCurrentWorkingDirectory()
                                  .getChildFile("Source/UI/UIConfig.json"));
        manager.loadInitial();
        const auto config = manager.getConfig();

        juce::StringArray wrapped;
        auto rowWidth = 0.0f;

        // Checked at three card widths, not one. toggleMaxColumns exists
        // precisely so the answer does not depend on the window size.
        for (const auto editorWidth : { 1100, 1320, 1800 })
        {
            const auto columns = config->getInt("fx.grid.columns", 4);
            const auto gridGap = config->getInt("fx.grid.gap", 8);
            const auto contentWidth = editorWidth - 14;
            const auto cellWidth = (contentWidth - gridGap * (columns - 1)) / columns;

            for (const auto* card : { "doom", "lucy" })
            {
                const juce::String key(card);
                rowWidth = static_cast<float>(cellWidth
                                              - 2 * config->getInt("cards." + key + ".cardInner.padding", 4)
                                              - 8);

                const auto toggleGap = static_cast<float>(
                    config->getInt("cards." + key + ".cardInner.rows.row1.gap", 2));
                const auto maxColumns = config->getInt("cards." + key + ".controls.toggleMaxColumns", 0);

                // The width the card will actually compute for a capped row.
                const auto configuredToggle = config->getFloat("cards." + key + ".controls.toggleWidth", 68.0f);
                const auto toggleWidth = maxColumns > 0
                                             ? juce::jmin(configuredToggle,
                                                          juce::jmax(16.0f, rowWidth / static_cast<float>(maxColumns)
                                                                                - 2.0f * toggleGap))
                                             : configuredToggle;

                const std::vector<float> toggles(6, toggleWidth);
                if (px3::ui::wrappedLineCount(toggles, toggleGap * 2.0f, rowWidth) != 2)
                {
                    wrapped.add(key + " toggles @" + juce::String(editorWidth));
                }

                const auto choiceGap = static_cast<float>(
                    config->getInt("cards." + key + ".cardInner.rows.row2.gap", 3));
                const auto choiceColumns = config->getInt("cards." + key + ".controls.choiceMaxColumns", 0);
                const auto configuredChoice = config->getFloat("cards." + key + ".controls.choiceWidth", 92.0f);
                const auto choiceWidth = choiceColumns > 0
                                             ? juce::jmin(configuredChoice,
                                                          juce::jmax(16.0f, rowWidth / static_cast<float>(choiceColumns)
                                                                                - 2.0f * choiceGap))
                                             : configuredChoice;
                const std::vector<float> choices(3, choiceWidth);
                if (px3::ui::wrappedLineCount(choices, choiceGap * 2.0f, rowWidth) > 1)
                {
                    wrapped.add(key + " dropdowns @" + juce::String(editorWidth));
                }
            }
        }

        {
            // The width property has to bite at a wide card, or a dropdown row
            // stretches into three banners. maxColumns sets the room available;
            // the width caps what is taken of it.
            const auto wideRow = static_cast<float>((1800 - 14 - 8 * 3) / 4 - 2 * 4 - 8);
            const auto share = wideRow / 3.0f - 2.0f * 1.0f;
            const auto width = config->getFloat("cards.doom.controls.choiceWidth", 0.0f);

            check("FxCard_ChoiceWidthCapsAWideCard",
                  width > 0.0f && width < share,
                  "at an 1800px editor a dropdown could be " + juce::String(share, 1)
                      + "px, capped by choiceWidth to " + juce::String(width, 1));
        }

        check("FxCard_DoomAndLucyRowsHoldTheirShapeAtAnyWidth", wrapped.isEmpty(),
              wrapped.isEmpty() ? "6 toggles stay 3-across over 2 lines, and 3 dropdowns stay "
                                  "on 1 line, at 1100/1320/1800px"
                                : "reflowed: " + wrapped.joinIntoString(", "));
    }

    // ---- integration: state, presets, ordering -----------------------------
    {
        PX3SynthAudioProcessor processor;

        struct FloatParam { const char* id; float value; };
        const std::array<FloatParam, 14> floats { {
            { "doomMix", 0.71f }, { "doomClock", 0.33f }, { "doomLoopLength", 0.62f },
            { "doomLoopModify", 0.19f }, { "doomOverdub", 0.44f }, { "doomFade", 0.27f },
            { "doomWetTime", 0.83f }, { "doomWetModify", 0.56f }, { "doomCross", 0.38f },
            { "doomGlue", 0.91f }, { "doomEq", -0.64f }, { "doomBalance", 0.22f },
            { "doomBlend", 0.77f }, { "doomSpread", 0.11f },
        } };

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

        juce::StringArray missing;
        for (const auto& f : floats)
        {
            if (auto* param = findParam(f.id))
            {
                param->setValueNotifyingHost(param->convertTo0to1(f.value));
            }
            else
            {
                missing.add(f.id);
            }
        }

        const std::array<const char*, 6> bools {
            { "doomEnabled", "doomFreeze", "doomLoopActive", "doomWetActive",
              "doomLoopHalf", "doomClockSmooth" }
        };
        for (const auto* id : bools)
        {
            if (auto* param = findParam(id))
            {
                param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
            }
            else
            {
                missing.add(id);
            }
        }

        const std::array<const char*, 4> choices {
            { "doomRouting", "doomLoopMode", "doomWetMode", "doomCrossSource" }
        };
        for (const auto* id : choices)
        {
            if (auto* param = findParam(id))
            {
                param->setValueNotifyingHost(1.0f);
            }
            else
            {
                missing.add(id);
            }
        }

        check("Doom_AllParametersExist", missing.isEmpty(),
              missing.isEmpty() ? "24 DOOM parameters registered"
                                : "missing " + missing.joinIntoString(", "));

        // Non-default order that includes DOOM somewhere other than its slot.
        auto order = px3::kDefaultFxOrder;
        std::rotate(order.begin(), order.begin() + 3, order.end());
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
                    ranged != nullptr && ranged->paramID.startsWithIgnoreCase("doom"))
                {
                    drifted.add(ranged->paramID);
                }
            }
            ++index;
        }

        check("Doom_EveryParameterRoundTripsThroughDawState", drifted.isEmpty(),
              drifted.isEmpty() ? "24 parameters restored exactly"
                                : "drifted: " + drifted.joinIntoString(", "));

        check("Doom_FxOrderIncludingDoomSurvivesState",
              restored.getFxProcessingOrder() == order, "");
    }

    {
        // DOOM must actually be IN the chain, and its position must matter.
        auto renderWithOrder = [](const px3::FxOrder& order)
        {
            PX3SynthAudioProcessor processor;
            for (auto* param : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    if (ranged->paramID == "doomMix")       ranged->setValueNotifyingHost(0.8f);
                    if (ranged->paramID == "doomWetTime")   ranged->setValueNotifyingHost(0.6f);
                    if (ranged->paramID == "delayMix" || ranged->paramID == "delayAmount")
                        ranged->setValueNotifyingHost(0.6f);
                    if (ranged->paramID == "reverbAmount")  ranged->setValueNotifyingHost(0.6f);
                }
            }
            processor.setFxProcessingOrder(order);
            return render(processor, 48000, { { 2000, true, 60, 0.9f } });
        };

        auto doomFirst = px3::kDefaultFxOrder;
        auto doomLast = px3::kDefaultFxOrder;

        // Move DOOM to the very front, and to the very back.
        auto moveTo = [](px3::FxOrder order, int stage, int position)
        {
            std::vector<int> v(order.begin(), order.end());
            const auto it = std::find(v.begin(), v.end(), stage);
            const auto from = static_cast<int>(std::distance(v.begin(), it));
            const auto moved = v[static_cast<std::size_t>(from)];
            v.erase(v.begin() + from);
            v.insert(v.begin() + position, moved);
            std::copy(v.begin(), v.end(), order.begin());
            return order;
        };

        doomFirst = moveTo(doomFirst, px3::fxStageDoom, 0);
        doomLast = moveTo(doomLast, px3::fxStageDoom, px3::kFxStageCount - 1);

        const auto a = renderWithOrder(doomFirst);
        const auto b = renderWithOrder(doomLast);

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

        check("Doom_PositionInTheChainChangesTheAudio", differs,
              "DOOM first rms " + juce::String(a.rms(), 5) + ", DOOM last "
                  + juce::String(b.rms(), 5));
    }
}

// ============================================================================
// LUCY
// ============================================================================

namespace lucytest
{
using doomtest::Result;

enum class Source { silence, impulse, sine, saw, chord, pluck, noise };

Result runLucy(const LucySettings& settings,
               Source source,
               double seconds,
               double sampleRate = kSampleRate,
               float frequency = 220.0f,
               uint32_t seed = 24680u)
{
    px3::Lucy lucy;
    lucy.prepare(sampleRate);
    lucy.setSeed(seed);
    lucy.updateForBlock(settings);

    const auto total = static_cast<int>(sampleRate * seconds);
    Result result;
    result.left.reserve(static_cast<std::size_t>(total));
    result.right.reserve(static_cast<std::size_t>(total));

    auto phase = 0.0;
    auto phase3 = 0.0;
    auto phase5 = 0.0;
    const auto increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int i = 0; i < total; ++i)
    {
        auto in = 0.0f;
        switch (source)
        {
            case Source::silence:
                break;
            case Source::impulse:
                // Past the parameter smoothing ramps, so the dry path is not
                // still fading in when it arrives.
                in = i == 4096 ? 1.0f : 0.0f;
                break;
            case Source::sine:
                in = 0.5f * static_cast<float>(std::sin(phase));
                break;
            case Source::saw:
            {
                // Band-limited enough for a test: a raw ramp would alias, and
                // the measurement would be of the source rather than of LUCY.
                auto sum = 0.0;
                for (int h = 1; h <= 12; ++h)
                {
                    sum += std::sin(phase * h) / h;
                }
                in = 0.28f * static_cast<float>(sum);
                break;
            }
            case Source::chord:
                in = 0.2f * static_cast<float>(std::sin(phase) + std::sin(phase3) + std::sin(phase5));
                break;
            case Source::pluck:
            {
                const auto age = static_cast<double>(i % static_cast<int>(sampleRate * 0.5));
                const auto env = std::exp(-age / (sampleRate * 0.08));
                in = 0.6f * static_cast<float>(env * std::sin(phase));
                break;
            }
            case Source::noise:
                // Unsigned: signed overflow is undefined, and this multiply
                // overflows int by design - UBSan flagged it in both copies.
                in = 0.3f * (static_cast<float>((static_cast<uint32_t>(i) * 1103515245u + 12345u) & 0xFFFFu)
                             / 32768.0f - 1.0f);
                break;
        }

        phase += increment;
        phase3 += increment * 1.2599;   // minor third
        phase5 += increment * 1.4983;   // fifth

        float outL = 0.0f;
        float outR = 0.0f;
        lucy.processSampleFrame(in, in, outL, outR);
        result.left.push_back(outL);
        result.right.push_back(outR);
    }

    return result;
}

LucySettings audible()
{
    LucySettings s;
    s.enabled = true;
    s.global = 1.0f;
    return s;
}

// Energy above roughly a quarter of Nyquist as a fraction of the total. The
// documented difference between STANDARD (darker) and INVERSE (brighter) is a
// spectral tilt, so it has to be measured as one rather than as a level.
double brightness(const Result& r)
{
    auto hf = 0.0;
    auto total = 0.0;
    for (std::size_t i = 1; i < r.left.size(); ++i)
    {
        const auto d = r.left[i] - r.left[i - 1];
        hf += static_cast<double>(d) * d;
        total += static_cast<double>(r.left[i]) * r.left[i];
    }
    return hf / juce::jmax(1.0e-12, total);
}
} // namespace lucytest

void testLucy()
{
    suite("LUCY");

    using namespace lucytest;

    // ---- construction and defaults -----------------------------------------
    {
        const LucySettings defaults;
        check("Lucy_DefaultsAreValid",
              defaults.enabled
                  && juce::approximatelyEqual(defaults.global, 0.0f)
                  && juce::approximatelyEqual(defaults.filterWidth, 0.0f)
                  && defaults.modeIndex == 0 && defaults.packetIndex == 0
                  && ! defaults.freeze && ! defaults.gate && ! defaults.slow,
              "global and filter width both zero, packets clean");

        px3::Lucy lucy;
        lucy.prepare(kSampleRate);
        lucy.reset();
        float l = 0.0f;
        float r = 0.0f;
        lucy.processSampleFrame(0.0f, 0.0f, l, r);
        check("Lucy_ConstructsAndProcessesWithoutPreparingSettings",
              std::isfinite(l) && std::isfinite(r), "");
    }

    // ---- sample rates ------------------------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            auto s = audible();
            s.loss = 0.6f;
            const auto out = runLucy(s, Source::saw, 1.5, rate);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << juce::String(static_cast<int>(rate)) << " rms "
                   << juce::String(out.rms(), 4) << "  ";
        }
        check("Lucy_RunsAtEverySupportedSampleRate", allFine, detail);
    }

    // ---- silence, impulse, sines, complex material -------------------------
    {
        auto s = audible();
        s.loss = 0.8f;
        s.verb = 0.6f;
        s.filterWidth = 0.5f;

        const auto quiet = runLucy(s, Source::silence, 2.0);
        check("Lucy_SilenceStaysSilent",
              quiet.finite() && quiet.peak() < 1.0e-3f && std::abs(quiet.dc()) < 1.0e-4,
              "peak " + juce::String(quiet.peak(), 8) + ", dc " + juce::String(quiet.dc(), 8));

        const auto impulse = runLucy(s, Source::impulse, 3.0);
        check("Lucy_ImpulseStaysBounded",
              impulse.finite() && impulse.peak() < 4.0f,
              "peak " + juce::String(impulse.peak(), 4));

        juce::String freqDetail;
        auto allFine = true;
        for (const auto hz : { 50.0f, 100.0f, 440.0f, 1000.0f, 5000.0f, 10000.0f })
        {
            const auto out = runLucy(s, Source::sine, 1.5, kSampleRate, hz);
            const auto ok = out.finite() && out.peak() < 4.0f;
            allFine = allFine && ok;
            freqDetail << juce::String(static_cast<int>(hz)) << " " << juce::String(out.peak(), 3) << "  ";
        }
        check("Lucy_SineIsStableAcrossTheBand", allFine, freqDetail);

        juce::String materialDetail;
        auto materialsFine = true;
        const std::array<std::pair<const char*, Source>, 4> materials { {
            { "saw", Source::saw }, { "chord", Source::chord },
            { "pluck", Source::pluck }, { "noise", Source::noise },
        } };
        for (const auto& material : materials)
        {
            const auto out = runLucy(s, material.second, 2.0);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            materialsFine = materialsFine && ok;
            materialDetail << material.first << " " << juce::String(out.rms(), 4) << "  ";
        }
        check("Lucy_HandlesSynthMaterial", materialsFine, materialDetail);
    }

    // ---- LOSS --------------------------------------------------------------
    {
        // At zero loss the spectral path has to be effectively transparent.
        // Hann analysis and synthesis at 75% overlap reconstruct exactly, so a
        // frame nothing touched must come back out unchanged - if this drifts,
        // the engine is colouring before any mode has been chosen.
        auto clean = audible();
        clean.loss = 0.0f;
        clean.autoGain = 0.0f;

        const auto out = runLucy(clean, Source::sine, 1.5, kSampleRate, 440.0f);

        std::vector<float> reference;
        reference.reserve(out.left.size());
        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 440.0 / kSampleRate;
        for (std::size_t i = 0; i < out.left.size(); ++i)
        {
            reference.push_back(0.5f * static_cast<float>(std::sin(phase)));
            phase += increment;
        }

        px3::Lucy probe;
        probe.prepare(kSampleRate);
        probe.updateForBlock(clean);
        const auto latency = static_cast<std::size_t>(probe.wetLatencySamples());

        auto worstError = 0.0f;
        for (auto i = latency + 8192; i < out.left.size(); ++i)
        {
            worstError = juce::jmax(worstError, std::abs(out.left[i] - reference[i - latency]));
        }

        check("Lucy_ZeroLossReconstructsTheInput", worstError < 0.08f,
              "worst deviation from the delayed input " + juce::String(worstError, 5));
    }

    {
        juce::String detail;
        auto allStable = true;
        std::vector<double> rmsByLoss;

        for (const auto loss : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.loss = loss;
            const auto out = runLucy(s, Source::saw, 2.0);
            allStable = allStable && out.finite() && out.peak() < 4.0f;
            rmsByLoss.push_back(out.rms());
            detail << juce::String(loss, 2) << ":" << juce::String(out.rms(), 4) << "  ";
        }
        check("Lucy_LossIsStableAtEveryDepth", allStable, detail);

        // AUTO GAIN exists because the loss modes change loudness by
        // construction. With it up, the level must not run away as loss rises.
        auto maxRatio = 0.0;
        for (const auto rms : rmsByLoss)
        {
            maxRatio = juce::jmax(maxRatio, rms / juce::jmax(1.0e-9, rmsByLoss.front()));
        }
        check("Lucy_AutoGainKeepsLevelRoughlyConstantAcrossLoss", maxRatio < 3.0,
              "loudest/cleanest across the loss sweep = " + juce::String(maxRatio, 3));
    }

    {
        // The documented tilt: STANDARD is darker, INVERSE is what STANDARD
        // threw away and is therefore brighter and thinner.
        auto standard = audible();
        standard.loss = 0.75f;
        standard.modeIndex = 0;
        standard.autoGain = 0.0f;

        auto inverse = standard;
        inverse.modeIndex = 1;

        auto jitter = standard;
        jitter.modeIndex = 2;

        const auto a = runLucy(standard, Source::saw, 2.0);
        const auto b = runLucy(inverse, Source::saw, 2.0);
        const auto c = runLucy(jitter, Source::saw, 2.0);

        check("Lucy_EveryLossModeIsStable",
              a.finite() && b.finite() && c.finite()
                  && a.peak() < 4.0f && b.peak() < 4.0f && c.peak() < 4.0f,
              "standard " + juce::String(a.rms(), 4) + ", inverse " + juce::String(b.rms(), 4)
                  + ", jitter " + juce::String(c.rms(), 4));

        check("Lucy_InverseIsBrighterAndThinnerThanStandard",
              brightness(b) > brightness(a) && b.rms() < a.rms(),
              "standard brightness " + juce::String(brightness(a), 5) + " rms "
                  + juce::String(a.rms(), 5) + ", inverse brightness "
                  + juce::String(brightness(b), 5) + " rms " + juce::String(b.rms(), 5));

        check("Lucy_JitterChangesTheSignal",
              std::abs(c.rms() - a.rms()) > 1.0e-6 || brightness(c) != brightness(a),
              "jitter rms " + juce::String(c.rms(), 5));
    }

    {
        // LOSS controls WHICH frequencies are affected, not only how much: at a
        // low setting only a strip is touched, so the output must stay closer
        // to the unprocessed signal than at a high setting.
        auto narrow = audible();
        narrow.loss = 0.12f;
        narrow.autoGain = 0.0f;

        auto wide = narrow;
        wide.loss = 1.0f;

        auto bypassed = audible();
        bypassed.global = 0.0f;

        const auto a = runLucy(narrow, Source::saw, 2.0);
        const auto b = runLucy(wide, Source::saw, 2.0);
        const auto dry = runLucy(bypassed, Source::saw, 2.0);

        px3::Lucy probe;
        probe.prepare(kSampleRate);
        probe.updateForBlock(narrow);
        const auto latency = static_cast<std::size_t>(probe.wetLatencySamples());

        // Against the DELAYED dry, and normalised by its level. Comparing
        // against the undelayed input measures the latency; comparing raw
        // energy measures how much quieter high loss is. Neither is the
        // question, which is how much of the spectrum was touched.
        auto deviation = [&dry, latency](const Result& r)
        {
            auto sum = 0.0;
            auto reference = 0.0;
            auto count = 0;
            for (std::size_t i = latency; i < r.left.size() && (i - latency) < dry.left.size(); ++i)
            {
                const auto d = r.left[i] - dry.left[i - latency];
                sum += static_cast<double>(d) * d;
                reference += static_cast<double>(dry.left[i - latency]) * dry.left[i - latency];
                ++count;
            }
            juce::ignoreUnused(count);
            return std::sqrt(sum / juce::jmax(1.0e-12, reference));
        };

        check("Lucy_LossWidensTheAffectedBandAsItRises",
              deviation(b) > deviation(a),
              "narrow strip deviates " + juce::String(deviation(a), 5) + ", full spectrum "
                  + juce::String(deviation(b), 5));
    }

    {
        // WEIGHTING chooses which end of the spectrum survives the coder.
        auto dark = audible();
        dark.loss = 0.8f;
        dark.weighting = -1.0f;
        dark.autoGain = 0.0f;

        auto bright = dark;
        bright.weighting = 1.0f;

        const auto a = runLucy(dark, Source::noise, 2.0);
        const auto b = runLucy(bright, Source::noise, 2.0);

        check("Lucy_WeightingChoosesWhichEndSurvives",
              std::abs(brightness(a) - brightness(b)) > 1.0e-6 && a.finite() && b.finite(),
              "dark " + juce::String(brightness(a), 5) + ", bright "
                  + juce::String(brightness(b), 5));
    }

    // ---- SPEED -------------------------------------------------------------
    {
        // One control, three consequences. Slow holds a decision across many
        // frames, which is what spectral smearing IS - so the short-term level
        // must move less than at fast.
        auto slow = audible();
        slow.loss = 0.8f;
        slow.speed = 0.0f;
        slow.packetIndex = 1;

        auto fast = slow;
        fast.speed = 1.0f;

        auto variability = [](const Result& r)
        {
            const auto window = static_cast<std::size_t>(kSampleRate * 0.02);
            std::vector<double> levels;
            for (std::size_t w = 0; w + window < r.left.size(); w += window)
            {
                auto sum = 0.0;
                for (std::size_t i = w; i < w + window; ++i)
                {
                    sum += static_cast<double>(r.left[i]) * r.left[i];
                }
                levels.push_back(std::sqrt(sum / static_cast<double>(window)));
            }
            if (levels.size() < 2)
            {
                return 0.0;
            }
            auto mean = 0.0;
            for (const auto l : levels) { mean += l; }
            mean /= static_cast<double>(levels.size());
            auto variance = 0.0;
            for (const auto l : levels) { variance += (l - mean) * (l - mean); }
            return std::sqrt(variance / static_cast<double>(levels.size())) / juce::jmax(1.0e-9, mean);
        };

        const auto a = runLucy(slow, Source::saw, 3.0);
        const auto b = runLucy(fast, Source::saw, 3.0);

        check("Lucy_SpeedIsStableAtBothExtremes",
              a.finite() && b.finite() && a.peak() < 4.0f && b.peak() < 4.0f, "");

        check("Lucy_SpeedChangesHowFastTheDegradationEvolves",
              std::abs(variability(a) - variability(b)) > 1.0e-3,
              "slow variability " + juce::String(variability(a), 5) + ", fast "
                  + juce::String(variability(b), 5));
    }

    // ---- PACKETS -----------------------------------------------------------
    {
        static const char* packetNames[] = { "CLEAN", "LOSS", "REPEAT" };
        juce::String detail;
        auto allStable = true;
        std::array<Result, 3> byMode;

        for (int mode = 0; mode < 3; ++mode)
        {
            auto s = audible();
            s.loss = 0.8f;
            s.speed = 0.5f;
            s.packetIndex = mode;
            byMode[static_cast<std::size_t>(mode)] = runLucy(s, Source::saw, 3.0);
            const auto& out = byMode[static_cast<std::size_t>(mode)];
            allStable = allStable && out.finite() && out.peak() < 4.0f;
            detail << packetNames[mode] << " " << juce::String(out.rms(), 4) << "  ";
        }
        check("Lucy_EveryPacketModeIsStable", allStable, detail);

        auto differs = [](const Result& a, const Result& b)
        {
            const auto count = std::min(a.left.size(), b.left.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                if (std::abs(a.left[i] - b.left[i]) > 1.0e-4f)
                {
                    return true;
                }
            }
            return false;
        };

        check("Lucy_PacketModesAreGenuinelyDifferent",
              differs(byMode[0], byMode[1]) && differs(byMode[1], byMode[2])
                  && differs(byMode[0], byMode[2]),
              "clean, loss and repeat all produce different audio");

        // PACKET LOSS is drop-outs: there must be quiet stretches CLEAN does
        // not have. That is what separates it from a tremolo, which would be
        // periodic, and from noise, which would have no runs at all.
        auto quietRuns = [](const Result& r)
        {
            const auto window = static_cast<std::size_t>(kSampleRate * 0.01);
            auto runs = 0;
            auto inRun = false;
            for (std::size_t w = 0; w + window < r.left.size(); w += window)
            {
                auto peak = 0.0f;
                for (std::size_t i = w; i < w + window; ++i)
                {
                    peak = juce::jmax(peak, std::abs(r.left[i]));
                }
                const auto quiet = peak < 0.01f;
                if (quiet && ! inRun)
                {
                    ++runs;
                }
                inRun = quiet;
            }
            return runs;
        };

        check("Lucy_PacketLossProducesDropOuts",
              quietRuns(byMode[1]) > quietRuns(byMode[0]),
              "clean has " + juce::String(quietRuns(byMode[0])) + " quiet runs, packet loss "
                  + juce::String(quietRuns(byMode[1])));

        // PACKET REPEAT fills the same gaps with material rather than silence,
        // so it must be less gappy than PACKET LOSS.
        check("Lucy_PacketRepeatFillsTheGapsRatherThanEmptyingThem",
              quietRuns(byMode[2]) <= quietRuns(byMode[1]),
              "packet loss " + juce::String(quietRuns(byMode[1])) + " runs, repeat "
                  + juce::String(quietRuns(byMode[2])));

        auto s = audible();
        s.loss = 0.8f;
        s.packetIndex = 1;
        const auto a = runLucy(s, Source::saw, 1.5, kSampleRate, 220.0f, 111u);
        const auto b = runLucy(s, Source::saw, 1.5, kSampleRate, 220.0f, 111u);
        const auto c = runLucy(s, Source::saw, 1.5, kSampleRate, 220.0f, 222u);

        auto identical = a.left.size() == b.left.size();
        for (std::size_t i = 0; i < a.left.size() && identical; ++i)
        {
            identical = juce::approximatelyEqual(a.left[i], b.left[i]);
        }
        check("Lucy_PacketChainIsDeterministicUnderASeed",
              identical && differs(a, c),
              "same seed is sample-identical; a different seed is not");
    }

    // ---- FREEZE ------------------------------------------------------------
    {
        // A spectral freeze sustains on silence. This is what says it is a
        // freeze rather than a short looper: the frozen sound has no loop point
        // and keeps going indefinitely.
        auto runFreeze = [](bool slushy, bool withPackets, bool withVerb)
        {
            px3::Lucy lucy;
            lucy.prepare(kSampleRate);
            lucy.setSeed(31337u);

            auto s = audible();
            s.loss = 0.5f;
            s.freezeSlushy = slushy;
            s.packetIndex = withPackets ? 2 : 0;
            s.verb = withVerb ? 0.6f : 0.0f;
            lucy.updateForBlock(s);

            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            for (int i = 0; i < static_cast<int>(kSampleRate * 1.5); ++i)
            {
                float l = 0.0f;
                float r = 0.0f;
                const auto in = 0.4f * static_cast<float>(std::sin(phase));
                phase += increment;
                lucy.processSampleFrame(in, in, l, r);
            }

            s.freeze = true;
            lucy.updateForBlock(s);

            Result out;
            const auto length = static_cast<int>(kSampleRate * 2.5);
            for (int i = 0; i < length; ++i)
            {
                float l = 0.0f;
                float r = 0.0f;
                lucy.processSampleFrame(0.0f, 0.0f, l, r);
                out.left.push_back(l);
                out.right.push_back(r);
            }
            return out;
        };

        const auto solid = runFreeze(false, false, false);
        const auto slushy = runFreeze(true, false, false);
        const auto withPackets = runFreeze(false, true, false);
        const auto withVerb = runFreeze(false, false, true);

        check("Lucy_FreezeSustainsOnSilence",
              solid.finite() && solid.tailRms() > 1.0e-4 && solid.peak() < 4.0f,
              "tail rms after 2.5s of silence " + juce::String(solid.tailRms(), 6));

        check("Lucy_SlushyFreezeIsDifferentFromSolid",
              slushy.finite() && std::abs(slushy.tailRms() - solid.tailRms()) > 1.0e-8,
              "solid " + juce::String(solid.tailRms(), 6) + ", slushy "
                  + juce::String(slushy.tailRms(), 6));

        check("Lucy_FreezeCombinesWithPacketsAndVerb",
              withPackets.finite() && withPackets.peak() < 4.0f
                  && withVerb.finite() && withVerb.peak() < 4.0f,
              "packets " + juce::String(withPackets.tailRms(), 5) + ", verb "
                  + juce::String(withVerb.tailRms(), 5));

        auto live = audible();
        live.freeze = true;
        live.freezer = 0.0f;
        auto frozen = live;
        frozen.freezer = 1.0f;

        const auto a = runLucy(live, Source::saw, 2.0);
        const auto b = runLucy(frozen, Source::saw, 2.0);
        auto freezerDiffers = false;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            if (std::abs(a.left[i] - b.left[i]) > 1.0e-4f)
            {
                freezerDiffers = true;
                break;
            }
        }
        check("Lucy_FreezerBalancesLiveAgainstFrozen", freezerDiffers, "");
    }

    // ---- FILTER ------------------------------------------------------------
    {
        auto none = audible();
        none.loss = 0.0f;
        none.autoGain = 0.0f;
        none.filterWidth = 0.0f;

        auto narrow = none;
        narrow.filterWidth = 1.0f;

        const auto open = runLucy(none, Source::noise, 1.5);
        const auto banded = runLucy(narrow, Source::noise, 1.5);

        check("Lucy_FilterNarrowsTheBand",
              std::abs(brightness(banded) - brightness(open)) > 1.0e-4
                  && banded.finite() && open.finite(),
              "open " + juce::String(brightness(open), 5) + ", narrowed "
                  + juce::String(brightness(banded), 5));

        auto slopesFine = true;
        for (int slope = 0; slope < 3; ++slope)
        {
            for (const auto freq : { 0.0f, 0.5f, 1.0f })
            {
                auto s = audible();
                s.loss = 0.0f;
                s.filterWidth = 0.9f;
                s.filterFreq = freq;
                s.slopeIndex = slope;
                const auto out = runLucy(s, Source::noise, 1.0);
                slopesFine = slopesFine && out.finite() && out.peak() < 4.0f;
            }
        }
        check("Lucy_EverySlopeIsStableAcrossTheSweep", slopesFine,
              "6, 24 and 96 dB at minimum, middle and maximum frequency");

        // INVERT is the band taken OUT, so it must be the complement rather
        // than a second filter with a character of its own.
        auto pass = audible();
        pass.loss = 0.0f;
        pass.filterWidth = 0.8f;
        pass.autoGain = 0.0f;
        auto reject = pass;
        reject.filterInvert = true;

        const auto p = runLucy(pass, Source::noise, 1.5);
        const auto rj = runLucy(reject, Source::noise, 1.5);
        check("Lucy_FilterInvertIsTheComplement",
              std::abs(brightness(p) - brightness(rj)) > 1.0e-6 && p.finite() && rj.finite(),
              "band-pass " + juce::String(brightness(p), 5) + ", band-reject "
                  + juce::String(brightness(rj), 5));
    }

    // ---- VERB --------------------------------------------------------------
    {
        auto dry = audible();
        dry.loss = 0.0f;
        dry.verb = 0.0f;

        auto wet = dry;
        wet.verb = 1.0f;
        wet.verbDecay = 1.0f;

        // Measured as the tail after a sustained note stops, which is how a
        // reverb is actually heard. A lone impulse is the wrong stimulus for
        // this one: it quantises inside its own feedback loop by design, so a
        // single spike falls under that floor long before a note's tail does.
        auto runTail = [](const LucySettings& s)
        {
            px3::Lucy lucy;
            lucy.prepare(kSampleRate);
            lucy.updateForBlock(s);

            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            for (int i = 0; i < static_cast<int>(kSampleRate * 1.5); ++i)
            {
                float l = 0.0f;
                float r = 0.0f;
                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;
                lucy.processSampleFrame(in, in, l, r);
            }

            Result out;
            for (int i = 0; i < static_cast<int>(kSampleRate * 1.5); ++i)
            {
                float l = 0.0f;
                float r = 0.0f;
                lucy.processSampleFrame(0.0f, 0.0f, l, r);
                out.left.push_back(l);
                out.right.push_back(r);
            }
            return out;
        };

        const auto a = runTail(dry);
        const auto b = runTail(wet);

        // The LATE tail. The first few milliseconds after the note stops are
        // the transform flushing its last frame, which every configuration has
        // and which dominates an RMS taken over the whole window.
        check("Lucy_VerbAtMaximumDecayStaysBounded",
              b.finite() && b.peak() < 4.0f && b.tailRms() > a.tailRms() * 8.0,
              "dry late tail " + juce::String(a.tailRms(), 8) + ", wet late tail "
                  + juce::String(b.tailRms(), 8));

        // PRE feeds the loss, POST does not. That routing difference is the
        // point of the control, so it has to change the audio.
        auto pre = audible();
        pre.loss = 0.8f;
        pre.verb = 0.7f;
        pre.verbPost = false;
        auto post = pre;
        post.verbPost = true;

        const auto p = runLucy(pre, Source::saw, 2.5);
        const auto q = runLucy(post, Source::saw, 2.5);

        auto differs = false;
        for (std::size_t i = 0; i < p.left.size(); ++i)
        {
            if (std::abs(p.left[i] - q.left[i]) > 1.0e-4f)
            {
                differs = true;
                break;
            }
        }
        check("Lucy_VerbRoutingChangesWhetherTheLossHearsIt",
              differs && p.finite() && q.finite(),
              "pre rms " + juce::String(p.rms(), 5) + ", post " + juce::String(q.rms(), 5));

        auto hostileVerb = audible();
        hostileVerb.loss = 1.0f;
        hostileVerb.packetIndex = 2;
        hostileVerb.verb = 1.0f;
        hostileVerb.verbDecay = 1.0f;
        hostileVerb.freeze = true;
        const auto held = runLucy(hostileVerb, Source::saw, 6.0);
        check("Lucy_VerbWithLossPacketsAndFreezeDoesNotRunAway",
              held.finite() && held.peak() < 4.0f,
              "peak " + juce::String(held.peak(), 4));
    }

    // ---- GATE --------------------------------------------------------------
    {
        juce::String detail;
        auto allStable = true;
        std::vector<double> rmsByCutoff;

        for (const auto cutoff : { 0.0f, 0.25f, 0.5f, 1.0f })
        {
            auto s = audible();
            s.loss = 0.0f;
            s.gate = true;
            s.gateCutoff = cutoff;
            s.autoGain = 0.0f;
            const auto out = runLucy(s, Source::pluck, 2.5);
            allStable = allStable && out.finite() && out.peak() < 4.0f;
            rmsByCutoff.push_back(out.rms());
            detail << juce::String(cutoff, 2) << ":" << juce::String(out.rms(), 4) << "  ";
        }
        check("Lucy_GateIsStableAtEveryCutoff", allStable, detail);

        check("Lucy_HigherGateCutoffPassesLess",
              rmsByCutoff.back() < rmsByCutoff.front(),
              "cutoff 0 rms " + juce::String(rmsByCutoff.front(), 5) + ", cutoff 1 "
                  + juce::String(rmsByCutoff.back(), 5));

        // A gate that chatters at the threshold buzzes rather than sputters, so
        // the hysteresis has to hold. Measured as the worst sample-to-sample
        // step on a signal parked right at the cutoff.
        auto s = audible();
        s.loss = 0.0f;
        s.gate = true;
        s.gateCutoff = 0.5f;
        s.autoGain = 0.0f;
        const auto out = runLucy(s, Source::sine, 2.0, kSampleRate, 220.0f);

        auto worstStep = 0.0f;
        for (auto i = static_cast<std::size_t>(kSampleRate * 0.2); i < out.left.size(); ++i)
        {
            worstStep = juce::jmax(worstStep, std::abs(out.left[i] - out.left[i - 1]));
        }
        check("Lucy_GateDoesNotChatterAtTheThreshold", worstStep < 0.35f,
              "worst step at the cutoff " + juce::String(worstStep, 5));
    }

    // ---- LIMITER -----------------------------------------------------------
    {
        // Fed deliberate overload. The limiter is not decoration here: freeze,
        // reverb feedback and coarse spectral quantisation can each produce
        // peaks the input never had.
        // The overload has to reach the limiter's INPUT. LOSS GAIN sits after
        // it, by design and by the source's own documentation - it is there to
        // compensate for the limiter making things quieter - so pushing gain
        // through that control would be testing the wrong stage.
        auto s = audible();
        s.loss = 1.0f;
        s.verb = 1.0f;
        s.verbDecay = 1.0f;
        s.threshold = 0.2f;
        s.autoGain = 1.0f;
        s.gainDb = 0.0f;

        const auto out = runLucy(s, Source::noise, 3.0);
        check("Lucy_LimiterBoundsDeliberateOverload",
              out.finite() && out.peak() < 0.45f && std::abs(out.dc()) < 0.05,
              "maximum loss, verb and auto gain into a 0.2 threshold -> peak "
                  + juce::String(out.peak(), 4));

        auto loose = s;
        loose.threshold = 1.0f;
        const auto unlimited = runLucy(loose, Source::noise, 3.0);
        check("Lucy_LowerThresholdMeansMoreLimiting",
              out.peak() < unlimited.peak(),
              "threshold 0.2 peak " + juce::String(out.peak(), 4) + ", threshold 1.0 peak "
                  + juce::String(unlimited.peak(), 4));
    }

    // ---- SLOW --------------------------------------------------------------
    {
        auto fast = audible();
        fast.loss = 0.7f;
        fast.slow = false;
        auto slow = fast;
        slow.slow = true;

        const auto a = runLucy(fast, Source::saw, 2.5);
        const auto b = runLucy(slow, Source::saw, 2.5);

        auto differs = false;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            if (std::abs(a.left[i] - b.left[i]) > 1.0e-4f)
            {
                differs = true;
                break;
            }
        }
        check("Lucy_SlowChangesTheTransformAndTheSound",
              differs && b.finite() && b.peak() < 4.0f,
              "fast rms " + juce::String(a.rms(), 5) + ", slow " + juce::String(b.rms(), 5));

        px3::Lucy lucy;
        lucy.prepare(kSampleRate);
        lucy.updateForBlock(fast);
        const auto fastLatency = lucy.wetLatencySamples();
        lucy.updateForBlock(slow);
        const auto slowLatency = lucy.wetLatencySamples();

        // Slow doubles the transform, so it costs exactly one more fast-mode
        // FFT of latency. Asserted as the difference rather than as a total,
        // because the total also carries the jitter line and the limiter's
        // lookahead.
        check("Lucy_SlowCostsMoreLatencyAsDocumented",
              slowLatency - fastLatency == 512 && fastLatency > 512,
              juce::String(fastLatency) + " samples normally, " + juce::String(slowLatency)
                  + " in slow");
    }

    // ---- stereo ------------------------------------------------------------
    {
        // Packets alternate sides, which is documented behaviour and is what
        // makes the stereo movement related rather than two independent effects.
        auto narrow = audible();
        narrow.loss = 0.8f;
        narrow.packetIndex = 1;
        narrow.spread = 0.0f;

        auto wide = narrow;
        wide.spread = 1.0f;
        wide.verb = 0.6f;

        const auto a = runLucy(narrow, Source::saw, 3.0);
        const auto b = runLucy(wide, Source::saw, 3.0);

        check("Lucy_PacketsAlternateSides",
              a.correlation() < 0.999 && a.finite(),
              "channel correlation with independent packet chains "
                  + juce::String(a.correlation(), 4));

        check("Lucy_SpreadWidensTheImage",
              b.correlation() <= a.correlation() + 0.02 && b.finite(),
              "narrow " + juce::String(a.correlation(), 4) + ", wide "
                  + juce::String(b.correlation(), 4));
    }

    // ---- hostile combinations ---------------------------------------------
    {
        struct Hostile { const char* name; LucySettings settings; };

        auto everything = audible();
        everything.loss = 1.0f;
        everything.speed = 1.0f;
        everything.modeIndex = 2;
        everything.packetIndex = 2;
        everything.filterWidth = 1.0f;
        everything.filterFreq = 1.0f;
        everything.slopeIndex = 2;
        everything.verb = 1.0f;
        everything.verbDecay = 1.0f;
        everything.freeze = true;
        everything.freezeSlushy = true;
        everything.freezer = 1.0f;
        everything.gate = true;
        everything.gateCutoff = 1.0f;
        everything.threshold = 0.05f;
        everything.autoGain = 1.0f;
        everything.weighting = 1.0f;
        everything.gainDb = 36.0f;
        everything.spread = 1.0f;
        everything.slow = true;

        auto inverseExtreme = everything;
        inverseExtreme.modeIndex = 1;
        inverseExtreme.slow = false;

        auto standardExtreme = everything;
        standardExtreme.modeIndex = 0;
        standardExtreme.packetIndex = 1;
        standardExtreme.gate = false;

        auto slowSpeed = everything;
        slowSpeed.speed = 0.0f;
        slowSpeed.filterInvert = true;

        const std::array<Hostile, 4> cases { {
            { "everything, jitter, slow", everything },
            { "everything, inverse", inverseExtreme },
            { "everything, standard, packet loss", standardExtreme },
            { "everything, speed 0, band reject", slowSpeed },
        } };

        juce::String detail;
        auto allSurvive = true;
        for (const auto& c : cases)
        {
            const auto out = runLucy(c.settings, Source::noise, 6.0);
            const auto ok = out.finite() && out.peak() < 8.0f && std::abs(out.dc()) < 0.2;
            allSurvive = allSurvive && ok;
            if (! ok)
            {
                detail << c.name << " peak " << juce::String(out.peak(), 3)
                       << " dc " << juce::String(out.dc(), 4) << "  ";
            }
        }
        check("Lucy_HostileCombinationsStayValidAudio", allSurvive,
              detail.isEmpty() ? "all four survive 6s of noise at maximum everything" : detail);
    }

    // ---- automation --------------------------------------------------------
    {
        struct Sweep { const char* name; float LucySettings::* member; };
        const std::array<Sweep, 11> sweeps { {
            { "global", &LucySettings::global },
            { "loss", &LucySettings::loss },
            { "speed", &LucySettings::speed },
            { "filterWidth", &LucySettings::filterWidth },
            { "filterFreq", &LucySettings::filterFreq },
            { "verb", &LucySettings::verb },
            { "verbDecay", &LucySettings::verbDecay },
            { "freezer", &LucySettings::freezer },
            { "gateCutoff", &LucySettings::gateCutoff },
            { "threshold", &LucySettings::threshold },
            { "spread", &LucySettings::spread },
        } };

        juce::String detail;
        auto allSmooth = true;

        for (const auto& sweep : sweeps)
        {
            px3::Lucy lucy;
            lucy.prepare(kSampleRate);
            lucy.setSeed(4321u);

            auto s = audible();
            s.loss = 0.5f;

            const auto total = static_cast<int>(kSampleRate * 3.0);
            const auto blockSize = 64;

            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            auto worstStep = 0.0f;
            auto previous = 0.0f;
            auto valid = true;

            for (int i = 0; i < total; ++i)
            {
                if (i % blockSize == 0)
                {
                    s.*(sweep.member) = static_cast<float>(i) / static_cast<float>(total);
                    lucy.updateForBlock(s);
                }

                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;

                float l = 0.0f;
                float r = 0.0f;
                lucy.processSampleFrame(in, in, l, r);
                valid = valid && std::isfinite(l) && std::isfinite(r) && std::abs(l) < 8.0f;

                if (i > 2000)
                {
                    worstStep = juce::jmax(worstStep, std::abs(l - previous));
                }
                previous = l;
            }

            const auto ok = valid && worstStep < 0.75f;
            allSmooth = allSmooth && ok;
            if (! ok)
            {
                detail << sweep.name << " step " << juce::String(worstStep, 4) << "  ";
            }
        }

        check("Lucy_EveryParameterSweepsWithoutDiscontinuity", allSmooth,
              detail.isEmpty() ? "11 controls swept min to max under audio" : detail);
    }

    // ---- bypass ------------------------------------------------------------
    {
        px3::Lucy lucy;
        lucy.prepare(kSampleRate);

        auto s = audible();
        s.loss = 0.8f;
        s.verb = 0.7f;
        lucy.updateForBlock(s);

        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i)
        {
            float l = 0.0f;
            float r = 0.0f;
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;
            lucy.processSampleFrame(in, in, l, r);
        }

        s.enabled = false;
        lucy.updateForBlock(s);

        auto maxDeviation = 0.0f;
        auto worstStep = 0.0f;
        auto previous = 0.0f;
        const auto length = static_cast<int>(kSampleRate * 0.5);
        for (int i = 0; i < length; ++i)
        {
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;

            float l = 0.0f;
            float r = 0.0f;
            lucy.processSampleFrame(in, in, l, r);

            if (i > 0)
            {
                worstStep = juce::jmax(worstStep, std::abs(l - previous));
            }
            previous = l;

            if (i > static_cast<int>(kSampleRate * 0.1))
            {
                maxDeviation = juce::jmax(maxDeviation, std::abs(l - in));
            }
        }

        check("Lucy_BypassPassesDryWithoutAClick",
              maxDeviation < 1.0e-5f && worstStep < 0.2f,
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

        const std::array<const char*, 24> ids { {
            "lucyEnabled", "lucyFilterInvert", "lucyVerbPost", "lucyFreeze",
            "lucyFreezeSlushy", "lucyGate", "lucySlow", "lucyGlobal", "lucyLoss",
            "lucySpeed", "lucyFilter", "lucyFilterFreq", "lucyVerb", "lucyVerbDecay",
            "lucyFreezer", "lucyGateCutoff", "lucyThreshold", "lucyAutoGain",
            "lucyWeighting", "lucyGain", "lucySpread", "lucyMode", "lucyPackets",
            "lucySlope",
        } };

        juce::StringArray missing;
        for (const auto* id : ids)
        {
            if (auto* param = findParam(id))
            {
                // A value nothing defaults to, so a parameter that failed to
                // save shows up as a difference rather than as a coincidence.
                param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.15f : 0.85f);
            }
            else
            {
                missing.add(id);
            }
        }

        check("Lucy_AllParametersExist", missing.isEmpty(),
              missing.isEmpty() ? "24 LUCY parameters registered"
                                : "missing " + missing.joinIntoString(", "));

        auto order = px3::kDefaultFxOrder;
        std::rotate(order.begin(), order.begin() + 5, order.end());
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
                    ranged != nullptr && ranged->paramID.startsWithIgnoreCase("lucy"))
                {
                    drifted.add(ranged->paramID);
                }
            }
            ++index;
        }

        check("Lucy_EveryParameterRoundTripsThroughDawState", drifted.isEmpty(),
              drifted.isEmpty() ? "24 parameters restored exactly"
                                : "drifted: " + drifted.joinIntoString(", "));

        check("Lucy_FxOrderIncludingLucySurvivesState",
              restored.getFxProcessingOrder() == order, "");
    }

    {
        // LUCY and DOOM in both orders. Position has to change the audio, or
        // the chain is not really a chain.
        auto renderWithOrder = [](const px3::FxOrder& order)
        {
            PX3SynthAudioProcessor processor;
            for (auto* param : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    if (ranged->paramID == "lucyGlobal")   ranged->setValueNotifyingHost(0.7f);
                    if (ranged->paramID == "lucyLoss")     ranged->setValueNotifyingHost(0.7f);
                    if (ranged->paramID == "doomMix")      ranged->setValueNotifyingHost(0.6f);
                    if (ranged->paramID == "reverbAmount") ranged->setValueNotifyingHost(0.6f);
                }
            }
            processor.setFxProcessingOrder(order);
            return render(processor, 48000, { { 2000, true, 60, 0.9f } });
        };

        auto moveTo = [](px3::FxOrder order, int stage, int position)
        {
            std::vector<int> v(order.begin(), order.end());
            const auto it = std::find(v.begin(), v.end(), stage);
            const auto from = static_cast<int>(std::distance(v.begin(), it));
            const auto moved = v[static_cast<std::size_t>(from)];
            v.erase(v.begin() + from);
            v.insert(v.begin() + position, moved);
            std::copy(v.begin(), v.end(), order.begin());
            return order;
        };

        const auto lucyBeforeDoom = moveTo(moveTo(px3::kDefaultFxOrder, px3::fxStageLucy, 0),
                                           px3::fxStageDoom, 1);
        const auto doomBeforeLucy = moveTo(moveTo(px3::kDefaultFxOrder, px3::fxStageDoom, 0),
                                           px3::fxStageLucy, 1);

        const auto a = renderWithOrder(lucyBeforeDoom);
        const auto b = renderWithOrder(doomBeforeLucy);

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

        check("Lucy_PositionRelativeToDoomChangesTheAudio", differs,
              "LUCY->DOOM rms " + juce::String(a.rms(), 5) + ", DOOM->LUCY "
                  + juce::String(b.rms(), 5));
    }
}

} // namespace px3tests
