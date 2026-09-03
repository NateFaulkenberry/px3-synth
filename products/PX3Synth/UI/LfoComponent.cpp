#include "LfoComponent.h"

#include "BypassButton.h"
#include "CardInner.h"

#include "LfoMode.h"
#include "UIConfig.h"

#include <cmath>

juce::PopupMenu::Options LfoComponent::WaveformComboLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                                                  juce::Label& label)
{
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label);
    // Parented to the EDITOR, not to the combo's immediate parent.
    //
    // A menu hosted inside a component is clipped to that component's bounds.
    // The immediate parent here is one card, which is often shorter than the
    // menu - so the lower items were drawn clipped and could not be clicked,
    // which is why selecting an item sometimes appeared to do nothing and took
    // several attempts. The editor is large enough to hold the whole menu, and
    // keeping it in-window still suits hosts that dislike desktop-level popups.
    auto* host = box.getTopLevelComponent();
    return options.withParentComponent(host != nullptr ? host : box.getParentComponent())
                  .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards);
}

LfoComponent::LfoComponent(juce::ToggleButton& enabledButtonIn,
                                                     juce::Label& assignLabelIn,
                                                     juce::ComboBox& assignBoxIn,
                                                     juce::Slider& rateKnobIn,
                                                     juce::Label& rateLabelIn,
                                                     juce::Label& rateValueLabelIn,
                                     juce::Slider& amountKnobIn,
                                     juce::Label& amountLabelIn,
                                     juce::Label& amountValueLabelIn,
                                                     juce::ComboBox& waveformBoxIn,
                                                     juce::Label& waveformLabelIn,
                                                     juce::Colour accentIn,
                                                     const juce::String& configPrefixIn)
        : enabledButton(enabledButtonIn),
            rateKnob(rateKnobIn),
      rateLabel(rateLabelIn),
      rateValueLabel(rateValueLabelIn),
    amountKnob(amountKnobIn),
    amountLabel(amountLabelIn),
    amountValueLabel(amountValueLabelIn),
      assignLabel(assignLabelIn),
      assignBox(assignBoxIn),
      waveformBox(waveformBoxIn),
      waveformLabel(waveformLabelIn),
    accent(accentIn),
    configPrefix(configPrefixIn)
{
    // The card background toggles this section, so the pointer says it is
    // clickable. Child controls carry their own cursors.
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

        addAndMakeVisible(enabledButton);
    baseRateValueTextColour = rateValueLabel.findColour(juce::Label::textColourId);
    baseAmountValueTextColour = amountValueLabel.findColour(juce::Label::textColourId);
    addAndMakeVisible(rateKnob);
    addAndMakeVisible(rateLabel);
    addAndMakeVisible(rateValueLabel);
    addAndMakeVisible(amountKnob);
    addAndMakeVisible(amountLabel);
    addAndMakeVisible(amountValueLabel);
    assignBox.setLookAndFeel(&waveformComboLookAndFeel);
    addAndMakeVisible(assignLabel);
    addAndMakeVisible(assignBox);
    waveformBox.setLookAndFeel(&waveformComboLookAndFeel);
    addAndMakeVisible(waveformBox);
    addAndMakeVisible(waveformLabel);
}

LfoComponent::~LfoComponent()
{
    assignBox.setLookAndFeel(nullptr);
    waveformBox.setLookAndFeel(nullptr);
}

void LfoComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void LfoComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);


    // Re-run the layout, not just the paint: cardInner parses its rows in
    // resized(), so a repaint alone would draw the new colours into the old
    // geometry.
    resized();
    repaint();
}

