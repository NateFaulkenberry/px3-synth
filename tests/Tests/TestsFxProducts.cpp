#include "TestSupport.h"

#include "../../products/PX3Delay/PluginProcessor.h"

// testFxProducts
//
// The standalone FX plug-ins.
//
// What matters here is not that the effect works - the Synth's suite already
// proves that, and there is only ONE implementation, so proving it twice would
// prove nothing new. What matters is that the wrapper around it is a correct
// plug-in: right identity, right buses, state that survives a round trip, and
// audio that actually reaches the effect.

namespace px3tests
{

void testFxProducts()
{
    suite("FX PRODUCTS");

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    const auto prepared = [](juce::AudioProcessor& p)
    {
        p.setPlayConfigDetails(2, 2, kRate, kBlock);
        p.prepareToPlay(kRate, kBlock);
    };

    // A short burst then silence, so a delay's repeats are what remains.
    const auto fillImpulse = [](juce::AudioBuffer<float>& buffer)
    {
        buffer.clear();
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            for (int i = 0; i < juce::jmin(64, buffer.getNumSamples()); ++i)
            {
                buffer.setSample(c, i, std::sin(static_cast<float>(i) * 0.3f) * 0.7f);
            }
        }
    };

    // ---- it is an audio effect, not an instrument --------------------------
    {
        PX3DelayAudioProcessor delay;

        check("FxProduct_DelayIsAStereoAudioEffect",
              ! delay.acceptsMidi() && ! delay.isMidiEffect()
                  && delay.getName() == "PX3 Delay"
                  && delay.getTotalNumInputChannels() == 2
                  && delay.getTotalNumOutputChannels() == 2,
              delay.getName() + ": " + juce::String(delay.getTotalNumInputChannels()) + " in, "
                  + juce::String(delay.getTotalNumOutputChannels()) + " out, midi "
                  + (delay.acceptsMidi() ? "YES" : "no"));
    }

    // ---- the layouts a host may ask for -------------------------------------
    {
        PX3DelayAudioProcessor delay;

        const auto layout = [](int in, int out)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add(in == 1 ? juce::AudioChannelSet::mono()
                                     : juce::AudioChannelSet::stereo());
            l.outputBuses.add(out == 1 ? juce::AudioChannelSet::mono()
                                       : juce::AudioChannelSet::stereo());
            return l;
        };

        const auto stereo = delay.checkBusesLayoutSupported(layout(2, 2));
        const auto mono = delay.checkBusesLayoutSupported(layout(1, 1));
        const auto mismatched = delay.checkBusesLayoutSupported(layout(1, 2));

