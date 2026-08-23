#include "AudioEnginePanel.h"

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

AudioEnginePanel::AudioEnginePanel(SynthProjectAudioProcessor& processorIn)
    : processor(processorIn)
{
    configureMiniSlider(positionSlider);
    configureMiniSlider(grainSlider);
    configureMiniSlider(textureSlider);
    configureMiniSlider(animateSlider);
    configureMiniSlider(rateSlider);

    positionSlider.setRange(0.0, 1.0, 0.0);
    grainSlider.setRange(0.0, 1.0, 0.0);
    textureSlider.setRange(0.0, 1.0, 0.0);
    animateSlider.setRange(0.0, 1.0, 0.0);
    rateSlider.setRange(0.01, 4.0, 0.0);

    auto& positionParam = processor.getAudioPositionParam();
    auto& grainParam = processor.getAudioGrainParam();
    auto& textureParam = processor.getAudioTextureParam();
    auto& animateParam = processor.getAudioAnimateParam();
    auto& rateParam = processor.getAudioRateParam();
    auto& modeParam = processor.getAudioAnimModeParam();
    auto& syncParam = processor.getAudioAnimSyncParam();

    modeBox.addItem("Forward", 1);
    modeBox.addItem("Reverse", 2);
    modeBox.addItem("PingPong", 3);
    modeBox.setSelectedItemIndex(modeParam.getIndex(), juce::dontSendNotification);

    const auto syncChoices = syncParam.choices.size();
    for (int i = 0; i < syncChoices; ++i)
    {
        syncBox.addItem(syncParam.choices[i], i + 1);
    }
    syncBox.setSelectedItemIndex(syncParam.getIndex(), juce::dontSendNotification);

    modeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    modeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    modeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    syncBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    syncBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    syncBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    configureMiniLabel(positionLabel, "POS");
    configureMiniLabel(grainLabel, "GRAIN");
    configureMiniLabel(textureLabel, "TEXT");
    configureMiniLabel(animateLabel, "ANIM");
    configureMiniLabel(rateLabel, "RATE");
    configureMiniLabel(modeLabel, "MODE");
    configureMiniLabel(syncLabel, "SYNC");

    offButton.onClick = [this]() { processor.disableAudioEngine(); };
    resetButton.onClick = [this]() { processor.resetAudioEngine(); };
    offButton.setTooltip("Disable Audio Engine routing");
    resetButton.setTooltip("Reset Audio Engine parameters and loaded audio");

    addAndMakeVisible(positionSlider);
    addAndMakeVisible(grainSlider);
    addAndMakeVisible(textureSlider);
    addAndMakeVisible(animateSlider);
    addAndMakeVisible(rateSlider);
    addAndMakeVisible(modeBox);
    addAndMakeVisible(syncBox);
    addAndMakeVisible(positionLabel);
    addAndMakeVisible(grainLabel);
    addAndMakeVisible(textureLabel);
    addAndMakeVisible(animateLabel);
    addAndMakeVisible(rateLabel);
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(syncLabel);
    addAndMakeVisible(offButton);
    addAndMakeVisible(resetButton);

    attachSlider(positionParam, positionSlider);
    attachSlider(grainParam, grainSlider);
    attachSlider(textureParam, textureSlider);
    attachSlider(animateParam, animateSlider);
    attachSlider(rateParam, rateSlider);
    attachComboBox(modeParam, modeBox);
    attachComboBox(syncParam, syncBox);

    processor.copyCurrentAudioWaveformPreview(waveform);

    startTimerHz(30);
}

void AudioEnginePanel::attachSlider(juce::RangedAudioParameter& parameter, juce::Slider& slider)
{
    sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(parameter, slider, nullptr));
}

void AudioEnginePanel::attachComboBox(juce::RangedAudioParameter& parameter, juce::ComboBox& comboBox)
{
    comboBoxAttachments.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(parameter, comboBox, nullptr));
}

bool AudioEnginePanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (isSupportedAudioFile(juce::File(f)))
        {
            return true;
        }
    }

    return false;
}

