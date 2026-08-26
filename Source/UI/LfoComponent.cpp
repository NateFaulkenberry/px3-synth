#include "LfoComponent.h"

#include "ComponentCardDrawing.h"
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
                                     juce::Slider& amountKnobIn,
                                     juce::Label& amountLabelIn,
                                     juce::Label& amountValueLabelIn,
                                                     juce::ComboBox& waveformBoxIn,
                                                     juce::Label& waveformLabelIn,
                                                     juce::Colour accentIn,
                                                     const juce::String& configPrefixIn)
        : enabledButton(enabledButtonIn),
            enabledLabel(enabledLabelIn),
            rateKnob(rateKnobIn),
      rateLabel(rateLabelIn),
      rateValueLabel(rateValueLabelIn),
    amountKnob(amountKnobIn),
    amountLabel(amountLabelIn),
    amountValueLabel(amountValueLabelIn),
      assignLabel(assignLabelIn),
      assignBox(assignBoxIn),
      waveformBox(waveformBoxIn),
      waveformLabel(waveformLabelIn),
    accent(accentIn),
    configPrefix(configPrefixIn)
{
        addAndMakeVisible(enabledButton);
        addAndMakeVisible(enabledLabel);
    baseRateValueTextColour = rateValueLabel.findColour(juce::Label::textColourId);
    baseAmountValueTextColour = amountValueLabel.findColour(juce::Label::textColourId);
    addAndMakeVisible(rateKnob);
    addAndMakeVisible(rateLabel);
    addAndMakeVisible(rateValueLabel);
    addAndMakeVisible(amountKnob);
    addAndMakeVisible(amountLabel);
    addAndMakeVisible(amountValueLabel);
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

    const auto pref = configPrefix + ".visual.";

    const auto textColour = uiConfig != nullptr ? uiConfig->getColour(pref + "onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat(pref + "onLabel.fontSize", 11.5f) : 11.5f;
    const auto text = uiConfig != nullptr ? uiConfig->getString(pref + "onLabel.text", "ON") : juce::String("ON");
    enabledLabel.setText(text, juce::dontSendNotification);
    enabledLabel.setColour(juce::Label::textColourId, textColour);
    enabledLabel.setFont(juce::FontOptions(fontSize));

    repaint();
}

void LfoComponent::refreshFromParameters(bool enabled, float rateHz, float amount, int waveformIndex)
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
    rateKnob.getProperties().set("knobBypassed", !currentEnabled);
    rateKnob.getProperties().set("psychedelicBypassGray", !currentEnabled);
    amountKnob.setEnabled(currentEnabled);
    amountKnob.setInterceptsMouseClicks(currentEnabled, currentEnabled);
    amountKnob.getProperties().set("knobBypassed", !currentEnabled);
    amountKnob.getProperties().set("psychedelicBypassGray", !currentEnabled);
    rateLabel.setEnabled(currentEnabled);
    rateValueLabel.setEnabled(currentEnabled);
    amountLabel.setEnabled(currentEnabled);
    amountValueLabel.setEnabled(currentEnabled);
    const auto disabledRateValueColour = juce::Colour::fromRGB(178, 178, 178);
    const auto disabledAmountValueColour = juce::Colour::fromRGB(178, 178, 178);
    rateValueLabel.setColour(juce::Label::textColourId,
                             currentEnabled ? baseRateValueTextColour : disabledRateValueColour);
    amountValueLabel.setColour(juce::Label::textColourId,
                               currentEnabled ? baseAmountValueTextColour : disabledAmountValueColour);

    currentRateHz = juce::jlimit(0.01f, 20.0f, rateHz);
    rateValueLabel.setText(juce::String(currentRateHz, 2) + " Hz", juce::dontSendNotification);
    currentAmount = juce::jlimit(-1.0f, 1.0f, amount);
    const auto amountPercent = static_cast<int>(std::lround(currentAmount * 100.0f));
    const auto amountPrefix = amountPercent > 0 ? juce::String("+") : juce::String();
    amountValueLabel.setText(amountPrefix + juce::String(amountPercent) + "%", juce::dontSendNotification);

    const auto clamped = px3::clampLfoWaveformIndex(waveformIndex);
    currentWaveformIndex = clamped;
    if (waveformBox.getSelectedItemIndex() != clamped)
    {
        waveformBox.setSelectedItemIndex(clamped, juce::dontSendNotification);
    }

    if (enabledChanged)
    {
        rateKnob.repaint();
        amountKnob.repaint();
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
    const auto labelWidth = uiConfig != nullptr ? uiConfig->getInt(configPrefix + ".visual.onLabel.width", 52) : 52;
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

    auto amountBlock = area.removeFromBottom(44);
    auto amountValueRow = amountBlock.removeFromBottom(20);
    amountValueLabel.setBounds(amountValueRow.reduced(2, 0));
    auto amountLabelRow = amountBlock.removeFromTop(18);
    amountLabel.setBounds(amountLabelRow.reduced(2, 0));

    amountBlock.removeFromTop(4);
    const auto amountKnobSize = juce::jlimit(44, 84, juce::jmin(amountBlock.getWidth() - 16, amountBlock.getHeight()));
    amountKnob.setBounds(juce::Rectangle<int>(amountKnobSize, amountKnobSize).withCentre(amountBlock.getCentre()));

    area.removeFromBottom(6);

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
    const auto bgTintAlpha = uiConfig != nullptr ? uiConfig->getFloat(configPrefix + ".visual.bgTintAlpha", 0.10f) : 0.10f;
    const auto enabledBgTintColour = uiConfig != nullptr ? uiConfig->getColour(configPrefix + ".visual.bgTintColour", effectiveAccent)
                                                         : effectiveAccent;
    const auto topFillAlpha = uiConfig != nullptr ? uiConfig->getFloat(configPrefix + ".visual.topFillAlpha", 0.10f) : 0.10f;
    const auto enabledTopFillColour = uiConfig != nullptr ? uiConfig->getColour(configPrefix + ".visual.topFillColour", effectiveAccent)
                                                          : effectiveAccent;
    const auto bgTintColour = currentEnabled ? enabledBgTintColour : juce::Colour::fromRGB(112, 112, 112);
    const auto topFillColour = currentEnabled ? enabledTopFillColour : juce::Colour::fromRGB(136, 136, 136);

    px3::ui::ComponentCardStyle cardStyle;
    cardStyle.borderPadding = 0.0f;
    cardStyle.cornerRadius = 8.0f;
    cardStyle.fillInset = 6.0f;
    cardStyle.backgroundColour = bgTintColour;
    cardStyle.backgroundAlpha = bgTintAlpha;
    cardStyle.topFillColour = topFillColour;
    cardStyle.topFillAlpha = topFillAlpha;
    cardStyle.topFillHeightRatio = 0.5f;
    cardStyle.drawOutline = true;
    cardStyle.outlineColour = juce::Colour::fromRGB(220, 232, 252);
    cardStyle.outlineAlpha = static_cast<float>(currentEnabled ? 88 : 66) / 255.0f;
    cardStyle.outlineThickness = 1.2f;
    px3::ui::drawComponentCard(g, cardBounds, cardStyle);

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
