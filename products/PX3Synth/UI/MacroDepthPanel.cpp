#include "MacroDepthPanel.h"

#include "UIConfig.h"

#include <algorithm>
#include <cmath>

namespace px3::ui
{
namespace
{
juce::Colour colourFrom(const UIConfig* config, const juce::String& key, juce::Colour fallback)
{
    return config != nullptr ? config->getColour(key, fallback) : fallback;
}

int intFrom(const UIConfig* config, const juce::String& key, int fallback)
{
    return config != nullptr ? config->getInt(key, fallback) : fallback;
}

float floatFrom(const UIConfig* config, const juce::String& key, float fallback)
{
    return config != nullptr ? config->getFloat(key, fallback) : fallback;
}

// The name a musician knows a parameter by. The routing stores IDs, which are
// serialisation keys and read like filenames; the panel is the one place that
// has to turn one back into a name, so it asks the parameter itself rather
// than keeping a table that would need updating with every new control.
juce::String displayNameFor(PX3SynthAudioProcessor& processor, const juce::String& parameterId)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            if (ranged->getParameterID() == parameterId)
            {
                const auto name = ranged->getName(64);
                return name.isNotEmpty() ? name : parameterId;
            }
        }
    }

    return parameterId;
}

// Depth is stored -1..+1 and shown as a signed percentage. Bipolar because the
// accumulator has always taken a signed depth - it picks the headroom on the
// side the depth points at - so an inverted route is expressible in the model
// and in the sound, and hiding it here would be the UI narrowing the format.
juce::String depthText(float depth)
{
    const auto percent = juce::roundToInt(depth * 100.0f);
    return (percent > 0 ? "+" : "") + juce::String(percent) + "%";
}
} // namespace

void MacroDepthSliderLook::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float, float,
                                            juce::Slider::SliderStyle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto centreY = bounds.getCentreY();
    const auto radius = trackThickness * 0.5f;

    // The track, full width, flat.
    g.setColour(track);
    g.fillRoundedRectangle(bounds.getX(), centreY - radius, bounds.getWidth(),
                           trackThickness, radius);

    // The filled portion, from the CENTRE outwards, because the range is
    // bipolar and zero is the middle rather than the left end.
    const auto zeroPos = static_cast<float>(
        slider.getPositionOfValue(juce::jlimit(slider.getMinimum(), slider.getMaximum(), 0.0)));
    const auto from = juce::jmin(zeroPos, sliderPos);
    const auto to = juce::jmax(zeroPos, sliderPos);

    if (to - from > 0.5f)
    {
        g.setColour(fill);
        g.fillRoundedRectangle(from, centreY - radius, to - from, trackThickness, radius);
    }

    g.setColour(thumb);
    g.fillEllipse(sliderPos - thumbRadius, centreY - thumbRadius,
                  thumbRadius * 2.0f, thumbRadius * 2.0f);
}

MacroDepthPanel::~MacroDepthPanel()
{
    for (auto& row : rows) { row->depth.setLookAndFeel(nullptr); }
}

MacroDepthPanel::MacroDepthPanel(PX3SynthAudioProcessor& processorIn)
    : processor(processorIn)
{
    setInterceptsMouseClicks(true, true);

    header.setJustificationType(juce::Justification::centredLeft);
    header.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    addAndMakeVisible(header);

    emptyNotice = "Nothing assigned yet.\nDouble-click the macro knob to assign parameters to it.";

    closeButton.onClick = [this]
    {
        if (onCloseRequested != nullptr) { onCloseRequested(); }
    };
    addAndMakeVisible(closeButton);

    // Same action as the footer button: the panel does not know what closing
    // means - the editor owns the transient state - so both ask it.
    closeGlyph.onClick = [this]
    {
        if (onCloseRequested != nullptr) { onCloseRequested(); }
    };
    addAndMakeVisible(closeGlyph);

    viewport.setViewedComponent(&rowHost, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
}

void MacroDepthPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    applyStyleFromConfig();
    resized();
    repaint();
}

void MacroDepthPanel::setPointerTargetY(int yInPanelCoordinates)
{
    if (pointerTargetY == yInPanelCoordinates) { return; }

    pointerTargetY = yInPanelCoordinates;
    repaint();
}

