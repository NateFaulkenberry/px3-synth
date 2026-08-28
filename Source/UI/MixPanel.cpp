#include "MixPanel.h"

#include "PluginProcessorInternals.h"

#include "PluginProcessor.h"
#include "UIConfig.h"

#include <array>
#include <cmath>

MixPanel::MixPanel(PX3SynthAudioProcessor& processorIn,
                   juce::LookAndFeel* knobLookAndFeelIn,
                   juce::Colour panelAccent)
        : processor(processorIn),
            knobLookAndFeel(knobLookAndFeelIn),
            channels { { &subChannel, &osc1Channel, &osc2Channel, &osc3Channel, &fxChannel } },
            accent(panelAccent)
{
        configureChannelWidgets(subChannel, "SUB", true, false);
        configureChannelWidgets(osc1Channel, "OSC 1", true, false);
        configureChannelWidgets(osc2Channel, "OSC 2", true, false);
        configureChannelWidgets(osc3Channel, "OSC 3", true, false);
        configureChannelWidgets(fxChannel, "FX", false, true);

        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerLevelParam(0), subChannel.fader, nullptr));
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerLevelParam(1), osc1Channel.fader, nullptr));
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerLevelParam(2), osc2Channel.fader, nullptr));
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerLevelParam(3), osc3Channel.fader, nullptr));
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getFxReturnGainParam(), fxChannel.fader, nullptr));

        for (int sourceIndex = 0; sourceIndex < PX3SynthAudioProcessor::kMixerSourceCount; ++sourceIndex)
        {
            auto* channel = channels[static_cast<std::size_t>(sourceIndex)];
            sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerPanParam(sourceIndex), channel->pan, nullptr));
            sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerSendParam(sourceIndex), channel->send, nullptr));
            buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getMixerMuteParam(sourceIndex), channel->mute, nullptr));
            buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getMixerSoloParam(sourceIndex), channel->solo, nullptr));
        }

        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getFxReturnPanParam(), fxChannel.pan, nullptr));
        buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getFxReturnMuteParam(), fxChannel.mute, nullptr));
        buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getFxReturnSoloParam(), fxChannel.solo, nullptr));

        applyConfigToChannels();
        startTimerHz(30);
}

MixPanel::~MixPanel() = default;

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

    auto gap = uiConfig != nullptr ? uiConfig->getInt("mix.channel.spacing", 10) : 10;
    const auto channelCount = static_cast<int>(channels.size());

    if (channelCount <= 0)
    {
        return;
    }

    if (area.getWidth() < 1)
    {
        return;
    }

    if (area.getWidth() <= gap * (channelCount - 1))
    {
        gap = 2;
    }

    const auto totalGap = gap * (channelCount - 1);
    const auto usableWidth = juce::jmax(channelCount, area.getWidth() - totalGap);
    const auto baseWidth = juce::jmax(48, usableWidth / channelCount);
    auto remainder = juce::jmax(0, usableWidth - baseWidth * channelCount);

    auto x = area.getX();
    for (auto* channel : channels)
    {
        if (channel == nullptr || channel->component == nullptr)
        {
            continue;
        }

        const auto extra = remainder > 0 ? 1 : 0;
        if (remainder > 0)
        {
            --remainder;
        }
        const auto width = baseWidth + extra;
        channel->component->setBounds(x, area.getY(), width, area.getHeight());
        x += width + gap;
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

void MixPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    applyConfigToChannels();
    resized();
    repaint();
}

void MixPanel::timerCallback()
{
    refreshMeterValues();
}

