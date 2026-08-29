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
    resized();
    repaint();
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

void BusInsertOverlay::paint(juce::Graphics&)
{
    // The subclasses draw their own faces. This exists so the base can be
    // instantiated in a test without a face.
}

//==============================================================================
// EQ
//==============================================================================
namespace
{
// The display's frequency axis. Fixed rather than following the bands, because
// a curve whose axis moves under it cannot be read.
constexpr float kCurveMinHz = 20.0f;
constexpr float kCurveMaxHz = 20000.0f;
constexpr float kCurveRangeDb = 20.0f;

float frequencyToX(float hz, juce::Rectangle<float> area)
{
    const auto position = std::log(hz / kCurveMinHz) / std::log(kCurveMaxHz / kCurveMinHz);
    return area.getX() + position * area.getWidth();
}

float decibelsToY(float db, juce::Rectangle<float> area)
{
    const auto position = 0.5f - juce::jlimit(-1.0f, 1.0f, db / kCurveRangeDb) * 0.5f;
    return area.getY() + position * area.getHeight();
}
} // namespace

BusEqOverlay::BusEqOverlay(PX3SynthAudioProcessor& processorIn)
    : BusInsertOverlay(processorIn)
{
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

void BusEqOverlay::rebuildForBus()
{
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
    // The curve is drawn from the LIVE processor, whose smoothing moves between
    // frames even when nothing is being dragged, so the display repaints on the
    // clock rather than on parameter changes.
    repaint(curveArea);
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
    auto area = getLocalBounds().reduced(configInt(uiConfig, "busInserts.eq.padding", 22));

    auto header = area.removeFromTop(configInt(uiConfig, "busInserts.eq.headerHeight", 34));
    const auto buttonWidth = configInt(uiConfig, "busInserts.eq.buttonWidth", 64);
    closeButton.setBounds(header.removeFromRight(buttonWidth).reduced(0, 4));
    header.removeFromRight(8);
    enableButton.setBounds(header.removeFromRight(configInt(uiConfig, "busInserts.eq.enableWidth", 44)).reduced(0, 4));

    area.removeFromTop(configInt(uiConfig, "busInserts.eq.headerGap", 12));

    curveArea = area.removeFromTop(configInt(uiConfig, "busInserts.eq.curveHeight", 190));
    area.removeFromTop(configInt(uiConfig, "busInserts.eq.curveGap", 16));

    const auto columnGap = configInt(uiConfig, "busInserts.eq.columnGap", 10);
    const auto knobSize = configInt(uiConfig, "busInserts.eq.knobSize", 54);
    const auto captionHeight = 14;
    const auto valueHeight = 13;

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
        column.removeFromTop(4);

        // The two inner bands have no selector, so they give the row back
        // rather than leaving a hole - the columns stay aligned because every
        // band reserves the same height for it.
        auto typeRow = column.removeFromTop(configInt(uiConfig, "busInserts.eq.typeHeight", 22));
        if (strip.type.isVisible())
        {
            strip.type.setBounds(typeRow);
        }
        column.removeFromTop(8);

        const auto rowHeight = knobSize + valueHeight + captionHeight + 4;
        for (const auto& entry : { std::make_tuple(&strip.frequency, &strip.frequencyCaption, &strip.frequencyValue),
                                   std::make_tuple(&strip.gain, &strip.gainCaption, &strip.gainValue),
                                   std::make_tuple(&strip.q, &strip.qCaption, &strip.qValue) })
        {
            auto row = column.removeFromTop(rowHeight);
            std::get<0>(entry)->setBounds(row.removeFromTop(knobSize).withSizeKeepingCentre(knobSize, knobSize));
            std::get<1>(entry)->setBounds(row.removeFromTop(captionHeight));
            std::get<2>(entry)->setBounds(row.removeFromTop(valueHeight));
        }
    }
}

void BusEqOverlay::paintCurve(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto gridColour = configColour(uiConfig, "busInserts.eq.gridColor",
                                         juce::Colour::fromRGBA(255, 255, 255, 34));
    const auto zeroColour = configColour(uiConfig, "busInserts.eq.zeroLineColor",
                                         juce::Colour::fromRGBA(255, 255, 255, 78));
    const auto curveColour = configColour(uiConfig, "busInserts.eq.curveColor",
                                          juce::Colour::fromRGB(130, 190, 255));

    g.setColour(gridColour);
    for (const auto hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const auto x = frequencyToX(hz, area);
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
    }
    for (const auto db : { -12.0f, -6.0f, 6.0f, 12.0f })
    {
        const auto y = decibelsToY(db, area);
        g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());
    }

    g.setColour(zeroColour);
    g.drawHorizontalLine(juce::roundToInt(decibelsToY(0.0f, area)), area.getX(), area.getRight());

    // One sample per pixel. Asking the processor per pixel is cheap - a biquad
    // magnitude is a handful of trig calls - and it means the curve cannot
    // disagree with the audio.
    juce::Path curve;
    const auto width = juce::jmax(2, juce::roundToInt(area.getWidth()));
    for (int i = 0; i < width; ++i)
    {
        const auto position = static_cast<float>(i) / static_cast<float>(width - 1);
        const auto hz = kCurveMinHz * std::pow(kCurveMaxHz / kCurveMinHz, position);
        const auto db = processor.getBusEqMagnitudeDb(busIndex, hz);
        const auto x = area.getX() + position * area.getWidth();
        const auto y = decibelsToY(db, area);

        if (i == 0)
        {
            curve.startNewSubPath(x, y);
        }
        else
        {
            curve.lineTo(x, y);
        }
    }

    const auto& params = processor.getBusInsertParams(busIndex);
    const auto enabled = params.eqEnabled != nullptr && params.eqEnabled->get();

    g.setColour(curveColour.withAlpha(enabled ? 1.0f : 0.35f));
    g.strokePath(curve, juce::PathStrokeType(2.0f));
}

