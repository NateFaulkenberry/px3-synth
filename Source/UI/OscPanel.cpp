#include "OscPanel.h"

#include "CardInner.h"

#include "UIConfig.h"

#include <cmath>

OscPanel::OscPanel(juce::ToggleButton& subEnabledButton,
                   juce::Slider& subPitchKnob,
                   juce::Label& subPitchLabel,
                   juce::Label& subPitchValueLabel,
                   juce::ComboBox& subOctaveBox,
                   juce::Label& subOctaveLabel,
                   juce::ComboBox& subWaveformBox,
                   juce::Label& subWaveformLabel,
                   juce::Slider& osc1PitchKnob,
                   juce::Label& osc1PitchLabel,
                   juce::Label& osc1PitchValueLabel,
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
                   juce::Slider& osc2PitchKnob,
                   juce::Label& osc2PitchLabel,
                   juce::Label& osc2PitchValueLabel,
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
                   juce::Slider& osc3PitchKnob,
                   juce::Label& osc3PitchLabel,
                   juce::Label& osc3PitchValueLabel,
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
                                                        subPitchKnob,
                                                        subPitchLabel,
                                                        subPitchValueLabel,
                                                        subOctaveBox,
                                                        subOctaveLabel,
                                                        subWaveformBox,
                                                        subWaveformLabel,
                                                        subAccent);
    addAndMakeVisible(*subOscComponent);

    oscillatorComponents[0] = std::make_unique<OscillatorComponent>(osc1EnabledButton,
                                                                     osc1PitchKnob,
                                                                     osc1PitchLabel,
                                                                     osc1PitchValueLabel,
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
                                                                     osc2PitchKnob,
                                                                     osc2PitchLabel,
                                                                     osc2PitchValueLabel,
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
                                                                     osc3PitchKnob,
                                                                     osc3PitchLabel,
                                                                     osc3PitchValueLabel,
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

void OscPanel::resized()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("osc.panel.layout.padX", 12) : 12;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("osc.panel.layout.padY", 10) : 10;
    auto panelArea = getLocalBounds().reduced(padX, padY);

    // Laid out by FlexBox rather than by hand. The old arithmetic divided the
    // panel into FIVE columns for four cards - the fifth was dead space on the
    // right - and each card then narrowed itself again with its own `width`.
    // One row of four items replaces both.
    const px3::ui::FlexStyle fallback { true,
                                        px3::ui::FlexDirection::row,
                                        px3::ui::FlexWrapMode::noWrap,
                                        px3::ui::JustifyContent::spaceBetween,
                                        px3::ui::AlignItems::stretch,
                                        px3::ui::AlignItems::centre,
                                        8.0f };
    const auto flexStyle = px3::ui::FlexStyle::readLayered(uiConfig.get(), { "panels.osc.flex" }, fallback);

    constexpr int cardCount = 4;
    const auto gapMargin = flexStyle.gapMargin();
    const auto gapWidth = gapMargin.left + gapMargin.right;

    // Each card wants `itemWidth`, capped to an equal share so they still fit
    // when the window is narrow. On a wide window they stay at their preferred
    // width and justifyContent decides what happens to the slack - which is
    // what makes `space-between` mean something here.
    const auto preferred = uiConfig != nullptr ? uiConfig->getFloat("panels.osc.itemWidth", 300.0f) : 300.0f;
    const auto share = juce::jmax(1.0f, (static_cast<float>(panelArea.getWidth())
                                         - gapWidth * static_cast<float>(cardCount))
                                            / static_cast<float>(cardCount));
    const auto itemWidth = juce::jmin(juce::jmax(1.0f, preferred), share);

    auto box = flexStyle.toFlexBox();
    for (int i = 0; i < cardCount; ++i)
    {
        box.items.add(juce::FlexItem(itemWidth, static_cast<float>(panelArea.getHeight()))
                          .withMargin(gapMargin));
    }
    box.performLayout(panelArea.toFloat());

    const auto slot = [&box](int i) { return box.items.getReference(i).currentBounds.toNearestInt(); };

    // Percentage card dimensions resolve against this box, so every card is
    // told what it is. Without it a card would have to guess, and "50%" would
    // quietly mean something different in each column.
    if (subOscComponent != nullptr)
    {
        subOscComponent->setPanelContentBounds(panelArea);
        subOscComponent->setBounds(slot(0));
    }

    for (int oscIndex = 0; oscIndex < static_cast<int>(oscillatorComponents.size()); ++oscIndex)
    {
        if (auto* component = oscillatorComponents[static_cast<std::size_t>(oscIndex)].get())
        {
            component->setInstanceIndex(oscIndex + 1);
            component->setPanelContentBounds(panelArea);
            component->setBounds(slot(oscIndex + 1));
        }
    }
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

void OscPanel::refreshSubOscFromParameters(bool enabled, int octaveIndex, int waveformIndex)
{
    if (subOscComponent != nullptr)
    {
        subOscComponent->refreshFromParameters(enabled, octaveIndex, waveformIndex);
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
