#pragma once

#include <JuceHeader.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

class PianoKeyboard final : public juce::Component,
                            private juce::Timer
{
public:
    static constexpr int firstMidiNote = 21;  // A0
    static constexpr int lastMidiNote = 108;  // C8
    static constexpr int totalKeys = lastMidiNote - firstMidiNote + 1;
    static constexpr int whiteKeys = 52;

    PianoKeyboard();

    void setActiveNotes(const std::array<bool, totalKeys>& noteStates,
                        const std::array<float, totalKeys>& velocities);

    void paint(juce::Graphics& g) override;

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

    void timerCallback() override;
    void spawnLightningBurst(int midiNote, bool isBlackKey, float velocityNorm);
    bool getKeyBoundsForNote(int midiNote, juce::Rectangle<float>& bounds, bool& isBlack) const;

    static bool isBlackKey(int midiNote);
    static int whiteKeyIndex(int midiNote);
    static juce::String noteNameFor(int midiNote);

    std::array<bool, totalKeys> activeNotes {};
    std::array<bool, totalKeys> previousActiveNotes {};
    std::array<float, totalKeys> noteVelocities {};
    std::vector<Spark> sparks;
    juce::Random rng;
    float vibrationPhase { 0.0f };
};
