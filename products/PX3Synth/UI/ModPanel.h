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
#include "ModulatorTabs.h"

class UIConfig;

class ModPanel final : public juce::Component
{
public:
    static constexpr int kLfos = PX3SynthAudioProcessor::kLfoSourceCount;
    static constexpr int kEnvs = PX3SynthAudioProcessor::kEnvelopeSourceCount;

    ModPanel(PX3SynthAudioProcessor& processorIn,
             juce::ToggleButton& lfoEnabledButton,
             juce::Slider& lfoRateKnob,
             juce::Label& lfoRateLabel,
             juce::Label& lfoRateValueLabel,
             juce::ComboBox& lfoWaveformBox,
             juce::Label& lfoWaveformLabel,
             juce::LookAndFeel* sharedLfoKnobLookAndFeel,
             juce::Colour panelAccent,
             juce::Colour lfoAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void childBoundsChanged(juce::Component* child) override;

    // When the editor's scene places the two tab panels (VOICE's "voice.mods"
    // row: "mods.lfo" and "mods.env"), this panel draws nothing of its own.
    void setSceneManaged(bool managed);
    bool isSceneManaged() const noexcept { return sceneManaged; }
    // 0..3 = LFO 1-4, 4..7 = ENV 1-4 (graph source order).
    juce::Component* getCard(int index);
    // LFO 1-4 and ENV 1-4, one card showing at a time under a strip of tabs;
    // each tab carries its source's patch-bay jack.
    ModulatorTabs& getLfoTabs() noexcept { return lfoTabs; }
    ModulatorTabs& getEnvTabs() noexcept { return envTabs; }
    // The jacks live on the tabs now, which place them; kept so callers that
    // relayout after a resize need not know that.
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
    // Source jacks on the LFO 1-4 and ENV 1-4 tabs (graph sources 0..7).
    void attachModSources(px3::ui::modrouting::ModDragController& controller);
    void refreshModSources(const std::vector<px3::ui::modrouting::RouteInfo>& routes);
    // For the tests: ENV n's LOOP switch and KEY knob.
    juce::Button* getEnvelopeLoopButton(int env) { return juce::isPositiveAndBelow(env, kEnvs) ? &envelopes[static_cast<std::size_t>(env)].loopButton : nullptr; }
    juce::Button* getEnvelopeSyncButton(int env) { return juce::isPositiveAndBelow(env, kEnvs) ? &envelopes[static_cast<std::size_t>(env)].syncButton : nullptr; }
    juce::Slider* getEnvelopeKeyKnob(int env) { return juce::isPositiveAndBelow(env, kEnvs) ? &envelopes[static_cast<std::size_t>(env)].keyKnob : nullptr; }
    px3::ui::modrouting::ModSourceSocket* getCardSocket(int source)
    { return juce::isPositiveAndBelow(source, static_cast<int>(cardSockets.size())) ? cardSockets[static_cast<std::size_t>(source)].get() : nullptr; }

private:
    struct LfoBundle
    {
        px3::ui::BypassButton enabledButton;
        juce::Slider rateKnob;
        px3::ui::ChipLabel rateLabel;
        juce::Label rateValueLabel;
        juce::ComboBox waveformBox;
        px3::ui::ChipLabel waveformLabel;
        std::unique_ptr<juce::ButtonParameterAttachment> enabledAttachment;
        std::unique_ptr<juce::SliderParameterAttachment> rateAttachment;
        std::unique_ptr<juce::ComboBoxParameterAttachment> waveformAttachment;
        std::unique_ptr<LfoComponent> component;
    };

    struct EnvBundle
    {
        px3::ui::BypassButton enabledButton;
        std::unique_ptr<juce::ButtonParameterAttachment> enabledAttachment;
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
    std::array<LfoBundle, PX3SynthAudioProcessor::kLfoSourceCount - 1> extraLfos;   // LFO 2 onward
    std::array<EnvBundle, PX3SynthAudioProcessor::kEnvelopeSourceCount> envelopes;

    juce::Colour accent;
    juce::Colour lfoHeaderAccent;
    juce::LookAndFeel* lfoKnobLookAndFeel { nullptr };
    std::shared_ptr<const UIConfig> uiConfig;
    std::array<std::unique_ptr<px3::ui::modrouting::ModSourceSocket>, kLfos + kEnvs> cardSockets;
    // The cards are children of these, not of the panel.
    ModulatorTabs lfoTabs { "LFO", juce::Colour(0xff9f7aea) };
    ModulatorTabs envTabs { "ENV", juce::Colour(0xfff2c14e) };
    bool sceneManaged { false };
};