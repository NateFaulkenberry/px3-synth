// The FX cards: how each effect's card is BUILT, and the chain order it sits in.
//
// Split out of PluginEditor.cpp, which had grown to 4,400 lines. These are
// member functions of the same class, so this needs no change to the header -
// PluginEditorLook.cpp and PluginEditorDebug.cpp work the same way.
//
// Four card builders, the bypass refresh that greys them, and the two methods
// that move a card in the chain. They share no state with the rest of the
// editor beyond the members they are already members of, and they reach for
// nothing that was file-local to PluginEditor.cpp.

#include "PluginEditor.h"
#include "ParameterKnob.h"
#include "KnobOverlays.h"
#include "Card.h"
#include "DoomCardLayout.h"
#include "LucyCardLayout.h"
#include "UIConfig.h"
#include "PluginProcessorInternals.h"

#include <algorithm>
#include <cmath>

void PX3SynthAudioProcessorEditor::applyFxChainOrder(const px3::FxOrder& order,
                                                     const juce::String& source,
                                                     const juce::String& reason,
                                                     int fromIndex,
                                                     int toIndex)
{
    fxSectionOrder = order;
    commitFxOrderToProcessor(source, reason, fromIndex, toIndex);

    if (fxPanel != nullptr)
    {
        fxPanel->setChainOrder(fxSectionOrder);
    }
}

void PX3SynthAudioProcessorEditor::commitFxOrderToProcessor(const juce::String& source,
                                                                const juce::String& reason,
                                                                int fromIndex,
                                                                int toIndex)
{
    audioProcessor.setFxProcessingOrderWithReason(fxSectionOrder, source, reason, fromIndex, toIndex);
}
void PX3SynthAudioProcessorEditor::refreshFxBypassUI()
{
    const auto vibeEnabled = audioProcessor.getVibeEnabledParam().get();
    const auto delayEnabled = audioProcessor.getDelayEnabledParam().get();
    const auto delayIsGranular = audioProcessor.getDelayAlgorithmParam().getIndex() == 0;
    const auto granularModeSelectable = delayEnabled && delayIsGranular;
    const auto moodEnabled = audioProcessor.getMoodEnabledParam().get();
    const auto reverbEnabled = audioProcessor.getReverbEnabledParam().get();

    robBypassButton.setToggleState(vibeEnabled, juce::dontSendNotification);
    delayBypassButton.setToggleState(delayEnabled, juce::dontSendNotification);
    moodBypassButton.setToggleState(moodEnabled, juce::dontSendNotification);

    if (fxPanel != nullptr)
    {
        fxPanel->setActive(vibeEnabled, delayEnabled, granularModeSelectable, moodEnabled, reverbEnabled);
    }

    // Cards that own their controls grey themselves out; the panel is told
    // separately so the signal-flow node dims with them.
    const auto doomEnabled = audioProcessor.getDoomEnabledParam().get();
    if (doomCard != nullptr)
    {
        doomCard->bypassButton().setToggleState(doomEnabled, juce::dontSendNotification);
        doomCard->setActive(doomEnabled);
    }
    const auto lucyEnabled = audioProcessor.getLucyEnabledParam().get();
    if (lucyCard != nullptr)
    {
        lucyCard->bypassButton().setToggleState(lucyEnabled, juce::dontSendNotification);
        lucyCard->setActive(lucyEnabled);
    }

    // Reverb greys out with the rest. It was left out when it became a card,
    // so bypassing it dimmed the signal-flow node and left the card lit.
    const auto reverbEnabled2 = audioProcessor.getReverbEnabledParam().get();
    if (reverbCard != nullptr)
    {
        reverbCard->bypassButton().setToggleState(reverbEnabled2, juce::dontSendNotification);
        reverbCard->setActive(reverbEnabled2);
    }

    const auto chorusEnabled = audioProcessor.getChorusEnabledParam().get();
    if (chorusCard != nullptr)
    {
        chorusCard->bypassButton().setToggleState(chorusEnabled, juce::dontSendNotification);
        chorusCard->setActive(chorusEnabled);
    }

    const auto spreadEnabled = audioProcessor.getSpreadEnabledParam().get();
    if (spreadCard != nullptr)
    {
        spreadCard->bypassButton().setToggleState(spreadEnabled, juce::dontSendNotification);
        spreadCard->setActive(spreadEnabled);
    }

    if (fxPanel != nullptr)
    {
        fxPanel->setSectionActive(px3::fxStageDoom, doomEnabled);
        fxPanel->setSectionActive(px3::fxStageLucy, lucyEnabled);
        fxPanel->setSectionActive(px3::fxStageChorus, chorusEnabled);
        fxPanel->setSectionActive(px3::fxStageStereoSpread, spreadEnabled);
    }
}

