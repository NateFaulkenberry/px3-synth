#include "OscPanel.h"

#include "SceneBinding.h"

#include "UIConfig.h"

#include <cmath>

OscPanel::OscPanel(juce::ToggleButton& subEnabledButton,
                   TuningControls& subTuning,
                   juce::ComboBox& subWaveformBox,
                   juce::Label& subWaveformLabel,
                   TuningControls& osc1Tuning,
                   juce::Slider& osc1MacroA,
                   juce::Slider& osc1MacroB,
                   juce::Slider& osc1MacroC,
                   juce::ToggleButton& osc1EnabledButton,
                   juce::Label& osc1MacroALabel,
                   juce::Label& osc1MacroBLabel,
                   juce::Label& osc1MacroCLabel,
                   juce::Label& osc1MacroAValueLabel,
                   juce::Label& osc1MacroBValueLabel,
                   juce::Label& osc1MacroCValueLabel,
                   juce::ComboBox& osc1ModeBox,
                   juce::Label& osc1ModeLabel,
                   juce::ComboBox& osc1VowelBox,
                   juce::Label& osc1VowelLabel,
                   TuningControls& osc2Tuning,
                   juce::Slider& osc2MacroA,
                   juce::Slider& osc2MacroB,
                   juce::Slider& osc2MacroC,
                   juce::ToggleButton& osc2EnabledButton,
                   juce::Label& osc2MacroALabel,
                   juce::Label& osc2MacroBLabel,
                   juce::Label& osc2MacroCLabel,
                   juce::Label& osc2MacroAValueLabel,
                   juce::Label& osc2MacroBValueLabel,
                   juce::Label& osc2MacroCValueLabel,
                   juce::ComboBox& osc2ModeBox,
                   juce::Label& osc2ModeLabel,
                   juce::ComboBox& osc2VowelBox,
                   juce::Label& osc2VowelLabel,
                   TuningControls& osc3Tuning,
                   juce::Slider& osc3MacroA,
                   juce::Slider& osc3MacroB,
                   juce::Slider& osc3MacroC,
                   juce::ToggleButton& osc3EnabledButton,
                   juce::Label& osc3MacroALabel,
                   juce::Label& osc3MacroBLabel,
                   juce::Label& osc3MacroCLabel,
                   juce::Label& osc3MacroAValueLabel,
                   juce::Label& osc3MacroBValueLabel,
                   juce::Label& osc3MacroCValueLabel,
                   juce::ComboBox& osc3ModeBox,
                   juce::Label& osc3ModeLabel,
                   juce::ComboBox& osc3VowelBox,
                   juce::Label& osc3VowelLabel,
                   juce::Colour subAccent,
                                     juce::Colour oscAccent)
        : accent(oscAccent),
          subHeaderAccent(subAccent),
                    oscHeaderAccent(oscAccent)
{
    subOscComponent = std::make_unique<SubOscComponent>(subEnabledButton,
                                                        subTuning,
                                                        subWaveformBox,
                                                        subWaveformLabel,
                                                        subAccent);
    addAndMakeVisible(*subOscComponent);

    oscillatorComponents[0] = std::make_unique<OscillatorComponent>(osc1EnabledButton,
                                                                     osc1Tuning,
                                                                     osc1MacroA,
                                                                     osc1MacroB,
                                                                     osc1MacroC,
                                                                     osc1MacroALabel,
                                                                     osc1MacroBLabel,
                                                                     osc1MacroCLabel,
                                                                     osc1MacroAValueLabel,
                                                                     osc1MacroBValueLabel,
                                                                     osc1MacroCValueLabel,
                                                                     osc1ModeBox,
                                                                     osc1ModeLabel,
                                                                     osc1VowelBox,
                                                                     osc1VowelLabel,
                                                                     oscAccent);
    oscillatorComponents[1] = std::make_unique<OscillatorComponent>(osc2EnabledButton,
                                                                     osc2Tuning,
                                                                     osc2MacroA,
                                                                     osc2MacroB,
                                                                     osc2MacroC,
                                                                     osc2MacroALabel,
                                                                     osc2MacroBLabel,
                                                                     osc2MacroCLabel,
                                                                     osc2MacroAValueLabel,
                                                                     osc2MacroBValueLabel,
                                                                     osc2MacroCValueLabel,
                                                                     osc2ModeBox,
                                                                     osc2ModeLabel,
                                                                     osc2VowelBox,
                                                                     osc2VowelLabel,
                                                                     oscAccent);
    oscillatorComponents[2] = std::make_unique<OscillatorComponent>(osc3EnabledButton,
                                                                     osc3Tuning,
                                                                     osc3MacroA,
                                                                     osc3MacroB,
                                                                     osc3MacroC,
                                                                     osc3MacroALabel,
                                                                     osc3MacroBLabel,
                                                                     osc3MacroCLabel,
                                                                     osc3MacroAValueLabel,
                                                                     osc3MacroBValueLabel,
                                                                     osc3MacroCValueLabel,
                                                                     osc3ModeBox,
                                                                     osc3ModeLabel,
                                                                     osc3VowelBox,
                                                                     osc3VowelLabel,
                                                                     oscAccent);

    for (auto& oscillatorComponent : oscillatorComponents)
    {
        if (oscillatorComponent != nullptr)
        {
            addAndMakeVisible(*oscillatorComponent);
        }
    }
}

