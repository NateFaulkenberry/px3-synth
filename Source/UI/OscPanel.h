#pragma once

#include <JuceHeader.h>

#include "LfoComponent.h"
#include "OscillatorDisplayComponent.h"

class OscPanel final : public juce::Component
{
public:
    OscPanel(juce::Slider& macroA,
             juce::Slider& macroB,
             juce::Slider& macroC,
             juce::Label& macroALabel,
             juce::Label& macroBLabel,
             juce::Label& macroCLabel,
             juce::ComboBox& modeBox,
             juce::Label& modeLabel,
             juce::ComboBox& vowelBox,
             juce::Label& vowelLabel,
             juce::Label& lfoAssignLabel,
             juce::ComboBox& lfoAssignBox,
             juce::Slider& lfoRateKnob,
             juce::Label& lfoRateLabel,
             juce::Label& lfoRateValueLabel,
             juce::ComboBox& lfoWaveformBox,
             juce::Label& lfoWaveformLabel,
             juce::Colour panelAccent,
             juce::Colour lfoAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromSelections(int modeIndex, int vowelIndex);
    void refreshLfoFromParameters(float rateHz, int waveformIndex);
    void advanceAnimation(float oscDeltaPhase, float lfoDeltaPhase);

private:
    std::unique_ptr<OscillatorDisplayComponent> oscillatorDisplayComponent;
    std::unique_ptr<LfoComponent> lfoComponent;

    juce::String title { "OSC" };
    juce::Colour accent;
};
