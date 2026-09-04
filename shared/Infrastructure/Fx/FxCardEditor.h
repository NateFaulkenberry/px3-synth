#pragma once

#include <JuceHeader.h>

#include "FxCardComponent.h"
#include "KnobLookAndFeel.h"
#include "UIConfig.h"

#include <memory>
#include <vector>

namespace px3::fx
{

// A standalone effect's window, when the effect is drawn as a card.
//
// The card, its style, the PX3 knob and the parameter attachments - the parts
// every card-shaped product repeats. A product's editor is then the rows it
// declares and the parameters it attaches, which is the only part that differs
// between one effect and the next.
//
// The card is shared/UI/Fx/FxCardComponent, the same one the Synth's FX panel
// builds, styled from the same UIConfig under the same key - so a standalone
// effect looks like its card inside the Synth because it is that card.
class FxCardEditor : public juce::AudioProcessorEditor
{
public:
    FxCardEditor(juce::AudioProcessor& processorIn,
                 const juce::String& styleKey,
                 const juce::String& title);
    ~FxCardEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    px3::ui::FxCardComponent& debugCard() { return card; }

protected:
    // Declare the rows first, then attach: a control has to exist before it
    // can be attached to anything.
    px3::ui::FxCardComponent& rows() { return card; }

    // Attaches by the id the row declared. Sets the knob's range from the
    // parameter and gives it the PX3 look, which is what the Synth does for
    // every knob on every card.
    void attachKnob(const juce::String& id, juce::AudioParameterFloat& parameter);
    void attachChoice(const juce::String& id, juce::AudioParameterChoice& parameter);
    void attachToggle(const juce::String& id, juce::AudioParameterBool& parameter);
    // A two-value CHOICE shown as a toggle, which is what the Synth does for
    // Doom's cross source: the parameter has two named options and the card
    // says which one is selected rather than offering a dropdown of two.
    // ButtonParameterAttachment takes any RangedAudioParameter and maps 0 and 1
    // to off and on, so no parameter changes type to be drawn this way.
    void attachToggle(const juce::String& id, juce::RangedAudioParameter& parameter);
    // The card's own bypass switch, which every FX card has.
    void attachBypass(juce::AudioParameterBool& parameter);

    // Called once the product has declared and attached everything.
    //
    // The no-argument form sizes the window to ONE CELL of the Synth's FX grid,
    // so a standalone effect opens at the shape it has inside the Synth rather
    // than stretched into a landscape window. Both read the same UIConfig keys
    // the grid does. The explicit form remains for a product that genuinely
    // wants its own size.
    void finishSetup();
    void finishSetup(int width, int height);

private:
    // The ground visible around the card, on every side. Named because both
    // resized() and the window size derived from a grid cell have to agree
    // about it.
    static constexpr int kCardMargin = 10;

    px3::ui::FxCardComponent card;
    px3::ui::KnobLookAndFeel knobLook;
    std::shared_ptr<const UIConfig> uiConfig;

    std::vector<juce::Slider*> styledKnobs;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> boxAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxCardEditor)
};

} // namespace px3::fx