MixerToggleButton::Style MixPanel::buttonStyleFromConfig(const std::shared_ptr<const UIConfig>& uiConfig,
                                                          const juce::String& pathPrefix,
                                                          const MixerToggleButton::Style& fallback)
{
    auto style = fallback;
    if (uiConfig == nullptr)
    {
        return style;
    }

    style.width = uiConfig->getInt(pathPrefix + ".size.width", style.width);
    style.height = uiConfig->getInt(pathPrefix + ".size.height", style.height);
    style.cornerRadius = uiConfig->getFloat(pathPrefix + ".cornerRadius", style.cornerRadius);
    style.textSize = uiConfig->getFloat(pathPrefix + ".textSize", style.textSize);
    style.textColour = uiConfig->getColour(pathPrefix + ".textColour", style.textColour);
    style.normalColour = uiConfig->getColour(pathPrefix + ".normalColour", style.normalColour);
    style.hoverColour = uiConfig->getColour(pathPrefix + ".hoverColour", style.hoverColour);
    style.activeColour = uiConfig->getColour(pathPrefix + ".activeColour", style.activeColour);
    style.pressedColour = uiConfig->getColour(pathPrefix + ".pressedColour", style.pressedColour);
    style.disabledColour = uiConfig->getColour(pathPrefix + ".disabledColour", style.disabledColour);
    style.borderColour = uiConfig->getColour(pathPrefix + ".borderColour", style.borderColour);
    return style;
}

void MixPanel::configureChannelWidgets(ChannelWidgets& channel,
                                       const juce::String& titleText,
                                       bool hasSend,
                                       bool stereoTagVisible)
{
    channel.hasSend = hasSend;

    channel.title.setText(titleText, juce::dontSendNotification);
    channel.title.setJustificationType(juce::Justification::centred);
    channel.title.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 226, 236));
    channel.title.setFont(juce::FontOptions(11.5f));
    channel.title.setInterceptsMouseClicks(false, false);

    channel.stereoTag.setText("STEREO", juce::dontSendNotification);
    channel.stereoTag.setJustificationType(juce::Justification::centred);
    channel.stereoTag.setColour(juce::Label::textColourId, juce::Colour::fromRGBA(220, 226, 236, 170));
    channel.stereoTag.setFont(juce::FontOptions(9.5f));
    channel.stereoTag.setInterceptsMouseClicks(false, false);
    channel.stereoTag.setVisible(stereoTagVisible);

    channel.panLabel.setText("PAN", juce::dontSendNotification);
    channel.panLabel.setJustificationType(juce::Justification::centred);
    channel.panLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 226, 236));
    channel.panLabel.setFont(juce::FontOptions(9.5f));
    channel.panLabel.setInterceptsMouseClicks(false, false);

    channel.sendLabel.setText("SEND", juce::dontSendNotification);
    channel.sendLabel.setJustificationType(juce::Justification::centred);
    channel.sendLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 226, 236));
    channel.sendLabel.setFont(juce::FontOptions(9.5f));
    channel.sendLabel.setInterceptsMouseClicks(false, false);
    channel.sendLabel.setVisible(hasSend);

    channel.valueLabel.setText("0.0 dB", juce::dontSendNotification);
    channel.valueLabel.setJustificationType(juce::Justification::centred);
    channel.valueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGBA(220, 226, 236, 180));
    channel.valueLabel.setFont(juce::FontOptions(9.5f));
    channel.valueLabel.setInterceptsMouseClicks(false, false);

    // The sources are trimmed 4 dB at generation, so the fader runs to +4 dB
    // and a channel can still be driven to full scale. Double-click returns to
    // 0 dB, which is the default and reads as unity on the label.
    channel.fader.setRange(0.0, static_cast<double>(px3::processor_internal::channelFaderMaxGain()), 0.0);
    channel.fader.setDoubleClickReturnValue(true, 1.0);

    channel.pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    channel.pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    channel.pan.setRange(-1.0, 1.0, 0.0);
    channel.pan.setDoubleClickReturnValue(true, 0.0);
    channel.pan.getProperties().set("isMixerPanKnob", true);
    if (knobLookAndFeel != nullptr)
    {
        channel.pan.setLookAndFeel(knobLookAndFeel);
    }

    channel.send.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    channel.send.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    channel.send.setRange(0.0, 1.0, 0.0);
    channel.send.setDoubleClickReturnValue(true, 0.0);
    channel.send.setVisible(hasSend);
    if (knobLookAndFeel != nullptr)
    {
        channel.send.setLookAndFeel(knobLookAndFeel);
    }

    channel.component = std::make_unique<MixerChannelComponent>(
        MixerChannelComponent::Controls { &channel.title,
                                          &channel.meter,
                                          &channel.mute,
                                          &channel.solo,
                                          &channel.fader,
                                          &channel.valueLabel,
                                          &channel.pan,
                                          &channel.panLabel,
                                          &channel.send,
                                          &channel.sendLabel,
                                          &channel.stereoTag,
                                          hasSend });

    addAndMakeVisible(*channel.component);
    channel.component->addAndMakeVisible(channel.title);
    channel.component->addAndMakeVisible(channel.stereoTag);
    channel.component->addAndMakeVisible(channel.meter);
    channel.component->addAndMakeVisible(channel.mute);
    channel.component->addAndMakeVisible(channel.solo);
    channel.component->addAndMakeVisible(channel.fader);
    channel.component->addAndMakeVisible(channel.valueLabel);
    channel.component->addAndMakeVisible(channel.pan);
    channel.component->addAndMakeVisible(channel.panLabel);
    channel.component->addAndMakeVisible(channel.send);
    channel.component->addAndMakeVisible(channel.sendLabel);
}

