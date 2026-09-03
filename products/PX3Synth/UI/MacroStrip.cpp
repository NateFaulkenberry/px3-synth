#include "MacroStrip.h"

#include "PluginProcessor.h"
#include "MacroLook.h"
#include "UIConfig.h"

static_assert(MacroStrip::kCount == PX3SynthAudioProcessor::kMacroCount,
              "The strip must hold exactly one cell per macro.");

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
    // One cell per macro down the strip. The knob takes a square of whatever width is
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
    const auto knobScale = uiConfig != nullptr
                             ? uiConfig->getFloat("macro.strip.knobScale", 0.9f)
                             : 0.9f;
    // Each cell's edges come from the strip's own height rather than from a
    // rounded-down cell height taken N times: integer division leaves up to
    // N-1 pixels, and removeFromTop pools all of them at the bottom, so the
    // last macro sits high in a taller gap. Dividing by position spreads that
    // remainder one pixel at a time and the cells stay even.
    const auto count = PX3SynthAudioProcessor::kMacroCount;
    const auto top = area.getY();
    const auto span = area.getHeight();

    for (int macro = 0; macro < count; ++macro)
    {
        const auto cellTop = top + (span * macro) / count;
        const auto cellBottom = top + (span * (macro + 1)) / count;
        auto cell = juce::Rectangle<int>(area.getX(), cellTop,
                                         area.getWidth(), cellBottom - cellTop);
        auto& entry = entries[static_cast<std::size_t>(macro)];

        // The knob sits directly ON its caption - no gap - because they are one
        // control, and whatever the cell has spare goes ABOVE and BELOW that
        // pair rather than between them. Bottom-aligning the pair instead left
        // every cell's slack above the knob, so the whole column sat low in the
        // strip: 53 px of air over the first macro against 10 under the last.
        const auto captionCellHeight = juce::jmin(captionHeight, cell.getHeight());

        // Scaled down from what the cell allows, rather than by narrowing the
        // strip: the strip's width is a layout budget every panel is placed
        // against, so taking the knob off it would move every panel. This
        // leaves the strip, the padding and the caption exactly where they are
        // and only shrinks the disc, with the extra room going evenly around it
        // because the pair is centred in its cell.
        const auto fit = juce::jmax(0, juce::jmin(cell.getWidth(),
                                                  cell.getHeight() - captionCellHeight));
        const auto side = juce::jmax(0, juce::roundToInt(static_cast<float>(fit) * knobScale));
        const auto groupTop = cell.getY() + (cell.getHeight() - (side + captionCellHeight)) / 2;

        entry.knob.setBounds(cell.getCentreX() - side / 2, groupTop, side, side);
        entry.caption.setBounds(cell.getX(), groupTop + side,
                                cell.getWidth(), captionCellHeight);
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
