// Where everything goes, and what is on screen at all.
//
// Split out of PluginEditor.cpp. These are member functions of the same class,
// so this needs no change to the header - PluginEditorLook.cpp and
// PluginEditorDebug.cpp work the same way.
//
// resized() and the six per-panel layout methods it calls, the visibility rule
// that decides which panel a section shows, and paintOverChildren, which draws
// the frame on top of whatever the children drew.
//
// applyAnimationPreference is here rather than with the refresh family because
// what it changes is what the editor SHOWS - an animator either runs or the
// component sits at its resting state - which is the same kind of question as
// whether a panel is visible.

#include "PluginEditor.h"
#include "MacroLook.h"
#include "EditorSections.h"
#include "ParameterKnob.h"
#include "KnobOverlays.h"
#include "Card.h"
#include "UIConfig.h"
#include "../DSP/PluginProcessorInternals.h"

#include <algorithm>
#include <cmath>

using namespace px3::ui;

void PX3SynthAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // The bus insert sheets draw their backdrop on the SCRIM, which is a
    // component below them, rather than over the top of everything with a hole
    // cut for the sheet. Their faces are translucent, and a hole would let the
    // sharp, undimmed editor show through them while everything beside them was
    // blurred and dark.
    if (busInsertVisible)
    {
        return;
    }

    if (!presetBrowserVisible)
    {
        return;
    }

    // The preset browser is drawn as a modal-like sheet over the main UI. The
    // rest of the editor is blurred and dimmed rather than replaced, so the
    // patch you are browsing away from stays in view.
    px3::ui::paintModalBackdrop(g,
                                getLocalBounds(),
                                presetBrowserPanel.getBounds().toFloat(),
                                presetBrowserBackdropSnapshot,
                                10.0f,
                                juce::Colour::fromRGBA(0, 0, 0, 180),
                                uiConfig != nullptr
                                    ? uiConfig->getFloat("busInserts.backdropBlur", 4.5f)
                                    : 4.5f);
}

