#include "ModPanel.h"

#include "UIConfig.h"

#include <cmath>

ModPanel::ModPanel(PX3SynthAudioProcessor& processorIn,
                   juce::ToggleButton& lfoEnabledButton,
                   juce::Label& lfoEnabledLabel,
                   juce::Label& lfoAssignLabel,
                   juce::ComboBox& lfoAssignBox,
                   juce::Slider& lfoRateKnob,
                   juce::Label& lfoRateLabel,
                   juce::Label& lfoRateValueLabel,
                                     juce::Slider& lfoAmountKnob,
                                     juce::Label& lfoAmountLabel,
                                     juce::Label& lfoAmountValueLabel,
                   juce::ComboBox& lfoWaveformBox,
                   juce::Label& lfoWaveformLabel,
                 juce::LookAndFeel* sharedLfoKnobLookAndFeel,
                   juce::Colour panelAccent,
                   juce::Colour lfoAccent)
        : processor(processorIn),
            accent(panelAccent),
        lfoHeaderAccent(lfoAccent),
        lfoKnobLookAndFeel(sharedLfoKnobLookAndFeel)
{
    lfoComponent = std::make_unique<LfoComponent>(lfoEnabledButton,
                                                  lfoEnabledLabel,
                                                  lfoAssignLabel,
                                                  lfoAssignBox,
                                                  lfoRateKnob,
                                                  lfoRateLabel,
                                                  lfoRateValueLabel,
                                                      lfoAmountKnob,
                                                      lfoAmountLabel,
                                                      lfoAmountValueLabel,
                                                  lfoWaveformBox,
                                                  lfoWaveformLabel,
                                                  lfoAccent);

    // Match additional LFO cards: no explicit "Freq" chip label under the knob.
    lfoRateLabel.setText("", juce::dontSendNotification);
    lfoRateLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    addAndMakeVisible(*lfoComponent);

    for (int lfoIndex = 1; lfoIndex < PX3SynthAudioProcessor::kLfoSourceCount; ++lfoIndex)
    {
        auto& bundle = extraLfos[static_cast<std::size_t>(lfoIndex - 1)];
        configureOwnedLfoBundle(lfoIndex, bundle);
        addAndMakeVisible(*bundle.component);
    }

    for (int envIndex = 0; envIndex < PX3SynthAudioProcessor::kEnvelopeSourceCount; ++envIndex)
    {
        auto& bundle = envelopes[static_cast<std::size_t>(envIndex)];
        configureOwnedEnvBundle(envIndex, bundle);
        addAndMakeVisible(*bundle.component);
    }
}

