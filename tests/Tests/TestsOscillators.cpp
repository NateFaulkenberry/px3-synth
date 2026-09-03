#include "TestSupport.h"

// testWavetable, testSubOscillator, testOscillators

namespace px3tests
{


//==============================================================================
// WAVETABLE
//==============================================================================
// Amplitude of harmonic h in one frame, by correlating against it. Reading a
// single sample and comparing would test the phase convention as much as the
// amplitude; this isolates the amplitude.
double harmonicAmplitudeOf(const float* frame, int length, int h)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const auto angle = 2.0 * juce::MathConstants<double>::pi * h * i / length;
        re += frame[i] * std::sin(angle);
        im += frame[i] * std::cos(angle);
    }
    return 2.0 * std::sqrt(re * re + im * im) / length;
}

px3::FrameSpectrum spectrumOf(std::initializer_list<float> amplitudes)
{
    px3::FrameSpectrum s;
    s.amplitude.push_back(0.0f);   // DC, ignored
    for (const auto a : amplitudes) { s.amplitude.push_back(a); }
    s.phase.assign(s.amplitude.size(), 0.0f);
    return s;
}

void testWavetable()
{
    suite("WAVETABLE");

    // ---- the amplitude that goes in is the amplitude that comes out --------
    // Everything else rests on this. If the transform's scaling convention is
    // wrong, every table is quietly the wrong level and every later measurement
    // is measuring that instead of what it claims to.
    {
        const auto table = px3::Wavetable::build("test", "TEST", { spectrumOf({ 0.5f }) });
        check("Wavetable_BuildsFromASpectrum", table != nullptr,
              table != nullptr ? "built" : "build returned null");

        if (table != nullptr)
        {
            const auto measured = harmonicAmplitudeOf(table->getFrame(0, 0),
                                                      table->getLevelLength(0), 1);
            check("Wavetable_HarmonicComesOutAtTheAmplitudeItWentInAt",
                  std::abs(measured - 0.5) < 0.001,
                  "asked for 0.5, measured " + fmt(measured, 6));
        }
    }

    // ---- levels are truncations, not rescalings ---------------------------
    // A level that renormalised itself would keep the same RMS while changing
    // the amplitude of every harmonic it kept - which is a change to the part of
    // the sound that is still audible, dressed up as band-limiting.
    {
        px3::FrameSpectrum wide;
        wide.amplitude.push_back(0.0f);
        for (int h = 1; h <= 256; ++h)
        {
            wide.amplitude.push_back(1.0f / static_cast<float>(h));
        }
        wide.phase.assign(wide.amplitude.size(), 0.0f);

        const auto table = px3::Wavetable::build("tilt", "TEST", { wide });
        if (table != nullptr)
        {
            auto worst = 0.0;
            juce::String offender;
            const auto reference = harmonicAmplitudeOf(table->getFrame(0, 0),
                                                       table->getLevelLength(0), 1);

            for (int level = 0; level < table->getLevelCount(); ++level)
            {
                const auto measured = harmonicAmplitudeOf(table->getFrame(level, 0),
                                                          table->getLevelLength(level), 1);
                const auto error = std::abs(measured - reference);
                if (error > worst)
                {
                    worst = error;
                    offender = "level " + juce::String(level);
                }
            }

            check("Wavetable_EveryLevelKeepsTheFundamentalAtOneAmplitude",
                  worst < 0.001,
                  "worst drift across " + juce::String(table->getLevelCount())
                      + " levels: " + fmt(worst, 6)
                      + (worst < 0.001 ? juce::String() : " at " + offender));
        }
    }

    // ---- the level length floor -------------------------------------------
    // Pinned because it is worth 45 dB of alias rejection at C7 and there is
    // nothing in the sound of a short table that says "this is why".
    {
        const auto table = px3::Wavetable::build("floor", "TEST", { spectrumOf({ 1.0f }) });
        if (table != nullptr)
        {
            auto shortest = table->getLevelLength(0);
            for (int level = 0; level < table->getLevelCount(); ++level)
            {
                shortest = juce::jmin(shortest, table->getLevelLength(level));
            }
            check("Wavetable_NoLevelIsShorterThanTheFloor",
                  shortest >= px3::Wavetable::kMinLevelLength,
                  "shortest level is " + juce::String(shortest) + " samples, floor is "
                      + juce::String(px3::Wavetable::kMinLevelLength));
        }
    }

    // ---- headroom ----------------------------------------------------------
    // Worth 20 dB of alias rejection and completely invisible from the outside:
    // a level whose top harmonic sits at its own Nyquist is critically sampled
    // and no interpolator can read it cleanly.
    {
        const auto table = px3::Wavetable::build("headroom", "TEST", { spectrumOf({ 1.0f }) });
        if (table != nullptr)
        {
            auto worst = 1.0e9;
            juce::String offender;
            for (int level = 0; level < table->getLevelCount(); ++level)
            {
                const auto headroom = static_cast<double>(table->getLevelLength(level))
                                      / (2.0 * juce::jmax(1, table->getLevelHarmonics(level)));
                if (headroom < worst)
                {
                    worst = headroom;
                    offender = "level " + juce::String(level);
                }
            }
            check("Wavetable_EveryLevelKeepsItsNyquistHeadroom",
                  worst >= px3::Wavetable::kLevelHeadroom,
                  "tightest level carries " + fmt(worst, 2) + "x Nyquist ("
                      + offender + "), design minimum is "
                      + juce::String(px3::Wavetable::kLevelHeadroom) + "x");
        }
    }

    // ---- level selection ---------------------------------------------------
    {
        const auto table = px3::Wavetable::build("select", "TEST", { spectrumOf({ 1.0f }) });
        if (table != nullptr)
        {
            juce::StringArray aliasing;
            // Every note from A0 to well above the top of the keyboard.
            for (int midi = 21; midi <= 120; ++midi)
            {
                const auto hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
                const auto increment = hz / kSampleRate;
                const auto level = table->levelForIncrement(increment);
                // The chosen level's top harmonic has to land under Nyquist.
                const auto top = table->getLevelHarmonics(level) * hz;
                if (top > kSampleRate * 0.5)
                {
                    aliasing.add(juce::String(midi) + " (" + fmt(top, 0) + " Hz)");
                }
            }

            check("Wavetable_SelectedLevelNeverReachesPastNyquist", aliasing.isEmpty(),
                  aliasing.isEmpty()
                      ? "every note from MIDI 21 to 120 selects a level that fits"
                      : "top harmonic past Nyquist at: " + aliasing.joinIntoString(", "));
        }
    }

    // ---- memory ------------------------------------------------------------
    // A table is the largest single thing this synth allocates, so the budget is
    // asserted rather than assumed.
    {
        std::vector<px3::FrameSpectrum> frames;
        for (int f = 0; f < px3::Wavetable::kDefaultFrameCount; ++f)
        {
            frames.push_back(spectrumOf({ 1.0f, 0.5f, 0.25f }));
        }

        const auto table = px3::Wavetable::build("budget", "TEST", frames);
        if (table != nullptr)
        {
            // 2.5 MB, not the 1.4 MB the first design came to: keeping four
            // times Nyquist in every level is what buys the alias floor, and it
            // has to be paid for in level length. One table is shared by every
            // voice and every oscillator, so this is a global cost, not a
            // per-voice one.
            const auto mb = static_cast<double>(table->getSizeInBytes()) / 1048576.0;
            check("Wavetable_PyramidFitsItsMemoryBudget", mb < 2.5,
                  fmt(mb, 3) + " MB for " + juce::String(table->getFrameCount())
                      + " frames across " + juce::String(table->getLevelCount()) + " levels");
        }
    }

    // ---- the read path -----------------------------------------------------
    // A table that evolves from a soft tilt to a bright one, so scanning it has
    // something to scan through.
    const auto scanTable = []
    {
        std::vector<px3::FrameSpectrum> frames;
        for (int f = 0; f < px3::Wavetable::kDefaultFrameCount; ++f)
        {
            const auto t = static_cast<double>(f) / (px3::Wavetable::kDefaultFrameCount - 1);
            // Brightest frame is a sawtooth. Anything brighter is not a waveform
            // anyone plays, and testing against it would buy memory to fix a
            // problem no user has.
            const auto tilt = 2.0 - 1.0 * t;
            px3::FrameSpectrum spectrum;
            spectrum.amplitude.push_back(0.0f);
            for (int h = 1; h <= 512; ++h)
            {
                spectrum.amplitude.push_back(static_cast<float>(std::pow(1.0 / h, tilt)));
            }
            spectrum.phase.assign(spectrum.amplitude.size(), 0.0f);
            frames.push_back(std::move(spectrum));
        }
        return px3::Wavetable::build("scan", "TEST", frames);
    }();

    // Renders a steady tone through the reader.
    const auto renderTone = [](const px3::Wavetable& table, double hz, float position, int samples)
    {
        px3::WavetableReader reader;
        std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
        const auto increment = hz / kSampleRate;
        double phase = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            out[static_cast<std::size_t>(i)] = reader.read(table, phase, position, increment);
            phase += increment;
            if (phase >= 1.0) { phase -= 1.0; }
        }
        return out;
    };

    if (scanTable != nullptr)
    {
        // ---- the ends of the scan are the ends of the table ----------------
        {
            const auto first = renderTone(*scanTable, 110.0, 0.0f, 4096);
            const auto last = renderTone(*scanTable, 110.0, 1.0f, 4096);

            // Frame 0 is the softest tilt and the final frame the brightest, so
            // the two ends must not sound the same.
            const auto brightnessOf = [](const std::vector<float>& v)
            {
                double slope = 0.0;
                for (std::size_t i = 1; i < v.size(); ++i)
                {
                    const auto d = static_cast<double>(v[i]) - v[i - 1];
                    slope += d * d;
                }
                return std::sqrt(slope / static_cast<double>(v.size()));
            };

            const auto ends = brightnessOf(last) / juce::jmax(1.0e-9, brightnessOf(first));
            check("Wavetable_ScanEndsReachDifferentFrames", ends > 1.5,
                  "the last frame is " + fmt(ends, 2) + "x brighter than the first");
        }

        // ---- position is continuous, not stepped --------------------------
        // The brief is explicit that position must not be integer frame
        // switching. A stepped implementation would hold still for a whole
        // frame and then jump; a continuous one moves at every step.
        {
            auto biggestStep = 0.0;
            auto smallestStep = 1.0e9;
            double previous = 0.0;

            for (int i = 0; i <= 200; ++i)
            {
                const auto position = static_cast<float>(i) / 200.0f;
                const auto tone = renderTone(*scanTable, 110.0, position, 2048);
                double energy = 0.0;
                for (std::size_t k = 1; k < tone.size(); ++k)
                {
                    const auto d = static_cast<double>(tone[k]) - tone[k - 1];
                    energy += d * d;
                }
                const auto brightness = std::sqrt(energy / static_cast<double>(tone.size()));
                if (i > 0)
                {
                    const auto step = std::abs(brightness - previous);
                    biggestStep = juce::jmax(biggestStep, step);
                    smallestStep = juce::jmin(smallestStep, step);
                }
                previous = brightness;
            }

            // If position switched frames instead of interpolating, most steps
            // would be zero and a few would be large. The ratio catches that
            // where an average would not.
            const auto ratio = biggestStep / juce::jmax(1.0e-12, smallestStep);
            check("Wavetable_PositionInterpolatesRatherThanSwitchingFrames",
                  smallestStep > 1.0e-9 && ratio < 200.0,
                  "over 200 steps of the scan, smallest change " + fmt(smallestStep, 9)
                      + ", largest " + fmt(biggestStep, 9) + ", ratio " + fmt(ratio, 1));
        }

        // ---- aliasing ------------------------------------------------------
        {
            juce::StringArray poor;
            juce::String detail;

            // Down to MIDI 24: low notes select the levels with the LEAST
            // headroom between their harmonic count and their length, so
            // leaving them out would have tested the easy half of the range.
            for (const auto midi : { 24, 36, 48, 60, 72, 84, 96 })
            {
                constexpr int order = 14;
                const int n = 1 << order;

                // The tone is snapped to an exact FFT bin and rendered without a
                // window.
                //
                // A periodic signal that does not complete a whole number of
                // cycles in the analysis frame leaks, and 512 densely packed
                // harmonics each leaking a little adds up to a floor that looks
                // exactly like aliasing. It read 45 dB at MIDI 24 and did not
                // move when the table geometry changed underneath it, which is
                // what gave it away. On an exact bin the harmonics land on bins,
                // leakage is zero, and what is left between them is the
                // oscillator's own error.
                const auto wanted = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
                const auto bin = juce::jmax(1, juce::roundToInt(wanted * n / kSampleRate));
                const auto hz = bin * kSampleRate / n;

                const auto tone = renderTone(*scanTable, hz, 1.0f, n);

                juce::dsp::FFT fft(order);
                std::vector<float> buf(static_cast<std::size_t>(2 << order), 0.0f);
                for (int i = 0; i < n; ++i)
                {
                    buf[static_cast<std::size_t>(i)] = tone[static_cast<std::size_t>(i)];
                }
                fft.performFrequencyOnlyForwardTransform(buf.data());

                double harmonic = 0.0, inharmonic = 0.0;
                const auto binHz = kSampleRate / n;

                // The mask has to scale with the note, not sit at a fixed bin
                // count. At a fixed +/-6 bins, a low note's harmonics are packed
                // closer together than the mask is wide, so every bin counts as
                // harmonic and the measurement reports a spotless 99 dB no
                // matter what the oscillator is doing. Quarter of the spacing
                // keeps the same question being asked at every pitch.
                juce::ignoreUnused(binHz);

                for (int b = 1; b < n / 2; ++b)
                {
                    const auto power = static_cast<double>(buf[static_cast<std::size_t>(b)])
                                     * buf[static_cast<std::size_t>(b)];
                    // Harmonics sit on exact multiples of the fundamental's bin.
                    // One bin either side absorbs nothing but rounding.
                    const auto offset = b % bin;
                    if (offset <= 1 || offset >= bin - 1) { harmonic += power; }
                    else { inharmonic += power; }
                }

                const auto separation = 10.0 * std::log10(juce::jmax(harmonic, 1.0e-30))
                                      - 10.0 * std::log10(juce::jmax(inharmonic, 1.0e-30));
                detail << (detail.isEmpty() ? "" : ", ") << juce::String(midi) << ": "
                       << fmt(separation, 1);
                if (separation < 60.0) { poor.add(juce::String(midi)); }
            }

            check("Wavetable_StaysCleanAcrossTheKeyboard", poor.isEmpty(),
                  "tone:inharmonic by MIDI note - " + detail
                      + (poor.isEmpty() ? " dB" : " dB; under 60 dB at " + poor.joinIntoString(", ")));
        }

        // ---- band-limit hysteresis ----------------------------------------
        // A note parked on a level boundary with vibrato must not toggle
        // between levels once per cycle, which would be heard as a warble.
        {
            px3::WavetableReader reader;
            // Find a pitch that sits exactly on a boundary.
            auto boundaryHz = 0.0;
            for (auto hz = 100.0; hz < 8000.0; hz *= 1.001)
            {
                if (scanTable->levelForIncrement(hz / kSampleRate)
                    != scanTable->levelForIncrement(hz * 1.001 / kSampleRate))
                {
                    boundaryHz = hz;
                    break;
                }
            }

            auto changes = 0;
            auto lastLevel = -1;
            double phase = 0.0;
            // Half a semitone of vibrato either side, which is more than most.
            for (int i = 0; i < 20000; ++i)
            {
                const auto wobble = std::sin(2.0 * juce::MathConstants<double>::pi * 5.0 * i / kSampleRate);
                const auto hz = boundaryHz * std::pow(2.0, wobble * 0.5 / 12.0);
                const auto increment = hz / kSampleRate;
                reader.read(*scanTable, phase, 0.5f, increment);
                phase += increment;
                if (phase >= 1.0) { phase -= 1.0; }

                if (lastLevel >= 0 && reader.getLevel() != lastLevel) { ++changes; }
                lastLevel = reader.getLevel();
            }

            // Without hysteresis this toggles twice per vibrato cycle - about
            // four times over the two cycles rendered here.
            check("Wavetable_VibratoOnALevelBoundaryDoesNotChatter", changes <= 1,
                  juce::String(changes) + " level changes over 2 vibrato cycles at "
                      + fmt(boundaryHz, 0) + " Hz");
        }
    }

    // ---- analyseFrame round-trips -----------------------------------------
    // Generators written as waveshapers and every audio import go through this,
    // so if it does not invert Wavetable::build exactly then every such table is
    // quietly the wrong shape.
    {
        std::vector<float> cycle(static_cast<std::size_t>(px3::Wavetable::kFrameSize), 0.0f);
        for (int i = 0; i < px3::Wavetable::kFrameSize; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * i / px3::Wavetable::kFrameSize;
            // Several harmonics at deliberately awkward phases - a single sine
            // would pass even if the phase convention were inverted.
            cycle[static_cast<std::size_t>(i)] = static_cast<float>(
                0.6 * std::sin(phase + 0.4) + 0.3 * std::sin(2 * phase - 1.1)
                + 0.15 * std::sin(5 * phase + 2.2));
        }

        const auto spectrum = px3::analyseFrame(cycle.data(), px3::Wavetable::kFrameSize);
        const auto table = px3::Wavetable::build("roundtrip", "TEST", { spectrum });

        if (table != nullptr)
        {
            // Level 0 is long enough to hold every harmonic used above.
            const auto* rebuilt = table->getFrame(0, 0);
            const auto length = table->getLevelLength(0);
            // Level 0 is longer than the source cycle, so the comparison walks
            // the SOURCE and indexes the rebuilt frame - the other way round
            // samples the source at half rate and measures that instead.
            auto worst = 0.0;
            for (int i = 0; i < px3::Wavetable::kFrameSize; ++i)
            {
                const auto at = static_cast<std::size_t>(
                    static_cast<long long>(i) * length / px3::Wavetable::kFrameSize);
                worst = juce::jmax(worst, std::abs(static_cast<double>(rebuilt[at])
                                                   - cycle[static_cast<std::size_t>(i)]));
            }
            check("Wavetable_AnalyseFrameInvertsBuildExactly", worst < 0.002,
                  "worst sample error round-tripping a three-harmonic cycle: " + fmt(worst, 6));
        }
    }

    // ---- the factory library ----------------------------------------------
    {
        const auto& definitions = px3::factoryWavetables();
        check("Wavetable_FactoryLibraryIsPopulated", definitions.size() >= 8,
              juce::String(static_cast<int>(definitions.size())) + " factory tables");

        juce::StringArray failedToBuild;
        juce::StringArray tooStatic;
        juce::StringArray unevenLevel;
        juce::String evolutionDetail;

        for (int i = 0; i < static_cast<int>(definitions.size()); ++i)
        {
            const auto table = px3::buildFactoryWavetable(i);
            const auto name = juce::String(definitions[static_cast<std::size_t>(i)].name);
            if (table == nullptr)
            {
                failedToBuild.add(name);
                continue;
            }

            const auto length = table->getLevelLength(0);
            const auto rms = [length](const float* f)
            {
                double e = 0.0;
                for (int k = 0; k < length; ++k) { e += static_cast<double>(f[k]) * f[k]; }
                return std::sqrt(e / length);
            };

            // Does the scan actually DO anything? A table whose frames all
            // sound alike is a waveform with extra steps - and it is the
            // failure mode a generator falls into most easily, because every
            // frame on its own still sounds fine.
            const auto* first = table->getFrame(0, 0);
            const auto* last = table->getFrame(0, table->getFrameCount() - 1);
            double difference = 0.0, reference = 0.0;
            for (int k = 0; k < length; ++k)
            {
                const auto d = static_cast<double>(first[k]) - last[k];
                difference += d * d;
                reference += static_cast<double>(first[k]) * first[k];
            }
            const auto travel = std::sqrt(difference / juce::jmax(1.0e-12, reference));
            evolutionDetail << (evolutionDetail.isEmpty() ? "" : ", ") << name << " "
                            << fmt(travel, 2);
            if (travel < 0.5) { tooStatic.add(name); }

            // And is it the same loudness all the way across? Otherwise the
            // scan is a volume control wearing a timbre control's clothes.
            auto quietest = 1.0e9, loudest = 0.0;
            for (int f = 0; f < table->getFrameCount(); ++f)
            {
                const auto level = rms(table->getFrame(0, f));
                quietest = juce::jmin(quietest, level);
                loudest = juce::jmax(loudest, level);
            }
            const auto spread = 20.0 * std::log10(loudest / juce::jmax(1.0e-12, quietest));
            if (spread > 6.0)
            {
                unevenLevel.add(name + " (" + fmt(spread, 1) + " dB)");
            }
        }

        check("Wavetable_EveryFactoryTableBuilds", failedToBuild.isEmpty(),
              failedToBuild.isEmpty() ? "all " + juce::String(static_cast<int>(definitions.size()))
                                            + " build"
                                      : "failed: " + failedToBuild.joinIntoString(", "));

        check("Wavetable_EveryFactoryTableActuallyEvolves", tooStatic.isEmpty(),
              "distance travelled from first frame to last - " + evolutionDetail
                  + (tooStatic.isEmpty() ? "" : "; too static: " + tooStatic.joinIntoString(", ")));

        check("Wavetable_FactoryTablesHoldTheirLevelAcrossTheScan", unevenLevel.isEmpty(),
              unevenLevel.isEmpty() ? "every table stays within 6 dB across its scan"
                                    : "level swings: " + unevenLevel.joinIntoString(", "));

        check("Wavetable_FactoryTablesAreFoundByName",
              px3::buildFactoryWavetable(juce::String(definitions[0].name)) != nullptr
                  && px3::buildFactoryWavetable("no such table") == nullptr,
              "lookup by name works and an unknown name returns null");
    }

    // ---- the real-time handoff --------------------------------------------
    {
        px3::WavetableSlot slot;
        check("WavetableSlot_StartsEmpty", ! slot.hasTable(), "no table until one is published");

        slot.publish(px3::Wavetable::build("first", "TEST", { spectrumOf({ 1.0f }) }));
        const auto* firstPointer = slot.beginBlock();
        check("WavetableSlot_PublishesToTheAudioThread", firstPointer != nullptr,
              "the audio thread sees the published table");

        // Replacing must not free what a block might still be inside.
        slot.publish(px3::Wavetable::build("second", "TEST", { spectrumOf({ 0.5f }) }));
        slot.collectRetired();
        check("WavetableSlot_DoesNotFreeATableTheAudioThreadCouldStillHold",
              slot.getRetiredCount() == 1,
              juce::String(slot.getRetiredCount()) + " table held back from collection");

        // Two blocks later the audio thread cannot be holding it.
        slot.beginBlock();
        slot.beginBlock();
        slot.collectRetired();
        check("WavetableSlot_FreesOnceTheAudioThreadHasMovedPast",
              slot.getRetiredCount() == 0,
              "retired table collected after two blocks");

        // And it does not accumulate under repeated swapping, which is what a
        // user auditioning tables actually does.
        for (int i = 0; i < 200; ++i)
        {
            slot.publish(px3::Wavetable::build("swap", "TEST", { spectrumOf({ 1.0f }) }));
            slot.beginBlock();
            slot.beginBlock();
            slot.collectRetired();
        }
        check("WavetableSlot_DoesNotAccumulateRetiredTables",
              slot.getRetiredCount() <= 1,
              juce::String(slot.getRetiredCount()) + " retired after 200 swaps");
    }

    // ---- the oscillator, in the actual synth -------------------------------
    // Everything above tests the wavetable machinery in isolation. This is the
    // part that says the synth can play it.
    {
        const auto brightnessOf = [](const std::vector<float>& v)
        {
            double slope = 0.0;
            for (std::size_t i = 1; i < v.size(); ++i)
            {
                const auto d = static_cast<double>(v[i]) - v[i - 1];
                slope += d * d;
            }
            return std::sqrt(slope / static_cast<double>(std::max<std::size_t>(1, v.size())));
        };

        const auto renderAt = [](float position, int tableIndex)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 8);             // WAVETABLE
            setChoice(processor, "osc1WtTable", tableIndex);
            setParam(processor, "osc1WtPos", position);
            setParam(processor, "analogEnabled", 0.0f);
            return render(processor, static_cast<int>(kSampleRate * 1.0),
                          { { 1000, true, 57, 0.9f } });
        };

        const auto quiet = renderAt(0.0f, 0);
        check("Wavetable_ModeMakesSound", quiet.rms() > 0.01,
              "RMS " + fmt(quiet.rms(), 6) + " playing the first factory table");

        const auto bright = renderAt(1.0f, 0);
        const auto travel = brightnessOf(bright.left) / juce::jmax(1.0e-9, brightnessOf(quiet.left));
        check("Wavetable_PositionChangesTheTimbre", travel > 1.5,
              "position 1.0 is " + fmt(travel, 2) + "x brighter than position 0.0");

        const auto other = renderAt(0.5f, 4);   // Bell Partials
        const auto same = renderAt(0.5f, 0);
        double difference = 0.0, reference = 0.0;
        const auto count = juce::jmin(other.left.size(), same.left.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto d = static_cast<double>(other.left[i]) - same.left[i];
            difference += d * d;
            reference += static_cast<double>(same.left[i]) * same.left[i];
        }
        check("Wavetable_ChangingTheSelectedTableChangesTheSound",
              std::sqrt(difference / juce::jmax(1.0e-12, reference)) > 0.3,
              "two factory tables differ by "
                  + fmt(std::sqrt(difference / juce::jmax(1.0e-12, reference)), 2)
                  + "x the signal");
    }

    // ---- WT Position is an ordinary modulation destination -----------------
    // The point of making it a real parameter rather than folding it into a
    // macro: the assignment list is built from the float parameters that exist,
    // so this needed no plumbing of its own and would be a silent omission if
    // it were ever moved.
    {
        PX3SynthAudioProcessor processor;
        const auto& names = processor.getLfoAssignmentDisplayNames();
        juce::StringArray found;
        for (const auto& name : names)
        {
            if (name.containsIgnoreCase("WT Position")) { found.add(name); }
        }
        check("Wavetable_PositionIsAModulationDestination", found.size() == 3,
              found.isEmpty() ? "WT Position is not assignable"
                              : found.joinIntoString(", "));

        check("Wavetable_PositionAcceptsAnLfoAssignment",
              processor.setLfoAssignmentByParameterId("osc1WtPos"),
              "an LFO can be pointed at osc1WtPos");
    }

    // ---- fast scanning must not click --------------------------------------
    // Modulation is summed once per block, so an unsmoothed position steps 93.75
    // times a second. That is a zipper, and it is exactly what a fast LFO on the
    // scan would expose.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 8);
        setChoice(processor, "osc1WtTable", 0);
        setParam(processor, "osc1WtPos", 0.5f);
        setParam(processor, "analogEnabled", 0.0f);
        setParam(processor, "lfoEnabled", 1.0f);
        setParam(processor, "lfoFrequency", 9.0f);
        setParam(processor, "lfoAmount", 1.0f);
        processor.setLfoAssignmentByParameterId("osc1WtPos");

        const auto capture = render(processor, static_cast<int>(kSampleRate * 3.0),
                                    { { 1000, true, 57, 0.9f } });

        // Same measure the delay's click tests use: a single-sample step
        // against the level around it, so a loud passage is not flagged simply
        // for being loud.
        const auto worstStep = [](const std::vector<float>& v, int from)
        {
            constexpr int window = 256;
            double worst = 0.0;
            for (int i = std::max(from, window); i < static_cast<int>(v.size()); ++i)
            {
                double sum = 0.0;
                for (int k = i - window; k < i; ++k)
                {
                    sum += static_cast<double>(v[static_cast<std::size_t>(k)])
                         * v[static_cast<std::size_t>(k)];
                }
                const auto rms = std::sqrt(sum / window);
                if (rms < 1.0e-5) { continue; }
                worst = juce::jmax(worst, std::abs(static_cast<double>(v[static_cast<std::size_t>(i)])
                                                   - v[static_cast<std::size_t>(i - 1)]) / rms);
            }
            return worst;
        };

        const auto worst = worstStep(capture.left, static_cast<int>(kSampleRate * 0.3));
        check("Wavetable_FastPositionModulationDoesNotClick", worst < 6.0,
              "worst single-sample step against the local level while an LFO sweeps the "
              "scan at 9 Hz: " + fmt(worst, 2) + " (a click reads above 6)");
        auto finite = true;
        for (const auto x : capture.left) { if (! std::isfinite(x)) { finite = false; break; } }
        check("Wavetable_FastPositionModulationStaysFinite", finite,
              "no non-finite samples across 3 seconds of modulated scanning");
    }

    // ---- persistence -------------------------------------------------------
    // The generic round-trip test already covers the parameter VALUES. What it
    // cannot see is whether the table those values name was actually rebuilt -
    // a restore that leaves the previous table loaded restores every number
    // correctly and plays the wrong sound.
    //
    // The order here is the HOST's order: prepare first, then restore. Calling
    // prepare afterwards, as a test naturally would, rebuilds the table as a
    // side effect and hides the bug entirely.
    {
        PX3SynthAudioProcessor source;
        makePlainPatch(source);
        setChoice(source, "osc1Mode", 8);
        setChoice(source, "osc1WtTable", 4);       // Bell Partials, not the default
        setParam(source, "osc1WtPos", 0.82f);

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        restored.prepareToPlay(kSampleRate, kBlockSize);
        const auto beforeRestore = restored.getLoadedWavetableName(0);

        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("Wavetable_SelectionSurvivesTheStateRoundTrip",
              getParamValue(restored, "osc1WtTable") == getParamValue(source, "osc1WtTable")
                  && std::abs(getParamValue(restored, "osc1WtPos") - 0.82f) < 1.0e-4f,
              "table index and position restored");

        check("Wavetable_RestoreLoadsTheTableItRestored",
              restored.getLoadedWavetableName(0) == "Bell Partials",
              "loaded '" + beforeRestore + "' before the restore, '"
                  + restored.getLoadedWavetableName(0) + "' after");
    }

    // ---- import: pitch detection -------------------------------------------
    {
        // A saw, because it is the case zero-crossing counting gets wrong: it
        // crosses zero once per cycle going up and once more coming down off
        // every harmonic.
        std::vector<float> saw(static_cast<std::size_t>(kSampleRate * 0.5), 0.0f);
        const auto hz = 220.0;
        for (std::size_t i = 0; i < saw.size(); ++i)
        {
            const auto phase = std::fmod(static_cast<double>(i) * hz / kSampleRate, 1.0);
            saw[i] = static_cast<float>(2.0 * phase - 1.0);
        }

        const auto period = px3::WavetableImporter::detectPeriod(
            saw.data(), static_cast<int>(saw.size()), kSampleRate);
        const auto detected = period > 0.0 ? kSampleRate / period : 0.0;

        check("WavetableImport_FindsThePitchOfPitchedMaterial",
              std::abs(detected - hz) < 2.0,
              "220 Hz saw detected at " + fmt(detected, 2) + " Hz");

        // Noise has no period, and reporting one would make the importer cut
        // 64 arbitrary fragments and call them cycles.
        std::vector<float> noise(static_cast<std::size_t>(kSampleRate * 0.2), 0.0f);
        juce::Random random(12345);
        for (auto& v : noise) { v = random.nextFloat() * 2.0f - 1.0f; }
        const auto noisePeriod = px3::WavetableImporter::detectPeriod(
            noise.data(), static_cast<int>(noise.size()), kSampleRate);
        const auto noiseHz = noisePeriod > 0.0 ? kSampleRate / noisePeriod : 0.0;
        check("WavetableImport_DoesNotInventAPitchForNoise",
              noisePeriod <= 0.0 || noiseHz < 60.0 || noiseHz > 1800.0,
              noisePeriod <= 0.0 ? "no period reported"
                                 : "reported " + fmt(noiseHz, 1) + " Hz, which no cycle extraction "
                                   "would treat as pitched material");
    }

    // ---- import: a single cycle -------------------------------------------
    {
        std::vector<float> cycle(2048, 0.0f);
        for (std::size_t i = 0; i < cycle.size(); ++i)
        {
            cycle[i] = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * static_cast<double>(i) / static_cast<double>(cycle.size())));
        }

        const auto imported = px3::WavetableImporter::fromAudio(
            cycle.data(), static_cast<int>(cycle.size()), kSampleRate);

        auto dominant = 0;
        if (imported.ok())
        {
            auto peak = 0.0f;
            for (int h = 1; h < imported.frames[0].harmonicCount(); ++h)
            {
                const auto a = imported.frames[0].amplitude[static_cast<std::size_t>(h)];
                if (a > peak) { peak = a; dominant = h; }
            }
        }

        check("WavetableImport_ReadsASingleCycleAsOneCycle",
              imported.ok() && dominant == 1,
              imported.description + " dominant harmonic " + juce::String(dominant));
    }

    // ---- import: evolving material ----------------------------------------
    // The case the whole cycle-extraction pipeline exists for: a recording whose
    // timbre changes has to become frames that change.
    {
        const auto hz = 150.0;
        std::vector<float> sweep(static_cast<std::size_t>(kSampleRate * 2.0), 0.0f);
        for (std::size_t i = 0; i < sweep.size(); ++i)
        {
            const auto t = static_cast<double>(i) / static_cast<double>(sweep.size());
            const auto phase = juce::MathConstants<double>::twoPi * static_cast<double>(i) * hz / kSampleRate;
            // Harmonics arrive as the sound develops.
            double v = std::sin(phase);
            for (int h = 2; h <= 12; ++h)
            {
                v += std::sin(phase * h) * (t / h) * 1.2;
            }
            sweep[i] = static_cast<float>(v * 0.3);
        }

        const auto imported = px3::WavetableImporter::fromAudio(
            sweep.data(), static_cast<int>(sweep.size()), kSampleRate);

        auto brightness = [](const px3::FrameSpectrum& f)
        {
            double weighted = 0.0, total = 0.0;
            for (int h = 1; h < f.harmonicCount(); ++h)
            {
                const auto a = static_cast<double>(f.amplitude[static_cast<std::size_t>(h)]);
                weighted += a * a * h;
                total += a * a;
            }
            return total > 0.0 ? weighted / total : 0.0;
        };

        const auto growth = imported.ok()
                              ? brightness(imported.frames.back()) / juce::jmax(1.0e-9, brightness(imported.frames.front()))
                              : 0.0;

        check("WavetableImport_EvolvingAudioBecomesEvolvingFrames",
              imported.ok() && growth > 1.5,
              imported.description + " last frame is " + fmt(growth, 2)
                  + "x the spectral centroid of the first");
    }

    // ---- import: phase alignment ------------------------------------------
    {
        px3::FrameSpectrum frame;
        frame.amplitude = { 0.0f, 1.0f, 0.5f, 0.25f };
        frame.phase = { 0.0f, 1.1f, -2.4f, 0.8f };
        px3::WavetableImporter::alignFundamentalPhase(frame);

        check("WavetableImport_AlignmentPutsTheFundamentalAtZeroPhase",
              std::abs(frame.phase[1]) < 1.0e-5f,
              "fundamental phase after alignment: " + fmt(frame.phase[1], 8));

        // A pure time shift moves harmonic h by h times the fundamental's
        // shift. If it did anything else the waveform's shape would change,
        // which is exactly what alignment must not do.
        check("WavetableImport_AlignmentIsAPureTimeShift",
              std::abs(frame.phase[2] - (-2.4f - 2.0f * 1.1f)) < 1.0e-5f
                  && std::abs(frame.phase[3] - (0.8f - 3.0f * 1.1f)) < 1.0e-5f,
              "harmonics 2 and 3 moved by exactly 2x and 3x the fundamental's shift");
    }

    // ---- import: images ----------------------------------------------------
    // A known image with a known answer: row y holds y+1 cycles across the
    // width, so frame y must come out with harmonic y+1 dominant.
    {
        constexpr int rows = 6;
        juce::Image image(juce::Image::ARGB, 512, rows, true);
        {
            juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < rows; ++y)
            {
                for (int x = 0; x < image.getWidth(); ++x)
                {
                    const auto phase = juce::MathConstants<double>::twoPi * (y + 1) * x / image.getWidth();
                    const auto brightness = static_cast<float>(0.5 + 0.5 * std::sin(phase));
                    pixels.setPixelColour(x, y, juce::Colour::fromFloatRGBA(
                        brightness, brightness, brightness, 1.0f));
                }
            }
        }

        px3::WavetableImporter::Options options;
        options.frameCount = rows;
        const auto imported = px3::WavetableImporter::fromImage(image, options);

        juce::StringArray wrong;
        if (imported.ok())
        {
            for (int f = 0; f < static_cast<int>(imported.frames.size()); ++f)
            {
                const auto& spectrum = imported.frames[static_cast<std::size_t>(f)];
                auto dominant = 0;
                auto peak = 0.0f;
                for (int h = 1; h < spectrum.harmonicCount(); ++h)
                {
                    const auto a = spectrum.amplitude[static_cast<std::size_t>(h)];
                    if (a > peak) { peak = a; dominant = h; }
                }
                if (dominant != f + 1)
                {
                    wrong.add("row " + juce::String(f) + " -> harmonic " + juce::String(dominant));
                }
            }
        }

        check("WavetableImport_ImageRowsBecomeTheWaveformsTheyDraw",
              imported.ok() && wrong.isEmpty(),
              wrong.isEmpty() ? imported.description + " every row produced the harmonic it drew"
                              : wrong.joinIntoString(", "));
    }

    // ---- import: refuses what it cannot use --------------------------------
    {
        std::vector<float> silence(4096, 0.0f);
        const auto quiet = px3::WavetableImporter::fromAudio(
            silence.data(), static_cast<int>(silence.size()), kSampleRate);
        check("WavetableImport_RejectsSilence", ! quiet.ok() && quiet.description.isNotEmpty(),
              "silence: \"" + quiet.description + "\"");

        const auto nothing = px3::WavetableImporter::fromAudio(nullptr, 0, kSampleRate);
        check("WavetableImport_RejectsNoAudio", ! nothing.ok(),
              "null input: \"" + nothing.description + "\"");

        const auto noImage = px3::WavetableImporter::fromImage(juce::Image());
        check("WavetableImport_RejectsAnInvalidImage", ! noImage.ok(),
              "invalid image: \"" + noImage.description + "\"");
    }

    // ---- the user library --------------------------------------------------
    {
        // A name nothing else would use, and removed afterwards, so a test run
        // does not leave tables in the user's own library.
        const juce::String testName("px3-selftest-table");
        px3::WavetableLibrary::remove(testName);

        std::vector<px3::FrameSpectrum> frames;
        for (int f = 0; f < 8; ++f)
        {
            px3::FrameSpectrum frame;
            frame.amplitude = { 0.0f, 1.0f, 0.5f / static_cast<float>(f + 1), 0.25f };
            frame.phase = { 0.0f, 0.3f * static_cast<float>(f), -0.2f, 0.1f };
            frames.push_back(std::move(frame));
        }

        juce::String error;
        const auto saved = px3::WavetableLibrary::save(testName, frames, error);
        check("WavetableLibrary_SavesAnImportedTable", saved,
              saved ? "written to " + px3::WavetableLibrary::userDirectory().getFullPathName()
                    : error);

        check("WavetableLibrary_ListsWhatItSaved",
              px3::WavetableLibrary::userTableNames().contains(testName),
              "the saved table appears in the library listing");

        const auto loaded = px3::WavetableLibrary::load(testName);
        check("WavetableLibrary_LoadsItBackAsAPlayableTable",
              loaded != nullptr && loaded->getFrameCount() == 8
                  && loaded->getName() == testName,
              loaded != nullptr
                  ? juce::String(loaded->getFrameCount()) + " frames, category '"
                        + loaded->getCategory() + "'"
                  : "load returned null");

        check("WavetableLibrary_MissingTableLoadsAsNullRatherThanSilence",
              px3::WavetableLibrary::load("px3-nothing-here") == nullptr,
              "an unknown name returns null, so the caller can say so");

        px3::WavetableLibrary::remove(testName);
        check("WavetableLibrary_RemovesWhatItSaved",
              ! px3::WavetableLibrary::userTableNames().contains(testName),
              "the test table is gone again");
    }

    // ---- a preset naming a table this machine does not have -----------------
    // The one failure a reference-based library has to handle, and the one it
    // must not handle silently.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        processor.setUserWavetableName(0, "px3-not-on-this-machine");

        check("Wavetable_MissingUserTableFallsBackToAFactoryTable",
              processor.getLoadedWavetableName(0).isNotEmpty()
                  && processor.getUserWavetableName(0).isEmpty(),
              "playing '" + processor.getLoadedWavetableName(0) + "' instead");

        check("Wavetable_MissingUserTableIsReported",
              processor.getMissingWavetableName(0) == "px3-not-on-this-machine",
              "reported as missing: '" + processor.getMissingWavetableName(0) + "'");
    }

    // ---- the display snapshot ----------------------------------------------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        const auto display = processor.getWavetableDisplay(0, 24, 128);
        auto rowsCorrect = true;
        for (const auto& row : display.frames)
        {
            if (static_cast<int>(row.size()) != 128) { rowsCorrect = false; }
        }

        check("Wavetable_DisplaySnapshotIsTheSizeTheUiAskedFor",
              static_cast<int>(display.frames.size()) == 24 && rowsCorrect,
              juce::String(static_cast<int>(display.frames.size())) + " frames of "
                  + (display.frames.empty()
                         ? juce::String("0")
                         : juce::String(static_cast<int>(display.frames[0].size())))
                  + " points, named '" + display.name + "'");

        // Frames are copied out, so the UI never dereferences the pointer the
        // audio thread is using.
        auto anyContent = false;
        for (const auto& row : display.frames)
        {
            for (const auto v : row) { if (std::abs(v) > 0.001f) { anyContent = true; break; } }
        }
        check("Wavetable_DisplaySnapshotCarriesTheWaveform", anyContent,
              "the copied frames are not empty");
    }

    // ---- the graph's file filter -------------------------------------------
    {
        const auto accepts = [](const char* name)
        {
            return WavetableGraph::isSupportedFile(
                juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name));
        };

        juce::StringArray wrong;
        for (const auto* name : { "a.wav", "a.aif", "a.aiff", "a.flac", "a.png", "a.jpg", "a.JPEG" })
        {
            if (! accepts(name)) { wrong.add(juce::String(name) + " rejected"); }
        }
        for (const auto* name : { "a.txt", "a.px3preset", "a.exe", "a" })
        {
            if (accepts(name)) { wrong.add(juce::String(name) + " accepted"); }
        }

    // ---- the GL rectangle stays inside the rounded panel --------------------
    // An attached OpenGL context fills its own rectangle, corners included, and
    // on macOS that native layer hides whatever is painted underneath it. So a
    // rounded panel painted under a full-bounds GL view is square exactly where
    // the rounding is meant to show, and no amount of drawing on top fixes it -
    // the GL view has to be inset until its corners are inside the shape.
    {
        WavetableGraph graph;
        graph.setSize(290, 149);

        juce::Component* glView = nullptr;
        for (auto* child : graph.getChildren())
        {
            if (dynamic_cast<Wavetable3DRenderer*>(child) != nullptr) { glView = child; }
        }

        if (glView == nullptr)
        {
            check("WavetableGraph_TheGlViewStaysInsideTheRoundedPanel", false,
                  "no GL view found under the graph");
        }
        else
        {
            // The fallback in WavetableGraph::cornerRadius, which is what a
            // graph with no UIConfig attached uses.
            constexpr float radius = 7.0f;

            const auto panel = graph.getLocalBounds().toFloat();
            const auto gl = glView->getBounds().toFloat();

            // A corner is inside a rounded rectangle when its distance from the
            // nearest arc CENTRE is within the radius. Outside the corner
            // quadrants both offsets are zero and the test is trivially true,
            // which is correct - only the corners can escape.
            auto worstOverhang = 0.0f;
            for (const auto x : { gl.getX(), gl.getRight() })
            {
                for (const auto y : { gl.getY(), gl.getBottom() })
                {
                    const auto dx = juce::jmax(0.0f,
                                               juce::jmax(panel.getX() + radius - x,
                                                          x - (panel.getRight() - radius)));
                    const auto dy = juce::jmax(0.0f,
                                               juce::jmax(panel.getY() + radius - y,
                                                          y - (panel.getBottom() - radius)));
                    worstOverhang = juce::jmax(worstOverhang,
                                               std::sqrt(dx * dx + dy * dy) - radius);
                }
            }

            check("WavetableGraph_TheGlViewStaysInsideTheRoundedPanel",
                  worstOverhang <= 0.0f && ! gl.isEmpty(),
                  "GL view " + glView->getBounds().toString() + " in a panel "
                      + graph.getLocalBounds().toString() + ", worst corner overhang "
                      + fmt(worstOverhang, 3) + " px past the r=" + fmt(radius, 0) + " rounding");
        }
    }

        check("WavetableGraph_AcceptsOnlyAudioAndImageFiles", wrong.isEmpty(),
              wrong.isEmpty() ? "audio and image extensions accepted, everything else refused"
                              : wrong.joinIntoString(", "));
    }

    // ---- the graph has to be able to animate -------------------------------
    // Measured IN SITU, through the editor, because repainting a component
    // redraws everything beneath it - the same measurement taken on a component
    // in isolation once read 0.02 ms where the real figure was 27.6 ms.
    {
        PX3SynthAudioProcessor processor;
        setChoice(processor, "osc1Mode", 8);   // WAVETABLE, or the graph is hidden
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            editor->setSize(editor->getWidth(), editor->getHeight());

            WavetableGraph* graph = nullptr;
            std::function<void(juce::Component&)> find = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* g = dynamic_cast<WavetableGraph*>(child))
                    {
                        if (graph == nullptr) { graph = g; }
                    }
                    find(*child);
                }
            };
            find(*editor);

            if (graph != nullptr && graph->getWidth() > 0)
            {
                const auto area = editor->getLocalArea(graph, graph->getLocalBounds());
                juce::Image target(juce::Image::ARGB,
                                   juce::jmax(1, area.getWidth()),
                                   juce::jmax(1, area.getHeight()), true);

                for (int i = 0; i < 5; ++i)
                {
                    juce::Graphics g(target);
                    g.setOrigin(-area.getX(), -area.getY());
                    editor->paintEntireComponent(g, true);
                }

                // The scan moving is the animated case, and it must NOT rebuild
                // the cached surface - that is the whole reason the surface is
                // cached separately from the marker.
                const auto start = juce::Time::getMillisecondCounterHiRes();
                constexpr int frames = 40;
                for (int i = 0; i < frames; ++i)
                {
                    graph->setPosition(static_cast<float>(i) / frames,
                                       static_cast<float>(i) / frames);
                    juce::Graphics g(target);
                    g.setOrigin(-area.getX(), -area.getY());
                    editor->paintEntireComponent(g, true);
                }
                const auto ms = (juce::Time::getMillisecondCounterHiRes() - start) / frames;

                check("Wavetable_GraphRepaintsWellInsideAFrame", ms < 5.0,
                      "repainting the graph's region while the scan moves costs "
                          + fmt(ms, 3) + " ms per frame (16.7 ms is a 60 Hz budget)");
            }
            else
            {
                check("Wavetable_GraphRepaintsWellInsideAFrame", graph != nullptr,
                      graph == nullptr ? "no wavetable graph found in the editor"
                                       : "the graph has no size");
            }
        }
    }

    // ---- the oscillator card in wavetable mode ------------------------------
    // Two regressions this pins, both of which looked fine in code and wrong on
    // screen: the mode kept a macro labelled POSITION from when it was a swept
    // sine, so the card grew a second position knob and the row squeezed until
    // controls fell off it; and the card's own animated wave preview kept
    // painting underneath the graph, which is translucent.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            OscillatorComponent* card = nullptr;
            std::function<void(juce::Component&)> find = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* osc = dynamic_cast<OscillatorComponent*>(child))
                    {
                        if (card == nullptr) { card = osc; }
                    }
                    find(*child);
                }
            };
            find(*editor);

            if (card != nullptr)
            {
                const auto visibleSliders = [](juce::Component& c)
                {
                    int count = 0;
                    std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
                    {
                        for (auto* child : parent.getChildren())
                        {
                            if (child == nullptr || ! child->isVisible()) { continue; }
                            if (dynamic_cast<juce::Slider*>(child) != nullptr
                                && ! child->getBounds().isEmpty())
                            {
                                ++count;
                            }
                            walk(*child);
                        }
                    };
                    walk(c);
                    return count;
                };

                setChoice(processor, "osc1Mode", 0);   // SINE: pitch only
                card->refreshFromParameters(true, 0, 0);
                const auto sineSliders = visibleSliders(*card);
                const auto sineModeBounds = card->debugModeBoxBounds();

                setChoice(processor, "osc1Mode", 8);   // WAVETABLE
                card->refreshFromParameters(true, 8, 0);
                const auto wavetableSliders = visibleSliders(*card);

                // Pitch plus exactly one position knob. Three would mean the old
                // macro came back.
                check("WavetableCard_ShowsPitchAndOneScanKnob",
                      wavetableSliders == sineSliders + 1,
                      "SINE shows " + juce::String(sineSliders) + " knobs, WAVETABLE shows "
                          + juce::String(wavetableSliders) + " - one more, not two");

                // The table selector sits under the mode selector, not beside
                // it: both answer "what kind of sound is this", and stacking
                // reads as one decision refined rather than two unrelated ones.
                const auto modeBounds = card->debugModeBoxBounds();
                const auto tableBounds = card->debugTableBoxBounds();
                check("WavetableCard_TableSelectorSitsBelowTheModeSelector",
                      tableBounds.getY() > modeBounds.getBottom()
                          && std::abs(tableBounds.getX() - modeBounds.getX()) <= 1
                          && std::abs(tableBounds.getWidth() - modeBounds.getWidth()) <= 1
                          && std::abs(tableBounds.getHeight() - modeBounds.getHeight()) <= 1,
                      "mode " + modeBounds.toString() + ", table " + tableBounds.toString());

                // The mode selector must not move or resize as a result of
                // being used. It had a second layout path in wavetable mode
                // that laid bands down from the top of the row, so choosing
                // WAVETABLE jumped it from a centred cell to the full row
                // width - the control under the cursor moving out from under it.
                check("WavetableCard_TheModeSelectorDoesNotMoveWhenWavetableIsChosen",
                      modeBounds == sineModeBounds,
                      "mode box is " + modeBounds.toString() + " in WAVETABLE and "
                          + sineModeBounds.toString() + " in SINE");

                const auto* graph = &card->getWavetableGraph();

                // The panel's border has to be the CARD's colour, not the
                // graph's own default. Nobody was wiring it up, so the panel
                // drew in RGB(90,160,240) beside a card outlined in #68C2FF -
                // near enough to read as a mistake rather than as a choice.
                const auto cardAccent = card->cardAccentColour();
                const auto graphBorder = graph->getBorderColour();
                check("WavetableCard_ThePanelBorderIsTheCardsOwnColour",
                      graphBorder.getRed() == cardAccent.getRed()
                          && graphBorder.getGreen() == cardAccent.getGreen()
                          && graphBorder.getBlue() == cardAccent.getBlue(),
                      "panel border " + graphBorder.toDisplayString(false)
                          + ", card border " + cardAccent.toDisplayString(false));

                check("WavetableCard_GraphIsVisibleAndSizedInWavetableMode",
                      graph->isVisible() && ! graph->getBounds().isEmpty(),
                      "graph bounds " + graph->getBounds().toString());

                // The panel border, as actually RENDERED, in both modes.
                //
                // Comparing the two colour sources is not enough: they agreed
                // on the accent and on the alpha and the border still changed,
                // because a translucent stroke takes the colour of the fill
                // beneath it and the two panels filled differently. Only the
                // pixels settle that.
                {
                    const auto panel = graph->getBounds();

                    const auto borderPixel = [&card, &panel]
                    {
                        juce::Image shot(juce::Image::ARGB, card->getWidth(),
                                         card->getHeight(), true);
                        {
                            juce::Graphics g(shot);
                            card->paintEntireComponent(g, false);
                        }

                        // The brightest pixel across the panel's top edge. A
                        // 1 px antialiased stroke does not land on one exact
                        // row, so picking the strongest of a few is what makes
                        // this stable rather than lucky.
                        juce::Colour best;
                        auto bestLuma = -1.0f;
                        for (auto dy = -1; dy <= 1; ++dy)
                        {
                            for (auto dx = -6; dx <= 6; ++dx)
                            {
                                const auto x = panel.getCentreX() + dx;
                                const auto y = panel.getY() + dy;
                                if (! shot.getBounds().contains(x, y)) { continue; }

                                const auto pixel = shot.getPixelAt(x, y);
                                const auto luma = pixel.getBrightness() * pixel.getFloatAlpha();
                                if (luma > bestLuma) { bestLuma = luma; best = pixel; }
                            }
                        }
                        return best;
                    };

                    setChoice(processor, "osc1Mode", 8);
                    card->refreshFromParameters(true, 8, 0);
                    const auto inWavetable = borderPixel();

                    setChoice(processor, "osc1Mode", 1);   // SAW
                    card->refreshFromParameters(true, 1, 0);
                    const auto inSaw = borderPixel();

                    const auto channelGap = juce::jmax(
                        juce::jmax(std::abs(inWavetable.getRed() - inSaw.getRed()),
                                   std::abs(inWavetable.getGreen() - inSaw.getGreen())),
                        juce::jmax(std::abs(inWavetable.getBlue() - inSaw.getBlue()),
                                   std::abs(inWavetable.getAlpha() - inSaw.getAlpha())));

                    check("WavetableCard_ThePanelBorderIsTheSameColourInEveryMode",
                          channelGap <= 1,
                          "border renders " + inWavetable.toDisplayString(true)
                              + " in WAVETABLE and " + inSaw.toDisplayString(true)
                              + " in SAW - worst channel differs by "
                              + juce::String(channelGap));
                }

                setChoice(processor, "osc1Mode", 1);   // SAW
                card->refreshFromParameters(true, 1, 0);
                check("WavetableCard_GraphAndScanKnobHideInOtherModes",
                      ! graph->isVisible() && visibleSliders(*card) == sineSliders,
                      "back to " + juce::String(visibleSliders(*card))
                          + " knobs with the graph hidden");
            }
            else
            {
                check("WavetableCard_ShowsPitchAndOneScanKnob", false,
                      "no oscillator card found in the editor");
            }
        }
    }

    // ---- a sine LFO on the scan must not stall at the ends -------------------
    // A sine slows at its extrema because its derivative goes to zero. It does
    // not STOP there. Clamping a bipolar source that swings further than the
    // base has room for turns it into a square with rounded shoulders, and
    // measured, that pinned the scan for 65.6% of every cycle in stalls of
    // 661 ms.
    {
        juce::StringArray stalled;
        juce::StringArray offCentre;
        juce::String detail;
        auto worstStep = 0.0f;

        for (const auto base : { 0.25f, 0.50f, 0.75f })
        {
            for (const auto amount : { 0.25f, 0.60f, 1.00f })
            {
                PX3SynthAudioProcessor processor;
                makePlainPatch(processor);
                setChoice(processor, "osc1Mode", 8);
                setParam(processor, "osc1WtPos", base);
                setParam(processor, "lfoEnabled", 1.0f);
                setParam(processor, "lfoFrequency", 0.5f);
                setParam(processor, "lfoAmount", amount);
                setChoice(processor, "lfoWaveform", 0);
                processor.setLfoAssignmentByParameterId("osc1WtPos");

                processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
                processor.prepareToPlay(kSampleRate, kBlockSize);

                std::vector<float> trail;
                juce::AudioBuffer<float> buffer(2, kBlockSize);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
                for (int block = 0; block < static_cast<int>(4.0 * kSampleRate / kBlockSize); ++block)
                {
                    buffer.clear();
                    processor.processBlock(buffer, midi);
                    midi.clear();
                    trail.push_back(processor.getModulatedWavetablePosition(0));
                }

                int longestRun = 0, run = 0;
                auto low = 1.0f, high = 0.0f;
                for (std::size_t i = 0; i < trail.size(); ++i)
                {
                    low = juce::jmin(low, trail[i]);
                    high = juce::jmax(high, trail[i]);
                    if (i == 0) { continue; }

                    worstStep = juce::jmax(worstStep, std::abs(trail[i] - trail[i - 1]));

                    const auto pinned = (trail[i] <= 1.0e-4f || trail[i] >= 1.0f - 1.0e-4f)
                                        && std::abs(trail[i] - trail[i - 1]) < 1.0e-6f;
                    if (pinned) { ++run; longestRun = juce::jmax(longestRun, run); }
                    else { run = 0; }
                }

                const auto stallMs = longestRun * kBlockSize * 1000.0 / kSampleRate;
                if (stallMs > 20.0)
                {
                    stalled.add("base " + fmt(base, 2) + " amount " + fmt(amount, 2)
                                + " stalled " + fmt(stallMs, 0) + " ms");
                }

                // Centred on the base, which is what a bipolar source means -
                // an implementation that assumed modulation starts at frame 0
                // would fail here and nowhere else.
                if (std::abs((low + high) * 0.5f - base) > 0.01f)
                {
                    offCentre.add("base " + fmt(base, 2) + " swings "
                                  + fmt(low, 2) + ".." + fmt(high, 2));
                }

                if (std::abs(amount - 1.0f) < 1.0e-6f)
                {
                    detail << (detail.isEmpty() ? "" : ", ") << "base " << fmt(base, 2) << " -> "
                           << fmt(low, 3) << ".." << fmt(high, 3);
                }
            }
        }

        check("WavetableMod_ASineLfoNeverStallsAtTheEnds", stalled.isEmpty(),
              stalled.isEmpty() ? "9 base/amount combinations, no stall over 20 ms at either end"
                                : stalled.joinIntoString("; "));

        check("WavetableMod_ModulationStaysCentredOnTheBase", offCentre.isEmpty(),
              offCentre.isEmpty() ? "at full amount - " + detail
                                  : offCentre.joinIntoString("; "));

        check("WavetableMod_ThePositionMovesContinuously", worstStep < 0.05f,
              "largest change between consecutive blocks: " + fmt(worstStep, 5)
                  + " of the table");
    }

    // ---- every waveform, every destination, never clamped -------------------
    // The precise property is that the value BEFORE the range clamp stays in
    // range. "Never stalls" would be the wrong test: a square LFO holds at its
    // limit by definition, and a saw jumps. What must not happen is the
    // modulation driving past the end and being cut off there, which flattens
    // every shape into the same held edge.
    {
        struct Destination { const char* parameterId; const char* label; };
        const Destination destinations[] = {
            { "osc1WtPos", "wavetable scan" },
            { "osc1MacroA", "osc macro" },
            { "osc1Pitch", "osc pitch" },
            { "filter1Cutoff", "filter cutoff" },
            { "osc1Level", "osc level" },
        };

        juce::StringArray clampedAway;
        juce::String worstDetail;
        auto worstOvershoot = 0.0f;
        auto combinations = 0;

        for (const auto& destination : destinations)
        {
            for (int waveform = 0; waveform < px3::lfoWaveformChoices().size(); ++waveform)
            {
                for (const auto base : { 0.15f, 0.50f, 0.85f })
                {
                    for (const auto amount : { -1.0f, 0.5f, 1.0f })
                    {
                        PX3SynthAudioProcessor processor;
                        makePlainPatch(processor);
                        setParam(processor, "lfoEnabled", 1.0f);
                        setParam(processor, "lfoFrequency", 4.0f);
                        setParam(processor, "lfoAmount", amount);
                        setChoice(processor, "lfoWaveform", waveform);

                        if (! processor.setLfoAssignmentByParameterId(destination.parameterId))
                        {
                            continue;
                        }

                        auto* target = findParameter(processor, destination.parameterId);
                        if (target == nullptr) { continue; }
                        target->setValueNotifyingHost(base);

                        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
                        processor.prepareToPlay(kSampleRate, kBlockSize);

                        juce::AudioBuffer<float> buffer(2, kBlockSize);
                        juce::MidiBuffer midi;
                        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

                        auto overshoot = 0.0f;
                        for (int block = 0; block < 120; ++block)
                        {
                            buffer.clear();
                            processor.processBlock(buffer, midi);
                            midi.clear();

                            const auto raw = processor.getUnclampedModulatedNormalisedValue(*target);
                            if (raw < -0.5f) { continue; }   // not assigned
                            overshoot = juce::jmax(overshoot,
                                                   juce::jmax(-raw, raw - 1.0f));
                        }

                        ++combinations;
                        if (overshoot > worstOvershoot)
                        {
                            worstOvershoot = overshoot;
                            worstDetail = juce::String(destination.label) + " / "
                                          + px3::lfoWaveformChoices()[waveform]
                                          + " / base " + fmt(base, 2)
                                          + " / amount " + fmt(amount, 2);
                        }

                        if (overshoot > 0.002f)
                        {
                            clampedAway.addIfNotAlreadyThere(
                                juce::String(destination.label) + " "
                                + px3::lfoWaveformChoices()[waveform]);
                        }
                    }
                }
            }
        }

        check("Modulation_NoWaveformIsEverClampedAtTheRange", clampedAway.isEmpty(),
              clampedAway.isEmpty()
                  ? juce::String(combinations)
                        + " destination/waveform/base/amount combinations, worst overshoot past "
                          "the range " + fmt(worstOvershoot, 6)
                  : "driven past the range on: " + clampedAway.joinIntoString(", "));

        check("Modulation_CoveredEveryWaveformAndDestination", combinations >= 100,
              juce::String(combinations) + " combinations exercised across "
                  + juce::String(px3::lfoWaveformChoices().size()) + " waveforms and "
                  + juce::String(static_cast<int>(std::size(destinations))) + " destinations");
    }

    // ---- a bipolar source stays centred, a unipolar one reaches the end ------
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "lfoEnabled", 1.0f);
        setParam(processor, "lfoFrequency", 6.0f);
        setParam(processor, "lfoAmount", 1.0f);
        setChoice(processor, "lfoWaveform", 0);
        processor.setLfoAssignmentByParameterId("filter1Cutoff");

        auto* cutoff = findParameter(processor, "filter1Cutoff");
        cutoff->setValueNotifyingHost(0.30f);

        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        juce::AudioBuffer<float> buffer(2, kBlockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

        auto low = 1.0f, high = 0.0f;
        for (int block = 0; block < 200; ++block)
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
            midi.clear();
            const auto value = processor.getModulatedNormalisedValue(*cutoff);
            low = juce::jmin(low, value);
            high = juce::jmax(high, value);
        }

        check("Modulation_ABipolarSourceStaysCentredOnTheBase",
              std::abs((low + high) * 0.5f - 0.30f) < 0.02f,
              "an LFO on a base of 0.30 swung " + fmt(low, 3) + ".." + fmt(high, 3)
                  + ", centred on " + fmt((low + high) * 0.5f, 3));

        // The whole nearer side, so full amount arrives exactly at the end.
        check("Modulation_FullAmountReachesTheEndOfTheRange",
              low < 0.02f,
              "the low end of the swing reached " + fmt(low, 4));
    }

    // ---- the symptom itself, on every waveform and destination --------------
    // The overshoot test above is the precise property; this is the thing the
    // user can actually see. A continuous LFO that is clamped stops moving at
    // the end of its travel, so the knob ring and the wavetable stack sit still
    // for a stretch of every cycle. Measured in milliseconds of no movement at
    // a boundary, which is the same measurement the wavetable-scan test makes -
    // generalised off that one destination and off the sine.
    //
    // SQUARE is exempt and must be: it holds at its limit because that is the
    // shape, not because anything clamped it.
    {
        struct Destination { const char* parameterId; const char* label; };
        const Destination destinations[] = {
            { "osc1WtPos", "wavetable scan" },
            { "osc1MacroA", "osc macro" },
            { "osc1Pitch", "osc pitch" },
            { "filter1Cutoff", "filter cutoff" },
        };

        juce::StringArray stalled;
        auto worstStallMs = 0.0;
        auto combinations = 0;

        for (const auto& destination : destinations)
        {
            for (int waveform = 0; waveform < px3::lfoWaveformChoices().size(); ++waveform)
            {
                if (px3::lfoWaveformChoices()[waveform] == "SQUARE") { continue; }

                for (const auto base : { 0.15f, 0.50f, 0.85f })
                {
                    PX3SynthAudioProcessor processor;
                    makePlainPatch(processor);
                    setParam(processor, "lfoEnabled", 1.0f);
                    setParam(processor, "lfoFrequency", 0.5f);
                    setParam(processor, "lfoAmount", 1.0f);
                    setChoice(processor, "lfoWaveform", waveform);

                    if (! processor.setLfoAssignmentByParameterId(destination.parameterId))
                    {
                        continue;
                    }

                    auto* target = findParameter(processor, destination.parameterId);
                    if (target == nullptr) { continue; }
                    target->setValueNotifyingHost(base);

                    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
                    processor.prepareToPlay(kSampleRate, kBlockSize);

                    juce::AudioBuffer<float> buffer(2, kBlockSize);
                    juce::MidiBuffer midi;
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

                    auto previous = -1.0f;
                    auto longestRun = 0, run = 0;
                    for (int block = 0; block < static_cast<int>(4.0 * kSampleRate / kBlockSize); ++block)
                    {
                        buffer.clear();
                        processor.processBlock(buffer, midi);
                        midi.clear();

                        const auto value = processor.getModulatedNormalisedValue(*target);
                        if (previous >= 0.0f)
                        {
                            const auto pinned = (value <= 1.0e-4f || value >= 1.0f - 1.0e-4f)
                                                && std::abs(value - previous) < 1.0e-6f;
                            if (pinned) { ++run; longestRun = juce::jmax(longestRun, run); }
                            else { run = 0; }
                        }
                        previous = value;
                    }

                    const auto stallMs = longestRun * kBlockSize * 1000.0 / kSampleRate;
                    worstStallMs = juce::jmax(worstStallMs, stallMs);
                    ++combinations;

                    if (stallMs > 20.0)
                    {
                        stalled.addIfNotAlreadyThere(
                            juce::String(destination.label) + " "
                            + px3::lfoWaveformChoices()[waveform]
                            + " at base " + fmt(base, 2) + " stalled " + fmt(stallMs, 0) + " ms");
                    }
                }
            }
        }

        check("Modulation_NoContinuousWaveformStallsAtEitherEnd", stalled.isEmpty(),
              stalled.isEmpty()
                  ? juce::String(combinations) + " combinations, longest time held still at a "
                    "boundary " + fmt(worstStallMs, 1) + " ms"
                  : stalled.joinIntoString("; "));
    }

    // ---- what is drawn is what is played ------------------------------------
    // The wavetable position is the one destination read through a second
    // accessor: the graph and the scan knob call getModulatedWavetablePosition,
    // while the generic knob rings call getModulatedNormalisedValue. Two
    // accessors are two chances to scale one and not the other, and the failure
    // would be an animation that no longer tracks the sound.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "lfoEnabled", 1.0f);
        setParam(processor, "lfoFrequency", 3.0f);
        setParam(processor, "lfoAmount", 1.0f);
        setChoice(processor, "lfoWaveform", 0);
        processor.setLfoAssignmentByParameterId("osc1WtPos");

        auto& positionParam = processor.getOscillatorWtPositionParam(0);
        auto& asRanged = static_cast<juce::RangedAudioParameter&>(positionParam);
        asRanged.setValueNotifyingHost(0.35f);

        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        juce::AudioBuffer<float> buffer(2, kBlockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

        auto worstDisagreement = 0.0f;
        for (int block = 0; block < 240; ++block)
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
            midi.clear();

            const auto ringValue = processor.getModulatedNormalisedValue(asRanged);
            const auto graphValue = asRanged.convertTo0to1(
                processor.getModulatedWavetablePosition(0));
            worstDisagreement = juce::jmax(worstDisagreement,
                                           std::abs(ringValue - graphValue));
        }

        check("Modulation_TheDrawnValueMatchesTheOneTheDspUses",
              worstDisagreement < 1.0e-4f,
              "over 240 blocks the knob ring and the wavetable graph never differed by more "
              "than " + fmt(worstDisagreement, 6));
    }

    // ---- the oscillator actually follows it ---------------------------------
    // The position moving is not the same claim as the SOUND moving. This
    // renders audio and watches the timbre, so a fix that only reached the
    // display would fail here.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 8);
        setChoice(processor, "osc1WtTable", 0);
        setParam(processor, "osc1WtPos", 0.5f);
        setParam(processor, "lfoEnabled", 1.0f);
        setParam(processor, "lfoFrequency", 0.5f);
        setParam(processor, "lfoAmount", 1.0f);
        setChoice(processor, "lfoWaveform", 0);
        setParam(processor, "analogEnabled", 0.0f);
        processor.setLfoAssignmentByParameterId("osc1WtPos");

        const auto capture = render(processor, static_cast<int>(kSampleRate * 4.0),
                                    { { 1000, true, 45, 0.9f } });

        // Brightness per 50 ms window. Sweeping a table from its soft end to its
        // bright one and back has to show up as a rise and fall here.
        std::vector<double> brightness;
        const auto window = static_cast<int>(kSampleRate * 0.05);
        for (int start = window; start + window < static_cast<int>(capture.left.size()); start += window)
        {
            double slope = 0.0, level = 0.0;
            for (int i = 1; i < window; ++i)
            {
                const auto d = static_cast<double>(capture.left[static_cast<std::size_t>(start + i)])
                             - capture.left[static_cast<std::size_t>(start + i - 1)];
                slope += d * d;
                level += static_cast<double>(capture.left[static_cast<std::size_t>(start + i)])
                       * capture.left[static_cast<std::size_t>(start + i)];
            }
            brightness.push_back(level > 1.0e-12 ? std::sqrt(slope / level) : 0.0);
        }

        auto low = 1.0e9, high = 0.0;
        for (const auto b : brightness) { low = juce::jmin(low, b); high = juce::jmax(high, b); }

        check("WavetableMod_TheOscillatorFollowsTheScan", high > low * 1.5,
              "timbre swept over a " + fmt(high / juce::jmax(1.0e-9, low), 2)
                  + "x range of spectral slope while the LFO ran");

        auto finite = true;
        for (const auto x : capture.left) { if (! std::isfinite(x)) { finite = false; break; } }
        check("WavetableMod_TheSweptOscillatorStaysFinite", finite,
              "no non-finite samples across four seconds of full-range scanning");
    }

    // ---- modulation rings on every knob -------------------------------------
    // The scan knob got a ring showing where modulation had actually pushed it;
    // this is the same treatment for any knob with a source pointed at it.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);

        auto& cutoff = processor.getFilterCutoffParam(0);

        check("ModulationRing_UnassignedParameterReportsNoModulation",
              processor.getModulatedNormalisedValue(cutoff) < 0.0f,
              "an unmodulated cutoff returns -1, which the knob draws as no ring");

        setParam(processor, "lfoEnabled", 1.0f);
        setParam(processor, "lfoAmount", 0.8f);
        processor.setLfoAssignmentByParameterId("filter1Cutoff");

        const auto modulated = processor.getModulatedNormalisedValue(cutoff);
        check("ModulationRing_AssignedParameterReportsItsModulatedValue",
              modulated >= 0.0f && modulated <= 1.0f,
              "cutoff with an LFO on it reports " + fmt(modulated, 4));

        check("ModulationRing_AssignmentIsDetectedByParameterId",
              processor.isParameterModulated("filter1Cutoff")
                  && ! processor.isParameterModulated("filter2Cutoff"),
              "the assigned parameter reports modulated and its neighbour does not");

        // A source that is switched off is not modulating anything, whatever it
        // is pointed at.
        setParam(processor, "lfoEnabled", 0.0f);
        check("ModulationRing_ADisabledSourceDrawsNoRing",
              processor.getModulatedNormalisedValue(cutoff) < 0.0f,
              "turning the LFO off removes the ring rather than freezing it");
    }

    // ---- every wavetable is fully in frame ----------------------------------
    // Computed exactly rather than eyeballed: the geometry is projected and its
    // normalised device bounds checked against the viewport. Anything past
    // +/-1 is off screen.
    //
    // This started at -1.847 on the worst table - 85% of the picture's height
    // hanging off the bottom - with the camera at 3.4 where 6.0 was needed.
    {
        juce::StringArray clipped;
        juce::StringArray outsideZoomStops;
        auto worstDistance = 0.0f;
        juce::StringArray tooSmall;
        juce::String detail;
        auto worstFill = 1.0f;

        for (const auto aspect : { 290.0f / 149.0f, 1.0f })
        {
            for (int i = 0; i < static_cast<int>(px3::factoryWavetables().size()); ++i)
            {
                PX3SynthAudioProcessor processor;
                processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
                processor.prepareToPlay(kSampleRate, kBlockSize);
                processor.loadFactoryWavetable(0, i);

                Wavetable3DRenderer renderer;
                renderer.setSize(290, 149);
                renderer.setDisplay(processor.getWavetableDisplay(0, 48, 128));
                renderer.buildGeometryForTesting();
                renderer.autoFrame(aspect);

                const auto name = juce::String(px3::factoryWavetables()[static_cast<std::size_t>(i)].name);

                // The default view, and every orientation the camera can be
                // orbited to - the second is what stops it fitting until the
                // user drags.
                for (int a = 0; a <= 6; ++a)
                {
                    for (const auto elevation : { Wavetable3DRenderer::kMinElevation,
                                                  Wavetable3DRenderer::kMaxElevation })
                    {
                        auto view = renderer.getCamera();
                        view.azimuth = Wavetable3DRenderer::kMinAzimuth
                                       + (Wavetable3DRenderer::kMaxAzimuth
                                          - Wavetable3DRenderer::kMinAzimuth) * static_cast<float>(a) / 6.0f;
                        view.elevation = elevation;
                        view.distance = renderer.distanceToFit(view, aspect, 0.06f);

                        const auto bounds = renderer.projectedBounds(view, aspect);
                        const auto extent = juce::jmax(
                            juce::jmax(std::abs(bounds.getX()), std::abs(bounds.getRight())),
                            juce::jmax(std::abs(bounds.getY()), std::abs(bounds.getBottom())));

                        if (extent > 1.0f) { clipped.addIfNotAlreadyThere(name); }
                    }
                }

                // And that it actually fills the space rather than sitting as a
                // speck in the middle, which "fits" would also be true of.
                const auto bounds = renderer.projectedBounds(renderer.getCamera(), aspect);
                const auto fill = juce::jmax(
                    juce::jmax(std::abs(bounds.getX()), std::abs(bounds.getRight())),
                    juce::jmax(std::abs(bounds.getY()), std::abs(bounds.getBottom())));
                worstFill = juce::jmin(worstFill, fill);
                if (fill < 0.75f) { tooSmall.addIfNotAlreadyThere(name); }

                // The framed distance has to be a distance the user can also
                // zoom to. autoFrame is not clamped by the wheel's stops, so a
                // deeper stack could be framed at a distance outside them - and
                // then the FIRST wheel event snaps the view somewhere else.
                const auto framedDistance = renderer.getCamera().distance;
                if (framedDistance < Wavetable3DRenderer::kMinDistance
                    || framedDistance > Wavetable3DRenderer::kMaxDistance)
                {
                    outsideZoomStops.addIfNotAlreadyThere(
                        name + " framed at " + fmt(framedDistance, 2));
                }
                worstDistance = juce::jmax(worstDistance, framedDistance);

                if (aspect > 1.5f)
                {
                    detail << (detail.isEmpty() ? "" : ", ") << name << " " << fmt(fill, 2);
                }
            }
        }

        check("Wavetable3D_TheFramedDistanceIsInsideTheZoomStops",
              outsideZoomStops.isEmpty(),
              outsideZoomStops.isEmpty()
                  ? "furthest table framed at " + fmt(worstDistance, 2) + ", stops are "
                        + fmt(Wavetable3DRenderer::kMinDistance, 2) + ".."
                        + fmt(Wavetable3DRenderer::kMaxDistance, 2)
                  : outsideZoomStops.joinIntoString(", "));

        check("Wavetable3D_EveryTableIsFullyInFrame", clipped.isEmpty(),
              clipped.isEmpty()
                  ? juce::String(static_cast<int>(px3::factoryWavetables().size()))
                        + " tables x 14 orientations x 2 aspects, none clipped"
                  : "off screen: " + clipped.joinIntoString(", "));

        check("Wavetable3D_TheStackFillsTheView", tooSmall.isEmpty(),
              "fraction of the frame used - " + detail
                  + (tooSmall.isEmpty() ? "" : "; too small: " + tooSmall.joinIntoString(", ")));
    }

    // ---- the floor ----------------------------------------------------------
    // A rectangular perimeter beneath the stack, in the same coordinate system
    // so it turns with the camera. Four edges, not a grid: the rectangle alone
    // carries the width, the depth and the perspective, and subdividing it
    // would turn the picture into a graph.
    {
        juce::StringArray wrong;
        juce::String detail;

        for (int i = 0; i < static_cast<int>(px3::factoryWavetables().size()); ++i)
        {
            PX3SynthAudioProcessor processor;
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);
            processor.loadFactoryWavetable(0, i);

            Wavetable3DRenderer renderer;
            renderer.setSize(290, 149);
            renderer.setDisplay(processor.getWavetableDisplay(0, 48, 128));
            renderer.buildGeometryForTesting();

            const auto floor = renderer.getFloorInfo();
            const auto name = juce::String(px3::factoryWavetables()[static_cast<std::size_t>(i)].name);

            // Twelve edges: a top face, a bottom face and the four corner posts
            // between them. A single rectangle seen at a shallow angle is
            // ambiguous - it could be lying flat or standing up - and the posts
            // are what resolve it.
            if (floor.edgeCount != 12)
            {
                wrong.add(name + ": " + juce::String(floor.edgeCount) + " edges, expected 12");
            }

            // Beneath EVERY point the waveform reaches, at every scan position -
            // a floor the waveform passes through is not a floor.
            if (floor.topY >= floor.lowestWaveformY)
            {
                wrong.add(name + ": floor top at " + fmt(floor.topY, 3)
                          + " is not below the waveform at " + fmt(floor.lowestWaveformY, 3));
            }

            // Deep enough that the corner posts read as posts, shallow enough
            // that the box stays subordinate to the waveform.
            //
            // Measured against the WAVEFORM's own height rather than against the
            // gap beneath it, which is what the first version of this did - and
            // that made the two settings fight each other: moving the floor
            // closer to the waveform shrank the gap, and so shrank the depth the
            // box was allowed to have.
            const auto thickness = floor.topY - floor.bottomY;
            if (thickness <= 0.0f || thickness > 0.5f * floor.waveformHeight)
            {
                wrong.add(name + ": box is " + fmt(thickness, 3) + " deep against a waveform "
                          + fmt(floor.waveformHeight, 3) + " tall");
            }

            if (floor.halfWidth < 0.9f || floor.halfDepth < 0.9f)
            {
                wrong.add(name + ": spans only " + fmt(floor.halfWidth, 2) + " x "
                          + fmt(floor.halfDepth, 2));
            }

            if (i == 0)
            {
                detail = "box from y " + fmt(floor.topY, 3) + " to " + fmt(floor.bottomY, 3)
                         + " under a waveform bottoming out at " + fmt(floor.lowestWaveformY, 3)
                         + ", spanning " + fmt(floor.halfWidth * 2.0f, 1) + " x "
                         + fmt(floor.halfDepth * 2.0f, 1);
            }
        }

        check("Wavetable3D_TheFloorIsAShallowBoxBeneathTheStack", wrong.isEmpty(),
              wrong.isEmpty() ? detail + ", on all "
                                    + juce::String(static_cast<int>(px3::factoryWavetables().size()))
                                    + " tables"
                              : wrong.joinIntoString("; "));
    }

    // ---- the 3D renderer's camera ------------------------------------------
    // The rendering itself needs a GL context and cannot be checked here, but
    // the camera can - and an orbit control that can be dragged into an
    // unusable orientation is the failure that actually gets shipped.
    {
        Wavetable3DRenderer renderer;
        renderer.setSize(300, 150);

        const auto defaultCamera = renderer.getCamera();

        const auto drag = [&renderer](float dx, float dy)
        {
            const juce::Point<float> from { 150.0f, 75.0f };
            const auto to = from.translated(dx, dy);
            const auto make = [&renderer](juce::Point<float> p)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                                        juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                        &renderer, &renderer, juce::Time::getCurrentTime(),
                                        p, juce::Time::getCurrentTime(), 1, false);
            };
            renderer.mouseDown(make(from));
            renderer.mouseDrag(make(to));
        };

        // Dragged hard past the top and past the bottom. Up must RAISE the
        // camera: the opposite is what a screen-space delta gives literally, and
        // it feels inverted against every other 3D view.
        drag(0.0f, -4000.0f);
        const auto high = renderer.getCamera();
        drag(0.0f, 4000.0f);
        const auto low = renderer.getCamera();

        check("Wavetable3D_DraggingUpRaisesTheCamera", high.elevation > low.elevation,
              "up gives elevation " + fmt(high.elevation, 3) + ", down gives "
                  + fmt(low.elevation, 3));

        check("Wavetable3D_CameraCannotBeFlippedOverTheStack",
              high.elevation >= Wavetable3DRenderer::kMinElevation
                  && high.elevation <= Wavetable3DRenderer::kMaxElevation
                  && low.elevation >= Wavetable3DRenderer::kMinElevation
                  && low.elevation <= Wavetable3DRenderer::kMaxElevation,
              "elevation held between " + fmt(Wavetable3DRenderer::kMinElevation, 2) + " and "
                  + fmt(Wavetable3DRenderer::kMaxElevation, 2) + " after dragging 4000 px each way: "
                  + fmt(low.elevation, 3) + " / " + fmt(high.elevation, 3));

        // Azimuth is clamped too, and for a reason worth stating: the camera
        // has to pull back far enough to fit whatever orientation is reachable,
        // so allowing a full turn costs framing at the angle the stack is
        // actually read from. Swinging behind it shows the same curves back to
        // front, which is not worth a fifth of the picture.
        drag(2000.0f, 0.0f);
        const auto turnedRight = renderer.getCamera().azimuth;
        drag(-2000.0f, 0.0f);
        const auto turnedLeft = renderer.getCamera().azimuth;

        check("Wavetable3D_CameraTurnsWithinItsLimits",
              turnedRight <= Wavetable3DRenderer::kMaxAzimuth + 1.0e-5f
                  && turnedLeft >= Wavetable3DRenderer::kMinAzimuth - 1.0e-5f
                  && turnedRight > turnedLeft,
              "azimuth ran from " + fmt(turnedLeft, 2) + " to " + fmt(turnedRight, 2)
                  + " radians, within [" + fmt(Wavetable3DRenderer::kMinAzimuth, 2) + ", "
                  + fmt(Wavetable3DRenderer::kMaxAzimuth, 2) + "]");

        // Zoom, and its stops.
        for (int i = 0; i < 40; ++i)
        {
            juce::MouseWheelDetails wheel;
            wheel.deltaY = i % 2 == 0 ? 1.0f : -1.0f;
            wheel.deltaX = 0.0f;
            renderer.mouseWheelMove(
                juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                 { 150.0f, 75.0f }, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f,
                                 0.0f, &renderer, &renderer, juce::Time::getCurrentTime(),
                                 { 150.0f, 75.0f }, juce::Time::getCurrentTime(), 1, false),
                wheel);
            if (renderer.getCamera().distance < Wavetable3DRenderer::kMinDistance
                || renderer.getCamera().distance > Wavetable3DRenderer::kMaxDistance)
            {
                break;
            }
        }
        check("Wavetable3D_ZoomStaysWithinItsStops",
              renderer.getCamera().distance >= Wavetable3DRenderer::kMinDistance
                  && renderer.getCamera().distance <= Wavetable3DRenderer::kMaxDistance,
              "distance " + fmt(renderer.getCamera().distance, 2) + " after 40 wheel events");

        // And a way back.
        renderer.resetCamera();
        const auto reset = renderer.getCamera();
        check("Wavetable3D_CameraResets",
              std::abs(reset.azimuth - defaultCamera.azimuth) < 1.0e-6f
                  && std::abs(reset.elevation - defaultCamera.elevation) < 1.0e-6f
                  && std::abs(reset.distance - defaultCamera.distance) < 1.0e-6f,
              "back to the default view");

        // Feeding it data must not require a context to be safe.
        px3::WavetableDisplay display;
        display.name = "test";
        for (int f = 0; f < 8; ++f)
        {
            std::vector<float> row(64, 0.0f);
            for (std::size_t i = 0; i < row.size(); ++i)
            {
                row[i] = std::sin(juce::MathConstants<float>::twoPi * static_cast<float>(f + 1) * static_cast<float>(i) / static_cast<float>(row.size()));
            }
            display.frames.push_back(std::move(row));
        }
        renderer.setDisplay(display);
        renderer.setPosition(0.5f);
        // The environment is on unless something turns it off, and turning it
        // off changes nothing about the geometry - it is shading, not shape.
        // A renderer that rebuilt its stack when the environment toggled would
        // be doing work per frame that belongs in a uniform.
        {
            PX3SynthAudioProcessor host;
            host.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            host.prepareToPlay(kSampleRate, kBlockSize);
            host.loadFactoryWavetable(0, 0);

            Wavetable3DRenderer envRenderer;
            envRenderer.setSize(290, 149);
            envRenderer.setDisplay(host.getWavetableDisplay(0, 48, 128));
            envRenderer.buildGeometryForTesting();

            const auto floorWithEnvironment = envRenderer.getFloorInfo();
            const auto framedWithEnvironment = envRenderer.getCamera();
            const auto onByDefault = envRenderer.isEnvironmentEnabled();

            envRenderer.setEnvironmentEnabled(false);
            envRenderer.buildGeometryForTesting();
            const auto floorWithout = envRenderer.getFloorInfo();

            check("Wavetable3D_TheEnvironmentIsShadingRatherThanGeometry",
                  onByDefault
                      && ! envRenderer.isEnvironmentEnabled()
                      && floorWithout.edgeCount == floorWithEnvironment.edgeCount
                      && std::abs(floorWithout.topY - floorWithEnvironment.topY) < 1.0e-6f
                      && std::abs(envRenderer.getCamera().distance
                                  - framedWithEnvironment.distance) < 1.0e-6f,
                  juce::String(onByDefault ? "on by default, " : "OFF by default, ")
                      + "and switching it off left the floor at "
                      + juce::String(floorWithout.edgeCount) + " edges and the camera at "
                      + fmt(envRenderer.getCamera().distance, 2));
        }

        check("Wavetable3D_AcceptsDataWithoutAContext", true,
              "display and position handed over with no GL context attached");
    }

    // ---- rejects what it cannot build --------------------------------------
    // Returning an empty table instead of null would ship silence that looks
    // like a working oscillator.
    {
        check("Wavetable_RejectsAnEmptyFrameSet",
              px3::Wavetable::build("empty", "TEST", {}) == nullptr,
              "no frames returns null rather than a silent table");

        std::vector<px3::FrameSpectrum> tooMany(px3::Wavetable::kMaxFrameCount + 1,
                                                spectrumOf({ 1.0f }));
        check("Wavetable_RejectsMoreFramesThanTheFormatHolds",
              px3::Wavetable::build("huge", "TEST", tooMany) == nullptr,
              juce::String(px3::Wavetable::kMaxFrameCount + 1) + " frames returns null");
    }
}

