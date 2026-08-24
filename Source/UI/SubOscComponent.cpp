#include "SubOscComponent.h"

#include "SubOscMode.h"

#include <cmath>

SubOscComponent::SubOscComponent(juce::ToggleButton& enabledButtonIn,
                                               juce::Label& enabledLabelIn,
                                               juce::Slider& levelKnobIn,
                                               juce::Label& levelLabelIn,
                                               juce::ComboBox& octaveBoxIn,
                                               juce::Label& octaveLabelIn,
                                               juce::ComboBox& waveformBoxIn,
                                               juce::Label& waveformLabelIn,
                                               juce::Colour accentIn)
    : enabledButton(enabledButtonIn),
      enabledLabel(enabledLabelIn),
      levelKnob(levelKnobIn),
      levelLabel(levelLabelIn),
      octaveBox(octaveBoxIn),
      octaveLabel(octaveLabelIn),
      waveformBox(waveformBoxIn),
      waveformLabel(waveformLabelIn),
      accent(accentIn)
{
    addAndMakeVisible(enabledButton);
    addAndMakeVisible(enabledLabel);
    addAndMakeVisible(levelKnob);
    addAndMakeVisible(levelLabel);
    addAndMakeVisible(octaveBox);
    addAndMakeVisible(octaveLabel);
    addAndMakeVisible(waveformBox);
    addAndMakeVisible(waveformLabel);
}

void SubOscComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void SubOscComponent::refreshFromParameters(bool enabled, int octaveIndex, int waveformIndex)
{
    currentEnabled = enabled;
    currentWaveformIndex = px3::clampSubOscWaveformIndex(waveformIndex);

    enabledButton.setToggleState(enabled, juce::dontSendNotification);
    if (octaveBox.getSelectedItemIndex() != px3::clampSubOscOctaveIndex(octaveIndex))
    {
        octaveBox.setSelectedItemIndex(px3::clampSubOscOctaveIndex(octaveIndex), juce::dontSendNotification);
    }
    if (waveformBox.getSelectedItemIndex() != currentWaveformIndex)
    {
        waveformBox.setSelectedItemIndex(currentWaveformIndex, juce::dontSendNotification);
    }

    levelKnob.setEnabled(currentEnabled);
    levelLabel.setEnabled(currentEnabled);
    octaveBox.setEnabled(currentEnabled);
    octaveLabel.setEnabled(currentEnabled);
    waveformBox.setEnabled(currentEnabled);
    waveformLabel.setEnabled(currentEnabled);
}

void SubOscComponent::advanceAnimation(float deltaPhase)
{
    visualPhase += deltaPhase;
    if (visualPhase >= juce::MathConstants<float>::twoPi)
    {
        visualPhase -= juce::MathConstants<float>::twoPi;
    }

    repaint();
}

void SubOscComponent::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

    auto top = area.removeFromTop(24);
    enabledLabel.setBounds(top.removeFromLeft(58));
    enabledButton.setBounds(top.removeFromLeft(40));

    area.removeFromTop(6);
    auto selectors = area.removeFromTop(24);
    auto octaveCell = selectors.removeFromLeft(selectors.getWidth() / 2).reduced(0, 0);
    auto waveformCell = selectors;

    octaveLabel.setBounds(octaveCell.removeFromLeft(56));
    octaveBox.setBounds(octaveCell.reduced(1, 0));

    waveformLabel.setBounds(waveformCell.removeFromLeft(56));
    waveformBox.setBounds(waveformCell.reduced(1, 0));

    area.removeFromTop(8);
    const auto knobSize = juce::jlimit(54, 108, juce::jmin(area.getWidth() - 18, area.getHeight() - 18));
    const auto knobBounds = juce::Rectangle<int>(knobSize, knobSize)
                                .withCentre({ area.getCentreX(), area.getCentreY() + 8 });
    levelLabel.setBounds(juce::Rectangle<int>(knobBounds.getX(), knobBounds.getY() - 20, knobBounds.getWidth(), 18));
    levelKnob.setBounds(knobBounds);
}

void SubOscComponent::paint(juce::Graphics& g)
{
    auto graph = getLocalBounds().toFloat().reduced(12.0f, 10.0f);
    graph.removeFromTop(58.0f);
    graph.removeFromBottom(118.0f);

    if (graph.getWidth() < 40.0f || graph.getHeight() < 20.0f)
    {
        return;
    }

    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);

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

    juce::Path wave;
    const auto width = juce::jmax(1.0f, right - left);
    const auto height = juce::jmax(1.0f, bottom - top);
    for (int s = 0; s <= 64; ++s)
    {
        const auto t = static_cast<float>(s) / 64.0f;
        const auto phaseNorm = std::fmod(t + visualPhase / juce::MathConstants<float>::twoPi, 1.0f);
        const auto y = waveformSample(phaseNorm, currentWaveformIndex);
        const auto xPos = left + t * width;
        const auto yPos = mid - juce::jlimit(-1.0f, 1.0f, y) * (height * 0.42f);

        if (s == 0)
        {
            wave.startNewSubPath(xPos, yPos);
        }
        else
        {
            wave.lineTo(xPos, yPos);
        }
    }

    g.setColour(effectiveAccent.withAlpha(currentEnabled ? 0.72f : 0.42f));
    g.strokePath(wave,
                 juce::PathStrokeType(2.2f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
}

float SubOscComponent::waveformSample(float phaseNorm, int waveformIndex)
{
    const auto p = phaseNorm - std::floor(phaseNorm);

    switch (px3::clampSubOscWaveformIndex(waveformIndex))
    {
        case 0:
            return std::sin(p * juce::MathConstants<float>::twoPi);
        case 1:
            return p < 0.5f ? 1.0f : -1.0f;
        default:
            break;
    }

    return std::sin(p * juce::MathConstants<float>::twoPi);
}
