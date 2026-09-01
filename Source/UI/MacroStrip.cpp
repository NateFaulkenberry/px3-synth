#include "MacroStrip.h"

#include "../DSP/PluginProcessor.h"
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

        entry.caption.setText("M" + juce::String(macro + 1), juce::dontSendNotification);
        entry.caption.setJustificationType(juce::Justification::centred);
        entry.caption.setFont(juce::FontOptions(9.0f));
        entry.caption.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        entry.caption.setInterceptsMouseClicks(false, false);
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
    // The brief's budget is "approximately 40px or less". 38 leaves the panel
    // beside it two pixels it would not otherwise have.
    return config != nullptr ? config->getInt("editor.layout.macroStripWidth", 38) : 38;
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
    // left after padding, and the caption sits under it - so the strip stays
    // inside its budget at any height rather than assuming one.
    auto area = getLocalBounds().reduced(3, 6);
    if (area.isEmpty()) { return; }

    const auto captionHeight = 11;
    const auto cellHeight = juce::jmax(1, area.getHeight() / PX3SynthAudioProcessor::kMacroCount);

    for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
    {
        auto cell = area.removeFromTop(cellHeight);
        auto& entry = entries[static_cast<std::size_t>(macro)];

        auto caption = cell.removeFromBottom(juce::jmin(captionHeight, cell.getHeight()));
        entry.caption.setBounds(caption);

        const auto side = juce::jmin(cell.getWidth(), cell.getHeight());
        entry.knob.setBounds(juce::Rectangle<int>(side, side).withCentre(cell.getCentre()));
    }
}

void MacroStrip::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 10));
    g.fillRoundedRectangle(area, 6.0f);

    // While assigning, the strip says which macro is being edited. The
    // colour is the macro language - a solid violet - and deliberately not
    // the amber MIDI Learn uses, so the two modes are never confused.
    if (juce::isPositiveAndBelow(assigningMacro, PX3SynthAudioProcessor::kMacroCount))
    {
        const auto& entry = entries[static_cast<std::size_t>(assigningMacro)];
        const auto highlight = entry.knob.getBounds().toFloat().expanded(4.0f);

        g.setColour(juce::Colour::fromRGBA(168, 130, 255, 46));
        g.fillRoundedRectangle(highlight, 7.0f);
        g.setColour(juce::Colour::fromRGB(168, 130, 255));
        g.drawRoundedRectangle(highlight, 7.0f, 1.4f);
    }
}
