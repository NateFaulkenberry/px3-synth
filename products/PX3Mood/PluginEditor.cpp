#include "PluginEditor.h"

#include "UIConfigManager.h"

namespace
{
// Mood's colour in the Synth's FX chain.
const juce::Colour kMoodAccent = juce::Colour::fromRGB(198, 140, 255);
} // namespace

PX3MoodAudioProcessorEditor::PX3MoodAudioProcessorEditor(PX3MoodAudioProcessor& processorIn)
    : juce::AudioProcessorEditor(&processorIn),
      processor(processorIn),
      panel(enabledButton, freezeButton,
            mixKnob, mixLabel, clockKnob, clockLabel,
            wetTimeKnob, wetTimeLabel, wetModifyKnob, wetModifyLabel,
            loopLengthKnob, loopLengthLabel, loopModifyKnob, loopModifyLabel,
            feedbackKnob, feedbackLabel, spreadKnob, spreadLabel,
            degradeKnob, degradeLabel,
            routingBox, routingLabel, wetModeBox, wetModeLabel,
            loopModeBox, loopModeLabel, kMoodAccent)
{
    const auto configFile = UIConfigManager::findShippingConfigFile();
    if (configFile.existsAsFile())
    {
        juce::String error;
        if (auto config = UIConfig::fromJsonText(configFile.loadFileAsString(), error))
        {
            uiConfig = config;
            panel.setUIConfig(uiConfig);
        }
    }

    const auto slider = [this](juce::RangedAudioParameter& p, juce::Slider& s)
    {
        s.setLookAndFeel(&knobLook);
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(p, s, nullptr));
    };
    slider(processor.mix(), mixKnob);
    slider(processor.clock(), clockKnob);
    slider(processor.wetTime(), wetTimeKnob);
    slider(processor.wetModify(), wetModifyKnob);
    slider(processor.loopLength(), loopLengthKnob);
    slider(processor.loopModify(), loopModifyKnob);
    slider(processor.feedback(), feedbackKnob);
    slider(processor.spread(), spreadKnob);
    slider(processor.degrade(), degradeKnob);

    boxAttachments.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(
        processor.routing(), routingBox, nullptr));
    boxAttachments.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(
        processor.wetMode(), wetModeBox, nullptr));
    boxAttachments.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(
        processor.loopMode(), loopModeBox, nullptr));
    buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(
        processor.enabled(), enabledButton, nullptr));
    buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(
        processor.freeze(), freezeButton, nullptr));

    addAndMakeVisible(panel);

    enabledButton.onStateChange = [this] { panel.setActive(processor.enabled().get()); };
    panel.setActive(processor.enabled().get());

    setSize(720, 340);
}

PX3MoodAudioProcessorEditor::~PX3MoodAudioProcessorEditor()
{
    // Attachments before the controls, and the look before it goes.
    sliderAttachments.clear();
    boxAttachments.clear();
    buttonAttachments.clear();

    for (auto* knob : { &mixKnob, &clockKnob, &wetTimeKnob, &wetModifyKnob, &loopLengthKnob,
                        &loopModifyKnob, &feedbackKnob, &spreadKnob, &degradeKnob })
    {
        knob->setLookAndFeel(nullptr);
    }
}

void PX3MoodAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(18, 20, 24));
}

void PX3MoodAudioProcessorEditor::resized()
{
    panel.setBounds(getLocalBounds().reduced(10));
}
