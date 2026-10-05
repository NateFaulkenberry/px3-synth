#include "PluginEditor.h"
#include "FxCardDeclarations.h"
#include "ReverbCard.h"

PX3ReverbAudioProcessorEditor::PX3ReverbAudioProcessorEditor(PX3ReverbAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "reverb", "REVERB"),
      reverbProcessor(processorIn)
{
    // The same rows the Synth builds: one declaration, FxCardDeclarations.h.
    px3::ui::fxcards::declareReverbRows(rows(), processorIn.algorithm().choices);

    attachKnob("amount", processorIn.amount());
    for (int c = 0; c < px3::reverb::kControlCount; ++c)
        attachKnob(px3::ui::reverbCard::kKnobIds[c], processorIn.control(static_cast<px3::reverb::Control>(c)));
    attachChoice("algorithm", processorIn.algorithm());
    attachBypass(processorIn.enabled());
    if (auto* box = rows().choice("preset"))
    {
        px3::ui::reverbCard::wirePresetMenu(*box, [this] { return reverbProcessor.algorithm().getIndex(); },
                                            [this](int type, const juce::String& name) { reverbProcessor.applyPreset(type, name); });
    }

    finishSetup();
    timerCallback();
    startTimerHz(15);
}

PX3ReverbAudioProcessorEditor::~PX3ReverbAudioProcessorEditor()
{
    stopTimer();
}

void PX3ReverbAudioProcessorEditor::timerCallback()
{
    const auto type = reverbProcessor.algorithm().getIndex();
    px3::ui::reverbCard::syncControls(rows(), type);
    if (auto* box = rows().choice("preset"))
    {
        px3::ui::reverbCard::syncPresetMenu(*box, type, reverbProcessor.getPresetSelection(),
                                            [this](px3::reverb::Control c) { return static_cast<juce::RangedAudioParameter&>(reverbProcessor.control(c)).getValue(); });
    }
}
