#include "ParameterKnob.h"
#include "ModPanel.h"

#include "UIConfig.h"
#include "Theme.h"

#include <cmath>

ModPanel::ModPanel(PX3SynthAudioProcessor& processorIn,
                   juce::ToggleButton& lfoEnabledButton,
                   juce::Label& lfoAssignLabel,
                   juce::ComboBox& lfoAssignBox,
                   juce::Slider& lfoRateKnob,
                   juce::Label& lfoRateLabel,
                   juce::Label& lfoRateValueLabel,
                                     juce::Slider& lfoAmountKnob,
                                     juce::Label& lfoAmountLabel,
                                     juce::Label& lfoAmountValueLabel,
                   juce::ComboBox& lfoWaveformBox,
                   juce::Label& lfoWaveformLabel,
                 juce::LookAndFeel* sharedLfoKnobLookAndFeel,
                   juce::Colour panelAccent,
                   juce::Colour lfoAccent)
        : processor(processorIn),
            accent(panelAccent),
        lfoHeaderAccent(lfoAccent),
        lfoKnobLookAndFeel(sharedLfoKnobLookAndFeel)
{
    lfoComponent = std::make_unique<LfoComponent>(lfoEnabledButton,
                                                  lfoAssignLabel,
                                                  lfoAssignBox,
                                                  lfoRateKnob,
                                                  lfoRateLabel,
                                                  lfoRateValueLabel,
                                                      lfoAmountKnob,
                                                      lfoAmountLabel,
                                                      lfoAmountValueLabel,
                                                  lfoWaveformBox,
                                                  lfoWaveformLabel,
                                                  lfoAccent);
    lfoComponent->attachRampAndKeySync(processor.getLfoRampTimeParam(0),
                                       processor.getLfoKeySyncParam(0),
                                       lfoKnobLookAndFeel);
    lfoComponent->attachClock(processor.getLfoClockModeParam(0), processor.getLfoClockDivisionParam(0));

    // LFO 1's rate label is left exactly as configureKnob set it. This used to
    // blank the text to match the other LFO cards, which had no rate label at
    // the time; they do now, so blanking it made LFO 1 the odd one out.

    addAndMakeVisible(lfoTabs);
    addAndMakeVisible(envTabs);
    lfoTabs.addPage(*lfoComponent);

    for (int lfoIndex = 1; lfoIndex < PX3SynthAudioProcessor::kLfoSourceCount; ++lfoIndex)
    {
        auto& bundle = extraLfos[static_cast<std::size_t>(lfoIndex - 1)];
        configureOwnedLfoBundle(lfoIndex, bundle);
        lfoTabs.addPage(*bundle.component);
    }

    for (int envIndex = 0; envIndex < PX3SynthAudioProcessor::kEnvelopeSourceCount; ++envIndex)
    {
        auto& bundle = envelopes[static_cast<std::size_t>(envIndex)];
        configureOwnedEnvBundle(envIndex, bundle);
        envTabs.addPage(*bundle.component);
    }

    // The tab each strip shows is the processor's, not this window's: a window
    // opened later in the session comes back to the same modulators.
    lfoTabs.setSelected(processor.getSelectedLfoTab(), false);
    envTabs.setSelected(processor.getSelectedEnvTab(), false);
    lfoTabs.onSelectionChanged = [this](int tab) { processor.setSelectedLfoTab(tab); };
    envTabs.onSelectionChanged = [this](int tab) { processor.setSelectedEnvTab(tab); };
}

void ModPanel::attachModSources(px3::ui::modrouting::ModDragController& controller)
{
    // A jack on every LFO and ENV tab: drag it onto any knob to patch it.
    for (int i = 0; i < static_cast<int>(cardSockets.size()); ++i)
    {
        auto& socket = cardSockets[static_cast<std::size_t>(i)];
        socket = std::make_unique<px3::ui::modrouting::ModSourceSocket>(controller, i, false);
        if (i < kLfos) { lfoTabs.setSocket(i, socket.get()); }
        else           { envTabs.setSocket(i - kLfos, socket.get()); }
    }
    resized();
}

