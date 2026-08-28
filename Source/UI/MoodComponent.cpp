#include "MoodComponent.h"

#include "CardInner.h"

#include <array>

#include "UIConfig.h"

MoodComponent::MoodComponent(juce::ToggleButton& enabledButtonIn,
                             juce::ToggleButton& freezeButtonIn,
                             juce::Slider& mixKnobIn,
                             juce::Label& mixLabelIn,
                             juce::Slider& clockKnobIn,
                             juce::Label& clockLabelIn,
                             juce::Slider& wetTimeKnobIn,
                             juce::Label& wetTimeLabelIn,
                             juce::Slider& wetModifyKnobIn,
                             juce::Label& wetModifyLabelIn,
                             juce::Slider& loopLengthKnobIn,
                             juce::Label& loopLengthLabelIn,
                             juce::Slider& loopModifyKnobIn,
                             juce::Label& loopModifyLabelIn,
                             juce::Slider& feedbackKnobIn,
                             juce::Label& feedbackLabelIn,
                             juce::Slider& spreadKnobIn,
                             juce::Label& spreadLabelIn,
                             juce::Slider& degradeKnobIn,
                             juce::Label& degradeLabelIn,
                             juce::ComboBox& routingBoxIn,
                             juce::Label& routingLabelIn,
                             juce::ComboBox& wetModeBoxIn,
                             juce::Label& wetModeLabelIn,
                             juce::ComboBox& loopModeBoxIn,
                             juce::Label& loopModeLabelIn,
                             juce::Colour accentIn)
    : enabledButton(enabledButtonIn),
      freezeButton(freezeButtonIn),
      mixKnob(mixKnobIn),
      mixLabel(mixLabelIn),
      clockKnob(clockKnobIn),
      clockLabel(clockLabelIn),
      wetTimeKnob(wetTimeKnobIn),
      wetTimeLabel(wetTimeLabelIn),
      wetModifyKnob(wetModifyKnobIn),
      wetModifyLabel(wetModifyLabelIn),
      loopLengthKnob(loopLengthKnobIn),
      loopLengthLabel(loopLengthLabelIn),
      loopModifyKnob(loopModifyKnobIn),
      loopModifyLabel(loopModifyLabelIn),
      feedbackKnob(feedbackKnobIn),
      feedbackLabel(feedbackLabelIn),
      spreadKnob(spreadKnobIn),
      spreadLabel(spreadLabelIn),
      degradeKnob(degradeKnobIn),
      degradeLabel(degradeLabelIn),
      routingBox(routingBoxIn),
      routingLabel(routingLabelIn),
      wetModeBox(wetModeBoxIn),
      wetModeLabel(wetModeLabelIn),
      loopModeBox(loopModeBoxIn),
      loopModeLabel(loopModeLabelIn),
      accent(accentIn)
{
    addAndMakeVisible(enabledButton);
    // Restored. It was already attached to the freeze parameter in
    // PluginEditor - only the call that put it on screen was commented out - so
    // showing it reconnects the existing control rather than adding a new one.
    addAndMakeVisible(freezeButton);

    addAndMakeVisible(mixKnob);
    addAndMakeVisible(mixLabel);
    addAndMakeVisible(clockKnob);
    addAndMakeVisible(clockLabel);
    addAndMakeVisible(wetTimeKnob);
    addAndMakeVisible(wetTimeLabel);
    addAndMakeVisible(wetModifyKnob);
    addAndMakeVisible(wetModifyLabel);
    addAndMakeVisible(loopLengthKnob);
    addAndMakeVisible(loopLengthLabel);
    addAndMakeVisible(loopModifyKnob);
    addAndMakeVisible(loopModifyLabel);
    addAndMakeVisible(feedbackKnob);
    addAndMakeVisible(feedbackLabel);
    addAndMakeVisible(spreadKnob);
    addAndMakeVisible(spreadLabel);
    addAndMakeVisible(degradeKnob);
    addAndMakeVisible(degradeLabel);

    addAndMakeVisible(routingBox);
    addAndMakeVisible(routingLabel);
    addAndMakeVisible(wetModeBox);
    addAndMakeVisible(wetModeLabel);
    addAndMakeVisible(loopModeBox);
    addAndMakeVisible(loopModeLabel);
}

void MoodComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void MoodComponent::setActive(bool enabled)
{
    isActive = enabled;

    // Freeze follows bypass like every other control. It was absent from this
    // list only because the button was not on screen; now that it is, leaving
    // it out would make it the one live control on a bypassed card.
    freezeButton.setEnabled(enabled);

    mixKnob.setEnabled(enabled);
    clockKnob.setEnabled(enabled);
    wetTimeKnob.setEnabled(enabled);
    wetModifyKnob.setEnabled(enabled);
    loopLengthKnob.setEnabled(enabled);
    loopModifyKnob.setEnabled(enabled);
    feedbackKnob.setEnabled(enabled);
    spreadKnob.setEnabled(enabled);
    degradeKnob.setEnabled(enabled);

    routingBox.setEnabled(enabled);
    wetModeBox.setEnabled(enabled);
    loopModeBox.setEnabled(enabled);

    mixLabel.setEnabled(enabled);
    clockLabel.setEnabled(enabled);
    wetTimeLabel.setEnabled(enabled);
    wetModifyLabel.setEnabled(enabled);
    loopLengthLabel.setEnabled(enabled);
    loopModifyLabel.setEnabled(enabled);
    feedbackLabel.setEnabled(enabled);
    spreadLabel.setEnabled(enabled);
    degradeLabel.setEnabled(enabled);
    routingLabel.setEnabled(enabled);
    wetModeLabel.setEnabled(enabled);
    loopModeLabel.setEnabled(enabled);

    mixKnob.getProperties().set("psychedelicBypassGray", !enabled);
    clockKnob.getProperties().set("psychedelicBypassGray", !enabled);
    wetTimeKnob.getProperties().set("psychedelicBypassGray", !enabled);
    wetModifyKnob.getProperties().set("psychedelicBypassGray", !enabled);
    loopLengthKnob.getProperties().set("psychedelicBypassGray", !enabled);
    loopModifyKnob.getProperties().set("psychedelicBypassGray", !enabled);
    feedbackKnob.getProperties().set("psychedelicBypassGray", !enabled);
    spreadKnob.getProperties().set("psychedelicBypassGray", !enabled);
    degradeKnob.getProperties().set("psychedelicBypassGray", !enabled);

    repaint();
}

void MoodComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    // cardInner parses its rows in resized(), so a live reload has to redo
    // the layout as well as the paint.
    resized();
    repaint();
}