int MacroDepthPanel::pointerWidth() const
{
    // pointerWidth is the name this key had before the panel shared the update
    // notice's bubble. Read as a fallback so a UIConfig customised against the
    // old name still positions the panel's contents correctly.
    const auto legacy = intFrom(uiConfig.get(), "macroDepth.layout.pointerWidth", 9);
    return juce::jmax(0, intFrom(uiConfig.get(), "macroDepth.layout.arrowWidth", legacy));
}

void MacroDepthPanel::applyStyleFromConfig()
{
    // Every colour and size the panel draws with, in one place, so a change in
    // UIConfig.json reaches the whole panel rather than half of it.
    sliderLook.track = colourFrom(uiConfig.get(), "macroDepth.colors.sliderTrack",
                                  juce::Colour::fromRGBA(255, 255, 255, 38));
    sliderLook.fill = colourFrom(uiConfig.get(), "macroDepth.colors.sliderFill", accent);
    sliderLook.thumb = colourFrom(uiConfig.get(), "macroDepth.colors.sliderThumb",
                                  accent.brighter(0.45f));
    sliderLook.trackThickness = static_cast<float>(
        intFrom(uiConfig.get(), "macroDepth.layout.sliderTrackThickness", 3));
    sliderLook.thumbRadius = static_cast<float>(
        intFrom(uiConfig.get(), "macroDepth.layout.sliderThumbRadius", 5));

    header.setFont(juce::FontOptions(static_cast<float>(
        intFrom(uiConfig.get(), "macroDepth.layout.headerFontSize", 13)), juce::Font::bold));
    header.setColour(juce::Label::textColourId,
                     colourFrom(uiConfig.get(), "macroDepth.colors.header",
                                juce::Colour::fromRGB(236, 240, 248)));

    closeButton.setColour(juce::TextButton::buttonColourId,
                          colourFrom(uiConfig.get(), "macroDepth.colors.closeButton",
                                     juce::Colour::fromRGBA(52, 56, 64, 235)));
    SheetCloseButton::Style glyphStyle;
    // Small enough to sit in the header row without crowding the macro's name.
    glyphStyle.size = 18;
    SheetCloseButton::readStyleFrom(uiConfig.get(), "macroDepth.closeButton", glyphStyle);
    closeGlyph.applyStyle(glyphStyle);

    closeButton.setColour(juce::TextButton::textColourOffId,
                          colourFrom(uiConfig.get(), "macroDepth.colors.closeButtonText",
                                     juce::Colour::fromRGB(232, 236, 242)));

    for (auto& row : rows)
    {
        row->name.setFont(juce::FontOptions(static_cast<float>(
            intFrom(uiConfig.get(), "macroDepth.layout.rowFontSize", 12))));
        row->name.setColour(juce::Label::textColourId,
                            colourFrom(uiConfig.get(), "macroDepth.colors.rowLabel",
                                       juce::Colour::fromRGB(224, 226, 232)));
        row->value.setFont(juce::FontOptions(static_cast<float>(
            intFrom(uiConfig.get(), "macroDepth.layout.valueFontSize", 11))));
        row->value.setColour(juce::Label::textColourId,
                             colourFrom(uiConfig.get(), "macroDepth.colors.rowValue",
                                        juce::Colour::fromRGB(198, 202, 212)));
        row->depth.setLookAndFeel(&sliderLook);
    }
}

void MacroDepthPanel::setAccentColour(juce::Colour colour)
{
    accent = colour;

    for (auto& row : rows)
    {
        row->depth.setColour(juce::Slider::trackColourId, accent.withAlpha(0.85f));
        row->depth.setColour(juce::Slider::thumbColourId, accent.brighter(0.25f));
    }

    repaint();
}

void MacroDepthPanel::setMacro(int macroIndexIn)
{
    macroIndex = macroIndexIn;
    refreshFromProcessor();
}

