#include "FxCardEditor.h"

#include "UIConfigManager.h"

namespace px3::fx
{

FxCardEditor::FxCardEditor(juce::AudioProcessor& processorIn,
                           const juce::String& styleKey,
                           const juce::String& title)
    : juce::AudioProcessorEditor(&processorIn),
      card(styleKey, title)
{
    const auto configFile = UIConfigManager::findShippingConfigFile();
    if (configFile.existsAsFile())
    {
        juce::String error;
        if (auto config = UIConfig::fromJsonText(configFile.loadFileAsString(), error))
        {
            uiConfig = config;
            card.setUIConfig(uiConfig);
        }
    }

    addAndMakeVisible(card);
}

FxCardEditor::~FxCardEditor()
{
    // Attachments before the controls they point at, and the look-and-feel
    // before it goes out of scope under the knobs still holding it.
    sliderAttachments.clear();
    boxAttachments.clear();
    buttonAttachments.clear();

    for (auto* knob : styledKnobs)
    {
        if (knob != nullptr) { knob->setLookAndFeel(nullptr); }
    }
}

void FxCardEditor::attachKnob(const juce::String& id, juce::AudioParameterFloat& parameter)
{
    auto* slider = card.knob(id);
    if (slider == nullptr) { jassertfalse; return; }   // a row declared no such knob

    const auto& range = parameter.getNormalisableRange();
    slider->setRange(range.start, range.end);
    slider->setLookAndFeel(&knobLook);
    styledKnobs.push_back(slider);

    sliderAttachments.push_back(
        std::make_unique<juce::SliderParameterAttachment>(parameter, *slider, nullptr));
}

void FxCardEditor::attachChoice(const juce::String& id, juce::AudioParameterChoice& parameter)
{
    if (auto* box = card.choice(id))
    {
        boxAttachments.push_back(
            std::make_unique<juce::ComboBoxParameterAttachment>(parameter, *box, nullptr));
    }
    else { jassertfalse; }
}

void FxCardEditor::attachToggle(const juce::String& id, juce::AudioParameterBool& parameter)
{
    if (auto* button = card.toggle(id))
    {
        buttonAttachments.push_back(
            std::make_unique<juce::ButtonParameterAttachment>(parameter, *button, nullptr));
    }
    else { jassertfalse; }
}

void FxCardEditor::attachBypass(juce::AudioParameterBool& parameter)
{
    buttonAttachments.push_back(
        std::make_unique<juce::ButtonParameterAttachment>(parameter, card.bypassButton(), nullptr));
}

void FxCardEditor::finishSetup(int width, int height)
{
    setSize(width, height);
}

void FxCardEditor::paint(juce::Graphics& g)
{
    // The panel ground the Synth's FX page uses, so a card looks the same
    // standing alone as it does in the chain.
    g.fillAll(juce::Colour::fromRGB(18, 20, 24));
}

void FxCardEditor::resized()
{
    card.setBounds(getLocalBounds().reduced(10));
}

} // namespace px3::fx
