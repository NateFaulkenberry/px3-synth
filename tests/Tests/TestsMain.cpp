#include "TestSupport.h"

// The runner, and the exploratory sweep reports it prints. Those measure and
// print rather than asserting anything, which is why they live beside main
// rather than in a suite.

namespace px3tests
{


// ============================================================================
// ARTIFACT SCAN  (measurement mode, not pass/fail)
// ============================================================================

namespace artifactscan
{
// A discontinuity is a sample-to-sample jump the SIGNAL cannot explain, so it
// has to be measured against the local slope rather than against an absolute
// threshold. A loud passage legitimately has large deltas; a click is a delta
// that does not belong to its neighbourhood.
struct Worst
{
    double ratio { 0.0 };   // jump / local RMS slope
    int index { 0 };
    double jump { 0.0 };
};

Worst worstDiscontinuity(const std::vector<float>& x, int skip)
{
    Worst worst;
    if (static_cast<int>(x.size()) < skip + 256)
    {
        return worst;
    }

    // Local slope, measured over a window either side, excluding the sample
    // under test so a click cannot raise its own reference.
    constexpr int kWindow = 96;

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
        if (reference < 1.0e-7)
        {
            continue;
        }

        const auto ratio = jump / reference;
        if (ratio > worst.ratio)
        {
            worst = { ratio, i, jump };
        }
    }

    return worst;
}
} // namespace artifactscan

void scanDoomLucyArtifacts()
{
    using namespace artifactscan;

    std::printf("\nDOOM / LUCY ARTIFACT SCAN\n");
    std::printf("  Feeds a steady tone and looks for sample-to-sample jumps the signal\n");
    std::printf("  itself cannot explain. The ratio is the jump against the local slope,\n");
    std::printf("  so a loud passage does not flag and a click in a quiet one does.\n");
    std::printf("  Anything above about 8 is worth listening to.\n\n");
    std::printf("  %-42s %8s %10s %10s\n", "configuration", "ratio", "jump", "at (s)");
    std::printf("  %-42s %8s %10s %10s\n", "------------------------------------------",
                "--------", "----------", "----------");

    const auto seconds = 7.0;
    const auto total = static_cast<int>(kSampleRate * seconds);

    auto tone = [total](double hz)
    {
        std::vector<float> x;
        x.reserve(static_cast<std::size_t>(total));
        for (int i = 0; i < total; ++i)
        {
            x.push_back(0.45f * static_cast<float>(
                std::sin(juce::MathConstants<double>::twoPi * hz * i / kSampleRate)));
        }
        return x;
    };

    const auto input = tone(220.0);

    auto report = [&](const char* label, const std::vector<float>& out)
    {
        // The first two seconds are the engine filling. RELAY's taps run out to
        // about 0.9s, so the edge between an empty buffer and the signal is
        // still propagating through them well past half a second - and that edge
        // is a start-up transient, not something anyone plays through.
        const auto worst = worstDiscontinuity(out, static_cast<int>(kSampleRate * 2.0));
        std::printf("  %-42s %8.1f %10.6f %10.3f\n", label, worst.ratio, worst.jump,
                    worst.index / kSampleRate);
    };

    // ---- DOOM ---------------------------------------------------------------
    {
        static const char* loopNames[] = { "BURST", "RADIO", "MASK" };
        static const char* wetNames[] = { "SOUP", "RELAY", "FLIP" };

        for (int wet = 0; wet < 3; ++wet)
        {
            DoomSettings s;
            s.enabled = true;
            s.mix = 1.0f;
            s.wetModeIndex = wet;
            s.wetTime = 0.5f;
            s.wetModify = 0.5f;
            s.balance = 1.0f;

            px3::Doom doom;
            doom.prepare(kSampleRate);
            doom.setSeed(4242u);
            doom.updateForBlock(s);

            std::vector<float> out;
            out.reserve(input.size());
            for (const auto sample : input)
            {
                float l = 0.0f;
                float r = 0.0f;
                doom.processSampleFrame(sample, sample, l, r);
                out.push_back(l);
            }
            report((juce::String("DOOM wet ") + wetNames[wet]).toRawUTF8(), out);
        }

        for (int loop = 0; loop < 3; ++loop)
        {
            for (const auto modify : { 0.0f, 0.5f, 1.0f })
            {
                DoomSettings s;
                s.enabled = true;
                s.mix = 1.0f;
                s.loopActive = true;
                s.wetActive = false;
                s.loopModeIndex = loop;
                s.loopModify = modify;
                s.loopLength = 0.5f;
                s.balance = 0.0f;

                px3::Doom doom;
                doom.prepare(kSampleRate);
                doom.setSeed(4242u);

                // Listen first, then engage: the looper captures what already
                // happened, so engaging it at zero captures silence.
                auto listening = s;
                listening.loopActive = false;
                doom.updateForBlock(listening);
                for (const auto sample : input)
                {
                    float l = 0.0f;
                    float r = 0.0f;
                    doom.processSampleFrame(sample, sample, l, r);
                }
                doom.updateForBlock(s);

                std::vector<float> out;
                out.reserve(input.size());
                for (const auto sample : input)
                {
                    float l = 0.0f;
                    float r = 0.0f;
                    doom.processSampleFrame(sample, sample, l, r);
                    out.push_back(l);
                }
                report((juce::String("DOOM loop ") + loopNames[loop]
                        + " modify " + juce::String(modify, 1)).toRawUTF8(), out);
            }
        }

        for (const auto clock : { 0.0f, 0.3f, 0.6f, 1.0f })
        {
            DoomSettings s;
            s.enabled = true;
            s.mix = 1.0f;
            s.clock = clock;
            s.wetTime = 0.5f;
            s.balance = 1.0f;

            px3::Doom doom;
            doom.prepare(kSampleRate);
            doom.setSeed(4242u);
            doom.updateForBlock(s);

            std::vector<float> out;
            out.reserve(input.size());
            for (const auto sample : input)
            {
                float l = 0.0f;
                float r = 0.0f;
                doom.processSampleFrame(sample, sample, l, r);
                out.push_back(l);
            }
            report((juce::String("DOOM clock ") + juce::String(clock, 1)).toRawUTF8(), out);
        }
    }

    // ---- LUCY ---------------------------------------------------------------
    {
        static const char* modeNames[] = { "STANDARD", "INVERSE", "JITTER" };
        static const char* packetNames[] = { "CLEAN", "LOSS", "REPEAT" };

        for (int mode = 0; mode < 3; ++mode)
        {
            for (int packets = 0; packets < 3; ++packets)
            {
                px3::LucyUserParameters s;
                s.enabled = true;
                s.global = 1.0f;
                s.mode = static_cast<px3::LucyLossMode>(mode);
                s.packets = static_cast<px3::LucyPacketMode>(packets);
                s.loss = 0.6f;
                s.speed = 0.5f;

                px3::Lucy lucy;
                lucy.prepare(kSampleRate);
                lucy.setSeed(4242u);
                lucy.updateForBlock(s);

                std::vector<float> out;
                out.reserve(input.size());
                for (const auto sample : input)
                {
                    float l = 0.0f;
                    float r = 0.0f;
                    lucy.processSampleFrame(sample, sample, l, r);
                    out.push_back(l);
                }
                report((juce::String("LUCY ") + modeNames[mode] + " / " + packetNames[packets]).toRawUTF8(),
                       out);
            }
        }

        for (const auto gate : { 0.2f, 0.5f, 0.8f })
        {
            px3::LucyUserParameters s;
            s.enabled = true;
            s.global = 1.0f;
            s.loss = 0.4f;
            s.gate = true;
            s.gateThreshold = gate;

            px3::Lucy lucy;
            lucy.prepare(kSampleRate);
            lucy.setSeed(4242u);
            lucy.updateForBlock(s);

            std::vector<float> out;
            out.reserve(input.size());
            for (const auto sample : input)
            {
                float l = 0.0f;
                float r = 0.0f;
                lucy.processSampleFrame(sample, sample, l, r);
                out.push_back(l);
            }
            report((juce::String("LUCY gate cutoff ") + juce::String(gate, 1)).toRawUTF8(), out);
        }

        for (const auto freezeSlushy : { false, true })
        {
            px3::LucyUserParameters s;
            s.enabled = true;
            s.global = 1.0f;
            s.loss = 0.5f;
            s.freeze = freezeSlushy ? px3::LucyFreezeMode::slushy : px3::LucyFreezeMode::solid;

            px3::Lucy lucy;
            lucy.prepare(kSampleRate);
            lucy.setSeed(4242u);
            lucy.updateForBlock(s);

            std::vector<float> out;
            out.reserve(input.size());
            for (const auto sample : input)
            {
                float l = 0.0f;
                float r = 0.0f;
                lucy.processSampleFrame(sample, sample, l, r);
                out.push_back(l);
            }
            report((juce::String("LUCY freeze ") + (freezeSlushy ? "slushy" : "solid")).toRawUTF8(), out);
        }
    }

    std::printf("\n");
}

} // namespace px3tests

