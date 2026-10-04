#pragma once

#include <JuceHeader.h>

#include "BypassButton.h"
#include "MixerControls.h"
#include "ChipLabel.h"
#include "ToggleChipButton.h"

#include <array>
#include <memory>

#include "EnvelopeComponent.h"
#include "LfoComponent.h"
#include "PluginProcessor.h"
#include "ModRouting.h"

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
    void childBoundsChanged(juce::Component* child) override;

    // When the editor's scene places the six cards (VOICE's "voice.mods" row),
    // this panel only keeps each card's source jack in its title band.
    void setSceneManaged(bool managed);
    bool isSceneManaged() const noexcept { return sceneManaged; }
    // 0..2 = LFO 1-3, 3..5 = ENV 1-3 (graph source order).
    juce::Component* getCard(int index);
    void layoutSockets();

    void refreshFromParameters();
    // No arguments: it reads the parameters itself. It used to take three that
    // it ignored while reading the same values from the processor, so the
    // caller computed and passed state that was thrown away.
    void refreshLfoFromParameters();
    void advanceAnimation(float lfoDeltaSeconds);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    int getPreferredContentWidth() const;
    int getPreferredContentHeight() const;
    // Source jacks on the LFO 1-3 and ENV 1-3 cards (graph sources 0..5).
    void attachModSources(px3::ui::modrouting::ModDragController& controller);
    void refreshModSources(const std::vector<px3::ui::modrouting::RouteInfo>& routes);
    // For the tests: ENV n's LOOP switch and KEY knob.
    juce::Button* getEnvelopeLoopButton(int env) { return juce::isPositiveAndBelow(env, 3) ? &envelopes[static_cast<std::size_t>(env)].loopButton : nullptr; }
    juce::Button* getEnvelopeSyncButton(int env) { return juce::isPositiveAndBelow(env, 3) ? &envelopes[static_cast<std::size_t>(env)].syncButton : nullptr; }
    juce::Slider* getEnvelopeKeyKnob(int env) { return juce::isPositiveAndBelow(env, 3) ? &envelopes[static_cast<std::size_t>(env)].keyKnob : nullptr; }
    px3::ui::modrouting::ModSourceSocket* getCardSocket(int source)
    { return juce::isPositiveAndBelow(source, 6) ? cardSockets[static_cast<std::size_t>(source)].get() : nullptr; }

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
        // LOOP (mod.envN.loop) and KEY (mod.envN.keytrack).
        px3::ui::ToggleChipButton loopButton;
        px3::ui::ToggleChipButton syncButton;
        std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment;
        juce::Slider keyKnob;
        px3::ui::ChipLabel keyLabel;
        juce::Label keyValueLabel;
        std::unique_ptr<juce::ButtonParameterAttachment> loopAttachment;
        std::unique_ptr<juce::SliderParameterAttachment> keyAttachment;
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
    std::array<std::unique_ptr<px3::ui::modrouting::ModSourceSocket>, 6> cardSockets;
    bool sceneManaged { false };
    bool placingSockets { false };
};