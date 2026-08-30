#pragma once

#include <JuceHeader.h>

#include <memory>

class UIConfig;

// A fixed speckle of light and dark dots over a shape, giving painted metal and
// moulded plastic a bit of grain instead of a flat fill.
//
// The pattern is derived from the pixel coordinates, not from a random source,
// so it is identical on every repaint. A per-frame random would shimmer at the
// UI's 30 Hz refresh, which reads as a rendering fault rather than as texture.
void paintSurfaceNoise(juce::Graphics& g, juce::Rectangle<float> area, float amount);

struct FaderStyle
{
    float trackWidth { 4.0f };
    float thumbWidth { 18.0f };
    float thumbHeight { 8.0f };
    float cornerRadius { 2.0f };
    float trackPadding { 6.0f };
    juce::Colour trackColour { juce::Colour::fromRGBA(130, 190, 255, 180) };
    juce::Colour trackBackgroundColour { juce::Colour::fromRGBA(26, 28, 32, 190) };
    juce::Colour thumbColour { juce::Colour::fromRGB(230, 236, 246) };
    juce::Colour disabledColour { juce::Colour::fromRGBA(120, 120, 120, 120) };
    juce::Colour hoverColour { juce::Colour::fromRGBA(240, 246, 255, 220) };
    // Tints the filled part of the track and the cap's indicator line. Each
    // channel passes its own card identity here, so a fader matches the source
    // it controls.
    juce::Colour accentColour { juce::Colour::fromRGB(130, 190, 255) };
    // Scale ticks either side of the track, as a console fader has. 0 hides
    // them.
    int tickCount { 9 };

    static FaderStyle fromUIConfig(const std::shared_ptr<const UIConfig>& uiConfig, const juce::String& pathPrefix);
};

class FaderStyleLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void setStyle(const FaderStyle& styleIn);

    void drawLinearSlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          const juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;

private:
    FaderStyle style;
};

// A pan knob with detents at centre and at both extremes.
//
// Centre, hard left and hard right are the three positions people actually aim
// for and the hardest to land exactly with a rotary drag. Centre gets the wider
// catch because it is the one used most. The snap applies only while dragging:
// a typed or automated value is left exactly as given.
class PanKnob final : public juce::Slider
{
public:
    PanKnob();

    void setCentreDetent(double range);
    void setExtremeDetent(double range);

private:
    double snapValue(double attemptedValue, DragMode dragMode) override;

    double snapRange { 0.14 };
    double extremeSnapRange { 0.06 };
};

class FaderSlider final : public juce::Slider
{
public:
    FaderSlider();
    ~FaderSlider() override;

    void applyStyle(const FaderStyle& style);

private:
    FaderStyleLookAndFeel faderLookAndFeel;
};

class MixerToggleButton : public juce::TextButton
{
public:
    struct Style
    {
        int width { 46 };
        int height { 18 };
        float cornerRadius { 4.0f };
        float textSize { 10.0f };
        juce::Colour textColour { juce::Colour::fromRGB(232, 232, 232) };
        juce::Colour normalColour { juce::Colour::fromRGBA(44, 46, 52, 210) };
        juce::Colour hoverColour { juce::Colour::fromRGBA(66, 70, 79, 230) };
        juce::Colour activeColour { juce::Colour::fromRGBA(76, 136, 202, 230) };
        juce::Colour pressedColour { juce::Colour::fromRGBA(96, 164, 238, 230) };
        juce::Colour disabledColour { juce::Colour::fromRGBA(30, 30, 30, 130) };
        juce::Colour borderColour { juce::Colour::fromRGBA(220, 224, 236, 120) };

        // Where the legend sits relative to the cap. Beneath it on a console
        // strip, where a column of buttons reads downward; to one side when the
        // button is alone in a row and the vertical space is not there.
        enum class LegendPlacement { below, left, right };
        LegendPlacement legendPlacement { LegendPlacement::below };
    };

    explicit MixerToggleButton(const juce::String& text);

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void applyStyle(const Style& styleIn);

protected:
    // What lights the lamp. For MUTE, SOLO and PHASE that is the toggle state;
    // a button that opens something rather than latching overrides it.
    virtual bool isLit() const { return getToggleState(); }

    Style style;
};

