#pragma once

#include <JuceHeader.h>

#include "ModalBackdrop.h"
#include "Theme.h"

// Asked before a preset switch would throw away edits: SAVE, DON'T SAVE or
// CANCEL. The same faceplate as the preset sheet and the EQ / COMP sheets, over
// the same dimmed and blurred backdrop.
//
// It covers the whole editor, so nothing behind it can be clicked while the
// question is open - including the preset sheet, if that is where the switch
// came from. A click outside the card does nothing: dismissing by accident is
// exactly what a save prompt exists to prevent. Escape is CANCEL and Return is
// SAVE, the platform convention.
class UnsavedChangesPrompt final : public juce::Component
{
public:
    enum class Answer { save, discard, cancel };

    // A sheet key. SOLID is the default action: a filled key in the sheet's
    // text colour, like LOAD PRESET. The others are outlined in that colour.
    class Key final : public juce::TextButton
    {
    public:
        Key(const juce::String& text, bool solidIn) : juce::TextButton(text), solid(solidIn)
        {
            setWantsKeyboardFocus(false);
        }

        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            namespace th = px3::ui::theme;
            const auto bounds = getLocalBounds().toFloat();
            auto ink = th::colour::textPrimary;
            if (down) { ink = ink.darker(0.18f); }
            else if (over) { ink = ink.brighter(0.25f); }

            if (solid)
            {
                g.setColour(ink);
                g.fillRect(bounds);
                g.setColour(th::colour::panelBottom);
            }
            else
            {
                if (over || down)
                {
                    g.setColour(th::colour::hover);
                    g.fillRect(bounds);
                }
                g.setColour(ink.withAlpha(0.55f));
                g.drawRect(bounds, 1.0f);
                g.setColour(ink);
            }
            g.setFont(th::font(th::Type::label).boldened());
            g.drawFittedText(getButtonText(), getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
        }

    private:
        bool solid { false };
    };

    UnsavedChangesPrompt()
    {
        setName("UnsavedChangesPrompt");
        setWantsKeyboardFocus(true);
        for (auto* key : { &saveKey, &discardKey, &cancelKey }) { addAndMakeVisible(*key); }
        saveKey.onClick = [this] { answer(Answer::save); };
        discardKey.onClick = [this] { answer(Answer::discard); };
        cancelKey.onClick = [this] { answer(Answer::cancel); };
    }

    // `backdrop` is the editor as it looked when the question was asked.
    void ask(juce::Image backdrop, std::function<void(Answer)> onAnswerIn)
    {
        snapshot = std::move(backdrop);
        treated = {};
        onAnswer = std::move(onAnswerIn);
        setVisible(true);
        toFront(true);
        grabKeyboardFocus();
        resized();
        repaint();
    }

    bool isAsking() const noexcept { return isVisible() && onAnswer != nullptr; }
    juce::String debugMessage() const { return message; }
    juce::Rectangle<int> debugCard() const noexcept { return card; }

    // Answers as if the key had been pressed - the keys and the tests both
    // come through here.
    void answer(Answer a)
    {
        auto callback = std::move(onAnswer);
        onAnswer = nullptr;
        setVisible(false);
        snapshot = {};
        treated = {};
        if (callback != nullptr) { callback(a); }
    }

    void resized() override
    {
        namespace th = px3::ui::theme;
        const auto unit = static_cast<int>(th::space::unit);
        const auto band = static_cast<int>(std::ceil(th::space::minTitleBand));
        const auto keyHeight = 7 * unit;
        const auto padding = 4 * unit;

        // As wide as the three keys need at a comfortable size, and never
        // wider than the window.
        const auto keyWidth = juce::roundToInt(juce::jmax(th::textWidth("DON'T SAVE", th::Type::label) + 6.0f * th::space::unit,
                                                          24.0f * th::space::unit));
        const auto width = juce::jmin(getWidth() - 2 * unit, juce::jmax(90 * unit, 3 * keyWidth + 2 * unit + 2 * padding));
        const auto textWidth = static_cast<float>(width - 2 * padding);
        const auto lines = juce::jmax(1, static_cast<int>(std::ceil(th::textWidth(message, th::Type::control) / juce::jmax(1.0f, textWidth))) + 1);
        const auto lineHeight = juce::roundToInt(th::size(th::Type::control) * 1.5f);
        const auto height = band + padding + lines * lineHeight + padding + keyHeight + padding;

        card = getLocalBounds().withSizeKeepingCentre(width, height);
        auto body = card.withTrimmedTop(band).reduced(padding);
        auto keys = body.removeFromBottom(keyHeight);
        messageArea = body.withTrimmedBottom(padding);

        // DON'T SAVE alone on the left, away from SAVE, so the two are not
        // one slip apart; CANCEL and SAVE on the right, SAVE last.
        discardKey.setBounds(keys.removeFromLeft(keyWidth));
        saveKey.setBounds(keys.removeFromRight(keyWidth));
        keys.removeFromRight(2 * unit);
        cancelKey.setBounds(keys.removeFromRight(keyWidth));
    }

    void paint(juce::Graphics& g) override
    {
        namespace th = px3::ui::theme;
        // The backdrop treatment is rendered once per question, then blitted.
        if (snapshot.isValid() && (treated.isNull() || treated.getWidth() != getWidth() || treated.getHeight() != getHeight()))
        {
            treated = juce::Image(juce::Image::ARGB, juce::jmax(1, getWidth()), juce::jmax(1, getHeight()), true);
            juce::Graphics tg(treated);
            px3::ui::paintModalBackdrop(tg, getLocalBounds(), {}, snapshot, 0.0f);
        }
        if (treated.isValid()) { g.drawImageAt(treated, 0, 0, false); }
        else { g.fillAll(juce::Colour::fromRGBA(0, 0, 0, 180)); }

        th::drawModulePanel(g, card.toFloat(), "UNSAVED CHANGES", kAccent, true, th::space::minTitleBand);
        g.setColour(th::colour::textPrimary);
        g.setFont(th::font(th::Type::control));
        g.drawFittedText(message, messageArea, juce::Justification::centred, 4, 1.0f);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (! isAsking()) { return false; }
        if (key == juce::KeyPress::escapeKey) { answer(Answer::cancel); return true; }
        if (key == juce::KeyPress::returnKey) { answer(Answer::save); return true; }
        return true;   // nothing behind the question takes keys while it is open
    }

    // Clicks outside the card land here and stop.
    void mouseDown(const juce::MouseEvent&) override {}

    static inline const juce::Colour kAccent { 0xff8abcff };   // the preset sheet's

    Key saveKey { "SAVE", true };
    Key discardKey { "DON'T SAVE", false };
    Key cancelKey { "CANCEL", false };

private:
    const juce::String message { "Do you want to save your changes?" };
    juce::Image snapshot;
    juce::Image treated;
    juce::Rectangle<int> card;
    juce::Rectangle<int> messageArea;
    std::function<void(Answer)> onAnswer;
};
