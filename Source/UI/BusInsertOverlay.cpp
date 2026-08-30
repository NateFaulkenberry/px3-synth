#include "BusInsertOverlay.h"

#include "../DSP/PluginProcessor.h"
#include "UIConfig.h"

namespace px3::ui
{

namespace
{
juce::Colour configColour(const std::shared_ptr<const UIConfig>& config,
                          const juce::String& path,
                          juce::Colour fallback)
{
    return config != nullptr ? config->getColour(path, fallback) : fallback;
}

int configInt(const std::shared_ptr<const UIConfig>& config, const juce::String& path, int fallback)
{
    return config != nullptr ? config->getInt(path, fallback) : fallback;
}

float configFloat(const std::shared_ptr<const UIConfig>& config, const juce::String& path, float fallback)
{
    return config != nullptr ? config->getFloat(path, fallback) : fallback;
}

void styleCaption(juce::Label& label, juce::Colour colour, float size, juce::Justification justification)
{
    label.setJustificationType(justification);
    label.setColour(juce::Label::textColourId, colour);
    label.setFont(juce::Font(juce::FontOptions(size, juce::Font::bold)));
    label.setInterceptsMouseClicks(false, false);
}

void configureRotary(juce::Slider& slider, juce::LookAndFeel* lookAndFeel)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    if (lookAndFeel != nullptr)
    {
        slider.setLookAndFeel(lookAndFeel);
    }
}
} // namespace

//==============================================================================
BusInsertOverlay::BusInsertOverlay(PX3SynthAudioProcessor& processorIn)
    : processor(processorIn)
{
    addAndMakeVisible(closeButton);
    addAndMakeVisible(enableButton);
    closeButton.onClick = [this]()
    {
        if (onClose != nullptr)
        {
            onClose();
        }
    };
}

BusInsertOverlay::~BusInsertOverlay() = default;

void BusInsertOverlay::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    refreshCardStyle();
    uiConfigChanged();
    resized();
    repaint();
}

void BusInsertOverlay::setSheetVisible(bool shown)
{
    setVisible(shown);
}

void BusInsertOverlay::refreshCardStyle()
{
    card.setStyleKey(cardStyleKey());
    card.setConfig(uiConfig);
    card.setPanelContentBounds(getLocalBounds());
    card.layout(getLocalBounds());

    innerStyle = {};
    if (uiConfig != nullptr)
    {
        const auto base = "cards." + cardStyleKey() + ".innerOverlay";
        innerStyle.margin = uiConfig->getFloat(base + ".margin", innerStyle.margin);
        innerStyle.radius = uiConfig->getFloat(base + ".radius", innerStyle.radius);
        innerStyle.colour = uiConfig->getColour(base + ".color", innerStyle.colour);
        // Opacity as its own property, the way every Fill in the card system
        // works, so the panel can be made translucent without hand-editing an
        // alpha byte into the colour.
        innerStyle.opacity = uiConfig->getFloat(base + ".opacity", innerStyle.opacity);
    }
}

juce::Rectangle<int> BusInsertOverlay::headerBounds() const
{
    return card.contentBelowTitle().withHeight(
        uiConfig != nullptr ? uiConfig->getInt("busInserts.headerHeight", 30) : 30);
}

juce::Rectangle<int> BusInsertOverlay::innerOverlayBounds() const
{
    const auto header = headerBounds();
    const auto gap = uiConfig != nullptr ? uiConfig->getInt("busInserts.headerGap", 8) : 8;
    return card.contentBelowTitle()
        .withTrimmedTop(header.getHeight() + gap)
        .reduced(juce::roundToInt(innerStyle.margin));
}

juce::Colour BusInsertOverlay::busAccentColour() const
{
    // Straight from the strip that opened this sheet, so the two are visibly
    // the same channel rather than two components that happen to share a name.
    const auto key = busIndex == PX3SynthAudioProcessor::fxBusInsert ? "cards.mixerFx.border.color"
                                                                     : "cards.mixerDry.border.color";
    return configColour(uiConfig, key, juce::Colour::fromRGB(237, 241, 247));
}

