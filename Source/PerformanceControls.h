#pragma once

#include <JuceHeader.h>

#include <functional>

class PerformanceControls final : public juce::Component,
                                  private juce::Timer
{
public:
    PerformanceControls();

    void setControllerState(float pitchBendNormalized,
                            float modWheelNormalized,
                            float pitchActivity,
                            float modActivity);

    std::function<void(float)> onPitchBendChanged;
    std::function<void()> onPitchBendGestureEnded;
    std::function<void(float)> onModWheelChanged;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    enum class ActiveControl
    {
        none,
        pitch,
        mod
    };

    struct WheelVisual
    {
        juce::Rectangle<float> panel;
        juce::Rectangle<float> track;
    };

    void timerCallback() override;
    void updateFromMousePosition(juce::Point<float> position);

    WheelVisual getPitchVisual() const;
    WheelVisual getModVisual() const;

    static float clampPitch(float value);
    static float clampMod(float value);

    float targetPitch { 0.0f };
    float targetMod { 0.0f };
    float targetPitchGlow { 0.0f };
    float targetModGlow { 0.0f };

    float visualPitch { 0.0f };
    float visualMod { 0.0f };
    float visualPitchGlow { 0.0f };
    float visualModGlow { 0.0f };

    ActiveControl activeControl { ActiveControl::none };
};
