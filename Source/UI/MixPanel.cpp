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
            channels { { &subChannel, &osc1Channel, &osc2Channel, &osc3Channel, &dryChannel, &fxChannel } },
            accent(panelAccent)
{
        configureChannelWidgets(subChannel, "SUB", true, false);
        configureChannelWidgets(osc1Channel, "OSC 1", true, false);
        configureChannelWidgets(osc2Channel, "OSC 2", true, false);
        configureChannelWidgets(osc3Channel, "OSC 3", true, false);
        // No FX send on the dry bus: it is what the sends feed away from, so a
        // send here would route the dry path into the FX chain it bypasses.
        // Built before configureChannelWidgets, which hands the pointers to the
        // channel component.
        addInsertButtons(dryChannel, PX3SynthAudioProcessor::dryBusInsert);
        addInsertButtons(fxChannel, PX3SynthAudioProcessor::fxBusInsert);
        configureChannelWidgets(dryChannel, "DRY", false, true);
        configureChannelWidgets(fxChannel, "FX", false, true);

        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerLevelParam(0), subChannel.fader, nullptr));
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerLevelParam(1), osc1Channel.fader, nullptr));
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerLevelParam(2), osc2Channel.fader, nullptr));
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerLevelParam(3), osc3Channel.fader, nullptr));
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getDryBusGainParam(), dryChannel.fader, nullptr));
        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getFxReturnGainParam(), fxChannel.fader, nullptr));

        for (int sourceIndex = 0; sourceIndex < PX3SynthAudioProcessor::kMixerSourceCount; ++sourceIndex)
        {
            auto* channel = channels[static_cast<std::size_t>(sourceIndex)];
            sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerPanParam(sourceIndex), channel->pan, nullptr));
            sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getMixerSendParam(sourceIndex), channel->send, nullptr));
            buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getMixerMuteParam(sourceIndex), channel->mute, nullptr));
            buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getMixerSoloParam(sourceIndex), channel->solo, nullptr));
            buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getMixerPhaseInvertParam(sourceIndex), channel->phase, nullptr));
        }

        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getDryBusPanParam(), dryChannel.pan, nullptr));
        buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getDryBusMuteParam(), dryChannel.mute, nullptr));
        buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getDryBusSoloParam(), dryChannel.solo, nullptr));
        buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getDryBusPhaseInvertParam(), dryChannel.phase, nullptr));

        sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(processor.getFxReturnPanParam(), fxChannel.pan, nullptr));
        buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getFxReturnMuteParam(), fxChannel.mute, nullptr));
        buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getFxReturnSoloParam(), fxChannel.solo, nullptr));
        buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(processor.getFxReturnPhaseInvertParam(), fxChannel.phase, nullptr));

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
        channel->component->setPanelContentBounds(area);
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
    refreshInsertButtonStates();
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

    // Readouts under the pan and send knobs, styled like the fader's dB text.
    for (auto* valueLabel : { &channel.panValueLabel, &channel.sendValueLabel })
    {
        valueLabel->setJustificationType(juce::Justification::centred);
        valueLabel->setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 214, 224));
        valueLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        valueLabel->setFont(juce::FontOptions(9.5f));
        valueLabel->setInterceptsMouseClicks(false, false);
    }

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
    channel.pan.setCentreDetent(0.14);
    channel.pan.setExtremeDetent(0.06);
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
                                          &channel.phase,
                                          &channel.fader,
                                          &channel.valueLabel,
                                          &channel.pan,
                                          &channel.panLabel,
                                          &channel.panValueLabel,
                                          &channel.send,
                                          &channel.sendLabel,
                                          &channel.sendValueLabel,
                                          &channel.stereoTag,
                                          channel.eqInsert.get(),
                                          channel.compInsert.get(),
                                          hasSend });

    addAndMakeVisible(*channel.component);
    channel.component->addAndMakeVisible(channel.title);
    channel.component->addAndMakeVisible(channel.phase);
    channel.component->addAndMakeVisible(channel.stereoTag);
    channel.component->addAndMakeVisible(channel.meter);
    channel.component->addAndMakeVisible(channel.mute);
    channel.component->addAndMakeVisible(channel.solo);
    channel.component->addAndMakeVisible(channel.fader);
    channel.component->addAndMakeVisible(channel.valueLabel);
    channel.component->addAndMakeVisible(channel.pan);
    channel.component->addAndMakeVisible(channel.panLabel);
    channel.component->addAndMakeVisible(channel.panValueLabel);
    channel.component->addAndMakeVisible(channel.send);
    channel.component->addAndMakeVisible(channel.sendLabel);
    channel.component->addAndMakeVisible(channel.sendValueLabel);

    if (channel.eqInsert != nullptr)
    {
        channel.component->addAndMakeVisible(*channel.eqInsert);
    }

    if (channel.compInsert != nullptr)
    {
        channel.component->addAndMakeVisible(*channel.compInsert);
    }
}

void MixPanel::addInsertButtons(ChannelWidgets& channel, int bus)
{
    channel.eqInsert = std::make_unique<InsertButton>("EQ");
    channel.compInsert = std::make_unique<InsertButton>("CMP");

    channel.eqInsert->onClick = [this, bus]()
    {
        if (onOpenBusInsert != nullptr)
        {
            onOpenBusInsert(bus, true);
        }
    };

    channel.compInsert->onClick = [this, bus]()
    {
        if (onOpenBusInsert != nullptr)
        {
            onOpenBusInsert(bus, false);
        }
    };
}