void BusInsertOverlay::setKnobLookAndFeel(juce::LookAndFeel* lookAndFeel)
{
    knobLookAndFeel = lookAndFeel;
    knobLookAndFeelChanged();
}

void BusInsertOverlay::setBus(int bus)
{
    busIndex = juce::jlimit(0, PX3SynthAudioProcessor::kBusInsertCount - 1, bus);
    clearAttachments();
    rebuildForBus();
    refreshCardStyle();
    resized();
    repaint();
}

void BusInsertOverlay::setBusName(juce::String name)
{
    busName = std::move(name);
    repaint();
}

void BusInsertOverlay::clearAttachments()
{
    sliderAttachments.clear();
    buttonAttachments.clear();
    comboAttachments.clear();
}

void BusInsertOverlay::paint(juce::Graphics& g)
{
    // Border, padding gap, translucent background: the card system, unchanged,
    // so a sheet is recognisably the same object as every other framed thing in
    // the plugin. What sits inside is a single solid panel rather than a card's
    // two-part gloss.
    card.draw(g, sheetTitle());

    const auto inner = innerOverlayBounds().toFloat();
    g.setColour(innerStyle.effectiveColour());
    g.fillRoundedRectangle(inner, innerStyle.radius);
}

//==============================================================================
// EQ
//==============================================================================
BusEqOverlay::BusEqOverlay(PX3SynthAudioProcessor& processorIn)
    : BusInsertOverlay(processorIn),
      graph(processorIn)
{
    addAndMakeVisible(graph);

    static const std::array<const char*, kBandCount> captions { { "BAND 1", "BAND 2", "BAND 3", "BAND 4" } };

    for (int band = 0; band < kBandCount; ++band)
    {
        auto& strip = bands[static_cast<std::size_t>(band)];

        addAndMakeVisible(strip.caption);
        styleCaption(strip.caption, juce::Colours::white, 11.0f, juce::Justification::centred);
        strip.caption.setText(captions[static_cast<std::size_t>(band)], juce::dontSendNotification);

        // Only the outer bands offer a choice; the inner two are bells and have
        // no type parameter at all, so they get no selector rather than a
        // selector with one entry in it.
        strip.type.setVisible(band == 0 || band == kBandCount - 1);
        addChildComponent(strip.type);

        for (auto* slider : { &strip.frequency, &strip.gain, &strip.q })
        {
            addAndMakeVisible(*slider);
        }

        for (auto* label : { &strip.frequencyValue, &strip.gainValue, &strip.qValue })
        {
            addAndMakeVisible(*label);
            styleCaption(*label, juce::Colours::white.withAlpha(0.85f), 10.0f, juce::Justification::centred);
        }

        for (const auto& entry : { std::make_pair(&strip.frequencyCaption, "FREQ"),
                                   std::make_pair(&strip.gainCaption, "GAIN"),
                                   std::make_pair(&strip.qCaption, "Q") })
        {
            addAndMakeVisible(*entry.first);
            styleCaption(*entry.first, juce::Colours::white.withAlpha(0.55f), 9.0f, juce::Justification::centred);
            entry.first->setText(entry.second, juce::dontSendNotification);
        }
    }

    startTimerHz(24);
}

BusEqOverlay::~BusEqOverlay()
{
    stopTimer();
    clearAttachments();
}

void BusEqOverlay::knobLookAndFeelChanged()
{
    for (auto& strip : bands)
    {
        for (auto* slider : { &strip.frequency, &strip.gain, &strip.q })
        {
            configureRotary(*slider, knobLookAndFeel);
        }
    }
}

void BusEqOverlay::uiConfigChanged()
{
    graph.setUIConfig(uiConfig);
    graph.setAccentColour(busAccentColour());
}

void BusEqOverlay::setSheetVisible(bool shown)
{
    // The spectrum tap costs the audio thread nothing while the sheet is
    // closed, and that only holds if something actually switches it off.
    graph.setAnalyserRunning(shown);
    BusInsertOverlay::setSheetVisible(shown);
}

