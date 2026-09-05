#include "TestSupport.h"

#include "../../products/PX3Delay/PluginProcessor.h"
#include "../../products/PX3Mood/PluginProcessor.h"
#include "../../products/PX3Chorus/PluginProcessor.h"
#include "../../products/PX3Spread/PluginProcessor.h"
#include "../../products/PX3Reverb/PluginProcessor.h"
#include "../../products/PX3Doom/PluginProcessor.h"
#include "../../products/PX3Lucy/PluginProcessor.h"
#include "../../shared/Infrastructure/Fx/FxCardEditor.h"
#include "../../shared/UI/Style/UIConfigManager.h"
#include "../../shared/UI/Style/KnobLookAndFeel.h"
#include "../../shared/UI/Components/ChipLabel.h"
#include "../../products/PX3Delay/PluginEditor.h"
#include "../../products/PX3Mood/PluginEditor.h"
#include "../../products/PX3Synth/UI/PluginEditor.h"
#include "../../products/PX3Synth/UI/FxPanel.h"
#include "../../products/PX3Synth/UI/EditorSections.h"
#include "../../products/PX3Synth/DSP/FxChain.h"

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

        // The choice reaches the engine AS A CHOICE now. This test used to
        // assert the index/2 conversion it replaces - the arrangement in which
        // two of the three settings were once wired to each other's labels.
        const std::array<std::pair<int, px3::MoodRouting>, 3> expected { {
            { 0, px3::MoodRouting::dryToWet },
            { 1, px3::MoodRouting::loopToWet },
            { 2, px3::MoodRouting::parallel },
        } };

        juce::StringArray seen;
        auto correct = true;
        for (const auto& [index, wanted] : expected)
        {
            mood.routing().setValueNotifyingHost(mood.routing().convertTo0to1((float) index));
            const auto routing = mood.debugUserParameters().routing;
            if (routing != wanted) { correct = false; }
            seen.add(mood.routing().choices[index] + " -> "
                     + juce::String(static_cast<int>(routing)));
        }

        check("FxProduct_MoodRoutingReachesTheEngineAsAChoice",
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
        const auto& gain = lucy.lossGain().getNormalisableRange();

        check("FxProduct_TheParametersThatAreNotUnitRangesKeptTheirRanges",
              std::abs(doomEq.start + 1.0f) < 1.0e-6f && std::abs(doomEq.end - 1.0f) < 1.0e-6f
                  && std::abs(gain.start + 36.0f) < 1.0e-4f
                  && std::abs(gain.end - 36.0f) < 1.0e-4f,
              "Doom EQ " + fmt(doomEq.start, 1) + ".." + fmt(doomEq.end, 1)
                  + ", Lucy loss gain " + fmt(gain.start, 1) + ".." + fmt(gain.end, 1) + " dB");
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
        source.lossGain().setValueNotifyingHost(source.lossGain().convertTo0to1(-12.0f));
        source.mode().setValueNotifyingHost(source.mode().convertTo0to1(1.0f));

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3LucyAudioProcessor reopened;
        prepared(reopened);
        reopened.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("FxProduct_LucyStateSurvivesASaveAndReloadIncludingItsDecibelGain",
              std::abs(reopened.loss().get() - 0.9f) < 1.0e-3f
                  && std::abs(reopened.lossGain().get() + 12.0f) < 0.1f
                  && reopened.mode().getIndex() == 1,
              "loss " + fmt(reopened.loss().get(), 3) + ", gain "
                  + fmt(reopened.lossGain().get(), 2) + " dB, mode "
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

    // ---- a standalone styles its own controls ------------------------------
    //
    // Without help. The comparison further down hands both cards the same
    // config on purpose, which is what let this hide: FxCardEditor applied the
    // config in its constructor, and a product declares its rows in its own
    // constructor body, which runs afterwards - so every per-control key was
    // read and applied to a card that had no controls yet. Chip colours,
    // caption colours, fonts and dropdown colours all silently did nothing,
    // while the card's border and artwork looked right because those are read
    // while painting.
    {
        juce::StringArray unstyled;

        // The expected colour is READ FROM THE CONFIG, not written here.
        //
        // Spelling it out meant this failed the first time the scheme was
        // retuned, which is a test asserting a design decision rather than the
        // behaviour it was written for: that a standalone card applies whatever
        // its config says. The colour is the design's to change.
        const auto configFileForStyle = UIConfigManager::findShippingConfigFile();
        std::shared_ptr<const UIConfig> styleConfig;
        if (configFileForStyle.existsAsFile())
        {
            juce::String styleError;
            styleConfig = UIConfig::fromJsonText(configFileForStyle.loadFileAsString(), styleError);
        }

        // THE ARTWORK FIT AND ALIGNMENT REACH THE CARD.
        //
        // Both are named in config as words - "stretch", "topLeft" - and both
        // parsers fall back silently on a name they do not know, which is the
        // right behaviour at runtime and invisible in a screenshot: a card
        // whose fit is misspelt draws cover-centred, exactly as it did before
        // anyone set a fit, and nothing says why.
        //
        // Compares what the card RESOLVED against what the config SAYS, so it
        // pins the wiring rather than the design: retuning any card's fit is
        // one edit to the JSON and this still passes.
        {
            juce::StringArray wrong;
            juce::StringArray resolved;

            for (const auto* styleKey : { "vibe", "delay", "reverb", "mood",
                                          "doom", "lucy", "chorus", "stereoSpread" })
            {
                if (styleConfig == nullptr) { break; }

                const juce::String base = juce::String("cards.") + styleKey;
                const auto declaredFit = styleConfig->getString(base + ".artwork.fit", "cover");
                const auto declaredAlign = styleConfig->getString(base + ".artwork.align", "centre");

                const auto style = px3::ui::CardStyle::fromConfig(styleConfig.get(),
                                                                  "cards.defaults",
                                                                  base);
                const juce::String actualFit = px3::ui::describeArtworkFit(style.artwork.fit);
                const juce::String actualAlign = px3::ui::describeArtworkAlign(style.artwork.align);

                resolved.add(juce::String(styleKey) + " " + actualFit + "/" + actualAlign);

                if (! actualFit.equalsIgnoreCase(declaredFit))
                {
                    wrong.add(juce::String(styleKey) + " fit: config says '" + declaredFit
                              + "', card resolved '" + actualFit + "'");
                }
                if (! actualAlign.equalsIgnoreCase(declaredAlign))
                {
                    wrong.add(juce::String(styleKey) + " align: config says '" + declaredAlign
                              + "', card resolved '" + actualAlign + "'");
                }
            }

            check("FxCards_ArtworkFitAndAlignmentComeFromConfig",
                  wrong.isEmpty(),
                  wrong.isEmpty() ? "every card resolved what it declares: " + resolved.joinIntoString(", ")
                                  : wrong.joinIntoString("; "));
        }

        const auto checkStyled = [&](const juce::String& name,
                                     const juce::String& styleKey,
                                     juce::AudioProcessor& processor,
                                     const juce::String& knobId)
        {
            if (styleConfig == nullptr) { unstyled.add("no config to compare against"); return; }

            const auto key = "cards." + styleKey + ".controls.labelBackground";
            if (styleConfig->getValue(key).isVoid())
            {
                unstyled.add(name + " declares no " + key + " to check");
                return;
            }

            const auto expected = styleConfig->getColour(key, juce::Colours::white);

            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            auto* cardEditor = dynamic_cast<px3::fx::FxCardEditor*>(editor.get());
            if (cardEditor == nullptr) { unstyled.add(name + " (no card editor)"); return; }

            auto* label = dynamic_cast<px3::ui::ChipLabel*>(cardEditor->debugCard().knobLabel(knobId));
            if (label == nullptr) { unstyled.add(name + " (no caption for " + knobId + ")"); return; }

            const auto actual = label->getChipStyle().background;
            if (actual != expected)
            {
                unstyled.add(name + " caption background is " + actual.toDisplayString(true)
                             + ", config says " + expected.toDisplayString(true));
            }
        };

        PX3DoomAudioProcessor doomStyled;
        checkStyled("Doom", "doom", doomStyled, "clock");
        PX3LucyAudioProcessor lucyStyled;
        checkStyled("Lucy", "lucy", lucyStyled, "loss");

        check("FxProducts_AStandaloneStylesItsOwnControlsFromConfig",
              unstyled.isEmpty(),
              unstyled.isEmpty()
                  ? "a standalone card reads its own per-control styling, with nothing "
                    "applied for it"
                  : unstyled.joinIntoString("; "));
    }

    // ---- the reverb's nine survive a save and reload -----------------------
    //
    // They were registered from the start and had no UI, so nothing had ever
    // moved them and then asked for them back. Now that the card exposes them,
    // a value set in a session has to still be there when it reopens.
    //
    // Both products, because they persist by different routes: the Synth writes
    // a ValueTree keyed by parameter ID, the effect uses the base class. Either
    // could drop a parameter without the other noticing.
    {
        const std::array<const char*, 9> ids { {
            "reverbSize", "reverbDecay", "reverbDamping", "reverbPreDelay",
            "reverbModDepth", "reverbModRate", "reverbWidth",
            "reverbCloudFeedback", "reverbCloudDiffusion" } };

        const auto setAll = [&](juce::AudioProcessor& processor, float value)
        {
            int moved = 0;
            for (auto* parameter : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                {
                    for (const auto* id : ids)
                    {
                        if (ranged->getParameterID() == id)
                        {
                            ranged->setValueNotifyingHost(value);
                            ++moved;
                        }
                    }
                }
            }
            return moved;
        };

        const auto readAll = [&](juce::AudioProcessor& processor)
        {
            std::vector<std::pair<juce::String, float>> values;
            for (auto* parameter : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                {
                    for (const auto* id : ids)
                    {
                        if (ranged->getParameterID() == id)
                        {
                            values.emplace_back(ranged->getParameterID(), ranged->getValue());
                        }
                    }
                }
            }
            return values;
        };

        juce::StringArray lost;

        const auto roundTrip = [&](const juce::String& name, auto& source, auto& target)
        {
            const auto moved = setAll(source, 0.77f);
            if (moved != static_cast<int>(ids.size()))
            {
                lost.add(name + " has only " + juce::String(moved) + " of "
                         + juce::String(static_cast<int>(ids.size())) + " parameters");
                return;
            }

            juce::MemoryBlock state;
            source.getStateInformation(state);
            target.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

            for (const auto& [id, value] : readAll(target))
            {
                if (std::abs(value - 0.77f) > 0.01f)
                {
                    lost.add(name + "." + id + " reloaded as " + juce::String(value, 3));
                }
            }
        };

        {
            PX3SynthAudioProcessor a, b;
            roundTrip("Synth", a, b);
        }
        {
            PX3ReverbAudioProcessor a, b;
            roundTrip("Reverb", a, b);
        }

        check("FxProducts_TheReverbsNineParametersSurviveASaveAndReload",
              lost.isEmpty(),
              lost.isEmpty()
                  ? "all nine reload at the value they were saved at, in both the "
                    "Synth and the standalone"
                  : lost.joinIntoString(", "));
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
            switchReachesTheParameter("Chorus", chorus.enabled(), chorus.createEditor());
            PX3SpreadAudioProcessor spread;
            switchReachesTheParameter("Spread", spread.enabled(), spread.createEditor());
            PX3ReverbAudioProcessor reverb;
            switchReachesTheParameter("Reverb", reverb.enabled(), reverb.createEditor());
            PX3DoomAudioProcessor doom;
            switchReachesTheParameter("Doom", doom.enabled(), doom.createEditor());
            PX3LucyAudioProcessor lucy;
            switchReachesTheParameter("Lucy", lucy.enabled(), lucy.createEditor());

            check("FxProducts_TheCardsBypassSwitchDrivesTheParameter",
                  broken.isEmpty(),
                  broken.isEmpty() ? "every switch moved its parameter: " + states.joinIntoString(", ")
                                   : "these switches changed nothing: " + broken.joinIntoString(", "));
        }

        // EVERY KNOB ON A CARD DRIVES A PARAMETER.
        //
        // The cards attach their controls by id, one attach call per control,
        // and a knob whose call is missing or misspelt still lays out, still
        // draws, still turns - and does nothing. Nothing else catches that:
        // the parity test compares geometry and palette, which an unattached
        // knob matches perfectly, and the DSP tests drive the parameters
        // directly and never touch a control.
        {
            juce::StringArray inert;
            juce::StringArray counted;

            const auto everyKnobMovesSomething = [&](const juce::String& name,
                                                     juce::AudioProcessor& processor,
                                                     juce::AudioProcessorEditor* editorIn)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(editorIn);
                auto* cardEditor = dynamic_cast<px3::fx::FxCardEditor*>(editor.get());
                if (cardEditor == nullptr) { counted.add(name + " (no card editor)"); return; }

                const auto& parameters = processor.getParameters();
                const auto snapshot = [&]
                {
                    std::vector<float> values;
                    for (auto* parameter : parameters) { values.push_back(parameter->getValue()); }
                    return values;
                };

                const auto knobs = cardEditor->debugCard().allKnobs();
                int moved = 0;
                for (auto* knob : knobs)
                {
                    const auto before = snapshot();

                    // Somewhere else in the range, whichever end is further,
                    // so a knob already sitting at a limit is still moved.
                    const auto position = knob->getValue();
                    const auto low = knob->getMinimum();
                    const auto high = knob->getMaximum();
                    const auto target = (position - low) > (high - position) ? low : high;
                    knob->setValue(target, juce::sendNotificationSync);

                    const auto after = snapshot();
                    if (after != before) { ++moved; }
                    else { inert.add(name + " " + knob->getName()); }

                    knob->setValue(position, juce::sendNotificationSync);
                }

                counted.add(name + " " + juce::String(moved) + "/" + juce::String((int) knobs.size()));
            };

            PX3ChorusAudioProcessor chorusKnobs;
            everyKnobMovesSomething("Chorus", chorusKnobs, chorusKnobs.createEditor());
            PX3SpreadAudioProcessor spreadKnobs;
            everyKnobMovesSomething("Spread", spreadKnobs, spreadKnobs.createEditor());
            PX3ReverbAudioProcessor reverbKnobs;
            everyKnobMovesSomething("Reverb", reverbKnobs, reverbKnobs.createEditor());
            PX3DoomAudioProcessor doomKnobs;
            everyKnobMovesSomething("Doom", doomKnobs, doomKnobs.createEditor());
            PX3LucyAudioProcessor lucyKnobs;
            everyKnobMovesSomething("Lucy", lucyKnobs, lucyKnobs.createEditor());

            check("FxProducts_EveryKnobOnACardIsAttachedToAParameter",
                  inert.isEmpty(),
                  inert.isEmpty() ? "every knob moved a parameter: " + counted.joinIntoString(", ")
                                  : "these knobs are attached to nothing: " + inert.joinIntoString(", "));
        }

        // THE PAIRED KNOBS SHOW EXACTLY ONE FUNCTION AT A TIME.
        //
        // DOOM and LUCY give six knobs a second function each, as the pedals
        // they follow print them. Both halves are real parameters and stay
        // attached whichever is displayed - ALT only chooses which one you can
        // see - so the thing that can break silently is the DISPLAY: two
        // captions at once, or none, or a knob whose caption names the other
        // function.
        {
            juce::StringArray wrong;
            juce::StringArray counted;

            const auto captionsFollowTheAltSwitch = [&](const juce::String& name,
                                                        juce::AudioProcessorEditor* editorIn)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(editorIn);
                auto* cardEditor = dynamic_cast<px3::fx::FxCardEditor*>(editor.get());
                if (cardEditor == nullptr) { counted.add(name + " (no card editor)"); return; }

                auto& card = cardEditor->debugCard();
                if (! card.hasAlternates()) { wrong.add(name + " has no paired knobs"); return; }

                // Both states, and the same expectation in each: of every pair
                // exactly one knob and exactly one caption is on screen.
                for (const auto alt : { false, true })
                {
                    card.setAltMode(alt);

                    auto pairs = 0;
                    for (const auto& id : card.debugPairedKnobIds())
                    {
                        ++pairs;
                        auto* primary = card.knob(id.first);
                        auto* alternate = card.knob(id.second);
                        auto* primaryLabel = card.knobLabel(id.first);
                        auto* alternateLabel = card.knobLabel(id.second);

                        if (primary == nullptr || alternate == nullptr
                                || primaryLabel == nullptr || alternateLabel == nullptr)
                        {
                            wrong.add(name + " " + id.first + ": a half of the pair is missing");
                            continue;
                        }

                        if (primary->isVisible() == alternate->isVisible())
                        {
                            wrong.add(name + " " + id.first + ": both knobs or neither visible");
                        }
                        if (primaryLabel->isVisible() == alternateLabel->isVisible())
                        {
                            wrong.add(name + " " + id.first + ": both captions or neither visible");
                        }
                        // The visible caption must be the one naming the
                        // visible knob, not the other way round.
                        if (alternate->isVisible() != alt || alternateLabel->isVisible() != alt)
                        {
                            wrong.add(name + " " + id.first + ": ALT showed the wrong function");
                        }
                        // And they occupy the same place, so switching does
                        // not move anything on the card.
                        if (primary->getBounds() != alternate->getBounds()
                                || primaryLabel->getBounds() != alternateLabel->getBounds())
                        {
                            wrong.add(name + " " + id.first + ": the pair is not in one place");
                        }
                    }
                    if (! alt) { counted.add(name + " " + juce::String(pairs) + " pairs"); }
                }

                card.setAltMode(false);
            };

            PX3DoomAudioProcessor doomAlt;
            captionsFollowTheAltSwitch("Doom", doomAlt.createEditor());
            PX3LucyAudioProcessor lucyAlt;
            captionsFollowTheAltSwitch("Lucy", lucyAlt.createEditor());

            check("FxCards_APairedKnobShowsOneFunctionAtATime",
                  wrong.isEmpty(),
                  wrong.isEmpty() ? "one knob and one caption per pair, in one place: "
                                        + counted.joinIntoString(", ")
                                  : wrong.joinIntoString("; "));
        }

        // A BYPASSED CARD GREYS OUT COMPLETELY.
        //
        // The card already dimmed its artwork and desaturated its knobs on
        // bypass, but its captions kept the card's colour scheme - so a
        // switched-off card still had a row of coloured chips on it. Every
        // caption has to follow the knob it names.
        {
            juce::StringArray coloured;
            juce::StringArray counted;

            const auto captionsFollowTheBypass = [&](const juce::String& name,
                                                     juce::AudioProcessorEditor* editorIn)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(editorIn);
                auto* cardEditor = dynamic_cast<px3::fx::FxCardEditor*>(editor.get());
                if (cardEditor == nullptr) { counted.add(name + " (no card editor)"); return; }

                auto& card = cardEditor->debugCard();
                const auto labels = card.allKnobLabels();
                if (labels.empty()) { coloured.add(name + " (no captions at all)"); return; }

                card.setActive(false);
                int grey = 0;
                for (auto* label : labels)
                {
                    if (auto* chip = dynamic_cast<px3::ui::ChipLabel*>(label))
                    {
                        if (chip->isGreyedOut()) { ++grey; }
                    }
                }

                counted.add(name + " " + juce::String(grey) + "/" + juce::String((int) labels.size()));
                if (grey != (int) labels.size()) { coloured.add(name); }

                // And back again: bypass is a toggle, not a one-way trip.
                card.setActive(true);
                for (auto* label : labels)
                {
                    if (auto* chip = dynamic_cast<px3::ui::ChipLabel*>(label))
                    {
                        if (chip->isGreyedOut()) { coloured.add(name + " (stayed grey)"); break; }
                    }
                }
            };

            PX3ChorusAudioProcessor chorus;
            captionsFollowTheBypass("Chorus", chorus.createEditor());
            PX3SpreadAudioProcessor spread;
            captionsFollowTheBypass("Spread", spread.createEditor());
            PX3ReverbAudioProcessor reverb;
            captionsFollowTheBypass("Reverb", reverb.createEditor());
            PX3DoomAudioProcessor doom;
            captionsFollowTheBypass("Doom", doom.createEditor());
            PX3LucyAudioProcessor lucy;
            captionsFollowTheBypass("Lucy", lucy.createEditor());

            check("FxProducts_ABypassedCardGreysItsCaptionsToo",
                  coloured.isEmpty(),
                  coloured.isEmpty()
                      ? "captions greyed and restored: " + counted.joinIntoString(", ")
                      : "captions did not follow the bypass: " + coloured.joinIntoString(", "));
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
    // ---- a standalone card is the Synth's card ------------------------------
    //
    // The claim FxCardEditor's own comment makes: "a standalone effect looks
    // like its card inside the Synth because it is that card". Nothing checked
    // it, and it was false for a while - the effects shipped without the
    // UIConfig.json their styling comes from, so an installed one fell back to
    // code defaults while the Synth's copy did not.
    //
    // Compared as a layout signature rather than as two snapshots, because a
    // pixel difference says only that they differ. This names the control that
    // moved and the colour that changed.
    //
    // Only these four: Delay, Reverb and Mood are not card-shaped inside the
    // Synth at all - they have components of their own - so there is no card
    // to compare them against.
    {
        PX3SynthAudioProcessor synth;
        synth.setPlayConfigDetails(0, 2, kRate, kBlock);
        synth.prepareToPlay(kRate, kBlock);

        std::unique_ptr<juce::AudioProcessorEditor> synthBase(synth.createEditor());
        auto* synthEditor = dynamic_cast<PX3SynthAudioProcessorEditor*>(synthBase.get());

        if (synthEditor != nullptr)
        {
            synthEditor->setSize(1400, 900);
            synthEditor->debugSelectSection(px3::ui::kSectionFx);
            // The Synth loads its config on a timer tick. Without this the
            // comparison reads the Synth's cards before they have been styled
            // and reports the STANDALONE as the odd one out, which is exactly
            // backwards.
            synthEditor->debugLoadUiConfig();

            auto* panel = synthEditor->debugFxPanel();

            // The size both are measured at. Any size would do so long as it is
            // the same one; a grid cell is the size they actually meet at.
            const juce::Rectangle<int> cell { 0, 0, 318, 500 };

            // ONE config, given to both.
            //
            // The two sides find their config by different routes - the Synth's
            // resolver gates a source-tree probe behind a build flag, the
            // standalone's walks up from the executable - and that difference
            // is not what is being tested here. Handing both the same file
            // compares what they BUILD from it, which is the claim. Whether
            // each can find it when installed is a separate question, and the
            // answer to it is that the effects now ship a copy inside their
            // own bundles.
            const auto configFile = UIConfigManager::findShippingConfigFile();
            juce::String configError;
            std::shared_ptr<const UIConfig> sharedConfig;
            if (configFile.existsAsFile())
            {
                sharedConfig = UIConfig::fromJsonText(configFile.loadFileAsString(), configError);
            }

            const auto signatureOf = [&cell, &sharedConfig](px3::ui::FxCardComponent& card)
            {
                if (sharedConfig != nullptr) { card.setUIConfig(sharedConfig); }
                card.setBounds(cell);
                card.resized();
                return card.debugLayoutSignature();
            };

            juce::StringArray differing;
            juce::StringArray firstDifference;

            const auto compare = [&](const juce::String& name, int stage,
                                     juce::AudioProcessor& processor)
            {
                auto* synthCard = panel != nullptr ? panel->cardForSection(stage) : nullptr;
                if (synthCard == nullptr) { differing.add(name + " (no card in the Synth)"); return; }

                std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
                auto* cardEditor = dynamic_cast<px3::fx::FxCardEditor*>(editor.get());
                if (cardEditor == nullptr) { differing.add(name + " (not a card editor)"); return; }

                const auto expected = signatureOf(*synthCard);
                const auto actual = signatureOf(cardEditor->debugCard());

                if (expected == actual) { return; }

                differing.add(name);

                // The first line that differs, so a failure reads as one fact
                // rather than as two screens of coordinates.
                juce::StringArray a, b;
                a.addLines(expected);
                b.addLines(actual);
                for (int i = 0; i < juce::jmax(a.size(), b.size()); ++i)
                {
                    const auto left = i < a.size() ? a[i] : juce::String("(missing)");
                    const auto right = i < b.size() ? b[i] : juce::String("(missing)");
                    if (left != right)
                    {
                        firstDifference.add(name + ": synth [" + left + "] standalone [" + right + "]");
                        break;
                    }
                }
            };

            PX3DoomAudioProcessor doom;
            compare("Doom", px3::fxStageDoom, doom);
            PX3LucyAudioProcessor lucy;
            compare("Lucy", px3::fxStageLucy, lucy);
            PX3ChorusAudioProcessor chorus;
            compare("Chorus", px3::fxStageChorus, chorus);
            PX3SpreadAudioProcessor spread;
            compare("Spread", px3::fxStageStereoSpread, spread);
            // Reverb joined the cards when the Synth stopped showing it as a
            // mode and an amount over nine parameters with no control at all.
            PX3ReverbAudioProcessor reverb;
            compare("Reverb", px3::fxStageReverb, reverb);

            // A missing config would make every card fall back to the same
            // defaults and the comparison would pass by having nothing to
            // compare. Said out loud rather than passing quietly.
            check("FxProducts_TheComparisonHasARealConfigToWorkFrom",
                  sharedConfig != nullptr,
                  sharedConfig != nullptr
                      ? "styling both cards from " + configFile.getFileName()
                      : "no UIConfig.json found - the comparison below would "
                        "compare two sets of code defaults: " + configError);

            // ---- and the two that are not cards ------------------------
            //
            // Delay and Mood hand their controls to the SAME shared component
            // the Synth's FX page uses, so parity there is a question of the
            // size it is given rather than of what it builds. The Synth puts
            // every stage in one grid - the bespoke components beside the cards
            // - so they all have the same shape there, and these windows should
            // open at it.
            //
            // Compared by walking the children, because these are not cards and
            // have no layout signature of their own.
            // Position AND content.
            //
            // Bounds alone passed a window with every label blank and every
            // dropdown empty: the controls were all in the right places and
            // none of them said anything. What a control CONTAINS is as much
            // part of "renders the same" as where it sits.
            const auto childBounds = [](juce::Component& component)
            {
                juce::StringArray lines;
                for (auto* child : component.getChildren())
                {
                    if (child == nullptr) { continue; }
                    const auto b = child->getBounds();
                    juce::String line = juce::String(b.getX()) + "," + juce::String(b.getY()) + ","
                                      + juce::String(b.getWidth()) + "," + juce::String(b.getHeight());

                    if (auto* label = dynamic_cast<juce::Label*>(child))
                    {
                        line += " text=\"" + label->getText() + "\"";
                    }
                    else if (auto* box = dynamic_cast<juce::ComboBox*>(child))
                    {
                        juce::StringArray items;
                        for (int i = 0; i < box->getNumItems(); ++i) { items.add(box->getItemText(i)); }
                        line += " items=[" + items.joinIntoString("/") + "]";
                    }
                    else if (auto* button = dynamic_cast<juce::Button*>(child))
                    {
                        // The class too: a stock ToggleButton draws a system
                        // checkbox where the Synth draws a chip, and the two
                        // occupy the same rectangle.
                        line += juce::String(" button=") + typeid(*button).name()
                              + " text=\"" + button->getButtonText() + "\"";
                    }

                    lines.add(line);
                }
                lines.sort(false);
                return lines.joinIntoString(" | ");
            };

            juce::StringArray panelDiffs;

            // Typed on the standalone's panel, so the Synth's component is
            // required to be the SAME CLASS - which is the claim being made -
            // and so both can be handed the same config. They must be: these
            // components size themselves from it, and comparing one that has it
            // against one that does not produced a page of differences that
            // said nothing about either.
            const auto comparePanel = [&](const juce::String& name, int stage,
                                          auto* standalonePanel)
            {
                using PanelType = std::remove_pointer_t<decltype(standalonePanel)>;

                auto* synthPanel = panel != nullptr
                                       ? dynamic_cast<PanelType*>(panel->debugComponentForSection(stage))
                                       : nullptr;
                if (synthPanel == nullptr || standalonePanel == nullptr)
                {
                    panelDiffs.add(name + " (the Synth does not show this as the same component)");
                    return;
                }

                if (sharedConfig != nullptr)
                {
                    synthPanel->setUIConfig(sharedConfig);
                    standalonePanel->setUIConfig(sharedConfig);
                }

                synthPanel->setBounds(cell);
                synthPanel->resized();
                standalonePanel->setBounds(cell);
                standalonePanel->resized();

                const auto expected = childBounds(*synthPanel);
                const auto actual = childBounds(*standalonePanel);

                if (expected != actual)
                {
                    panelDiffs.add(name + ": synth [" + expected + "] standalone [" + actual + "]");
                }
            };

            PX3DelayAudioProcessor delayProcessor;
            std::unique_ptr<juce::AudioProcessorEditor> delayEditor(delayProcessor.createEditor());
            if (auto* d = dynamic_cast<PX3DelayAudioProcessorEditor*>(delayEditor.get()))
            {
                comparePanel("Delay", px3::fxStageDelay, &d->debugPanel());
            }

            PX3MoodAudioProcessor moodProcessor;
            std::unique_ptr<juce::AudioProcessorEditor> moodEditor(moodProcessor.createEditor());
            if (auto* m = dynamic_cast<PX3MoodAudioProcessorEditor*>(moodEditor.get()))
            {
                comparePanel("Mood", px3::fxStageMood, &m->debugPanel());
            }

            // ---- every stage is actually in the grid -----------------------
            //
            // Built and never added is a silent failure: componentForSection
            // answers, the signal-flow strip lists the stage, and the card is
            // simply not on screen. Removing Reverb's addAndMakeVisible took
            // Vibe, Delay and Mood's with it and the whole suite stayed green.
            {
                juce::StringArray missing;
                const std::array<std::pair<const char*, int>, 8> stages { {
                    { "Vibe", px3::fxStageVibe }, { "Delay", px3::fxStageDelay },
                    { "Reverb", px3::fxStageReverb }, { "Mood", px3::fxStageMood },
                    { "Doom", px3::fxStageDoom }, { "Lucy", px3::fxStageLucy },
                    { "Chorus", px3::fxStageChorus }, { "Spread", px3::fxStageStereoSpread } } };

                for (const auto& [name, stage] : stages)
                {
                    auto* component = panel != nullptr ? panel->debugComponentForSection(stage) : nullptr;
                    if (component == nullptr) { missing.add(juce::String(name) + " (no component)"); }
                    else if (component->getParentComponent() == nullptr)
                    {
                        missing.add(juce::String(name) + " (built, never added)");
                    }
                    else if (! component->isVisible())
                    {
                        missing.add(juce::String(name) + " (added, not visible)");
                    }
                }

                check("FxPanel_EveryStageIsOnScreen",
                      missing.isEmpty(),
                      missing.isEmpty() ? "all eight stages are in the grid and visible"
                                        : "missing from the FX panel: " + missing.joinIntoString(", "));
            }

            // ---- every knob on a card wears the PX3 look --------------------
            //
            // A knob with no look-and-feel draws as a stock JUCE rotary, which
            // is a different control in the same place.
            {
                juce::StringArray unstyled;

                const auto checkLooks = [&](const juce::String& name, int stage)
                {
                    auto* c = panel != nullptr ? panel->cardForSection(stage) : nullptr;
                    if (c == nullptr) { return; }

                    for (auto* knob : c->allKnobs())
                    {
                        if (knob == nullptr) { continue; }
                        if (dynamic_cast<px3::ui::KnobLookAndFeel*>(&knob->getLookAndFeel()) == nullptr)
                        {
                            unstyled.add(name + " knob at " + knob->getBounds().toString());
                        }
                    }
                };

                checkLooks("Reverb", px3::fxStageReverb);
                checkLooks("Doom", px3::fxStageDoom);
                checkLooks("Chorus", px3::fxStageChorus);

                check("FxCards_EveryKnobUsesThePx3Look",
                      unstyled.isEmpty(),
                      unstyled.isEmpty() ? "every knob on every card carries the PX3 look"
                                         : "stock JUCE rotaries: " + unstyled.joinIntoString(", "));
            }

            // ---- and every knob on a card drives a parameter ---------------
            //
            // The Synth attaches its card controls by id, one call per
            // control, in buildReverbCard and its siblings. A knob whose call
            // is missing or misspelt still lays out, still draws, still turns -
            // and does nothing. Neither the look test above nor the standalone
            // parity comparison catches it: an unattached knob has the right
            // look in the right place.
            {
                juce::StringArray inert;
                juce::StringArray counted;

                const auto& parameters = synth.getParameters();
                const auto snapshot = [&]
                {
                    std::vector<float> values;
                    for (auto* parameter : parameters) { values.push_back(parameter->getValue()); }
                    return values;
                };

                const auto checkAttachments = [&](const juce::String& name, int stage)
                {
                    auto* c = panel != nullptr ? panel->cardForSection(stage) : nullptr;
                    if (c == nullptr) { counted.add(name + " (no card)"); return; }

                    int moved = 0;
                    const auto knobs = c->allKnobs();
                    for (auto* knob : knobs)
                    {
                        if (knob == nullptr) { continue; }
                        const auto before = snapshot();

                        // Whichever end of the range is further away, so a knob
                        // already sitting at a limit still moves.
                        const auto position = knob->getValue();
                        const auto low = knob->getMinimum();
                        const auto high = knob->getMaximum();
                        const auto target = (position - low) > (high - position) ? low : high;
                        knob->setValue(target, juce::sendNotificationSync);

                        if (snapshot() != before) { ++moved; }
                        else { inert.add(name + " knob at " + knob->getBounds().toString()); }

                        knob->setValue(position, juce::sendNotificationSync);
                    }
                    counted.add(name + " " + juce::String(moved) + "/" + juce::String((int) knobs.size()));
                };

                checkAttachments("Reverb", px3::fxStageReverb);
                checkAttachments("Doom", px3::fxStageDoom);
                checkAttachments("Lucy", px3::fxStageLucy);
                checkAttachments("Chorus", px3::fxStageChorus);
                checkAttachments("Spread", px3::fxStageStereoSpread);

                check("FxCards_EveryKnobOnTheSynthsCardsIsAttached",
                      inert.isEmpty(),
                      inert.isEmpty() ? "every knob moved a parameter: " + counted.joinIntoString(", ")
                                      : "these knobs are attached to nothing: " + inert.joinIntoString(", "));
            }

            check("FxProducts_TheNonCardEffectsLayOutLikeTheSynthsPanels",
                  panelDiffs.isEmpty(),
                  panelDiffs.isEmpty()
                      ? "Delay and Mood place their controls identically to the "
                        "Synth's panels at the same size"
                      : panelDiffs.joinIntoString("  /  "));

            // ---- every standalone opens at one grid cell -------------------
            juce::StringArray sizes;
            juce::StringArray wrongSize;
            const auto want = px3::fx::standaloneFxWindowSize(sharedConfig.get());

            const auto checkSize = [&](const juce::String& name, juce::AudioProcessorEditor* editor)
            {
                if (editor == nullptr) { return; }
                const auto got = editor->getLocalBounds();
                sizes.add(name + " " + juce::String(got.getWidth()) + "x"
                          + juce::String(got.getHeight()));
                if (got.getWidth() != want.getWidth() || got.getHeight() != want.getHeight())
                {
                    wrongSize.add(name);
                }
            };

            checkSize("Delay", delayEditor.get());
            checkSize("Mood", moodEditor.get());

            check("FxProducts_EveryStandaloneOpensAtOneGridCell",
                  wrongSize.isEmpty(),
                  juce::String("wanted ") + juce::String(want.getWidth()) + "x"
                      + juce::String(want.getHeight()) + "; got " + sizes.joinIntoString(", ")
                      + (wrongSize.isEmpty() ? "" : "  WRONG: " + wrongSize.joinIntoString(", ")));

            check("FxProducts_AStandaloneCardMatchesTheSynthsCardExactly",
                  differing.isEmpty(),
                  differing.isEmpty()
                      ? "Doom, Lucy, Chorus, Spread and Reverb lay out and colour identically "
                        "in both, at the same size"
                      : "differ: " + differing.joinIntoString(", ") + ". "
                            + firstDifference.joinIntoString("  /  "));
        }
    }
}

} // namespace px3tests
