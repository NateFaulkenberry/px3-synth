#include "LfoComponent.h"
#include "ComboSync.h"
#include "LfoGenerator.h"

#include "BypassButton.h"
#include "CardInner.h"
#include "CompactModuleLayout.h"
#include "ParameterKnob.h"

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
                                                     juce::Slider& rateKnobIn,
                                                     juce::Label& rateLabelIn,
                                                     juce::Label& rateValueLabelIn,
                                                     juce::ComboBox& waveformBoxIn,
                                                     juce::Label& waveformLabelIn,
                                                     juce::Colour accentIn,
                                                     const juce::String& configPrefixIn)
        : enabledButton(enabledButtonIn),
            rateKnob(rateKnobIn),
      rateLabel(rateLabelIn),
      rateValueLabel(rateValueLabelIn),
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
    graphView.paintStill = [this](juce::Graphics& g) { paintGraphStill(g); };
    graphView.paintMoving = [this](juce::Graphics& g) { paintGraphMoving(g); };
    addAndMakeVisible(graphView);
    addAndMakeVisible(rateKnob);
    addAndMakeVisible(rateLabel);
    addAndMakeVisible(rateValueLabel);
    waveformBox.setLookAndFeel(&waveformComboLookAndFeel);
    addAndMakeVisible(waveformBox);
    addAndMakeVisible(waveformLabel);
}

LfoComponent::~LfoComponent()
{
    waveformBox.setLookAndFeel(nullptr);
    rampTimeKnob.setLookAndFeel(nullptr);
}

void LfoComponent::attachClock(juce::AudioParameterChoice& mode, juce::AudioParameterChoice& division)
{
    clockModeBox.addItemList(mode.choices, 1);
    clockDivisionBox.addItemList(division.choices, 1);
    clockModeLabel.setText("CLOCK", juce::dontSendNotification);
    clockDivisionLabel.setText("DIVISION", juce::dontSendNotification);
    for (auto* label : { &clockModeLabel, &clockDivisionLabel, &clockStatus })
    {
        label->setFont(juce::FontOptions(10.5f));
        label->setColour(juce::Label::textColourId, juce::Colour(0xffdadada));
        addAndMakeVisible(*label);
    }
    // Centred over their boxes, like every other caption on the card.
    clockModeLabel.setJustificationType(juce::Justification::centred);
    clockDivisionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(clockModeBox);
    addAndMakeVisible(clockDivisionBox);
    clockModeBox.setTooltip("LFO clock mode");
    clockDivisionBox.setTooltip("LFO musical division");
    clockModeBox.onChange = [this] { refreshClockControls(); };
    clockModeAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(mode, clockModeBox, nullptr);
    clockDivisionAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(division, clockDivisionBox, nullptr);
    clockControlsAttached = true;
    refreshClockControls();
    resized();
}

void LfoComponent::setClockAvailable(bool available)
{
    clockAvailable = available;
    refreshClockControls();
}

void LfoComponent::refreshClockControls()
{
    if (! clockControlsAttached) { return; }
    const auto free = clockModeBox.getSelectedId() == 1;
    clockModeBox.setEnabled(currentEnabled);
    clockDivisionBox.setEnabled(currentEnabled && ! free);
    rateKnob.setEnabled(currentEnabled && free);
    rampTimeKnob.setEnabled(currentEnabled && free);
    keySyncButton.setEnabled(currentEnabled && clockModeBox.getSelectedId() != 3);
    if (! free) { rateValueLabel.setText("SYNC", juce::dontSendNotification); }
    clockStatus.setText(! free && ! clockAvailable ? "NO HOST CLOCK" : juce::String(), juce::dontSendNotification);
}

void LfoComponent::attachRampAndKeySync(juce::RangedAudioParameter& rampTimeParameter,
                                        juce::RangedAudioParameter& keySyncParameter,
                                        juce::LookAndFeel* knobLookAndFeel)
{
    rampTimeKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    rampTimeKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    if (knobLookAndFeel != nullptr)
    {
        rampTimeKnob.setLookAndFeel(knobLookAndFeel);
    }
    rampTimeKnob.setTooltip("How long RAMP UP and RAMP DOWN take to travel");
    rampTimeKnob.onValueChange = [this]
    {
        if (laidOutForRamp)
        {
            rateValueLabel.setText(formatRampSeconds(rampTimeKnob.getValue()), juce::dontSendNotification);
        }
    };

    // The same caption on and off: the chip's light is the state.
    keySyncButton.setClickingTogglesState(true);
    keySyncButton.setStateLabels("KEY SYNC", "KEY SYNC");
    keySyncButton.setFontSize(10.5f);
    keySyncButton.setTooltip("On: every new note restarts this LFO. "
                             "Off: cyclic shapes run freely, and a ramp restarts on the first note after silence.");

    rampTimeAttachment = px3::ui::makeParameterKnobAttachment(rampTimeParameter, rampTimeKnob);
    keySyncAttachment = std::make_unique<juce::ButtonParameterAttachment>(keySyncParameter, keySyncButton, nullptr);

    // The RATE knob's caption and readout are borrowed while a ramp is shown,
    // so the two knobs are captioned identically apart from the words.
    rateCaptionText = rateLabel.getText();

    addChildComponent(rampTimeKnob);
    addAndMakeVisible(keySyncButton);
    keySyncCaptionSpacer.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(keySyncCaptionSpacer);
    rampControlsAttached = true;
    resized();
}

