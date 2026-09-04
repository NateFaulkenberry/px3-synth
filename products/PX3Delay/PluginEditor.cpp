#include "PluginEditor.h"

#include "FxCardEditor.h"

#include "UIConfigManager.h"

namespace
{
// The delay's colour in the Synth's FX chain, so the standalone is recognisably
// the same effect rather than a differently-coloured relative.
const juce::Colour kDelayAccent = juce::Colour::fromRGB(120, 190, 255);
} // namespace

PX3DelayAudioProcessorEditor::PX3DelayAudioProcessorEditor(PX3DelayAudioProcessor& processorIn)
    : juce::AudioProcessorEditor(&processorIn),
      processor(processorIn),
      panel(enabledButton, amountKnob, amountLabel, algorithmBox, algorithmLabel,
            syncBox, syncLabel, modeBox, modeLabel, timeKnob, timeLabel,
            feedbackKnob, feedbackLabel, kDelayAccent)
{
    // The shipped style, so the standalone reads as PX3 rather than as JUCE.
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

    attach(processor.debugAmountParam(), amountKnob);
    attach(processor.debugTimeParam(), timeKnob);
    attach(processor.debugFeedbackParam(), feedbackKnob);
    attach(processor.debugAlgorithmParam(), algorithmBox);
    attach(processor.debugEnabledParam(), enabledButton);

    for (auto* knob : { &amountKnob, &timeKnob, &feedbackKnob })
    {
        knob->setLookAndFeel(&knobLook);
    }

    addAndMakeVisible(panel);

    // Follows the enable switch and the algorithm, the same way the Synth's
    // panel does: the granular controls only mean something on the granular
    // algorithm.
    const auto refresh = [this]
    {
        panel.setActive(processor.debugEnabledParam().get(),
                        processor.debugAlgorithmParam().getIndex() == 0);
    };
    enabledButton.onStateChange = refresh;
    algorithmBox.onChange = refresh;
    refresh();

    // The same window every other effect opens at: one cell of the Synth's FX
    // grid. This panel is not a card, but the Synth lays it out in that grid
    // beside the ones that are, so it has the same shape there.
    const auto window = px3::fx::standaloneFxWindowSize(uiConfig.get());
    setSize(window.getWidth(), window.getHeight());
}

PX3DelayAudioProcessorEditor::~PX3DelayAudioProcessorEditor()
{
    // Attachments first, before the controls they point at - the rule the
    // Synth's editor learned the hard way.
    sliderAttachments.clear();
    boxAttachments.clear();
    buttonAttachments.clear();

    // And the look before it goes, for the same reason.
    for (auto* knob : { &amountKnob, &timeKnob, &feedbackKnob })
    {
        knob->setLookAndFeel(nullptr);
    }
}

void PX3DelayAudioProcessorEditor::attach(juce::RangedAudioParameter& parameter, juce::Slider& slider)
{
    sliderAttachments.push_back(
        std::make_unique<juce::SliderParameterAttachment>(parameter, slider, nullptr));
}

void PX3DelayAudioProcessorEditor::attach(juce::RangedAudioParameter& parameter, juce::ComboBox& box)
{
    boxAttachments.push_back(
        std::make_unique<juce::ComboBoxParameterAttachment>(parameter, box, nullptr));
}

void PX3DelayAudioProcessorEditor::attach(juce::RangedAudioParameter& parameter, juce::Button& button)
{
    buttonAttachments.push_back(
        std::make_unique<juce::ButtonParameterAttachment>(parameter, button, nullptr));
}

void PX3DelayAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(18, 20, 24));
}

void PX3DelayAudioProcessorEditor::resized()
{
    panel.setBounds(getLocalBounds().reduced(px3::fx::kFxWindowMargin));
}