void PX3SynthAudioProcessorEditor::resized()
{
    // setResizeLimits() can trigger resized() during construction before
    // extracted panel components are created.
    if (oscPanel == nullptr || modPanel == nullptr || ampPanel == nullptr || fltPanel == nullptr || fxPanel == nullptr || mixPanel == nullptr)
    {
        return;
    }

    // Layout policy:
    // - Header prioritizes logo/preset bar/fx cards for quick performance edits.
    // - Mid section hosts core synth controls.
    // - Bottom section reserves reliable space for performance strip + keyboard.
    // This balancing intentionally avoids dramatic jumps while resizing.
    auto bounds = getLocalBounds().reduced(16);

    const auto headerHeight = uiConfig != nullptr ? uiConfig->getInt("editor.layout.headerHeight", 120) : 120;
    const auto controlsHeight = juce::jlimit(150, 270, static_cast<int>(std::lround(static_cast<double>(getHeight()) * 0.34)));
    // Fixed, not a fraction of the window. It was height * 0.15, which meant
    // every time the window grew to give the FX cards room the keyboard
    // silently took a share of it - and the fraction had to be re-based to
    // claw that back. 106px is what the fraction produced at every window
    // height up to about 707 anyway, because the lower clamp bound was doing
    // the work; making it explicit means the panels get all of any extra
    // height, at the default size and on resize.
    constexpr auto keyboardHeight = 106;
    const auto sectionGap = uiConfig != nullptr ? uiConfig->getInt("editor.layout.sectionGap", 10) : 10;

    headerArea = bounds.removeFromTop(headerHeight);
    topMenuStripArea = headerArea;

    // How far the bar's contents sit inside the strip. The tabs are meant to
    // read as part of the bar rather than as buttons placed on it, so this is
    // deliberately small.
    const auto stripPadX = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.stripPadX", 3) : 3;
    const auto stripPadY = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.stripPadY", 3) : 3;
    auto topStripContent = topMenuStripArea.reduced(stripPadX, stripPadY);
    const auto logoWidth = uiConfig != nullptr ? uiConfig->getInt("editor.layout.logoPanelWidth", 150) : 150;
    logoPanelArea = topStripContent.removeFromLeft(logoWidth);

    // The logo is drawn across its whole panel but only opens the site from the
    // left of it. The right edge butts against the OSC button now that the
    // sections are flush, and losing the panel you meant to click because a
    // browser opened is a bad trade.
    const auto logoClickInset = uiConfig != nullptr ? uiConfig->getInt("editor.layout.logoClickInsetRight", 18) : 18;
    logoClickArea = logoPanelArea.withTrimmedRight(juce::jlimit(0, logoPanelArea.getWidth() / 2, logoClickInset));

    const auto gainWidth = uiConfig != nullptr ? uiConfig->getInt("editor.layout.gainPanelWidth", 100) : 100;
    topMenuGainArea = topStripContent.removeFromRight(gainWidth);

    headerPlaceholderArea = topStripContent;
    if (topMenuBar != nullptr)
    {
        topMenuBar->setBounds(headerPlaceholderArea);

        const auto menuOrigin = topMenuBar->getPosition();
        topMenuSectionButtonsArea = topMenuBar->getSectionButtonsArea().translated(menuOrigin.x, menuOrigin.y);
        topMenuPresetClusterArea = topMenuBar->getPresetClusterArea().translated(menuOrigin.x, menuOrigin.y);
        topMenuMenuButtonArea = topMenuBar->getPresetMenuButtonBounds().translated(menuOrigin.x, menuOrigin.y);
        presetBarArea = topMenuPresetClusterArea;
    }

    // Knob and label as one group: the label sits directly under the knob
    // rather than pinned to the bottom of the area, which left a gap that grew
    // with the header height.
    auto gainArea = topMenuGainArea.reduced(9, 4);
    // The knob stands alone: no caption, and its name is on hover instead. The
    // top bar is the tightest space in the interface, and a permanent label
    // under a control whose function is obvious costs more than it explains.
    const auto gainKnobSize = juce::jlimit(46, 60,
                                           juce::jmin(gainArea.getWidth() - 6,
                                                      gainArea.getHeight() - 4));

    gainKnob.setBounds(juce::Rectangle<int>(gainKnobSize, gainKnobSize)
                           .withCentre(gainArea.getCentre()));
    gainLabel.setBounds({});

    bounds.removeFromTop(sectionGap);

    const auto desiredControlsHeight = juce::jmax(controlsHeight, bounds.getHeight() - keyboardHeight);
    controlsArea = bounds.removeFromTop(juce::jlimit(0, bounds.getHeight(), desiredControlsHeight));

    // No horizontal inset: the header, the panels and the keyboard all derive
    // from the same `bounds`, and a 4px reduce here was the only thing making
    // the keyboard start and end inboard of the other two.
    auto keyboardRow = bounds;
    const auto perfWidth = juce::jlimit(112, 190, keyboardRow.getWidth() / 8);
    performanceControlsArea = keyboardRow.removeFromLeft(perfWidth);

    // Both sit exactly where they belong; nothing is grown. Their particles are
    // drawn by sparkOverlay, which covers the pair plus the room the animation
    // needs above them.
    pianoKeyboard.setBounds(keyboardRow);
    performanceControls.setBounds(performanceControlsArea);

    const auto headroom = juce::jmin(keyboardSparkHeadroom, controlsArea.getHeight());
    const auto spill = juce::jmax(0, performanceSparkSpill);

    sparkOverlay.setBounds(performanceControlsArea.getUnion(keyboardRow)
                               .expanded(spill, 0)
                               .withTop(keyboardRow.getY() - headroom)
                               .withBottom(keyboardRow.getBottom() + spill)
                               .getIntersection(getLocalBounds()));

    // The overlay goes last, above both, and takes no mouse events - so the
    // keys and the wheels keep every click that was ever theirs.
    pianoKeyboard.toFront(false);
    performanceControls.toFront(false);
    sparkOverlay.toFront(false);

    // Vertical inset only. The horizontal 8 here was the reason the cards sat
    // inboard of the top nav: the header strip is drawn at the full width of
    // `bounds`, so any extra horizontal reduce on the panel area misaligns the
    // two edges.
    panelViewportArea = controlsArea.reduced(0, 8);

    // The macro strip comes off the LEFT of the one rectangle every panel is
    // laid out in. Doing it here rather than in each panel is what puts the
    // same four knobs on OSC, MOD, FLT, FX, AMP and MIX with one instance and
    // no panel needing to know they exist.
    // The look-and-feel is shared by every knob and has no config prefix of
    // its own, so the macro colours are resolved here and handed to it.
    knobLookAndFeel.macroAccent = px3::ui::macroAccentColour(uiConfig.get());
    knobLookAndFeel.macroLabelBackground = px3::ui::macroLabelBackgroundColour(uiConfig.get());
    knobLookAndFeel.macroLabelText = px3::ui::macroLabelTextColour(uiConfig.get());

    // The macro knobs' own look takes the same three colours, so its arc, dots
    // and pointer track the macro accent with everything else.
    macroKnobLookAndFeel.overlayColours = { knobLookAndFeel.macroAccent,
                                            knobLookAndFeel.macroLabelBackground,
                                            knobLookAndFeel.macroLabelText };
    macroKnobLookAndFeel.pointerColour = px3::ui::macroPointerColour(uiConfig.get());
    macroKnobLookAndFeel.pointerDisabledColour
        = px3::ui::macroPointerDisabledColour(uiConfig.get());

    // SETTINGS is the one view without the macro strip: it is a form, and a
    // performance surface beside it would be four knobs with nothing on this
    // page to assign them to. The strip's width goes back to the panel rather
    // than being left as a gap, so the form is genuinely full width.
    const auto showMacroStrip = selectedTopMenuSection != kSectionSettings;

    if (showMacroStrip)
    {
        macroStripArea = panelViewportArea.removeFromLeft(
            MacroStrip::preferredWidth(uiConfig.get()));
        panelViewportArea.removeFromLeft(2);
    }
    else
    {
        macroStripArea = {};
    }

    if (macroStrip != nullptr)
    {
        macroStrip->setVisible(showMacroStrip);
        macroStrip->setBounds(macroStripArea);
    }

    if (macroAssignOverlay != nullptr)
    {
        // Over the knobs and the macro strip, and NOTHING else.
        //
        // Covering the whole editor swallowed the top menu, so a user could not
        // change panel while assigning - which is most of the point of a macro
        // that reaches across the synth. It also swallowed the keyboard, so
        // they could not hear what they were building either.
        macroAssignOverlay->setBounds(macroStripArea.getUnion(panelViewportArea));
    }

    if (macroDepthPanel != nullptr && macroDepthPanel->isVisible())
    {
        macroDepthScrim.setBounds(getLocalBounds());
        layoutMacroDepthPanel();
        macroAssignOverlay->toFront(false);
    }
    // panels.osc: a declared height wins over the editor's allocation, and
    // overflowY decides whether the panel scrolls when its content is taller
    // than the space it has.
    {
        const auto panelStyle = px3::ui::PanelStyle::fromConfig(uiConfig.get(), "panels.osc");
        auto oscArea = panelViewportArea;
        if (panelStyle.height > 0)
        {
            oscArea = oscArea.withHeight(juce::jmin(panelStyle.height, panelViewportArea.getHeight()));
        }
        oscPanelViewport.setBounds(oscArea);
        oscPanelViewport.setScrollBarsShown(panelStyle.scrollVertically, false);

        // The viewed component keeps its declared height even when that exceeds
        // the viewport - that is what there is to scroll. Without scrolling it
        // matches the viewport exactly, so nothing can be clipped away.
        // A scrolling panel gets a tail of empty space past its last row, so
        // the bottom card can be scrolled clear of the viewport edge instead of
        // stopping flush against it.
        const auto scrollTail = uiConfig != nullptr ? uiConfig->getInt("editor.layout.scrollTail", 30) : 30;
        const auto contentHeight = panelStyle.scrollVertically && panelStyle.height > 0
                                       ? juce::jmax(panelStyle.height, oscArea.getHeight()) + scrollTail
                                       : oscPanelViewport.getMaximumVisibleHeight();
        const auto oscGutter = oscPanelViewport.isVerticalScrollBarShown() ? kScrollBarGutter : 0;
        oscPanel->setSize(juce::jmax(1, oscPanelViewport.getMaximumVisibleWidth() - oscGutter),
                          contentHeight);
    }
    modPanelViewport.setBounds(panelViewportArea);
    ampPanel->setBounds(panelViewportArea);
    fltPanel->setBounds(panelViewportArea);
    fxPanel->setBounds(panelViewportArea);
    mixPanel->setBounds(panelViewportArea);
    if (settingsPanel != nullptr)
    {
        settingsPanel->setBounds(panelViewportArea);
    }

    layoutOscPanel();
    layoutModPanel();
    layoutAmpPanel();
    layoutFilterPanel();
    layoutFxPanel();
    layoutMixPanel();

    const auto browserWidth = juce::jlimit(520, 760, getWidth() - 120);
    const auto browserHeight = juce::jlimit(360, 520, getHeight() - 120);
    auto browserX = (getWidth() - browserWidth) / 2;
    auto browserY = (getHeight() - browserHeight) / 2;

    if (presetBrowserPanel.getWidth() > 0 && presetBrowserPanel.getHeight() > 0)
    {
        browserX = presetBrowserPanel.getX();
        browserY = presetBrowserPanel.getY();
    }

    browserX = juce::jlimit(8, juce::jmax(8, getWidth() - browserWidth - 8), browserX);
    browserY = juce::jlimit(8, juce::jmax(8, getHeight() - browserHeight - 8), browserY);
    presetBrowserScrim.setBounds(getLocalBounds());
    presetBrowserPanel.setBounds(browserX, browserY, browserWidth, browserHeight);

    auto browserArea = presetBrowserPanel.getLocalBounds().reduced(10);
    presetBrowserTitle.setBounds(browserArea.removeFromTop(24));
    browserArea.removeFromTop(6);

    auto filterRow = browserArea.removeFromTop(26);
    presetScopeBox.setBounds(filterRow.removeFromLeft(120));
    filterRow.removeFromLeft(6);
    presetCategoryBox.setBounds(filterRow.removeFromLeft(170));
    filterRow.removeFromLeft(6);
    presetSearchEditor.setBounds(filterRow);

    browserArea.removeFromTop(6);
    auto footer = browserArea.removeFromBottom(74);
    presetListBox.setBounds(browserArea.removeFromLeft(browserArea.getWidth() * 2 / 3));
    browserArea.removeFromLeft(8);
    presetBrowserDetails.setBounds(browserArea);

    auto footerRight = footer.removeFromRight(190);
    presetBrowserLoadButton.setBounds(footerRight.removeFromLeft(90));
    footerRight.removeFromLeft(10);
    presetBrowserCloseButton.setBounds(footerRight.removeFromLeft(90));


}
bool PX3SynthAudioProcessorEditor::isPanelVisible(int sectionIndex) const
{
    return selectedTopMenuSection == juce::jlimit(0, kSectionSettings, sectionIndex);
}

