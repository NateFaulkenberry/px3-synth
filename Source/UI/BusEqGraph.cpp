#include "BusEqGraph.h"

#include "../DSP/PluginProcessor.h"
#include "UIConfig.h"

namespace px3::ui
{

namespace
{
constexpr float kAxisLabelHeight = 15.0f;
constexpr float kAxisLabelWidth = 30.0f;
constexpr float kHandleRadius = 7.0f;
constexpr float kPickRadius = 15.0f;

// The gridlines that get a label. Every decade plus the 2 and 5 within it,
// which is the ruling every EQ uses because it is what the ear divides by.
struct GridLine { float hz; const char* label; };
const std::array<GridLine, 10> kFrequencyGrid { {
    { 20.0f, "20" }, { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" },
    { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" },
    { 10000.0f, "10k" }, { 20000.0f, "20k" },
} };

// Unlabelled ticks between the labelled ones, so the log spacing is readable
// without crowding the axis with numbers.
const std::array<float, 16> kMinorFrequencyGrid { {
    30.0f, 40.0f, 60.0f, 70.0f, 80.0f, 90.0f, 300.0f, 400.0f,
    600.0f, 700.0f, 800.0f, 900.0f, 3000.0f, 4000.0f, 6000.0f, 15000.0f,
} };

const std::array<float, 5> kDecibelGrid { { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f } };

juce::Colour configColour(const std::shared_ptr<const UIConfig>& config,
                          const juce::String& path,
                          juce::Colour fallback)
{
    return config != nullptr ? config->getColour(path, fallback) : fallback;
}
} // namespace

BusEqGraph::BusEqGraph(PX3SynthAudioProcessor& processorIn)
    : processor(processorIn)
{
    fftScratch.assign(static_cast<std::size_t>(kFftSize) * 2, 0.0f);

    window.resize(static_cast<std::size_t>(kFftSize));
    for (int i = 0; i < kFftSize; ++i)
    {
        // Hann. The trace is being read for shape, not for absolute level, and
        // Hann's narrow main lobe is what keeps a bass note from smearing
        // across a third of the display.
        window[static_cast<std::size_t>(i)] =
            0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                   * static_cast<float>(i) / static_cast<float>(kFftSize - 1));
    }

    spectrumDb.fill(-120.0f);
    setName("BusEqGraph");
    setInterceptsMouseClicks(true, false);
}

BusEqGraph::~BusEqGraph()
{
    stopTimer();
    // Never leave the audio thread writing for a component that is gone.
    processor.getBusAnalyser(busIndex).setActive(false);
}

void BusEqGraph::setBus(int bus)
{
    const auto clamped = juce::jlimit(0, PX3SynthAudioProcessor::kBusInsertCount - 1, bus);
    if (clamped == busIndex)
    {
        return;
    }

    // The old bus stops being watched the moment the graph looks elsewhere.
    processor.getBusAnalyser(busIndex).setActive(false);
    busIndex = clamped;
    processor.getBusAnalyser(busIndex).setActive(analyserRunning);

    spectrumDb.fill(-120.0f);
    spectrumPrimed = false;
    repaint();
}

void BusEqGraph::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    repaint();
}

void BusEqGraph::setAccentColour(juce::Colour colour)
{
    accent = colour;
    repaint();
}

void BusEqGraph::setEditable(bool shouldBeEditable)
{
    if (editable == shouldBeEditable)
    {
        return;
    }

    editable = shouldBeEditable;

    // Any drag in progress is abandoned rather than left half-applied, and the
    // hover state cleared so a handle does not stay lit under a dead pointer.
    dragBand = -1;
    hoverBand = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void BusEqGraph::setAnalyserRunning(bool shouldRun)
{
    if (analyserRunning == shouldRun)
    {
        return;
    }

    analyserRunning = shouldRun;
    processor.getBusAnalyser(busIndex).setActive(shouldRun);

    if (shouldRun)
    {
        spectrumDb.fill(-120.0f);
        spectrumPrimed = false;
        // 60, not 24. At 24 the trace visibly stepped between frames - the
        // decay below is what makes it look continuous, and a decay is only as
        // smooth as the rate it is applied at.
        startTimerHz(kRefreshHz);
    }
    else
    {
        stopTimer();
    }
}

//==============================================================================
// geometry
//==============================================================================
juce::Rectangle<float> BusEqGraph::plotBounds() const
{
    return getLocalBounds().toFloat()
        .withTrimmedBottom(kAxisLabelHeight)
        .withTrimmedLeft(kAxisLabelWidth)
        .withTrimmedRight(6.0f)
        .withTrimmedTop(6.0f);
}

float BusEqGraph::frequencyToX(float hz) const
{
    const auto plot = plotBounds();
    const auto position = std::log(juce::jlimit(kMinHz, kMaxHz, hz) / kMinHz) / std::log(kMaxHz / kMinHz);
    return plot.getX() + position * plot.getWidth();
}

float BusEqGraph::xToFrequency(float x) const
{
    const auto plot = plotBounds();
    const auto position = juce::jlimit(0.0f, 1.0f, (x - plot.getX()) / juce::jmax(1.0f, plot.getWidth()));
    return kMinHz * std::pow(kMaxHz / kMinHz, position);
}

float BusEqGraph::decibelsToY(float db) const
{
    const auto plot = plotBounds();
    const auto position = 0.5f - juce::jlimit(-1.0f, 1.0f, db / kRangeDb) * 0.5f;
    return plot.getY() + position * plot.getHeight();
}

float BusEqGraph::yToDecibels(float y) const
{
    const auto plot = plotBounds();
    const auto position = juce::jlimit(0.0f, 1.0f, (y - plot.getY()) / juce::jmax(1.0f, plot.getHeight()));
    return (0.5f - position) * 2.0f * kRangeDb;
}

bool BusEqGraph::bandHasGain(int band) const
{
    const auto& params = processor.getBusInsertParams(busIndex);
    const auto b = static_cast<std::size_t>(band);
    // Index 1 on an outer band is the pass filter, which has no gain at all.
    return params.bandType[b] == nullptr || params.bandType[b]->getIndex() == 0;
}

juce::Point<float> BusEqGraph::handlePosition(int band) const
{
    const auto& params = processor.getBusInsertParams(busIndex);
    const auto b = static_cast<std::size_t>(band);
    if (params.bandFreq[b] == nullptr)
    {
        return {};
    }

    const auto hz = params.bandFreq[b]->get();
    const auto db = bandHasGain(band) && params.bandGain[b] != nullptr ? params.bandGain[b]->get() : 0.0f;
    return { frequencyToX(hz), decibelsToY(db) };
}

int BusEqGraph::pickHandle(juce::Point<float> position) const
{
    auto best = -1;
    auto bestDistance = kPickRadius * kPickRadius;

    for (int band = 0; band < kBandCount; ++band)
    {
        const auto delta = handlePosition(band) - position;
        const auto distance = delta.getX() * delta.getX() + delta.getY() * delta.getY();
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = band;
        }
    }

    return best;
}

//==============================================================================
// the analyser
//==============================================================================
void BusEqGraph::timerCallback()
{
    refreshSpectrum();
    repaint();
}

void BusEqGraph::refreshSpectrum()
{
    auto& analyser = processor.getBusAnalyser(busIndex);
    if (! analyser.isActive())
    {
        return;
    }

    std::fill(fftScratch.begin(), fftScratch.end(), 0.0f);
    analyser.readWindow(fftScratch.data());
    for (int i = 0; i < kFftSize; ++i)
    {
        fftScratch[static_cast<std::size_t>(i)] *= window[static_cast<std::size_t>(i)];
    }

    fft.performFrequencyOnlyForwardTransform(fftScratch.data());

    const auto sampleRate = juce::jmax(8000.0, processor.getSampleRate());
    const auto binsPerHz = static_cast<double>(kFftSize) / sampleRate;

    // Resampled onto the LOG axis rather than drawn linearly. A linear bin walk
    // puts nine tenths of its points in the top octave, where the display has
    // the least room for them, and leaves the bass drawn from three bins.
    //
    // How a display point is filled depends on how many FFT bins it covers, and
    // getting this wrong was the visible stair-stepping:
    //
    //   spans MANY bins  -> take the peak, so a narrow resonance inside the
    //                       point is not averaged away
    //   spans ONE or FEWER -> INTERPOLATE between the neighbouring bins
    //
    // The old code took the peak over a fixed +-6% band in both cases. At 33 Hz
    // that band is a third of one bin wide, so 42 consecutive display points
    // resolved to the same bin and returned the same number - a 36 px dead-flat
    // step in the curve. The data underneath varied; the aggregation discarded
    // it.
    const auto lastUsableBin = kFftSize / 2 - 2;

    for (int i = 0; i < kSpectrumBins; ++i)
    {
        const auto position = static_cast<float>(i) / static_cast<float>(kSpectrumBins - 1);
        const auto hz = kMinHz * std::pow(kMaxHz / kMinHz, position);

        // The band this display point covers, taken from its neighbours on the
        // log axis rather than a fixed percentage - so it tracks the axis
        // instead of being right at one end and wrong at the other.
        const auto step = std::pow(kMaxHz / kMinHz, 1.0f / static_cast<float>(kSpectrumBins - 1));
        const auto lowHz = hz / step;
        const auto highHz = hz * step;

        const auto exactBin = static_cast<float>(hz * binsPerHz);
        const auto firstBin = static_cast<int>(std::floor(lowHz * binsPerHz));
        const auto lastBin = static_cast<int>(std::ceil(highHz * binsPerHz));

        float magnitude = 0.0f;

        if (lastBin - firstBin >= 2)
        {
            // Several bins under one point: the peak, so a narrow resonance
            // survives.
            for (int bin = juce::jmax(1, firstBin); bin <= juce::jmin(lastUsableBin, lastBin); ++bin)
            {
                magnitude = juce::jmax(magnitude, fftScratch[static_cast<std::size_t>(bin)]);
            }
        }
        else
        {
            // Between two bins: interpolate. This is what removes the plateaus.
            const auto lower = juce::jlimit(1, lastUsableBin, static_cast<int>(std::floor(exactBin)));
            const auto upper = juce::jlimit(1, lastUsableBin, lower + 1);
            const auto t = juce::jlimit(0.0f, 1.0f, exactBin - static_cast<float>(lower));

            const auto a = fftScratch[static_cast<std::size_t>(lower)];
            const auto b = fftScratch[static_cast<std::size_t>(upper)];
            magnitude = a + (b - a) * t;
        }

        const auto db = juce::Decibels::gainToDecibels(magnitude * 2.0f / static_cast<float>(kFftSize), -120.0f);

        auto& smoothed = spectrumDb[static_cast<std::size_t>(i)];
        if (! spectrumPrimed)
        {
            smoothed = db;
        }
        else
        {
            // Fast up, slow down: a peak-hold-ish decay, so a transient is
            // visible for long enough to read instead of flickering past.
            //
            // The coefficient is derived from the refresh rate rather than
            // written as a constant, so changing the rate changes only the
            // smoothness and not the fall time.
            smoothed = db > smoothed ? db : smoothed + (db - smoothed) * kDecayPerFrame;
        }
    }

    spectrumPrimed = true;
}

//==============================================================================
// painting
//==============================================================================
void BusEqGraph::paintGrid(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    const auto gridColour = configColour(uiConfig, "busInserts.eq.gridColor",
                                         juce::Colour::fromRGBA(255, 255, 255, 34));
    const auto minorColour = gridColour.withMultipliedAlpha(0.45f);
    const auto labelColour = configColour(uiConfig, "busInserts.eq.axisLabelColor",
                                          juce::Colour::fromRGBA(255, 255, 255, 150));
    const auto zeroColour = configColour(uiConfig, "busInserts.eq.zeroLineColor",
                                         juce::Colour::fromRGBA(255, 255, 255, 96));

    g.setColour(minorColour);
    for (const auto hz : kMinorFrequencyGrid)
    {
        const auto x = frequencyToX(hz);
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
    }

    g.setFont(juce::FontOptions(9.5f));
    for (const auto& line : kFrequencyGrid)
    {
        const auto x = frequencyToX(line.hz);
        g.setColour(gridColour);
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());

        // The first and last labels are pulled inboard so they sit under the
        // plot instead of half outside the component.
        auto labelBox = juce::Rectangle<float>(36.0f, kAxisLabelHeight)
                            .withCentre({ x, plot.getBottom() + kAxisLabelHeight * 0.5f });
        labelBox.setX(juce::jlimit(0.0f,
                                   static_cast<float>(getWidth()) - labelBox.getWidth(),
                                   labelBox.getX()));

        g.setColour(labelColour);
        g.drawText(line.label, labelBox, juce::Justification::centred);
    }

