#include "FxCardComponent.h"

#include "UIConfig.h"

#include <algorithm>

namespace px3::ui
{
namespace
{
// Defaults only. Every one of these is overridable per card through
// cards.<key>.controls in UIConfig.json - see setUIConfig.
constexpr float kDefaultKnobCell = 62.0f;
constexpr float kDefaultFeatureKnobCell = 108.0f;
constexpr float kDefaultChoiceCell = 92.0f;
constexpr float kDefaultToggleCell = 96.0f;
constexpr int kDefaultLabelHeight = 14;
constexpr int kDefaultReadoutHeight = 16;
constexpr int kDefaultControlHeight = 22;
} // namespace

FxCardComponent::FxCardComponent(juce::String styleKeyIn, juce::String titleIn)
    : styleKey(std::move(styleKeyIn)), title(std::move(titleIn))
{
    // The card background toggles this section, so the pointer says it is
    // clickable. Child controls carry their own cursors.
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    bypass.setSectionName(title);
    addAndMakeVisible(bypass);
}

FxCardComponent::~FxCardComponent()
{
    // The knobs' look and feel belongs to the editor and outlives nothing here,
    // but JUCE requires it be cleared before the LookAndFeel is destroyed.
    for (auto& entry : knobs)
    {
        entry.knob->setLookAndFeel(nullptr);
    }
}

// ============================================================================
// declaration
// ============================================================================

void FxCardComponent::addToggleRow(std::vector<ToggleSpec> specs)
{
    Row row { RowKind::toggles, {} };

    for (auto& spec : specs)
    {
        auto button = std::make_unique<ToggleChipButton>();
        button->setStateLabels(spec.onText, spec.offText);
        button->setButtonText(spec.offText);
        button->setTooltip(spec.tooltip);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(*button);

        row.ids.push_back(spec.id);
        toggles.push_back({ spec.id, std::move(button) });
    }

    rows.push_back(std::move(row));
}

void FxCardComponent::addChoiceRow(std::vector<ChoiceSpec> specs)
{
    Row row { RowKind::choices, {} };

    for (auto& spec : specs)
    {
        auto box = std::make_unique<juce::ComboBox>();
        for (int i = 0; i < spec.choices.size(); ++i)
        {
            box->addItem(spec.choices[i], i + 1);
        }
        box->setSelectedItemIndex(0, juce::dontSendNotification);
        box->setTooltip(spec.tooltip);
        addAndMakeVisible(*box);

        auto label = std::make_unique<ChipLabel>();
        label->setText(spec.label, juce::dontSendNotification);
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::FontOptions(11.5f));
        label->setTooltip(spec.tooltip);
        addAndMakeVisible(*label);

        row.ids.push_back(spec.id);
        choices.push_back({ spec.id, std::move(box), std::move(label) });
    }

    rows.push_back(std::move(row));
}

void FxCardComponent::addKnobRow(std::vector<KnobSpec> specs)
{
    Row row { RowKind::knobs, {} };

    for (auto& spec : specs)
    {
        auto knob = std::make_unique<juce::Slider>();
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob->setTooltip(spec.tooltip);
        addAndMakeVisible(*knob);

        auto label = std::make_unique<ChipLabel>();
        label->setText(spec.label, juce::dontSendNotification);
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::FontOptions(11.5f));
        label->setInterceptsMouseClicks(true, false);
        label->setTooltip(spec.tooltip);
        addAndMakeVisible(*label);

        // The alternate, if this knob carries one: a second slider that will
        // be given the SAME bounds, and a second caption under the first. Both
        // are created here and attached by the editor exactly like a primary,
        // so neither depends on which one the panel is showing.
        std::unique_ptr<juce::Slider> altKnob;
        std::unique_ptr<ChipLabel> altLabel;
        if (spec.altId.isNotEmpty())
        {
            altKnob = std::make_unique<juce::Slider>();
            altKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            altKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            altKnob->setTooltip(spec.altTooltip);
            addAndMakeVisible(*altKnob);
            altKnob->setVisible(false);

            altLabel = std::make_unique<ChipLabel>();
            altLabel->setText(spec.altLabel, juce::dontSendNotification);
            altLabel->setJustificationType(juce::Justification::centred);
            // Smaller than the primary. The pedal prints the alternate
            // underneath in smaller type, and the point of the pairing is that
            // one caption dominates rather than that there are two of them.
            altLabel->setFont(juce::FontOptions(9.5f));
            altLabel->setInterceptsMouseClicks(true, false);
            altLabel->setTooltip(spec.altTooltip);
            addAndMakeVisible(*altLabel);
        }

        row.ids.push_back(spec.id);
        knobs.push_back({ spec.id, std::move(knob), std::move(label),
                          spec.altId, std::move(altKnob), std::move(altLabel) });
    }

    rows.push_back(std::move(row));
}