void BusEqOverlay::rebuildForBus()
{
    graph.setBus(busIndex);
    graph.setUIConfig(uiConfig);
    graph.setAccentColour(busAccentColour());

    const auto& params = processor.getBusInsertParams(busIndex);
    if (params.eqEnabled == nullptr)
    {
        return;
    }

    buttonAttachments.push_back(
        std::make_unique<juce::ButtonParameterAttachment>(*params.eqEnabled, enableButton, nullptr));

    for (int band = 0; band < kBandCount; ++band)
    {
        const auto b = static_cast<std::size_t>(band);
        auto& strip = bands[b];

        if (params.bandType[b] != nullptr)
        {
            strip.type.clear(juce::dontSendNotification);
            strip.type.addItemList(params.bandType[b]->choices, 1);
            comboAttachments.push_back(
                std::make_unique<juce::ComboBoxParameterAttachment>(*params.bandType[b], strip.type, nullptr));
        }

        if (params.bandFreq[b] != nullptr)
        {
            sliderAttachments.push_back(
                std::make_unique<juce::SliderParameterAttachment>(*params.bandFreq[b], strip.frequency, nullptr));
        }
        if (params.bandGain[b] != nullptr)
        {
            sliderAttachments.push_back(
                std::make_unique<juce::SliderParameterAttachment>(*params.bandGain[b], strip.gain, nullptr));
        }
        if (params.bandQ[b] != nullptr)
        {
            sliderAttachments.push_back(
                std::make_unique<juce::SliderParameterAttachment>(*params.bandQ[b], strip.q, nullptr));
        }
    }

    refreshReadouts();
}

void BusEqOverlay::timerCallback()
{
    if (! isVisible())
    {
        return;
    }

    refreshReadouts();
}

void BusEqOverlay::refreshReadouts()
{
    for (auto& strip : bands)
    {
        const auto hz = static_cast<float>(strip.frequency.getValue());
        strip.frequencyValue.setText(hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) + " k"
                                                   : juce::String(juce::roundToInt(hz)) + " Hz",
                                     juce::dontSendNotification);
        strip.gainValue.setText(juce::String(strip.gain.getValue(), 1) + " dB", juce::dontSendNotification);
        strip.qValue.setText(juce::String(strip.q.getValue(), 2), juce::dontSendNotification);

        // A band set flat is doing nothing, and saying so is more useful than
        // showing "0.0 dB" in the same weight as a band that is working. A pass
        // filter has no gain at all, so its readout is not dimmed by level.
        const auto isPass = strip.type.isVisible() && strip.type.getSelectedItemIndex() == 1;
        const auto working = isPass || std::abs(strip.gain.getValue()) > 0.05;
        strip.gainValue.setColour(juce::Label::textColourId,
                                  juce::Colours::white.withAlpha(working ? 0.85f : 0.35f));
        strip.gain.setEnabled(! isPass);
    }
}

