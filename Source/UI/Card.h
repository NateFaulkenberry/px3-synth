#pragma once

#include <JuceHeader.h>

class UIConfig;

namespace px3::ui
{
// A small, deliberately closed styling model for PX3's Panels and Cards.
//
// Every field here is read by the layout or rendering code in Card.cpp. Nothing
// is exposed in UIConfig.json that is not consumed - the previous system had
// properties that looked configurable and did nothing, and that is the specific
// failure this replaces.
//
// The model is three levels:
//
//     Panel  - a full-width section: height, and whether it scrolls
//     Card   - the standard component frame: box, border, background, gloss, title
//     (later) internal control styling, which is deliberately NOT modelled here
//
// Card internals are out of scope by design. Keeping "the frame" and "what is
// inside the frame" separate is what will let control styling be added later
// without disturbing any of this.

// A length that is either an absolute pixel count or a percentage of a
// reference extent. "auto" means "use the space the layout already gave me",
// which is the default and preserves existing behaviour when unspecified.
struct Dimension
{
    enum class Unit { automatic, pixels, percent };

    Unit unit { Unit::automatic };
    float value { 0.0f };

    // Accepts 300, "300", "300px", "33%", or "auto".
    static Dimension parse(const juce::var& value, Dimension fallback);

    // `reference` is the extent a percentage is measured against; `available`
    // is what "auto" resolves to. The caller decides what those mean, and the
    // Card system is explicit about it: percentages always reference the parent
    // PANEL's content box, never the slot, the sibling, or the card itself.
    float resolve(float reference, float available) const;

    bool isAuto() const { return unit == Unit::automatic; }
};

struct Insets
{
    float top { 0.0f };
    float right { 0.0f };
    float bottom { 0.0f };
    float left { 0.0f };

    // Accepts a single number (all sides) or {top,right,bottom,left}.
    static Insets parse(const juce::var& value, Insets fallback);

    juce::Rectangle<float> shrink(juce::Rectangle<float> r) const;
    float horizontal() const { return left + right; }
    float vertical() const { return top + bottom; }
};

struct Fill
{
    juce::Colour colour { juce::Colours::white };
    float opacity { 0.0f };

    juce::Colour effective() const { return colour.withAlpha(juce::jlimit(0.0f, 1.0f, opacity)); }
};

struct BorderStyle
{
    bool enabled { true };
    float width { 1.2f };
    juce::Colour colour { juce::Colour::fromRGB(220, 232, 252) };
    float opacity { 0.35f };
    float radius { 8.0f };
};

// Two fills inset from the card by their own margin, which is what produces the
// visible gap between the border and the gloss.
struct GlossStyle
{
    float margin { 6.0f };
    // Where the top fill ends and the bottom fill begins, as a fraction of the
    // gloss box height. Without this the two fills could only ever be halves.
    float split { 0.5f };
    Fill topFill;
    Fill bottomFill;
};

struct TitleStyle
{
    float fontSize { 11.0f };
    juce::Colour colour { juce::Colour::fromRGB(220, 232, 252) };
    juce::Justification align { juce::Justification::centredTop };
    // Vertical offset in pixels from the top of the card's padding box.
    // Negative moves the title up, positive moves it down.
    float y { 0.0f };
    float height { 14.0f };
};

// How a card looks when its component is bypassed. Two properties, both read
// by CardStyle::disabledVariant.
struct DisabledStyle
{
    // 0 = fully greyscale. Kept configurable rather than hard-coded because
    // "how grey is bypassed" is a look, and looks belong in the config.
    float saturation { 0.0f };
    // Multiplies every layer's opacity, so a bypassed card recedes as well as
    // desaturating. 1.0 leaves the card at full strength in grey.
    float dim { 0.75f };
};

struct CardStyle
{
    Dimension width;
    Dimension height;
    Insets margin;
    Insets padding { 10.0f, 10.0f, 10.0f, 10.0f };

    BorderStyle border;
    Fill background { juce::Colours::black, 0.10f };
    GlossStyle gloss;
    TitleStyle title;
    DisabledStyle disabled;

    // Reads `defaultsPath` then overlays `stylePath`, so a card only declares
    // what differs from the shared default.
    // Two overloads rather than a defaulted argument: a default of {} inside
    // the class body needs CardStyle to be complete, which it is not yet.
    static CardStyle fromConfig(const UIConfig* config,
                                const juce::String& defaultsPath,
                                const juce::String& stylePath,
                                const CardStyle& fallback);
    static CardStyle fromConfig(const UIConfig* config,
                                const juce::String& defaultsPath,
                                const juce::String& stylePath);

