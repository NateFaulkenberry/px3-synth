#pragma once

#include <JuceHeader.h>

#include "Wavetable.h"
#include "UIConfig.h"
#include "Wavetable3DRenderer.h"

// The wavetable display, and the thing files are dropped onto.
//
// It is the drop target itself rather than a button next to one, because the
// picture is what a user is looking at when they think "I want that sound in
// here". A hidden import dialog is a worse version of the same feature.
class WavetableGraph final : public juce::Component,
                             public juce::FileDragAndDropTarget
{
public:
    WavetableGraph();

    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void setAccentColour(juce::Colour accentIn);

    // The panel's visual identity, resolved from config in one place.
    //
    // Exposed because the oscillator card draws a DIFFERENT panel in the same
    // rectangle in every non-wavetable mode, and the two have to be identical
    // or the border changes colour when the mode changes - which it did. They
    // shared an accent and an alpha and still differed, because the mode visual
    // filled translucent RGBA(12,16,26,170) where this fills opaque #0C0C0E,
    // and a border at alpha 0.32 composites to whatever is under it.
    juce::Colour getBorderColour() const { return borderColour(); }
    juce::Colour getPanelFillColour() const;
    float getPanelCornerRadius() const { return cornerRadius(); }
    float getPanelBorderWidth() const;

    // Rebuilding the surface is the expensive part, so it happens when the
    // TABLE changes and not when the position does.
    void setDisplay(px3::WavetableDisplay displayIn);

    // Base is where the parameter sits, modulated is where the sound actually
    // is. Showing both is what makes an LFO on the scan legible rather than
    // just animated.
    void setPosition(float base, float modulated);

    void setMissingTableName(const juce::String& name);
    void setBypassed(bool shouldBeBypassed);

    // Called on the message thread when a file is dropped. The component knows
    // nothing about importing - it reports the file and lets the editor decide.
    std::function<void(const juce::File&)> onFileDropped;

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    static bool isSupportedFile(const juce::File& file);

private:
    // The GPU view sits between the background this component paints and the
    // overlay above it, because a JUCE parent paints BEFORE its children - so
    // markers and text drawn in paint() would end up underneath the GL view.
    struct Overlay final : public juce::Component
    {
        explicit Overlay(WavetableGraph& ownerIn) : owner(ownerIn)
        {
            setInterceptsMouseClicks(false, false);
        }
        void paint(juce::Graphics& g) override { owner.paintOverlay(g); }
        WavetableGraph& owner;
    };

    void paintOverlay(juce::Graphics& g);
    void rebuildSurface();
    float cornerRadius() const;
    juce::Colour borderColour() const;
    int glInset() const;
    juce::Colour configColour(const juce::String& path, juce::Colour fallback) const;
    float configFloat(const juce::String& path, float fallback) const;

    std::shared_ptr<const UIConfig> config;
    juce::Colour accent { juce::Colour::fromRGB(90, 160, 240) };

    px3::WavetableDisplay display;
    juce::String missingTableName;
    bool enabled { true };

    // The stack of frames, drawn once into an image.
    //
    // Measured, this does NOT currently pay for itself: repainting the graph's
    // region in situ costs 0.898 ms with the cache and 0.819 ms without it, both
    // dominated by the editor beneath rather than by forty polylines of 192
    // points. It is kept because the display size is a parameter - a 256-frame
    // table drawn at full width is six times the geometry - not because the
    // present numbers justify it. If that stops being true, delete it.
    juce::Image surface;

    Wavetable3DRenderer glView;
    Overlay overlay { *this };

    float basePosition { 0.0f };
    float modulatedPosition { 0.0f };
    bool dragging { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableGraph)
};
