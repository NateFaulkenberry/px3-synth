#include "TopMenuBar.h"

#include "CardInner.h"

#include "UIConfig.h"

TopMenuTabButton::TopMenuTabButton(const juce::String& name)
    : juce::TextButton(name)
{
    setName(name);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void TopMenuTabButton::setAccentColour(juce::Colour colour)
{
    accent = colour;
    repaint();
}

void TopMenuTabButton::setShowSeam(bool shouldShow)
{
    showSeam = shouldShow;
    repaint();
}

void TopMenuTabButton::applyStyle(const Style& styleIn)
{
    style = styleIn;
    repaint();
}

void TopMenuTabButton::setShowLed(bool shouldShow)
{
    showLed = shouldShow;
    repaint();
}

void TopMenuTabButton::setSubtitles(const juce::String& left, const juce::String& right)
{
    if (subtitleLeft == left && subtitleRight == right)
    {
        return;
    }

    subtitleLeft = left;
    subtitleRight = right;
    repaint();
}

void TopMenuTabButton::setAlwaysActiveText(bool shouldBeActive)
{
    if (alwaysActiveText == shouldBeActive)
    {
        return;
    }

    alwaysActiveText = shouldBeActive;
    repaint();
}

void TopMenuTabButton::paintButton(juce::Graphics& g,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
{
    // A flush panel, not a rounded button: square corners and a hairline seam
    // are what let six of these read as one strip rather than six controls.
    const auto area = getLocalBounds().toFloat();
    const auto on = getToggleState();

    auto face = on ? style.faceActive : style.face;
    if (shouldDrawButtonAsDown)
    {
        face = face.darker(0.18f);
    }
    else if (shouldDrawButtonAsHighlighted)
    {
        face = face.brighter(0.14f);
    }

    // Flat, not graded: the tabs sit on the bar's own colour, so any vertical
    // ramp here reads as a button laid on top of the bar rather than as part
    // of it. The LED, the lit top edge and the brighter inset carry state.
    g.setColour(face);
    g.fillRect(area);

    // A lit top edge marks the selected tab, the way a selected panel does.
    if (on)
    {
        g.setColour(accent);
        g.fillRect(area.withHeight(2.0f));
    }

    // A hairline one pixel in from the edge. Inset rather than on the boundary
    // so butted neighbours do not draw two lines against each other.
    g.setColour(on ? style.insetActive : style.inset);
    g.drawRect(area.reduced(1.0f), 1.0f);

    // ---- LED ---------------------------------------------------------------
    const auto ledDiameter = juce::jmin(7.0f, area.getHeight() * 0.16f);
    const auto led = juce::Rectangle<float>(ledDiameter, ledDiameter)
                         .withCentre({ area.getCentreX(), area.getY() + area.getHeight() * 0.34f });

    if (showLed)
    {
        g.setColour(juce::Colour::fromRGBA(0, 0, 0, 190));
        g.fillEllipse(led.expanded(1.4f));

        if (on)
        {
            g.setColour(accent.withAlpha(0.35f));
            g.fillEllipse(led.expanded(3.0f));
            g.setColour(accent.brighter(0.35f));
        }
        else
        {
            g.setColour(accent.withMultipliedAlpha(0.18f));
        }
        g.fillEllipse(led);
    }

    // ---- legend ------------------------------------------------------------
    const auto legend = (on || alwaysActiveText) ? style.textActive : style.text;

    // With no lamp above it the legend centres in the whole face instead of the
    // band beneath one.
    auto legendArea = showLed ? area.withTop(led.getBottom() + 2.0f) : area;

    // A subtitle takes the bottom of the face, and the legend centres in what
    // is left - so a tab without one is laid out exactly as it was.
    const auto hasSubtitle = subtitleLeft.isNotEmpty() || subtitleRight.isNotEmpty();

    // Both fonts are sized from their OWN band when there are two of them.
    // Sizing them from the whole face is what made the first attempt
    // unreadable: the tab is 32px tall, so area * 0.21 put the subtitle at
    // 6.7px and drawFittedText then shrank it further to fit. Measured from
    // its own 12px band it lands at 9px, against a 12px name.
    auto legendFontSize = juce::jmin(13.0f, area.getHeight() * 0.30f);
    juce::Rectangle<float> subtitleArea;
    auto subtitleFontSize = 0.0f;

    if (hasSubtitle)
    {
        // The name takes a band at the top and the subtitles take EVERYTHING
        // below it, down to the bottom edge, rather than a fixed strip with
        // slack under it.
        const auto nameBand = legendArea.removeFromTop(
            juce::jmin(legendArea.getHeight() * 0.58f, 19.0f));
        subtitleArea = legendArea;
        legendArea = nameBand;

        legendFontSize = juce::jmin(13.0f, nameBand.getHeight() * 0.68f);
        subtitleFontSize = juce::jmin(10.0f, subtitleArea.getHeight() * 0.66f);
    }

    g.setColour(legend);
    g.setFont(juce::FontOptions(legendFontSize, juce::Font::bold));
    g.drawFittedText(getButtonText(), legendArea.toNearestInt(), juce::Justification::centred, 1);

    if (hasSubtitle)
    {
        // The same colour as the name. Size alone carries the hierarchy - it
        // was dimmed as well, and between that and a font sized off the whole
        // 32px face the subtitles came out unreadable.
        g.setColour(legend);
        g.setFont(juce::FontOptions(subtitleFontSize));

        // Two equal columns, each centred in its own. A long category cannot
        // push the author off the right-hand edge: it is fitted into its own
        // column instead. The minimum scale is high because there is ample
        // width here - a subtitle that has to shrink much is already too small
        // to read.
        juce::FlexBox columns;
        columns.flexDirection = juce::FlexBox::Direction::row;
        columns.items.add(juce::FlexItem().withFlex(1.0f));
        columns.items.add(juce::FlexItem().withFlex(1.0f));
        columns.performLayout(subtitleArea.reduced(6.0f, 0.0f));

        g.drawFittedText(subtitleLeft.toUpperCase(),
                         columns.items.getReference(0).currentBounds.toNearestInt(),
                         juce::Justification::centred, 1, 0.9f);
        g.drawFittedText(subtitleRight.toUpperCase(),
                         columns.items.getReference(1).currentBounds.toNearestInt(),
                         juce::Justification::centred, 1, 0.9f);
    }

    // The seam between neighbours. One line on the right only, so butted tabs
    // share a single hairline instead of drawing two against each other.
    if (showSeam)
    {
        g.setColour(style.seam);
        g.fillRect(area.getRight() - 1.0f, area.getY(), 1.0f, area.getHeight());
    }
}

TopMenuBar::TopMenuBar()
{
    // The preset controls wear the same tab face as the sections, minus the
    // lamp: none of them reports an on/off state.
    for (auto* button : { &presetPrevButton, &presetNameButton, &presetNextButton, &presetMenuButton })
    {
        button->setShowLed(false);
    }

    // Butted together with no gaps, so a hairline seam is what keeps them
    // readable as separate controls. The last one has nothing to its right.
    presetPrevButton.setShowSeam(true);
    // The preset tab is showing you what is loaded, not offering an unselected
    // choice, so it wears the active text colour permanently.
    presetNameButton.setAlwaysActiveText(true);
    presetNameButton.setShowSeam(true);
    presetNextButton.setShowSeam(false);
    presetMenuButton.setShowSeam(false);

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
    // Laid out by FlexBox, reading the same style vocabulary the cards use.
    // The bar was a chain of removeFromLeft/removeFromRight calls with eleven
    // tuning numbers behind it; this keeps the numbers but lets direction,
    // justification and gaps be configured rather than implied by call order.
    using namespace px3::ui;

    const auto readFlex = [this](const char* path, const FlexStyle& fallback)
    {
        return FlexStyle::readLayered(uiConfig.get(), { juce::String(path) }, fallback);
    };

    const FlexStyle barFallback { true, FlexDirection::row, FlexWrapMode::noWrap,
                                  JustifyContent::start, AlignItems::stretch,
                                  AlignItems::centre, 8.0f };
    const auto barFlex = readFlex("topMenu.flex", barFallback);

    const auto rowHeight = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.rowHeight", 32) : 32;
    const auto sectionMinWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.sectionMinWidth", 282) : 282;
    const auto sectionMaxWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.sectionMaxWidth", 390) : 390;

    // rowHeight of 0 means "fill the bar", which is what makes the buttons take
    // the strip's full height rather than a fixed band inside it.
    auto presetRow = rowHeight > 0 ? getLocalBounds().withSizeKeepingCentre(getWidth(), rowHeight)
                                   : getLocalBounds();

    const auto sectionsWidth = juce::jlimit(sectionMinWidth, sectionMaxWidth, presetRow.getWidth() / 2);

    auto barBox = barFlex.toFlexBox();
    const auto barGap = barFlex.gapMargin();
    auto sectionsItem = juce::FlexItem(static_cast<float>(sectionsWidth),
                                       static_cast<float>(presetRow.getHeight())).withMargin(barGap);
    auto presetItem = juce::FlexItem(static_cast<float>(juce::jmax(1, presetRow.getWidth() - sectionsWidth)),
                                     static_cast<float>(presetRow.getHeight())).withMargin(barGap);
    presetItem.flexGrow = 1.0f;
    barBox.items.add(sectionsItem);
    barBox.items.add(presetItem);
    barBox.performLayout(presetRow.toFloat());

    topMenuSectionButtonsArea = barBox.items.getReference(0).currentBounds.toNearestInt();
    topMenuPresetClusterArea = barBox.items.getReference(1).currentBounds.toNearestInt();

    // ---- section buttons ---------------------------------------------------
    // Every button takes an equal share of the row and its full height: they
    // are the bar's primary navigation, so they fill it rather than floating in
    // it.
    {
        const FlexStyle fallback { true, FlexDirection::row, FlexWrapMode::noWrap,
                                   JustifyContent::spaceBetween, AlignItems::stretch,
                                   AlignItems::centre, 6.0f };
        auto box = readFlex("topMenu.sections.flex", fallback).toFlexBox();
        const auto gap = readFlex("topMenu.sections.flex", fallback).gapMargin();
        const auto row = topMenuSectionButtonsArea;
        const auto count = static_cast<int>(topMenuSectionButtons.size());

        const auto laidOutWidth = static_cast<float>(juce::jmax(1, row.getWidth())) + gap.left + gap.right;
        const std::vector<float> natural(static_cast<std::size_t>(count),
                                         laidOutWidth / static_cast<float>(juce::jmax(1, count)));
        const auto widths = fitRowItemWidths(natural, gap.left + gap.right, laidOutWidth);

        for (const auto width : widths)
        {
            auto item = juce::FlexItem(width, static_cast<float>(row.getHeight())).withMargin(gap);
            item.flexGrow = 1.0f;
            box.items.add(item);
        }
        // Laid out into a row widened by half a gap on each side, so the outer
        // half-margins fall outside it and the first and last buttons sit flush
        // with the bar's edges. Gap belongs BETWEEN buttons; without this the
        // row keeps half a gap of padding at each end and never fills.
        const auto halfGap = gap.left;
        box.performLayout(row.toFloat().expanded(halfGap, 0.0f));

        for (int i = 0; i < count; ++i)
        {
            auto* button = topMenuSectionButtons[static_cast<std::size_t>(i)];
            button->setBounds(box.items.getReference(i).currentBounds.toNearestInt());
            button->setShowSeam(i < count - 1);

            // The tab lights in the colour of the panel it opens, so the strip
            // carries the same identity language as the cards below it.
            if (uiConfig != nullptr)
            {
                const auto key = juce::String("topMenu.sections.accents.") + button->getName().toLowerCase();
                button->setAccentColour(uiConfig->getColour(key, juce::Colour::fromRGB(74, 153, 255)));
            }
        }
    }

    // ---- preset cluster ----------------------------------------------------
    // prev | name | next, then the menu button. These keep declared widths -
    // they are fixed-size controls, not navigation, so they do not stretch.
    {
        const auto menuWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.presetClusterMenuWidth", 94) : 94;
        const auto prevNextWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.prevNextWidth", 26) : 26;
        const auto smallGap = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.presetSmallGap", 8) : 8;
        const auto horizontalPad = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.presetHorizontalPad", 5) : 5;
        const auto menuButtonMinWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.menuButtonMinWidth", 64) : 64;
        const auto menuButtonMaxWidth = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.menuButtonMaxWidth", 78) : 78;
        const auto menuButtonInset = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.menuButtonInset", 16) : 16;

        // The menu sits in the same cluster as the preset picker, so its
        // separation is trimmed here rather than coming from the bar's flex gap.
        const auto menuGap = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.presetMenuGap", 1) : 1;

        // Trimmed as whole pixels rather than left to the bar's flex gap. A
        // gap of 1 there becomes a 0.5px half-margin on each side, and each
        // rounds up independently - so a 1px gap came out 2px wide.
        const auto clusterGap = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.presetClusterGap", 1) : 1;

        auto presetLayout = topMenuPresetClusterArea;
        presetLayout.removeFromLeft(clusterGap);
        auto menuSectionArea = presetLayout.removeFromRight(menuWidth);
        presetLayout.removeFromRight(menuGap);
        auto selector = presetLayout.reduced(horizontalPad, 0);

        presetPrevButton.setBounds(selector.removeFromLeft(prevNextWidth));
        selector.removeFromLeft(smallGap);
        presetNextButton.setBounds(selector.removeFromRight(prevNextWidth));
        selector.removeFromRight(smallGap);
        presetNameButton.setBounds(selector);

        const auto menuButtonWidth = juce::jlimit(menuButtonMinWidth,
                                                  menuButtonMaxWidth,
                                                  menuSectionArea.getWidth() - menuButtonInset);
        presetMenuButton.setBounds(juce::Rectangle<int>(menuButtonWidth, menuSectionArea.getHeight())
                                       .withCentre(menuSectionArea.getCentre()));
    }
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
    // Upper case on the way in, not in the file: the preset keeps whatever
    // casing it was saved with everywhere else - the browser list, the details
    // pane, the .px3 itself - and only the tab renders it this way, to sit with
    // the section tabs and the category and author beneath it.
    presetNameButton.setButtonText(name.toUpperCase());
}

void TopMenuBar::setPresetDetails(const juce::String& category, const juce::String& author)
{
    presetNameButton.setSubtitles(category, author);
}

void TopMenuBar::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    // One style for the whole strip. The buttons paint themselves now, so the
    // JUCE colour IDs the previous path set were going unread.
    TopMenuTabButton::Style tabStyle;
    if (uiConfig != nullptr)
    {
        tabStyle.face = uiConfig->getColour("topMenu.tabStyle.face", tabStyle.face);
        tabStyle.faceActive = uiConfig->getColour("topMenu.tabStyle.faceActive", tabStyle.faceActive);
        tabStyle.text = uiConfig->getColour("topMenu.tabStyle.text", tabStyle.text);
        tabStyle.textActive = uiConfig->getColour("topMenu.tabStyle.textActive", tabStyle.textActive);
        tabStyle.seam = uiConfig->getColour("topMenu.tabStyle.seam", tabStyle.seam);
        tabStyle.inset = uiConfig->getColour("topMenu.tabStyle.inset", tabStyle.inset);
        tabStyle.insetActive = uiConfig->getColour("topMenu.tabStyle.insetActive", tabStyle.insetActive);
    }

    for (auto* button : topMenuSectionButtons)
    {
        button->applyStyle(tabStyle);
    }
    for (auto* button : { &presetPrevButton, &presetNameButton, &presetNextButton, &presetMenuButton })
    {
        button->applyStyle(tabStyle);
    }

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

juce::TextButton& TopMenuBar::getPresetNameButton()
{
    return presetNameButton;
}

void TopMenuBar::configureTopMenuSectionButton(TopMenuTabButton& button,
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
