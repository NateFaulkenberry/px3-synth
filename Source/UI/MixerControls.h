#pragma once

#include <JuceHeader.h>

#include <memory>

class UIConfig;

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
    };

    explicit MixerToggleButton(const juce::String& text);

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void applyStyle(const Style& styleIn);

protected:
    Style style;
};

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
    };

    void setLevel(float linearLevel);
    void applyStyle(const Style& styleIn);
    void paint(juce::Graphics& g) override;

private:
    float level { 0.0f };
    Style style;
};
