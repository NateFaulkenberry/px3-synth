#pragma once

#include "FxPluginProcessor.h"
#include "Mood.h"
#include "MoodTypes.h"

// PX3 Mood.
//
// The same Mood the Synth runs - shared/DSP/Mood - with a host adapter around
// it. Parameters read directly rather than through the Synth's modulation
// accumulator, which is the only difference between the two consumers.
class PX3MoodAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3MoodAudioProcessor();

    const juce::String getName() const override { return "PX3 Mood"; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Loops and long wet times keep sounding well past the input.
    double getTailLengthSeconds() const override { return 12.0; }

    //---- for the editor and the tests -------------------------------------
    juce::AudioParameterBool& enabled() { return *enabledParam; }
    juce::AudioParameterBool& freeze() { return *freezeParam; }
    juce::AudioParameterFloat& mix() { return *mixParam; }
    juce::AudioParameterFloat& clock() { return *clockParam; }
    juce::AudioParameterFloat& wetTime() { return *wetTimeParam; }
    juce::AudioParameterFloat& wetModify() { return *wetModifyParam; }
    juce::AudioParameterFloat& loopLength() { return *loopLengthParam; }
    juce::AudioParameterFloat& loopModify() { return *loopModifyParam; }
    juce::AudioParameterFloat& feedback() { return *feedbackParam; }
    juce::AudioParameterFloat& spread() { return *spreadParam; }
    juce::AudioParameterFloat& degrade() { return *degradeParam; }
    juce::AudioParameterChoice& routing() { return *routingParam; }
    juce::AudioParameterChoice& wetMode() { return *wetModeParam; }
    juce::AudioParameterChoice& loopMode() { return *loopModeParam; }
    MoodSettings debugSettingsForBlock() const { return settingsForBlock(); }

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;

private:
    MoodSettings settingsForBlock() const;

    Mood mood;

    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterBool* freezeParam { nullptr };
    juce::AudioParameterFloat* mixParam { nullptr };
    juce::AudioParameterFloat* clockParam { nullptr };
    juce::AudioParameterFloat* wetTimeParam { nullptr };
    juce::AudioParameterFloat* wetModifyParam { nullptr };
    juce::AudioParameterFloat* loopLengthParam { nullptr };
    juce::AudioParameterFloat* loopModifyParam { nullptr };
    juce::AudioParameterFloat* feedbackParam { nullptr };
    juce::AudioParameterFloat* spreadParam { nullptr };
    juce::AudioParameterFloat* degradeParam { nullptr };
    juce::AudioParameterChoice* routingParam { nullptr };
    juce::AudioParameterChoice* wetModeParam { nullptr };
    juce::AudioParameterChoice* loopModeParam { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3MoodAudioProcessor)
};
