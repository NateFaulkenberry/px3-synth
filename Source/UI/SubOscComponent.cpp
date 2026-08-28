#include "SubOscComponent.h"

#include "SubOscMode.h"
#include "UIConfig.h"

#include <cmath>

SubOscComponent::SubOscComponent(juce::ToggleButton& enabledButtonIn,
                                               juce::Label& enabledLabelIn,
                                                                                             juce::Slider& pitchIn,
                                                                                             juce::Label& pitchLabelIn,
                                                                                             juce::Label& pitchValueLabelIn,
                                               juce::ComboBox& octaveBoxIn,
                                               juce::Label& octaveLabelIn,
                                               juce::ComboBox& waveformBoxIn,
                                               juce::Label& waveformLabelIn,
                                               juce::Colour accentIn)
    : enabledButton(enabledButtonIn),
      enabledLabel(enabledLabelIn),
            pitch(pitchIn),
            pitchLabel(pitchLabelIn),
        pitchValueLabel(pitchValueLabelIn),
      octaveBox(octaveBoxIn),
      octaveLabel(octaveLabelIn),
      waveformBox(waveformBoxIn),
      waveformLabel(waveformLabelIn),
      accent(accentIn)
{
    addAndMakeVisible(enabledButton);
    addAndMakeVisible(enabledLabel);
    addAndMakeVisible(pitch);
    addAndMakeVisible(pitchLabel);
    addAndMakeVisible(pitchValueLabel);
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

void SubOscComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    card.setConfig(uiConfig);
    resized();
    repaint();
}

void SubOscComponent::refreshFromParameters(bool enabled, int octaveIndex, int waveformIndex)
{
    const auto enabledChanged = (currentEnabled != enabled);
    const auto waveformChanged = (currentWaveformIndex != px3::clampSubOscWaveformIndex(waveformIndex));

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

    octaveBox.setEnabled(currentEnabled);
    octaveLabel.setEnabled(currentEnabled);
    waveformBox.setEnabled(currentEnabled);
    waveformLabel.setEnabled(currentEnabled);
    pitch.setEnabled(currentEnabled);
    pitchLabel.setEnabled(currentEnabled);
    pitchValueLabel.setEnabled(currentEnabled);

    if (enabledChanged || waveformChanged)
    {
        repaint();
    }
}

void SubOscComponent::advanceAnimation(float deltaPhase)
{
    if (!currentEnabled)
    {
        return;
    }

    visualPhase += deltaPhase;
    if (visualPhase >= juce::MathConstants<float>::twoPi)
    {
        visualPhase -= juce::MathConstants<float>::twoPi;
    }

    repaint();
}

void SubOscComponent::resized()
{
    // One place decides the card geometry, and paint() reads the same result.
    // Previously resized() and paint() each re-derived it from a hard-coded
    // 300px width, so any change had to be made in both or they drifted apart.
    card.setStyleKey("subOsc");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    auto area = card.contentBelowTitle();

    auto enabledRow = area.removeFromTop(24);
    enabledLabel.setBounds(enabledRow.removeFromLeft(56));
    enabledButton.setBounds(enabledRow.removeFromLeft(40).reduced(2, 2));

    area.removeFromTop(6);

    auto octaveRow = area.removeFromTop(24);
    octaveLabel.setBounds(octaveRow.removeFromLeft(56));
    octaveBox.setBounds(octaveRow.reduced(2, 1));

    area.removeFromTop(6);

    auto waveformRow = area.removeFromTop(24);
    waveformLabel.setBounds(waveformRow.removeFromLeft(56));
    waveformBox.setBounds(waveformRow.reduced(2, 1));

    area.removeFromTop(2);

    auto pitchLabelRow = area.removeFromTop(18);
    pitchLabel.setBounds(pitchLabelRow.withSizeKeepingCentre(58, 18));

    area.removeFromTop(2);

    auto pitchRow = area.removeFromTop(54);
    pitch.setBounds(pitchRow.withSizeKeepingCentre(50, 50));

    area.removeFromTop(2);

    auto pitchValueRow = area.removeFromTop(16);
    pitchValueLabel.setBounds(pitchValueRow.withSizeKeepingCentre(84, 16));

    area.removeFromTop(8);

    auto graphArea = area;
    const auto requestedGraphHeight = static_cast<int>(juce::jmax(80, getLocalBounds().reduced(20, 14).getHeight() - 220));
    const auto maxGraphHeight = juce::jmax(80, graphArea.getHeight() - 24);
    const auto graphHeight = juce::jmin(requestedGraphHeight, maxGraphHeight);

    graphArea.removeFromBottom(graphHeight);
    graphArea.removeFromBottom(10);
}

void SubOscComponent::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    card.setPanelContentBounds(panelContent);
    resized();
    repaint();
}

void SubOscComponent::paint(juce::Graphics& g)
{
    // The card owns its own frame and its own title. The title used to be drawn
    // by OscPanel using this component's bounds - a parent painting into a
    // child's area, which is the same ownership mistake that left stale
    // outlines behind when panels were swapped.
    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);

    // Enabled state is runtime, not style, so it modulates the parsed style
    // rather than living in the configuration.
    if (currentEnabled)
    {
        card.draw(g, "SUB OSC");
    }
    else
    {
        card.drawInactive(g, "SUB OSC");
    }

    auto graphLayout = card.content();
    graphLayout.removeFromTop(24.0f);
    graphLayout.removeFromTop(6.0f);
    graphLayout.removeFromTop(24.0f);
    graphLayout.removeFromTop(6.0f);
    graphLayout.removeFromTop(24.0f);
    graphLayout.removeFromTop(8.0f);
    graphLayout.removeFromTop(2.0f);
    graphLayout.removeFromTop(18.0f);
    graphLayout.removeFromTop(2.0f);
    graphLayout.removeFromTop(54.0f);
    graphLayout.removeFromTop(2.0f);
    graphLayout.removeFromTop(16.0f);
    graphLayout.removeFromTop(8.0f);

    const auto requestedGraphHeight = static_cast<float>(juce::jmax(80, getLocalBounds().reduced(20, 14).getHeight() - 220));
    const auto maxGraphHeight = juce::jmax(80.0f, graphLayout.getHeight() - 24.0f);
    const auto graphHeight = juce::jmin(requestedGraphHeight, maxGraphHeight);
    graphLayout.removeFromBottom(10.0f);
    auto graph = graphLayout.removeFromBottom(graphHeight).reduced(0.0f, 2.0f);

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
