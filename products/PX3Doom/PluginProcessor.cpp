#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3DoomAudioProcessor::PX3DoomAudioProcessor()
{
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);
    // EQ is a TILT: left removes highs, right removes lows. Bipolar in the
    // Synth, and declaring it 0..1 here would move its centre.
    const auto bipolar = juce::NormalisableRange<float>(-1.0f, 1.0f);

    addParameter(enabledParam = new juce::AudioParameterBool("doomEnabled", "Doom Enabled", true));
    addParameter(freezeParam = new juce::AudioParameterBool("doomFreeze", "Doom Freeze", false));
    addParameter(loopActiveParam = new juce::AudioParameterBool("doomLoopActive", "Doom Looper Active", false));
    addParameter(wetActiveParam = new juce::AudioParameterBool("doomWetActive", "Doom Wet Active", true));
    addParameter(loopHalfParam = new juce::AudioParameterBool("doomLoopHalf", "Doom Loop Half", false));
    addParameter(clockSmoothParam = new juce::AudioParameterBool("doomClockSmooth", "Doom Clock Smooth", false));
    addParameter(mixParam = new juce::AudioParameterFloat("doomMix", "Doom Mix", unit, 0.0f));
    addParameter(clockParam = new juce::AudioParameterFloat("doomClock", "Doom Clock", unit, 1.0f));
    addParameter(loopLengthParam = new juce::AudioParameterFloat("doomLoopLength", "Doom Loop Length", unit, 0.45f));
    addParameter(loopModifyParam = new juce::AudioParameterFloat("doomLoopModify", "Doom Loop Modify", unit, 0.50f));
    addParameter(overdubParam = new juce::AudioParameterFloat("doomOverdub", "Doom Overdub", unit, 0.0f));
    addParameter(fadeParam = new juce::AudioParameterFloat("doomFade", "Doom Fade", unit, 1.0f));
    addParameter(wetTimeParam = new juce::AudioParameterFloat("doomWetTime", "Doom Wet Time", unit, 0.45f));
    addParameter(wetModifyParam = new juce::AudioParameterFloat("doomWetModify", "Doom Wet Modify", unit, 0.40f));
    addParameter(crossParam = new juce::AudioParameterFloat("doomCross", "Doom Cross", unit, 0.0f));
    addParameter(glueParam = new juce::AudioParameterFloat("doomGlue", "Doom Glue", unit, 0.15f));
    addParameter(eqParam = new juce::AudioParameterFloat("doomEq", "Doom EQ", bipolar, 0.0f));
    addParameter(balanceParam = new juce::AudioParameterFloat("doomBalance", "Doom Balance", unit, 0.5f));
    addParameter(blendParam = new juce::AudioParameterFloat("doomBlend", "Doom Blend", unit, 0.0f));
    addParameter(spreadParam = new juce::AudioParameterFloat("doomSpread", "Doom Spread", unit, 0.5f));
    addParameter(routingParam = new juce::AudioParameterChoice(
        "doomRouting", "Doom Routing", juce::StringArray { "INPUT", "INPUT+LOOP", "LOOP" }, 0));
    addParameter(loopModeParam = new juce::AudioParameterChoice(
        "doomLoopMode", "Doom Loop Mode", juce::StringArray { "BURST", "RADIO", "MASK" }, 1));
    addParameter(wetModeParam = new juce::AudioParameterChoice(
        "doomWetMode", "Doom Wet Mode", juce::StringArray { "SOUP", "RELAY", "FLIP" }, 0));
    addParameter(crossSourceParam = new juce::AudioParameterChoice(
        "doomCrossSource", "Doom Cross Source", juce::StringArray { "INPUT", "CHANNEL" }, 0));

    // Mix defaults to the Synth's 0 for every other product's Amount reason in
    // reverse: Doom's mix at 0 is the DRY signal, and a destroyer that arrives
    // at full wet the moment it is inserted is not a kind default. It is left
    // where the Synth puts it.
}

void PX3DoomAudioProcessor::prepareFx(double sampleRate, int)
{
    doom.prepare(sampleRate);
}

DoomSettings PX3DoomAudioProcessor::settingsForBlock() const
{
    DoomSettings settings;
    settings.enabled = enabledParam->get();
    settings.freeze = freezeParam->get();
    settings.loopActive = loopActiveParam->get();
    settings.wetActive = wetActiveParam->get();
    settings.loopHalf = loopHalfParam->get();
    settings.clockSmooth = clockSmoothParam->get();
    settings.mix = mixParam->get();
    settings.clock = clockParam->get();
    settings.loopLength = loopLengthParam->get();
    settings.loopModify = loopModifyParam->get();
    settings.overdub = overdubParam->get();
    settings.fade = fadeParam->get();
    settings.wetTime = wetTimeParam->get();
    settings.wetModify = wetModifyParam->get();
    settings.cross = crossParam->get();
    settings.glue = glueParam->get();
    settings.eq = eqParam->get();
    settings.balance = balanceParam->get();
    settings.blend = blendParam->get();
    settings.spread = spreadParam->get();
    settings.routingIndex = routingParam->getIndex();
    settings.loopModeIndex = loopModeParam->getIndex();
    settings.wetModeIndex = wetModeParam->getIndex();
    settings.crossSourceIndex = crossSourceParam->getIndex();
    return settings;
}

void PX3DoomAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    doom.updateForBlock(settingsForBlock());

    const auto numSamples = buffer.getNumSamples();
    const auto stereo = buffer.getNumChannels() > 1;
    auto* left = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = stereo ? right[i] : inL;

        float outL = inL, outR = inR;
        doom.processSampleFrame(inL, inR, outL, outR);

        if (stereo) { left[i] = outL; right[i] = outR; }
        else        { left[i] = 0.5f * (outL + outR); }
    }
}

juce::AudioProcessorEditor* PX3DoomAudioProcessor::createEditor()
{
    return new PX3DoomAudioProcessorEditor(*this);
}
