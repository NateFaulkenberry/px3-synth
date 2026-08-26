#include "TopMenuBar.h"

#include "UIConfig.h"

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
    configureTopMenuSectionButton(topMenuModButton, "MOD", 1);
    configureTopMenuSectionButton(topMenuAmpButton, "AMP", 2);
    configureTopMenuSectionButton(topMenuFltButton, "FLT", 3);
    configureTopMenuSectionButton(topMenuFxButton, "FX", 4);
    configureTopMenuSectionButton(topMenuMixButton, "MIX", 5);

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
    addAndMakeVisible(topMenuModButton);
    addAndMakeVisible(topMenuAmpButton);
    addAndMakeVisible(topMenuFltButton);
    addAndMakeVisible(topMenuFxButton);
    addAndMakeVisible(topMenuMixButton);
}

void TopMenuBar::resized()
{
    const auto rowHeight = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.rowHeight", 32) : 32;
    const auto sectionGapPx = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.sectionGap", 6) : 6;
    const auto sectionMinWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.sectionMinWidth", 282) : 282;
    const auto sectionMaxWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.sectionMaxWidth", 390) : 390;
    const auto presetClusterRightWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.presetClusterMenuWidth", 94) : 94;
    const auto presetPrevNextWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.prevNextWidth", 26) : 26;
    const auto presetHorizontalPad = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.presetHorizontalPad", 5) : 5;
    const auto presetSmallGap = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.presetSmallGap", 4) : 4;
    const auto menuButtonMinWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.menuButtonMinWidth", 64) : 64;
    const auto menuButtonMaxWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.menuButtonMaxWidth", 78) : 78;
    const auto menuButtonInset = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.menuButtonInset", 16) : 16;

    auto presetRow = getLocalBounds().withSizeKeepingCentre(getWidth(), rowHeight);

    auto sectionButtonsRow = presetRow;
    topMenuSectionButtonsArea = sectionButtonsRow.removeFromLeft(juce::jlimit(sectionMinWidth,
                                                                               sectionMaxWidth,
                                                                               presetRow.getWidth() / 2));
    sectionButtonsRow.removeFromLeft(8);
    topMenuPresetClusterArea = sectionButtonsRow;

    auto sectionButtonsLayout = topMenuSectionButtonsArea.reduced(5, 0);
    const auto buttonWidth = juce::jmax(42, (sectionButtonsLayout.getWidth() - (sectionGapPx * 5)) / 6);
    for (int i = 0; i < 6; ++i)
    {
        topMenuSectionButtons[static_cast<std::size_t>(i)]->setBounds(sectionButtonsLayout.removeFromLeft(buttonWidth));
        if (i < 5)
        {
            sectionButtonsLayout.removeFromLeft(sectionGapPx);
        }
    }

    auto presetLayout = topMenuPresetClusterArea;
    auto menuSectionArea = presetLayout.removeFromRight(presetClusterRightWidth);
    auto presetSelectorArea = presetLayout.reduced(presetHorizontalPad, 0);
    auto presetSelectorLayout = presetSelectorArea;
    presetPrevButton.setBounds(presetSelectorLayout.removeFromLeft(presetPrevNextWidth));
    presetSelectorLayout.removeFromLeft(presetSmallGap);
    presetNextButton.setBounds(presetSelectorLayout.removeFromRight(presetPrevNextWidth));
    presetSelectorLayout.removeFromRight(8);
    presetNameButton.setBounds(presetSelectorLayout);
    auto menuButtonBounds = menuSectionArea;
    const auto menuButtonWidth = juce::jlimit(menuButtonMinWidth, menuButtonMaxWidth, menuButtonBounds.getWidth() - menuButtonInset);
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
    const auto clamped = juce::jlimit(0, 5, sectionIndex);
    for (int i = 0; i < 6; ++i)
    {
        topMenuSectionButtons[static_cast<std::size_t>(i)]->setToggleState(i == clamped, juce::dontSendNotification);
    }
}

void TopMenuBar::setPresetName(const juce::String& name)
{
    presetNameButton.setButtonText(name);
}

void TopMenuBar::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    const auto applyStyle = [this](juce::TextButton& button,
                                   const juce::String& defaultsPath,
                                   const juce::String& overridePath)
    {
        if (uiConfig == nullptr)
        {
            return;
        }

        const auto style = uiConfig->mergedObject(defaultsPath, overridePath);
        uiConfig->applyTextButtonStyle(style, button);
    };

    applyStyle(presetPrevButton, "styles.buttons.topMenuPreset", "topMenu.buttons.presetPrev");
    applyStyle(presetNameButton, "styles.buttons.topMenuPreset", "topMenu.buttons.presetName");
    applyStyle(presetNextButton, "styles.buttons.topMenuPreset", "topMenu.buttons.presetNext");
    applyStyle(presetMenuButton, "styles.buttons.topMenuPreset", "topMenu.buttons.presetMenu");
    applyStyle(topMenuOscButton, "styles.buttons.topMenuSection", "topMenu.buttons.sectionOsc");
    applyStyle(topMenuModButton, "styles.buttons.topMenuSection", "topMenu.buttons.sectionMod");
    applyStyle(topMenuAmpButton, "styles.buttons.topMenuSection", "topMenu.buttons.sectionAmp");
    applyStyle(topMenuFltButton, "styles.buttons.topMenuSection", "topMenu.buttons.sectionFlt");
    applyStyle(topMenuFxButton, "styles.buttons.topMenuSection", "topMenu.buttons.sectionFx");
    applyStyle(topMenuMixButton, "styles.buttons.topMenuSection", "topMenu.buttons.sectionMix");

    resized();
    repaint();
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
