#include "TestSupport.h"

#include "../../products/PX3Delay/PluginProcessor.h"
#include "../../products/PX3Mood/PluginProcessor.h"
#include "../../products/PX3Chorus/PluginProcessor.h"
#include "../../products/PX3Spread/PluginProcessor.h"
#include "../../products/PX3Reverb/PluginProcessor.h"
#include "../../products/PX3Doom/PluginProcessor.h"
#include "../../products/PX3Lucy/PluginProcessor.h"
#include "../../shared/Infrastructure/Fx/FxCardEditor.h"

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

    // ========================================================================
    // PX3 Mood
    // ========================================================================
    {
        PX3MoodAudioProcessor mood;
        prepared(mood);

        check("FxProduct_MoodIsAStereoAudioEffect",
              ! mood.acceptsMidi() && mood.getName() == "PX3 Mood"
                  && mood.getTotalNumInputChannels() == 2
                  && mood.getTotalNumOutputChannels() == 2,
              mood.getName() + ": " + juce::String(mood.getTotalNumInputChannels()) + " in, "
                  + juce::String(mood.getTotalNumOutputChannels()) + " out");
    }

    {
        PX3MoodAudioProcessor mood;
        prepared(mood);
        mood.enabled().setValueNotifyingHost(1.0f);
        mood.mix().setValueNotifyingHost(1.0f);

        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        fillImpulse(buffer);
        juce::AudioBuffer<float> input(2, kBlock);
        input.makeCopyOf(buffer);

        mood.processBlock(buffer, midi);

        auto changed = false, finite = true;
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const auto out = buffer.getSample(c, i);
                if (! std::isfinite(out)) { finite = false; }
                if (std::abs(out - input.getSample(c, i)) > 1.0e-6f) { changed = true; }
            }
        }

        check("FxProduct_MoodProcessesAudio",
              changed && finite,
              changed ? juce::String("the block was processed and stayed finite")
                      : juce::String("OUTPUT MATCHED INPUT"));
    }

    {
        // Routing is a three-way choice the DSP reads as 0..1. The Synth
        // converts it as index/2; getting that wrong here would put PARALLEL
        // somewhere the Synth never puts it, and the effect would differ
        // between the two products for no visible reason.
        PX3MoodAudioProcessor mood;
        prepared(mood);

        juce::StringArray seen;
        auto correct = true;
        for (int index = 0; index < 3; ++index)
        {
            mood.routing().setValueNotifyingHost(mood.routing().convertTo0to1((float) index));
            const auto routing = mood.debugSettingsForBlock().routing;
            const auto expected = static_cast<float>(index) / 2.0f;
            if (std::abs(routing - expected) > 1.0e-4f) { correct = false; }
            seen.add(juce::String(index) + " -> " + fmt(routing, 2));
        }

        check("FxProduct_MoodRoutingMapsTheWayTheSynthMapsIt",
              correct, seen.joinIntoString(", "));
    }

    {
        PX3MoodAudioProcessor source;
        prepared(source);
        source.mix().setValueNotifyingHost(0.77f);
        source.feedback().setValueNotifyingHost(0.42f);
        source.wetMode().setValueNotifyingHost(source.wetMode().convertTo0to1(2.0f));
        source.freeze().setValueNotifyingHost(1.0f);

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3MoodAudioProcessor reopened;
        prepared(reopened);
        reopened.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("FxProduct_MoodStateSurvivesASaveAndReload",
              std::abs(reopened.mix().get() - 0.77f) < 1.0e-3f
                  && std::abs(reopened.feedback().get() - 0.42f) < 1.0e-3f
                  && reopened.wetMode().getIndex() == 2
                  && reopened.freeze().get(),
              "mix " + fmt(reopened.mix().get(), 3) + ", feedback "
                  + fmt(reopened.feedback().get(), 3) + ", wet mode "
                  + juce::String(reopened.wetMode().getIndex())
                  + ", freeze " + (reopened.freeze().get() ? "on" : "off"));
    }

    // ========================================================================
    // PX3 Chorus
    // ========================================================================
    {
        PX3ChorusAudioProcessor chorus;
        prepared(chorus);
        chorus.enabled().setValueNotifyingHost(1.0f);

        // Fed continuously rather than judged on the first block: the wet path
        // is a modulated delay, so on block one the line is still empty and the
        // output is legitimately the input. Looking there said "OUTPUT MATCHED
        // INPUT" about a chorus that works.
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::AudioBuffer<float> input(2, kBlock);
        juce::MidiBuffer midi;

        auto changed = false, finite = true;
        for (int block = 0; block < 12; ++block)
        {
            for (int c = 0; c < 2; ++c)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const auto phase = static_cast<float>(block * kBlock + i) * 0.04f;
                    buffer.setSample(c, i, std::sin(phase) * 0.6f);
                }
            }
            input.makeCopyOf(buffer);

            chorus.processBlock(buffer, midi);

            for (int c = 0; c < 2; ++c)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const auto out = buffer.getSample(c, i);
                    if (! std::isfinite(out)) { finite = false; }
                    if (std::abs(out - input.getSample(c, i)) > 1.0e-4f) { changed = true; }
                }
            }
        }

        check("FxProduct_ChorusProcessesAudio",
              changed && finite && chorus.getName() == "PX3 Chorus",
              changed ? juce::String("processed and finite") : juce::String("OUTPUT MATCHED INPUT"));
    }

    {
        // Tone is BIPOLAR in the Synth: -1 warm, +1 clear. Declaring it 0..1
        // here would move its centre and silently reinterpret every stored
        // value, so the range is checked rather than assumed.
        PX3ChorusAudioProcessor chorus;
        const auto& range = chorus.tone().getNormalisableRange();

        check("FxProduct_ChorusToneStaysBipolarLikeTheSynths",
              std::abs(range.start + 1.0f) < 1.0e-6f && std::abs(range.end - 1.0f) < 1.0e-6f
                  && std::abs(chorus.tone().get()) < 1.0e-6f,
              "tone ranges " + fmt(range.start, 1) + " to " + fmt(range.end, 1)
                  + ", centred at " + fmt(chorus.tone().get(), 2));
    }

    // ========================================================================
    // PX3 Spread
    // ========================================================================
    //
    // A widener is where a wrapper mistake is least visible: swap the channels
    // and it still "works", lose the side signal and it just sounds narrow.
    // These check the routing and the phase rather than only that audio came
    // out the other end.
    {
        PX3SpreadAudioProcessor spreadFx;
        prepared(spreadFx);
        spreadFx.enabled().setValueNotifyingHost(1.0f);
        spreadFx.amount().setValueNotifyingHost(1.0f);
        spreadFx.width().setValueNotifyingHost(1.0f);

        // Hard-panned left: whatever the widener does, the left channel must
        // stay the loud one. If the wrapper swapped the channels this is the
        // only test that would notice.
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample(0, i, std::sin(static_cast<float>(i) * 0.05f) * 0.8f);
        }

        juce::MidiBuffer midi;
        spreadFx.processBlock(buffer, midi);

        const auto leftRms = buffer.getRMSLevel(0, 0, kBlock);
        const auto rightRms = buffer.getRMSLevel(1, 0, kBlock);

        check("FxProduct_SpreadKeepsAHardLeftSignalOnTheLeft",
              leftRms > rightRms && leftRms > 1.0e-3f,
              "left rms " + fmt(leftRms, 4) + " against right " + fmt(rightRms, 4));
    }

    {
        // MONO SAFE, at full width, must not destroy the sum. A widener that
        // inverts one side sounds enormous and disappears the moment anything
        // downstream folds to mono - the classic failure, and invisible until
        // somebody plays it on a phone.
        PX3SpreadAudioProcessor spreadFx;
        prepared(spreadFx);
        spreadFx.enabled().setValueNotifyingHost(1.0f);
        spreadFx.amount().setValueNotifyingHost(1.0f);
        spreadFx.width().setValueNotifyingHost(1.0f);
        spreadFx.mode().setValueNotifyingHost(spreadFx.mode().convertTo0to1(3.0f));  // MONO SAFE

        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        for (int i = 0; i < kBlock; ++i)
        {
            const auto v = std::sin(static_cast<float>(i) * 0.05f) * 0.6f;
            buffer.setSample(0, i, v);
            buffer.setSample(1, i, v);
        }

        juce::MidiBuffer midi;
        for (int b = 0; b < 8; ++b) { spreadFx.processBlock(buffer, midi); }

        // The mono sum, which is what a phone speaker hears.
        auto sumEnergy = 0.0;
        for (int i = 0; i < kBlock; ++i)
        {
            const auto mono = 0.5f * (buffer.getSample(0, i) + buffer.getSample(1, i));
            sumEnergy += static_cast<double>(mono) * mono;
        }
        const auto monoRms = std::sqrt(sumEnergy / kBlock);

        check("FxProduct_SpreadMonoSafeSurvivesAFoldToMono",
              monoRms > 1.0e-3f,
              "the mono sum after MONO SAFE at full width is rms " + fmt((float) monoRms, 5)
                  + " - a widener that inverted a side would collapse here");
    }

    {
        PX3SpreadAudioProcessor source;
        prepared(source);
        source.width().setValueNotifyingHost(0.9f);
        source.mode().setValueNotifyingHost(source.mode().convertTo0to1(2.0f));

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3SpreadAudioProcessor reopened;
        prepared(reopened);
        reopened.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("FxProduct_SpreadStateSurvivesASaveAndReload",
              std::abs(reopened.width().get() - 0.9f) < 1.0e-3f
                  && reopened.mode().getIndex() == 2,
              "width " + fmt(reopened.width().get(), 3) + ", mode "
                  + juce::String(reopened.mode().getIndex()));
    }

    // ========================================================================
    // The remaining products, on the checks that apply to all of them
    // ========================================================================
    //
    // Not the effects - one implementation, already covered by the Synth's
    // suite, and proving it twice would prove nothing. These are the wrapper:
    // it is an audio effect, it passes audio through the shared DSP, it stays
    // finite, and its state survives a round trip.
    {
        const auto exercise = [&](juce::AudioProcessor& fx,
                                  const juce::String& expectedName)
        {
            prepared(fx);

            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::MidiBuffer midi;

            auto finite = true;
            for (int block = 0; block < 8; ++block)
            {
                for (int c = 0; c < 2; ++c)
                {
                    for (int i = 0; i < kBlock; ++i)
                    {
                        const auto phase = static_cast<float>(block * kBlock + i) * 0.04f;
                        buffer.setSample(c, i, std::sin(phase) * 0.6f);
                    }
                }
                fx.processBlock(buffer, midi);

                for (int c = 0; c < 2; ++c)
                {
                    for (int i = 0; i < kBlock; ++i)
                    {
                        if (! std::isfinite(buffer.getSample(c, i))) { finite = false; }
                    }
                }
            }

            const auto correct = finite
                              && fx.getName() == expectedName
                              && ! fx.acceptsMidi()
                              && fx.getTotalNumInputChannels() == 2
                              && fx.getTotalNumOutputChannels() == 2;

            check(("FxProduct_" + expectedName.replace("PX3 ", "") + "IsAWorkingStereoEffect").toRawUTF8(),
                  correct,
                  fx.getName() + ": stereo in and out, midi "
                      + (fx.acceptsMidi() ? "YES" : "no") + ", output "
                      + (finite ? "finite over 8 blocks" : "NON-FINITE"));
        };

        PX3ReverbAudioProcessor reverb;  exercise(reverb, "PX3 Reverb");
        PX3DoomAudioProcessor doom;      exercise(doom, "PX3 Doom");
        PX3LucyAudioProcessor lucy;      exercise(lucy, "PX3 Lucy");
    }

    {
        // The parameter ranges that are NOT 0..1. Three products got this
        // wrong in draft, so each one that differs is checked rather than
        // trusted: Doom's EQ is a tilt, Lucy's weighting is a tilt, and Lucy's
        // gain is in decibels. Declaring any of them 0..1 would silently
        // reinterpret every value a user had stored.
        PX3DoomAudioProcessor doom;
        PX3LucyAudioProcessor lucy;

        const auto& doomEq = doom.eq().getNormalisableRange();
        const auto& weighting = lucy.weighting().getNormalisableRange();
        const auto& gain = lucy.gain().getNormalisableRange();

        check("FxProduct_TheParametersThatAreNotUnitRangesKeptTheirRanges",
              std::abs(doomEq.start + 1.0f) < 1.0e-6f && std::abs(doomEq.end - 1.0f) < 1.0e-6f
                  && std::abs(weighting.start + 1.0f) < 1.0e-6f
                  && std::abs(gain.start + 36.0f) < 1.0e-4f
                  && std::abs(gain.end - 36.0f) < 1.0e-4f,
              "Doom EQ " + fmt(doomEq.start, 1) + ".." + fmt(doomEq.end, 1)
                  + ", Lucy weighting " + fmt(weighting.start, 1) + ".." + fmt(weighting.end, 1)
                  + ", Lucy gain " + fmt(gain.start, 1) + ".." + fmt(gain.end, 1) + " dB");
    }

    {
        // State, for the two with the most of it.
        PX3DoomAudioProcessor source;
        prepared(source);
        source.mix().setValueNotifyingHost(0.66f);
        source.glue().setValueNotifyingHost(0.31f);
        source.wetMode().setValueNotifyingHost(source.wetMode().convertTo0to1(2.0f));
        source.freeze().setValueNotifyingHost(1.0f);

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3DoomAudioProcessor reopened;
        prepared(reopened);
        reopened.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("FxProduct_DoomStateSurvivesASaveAndReload",
              std::abs(reopened.mix().get() - 0.66f) < 1.0e-3f
                  && std::abs(reopened.glue().get() - 0.31f) < 1.0e-3f
                  && reopened.wetMode().getIndex() == 2 && reopened.freeze().get(),
              "mix " + fmt(reopened.mix().get(), 3) + ", glue " + fmt(reopened.glue().get(), 3)
                  + ", wet mode " + juce::String(reopened.wetMode().getIndex())
                  + ", freeze " + (reopened.freeze().get() ? "on" : "off"));
    }

    {
        PX3LucyAudioProcessor source;
        prepared(source);
        source.loss().setValueNotifyingHost(0.9f);
        // Through the whole -36..+36 dB range, which a 0..1 copy would mangle.
        source.gain().setValueNotifyingHost(source.gain().convertTo0to1(-12.0f));
        source.mode().setValueNotifyingHost(source.mode().convertTo0to1(1.0f));

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3LucyAudioProcessor reopened;
        prepared(reopened);
        reopened.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("FxProduct_LucyStateSurvivesASaveAndReloadIncludingItsDecibelGain",
              std::abs(reopened.loss().get() - 0.9f) < 1.0e-3f
                  && std::abs(reopened.gain().get() + 12.0f) < 0.1f
                  && reopened.mode().getIndex() == 1,
              "loss " + fmt(reopened.loss().get(), 3) + ", gain "
                  + fmt(reopened.gain().get(), 2) + " dB, mode "
                  + juce::String(reopened.mode().getIndex()));
    }

    {
        // Every product must survive the rates and block sizes a host uses.
        auto survived = true;
        juce::StringArray notes;

        const auto sweep = [&](juce::AudioProcessor& fx, const char* name)
        {
            for (const auto rate : { 44100.0, 96000.0 })
            {
                for (const auto block : { 32, 1024 })
                {
                    fx.setPlayConfigDetails(2, 2, rate, block);
                    fx.prepareToPlay(rate, block);

                    juce::AudioBuffer<float> buffer(2, block);
                    juce::MidiBuffer midi;
                    buffer.clear();
                    for (int i = 0; i < juce::jmin(16, block); ++i) { buffer.setSample(0, i, 0.5f); }
                    fx.processBlock(buffer, midi);

                    for (int c = 0; c < 2; ++c)
                    {
                        for (int i = 0; i < block; ++i)
                        {
                            if (! std::isfinite(buffer.getSample(c, i)))
                            {
                                survived = false;
                                notes.addIfNotAlreadyThere(name);
                            }
                        }
                    }
                }
            }
        };

        PX3DelayAudioProcessor delay;   sweep(delay, "Delay");
        PX3MoodAudioProcessor mood;     sweep(mood, "Mood");
        PX3ChorusAudioProcessor chorus; sweep(chorus, "Chorus");
        PX3SpreadAudioProcessor spread; sweep(spread, "Spread");
        PX3ReverbAudioProcessor reverb; sweep(reverb, "Reverb");
        PX3DoomAudioProcessor doom;     sweep(doom, "Doom");
        PX3LucyAudioProcessor lucy;     sweep(lucy, "Lucy");

        check("FxProduct_EveryProductSurvivesTheRatesAndBlockSizesAHostUses",
              survived,
              survived ? juce::String("all seven finite at 44.1 and 96 kHz, 32 and 1024 samples")
                       : "NON-FINITE: " + notes.joinIntoString(", "));
    }

    // ---- bypass actually bypasses -----------------------------------------
    //
    // Inside the Synth a disabled stage is skipped by the chain, so the card's
    // switch works whether or not the effect implements bypass itself. Standing
    // alone there is no chain: FxPluginProcessor::processBlock calls
    // processFxBlock every block, and the switch only does anything if the
    // effect honours the flag it was handed.
    //
    // Measured rather than reasoned about: some effects ramp their wet mix to
    // zero, which is a bypass, and others do not.
    {
        // A signal with content at several frequencies, so an effect that only
        // alters part of the spectrum still shows up as a difference.
        const auto makeInput = [](juce::AudioBuffer<float>& buffer)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                auto* data = buffer.getWritePointer(channel);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const auto t = static_cast<float>(i) / static_cast<float>(kRate);
                    data[i] = 0.30f * std::sin(juce::MathConstants<float>::twoPi * 220.0f * t)
                            + 0.20f * std::sin(juce::MathConstants<float>::twoPi * 1500.0f * t)
                            + (channel == 1 ? 0.10f : 0.0f);
                }
            }
        };

        // Runs long enough for any ramp to settle, then compares the LAST block
        // against the dry signal. Comparing the first block would fail an
        // effect that bypasses correctly but fades into it.
        const auto worstDifferenceFromDry = [&](auto& processor,
                                                juce::AudioParameterBool& enabled,
                                                bool enabledState)
        {
            processor.setPlayConfigDetails(2, 2, kRate, kBlock);
            processor.prepareToPlay(kRate, kBlock);
            enabled.setValueNotifyingHost(enabledState ? 1.0f : 0.0f);

            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::AudioBuffer<float> dry(2, kBlock);
            juce::MidiBuffer midi;

            auto worst = 0.0f;
            for (int block = 0; block < 40; ++block)
            {
                makeInput(buffer);
                dry.makeCopyOf(buffer);
                processor.processBlock(buffer, midi);

                if (block < 32) { continue; }   // let any fade finish

                for (int channel = 0; channel < 2; ++channel)
                {
                    const auto* out = buffer.getReadPointer(channel);
                    const auto* in = dry.getReadPointer(channel);
                    for (int i = 0; i < kBlock; ++i)
                    {
                        worst = juce::jmax(worst, std::abs(out[i] - in[i]));
                    }
                }
            }
            return worst;
        };

        juce::StringArray leaking;
        juce::StringArray inaudible;
        juce::StringArray measured;

        // Both directions, because only one of them is a claim about bypass.
        //
        // "Bypassed output equals the input" passes on its own for an effect
        // that does nothing at all - a default amount of zero, a stage that was
        // never prepared - and would report every product healthy while proving
        // none of them were. The enabled measurement is the control: an effect
        // has to CHANGE the signal before its bypass leaving the signal alone
        // means anything.
        const auto record = [&](const juce::String& name, float wet, float dry)
        {
            measured.add(name + " on " + juce::String(wet, 4)
                         + " / off " + juce::String(dry, 4));

            if (wet <= 0.001f) { inaudible.add(name); }
            if (dry > 0.001f) { leaking.add(name + " (" + juce::String(dry, 4) + ")"); }
        };

        // Turning the effect UP before measuring it.
        //
        // Doom and Lucy ship fully dry - doomMix and lucyGlobal both default to
        // zero - which is a deliberate default and not a fault, but it means
        // measuring them at defaults compares silence with silence. Anything
        // named here is set before the comparison so the control is a real one.
        const auto turnUp = [](juce::AudioProcessor& processor,
                               const juce::String& parameterId, float value)
        {
            for (auto* parameter : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                {
                    if (ranged->getParameterID() == parameterId)
                    {
                        ranged->setValueNotifyingHost(value);
                        return true;
                    }
                }
            }
            return false;
        };

        const auto measure = [&](const juce::String& name, auto& processor,
                                 juce::AudioParameterBool& enabled,
                                 const juce::String& wetParameterId = {})
        {
            const auto apply = [&]
            {
                if (wetParameterId.isNotEmpty()) { turnUp(processor, wetParameterId, 1.0f); }
            };

            apply();
            const auto wet = worstDifferenceFromDry(processor, enabled, true);
            apply();   // prepareToPlay does not reset parameters, but say so anyway
            const auto dry = worstDifferenceFromDry(processor, enabled, false);
            record(name, wet, dry);
        };

        { PX3DelayAudioProcessor p;  measure("Delay",  p, p.debugEnabledParam()); }
        { PX3MoodAudioProcessor p;   measure("Mood",   p, p.enabled()); }
        { PX3ChorusAudioProcessor p; measure("Chorus", p, p.enabled()); }
        { PX3SpreadAudioProcessor p; measure("Spread", p, p.enabled()); }
        { PX3ReverbAudioProcessor p; measure("Reverb", p, p.enabled()); }
        { PX3DoomAudioProcessor p;   measure("Doom",   p, p.enabled(), "doomMix"); }
        { PX3LucyAudioProcessor p;   measure("Lucy",   p, p.enabled(), "lucyGlobal"); }

        // And the card's own switch has to reach that parameter. The audio path
        // above is only half the control: a button that changes nothing looks
        // exactly like an effect that ignores its flag.
        {
            juce::StringArray broken;
            juce::StringArray states;

            const auto switchReachesTheParameter = [&](const juce::String& name,
                                                       auto& processor,
                                                       juce::AudioParameterBool& enabled,
                                                       juce::AudioProcessorEditor* editorIn)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(editorIn);
                auto* card = dynamic_cast<px3::fx::FxCardEditor*>(editor.get());
                if (card == nullptr) { states.add(name + " (no card editor)"); return; }

                auto& button = card->debugCard().bypassButton();
                const auto before = enabled.get();

                button.setToggleState(! before, juce::sendNotificationSync);
                const auto after = enabled.get();

                states.add(name + " " + (before ? "on" : "off") + "->" + (after ? "on" : "off"));
                if (after == before) { broken.add(name); }
            };

            PX3ChorusAudioProcessor chorus;
            switchReachesTheParameter("Chorus", chorus, chorus.enabled(), chorus.createEditor());
            PX3SpreadAudioProcessor spread;
            switchReachesTheParameter("Spread", spread, spread.enabled(), spread.createEditor());
            PX3ReverbAudioProcessor reverb;
            switchReachesTheParameter("Reverb", reverb, reverb.enabled(), reverb.createEditor());
            PX3DoomAudioProcessor doom;
            switchReachesTheParameter("Doom", doom, doom.enabled(), doom.createEditor());
            PX3LucyAudioProcessor lucy;
            switchReachesTheParameter("Lucy", lucy, lucy.enabled(), lucy.createEditor());

            check("FxProducts_TheCardsBypassSwitchDrivesTheParameter",
                  broken.isEmpty(),
                  broken.isEmpty() ? "every switch moved its parameter: " + states.joinIntoString(", ")
                                   : "these switches changed nothing: " + broken.joinIntoString(", "));
        }

        check("FxProducts_AnEnabledEffectActuallyChangesTheSignal",
              inaudible.isEmpty(),
              inaudible.isEmpty()
                  ? "every effect alters its input at default settings: "
                        + measured.joinIntoString(", ")
                  : "these do nothing even when enabled, so their bypass proves "
                    "nothing: " + inaudible.joinIntoString(", "));

        check("FxProducts_BypassPassesTheSignalThroughUnchanged",
              leaking.isEmpty(),
              leaking.isEmpty()
                  ? "every effect returns its input when bypassed"
                  : "still altering the signal while bypassed: "
                        + leaking.joinIntoString(", "));
    }
}

} // namespace px3tests
