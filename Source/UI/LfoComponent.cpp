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

LfoComponent::LfoComponent(juce::Label& assignLabelIn,
                                         juce::ComboBox& assignBoxIn,
                                         juce::Slider& rateKnobIn,
                                         juce::Label& rateLabelIn,
                                         juce::Label& rateValueLabelIn,
                                         juce::ComboBox& waveformBoxIn,
                                         juce::Label& waveformLabelIn,
                                         juce::Colour accentIn)
    : rateKnob(rateKnobIn),
      rateLabel(rateLabelIn),
      rateValueLabel(rateValueLabelIn),
      assignLabel(assignLabelIn),
      assignBox(assignBoxIn),
      waveformBox(waveformBoxIn),
      waveformLabel(waveformLabelIn),
      accent(accentIn)
{
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
    repaint();
}

void LfoComponent::refreshFromParameters(float rateHz, int waveformIndex)
{
    rateValueLabel.setText(juce::String(juce::jlimit(0.01f, 20.0f, rateHz), 2) + " Hz", juce::dontSendNotification);

    const auto clamped = px3::clampLfoWaveformIndex(waveformIndex);
    currentWaveformIndex = clamped;
    if (waveformBox.getSelectedItemIndex() != clamped)
    {
        waveformBox.setSelectedItemIndex(clamped, juce::dontSendNotification);
    }
}

void LfoComponent::advanceAnimation(float deltaPhase)
{
    visualPhase += deltaPhase;
    if (visualPhase >= juce::MathConstants<float>::twoPi)
    {
        visualPhase -= juce::MathConstants<float>::twoPi;
    }

    repaint();
}

void LfoComponent::resized()
{
    auto cardArea = getLocalBounds().reduced(6, 6);
    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, cardArea.getWidth());
    cardArea = cardArea.withSizeKeepingCentre(cardWidth, cardArea.getHeight());
    auto area = cardArea.reduced(10, 10);

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
    const auto topFillAlpha = uiConfig != nullptr ? uiConfig->getFloat("osc.lfo.visual.topFillAlpha", 0.10f) : 0.10f;
    const auto topFillColour = uiConfig != nullptr ? uiConfig->getColour("osc.lfo.visual.topFillColour", accent)
                                                   : accent;

    g.setColour(accent.withAlpha(0.10f));
    g.fillRoundedRectangle(cardBounds, 8.0f);
    g.setColour(topFillColour.withAlpha(topFillAlpha));
    g.fillRoundedRectangle(cardBounds.withTrimmedBottom(cardBounds.getHeight() * 0.5f), 8.0f);
    g.setColour(juce::Colour::fromRGBA(220, 232, 252, 88));
    g.drawRoundedRectangle(cardBounds, 8.0f, 1.2f);

    auto graphLayout = cardBounds.reduced(10.0f, 10.0f);
    graphLayout.removeFromTop(24.0f);
    graphLayout.removeFromTop(8.0f);
    graphLayout.removeFromBottom(10.0f);

    auto graph = graphLayout.removeFromBottom(96.0f).reduced(0.0f, 2.0f);

    if (graph.getWidth() < 40.0f || graph.getHeight() < 20.0f)
    {
        return;
    }

    g.setColour(juce::Colour::fromRGBA(14, 14, 18, 170));
    g.fillRoundedRectangle(graph, 7.0f);
    g.setColour(accent.withAlpha(0.32f));
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

    g.setColour(accent.withAlpha(0.70f));
    g.strokePath(wave,
                 juce::PathStrokeType(2.6f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
    g.setColour(juce::Colour::fromRGB(232, 240, 255));
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
