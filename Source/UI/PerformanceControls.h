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

    // Extra height ABOVE the controls, reserved for the wheels' spark
    // animations. Same arrangement the keyboard uses: a component cannot paint
    // outside its own bounds, so it is grown upward and draws its controls at
    // the bottom of itself. The strip is transparent and passes clicks through.
    void setSparkHeadroom(int pixels);
    int getSparkHeadroom() const noexcept { return sparkHeadroomPx; }
    // The controls themselves: the component minus its spark headroom.
    juce::Rectangle<int> controlsArea() const;

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    int sparkHeadroomPx { 0 };

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

    // One sparkle. Emitted radially, tinted from a rotating hue, and drawn as a
    // four-point star rather than a dot so it reads as a spark rather than as
    // noise.
    struct Sparkle
    {
        juce::Point<float> position;
        juce::Point<float> velocity;
        float lifetimeSeconds { 0.0f };
        float maxLifetimeSeconds { 0.0f };
        float size { 1.0f };
        float hue { 0.0f };
        float rotation { 0.0f };
        float spin { 0.0f };
    };

    void timerCallback() override;
    void updateFromMousePosition(juce::Point<float> position);
    // `intensity` is 0 to 1 and drives count, speed, size and lifetime
    // together - one number, so the whole burst grows with the bend rather than
    // only part of it changing.
    void emitSparkles(juce::Point<float> origin, float intensity);
    static juce::Path createSparklePath(float size);

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
    std::vector<Sparkle> sparkles;
    // Advances with every burst so consecutive sparkles are different colours
    // and the emission reads as a rainbow rather than as one tint at a time.
    float hueCycle { 0.0f };
    // A moved wheel gets a short-lived boost on top of its displacement, so a
    // fast sweep sparks harder than a slow one at the same position.
    float pitchKick { 0.0f };
    float modKick { 0.0f };
    float pitchEmitAccumulator { 0.0f };
    float modEmitAccumulator { 0.0f };
    juce::Random rng;
};
