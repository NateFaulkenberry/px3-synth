#pragma once

#include <JuceHeader.h>

// The PX3 design tokens: colour, type scale, spacing and depth.
//
// One place that says what a panel, a label, a value or a knob looks like, so
// the instrument reads as one system rather than as a set of screens painted
// at different times. Everything that draws chrome (module panels, captions,
// knobs, dropdowns, menus, tabs, tooltips) takes its numbers from here.
//
// Per-module IDENTITY colour (OSC blue, FILTER amber, a card's accent) still
// comes from UIConfig - colour conveys which module and what state; the
// surfaces, the type and the knob family do not vary.
namespace px3::ui::theme
{

// ---- colour ---------------------------------------------------------------
namespace colour
{
// The rack the modules are mounted in, and its rails.
inline const juce::Colour chassis      { 0xff0c0e10 };
inline const juce::Colour rail         { 0xff14171a };
inline const juce::Colour railEdge     { 0xff22262a };

// A module faceplate: a subtle vertical falloff, a dark outer edge and a
// one-pixel lit top edge - depth without gloss.
inline const juce::Colour panelTop     { 0xff22262a };
inline const juce::Colour panelBottom  { 0xff1b1e21 };
inline const juce::Colour panelEdge    { 0xff07080a };
inline const juce::Colour panelLight   { 0x14ffffff };

// Inset regions: displays, graphs, dropdown wells.
inline const juce::Colour inset        { 0xff0f1113 };
inline const juce::Colour insetEdge    { 0xff050607 };
inline const juce::Colour insetLight   { 0x0dffffff };

// Text.
inline const juce::Colour textPrimary   { 0xffe8ebed };
inline const juce::Colour textLabel     { 0xffc3c9ce };
inline const juce::Colour textSecondary { 0xff8b949b };
inline const juce::Colour textDim       { 0xff5d656c };
inline const juce::Colour textValue     { 0xfff3f5f6 };

// Interaction.
inline const juce::Colour focus        { 0xff59b8ff };
inline const juce::Colour hover        { 0x18ffffff };
inline const juce::Colour selection    { 0xff2b4256 };

// Knob family.
inline const juce::Colour knobCapTop    { 0xff4a5056 };
inline const juce::Colour knobCapBottom { 0xff26292d };
inline const juce::Colour knobEdge      { 0xff0a0b0c };
inline const juce::Colour knobTrack     { 0xff0b0c0e };
inline const juce::Colour knobPointer   { 0xfff1f3f4 };
inline const juce::Colour knobAccent    { 0xffe8a24c };
} // namespace colour

// ---- type -----------------------------------------------------------------
// One typeface (the platform UI sans) at a fixed set of roles. A caption is a
// label wherever it sits; a readout is a value wherever it sits.
enum class Type
{
    heading,      // module titles: OSC 1, FILTER 2, DELAY
    tab,          // section navigation
    label,        // control captions: CUTOFF, TIME
    secondary,    // sub-captions, units, hints, sub-section headings
    value,        // knob readouts: 12.0 kHz, 250 ms
    display,      // numbers inside inset displays (graphs, scales)
    control,      // text inside dropdowns and buttons
    menu          // popup menu items
};

float size(Type type) noexcept;
juce::Font font(Type type);
// Letter spacing for the uppercase roles, as JUCE's extra kerning factor.
float tracking(Type type) noexcept;

// ---- spacing and depth -----------------------------------------------------
namespace space
{
inline constexpr float unit = 4.0f;          // the grid everything sits on
inline constexpr float panelRadius = 4.0f;   // module faceplate corners
inline constexpr float insetRadius = 3.0f;   // displays, wells, chips
inline constexpr float headerHeight = 22.0f; // module title band
inline constexpr float powerButton = 20.0f;  // every card's power toggle, one size
inline constexpr float powerInset = 6.0f;    // from the card's left edge
inline constexpr float powerPadding = 4.0f;  // clear above and below it inside the title band
// The title band is at least this tall, so the power button never touches its
// edges; the header grows to fit the button, the button never shrinks.
inline constexpr float minTitleBand = powerButton + 2.0f * powerPadding;
inline constexpr float accentBar = 2.0f;     // identity stripe on a module
} // namespace space

// ---- shared painters --------------------------------------------------------

// A module faceplate with its title band. `accent` is the module's identity
// colour (drawn as a thin stripe and the title's tick); `active` false greys
// the stripe and dims the title, which is how bypass reads everywhere.
void drawModulePanel(juce::Graphics& g,
                     juce::Rectangle<float> bounds,
                     const juce::String& title,
                     juce::Colour accent,
                     bool active,
                     float titleHeight = space::headerHeight);

// A recessed well for a display or a value field.
void drawInset(juce::Graphics& g, juce::Rectangle<float> bounds, float radius = space::insetRadius);

// A printed caption: uppercase, tracked, fitted (never ellipsised).
void drawLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
               Type type, juce::Colour colour,
               juce::Justification justification = juce::Justification::centred);

// The rendered width of `text` in a role, so tests and layout can check fit.
float textWidth(const juce::String& text, Type type);

} // namespace px3::ui::theme

namespace px3::ui
{
// The instrument's look-and-feel for everything that is not a knob: labels,
// dropdowns, popup menus, buttons, toggles, tooltips and scrollbars.
//
// Set on the editor, so every child without its own look inherits it. The
// knob look derives from it, so a knob's popup and text box match too.
class InstrumentLookAndFeel : public juce::LookAndFeel_V4
{
public:
    InstrumentLookAndFeel();

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;
    void drawBubble(juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                    const juce::Rectangle<float>& body) override;
    juce::Font getSliderPopupFont(juce::Slider&) override;
    int getSliderPopupPlacement(juce::Slider&) override;
};
} // namespace px3::ui
