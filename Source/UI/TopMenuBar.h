#pragma once

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>

class UIConfig;

// A section tab in the top menu bar.
//
// Drawn as a flush, full-height panel rather than a rounded button: an LED
// above a bold caps legend, with a hairline seam on its right edge. Adjacent
// tabs butt together with no gap, so the row reads as one strip of hardware
// instead of six separate controls floating in a bar.
class TopMenuTabButton final : public juce::TextButton
{
public:
    // Colours for the tab face. These come from UIConfig, so the strip stays
    // configurable now that the tab paints itself rather than going through
    // JUCE's TextButton colour IDs.
    struct Style
    {
        juce::Colour face { juce::Colour::fromRGB(30, 32, 37) };
        juce::Colour faceActive { juce::Colour::fromRGB(46, 49, 56) };
        juce::Colour text { juce::Colour::fromRGB(186, 190, 198) };
        juce::Colour textActive { juce::Colour::fromRGB(245, 247, 250) };
        juce::Colour seam { juce::Colour::fromRGBA(0, 0, 0, 150) };
        // A hairline drawn one pixel inside the face, so each control reads as
        // its own key in the strip without a heavy outline around it.
        juce::Colour inset { juce::Colour::fromRGBA(226, 232, 240, 40) };
        juce::Colour insetActive { juce::Colour::fromRGBA(240, 245, 252, 90) };
    };

    explicit TopMenuTabButton(const juce::String& name);

    void applyStyle(const Style& styleIn);

    // Lights the LED and tints the active face. Each section passes the colour
    // of the panel it opens.
    void setAccentColour(juce::Colour colour);
    // The last tab in a row has no neighbour to be separated from.
    void setShowSeam(bool shouldShow);
    // A tab that opens a menu rather than selecting a panel has no on/off state
    // to report, so it wears the same face with no lamp on it.
    void setShowLed(bool shouldShow);

    // A second line under the legend, left and right justified. The preset tab
    // uses it for the loaded preset's category and author; a tab with neither
    // draws exactly as it did before, with the legend centred in the whole
    // face. Drawn upper case and smaller than the legend, so the name stays
    // the thing you read first.
    void setSubtitles(const juce::String& left, const juce::String& right);

    // Wears the ACTIVE text colour whether or not it is toggled on. The preset
    // tab is not a section tab - it never reports an on state - so without this
    // it was permanently drawn in the dimmed colour the unselected sections
    // use, which is not what it is: it is showing you what is loaded.
    void setAlwaysActiveText(bool shouldBeActive);

    // How the tab's text is laid out inside its face, read from UIConfig. A
    // zero means "derive it from the tab's height", which is what every tab did
    // before any of this was configurable - so a tab given no content style is
    // laid out exactly as it was.
    struct ContentStyle
    {
        float paddingTop { 0.0f };
        float paddingBottom { 0.0f };
        float paddingLeft { 6.0f };
        float paddingRight { 6.0f };
        // 0 = derived from the band the text sits in.
        float nameFontSize { 0.0f };
        float detailFontSize { 0.0f };
        // 0 = the second line takes 58% of what is left under the name.
        float detailRowHeight { 0.0f };
        float dividerAlpha { 1.0f };
    };

    void setContentStyle(const ContentStyle& styleIn);

private:
    void paintButton(juce::Graphics& g,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    Style style;
    juce::Colour accent { juce::Colour::fromRGB(74, 153, 255) };
    bool showSeam { true };
    bool showLed { true };
    juce::String subtitleLeft;
    juce::String subtitleRight;
    bool alwaysActiveText { false };
    ContentStyle content;
};

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
    // Shown under the name, upper case and smaller: category on the left,
    // author on the right. Either may be empty.
    void setPresetDetails(const juce::String& category, const juce::String& author);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    const juce::Rectangle<int>& getSectionButtonsArea() const;
    const juce::Rectangle<int>& getPresetClusterArea() const;
    juce::Rectangle<int> getPresetMenuButtonBounds() const;
    juce::TextButton& getPresetMenuButton();
    // The preset name doubles as the button that opens the browser sheet.
    juce::TextButton& getPresetNameButton();
    juce::TextButton& getPresetNextButton();

private:
    void configureTopMenuSectionButton(TopMenuTabButton& button,
                                       const juce::String& text,
                                       int sectionIndex);

    TopMenuTabButton presetPrevButton { "" };
    TopMenuTabButton presetNameButton { "" };
    TopMenuTabButton presetNextButton { "" };
    TopMenuTabButton presetMenuButton { "MENU" };
    TopMenuTabButton topMenuOscButton { "OSC" };
    TopMenuTabButton topMenuModButton { "MOD" };
    TopMenuTabButton topMenuAmpButton { "AMP" };
    TopMenuTabButton topMenuFltButton { "FLT" };
    TopMenuTabButton topMenuFxButton { "FX" };
    TopMenuTabButton topMenuMixButton { "MIX" };

    std::array<TopMenuTabButton*, 6> topMenuSectionButtons {
        { &topMenuOscButton, &topMenuModButton, &topMenuAmpButton, &topMenuFltButton, &topMenuFxButton, &topMenuMixButton }
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
