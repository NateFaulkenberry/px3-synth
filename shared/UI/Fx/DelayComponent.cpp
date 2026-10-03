#include "DelayComponent.h"
#include "ChipLabel.h"

#include "BypassButton.h"
#include "CardInner.h"

#include "UIConfig.h"

#include <algorithm>

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

void DelayComponent::setAlgorithmControls(const AlgorithmControls& controls)
{
    algorithmControls = controls;
    for (auto [slider, label] : { std::pair { controls.quality, controls.qualityLabel },
                                  std::pair { controls.wobble, controls.wobbleLabel },
                                  std::pair { controls.slip, controls.slipLabel },
                                  std::pair { controls.modDepth, controls.modDepthLabel } })
    {
        if (slider != nullptr) { addChildComponent(*slider); }
        if (label != nullptr) { addChildComponent(*label); }
    }
    setAlgorithm(algorithm);
}

std::vector<std::pair<juce::Slider*, juce::Label*>> DelayComponent::shownAlgorithmControls() const
{
    std::vector<std::pair<juce::Slider*, juce::Label*>> shown;
    const auto& c = algorithmControls;
    if (algorithm == 1)
    {
        for (auto pair : { std::pair { c.quality, c.qualityLabel }, std::pair { c.wobble, c.wobbleLabel },
                           std::pair { c.slip, c.slipLabel } })
        {
            if (pair.first != nullptr) { shown.push_back(pair); }
        }
    }
    else if (algorithm == 5 && c.modDepth != nullptr)
    {
        shown.push_back({ c.modDepth, c.modDepthLabel });
    }
    return shown;
}

int DelayComponent::visibleAlgorithmControlCount() const noexcept
{
    return static_cast<int>(shownAlgorithmControls().size());
}

void DelayComponent::setAlgorithm(int algorithmIndex)
{
    const auto changed = algorithm != algorithmIndex;
    algorithm = algorithmIndex;
    const auto shown = shownAlgorithmControls();
    const auto& c = algorithmControls;
    for (auto [slider, label] : { std::pair { c.quality, c.qualityLabel }, std::pair { c.wobble, c.wobbleLabel },
                                  std::pair { c.slip, c.slipLabel }, std::pair { c.modDepth, c.modDepthLabel } })
    {
        const auto visible = std::find_if(shown.begin(), shown.end(),
                                          [s = slider](const auto& p) { return p.first == s; }) != shown.end();
        if (slider != nullptr) { slider->setVisible(visible); }
        if (label != nullptr) { label->setVisible(visible); }
    }
    if (changed || ! shown.empty()) { resized(); }
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
    for (auto* slider : { algorithmControls.quality, algorithmControls.wobble,
                          algorithmControls.slip, algorithmControls.modDepth })
    {
        if (slider == nullptr) { continue; }
        slider->setEnabled(isActive);
        slider->getProperties().set("psychedelicBypassGray", !isActive);
    }
    px3::ui::ChipLabel::setGreyedOut(!isActive,
                                     { &amountLabel, &algorithmLabel, &syncLabel,
                                       &modeLabel, &timeLabel, &feedbackLabel });

    repaint();
}

void DelayComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    // The captions this component was handed. It does not own them, but it
    // is the only place that knows which style key they belong to - so the
    // card's chip colours reach them here or not at all.
    px3::ui::ChipLabel::applyFromConfig(uiConfig.get(), "delay",
                                        { &amountLabel, &algorithmLabel, &syncLabel, &modeLabel, &timeLabel, &feedbackLabel });
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
    const auto extras = shownAlgorithmControls();
    inner.setRowCount(extras.empty() ? 2 : 3);
    inner.layout(card.contentBelowTitle());

    // The power toggle is pinned to cardInner's corner, outside the flex flow,
    // so it stays put no matter what the first row contains.
    enabledButton.setBounds(card.powerBounds());

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

    // Row 3: the controls this algorithm has and the others do not.
    if (! extras.empty())
    {
        auto flex = inner.rowFlex(2);
        const auto gap = inner.rowGap(2);
        const auto row = inner.rowContent(2);
        for (std::size_t i = 0; i < extras.size(); ++i)
        {
            flex.items.add(juce::FlexItem(60.0f, static_cast<float>(juce::jmax(1, row.getHeight()))).withMargin(gap));
        }
        flex.performLayout(row.toFloat());
        for (std::size_t i = 0; i < extras.size(); ++i)
        {
            px3::ui::layoutLabelledControl(flex.items.getReference(static_cast<int>(i)).currentBounds.toNearestInt(),
                                           { nullptr, extras[i].first, extras[i].second,
                                             ControlShape::square, 0, 16, 44 },
                                           inner.rowControl(2));
        }
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
