#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

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

    struct CatSpark
    {
        juce::Point<float> position;
        juce::Point<float> velocity;
        float lifetimeSeconds { 0.0f };
        float maxLifetimeSeconds { 0.0f };
        float scale { 1.0f };
        float rotation { 0.0f };
        float spin { 0.0f };
    };

    struct UnicornSpark
    {
        juce::Point<float> position;
        juce::Point<float> velocity;
        float lifetimeSeconds { 0.0f };
        float maxLifetimeSeconds { 0.0f };
        float scale { 1.0f };
        float facing { 1.0f };
        float rotation { 0.0f };
        float spin { 0.0f };
    };

    void timerCallback() override;
    void updateFromMousePosition(juce::Point<float> position);
    void spawnCatsFromModWheel(float movementAmount);
    void spawnUnicornsFromPitchWheel(float movementAmount, float direction);
    static juce::Path createCatPath(float scale);
    static juce::Path createUnicornPath(float scale, float facing);
    static juce::Path createUnicornHornPath(float scale, float facing);

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
    float previousTargetPitch { 0.0f };
    float previousTargetMod { 0.0f };

    ActiveControl activeControl { ActiveControl::none };
    std::vector<CatSpark> catSparks;
    std::vector<UnicornSpark> unicornSparks;
    juce::Random rng;
};
