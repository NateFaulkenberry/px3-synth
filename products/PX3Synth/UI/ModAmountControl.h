#pragma once

#include <JuceHeader.h>

#include "Theme.h"

#include <cmath>
#include <memory>

namespace px3::ui
{

// The amount as a musician reads it ("+35", "-12", "\xc2\xb1 3.5").
inline juce::String modAmountText(float depth, bool bipolar)
{
    const auto percent = depth * 100.0f;
    const auto magnitude = std::abs(percent);
    // Whole numbers read as "35"; a fine-tuned amount keeps its decimal ("3.5").
    const auto number = std::abs(magnitude - std::round(magnitude)) < 0.05f
                            ? juce::String(juce::roundToInt(magnitude))
                            : juce::String(magnitude, 1);
    if (bipolar)
    {
        // Bipolar sources swing both ways: +/- 35, or inverted -/+ 35.
        return juce::String(juce::CharPointer_UTF8(percent < 0.0f ? "\xe2\x88\x93 " : "\xc2\xb1 ")) + number;
    }
    return (percent < 0.0f ? juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")) : juce::String("+")) + number;
}

// A modulation amount: a centre-zero bar in the source's colour with the value
// printed as a musician reads it ("± 35", "+12", "± 3.5"). Drag for coarse,
// Cmd/Alt-drag for fine, mouse wheel / arrow keys for 1 % steps (0.1 % with
// Shift), the end arrows nudge, double-click types an exact number.
class ModAmountControl final : public juce::Slider
{
public:
    explicit ModAmountControl(juce::Colour colourIn = juce::Colour(0xff8f7cf0), bool bipolarIn = false)
        : colour(colourIn), bipolar(bipolarIn)
    {
        setComponentID("mod.routing.depth");
        setSliderStyle(juce::Slider::LinearHorizontal);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setRange(-1.0, 1.0, 0.0);
        setVelocityModeParameters(0.12, 1, 0.0, true,
                                  static_cast<juce::ModifierKeys::Flags>(juce::ModifierKeys::commandModifier
                                                                         | juce::ModifierKeys::altModifier));
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setTooltip("Amount: drag (Cmd/Alt = fine), wheel or arrow keys = 1 % (Shift = 0.1 %), "
                   "end arrows nudge, double-click to type");
    }

    // What the control prints, for tests and accessibility.
    juce::String readout() const { return modAmountText(static_cast<float>(getValue()), bipolar); }
    void setFillColour(juce::Colour c) { if (colour != c) { colour = c; repaint(); } }
    void setBipolar(bool shouldBeBipolar) { if (bipolar != shouldBeBipolar) { bipolar = shouldBeBipolar; repaint(); } }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        theme::drawInset(g, area);
        const auto track = area.reduced(kNudge, 3.0f);
        const auto norm = static_cast<float>((getValue() - getMinimum()) / juce::jmax(1.0e-9, getMaximum() - getMinimum()));
        const auto centre = track.getX() + track.getWidth() * 0.5f;
        const auto x = track.getX() + track.getWidth() * norm;
        g.setColour(colour.withAlpha(isEnabled() ? 0.42f : 0.18f));
        g.fillRect(juce::Rectangle<float>(juce::jmin(centre, x), track.getY(), std::abs(x - centre), track.getHeight()));
        g.setColour(colour);
        g.fillRect(juce::Rectangle<float>(x - 1.0f, track.getY(), 2.0f, track.getHeight()));
        g.setColour(theme::colour::textDim.withAlpha(0.6f));
        g.drawVerticalLine(juce::roundToInt(centre), track.getY(), track.getBottom());
        // Nudge arrows at both ends.
        g.setColour(theme::colour::textSecondary);
        g.setFont(theme::font(theme::Type::label));
        g.drawText(juce::CharPointer_UTF8("\xe2\x80\xb9"), area.withWidth(kNudge), juce::Justification::centred, false);
        g.drawText(juce::CharPointer_UTF8("\xe2\x80\xba"), area.withTrimmedLeft(area.getWidth() - kNudge),
                   juce::Justification::centred, false);
        g.setColour(hasKeyboardFocus(false) ? theme::colour::textValue : theme::colour::textPrimary);
        g.setFont(theme::font(theme::Type::value));
        g.drawText(modAmountText(static_cast<float>(getValue()), bipolar), track, juce::Justification::centred, false);
        if (hasKeyboardFocus(false))
        {
            g.setColour(theme::colour::focus.withAlpha(0.6f));
            g.drawRect(area, 1.0f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.x < kNudge || e.x > getWidth() - kNudge)
        {
            nudge(e.x < kNudge ? -1 : 1, e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isAltDown());
            grabKeyboardFocus();
            return;
        }
        juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.x < static_cast<float>(kNudge) || e.mouseDownPosition.x > static_cast<float>(getWidth() - kNudge)) { return; }
        juce::Slider::mouseDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.x < static_cast<float>(kNudge) || e.mouseDownPosition.x > static_cast<float>(getWidth() - kNudge)) { return; }
        juce::Slider::mouseUp(e);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto delta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (std::abs(delta) < 1.0e-4f) { return; }
        nudge(delta > 0.0f ? 1 : -1, e.mods.isShiftDown());
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto code = key.getKeyCode();
        const auto fine = key.getModifiers().isShiftDown();
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) { nudge(1, fine); return true; }
        if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) { nudge(-1, fine); return true; }
        if (code == juce::KeyPress::returnKey) { beginTextEntry(); return true; }
        return false;
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { beginTextEntry(); }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    // Steps of 1 % (fine: 0.1 %), one host gesture per step.
    void nudge(int direction, bool fine)
    {
        const auto step = fine ? 0.001 : 0.01;
        const auto target = juce::jlimit(getMinimum(), getMaximum(),
                                         std::round((getValue() + direction * step) / 0.001) * 0.001);
        startedDragging();
        setValue(target, juce::sendNotificationSync);
        stoppedDragging();
    }

    void beginTextEntry()
    {
        editor = std::make_unique<juce::TextEditor>();
        editor->setJustification(juce::Justification::centred);
        editor->setFont(theme::font(theme::Type::value));
        editor->setText(juce::String(getValue() * 100.0, 1), false);
        editor->selectAll();
        editor->setBounds(getLocalBounds());
        editor->onReturnKey = [this] { commitText(); };
        editor->onEscapeKey = [this] { closeText(); };
        editor->onFocusLost = [this] { commitText(); };
        addAndMakeVisible(*editor);
        editor->grabKeyboardFocus();
    }

