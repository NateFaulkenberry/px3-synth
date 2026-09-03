#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3ChorusAudioProcessor::PX3ChorusAudioProcessor()
{
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);

    addParameter(enabledParam = new juce::AudioParameterBool("chorusEnabled", "Chorus Enabled", true));
    // Amount defaults to full here, not zero. In the Synth this is one stage of
    // a chain that starts silent; a standalone chorus that does nothing when
    // inserted reads as broken. Every other default is the Synth's.
    addParameter(amountParam = new juce::AudioParameterFloat("chorusAmount", "Chorus Amount", unit, 1.0f));
    addParameter(rateParam = new juce::AudioParameterFloat("chorusRate", "Chorus Rate", unit, 0.35f));
    addParameter(depthParam = new juce::AudioParameterFloat("chorusDepth", "Chorus Depth", unit, 0.5f));
    addParameter(widthParam = new juce::AudioParameterFloat("chorusWidth", "Chorus Width", unit, 0.75f));
    addParameter(spreadParam = new juce::AudioParameterFloat("chorusSpread", "Chorus Spread", unit, 0.5f));
    addParameter(lowCutParam = new juce::AudioParameterFloat("chorusLowCut", "Chorus Low Cut", unit, 0.3f));
    addParameter(feedbackParam = new juce::AudioParameterFloat("chorusFeedback", "Chorus Feedback", unit, 0.0f));
    addParameter(characterParam = new juce::AudioParameterFloat("chorusCharacter", "Chorus Character", unit, 0.5f));
    addParameter(mixParam = new juce::AudioParameterFloat("chorusMix", "Chorus Mix", unit, 1.0f));
    // Tone is BIPOLAR: -1 warm, +1 clear. Copying it as 0..1 would have moved
    // its centre and changed what every stored value meant.
    addParameter(toneParam = new juce::AudioParameterFloat(
        "chorusTone", "Chorus Tone", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    addParameter(modeParam = new juce::AudioParameterChoice(
        "chorusMode", "Chorus Mode",
        juce::StringArray { "DIM 1", "DIM 2", "DIM 3", "DIM 4", "DIM 1+4",
                            "DIM 2+4", "DIM 3+4", "ENSEMBLE", "CE WARM" }, 1));
}

void PX3ChorusAudioProcessor::prepareFx(double sampleRate, int)
{
    chorus.prepare(sampleRate);
}

ChorusSettings PX3ChorusAudioProcessor::settingsForBlock() const
{
    ChorusSettings settings;
    settings.enabled = enabledParam->get();
    settings.modeIndex = modeParam->getIndex();
    settings.amount = amountParam->get();
    settings.rate = rateParam->get();
    settings.depth = depthParam->get();
    settings.width = widthParam->get();
    settings.spread = spreadParam->get();
    settings.tone = toneParam->get();
    settings.lowCut = lowCutParam->get();
    settings.feedback = feedbackParam->get();
    settings.character = characterParam->get();
    settings.mix = mixParam->get();
    return settings;
}

void PX3ChorusAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    chorus.updateForBlock(settingsForBlock());

    const auto numSamples = buffer.getNumSamples();
    const auto stereo = buffer.getNumChannels() > 1;
    auto* left = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = stereo ? right[i] : inL;

        float outL = inL, outR = inR;
        chorus.processSampleFrame(inL, inR, outL, outR);

        if (stereo) { left[i] = outL; right[i] = outR; }
        else        { left[i] = 0.5f * (outL + outR); }
    }
}

juce::AudioProcessorEditor* PX3ChorusAudioProcessor::createEditor()
{
    return new PX3ChorusAudioProcessorEditor(*this);
}
