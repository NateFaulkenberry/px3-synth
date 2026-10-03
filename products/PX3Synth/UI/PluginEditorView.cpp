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
#include "PluginProcessorInternals.h"

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

    // Every box the editor shows - header, macro strip, sections, cards,
    // keyboard - comes from the instrument scene (InstrumentScene.json). What
    // remains below is overlays and sheets that float above the layout.
    applySceneLayout();

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

    // Under the top bar, tucked to the right so it sits below the gear it is
    // about rather than over the preset name - and now pointing at it, since
    // the notice is a speech bubble whose arrow comes off the top right.
    //
    // Everything here is a UIConfig knob. The height has to include the arrow:
    // the bubble draws its body below the pointer, so a component sized only
    // for the text would have the arrow eat the first few pixels of it.
    if (updateNotice.isVisible() && topMenuBar != nullptr)
    {
        SpeechBubbleLabel::Style style;
        if (uiConfig != nullptr)
        {
            // Opaque fallbacks: translucency is the opacity keys' job now, and a
            // colour carrying its own alpha would be multiplied by them twice.
            style.background = uiConfig->getColour("updateNotice.colors.background",
                                                   juce::Colours::black);
            style.border     = uiConfig->getColour("updateNotice.colors.border",
                                                   juce::Colours::white);
            style.text       = uiConfig->getColour("updateNotice.colors.text", juce::Colours::white);

            style.backgroundOpacity = uiConfig->getFloat("updateNotice.colors.backgroundOpacity",
                                                         style.backgroundOpacity);
            style.borderOpacity     = uiConfig->getFloat("updateNotice.colors.borderOpacity",
                                                         style.borderOpacity);

            style.cornerRadius = uiConfig->getFloat("updateNotice.layout.cornerRadius", 6.0f);
            style.borderWidth  = uiConfig->getFloat("updateNotice.layout.borderWidth", 1.0f);

            style.arrowWidth          = uiConfig->getFloat("updateNotice.layout.arrowWidth", 14.0f);
            style.arrowHeight         = uiConfig->getFloat("updateNotice.layout.arrowHeight", 8.0f);
            style.arrowInsetFromRight = uiConfig->getFloat("updateNotice.layout.arrowInsetFromRight", 18.0f);

            style.paddingX = uiConfig->getFloat("updateNotice.layout.paddingX", 10.0f);
            style.paddingY = uiConfig->getFloat("updateNotice.layout.paddingY", 4.0f);
            style.fontSize = uiConfig->getFloat("updateNotice.layout.fontSize", 12.0f);
        }
        updateNotice.setStyle(style);

        const auto bar = topMenuBar->getBounds();
        const auto width = uiConfig != nullptr
                               ? uiConfig->getInt("updateNotice.layout.width", 340)
                               : 340;
        const auto textHeight = uiConfig != nullptr
                                    ? uiConfig->getInt("updateNotice.layout.textHeight", 18)
                                    : 18;
        const auto offsetX = uiConfig != nullptr
                                 ? uiConfig->getInt("updateNotice.layout.offsetX", 0)
                                 : 0;
        const auto offsetY = uiConfig != nullptr
                                 ? uiConfig->getInt("updateNotice.layout.offsetY", 2)
                                 : 2;

        const auto clamped = juce::jmin(width, juce::jmax(1, bar.getWidth()));
        const auto height = textHeight + static_cast<int>(std::ceil(style.arrowHeight))
                            + static_cast<int>(std::ceil(style.paddingY * 2.0f));

        updateNotice.setBounds(bar.getRight() - clamped + offsetX,
                               bar.getBottom() + offsetY,
                               clamped,
                               height);

        // The close glyph, against the top-right of the notice's BODY - below
        // the arrow, which occupies the strip above it. Small by default so it
        // fits the line of text rather than setting the notice's height.
        px3::ui::SheetCloseButton::Style closeStyle;
        closeStyle.size = juce::jmax(8, textHeight - 4);
        closeStyle.offsetX = -static_cast<int>(std::ceil(style.paddingX * 0.5f));
        px3::ui::SheetCloseButton::readStyleFrom(uiConfig.get(), "updateNotice.closeButton",
                                                 closeStyle);
        updateNoticeCloseButton.applyStyle(closeStyle);

        auto body = updateNotice.getLocalBounds()
                        .withTrimmedTop(static_cast<int>(std::ceil(style.arrowHeight)));
        updateNoticeCloseButton.setBounds(updateNoticeCloseButton.boundsWithin(body));
    }

    if (macroDepthPanel != nullptr && macroDepthPanel->isVisible())
    {
        macroDepthScrim.setBounds(getLocalBounds());
        layoutMacroDepthPanel();
        macroAssignOverlay->toFront(false);
    }
    layoutModPanel();
    updatePanelVisibility();

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
    const auto presetTitleArea = browserArea.removeFromTop(24);
    presetBrowserTitle.setBounds(presetTitleArea);

    {
        px3::ui::SheetCloseButton::Style closeStyle;
        closeStyle.size = 20;
        px3::ui::SheetCloseButton::readStyleFrom(uiConfig.get(), "presetBrowser.closeButton",
                                                 closeStyle);
        presetBrowserCloseGlyph.applyStyle(closeStyle);
        presetBrowserCloseGlyph.setBounds(
            presetBrowserCloseGlyph.boundsWithin(presetTitleArea));
    }
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

#if PX3_UI_DESIGNER
    if (layoutDesigner != nullptr)
    {
        layoutDesigner->editorLaidOut();
    }
#endif

}
bool PX3SynthAudioProcessorEditor::isPanelVisible(int sectionIndex) const
{
    // OSC, FILTER and AMP live side by side in one scene row (view.osc); the
    // others each have a view of their own.
    auto nodeId = juce::String();
    switch (sectionIndex)
    {
        case kSectionOsc: nodeId = "primary.osc"; break;
        case kSectionMod: nodeId = "view.mod"; break;
        case kSectionAmp: nodeId = "primary.amp"; break;
        case kSectionFilter: nodeId = "primary.filter"; break;
        case kSectionFx: nodeId = "view.fx"; break;
        case kSectionMix: nodeId = "view.mix"; break;
        case kSectionSettings: nodeId = "view.settings"; break;
        default: break;
    }
    if (uiLayout.findNode(nodeId) != nullptr && ! uiLayout.isNodeVisible(nodeId))
    {
        return false;
    }

    if (isPrimaryCoreComposite() && (sectionIndex == kSectionAmp || sectionIndex == kSectionFilter))
    {
        return true;
    }
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
        const auto scrollTail = uiConfig != nullptr ? uiConfig->getInt("editor.layout.scrollTail", 30) : 30;
        const auto contentHeight = preferredHeight + scrollTail;
        // A scrolling panel's content stops short of the scrollbar. Decided from
        // the sizes rather than from the viewport's current bar, which lags a
        // viewport that has only just been given its bounds.
        const auto scrolls = contentHeight > modPanelViewport.getHeight();
        const auto gutter = scrolls ? modPanelViewport.getScrollBarThickness() + kScrollBarGutter : 0;
        const auto available = juce::jmax(1, modPanelViewport.getWidth() - gutter);
        const auto contentWidth = juce::jmax(available, preferredWidth);
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