bool FxCardComponent::hasAlternates() const noexcept
{
    return std::any_of(knobs.begin(), knobs.end(),
                       [](const KnobEntry& e) { return e.altKnob != nullptr; });
}

void FxCardComponent::setAltMode(bool showAlternates)
{
    altMode = showAlternates;

    for (auto& entry : knobs)
    {
        if (entry.altKnob == nullptr) { continue; }

        // Visibility, not attachment. Both sliders keep their parameters, so
        // an automated alternate keeps moving while the primary is on show.
        entry.knob->setVisible(! altMode);
        entry.altKnob->setVisible(altMode);

        // Whichever function is live gets the full caption treatment; the
        // other stays legible but recedes.
        constexpr auto kRecededAlpha = 0.55f;
        entry.label->setAlpha(altMode ? kRecededAlpha : 1.0f);
        if (entry.altLabel != nullptr)
        {
            entry.altLabel->setAlpha(altMode ? 1.0f : kRecededAlpha);
        }
    }

    repaint();
}

void FxCardComponent::addFeatureKnobRow(KnobSpec spec)
{
    addKnobRow({ std::move(spec) });
    rows.back().kind = RowKind::featureKnob;
}

// ============================================================================
// lookup
// ============================================================================

juce::Slider* FxCardComponent::knob(const juce::String& id) const
{
    const auto it = std::find_if(knobs.begin(), knobs.end(),
                                 [&id](const KnobEntry& e) { return e.id == id; });
    if (it != knobs.end()) { return it->knob.get(); }

    // An alternate answers to its own id, so attaching one is the same call as
    // attaching a primary and no caller has to know about the pairing.
    const auto alt = std::find_if(knobs.begin(), knobs.end(),
                                  [&id](const KnobEntry& e)
                                  { return e.altKnob != nullptr && e.altId == id; });
    return alt != knobs.end() ? alt->altKnob.get() : nullptr;
}

juce::Label* FxCardComponent::knobLabel(const juce::String& id) const
{
    const auto it = std::find_if(knobs.begin(), knobs.end(),
                                 [&id](const KnobEntry& e) { return e.id == id; });
    if (it != knobs.end()) { return it->label.get(); }

    const auto alt = std::find_if(knobs.begin(), knobs.end(),
                                  [&id](const KnobEntry& e)
                                  { return e.altLabel != nullptr && e.altId == id; });
    return alt != knobs.end() ? alt->altLabel.get() : nullptr;
}

juce::ComboBox* FxCardComponent::choice(const juce::String& id) const
{
    const auto it = std::find_if(choices.begin(), choices.end(),
                                 [&id](const ChoiceEntry& e) { return e.id == id; });
    return it != choices.end() ? it->box.get() : nullptr;
}

juce::ToggleButton* FxCardComponent::toggle(const juce::String& id) const
{
    const auto it = std::find_if(toggles.begin(), toggles.end(),
                                 [&id](const ToggleEntry& e) { return e.id == id; });
    return it != toggles.end() ? it->button.get() : nullptr;
}

std::vector<juce::Slider*> FxCardComponent::allKnobs() const
{
    // Alternates included. They are real controls with real parameters, and
    // the styling and attachment passes that use this have to reach them or a
    // knob nobody is currently looking at goes unstyled and unattached.
    std::vector<juce::Slider*> result;
    result.reserve(knobs.size());
    for (const auto& entry : knobs)
    {
        result.push_back(entry.knob.get());
        if (entry.altKnob != nullptr) { result.push_back(entry.altKnob.get()); }
    }
    return result;
}

std::vector<juce::Label*> FxCardComponent::allKnobLabels() const
{
    std::vector<juce::Label*> result;
    result.reserve(knobs.size());
    for (const auto& entry : knobs)
    {
        result.push_back(entry.label.get());
        if (entry.altLabel != nullptr) { result.push_back(entry.altLabel.get()); }
    }
    return result;
}