void ModPanel::configureOwnedLfoBundle(int lfoIndex, LfoBundle& bundle)
{
    bundle.enabledLabel.setText("ON", juce::dontSendNotification);
    bundle.enabledLabel.setJustificationType(juce::Justification::centredLeft);
    bundle.enabledLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.enabledLabel.setFont(juce::FontOptions(11.5f));
    bundle.enabledLabel.setInterceptsMouseClicks(false, false);

    bundle.assignLabel.setText("Assign", juce::dontSendNotification);
    bundle.assignLabel.setJustificationType(juce::Justification::centred);
    bundle.assignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignLabel.setFont(juce::FontOptions(11.5f));
    bundle.assignLabel.setInterceptsMouseClicks(true, false);
    bundle.assignLabel.setTooltip("LFO Assignment");

    bundle.rateLabel.setText("", juce::dontSendNotification);
    bundle.rateLabel.setJustificationType(juce::Justification::centred);
    bundle.rateLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.rateLabel.setFont(juce::FontOptions(13.0f));
    bundle.rateLabel.setInterceptsMouseClicks(false, false);
    bundle.rateLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    bundle.rateValueLabel.setJustificationType(juce::Justification::centred);
    bundle.rateValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    bundle.rateValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    bundle.rateValueLabel.setFont(juce::FontOptions(11.0f));
    bundle.rateValueLabel.setInterceptsMouseClicks(false, false);

    bundle.waveformLabel.setText("WAVE", juce::dontSendNotification);
    bundle.waveformLabel.setJustificationType(juce::Justification::centredLeft);
    bundle.waveformLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.waveformLabel.setFont(juce::FontOptions(11.5f));
    bundle.waveformLabel.setInterceptsMouseClicks(true, false);
    bundle.waveformLabel.setTooltip("Waveform");

    bundle.amountLabel.setText("AMOUNT", juce::dontSendNotification);
    bundle.amountLabel.setJustificationType(juce::Justification::centred);
    bundle.amountLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.amountLabel.setFont(juce::FontOptions(11.0f));
    bundle.amountLabel.setInterceptsMouseClicks(false, false);
    bundle.amountLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    bundle.amountValueLabel.setJustificationType(juce::Justification::centred);
    bundle.amountValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    bundle.amountValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    bundle.amountValueLabel.setFont(juce::FontOptions(11.0f));
    bundle.amountValueLabel.setInterceptsMouseClicks(false, false);

    bundle.enabledButton.setButtonText("");
    bundle.enabledButton.setClickingTogglesState(true);
    bundle.enabledButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
    bundle.enabledButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));

    bundle.assignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    bundle.assignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    bundle.waveformBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    bundle.waveformBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.waveformBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    bundle.rateKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bundle.rateKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    const auto& lfoRateParam = processor.getLfoFrequencyParam(lfoIndex);
    const auto& lfoRateRange = lfoRateParam.getNormalisableRange();
    bundle.rateKnob.setRange(lfoRateRange.start, lfoRateRange.end);
    if (lfoKnobLookAndFeel != nullptr)
    {
        bundle.rateKnob.setLookAndFeel(lfoKnobLookAndFeel);
    }

    bundle.amountKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bundle.amountKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bundle.amountKnob.setRange(-1.0, 1.0, 0.0);
    if (lfoKnobLookAndFeel != nullptr)
    {
        bundle.amountKnob.setLookAndFeel(lfoKnobLookAndFeel);
    }

    const auto& lfoWaveformParam = processor.getLfoWaveformParam(lfoIndex);
    for (int i = 0; i < lfoWaveformParam.choices.size(); ++i)
    {
        bundle.waveformBox.addItem(lfoWaveformParam.choices[i], i + 1);
    }
    bundle.waveformBox.setSelectedItemIndex(lfoWaveformParam.getIndex(), juce::dontSendNotification);

    const auto& assignments = processor.getLfoAssignmentDisplayNames();
    for (int i = 0; i < assignments.size(); ++i)
    {
        bundle.assignBox.addItem(assignments[i], i + 1);
    }
    bundle.assignBox.setSelectedId(processor.getLfoAssignmentIndex(lfoIndex) + 1, juce::dontSendNotification);

    bundle.assignBox.onChange = [this, lfoIndex, &bundle]()
    {
        const auto selected = juce::jmax(0, bundle.assignBox.getSelectedId() - 1);
        processor.setLfoAssignmentIndex(lfoIndex, selected);
    };

    bundle.rateKnob.onValueChange = [&bundle]()
    {
        const auto hz = juce::jlimit(0.01f, 20.0f, static_cast<float>(bundle.rateKnob.getValue()));
        bundle.rateValueLabel.setText(juce::String(hz, 2) + " Hz", juce::dontSendNotification);
    };

    bundle.amountKnob.onValueChange = [&bundle]()
    {
        const auto amount = juce::jlimit(-1.0f, 1.0f, static_cast<float>(bundle.amountKnob.getValue()));
        const auto amountPercent = static_cast<int>(std::lround(amount * 100.0f));
        const auto prefix = amountPercent > 0 ? juce::String("+") : juce::String();
        bundle.amountValueLabel.setText(prefix + juce::String(amountPercent) + "%", juce::dontSendNotification);
    };

    bundle.enabledAttachment = std::make_unique<juce::ButtonParameterAttachment>(processor.getLfoEnabledParam(lfoIndex), bundle.enabledButton, nullptr);
    bundle.rateAttachment = std::make_unique<juce::SliderParameterAttachment>(processor.getLfoFrequencyParam(lfoIndex), bundle.rateKnob, nullptr);
    bundle.amountAttachment = std::make_unique<juce::SliderParameterAttachment>(processor.getLfoAmountParam(lfoIndex), bundle.amountKnob, nullptr);
    bundle.waveformAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(processor.getLfoWaveformParam(lfoIndex), bundle.waveformBox, nullptr);

    bundle.component = std::make_unique<LfoComponent>(bundle.enabledButton,
                                                      bundle.enabledLabel,
                                                      bundle.assignLabel,
                                                      bundle.assignBox,
                                                      bundle.rateKnob,
                                                      bundle.rateLabel,
                                                      bundle.rateValueLabel,
                                                      bundle.amountKnob,
                                                      bundle.amountLabel,
                                                      bundle.amountValueLabel,
                                                      bundle.waveformBox,
                                                      bundle.waveformLabel,
                                                      lfoHeaderAccent,
                                                      "mod.lfo" + juce::String(lfoIndex + 1));
}

