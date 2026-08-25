#include "ModPanel.h"

#include "UIConfig.h"

ModPanel::ModPanel(juce::AudioParameterFloat& attack,
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
                   juce::Colour panelAccent,
                   juce::Colour lfoAccent)
    : accent(panelAccent),
      lfoHeaderAccent(lfoAccent)
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

    addAndMakeVisible(*envelopeGraph);
    addAndMakeVisible(*lfoComponent);
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

    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padY", 10) : 10;
    auto cardArea = getLocalBounds().reduced(panelPadX, panelPadY);

    constexpr int gap = 8;
    const auto columnWidth = juce::jmax(1, (cardArea.getWidth() - gap) / 2);
    auto envCardArea = juce::Rectangle<int>(cardArea.getX(), cardArea.getY(), columnWidth, cardArea.getHeight());

    const auto envCardBounds = envCardArea.toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(0.10f));
    g.fillRoundedRectangle(envCardBounds, 8.0f);
    g.setColour(juce::Colour::fromRGBA(220, 232, 252, 88));
    g.drawRoundedRectangle(envCardBounds, 8.0f, 1.2f);

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
        drawCardTitle("LFO", lfoComponent->getBounds(), lfoHeaderAccent);
    }

    if (envelopeGraph != nullptr)
    {
        drawCardTitle("ENV", envelopeGraph->getBounds(), accent);
    }
}

void ModPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setUIConfig(uiConfig);
    }

    if (lfoComponent != nullptr)
    {
        lfoComponent->setUIConfig(uiConfig);
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

    constexpr int gap = 8;
    const auto columnWidth = juce::jmax(1, (panelArea.getWidth() - gap) / 2);
    auto envCardArea = juce::Rectangle<int>(panelArea.getX(), panelArea.getY(), columnWidth, panelArea.getHeight());
    auto lfoCardArea = juce::Rectangle<int>(envCardArea.getRight() + gap, panelArea.getY(), columnWidth, panelArea.getHeight());

    envelopeGraph->setBounds(envCardArea.reduced(10, 10));
    lfoComponent->setBounds(lfoCardArea.reduced(2, 2));
}

void ModPanel::refreshFromParameters()
{
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->refreshFromParameters();
    }
}

void ModPanel::refreshLfoFromParameters(bool enabled, float rateHz, int waveformIndex)
{
    if (lfoComponent != nullptr)
    {
        lfoComponent->refreshFromParameters(enabled, rateHz, waveformIndex);
    }
}

void ModPanel::advanceAnimation(float lfoDeltaSeconds)
{
    if (lfoComponent != nullptr)
    {
        lfoComponent->advanceAnimation(lfoDeltaSeconds);
    }
}