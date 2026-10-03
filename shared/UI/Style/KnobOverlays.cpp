#include "KnobOverlays.h"


namespace px3::ui
{

void drawKnobOverlays(juce::Graphics& g,
                      juce::Rectangle<float> bounds,
                      const juce::Slider& slider,
                      bool renderGrayscale,
                      const KnobOverlayColours& colours,
                      bool paleSubstrate)
{
    const auto center = bounds.getCentre();
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // ---- Modulation rings ----------------------------------------------------
    //
    // One arc per route, outside the knob, in the source's colour: the range
    // the route can sweep the control across. Stacked outwards, at most three.
    if (const auto* rings = slider.getProperties()[px3::knob_properties::modRings].getArray();
        rings != nullptr && ! renderGrayscale)
    {
        const auto rotary = slider.getRotaryParameters();
        const auto span = rotary.endAngleRadians - rotary.startAngleRadians;
        const auto count = juce::jmin(3, rings->size() / 3);
        const auto stroke = radius > 20.0f ? 2.2f : 1.6f;
        for (int i = 0; i < count; ++i)
        {
            const auto colour = juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>((*rings)[i * 3])));
            const auto lo = static_cast<float>(static_cast<double>((*rings)[i * 3 + 1]));
            const auto hi = static_cast<float>(static_cast<double>((*rings)[i * 3 + 2]));
            const auto ringRadius = radius + 2.4f + static_cast<float>(i) * (stroke + 0.8f);
            juce::Path track;
            track.addCentredArc(center.x, center.y, ringRadius, ringRadius, 0.0f,
                                rotary.startAngleRadians, rotary.endAngleRadians, true);
            g.setColour(colour.withAlpha(0.16f));
            g.strokePath(track, juce::PathStrokeType(stroke * 0.6f));
            const auto a0 = rotary.startAngleRadians + span * juce::jlimit(0.0f, 1.0f, lo);
            const auto a1 = rotary.startAngleRadians + span * juce::jlimit(0.0f, 1.0f, juce::jmax(hi, lo + 0.004f));
            juce::Path arc;
            arc.addCentredArc(center.x, center.y, ringRadius, ringRadius, 0.0f, a0, a1, true);
            g.setColour(colour);
            g.strokePath(arc, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // A small jack-coloured dot at the lower left says "patched" even when
        // the depth is too small for the arc to read.
        if (count > 0)
        {
            const auto colour = juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>((*rings)[0])));
            const auto dot = juce::Rectangle<float>(5.0f, 5.0f).withCentre(
                { center.x - radius * 0.86f, center.y + radius * 0.86f });
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillEllipse(dot.expanded(1.0f));
            g.setColour(colour);
            g.fillEllipse(dot);
        }
    }

    // ---- MIDI mapping ------------------------------------------------------
    //
    // Drawn here so every knob in the synth gets it from one place, whichever
    // look-and-feel drew the knob underneath.
    const auto midiCc = static_cast<int>(
        slider.getProperties().getWithDefault(px3::knob_properties::midiCc, -1));
    const auto midiSelected = static_cast<bool>(
        slider.getProperties().getWithDefault(px3::knob_properties::midiSelected, false));

    if (midiSelected)
    {
        // A dashed ring outside the knob rather than a fill over it: the value
        // and the modulation ring both still have to be readable while the
        // user is picking which knobs to assign.
        juce::Path ring;
        ring.addEllipse(bounds.expanded(2.0f));

        const float dashes[] = { 4.0f, 3.0f };
        juce::Path dashed;
        juce::PathStrokeType(1.6f).createDashedStroke(dashed, ring, dashes, 2);

        g.setColour(juce::Colour::fromRGB(255, 214, 92));
        g.fillPath(dashed);
    }

    // ---- macros ------------------------------------------------------------
    //
    // Violet throughout, where MIDI is amber, so the three states the brief
    // asks to be distinguishable - driven by a macro, mapped to a CC, both -
    // are told apart by colour before anything is read.
    const auto macroMask = static_cast<int>(
        slider.getProperties().getWithDefault(px3::knob_properties::macroMask, 0));
    const auto macroAssignable = static_cast<bool>(
        slider.getProperties().getWithDefault(px3::knob_properties::macroAssignable, false));

    if (macroAssignable)
    {
        // Every eligible knob says so while assigning. A solid ring, against
        // MIDI Learn's dashed one, so the two modes never look alike.
        const auto alreadyAssigned = macroMask != 0;
        juce::Path ring;
        ring.addEllipse(bounds.expanded(alreadyAssigned ? 3.0f : 2.0f));

        g.setColour(colours.macroAccent.withAlpha(alreadyAssigned ? 0.92f : 0.47f));
        g.strokePath(ring, juce::PathStrokeType(alreadyAssigned ? 2.2f : 1.3f));
    }

    if (macroMask != 0)
    {
        // One macro names itself in full; several become "M1+", which is the
        // compact form §21 asks for once more than one will not fit.
        auto first = -1;
        auto count = 0;
        // Every bit the mask can carry, rather than a particular product's
        // macro count. The mask IS the list; iterating it needs no knowledge
        // of who built it, and that was this file's only tie to the Synth.
        for (int macro = 0; macro < static_cast<int>(sizeof(macroMask) * 8); ++macro)
        {
            if ((macroMask & (1 << macro)) == 0) { continue; }
            if (first < 0) { first = macro; }
            ++count;
        }

        const auto text = count > 1 ? "M" + juce::String(first + 1) + "+"
                                    : "MACRO " + juce::String(first + 1);
        const auto fontHeight = juce::jlimit(6.0f, 8.5f, radius * 0.36f);

        // Above the spindle, where the CC label sits below it, so a knob that
        // is both macro-driven and CC-mapped shows both without overlap.
        g.setFont(juce::FontOptions(fontHeight));
        const auto textWidth = juce::jmin(bounds.getWidth() - 4.0f,
                                          juce::GlyphArrangement::getStringWidth(
                                              g.getCurrentFont(), text) + 8.0f);

        const auto plate = juce::Rectangle<float>(textWidth, fontHeight + 4.0f)
                               .withCentre({ center.x, center.y - radius * 0.52f });

        // On a plate rather than straight onto the knob. A knob is busy and
        // mostly dark, with a ring and a pointer moving over it; light chip,
        // dark text stays readable across all of that.
        g.setColour(renderGrayscale ? juce::Colour::fromRGBA(232, 232, 232, 200)
                                    : colours.macroLabelBackground);
        g.fillRoundedRectangle(plate, plate.getHeight() * 0.5f);

        g.setColour(renderGrayscale ? juce::Colour::fromRGB(40, 40, 40) : colours.macroLabelText);
        g.drawFittedText(text, plate.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    if (midiCc >= 0)
    {
        // Inside the knob, under the spindle, where no readout sits. Small and
        // quiet: it has to say "this is on a controller" at a glance without
        // competing with the value the knob is actually showing.
        const auto text = "CC" + juce::String(midiCc);
        const auto fontHeight = juce::jlimit(6.5f, 9.0f, radius * 0.40f);
        const auto label = juce::Rectangle<float>(bounds.getX(),
                                                  center.y + radius * 0.30f,
                                                  bounds.getWidth(),
                                                  fontHeight + 2.0f);

        g.setFont(juce::FontOptions(fontHeight));
        const auto ccColour = renderGrayscale
                                  ? juce::Colour::fromRGBA(200, 200, 200, 190)
                                  : paleSubstrate
                                        // The same amber, taken dark enough to
                                        // read on a white cap. Bright amber on
                                        // pale grey is legible only at a size
                                        // this label never gets.
                                        ? juce::Colour::fromRGBA(146, 96, 6, 255)
                                        : juce::Colour::fromRGBA(255, 214, 92, 225);
        g.setColour(ccColour);
        g.drawFittedText(text, label.toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }
}

} // namespace px3::ui