void MacroDepthPanel::refreshFromProcessor()
{
    // Identity first, and unconditionally: it is a fact about which macro this
    // is, not about the rows, and the fast path below returns before the
    // rebuild that used to set it.
    header.setText(juce::isPositiveAndBelow(macroIndex, PX3SynthAudioProcessor::kMacroCount)
                       ? PX3SynthAudioProcessor::macroDisplayName(macroIndex).toUpperCase()
                       : juce::String(),
                   juce::dontSendNotification);

    // Rebuilding destroys the sliders, so it must not happen for no reason:
    // called on the editor's refresh pass, that would tear down the control
    // under the user's pointer mid-drag. The rows are rebuilt only when the
    // SET of destinations has actually changed - an assignment added or
    // removed, or a preset loaded. A depth that changed underneath is picked
    // up without a rebuild.
    if (juce::isPositiveAndBelow(macroIndex, PX3SynthAudioProcessor::kMacroCount))
    {
        const auto current = processor.getMacroDestinations(macroIndex);

        auto sameSet = current.size() == rows.size();
        if (sameSet)
        {
            for (std::size_t i = 0; i < current.size(); ++i)
            {
                if (current[i].parameterId != rows[i]->parameterId) { sameSet = false; break; }
            }
        }

        if (sameSet)
        {
            for (std::size_t i = 0; i < current.size(); ++i)
            {
                auto& row = *rows[i];
                if (! row.depth.isMouseButtonDown()
                    && std::abs(static_cast<float>(row.depth.getValue()) - current[i].depth) > 1.0e-6f)
                {
                    row.depth.setValue(current[i].depth, juce::dontSendNotification);
                    refreshValueLabel(row);
                }
            }
            return;
        }
    }

    // Rebuilt rather than diffed. The list is at most a few dozen rows and is
    // rebuilt only on an assignment change, so the simplest thing that cannot
    // leave a row pointing at a route that no longer exists is the right one.
    // Detach the look before the rows go: a Component holding a pointer to a
    // LookAndFeel it outlives is the classic way to crash on teardown.
    for (auto& row : rows) { row->depth.setLookAndFeel(nullptr); }
    rows.clear();
    rowHost.removeAllChildren();

    if (! juce::isPositiveAndBelow(macroIndex, PX3SynthAudioProcessor::kMacroCount))
    {
        resized();
        return;
    }

    for (const auto& destination : processor.getMacroDestinations(macroIndex))
    {
        auto row = std::make_unique<Row>();
        row->parameterId = destination.parameterId;

        row->name.setText(displayNameFor(processor, destination.parameterId),
                          juce::dontSendNotification);
        row->name.setJustificationType(juce::Justification::centredLeft);
        row->name.setFont(juce::FontOptions(11.5f));
        row->name.setColour(juce::Label::textColourId,
                            colourFrom(uiConfig.get(), "macroDepth.colors.rowLabel",
                                       juce::Colour::fromRGB(224, 226, 232)));
        row->name.setInterceptsMouseClicks(false, false);
        rowHost.addAndMakeVisible(row->name);

        row->depth.setRange(-1.0, 1.0, 0.001);
        row->depth.setDoubleClickReturnValue(true, PX3SynthAudioProcessor::kMacroDepthDefault);
        row->depth.setValue(destination.depth, juce::dontSendNotification);
        row->depth.setTooltip("How much of " + PX3SynthAudioProcessor::macroDisplayName(macroIndex)
                              + " reaches " + row->name.getText());
        auto* raw = row.get();
        row->depth.onValueChange = [this, raw]
        {
            writeDepth(*raw);
            refreshValueLabel(*raw);
        };
        row->depth.setLookAndFeel(&sliderLook);
        rowHost.addAndMakeVisible(row->depth);

        row->value.setJustificationType(juce::Justification::centredRight);
        row->value.setFont(juce::FontOptions(11.0f));
        row->value.setColour(juce::Label::textColourId,
                             colourFrom(uiConfig.get(), "macroDepth.colors.rowValue",
                                        juce::Colour::fromRGB(198, 202, 212)));
        row->value.setInterceptsMouseClicks(false, false);
        rowHost.addAndMakeVisible(row->value);
        refreshValueLabel(*row);

        rows.push_back(std::move(row));
    }

    applyStyleFromConfig();
    resized();
    repaint();
}

