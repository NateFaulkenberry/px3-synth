#include "TestSupport.h"
#include "LfoGenerator.h"
#include "OscillatorUnit.h"
#include "Reverb.h"
#include "Distortion.h"

// v0.8.0 release QA: one test per defect found by the DSP and UI audits of
// everything added since v0.7.6, each written to fail on the defect before it
// was fixed. Grouped here so the release's own findings stay in one place.
namespace px3tests
{
namespace
{
using Processor = PX3SynthAudioProcessor;
}

void testReleaseQa()
{
    suite("RELEASE QA 0.8.0");

    // ---- SMOOTH RND is continuous through the processor's block path -------------
    // The processor reads an LFO once per block at the block's midpoint. The
    // midpoint could pass the end of a cycle before the cycle counter moved, and
    // the shape then eased from the OLD cycle's value at t ~ 0: a full-scale jump
    // back for one block in about half of all cycles.
    {
        LfoGenerator lfo;
        lfo.prepare(48000.0);
        LfoSettings settings;
        settings.frequencyHz = 2.0f;
        settings.waveformIndex = px3::lfoWaveformToIndex(px3::LfoWaveform::smoothRandom);
        lfo.setSettings(settings);
        auto previous = lfo.getMidpointSignalAndAdvance(512);
        auto worst = 0.0f;
        for (int block = 0; block < 4000; ++block)
        {
            const auto value = lfo.getMidpointSignalAndAdvance(512);
            worst = juce::jmax(worst, std::abs(value - previous));
            previous = value;
        }
        // At 2 Hz a 512-sample block is 1/47 of a cycle; the eased curve moves at
        // most (pi/2) x 2 x that, about 0.067.
        check("Qa_SmoothRandomHasNoBlockJumps", worst < 0.1f,
              "largest block-to-block step " + fmt(worst, 3) + " over 4000 blocks at 2 Hz");
    }

    // ---- the three LFOs' random shapes are independent ---------------------------
    // The random series was a hash of the cycle number alone: LFO 1, 2 and 3 on
    // S&H at the same rate stepped through the very same values.
    {
        Processor processor;
        makePlainPatch(processor);
        for (int i = 1; i <= 3; ++i)
        {
            const auto prefix = "mod.lfo" + juce::String(i) + ".";
            setParam(processor, prefix + "enabled", 1.0f);
            setParam(processor, prefix + "amount", 1.0f);
            setParam(processor, prefix + "frequency", 4.0f);
            setChoice(processor, prefix + "waveform", px3::lfoWaveformToIndex(px3::LfoWaveform::sampleHold));
        }
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);
        juce::AudioBuffer<float> buffer(2, kBlockSize);
        auto same12 = 0, same23 = 0, blocks = 0;
        for (int block = 0; block < 400; ++block)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            const auto a = processor.debugGetLfoCurrentValue(0);
            const auto b = processor.debugGetLfoCurrentValue(1);
            const auto c = processor.debugGetLfoCurrentValue(2);
            same12 += std::abs(a - b) < 1.0e-6f ? 1 : 0;
            same23 += std::abs(b - c) < 1.0e-6f ? 1 : 0;
            ++blocks;
        }
        check("Qa_EachLfosRandomSeriesIsItsOwn", same12 < blocks / 10 && same23 < blocks / 10,
              "LFO1=LFO2 on " + juce::String(same12) + " and LFO2=LFO3 on " + juce::String(same23) + " of "
                  + juce::String(blocks) + " blocks");
    }

    // ---- filter key tracking is in place from the note's first sample ------------
    // The tracked cutoff was only ever a TARGET: each note started at the
    // untracked cutoff and glided up over its first ~20 ms - an audible sweep on
    // every note, a chirp at high resonance. A tracked note now starts where it
    // belongs: its onset matches a note whose cutoff was simply set there.
    {
        const auto onset = [](float cutoffHz, float keyTrack)
        {
            Processor processor;
            makePlainPatch(processor);
            setChoice(processor, "voice.osc1.mode", 1);   // SAW
            setParam(processor, "voice.filter1.enabled", 1.0f);
            setChoice(processor, "voice.filter1.type", 1);   // LP24
            setParam(processor, "voice.filter1.cutoff", cutoffHz);
            setParam(processor, "voice.filter1.resonance", 4.0f);
            setParam(processor, "voice.filter1.keytrack", keyTrack);
            setParam(processor, "voice.filter1.keytrack.key", 60.0f);
            const auto capture = render(processor, 4096, { { 0, true, 84, 0.9f } });
            return std::pair<double, double> { capture.rmsOver(96, 960), capture.rmsOver(2400, 4000) };
        };
        // Note 84 is two octaves above the reference: +100% tracking quadruples 400 Hz.
        const auto tracked = onset(400.0f, 1.0f);
        const auto direct = onset(1600.0f, 0.0f);
        const auto onsetDb = 20.0 * std::log10(juce::jmax(1.0e-9, tracked.first) / juce::jmax(1.0e-9, direct.first));
        const auto laterDb = 20.0 * std::log10(juce::jmax(1.0e-9, tracked.second) / juce::jmax(1.0e-9, direct.second));
        check("Qa_KeyTrackedCutoffIsInPlaceAtNoteOn", std::abs(onsetDb) < 0.5 && std::abs(laterDb) < 0.5,
              "tracked vs set-directly: onset (2-20 ms) " + fmt(onsetDb, 2) + " dB, later " + fmt(laterDb, 2) + " dB");
    }

    // ---- DIGITAL FOLD is a wavefolder --------------------------------------------
    // FOLD crossfaded sin(n x) into sin((n+1) x): at mid-knob the output was the
    // 4th harmonic alone - the sine moved up two octaves with its fundamental
    // gone - where the manual promises a fold that ADDS harmonics. Measured on the
    // unit with BITS at 16 and RATE at 1x, so only the fold shapes the spectrum.
    {
        constexpr int order = 14;
        constexpr int size = 1 << order;
        const auto binHz = 48000.0 / size;
        constexpr int fundamentalBin = 75;   // 219.7 Hz, a whole number of cycles per frame
        const auto spectrum = [&](float fold)
        {
            OscillatorUnit unit;
            unit.prepare(48000.0);
            OscillatorSettings settings;
            settings.modeIndex = 14;   // DIGITAL
            settings.macroA = 1.0f;    // BITS: 16
            settings.macroB = 0.0f;    // RATE: 1x
            settings.macroC = fold;
            unit.setSettings(settings);
            unit.resetForNote(0.0, 1u);
            OscillatorUnit::RenderContext context;
            context.frequencyHz = fundamentalBin * binHz;
            std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
            for (int i = 0; i < 8192; ++i) { unit.renderSample(context); }
            for (int i = 0; i < size; ++i) { data[static_cast<std::size_t>(i)] = static_cast<float>(unit.renderSample(context)); }
            juce::dsp::FFT fft(order);
            fft.performFrequencyOnlyForwardTransform(data.data());
            std::vector<double> harmonics;
            for (int h = 1; h * fundamentalBin < size / 2; ++h)
            {
                harmonics.push_back(static_cast<double>(data[static_cast<std::size_t>(h * fundamentalBin)]));
            }
            return harmonics;
        };
        const auto describe = [](const std::vector<double>& h)
        {
            const auto strongest = *std::max_element(h.begin(), h.end());
            auto count = 0;
            for (const auto v : h) { count += v > strongest * 0.01 ? 1 : 0; }   // within 40 dB
            return std::pair<double, int> { 20.0 * std::log10(juce::jmax(1.0e-12, h[0] / strongest)), count };
        };
        const auto none = describe(spectrum(0.0f));
        const auto half = describe(spectrum(0.5f));
        const auto full = describe(spectrum(1.0f));
        check("Qa_DigitalFoldIsAWavefolder",
              none.second <= 2 && half.second > none.second + 2 && full.second > half.second
                  && half.first > -18.0 && full.first > -18.0,
              "harmonics within 40 dB: " + juce::String(none.second) + " / " + juce::String(half.second) + " / "
                  + juce::String(full.second) + " at FOLD 0 / 0.5 / 1; fundamental vs strongest "
                  + fmt(half.first, 1) + " dB and " + fmt(full.first, 1) + " dB");
    }

    // ---- an external MIDI clock moves a TRANSPORT LFO smoothly --------------------
    // The song position only advanced on each clock tick (1/24 beat), and the
    // TRANSPORT clock re-derives the LFO's phase from it every block: the LFO
    // froze for a block or two, then jumped - a stair, not a wave.
    {
        Processor processor;
        processor.setPlayConfigDetails(0, 2, 48000.0, 512);
        processor.prepareToPlay(48000.0, 512);
        setParam(processor, "mod.lfo1.enabled", 1.0f);
        setParam(processor, "mod.lfo1.amount", 1.0f);
        setChoice(processor, "mod.lfo1.waveform", 0);       // SINE
        setParam(processor, "mod.lfo1.clock.mode", 2.0f);   // TRANSPORT
        juce::AudioBuffer<float> buffer(2, 512);
        const auto tickInterval = 60.0 * 48000.0 / (120.0 * 24.0);   // 1000 samples
        double nextTick = 0.0;
        auto held = 0, compared = 0;
        float previous = 0.0f;
        for (int block = 0; block < 600; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0) { midi.addEvent(juce::MidiMessage::midiStart(), 0); }
            const auto blockStart = static_cast<double>(block) * 512.0;
            while (nextTick < blockStart + 512.0)
            {
                midi.addEvent(juce::MidiMessage::midiClock(), static_cast<int>(nextTick - blockStart));
                nextTick += tickInterval;
            }
            buffer.clear();
            processor.processBlock(buffer, midi);
            const auto value = processor.debugGetLfoCurrentValue(0);
            if (block > 100)   // past the tempo estimate settling
            {
                ++compared;
                held += std::abs(value - previous) < 1.0e-6f ? 1 : 0;
            }
            previous = value;
        }
        check("Qa_MidiClockTransportLfoMovesEveryBlock", compared > 0 && held == 0,
              juce::String(held) + " of " + juce::String(compared) + " blocks held still while the clock ran");
    }

    // ---- delay WOBBLE / MOD DEPTH and the compressor's gains are smooth -----------
    // All five were read once per block and applied raw per sample, so an LFO
    // on any of them moved the sound in steps at every block boundary - a
    // click at each 512 samples. The test: how much bigger the signal's
    // curvature (second difference) is at block boundaries than elsewhere.
    {
        const auto boundaryRatio = [](const juce::String& destination, const std::function<void(Processor&)>& patch)
        {
            Processor processor;
            makePlainPatch(processor);
            patch(processor);
            setParam(processor, "mod.lfo1.enabled", 1.0f);
            setParam(processor, "mod.lfo1.amount", 1.0f);
            setParam(processor, "mod.lfo1.frequency", 5.0f);
            setChoice(processor, "mod.lfo1.waveform", 0);   // SINE
            juce::String error;
            processor.setGraphRoute(0, { 0, destination, px3::synth::ModulationPolarity::native,
                                         px3::synth::ModulationCurve::linear }, error);
            auto& depth = processor.getGraphRouteDepthParam(0);
            depth.setValueNotifyingHost(depth.convertTo0to1(1.0f));
            const auto capture = render(processor, 48000, { { 0, true, 45, 0.9f } });
            double atBoundary = 0.0, elsewhere = 0.0;
            int nb = 0, ne = 0;
            for (int i = 4096; i + 1 < static_cast<int>(capture.left.size()); ++i)
            {
                const auto curvature = std::abs(static_cast<double>(capture.left[static_cast<std::size_t>(i + 1)])
                                                - 2.0 * capture.left[static_cast<std::size_t>(i)]
                                                + capture.left[static_cast<std::size_t>(i - 1)]);
                if (i % kBlockSize == 0) { atBoundary += curvature; ++nb; } else { elsewhere += curvature; ++ne; }
            }
            return (atBoundary / juce::jmax(1, nb)) / juce::jmax(1.0e-12, elsewhere / juce::jmax(1, ne));
        };

        juce::StringArray stepped;
        const auto probe = [&](const juce::String& id, const std::function<void(Processor&)>& patch)
        {
            const auto ratio = boundaryRatio(id, patch);
            if (ratio > 1.5) { stepped.add(id + " " + fmt(ratio, 2) + "x"); }
            return ratio;
        };
        const auto delayPatch = [](int algorithm)
        {
            return [algorithm](Processor& p)
            {
                setChoice(p, "voice.osc1.mode", 1);   // SAW
                setParam(p, "fx.delay.enabled", 1.0f);
                setParam(p, "fx.delay.amount", 1.0f);
                setChoice(p, "fx.delay.algorithm", algorithm);
                setParam(p, "mix.osc1.send.fx", 1.0f);
            };
        };
        const auto compPatch = [](Processor& p)
        {
            setChoice(p, "voice.osc1.mode", 1);
            setParam(p, "mix.dry.insert.comp.enabled", 1.0f);
        };
        const auto r1 = probe("fx.delay.mod.depth", delayPatch(5));   // Modulated
        const auto r2 = probe("fx.delay.wobble", delayPatch(1));      // Tape
        const auto r3 = probe("mix.dry.insert.comp.input", compPatch);
        const auto r4 = probe("mix.dry.insert.comp.output", compPatch);
        const auto r5 = probe("mix.dry.insert.comp.mix", compPatch);
        check("Qa_ModulatedDelayAndCompControlsAreSmooth", stepped.isEmpty(),
              "curvature at block boundaries vs elsewhere: mod depth " + fmt(r1, 2) + "x, wobble " + fmt(r2, 2)
                  + "x, comp input " + fmt(r3, 2) + "x, output " + fmt(r4, 2) + "x, mix " + fmt(r5, 2) + "x");
    }

    // ---- shimmer sounds the same at every sample rate ------------------------------
    // Its grain window (2048 samples), its loop lowpass (0.35) and its DC pole
    // were fixed per SAMPLE: at 96 kHz the window was half as long, the loop
    // brighter and its gain higher. Same settings, same burst, 48 and 96 kHz:
    // the tail's level and brightness must match, and it must still decay.
    {
        struct Tail { double level1s; double level3s; double centroid; };
        const auto tail = [](double sr, float shimmerAmount = 1.0f)
        {
            ::Reverb reverb;
            reverb.prepare(sr);
            ReverbSettings settings;
            settings.amount = 1.0f;
            settings.algorithmIndex = 3;   // CLOUD
            settings.decay = 1.0f;
            settings.cloudFeedback = 1.0f;
            settings.shimmer = shimmerAmount;
            const auto total = static_cast<int>(sr * 4.0);
            std::vector<float> out(static_cast<std::size_t>(total));
            constexpr int block = 256;
            for (int start = 0; start < total; start += block)
            {
                reverb.updateForBlock(settings, block);
                for (int i = start; i < juce::jmin(total, start + block); ++i)
                {
                    const auto t = static_cast<double>(i) / sr;
                    const auto in = t < 0.1 ? static_cast<float>(0.5 * std::sin(juce::MathConstants<double>::twoPi * 440.0 * t)) : 0.0f;
                    float l = 0.0f, r = 0.0f;
                    reverb.processSampleFrame(in, in, l, r);
                    out[static_cast<std::size_t>(i)] = 0.5f * (l + r);
                }
            }
            const auto rmsAt = [&](double from, double to)
            {
                double e = 0.0; int n = 0;
                for (int i = static_cast<int>(from * sr); i < static_cast<int>(to * sr); ++i) { e += out[static_cast<std::size_t>(i)] * out[static_cast<std::size_t>(i)]; ++n; }
                return std::sqrt(e / juce::jmax(1, n));
            };
            // Brightness: zero crossings per second over the 1 s window - a
            // centroid proxy that does not care about the sample rate.
            int crossings = 0;
            for (int i = static_cast<int>(1.0 * sr) + 1; i < static_cast<int>(2.0 * sr); ++i)
            {
                crossings += (out[static_cast<std::size_t>(i - 1)] < 0.0f) != (out[static_cast<std::size_t>(i)] < 0.0f) ? 1 : 0;
            }
            return Tail { rmsAt(1.0, 2.0), rmsAt(3.0, 4.0), static_cast<double>(crossings) };
        };
        const auto at48 = tail(48000.0);
        const auto at96 = tail(96000.0);
        const auto dry48 = tail(48000.0, 0.0f);
        const auto dry96 = tail(96000.0, 0.0f);
        std::printf("  ..    CLOUD without shimmer, 96 vs 48 kHz: tail %.2f dB, brightness x%.2f\n",
                    20.0 * std::log10(dry96.level1s / juce::jmax(1.0e-12, dry48.level1s)), dry96.centroid / juce::jmax(1.0, dry48.centroid));
        const auto levelDb = 20.0 * std::log10(juce::jmax(1.0e-12, at96.level1s) / juce::jmax(1.0e-12, at48.level1s));
        const auto brightness = at96.centroid / juce::jmax(1.0, at48.centroid);
        check("Qa_ShimmerIsTheSameAtEverySampleRate",
              std::abs(levelDb) < 3.0 && brightness > 0.8 && brightness < 1.25
                  && at48.level3s < at48.level1s && at96.level3s < at96.level1s,
              "96 vs 48 kHz: tail " + fmt(levelDb, 2) + " dB, brightness x" + fmt(brightness, 2)
                  + "; decays " + fmt(20.0 * std::log10(at48.level3s / juce::jmax(1.0e-12, at48.level1s)), 1) + " / "
                  + fmt(20.0 * std::log10(at96.level3s / juce::jmax(1.0e-12, at96.level1s)), 1) + " dB from 1 s to 3 s");
    }

    // ---- aliasing: DRIVE at full drive, and the analog filter models ----------------
    // Measured, not assumed. Energy that is not at a harmonic of the input, in
    // dB against the fundamental, below 15 kHz (where aliasing is heard).
    {
        // Blackman-Harris spectrum, harmonics masked +/- 6 bins.
        const auto aliasDb = [](const std::vector<float>& x, double sr, double f0)
        {
            constexpr int order = 15;
            constexpr int n = 1 << order;
            std::vector<float> data(static_cast<std::size_t>(n) * 2, 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const auto t = juce::MathConstants<double>::twoPi * i / n;
                const auto w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
                data[static_cast<std::size_t>(i)] = static_cast<float>(x[static_cast<std::size_t>(i)] * w);
            }
            juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(data.data());
            const auto binHz = sr / n;
            double fundamental = 1.0e-30, alias = 0.0;
            for (int k = 3; k < n / 2; ++k)
            {
                const auto hz = k * binHz;
                const auto power = static_cast<double>(data[static_cast<std::size_t>(k)]) * data[static_cast<std::size_t>(k)];
                const auto harmonic = std::round(hz / f0);
                const auto nearHarmonic = harmonic >= 1.0 && std::abs(hz - harmonic * f0) < 6.0 * binHz;
                if (harmonic == 1.0 && nearHarmonic) { fundamental = juce::jmax(fundamental, power); }
                if (! nearHarmonic && hz < 15000.0) { alias += power; }
            }
            return 10.0 * std::log10(juce::jmax(1.0e-30, alias) / fundamental);
        };

        // DRIVE: a 5 kHz sine into HARD at full drive (the worst case), and a
        // survey of more typical settings for the report.
        const auto driveAt = [&](double hz, float driveAmount, int type)
        {
            px3::Distortion drive;
            drive.prepare(48000.0);
            px3::DistortionSettings settings;
            settings.type = type;
            settings.drive = driveAmount;
            settings.tight = 0.0f;
            settings.tone = 1.0f;
            settings.mix = 1.0f;
            std::vector<float> out;
            for (int i = 0; i < 8192 + 32768; ++i)
            {
                if (i % 256 == 0) { drive.updateForBlock(settings); }
                const auto in = static_cast<float>(0.5 * std::sin(juce::MathConstants<double>::twoPi * hz * i / 48000.0));
                float l = 0.0f, r = 0.0f;
                drive.processSampleFrame(in, in, l, r);
                if (i >= 8192) { out.push_back(l); }
            }
            return aliasDb(out, 48000.0, hz);
        };
        juce::String survey;
        auto worstTypical = -200.0;
        for (const auto hz : { 220.0, 1000.0, 3000.0 })
        {
            for (const auto amount : { 0.35f, 0.7f, 1.0f })
            {
                const auto soft = driveAt(hz, amount, 0);
                const auto hard = driveAt(hz, amount, 1);
                worstTypical = juce::jmax(worstTypical, juce::jmax(soft, hard));
                survey << fmt(hz, 0) << "Hz/" << fmt(amount, 2) << ": " << fmt(soft, 0) << "/" << fmt(hard, 0) << "  ";
            }
        }
        std::printf("  ..    DRIVE alias dB (SOFT/HARD) by input Hz/drive: %s\n", survey.toRawUTF8());
        double driveAlias = 0.0;
        {
            px3::Distortion drive;
            drive.prepare(48000.0);
            px3::DistortionSettings settings;
            settings.type = 1;
            settings.drive = 1.0f;
            settings.tight = 0.0f;
            settings.tone = 1.0f;
            settings.mix = 1.0f;
            std::vector<float> out;
            for (int i = 0; i < 8192 + 32768; ++i)
            {
                if (i % 256 == 0) { drive.updateForBlock(settings); }
                const auto in = static_cast<float>(0.5 * std::sin(juce::MathConstants<double>::twoPi * 5003.0 * i / 48000.0));
                float l = 0.0f, r = 0.0f;
                drive.processSampleFrame(in, in, l, r);
                if (i >= 8192) { out.push_back(l); }
            }
            driveAlias = aliasDb(out, 48000.0, 5003.0);
        }

        // Each analog filter model at full resonance on a 3.5 kHz saw (cutoff
        // 8 kHz), against the same saw unfiltered: what the filter adds.
        const auto filtered = [&](int type)
        {
            Processor processor;
            makePlainPatch(processor);
            setChoice(processor, "voice.osc1.mode", 1);   // SAW
            setParam(processor, "voice.filter1.enabled", type >= 0 ? 1.0f : 0.0f);
            if (type >= 0)
            {
                setChoice(processor, "voice.filter1.type", type);
                setParam(processor, "voice.filter1.cutoff", 8000.0f);
                if (auto* q = findParameter(processor, "voice.filter1.resonance")) { q->setValueNotifyingHost(0.5f); }
            }
            const auto capture = render(processor, 8192 + 32768, { { 0, true, 105, 0.9f } });
            std::vector<float> tail(capture.left.begin() + 8192, capture.left.end());
            return aliasDb(tail, kSampleRate, 440.0 * std::pow(2.0, (105 - 69) / 12.0));
        };
        const auto bare = filtered(-1);
        {
            // The filters at a more typical pitch, for the report.
            const auto at = [&](int note, int type)
            {
                Processor processor;
                makePlainPatch(processor);
                setChoice(processor, "voice.osc1.mode", 1);
                setParam(processor, "voice.filter1.enabled", 1.0f);
                setChoice(processor, "voice.filter1.type", type);
                setParam(processor, "voice.filter1.cutoff", 3000.0f);
                if (auto* q = findParameter(processor, "voice.filter1.resonance")) { q->setValueNotifyingHost(0.5f); }
                const auto capture = render(processor, 8192 + 32768, { { 0, true, note, 0.9f } });
                std::vector<float> tail(capture.left.begin() + 8192, capture.left.end());
                return aliasDb(tail, kSampleRate, 440.0 * std::pow(2.0, (note - 69) / 12.0));
            };
            std::printf("  ..    filters at A3 (220 Hz saw, cutoff 3 kHz, half resonance): LADDER24 %.1f, CURTIS24 %.1f, ARP12 %.1f, LP24 %.1f dB\n",
                        at(57, 11), at(57, 12), at(57, 13), at(57, 1));
        }
        juce::String filters;
        auto worstFilter = -200.0;
        for (const auto& [name, type] : { std::pair<const char*, int> { "LADDER24", 11 }, { "CURTIS24", 12 }, { "ARP12", 13 }, { "LP24", 1 } })
        {
            const auto db = filtered(type);
            worstFilter = juce::jmax(worstFilter, db);
            filters << name << " " << fmt(db, 1) << " dB, ";
        }
        std::printf("  ..    alias below 15 kHz vs fundamental: DRIVE HARD full %.1f dB; saw alone %.1f dB; %s\n",
                    driveAlias, bare, filters.toRawUTF8());
        // A band-limited saw at full HARD drive: the realistic worst case (a
        // sine survey alone hid it - the saw's own upper harmonics alias too).
        double sawAlias = 0.0;
        {
            px3::Distortion drive;
            drive.prepare(48000.0);
            px3::DistortionSettings settings;
            settings.type = 1;
            settings.drive = 1.0f;
            settings.tight = 0.0f;
            settings.tone = 1.0f;
            settings.mix = 1.0f;
            std::vector<float> out;
            for (int i = 0; i < 8192 + 32768; ++i)
            {
                if (i % 256 == 0) { drive.updateForBlock(settings); }
                double v = 0.0;
                for (int h = 1; h * 440.0 < 22000.0; ++h) { v += std::sin(juce::MathConstants<double>::twoPi * h * 440.0 * i / 48000.0) / h; }
                float l = 0.0f, r = 0.0f;
                drive.processSampleFrame(static_cast<float>(0.55 * v), static_cast<float>(0.55 * v), l, r);
                if (i >= 8192) { out.push_back(l); }
            }
            sawAlias = aliasDb(out, 48000.0, 440.0);
        }
        // 8x polyphase-IIR oversampling with first-order ADAA inside it.
        // Before: 5 kHz sine -24 dB, 440 Hz saw -37 dB.
        check("Qa_DriveAliasingIsBelow60dBEverywhere", worstTypical < -70.0 && driveAlias < -60.0 && sawAlias < -60.0,
              "sines 220 Hz-3 kHz any drive: " + fmt(worstTypical, 1) + " dB; 5 kHz full HARD: " + fmt(driveAlias, 1)
                  + " dB; 440 Hz saw full HARD: " + fmt(sawAlias, 1) + " dB");

        // MIX blends two signals with the same phase: at half mix with the
        // clipper linear (tiny input, no drive) the stage is flat - no comb
        // from the oversampling filters' delay against the dry signal.
        {
            px3::Distortion drive;
            drive.prepare(48000.0);
            px3::DistortionSettings settings;
            settings.type = 1;
            settings.drive = 0.0f;
            settings.tight = 0.0f;
            settings.tone = 1.0f;
            settings.mix = 0.5f;
            settings.level = 0.5f;
            juce::String gains;
            auto worstDb = 0.0;
            for (const auto hz : { 200.0, 1000.0, 4000.0, 8000.0, 12000.0 })
            {
                double inE = 0.0, outE = 0.0;
                drive.reset();
                for (int i = 0; i < 48000; ++i)
                {
                    if (i % 256 == 0) { drive.updateForBlock(settings); }
                    const auto x = static_cast<float>(0.01 * std::sin(juce::MathConstants<double>::twoPi * hz * i / 48000.0));
                    float l = 0.0f, r = 0.0f;
                    drive.processSampleFrame(x, x, l, r);
                    if (i > 24000) { inE += static_cast<double>(x) * x; outE += static_cast<double>(l) * l; }
                }
                const auto db = 10.0 * std::log10(outE / inE);
                gains << fmt(hz, 0) << " Hz " << fmt(db, 2) << " dB, ";
                if (hz < 10000.0) { worstDb = juce::jmax(worstDb, std::abs(db)); }   // the TONE filter's top is 14 kHz
            }
            check("Qa_DriveMixDoesNotComb", worstDb < 0.5, "half MIX, linear: " + gains);
        }

        // Engaging and bypassing crossfade: no step larger than the signal's own.
        {
            px3::Distortion drive;
            drive.prepare(48000.0);
            px3::DistortionSettings settings;
            settings.type = 0;
            settings.drive = 0.5f;
            settings.mix = 0.0f;
            float previous = 0.0f;
            auto worstStep = 0.0f, steadyStep = 0.0f;
            for (int i = 0; i < 48000; ++i)
            {
                if (i % 256 == 0)
                {
                    settings.mix = (i / 12000) % 2 == 1 ? 1.0f : 0.0f;   // on at 12000, off at 24000, on at 36000
                    drive.updateForBlock(settings);
                }
                const auto x = static_cast<float>(0.3 * std::sin(juce::MathConstants<double>::twoPi * 220.0 * i / 48000.0));
                float l = 0.0f, r = 0.0f;
                drive.processSampleFrame(x, x, l, r);
                const auto step = std::abs(l - previous);
                previous = l;
                if (i > 100 && i < 11000) { steadyStep = juce::jmax(steadyStep, step); }
                if (i > 100) { worstStep = juce::jmax(worstStep, step); }
            }
            check("Qa_DriveEngagesWithoutAClick", worstStep < steadyStep * 3.0f,
                  "largest step " + fmt(worstStep, 4) + " against the dry signal's own " + fmt(steadyStep, 4));
        }

        check("Qa_AnalogFiltersAddNoAudibleAliasing", worstFilter < -40.0,
              "worst model at half resonance on a 3.5 kHz saw: " + fmt(worstFilter, 1) + " dB (saw alone " + fmt(bare, 1) + " dB)");
    }

    // ---- host state round-trips exactly ------------------------------------------
    // Save, load, save again: the second save must be the first, byte for byte.
    // A difference means loading changed something - a value nudged on the way
    // in, an ordering, a field that grows - and a host that compares state to
    // mark a project dirty would see every reload as an edit.
    {
        Processor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);
        juce::MemoryBlock first, second, third;
        processor.getStateInformation(first);
        processor.setStateInformation(first.getData(), static_cast<int>(first.getSize()));
        processor.getStateInformation(second);
        processor.setStateInformation(second.getData(), static_cast<int>(second.getSize()));
        processor.getStateInformation(third);
        juce::String where;
        if (second != third || first != second)
        {
            const auto a = Processor::getXmlFromBinary(first.getData(), static_cast<int>(first.getSize()));
            const auto b = Processor::getXmlFromBinary(second.getData(), static_cast<int>(second.getSize()));
            const auto ta = a != nullptr ? a->toString() : juce::String();
            const auto tb = b != nullptr ? b->toString() : juce::String();
            auto i = 0;
            while (i < juce::jmin(ta.length(), tb.length()) && ta[i] == tb[i]) { ++i; }
            where = "first difference at char " + juce::String(i) + ": ..." + ta.substring(juce::jmax(0, i - 80), i + 60)
                    + "  VS  ..." + tb.substring(juce::jmax(0, i - 80), i + 60);
        }
        check("Qa_HostStateRoundTripsExactly", first == second && second == third,
              first == second && second == third ? juce::String(static_cast<int>(first.getSize())) + " bytes, identical across two reloads" : where);
    }

    // ---- no knob reads out more than 2 decimals ------------------------------------
    // A continuous parameter with no formatter of its own fell back to JUCE's,
    // which prints 7 decimals: a knob read "0.8451094".
    {
        Processor processor;
        juce::StringArray long_;
        auto checked = 0;
        for (auto* p : processor.getParameters())
        {
            if (dynamic_cast<juce::AudioParameterFloat*>(p) == nullptr) { continue; }
            for (const auto v : { 0.0f, 0.1234567f, 0.8451094f, 1.0f })
            {
                const auto text = p->getText(v, 0);
                const auto dot = text.indexOfChar('.');
                auto digits = 0;
                if (dot >= 0)
                {
                    for (auto i = dot + 1; i < text.length() && juce::CharacterFunctions::isDigit(text[i]); ++i) { ++digits; }
                }
                ++checked;
                if (digits > 2) { long_.addIfNotAlreadyThere(p->getName(64) + " \"" + text + "\""); }
            }
        }
        check("Qa_NoKnobShowsMoreThanTwoDecimals", long_.isEmpty() && checked > 0,
              long_.isEmpty() ? juce::String(checked) + " readouts" : long_.joinIntoString(", ").substring(0, 400));
    }

    // ---- nothing leaves the plugin above full scale ---------------------------------
    // The output ceiling is the instrument's guarantee, and it is last. The
    // reverb's level match ran after it, scaling the whole master by up to
    // 1.45x a block at a time: every source, fader and the master at the top,
    // with the reverb and the Analog Engine on (both defaults), peaked 1.14.
    {
        Processor processor;
        makePlainPatch(processor);
        setParam(processor, "global.character.enabled", 1.0f);   // the default
        for (const auto* osc : { "voice.osc1", "voice.osc2", "voice.osc3" })
        {
            setParam(processor, juce::String(osc) + ".enabled", 1.0f);
            setChoice(processor, juce::String(osc) + ".mode", 1);   // SAW
        }
        setParam(processor, "voice.sub.enabled", 1.0f);
        for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
        {
            if (auto* p = findParameter(processor, juce::String("mix.") + id + ".level")) { p->setValueNotifyingHost(1.0f); }
            setParam(processor, juce::String("mix.") + id + ".send.fx", 0.5f);
        }
        if (auto* p = findParameter(processor, "mix.fx.level")) { p->setValueNotifyingHost(1.0f); }
        if (auto* p = findParameter(processor, "mix.master.level")) { p->setValueNotifyingHost(1.0f); }
        setParam(processor, "fx.reverb.enabled", 1.0f);
        setParam(processor, "fx.reverb.amount", 0.4f);
        std::vector<NoteEvent> chords;
        for (int bar = 0; bar < 6; ++bar)
        {
            for (const auto note : { 36, 43, 48, 52, 55, 60, 64, 67 })
            {
                chords.push_back({ bar * 16000, true, note, 1.0f });
                chords.push_back({ bar * 16000 + 12000, false, note, 0.0f });
            }
        }
        const auto capture = render(processor, 6 * 16000 + 48000, chords);
        check("Qa_NothingLeavesThePluginAboveFullScale", capture.peak() <= 1.0 && capture.isFinite(),
              "peak " + fmt(capture.peak(), 4) + " with every source, fader and the master at the top");
    }

    // ---- envelope modulation inside the voice does not depend on the host buffer -----
    // ENV routes into in-voice destinations (filter, oscillator pitch) were
    // applied once per host block, from the envelope value at the end of the
    // previous block - and as 0 for a note's whole first block. A filter pluck
    // or a pitch drop was a staircase whose step was the host's buffer: at
    // 1024 samples, 21 ms steps against a 30 ms decay.
    {
        const auto renderPluck = [](int blockSize, const juce::String& destination)
        {
            Processor processor;
            makePlainPatch(processor);
            setChoice(processor, "voice.osc1.mode", 1);   // SAW
            setParam(processor, "voice.filter1.enabled", 1.0f);
            setChoice(processor, "voice.filter1.type", 1);   // LP24
            setParam(processor, "voice.filter1.cutoff", 300.0f);
            setParam(processor, "mod.env1.enabled", 1.0f);
            setParam(processor, "mod.env1.amount", 1.0f);
            processor.getEnvelopeAttackParam(0).setValueNotifyingHost(processor.getEnvelopeAttackParam(0).convertTo0to1(0.001f));
            processor.getEnvelopeDecayParam(0).setValueNotifyingHost(processor.getEnvelopeDecayParam(0).convertTo0to1(0.03f));
            processor.getEnvelopeSustainParam(0).setValueNotifyingHost(0.0f);
            juce::String error;
            processor.setGraphRoute(0, { Processor::kLfoSourceCount, destination, px3::synth::ModulationPolarity::native,
                                         px3::synth::ModulationCurve::linear }, error);
            auto& depth = processor.getGraphRouteDepthParam(0);
            depth.setValueNotifyingHost(depth.convertTo0to1(destination.contains("pitch") ? 0.5f : 0.8f));
            processor.setPlayConfigDetails(0, 2, kSampleRate, blockSize);
            processor.prepareToPlay(kSampleRate, blockSize);
            juce::AudioBuffer<float> buffer(2, blockSize);
            std::vector<float> out;
            for (int position = 0; position < 9600; position += blockSize)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                if (position == 0) { midi.addEvent(juce::MidiMessage::noteOn(1, 45, 0.9f), 0); }
                processor.processBlock(buffer, midi);
                for (int i = 0; i < blockSize; ++i) { out.push_back(buffer.getSample(0, i)); }
            }
            return out;
        };
        // Level-free measures, so the voice-count gain stage (which follows
        // load once per host block and moves the onset level by itself) does
        // not count: the filter by its brightness, the pitch by its period.
        const auto brightness = [](const std::vector<float>& x, int from)
        {
            // Spectral centroid of a 512-sample Hann window, in Hz.
            constexpr int order = 9;
            constexpr int n = 1 << order;
            std::vector<float> data(static_cast<std::size_t>(n) * 2, 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const auto w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * static_cast<float>(i) / n);
                data[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(from + i)] * w;
            }
            juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(data.data());
            double weighted = 0.0, total = 0.0;
            for (int k = 1; k < n / 2; ++k)
            {
                const auto m = static_cast<double>(data[static_cast<std::size_t>(k)]);
                weighted += k * 48000.0 / n * m;
                total += m;
            }
            return total > 0.0 ? weighted / total : 0.0;
        };
        const auto pitchAt = [](const std::vector<float>& x, int from)
        {
            return estimateFrequency(x, from, from + 960, 40.0, 2000.0);
        };
        auto worstBrightness = 0.0, worstCents = 0.0;
        {
            const auto small = renderPluck(64, "voice.filter1.cutoff");
            const auto large = renderPluck(1024, "voice.filter1.cutoff");
            for (int from = 0; from + 512 < 2400; from += 96)   // the pluck's sweep, its first 50 ms
            {
                const auto a = brightness(small, from);
                const auto b2 = brightness(large, from);
                worstBrightness = juce::jmax(worstBrightness, std::abs(std::log2(a / juce::jmax(1.0, b2))) * 12.0);
            }
        }
        {
            const auto small = renderPluck(64, "voice.osc1.pitch.mod");
            const auto large = renderPluck(1024, "voice.osc1.pitch.mod");
            for (int from = 96; from + 960 < 7200; from += 192)
            {
                const auto a = pitchAt(small, from);
                const auto b2 = pitchAt(large, from);
                if (a > 0.0 && b2 > 0.0) { worstCents = juce::jmax(worstCents, std::abs(1200.0 * std::log2(a / b2))); }
            }
        }
        // Brightness in semitones of centroid shift; pitch in cents.
        check("Qa_EnvelopeModulationIsIndependentOfTheHostBuffer", worstBrightness < 0.5 && worstCents < 15.0,
              "64- vs 1024-sample buffers, over the pluck: brightness differs by at most " + fmt(worstBrightness, 2)
                  + " semitones of centroid, pitch by at most " + fmt(worstCents, 1) + " cents");
    }

    // ---- a freshly loaded preset is not marked as edited ------------------------------
    // Reported: open the standalone, press > to load Dial Tone, and the name
    // immediately shows "*". Whatever writes the state after the load is
    // listed by parameter.
    {
        Processor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);
        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
        juce::StringArray dirtyAfter;
        juce::String changed;
        if (editor != nullptr && editor->debugTopMenuBar() != nullptr)
        {
            editor->setSize(1518, 938);
            for (int tick = 0; tick < 10; ++tick) { editor->debugTimerTick(); }
            for (int press = 0; press < 4; ++press)   // the first few presets, as a user steps through them
            {
                // The button's own handler, run now: triggerClick() posts the click,
                // and under load the snapshot below could be taken before it ran.
                if (auto& next = editor->debugTopMenuBar()->getPresetNextButton(); next.onClick) { next.onClick(); }
                juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                const auto loaded = processor.createPresetStateTree().createXml()->toString();
                juce::AudioBuffer<float> buffer(2, kBlockSize);
                for (int tick = 0; tick < 20; ++tick)
                {
                    juce::MidiBuffer midi;
                    buffer.clear();
                    processor.processBlock(buffer, midi);
                    editor->debugTimerTick();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
                }
                const auto now = processor.createPresetStateTree().createXml()->toString();
                if (editor->debugPresetDisplayName().endsWithChar('*') || loaded != now)
                {
                    dirtyAfter.add(editor->debugPresetDisplayName());
                    if (changed.isEmpty())
                    {
                        // The first differing PARAMETER line.
                        juce::StringArray a, b;
                        a.addLines(loaded);
                        b.addLines(now);
                        for (int i = 0; i < juce::jmin(a.size(), b.size()); ++i)
                        {
                            if (a[i] != b[i]) { changed = a[i].trim() + "  ->  " + b[i].trim(); break; }
                        }
                    }
                }
            }
        }
        check("Qa_ALoadedPresetIsNotMarkedEdited", editor != nullptr && dirtyAfter.isEmpty(),
              dirtyAfter.isEmpty() ? juce::String("4 presets stepped through, none marked edited")
                                   : "marked edited: " + dirtyAfter.joinIntoString(", ") + "; first change " + changed);
    }

    // ---- switching away from unsaved edits asks first ------------------------------
    // SAVE / DON'T SAVE / CANCEL before any preset switch that would discard
    // edits: the arrows, the sheet's LOAD and import all go through it.
    {
        Processor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);
        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
        juce::StringArray wrong;
        if (editor != nullptr && editor->debugTopMenuBar() != nullptr)
        {
            editor->setSize(1518, 938);
            auto& prompt = editor->debugUnsavedPrompt();
            const auto next = [editor]
            {
                if (auto& b = editor->debugTopMenuBar()->getPresetNextButton(); b.onClick) { b.onClick(); }
            };
            const auto name = [editor] { return editor->debugPresetDisplayName(); };
            const auto edit = [&processor](float hz) { setParam(processor, "voice.filter1.cutoff", hz); };

            next();                                   // a clean load: no question
            if (prompt.isAsking()) { wrong.add("asked on a clean switch"); prompt.answer(UnsavedChangesPrompt::Answer::cancel); }
            const auto first = name();

            edit(432.0f);
            // Opening the question has to feel instant. The backdrop was 48
            // resampled full-window draws: over 2 s into a Retina (2x)
            // context. A coarse guard at the scale of that bug, not a benchmark.
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                const auto snap = editor->createComponentSnapshot(editor->getLocalBounds());
                juce::Image out(juce::Image::ARGB, snap.getWidth() * 2, snap.getHeight() * 2, true);
                {
                    juce::Graphics g(out);
                    g.addTransform(juce::AffineTransform::scale(2.0f));
                    px3::ui::paintModalBackdrop(g, editor->getLocalBounds(), {}, snap, 0.0f);
                }
                const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
                check("Qa_ASheetBackdropOpensQuickly", ms < 400.0,
                      "snapshot and blurred backdrop into a 2x context: " + juce::String(ms, 1) + " ms");
            }
            next();
            if (! prompt.isAsking()) { wrong.add("did not ask with edits"); }
            if (name() != first + "*") { wrong.add("switched before the answer: " + name()); }
            if (prompt.debugMessage() != "Do you want to save your changes?") { wrong.add("message reads " + prompt.debugMessage()); }

            prompt.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            if (prompt.isAsking() || name() != first + "*") { wrong.add("CANCEL (Escape) did not keep the edits: " + name()); }

            // From the sheet: CANCEL leaves it open, DON'T SAVE loads and closes it.
            if (auto& b = editor->debugTopMenuBar()->getPresetNameButton(); b.onClick) { b.onClick(); }
            auto& list = editor->debugPresetListBox();
            list.selectRow(juce::jmin(3, editor->debugPresetRowCount() - 1));
            const auto load = [editor] { if (auto& b = editor->debugPresetLoadButton(); b.onClick) { b.onClick(); } };
            load();
            if (! prompt.isAsking()) { wrong.add("LOAD PRESET did not ask"); }
            prompt.answer(UnsavedChangesPrompt::Answer::cancel);
            if (! editor->debugPresetBrowserVisible()) { wrong.add("CANCEL closed the preset sheet"); }
            if (name() != first + "*") { wrong.add("CANCEL from the sheet lost the edits: " + name()); }
            load();
            if (prompt.isAsking()) { prompt.answer(UnsavedChangesPrompt::Answer::discard); }
            else { wrong.add("did not ask a second time"); }
            const auto second = name();
            if (second == first || second.endsWithChar('*')) { wrong.add("DON'T SAVE did not load cleanly: " + second); }
            if (editor->debugPresetBrowserVisible()) { wrong.add("the sheet stayed open after loading"); }

            const auto card = prompt.debugCard();
            if (! card.isEmpty() && ! editor->getLocalBounds().contains(card)) { wrong.add("card outside the window"); }
        }
        else { wrong.add("no editor"); }
        check("Qa_UnsavedEditsAskBeforeAPresetSwitch", wrong.isEmpty(),
              wrong.isEmpty() ? juce::String("clean switch silent; edited switch asks, CANCEL keeps, DON'T SAVE loads")
                              : wrong.joinIntoString("; "));
    }

    // ---- the EQ and COMP sheets drag by their title band ----------------------------
    {
        Processor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);
        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
        juce::StringArray wrong;
        if (editor != nullptr)
        {
            editor->setSize(1518, 938);
            for (const auto wantsEq : { true, false })
            {
                const juce::String which = wantsEq ? "EQ" : "COMP";
                editor->debugOpenBusInsert(PX3SynthAudioProcessor::dryBusInsert, wantsEq);
                px3::ui::BusInsertOverlay* sheet = nullptr;
                for (auto* child : editor->getChildren())
                {
                    if (auto* s = dynamic_cast<px3::ui::BusInsertOverlay*>(child); s != nullptr && s->isVisible()) { sheet = s; }
                }
                if (sheet == nullptr) { wrong.add(which + " did not open"); continue; }

                const auto event = [sheet](juce::Point<float> at)
                {
                    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(),
                                            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, sheet, sheet, juce::Time::getCurrentTime(),
                                            at, juce::Time::getCurrentTime(), 1, false);
                };
                const auto start = sheet->getPosition();
                const auto band = sheet->debugTitleBand();
                const auto grab = juce::Point<float>(band.getX() + band.getWidth() * 0.4f, band.getCentreY());

                sheet->mouseDown(event(grab));
                sheet->mouseDrag(event(grab + juce::Point<float>(60.0f, 40.0f)));
                sheet->mouseUp(event(grab + juce::Point<float>(60.0f, 40.0f)));
                if (sheet->getPosition() != start + juce::Point<int>(60, 40))
                {
                    wrong.add(which + " title drag moved it " + (sheet->getPosition() - start).toString());
                }

                // Never off the window, however far the drag goes.
                const auto here = sheet->getPosition();
                const auto grab2 = grab;
                sheet->mouseDown(event(grab2));
                sheet->mouseDrag(event(grab2 + juce::Point<float>(5000.0f, 5000.0f)));
                sheet->mouseUp(event(grab2));
                if (! editor->getLocalBounds().contains(sheet->getBounds())) { wrong.add(which + " dragged off the window"); }
                juce::ignoreUnused(here);

                // The face is not a handle.
                const auto parked = sheet->getPosition();
                const auto body = juce::Point<float>(band.getCentreX(), band.getBottom() + 40.0f);
                sheet->mouseDown(event(body));
                sheet->mouseDrag(event(body + juce::Point<float>(-30.0f, -30.0f)));
                sheet->mouseUp(event(body));
                if (sheet->getPosition() != parked) { wrong.add(which + " moved from a drag on its face"); }

                editor->debugCloseBusInsert();
            }
        }
        else { wrong.add("no editor"); }
        check("Qa_EqAndCompSheetsDragByTheirTitle", wrong.isEmpty(),
              wrong.isEmpty() ? juce::String("both sheets follow a title drag, stay in the window, ignore a face drag")
                              : wrong.joinIntoString("; "));
    }

    // ---- the editor leaves voice.amp.enabled alone --------------------------------
    // AMP ENV has no switch on screen, and the editor wrote the parameter back
    // on every tick: host automation of it fought the open window. The voice
    // now treats AMP ENV as always on, and the editor never writes it.
    {
        Processor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);
        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
        setParam(processor, "voice.amp.enabled", 0.0f);
        if (editor != nullptr)
        {
            editor->setSize(1518, 938);
            for (int tick = 0; tick < 10; ++tick) { editor->debugTimerTick(); }
        }
        const auto after = getParamValue(processor, "voice.amp.enabled");
        check("Qa_EditorLeavesTheAmpEnableAlone", editor != nullptr && after < 0.5f,
              "voice.amp.enabled set off by the host reads " + fmt(after, 0) + " after 10 editor ticks");
    }

    // ---- animated displays repaint only themselves ---------------------------------
    // With animations on, the idle editor took 27% of a core and a held chord
    // saturated the message thread: every wave frame repainted its whole card
    // through an image cache. The waves are opaque layers now, and nothing
    // around them is image-cached (PX3Bench uinative is the measurement).
    {
        Processor processor;
        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        base->setSize(1518, 938);
        int layers = 0, translucent = 0, cachedAncestors = 0;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (child == nullptr) { continue; }
                if (dynamic_cast<px3::ui::AnimatedDisplay*>(child) != nullptr)
                {
                    ++layers;
                    if (! child->isOpaque()) { ++translucent; }
                    for (auto* p = child->getParentComponent(); p != nullptr; p = p->getParentComponent())
                    {
                        if (p->getCachedComponentImage() != nullptr) { ++cachedAncestors; break; }
                    }
                }
                walk(*child);
            }
        };
        walk(*base);
        check("Qa_AnimatedDisplaysAreOpaqueUncachedLayers", layers >= 7 && translucent == 0 && cachedAncestors == 0,
              juce::String(layers) + " display layers (3 OSC, SUB, 3 LFO), " + juce::String(translucent)
                  + " translucent, " + juce::String(cachedAncestors) + " inside an image-cached parent");
    }
}
} // namespace px3tests