void LfoComponent::refreshFromParameters(bool enabled, float rateHz, float amount, int waveformIndex)
{
    const auto enabledChanged = currentEnabled != enabled;
    currentEnabled = enabled;

    enabledButton.setToggleState(currentEnabled, juce::dontSendNotification);
    assignBox.setEnabled(currentEnabled);
    assignLabel.setEnabled(currentEnabled);
    waveformBox.setEnabled(currentEnabled);
    waveformLabel.setEnabled(currentEnabled);
    rateKnob.setEnabled(currentEnabled);
    rateKnob.setInterceptsMouseClicks(currentEnabled, currentEnabled);
    rateKnob.getProperties().set("knobBypassed", !currentEnabled);
    rateKnob.getProperties().set("psychedelicBypassGray", !currentEnabled);
    amountKnob.setEnabled(currentEnabled);
    amountKnob.setInterceptsMouseClicks(currentEnabled, currentEnabled);
    amountKnob.getProperties().set("knobBypassed", !currentEnabled);
    amountKnob.getProperties().set("psychedelicBypassGray", !currentEnabled);
    rateLabel.setEnabled(currentEnabled);
    rateValueLabel.setEnabled(currentEnabled);
    amountLabel.setEnabled(currentEnabled);
    amountValueLabel.setEnabled(currentEnabled);
    const auto disabledRateValueColour = juce::Colour::fromRGB(178, 178, 178);
    const auto disabledAmountValueColour = juce::Colour::fromRGB(178, 178, 178);
    rateValueLabel.setColour(juce::Label::textColourId,
                             currentEnabled ? baseRateValueTextColour : disabledRateValueColour);
    amountValueLabel.setColour(juce::Label::textColourId,
                               currentEnabled ? baseAmountValueTextColour : disabledAmountValueColour);

    currentRateHz = juce::jlimit(0.01f, 20.0f, rateHz);
    rateValueLabel.setText(juce::String(currentRateHz, 2) + " Hz", juce::dontSendNotification);
    currentAmount = juce::jlimit(-1.0f, 1.0f, amount);
    const auto amountPercent = static_cast<int>(std::lround(currentAmount * 100.0f));
    const auto amountPrefix = amountPercent > 0 ? juce::String("+") : juce::String();
    amountValueLabel.setText(amountPrefix + juce::String(amountPercent) + "%", juce::dontSendNotification);

    const auto clamped = px3::clampLfoWaveformIndex(waveformIndex);
    currentWaveformIndex = clamped;
    if (waveformBox.getSelectedItemIndex() != clamped)
    {
        waveformBox.setSelectedItemIndex(clamped, juce::dontSendNotification);
    }

    if (enabledChanged)
    {
        rateKnob.repaint();
        amountKnob.repaint();
        repaint();
    }
}

void LfoComponent::advanceAnimation(float deltaSeconds)
{
    if (!currentEnabled)
    {
        return;
    }

    const auto clampedDeltaSeconds = juce::jlimit(1.0f / 120.0f, 0.2f, deltaSeconds);
    const auto phaseAdvance = juce::MathConstants<float>::twoPi * currentRateHz * clampedDeltaSeconds;
    visualPhase = std::fmod(visualPhase + phaseAdvance, juce::MathConstants<float>::twoPi);

    repaint();
}

void LfoComponent::resized()
{
    // resized() used to derive its own card rectangle - reduced(6,6), clamped
    // to 300px, reduced(10,10) - in parallel with the one paint() asked the
    // CardHost for. Two independent ideas of where the card was is exactly the
    // dependency that stopped cardInner working here, so the layout now comes
    // from the card, as it already did for Sub Osc and Osc.
    card.setStyleKey(configPrefix.fromLastOccurrenceOf(".", false, false));
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());
    // The wave graph is drawn in this card's identity colour, so a card
    // recoloured in UIConfig recolours its graph with it rather than
    // keeping a group accent baked in at construction.
    accent = card.style().border.colour;


    // The power glyph lights in this card's own identity colour.

    if (auto* power = dynamic_cast<px3::ui::BypassButton*>(&enabledButton))

    {

        power->setAccentColour(card.style().border.colour);

    }


    inner.setStylePath("cards.lfo.cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(3);
    inner.layout(card.contentBelowTitle());

    // The power toggle is pinned to cardInner's corner, outside the flex flow,
    // so it stays put no matter what the first row contains.
    enabledButton.setBounds(inner.powerBounds());

    using px3::ui::ControlShape;

    // Row 1: bypass, assign and wave type.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        flex.items.add(juce::FlexItem(84.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(84.0f, cellHeight).withMargin(gap));
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        px3::ui::layoutLabelledControl(cell(0),
                                       { &assignLabel, &assignBox, nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       inner.rowControl(0));
        px3::ui::layoutLabelledControl(cell(1),
                                       { &waveformLabel, &waveformBox, nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       inner.rowControl(0));
    }

    // Row 2: rate and amount, each name-knob-value. Both cells declare the same
    // label height, readout height and cap, which is what makes the two knobs
    // come out the same size - a hidden label on one of them was enough to make
    // them differ.
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        for (int i = 0; i < 2; ++i)
        {
            auto item = juce::FlexItem(84.0f, cellHeight).withMargin(gap);
            item.flexGrow = 1.0f;
            flex.items.add(item);
        }
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        // Name above, knob, value below. The names used to be hidden, which left
        // two unlabelled knobs telling you only "0.54 Hz" and "+100%".
        px3::ui::layoutLabelledControl(cell(0),
                                       { &rateLabel, &rateKnob, &rateValueLabel,
                                         ControlShape::square, 16, 20, 84 },
                                       inner.rowControl(1));
        px3::ui::layoutLabelledControl(cell(1),
                                       { &amountLabel, &amountKnob, &amountValueLabel,
                                         ControlShape::square, 16, 20, 84 },
                                       inner.rowControl(1));
    }

    // Row 3 is the wave graph, drawn by paint().
}