juce::String LfoComponent::formatRampSeconds(double seconds)
{
    return seconds < 10.0 ? juce::String(seconds, 2) + " s" : juce::String(seconds, 1) + " s";
}

void LfoComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
    graphView.invalidateStill();
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

void LfoComponent::refreshFromParameters(bool enabled, float rateHz, int waveformIndex)
{
    const auto enabledChanged = currentEnabled != enabled;
    currentEnabled = enabled;
    // The clock's say on the rate controls, applied here in the same pass:
    // set live here and then dead again by refreshClockControls on every tick,
    // a synced LFO's rate, ramp and key-sync controls flipped twice a tick and
    // repainted the card 30 times a second.
    const auto clockFree = ! clockControlsAttached || clockModeBox.getSelectedId() == 1;
    const auto rateLive = currentEnabled && clockFree;

    enabledButton.setToggleState(currentEnabled, juce::dontSendNotification);
    waveformBox.setEnabled(currentEnabled);
    waveformLabel.setEnabled(currentEnabled);
    rateKnob.setEnabled(rateLive);
    rateKnob.setInterceptsMouseClicks(rateLive, rateLive);
    rateKnob.getProperties().set("knobBypassed", !currentEnabled);
    rateKnob.getProperties().set("psychedelicBypassGray", !currentEnabled);
    rateLabel.setEnabled(currentEnabled);
    rateValueLabel.setEnabled(currentEnabled);
    const auto disabledRateValueColour = juce::Colour::fromRGB(178, 178, 178);
    rateValueLabel.setColour(juce::Label::textColourId,
                             currentEnabled ? baseRateValueTextColour : disabledRateValueColour);

    currentRateHz = juce::jlimit(0.01f, 20.0f, rateHz);
    rateValueLabel.setText(clockFree ? juce::String(currentRateHz, 2) + " Hz" : juce::String("SYNC"), juce::dontSendNotification);

    const auto clamped = px3::clampLfoWaveformIndex(waveformIndex);
    // The animation tick is not there to show a new shape when animations
    // are off.
    if (clamped != currentWaveformIndex) { graphView.requestFrame(); }
    currentWaveformIndex = clamped;
    px3::ui::syncComboItemIndex(waveformBox, clamped);

    if (rampControlsAttached)
    {
        rampTimeKnob.setEnabled(rateLive);
        rampTimeKnob.setInterceptsMouseClicks(rateLive, rateLive);
        rampTimeKnob.getProperties().set("knobBypassed", !currentEnabled);
        keySyncButton.setEnabled(currentEnabled && (! clockControlsAttached || clockModeBox.getSelectedId() != 3));

        const auto isRamp = px3::isRampLfoWaveformIndex(clamped);
        rateLabel.setText(isRamp ? juce::String("TIME") : rateCaptionText, juce::dontSendNotification);
        if (isRamp && clockFree)
        {
            rateValueLabel.setText(formatRampSeconds(rampTimeKnob.getValue()), juce::dontSendNotification);
        }

        // The knob in the rate position changes with the kind of shape, and that
        // choice is made in resized() - which a waveform change does not
        // otherwise trigger.
        if (isRamp != laidOutForRamp)
        {
            laidOutForRamp = isRamp;
            resized();
            repaint();
        }
    }

    if (enabledChanged)
    {
        rateKnob.repaint();
        repaint();
        graphView.invalidateStill();
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

    // Only the wave display moves; the rest of the card (knobs, boxes, title)
    // stays cached.
    graphView.requestFrame();
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
    enabledButton.setBounds(card.powerBounds());

    if (compactLayout)
    {
        layoutCompact();
        placeGraphView();
        return;
    }

    using px3::ui::ControlShape;

    if (clockControlsAttached)
    {
        auto area = inner.rowContent(2).removeFromTop(54);
        auto first = area.removeFromLeft(area.getWidth() / 2).reduced(2, 0);
        auto second = area.reduced(2, 0);
        clockModeLabel.setBounds(first.removeFromTop(14));
        clockDivisionLabel.setBounds(second.removeFromTop(14));
        clockModeBox.setBounds(first.removeFromTop(24));
        clockDivisionBox.setBounds(second.removeFromTop(24));
        clockStatus.setBounds(first.removeFromTop(16));
    }

    // Row 1: wave type and key sync.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        flex.items.add(juce::FlexItem(84.0f, cellHeight).withMargin(gap));
        if (rampControlsAttached)
        {
            flex.items.add(juce::FlexItem(66.0f, cellHeight).withMargin(gap));
        }
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        px3::ui::layoutLabelledControl(cell(0),
                                       { &waveformLabel, &waveformBox, nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       inner.rowControl(0));
        if (rampControlsAttached)
        {
            // No caption of its own, but the same reserved caption height, so
            // the chip lines up with the dropdown beside it.
            px3::ui::layoutLabelledControl(cell(1),
                                           { &keySyncCaptionSpacer, &keySyncButton, nullptr,
                                             ControlShape::stretch, 14, 0, 24 },
                                           inner.rowControl(0));
            keySyncButton.setAccentColour(card.style().border.colour);
        }
    }

    // Row 2: rate, name-knob-value, centred in the row.
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        flex.items.add(juce::FlexItem(84.0f, cellHeight).withMargin(gap));
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        // Name above, knob, value below. The name used to be hidden, which left
        // an unlabelled knob telling you only "0.54 Hz".
        const auto showRampTime = rampControlsAttached && laidOutForRamp;
        rateKnob.setVisible(! showRampTime);
        rampTimeKnob.setVisible(showRampTime);
        juce::Slider& timeKnob = showRampTime ? rampTimeKnob : rateKnob;
        px3::ui::layoutLabelledControl(cell(0),
                                       { &rateLabel, &timeKnob, &rateValueLabel,
                                         ControlShape::square, 16, 20, 84 },
                                       inner.rowControl(1));
    }

    // Row 3 is the wave graph.
    placeGraphView();
}