    for (const auto db : kDecibelGrid)
    {
        const auto y = decibelsToY(db);
        g.setColour(db == 0.0f ? zeroColour : gridColour);
        g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());

        g.setColour(labelColour);
        g.drawText(juce::String(db > 0.0f ? "+" : "") + juce::String(juce::roundToInt(db)),
                   juce::Rectangle<float>(0.0f, y - 7.0f, kAxisLabelWidth - 4.0f, 14.0f),
                   juce::Justification::centredRight);
    }
}

void BusEqGraph::paintSpectrum(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    if (! spectrumPrimed)
    {
        return;
    }

    const auto fillColour = configColour(uiConfig, "busInserts.eq.spectrumColor",
                                         juce::Colour::fromRGBA(150, 200, 255, 58));

    // The trace has its own dB scale: the curve's +-18 dB is a RELATIVE range
    // and the signal's is absolute, so sharing one axis would be meaningless.
    // -84..0 dBFS maps to the plot height.
    constexpr auto kFloorDb = -84.0f;
    constexpr auto kCeilingDb = 0.0f;

    juce::Path trace;
    trace.startNewSubPath(plot.getX(), plot.getBottom());

    for (int i = 0; i < kSpectrumBins; ++i)
    {
        const auto position = static_cast<float>(i) / static_cast<float>(kSpectrumBins - 1);
        const auto x = plot.getX() + position * plot.getWidth();
        const auto norm = juce::jlimit(0.0f, 1.0f,
                                       (spectrumDb[static_cast<std::size_t>(i)] - kFloorDb)
                                           / (kCeilingDb - kFloorDb));
        trace.lineTo(x, plot.getBottom() - norm * plot.getHeight());
    }

    trace.lineTo(plot.getRight(), plot.getBottom());
    trace.closeSubPath();

    g.setColour(fillColour);
    g.fillPath(trace);
    g.setColour(fillColour.withMultipliedAlpha(2.2f));
    g.strokePath(trace, juce::PathStrokeType(1.0f));
}

