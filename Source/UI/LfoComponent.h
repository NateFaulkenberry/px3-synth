#pragma once

#include <JuceHeader.h>

// Reusable LFO UI section that visualizes and lays out controls while
// remaining independent from modulation destinations.
class LfoComponent final : public juce::Component
{
public:
    LfoComponent(juce::Label& assignLabelIn,
                        juce::ComboBox& assignBoxIn,
                        juce::Slider& rateKnobIn,
                        juce::Label& rateLabelIn,
                        juce::Label& rateValueLabelIn,
                        juce::ComboBox& waveformBoxIn,
                        juce::Label& waveformLabelIn,
                        juce::Colour accentIn);
    ~LfoComponent() override;

    void setAccentColour(juce::Colour accentIn);
    void refreshFromParameters(float rateHz, int waveformIndex);
    void advanceAnimation(float deltaPhase);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    static float waveformSample(float phaseNorm, int waveformIndex);

    struct WaveformComboLookAndFeel final : public juce::LookAndFeel_V4
    {
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                 juce::Label& label) override;
    };

    juce::Slider& rateKnob;
    juce::Label& rateLabel;
    juce::Label& rateValueLabel;
    juce::Label& assignLabel;
    juce::ComboBox& assignBox;
    juce::ComboBox& waveformBox;
    juce::Label& waveformLabel;
    juce::Colour accent;
    WaveformComboLookAndFeel waveformComboLookAndFeel;

    int currentWaveformIndex { 0 };
    float visualPhase { 0.0f };
};
