#pragma once

#include "FxPluginProcessor.h"
#include "Delay.h"
#include "DelayTypes.h"

// PX3 Delay.
//
// The same Delay the Synth runs - shared/DSP/Delay, one implementation - with
// a host adapter around it instead of the Synth's mixer.
//
// The difference between the two consumers is exactly one thing: the Synth
// builds DelaySettings through its modulation accumulator, so an LFO or a
// macro can move the delay time. There is no modulation matrix here, so this
// reads the parameters directly. That is the boundary between "the FX
// parameter" and "the Synth's modulation destination", and it is the only
// place the two differ.
class PX3DelayAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3DelayAudioProcessor();

    const juce::String getName() const override { return "PX3 Delay"; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // The delay's own tail: feedback keeps sounding after input stops, and a
    // host that trims the tail too early cuts repeats off.
    double getTailLengthSeconds() const override { return 8.0; }

    //---- for the tests ----------------------------------------------------
    juce::AudioParameterFloat& debugAmountParam() { return *amountParam; }
    juce::AudioParameterFloat& debugTimeParam() { return *timeParam; }
    juce::AudioParameterFloat& debugFeedbackParam() { return *feedbackParam; }
    juce::AudioParameterBool& debugEnabledParam() { return *enabledParam; }
    juce::AudioParameterChoice& debugAlgorithmParam() { return *algorithmParam; }
    DelaySettings debugSettingsForBlock() const { return settingsForBlock(); }

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;

private:
    DelaySettings settingsForBlock() const;

    Delay delay;

    // Parameter IDs match the Synth's, deliberately. They are in a different
    // plug-in so they cannot collide, and keeping them identical means a
    // reader comparing the two sees the same names for the same controls.
    juce::AudioParameterFloat* amountParam { nullptr };
    juce::AudioParameterFloat* timeParam { nullptr };
    juce::AudioParameterFloat* feedbackParam { nullptr };
    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterChoice* algorithmParam { nullptr };
    juce::AudioParameterChoice* granularModeParam { nullptr };
    juce::AudioParameterChoice* syncDivisionParam { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3DelayAudioProcessor)
};
