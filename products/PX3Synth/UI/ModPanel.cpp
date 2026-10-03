#include "ParameterKnob.h"
#include "ModPanel.h"

#include "UIConfig.h"

#include <cmath>

namespace
{
class GraphCableCanvas final : public juce::Component
{
public:
    explicit GraphCableCanvas(PX3SynthAudioProcessor& processorIn) : processor(processorIn)
    {
        setComponentID("mod.graph.canvas");
        setWantsKeyboardFocus(true);
    }
    std::function<void(int)> slotSelected;
    int selectedSlot { 0 };
    int preferredHeight()
    {
        refreshDestinations();
        return juce::jmax(sourceCount, destinations.size()) * 28 + 24;
    }
    void paint(juce::Graphics& graphics) override
    {
        refreshDestinations();
        graphics.fillAll(juce::Colour(0xff141819));
        graphics.setFont(juce::FontOptions(11.5f));
        for (int source = 0; source < sourceCount; ++source)
        {
            const auto point = sourcePoint(source);
            graphics.setColour(sourceColour(source));
            graphics.drawEllipse(point.x - 5.0f, point.y - 5.0f, 10.0f, 10.0f, 1.5f);
            graphics.setColour(juce::Colour(0xffe2e5e3));
            graphics.drawText(PX3SynthAudioProcessor::graphSourceName(source),
                              8, juce::roundToInt(point.y) - 10, 132, 20, juce::Justification::centredLeft);
        }
        for (int destination = 0; destination < destinations.size(); ++destination)
        {
            const auto point = destinationPoint(destination);
            graphics.setColour(juce::Colour(0xffc1c9c7));
            graphics.drawEllipse(point.x - 5.0f, point.y - 5.0f, 10.0f, 10.0f, 1.5f);
            if (const auto* entry = processor.getParameterCatalog().find(destinations[destination]))
            {
                graphics.drawFittedText(entry->name,
                    { juce::roundToInt(point.x) + 12, juce::roundToInt(point.y) - 10,
                      juce::jmax(1, getWidth() - juce::roundToInt(point.x) - 20), 20 },
                    juce::Justification::centredLeft, 1);
            }
        }
        for (int slot = 0; slot < PX3SynthAudioProcessor::kGraphRouteSlots; ++slot)
        {
            const auto route = processor.getGraphRoute(slot);
            const auto destination = destinations.indexOf(route.destination);
            if (route.source < 0 || destination < 0) { continue; }
            const auto path = cable(sourcePoint(route.source), destinationPoint(destination));
            graphics.setColour(sourceColour(route.source).withAlpha(slot == selectedSlot ? 1.0f : 0.48f));
            graphics.strokePath(path, juce::PathStrokeType(slot == selectedSlot ? 3.0f : 1.5f));
        }
        if (dragSource >= 0)
        {
            graphics.setColour(sourceColour(dragSource));
            graphics.strokePath(cable(sourcePoint(dragSource), dragPoint), juce::PathStrokeType(2.0f));
        }
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        refreshDestinations();
        for (int source = 0; source < sourceCount; ++source)
        {
            if (sourcePoint(source).getDistanceFrom(event.position) <= 12.0f)
            {
                dragSource = source;
                dragPoint = event.position;
                repaint();
                return;
            }
        }
        for (int slot = 0; slot < PX3SynthAudioProcessor::kGraphRouteSlots; ++slot)
        {
            const auto route = processor.getGraphRoute(slot);
            const auto destination = destinations.indexOf(route.destination);
            if (route.source < 0 || destination < 0) { continue; }
            juce::Path hit;
            juce::PathStrokeType(12.0f).createStrokedPath(hit, cable(sourcePoint(route.source), destinationPoint(destination)));
            if (hit.contains(event.position))
            {
                selectedSlot = slot;
                if (slotSelected) { slotSelected(slot); }
                repaint();
                return;
            }
        }
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragSource >= 0)
        {
            dragPoint = event.position;
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
            {
                const auto point = viewport->getLocalPoint(this, event.getPosition());
                viewport->autoScroll(point.x, point.y, 24, 12);
            }
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (dragSource < 0) { return; }
        for (int destination = 0; destination < destinations.size(); ++destination)
        {
            if (destinationPoint(destination).getDistanceFrom(event.position) > 12.0f) { continue; }
            for (int slot = 0; slot < PX3SynthAudioProcessor::kGraphRouteSlots; ++slot)
            {
                if (processor.getGraphRoute(slot).source >= 0) { continue; }
                juce::String error;
                if (processor.setGraphRoute(slot, { dragSource, destinations[destination] }, error))
                {
                    auto& depth = processor.getGraphRouteDepthParam(slot);
                    depth.beginChangeGesture();
                    depth.setValueNotifyingHost(depth.convertTo0to1(0.5f));
                    depth.endChangeGesture();
                    selectedSlot = slot;
                    if (slotSelected) { slotSelected(slot); }
                }
                break;
            }
            break;
        }
        dragSource = -1;
        repaint();
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getKeyCode() != juce::KeyPress::deleteKey && key.getKeyCode() != juce::KeyPress::backspaceKey) { return false; }
        juce::String error;
        if (processor.setGraphRoute(selectedSlot, {}, error))
        {
            if (slotSelected) { slotSelected(selectedSlot); }
            repaint();
        }
        return true;
    }
private:
    static constexpr int sourceCount = PX3SynthAudioProcessor::kLfoSourceCount
                                      + PX3SynthAudioProcessor::kEnvelopeSourceCount + PX3SynthAudioProcessor::kMacroCount;
    juce::Point<float> sourcePoint(int source) const
    {
        return { 150.0f, 12.0f + static_cast<float>(source) * 28.0f };
    }
    juce::Point<float> destinationPoint(int destination) const
    {
        return { static_cast<float>(getWidth() - 220), 12.0f + static_cast<float>(destination) * 28.0f };
    }
    static juce::Colour sourceColour(int source)
    {
        return source < 3 ? juce::Colour(0xff7fc6b2) : source < 6 ? juce::Colour(0xffd7b873) : juce::Colour(0xff79b5d3);
    }
    static juce::Path cable(juce::Point<float> start, juce::Point<float> end)
    {
        juce::Path path;
        path.startNewSubPath(start);
        const auto bend = juce::jmax(24.0f, (end.x - start.x) * 0.4f);
        path.cubicTo(start.x + bend, start.y, end.x - bend, end.y, end.x, end.y);
        return path;
    }
    void refreshDestinations()
    {
        destinations.clear();
        for (int slot = 0; slot < PX3SynthAudioProcessor::kGraphRouteSlots; ++slot)
        {
            const auto route = processor.getGraphRoute(slot);
            if (route.source >= 0 && ! destinations.contains(route.destination)) { destinations.add(route.destination); }
        }
        const auto selected = processor.getGraphRoute(selectedSlot);
        if (selected.destination.isNotEmpty() && ! destinations.contains(selected.destination)) { destinations.add(selected.destination); }
        for (const auto* id : { "voice.filter1.cutoff", "voice.filter1.resonance", "voice.filter2.cutoff", "mix.osc1.pan",
                                "voice.osc1.macro.a", "voice.osc1.pitch.mod", "fx.delay.amount", "fx.reverb.amount" })
        {
            if (destinations.size() >= 12) { break; }
            if (! destinations.contains(id)) { destinations.add(id); }
        }
    }
    PX3SynthAudioProcessor& processor;
    juce::StringArray destinations;
    int dragSource { -1 };
    juce::Point<float> dragPoint;
};