void ModPanel::refreshModSources(const std::vector<px3::ui::modrouting::RouteInfo>& routes)
{
    for (auto& socket : cardSockets)
    {
        if (socket == nullptr) { continue; }
        auto count = 0;
        for (const auto& route : routes) { count += route.source == socket->getSource() ? 1 : 0; }
        socket->setRouteCount(count);
    }
}

void ModPanel::configureOwnedLfoBundle(int lfoIndex, LfoBundle& bundle)
{
    bundle.enabledButton.setSectionName("LFO " + juce::String(lfoIndex + 1));

    bundle.assignLabel.setText("ASSIGN", juce::dontSendNotification);
    bundle.assignLabel.setJustificationType(juce::Justification::centred);
    bundle.assignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignLabel.setFont(juce::FontOptions(11.5f));
    bundle.assignLabel.setInterceptsMouseClicks(true, false);
    bundle.assignLabel.setTooltip("LFO Assignment");

    bundle.rateLabel.setText("RATE", juce::dontSendNotification);
    bundle.rateLabel.setJustificationType(juce::Justification::centred);
    bundle.rateLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.rateLabel.setFont(juce::FontOptions(13.0f));
    bundle.rateLabel.setInterceptsMouseClicks(false, false);
    bundle.rateLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    bundle.rateValueLabel.setJustificationType(juce::Justification::centred);
    bundle.rateValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    bundle.rateValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    bundle.rateValueLabel.setFont(juce::FontOptions(11.0f));
    bundle.rateValueLabel.setInterceptsMouseClicks(false, false);

    bundle.waveformLabel.setText("WAVE", juce::dontSendNotification);
    bundle.waveformLabel.setJustificationType(juce::Justification::centred);
    bundle.waveformLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.waveformLabel.setFont(juce::FontOptions(11.5f));
    bundle.waveformLabel.setInterceptsMouseClicks(true, false);
    bundle.waveformLabel.setTooltip("Waveform");

    bundle.amountKnob.setCentreDetent(0.06);
    bundle.amountKnob.setExtremeDetent(0.0);
    bundle.amountLabel.setText("AMOUNT", juce::dontSendNotification);
    bundle.amountLabel.setJustificationType(juce::Justification::centred);
    bundle.amountLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.amountLabel.setFont(juce::FontOptions(11.0f));
    bundle.amountLabel.setInterceptsMouseClicks(false, false);
    bundle.amountLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    bundle.amountValueLabel.setJustificationType(juce::Justification::centred);
    bundle.amountValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    bundle.amountValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    bundle.amountValueLabel.setFont(juce::FontOptions(11.0f));
    bundle.amountValueLabel.setInterceptsMouseClicks(false, false);


    bundle.assignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    bundle.assignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    bundle.waveformBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    bundle.waveformBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.waveformBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    bundle.rateKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bundle.rateKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    const auto& lfoRateParam = processor.getLfoFrequencyParam(lfoIndex);
    const auto& lfoRateRange = lfoRateParam.getNormalisableRange();
    bundle.rateKnob.setRange(lfoRateRange.start, lfoRateRange.end);
    if (lfoKnobLookAndFeel != nullptr)
    {
        bundle.rateKnob.setLookAndFeel(lfoKnobLookAndFeel);
    }

    bundle.amountKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bundle.amountKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bundle.amountKnob.setRange(-1.0, 1.0, 0.0);
    if (lfoKnobLookAndFeel != nullptr)
    {
        bundle.amountKnob.setLookAndFeel(lfoKnobLookAndFeel);
    }

    const auto& lfoWaveformParam = processor.getLfoWaveformParam(lfoIndex);
    for (int i = 0; i < lfoWaveformParam.choices.size(); ++i)
    {
        bundle.waveformBox.addItem(lfoWaveformParam.choices[i], i + 1);
    }
    bundle.waveformBox.setSelectedItemIndex(lfoWaveformParam.getIndex(), juce::dontSendNotification);

    const auto& assignments = processor.getLfoAssignmentDisplayNames();
    for (int i = 0; i < assignments.size(); ++i)
    {
        bundle.assignBox.addItem(assignments[i], i + 1);
    }
    bundle.lastAssignmentIndex = processor.getLfoAssignmentIndex(lfoIndex);
    bundle.assignBox.setSelectedId(bundle.lastAssignmentIndex + 1, juce::dontSendNotification);

    bundle.assignBox.onChange = [this, lfoIndex, &bundle]()
    {
        const auto selected = juce::jmax(0, bundle.assignBox.getSelectedId() - 1);
        processor.setLfoAssignmentIndex(lfoIndex, selected);
    };

    bundle.rateKnob.onValueChange = [&bundle]()
    {
        const auto hz = juce::jlimit(0.01f, 20.0f, static_cast<float>(bundle.rateKnob.getValue()));
        bundle.rateValueLabel.setText(juce::String(hz, 2) + " Hz", juce::dontSendNotification);
    };

    bundle.amountKnob.onValueChange = [&bundle]()
    {
        const auto amount = juce::jlimit(-1.0f, 1.0f, static_cast<float>(bundle.amountKnob.getValue()));
        const auto amountPercent = static_cast<int>(std::lround(amount * 100.0f));
        const auto prefix = amountPercent > 0 ? juce::String("+") : juce::String();
        bundle.amountValueLabel.setText(prefix + juce::String(amountPercent) + "%", juce::dontSendNotification);
    };

    bundle.enabledAttachment = std::make_unique<juce::ButtonParameterAttachment>(processor.getLfoEnabledParam(lfoIndex), bundle.enabledButton, nullptr);
    bundle.rateAttachment = px3::ui::makeParameterKnobAttachment(processor.getLfoFrequencyParam(lfoIndex), bundle.rateKnob);
    bundle.amountAttachment = px3::ui::makeParameterKnobAttachment(processor.getLfoAmountParam(lfoIndex), bundle.amountKnob);
    bundle.waveformAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(processor.getLfoWaveformParam(lfoIndex), bundle.waveformBox, nullptr);

    bundle.component = std::make_unique<LfoComponent>(bundle.enabledButton,
                                                      bundle.assignLabel,
                                                      bundle.assignBox,
                                                      bundle.rateKnob,
                                                      bundle.rateLabel,
                                                      bundle.rateValueLabel,
                                                      bundle.amountKnob,
                                                      bundle.amountLabel,
                                                      bundle.amountValueLabel,
                                                      bundle.waveformBox,
                                                      bundle.waveformLabel,
                                                      lfoHeaderAccent,
                                                      "mod.lfo" + juce::String(lfoIndex + 1));
    bundle.component->attachRampAndKeySync(processor.getLfoRampTimeParam(lfoIndex),
                                           processor.getLfoKeySyncParam(lfoIndex),
                                           lfoKnobLookAndFeel);
        bundle.component->attachClock(processor.getLfoClockModeParam(lfoIndex), processor.getLfoClockDivisionParam(lfoIndex));
}

