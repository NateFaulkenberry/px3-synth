#include "ModPanel.h"

#include "UIConfig.h"

ModPanel::ModPanel(PX3SynthAudioProcessor& processorIn,
                   juce::AudioParameterFloat& attack,
                   juce::AudioParameterFloat& decay,
                   juce::AudioParameterFloat& sustain,
                   juce::AudioParameterFloat& release,
                   juce::AudioParameterBool& envEnabled,
                   juce::ToggleButton& envEnabledButton,
                   juce::Label& envEnabledLabel,
                   juce::Label& envAssignLabel,
                   juce::ComboBox& envAssignBox,
                   juce::ToggleButton& lfoEnabledButton,
                   juce::Label& lfoEnabledLabel,
                   juce::Label& lfoAssignLabel,
                   juce::ComboBox& lfoAssignBox,
                   juce::Slider& lfoRateKnob,
                   juce::Label& lfoRateLabel,
                   juce::Label& lfoRateValueLabel,
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
    envelopeGraph = std::make_unique<EnvelopeComponent>(attack,
                                                        decay,
                                                        sustain,
                                                        release,
                                                        envEnabled,
                                                        envEnabledButton,
                                                        envEnabledLabel,
                                                        envAssignLabel,
                                                        envAssignBox,
                                                        panelAccent);
    lfoComponent = std::make_unique<LfoComponent>(lfoEnabledButton,
                                                  lfoEnabledLabel,
                                                  lfoAssignLabel,
                                                  lfoAssignBox,
                                                  lfoRateKnob,
                                                  lfoRateLabel,
                                                  lfoRateValueLabel,
                                                  lfoWaveformBox,
                                                  lfoWaveformLabel,
                                                  lfoAccent);

    // Match additional LFO cards: no explicit "Freq" chip label under the knob.
    lfoRateLabel.setText("", juce::dontSendNotification);
    lfoRateLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    addAndMakeVisible(*envelopeGraph);
    addAndMakeVisible(*lfoComponent);

    for (int lfoIndex = 1; lfoIndex < PX3SynthAudioProcessor::kLfoSourceCount; ++lfoIndex)
    {
        auto& bundle = extraLfos[static_cast<std::size_t>(lfoIndex - 1)];
        configureOwnedLfoBundle(lfoIndex, bundle);
        addAndMakeVisible(*bundle.component);
    }

    for (int envIndex = 1; envIndex < PX3SynthAudioProcessor::kEnvelopeSourceCount; ++envIndex)
    {
        auto& bundle = extraEnvelopes[static_cast<std::size_t>(envIndex - 1)];
        configureOwnedEnvBundle(envIndex, bundle);
        addAndMakeVisible(*bundle.component);
    }
}

void ModPanel::configureOwnedLfoBundle(int lfoIndex, LfoBundle& bundle)
{
    const auto applyChipLabelStyle = [](juce::Label& label)
    {
        label.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 54));
        label.setColour(juce::Label::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 96));
    };

    bundle.enabledLabel.setText("ON", juce::dontSendNotification);
    bundle.enabledLabel.setJustificationType(juce::Justification::centredLeft);
    bundle.enabledLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.enabledLabel.setFont(juce::FontOptions(11.5f));
    bundle.enabledLabel.setInterceptsMouseClicks(false, false);
    applyChipLabelStyle(bundle.enabledLabel);

    bundle.assignLabel.setText("Assign", juce::dontSendNotification);
    bundle.assignLabel.setJustificationType(juce::Justification::centred);
    bundle.assignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignLabel.setFont(juce::FontOptions(11.5f));
    bundle.assignLabel.setInterceptsMouseClicks(true, false);
    bundle.assignLabel.setTooltip("LFO Assignment");
    applyChipLabelStyle(bundle.assignLabel);

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
    applyChipLabelStyle(bundle.waveformLabel);

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

    bundle.enabledAttachment = std::make_unique<juce::ButtonParameterAttachment>(processor.getLfoEnabledParam(lfoIndex), bundle.enabledButton, nullptr);
    bundle.rateAttachment = std::make_unique<juce::SliderParameterAttachment>(processor.getLfoFrequencyParam(lfoIndex), bundle.rateKnob, nullptr);
    bundle.waveformAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(processor.getLfoWaveformParam(lfoIndex), bundle.waveformBox, nullptr);

    bundle.component = std::make_unique<LfoComponent>(bundle.enabledButton,
                                                      bundle.enabledLabel,
                                                      bundle.assignLabel,
                                                      bundle.assignBox,
                                                      bundle.rateKnob,
                                                      bundle.rateLabel,
                                                      bundle.rateValueLabel,
                                                      bundle.waveformBox,
                                                      bundle.waveformLabel,
                                                      lfoHeaderAccent,
                                                      "mod.lfo" + juce::String(lfoIndex + 1));
}

