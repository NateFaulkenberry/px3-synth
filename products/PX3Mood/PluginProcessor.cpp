#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3MoodAudioProcessor::PX3MoodAudioProcessor()
{
    // Ranges, defaults, names and IDs as the Synth declares them, so the same
    // control means the same thing in both products.
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);

    addParameter(enabledParam = new juce::AudioParameterBool("moodEnabled", "Mood Enabled", true));
    addParameter(freezeParam = new juce::AudioParameterBool("moodFreeze", "Mood Freeze", false));
    addParameter(mixParam = new juce::AudioParameterFloat("moodMix", "Mood Mix", unit, 0.35f));
    addParameter(clockParam = new juce::AudioParameterFloat("moodClock", "Mood Clock", unit, 1.0f));
    addParameter(wetTimeParam = new juce::AudioParameterFloat("moodWetTime", "Mood Wet Time", unit, 0.40f));
    addParameter(wetModifyParam = new juce::AudioParameterFloat("moodWetModify", "Mood Wet Modify", unit, 0.45f));
    addParameter(loopLengthParam = new juce::AudioParameterFloat("moodLoopLength", "Mood Loop Length", unit, 0.28f));
    addParameter(loopModifyParam = new juce::AudioParameterFloat("moodLoopModify", "Mood Loop Modify", unit, 0.50f));
    addParameter(feedbackParam = new juce::AudioParameterFloat("moodFeedback", "Mood Feedback", unit, 0.35f));
    addParameter(spreadParam = new juce::AudioParameterFloat("moodSpread", "Mood Spread", unit, 0.50f));
    addParameter(degradeParam = new juce::AudioParameterFloat("moodDegrade", "Mood Degrade", unit, 0.20f));
    addParameter(routingParam = new juce::AudioParameterChoice(
        "moodRouting", "Mood Routing", juce::StringArray { "DRY->WET", "LOOP->WET", "PARALLEL" }, 0));
    addParameter(wetModeParam = new juce::AudioParameterChoice(
        "moodWetMode", "Mood Wet Mode", juce::StringArray { "REVERB", "DELAY", "SLIP" }, 0));
    addParameter(loopModeParam = new juce::AudioParameterChoice(
        "moodLoopMode", "Mood Loop Mode", juce::StringArray { "ENV", "TAPE", "STRETCH" }, 0));
}

void PX3MoodAudioProcessor::prepareFx(double sampleRate, int)
{
    mood.prepare(sampleRate);
}

px3::MoodUserParameters PX3MoodAudioProcessor::userParametersForBlock() const
{
    px3::MoodUserParameters settings;
    settings.enabled = enabledParam->get();
    settings.freeze = freezeParam->get();
    settings.mix = mixParam->get();
    settings.clock = clockParam->get();
    settings.wetTime = wetTimeParam->get();
    settings.wetModify = wetModifyParam->get();
    settings.loopLength = loopLengthParam->get();
    settings.loopModify = loopModifyParam->get();
    settings.feedback = feedbackParam->get();
    settings.spread = spreadParam->get();
    settings.degrade = degradeParam->get();

    // A three-way choice reaching the engine as a choice. The index/2 dance
    // this replaces is gone from both products at once.
    settings.routing = static_cast<px3::MoodRouting>(routingParam->getIndex());
    settings.wetMode = static_cast<px3::MoodWetMode>(wetModeParam->getIndex());
    settings.loopMode = static_cast<px3::MoodLoopMode>(loopModeParam->getIndex());
    return settings;
}

void PX3MoodAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    mood.updateForBlock(userParametersForBlock());

    const auto numSamples = buffer.getNumSamples();
    const auto stereo = buffer.getNumChannels() > 1;
    auto* left = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = stereo ? right[i] : inL;

        float outL = inL, outR = inR;
        mood.processSampleFrame(inL, inR, outL, outR);

        if (stereo) { left[i] = outL; right[i] = outR; }
        else        { left[i] = 0.5f * (outL + outR); }
    }
}

juce::AudioProcessorEditor* PX3MoodAudioProcessor::createEditor()
{
    return new PX3MoodAudioProcessorEditor(*this);
}