void PX3SynthAudioProcessorEditor::buildDoomCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("doom", "DOOM");

    // ONE declaration, shared with the standalone. See DoomCardLayout.h.
    px3::ui::doomLayout::declareRows(*card,
                                     audioProcessor.getDoomWetModeParam().choices,
                                     audioProcessor.getDoomLoopModeParam().choices,
                                     audioProcessor.getDoomRoutingParam().choices);

    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 14> knobAttachments { {
        // the six primaries
        { "wetTime", &audioProcessor.getDoomWetTimeParam() },
        { "wetModify", &audioProcessor.getDoomWetModifyParam() },
        { "loopLength", &audioProcessor.getDoomLoopLengthParam() },
        { "loopModify", &audioProcessor.getDoomLoopModifyParam() },
        { "clock", &audioProcessor.getDoomClockParam() },
        { "mix", &audioProcessor.getDoomMixParam() },
        // their alternates
        { "cross", &audioProcessor.getDoomCrossParam() },
        { "eq", &audioProcessor.getDoomEqParam() },
        { "fade", &audioProcessor.getDoomFadeParam() },
        { "blend", &audioProcessor.getDoomBlendParam() },
        { "glue", &audioProcessor.getDoomGlueParam() },
        { "balance", &audioProcessor.getDoomBalanceParam() },
        // and the two that are not on the pedal's face
        { "overdub", &audioProcessor.getDoomOverdubParam() },
        { "spread", &audioProcessor.getDoomSpreadParam() },
    } };

    for (const auto& attachment : knobAttachments)
    {
        auto* slider = card->knob(attachment.id);
        jassert(slider != nullptr);
        const auto& range = attachment.parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*attachment.parameter, *slider);
    }

    struct ChoiceAttachment { const char* id; juce::RangedAudioParameter* parameter; };
    const std::array<ChoiceAttachment, 3> choiceAttachments { {
        { "wetMode", &audioProcessor.getDoomWetModeParam() },
        { "loopMode", &audioProcessor.getDoomLoopModeParam() },
        { "routing", &audioProcessor.getDoomRoutingParam() },
    } };

    for (const auto& attachment : choiceAttachments)
    {
        auto* box = card->choice(attachment.id);
        jassert(box != nullptr);
        attachComboBox(*attachment.parameter, *box);
    }

    struct ToggleAttachment { const char* id; juce::RangedAudioParameter* parameter; };
    const std::array<ToggleAttachment, 6> toggleAttachments { {
        { "loopActive", &audioProcessor.getDoomLoopActiveParam() },
        { "wetActive", &audioProcessor.getDoomWetActiveParam() },
        { "freeze", &audioProcessor.getDoomFreezeParam() },
        { "loopHalf", &audioProcessor.getDoomLoopHalfParam() },
        { "clockSmooth", &audioProcessor.getDoomClockSmoothParam() },
        { "crossSource", &audioProcessor.getDoomCrossSourceParam() },
    } };

    for (const auto& attachment : toggleAttachments)
    {
        auto* button = card->toggle(attachment.id);
        jassert(button != nullptr);
        attachButton(*attachment.parameter, *button);
    }

    attachButton(audioProcessor.getDoomEnabledParam(), card->bypassButton());

    // ALT is deliberately NOT a parameter: it selects which function the six
    // paired knobs display, which is a property of this panel rather than of
    // the sound.
    px3::ui::doomLayout::wireAltSwitch(*card);

    doomCard = card.get();
    fxPanel->addCard(px3::fxStageDoom, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildLucyCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("lucy", "LUCY");

    // ONE declaration, shared with the standalone. See LucyCardLayout.h.
    px3::ui::lucyLayout::declareRows(*card,
                                     audioProcessor.getLucyModeParam().choices,
                                     audioProcessor.getLucySlopeParam().choices,
                                     audioProcessor.getLucyPacketsParam().choices,
                                     audioProcessor.getLucyWeightingParam().choices,
                                     audioProcessor.getLucyFreezeParam().choices);

    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 13> knobAttachments { {
        // the six primaries
        { "filter", &audioProcessor.getLucyFilterParam() },
        { "verb", &audioProcessor.getLucyVerbParam() },
        { "freq", &audioProcessor.getLucyFilterFreqParam() },
        { "speed", &audioProcessor.getLucySpeedParam() },
        { "loss", &audioProcessor.getLucyLossParam() },
        { "global", &audioProcessor.getLucyGlobalParam() },
        // their alternates, attached exactly the same way: each is a real
        // parameter with its own automation lane, and which one the panel is
        // showing has no bearing on it
        { "gate", &audioProcessor.getLucyGateThresholdParam() },
        { "decay", &audioProcessor.getLucyVerbDecayParam() },
        { "limiterThreshold", &audioProcessor.getLucyLimiterThresholdParam() },
        { "autoGain", &audioProcessor.getLucyAutoGainParam() },
        { "lossGain", &audioProcessor.getLucyLossGainParam() },
        { "freezer", &audioProcessor.getLucyFreezerParam() },
        { "spread", &audioProcessor.getLucySpreadParam() },
    } };

    for (const auto& attachment : knobAttachments)
    {
        auto* slider = card->knob(attachment.id);
        jassert(slider != nullptr);
        const auto& range = attachment.parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*attachment.parameter, *slider);
    }

    struct ChoiceAttachment { const char* id; juce::RangedAudioParameter* parameter; };
    const std::array<ChoiceAttachment, 5> choiceAttachments { {
        { "mode", &audioProcessor.getLucyModeParam() },
        { "packets", &audioProcessor.getLucyPacketsParam() },
        { "freeze", &audioProcessor.getLucyFreezeParam() },
        { "slope", &audioProcessor.getLucySlopeParam() },
        { "weighting", &audioProcessor.getLucyWeightingParam() },
    } };

    for (const auto& attachment : choiceAttachments)
    {
        auto* box = card->choice(attachment.id);
        jassert(box != nullptr);
        attachComboBox(*attachment.parameter, *box);
    }

    struct ToggleAttachment { const char* id; juce::RangedAudioParameter* parameter; };
    const std::array<ToggleAttachment, 4> toggleAttachments { {
        { "gateOn", &audioProcessor.getLucyGateParam() },
        { "verbPost", &audioProcessor.getLucyVerbPostParam() },
        { "filterInvert", &audioProcessor.getLucyFilterInvertParam() },
        { "slow", &audioProcessor.getLucySlowParam() },
    } };

    for (const auto& attachment : toggleAttachments)
    {
        auto* button = card->toggle(attachment.id);
        jassert(button != nullptr);
        attachButton(*attachment.parameter, *button);
    }

    attachButton(audioProcessor.getLucyEnabledParam(), card->bypassButton());

    // ALT is deliberately NOT a parameter: it selects which function the six
    // paired knobs display, which is a property of this panel rather than of
    // the sound.
    px3::ui::lucyLayout::wireAltSwitch(*card);

    lucyCard = card.get();
    fxPanel->addCard(px3::fxStageLucy, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildReverbCard()
{
    // The same card the standalone PX3 Reverb is, row for row.
    //
    // Reverb used to be a compact face here - a mode and an amount - while nine
    // registered, automatable parameters had no control anywhere in the Synth.
    // They were reachable only by automation, which is a strange place for a
    // reverb's decay to live. Built as a card, the two products are one UI.
    auto card = std::make_unique<px3::ui::FxCardComponent>("reverb", "REVERB");

    card->addChoiceRow({ { "algorithm", "MODE", "Room, plate, hall or cloud",
                           audioProcessor.getReverbAlgorithmParam().choices } });

    card->addKnobRow({ { "size", "SIZE", "Room size" },
                       { "decay", "DECAY", "How long the tail lasts" },
                       { "damping", "DAMPING", "How fast the top of the tail is lost" },
                       { "preDelay", "PRE", "Gap before the tail begins" } });

    // Three rows rather than one of five. Five cells overran the inner card at
    // the width it is drawn, clipping the outer captions off both edges. Split
    // by what the controls do - the tail's movement, then the cloud algorithm's
    // own pair - rather than at whatever number happens to fit.
    card->addKnobRow({ { "modDepth", "DEPTH", "Movement in the tail" },
                       { "modRate", "RATE", "How fast that movement is" },
                       { "width", "WIDTH", "Stereo spread of the tail" } });

    card->addKnobRow({ { "cloudFeedback", "REGEN", "Cloud regeneration" },
                       { "cloudDiffusion", "SMEAR", "Cloud smearing" } });

    card->addFeatureKnobRow({ "amount", "AMOUNT", "Dry against wet" });

    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 10> knobAttachments { {
        { "amount", &audioProcessor.getReverbAmountParam() },
        { "size", &audioProcessor.getReverbSizeParam() },
        { "decay", &audioProcessor.getReverbDecayParam() },
        { "damping", &audioProcessor.getReverbDampingParam() },
        { "preDelay", &audioProcessor.getReverbPreDelayParam() },
        { "modDepth", &audioProcessor.getReverbModDepthParam() },
        { "modRate", &audioProcessor.getReverbModRateParam() },
        { "width", &audioProcessor.getReverbWidthParam() },
        { "cloudFeedback", &audioProcessor.getReverbCloudFeedbackParam() },
        { "cloudDiffusion", &audioProcessor.getReverbCloudDiffusionParam() },
    } };

    for (const auto& attachment : knobAttachments)
    {
        auto* slider = card->knob(attachment.id);
        jassert(slider != nullptr);
        const auto& range = attachment.parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*attachment.parameter, *slider);
    }

    attachComboBox(audioProcessor.getReverbAlgorithmParam(), *card->choice("algorithm"));
    attachButton(audioProcessor.getReverbEnabledParam(), card->bypassButton());


    reverbCard = card.get();
    fxPanel->addCard(px3::fxStageReverb, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildChorusCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("chorus", "CHORUS");

    card->addChoiceRow({ { "mode", "MODE", "Dimension mode, ensemble, or CE-style",
                           audioProcessor.getChorusModeParam().choices } });

    card->addKnobRow({ { "rate", "RATE", "Modulation rate" },
                       { "depth", "DEPTH", "Modulation excursion" },
                       { "width", "WIDTH", "Stereo expansion of the wet pair" },
                       { "spread", "SPREAD", "Phase offset between the two delay paths" } });

    card->addKnobRow({ { "tone", "TONE", "Warm against clear, on the wet path only" },
                       { "lowCut", "LOW CUT", "Wet-path high-pass: what anchors the bass" },
                       { "feedback", "FEEDBACK", "Colour; capped short of flanging" },
                       { "character", "CHARACTER", "BBD emphasis, companding and bandwidth" },
                       { "mix", "MIX", "Final dry against wet" } });

    card->addFeatureKnobRow({ "amount", "AMOUNT", "Overall intensity" });

    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 10> knobAttachments { {
        { "amount", &audioProcessor.getChorusAmountParam() },
        { "rate", &audioProcessor.getChorusRateParam() },
        { "depth", &audioProcessor.getChorusDepthParam() },
        { "width", &audioProcessor.getChorusWidthParam() },
        { "spread", &audioProcessor.getChorusSpreadParam() },
        { "tone", &audioProcessor.getChorusToneParam() },
        { "lowCut", &audioProcessor.getChorusLowCutParam() },
        { "feedback", &audioProcessor.getChorusFeedbackParam() },
        { "character", &audioProcessor.getChorusCharacterParam() },
        { "mix", &audioProcessor.getChorusMixParam() },
    } };

    for (const auto& attachment : knobAttachments)
    {
        auto* slider = card->knob(attachment.id);
        jassert(slider != nullptr);
        const auto& range = attachment.parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*attachment.parameter, *slider);
    }

    attachComboBox(audioProcessor.getChorusModeParam(), *card->choice("mode"));
    attachButton(audioProcessor.getChorusEnabledParam(), card->bypassButton());

    chorusCard = card.get();
    fxPanel->addCard(px3::fxStageChorus, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildStereoSpreadCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("stereoSpread", "SPREAD");

    card->addChoiceRow({ { "mode", "MODE", "Widening strategy",
                           audioProcessor.getSpreadModeParam().choices } });

    card->addKnobRow({ { "width", "WIDTH", "Overall stereo expansion" },
                       { "depth", "DEPTH", "Decorrelation depth" },
                       { "center", "CENTER", "How strongly the middle is anchored" },
                       { "tone", "TONE", "Tilt on the side signal only" } });

    card->addKnobRow({ { "lowWidth", "LOW W", "Width permitted below the low crossover" },
                       { "highWidth", "HIGH W", "Width in the top band" },
                       { "lowFreq", "LOW XO", "Low crossover: below it, mono" },
                       { "highFreq", "HIGH XO", "High crossover: above it, level rather than phase" },
                       { "mix", "MIX", "Final dry against wet" } });

    card->addFeatureKnobRow({ "amount", "AMOUNT", "Overall amount of spatial processing" });

    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 10> knobAttachments { {
        { "amount", &audioProcessor.getSpreadAmountParam() },
        { "width", &audioProcessor.getSpreadWidthParam() },
        { "depth", &audioProcessor.getSpreadDepthParam() },
        { "center", &audioProcessor.getSpreadCenterParam() },
        { "tone", &audioProcessor.getSpreadToneParam() },
        { "lowWidth", &audioProcessor.getSpreadLowWidthParam() },
        { "highWidth", &audioProcessor.getSpreadHighWidthParam() },
        { "lowFreq", &audioProcessor.getSpreadLowFreqParam() },
        { "highFreq", &audioProcessor.getSpreadHighFreqParam() },
        { "mix", &audioProcessor.getSpreadMixParam() },
    } };

    for (const auto& attachment : knobAttachments)
    {
        auto* slider = card->knob(attachment.id);
        jassert(slider != nullptr);
        const auto& range = attachment.parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*attachment.parameter, *slider);
    }

    attachComboBox(audioProcessor.getSpreadModeParam(), *card->choice("mode"));
    attachButton(audioProcessor.getSpreadEnabledParam(), card->bypassButton());

    spreadCard = card.get();
    fxPanel->addCard(px3::fxStageStereoSpread, std::move(card));
}