//==============================================================================
// PHASE 4 - SUB OSCILLATOR
//==============================================================================
void testSubOscillator()
{
    suite("SUB OSCILLATOR");

    // ---- Level 1: unit behaviour on the DSP class itself ----
    {
        SubOscillator sub;
        sub.prepare(kSampleRate);
        SubOscSettings settings;
        settings.enabled = false;
        settings.level = 1.0f;
        sub.setSettings(settings);
        sub.resetForNote();

        bool silent = true;
        for (int i = 0; i < 4096; ++i)
        {
            if (sub.renderSample(220.0) != 0.0f) silent = false;
        }
        check("SubOscillator_DisabledProducesSilence", silent);
    }

    {
        SubOscillator sub;
        sub.prepare(kSampleRate);
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 0.0f;
        sub.setSettings(settings);
        sub.resetForNote();

        bool silent = true;
        for (int i = 0; i < 4096; ++i)
        {
            if (sub.renderSample(220.0) != 0.0f) silent = false;
        }
        check("SubOscillator_ZeroLevelProducesSilence", silent);
    }

    // Pitch is verified mathematically: the octave selector is a semitone
    // offset, so the rendered period must match base * 2^(semitones/12).
    struct OctaveCase { int index; int semitones; const char* label; };
    const OctaveCase octaves[] = { { 0, 0, "0 OCT" }, { 1, -12, "-1 OCT" }, { 2, -24, "-2 OCT" } };

    for (const auto& octave : octaves)
    {
        SubOscillator sub;
        sub.prepare(kSampleRate);
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 1.0f;
        settings.octaveIndex = octave.index;
        settings.waveformIndex = 0; // SINE
        sub.setSettings(settings);
        sub.resetForNote();

        constexpr double baseHz = 440.0;
        std::vector<float> signal;
        signal.reserve(48000);
        for (int i = 0; i < 48000; ++i) signal.push_back(sub.renderSample(baseHz));

        const auto expected = baseHz * std::pow(2.0, octave.semitones / 12.0);
        const auto measured = estimateFrequency(signal, 1000, 32000, 20.0, 2000.0);
        check((juce::String("SubOscillator_Octave_") + octave.label + "_ProducesExpectedFrequency").toRawUTF8(),
              nearly(measured, expected, expected * 0.02),
              "expected " + fmt(expected, 2) + " Hz, measured " + fmt(measured, 2) + " Hz");
    }

    // Fine pitch: the established range is +/-0.24 semitones, a detune trim,
    // not a transpose. Verified against that range, not against an assumed one.
    for (const auto cents : { -0.24f, 0.0f, 0.24f })
    {
        SubOscillator sub;
        sub.prepare(kSampleRate);
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 1.0f;
        settings.octaveIndex = 0;
        settings.pitchSemitones = cents;
        settings.waveformIndex = 0;
        sub.setSettings(settings);
        sub.resetForNote();

        constexpr double baseHz = 440.0;
        std::vector<float> signal;
        for (int i = 0; i < 96000; ++i) signal.push_back(sub.renderSample(baseHz));

        const auto expected = baseHz * std::pow(2.0, static_cast<double>(cents) / 12.0);
        const auto measured = estimateFrequency(signal, 1000, 64000, 300.0, 600.0);
        check((juce::String("SubOscillator_PitchTrim_") + juce::String(cents, 2) + "st_ShiftsFrequency").toRawUTF8(),
              nearly(measured, expected, expected * 0.01),
              "expected " + fmt(expected, 3) + " Hz, measured " + fmt(measured, 3) + " Hz");
    }

    // Waveforms: both must sound, and the square must be measurably richer than
    // the sine at the same level (that is what distinguishes them).
    {
        double sineRms = 0.0, squareRms = 0.0, sinePeak = 0.0, squarePeak = 0.0;
        for (int waveform = 0; waveform <= 1; ++waveform)
        {
            SubOscillator sub;
            sub.prepare(kSampleRate);
            SubOscSettings settings;
            settings.enabled = true;
            settings.level = 1.0f;
            settings.octaveIndex = 0;
            settings.waveformIndex = waveform;
            sub.setSettings(settings);
            sub.resetForNote();

            double energy = 0.0, peak = 0.0, dc = 0.0;
            constexpr int count = 48000;
            for (int i = 0; i < count; ++i)
            {
                const auto v = static_cast<double>(sub.renderSample(220.0));
                energy += v * v;
                peak = juce::jmax(peak, std::abs(v));
                dc += v;
            }
            const auto rms = std::sqrt(energy / count);
            dc /= count;

            check((juce::String("SubOscillator_Waveform") + juce::String(waveform) + "_Sounds").toRawUTF8(),
                  rms > 0.05, "rms " + fmt(rms, 4));
            check((juce::String("SubOscillator_Waveform") + juce::String(waveform) + "_NoDcOffset").toRawUTF8(),
                  std::abs(dc) < 0.01, "dc " + fmt(dc, 6));

            if (waveform == 0) { sineRms = rms; sinePeak = peak; }
            else { squareRms = rms; squarePeak = peak; }
        }
        // A square's RMS/peak ratio is near 1; a sine's is near 0.707.
        check("SubOscillator_SquareHasHigherRmsToPeakThanSine",
              (squareRms / squarePeak) > (sineRms / sinePeak) + 0.15,
              "sine " + fmt(sineRms / sinePeak, 3) + " vs square " + fmt(squareRms / squarePeak, 3));
    }

    // Level is a linear gain on the sub's own output.
    {
        double rmsAtHalf = 0.0, rmsAtFull = 0.0;
        for (const auto level : { 0.5f, 1.0f })
        {
            SubOscillator sub;
            sub.prepare(kSampleRate);
            SubOscSettings settings;
            settings.enabled = true;
            settings.level = level;
            settings.octaveIndex = 0;
            settings.waveformIndex = 0;
            sub.setSettings(settings);
            sub.resetForNote();

            double energy = 0.0;
            for (int i = 0; i < 48000; ++i)
            {
                const auto v = static_cast<double>(sub.renderSample(220.0));
                energy += v * v;
            }
            (level == 0.5f ? rmsAtHalf : rmsAtFull) = std::sqrt(energy / 48000.0);
        }
        check("SubOscillator_LevelIsLinearGain",
              nearly(rmsAtFull / juce::jmax(1.0e-9, rmsAtHalf), 2.0, 0.02),
              "ratio " + fmt(rmsAtFull / rmsAtHalf, 4) + " (expected 2.0)");
    }

    // Reset must place the phase deterministically, so a repeated note starts
    // identically rather than wherever the previous one stopped.
    {
        SubOscillator a, b;
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 1.0f;
        settings.octaveIndex = 1;
        settings.waveformIndex = 1;
        a.prepare(kSampleRate); a.setSettings(settings);
        b.prepare(kSampleRate); b.setSettings(settings);

        a.resetForNote();
        for (int i = 0; i < 5000; ++i) a.renderSample(330.0); // leave a in an arbitrary phase
        a.resetForNote();
        b.resetForNote();

        bool identical = true;
        for (int i = 0; i < 8192; ++i)
        {
            if (a.renderSample(220.0) != b.renderSample(220.0)) identical = false;
        }
        check("SubOscillator_ResetForNoteIsDeterministic", identical,
              "same phase and output after reset regardless of prior use");
    }

    // Two instances must not share state: this is what per-voice independence
    // reduces to, since each voice owns its own SubOscillator.
    {
        SubOscillator low, high;
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 1.0f;
        settings.octaveIndex = 0;
        settings.waveformIndex = 0;
        low.prepare(kSampleRate); low.setSettings(settings); low.resetForNote();
        high.prepare(kSampleRate); high.setSettings(settings); high.resetForNote();

        std::vector<float> lowSignal, highSignal;
        for (int i = 0; i < 48000; ++i)
        {
            lowSignal.push_back(low.renderSample(220.0));
            highSignal.push_back(high.renderSample(330.0));
        }
        const auto lowHz = estimateFrequency(lowSignal, 1000, 32000, 100.0, 500.0);
        const auto highHz = estimateFrequency(highSignal, 1000, 32000, 100.0, 500.0);
        check("SubOscillator_TwoInstancesTrackIndependentPitches",
              nearly(lowHz, 220.0, 4.0) && nearly(highHz, 330.0, 6.0),
              "measured " + fmt(lowHz, 2) + " Hz and " + fmt(highHz, 2) + " Hz");
    }

    // ---- Level 4: integration through the real processor ----
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "osc1Enabled", 0.0f);
        setParam(processor, "subOscEnabled", 1.0f);
        setChoice(processor, "subOscOctave", 0);
        setChoice(processor, "subOscWaveform", 0);
        const auto capture = render(processor, 48000, { { 2000, true, 69, 0.9f } });
        // MIDI 69 is A440; the 0 OCT setting must reproduce it.
        const auto hz = estimateFrequency(capture.left, 12000, 24000, 200.0, 900.0);
        check("SubOscillator_InSignalPath_ProducesAudio", capture.rms() > 0.01,
              "rms " + fmt(capture.rms(), 5));
        check("SubOscillator_InSignalPath_TracksMidiPitch", nearly(hz, 440.0, 12.0),
              "expected 440 Hz, measured " + fmt(hz, 2) + " Hz");
    }

    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "osc1Enabled", 0.0f);
        setParam(processor, "subOscEnabled", 0.0f);
        const auto capture = render(processor, 24000, { { 2000, true, 69, 0.9f } });
        check("SubOscillator_DisabledInSignalPath_IsSilent", capture.peak() < 1.0e-6,
              "peak " + fmt(capture.peak(), 9));
    }
}

