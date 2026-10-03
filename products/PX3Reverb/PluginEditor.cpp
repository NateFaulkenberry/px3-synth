#include "PluginEditor.h"
#include "FxCardDeclarations.h"

PX3ReverbAudioProcessorEditor::PX3ReverbAudioProcessorEditor(PX3ReverbAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "reverb", "REVERB")
{
    // The same rows the Synth builds: one declaration, FxCardDeclarations.h.
    px3::ui::fxcards::declareReverbRows(rows(), processorIn.algorithm().choices);

    attachKnob("amount", processorIn.amount());
    attachKnob("size", processorIn.size());
    attachKnob("decay", processorIn.decay());
    attachKnob("damping", processorIn.damping());
    attachKnob("preDelay", processorIn.preDelay());
    attachKnob("modDepth", processorIn.modDepth());
    attachKnob("modRate", processorIn.modRate());
    attachKnob("width", processorIn.width());
    attachKnob("cloudFeedback", processorIn.cloudFeedback());
    attachKnob("cloudDiffusion", processorIn.cloudDiffusion());
    for (auto* parameter : processorIn.getParameters())
    {
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(parameter);
            f != nullptr && f->getParameterID() == "fx.reverb.shimmer")
        {
            attachKnob("shimmer", *f);
        }
    }
    attachChoice("algorithm", processorIn.algorithm());
    attachBypass(processorIn.enabled());

    finishSetup();
}
