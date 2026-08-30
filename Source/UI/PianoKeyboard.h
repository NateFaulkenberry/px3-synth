#pragma once

#include <JuceHeader.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

#include "Card.h"

class PianoKeyboard final : public juce::Component,
                            private juce::Timer
{
public:
    static constexpr int firstMidiNote = 21;  // A0
    static constexpr int lastMidiNote = 108;  // C8
    static constexpr int totalKeys = lastMidiNote - firstMidiNote + 1;
    static constexpr int whiteKeys = 52;

    PianoKeyboard();

    std::function<void(int midiNote, float velocityNorm)> onNoteOn;
    std::function<void(int midiNote)> onNoteOff;

    void setActiveNotes(const std::array<bool, totalKeys>& noteStates,
                        const std::array<float, totalKeys>& velocities);

    // How the "no oscillator engaged" warning is drawn. Every value comes from
    // UIConfig under keyboard.warning, because it is a piece of the interface
    // like any other and the rest of the plugin's chrome is configurable.
    struct WarningStyle
    {
        juce::String text { "Please engage an oscillator!" };
        // start | centre | end, applied horizontally within the keyboard.
        juce::Justification::Flags alignment { juce::Justification::centred };
        juce::Colour background { juce::Colour::fromRGBA(18, 19, 23, 232) };
        juce::Colour border { juce::Colour::fromRGBA(255, 138, 138, 190) };
        juce::Colour textColour { juce::Colour::fromRGB(245, 247, 250) };
        float borderWidth { 1.2f };
        float cornerRadius { 8.0f };
        float fontSize { 15.0f };
        px3::ui::Insets padding { 10.0f, 18.0f, 10.0f, 18.0f };
        px3::ui::Insets margin { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    void setWarningStyle(const WarningStyle& style);

    // Room for the sparks on EVERY side, not just above.
    //
    // A component cannot paint outside its own bounds - the parent clips it -
    // so the component is grown in each direction and draws the keys in the
    // middle of itself. The margin is transparent, does not respond to the
    // mouse, and exists only so the sparks have somewhere to go. The keys, the
    // hit testing and the warning all follow the keyboard area rather than the
    // component bounds.
    //
    // Four sides rather than one because a burst leaves a key in every
    // direction: clipping any edge cuts a visible arc out of it.
    void setSparkMargins(juce::BorderSize<int> margins);
    juce::BorderSize<int> getSparkMargins() const noexcept { return sparkMargins; }
    int getSparkHeadroom() const noexcept { return sparkMargins.getTop(); }
    // The keys themselves: the component minus its spark margins.
    juce::Rectangle<int> keyboardArea() const;

    // Silenced when every oscillator source is bypassed: nothing this keyboard
    // does can make a sound, so it stops animating, greys out, stops responding
    // to the mouse, and says why.
    void setSilenced(bool shouldBeSilenced);
    bool isSilenced() const noexcept { return silenced; }

    void paint(juce::Graphics& g) override;
    // The headroom is not part of the instrument, so clicks pass through it to
    // whatever is behind.
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    struct KeyGeometry
    {
        int midiNote { 0 };
        bool isBlack { false };
        juce::Rectangle<float> bounds;
    };

    struct Spark
    {
        juce::Point<float> position;
        juce::Point<float> velocity;
        juce::Colour colour;
        float lifetimeSeconds { 0.0f };
        float maxLifetimeSeconds { 0.0f };
        float width { 1.0f };
        float segmentLength { 6.0f };
        float zigzagAmplitude { 2.0f };
    };

    void paintKeyboard(juce::Graphics& g);
    void timerCallback() override;
    void spawnLightningBurst(int midiNote, bool isBlackKey, float velocityNorm);
    bool getKeyBoundsForNote(int midiNote, juce::Rectangle<float>& bounds, bool& isBlack) const;
    int midiNoteAt(juce::Point<float> position) const;

    static bool isBlackKey(int midiNote);
    static int whiteKeyIndex(int midiNote);
    static juce::String noteNameFor(int midiNote);

    std::array<bool, totalKeys> activeNotes {};
    std::array<bool, totalKeys> previousActiveNotes {};
    std::array<float, totalKeys> noteVelocities {};
    std::vector<Spark> sparks;
    juce::Random rng;
    float vibrationPhase { 0.0f };
    int heldMidiNote { -1 };
    float clickVelocityNorm { 0.65f };
    bool silenced { false };
    juce::BorderSize<int> sparkMargins { 0, 0, 0, 0 };
    WarningStyle warningStyle;
};
