#pragma once

#include "BypassButton.h"
#include "Card.h"
#include "CardInner.h"
#include "ChipLabel.h"
#include "ToggleChipButton.h"

#include <JuceHeader.h>

#include <memory>
#include <vector>

class UIConfig;

namespace px3::ui
{

// An FX card that owns its own controls.
//
// The older FX components take every knob, label and box by reference in the
// constructor, which works at nine controls and does not at twenty-four. The
// controls still belong to one place and the editor still attaches parameters
// to them - what changes is that the card holds them and is asked for them by
// id, rather than the editor holding them and passing fifty references.
//
// Layout is declared as rows of controls and resolved by CardInner, so an FX
// with a different control set is a different declaration rather than a
// different resized().
class FxCardComponent final : public juce::Component
{
public:
    struct KnobSpec
    {
        juce::String id;
        juce::String label;
        juce::String tooltip;

        // THE ALTERNATE FUNCTION this knob also carries, if any.
        //
        // Modelled on the pedal LUCY follows, where six knobs each have a
        // second job printed underneath. Both parameters exist and are
        // attached at all times - the card holds TWO sliders in the one place
        // and shows whichever the ALT switch selects - so automating an
        // alternate never depends on what the panel is currently displaying.
        //
        // Empty altId means an ordinary single-function knob, which is what
        // every other card's knobs are.
        // Default member initialisers, so the twenty-odd existing specs that
        // brace-initialise three fields still compile without a warning.
        juce::String altId {};
        juce::String altLabel {};
        juce::String altTooltip {};
    };

    struct ChoiceSpec
    {
        juce::String id;
        juce::String label;
        juce::String tooltip;
        juce::StringArray choices;
    };

    struct ToggleSpec
    {
        juce::String id;
        juce::String onText;
        juce::String offText;
        juce::String tooltip;
    };

    // styleKey indexes UIConfig ("doom" -> cards.doom.*, cards.doom.cardInner).
    FxCardComponent(juce::String styleKey, juce::String title);
    ~FxCardComponent() override;

    // Rows are laid out top to bottom in the order they are added.
    void addToggleRow(std::vector<ToggleSpec> specs);
    void addChoiceRow(std::vector<ChoiceSpec> specs);
    void addKnobRow(std::vector<KnobSpec> specs);
    // One knob given its own row and drawn large - the macro control the rest
    // of the card feeds into.
    void addFeatureKnobRow(KnobSpec spec);

    // Null for an id the card does not have, so a wiring mistake is a crash at
    // the call site rather than a silently unattached control.
    // Answers for an alternate's id as well as a primary's, so attaching one
    // is the same call either way.
    juce::Slider* knob(const juce::String& id) const;

    // Which function the paired knobs are showing. Purely a display state: the
    // alternate parameters stay attached and automatable either way.
    void setAltMode(bool showAlternates);
    bool isAltMode() const noexcept { return altMode; }
    bool hasAlternates() const noexcept;
    juce::Label* knobLabel(const juce::String& id) const;
    juce::ComboBox* choice(const juce::String& id) const;
    juce::ToggleButton* toggle(const juce::String& id) const;
    BypassButton& bypassButton() noexcept { return bypass; }

    // Every control the card owns, for the editor's styling and teardown
    // passes. Ownership stays here.
    std::vector<juce::Slider*> allKnobs() const;
    std::vector<juce::Label*> allKnobLabels() const;
    std::vector<juce::ComboBox*> allChoices() const;

    void setAccentColour(juce::Colour colour);
    void setActive(bool enabled);
    void setUIConfig(std::shared_ptr<const UIConfig> config);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;

    // Everything this card lays out, as text: the rows in order, and each
    // control's id and bounds within the card.
    //
    // It exists so a standalone effect can be compared against the same card
    // inside the Synth and the difference NAMED. Two snapshots would say only
    // that they differ; this says which control moved and by how much, which
    // is the difference between a test that catches a regression and one that
    // only reports it.
    juce::String debugLayoutSignature() const;

private:
    enum class RowKind { toggles, choices, knobs, featureKnob };

    struct Row
    {
        RowKind kind { RowKind::knobs };
        std::vector<juce::String> ids;
    };

    struct KnobEntry
    {
        juce::String id;
        std::unique_ptr<juce::Slider> knob;
        std::unique_ptr<ChipLabel> label;

        // Both null unless the spec named an alternate. The alternate slider
        // shares the primary's bounds and only one of the two is visible.
        juce::String altId;
        std::unique_ptr<juce::Slider> altKnob;
        std::unique_ptr<ChipLabel> altLabel;
    };

    struct ChoiceEntry
    {
        juce::String id;
        std::unique_ptr<juce::ComboBox> box;
        std::unique_ptr<ChipLabel> label;
    };

    struct ToggleEntry
    {
        juce::String id;
        std::unique_ptr<ToggleChipButton> button;
    };

    void layoutToggleRow(int rowIndex, const Row& row);
    void layoutChoiceRow(int rowIndex, const Row& row);
    void layoutKnobRow(int rowIndex, const Row& row, bool feature);

    juce::String styleKey;
    juce::String title;

    CardHost card;
    CardInner inner;
    juce::Colour accent { juce::Colour::fromRGB(255, 198, 110) };
    std::shared_ptr<const UIConfig> uiConfig;
    bool isActive { true };

    BypassButton bypass;

    bool altMode { false };

    std::vector<Row> rows;
    std::vector<KnobEntry> knobs;
    std::vector<ChoiceEntry> choices;
    std::vector<ToggleEntry> toggles;
};

} // namespace px3::ui