void PX3SynthAudioProcessorEditor::updatePanelVisibility()
{
    oscPanelViewport.setVisible(isPanelVisible(kSectionOsc));
    if (oscPanel != nullptr)
    {
        oscPanel->setVisible(true);
    }
    modPanelViewport.setVisible(isPanelVisible(kSectionMod));
    if (modPanel != nullptr)
    {
        modPanel->setVisible(true);
    }
    ampPanel->setVisible(isPanelVisible(kSectionAmp));
    fltPanel->setVisible(isPanelVisible(kSectionFilter));
    fxPanel->setVisible(isPanelVisible(kSectionFx));
    mixPanel->setVisible(isPanelVisible(kSectionMix));

    if (settingsPanel != nullptr)
    {
        settingsPanel->setVisible(isPanelVisible(kSectionSettings));
    }
}

void PX3SynthAudioProcessorEditor::applyAnimationPreference()
{
    const auto enabled = px3::GlobalSettings::getInstance().areAnimationsEnabled();

    pianoKeyboard.setAnimationsEnabled(enabled);
    performanceControls.setAnimationsEnabled(enabled);

    // The logo settles rather than freezing mid-shake: the timer stops
    // advancing its phase, so without this it would hold whatever offset it
    // had when the setting changed.
    if (! enabled)
    {
        logoVibrationIntensity = 0.0f;
        logoVibrationPhase = 0.0f;
        repaint(logoPanelArea.expanded(8));
    }
}