void ModPanel::configureOwnedEnvBundle(int envIndex, EnvBundle& bundle)
{
    const auto applyChipLabelStyle = [](juce::Label& label)
    {
        label.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 54));
        label.setColour(juce::Label::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 96));
    };

    bundle.enabledLabel.setText("ON", juce::dontSendNotification);
    bundle.enabledLabel.setJustificationType(juce::Justification::centredLeft);
    bundle.enabledLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.enabledLabel.setFont(juce::FontOptions(11.5f));
    bundle.enabledLabel.setInterceptsMouseClicks(false, false);
    applyChipLabelStyle(bundle.enabledLabel);

    bundle.assignLabel.setText("Assign", juce::dontSendNotification);
    bundle.assignLabel.setJustificationType(juce::Justification::centred);
    bundle.assignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignLabel.setFont(juce::FontOptions(11.5f));
    bundle.assignLabel.setInterceptsMouseClicks(true, false);
    bundle.assignLabel.setTooltip("Envelope Assignment");
    applyChipLabelStyle(bundle.assignLabel);

    bundle.enabledButton.setButtonText("");
    bundle.enabledButton.setClickingTogglesState(true);
    bundle.enabledButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
    bundle.enabledButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));

    bundle.assignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    bundle.assignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

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

    bundle.enabledAttachment = std::make_unique<juce::ButtonParameterAttachment>(processor.getAmpEnvEnabledParam(envIndex), bundle.enabledButton, nullptr);

    bundle.component = std::make_unique<EnvelopeComponent>(processor.getAttackParam(envIndex),
                                                           processor.getDecayParam(envIndex),
                                                           processor.getSustainParam(envIndex),
                                                           processor.getReleaseParam(envIndex),
                                                           processor.getAmpEnvEnabledParam(envIndex),
                                                           bundle.enabledButton,
                                                           bundle.enabledLabel,
                                                           bundle.assignLabel,
                                                           bundle.assignBox,
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

void ModPanel::paintOverChildren(juce::Graphics& g)
{
    const auto cardTitleFontSize = uiConfig != nullptr ? uiConfig->getFloat("mod.panel.cardTitle.fontSize", 11.0f) : 11.0f;
    const auto drawCardTitle = [&g, cardTitleFontSize](const juce::String& text, juce::Rectangle<int> bounds, juce::Colour colour)
    {
        g.setColour(colour.brighter(0.2f));
        g.setFont(juce::FontOptions(cardTitleFontSize, juce::Font::bold));
        g.drawText(text, bounds.removeFromTop(14), juce::Justification::centredTop, true);
    };

    if (lfoComponent != nullptr)
    {
        const auto lfo1Title = uiConfig != nullptr ? uiConfig->getString("mod.lfo1.title.text", "LFO 1") : juce::String("LFO 1");
        drawCardTitle(lfo1Title, lfoComponent->getBounds(), lfoHeaderAccent);
    }
    for (int i = 0; i < static_cast<int>(extraLfos.size()); ++i)
    {
        const auto lfoTitle = uiConfig != nullptr
                                  ? uiConfig->getString("mod.lfo" + juce::String(i + 2) + ".title.text", "LFO " + juce::String(i + 2))
                                  : juce::String("LFO ") + juce::String(i + 2);
        drawCardTitle(lfoTitle, extraLfos[static_cast<std::size_t>(i)].component->getBounds(), lfoHeaderAccent);
    }

    if (envelopeGraph != nullptr)
    {
        const auto env1Title = uiConfig != nullptr ? uiConfig->getString("mod.env1.title.text", "ENV 1") : juce::String("ENV 1");
        drawCardTitle(env1Title, envelopeGraph->getBounds(), accent);
    }
    for (int i = 0; i < static_cast<int>(extraEnvelopes.size()); ++i)
    {
        const auto envTitle = uiConfig != nullptr
                                  ? uiConfig->getString("mod.env" + juce::String(i + 2) + ".title.text", "ENV " + juce::String(i + 2))
                                  : juce::String("ENV ") + juce::String(i + 2);
        drawCardTitle(envTitle, extraEnvelopes[static_cast<std::size_t>(i)].component->getBounds(), accent);
    }
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
        for (auto& bundle : extraEnvelopes)
        {
            uiConfig->applyComboStyle(comboStyle, bundle.assignBox);
        }
    }

    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setUIConfig(uiConfig);
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

    for (auto& bundle : extraEnvelopes)
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
    if (envelopeGraph == nullptr || lfoComponent == nullptr)
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
    const auto minEnvHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minEnvHeight", 220) : 220;

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

    envelopeGraph->setBounds(envCells[0].reduced(2, 2));
    lfoComponent->setBounds(lfoCells[0].reduced(2, 2));
    extraLfos[0].component->setBounds(lfoCells[1].reduced(2, 2));
    extraLfos[1].component->setBounds(lfoCells[2].reduced(2, 2));
    extraEnvelopes[0].component->setBounds(envCells[1].reduced(2, 2));
    extraEnvelopes[1].component->setBounds(envCells[2].reduced(2, 2));
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
    const auto minEnvHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minEnvHeight", 220) : 220;
    return panelPadY * 2 + rowGap + minLfoHeight + minEnvHeight;
}

void ModPanel::refreshFromParameters()
{
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->refreshFromParameters();
    }

    for (int i = 0; i < static_cast<int>(extraEnvelopes.size()); ++i)
    {
        const auto envIndex = i + 1;
        auto& bundle = extraEnvelopes[static_cast<std::size_t>(i)];
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