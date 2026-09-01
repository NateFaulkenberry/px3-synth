#include "PianoKeyboard.h"

#include "RoundedRect.h"
#include "UIConfig.h"

#include <algorithm>
#include <cmath>

PianoKeyboard::PianoKeyboard()
{
    activeNotes.fill(false);
    previousActiveNotes.fill(false);
    noteVelocities.fill(0.0f);
    startTimerHz(60);
}

void PianoKeyboard::setActiveNotes(const std::array<bool, PianoKeyboard::totalKeys>& noteStates,
                                   const std::array<float, PianoKeyboard::totalKeys>& velocities)
{
    if (activeNotes != noteStates || noteVelocities != velocities)
    {
        activeNotes = noteStates;
        noteVelocities = velocities;
        repaint();
    }
}

void PianoKeyboard::paintKeyboard(juce::Graphics& g)
{
    // Only the keyboard's own rectangle is filled. fillAll would paint the
    // spark headroom too, which sits over the panel above.
    g.setColour(style.background.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, style.backgroundOpacity)));
    px3::ui::fillRounded(g, keyboardArea().toFloat(), style.backgroundRadius);

    const auto area = keyboardArea().toFloat().reduced(style.padding);
    const auto whiteKeyWidth = area.getWidth() / static_cast<float>(whiteKeys);
    const auto whiteKeyHeight = area.getHeight();
    const auto blackKeyWidth = whiteKeyWidth * style.blackWidthRatio;
    const auto blackKeyHeight = whiteKeyHeight * style.blackHeightRatio;

    std::vector<KeyGeometry> whites;
    std::vector<KeyGeometry> blacks;
    whites.reserve(whiteKeys);
    blacks.reserve(totalKeys - whiteKeys);

    for (int midiNote = firstMidiNote; midiNote <= lastMidiNote; ++midiNote)
    {
        const auto noteIsBlack = isBlackKey(midiNote);
        const auto whiteIndex = whiteKeyIndex(midiNote);

        if (noteIsBlack)
        {
            const auto centerX = area.getX() + static_cast<float>(whiteIndex) * whiteKeyWidth;
            const juce::Rectangle<float> blackRect(centerX - blackKeyWidth * 0.5f,
                                                   area.getY(),
                                                   blackKeyWidth,
                                                   blackKeyHeight);

            blacks.push_back({ midiNote, true, blackRect });
        }
        else
        {
            const auto x = area.getX() + static_cast<float>(whiteIndex) * whiteKeyWidth;
            const juce::Rectangle<float> whiteRect(x,
                                                   area.getY(),
                                                   whiteKeyWidth,
                                                   whiteKeyHeight);

            whites.push_back({ midiNote, false, whiteRect });
        }
    }

    for (const auto& key : whites)
    {
        const auto noteIndex = key.midiNote - firstMidiNote;
        const auto isActive = activeNotes[static_cast<std::size_t>(noteIndex)];
        const auto shakeX = isActive ? std::sin(vibrationPhase * 10.5f + static_cast<float>(key.midiNote) * 0.73f) * 1.3f : 0.0f;
        const auto shakeY = isActive ? std::cos(vibrationPhase * 8.1f + static_cast<float>(key.midiNote) * 0.51f) * 0.45f : 0.0f;
        const auto drawBounds = key.bounds.translated(shakeX, shakeY);

        g.setColour(isActive ? style.whiteActiveFill : style.whiteFill);
        px3::ui::fillRounded(g, drawBounds, style.whiteRadius);

        g.setColour(style.whiteBorder);
        px3::ui::drawRounded(g, drawBounds, style.whiteRadius, style.whiteBorderWidth);

        const auto semitone = key.midiNote % 12;
        const auto labelC = semitone == 0;
        const auto labelAEdges = key.midiNote == firstMidiNote || key.midiNote == lastMidiNote;

        if (labelC || labelAEdges)
        {
            g.setColour(style.labelColour);
            g.setFont(juce::FontOptions(style.labelSize));
            g.drawText(noteNameFor(key.midiNote),
                       drawBounds.withTrimmedTop(drawBounds.getHeight() - 18.0f).toNearestInt(),
                       juce::Justification::centred);
        }
    }

    for (const auto& key : blacks)
    {
        const auto noteIndex = key.midiNote - firstMidiNote;
        const auto isActive = activeNotes[static_cast<std::size_t>(noteIndex)];
        const auto shakeX = isActive ? std::sin(vibrationPhase * 11.7f + static_cast<float>(key.midiNote) * 0.57f) * 0.9f : 0.0f;
        const auto shakeY = isActive ? std::cos(vibrationPhase * 9.6f + static_cast<float>(key.midiNote) * 0.63f) * 0.35f : 0.0f;
        const auto drawBounds = key.bounds.translated(shakeX, shakeY);

        g.setColour(isActive ? style.blackActiveFill : style.blackFill);
        px3::ui::fillRounded(g, drawBounds, style.blackRadius);

        g.setColour(style.blackBorder);
        px3::ui::drawRounded(g, drawBounds, style.blackRadius, style.blackBorderWidth);
    }

}