void PX3SynthAudioProcessorEditor::layoutOscPanel()
{
    if (oscPanel != nullptr)
    {
        oscPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutFilterPanel()
{
    if (fltPanel != nullptr)
    {
        fltPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutAmpPanel()
{
    if (ampPanel != nullptr)
    {
        ampPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutModPanel()
{
    if (modPanel != nullptr && modPanelViewport.getWidth() > 0 && modPanelViewport.getHeight() > 0)
    {
        const auto preferredWidth = modPanel->getPreferredContentWidth();
        const auto preferredHeight = modPanel->getPreferredContentHeight();
        // A scrolling panel's content stops short of the scrollbar. getWidth()
        // includes the bar, so sizing to it put the cards underneath it.
        const auto gutter = modPanelViewport.isVerticalScrollBarShown() ? kScrollBarGutter : 0;
        const auto available = juce::jmax(1, modPanelViewport.getMaximumVisibleWidth() - gutter);
        const auto contentWidth = juce::jmax(available, preferredWidth);
        const auto scrollTail = uiConfig != nullptr ? uiConfig->getInt("editor.layout.scrollTail", 30) : 30;
        const auto contentHeight = preferredHeight + scrollTail;
        modPanel->setBounds(0, 0, contentWidth, contentHeight);
        modPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutFxPanel()
{
    // The panel lays itself out. It owns the signal-flow strip, the viewport
    // and the grid, so the editor's only job is to hand it the chain order.
    if (fxPanel != nullptr)
    {
        fxPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutMixPanel()
{
    if (mixPanel != nullptr)
    {
        mixPanel->resized();
    }
}
