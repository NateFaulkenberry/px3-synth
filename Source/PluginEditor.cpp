#include "PluginEditor.h"

#include "BinaryData.h"

#include <algorithm>
#include <cmath>

namespace
{
const std::array<const char*, 4> kGroupNames { "OSC", "FILTER", "AMP ENV", "OUTPUT" };
const std::array<juce::Colour, 4> kGroupAccents {
    juce::Colour::fromRGB(74, 153, 255),   // OSC: blue
    juce::Colour::fromRGB(255, 88, 88),    // FILTER: red
    juce::Colour::fromRGB(73, 222, 121),   // AMP ENV: green
    juce::Colour::fromRGB(255, 216, 74)    // OUTPUT: yellow
};
}

void SynthProjectAudioProcessorEditor::KnobLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                                                         int x,
                                                                         int y,
                                                                         int width,
                                                                         int height,
                                                                         float sliderPos,
                                                                         float rotaryStartAngle,
                                                                         float rotaryEndAngle,
                                                                         juce::Slider& slider)
{
    const auto fullBounds = juce::Rectangle<float>(static_cast<float>(x),
                                                   static_cast<float>(y),
                                                   static_cast<float>(width),
                                                   static_cast<float>(height));
    const auto diameter = juce::jmin(fullBounds.getWidth(), fullBounds.getHeight()) - 10.0f;
    const auto bounds = juce::Rectangle<float>(diameter, diameter).withCentre(fullBounds.getCentre());

    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto center = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = slider.isColourSpecified(juce::Slider::rotarySliderFillColourId)
                            ? slider.findColour(juce::Slider::rotarySliderFillColourId)
                            : juce::Colour::fromRGB(234, 166, 76);

    // Drop shadow for more tactile depth.
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 110));
    g.fillEllipse(bounds.translated(0.0f, 3.0f));

    juce::ColourGradient knobGradient(juce::Colour::fromRGB(72, 72, 72),
                                      bounds.getX(),
                                      bounds.getY(),
                                      juce::Colour::fromRGB(34, 34, 34),
                                      bounds.getRight(),
                                      bounds.getBottom(),
                                      false);
    g.setGradientFill(knobGradient);
    g.fillEllipse(bounds);

    // Deterministic micro-grain adds realism without per-frame flicker.
    g.saveState();
    juce::Path grainMask;
    grainMask.addEllipse(bounds.reduced(2.6f));
    g.reduceClipRegion(grainMask);

    for (int i = 0; i < 48; ++i)
    {
        const auto seedA = static_cast<float>(i) * 12.9898f + center.x * 0.37f + center.y * 0.21f;
        const auto seedB = static_cast<float>(i) * 7.913f + center.x * 0.19f + center.y * 0.42f;
        const auto noiseA = std::sin(seedA) * 43758.5453f;
        const auto noiseB = std::sin(seedB) * 24141.1829f;
        const auto fracA = noiseA - std::floor(noiseA);
        const auto fracB = noiseB - std::floor(noiseB);

        const auto theta = fracA * juce::MathConstants<float>::twoPi;
        const auto radial = (0.22f + 0.70f * fracB) * radius;
        const auto dotX = center.x + std::cos(theta) * radial;
        const auto dotY = center.y + std::sin(theta) * radial;
        const auto dotSize = 0.55f + fracB * 0.9f;

        const auto isBright = fracA > 0.5f;
        const auto alpha = static_cast<juce::uint8>(isBright ? (20 + static_cast<int>(fracB * 22.0f))
                                                              : (16 + static_cast<int>(fracB * 18.0f)));
        g.setColour(isBright ? juce::Colour::fromRGBA(255, 255, 255, alpha)
                             : juce::Colour::fromRGBA(0, 0, 0, alpha));
        g.fillEllipse(dotX, dotY, dotSize, dotSize);
    }

    g.restoreState();

    // Top highlight to reinforce 3D curvature.
    juce::ColourGradient highlight(accent.withAlpha(0.42f),
                                   center.x,
                                   bounds.getY(),
                                   accent.withAlpha(0.0f),
                                   center.x,
                                   center.y,
                                   false);
    g.setGradientFill(highlight);
    g.fillEllipse(bounds.reduced(3.5f));

    g.setColour(juce::Colour::fromRGB(110, 110, 110));
    g.drawEllipse(bounds, 1.6f);

    g.setColour(juce::Colour::fromRGB(14, 14, 14));
    g.drawEllipse(bounds.expanded(0.6f), 0.9f);

    juce::Path ring;
    ring.addCentredArc(center.x,
                       center.y,
                       radius * 0.88f,
                       radius * 0.88f,
                       0.0f,
                       rotaryStartAngle,
                       angle,
                       true);
    g.setColour(accent);
    g.strokePath(ring, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addRoundedRectangle(-2.1f, -radius * 0.56f, 4.2f, radius * 0.36f, 1.7f);
    g.setColour(juce::Colour::fromRGB(246, 246, 246));
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(center.x, center.y));

    g.setColour(juce::Colour::fromRGB(210, 210, 210));
    g.fillEllipse(center.x - 3.1f, center.y - 3.1f, 6.2f, 6.2f);
}

