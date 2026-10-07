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
#include "ReverbCard.h"
#include "ParameterKnob.h"
#include "KnobOverlays.h"
#include "Card.h"
#include "DoomCardLayout.h"
#include "LucyCardLayout.h"
#include "FxCardDeclarations.h"
#include "MoodCaptions.h"
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

    delayBypassButton.setToggleState(delayEnabled, juce::dontSendNotification);
    moodBypassButton.setToggleState(moodEnabled, juce::dontSendNotification);

    if (fxPanel != nullptr)
    {
        fxPanel->setActive(delayEnabled, granularModeSelectable, moodEnabled, reverbEnabled);
        fxPanel->setDelayAlgorithm(audioProcessor.getDelayAlgorithmParam().getIndex());
    }

    // MOOD's mode-dependent captions (MoodCaptions.h, shared with PX3 Mood).
    px3::ui::moodCaptions::applyAll(audioProcessor.getMoodWetModeParam().getIndex(),
                                    audioProcessor.getMoodLoopModeParam().getIndex(),
                                    moodWetTimeLabel, moodWetTimeKnob,
                                    moodWetModifyLabel, moodWetModifyKnob,
                                    moodLoopModifyLabel, moodLoopModifyKnob);

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
        px3::ui::lucyLayout::syncSpeed(*lucyCard, audioProcessor.getLucyModeParam().getIndex(),
                                       audioProcessor.getLucyPacketsParam().getIndex(),
                                       audioProcessor.getLucyFreezeParam().getIndex());
    }

    // Reverb greys out with the rest. It was left out when it became a card,
    // so bypassing it dimmed the signal-flow node and left the card lit.
    const auto reverbEnabled2 = audioProcessor.getReverbEnabledParam().get();
    if (reverbCard != nullptr)
    {
        reverbCard->bypassButton().setToggleState(reverbEnabled2, juce::dontSendNotification);
        reverbCard->setActive(reverbEnabled2);
        // Per-type slot, captions, dimming and the PRESET menu (ReverbCard.h,
        // shared with PX3 Reverb).
        const auto type = audioProcessor.getReverbAlgorithmParam().getIndex();
        px3::ui::reverbCard::syncControls(*reverbCard, type);
        if (auto* box = reverbCard->choice("preset"))
        {
            px3::ui::reverbCard::syncPresetMenu(*box, type, audioProcessor.getReverbPresetSelection(),
                                                [this](px3::reverb::Control c)
                                                { return static_cast<const juce::RangedAudioParameter&>(audioProcessor.getReverbControlParam(c)).getValue(); });
        }
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

    const auto* driveEnabledParam = audioProcessor.findRangedParameterById("fx.distortion.enabled");
    const auto driveEnabled = driveEnabledParam == nullptr || driveEnabledParam->getValue() >= 0.5f;
    if (driveCard != nullptr)
    {
        driveCard->bypassButton().setToggleState(driveEnabled, juce::dontSendNotification);
        driveCard->setActive(driveEnabled);
    }
    if (vibeCard != nullptr)
    {
        vibeCard->bypassButton().setToggleState(vibeEnabled, juce::dontSendNotification);
        vibeCard->setActive(vibeEnabled);
    }
    const auto analogEnabled = audioProcessor.getAnalogDriftEnabledParam().get();
    if (analogCard != nullptr)
    {
        analogCard->bypassButton().setToggleState(analogEnabled, juce::dontSendNotification);
        analogCard->setActive(analogEnabled);
    }

    if (fxPanel != nullptr)
    {
        fxPanel->setSectionActive(px3::fxStageDistortion, driveEnabled);
        fxPanel->setSectionActive(px3::fxStageVibe, vibeEnabled);
        fxPanel->setSectionActive(px3::fxStageAnalog, analogEnabled);
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

// Attaches every knob, choice and the bypass of a card by PARAMETER ID, looked
// up on the processor - the IDs are the contract, the card ids are local names.
void PX3SynthAudioProcessorEditor::attachCardControls(
    px3::ui::FxCardComponent& card,
    const std::vector<std::pair<const char*, const char*>>& knobs,
    std::initializer_list<std::pair<const char*, const char*>> choices,
    const char* enabledParameterId)
{
    for (const auto& [cardId, parameterId] : knobs)
    {
        auto* slider = card.knob(cardId);
        auto* parameter = audioProcessor.findRangedParameterById(parameterId);
        jassert(slider != nullptr && parameter != nullptr);
        if (slider == nullptr || parameter == nullptr) { continue; }
        const auto& range = parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*parameter, *slider);
    }
    for (const auto& [cardId, parameterId] : choices)
    {
        auto* box = card.choice(cardId);
        auto* parameter = audioProcessor.findRangedParameterById(parameterId);
        jassert(box != nullptr && parameter != nullptr);
        if (box != nullptr && parameter != nullptr) { attachComboBox(*parameter, *box); }
    }
    if (auto* enabled = audioProcessor.findRangedParameterById(enabledParameterId))
    {
        attachButton(*enabled, card.bypassButton());
    }
}

void PX3SynthAudioProcessorEditor::buildReverbCard()
{
    // The same card the standalone PX3 Reverb is, row for row
    // (FxCardDeclarations.h).
    auto card = std::make_unique<px3::ui::FxCardComponent>("reverb", "REVERB");
    px3::ui::fxcards::declareReverbRows(*card, audioProcessor.getReverbAlgorithmParam().choices);
    std::vector<std::pair<const char*, const char*>> knobs { { "amount", "fx.reverb.amount" } };
    for (const auto& spec : px3::reverb::kParameterSpecs)
        knobs.push_back({ px3::ui::reverbCard::kKnobIds[static_cast<int>(spec.control)], spec.id });
    attachCardControls(*card, knobs, { { "algorithm", "fx.reverb.algorithm" } }, "fx.reverb.enabled");
    if (auto* box = card->choice("preset"))
    {
        px3::ui::reverbCard::wirePresetMenu(*box, [this] { return audioProcessor.getReverbAlgorithmParam().getIndex(); },
                                            [this](int type, const juce::String& name) { audioProcessor.applyReverbPreset(type, name); });
    }
    reverbCard = card.get();
    fxPanel->addCard(px3::fxStageReverb, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildChorusCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("chorus", "CHORUS");
    px3::ui::fxcards::declareChorusRows(*card, audioProcessor.getChorusModeParam().choices);
    attachCardControls(*card,
                       { { "amount", "fx.chorus.amount" }, { "rate", "fx.chorus.rate" },
                         { "depth", "fx.chorus.depth" }, { "width", "fx.chorus.width" },
                         { "spread", "fx.chorus.spread" }, { "tone", "fx.chorus.tone" },
                         { "lowCut", "fx.chorus.low.cut" }, { "feedback", "fx.chorus.feedback" },
                         { "character", "fx.chorus.character" }, { "mix", "fx.chorus.mix" } },
                       { { "mode", "fx.chorus.mode" } },
                       "fx.chorus.enabled");
    chorusCard = card.get();
    fxPanel->addCard(px3::fxStageChorus, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildStereoSpreadCard()
{
    // Spread runs on the master bus, not in the send chain: the FX page shows
    // it after the chain, outside the reorderable strip. Basic view: WIDTH,
    // LOW MONO, MIX, AMOUNT; ADVANCED unfolds the rest.
    auto card = std::make_unique<px3::ui::FxCardComponent>("stereoSpread", "SPREAD");
    px3::ui::fxcards::declareSpreadRows(*card, audioProcessor.getSpreadModeParam().choices);
    px3::ui::fxcards::wireAdvancedSwitch(*card);
    attachCardControls(*card,
                       { { "amount", "fx.spread.amount" }, { "width", "fx.spread.width" },
                         { "depth", "fx.spread.depth" }, { "center", "fx.spread.center" },
                         { "tone", "fx.spread.tone" }, { "lowWidth", "fx.spread.low.width" },
                         { "highWidth", "fx.spread.high.width" }, { "lowFreq", "fx.spread.low.freq" },
                         { "highFreq", "fx.spread.high.freq" }, { "mix", "fx.spread.mix" } },
                       { { "mode", "fx.spread.mode" } },
                       "fx.spread.enabled");
    spreadCard = card.get();
    fxPanel->addCard(px3::fxStageStereoSpread, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildDriveCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("drive", "DRIVE");
    auto* type = dynamic_cast<juce::AudioParameterChoice*>(
        audioProcessor.findRangedParameterById("fx.distortion.type"));
    px3::ui::fxcards::declareDriveRows(*card, type != nullptr ? type->choices
                                                              : juce::StringArray { "SOFT", "HARD", "ASYM" });
    attachCardControls(*card,
                       { { "drive", "fx.distortion.drive" }, { "tight", "fx.distortion.tight" },
                         { "tone", "fx.distortion.tone" }, { "level", "fx.distortion.level" },
                         { "mix", "fx.distortion.mix" } },
                       { { "type", "fx.distortion.type" } },
                       "fx.distortion.enabled");
    driveCard = card.get();
    fxPanel->addCard(px3::fxStageDistortion, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildVibeCard()
{
    // VIBE: the Uni-Vibe, a stage in the FX send chain.
    auto card = std::make_unique<px3::ui::FxCardComponent>("vibe", "VIBE");
    auto* mode = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.findRangedParameterById("fx.vibe.mode"));
    auto* stereo = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.findRangedParameterById("fx.vibe.stereo"));
    px3::ui::fxcards::declareVibeRows(*card,
                                      mode != nullptr ? mode->choices : juce::StringArray { "CHORUS", "VIBRATO" },
                                      stereo != nullptr ? stereo->choices : juce::StringArray { "LINKED", "INVERTED" });
    attachCardControls(*card,
                       { { "speed", "fx.vibe.speed" }, { "intensity", "fx.vibe.intensity" },
                         { "level", "fx.vibe.level" } },
                       { { "mode", "fx.vibe.mode" }, { "stereo", "fx.vibe.stereo" } },
                       "fx.vibe.enabled");
    vibeCard = card.get();
    fxPanel->addCard(px3::fxStageVibe, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildAnalogCard()
{
    // ANALOG: per-voice analog drift and nonlinearity. It runs inside the
    // voices, so its card sits ahead of the send chain and has no strip node.
    auto card = std::make_unique<px3::ui::FxCardComponent>("analog", "ANALOG");
    auto* type = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.findRangedParameterById("fx.analog.type"));
    px3::ui::fxcards::declareAnalogRows(*card, type != nullptr ? type->choices : juce::StringArray {});
    attachCardControls(*card,
                       { { "amount", "fx.analog.amount" } },
                       { { "type", "fx.analog.type" } },
                       "fx.analog.enabled");
    analogCard = card.get();
    fxPanel->addCard(px3::fxStageAnalog, std::move(card));
}