class GraphRouteEditor final : public juce::Component
{
public:
    explicit GraphRouteEditor(PX3SynthAudioProcessor& processorIn) : processor(processorIn), canvas(processorIn)
    {
        constexpr std::array<const char*, 6> names { "SLOT", "SOURCE", "DESTINATION", "POLARITY", "CURVE", "DEPTH" };
        for (std::size_t index = 0; index < labels.size(); ++index)
        {
            labels[index].setText(names[index], juce::dontSendNotification);
            addAndMakeVisible(labels[index]);
        }
        for (auto* menu : { &slot, &source, &destination, &polarity, &curve }) { addAndMakeVisible(*menu); }
        for (int index = 0; index < PX3SynthAudioProcessor::kGraphRouteSlots; ++index)
        {
            slot.addItem(juce::String(index + 1).paddedLeft('0', 2), index + 1);
        }
        source.addItem("None", 1);
        source.setComponentID("mod.route.source");
        for (int index = 0; index < PX3SynthAudioProcessor::kLfoSourceCount
                                  + PX3SynthAudioProcessor::kEnvelopeSourceCount
                                  + PX3SynthAudioProcessor::kMacroCount; ++index)
        {
            source.addItem(PX3SynthAudioProcessor::graphSourceName(index), index + 2);
        }
        destination.addItem("None", 1);
        destination.setComponentID("mod.route.destination");
        destinationIds.add(juce::String());
        for (const auto& entry : processor.getParameterCatalog().entries())
        {
            if (! processor.isGraphDestination(entry.id)) { continue; }
            destinationIds.add(entry.id);
            destination.addItem(entry.name + "  |  " + entry.id, destinationIds.size());
        }
        polarity.addItemList({ "Native", "Unipolar", "Bipolar" }, 1);
        curve.addItemList({ "Linear", "Square", "Square Root" }, 1);
        depth.setSliderStyle(juce::Slider::LinearHorizontal);
        depth.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 26);
        addAndMakeVisible(depth);
        addAndMakeVisible(clear);
        addAndMakeVisible(status);
        canvasViewport.setViewedComponent(&canvas, false);
        canvasViewport.setScrollBarsShown(true, false);
        addAndMakeVisible(canvasViewport);
        canvas.slotSelected = [this](int index)
        {
            slot.setSelectedId(index + 1, juce::dontSendNotification);
            loadSlot();
        };
        slot.onChange = [this] { loadSlot(); };
        for (auto* menu : { &source, &destination, &polarity, &curve })
        {
            menu->onChange = [this] { apply(); };
        }
        clear.onClick = [this]
        {
            juce::String error;
            if (processor.setGraphRoute(slot.getSelectedId() - 1, {}, error)) { loadSlot(); }
            status.setText(error, juce::dontSendNotification);
        };
        slot.setSelectedId(1, juce::dontSendNotification);
        loadSlot();
        setSize(980, 650);
    }

    void paint(juce::Graphics& graphics) override { graphics.fillAll(juce::Colour(0xff1b1e20)); }
    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        std::array<juce::Component*, 6> controls { &slot, &source, &destination, &polarity, &curve, &depth };
        for (std::size_t index = 0; index < controls.size(); ++index)
        {
            auto row = area.removeFromTop(32);
            labels[index].setBounds(row.removeFromLeft(108));
            controls[index]->setBounds(row);
            area.removeFromTop(8);
        }
        clear.setBounds(area.removeFromTop(28).removeFromLeft(110));
        status.setBounds(area.removeFromTop(30));
        area.removeFromTop(8);
        canvasViewport.setBounds(area);
        canvas.setSize(juce::jmax(1, area.getWidth() - canvasViewport.getScrollBarThickness()),
                   juce::jmax(area.getHeight(), canvas.preferredHeight()));
    }