namespace px3::ui
{
// Parses a MixerToggleButton::Style out of a config block.
//
// Free, and taking its paths as arguments, because three different places want
// it now: the mixer strip's own buttons, the insert buttons in the strip
// corners, and the ON and CLOSE buttons on the two insert sheets.
//
// `sharedBase` is read first and `overrideBase` layered on top key by key, so a
// sheet only declares what it changes. An empty overrideBase reads the shared
// block alone.
MixerToggleButton::Style mixerToggleStyleFromConfig(const UIConfig* config,
                                                    const juce::String& sharedBase,
                                                    const juce::String& overrideBase,
                                                    const MixerToggleButton::Style& fallback);
} // namespace px3::ui

class MuteButton final : public MixerToggleButton
{
public:
    MuteButton();
};

class SoloButton final : public MixerToggleButton
{
public:
    SoloButton();
};

// Polarity flip. The legend is the classic slashed-O used on consoles for phase
// inversion - it is a symbol rather than a word because it has to fit in the
// same button as MUTE and SOLO.
class PhaseButton final : public MixerToggleButton
{
public:
    PhaseButton();
};

// Opens a bus insert's editor. Square rather than the wide rectangle MUTE and
// SOLO use, because it is not a state toggle sitting in a row of state toggles -
// it opens something. It keeps the same paint, the same border and the same
// active colour so it still reads as part of the strip, and it lights while its
// insert is switched on so the strip says at a glance what is running.
class InsertButton final : public MixerToggleButton
{
public:
    explicit InsertButton(const juce::String& legend);

    // The insert's enable state, which is what lights the button - as distinct
    // from the overlay being open.
    void setInsertActive(bool active);

protected:
    bool isLit() const override { return insertActive; }

private:
    bool insertActive { false };
};

class MixerLevelMeter final : public juce::Component
{
public:
    struct Style
    {
        float cornerRadius { 2.0f };
        juce::Colour backgroundColour { juce::Colour::fromRGBA(18, 20, 24, 180) };
        juce::Colour fillColour { juce::Colour::fromRGBA(120, 210, 140, 220) };
        juce::Colour highColour { juce::Colour::fromRGBA(230, 196, 90, 230) };
        juce::Colour clipColour { juce::Colour::fromRGBA(230, 90, 90, 230) };
        juce::Colour borderColour { juce::Colour::fromRGBA(220, 224, 236, 90) };
        // Number of LED segments in the ladder. A real console meter is a strip
        // of discrete lamps, not a continuous bar, and the segment count is what
        // sets how coarse it reads.
        int segmentCount { 22 };
        // How long a peak marker hangs before it starts falling back, in
        // 30 Hz frames, and how fast it falls once it does.
        int peakHoldFrames { 30 };
        float peakFallPerFrame { 0.012f };
        // Frames the clip lamp stays lit after the last over.
        int clipHoldFrames { 45 };
        // Meter ballistics, per 30 Hz frame. Rising fast and falling slowly is
        // what every hardware meter does: it makes level readable instead of
        // flickering, because the eye cannot track a bar that follows RMS
        // sample for sample.
        float riseCoefficient { 0.55f };
        float fallCoefficient { 0.14f };
    };

    void setLevel(float linearLevel);
    // What the bar is currently showing. Exposed so a test can assert the meter
    // reaches empty rather than merely looking empty.
    float displayLevelForTest() const noexcept { return level; }
    void applyStyle(const Style& styleIn);
    void paint(juce::Graphics& g) override;

private:
    // What the bar shows: the incoming level put through the ballistics above.
    // The peak marker deliberately tracks the RAW value instead, so a transient
    // still registers even though the bar is too slow to reach it.
    float level { 0.0f };
    // Peak hold: the highest recent level, which hangs and then falls. Reading a
    // transient off a bar that follows the signal exactly is impossible - the
    // peak is gone before the eye catches it.
    float peakLevel { 0.0f };
    int peakHoldCounter { 0 };
    int clipHoldCounter { 0 };
    // What the last repaint actually drew. The ballistics converge and then sit
    // still, so without this the meter would repaint 30 times a second per
    // channel to draw an identical picture.
    float paintedLevel { -1.0f };
    float paintedPeak { -1.0f };
    int paintedClip { -1 };
    Style style;
};
