#pragma once

// The release UI's home for the messages the keyboard used to carry (MIDI /
// macro assignment prompts, "Please engage an oscillator!") while the
// performance section is hidden. A small plate centred at the bottom of the
// instrument, shown only while there is something to say; it takes no clicks.

#include <JuceHeader.h>

#include "Theme.h"

namespace px3::ui
{
class NoticeBanner final : public juce::Component
{
public:
    NoticeBanner()
    {
        setComponentID("notice.banner");
        setInterceptsMouseClicks(false, false);
    }

    // Shows `text` (empty hides it), centred on the bottom edge of `area`.
    void show(const juce::String& text, juce::Rectangle<int> area)
    {
        if (text != message) { message = text; repaint(); }
        if (message.isEmpty()) { setVisible(false); return; }
        const auto width = juce::jmin(area.getWidth(), juce::roundToInt(textWidth(message)) + 2 * kPadding);
        setBounds(juce::Rectangle<int>(width, kHeight).withCentre({ area.getCentreX(), area.getBottom() - kHeight / 2 - 6 }));
        setVisible(true);
        toFront(false);
    }

    juce::String getMessage() const { return message; }
    static float textWidth(const juce::String& text) { return theme::textWidth(text, theme::Type::label); }
    static constexpr int kPadding = 16;
    static constexpr int kHeight = 26;

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xf0121416));
        g.fillRect(area);
        g.setColour(theme::colour::knobAccent);
        g.fillRect(area.removeFromLeft(3.0f));
        g.setColour(theme::colour::railEdge);
        g.drawRect(getLocalBounds().toFloat(), 1.0f);
        theme::drawLabel(g, message, getLocalBounds().toFloat().reduced(static_cast<float>(kPadding) - 4.0f, 0.0f),
                         theme::Type::label, theme::colour::textPrimary);
    }

private:
    juce::String message;
};
} // namespace px3::ui