private:
    void loadSlot()
    {
        attachment.reset();
        loading = true;
        const auto index = slot.getSelectedId() - 1;
        const auto route = processor.getGraphRoute(index);
        source.setSelectedId(route.source + 2, juce::dontSendNotification);
        destination.setSelectedId(destinationIds.indexOf(route.destination) + 1, juce::dontSendNotification);
        polarity.setSelectedId(static_cast<int>(route.polarity) + 1, juce::dontSendNotification);
        curve.setSelectedId(static_cast<int>(route.curve) + 1, juce::dontSendNotification);
        attachment = std::make_unique<juce::SliderParameterAttachment>(processor.getGraphRouteDepthParam(index), depth, nullptr);
        loading = false;
        canvas.selectedSlot = index;
        resized();
        canvas.repaint();
    }
    void apply()
    {
        if (loading) { return; }
        if (source.getSelectedId() <= 1 || destination.getSelectedId() <= 1)
        {
            if (processor.getGraphRoute(slot.getSelectedId() - 1).source >= 0)
            {
                juce::String error;
                if (processor.setGraphRoute(slot.getSelectedId() - 1, {}, error)) { loadSlot(); }
                status.setText(error, juce::dontSendNotification);
            }
            return;
        }
        PX3SynthAudioProcessor::GraphRouteConfiguration route;
        route.source = source.getSelectedId() - 2;
        route.destination = destinationIds[destination.getSelectedId() - 1];
        route.polarity = static_cast<px3::synth::ModulationPolarity>(polarity.getSelectedId() - 1);
        route.curve = static_cast<px3::synth::ModulationCurve>(curve.getSelectedId() - 1);
        juce::String error;
        processor.setGraphRoute(slot.getSelectedId() - 1, route, error);
        status.setText(error, juce::dontSendNotification);
        resized();
        canvas.repaint();
    }
    PX3SynthAudioProcessor& processor;
    std::array<juce::Label, 6> labels;
    juce::ComboBox slot, source, destination, polarity, curve;
    juce::StringArray destinationIds;
    juce::Slider depth;
    juce::TextButton clear { "CLEAR ROUTE" };
    juce::Label status;
    bool loading { false };
    GraphCableCanvas canvas;
    juce::Viewport canvasViewport;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