void MacroDepthPanel::writeDepth(const Row& row)
{
    // Straight through to the routing. The processor rebuilds the audio
    // thread's table, so the number on screen and the number in the DSP are
    // the same number.
    processor.setMacroDestinationDepth(macroIndex, row.parameterId,
                                       static_cast<float>(row.depth.getValue()));
}

void MacroDepthPanel::refreshValueLabel(Row& row)
{
    row.value.setText(depthText(static_cast<float>(row.depth.getValue())),
                      juce::dontSendNotification);
}

int MacroDepthPanel::rowHeight() const
{
    return juce::jmax(18, intFrom(uiConfig.get(), "macroDepth.layout.rowHeight", 30));
}

int MacroDepthPanel::columnWidth() const
{
    return juce::jmax(140, intFrom(uiConfig.get(), "macroDepth.layout.columnWidth", 260));
}

int MacroDepthPanel::rowAreaHeight() const
{
    const auto headerH = intFrom(uiConfig.get(), "macroDepth.layout.headerHeight", 26);
    const auto footerH = intFrom(uiConfig.get(), "macroDepth.layout.footerHeight", 30);
    const auto padding = intFrom(uiConfig.get(), "macroDepth.layout.padding", 10);
    return juce::jmax(rowHeight(), getHeight() - headerH - footerH - padding * 2);
}

int MacroDepthPanel::columnsForRows(int rowCount, int width, int height) const
{
    if (rowCount <= 0) { return 1; }

    const auto padding = intFrom(uiConfig.get(), "macroDepth.layout.padding", 10);
    const auto usableWidth = juce::jmax(columnWidth(), width - padding * 2 - pointerWidth());

    // Two limits on how long a column gets. The hard one is the height it has
    // to fit in; the soft one is how long a list stays scannable, because a
    // column that is technically 20 rows tall is a wall of sliders when the
    // space to its right is empty. Past the soft limit the rows wrap into
    // another column, which is what "use the horizontal space" means here.
    const auto fits = juce::jmax(1, height / rowHeight());
    const auto readable = juce::jmax(1, intFrom(uiConfig.get(),
                                                "macroDepth.layout.preferredRowsPerColumn", 8));
    const auto perColumn = juce::jmin(fits, readable);

    // Columns before scrolling: the space to the right is free, and a narrow
    // scroller at four assignments is the failure the brief names.
    const auto wanted = (rowCount + perColumn - 1) / perColumn;
    const auto affordable = juce::jmax(1, usableWidth / columnWidth());
    return juce::jlimit(1, affordable, wanted);
}

bool MacroDepthPanel::debugIsScrolling() const
{
    return rowHost.getHeight() > viewport.getMaximumVisibleHeight();
}

juce::StringArray MacroDepthPanel::debugRowParameterIds() const
{
    juce::StringArray ids;
    for (const auto& row : rows) { ids.add(row->parameterId); }
    return ids;
}

juce::Slider* MacroDepthPanel::debugDepthSliderFor(const juce::String& parameterId)
{
    for (auto& row : rows)
    {
        if (row->parameterId == parameterId) { return &row->depth; }
    }
    return nullptr;
}

juce::Label* MacroDepthPanel::debugValueLabelFor(const juce::String& parameterId)
{
    for (auto& row : rows)
    {
        if (row->parameterId == parameterId) { return &row->value; }
    }
    return nullptr;
}