void BusEqGraph::paintCurve(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    const auto& params = processor.getBusInsertParams(busIndex);
    const auto enabled = params.eqEnabled != nullptr && params.eqEnabled->get();

    juce::Path curve;
    const auto width = juce::jmax(2, juce::roundToInt(plot.getWidth()));
    for (int i = 0; i < width; ++i)
    {
        const auto position = static_cast<float>(i) / static_cast<float>(width - 1);
        const auto hz = kMinHz * std::pow(kMaxHz / kMinHz, position);
        // Asked of the running processor, so what is drawn is what is heard -
        // the smoothing included.
        const auto db = processor.getBusEqMagnitudeDb(busIndex, hz);
        const auto point = juce::Point<float>(plot.getX() + position * plot.getWidth(), decibelsToY(db));

        if (i == 0)
        {
            curve.startNewSubPath(point);
        }
        else
        {
            curve.lineTo(point);
        }
    }

    // A soft fill between the curve and the 0 dB line, so a boost and a cut are
    // distinguishable at a glance rather than by tracing the line.
    auto filled = curve;
    filled.lineTo(plot.getRight(), decibelsToY(0.0f));
    filled.lineTo(plot.getX(), decibelsToY(0.0f));
    filled.closeSubPath();

    g.setColour(accent.withAlpha(enabled ? 0.16f : 0.06f));
    g.fillPath(filled);

    g.setColour(accent.withAlpha(enabled ? 1.0f : 0.35f));
    g.strokePath(curve, juce::PathStrokeType(2.0f));
}

