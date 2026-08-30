#pragma once

#include <JuceHeader.h>

#include "Card.h"

#include <memory>

class UIConfig;

namespace px3::ui
{
// Where one of the two insert buttons sits in a strip, and how big it is.
//
// Free, and taking its paths as arguments, so the PRECEDENCE can be tested
// directly. It has already been wrong once: the shared block was skipped
// entirely because the existence check could not distinguish an undeclared
// path from an empty one, and nothing failed - the buttons just silently kept
// their compiled defaults.
struct InsertButtonLayout
{
    int size { 26 };
    int offsetX { 0 };
    int offsetY { 0 };
};

// `sharedBase` is read first and `overrideBase` layered on top, key by key, so
// a card block only has to declare what it changes.
InsertButtonLayout readInsertButtonLayout(const UIConfig* config,
                                          const juce::String& sharedBase,
                                          const juce::String& overrideBase);
} // namespace px3::ui

class MixerChannelComponent final : public juce::Component
{
public:
    struct Controls
    {
        juce::Label* title { nullptr };
        juce::Component* meter { nullptr };
        juce::Button* mute { nullptr };
        juce::Button* solo { nullptr };
        juce::Button* phase { nullptr };
        juce::Slider* fader { nullptr };
        juce::Label* valueLabel { nullptr };
        juce::Slider* pan { nullptr };
        juce::Label* panLabel { nullptr };
        juce::Label* panValueLabel { nullptr };
        juce::Slider* send { nullptr };
        juce::Label* sendLabel { nullptr };
        juce::Label* sendValueLabel { nullptr };
        juce::Label* stereoTag { nullptr };
        // Bus inserts. Only the dry and FX strips have them; a source channel
        // passes nullptr and the layout skips the corners entirely.
        juce::Button* eqInsert { nullptr };
        juce::Button* compInsert { nullptr };
        bool hasSend { true };
    };

    explicit MixerChannelComponent(Controls controlsIn);

    // Foundation for the shared Card system. The mixer channel now draws the
    // same frame as every other component and reads a style block of its own,
    // so it participates in the system without its internals being redesigned:
    // its controls, and its existing title Label, are untouched. The card's own
    // title is intentionally left empty here - moving the title off the Label
    // belongs to the phase that reworks mixer internals.
    void setCardStyleKey(juce::String key);
    // This channel's identity colour, so its fader and meter can match the card.
    juce::Colour cardAccentColour() const;
    // Whether the source this channel controls is switched on. A bypassed
    // oscillator's channel greys out and stops responding, the same as the
    // oscillator's own card does - a live fader on a silent source is a lie.
    void setSourceActive(bool active);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void setPanelContentBounds(juce::Rectangle<int> panelContent);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void refreshCardStyle();
    void layoutInsertButtons();

    Controls controls;
    std::shared_ptr<const UIConfig> uiConfig;
    juce::String cardStyleKey { "mixerChannel" };
    px3::ui::CardHost card;
    bool sourceActive { true };
    int sectionSpacing { 10 };
    // Below the card title, before the first row.
    int titleGap { 6 };
    // Fader to its own dB readout: tighter than a section break, because the
    // two are one control.
    int faderValueSpacing { 0 };
    int buttonGap { 4 };
    int footerLabelHeight { 12 };
    int meterHeight { 12 };

    // The two insert buttons are placed by coordinate rather than by row: they
    // sit in the strip's bottom corners, under the last knob row, and the exact
    // spot is a design decision the config owns.
    px3::ui::InsertButtonLayout eqLayout;
    px3::ui::InsertButtonLayout compLayout;
};
