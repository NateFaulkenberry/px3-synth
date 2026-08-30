#pragma once

#include <JuceHeader.h>

#include "FetPanelStyle.h"
#include "VuBallistics.h"

#include <functional>

class UIConfig;

namespace px3::ui
{

// A moving-coil VU meter: cached face, vector needle, physical movement.
//
// Its own component rather than a rectangle inside the compressor's paint, for
// two reasons. The face - ticks, numbers, arc, lamps, badge - does not change
// between frames and is rendered once into an image; only the needle is
// redrawn. And a component can invalidate just the swept region, so animating
// the needle does not repaint the panel around it.
class VuMeterComponent final : public juce::Component,
                               private juce::Timer
{
public:
    enum class Mode { gainReduction, level };

    VuMeterComponent();
    ~VuMeterComponent() override;

    // What the movement is reading. Supplied per frame by the owner.
    std::function<double()> getTargetPosition;
    // Whether the unit is live: the lamps go out and the needle rests when not.
    std::function<bool()> isLive;

    void setMode(Mode mode);
    void setUIConfig(std::shared_ptr<const UIConfig> config);
    void setBadgeText(juce::String text);

    // 0 VU against digital full scale. There is no fixed digital equivalent of
    // +4 dBu, so this is a stated choice: -18 dBFS, the common alignment.
    static constexpr float kZeroVuDbfs = -18.0f;
    // Where a level sits on the face, with +3 VU at the right stop.
    static double positionForLevelDb(double dbfs);
    static double positionForReductionDb(double db);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // For tests and the debug console: the movement's live state.
    double needlePosition() const noexcept { return movement.position(); }
    double needleVelocity() const noexcept { return movement.velocity(); }

private:
    void timerCallback() override;
    void rebuildFace();
    void paintFace(juce::Graphics& g, juce::Rectangle<float> bounds) const;
    // The needle as geometry: a tapered blade about a true pivot.
    static juce::Path needlePath(float length, float width);
    juce::Rectangle<int> needleRegion() const;

    VuBallistics movement;
    Mode meterMode { Mode::gainReduction };
    std::shared_ptr<const UIConfig> uiConfig;
    juce::String badgeText { "P(X3) LIMITING AMPLIFIER" };

    juce::Image face;
    double lastFrameSeconds { 0.0 };
    double lastDrawnPosition { -1.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VuMeterComponent)
};

} // namespace px3::ui
