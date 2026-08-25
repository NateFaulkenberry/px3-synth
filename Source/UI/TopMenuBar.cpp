#include "TopMenuBar.h"

TopMenuBar::TopMenuBar()
{
    const auto setupPresetButton = [](juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGBA(40, 40, 40, 210));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(232, 232, 232));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGBA(68, 124, 180, 220));
    };

    setupPresetButton(presetPrevButton);
    setupPresetButton(presetNameButton);
    setupPresetButton(presetNextButton);
    setupPresetButton(presetMenuButton);

    presetPrevButton.setButtonText("<");
    presetNameButton.setButtonText("INIT");
    presetNextButton.setButtonText(">");
    presetMenuButton.setButtonText("MENU");

    configureTopMenuSectionButton(topMenuOscButton, "OSC", 0);
    configureTopMenuSectionButton(topMenuEnvButton, "ENV", 1);
    configureTopMenuSectionButton(topMenuFltButton, "FLT", 2);
    configureTopMenuSectionButton(topMenuFxButton, "FX", 3);
    configureTopMenuSectionButton(topMenuMixButton, "MIX", 4);

    presetPrevButton.onClick = [this]()
    {
        if (onPresetPrevious != nullptr)
        {
            onPresetPrevious();
        }
    };

    presetNextButton.onClick = [this]()
    {
        if (onPresetNext != nullptr)
        {
            onPresetNext();
        }
    };

    presetNameButton.onClick = [this]()
    {
        if (onPresetName != nullptr)
        {
            onPresetName();
        }
    };

    presetMenuButton.onClick = [this]()
    {
        if (onPresetMenu != nullptr)
        {
            onPresetMenu();
        }
    };

    addAndMakeVisible(presetPrevButton);
    addAndMakeVisible(presetNameButton);
    addAndMakeVisible(presetNextButton);
    addAndMakeVisible(presetMenuButton);
    addAndMakeVisible(topMenuOscButton);
    addAndMakeVisible(topMenuEnvButton);
    addAndMakeVisible(topMenuFltButton);
    addAndMakeVisible(topMenuFxButton);
    addAndMakeVisible(topMenuMixButton);
}

void TopMenuBar::resized()
{
    auto presetRow = getLocalBounds().withSizeKeepingCentre(getWidth(), 32);

    auto sectionButtonsRow = presetRow;
    topMenuSectionButtonsArea = sectionButtonsRow.removeFromLeft(juce::jlimit(230, 330, presetRow.getWidth() / 2));
    sectionButtonsRow.removeFromLeft(8);
    topMenuPresetClusterArea = sectionButtonsRow;

    const auto sectionGapPx = 6;
    auto sectionButtonsLayout = topMenuSectionButtonsArea.reduced(5, 0);
    const auto buttonWidth = juce::jmax(42, (sectionButtonsLayout.getWidth() - (sectionGapPx * 4)) / 5);
    for (int i = 0; i < 5; ++i)
    {
        topMenuSectionButtons[static_cast<std::size_t>(i)]->setBounds(sectionButtonsLayout.removeFromLeft(buttonWidth));
        if (i < 4)
        {
            sectionButtonsLayout.removeFromLeft(sectionGapPx);
        }
    }

    auto presetLayout = topMenuPresetClusterArea;
    auto menuSectionArea = presetLayout.removeFromRight(94);
    auto presetSelectorArea = presetLayout.reduced(5, 0);
    auto presetSelectorLayout = presetSelectorArea;
    presetPrevButton.setBounds(presetSelectorLayout.removeFromLeft(26));
    presetSelectorLayout.removeFromLeft(4);
    presetNextButton.setBounds(presetSelectorLayout.removeFromRight(26));
    presetSelectorLayout.removeFromRight(8);
    presetNameButton.setBounds(presetSelectorLayout);
    auto menuButtonBounds = menuSectionArea;
    const auto menuButtonWidth = juce::jlimit(64, 78, menuButtonBounds.getWidth() - 16);
    menuButtonBounds = juce::Rectangle<int>(menuButtonWidth, menuButtonBounds.getHeight())
                           .withCentre(menuButtonBounds.getCentre())
                           .translated(3, 0);
    presetMenuButton.setBounds(menuButtonBounds);
}

void TopMenuBar::setOnSectionSelected(std::function<void(int)> callback)
{
    onSectionSelected = std::move(callback);
}

void TopMenuBar::setOnPresetPrevious(std::function<void()> callback)
{
    onPresetPrevious = std::move(callback);
}

void TopMenuBar::setOnPresetNext(std::function<void()> callback)
{
    onPresetNext = std::move(callback);
}

void TopMenuBar::setOnPresetName(std::function<void()> callback)
{
    onPresetName = std::move(callback);
}

void TopMenuBar::setOnPresetMenu(std::function<void()> callback)
{
    onPresetMenu = std::move(callback);
}

void TopMenuBar::setSelectedSection(int sectionIndex)
{
    const auto clamped = juce::jlimit(0, 4, sectionIndex);
    for (int i = 0; i < 5; ++i)
    {
        topMenuSectionButtons[static_cast<std::size_t>(i)]->setToggleState(i == clamped, juce::dontSendNotification);
    }
}

void TopMenuBar::setPresetName(const juce::String& name)
{
    presetNameButton.setButtonText(name);
}

const juce::Rectangle<int>& TopMenuBar::getSectionButtonsArea() const
{
    return topMenuSectionButtonsArea;
}

const juce::Rectangle<int>& TopMenuBar::getPresetClusterArea() const
{
    return topMenuPresetClusterArea;
}

juce::Rectangle<int> TopMenuBar::getPresetMenuButtonBounds() const
{
    return presetMenuButton.getBounds();
}

juce::TextButton& TopMenuBar::getPresetMenuButton()
{
    return presetMenuButton;
}

void TopMenuBar::configureTopMenuSectionButton(juce::TextButton& button,
                                               const juce::String& text,
                                               int sectionIndex)
{
    button.setButtonText(text);
    button.setClickingTogglesState(false);
    button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGBA(40, 40, 40, 210));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGBA(82, 140, 196, 220));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(224, 224, 224));
    button.setColour(juce::TextButton::textColourOnId, juce::Colour::fromRGB(245, 245, 245));
    button.onClick = [this, sectionIndex]()
    {
        if (onSectionSelected != nullptr)
        {
            onSectionSelected(sectionIndex);
        }
    };
}
