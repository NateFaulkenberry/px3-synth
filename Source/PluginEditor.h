#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

#include "PerformanceControls.h"
#include "PianoKeyboard.h"
#include "PluginProcessor.h"
#include "SourceEnginePanel.h"

class SynthProjectAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit SynthProjectAudioProcessorEditor(SynthProjectAudioProcessor&);
    ~SynthProjectAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

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
    void configureEffectKnob(juce::Slider& slider,
                             KnobLabel& label,
                             const juce::String& labelText,
                             juce::AudioParameterFloat& parameter);
    void refreshAnyKeyDownState();
    void refreshOscillatorModeUI();
    void refreshFxBypassUI();
    void updateFxSectionTargets(const juce::Rectangle<int>& topArea, int topGap);
    void layoutFxSectionsFromCurrentAreas();
    void animateFxSections();
    int indexForFxSection(int sectionId) const;
    int fxSectionAtPoint(juce::Point<int> point) const;
    void moveFxSectionToSlot(int sectionId, int slotIndex);
    void commitFxOrderToProcessor();
    void layoutKnobGroup(const juce::Rectangle<int>& groupArea,
                         int startIndex,
                         int knobCount,
                         const juce::Colour& sectionAccent);
    static juce::String noteNameForMidi(int midiNote);
    void timerCallback() override;

    SynthProjectAudioProcessor& audioProcessor;
    KnobLookAndFeel knobLookAndFeel;
    juce::TooltipWindow tooltipWindow;
    SourceEnginePanel sourceEnginePanel;
    PerformanceControls performanceControls;
    PianoKeyboard pianoKeyboard;
    SynthProjectAudioProcessor::MidiStatus midiStatus;

    juce::Image backgroundImage;
    juce::Image logoFrame;
    bool anyKeyDown { false };
    float logoWobblePhase { 0.0f };
    float oscVizPhase { 0.0f };

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> controlsArea;
    juce::Rectangle<int> logoPanelArea;
    juce::Rectangle<int> headerPlaceholderArea;
    juce::Rectangle<int> robSectionArea;
    juce::Rectangle<int> isaacSectionArea;
    juce::Rectangle<int> reverbSectionArea;
    juce::Rectangle<int> topSpareSectionArea;
    juce::Rectangle<int> midiStatusArea;
    juce::Rectangle<int> performanceControlsArea;
    std::array<juce::Rectangle<int>, 4> knobGroupAreas {};
    std::array<juce::Rectangle<int>, 3> fxSectionSlots {};
    std::array<juce::Rectangle<float>, 3> fxSectionCurrentAreas {};
    std::array<juce::Rectangle<float>, 3> fxSectionTargetAreas {};
    std::array<int, 3> fxSectionOrder { { 0, 1, 2 } };
    bool fxSectionsInitialized { false };
    int draggingFxSection { -1 };
    float draggingSectionOffsetX { 0.0f };
    int pressedFxSection { -1 };
    juce::Point<int> fxDragStartPoint;
    bool fxDragHasMoved { false };

    juce::Slider oscSineKnob;
    juce::Slider oscSawKnob;
    juce::Slider oscSquareKnob;
    juce::ComboBox oscModeBox;
    KnobLabel oscModeLabel;
    juce::ComboBox oscVowelBox;
    KnobLabel oscVowelLabel;
    juce::Slider cutoffKnob;
    juce::Slider resonanceKnob;
    juce::ComboBox filterTypeBox;
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
    KnobLabel filterTypeLabel;
    KnobLabel attackLabel;
    KnobLabel decayLabel;
    KnobLabel sustainLabel;
    KnobLabel releaseLabel;
    KnobLabel gainLabel;
    KnobLabel midiStatusLabel;

    juce::Slider robWarmthKnob;
    KnobLabel robWarmthLabel;
    juce::ComboBox robTypeBox;
    KnobLabel robTypeLabel;
    juce::Slider isaacTextureKnob;
    KnobLabel isaacTextureLabel;
    juce::ComboBox delayAlgoBox;
    KnobLabel delayAlgoLabel;
    juce::Slider delayTimeKnob;
    KnobLabel delayTimeLabel;
    juce::Slider delayFeedbackKnob;
    KnobLabel delayFeedbackLabel;
    juce::ComboBox granularSyncBox;
    KnobLabel granularSyncLabel;
    juce::Slider reverbKnob;
    KnobLabel reverbLabel;
    juce::ComboBox reverbTypeBox;
    KnobLabel reverbTypeLabel;
    juce::ToggleButton robBypassButton;
    juce::ToggleButton delayBypassButton;
    juce::ToggleButton reverbBypassButton;

    std::array<KnobBinding, 10> knobBindings {};
    int lastOscModeIndex { -1 };
};