void MoodComponent::resized()
{
    card.setStyleKey("mood");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    inner.setStylePath("cards.mood.cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(3);
    inner.layout(card.contentBelowTitle());

    using px3::ui::ControlShape;

    // Row 1: bypass and freeze, each with its caption painted beside it.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        flex.items.add(juce::FlexItem(24.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(26.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(24.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(46.0f, cellHeight).withMargin(gap));
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        enabledButton.setBounds(juce::Rectangle<int>(22, 22).withCentre(cell(0).getCentre()));
        onLabelBounds = cell(1);
        freezeButton.setBounds(juce::Rectangle<int>(22, 22).withCentre(cell(2).getCentre()));
        freezeLabelBounds = cell(3);
    }

    // Row 2: the wet and loop mode dropdowns, plus routing. Routing is not in
    // the spec's list for this row, but it is a real control that has to live
    // somewhere, and it is a dropdown like the other two.
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);
        const auto rowWidth = static_cast<float>(juce::jmax(1, row.getWidth()));

        const std::vector<float> widths { 96.0f, 96.0f, 96.0f };
        const auto gapWidth = gap.left + gap.right;
        const auto lines = px3::ui::wrappedLineCount(widths, gapWidth, rowWidth);
        const auto cellHeight = juce::jmax(1.0f,
                                           static_cast<float>(row.getHeight()) / static_cast<float>(lines)
                                               - (gap.top + gap.bottom));

        for (const auto width : widths)
        {
            flex.items.add(juce::FlexItem(width, cellHeight).withMargin(gap));
        }
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        px3::ui::layoutLabelledControl(cell(0), &wetModeLabel, &wetModeBox, nullptr,
                                       14, 0, ControlShape::stretch, 22);
        px3::ui::layoutLabelledControl(cell(1), &loopModeLabel, &loopModeBox, nullptr,
                                       14, 0, ControlShape::stretch, 22);
        px3::ui::layoutLabelledControl(cell(2), &routingLabel, &routingBox, nullptr,
                                       14, 0, ControlShape::stretch, 22);
    }

    // Row 3: all nine knobs in one wrapping row. This replaces a hand-built
    // 3x3 grid; the row wraps into however many lines the width allows, which
    // is what makes the arrangement follow the card rather than a fixed shape.
    {
        auto flex = inner.rowFlex(2);
        const auto gap = inner.rowGap(2);
        const auto row = inner.rowContent(2);
        const auto rowWidth = static_cast<float>(juce::jmax(1, row.getWidth()));

        const std::array<std::pair<juce::Slider*, juce::Label*>, 9> knobs { {
            { &wetTimeKnob, &wetTimeLabel },
            { &wetModifyKnob, &wetModifyLabel },
            { &loopLengthKnob, &loopLengthLabel },
            { &loopModifyKnob, &loopModifyLabel },
            { &feedbackKnob, &feedbackLabel },
            { &spreadKnob, &spreadLabel },
            { &clockKnob, &clockLabel },
            { &mixKnob, &mixLabel },
            { &degradeKnob, &degradeLabel },
        } };

        const std::vector<float> widths(knobs.size(), 64.0f);
        const auto gapWidth = gap.left + gap.right;
        const auto lines = px3::ui::wrappedLineCount(widths, gapWidth, rowWidth);
        const auto cellHeight = juce::jmax(1.0f,
                                           static_cast<float>(row.getHeight()) / static_cast<float>(lines)
                                               - (gap.top + gap.bottom));

        for (const auto width : widths)
        {
            flex.items.add(juce::FlexItem(width, cellHeight).withMargin(gap));
        }
        flex.performLayout(row.toFloat());

        for (std::size_t i = 0; i < knobs.size(); ++i)
        {
            px3::ui::layoutLabelledControl(flex.items.getReference(static_cast<int>(i)).currentBounds.toNearestInt(),
                                           nullptr, knobs[i].first, knobs[i].second,
                                           0, 16, ControlShape::square, 64);
        }
    }
}

void MoodComponent::paint(juce::Graphics& g)
{
    // Card and title are owned here, not by FxPanel. Because the card follows
    // this component's bounds, drag-and-drop reordering moves it automatically -
    // the panel no longer has to paint anything at the dragged position.
    card.setStyleKey("mood");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    if (isActive)
    {
        card.draw(g, "MOOD");
    }
    else
    {
        card.drawInactive(g, "MOOD");
    }

    const auto textColour = uiConfig != nullptr
                                ? uiConfig->getColour("fx.mood.visual.onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat("fx.mood.visual.onLabel.fontSize", 11.5f) : 11.5f;
    // Both captions come from row 1, beside the buttons they name.
    g.setColour(textColour.withAlpha(isActive ? 1.0f : 0.6f));
    g.setFont(juce::FontOptions(fontSize));
    g.drawText("ON", onLabelBounds, juce::Justification::centredLeft, false);
    g.drawText("Freeze", freezeLabelBounds, juce::Justification::centredLeft, false);
}