void AudioEnginePanel::fileDragEnter(const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void AudioEnginePanel::fileDragExit(const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void AudioEnginePanel::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;

    for (const auto& f : files)
    {
        const juce::File file(f);
        if (isSupportedAudioFile(file))
        {
            requestLoadFile(file);
            return;
        }
    }

    loadErrorFlash = true;
    errorFlashTicks = 18;
    repaint();
}

void AudioEnginePanel::mouseUp(const juce::MouseEvent& event)
{
    if (!waveformArea.contains(event.getPosition()))
    {
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser>("Choose an audio file for P(X3)",
                                                       juce::File(),
                                                       "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg",
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

void AudioEnginePanel::paint(juce::Graphics& g)
{
    auto panel = getLocalBounds().toFloat();

    g.setColour(juce::Colour::fromRGBA(44, 42, 58, 210));
    g.fillRoundedRectangle(panel, 10.0f);

    g.setColour(juce::Colour::fromRGBA(218, 182, 255, 120));
    g.drawRoundedRectangle(panel, 10.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(240, 230, 255));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("AUDIO ENGINE", getLocalBounds().removeFromTop(16), juce::Justification::centred);

    const auto waveAreaF = waveformArea.toFloat();

    g.setColour(dragOver ? juce::Colour::fromRGBA(188, 140, 255, 80)
                         : juce::Colour::fromRGBA(255, 255, 255, 18));
    g.fillRoundedRectangle(waveAreaF, 7.0f);

    g.setColour(dragOver ? juce::Colour::fromRGBA(220, 188, 255, 220)
                         : juce::Colour::fromRGBA(255, 255, 255, 84));
    g.drawRoundedRectangle(waveAreaF, 7.0f, 1.0f);

    if (waveform.size() > 3)
    {
        juce::Path path;
        const auto cx = waveAreaF.getX();
        const auto cy = waveAreaF.getCentreY();
        const auto w = waveAreaF.getWidth();
        const auto h = waveAreaF.getHeight() * 0.80f;

        path.startNewSubPath(cx, cy);
        for (std::size_t i = 0; i < waveform.size(); ++i)
        {
            const auto x = cx + (static_cast<float>(i) / static_cast<float>(waveform.size() - 1)) * w;
            const auto mag = juce::jlimit(0.0f, 1.0f, waveform[i]);
            const auto top = cy - mag * h * 0.5f;
            const auto bottom = cy + mag * h * 0.5f;
            path.startNewSubPath(x, top);
            path.lineTo(x, bottom);
        }

        g.setColour(juce::Colour::fromRGBA(210, 188, 255, 180));
        g.strokePath(path, juce::PathStrokeType(1.0f));

        const auto markerX = waveAreaF.getX() + juce::jlimit(0.0f, 1.0f, currentPosition) * waveAreaF.getWidth();
        const auto regionWidth = waveAreaF.getWidth() * juce::jmap(static_cast<float>(textureSlider.getValue()), 0.04f, 0.22f);
        g.setColour(juce::Colour::fromRGBA(188, 140, 255, 38));
        g.fillRoundedRectangle(juce::Rectangle<float>(markerX - regionWidth * 0.5f,
                                  waveAreaF.getY() + 2.0f,
                                  regionWidth,
                                  waveAreaF.getHeight() - 4.0f),
                       3.0f);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 210));
        g.drawLine(markerX, waveAreaF.getY() + 2.0f, markerX, waveAreaF.getBottom() - 2.0f, 1.2f);
    }
    else
    {
        g.setColour(juce::Colour::fromRGBA(236, 236, 236, 160));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText("DROP / CLICK AUDIO", waveformArea, juce::Justification::centred);
    }

    if (loadErrorFlash)
    {
        g.setColour(juce::Colour::fromRGBA(255, 90, 90, 100));
        g.fillRoundedRectangle(panel, 10.0f);
    }
}

void AudioEnginePanel::resized()
{
    auto area = getLocalBounds().reduced(6);
    area.removeFromTop(16);

    waveformArea = area.removeFromTop(48).reduced(2);
    area.removeFromTop(4);

    auto row1 = area.removeFromTop(14);
    positionLabel.setBounds(row1.removeFromLeft(38));
    positionSlider.setBounds(row1);

    area.removeFromTop(2);
    auto row2 = area.removeFromTop(14);
    grainLabel.setBounds(row2.removeFromLeft(38));
    grainSlider.setBounds(row2);

    area.removeFromTop(2);
    auto row3 = area.removeFromTop(14);
    textureLabel.setBounds(row3.removeFromLeft(38));
    textureSlider.setBounds(row3);

    area.removeFromTop(2);
    auto row4 = area.removeFromTop(14);
    animateLabel.setBounds(row4.removeFromLeft(38));
    animateSlider.setBounds(row4);

    area.removeFromTop(2);
    auto row5 = area.removeFromTop(14);
    rateLabel.setBounds(row5.removeFromLeft(38));
    rateSlider.setBounds(row5);

    area.removeFromTop(2);
    auto row6 = area.removeFromTop(16);
    modeLabel.setBounds(row6.removeFromLeft(38));
    modeBox.setBounds(row6);

    area.removeFromTop(2);
    auto row7 = area.removeFromTop(16);
    syncLabel.setBounds(row7.removeFromLeft(38));
    syncBox.setBounds(row7.removeFromLeft(juce::jmax(60, row7.getWidth() - 98)));
    row7.removeFromLeft(4);
    offButton.setBounds(row7.removeFromLeft(42));
    row7.removeFromLeft(4);
    resetButton.setBounds(row7.removeFromLeft(52));
}

void AudioEnginePanel::timerCallback()
{
    processor.copyCurrentAudioWaveformPreview(waveform);
    currentPosition = processor.copyCurrentAudioPosition();

    if (processor.consumeAudioLoadErrorFlag())
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

void AudioEnginePanel::requestLoadFile(const juce::File& file)
{
    if (!isSupportedAudioFile(file))
    {
        loadErrorFlash = true;
        errorFlashTicks = 18;
        return;
    }

    processor.requestAudioLoadAsync(file);
}

bool AudioEnginePanel::isSupportedAudioFile(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".mp3" || ext == ".ogg";
}