void OscPanel::paint(juce::Graphics& g)
{
    if (hasSceneStyle)
    {
        const auto area = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(sceneBackground);
        g.fillRoundedRectangle(area, sceneCornerRadius);
        g.setColour(accent);
        g.drawRoundedRectangle(area, sceneCornerRadius, sceneBorderWidth);
        return;
    }

    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("osc.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("osc.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto panelRadius = uiConfig != nullptr ? uiConfig->getFloat("osc.panel.cornerRadius", 10.0f) : 10.0f;

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);

    // Card titles are drawn by the cards themselves - see px3::ui::drawCard.
    // Painting them here meant the panel wrote into its children's bounds,
    // which is how a title could survive the component it belonged to.
}

void OscPanel::setSceneStyle(juce::Colour background,
                             juce::Colour foreground,
                             juce::Colour accentIn,
                             float cornerRadius,
                             float borderWidth)
{
    sceneBackground = background;
    sceneForeground = foreground;
    accent = accentIn;
    sceneCornerRadius = juce::jmax(0.0f, cornerRadius);
    sceneBorderWidth = juce::jmax(0.0f, borderWidth);
    hasSceneStyle = true;

    if (subOscComponent != nullptr)
    {
        subOscComponent->setAccentColour(accentIn);
        subOscComponent->setHardwareFaceplate(true);
    }
    for (auto& oscillatorComponent : oscillatorComponents)
    {
        if (oscillatorComponent != nullptr)
        {
            oscillatorComponent->setAccentColour(accentIn);
            oscillatorComponent->setHardwareFaceplate(true);
        }
    }

    const auto tint = [foreground, accentIn](auto&& self, juce::Component& component) -> void
    {
        if (dynamic_cast<juce::Label*>(&component) != nullptr)
        {
            component.setColour(juce::Label::textColourId, foreground);
        }
        if (dynamic_cast<juce::ComboBox*>(&component) != nullptr)
        {
            component.setColour(juce::ComboBox::textColourId, foreground);
        }
        if (auto* slider = dynamic_cast<juce::Slider*>(&component))
        {
            slider->setColour(juce::Slider::rotarySliderFillColourId, accentIn);
        }
        for (int i = 0; i < component.getNumChildComponents(); ++i)
        {
            self(self, *component.getChildComponent(i));
        }
    };
    tint(tint, *this);
    repaint();
}

void OscPanel::resized()
{
    // The four cards are placed by the instrument scene (primary.osc / osc.* in
    // InstrumentScene.json). What stays here is what each card is TOLD: its
    // index, and the box its percentage dimensions resolve against.
    const auto panelArea = getLocalBounds();
    if (subOscComponent != nullptr)
    {
        subOscComponent->setPanelContentBounds(panelArea);
    }
    for (int oscIndex = 0; oscIndex < static_cast<int>(oscillatorComponents.size()); ++oscIndex)
    {
        if (auto* component = oscillatorComponents[static_cast<std::size_t>(oscIndex)].get())
        {
            component->setInstanceIndex(oscIndex + 1);
            component->setPanelContentBounds(panelArea);
        }
    }
    px3::ui::requestSceneLayout(*this);
}

juce::Component* OscPanel::getCard(int index) const noexcept
{
    if (index == 0) { return subOscComponent.get(); }
    if (index >= 1 && index <= 3) { return oscillatorComponents[static_cast<std::size_t>(index - 1)].get(); }
    return nullptr;
}

void OscPanel::refreshOscillatorFromParameters(int oscIndex, bool enabled, int modeIndex, int vowelIndex)
{
    const auto idx = juce::jlimit(0, 2, oscIndex);
    auto& oscillatorComponent = oscillatorComponents[static_cast<std::size_t>(idx)];
    if (oscillatorComponent != nullptr)
    {
        oscillatorComponent->refreshFromParameters(enabled, modeIndex, vowelIndex);
    }
}

void OscPanel::setWavetableControls(int oscIndex,
                                    juce::ComboBox& tableBox,
                                    juce::Label& tableLabel,
                                    juce::Slider& positionSlider,
                                    juce::Label& positionLabel,
                                    juce::Label& positionValue)
{
    const auto idx = juce::jlimit(0, 2, oscIndex);
    if (auto& component = oscillatorComponents[static_cast<std::size_t>(idx)])
    {
        component->setWavetableControls(tableBox, tableLabel, positionSlider,
                                        positionLabel, positionValue);
    }
}

WavetableGraph* OscPanel::getWavetableGraph(int oscIndex)
{
    const auto idx = juce::jlimit(0, 2, oscIndex);
    if (auto& component = oscillatorComponents[static_cast<std::size_t>(idx)])
    {
        return &component->getWavetableGraph();
    }
    return nullptr;
}

void OscPanel::refreshSubOscFromParameters(bool enabled, int waveformIndex)
{
    if (subOscComponent != nullptr)
    {
        subOscComponent->refreshFromParameters(enabled, waveformIndex);
    }
}

void OscPanel::advanceAnimation(float oscDeltaPhase)
{
    for (auto& oscillatorComponent : oscillatorComponents)
    {
        if (oscillatorComponent != nullptr)
        {
            oscillatorComponent->advanceAnimation(oscDeltaPhase);
        }
    }

    if (subOscComponent != nullptr)
    {
        subOscComponent->advanceAnimation(oscDeltaPhase);
    }
}

void OscPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (subOscComponent != nullptr)
    {
        subOscComponent->setUIConfig(uiConfig);
    }
    for (auto& oscillatorComponent : oscillatorComponents)
    {
        if (oscillatorComponent != nullptr)
        {
            oscillatorComponent->setUIConfig(uiConfig);
        }
    }
    repaint();
}