        check("FxProduct_DelayTakesStereoAndMonoButNotAMismatch",
              stereo && mono && ! mismatched,
              juce::String("stereo ") + (stereo ? "yes" : "NO") + ", mono "
                  + (mono ? "yes" : "NO") + ", mono-in/stereo-out "
                  + (mismatched ? "ACCEPTED" : "refused"));
    }

    // ---- audio reaches the effect ------------------------------------------
    //
    // A delay whose output equals its input has been wired up wrong in a way
    // no other check here would notice.
    {
        PX3DelayAudioProcessor delay;
        prepared(delay);
        delay.debugEnabledParam().setValueNotifyingHost(1.0f);
        delay.debugAmountParam().setValueNotifyingHost(1.0f);
        delay.debugFeedbackParam().setValueNotifyingHost(0.6f);

        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;

        fillImpulse(buffer);
        juce::AudioBuffer<float> input(2, kBlock);
        input.makeCopyOf(buffer);

        delay.processBlock(buffer, midi);

        auto changed = false;
        auto finite = true;
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const auto out = buffer.getSample(c, i);
                if (! std::isfinite(out)) { finite = false; }
                if (std::abs(out - input.getSample(c, i)) > 1.0e-6f) { changed = true; }
            }
        }

        // Then silence, and the repeats have to arrive. Looked for over a
        // second rather than in the next block: at 256 samples a block is
        // 5.3 ms, which is shorter than the delay, so the first version of
        // this looked before the repeat could possibly have come back.
        auto tail = 0.0f;
        for (int block = 0; block < 200; ++block)
        {
            buffer.clear();
            delay.processBlock(buffer, midi);
            tail = juce::jmax(tail, buffer.getMagnitude(0, kBlock));
        }

        check("FxProduct_DelayProcessesAudioAndKeepsItsTail",
              changed && finite && tail > 1.0e-5f,
              juce::String(changed ? "the block was processed" : "OUTPUT MATCHED INPUT")
                  + ", loudest repeat over the second after the input stopped "
                  + fmt(tail, 6));
    }

    // ---- silence in, silence out -------------------------------------------
    {
        PX3DelayAudioProcessor delay;
        prepared(delay);

        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        buffer.clear();

        for (int i = 0; i < 4; ++i) { delay.processBlock(buffer, midi); }

        check("FxProduct_DelayIsSilentOnSilence",
              buffer.getMagnitude(0, kBlock) < 1.0e-7f,
              "peak on silence " + fmt(buffer.getMagnitude(0, kBlock), 8));
    }

    // ---- state survives a round trip ---------------------------------------
    {
        PX3DelayAudioProcessor source;
        prepared(source);
        source.debugAmountParam().setValueNotifyingHost(0.83f);
        source.debugTimeParam().setValueNotifyingHost(0.21f);
        source.debugFeedbackParam().setValueNotifyingHost(0.64f);
        source.debugAlgorithmParam().setValueNotifyingHost(
            source.debugAlgorithmParam().convertTo0to1(3.0f));
        source.debugEnabledParam().setValueNotifyingHost(0.0f);

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3DelayAudioProcessor reopened;
        prepared(reopened);
        reopened.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("FxProduct_DelayStateSurvivesASaveAndReload",
              std::abs(reopened.debugAmountParam().get() - 0.83f) < 1.0e-3f
                  && std::abs(reopened.debugTimeParam().get() - 0.21f) < 1.0e-3f
                  && std::abs(reopened.debugFeedbackParam().get() - 0.64f) < 1.0e-3f
                  && reopened.debugAlgorithmParam().getIndex() == 3
                  && ! reopened.debugEnabledParam().get(),
              "reloaded amount " + fmt(reopened.debugAmountParam().get(), 3)
                  + ", time " + fmt(reopened.debugTimeParam().get(), 3)
                  + ", algorithm " + juce::String(reopened.debugAlgorithmParam().getIndex())
                  + ", enabled " + (reopened.debugEnabledParam().get() ? "yes" : "no"));
    }

    // ---- the standalone reads its parameters directly ----------------------
    //
    // The Synth builds the same settings through its modulation accumulator so
    // an LFO can move the delay time. There is nothing to modulate with here,
    // and this is the one place the two consumers differ - so it is worth
    // saying out loud rather than leaving implied.
    {
        PX3DelayAudioProcessor delay;
        prepared(delay);
        delay.debugTimeParam().setValueNotifyingHost(0.7f);
        delay.debugFeedbackParam().setValueNotifyingHost(0.25f);

        const auto settings = delay.debugSettingsForBlock();

        check("FxProduct_DelaySettingsComeStraightFromItsParameters",
              std::abs(settings.timeControl - 0.7f) < 1.0e-4f
                  && std::abs(settings.feedbackControl - 0.25f) < 1.0e-4f
                  && settings.bpm > 0.0,
              "time " + fmt(settings.timeControl, 3) + ", feedback "
                  + fmt(settings.feedbackControl, 3) + ", bpm " + fmt((float) settings.bpm, 1));
    }

    // ---- sample rate and block size changes --------------------------------
    {
        PX3DelayAudioProcessor delay;
        auto survived = true;

        for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const auto block : { 32, 256, 1024 })
            {
                delay.setPlayConfigDetails(2, 2, rate, block);
                delay.prepareToPlay(rate, block);

                juce::AudioBuffer<float> buffer(2, block);
                juce::MidiBuffer midi;
                buffer.clear();
                for (int i = 0; i < juce::jmin(16, block); ++i) { buffer.setSample(0, i, 0.5f); }

                delay.processBlock(buffer, midi);

                for (int c = 0; c < 2 && survived; ++c)
                {
                    for (int i = 0; i < block; ++i)
                    {
                        if (! std::isfinite(buffer.getSample(c, i))) { survived = false; break; }
                    }
                }
            }
        }

        check("FxProduct_DelaySurvivesEverySampleRateAndBlockSize",
              survived,
              survived ? juce::String("finite at 44.1/48/96 kHz across 32, 256 and 1024 samples")
                       : juce::String("NON-FINITE OUTPUT"));
    }
}

} // namespace px3tests