void LfoComponent::mouseUp(const juce::MouseEvent& event)
{
    // Clicking the card's background toggles its power, the same as clicking
    // the button. No tooltip here: the whole card is not a control, and a card
    // that explained itself on hover would be noise.
    // The wave graph is a display, not a switch: it shows no pointer and it
    // takes no click, so the two agree.
    if (inner.rowContent(2).contains(event.getPosition()))
    {
        return;
    }

    if (px3::ui::isCardBackgroundToggleClick(event))
    {
        enabledButton.setToggleState(! enabledButton.getToggleState(), juce::sendNotification);
    }
}

void LfoComponent::mouseMove(const juce::MouseEvent& event)
{
    // The wave graph is a display, not a control, so it does not take the
    // pointer that marks the rest of the card as clickable.
    setMouseCursor(inner.rowContent(2).contains(event.getPosition())
                       ? juce::MouseCursor::NormalCursor
                       : juce::MouseCursor::PointingHandCursor);
}

void LfoComponent::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    card.setPanelContentBounds(panelContent);
    repaint();
}

void LfoComponent::paint(juce::Graphics& g)
{
    // The card and its title live here. ModPanel used to draw the title into
    // this component's bounds, which is the parent-owns-child's-pixels pattern
    // this refactor removes everywhere it appears.
    card.setStyleKey(configPrefix.fromLastOccurrenceOf(".", false, false));
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    const auto title = configPrefix.fromLastOccurrenceOf(".", false, false)
                           .toUpperCase().replace("LFO", "LFO ");
    if (currentEnabled)
    {
        card.draw(g, title);
    }
    else
    {
        card.drawInactive(g, title);
    }

    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);
    juce::ignoreUnused(effectiveAccent);

    // The graph is row 3. It used to be found by replaying resized()'s stack of
    // removeFromTop calls against a separately-derived card rectangle.
    const auto graph = inner.rowContent(2).toFloat().reduced(0.0f, 2.0f);

    if (graph.getWidth() < 40.0f || graph.getHeight() < 20.0f)
    {
        return;
    }

    g.setColour(juce::Colour::fromRGBA(14, 14, 18, 170));
    g.fillRoundedRectangle(graph, 7.0f);
    g.setColour(effectiveAccent.withAlpha(0.32f));
    g.drawRoundedRectangle(graph, 7.0f, 1.0f);

    const auto left = graph.getX() + 6.0f;
    const auto right = graph.getRight() - 6.0f;
    const auto top = graph.getY() + 5.0f;
    const auto bottom = graph.getBottom() - 5.0f;
    const auto mid = (top + bottom) * 0.5f;

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
    g.drawLine(left, mid, right, mid, 0.9f);
    for (int gx = 1; gx < 6; ++gx)
    {
        const auto x = left + (right - left) * (static_cast<float>(gx) / 6.0f);
        g.drawLine(x, top, x, bottom, 0.7f);
    }

    juce::Path wave;
    const auto width = juce::jmax(1.0f, right - left);
    const auto height = juce::jmax(1.0f, bottom - top);
    for (int s = 0; s <= 72; ++s)
    {
        const auto t = static_cast<float>(s) / 72.0f;
        const auto phaseNorm = std::fmod(t + visualPhase / juce::MathConstants<float>::twoPi, 1.0f);
        const auto y = waveformSample(phaseNorm, currentWaveformIndex);
        const auto xPos = left + t * width;
        const auto yPos = mid - juce::jlimit(-1.0f, 1.0f, y) * (height * 0.40f);

        if (s == 0)
        {
            wave.startNewSubPath(xPos, yPos);
        }
        else
        {
            wave.lineTo(xPos, yPos);
        }
    }

    g.setColour(effectiveAccent.withAlpha(currentEnabled ? 0.70f : 0.42f));
    g.strokePath(wave,
                 juce::PathStrokeType(2.6f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
    const auto waveDetailColour = currentEnabled ? juce::Colour::fromRGB(232, 240, 255)
                                                 : juce::Colour::fromRGB(178, 178, 178);
    g.setColour(waveDetailColour);
    g.strokePath(wave,
                 juce::PathStrokeType(1.2f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
}

float LfoComponent::waveformSample(float phaseNorm, int waveformIndex)
{
    const auto p = phaseNorm - std::floor(phaseNorm);

    switch (px3::clampLfoWaveformIndex(waveformIndex))
    {
        case 0:
            return std::sin(p * juce::MathConstants<float>::twoPi);
        case 1:
            return 1.0f - 4.0f * std::abs(p - 0.5f);
        case 2:
            return p * 2.0f - 1.0f;
        case 3:
            return p < 0.5f ? 1.0f : -1.0f;
        default:
            break;
    }

    return std::sin(p * juce::MathConstants<float>::twoPi);
}