void ModPanel::configureOwnedEnvBundle(int envIndex, EnvBundle& bundle)
{
    bundle.enabledButton.setSectionName("ENV " + juce::String(envIndex + 1));

    bundle.assignLabel.setText("ASSIGN", juce::dontSendNotification);
    bundle.assignLabel.setJustificationType(juce::Justification::centred);
    bundle.assignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignLabel.setFont(juce::FontOptions(11.5f));
    bundle.assignLabel.setInterceptsMouseClicks(true, false);
    bundle.assignLabel.setTooltip("Envelope Assignment");


    bundle.assignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    bundle.assignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.assignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    bundle.amountKnob.setCentreDetent(0.06);
    bundle.amountKnob.setExtremeDetent(0.0);
    bundle.amountLabel.setText("AMOUNT", juce::dontSendNotification);
    bundle.amountLabel.setJustificationType(juce::Justification::centred);
    bundle.amountLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    bundle.amountLabel.setFont(juce::FontOptions(11.0f));
    bundle.amountLabel.setInterceptsMouseClicks(false, false);
    bundle.amountLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    bundle.amountValueLabel.setJustificationType(juce::Justification::centred);
    bundle.amountValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    bundle.amountValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    bundle.amountValueLabel.setFont(juce::FontOptions(11.0f));
    bundle.amountValueLabel.setInterceptsMouseClicks(false, false);

    bundle.amountKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bundle.amountKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bundle.amountKnob.setRange(-1.0, 1.0, 0.0);
    if (lfoKnobLookAndFeel != nullptr)
    {
        bundle.amountKnob.setLookAndFeel(lfoKnobLookAndFeel);
    }

    const auto& assignments = processor.getEnvelopeAssignmentDisplayNames();
    for (int i = 0; i < assignments.size(); ++i)
    {
        bundle.assignBox.addItem(assignments[i], i + 1);
    }
    bundle.lastAssignmentIndex = processor.getEnvelopeAssignmentIndex(envIndex);
    bundle.assignBox.setSelectedId(bundle.lastAssignmentIndex + 1, juce::dontSendNotification);

    bundle.assignBox.onChange = [this, envIndex, &bundle]()
    {
        const auto selected = juce::jmax(0, bundle.assignBox.getSelectedId() - 1);
        processor.setEnvelopeAssignmentIndex(envIndex, selected);
    };

    bundle.amountKnob.onValueChange = [&bundle]()
    {
        const auto amount = juce::jlimit(-1.0f, 1.0f, static_cast<float>(bundle.amountKnob.getValue()));
        const auto amountPercent = static_cast<int>(std::lround(amount * 100.0f));
        const auto prefix = amountPercent > 0 ? juce::String("+") : juce::String();
        bundle.amountValueLabel.setText(prefix + juce::String(amountPercent) + "%", juce::dontSendNotification);
    };

    bundle.enabledAttachment = std::make_unique<juce::ButtonParameterAttachment>(processor.getEnvelopeEnabledParam(envIndex), bundle.enabledButton, nullptr);
    bundle.amountAttachment = px3::ui::makeParameterKnobAttachment(processor.getEnvelopeAmountParam(envIndex), bundle.amountKnob);

    bundle.component = std::make_unique<EnvelopeComponent>(processor.getEnvelopeAttackParam(envIndex),
                                                           processor.getEnvelopeDecayParam(envIndex),
                                                           processor.getEnvelopeSustainParam(envIndex),
                                                           processor.getEnvelopeReleaseParam(envIndex),
                                                           processor.getEnvelopeEnabledParam(envIndex),
                                                           bundle.enabledButton,
                                                           bundle.assignLabel,
                                                           bundle.assignBox,
                                                           &bundle.amountKnob,
                                                           &bundle.amountLabel,
                                                           &bundle.amountValueLabel,
                                                           accent,
                                                           juce::String("mod.env") + juce::String(envIndex + 1));

    // LOOP and KEY TRACK, either side of AMOUNT.
    if (auto* loop = dynamic_cast<juce::AudioParameterBool*>(
            processor.findRangedParameterById("mod.env" + juce::String(envIndex + 1) + ".loop")))
    {
        bundle.loopButton.setButtonText("LOOP");
        bundle.loopButton.setClickingTogglesState(true);
        bundle.loopButton.setTooltip("Loop: repeat attack/decay while the key is held");
        bundle.loopButton.setAccentColour(accent);
        bundle.loopAttachment = std::make_unique<juce::ButtonParameterAttachment>(*loop, bundle.loopButton, nullptr);
    }
    if (auto* sync = dynamic_cast<juce::AudioParameterBool*>(
            processor.findRangedParameterById("mod.env" + juce::String(envIndex + 1) + ".sync")))
    {
        bundle.syncButton.setButtonText("SYNC");
        bundle.syncButton.setClickingTogglesState(true);
        bundle.syncButton.setTooltip("Tempo sync: attack, decay and release snap to musical divisions of the host "
                                     "or MIDI clock tempo");
        bundle.syncButton.setAccentColour(accent);
        bundle.syncAttachment = std::make_unique<juce::ButtonParameterAttachment>(*sync, bundle.syncButton, nullptr);
        bundle.component->setSyncButton(&bundle.syncButton);
    }
    if (auto* key = processor.findRangedParameterById("mod.env" + juce::String(envIndex + 1) + ".keytrack"))
    {
        bundle.keyKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        bundle.keyKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        bundle.keyKnob.setTooltip("Key tracking: higher notes run the envelope faster");
        if (lfoKnobLookAndFeel != nullptr) { bundle.keyKnob.setLookAndFeel(lfoKnobLookAndFeel); }
        bundle.keyAttachment = px3::ui::makeParameterKnobAttachment(*key, bundle.keyKnob);
        bundle.keyLabel.setText("KEY", juce::dontSendNotification);
        bundle.keyLabel.setJustificationType(juce::Justification::centred);
        bundle.keyLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        bundle.keyLabel.setFont(juce::FontOptions(11.0f));
        bundle.keyLabel.setInterceptsMouseClicks(false, false);
        bundle.keyValueLabel.setJustificationType(juce::Justification::centred);
        bundle.keyValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
        bundle.keyValueLabel.setFont(juce::FontOptions(11.0f));
        bundle.keyValueLabel.setInterceptsMouseClicks(false, false);
        bundle.keyKnob.onValueChange = [&bundle]
        {
            bundle.keyValueLabel.setText(juce::String(juce::roundToInt(bundle.keyKnob.getValue() * 100.0)) + "%",
                                         juce::dontSendNotification);
        };
        bundle.keyKnob.onValueChange();
    }
    bundle.component->setLoopAndKeyControls(&bundle.loopButton, &bundle.keyKnob, &bundle.keyLabel, &bundle.keyValueLabel);

    // ENV 1/2/3 occupy slots 1..3. AMP ENV is slot 0 and is reached by a
    // different component entirely, which is what keeps the two systems from
    // acquiring a shared owner by accident.
    const auto slot = envIndex + 1;
    // ATTACK | DECAY | SUSTAIN | RELEASE under the graph. The card is taller
    // than it was to make the room, rather than taking it from the graph.
    bundle.component->setKnobLookAndFeel(lfoKnobLookAndFeel);
    bundle.component->setAdsrKnobsVisible(true);

    bundle.component->setEnvelopeMode(processor.getEnvelopeMode(slot));
    bundle.component->onEnvelopeModeChanged = [this, slot](px3::BreakpointEnvelope::Mode mode)
    {
        processor.setEnvelopeMode(slot, mode);
    };

    bundle.component->setShapedEnvelope(processor.getShapedEnvelope(slot));
    bundle.component->onEnvelopeEdited = [this, envIndex, slot](const px3::BreakpointEnvelope& edited)
    {
        processor.setShapedEnvelope(slot, edited);

        if (edited.isAdsrSkeleton())
        {
            const auto adsr = edited.toAdsr();
            const auto write = [](juce::AudioParameterFloat& parameter, float value)
            {
                parameter.beginChangeGesture();
                parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
                parameter.endChangeGesture();
            };
            write(processor.getEnvelopeAttackParam(envIndex), adsr.attackSeconds);
            write(processor.getEnvelopeDecayParam(envIndex), adsr.decaySeconds);
            write(processor.getEnvelopeSustainParam(envIndex), adsr.sustainLevel);
            write(processor.getEnvelopeReleaseParam(envIndex), adsr.releaseSeconds);
        }
    };
}