using namespace px3tests;

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String filter = argc > 1 ? argv[1] : "";
    auto wants = [&filter](const char* name)
    {
        return filter.isEmpty() || filter == name;
    };

    std::printf("\nPX3 COMPONENT TESTS  (%.0f Hz, %d-sample blocks, shipping build)\n",
                kSampleRate, kBlockSize);

    if (filter == "glcheck")
    {
        // Does the shader actually compile on this machine's driver? Nothing
        // else in the suite can answer that: a console test has no window, so
        // one is made here on purpose.
        std::printf("\nGPU SHADER CHECK\n\n");

        struct Host final : public juce::DocumentWindow
        {
            Host() : juce::DocumentWindow("px3 gl", juce::Colours::black, 0)
            {
                renderer.setSize(320, 200);
                setContentNonOwned(&renderer, true);
                setOpaque(true);
                setVisible(true);
                setTopLeftPosition(-4000, -4000);   // off-screen, but real
            }
            void closeButtonPressed() override {}
            Wavetable3DRenderer renderer;
        };

        auto host = std::make_unique<Host>();

        px3::WavetableDisplay display;
        display.name = "check";
        for (int f = 0; f < 16; ++f)
        {
            std::vector<float> row(128, 0.0f);
            for (std::size_t i = 0; i < row.size(); ++i)
            {
                row[i] = std::sin(juce::MathConstants<float>::twoPi
                                  * static_cast<float>(f + 1)
                                  * static_cast<float>(i)
                                  / static_cast<float>(row.size()));
            }
            display.frames.push_back(std::move(row));
        }
        host->renderer.setPixelAudit(true);
        host->renderer.setDisplay(display);
        host->renderer.setPosition(0.4f);

        // The environment is off for this check, and has to be. The lit-pixel
        // count is "how much of the frame differs from the cleared background",
        // which is a measurement of whether the STACK drew - and the
        // environment lifts almost every pixel off the clear colour, which took
        // the figure from 31% to 82% without a single extra ribbon being drawn.
        // See the envcheck mode for the environment's own measurements.
        host->renderer.setEnvironmentEnabled(false);

        // The GL thread needs the message loop running to get going.
        const auto deadline = juce::Time::getMillisecondCounter() + 4000;
        while (juce::Time::getMillisecondCounter() < deadline
               && ! host->renderer.isRendering()
               && host->renderer.getShaderError().isEmpty())
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        }

        const auto error = host->renderer.getShaderError();
        const auto rendering = host->renderer.isRendering();

        std::printf("  rendering:    %s\n", rendering ? "YES" : "no");
        std::printf("  shader error: %s\n", error.isEmpty() ? "(none)" : error.toRawUTF8());

        // A few frames with the audit on, then ask what actually reached the
        // framebuffer. "It rendered" and "something is visible" are different
        // claims and only the second one matters.
        for (int i = 0; i < 10; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        }
        const auto lit = host->renderer.getLitPixelCount();
        const auto audited = host->renderer.getAuditedPixelCount();
        std::printf("  lit pixels:   %d of %d (%.1f%% of the framebuffer)\n",
                    lit, audited,
                    audited > 0 ? 100.0 * lit / audited : 0.0);

        // A few more frames, which is where a bad draw range would take the
        // process down rather than merely fail to draw.
        for (int i = 0; i < 20; ++i)
        {
            host->renderer.setPosition(static_cast<float>(i) / 20.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(16);
        }
        std::printf("  survived 20 more frames with the scan moving\n");

        host.reset();
        const auto visible = lit > 200;
        std::printf("\n  %s\n\n", rendering && error.isEmpty() && visible
                                    ? "GPU renderer is working."
                                    : "GPU renderer is NOT drawing anything.");
        return rendering && error.isEmpty() && visible ? 0 : 1;
    }

    if (filter == "ampenv")
    {
        // The AMP ENV, measured from rendered AUDIO rather than from the model.
        //
        // The model tests pass; the report is about what you hear. So this sets
        // the four ADSR parameters, plays a note, and reads the envelope back
        // out of the signal - time to peak, the level it settles at, and how
        // long the tail takes after note-off.
        std::printf("\nAMP ENVELOPE, MEASURED FROM THE AUDIO\n\n");

        struct Shape { double peakSeconds; double sustainLevel; double releaseSeconds;
                       double peakLevel; };

        const auto measure = [](float attack, float hold, float decay,
                                float sustain, float release)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "ampAttack", attack);
            juce::ignoreUnused(hold);   // AMP ENV has no hold stage
            setParam(processor, "ampDecay", decay);
            setParam(processor, "ampSustain", sustain);
            setParam(processor, "ampRelease", release);

            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

            const auto secondsPerBlock = static_cast<double>(kBlockSize) / kSampleRate;
            const auto heldBlocks = static_cast<int>(3.0 / secondsPerBlock);

            std::vector<double> held;
            for (int block = 0; block < heldBlocks; ++block)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();
                held.push_back(buffer.getMagnitude(0, kBlockSize));
            }

            Shape shape {};
            auto peak = 0.0;
            std::size_t peakBlock = 0;
            for (std::size_t i = 0; i < held.size(); ++i)
            {
                if (held[i] > peak) { peak = held[i]; peakBlock = i; }
            }
            shape.peakLevel = peak;
            shape.peakSeconds = static_cast<double>(peakBlock + 1) * secondsPerBlock;

            // Where it settles: the last half second of the held note.
            auto tail = 0.0;
            auto tailCount = 0;
            for (std::size_t i = held.size() > 45 ? held.size() - 45 : 0; i < held.size(); ++i)
            {
                tail += held[i]; ++tailCount;
            }
            const auto settled = tailCount > 0 ? tail / tailCount : 0.0;
            shape.sustainLevel = peak > 1.0e-9 ? settled / peak : 0.0;

            // Release: note-off, then time until the tail is 60 dB down on
            // where it started.
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            const auto startOfRelease = settled;
            const auto floorLevel = startOfRelease * 0.001;
            auto releaseBlocks = 0;
            for (int block = 0; block < static_cast<int>(12.0 / secondsPerBlock); ++block)
            {
                buffer.clear();
                processor.processBlock(buffer, off);
                off.clear();
                ++releaseBlocks;
                if (buffer.getMagnitude(0, kBlockSize) <= floorLevel) { break; }
            }
            shape.releaseSeconds = static_cast<double>(releaseBlocks) * secondsPerBlock;
            return shape;
        };

        struct Case { const char* label; float a, h, d, s, r; };
        const Case cases[] = {
            { "fast A, full S",     0.010f, 0.0f, 0.100f, 1.00f, 0.100f },
            { "slow A, full S",     0.500f, 0.0f, 0.100f, 1.00f, 0.100f },
            { "fast A, half S",     0.010f, 0.0f, 0.200f, 0.50f, 0.100f },
            { "fast A, zero S",     0.010f, 0.0f, 0.300f, 0.00f, 0.100f },
            { "long release",       0.010f, 0.0f, 0.100f, 1.00f, 2.000f },
        };

        std::printf("  %-18s %8s %8s  %8s %8s %8s  %8s %8s\n",
                    "case", "attack", "peak at", "sustain", "peak abs", "held abs",
                    "release", "tail");
        for (const auto& c : cases)
        {
            const auto shape = measure(c.a, c.h, c.d, c.s, c.r);
            std::printf("  %-18s %8.3f %8.3f  %8.2f %8.4f %8.4f  %8.3f %8.3f\n",
                        c.label, c.a, shape.peakSeconds, c.s,
                        shape.peakLevel, shape.peakLevel * shape.sustainLevel,
                        c.r, shape.releaseSeconds);
        }
        // Where does the overshoot begin? Sustain is 1.0 throughout, so the
        // envelope should be flat after the attack and peak == held.
        std::printf("\n  attack sweep, sustain 1.0 (peak should equal held):\n");
        std::printf("  %10s %10s %10s %8s\n", "attack", "peak abs", "held abs", "ratio");
        for (const auto attack : { 0.001f, 0.005f, 0.010f, 0.020f, 0.040f,
                                   0.080f, 0.160f, 0.320f })
        {
            const auto shape = measure(attack, 0.0f, 0.100f, 1.0f, 0.100f);
            const auto held = shape.peakLevel * shape.sustainLevel;
            std::printf("  %10.3f %10.4f %10.4f %8.3f%s\n",
                        attack, shape.peakLevel, held,
                        held > 1.0e-9 ? shape.peakLevel / held : 0.0,
                        (held > 1.0e-9 && shape.peakLevel / held > 1.05) ? "  <- overshoot" : "");
        }
        // The envelope ALONE, with no oscillator in the way. If the overshoot
        // is in the envelope it shows here; if it is the waveform's onset, this
        // is flat and the audio measurement was measuring the wrong thing.
        std::printf("\n  the AmpEnvelope on its own, sustain 1.0:\n");
        std::printf("  %10s %10s %10s %8s\n", "attack", "peak", "settled", "ratio");
        for (const auto attack : { 0.001f, 0.010f, 0.040f, 0.080f, 0.320f })
        {
            AmpEnvelope envelope;
            envelope.prepare(kSampleRate);
            EnvelopeSettings settings;
            settings.attackSeconds = attack;
            settings.decaySeconds = 0.100f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = 0.100f;
            envelope.setSettings(settings);
            envelope.noteOn();

            auto peak = 0.0f;
            auto settled = 0.0f;
            const auto samples = static_cast<int>(kSampleRate * 2.0);
            for (int i = 0; i < samples; ++i)
            {
                const auto value = envelope.getNextSample();
                peak = juce::jmax(peak, value);
                if (i > samples - 1000) { settled = value; }
            }
            std::printf("  %10.3f %10.4f %10.4f %8.3f%s\n",
                        attack, peak, settled,
                        settled > 1.0e-9f ? peak / settled : 0.0f,
                        (settled > 1.0e-9f && peak / settled > 1.05f) ? "  <- overshoot" : "");
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "attackpop")
    {
        // A pop at note-on under a LONG attack.
        //
        // With a one-second attack the envelope is under 0.001 for the first
        // millisecond, so nothing audible should be there at all. A pop means
        // something in the voice is NOT being scaled by the envelope - so this
        // measures the signal sample by sample rather than by peak, because a
        // discontinuity is a step between neighbours and a peak reading cannot
        // see one.
        std::printf("\nATTACK POP\n\n");

        const auto render = [](float attack, int samples, bool plain)
        {
            PX3SynthAudioProcessor processor;

            // Factory defaults are what the plugin actually loads with, and
            // makePlainPatch turns off most of the instrument - so a transient
            // that only the full signal path produces is invisible under it.
            if (plain) { makePlainPatch(processor); }
            setParam(processor, "ampAttack", attack);
            setParam(processor, "ampDecay", 0.100f);
            setParam(processor, "ampSustain", 1.00f);
            setParam(processor, "ampRelease", 0.100f);

            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

            std::vector<float> trace;
            trace.reserve(static_cast<std::size_t>(samples));
            while (static_cast<int>(trace.size()) < samples)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();
                for (int i = 0; i < kBlockSize && static_cast<int>(trace.size()) < samples; ++i)
                {
                    trace.push_back(buffer.getSample(0, i));
                }
            }
            return trace;
        };

        for (const auto plain : { true, false })
        {
            std::printf("  %s\n", plain ? "stripped patch:" : "FACTORY DEFAULTS:");
        for (const auto attack : { 1.000f, 0.500f, 0.100f, 0.010f })
        {
            const auto trace = render(attack, static_cast<int>(kSampleRate * 0.05), plain);

            // The envelope's own value, for comparison: what the signal SHOULD
            // be bounded by.
            AmpEnvelope reference;
            reference.prepare(kSampleRate);
            EnvelopeSettings settings;
            settings.attackSeconds = attack;
            settings.decaySeconds = 0.100f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = 0.100f;
            reference.setSettings(settings);
            reference.noteOn();

            auto worstStep = 0.0f;
            auto worstAt = 0;
            for (std::size_t i = 1; i < trace.size(); ++i)
            {
                const auto step = std::abs(trace[i] - trace[i - 1]);
                if (step > worstStep) { worstStep = step; worstAt = static_cast<int>(i); }
            }

            auto envAtWorst = 0.0f;
            for (int i = 0; i <= worstAt; ++i) { envAtWorst = reference.getNextSample(); }

            std::printf("  attack %5.3f s: first sample %+.6f, largest step %.6f at sample %d "
                        "(%.2f ms), envelope there %.6f\n",
                        attack, trace.empty() ? 0.0f : trace[0], worstStep, worstAt,
                        1000.0 * worstAt / kSampleRate, envAtWorst);
        }
        std::printf("\n");
        }

        // A SECOND note, after the first has been released. This is what a
        // player actually does, and it is where a voice gets reused with its
        // envelope part way through a release.
        std::printf("  a second note while the first is still releasing:\n");
        for (const auto gapSeconds : { 0.05, 0.20, 0.60 })
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "ampAttack", 1.000f);
            setParam(processor, "ampDecay", 0.100f);
            setParam(processor, "ampSustain", 1.00f);
            setParam(processor, "ampRelease", 0.500f);

            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer midi;

            const auto blocksFor = [](double seconds)
            {
                return static_cast<int>(seconds * kSampleRate / kBlockSize);
            };

            // Note on, hold, note off.
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            for (int b = 0; b < blocksFor(0.8); ++b)
            {
                buffer.clear(); processor.processBlock(buffer, midi); midi.clear();
            }
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            for (int b = 0; b < blocksFor(gapSeconds); ++b)
            {
                buffer.clear(); processor.processBlock(buffer, midi); midi.clear();
            }

            // The level just before the second note, then the second note.
            buffer.clear(); processor.processBlock(buffer, midi);
            const auto levelBefore = buffer.getMagnitude(0, kBlockSize);

            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            std::vector<float> after;
            for (int b = 0; b < blocksFor(0.05); ++b)
            {
                buffer.clear(); processor.processBlock(buffer, midi); midi.clear();
                for (int i = 0; i < kBlockSize; ++i) { after.push_back(buffer.getSample(0, i)); }
            }

            auto worstStep = 0.0f;
            auto worstAt = 0;
            for (std::size_t i = 1; i < after.size(); ++i)
            {
                const auto step = std::abs(after[i] - after[i - 1]);
                if (step > worstStep) { worstStep = step; worstAt = static_cast<int>(i); }
            }
            auto peakAfter = 0.0f;
            for (const auto v : after) { peakAfter = juce::jmax(peakAfter, std::abs(v)); }

            std::printf("    gap %4.0f ms: level before %.5f, peak in the first 50 ms %.5f, "
                        "largest step %.5f at %.2f ms\n",
                        gapSeconds * 1000.0, levelBefore, peakAfter, worstStep,
                        1000.0 * worstAt / kSampleRate);
        }
        std::printf("\n");

        // The envelope alone, retriggered during its release, with a long
        // attack. This is where the shape of the artifact shows.
        {
            AmpEnvelope envelope;
            envelope.prepare(kSampleRate);
            EnvelopeSettings settings;
            settings.attackSeconds = 1.000f;
            settings.decaySeconds = 0.100f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = 0.500f;
            envelope.setSettings(settings);

            envelope.noteOn();
            for (int i = 0; i < static_cast<int>(kSampleRate * 1.2); ++i)
            {
                envelope.getNextSample();
            }
            envelope.noteOff();

            auto atNoteOff = 0.0f;
            for (int i = 0; i < static_cast<int>(kSampleRate * 0.05); ++i)
            {
                atNoteOff = envelope.getNextSample();
            }

            std::printf("  the ENVELOPE retriggered during its release "
                        "(1 s attack, level %.4f):\n", atNoteOff);
            envelope.noteOn();
            for (const auto ms : { 0.0, 0.5, 1.0, 2.0, 5.0, 10.0, 25.0, 50.0, 100.0 })
            {
                static double lastMs = -1.0;
                const auto target = static_cast<int>(kSampleRate * ms / 1000.0);
                const auto from = static_cast<int>(kSampleRate * juce::jmax(0.0, lastMs) / 1000.0);
                auto value = 0.0f;
                for (int i = from; i <= target; ++i) { value = envelope.getNextSample(); }
                lastMs = ms;
                std::printf("    %6.1f ms after the retrigger: %.5f\n", ms, value);
            }
            std::printf("\n");
        }

        // Is anything OTHER than the envelope changing the gain?
        //
        // Render a note, and divide the signal's own envelope by the AMP ENV
        // value at the same instant. If the envelope is the only gain in the
        // path that ratio is flat; a duck or a limiter shows as it dipping and
        // recovering. Run at two velocities because the report is that playing
        // harder makes it worse.
        for (const auto velocity : { 1.0f, 0.35f })
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "ampAttack", 1.000f);
            setParam(processor, "ampDecay", 0.100f);
            setParam(processor, "ampSustain", 1.00f);
            setParam(processor, "ampRelease", 0.100f);
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            AmpEnvelope reference;
            reference.prepare(kSampleRate);
            EnvelopeSettings settings;
            settings.attackSeconds = 1.000f;
            settings.decaySeconds = 0.100f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = 0.100f;
            reference.setSettings(settings);
            reference.noteOn();

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, velocity), 0);

            std::printf("  velocity %.2f, signal divided by the envelope "
                        "(flat means the envelope is the only gain):\n", velocity);

            auto worstDip = 1.0e9;
            auto worstAtMs = 0.0;
            auto settled = 0.0;
            for (int block = 0; block < static_cast<int>(0.30 * kSampleRate / kBlockSize); ++block)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();

                auto env = 0.0f;
                for (int i = 0; i < kBlockSize; ++i) { env = reference.getNextSample(); }

                const auto level = buffer.getMagnitude(0, kBlockSize);
                const auto ratio = env > 1.0e-5f ? level / env : 0.0;
                const auto ms = 1000.0 * (block + 1) * kBlockSize / kSampleRate;

                if (block >= 12) { settled = ratio; }
                if (block >= 2 && ratio < worstDip) { worstDip = ratio; worstAtMs = ms; }

                if (block < 14 || block % 6 == 0)
                {
                    std::printf("    %6.1f ms  env %.5f  level %.5f  ratio %.4f\n",
                                ms, env, level, ratio);
                }
            }
            std::printf("    -> lowest ratio %.4f at %.1f ms, settled at %.4f (%.1f%% of "
                        "settled)\n\n",
                        worstDip, worstAtMs, settled,
                        settled > 0.0 ? 100.0 * worstDip / settled : 0.0);
        }

        // Every oscillator mode, with a one second attack. The envelope is
        // under 0.005 for the first 5 ms, so nothing should be audible there
        // in ANY mode - a mode that excites something at note-on regardless of
        // the envelope shows up as a level the envelope does not permit.
        std::printf("  every mode, 1 s attack, the first 5 ms:\n");
        std::printf("    %-14s %10s %10s %10s\n", "mode", "peak 5ms", "env there", "ratio");
        for (int mode = 0; mode < px3::oscillatorModeChoices().size(); ++mode)
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "ampAttack", 1.000f);
            setParam(processor, "ampDecay", 0.100f);
            setParam(processor, "ampSustain", 1.00f);
            setParam(processor, "ampRelease", 0.100f);
            setChoice(processor, "osc1Mode", mode);
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

            auto peak = 0.0f;
            const auto blocks = juce::jmax(1, static_cast<int>(0.005 * kSampleRate / kBlockSize));
            for (int b = 0; b < blocks; ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();
                peak = juce::jmax(peak, buffer.getMagnitude(0, kBlockSize));
            }

            AmpEnvelope reference;
            reference.prepare(kSampleRate);
            EnvelopeSettings settings;
            settings.attackSeconds = 1.000f;
            settings.decaySeconds = 0.100f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = 0.100f;
            reference.setSettings(settings);
            reference.noteOn();
            auto env = 0.0f;
            for (int i = 0; i < blocks * kBlockSize; ++i) { env = reference.getNextSample(); }

            std::printf("    %-14s %10.6f %10.6f %10.2f%s\n",
                        px3::oscillatorModeChoices()[mode].toRawUTF8(), peak, env,
                        env > 1.0e-6f ? peak / env : 0.0f,
                        (env > 1.0e-6f && peak / env > 1.0f) ? "   <-- louder than the envelope allows"
                                                             : "");
        }
        std::printf("\n");

        // The reported repro, exactly: one sine oscillator, no sub, no
        // modulation, FX bypassed, a 4 second attack, and a THREE NOTE CHORD
        // at full velocity. Every measurement above played a single note.
        {
            PX3SynthAudioProcessor processor;

            // NOT makePlainPatch. That helper also drops the master gain to
            // 0.6, silences the filters and zeroes every send - so it turns off
            // things the report did not say to turn off. This switches off only
            // what was described and leaves everything else where the plugin
            // loads it, including the analog engine.
            setParam(processor, "osc1Enabled", 1.0f);
            setParam(processor, "osc2Enabled", 0.0f);
            setParam(processor, "osc3Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "delayEnabled", 0.0f);
            setParam(processor, "reverbEnabled", 0.0f);
            setParam(processor, "moodEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 0.0f);
            for (int i = 0; i < 3; ++i)
            {
                const auto slot = juce::String(i + 1);
                setParam(processor, "env" + slot + "Enabled", 0.0f);
                const auto lfoPrefix = i == 0 ? juce::String("lfo") : "lfo" + slot;
                setParam(processor, i == 0 ? juce::String("lfoEnabled") : lfoPrefix + "Enabled",
                         0.0f);
            }

            setChoice(processor, "osc1Mode", 0);           // SINE
            setParam(processor, "ampAttack", 4.000f);
            setParam(processor, "ampDecay", 0.100f);
            setParam(processor, "ampSustain", 1.00f);
            setParam(processor, "ampRelease", 0.500f);

            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

            std::vector<float> trace;
            const auto seconds = 0.5;
            for (int b = 0; b < static_cast<int>(seconds * kSampleRate / kBlockSize); ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();
                for (int i = 0; i < kBlockSize; ++i) { trace.push_back(buffer.getSample(0, i)); }
            }

            AmpEnvelope reference;
            reference.prepare(kSampleRate);
            EnvelopeSettings settings;
            settings.attackSeconds = 4.000f;
            settings.decaySeconds = 0.100f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = 0.500f;
            reference.setSettings(settings);
            reference.noteOn();

            std::printf("  THE REPRO: sine, no sub, no modulation, FX off, 4 s attack,\n"
                        "  a three note chord at full velocity.\n\n");
            std::printf("    %8s %12s %12s %10s\n", "ms", "signal", "envelope x3", "ratio");

            auto worstStep = 0.0f;
            auto worstAtMs = 0.0;
            auto previous = 0.0f;
            auto peakEarly = 0.0f;
            auto envAtPeak = 0.0f;

            for (std::size_t i = 0; i < trace.size(); ++i)
            {
                const auto env = reference.getNextSample();
                const auto step = std::abs(trace[i] - previous);
                if (step > worstStep)
                {
                    worstStep = step;
                    worstAtMs = 1000.0 * static_cast<double>(i) / kSampleRate;
                }
                previous = trace[i];

                if (i < static_cast<std::size_t>(kSampleRate * 0.02)
                    && std::abs(trace[i]) > peakEarly)
                {
                    peakEarly = std::abs(trace[i]);
                    envAtPeak = env;
                }

                const auto ms = 1000.0 * static_cast<double>(i) / kSampleRate;
                if (i % static_cast<std::size_t>(kSampleRate * 0.02) == 0)
                {
                    std::printf("    %8.1f %12.6f %12.6f %10.3f\n", ms, trace[i], env * 3.0f,
                                env > 1.0e-7f ? std::abs(trace[i]) / env : 0.0f);
                }
            }

            // The first 48 samples, and the steps AT block boundaries.
            //
            // setSettings runs once per block, so a 4 second attack rebuilds
            // the envelope 93 times a second. If a rebuild loses the envelope's
            // position, the artifact lands on block boundaries and nowhere else.
            std::printf("\n    the first 32 samples:\n     ");
            for (std::size_t i = 0; i < 32 && i < trace.size(); ++i)
            {
                std::printf("%s%+.6f", (i % 4 == 0 ? "\n      " : "  "), trace[i]);
            }
            std::printf("\n\n");

            auto worstBoundary = 0.0f;
            auto worstBoundaryAt = 0;
            auto worstInterior = 0.0f;
            for (std::size_t i = 1; i < trace.size(); ++i)
            {
                const auto step = std::abs(trace[i] - trace[i - 1]);
                if (static_cast<int>(i) % kBlockSize == 0)
                {
                    if (step > worstBoundary)
                    {
                        worstBoundary = step;
                        worstBoundaryAt = static_cast<int>(i);
                    }
                }
                else if (step > worstInterior)
                {
                    worstInterior = step;
                }
            }
            std::printf("    largest step AT a block boundary: %.6f (sample %d, %.1f ms)\n",
                        worstBoundary, worstBoundaryAt,
                        1000.0 * worstBoundaryAt / kSampleRate);
            std::printf("    largest step anywhere else:       %.6f\n", worstInterior);
            std::printf("    %s\n",
                        worstBoundary > worstInterior * 2.0f
                            ? "*** the artifact is on block boundaries - a per-block rebuild ***"
                            : "block boundaries are no worse than anywhere else");

            // Other buffer sizes. The harness runs 512; a standalone commonly
            // runs 128 or 256, and the envelope is re-sent once per block, so
            // the rate of that changes with the buffer.
            std::printf("\n    the same chord at other buffer sizes:\n");
            std::printf("      %8s %14s %14s\n", "block", "worst step", "peak 20 ms");
            for (const auto block : { 32, 64, 128, 256, 512 })
            {
                PX3SynthAudioProcessor other;
                setParam(other, "osc1Enabled", 1.0f);
                setParam(other, "osc2Enabled", 0.0f);
                setParam(other, "osc3Enabled", 0.0f);
                setParam(other, "subOscEnabled", 0.0f);
                setParam(other, "delayEnabled", 0.0f);
                setParam(other, "reverbEnabled", 0.0f);
                setParam(other, "moodEnabled", 0.0f);
                setParam(other, "vibeEnabled", 0.0f);
                setChoice(other, "osc1Mode", 0);
                setParam(other, "ampAttack", 4.000f);
                setParam(other, "ampDecay", 0.100f);
                setParam(other, "ampSustain", 1.00f);
                setParam(other, "ampRelease", 0.500f);

                other.setPlayConfigDetails(0, 2, kSampleRate, block);
                other.prepareToPlay(kSampleRate, block);

                juce::AudioBuffer<float> b(2, block);
                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
                m.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
                m.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

                auto step = 0.0f;
                auto early = 0.0f;
                auto prev = 0.0f;
                auto index = 0;
                for (int n = 0; n < static_cast<int>(0.3 * kSampleRate) / block; ++n)
                {
                    b.clear();
                    other.processBlock(b, m);
                    m.clear();
                    for (int i = 0; i < block; ++i)
                    {
                        const auto v = b.getSample(0, i);
                        step = juce::jmax(step, std::abs(v - prev));
                        prev = v;
                        if (index < static_cast<int>(kSampleRate * 0.02))
                        {
                            early = juce::jmax(early, std::abs(v));
                        }
                        ++index;
                    }
                }
                std::printf("      %8d %14.6f %14.6f\n", block, step, early);
            }

            std::printf("\n    peak in the first 20 ms: %.6f, envelope there %.6f",
                        peakEarly, envAtPeak);
            if (envAtPeak > 1.0e-7f)
            {
                std::printf("  (%.1fx what one voice at that envelope would give)",
                            peakEarly / envAtPeak);
            }
            std::printf("\n    largest sample-to-sample step: %.6f at %.2f ms\n\n",
                        worstStep, worstAtMs);

            // Written out so it can be listened to.
            const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("px3-chord-attack.wav");
            juce::AudioBuffer<float> whole(1, static_cast<int>(trace.size()));
            for (std::size_t i = 0; i < trace.size(); ++i)
            {
                whole.setSample(0, static_cast<int>(i), trace[i]);
            }
            juce::WavAudioFormat wav;
            file.deleteFile();
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()))
            {
                if (auto* writer = wav.createWriterFor(stream.get(), kSampleRate, 1, 24, {}, 0))
                {
                    stream.release();
                    writer->writeFromAudioSampleBuffer(whole, 0, whole.getNumSamples());
                    delete writer;
                    std::printf("    wrote %s\n\n", file.getFullPathName().toRawUTF8());
                }
            }
        }

        // The realistic case: the synth has been RUNNING and idle for a while,
        // then a note arrives. Every earlier render here fired the note in the
        // first block or two after prepareToPlay.
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "osc1Enabled", 1.0f);
            setParam(processor, "osc2Enabled", 0.0f);
            setParam(processor, "osc3Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "delayEnabled", 0.0f);
            setParam(processor, "reverbEnabled", 0.0f);
            setParam(processor, "moodEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 0.0f);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "ampAttack", 4.000f);
            setParam(processor, "ampDecay", 0.100f);
            setParam(processor, "ampSustain", 1.00f);
            setParam(processor, "ampRelease", 0.500f);
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer empty;

            // Two seconds of idle, the way a running plugin sits before a key
            // is pressed.
            for (int b = 0; b < static_cast<int>(2.0 * kSampleRate / kBlockSize); ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, empty);
            }

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

            std::printf("  after two seconds of idle, a 3 note chord, 4 s attack:\n");
            std::printf("    %8s %14s\n", "block", "peak");
            for (int b = 0; b < 12; ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();
                std::printf("    %8d %14.6f%s\n", b, buffer.getMagnitude(0, kBlockSize),
                            b == 0 ? "   <-- the block containing the note-on" : "");
            }
            std::printf("\n");
        }

        // With the EDITOR OPEN and its timer running - which the standalone
        // always has and every render here so far has not.
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "osc1Enabled", 1.0f);
            setParam(processor, "osc2Enabled", 0.0f);
            setParam(processor, "osc3Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "delayEnabled", 0.0f);
            setParam(processor, "reverbEnabled", 0.0f);
            setParam(processor, "moodEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 0.0f);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "ampAttack", 4.000f);
            setParam(processor, "ampDecay", 0.100f);
            setParam(processor, "ampSustain", 1.00f);
            setParam(processor, "ampRelease", 0.500f);
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            if (editor != nullptr)
            {
                editor->setSize(1320, 798);
            }

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer empty;

            // Idle, letting the editor's timers run between blocks.
            for (int b = 0; b < 60; ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, empty);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(2);
            }

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

            std::printf("  with the editor open, a 3 note chord, 4 s attack:\n");
            std::printf("    %8s %14s\n", "block", "peak");
            auto worst = 0.0f;
            for (int b = 0; b < 12; ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();
                const auto pk = buffer.getMagnitude(0, kBlockSize);
                worst = juce::jmax(worst, pk);
                std::printf("    %8d %14.6f%s\n", b, pk,
                            b == 0 ? "   <-- the block containing the note-on" : "");
                juce::MessageManager::getInstance()->runDispatchLoopUntil(2);
            }
            std::printf("    peak over the first 12 blocks: %.6f%s\n\n", worst,
                        worst > 0.1f ? "   *** a burst the envelope does not allow ***" : "");
            editor.reset();
        }

        // The capture from a real host showed 2 voices already sounding at
        // envelope 0.545 when the chord arrived, and gone one block later. So:
        // press a chord, let it climb, then press the SAME chord again while
        // it is still ringing.
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "osc1Enabled", 1.0f);
            setParam(processor, "osc2Enabled", 0.0f);
            setParam(processor, "osc3Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "delayEnabled", 0.0f);
            setParam(processor, "reverbEnabled", 0.0f);
            setParam(processor, "moodEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 0.0f);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "ampAttack", 4.000f);
            setParam(processor, "ampDecay", 0.100f);
            setParam(processor, "ampSustain", 1.00f);
            setParam(processor, "ampRelease", 0.500f);
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer midi;
            const int notes[] = { 72, 76, 79 };
            for (const auto note : notes)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
            }

            // Hold until the envelope is where the capture found it: 0.545 of
            // a four second attack is about 2.2 seconds in.
            for (int b = 0; b < static_cast<int>(2.2 * kSampleRate / kBlockSize); ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();
            }

            buffer.clear();
            processor.processBlock(buffer, midi);
            const auto ringing = buffer.getMagnitude(0, kBlockSize);

            // The same chord again, without releasing the first.
            for (const auto note : notes)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
            }

            std::printf("  the SAME chord pressed again while still ringing at %.4f:\n",
                        ringing);
            std::printf("    %8s %14s\n", "block", "peak");
            // Seeded from the LAST sample before the re-press, not from zero.
            // Seeding with zero counts the first sample of an already-ringing
            // chord as a step of its own amplitude - which is how this reported
            // a 0.20 discontinuity that was never there.
            auto worstStep = 0.0f;
            auto previous = buffer.getSample(0, kBlockSize - 1);
            for (int b = 0; b < 10; ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();
                for (int i = 0; i < kBlockSize; ++i)
                {
                    worstStep = juce::jmax(worstStep,
                                           std::abs(buffer.getSample(0, i) - previous));
                    previous = buffer.getSample(0, i);
                }
                std::printf("    %8d %14.6f%s\n", b, buffer.getMagnitude(0, kBlockSize),
                            b == 0 ? "   <-- the re-press" : "");
            }
            std::printf("    largest sample-to-sample step through it: %.6f\n\n", worstStep);
        }

        // Same notes versus DIFFERENT notes, which discriminates same-note
        // voice reuse from ordinary polyphony.
        {
            const auto repress = [](const int* second)
            {
                PX3SynthAudioProcessor processor;
                setParam(processor, "osc1Enabled", 1.0f);
                setParam(processor, "osc2Enabled", 0.0f);
                setParam(processor, "osc3Enabled", 0.0f);
                setParam(processor, "subOscEnabled", 0.0f);
                setParam(processor, "delayEnabled", 0.0f);
                setParam(processor, "reverbEnabled", 0.0f);
                setParam(processor, "moodEnabled", 0.0f);
                setParam(processor, "vibeEnabled", 0.0f);
                setChoice(processor, "osc1Mode", 0);
                setParam(processor, "ampAttack", 4.000f);
                setParam(processor, "ampDecay", 0.100f);
                setParam(processor, "ampSustain", 1.00f);
                setParam(processor, "ampRelease", 0.500f);
                processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
                processor.prepareToPlay(kSampleRate, kBlockSize);

                juce::AudioBuffer<float> buffer(2, kBlockSize);
                juce::MidiBuffer midi;
                const int first[] = { 72, 76, 79 };
                for (const auto note : first)
                {
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
                }
                for (int b = 0; b < static_cast<int>(2.2 * kSampleRate / kBlockSize); ++b)
                {
                    buffer.clear(); processor.processBlock(buffer, midi); midi.clear();
                }

                for (int i = 0; i < 3; ++i)
                {
                    midi.addEvent(juce::MidiMessage::noteOn(1, second[i], 1.0f), 0);
                }

                auto worst = 0.0f;
                auto prev = buffer.getSample(0, kBlockSize - 1);   // not zero
                for (int b = 0; b < 6; ++b)
                {
                    buffer.clear(); processor.processBlock(buffer, midi); midi.clear();
                    for (int i = 0; i < kBlockSize; ++i)
                    {
                        worst = juce::jmax(worst, std::abs(buffer.getSample(0, i) - prev));
                        prev = buffer.getSample(0, i);
                    }
                }
                return worst;
            };

            const int same[] = { 72, 76, 79 };
            const int other[] = { 74, 77, 81 };
            // The samples either side of the worst step.
            {
                PX3SynthAudioProcessor processor;
                setParam(processor, "osc1Enabled", 1.0f);
                setParam(processor, "osc2Enabled", 0.0f);
                setParam(processor, "osc3Enabled", 0.0f);
                setParam(processor, "subOscEnabled", 0.0f);
                setParam(processor, "delayEnabled", 0.0f);
                setParam(processor, "reverbEnabled", 0.0f);
                setParam(processor, "moodEnabled", 0.0f);
                setParam(processor, "vibeEnabled", 0.0f);
                setChoice(processor, "osc1Mode", 0);
                setParam(processor, "ampAttack", 4.000f);
                setParam(processor, "ampSustain", 1.00f);
                setParam(processor, "ampRelease", 0.500f);
                processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
                processor.prepareToPlay(kSampleRate, kBlockSize);

                juce::AudioBuffer<float> buffer(2, kBlockSize);
                juce::MidiBuffer midi;
                const int chord[] = { 72, 76, 79 };
                for (const auto note : chord)
                { midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0); }
                for (int b = 0; b < static_cast<int>(2.2 * kSampleRate / kBlockSize); ++b)
                { buffer.clear(); processor.processBlock(buffer, midi); midi.clear(); }

                for (const auto note : chord)
                { midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0); }

                std::vector<float> trace;
                for (int b = 0; b < 40; ++b)
                {
                    buffer.clear(); processor.processBlock(buffer, midi); midi.clear();
                    for (int i = 0; i < kBlockSize; ++i)
                    { trace.push_back(buffer.getSample(0, i)); }
                }

                auto at = 1; auto worst = 0.0f;
                for (std::size_t i = 1; i < trace.size(); ++i)
                {
                    const auto d = std::abs(trace[i] - trace[i-1]);
                    if (d > worst) { worst = d; at = static_cast<int>(i); }
                }
                std::printf("  worst step %.6f at sample %d of the re-press "
                            "(block %d, offset %d):\n", worst, at, at / kBlockSize,
                            at % kBlockSize);
                for (int i = juce::jmax(0, at - 5); i <= at + 5
                     && i < static_cast<int>(trace.size()); ++i)
                {
                    std::printf("    %5d  %+.6f%s\n", i, trace[static_cast<std::size_t>(i)],
                                i == at ? "   <-- here" : "");
                }
                std::printf("\n");
            }

            std::printf("  largest step when the second chord is:\n");
            std::printf("    the SAME notes:      %.6f\n", repress(same));
            std::printf("    DIFFERENT notes:     %.6f\n\n", repress(other));
        }

        std::printf("  Nothing above shows a discontinuity or a duck. What this mode has\n"
                    "  ruled out, with numbers: the envelope does reach the audio, no stage\n"
                    "  after it changes gain with level, and no oscillator mode leaks a\n"
                    "  transient past it. The one fault it did find - a retriggered attack\n"
                    "  diving to silence first - is fixed and pinned by AmpEnv_Retrigger*.\n\n");
        return 0;
    }

    if (filter == "sharpcheck")
    {
        // Waveform line sharpness, diagnosed rather than adjusted.
        //
        // The measurement isolates ONE curve by subtracting two renders of the
        // same scene - one with a frame selected, one without. Whatever differs
        // is that curve and nothing else: not the floor, not the environment,
        // not the other frames. Profiling a column of the difference then says
        // how many physical pixels the curve's edge takes, which is the whole
        // distinction between antialiasing and blur.
        std::printf("\nWAVEFORM SHARPNESS\n\n");

        struct Host final : public juce::DocumentWindow
        {
            Host() : juce::DocumentWindow("px3 sharp", juce::Colours::black, 0)
            {
                renderer.setSize(290, 149);
                setContentNonOwned(&renderer, true);
                setOpaque(true);
                setVisible(true);
                setTopLeftPosition(-4000, -4000);
            }
            void closeButtonPressed() override {}
            Wavetable3DRenderer renderer;
        };

        auto host = std::make_unique<Host>();

        // Two FLAT frames, so each curve is a straight horizontal line and a
        // vertical profile crosses it square on. A real table was tried first
        // and cannot isolate anything: moving the scan changes the whole
        // neighbourhood of frames, so the difference spanned 119 px of picture
        // rather than one curve.
        px3::WavetableDisplay flat;
        flat.name = "flat";
        flat.frames.push_back(std::vector<float>(128, -0.45f));
        flat.frames.push_back(std::vector<float>(128, 0.45f));

        host->renderer.setPixelAudit(true);
        host->renderer.setDisplay(flat);
        host->renderer.setPosition(0.5f);

        const auto deadline = juce::Time::getMillisecondCounter() + 4000;
        while (juce::Time::getMillisecondCounter() < deadline
               && ! host->renderer.isRendering()
               && host->renderer.getShaderError().isEmpty())
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        }

        if (! host->renderer.isRendering())
        {
            std::printf("  no GL context came up\n\n");
            host.reset();
            return 1;
        }

        const auto settle = []
        {
            for (int i = 0; i < 10; ++i)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(25);
            }
        };
        settle();

        const auto surface = host->renderer.getSurface();
        const auto W = surface.framebufferWidth;
        const auto H = surface.framebufferHeight;

        std::printf("  component      %d x %d points\n",
                    surface.componentWidth, surface.componentHeight);
        std::printf("  framebuffer    %d x %d px\n", W, H);
        std::printf("  device scale   %.2f\n", surface.renderingScale);
        const auto expectedW = juce::roundToInt(surface.componentWidth * surface.renderingScale);
        const auto expectedH = juce::roundToInt(surface.componentHeight * surface.renderingScale);
        std::printf("  viewport is %s the physical size (%d x %d expected)\n\n",
                    (W == expectedW && H == expectedH) ? "EXACTLY" : "NOT",
                    expectedW, expectedH);

        std::printf("  eye depth at model z=+1.8: %.3f\n",
                    host->renderer.eyeDepthForModelZ(1.8f));
        std::printf("  eye depth at model z=-1.8: %.3f\n",
                    host->renderer.eyeDepthForModelZ(-1.8f));
        std::printf("  the frame at z=+1.8 (vFrame 1) is NEAREST the camera\n\n");

        const auto frameAt = [&host, &settle](float position)
        {
            host->renderer.setPosition(position);
            settle();
            return host->renderer.getFrameLuminance();
        };

        // Two renders that differ only in which of the two frames is
        // highlighted. The floor and the environment are identical in both and
        // subtract away exactly.
        const auto withA = frameAt(0.0f);
        const auto withB = frameAt(1.0f);

        if (withA.size() != static_cast<std::size_t>(W) * static_cast<std::size_t>(H)
            || withB.size() != withA.size())
        {
            std::printf("  frame capture failed (%d vs %d px)\n\n",
                        static_cast<int>(withA.size()), W * H);
            host.reset();
            return 1;
        }

        // The difference isolates the highlighted curve.
        std::vector<float> difference(withA.size(), 0.0f);
        auto peak = 0.0f;
        auto peakIndex = std::size_t { 0 };
        for (std::size_t i = 0; i < difference.size(); ++i)
        {
            difference[i] = std::abs(withA[i] - withB[i]);
            if (difference[i] > peak) { peak = difference[i]; peakIndex = i; }
        }

        const auto peakColumn = static_cast<int>(peakIndex % static_cast<std::size_t>(W));
        const auto peakRow = static_cast<int>(peakIndex / static_cast<std::size_t>(W));
        std::printf("  the highlighted curve is strongest at column %d, row %d (%.4f)\n\n",
                    peakColumn, peakRow, peak);

        // A vertical profile of the isolated curve, through that column.
        std::printf("  edge profile of the highlighted curve, physical pixel rows:\n");
        auto first = -1, last = -1;
        for (int dy = -12; dy <= 12; ++dy)
        {
            const auto row = peakRow + dy;
            if (row < 0 || row >= H) { continue; }
            const auto value = difference[static_cast<std::size_t>(row)
                                          * static_cast<std::size_t>(W)
                                          + static_cast<std::size_t>(peakColumn)];
            const auto level = value / peak;
            const auto bars = juce::jlimit(0, 52, juce::roundToInt(level * 52.0f));
            std::printf("    %+3d  %.4f  %s\n", dy, value,
                        juce::String::repeatedString("#", bars).toRawUTF8());
            if (level >= 0.10f) { if (first < 0) { first = dy; } last = dy; }
        }

        // Core and edge, in physical pixels.
        auto core = 0, total = 0;
        for (int row = 0; row < H; ++row)
        {
            const auto value = difference[static_cast<std::size_t>(row)
                                          * static_cast<std::size_t>(W)
                                          + static_cast<std::size_t>(peakColumn)];
            const auto level = value / peak;
            if (level >= 0.90f) { ++core; }
            if (level >= 0.10f) { ++total; }
        }
        std::printf("\n  core (>=90%% of peak): %d px    full extent (>=10%%): %d px\n",
                    core, total);
        std::printf("  so the edge takes %.2f px per side\n", (total - core) * 0.5);
        const auto edgePerSide = (total - core) * 0.5;
        const auto isAntialiased = edgePerSide <= 1.5;
        std::printf("  %s\n\n",
                    isAntialiased
                        ? "that is antialiasing"
                        : "*** that is blur: the transition is wider than an antialiased edge ***");

        // And the real 48-frame stack, which is where stacking order matters.
        // A crisp picture separates its frames; a muddy one averages them.
        {
            PX3SynthAudioProcessor processor;
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);
            processor.loadFactoryWavetable(0, 0);

            host->renderer.setDisplay(processor.getWavetableDisplay(0, 48, 128));
            host->renderer.setPosition(0.45f);
            settle();

            const auto real = host->renderer.getLuminanceProbe();
            std::printf("  the real 48-frame stack:\n");
            std::printf("    steepest edges   %.4f  (higher is crisper)\n", real.steepestEdges);
            std::printf("    brightest pixel  %.4f\n", real.brightest);

            // How well the highlighted frame stands out from the frames around
            // it. Averaged-together frames have nothing standing above them.
            const auto frame = host->renderer.getFrameLuminance();
            if (frame.size() == static_cast<std::size_t>(W) * static_cast<std::size_t>(H))
            {
                std::vector<float> sorted(frame);
                std::sort(sorted.begin(), sorted.end());
                const auto median = sorted[sorted.size() / 2];
                const auto ninetyNinth = sorted[static_cast<std::size_t>(
                    static_cast<double>(sorted.size()) * 0.99)];
                std::printf("    median %.4f, 99th percentile %.4f, ratio %.2f\n",
                            median, ninetyNinth,
                            median > 1.0e-4f ? ninetyNinth / median : 0.0);
            }
            std::printf("\n");
        }

        host.reset();
        return isAntialiased ? 0 : 1;
    }

    if (filter == "envcheck")
    {
        // The environment, measured with it off and with it on.
        //
        // This is the brief's own acceptance test, and it needs a real context:
        // the whole effect is a fragment shader, so there is nothing to
        // evaluate without a driver. It prints the numbers rather than a
        // verdict, because "too strong" and "too weak" are judgements about a
        // picture - but the numbers are what say whether the picture changed at
        // all, and by how much.
        std::printf("\nENVIRONMENT A/B\n\n");

        struct Host final : public juce::DocumentWindow
        {
            Host() : juce::DocumentWindow("px3 env", juce::Colours::black, 0)
            {
                // The real panel's size, not a round number. The framing is a
                // function of aspect, so a harness at 1.6 says nothing about a
                // panel at 1.95.
                renderer.setSize(290, 149);
                setContentNonOwned(&renderer, true);
                setOpaque(true);
                setVisible(true);
                setTopLeftPosition(-4000, -4000);
            }
            void closeButtonPressed() override {}
            Wavetable3DRenderer renderer;
        };

        auto host = std::make_unique<Host>();

        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);
        processor.loadFactoryWavetable(0, 0);

        host->renderer.setPixelAudit(true);
        host->renderer.setDisplay(processor.getWavetableDisplay(0, 48, 128));
        host->renderer.setPosition(0.4f);

        const auto deadline = juce::Time::getMillisecondCounter() + 4000;
        while (juce::Time::getMillisecondCounter() < deadline
               && ! host->renderer.isRendering()
               && host->renderer.getShaderError().isEmpty())
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        }

        if (! host->renderer.isRendering())
        {
            std::printf("  no GL context came up - %s\n\n",
                        host->renderer.getShaderError().isEmpty()
                            ? "no shader error either"
                            : host->renderer.getShaderError().toRawUTF8());
            host.reset();
            return 1;
        }

        const auto settle = []
        {
            for (int i = 0; i < 12; ++i)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(25);
            }
        };

        host->renderer.setEnvironmentEnabled(false);
        settle();
        const auto off = host->renderer.getLuminanceProbe();
        const auto litOff = host->renderer.getLitPixelCount();

        host->renderer.setEnvironmentEnabled(true);
        settle();
        const auto on = host->renderer.getLuminanceProbe();
        const auto litOn = host->renderer.getLitPixelCount();

        const auto row = [](const char* label, float a, float b)
        {
            std::printf("  %-22s %7.4f   %7.4f   %+7.4f\n", label, a, b, b - a);
        };

        std::printf("  %-22s %7s   %7s   %7s\n", "", "OFF", "ON", "delta");
        row("centre luminance", off.centre, on.centre);
        row("corner luminance", off.corners, on.corners);
        row("upper luminance", off.upper, on.upper);
        row("lower luminance", off.lower, on.lower);
        row("darkest pixel", off.darkest, on.darkest);
        row("brightest pixel", off.brightest, on.brightest);
        row("soft-edge fraction", off.softFraction, on.softFraction);
        row("steepest edges", off.steepestEdges, on.steepestEdges);
        std::printf("\n");

        // What the framebuffer is actually being rendered at. A picture drawn
        // at one resolution and shown at another is blurred by the resample,
        // whatever the shader does - so this has to be ruled in or out before
        // any amount of shader tuning means anything.
        std::printf("  component %d x %d, framebuffer %d px",
                    host->renderer.getWidth(), host->renderer.getHeight(),
                    host->renderer.getAuditedPixelCount());
        const auto expected = host->renderer.getWidth() * host->renderer.getHeight();
        if (expected > 0)
        {
            const auto ratio = static_cast<double>(host->renderer.getAuditedPixelCount())
                               / static_cast<double>(expected);
            std::printf(" - %.2f px per point, so scale %.2f\n\n", ratio, std::sqrt(ratio));
        }
        else
        {
            std::printf("\n\n");
        }

        const auto vignetteOff = off.centre - off.corners;
        const auto vignetteOn = on.centre - on.corners;
        std::printf("  centre over corners:   %+.4f off, %+.4f on\n",
                    vignetteOff, vignetteOn);
        std::printf("  lower over upper:      %+.4f off, %+.4f on\n",
                    off.lower - off.upper, on.lower - on.upper);
        std::printf("  lit pixels:            %d off, %d on\n\n", litOff, litOn);

        // Does the floor take light from the scan? Measured by sweeping the
        // scan and watching the frame, with the caveat that the STACK moves
        // too - so this is reported as a sweep rather than gated, and the
        // number that matters is how it compares with the light pool removed.
        std::printf("  the scan sweeping, with the environment on:\n");
        std::printf("    %-8s %8s %8s\n", "scan", "lower", "centre");
        auto lowestLower = 1.0f, highestLower = 0.0f;
        for (const auto scan : { 0.05f, 0.35f, 0.65f, 0.95f })
        {
            host->renderer.setPosition(scan);
            settle();
            const auto swept = host->renderer.getLuminanceProbe();
            std::printf("    %-8.2f %8.4f %8.4f\n", scan, swept.lower, swept.centre);
            lowestLower = juce::jmin(lowestLower, swept.lower);
            highestLower = juce::jmax(highestLower, swept.lower);
        }
        std::printf("    lower region moves by %.4f across the sweep\n\n",
                    highestLower - lowestLower);

        host->renderer.setPosition(0.4f);
        settle();

        // Changing the TABLE has to re-fit the camera. Reported by a user as
        // "it doesn't resize until I click and drag it", which is a claim about
        // the render loop and can only be checked on a real context.
        juce::StringArray refitFailures;

        // Orbited first, because that is the state the report came from - the
        // user was dragging the view. autoFrame fits the DEFAULT orientation;
        // whether it fits the one actually on screen is the question.
        {
            const juce::Point<float> from { 145.0f, 74.0f };
            const auto to = from.translated(220.0f, -60.0f);
            const auto make = [&host](juce::Point<float> p)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                                        juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                        &host->renderer, &host->renderer,
                                        juce::Time::getCurrentTime(), p,
                                        juce::Time::getCurrentTime(), 1, false);
            };
            host->renderer.mouseDown(make(from));
            host->renderer.mouseDrag(make(to));
            settle();
            std::printf("  camera orbited to azimuth %.2f, elevation %.2f\n",
                        host->renderer.getCamera().azimuth,
                        host->renderer.getCamera().elevation);
        }

        std::printf("  changing table:\n");
        std::printf("    %-16s %9s %9s %9s\n", "table", "framed", "extent", "fits");
        for (int table = 0; table < static_cast<int>(px3::factoryWavetables().size()); ++table)
        {
            processor.loadFactoryWavetable(0, table);
            host->renderer.setDisplay(processor.getWavetableDisplay(0, 48, 128));
            settle();

            const auto framed = host->renderer.getCamera().distance;
            const auto needed = host->renderer.distanceThatFits(
                static_cast<float>(host->renderer.getWidth())
                    / static_cast<float>(host->renderer.getHeight()),
                0.06f);
            const auto name = juce::String(
                px3::factoryWavetables()[static_cast<std::size_t>(table)].name);

            // Where the geometry actually lands at the camera the renderer is
            // using right now. Anything past 1.0 on either axis is off screen -
            // which is the whole of the report, in one number.
            const auto aspect = static_cast<float>(host->renderer.getWidth())
                                / static_cast<float>(host->renderer.getHeight());
            const auto bounds = host->renderer.projectedBounds(
                host->renderer.getCamera(), aspect);
            const auto extent = juce::jmax(
                juce::jmax(std::abs(bounds.getX()), std::abs(bounds.getRight())),
                juce::jmax(std::abs(bounds.getY()), std::abs(bounds.getBottom())));

            std::printf("    %-16s %9.3f %9.3f %9s\n", name.toRawUTF8(), framed, extent,
                        extent <= 1.0f ? "yes" : "NO");
            juce::ignoreUnused(needed);
            if (extent > 1.0f) { refitFailures.add(name); }
        }
        std::printf("\n");

        // The three properties the brief actually specifies, as pass/fail. The
        // rest of the numbers are for judging the picture; these are the ones
        // that can be wrong.
        struct Criterion { const char* what; bool ok; };
        const Criterion criteria[] = {
            // Section 5: ambient illumination, so nothing is a pure black void.
            // The trap this catches is a vignette applied to the whole scene
            // rather than to the light it adds, which darkens the corners BELOW
            // where they started - measured at 0.0401 against 0.0480 before an
            // ambient term was added.
            { "nothing is darker than it was without the environment",
              on.darkest >= off.darkest },
            // Section 4: the vignette has to actually focus the middle.
            { "the centre stands further above the corners than it did",
              (on.centre - on.corners) > (off.centre - off.corners) },
            // Section 3: an environment, not a flat rectangle.
            { "the corners are lifted off the flat background",
              on.corners > off.corners + 0.005f },
            // The floor takes light from the selected waveform. The lower
            // region is where the floor lives and where the stack barely
            // reaches, which is what makes this a measurement OF the floor:
            // measured, it moves 0.0001 across the sweep with the light pool
            // removed and 0.0097 with it.
            { "the floor is lit by the scan moving over it",
              (highestLower - lowestLower) > 0.003f },
            // Loading a table re-fits the view that is ON SCREEN, not the
            // default one. Checked with the camera orbited, because with it at
            // the defaults the two are the same calculation and the bug is
            // invisible.
            { "every table fits the frame after a table change, camera orbited",
              refitFailures.isEmpty() },
        };

        auto allOk = true;
        for (const auto& criterion : criteria)
        {
            std::printf("  %-4s %s\n", criterion.ok ? "ok" : "FAIL", criterion.what);
            allOk = allOk && criterion.ok;
        }
        std::printf("\n");

        host.reset();
        return allOk ? 0 : 1;
    }

    if (filter == "installpresets")
    {
        // Runs the real factory-library install and reports what landed on
        // disk. This is the one path the automated suite cannot cover without
        // writing into the user's application-support directory as a side
        // effect, so it is an explicit developer action instead.
        PX3SynthAudioProcessor processor;
        PresetManager manager(processor);

        juce::String error;
        const auto ok = manager.initialise(error);

        std::printf("\nFACTORY PRESET INSTALL\n\n");
        if (! ok)
        {
            std::printf("  FAILED: %s\n\n", error.toRawUTF8());
            return 1;
        }

        const auto root = manager.getFactoryPresetRootDir();
        std::printf("  root: %s\n", root.getFullPathName().toRawUTF8());
        std::printf("  library version stamp: %s\n\n",
                    root.getChildFile(".factory-version").loadFileAsString().trim().toRawUTF8());

        auto files = root.findChildFiles(juce::File::findFiles, true, "*.px3preset");
        files.sort();
        for (const auto& file : files)
        {
            std::printf("    %-14s %s\n",
                        file.getParentDirectory().getFileName().toRawUTF8(),
                        file.getFileNameWithoutExtension().toRawUTF8());
        }
        std::printf("\n  %d preset files\n\n", files.size());
        return 0;
    }

    if (filter == "artifacts")
    {
        scanDoomLucyArtifacts();
        return 0;
    }

    if (filter == "params")
    {
        // Every parameter, its type, its default as the NORMALISED value a
        // preset file stores, and - for choices - the normalised value of each
        // option. Preset definitions are written in normalised units, so
        // authoring one without this is guesswork.
        PX3SynthAudioProcessor processor;

        std::printf("\nPARAMETERS  (preset files store the normalised value)\n\n");
        std::printf("  %-26s %-8s %10s  %s\n", "id", "type", "default", "range / choices");
        std::printf("  %-26s %-8s %10s  %s\n", "--------------------------", "--------",
                    "----------", "------------------------------");

        for (auto* parameter : processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            if (ranged == nullptr)
            {
                continue;
            }

            const auto id = ranged->getParameterID();
            const auto normalised = ranged->getValue();

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(ranged))
            {
                juce::String options;
                const auto count = choice->choices.size();
                for (int i = 0; i < count; ++i)
                {
                    const auto value = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
                    options << choice->choices[i] << "=" << juce::String(value, 4);
                    if (i + 1 < count)
                    {
                        options << "  ";
                    }
                }
                std::printf("  %-26s %-8s %10.4f  %s\n", id.toRawUTF8(), "choice",
                            normalised, options.toRawUTF8());
            }
            else if (dynamic_cast<juce::AudioParameterBool*>(ranged) != nullptr)
            {
                std::printf("  %-26s %-8s %10.4f  off=0  on=1\n", id.toRawUTF8(), "bool", normalised);
            }
            else
            {
                const auto& range = ranged->getNormalisableRange();
                std::printf("  %-26s %-8s %10.4f  %.3f .. %.3f%s\n", id.toRawUTF8(), "float",
                            normalised, range.start, range.end,
                            range.skew != 1.0f ? "  (skewed)" : "");
            }
        }

        std::printf("\n");
        return 0;
    }

    if (filter == "reverbmetrics")
    {
        // Baseline characterisation of every algorithm at a few settings.
        std::printf("\nREVERB QUALITY METRICS (fully wet impulse response)\n");
        std::printf("  ED = normalised echo density (1.0 = fully diffuse)\n");
        std::printf("  flat = spectral flatness of the late tail (higher = less metallic)\n");
        std::printf("  ripple = deviation from a smooth exponential decay\n");
        std::printf("  corr = inter-channel correlation (lower = wider)\n\n");
        static const char* names[] = { "0 ROOM", "1 PLATE", "2 HALL", "3 CLOUD" };
        for (int algorithm = 0; algorithm < 4; ++algorithm)
        {
            for (const auto decay : { 0.35f, 0.75f })
            {
                ReverbSettings s;
                s.algorithmIndex = algorithm;
                s.decay = decay;
                s.size = 0.6f;
                s.damping = 0.45f;
                s.preDelay = 0.0f;
                const auto m = measureReverb(s);
                reportReverbMetrics((juce::String(names[algorithm]) + " decay " + juce::String(decay, 2)).toRawUTF8(), m);
            }
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "moodartifacts")
    {
        std::printf("\nMOOD DISCONTINUITY SCAN\n");
        std::printf("  Feeds a steady tone and looks for sample-to-sample jumps that the\n");
        std::printf("  signal itself cannot explain. A read pointer that wraps without a\n");
        std::printf("  crossfade produces one of these once per loop, slice or window.\n\n");

        static const char* loopNames[] = { "ENV", "TAPE", "STRETCH" };
        static const char* wetNames[] = { "REVERB", "DELAY", "SLIP" };

        auto scan = [](int loopMode, int wetMode, float routing)
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
            s.clock = 1.0f;          // full rate: any stepping here is not the clock
            s.degrade = 0.0f;        // and not the lo-fi control either
            s.spread = 0.5f;
            s.feedback = 0.4f;
            s.loopLength = 0.35f;
            s.loopModify = 0.62f;
            s.wetTime = 0.4f;
            s.wetModify = 0.45f;
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

            // A 220 Hz sine at 0.5 moves at most 0.5*2*pi*220/48000 = 0.0144 per
            // sample. Anything far above that is a discontinuity, not programme.
            const auto from = static_cast<int>(kSampleRate * 2.0);
            double worst = 0.0;
            int jumps = 0;
            std::vector<int> positions;
            for (int i = from + 1; i < total; ++i)
            {
                const auto d = std::abs(static_cast<double>(out[static_cast<std::size_t>(i)]
                                                            - out[static_cast<std::size_t>(i - 1)]));
                worst = juce::jmax(worst, d);
                if (d > 0.05)
                {
                    ++jumps;
                    if (positions.size() < 6) positions.push_back(i);
                }
            }
            // Spacing between jumps tells us which wrap is responsible.
            double spacingMs = 0.0;
            if (positions.size() >= 2)
            {
                spacingMs = (positions[1] - positions[0]) * 1000.0 / kSampleRate;
            }
            return std::make_tuple(worst, jumps, spacingMs);
        };

        std::printf("  routing = micro-looper only (isolates the loop channel)\n");
        std::printf("  loop      wet        worst step   jumps>0.05   spacing ms\n");
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            const auto [worst, jumps, spacing] = scan(loopMode, 1, 1.0f);
            std::printf("    %-8s %-9s  %9.5f  %10d  %9.1f%s\n",
                        loopNames[loopMode], "(bypassed)", worst, jumps, spacing,
                        jumps > 0 ? "   <-- discontinuities" : "");
        }

        std::printf("\n  routing = input only (isolates the wet channel)\n");
        std::printf("  loop      wet        worst step   jumps>0.05   spacing ms\n");
        for (int wetMode = 0; wetMode < 3; ++wetMode)
        {
            const auto [worst, jumps, spacing] = scan(1, wetMode, 0.0f);
            std::printf("    %-8s %-9s  %9.5f  %10d  %9.1f%s\n",
                        "(n/a)", wetNames[wetMode], worst, jumps, spacing,
                        jumps > 0 ? "   <-- discontinuities" : "");
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "moodmetrics")
    {
        std::printf("\nMOOD CHARACTERISATION (driven on the Mood class)\n");
        std::printf("  sep = channel separation in dB with a hard-left input (high = stereo preserved)\n");
        std::printf("  S/M = side-to-mid ratio (0 = the output is mono)\n");
        std::printf("  corr = inter-channel correlation\n\n");

        static const char* loopNames[] = { "ENV", "TAPE", "STRETCH" };
        static const char* wetNames[] = { "REVERB", "DELAY", "SLIP" };

        std::printf("  loop mode  wet mode    spread   corr     S/M      sep dB    rms      peak\n");
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                for (const auto spread : { 0.0f, 1.0f })
                {
                    MoodSettings s;
                    s.mix = 1.0f;
                    s.loopModeIndex = loopMode;
                    s.wetModeIndex = wetMode;
                    s.spread = spread;
                    s.routing = 1.0f;      // input + micro-looper
                    s.feedback = 0.4f;
                    const auto m = measureMood(s);
                    const auto sep = measureMood(s, true);
                    std::printf("  %-10s %-10s %5.2f  %+.4f  %.4f  %+8.2f  %.6f  %.4f%s\n",
                                loopNames[loopMode], wetNames[wetMode], spread,
                                m.interChannelCorrelation, m.sideToMidRatio,
                                sep.channelSeparationDb, m.rms, m.peak,
                                m.finite ? "" : "  NON-FINITE");
                }
            }
        }

        std::printf("\n  does SPREAD widen? (side-to-mid ratio should rise with the knob)\n");
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            std::printf("    loop %-8s", loopNames[loopMode]);
            for (const auto spread : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.loopModeIndex = loopMode;
                s.wetModeIndex = 0;
                s.spread = spread;
                s.routing = 0.5f;
                std::printf("  %.4f", measureMood(s).sideToMidRatio);
            }
            std::printf("\n");
        }

        // CLOCK is a sample rate, so what it changes is PITCH and length, not
        // level. It has to be measured against an EXISTING loop: recording and
        // playing back at the same rate preserves pitch by definition, so a
        // render that holds the clock constant throughout shows nothing. The
        // loop is captured at full clock, then frozen, and only then is the
        // clock moved - which is the pedal's own description of the control.
        std::printf("\n  does CLOCK transpose a captured loop? (440 Hz in, playback Hz)\n");
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            std::printf("    loop %-8s", loopNames[loopMode]);
            for (const auto clock : { 1.0f, 0.75f, 0.5f, 0.25f, 0.0f })
            {
                Mood mood;
                mood.prepare(kSampleRate);
                mood.reset();
                MoodSettings ms;
                ms.enabled = true;
                ms.mix = 1.0f;
                ms.loopModeIndex = loopMode;
                ms.loopModify = loopMode == 1 ? 0.70f : 0.75f;   // unity-ish playback speed
                ms.loopLength = 0.5f;
                ms.wetModeIndex = 1;
                ms.wetModify = 0.0f;       // no wet feedback muddying the pitch
                ms.wetTime = 0.0f;
                ms.clock = clock;
                ms.routing = 1.0f;         // micro-looper only
                ms.spread = 0.0f;
                ms.degrade = 0.0f;
                ms.feedback = 0.0f;
                // Capture at full clock.
                ms.clock = 1.0f;
                ms.freeze = false;
                mood.updateForBlock(ms);
                const auto captureSamples = static_cast<int>(kSampleRate * 4.0);
                for (int i = 0; i < captureSamples; ++i)
                {
                    const auto in = std::sin(juce::MathConstants<float>::twoPi * 440.0f
                                             * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.6f;
                    float l = 0.0f, r = 0.0f;
                    mood.processSampleFrame(in, in, l, r);
                }

                // Freeze what was captured, then move the clock.
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
                std::printf("  %7.1f", estimateFrequency(out, static_cast<int>(kSampleRate * 3.0),
                                                         static_cast<int>(kSampleRate * 2.0), 20.0, 2000.0));
            }
            std::printf("   (clock 1.0 -> 0.0)\n");
        }

        std::printf("\n  does DEGRADE do anything? (rms across the knob)\n");
        for (int wetMode = 0; wetMode < 3; ++wetMode)
        {
            std::printf("    wet %-9s", wetNames[wetMode]);
            for (const auto degrade : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.wetModeIndex = wetMode;
                s.degrade = degrade;
                s.routing = 0.0f;
                std::printf("  %.6f", measureMood(s).rms);
            }
            std::printf("\n");
        }

        std::printf("\n");
        return 0;
    }

    if (filter == "delaystress")
    {
        std::printf("\nDELAY STRESS: moving controls, then silence\n");
        std::printf("  feeds audio while sweeping a control, stops the input, and reports\n");
        std::printf("  the tail level 1 s and 10 s after the input stops.\n\n");

        static const char* names[] = { "0 Granular", "1 Tape", "2 AnalogBBD", "3 PingPong",
                                       "4 Stereo", "5 Modulated", "6 Diffusion" };

        // sweepWhich: 0 = none, 1 = time, 2 = feedback, 3 = amount, 4 = sync division
        auto stress = [&](int algo, int sweepWhich, float feedbackLevel)
        {
            Delay delay;
            delay.prepare(kSampleRate);
            delay.reset();

            const auto driveSamples = static_cast<int>(kSampleRate * 6.0);
            const auto tailSamples = static_cast<int>(kSampleRate * 12.0);
            juce::Random random(0x0DE1A1u);

            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(driveSamples + tailSamples));

            const auto blockSize = 64;
            int i = 0;
            const auto total = driveSamples + tailSamples;
            while (i < total)
            {
                const auto progress = juce::jlimit(0.0f, 1.0f,
                                                   static_cast<float>(i) / static_cast<float>(driveSamples));
                DelaySettings s;
                s.enabled = true;
                s.algorithmIndex = algo;
                s.amount = sweepWhich == 3 ? progress : 0.8f;
                s.timeControl = sweepWhich == 1 ? progress : 0.4f;
                s.feedbackControl = sweepWhich == 2 ? progress : feedbackLevel;
                s.syncDivisionIndex = sweepWhich == 4 ? (1 + (i / (int) kSampleRate) % 7) : 0;
                delay.updateForBlock(s);

                for (int j = 0; j < blockSize && i < total; ++j, ++i)
                {
                    float in = 0.0f;
                    if (i < driveSamples)
                    {
                        const auto env = 0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.0002f);
                        in = (std::sin(juce::MathConstants<float>::twoPi * 196.0f
                                       * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.4f
                              + (random.nextFloat() * 0.1f - 0.05f)) * env;
                    }
                    float l = 0.0f, r = 0.0f;
                    delay.processSampleFrame(in, in, l, r);
                    out.push_back(l);
                }
            }

            auto rmsAt = [&](double secondsAfterStop)
            {
                const auto from = driveSamples + static_cast<int>(kSampleRate * secondsAfterStop);
                const auto to = juce::jmin(static_cast<int>(out.size()), from + static_cast<int>(kSampleRate * 0.5));
                if (to <= from) return 0.0;
                double e = 0.0;
                for (int k = from; k < to; ++k) e += static_cast<double>(out[(std::size_t) k]) * out[(std::size_t) k];
                return std::sqrt(e / (to - from));
            };

            bool finite = true;
            double peak = 0.0;
            for (const auto v : out) { if (! std::isfinite(v)) finite = false; peak = juce::jmax(peak, std::abs((double) v)); }

            // Dominant frequency of whatever is left at the end, so a stuck
            // tone can be identified rather than just noticed.
            const auto tailStart = driveSamples + static_cast<int>(kSampleRate * 9.0);
            const auto tailHz = estimateFrequency(out, tailStart, static_cast<int>(kSampleRate * 2.0), 20.0, 8000.0);

            return std::make_tuple(rmsAt(1.0), rmsAt(10.0), peak, finite, tailHz);
        };

        static const char* sweepNames[] = { "static", "TIME sweep", "FEEDBACK sweep",
                                            "AMOUNT sweep", "SYNC changes" };
        for (int algo = 0; algo < 7; ++algo)
        {
            for (int sweep = 0; sweep < 5; ++sweep)
            {
                const auto [r1, r10, peak, finite, hz] = stress(algo, sweep, 0.85f);
                const auto stuck = r10 > 1.0e-5 && r10 > r1 * 0.5;
                std::printf("  %-12s %-15s  +1s %.7f  +10s %.7f  peak %.4f  tailHz %6.1f%s%s\n",
                            names[algo], sweepNames[sweep], r1, r10, peak, hz,
                            finite ? "" : "  NON-FINITE",
                            stuck ? "   STUCK" : "");
            }
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "delaymetrics")
    {
        std::printf("\nDELAY CHARACTERISATION (impulse response, driven on the Delay class)\n\n");
        static const char* names[] = { "0 Granular", "1 Tape", "2 Analog/BBD", "3 Ping-Pong",
                                       "4 Stereo", "5 Modulated", "6 Diffusion" };

        std::printf("  zero-amount transparency (worst sample deviation from input)\n");
        for (int algo = 0; algo < 7; ++algo)
        {
            std::printf("    %-14s %.6f\n", names[algo], delayZeroAmountBleed(algo));
        }

        std::printf("\n  algorithm       echo1 ms  echo2 ms   peak    early rms   late rms   corr   sub-30Hz\n");
        for (int algo = 0; algo < 7; ++algo)
        {
            DelaySettings s;
            s.amount = 0.7f;
            s.timeControl = 0.35f;
            s.feedbackControl = 0.5f;
            s.algorithmIndex = algo;
            const auto m = measureDelay(s);
            std::printf("    %-14s %7.1f  %7.1f  %7.4f  %9.6f  %9.6f  %+.3f  %7.4f%s\n",
                        names[algo], m.firstEchoMs, m.secondEchoMs, m.peak,
                        m.tailRmsEarly, m.tailRmsLate, m.interChannelCorrelation,
                        m.lowFrequencyEnergyRatio, m.finite ? "" : "  NON-FINITE");
        }

        std::printf("\n  stability at maximum feedback (late rms should not exceed early rms)\n");
        for (int algo = 0; algo < 7; ++algo)
        {
            DelaySettings s;
            s.amount = 1.0f;
            s.timeControl = 0.35f;
            s.feedbackControl = 1.0f;
            s.algorithmIndex = algo;
            const auto m = measureDelay(s, kSampleRate, static_cast<int>(kSampleRate * 20.0));
            const auto growth = m.tailRmsEarly > 1.0e-9 ? m.tailRmsLate / m.tailRmsEarly : 0.0;
            std::printf("    %-14s early %.6f  late %.6f  ratio %7.3f  peak %.4f%s\n",
                        names[algo], m.tailRmsEarly, m.tailRmsLate, growth, m.peak,
                        growth > 1.05 ? "   GROWING" : "");
        }

        std::printf("\n  delay time knob sweep (first echo, ms) - should rise monotonically\n");
        for (int algo = 1; algo < 7; ++algo)
        {
            std::printf("    %-14s", names[algo]);
            for (const auto t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                DelaySettings s;
                s.amount = 0.7f;
                s.timeControl = t;
                s.feedbackControl = 0.3f;
                s.algorithmIndex = algo;
                std::printf(" %7.1f", measureDelay(s).firstEchoMs);
            }
            std::printf("\n");
        }

        std::printf("\n  sample-rate consistency (first echo ms / late rms at amount 0.7)\n");
        for (int algo = 1; algo < 7; ++algo)
        {
            std::printf("    %-14s", names[algo]);
            for (const auto sr : { 44100.0, 48000.0, 96000.0 })
            {
                DelaySettings s;
                s.amount = 0.7f;
                s.timeControl = 0.35f;
                s.feedbackControl = 0.6f;
                s.algorithmIndex = algo;
                const auto m = measureDelay(s, sr);
                std::printf("  %6.1f/%.5f", m.firstEchoMs, m.tailRmsLate);
            }
            std::printf("\n");
        }

        std::printf("\n");
        return 0;
    }

    if (filter == "vibemetrics")
    {
        // Objective characterisation of the vibe engine, so "more analog" is
        // measured rather than asserted.
        std::printf("\nVIBE ENGINE METRICS\n");

        // 1. Are the per-voice drift signals independent? Measured on the
        // engine directly: routing this through audio does not work, because
        // juce::Synthesiser retargets the existing voice when the same pitch is
        // played twice, so only one voice would ever sound.
        {
            VibeEngine engine;
            engine.prepare(kSampleRate, 64, 0x13579BDFu);
            VibeEngine::Tuning tuning;
            engine.setTuning(tuning);
            engine.setGlobalAmount(1.0f);

            std::vector<double> a, b, c;
            for (int block = 0; block < 4000; ++block)   // ~43 s
            {
                engine.advance(512, 0.25f);
                a.push_back(engine.getVoiceVariation(0).pitchCents);
                b.push_back(engine.getVoiceVariation(1).pitchCents);
                c.push_back(engine.getVoiceVariation(2).pitchCents);
            }
            auto spread = [](const std::vector<double>& v)
            {
                double mean = 0.0; for (auto x : v) mean += x; mean /= static_cast<double>(v.size());
                double var = 0.0; for (auto x : v) var += (x - mean) * (x - mean);
                return std::sqrt(var / static_cast<double>(v.size()));
            };
            auto correlation = [](const std::vector<double>& x, const std::vector<double>& y)
            {
                double mx = 0, my = 0; const auto n = static_cast<double>(x.size());
                for (std::size_t i = 0; i < x.size(); ++i) { mx += x[i]; my += y[i]; }
                mx /= n; my /= n;
                double sxy = 0, sxx = 0, syy = 0;
                for (std::size_t i = 0; i < x.size(); ++i)
                {
                    sxy += (x[i]-mx)*(y[i]-my); sxx += (x[i]-mx)*(x[i]-mx); syy += (y[i]-my)*(y[i]-my);
                }
                return sxx > 1e-12 && syy > 1e-12 ? sxy / std::sqrt(sxx*syy) : 1.0;
            };
            std::printf("  per-voice pitch drift: movement %.3f / %.3f / %.3f cents (std dev)\n",
                        spread(a), spread(b), spread(c));
            std::printf("  drift correlation voice0-1 %+.3f, voice0-2 %+.3f  (0 = independent)\n",
                        correlation(a, b), correlation(a, c));
        }

        // 2. DC injected by the asymmetry stage.
        {
            for (const auto amount : { 0.0f, 1.0f })
            {
                PX3SynthAudioProcessor processor;
                makePlainPatch(processor);
                setChoice(processor, "osc1Mode", 0);
                setParam(processor, "vibeEnabled", 1.0f);
                setParam(processor, "vibeAmount", amount);
                const auto c = render(processor, 96000, { { 2000, true, 57, 0.9f } });
                std::printf("  DC offset at vibe %.1f: %+.6f  (peak %.4f)\n",
                            amount, c.dcOffset(), c.peak());
            }
        }

        // 3. Noise spectrum tilt. Analog hiss is 1/f weighted; white noise is
        // the giveaway of a digital source.
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            for (int i = 1; i <= 3; ++i) setParam(processor, "osc" + juce::String(i) + "Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 1.0f);
            setParam(processor, "vibeAmount", 1.0f);
            const auto c = render(processor, 96000, { { 2000, true, 57, 0.9f } });
            std::printf("  vibe noise floor with all sources off: rms %.8f\n", c.rms());
        }

        // Which measures actually rise monotonically with the amount control?
        //
        // Its own block. This survey shares nothing with the noise-floor
        // measurement above and was sitting inside that block by accident,
        // which is what made its processor and capture shadow the ones there.
        {
            std::printf("\n  %-8s %10s %10s %10s %10s\n", "amount", "rms", "peak", "crest", "dc");
            for (const auto amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                PX3SynthAudioProcessor processor;
                makePlainPatch(processor);
                setChoice(processor, "osc1Mode", 0);
                setParam(processor, "vibeEnabled", 1.0f);
                setParam(processor, "vibeAmount", amount);
                const auto c = render(processor, 96000, { { 2000, true, 57, 0.9f } });
                const auto r = c.rmsOver(20000, 94000);
                std::printf("  %-8.2f %10.6f %10.6f %10.4f %+10.6f\n",
                            amount, r, c.peak(), c.peak() / juce::jmax(1.0e-9, r), c.dcOffset());
            }
        }

        // 4. Block-size dependence. The character must not change with the
        // host's buffer size.
        {
            for (const auto blockSize : { 64, 512 })
            {
                PX3SynthAudioProcessor processor;
                makePlainPatch(processor);
                setParam(processor, "vibeEnabled", 1.0f);
                setParam(processor, "vibeAmount", 1.0f);
                processor.setPlayConfigDetails(0, 2, kSampleRate, blockSize);
                processor.prepareToPlay(kSampleRate, blockSize);
                juce::AudioBuffer<float> buffer(2, blockSize);
                double energy = 0.0; juce::int64 n = 0;
                const auto blocks = static_cast<int>(8.0 * kSampleRate / blockSize);
                for (int b = 0; b < blocks; ++b)
                {
                    buffer.clear();
                    juce::MidiBuffer midi;
                    if (b == 4) midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.9f), 0);
                    processor.processBlock(buffer, midi);
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const auto v = static_cast<double>(buffer.getSample(0, i));
                        energy += v * v; ++n;
                    }
                }
                std::printf("  block %4d: rms %.6f\n", blockSize, std::sqrt(energy / static_cast<double>(juce::jmax<juce::int64>(1, n))));
            }
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "gainstage")
    {
        // Absolute source-to-master gain at the default fader position and at
        // the top of its travel. Moving the headroom from the fader to the
        // oscillator must leave both of these unchanged.
        std::printf("\n  %-22s %12s %12s %12s\n", "mix.osc1.level", "masterRms", "oscBusRms", "polyGain");
        for (const auto label : { "default", "maximum" })
        {
            PX3SynthAudioProcessor processor;
            // Defaults everywhere except: one oscillator on, no FX, no vibe.
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
            if (juce::String(label) == "maximum")
            {
                if (auto* lv = findParameter(processor, "mix.osc1.level"))
                {
                    lv->setValueNotifyingHost(1.0f); // top of the fader
                }
            }
            const auto capture = render(processor, 48000, { { 2000, true, 57, 0.9f } });
            std::printf("  %-22s %12.6f %12.6f %12.6f\n", label, capture.rmsOver(24000, 46000),
                        processor.debugGetOscillatorBusRms(), processor.debugGetPolyphonyGainApplied());
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "probe")
    {
        // Diagnostic probe, not an assertion: prints the level at each stage so
        // an unexpected measurement can be attributed to a stage rather than
        // guessed at.
        std::printf("\n  %-10s %12s %12s %12s %12s\n",
                    "mix.osc1.level", "masterRms", "oscBusRms", "polyGain", "srcRms");
        for (const auto level : { 0.125f, 0.25f, 0.5f, 1.0f })
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "mix.osc1.level", level);
            const auto capture = render(processor, 48000, { { 2000, true, 57, 0.9f } });
            std::printf("  %-10.3f %12.6f %12.6f %12.6f %12.6f\n",
                        level, capture.rms(),
                        processor.debugGetOscillatorBusRms(),
                        processor.debugGetPolyphonyGainApplied(),
                        processor.debugGetMixerSourceRms(1));
        }

        std::printf("\n  %-14s %12s %12s %12s\n", "sources", "masterRms", "oscBusRms", "polyGain");
        for (int count = 1; count <= 3; ++count)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "osc2Enabled", count >= 2 ? 1.0f : 0.0f);
            setParam(processor, "osc2Coarse", 7.0f);
            setParam(processor, "osc3Enabled", count >= 3 ? 1.0f : 0.0f);
            setParam(processor, "osc3Coarse", 12.0f);
            const auto capture = render(processor, 48000, { { 2000, true, 57, 0.9f } });
            std::printf("  %-14d %12.6f %12.6f %12.6f\n",
                        count, capture.rms(),
                        processor.debugGetOscillatorBusRms(),
                        processor.debugGetPolyphonyGainApplied());
        }

        std::printf("\n  VIBE amount sweep\n  %-10s %12s %12s\n", "amount", "rms", "peak");
        for (const auto amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "filter1Enabled", 1.0f);
            setParam(processor, "filter1Cutoff", 2500.0f);
            setParam(processor, "vibeEnabled", 1.0f);
            setParam(processor, "vibeAmount", amount);
            const auto capture = render(processor, 48000, { { 2000, true, 45, 0.9f } });
            std::printf("  %-10.3f %12.6f %12.6f\n", amount, capture.rms(), capture.peak());
        }

        std::printf("\n  KARPLUS macroA sweep (mode 13)\n  %-10s %12s\n", "macroA", "rms");
        for (const auto macro : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 13);
            setParam(processor, "osc1MacroA", macro);
            const auto capture = render(processor, 48000, { { 2000, true, 57, 0.9f } });
            std::printf("  %-10.3f %12.6f\n", macro, capture.rms());
        }
        return 0;
    }

    if (wants("envelope")) testBreakpointEnvelope();
    if (wants("wavetable")) testWavetable();
    if (wants("subosc")) testSubOscillator();
    if (wants("osc")) testOscillators();
    if (wants("ampenv")) testAmpEnvelope();
    if (wants("modenv")) testModEnvelopes();
    if (wants("lfo")) testLfo();
    if (wants("vibe")) testVibe();
    if (wants("reverb")) testReverb();
    if (wants("comb")) testComb();
    if (wants("cardstyle")) testCardStyle();
    if (wants("cardinner")) testCardInner();
    if (wants("fxchain")) testFxChain();
    if (wants("doom")) testDoom();
    if (wants("lucy")) testLucy();
    if (wants("chorus")) testChorus();
    if (wants("spread")) testStereoSpread();
    if (wants("delay")) testDelay();
    if (wants("mood")) testMood();
    if (wants("fx")) testEffectIndependence();
    if (wants("preset")) testPresets();
    if (wants("factorypresets")) testFactoryPresets();
    if (wants("midimapping")) testMidiMapping();
    if (wants("macro")) testMacroSystem();
    if (wants("envmode")) testEnvelopeModes();
    if (wants("vumeter")) testVuBallistics();
    if (wants("businserts")) testBusInserts();
    if (wants("multiout")) testMultiOutput();
    if (wants("updater")) testUpdater();
    if (wants("ecosystem")) testEcosystem();
    if (wants("fxproducts")) testFxProducts();
    if (wants("uninstaller")) testUninstaller();
    if (wants("filters")) testFilters();
    if (wants("oscrichness")) testOscillatorModeRichness();
    if (wants("analog")) testAnalogEngine();
    if (wants("editor")) testEditorLifecycle();
    if (wants("editorlayout")) testEditorLayout();
    if (wants("integration")) testIntegration();

    std::printf("\n------------------------------------------------------------\n");
    std::printf("  %d passed, %d failed\n", gPassed, gFailed);
    if (! gFailures.empty())
    {
        std::printf("\n  failures:\n");
        for (const auto& failure : gFailures)
        {
            std::printf("    - %s\n", failure.c_str());
        }
    }
    std::printf("\n");
    return gFailed == 0 ? 0 : 1;
}
