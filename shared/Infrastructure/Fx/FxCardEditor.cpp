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

void FxCardEditor::attachToggle(const juce::String& id, juce::RangedAudioParameter& parameter)
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

    // And grey the card while it is bypassed.
    //
    // The attachment above only moves the switch. Inside the Synth the panel
    // calls setActive on every card from refreshFxBypassUI, and standing alone
    // there is no panel to do it - so a bypassed effect kept its full colour
    // and its lit controls, and looked exactly like an effect that was running.
    card.bypassButton().onStateChange = [this]
    {
        card.setActive(card.bypassButton().getToggleState());
    };

    card.setActive(parameter.get());
}

void FxCardEditor::finishSetup()
{
    // Apply the config AGAIN, now that the product has declared its rows.
    //
    // The constructor applies it too, but a product declares its controls in
    // its own constructor body - which runs after this base class's - so at
    // that point the card has no knobs, boxes or toggles to style, and every
    // per-control key silently did nothing. Chip colours, caption colours,
    // fonts, dropdown colours: all of them read from UIConfig and none of them
    // reaching a control. The card itself looked right because its border,
    // background and artwork are read while painting rather than applied here.
    //
    // finishSetup is where this belongs because it is the one call every
    // product makes last, after everything exists.
    if (uiConfig != nullptr) { card.setUIConfig(uiConfig); }

    const auto window = standaloneFxWindowSize(uiConfig.get());
    setSize(window.getWidth(), window.getHeight());
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
    card.setBounds(getLocalBounds().reduced(kFxWindowMargin));
}

} // namespace px3::fx