void LfoComponent::placeGraphView()
{
    // A pixel past the graph each way, so its 1 px frame is inside the layer.
    graphView.setBounds(graphBounds().expanded(1.0f).getSmallestIntegerContainer());
    graphView.invalidateStill();
}

void LfoComponent::layoutCompact()
{
    namespace c = px3::ui::compact;
    auto area = card.contentBelowTitle().reduced(c::pad, c::pad - 1);
    const auto rowH = c::captionHeight + c::boxHeight;

    // Clear of the header, so the display does not sit against it.
    area.removeFromTop(c::headerClearance);

    // The display on top, the controls under it - WAVE | KEY SYNC, CLOCK |
    // DIVISION, RATE - placed from the bottom up so the display takes
    // what they leave.
    const auto boxRows = clockControlsAttached ? 2 : 1;
    const auto knob = juce::jlimit(26, 50, (area.getHeight() - boxRows * (rowH + c::gap) - c::gap - 40) / 2);

    // RATE, alone in its row: centred, in a cell half the row wide - the width
    // it had when it shared the row - so its caption and readout do not stretch.
    {
        const auto knobRow = area.removeFromBottom(c::knobRowHeight(knob));
        const auto rateCell = knobRow.withSizeKeepingCentre(c::cells(knobRow, 2).front().getWidth(), knobRow.getHeight());
        const auto showRampTime = rampControlsAttached && laidOutForRamp;
        rateKnob.setVisible(! showRampTime);
        rampTimeKnob.setVisible(showRampTime);
        juce::Slider& timeKnob = showRampTime ? rampTimeKnob : rateKnob;
        c::knobCell(rateCell, &rateLabel, timeKnob, &rateValueLabel, knob);
    }
    area.removeFromBottom(c::gap);

    // CLOCK | DIVISION
    if (clockControlsAttached)
    {
        const auto boxes = c::cells(area.removeFromBottom(rowH), 2);
        c::boxCell(boxes[0], &clockModeLabel, clockModeBox);
        c::boxCell(boxes[1], &clockDivisionLabel, clockDivisionBox);
        area.removeFromBottom(c::gap);
    }

    // WAVE | KEY SYNC
    {
        auto row = area.removeFromBottom(rowH);
        if (rampControlsAttached)
        {
            auto chip = row.removeFromRight(juce::jlimit(54, 66, row.getWidth() / 4));
            row.removeFromRight(c::gap);
            c::boxCell(chip, &keySyncCaptionSpacer, keySyncButton);
            keySyncButton.setAccentColour(card.style().border.colour);
        }
        c::boxCell(row, &waveformLabel, waveformBox);
    }
    area.removeFromBottom(c::gap + 2);

    compactGraph = area;
    // The "NO HOST CLOCK" status sits in the display's top-left corner.
    clockStatus.setBounds(clockControlsAttached ? compactGraph.reduced(4, 2).withHeight(c::captionHeight)
                                                : juce::Rectangle<int>());
    clockStatus.toFront(false);
}