void ModPanel::setSceneManaged(bool managed)
{
    sceneManaged = managed;
    // Placed by the scene as compact modules: their interiors go dense too.
    // Not cached as images: an animated display inside a cached card dirtied
    // the cache, and re-rendering it through an exclusion clip cost more than
    // the card. The moving parts are opaque layers instead (AnimatedDisplay),
    // so a frame repaints only them.
    if (lfoComponent != nullptr) { lfoComponent->setCompactLayout(managed); }
    for (auto& bundle : extraLfos) { if (bundle.component != nullptr) { bundle.component->setCompactLayout(managed); } }
    for (auto& bundle : envelopes) { if (bundle.component != nullptr) { bundle.component->setCompactLayout(managed); } }
}

juce::Component* ModPanel::getCard(int index)
{
    if (index == 0) { return lfoComponent.get(); }
    if (juce::isPositiveAndBelow(index, kLfos)) { return extraLfos[static_cast<std::size_t>(index - 1)].component.get(); }
    if (juce::isPositiveAndBelow(index - kLfos, kEnvs))
    {
        return envelopes[static_cast<std::size_t>(index - kLfos)].component.get();
    }
    return nullptr;
}

void ModPanel::layoutSockets()
{
    lfoTabs.resized();
    envTabs.resized();
}

