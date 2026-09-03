#pragma once

#include <JuceHeader.h>

#include "Card.h"

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

        // Hover is an inner glow rather than a brighter face.
        //
        // Lifting the whole fill changed what the tab IS - a selected tab and a
        // hovered unselected one drifted towards the same colour, so the strip
        // stopped saying which section was open. A glow inside the edges reads
        // as a highlight over the face instead of a different face, and it
        // works the same on both states.
        //
        // The equivalent of a CSS `box-shadow: inset 0 0 <size>px <colour>`.
        juce::Colour hoverGlow { juce::Colour::fromRGB(255, 255, 255) };
        float hoverGlowOpacity { 0.22f };
        float hoverGlowSize { 10.0f };
        // Pressed reads as the same glow, harder.
        float pressedGlowOpacity { 0.34f };
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

    // How the tab's text is laid out inside its face, read from UIConfig. Laid
    // out the same way a card's inner rows are: a padded content box, then rows
    // whose heights are Dimensions - pixels, a percentage of that box, or auto
    // for an equal share of what is left - each with its own padding.
    //
    // It is worth this much structure because the tab is 32px carrying three
    // strings, and because the first attempt fixed the name band at "58% of the
    // face, but never more than 19px". On a taller tab that cap pinned the name
    // to the top and handed every spare pixel to the row underneath.
    struct ContentStyle
    {
        struct Row
        {
            px3::ui::Dimension height {};                  // auto: an equal share
            px3::ui::Insets padding {};
            float fontSize { 0.0f };              // 0: derived from the row
        };

        px3::ui::Insets padding {};
        Row name { px3::ui::Dimension { px3::ui::Dimension::Unit::percent, 58.0f }, px3::ui::Insets {}, 0.0f };
        Row detail { px3::ui::Dimension { px3::ui::Dimension::Unit::percent, 42.0f }, px3::ui::Insets {}, 0.0f };

        float dividerAlpha { 1.0f };
        float dividerInset { 1.0f };
        float dividerWidth { 1.0f };

        // "CATEGORY: " and "AUTHOR: " in front of the values. Off leaves the
        // two values on their own, which is what the row was before they were
        // added and is worth being able to get back to.
        bool showLabels { true };
        bool detailUppercase { true };
        bool nameBold { true };

        // Where each value sits in its own half. Centred by default; "edges"
        // pushes the category out to the left and the author to the right,
        // away from the divider.
        // Where CATEGORY and AUTHOR sit inside their own cells, and how much
        // room each cell keeps clear around its text.
        //
        // Independent per cell: the pair reads as two fields, and wanting the
        // category hard left while the author stays centred is a normal thing
        // to want. A single shared setting could not express it.
        juce::Justification categoryAlign { juce::Justification::centred };
        juce::Justification authorAlign { juce::Justification::centred };
        px3::ui::Insets categoryPadding {};
        px3::ui::Insets authorPadding {};

        // Drawn in the tab's own text colour when these are transparent, which
        // is what keeps a themed tab consistent without restating its colours.
        juce::Colour nameColour { juce::Colours::transparentBlack };
        juce::Colour detailColour { juce::Colours::transparentBlack };
    };

    void setContentStyle(const ContentStyle& styleIn);

    // An icon in place of the legend. A tab with one draws neither a lamp nor
    // text: the glyph is the whole content, centred in the face. Everything
    // else - the face, the hover and pressed glows, the lit top edge, the
    // inset hairline - is the tab's as usual, which is what makes an icon tab
    // read as a member of the strip rather than a button added beside it.
    //
    // The path is given in any coordinates; it is scaled to fit.
    void setIcon(const juce::Path& path, float scaleOfFace = 0.46f);

    // A gear, built rather than loaded: a ring of trapezoidal teeth with a
    // hole through the middle. Even-odd winding is what makes the hole a hole
    // instead of a filled disc.
    static juce::Path gearIcon();

    // "There is something here you have not seen."
    //
    // A glow over the tab's own face, in the attention colour, on top of
    // whatever the tab is already doing. It PULSES when animations are on and
    // sits at a steady glow when they are off - the same rule the keyboard's
    // key highlight follows, so a user who has turned animations off still
    // gets the signal rather than losing it.
    //
    // The phase is driven from outside: one timer in the bar rather than one
    // per tab, and nothing running at all when no tab is asking for attention.
    void setAttention(bool shouldWantAttention);
    bool wantsAttention() const noexcept { return attention; }
    void setAttentionPhase(float phase01);
    void setAttentionColour(juce::Colour colour) { attentionColour = colour; }

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
    juce::Path icon;
    float iconScale { 0.46f };
    bool attention { false };
    float attentionPhase { 0.0f };
    juce::Colour attentionColour { juce::Colour::fromRGB(120, 220, 170) };
};

class TopMenuBar final : public juce::Component,
                        private juce::Timer
{
public:
    TopMenuBar();

    void resized() override;

    void setOnSectionSelected(std::function<void(int)> callback);
    void setOnPresetPrevious(std::function<void()> callback);
    void setOnPresetNext(std::function<void()> callback);
    void setOnPresetName(std::function<void()> callback);
    void setOnPresetMenu(std::function<void()> callback);
    void setOnSettings(std::function<void()> callback);

    // 0..5 select a panel tab; kSettingsSection selects SETTINGS, which lights
    // the gear and leaves every panel tab unlit.
    static constexpr int kSettingsSection = 6;
    void setSelectedSection(int sectionIndex);
    void setPresetName(const juce::String& name);
    // Shown under the name, upper case and smaller: category on the left,
    // author on the right. Either may be empty.
    void setPresetDetails(const juce::String& category, const juce::String& author);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    // Marks the gear as having something to show. Runs the pulse while it is
    // set and stops entirely when it is not, so a bar with nothing to announce
    // costs no timer at all.
    void setUpdateAvailable(bool isAvailable);
    bool isUpdateAvailable() const noexcept { return updateAvailable; }

    // For the tests: the gear, and whether it is currently glowing.
    TopMenuTabButton& debugSettingsButton() { return settingsButton; }

    const juce::Rectangle<int>& getSectionButtonsArea() const;
    const juce::Rectangle<int>& getPresetClusterArea() const;
    juce::Rectangle<int> getPresetMenuButtonBounds() const;
    juce::TextButton& getPresetMenuButton();
    // The preset name doubles as the button that opens the browser sheet.
    juce::TextButton& getPresetNameButton();
    juce::TextButton& getPresetNextButton();
    juce::TextButton& getPresetPrevButton();
    juce::TextButton& getSettingsButton();
    // For the tests: a section tab by index, and where it sits.
    juce::TextButton& getSectionButton(int sectionIndex);
    juce::Rectangle<int> getSectionButtonBounds(int sectionIndex) const;

private:
    void configureTopMenuSectionButton(TopMenuTabButton& button,
                                       const juce::String& text,
                                       int sectionIndex);

    TopMenuTabButton presetPrevButton { "" };
    TopMenuTabButton presetNameButton { "" };
    TopMenuTabButton presetNextButton { "" };
    TopMenuTabButton presetMenuButton { "MENU" };
    TopMenuTabButton settingsButton { "SETTINGS" };
    bool updateAvailable { false };
    float pulsePhase { 0.0f };

    void timerCallback() override;
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
    std::function<void()> onSettings;

    std::shared_ptr<const UIConfig> uiConfig;
};