void BusEqOverlay::resized()
{
    refreshCardStyle();

    auto header = headerBounds();
    const auto buttonWidth = configInt(uiConfig, "busInserts.buttonWidth", 62);
    closeButton.setBounds(header.removeFromRight(buttonWidth).reduced(0, 3));
    header.removeFromRight(6);
    enableButton.setBounds(header.removeFromRight(configInt(uiConfig, "busInserts.enableWidth", 42)).reduced(0, 3));

    auto area = innerOverlayBounds().reduced(configInt(uiConfig, "busInserts.eq.innerPadding", 12));

    graph.setBounds(area.removeFromTop(configInt(uiConfig, "busInserts.eq.graphHeight", 210)));
    area.removeFromTop(configInt(uiConfig, "busInserts.eq.graphGap", 14));

    const auto columnGap = configInt(uiConfig, "busInserts.eq.columnGap", 10);
    const auto knobSize = configInt(uiConfig, "busInserts.eq.knobSize", 46);
    constexpr auto captionHeight = 13;
    constexpr auto valueHeight = 12;

    const auto columnWidth = (area.getWidth() - columnGap * (kBandCount - 1)) / kBandCount;

    for (int band = 0; band < kBandCount; ++band)
    {
        auto column = area.removeFromLeft(columnWidth);
        if (band < kBandCount - 1)
        {
            area.removeFromLeft(columnGap);
        }

        auto& strip = bands[static_cast<std::size_t>(band)];
        strip.caption.setBounds(column.removeFromTop(captionHeight));
        column.removeFromTop(3);

        // Every band reserves the same height for a type selector even though
        // only the outer two have one, so the three knob rows below stay in
        // line across all four columns.
        auto typeRow = column.removeFromTop(configInt(uiConfig, "busInserts.eq.typeHeight", 20));
        if (strip.type.isVisible())
        {
            strip.type.setBounds(typeRow);
        }
        column.removeFromTop(6);

        // The three knobs sit side by side rather than stacked: the graph above
        // is the primary control now, and these are for precision, so they get
        // one row rather than a column three deep.
        auto knobRow = column.removeFromTop(knobSize);
        auto captionRow = column.removeFromTop(captionHeight);
        auto valueRow = column.removeFromTop(valueHeight);

        const auto slot = knobRow.getWidth() / 3;
        for (const auto& entry : { std::make_tuple(&strip.frequency, &strip.frequencyCaption, &strip.frequencyValue),
                                   std::make_tuple(&strip.gain, &strip.gainCaption, &strip.gainValue),
                                   std::make_tuple(&strip.q, &strip.qCaption, &strip.qValue) })
        {
            auto knobSlot = knobRow.removeFromLeft(slot);
            std::get<0>(entry)->setBounds(knobSlot.withSizeKeepingCentre(juce::jmin(slot, knobSize), knobSize));
            std::get<1>(entry)->setBounds(captionRow.removeFromLeft(slot));
            std::get<2>(entry)->setBounds(valueRow.removeFromLeft(slot));
        }
    }
}

//==============================================================================
// Compressor
//==============================================================================
BusCompOverlay::BusCompOverlay(PX3SynthAudioProcessor& processorIn)
    : BusInsertOverlay(processorIn)
{
    // Ratio is a vertical bank of latching push buttons, as the panel has.
    static const std::array<const char*, 5> ratioLegends { { "4", "8", "12", "20", "ALL" } };

    for (std::size_t i = 0; i < ratioButtons.size(); ++i)
    {
        auto& button = ratioButtons[i];
        button.setButtonText(ratioLegends[i]);
        button.setClickingTogglesState(false);
        button.setLookAndFeel(&pushLook);
        addAndMakeVisible(button);
        button.onClick = [this, i]()
        {
            const auto& params = processor.getBusInsertParams(busIndex);
            if (params.compRatio == nullptr)
            {
                return;
            }

            // Set through the parameter rather than the button, so the host
            // records the change and every other view of it follows.
            const auto normalised = params.compRatio->convertTo0to1(static_cast<float>(i));
            params.compRatio->setValueNotifyingHost(normalised);
        };
    }

    linkButton.setClickingTogglesState(true);
    linkButton.setLookAndFeel(&pushLook);
    addAndMakeVisible(linkButton);

    // The legends are engraved onto the panel in paint(), not placed as labels,
    // so these captions carry only the mix readout.
    addAndMakeVisible(mixValue);
    styleCaption(mixValue, juce::Colour::fromRGB(38, 40, 44), 9.5f, juce::Justification::centred);

    for (auto* caption : { &inputCaption, &outputCaption, &attackCaption, &releaseCaption, &mixCaption })
    {
        caption->setVisible(false);
    }

    startTimerHz(24);
}

BusCompOverlay::~BusCompOverlay()
{
    stopTimer();
    clearAttachments();

    // The look-and-feels are members declared after the controls that point at
    // them, so they are destroyed FIRST - leaving every slider and button
    // holding a dangling pointer for the rest of the teardown. Same shape as
    // the attachment bug: a base or later member going before its users.
    for (auto* slider : { &input, &output, &attack, &release, &mix })
    {
        slider->setLookAndFeel(nullptr);
    }
    for (auto& button : ratioButtons)
    {
        button.setLookAndFeel(nullptr);
    }
    linkButton.setLookAndFeel(nullptr);
}

