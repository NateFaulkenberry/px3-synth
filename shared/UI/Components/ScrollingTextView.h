#pragma once

#include <JuceHeader.h>

namespace px3::ui
{

// A block of wrapped, read-only text that scrolls when there is more of it than
// there is room for.
//
// This exists because the two obvious answers are both wrong here. A Label does
// not wrap and does not scroll, so long text is simply cut off. A read-only
// TextEditor does both - and costs about eight milliseconds of every editor
// repaint, measured, which is more than the entire rest of the plug-in's UI put
// together and enough on its own to miss a 60 Hz frame.
//
// So: one TextLayout, built when the text or the width changes, and drawn. The
// layout is the expensive part and it is not rebuilt per frame; painting it is
// just glyphs.
class ScrollingTextView final : public juce::Component
{
public:
    ScrollingTextView() = default;

    void setContent(const juce::String& newText, juce::Colour colour, float newFontSize)
    {
        if (text == newText && textColour == colour
                && juce::approximatelyEqual(fontSize, newFontSize))
        {
            return;
        }

        text = newText;
        textColour = colour;
        fontSize = newFontSize;
        layoutDirty = true;
        rebuildLayout(static_cast<float>(getWidth()));
        repaint();
    }

    const juce::String& getText() const noexcept { return text; }

    // The height this text needs at a given width, so an owner can size it
    // inside a viewport and let the viewport decide whether to scroll.
    int heightForWidth(int width)
    {
        rebuildLayout(static_cast<float>(width));
        return juce::jmax(0, static_cast<int>(std::ceil(layout.getHeight())));
    }

    void resized() override { rebuildLayout(static_cast<float>(getWidth())); }

    void paint(juce::Graphics& g) override
    {
        if (text.isEmpty()) { return; }

        layout.draw(g, getLocalBounds().toFloat());
    }

private:
    void rebuildLayout(float width)
    {
        // Guarded on width as well as content: the layout depends on both, and
        // rebuilding it on every paint is the cost this component exists to
        // avoid.
        if (width <= 0.0f) { return; }
        if (juce::approximatelyEqual(width, laidOutWidth) && ! layoutDirty) { return; }

        juce::AttributedString attributed;
        attributed.setText(text);
        attributed.setColour(textColour);
        attributed.setFont(juce::Font(juce::FontOptions(fontSize)));
        attributed.setJustification(juce::Justification::topLeft);
        attributed.setWordWrap(juce::AttributedString::byWord);

        layout.createLayout(attributed, width);
        laidOutWidth = width;
        layoutDirty = false;
    }

    juce::String text;
    juce::Colour textColour { juce::Colours::white };
    float fontSize { 11.0f };

    juce::TextLayout layout;
    float laidOutWidth { -1.0f };
    bool layoutDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScrollingTextView)
};

} // namespace px3::ui
