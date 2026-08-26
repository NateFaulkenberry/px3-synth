#include "MixerControls.h"

#include "UIConfig.h"

FaderStyle FaderStyle::fromUIConfig(const std::shared_ptr<const UIConfig>& uiConfig, const juce::String& pathPrefix)
{
    FaderStyle style;
    if (uiConfig == nullptr)
    {
        return style;
    }

    style.trackWidth = uiConfig->getFloat(pathPrefix + ".trackWidth", style.trackWidth);
    style.thumbWidth = uiConfig->getFloat(pathPrefix + ".thumbWidth", style.thumbWidth);
    style.thumbHeight = uiConfig->getFloat(pathPrefix + ".thumbHeight", style.thumbHeight);
    style.cornerRadius = uiConfig->getFloat(pathPrefix + ".cornerRadius", style.cornerRadius);
    style.trackPadding = uiConfig->getFloat(pathPrefix + ".trackPadding", style.trackPadding);
    style.trackColour = uiConfig->getColour(pathPrefix + ".trackColour", style.trackColour);
    style.trackBackgroundColour = uiConfig->getColour(pathPrefix + ".trackBackgroundColour", style.trackBackgroundColour);
    style.thumbColour = uiConfig->getColour(pathPrefix + ".thumbColour", style.thumbColour);
    style.disabledColour = uiConfig->getColour(pathPrefix + ".disabledColour", style.disabledColour);
    style.hoverColour = uiConfig->getColour(pathPrefix + ".hoverColour", style.hoverColour);
    return style;
}

void FaderStyleLookAndFeel::setStyle(const FaderStyle& styleIn)
{
    style = styleIn;
}

void FaderStyleLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             float sliderPos,
                                             float,
                                             float,
                                             const juce::Slider::SliderStyle,
                                             juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                               static_cast<float>(y),
                                               static_cast<float>(width),
                                               static_cast<float>(height));
    const auto trackArea = bounds.reduced(style.trackPadding, style.trackPadding);
    const auto trackX = trackArea.getCentreX() - style.trackWidth * 0.5f;
    const auto track = juce::Rectangle<float>(trackX,
                                              trackArea.getY(),
                                              style.trackWidth,
                                              trackArea.getHeight());

    g.setColour(slider.isEnabled() ? style.trackBackgroundColour : style.disabledColour);
    g.fillRoundedRectangle(track, style.cornerRadius);

    const auto thumbY = juce::jlimit(trackArea.getY(),
                                     trackArea.getBottom() - style.thumbHeight,
                                     sliderPos - style.thumbHeight * 0.5f);

    const auto filled = juce::Rectangle<float>(track.getX(),
                                               thumbY + style.thumbHeight * 0.5f,
                                               track.getWidth(),
                                               track.getBottom() - (thumbY + style.thumbHeight * 0.5f));
    g.setColour(slider.isEnabled() ? style.trackColour : style.disabledColour.withMultipliedAlpha(0.85f));
    g.fillRoundedRectangle(filled, style.cornerRadius);

    auto thumbColour = slider.isEnabled() ? style.thumbColour : style.disabledColour;
    if (slider.isMouseOverOrDragging())
    {
        thumbColour = style.hoverColour;
    }
    g.setColour(thumbColour);

    const auto thumbRect = juce::Rectangle<float>(trackArea.getCentreX() - style.thumbWidth * 0.5f,
                                                  thumbY,
                                                  style.thumbWidth,
                                                  style.thumbHeight);
    g.fillRoundedRectangle(thumbRect, style.cornerRadius);
}

FaderSlider::FaderSlider()
{
    setSliderStyle(juce::Slider::LinearVertical);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setScrollWheelEnabled(false);
    setLookAndFeel(&faderLookAndFeel);
}

FaderSlider::~FaderSlider()
{
    setLookAndFeel(nullptr);
}

void FaderSlider::applyStyle(const FaderStyle& style)
{
    faderLookAndFeel.setStyle(style);
    repaint();
}

MixerToggleButton::MixerToggleButton(const juce::String& text)
    : juce::TextButton(text)
{
    setClickingTogglesState(true);
}

void MixerToggleButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto fill = style.normalColour;
    if (!isEnabled())
    {
        fill = style.disabledColour;
    }
    else if (getToggleState())
    {
        fill = style.activeColour;
    }

    if (shouldDrawButtonAsDown)
    {
        fill = style.pressedColour;
    }
    else if (shouldDrawButtonAsHighlighted)
    {
        fill = style.hoverColour;
    }

    const auto area = getLocalBounds().toFloat();
    g.setColour(fill);
    g.fillRoundedRectangle(area, style.cornerRadius);

    g.setColour(style.borderColour);
    g.drawRoundedRectangle(area, style.cornerRadius, 1.0f);

    g.setColour(style.textColour.withMultipliedAlpha(isEnabled() ? 1.0f : 0.7f));
    g.setFont(juce::FontOptions(style.textSize));
    g.drawFittedText(getName(), getLocalBounds(), juce::Justification::centred, 1);
}

void MixerToggleButton::applyStyle(const Style& styleIn)
{
    style = styleIn;
    repaint();
}

MuteButton::MuteButton()
    : MixerToggleButton("MUTE")
{
    setName("MUTE");
}

SoloButton::SoloButton()
    : MixerToggleButton("SOLO")
{
    setName("SOLO");
}

void MixerLevelMeter::setLevel(float linearLevel)
{
    const auto clamped = juce::jlimit(0.0f, 2.0f, linearLevel);
    if (std::abs(clamped - level) < 0.0001f)
    {
        return;
    }
    level = clamped;
    repaint();
}

void MixerLevelMeter::applyStyle(const Style& styleIn)
{
    style = styleIn;
    repaint();
}

void MixerLevelMeter::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour(style.backgroundColour);
    g.fillRoundedRectangle(area, style.cornerRadius);

    const auto normalized = juce::jlimit(0.0f, 1.0f, level);
    auto fill = style.fillColour;
    if (normalized > 0.90f)
    {
        fill = style.clipColour;
    }
    else if (normalized > 0.70f)
    {
        fill = style.highColour;
    }

    const auto fillWidth = area.getWidth() * normalized;
    if (fillWidth > 0.0f)
    {
        g.setColour(fill);
        g.fillRoundedRectangle(juce::Rectangle<float>(area.getX(), area.getY(), fillWidth, area.getHeight()), style.cornerRadius);
    }

    g.setColour(style.borderColour);
    g.drawRoundedRectangle(area, style.cornerRadius, 1.0f);
}
