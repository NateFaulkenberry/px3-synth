#include "DelayComponent.h"

#include "CardInner.h"

#include "UIConfig.h"

DelayComponent::DelayComponent(juce::ToggleButton& enabledButtonIn,
                                         juce::Slider& amountKnobIn,
                                         juce::Label& amountLabelIn,
                                         juce::ComboBox& algorithmBoxIn,
                                         juce::Label& algorithmLabelIn,
                                         juce::ComboBox& syncBoxIn,
                                         juce::Label& syncLabelIn,
                                         juce::ComboBox& modeBoxIn,
                                         juce::Label& modeLabelIn,
                                         juce::Slider& timeKnobIn,
                                         juce::Label& timeLabelIn,
                                         juce::Slider& feedbackKnobIn,
                                         juce::Label& feedbackLabelIn,
                                         juce::Colour accentIn)
    : enabledButton(enabledButtonIn),
      amountKnob(amountKnobIn),
      amountLabel(amountLabelIn),
      algorithmBox(algorithmBoxIn),
      algorithmLabel(algorithmLabelIn),
      syncBox(syncBoxIn),
      syncLabel(syncLabelIn),
      modeBox(modeBoxIn),
      modeLabel(modeLabelIn),
      timeKnob(timeKnobIn),
      timeLabel(timeLabelIn),
      feedbackKnob(feedbackKnobIn),
      feedbackLabel(feedbackLabelIn),
      accent(accentIn)
{
    addAndMakeVisible(enabledButton);
    addAndMakeVisible(amountKnob);
    addAndMakeVisible(amountLabel);
    addAndMakeVisible(algorithmBox);
    addAndMakeVisible(algorithmLabel);
    addAndMakeVisible(syncBox);
    addAndMakeVisible(syncLabel);
    addAndMakeVisible(modeBox);
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(timeKnob);
    addAndMakeVisible(timeLabel);
    addAndMakeVisible(feedbackKnob);
    addAndMakeVisible(feedbackLabel);
}

void DelayComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void DelayComponent::setActive(bool enabled, bool granularModeSelectable)
{
    isActive = enabled;

    amountKnob.setEnabled(isActive);
    amountLabel.setEnabled(isActive);
    algorithmBox.setEnabled(isActive);
    algorithmLabel.setEnabled(isActive);
    timeKnob.setEnabled(isActive);
    timeLabel.setEnabled(isActive);
    feedbackKnob.setEnabled(isActive);
    feedbackLabel.setEnabled(isActive);
    syncBox.setEnabled(isActive);
    syncLabel.setEnabled(isActive);
    modeBox.setEnabled(granularModeSelectable);
    modeLabel.setEnabled(granularModeSelectable);

    amountKnob.getProperties().set("psychedelicBypassGray", !isActive);
    timeKnob.getProperties().set("psychedelicBypassGray", !isActive);
    feedbackKnob.getProperties().set("psychedelicBypassGray", !isActive);

    repaint();
}

void DelayComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    // cardInner parses its rows in resized(), so a live reload has to redo
    // the layout as well as the paint.
    resized();
    repaint();
}

void DelayComponent::resized()
{
    card.setStyleKey("delay");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    inner.setKeys("cards.defaults.cardInner", "cards.delay.cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(3);
    inner.layout(card.contentBelowTitle());

    using px3::ui::ControlShape;

    // Row 1: the bypass button and the painted "ON" text beside it.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        flex.items.add(juce::FlexItem(24.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(26.0f, cellHeight).withMargin(gap));
        flex.performLayout(row.toFloat());

        enabledButton.setBounds(juce::Rectangle<int>(22, 22)
                                    .withCentre(flex.items.getReference(0).currentBounds.toNearestInt().getCentre()));
        onLabelBounds = flex.items.getReference(1).currentBounds.toNearestInt();
    }

    // Row 2: the amount knob with its label below.
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);

        flex.items.add(juce::FlexItem(100.0f, static_cast<float>(juce::jmax(1, row.getHeight())))
                           .withMargin(gap));
        flex.performLayout(row.toFloat());

        px3::ui::layoutLabelledControl(flex.items.getReference(0).currentBounds.toNearestInt(),
                                       nullptr, &amountKnob, &amountLabel,
                                       0, 18, ControlShape::square, 80);
    }

    // Row 3: five controls in one wrapping row - the two small knobs and the
    // three dropdowns. The spec is explicit that this must not become a fourth
    // row; the row wraps instead, and how it wraps is UIConfig's business.
    {
        auto flex = inner.rowFlex(2);
        const auto gap = inner.rowGap(2);
        const auto row = inner.rowContent(2);
        const auto rowWidth = static_cast<float>(juce::jmax(1, row.getWidth()));

        const std::vector<float> widths { 60.0f, 60.0f, 104.0f, 104.0f, 104.0f };
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
        px3::ui::layoutLabelledControl(cell(0), nullptr, &timeKnob, &timeLabel,
                                       0, 16, ControlShape::square, 44);
        px3::ui::layoutLabelledControl(cell(1), nullptr, &feedbackKnob, &feedbackLabel,
                                       0, 16, ControlShape::square, 44);
        px3::ui::layoutLabelledControl(cell(2), &syncLabel, &syncBox, nullptr,
                                       14, 0, ControlShape::stretch, 22);
        px3::ui::layoutLabelledControl(cell(3), &algorithmLabel, &algorithmBox, nullptr,
                                       14, 0, ControlShape::stretch, 22);
        px3::ui::layoutLabelledControl(cell(4), &modeLabel, &modeBox, nullptr,
                                       14, 0, ControlShape::stretch, 22);
    }
}

void DelayComponent::paint(juce::Graphics& g)
{
    // Card and title are owned here, not by FxPanel. Because the card follows
    // this component's bounds, drag-and-drop reordering moves it automatically -
    // the panel no longer has to paint anything at the dragged position.
    card.setStyleKey("delay");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    if (isActive)
    {
        card.draw(g, "DELAY");
    }
    else
    {
        card.drawInactive(g, "DELAY");
    }

    const auto textColour = uiConfig != nullptr
                                ? uiConfig->getColour("fx.delay.visual.onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat("fx.delay.visual.onLabel.fontSize", 11.5f) : 11.5f;
    // Beside the button, wherever row 1 put it.
    const auto textBounds = onLabelBounds;

    g.setColour(textColour.withAlpha(isActive ? 1.0f : 0.6f));
    g.setFont(juce::FontOptions(fontSize));
    g.drawText("ON", textBounds, juce::Justification::centredLeft, false);
}
