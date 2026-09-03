// The editor's own paint: the frame the panels sit inside.
//
// Split from what was PluginEditorLook.cpp when the knob look-and-feel moved to
// shared/UI/Style - that is shared, this is the Synth's window.

#include "MacroLook.h"
#include "ParameterKnob.h"
#include "PluginEditor.h"
#include "KnobOverlays.h"
#include "WavetableLibrary.h"
#include "WavetableImporter.h"
#include "WavetableFactory.h"
#include "ModalBackdrop.h"
#include "RoundedRect.h"
#include "PluginProcessorInternals.h"
#include "Card.h"
#include "BinaryData.h"
#include "PX3Version.h"
#include "UIConfig.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <mach/mach.h>

void PX3SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto bg = uiConfig != nullptr
                        ? uiConfig->getColour("editor.background.baseColour", juce::Colour::fromRGB(0x1A, 0x1A, 0x1A))
                        : juce::Colour::fromRGB(0x1A, 0x1A, 0x1A);
    const auto stripRadius = uiConfig != nullptr ? uiConfig->getFloat("editor.topStrip.cornerRadius", 12.0f) : 12.0f;
    g.fillAll(bg);


    if (backgroundImage.isValid())
    {
        g.drawImageWithin(backgroundImage,
                          0,
                          0,
                          getWidth(),
                          getHeight(),
                          juce::RectanglePlacement::fillDestination,
                          false);

        const auto darkness = uiConfig != nullptr ? uiConfig->getInt("editor.background.imageDarkness", 150) : 150;
        const auto alpha = static_cast<juce::uint8>(juce::jlimit(0, 255, darkness));
        g.setColour(juce::Colour::fromRGBA(26, 26, 26, alpha));
        g.fillAll();
    }

    // The strip is filled AFTER the background image, not before it. Drawn
    // first, the image simply covered it - which is why only the logo panel,
    // the one piece painted after the image, had any background at all.
    //
    // One fill for the whole strip: there used to be an outline and four
    // vertical dividers marking the boundaries between logo, sections, presets
    // and menu, and with the sections butted together there is nothing left to
    // mark.
    g.setColour(uiConfig != nullptr
                    ? uiConfig->getColour("editor.topStrip.fillColour", juce::Colour::fromRGB(0x1A, 0x1A, 0x1A))
                    : juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));
    g.fillRoundedRectangle(topMenuStripArea.toFloat(), stripRadius);

    // The logo and master gain sections get the same hairline the tabs carry,
    // so every block in the bar is framed alike. Rounded here rather than
    // square because these two sit at the strip's rounded ends - a square
    // outline would cut across the corner.
    {
        const auto insetColour = uiConfig != nullptr
                                     ? uiConfig->getColour("topMenu.tabStyle.inset",
                                                           juce::Colour::fromRGBA(226, 232, 240, 40))
                                     : juce::Colour::fromRGBA(226, 232, 240, 40);
        const auto defaultRadius = juce::jmax(0.0f, stripRadius - 1.0f);

        // Per-corner radii for the INSET hairline itself - this is the only
        // border these two sections have. Nothing here fills, so changing a
        // radius moves the outline and leaves the background untouched. A
        // section that butts against the tabs can be square on that side and
        // follow the strip's rounding on the other.
        const auto strokeSection = [&](const juce::Rectangle<int>& section, const juce::String& path)
        {
            if (section.isEmpty())
            {
                return;
            }

            const auto radiusFor = [&](const char* corner)
            {
                return uiConfig != nullptr ? uiConfig->getFloat(path + "." + corner, defaultRadius)
                                           : defaultRadius;
            };

            const auto area = section.toFloat().reduced(1.0f);
            const auto tl = juce::jmax(0.0f, radiusFor("topLeft"));
            const auto tr = juce::jmax(0.0f, radiusFor("topRight"));
            const auto br = juce::jmax(0.0f, radiusFor("bottomRight"));
            const auto bl = juce::jmax(0.0f, radiusFor("bottomLeft"));

            juce::Path outline;
            outline.startNewSubPath(area.getX() + tl, area.getY());
            outline.lineTo(area.getRight() - tr, area.getY());
            if (tr > 0.0f) outline.addArc(area.getRight() - tr * 2.0f, area.getY(), tr * 2.0f, tr * 2.0f,
                                          0.0f, juce::MathConstants<float>::halfPi, false);
            outline.lineTo(area.getRight(), area.getBottom() - br);
            if (br > 0.0f) outline.addArc(area.getRight() - br * 2.0f, area.getBottom() - br * 2.0f, br * 2.0f, br * 2.0f,
                                          juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi, false);
            outline.lineTo(area.getX() + bl, area.getBottom());
            if (bl > 0.0f) outline.addArc(area.getX(), area.getBottom() - bl * 2.0f, bl * 2.0f, bl * 2.0f,
                                          juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f, false);
            outline.lineTo(area.getX(), area.getY() + tl);
            if (tl > 0.0f) outline.addArc(area.getX(), area.getY(), tl * 2.0f, tl * 2.0f,
                                          juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi, false);
            outline.closeSubPath();

            g.strokePath(outline, juce::PathStrokeType(1.0f));
        };

        g.setColour(insetColour);
        strokeSection(logoPanelArea, "topMenu.logoSection.inset.cornerRadius");
        strokeSection(topMenuGainArea, "topMenu.gainSection.inset.cornerRadius");
    }

    if (logoFrame.isValid())
    {
                // The version line used to sit under the logo; it is a menu item
                // now, so the logo has the panel to itself.
                // The 46px reserve was room for the version subtitle. With that
                // gone the logo fills the panel, which is what lets a shorter
                // header still show it at a sensible size.
                // How much shorter than its panel the logo sits - its breathing
                // room inside the section.
                const auto logoInset = uiConfig != nullptr ? uiConfig->getInt("editor.logo.heightInset", 20) : 20;
                const auto logoSize = static_cast<float>(juce::jlimit(40, 120, logoPanelArea.getHeight() - logoInset));
                // Centred on the panel outright, rather than derived from a content
                // height and a top offset. One expression, so changing the size
                // cannot move it off centre.
                const auto logoArea = juce::Rectangle<float>(logoSize, logoSize)
                                          .withCentre(logoPanelArea.toFloat().getCentre());
        const auto vibration = juce::jlimit(0.0f, 1.0f, logoVibrationIntensity);
        const auto shakePx = vibration * 3.2f;
        const auto shakeX = std::sin(logoVibrationPhase * 5.7f) * shakePx;
        const auto shakeY = std::cos(logoVibrationPhase * 7.9f + 0.8f) * (shakePx * 0.85f);
        // The logo is thin white strokes on transparency. Scaled down with the
        // default resampler those strokes average toward the transparent pixels
        // beside them and the whole mark reads dimmer - which is why it looked
        // darker once the header shrank.
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

        auto transform = juce::AffineTransform::scale(logoArea.getWidth() / static_cast<float>(logoFrame.getWidth()),
                                                      logoArea.getHeight() / static_cast<float>(logoFrame.getHeight()))
                             .translated(logoArea.getX(), logoArea.getY());
        transform = transform.translated(shakeX, shakeY);
        g.drawImageTransformed(logoFrame, transform);

        // The GIF decoder hands back frame 0, which is one of the darker frames
        // in the loop - so the logo at rest reads dimmer than it does mid
        // animation, when the glitch masks are adding light on top of it.
        //
        // Redrawing the same frame builds up the alpha of its anti-aliased
        // strokes, which is what brightens them: a thin stroke at 40% alpha
        // becomes 64% after a second pass. Cheaper and truer to the artwork
        // than tinting it.
        const auto logoBoost = uiConfig != nullptr ? uiConfig->getFloat("editor.logo.boost", 0.55f) : 0.55f;
        if (logoBoost > 0.0f)
        {
            g.setOpacity(juce::jlimit(0.0f, 1.0f, logoBoost));
            g.drawImageTransformed(logoFrame, transform);
            g.setOpacity(1.0f);
        }

        if (logoVibrationIntensity > 0.01f
            && logoGlitchMaskR.isValid()
            && logoGlitchMaskG.isValid()
            && logoGlitchMaskB.isValid())
        {
            const auto glitchStrength = juce::jlimit(0.0f, 1.0f, logoVibrationIntensity);
            const auto split = 0.6f + glitchStrength * 3.0f;
            const auto phase = logoVibrationPhase;
            const auto offsetX = std::sin(phase * 8.7f) * split;
            const auto offsetY = std::cos(phase * 6.3f + 0.5f) * (split * 0.32f);
            const auto alpha = 0.17f + glitchStrength * 0.40f;

            g.setOpacity(alpha);
            g.drawImageTransformed(logoGlitchMaskR,
                                   transform.translated(-offsetX * 0.95f, -offsetY * 0.45f));
            g.setOpacity(alpha * 0.72f);
            g.drawImageTransformed(logoGlitchMaskG,
                                   transform.translated(offsetX * 0.22f, offsetY * 0.20f));
            g.setOpacity(alpha);
            g.drawImageTransformed(logoGlitchMaskB,
                                   transform.translated(offsetX, offsetY * 0.45f));
            g.setOpacity(1.0f);
        }
    }

    // The FX section cards are painted by FxPanel itself - see
    // FxPanel::paintSectionCards. They live inside that panel's rectangle, and
    // six panels share that rectangle and are swapped by visibility, so
    // decoration painted from here was not tied to the panel's lifetime and
    // could survive a switch to another panel as stale outlines.


    // The strip BEHIND the performance controls and the keyboard.
    //
    // A third layer, easy to forget: a translucent gradient, a hairline outline
    // and a divider, spanning both components. Its corners were fixed at 12px
    // while the two components on top of it were square, so as soon as either
    // of them was rounded this showed through the gap - which is exactly what
    // "something is peeking past the corners" is.
    //
    // Every part of it is configurable now, including off, since it is the
    // layer most likely to be in the way of a rounded panel above it.
    const auto stripEnabled = uiConfig == nullptr || uiConfig->getBool("performanceStrip.enabled", true);

    if (stripEnabled && performanceControlsArea.getWidth() > 0 && pianoKeyboard.getBounds().getWidth() > 0)
    {
        // The KEYS, not the keyboard component. The component is taller than
        // the instrument it draws - it carries transparent headroom above the
        // keys so the sparks are not clipped - and taking its raw bounds here
        // dragged this gradient and its outline up into that headroom, where it
        // read as a stray panel floating above the keyboard.
        const auto keys = pianoKeyboard.keyboardArea() + pianoKeyboard.getPosition();
        const auto performanceStrip = performanceControlsArea.getUnion(keys).toFloat();

        const auto radii = px3::ui::CornerRadii::fromConfig(uiConfig.get(), "performanceStrip",
                                                            px3::ui::CornerRadii::all(12.0f));

        // A solid fill, not a gradient. The diagonal ramp it used to carry read
        // as a panel laid on top of the bar rather than as part of it, and it
        // could not be matched to anything else in the interface - every other
        // surface here is a flat colour with an opacity.
        const auto fill = uiConfig != nullptr
                              ? uiConfig->getColour("performanceStrip.background.color",
                                                    juce::Colour::fromRGB(20, 20, 20))
                              : juce::Colour::fromRGB(20, 20, 20);
        const auto fillOpacity = uiConfig != nullptr
                                     ? uiConfig->getFloat("performanceStrip.background.opacity", 1.0f)
                                     : 1.0f;

        g.setColour(fill.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, fillOpacity)));
        px3::ui::fillRounded(g, performanceStrip, radii);

        const auto outlineWidth = uiConfig != nullptr
                                      ? uiConfig->getFloat("performanceStrip.outline.width", 1.0f)
                                      : 1.0f;
        if (outlineWidth > 0.0f)
        {
            g.setColour(uiConfig != nullptr
                            ? uiConfig->getColour("performanceStrip.outline.color",
                                                  juce::Colour::fromRGBA(255, 255, 255, 50))
                            : juce::Colour::fromRGBA(255, 255, 255, 50));
            px3::ui::drawRounded(g, performanceStrip, radii, outlineWidth);
        }

        const auto dividerWidth = uiConfig != nullptr
                                      ? uiConfig->getFloat("performanceStrip.divider.width", 1.0f)
                                      : 1.0f;
        if (dividerWidth > 0.0f)
        {
            const auto inset = uiConfig != nullptr
                                   ? uiConfig->getFloat("performanceStrip.divider.inset", 8.0f)
                                   : 8.0f;
            const auto dividerX = static_cast<float>(performanceControlsArea.getRight() + 3);
            g.setColour(uiConfig != nullptr
                            ? uiConfig->getColour("performanceStrip.divider.color",
                                                  juce::Colour::fromRGBA(255, 255, 255, 38))
                            : juce::Colour::fromRGBA(255, 255, 255, 38));
            g.drawLine(dividerX,
                       performanceStrip.getY() + inset,
                       dividerX,
                       performanceStrip.getBottom() - inset,
                       dividerWidth);
        }
    }

}
