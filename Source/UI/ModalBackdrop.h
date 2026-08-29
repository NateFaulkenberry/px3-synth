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
void paintModalBackdrop(juce::Graphics& g,
                        juce::Rectangle<int> fullBounds,
                        juce::Rectangle<float> panelBounds,
                        const juce::Image& snapshot,
                        float panelCornerRadius,
                        juce::Colour dimColour = juce::Colour::fromRGBA(0, 0, 0, 180));

// Blocks every mouse event from reaching the UI behind a sheet, while still
// letting the owner see clicks - which is what click-outside-to-close needs.
class ModalScrim final : public juce::Component
{
public:
    explicit ModalScrim(juce::Component& ownerIn) : owner(ownerIn) {}

    void mouseDown(const juce::MouseEvent& e) override { forward(e, &juce::Component::mouseDown); }
    void mouseDrag(const juce::MouseEvent& e) override { forward(e, &juce::Component::mouseDrag); }
    void mouseUp(const juce::MouseEvent& e) override { forward(e, &juce::Component::mouseUp); }

private:
    void forward(const juce::MouseEvent& e, void (juce::Component::*handler)(const juce::MouseEvent&))
    {
        (owner.*handler)(e.getEventRelativeTo(&owner));
    }

    juce::Component& owner;
};

} // namespace px3::ui