// The keys occupy the whole component again. They stopped doing so when the
// sparks needed room to spill into; now that the sparks are drawn by the
// overlay above, the component is just the instrument.
// Sparks are painted by the shared overlay above the keyboard row, not by the
// keyboard itself.
//
// Two components were each spilling particles over the other - the keys over
// the wheels, the wheels over the keys - and z-order can only ever favour one,
// so whichever went in front hid the other's particles behind its own opaque
// face. A single transparent layer above both is the only arrangement where
// neither loses.
//
// `offset` translates from this component's coordinates into the overlay's.
// The box the live sparks occupy. The overlay repaints only this, rather than
// the whole strip: it is transparent, so repainting it costs a redraw of
// everything beneath it too - measured at 3.6 ms for the full region, which is
// 22% of a 60 Hz frame spent redrawing a mixer panel that did not change.
//
// Each spark contributes its OWN reach rather than a shared worst case. A bolt
// is drawn forward from its position over four segments of its own length, with
// its own zigzag either side and its own stroke width, so a single constant is
// either too small for the biggest bolt or far too large for the rest. It was
// too small - 56 px against a true maximum of 62.9 - and the far end of a long
// bolt was drawn outside the region that ever gets erased, which is precisely
// how a fragment gets left behind on screen.
juce::Rectangle<float> PianoKeyboard::sparkBounds() const
{
    if (sparks.empty())
    {
        return {};
    }

    juce::Rectangle<float> bounds;
    auto first = true;

    for (const auto& spark : sparks)
    {
        // Four segments along the direction of travel, plus the zigzag either
        // side of it and half the outer stroke. Taken as a radius because the
        // direction is arbitrary.
        const auto reach = spark.segmentLength * 4.0f
                           + spark.zigzagAmplitude
                           + spark.width * 1.45f * 0.5f
                           + 2.0f;

        const auto box = juce::Rectangle<float>(spark.position, spark.position).expanded(reach);
        bounds = first ? box : bounds.getUnion(box);
        first = false;
    }

    return bounds;
}

