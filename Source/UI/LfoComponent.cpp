#include "LfoComponent.h"

#include "LfoMode.h"
#include "UIConfig.h"

#include <cmath>

juce::PopupMenu::Options LfoComponent::WaveformComboLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                                                  juce::Label& label)
{
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label);
    return options.withParentComponent(box.getParentComponent())
                  .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards);
}

LfoComponent::LfoComponent(juce::ToggleButton& enabledButtonIn,
                                                     juce::Label& enabledLabelIn,
                                                     juce::Label& assignLabelIn,
                                                     juce::ComboBox& assignBoxIn,
                                                     juce::Slider& rateKnobIn,
                                                     juce::Label& rateLabelIn,
                                                     juce::Label& rateValueLabelIn,
                                                     juce::ComboBox& waveformBoxIn,
                                                     juce::Label& waveformLabelIn,
                                                     juce::Colour accentIn)
        : enabledButton(enabledButtonIn),
            enabledLabel(enabledLabelIn),
            rateKnob(rateKnobIn),
      rateLabel(rateLabelIn),
      rateValueLabel(rateValueLabelIn),
      assignLabel(assignLabelIn),
      assignBox(assignBoxIn),
      waveformBox(waveformBoxIn),
      waveformLabel(waveformLabelIn),
      accent(accentIn)
{
        addAndMakeVisible(enabledButton);
        addAndMakeVisible(enabledLabel);
    baseRateKnobFillColour = rateKnob.findColour(juce::Slider::rotarySliderFillColourId);
    baseRateValueTextColour = rateValueLabel.findColour(juce::Label::textColourId);
    addAndMakeVisible(rateKnob);
    addAndMakeVisible(rateLabel);
    addAndMakeVisible(rateValueLabel);
    assignBox.setLookAndFeel(&waveformComboLookAndFeel);
    addAndMakeVisible(assignLabel);
    addAndMakeVisible(assignBox);
    waveformBox.setLookAndFeel(&waveformComboLookAndFeel);
    addAndMakeVisible(waveformBox);
    addAndMakeVisible(waveformLabel);
}

LfoComponent::~LfoComponent()
{
    assignBox.setLookAndFeel(nullptr);
    waveformBox.setLookAndFeel(nullptr);
}

void LfoComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void LfoComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    const auto textColour = uiConfig != nullptr
                                ? uiConfig->getColour("mod.lfo.visual.onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat("mod.lfo.visual.onLabel.fontSize", 11.5f) : 11.5f;
    const auto text = uiConfig != nullptr ? uiConfig->getString("mod.lfo.visual.onLabel.text", "ON") : juce::String("ON");
    enabledLabel.setText(text, juce::dontSendNotification);
    enabledLabel.setColour(juce::Label::textColourId, textColour);
    enabledLabel.setFont(juce::FontOptions(fontSize));

    repaint();
}

void LfoComponent::refreshFromParameters(bool enabled, float rateHz, int waveformIndex)
{
    const auto enabledChanged = currentEnabled != enabled;
    currentEnabled = enabled;

    enabledButton.setToggleState(currentEnabled, juce::dontSendNotification);
    assignBox.setEnabled(currentEnabled);
    assignLabel.setEnabled(currentEnabled);
    waveformBox.setEnabled(currentEnabled);
    waveformLabel.setEnabled(currentEnabled);
    rateKnob.setEnabled(currentEnabled);
    rateKnob.setInterceptsMouseClicks(currentEnabled, currentEnabled);
    rateKnob.getProperties().set("psychedelicBypassGray", !currentEnabled);
    const auto disabledKnobFill = juce::Colour::fromRGB(158, 158, 158);
    rateKnob.setColour(juce::Slider::rotarySliderFillColourId, currentEnabled ? baseRateKnobFillColour : disabledKnobFill);
    rateLabel.setEnabled(currentEnabled);
    rateValueLabel.setEnabled(currentEnabled);
    const auto disabledRateValueColour = juce::Colour::fromRGB(178, 178, 178);
    rateValueLabel.setColour(juce::Label::textColourId,
                             currentEnabled ? baseRateValueTextColour : disabledRateValueColour);

    currentRateHz = juce::jlimit(0.01f, 20.0f, rateHz);
    rateValueLabel.setText(juce::String(currentRateHz, 2) + " Hz", juce::dontSendNotification);

    const auto clamped = px3::clampLfoWaveformIndex(waveformIndex);
    currentWaveformIndex = clamped;
    if (waveformBox.getSelectedItemIndex() != clamped)
    {
        waveformBox.setSelectedItemIndex(clamped, juce::dontSendNotification);
    }

    if (enabledChanged)
    {
        repaint();
    }
}

