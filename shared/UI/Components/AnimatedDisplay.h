#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <functional>

namespace px3::ui
{
// An animated display (a scrolling wave, a moving playhead) as its own opaque
// layer over the card that owns it.
//
// Without it, every animation frame repainted the display's rectangle through
// everything beneath it: the card's gradient faceplate, the panel, the editor
// background - all shaded again in software at the display's scale, 30 times
// a second, for each of six displays. Measured with animations on: 27% of a
// core idle, the message thread saturated with a chord held.
//
// Here the still part - whatever the owner draws under the moving part,
// faceplate included - is rendered once into an image and blitted; only the
// moving part is drawn per frame. Being opaque, nothing beneath it is painted
// for its area at all.
//
// Both callbacks draw in the OWNER's coordinates, so the owner's existing
// drawing code is reused unchanged. Call invalidateStill() whenever the still
// part could have changed (bypass, colours, layout); a resize does it itself.
class AnimatedDisplay final : public juce::Component
{
public:
    AnimatedDisplay()
    {
        setOpaque(true);
        setInterceptsMouseClicks(false, false);   // the owner keeps its mouse behaviour
    }

    std::function<void(juce::Graphics&)> paintStill;
    std::function<void(juce::Graphics&)> paintMoving;

    void invalidateStill()
    {
        stillValid = false;
        requestFrame();
    }

    // Redraw the moving part. The animation tick calls this every frame, but
    // with "Enable animations" off there is no tick, so the owner must also
    // call it whenever anything the drawing depends on changes - or a new
    // wave choice shows the old wave until something else repaints.
    void requestFrame()
    {
        ++frameRequests;
        repaint();
    }

    // How many redraws have been asked for; for tests.
    std::uint64_t getFrameRequestCount() const noexcept { return frameRequests; }

    // The same, without asking for a repaint - for the owner's own paint(),
    // where a repaint request would loop.
    void markStillStale() noexcept { stillValid = false; }

    void resized() override { stillValid = false; }

    void paint(juce::Graphics& g) override
    {
        const auto scale = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto wanted = juce::Rectangle<int>(juce::roundToInt(static_cast<float>(getWidth()) * scale),
                                                 juce::roundToInt(static_cast<float>(getHeight()) * scale));
        if (! stillValid || still.getBounds() != wanted)
        {
            still = juce::Image(juce::Image::RGB, juce::jmax(1, wanted.getWidth()), juce::jmax(1, wanted.getHeight()), true);
            juce::Graphics sg(still);
            sg.addTransform(juce::AffineTransform::scale(scale));
            sg.fillAll(juce::Colour(0xff0c0e10));   // the chassis, under anything translucent
            sg.addTransform(juce::AffineTransform::translation(static_cast<float>(-getX()), static_cast<float>(-getY())));
            if (paintStill != nullptr) { paintStill(sg); }
            stillValid = true;
        }

        g.drawImage(still, getLocalBounds().toFloat());
        if (paintMoving != nullptr)
        {
            juce::Graphics::ScopedSaveState state(g);
            g.addTransform(juce::AffineTransform::translation(static_cast<float>(-getX()), static_cast<float>(-getY())));
            paintMoving(g);
        }
    }

private:
    juce::Image still;
    bool stillValid { false };
    std::uint64_t frameRequests { 0 };
};
} // namespace px3::ui