private:
    void commitText()
    {
        if (editor == nullptr) { return; }
        const auto text = editor->getText().retainCharacters("0123456789.-+");
        if (text.containsAnyOf("0123456789"))
        {
            startedDragging();
            setValue(juce::jlimit(getMinimum(), getMaximum(), text.getDoubleValue() / 100.0), juce::sendNotificationSync);
            stoppedDragging();
        }
        closeText();
    }
    void closeText()
    {
        // Deleted after this callback returns: the editor is still on the stack.
        juce::Component::SafePointer<ModAmountControl> self(this);
        juce::MessageManager::callAsync([self] { if (self != nullptr) { self->editor.reset(); self->repaint(); } });
        if (editor != nullptr) { editor->setVisible(false); }
    }

    static constexpr int kNudge = 12;
    juce::Colour colour;
    bool bipolar { false };
    std::unique_ptr<juce::TextEditor> editor;
};
// Unpatch: a bare X, no key behind it - the matrix's routes and the macro
// depth panel's rows both end in one, after the thing it removes.
class UnpatchGlyph final : public juce::Button
{
public:
    UnpatchGlyph() : juce::Button("unpatch") { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto box = getLocalBounds().toFloat().withSizeKeepingCentre(8.0f, 8.0f);
        g.setColour(down ? juce::Colour(0xffff6b6b).darker(0.2f)
                         : over ? juce::Colour(0xffff6b6b) : theme::colour::textSecondary);
        g.drawLine(box.getX(), box.getY(), box.getRight(), box.getBottom(), 1.5f);
        g.drawLine(box.getRight(), box.getY(), box.getX(), box.getBottom(), 1.5f);
    }
};

} // namespace px3::ui
