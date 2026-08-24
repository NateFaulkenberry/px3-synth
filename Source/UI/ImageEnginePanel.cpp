#include "ImageEnginePanel.h"

#include <algorithm>

namespace
{
void configureMiniSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
}

void configureMiniLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(230, 230, 230));
    label.setFont(juce::FontOptions(10.0f));
    label.setInterceptsMouseClicks(false, false);
}
}

ImageEnginePanel::ImageEnginePanel(PX3SynthAudioProcessor& processorIn)
    : processor(processorIn)
{
    configureMiniSlider(positionSlider);
    configureMiniSlider(animateSlider);
    configureMiniSlider(rateSlider);

    positionSlider.setRange(0.0, 1.0, 0.0);
    animateSlider.setRange(0.0, 1.0, 0.0);
    rateSlider.setRange(0.01, 4.0, 0.0);

    auto& positionParam = processor.getImagePositionParam();
    auto& animateParam = processor.getImageAnimateParam();
    auto& rateParam = processor.getImageRateParam();
    auto& modeParam = processor.getImageAnimModeParam();
    auto& targetParam = processor.getImageTargetParam();

    modeBox.addItem("Forward", 1);
    modeBox.addItem("Reverse", 2);
    modeBox.addItem("PingPong", 3);
    modeBox.setSelectedItemIndex(modeParam.getIndex(), juce::dontSendNotification);

    const auto targetChoiceCount = targetParam.choices.size();
    for (int i = 0; i < targetChoiceCount; ++i)
    {
        targetBox.addItem(targetParam.choices[i], i + 1);
    }
    targetBox.setSelectedItemIndex(targetParam.getIndex(), juce::dontSendNotification);

    modeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    modeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    modeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    targetBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    targetBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    targetBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    configureMiniLabel(positionLabel, "POS");
    configureMiniLabel(animateLabel, "ANIM");
    configureMiniLabel(rateLabel, "RATE");
    configureMiniLabel(modeLabel, "MODE");
    configureMiniLabel(targetLabel, "TARGET");

    offButton.onClick = [this]() { processor.disableImageEngine(); };
    resetButton.onClick = [this]() { processor.resetImageEngine(); };
    offButton.setTooltip("Disable Image Engine routing");
    resetButton.setTooltip("Reset Image Engine parameters and loaded image");

    addAndMakeVisible(positionSlider);
    addAndMakeVisible(animateSlider);
    addAndMakeVisible(rateSlider);
    addAndMakeVisible(modeBox);
    addAndMakeVisible(targetBox);
    addAndMakeVisible(positionLabel);
    addAndMakeVisible(animateLabel);
    addAndMakeVisible(rateLabel);
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(targetLabel);
    addAndMakeVisible(offButton);
    addAndMakeVisible(resetButton);

    attachSlider(positionParam, positionSlider);
    attachSlider(animateParam, animateSlider);
    attachSlider(rateParam, rateSlider);
    attachComboBox(modeParam, modeBox);
    attachComboBox(targetParam, targetBox);

    waveform = processor.copyCurrentImageWaveformPreview(320);
    processor.copyImagePreview(previewImage);

    startTimerHz(30);
}

void ImageEnginePanel::attachSlider(juce::RangedAudioParameter& parameter, juce::Slider& slider)
{
    sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(parameter, slider, nullptr));
}

void ImageEnginePanel::attachComboBox(juce::RangedAudioParameter& parameter, juce::ComboBox& comboBox)
{
    comboBoxAttachments.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(parameter, comboBox, nullptr));
}

bool ImageEnginePanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (isSupportedImageFile(juce::File(f)))
        {
            return true;
        }
    }

    return false;
}

void ImageEnginePanel::fileDragEnter(const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void ImageEnginePanel::fileDragExit(const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void ImageEnginePanel::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;

    for (const auto& f : files)
    {
        const juce::File file(f);
        if (isSupportedImageFile(file))
        {
            requestLoadFile(file);
            return;
        }
    }

    loadErrorFlash = true;
    errorFlashTicks = 18;
    repaint();
}

void ImageEnginePanel::mouseUp(const juce::MouseEvent& event)
{
    if (!imagePreviewArea.contains(event.getPosition()))
    {
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser>("Choose an image for P(X3)",
                                                       juce::File(),
                                                       "*.png;*.jpg;*.jpeg;*.bmp;*.gif",
                                                       true);

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser)
                             {
                                 const auto selected = chooser.getResult();
                                 if (selected.existsAsFile())
                                 {
                                     requestLoadFile(selected);
                                 }
                                 fileChooser.reset();
                             });
}

