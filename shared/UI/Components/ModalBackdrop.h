#pragma once

#include <JuceHeader.h>

namespace px3::ui
{

// The dim-and-blur treatment behind a modal sheet.
//
// Extracted from the preset browser, which is where it was written and where it
// is still used. The EQ and compressor overlays reuse the same backdrop - the
// point being that every sheet in the plugin dims the window the same way, even
// though each one draws a completely different panel on top of it.
//
// It is a snapshot blur, not a live one: the caller grabs the editor once when
// the sheet opens and hands the image back on every paint. A live blur would
// mean re-rendering the whole UI every frame to throw most of it away.
//
// An EMPTY panelBounds means "no hole": the treatment covers the whole area.
// That is what a sheet with a translucent face needs - the backdrop has to be
// behind it, not around it, or the sheet shows the sharp bright editor through
// itself while everything beside it is blurred and dark.
// `blurRadius` is how far the treatment spreads, in pixels at 1x. The default
// is 4.5: three quarters of the 6 this started at, which is a visibly softer
// touch on the UI behind a sheet while still reading as out of focus.
void paintModalBackdrop(juce::Graphics& g,
                        juce::Rectangle<int> fullBounds,
                        juce::Rectangle<float> panelBounds,
                        const juce::Image& snapshot,
                        float panelCornerRadius,
                        juce::Colour dimColour = juce::Colour::fromRGBA(0, 0, 0, 180),
                        float blurRadius = 4.5f);

// Blocks every mouse event from reaching the UI behind a sheet, while still
// letting the owner see clicks - which is what click-outside-to-close needs.
class ModalScrim final : public juce::Component
{
public:
    explicit ModalScrim(juce::Component& ownerIn) : owner(ownerIn) {}

    // The dimmed backdrop is drawn HERE rather than over the top of everything,
    // because this component sits below the sheet in z-order. A sheet with a
    // translucent face then shows the treated backdrop through itself, which is
    // what makes it look like one surface over another rather than a window cut
    // into the dimming.
    void setBackdropImage(juce::Image image)
    {
        backdrop = std::move(image);
        treated = {};
        repaint();
    }

    void setBlurRadius(float radius)
    {
        blurRadius = juce::jmax(0.0f, radius);
        treated = {};
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        // The treatment is rendered ONCE and then blitted.
        //
        // It used to run on every paint, and the blur is ~49 draws of a
        // full-window snapshot. Anything animating above the scrim - the
        // compressor's needle, most obviously - invalidates a region of it
        // every frame, and re-blurring that region measured 27.6 ms per frame
        // against 0.02 ms for the needle itself. The backdrop is a still image
        // for as long as the sheet is open, so there was never a reason to
        // recompute it.
        if (backdrop.isValid()
            && (treated.isNull() || treated.getWidth() != getWidth() || treated.getHeight() != getHeight()))
        {
            treated = juce::Image(juce::Image::ARGB, juce::jmax(1, getWidth()), juce::jmax(1, getHeight()), true);
            juce::Graphics tg(treated);
            paintModalBackdrop(tg, getLocalBounds(), {}, backdrop, 0.0f,
                               juce::Colour::fromRGBA(0, 0, 0, 180), blurRadius);
        }

        if (treated.isValid())
        {
            g.drawImageAt(treated, 0, 0, false);
        }
    }

private:
    juce::Image backdrop;
    juce::Image treated;
    float blurRadius { 4.5f };

    void forward(const juce::MouseEvent& e, void (juce::Component::*handler)(const juce::MouseEvent&))
    {
        (owner.*handler)(e.getEventRelativeTo(&owner));
    }

    juce::Component& owner;
};

} // namespace px3::ui
