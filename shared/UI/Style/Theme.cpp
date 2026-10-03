#include "Theme.h"

namespace px3::ui::theme
{

float size(Type type) noexcept
{
    switch (type)
    {
        case Type::heading:   return 11.5f;
        case Type::tab:       return 11.0f;
        case Type::label:     return 10.0f;
        case Type::secondary: return 9.0f;
        case Type::value:     return 10.5f;
        case Type::display:   return 9.5f;
        case Type::control:   return 11.0f;
        case Type::menu:      return 13.0f;
    }
    return 10.0f;
}

float tracking(Type type) noexcept
{
    switch (type)
    {
        case Type::heading:   return 0.10f;
        case Type::tab:       return 0.08f;
        case Type::label:     return 0.06f;
        case Type::secondary: return 0.06f;
        default:              return 0.0f;
    }
}

juce::Font font(Type type)
{
    const auto bold = type == Type::heading || type == Type::tab || type == Type::label;
    auto options = juce::FontOptions(size(type), bold ? juce::Font::bold : juce::Font::plain);
    if (type == Type::display)
    {
        options = options.withName(juce::Font::getDefaultMonospacedFontName());
    }
    return juce::Font(options).withExtraKerningFactor(tracking(type));
}

float textWidth(const juce::String& text, Type type)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font(type), text, 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}

void drawInset(juce::Graphics& g, juce::Rectangle<float> bounds, float radius)
{
    if (bounds.isEmpty()) { return; }
    g.setColour(colour::inset);
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(colour::insetEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
    // Lit bottom lip: the well reads as recessed into the faceplate.
    g.setColour(colour::insetLight);
    g.drawHorizontalLine(juce::roundToInt(bounds.getBottom()) - 1,
                         bounds.getX() + radius, bounds.getRight() - radius);
}

void drawLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
               Type type, juce::Colour textColour, juce::Justification justification)
{
    if (text.isEmpty() || area.isEmpty()) { return; }
    g.setColour(textColour);
    g.setFont(font(type));
    // Fitted, never ellipsised: a caption shrunk by a few percent still reads,
    // a truncated one does not.
    g.drawFittedText(text, area.toNearestInt(), justification, 1, 0.72f);
}

void drawModulePanel(juce::Graphics& g,
                     juce::Rectangle<float> bounds,
                     const juce::String& title,
                     juce::Colour accent,
                     bool active,
                     float titleHeight)
{
    if (bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f) { return; }

    const auto r = space::panelRadius;

    // Faceplate: a quiet top-to-bottom falloff, nothing glossy.
    g.setGradientFill(juce::ColourGradient(colour::panelTop, 0.0f, bounds.getY(),
                                           colour::panelBottom, 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, r);

    // Edge and the lit top bevel.
    g.setColour(colour::panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), r, 1.0f);
    g.setColour(colour::panelLight);
    g.drawHorizontalLine(juce::roundToInt(bounds.getY()) + 1, bounds.getX() + r, bounds.getRight() - r);

    if (titleHeight <= 0.0f) { return; }

    // Title band: a slightly darker strip with the module's identity stripe.
    const auto band = bounds.withHeight(juce::jmin(titleHeight, bounds.getHeight()));
    g.setColour(juce::Colours::black.withAlpha(0.16f));
    g.fillRect(band.reduced(1.0f, 0.0f).withTrimmedTop(2.0f));
    g.setColour(colour::panelEdge.withAlpha(0.8f));
    g.drawHorizontalLine(juce::roundToInt(band.getBottom()), bounds.getX() + 1.0f, bounds.getRight() - 1.0f);

    const auto stripe = active ? accent : accent.withSaturation(0.0f).withMultipliedBrightness(0.55f);
    g.setColour(stripe.withAlpha(active ? 0.95f : 0.6f));
    juce::Path bar;
    bar.addRoundedRectangle(bounds.getX() + 1.0f, bounds.getY() + 1.0f, bounds.getWidth() - 2.0f,
                            space::accentBar, juce::jmax(0.0f, r - 1.0f), juce::jmax(0.0f, r - 1.0f), true, true, false, false);
    g.fillPath(bar);

    if (title.isNotEmpty())
    {
        drawLabel(g, title.toUpperCase(), band.reduced(28.0f, 0.0f).withTrimmedTop(2.0f), Type::heading,
                  active ? colour::textPrimary : colour::textDim);
    }
}

} // namespace px3::ui::theme

namespace px3::ui
{
using namespace theme;

InstrumentLookAndFeel::InstrumentLookAndFeel()
{
    setColour(juce::ComboBox::backgroundColourId, colour::inset);
    setColour(juce::ComboBox::textColourId, colour::textPrimary);
    setColour(juce::ComboBox::outlineColourId, colour::insetEdge);
    setColour(juce::ComboBox::arrowColourId, colour::textSecondary);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff181b1e));
    setColour(juce::PopupMenu::textColourId, colour::textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colour::selection);
    setColour(juce::PopupMenu::highlightedTextColourId, colour::textValue);
    setColour(juce::PopupMenu::headerTextColourId, colour::textSecondary);
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xf01a1d20));
    setColour(juce::TooltipWindow::textColourId, colour::textPrimary);
    setColour(juce::TooltipWindow::outlineColourId, colour::railEdge);
    setColour(juce::Label::textColourId, colour::textLabel);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25292d));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff35414c));
    setColour(juce::TextButton::textColourOffId, colour::textLabel);
    setColour(juce::TextButton::textColourOnId, colour::textValue);
    setColour(juce::ScrollBar::thumbColourId, juce::Colour(0x40ffffff));
    setColour(juce::BubbleComponent::backgroundColourId, juce::Colour(0xf0121416));
    setColour(juce::BubbleComponent::outlineColourId, colour::railEdge);
    setColour(juce::TextEditor::backgroundColourId, colour::inset);
    setColour(juce::TextEditor::textColourId, colour::textPrimary);
    setColour(juce::TextEditor::outlineColourId, colour::insetEdge);
    setColour(juce::TextEditor::focusedOutlineColourId, colour::focus.withAlpha(0.6f));
    setColour(juce::ListBox::backgroundColourId, colour::inset);
}