void BusCompOverlay::knobLookAndFeelChanged()
{
    // The panel's own knob, not the plugin's: this face is a rack unit, and a
    // synth knob in the middle of it would be the one thing that gives it away.
    for (auto* slider : { &input, &output, &attack, &release, &mix })
    {
        configureRotary(*slider, &knobLook);
        addAndMakeVisible(*slider);
    }
    juce::ignoreUnused(knobLookAndFeel);
}

void BusCompOverlay::rebuildForBus()
{
    const auto& params = processor.getBusInsertParams(busIndex);
    if (params.compEnabled == nullptr)
    {
        return;
    }

    buttonAttachments.push_back(
        std::make_unique<juce::ButtonParameterAttachment>(*params.compEnabled, enableButton, nullptr));
    buttonAttachments.push_back(
        std::make_unique<juce::ButtonParameterAttachment>(*params.compLink, linkButton, nullptr));

    sliderAttachments.push_back(
        std::make_unique<juce::SliderParameterAttachment>(*params.compInput, input, nullptr));
    sliderAttachments.push_back(
        std::make_unique<juce::SliderParameterAttachment>(*params.compOutput, output, nullptr));
    sliderAttachments.push_back(
        std::make_unique<juce::SliderParameterAttachment>(*params.compAttack, attack, nullptr));
    sliderAttachments.push_back(
        std::make_unique<juce::SliderParameterAttachment>(*params.compRelease, release, nullptr));
    sliderAttachments.push_back(
        std::make_unique<juce::SliderParameterAttachment>(*params.compMix, mix, nullptr));
}

void BusCompOverlay::timerCallback()
{
    if (! isVisible())
    {
        return;
    }

    const auto& params = processor.getBusInsertParams(busIndex);

    // The ratio buttons are a radio group driven by a choice parameter, so
    // their lit state is polled rather than owned - automation and preset loads
    // move it too.
    if (params.compRatio != nullptr)
    {
        const auto selected = params.compRatio->getIndex();
        for (int i = 0; i < static_cast<int>(ratioButtons.size()); ++i)
        {
            ratioButtons[static_cast<std::size_t>(i)].setToggleState(i == selected, juce::dontSendNotification);
        }
    }

    mixValue.setText(juce::String(juce::roundToInt(mix.getValue() * 100.0)) + "%", juce::dontSendNotification);

    const auto reduction = processor.getBusGainReductionDb(busIndex);
    if (std::abs(reduction - meterDb) > 0.01f)
    {
        meterDb = reduction;
        repaint(meterArea);
    }
}