void LfoComponent::advanceAnimation(float deltaSeconds)
{
    if (!currentEnabled)
    {
        return;
    }

    const auto clampedDeltaSeconds = juce::jlimit(1.0f / 120.0f, 0.2f, deltaSeconds);
    const auto phaseAdvance = juce::MathConstants<float>::twoPi * currentRateHz * clampedDeltaSeconds;
    visualPhase = std::fmod(visualPhase + phaseAdvance, juce::MathConstants<float>::twoPi);

    repaint();
}

void LfoComponent::resized()
{
    auto cardArea = getLocalBounds().reduced(6, 6);
    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, cardArea.getWidth());
    cardArea = cardArea.withSizeKeepingCentre(cardWidth, cardArea.getHeight());
    auto area = cardArea.reduced(10, 10);

    auto enabledRow = area.removeFromTop(24);
    const auto labelWidth = uiConfig != nullptr
                                ? uiConfig->getInt("mod.lfo.visual.onLabel.width",
                                                   uiConfig->getInt("mod.lfo.visual.onLabel.bounds.width", 52))
                                : 52;
    enabledLabel.setBounds(enabledRow.removeFromLeft(labelWidth));
    enabledButton.setBounds(enabledRow.removeFromLeft(40).reduced(2, 2));

    area.removeFromTop(6);

    auto assignRow = area.removeFromTop(24);
    assignLabel.setBounds(assignRow.removeFromLeft(52));
    assignBox.setBounds(assignRow.reduced(2, 1));

    area.removeFromTop(6);

    auto top = area.removeFromTop(24);
    waveformLabel.setBounds(top.removeFromLeft(78));
    waveformBox.setBounds(top.reduced(2, 1));

    area.removeFromTop(8);

    area.removeFromBottom(96);
    area.removeFromBottom(10);

    auto valueRow = area.removeFromBottom(20);
    rateValueLabel.setBounds(valueRow.reduced(2, 0));

    area.removeFromBottom(4);
    auto labelRow = area.removeFromTop(18);
    rateLabel.setBounds(labelRow.reduced(2, 0));

    area.removeFromTop(6);
    const auto knobSize = juce::jlimit(52, 110, juce::jmin(area.getWidth() - 16, area.getHeight()));
    rateKnob.setBounds(juce::Rectangle<int>(knobSize, knobSize).withCentre(area.getCentre()));
}

