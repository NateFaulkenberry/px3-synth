#include "MixerChannelComponent.h"

#include "UIConfig.h"

MixerChannelComponent::MixerChannelComponent(Controls controlsIn)
    : controls(std::move(controlsIn))
{
}

void MixerChannelComponent::setSourceActive(bool active)
{
    if (sourceActive == active)
    {
        return;
    }

    sourceActive = active;

    for (auto* component : { controls.meter, static_cast<juce::Component*>(controls.mute),
                             static_cast<juce::Component*>(controls.solo),
                             static_cast<juce::Component*>(controls.phase),
                             static_cast<juce::Component*>(controls.eqInsert),
                             static_cast<juce::Component*>(controls.compInsert),
                             static_cast<juce::Component*>(controls.fader),
                             static_cast<juce::Component*>(controls.pan),
                             static_cast<juce::Component*>(controls.send) })
    {
        if (component != nullptr)
        {
            component->setEnabled(active);
        }
    }

    for (auto* label : { controls.valueLabel, controls.panLabel, controls.panValueLabel,
                         controls.sendLabel, controls.sendValueLabel, controls.stereoTag })
    {
        if (label != nullptr)
        {
            label->setEnabled(active);
        }
    }

    repaint();
}

juce::Colour MixerChannelComponent::cardAccentColour() const
{
    return card.style().border.colour;
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

    // The two insert buttons are placed by coordinate, so each needs its own
    // size and offset. The shared block is read first and a card block may
    // override it, which is what lets the dry strip and the FX strip place them
    // differently without either having to restate the parts they share.
    //
    // Each key is tested with getValue, NOT with getObject: getObject returns a
    // fresh empty object for a path that does not exist, so it can never report
    // absence. Using it as an existence test here matched the card block that
    // was not there, read nothing out of it, and stopped before reaching the
    // shared one - so mix.inserts was silently dead.
    auto readLayout = [this](const juce::String& which, InsertButtonLayout& target)
    {
        target = {};
        if (uiConfig == nullptr)
        {
            return;
        }

        const auto apply = [&](const juce::String& base)
        {
            const auto number = [&](const char* key, int& field)
            {
                if (const auto value = uiConfig->getValue(base + key); ! value.isVoid())
                {
                    field = static_cast<int>(value);
                }
            };

            number(".size", target.size);
            number(".offsetX", target.offsetX);
            number(".offsetY", target.offsetY);
        };

        apply("mix.inserts." + which);
        apply("cards." + cardStyleKey + ".inserts." + which);
    };

    readLayout("eq", eqLayout);
    readLayout("comp", compLayout);
}

// The insert buttons sit in the strip's bottom corners: EQ on the left, COMP on
// the right, both square, both inside the card's content box so they line up
// with the fader and knobs above rather than with the raw component edge.
void MixerChannelComponent::layoutInsertButtons()
{
    if (controls.eqInsert == nullptr && controls.compInsert == nullptr)
    {
        return;
    }

    const auto content = card.contentBelowTitle().toNearestInt();

    if (controls.eqInsert != nullptr)
    {
        controls.eqInsert->setBounds(content.getX() + eqLayout.offsetX,
                                     content.getBottom() - eqLayout.size + eqLayout.offsetY,
                                     eqLayout.size,
                                     eqLayout.size);
    }

    if (controls.compInsert != nullptr)
    {
        controls.compInsert->setBounds(content.getRight() - compLayout.size + compLayout.offsetX,
                                       content.getBottom() - compLayout.size + compLayout.offsetY,
                                       compLayout.size,
                                       compLayout.size);
    }
}

void MixerChannelComponent::paint(juce::Graphics& g)
{
    // The card draws the title, exactly as it does on every other component, so
    // the channel name picks up the shared title style and position instead of
    // being a Label the strip positions itself.
    const auto title = controls.title != nullptr ? controls.title->getText() : juce::String();

    // Greyscale when the source is off, exactly as its own card does.
    if (sourceActive)
    {
        card.draw(g, title);
    }
    else
    {
        card.drawInactive(g, title);
    }
}

