#pragma once

#include <JuceHeader.h>

#include <array>
#include <memory>
#include <vector>

class PX3SynthAudioProcessor;
class UIConfig;

namespace px3::ui
{

// The EQ's response graph, with the bands as draggable handles.
//
// Built on the same interaction model as the ADSR graph: pick the nearest
// handle within a radius, wrap the edit in a host gesture, and reset to the
// parameter's own default on a double click. What is different here is that a
// band has three values and a pointer has two, so the third - Q - is on the
// wheel, over the handle it belongs to.
//
// Behind the curve is a live analyser of the bus, read from a lock-free tap.
// See px3::BusAnalyser for why the tap is shaped the way it is.
class BusEqGraph final : public juce::Component,
                         private juce::Timer
{
public:
    explicit BusEqGraph(PX3SynthAudioProcessor& processorIn);
    ~BusEqGraph() override;

    void setBus(int bus);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void setAccentColour(juce::Colour colour);

    // Switches the audio-side tap on and off with the overlay's visibility.
    void setAnalyserRunning(bool shouldRun);

    // Whether the bands can be dragged. A graph that answers the mouse while
    // the EQ is bypassed is offering an edit that changes nothing audible.
    void setEditable(bool shouldBeEditable);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    // The plot box, inside the axis labels. Public so the overlay can align
    // the band columns underneath it.
    juce::Rectangle<float> plotBounds() const;

    // Throws the cached grid away, so the next paint draws it again. Only the
    // measurement needs this - it is how PX3Diag eqspectrum prices a frame that
    // has to redraw the grid against one that does not.
    void debugInvalidateGridCache() { gridCache = {}; }
    bool debugGridCacheIsValid() const { return gridCache.isValid(); }
    int debugGridCacheWidth() const { return gridCache.getWidth(); }

    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    // Matches the parameter range, so a band at its limit sits on the top or
    // bottom gridline rather than somewhere arbitrary inside the box.
    static constexpr float kRangeDb = 18.0f;

private:
    static constexpr int kBandCount = 4;
    static constexpr int kFftOrder = 12;                 // 4096, the tap's window
    static constexpr int kFftSize = 1 << kFftOrder;
    // Resampled onto the log axis. Wide enough that the trace has more points
    // than the plot has pixels, so the curve is limited by the display rather
    // than by the resampling - at 256 the steps were visible.
    static constexpr int kSpectrumBins = 1024;

    // The display's refresh, and the decay applied per frame at that rate.
    // 0.22 per frame at 24 Hz is a fall time of about 165 ms; the constant
    // below reproduces it at whatever rate is set, so the two can be changed
    // independently.
    static constexpr int kRefreshHz = 60;
    // 0.22 per frame at 24 Hz is a fall time constant of 168 ms. The same fall
    // at 60 Hz is 0.0946 per frame, so raising the refresh made the trace
    // smoother without making it decay faster.
    static constexpr float kDecayPerFrame = 0.0946f;

    void timerCallback() override;
    void refreshSpectrum();

    float frequencyToX(float hz) const;
    float xToFrequency(float x) const;
    float decibelsToY(float db) const;
    float yToDecibels(float y) const;

    juce::Point<float> handlePosition(int band) const;
    int pickHandle(juce::Point<float> position) const;
    // A pass filter has no gain, so its handle only moves horizontally.
    bool bandHasGain(int band) const;

    void paintGrid(juce::Graphics& g, juce::Rectangle<float> plot) const;
    // Draws paintGrid once into gridCache, at the scale of the context it
    // will be drawn back into.
    void rebuildGridCache(float scale);
    void paintSpectrum(juce::Graphics& g, juce::Rectangle<float> plot) const;
    void paintCurve(juce::Graphics& g, juce::Rectangle<float> plot) const;
    void paintHandles(juce::Graphics& g) const;
    void paintReadout(juce::Graphics& g, int band) const;

    PX3SynthAudioProcessor& processor;
    std::shared_ptr<const UIConfig> uiConfig;
    juce::Colour accent { juce::Colour::fromRGB(130, 190, 255) };
    int busIndex { 0 };

    int hoverBand { -1 };
    int dragBand { -1 };
    bool analyserRunning { false };
    // Starts false: the EQ defaults to bypassed, and a graph that begins
    // editable would accept a drag before the first poll ran.
    bool editable { false };

    // The gridlines, their labels and the zero line, drawn once. None of it
    // changes between frames - only on a resize or a config reload - and the
    // trace in front of it is repainted 60 times a second. The VU meter's face
    // is cached for the same reason.
    juce::Image gridCache;
    float gridCacheScale { 0.0f };

    juce::dsp::FFT fft { kFftOrder };
    std::vector<float> fftScratch;
    std::vector<float> window;
    // Smoothed magnitudes on the LOG axis, one per horizontal bin, in dB.
    std::array<float, kSpectrumBins> spectrumDb {};
    bool spectrumPrimed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BusEqGraph)
};

} // namespace px3::ui