std::vector<juce::ComboBox*> FxCardComponent::allChoices() const
{
    std::vector<juce::ComboBox*> result;
    result.reserve(choices.size());
    for (const auto& entry : choices)
    {
        result.push_back(entry.box.get());
    }
    return result;
}

// ============================================================================
// state
// ============================================================================

void FxCardComponent::setAccentColour(juce::Colour colour)
{
    accent = colour;
    repaint();
}

void FxCardComponent::setActive(bool enabled)
{
    isActive = enabled;

    for (auto& entry : knobs)
    {
        entry.knob->setEnabled(enabled);
        // The same grey-out every other bypassed card uses, so a disabled DOOM
        // knob looks like a disabled Mood knob.
        entry.knob->getProperties().set("psychedelicBypassGray", ! enabled);
        // The caption greys out with the knob it names. Without this a
        // bypassed card dimmed its artwork and desaturated its knobs while its
        // captions kept the card's full colour scheme.
        if (entry.label != nullptr) { entry.label->setGreyedOut(! enabled); }

        // And so does the alternate, which is a real control on the card
        // whether or not the ALT switch is currently showing it.
        if (entry.altKnob != nullptr)
        {
            entry.altKnob->setEnabled(enabled);
            entry.altKnob->getProperties().set("psychedelicBypassGray", ! enabled);
        }
        if (entry.altLabel != nullptr) { entry.altLabel->setGreyedOut(! enabled); }
    }
    for (auto& entry : choices)
    {
        entry.box->setEnabled(enabled);
        if (entry.label != nullptr) { entry.label->setGreyedOut(! enabled); }
    }
    for (auto& entry : toggles)
    {
        entry.button->setEnabled(enabled);
    }

    repaint();
}

void FxCardComponent::setUIConfig(std::shared_ptr<const UIConfig> config)
{
    uiConfig = std::move(config);

    if (uiConfig != nullptr)
    {
        // Dropdowns take the same background as the top menu's active buttons,
        // which is where every other box in the plugin gets it.
        const auto boxBackground = uiConfig->getColour("cards." + styleKey + ".controls.boxBackground",
                                                       juce::Colour::fromRGBA(34, 34, 34, 210));
        const auto boxText = uiConfig->getColour("cards." + styleKey + ".controls.boxText",
                                                 juce::Colour::fromRGB(232, 232, 232));
        const auto boxOutline = uiConfig->getColour("cards." + styleKey + ".controls.boxOutline",
                                                    juce::Colour::fromRGBA(255, 255, 255, 105));
        const auto labelColour = uiConfig->getColour("cards." + styleKey + ".controls.labelColour",
                                                     juce::Colour::fromRGB(232, 232, 232));
        const auto labelFont = uiConfig->getFloat("cards." + styleKey + ".controls.labelFontSize", 11.5f);

        // The caption chips. Background and outline are separate colours with
        // separate opacities, so a card can carry its own scheme rather than
        // every caption in the plugin being the same translucent white.
        const auto chipKey = "cards." + styleKey + ".controls.";
        const auto chipStyle = px3::ui::ChipLabel::styleFromConfig(uiConfig.get(), styleKey);

        for (auto& entry : choices)
        {
            entry.box->setColour(juce::ComboBox::backgroundColourId, boxBackground);
            entry.box->setColour(juce::ComboBox::textColourId, boxText);
            entry.box->setColour(juce::ComboBox::outlineColourId, boxOutline);
            entry.label->setColour(juce::Label::textColourId, labelColour);
            entry.label->setFont(juce::FontOptions(labelFont));
            entry.label->setChipStyle(chipStyle);
        }
        for (auto& entry : knobs)
        {
            entry.label->setColour(juce::Label::textColourId, labelColour);
            entry.label->setFont(juce::FontOptions(labelFont));
            entry.label->setChipStyle(chipStyle);
        }

        for (auto& entry : toggles)
        {
            px3::ui::ToggleChipButton::applyFromConfig(uiConfig.get(), styleKey,
                                                       { entry.button.get() });
        }
    }

    // cardInner parses its rows in resized(), so a live config reload has to
    // redo the layout as well as the paint.
    resized();
    repaint();
}

// ============================================================================
// layout
// ============================================================================

