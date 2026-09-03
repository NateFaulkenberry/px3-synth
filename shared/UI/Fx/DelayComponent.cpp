#include "DelayComponent.h"

#include "BypassButton.h"
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
    // The card background toggles this section, so the pointer says it is
    // clickable. Child controls carry their own cursors.
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

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

    // The power glyph lights in this card's own identity colour.

    if (auto* power = dynamic_cast<px3::ui::BypassButton*>(&enabledButton))

    {

        power->setAccentColour(card.style().border.colour);

    }


    inner.setStylePath("cards.delay.cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(2);
    inner.layout(card.contentBelowTitle());

    // The power toggle is pinned to cardInner's corner, outside the flex flow,
    // so it stays put no matter what the first row contains.
    enabledButton.setBounds(inner.powerBounds());

    using px3::ui::ControlShape;


    // Row 1: the amount knob with its label below.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);

        flex.items.add(juce::FlexItem(100.0f, static_cast<float>(juce::jmax(1, row.getHeight())))
                           .withMargin(gap));
        flex.performLayout(row.toFloat());

        px3::ui::layoutLabelledControl(flex.items.getReference(0).currentBounds.toNearestInt(),
                                       { nullptr, &amountKnob, &amountLabel,
                                         ControlShape::square, 0, 18, 80 },
                                       inner.rowControl(0));
    }

    // Row 2: five controls in one wrapping row - the two small knobs and the
    // three dropdowns. The spec is explicit that this must not become a fourth
    // row; the row wraps instead, and how it wraps is UIConfig's business.
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);
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
        px3::ui::layoutLabelledControl(cell(0),
                                       { nullptr, &timeKnob, &timeLabel,
                                         ControlShape::square, 0, 16, 44 },
                                       inner.rowControl(1));
        px3::ui::layoutLabelledControl(cell(1),
                                       { nullptr, &feedbackKnob, &feedbackLabel,
                                         ControlShape::square, 0, 16, 44 },
                                       inner.rowControl(1));
        px3::ui::layoutLabelledControl(cell(2),
                                       { &syncLabel, &syncBox, nullptr,
                                         ControlShape::stretch, 14, 0, 22 },
                                       inner.rowControl(1));
        px3::ui::layoutLabelledControl(cell(3),
                                       { &algorithmLabel, &algorithmBox, nullptr,
                                         ControlShape::stretch, 14, 0, 22 },
                                       inner.rowControl(1));
        px3::ui::layoutLabelledControl(cell(4),
                                       { &modeLabel, &modeBox, nullptr,
                                         ControlShape::stretch, 14, 0, 22 },
                                       inner.rowControl(1));
    }
}

void DelayComponent::mouseUp(const juce::MouseEvent& event)
{
    // Clicking the card's background toggles its power, the same as clicking
    // the button. No tooltip here: the whole card is not a control, and a card
    // that explained itself on hover would be noise.
    if (px3::ui::isCardBackgroundToggleClick(event))
    {
        enabledButton.setToggleState(! enabledButton.getToggleState(), juce::sendNotification);
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

}