void ImageEnginePanel::paint(juce::Graphics& g)
{
    auto panel = getLocalBounds().toFloat();

    g.setColour(juce::Colour::fromRGBA(34, 46, 58, 210));
    g.fillRoundedRectangle(panel, 10.0f);

    g.setColour(juce::Colour::fromRGBA(140, 208, 255, 120));
    g.drawRoundedRectangle(panel, 10.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(228, 244, 255));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("IMAGE ENGINE", getLocalBounds().removeFromTop(16), juce::Justification::centred);

    auto imageAreaF = imagePreviewArea.toFloat();
    auto waveAreaF = waveformArea.toFloat();

    g.setColour(dragOver ? juce::Colour::fromRGBA(120, 210, 255, 78)
                         : juce::Colour::fromRGBA(255, 255, 255, 22));
    g.fillRoundedRectangle(imageAreaF, 7.0f);

    g.setColour(dragOver ? juce::Colour::fromRGBA(150, 225, 255, 220)
                         : juce::Colour::fromRGBA(255, 255, 255, 84));
    g.drawRoundedRectangle(imageAreaF, 7.0f, 1.0f);

    if (previewImage.isValid())
    {
        auto fit = imageAreaF.reduced(4.0f).toNearestInt();
        g.drawImageWithin(previewImage,
                          fit.getX(),
                          fit.getY(),
                          fit.getWidth(),
                          fit.getHeight(),
                          juce::RectanglePlacement::centred,
                          false);
    }
    else
    {
        g.setColour(juce::Colour::fromRGBA(236, 236, 236, 160));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText("DROP / CLICK", imagePreviewArea, juce::Justification::centred);
    }

    const auto markerY = imageAreaF.getY() + (1.0f - juce::jlimit(0.0f, 1.0f, currentPosition)) * imageAreaF.getHeight();
    g.setColour(juce::Colour::fromRGBA(80, 220, 240, 190));
    g.drawLine(imageAreaF.getX() + 2.0f, markerY, imageAreaF.getRight() - 2.0f, markerY, 1.2f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 20));
    g.fillRoundedRectangle(waveAreaF, 7.0f);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 88));
    g.drawRoundedRectangle(waveAreaF, 7.0f, 1.0f);

    if (waveform.size() > 3)
    {
        juce::Path path;
        const auto cx = waveAreaF.getX();
        const auto cy = waveAreaF.getCentreY();
        const auto w = waveAreaF.getWidth();
        const auto h = waveAreaF.getHeight() * 0.44f;

        path.startNewSubPath(cx, cy - waveform[0] * h);
        for (std::size_t i = 1; i < waveform.size(); ++i)
        {
            const auto x = cx + (static_cast<float>(i) / static_cast<float>(waveform.size() - 1)) * w;
            const auto y = cy - waveform[i] * h;
            path.lineTo(x, y);
        }

        g.setColour(juce::Colour::fromRGBA(109, 224, 255, 220));
        g.strokePath(path, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (loadErrorFlash)
    {
        g.setColour(juce::Colour::fromRGBA(255, 90, 90, 100));
        g.fillRoundedRectangle(panel, 10.0f);
    }
}

void ImageEnginePanel::resized()
{
    auto area = getLocalBounds().reduced(6);
    area.removeFromTop(16);

    auto previewBand = area.removeFromTop(56);
    imagePreviewArea = previewBand.removeFromLeft(previewBand.getWidth() / 2).reduced(2);
    waveformArea = previewBand.reduced(2);

    area.removeFromTop(4);

    auto row1 = area.removeFromTop(14);
    positionLabel.setBounds(row1.removeFromLeft(32));
    positionSlider.setBounds(row1);

    area.removeFromTop(2);
    auto row2 = area.removeFromTop(14);
    animateLabel.setBounds(row2.removeFromLeft(32));
    animateSlider.setBounds(row2);

    area.removeFromTop(2);
    auto row3 = area.removeFromTop(14);
    rateLabel.setBounds(row3.removeFromLeft(32));
    rateSlider.setBounds(row3);

    area.removeFromTop(2);
    auto row4 = area.removeFromTop(16);
    modeLabel.setBounds(row4.removeFromLeft(32));
    modeBox.setBounds(row4);

    area.removeFromTop(2);
    auto row5 = area.removeFromTop(16);
    targetLabel.setBounds(row5.removeFromLeft(50));
    targetBox.setBounds(row5.removeFromLeft(juce::jmax(60, row5.getWidth() - 98)));
    row5.removeFromLeft(4);
    offButton.setBounds(row5.removeFromLeft(42));
    row5.removeFromLeft(4);
    resetButton.setBounds(row5.removeFromLeft(52));
}

void ImageEnginePanel::timerCallback()
{
    waveform = processor.copyCurrentImageWaveformPreview(320);
    currentPosition = processor.copyCurrentImagePosition();
    processor.copyImagePreview(previewImage);

    const auto wavetableModeActive = processor.getOscillatorModeParam().getIndex() == 8;
    targetBox.setEnabled(!wavetableModeActive);
    targetLabel.setEnabled(!wavetableModeActive);
    targetBox.setTooltip(wavetableModeActive
                             ? "When OSC mode is WAVETABLE, Image Engine is reserved for oscillator only."
                             : "");

    if (processor.consumeImageLoadErrorFlag())
    {
        loadErrorFlash = true;
        errorFlashTicks = 18;
    }

    if (errorFlashTicks > 0)
    {
        --errorFlashTicks;
        if (errorFlashTicks == 0)
        {
            loadErrorFlash = false;
        }
    }

    repaint();
}

void ImageEnginePanel::requestLoadFile(const juce::File& file)
{
    if (!isSupportedImageFile(file))
    {
        loadErrorFlash = true;
        errorFlashTicks = 18;
        return;
    }

    processor.requestImageLoadAsync(file);
}

bool ImageEnginePanel::isSupportedImageFile(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif";
}
