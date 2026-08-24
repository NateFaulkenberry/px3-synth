#pragma once

#include <JuceHeader.h>

// Reusable LFO UI section that visualizes and lays out controls while
// remaining independent from modulation destinations.
class GenericLfoComponent final : public juce::Component
{
public:
    GenericLfoComponent(juce::Slider& rateKnobIn,
                        juce::Label& rateLabelIn,
                        juce::Label& rateValueLabelIn,
                        juce::ComboBox& waveformBoxIn,
                        juce::Label& waveformLabelIn,
                        juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void refreshFromParameters(float rateHz, int waveformIndex);
    void advanceAnimation(float deltaPhase);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    static float waveformSample(float phaseNorm, int waveformIndex);

    juce::Slider& rateKnob;
    juce::Label& rateLabel;
    juce::Label& rateValueLabel;
    juce::ComboBox& waveformBox;
    juce::Label& waveformLabel;
    juce::Colour accent;

    int currentWaveformIndex { 0 };
    float visualPhase { 0.0f };
};