void ModPanel::configureOwnedEnvBundle(int envIndex, EnvBundle& bundle)
{
    bundle.enabledLabel.setText("ON", juce::dontSendNotification);
    bundle.enabledLabel.setJustificationType(juce::Justification::centredLeft);
    bundle.enabledLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.enabledLabel.setFont(juce::FontOptions(11.5f));
    bundle.enabledLabel.setInterceptsMouseClicks(false, false);

    bundle.assignLabel.setText("Assign", juce::dontSendNotification);
    bundle.assignLabel.setJustificationType(juce::Justification::centred);
    bundle.assignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignLabel.setFont(juce::FontOptions(11.5f));
    bundle.assignLabel.setInterceptsMouseClicks(true, false);
    bundle.assignLabel.setTooltip("Envelope Assignment");

    bundle.enabledButton.setButtonText("");
    bundle.enabledButton.setClickingTogglesState(true);
    bundle.enabledButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
    bundle.enabledButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));

    bundle.assignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    bundle.assignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    bundle.amountLabel.setText("AMOUNT", juce::dontSendNotification);
    bundle.amountLabel.setJustificationType(juce::Justification::centred);
    bundle.amountLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.amountLabel.setFont(juce::FontOptions(11.0f));
    bundle.amountLabel.setInterceptsMouseClicks(false, false);
    bundle.amountLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    bundle.amountValueLabel.setJustificationType(juce::Justification::centred);
    bundle.amountValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    bundle.amountValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    bundle.amountValueLabel.setFont(juce::FontOptions(11.0f));
    bundle.amountValueLabel.setInterceptsMouseClicks(false, false);

    bundle.amountKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bundle.amountKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bundle.amountKnob.setRange(-1.0, 1.0, 0.0);
    if (lfoKnobLookAndFeel != nullptr)
    {
        bundle.amountKnob.setLookAndFeel(lfoKnobLookAndFeel);
    }

    const auto& assignments = processor.getEnvelopeAssignmentDisplayNames();
    for (int i = 0; i < assignments.size(); ++i)
    {
        bundle.assignBox.addItem(assignments[i], i + 1);
    }
    bundle.assignBox.setSelectedId(processor.getEnvelopeAssignmentIndex(envIndex) + 1, juce::dontSendNotification);

    bundle.assignBox.onChange = [this, envIndex, &bundle]()
    {
        const auto selected = juce::jmax(0, bundle.assignBox.getSelectedId() - 1);
        processor.setEnvelopeAssignmentIndex(envIndex, selected);
    };

    bundle.amountKnob.onValueChange = [&bundle]()
    {
        const auto amount = juce::jlimit(-1.0f, 1.0f, static_cast<float>(bundle.amountKnob.getValue()));
        const auto amountPercent = static_cast<int>(std::lround(amount * 100.0f));
        const auto prefix = amountPercent > 0 ? juce::String("+") : juce::String();
        bundle.amountValueLabel.setText(prefix + juce::String(amountPercent) + "%", juce::dontSendNotification);
    };

    bundle.enabledAttachment = std::make_unique<juce::ButtonParameterAttachment>(processor.getEnvelopeEnabledParam(envIndex), bundle.enabledButton, nullptr);
    bundle.amountAttachment = std::make_unique<juce::SliderParameterAttachment>(processor.getEnvelopeAmountParam(envIndex), bundle.amountKnob, nullptr);

    bundle.component = std::make_unique<EnvelopeComponent>(processor.getEnvelopeAttackParam(envIndex),
                                                           processor.getEnvelopeDecayParam(envIndex),
                                                           processor.getEnvelopeSustainParam(envIndex),
                                                           processor.getEnvelopeReleaseParam(envIndex),
                                                           processor.getEnvelopeEnabledParam(envIndex),
                                                           bundle.enabledButton,
                                                           bundle.enabledLabel,
                                                           bundle.assignLabel,
                                                           bundle.assignBox,
                                                           &bundle.amountKnob,
                                                           &bundle.amountLabel,
                                                           &bundle.amountValueLabel,
                                                           accent,
                                                           "mod.env" + juce::String(envIndex + 1));
}

void ModPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("mod.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("mod.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto panelRadius = uiConfig != nullptr ? uiConfig->getFloat("mod.panel.cornerRadius", 10.0f) : 10.0f;
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);

}

void ModPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (uiConfig != nullptr)
    {
        const auto comboStyle = uiConfig->getObject("styles.combos.default");
        for (auto& bundle : extraLfos)
        {
            uiConfig->applyComboStyle(comboStyle, bundle.assignBox);
            uiConfig->applyComboStyle(comboStyle, bundle.waveformBox);
        }
        for (auto& bundle : envelopes)
        {
            uiConfig->applyComboStyle(comboStyle, bundle.assignBox);
        }
    }

    if (lfoComponent != nullptr)
    {
        lfoComponent->setUIConfig(uiConfig);
    }

    for (auto& bundle : extraLfos)
    {
        if (bundle.component != nullptr)
        {
            bundle.component->setUIConfig(uiConfig);
        }
    }

    for (auto& bundle : envelopes)
    {
        if (bundle.component != nullptr)
        {
            bundle.component->setUIConfig(uiConfig);
        }
    }

    repaint();
}

void ModPanel::resized()
{
    if (lfoComponent == nullptr)
    {
        return;
    }

    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padY", 10) : 10;
    auto panelArea = getLocalBounds().reduced(panelPadX, panelPadY);

    const auto colGap = uiConfig != nullptr ? uiConfig->getInt("mod.grid.colGap", 8) : 8;
    const auto rowGap = uiConfig != nullptr ? uiConfig->getInt("mod.grid.rowGap", 10) : 10;
    const auto minColWidth = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minColWidth", 280) : 280;
    const auto minLfoHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minLfoHeight", 300) : 300;
    const auto minEnvHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minEnvHeight", 280) : 280;

    const auto colWidth = juce::jmax(minColWidth, juce::jmax(1, (panelArea.getWidth() - (2 * colGap)) / 3));
    const auto lfoRowHeight = juce::jmax(minLfoHeight, juce::jmax(1, (panelArea.getHeight() - rowGap) / 2));
    const auto envRowHeight = juce::jmax(minEnvHeight, juce::jmax(1, panelArea.getHeight() - rowGap - lfoRowHeight));
    const auto totalGridWidth = colWidth * 3 + colGap * 2;
    const auto gridX = panelArea.getX() + juce::jmax(0, (panelArea.getWidth() - totalGridWidth) / 2);

    auto lfoRow = juce::Rectangle<int>(gridX, panelArea.getY(), totalGridWidth, lfoRowHeight);
    auto envRow = juce::Rectangle<int>(gridX, lfoRow.getBottom() + rowGap, totalGridWidth, envRowHeight);

    std::array<juce::Rectangle<int>, 3> lfoCells {};
    std::array<juce::Rectangle<int>, 3> envCells {};
    for (int i = 0; i < 3; ++i)
    {
        const auto x = gridX + i * (colWidth + colGap);
        lfoCells[static_cast<std::size_t>(i)] = juce::Rectangle<int>(x, lfoRow.getY(), colWidth, lfoRow.getHeight());
        envCells[static_cast<std::size_t>(i)] = juce::Rectangle<int>(x, envRow.getY(), colWidth, envRow.getHeight());
    }

    lfoComponent->setBounds(lfoCells[0].reduced(2, 2));
    extraLfos[0].component->setBounds(lfoCells[1].reduced(2, 2));
    extraLfos[1].component->setBounds(lfoCells[2].reduced(2, 2));

    for (int i = 0; i < static_cast<int>(envelopes.size()); ++i)
    {
        envelopes[static_cast<std::size_t>(i)].component->setBounds(envCells[static_cast<std::size_t>(i)].reduced(2, 2));
    }
}

