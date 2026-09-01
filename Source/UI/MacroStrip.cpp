#include "MacroStrip.h"

#include "../DSP/PluginProcessor.h"
#include "MacroLook.h"
#include "UIConfig.h"

MacroStrip::MacroStrip(PX3SynthAudioProcessor& processorIn, juce::LookAndFeel* knobLookAndFeel)
    : processor(processorIn)
{
    for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
    {
        auto& entry = entries[static_cast<std::size_t>(macro)];

        entry.knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        entry.knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        if (knobLookAndFeel != nullptr) { entry.knob.setLookAndFeel(knobLookAndFeel); }
        addAndMakeVisible(entry.knob);

        // "M1" is what fits; "Macro 1" is what it means. The full name is on
        // both the caption and the knob, so hovering anywhere over the control
        // says which one it is.
        const auto fullName = "Macro " + juce::String(macro + 1);

        entry.caption.setText("M" + juce::String(macro + 1), juce::dontSendNotification);
        entry.caption.setJustificationType(juce::Justification::centred);
        entry.caption.setFont(juce::FontOptions(11.0f));
        entry.caption.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        entry.caption.setInterceptsMouseClicks(false, false);
        entry.caption.setTooltip(fullName);
        entry.knob.setTooltip(fullName);
        addAndMakeVisible(entry.caption);

        // Bound like any other parameter knob, which is what makes the macros
        // MIDI-mappable through the existing system with no new code.
        px3::ui::attachParameterKnob(processor.getMacroParam(macro), entry.knob, attachments);
    }
}

MacroStrip::~MacroStrip()
{
    for (auto& entry : entries)
    {
        entry.knob.setLookAndFeel(nullptr);
    }
}

int MacroStrip::preferredWidth(const UIConfig* config)
{
    return config != nullptr ? config->getInt("editor.layout.macroStripWidth", 70) : 70;
}

void MacroStrip::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    resized();
    repaint();
}

void MacroStrip::setAssigningMacro(int macroIndex)
{
    if (assigningMacro == macroIndex) { return; }

    assigningMacro = macroIndex;
    repaint();
}

juce::Slider& MacroStrip::knob(int macroIndex)
{
    return entries[static_cast<std::size_t>(
        juce::jlimit(0, PX3SynthAudioProcessor::kMacroCount - 1, macroIndex))].knob;
}

const juce::Slider& MacroStrip::knob(int macroIndex) const
{
    return entries[static_cast<std::size_t>(
        juce::jlimit(0, PX3SynthAudioProcessor::kMacroCount - 1, macroIndex))].knob;
}

void MacroStrip::resized()
{
    // Four cells down the strip. The knob takes a square of whatever width is
    // left after padding, and its caption sits directly beneath it - so the
    // strip fills the width it is given at any height rather than assuming one.
    // Padding from config: a few pixels at the sides, more at the top and
    // bottom, so the first and last captions are not pressed against the edge.
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("macro.strip.padX", 4) : 4;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("macro.strip.padY", 10) : 10;

    auto area = getLocalBounds().reduced(padX, padY);
    if (area.isEmpty()) { return; }

    const auto captionHeight = uiConfig != nullptr
                                 ? uiConfig->getInt("macro.strip.captionHeight", 14)
                                 : 14;
    const auto cellHeight = juce::jmax(1, area.getHeight() / PX3SynthAudioProcessor::kMacroCount);

    for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
    {
        auto cell = area.removeFromTop(cellHeight);
        auto& entry = entries[static_cast<std::size_t>(macro)];

        auto caption = cell.removeFromBottom(juce::jmin(captionHeight, cell.getHeight()));
        entry.caption.setBounds(caption);

        // Sat ON its caption rather than centred in what is left. Centring put
        // whatever the cell had spare between the knob and the label it
        // belongs to, which reads as two things rather than one control.
        const auto side = juce::jmin(cell.getWidth(), cell.getHeight());
        entry.knob.setBounds(cell.getCentreX() - side / 2,
                             cell.getBottom() - side,
                             side,
                             side);
    }
}

void MacroStrip::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 10));
    g.fillRoundedRectangle(area, 6.0f);

    // While assigning, the strip says which macro is being edited.
    //
    // The highlight covers the caption as well as the knob: they are one
    // control, and a box around only half of it looks like the label belongs
    // to something else.
    if (juce::isPositiveAndBelow(assigningMacro, PX3SynthAudioProcessor::kMacroCount))
    {
        const auto& entry = entries[static_cast<std::size_t>(assigningMacro)];
        const auto highlight = entry.knob.getBounds()
                                   .getUnion(entry.caption.getBounds())
                                   .toFloat()
                                   .expanded(3.0f);

        const auto accent = px3::ui::macroAccentColour(uiConfig.get());
        g.setColour(accent.withAlpha(0.18f));
        g.fillRoundedRectangle(highlight, 7.0f);
        g.setColour(accent);
        g.drawRoundedRectangle(highlight, 7.0f, 1.4f);
    }
}