void BusEqGraph::paintHandles(juce::Graphics& g) const
{
    const auto& params = processor.getBusInsertParams(busIndex);
    const auto enabled = params.eqEnabled != nullptr && params.eqEnabled->get();

    for (int band = 0; band < kBandCount; ++band)
    {
        const auto centre = handlePosition(band);
        const auto active = band == dragBand || band == hoverBand;
        const auto radius = kHandleRadius * (active ? 1.25f : 1.0f);

        g.setColour(juce::Colour::fromRGBA(10, 12, 16, 210));
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));

        g.setColour(accent.withAlpha(enabled ? (active ? 1.0f : 0.8f) : 0.3f));
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), 2.0f);

        g.setColour(juce::Colour::fromRGB(236, 242, 250).withAlpha(enabled ? 1.0f : 0.4f));
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(juce::String(band + 1),
                   juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre).toNearestInt(),
                   juce::Justification::centred);
    }
}

void BusEqGraph::paintReadout(juce::Graphics& g, int band) const
{
    const auto& params = processor.getBusInsertParams(busIndex);
    const auto b = static_cast<std::size_t>(band);
    if (params.bandFreq[b] == nullptr)
    {
        return;
    }

    const auto hz = params.bandFreq[b]->get();
    juce::String text = hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) + " kHz"
                                      : juce::String(juce::roundToInt(hz)) + " Hz";
    if (bandHasGain(band) && params.bandGain[b] != nullptr)
    {
        const auto db = params.bandGain[b]->get();
        text << "   " << (db >= 0.0f ? "+" : "") << juce::String(db, 1) << " dB";
    }
    if (params.bandQ[b] != nullptr)
    {
        text << "   Q " << juce::String(params.bandQ[b]->get(), 2);
    }

    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    const auto textWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text);

    auto bubble = juce::Rectangle<float>(textWidth + 20.0f, 24.0f);
    bubble.setCentre(handlePosition(band).translated(0.0f, -26.0f));
    const auto plot = plotBounds();
    bubble = bubble.withPosition(juce::jlimit(plot.getX(), plot.getRight() - bubble.getWidth(), bubble.getX()),
                                 juce::jlimit(plot.getY(), plot.getBottom() - bubble.getHeight(), bubble.getY()));

    g.setColour(juce::Colour::fromRGBA(9, 12, 17, 236));
    g.fillRoundedRectangle(bubble, 5.0f);
    g.setColour(accent.withAlpha(0.8f));
    g.drawRoundedRectangle(bubble, 5.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(236, 242, 250));
    g.drawText(text, bubble, juce::Justification::centred);
}

