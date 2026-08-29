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
    g.setColour(innerStyle.colour);
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
    static const std::array<const char*, 5> ratioLegends { { "4", "8", "12", "20", "ALL" } };

    for (std::size_t i = 0; i < ratioButtons.size(); ++i)
    {
        auto& button = ratioButtons[i];
        button.setButtonText(ratioLegends[i]);
        button.setClickingTogglesState(false);
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

    addAndMakeVisible(linkButton);

    for (const auto& entry : { std::make_pair(&inputCaption, "INPUT"),
                               std::make_pair(&outputCaption, "OUTPUT"),
                               std::make_pair(&attackCaption, "ATTACK"),
                               std::make_pair(&releaseCaption, "RELEASE"),
                               std::make_pair(&mixCaption, "MIX") })
    {
        addAndMakeVisible(*entry.first);
        styleCaption(*entry.first, juce::Colour::fromRGB(40, 42, 46), 10.0f, juce::Justification::centred);
        entry.first->setText(entry.second, juce::dontSendNotification);
    }

    addAndMakeVisible(mixValue);
    styleCaption(mixValue, juce::Colour::fromRGB(40, 42, 46), 10.0f, juce::Justification::centred);

    startTimerHz(24);
}

BusCompOverlay::~BusCompOverlay()
{
    stopTimer();
    clearAttachments();
}

