#include "MixPanel.h"

#include "UIConfig.h"

#include <array>

MixPanel::MixPanel(juce::Slider& subOscGainFaderIn,
                                     juce::Label& subOscGainLabelIn,
                                     juce::Slider& osc1GainFaderIn,
                                     juce::Label& osc1GainLabelIn,
                                     juce::Slider& osc2GainFaderIn,
                                     juce::Label& osc2GainLabelIn,
                                     juce::Slider& osc3GainFaderIn,
                                     juce::Label& osc3GainLabelIn,
                                     juce::Colour panelAccent)
        : subOscGainFader(subOscGainFaderIn),
            subOscGainLabel(subOscGainLabelIn),
            osc1GainFader(osc1GainFaderIn),
            osc1GainLabel(osc1GainLabelIn),
            osc2GainFader(osc2GainFaderIn),
            osc2GainLabel(osc2GainLabelIn),
            osc3GainFader(osc3GainFaderIn),
            osc3GainLabel(osc3GainLabelIn),
            accent(panelAccent)
{
        configureFader(subOscGainFader);
        configureFader(osc1GainFader);
        configureFader(osc2GainFader);
        configureFader(osc3GainFader);

        configureLabel(subOscGainLabel, "Sub Osc");
        configureLabel(osc1GainLabel, "Osc 1");
        configureLabel(osc2GainLabel, "Osc 2");
        configureLabel(osc3GainLabel, "Osc 3");

        addAndMakeVisible(subOscGainFader);
        addAndMakeVisible(subOscGainLabel);
        addAndMakeVisible(osc1GainFader);
        addAndMakeVisible(osc1GainLabel);
        addAndMakeVisible(osc2GainFader);
        addAndMakeVisible(osc2GainLabel);
        addAndMakeVisible(osc3GainFader);
        addAndMakeVisible(osc3GainLabel);
}

void MixPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("mix.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("mix.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto panelRadius = uiConfig != nullptr ? uiConfig->getFloat("mix.panel.cornerRadius", 10.0f) : 10.0f;

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);

}

void MixPanel::resized()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("mix.panel.layout.padX", 14) : 14;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("mix.panel.layout.padY", 12) : 12;
    auto area = getLocalBounds().reduced(padX, padY);

    auto columnsArea = area;
    const auto labelHeight = 20;
    auto labelRow = columnsArea.removeFromBottom(labelHeight);
    columnsArea.removeFromBottom(8);

    constexpr int channelCount = 4;
    constexpr int gap = 12;
    const auto totalGap = gap * (channelCount - 1);
    const auto channelWidth = juce::jmax(28, (columnsArea.getWidth() - totalGap) / channelCount);

    std::array<juce::Slider*, channelCount> faders { { &subOscGainFader, &osc1GainFader, &osc2GainFader, &osc3GainFader } };
    std::array<juce::Label*, channelCount> labels { { &subOscGainLabel, &osc1GainLabel, &osc2GainLabel, &osc3GainLabel } };

    auto x = columnsArea.getX();
    for (int i = 0; i < channelCount; ++i)
    {
        auto channelBounds = juce::Rectangle<int>(x, columnsArea.getY(), channelWidth, columnsArea.getHeight());
        auto faderBounds = channelBounds.reduced(8, 2);

        faders[static_cast<std::size_t>(i)]->setBounds(faderBounds);
        labels[static_cast<std::size_t>(i)]->setBounds(juce::Rectangle<int>(x, labelRow.getY(), channelWidth, labelRow.getHeight()));
        x += channelWidth + gap;
    }
}

void MixPanel::refreshFromParameters()
{
    // Attachments are the source of truth for parameter-sync.
}

void MixPanel::advanceAnimation(float deltaPhase)
{
    juce::ignoreUnused(deltaPhase);
}

void MixPanel::configureFader(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange(0.0, 1.0, 0.0);
    slider.setScrollWheelEnabled(false);
    slider.setDoubleClickReturnValue(true, 1.0);
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGBA(26, 28, 32, 190));
    slider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGBA(130, 190, 255, 180));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(230, 236, 246));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGBA(255, 255, 255, 36));
}

void MixPanel::configureLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 226, 236));
    label.setFont(juce::FontOptions(11.5f));
    label.setInterceptsMouseClicks(false, false);
}

void MixPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    repaint();
}