void BusEqGraph::paint(juce::Graphics& g)
{
    const auto plot = plotBounds();

    paintGrid(g, plot);
    paintSpectrum(g, plot);
    paintCurve(g, plot);
    paintHandles(g);

    const auto readoutBand = dragBand >= 0 ? dragBand : hoverBand;
    if (readoutBand >= 0)
    {
        paintReadout(g, readoutBand);
    }
}

//==============================================================================
// interaction
//==============================================================================
void BusEqGraph::mouseMove(const juce::MouseEvent& event)
{
    if (! editable)
    {
        return;
    }

    const auto picked = pickHandle(event.position);
    if (picked != hoverBand)
    {
        hoverBand = picked;
        setMouseCursor(picked >= 0 ? juce::MouseCursor::DraggingHandCursor
                                   : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void BusEqGraph::mouseExit(const juce::MouseEvent&)
{
    if (dragBand < 0 && hoverBand >= 0)
    {
        hoverBand = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void BusEqGraph::mouseDown(const juce::MouseEvent& event)
{
    if (! editable)
    {
        return;
    }

    dragBand = pickHandle(event.position);
    hoverBand = dragBand;

    if (dragBand < 0)
    {
        return;
    }

    const auto& params = processor.getBusInsertParams(busIndex);
    const auto b = static_cast<std::size_t>(dragBand);

    // One gesture around the whole drag, so a host records it as a single edit
    // rather than as a few hundred.
    if (params.bandFreq[b] != nullptr) params.bandFreq[b]->beginChangeGesture();
    if (bandHasGain(dragBand) && params.bandGain[b] != nullptr) params.bandGain[b]->beginChangeGesture();

    repaint();
}

void BusEqGraph::mouseDrag(const juce::MouseEvent& event)
{
    if (dragBand < 0)
    {
        return;
    }

    const auto& params = processor.getBusInsertParams(busIndex);
    const auto b = static_cast<std::size_t>(dragBand);

    if (params.bandFreq[b] != nullptr)
    {
        const auto hz = xToFrequency(event.position.getX());
        params.bandFreq[b]->setValueNotifyingHost(params.bandFreq[b]->convertTo0to1(hz));
    }

    // A pass filter's handle rides the 0 dB line: it has no gain to set, and
    // letting the pointer drag it off the line would show a value that does
    // not exist.
    if (bandHasGain(dragBand) && params.bandGain[b] != nullptr)
    {
        const auto db = yToDecibels(event.position.getY());
        params.bandGain[b]->setValueNotifyingHost(params.bandGain[b]->convertTo0to1(db));
    }

    repaint();
}

void BusEqGraph::mouseUp(const juce::MouseEvent&)
{
    if (dragBand < 0)
    {
        return;
    }

    const auto& params = processor.getBusInsertParams(busIndex);
    const auto b = static_cast<std::size_t>(dragBand);

    if (params.bandFreq[b] != nullptr) params.bandFreq[b]->endChangeGesture();
    if (bandHasGain(dragBand) && params.bandGain[b] != nullptr) params.bandGain[b]->endChangeGesture();

    dragBand = -1;
    repaint();
}

void BusEqGraph::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (! editable)
    {
        return;
    }

    const auto band = pickHandle(event.position);
    if (band < 0)
    {
        return;
    }

    // Back to the band's own defaults - the same gesture the ADSR graph uses,
    // and the reason the defaults are chosen to be a sensible starting point
    // rather than an arbitrary one.
    const auto& params = processor.getBusInsertParams(busIndex);
    const auto b = static_cast<std::size_t>(band);

    for (auto* parameter : { static_cast<juce::RangedAudioParameter*>(params.bandFreq[b]),
                             static_cast<juce::RangedAudioParameter*>(params.bandGain[b]),
                             static_cast<juce::RangedAudioParameter*>(params.bandQ[b]) })
    {
        if (parameter == nullptr)
        {
            continue;
        }
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->getDefaultValue());
        parameter->endChangeGesture();
    }

    repaint();
}

void BusEqGraph::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (! editable)
    {
        return;
    }

    // Q is the third value, and a pointer only has two. It belongs on the
    // wheel over the handle it applies to rather than as a modifier drag, which
    // is undiscoverable and collides with the host's own modifier handling.
    const auto band = dragBand >= 0 ? dragBand : pickHandle(event.position);
    if (band < 0)
    {
        return;
    }

    const auto& params = processor.getBusInsertParams(busIndex);
    const auto b = static_cast<std::size_t>(band);
    if (params.bandQ[b] == nullptr)
    {
        return;
    }

    const auto delta = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
    if (std::abs(delta) < 1.0e-4f)
    {
        return;
    }

    // Moved in NORMALISED space, so a notch is the same perceptual step at both
    // ends of a skewed range. The step is small on purpose: Q runs 0.30 to 8.0,
    // and a coarse notch walks the whole range in three clicks.
    const auto current = params.bandQ[b]->convertTo0to1(params.bandQ[b]->get());
    const auto next = juce::jlimit(0.0f, 1.0f, current + delta * 0.06f);
    params.bandQ[b]->beginChangeGesture();
    params.bandQ[b]->setValueNotifyingHost(next);
    params.bandQ[b]->endChangeGesture();

    hoverBand = band;
    repaint();
}

} // namespace px3::ui
