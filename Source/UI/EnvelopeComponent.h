#pragma once

#include <JuceHeader.h>

#include <memory>

class UIConfig;

// Reusable interactive ADSR graph that binds directly to provided parameters.
class EnvelopeComponent final : public juce::Component
{
public:
    EnvelopeComponent(juce::AudioParameterFloat& attackIn,
                      juce::AudioParameterFloat& decayIn,
                      juce::AudioParameterFloat& sustainIn,
                      juce::AudioParameterFloat& releaseIn,
                      juce::AudioParameterBool& enabledIn,
                      juce::ToggleButton& enabledButtonIn,
                      juce::Label& enabledLabelIn,
                      juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void refreshFromParameters();

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    enum class DragHandle
    {
        none,
        attack,
        decaySustain,
        release
    };

    struct Geometry
    {
        float left { 0.0f };
        float right { 0.0f };
        float top { 0.0f };
        float bottom { 0.0f };
        float attackRangeWidth { 1.0f };
        float releaseRangeWidth { 1.0f };
        float minDecayGap { 1.0f };
        float minSustainWidth { 1.0f };
        juce::Point<float> start;
        juce::Point<float> attackPoint;
        juce::Point<float> decaySustainPoint;
        juce::Point<float> releasePoint;
        juce::Point<float> end;
    };

    static float clamp01(float v);
    static float timeToVisualNorm(float seconds, float minValue, float maxValue);
    static float visualNormToTime(float norm, float minValue, float maxValue);
    Geometry computeGeometry() const;
    static float distSq(juce::Point<float> a, juce::Point<float> b);
    static float distToSegmentSq(juce::Point<float> p, juce::Point<float> a, juce::Point<float> b);
    DragHandle pickHandle(juce::Point<float> p, const Geometry& geom) const;
    juce::Point<float> handlePositionFor(DragHandle handle, const Geometry& geom) const;
    void drawHandleMarker(juce::Graphics& g, juce::Point<float> center, DragHandle handle) const;
    void drawHandleLabel(juce::Graphics& g,
                         juce::Point<float> center,
                         DragHandle handle,
                         const juce::String& id) const;
    juce::String valueTextForHandle(DragHandle handle) const;
    void setParameterFromActualValue(juce::AudioParameterFloat& parameter, float value);
    void applyDragPosition(juce::Point<float> mousePos, const Geometry& geom);

    juce::AudioParameterFloat& attack;
    juce::AudioParameterFloat& decay;
    juce::AudioParameterFloat& sustain;
    juce::AudioParameterFloat& release;
    juce::AudioParameterBool& enabled;
    juce::ToggleButton& enabledButton;
    juce::Label& enabledLabel;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    DragHandle hoverHandle { DragHandle::none };
    DragHandle dragHandle { DragHandle::none };
    bool draggingSustainSegment { false };
    float sustainDragStartX { 0.0f };
    float sustainDragStartDecayX { 0.0f };
    float sustainDragStartReleaseX { 0.0f };
    float lastAttack { -1.0f };
    float lastDecay { -1.0f };
    float lastSustain { -1.0f };
    float lastRelease { -1.0f };
    bool currentEnabled { true };
    juce::Colour baseEnabledLabelTextColour;
};
