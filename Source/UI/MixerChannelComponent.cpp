#include "MixerChannelComponent.h"

#include "UIConfig.h"

MixerChannelComponent::MixerChannelComponent(Controls controlsIn)
    : controls(std::move(controlsIn))
{
}

void MixerChannelComponent::setCardStyleKey(juce::String key)
{
    if (cardStyleKey != key)
    {
        cardStyleKey = std::move(key);
        refreshCardStyle();
        repaint();
    }
}

void MixerChannelComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    refreshCardStyle();
    resized();
    repaint();
}

void MixerChannelComponent::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    card.setPanelContentBounds(panelContent);
    repaint();
}

void MixerChannelComponent::refreshCardStyle()
{
    card.setStyleKey(cardStyleKey);
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());
}

void MixerChannelComponent::paint(juce::Graphics& g)
{
    // Frame only. The channel's title is still its own Label, so no title is
    // passed here - two titles would be worse than none.
    card.draw(g, {});
}

void MixerChannelComponent::resized()
{
    refreshCardStyle();

    auto area = getLocalBounds();

    if (controls.title != nullptr)
    {
        controls.title->setBounds(area.removeFromTop(18));
        area.removeFromTop(sectionSpacing);
    }

    if (controls.stereoTag != nullptr && controls.stereoTag->isVisible())
    {
        controls.stereoTag->setBounds(area.removeFromTop(12));
        area.removeFromTop(sectionSpacing);
    }

    if (controls.meter != nullptr && controls.meter->isVisible())
    {
        controls.meter->setBounds(area.removeFromTop(meterHeight));
        area.removeFromTop(sectionSpacing);
    }

    if (controls.mute != nullptr && controls.solo != nullptr)
    {
        auto buttonsRow = area.removeFromTop(18);
        const auto buttonWidth = (buttonsRow.getWidth() - buttonGap) / 2;
        controls.mute->setBounds(buttonsRow.removeFromLeft(buttonWidth));
        buttonsRow.removeFromLeft(buttonGap);
        controls.solo->setBounds(buttonsRow);
        area.removeFromTop(sectionSpacing);
    }

    if (controls.fader != nullptr)
    {
        const auto valueHeight = (controls.valueLabel != nullptr && controls.valueLabel->isVisible()) ? footerLabelHeight : 0;
        const auto faderHeight = juce::jmax(70, area.getHeight() - valueHeight - 2 * (footerLabelHeight + sectionSpacing + 22));
        controls.fader->setBounds(area.removeFromTop(faderHeight));
        area.removeFromTop(sectionSpacing);
    }

    if (controls.valueLabel != nullptr && controls.valueLabel->isVisible())
    {
        controls.valueLabel->setBounds(area.removeFromTop(footerLabelHeight));
        area.removeFromTop(sectionSpacing);
    }

    const int knobSize = 46;
    const int knobGap = buttonGap;
    const int labelSlotWidth = juce::jmax(knobSize, 36);

    if (controls.hasSend && controls.pan != nullptr && controls.send != nullptr)
    {
        if (controls.panLabel != nullptr || controls.sendLabel != nullptr)
        {
            auto labelsRow = area.removeFromTop(footerLabelHeight);
            const auto labelsWidth = 2 * labelSlotWidth + knobGap;
            auto centeredLabels = labelsRow.withSizeKeepingCentre(labelsWidth, footerLabelHeight);

            if (controls.panLabel != nullptr)
            {
                controls.panLabel->setBounds(centeredLabels.removeFromLeft(labelSlotWidth));
            }

            centeredLabels.removeFromLeft(knobGap);

            if (controls.sendLabel != nullptr)
            {
                controls.sendLabel->setBounds(centeredLabels.removeFromLeft(labelSlotWidth));
            }

            area.removeFromTop(sectionSpacing);
        }

        auto knobsRow = area.removeFromTop(knobSize);
        const auto knobsWidth = 2 * labelSlotWidth + knobGap;
        auto centeredKnobs = knobsRow.withSizeKeepingCentre(knobsWidth, knobSize);
        auto panSlot = centeredKnobs.removeFromLeft(labelSlotWidth);
        controls.pan->setBounds(panSlot.withSizeKeepingCentre(knobSize, knobSize));
        centeredKnobs.removeFromLeft(knobGap);
        auto sendSlot = centeredKnobs.removeFromLeft(labelSlotWidth);
        controls.send->setBounds(sendSlot.withSizeKeepingCentre(knobSize, knobSize));
    }
    else
    {
        if (controls.panLabel != nullptr)
        {
            auto panLabelRow = area.removeFromTop(footerLabelHeight);
            controls.panLabel->setBounds(panLabelRow.withSizeKeepingCentre(labelSlotWidth, footerLabelHeight));
            area.removeFromTop(sectionSpacing);
        }

        if (controls.pan != nullptr)
        {
            auto panRow = area.removeFromTop(knobSize);
            controls.pan->setBounds(panRow.withSizeKeepingCentre(knobSize, knobSize));
        }

        if (controls.sendLabel != nullptr)
        {
            controls.sendLabel->setBounds({});
        }

        if (controls.send != nullptr)
        {
            controls.send->setBounds({});
        }
    }
}
