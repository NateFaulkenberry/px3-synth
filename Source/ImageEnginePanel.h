#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <memory>

class ImageEnginePanel final : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               private juce::Timer
{
public:
    explicit ImageEnginePanel(SynthProjectAudioProcessor& processorIn);

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
    static bool isSupportedImageFile(const juce::File& file);

    SynthProjectAudioProcessor& processor;

    juce::Rectangle<int> imagePreviewArea;
    juce::Rectangle<int> waveformArea;

    juce::Slider positionSlider;
    juce::Slider animateSlider;
    juce::Slider rateSlider;
    juce::ComboBox modeBox;
    juce::ComboBox targetBox;

    juce::Label positionLabel;
    juce::Label animateLabel;
    juce::Label rateLabel;
    juce::Label modeLabel;
    juce::Label targetLabel;

    juce::Image previewImage;
    std::vector<float> waveform;
    float currentPosition { 0.0f };
    bool dragOver { false };
    bool loadErrorFlash { false };
    int errorFlashTicks { 0 };

    std::unique_ptr<juce::FileChooser> fileChooser;
};