void BusCompOverlay::resized()
{
    refreshCardStyle();

    auto header = headerBounds();
    closeButton.setBounds(header.removeFromRight(configInt(uiConfig, "busInserts.buttonWidth", 62)).reduced(0, 3));
    header.removeFromRight(6);
    enableButton.setBounds(header.removeFromRight(configInt(uiConfig, "busInserts.enableWidth", 42)).reduced(0, 3));

    auto panel = innerOverlayBounds().reduced(configInt(uiConfig, "busInserts.comp.innerPadding", 14));

    // The rack ears carry the screws and nothing else, exactly as they do on
    // the unit: they are what make a panel read as a panel rather than a box.
    const auto earWidth = configInt(uiConfig, "busInserts.comp.earWidth", 26);
    panel.removeFromLeft(earWidth);
    panel.removeFromRight(earWidth);

    const auto gap = configInt(uiConfig, "busInserts.comp.sectionGap", 10);
    const auto legendHeight = configInt(uiConfig, "busInserts.comp.legendHeight", 14);
    const auto largeKnob = configInt(uiConfig, "busInserts.comp.largeKnobSize", 62);
    const auto smallKnob = configInt(uiConfig, "busInserts.comp.smallKnobSize", 34);

    // Left to right: INPUT, OUTPUT, the two time constants stacked, the ratio
    // bank, the meter, and finally the two controls this unit has that the
    // original does not - where its meter-select bank sat.
    auto column = [&](int width)
    {
        auto slot = panel.removeFromLeft(width);
        panel.removeFromLeft(gap);
        return slot;
    };

    const auto largeColumn = largeKnob + configInt(uiConfig, "busInserts.comp.scaleMargin", 30);

    {
        auto slot = column(largeColumn);
        slot.removeFromBottom(legendHeight);
        inputKnobArea = slot.toFloat().withSizeKeepingCentre(static_cast<float>(largeKnob),
                                                             static_cast<float>(largeKnob));
        input.setBounds(inputKnobArea.toNearestInt());
    }
    {
        auto slot = column(largeColumn);
        slot.removeFromBottom(legendHeight);
        outputKnobArea = slot.toFloat().withSizeKeepingCentre(static_cast<float>(largeKnob),
                                                              static_cast<float>(largeKnob));
        output.setBounds(outputKnobArea.toNearestInt());
    }

    {
        // ATTACK above RELEASE, both small, as the panel stacks them.
        auto slot = column(configInt(uiConfig, "busInserts.comp.timeColumnWidth", 70));
        const auto half = slot.getHeight() / 2;
        auto top = slot.removeFromTop(half);
        auto bottom = slot;
        top.removeFromBottom(legendHeight);
        bottom.removeFromBottom(legendHeight);
        attack.setBounds(top.withSizeKeepingCentre(smallKnob, smallKnob));
        release.setBounds(bottom.withSizeKeepingCentre(smallKnob, smallKnob));
    }

    {
        auto slot = column(configInt(uiConfig, "busInserts.comp.ratioWidth", 46));
        slot.removeFromBottom(legendHeight);
        ratioBankArea = slot.toFloat();

        const auto buttonGap = configInt(uiConfig, "busInserts.comp.ratioButtonGap", 3);
        const auto count = static_cast<int>(ratioButtons.size());
        const auto buttonHeight = (slot.getHeight() - buttonGap * (count - 1)) / count;
        for (auto& button : ratioButtons)
        {
            button.setBounds(slot.removeFromTop(buttonHeight));
            slot.removeFromTop(buttonGap);
        }
    }

    {
        auto slot = column(configInt(uiConfig, "busInserts.comp.meterWidth", 176));
        meterArea = slot.reduced(0, configInt(uiConfig, "busInserts.comp.meterInset", 6));
    }

    {
        // MIX and LINK, in the space the meter-select bank occupies on the
        // hardware. Placed here rather than squeezed in among the four original
        // controls: they are ours, and putting them where a switch bank lives
        // keeps the rest of the panel honest.
        mixBankArea = panel.toFloat();
        auto slot = panel;
        auto knobSlot = slot.removeFromTop(slot.getHeight() / 2);
        knobSlot.removeFromBottom(legendHeight);
        mix.setBounds(knobSlot.withSizeKeepingCentre(smallKnob, smallKnob));
        mixValue.setBounds(knobSlot.getX(), knobSlot.getBottom(), knobSlot.getWidth(), legendHeight);

        slot.removeFromBottom(legendHeight);
        linkButton.setBounds(slot.withSizeKeepingCentre(juce::jmin(slot.getWidth(), 46),
                                                        juce::jmin(slot.getHeight(), 22)));
    }
}

