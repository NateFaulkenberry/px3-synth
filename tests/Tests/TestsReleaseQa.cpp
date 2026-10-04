#include "TestSupport.h"
#include "LfoGenerator.h"
#include "OscillatorUnit.h"

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
}
} // namespace px3tests