void ModPanel::childBoundsChanged(juce::Component*)
{
}

void ModPanel::paint(juce::Graphics& g)
{
    // Scene-managed, the cards tile the panel with 1 px seams; the chassis
    // behind shows through the seams, so there is nothing to draw here.
    if (sceneManaged) { return; }
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("mod.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("mod.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto panelRadius = uiConfig != nullptr ? uiConfig->getFloat("mod.panel.cornerRadius", 10.0f) : 10.0f;
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);

}

void ModPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (uiConfig != nullptr)
    {
        // The tabs take the cards' identity colours (LFO purple, ENV yellow)
        // and a height from the config.
        lfoTabs.setAccent(uiConfig->getColour("cards.lfo1.border.color", juce::Colour(0xff9f7aea)));
        envTabs.setAccent(uiConfig->getColour("cards.env1.border.color", juce::Colour(0xfff2c14e)));
        const auto tabHeight = uiConfig->getInt("mod.tabs.height", 28);
        lfoTabs.setTabHeight(tabHeight);
        envTabs.setTabHeight(tabHeight);

        const auto comboStyle = uiConfig->getObject("styles.combos.default");
        for (auto& bundle : extraLfos)
        {
            uiConfig->applyComboStyle(comboStyle, bundle.assignBox);
            uiConfig->applyComboStyle(comboStyle, bundle.waveformBox);
        }
        for (auto& bundle : envelopes)
        {
            uiConfig->applyComboStyle(comboStyle, bundle.assignBox);
        }
    }

    if (lfoComponent != nullptr)
    {
        lfoComponent->setUIConfig(uiConfig);
    }

    for (auto& bundle : extraLfos)
    {
        if (bundle.component != nullptr)
        {
            bundle.component->setUIConfig(uiConfig);
        }
    }

    for (auto& bundle : envelopes)
    {
        if (bundle.component != nullptr)
        {
            bundle.component->setUIConfig(uiConfig);
        }
    }

    repaint();
}