    // The card's outer box.
    //
    //   slot         - the bounds the panel gave this component
    //   panelContent - the parent panel's content box, and the ONLY reference
    //                  a percentage width or height is measured against
    //
    // The resolved box is centred in the slot, so a card narrower than its slot
    // sits in the middle of it rather than hugging one edge.
    //
    // A card never exceeds the slot it was given - the equivalent of CSS
    // `max-width: 100%`. A percentage larger than the slot is therefore capped
    // rather than overflowing into the neighbouring column. This is a stated
    // rule, not an accident: overflow between columns looks like a bug, and the
    // cap is what keeps fixed pixel widths sensible as the window narrows.
    juce::Rectangle<float> resolveBounds(juce::Rectangle<float> slot,
                                         juce::Rectangle<float> panelContent) const;

    // The same style with every colour desaturated and every layer dimmed, for
    // a bypassed component. Returned rather than drawn so the transform can be
    // tested directly instead of only being visible on screen.
    CardStyle disabledVariant() const;

    // Inside the border, after padding. This is where a component lays out its
    // own controls, and it is the only geometry a component needs from the card.
    juce::Rectangle<float> contentBounds(juce::Rectangle<float> cardBounds) const;
};

struct PanelStyle
{
    // Panels are full width by definition, so only height and scrolling are
    // modelled. Anything else would be a property with no consumer.
    int height { 0 };            // 0 means "use whatever the editor allocates"
    bool scrollVertically { false };

    static PanelStyle fromConfig(const UIConfig* config, const juce::String& path,
                                 const PanelStyle& fallback);
    static PanelStyle fromConfig(const UIConfig* config, const juce::String& path);
};

// Holds a parsed CardStyle and re-parses it whenever the configuration object
// changes.
//
// This exists because of a real bug: components parsed their style in resized()
// and a live config reload only called repaint(), so the reload stored the new
// config and then painted with the style parsed from the old one. Every
// property looked configurable and nothing moved - the exact failure this
// system was built to remove.
//
// Keying invalidation on the UIConfig pointer makes that impossible to repeat.
// UIConfigManager builds a new UIConfig on every reload, so a changed pointer
// IS a changed file, and a component cannot forget to refresh.
class CardStyleCache
{
public:
    void setKeys(juce::String defaultsPathIn, juce::String stylePathIn);
    void setConfig(std::shared_ptr<const UIConfig> configIn);

    // Always current: re-parses if the config or the keys have changed since
    // the last call, so callers never hold a stale style.
    const CardStyle& style() const;

private:
    std::shared_ptr<const UIConfig> config;
    juce::String defaultsPath { "cards.defaults" };
    juce::String stylePath;

    mutable CardStyle cached;
    mutable const UIConfig* parsedFrom { nullptr };
    mutable juce::String parsedStylePath;
    mutable bool hasParsed { false };
};

// Everything a component needs to host a Card, in one member.
//
// Without this each component repeats the same four fields and the same
// resolve-then-draw sequence, and every repetition is a chance to get the
// invalidation or the percentage reference subtly wrong - which is how the
// stale-style bug happened the first time.
//
// Typical use:
//
//     resized()  { card.layout(getLocalBounds()); auto area = card.content(); ... }
//     paint(g)   { card.draw(g, "OSC 1"); ... }
//
class CardHost
{
public:
    // The block under `cards` this component reads, e.g. "osc1".
    void setStyleKey(const juce::String& key);
    void setConfig(std::shared_ptr<const UIConfig> config);
    // The parent panel's content box: the reference for percentage dimensions.
    void setPanelContentBounds(juce::Rectangle<int> panelContent);

    // Resolves the card box from the component's own bounds. Call from resized().
    void layout(juce::Rectangle<int> componentBounds);

    const CardStyle& style() const { return cache.style(); }
    juce::Rectangle<float> bounds() const { return cardBounds; }
    // Inside the border and padding: where the component lays out its controls.
    juce::Rectangle<float> content() const { return cache.style().contentBounds(cardBounds); }
    // content(), minus the space the title occupies.
    juce::Rectangle<int> contentBelowTitle() const;

    void draw(juce::Graphics& g, const juce::String& title) const;
    // For a bypassed component: greyscale and dimmed. Bypass is runtime state,
    // so it is applied to the parsed style rather than being a separate style
    // block that could drift out of sync with the active one.
    void drawInactive(juce::Graphics& g, const juce::String& title) const;

private:
    CardStyleCache cache;
    juce::Rectangle<int> panelContentBounds;
    juce::Rectangle<int> lastComponentBounds;
    juce::Rectangle<float> cardBounds;
};

// Draws border, background, gloss and title. Nothing else: what goes inside the
// card is the component's business.
void drawCard(juce::Graphics& g,
              juce::Rectangle<float> cardBounds,
              const CardStyle& style,
              const juce::String& title);

} // namespace px3::ui