void BusEqOverlay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto corner = configFloat(uiConfig, "busInserts.eq.cornerRadius", 12.0f);

    g.setColour(configColour(uiConfig, "busInserts.eq.backgroundColor",
                             juce::Colour::fromRGBA(20, 23, 29, 250)));
    g.fillRoundedRectangle(bounds, corner);

    g.setColour(configColour(uiConfig, "busInserts.eq.borderColor",
                             juce::Colour::fromRGBA(130, 190, 255, 120)));
    g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.5f);

    g.setColour(configColour(uiConfig, "busInserts.eq.titleColor", juce::Colours::white));
    g.setFont(juce::Font(juce::FontOptions(configFloat(uiConfig, "busInserts.eq.titleSize", 16.0f),
                                           juce::Font::bold)));
    g.drawText(sheetTitle(),
               getLocalBounds().reduced(configInt(uiConfig, "busInserts.eq.padding", 22), 0)
                   .withHeight(configInt(uiConfig, "busInserts.eq.headerHeight", 34))
                   .translated(0, configInt(uiConfig, "busInserts.eq.padding", 22)),
               juce::Justification::centredLeft);

    const auto curve = curveArea.toFloat();
    g.setColour(configColour(uiConfig, "busInserts.eq.curveBackgroundColor",
                             juce::Colour::fromRGBA(10, 12, 16, 220)));
    g.fillRoundedRectangle(curve, 6.0f);
    paintCurve(g, curve.reduced(6.0f));
    g.setColour(configColour(uiConfig, "busInserts.eq.curveBorderColor",
                             juce::Colour::fromRGBA(255, 255, 255, 40)));
    g.drawRoundedRectangle(curve.reduced(0.5f), 6.0f, 1.0f);
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
    auto area = getLocalBounds().reduced(configInt(uiConfig, "busInserts.comp.padding", 22));

    auto header = area.removeFromTop(configInt(uiConfig, "busInserts.comp.headerHeight", 34));
    closeButton.setBounds(header.removeFromRight(configInt(uiConfig, "busInserts.comp.buttonWidth", 64)).reduced(0, 4));
    header.removeFromRight(8);
    enableButton.setBounds(header.removeFromRight(configInt(uiConfig, "busInserts.comp.enableWidth", 44)).reduced(0, 4));

    area.removeFromTop(configInt(uiConfig, "busInserts.comp.headerGap", 12));

    // The face is laid out as the hardware is: the meter on the right, the four
    // large controls to its left, the ratio buttons in a row beneath.
    const auto meterWidth = configInt(uiConfig, "busInserts.comp.meterWidth", 210);
    meterArea = area.removeFromRight(meterWidth).removeFromTop(configInt(uiConfig, "busInserts.comp.meterHeight", 130));
    area.removeFromRight(configInt(uiConfig, "busInserts.comp.meterGap", 18));

    const auto knobSize = configInt(uiConfig, "busInserts.comp.knobSize", 62);
    const auto captionHeight = 14;

    auto knobRow = area.removeFromTop(knobSize + captionHeight + 6);
    const auto knobGap = configInt(uiConfig, "busInserts.comp.knobGap", 12);
    const auto slots = 5;
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
        slot.removeFromTop(4);
        entry.second->setBounds(slot.removeFromTop(captionHeight));
    }

    // The mix readout sits under its own caption. It is the one control here
    // whose value is not obvious from the knob position, because 100% is the
    // compressor alone and people reach for it expecting a blend.
    mixValue.setBounds(mixCaption.getBounds().translated(0, captionHeight));

    area.removeFromTop(configInt(uiConfig, "busInserts.comp.ratioGap", 20));

    auto ratioRow = area.removeFromTop(configInt(uiConfig, "busInserts.comp.ratioHeight", 30));
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
    const auto bounds = getLocalBounds().toFloat();
    const auto corner = configFloat(uiConfig, "busInserts.comp.cornerRadius", 12.0f);

    // The silver face. A vertical gradient with a brushed grain over it: the
    // grain is what stops a flat fill reading as plastic, and it is derived
    // from pixel position rather than from a random source so it does not
    // shimmer between frames.
    const auto top = configColour(uiConfig, "busInserts.comp.panelTopColor",
                                  juce::Colour::fromRGB(206, 208, 210));
    const auto bottom = configColour(uiConfig, "busInserts.comp.panelBottomColor",
                                     juce::Colour::fromRGB(166, 169, 173));

    g.setGradientFill(juce::ColourGradient(top, bounds.getCentreX(), bounds.getY(),
                                           bottom, bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, corner);

    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, corner);
        g.reduceClipRegion(clip);
        paintSurfaceNoise(g, bounds, configFloat(uiConfig, "busInserts.comp.grainAmount", 0.06f));
    }

    g.setColour(configColour(uiConfig, "busInserts.comp.borderColor",
                             juce::Colour::fromRGBA(60, 62, 66, 200)));
    g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.5f);

    const auto padding = configInt(uiConfig, "busInserts.comp.padding", 22);
    g.setColour(configColour(uiConfig, "busInserts.comp.titleColor", juce::Colour::fromRGB(38, 40, 44)));
    g.setFont(juce::Font(juce::FontOptions(configFloat(uiConfig, "busInserts.comp.titleSize", 16.0f),
                                           juce::Font::bold)));
    g.drawText(sheetTitle(),
               getLocalBounds().reduced(padding, 0)
                   .withHeight(configInt(uiConfig, "busInserts.comp.headerHeight", 34))
                   .translated(0, padding),
               juce::Justification::centredLeft);

    paintMeter(g, meterArea.toFloat());
}

} // namespace px3::ui