void SynthProjectAudioProcessorEditor::KnobLabel::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(2.0f, 1.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 54));
    g.fillRoundedRectangle(area, 7.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 96));
    g.drawRoundedRectangle(area, 7.0f, 1.0f);

    g.setColour(findColour(juce::Label::textColourId));
    g.setFont(getFont());
    g.drawText(getText(),
               area.reduced(8.0f, 0.0f).toNearestInt(),
               juce::Justification::centred,
               true);
}

juce::String SynthProjectAudioProcessorEditor::noteNameForMidi(int midiNote)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    if (midiNote < 0 || midiNote > 127)
    {
        return "-";
    }

    const auto octave = (midiNote / 12) - 1;
    return juce::String(names[midiNote % 12]) + juce::String(octave);
}

void SynthProjectAudioProcessorEditor::configureKnob(KnobBinding& binding,
                                                      const juce::String& labelText,
                                                      juce::AudioParameterFloat& parameter)
{
    binding.parameter = &parameter;

    auto& knob = *binding.slider;
    auto& label = *binding.label;

    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    const auto& range = parameter.getNormalisableRange();
    knob.setRange(range.start, range.end);
    knob.setValue(parameter.get());
    knob.setLookAndFeel(&knobLookAndFeel);
    knob.onValueChange = [&parameter, &knob]()
    {
        parameter.setValueNotifyingHost(parameter.convertTo0to1(static_cast<float>(knob.getValue())));
    };

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(225, 225, 225));
    label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label.setFont(juce::FontOptions(13.0f));
    label.setInterceptsMouseClicks(false, false);

    addAndMakeVisible(knob);
    addAndMakeVisible(label);
}

SynthProjectAudioProcessorEditor::SynthProjectAudioProcessorEditor(SynthProjectAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p)
{
    backgroundImage = juce::ImageFileFormat::loadFrom(BinaryData::pp_png, BinaryData::pp_pngSize);
    logoFrame = juce::ImageFileFormat::loadFrom(BinaryData::px3_gif, BinaryData::px3_gifSize);

    setResizable(true, true);
    setResizeLimits(980, 540, 1900, 900);

    addAndMakeVisible(pianoKeyboard);

    knobBindings = {
        KnobBinding { &oscSineKnob, &oscSineLabel, nullptr },
        KnobBinding { &oscSawKnob, &oscSawLabel, nullptr },
        KnobBinding { &oscSquareKnob, &oscSquareLabel, nullptr },
        KnobBinding { &cutoffKnob, &cutoffLabel, nullptr },
        KnobBinding { &resonanceKnob, &resonanceLabel, nullptr },
        KnobBinding { &attackKnob, &attackLabel, nullptr },
        KnobBinding { &decayKnob, &decayLabel, nullptr },
        KnobBinding { &sustainKnob, &sustainLabel, nullptr },
        KnobBinding { &releaseKnob, &releaseLabel, nullptr },
        KnobBinding { &gainKnob, &gainLabel, nullptr }
    };

    configureKnob(knobBindings[0], "Sine", audioProcessor.getOscSineParam());
    configureKnob(knobBindings[1], "Saw", audioProcessor.getOscSawParam());
    configureKnob(knobBindings[2], "Square", audioProcessor.getOscSquareParam());
    configureKnob(knobBindings[3], "Cutoff", audioProcessor.getFilterCutoffParam());
    configureKnob(knobBindings[4], "Reso", audioProcessor.getFilterResonanceParam());
    configureKnob(knobBindings[5], "Attack", audioProcessor.getAttackParam());
    configureKnob(knobBindings[6], "Decay", audioProcessor.getDecayParam());
    configureKnob(knobBindings[7], "Sustain", audioProcessor.getSustainParam());
    configureKnob(knobBindings[8], "Release", audioProcessor.getReleaseParam());
    configureKnob(knobBindings[9], "Gain", audioProcessor.getMasterGainParam());

    midiStatusLabel.setText("MIDI In: waiting for note...", juce::dontSendNotification);
    midiStatusLabel.setJustificationType(juce::Justification::centred);
    midiStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(236, 172, 88));
    midiStatusLabel.setFont(juce::FontOptions(14.0f));
    midiStatusLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(midiStatusLabel);

    setSize(1320, 700);

    startTimerHz(30);
}

SynthProjectAudioProcessorEditor::~SynthProjectAudioProcessorEditor()
{
    for (auto& binding : knobBindings)
    {
        binding.slider->setLookAndFeel(nullptr);
    }
}

void SynthProjectAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));

    if (backgroundImage.isValid())
    {
        g.drawImageWithin(backgroundImage,
                          0,
                          0,
                          getWidth(),
                          getHeight(),
                          juce::RectanglePlacement::fillDestination,
                          false);

        g.setColour(juce::Colour::fromRGBA(26, 26, 26, 150));
        g.fillAll();
    }

    g.setColour(juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));
    g.fillRoundedRectangle(logoPanelArea.toFloat(), 12.0f);

    if (logoFrame.isValid())
    {
        const auto logoSize = static_cast<float>(juce::jlimit(80, 120, logoPanelArea.getHeight() - 46));
        const auto logoArea = juce::Rectangle<float>(static_cast<float>(logoPanelArea.getX() + 18),
                                 static_cast<float>(logoPanelArea.getY() + 14),
                                 logoSize,
                                 logoSize);
        const auto wobble = anyKeyDown ? std::sin(logoWobblePhase) * 0.075f : 0.0f;
        auto transform = juce::AffineTransform::scale(logoArea.getWidth() / static_cast<float>(logoFrame.getWidth()),
                                                      logoArea.getHeight() / static_cast<float>(logoFrame.getHeight()))
                             .translated(logoArea.getX(), logoArea.getY());
        transform = transform.rotated(static_cast<float>(wobble), logoArea.getCentreX(), logoArea.getCentreY());
        g.drawImageTransformed(logoFrame, transform);

          g.setColour(juce::Colour::fromRGB(232, 232, 232));
          g.setFont(juce::FontOptions(14.0f));
          const auto subtitleArea = juce::Rectangle<int>(logoPanelArea.getX() + 10,
                                         static_cast<int>(std::round(logoArea.getBottom())) + 6,
                                         logoPanelArea.getWidth() - 20,
                                         18);
          g.drawText("Subtractive Synth", subtitleArea, juce::Justification::centred);
    }

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 26));
    g.drawLine(static_cast<float>(controlsArea.getX()),
               static_cast<float>(controlsArea.getY()),
               static_cast<float>(controlsArea.getRight()),
               static_cast<float>(controlsArea.getY()),
               1.0f);

    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));

    for (std::size_t i = 0; i < knobGroupAreas.size(); ++i)
    {
        const auto panel = knobGroupAreas[i].reduced(2);
        const auto accent = kGroupAccents[i];
        const auto panelFloat = panel.toFloat();

        g.setColour(accent.withAlpha(0.16f));
        g.fillRoundedRectangle(panelFloat, 10.0f);

        g.setColour(accent.withAlpha(0.10f));
        g.fillRoundedRectangle(panel.withTrimmedBottom(panel.getHeight() / 2).toFloat(), 10.0f);

        g.setColour(accent.withAlpha(0.78f));
        g.drawRoundedRectangle(panelFloat, 10.0f, 1.0f);

        const auto labelArea = panel.withHeight(24);
        g.setColour(accent.brighter(0.35f));
        g.drawText(kGroupNames[i], labelArea, juce::Justification::centred);
    }
}

void SynthProjectAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(16);

    const auto headerHeight = juce::jlimit(130, 200, getHeight() / 4);
    const auto controlsHeight = juce::jlimit(170, 260, getHeight() / 3);
    const auto statusHeight = 36;

    headerArea = bounds.removeFromTop(headerHeight);
    logoPanelArea = { headerArea.getX() + 4, headerArea.getY() + 4, 160, headerArea.getHeight() - 8 };
    headerPlaceholderArea = { logoPanelArea.getRight() + 12,
                              headerArea.getY(),
                              juce::jmax(0, headerArea.getRight() - (logoPanelArea.getRight() + 12)),
                              headerArea.getHeight() };
    bounds.removeFromTop(10);

    controlsArea = bounds.removeFromTop(controlsHeight);
    bounds.removeFromTop(10);
    bounds.removeFromBottom(10);
    midiStatusArea = bounds.removeFromBottom(statusHeight);
    bounds.removeFromBottom(10);
    pianoKeyboard.setBounds(bounds);
    midiStatusLabel.setBounds(midiStatusArea.withTrimmedLeft(180).withTrimmedRight(180));

    const auto groupGap = 24;
    auto groupsSpan = controlsArea.reduced(8, 8);
    groupsSpan.removeFromTop(30);

    const auto usableWidth = groupsSpan.getWidth() - (groupGap * 3);
    const auto usableWidthD = static_cast<double>(usableWidth);
    const auto oscWidth = static_cast<int>(std::lround(usableWidthD * 0.30));
    const auto filterWidth = static_cast<int>(std::lround(usableWidthD * 0.20));
    const auto envWidth = static_cast<int>(std::lround(usableWidthD * 0.40));
    const auto outWidth = usableWidth - oscWidth - filterWidth - envWidth;

    auto x = groupsSpan.getX();
    knobGroupAreas[0] = { x, groupsSpan.getY(), oscWidth, groupsSpan.getHeight() };
    x += oscWidth + groupGap;
    knobGroupAreas[1] = { x, groupsSpan.getY(), filterWidth, groupsSpan.getHeight() };
    x += filterWidth + groupGap;
    knobGroupAreas[2] = { x, groupsSpan.getY(), envWidth, groupsSpan.getHeight() };
    x += envWidth + groupGap;
    knobGroupAreas[3] = { x, groupsSpan.getY(), outWidth, groupsSpan.getHeight() };

    layoutKnobGroup(knobGroupAreas[0], 0, 3, kGroupAccents[0]);
    layoutKnobGroup(knobGroupAreas[1], 3, 2, kGroupAccents[1]);
    layoutKnobGroup(knobGroupAreas[2], 5, 4, kGroupAccents[2]);
    layoutKnobGroup(knobGroupAreas[3], 9, 1, kGroupAccents[3]);
}

