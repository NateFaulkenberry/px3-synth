#include "PianoKeyboard.h"

#include "RoundedRect.h"
#include "UIConfig.h"

#include <algorithm>
#include <cmath>

PianoKeyboard::PianoKeyboard()
{
    activeNotes.fill(false);
    noteVelocities.fill(0.0f);
    // Only to notice a press whose mouse-up never arrived (see timerCallback).
    startTimerHz(10);
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

juce::Rectangle<float> PianoKeyboard::keysArea() const
{
    // Whole pixels, so the last key's edge and the cheek meet on one.
    return keyboardArea().toFloat().reduced(style.padding)
        .withTrimmedRight(std::round(juce::jmax(0.0f, style.cheekRight)));
}

void PianoKeyboard::paintEndCheek(juce::Graphics& g, juce::Rectangle<float> cheek, bool innerFaceOnLeft)
{
    // A raised block lit along its top and along the face that meets the
    // instrument, like the moulded end of a keyboard's case. The same block,
    // mirrored, ends both sides of the bottom row.
    juce::ColourGradient face(juce::Colour::fromRGB(48, 52, 57), cheek.getX(), cheek.getY(),
                              juce::Colour::fromRGB(30, 33, 37), cheek.getX(), cheek.getBottom(), false);
    g.setGradientFill(face);
    g.fillRect(cheek);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.fillRect(cheek.withHeight(1.0f));
    const auto shadow = innerFaceOnLeft ? cheek.withWidth(1.0f) : cheek.withLeft(cheek.getRight() - 1.0f);
    const auto lit = innerFaceOnLeft ? cheek.withTrimmedLeft(1.0f).withWidth(1.0f)
                                     : cheek.withTrimmedRight(1.0f).withLeft(cheek.getRight() - 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRect(shadow);
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.fillRect(lit);
}

void PianoKeyboard::paintKeyboard(juce::Graphics& g)
{
    g.setColour(style.background.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, style.backgroundOpacity)));
    px3::ui::fillRounded(g, keyboardArea().toFloat(), style.backgroundRadius);

    const auto area = keysArea();
    const auto whiteKeyWidth = area.getWidth() / static_cast<float>(whiteKeys);
    const auto whiteKeyHeight = area.getHeight();
    const auto blackKeyWidth = whiteKeyWidth * style.blackWidthRatio;
    const auto blackKeyHeight = whiteKeyHeight * style.blackHeightRatio;

    // The end cheek: a raised block from the last key to the keyboard's edge,
    // lit along its top and its inner face like the moulded end of a real
    // keyboard's case.
    if (style.cheekRight > 0.0f)
    {
        const auto frame = keyboardArea().toFloat();
        paintEndCheek(g, juce::Rectangle<float>(area.getRight(), frame.getY(),
                                                frame.getRight() - area.getRight(), frame.getHeight()),
                      true);
    }

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
        // A held key is pressed in and lit: it drops a pixel from the key
        // rail and the rail casts a short shadow onto it. Still - nothing
        // moves while it is held.
        const auto drawBounds = isActive ? key.bounds.withTrimmedTop(1.0f) : key.bounds;

        g.setColour(isActive ? style.whiteActiveFill : style.whiteFill);
        px3::ui::fillRounded(g, drawBounds, style.whiteRadius);
        if (isActive) { paintPressShadow(g, drawBounds); }

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
        // Pressed, a black key's front tips down out of view: it reads a
        // little shorter from above.
        const auto drawBounds = isActive ? key.bounds.withTrimmedTop(1.0f).withTrimmedBottom(2.0f) : key.bounds;

        g.setColour(isActive ? style.blackActiveFill : style.blackFill);
        px3::ui::fillRounded(g, drawBounds, style.blackRadius);
        if (isActive) { paintPressShadow(g, drawBounds); }

        g.setColour(style.blackBorder);
        px3::ui::drawRounded(g, drawBounds, style.blackRadius, style.blackBorderWidth);
    }

}

// The key rail's shadow on a key that has gone down.
void PianoKeyboard::paintPressShadow(juce::Graphics& g, juce::Rectangle<float> key)
{
    const auto depth = juce::jmin(5.0f, key.getHeight() * 0.25f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.30f), 0.0f, key.getY(),
                                           juce::Colours::transparentBlack, 0.0f, key.getY() + depth, false));
    g.fillRect(key.withHeight(depth));
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
    s.cheekRight = styleFloat(config, prefix + ".cheek.right", s.cheekRight);

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
        // Drop everything held. A key held when the last oscillator was
        // bypassed would otherwise stay lit under the grey.
        // Released, not just forgotten: dropping the key here without its
        // note-off left the note sounding and the key held for good.
        releaseHeldNote();
        activeNotes.fill(false);
        noteVelocities.fill(0.0f);
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

void PianoKeyboard::releaseHeldNote()
{
    const auto note = heldMidiNote;
    heldMidiNote = -1;

    if (note >= firstMidiNote && note <= lastMidiNote && onNoteOff)
    {
        onNoteOff(note);
    }
}

void PianoKeyboard::mouseUp(const juce::MouseEvent&)
{
    releaseHeldNote();
}

void PianoKeyboard::mouseExit(const juce::MouseEvent&)
{
    releaseHeldNote();
}

void PianoKeyboard::timerCallback()
{
    // A mouse-up the host never delivered - the window losing focus mid-press,
    // a host shortcut taking the click - left the key held and its note on. If
    // no button is actually down any more, the press is over.
    if (heldMidiNote >= 0 && ! juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
    {
        releaseHeldNote();
    }
}

bool PianoKeyboard::getKeyBoundsForNote(int midiNote, juce::Rectangle<float>& bounds, bool& isBlack) const
{
    if (midiNote < firstMidiNote || midiNote > lastMidiNote)
    {
        return false;
    }

    const auto area = keysArea();
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
    const auto area = keysArea();
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
