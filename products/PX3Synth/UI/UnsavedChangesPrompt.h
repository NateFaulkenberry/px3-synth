#pragma once

#include <JuceHeader.h>

#include "ModalBackdrop.h"
#include "Theme.h"

#include <memory>
#include <vector>

// A question, asked in the preset sheet's faceplate over the dimmed, blurred
// backdrop: a title, one line, and two or three keys.
//
// Two questions use it. Before a preset switch would discard edits: SAVE /
// DON'T SAVE / CANCEL (ask()). And once per session when an update is waiting:
// UPDATE / CANCEL (askChoice()).
//
// It covers the whole editor, so nothing behind it can be clicked while the
// question is open - including the preset sheet, if that is where it came
// from. A click outside the card does nothing: dismissing by accident is what
// a question like this exists to prevent. Escape is the cancelling key and
// Return the default (solid) key, the platform convention.
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

        bool isSolid() const noexcept { return solid; }

    private:
        bool solid { false };
    };

    // One key of a question. `left` keys sit apart on the left (DON'T SAVE,
    // kept one slip away from SAVE); the rest are packed on the right in the
    // order given, so the default key goes last.
    struct Choice
    {
        juce::String label;
        int id { 0 };
        bool solid { false };      // the default: Return chooses it
        bool cancels { false };    // Escape chooses it
        bool left { false };
    };

    UnsavedChangesPrompt()
    {
        setName("UnsavedChangesPrompt");
        setWantsKeyboardFocus(true);
    }

    // The unsaved-changes question. `backdrop` is the editor as it looked when
    // the question was asked.
    void ask(juce::Image backdrop, std::function<void(Answer)> onAnswerIn)
    {
        askChoice("UNSAVED CHANGES", "Do you want to save your changes?",
                  { { "DON'T SAVE", static_cast<int>(Answer::discard), false, false, true },
                    { "CANCEL", static_cast<int>(Answer::cancel), false, true, false },
                    { "SAVE", static_cast<int>(Answer::save), true, false, false } },
                  std::move(backdrop),
                  [onAnswerIn = std::move(onAnswerIn)](int id)
                  {
                      if (onAnswerIn != nullptr) { onAnswerIn(static_cast<Answer>(id)); }
                  });
    }

    // Any question: a title, a line, and its keys. `onChoose` gets the chosen
    // key's id, once.
    void askChoice(const juce::String& titleIn, const juce::String& messageIn, std::vector<Choice> choicesIn,
                   juce::Image backdrop, std::function<void(int)> onChooseIn)
    {
        title = titleIn;
        message = messageIn;
        choices = std::move(choicesIn);
        keys.clear();
        for (const auto& choice : choices)
        {
            auto key = std::make_unique<Key>(choice.label, choice.solid);
            const auto id = choice.id;
            key->onClick = [this, id] { choose(id); };
            addAndMakeVisible(*key);
            keys.push_back(std::move(key));
        }
        snapshot = std::move(backdrop);
        treated = {};
        onChoose = std::move(onChooseIn);
        setVisible(true);
        toFront(true);
        grabKeyboardFocus();
        resized();
        repaint();
    }

    bool isAsking() const noexcept { return isVisible() && onChoose != nullptr; }
    juce::String debugTitle() const { return title; }
    juce::String debugMessage() const { return message; }
    juce::StringArray debugKeyLabels() const
    {
        juce::StringArray labels;
        for (const auto& c : choices) { labels.add(c.label); }
        return labels;
    }
    juce::Rectangle<int> debugCard() const noexcept { return card; }

    // Answers as if the key had been pressed - the keys and the tests both
    // come through here.
    void answer(Answer a) { choose(static_cast<int>(a)); }
    void choose(int id)
    {
        auto callback = std::move(onChoose);
        onChoose = nullptr;
        setVisible(false);
        snapshot = {};
        treated = {};
        if (callback != nullptr) { callback(id); }
    }

    void resized() override
    {
        namespace th = px3::ui::theme;
        const auto unit = static_cast<int>(th::space::unit);
        const auto band = static_cast<int>(std::ceil(th::space::minTitleBand));
        const auto keyHeight = 7 * unit;
        const auto padding = 4 * unit;

        // Every key the width the longest label needs at a comfortable size,
        // and the card wide enough for three of them - so a two-key question
        // is the same size as a three-key one - and never wider than the window.
        auto longest = 24.0f * th::space::unit;
        for (const auto& c : choices) { longest = juce::jmax(longest, th::textWidth(c.label, th::Type::label) + 6.0f * th::space::unit); }
        const auto keyWidth = juce::roundToInt(juce::jmax(longest, th::textWidth("DON'T SAVE", th::Type::label) + 6.0f * th::space::unit));
        const auto width = juce::jmin(getWidth() - 2 * unit, juce::jmax(90 * unit, 3 * keyWidth + 2 * unit + 2 * padding));
        const auto textWidth = static_cast<float>(width - 2 * padding);
        const auto lines = juce::jmax(1, static_cast<int>(std::ceil(th::textWidth(message, th::Type::control) / juce::jmax(1.0f, textWidth))) + 1);
        const auto lineHeight = juce::roundToInt(th::size(th::Type::control) * 1.5f);
        const auto height = band + padding + lines * lineHeight + padding + keyHeight + padding;

        card = getLocalBounds().withSizeKeepingCentre(width, height);
        auto body = card.withTrimmedTop(band).reduced(padding);
        auto row = body.removeFromBottom(keyHeight);
        messageArea = body.withTrimmedBottom(padding);

        for (std::size_t i = 0; i < choices.size() && i < keys.size(); ++i)
        {
            if (choices[i].left) { keys[i]->setBounds(row.removeFromLeft(keyWidth)); }
        }
        // The right-hand keys in the order given, the last at the far right.
        for (auto i = static_cast<int>(choices.size()) - 1; i >= 0; --i)
        {
            const auto idx = static_cast<std::size_t>(i);
            if (idx >= keys.size() || choices[idx].left) { continue; }
            keys[idx]->setBounds(row.removeFromRight(keyWidth));
            row.removeFromRight(2 * unit);
        }
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

        th::drawModulePanel(g, card.toFloat(), title, kAccent, true, th::space::minTitleBand);
        g.setColour(th::colour::textPrimary);
        g.setFont(th::font(th::Type::control));
        g.drawFittedText(message, messageArea, juce::Justification::centred, 4, 1.0f);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (! isAsking()) { return false; }
        for (const auto& c : choices)
        {
            if ((key == juce::KeyPress::escapeKey && c.cancels) || (key == juce::KeyPress::returnKey && c.solid))
            {
                choose(c.id);
                return true;
            }
        }
        return true;   // nothing behind the question takes keys while it is open
    }

    // Clicks outside the card land here and stop.
    void mouseDown(const juce::MouseEvent&) override {}

    static inline const juce::Colour kAccent { 0xff8abcff };   // the preset sheet's

private:
    juce::String title;
    juce::String message;
    std::vector<Choice> choices;
    std::vector<std::unique_ptr<Key>> keys;
    juce::Image snapshot;
    juce::Image treated;
    juce::Rectangle<int> card;
    juce::Rectangle<int> messageArea;
    std::function<void(int)> onChoose;
};
