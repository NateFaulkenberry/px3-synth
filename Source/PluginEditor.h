#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

#include "PianoKeyboard.h"
#include "PluginProcessor.h"

class SynthProjectAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit SynthProjectAudioProcessorEditor(SynthProjectAudioProcessor&);
    ~SynthProjectAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class KnobLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider(juce::Graphics& g,
                              int x,
                              int y,
                              int width,
                              int height,
                              float sliderPos,
                              float rotaryStartAngle,
                              float rotaryEndAngle,
                              juce::Slider& slider) override;
    };

    class KnobLabel final : public juce::Label
    {
    public:
        void paint(juce::Graphics& g) override;
    };

    struct KnobBinding
    {
        juce::Slider* slider { nullptr };
        juce::Label* label { nullptr };
        juce::AudioParameterFloat* parameter { nullptr };
    };

    void configureKnob(KnobBinding& binding, const juce::String& labelText, juce::AudioParameterFloat& parameter);
    void refreshAnyKeyDownState();
    void layoutKnobGroup(const juce::Rectangle<int>& groupArea,
                         int startIndex,
                         int knobCount,
                         const juce::Colour& sectionAccent);
    static juce::String noteNameForMidi(int midiNote);
    void timerCallback() override;

    SynthProjectAudioProcessor& audioProcessor;
    KnobLookAndFeel knobLookAndFeel;
    PianoKeyboard pianoKeyboard;
    SynthProjectAudioProcessor::MidiStatus midiStatus;

    juce::Image backgroundImage;
    juce::Image logoFrame;
    bool anyKeyDown { false };
    float logoWobblePhase { 0.0f };

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> controlsArea;
    juce::Rectangle<int> logoPanelArea;
    juce::Rectangle<int> headerPlaceholderArea;
    juce::Rectangle<int> midiStatusArea;
    std::array<juce::Rectangle<int>, 4> knobGroupAreas {};

    juce::Slider oscSineKnob;
    juce::Slider oscSawKnob;
    juce::Slider oscSquareKnob;
    juce::Slider cutoffKnob;
    juce::Slider resonanceKnob;
    juce::Slider attackKnob;
    juce::Slider decayKnob;
    juce::Slider sustainKnob;
    juce::Slider releaseKnob;
    juce::Slider gainKnob;

    KnobLabel oscSineLabel;
    KnobLabel oscSawLabel;
    KnobLabel oscSquareLabel;
    KnobLabel cutoffLabel;
    KnobLabel resonanceLabel;
    KnobLabel attackLabel;
    KnobLabel decayLabel;
    KnobLabel sustainLabel;
    KnobLabel releaseLabel;
    KnobLabel gainLabel;
    KnobLabel midiStatusLabel;

    std::array<KnobBinding, 10> knobBindings {};
};
