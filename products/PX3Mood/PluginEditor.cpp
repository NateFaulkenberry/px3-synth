#include "PluginEditor.h"

#include "FxCardEditor.h"

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

    // Every knob carries its name, every box its options, and the chip its two
    // states - the same strings the Synth uses, because this is the same panel.
    //
    // MoodComponent places these controls and draws the card around them; it
    // does not fill them in. Handed empty ones it lays out a page of unlabelled
    // knobs and blank dropdowns, which is exactly what this window was.
    const auto label = [](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        l.setFont(juce::FontOptions(11.5f));
        l.setTooltip(text);
    };

    label(mixLabel, "MIX");
    label(clockLabel, "CLOCK");
    label(wetTimeLabel, "WET TIME");
    label(wetModifyLabel, "WET MOD");
    label(loopLengthLabel, "LOOP LEN");
    label(loopModifyLabel, "LOOP MOD");
    label(feedbackLabel, "FEEDBACK");
    label(spreadLabel, "SPREAD");
    label(degradeLabel, "DEGRADE");
    label(routingLabel, "ROUTE");
    label(wetModeLabel, "WET");
    label(loopModeLabel, "LOOP");

    // ComboBoxParameterAttachment selects an item; it does not create them.
    const auto fillBox = [](juce::ComboBox& box, juce::AudioParameterChoice& parameter)
    {
        for (int i = 0; i < parameter.choices.size(); ++i)
        {
            box.addItem(parameter.choices[i], i + 1);
        }
        box.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
        box.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
        box.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    };

    fillBox(routingBox, processor.routing());
    fillBox(wetModeBox, processor.wetMode());
    fillBox(loopModeBox, processor.loopMode());

    freezeButton.setButtonText("FREEZE OFF");
    freezeButton.setStateLabels("FREEZE ON", "FREEZE OFF");
    freezeButton.setTooltip("Freeze the Mood loop");
    freezeButton.setAccentColour(kMoodAccent);

    const auto slider = [this](juce::RangedAudioParameter& p, juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        const auto& range = p.getNormalisableRange();
        s.setRange(range.start, range.end);
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

    // Tinted and named like the Synth's, so the glyph lights in this
    // effect's colour and its hover text says what it powers.
    enabledButton.setAccentColour(kMoodAccent);
    enabledButton.setSectionName("Mood");

    addAndMakeVisible(panel);

    enabledButton.onStateChange = [this] { panel.setActive(processor.enabled().get()); };
    panel.setActive(processor.enabled().get());

    // The same window every other effect opens at: one cell of the Synth's FX
    // grid. This panel is not a card, but the Synth lays it out in that grid
    // beside the ones that are, so it has the same shape there.
    const auto window = px3::fx::standaloneFxWindowSize(uiConfig.get());
    setSize(window.getWidth(), window.getHeight());
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
    panel.setBounds(getLocalBounds().reduced(px3::fx::kFxWindowMargin));
}
