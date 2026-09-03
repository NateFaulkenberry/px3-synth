#pragma once

#include "FxPluginProcessor.h"
#include "Chorus.h"
#include "ChorusTypes.h"

// PX3 Chorus. The same shared/DSP/Chorus the Synth runs.
class PX3ChorusAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3ChorusAudioProcessor();

    const juce::String getName() const override { return "PX3 Chorus"; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 0.5; }

    juce::AudioParameterBool& enabled() { return *enabledParam; }
    juce::AudioParameterChoice& mode() { return *modeParam; }
    juce::AudioParameterFloat& amount() { return *amountParam; }
    juce::AudioParameterFloat& rate() { return *rateParam; }
    juce::AudioParameterFloat& depth() { return *depthParam; }
    juce::AudioParameterFloat& width() { return *widthParam; }
    juce::AudioParameterFloat& spread() { return *spreadParam; }
    juce::AudioParameterFloat& tone() { return *toneParam; }
    juce::AudioParameterFloat& lowCut() { return *lowCutParam; }
    juce::AudioParameterFloat& feedback() { return *feedbackParam; }
    juce::AudioParameterFloat& character() { return *characterParam; }
    juce::AudioParameterFloat& mix() { return *mixParam; }
    ChorusSettings debugSettingsForBlock() const { return settingsForBlock(); }

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;

private:
    ChorusSettings settingsForBlock() const;

    px3::Chorus chorus;

    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterChoice* modeParam { nullptr };
    juce::AudioParameterFloat* amountParam { nullptr };
    juce::AudioParameterFloat* rateParam { nullptr };
    juce::AudioParameterFloat* depthParam { nullptr };
    juce::AudioParameterFloat* widthParam { nullptr };
    juce::AudioParameterFloat* spreadParam { nullptr };
    juce::AudioParameterFloat* toneParam { nullptr };
    juce::AudioParameterFloat* lowCutParam { nullptr };
    juce::AudioParameterFloat* feedbackParam { nullptr };
    juce::AudioParameterFloat* characterParam { nullptr };
    juce::AudioParameterFloat* mixParam { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3ChorusAudioProcessor)
};
