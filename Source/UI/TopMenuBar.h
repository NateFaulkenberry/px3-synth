#pragma once

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>

class UIConfig;

class TopMenuBar final : public juce::Component
{
public:
    TopMenuBar();

    void resized() override;

    void setOnSectionSelected(std::function<void(int)> callback);
    void setOnPresetPrevious(std::function<void()> callback);
    void setOnPresetNext(std::function<void()> callback);
    void setOnPresetName(std::function<void()> callback);
    void setOnPresetMenu(std::function<void()> callback);

    void setSelectedSection(int sectionIndex);
    void setPresetName(const juce::String& name);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    const juce::Rectangle<int>& getSectionButtonsArea() const;
    const juce::Rectangle<int>& getPresetClusterArea() const;
    juce::Rectangle<int> getPresetMenuButtonBounds() const;
    juce::TextButton& getPresetMenuButton();

private:
    void configureTopMenuSectionButton(juce::TextButton& button,
                                       const juce::String& text,
                                       int sectionIndex);

    juce::TextButton presetPrevButton;
    juce::TextButton presetNameButton;
    juce::TextButton presetNextButton;
    juce::TextButton presetMenuButton;
    juce::TextButton topMenuOscButton;
    juce::TextButton topMenuModButton;
    juce::TextButton topMenuFltButton;
    juce::TextButton topMenuFxButton;
    juce::TextButton topMenuMixButton;

    std::array<juce::TextButton*, 5> topMenuSectionButtons {
        { &topMenuOscButton, &topMenuModButton, &topMenuFltButton, &topMenuFxButton, &topMenuMixButton }
    };

    juce::Rectangle<int> topMenuSectionButtonsArea;
    juce::Rectangle<int> topMenuPresetClusterArea;

    std::function<void(int)> onSectionSelected;
    std::function<void()> onPresetPrevious;
    std::function<void()> onPresetNext;
    std::function<void()> onPresetName;
    std::function<void()> onPresetMenu;

    std::shared_ptr<const UIConfig> uiConfig;
};