void ModPanel::resized()
{
    if (lfoComponent == nullptr)
    {
        return;
    }
    if (sceneManaged)
    {
        layoutSockets();
        return;
    }

    // Outside the scene (not used by the editor today): the two tab panels
    // side by side, LFOs left.
    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padY", 10) : 10;
    auto panelArea = getLocalBounds().reduced(panelPadX, panelPadY);
    lfoTabs.setBounds(panelArea.removeFromLeft((panelArea.getWidth() - 1) / 2));
    panelArea.removeFromLeft(1);
    envTabs.setBounds(panelArea);
}

int ModPanel::getPreferredContentWidth() const
{
    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padX", 12) : 12;
    const auto colGap = uiConfig != nullptr ? uiConfig->getInt("mod.grid.colGap", 8) : 8;
    const auto minColWidth = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minColWidth", 280) : 280;
    return panelPadX * 2 + minColWidth * 3 + colGap * 2;
}

int ModPanel::getPreferredContentHeight() const
{
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padY", 10) : 10;
    const auto rowGap = uiConfig != nullptr ? uiConfig->getInt("mod.grid.rowGap", 10) : 10;
    const auto minLfoHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minLfoHeight", 300) : 300;
    const auto minEnvHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minEnvHeight", 280) : 280;
    return panelPadY * 2 + rowGap + minLfoHeight + minEnvHeight;
}

