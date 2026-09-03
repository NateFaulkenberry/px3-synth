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
    // The card's own bypass switch, which every FX card has.
    void attachBypass(juce::AudioParameterBool& parameter);

    // Called once the product has declared and attached everything.
    void finishSetup(int width, int height);

private:
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