void MixerChannelComponent::resized()
{
    refreshCardStyle();

    // Inside the card's content box, not the raw component bounds. Using the
    // latter is why the meter and the mute/solo row sat flush against the
    // strip's edge while every other card kept its padding.
    auto area = card.contentBelowTitle().toNearestInt();

    // The card's title sits directly above this box, so the first row needs a
    // gap of its own or the meter reads as attached to the name.
    area.removeFromTop(titleGap);

    // The title label is retired: the card renders the name now. It is kept as
    // the text's home so MixPanel still sets it in one place.
    if (controls.title != nullptr)
    {
        controls.title->setVisible(false);
        controls.title->setBounds({});
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
        // MUTE | SOLO | PHASE, sharing the row evenly. Phase is narrower in its
        // legend but not in its target: a symbol is harder to hit than a word,
        // so it gets the same width as the others.
        auto buttonsRow = area.removeFromTop(42);
        const auto slots = controls.phase != nullptr ? 3 : 2;
        const auto buttonWidth = (buttonsRow.getWidth() - buttonGap * (slots - 1)) / slots;

        controls.mute->setBounds(buttonsRow.removeFromLeft(buttonWidth));
        buttonsRow.removeFromLeft(buttonGap);

        if (controls.phase != nullptr)
        {
            controls.solo->setBounds(buttonsRow.removeFromLeft(buttonWidth));
            buttonsRow.removeFromLeft(buttonGap);
            controls.phase->setBounds(buttonsRow);
        }
        else
        {
            controls.solo->setBounds(buttonsRow);
        }

        area.removeFromTop(sectionSpacing);
    }

    if (controls.fader != nullptr)
    {
        const auto valueHeight = (controls.valueLabel != nullptr && controls.valueLabel->isVisible()) ? footerLabelHeight : 0;
        // Two footer rows per knob column now - the name and the value - so the
        // fader gives back the extra row rather than pushing the knobs off.
        const auto faderHeight = juce::jmax(70, area.getHeight() - valueHeight
                                                    - 3 * (footerLabelHeight + sectionSpacing)
                                                    - 2 * 22);
        controls.fader->setBounds(area.removeFromTop(faderHeight));
        // The readout belongs to the fader, so it sits tight under it rather
        // than a full section away - the fader's own trackPadding already
        // leaves a visual gap below the last tick.
        area.removeFromTop(faderValueSpacing);
    }

    if (controls.valueLabel != nullptr && controls.valueLabel->isVisible())
    {
        controls.valueLabel->setBounds(area.removeFromTop(footerLabelHeight));
        area.removeFromTop(sectionSpacing);
    }

    const int knobSize = 46;
    // Wider than the mute/solo gap: two round controls sitting shoulder to
    // shoulder need more air between them than two rectangles do.
    const int knobGap = buttonGap + 10;
    const int labelSlotWidth = juce::jmax(knobSize, 36);

    if (controls.hasSend && controls.pan != nullptr && controls.send != nullptr)
    {
        if (controls.panLabel != nullptr || controls.sendLabel != nullptr)
        {
            auto labelsRow = area.removeFromTop(footerLabelHeight);
            area.removeFromTop(2);
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

        // A readout under each knob, matching every other knob in the plugin.
        auto valuesRow = area.removeFromTop(footerLabelHeight);
        auto centeredValues = valuesRow.withSizeKeepingCentre(knobsWidth, footerLabelHeight);
        if (controls.panValueLabel != nullptr)
        {
            controls.panValueLabel->setBounds(centeredValues.removeFromLeft(labelSlotWidth));
        }
        centeredValues.removeFromLeft(knobGap);
        if (controls.sendValueLabel != nullptr)
        {
            controls.sendValueLabel->setBounds(centeredValues.removeFromLeft(labelSlotWidth));
        }
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

        if (controls.panValueLabel != nullptr)
        {
            auto panValueRow = area.removeFromTop(footerLabelHeight);
            controls.panValueLabel->setBounds(panValueRow.withSizeKeepingCentre(labelSlotWidth, footerLabelHeight));
        }

        if (controls.sendValueLabel != nullptr)
        {
            controls.sendValueLabel->setBounds({});
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

    layoutInsertButtons();
}
