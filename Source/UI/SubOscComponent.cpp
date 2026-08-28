#include "SubOscComponent.h"

#include "CardInner.h"

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
    card.setStyleKey("subOsc");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    inner.setStylePath("cards.subOsc.cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(3);
    inner.layout(card.contentBelowTitle());

    // Row 1: bypass, octave and wave, each as a label-over-control pair. The
    // row's flex settings decide where they sit; this only says how big each
    // one wants to be, which is what keeps the controls looking as they did.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        flex.items.add(juce::FlexItem(46.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(76.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(76.0f, cellHeight).withMargin(gap));
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        using px3::ui::ControlShape;
        px3::ui::layoutLabelledControl(cell(0),
                                       { &enabledLabel, &enabledButton, nullptr,
                                         ControlShape::square, 14, 0, 22 },
                                       inner.rowControl(0));
        px3::ui::layoutLabelledControl(cell(1),
                                       { &octaveLabel, &octaveBox, nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       inner.rowControl(0));
        px3::ui::layoutLabelledControl(cell(2),
                                       { &waveformLabel, &waveformBox, nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       inner.rowControl(0));
    }

    // Row 2: the pitch knob, which keeps its existing label and value readout.
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);

        flex.items.add(juce::FlexItem(96.0f, static_cast<float>(juce::jmax(1, row.getHeight()))).withMargin(gap));
        flex.performLayout(row.toFloat());

        px3::ui::layoutLabelledControl(flex.items.getReference(0).currentBounds.toNearestInt(),
                                       { &pitchLabel, &pitch, &pitchValueLabel,
                                         px3::ui::ControlShape::square, 16, 16, 56 },
                                       inner.rowControl(1));
    }

    // Row 3 is the wave table, drawn in paint() rather than being a child
    // component, so it only needs its bounds - see waveTableBounds.
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

    // The wave table occupies row 3. It used to be found by replaying the same
    // removeFromTop sequence resized() used, which meant two copies of one
    // layout that had to be kept in step by hand.
    auto graph = inner.rowContent(2).toFloat();

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
