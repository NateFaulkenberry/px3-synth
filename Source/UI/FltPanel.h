#pragma once

#include <JuceHeader.h>

#include "FilterComponent.h"

class FltPanel final : public juce::Component
{
public:
    FltPanel(juce::Slider& cutoffKnob,
             juce::Label& cutoffLabel,
             juce::Slider& resonanceKnob,
             juce::Label& resonanceLabel,
             juce::ComboBox& filterTypeBox,
             juce::AudioParameterFloat& cutoffParam,
             juce::AudioParameterFloat& resonanceParam,
             juce::AudioParameterChoice& filterTypeParam,
             juce::Colour panelAccent);
    ~FltPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();

private:
    struct FilterComboLookAndFeel final : public juce::LookAndFeel_V4
    {
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                 juce::Label& label) override;
    };

    juce::Slider& cutoffKnob;
    juce::Label& cutoffLabel;
    juce::Slider& resonanceKnob;
    juce::Label& resonanceLabel;
    juce::ComboBox& filterTypeBox;

    FilterComboLookAndFeel filterComboLookAndFeel;

    std::unique_ptr<FilterComponent> filterComponent;

    juce::String title { "FLT" };
    juce::Colour accent;
};