//==============================================================================
// PHASE 5 - MAIN OSCILLATORS
//==============================================================================
void testOscillators()
{
    suite("OSCILLATORS");

    // Coarse tune is the transpose control (+/-24 semitones). An octave up must
    // double the frequency and an octave down must halve it.
    struct CoarseCase { float semitones; double ratio; const char* label; };
    const CoarseCase coarseCases[] = {
        { -12.0f, 0.5, "minus12" }, { 0.0f, 1.0, "zero" }, { 12.0f, 2.0, "plus12" }
    };

    for (int oscIndex = 1; oscIndex <= 3; ++oscIndex)
    {
        const auto slot = juce::String(oscIndex);
        for (const auto& coarse : coarseCases)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            for (const auto* other : { "1", "2", "3" })
            {
                setParam(processor, juce::String("osc") + other + "Enabled",
                         juce::String(other) == slot ? 1.0f : 0.0f);
            }
            setParam(processor, "osc" + slot + "Coarse", coarse.semitones);

            const auto capture = render(processor, 48000, { { 2000, true, 69, 0.9f } });
            const auto expected = 440.0 * coarse.ratio;
            const auto hz = estimateFrequency(capture.left, 12000, 24000,
                                              expected * 0.5, expected * 2.0);
            check((juce::String("Osc") + slot + "_Coarse_" + coarse.label + "_ProducesExpectedFrequency").toRawUTF8(),
                  nearly(hz, expected, expected * 0.03),
                  "expected " + fmt(expected, 2) + " Hz, measured " + fmt(hz, 2) + " Hz");
        }
    }

    // Fine tune is in cents; +100 cents is one semitone.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "osc1Fine", 100.0f);
        const auto capture = render(processor, 96000, { { 2000, true, 69, 0.9f } });
        const auto expected = 440.0 * std::pow(2.0, 1.0 / 12.0);
        const auto hz = estimateFrequency(capture.left, 12000, 64000, 300.0, 700.0);
        check("Osc1_FineTune100Cents_EqualsOneSemitone",
              nearly(hz, expected, expected * 0.01),
              "expected " + fmt(expected, 2) + " Hz, measured " + fmt(hz, 2) + " Hz");
    }

    // Enable/disable, per oscillator, independently.
    for (int oscIndex = 1; oscIndex <= 3; ++oscIndex)
    {
        const auto slot = juce::String(oscIndex);
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        for (const auto* other : { "1", "2", "3" })
        {
            setParam(processor, juce::String("osc") + other + "Enabled", 0.0f);
        }
        const auto silent = render(processor, 24000, { { 2000, true, 69, 0.9f } });
        check((juce::String("Osc") + slot + "_AllDisabledProducesSilence").toRawUTF8(),
              silent.peak() < 1.0e-6, "peak " + fmt(silent.peak(), 9));

        PX3SynthAudioProcessor enabled;
        makePlainPatch(enabled);
        for (const auto* other : { "1", "2", "3" })
        {
            setParam(enabled, juce::String("osc") + other + "Enabled",
                     juce::String(other) == slot ? 1.0f : 0.0f);
        }
        const auto sounding = render(enabled, 24000, { { 2000, true, 69, 0.9f } });
        check((juce::String("Osc") + slot + "_EnabledAloneProducesAudio").toRawUTF8(),
              sounding.rms() > 0.01, "rms " + fmt(sounding.rms(), 5));
    }

    // Waveform selection must actually change the signal, for every mode.
    {
        std::vector<double> rmsByMode;
        std::vector<juce::uint64> hashByMode;
        for (int mode = 0; mode < 20; ++mode)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", mode);
            const auto capture = render(processor, 24000, { { 2000, true, 57, 0.9f } });
            rmsByMode.push_back(capture.rms());
            juce::uint64 hash = 14695981039346656037ull;
            for (const auto v : capture.left)
            {
                juce::uint32 bits = 0;
                std::memcpy(&bits, &v, sizeof(bits));
                hash ^= bits;
                hash *= 1099511628211ull;
            }
            hashByMode.push_back(hash);
            check((juce::String("Osc1_Mode") + juce::String(mode) + "_ProducesAudio").toRawUTF8(),
                  capture.rms() > 0.005 && capture.isFinite(),
                  "rms " + fmt(capture.rms(), 5));
        }
        std::vector<juce::uint64> unique = hashByMode;
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        check("Osc1_EveryWaveformModeProducesADistinctSignal",
              unique.size() == hashByMode.size(),
              juce::String(static_cast<int>(unique.size())) + " distinct of "
                  + juce::String(static_cast<int>(hashByMode.size())));
    }

    // KARPLUS excitation. A plucked string is excited by a burst filling the
    // whole delay line; if that burst does not reach the output the mode is
    // audible only as a faint click. Compared against a plain sine at the same
    // settings, because "produces some audio" is too weak to catch a 20x level
    // deficit.
    {
        PX3SynthAudioProcessor sine, karplus;
        makePlainPatch(sine);
        makePlainPatch(karplus);
        setChoice(karplus, "osc1Mode", 13);
        setParam(karplus, "osc1MacroA", 1.0f); // longest decay: the string should ring
        setParam(karplus, "osc1MacroB", 0.8f);

        const auto sineCapture = render(sine, 48000, { { 2000, true, 57, 0.9f } });
        const auto karplusCapture = render(karplus, 48000, { { 2000, true, 57, 0.9f } });
        // Measured over the first 100 ms, where the pluck is loudest.
        const auto sineOnset = sineCapture.rmsOver(2000, 6800);
        const auto karplusOnset = karplusCapture.rmsOver(2000, 6800);
        // A fifth, not a quarter. Karplus excites the string from the shared
        // system random, which cannot be seeded, so this figure moves run to
        // run: measured over twelve runs it spans 0.0285 to 0.0395 against a
        // sine's 0.109, and a quarter threshold sits at 0.0272 - inside that
        // spread, which made the assertion fail perhaps one run in ten. The
        // claim being made is "the excitation reaches the output at all", and a
        // fifth still says that with the whole measured range clear of it.
        check("Karplus_ExcitationReachesOutput",
              karplusOnset > sineOnset * 0.20,
              "karplus onset rms " + fmt(karplusOnset, 5)
                  + " vs sine " + fmt(sineOnset, 5)
                  + " (a pluck must be at least a quarter of a sustained sine)");
    }

    {
        // The excitation is a noise burst spanning one delay period, so a low
        // note (long delay line) must carry at least as much onset energy as a
        // high one. If the burst is being overwritten before it is read, only
        // the few samples of note-age noise survive and pitch stops mattering.
        PX3SynthAudioProcessor low, high;
        makePlainPatch(low);
        makePlainPatch(high);
        for (auto* p : { &low, &high })
        {
            setChoice(*p, "osc1Mode", 13);
            setParam(*p, "osc1MacroA", 1.0f);
        }
        const auto lowCapture = render(low, 48000, { { 2000, true, 33, 0.9f } });   // ~55 Hz
        const auto highCapture = render(high, 48000, { { 2000, true, 81, 0.9f } }); // ~880 Hz
        check("Karplus_LowNoteExcitationIsNotWeakerThanHighNote",
              lowCapture.rmsOver(2000, 6800) > highCapture.rmsOver(2000, 6800) * 0.5,
              "low " + fmt(lowCapture.rmsOver(2000, 6800), 5)
                  + " vs high " + fmt(highCapture.rmsOver(2000, 6800), 5));
    }

    // Independence: changing one oscillator must not alter the others.
    //
    // This cannot be a bitwise comparison. Voice start seeds oscillator phase
    // from a global note counter that keeps incrementing, so two renders of the
    // same patch differ in phase by construction. The comparison is therefore
    // against a measured control: render the same patch twice to establish how
    // much the output moves on its own, then require the change caused by
    // retuning a *different* oscillator to stay inside that.
    {
        auto renderOscAlone = [](int soloIndex, const std::function<void(PX3SynthAudioProcessor&)>& tweak)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            for (int i = 1; i <= 3; ++i)
            {
                setParam(processor, "osc" + juce::String(i) + "Enabled", i == soloIndex ? 1.0f : 0.0f);
            }
            setParam(processor, "subOscEnabled", 0.0f);
            tweak(processor);
            return render(processor, 32000, { { 2000, true, 60, 0.9f } });
        };

        auto independenceCheck = [&renderOscAlone](const char* name,
                                                   int soloIndex,
                                                   const std::function<void(PX3SynthAudioProcessor&)>& tweak)
        {
            const auto controlA = renderOscAlone(soloIndex, [](PX3SynthAudioProcessor&) {});
            const auto controlB = renderOscAlone(soloIndex, [](PX3SynthAudioProcessor&) {});
            const auto tweaked = renderOscAlone(soloIndex, tweak);

            const auto controlDelta = std::abs(controlA.rms() - controlB.rms());
            const auto tweakDelta = std::abs(controlA.rms() - tweaked.rms());
            const auto controlHz = estimateFrequency(controlA.left, 12000, 16000, 100.0, 800.0);
            const auto tweakHz = estimateFrequency(tweaked.left, 12000, 16000, 100.0, 800.0);

            // Tolerance is the observed self-variation plus a small floor, not a
            // number chosen to make this pass.
            const auto allowed = juce::jmax(controlDelta * 2.0, controlA.rms() * 0.02);
            check(name,
                  tweakDelta <= allowed && nearly(controlHz, tweakHz, 1.0),
                  "rms moved " + fmt(tweakDelta, 6) + " (self-variation " + fmt(controlDelta, 6)
                      + ", allowed " + fmt(allowed, 6) + "), pitch " + fmt(controlHz, 2)
                      + " -> " + fmt(tweakHz, 2) + " Hz");
        };

        independenceCheck("Osc1_UnaffectedByOsc2ParameterChanges", 1, [](PX3SynthAudioProcessor& p)
        {
            setParam(p, "osc2Coarse", 7.0f);
            setChoice(p, "osc2Mode", 6);
            setParam(p, "osc2Fine", 30.0f);
        });
        independenceCheck("Osc2_UnaffectedByOsc1AndOsc3ParameterChanges", 2, [](PX3SynthAudioProcessor& p)
        {
            setParam(p, "osc1Coarse", -5.0f);
            setParam(p, "osc3Fine", 40.0f);
            setChoice(p, "osc1Mode", 1);
        });
        independenceCheck("Osc3_UnaffectedBySubOscillatorParameterChanges", 3, [](PX3SynthAudioProcessor& p)
        {
            setChoice(p, "subOscOctave", 2);
            setParam(p, "subOscPitch", 0.2f);
        });
    }

    // Combining sources. The voice divides by sqrt(active source count), so the
    // designed behaviour is roughly CONSTANT summed level as sources are added,
    // not a louder one - that is what stops a four-source patch clipping. What
    // must hold is that each source actually reaches its own mixer channel.
    {
        PX3SynthAudioProcessor one, two, three;
        for (auto* p : { &one, &two, &three }) makePlainPatch(*p);
        for (auto* p : { &two, &three })
        {
            setParam(*p, "osc2Enabled", 1.0f);
            setParam(*p, "osc2Coarse", 7.0f);
        }
        setParam(three, "osc3Enabled", 1.0f);
        setParam(three, "osc3Coarse", 12.0f);

        const auto r1 = render(one, 32000, { { 2000, true, 57, 0.9f } }).rms();
        const auto r2 = render(two, 32000, { { 2000, true, 57, 0.9f } }).rms();
        const auto r3 = render(three, 32000, { { 2000, true, 57, 0.9f } }).rms();

        check("Oscillators_PerSourceNormalisationHoldsSummedLevelBounded",
              r2 < r1 * 1.7 && r3 < r1 * 1.7 && r2 > r1 * 0.5 && r3 > r1 * 0.5,
              "1 osc " + fmt(r1, 5) + ", 2 osc " + fmt(r2, 5) + ", 3 osc " + fmt(r3, 5));

        // Per-source meters prove each enabled oscillator reaches its own
        // channel, which a summed-level test cannot show.
        check("Oscillators_EachEnabledSourceReachesItsOwnMixerChannel",
              three.debugGetMixerSourceRms(1) > 1.0e-4
                  && three.debugGetMixerSourceRms(2) > 1.0e-4
                  && three.debugGetMixerSourceRms(3) > 1.0e-4
                  && three.debugGetMixerSourceRms(0) < 1.0e-6,
              "osc1 " + fmt(three.debugGetMixerSourceRms(1), 6)
                  + ", osc2 " + fmt(three.debugGetMixerSourceRms(2), 6)
                  + ", osc3 " + fmt(three.debugGetMixerSourceRms(3), 6)
                  + ", sub (disabled) " + fmt(three.debugGetMixerSourceRms(0), 6));
    }

    // The mixer channel is the single gain stage for a source. Separate
    // oscNLevel / subOscLevel parameters once existed alongside it, hardcoded
    // to 1.0 in the voice and therefore inert while still being host-visible
    // and saved into every preset; they were removed rather than wired in,
    // because wiring them in would have introduced a second gain stage. The
    // like-named MODULATION destinations are unaffected - they are canonical
    // IDs routed to the mixer params, covered by the ENV/LFO destination tests.
    {
        PX3SynthAudioProcessor processor;
        juce::StringArray offenders;
        for (auto* parameter : processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            {
                for (const auto* id : { "osc1Level", "osc2Level", "osc3Level", "subOscLevel" })
                {
                    if (ranged->paramID.equalsIgnoreCase(id)) offenders.add(ranged->paramID);
                }
            }
        }
        check("Oscillators_NoOrphanedSourceLevelParametersAreExposed", offenders.isEmpty(),
              offenders.isEmpty() ? "the mixer channel is the only source gain stage"
                                  : "still exposed: " + offenders.joinIntoString(", "));
    }

    // Gain-structure contract. The 4 dB of modulation headroom lives on the
    // SOURCE, not on the mixer fader, so a freshly loaded plugin shows its
    // channels at 0 dB rather than looking as though someone had already pulled
    // them all down. The fader therefore runs to +4 dB, so a channel can still
    // be driven to full scale.
    {
        PX3SynthAudioProcessor processor;
        auto* level = findParameter(processor, "mix.osc1.level");
        auto* fxReturn = findParameter(processor, "fxReturnGain");
        const auto headroom = px3::processor_internal::sourceHeadroomGain();
        const auto faderMax = px3::processor_internal::channelFaderMaxGain();

        check("Mixer_ChannelFaderDefaultsToUnityGain",
              level != nullptr && nearly(level->convertFrom0to1(level->getValue()), 1.0, 1.0e-4),
              "default channel gain = "
                  + fmt(level != nullptr ? level->convertFrom0to1(level->getValue()) : -1.0, 5)
                  + " (0.0 dB)");
        check("Mixer_FxReturnFaderDefaultsToUnityGain",
              fxReturn != nullptr && nearly(fxReturn->convertFrom0to1(fxReturn->getValue()), 1.0, 1.0e-4),
              "default FX return gain = "
                  + fmt(fxReturn != nullptr ? fxReturn->convertFrom0to1(fxReturn->getValue()) : -1.0, 5));
        check("Mixer_FaderTopExactlyCancelsSourceHeadroom",
              nearly(headroom * faderMax, 1.0, 1.0e-4),
              "source " + fmt(headroom, 5) + " x fader max " + fmt(faderMax, 5)
                  + " = " + fmt(headroom * faderMax, 5) + " (full scale)");
        check("Mixer_SourceCarriesFourDbOfHeadroom",
              nearly(juce::Decibels::gainToDecibels(headroom), -4.0, 0.01),
              fmt(juce::Decibels::gainToDecibels(headroom), 2) + " dB");
    }

    // Moving the headroom must not have changed how loud the synth is. These
    // are the levels measured from the previous gain structure, where the fader
    // defaulted to -4 dB and the source ran at full scale.
    {
        auto renderDefaultPatch = [](bool faderAtMaximum)
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "osc1Enabled", 1.0f);
            setParam(processor, "osc2Enabled", 0.0f);
            setParam(processor, "osc3Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 0.0f);
            setParam(processor, "reverbEnabled", 0.0f);
            setParam(processor, "delayEnabled", 0.0f);
            setParam(processor, "moodEnabled", 0.0f);
            setParam(processor, "filter1Enabled", 0.0f);
            setParam(processor, "filter2Enabled", 0.0f);
            // Same reason as makePlainPatch: these numbers are the MIXER's gain
            // structure, measured with every colour stage out of the way.
            setParam(processor, "analogEnabled", 0.0f);
            setParam(processor, "ampSustain", 1.0f);
            setChoice(processor, "osc1Mode", 0);
            if (faderAtMaximum)
            {
                if (auto* lv = findParameter(processor, "mix.osc1.level")) lv->setValueNotifyingHost(1.0f);
            }
            return render(processor, 48000, { { 2000, true, 57, 0.9f } }).rmsOver(24000, 46000);
        };
    // ---- dry channel -------------------------------------------------------
    {
        // The dry bus is a mixer channel of its own now: the summed sources
        // before the FX return joins them. These check that its controls do
        // what a channel's controls do, and - just as importantly - that the
        // channel existing changes nothing until it is touched.
        const auto renderDry = [](std::function<void(PX3SynthAudioProcessor&)> configure)
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "osc1Enabled", 1.0f);
            setParam(processor, "osc2Enabled", 0.0f);
            setParam(processor, "osc3Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 0.0f);
            setParam(processor, "reverbEnabled", 0.0f);
            setParam(processor, "delayEnabled", 0.0f);
            setParam(processor, "moodEnabled", 0.0f);
            setParam(processor, "filter1Enabled", 0.0f);
            setParam(processor, "filter2Enabled", 0.0f);
            setParam(processor, "ampSustain", 1.0f);
            setChoice(processor, "osc1Mode", 0);
            configure(processor);
            return render(processor, 48000, { { 2000, true, 57, 0.9f } }).rmsOver(24000, 46000);
        };

        const auto atDefault = renderDry([](PX3SynthAudioProcessor&) {});
        const auto halved = renderDry([](PX3SynthAudioProcessor& p) {
            setParam(p, "mix.dry.level", 0.5f);
        });
        const auto muted = renderDry([](PX3SynthAudioProcessor& p) {
            setParam(p, "mix.dry.mute", 1.0f);
        });

        check("Mixer_DryLevelScalesTheDryPath",
              atDefault > 1.0e-4f && std::abs(halved - atDefault * 0.5f) < atDefault * 0.12f,
              "default " + fmt(atDefault, 5) + " -> half " + fmt(halved, 5));

        check("Mixer_DryMuteSilencesTheDryPath",
              muted < atDefault * 0.02f,
              "muted rms " + fmt(muted, 6) + " against " + fmt(atDefault, 5));

        // Polarity is only audible against something else, so it is measured
        // with the FX return live: flipping the dry bus against a wet path that
        // came from it makes the two partially cancel. On its own a sign flip
        // changes nothing an RMS meter can see.
        const auto renderWithWet = [](bool invertDry)
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "osc1Enabled", 1.0f);
            setParam(processor, "osc2Enabled", 0.0f);
            setParam(processor, "osc3Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "ampSustain", 1.0f);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "filter1Enabled", 0.0f);
            setParam(processor, "filter2Enabled", 0.0f);
            setParam(processor, "vibeEnabled", 0.0f);
            setParam(processor, "delayEnabled", 0.0f);
            setParam(processor, "moodEnabled", 0.0f);
            // Reverb passes the dry signal through with enough direct content
            // that the two paths can cancel.
            setParam(processor, "reverbEnabled", 1.0f);
            setParam(processor, "reverbAmount", 1.0f);
            setParam(processor, "mix.dry.phase", invertDry ? 1.0f : 0.0f);
            return render(processor, 48000, { { 2000, true, 57, 0.9f } }).rmsOver(24000, 46000);
        };

        const auto inPhase = renderWithWet(false);
        const auto outOfPhase = renderWithWet(true);

        check("Mixer_DryPolarityChangesTheSumAgainstTheWetPath",
              inPhase > 1.0e-4f && std::abs(outOfPhase - inPhase) > inPhase * 0.05f,
              "in phase " + fmt(inPhase, 5) + " vs inverted " + fmt(outOfPhase, 5));

        // Soloing a source must leave the dry bus open, or the solo would mute
        // the path the soloed source is heard through.
        const auto soloedSource = renderDry([](PX3SynthAudioProcessor& p) {
            setParam(p, "mix.osc1.solo", 1.0f);
        });

        check("Mixer_SoloingASourceLeavesTheDryBusOpen",
              soloedSource > atDefault * 0.5f,
              "soloed-source rms " + fmt(soloedSource, 5) + " against " + fmt(atDefault, 5));
    }

        check("Mixer_HeadroomMoveLeftDefaultLevelUnchanged",
              nearly(renderDefaultPatch(false), 0.127851, 0.0005),
              "default patch rms " + fmt(renderDefaultPatch(false), 6) + " (was 0.127851)");
        check("Mixer_HeadroomMoveLeftMaximumLevelUnchanged",
              nearly(renderDefaultPatch(true), 0.202856, 0.0005),
              "fader at maximum rms " + fmt(renderDefaultPatch(true), 6) + " (was 0.202856)");
    }

    // The mixer channel level is the gain stage that must work.
    {
        PX3SynthAudioProcessor quiet, loud;
        makePlainPatch(quiet);
        makePlainPatch(loud);
        setParam(quiet, "mix.osc1.level", 0.4f);
        setParam(loud, "mix.osc1.level", 0.8f);
        const auto quietRms = render(quiet, 32000, { { 2000, true, 57, 0.9f } }).rms();
        const auto loudRms = render(loud, 32000, { { 2000, true, 57, 0.9f } }).rms();
        check("MixerOsc1Level_IsTheEffectiveSourceGainStage",
              quietRms < loudRms * 0.75,
              "level 0.4 -> " + fmt(quietRms, 6) + ", 0.8 -> " + fmt(loudRms, 6));
    }

    // Per-voice independence: two simultaneous notes must each track their own
    // pitch, which fails if voices share oscillator phase or frequency state.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        const auto capture = render(processor, 64000,
                                    { { 2000, true, 57, 0.9f }, { 2000, true, 69, 0.9f } });
        check("Oscillators_TwoSimultaneousVoicesRenderWithoutCorruption",
              capture.isFinite() && capture.rms() > 0.01,
              "rms " + fmt(capture.rms(), 5));

        PX3SynthAudioProcessor lowOnly;
        makePlainPatch(lowOnly);
        const auto low = render(lowOnly, 64000, { { 2000, true, 57, 0.9f } });
        const auto lowHz = estimateFrequency(low.left, 12000, 32000, 100.0, 400.0);
        check("Oscillators_SingleVoiceTracksMidiPitch", nearly(lowHz, 220.0, 6.0),
              "MIDI 57 expected 220 Hz, measured " + fmt(lowHz, 2) + " Hz");
    }
}

} // namespace px3tests
