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

    // Two channels, so two rows of state before the knobs: which channel is
    // running, and the three global switches.
    // Every state toggle in one block, three across, then the three mode
    // selectors. The three-across limit is toggleMaxColumns in UIConfig, so the
    // chips keep their proportions as the card resizes.
    card->addToggleRow({ { "loopActive", "LOOPER", "LISTEN", "Play the captured micro-loop, or keep listening" },
                         { "wetActive", "WET ON", "WET OFF", "Engage the wet channel" },
                         { "freeze", "FROZEN", "FREEZE", "Freeze the wet channel and repeat it" },
                         { "loopHalf", "HALF", "FULL", "Halve the micro-loop length" },
                         { "clockSmooth", "SMOOTH", "STEPPED",
                           "Sweep the clock continuously instead of in harmonised steps" },
                         { "crossSource", "CROSS: CHAN", "CROSS: INPUT",
                           "Modulate from your playing, or let each channel modulate the other" } });

    card->addChoiceRow({ { "loopMode", "LOOP", "Micro-looper mode",
                           audioProcessor.getDoomLoopModeParam().choices },
                         { "routing", "ROUTE", "What the wet channel processes",
                           audioProcessor.getDoomRoutingParam().choices },
                         { "wetMode", "WET", "Wet channel mode",
                           audioProcessor.getDoomWetModeParam().choices } });

    card->addKnobRow({ { "clock", "CLOCK", "Engine sample rate: loop length, pitch and wet time at once" },
                       { "loopLength", "LENGTH", "Micro-looper length (mode dependent)" },
                       { "loopModify", "MODIFY", "Micro-looper character (mode dependent)" },
                       { "wetTime", "TIME", "Wet channel time (mode dependent)" },
                       { "wetModify", "SHAPE", "Wet channel character (mode dependent)" },
                       { "balance", "BALANCE", "Micro-looper against wet channel" } });

    card->addKnobRow({ { "cross", "CROSS", "Signal-dependent interference in pitch and loudness" },
                       { "glue", "GLUE", "End of chain saturator, then destroyer" },
                       { "eq", "EQ", "Tilt: left removes highs, right removes lows" },
                       { "overdub", "OVERDUB", "Record onto the micro-loop" },
                       { "fade", "FADE", "How much of the loop survives each lap while overdubbing" },
                       { "blend", "BLEND", "Clean micro-loop blended past the wet channel" },
                       { "spread", "SPREAD", "Stereo processing depth" } });

    card->addFeatureKnobRow({ "mix", "MIX", "Dry against DOOM" });

    // Attaching by id rather than by reference: the card owns the controls, and
    // a typo here is a null dereference at startup rather than a control that
    // silently does nothing.
    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 14> knobAttachments { {
        { "mix", &audioProcessor.getDoomMixParam() },
        { "clock", &audioProcessor.getDoomClockParam() },
        { "loopLength", &audioProcessor.getDoomLoopLengthParam() },
        { "loopModify", &audioProcessor.getDoomLoopModifyParam() },
        { "overdub", &audioProcessor.getDoomOverdubParam() },
        { "fade", &audioProcessor.getDoomFadeParam() },
        { "wetTime", &audioProcessor.getDoomWetTimeParam() },
        { "wetModify", &audioProcessor.getDoomWetModifyParam() },
        { "cross", &audioProcessor.getDoomCrossParam() },
        { "glue", &audioProcessor.getDoomGlueParam() },
        { "eq", &audioProcessor.getDoomEqParam() },
        { "balance", &audioProcessor.getDoomBalanceParam() },
        { "blend", &audioProcessor.getDoomBlendParam() },
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
        { "loopMode", &audioProcessor.getDoomLoopModeParam() },
        { "wetMode", &audioProcessor.getDoomWetModeParam() },
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

    // The macro knob wears the rainbow ring, the same as VIBE's amount.
    if (auto* knob = card->knob("mix"))
    {
        knob->getProperties().set("psychedelicFx", true);
        knob->getProperties().set("psychedelicInverted", true);
    }

    doomCard = card.get();
    fxPanel->addCard(px3::fxStageDoom, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildLucyCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("lucy", "LUCY");

    card->addToggleRow({ { "freeze", "FROZEN", "FREEZE", "Freeze the spectrum" },
                         { "freezeSlushy", "SLUSHY", "SOLID",
                           "Let the freeze keep updating from what you play" },
                         { "gate", "GATE ON", "GATE OFF", "Silence anything below the cutoff" },
                         { "verbPost", "V-POST", "V-PRE",
                           "Reverb after the chain, or in front of it feeding the loss" },
                         { "filterInvert", "REJECT", "PASS",
                           "Keep the band, or keep everything but the band" },
                         { "slow", "SLOW ON", "SLOW OFF",
                           "Bigger, darker, slower, and with more latency" } });

    card->addChoiceRow({ { "mode", "MODE", "Loss mode",
                           audioProcessor.getLucyModeParam().choices },
                         { "slope", "SLOPE", "Filter slope",
                           audioProcessor.getLucySlopeParam().choices },
                         { "packets", "PACKETS", "Packet corruption mode",
                           audioProcessor.getLucyPacketsParam().choices } });

    card->addKnobRow({ { "loss", "LOSS", "Depth of the loss and packet effects, and which frequencies they reach" },
                       { "speed", "SPEED", "How fast the loss, packets and freeze update" },
                       { "filter", "FILTER", "Filter width; fully down is no filtering" },
                       { "filterFreq", "FREQ", "Filter centre frequency" },
                       { "verb", "VERB", "Reverb mix" },
                       { "verbDecay", "DECAY", "Reverb size and length" } });

    card->addKnobRow({ { "freezer", "FREEZER", "Live against frozen" },
                       { "gateCutoff", "CUTOFF", "Gate threshold" },
                       { "threshold", "LIMIT", "Limiter threshold; lower means more limiting" },
                       { "autoGain", "AUTO GAIN", "Gain compensation for the loss modes" },
                       { "weighting", "WEIGHT", "Dark, psychoacoustic, or bright frequency weighting" },
                       { "gain", "GAIN", "Wet gain, plus or minus 36 dB" },
                       { "spread", "SPREAD", "Packet alternation and reverb width" } });

    card->addFeatureKnobRow({ "global", "GLOBAL", "Overall amount of processing" });

    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 14> knobAttachments { {
        { "global", &audioProcessor.getLucyGlobalParam() },
        { "loss", &audioProcessor.getLucyLossParam() },
        { "speed", &audioProcessor.getLucySpeedParam() },
        { "filter", &audioProcessor.getLucyFilterParam() },
        { "filterFreq", &audioProcessor.getLucyFilterFreqParam() },
        { "verb", &audioProcessor.getLucyVerbParam() },
        { "verbDecay", &audioProcessor.getLucyVerbDecayParam() },
        { "freezer", &audioProcessor.getLucyFreezerParam() },
        { "gateCutoff", &audioProcessor.getLucyGateCutoffParam() },
        { "threshold", &audioProcessor.getLucyThresholdParam() },
        { "autoGain", &audioProcessor.getLucyAutoGainParam() },
        { "weighting", &audioProcessor.getLucyWeightingParam() },
        { "gain", &audioProcessor.getLucyGainParam() },
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
    const std::array<ChoiceAttachment, 3> choiceAttachments { {
        { "mode", &audioProcessor.getLucyModeParam() },
        { "packets", &audioProcessor.getLucyPacketsParam() },
        { "slope", &audioProcessor.getLucySlopeParam() },
    } };

    for (const auto& attachment : choiceAttachments)
    {
        auto* box = card->choice(attachment.id);
        jassert(box != nullptr);
        attachComboBox(*attachment.parameter, *box);
    }

    struct ToggleAttachment { const char* id; juce::RangedAudioParameter* parameter; };
    const std::array<ToggleAttachment, 6> toggleAttachments { {
        { "freeze", &audioProcessor.getLucyFreezeParam() },
        { "freezeSlushy", &audioProcessor.getLucyFreezeSlushyParam() },
        { "gate", &audioProcessor.getLucyGateParam() },
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

    // The macro knob wears the rainbow ring, the same as VIBE's amount.
    if (auto* knob = card->knob("global"))
    {
        knob->getProperties().set("psychedelicFx", true);
    }

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
    card->addKnobRow({ { "modDepth", "MOD DEPTH", "Movement in the tail" },
                       { "modRate", "MOD RATE", "How fast that movement is" },
                       { "width", "WIDTH", "Stereo spread of the tail" } });

    card->addKnobRow({ { "cloudFeedback", "CLOUD FB", "Cloud regeneration" },
                       { "cloudDiffusion", "CLOUD DIFF", "Cloud smearing" } });

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

    // The rainbow ring every FX amount knob wears. The old reverb knob had it
    // and it went out with that knob; without it this one knob is the only
    // amount in the panel drawn as an ordinary control.
    if (auto* knob = card->knob("amount"))
    {
        knob->getProperties().set("psychedelicFx", true);
    }

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

    // The macro knob wears the rainbow ring, the same as VIBE's amount.
    if (auto* knob = card->knob("amount"))
    {
        knob->getProperties().set("psychedelicFx", true);
    }

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

    // The macro knob wears the rainbow ring, the same as VIBE's amount.
    if (auto* knob = card->knob("amount"))
    {
        knob->getProperties().set("psychedelicFx", true);
    }

    spreadCard = card.get();
    fxPanel->addCard(px3::fxStageStereoSpread, std::move(card));
}