void FxCardComponent::layoutToggleRow(int rowIndex, const Row& row)
{
    auto flex = inner.rowFlex(rowIndex);
    const auto gap = inner.rowGap(rowIndex);
    const auto content = inner.rowContent(rowIndex);
    const auto rowWidth = static_cast<float>(juce::jmax(1, content.getWidth()));

    const auto controlHeight = uiConfig != nullptr
                                   ? uiConfig->getInt("cards." + styleKey + ".controls.toggleHeight", kDefaultControlHeight)
                                   : kDefaultControlHeight;

    // maxColumns pins how many chips may sit on one line. Expressed as a count
    // rather than a pixel width because a width only holds at one card size:
    // the grid is four cards across a resizable window, so a width tuned for
    // one layout silently fits a different number of chips in another.
    const auto maxColumns = uiConfig != nullptr
                                ? uiConfig->getInt("cards." + styleKey + ".controls.toggleMaxColumns", 0)
                                : 0;

    auto cellWidth = uiConfig != nullptr
                         ? uiConfig->getFloat("cards." + styleKey + ".controls.toggleWidth", kDefaultToggleCell)
                         : kDefaultToggleCell;

    if (maxColumns > 0)
    {
        // Each item carries `gap` as a margin on every side, so a line of n
        // items occupies n * (width + 2 * gap). The column count sets the room
        // available; the width property caps what is taken of it.
        const auto share = juce::jmax(16.0f, rowWidth / static_cast<float>(maxColumns) - 2.0f * gap.left);
        cellWidth = juce::jmin(share, cellWidth);
    }

    const std::vector<float> widths(row.ids.size(), cellWidth);
    const auto gapWidth = gap.left + gap.right;
    const auto lines = wrappedLineCount(widths, gapWidth, rowWidth);
    const auto cellHeight = juce::jmax(1.0f,
                                       static_cast<float>(content.getHeight()) / static_cast<float>(lines)
                                           - (gap.top + gap.bottom));

    for (const auto width : widths)
    {
        flex.items.add(juce::FlexItem(width, cellHeight).withMargin(gap));
    }
    flex.performLayout(content.toFloat());

    for (std::size_t i = 0; i < row.ids.size(); ++i)
    {
        auto* button = toggle(row.ids[i]);
        if (button == nullptr)
        {
            continue;
        }
        // The chip draws its own caption, so there is nothing beside it to keep
        // in step with it.
        layoutLabelledControl(flex.items.getReference(static_cast<int>(i)).currentBounds.toNearestInt(),
                              { nullptr, button, nullptr, ControlShape::stretch, 0, 0, controlHeight },
                              inner.rowControl(rowIndex));
    }
}

void FxCardComponent::layoutChoiceRow(int rowIndex, const Row& row)
{
    auto flex = inner.rowFlex(rowIndex);
    const auto gap = inner.rowGap(rowIndex);
    const auto content = inner.rowContent(rowIndex);
    const auto rowWidth = static_cast<float>(juce::jmax(1, content.getWidth()));

    // Same as the toggle row: a column count survives a resize where a pixel
    // width does not.
    const auto maxColumns = uiConfig != nullptr
                                ? uiConfig->getInt("cards." + styleKey + ".controls.choiceMaxColumns", 0)
                                : 0;

    auto cellWidth = uiConfig != nullptr
                         ? uiConfig->getFloat("cards." + styleKey + ".controls.choiceWidth", kDefaultChoiceCell)
                         : kDefaultChoiceCell;

    if (maxColumns > 0)
    {
        // The column count sets how much room a box MAY have; the width
        // property caps how much it takes. Without the cap each box stretches
        // to fill its share, which on a wide card turns three dropdowns into
        // three banners.
        const auto share = juce::jmax(16.0f, rowWidth / static_cast<float>(maxColumns) - 2.0f * gap.left);
        cellWidth = juce::jmin(share, cellWidth);
    }

    const auto labelHeight = uiConfig != nullptr
                                 ? uiConfig->getInt("cards." + styleKey + ".controls.labelHeight", kDefaultLabelHeight)
                                 : kDefaultLabelHeight;
    const auto controlHeight = uiConfig != nullptr
                                   ? uiConfig->getInt("cards." + styleKey + ".controls.choiceHeight", kDefaultControlHeight)
                                   : kDefaultControlHeight;

    const std::vector<float> widths(row.ids.size(), cellWidth);
    const auto gapWidth = gap.left + gap.right;
    const auto lines = wrappedLineCount(widths, gapWidth, rowWidth);
    const auto cellHeight = juce::jmax(1.0f,
                                       static_cast<float>(content.getHeight()) / static_cast<float>(lines)
                                           - (gap.top + gap.bottom));

    for (const auto width : widths)
    {
        flex.items.add(juce::FlexItem(width, cellHeight).withMargin(gap));
    }
    flex.performLayout(content.toFloat());

    for (std::size_t i = 0; i < row.ids.size(); ++i)
    {
        const auto it = std::find_if(choices.begin(), choices.end(),
                                     [&row, i](const ChoiceEntry& e) { return e.id == row.ids[i]; });
        if (it == choices.end())
        {
            continue;
        }
        layoutLabelledControl(flex.items.getReference(static_cast<int>(i)).currentBounds.toNearestInt(),
                              { it->label.get(), it->box.get(), nullptr,
                                ControlShape::stretch, labelHeight, 0, controlHeight },
                              inner.rowControl(rowIndex));
    }
}