void BusCompOverlay::knobLookAndFeelChanged()
{
    for (auto* slider : { &input, &output, &attack, &release, &mix })
    {
        configureRotary(*slider, knobLookAndFeel);
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

    auto area = innerOverlayBounds().reduced(configInt(uiConfig, "busInserts.comp.innerPadding", 16));

    // Laid out as the hardware is: the meter on the right, the large controls
    // to its left, the ratio buttons in a row beneath.
    const auto meterWidth = configInt(uiConfig, "busInserts.comp.meterWidth", 200);
    meterArea = area.removeFromRight(meterWidth).removeFromTop(configInt(uiConfig, "busInserts.comp.meterHeight", 124));
    area.removeFromRight(configInt(uiConfig, "busInserts.comp.meterGap", 16));

    const auto knobSize = configInt(uiConfig, "busInserts.comp.knobSize", 58);
    constexpr auto captionHeight = 13;

    auto knobRow = area.removeFromTop(knobSize + captionHeight * 2 + 6);
    const auto knobGap = configInt(uiConfig, "busInserts.comp.knobGap", 10);
    constexpr auto slots = 5;
    const auto slotWidth = (knobRow.getWidth() - knobGap * (slots - 1)) / slots;

    for (const auto& entry : { std::make_pair(&input, &inputCaption),
                               std::make_pair(&output, &outputCaption),
                               std::make_pair(&attack, &attackCaption),
                               std::make_pair(&release, &releaseCaption),
                               std::make_pair(&mix, &mixCaption) })
    {
        auto slot = knobRow.removeFromLeft(slotWidth);
        knobRow.removeFromLeft(knobGap);
        entry.first->setBounds(slot.removeFromTop(knobSize).withSizeKeepingCentre(knobSize, knobSize));
        slot.removeFromTop(3);
        entry.second->setBounds(slot.removeFromTop(captionHeight));
    }

    // The mix readout sits under its own caption. It is the one control here
    // whose value is not obvious from the knob position, because 100% is the
    // compressor alone and people reach for it expecting a blend.
    mixValue.setBounds(mixCaption.getBounds().translated(0, captionHeight));

    area.removeFromTop(configInt(uiConfig, "busInserts.comp.ratioGap", 18));

    auto ratioRow = area.removeFromTop(configInt(uiConfig, "busInserts.comp.ratioHeight", 28));
    const auto ratioGap = configInt(uiConfig, "busInserts.comp.ratioButtonGap", 6);
    const auto ratioSlots = static_cast<int>(ratioButtons.size()) + 1;
    const auto ratioWidth = (ratioRow.getWidth() - ratioGap * (ratioSlots - 1)) / ratioSlots;

    for (auto& button : ratioButtons)
    {
        button.setBounds(ratioRow.removeFromLeft(ratioWidth));
        ratioRow.removeFromLeft(ratioGap);
    }

    linkButton.setBounds(ratioRow.removeFromLeft(ratioWidth));
}

void BusCompOverlay::paintMeter(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto faceColour = configColour(uiConfig, "busInserts.comp.meterFaceColor",
                                         juce::Colour::fromRGB(238, 231, 210));
    const auto inkColour = configColour(uiConfig, "busInserts.comp.meterInkColor",
                                        juce::Colour::fromRGB(38, 36, 32));
    const auto needleColour = configColour(uiConfig, "busInserts.comp.meterNeedleColor",
                                           juce::Colour::fromRGB(24, 24, 26));

    g.setColour(faceColour);
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(inkColour.withAlpha(0.45f));
    g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);

    // A moving-coil movement: the needle pivots below the face, and the scale
    // is an arc rather than a bar. On this unit the scale reads gain reduction,
    // so it runs backwards - 0 on the right, and the needle falls to the left
    // as the compressor works, which is why the hardware's meter "drops".
    const auto pivot = juce::Point<float>(area.getCentreX(), area.getBottom() + area.getHeight() * 0.55f);
    const auto radius = area.getHeight() * 1.28f;
    constexpr auto kSpan = 0.62f;   // radians either side of vertical
    constexpr auto kFullScaleDb = 20.0f;

    auto angleFor = [&](float db)
    {
        const auto position = juce::jlimit(0.0f, 1.0f, db / kFullScaleDb);
        return kSpan - position * (kSpan * 2.0f);
    };

    g.setColour(inkColour.withAlpha(0.8f));
    for (const auto db : { 0.0f, 3.0f, 5.0f, 7.0f, 10.0f, 15.0f, 20.0f })
    {
        const auto angle = angleFor(db);
        const auto direction = juce::Point<float>(std::sin(angle), -std::cos(angle));
        const auto outer = pivot + direction * radius;
        const auto inner = pivot + direction * (radius - (db == 0.0f || db == 20.0f ? 10.0f : 6.0f));
        g.drawLine({ inner, outer }, db == 0.0f ? 1.8f : 1.0f);
    }

    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    g.drawText("GAIN REDUCTION",
               area.withTrimmedTop(area.getHeight() * 0.62f).toNearestInt(),
               juce::Justification::centred);

    const auto& params = processor.getBusInsertParams(busIndex);
    const auto live = params.compEnabled != nullptr && params.compEnabled->get();

    const auto angle = angleFor(live ? meterDb : 0.0f);
    const auto direction = juce::Point<float>(std::sin(angle), -std::cos(angle));
    g.setColour(needleColour.withAlpha(live ? 1.0f : 0.4f));
    g.drawLine({ pivot + direction * (radius * 0.20f), pivot + direction * (radius - 2.0f) }, 2.0f);
}

void BusCompOverlay::paint(juce::Graphics& g)
{
    // The card frame and the solid inner panel, exactly as the EQ sheet draws
    // them - the two are the same object with different contents.
    BusInsertOverlay::paint(g);

    // The grain is what makes the inner panel read as brushed metal rather than
    // as flat plastic. It is texture over the solid fill, not a second fill,
    // and it is derived from pixel position so it does not shimmer between
    // frames. grainAmount 0 removes it.
    const auto inner = innerOverlayBounds().toFloat();
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addRoundedRectangle(inner, innerStyle.radius);
        g.reduceClipRegion(clip);
        paintSurfaceNoise(g, inner, configFloat(uiConfig, "busInserts.comp.grainAmount", 0.06f));
    }

    paintMeter(g, meterArea.toFloat());
}

} // namespace px3::ui
