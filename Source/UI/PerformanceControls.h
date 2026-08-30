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

    // Room for the sparkles on EVERY side, not just above.
    //
    // A component cannot paint outside its own bounds, so the component is
    // grown in each direction and draws its controls in the middle of itself.
    // The margin is transparent and passes clicks through, which is what lets
    // it overlap the keyboard to the right without swallowing key presses.
    //
    // Four sides rather than one because the wheels emit radially: sparkles
    // leave in every direction, so clipping any edge cuts a visible arc out of
    // the burst.
    void setSparkMargins(juce::BorderSize<int> margins);
    juce::BorderSize<int> getSparkMargins() const noexcept { return sparkMargins; }
    int getSparkHeadroom() const noexcept { return sparkMargins.getTop(); }
    // The controls themselves: the component minus its spark margins.
    juce::Rectangle<int> controlsArea() const;

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    juce::BorderSize<int> sparkMargins { 0, 0, 0, 0 };

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
    // Emitted from the RIM of the handle, not its centre: `centre` and
    // `radius` describe the wheel's knob, and each sparkle leaves the point on
    // that circle where it was born, travelling outward along the same radius.
    void emitSparkles(juce::Point<float> centre, float radius, float intensity);
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