void FxCardComponent::layoutKnobRow(int rowIndex, const Row& row, bool feature)
{
    auto flex = inner.rowFlex(rowIndex);
    const auto gap = inner.rowGap(rowIndex);
    const auto content = inner.rowContent(rowIndex);
    const auto rowWidth = static_cast<float>(juce::jmax(1, content.getWidth()));

    const auto key = feature ? ".controls.featureKnobSize" : ".controls.knobSize";
    const auto fallback = feature ? kDefaultFeatureKnobCell : kDefaultKnobCell;
    const auto cellWidth = uiConfig != nullptr
                               ? uiConfig->getFloat("cards." + styleKey + key, fallback)
                               : fallback;
    const auto readoutHeight = uiConfig != nullptr
                                   ? uiConfig->getInt("cards." + styleKey + ".controls.readoutHeight", kDefaultReadoutHeight)
                                   : kDefaultReadoutHeight;

    const std::vector<float> widths(row.ids.size(), cellWidth);
    const auto gapWidth = gap.left + gap.right;
    const auto lines = wrappedLineCount(widths, gapWidth, rowWidth);
    const auto cellHeight = juce::jmax(1.0f,
                                       static_cast<float>(content.getHeight()) / static_cast<float>(lines)
                                           - (gap.top + gap.bottom));

    for (const auto width : widths)
    {
        flex.items.add(juce::FlexItem(width, cellHeight).withMargin(gap));
    }
    flex.performLayout(content.toFloat());

    for (std::size_t i = 0; i < row.ids.size(); ++i)
    {
        const auto it = std::find_if(knobs.begin(), knobs.end(),
                                     [&row, i](const KnobEntry& e) { return e.id == row.ids[i]; });
        if (it == knobs.end())
        {
            continue;
        }
        // A paired knob needs a second caption line, so its cell gives the
        // readout twice the height and the two chips split it. An unpaired
        // knob is laid out exactly as it always was.
        const auto paired = it->altLabel != nullptr;
        const auto readout = paired ? readoutHeight * 2 : readoutHeight;

        // Square, so a knob stays a circle. Only dropdowns stretch.
        layoutLabelledControl(flex.items.getReference(static_cast<int>(i)).currentBounds.toNearestInt(),
                              { nullptr, it->knob.get(), it->label.get(),
                                ControlShape::square, 0, readout, static_cast<int>(cellWidth) },
                              inner.rowControl(rowIndex));

        if (paired)
        {
            // The primary chip was given both lines; split it and hand the
            // lower one to the alternate.
            auto captions = it->label->getBounds();
            const auto half = captions.getHeight() / 2;
            it->label->setBounds(captions.removeFromTop(half));
            it->altLabel->setBounds(captions);

            // The two sliders occupy exactly the same circle. Only one is ever
            // visible, so they cannot overlap on screen.
            it->altKnob->setBounds(it->knob->getBounds());
        }
    }
}