void PianoKeyboard::paintSparksInto(juce::Graphics& g, juce::Point<int> offset) const
{
    if (sparks.empty())
    {
        return;
    }

    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::translation(static_cast<float>(offset.getX()),
                                                      static_cast<float>(offset.getY())));

    for (const auto& spark : sparks)
    {
        const auto lifeDivisor = (spark.maxLifetimeSeconds > 0.0001f) ? spark.maxLifetimeSeconds : 0.0001f;
        auto lifeNorm = spark.lifetimeSeconds / lifeDivisor;
        lifeNorm = (lifeNorm < 0.0f) ? 0.0f : lifeNorm;
        lifeNorm = (lifeNorm > 1.0f) ? 1.0f : lifeNorm;
        const auto alpha = lifeNorm;
        const auto colour = spark.colour.withMultipliedAlpha(alpha);

        const auto velocityLen = std::sqrt(spark.velocity.getX() * spark.velocity.getX()
                                           + spark.velocity.getY() * spark.velocity.getY());
        if (velocityLen <= 0.0001f)
        {
            continue;
        }

        const auto direction = spark.velocity / velocityLen;
        const auto perp = juce::Point<float>(-direction.getY(), direction.getX());

        juce::Path bolt;
        bolt.startNewSubPath(spark.position);

        auto current = spark.position;
        for (int seg = 0; seg < 4; ++seg)
        {
            const auto zigSign = (seg % 2 == 0) ? 1.0f : -1.0f;
            const auto zigAmount = spark.zigzagAmplitude * zigSign * (0.85f + 0.15f * lifeNorm);
            const auto next = current + (direction * spark.segmentLength) + (perp * zigAmount);
            bolt.lineTo(next);
            current = next;
        }

        g.setColour(colour.withMultipliedAlpha(0.42f));
        const auto outerWidth = (spark.width * 1.45f * alpha > 1.0f) ? spark.width * 1.45f * alpha : 1.0f;
        g.strokePath(bolt, juce::PathStrokeType(outerWidth));

        g.setColour(colour);
        const auto innerWidth = (spark.width * alpha > 0.9f) ? spark.width * alpha : 0.9f;
        g.strokePath(bolt, juce::PathStrokeType(innerWidth));

        g.setColour(colour.withMultipliedAlpha(0.65f * alpha));
        g.fillEllipse(spark.position.getX() - 1.1f, spark.position.getY() - 1.1f, 2.2f, 2.2f);
    }
}

namespace
{
// Reads a colour only when the key is actually present, so an absent key keeps
// the compiled default instead of being overwritten by a fallback.
juce::Colour styleColour(const UIConfig* config, const juce::String& path, juce::Colour fallback)
{
    if (config == nullptr || config->getValue(path).isVoid())
    {
        return fallback;
    }
    return config->getColour(path, fallback);
}

float styleFloat(const UIConfig* config, const juce::String& path, float fallback)
{
    if (config == nullptr || config->getValue(path).isVoid())
    {
        return fallback;
    }
    return config->getFloat(path, fallback);
}
} // namespace

PianoKeyboard::Style PianoKeyboard::Style::fromConfig(const UIConfig* config, const juce::String& prefix)
{
    Style s;
    if (config == nullptr)
    {
        return s;
    }

    s.background = styleColour(config, prefix + ".background.color", s.background);
    s.backgroundOpacity = styleFloat(config, prefix + ".background.opacity", s.backgroundOpacity);
    s.backgroundRadius = px3::ui::CornerRadii::fromConfig(config, prefix + ".background", s.backgroundRadius);
    s.padding = styleFloat(config, prefix + ".padding", s.padding);

    s.whiteFill = styleColour(config, prefix + ".whiteKey.fill", s.whiteFill);
    s.whiteActiveFill = styleColour(config, prefix + ".whiteKey.activeFill", s.whiteActiveFill);
    s.whiteBorder = styleColour(config, prefix + ".whiteKey.border.color", s.whiteBorder);
    s.whiteBorderWidth = styleFloat(config, prefix + ".whiteKey.border.width", s.whiteBorderWidth);
    s.whiteRadius = px3::ui::CornerRadii::fromConfig(config, prefix + ".whiteKey.border", s.whiteRadius);

    s.blackFill = styleColour(config, prefix + ".blackKey.fill", s.blackFill);
    s.blackActiveFill = styleColour(config, prefix + ".blackKey.activeFill", s.blackActiveFill);
    s.blackBorder = styleColour(config, prefix + ".blackKey.border.color", s.blackBorder);
    s.blackBorderWidth = styleFloat(config, prefix + ".blackKey.border.width", s.blackBorderWidth);
    s.blackRadius = px3::ui::CornerRadii::fromConfig(config, prefix + ".blackKey.border", s.blackRadius);
    s.blackWidthRatio = styleFloat(config, prefix + ".blackKey.widthRatio", s.blackWidthRatio);
    s.blackHeightRatio = styleFloat(config, prefix + ".blackKey.heightRatio", s.blackHeightRatio);

    s.labelColour = styleColour(config, prefix + ".label.color", s.labelColour);
    s.labelSize = styleFloat(config, prefix + ".label.fontSize", s.labelSize);

    s.silencedVeil = styleColour(config, prefix + ".silencedVeil", s.silencedVeil);
    s.whiteSparkColour = styleColour(config, prefix + ".sparks.whiteKeyColor", s.whiteSparkColour);
    s.blackSparkColour = styleColour(config, prefix + ".sparks.blackKeyColor", s.blackSparkColour);
    return s;
}