// The lit VU. Revision H changed the movement to a light-box type with two
// internal lamps, so the face is illuminated rather than merely pale - that
// glow is most of what distinguishes it from the black-face revisions.
void BusCompOverlay::paintMeter(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto bezelColour = configColour(uiConfig, "busInserts.comp.meterBezelColor",
                                          juce::Colour::fromRGB(26, 52, 96));
    const auto faceColour = configColour(uiConfig, "busInserts.comp.meterFaceColor",
                                         juce::Colour::fromRGB(238, 231, 210));
    const auto inkColour = configColour(uiConfig, "busInserts.comp.meterInkColor",
                                        juce::Colour::fromRGB(38, 36, 32));
    const auto needleColour = configColour(uiConfig, "busInserts.comp.meterNeedleColor",
                                           juce::Colour::fromRGB(24, 24, 26));
    const auto glowColour = configColour(uiConfig, "busInserts.comp.meterGlowColor",
                                         juce::Colour::fromRGBA(255, 244, 206, 210));

    const auto& params = processor.getBusInsertParams(busIndex);
    const auto live = params.compEnabled != nullptr && params.compEnabled->get();

    // The bezel the movement sits in.
    g.setColour(bezelColour);
    g.fillRoundedRectangle(area, 3.0f);
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 90));
    g.drawRoundedRectangle(area.reduced(0.5f), 3.0f, 1.0f);

    auto face = area.reduced(configFloat(uiConfig, "busInserts.comp.meterBezelWidth", 7.0f));
    face = face.withTrimmedBottom(face.getHeight() * 0.24f);

    g.setColour(faceColour);
    g.fillRect(face);

    // The two lamps, behind the face. Off when the unit is bypassed, which is
    // the quickest read on this panel of whether anything is happening.
    if (live)
    {
        for (const auto x : { face.getX() + face.getWidth() * 0.28f, face.getX() + face.getWidth() * 0.72f })
        {
            juce::ColourGradient lamp(glowColour, x, face.getBottom(),
                                      glowColour.withAlpha(0.0f), x, face.getY() - face.getHeight() * 0.35f,
                                      true);
            g.setGradientFill(lamp);
            g.fillRect(face);
        }
    }

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 70));
    g.drawRect(face, 1.0f);

    // The scale. Gain reduction runs backwards - 0 at the right, and the needle
    // falls to the left as the unit works, which is why the meter "drops".
    const auto pivot = juce::Point<float>(face.getCentreX(), face.getBottom() + face.getHeight() * 0.62f);
    const auto radius = face.getHeight() * 1.34f;
    constexpr auto kSpan = 0.56f;
    constexpr auto kFullScaleDb = 20.0f;

    auto angleFor = [&](float db)
    {
        const auto position = juce::jlimit(0.0f, 1.0f, db / kFullScaleDb);
        return kSpan - position * (kSpan * 2.0f);
    };

    g.setColour(inkColour.withAlpha(0.85f));
    for (const auto db : { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f, 10.0f, 15.0f, 20.0f })
    {
        const auto angle = angleFor(db);
        const auto direction = juce::Point<float>(std::sin(angle), -std::cos(angle));
        const auto major = db == 0.0f || db == 5.0f || db == 10.0f || db == 20.0f;
        g.drawLine({ pivot + direction * (radius - (major ? 9.0f : 5.0f)), pivot + direction * radius },
                   major ? 1.5f : 0.9f);

        if (major)
        {
            g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
            g.drawText(juce::String(juce::roundToInt(db)),
                       juce::Rectangle<float>(16.0f, 9.0f)
                           .withCentre(pivot + direction * (radius - 16.0f)),
                       juce::Justification::centred, false);
        }
    }

    const auto angle = angleFor(live ? meterDb : 0.0f);
    const auto direction = juce::Point<float>(std::sin(angle), -std::cos(angle));
    g.setColour(needleColour.withAlpha(live ? 1.0f : 0.35f));
    g.drawLine({ pivot + direction * (radius * 0.30f), pivot + direction * (radius - 3.0f) }, 1.8f);

    // The badge below the movement. This plug-in's own mark: the panel is a
    // generic archetype of a rack FET limiter, and it carries no other maker's
    // name, model number or logo.
    auto badge = area.withTop(face.getBottom() + 3.0f).reduced(10.0f, 2.0f);
    if (badge.getHeight() > 8.0f)
    {
        g.setColour(configColour(uiConfig, "busInserts.comp.badgeColor",
                                 juce::Colour::fromRGBA(255, 255, 255, 30)));
        g.fillRoundedRectangle(badge, 2.0f);
        panel::drawLegend(g, badge, "P(X3) LIMITING AMPLIFIER",
                          configColour(uiConfig, "busInserts.comp.badgeTextColor",
                                       juce::Colour::fromRGB(232, 238, 248)),
                          configFloat(uiConfig, "busInserts.comp.badgeFontSize", 7.0f));
    }
}

