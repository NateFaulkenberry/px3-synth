#include "MixerChannelComponent.h"

#include "UIConfig.h"

MixerChannelComponent::MixerChannelComponent(Controls controlsIn)
    : controls(std::move(controlsIn))
{
}

void MixerChannelComponent::applyLayoutFromConfig(const std::shared_ptr<const UIConfig>& config)
{
    if (config == nullptr)
    {
        return;
    }

    sectionSpacing = config->getInt("mix.channel.layout.sectionSpacing", sectionSpacing);
    buttonGap = config->getInt("mix.channel.layout.buttonGap", buttonGap);
    footerLabelHeight = config->getInt("mix.channel.layout.footerLabelHeight", footerLabelHeight);
    meterHeight = config->getInt("mix.channel.layout.meterHeight", meterHeight);
}

void MixerChannelComponent::resized()
{
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

    if (controls.panLabel != nullptr)
    {
        controls.panLabel->setBounds(area.removeFromTop(footerLabelHeight));
    }

    if (controls.pan != nullptr)
    {
        controls.pan->setBounds(area.removeFromTop(22));
        area.removeFromTop(sectionSpacing);
    }

    if (controls.hasSend)
    {
        if (controls.sendLabel != nullptr)
        {
            controls.sendLabel->setBounds(area.removeFromTop(footerLabelHeight));
        }

        if (controls.send != nullptr)
        {
            controls.send->setBounds(area.removeFromTop(22));
        }
    }
}
