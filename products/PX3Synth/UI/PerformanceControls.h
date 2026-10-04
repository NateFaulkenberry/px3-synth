#pragma once

#include <JuceHeader.h>

class UIConfig;

#include "RoundedRect.h"

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

    // The controls occupy the whole component.
    juce::Rectangle<int> controlsArea() const;
    // The strip less the left cheek: where the wheel panels tile.
    juce::Rectangle<int> wheelsArea() const;

    // Everything the pitch and mod section is drawn with. Every field is read
    // by paint or drawWheel.
    struct Style
    {
        juce::Colour background { juce::Colour::fromRGB(20, 20, 20) };
        // The wheel panels tile the strip like modules: panelInset clear of the
        // strip's edge, panelGap between PITCH and MOD.
        float panelInset { 0.0f };
        float panelGap { 1.0f };
        // The keyboard-case end on the strip's left, mirroring the cheek at
        // the keyboard's right, so the bottom row reads as one instrument.
        float cheekLeft { 0.0f };
        float backgroundOpacity { 1.0f };
        // The outer frame, inset from the component edge.
        float borderInset { 2.0f };
        juce::Colour borderColour { juce::Colour::fromRGBA(255, 255, 255, 24) };
        float borderWidth { 1.0f };
        px3::ui::CornerRadii borderRadius { px3::ui::CornerRadii::all(10.0f) };

        // Each wheel's backing panel. The shared colour is the fallback and
        // either wheel can override it, the same way their accents do - the
        // two are a pair, and wanting them to differ is a normal thing to want.
        juce::Colour panelColour { juce::Colour::fromRGB(17, 17, 17) };
        float panelOpacity { 0.863f };          // 220/255, the value it shipped with
        juce::Colour pitchPanelColour { juce::Colour::fromRGB(17, 17, 17) };
        float pitchPanelOpacity { 0.863f };
        juce::Colour modPanelColour { juce::Colour::fromRGB(17, 17, 17) };
        float modPanelOpacity { 0.863f };
        px3::ui::CornerRadii panelRadius { px3::ui::CornerRadii::all(8.0f) };

        juce::Colour titleColour { juce::Colour::fromRGB(228, 228, 228) };
        float titleSize { 10.5f };
        float titleHeight { 18.0f };

        // The slot the handle rides in. Its tint is the wheel's accent, at an
        // alpha that rises with activity - so both ends of that range are here.
        px3::ui::CornerRadii trackRadius { px3::ui::CornerRadii::all(8.0f) };
        // The slot's width, and how far it stands off the wheel panel's top
        // and bottom edges. The handle travels the slot's length less its own
        // radius at each end, so these set how far a wheel can move.
        float trackWidth { 10.0f };
        float trackInsetTop { 22.0f };
        float trackInsetBottom { 22.0f };
        float trackFillAlpha { 0.22f };
        float trackFillGlowAlpha { 0.28f };
        float trackBorderAlpha { 0.36f };
        float trackBorderGlowAlpha { 0.42f };
        float trackBorderWidth { 1.0f };
        float centreLineAlpha { 0.30f };

        float handleRadius { 7.0f };
        float handleGlowOuterRadius { 15.0f };
        float handleGlowInnerRadius { 11.0f };
        juce::Colour handleRimColour { juce::Colour::fromRGB(255, 255, 255) };

        // A hairline between the two wheels.
        //
        // The wheels sit SIDE BY SIDE - 70 x 96 each in the shipping layout -
        // so the line that falls between them is vertical. Horizontal instead
        // runs across the middle of both rather than between them, which is a
        // different thing and a legitimate one to want, so the orientation is
        // a setting rather than a consequence of the layout.
        //
        // Width is here as well as the three asked for, because a divider that
        // cannot be set to 0 is one that cannot be removed.
        enum class DividerOrientation { horizontal, vertical };
        DividerOrientation dividerOrientation { DividerOrientation::horizontal };
        juce::Colour dividerColour { juce::Colour::fromRGB(255, 255, 255) };
        float dividerOpacity { 0.15f };
        float dividerInset { 8.0f };    // from the top and bottom of the strip
        float dividerWidth { 1.0f };

        juce::Colour pitchAccent { juce::Colour::fromRGB(82, 155, 255) };
        juce::Colour modAccent { juce::Colour::fromRGB(232, 84, 78) };

        static Style fromConfig(const UIConfig* config, const juce::String& prefix);
    };

    void setStyle(const Style& style);

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
    juce::Rectangle<float> trackIn(juce::Rectangle<float> panel) const;
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
    // The values last painted, so the timer repaints only on a visible change.
    float drawnPitch { -2.0f }, drawnMod { -2.0f }, drawnPitchGlow { -2.0f }, drawnModGlow { -2.0f };

    ActiveControl activeControl { ActiveControl::none };
    Style style;
};