// The buttons light with their insert's enable state, so the strip says what is
// running without the overlay being open. Polled with the rest of the meters
// rather than attached: an enable can change from the overlay, from the host's
// automation or from a preset load, and polling covers all three.
void MixPanel::refreshInsertButtonStates()
{
    const std::array<std::pair<ChannelWidgets*, int>, 2> insertChannels { {
        { &dryChannel, PX3SynthAudioProcessor::dryBusInsert },
        { &fxChannel, PX3SynthAudioProcessor::fxBusInsert },
    } };

    for (const auto& entry : insertChannels)
    {
        const auto& params = processor.getBusInsertParams(entry.second);

        if (entry.first->eqInsert != nullptr && params.eqEnabled != nullptr)
        {
            entry.first->eqInsert->setInsertActive(params.eqEnabled->get());
        }

        if (entry.first->compInsert != nullptr && params.compEnabled != nullptr)
        {
            entry.first->compInsert->setInsertActive(params.compEnabled->get());
        }
    }
}

void MixPanel::applyConfigToChannels()
{
    // Each channel reads its own style block, so it can wear the colours of the
    // thing it controls: the sub oscillator, the three oscillators, and the FX
    // return - which takes the Delay card's scheme.
    const std::array<std::pair<ChannelWidgets*, const char*>, 6> styledChannels { {
        { &subChannel, "mixerSub" },
        { &osc1Channel, "mixerOsc1" },
        { &osc2Channel, "mixerOsc2" },
        { &osc3Channel, "mixerOsc3" },
        { &dryChannel, "mixerDry" },
        { &fxChannel, "mixerFx" },
    } };

    for (const auto& entry : styledChannels)
    {
        if (entry.first->component != nullptr)
        {
            entry.first->component->setCardStyleKey(entry.second);
            entry.first->component->setUIConfig(uiConfig);
        }
    }

    const auto faderStyle = FaderStyle::fromUIConfig(uiConfig, "mix.fader");
    const auto muteStyle = buttonStyleFromConfig(uiConfig, "mix.mute", MixerToggleButton::Style());
    const auto soloStyle = buttonStyleFromConfig(uiConfig, "mix.solo", MixerToggleButton::Style());
    const auto phaseStyle = buttonStyleFromConfig(uiConfig, "mix.phase", MixerToggleButton::Style());
    // Defaults to the phase button's look, which is the one the strip already
    // uses for a symbol rather than a word. Its own block overrides it.
    const auto insertStyle = buttonStyleFromConfig(uiConfig, "mix.insertButton", phaseStyle);
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

    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        auto* channel = channels[index];
        if (channel == nullptr)
        {
            continue;
        }

        // The fader keeps the colour of the SOURCE it controls, read from that
        // source's own card - not from the channel strip's card, which now
        // wears the mixer's scheme. Taking it from the strip would have turned
        // every fader white along with the cards.
        auto channelFaderStyle = faderStyle;
        if (uiConfig != nullptr)
        {
            static const std::array<const char*, 6> sourceCards { {
                "cards.subOsc.border.color",
                "cards.osc1.border.color",
                "cards.osc2.border.color",
                "cards.osc3.border.color",
                "cards.delay.border.color",   // the dry bus takes the FX scheme
                "cards.delay.border.color",   // and so does the FX return
            } };

            const auto slot = index;
            if (slot < sourceCards.size())
            {
                channelFaderStyle.accentColour =
                    uiConfig->getColour(sourceCards[slot], channelFaderStyle.accentColour);
                channelFaderStyle.trackColour = channelFaderStyle.accentColour.withAlpha(0.80f);
            }
        }
        channel->fader.applyStyle(channelFaderStyle);
        channel->mute.applyStyle(muteStyle);
        channel->solo.applyStyle(soloStyle);
        channel->phase.applyStyle(phaseStyle);

        if (channel->eqInsert != nullptr)
        {
            channel->eqInsert->applyStyle(insertStyle);
        }

        if (channel->compInsert != nullptr)
        {
            channel->compInsert->applyStyle(insertStyle);
        }
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
            refreshKnobReadouts(*channel);

            // Sub Osc is source 0 and the three oscillators follow it, matching
            // the mixer's own source order.
            if (channel->component != nullptr)
            {
                const auto sourceOn = sourceIndex == 0
                                          ? processor.getSubOscEnabledParam().get()
                                          : processor.getOscillatorEnabledParam(sourceIndex - 1).get();
                channel->component->setSourceActive(sourceOn);
            }
        }
    }

    dryChannel.meter.setLevel(processor.debugGetDryBusRms());
    dryChannel.valueLabel.setText(linearGainToDbText(static_cast<float>(dryChannel.fader.getValue())), juce::dontSendNotification);
    refreshKnobReadouts(dryChannel);

    fxChannel.meter.setLevel(processor.debugGetFxReturnRms());
    fxChannel.valueLabel.setText(linearGainToDbText(static_cast<float>(fxChannel.fader.getValue())), juce::dontSendNotification);
    refreshKnobReadouts(fxChannel);
}

void MixPanel::refreshKnobReadouts(ChannelWidgets& channel)
{
    // Pan reads as a side and a percentage - "L42", "C", "R08" - because "-0.42"
    // says nothing about which speaker it favours.
    const auto pan = static_cast<float>(channel.pan.getValue());
    const auto panPercent = juce::roundToInt(std::abs(pan) * 100.0f);
    const auto panText = panPercent == 0 ? juce::String("C")
                                         : (pan < 0.0f ? "L" : "R") + juce::String(panPercent);
    channel.panValueLabel.setText(panText, juce::dontSendNotification);

    if (channel.hasSend)
    {
        const auto sendPercent = juce::roundToInt(juce::jlimit(0.0, 1.0, channel.send.getValue()) * 100.0);
        channel.sendValueLabel.setText(juce::String(sendPercent) + "%", juce::dontSendNotification);
    }
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
