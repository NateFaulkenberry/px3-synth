#pragma once

#include "FxPluginProcessor.h"
#include "Reverb.h"
#include "ReverbParameters.h"
#include "ReverbTypes.h"

#include <array>

// PX3 Reverb. The same shared/DSP/Reverb the Synth runs, with the same
// parameters (ReverbParameters.h), the same card and the same type presets.
class PX3ReverbAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3ReverbAudioProcessor();

    const juce::String getName() const override { return "PX3 Reverb"; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    // CLOUD reaches 60 seconds; a host that trims the tail early cuts it off.
    double getTailLengthSeconds() const override { return 60.0; }

    juce::AudioParameterBool& enabled() { return *enabledParam; }
    juce::AudioParameterChoice& algorithm() { return *algorithmParam; }
    juce::AudioParameterFloat& amount() { return *amountParam; }
    juce::AudioParameterFloat& control(px3::reverb::Control c) { return *controlParams[static_cast<std::size_t>(c)]; }
    ReverbSettings debugSettingsForBlock() const { return settingsForBlock(); }

    // The PRESET menu's selection ("TYPE/Name"), and applying one. Message thread.
    juce::String getPresetSelection() const { return presetSelection; }
    void setPresetSelection(const juce::String& selection) { presetSelection = selection; }
    void applyPreset(int type, const juce::String& name);

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;
    void writeExtraState(juce::ValueTree& state) const override;
    void readExtraState(const juce::ValueTree& state) override;

private:
    ReverbSettings settingsForBlock() const;

    ::Reverb reverb;

    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterChoice* algorithmParam { nullptr };
    juce::AudioParameterFloat* amountParam { nullptr };
    std::array<juce::AudioParameterFloat*, px3::reverb::kControlCount> controlParams {};
    juce::String presetSelection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3ReverbAudioProcessor)
};