void PianoKeyboard::setStyle(const Style& newStyle)
{
    style = newStyle;
    repaint();
}

juce::Rectangle<int> PianoKeyboard::keyboardArea() const
{
    return getLocalBounds();
}

void PianoKeyboard::setWarningStyle(const WarningStyle& newWarningStyle)
{
    warningStyle = newWarningStyle;
    repaint();
}

void PianoKeyboard::setSilenced(bool shouldBeSilenced)
{
    if (silenced == shouldBeSilenced)
    {
        return;
    }

    silenced = shouldBeSilenced;

    if (silenced)
    {
        // Drop everything in flight. A key held when the last oscillator was
        // bypassed would otherwise stay lit under the grey, and its sparks
        // would keep animating over a keyboard that can no longer sound.
        sparks.clear();
        activeNotes.fill(false);
        previousActiveNotes.fill(false);
        noteVelocities.fill(0.0f);
        heldMidiNote = -1;

        // The sparks are drawn on the overlay above, so clearing them here and
        // repainting THIS component leaves the last frame of them on screen
        // over a greyed-out keyboard. The overlay has to be told.
        if (onSparksChanged != nullptr)
        {
            onSparksChanged();
        }
        hadSparksLastFrame = false;
    }

    // The timer is deliberately NOT stopped and restarted here. A component
    // that turns its own clock off can only come back if something turns it
    // on again, and that made the keyboard's liveness depend on a state
    // machine outside it staying in step. It keeps ticking; timerCallback
    // does nothing while silenced, which costs a comparison per frame and
    // cannot strand the keyboard.

    setMouseCursor(silenced ? juce::MouseCursor::NormalCursor
                            : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void PianoKeyboard::setNotice(juce::String text)
{
    if (notice == text) { return; }

    notice = std::move(text);
    repaint();
}

void PianoKeyboard::paint(juce::Graphics& g)
{
    // Drawn once normally; when silenced the same drawing is taken into an
    // image, desaturated and dimmed, so the grey version cannot drift from the
    // live one.
    if (! silenced)
    {
        paintKeyboard(g);
    }

    // A live keyboard with something to say: the same banner, drawn over keys
    // that still work. The silenced path below draws its own.
    if (! silenced)
    {
        if (notice.isNotEmpty())
        {
            paintBanner(g, notice);
        }

        return;
    }

    // Grey, then dim: desaturating alone still reads as a live keyboard, and
    // the point is that nothing here can make a sound.
    {
        juce::Image shot(juce::Image::ARGB, juce::jmax(1, getWidth()), juce::jmax(1, getHeight()), true);
        {
            juce::Graphics ig(shot);
            paintKeyboard(ig);
        }
        shot.desaturate();
        g.setOpacity(1.0f);
        g.drawImageAt(shot, 0, 0);
        g.setColour(style.silencedVeil);
        px3::ui::fillRounded(g, keyboardArea().toFloat(), style.backgroundRadius);
    }

    // ---- the warning ------------------------------------------------------
    // The notice wins while it is showing: Select Mode is the thing the user
    // is doing right now, where "engage an oscillator" is a standing state.
    paintBanner(g, notice.isNotEmpty() ? notice : warningStyle.text);
}

PianoKeyboard::BannerFit PianoKeyboard::debugBannerFit(const juce::String& text)
{
    // Draws the banner for real and reports what the DRAWING decided.
    //
    // The first version of this recomputed the box width alongside paintBanner,
    // which meant it reproduced whatever paintBanner did - including sizing the
    // box to the wrong string, the exact bug it existed to catch. The width
    // below is the one the drawing used; only the text measurement is the
    // test's own, and that is the comparison that matters.
    juce::Image scratch(juce::Image::ARGB,
                        juce::jmax(1, getWidth()),
                        juce::jmax(1, getHeight()),
                        true);
    {
        juce::Graphics g(scratch);
        paintBanner(g, text);
    }

    BannerFit fit;
    const juce::Font font(juce::FontOptions(warningStyle.fontSize, juce::Font::bold));
    fit.textWidth = juce::GlyphArrangement::getStringWidth(font, text);
    fit.paddingWidth = warningStyle.padding.horizontal();
    fit.boxWidth = lastBannerBoxWidth;
    return fit;
}

void PianoKeyboard::paintBanner(juce::Graphics& g, const juce::String& text)
{
    const auto host = warningStyle.margin.shrink(keyboardArea().toFloat());
    if (host.isEmpty())
    {
        return;
    }

    g.setFont(juce::FontOptions(warningStyle.fontSize, juce::Font::bold));

    // Sized to the text being DRAWN, not to the configured warning string.
    // The banner started life showing one fixed message, so measuring that one
    // was the same thing; it stopped being the same thing the moment MIDI and
    // macro assignment started putting their own, longer messages in it, and
    // those were squeezed and then cut off.
    const auto textWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text);
    const auto boxWidth = juce::jmin(host.getWidth(),
                                     textWidth + warningStyle.padding.horizontal());
    const auto boxHeight = juce::jmin(host.getHeight(),
                                      warningStyle.fontSize + warningStyle.padding.vertical());

    lastBannerBoxWidth = boxWidth;

    auto box = juce::Rectangle<float>(boxWidth, boxHeight).withY(host.getCentreY() - boxHeight * 0.5f);
    if (warningStyle.alignment == juce::Justification::left)
    {
        box.setX(host.getX());
    }
    else if (warningStyle.alignment == juce::Justification::right)
    {
        box.setX(host.getRight() - boxWidth);
    }
    else
    {
        box.setX(host.getCentreX() - boxWidth * 0.5f);
    }

    g.setColour(warningStyle.background);
    g.fillRoundedRectangle(box, warningStyle.cornerRadius);

    if (warningStyle.borderWidth > 0.0f)
    {
        g.setColour(warningStyle.border);
        g.drawRoundedRectangle(box, warningStyle.cornerRadius, warningStyle.borderWidth);
    }

    g.setColour(warningStyle.textColour);

    // The box fits the text at full size unless the keyboard itself is too
    // narrow for it, where the box is clamped to the available width. Shrink
    // further rather than truncate: a message read at 70% is a message read.
    g.drawFittedText(text,
                     warningStyle.padding.shrink(box).toNearestInt(),
                     juce::Justification::centred,
                     1,
                     0.7f);

}

void PianoKeyboard::mouseDown(const juce::MouseEvent& event)
{
    if (silenced)
    {
        // Nothing here can make a sound, so clicking a key must not pretend
        // otherwise - no note, no lightning, no lit key.
        return;
    }

    const auto note = midiNoteAt(event.position);
    if (note < firstMidiNote || note > lastMidiNote)
    {
        return;
    }

    heldMidiNote = note;
    if (onNoteOn)
    {
        onNoteOn(note, clickVelocityNorm);
    }
}

void PianoKeyboard::mouseDrag(const juce::MouseEvent& event)
{
    if (silenced)
    {
        // Nothing here can make a sound, so clicking a key must not pretend
        // otherwise - no note, no lightning, no lit key.
        return;
    }

    const auto note = midiNoteAt(event.position);
    if (note == heldMidiNote)
    {
        return;
    }

    if (heldMidiNote >= firstMidiNote && heldMidiNote <= lastMidiNote && onNoteOff)
    {
        onNoteOff(heldMidiNote);
    }

    heldMidiNote = -1;

    if (note >= firstMidiNote && note <= lastMidiNote)
    {
        heldMidiNote = note;
        if (onNoteOn)
        {
            onNoteOn(note, clickVelocityNorm);
        }
    }
}

void PianoKeyboard::mouseUp(const juce::MouseEvent&)
{
    if (heldMidiNote >= firstMidiNote && heldMidiNote <= lastMidiNote && onNoteOff)
    {
        onNoteOff(heldMidiNote);
    }

    heldMidiNote = -1;
}

void PianoKeyboard::mouseExit(const juce::MouseEvent&)
{
    if (heldMidiNote >= firstMidiNote && heldMidiNote <= lastMidiNote && onNoteOff)
    {
        onNoteOff(heldMidiNote);
    }

    heldMidiNote = -1;
}

void PianoKeyboard::timerCallback()
{
    if (silenced)
    {
        // Nothing to advance: no sparks, no held keys, no vibration. But if
        // anything was drawn on the overlay last frame it still has to be
        // cleared, or it stays there for as long as the keyboard is silent.
        if (hadSparksLastFrame && onSparksChanged != nullptr)
        {
            onSparksChanged();
            hadSparksLastFrame = false;
        }
        return;
    }


    constexpr float dt = 1.0f / 60.0f;

    vibrationPhase += dt;

    bool anyActive = false;

    for (int midiNote = firstMidiNote; midiNote <= lastMidiNote; ++midiNote)
    {
        const auto index = static_cast<std::size_t>(midiNote - firstMidiNote);
        const auto isActive = activeNotes[index];
        const auto velocityNorm = juce::jlimit(0.0f, 1.0f, noteVelocities[index]);

        if (isActive)
        {
            anyActive = true;
        }

        if (isActive && !previousActiveNotes[index])
        {
            spawnLightningBurst(midiNote, isBlackKey(midiNote), velocityNorm);
        }

        const auto sustainSpawnChance = 0.05f + 0.35f * velocityNorm;

        if (isActive && rng.nextFloat() < sustainSpawnChance)
        {
            spawnLightningBurst(midiNote, isBlackKey(midiNote), velocityNorm);
        }

        previousActiveNotes[index] = isActive;
    }

    for (auto& spark : sparks)
    {
        spark.position += spark.velocity;
        spark.velocity *= 0.93f;
        spark.lifetimeSeconds -= dt;
    }

    sparks.erase(std::remove_if(sparks.begin(),
                                sparks.end(),
                                [](const Spark& spark) { return spark.lifetimeSeconds <= 0.0f; }),
                 sparks.end());

    if (sparks.size() > 450)
    {
        sparks.erase(sparks.begin(), sparks.begin() + static_cast<std::ptrdiff_t>(sparks.size() - 450));
    }

    if (anyActive)
    {
        repaint();
    }

    // The sparks live in the overlay above, so a frame that only moved them
    // repaints that rather than the keyboard.
    //
    // Reported for one frame AFTER the last spark dies as well, or the overlay
    // is never told to clear the final one and it stays on screen.
    if ((! sparks.empty() || hadSparksLastFrame) && onSparksChanged != nullptr)
    {
        onSparksChanged();
    }
    hadSparksLastFrame = ! sparks.empty();
}

void PianoKeyboard::spawnLightningBurst(int midiNote, bool keyIsBlack, float velocityNorm)
{
    juce::Rectangle<float> keyBounds;
    bool actualBlack = false;

    if (!getKeyBoundsForNote(midiNote, keyBounds, actualBlack))
    {
        return;
    }

    juce::ignoreUnused(keyIsBlack);

    const auto intensity = juce::jlimit(0.1f, 1.0f, velocityNorm);

    const auto colour = actualBlack ? style.blackSparkColour : style.whiteSparkColour;

    const auto sparkCount = juce::jlimit(1, 6, static_cast<int>(std::lround(1.0 + intensity * 5.0)));

    for (int i = 0; i < sparkCount; ++i)
    {
        const auto centerX = keyBounds.getCentreX();
        const auto centerY = keyBounds.getCentreY();
        const auto spreadX = keyBounds.getWidth() * (actualBlack ? 0.95f : 0.80f);
        const auto spreadY = keyBounds.getHeight() * (actualBlack ? 0.62f : 0.52f);

        const auto spawnX = centerX + (rng.nextFloat() * 2.0f - 1.0f) * spreadX;
        const auto spawnY = centerY + (rng.nextFloat() * 2.0f - 1.0f) * spreadY;

        const auto angle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        const auto speed = (1.4f + rng.nextFloat() * 4.2f) * (0.55f + intensity * 0.95f);

        Spark spark;
        spark.position = { spawnX, spawnY };
        spark.velocity = { std::cos(angle) * speed, std::sin(angle) * speed };
        spark.colour = colour;
        spark.lifetimeSeconds = (0.10f + rng.nextFloat() * 0.24f) * (0.62f + intensity * 0.70f);
        spark.maxLifetimeSeconds = spark.lifetimeSeconds;
        spark.width = (0.9f + rng.nextFloat() * 2.0f) * (0.55f + intensity * 0.85f);
        spark.segmentLength = (4.0f + rng.nextFloat() * 7.0f) * (0.60f + intensity * 0.75f);
        spark.zigzagAmplitude = (1.2f + rng.nextFloat() * 4.2f) * (0.55f + intensity * 0.90f);
        sparks.push_back(spark);
    }
}

bool PianoKeyboard::getKeyBoundsForNote(int midiNote, juce::Rectangle<float>& bounds, bool& isBlack) const
{
    if (midiNote < firstMidiNote || midiNote > lastMidiNote)
    {
        return false;
    }

    const auto area = keyboardArea().toFloat().reduced(style.padding);
    const auto whiteKeyWidth = area.getWidth() / static_cast<float>(whiteKeys);
    const auto whiteKeyHeight = area.getHeight();
    const auto blackKeyWidth = whiteKeyWidth * style.blackWidthRatio;
    const auto blackKeyHeight = whiteKeyHeight * style.blackHeightRatio;

    isBlack = isBlackKey(midiNote);
    const auto whiteIndex = whiteKeyIndex(midiNote);

    if (isBlack)
    {
        const auto centerX = area.getX() + static_cast<float>(whiteIndex) * whiteKeyWidth;
        bounds = juce::Rectangle<float>(centerX - blackKeyWidth * 0.5f,
                                        area.getY(),
                                        blackKeyWidth,
                                        blackKeyHeight);
    }
    else
    {
        const auto x = area.getX() + static_cast<float>(whiteIndex) * whiteKeyWidth;
        bounds = juce::Rectangle<float>(x,
                                        area.getY(),
                                        whiteKeyWidth,
                                        whiteKeyHeight);
    }

    return true;
}

int PianoKeyboard::midiNoteAt(juce::Point<float> position) const
{
    const auto area = keyboardArea().toFloat().reduced(style.padding);
    if (!area.contains(position))
    {
        return -1;
    }

    // Prioritize black keys since they sit visually above white keys.
    for (int midiNote = firstMidiNote; midiNote <= lastMidiNote; ++midiNote)
    {
        if (!isBlackKey(midiNote))
        {
            continue;
        }

        juce::Rectangle<float> bounds;
        bool isBlack = false;
        if (getKeyBoundsForNote(midiNote, bounds, isBlack) && bounds.contains(position))
        {
            return midiNote;
        }
    }

    for (int midiNote = firstMidiNote; midiNote <= lastMidiNote; ++midiNote)
    {
        if (isBlackKey(midiNote))
        {
            continue;
        }

        juce::Rectangle<float> bounds;
        bool isBlack = false;
        if (getKeyBoundsForNote(midiNote, bounds, isBlack) && bounds.contains(position))
        {
            return midiNote;
        }
    }

    return -1;
}

bool PianoKeyboard::isBlackKey(int midiNote)
{
    const auto semitone = midiNote % 12;

    return semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
}

int PianoKeyboard::whiteKeyIndex(int midiNote)
{
    int whiteCount = 0;

    for (int n = firstMidiNote; n < midiNote; ++n)
    {
        if (!isBlackKey(n))
        {
            ++whiteCount;
        }
    }

    return whiteCount;
}

juce::String PianoKeyboard::noteNameFor(int midiNote)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto octave = (midiNote / 12) - 1;

    return juce::String(names[midiNote % 12]) + juce::String(octave);
}
