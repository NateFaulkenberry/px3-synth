#pragma once

#include "FxPluginProcessor.h"
#include "Lucy.h"
#include "LucyTypes.h"

// PX3 Lucy. The same shared/DSP/Lucy the Synth runs - the spectral loss,
// packet corruption and freeze described in docs/LUCY_DSP_DESIGN.md.
class PX3LucyAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3LucyAudioProcessor();

    const juce::String getName() const override { return "PX3 Lucy"; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    // A frozen spectrum sustains, and the reverb has its own tail.
    double getTailLengthSeconds() const override { return 15.0; }

    juce::AudioParameterFloat& global() { return *globalParam; }
    juce::AudioParameterFloat& loss() { return *lossParam; }
    juce::AudioParameterFloat& speed() { return *speedParam; }
    juce::AudioParameterFloat& filter() { return *filterParam; }
    juce::AudioParameterFloat& filterFreq() { return *filterFreqParam; }
    juce::AudioParameterFloat& verb() { return *verbParam; }
    juce::AudioParameterFloat& verbDecay() { return *verbDecayParam; }
    juce::AudioParameterFloat& freezer() { return *freezerParam; }
    juce::AudioParameterFloat& gateCutoff() { return *gateCutoffParam; }
    juce::AudioParameterFloat& threshold() { return *thresholdParam; }
    juce::AudioParameterFloat& autoGain() { return *autoGainParam; }
    juce::AudioParameterFloat& weighting() { return *weightingParam; }
    juce::AudioParameterFloat& gain() { return *gainParam; }
    juce::AudioParameterFloat& spread() { return *spreadParam; }
    juce::AudioParameterBool& enabled() { return *enabledParam; }
    juce::AudioParameterBool& filterInvert() { return *filterInvertParam; }
    juce::AudioParameterBool& verbPost() { return *verbPostParam; }
    juce::AudioParameterBool& freeze() { return *freezeParam; }
    juce::AudioParameterBool& freezeSlushy() { return *freezeSlushyParam; }
    juce::AudioParameterBool& gate() { return *gateParam; }
    juce::AudioParameterBool& slow() { return *slowParam; }
    juce::AudioParameterChoice& mode() { return *modeParam; }
    juce::AudioParameterChoice& packets() { return *packetsParam; }
    juce::AudioParameterChoice& slope() { return *slopeParam; }
    LucySettings debugSettingsForBlock() const { return settingsForBlock(); }

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;

private:
    LucySettings settingsForBlock() const;

    px3::Lucy lucy;

    juce::AudioParameterFloat* globalParam { nullptr };
    juce::AudioParameterFloat* lossParam { nullptr };
    juce::AudioParameterFloat* speedParam { nullptr };
    juce::AudioParameterFloat* filterParam { nullptr };
    juce::AudioParameterFloat* filterFreqParam { nullptr };
    juce::AudioParameterFloat* verbParam { nullptr };
    juce::AudioParameterFloat* verbDecayParam { nullptr };
    juce::AudioParameterFloat* freezerParam { nullptr };
    juce::AudioParameterFloat* gateCutoffParam { nullptr };
    juce::AudioParameterFloat* thresholdParam { nullptr };
    juce::AudioParameterFloat* autoGainParam { nullptr };
    juce::AudioParameterFloat* weightingParam { nullptr };
    juce::AudioParameterFloat* gainParam { nullptr };
    juce::AudioParameterFloat* spreadParam { nullptr };
    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterBool* filterInvertParam { nullptr };
    juce::AudioParameterBool* verbPostParam { nullptr };
    juce::AudioParameterBool* freezeParam { nullptr };
    juce::AudioParameterBool* freezeSlushyParam { nullptr };
    juce::AudioParameterBool* gateParam { nullptr };
    juce::AudioParameterBool* slowParam { nullptr };
    juce::AudioParameterChoice* modeParam { nullptr };
    juce::AudioParameterChoice* packetsParam { nullptr };
    juce::AudioParameterChoice* slopeParam { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3LucyAudioProcessor)
};