juce::Font InstrumentLookAndFeel::getLabelFont(juce::Label& label)
{
    // A label keeps the size its owner chose only within the scale's range, so
    // the 15 literal sizes the panels used collapse onto the roles.
    const auto requested = label.getFont().getHeight();
    if (requested >= 14.0f) { return label.getFont(); }
    if (requested >= 12.5f) { return font(Type::heading); }
    return label.getFont().isBold() ? font(Type::label) : font(Type::value);
}

juce::Font InstrumentLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return font(Type::control).withHeight(juce::jmin(size(Type::control),
                                                     static_cast<float>(box.getHeight()) * 0.62f));
}

juce::Font InstrumentLookAndFeel::getPopupMenuFont() { return font(Type::menu); }

juce::Font InstrumentLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return font(Type::control).withHeight(juce::jmin(size(Type::control), static_cast<float>(buttonHeight) * 0.6f));
}

void InstrumentLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                         int, int, int, int, juce::ComboBox& box)
{
    auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    drawInset(g, area, space::insetRadius);

    if (box.isMouseOver(true) && box.isEnabled())
    {
        g.setColour(colour::hover);
        g.fillRoundedRectangle(area.reduced(1.0f), space::insetRadius);
    }
    if (box.hasKeyboardFocus(true))
    {
        g.setColour(colour::focus.withAlpha(0.5f));
        g.drawRoundedRectangle(area.reduced(0.5f), space::insetRadius, 1.0f);
    }

    // A small chevron at the right, drawn rather than JUCE's filled triangle.
    const auto arrowZone = area.removeFromRight(juce::jmin(16.0f, area.getHeight()));
    const auto c = arrowZone.getCentre();
    juce::Path chevron;
    chevron.startNewSubPath(c.x - 3.0f, c.y - 1.5f);
    chevron.lineTo(c.x, c.y + 1.5f);
    chevron.lineTo(c.x + 3.0f, c.y - 1.5f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath(chevron, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void InstrumentLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(4, 0, juce::jmax(0, box.getWidth() - 4 - juce::jmin(16, box.getHeight())), box.getHeight());
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
    label.setMinimumHorizontalScale(0.7f);
}

void InstrumentLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    g.setColour(colour::railEdge);
    g.drawRect(0, 0, width, height, 1);
}

void InstrumentLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle(area, space::insetRadius);
    g.setColour(findColour(juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle(area.reduced(0.5f), space::insetRadius, 1.0f);

    juce::AttributedString s;
    s.setJustification(juce::Justification::centredLeft);
    s.append(text, juce::Font(juce::FontOptions(12.0f)), findColour(juce::TooltipWindow::textColourId));
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths(s, 300.0f);
    layout.draw(g, area.reduced(8.0f, 5.0f));
}

juce::Rectangle<int> InstrumentLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                             juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.append(tipText, juce::Font(juce::FontOptions(12.0f)));
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths(s, 300.0f);
    const auto w = static_cast<int>(layout.getWidth() + 18.0f);
    const auto h = static_cast<int>(layout.getHeight() + 12.0f);
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                w, h)
        .constrainedWithin(parentArea);
}

void InstrumentLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                          bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                          bool isMouseOver, bool isMouseDown)
{
    auto thumb = isScrollbarVertical
                     ? juce::Rectangle<int>(x, thumbStartPosition, width, thumbSize)
                     : juce::Rectangle<int>(thumbStartPosition, y, thumbSize, height);
    thumb = thumb.reduced(isScrollbarVertical ? 2 : 0, isScrollbarVertical ? 0 : 2);
    g.setColour(juce::Colours::white.withAlpha(isMouseDown ? 0.38f : (isMouseOver ? 0.28f : 0.16f)));
    g.fillRoundedRectangle(thumb.toFloat(), 2.5f);
}

void InstrumentLookAndFeel::drawBubble(juce::Graphics& g, juce::BubbleComponent& bubble,
                                       const juce::Point<float>&, const juce::Rectangle<float>& body)
{
    g.setColour(bubble.findColour(juce::BubbleComponent::backgroundColourId));
    g.fillRoundedRectangle(body, space::insetRadius);
    g.setColour(bubble.findColour(juce::BubbleComponent::outlineColourId));
    g.drawRoundedRectangle(body.reduced(0.5f), space::insetRadius, 1.0f);
}

juce::Font InstrumentLookAndFeel::getSliderPopupFont(juce::Slider&) { return font(Type::value); }

int InstrumentLookAndFeel::getSliderPopupPlacement(juce::Slider&) { return juce::BubbleComponent::above; }

} // namespace px3::ui