juce::Rectangle<int> MacroDepthPanel::preferredBoundsWithin(juce::Rectangle<int> available,
                                                            juce::Point<int> anchor) const
{
    const auto padding = intFrom(uiConfig.get(), "macroDepth.layout.padding", 10);
    const auto headerH = intFrom(uiConfig.get(), "macroDepth.layout.headerHeight", 26);
    const auto footerH = intFrom(uiConfig.get(), "macroDepth.layout.footerHeight", 30);

    // With nothing assigned there are no rows to size from, and the panel used
    // to come out one row tall - too small for the sentence it then had to
    // draw, which was cut off exactly when it was all the panel had to say.
    if (rows.empty())
    {
        const auto width = juce::jmin(available.getWidth(),
                                      columnWidth() + padding * 2 + pointerWidth());
        const auto height = juce::jmin(available.getHeight(),
                                       headerH + footerH + padding * 2
                                           + rowHeight() * 3);

        return juce::Rectangle<int>(available.getX(), anchor.getY() - height / 2, width, height)
            .constrainedWithin(available);
    }

    const auto rowCount = juce::jmax(1, static_cast<int>(rows.size()));
    const auto maxRowArea = juce::jmax(rowHeight(),
                                       available.getHeight() - headerH - footerH - padding * 2);
    const auto columns = columnsForRows(rowCount,
                                        available.getWidth(),
                                        maxRowArea);

    // As tall as the longest column needs to be, capped by the space there is.
    const auto perColumn = juce::jmax(1, (rowCount + columns - 1) / columns);
    const auto rowsPerColumn = juce::jmin(perColumn, juce::jmax(1, maxRowArea / rowHeight()));

    auto width = juce::jmin(available.getWidth(),
                            columns * columnWidth() + padding * 2 + pointerWidth());
    auto height = juce::jmin(available.getHeight(),
                             rowsPerColumn * rowHeight() + headerH + footerH + padding * 2);

    // Beside the knob, vertically centred on it, then pushed back inside the
    // space it has to live in - so a macro at the bottom of the strip does not
    // open a panel that hangs off the window.
    auto bounds = juce::Rectangle<int>(available.getX(),
                                       anchor.getY() - height / 2,
                                       width, height);
    return bounds.constrainedWithin(available);
}

void MacroDepthPanel::resized()
{
    const auto padding = intFrom(uiConfig.get(), "macroDepth.layout.padding", 10);
    const auto headerH = intFrom(uiConfig.get(), "macroDepth.layout.headerHeight", 26);
    const auto footerH = intFrom(uiConfig.get(), "macroDepth.layout.footerHeight", 30);

    auto area = getLocalBounds().reduced(padding);
    area.removeFromLeft(pointerWidth());   // the arrow's strip, not content
    const auto headerArea = area.removeFromTop(headerH);
    header.setBounds(headerArea);

    // Over the header's top-right corner rather than beside it, so the header
    // keeps its full width and the glyph does not move when headerHeight does.
    closeGlyph.setBounds(closeGlyph.boundsWithin(headerArea));

    auto footer = area.removeFromBottom(footerH);
    closeButton.setBounds(footer.removeFromRight(juce::jmin(90, footer.getWidth()))
                                .withSizeKeepingCentre(juce::jmin(90, footer.getWidth()), 24));

    viewport.setBounds(area);

    layoutRows();
}

void MacroDepthPanel::layoutRows()
{
    const auto visible = viewport.getBounds();
    if (visible.isEmpty() || rows.empty())
    {
        rowHost.setBounds(0, 0, juce::jmax(1, visible.getWidth()), juce::jmax(1, visible.getHeight()));
        return;
    }

    const auto rowCount = static_cast<int>(rows.size());
    const auto columns = columnsForRows(rowCount, getWidth(), visible.getHeight());
    // Split as evenly as the column count allows, so two columns of seven
    // rather than one of eight and one of six.
    const auto rowsPerColumn = juce::jmax(1, (rowCount + columns - 1) / columns);

    // The host is as tall as the tallest column. If that exceeds the viewport
    // - more rows than columns and height can take between them - the viewport
    // scrolls, which is the last resort rather than the first.
    const auto hostHeight = juce::jmax(visible.getHeight(), rowsPerColumn * rowHeight());
    const auto hostWidth = juce::jmax(1, visible.getWidth() - (hostHeight > visible.getHeight()
                                                                   ? viewport.getScrollBarThickness()
                                                                   : 0));
    rowHost.setBounds(0, 0, hostWidth, hostHeight);

    const auto colWidth = juce::jmax(80, hostWidth / columns);
    const auto valueWidth = juce::jmax(38, intFrom(uiConfig.get(), "macroDepth.layout.valueWidth", 46));
    const auto nameWidth = juce::jmax(60, intFrom(uiConfig.get(), "macroDepth.layout.nameWidth", 96));

    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const auto index = static_cast<int>(i);
        const auto column = index / rowsPerColumn;
        const auto rowInColumn = index % rowsPerColumn;

        auto cell = juce::Rectangle<int>(column * colWidth, rowInColumn * rowHeight(),
                                         colWidth, rowHeight()).reduced(4, 2);

        auto& row = *rows[i];
        row.name.setBounds(cell.removeFromLeft(juce::jmin(nameWidth, cell.getWidth() / 2)));
        row.value.setBounds(cell.removeFromRight(juce::jmin(valueWidth, cell.getWidth() / 2)));
        row.depth.setBounds(cell.reduced(4, 0));
    }
}