int ModPanel::getPreferredContentWidth() const
{
    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padX", 12) : 12;
    const auto colGap = uiConfig != nullptr ? uiConfig->getInt("mod.grid.colGap", 8) : 8;
    const auto minColWidth = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minColWidth", 280) : 280;
    return panelPadX * 2 + minColWidth * 3 + colGap * 2;
}

int ModPanel::getPreferredContentHeight() const
{
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padY", 10) : 10;
    const auto rowGap = uiConfig != nullptr ? uiConfig->getInt("mod.grid.rowGap", 10) : 10;
    const auto minLfoHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minLfoHeight", 300) : 300;
    const auto minEnvHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minEnvHeight", 280) : 280;
    return panelPadY * 2 + rowGap + minLfoHeight + minEnvHeight;
}

void ModPanel::refreshFromParameters()
{
    for (int i = 0; i < static_cast<int>(envelopes.size()); ++i)
    {
        const auto envIndex = i;
        auto& bundle = envelopes[static_cast<std::size_t>(i)];
        if (bundle.component != nullptr)
        {
            bundle.assignBox.setSelectedId(processor.getEnvelopeAssignmentIndex(envIndex) + 1, juce::dontSendNotification);
            bundle.component->refreshFromParameters();
        }
    }
}

void ModPanel::refreshLfoFromParameters(bool enabled, float rateHz, int waveformIndex)
{
    juce::ignoreUnused(enabled, rateHz, waveformIndex);

    if (lfoComponent != nullptr)
    {
        lfoComponent->refreshFromParameters(processor.getLfoEnabledParam(0).get(),
                                            processor.getLfoFrequencyParam(0).get(),
                                            processor.getLfoAmountParam(0).get(),
                                            processor.getLfoWaveformParam(0).getIndex());
    }

    for (int i = 0; i < static_cast<int>(extraLfos.size()); ++i)
    {
        const auto lfoIndex = i + 1;
        auto& bundle = extraLfos[static_cast<std::size_t>(i)];
        if (bundle.component != nullptr)
        {
            bundle.assignBox.setSelectedId(processor.getLfoAssignmentIndex(lfoIndex) + 1, juce::dontSendNotification);
            bundle.component->refreshFromParameters(processor.getLfoEnabledParam(lfoIndex).get(),
                                                    processor.getLfoFrequencyParam(lfoIndex).get(),
                                                    processor.getLfoAmountParam(lfoIndex).get(),
                                                    processor.getLfoWaveformParam(lfoIndex).getIndex());
        }
    }
}

void ModPanel::advanceAnimation(float lfoDeltaSeconds)
{
    if (lfoComponent != nullptr)
    {
        lfoComponent->advanceAnimation(lfoDeltaSeconds);
    }

    for (auto& bundle : extraLfos)
    {
        if (bundle.component != nullptr)
        {
            bundle.component->advanceAnimation(lfoDeltaSeconds);
        }
    }
}