juce::Rectangle<int> LfoComponent::graphArea() const
{
    if (compactLayout) { return compactGraph; }
    return inner.rowContent(2).withTrimmedTop(clockControlsAttached ? 54 : 0);
}

juce::Rectangle<int> LfoComponent::graphHitArea() const
{
    return compactLayout ? compactGraph : inner.rowContent(2);
}

void LfoComponent::mouseUp(const juce::MouseEvent& event)
{
    // Clicking the card's background toggles its power, the same as clicking
    // the button. No tooltip here: the whole card is not a control, and a card
    // that explained itself on hover would be noise.
    // The wave graph is a display, not a switch: it shows no pointer and it
    // takes no click, so the two agree.
    if (graphHitArea().contains(event.getPosition()))
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
    setMouseCursor(graphHitArea().contains(event.getPosition())
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
    // The wave display is its own opaque layer (graphView), so a frame of it
    // never repaints this.
    paintChrome(g);
}

// The card and its title. ModPanel used to draw the title into this
// component's bounds, which is the parent-owns-child's-pixels pattern this
// refactor removes everywhere it appears.
void LfoComponent::paintChrome(juce::Graphics& g)
{
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
}

// The graph is row 3. It used to be found by replaying resized()'s stack of
// removeFromTop calls against a separately-derived card rectangle.
juce::Rectangle<float> LfoComponent::graphBounds() const
{
    return graphArea().toFloat().reduced(0.0f, compactLayout ? 0.0f : 2.0f);
}

void LfoComponent::paintGraphStill(juce::Graphics& g)
{
    paintChrome(g);
    const auto graph = graphBounds();
    if (graph.getWidth() < 40.0f || graph.getHeight() < 20.0f)
    {
        return;
    }

    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);
    g.setColour(juce::Colour::fromRGBA(14, 14, 18, 170));
    g.fillRect(graph);
    g.setColour(effectiveAccent.withAlpha(0.32f));
    g.drawRect(graph.expanded(0.5f), 1.0f);

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
}

void LfoComponent::paintGraphMoving(juce::Graphics& g)
{
    const auto graph = graphBounds();
    if (graph.getWidth() < 40.0f || graph.getHeight() < 20.0f)
    {
        return;
    }

    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);
    const auto left = graph.getX() + 6.0f;
    const auto right = graph.getRight() - 6.0f;
    const auto top = graph.getY() + 5.0f;
    const auto bottom = graph.getBottom() - 5.0f;
    const auto mid = (top + bottom) * 0.5f;

    juce::Path wave;
    const auto width = juce::jmax(1.0f, right - left);
    const auto height = juce::jmax(1.0f, bottom - top);
    for (int s = 0; s <= 72; ++s)
    {
        const auto t = static_cast<float>(s) / 72.0f;
        const auto phaseNorm = std::fmod(t + visualPhase / juce::MathConstants<float>::twoPi, 1.0f);
        // A ramp is drawn still, as the one-shot it is: travel, then hold.
        const auto y = px3::isRampLfoWaveformIndex(currentWaveformIndex)
                           ? rampPreviewSample(t, currentWaveformIndex)
                           : waveformSample(phaseNorm, currentWaveformIndex);
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
                                      juce::PathStrokeType::mitered,
                                      juce::PathStrokeType::rounded));
    const auto waveDetailColour = currentEnabled ? juce::Colour::fromRGB(232, 240, 255)
                                                 : juce::Colour::fromRGB(178, 178, 178);
    g.setColour(waveDetailColour);
    g.strokePath(wave,
                 juce::PathStrokeType(1.2f,
                                      juce::PathStrokeType::mitered,
                                      juce::PathStrokeType::rounded));
}

float LfoComponent::rampPreviewSample(float t, int waveformIndex)
{
    constexpr auto travel = 0.8f;
    const auto rising = t < travel ? -1.0f + 2.0f * (t / travel) : 1.0f;
    return waveformIndex == px3::lfoWaveformToIndex(px3::LfoWaveform::rampDown) ? -rising : rising;
}

float LfoComponent::waveformSample(float phaseNorm, int waveformIndex)
{
    const auto p = phaseNorm - std::floor(phaseNorm);
    const auto cycle = static_cast<std::int64_t>(std::floor(phaseNorm));

    switch (px3::clampLfoWaveformIndex(waveformIndex))
    {
        case static_cast<int>(px3::LfoWaveform::sampleHold):
            return LfoGenerator::randomForCycle(cycle);
        case static_cast<int>(px3::LfoWaveform::smoothRandom):
        {
            const auto a = LfoGenerator::randomForCycle(cycle);
            const auto b = LfoGenerator::randomForCycle(cycle + 1);
            return a + (b - a) * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * p));
        }
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
