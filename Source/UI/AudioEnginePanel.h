#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <memory>

class AudioEnginePanel final : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               private juce::Timer
{
public:
    explicit AudioEnginePanel(SynthProjectAudioProcessor& processorIn);

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void mouseUp(const juce::MouseEvent& event) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void requestLoadFile(const juce::File& file);
    void attachSlider(juce::RangedAudioParameter& parameter, juce::Slider& slider);
    void attachComboBox(juce::RangedAudioParameter& parameter, juce::ComboBox& comboBox);
    static bool isSupportedAudioFile(const juce::File& file);

    SynthProjectAudioProcessor& processor;

    juce::Rectangle<int> waveformArea;
    juce::Slider positionSlider;
    juce::Slider grainSlider;
    juce::Slider textureSlider;
    juce::Slider animateSlider;
    juce::Slider rateSlider;
    juce::ComboBox modeBox;
    juce::ComboBox syncBox;
    juce::ComboBox targetBox;

    juce::Label positionLabel;
    juce::Label grainLabel;
    juce::Label textureLabel;
    juce::Label animateLabel;
    juce::Label rateLabel;
    juce::Label modeLabel;
    juce::Label syncLabel;
    juce::Label targetLabel;
    juce::TextButton offButton { "OFF" };
    juce::TextButton resetButton { "RESET" };

    std::vector<float> waveform;
    float currentPosition { 0.0f };
    bool dragOver { false };
    bool loadErrorFlash { false };
    int errorFlashTicks { 0 };

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> comboBoxAttachments;
};
