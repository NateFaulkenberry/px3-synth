#include "PianoKeyboard.h"

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
    g.fillAll(juce::Colour::fromRGB(25, 25, 25));
    const auto area = getLocalBounds().toFloat().reduced(8.0f);
    const auto whiteKeyWidth = area.getWidth() / static_cast<float>(whiteKeys);
    const auto whiteKeyHeight = area.getHeight();
    const auto blackKeyWidth = whiteKeyWidth * 0.64f;
    const auto blackKeyHeight = whiteKeyHeight * 0.62f;

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

        g.setColour(isActive ? juce::Colour::fromRGB(255, 220, 120) : juce::Colour::fromRGB(245, 245, 240));
        g.fillRect(drawBounds);

        g.setColour(juce::Colour::fromRGB(50, 50, 50));
        g.drawRect(drawBounds, 1.0f);

        const auto semitone = key.midiNote % 12;
        const auto labelC = semitone == 0;
        const auto labelAEdges = key.midiNote == firstMidiNote || key.midiNote == lastMidiNote;

        if (labelC || labelAEdges)
        {
            g.setColour(juce::Colour::fromRGB(70, 70, 70));
            g.setFont(juce::FontOptions(11.0f));
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

        g.setColour(isActive ? juce::Colour::fromRGB(225, 95, 75) : juce::Colour::fromRGB(18, 18, 18));
        g.fillRoundedRectangle(drawBounds, 2.5f);

        g.setColour(juce::Colour::fromRGB(0, 0, 0));
        g.drawRoundedRectangle(drawBounds, 2.5f, 1.0f);
    }

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

void PianoKeyboard::setWarningStyle(const WarningStyle& style)
{
    warningStyle = style;
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
        stopTimer();
    }
    else
    {
        startTimerHz(60);
    }

    setMouseCursor(silenced ? juce::MouseCursor::NormalCursor
                            : juce::MouseCursor::PointingHandCursor);
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
    if (! silenced)
    {
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
        g.setColour(juce::Colour::fromRGBA(0, 0, 0, 110));
        g.fillRect(getLocalBounds());
    }

    // ---- the warning ------------------------------------------------------
    const auto host = warningStyle.margin.shrink(getLocalBounds().toFloat());
    if (host.isEmpty())
    {
        return;
    }

    g.setFont(juce::FontOptions(warningStyle.fontSize, juce::Font::bold));
    const auto textWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), warningStyle.text);
    const auto boxWidth = juce::jmin(host.getWidth(),
                                     textWidth + warningStyle.padding.horizontal());
    const auto boxHeight = juce::jmin(host.getHeight(),
                                      warningStyle.fontSize + warningStyle.padding.vertical());

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
    g.drawFittedText(warningStyle.text,
                     warningStyle.padding.shrink(box).toNearestInt(),
                     juce::Justification::centred,
                     1,
                     0.8f);

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

    if (anyActive || !sparks.empty())
    {
        repaint();
    }
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

    const auto colour = actualBlack ? juce::Colour::fromRGB(225, 95, 75)
                                    : juce::Colour::fromRGB(255, 220, 120);

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

    const auto area = getLocalBounds().toFloat().reduced(8.0f);
    const auto whiteKeyWidth = area.getWidth() / static_cast<float>(whiteKeys);
    const auto whiteKeyHeight = area.getHeight();
    const auto blackKeyWidth = whiteKeyWidth * 0.64f;
    const auto blackKeyHeight = whiteKeyHeight * 0.62f;

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
    const auto area = getLocalBounds().toFloat().reduced(8.0f);
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