void BusCompOverlay::paint(juce::Graphics& g)
{
    // The card frame and the solid inner panel, exactly as the EQ sheet draws
    // them - the two are the same object with different contents.
    BusInsertOverlay::paint(g);

    const auto inner = innerOverlayBounds().toFloat();
    const auto ink = configColour(uiConfig, "busInserts.comp.inkColor", juce::Colour::fromRGB(46, 48, 52));
    const auto legendSize = configFloat(uiConfig, "busInserts.comp.legendFontSize", 8.5f);

    // Brushed aluminium. Revision H's faceplate is natural brushed aluminium
    // rather than the black anodised finish of the revisions before it, so the
    // grain runs horizontally over a solid light panel.
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addRoundedRectangle(inner, innerStyle.radius);
        g.reduceClipRegion(clip);
        paintSurfaceNoise(g, inner, configFloat(uiConfig, "busInserts.comp.grainAmount", 0.06f));
    }

    // Rack ears: a seam and two screws at each end.
    const auto earWidth = static_cast<float>(configInt(uiConfig, "busInserts.comp.earWidth", 26))
                          + static_cast<float>(configInt(uiConfig, "busInserts.comp.innerPadding", 14));
    const auto screwRadius = configFloat(uiConfig, "busInserts.comp.screwRadius", 3.4f);

    g.setColour(ink.withAlpha(0.18f));
    for (const auto x : { inner.getX() + earWidth, inner.getRight() - earWidth })
    {
        g.drawVerticalLine(juce::roundToInt(x), inner.getY() + 6.0f, inner.getBottom() - 6.0f);
    }

    for (const auto x : { inner.getX() + earWidth * 0.5f, inner.getRight() - earWidth * 0.5f })
    {
        panel::drawScrew(g, { x, inner.getY() + 14.0f }, screwRadius);
        panel::drawScrew(g, { x, inner.getBottom() - 14.0f }, screwRadius);
    }

    // The engraved scales around the two large knobs. The numbers belong to the
    // panel, not to the cap, which is why they do not turn with it.
    static const juce::StringArray kInputMarks { "-12", "-6", "0", "6", "12", "18", "24", "30", "36" };
    static const juce::StringArray kOutputMarks { "-24", "-16", "-8", "0", "8", "16", "24" };

    if (! inputKnobArea.isEmpty())
    {
        panel::drawKnobScale(g, inputKnobArea, kInputMarks, ink.withAlpha(0.8f));
        panel::drawLegend(g, inputKnobArea.withY(inputKnobArea.getBottom() + 20.0f).withHeight(14.0f),
                          "INPUT", ink, legendSize);
    }
    if (! outputKnobArea.isEmpty())
    {
        panel::drawKnobScale(g, outputKnobArea, kOutputMarks, ink.withAlpha(0.8f));
        panel::drawLegend(g, outputKnobArea.withY(outputKnobArea.getBottom() + 20.0f).withHeight(14.0f),
                          "OUTPUT", ink, legendSize);
    }

    // The rest of the legends, engraved under what they name.
    auto legendUnder = [&](juce::Rectangle<int> control, const juce::String& text)
    {
        if (control.isEmpty())
        {
            return;
        }
        panel::drawLegend(g,
                          juce::Rectangle<float>(static_cast<float>(control.getX()) - 12.0f,
                                                 static_cast<float>(control.getBottom()) + 2.0f,
                                                 static_cast<float>(control.getWidth()) + 24.0f,
                                                 13.0f),
                          text, ink, legendSize);
    };

    legendUnder(attack.getBounds(), "ATTACK");
    legendUnder(release.getBounds(), "RELEASE");
    legendUnder(linkButton.getBounds(), "LINK");

    if (! ratioBankArea.isEmpty())
    {
        panel::drawLegend(g, ratioBankArea.withY(ratioBankArea.getBottom() + 1.0f).withHeight(13.0f),
                          "RATIO", ink, legendSize);
    }
    if (! mixBankArea.isEmpty())
    {
        panel::drawLegend(g,
                          juce::Rectangle<float>(mixBankArea.getX(),
                                                 static_cast<float>(mix.getBounds().getBottom()) + 2.0f,
                                                 mixBankArea.getWidth(), 13.0f),
                          "MIX", ink, legendSize);
    }

    paintMeter(g, meterArea.toFloat());
}

} // namespace px3::ui
