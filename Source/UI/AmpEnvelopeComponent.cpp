#include "AmpEnvelopeComponent.h"

#include "UIConfig.h"

AmpEnvelopeComponent::AmpEnvelopeComponent(PX3SynthAudioProcessor& processorIn, juce::Colour accentIn)
    : processor(processorIn)
{
    // AMP ENV edits index 0. It stays a separate component from ENV 1/2/3 and
    // reaches a different slot; what it shares with them is the editor, not the
    // envelope.
    enabledLabel.setText("ON", juce::dontSendNotification);
    enabledLabel.setJustificationType(juce::Justification::centredLeft);
    enabledLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    enabledLabel.setFont(juce::FontOptions(11.5f));
    enabledLabel.setInterceptsMouseClicks(false, false);

    assignLabel.setText("AMP", juce::dontSendNotification);
    assignLabel.setJustificationType(juce::Justification::centred);
    assignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    assignLabel.setFont(juce::FontOptions(11.5f));
    assignLabel.setInterceptsMouseClicks(false, false);

    enabledButton.setButtonText("");
    enabledButton.setClickingTogglesState(true);
    enabledButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
    enabledButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));

    assignBox.addItem("AMP", 1);
    assignBox.setSelectedId(1, juce::dontSendNotification);
    assignBox.setEnabled(false);
    assignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    assignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    assignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    envelopeGraph = std::make_unique<EnvelopeComponent>(processor.getAttackParam(),
                                                        processor.getDecayParam(),
                                                        processor.getSustainParam(),
                                                        processor.getReleaseParam(),
                                                        processor.getAmpEnvEnabledParam(),
                                                        enabledButton,
                                                        assignLabel,
                                                        assignBox,
                                                        nullptr,
                                                        nullptr,
                                                        nullptr,
                                                        accentIn,
                                                        "amp.env");

    // Slot 0 is AMP ENV. The parameters are written back for any four-point
    // skeleton, curved or not, which is what keeps the knobs under the graph
    // and a DAW's automation of ampAttack in step with a drag. Only once a
    // point has been ADDED is there no ADSR left to write, and the stored shape
    // is then the whole truth.
    envelopeGraph->setShapedEnvelope(processor.getShapedEnvelope(0));
    envelopeGraph->onEnvelopeEdited = [this](const px3::BreakpointEnvelope& edited)
    {
        processor.setShapedEnvelope(0, edited);

        if (edited.isAdsrSkeleton())
        {
            const auto adsr = edited.toAdsr();
            const auto write = [](juce::AudioParameterFloat& parameter, float value)
            {
                parameter.beginChangeGesture();
                parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
                parameter.endChangeGesture();
            };
            write(processor.getAttackParam(), adsr.attackSeconds);
            write(processor.getDecayParam(), adsr.decaySeconds);
            write(processor.getSustainParam(), adsr.sustainLevel);
            write(processor.getReleaseParam(), adsr.releaseSeconds);
        }
    };

    enabledButton.setVisible(false);
    enabledButton.setEnabled(false);
    enabledLabel.setVisible(false);
    enabledLabel.setEnabled(false);
    assignLabel.setVisible(false);
    assignLabel.setEnabled(false);
    assignBox.setVisible(false);
    assignBox.setEnabled(false);

    // "amp.env" would derive the key "env" and the title "ENV", and cards.env
    // does not exist - so the graph fell back to cards.defaults for both its
    // frame and its row heights. It carries the AMP ENV identity instead.
    envelopeGraph->setCardIdentity("ampEnv", "AMP ENV");

    // ATTACK | DECAY | SUSTAIN | RELEASE under the graph. The card keeps its
    // height and the graph gives the room up, so nothing below AMP ENV moves.
    envelopeGraph->setAdsrKnobsVisible(true);

    // Slot 0 is AMP ENV. The card owns the selector; which slot it edits is
    // the owner's business.
    envelopeGraph->setEnvelopeMode(processor.getEnvelopeMode(0));
    envelopeGraph->onEnvelopeModeChanged = [this](px3::BreakpointEnvelope::Mode mode)
    {
        processor.setEnvelopeMode(0, mode);
    };

    addAndMakeVisible(*envelopeGraph);
}

void AmpEnvelopeComponent::resized()
{
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setBounds(getLocalBounds());
    }
}

void AmpEnvelopeComponent::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    // This component draws nothing itself. It hosts the envelope graph, which
    // owns the one card - drawing a second one here is what put a generic ENV
    // card inside the AMP ENV card.
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setPanelContentBounds(panelContent);
    }
    repaint();
}


void AmpEnvelopeComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    if (configIn != nullptr)
    {
        const auto comboStyle = configIn->getObject("styles.combos.default");
        configIn->applyComboStyle(comboStyle, assignBox);
    }

    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setUIConfig(std::move(configIn));
    }
}

void AmpEnvelopeComponent::refreshFromParameters()
{
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->refreshFromParameters();

        // While the shape is still ADSR the parameters are authoritative, so the
        // graph is rebuilt from them - that is what makes a knob move the curve
        // and a DAW's automation move it too.
        // The parameters carry the four times and the level; the stored shape
        // carries the curves. On a skeleton they are applied together, so
        // turning a knob moves the graph without straightening what was drawn.
        const auto stored = processor.getShapedEnvelope(0);
        envelopeGraph->setShapedEnvelope(
            stored.isAdsrSkeleton()
                ? stored.withAdsrApplied(processor.currentAmpEnvelopeSettings())
                : stored);

        // Where the envelope being played has got to, taken from the DSP's own
        // runtime state rather than from a clock of the UI's - the graph
        // follows the sound instead of guessing alongside it.
        envelopeGraph->setEnvelopeMode(processor.getEnvelopeMode(0));
        envelopeGraph->setEnvelopeProgress(processor.getEnvelopeProgress(0));
    }
    repaint();
}