void LfoComponent::paint(juce::Graphics& g)
{
    auto card = getLocalBounds().reduced(6, 6);
    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, card.getWidth());
    card = card.withSizeKeepingCentre(cardWidth, card.getHeight());
    const auto cardBounds = card.toFloat();
    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);
    const auto bgTintAlpha = uiConfig != nullptr ? uiConfig->getFloat("mod.lfo.visual.bgTintAlpha", 0.10f) : 0.10f;
    const auto enabledBgTintColour = uiConfig != nullptr ? uiConfig->getColour("mod.lfo.visual.bgTintColour", effectiveAccent)
                                                         : effectiveAccent;
    const auto topFillAlpha = uiConfig != nullptr ? uiConfig->getFloat("mod.lfo.visual.topFillAlpha", 0.10f) : 0.10f;
    const auto enabledTopFillColour = uiConfig != nullptr ? uiConfig->getColour("mod.lfo.visual.topFillColour", effectiveAccent)
                                                          : effectiveAccent;
    const auto bgTintColour = currentEnabled ? enabledBgTintColour : juce::Colour::fromRGB(112, 112, 112);
    const auto topFillColour = currentEnabled ? enabledTopFillColour : juce::Colour::fromRGB(136, 136, 136);

    const auto innerFillBounds = cardBounds.reduced(6.0f);
    g.setColour(bgTintColour.withAlpha(bgTintAlpha));
    g.fillRoundedRectangle(innerFillBounds, 8.0f);
    g.setColour(topFillColour.withAlpha(topFillAlpha));
    juce::Path topFill;
    const auto topFillBounds = innerFillBounds;
    const auto topHalf = topFillBounds.withTrimmedBottom(topFillBounds.getHeight() * 0.5f);
    topFill.addRoundedRectangle(topHalf.getX(),
                                topHalf.getY(),
                                topHalf.getWidth(),
                                topHalf.getHeight(),
                                8.0f,
                                8.0f,
                                true,
                                true,
                                false,
                                false);
    g.fillPath(topFill);
    g.setColour(juce::Colour::fromRGBA(220, 232, 252, currentEnabled ? 88 : 66));
    g.drawRoundedRectangle(cardBounds, 8.0f, 1.2f);

    auto graphLayout = cardBounds.reduced(10.0f, 10.0f);
    graphLayout.removeFromTop(24.0f);
    graphLayout.removeFromTop(6.0f);
    graphLayout.removeFromTop(24.0f);
    graphLayout.removeFromTop(6.0f);
    graphLayout.removeFromTop(8.0f);
    graphLayout.removeFromBottom(10.0f);

    auto graph = graphLayout.removeFromBottom(96.0f).reduced(0.0f, 2.0f);

    if (graph.getWidth() < 40.0f || graph.getHeight() < 20.0f)
    {
        return;
    }

    g.setColour(juce::Colour::fromRGBA(14, 14, 18, 170));
    g.fillRoundedRectangle(graph, 7.0f);
    g.setColour(effectiveAccent.withAlpha(0.32f));
    g.drawRoundedRectangle(graph, 7.0f, 1.0f);

    const auto left = graph.getX() + 6.0f;
    const auto right = graph.getRight() - 6.0f;
    const auto top = graph.getY() + 5.0f;
    const auto bottom = graph.getBottom() - 5.0f;
    const auto mid = (top + bottom) * 0.5f;

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
    g.drawLine(left, mid, right, mid, 0.9f);
    for (int gx = 1; gx < 6; ++gx)
    {
        const auto x = left + (right - left) * (static_cast<float>(gx) / 6.0f);
        g.drawLine(x, top, x, bottom, 0.7f);
    }

    juce::Path wave;
    const auto width = juce::jmax(1.0f, right - left);
    const auto height = juce::jmax(1.0f, bottom - top);
    for (int s = 0; s <= 72; ++s)
    {
        const auto t = static_cast<float>(s) / 72.0f;
        const auto phaseNorm = std::fmod(t + visualPhase / juce::MathConstants<float>::twoPi, 1.0f);
        const auto y = waveformSample(phaseNorm, currentWaveformIndex);
        const auto xPos = left + t * width;
        const auto yPos = mid - juce::jlimit(-1.0f, 1.0f, y) * (height * 0.40f);

        if (s == 0)
        {
            wave.startNewSubPath(xPos, yPos);
        }
        else
        {
            wave.lineTo(xPos, yPos);
        }
    }

    g.setColour(effectiveAccent.withAlpha(currentEnabled ? 0.70f : 0.42f));
    g.strokePath(wave,
                 juce::PathStrokeType(2.6f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
    const auto waveDetailColour = currentEnabled ? juce::Colour::fromRGB(232, 240, 255)
                                                 : juce::Colour::fromRGB(178, 178, 178);
    g.setColour(waveDetailColour);
    g.strokePath(wave,
                 juce::PathStrokeType(1.2f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
}

float LfoComponent::waveformSample(float phaseNorm, int waveformIndex)
{
    const auto p = phaseNorm - std::floor(phaseNorm);

    switch (px3::clampLfoWaveformIndex(waveformIndex))
    {
        case 0:
            return std::sin(p * juce::MathConstants<float>::twoPi);
        case 1:
            return 1.0f - 4.0f * std::abs(p - 0.5f);
        case 2:
            return p * 2.0f - 1.0f;
        case 3:
            return p < 0.5f ? 1.0f : -1.0f;
        default:
            break;
    }

    return std::sin(p * juce::MathConstants<float>::twoPi);
}