class GraphRouteWindow final : public juce::DocumentWindow
{
public:
    explicit GraphRouteWindow(PX3SynthAudioProcessor& processor)
        : juce::DocumentWindow("PX3 MODULATION ROUTES", juce::Colour(0xff1b1e20), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new GraphRouteEditor(processor), true);
        setResizable(true, true);
        setResizeLimits(760, 650, 1400, 1000);
        centreWithSize(980, 685);
    }
    void closeButtonPressed() override { setVisible(false); }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getKeyCode() != juce::KeyPress::escapeKey) { return false; }
        setVisible(false);
        return true;
    }
};
}

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

    addAndMakeVisible(*lfoComponent);

    for (int lfoIndex = 1; lfoIndex < PX3SynthAudioProcessor::kLfoSourceCount; ++lfoIndex)
    {
        auto& bundle = extraLfos[static_cast<std::size_t>(lfoIndex - 1)];
        configureOwnedLfoBundle(lfoIndex, bundle);
        addAndMakeVisible(*bundle.component);
    }

    for (int envIndex = 0; envIndex < PX3SynthAudioProcessor::kEnvelopeSourceCount; ++envIndex)
    {
        auto& bundle = envelopes[static_cast<std::size_t>(envIndex)];
        configureOwnedEnvBundle(envIndex, bundle);
        addAndMakeVisible(*bundle.component);
    }
    routesButton.setComponentID("mod.routes.open");
    routesButton.setTooltip("Open modulation routes");
    addAndMakeVisible(routesButton);
    routesButton.onClick = [this]
    {
        routesWindow = std::make_unique<GraphRouteWindow>(processor);
        routesWindow->setVisible(true);
    };
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
    bundle.waveformLabel.setJustificationType(juce::Justification::centredLeft);
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

void ModPanel::paint(juce::Graphics& g)
{
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

    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("mod.panel.layout.padY", 10) : 10;
    auto panelArea = getLocalBounds().reduced(panelPadX, panelPadY);
    routesButton.setBounds(panelArea.removeFromTop(28).removeFromRight(100));
    panelArea.removeFromTop(8);

    const auto colGap = uiConfig != nullptr ? uiConfig->getInt("mod.grid.colGap", 8) : 8;
    const auto rowGap = uiConfig != nullptr ? uiConfig->getInt("mod.grid.rowGap", 10) : 10;
    const auto minColWidth = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minColWidth", 280) : 280;
    const auto minLfoHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minLfoHeight", 300) : 300;
    const auto minEnvHeight = uiConfig != nullptr ? uiConfig->getInt("mod.grid.minEnvHeight", 280) : 280;

    const auto colWidth = juce::jmax(minColWidth, juce::jmax(1, (panelArea.getWidth() - (2 * colGap)) / 3));
    // Each row takes its own minimum first and only then shares what is left
    // over. Halving the panel and giving the LFO row the first half meant that
    // once the envelope cards grew, the ENV row's minimum pushed it PAST the
    // bottom of the content - so the cards were cut off and there was nothing
    // to scroll to, because the panel did not know it needed to be taller.
    //
    // A tail is held back below the last row, so there is somewhere to scroll
    // to rather than the cards ending flush against the edge. It reads the same
    // key the viewport adds to the content height, so the two cannot disagree.
    const auto scrollTail = uiConfig != nullptr ? uiConfig->getInt("editor.layout.scrollTail", 30) : 30;
    const auto usable = juce::jmax(1, panelArea.getHeight() - scrollTail);
    const auto surplus = juce::jmax(0, usable - rowGap - minLfoHeight - minEnvHeight);

    const auto lfoRowHeight = minLfoHeight + surplus / 2;
    const auto envRowHeight = minEnvHeight + (surplus - surplus / 2);
    const auto totalGridWidth = colWidth * 3 + colGap * 2;
    const auto gridX = panelArea.getX() + juce::jmax(0, (panelArea.getWidth() - totalGridWidth) / 2);

    auto lfoRow = juce::Rectangle<int>(gridX, panelArea.getY(), totalGridWidth, lfoRowHeight);
    auto envRow = juce::Rectangle<int>(gridX, lfoRow.getBottom() + rowGap, totalGridWidth, envRowHeight);

    std::array<juce::Rectangle<int>, 3> lfoCells {};
    std::array<juce::Rectangle<int>, 3> envCells {};
    for (int i = 0; i < 3; ++i)
    {
        const auto x = gridX + i * (colWidth + colGap);
        lfoCells[static_cast<std::size_t>(i)] = juce::Rectangle<int>(x, lfoRow.getY(), colWidth, lfoRow.getHeight());
        envCells[static_cast<std::size_t>(i)] = juce::Rectangle<int>(x, envRow.getY(), colWidth, envRow.getHeight());
    }

    lfoComponent->setBounds(lfoCells[0].reduced(2, 2));
    extraLfos[0].component->setBounds(lfoCells[1].reduced(2, 2));
    extraLfos[1].component->setBounds(lfoCells[2].reduced(2, 2));

    for (int i = 0; i < static_cast<int>(envelopes.size()); ++i)
    {
        envelopes[static_cast<std::size_t>(i)].component->setBounds(envCells[static_cast<std::size_t>(i)].reduced(2, 2));
    }
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
    return panelPadY * 2 + rowGap + minLfoHeight + minEnvHeight + 36;
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