void ModPanel::refreshFromParameters()
{
    for (int i = 0; i < static_cast<int>(envelopes.size()); ++i)
    {
        const auto envIndex = i;
        auto& bundle = envelopes[static_cast<std::size_t>(i)];
        if (bundle.component != nullptr)
        {
            const auto assignment = processor.getEnvelopeAssignmentIndex(envIndex);
            if (assignment != bundle.lastAssignmentIndex)
            {
                bundle.lastAssignmentIndex = assignment;
                bundle.assignBox.setSelectedId(assignment + 1, juce::dontSendNotification);
            }

            bundle.component->refreshFromParameters();

            // Rebuilt from the parameters while the shape is still ADSR, so a
            // knob or a DAW automation lane moves the curve.
            const auto slot = static_cast<int>(&bundle - envelopes.data()) + 1;
            // The parameters carry the four times and the level; the stored
            // shape carries the curves. On a skeleton they are applied
            // together, so turning a knob moves the graph without
            // straightening what was drawn.
            const auto stored = processor.getShapedEnvelope(slot);
            bundle.component->setShapedEnvelope(
                stored.isAdsrSkeleton()
                    ? stored.withAdsrApplied(processor.envelopeParameterSettings(slot - 1))
                    : stored);

            // And how far the playing note has taken this envelope, read from
            // the voice itself rather than clocked alongside it in the UI.
            bundle.component->setEnvelopeMode(processor.getEnvelopeMode(slot));
            bundle.component->setEnvelopeProgress(processor.getEnvelopeProgress(slot));
        }
    }
}

void ModPanel::refreshLfoFromParameters()
{
    if (lfoComponent != nullptr)
    {
        lfoComponent->refreshFromParameters(processor.getLfoEnabledParam(0).get(),
                                            processor.getLfoFrequencyParam(0).get(),
                                            processor.getLfoAmountParam(0).get(),
                                            processor.getLfoWaveformParam(0).getIndex());
                            lfoComponent->setClockAvailable(processor.isLfoClockAvailable(0));
    }

    for (int i = 0; i < static_cast<int>(extraLfos.size()); ++i)
    {
        const auto lfoIndex = i + 1;
        auto& bundle = extraLfos[static_cast<std::size_t>(i)];
        if (bundle.component != nullptr)
        {
            const auto assignment = processor.getLfoAssignmentIndex(lfoIndex);
            if (assignment != bundle.lastAssignmentIndex)
            {
                bundle.lastAssignmentIndex = assignment;
                bundle.assignBox.setSelectedId(assignment + 1, juce::dontSendNotification);
            }

            bundle.component->refreshFromParameters(processor.getLfoEnabledParam(lfoIndex).get(),
                                                    processor.getLfoFrequencyParam(lfoIndex).get(),
                                                    processor.getLfoAmountParam(lfoIndex).get(),
                                                    processor.getLfoWaveformParam(lfoIndex).getIndex());
            bundle.component->setClockAvailable(processor.isLfoClockAvailable(lfoIndex));
        }
    }
}

void ModPanel::advanceAnimation(float lfoDeltaSeconds)
{
    if (lfoComponent != nullptr)
    {
        lfoComponent->advanceAnimation(lfoDeltaSeconds);
    }

    for (auto& bundle : extraLfos)
    {
        if (bundle.component != nullptr)
        {
            bundle.component->advanceAnimation(lfoDeltaSeconds);
        }
    }
}