juce::String FxCardComponent::debugLayoutSignature() const
{
    const auto rect = [](const juce::Component* component)
    {
        if (component == nullptr) { return juce::String("absent"); }
        const auto b = component->getBounds();
        return juce::String(b.getX()) + "," + juce::String(b.getY()) + ","
             + juce::String(b.getWidth()) + "," + juce::String(b.getHeight());
    };

    const auto kindName = [](RowKind kind)
    {
        switch (kind)
        {
            case RowKind::toggles:     return "toggles";
            case RowKind::choices:     return "choices";
            case RowKind::knobs:       return "knobs";
            case RowKind::featureKnob: return "feature";
        }
        return "?";
    };

    juce::StringArray lines;

    // The palette as well as the geometry. A card that is laid out identically
    // and painted in the wrong colours is still not the same card, and that is
    // exactly what a standalone effect looked like when it could not find the
    // config these come from.
    const auto& cardStyle = card.style();
    lines.add("accent " + accent.toDisplayString(true));
    lines.add("border " + cardStyle.border.colour.toDisplayString(true));
    lines.add("background " + cardStyle.background.colour.toDisplayString(true));
    lines.add("title " + cardStyle.title.colour.toDisplayString(true));
    // Artwork counts as palette: a card carrying a picture in one product and
    // not the other is not the same card, however well its knobs line up.
    lines.add("artwork " + (cardStyle.artwork.image.isNotEmpty() ? cardStyle.artwork.image
                                                                 : juce::String("none"))
              + " @" + juce::String(cardStyle.artwork.opacity, 3)
              + " " + describeArtworkFit(cardStyle.artwork.fit)
              + "/" + describeArtworkAlign(cardStyle.artwork.align));

    lines.add("bypass " + rect(&bypass));

    for (std::size_t r = 0; r < rows.size(); ++r)
    {
        const auto& row = rows[r];
        juce::String line = "row" + juce::String(static_cast<int>(r)) + " " + kindName(row.kind);

        for (const auto& id : row.ids)
        {
            // Looked up by id rather than by walking the entry vectors, so the
            // signature follows the row's declared ORDER - which is the thing
            // being compared - instead of the order things happened to be
            // constructed in.
            const juce::Component* control = knob(id);
            const juce::Component* label = knobLabel(id);

            if (control == nullptr) { control = choice(id); }
            if (control == nullptr) { control = toggle(id); }

            line += " | " + id + " " + rect(control);

            // The knob properties that CHANGE HOW IT DRAWS. Bypass greyscale
            // is a property, not a colour or a position, so a knob missing it
            // sits in exactly the right place in exactly the right palette and
            // still draws as a different control.
            //
            // Named individually rather than dumped wholesale: the Synth also
            // hangs px3ParamId on its knobs for MIDI mapping and modulation,
            // which a standalone effect has no use for and should not be
            // required to carry to count as the same card.
            if (const auto* slider = dynamic_cast<const juce::Slider*>(control))
            {
                const auto& properties = slider->getProperties();
                juce::StringArray drawing;
                for (const auto* name : { "psychedelicBypassGray" })
                {
                    if (properties.contains(name))
                    {
                        drawing.add(juce::String(name) + "="
                                    + properties[name].toString());
                    }
                }
                if (! drawing.isEmpty()) { line += " draws[" + drawing.joinIntoString(",") + "]"; }
            }

            if (label != nullptr) { line += " label " + rect(label); }
        }

        lines.add(line);
    }

    return lines.joinIntoString("\n");
}

void FxCardComponent::resized()
{
    card.setStyleKey(styleKey);
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    // The power glyph and every chip light in this card's own identity colour,
    // so an engaged control reads as part of this card.
    bypass.setAccentColour(card.style().border.colour);
    for (auto& entry : toggles)
    {
        entry.button->setAccentColour(card.style().border.colour);
    }

    inner.setStylePath("cards." + styleKey + ".cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(static_cast<int>(rows.size()));
    inner.layout(card.contentBelowTitle());

    // Pinned to cardInner's corner, outside the flex flow, so it stays put no
    // matter what the first row contains.
    bypass.setBounds(inner.powerBounds());

    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const auto index = static_cast<int>(i);
        switch (rows[i].kind)
        {
            case RowKind::toggles:     layoutToggleRow(index, rows[i]); break;
            case RowKind::choices:     layoutChoiceRow(index, rows[i]); break;
            case RowKind::featureKnob: layoutKnobRow(index, rows[i], true); break;
            case RowKind::knobs:       layoutKnobRow(index, rows[i], false); break;
        }
    }
}

void FxCardComponent::paint(juce::Graphics& g)
{
    card.setStyleKey(styleKey);
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    if (isActive)
    {
        card.draw(g, title);
    }
    else
    {
        card.drawInactive(g, title);
    }
}

void FxCardComponent::mouseUp(const juce::MouseEvent& event)
{
    // Clicking the card's background toggles its power, the same as clicking
    // the button. No tooltip: the whole card is not a control, and a card that
    // explained itself on hover would be noise.
    if (isCardBackgroundToggleClick(event))
    {
        bypass.setToggleState(! bypass.getToggleState(), juce::sendNotification);
    }
}

} // namespace px3::ui
