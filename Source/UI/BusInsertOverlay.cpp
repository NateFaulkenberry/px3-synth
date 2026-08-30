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

// A cached face has to be rendered at the DISPLAY's scale, not the component's.
// Drawn at 1x and blitted into a 2x context every glyph is upscaled, which is
// exactly how cached text goes soft on a Retina panel.
float displayScale()
{
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        return static_cast<float>(display->scale);
    }
    return 1.0f;
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
SheetCloseButton::SheetCloseButton()
    : juce::Button("CLOSE")
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Close");
}

void SheetCloseButton::applyStyle(const Style& styleIn)
{
    style = styleIn;
    repaint();
}

void SheetCloseButton::paintButton(juce::Graphics& g,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto side = juce::jmin(bounds.getWidth(), bounds.getHeight());
    if (side <= 0.0f)
    {
        return;
    }

    const auto box = juce::Rectangle<float>(side, side).withCentre(bounds.getCentre());
    const auto tint = shouldDrawButtonAsHighlighted ? style.hover : style.glyph;

    g.setColour(style.seat.withMultipliedAlpha(shouldDrawButtonAsDown ? 1.2f : 1.0f));
    g.fillEllipse(box);

    g.setColour((shouldDrawButtonAsHighlighted ? style.hover : style.ring)
                    .withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
    g.drawEllipse(box.reduced(style.ringWidth * 0.5f), style.ringWidth);

    // The X, inset from the ring so the two never touch.
    const auto inset = juce::jlimit(0.1f, 0.45f, style.glyphInset) * side;
    const auto glyph = box.reduced(inset);
    g.setColour(tint.withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
    g.drawLine(glyph.getX(), glyph.getY(), glyph.getRight(), glyph.getBottom(), style.glyphWidth);
    g.drawLine(glyph.getRight(), glyph.getY(), glyph.getX(), glyph.getBottom(), style.glyphWidth);
}

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
    refreshHeaderButtonStyles();

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

void BusInsertOverlay::refreshHeaderButtonStyles()
{
    const auto sheet = cardStyleKey() == "busInsertComp" ? juce::String("comp") : juce::String("eq");

    MixerToggleButton::Style enableFallback;
    enableFallback.width = 42;
    enableFallback.height = 24;

    enableStyle = px3::ui::mixerToggleStyleFromConfig(
        uiConfig.get(), "busInserts.enableButton", "busInserts." + sheet + ".enableButton", enableFallback);
    enableButton.applyStyle(enableStyle);

    // Its own X and Y, like the close glyph's.
    enableOffsetX = 0;
    enableOffsetY = 0;
    if (uiConfig != nullptr)
    {
        for (const auto& base : { juce::String("busInserts.enableButton"),
                                  "busInserts." + sheet + ".enableButton" })
        {
            if (const auto v = uiConfig->getValue(base + ".offsetX"); ! v.isVoid())
            {
                enableOffsetX = static_cast<int>(v);
            }
            if (const auto v = uiConfig->getValue(base + ".offsetY"); ! v.isVoid())
            {
                enableOffsetY = static_cast<int>(v);
            }
            if (const auto v = uiConfig->getValue(base + ".anchor"); ! v.isVoid())
            {
                const auto text = v.toString().trim().toLowerCase();
                if (text == "innertopleft")       enableAnchor = EnableAnchor::innerTopLeft;
                else if (text == "headertopright") enableAnchor = EnableAnchor::headerTopRight;
            }
        }
    }

    // The close glyph, shared block then per-sheet override, same precedence.
    SheetCloseButton::Style closeStyle;
    if (uiConfig != nullptr)
    {
        const auto apply = [&](const juce::String& base)
        {
            const auto number = [&](const char* key, auto& field)
            {
                if (const auto value = uiConfig->getValue(base + key); ! value.isVoid())
                {
                    field = static_cast<std::remove_reference_t<decltype(field)>>(static_cast<double>(value));
                }
            };
            const auto colour = [&](const char* key, juce::Colour& field)
            {
                if (const auto value = uiConfig->getValue(base + key); ! value.isVoid())
                {
                    field = uiConfig->getColour(base + key, field);
                }
            };

            number(".size", closeStyle.size);
            number(".offsetX", closeStyle.offsetX);
            number(".offsetY", closeStyle.offsetY);
            number(".ringWidth", closeStyle.ringWidth);
            number(".glyphWidth", closeStyle.glyphWidth);
            number(".glyphInset", closeStyle.glyphInset);
            colour(".seatColor", closeStyle.seat);
            colour(".ringColor", closeStyle.ring);
            colour(".glyphColor", closeStyle.glyph);
            colour(".hoverColor", closeStyle.hover);
        };

        apply("busInserts.closeButton");
        apply("busInserts." + sheet + ".closeButton");
    }

    closeButton.applyStyle(closeStyle);
}

// Both header controls are placed by coordinate, anchored to the top right of
// the header rectangle and offset from there. Anchoring to a CORNER rather than
// to the row's centre is what lets headerHeight go to 0 without either button
// jumping: a centre moves when the row's height changes, a corner does not.
void BusInsertOverlay::layoutHeaderButtons()
{
    const auto header = headerBounds();

    const auto& close = closeButton.getStyle();
    closeButton.setBounds(header.getRight() - close.size + close.offsetX,
                          header.getY() + close.offsetY,
                          close.size,
                          close.size);

    const auto width = juce::jmax(8, enableStyle.width);
    const auto height = juce::jmax(8, enableStyle.height);
    const auto gap = configInt(uiConfig, "busInserts.headerButtonGap", 8);

    if (enableAnchor == EnableAnchor::innerTopLeft)
    {
        const auto inner = innerOverlayBounds();
        enableButton.setBounds(inner.getX() + enableOffsetX,
                               inner.getY() + enableOffsetY,
                               width,
                               height);
    }
    else
    {
        enableButton.setBounds(header.getRight() - close.size - gap - width + enableOffsetX,
                               header.getY() + enableOffsetY,
                               width,
                               height);
    }

    // Both sit over the panel, and on the EQ sheet the graph is added after
    // them - so without this the graph would be drawn on top of the enable
    // button it now shares a corner with.
    closeButton.toFront(false);
    enableButton.toFront(false);
}

juce::Rectangle<int> BusInsertOverlay::headerBounds() const
{
    // Purely the space RESERVED above the inner panel. It does not size the
    // buttons and does not constrain them: both are placed by coordinate from
    // this rectangle's top right, so headerHeight can be 0 and they simply sit
    // over the panel instead of above it.
    const auto declared = uiConfig != nullptr ? uiConfig->getInt("busInserts.headerHeight", 30) : 30;
    return card.contentBelowTitle().withHeight(juce::jmax(0, declared));
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

    refreshControlEnablement();
    refreshReadouts();
}

// A bypassed EQ's controls are dead: the graph refuses the mouse and every band
// control greys out. Polled rather than attached because the enable also moves
// from automation, from a preset load, and from the strip.
void BusEqOverlay::refreshControlEnablement()
{
    const auto& params = processor.getBusInsertParams(busIndex);
    const auto live = params.eqEnabled != nullptr && params.eqEnabled->get();

    if (live == controlsLive && enablementApplied)
    {
        return;
    }

    controlsLive = live;
    enablementApplied = true;
    graph.setEditable(live);

    for (auto& strip : bands)
    {
        strip.type.setEnabled(live);
        strip.frequency.setEnabled(live);
        strip.q.setEnabled(live);
        for (auto* label : { &strip.caption, &strip.frequencyValue, &strip.gainValue,
                             &strip.qValue, &strip.frequencyCaption, &strip.gainCaption,
                             &strip.qCaption })
        {
            label->setEnabled(live);
        }
    }

    // The gain knobs have a second reason to be disabled, so refreshReadouts
    // owns them; it runs immediately after this on every tick.
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
        // Two reasons a gain knob is dead - the band is a pass filter, or the
        // whole EQ is bypassed - and both are decided here, or the two would
        // overwrite each other every frame.
        strip.gain.setEnabled(controlsLive && ! isPass);
    }
}

void BusEqOverlay::resized()
{
    refreshCardStyle();

    layoutHeaderButtons();

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

    // What the movement is wired to. Same latching push buttons as the ratio
    // bank, because they are the same kind of switch.
    static const std::array<const char*, 3> meterLegends { { "GR", "IN", "OUT" } };
    for (std::size_t i = 0; i < meterModeButtons.size(); ++i)
    {
        auto& button = meterModeButtons[i];
        button.setButtonText(meterLegends[i]);
        button.setClickingTogglesState(false);
        button.setLookAndFeel(&pushLook);
        addAndMakeVisible(button);
        button.onClick = [this, i]()
        {
            const auto& params = processor.getBusInsertParams(busIndex);
            if (params.compMeterMode == nullptr)
            {
                return;
            }

            // Through the parameter, so the choice is recorded by the host and
            // travels with the session and the preset.
            params.compMeterMode->setValueNotifyingHost(
                params.compMeterMode->convertTo0to1(static_cast<float>(i)));
        };
    }

    // The legends are engraved onto the panel in paint(), not placed as labels,
    // so these captions carry only the mix readout.
    addAndMakeVisible(mixValue);
    styleCaption(mixValue, juce::Colour::fromRGB(38, 40, 44), 9.5f, juce::Justification::centred);

    for (auto* caption : { &inputCaption, &outputCaption, &attackCaption, &releaseCaption, &mixCaption })
    {
        caption->setVisible(false);
    }

    addAndMakeVisible(meter);

    // The movement asks for its own target every frame and runs its own
    // physics; this sheet only tells it what the level is and whether the unit
    // is live. See docs/VU_METER_IMPLEMENTATION.md.
    meter.getTargetPosition = [this]() -> double
    {
        switch (meterMode())
        {
            case px3::CompMeterMode::input:
                return VuMeterComponent::positionForLevelDb(
                    processor.getBusCompressorLevelDb(busIndex, true));
            case px3::CompMeterMode::output:
                return VuMeterComponent::positionForLevelDb(
                    processor.getBusCompressorLevelDb(busIndex, false));
            case px3::CompMeterMode::gainReduction:
            default:
                return VuMeterComponent::positionForReductionDb(
                    processor.getBusGainReductionDb(busIndex));
        }
    };

    meter.isLive = [this]()
    {
        const auto& params = processor.getBusInsertParams(busIndex);
        return params.compEnabled != nullptr && params.compEnabled->get();
    };

    // The sheet's own poll is for the switches and readouts, not the needle.
    startTimerHz(30);
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
    for (auto& button : meterModeButtons)
    {
        button.setLookAndFeel(nullptr);
    }
    linkButton.setLookAndFeel(nullptr);
}

void BusCompOverlay::uiConfigChanged()
{
    face = {};
    meter.setUIConfig(uiConfig);
}

void BusCompOverlay::knobLookAndFeelChanged()
{
    // The plugin's own knob, tinted per section: the two large gain controls
    // and the mix blend take the warm accent, the two time constants the cool
    // one, so what a control DOES is readable before its legend is.
    //
    // The engraved scales around INPUT and OUTPUT are drawn from these
    // sliders' own rotary parameters, so swapping the knob look cannot leave
    // the numbers pointing somewhere the pointer never reaches.
    const auto warm = configColour(uiConfig, "busInserts.comp.largeKnobColor",
                                   juce::Colour::fromRGB(234, 166, 76));
    const auto cool = configColour(uiConfig, "busInserts.comp.smallKnobColor",
                                   juce::Colour::fromRGB(120, 186, 255));

    for (auto* slider : { &input, &output, &mix })
    {
        configureRotary(*slider, knobLookAndFeel);
        slider->setColour(juce::Slider::rotarySliderFillColourId, warm);
        addAndMakeVisible(*slider);
    }

    for (auto* slider : { &attack, &release })
    {
        configureRotary(*slider, knobLookAndFeel);
        slider->setColour(juce::Slider::rotarySliderFillColourId, cool);
        addAndMakeVisible(*slider);
    }
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

// A bypassed compressor's controls are dead, exactly as the EQ's are: a knob
// that moves a parameter nothing is reading is offering an edit that changes
// nothing audible.
void BusCompOverlay::refreshControlEnablement()
{
    const auto& params = processor.getBusInsertParams(busIndex);
    const auto live = params.compEnabled != nullptr && params.compEnabled->get();

    if (live == controlsLive && enablementApplied)
    {
        return;
    }

    controlsLive = live;
    enablementApplied = true;

    for (auto* slider : { &input, &output, &attack, &release, &mix })
    {
        slider->setEnabled(live);
    }
    for (auto& button : ratioButtons)
    {
        button.setEnabled(live);
    }
    for (auto& button : meterModeButtons)
    {
        button.setEnabled(live);
    }
    linkButton.setEnabled(live);
    mixValue.setEnabled(live);

    // The panel legends and the meter face are painted, not components, so they
    // follow the enable through a repaint rather than a flag.
    repaint();
}

px3::CompMeterMode BusCompOverlay::meterMode() const
{
    const auto& params = processor.getBusInsertParams(busIndex);
    if (params.compMeterMode == nullptr)
    {
        return px3::CompMeterMode::gainReduction;
    }
    return static_cast<px3::CompMeterMode>(juce::jlimit(0, 2, params.compMeterMode->getIndex()));
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

    refreshControlEnablement();

    // The mode buttons are a radio group driven by a choice parameter, so their
    // lit state is polled for the same reason the ratio bank's is.
    if (params.compMeterMode != nullptr)
    {
        const auto selected = params.compMeterMode->getIndex();
        for (int i = 0; i < static_cast<int>(meterModeButtons.size()); ++i)
        {
            meterModeButtons[static_cast<std::size_t>(i)]
                .setToggleState(i == selected, juce::dontSendNotification);
        }
    }

    // The needle is the meter component's business; this only has to keep the
    // face in step with the mode switch.
    meter.setMode(meterMode() == px3::CompMeterMode::gainReduction
                      ? VuMeterComponent::Mode::gainReduction
                      : VuMeterComponent::Mode::level);
}

void BusCompOverlay::resized()
{
    face = {};
    refreshCardStyle();

    layoutHeaderButtons();

    auto panel = innerOverlayBounds().reduced(configInt(uiConfig, "busInserts.comp.innerPadding", 14));

    // The rack ears carry the screws and nothing else, exactly as they do on
    // the unit: they are what make a panel read as a panel rather than a box.
    const auto earWidth = configInt(uiConfig, "busInserts.comp.earWidth", 26);
    panel.removeFromLeft(earWidth);
    panel.removeFromRight(earWidth);

    // MIX and LINK are taken off the RIGHT first, at a fixed width, so they stay
    // put when everything else moves. Laying them out last from "whatever is
    // left" would have made their position a function of every column before
    // them - shifting the meter would drag them along with it.
    auto mixSlot = panel.removeFromRight(configInt(uiConfig, "busInserts.comp.mixColumnWidth", 70));

    // The whole MIX/LINK group shifts together - knob, both its labels, the
    // button and its legend - because everything below is measured from this
    // rectangle. Moving the pieces individually would need four offsets that
    // have to be kept equal to stay aligned.
    mixSlot.translate(configInt(uiConfig, "busInserts.comp.mixOffsetX", 0), 0);
    mixBankArea = mixSlot.toFloat();

    // Everything else is offset from the left ear by this much.
    panel.removeFromLeft(configInt(uiConfig, "busInserts.comp.contentOffsetX", 30));

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
        slot = slot.reduced(0, configInt(uiConfig, "busInserts.comp.meterInset", 6));

        // The mode switch sits under the movement, as the meter-select bank
        // does on the hardware.
        meterModeArea = slot.removeFromBottom(configInt(uiConfig, "busInserts.comp.meterModeHeight", 20));
        slot.removeFromBottom(configInt(uiConfig, "busInserts.comp.meterModeGap", 5));
        meterArea = slot;
        meter.setBounds(meterArea);

        auto row = meterModeArea;
        const auto buttonGap = configInt(uiConfig, "busInserts.comp.meterModeButtonGap", 4);
        const auto count = static_cast<int>(meterModeButtons.size());
        const auto buttonWidth = (row.getWidth() - buttonGap * (count - 1)) / count;
        for (auto& button : meterModeButtons)
        {
            button.setBounds(row.removeFromLeft(buttonWidth));
            row.removeFromLeft(buttonGap);
        }
    }

    {
        // MIX and LINK, in the space the meter-select bank occupies on the
        // hardware. Placed here rather than squeezed in among the four original
        // controls: they are ours, and putting them where a switch bank lives
        // keeps the rest of the panel honest.
        auto slot = mixSlot;

        // The whole MIX stack sits lower than the switch bank it replaces.
        slot.removeFromTop(configInt(uiConfig, "busInserts.comp.mixOffsetY", 20));

        // 20% over the other small knobs: it is the one control on this panel
        // that is ours rather than the unit's, and it is reached for often.
        const auto mixKnob = juce::roundToInt(static_cast<float>(smallKnob)
                                              * configFloat(uiConfig, "busInserts.comp.mixKnobScale", 1.2f));

        // The three pieces are packed to their own height and the GROUP is
        // centred, rather than each being placed in a share of the column.
        // Giving the knob half the column and centring it in what was left put
        // most of the panel's height between the label and the knob it names.
        const auto stackGap = configInt(uiConfig, "busInserts.comp.mixStackGap", 2);
        const auto stackHeight = legendHeight + stackGap + mixKnob + stackGap + legendHeight;

        auto stack = slot.removeFromTop(slot.getHeight() / 2)
                         .withSizeKeepingCentre(slot.getWidth(), juce::jmin(stackHeight, slot.getHeight()));

        mixLabelArea = stack.removeFromTop(legendHeight).toFloat();
        stack.removeFromTop(stackGap);
        mix.setBounds(stack.removeFromTop(mixKnob).withSizeKeepingCentre(mixKnob, mixKnob));
        stack.removeFromTop(stackGap);
        mixValue.setBounds(stack.removeFromTop(legendHeight));

        slot.removeFromBottom(legendHeight);
        linkButton.setBounds(slot.withSizeKeepingCentre(juce::jmin(slot.getWidth(), 46),
                                                        juce::jmin(slot.getHeight(), 22)));
    }
}

// Everything static on the face, rendered once into an image. See the member
// declaration for why: the grain is a per-pixel loop and it was being run on
// every needle frame.
void BusCompOverlay::rebuildFace()
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
    {
        face = {};
        return;
    }

    const auto scale = juce::jlimit(1.0f, 4.0f, displayScale());
    face = juce::Image(juce::Image::ARGB,
                       juce::roundToInt(static_cast<float>(bounds.getWidth()) * scale),
                       juce::roundToInt(static_cast<float>(bounds.getHeight()) * scale),
                       true);

    juce::Graphics g(face);
    g.addTransform(juce::AffineTransform::scale(scale));

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

    const auto sweep = input.getRotaryParameters();

    if (! inputKnobArea.isEmpty())
    {
        panel::drawKnobScale(g, inputKnobArea, kInputMarks, ink.withAlpha(0.8f),
                             sweep.startAngleRadians, sweep.endAngleRadians);
        panel::drawLegend(g, inputKnobArea.withY(inputKnobArea.getBottom() + 20.0f).withHeight(14.0f),
                          "INPUT", ink, legendSize);
    }
    if (! outputKnobArea.isEmpty())
    {
        panel::drawKnobScale(g, outputKnobArea, kOutputMarks, ink.withAlpha(0.8f),
                             sweep.startAngleRadians, sweep.endAngleRadians);
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
    if (! mixLabelArea.isEmpty())
    {
        // Above the knob, with the percentage below it - so the pair reads
        // name-then-value downward, as the rest of the panel's legends do.
        panel::drawLegend(g, mixLabelArea, "MIX", ink, legendSize);
    }

}

void BusCompOverlay::paint(juce::Graphics& g)
{
    const auto scale = juce::jlimit(1.0f, 4.0f, displayScale());
    const auto wantedWidth = juce::roundToInt(static_cast<float>(getWidth()) * scale);
    const auto wantedHeight = juce::roundToInt(static_cast<float>(getHeight()) * scale);
    if (face.isNull() || face.getWidth() != wantedWidth || face.getHeight() != wantedHeight)
    {
        rebuildFace();
    }

    if (face.isValid())
    {
        // Drawn INTO the component's bounds rather than 1:1, so the extra
        // resolution is used rather than overflowing.
        g.drawImage(face, getLocalBounds().toFloat());
    }

    // The movement paints itself - see VuMeterComponent.
}

} // namespace px3::ui
