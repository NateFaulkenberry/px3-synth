#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3DoomAudioProcessor::PX3DoomAudioProcessor()
{
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);
    // EQ is a TILT: left removes highs, right removes lows. Bipolar in the
    // Synth, and declaring it 0..1 here would move its centre.
    const auto bipolar = juce::NormalisableRange<float>(-1.0f, 1.0f);

    addParameter(enabledParam = new juce::AudioParameterBool("fx.doom.enabled", "Doom Enabled", true));
    addParameter(freezeParam = new juce::AudioParameterBool("fx.doom.freeze", "Doom Freeze", false));
    addParameter(loopActiveParam = new juce::AudioParameterBool("fx.doom.loop.active", "Doom Looper Active", false));
    addParameter(wetActiveParam = new juce::AudioParameterBool("fx.doom.wet.active", "Doom Wet Active", true));
    addParameter(loopHalfParam = new juce::AudioParameterBool("fx.doom.loop.half", "Doom Loop Half", false));
    addParameter(clockSmoothParam = new juce::AudioParameterBool("fx.doom.clock.smooth", "Doom Clock Smooth", false));
    addParameter(mixParam = new juce::AudioParameterFloat("fx.doom.mix", "Doom Mix", unit, 0.0f));
    addParameter(clockParam = new juce::AudioParameterFloat("fx.doom.clock", "Doom Clock", unit, 1.0f));
    addParameter(loopLengthParam = new juce::AudioParameterFloat("fx.doom.loop.length", "Doom Loop Length", unit, 0.45f));
    addParameter(loopModifyParam = new juce::AudioParameterFloat("fx.doom.loop.modify", "Doom Loop Modify", unit, 0.50f));
    addParameter(overdubParam = new juce::AudioParameterFloat("fx.doom.overdub", "Doom Overdub", unit, 0.0f));
    addParameter(fadeParam = new juce::AudioParameterFloat("fx.doom.fade", "Doom Fade", unit, 1.0f));
    addParameter(wetTimeParam = new juce::AudioParameterFloat("fx.doom.wet.time", "Doom Wet Time", unit, 0.45f));
    addParameter(wetModifyParam = new juce::AudioParameterFloat("fx.doom.wet.modify", "Doom Wet Modify", unit, 0.40f));
    addParameter(crossParam = new juce::AudioParameterFloat("fx.doom.cross", "Doom Cross", unit, 0.0f));
    addParameter(glueParam = new juce::AudioParameterFloat("fx.doom.glue", "Doom Glue", unit, 0.15f));
    addParameter(eqParam = new juce::AudioParameterFloat("fx.doom.eq", "Doom EQ", bipolar, 0.0f));
    addParameter(balanceParam = new juce::AudioParameterFloat("fx.doom.balance", "Doom Balance", unit, 0.5f));
    addParameter(blendParam = new juce::AudioParameterFloat("fx.doom.blend", "Doom Blend", unit, 0.0f));
    addParameter(spreadParam = new juce::AudioParameterFloat("fx.doom.spread", "Doom Spread", unit, 0.5f));
    addParameter(routingParam = new juce::AudioParameterChoice(
        "fx.doom.routing", "Doom Routing", juce::StringArray { "INPUT", "INPUT+LOOP", "LOOP" }, 0));
    addParameter(loopModeParam = new juce::AudioParameterChoice(
        "fx.doom.loop.mode", "Doom Loop Mode", juce::StringArray { "BURST", "RADIO", "MASK" }, 1));
    addParameter(wetModeParam = new juce::AudioParameterChoice(
        "fx.doom.wet.mode", "Doom Wet Mode", juce::StringArray { "SOUP", "RELAY", "FLIP" }, 0));
    addParameter(crossSourceParam = new juce::AudioParameterChoice(
        "fx.doom.cross.source", "Doom Cross Source", juce::StringArray { "INPUT", "CHANNEL" }, 0));

    // Mix defaults to the Synth's 0 for every other product's Amount reason in
    // reverse: Doom's mix at 0 is the DRY signal, and a destroyer that arrives
    // at full wet the moment it is inserted is not a kind default. It is left
    // where the Synth puts it.
}

void PX3DoomAudioProcessor::prepareFx(double sampleRate, int)
{
    doom.prepare(sampleRate);
}

px3::DoomUserParameters PX3DoomAudioProcessor::userParametersForBlock() const
{
    px3::DoomUserParameters settings;
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
    settings.routing = static_cast<px3::DoomRouting>(routingParam->getIndex());
    settings.loopMode = static_cast<px3::DoomLoopMode>(loopModeParam->getIndex());
    settings.wetMode = static_cast<px3::DoomWetMode>(wetModeParam->getIndex());
    settings.crossSource = static_cast<px3::DoomCrossSource>(crossSourceParam->getIndex());
    return settings;
}

void PX3DoomAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    doom.updateForBlock(userParametersForBlock());

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