void SynthProjectAudioProcessorEditor::layoutKnobGroup(const juce::Rectangle<int>& groupArea,
                                                        int startIndex,
                                                        int knobCount,
                                                        const juce::Colour& sectionAccent)
{
    if (knobCount <= 0)
    {
        return;
    }

    const auto labelHeight = 22;
    const auto minGap = 8;

    auto area = groupArea.reduced(10, 8);
    area.removeFromTop(30);

    const auto knobTop = area.getY() + labelHeight + 10;
    const auto maxKnobByHeight = juce::jmax(56, area.getHeight() - labelHeight - 8);
    const auto maxKnobByWidth = (area.getWidth() - (knobCount - 1) * minGap) / knobCount;
    const auto baseKnobSize = juce::jlimit(56, 110, juce::jmin(maxKnobByWidth, maxKnobByHeight));
    const auto knobSize = juce::jmax(48, static_cast<int>(std::lround(static_cast<double>(baseKnobSize) * 0.85)));

    const auto totalKnobWidth = knobSize * knobCount;
    const auto dynamicGap = knobCount > 1
                                ? juce::jmax(minGap, (area.getWidth() - totalKnobWidth) / (knobCount - 1))
                                : 0;
    const auto rowWidth = totalKnobWidth + (knobCount - 1) * dynamicGap;
    auto x = area.getX() + (area.getWidth() - rowWidth) / 2;

    for (int i = 0; i < knobCount; ++i)
    {
        auto& binding = knobBindings[static_cast<std::size_t>(startIndex + i)];

        if (binding.slider == nullptr || binding.label == nullptr)
        {
            continue;
        }

        binding.slider->setColour(juce::Slider::rotarySliderFillColourId, sectionAccent);
        binding.label->setBounds(x, area.getY(), knobSize, labelHeight);
        binding.slider->setBounds(x, knobTop, knobSize, knobSize);
        x += knobSize + dynamicGap;
    }
}

void SynthProjectAudioProcessorEditor::refreshAnyKeyDownState()
{
    const auto noteStates = audioProcessor.copyActiveNoteStates();
    const auto noteVelocities = audioProcessor.copyActiveNoteVelocities();
    const auto keyDown = std::any_of(noteStates.begin(), noteStates.end(), [](bool state) { return state; });
    anyKeyDown = keyDown;
    pianoKeyboard.setActiveNotes(noteStates, noteVelocities);
}

void SynthProjectAudioProcessorEditor::timerCallback()
{
    const auto latestStatus = audioProcessor.copyMidiStatus();

    if (latestStatus.noteNumber != midiStatus.noteNumber
        || latestStatus.velocity != midiStatus.velocity
        || latestStatus.noteOn != midiStatus.noteOn)
    {
        midiStatus = latestStatus;
        const auto stateText = midiStatus.noteOn ? "Note On" : "Note Off";
        const auto statusText = midiStatus.noteNumber >= 0
                                    ? juce::String("MIDI In: ") + stateText
                                          + "  Note " + noteNameForMidi(midiStatus.noteNumber)
                                          + " (" + juce::String(midiStatus.noteNumber) + ")"
                                          + "  Velocity " + juce::String(midiStatus.velocity)
                                    : juce::String("MIDI In: waiting for note...");
        midiStatusLabel.setText(statusText, juce::dontSendNotification);
        repaint();
    }

    refreshAnyKeyDownState();

    if (anyKeyDown)
    {
        logoWobblePhase += 0.12f;

        if (logoWobblePhase > juce::MathConstants<float>::twoPi)
        {
            logoWobblePhase -= juce::MathConstants<float>::twoPi;
        }

        repaint();
    }
}