void MixPanel::applyConfigToChannels()
{
    const auto faderStyle = FaderStyle::fromUIConfig(uiConfig, "mix.fader");
    const auto muteStyle = buttonStyleFromConfig(uiConfig, "mix.mute", MixerToggleButton::Style());
    const auto soloStyle = buttonStyleFromConfig(uiConfig, "mix.solo", MixerToggleButton::Style());
    const auto titleSize = uiConfig != nullptr ? uiConfig->getFloat("mix.channel.titleSize", 11.5f) : 11.5f;
    const auto labelSize = uiConfig != nullptr ? uiConfig->getFloat("mix.channel.labelSize", 9.5f) : 9.5f;
    MixerLevelMeter::Style meterStyle;
    if (uiConfig != nullptr)
    {
        meterStyle.cornerRadius = uiConfig->getFloat("mix.meter.cornerRadius", meterStyle.cornerRadius);
        meterStyle.backgroundColour = uiConfig->getColour("mix.meter.backgroundColour", meterStyle.backgroundColour);
        meterStyle.fillColour = uiConfig->getColour("mix.meter.fillColour", meterStyle.fillColour);
        meterStyle.highColour = uiConfig->getColour("mix.meter.highColour", meterStyle.highColour);
        meterStyle.clipColour = uiConfig->getColour("mix.meter.clipColour", meterStyle.clipColour);
        meterStyle.borderColour = uiConfig->getColour("mix.meter.borderColour", meterStyle.borderColour);
    }

    for (auto* channel : channels)
    {
        if (channel == nullptr)
        {
            continue;
        }

        channel->fader.applyStyle(faderStyle);
        channel->mute.applyStyle(muteStyle);
        channel->solo.applyStyle(soloStyle);
        channel->title.setFont(juce::FontOptions(titleSize));
        channel->panLabel.setFont(juce::FontOptions(labelSize));
        channel->sendLabel.setFont(juce::FontOptions(labelSize));
        channel->valueLabel.setFont(juce::FontOptions(labelSize));
        channel->stereoTag.setFont(juce::FontOptions(labelSize - 0.5f));
        channel->meter.applyStyle(meterStyle);
    }
}

void MixPanel::refreshMeterValues()
{
    for (int sourceIndex = 0; sourceIndex < PX3SynthAudioProcessor::kMixerSourceCount; ++sourceIndex)
    {
        if (auto* channel = channels[static_cast<std::size_t>(sourceIndex)])
        {
            channel->meter.setLevel(processor.debugGetMixerSourceRms(sourceIndex));
            channel->valueLabel.setText(linearGainToDbText(static_cast<float>(channel->fader.getValue())), juce::dontSendNotification);
        }
    }

    fxChannel.meter.setLevel(processor.debugGetFxReturnRms());
    fxChannel.valueLabel.setText(linearGainToDbText(static_cast<float>(fxChannel.fader.getValue())), juce::dontSendNotification);
}

juce::String MixPanel::linearGainToDbText(float linearGain)
{
    if (linearGain <= 0.00001f)
    {
        return "-INF dB";
    }

    const auto db = juce::Decibels::gainToDecibels(linearGain);
    return juce::String(db, 1) + " dB";
}