SpeechBubble::Style MacroDepthPanel::bubbleStyle() const
{
    // The same bubble the update notice uses, pointing left instead of down.
    // Sharing the shape was the point of the refactor: this panel used to fill
    // a rounded rectangle, fill a triangle beside it, and then stroke only two
    // of that triangle's three edges - because stroking the third drew a line
    // across the arrow's mouth, where the panel is. One closed path needs none
    // of that, and the fills cannot disagree at the join.
    SpeechBubble::Style style;
    style.arrowEdge = SpeechBubble::ArrowEdge::left;

    const auto* config = uiConfig.get();

    style.background = colourFrom(config, "macroDepth.colors.background",
                                  juce::Colour::fromRGB(17, 19, 23));
    style.border = colourFrom(config, "macroDepth.colors.border", accent);

    // Nearly opaque. A popover carrying eight rows of small type sits over
    // whatever panel is behind it, and the translucency the other panels use
    // costs more in legibility here than it returns in depth.
    style.backgroundOpacity = floatFrom(config, "macroDepth.colors.backgroundOpacity", 0.99f);
    style.borderOpacity     = floatFrom(config, "macroDepth.colors.borderOpacity", 0.55f);

    style.cornerRadius = floatFrom(config, "macroDepth.layout.cornerRadius", 8.0f);
    // borderThickness is what this key was called before the panel shared the
    // notice's bubble; read second so an older UIConfig still styles the frame.
    style.borderWidth = floatFrom(config, "macroDepth.layout.borderWidth",
                                  floatFrom(config, "macroDepth.layout.borderThickness", 1.0f));

    // Horizontal reach and vertical span, the same two numbers the notice uses.
    style.arrowWidth  = static_cast<float>(pointerWidth());
    style.arrowHeight = floatFrom(config, "macroDepth.layout.arrowHeight",
                                  floatFrom(config, "macroDepth.layout.pointerHeight", 16.0f));

    // Where the macro knob is. Negative until the editor has placed the panel,
    // and the bubble draws plain rather than aiming at nothing.
    style.arrowCentreFromTop = pointerTargetY >= 0 ? static_cast<float>(pointerTargetY)
                                                   : -1.0f;
    return style;
}

void MacroDepthPanel::paint(juce::Graphics& g)
{
    // One path: background, border and the pointer that aims at the macro knob.
    SpeechBubble::paintBackground(g, getLocalBounds().toFloat(), bubbleStyle());

    // ---- nothing assigned --------------------------------------------------
    //
    // Drawn rather than put in a Label, because a Label does not wrap and this
    // sentence is longer than a narrow panel is wide - it was being cut off
    // exactly when it was the only thing the panel had to say.
    if (rows.empty() && emptyNotice.isNotEmpty())
    {
        const auto padding = intFrom(uiConfig.get(), "macroDepth.layout.padding", 10);
        const auto headerH = intFrom(uiConfig.get(), "macroDepth.layout.headerHeight", 26);
        const auto footerH = intFrom(uiConfig.get(), "macroDepth.layout.footerHeight", 30);

        auto textArea = getLocalBounds().reduced(padding);
        textArea.removeFromLeft(pointerWidth());
        textArea.removeFromTop(headerH);
        textArea.removeFromBottom(footerH);

        g.setColour(colourFrom(uiConfig.get(), "macroDepth.colors.emptyNotice",
                               juce::Colour::fromRGB(168, 174, 186)));
        g.setFont(juce::FontOptions(static_cast<float>(
            intFrom(uiConfig.get(), "macroDepth.layout.rowFontSize", 12))));
        g.drawFittedText(emptyNotice, textArea, juce::Justification::centred, 3);
    }
}

} // namespace px3::ui
