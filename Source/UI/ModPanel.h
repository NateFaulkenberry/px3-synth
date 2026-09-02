#pragma once

#include <JuceHeader.h>

#include "BypassButton.h"
#include "MixerControls.h"
#include "ChipLabel.h"

#include <array>
#include <memory>

#include "EnvelopeComponent.h"
#include "LfoComponent.h"
#include "PluginProcessor.h"

class UIConfig;

class ModPanel final : public juce::Component
{
public:
    ModPanel(PX3SynthAudioProcessor& processorIn,
             juce::ToggleButton& lfoEnabledButton,
             juce::Label& lfoAssignLabel,
             juce::ComboBox& lfoAssignBox,
             juce::Slider& lfoRateKnob,
             juce::Label& lfoRateLabel,
             juce::Label& lfoRateValueLabel,
             juce::Slider& lfoAmountKnob,
             juce::Label& lfoAmountLabel,
             juce::Label& lfoAmountValueLabel,
             juce::ComboBox& lfoWaveformBox,
             juce::Label& lfoWaveformLabel,
             juce::LookAndFeel* sharedLfoKnobLookAndFeel,
             juce::Colour panelAccent,
             juce::Colour lfoAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();
    // No arguments: it reads the parameters itself. It used to take three that
    // it ignored while reading the same values from the processor, so the
    // caller computed and passed state that was thrown away.
    void refreshLfoFromParameters();
    void advanceAnimation(float lfoDeltaSeconds);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    int getPreferredContentWidth() const;
    int getPreferredContentHeight() const;

private:
    struct LfoBundle
    {
        px3::ui::BypassButton enabledButton;
        px3::ui::ChipLabel assignLabel;
        juce::ComboBox assignBox;
        juce::Slider rateKnob;
        px3::ui::ChipLabel rateLabel;
        juce::Label rateValueLabel;
        PanKnob amountKnob;
        px3::ui::ChipLabel amountLabel;
        juce::Label amountValueLabel;
        juce::ComboBox waveformBox;
        px3::ui::ChipLabel waveformLabel;
        // The assignment last written to assignBox. Refresh runs at 30 Hz, and
        // writing a combo box unconditionally from a timer is only harmless for
        // as long as nothing downstream reacts to the write - which is not a
        // property worth relying on.
        int lastAssignmentIndex { -1 };
        std::unique_ptr<juce::ButtonParameterAttachment> enabledAttachment;
        std::unique_ptr<juce::SliderParameterAttachment> rateAttachment;
        std::unique_ptr<juce::SliderParameterAttachment> amountAttachment;
        std::unique_ptr<juce::ComboBoxParameterAttachment> waveformAttachment;
        std::unique_ptr<LfoComponent> component;
    };

    struct EnvBundle
    {
        px3::ui::BypassButton enabledButton;
        px3::ui::ChipLabel assignLabel;
        juce::ComboBox assignBox;
        PanKnob amountKnob;
        px3::ui::ChipLabel amountLabel;
        juce::Label amountValueLabel;
        // The assignment last written to assignBox. Refresh runs at 30 Hz, and
        // writing a combo box unconditionally from a timer is only harmless for
        // as long as nothing downstream reacts to the write - which is not a
        // property worth relying on.
        int lastAssignmentIndex { -1 };
        std::unique_ptr<juce::ButtonParameterAttachment> enabledAttachment;
        std::unique_ptr<juce::SliderParameterAttachment> amountAttachment;
        std::unique_ptr<EnvelopeComponent> component;
    };

    void configureOwnedLfoBundle(int lfoIndex, LfoBundle& bundle);
    void configureOwnedEnvBundle(int envIndex, EnvBundle& bundle);

    PX3SynthAudioProcessor& processor;
    std::unique_ptr<LfoComponent> lfoComponent;
    std::array<LfoBundle, 2> extraLfos;
    std::array<EnvBundle, PX3SynthAudioProcessor::kEnvelopeSourceCount> envelopes;

    juce::Colour accent;
    juce::Colour lfoHeaderAccent;
    juce::LookAndFeel* lfoKnobLookAndFeel { nullptr };
    std::shared_ptr<const UIConfig> uiConfig;
};